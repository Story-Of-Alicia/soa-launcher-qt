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

        core::wine::RuntimeLocator::apply_wine_environment_entries(
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

    void applies_umu_and_dxvk_environment_entries()
    {
        QProcessEnvironment environment;
        environment.insert(QStringLiteral("WINEPREFIX"), QStringLiteral("/safe/prefix"));

        core::wine::RuntimeLocator::apply_runtime_environment_entries(
            environment,
            {QStringLiteral("UMU_CONTAINER_NSENTER=1"),
             QStringLiteral("DXVK_HUD=fps"),
             QStringLiteral("WINEPREFIX=/escape")});

        QCOMPARE(environment.value(QStringLiteral("UMU_CONTAINER_NSENTER")),
                 QStringLiteral("1"));
        QCOMPARE(environment.value(QStringLiteral("DXVK_HUD")), QStringLiteral("fps"));
        QCOMPARE(environment.value(QStringLiteral("WINEPREFIX")),
                 QStringLiteral("/safe/prefix"));
    }

    void redacts_process_arguments_and_output()
    {
        const QString secret = QStringLiteral("private-token");
        const QStringList arguments = core::wine::redacted_command_args(
            {QStringLiteral("-ID"), QStringLiteral("[user]"), QStringLiteral("-OP"),
             QStringLiteral("[private-token]")},
            {secret});
        QCOMPARE(arguments,
                 QStringList({QStringLiteral("-ID"), QStringLiteral("[user]"),
                              QStringLiteral("-OP"), QStringLiteral("[REDACTED]")}));
        QCOMPARE(
            core::wine::redact_sensitive_text(
                QStringLiteral("launch -OP [private-token] private-token"), {secret}),
            QStringLiteral("launch -OP [REDACTED] [REDACTED]"));
    }

    void native_shell_does_not_require_rosetta()
    {
#if defined(Q_OS_MACOS)
        const QString shell = QStandardPaths::findExecutable(QStringLiteral("sh"));
        QVERIFY(!shell.isEmpty());
        QVERIFY2(!core::wine::macos::executable_requires_rosetta(shell),
                 qPrintable(QStringLiteral("Native shell was classified as Intel-only: %1 (%2)")
                                .arg(shell,
                                     core::wine::macos::executable_architectures(shell)
                                         .join(QLatin1Char(' ')))));
#else
        QSKIP("Rosetta classification only applies to macOS.");
#endif
    }

    void process_runner_completes_once()
    {
        const QString shell = QStandardPaths::findExecutable(QStringLiteral("sh"));
        QVERIFY(!shell.isEmpty());

        core::wine::ProcessRunner runner;
        core::wine::ProcessRunner::Request request;
        request.program = shell;
        request.arguments = {QStringLiteral("-c"), QStringLiteral("printf runner-ok")};
        request.timeout_ms = 5000;

        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        int completions = 0;
        core::wine::command_result result;
        connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        QVERIFY(runner.start(std::move(request),
                             [&](const core::wine::command_result& completed)
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
