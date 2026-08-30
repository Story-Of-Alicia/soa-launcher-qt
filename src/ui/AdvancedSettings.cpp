#include "ui/AdvancedSettings.hpp"
#include "ui/ImageDropdown.hpp"
#include "ui/Assets.hpp"
#include "ui/Layout.hpp"
#include "i18n/LanguageManager.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/Styles.hpp"
#include "config/Config.hpp"
#include "common/LaunchArguments.hpp"
#include "ui/LauncherDialog.hpp"
#include <QIcon>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
using util::config::Config;
namespace ls = util::layout::settings;
namespace aset = util::layout::advanced_settings;

namespace
{
    constexpr int kAdvancedRowCount = 4;
    constexpr const char* kDiagnosticTitle =
        QT_TRANSLATE_NOOP("Launcher", "DIAGNOSTIC MODE");
    constexpr const char* kDiagnosticLinuxDescription =
        QT_TRANSLATE_NOOP(
            "Launcher",
            "Creates labeled Alicia and Wine logs, a launch timeline and summary. Leave off for normal play.");
    constexpr const char* kDiagnosticMacDescription =
        QT_TRANSLATE_NOOP(
            "Launcher",
            "Creates labeled Alicia and Wine logs, a launch timeline, summary and host sample. Leave off for normal play.");
    constexpr const char* kDiagnosticAccessibleName =
        QT_TRANSLATE_NOOP("Launcher", "Diagnostic mode");
    constexpr const char* kDiagnosticAccessibleDescription =
        QT_TRANSLATE_NOOP("Launcher", "Create detailed files for each game launch");

}

AdvancedSettings::AdvancedSettings(QWidget* parent) : QWidget(parent)
{
    setup_game_path_option();
    setup_game_args_option();
#if defined(Q_OS_MACOS)
    setup_macos_compatibility_option();
#else
    setup_umu_runner_option();
#endif
    setup_diagnostics_option();
}
void AdvancedSettings::setup_game_path_option()
{
    const QSize w = window()->size();
    const int y = aset::row(0, kAdvancedRowCount);
    util::simple_utils::make_label_block(this, w, y,
                            "GAME INSTALL PATH",
                            "Where the game is installed. Leave blank to use the default location.");
    game_path_field = new QLineEdit(this);
    game_path_field->setText(Config::instance().game_install_path());
    game_path_field->setStyleSheet(util::styles::field(w));
    game_path_field->setGeometry(ls::field_rect(w, y));

    auto* browse = new QPushButton("...", this);
    browse->setCursor(Qt::PointingHandCursor);
    browse->setStyleSheet(util::styles::neutral_button(w));
    browse->setGeometry(ls::browse_rect(w, y));
    browse->setAccessibleName(QStringLiteral("Choose game installation folder"));
    connect(browse, &QPushButton::clicked, this, [this]()
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this,
            util::i18n::translate("Select Game Folder"),
            Config::instance().prefix_root());
        if (!dir.isEmpty())
        {
            if (!Config::instance().path_inside_prefix(dir))
            {
                const QString message = QStringLiteral(
                    "The game folder must remain inside the selected Wine prefix.");
                LauncherDialog::warning(
                    this,
                    QStringLiteral("Invalid Game Folder"),
                    message);
                return;
            }
            game_path_field->setText(dir);
            Config::instance().set_game_install_path(dir);
        }
    });
    connect(game_path_field, &QLineEdit::editingFinished, this, [this]()
    {
        const QString candidate = game_path_field->text().trimmed();
        if (candidate.isEmpty())
        {
            Config::instance().forget_game_install_path();
            game_path_field->setText(Config::instance().game_install_path());
            return;
        }
        if (!Config::instance().path_inside_prefix(candidate))
        {
            const QString message = QStringLiteral(
                "The game folder must remain inside the selected Wine prefix.");
            LauncherDialog::warning(
                this,
                QStringLiteral("Invalid Game Folder"),
                message);
            game_path_field->setText(Config::instance().game_install_path());
            return;
        }
        if (candidate != Config::instance().game_install_path())
            Config::instance().set_game_install_path(candidate);
    });
    connect(&Config::instance(), &Config::changed, game_path_field, [this]()
    {
        if (!game_path_field->hasFocus())
            game_path_field->setText(Config::instance().game_install_path());
    });
}
void AdvancedSettings::setup_game_args_option()
{
    const QSize w = window()->size();
    const int y = aset::row(1, kAdvancedRowCount);
    util::simple_utils::make_label_block(this, w, y,
                            "GAME LAUNCH ARGUMENTS",
                            "Passed to Alicia.exe. Optional for most players.");
    auto* field = new QLineEdit(this);
    field->setPlaceholderText(util::i18n::translate("%1 (default)").arg(QStringLiteral("Alicia.exe")));
    field->setAccessibleName(QStringLiteral("Game launch arguments"));
    field->setStyleSheet(util::styles::field(w));
    field->setGeometry(ls::ctrl_pos(w, y).x(), ls::ctrl_pos(w, y).y(),
                       ls::ctrl_w(w), util::layout::scaled(34, w));
    field->setText(Config::instance().game_args());
    connect(field, &QLineEdit::editingFinished, this, [this, field]()
    {
        const auto validation = util::launch_arguments::validate(field->text());
        if (!validation.valid)
        {
            LauncherDialog::warning(
                this,
                QStringLiteral("Invalid Launch Arguments"),
                validation.error);
            field->setText(Config::instance().game_args());
            return;
        }
        Config::instance().set_game_args(field->text());
    });
}

void AdvancedSettings::setup_macos_compatibility_option()
{
#if defined(Q_OS_MACOS)
    const QSize w = window()->size();
    const int y = aset::row(2, kAdvancedRowCount);
    util::simple_utils::make_label_block(
        this, w, y,
        "COMPATIBILITY PROFILE",
        "Normal is recommended. Fallback profiles isolate targeted graphics or audio behavior.");

    const QStringList labels {
        QStringLiteral("Normal (recommended)"),
        QStringLiteral("Safe display"),
        QStringLiteral("Low graphics"),
        QStringLiteral("Mac GL fallback"),
        QStringLiteral("Audio isolation (diagnostic)")
    };
    const QStringList ids {
        QStringLiteral("default"),
        QStringLiteral("safe-display"),
        QStringLiteral("low-graphics"),
        QStringLiteral("gl-behind"),
        QStringLiteral("audio-isolation")
    };

    auto* dropdown = new ImageDropdown(labels, this);
    dropdown->setAccessibleName(QStringLiteral("macOS compatibility profile"));
    dropdown->setAccessibleDescription(
        QStringLiteral("Select a targeted Wine compatibility profile"));
    dropdown->move(ls::ctrl_pos(w, y));
    const int current = ids.indexOf(Config::instance().macos_compatibility_profile());
    dropdown->set_index(current >= 0 ? current : 0);

    connect(dropdown, &ImageDropdown::changed, this, [ids](const int index)
    {
        if (index >= 0 && index < ids.size())
            Config::instance().set_macos_compatibility_profile(ids[index]);
    });
    connect(&Config::instance(), &Config::changed, dropdown, [dropdown, ids]()
    {
        const int index = ids.indexOf(
            Config::instance().macos_compatibility_profile());
        if (index >= 0)
            dropdown->set_index(index);
    });
#endif
}

void AdvancedSettings::setup_diagnostics_option()
{
    const QSize w = window()->size();
    const int y = aset::row(3, kAdvancedRowCount);

    util::simple_utils::make_label_block(
        this, w, y, QString::fromUtf8(kDiagnosticTitle),
#if defined(Q_OS_MACOS)
        QString::fromUtf8(kDiagnosticMacDescription));
#else
        QString::fromUtf8(kDiagnosticLinuxDescription));
#endif

    auto* diagnostic_slider = util::simple_utils::make_flat_button(this);
    const QRect geometry = ls::slider_rect(w, y);
    diagnostic_slider->setGeometry(geometry);
    diagnostic_slider->setIconSize(geometry.size());
    diagnostic_slider->setAccessibleName(
        util::i18n::translate(QString::fromUtf8(kDiagnosticAccessibleName)));
    diagnostic_slider->setAccessibleDescription(
        util::i18n::translate(QString::fromUtf8(kDiagnosticAccessibleDescription)));
    diagnostic_slider->setProperty(
        "soa_i18n_accessible_name_source", QString::fromUtf8(kDiagnosticAccessibleName));
    diagnostic_slider->setProperty(
        "soa_i18n_accessible_description_source",
        QString::fromUtf8(kDiagnosticAccessibleDescription));

    const auto paint = [slider = diagnostic_slider,
                        size = geometry.size()](const bool enabled)
    {
        const auto& asset = enabled
            ? util::assets::button(util::assets::Button::SliderOn)
            : util::assets::button(util::assets::Button::SliderOff);
        slider->setIcon(QIcon(asset.normal.scaled(
            size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    };
    paint(Config::instance().diagnostics_enabled());
    connect(diagnostic_slider, &QPushButton::clicked, this, [paint]()
    {
        Config::instance().set_diagnostics_enabled(
            !Config::instance().diagnostics_enabled());
        paint(Config::instance().diagnostics_enabled());
    });
    connect(&Config::instance(), &Config::changed, diagnostic_slider, [paint]()
    {
        paint(Config::instance().diagnostics_enabled());
    });
}


void AdvancedSettings::setup_umu_runner_option()
{
#if !defined(Q_OS_MACOS)
    const QSize w = window()->size();
    const int y = aset::row(2, kAdvancedRowCount);
    util::simple_utils::make_label_block(
        this, w, y,
        "UMU-RUNNER EXECUTABLE",
        "Optional custom umu-run executable used when launching Proton.");

    umu_path_field = new QLineEdit(this);
    umu_path_field->setText(Config::instance().umu_binary());
    umu_path_field->setPlaceholderText(util::i18n::translate("Auto-detect umu-run"));
    umu_path_field->setProperty("soa_i18n_placeholder_source", QStringLiteral("Auto-detect umu-run"));
    umu_path_field->setAccessibleName(QStringLiteral("Custom umu-run executable"));
    umu_path_field->setStyleSheet(util::styles::field(w));
    umu_path_field->setGeometry(ls::field_rect(w, y));

    auto* browse = new QPushButton("...", this);
    browse->setCursor(Qt::PointingHandCursor);
    browse->setStyleSheet(util::styles::neutral_button(w));
    browse->setGeometry(ls::browse_rect(w, y));
    browse->setAccessibleName(QStringLiteral("Choose custom umu-run executable"));

    const auto accept_path = [this](const QString& path)
    {
        const QString candidate = path.trimmed();
        if (candidate.isEmpty())
        {
            Config::instance().set_umu_binary(QString());
            umu_path_field->clear();
            return true;
        }

        const QFileInfo info(candidate);
        if (!info.isFile() || !info.isExecutable())
        {
            LauncherDialog::warning(
                this,
                QStringLiteral("Invalid UMU-Runner Executable"),
                QStringLiteral("Select an existing executable umu-run file."));
            umu_path_field->setText(Config::instance().umu_binary());
            return false;
        }

        const QString absolute = info.absoluteFilePath();
        umu_path_field->setText(absolute);
        Config::instance().set_umu_binary(absolute);
        return true;
    };

    connect(browse, &QPushButton::clicked, this, [this, accept_path]()
    {
        QString start = Config::instance().umu_binary();
        if (start.isEmpty())
            start = QDir::homePath();
        else
            start = QFileInfo(start).absolutePath();

        const QString path = QFileDialog::getOpenFileName(
            this,
            util::i18n::translate("Select UMU-Runner Executable"),
            start);
        if (!path.isEmpty())
            accept_path(path);
    });

    connect(umu_path_field, &QLineEdit::editingFinished, this, [this, accept_path]()
    {
        accept_path(umu_path_field->text());
    });

    connect(&Config::instance(), &Config::changed, umu_path_field, [this]()
    {
        if (!umu_path_field->hasFocus())
            umu_path_field->setText(Config::instance().umu_binary());
    });
#endif
}
