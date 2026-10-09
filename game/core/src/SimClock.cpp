#include "racer/SimClock.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    SimClock::SimClock(ClockConfig config) : config_(config) {}

    void SimClock::SetPaused(bool paused)
    {
        // Only a state change drops pending time; callers may re-assert the state every frame.
        if (paused != paused_)
            accumulator_ = 0.0;
        paused_ = paused;
    }

    ClockAdvance SimClock::Advance(double wall_dt_seconds)
    {
        ClockAdvance result;
        if (paused_ || wall_dt_seconds <= 0.0)
            return result;

        const double step = config_.step_seconds;
        // Epsilon absorbs 1/30 / (1/120) evaluating to 3.99999... instead of 4.
        constexpr double kStepEpsilon = 1e-6;

        accumulator_ += wall_dt_seconds;
        const auto whole = static_cast<uint64_t>(std::floor(accumulator_ / step + kStepEpsilon));
        const uint32_t steps = static_cast<uint32_t>(std::min<uint64_t>(whole, config_.max_steps_per_frame));
        accumulator_ = std::max(0.0, accumulator_ - steps * step);

        if (whole > steps)
        {
            // Keep only the sub-step remainder so the next frame cannot burst.
            const double keep = std::max(0.0, accumulator_ - std::floor(accumulator_ / step + kStepEpsilon) * step);
            result.dropped_seconds = accumulator_ - keep;
            accumulator_ = keep;
        }

        tick_ += steps;
        result.steps = steps;
        result.interpolation_alpha = static_cast<float>(std::min(accumulator_ / step, 0.999999));
        return result;
    }
}
