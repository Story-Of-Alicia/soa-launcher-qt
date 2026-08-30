#include "ui/PrefixProgress.hpp"
#include "ui/LauncherDialog.hpp"
#include "ui/Assets.hpp"
#include "ui/Layout.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/Colors.hpp"
#include "ui/ProgressBar.hpp"
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include "i18n/LanguageManager.hpp"

#include "runtime/Shell.hpp"
#include "common/Log.hpp"
#include <spdlog/spdlog.h>

namespace dl = util::layout::progress_modal;

namespace
{
    constexpr int    k_anim_interval_ms = 16;
    constexpr double k_fill_per_tick    = 0.6;
    constexpr double k_pct_runtime      = 10.0;
    constexpr double k_pct_wineboot     = 45.0;
    constexpr double k_pct_winetricks   = 80.0;
    constexpr double k_pct_done         = 100.0;
}

PrefixProgress::PrefixProgress(core::wine::Shell* shell_, QWidget* parent)
    : ModalOverlay(parent), shell(shell_)
{
    setup_buttons();
    close_button->installEventFilter(this);

    anim = new QTimer(this);
    anim->setInterval(k_anim_interval_ms);
    connect(anim, &QTimer::timeout, this, [this]()
    {
        if (current_pct < target_pct)
        {
            current_pct += k_fill_per_tick;
            if (current_pct > target_pct) current_pct = target_pct;
            update();
        }
        else if (done && current_pct >= k_pct_done && !emitted)
        {
            emitted = true;
            anim->stop();
            update();
            QTimer::singleShot(800, this, [this]()
            {
                hide();
                emit prefix_complete();
            });
        }
    });

    connect(shell, &core::wine::Shell::setup_status, this, [this](const QString& message)
    {
        status = message;

        if (message.contains("runtime", Qt::CaseInsensitive))
        {
            step = 1;
            target_pct = k_pct_runtime;
        }
        else if (message.contains("prefix", Qt::CaseInsensitive))
        {
            step = 2;
            target_pct = k_pct_wineboot;
        }
        else if (message.contains("components", Qt::CaseInsensitive)
                 || message.contains("DXVK", Qt::CaseInsensitive))
        {
            step = 3;
            target_pct = k_pct_winetricks;
        }
        update();
    });

    connect(shell, &core::wine::Shell::wine_setup_finished, this, [this](bool ok)
    {
        done   = ok;
        failed = !ok;
        if (ok)
        {
            target_pct = k_pct_done;
        }
        else if (status == QStringLiteral("Starting...")
                 || status.contains(QStringLiteral("Validating"), Qt::CaseInsensitive))
        {
            status = QStringLiteral("Wine prefix setup failed.");
        }
        update();
    });
}

void PrefixProgress::setup_buttons()
{
    const QSize w = window()->size();

    close_button = util::simple_utils::make_flat_button(this);
    close_button->setAccessibleName(QStringLiteral("Cancel prefix setup"));
    close_button->setIcon(QIcon(util::assets::images[util::assets::Image::CloseSettings]));
    close_button->setIconSize(dl::close_icon(w));
    close_button->setGeometry(dl::close(w));
    connect(close_button, &QPushButton::clicked, this, [this]()
    {
        if (shell && shell->is_busy())
        {
            const bool confirmed = LauncherDialog::confirm(
                this,
                LauncherDialog::Tone::Warning,
                QStringLiteral("Cancel Prefix Setup"),
                QStringLiteral(
                    "Cancel the current prefix setup? An incomplete prefix may need to be repaired the next time setup runs."),
                QStringLiteral("Cancel Setup"),
                QStringLiteral("Keep Running"),
                true);
            if (!confirmed)
                return;
            shell->cancel_current();
        }
        hide();
        emit closed();
    });
    close_button->raise();

}

void PrefixProgress::showEvent(QShowEvent* event)
{
    ModalOverlay::showEvent(event);
    current_pct = 0.0;
    target_pct  = 0.0;
    step        = 0;
    done        = false;
    failed      = false;
    emitted     = false;
    status      = "Starting...";
    anim->start();
}

void PrefixProgress::hideEvent(QHideEvent* event)
{
    ModalOverlay::hideEvent(event);
    anim->stop();
}

void PrefixProgress::paint_content(QPainter& painter)
{
    const QSize w = window()->size();

    painter.drawPixmap(dl::box_rect(w), util::assets::images[util::assets::Image::BoxDownload]);

    QFont title_font = util::assets::fonts[util::assets::Font::EurostileBlack];
    title_font.setPixelSize(util::layout::scaled(util::layout::text::k_row_title, w));
    title_font.setWeight(QFont::Black);
    painter.setFont(title_font);
    painter.setPen(util::colors::k_text_maroon);
    painter.drawText(dl::title(w), Qt::AlignCenter,
                     util::i18n::translate("INSTALLING WINE PREFIX"));

    QFont label_font = util::assets::fonts[util::assets::Font::Inter];
    label_font.setPixelSize(util::layout::scaled(util::layout::text::k_body, w));
    label_font.setWeight(QFont::Medium);
    painter.setFont(label_font);
    painter.setPen(util::colors::k_text_label);

    const QRect info = dl::info_row(w);
    const QString step_text = step > 0
        ? util::i18n::translate("Step %1 of 3").arg(step)
        : QString();
    const int step_width = step_text.isEmpty()
        ? 0
        : painter.fontMetrics().horizontalAdvance(step_text) + util::layout::scaled(12, w);
    const QRect status_rect = info.adjusted(0, 0, -step_width, 0);
    const QString status_text = painter.fontMetrics().elidedText(
        util::i18n::translate(status), Qt::ElideRight, qMax(1, status_rect.width()));
    painter.drawText(status_rect, Qt::AlignLeft | Qt::AlignVCenter, status_text);
    if (!step_text.isEmpty())
        painter.drawText(info, Qt::AlignRight | Qt::AlignVCenter, step_text);

    util::progress_bar::draw(painter, dl::bar_rect(w), current_pct / 100.0);

    QFont pct_font = util::assets::fonts[util::assets::Font::Inter];
    pct_font.setPixelSize(util::layout::scaled(util::layout::text::k_label, w));
    pct_font.setWeight(QFont::DemiBold);
    painter.setFont(pct_font);
    painter.setPen(failed ? util::colors::k_warning : util::colors::k_text_maroon);
    painter.drawText(dl::under_row(w), Qt::AlignCenter,
                     failed ? util::i18n::translate("FAILED")
                            : QString("%1%").arg(int(current_pct)));
}
