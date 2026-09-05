#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>

namespace ui::rules
{
    class RulesDocumentStore final
    {
    public:
        [[nodiscard]] static QString rules_url();
        [[nodiscard]] static QString cache_path();
        [[nodiscard]] static QString load_cached_document();
        [[nodiscard]] static bool save_cached_document(const QString& html);
        [[nodiscard]] static QString prepare_document(const QByteArray& source,
                                                      const QSize& window_size);

    private:
        [[nodiscard]] static QString rewrite_links(const QString& html);
    };
}
