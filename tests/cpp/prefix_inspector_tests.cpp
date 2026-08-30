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

class PrefixInspectorTests final : public QObject
{
    Q_OBJECT

private slots:
    void prefix_marker_tracks_runtime_changes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString runtime = directory.filePath(QStringLiteral("wine"));
        QVERIFY(QDir().mkpath(prefix));
        QFile executable(runtime);
        QVERIFY(executable.open(QIODevice::WriteOnly));
        QCOMPARE(executable.write("runtime-a"), qint64(9));
        executable.close();

        QVERIFY(core::wine::PrefixInspector::write_marker(prefix, runtime));
        QVERIFY(core::wine::PrefixInspector::marker_valid(prefix, runtime));

        QVERIFY(executable.open(QIODevice::Append));
        QCOMPARE(executable.write("-changed"), qint64(8));
        executable.close();
        QVERIFY(!core::wine::PrefixInspector::marker_valid(prefix, runtime));
    }

    void dxvk_requires_files_and_native_overrides()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString dllDirectory = QDir(prefix).filePath(
            QStringLiteral("drive_c/windows/syswow64"));
        QVERIFY(QDir().mkpath(dllDirectory));

        QFile systemRegistry(QDir(prefix).filePath(QStringLiteral("system.reg")));
        QVERIFY(systemRegistry.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(systemRegistry.write("#arch=win64\n") > 0);
        systemRegistry.close();

        for (const QString& dll : {QStringLiteral("d3d9.dll"), QStringLiteral("dxgi.dll")})
        {
            QFile file(QDir(dllDirectory).filePath(dll));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("dxvk") > 0);
        }

        QFile userRegistry(QDir(prefix).filePath(QStringLiteral("user.reg")));
        QVERIFY(userRegistry.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(userRegistry.write(
            "[Software\\Wine\\DllOverrides]\n"
            "\"*d3d9\"=\"native\"\n"
            "\"*dxgi\"=\"native,builtin\"\n") > 0);
        userRegistry.close();
        QVERIFY(core::wine::PrefixInspector::dxvk_installed(prefix));
        auto inspection = core::wine::PrefixInspector::inspect(
            prefix, QString(), false);
        QVERIFY(inspection.dxvk_files_present);
        QVERIFY(inspection.dxvk_overrides_present);

        QVERIFY(userRegistry.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        QVERIFY(userRegistry.write(
            "\"*d3d9\"=\"builtin\"\n"
            "\"*dxgi\"=\"native\"\n") > 0);
        userRegistry.close();
        QVERIFY(!core::wine::PrefixInspector::dxvk_installed(prefix));
        inspection = core::wine::PrefixInspector::inspect(
            prefix, QString(), false);
        QVERIFY(inspection.dxvk_files_present);
        QVERIFY(!inspection.dxvk_overrides_present);
    }

    void dxvk_accepts_unstarred_manual_overrides()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString dllDirectory = QDir(prefix).filePath(
            QStringLiteral("drive_c/windows/syswow64"));
        QVERIFY(QDir().mkpath(dllDirectory));

        QFile systemRegistry(QDir(prefix).filePath(QStringLiteral("system.reg")));
        QVERIFY(systemRegistry.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(systemRegistry.write("#arch=win64\n") > 0);
        systemRegistry.close();

        for (const QString& dll : {QStringLiteral("d3d9.dll"), QStringLiteral("dxgi.dll")})
        {
            QFile file(QDir(dllDirectory).filePath(dll));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("dxvk") > 0);
        }

        QFile userRegistry(QDir(prefix).filePath(QStringLiteral("user.reg")));
        QVERIFY(userRegistry.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(userRegistry.write(
            "\"d3d9\"=\"native\"\n"
            "\"dxgi\"=\"native\"\n") > 0);
        userRegistry.close();
        QVERIFY(core::wine::PrefixInspector::dxvk_installed(prefix));
        const auto inspection = core::wine::PrefixInspector::inspect(
            prefix, QString(), false);
        QVERIFY(inspection.dxvk_files_present);
        QVERIFY(inspection.dxvk_overrides_present);
    }

    void dxvk_uses_a_reproducible_winetricks_verb()
    {
        QCOMPARE(core::wine::PrefixInspector::dxvk_winetricks_verb(),
                 QStringLiteral("dxvk2071"));
#if !defined(Q_OS_MACOS)
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QStringList packages =
            core::wine::PrefixInspector::missing_packages(
                directory.filePath(QStringLiteral("prefix")),
                false,
                true);
        QVERIFY(packages.contains(
            core::wine::PrefixInspector::dxvk_winetricks_verb()));
        QVERIFY(!packages.contains(QStringLiteral("dxvk")));
#endif
    }


    void reads_architecture_from_user_registry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        QVERIFY(QDir().mkpath(prefix));

        QFile systemRegistry(QDir(prefix).filePath(QStringLiteral("system.reg")));
        QVERIFY(systemRegistry.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(systemRegistry.write("WINE REGISTRY Version 2\n") > 0);
        systemRegistry.close();

        QFile userRegistry(QDir(prefix).filePath(QStringLiteral("user.reg")));
        QVERIFY(userRegistry.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(userRegistry.write("#arch=win64\n") > 0);
        userRegistry.close();

        QCOMPARE(core::wine::PrefixInspector::architecture(prefix),
                 core::wine::PrefixArchitecture::Win64);
    }

#if defined(Q_OS_MACOS)
    void falls_back_to_system32_when_syswow64_is_absent()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString system32 = QDir(prefix).filePath(
            QStringLiteral("drive_c/windows/system32"));
        QVERIFY(QDir().mkpath(system32));

        QFile systemRegistry(QDir(prefix).filePath(QStringLiteral("system.reg")));
        QVERIFY(systemRegistry.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(systemRegistry.write("#arch=win64\n") > 0);
        systemRegistry.close();

        QCOMPARE(core::wine::PrefixInspector::game_dll_directory(prefix), system32);
    }

    void accepts_new_wow64_prefix_structure_without_arch_marker()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString driveC = QDir(prefix).filePath(QStringLiteral("drive_c"));
        QVERIFY(QDir().mkpath(QDir(driveC).filePath(QStringLiteral("windows/system32"))));
        QVERIFY(QDir().mkpath(QDir(prefix).filePath(QStringLiteral("dosdevices"))));
        QVERIFY(QFile::link(driveC, QDir(prefix).filePath(QStringLiteral("dosdevices/c:"))));

        for (const QString& name : {QStringLiteral("system.reg"), QStringLiteral("user.reg")})
        {
            QFile registry(QDir(prefix).filePath(name));
            QVERIFY(registry.open(QIODevice::WriteOnly | QIODevice::Text));
            QVERIFY(registry.write("WINE REGISTRY Version 2\n") > 0);
            registry.close();
        }

        const auto inspection = core::wine::PrefixInspector::inspect(
            prefix, QStringLiteral("/tmp/wine"), false);
        QVERIFY(inspection.exists);
        QVERIFY(inspection.structure_valid);
        QCOMPARE(inspection.architecture, core::wine::PrefixArchitecture::Win64);



        QVERIFY(inspection.required_components_present(false));
        QVERIFY(core::wine::PrefixInspector::missing_packages(
                    prefix, false, false).isEmpty());
        QCOMPARE(core::wine::PrefixInspector::game_dll_directory(prefix),
                 QDir(prefix).filePath(QStringLiteral("drive_c/windows/system32")));
    }

    void new_wow64_prefix_becomes_ready_after_legacy_components_are_present()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString driveC = QDir(prefix).filePath(QStringLiteral("drive_c"));
        const QString system32 = QDir(driveC).filePath(QStringLiteral("windows/system32"));
        QVERIFY(QDir().mkpath(system32));
        QVERIFY(QDir().mkpath(QDir(prefix).filePath(QStringLiteral("dosdevices"))));
        QVERIFY(QFile::link(driveC, QDir(prefix).filePath(QStringLiteral("dosdevices/c:"))));

        for (const QString& name : {QStringLiteral("system.reg"), QStringLiteral("user.reg")})
        {
            QFile registry(QDir(prefix).filePath(name));
            QVERIFY(registry.open(QIODevice::WriteOnly | QIODevice::Text));
            QVERIFY(registry.write("WINE REGISTRY Version 2\n") > 0);
            registry.close();
        }

        const QStringList requiredDlls {
            QStringLiteral("d3dx9_31.dll"),
            QStringLiteral("d3dx9_42.dll"),
            QStringLiteral("d3dcompiler_42.dll"),
            QStringLiteral("msvcp100.dll"),
            QStringLiteral("msvcr100.dll")
        };
        for (const QString& dll : requiredDlls)
        {
            QFile file(QDir(system32).filePath(dll));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("test") > 0);
            file.close();
        }

        const auto inspection = core::wine::PrefixInspector::inspect(
            prefix, QStringLiteral("/tmp/wine"), false);
        QVERIFY(inspection.structure_valid);
        QCOMPARE(inspection.architecture, core::wine::PrefixArchitecture::Win64);
        QVERIFY(inspection.required_components_present(false));
        QVERIFY(!inspection.physx_runtime);
    }
#endif

};

QTEST_MAIN(PrefixInspectorTests)
#include "prefix_inspector_tests.moc"
