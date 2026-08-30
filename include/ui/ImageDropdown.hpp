#pragma once

#include <QPoint>
#include <QStringList>
#include <QWidget>

class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

class ImageDropdown : public QWidget
{
    Q_OBJECT

public:
    explicit ImageDropdown(QStringList options, QWidget* parent = nullptr);

    void set_index(int i);
    void set_open_upwards(bool value);

signals:
    void changed(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void set_open(bool value);
    void select_relative(int delta);
    QRect closed_rect() const;
    QRect option_rect(int slot) const;

    QStringList items;
    QPoint closed_position;
    int current {};
    bool open {};
    bool opens_upward {};
    int hovered_slot {-1};
};
