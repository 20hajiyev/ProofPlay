#pragma once
#include <cstdint>

namespace racer
{
    struct ClockConfig
    {
        double step_seconds = 1.0 / 120.0;
        uint32_t max_steps_per_frame = 8;
    };

    struct ClockAdvance
    {
        uint32_t steps = 0;           // authoritative steps to run this render frame
        double dropped_seconds = 0.0; // wall time discarded because of the per-frame cap
        float interpolation_alpha = 0.0f; // leftover fraction of a step, for render only
    };

    // The single authoritative racing clock. Gameplay, AI control and physics all
    // advance only through the steps this returns; render never feeds back into it.
    class SimClock
    {
    public:
        explicit SimClock(ClockConfig config = {});

        ClockAdvance Advance(double wall_dt_seconds);

        // While paused, wall time is discarded rather than stored, so resuming never bursts.
        void SetPaused(bool paused);
        bool IsPaused() const { return paused_; }

        uint64_t Tick() const { return tick_; }
        double StepSeconds() const { return config_.step_seconds; }

    private:
        ClockConfig config_;
        double accumulator_ = 0.0;
        uint64_t tick_ = 0;
        bool paused_ = false;
    };
}
