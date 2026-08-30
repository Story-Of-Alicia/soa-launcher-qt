#include "ui/LauncherSettings.hpp"
#include "network/SwiftHttpClient.hpp"
#include "ui/LauncherDialog.hpp"

#include "ui/ImageDropdown.hpp"
#include "ui/Assets.hpp"
#include "config/Config.hpp"
#include "ui/Layout.hpp"
#include "i18n/LanguageManager.hpp"
#include "ui/SimpleUtils.hpp"
#include "common/DesktopEntry.hpp"

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <unistd.h>

#include <utility>

#ifdef Q_OS_MACOS
#include <unistd.h>
#endif

#ifndef SOA_LAUNCHER_VERSION
#define SOA_LAUNCHER_VERSION "0.3.0"
#endif

using util::config::Config;
namespace ls = util::layout::settings;
namespace lset = util::layout::launcher_settings;

namespace
{
    QString executable_path()
    {
#ifdef Q_OS_LINUX
        const QString appimage = qEnvironmentVariable("APPIMAGE");
        if (!appimage.isEmpty())
            return appimage;
#endif
        return QCoreApplication::applicationFilePath();
    }

    QString desktop_exec(const QString& executable)
    {
        return util::desktop_entry::quoted_exec_argument(executable);
    }

    bool configure_linux_startup(const bool enabled, QString& error)
    {
        const QString config_root = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        if (config_root.isEmpty())
        {
            error = QStringLiteral("The user configuration directory could not be located.");
            return false;
        }

        const QString directory = QDir(config_root).filePath(QStringLiteral("autostart"));
        const QString path = QDir(directory).filePath(QStringLiteral("soa-launcher.desktop"));
        if (!enabled)
        {
            if (!QFile::exists(path) || QFile::remove(path))
                return true;
            error = QStringLiteral("The launcher startup entry could not be removed.");
            return false;
        }

        if (!QDir().mkpath(directory))
        {
            error = QStringLiteral("The autostart directory could not be created.");
            return false;
        }

        QString contents;
        QTextStream stream(&contents);
        stream << "[Desktop Entry]\n"
               << "Type=Application\n"
               << "Name=Story Of Alicia Launcher\n"
               << "Exec=" << desktop_exec(executable_path()) << "\n"
               << "Icon=soa-launcher\n"
               << "Terminal=false\n"
               << "X-GNOME-Autostart-enabled=true\n";

        QSaveFile file(path);
        const QByteArray data = contents.toUtf8();
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
            || file.write(data) != data.size()
            || !file.commit())
        {
            error = QStringLiteral("The launcher startup entry could not be written.");
            return false;
        }
        return true;
    }

    QString macos_bundle_path()
    {
        QString path = QCoreApplication::applicationFilePath();
        const qsizetype marker = path.indexOf(QStringLiteral(".app/Contents/MacOS/"));
        if (marker >= 0)
            return path.left(marker + 4);
        return {};
    }

    bool configure_macos_startup(const bool enabled, QString& error)
    {
        const QString directory = QDir::home().filePath(QStringLiteral("Library/LaunchAgents"));
        const QString path = QDir(directory).filePath(
            QStringLiteral("com.storyofalicia.launcher.plist"));
        const QString domain = QStringLiteral("gui/%1").arg(static_cast<qulonglong>(getuid()));
        if (!enabled)
        {
            if (QFile::exists(path))
                QProcess::execute(QStringLiteral("/bin/launchctl"), {QStringLiteral("bootout"), domain, path});
            if (!QFile::exists(path) || QFile::remove(path))
                return true;
            error = QStringLiteral("The launcher login item could not be removed.");
            return false;
        }

        if (!QDir().mkpath(directory))
        {
            error = QStringLiteral("The LaunchAgents directory could not be created.");
            return false;
        }

        const QString bundle = macos_bundle_path();
        const QString program = bundle.isEmpty()
            ? executable_path()
            : QStringLiteral("/usr/bin/open");
        QString arguments;
        if (bundle.isEmpty())
        {
            arguments = QStringLiteral("<string>%1</string>")
                .arg(program.toHtmlEscaped());
        }
        else
        {
            arguments = QStringLiteral("<string>/usr/bin/open</string><string>%1</string>")
                .arg(bundle.toHtmlEscaped());
        }

        const QString contents = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"
            "<plist version=\"1.0\"><dict>"
            "<key>Label</key><string>com.storyofalicia.launcher</string>"
            "<key>ProgramArguments</key><array>%1</array>"
            "<key>RunAtLoad</key><true/>"
            "</dict></plist>").arg(arguments);

        QSaveFile file(path);
        const QByteArray data = contents.toUtf8();
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
            || file.write(data) != data.size()
            || !file.commit())
        {
            error = QStringLiteral("The launcher login item could not be written.");
            return false;
        }

        QProcess::execute(QStringLiteral("/bin/launchctl"), {QStringLiteral("bootout"), domain, path});
        if (QProcess::execute(QStringLiteral("/bin/launchctl"),
                              {QStringLiteral("bootstrap"), domain, path}) != 0)
        {
            error = QStringLiteral("The launcher login item was written but could not be activated.");
            return false;
        }
        return true;
    }

    bool configure_startup(const bool enabled, QString& error)
    {
#ifdef Q_OS_LINUX
        return configure_linux_startup(enabled, error);
#elif defined(Q_OS_MACOS)
        return configure_macos_startup(enabled, error);
#else
        error = QStringLiteral("Launch on startup is unavailable on this platform.");
        return false;
#endif
    }

    QString escaped_html(const QString& value)
    {
        return value.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    }
}

LauncherSettings::LauncherSettings(QWidget* parent) : QWidget(parent)
{
    setup_launch_on_startup_option();
    setup_after_game_start_option();
    setup_run_connectivity_test_option();
    setup_launcher_size_option();
    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed, this, [this]()
    {
        refresh_connectivity_report();
    });
}

void LauncherSettings::setup_launch_on_startup_option()
{
    const QSize w = window()->size();
    const int y = lset::row(0);
    util::simple_utils::make_label_block(
        this, w, y,
        "LAUNCH ON STARTUP",
        "Automatically open the launcher when you log in to your computer.");

    startup_button = util::simple_utils::make_flat_button(this);
    startup_button->setGeometry(ls::slider_rect(w, y));
    startup_button->setIconSize(startup_button->size());
    startup_button->setAccessibleName(QStringLiteral("Launch on startup"));
    set_startup_button_state(Config::instance().launch_on_startup());

    connect(startup_button, &QPushButton::clicked, this, [this]()
    {
        const bool previous = Config::instance().launch_on_startup();
        const bool enabled = !previous;
        QString error;
        if (!configure_startup(enabled, error))
        {
            LauncherDialog::error(this, QStringLiteral("Startup Setting Failed"), error);
            set_startup_button_state(previous);
            return;
        }
        Config::instance().set_launch_on_startup(enabled);
        set_startup_button_state(enabled);
    });
}

void LauncherSettings::set_startup_button_state(const bool enabled)
{
    if (!startup_button)
        return;
    const auto asset = enabled ? util::assets::Button::SliderOn
                               : util::assets::Button::SliderOff;
    startup_button->setIcon(QIcon(util::assets::button(asset).normal.scaled(
        startup_button->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void LauncherSettings::setup_after_game_start_option()
{
    const QSize w = window()->size();
    const int y = lset::row(1);
    util::simple_utils::make_label_block(
        this, w, y,
        "AFTER GAME START",
        "Choose what the launcher does after the game starts up.");

    auto* dropdown = new ImageDropdown(
        {QStringLiteral("Keep launcher open"), QStringLiteral("Minimize launcher")}, this);
    dropdown->set_index(Config::instance().after_game_start() == QStringLiteral("minimize") ? 1 : 0);
    dropdown->move(ls::ctrl_pos(w, y));

    connect(dropdown, &ImageDropdown::changed, this, [](const int index)
    {
        Config::instance().set_after_game_start(
            index == 1 ? QStringLiteral("minimize") : QStringLiteral("keep"));
    });
}

void LauncherSettings::setup_run_connectivity_test_option()
{
    const QSize w = window()->size();
    const int y = lset::row(2);
    util::simple_utils::make_label_block(
        this, w, y,
        "CONNECTIVITY CHECK",
        "Diagnose issues connecting to the game and related servers.");

    connectivity_button = util::simple_utils::make_flat_button(this);
    connectivity_button->setProperty("soa_allow_while_mutation_locked", true);
    const QRect button_rect = ls::run_check(w, y);
    connectivity_button->setGeometry(button_rect);
    connectivity_button->setIconSize(button_rect.size());
    connectivity_button->setIcon(QIcon(
        util::assets::button(util::assets::Button::RunCheck).normal.scaled(
            button_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    QFont connectivity_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    connectivity_font.setPixelSize(util::layout::scaled(11, w));
    connectivity_font.setWeight(QFont::Black);
    util::simple_utils::add_button_text(
        connectivity_button, util::assets::Button::RunCheck, QStringLiteral("RUN CHECK"), connectivity_font);
    connectivity_button->setAccessibleName(QStringLiteral("Run connectivity check"));

    connectivity_panel = new QFrame(this);
    connectivity_panel->setProperty("soa_allow_while_mutation_locked", true);
    connectivity_panel->setGeometry(lset::connectivity_results(w));
    connectivity_panel->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(255,255,255,0.42); border: 1px solid rgba(201,187,170,0.7); border-radius: %1px; }")
        .arg(util::layout::scaled(3, w)));
    connectivity_panel->hide();

    connectivity_label = new QLabel(connectivity_panel);
    connectivity_label->setGeometry(lset::connectivity_text(w));
    connectivity_label->setTextFormat(Qt::RichText);
    connectivity_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    connectivity_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connectivity_label->setAccessibleName(QStringLiteral("Connectivity check results"));
    connectivity_label->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; border: none; color: #5A4636; font-family: 'Inter'; font-size: %1px; }")
        .arg(qMax(8, util::layout::scaled(12, w))));

    copy_report_button = new QPushButton(QStringLiteral("COPY REPORT"), connectivity_panel);
    copy_report_button->setGeometry(lset::copy_report(w));
    copy_report_button->setCursor(Qt::PointingHandCursor);
    copy_report_button->setAccessibleName(QStringLiteral("Copy connectivity report"));
    copy_report_button->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; color: #9E8E7E; font-family: 'Inter'; font-size: %1px; font-weight: 700; }"
        "QPushButton:hover { color: #6F5F50; }")
        .arg(qMax(8, util::layout::scaled(11, w))));
    connect(copy_report_button, &QPushButton::clicked, this, [this]()
    {
        if (!QApplication::clipboard())
            return;
        QApplication::clipboard()->setText(connectivity_plain_report);
        copy_report_button->setText(util::i18n::translate("COPIED"));
        QTimer::singleShot(1200, copy_report_button, [this]()
        {
            copy_report_button->setText(util::i18n::translate("COPY REPORT"));
        });
    });

    network_manager = new core::network::SwiftHttpClient(this);
    connect(connectivity_button, &QPushButton::clicked,
            this, &LauncherSettings::run_connectivity_check);
}

void LauncherSettings::run_connectivity_check()
{
    if (pending_connectivity_checks > 0)
        return;

    set_connectivity_expanded(true);
    connectivity_order = {
        QStringLiteral("DNS"),
        QStringLiteral("Network ping"),
        QStringLiteral("1.0 files"),
        QStringLiteral("2.0 files"),
        QStringLiteral("Auth server"),
        QStringLiteral("Discord")
    };
    connectivity_details.clear();
    connectivity_success.clear();
    pending_connectivity_checks = connectivity_order.size();
    connectivity_button->setEnabled(false);
    util::simple_utils::set_button_loading(connectivity_button, true);
    connectivity_panel->show();
    refresh_connectivity_report();

    start_dns_check();
    start_ping_check();
    start_http_check(QStringLiteral("1.0 files"),
                     QUrl(QStringLiteral("https://r2.storyofalicia.com/game/version")));
    start_http_check(QStringLiteral("2.0 files"),
                     QUrl(QStringLiteral("https://r2.storyofalicia.com/game2/version")));
    start_http_check(QStringLiteral("Auth server"),
                     QUrl(QStringLiteral("https://authentication.storyofalicia.com/")));
    start_http_check(QStringLiteral("Discord"),
                     QUrl(QStringLiteral("https://discord.com/")));
}

void LauncherSettings::start_dns_check()
{
    static const QString production_host = QStringLiteral("production.storyofalicia.com");
    static const QString production_ip = QStringLiteral("5.75.155.237");
    const qulonglong request_id = network_manager->resolve(
        production_host,
        5000,
        [this](const core::network::DnsResponse& response)
        {
            static const QString expected_ip = QStringLiteral("5.75.155.237");
            const bool resolved = response.result == soa_http_result_completed
                && !response.address.isEmpty();
            const bool ok = resolved && response.address == expected_ip;
            QString detail;
            if (ok)
                detail = QStringLiteral("%1 (%2 ms)").arg(response.address).arg(response.elapsed_ms);
            else if (resolved)
                detail = QStringLiteral("resolved to %1; expected %2 (%3 ms)")
                    .arg(response.address, expected_ip)
                    .arg(response.elapsed_ms);
            else
                detail = response.error;
            record_connectivity_result(QStringLiteral("DNS"), ok, detail);
        });
    if (request_id == 0)
        record_connectivity_result(QStringLiteral("DNS"), false,
                                   QStringLiteral("could not start DNS lookup for %1 (%2)")
                                       .arg(production_host, production_ip));
}

void LauncherSettings::start_ping_check()
{
    const QString ping = QStandardPaths::findExecutable(QStringLiteral("ping"));
    if (ping.isEmpty())
    {
        record_connectivity_result(QStringLiteral("Network ping"), false,
                                   QStringLiteral("ping command unavailable"));
        return;
    }

    auto* process = new QProcess(this);
    process->setProperty("soa_finished", false);
    const auto timer = QSharedPointer<QElapsedTimer>::create();
    timer->start();

    const auto finish = [this, process, timer](const bool ok, const QString& detail)
    {
        if (process->property("soa_finished").toBool())
            return;
        process->setProperty("soa_finished", true);
        record_connectivity_result(QStringLiteral("Network ping"), ok, detail);
        process->deleteLater();
    };

    connect(process, &QProcess::finished, this,
            [finish, timer](const int exit_code, QProcess::ExitStatus status)
    {
        const bool ok = status == QProcess::NormalExit && exit_code == 0;
        finish(ok, ok ? QStringLiteral("%1 ms").arg(timer->elapsed())
                      : QStringLiteral("blocked or unreachable"));
    });
    connect(process, &QProcess::errorOccurred, this,
            [finish](QProcess::ProcessError)
    {
        finish(false, QStringLiteral("could not start ping"));
    });

#ifdef Q_OS_MACOS
    process->start(ping, {QStringLiteral("-c"), QStringLiteral("1"),
                          QStringLiteral("-W"), QStringLiteral("2000"),
                          QStringLiteral("r2.storyofalicia.com")});
#else
    process->start(ping, {QStringLiteral("-c"), QStringLiteral("1"),
                          QStringLiteral("-W"), QStringLiteral("2"),
                          QStringLiteral("r2.storyofalicia.com")});
#endif

    QTimer::singleShot(5000, process, [process, finish]()
    {
        if (process->property("soa_finished").toBool())
            return;
        process->kill();
        finish(false, QStringLiteral("timed out"));
    });
}

void LauncherSettings::start_http_check(const QString& label, const QUrl& url)
{
    const QByteArray user_agent = QByteArray("Story-Of-Alicia-Launcher/") + SOA_LAUNCHER_VERSION;
    const qulonglong request_id = network_manager->get(
        url,
        7000,
        256 * 1024,
        QByteArray("*/*"),
        user_agent,
        false,
        [this, label](const core::network::HttpResponse& response)
        {
            const bool reached = response.status >= 200 && response.status < 500;
            const bool ok = reached;
            QString detail;
            if (ok)
                detail = QStringLiteral("reachable (%1 ms)").arg(response.elapsed_ms);
            else if (response.status > 0)
                detail = QStringLiteral("HTTP %1 (%2 ms)").arg(response.status).arg(response.elapsed_ms);
            else
                detail = response.error;
            record_connectivity_result(label, ok, detail);
        });
    if (request_id == 0)
        record_connectivity_result(label, false, QStringLiteral("could not start request"));
}

void LauncherSettings::record_connectivity_result(const QString& label, const bool ok,
                                                   const QString& detail)
{
    if (connectivity_details.contains(label))
        return;
    connectivity_details.insert(label, detail);
    connectivity_success.insert(label, ok);
    pending_connectivity_checks = qMax(0, pending_connectivity_checks - 1);
    refresh_connectivity_report();
    if (pending_connectivity_checks == 0)
        finish_connectivity_check();
}

void LauncherSettings::refresh_connectivity_report()
{
    const QSize w = window()->size();
    const QStringList left {
        QStringLiteral("DNS"),
        QStringLiteral("Network ping"),
        QStringLiteral("Auth server")
    };
    const QStringList right {
        QStringLiteral("1.0 files"),
        QStringLiteral("2.0 files"),
        QStringLiteral("Discord")
    };

    const auto cell = [this, w](const QString& label)
    {
        const bool complete = connectivity_details.contains(label);
        const bool ok = connectivity_success.value(label);
        const QString detail = complete
            ? connectivity_details.value(label)
            : QStringLiteral("checking...");
        const QString color = !complete
            ? QStringLiteral("#9E8E7E")
            : ok ? QStringLiteral("#15936E") : QStringLiteral("#C34A3C");
        return QStringLiteral(
            "<td width=\"50%\" style=\"padding:%1px %2px %3px 0;\">"
            "<b>%4:</b> <span style=\"color:%5\">%6</span></td>")
            .arg(util::layout::scaled(1, w))
            .arg(util::layout::scaled(10, w))
            .arg(util::layout::scaled(2, w))
            .arg(escaped_html(util::i18n::translate(label)))
            .arg(color)
            .arg(escaped_html(util::i18n::translate(detail)));
    };

    QString html = QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    for (int i = 0; i < left.size(); ++i)
        html += QStringLiteral("<tr>%1%2</tr>")
            .arg(cell(left[i]))
            .arg(cell(right[i]));
    html += QStringLiteral("</table>");

    QString plain;
    for (const QString& label : std::as_const(connectivity_order))
    {
        const QString detail = connectivity_details.contains(label)
            ? connectivity_details.value(label)
            : QStringLiteral("checking...");
        plain += QStringLiteral("%1: %2\n")
            .arg(util::i18n::translate(label), util::i18n::translate(detail));
    }

    connectivity_label->setText(html);
    connectivity_plain_report = plain.trimmed();
}

void LauncherSettings::finish_connectivity_check()
{
    connectivity_button->setEnabled(true);
    util::simple_utils::set_button_loading(connectivity_button, false);
}

void LauncherSettings::setup_launcher_size_option()
{
    const QSize w = window()->size();

    launcher_size_title = new QLabel(QStringLiteral("LAUNCHER SIZE"), this);
    QFont title_font = util::assets::fonts[util::assets::Font::EurostileBlack];
    title_font.setPixelSize(util::layout::scaled(util::layout::text::k_row_title, w));
    title_font.setWeight(QFont::Black);
    launcher_size_title->setFont(title_font);
    launcher_size_title->setStyleSheet(QStringLiteral("color: #4F1717; background: transparent;"));

    launcher_size_description = new QLabel(
        QStringLiteral("Choose the window size used the next time the launcher starts."), this);
    launcher_size_description->setWordWrap(true);
    QFont description_font = util::assets::fonts[util::assets::Font::Inter];
    description_font.setPixelSize(util::layout::scaled(util::layout::text::k_desc, w));
    description_font.setWeight(QFont::Medium);
    launcher_size_description->setFont(description_font);
    launcher_size_description->setStyleSheet(
        QStringLiteral("color: #4F1717; background: transparent;"));

    const QStringList sizes {
        QStringLiteral("1120x677"),
        QStringLiteral("1400x846"),
        QStringLiteral("1600x967"),
        QStringLiteral("1920x1160")
    };
    launcher_size_dropdown = new ImageDropdown(
        {QStringLiteral("Small (1120x677)"),
         QStringLiteral("Default (1400x846)"),
         QStringLiteral("Large (1600x967)"),
         QStringLiteral("Extra Large (1920x1160)")}, this);
    launcher_size_dropdown->set_open_upwards(true);

    int index = sizes.indexOf(Config::instance().launcher_size());
    if (index < 0)
        index = 1;
    launcher_size_dropdown->set_index(index);

    connect(launcher_size_dropdown, &ImageDropdown::changed, this, [this, sizes](const int selected)
    {
        if (selected < 0 || selected >= sizes.size())
            return;
        if (Config::instance().launcher_size() == sizes[selected])
            return;
        Config::instance().set_launcher_size(sizes[selected]);
        LauncherDialog::information(
            this,
            QStringLiteral("Launcher Size Saved"),
            QStringLiteral("The new launcher size will be used after you restart the launcher."));
    });

    apply_dynamic_layout();
}

void LauncherSettings::set_connectivity_expanded(const bool expanded)
{
    if (connectivity_expanded == expanded)
        return;
    connectivity_expanded = expanded;
    apply_dynamic_layout();
    emit connectivity_panel_changed(expanded);
}

void LauncherSettings::apply_dynamic_layout()
{
    const QSize w = window()->size();
    const int y = lset::row(3, connectivity_expanded);
    if (launcher_size_title)
        launcher_size_title->setGeometry(ls::row_title(w, y));
    if (launcher_size_description)
        launcher_size_description->setGeometry(ls::row_desc(w, y));
    if (launcher_size_dropdown)
    {
        launcher_size_dropdown->move(ls::ctrl_pos(w, y));
        launcher_size_dropdown->raise();
    }
}
