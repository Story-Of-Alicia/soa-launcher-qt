#include "ui/RulesAgreement.hpp"

#include "network/SwiftHttpClient.hpp"
#include "ui/Assets.hpp"
#include "i18n/LanguageManager.hpp"
#include "ui/Layout.hpp"
#include "ui/RulesDocumentStore.hpp"
#include "ui/SimpleUtils.hpp"

#include <QDesktopServices>
#include <QFrame>
#include <QFontMetrics>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>

#include <spdlog/spdlog.h>



namespace
{
    constexpr QSize k_box_size {667, 635};
    constexpr int k_accept_cooldown_seconds = 10;

    QRect box_rect(const QSize window_size)
    {
        return util::layout::centered(k_box_size, window_size, 0, 4);
    }

    QRect local_rect(const QSize window_size, const QRect source)
    {
        return util::layout::scaled(source, window_size).translated(box_rect(window_size).topLeft());
    }

    void fit_button_label(QLabel* label, const int base_size)
    {
        if (!label)
            return;
        QFont font = label->font();
        int size = base_size;
        const int available = qMax(1, label->width() - util::layout::scaled(12, label->window()->size()));
        while (size > qMax(8, util::layout::scaled(10, label->window()->size())))
        {
            font.setPixelSize(size);
            if (QFontMetrics(font).horizontalAdvance(label->text()) <= available)
                break;
            --size;
        }
        font.setPixelSize(size);
        label->setFont(font);
    }
}

RulesAgreement::RulesAgreement(QWidget* parent)
    : ModalOverlay(parent)
{
    setup_controls();
    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed,
            this, [this]()
    {
        retranslate_content();
        update();
    });
}

void RulesAgreement::setup_controls()
{
    const QSize w = window()->size();

    network = new core::network::SwiftHttpClient(this);
    cooldown_timer = new QTimer(this);
    cooldown_timer->setInterval(1000);

    rules_text = new QTextBrowser(this);
    rules_text->setGeometry(local_rect(w, {20, 35, 597, 483}));
    rules_text->setFrameShape(QFrame::NoFrame);
    rules_text->setOpenLinks(false);
    rules_text->setOpenExternalLinks(false);
    rules_text->setStyleSheet(QStringLiteral(
        "QTextBrowser { background:transparent; color:#392518; padding:%1px %2px %2px %3px; "
        "font-family:'Inter'; font-size:%4px; border:0px; }"
        "QScrollBar:vertical { width:%5px; background:#E4DED9; margin:%6px 0px 0px 0px; }"
        "QScrollBar::handle:vertical { background:#B0A297; border-radius:%7px; min-height:%8px; }"
        "QScrollBar::handle:vertical:hover { background:#9A8A7E; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }")
        .arg(util::layout::scaled(32, w))
        .arg(util::layout::scaled(16, w))
        .arg(util::layout::scaled(48, w))
        .arg(qMax(9, util::layout::scaled(13, w)))
        .arg(qMax(4, util::layout::scaled(6, w)))
        .arg(util::layout::scaled(35, w))
        .arg(util::layout::scaled(3, w))
        .arg(util::layout::scaled(28, w)));

    agree_button = util::simple_utils::make_flat_button(this);
    const QSize agree_size = util::layout::scaled(
        util::assets::translated_buttons[util::assets::Button::Agree].normal.size(), w);
    const QRect box = box_rect(w);
    agree_button->setGeometry(box.center().x() - agree_size.width() / 2,
                              box.bottom() - util::layout::scaled(64, w),
                              agree_size.width(), agree_size.height());
    agree_button->setIconSize(agree_size);
    agree_button->installEventFilter(this);
    agree_button->setAccessibleName(QStringLiteral("Agree with the rules"));

    agree_button_label = new QLabel(agree_button);
    agree_button_label->setGeometry(agree_button->rect());
    agree_button_label->setAlignment(Qt::AlignCenter);
    agree_button_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont agree_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    agree_font.setPixelSize(util::layout::scaled(12, w));
    agree_font.setWeight(QFont::Black);
    agree_button_label->setFont(agree_font);
    agree_button_label->setStyleSheet(QStringLiteral(
        "QLabel { color:#FFFFFF; background:transparent; }"
        "QLabel:disabled { color:#FFFFFF; }"));
    agree_button_label->raise();

    connect(rules_text->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](const int value)
    {
        QScrollBar* bar = rules_text->verticalScrollBar();
        if (!has_scrolled_to_end && value >= bar->maximum() - util::layout::scaled(60, window()->size()))
        {
            has_scrolled_to_end = true;
            update_agree_button();
            SPDLOG_INFO("rules document scrolled to end");
        }
    });
    connect(rules_text, &QTextBrowser::anchorClicked, this, [this](const QUrl& url)
    {
        if (url.isRelative() || url.scheme().isEmpty())
        {
            rules_text->scrollToAnchor(url.fragment().isEmpty() ? url.toString() : url.fragment());
            return;
        }
        QDesktopServices::openUrl(url);
    });
    connect(cooldown_timer, &QTimer::timeout, this, [this]()
    {
        seconds_remaining = qMax(0, seconds_remaining - 1);
        if (seconds_remaining == 0)
            cooldown_timer->stop();
        update_agree_button();
    });
    connect(agree_button, &QPushButton::clicked, this, [this]()
    {
        if (!document_ready)
        {
            if (!loading)
                load_rules();
            return;
        }
        if (seconds_remaining > 0 && !has_scrolled_to_end)
            return;
        hide();
        emit accepted();
    });

    retranslate_content();
    update_agree_button();
}




void RulesAgreement::load_rules()
{
    if (request_id != 0)
        return;

    const QUrl url(ui::rules::RulesDocumentStore::rules_url());
    const bool allow_insecure = qEnvironmentVariableIntValue("SOA_ALLOW_INSECURE_RULES_URL") == 1;
    if (!url.isValid()
        || (url.scheme() != QStringLiteral("https") && !allow_insecure))
    {
        show_load_failure(util::i18n::translate("The rules document URL is invalid or insecure."));
        return;
    }

    loading = !document_ready;
    load_error.clear();
    if (!document_ready)
    {
        const int loading_height = util::layout::scaled(330, window()->size());
        rules_text->setHtml(QStringLiteral(
            "<div style='height:%1px; display:flex; align-items:center; justify-content:center; "
            "color:#988776; text-align:center;'>%2</div>")
            .arg(loading_height)
            .arg(util::i18n::translate("Loading rules...").toHtmlEscaped()));
    }
    update_agree_button();

    request_id = network->get(
        url,
        15000,
        4 * 1024 * 1024,
        QByteArray("text/html,application/xhtml+xml"),
        QByteArray("Story-Of-Alicia-Launcher"),
        allow_insecure,
        [this](const core::network::HttpResponse& response)
        {
            request_id = 0;
            finish_rules_request(response);
        });
    if (request_id == 0)
    {
        loading = false;
        show_load_failure(util::i18n::translate("Failed to start the rules request."));
    }
}

void RulesAgreement::finish_rules_request(const core::network::HttpResponse& response)
{
    loading = false;
    const int status = response.status;
    const QString network_message = response.error;
    const QString final_host = response.final_url.host().toLower();
    const QByteArray source = response.data;

    if (response.result != soa_http_result_completed || status < 200 || status >= 300)
    {
        if (!document_ready)
        {
            const QString cached = ui::rules::RulesDocumentStore::load_cached_document();
            if (!cached.isEmpty())
            {
                document_html = cached;
                rules_text->setHtml(document_html);
                document_ready = true;
                load_error.clear();
                start_cooldown();
            }
        }
        if (!document_ready)
        {
            show_load_failure(status > 0
                ? util::i18n::translate("The rules server returned HTTP %1.").arg(status)
                : util::i18n::translate("Failed to load rules: %1").arg(network_message));
        }
        else
        {
            SPDLOG_WARN("rules refresh failed; cached document retained: {}", network_message.toStdString());
            update_agree_button();
        }
        return;
    }
    if (source.isEmpty() || source.size() > 4 * 1024 * 1024)
    {
        if (!document_ready)
            show_load_failure(util::i18n::translate("The rules document was empty or unexpectedly large."));
        return;
    }
    if (final_host.contains(QStringLiteral("accounts.google.com"))
        || source.toLower().contains("servicelogin"))
    {
        if (!document_ready)
            show_load_failure(util::i18n::translate("The rules document is not publicly accessible."));
        return;
    }
    show_document(source, true);
}

void RulesAgreement::show_document(const QByteArray& source, const bool save_cache)
{
    const QString prepared = ui::rules::RulesDocumentStore::prepare_document(source, window()->size());
    if (prepared.isEmpty())
    {
        show_load_failure(util::i18n::translate("The rules document could not be rendered."));
        return;
    }

    document_html = prepared;
    rules_text->setHtml(document_html);
    document_ready = true;
    load_error.clear();
    rules_text->verticalScrollBar()->setValue(0);
    start_cooldown();

    if (save_cache && !ui::rules::RulesDocumentStore::save_cached_document(document_html))
        SPDLOG_WARN("rules document cache could not be refreshed");
    update_agree_button();
    SPDLOG_INFO("rules document rendered in launcher");
}

void RulesAgreement::show_load_failure(const QString& reason)
{
    loading = false;
    document_ready = false;
    load_error = reason;
    const QString link = ui::rules::RulesDocumentStore::rules_url();
    const QSize w = window()->size();
    rules_text->setHtml(QStringLiteral(
        "<div style='padding:%1px %2px; text-align:center; color:#8B2E2E;'>"
        "<p><b>%3</b></p><p>%4</p><p><a href='%5'>%6</a></p></div>")
        .arg(util::layout::scaled(90, w))
        .arg(util::layout::scaled(28, w))
        .arg(util::i18n::translate("Failed to load rules").toHtmlEscaped(),
             reason.toHtmlEscaped(),
             link.toHtmlEscaped(),
             util::i18n::translate("Open the rules in your browser").toHtmlEscaped()));
    update_agree_button();
    SPDLOG_ERROR("failed to load rules document: {}", reason.toStdString());
}

void RulesAgreement::start_cooldown()
{
    seconds_remaining = k_accept_cooldown_seconds;
    has_scrolled_to_end = rules_text->verticalScrollBar()->maximum() <= 0;
    cooldown_timer->start();
    update_agree_button();
}

void RulesAgreement::update_agree_button()
{
    const auto& assets = util::assets::translated_buttons[util::assets::Button::Agree];
    if (loading)
    {
        agree_button->setEnabled(false);
        set_button_pixmap(assets.loading.isNull() ? assets.normal : assets.loading);
        set_button_text(QStringLiteral("LOADING RULES..."));
    }
    else if (!document_ready)
    {
        agree_button->setEnabled(true);
        set_button_pixmap(assets.normal);
        set_button_text(QStringLiteral("RETRY"));
    }
    else if (seconds_remaining > 0 && !has_scrolled_to_end)
    {
        agree_button->setEnabled(false);
        set_button_pixmap(assets.loading.isNull() ? assets.normal : assets.loading);
        agree_button_label->setText(util::i18n::translate("PLEASE READ (%1)")
                                        .arg(seconds_remaining));
        fit_button_label(agree_button_label, util::layout::scaled(12, window()->size()));
    }
    else
    {
        agree_button->setEnabled(true);
        set_button_pixmap(assets.normal);
        set_button_text(QStringLiteral("I AGREE WITH THE RULES"));
    }
    agree_button_label->setEnabled(true);
    agree_button_label->raise();
}

void RulesAgreement::retranslate_content()
{
    agree_button->setAccessibleName(util::i18n::translate("Agree with the rules"));
    if (loading && !document_ready)
    {
        const int loading_height = util::layout::scaled(330, window()->size());
        rules_text->setHtml(QStringLiteral(
            "<div style='height:%1px; display:flex; align-items:center; justify-content:center; "
            "color:#988776; text-align:center;'>%2</div>")
            .arg(loading_height)
            .arg(util::i18n::translate("Loading rules...").toHtmlEscaped()));
    }
    else if (!load_error.isEmpty() && !document_ready)
    {
        show_load_failure(load_error);
    }
    update_agree_button();
}

void RulesAgreement::set_button_pixmap(const QPixmap& pixmap)
{
    const QPixmap scaled = pixmap.scaled(agree_button->iconSize(), Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation);
    QIcon icon;
    icon.addPixmap(scaled, QIcon::Normal);
    icon.addPixmap(scaled, QIcon::Disabled);
    agree_button->setIcon(icon);
}

void RulesAgreement::set_button_text(const QString& source)
{
    agree_button_label->setText(util::i18n::translate(source));
    fit_button_label(agree_button_label, util::layout::scaled(12, window()->size()));
}



void RulesAgreement::showEvent(QShowEvent* event)
{
    seconds_remaining = k_accept_cooldown_seconds;
    has_scrolled_to_end = false;
    rules_text->verticalScrollBar()->setValue(0);
    if (document_html.isEmpty())
    {
        const QString cached = ui::rules::RulesDocumentStore::load_cached_document();
        if (!cached.isEmpty())
        {
            document_html = cached;
            rules_text->setHtml(document_html);
            document_ready = true;
            load_error.clear();
            start_cooldown();
        }
    }
    if (request_id == 0)
        load_rules();
    update_agree_button();
    ModalOverlay::showEvent(event);
}

void RulesAgreement::paint_content(QPainter& painter)
{
    painter.drawPixmap(box_rect(window()->size()),
                       util::assets::images[util::assets::Image::RulesFrame]);
}

bool RulesAgreement::eventFilter(QObject* object, QEvent* event)
{
    if (object == agree_button && agree_button->isEnabled())
    {
        const auto& assets = util::assets::translated_buttons[util::assets::Button::Agree];
        switch (event->type())
        {
            case QEvent::Enter:
                set_button_pixmap(assets.hover);
                break;
            case QEvent::Leave:
                set_button_pixmap(assets.normal);
                break;
            case QEvent::MouseButtonPress:
                set_button_pixmap(assets.clicked);
                break;
            case QEvent::MouseButtonRelease:
                set_button_pixmap(agree_button->underMouse() ? assets.hover : assets.normal);
                break;
            default:
                break;
        }
    }
    return QWidget::eventFilter(object, event);
}
