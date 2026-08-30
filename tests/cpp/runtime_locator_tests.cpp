#include <QtTest>
#include <QByteArray>
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


namespace
{
    class EnvironmentOverride final
    {
    public:
        EnvironmentOverride(const char* name, const QByteArray& value)
            : name_(name), was_set_(qEnvironmentVariableIsSet(name)), previous_(qgetenv(name))
        {
            qputenv(name_, value);
        }

        ~EnvironmentOverride()
        {
            if (was_set_)
                qputenv(name_, previous_);
            else
                qunsetenv(name_);
        }

    private:
        QByteArray name_;
        bool was_set_ {};
        QByteArray previous_;
    };
}

class RuntimeLocatorTests final : public QObject
{
    Q_OBJECT

private slots:
    void resolves_runtime_folder_to_wine_entry_point()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString runtime = directory.filePath(QStringLiteral("runtime"));
        const QString bin = QDir(runtime).filePath(QStringLiteral("bin"));
        QVERIFY(QDir().mkpath(bin));

        const QString winePath = QDir(bin).filePath(QStringLiteral("wine"));
        QFile wine(winePath);
        QVERIFY(wine.open(QIODevice::WriteOnly));
        QVERIFY(wine.write("#!/bin/sh\nexit 0\n") > 0);
        wine.close();
        QVERIFY(wine.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner));

        QCOMPARE(core::wine::macos::resolve_wine_executable(runtime), winePath);
        QCOMPARE(core::wine::macos::runtime_root_for_executable(winePath), runtime);
    }

    void prefers_wine_over_legacy_wine64()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString bin = directory.filePath(QStringLiteral("bin"));
        QVERIFY(QDir().mkpath(bin));
        for (const QString& name : {QStringLiteral("wine64"), QStringLiteral("wine")})
        {
            QFile file(QDir(bin).filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("#!/bin/sh\nexit 0\n") > 0);
            file.close();
            QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ExeOwner));
        }
        QCOMPARE(core::wine::macos::resolve_wine_executable(directory.path()),
                 QDir(bin).filePath(QStringLiteral("wine")));
    }

    void probes_script_runtime_without_creating_a_prefix()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString bin = directory.filePath(QStringLiteral("bin"));
        QVERIFY(QDir().mkpath(bin));
        const QString winePath = QDir(bin).filePath(QStringLiteral("wine"));
        QFile wine(winePath);
        QVERIFY(wine.open(QIODevice::WriteOnly));
        QVERIFY(wine.write("#!/bin/sh\necho wine-test-11.0\n") > 0);
        wine.close();
        QVERIFY(wine.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner));

        const auto probe = core::wine::macos::probe_runtime(directory.path());
        QVERIFY2(probe.usable, qPrintable(probe.failure));
        QCOMPARE(probe.executable, winePath);
        QCOMPARE(probe.version, QStringLiteral("wine-test-11.0"));
    }


#if defined(Q_OS_LINUX)
    void umu_environment_uses_prefix_without_direct_proton_compat_path()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString proton = directory.filePath(QStringLiteral("proton"));
        QFile protonFile(proton);
        QVERIFY(protonFile.open(QIODevice::WriteOnly));
        QVERIFY(protonFile.write("#!/bin/sh\nexit 0\n") > 0);
        protonFile.close();
        QVERIFY(protonFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ExeOwner));

        const QString tmp = directory.filePath(QStringLiteral("tmp"));
        QVERIFY(QDir().mkpath(tmp));
        EnvironmentOverride compat_data("STEAM_COMPAT_DATA_PATH", QByteArray("/steam/compat"));
        EnvironmentOverride compat_client("STEAM_COMPAT_CLIENT_INSTALL_PATH", QByteArray("/steam"));
        EnvironmentOverride steam_app("SteamAppId", QByteArray("123"));
        EnvironmentOverride steam_game("SteamGameId", QByteArray("456"));
        EnvironmentOverride preload(
            "LD_PRELOAD",
            QByteArray("/steam/gameoverlayrenderer.so:/usr/lib/libgamemodeauto.so.0"));
        EnvironmentOverride tmpdir("TMPDIR", tmp.toUtf8());

        const QString prefix = directory.filePath(QStringLiteral("compat/pfx"));
        const QString compat = directory.filePath(QStringLiteral("compat"));
        core::wine::RuntimeSettings settings {proton, prefix, compat, QStringLiteral("win64"),
                                               QString(), true};
        const QProcessEnvironment environment =
            core::wine::RuntimeLocator::make_umu_environment(settings, directory.path());



        QCOMPARE(environment.value(QStringLiteral("WINEPREFIX")), compat);
        QCOMPARE(environment.value(QStringLiteral("PROTONPATH")), directory.path());
        QCOMPARE(environment.value(QStringLiteral("GAMEID")), QStringLiteral("umu-storyofalicia"));
        QCOMPARE(environment.value(QStringLiteral("SteamGameId")), QStringLiteral("456"));
        QCOMPARE(environment.value(QStringLiteral("TMPDIR")), tmp);
        QVERIFY(!environment.contains(QStringLiteral("STEAM_COMPAT_DATA_PATH")));
        QVERIFY(!environment.contains(QStringLiteral("STEAM_COMPAT_CLIENT_INSTALL_PATH")));
        QVERIFY(!environment.contains(QStringLiteral("SteamAppId")));
        QCOMPARE(environment.value(QStringLiteral("LD_PRELOAD")),
                 QStringLiteral("/usr/lib/libgamemodeauto.so.0"));
    }

    void umu_environment_drops_invalid_tmpdir()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString invalid_tmp = directory.filePath(QStringLiteral("missing"));
        EnvironmentOverride tmpdir("TMPDIR", invalid_tmp.toUtf8());

        const QString prefix = directory.filePath(QStringLiteral("compat/pfx"));
        core::wine::RuntimeSettings settings {QStringLiteral("proton"), prefix, QString(),
                                               QStringLiteral("win64"), QString(), true};
        const QProcessEnvironment environment =
            core::wine::RuntimeLocator::make_umu_environment(settings, directory.path());

        QVERIFY(!environment.contains(QStringLiteral("TMPDIR")));

        QCOMPARE(environment.value(QStringLiteral("WINEPREFIX")), prefix);
    }

    void repairs_doubled_proton_prefix()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root(directory.path());
        const QString prefix = root.filePath(QStringLiteral("pfx"));
        const QString inner = QDir(prefix).filePath(QStringLiteral("pfx"));
        QVERIFY(QDir().mkpath(QDir(inner).filePath(QStringLiteral("drive_c/windows"))));
        QVERIFY(QFile(QDir(prefix).filePath(QStringLiteral("version"))).open(
            QIODevice::WriteOnly));

        QVERIFY(core::wine::repair_doubled_proton_prefix(directory.path()));
        QVERIFY(QFileInfo(QDir(prefix).filePath(QStringLiteral("drive_c/windows"))).isDir());
        QVERIFY(!QFileInfo(inner).exists());
        QVERIFY(!QFileInfo(root.filePath(QStringLiteral(".pfx-migrating"))).exists());


        QVERIFY(!core::wine::repair_doubled_proton_prefix(directory.path()));
    }

    void removes_doubled_proton_prefix_stub()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString inner = QDir(directory.path())
                                  .filePath(QStringLiteral("pfx/pfx"));
        QVERIFY(QDir().mkpath(inner));

        QVERIFY(core::wine::repair_doubled_proton_prefix(directory.path()));
        QVERIFY(!QFileInfo(inner).exists());
    }
#endif

#if defined(Q_OS_MACOS)
    void configures_crossover_runtime_root()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString cxRoot = directory.filePath(
            QStringLiteral("CrossOver.app/Contents/SharedSupport/CrossOver"));
        const QString bin = QDir(cxRoot).filePath(QStringLiteral("bin"));
        QVERIFY(QDir().mkpath(bin));
        const QString winePath = QDir(bin).filePath(QStringLiteral("wine"));
        QFile wine(winePath);
        QVERIFY(wine.open(QIODevice::WriteOnly));
        QVERIFY(wine.write("#!/bin/sh\nexit 0\n") > 0);
        wine.close();
        QVERIFY(wine.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner));

        QProcessEnvironment environment;
        core::wine::macos::apply_runtime_environment(environment, winePath);
        QCOMPARE(environment.value(QStringLiteral("CX_ROOT")), cxRoot);
    }
#endif

};

QTEST_MAIN(RuntimeLocatorTests)
#include "runtime_locator_tests.moc"
