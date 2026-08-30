#pragma once

#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QPointer>

#include "common/GameVersion.hpp"
#include "ui/Stage.hpp"
#include "ui/Assets.hpp"

class AuthHandler;
class QCheckBox;
class LauncherDialog;
namespace core::wine
{
    class Shell;
}
namespace core::state
{
    class InstallState;
}

class AliciaChooser : public QWidget
{
    Q_OBJECT

public:
    explicit AliciaChooser(AuthHandler* auth, core::wine::Shell* shell,
                           core::state::InstallState* install_state, QWidget* parent = nullptr);

    void set_game_version(core::game::GameVersion version);
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    void settings_requested();
    void download_triggered();
    void reset_config_requested();

private:
    enum class State
    {
        Download,
        Login,
        Waiting,
        SignedIn
    };

    static State state_for(core::state::Stage stage);
    void on_stage_changed(core::state::Stage stage);
    void setup_title();
    void setup_settings_button();
    void setup_download_state();
    void setup_login_state();
    void setup_waiting_state();
    void setup_signedin_state();
    void set_state(State state);
    void apply_state_visibility();
    void refresh_enter_enabled();
    void refresh_acknowledgements();
    void refresh_keep_signed_in();
    void refresh_game_text();
    void refresh_session_banner();
    void set_warning(const QString& message);
    void retranslate_dynamic_text();

    AuthHandler* auth {};
    core::wine::Shell* shell {};
    core::state::InstallState* install_state {};
    core::game::GameVersion game_version {core::game::GameVersion::Playtest};
    State state {State::Download};
    core::state::Stage current_stage {core::state::Stage::Probing};
    QLabel* title_label {};
    QPushButton* settings_button {};
    QPushButton* download_button {};
    QLabel* message_label {};
    QPushButton* discord_button {};
    QCheckBox* keep_signed_button {};
    QLabel* disclaimer_label {};
    QLabel* waiting_title {};
    QLabel* steps_label {};
    QPushButton* try_again_button {};
    QCheckBox* signed_bug_checkbox {};
    QLabel* signed_bug_label {};
    QCheckBox* signed_rules_checkbox {};
    QLabel* signed_rules_label {};
    QLabel* signed_in_label {};
    QPushButton* enter_button {};
    QPushButton* reset_path_button {};
    QPointer<LauncherDialog> warning_dialog;
    QString displayed_warning;
    bool rules_accepted_cached {};
};
