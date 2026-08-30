#include <QtTest>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <utility>

#include "common/GameVersion.hpp"
#include "runtime/GameSession.hpp"
#include "runtime/MacWineRuntime.hpp"
#include "runtime/PrefixInspector.hpp"
#include "runtime/ProcessRunner.hpp"
#include "runtime/RuntimeLocator.hpp"
#include "runtime/WineProcess.hpp"
#include "common/DesktopEntry.hpp"
#include "common/LaunchArguments.hpp"

class LaunchArgumentTests final : public QObject
{
    Q_OBJECT

private slots:
    void game_phase_transition_graph_is_explicit()
    {
        using core::wine::GamePhase;
        using core::wine::is_valid_game_transition;

        QVERIFY(is_valid_game_transition(GamePhase::Idle, GamePhase::Preflight));
        QVERIFY(is_valid_game_transition(GamePhase::Preflight, GamePhase::CleaningPrefix));
        QVERIFY(is_valid_game_transition(GamePhase::CleaningPrefix,
                                         GamePhase::FirstLaunchSetup));
        QVERIFY(is_valid_game_transition(GamePhase::FirstLaunchSetup, GamePhase::Launching));
        QVERIFY(is_valid_game_transition(GamePhase::Launching, GamePhase::Running));
        QVERIFY(is_valid_game_transition(GamePhase::Running, GamePhase::Finished));
        QVERIFY(is_valid_game_transition(GamePhase::Finished, GamePhase::Idle));
        QVERIFY(is_valid_game_transition(GamePhase::Running,
                                         GamePhase::MonitoringUncertain));

        QVERIFY(!is_valid_game_transition(GamePhase::Idle, GamePhase::Launching));
        QVERIFY(!is_valid_game_transition(GamePhase::Running, GamePhase::Launching));
        QVERIFY(!is_valid_game_transition(GamePhase::MonitoringUncertain, GamePhase::Idle));
    }

    void accepts_safe_game_arguments()
    {
        const auto result = util::launch_arguments::validate(
            QStringLiteral("-windowed -language \"English UK\""));
        QVERIFY(result.valid);
        QCOMPARE(result.arguments,
                 QStringList({QStringLiteral("-windowed"),
                              QStringLiteral("-language"),
                              QStringLiteral("English UK")}));
    }

    void moves_runtime_environment_entries_out_of_game_arguments()
    {
        const auto result = util::launch_arguments::validate(
            QStringLiteral("UMU_CONTAINER_NSENTER=1 -windowed "
                           "DXVK_HUD=fps SOA_LABEL=\"hello world\""));

        QVERIFY(result.valid);
        QCOMPARE(result.arguments, QStringList({QStringLiteral("-windowed")}));
        QCOMPARE(result.environment_entries,
                 QStringList({QStringLiteral("UMU_CONTAINER_NSENTER=1"),
                              QStringLiteral("DXVK_HUD=fps"),
                              QStringLiteral("SOA_LABEL=hello world")}));
    }

    void rejects_reserved_game_arguments()
    {
        for (const QString& value : {
                 QStringLiteral("-OP stolen"),
                 QStringLiteral("-id=other"),
                 QStringLiteral("-GameID 99")})
        {
            const auto result = util::launch_arguments::validate(value);
            QVERIFY(!result.valid);
            QVERIFY(!result.error.isEmpty());
        }
    }

    void rejects_oversized_game_arguments()
    {
        const auto result = util::launch_arguments::validate(QString(4097, QLatin1Char('a')));
        QVERIFY(!result.valid);
    }

    void escapes_desktop_field_codes()
    {
        QCOMPARE(
            util::desktop_entry::quoted_exec_argument(QStringLiteral("/tmp/100%/launcher\"app")),
            QStringLiteral("\"/tmp/100%%/launcher\\\"app\""));
    }

    void escapes_desktop_control_characters()
    {
        QCOMPARE(
            util::desktop_entry::quoted_exec_argument(QStringLiteral("/tmp/a\nb\tapp")),
            QStringLiteral("\"/tmp/a\\nb\\tapp\""));
    }

};

QTEST_MAIN(LaunchArgumentTests)
#include "launch_argument_tests.moc"
