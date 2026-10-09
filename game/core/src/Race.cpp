#include "racer/Race.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    namespace
    {
        float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
        Vec2 Sub(Vec2 a, Vec2 b) { return { a.x - b.x, a.z - b.z }; }
        float Len(Vec2 a) { return std::sqrt(Dot(a, a)); }
    }

    TrackRoute::TrackRoute(std::vector<Vec2> centerline, float half_width_m)
        : points_(std::move(centerline)), half_width_(half_width_m)
    {
        cumulative_.resize(points_.size() + 1);
        cumulative_[0] = 0.0f;
        for (size_t i = 0; i < points_.size(); ++i)
            cumulative_[i + 1] = cumulative_[i] + Len(Sub(points_[(i + 1) % points_.size()], points_[i]));
        length_ = cumulative_.back();
    }

    TrackRoute::TrackRoute(std::vector<Vec2> centerline, float half_width_m, std::vector<float> elevation, std::vector<int> drops)
        : TrackRoute(std::move(centerline), half_width_m)
    {
        if (elevation.size() == points_.size())
            elevation_ = std::move(elevation);
        drops_ = std::move(drops);
    }

    bool TrackRoute::DeckOnSegment(size_t i) const
    {
        if (elevation_.empty() || i >= points_.size())
            return false;
        if (std::find(drops_.begin(), drops_.end(), int(i)) != drops_.end())
            return false;
        return elevation_[i] > 0.0f || elevation_[(i + 1) % points_.size()] > 0.0f;
    }

    float TrackRoute::ElevationAt(float s) const
    {
        if (elevation_.empty())
            return 0.0f;
        s = Ahead(0.0f, s);
        const size_t i = SegmentAt(s);
        const float seg = cumulative_[i + 1] - cumulative_[i];
        const float t = seg > 0.0f ? (s - cumulative_[i]) / seg : 0.0f;
        if (std::find(drops_.begin(), drops_.end(), int(i)) != drops_.end())
            return t <= 1e-4f ? elevation_[i] : 0.0f; // past the lip: the ground under the gap
        return elevation_[i] + (elevation_[(i + 1) % points_.size()] - elevation_[i]) * t;
    }

    float TrackRoute::Ahead(float from_s, float to_s) const
    {
        float d = std::fmod(to_s - from_s, length_);
        return d < 0.0f ? d + length_ : d;
    }

    size_t TrackRoute::SegmentAt(float s) const
    {
        s = Ahead(0.0f, s);
        const auto it = std::upper_bound(cumulative_.begin(), cumulative_.end(), s);
        return std::min(points_.size() - 1, size_t(std::max<std::ptrdiff_t>(0, (it - cumulative_.begin()) - 1)));
    }

    Vec2 TrackRoute::PointAt(float s) const
    {
        s = Ahead(0.0f, s);
        const size_t i = SegmentAt(s);
        const Vec2 a = points_[i], b = points_[(i + 1) % points_.size()];
        const float seg = cumulative_[i + 1] - cumulative_[i];
        const float t = seg > 0.0f ? (s - cumulative_[i]) / seg : 0.0f;
        return { a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t };
    }

    Vec2 TrackRoute::DirectionAt(float s) const
    {
        const size_t i = SegmentAt(s);
        const Vec2 d = Sub(points_[(i + 1) % points_.size()], points_[i]);
        const float l = Len(d);
        return l > 0.0f ? Vec2{ d.x / l, d.z / l } : Vec2{ 0, 1 };
    }

    TrackRoute::Projection TrackRoute::Project(Vec2 p, float hint_s, float window_m) const
    {
        Projection best;
        float best_d2 = 1e30f;
        for (size_t i = 0; i < points_.size(); ++i)
        {
            const float seg_len = cumulative_[i + 1] - cumulative_[i];
            if (hint_s >= 0.0f)
            {
                // Skip segments whose span is farther than the window from the hint (circular).
                const float mid = cumulative_[i] + seg_len * 0.5f;
                const float d = std::min(Ahead(hint_s, mid), Ahead(mid, hint_s));
                if (d > window_m + seg_len * 0.5f)
                    continue;
            }
            const Vec2 a = points_[i], b = points_[(i + 1) % points_.size()];
            const Vec2 ab = Sub(b, a);
            const float t = seg_len > 0.0f ? std::clamp(Dot(Sub(p, a), ab) / (seg_len * seg_len), 0.0f, 1.0f) : 0.0f;
            const Vec2 q{ a.x + ab.x * t, a.z + ab.z * t };
            const Vec2 pq = Sub(p, q);
            const float d2 = Dot(pq, pq);
            if (d2 < best_d2)
            {
                best_d2 = d2;
                const Vec2 dir = seg_len > 0.0f ? Vec2{ ab.x / seg_len, ab.z / seg_len } : Vec2{ 0, 1 };
                best.s = Ahead(0.0f, cumulative_[i] + seg_len * t);
                best.direction = dir;
                // Right of travel for +Y up, +Z forward, +X right: right = (dir.z, -dir.x).
                best.lateral = pq.x * dir.z - pq.z * dir.x;
            }
        }
        if (hint_s >= 0.0f && best_d2 >= 1e30f)
            return Project(p, -1.0f);
        return best;
    }

    const char* ToString(RacePhase p)
    {
        switch (p)
        {
        case RacePhase::Loading: return "Loading";
        case RacePhase::Grid: return "Grid";
        case RacePhase::Countdown: return "Countdown";
        case RacePhase::Racing: return "Racing";
        case RacePhase::Finished: return "Finished";
        case RacePhase::Results: return "Results";
        case RacePhase::Paused: return "Paused";
        case RacePhase::Restarting: return "Restarting";
        case RacePhase::Aborted: return "Aborted";
        }
        return "?";
    }

    std::string ValidateRaceConfig(const TrackRoute& route, const RaceConfig& c)
    {
        if (route.Length() <= 0.0f)
            return "route has no length";
        if (c.laps < 1)
            return "laps must be >= 1";
        if (c.checkpoints_s.size() < 2)
            return "need the finish line plus at least one checkpoint";
        if (c.checkpoints_s[0] != 0.0f)
            return "checkpoint 0 must be the finish line at s = 0";
        for (size_t i = 1; i < c.checkpoints_s.size(); ++i)
            if (!(c.checkpoints_s[i] > c.checkpoints_s[i - 1]) || c.checkpoints_s[i] >= route.Length())
                return "checkpoints must be strictly increasing and inside the route";
        return {};
    }

    RaceRules::RaceRules(const TrackRoute& route, RaceConfig config) : route_(route), config_(std::move(config)) {}

    int RaceRules::AddParticipant(Vec2 grid_position)
    {
        ParticipantState p;
        p.id = int(participants_.size());
        p.next_checkpoint = 0; // must cross the start/finish line to begin lap 1
        p.s = p.last_s = route_.Project(grid_position).s;
        participants_.push_back(p);
        grid_.push_back(grid_position);
        return p.id;
    }

    void RaceRules::Update(int id, Vec2 position, uint64_t tick)
    {
        ParticipantState& p = participants_[size_t(id)];
        const TrackRoute::Projection proj = route_.Project(position, p.s);
        p.last_s = p.s;
        p.s = proj.s;
        if (phase_ != RacePhase::Racing || p.finished)
            return;

        const float L = route_.Length();
        const float forward = route_.Ahead(p.last_s, p.s);
        const bool backwards = forward > L * 0.5f;
        const float moved = backwards ? L - forward : forward;

        if (backwards)
            p.backwards_m += moved;
        else if (moved > 0.5f)
            p.backwards_m = 0.0f;
        p.wrong_way = p.backwards_m > config_.wrong_way_after_m;

        // A teleport/reset or an off-route shortcut earns nothing.
        if (backwards || moved > config_.max_legal_step_m)
            return;
        const bool on_road = std::fabs(proj.lateral) <= route_.HalfWidth() + config_.gate_margin_m;
        if (!on_road)
            return;

        // Gates are strictly ordered; at most one lap's worth can be crossed per step.
        for (size_t guard = 0; guard < config_.checkpoints_s.size(); ++guard)
        {
            const float gate = config_.checkpoints_s[size_t(p.next_checkpoint)];
            const float to_gate = route_.Ahead(p.last_s, gate);
            if (to_gate > forward || (to_gate == 0.0f && forward == 0.0f))
                break;
            if (p.next_checkpoint == 0)
            {
                if (p.started)
                    ++p.laps_done;
                p.started = true;
                if (p.laps_done >= config_.laps)
                {
                    p.finished = true;
                    p.finish_tick = tick;
                    break;
                }
            }
            p.next_checkpoint = (p.next_checkpoint + 1) % int(config_.checkpoints_s.size());
        }
    }

    float RaceRules::TotalProgress(const ParticipantState& p) const
    {
        const float L = route_.Length();
        if (!p.started)
            return -route_.Ahead(p.s, 0.0f); // metres short of the start line
        const float since_line = route_.Ahead(0.0f, p.s);
        // Credit stops at the next unpassed gate, so cutting ahead does not improve rank.
        const float cap = p.next_checkpoint == 0 ? L : config_.checkpoints_s[size_t(p.next_checkpoint)];
        return p.laps_done * L + std::min(since_line, cap);
    }

    void RaceRules::Rank()
    {
        std::vector<int> order(participants_.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = int(i);
        std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
            const ParticipantState& A = participants_[size_t(a)];
            const ParticipantState& B = participants_[size_t(b)];
            if (A.finished != B.finished)
                return A.finished;
            if (A.finished)
                return A.finish_tick < B.finish_tick;
            return TotalProgress(A) > TotalProgress(B);
        });
        for (size_t i = 0; i < order.size(); ++i)
            participants_[size_t(order[i])].position = int(i) + 1;

        if (phase_ == RacePhase::Racing && !participants_.empty() &&
            std::all_of(participants_.begin(), participants_.end(), [](const ParticipantState& p) { return p.finished; }))
            phase_ = RacePhase::Finished;
    }

    void RaceRules::BeginGrid()
    {
        phase_ = RacePhase::Grid;
    }

    void RaceRules::Tick(float dt)
    {
        switch (phase_)
        {
        case RacePhase::Grid:
            phase_ = RacePhase::Countdown;
            countdown_ = config_.countdown_s;
            break;
        case RacePhase::Countdown:
            countdown_ -= dt;
            if (countdown_ <= 0.0f)
            {
                countdown_ = 0.0f;
                phase_ = RacePhase::Racing;
            }
            break;
        case RacePhase::Finished:
            phase_ = RacePhase::Results;
            break;
        default:
            break;
        }
    }

    void RaceRules::SetPaused(bool paused)
    {
        if (paused && phase_ != RacePhase::Paused)
        {
            before_pause_ = phase_;
            phase_ = RacePhase::Paused;
        }
        else if (!paused && phase_ == RacePhase::Paused)
        {
            phase_ = before_pause_;
        }
    }

    void RaceRules::Abort()
    {
        phase_ = RacePhase::Aborted;
    }

    void RaceRules::Restart()
    {
        phase_ = RacePhase::Restarting;
        for (size_t i = 0; i < participants_.size(); ++i)
        {
            ParticipantState fresh;
            fresh.id = int(i);
            fresh.next_checkpoint = 0;
            fresh.s = fresh.last_s = route_.Project(grid_[i]).s;
            participants_[i] = fresh;
        }
        phase_ = RacePhase::Grid;
    }
}
