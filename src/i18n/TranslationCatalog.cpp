#include "i18n/LanguageManager.hpp"
#include "TranslationPrivate.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <array>

#include <utility>

namespace
{
    bool translated_catalog_active {};

    constexpr std::array<const char*, 11> k_translation_contexts {
        "Launcher & Navigation",
        "Home & Account",
        "Setup & Rules",
        "Game Installation & Repair",
        "Game Launch & Session",
        "Runtime & Compatibility",
        "Network & Transfers",
        "Launcher Updates",
        "Settings",
        "About & Credits",
        "Logs & Diagnostics"
    };

    QString catalogue_translation(const QString& source)
    {
        const QByteArray utf8 = source.toUtf8();
        for (const char* context : k_translation_contexts)
        {
            const QString translated = QCoreApplication::translate(context, utf8.constData());
            if (translated != source)
                return translated;
        }
        return source;
    }
    struct DynamicTranslationTemplate
    {
        QString source;
        QRegularExpression expression;
        QVector<int> placeholders;
    };

    QRegularExpression expression_for_template(const QString& source, QVector<int>& placeholders)
    {
        QString pattern = QStringLiteral("\\A");
        for (qsizetype index = 0; index < source.size(); ++index)
        {
            if (source[index] == QLatin1Char('%') && index + 1 < source.size()
                && source[index + 1].isDigit())
            {
                const int placeholder = source[index + 1].digitValue();
                if (placeholder >= 1 && placeholder <= 9)
                {
                    placeholders.push_back(placeholder);
                    pattern += QStringLiteral("(.*?)");
                    ++index;
                    continue;
                }
            }
            pattern += QRegularExpression::escape(QString(1, source[index]));
        }
        pattern += QStringLiteral("\\z");
        return QRegularExpression(pattern, QRegularExpression::DotMatchesEverythingOption);
    }

    const QVector<DynamicTranslationTemplate>& dynamic_translation_templates()
    {
        static const QStringList templates {
            QStringLiteral("\n\nDiagnostic run: %1"),
            QStringLiteral("%1 (%2, exit %3)."),
            QStringLiteral("%1 (default)"),
            QStringLiteral("%1 and %2"),
            QStringLiteral("%1 are managed by the launcher and cannot be overridden."),
            QStringLiteral("%1 B"),
            QStringLiteral("%1 could not be started. See launcher.log for details."),
            QStringLiteral("%1 FILES (%2/%3)"),
            QStringLiteral("%1 GB"),
            QStringLiteral("%1 is the recommended setup for this computer. Alicia will use compatibility graphics by default.\n\nDXVK stays optional and can be enabled later in Settings. Nothing will be installed automatically."),
            QStringLiteral("%1 is the recommended Wine setup for this Mac. Alicia will use compatibility graphics and a 64-bit Wine prefix. Nothing will be installed automatically."),
            QStringLiteral("%1 KB"),
            QStringLiteral("%1 MB"),
            QStringLiteral("%1 MB downloaded"),
            QStringLiteral("%1 MB of %2 MB"),
            QStringLiteral("%1 NEEDED"),
            QStringLiteral("%1 READY"),
            QStringLiteral("%1 Verify and repair before launching again."),
            QStringLiteral("%1 was not found in the selected game folder."),
            QStringLiteral("Alicia.exe was not observed within %1 seconds. The launcher ended monitoring instead of waiting forever."),
            QStringLiteral("The compatibility process exited and Alicia.exe was not observed within %1 seconds. The launcher stopped monitoring instead of leaving the Wine host in the background. Check launcher.log for the first missing library or graphics error."),
            QStringLiteral("The 1024x720 windowed D3D9 compatibility hook could not be prepared: %1"),
            QStringLiteral("Wine could not start Alicia.exe. Check the labeled diagnostic run at %1."),
            QStringLiteral("Could not replace staged diagnostic component: %1"),
            QStringLiteral("Could not stage diagnostic component: %1"),
            QStringLiteral("Wine failed to start: %1"),
            QStringLiteral("The Wine version request exited with code %1."),
            QStringLiteral("The Wine version request exited with code %1: %2"),
            QStringLiteral("Wine and Winetricks are required to prepare Alicia's Windows components.\n\nMissing: %1. %2, then restart the launcher."),
            QStringLiteral("resolved to %1; expected %2 (%3 ms)"),
            QStringLiteral("could not start DNS lookup for %1 (%2)"),
            QStringLiteral("Launcher update workspace is not a directory: %1"),
            QStringLiteral("The interrupted update journal was corrupt and was moved to %1. Run repair again."),
            QStringLiteral("Manifest entries resolve to the same destination: %1"),
            QStringLiteral("Hash mismatch for %1"),
            QStringLiteral("Updated %1 file(s) and removed %2 obsolete file(s)."),
            QStringLiteral("Remote returned HTTP %1 while retrieving game version"),
            QStringLiteral("Remote returned HTTP %1 while retrieving game manifest"),
            QStringLiteral("Failed to retrieve game version: %1"),
            QStringLiteral("Failed to retrieve game manifest: %1"),
            QStringLiteral("Invalid manifest JSON: %1"),
            QStringLiteral("HTTP %1 while downloading %2"),
            QStringLiteral("Could not open temporary download file: %1"),
            QStringLiteral("Could not write temporary download file: %1"),
            QStringLiteral("Network download failed: %1"),
            QStringLiteral("Downloaded size mismatch: expected %1 bytes, received %2"),
            QStringLiteral("Download failed: %1"),
            QStringLiteral("Manifest contains an absolute path: %1"),
            QStringLiteral("Manifest contains a drive-qualified path: %1"),
            QStringLiteral("Manifest contains an unsafe path component: %1"),
            QStringLiteral("Manifest contains an invalid or oversized path component: %1"),
            QStringLiteral("Manifest contains a Windows-incompatible path component: %1"),
            QStringLiteral("Manifest contains a reserved Windows path component: %1"),
            QStringLiteral("Manifest path conflicts with launcher update metadata: %1"),
            QStringLiteral("Manifest path is too long: %1"),
            QStringLiteral("Manifest contains a negative file size: %1"),
            QStringLiteral("Manifest file is too large: %1"),
            QStringLiteral("Manifest contains duplicate or case-colliding paths: %1"),
            QStringLiteral("Manifest contains a file-versus-directory path collision: %1"),
            QStringLiteral("Refusing manifest destination through a symbolic link: %1"),
            QStringLiteral("Manifest destination escapes the installation directory: %1"),
            QStringLiteral("%1, and %2"),
            QStringLiteral("%1/s"),
            QStringLiteral("%1h"),
            QStringLiteral("%1h %2m"),
            QStringLiteral("%1m"),
            QStringLiteral("%1m %2s"),
            QStringLiteral("%1s"),
            QStringLiteral("By clicking the “Proceed with Discord” button, you acknowledge that your Discord ID will be stored on our database servers indefinitely for identification and service purposes. This data can be removed upon request by emailing %1."),
            QStringLiteral("CHECKING FILES (%1/%2)"),
            QStringLiteral("Click %1 when prompted."),
            QStringLiteral("Compatibility profile failed: %1"),
            QStringLiteral("Could not start %1"),
            QStringLiteral("Downloading version %1..."),
            QStringLiteral("DXVK could not be verified and was turned off. %1 The prefix remains ready with the built-in Direct3D backend."),
            QStringLiteral("Failed to load rules: %1"),
            QStringLiteral("from %1"),
            QStringLiteral("HTTP %1 (%2 ms)"),
            QStringLiteral("I have read and will obey the %1."),
            QStringLiteral("Installed: %1 · Selected: %2"),
            QStringLiteral("Invalid launch arguments: %1"),
            QStringLiteral("PLEASE READ (%1)"),
            QStringLiteral("Proton needs Proton and UMU. Winetricks is also required for Alicia's Windows components.\n\nMissing: %1. %2, then restart the launcher."),
            QStringLiteral("Pure Wine needs both Wine and Winetricks.\n\nMissing: %1. %2, then restart the launcher."),
            QStringLiteral("reachable (%1 ms)"),
            QStringLiteral("Select runtime: %1"),
            QStringLiteral("Signed in as %1"),
            QStringLiteral("Step %1 of 3"),
            QStringLiteral("The game exited unexpectedly. Diagnostic log: %1"),
            QStringLiteral("The last game transfer failed. Retry will verify existing files and continue: %1"),
            QStringLiteral("The launcher could not initialize config.json.\n\nPath: %1\nReason: %2"),
            QStringLiteral("The launcher could not save config.json.\n\nPath: %1\nReason: %2\n\nYour on-screen change is active only for this session."),
            QStringLiteral("The launcher detected %1 protected file changes."),
            QStringLiteral("The launcher detected a protected file change: %1"),
            QStringLiteral("The launcher update network request failed: %1"),
            QStringLiteral("The launcher update server returned HTTP %1."),
            QStringLiteral("The launcher will verify every %1 file against the current CDN manifest."),
            QStringLiteral("The macOS game package is incomplete. Repair the game installation; these required local components are missing:\n%1"),
            QStringLiteral("The rules server returned HTTP %1."),
            QStringLiteral("Time remaining: %1"),
            QStringLiteral("VERIFYING FILES (%1/%2)"),
            QStringLiteral("Version %1 is available for the launcher."),
            QStringLiteral("Version %1 is available. You must update the launcher before continuing."),
            QStringLiteral("VERSION %1 · QT %2"),
            QStringLiteral("Welcome to %1. To participate in the playtest, you have to first %2."),
            QStringLiteral("Wine could not start the game launch process: %1"),
            QStringLiteral("Winetricks finished, but DXVK could not be verified. %1 Check launcher.log for the installer output."),
            QStringLiteral("winetricks: %1 · Rosetta: %2"),
            QStringLiteral(" after a crash"),
            QStringLiteral(" The compatibility process exited with code %1%2."),
            QStringLiteral(" The launcher stayed in Launching instead of falsely reporting Running; check the launcher log for the first Wine or DLL error."),
            QStringLiteral("Add Wine"),
            QStringLiteral("Add Wine…"),
            QStringLiteral("alice.cfg was found, but none of the verified compatibility keys were present."),
            QStringLiteral("Already up to date."),
            QStringLiteral("An Intel Wine installation was found, but Rosetta is unavailable. Request Rosetta, complete the macOS prompt, then rescan."),
            QStringLiteral("Applied the conservative macOS graphics profile and saved alice.cfg.soa-macos-backup."),
            QStringLiteral("Audio isolation (diagnostic)"),
            QStringLiteral("Cancelled"),
            QStringLiteral("Choose the Wine installation used to run the game."),
            QStringLiteral("Could not create the Alicia log-hook staging folder."),
            QStringLiteral("Could not map the Alicia diagnostic log path to Wine."),
            QStringLiteral("could not start request"),
            QStringLiteral("DNS lookup returned no usable address"),
            QStringLiteral("DNS lookup timed out"),
            QStringLiteral("Download redirected to an untrusted origin"),
            QStringLiteral("Download returned a non-HTTP response"),
            QStringLiteral("Game Not Found"),
            QStringLiteral("Invalid CDN base URL"),
            QStringLiteral("Invalid Courier CDN origin"),
            QStringLiteral("Invalid DNS hostname"),
            QStringLiteral("Invalid Game Path"),
            QStringLiteral("Invalid or insecure URL"),
            QStringLiteral("Invalid remote manifest URL"),
            QStringLiteral("Invalid remote version URL"),
            QStringLiteral("Launcher update workspace escapes the game installation directory"),
            QStringLiteral("macOS uses Wine's built-in Direct3D 9 backend in this version. DXVK is intentionally unavailable until a tested Metal/Vulkan path exists."),
            QStringLiteral("Manifest contains an empty or invalid path"),
            QStringLiteral("Manifest contains an empty path"),
            QStringLiteral("Manifest contains too many files"),
            QStringLiteral("Manifest has no files"),
            QStringLiteral("Manifest total download size is too large"),
            QStringLiteral("Manifest total size is too large"),
            QStringLiteral("No executable Wine entry point was found in the selected app or folder."),
            QStringLiteral("No executable Wine entry point was found in this selection."),
            QStringLiteral("No usable Wine installation was found. Install Wine or add a Wine app, executable, or folder."),
            QStringLiteral("Not enough free disk space to stage, back up, and finish this update"),
            QStringLiteral("Prefix setup finished, but the Wine prefix structure is incomplete or incompatible."),
            QStringLiteral("Prefix setup was cancelled."),
            QStringLiteral("Refusing to use the filesystem root as a game installation directory"),
            QStringLiteral("Remote game manifest exceeds the allowed size"),
            QStringLiteral("Remote game version exceeds the allowed size"),
            QStringLiteral("Remote redirected game manifest to an untrusted origin"),
            QStringLiteral("Remote redirected game version to an untrusted origin"),
            QStringLiteral("Remote returned a non-HTTP response for game manifest"),
            QStringLiteral("Remote returned a non-HTTP response for game version"),
            QStringLiteral("Remote returned an invalid game version"),
            QStringLiteral("Runtime Missing"),
            QStringLiteral("Runtime Not Available"),
            QStringLiteral("Select a Wine application, executable, or installation folder."),
            QStringLiteral("Select Wine"),
            QStringLiteral("Server rejected the saved partial file; retrying from the beginning"),
            QStringLiteral("Server returned an invalid resume range; retrying the file from the beginning"),
            QStringLiteral("Sign In Required"),
            QStringLiteral("Some Wine applications are built for Intel Macs. macOS may now show its Rosetta installation prompt. Continue?"),
            QStringLiteral("System Wine"),
            QStringLiteral("The Alicia process safety check could not be started."),
            QStringLiteral("The compatibility prefix drive_c directory does not exist."),
            QStringLiteral("The compatibility process started, but Alicia.exe was never observed in the process list."),
            QStringLiteral("The compatibility process started, but the launcher could not verify whether Alicia.exe started."),
            QStringLiteral("The configured game folder is outside the active compatibility prefix."),
            QStringLiteral("The game executable could not be mapped to a safe C: path."),
            QStringLiteral("The game executable does not exist or cannot be resolved."),
            QStringLiteral("The game executable resolves outside the compatibility C: drive."),
            QStringLiteral("The game may still be running. Close it normally and restart the launcher before starting another session."),
            QStringLiteral("The labeled diagnostic folder is not writable, so alicia.log cannot be created."),
            QStringLiteral("The launcher could not apply the conservative alice.cfg profile."),
            QStringLiteral("The launcher could not create an alice.cfg backup."),
            QStringLiteral("The launcher could not prepare the Alicia process monitor for the selected runtime."),
            QStringLiteral("The launcher could not start stale runtime-process cleanup."),
            QStringLiteral("The launcher could not stop stale runtime processes in the game prefix."),
            QStringLiteral("The launcher could not verify whether Alicia.exe is already running. This safety check prevents terminating an active game session."),
            QStringLiteral("The launcher could not wait for stale runtime processes to stop."),
            QStringLiteral("The packaged Alicia injector and compatibility hook were not found."),
            QStringLiteral("The response exceeds the allowed size"),
            QStringLiteral("The runtime did not finish shutting down stale prefix processes."),
            QStringLiteral("The selected runtime does not expose its process-control helper, so stale prefix processes could not be cleaned before launch."),
            QStringLiteral("The selected runtime is unavailable."),
            QStringLiteral("The selected Wine installation could not be used."),
            QStringLiteral("The selected Wine installation is Intel-only. Request Rosetta in the Wine chooser, complete the macOS prompt, then retry."),
            QStringLiteral("The server redirected to an untrusted URL"),
            QStringLiteral("The server returned a non-HTTP response"),
            QStringLiteral("The server returned an insecure URL"),
            QStringLiteral("The server returned an invalid redirect"),
            QStringLiteral("The server returned no response"),
            QStringLiteral("The Wine host command could not be prepared."),
            QStringLiteral("This Wine installation is Intel-only and Rosetta is not installed."),
            QStringLiteral("Update backup size is too large"),
            QStringLiteral("Update disk-space requirement is too large"),
            QStringLiteral("Update recovery journal has invalid staging metadata"),
            QStringLiteral("Update recovery journal is invalid"),
            QStringLiteral("Update recovery journal is unexpectedly large"),
            QStringLiteral("Validating Wine..."),
            QStringLiteral("Wine App or Executable"),
            QStringLiteral("Wine could not complete its version check. It may be quarantined, incomplete, or missing a dependency."),
            QStringLiteral("Wine could not start Alicia.exe. Check launcher.log for details."),
            QStringLiteral("Wine did not answer its version request before the timeout."),
            QStringLiteral("Wine Folder"),
            QStringLiteral("Wine Not Usable"),
            QStringLiteral("You can still choose a different Wine installation manually."),
            QStringLiteral("Your login session is missing or expired. Sign in again."),
        };
        static const QVector<DynamicTranslationTemplate> compiled = []()
        {
            QVector<DynamicTranslationTemplate> result;
            result.reserve(templates.size());
            for (const QString& source : templates)
            {
                DynamicTranslationTemplate entry;
                entry.source = source;
                entry.expression = expression_for_template(source, entry.placeholders);
                if (!entry.placeholders.isEmpty() && entry.expression.isValid())
                    result.push_back(std::move(entry));
            }
            return result;
        }();
        return compiled;
    }

    QString translated_impl(const QString& source)
    {
        if (source.isEmpty())
            return {};

        if (!translated_catalog_active)
            return source;

        const QString exact = catalogue_translation(source);
        if (exact != source)
            return exact;

        for (const DynamicTranslationTemplate& entry : dynamic_translation_templates())
        {
            const QRegularExpressionMatch match = entry.expression.match(source);
            if (!match.hasMatch())
                continue;

            QString result = catalogue_translation(entry.source);
            QHash<int, QString> arguments;
            for (qsizetype index = 0; index < entry.placeholders.size(); ++index)
            {
                const int placeholder = entry.placeholders[index];
                if (!arguments.contains(placeholder))
                    arguments.insert(placeholder, match.captured(static_cast<int>(index + 1)));
            }
            for (int placeholder = 9; placeholder >= 1; --placeholder)
            {
                if (arguments.contains(placeholder))
                    result.replace(QStringLiteral("%") + QString::number(placeholder), arguments.value(placeholder));
            }
            return result;
        }

        return source;
    }
}

namespace soa::i18n::detail
{
    void set_catalog_active(const bool active)
    {
        translated_catalog_active = active;
    }

    QString translated(const QString& source)
    {
        return translated_impl(source);
    }
}

namespace soa::i18n
{
    QString translate(const char* source)
    {
        return detail::translated(QString::fromUtf8(source));
    }

    QString translate(const QString& source)
    {
        return detail::translated(source);
    }

    QString translate(const char* source, const QString& first_argument)
    {
        return translate(source).arg(first_argument);
    }
}
