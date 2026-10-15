// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

#include <QObject>
#include <QSet>
#include <QVector>
#include "common/Status.hpp"

class QTimer;

namespace soa::common::status
{
    class StatusReporter;

    class StatusBus : public QObject
    {
        Q_OBJECT

        public:
            static StatusBus& instance();

            void register_reporter(StatusReporter* r);
            void unregister_reporter(StatusReporter* r);

            bool any_working() const;
            StatusReporter* find(const QString& name) const;

            signals:
                void reporter_status_changed(StatusReporter* reporter, const Status & now);
                void stalled(StatusReporter * reporter, qint64 seconds_stalled);

        private:
            explicit StatusBus(QObject * parent = nullptr);
            StatusBus(const StatusBus &) = delete;
            StatusBus & operator=(const StatusBus&) = delete;

            void check_for_stalls();

            QVector<StatusReporter*> items;
            QSet<StatusReporter*> stall_reported;
            QTimer * watchdog {};
    };
}
