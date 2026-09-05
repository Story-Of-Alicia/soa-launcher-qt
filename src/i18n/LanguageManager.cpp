#include "i18n/LanguageManager.hpp"
#include "TranslationPrivate.hpp"

#include "config/Config.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QTranslator>

namespace util::i18n
{
    LanguageManager& LanguageManager::instance()
    {
        static LanguageManager manager;
        return manager;
    }

    LanguageManager::LanguageManager(QObject* parent)
        : QObject(parent),
          available_languages{
              {QStringLiteral("en"), QStringLiteral("English")},
              {QStringLiteral("nb"), QStringLiteral("Norsk")},
              {QStringLiteral("nl"), QStringLiteral("Nederlands")}}
    {
        translator = new QTranslator(this);
        if (QCoreApplication::instance())
            QCoreApplication::instance()->installEventFilter(this);
        connect(&util::config::Config::instance(), &util::config::Config::changed, this, [this]()
        {
            const QString configured = normalize_language(util::config::Config::instance().language());
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
        if (normalized == QStringLiteral("no") || normalized.startsWith(QStringLiteral("no-"))
            || normalized.startsWith(QStringLiteral("nb-")))
            return QStringLiteral("nb");
        if (normalized.startsWith(QStringLiteral("nl-")))
            return QStringLiteral("nl");
        if (normalized.startsWith(QStringLiteral("en-")))
            return QStringLiteral("en");
        for (const Language& language : available_languages)
        {
            if (language.code == normalized)
                return normalized;
        }
        return QStringLiteral("en");
    }

    QString LanguageManager::current_language() const
    {
        return active_language;
    }

    void LanguageManager::apply_configured_language()
    {
        (void)set_language(util::config::Config::instance().language());
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
            util::config::Config::instance().set_language(active_language);
            retranslate_registered();
            emit language_load_failed(normalized);
            emit language_changed(active_language);
            return false;
        }

        active_language = normalized;
        detail::set_catalog_active(active_language != QStringLiteral("en"));
        util::config::Config::instance().set_language(active_language);
        retranslate_registered();
        emit language_changed(active_language);
        return true;
    }

}
