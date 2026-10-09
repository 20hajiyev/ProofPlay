#pragma once
// ProofPlay analysis: replays a session, finds where the player loses time against the ideal
// speed profile, scores skill attributes, and proves fixes by re-simulating changed inputs.
#include "Session.h"

#include <functional>
#include <string>
#include <vector>

namespace proofplay
{
    struct Sample
    {
        uint64_t tick = 0;
        float s = 0;        // route position on the current lap
        float d = 0;        // total progress along the route (laps included), metres
        float speed = 0;    // m/s
        float lateral = 0;  // metres from the centreline, + = right
        float x = 0, z = 0;
        StepInput input;
    };

    struct Trace
    {
        std::vector<Sample> samples;
        std::vector<racer::CombatEvent> events; // only those involving the player
        std::vector<float> lap_times;
        uint32_t recoveries = 0;
        EndState end;
    };

    // Policy: the input for this step given the step index and the player's state.
    using Policy = std::function<StepInput(size_t step, const Sample& now)>;
    // Stop: return true to end the run early (counterfactual windows).
    using Stop = std::function<bool(const Sample& now)>;

    // keep_samples=false: only events/laps/end are kept (what-if runs read the car through stop).
    Trace Simulate(const SessionSetup& setup, const Policy& policy, size_t max_steps, const Stop& stop = {}, bool keep_samples = true);
    Trace Replay(const Session& s);

    struct Corner
    {
        int id = 0;
        float apex_s = 0;   // route position of the slowest ideal speed
        float entry_s = 0;  // segment start (braking zone)
        float exit_s = 0;
        float ideal_speed = 0;
    };
    std::vector<Corner> FindCorners(const SessionSetup& setup);

    struct Proof
    {
        int corner = 0;
        int lap = 0;
        std::string kind;          // "brake_earlier"
        float amount_m = 0;        // how much earlier
        float gain_s = 0;          // time saved by the corner exit, proven by re-simulation
        float exit_speed_gain = 0; // m/s
        int sims = 0;              // re-simulations spent
        float shift_m = 0;         // signed: > 0 act earlier, < 0 act later
        bool robust = false;       // the neighbouring shifts (5-10 m either side) are also safe
        float window_lo_m = 0, window_hi_m = 0; // every tested amount in this range gains time
        std::vector<float> path_real, path_fix; // x,z pairs through the window (map animation)
    };
    std::vector<Proof> ProveFixes(const Session& s, const Trace& t, const std::vector<Corner>& corners, int max_proofs);

    // Full report for the dashboard; with_proofs=false is the fast pass (no what-if search).
    std::string AnalyzeToJson(const Session& s, bool with_proofs = true);
    // Enables multi-process what-if: workers re-read the session from this file.
    void SetSessionPath(const std::string& path);
    // Worker entry: "pass:shift,..." over the loss-sorted passes; JSON results.
    std::string RunWhatIfJobs(const Session& s, const std::string& jobs);
    // Tests advice from any coach ([{corner, lap, kind, amount_m}]) by re-simulation; adds
    // status works | no_effect | worse | crash | not_testable and gain_s to each item.
    std::string EvaluateAdvice(const Session& s, const std::string& advice_json);
}
