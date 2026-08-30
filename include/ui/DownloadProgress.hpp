#pragma once

#include "ui/ModalOverlay.hpp"
#include <QString>

#include "network/Courier.h"
#include "network/DownloadStatus.hpp"

class QPushButton;

class DownloadProgress : public util::modal_overlay::ModalOverlay
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

signals:
    void closed();
    void download_started();
    void download_finished(bool ok);

protected:
    void paint_content(QPainter& painter) override;
    void showEvent(QShowEvent* event) override;

private:
    void setup_buttons();
    void start_download();
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
    core::network::DownloadStatus current;

    QPushButton* close_button {};
    QPushButton* retry_button {};
    QPushButton* details_button {};
};
