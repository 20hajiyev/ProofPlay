#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace racer
{
    struct Vec2
    {
        float x = 0.0f, z = 0.0f;
    };

    // Closed racing line on the ground plane (x, z). Distance along it ("s") is the single
    // progress measure used by rules, ranking and AI.
    class TrackRoute
    {
    public:
        struct Projection
        {
            float s = 0.0f;       // distance along the route, [0, Length())
            float lateral = 0.0f; // signed offset from the centreline, + = right of travel
            Vec2 direction;       // unit travel direction at s
        };

        TrackRoute() = default;
        TrackRoute(std::vector<Vec2> centerline, float half_width_m);
        // With a height per centreline point and jump drops (D-059): drops are point indices where
        // the raised deck ends in a lip; the segment after a drop is open to the ground below.
        TrackRoute(std::vector<Vec2> centerline, float half_width_m, std::vector<float> elevation, std::vector<int> drops);

        float Length() const { return length_; }
        float HalfWidth() const { return half_width_; }
        // Nearest point, searching only near hint_s when given (tracks can pass close to
        // themselves; a global search would let a car "jump" to the wrong section).
        Projection Project(Vec2 p, float hint_s = -1.0f, float window_m = 60.0f) const;
        Vec2 PointAt(float s) const;
        Vec2 DirectionAt(float s) const;
        // Forward distance from a to b along the loop, in [0, Length()).
        float Ahead(float from_s, float to_s) const;
        // Drive-surface height at s (0 = the quay); ground level over a jump gap.
        float ElevationAt(float s) const;
        // Whether segment i (point i -> i+1) has a raised deck under it.
        bool DeckOnSegment(size_t i) const;
        bool HasElevation() const { return !elevation_.empty(); }

    private:
        std::vector<Vec2> points_;
        std::vector<float> cumulative_; // s at each point
        float length_ = 0.0f;
        float half_width_ = 0.0f;
        std::vector<float> elevation_; // empty = flat
        std::vector<int> drops_;
        size_t SegmentAt(float s) const;
    };

    enum class RacePhase { Loading, Grid, Countdown, Racing, Finished, Results, Paused, Restarting, Aborted };
    const char* ToString(RacePhase phase);

    struct RaceConfig
    {
        int laps = 3;
        std::vector<float> checkpoints_s; // strictly increasing, first must be 0 (the finish line)
        float countdown_s = 3.0f;
        float gate_margin_m = 4.0f;          // how far outside the road edge a gate still counts
        float max_legal_step_m = 25.0f;      // larger jumps between steps (teleport/reset) earn no progress
        float wrong_way_after_m = 15.0f;
    };

    struct ParticipantState
    {
        int id = 0;
        int laps_done = 0;
        int next_checkpoint = 1;    // index into checkpoints_s; 0 = finish line
        float s = 0.0f;             // current route position
        float last_s = 0.0f;
        bool started = false;
        bool finished = false;
        uint64_t finish_tick = 0;
        float backwards_m = 0.0f;   // continuous distance driven against the route
        bool wrong_way = false;
        int position = 0;           // 1-based rank, updated by RaceRules::Rank
    };

    // Plan 2.9: lap and checkpoint rules, ranking and the race state machine. Positions are
    // fed from the authoritative simulation each step; nothing here reads render state.
    class RaceRules
    {
    public:
        RaceRules(const TrackRoute& route, RaceConfig config);

        int AddParticipant(Vec2 grid_position);
        void Update(int id, Vec2 position, uint64_t tick);
        void Rank();

        // Phase control.
        void BeginGrid();
        void Tick(float dt);          // advances countdown; enters Racing when it ends
        void SetPaused(bool paused);
        void Abort();
        void Restart();

        RacePhase Phase() const { return phase_; }
        float CountdownRemaining() const { return countdown_; }
        bool InputsLocked() const { return phase_ != RacePhase::Racing; }
        const ParticipantState& Participant(int id) const { return participants_[size_t(id)]; }
        const std::vector<ParticipantState>& Participants() const { return participants_; }
        // Race progress in metres, comparable across participants; used for ranking and AI.
        float TotalProgress(const ParticipantState& p) const;

    private:
        const TrackRoute& route_;
        RaceConfig config_;
        std::vector<ParticipantState> participants_;
        std::vector<Vec2> grid_;
        RacePhase phase_ = RacePhase::Loading;
        RacePhase before_pause_ = RacePhase::Loading;
        float countdown_ = 0.0f;
    };

    std::string ValidateRaceConfig(const TrackRoute& route, const RaceConfig& config);
}
