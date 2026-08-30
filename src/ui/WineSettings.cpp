#include "ui/WineSettings.hpp"
#include "ui/LauncherDialog.hpp"
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <functional>

#include "runtime/Shell.hpp"
#include "runtime/WineRegistry.hpp"
#include "ui/Assets.hpp"
#include "config/Config.hpp"
#include "ui/Layout.hpp"
#include "i18n/LanguageManager.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/Styles.hpp"

using util::config::Config;
namespace ls = util::layout::settings;
namespace wset = util::layout::wine_settings;
namespace cw = core::wine;

namespace
{
    void sync_field(QLineEdit* field, const std::function<QString()>& getter)
    {
        QObject::connect(&Config::instance(), &Config::changed, field, [field, getter]()
        {
            if (!field->hasFocus()) field->setText(getter());
        });
    }
}

WineSettings::WineSettings(core::wine::Shell* shell_, QWidget* parent)
    : QWidget(parent), shell(shell_)
{
    setup_dxvk_option();
    setup_prefix_option();
    setup_wine_binary_option();
    setup_tricks_option();
    setup_wine_args_option();
}

void WineSettings::setup_dxvk_option()
{
    const QSize w = window()->size();
    const int y = wset::row(0);
#if defined(Q_OS_MACOS)
    util::simple_utils::make_label_block(this, w, y,
        "USE DXVK",
        "Unavailable on macOS. Wine uses its built-in Direct3D renderer.");

    auto* slider = util::simple_utils::make_flat_button(this);
    const QRect sr = ls::slider_rect(w, y);
    slider->setIconSize(sr.size());
    slider->setGeometry(sr);
    slider->setIcon(QIcon(
        util::assets::button(util::assets::Button::SliderOff).normal.scaled(
            sr.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    slider->setEnabled(false);
    slider->setAccessibleName(QStringLiteral("Use DXVK"));
    slider->setAccessibleDescription(
        QStringLiteral("Unavailable on macOS"));
    slider->setToolTip(QStringLiteral(
        "DXVK requires Vulkan and cannot be enabled by this macOS launcher."));
#else
    util::simple_utils::make_label_block(this, w, y,
        "USE DXVK", "Translate Direct3D to Vulkan for better performance.");

    auto* slider = util::simple_utils::make_flat_button(this);
    const QRect sr = ls::slider_rect(w, y);
    const QSize ssz = sr.size();
    slider->setIconSize(ssz);
    slider->setGeometry(sr);
    auto paint = [slider, ssz](const bool on)
    {
        const auto& a = on ? util::assets::button(util::assets::Button::SliderOn)
                           : util::assets::button(util::assets::Button::SliderOff);
        slider->setIcon(QIcon(a.normal.scaled(ssz, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    };
    paint(Config::instance().use_dxvk());
    connect(slider, &QPushButton::clicked, this, [this, paint]()
    {
        const bool on = !Config::instance().use_dxvk();
        Config::instance().set_use_dxvk(on);
        paint(on);
        shell->sync_dxvk();
    });
    connect(&Config::instance(), &Config::changed, slider,
            [paint]() { paint(Config::instance().use_dxvk()); });
#endif
}

void WineSettings::setup_prefix_option()
{
    const QSize w = window()->size();
    const int y = wset::row(1);
    const QString description = QStringLiteral("The isolated Wine environment the game runs in.");
    const QString title = QStringLiteral("WINE PREFIX");
    util::simple_utils::make_label_block(this, w, y, title, description);

    auto* field = new QLineEdit(this);
    field->setText(Config::instance().wine_prefix());
    field->setAccessibleName(QStringLiteral("Wine prefix path"));
    field->setStyleSheet(util::styles::field(w));
    field->setGeometry(ls::field_rect(w, y));
    sync_field(field, []() { return Config::instance().wine_prefix(); });

    auto* browse = new QPushButton(QStringLiteral("..."), this);
    browse->setCursor(Qt::PointingHandCursor);
    browse->setStyleSheet(util::styles::neutral_button(w));
    browse->setGeometry(ls::browse_rect(w, y));
    browse->setAccessibleName(QStringLiteral("Choose Wine prefix folder"));
    connect(browse, &QPushButton::clicked, this, [this, field]()
    {
        const QString title = QStringLiteral("Select Wine Prefix Folder");
        const QString dir = QFileDialog::getExistingDirectory(
            this, util::i18n::translate(title));
        if (!dir.isEmpty())
        {
            field->setText(dir);
            Config::instance().set_wine_prefix(dir);
        }
    });
    connect(field, &QLineEdit::editingFinished, this, [field]()
    {
        if (field->text() != Config::instance().wine_prefix())
            Config::instance().set_wine_prefix(field->text());
    });
}

void WineSettings::setup_wine_binary_option()
{
    const QSize w = window()->size();
    const int y = wset::row(2);
#if defined(Q_OS_MACOS)
    util::simple_utils::make_label_block(this, w, y,
        "CUSTOM WINE",
        "A Wine app, executable, or installation folder. Blank uses Wine from PATH.");
#else
    util::simple_utils::make_label_block(this, w, y,
        "CUSTOM WINE / PROTON",
        "A wine binary, or a Proton folder's \"proton\" script. Blank uses system wine.");
#endif

    auto* field = new QLineEdit(this);
    field->setPlaceholderText(util::i18n::translate("system wine"));
    field->setAccessibleName(QStringLiteral("Custom Wine path"));
    field->setStyleSheet(util::styles::field(w));
    field->setGeometry(ls::field_rect(w, y));
    field->setText(Config::instance().wine_binary());
    sync_field(field, []() { return Config::instance().wine_binary(); });

    auto apply = [this, field](const QString& raw)
    {
        QString path = raw.trimmed();
#if !defined(Q_OS_MACOS)
        const QFileInfo info(path);
        if (info.isFile() && info.fileName() == QStringLiteral("proton"))
            path = info.absolutePath();
#endif
        if (!path.isEmpty())
        {
            cw::WineInstall install;
            QString error;
            if (!cw::WineRegistry::inspect_path(path, install, &error))
            {
                LauncherDialog::warning(this, QStringLiteral("Wine Not Usable"),
                    error.isEmpty() ? QStringLiteral("The selected Wine installation could not be used.") : error);
                field->setText(Config::instance().wine_binary());
                return;
            }
        }
        if (path == Config::instance().wine_binary())
        {
            return;
        }
        Config::instance().set_wine_binary(path);
        Config::instance().set_runtime_selected(!path.isEmpty());
#if defined(Q_OS_MACOS)
        Config::instance().set_setup_runtime_preference(QStringLiteral("wine"));
#endif
        field->setText(path);
    };

    auto* browse = new QPushButton(QStringLiteral("..."), this);
    browse->setCursor(Qt::PointingHandCursor);
    browse->setStyleSheet(util::styles::neutral_button(w));
    browse->setGeometry(ls::browse_rect(w, y));
#if defined(Q_OS_MACOS)
    browse->setAccessibleName(QStringLiteral("Choose Wine app, executable, or folder"));
#else
    browse->setAccessibleName(QStringLiteral("Choose Wine binary or Proton script"));
#endif
    connect(browse, &QPushButton::clicked, this, [this, apply]()
    {
#if defined(Q_OS_MACOS)
        const int selection = LauncherDialog::choose(
            this,
            LauncherDialog::Tone::Question,
            QStringLiteral("Select Wine"),
            QStringLiteral("Select a Wine application, executable, or installation folder."),
            {
                {QStringLiteral("Cancel"), LauncherDialog::Cancelled,
                 LauncherDialog::ActionStyle::Neutral, true},
                {QStringLiteral("Wine Folder"), LauncherDialog::Primary,
                 LauncherDialog::ActionStyle::Primary, false},
                {QStringLiteral("Wine App or Executable"), LauncherDialog::Secondary,
                 LauncherDialog::ActionStyle::Primary, false}
            });
        QString path;
        if (selection == LauncherDialog::Primary)
            path = QFileDialog::getExistingDirectory(
                this, util::i18n::translate("Select Wine Folder"));
        else if (selection == LauncherDialog::Secondary)
            path = QFileDialog::getOpenFileName(
                this,
                util::i18n::translate("Select Wine App or Executable"),
                QStringLiteral("/Applications"),
                QStringLiteral("%1 (*.app);;%2 (*)")
                .arg(util::i18n::translate("Applications"),
                     util::i18n::translate("All Files")));
        else
            return;
        if (!path.isEmpty())
            apply(path);
#else
        const QString file = QFileDialog::getOpenFileName(
            this, util::i18n::translate("Select Wine Binary or Proton Script"));
        if (!file.isEmpty()) apply(file);
#endif
    });
    connect(field, &QLineEdit::editingFinished, this, [field, apply]() { apply(field->text()); });
}

void WineSettings::setup_tricks_option()
{
    const QSize w = window()->size();
    const int y = wset::row(3);
    util::simple_utils::make_label_block(this, w, y,
        "WINETRICKS", "Path to Winetricks. Blank uses the one on PATH.");

    auto* field = new QLineEdit(this);
    field->setPlaceholderText(util::i18n::translate("from %1").arg(QStringLiteral("PATH")));
    field->setAccessibleName(QStringLiteral("Winetricks path"));
    field->setStyleSheet(util::styles::field(w));
    field->setGeometry(ls::field_rect(w, y));
    field->setText(Config::instance().winetricks_binary());
    sync_field(field, []() { return Config::instance().winetricks_binary(); });

    auto* browse = new QPushButton(QStringLiteral("..."), this);
    browse->setCursor(Qt::PointingHandCursor);
    browse->setStyleSheet(util::styles::neutral_button(w));
    browse->setGeometry(ls::browse_rect(w, y));
    browse->setAccessibleName(QStringLiteral("Choose Winetricks executable"));
    connect(browse, &QPushButton::clicked, this, [this, field]()
    {
        const QString file = QFileDialog::getOpenFileName(
            this, util::i18n::translate("Select Winetricks"));
        if (!file.isEmpty())
        {
            Config::instance().set_winetricks_binary(file);
            field->setText(file);
        }
    });
    connect(field, &QLineEdit::editingFinished, this, [field]()
    {
        if (field->text() != Config::instance().winetricks_binary())
            Config::instance().set_winetricks_binary(field->text());
    });
}


void WineSettings::setup_wine_args_option()
{
    const QSize w = window()->size();
    const int y = wset::row(4);
    const QString title = QStringLiteral("WINE ENVIRONMENT VARIABLES");
    const QString description = QStringLiteral(
        "Space-separated KEY=VALUE entries, for example WINEDEBUG=-all.");
    util::simple_utils::make_label_block(this, w, y, title, description);

    auto* field = new QLineEdit(this);
    field->setPlaceholderText(QStringLiteral("WINEESYNC=1"));
    field->setAccessibleName(QStringLiteral("Wine environment variables"));
    field->setStyleSheet(util::styles::field(w));
    field->setGeometry(ls::ctrl_pos(w, y).x(), ls::ctrl_pos(w, y).y(),
                       ls::ctrl_w(w), qMax(32, util::layout::scaled(40, w)));
    field->setText(Config::instance().wine_args());
    sync_field(field, []() { return Config::instance().wine_args(); });
    connect(field, &QLineEdit::editingFinished, this, [field]()
    {
        if (field->text() != Config::instance().wine_args())
            Config::instance().set_wine_args(field->text());
    });
}
