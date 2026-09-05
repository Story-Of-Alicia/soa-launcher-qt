#include "ui/RulesDocumentStore.hpp"

#include "ui/Layout.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

#ifndef SOA_RULES_DOCUMENT_URL
#define SOA_RULES_DOCUMENT_URL "https://docs.google.com/document/d/1vry3ZuDtzdS_mX1P2udWlb8z2Q9Atr3p1THZdtZ2EHA/export?format=html"
#endif

namespace ui::rules
{
    QString RulesDocumentStore::rules_url()
    {
        const QString override_url = qEnvironmentVariable("SOA_RULES_DOCUMENT_URL").trimmed();
        return override_url.isEmpty()
            ? QString::fromUtf8(SOA_RULES_DOCUMENT_URL).trimmed()
            : override_url;
    }

    QString RulesDocumentStore::cache_path()
    {
        QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (root.isEmpty())
            root = QDir::homePath() + QStringLiteral("/.local/share/Story of Alicia Launcher");
        QDir().mkpath(root);
        return QDir(root).filePath(QStringLiteral("rules-document-cache.html"));
    }

    QString RulesDocumentStore::load_cached_document()
    {
        QFile cache(cache_path());
        if (!cache.open(QIODevice::ReadOnly))
            return {};
        const QByteArray source = cache.readAll();
        if (source.isEmpty() || source.size() > 4 * 1024 * 1024)
            return {};
        return QString::fromUtf8(source);
    }

    bool RulesDocumentStore::save_cached_document(const QString& html)
    {
        if (html.isEmpty() || html.toUtf8().size() > 4 * 1024 * 1024)
            return false;

        QSaveFile cache(cache_path());
        if (!cache.open(QIODevice::WriteOnly))
            return false;
        const QByteArray encoded = html.toUtf8();
        return cache.write(encoded) == encoded.size() && cache.commit();
    }

    QString RulesDocumentStore::prepare_document(const QByteArray& source,
                                                  const QSize& window_size)
    {
        QString html = QString::fromUtf8(source);
        html.remove(QRegularExpression(QStringLiteral("<script\\b[^>]*>[\\s\\S]*?</script>"),
                                       QRegularExpression::CaseInsensitiveOption));
        html.remove(QRegularExpression(QStringLiteral("<!--([\\s\\S]*?)-->")));

        QString styles;
        const QRegularExpression style_expression(
            QStringLiteral("<style\\b[^>]*>([\\s\\S]*?)</style>"),
            QRegularExpression::CaseInsensitiveOption);
        auto style_iterator = style_expression.globalMatch(html);
        while (style_iterator.hasNext())
            styles += style_iterator.next().captured(1) + QLatin1Char('\n');

        const QRegularExpression body_expression(
            QStringLiteral("<body\\b[^>]*>([\\s\\S]*?)</body>"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch body_match = body_expression.match(html);
        QString body = body_match.hasMatch() ? body_match.captured(1) : html;
        body.remove(QRegularExpression(QStringLiteral("font-size\\s*:[^;\"']+;?"),
                                       QRegularExpression::CaseInsensitiveOption));
        body = rewrite_links(body);

        const QString launcher_styles = QStringLiteral(
            "body, p, li, td, span, a { font-family:'Inter'; font-size:%1px; line-height:1.5; color:#392518; }"
            "h1, h2, h3, h4 { color:#4F1717; font-family:'Eurostile'; font-weight:800; }"
            "h1 { font-size:%2px; text-align:center; margin:0 0 %3px 0; }"
            "h2 { font-size:%4px; text-align:center; margin:0 0 %5px 0; }"
            "h3 { font-size:%6px; margin-top:%7px; }"
            "a { color:#20AEDD; text-decoration:none; }"
            "hr { margin:%8px 0 %9px 0; color:#4F1717; }"
            "ul, ol { margin-left:%10px; }"
            "table { border-collapse:collapse; width:100%; }"
            "td { vertical-align:top; padding:%11px %12px; }")
            .arg(qMax(9, util::layout::scaled(13, window_size)))
            .arg(qMax(14, util::layout::scaled(22, window_size)))
            .arg(util::layout::scaled(8, window_size))
            .arg(qMax(10, util::layout::scaled(15, window_size)))
            .arg(util::layout::scaled(28, window_size))
            .arg(qMax(9, util::layout::scaled(14, window_size)))
            .arg(util::layout::scaled(22, window_size))
            .arg(util::layout::scaled(44, window_size))
            .arg(util::layout::scaled(6, window_size))
            .arg(util::layout::scaled(18, window_size))
            .arg(util::layout::scaled(2, window_size))
            .arg(util::layout::scaled(6, window_size));
        return QStringLiteral("<html><head><style>%1\n%2</style></head><body>%3</body></html>")
            .arg(styles, launcher_styles, body);
    }

    QString RulesDocumentStore::rewrite_links(const QString& html)
    {
        const QRegularExpression expression(
            QStringLiteral("href\\s*=\\s*([\"'])(.*?)\\1"),
            QRegularExpression::CaseInsensitiveOption);
        QString result;
        qsizetype cursor = 0;
        auto iterator = expression.globalMatch(html);
        while (iterator.hasNext())
        {
            const QRegularExpressionMatch match = iterator.next();
            result += html.mid(cursor, match.capturedStart() - cursor);
            QString target = match.captured(2);
            target.replace(QStringLiteral("&amp;"), QStringLiteral("&"), Qt::CaseInsensitive);
            QUrl url(target);
            if (url.isValid()
                && url.host().compare(QStringLiteral("www.google.com"), Qt::CaseInsensitive) == 0
                && url.path() == QStringLiteral("/url"))
            {
                const QString unwrapped = QUrlQuery(url).queryItemValue(QStringLiteral("q"));
                if (!unwrapped.isEmpty())
                    target = unwrapped;
            }
            const QChar quote = match.captured(1).isEmpty() ? QLatin1Char('\'') : match.captured(1).front();
            result += QStringLiteral("href=") + quote + target.toHtmlEscaped() + quote;
            cursor = match.capturedEnd();
        }
        result += html.mid(cursor);
        return result;
    }
}
