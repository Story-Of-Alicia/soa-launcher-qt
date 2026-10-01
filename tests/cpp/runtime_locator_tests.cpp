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
#include "runtime/WineRegistry.hpp"
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
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

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

        QCOMPARE(soa::runtime::macos::resolve_wine_executable(runtime), winePath);
        QCOMPARE(soa::runtime::macos::runtime_root_for_executable(winePath), runtime);
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
        QCOMPARE(soa::runtime::macos::resolve_wine_executable(directory.path()),
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

        const auto probe = soa::runtime::macos::probe_runtime(directory.path());
        QVERIFY2(probe.usable, qPrintable(probe.failure));
        QCOMPARE(probe.executable, winePath);
        QCOMPARE(probe.version, QStringLiteral("wine-test-11.0"));
    }


#if defined(Q_OS_LINUX)
    void managed_umu_requires_folder_redirection_support()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const auto makeUmu = [&directory](const QString& name, const QString& version)
        {
            const QString path = directory.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return QString();
            const QByteArray script = QByteArray("#!/bin/sh\necho 'umu-launcher version ")
                + version.toUtf8() + QByteArray("'\n");
            if (file.write(script) != script.size())
                return QString();
            file.close();
            if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                     | QFileDevice::ExeOwner))
                return QString();
            return path;
        };

        const QString oldUmu = makeUmu(QStringLiteral("umu-old"), QStringLiteral("1.3.0"));
        const QString supportedUmu = makeUmu(QStringLiteral("umu-new"), QStringLiteral("1.4.0"));
        const QString futureUmu = makeUmu(QStringLiteral("umu-future"), QStringLiteral("2.0.0"));
        QVERIFY(!oldUmu.isEmpty());
        QVERIFY(!supportedUmu.isEmpty());
        QVERIFY(!futureUmu.isEmpty());

        QVERIFY(!soa::runtime::umu_executable_supports_managed_folders(oldUmu));
        QVERIFY(soa::runtime::umu_executable_supports_managed_folders(supportedUmu));
        QVERIFY(soa::runtime::umu_executable_supports_managed_folders(futureUmu));
    }

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
        EnvironmentOverride compat_libraries("STEAM_COMPAT_LIBRARY_PATHS",
                                             QByteArray("/steam/library"));
        EnvironmentOverride umu_folders("UMU_FOLDERS_PATH", QByteArray("/outside/launcher"));
        EnvironmentOverride proton_path("PROTONPATH", QByteArray("/outside/proton"));
        EnvironmentOverride proton_verb("PROTON_VERB", QByteArray("runinprefix"));
        EnvironmentOverride proton_wined3d("PROTON_USE_WINED3D", QByteArray("1"));
        EnvironmentOverride no_proton("UMU_NO_PROTON", QByteArray("1"));
        EnvironmentOverride no_runtime("UMU_NO_RUNTIME", QByteArray("1"));
        EnvironmentOverride runtime_update("UMU_RUNTIME_UPDATE", QByteArray("0"));
        EnvironmentOverride container_nsenter("UMU_CONTAINER_NSENTER", QByteArray("1"));
        EnvironmentOverride zenity("UMU_ZENITY", QByteArray("1"));
        EnvironmentOverride runtime_path("RUNTIMEPATH", QByteArray("steamrt2"));
        EnvironmentOverride umu_steam_game("UMU_STEAM_GAME_ID", QByteArray("789"));
        EnvironmentOverride preload(
            "LD_PRELOAD",
            QByteArray("/steam/gameoverlayrenderer.so:/usr/lib/libgamemodeauto.so.0"));
        EnvironmentOverride tmpdir("TMPDIR", tmp.toUtf8());

        const QString prefix = directory.filePath(QStringLiteral("compat/pfx"));
        const QString compat = directory.filePath(QStringLiteral("compat"));
        soa::runtime::RuntimeSettings settings {
            proton, prefix, compat, QStringLiteral("win64"),
            QStringLiteral("UMU_FOLDERS_PATH=/escape UMU_NO_RUNTIME=1 RUNTIMEPATH=steamrt2"),
            true};
        const QProcessEnvironment environment =
            soa::runtime::RuntimeLocator::make_umu_environment(settings, directory.path());



        QCOMPARE(environment.value(QStringLiteral("WINEPREFIX")), prefix);
        QCOMPARE(environment.value(QStringLiteral("PROTONPATH")), directory.path());
        QVERIFY(!environment.contains(QStringLiteral("PROTON_VERB")));
        QVERIFY(!environment.contains(QStringLiteral("PROTON_USE_WINED3D")));
        QVERIFY(!environment.contains(QStringLiteral("UMU_NO_PROTON")));
        QVERIFY(!environment.contains(QStringLiteral("UMU_NO_RUNTIME")));
        QVERIFY(!environment.contains(QStringLiteral("UMU_RUNTIME_UPDATE")));
        QVERIFY(!environment.contains(QStringLiteral("UMU_CONTAINER_NSENTER")));
        QVERIFY(!environment.contains(QStringLiteral("UMU_ZENITY")));
        QVERIFY(!environment.contains(QStringLiteral("UMU_STEAM_GAME_ID")));
        QVERIFY(!environment.contains(QStringLiteral("RUNTIMEPATH")));
        QCOMPARE(environment.value(QStringLiteral("GAMEID")), QStringLiteral("umu-storyofalicia"));
        QCOMPARE(environment.value(QStringLiteral("UMU_FOLDERS_PATH")),
                 soa::runtime::managed_umu_data_home());
        QVERIFY(!environment.contains(QStringLiteral("SteamGameId")));
        QCOMPARE(environment.value(QStringLiteral("TMPDIR")), tmp);
        QVERIFY(!environment.contains(QStringLiteral("STEAM_COMPAT_DATA_PATH")));
        QVERIFY(!environment.contains(QStringLiteral("STEAM_COMPAT_CLIENT_INSTALL_PATH")));
        QVERIFY(!environment.contains(QStringLiteral("STEAM_COMPAT_LIBRARY_PATHS")));
        QVERIFY(!environment.contains(QStringLiteral("SteamAppId")));
        QCOMPARE(environment.value(QStringLiteral("LD_PRELOAD")),
                 QStringLiteral("/usr/lib/libgamemodeauto.so.0"));
    }

    void managed_umu_proton_omits_protonpath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("compat/pfx"));
        const QString compat = directory.filePath(QStringLiteral("compat"));
        soa::runtime::RuntimeSettings settings {
            soa::runtime::managed_proton_identifier(), prefix, compat,
            QStringLiteral("win64"), QString(), true};

        const QProcessEnvironment environment =
            soa::runtime::RuntimeLocator::make_umu_environment(
                settings, soa::runtime::managed_proton_identifier());

        QVERIFY(!environment.contains(QStringLiteral("PROTONPATH")));
        QCOMPARE(environment.value(QStringLiteral("WINEPREFIX")), prefix);
        QCOMPARE(environment.value(QStringLiteral("UMU_FOLDERS_PATH")),
                 soa::runtime::managed_umu_data_home());
        QVERIFY(QFileInfo(soa::runtime::managed_proton_identifier()).isAbsolute());
        QVERIFY(soa::runtime::managed_proton_identifier().startsWith(
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("runtimes")) + QDir::separator()));
        QCOMPARE(soa::runtime::WineRegistry::identify(
                     soa::runtime::managed_proton_identifier()),
                 soa::runtime::RuntimeType::Proton);
    }


    void managed_proton_detection_does_not_capture_custom_builds()
    {
        const QString dataHome = soa::runtime::managed_umu_data_home();
        const QString identifier = soa::runtime::managed_proton_identifier();
        const QString current = QDir(dataHome).filePath(
            QStringLiteral("umu/compatibilitytools/UMU-Proton-10.0-4"));
        const QString legacy = QDir(dataHome).filePath(
            QStringLiteral("Steam/compatibilitytools.d/UMU-Proton-10.0-4"));
        const QString custom = QDir(dataHome).filePath(
            QStringLiteral("umu/compatibilitytools/GE-Proton10-15"));

        QCOMPARE(identifier, QDir(dataHome).filePath(
            QStringLiteral("umu/compatibilitytools/UMU-Latest")));
        QVERIFY(soa::runtime::is_managed_proton(identifier));
        QVERIFY(soa::runtime::is_managed_proton(current));
        QVERIFY(soa::runtime::is_managed_proton(legacy));
        QVERIFY(!soa::runtime::is_managed_proton(custom));
    }

    void custom_proton_requires_internal_wine_entry_point()
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

        bool usable = true;
        QCOMPARE(soa::runtime::WineRegistry::identify(directory.path(), &usable),
                 soa::runtime::RuntimeType::Proton);
        QVERIFY(!usable);

        const QString bin = directory.filePath(QStringLiteral("files/bin"));
        QVERIFY(QDir().mkpath(bin));
        QFile wine(QDir(bin).filePath(QStringLiteral("wine")));
        QVERIFY(wine.open(QIODevice::WriteOnly));
        QVERIFY(wine.write("#!/bin/sh\nexit 0\n") > 0);
        wine.close();
        QVERIFY(wine.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner));

        QCOMPARE(soa::runtime::WineRegistry::identify(directory.path(), &usable),
                 soa::runtime::RuntimeType::Proton);
        QVERIFY(usable);
    }

    void umu_environment_uses_wined3d_when_dxvk_is_disabled()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("compat/pfx"));
        soa::runtime::RuntimeSettings settings {
            QStringLiteral("proton"), prefix, QString(), QStringLiteral("win64"), QString(), false};

        const QProcessEnvironment environment =
            soa::runtime::RuntimeLocator::make_umu_environment(settings, directory.path());

        QCOMPARE(environment.value(QStringLiteral("WINEPREFIX")), prefix);
        QCOMPARE(environment.value(QStringLiteral("PROTONPATH")), directory.path());
        QCOMPARE(environment.value(QStringLiteral("PROTON_USE_WINED3D")), QStringLiteral("1"));
        QCOMPARE(environment.value(QStringLiteral("WINEDLLOVERRIDES")),
                 QStringLiteral("winegstreamer="));
        QCOMPARE(environment.value(QStringLiteral("GAMEID")), QStringLiteral("umu-storyofalicia"));
        QCOMPARE(environment.value(QStringLiteral("STORE")), QStringLiteral("none"));
    }

    void umu_environment_drops_invalid_tmpdir()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString invalid_tmp = directory.filePath(QStringLiteral("missing"));
        EnvironmentOverride tmpdir("TMPDIR", invalid_tmp.toUtf8());

        const QString prefix = directory.filePath(QStringLiteral("compat/pfx"));
        soa::runtime::RuntimeSettings settings {QStringLiteral("proton"), prefix, QString(),
                                               QStringLiteral("win64"), QString(), true};
        const QProcessEnvironment environment =
            soa::runtime::RuntimeLocator::make_umu_environment(settings, directory.path());

        QVERIFY(!environment.contains(QStringLiteral("TMPDIR")));

        QCOMPARE(environment.value(QStringLiteral("WINEPREFIX")), prefix);
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
        soa::runtime::macos::apply_runtime_environment(environment, winePath);
        QCOMPARE(environment.value(QStringLiteral("CX_ROOT")), cxRoot);
    }
#endif

};

QTEST_MAIN(RuntimeLocatorTests)
#include "runtime_locator_tests.moc"
