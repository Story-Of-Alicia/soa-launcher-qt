#pragma once

#include "ui/ModalOverlay.hpp"
#include <QString>

#include "network/Courier.h"
#include "network/DownloadStatus.hpp"

class QPushButton;

class DownloadProgress : public soa::ui::ModalOverlay
{
    Q_OBJECT

public:
    enum class Mode
    {
        Download,
        Repair
    };

    explicit DownloadProgress(QWidget* parent = nullptr);
    explicit DownloadProgress(Mode mode, QWidget* parent = nullptr);
    ~DownloadProgress() override;

    void observe_operation(qulonglong operation_id);
    void stop_observing_operation();
    void start_download();
    [[nodiscard]] bool observing_operation() const { return observing_external_operation; }

signals:
    void closed();
    void download_started();
    void download_finished(bool ok);
    void external_cancel_requested(qulonglong operation_id);

protected:
    void paint_content(QPainter& painter) override;

private:
    void setup_buttons();
    void cancel_download();
    void cancel_active_operation(const QString& message);
    void set_terminal_error(const QString& message);
    QString operation_context_key() const;

    static QString human_size(qulonglong bytes);
    static QString human_speed(qulonglong bytes_per_sec);
    static QString human_eta(qulonglong remaining, qulonglong throughput);

    Mode mode {Mode::Download};
    courier* downloader {};
    qulonglong active_operation_id {};
    QString active_operation_key;
    bool cancellation_in_progress {};
    bool observing_external_operation {};
    soa::network::DownloadStatus current;

    QPushButton* close_button {};
    QPushButton* retry_button {};
    QPushButton* details_button {};
};
