#pragma once
// Records the player's race inside the game for ProofPlay: the setup once, then every physics
// step's commands, and writes sessions/<player>/<time>.json when the race stops running.
#include "Session.h"

#include <string>

namespace proofplay
{
    class Recorder
    {
    public:
        // dir: the sessions root; the player's name comes from PROOFPLAY_PLAYER (default "Oyuncu").
        // PROOFPLAY_OFF=1 disables recording.
        void Begin(SessionSetup setup, const std::string& dir);
        // Call just before RaceSession::Step with exactly what is passed to it.
        void BeforeStep(racer::runtime::RaceSession& race, const StepInput& in);
        // Call right after RaceSession::Step; writes the file once the race is over.
        void AfterStep(racer::runtime::RaceSession& race);
        bool Recording() const { return active_; }
        const std::string& LastFile() const { return last_file_; }

    private:
        Session session_;
        StatsTracker stats_;
        bool reset_ = false;
        std::string dir_, last_file_;
        bool active_ = false;
    };
}
