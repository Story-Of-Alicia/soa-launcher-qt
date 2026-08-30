#pragma once

#include <QObject>
#include <QString>

#include "network/Courier.h"
#include "network/DownloadStatus.hpp"
#include "ui/Stage.hpp"
#include "common/Status.hpp"

class QTimer;

namespace core::state
{
    class InstallState : public QObject
    {
        Q_OBJECT

    public:
        explicit InstallState(QObject* parent = nullptr);
        ~InstallState() override;

        [[nodiscard]] Stage stage() const { return current; }
        [[nodiscard]] QString error_message() const { return last_error; }
        [[nodiscard]] QString warning_message() const { return last_warning; }

        void probe();
        void dismiss_error();
        void confirm_rules_reviewed();
        void clear_rules_reviewed();


    signals:
        void stage_changed(core::state::Stage now);
        void error_changed(const QString& message);
        void warning_changed(const QString& message);

    private:
        void on_reporter_changed(const QString& name, const status::Status& status);
        void on_courier_status(const core::network::DownloadStatus& status);
        void set_error(const QString& message);
        void set_warning(const QString& message);
        void recompute();
        void schedule_probe();
        Stage compute() const;
        void start_update_check_if_needed();
        QString current_update_key() const;
        void cancel_update_check();

        bool probed {};
        bool prerequisites_confirmed {};
        bool rules_accepted {};
        bool rules_reviewed {};
        bool runtime_chosen {};
        bool prefix_exists {};
        bool prefix_ready {};
        bool game_installed {};
        bool update_needed {};
        bool authed {};
        bool courier_working {};
        bool update_check_in_progress {};
        bool update_check_complete {};

        core::status::State wine_state {status::State::Idle};
        core::status::State auth_state {status::State::Idle};
        QString wine_phase;
        QString last_error;
        QString last_warning;
        QString checked_update_key;

        courier* update_checker {};
        QTimer* probe_timer {};
        qulonglong update_operation_id {};

        Stage current {Stage::Probing};
    };
}
