#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "network/Courier.h"
#include "network/DownloadStatus.hpp"
#include "ui/Stage.hpp"
#include "common/Status.hpp"

class QTimer;

namespace soa::ui
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
        void recheck_before_launch();
        void cancel_prelaunch_check();
        void mark_game_synchronized();
        void begin_repair_transfer();
        void end_repair_transfer();

    signals:
        void stage_changed(soa::ui::Stage now);
        void error_changed(const QString& message);
        void warning_changed(const QString& message);
        void game_sync_started(qulonglong operation_id);
        void game_sync_finished(bool ok);
        void game_repair_required(const QStringList& changes);

    private:
        enum class PrelaunchCheck
        {
            None,
            Version,
            Integrity
        };

        void on_reporter_changed(const QString& name, const common::status::Status& status);
        void on_courier_status(const soa::network::DownloadStatus& status);
        void set_error(const QString& message);
        void set_warning(const QString& message);
        void recompute();
        void schedule_probe();
        Stage compute() const;
        void start_update_check_if_needed();
        void start_integrity_check();
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
        bool repair_transfer_active {};
        courier_phase update_phase {courier_phase_preparing};
        PrelaunchCheck prelaunch_check {PrelaunchCheck::None};

        soa::common::status::State wine_state {common::status::State::Idle};
        soa::common::status::State auth_state {common::status::State::Idle};
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
