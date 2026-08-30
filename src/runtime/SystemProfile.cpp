#include "runtime/SystemProfile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QSysInfo>
#include <QtGlobal>

#include "runtime/MacWineRuntime.hpp"

namespace core::system
{
    namespace
    {
        QString run_command(const QString& program, const QStringList& arguments, const int timeout_ms)
        {
            if (program.isEmpty())
                return {};
            QProcess process;
            process.start(program, arguments);
            if (!process.waitForStarted(qMin(timeout_ms, 2000)))
                return {};
            if (!process.waitForFinished(timeout_ms))
            {
                process.terminate();
                if (!process.waitForFinished(1000))
                    process.kill();
                return {};
            }
            return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        }

        CpuArchitecture current_architecture()
        {
            const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
            if (architecture == QStringLiteral("arm64") || architecture == QStringLiteral("aarch64"))
                return CpuArchitecture::Arm64;
            if (architecture == QStringLiteral("x86_64") || architecture == QStringLiteral("amd64"))
                return CpuArchitecture::X86_64;
            return CpuArchitecture::Unknown;
        }

        GpuVendor vendor_from_text(const QString& text)
        {
            const QString lower = text.toLower();
            if (lower.contains(QStringLiteral("apple")))
                return GpuVendor::Apple;
            if (lower.contains(QStringLiteral("nvidia")) || lower.contains(QStringLiteral("10de")))
                return GpuVendor::Nvidia;
            if (lower.contains(QStringLiteral("amd")) || lower.contains(QStringLiteral("ati"))
                || lower.contains(QStringLiteral("advanced micro devices"))
                || lower.contains(QStringLiteral("1002")))
                return GpuVendor::Amd;
            if (lower.contains(QStringLiteral("intel")) || lower.contains(QStringLiteral("8086")))
                return GpuVendor::Intel;
            return GpuVendor::Unknown;
        }

        int vendor_priority(const GpuVendor vendor)
        {
            switch (vendor)
            {
                case GpuVendor::Apple: return 4;
                case GpuVendor::Nvidia: return 3;
                case GpuVendor::Amd: return 2;
                case GpuVendor::Intel: return 1;
                default: return 0;
            }
        }

#if defined(Q_OS_LINUX)
        void detect_linux_gpus(SystemProfile& profile)
        {
            const QString lspci = QStandardPaths::findExecutable(QStringLiteral("lspci"));
            const QString output = run_command(lspci, {QStringLiteral("-mm"), QStringLiteral("-nn")}, 3000);
            for (const QString& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            {
                const QString lower = line.toLower();
                if (!lower.contains(QStringLiteral("vga compatible controller"))
                    && !lower.contains(QStringLiteral("3d controller"))
                    && !lower.contains(QStringLiteral("display controller")))
                    continue;
                profile.gpu_descriptions.append(line.trimmed());
                const GpuVendor candidate = vendor_from_text(line);
                if (vendor_priority(candidate) > vendor_priority(profile.gpu_vendor))
                    profile.gpu_vendor = candidate;
            }

            if (profile.gpu_vendor != GpuVendor::Unknown)
                return;

            QDir drm(QStringLiteral("/sys/class/drm"));
            const QRegularExpression card_pattern(QStringLiteral(R"(^card\d+$)"));
            for (const QString& entry : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            {
                if (!card_pattern.match(entry).hasMatch())
                    continue;
                QFile vendor_file(drm.filePath(entry + QStringLiteral("/device/vendor")));
                if (!vendor_file.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                const GpuVendor candidate = vendor_from_text(
                    QString::fromUtf8(vendor_file.readAll()).trimmed());
                if (vendor_priority(candidate) > vendor_priority(profile.gpu_vendor))
                    profile.gpu_vendor = candidate;
            }
        }

        bool linux_vulkan_likely()
        {
            const QString vulkaninfo = QStandardPaths::findExecutable(QStringLiteral("vulkaninfo"));
            if (vulkaninfo.isEmpty())
                return false;

            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(vulkaninfo, {QStringLiteral("--summary")});
            if (!process.waitForStarted(2000) || !process.waitForFinished(8000))
            {
                process.kill();
                process.waitForFinished(1000);
                return false;
            }
            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
                return false;

            const QString output = QString::fromUtf8(process.readAll()).toLower();
            return output.contains(QStringLiteral("device name"))
                || output.contains(QStringLiteral("devicename"))
                || output.contains(QStringLiteral("gpu"));
        }
#endif

#if defined(Q_OS_MACOS)
        void detect_macos_gpus(SystemProfile& profile)
        {
            const QString output = run_command(
                QStringLiteral("/usr/sbin/system_profiler"),
                {QStringLiteral("SPDisplaysDataType"), QStringLiteral("-detailLevel"),
                 QStringLiteral("mini")},
                8000);
            for (const QString& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            {
                const QString trimmed = line.trimmed();
                if (!trimmed.startsWith(QStringLiteral("Chipset Model:"), Qt::CaseInsensitive)
                    && !trimmed.startsWith(QStringLiteral("Model:"), Qt::CaseInsensitive))
                    continue;
                profile.gpu_descriptions.append(trimmed);
                const GpuVendor candidate = vendor_from_text(trimmed);
                if (vendor_priority(candidate) > vendor_priority(profile.gpu_vendor))
                    profile.gpu_vendor = candidate;
            }
            if (profile.cpu_architecture == CpuArchitecture::Arm64
                && profile.gpu_vendor == GpuVendor::Unknown)
            {
                profile.gpu_vendor = GpuVendor::Apple;
                profile.gpu_descriptions.append(QStringLiteral("Apple GPU"));
            }
        }

        bool rosetta_probe()
        {
            return core::wine::macos::rosetta_is_available();
        }
#endif
    }

    SystemProfile detect_system_profile()
    {
        SystemProfile profile;
        profile.cpu_architecture = current_architecture();
#if defined(Q_OS_LINUX)
        detect_linux_gpus(profile);
        profile.vulkan_likely = linux_vulkan_likely();
#elif defined(Q_OS_MACOS)
        detect_macos_gpus(profile);
        profile.rosetta_available = rosetta_probe();
#endif
        return profile;
    }
}
