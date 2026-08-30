#pragma once

#include <QByteArray>
#include <QEvent>
#include <QString>

#include "ui/ModalOverlay.hpp"

class QLabel;
class QPushButton;
class QShowEvent;
class QTextBrowser;
class QTimer;

namespace core::network
{
    class SwiftHttpClient;
    struct HttpResponse;
}
class QUrl;

class RulesAgreement final : public util::modal_overlay::ModalOverlay
{
    Q_OBJECT

public:
    explicit RulesAgreement(QWidget* parent = nullptr);

signals:
    void accepted();

protected:
    void paint_content(QPainter& painter) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void setup_controls();
    void load_rules();
    void finish_rules_request(const core::network::HttpResponse& response);
    void show_document(const QByteArray& source, bool save_cache);
    void show_load_failure(const QString& reason);
    void start_cooldown();
    void update_agree_button();
    void retranslate_content();
    void set_button_pixmap(const QPixmap& pixmap);
    void set_button_text(const QString& source);
    [[nodiscard]] QString rules_url() const;
    [[nodiscard]] QString cache_path() const;
    [[nodiscard]] bool load_cached_document();
    [[nodiscard]] bool save_cached_document();
    [[nodiscard]] QString prepare_document(const QByteArray& source) const;
    [[nodiscard]] static QString rewrite_links(const QString& html);

    QTextBrowser* rules_text {};
    QPushButton* agree_button {};
    QLabel* agree_button_label {};
    core::network::SwiftHttpClient* network {};
    qulonglong request_id {};
    QTimer* cooldown_timer {};
    QString document_html;
    QString load_error;
    int seconds_remaining {10};
    bool document_ready {};
    bool has_scrolled_to_end {};
    bool loading {};
};
