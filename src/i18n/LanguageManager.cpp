#include "i18n/LanguageManager.hpp"

#include <algorithm>
#include <complex>

#include "TranslationPrivate.hpp"
#include "config/Config.hpp"

#include <spdlog/spdlog.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include <QTranslator>

namespace soa::i18n
{
    LanguageManager& LanguageManager::instance()
    {
        static LanguageManager manager;
        return manager;
    }

    LanguageManager::LanguageManager(QObject* parent)
        : QObject(parent)
    {
        // English has no .qm file so we add it from the start
        available_languages ={{QStringLiteral("en"), QStringLiteral("English")}};
        translator = new QTranslator(this);

        const QDir dir { QStringLiteral(":/i18n") };
        const auto prefix = QStringLiteral("soa_launcher_");
        const QStringList filters = { prefix + QStringLiteral("*.qm") };
        const QFileInfoList file_list = dir.entryInfoList(filters, QDir::Files);
        QStringList codes { QStringLiteral("en") };

        for (const QFileInfo& file_info : file_list)
        {
            // soa_launcher_nb -> nb
            const QString code = file_info.completeBaseName().mid(prefix.size());
            // nb -> norsk bokmål
            const QString name = QLocale(code).nativeLanguageName();
            const QString native_name = name.isEmpty() ? code : name.at(0).toUpper() + name.mid(1);
            codes.append(code);
            available_languages.append({code, native_name});
        }

        SPDLOG_INFO("Available languages: {}", codes.join(", ").toStdString());

        if (QCoreApplication::instance()) QCoreApplication::instance()->installEventFilter(this);
        connect(&soa::config::Config::instance(), &config::Config::changed, this, [this]()
        {
            const QString configured = normalize_language(config::Config::instance().language());
            if (configured != active_language && !applying)
                (void)set_language(configured);
        });
    }

    const QVector<Language>& LanguageManager::languages() const
    {
        return available_languages;
    }

    QString LanguageManager::normalize_language(const QString& code) const
    {
        const QString normalized = code.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
        const QString base = normalized.section(QLatin1Char('-'), 0, 0);

        auto check_lang = [this](const QString& lang)
        {
            return std::ranges::any_of(available_languages, [&lang](const Language& language)
            {
                return language.code == lang;
            });
        };

        // TODO: add nynorsk check once necessary.
        // no is the old code for Norwegian, but the .qm file is nb
        if (normalized == QStringLiteral("no") || normalized.startsWith(QStringLiteral("no-")))
            return QStringLiteral("nb");

        // // Exact match, like nl
        if (check_lang(normalized))
            return normalized;

        // If theres a region we don't have, we fallback to the main language like nl-be -> nl
        return check_lang(base) ? base : QStringLiteral("en");
    }

    QString LanguageManager::current_language() const
    {
        return active_language;
    }

    void LanguageManager::apply_configured_language()
    {
        (void)set_language(soa::config::Config::instance().language());
    }

    bool LanguageManager::set_language(const QString& code)
    {
        const QString normalized = normalize_language(code);
        QApplication* application = qobject_cast<QApplication*>(QCoreApplication::instance());
        if (!application)
            return false;

        if (normalized == active_language && !registered_roots.isEmpty())
        {
            detail::set_catalog_active(normalized != QStringLiteral("en"));
            retranslate_registered();
            emit language_changed(active_language);
            return true;
        }

        if (translator)
            application->removeTranslator(translator);

        bool loaded = true;
        if (normalized != QStringLiteral("en"))
        {
            loaded = translator->load(QStringLiteral(":/i18n/soa_launcher_%1.qm").arg(normalized));
            if (loaded)
                application->installTranslator(translator);
        }

        if (!loaded)
        {
            active_language = QStringLiteral("en");
            detail::set_catalog_active(false);
            soa::config::Config::instance().set_language(active_language);
            retranslate_registered();
            emit language_load_failed(normalized);
            emit language_changed(active_language);
            return false;
        }

        active_language = normalized;
        detail::set_catalog_active(active_language != QStringLiteral("en"));
        soa::config::Config::instance().set_language(active_language);
        retranslate_registered();
        emit language_changed(active_language);
        return true;
    }

}
