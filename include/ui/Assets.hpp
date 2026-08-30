#pragma once

#include <QFont>
#include <QPixmap>
#include <unordered_map>

namespace util::assets
{
    enum class Image
    {
        BackgroundPlaytest,
        BackgroundAlicia2,
        BoxCard,
        BoxDownload,
        BoxGameInstall,
        BoxModal,
        BoxNote,
        BoxNote2,
        BoxSettings,
        BoxUpdate,
        BoxWaitingForAuth,
        CloseIcon,
        CloseNormal,
        CloseSettings,
        InstallPath,
        IntegrityCheckFile,
        LeftFrame,
        MenuDropdown,
        Minimize,
        ProgressBarEnd,
        ProgressBarMiddle,
        ProgressBarStart,
        ProgressBarTrack,
        RightFrame,
        RulesFrame,
        VersionFrameActive,
        VersionFrameInactive,
        VersionIconPlaytest,
        VersionIconAlicia2,
        VersionIconKatsu,
        SoaLogo,
        SettingsButton,
        Count
    };

    enum class Button
    {
        Agree,
        Enter,
        Cancel,
        Discord,
        DownloadGame,
        Install,
        RunCheck,
        UpdateAvailable,
        Repair,
        SliderOn,
        SliderOff,
        Count
    };

    enum class Font
    {
        EurostileBold,
        EurostileBlack,
        EurostileExtraBlack,
        Inter,
        NanumExtraBold,
        Count
    };

    struct ButtonAsset
    {
        QPixmap normal;
        QPixmap hover;
        QPixmap clicked;
        QPixmap loading;
    };

    void load_fonts();
    void load_buttons();
    void load_images();
    void load_all();
    void set_translated_button_assets(bool translated);
    [[nodiscard]] bool translated_button_assets_active();
    [[nodiscard]] const ButtonAsset& button(Button key);

    inline std::unordered_map<Image, QPixmap> images;
    inline std::unordered_map<Button, ButtonAsset> english_buttons;
    inline std::unordered_map<Button, ButtonAsset> translated_buttons;
    inline std::unordered_map<Font, QFont> fonts;
}
