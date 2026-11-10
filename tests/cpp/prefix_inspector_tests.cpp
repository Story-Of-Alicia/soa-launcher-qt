// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

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
#include "runtime/PrefixInspector.hpp"
#include "runtime/ProcessRunner.hpp"
#include "runtime/RuntimeLocator.hpp"
#include "runtime/WineProcess.hpp"
#include "runtime/WineRegistry.hpp"
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

        QVERIFY(soa::runtime::PrefixInspector::write_marker(prefix, runtime));
        QVERIFY(soa::runtime::PrefixInspector::marker_valid(prefix, runtime));

        QVERIFY(executable.open(QIODevice::Append));
        QCOMPARE(executable.write("-changed"), qint64(8));
        executable.close();
        QVERIFY(!soa::runtime::PrefixInspector::marker_valid(prefix, runtime));
    }

    void managed_proton_marker_survives_managed_version_updates()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        QVERIFY(QDir().mkpath(prefix));

        const QString root = QDir(soa::runtime::managed_umu_data_home()).filePath(
            QStringLiteral("Steam/compatibilitytools.d"));
        const QString first = QDir(root).filePath(QStringLiteral("UMU-Proton-10.0-4"));
        const QString next = QDir(root).filePath(QStringLiteral("UMU-Proton-10.0-5"));

        QVERIFY(soa::runtime::PrefixInspector::write_marker(prefix, first));
        QVERIFY(soa::runtime::PrefixInspector::marker_valid(prefix, first));
        QVERIFY(soa::runtime::PrefixInspector::marker_valid(prefix, next));
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
        QVERIFY(soa::runtime::PrefixInspector::dxvk_installed(prefix));
        auto inspection = soa::runtime::PrefixInspector::inspect(
            prefix, QString(), false);
        QVERIFY(inspection.dxvk_files_present);
        QVERIFY(inspection.dxvk_overrides_present);

        QVERIFY(userRegistry.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        QVERIFY(userRegistry.write(
            "\"*d3d9\"=\"builtin\"\n"
            "\"*dxgi\"=\"native\"\n") > 0);
        userRegistry.close();
        QVERIFY(!soa::runtime::PrefixInspector::dxvk_installed(prefix));
        inspection = soa::runtime::PrefixInspector::inspect(
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
        QVERIFY(soa::runtime::PrefixInspector::dxvk_installed(prefix));
        const auto inspection = soa::runtime::PrefixInspector::inspect(
            prefix, QString(), false);
        QVERIFY(inspection.dxvk_files_present);
        QVERIFY(inspection.dxvk_overrides_present);
    }

    void dxvk_uses_a_reproducible_winetricks_verb()
    {
        QCOMPARE(soa::runtime::PrefixInspector::dxvk_winetricks_verb(),
                 QStringLiteral("dxvk2071"));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QStringList packages =
            soa::runtime::PrefixInspector::missing_packages(
                directory.filePath(QStringLiteral("prefix")),
                false,
                true);
        QVERIFY(packages.contains(
            soa::runtime::PrefixInspector::dxvk_winetricks_verb()));
        QVERIFY(!packages.contains(QStringLiteral("dxvk")));
    }

    void proton_prefix_does_not_require_wine_dxvk_or_vcrun_setup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        QVERIFY(QDir().mkpath(QDir(prefix).filePath(QStringLiteral("drive_c/windows/syswow64"))));

        const auto inspection = soa::runtime::PrefixInspector::inspect(
            prefix, soa::runtime::managed_proton_identifier(), true);
        QVERIFY(inspection.dxvk_installed);
        QVERIFY(inspection.msvc_runtime);

        const QStringList packages = soa::runtime::PrefixInspector::missing_packages(
            prefix, true, true);
        QVERIFY(packages.contains(QStringLiteral("d3dx9")));
        QVERIFY(packages.contains(QStringLiteral("d3dcompiler_47")));
        QVERIFY(!packages.contains(QStringLiteral("vcrun2019")));
        QVERIFY(!packages.contains(soa::runtime::PrefixInspector::dxvk_winetricks_verb()));
    }

    void winetricks_backend_matches_actual_prefix_work()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));

        QCOMPARE(soa::runtime::PrefixInspector::required_winetricks_backend(
                     prefix, soa::runtime::RuntimeType::Wine, QStringLiteral("/tmp/wine"), false),
                 soa::runtime::WinetricksBackend::Host);

        const QString geProton = directory.filePath(QStringLiteral("GE-Proton10-1"));
        QVERIFY(QDir().mkpath(geProton));
        QCOMPARE(soa::runtime::PrefixInspector::required_winetricks_backend(
                     prefix, soa::runtime::RuntimeType::Proton, geProton, false),
                 soa::runtime::WinetricksBackend::Umu);

        const QString plainProton = directory.filePath(QStringLiteral("Proton-10"));
        QVERIFY(QDir().mkpath(plainProton));
        QCOMPARE(soa::runtime::PrefixInspector::required_winetricks_backend(
                     prefix, soa::runtime::RuntimeType::Proton, plainProton, false),
                 soa::runtime::WinetricksBackend::Host);

        const QString system32 = QDir(prefix).filePath(QStringLiteral("drive_c/windows/system32"));
        QVERIFY(QDir().mkpath(system32));
        for (const QString& dll : {QStringLiteral("d3dx9_43.dll"),
                                   QStringLiteral("d3dcompiler_47.dll"),
                                   QStringLiteral("msvcp140.dll"),
                                   QStringLiteral("vcruntime140.dll")})
        {
            QFile file(QDir(system32).filePath(dll));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("ready") > 0);
        }

        QCOMPARE(soa::runtime::PrefixInspector::required_winetricks_backend(
                     prefix, soa::runtime::RuntimeType::Wine, QStringLiteral("/tmp/wine"), false),
                 soa::runtime::WinetricksBackend::None);
        QCOMPARE(soa::runtime::PrefixInspector::required_winetricks_backend(
                     prefix, soa::runtime::RuntimeType::Wine, QStringLiteral("/tmp/wine"), true),
                 soa::runtime::WinetricksBackend::Host);
        QCOMPARE(soa::runtime::PrefixInspector::required_winetricks_backend(
                     prefix, soa::runtime::RuntimeType::Proton, plainProton, false),
                 soa::runtime::WinetricksBackend::None);
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

        QCOMPARE(soa::runtime::PrefixInspector::architecture(prefix),
                 soa::runtime::PrefixArchitecture::Win64);
    }
};

QTEST_MAIN(PrefixInspectorTests)
#include "prefix_inspector_tests.moc"
