#pragma once
#include "racer/Vehicle.h"

namespace racer
{
    struct RecoveryTuning
    {
        float fall_below_y = -5.0f;       // below the lowest drivable surface
        float upside_down_seconds = 2.0f; // up_y < 0.3 for this long
        float stuck_seconds = 5.0f;       // plan 2.9: auto stuck detection after 5 s without progress
        float stuck_speed_kmh = 3.0f;
        float safe_sample_seconds = 1.0f; // how often a safe anchor is recorded
        float anchor_min_speed_kmh = 10.0f;
    };

    enum class RecoveryReason { None, Fell, UpsideDown, Stuck };

    struct RecoveryRequest
    {
        RecoveryReason reason = RecoveryReason::None;
        float position[3] = {}; // ground point of the last safe anchor
        float heading_rad = 0.0f;
    };

    // Watches one car and decides when it must be put back on the road. Safe anchors are
    // only recorded while all four wheels touch and the chassis is upright.
    class RecoveryMonitor
    {
    public:
        explicit RecoveryMonitor(RecoveryTuning tuning = {}) : tuning_(tuning) {}

        RecoveryRequest Update(const VehicleTelemetry& t, float dt, bool player_wants_progress);
        void Reset(const float ground_position[3], float heading_rad);
        bool HasAnchor() const { return has_anchor_; }
        // Request to return to the last safe anchor (e.g. a manual reset); reason None if none yet.
        RecoveryRequest AnchorRequest(RecoveryReason reason) const
        {
            RecoveryRequest r;
            if (!has_anchor_)
                return r;
            r.reason = reason;
            r.position[0] = anchor_[0];
            r.position[1] = anchor_[1];
            r.position[2] = anchor_[2];
            r.heading_rad = anchor_heading_;
            return r;
        }

    private:
        RecoveryTuning tuning_;
        bool has_anchor_ = false;
        float anchor_[3] = {};
        float anchor_heading_ = 0.0f;
        float since_anchor_ = 0.0f;
        float upside_down_ = 0.0f;
        float stuck_ = 0.0f;
    };
}
