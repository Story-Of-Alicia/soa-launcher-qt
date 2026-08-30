#pragma once
#include <QFutureWatcher>
#include <QVector>
#include "ui/ModalOverlay.hpp"
#include "runtime/WineRegistry.hpp"

class QScrollArea;
class QLabel;
class QPushButton;
class QAbstractButton;
class QShowEvent;

class WineSelectMenu : public util::modal_overlay::ModalOverlay
{
    Q_OBJECT
public:
    explicit WineSelectMenu(QWidget* parent = nullptr);
signals:
    void runtime_chosen();
protected:
    void paint_content(QPainter& painter) override;
    void showEvent(QShowEvent* event) override;
private:
    void build_ui();
    void populate();
    void start_scan();
    void finish_scan();
    void rescan();
    void browse_runtime();
    void request_rosetta();
    void relayout();
    void select_row(int index);
    void confirm();
    void retranslate_dynamic_text();

    QVector<core::wine::WineInstall> runtimes;
    QFutureWatcher<QVector<core::wine::WineInstall>>* detector {};
    bool scanning {};
    int selected {-1};
    QLabel* runtime_status {};
    QScrollArea* list {};
    QVector<QAbstractButton*> rows;
    QPushButton* close_button {};
    QAbstractButton* rescan_button {};
    QAbstractButton* browse_button {};
    QAbstractButton* rosetta_button {};
    QAbstractButton* continue_button {};
};
