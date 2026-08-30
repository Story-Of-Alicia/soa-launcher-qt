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

#include "core/game/GameVersion.hpp"
#include "core/wine/GameSession.hpp"
#include "core/wine/MacWineRuntime.hpp"
#include "core/wine/PrefixInspector.hpp"
#include "core/wine/ProcessRunner.hpp"
#include "core/wine/RuntimeLocator.hpp"
#include "core/wine/WineProcess.hpp"
#include "util/DesktopEntry.hpp"
#include "util/LaunchArguments.hpp"

class LaunchArgumentTests final : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        qunsetenv("DEVELOPER_MODE");
    }

    void cleanup()
    {
        qunsetenv("DEVELOPER_MODE");
    }

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

    void accepts_developer_mode_values()
    {
        for (const QByteArray& value : {QByteArrayLiteral("on"), QByteArrayLiteral("TRUE")})
        {
            qputenv("DEVELOPER_MODE", value);
            QVERIFY(util::launch_arguments::developer_mode_enabled());
        }

        qputenv("DEVELOPER_MODE", QByteArrayLiteral("1"));
        QVERIFY(!util::launch_arguments::developer_mode_enabled());
        qputenv("DEVELOPER_MODE", QByteArrayLiteral("false"));
        QVERIFY(!util::launch_arguments::developer_mode_enabled());
    }

    void accepts_developer_credentials_only_in_developer_mode()
    {
        qputenv("DEVELOPER_MODE", QByteArrayLiteral("true"));
        const auto result = util::launch_arguments::validate(
            QStringLiteral("-ID [local-user] -OP [local-password] -windowed"));

        QVERIFY(result.valid);
        QCOMPARE(result.developer_id, QStringLiteral("[local-user]"));
        QCOMPARE(result.developer_op, QStringLiteral("[local-password]"));
        QCOMPARE(result.arguments, QStringList({QStringLiteral("-windowed")}));
    }

    void accepts_inline_developer_credentials()
    {
        qputenv("DEVELOPER_MODE", QByteArrayLiteral("ON"));
        const auto result = util::launch_arguments::validate(
            QStringLiteral("-ID=[local-user] -OP=[local-password]"));

        QVERIFY(result.valid);
        QCOMPARE(result.developer_id, QStringLiteral("[local-user]"));
        QCOMPARE(result.developer_op, QStringLiteral("[local-password]"));
    }

    void developer_mode_still_rejects_game_id_override()
    {
        qputenv("DEVELOPER_MODE", QByteArrayLiteral("true"));
        const auto result = util::launch_arguments::validate(QStringLiteral("-GameID 4"));

        QVERIFY(!result.valid);
        QVERIFY(result.error.contains(QStringLiteral("-GameID")));
    }

    void rejects_duplicate_developer_credentials()
    {
        qputenv("DEVELOPER_MODE", QByteArrayLiteral("true"));
        const auto result = util::launch_arguments::validate(
            QStringLiteral("-ID [one] -ID [two] -OP [password]"));

        QVERIFY(!result.valid);
        QVERIFY(!result.error.isEmpty());
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
