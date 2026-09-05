#include "i18n/LanguageManager.hpp"
#include "TranslationPrivate.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <utility>

namespace
{
    bool translated_catalog_active {};
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
            QStringLiteral("  SIGNED IN AS %1"),
            QStringLiteral(" The compatibility process exited with code %1%2."),
            QStringLiteral("\"%1\"=dword:00000001\n"),
            QStringLiteral("\"MacCompatibilityProfile\"=\"%1\"\n"),
            QStringLiteral("\"OpenGLSurfaceMode\"=\"%1\"\n\n"),
            QStringLiteral("%1 (%2 ms)"),
            QStringLiteral("%1 (%2, exit %3)."),
            QStringLiteral("%1 could not be started. See launcher.log for details."),
            QStringLiteral("%1 FILES (%2/%3)"),
            QStringLiteral("%1 is the recommended setup for this computer. Alicia will use compatibility graphics by default.\n\nDXVK stays optional and can be enabled later in Settings. Nothing will be installed automatically."),
            QStringLiteral("%1 ms"),
            QStringLiteral("%1 NEEDED"),
            QStringLiteral("%1 is the recommended Wine setup for this Mac. Alicia will use compatibility graphics and a 64-bit Wine prefix. Nothing will be installed automatically."),
            QStringLiteral("%1 READY"),
            QStringLiteral("%1 Verify and repair before launching again."),
            QStringLiteral("%1 was not found in the selected game folder."),
            QStringLiteral("%1 · Wine %2"),
            QStringLiteral("%1/%2/manifest.json"),
            QStringLiteral("%1x%2"),
            QStringLiteral("--start-address=0x%1"),
            QStringLiteral("--stop-address=0x%1"),
            QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict><key>Label</key><string>com.storyofalicia.launcher</string><key>ProgramArguments</key><array>%1</array><key>RunAtLoad</key><true/></dict></plist>"),
            QStringLiteral("<a href=\"https://storyofalicia.com\">%1</a><br><a href=\"https://github.com/Story-Of-Alicia\">%2</a><br><a href=\"mailto:dev@storyofalicia.com\">%3</a><br><br>%4"),
            QStringLiteral("<b>%1</b><br>%2 %3"),
            QStringLiteral("<string>%1</string>"),
            QStringLiteral("<td width=\"50%\" style=\"padding:1px 10px 2px 0;\"><b>%1:</b> <span style=\"color:%2\">%3</span></td>"),
            QStringLiteral("<tr>%1%2</tr>"),
            QStringLiteral("[HKEY_CURRENT_USER\\%1]\n"),
            QStringLiteral("Alicia.exe was not observed within %1 seconds. The launcher ended monitoring instead of waiting forever."),
            QStringLiteral("The compatibility process exited and Alicia.exe was not observed within %1 seconds. The launcher stopped monitoring instead of leaving the Wine host in the background. Check launcher.log for the first missing library or graphics error."),
            QStringLiteral("Built with Qt %1"),
            QStringLiteral("CHECKING FILES (%1/%2)"),
            QStringLiteral("Compatibility profile failed: %1"),
            QStringLiteral("Could not start %1"),
            QStringLiteral("enabled=%1 WINEDEBUG=%2 registry=%3"),
            QStringLiteral("Discord presence proxy request failed: %1"),
            QStringLiteral("Discord RPC error %1: %2"),
            QStringLiteral("discord-ipc-%1"),
            QStringLiteral("DXVK could not be verified and was turned off. %1 The prefix remains ready with the built-in Direct3D backend."),
            QStringLiteral("exit_code=%1 crashed=%2 game_confirmed=%3"),
            QStringLiteral("grace_ms=%1 attempts=%2 wrapper_exit=%3 crashed=%4"),
            QStringLiteral("winetricks: %1 · Rosetta: %2"),
            QStringLiteral("gui/%1"),
            QStringLiteral("host_pid=%1"),
            QStringLiteral("host_pid=%1 windows_pid=%2"),
            QStringLiteral("HTTP %1 (%2 ms)"),
            QStringLiteral("initial_delay_ms=%1 interval_ms=%2"),
            QStringLiteral("Invalid launch arguments: %1"),
            QStringLiteral("milestone_seconds=%1 game_confirmed=%2 wrapper_running=%3"),
            QStringLiteral("outcome=%1 exit_code=%2 crashed=%3 duration_ms=%4 %5"),
            QStringLiteral("profile=%1 game_version=%2 prefix=%3 executable=%4"),
            QStringLiteral("Proton needs Proton and UMU. Winetricks is also required for Alicia's Windows components.\n\nMissing: %1. %2, then restart the launcher."),
            QStringLiteral("Pure Wine needs both Wine and Winetricks.\n\nMissing: %1. %2, then restart the launcher."),
            QStringLiteral("reachable (%1 ms)"),
            QStringLiteral("reg.exe delete \"%1\" /v renderer /f >nul 2>&1 & exit /b 0"),
            QStringLiteral("Signed in as %1"),
            QStringLiteral("Step %1 of 3"),
            QStringLiteral("The game exited unexpectedly. Diagnostic log: %1"),
            QStringLiteral("The last game transfer failed. Retry will verify existing files and continue: %1"),
            QStringLiteral("The launcher could not initialize config.json.\n\nPath: %1\nReason: %2"),
            QStringLiteral("The launcher could not save config.json.\n\nPath: %1\nReason: %2\n\nYour on-screen change is active only for this session."),
            QStringLiteral("The launcher detected %1 protected file changes."),
            QStringLiteral("The launcher detected a protected file change: %1"),
            QStringLiteral("The launcher will verify every %1 file against the current CDN manifest."),
            QStringLiteral("The macOS game package is incomplete. Repair the game installation; these required local components are missing:\n%1"),
            QStringLiteral("Wine could not start the game launch process: %1"),
            QStringLiteral("Wine failed to start: %1"),
            QStringLiteral("The Wine version request exited with code %1."),
            QStringLiteral("The Wine version request exited with code %1: %2"),
            QStringLiteral("Time remaining: %1"),
            QStringLiteral("timeout_ms=%1 attempts=%2 wrapper_finished=%3 wrapper_exit=%4"),
            QStringLiteral("VERIFYING FILES (%1/%2)"),
            QStringLiteral("Welcome to %1. To participate in the playtest, you have to first %2."),
            QStringLiteral("windows_pid=%1 host_pid=%2 wrapper_exit=%3 fatal_trace=%4 present_seen=%5 draw_seen=%6"),
            QStringLiteral("windows_pid=%1 restored=%2"),
            QStringLiteral("Wine %1"),
            QStringLiteral("Wine could not start the game launch process: %1"),
            QStringLiteral("Winetricks finished, but DXVK could not be verified. %1 Check launcher.log for the installer output."),
            QStringLiteral("Updated %1 file(s) and removed %2 obsolete file(s)."),
            QStringLiteral("Remote returned HTTP %1 while retrieving %2"),
            QStringLiteral("Failed to retrieve %1: %2"),
            QStringLiteral("Invalid manifest JSON: %1"),
            QStringLiteral("HTTP %1 while downloading %2"),
            QStringLiteral("Could not open temporary download file: %1"),
            QStringLiteral("Could not write temporary download file: %1"),
            QStringLiteral("Network download failed: %1"),
            QStringLiteral("Download failed: %1"),
            QStringLiteral("Failed to retrieve %1"),
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
                if (entry.expression.isValid())
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

        const QByteArray utf8 = source.toUtf8();
        const QString exact = QCoreApplication::translate("Launcher", utf8.constData());
        if (exact != source)
            return exact;

        for (const DynamicTranslationTemplate& entry : dynamic_translation_templates())
        {
            const QRegularExpressionMatch match = entry.expression.match(source);
            if (!match.hasMatch())
                continue;

            const QByteArray template_utf8 = entry.source.toUtf8();
            QString result = QCoreApplication::translate("Launcher", template_utf8.constData());
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

namespace util::i18n::detail
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

namespace util::i18n
{
    QString translate(const char* source)
    {
        return QCoreApplication::translate("Launcher", source);
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
