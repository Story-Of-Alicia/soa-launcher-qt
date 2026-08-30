#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QAction;
class QEvent;
class QTranslator;

namespace util::i18n
{
    struct Language
    {
        QString code;
        QString native_name;
    };

    class LanguageManager final : public QObject
    {
        Q_OBJECT

    public:
        static LanguageManager& instance();

        [[nodiscard]] const QVector<Language>& languages() const;
        [[nodiscard]] QString current_language() const;
        [[nodiscard]] bool set_language(const QString& code);
        void apply_configured_language();
        void register_tree(QObject* root);
        void retranslate_registered();

    signals:
        void language_changed(const QString& code);
        void language_load_failed(const QString& code);

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        explicit LanguageManager(QObject* parent = nullptr);
        QString normalize_language(const QString& code) const;
        void capture_tree(QObject* root);
        void capture_object(QObject* object);
        void retranslate_tree(QObject* root);
        void retranslate_object(QObject* object);

        QVector<Language> available_languages;
        QVector<QObject*> registered_roots;
        QTranslator* translator {};
        QString active_language {QStringLiteral("en")};
        bool applying {};
    };

    QString translate(const char* source);
    QString translate(const QString& source);
    QString translate(const char* source, const QString& first_argument);
}
