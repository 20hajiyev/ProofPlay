#include "racer/Recovery.h"

#include <cmath>

namespace racer
{
    void RecoveryMonitor::Reset(const float p[3], float heading)
    {
        anchor_[0] = p[0];
        anchor_[1] = p[1];
        anchor_[2] = p[2];
        anchor_heading_ = heading;
        has_anchor_ = true;
        since_anchor_ = upside_down_ = stuck_ = 0.0f;
    }

    RecoveryRequest RecoveryMonitor::Update(const VehicleTelemetry& t, float dt, bool player_wants_progress)
    {
        RecoveryRequest r;
        const bool grounded = t.wheel_contact[0] && t.wheel_contact[1] && t.wheel_contact[2] && t.wheel_contact[3];

        since_anchor_ += dt;
        // Only a place the car is actually driving through is safe; a stuck spot is not.
        const bool moving = t.forward_speed_kmh > tuning_.anchor_min_speed_kmh;
        if (grounded && moving && t.up_y > 0.9f && since_anchor_ >= tuning_.safe_sample_seconds)
        {
            anchor_[0] = t.position[0];
            anchor_[1] = t.position[1];
            anchor_[2] = t.position[2];
            anchor_heading_ = t.heading_rad;
            has_anchor_ = true;
            since_anchor_ = 0.0f;
        }

        upside_down_ = t.up_y < 0.3f ? upside_down_ + dt : 0.0f;
        // Only "stuck" if the driver is trying to move; a parked car is not stuck.
        stuck_ = (player_wants_progress && std::fabs(t.forward_speed_kmh) < tuning_.stuck_speed_kmh) ? stuck_ + dt : 0.0f;

        if (!IsTelemetrySane(t) || t.position[1] < tuning_.fall_below_y)
            r.reason = RecoveryReason::Fell;
        else if (upside_down_ >= tuning_.upside_down_seconds)
            r.reason = RecoveryReason::UpsideDown;
        else if (stuck_ >= tuning_.stuck_seconds)
            r.reason = RecoveryReason::Stuck;

        if (r.reason != RecoveryReason::None && has_anchor_)
        {
            r.position[0] = anchor_[0];
            r.position[1] = anchor_[1];
            r.position[2] = anchor_[2];
            r.heading_rad = anchor_heading_;
            upside_down_ = stuck_ = 0.0f;
        }
        else if (r.reason != RecoveryReason::None)
        {
            r.reason = RecoveryReason::None; // nowhere safe to go yet; keep watching
        }
        return r;
    }
}
