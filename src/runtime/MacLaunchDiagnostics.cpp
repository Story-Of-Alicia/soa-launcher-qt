#include "runtime/MacLaunchDiagnostics.hpp"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>

#include "runtime/MacWineRuntime.hpp"
#include "runtime/ProcessRunner.hpp"
#include "config/Config.hpp"
#include <spdlog/spdlog.h>

namespace core::wine
{
    using util::config::Config;

    namespace
    {
        constexpr int k_host_sample_delay_ms = 40 * 1000;

        QString safe_label(QString value)
        {
            value = value.trimmed().toLower();
            value.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                          QStringLiteral("-"));
            value.remove(QRegularExpression(QStringLiteral("(^-+|-+$)")));
            return value.isEmpty() ? QStringLiteral("game") : value.left(32);
        }

        void prune_old_diagnostics(const QString& directory, const int keep = 20)
        {
            QDir dir(directory);
            const QFileInfoList runs = dir.entryInfoList(
                {QStringLiteral("run-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
            for (int index = keep; index < runs.size(); ++index)
            {
                QDir old_run(runs[index].absoluteFilePath());
                if (!old_run.removeRecursively())
                {
                    SPDLOG_DEBUG("diagnostics: could not remove old run {}",
                                 runs[index].absoluteFilePath().toStdString());
                }
            }
        }

        QStringList known_log_findings(const QString& path)
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                return {};

            constexpr qint64 k_max_diagnosis_bytes = 4 * 1024 * 1024;
            if (file.size() > k_max_diagnosis_bytes)
                file.seek(file.size() - k_max_diagnosis_bytes);
            const QString log = QString::fromUtf8(file.readAll());

            struct Signature
            {
                const char* needle;
                const char* code;
            };
            const Signature signatures[] {
                {"cannot find the FreeType font library", "runtime_freetype_missing"},
                {"built without Vulkan support", "vulkan_unavailable"},
                {"CreateDevice failed", "d3d_device_failure"},
                {"unhandled exception", "unhandled_exception"},
                {"page fault", "page_fault"},
            };

            QStringList findings;
            for (const Signature& signature : signatures)
            {
                if (log.contains(QString::fromLatin1(signature.needle), Qt::CaseInsensitive))
                    findings.append(QString::fromLatin1(signature.code));
            }

            static const QStringList benign {
                QStringLiteral("winepulse.drv"),  QStringLiteral("winealsa.drv"),
                QStringLiteral("wineoss.drv"),    QStringLiteral("wineandroid.drv"),
                QStringLiteral("winecoreaudio.drv"), QStringLiteral("winex11.drv"),
                QStringLiteral("wineps.drv"),     QStringLiteral("mscoree.dll"),
                QStringLiteral("mscoree"),
            };
            static const QRegularExpression failed_module(
                QStringLiteral(R"RX(Failed to load module L"([^"]+)")RX"),
                QRegularExpression::CaseInsensitiveOption);
            auto matches = failed_module.globalMatch(log);
            while (matches.hasNext())
            {
                const QString name = matches.next().captured(1).toLower();
                if (benign.contains(name))
                {
                    continue;
                }
                if (!findings.contains(QStringLiteral("dll_load_failure")))
                {
                    findings.append(QStringLiteral("dll_load_failure"));
                }
                SPDLOG_WARN("Unexpected module load failure: {}", name.toStdString());
            }
            return findings;
        }

        quint16 pe_u16(const QByteArray& data, const qsizetype offset)
        {
            if (offset < 0 || offset + 2 > data.size())
                return 0;
            const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + offset);
            return quint16(p[0]) | (quint16(p[1]) << 8);
        }

        quint32 pe_u32(const QByteArray& data, const qsizetype offset)
        {
            if (offset < 0 || offset + 4 > data.size())
                return 0;
            const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + offset);
            return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) |
                   (quint32(p[3]) << 24);
        }

        struct PeImageInfo
        {
            bool valid {};
            bool pe32 {};
            quint64 image_base {};
            quint32 entry_point_rva {};
        };

        PeImageInfo inspect_pe_image(const QString& path)
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return {};
            const QByteArray data = file.read(4096);
            if (data.size() < 0x100 || data[0] != 'M' || data[1] != 'Z')
            {
                return {};
            }

            const quint32 pe_offset = pe_u32(data, 0x3c);
            if (pe_offset + 0x80u > quint32(data.size()) ||
                data.mid(pe_offset, 4) != QByteArray("PE\0\0", 4))
            {
                return {};
            }

            const qsizetype optional = qsizetype(pe_offset) + 24;
            const quint16 magic = pe_u16(data, optional);
            PeImageInfo info;
            info.pe32 = magic == 0x10b;
            if (!info.pe32 && magic != 0x20b)
                return {};
            info.entry_point_rva = pe_u32(data, optional + 16);
            info.image_base = info.pe32 ? pe_u32(data, optional + 28)
                                        : quint64(pe_u32(data, optional + 24)) |
                                              (quint64(pe_u32(data, optional + 28)) << 32);
            info.valid = info.image_base != 0;
            return info;
        }

        void append_crash_analysis(const QString& diagnostic_path, const QString& executable_path)
        {
            if (diagnostic_path.isEmpty() || executable_path.isEmpty())
            {
                return;
            }

            QFile input(diagnostic_path);
            if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                return;
            }
            const QString log = QString::fromUtf8(input.readAll());
            input.close();

            static const QRegularExpression exception_pattern(QStringLiteral(
                R"(Unhandled exception code\s+([0-9a-fA-F]+).*?addr\s+0x([0-9a-fA-F]+))"));
            QRegularExpressionMatch exception_match;
            auto iterator = exception_pattern.globalMatch(log);
            while (iterator.hasNext())
                exception_match = iterator.next();

            QFile output(diagnostic_path);
            if (!output.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            {
                return;
            }
            QTextStream trace(&output);
            trace << "\n=== AUTOMATIC FIRST-FAULT ANALYSIS ===\n";

            const PeImageInfo pe = inspect_pe_image(executable_path);
            trace << "Executable: " << executable_path << "\n"
                  << "PE valid=" << pe.valid << " pe32=" << pe.pe32 << " preferred_image_base=0x"
                  << QString::number(pe.image_base, 16) << " entry_rva=0x"
                  << QString::number(pe.entry_point_rva, 16) << "\n";

            if (!exception_match.hasMatch())
            {
                trace << "No final unhandled exception record was "
                         "found. The process may have exited "
                         "voluntarily or been terminated externally.\n";
                trace.flush();
                return;
            }

            bool address_ok = false;
            const quint64 fault = exception_match.captured(2).toULongLong(&address_ok, 16);
            trace << "Exception code=0x" << exception_match.captured(1).toLower()
                  << " fault_address=0x" << QString::number(fault, 16) << "\n";

            static const QRegularExpression module_base_pattern(
                QStringLiteral(R"(LdrGetDllHandleEx[^\n]*Alicia\.exe[^\n]*->\s*([0-9a-fA-F]+))"),
                QRegularExpression::CaseInsensitiveOption);
            quint64 observed_base = 0;
            auto base_iterator = module_base_pattern.globalMatch(log);
            while (base_iterator.hasNext())
            {
                observed_base = base_iterator.next().captured(1).toULongLong(nullptr, 16);
            }

            const quint64 image_base = observed_base != 0 ? observed_base : pe.image_base;
            if (address_ok && image_base != 0 && fault >= image_base)
            {
                const quint64 rva = fault - image_base;
                trace << "Observed image base=0x" << QString::number(image_base, 16)
                      << " fault_rva=0x" << QString::number(rva, 16) << "\n"
                      << "Classification: faulting instruction "
                         "belongs to Alicia.exe itself. This "
                         "identifies where an invalid engine state "
                         "was consumed, not necessarily where it "
                         "was created.\n";

                const qsizetype exception_offset = exception_match.capturedStart();
                const qsizetype context_start = qMax<qsizetype>(0, exception_offset - 5000);
                const qsizetype context_length = qMin<qsizetype>(12000, log.size() - context_start);
                const QString context = log.mid(context_start, context_length);
                QStringList selected;
                for (const QString& line : context.split(QLatin1Char('\n')))
                {
                    if (line.contains(QStringLiteral("Unhandled exception"), Qt::CaseInsensitive) ||
                        line.contains(QStringLiteral("Register dump"), Qt::CaseInsensitive) ||
                        line.contains(QRegularExpression(
                            QStringLiteral(R"(\b(EIP|ESP|EBP|EAX|EBX|ECX|EDX|ESI|EDI)\b)"),
                            QRegularExpression::CaseInsensitiveOption)) ||
                        line.contains(QStringLiteral("Backtrace"), Qt::CaseInsensitive) ||
                        line.contains(QStringLiteral("Stack dump"), Qt::CaseInsensitive))
                    {
                        selected.append(line);
                    }
                }
                trace << "\n--- register/stack evidence ---\n"
                      << selected.join(QLatin1Char('\n'))
                      << "\n--- end register/stack evidence ---\n";

                QString disassembler;
                for (const QString& candidate :
                     {QStringLiteral("llvm-objdump"), QStringLiteral("objdump"),
                      QStringLiteral("gobjdump")})
                {
                    disassembler = QStandardPaths::findExecutable(candidate);
                    if (!disassembler.isEmpty())
                        break;
                }
                if (!disassembler.isEmpty())
                {
                    const quint64 start = fault > 96 ? fault - 96 : image_base;
                    const quint64 stop = fault + 128;
                    QStringList arguments;
                    if (QFileInfo(disassembler)
                            .fileName()
                            .contains(QStringLiteral("llvm"), Qt::CaseInsensitive))
                    {
                        arguments << QStringLiteral("--x86-asm-syntax=intel");
                    }
                    arguments
                        << QStringLiteral("-d")
                        << QStringLiteral("--start-address=0x%1").arg(QString::number(start, 16))
                        << QStringLiteral("--stop-address=0x%1").arg(QString::number(stop, 16))
                        << executable_path;
                    QProcess disassembly;
                    disassembly.setProcessChannelMode(QProcess::MergedChannels);
                    disassembly.start(disassembler, arguments);
                    const bool finished =
                        disassembly.waitForStarted(5000) && disassembly.waitForFinished(20000);
                    if (!finished && disassembly.state() != QProcess::NotRunning)
                    {
                        disassembly.kill();
                        disassembly.waitForFinished(2000);
                    }
                    trace << "\n--- disassembly around fault ---\n"
                          << "Command: " << disassembler << " " << arguments.join(QLatin1Char(' '))
                          << "\n"
                          << QString::fromUtf8(disassembly.readAll())
                          << "\n--- end disassembly ---\n";
                }
                else
                {
                    trace << "No llvm-objdump/objdump was found. "
                             "Install Xcode Command Line Tools to "
                             "append automatic PE disassembly.\n";
                }

                const bool saw_d3d_create =
                    log.contains(QStringLiteral("Direct3DCreate9"), Qt::CaseInsensitive);
                const bool saw_create_device =
                    log.contains(QStringLiteral("CreateDevice"), Qt::CaseInsensitive);
                const bool saw_physx =
                    log.contains(QStringLiteral("PhysXLoader.dll"), Qt::CaseInsensitive);
                const bool missing_dll = log.contains(QRegularExpression(
                    QStringLiteral(
                        R"((Library .* not found|failed to load .*\.dll|status c0000135))"),
                    QRegularExpression::CaseInsensitiveOption));
                trace << "\n--- subsystem evidence ---\n"
                      << "Direct3DCreate9 observed=" << saw_d3d_create << "\n"
                      << "CreateDevice text observed=" << saw_create_device << "\n"
                      << "PhysXLoader observed=" << saw_physx << "\n"
                      << "Missing-DLL signature observed=" << missing_dll << "\n";
                if (missing_dll)
                {
                    trace << "Classification: loader failure "
                             "evidence exists; inspect the first "
                             "missing-DLL line above.\n";
                }
                else if (saw_d3d_create && !saw_create_device)
                {
                    trace << "Classification: crash follows D3D9 "
                             "entry but no CreateDevice trace was "
                             "captured; graphics initialization "
                             "remains the leading boundary.\n";
                }
                else if (saw_create_device)
                {
                    trace << "Classification: D3D device creation "
                             "was reached; inspect HRESULT/return "
                             "evidence and the disassembly before "
                             "blaming the renderer.\n";
                }
                else
                {
                    trace << "Classification: no conclusive graphics "
                             "call boundary was captured; use the "
                             "disassembly and register evidence as "
                             "source of truth.\n";
                }
            }
            else
            {
                trace << "The fault address could not be mapped into "
                         "Alicia.exe using the observed/preferred "
                         "image base.\n";
            }
            trace << "=== END AUTOMATIC FIRST-FAULT ANALYSIS ===\n";
            trace.flush();
        }
    }

    class MacLaunchDiagnostics::Impl
    {
    public:
        Configuration configuration;
        QFile diagnostic_file;
        QString run_directory;
        QString diagnostic_path;
        QString timeline_path;
        QString summary_path;
        QString session_id;
        QStringList sensitive_values;
        QElapsedTimer clock;
        std::shared_ptr<std::atomic_uint64_t> sample_generation {
            std::make_shared<std::atomic_uint64_t>(0)};
        SnapshotProvider snapshot_provider;
        quint64 launch_generation {};
        bool timeline_finished {};
        bool seen_create_device {};
        bool seen_present {};
        bool seen_draw {};
        bool seen_exception {};
    };

    MacLaunchDiagnostics::MacLaunchDiagnostics(QObject* parent)
        : QObject(parent), d_(std::make_unique<Impl>())
    {
    }

    MacLaunchDiagnostics::~MacLaunchDiagnostics()
    {
        reset();
    }

    void MacLaunchDiagnostics::reset()
    {
        cancel_host_sample();
        if (d_->diagnostic_file.isOpen())
            d_->diagnostic_file.close();
        ++d_->launch_generation;
        d_->diagnostic_path.clear();
        d_->timeline_path.clear();
        d_->run_directory.clear();
        d_->summary_path.clear();
        d_->session_id.clear();
        d_->sensitive_values.clear();
        d_->snapshot_provider = {};
        d_->clock.invalidate();
        d_->timeline_finished = false;
        d_->seen_create_device = false;
        d_->seen_present = false;
        d_->seen_draw = false;
        d_->seen_exception = false;
        d_->configuration = {};
    }

    MacLaunchDiagnostics::Configuration
    MacLaunchDiagnostics::begin(const core::game::GameVersion version, const QString& prefix,
                                const QString& game_directory, const QString& executable_path,
                                const QStringList& sensitive_values,
                                SnapshotProvider snapshot_provider)
    {
        reset();
        d_->configuration.supported = true;
#if defined(Q_OS_MACOS)
        d_->configuration.profile = Config::instance().macos_compatibility_profile();
#else
        d_->configuration.profile = QStringLiteral("default");
#endif
        d_->configuration.enabled = Config::instance().diagnostics_enabled();
        d_->configuration.wine_debug = wine_debug_value(d_->configuration.enabled);

        RuntimeContext& runtime = d_->configuration.runtime;
        runtime.selector = Config::instance().wine_binary();
#if defined(Q_OS_MACOS)
        runtime.executable = macos::resolve_wine_executable(runtime.selector);
#else
        runtime.executable = QFileInfo(runtime.selector).isAbsolute()
            ? QFileInfo(runtime.selector).absoluteFilePath()
            : QStandardPaths::findExecutable(runtime.selector.isEmpty()
                                                  ? QStringLiteral("wine")
                                                  : runtime.selector);
#endif
        runtime.source = runtime.selector.trimmed().isEmpty()
            ? QStringLiteral("PATH") : QStringLiteral("selected");
        runtime.usable = QFileInfo(runtime.executable).isExecutable();
        if (!runtime.usable)
            runtime.failure = QStringLiteral("No executable Wine binary was resolved.");

        d_->sensitive_values = sensitive_values;
        d_->snapshot_provider = std::move(snapshot_provider);
        d_->clock.start();
        d_->timeline_finished = false;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        d_->session_id = QStringLiteral("run-%1-%2")
            .arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
                 safe_label(core::game::to_string(version)));

        if (!d_->configuration.enabled)
        {
            return d_->configuration;
        }

#if defined(Q_OS_MACOS)
        const QString log_root = macos::default_log_root();
#else
        const QString log_root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
        const QString diagnostic_directory =
            QDir(log_root).filePath(QStringLiteral("diagnostics"));
        QDir().mkpath(diagnostic_directory);
        const QString base_session_id = d_->session_id;
        d_->run_directory = QDir(diagnostic_directory).filePath(d_->session_id);
        for (int suffix = 2; QFileInfo::exists(d_->run_directory); ++suffix)
        {
            d_->session_id = QStringLiteral("%1-%2").arg(base_session_id).arg(suffix);
            d_->run_directory = QDir(diagnostic_directory).filePath(d_->session_id);
        }
        QDir().mkpath(d_->run_directory);
        prune_old_diagnostics(diagnostic_directory);
        d_->diagnostic_path = QDir(d_->run_directory).filePath(QStringLiteral("wine.log"));
        d_->timeline_path = QDir(d_->run_directory).filePath(QStringLiteral("timeline.jsonl"));
        d_->summary_path = QDir(d_->run_directory).filePath(QStringLiteral("summary.txt"));

        d_->diagnostic_file.setFileName(d_->diagnostic_path);
        if (d_->diagnostic_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            QTextStream trace(&d_->diagnostic_file);
            trace << "Story of Alicia "
#if defined(Q_OS_MACOS)
                  << "macOS"
#else
                  << "Linux"
#endif
                  << " launcher diagnostic\n"
                  << "UTC: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << "\n"
                  << "Profile: " << d_->configuration.profile << "\n"
                  << "Diagnostic mode: enabled\n"
                  << "Effective WINEDEBUG: " << d_->configuration.wine_debug << "\n"
                  << "Graphics registry: " << registry_summary(prefix) << "\n"
                  << "Runtime source: " << runtime.source << "\n"
                  << "Runtime selector: " << runtime.selector << "\n"
                  << "Runtime executable: " << runtime.executable << "\n"
                  << "Prefix: " << prefix << "\n"
                  << "Game directory: " << game_directory << "\n";
            if (!runtime.failure.isEmpty())
            {
                trace << "Wine status: " << runtime.failure << "\n";
            }

            const QStringList dlls {
                QStringLiteral("d3d9.dll"),           QStringLiteral("d3dx9_31.dll"),
                QStringLiteral("d3dx9_42.dll"),       QStringLiteral("D3DCompiler_42.dll"),
                QStringLiteral("D3DCompiler_47.dll"), QStringLiteral("xinput1_3.dll"),
                QStringLiteral("msvcp100.dll"),       QStringLiteral("msvcr100.dll"),
                QStringLiteral("PhysXLoader.dll"),    QStringLiteral("PhysXCore.dll"),
                QStringLiteral("PhysXCooking.dll"),   QStringLiteral("PhysXDevice.dll"),
                QStringLiteral("cudart32_30_9.dll")};
            for (const QString& dll : dlls)
            {
                trace << "DLL candidate " << dll
                      << ": game=" << QFileInfo(QDir(game_directory).filePath(dll)).exists()
                      << " syswow64="
                      << QFileInfo(QDir(prefix).filePath(
                                       QStringLiteral("drive_c/windows/syswow64/") + dll))
                             .exists()
                      << " system32="
                      << QFileInfo(QDir(prefix).filePath(
                                       QStringLiteral("drive_c/windows/system32/") + dll))
                             .exists()
                      << "\n";
            }
            trace << "--- Wine output ---\n";
            trace.flush();
            SPDLOG_INFO("Alicia diagnostics: {}", d_->diagnostic_path.toStdString());
        }
        else
        {
            SPDLOG_WARN("could not create Alicia diagnostic trace: {}",
                        d_->diagnostic_file.errorString().toStdString());
        }

        append_event(QStringLiteral("session_started"),
                     QStringLiteral("profile=%1 game_version=%2 prefix=%3 "
                                    "executable=%4")
                         .arg(d_->configuration.profile, core::game::to_string(version), prefix,
                              executable_path));
        append_event(QStringLiteral("diagnostics_configured"),
                     QStringLiteral("enabled=%1 WINEDEBUG=%2 registry=%3")
                         .arg(d_->configuration.enabled)
                         .arg(d_->configuration.wine_debug, registry_summary(prefix)));

        const quint64 generation = ++d_->launch_generation;
        for (const int seconds : {10, 30, 60, 120})
        {
            QTimer::singleShot(seconds * 1000, this,
                               [this, generation, seconds]()
                               {
                                   if (generation != d_->launch_generation ||
                                       d_->timeline_finished || !d_->snapshot_provider)
                                   {
                                       return;
                                   }
                                   const ProcessSnapshot snapshot = d_->snapshot_provider();
                                   if (!snapshot.session_active)
                                       return;
                                   append_event(QStringLiteral("process_still_active"),
                                                QStringLiteral("milestone_seconds=%1 "
                                                               "game_confirmed=%2 "
                                                               "wrapper_running=%3")
                                                    .arg(seconds)
                                                    .arg(snapshot.game_confirmed)
                                                    .arg(snapshot.wrapper_running));
                               });
        }
        return d_->configuration;
    }

    void MacLaunchDiagnostics::observe_output(const QString& chunk)
    {
        const QString safe = redact_sensitive_text(chunk, d_->sensitive_values);
        if (!d_->seen_create_device &&
            safe.contains(QStringLiteral("CreateDevice"), Qt::CaseInsensitive))
        {
            d_->seen_create_device = true;
            append_event(QStringLiteral("d3d9_create_device_observed"));
        }
        if (!d_->seen_present && QRegularExpression(QStringLiteral(R"(\bPresent\s*\()"),
                                                    QRegularExpression::CaseInsensitiveOption)
                                     .match(safe)
                                     .hasMatch())
        {
            d_->seen_present = true;
            append_event(QStringLiteral("d3d9_present_observed"));
        }
        if (!d_->seen_draw &&
            QRegularExpression(QStringLiteral(R"(\bDraw(?:Indexed)?Primitive(?:UP)?\s*\()"),
                               QRegularExpression::CaseInsensitiveOption)
                .match(safe)
                .hasMatch())
        {
            d_->seen_draw = true;
            append_event(QStringLiteral("d3d9_draw_observed"));
        }
        if (!d_->seen_exception &&
            (safe.contains(QStringLiteral("Unhandled exception"), Qt::CaseInsensitive) ||
             safe.contains(QStringLiteral("Exception frame is not in stack limits"),
                           Qt::CaseInsensitive)))
        {
            d_->seen_exception = true;
            append_event(QStringLiteral("fatal_exception_observed"), safe.left(512).simplified());
        }

        if (d_->diagnostic_file.isOpen())
        {
            d_->diagnostic_file.write(safe.toUtf8());
            d_->diagnostic_file.flush();
        }
    }

    void MacLaunchDiagnostics::append_event(const QString& event, const QString& details)
    {
        if (d_->timeline_path.isEmpty() || d_->timeline_finished)
        {
            return;
        }

        QJsonObject object;
        object.insert(QStringLiteral("schema"), 1);
        object.insert(QStringLiteral("session"), d_->session_id);
        object.insert(QStringLiteral("utc"),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        object.insert(QStringLiteral("elapsed_ms"), d_->clock.isValid() ? d_->clock.elapsed() : 0);
        object.insert(QStringLiteral("event"), event);
        const QString safe_details = redact_sensitive_text(details, d_->sensitive_values);
        if (!safe_details.isEmpty())
        {
            object.insert(QStringLiteral("details"), safe_details);
        }

        QFile file(d_->timeline_path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
            file.write("\n");
            file.flush();
        }
        SPDLOG_INFO("Alicia timeline T+{}ms {} {}", d_->clock.isValid() ? d_->clock.elapsed() : 0,
                    event.toStdString(), safe_details.toStdString());
    }

    void MacLaunchDiagnostics::write_effective_command(const QString& program,
                                                       const QStringList& redacted_arguments,
                                                       const QString& working_directory)
    {
        if (!d_->diagnostic_file.isOpen())
            return;
        QTextStream trace(&d_->diagnostic_file);
        trace << "\n=== EFFECTIVE ALICIA COMMAND ===\n"
              << "program=" << program << "\n"
              << "arguments=" << redacted_arguments.join(QLatin1Char(' ')) << "\n"
              << "working_directory=" << working_directory << "\n";
        const RuntimeContext& runtime = d_->configuration.runtime;
        if (runtime.usable)
        {
            trace << "wine_source=" << runtime.source << "\n"
                  << "wine_executable=" << runtime.executable << "\n";
        }
        trace.flush();
    }

    void MacLaunchDiagnostics::arm_host_sample(const qint64 windows_pid, const qint64 host_pid)
    {
#if defined(Q_OS_MACOS)
        if (!d_->configuration.enabled || d_->run_directory.isEmpty() ||
            !d_->sample_generation)
            return;

        const auto generation_token = d_->sample_generation;
        const std::uint64_t generation = generation_token->fetch_add(1) + 1;
        const QString run_directory = d_->run_directory;

        if (d_->diagnostic_file.isOpen())
        {
            QTextStream trace(&d_->diagnostic_file);
            trace << "\n=== DIAGNOSTIC HOST SAMPLE "
                     "ARMED ===\n"
                  << "UTC: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << "\n"
                  << "Delay milliseconds: " << k_host_sample_delay_ms << "\n"
                  << "Generation: " << generation << "\n"
                  << "Alicia Windows PID: " << windows_pid << "\n"
                  << "=== END HOST SAMPLE ARM RECORD ===\n";
            trace.flush();
            d_->diagnostic_file.flush();
        }

        SPDLOG_INFO("armed Alicia diagnostic host sample "
                    "generation {} for {} ms",
                    generation, k_host_sample_delay_ms);

        std::thread(
            [generation_token, generation, run_directory, windows_pid, host_pid]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(k_host_sample_delay_ms));
                if (generation_token->load() != generation)
                    return;

                const QString sample_path =
                    QDir(run_directory).filePath(QStringLiteral("host-sample.txt"));
                bool sample_started = false;
                bool sample_finished = false;
                int sample_exit_code = -1;
                if (host_pid > 0 && QFileInfo(QStringLiteral("/usr/bin/sample")).isExecutable())
                {
                    QProcess sampler;
                    sampler.setProcessChannelMode(QProcess::MergedChannels);
                    sampler.start(QStringLiteral("/usr/bin/sample"),
                                  {QString::number(host_pid), QStringLiteral("8"),
                                   QStringLiteral("-file"), sample_path});
                    sample_started = sampler.waitForStarted(3000);
                    sample_finished = sample_started && sampler.waitForFinished(15000);
                    if (!sample_finished && sampler.state() != QProcess::NotRunning)
                    {
                        sampler.kill();
                        sampler.waitForFinished(2000);
                    }
                    if (sample_finished)
                        sample_exit_code = sampler.exitCode();
                }

                if (generation_token->load() != generation)
                    return;
                const QString status_path =
                    QDir(run_directory).filePath(QStringLiteral("host-sample-status.txt"));
                QFile file(status_path);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                {
                    return;
                }

                QTextStream trace(&file);
                trace << "=== DIAGNOSTIC HOST SAMPLE ===\n"
                      << "UTC: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
                      << "\n"
                      << "Reason: opt-in delayed live-process "
                         "sample\n"
                      << "Alicia Windows PID: " << windows_pid << "\n"
                      << "Alicia host PID at arm time: " << host_pid << "\n"
                      << "sample_started=" << sample_started
                      << " sample_finished=" << sample_finished
                      << " sample_exit=" << sample_exit_code << "\n"
                      << "sample_path=" << sample_path << "\n"
                      << "Writer: isolated worker; main diagnostic "
                         "stream was not modified\n"
                      << "=== END HOST SAMPLE ===\n";
                trace.flush();
                file.flush();
            })
            .detach();
#else
        Q_UNUSED(windows_pid);
        Q_UNUSED(host_pid);
#endif
    }

    void MacLaunchDiagnostics::finish(const QString& outcome, const int exit_code,
                                      const bool crashed, const QString& details)
    {
        if (d_->timeline_finished || d_->timeline_path.isEmpty())
        {
            return;
        }
        append_event(QStringLiteral("session_finished"),
                     QStringLiteral("outcome=%1 exit_code=%2 crashed=%3 "
                                    "duration_ms=%4 %5")
                         .arg(outcome)
                         .arg(exit_code)
                         .arg(crashed)
                         .arg(elapsed_ms())
                         .arg(details));

        QFile summary(d_->summary_path);
        if (summary.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            const QString alicia_log =
                QDir(d_->run_directory).filePath(QStringLiteral("alicia.log"));

            QFile hook_log(alicia_log);
            if (hook_log.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                const QString hook_text = QString::fromUtf8(hook_log.readAll());
                if (!d_->seen_create_device &&
                    hook_text.contains(QStringLiteral("CreateDevice"), Qt::CaseInsensitive))
                {
                    d_->seen_create_device = true;
                }
                if (!d_->seen_present &&
                    hook_text.contains(QStringLiteral("Present"), Qt::CaseInsensitive))
                {
                    d_->seen_present = true;
                }
            }

            QString stage = QStringLiteral("before_process");
            if (d_->seen_draw)
                stage = QStringLiteral("rendering_started");
            else if (d_->seen_present)
                stage = QStringLiteral("after_present_before_draw");
            else if (d_->seen_create_device)
                stage = QStringLiteral("after_device_before_present");

            QTextStream stream(&summary);
            QStringList findings = known_log_findings(d_->diagnostic_path);
            findings.append(known_log_findings(alicia_log));

            findings.removeDuplicates();
            stream << "Story of Alicia "
#if defined(Q_OS_MACOS)
                   << "macOS"
#else
                   << "Linux"
#endif
                   << " launch summary\n"
                   << "session=" << d_->session_id << "\n"
                   << "outcome=" << outcome << "\n"
                   << "exit_code=" << exit_code << "\n"
                   << "crashed=" << crashed << "\n"
                   << "duration_ms=" << elapsed_ms() << "\n"
                   << "stage=" << stage << "\n"
                   << "create_device_seen=" << d_->seen_create_device << "\n"
                   << "present_seen=" << d_->seen_present << "\n"
                   << "draw_seen=" << d_->seen_draw << "\n"
                   << "exception_seen=" << d_->seen_exception << "\n"
                   << "details="
                   << redact_sensitive_text(details, d_->sensitive_values) << "\n"
                   << "findings="
                   << (findings.isEmpty() ? QStringLiteral("none")
                                          : findings.join(QLatin1Char(','))) << "\n"
                   << "wine_log=wine.log\n"
                   << "timeline=timeline.jsonl\n";
            if (QFileInfo(alicia_log).isFile())
                stream << "alicia_log=alicia.log\n";
            if (QFileInfo(QDir(d_->run_directory)
                              .filePath(QStringLiteral("host-sample.txt"))).isFile())
            {
                stream << "host_sample=host-sample.txt\n";
            }
            stream.flush();
        }
        d_->timeline_finished = true;
        SPDLOG_INFO("Alicia launch timeline complete: {} outcome={}",
                    d_->timeline_path.toStdString(), outcome.toStdString());
    }

    void MacLaunchDiagnostics::append_footer_and_analyze(const QString& footer,
                                                         const QString& executable_path)
    {
        if (d_->diagnostic_file.isOpen())
        {
            QTextStream trace(&d_->diagnostic_file);
            trace << footer;
            if (!footer.endsWith(QLatin1Char('\n')))
                trace << "\n";
            trace.flush();
            d_->diagnostic_file.close();
            append_crash_analysis(d_->diagnostic_path, executable_path);
        }
    }

    void MacLaunchDiagnostics::close_log()
    {
        if (d_->diagnostic_file.isOpen())
            d_->diagnostic_file.close();
    }

    void MacLaunchDiagnostics::cancel_host_sample()
    {
#if defined(Q_OS_MACOS)
        if (d_->sample_generation)
            d_->sample_generation->fetch_add(1);
#endif
    }

    bool MacLaunchDiagnostics::trace_has_fatal_failure() const
    {
        if (d_->diagnostic_path.isEmpty())
            return false;
        QFile input(d_->diagnostic_path);
        if (!input.open(QIODevice::ReadOnly))
            return false;
        constexpr qint64 tail_bytes = 512 * 1024;
        if (input.size() > tail_bytes)
            input.seek(input.size() - tail_bytes);
        const QByteArray tail = input.readAll();
        return tail.contains("Unhandled exception") ||
               tail.contains("Exception frame is not in stack limits "
                             "=> unable to dispatch exception") ||
               tail.contains("wine: Unhandled") || tail.contains("stack overflow") ||
               tail.contains("page fault on read access");
    }

    qint64 MacLaunchDiagnostics::elapsed_ms() const
    {
        return d_->clock.isValid() ? d_->clock.elapsed() : 0;
    }

    bool MacLaunchDiagnostics::saw_present() const
    {
        return d_->seen_present;
    }

    bool MacLaunchDiagnostics::saw_draw() const
    {
        return d_->seen_draw;
    }

    QString MacLaunchDiagnostics::diagnostic_path() const
    {
        return d_->diagnostic_path;
    }

    QString MacLaunchDiagnostics::timeline_path() const
    {
        return d_->timeline_path;
    }

    QString MacLaunchDiagnostics::run_directory() const
    {
        return d_->run_directory;
    }

    MacLaunchDiagnostics::Configuration MacLaunchDiagnostics::configuration() const
    {
        return d_->configuration;
    }

    bool MacLaunchDiagnostics::profile_uses_virtual_desktop(const QString& profile)
    {
        return profile == QStringLiteral("safe-display") ||
               profile == QStringLiteral("low-graphics") || profile == QStringLiteral("gl-behind");
    }

    bool MacLaunchDiagnostics::profile_disables_audio(const QString& profile)
    {
        Q_UNUSED(profile);
        return qEnvironmentVariable("SOA_AUDIO_NULL_BACKEND").trimmed()
            == QStringLiteral("1");
    }

    QString MacLaunchDiagnostics::opengl_surface_mode(const QString& profile)
    {
        return profile == QStringLiteral("gl-behind") ? QStringLiteral("behind") : QString();
    }

    bool MacLaunchDiagnostics::retina_enabled(const QString& profile)
    {
        Q_UNUSED(profile);
        return false;
    }

    namespace
    {
        QString wine_debug_for_mode(const QString& mode)
        {
            if (mode == QLatin1String("off"))
            {
                return QStringLiteral("-all");
            }
            if (mode == QLatin1String("normal"))
            {
                return QStringLiteral(
                    "+timestamp,+pid,+tid,err+all,fixme+all,+winediag,+loaddll");
            }
            if (mode == QLatin1String("verbose"))
            {
                return QStringLiteral(
                    "+timestamp,+pid,+tid,err+all,fixme+all,+winediag,+loaddll,"
                    "+module,+process,+thread,+seh,+d3d,+d3d9,+dsound,+mmdevapi,+winmm");
            }
            if (mode == QLatin1String("audio"))
            {
                return QStringLiteral(
                    "+timestamp,+pid,+tid,err+all,fixme+all,+winediag,+loaddll,"
                    "+virtual,+dsound,+mmdevapi,+winmm,+coreaudio");
            }
            if (mode == QLatin1String("forensic"))
            {
                return QStringLiteral(
                    "+timestamp,+pid,+tid,err+all,fixme+all,+winediag,+loaddll,"
                    "+module,+process,+thread,+seh,+d3d,+d3d9,+dsound,+mmdevapi,+winmm,"
                    "+reg,+file,+win,+msg,+sync,+heap,+virtual,+imm,+ntdll");
            }
            if (mode == QLatin1String("relay"))
            {
                return QStringLiteral(
                    "+timestamp,+pid,+tid,err+all,fixme+all,+relay,+snoop,+seh,"
                    "+loaddll,+module");
            }
            return QString();
        }
    }

    QString MacLaunchDiagnostics::wine_debug_value(const bool diagnostics_enabled)
    {
        const QString explicit_debug = qEnvironmentVariable("WINEDEBUG").trimmed();
        if (!explicit_debug.isEmpty())
        {
            SPDLOG_INFO("Using WINEDEBUG from the environment: {}",
                        explicit_debug.toStdString());
            return explicit_debug;
        }

        const QString mode = qEnvironmentVariable("SOA_LOG_MODE").trimmed().toLower();
        if (!mode.isEmpty())
        {
            const QString channels = wine_debug_for_mode(mode);
            if (!channels.isEmpty())
            {
                SPDLOG_INFO("Log mode '{}' -> WINEDEBUG={}",
                            mode.toStdString(), channels.toStdString());
                return channels;
            }
            SPDLOG_WARN("Unknown SOA_LOG_MODE '{}'; using the default. "
                        "Valid modes: off normal verbose forensic relay",
                        mode.toStdString());
        }

        return diagnostics_enabled
            ? QStringLiteral("+timestamp,+pid,+tid,+winediag,+seh,+loaddll,+module")
            : QStringLiteral("-all");
    }

    QString MacLaunchDiagnostics::registry_summary(const QString& prefix)
    {
        QFile registry(QDir(prefix).filePath(QStringLiteral("user.reg")));
        if (!registry.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return QStringLiteral("user.reg unavailable");
        }

        QStringList selected;
        QString section;
        const QString text = QString::fromUtf8(registry.readAll());
        for (const QString& raw : text.split(QLatin1Char('\n')))
        {
            const QString line = raw.trimmed();
            if (line.startsWith(QLatin1Char('[')))
                section = line;
            const bool relevant =
                line.contains(QStringLiteral("\"renderer\""), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("\"OffscreenRenderingMode\""), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("\"VideoMemorySize\""), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("\"OpenGLSurfaceMode\""), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("\"RetinaMode\""), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("\"CaptureDisplaysForFullscreen\""),
                              Qt::CaseInsensitive);
            if (relevant)
            {
                selected.append(section + QStringLiteral(" ") + line);
            }
        }
        return selected.isEmpty() ? QStringLiteral("no explicit graphics overrides")
                                  : selected.join(QStringLiteral(" | "));
    }
}
