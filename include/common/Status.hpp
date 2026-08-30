#pragma once

#include <QString>
#include <QDateTime>

namespace core::status
{
    enum class State
    {
        Idle,
        Working,
        Done,
        Failed,
        Retrying
    };

    struct Status
    {
        State     state    = State::Idle;
        QString   phase {};
        QString   message {};
        double    progress = -1.0;
        bool      watchdog_exempt {};
        QDateTime last_changed {};
    };

    inline const char* to_string(State s)
    {
        switch (s)
        {
            case State::Idle:     return "Idle";
            case State::Working:  return "Working";
            case State::Done:     return "Done";
            case State::Failed:   return "Failed";
            case State::Retrying: return "Retrying";
        }
        return "Idle";
    }
}
