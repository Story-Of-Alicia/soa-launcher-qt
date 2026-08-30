#include "ui/WineSelectMenu.hpp"
#include "ui/LauncherDialog.hpp"
#include "i18n/LanguageManager.hpp"

#include <QFileDialog>
#include <QAbstractButton>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

#include "common/Log.hpp"
#include "runtime/MacWineRuntime.hpp"
#include "runtime/WineRegistry.hpp"
#include "ui/Assets.hpp"
#include "ui/Colors.hpp"
#include "config/Config.hpp"
#include "ui/Layout.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/Styles.hpp"
#include <spdlog/spdlog.h>

using util::config::Config;
namespace cw = core::wine;
namespace
{
    constexpr QSize k_runtime_box_size {700, 520};

    QRect runtime_box_rect(const QSize window_size)
    {
        return util::layout::centered(k_runtime_box_size, window_size, 0, 12);
    }

    QRect runtime_local_rect(const QSize window_size, const QRect source)
    {
        return util::layout::scaled(source, window_size)
            .translated(runtime_box_rect(window_size).topLeft());
    }

    QString scroll_style(const QSize window_size)
    {
        return QStringLiteral(
            "QScrollArea { background:rgba(255,255,255,0.34); border:1px solid #D8C8B6;"
            " border-radius:0px; }"
            "QScrollArea > QWidget > QWidget { background:transparent; }"
            "QScrollBar:vertical { width:%1px; background:rgba(236,226,215,0.72);"
            " border-radius:%2px; margin:%3px %4px %3px %5px; }"
            "QScrollBar::handle:vertical { background:#BFAE9B; border-radius:%6px; min-height:%7px; }"
            "QScrollBar::handle:vertical:hover { background:#2FB4E0; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }")
            .arg(util::layout::scaled(12, window_size))
            .arg(util::layout::scaled(6, window_size))
            .arg(util::layout::scaled(4, window_size))
            .arg(util::layout::scaled(3, window_size))
            .arg(util::layout::scaled(1, window_size))
            .arg(util::layout::scaled(5, window_size))
            .arg(util::layout::scaled(34, window_size));
    }

    QString status_style(const QSize window_size)
    {
        return QStringLiteral(
            "QLabel { color:#6B5B4D; background:rgba(244,236,227,0.66);"
            " border:1px solid rgba(201,187,170,0.74); border-radius:%1px; padding:%2px %3px; }")
            .arg(util::layout::scaled(6, window_size))
            .arg(util::layout::scaled(5, window_size))
            .arg(util::layout::scaled(10, window_size));
    }

    QString empty_style(const QSize window_size)
    {
        return QStringLiteral("QLabel { color:#8A7A6B; padding:%1px; background:transparent; }")
            .arg(util::layout::scaled(28, window_size));
    }

    class RuntimeRow final : public QAbstractButton
    {
    public:
        RuntimeRow(QString name, QString type, QString architecture, QString path,
                   QString details, QWidget* parent)
            : QAbstractButton(parent), runtime_name(std::move(name)), runtime_type(std::move(type)),
              runtime_architecture(std::move(architecture)), runtime_path(std::move(path)),
              runtime_details(std::move(details))
        {
            setCheckable(true);
            setCursor(Qt::PointingHandCursor);
            setFocusPolicy(Qt::ClickFocus);
            setMouseTracking(true);
            setAttribute(Qt::WA_Hover, true);
            setAttribute(Qt::WA_NoSystemBackground, true);
            setAutoFillBackground(false);
            setToolTip(QStringLiteral("%1\n%2 · %3 · %4\n%5")
                .arg(runtime_name, runtime_type, runtime_architecture, runtime_details, runtime_path));
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            const QSize host = window()->size();
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);

            const QRectF card = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
            const qreal radius = 0.0;

            QColor top;
            QColor bottom;
            QColor border;
            if (!isEnabled())
            {
                top = QColor(239, 233, 226);
                bottom = QColor(226, 218, 209);
                border = QColor(205, 192, 178);
            }
            else if (isChecked())
            {
                top = QColor(236, 250, 255);
                bottom = QColor(205, 237, 247);
                border = QColor(47, 180, 224);
            }
            else if (underMouse())
            {
                top = QColor(255, 255, 255);
                bottom = QColor(245, 235, 225);
                border = QColor(181, 154, 130);
            }
            else
            {
                top = QColor(253, 249, 245);
                bottom = QColor(239, 229, 219);
                border = QColor(201, 181, 160);
            }

            QPainterPath shape;
            shape.addRoundedRect(card, radius, radius);
            QLinearGradient gradient(card.topLeft(), card.bottomLeft());
            gradient.setColorAt(0.0, top);
            gradient.setColorAt(1.0, bottom);
            painter.fillPath(shape, gradient);

            QPen edge(border, qMax(1, util::layout::scaled(isChecked() ? 2 : 1, host)));
            painter.setPen(edge);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(shape);

            painter.save();
            painter.setClipPath(shape);
            painter.fillRect(QRectF(card.left(), card.top(), card.width(),
                                    util::layout::scaled(2, host)),
                             QColor(255, 255, 255, 160));
            painter.restore();

            const int left = util::layout::scaled(16, host);
            const int right = util::layout::scaled(48, host);
            const QRect text_area = card.toAlignedRect().adjusted(
                left, util::layout::scaled(7, host),
                -right, -util::layout::scaled(7, host));

            QFont title_font = util::assets::fonts[util::assets::Font::EurostileBold];
            title_font.setPixelSize(util::layout::scaled(14, host));
            title_font.setWeight(QFont::Bold);
            painter.setFont(title_font);
            painter.setPen(isEnabled() ? util::colors::k_text_maroon : QColor(138, 122, 107));
            const QRect title_rect(text_area.left(), text_area.top(), text_area.width(),
                                   util::layout::scaled(23, host));
            painter.drawText(title_rect, Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(runtime_name, Qt::ElideRight,
                                                              title_rect.width()));

            QFont detail_font = util::assets::fonts[util::assets::Font::Inter];
            detail_font.setPixelSize(util::layout::scaled(12, host));
            detail_font.setWeight(QFont::DemiBold);
            painter.setFont(detail_font);
            painter.setPen(isEnabled() ? QColor(84, 65, 51) : QColor(145, 132, 120));
            const QString summary = QStringLiteral("%1  ·  %2  ·  %3")
                .arg(runtime_type, runtime_architecture, runtime_details);
            const QRect summary_rect(text_area.left(), title_rect.bottom(), text_area.width(),
                                     util::layout::scaled(22, host));
            painter.drawText(summary_rect, Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(summary, Qt::ElideRight,
                                                              summary_rect.width()));

            detail_font.setPixelSize(util::layout::scaled(11, host));
            detail_font.setWeight(QFont::Normal);
            painter.setFont(detail_font);
            painter.setPen(QColor(126, 110, 94));
            const QRect path_rect(text_area.left(), summary_rect.bottom(), text_area.width(),
                                  util::layout::scaled(21, host));
            painter.drawText(path_rect, Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(runtime_path, Qt::ElideMiddle,
                                                              path_rect.width()));

            const QPoint indicator(
                qRound(card.right()) - util::layout::scaled(22, host),
                qRound(card.center().y()));
            const int indicator_radius = util::layout::scaled(9, host);
            painter.setPen(QPen(isChecked() ? QColor(47, 180, 224) : QColor(169, 147, 126),
                                qMax(1, util::layout::scaled(2, host))));
            painter.setBrush(isEnabled() ? QColor(255, 252, 248) : QColor(229, 222, 214));
            painter.drawEllipse(indicator, indicator_radius, indicator_radius);
            if (isChecked())
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(47, 180, 224));
                painter.drawEllipse(indicator, indicator_radius - util::layout::scaled(3, host),
                                    indicator_radius - util::layout::scaled(3, host));
                QPen check_pen(Qt::white, qMax(1, util::layout::scaled(2, host)));
                check_pen.setCapStyle(Qt::RoundCap);
                check_pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(check_pen);
                const int x = indicator.x();
                const int y = indicator.y();
                painter.drawLine(x - util::layout::scaled(4, host), y,
                                 x - util::layout::scaled(1, host), y + util::layout::scaled(3, host));
                painter.drawLine(x - util::layout::scaled(1, host), y + util::layout::scaled(3, host),
                                 x + util::layout::scaled(5, host), y - util::layout::scaled(4, host));
            }


        }

        void enterEvent(QEnterEvent* event) override
        {
            update();
            QAbstractButton::enterEvent(event);
        }

        void leaveEvent(QEvent* event) override
        {
            update();
            QAbstractButton::leaveEvent(event);
        }
    
    private:
        QString runtime_name;
        QString runtime_type;
        QString runtime_architecture;
        QString runtime_path;
        QString runtime_details;
    };

    class AssetTextButton final : public QAbstractButton
    {
    public:
        AssetTextButton(const util::assets::Button asset, QString source, QWidget* parent)
            : QAbstractButton(parent), asset_key(asset), text_source(std::move(source))
        {
            setCursor(Qt::PointingHandCursor);
            setFocusPolicy(Qt::ClickFocus);
            setAutoFillBackground(false);
            setAttribute(Qt::WA_NoSystemBackground, true);
            connect(&util::i18n::LanguageManager::instance(),
                    &util::i18n::LanguageManager::language_changed,
                    this, [this]() { update(); });
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            const auto& states = util::assets::translated_buttons[asset_key];
            const QPixmap* pixmap = &states.normal;
            if (!isEnabled())
                pixmap = &states.normal;
            else if (isDown() && !states.clicked.isNull())
                pixmap = &states.clicked;
            else if (underMouse() && !states.hover.isNull())
                pixmap = &states.hover;

            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setRenderHint(QPainter::TextAntialiasing);
            if (!pixmap->isNull())
            {
                painter.setOpacity(isEnabled() ? 1.0 : 0.58);
                painter.drawPixmap(rect(), *pixmap);
                painter.setOpacity(1.0);
            }

            QFont font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
            font.setPixelSize(util::layout::scaled(11, window()->size()));
            font.setWeight(QFont::Black);
            painter.setFont(font);
            painter.setPen(QColor(255, 255, 255, isEnabled() ? 255 : 220));
            const QString text = util::i18n::translate(text_source).toUpper();
            painter.drawText(rect().adjusted(util::layout::scaled(8, window()->size()), 0,
                                             -util::layout::scaled(8, window()->size()), 0),
                             Qt::AlignCenter,
                             painter.fontMetrics().elidedText(text, Qt::ElideRight,
                                                              width() - util::layout::scaled(16, window()->size())));


        }

        void enterEvent(QEnterEvent* event) override
        {
            update();
            QAbstractButton::enterEvent(event);
        }

        void leaveEvent(QEvent* event) override
        {
            update();
            QAbstractButton::leaveEvent(event);
        }

    private:
        util::assets::Button asset_key;
        QString text_source;
    };

}

WineSelectMenu::WineSelectMenu(QWidget* parent) : ModalOverlay(parent)
{
    build_ui();
    detector = new QFutureWatcher<QVector<cw::WineInstall>>(this);
    connect(detector, &QFutureWatcher<QVector<cw::WineInstall>>::finished,
            this, &WineSelectMenu::finish_scan);
    relayout();
    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed, this,
            [this]() { retranslate_dynamic_text(); });
}

void WineSelectMenu::build_ui()
{
    close_button = util::simple_utils::make_flat_button(this);
    close_button->setAccessibleName(QStringLiteral("Close runtime selection"));
    close_button->setIcon(QIcon(util::assets::images[util::assets::Image::CloseSettings]));
    connect(close_button, &QPushButton::clicked, this, [this]() { hide(); emit closed(); });

    runtime_status = new QLabel(this);
    runtime_status->setStyleSheet(status_style(window()->size()));
    runtime_status->setWordWrap(true);

    list = new QScrollArea(this);
    list->setWidgetResizable(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setFrameShape(QFrame::NoFrame);
    list->setStyleSheet(scroll_style(window()->size()));
    list->viewport()->setAutoFillBackground(false);
    list->viewport()->setAttribute(Qt::WA_StyledBackground, false);

    const auto make_asset_button = [this](const util::assets::Button asset,
                                          const QString& source)
    {
        return new AssetTextButton(asset, source, this);
    };

    rescan_button = make_asset_button(
        util::assets::Button::Cancel, QStringLiteral("Rescan"));
#if defined(Q_OS_MACOS)
    const QString add_runtime_text = QStringLiteral("Add Wine…");
#else
    const QString add_runtime_text = QStringLiteral("Add Runtime…");
#endif
    browse_button = make_asset_button(
        util::assets::Button::Cancel, add_runtime_text);
    continue_button = make_asset_button(
        util::assets::Button::Install, QStringLiteral("Continue"));
    rescan_button->setAccessibleName(QStringLiteral("Rescan runtimes"));
    browse_button->setAccessibleName(QStringLiteral("Add runtime"));
    continue_button->setAccessibleName(QStringLiteral("Continue with selected runtime"));
    connect(rescan_button, &QAbstractButton::clicked, this, &WineSelectMenu::rescan);
    connect(browse_button, &QAbstractButton::clicked, this, &WineSelectMenu::browse_runtime);
    continue_button->setEnabled(false);
    connect(continue_button, &QAbstractButton::clicked, this, &WineSelectMenu::confirm);

#if defined(Q_OS_MACOS)
    rosetta_button = make_asset_button(
        util::assets::Button::Cancel, QStringLiteral("Request Rosetta…"));
    rosetta_button->setAccessibleName(QStringLiteral("Request Rosetta installation"));
    connect(rosetta_button, &QAbstractButton::clicked, this, &WineSelectMenu::request_rosetta);
#endif

    close_button->raise();
    rescan_button->raise();
    browse_button->raise();
    if (rosetta_button) rosetta_button->raise();
    continue_button->raise();
}

void WineSelectMenu::populate()
{
    auto* content = new QWidget;
    content->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* lay = new QVBoxLayout(content);
    const int margin = util::layout::scaled(8, window()->size());
    lay->setContentsMargins(margin, margin, margin, margin);
    lay->setSpacing(util::layout::scaled(8, window()->size()));

    rows.clear();
    selected = -1;
    continue_button->setEnabled(false);

    for (int i = 0; i < runtimes.size(); ++i)
    {
        const cw::WineInstall& wi = runtimes[i];
#if defined(Q_OS_MACOS)
        const QString type = util::i18n::translate("Wine");
#else
        const QString type = wi.type == cw::RuntimeType::Proton
            ? util::i18n::translate("Proton") : util::i18n::translate("Wine");
#endif
        QString details = wi.version.isEmpty() ? wi.issue : wi.version;
        if (details.isEmpty()) details = util::i18n::translate("Capability probe failed");
        if (wi.requires_rosetta)
        {
            details += wi.rosetta_available
                ? util::i18n::translate(" · Rosetta ready")
                : util::i18n::translate(" · Rosetta required");
        }
        const QString architecture = wi.architectures.isEmpty()
            ? util::i18n::translate("unknown architecture") : wi.architectures;
        auto* row = new RuntimeRow(wi.name, type, architecture, wi.path, details, content);
        row->setEnabled(wi.usable);
        row->setMinimumHeight(qMax(76, util::layout::scaled(94, window()->size())));
        row->setAccessibleName(util::i18n::translate("Select runtime: %1").arg(wi.name));
        row->setAccessibleDescription(QStringLiteral("%1 · %2 · %3")
            .arg(type, architecture, details));
        connect(row, &QAbstractButton::clicked, this, [this, i]() { select_row(i); });
        lay->addWidget(row);
        rows.push_back(row);
    }

    if (runtimes.isEmpty())
    {
#if defined(Q_OS_MACOS)
        const QString missingText = QStringLiteral(
            "No usable Wine installation was found. Install Wine or add a Wine app, executable, or folder.");
#else
        const QString missingText = QStringLiteral(
            "No usable Wine or Proton runtimes were found on this system.");
#endif
#if defined(Q_OS_MACOS)
        const QString emptyText =
            scanning ? util::i18n::translate("Scanning Wine installations…")
                     : util::i18n::translate(missingText);
#else
        const QString emptyText =
            scanning ? util::i18n::translate("Scanning Wine runtimes…")
                     : util::i18n::translate(missingText);
#endif
        auto* empty = new QLabel(emptyText, content);
        empty->setStyleSheet(empty_style(window()->size()));
        QFont empty_font = util::assets::fonts[util::assets::Font::Inter];
        empty_font.setPixelSize(util::layout::scaled(13, window()->size()));
        empty_font.setWeight(QFont::Medium);
        empty->setFont(empty_font);
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        lay->addWidget(empty);
    }

    lay->addStretch(1);
    list->setWidget(content);

#if defined(Q_OS_MACOS)
    const bool rosetta = cw::macos::rosetta_is_available();
    const bool tricks = cw::winetricks_available();
    runtime_status->setText(
        util::i18n::translate("winetricks: %1 · Rosetta: %2")
            .arg(tricks ? util::i18n::translate("ready")
                        : util::i18n::translate("not found"),
                 rosetta ? util::i18n::translate("ready")
                         : util::i18n::translate("not detected")));
    if (rosetta_button) rosetta_button->setVisible(!rosetta);
#else
    const bool tricks = cw::winetricks_available();
    runtime_status->setText(tricks
        ? util::i18n::translate("winetricks: ready")
        : util::i18n::translate(
              "winetricks not found - required components will be installed manually"));
#endif
    relayout();
}

void WineSelectMenu::start_scan()
{
    if (detector->isRunning()) return;
    scanning = true;
    runtimes.clear();
    rescan_button->setEnabled(false);
    browse_button->setEnabled(false);
    continue_button->setEnabled(false);
    populate();
    detector->setFuture(QtConcurrent::run([]()
    {
        QVector<cw::WineInstall> found = cw::WineRegistry::scan();
#if defined(Q_OS_MACOS)
        found.erase(std::remove_if(found.begin(), found.end(),
                                   [](const cw::WineInstall& runtime)
                                   {
                                       return runtime.type == cw::RuntimeType::Proton;
                                   }),
                    found.end());
#endif
        return found;
    }));
}

void WineSelectMenu::finish_scan()
{
    runtimes = detector->result();
    scanning = false;
    rescan_button->setEnabled(true);
    browse_button->setEnabled(true);
    populate();
}

void WineSelectMenu::rescan()
{
    start_scan();
}

void WineSelectMenu::browse_runtime()
{
#if defined(Q_OS_MACOS)
    const int selection = LauncherDialog::choose(
        this,
        LauncherDialog::Tone::Question,
        QStringLiteral("Add Wine"),
        QStringLiteral("Select a Wine application, executable, or installation folder."),
        {
            {QStringLiteral("Cancel"), LauncherDialog::Cancelled,
             LauncherDialog::ActionStyle::Neutral, true},
            {QStringLiteral("Select Wine Folder"), LauncherDialog::Primary,
             LauncherDialog::ActionStyle::Primary, false},
            {QStringLiteral("Select Wine App or Executable"), LauncherDialog::Secondary,
             LauncherDialog::ActionStyle::Primary, false}
        });

    QString path;
    if (selection == LauncherDialog::Primary)
        path = QFileDialog::getExistingDirectory(
            this, util::i18n::translate("Select Wine Folder"));
    else if (selection == LauncherDialog::Secondary)
        path = QFileDialog::getOpenFileName(
            this,
            util::i18n::translate("Select Wine App or Executable"),
            QStringLiteral("/Applications"),
            QStringLiteral("%1 (*.app);;%2 (*)")
                .arg(util::i18n::translate("Applications"),
                     util::i18n::translate("All Files")));
    else
        return;
#else
    const QString path = QFileDialog::getOpenFileName(
        this, util::i18n::translate("Select Wine Binary or Proton Script"));
#endif
    if (path.isEmpty()) return;

    cw::WineInstall install;
    QString error;
    if (!cw::WineRegistry::inspect_path(path, install, &error))
    {
        LauncherDialog::warning(this, QStringLiteral("Wine Not Usable"),
                                error.isEmpty() ? QStringLiteral("The selected Wine installation could not be used.") : error);
        return;
    }
    runtimes.append(install);
    populate();
    select_row(runtimes.size() - 1);
}

void WineSelectMenu::request_rosetta()
{
#if defined(Q_OS_MACOS)
    const bool confirmed = LauncherDialog::confirm(
        this,
        LauncherDialog::Tone::Question,
        QStringLiteral("Install Rosetta"),
        QStringLiteral("Some Wine applications are built for Intel Macs. macOS may now show its Rosetta installation prompt. Continue?"),
        QStringLiteral("Continue"),
        QStringLiteral("Cancel"));
    if (!confirmed) return;
    if (!cw::macos::request_rosetta_install_prompt())
    {
        LauncherDialog::warning(this, QStringLiteral("Rosetta Request Failed"),
                                QStringLiteral("macOS could not start the Rosetta installation request."));
        return;
    }
    LauncherDialog::information(this, QStringLiteral("Rosetta"),
                                QStringLiteral("Complete the macOS prompt, then press Rescan."));
#endif
}

void WineSelectMenu::select_row(const int index)
{
    selected = index;
    for (int i = 0; i < rows.size(); ++i) rows[i]->setChecked(i == index);
    continue_button->setEnabled(index >= 0 && index < runtimes.size() && runtimes[index].usable);
}

void WineSelectMenu::retranslate_dynamic_text()
{
    const int previous_selection = selected;
    populate();
    if (previous_selection >= 0 && previous_selection < runtimes.size())
        select_row(previous_selection);
    update();
}

void WineSelectMenu::confirm()
{
    if (selected < 0 || selected >= runtimes.size() || !runtimes[selected].usable) return;
    const cw::WineInstall& wi = runtimes[selected];
    Config::instance().set_wine_binary(wi.path);
    Config::instance().set_runtime_selected(true);
#if defined(Q_OS_MACOS)
    Config::instance().set_setup_runtime_preference(QStringLiteral("wine"));
    Config::instance().set_use_dxvk(false);
    Config::instance().set_wine_arch(QStringLiteral("win64"));
#else
    Config::instance().set_setup_runtime_preference(
        wi.type == cw::RuntimeType::Proton
            ? QStringLiteral("proton") : QStringLiteral("wine"));
#endif
    SPDLOG_INFO("runtime selected: {} ({})", wi.name.toStdString(), wi.path.toStdString());
    emit runtime_chosen();
}

void WineSelectMenu::paint_content(QPainter& painter)
{
    const QSize w = window()->size();
    const QRect box = runtime_box_rect(w);
    painter.drawPixmap(box, util::assets::images[util::assets::Image::BoxSettings]);

    QFont title_font = util::assets::fonts[util::assets::Font::EurostileBlack];
    title_font.setPixelSize(util::layout::scaled(27, w));
    title_font.setWeight(QFont::Black);
    painter.setFont(title_font);
    painter.setPen(util::colors::k_text_maroon);
    painter.drawText(runtime_local_rect(w, {30, 34, 640, 38}), Qt::AlignCenter,
                     util::i18n::translate("SELECT RUNTIME"));

    QFont body_font = util::assets::fonts[util::assets::Font::Inter];
    body_font.setPixelSize(util::layout::scaled(14, w));
    body_font.setWeight(QFont::Medium);
    painter.setFont(body_font);
    painter.setPen(util::colors::k_text_body);
#if defined(Q_OS_MACOS)
    const QString text = QStringLiteral("Choose the Wine installation used to run the game.");
#else
    const QString text = QStringLiteral("Choose the Wine or Proton version used to run the game.");
#endif
    painter.drawText(runtime_local_rect(w, {68, 88, 564, 46}),
                     Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                     util::i18n::translate(text));
}

void WineSelectMenu::showEvent(QShowEvent* event)
{
    if (!scanning && runtimes.isEmpty())
        start_scan();
    ModalOverlay::showEvent(event);
}

void WineSelectMenu::relayout()
{
    const QSize w = window()->size();
    const QRect box = runtime_box_rect(w);
    close_button->setIconSize(util::layout::scaled(util::layout::modal_close::k_icon, w));
    close_button->setGeometry(
        runtime_local_rect(w, util::layout::modal_close::rect_in(
                                  {0, 0, k_runtime_box_size.width(),
                                   k_runtime_box_size.height()})));

    list->setGeometry(runtime_local_rect(w, {46, 145, 608, 244}));

    QFont status_font = util::assets::fonts[util::assets::Font::Inter];
    status_font.setPixelSize(util::layout::scaled(12, w));
    status_font.setWeight(QFont::Medium);
    runtime_status->setFont(status_font);

    const int gap = util::layout::scaled(10, w);
    const QRect status_row = runtime_local_rect(w, {46, 398, 608, 42});
    if (rosetta_button && !rosetta_button->isHidden())
    {
        const int rosetta_width = util::layout::scaled(190, w);
        runtime_status->setGeometry(status_row.adjusted(0, 0, -rosetta_width - gap, 0));
        rosetta_button->setGeometry(status_row.right() - rosetta_width + 1,
                                    status_row.top(), rosetta_width, status_row.height());
    }
    else
    {
        runtime_status->setGeometry(status_row);
        if (rosetta_button)
            rosetta_button->setGeometry(QRect{});
    }

    const QRect buttons = runtime_local_rect(w, {46, 454, 608, 44});
    const int button_gap = util::layout::scaled(10, w);
    const int button_width = (buttons.width() - 2 * button_gap) / 3;
    rescan_button->setGeometry(buttons.left(), buttons.top(), button_width, buttons.height());
    browse_button->setGeometry(buttons.left() + button_width + button_gap,
                               buttons.top(), button_width, buttons.height());
    continue_button->setGeometry(buttons.left() + 2 * (button_width + button_gap),
                                 buttons.top(), buttons.width() - 2 * (button_width + button_gap),
                                 buttons.height());

}
