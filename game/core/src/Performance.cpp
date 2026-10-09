#include "racer/Performance.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    namespace
    {
        PerformanceLevel L(const char* id) { PerformanceLevel l; l.id = id; return l; }
        PerformanceLevel Torque(const char* id, float t, float rpm = 0.0f, float inertia = 1.0f)
        {
            PerformanceLevel l = L(id);
            l.torque = t, l.rpm = rpm, l.inertia = inertia;
            return l;
        }
    }

    const std::vector<PerformanceSlot>& PerformanceSlots()
    {
        static const std::vector<PerformanceSlot> slots = [] {
            std::vector<PerformanceSlot> s;
            // engine internals: ported head, cams, forged pistons; higher rev limit, lighter rotating mass
            s.push_back({ "engine", { L("stock"), Torque("stage1", 1.08f), Torque("stage2", 1.16f, 300.0f, 0.95f), Torque("stage3", 1.26f, 600.0f, 0.88f) } });
            PerformanceLevel street = Torque("street", 1.15f), race = Torque("race", 1.32f);
            street.low_end = 0.85f, race.low_end = 0.62f;
            s.push_back({ "turbo", { L("none"), street, race } });
            s.push_back({ "intake", { L("stock"), Torque("cold_air", 1.03f) } });
            s.push_back({ "ecu", { L("stock"), Torque("tuned", 1.05f, 400.0f) } });
            s.push_back({ "exhaust_sys", { L("stock"), Torque("sport", 1.04f), Torque("straight", 1.07f) } });
            PerformanceLevel close = L("close"), seq = L("sequential");
            close.final_drive = 1.08f, close.shift = 0.75f;
            seq.final_drive = 1.08f, seq.shift = 0.45f;
            s.push_back({ "gearbox", { L("stock"), close, seq } });
            PerformanceLevel ss = L("sport"), sr = L("race");
            ss.susp = 1.2f, ss.grip = 1.02f, ss.ride = -0.015f;
            sr.susp = 1.45f, sr.grip = 1.04f, sr.ride = -0.03f; // coilovers sit the car lower
            s.push_back({ "suspension", { L("stock"), ss, sr } });
            PerformanceLevel bs = L("sport"), br = L("race");
            bs.brake = 1.25f, br.brake = 1.5f;
            s.push_back({ "brakes", { L("stock"), bs, br } });
            PerformanceLevel ts = L("sport"), tr = L("semi_slick");
            ts.grip = 1.07f, tr.grip = 1.15f;
            s.push_back({ "tyres", { L("street"), ts, tr } });
            PerformanceLevel wl = L("light"), wr = L("stripped");
            wl.mass = -60.0f, wr.mass = -140.0f, wr.health = -10.0f;
            s.push_back({ "weight", { L("stock"), wl, wr } });
            return s;
        }();
        return slots;
    }

    static const PerformanceSlot* FindSlot(const std::string& slot)
    {
        for (const PerformanceSlot& s : PerformanceSlots())
            if (s.id == slot)
                return &s;
        return nullptr;
    }

    static const PerformanceLevel* Chosen(const CarSetup& setup, const PerformanceSlot& slot)
    {
        const auto it = setup.parts.find(kPerformancePrefix + slot.id);
        if (it != setup.parts.end())
            for (const PerformanceLevel& l : slot.levels)
                if (l.id == it->second)
                    return &l;
        return &slot.levels.front(); // absent or no longer offered: stock
    }

    std::string ChosenPerformance(const CarSetup& s, const std::string& slot)
    {
        const PerformanceSlot* p = FindSlot(slot);
        return p ? Chosen(s, *p)->id : std::string();
    }

    void CyclePerformance(CarSetup& s, const std::string& slot, int dir)
    {
        const PerformanceSlot* p = FindSlot(slot);
        if (!p || dir == 0)
            return;
        const int n = int(p->levels.size());
        const int cur = int(Chosen(s, *p) - p->levels.data());
        const int next = ((cur + dir) % n + n) % n;
        if (next == 0)
            s.parts.erase(kPerformancePrefix + slot);
        else
            s.parts[kPerformancePrefix + slot] = p->levels[size_t(next)].id;
    }

    VehicleDefinition ApplyPerformance(const VehicleDefinition& base, const CarSetup& setup)
    {
        PerformanceLevel sum;
        sum.low_end = 1.0f;
        for (const PerformanceSlot& slot : PerformanceSlots())
        {
            const PerformanceLevel& l = *Chosen(setup, slot);
            sum.torque *= l.torque;
            sum.low_end = std::min(sum.low_end, l.low_end);
            sum.rpm += l.rpm;
            sum.inertia *= l.inertia;
            sum.mass += l.mass;
            sum.grip *= l.grip;
            sum.brake *= l.brake;
            sum.susp *= l.susp;
            sum.final_drive *= l.final_drive;
            sum.shift *= l.shift;
            sum.health += l.health;
            sum.ride += l.ride;
        }
        VehicleDefinition d = base;
        // a higher rev limit stretches the torque curve over the wider range
        if (sum.rpm > 0.0f && base.max_rpm > base.min_rpm)
        {
            const float k = (base.max_rpm + sum.rpm - base.min_rpm) / (base.max_rpm - base.min_rpm);
            for (TorquePoint& p : d.torque_curve)
                p.rpm = base.min_rpm + (p.rpm - base.min_rpm) * k;
            d.max_rpm = base.max_rpm + sum.rpm;
            d.shift_up_rpm = base.shift_up_rpm + sum.rpm;
        }
        // turbo lag: off boost the torque falls to low_end at idle, full from ~45% of the rev range
        if (sum.low_end < 1.0f)
        {
            const float boost = d.min_rpm + 0.45f * (d.max_rpm - d.min_rpm);
            for (TorquePoint& p : d.torque_curve)
            {
                const float t = std::clamp((p.rpm - d.min_rpm) / std::max(1.0f, boost - d.min_rpm), 0.0f, 1.0f);
                p.normalized *= sum.low_end + (1.0f - sum.low_end) * t;
            }
        }
        const float torque = std::min(sum.torque, kMaxTorqueGain);
        d.max_engine_torque_nm = base.max_engine_torque_nm * torque;
        d.engine_inertia_kgm2 = base.engine_inertia_kgm2 * sum.inertia;
        d.mass_kg = std::max(base.mass_kg * 0.75f, base.mass_kg + sum.mass);
        d.tires.lateral_grip = base.tires.lateral_grip * sum.grip;
        d.tires.longitudinal_grip = base.tires.longitudinal_grip * sum.grip;
        d.brake_torque_nm = base.brake_torque_nm * sum.brake;
        d.front_suspension.frequency_hz = base.front_suspension.frequency_hz * sum.susp;
        d.rear_suspension.frequency_hz = base.rear_suspension.frequency_hz * sum.susp;
        for (SuspensionDef* sd : { &d.front_suspension, &d.rear_suspension })
            sd->max_length_m = std::max(sd->min_length_m + 0.12f, sd->max_length_m + sum.ride);
        d.final_drive = base.final_drive * sum.final_drive;
        d.shift_time_s = base.shift_time_s * sum.shift;
        d.health = std::max(base.health * 0.5f, base.health + sum.health);
        // top speed (used to match AI fields): drag-limited, so it grows with the cube root of power;
        // a shorter final drive costs a little of it
        const float power = torque * (d.max_rpm / std::max(1.0f, base.max_rpm));
        d.top_speed_target_kmh = base.top_speed_target_kmh * std::cbrt(power) * std::pow(1.0f / sum.final_drive, 0.3f);
        return d;
    }

    int PerformanceIndex(const VehicleDefinition& d)
    {
        // power-to-weight (Nm x rpm / kg) carries most of it; grip and brakes the rest. Scaled so a
        // stock class D car reads ~400 and a full build stays under the 999 ceiling (D01: 835 -> 999
        // clipped with the first weights)
        const float pw = d.max_engine_torque_nm * d.max_rpm / std::max(1.0f, d.mass_kg);
        const float grip = 0.5f * (d.tires.lateral_grip + d.tires.longitudinal_grip);
        const float brake = d.brake_torque_nm / std::max(1.0f, d.mass_kg);
        const float pi = 0.26f * pw + 110.0f * grip + 20.0f * brake;
        return std::clamp(int(std::lround(pi)), 100, 999);
    }
}
