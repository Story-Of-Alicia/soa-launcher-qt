#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>

class QFrame;
class ImageDropdown;
class QLabel;
class QPushButton;
class QUrl;

namespace core::network
{
    class SwiftHttpClient;
}

class LauncherSettings : public QWidget
{
    Q_OBJECT

public:
    explicit LauncherSettings(QWidget* parent = nullptr);

signals:
    void connectivity_panel_changed(bool expanded);

private:
    void setup_launch_on_startup_option();
    void setup_after_game_start_option();
    void setup_run_connectivity_test_option();
    void setup_launcher_size_option();
    void set_startup_button_state(bool enabled);
    void run_connectivity_check();
    void start_dns_check();
    void start_ping_check();
    void start_http_check(const QString& label, const QUrl& url);
    void record_connectivity_result(const QString& label, bool ok, const QString& detail);
    void refresh_connectivity_report();
    void finish_connectivity_check();
    void set_connectivity_expanded(bool expanded);
    void apply_dynamic_layout();

    QPushButton* startup_button {};
    QPushButton* connectivity_button {};
    QFrame* connectivity_panel {};
    QLabel* connectivity_label {};
    QPushButton* copy_report_button {};
    QLabel* launcher_size_title {};
    QLabel* launcher_size_description {};
    ImageDropdown* launcher_size_dropdown {};
    core::network::SwiftHttpClient* network_manager {};
    QStringList connectivity_order;
    QHash<QString, QString> connectivity_details;
    QHash<QString, bool> connectivity_success;
    QString connectivity_plain_report;
    int pending_connectivity_checks {};
    bool connectivity_expanded {};
};
