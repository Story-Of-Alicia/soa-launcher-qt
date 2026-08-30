#pragma once

#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "runtime/ProcessRunner.hpp"

namespace core::wine
{
    class RuntimeLocator;

    class PrefixSetupJob final : public QObject
    {
    public:
        struct Callbacks
        {
            std::function<void(const command_result&)> command_finished;
            std::function<void(const QString&)> setup_status;
            std::function<void(const QString&, const QString&)> fail_user;
            std::function<void(const QString&, const QString&)> user_error;
            std::function<void(const QString&)> user_notice;
            std::function<void(bool)> setup_finished;
            std::function<void(const QString&, double, bool)> working;
            std::function<void(const QString&)> done;
            std::function<void(const QString&)> failed;
        };

        PrefixSetupJob(RuntimeLocator& runtime, ProcessRunner& runner, Callbacks callbacks,
                       QObject* parent = nullptr);

        [[nodiscard]] bool is_active() const;

        void setup();
        void setup_wine();
        void setup_proton();
        void sync_dxvk();
        void cancel();

    private:
        enum class Kind
        {
            None,
            Setup,
            Dxvk
        };

        struct SetupCommand
        {
            QString message;
            QString program;
            QStringList arguments;
            QProcessEnvironment environment;
            int timeout_ms {15 * 60 * 1000};
            bool inspect_components_after {};
            bool invalidates_marker {};
            bool optional_failure {};




            bool succeeds_if_prefix_ready {};
        };

        [[nodiscard]] bool ensure_idle();
        [[nodiscard]] bool prefix_structure_ready() const;
        [[nodiscard]] QStringList missing_component_packages() const;
        [[nodiscard]] bool queue_missing_components();
        void run_setup(QVector<SetupCommand> commands, Kind kind);
        void advance();
        void finalize(Kind completed_kind, int attempts_remaining);
        void finish_failure(const QString& message);
        void finish_optional_dxvk_failure(const QString& message);
        void skip_optional_command(const QString& message);
        void reset_state();

        RuntimeLocator& runtime_;
        ProcessRunner& runner_;
        Callbacks callbacks_;
        QVector<SetupCommand> queue_;
        int index_ {-1};
        Kind kind_ {Kind::None};
        quint64 generation_ {};
        bool marker_invalidated_ {};
        bool active_ {};
    };
}
