#include "ui/DownloadProgress.hpp"
#include "ui/Assets.hpp"
#include "ui/Layout.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/Styles.hpp"
#include "ui/Colors.hpp"
#include "config/Config.hpp"
#include "common/GameVersion.hpp"
#include "ui/ProgressBar.hpp"
#include "ui/LauncherDialog.hpp"
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include "i18n/LanguageManager.hpp"

#include "network/CourierBridge.hpp"
#include "common/Log.hpp"
#include <spdlog/spdlog.h>

namespace dl = soa::ui::layout::progress_modal;
using soa::config::Config;
using soa::network::CourierBridge;
using soa::network::DownloadStatus;
using soa::common::status::State;

DownloadProgress::DownloadProgress(QWidget* parent)
    : DownloadProgress(Mode::Download, parent)
{
}

DownloadProgress::DownloadProgress(const Mode mode_, QWidget* parent)
    : ModalOverlay(parent), mode(mode_)
{
    hide();
    setup_buttons();

    connect(&CourierBridge::instance(), &CourierBridge::download_status, this,
        [this](const DownloadStatus& status)
        {
            if (status.operation_id != active_operation_id)
                return;

            const bool finished = status.base.state == State::Done || status.base.state == State::Failed;
            const bool observed = observing_external_operation;
            current = status;

            const char* operation = mode == Mode::Repair ? "repair" : "download";
            const bool cancelled = status.result == courier_result_cancelled;
            if (status.base.state == State::Done)
                SPDLOG_INFO("{}: finished - {}", operation, status.base.message.toStdString());
            else if (cancelled)
                SPDLOG_INFO("{}: cancelled - {}", operation, status.base.message.toStdString());
            else if (status.base.state == State::Failed)
                SPDLOG_ERROR("{}: failed - {}", operation, status.base.message.toStdString());

            const bool retryable_failure = status.base.state == State::Failed && !cancelled;
            retry_button->setVisible(retryable_failure);
            details_button->setVisible(retryable_failure);
            update();

            if (finished)
            {
                if (!observed)
                    CourierBridge::instance().clear_operation(active_operation_id);
                active_operation_id = 0;
                active_operation_key.clear();
                cancellation_in_progress = false;
                observing_external_operation = false;
                close_button->setEnabled(true);
                close_button->setCursor(Qt::PointingHandCursor);
                if (!observed)
                    emit download_finished(status.base.state == State::Done);
            }
        });

    connect(&Config::instance(), &Config::changed, this, [this]()
    {
        if (active_operation_id == 0 || cancellation_in_progress
            || operation_context_key() == active_operation_key)
        {
            return;
        }

        QTimer::singleShot(0, this, [this]()
        {
            if (active_operation_id == 0 || cancellation_in_progress
                || operation_context_key() == active_operation_key)
            {
                return;
            }

            SPDLOG_WARN("{}: cancelling because the active prefix or game path changed",
                        mode == Mode::Repair ? "repair" : "download");
            const QString reason = QStringLiteral(
                "Cancelled because the Wine prefix or game install settings changed.");
            cancel_active_operation(reason);
            hide();
            emit closed();
        });
    });
}

DownloadProgress::~DownloadProgress()
{
    if (downloader)
    {
        courier_cancel(downloader);
        CourierBridge::instance().clear_operation(active_operation_id);
        courier_destroy(downloader);
        downloader = nullptr;
    }
}

void DownloadProgress::observe_operation(const qulonglong operation_id)
{
    if (operation_id == 0)
        return;

    if (downloader)
    {
        courier_cancel(downloader);
        CourierBridge::instance().clear_operation(active_operation_id);
        courier_destroy(downloader);
        downloader = nullptr;
    }

    active_operation_id = operation_id;
    active_operation_key = operation_context_key();
    cancellation_in_progress = false;
    observing_external_operation = true;

    current = DownloadStatus{};
    current.operation_id = operation_id;
    current.base.state = State::Working;
    current.base.progress = 0.0;
    current.base.message = QStringLiteral("Preparing download...");
    current.phase = courier_phase_preparing;

    retry_button->hide();
    details_button->hide();
    close_button->setEnabled(true);
    close_button->setCursor(Qt::PointingHandCursor);
    update();
}

void DownloadProgress::stop_observing_operation()
{
    if (!observing_external_operation)
        return;

    active_operation_id = 0;
    active_operation_key.clear();
    cancellation_in_progress = false;
    observing_external_operation = false;
    close_button->setEnabled(true);
    close_button->setCursor(Qt::PointingHandCursor);
}

void DownloadProgress::setup_buttons()
{
    const QSize window_size = window()->size();

    close_button = soa::ui::simple_utils::make_flat_button(this);
    close_button->setAccessibleName(mode == Mode::Repair
        ? QStringLiteral("Cancel or close repair")
        : QStringLiteral("Cancel or close download"));
    const QPixmap& close_pixmap = soa::ui::assets::images[soa::ui::assets::Image::CloseNormal];
    QIcon close_icon;
    close_icon.addPixmap(close_pixmap, QIcon::Normal, QIcon::Off);
    close_icon.addPixmap(close_pixmap, QIcon::Disabled, QIcon::Off);
    close_button->setIcon(close_icon);
    close_button->setIconSize(dl::close_icon(window_size));
    close_button->setGeometry(dl::close(window_size));
    connect(close_button, &QPushButton::clicked, this, &DownloadProgress::cancel_download);
    close_button->raise();

    retry_button = new QPushButton(QStringLiteral("RETRY"), this);
    retry_button->setCursor(Qt::PointingHandCursor);
    retry_button->setStyleSheet(soa::ui::styles::primary_button(window_size));
    retry_button->setGeometry(dl::retry_button(window_size));
    retry_button->setAccessibleName(mode == Mode::Repair
        ? QStringLiteral("Retry repair")
        : QStringLiteral("Retry download"));
    retry_button->setVisible(false);
    connect(retry_button, &QPushButton::clicked, this, &DownloadProgress::start_download);
    retry_button->raise();

    details_button = new QPushButton(QStringLiteral("SHOW ERROR"), this);
    details_button->setCursor(Qt::PointingHandCursor);
    details_button->setStyleSheet(soa::ui::styles::neutral_button(window_size));
    details_button->setGeometry(dl::details_button(window_size));
    details_button->setAccessibleName(QStringLiteral("Show full error"));
    details_button->setVisible(false);
    connect(details_button, &QPushButton::clicked, this, [this]()
    {
        if (current.base.message.isEmpty())
            return;
        LauncherDialog::error(
            this,
            mode == Mode::Repair
                ? QStringLiteral("Repair Error Details")
                : QStringLiteral("Download Error Details"),
            current.base.message,
            QStringLiteral("Retry continues from the files that were already verified or downloaded."));
    });
    details_button->raise();
}

void DownloadProgress::cancel_download()
{
    if (observing_external_operation)
    {
        const qulonglong operation_id = active_operation_id;
        stop_observing_operation();
        emit external_cancel_requested(operation_id);
        hide();
        emit closed();
        return;
    }

    if (current.base.state == State::Working && current.base.progress > 0.0)
    {
        const bool confirmed = LauncherDialog::confirm(
            this,
            LauncherDialog::Tone::Warning,
            mode == Mode::Repair
                ? QStringLiteral("Cancel Repair")
                : QStringLiteral("Cancel Download"),
            mode == Mode::Repair
                ? QStringLiteral("Cancel the current repair? Verified and partial files will be kept so a later retry can continue.")
                : QStringLiteral("Cancel the current game download? Verified and partial files will be kept so a later retry can continue."),
            mode == Mode::Repair
                ? QStringLiteral("Cancel Repair")
                : QStringLiteral("Cancel Download"),
            QStringLiteral("Keep Running"),
            true);
        if (!confirmed)
            return;
    }

    cancel_active_operation(QStringLiteral("Cancelled."));
    hide();
    emit closed();
}

void DownloadProgress::cancel_active_operation(const QString& message)
{
    if (active_operation_id == 0 || cancellation_in_progress)
        return;

    cancellation_in_progress = true;
    const qulonglong operation_id = active_operation_id;

    if (downloader)
        courier_cancel(downloader);

    DownloadStatus cancelled;
    cancelled.operation_id = operation_id;
    cancelled.result = courier_result_cancelled;
    cancelled.base.state = State::Failed;
    cancelled.base.message = message;
    cancelled.base.progress = -1.0;
    CourierBridge::instance().report(cancelled);

    if (active_operation_id == operation_id)
    {
        CourierBridge::instance().clear_operation(operation_id);
        active_operation_id = 0;
        active_operation_key.clear();
        current = cancelled;
        emit download_finished(false);
    }

    cancellation_in_progress = false;
}

void DownloadProgress::set_terminal_error(const QString& message)
{
    CourierBridge::instance().clear_operation(active_operation_id);
    active_operation_id = 0;
    active_operation_key.clear();
    cancellation_in_progress = false;
    current = DownloadStatus{};
    current.base.state = State::Failed;
    current.base.message = message;
    current.base.progress = -1.0;
    retry_button->show();
    details_button->show();
    update();
    emit download_finished(false);
}

void DownloadProgress::start_download()
{
    if (active_operation_id != 0)
        return;

    observing_external_operation = false;
    close_button->setEnabled(true);
    close_button->setCursor(Qt::PointingHandCursor);

    auto& config = Config::instance();
    const auto version = config.game_version();
    const auto& game = soa::common::game::profile(version);
    const QString install = config.game_install_path();

    if (!config.path_inside_prefix(install))
    {
        const QString error = QStringLiteral(
            "The selected game folder is outside the Wine prefix. Choose a safe install folder first.");
        set_terminal_error(error);
        return;
    }

    retry_button->hide();
    details_button->hide();
    current = DownloadStatus{};
    current.base.state = State::Working;
    current.base.message = mode == Mode::Repair
        ? QStringLiteral("Preparing repair...")
        : QStringLiteral("Preparing download...");
    current.base.progress = 0.0;
    update();

    if (downloader)
    {
        courier_cancel(downloader);
        CourierBridge::instance().clear_operation(active_operation_id);
        courier_destroy(downloader);
        downloader = nullptr;
        active_operation_id = 0;
    }

    downloader = courier_create(
        game.cdn_base_url,
        CourierBridge::progress_callback(),
        CourierBridge::done_callback(),
        &CourierBridge::instance());

    if (!downloader)
    {
        SPDLOG_ERROR("download: failed to create courier for game {}",
                     soa::common::game::to_string(version).toStdString());
        set_terminal_error(mode == Mode::Repair
            ? QStringLiteral("The repair service could not be created.")
            : QStringLiteral("The downloader could not be created."));
        return;
    }

    SPDLOG_INFO("{}: starting game {} update from {} to {}",
                mode == Mode::Repair ? "repair" : "download",
                soa::common::game::to_string(version).toStdString(),
                game.cdn_base_url,
                install.toStdString());
    active_operation_id = mode == Mode::Repair
        ? courier_repair(downloader, install.toUtf8().constData())
        : courier_update(downloader, install.toUtf8().constData());
    if (active_operation_id == 0)
    {
        set_terminal_error(mode == Mode::Repair
            ? QStringLiteral("The repair could not be started.")
            : QStringLiteral("The download could not be started."));
        return;
    }
    active_operation_key = operation_context_key();
    CourierBridge::instance().begin_operation(active_operation_id);
    current.operation_id = active_operation_id;
    emit download_started();
    CourierBridge::instance().report(current);
}

QString DownloadProgress::operation_context_key() const
{
    const auto& config = Config::instance();
    const auto version = config.game_version();
    const auto& game = soa::common::game::profile(version);
    return soa::common::game::to_string(version)
        + QLatin1Char('|') + config.prefix_root()
        + QLatin1Char('|') + config.game_install_path()
        + QLatin1Char('|') + QString::fromLatin1(game.cdn_base_url);
}

QString DownloadProgress::human_size(const qulonglong bytes)
{
    constexpr double kb = 1'000.0;
    constexpr double mb = 1'000'000.0;
    constexpr double gb = 1'000'000'000.0;
    if (bytes >= gb)
        return soa::i18n::translate("%1 GB").arg(QString::number(bytes / gb, 'f', 1));
    if (bytes >= mb)
        return soa::i18n::translate("%1 MB").arg(QString::number(bytes / mb, 'f', 1));
    if (bytes >= kb)
        return soa::i18n::translate("%1 KB").arg(QString::number(bytes / kb, 'f', 0));
    return soa::i18n::translate("%1 B").arg(QString::number(bytes));
}

QString DownloadProgress::human_speed(const qulonglong bytes_per_sec)
{
    if (bytes_per_sec == 0) return "--";
    return soa::i18n::translate("%1/s").arg(human_size(bytes_per_sec));
}

QString DownloadProgress::human_eta(const qulonglong remaining, const qulonglong throughput)
{
    if (throughput == 0) return soa::i18n::translate("Estimating...");

    const qulonglong total_seconds = (remaining + throughput - 1) / throughput;
    const qulonglong hours = total_seconds / 3600;
    const qulonglong minutes = (total_seconds % 3600) / 60;
    const qulonglong seconds = total_seconds % 60;

    if (hours > 0)
        return minutes > 0
            ? soa::i18n::translate("%1h %2m").arg(hours).arg(minutes)
            : soa::i18n::translate("%1h").arg(hours);
    if (minutes > 0)
        return seconds > 0
            ? soa::i18n::translate("%1m %2s").arg(minutes).arg(seconds)
            : soa::i18n::translate("%1m").arg(minutes);
    return soa::i18n::translate("%1s").arg(seconds);
}

void DownloadProgress::paint_content(QPainter& painter)
{
    const QSize window_size = window()->size();
    painter.drawPixmap(dl::box_rect(window_size), soa::ui::assets::images[soa::ui::assets::Image::BoxDownload]);

    const bool done = current.base.state == State::Done;
    const bool failed = current.base.state == State::Failed;
    const int files = current.file_count;

    QString title_text;
    if (done)
        title_text = mode == Mode::Repair
            ? soa::i18n::translate("REPAIR COMPLETE")
            : soa::i18n::translate("DOWNLOAD COMPLETE");
    else if (failed)
        title_text = mode == Mode::Repair
            ? soa::i18n::translate("REPAIR FAILED")
            : soa::i18n::translate("DOWNLOAD FAILED");
    else
    {
        switch (current.phase)
        {
            case courier_phase_preparing:
                title_text = mode == Mode::Repair
                    ? soa::i18n::translate("PREPARING REPAIR")
                    : soa::i18n::translate("PREPARING");
                break;
            case courier_phase_checking:
                title_text = files > 0
                    ? soa::i18n::translate("CHECKING FILES (%1/%2)")
                        .arg(current.file_index).arg(files)
                    : soa::i18n::translate("CHECKING FILES");
                break;
            case courier_phase_verifying:
                title_text = files > 0
                    ? soa::i18n::translate("VERIFYING FILES (%1/%2)")
                        .arg(current.file_index).arg(files)
                    : soa::i18n::translate("VERIFYING FILES");
                break;
            case courier_phase_downloading:
            default:
            {
                const bool resuming = current.base.message.startsWith(
                    QStringLiteral("Resuming"), Qt::CaseInsensitive);
                const QString action = mode == Mode::Repair
                    ? (resuming ? soa::i18n::translate("RESUMING REPAIR")
                                : soa::i18n::translate("REPAIRING"))
                    : (resuming ? soa::i18n::translate("RESUMING")
                                : soa::i18n::translate("DOWNLOADING"));
                title_text = files > 0
                    ? soa::i18n::translate("%1 FILES (%2/%3)")
                        .arg(action).arg(current.file_index).arg(files)
                    : action;
                break;
            }
        }
    }

    QFont title_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileBlack];
    title_font.setPixelSize(soa::ui::layout::scaled(soa::ui::layout::text::k_row_title, window_size));
    title_font.setWeight(QFont::Black);
    painter.setFont(title_font);
    painter.setPen(soa::ui::colors::k_text_maroon);
    painter.drawText(dl::title(window_size), Qt::AlignCenter, title_text);

    QFont label_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    label_font.setPixelSize(soa::ui::layout::scaled(soa::ui::layout::text::k_body, window_size));
    label_font.setWeight(QFont::Medium);
    painter.setFont(label_font);
    painter.setPen(failed ? soa::ui::colors::k_warning : soa::ui::colors::k_text_label);

    const QRect info = dl::info_row(window_size);
    const qulonglong remaining = current.total > current.received ? current.total - current.received : 0;
    if (failed)
    {
        const QString message = painter.fontMetrics().elidedText(
            soa::i18n::translate(current.base.message), Qt::ElideRight, info.width());
        painter.drawText(info, Qt::AlignCenter, message);
    }
    else if (done)
    {
        painter.drawText(info, Qt::AlignCenter, soa::i18n::translate(current.base.message));
    }
    else if (current.phase == courier_phase_downloading)
    {
        painter.drawText(info, Qt::AlignLeft | Qt::AlignVCenter,
                         soa::i18n::translate("Time remaining: %1").arg(human_eta(remaining, current.speed)));
        painter.drawText(info, Qt::AlignRight | Qt::AlignVCenter,
                         human_speed(current.speed));
    }
    else if (current.phase == courier_phase_checking
             || current.phase == courier_phase_verifying)
    {
    }
    else
    {
        painter.drawText(info, Qt::AlignCenter, painter.fontMetrics().elidedText(
            soa::i18n::translate(current.base.message), Qt::ElideMiddle, info.width()));
    }

    soa::ui::progress_bar::draw(painter, dl::bar_rect(window_size), current.base.progress);

    QFont percent_font = soa::ui::assets::fonts[soa::ui::assets::Font::NanumExtraBold];
    percent_font.setPixelSize(soa::ui::layout::scaled(soa::ui::layout::text::k_label, window_size));
    percent_font.setWeight(QFont::ExtraBold);
    painter.setFont(percent_font);
    painter.setPen(failed ? soa::ui::colors::k_warning : soa::ui::colors::k_text_maroon);
    const int shown = current.base.progress < 0.0 ? 0 : qRound(current.base.progress * 100.0);
    painter.drawText(dl::under_row(window_size), Qt::AlignCenter,
                     failed ? soa::i18n::translate("Retry continues from saved files")
                            : QString("%1%").arg(qBound(0, shown, 100)));
}
