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

class EnvironmentTests final : public QObject
{
    Q_OBJECT

private slots:
    void accepts_only_safe_runtime_environment_entries()
    {
        QProcessEnvironment environment;
        environment.insert(QStringLiteral("WINEPREFIX"), QStringLiteral("/safe/prefix"));
        environment.insert(QStringLiteral("PATH"), QStringLiteral("/safe/path"));

        soa::runtime::RuntimeLocator::apply_wine_environment_entries(
            environment, QStringLiteral("WINEPREFIX=/escape PATH=/unsafe "
                                        "SOA_RENDER_HINT=fast "
                                        "SOA_LABEL=\"hello world\" "
                                        "invalid-key=value"));

        QCOMPARE(environment.value(QStringLiteral("WINEPREFIX")),
                 QStringLiteral("/safe/prefix"));
        QCOMPARE(environment.value(QStringLiteral("PATH")), QStringLiteral("/safe/path"));
        QCOMPARE(environment.value(QStringLiteral("SOA_RENDER_HINT")), QStringLiteral("fast"));
        QCOMPARE(environment.value(QStringLiteral("SOA_LABEL")), QStringLiteral("hello world"));
        QVERIFY(!environment.contains(QStringLiteral("invalid-key")));
    }

    void applies_only_non_launcher_owned_runtime_environment_entries()
    {
        QProcessEnvironment environment;
        environment.insert(QStringLiteral("WINEPREFIX"), QStringLiteral("/safe/prefix"));
        environment.insert(QStringLiteral("PROTONPATH"), QStringLiteral("/safe/proton"));
        environment.insert(QStringLiteral("GAMEID"), QStringLiteral("umu-storyofalicia"));
        environment.insert(QStringLiteral("STORE"), QStringLiteral("none"));

        soa::runtime::RuntimeLocator::apply_runtime_environment_entries(
            environment,
            {QStringLiteral("UMU_CONTAINER_NSENTER=1"),
             QStringLiteral("UMU_NO_RUNTIME=1"),
             QStringLiteral("PROTONPATH=/escape/proton"),
             QStringLiteral("PROTON_USE_WINED3D=1"),
             QStringLiteral("STEAM_COMPAT_DATA_PATH=/escape/compat"),
             QStringLiteral("RUNTIMEPATH=steamrt2"),
             QStringLiteral("GAMEID=other-game"),
             QStringLiteral("STORE=steam"),
             QStringLiteral("WINEPREFIX=/escape/prefix"),
             QStringLiteral("WINEDLLOVERRIDES=d3d9=n"),
             QStringLiteral("LD_PRELOAD=/tmp/inject.so"),
             QStringLiteral("DXVK_HUD=fps"),
             QStringLiteral("MANGOHUD=1")});

        for (const QString& key : {
                 QStringLiteral("UMU_CONTAINER_NSENTER"),
                 QStringLiteral("UMU_NO_RUNTIME"),
                 QStringLiteral("PROTON_USE_WINED3D"),
                 QStringLiteral("STEAM_COMPAT_DATA_PATH"),
                 QStringLiteral("RUNTIMEPATH"),
                 QStringLiteral("WINEDLLOVERRIDES"),
                 QStringLiteral("LD_PRELOAD")})
        {
            QVERIFY2(!environment.contains(key), qPrintable(key));
        }
        QCOMPARE(environment.value(QStringLiteral("PROTONPATH")), QStringLiteral("/safe/proton"));
        QCOMPARE(environment.value(QStringLiteral("GAMEID")), QStringLiteral("umu-storyofalicia"));
        QCOMPARE(environment.value(QStringLiteral("STORE")), QStringLiteral("none"));
        QCOMPARE(environment.value(QStringLiteral("WINEPREFIX")), QStringLiteral("/safe/prefix"));
        QCOMPARE(environment.value(QStringLiteral("DXVK_HUD")), QStringLiteral("fps"));
        QCOMPARE(environment.value(QStringLiteral("MANGOHUD")), QStringLiteral("1"));
    }

    void redacts_process_arguments_and_output()
    {
        const QString secret = QStringLiteral("private-token");
        const QStringList arguments = soa::runtime::redacted_command_args(
            {QStringLiteral("-ID"), QStringLiteral("[user]"), QStringLiteral("-OP"),
             QStringLiteral("[private-token]")},
            {secret});
        QCOMPARE(arguments,
                 QStringList({QStringLiteral("-ID"), QStringLiteral("[user]"),
                              QStringLiteral("-OP"), QStringLiteral("[REDACTED]")}));
        QCOMPARE(
            soa::runtime::redact_sensitive_text(
                QStringLiteral("launch -OP [private-token] private-token"), {secret}),
            QStringLiteral("launch -OP [REDACTED] [REDACTED]"));
    }

    void native_shell_does_not_require_rosetta()
    {
#if defined(Q_OS_MACOS)
        const QString shell = QStandardPaths::findExecutable(QStringLiteral("sh"));
        QVERIFY(!shell.isEmpty());
        QVERIFY2(!soa::runtime::macos::executable_requires_rosetta(shell),
                 qPrintable(QStringLiteral("Native shell was classified as Intel-only: %1 (%2)")
                                .arg(shell,
                                     soa::runtime::macos::executable_architectures(shell)
                                         .join(QLatin1Char(' ')))));
#else
        QSKIP("Rosetta classification only applies to macOS.");
#endif
    }

    void process_runner_completes_once()
    {
        const QString shell = QStandardPaths::findExecutable(QStringLiteral("sh"));
        QVERIFY(!shell.isEmpty());

        soa::runtime::ProcessRunner runner;
        soa::runtime::ProcessRunner::Request request;
        request.program = shell;
        request.arguments = {QStringLiteral("-c"), QStringLiteral("printf runner-ok")};
        request.timeout_ms = 5000;

        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        int completions = 0;
        soa::runtime::command_result result;
        connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        QVERIFY(runner.start(std::move(request),
                             [&](const soa::runtime::command_result& completed)
                             {
                                 ++completions;
                                 result = completed;
                                 loop.quit();
                             }));
        watchdog.start(8000);
        loop.exec();

        QCOMPARE(completions, 1);
        QVERIFY(result.ok());
        QVERIFY(result.started);
        QCOMPARE(result.exit_code, 0);
        QCOMPARE(result.output, QStringLiteral("runner-ok"));
        QVERIFY(!runner.is_busy());
        QTest::qWait(50);
        QCOMPARE(completions, 1);
    }

};

QTEST_MAIN(EnvironmentTests)
#include "environment_tests.moc"
