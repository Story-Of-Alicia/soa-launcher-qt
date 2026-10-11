#pragma once
#include <QDialog>
#include <QVector>

class QPlainTextEdit;
class QComboBox;
class QPushButton;

class LauncherLog : public QDialog
{
    Q_OBJECT
public:
    static LauncherLog* instance();

public slots:
    void append_line(int level, const QString& text);

private:
    explicit LauncherLog(QWidget* parent = nullptr);

    struct Entry { int level; QString text; };
    QVector<Entry> entries;
    int min_level {2};
    bool autoscroll {true};

    QPlainTextEdit* output {};
    QComboBox* verbosity {};
    QPushButton* clear_button {};
    QPushButton* copy_button {};
    QPushButton* autoscroll_button {};

    void rerender();
    void retranslate();
    bool passes(int level) const { return level >= min_level; }
};
