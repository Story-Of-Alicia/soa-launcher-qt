#include "runtime/PrefixInspector.hpp"
#include "runtime/MacWineRuntime.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace core::wine
{
    namespace
    {
        QString marker_path(const QString& prefix)
        {
            return QDir(prefix).filePath(QStringLiteral(".soa-prefix-ready"));
        }

        QString runtime_fingerprint(const QString& runtime)
        {
            QFileInfo target(runtime);
            if (target.isDir())
            {
                const QFileInfo proton(QDir(runtime).filePath(QStringLiteral("proton")));
                if (proton.exists())
                    target = proton;
                else
                {
                    const QString wine = macos::resolve_wine_executable(runtime);
                    if (!wine.isEmpty())
                        target = QFileInfo(wine);
                }
            }
            const QString canonical = target.canonicalFilePath();
            const QString path = canonical.isEmpty() ? target.absoluteFilePath() : canonical;
            return QStringLiteral("%1|%2|%3")
                .arg(path)
                .arg(target.exists() ? target.size() : -1)
                .arg(target.exists() ? target.lastModified().toMSecsSinceEpoch() : -1);
        }

        bool native_override(const QString& prefix, const QString& dll)
        {
            QFile registry(QDir(prefix).filePath(QStringLiteral("user.reg")));
            if (!registry.open(QIODevice::ReadOnly | QIODevice::Text))
                return false;
            const QString content = QString::fromUtf8(registry.readAll());



            const QRegularExpression expression(
                QStringLiteral("\"\\*?%1\"=\"([^\"]+)\"")
                    .arg(QRegularExpression::escape(dll)),
                QRegularExpression::CaseInsensitiveOption);
            const auto match = expression.match(content);
            if (!match.hasMatch())
                return false;
            const QString value = match.captured(1).toLower();
            return value == QStringLiteral("native")
                || value == QStringLiteral("n")
                || value.startsWith(QStringLiteral("native,"));
        }

        bool dxvk_files_available(const QString& prefix)
        {
            return PrefixInspector::component_exists(
                       prefix, QStringLiteral("d3d9.dll"))
                && PrefixInspector::component_exists(
                       prefix, QStringLiteral("dxgi.dll"));
        }

        bool dxvk_overrides_available(const QString& prefix)
        {
            return native_override(prefix, QStringLiteral("d3d9"))
                && native_override(prefix, QStringLiteral("dxgi"));
        }

        PrefixArchitecture registry_architecture(const QString& prefix)
        {
            static const QRegularExpression expression(
                QStringLiteral(R"((?:^|\n)\s*#arch\s*=\s*(win32|win64)\s*(?:\r?$))"),
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
            const QStringList registry_files {
                QStringLiteral("system.reg"),
                QStringLiteral("user.reg"),
                QStringLiteral("userdef.reg")
            };

            for (const QString& file_name : registry_files)
            {
                QFile registry(QDir(prefix).filePath(file_name));
                if (!registry.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                const QString header = QString::fromUtf8(registry.read(256 * 1024));
                const auto match = expression.match(header);
                if (!match.hasMatch())
                    continue;
                return match.captured(1).compare(QStringLiteral("win64"), Qt::CaseInsensitive) == 0
                    ? PrefixArchitecture::Win64
                    : PrefixArchitecture::Win32;
            }
            return PrefixArchitecture::Unknown;
        }

        bool prefix_structure_valid(const QString& prefix)
        {
            const QDir root(prefix);
            const QFileInfo system_registry(root.filePath(QStringLiteral("system.reg")));
            const QFileInfo user_registry(root.filePath(QStringLiteral("user.reg")));
            const QDir windows(root.filePath(QStringLiteral("drive_c/windows")));
            const QDir system32(windows.filePath(QStringLiteral("system32")));
            const QDir dosdevices(root.filePath(QStringLiteral("dosdevices")));
            const QFileInfo c_drive(dosdevices.filePath(QStringLiteral("c:")));

            return system_registry.isFile() && system_registry.size() > 0
                && user_registry.isFile() && user_registry.size() > 0
                && windows.exists() && system32.exists()
                && dosdevices.exists() && (c_drive.exists() || c_drive.isSymLink());
        }
    }

    PrefixArchitecture PrefixInspector::architecture(const QString& prefix)
    {
        const PrefixArchitecture recorded = registry_architecture(prefix);
        if (recorded != PrefixArchitecture::Unknown)
            return recorded;
        if (QFileInfo::exists(QDir(prefix).filePath(QStringLiteral("drive_c/windows/syswow64"))))
            return PrefixArchitecture::Win64;
#if defined(Q_OS_MACOS)
        if (prefix_structure_valid(prefix))
            return PrefixArchitecture::Win64;
#endif
        return PrefixArchitecture::Unknown;
    }

    QString PrefixInspector::game_dll_directory(const QString& prefix)
    {
        const QString windows = QDir(prefix).filePath(QStringLiteral("drive_c/windows"));
        if (architecture(prefix) == PrefixArchitecture::Win64)
        {
            const QString syswow64 = QDir(windows).filePath(QStringLiteral("syswow64"));
#if defined(Q_OS_MACOS)
            if (!QDir(syswow64).exists())
                return QDir(windows).filePath(QStringLiteral("system32"));
#endif
            return syswow64;
        }
        return QDir(windows).filePath(QStringLiteral("system32"));
    }

    bool PrefixInspector::component_exists(const QString& prefix, const QString& file_name)
    {
        return QFileInfo::exists(QDir(game_dll_directory(prefix)).filePath(file_name));
    }

    bool PrefixInspector::dxvk_installed(const QString& prefix)
    {
        return dxvk_files_available(prefix)
            && dxvk_overrides_available(prefix);
    }

    QString PrefixInspector::dxvk_winetricks_verb()
    {



        return QStringLiteral("dxvk2071");
    }

    bool PrefixInspector::marker_valid(const QString& prefix, const QString& runtime)
    {
        QFile marker(marker_path(prefix));
        if (!marker.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        const QString version = QString::fromUtf8(marker.readLine()).trimmed();
        const QString recordedRuntime = QString::fromUtf8(marker.readLine()).trimmed();
        const QString recordedFingerprint = QString::fromUtf8(marker.readLine()).trimmed();
        return version == QStringLiteral("5")
            && recordedRuntime == runtime
            && recordedFingerprint == runtime_fingerprint(runtime);
    }

    bool PrefixInspector::write_marker(const QString& prefix, const QString& runtime)
    {
        if (!QDir().mkpath(prefix))
            return false;
        QSaveFile marker(marker_path(prefix));
        if (!marker.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        marker.write("5\n");
        marker.write(runtime.toUtf8());
        marker.write("\n");
        marker.write(runtime_fingerprint(runtime).toUtf8());
        marker.write("\n");
        return marker.commit();
    }

    bool PrefixInspector::remove_marker(const QString& prefix)
    {
        const QString path = marker_path(prefix);
        return !QFileInfo::exists(path) || QFile::remove(path);
    }

    PrefixInspection PrefixInspector::inspect(const QString& prefix,
                                               const QString& runtime,
                                               const bool proton)
    {
        PrefixInspection result;
        result.exists = !prefix.isEmpty() && QDir(prefix).exists(QStringLiteral("drive_c"));
        if (!result.exists)
            return result;

        result.architecture = architecture(prefix);
        result.structure_valid = prefix_structure_valid(prefix);
        result.marker_valid = marker_valid(prefix, runtime);
        result.d3dx9_31 = component_exists(prefix, QStringLiteral("d3dx9_31.dll"));
        result.d3dx9_42 = component_exists(prefix, QStringLiteral("d3dx9_42.dll"));
        result.d3dx9_43 = component_exists(prefix, QStringLiteral("d3dx9_43.dll"));
        result.d3dcompiler_42 = component_exists(prefix, QStringLiteral("d3dcompiler_42.dll"));
        result.d3dcompiler_47 = component_exists(prefix, QStringLiteral("d3dcompiler_47.dll"));
        result.msvc2010_runtime = component_exists(prefix, QStringLiteral("msvcp100.dll"))
            && component_exists(prefix, QStringLiteral("msvcr100.dll"));
        result.physx_runtime = component_exists(prefix, QStringLiteral("PhysXLoader.dll"));
        result.msvc_runtime = proton
            || (component_exists(prefix, QStringLiteral("msvcp140.dll"))
                && component_exists(prefix, QStringLiteral("vcruntime140.dll")));
        result.dxvk_files_present = dxvk_files_available(prefix);
        result.dxvk_overrides_present =
            dxvk_overrides_available(prefix);
        result.dxvk_installed = proton
            || (result.dxvk_files_present
                && result.dxvk_overrides_present);
        return result;
    }

    QStringList PrefixInspector::missing_packages(const QString& prefix,
                                                  const bool proton,
                                                  const bool request_dxvk)
    {
        QStringList packages;
#if defined(Q_OS_MACOS)
        Q_UNUSED(proton);
        Q_UNUSED(request_dxvk);
        Q_UNUSED(prefix);


#else
        if (!component_exists(prefix, QStringLiteral("d3dx9_43.dll")))
            packages << QStringLiteral("d3dx9");
        if (!component_exists(prefix, QStringLiteral("d3dcompiler_47.dll")))
            packages << QStringLiteral("d3dcompiler_47");
        if (!proton
            && (!component_exists(prefix, QStringLiteral("msvcp140.dll"))
                || !component_exists(prefix, QStringLiteral("vcruntime140.dll"))))
        {
            packages << QStringLiteral("vcrun2019");
        }
        if (!proton && request_dxvk && !dxvk_installed(prefix))
            packages << dxvk_winetricks_verb();
#endif
        packages.removeDuplicates();
        return packages;
    }
}
