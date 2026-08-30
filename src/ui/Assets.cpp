#include "ui/Assets.hpp"

#include <QFontDatabase>
#include <spdlog/spdlog.h>

#include <initializer_list>
#include <utility>

namespace
{
    bool translated_assets_active {};
}

namespace util::assets
{
    QPixmap load_pixmap(const QString& path)
    {
        QPixmap pixmap(QStringLiteral(":/assets/") + path);
        if (pixmap.isNull())
            spdlog::warn("Failed to load image: {}", path.toStdString());
        return pixmap;
    }

    void load_images()
    {
        const std::initializer_list<std::pair<Image, QString>> definitions =
        {
            {Image::BackgroundPlaytest, "BG1.0.png"},
            {Image::BackgroundAlicia2, "BG2.0.png"},
            {Image::BoxCard, "box-card.png"},
            {Image::BoxDownload, "box-download.png"},
            {Image::BoxGameInstall, "box-game-install.png"},
            {Image::BoxModal, "box-modal.png"},
            {Image::BoxNote, "box-note.png"},
            {Image::BoxNote2, "box-note2.png"},
            {Image::BoxSettings, "box-settings.png"},
            {Image::BoxUpdate, "box-update.png"},
            {Image::BoxWaitingForAuth, "box-waiting-for-auth.png"},
            {Image::CloseIcon, "close-icon2.png"},
            {Image::CloseNormal, "close-normal.png"},
            {Image::CloseSettings, "close-settings.png"},
            {Image::InstallPath, "install-path.png"},
            {Image::IntegrityCheckFile, "integrity-check-file.png"},
            {Image::LeftFrame, "left-frame.png"},
            {Image::MenuDropdown, "menu-dopdown.png"},
            {Image::Minimize, "Minimize.png"},
            {Image::ProgressBarEnd, "progress-bar-end.png"},
            {Image::ProgressBarMiddle, "progress-bar-middle.png"},
            {Image::ProgressBarStart, "progress-bar-start.png"},
            {Image::ProgressBarTrack, "progress-bar-track.png"},
            {Image::RightFrame, "right-frame.png"},
            {Image::RulesFrame, "rules-frame.png"},
            {Image::VersionFrameActive, "ver-frame-active.png"},
            {Image::VersionFrameInactive, "ver-frame-inactive-aligned.png"},
            {Image::VersionIconPlaytest, "ver-icon-pt.png"},
            {Image::VersionIconAlicia2, "ver-icon-2.0.png"},
            {Image::VersionIconKatsu, "soa-katsu-version-icon.png"},
            {Image::SoaLogo, "soa-logo.png"},
            {Image::SettingsButton, "Settings Button.png"}
        };

        for (const auto& [key, path] : definitions)
        {
            if (QPixmap pixmap = load_pixmap(path); !pixmap.isNull())
                images[key] = pixmap;
        }
    }

    void load_buttons()
    {
        struct ButtonDefinition
        {
            Button key;
            QString normal;
            QString hover;
            QString clicked;
            QString loading;
            QString translated_normal;
            QString translated_hover;
            QString translated_clicked;
            QString translated_loading;
        };

        const std::initializer_list<ButtonDefinition> definitions =
        {
            {Button::Agree, "agree_normal.png", "agree_hover.png", "agree_clicked.png", "agree-loading.png", "agree_normal-blank.png", "agree_hover-blank.png", "agree_clicked-blank.png", "agree-loading-blank.png"},
            {Button::Enter, "enter_normal.png", "enter_hover.png", "enter_clicked.png", "enter_loading.png", "enter_normal-blank.png", "enter_hover-blank.png", "enter_clicked-blank.png", "enter_loading-blank.png"},
            {Button::Cancel, "btn-cancel-normal.png", "btn-cancel-hover.png", "btn-cancel-clicked.png", "", "btn-cancel-normal-blank.png", "btn-cancel-hover-blank.png", "btn-cancel-clicked-blank.png", ""},
            {Button::Discord, "btn-discord-normal.png", "btn-discord-hover.png", "btn-discord-clicked.png", "btn-discord-loading.png", "btn-discord-normal-blank.png", "btn-discord-hover-blank.png", "btn-discord-clicked-blank.png", "btn-discord-loading-blank.png"},
            {Button::DownloadGame, "btn-download-game-normal.png", "btn-download-game-hover.png", "btn-download-game-clicked.png", "btn-download-game-loading.png", "btn-download-game-normal-blank.png", "btn-download-game-hover-blank.png", "btn-download-game-clicked-blank.png", "btn-download-game-loading-blank.png"},
            {Button::Install, "btn-install-normal.png", "btn-install-hover.png", "btn-install-clicked.png", "btn-install-loading.png", "btn-install-normal-blank.png", "btn-install-hover-blank.png", "btn-install-clicked-blank.png", "btn-install-loading-blank.png"},
            {Button::RunCheck, "btn-run-check-normal.png", "btn-run-check-hover.png", "btn-run-check-clicked.png", "btn-run-check-loading.png", "btn-run-check-normal-blank.png", "btn-run-check-hover-blank.png", "btn-run-check-clicked-blank.png", "btn-run-check-loading-blank.png"},
            {Button::UpdateAvailable, "btn-update-available.png", "btn-update-available-hover.png", "btn-update-available-clicked.png", "", "btn-update-available-blank.png", "btn-update-available-hover-blank.png", "btn-update-available-clicked-blank.png", ""},
            {Button::Repair, "btn-repair.png", "btn-repair-hover.png", "btn-repair-clicked.png", "", "btn-repair-blank.png", "btn-repair-hover-blank.png", "btn-repair-clicked-blank.png", ""},
            {Button::SliderOn, "slider-toggle-on.png", "", "", "", "slider-toggle-on.png", "", "", ""},
            {Button::SliderOff, "slider-toggle-off.png", "", "", "", "slider-toggle-off.png", "", "", ""}
        };

        const auto load_asset = [](const QString& normal, const QString& hover,
                                   const QString& clicked, const QString& loading)
        {
            ButtonAsset asset;
            asset.normal = normal.isEmpty() ? QPixmap{} : load_pixmap(normal);
            asset.hover = hover.isEmpty() ? QPixmap{} : load_pixmap(hover);
            asset.clicked = clicked.isEmpty() ? QPixmap{} : load_pixmap(clicked);
            asset.loading = loading.isEmpty() ? QPixmap{} : load_pixmap(loading);
            return asset;
        };

        for (const ButtonDefinition& definition : definitions)
        {
            english_buttons[definition.key] = load_asset(
                definition.normal, definition.hover, definition.clicked, definition.loading);
            translated_buttons[definition.key] = load_asset(
                definition.translated_normal, definition.translated_hover,
                definition.translated_clicked, definition.translated_loading);
        }
    }

    void load_fonts()
    {
        const std::initializer_list<std::pair<Font, QString>> definitions =
        {
            {Font::EurostileBold, "fonts/Eurostile-Bold.otf"},
            {Font::EurostileBlack, "fonts/Eurostile-Black.otf"},
            {Font::EurostileExtraBlack, "fonts/Eurostile-ExtraBlack.otf"},
            {Font::Inter, "fonts/InterVariable.ttf"},
            {Font::NanumExtraBold, "fonts/NanumGothic-ExtraBold.ttf"}
        };

        for (const auto& [key, path] : definitions)
        {
            const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/assets/") + path);
            if (id == -1)
            {
                spdlog::warn("Failed to load font: {}", path.toStdString());
                continue;
            }

            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (families.isEmpty())
            {
                spdlog::warn("No font families found in: {}", path.toStdString());
                continue;
            }
            fonts[key] = QFont(families.first());
        }
    }

    void load_all()
    {
        load_images();
        load_buttons();
        load_fonts();

        for (int value = 0; value < static_cast<int>(Image::Count); ++value)
        {
            const auto key = static_cast<Image>(value);
            if (!images.contains(key))
            {
                spdlog::error("Required image asset {} was not loaded", value);
                images.emplace(key, QPixmap{});
            }
        }

        for (int value = 0; value < static_cast<int>(Button::Count); ++value)
        {
            const auto key = static_cast<Button>(value);
            if (!english_buttons.contains(key))
            {
                spdlog::error("Required English button asset {} was not loaded", value);
                english_buttons.emplace(key, ButtonAsset{});
            }
            if (!translated_buttons.contains(key))
            {
                spdlog::error("Required translated button asset {} was not loaded", value);
                translated_buttons.emplace(key, ButtonAsset{});
            }
        }

        for (int value = 0; value < static_cast<int>(Font::Count); ++value)
        {
            const auto key = static_cast<Font>(value);
            if (!fonts.contains(key))
            {
                spdlog::error("Required font asset {} was not loaded; using the system font", value);
                fonts.emplace(key, QFont{});
            }
        }
    }

    void set_translated_button_assets(const bool translated)
    {
        translated_assets_active = translated;
    }

    bool translated_button_assets_active()
    {
        return translated_assets_active;
    }

    const ButtonAsset& button(const Button key)
    {
        auto& assets = translated_assets_active ? translated_buttons : english_buttons;
        return assets[key];
    }
}
