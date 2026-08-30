#include "runtime/GameSession.hpp"

namespace core::wine
{
    QString game_phase_name(const GamePhase phase)
    {
        switch (phase)
        {
        case GamePhase::Idle:
            return QStringLiteral("Idle");
        case GamePhase::Preflight:
            return QStringLiteral("Preflight");
        case GamePhase::CleaningPrefix:
            return QStringLiteral("CleaningPrefix");
        case GamePhase::FirstLaunchSetup:
            return QStringLiteral("FirstLaunchSetup");
        case GamePhase::Launching:
            return QStringLiteral("Launching");
        case GamePhase::Running:
            return QStringLiteral("Running");
        case GamePhase::MonitoringUncertain:
            return QStringLiteral("MonitoringUncertain");
        case GamePhase::Finished:
            return QStringLiteral("Finished");
        }
        return QStringLiteral("Unknown");
    }

    bool is_valid_game_transition(const GamePhase from, const GamePhase to)
    {
        if (from == to)
            return true;
        switch (from)
        {
        case GamePhase::Idle:
            return to == GamePhase::Preflight || to == GamePhase::Running;
        case GamePhase::Preflight:
            return to == GamePhase::CleaningPrefix || to == GamePhase::FirstLaunchSetup ||
                   to == GamePhase::Running || to == GamePhase::Finished;
        case GamePhase::CleaningPrefix:
            return to == GamePhase::FirstLaunchSetup || to == GamePhase::Finished;
        case GamePhase::FirstLaunchSetup:
            return to == GamePhase::Launching || to == GamePhase::Finished;
        case GamePhase::Launching:
            return to == GamePhase::Running || to == GamePhase::Finished;
        case GamePhase::Running:
            return to == GamePhase::MonitoringUncertain || to == GamePhase::Finished;
        case GamePhase::MonitoringUncertain:
            return false;
        case GamePhase::Finished:
            return to == GamePhase::Idle;
        }
        return false;
    }
}
