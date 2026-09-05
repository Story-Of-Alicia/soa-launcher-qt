#pragma once

#include <QWidget>

class QCloseEvent;

#include "common/GameVersion.hpp"
#include "ui/Stage.hpp"
#include "ui/View.hpp"

namespace util::modal_overlay
{
    class ModalOverlay;
}
namespace core::wine
{
    class Shell;
}
namespace core::state
{
    class InstallState;
}
namespace core::integrity
{
    class GameIntegrityWatcher;
}
namespace core::update
{
    class LauncherUpdateManager;
}
namespace core::discord
{
    class DiscordRpc;
}

class QLabel;
class QPushButton;
class AliciaChooser;
class PrerequisitesIntro;
class RulesAgreement;
class RepairFiles;
class Settings;
class GameInstall;
class DownloadProgress;
class WineSelectMenu;
class WineInstall;
class LauncherUpdate;
class AuthHandler;
class LauncherMenuController;
class SystemTrayController;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void open_launcher_settings();
    [[nodiscard]] bool has_system_tray() const;

    [[nodiscard]] AuthHandler* auth_handler() const
    {
        return auth;
    }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void setup_window_buttons();
    void setup_system_tray();
    void setup_launcher_menu();
    void setup_discord_rpc();
    void setup_version_label();
    void setup_settings();
    void setup_prerequisites();
    void setup_rules();
    void setup_repair_files();
    void setup_integrity_watcher();
    void setup_alicia_chooser();
    void setup_game_selector();
    void setup_wine_install();
    void setup_game_install();
    void setup_wine_select();
    void setup_launcher_updates();
    void continue_after_launcher_update_check();
    void set_game_version(core::game::GameVersion version);
    void refresh_game_selector();
    void set_game_switching_enabled(core::state::Stage stage);
    void on_overlay_opened(util::modal_overlay::ModalOverlay* overlay);
    void on_overlay_closed(util::modal_overlay::ModalOverlay* overlay);
    void update_chrome_visibility();
    void open_overlay(util::modal_overlay::ModalOverlay* overlay);
    void close_overlay(util::modal_overlay::ModalOverlay* overlay);
    void on_stage_changed(core::state::Stage stage);
    void open_for_current_stage();
    void show_launcher();
    void run_game_directly();
    void refresh_tray_actions();
    void raise_persistent_controls();
    void show_about();
    void show_credits();
    void request_quit();
    void retranslate_dynamic_text();
    [[nodiscard]] bool can_run_game_directly() const;

    bool chrome_hidden {};
    bool repair_active {};
    bool minimized_for_game {};
    bool force_quit_requested {};
    bool launcher_update_check_complete {};
    SystemTrayController* tray_controller {};
    LauncherMenuController* launcher_menu_controller {};
    QLabel* version_art_label {};
    QLabel* version_label {};
    QPushButton* close_button {};
    QPushButton* minimize_button {};
    QPushButton* playtest_button {};
    QPushButton* alicia_2_button {};
    AliciaChooser* alicia_chooser {};
    PrerequisitesIntro* prerequisites_intro {};
    RulesAgreement* rules_agreement {};
    RepairFiles* repair_files {};
    Settings* settings {};
    WineInstall* wine_install {};
    GameInstall* game_install {};
    DownloadProgress* repair_progress {};
    WineSelectMenu* wine_select {};
    LauncherUpdate* launcher_update {};
    core::wine::Shell* shell {};
    AuthHandler* auth {};
    core::state::InstallState* install_state {};
    core::integrity::GameIntegrityWatcher* integrity_watcher {};
    core::update::LauncherUpdateManager* launcher_update_manager {};
    core::discord::DiscordRpc* discord_rpc {};
    core::game::GameVersion game_version {core::game::GameVersion::Playtest};
    core::state::View last_view {core::state::View::Loading};
};
