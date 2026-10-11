#include "i18n/LanguageManager.hpp"
#include "TranslationPrivate.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <utility>

namespace
{
    bool translated_catalog_active {};

    const QStringList& fallback_translation_contexts()
    {
        static const QStringList contexts {
            QStringLiteral("Launcher & Navigation"),
            QStringLiteral("Home & Account"),
            QStringLiteral("Setup & Rules"),
            QStringLiteral("Game Installation & Repair"),
            QStringLiteral("Game Launch & Session"),
            QStringLiteral("Runtime & Compatibility"),
            QStringLiteral("Network & Transfers"),
            QStringLiteral("Launcher Updates"),
            QStringLiteral("Settings"),
            QStringLiteral("About & Credits"),
            QStringLiteral("Logs & Diagnostics")
        };
        return contexts;
    }

    const QRegularExpression& placeholder_expression()
    {
        static const QRegularExpression expression(
            QStringLiteral("%L?(?:[1-9][0-9]?|n)"));
        return expression;
    }

    QSet<QString> placeholders_in(const QString& text)
    {
        QSet<QString> placeholders;
        auto matches = placeholder_expression().globalMatch(text);
        while (matches.hasNext())
            placeholders.insert(matches.next().captured());
        return placeholders;
    }

    struct DynamicTranslationTemplate
    {
        QString source;
        QRegularExpression expression;
        QStringList placeholders;
        qsizetype literal_size {};
    };

    struct TranslationSourceCatalog
    {
        QStringList contexts;
        QVector<DynamicTranslationTemplate> dynamic_templates;
    };

    QRegularExpression expression_for_template(const QString& source,
                                                QStringList& placeholders,
                                                qsizetype& literal_size)
    {
        QString pattern = QStringLiteral("\\A");
        qsizetype offset = 0;
        auto matches = placeholder_expression().globalMatch(source);
        while (matches.hasNext())
        {
            const QRegularExpressionMatch match = matches.next();
            const qsizetype start = match.capturedStart();
            const qsizetype end = match.capturedEnd();
            const QString literal = source.mid(offset, start - offset);
            pattern += QRegularExpression::escape(literal);
            literal_size += literal.size();
            pattern += QStringLiteral("(.*?)");
            placeholders.push_back(match.captured());
            offset = end;
        }

        const QString tail = source.mid(offset);
        pattern += QRegularExpression::escape(tail);
        literal_size += tail.size();
        pattern += QStringLiteral("\\z");
        return QRegularExpression(pattern, QRegularExpression::DotMatchesEverythingOption);
    }

    TranslationSourceCatalog load_source_catalog()
    {
        TranslationSourceCatalog catalog;
        QFile file(QStringLiteral(":/i18n/soa_launcher_en.ts"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            catalog.contexts = fallback_translation_contexts();
            return catalog;
        }

        QXmlStreamReader xml(&file);
        QString current_context;
        bool inside_context = false;

        while (!xml.atEnd())
        {
            xml.readNext();
            if (!xml.isStartElement())
            {
                if (xml.isEndElement() && xml.name() == QLatin1String("context"))
                {
                    inside_context = false;
                    current_context.clear();
                }
                continue;
            }

            if (xml.name() == QLatin1String("context"))
            {
                inside_context = true;
                current_context.clear();
                continue;
            }

            if (inside_context && xml.name() == QLatin1String("name"))
            {
                current_context = xml.readElementText();
                if (!current_context.isEmpty() && !catalog.contexts.contains(current_context))
                    catalog.contexts.push_back(current_context);
                continue;
            }

            if (inside_context && xml.name() == QLatin1String("source"))
            {
                const QString source = xml.readElementText();
                if (source.isEmpty() || placeholders_in(source).isEmpty())
                    continue;

                DynamicTranslationTemplate entry;
                entry.source = source;
                entry.expression = expression_for_template(
                    source, entry.placeholders, entry.literal_size);
                if (entry.expression.isValid() && !entry.placeholders.isEmpty())
                    catalog.dynamic_templates.push_back(std::move(entry));
            }
        }

        if (catalog.contexts.isEmpty())
            catalog.contexts = fallback_translation_contexts();

        std::sort(catalog.dynamic_templates.begin(), catalog.dynamic_templates.end(),
            [](const DynamicTranslationTemplate& left,
               const DynamicTranslationTemplate& right)
            {
                if (left.literal_size != right.literal_size)
                    return left.literal_size > right.literal_size;
                return left.source.size() > right.source.size();
            });
        return catalog;
    }

    const TranslationSourceCatalog& source_catalog()
    {
        static const TranslationSourceCatalog catalog = load_source_catalog();
        return catalog;
    }

    QString catalogue_translation(const QString& source)
    {
        const QByteArray utf8 = source.toUtf8();
        for (const QString& context : source_catalog().contexts)
        {
            const QByteArray context_utf8 = context.toUtf8();
            const QString translated = QCoreApplication::translate(
                context_utf8.constData(), utf8.constData());
            if (!translated.trimmed().isEmpty() && translated != source
                && placeholders_in(translated) == placeholders_in(source))
            {
                return translated;
            }
        }
        return source;
    }

    QString substitute_dynamic_arguments(const DynamicTranslationTemplate& entry,
                                         const QRegularExpressionMatch& match,
                                         QString translated)
    {
        QHash<QString, QString> arguments;
        for (qsizetype index = 0; index < entry.placeholders.size(); ++index)
        {
            const QString& placeholder = entry.placeholders[index];
            if (!arguments.contains(placeholder))
                arguments.insert(placeholder, match.captured(static_cast<int>(index + 1)));
        }

        QStringList keys = arguments.keys();
        std::sort(keys.begin(), keys.end(), [](const QString& left, const QString& right)
        {
            return left.size() > right.size();
        });
        for (const QString& placeholder : keys)
            translated.replace(placeholder, arguments.value(placeholder));
        return translated;
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

        if (!placeholders_in(source).isEmpty())
            return source;

        for (const DynamicTranslationTemplate& entry : source_catalog().dynamic_templates)
        {
            const QRegularExpressionMatch match = entry.expression.match(source);
            if (!match.hasMatch())
                continue;

            const QString translated_template = catalogue_translation(entry.source);
            if (translated_template == entry.source)
                continue;

            return substitute_dynamic_arguments(entry, match, translated_template);
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
