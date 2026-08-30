#pragma once

#include <QDialog>
#include <QPoint>
#include <QString>
#include <QVector>

class QFrame;
class QLabel;
class QMouseEvent;
class QPushButton;

class LauncherDialog final : public QDialog
{
public:
    enum class Tone
    {
        Information,
        Warning,
        Error,
        Question
    };

    enum class ActionStyle
    {
        Primary,
        Neutral,
        Destructive
    };

    struct Action
    {
        QString text;
        int result {};
        ActionStyle style {ActionStyle::Neutral};
        bool is_default {};
    };

    static constexpr int Cancelled = QDialog::Rejected;
    static constexpr int Primary = QDialog::Accepted;
    static constexpr int Secondary = 2;
    static constexpr int Tertiary = 3;

    LauncherDialog(Tone tone, const QString& title, const QString& message,
                   const QString& details = {}, QWidget* parent = nullptr);

    QPushButton* add_action(const Action& action);
    void set_actions(const QVector<Action>& actions);

    static LauncherDialog* open_message(QWidget* parent, Tone tone,
                                        const QString& title, const QString& message,
                                        const QString& details = {});
    static void information(QWidget* parent, const QString& title,
                            const QString& message, const QString& details = {});
    static void warning(QWidget* parent, const QString& title,
                        const QString& message, const QString& details = {});
    static void error(QWidget* parent, const QString& title,
                      const QString& message, const QString& details = {});
    static bool confirm(QWidget* parent, Tone tone, const QString& title,
                        const QString& message,
                        const QString& accept_text = QStringLiteral("Continue"),
                        const QString& cancel_text = QStringLiteral("Cancel"),
                        bool destructive = false);
    static int choose(QWidget* parent, Tone tone, const QString& title,
                      const QString& message, const QVector<Action>& actions,
                      const QString& details = {});

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void build_ui();
    void retranslate();
    QString tone_label() const;
    QString tone_symbol() const;

    Tone tone;
    QString title_source;
    QString message_source;
    QString details_source;
    QFrame* panel {};
    QLabel* tone_badge {};
    QLabel* title_label {};
    QLabel* message_label {};
    QLabel* details_label {};
    QFrame* details_card {};
    QPushButton* close_button {};
    QWidget* action_container {};
    QVector<QPushButton*> action_buttons;
    bool dragging {};
    QPoint drag_offset;
};
