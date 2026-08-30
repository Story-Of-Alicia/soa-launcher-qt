#include "common/StatusReporter.hpp"
#include "common/StatusBus.hpp"

#include "common/Log.hpp"
#include <spdlog/spdlog.h>

#include <utility>

namespace core::status
{
    StatusReporter::StatusReporter(QString name_, QObject* parent)
        : QObject(parent), name(std::move(name_))
    {
        current.last_changed = QDateTime::currentDateTime();
        StatusBus::instance().register_reporter(this);
    }

    StatusReporter::~StatusReporter()
    {
        StatusBus::instance().unregister_reporter(this);
    }

    void StatusReporter::set_status(Status next)
    {
        const QDateTime changedAt = QDateTime::currentDateTime();
        const bool unchanged = current.state == next.state
            && current.phase == next.phase
            && current.message == next.message
            && current.progress == next.progress
            && current.watchdog_exempt == next.watchdog_exempt;



        if (unchanged)
        {
            current.last_changed = changedAt;
            return;
        }

        next.last_changed = changedAt;
        current = std::move(next);

        const bool noisy_courier_progress =
            name == QStringLiteral("courier")
            && current.state == State::Working
            && (current.phase == QStringLiteral("downloading")
                || current.phase == QStringLiteral("verifying"));

        if (!noisy_courier_progress)
        {
            SPDLOG_DEBUG("status[{}]: {} {} {}",
                         name.toStdString(),
                         to_string(current.state),
                         current.phase.toStdString(),
                         current.message.toStdString());
        }

        emit status_changed(current);
    }

    void StatusReporter::working(const QString& phase, const double progress,
                                 const bool watchdogExempt)
    {
        Status s;
        s.state = State::Working;
        s.phase = phase;
        s.progress = progress;
        s.watchdog_exempt = watchdogExempt;
        set_status(std::move(s));
    }

    void StatusReporter::done(const QString& message)
    {
        Status s;
        s.state   = State::Done;
        s.message = message;
        s.progress = 1.0;
        set_status(std::move(s));
    }

    void StatusReporter::fail(const QString& message)
    {
        Status s;
        s.state   = State::Failed;
        s.message = message;
        set_status(std::move(s));
    }

    void StatusReporter::retry(const QString& message)
    {
        Status s;
        s.state   = State::Retrying;
        s.message = message;
        set_status(std::move(s));
    }

    void StatusReporter::idle()
    {
        set_status(Status{});
    }
}
