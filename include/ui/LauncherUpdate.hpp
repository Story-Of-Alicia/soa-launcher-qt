#pragma once

#include <QEvent>
#include <QString>
#include <QStringList>

#include "ui/ModalOverlay.hpp"

class QLabel;
class QPixmap;
class QPushButton;
class QComboBox;

class LauncherUpdate final : public util::modal_overlay::ModalOverlay
{
    Q_OBJECT

public:
    explicit LauncherUpdate(QWidget* parent = nullptr);

    void set_release(const QString& version, bool required, const QString& message);
    void set_versions(const QString& current_version, const QStringList& versions,
                      bool catalogue_visible);
    void set_downloading(bool downloading);
    void set_progress(qint64 received, qint64 total);
    void set_starting_installer();
    [[nodiscard]] bool busy() const { return downloading_update || starting_installer; }

signals:
    void update_requested();
    void version_selected(const QString& version);
    void postponed();

protected:
    void paint_content(QPainter& painter) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void setup_controls();
    void retranslate_content();
    void refresh_layout();
    void set_update_button_text(const QString& source);
    void set_button_pixmap(const QPixmap& pixmap);

    QLabel* title_label {};
    QLabel* message_label {};
    QLabel* details_label {};
    QLabel* progress_label {};
    QLabel* update_button_label {};
    QPushButton* update_button {};
    QPushButton* cancel_button {};
    QPushButton* close_button {};
    QComboBox* version_combo {};
    QString release_version;
    QString current_version;
    QString release_message;
    QString update_button_source {QStringLiteral("UPDATE NOW")};
    bool required_update {};
    bool downloading_update {};
    bool starting_installer {};
    bool catalogue_mode {};
    double progress_fraction {};
};
