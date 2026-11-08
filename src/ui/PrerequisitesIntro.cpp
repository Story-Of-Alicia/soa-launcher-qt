// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#include "ui/PrerequisitesIntro.hpp"
#include "i18n/LanguageManager.hpp"

#include "ui/Assets.hpp"
#include "ui/Colors.hpp"
#include "config/Config.hpp"
#include "runtime/PrefixInspector.hpp"
#include "ui/Layout.hpp"

#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>

#include <algorithm>
#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>

namespace
{
    constexpr QSize k_box_size {680, 410};

    QRect box_rect(const QSize window_size)
    {
        return soa::ui::layout::centered(k_box_size, window_size, 0, 8);
    }

    QRect local_rect(const QSize window_size, const QRect source)
    {
        return soa::ui::layout::scaled(source, window_size).translated(box_rect(window_size).topLeft());
    }

    QString recommendation_style(const QSize window_size, const bool error = false)
    {
        return QStringLiteral(
            "QLabel { background:%1; border:1px solid %2; border-radius:0px; color:%3; "
            "padding:%4px %5px %6px %5px; }")
            .arg(error ? QStringLiteral("rgba(255,245,242,0.94)")
                       : QStringLiteral("rgba(255,255,255,0.76)"),
                 error ? QStringLiteral("rgba(192,111,91,205)")
                       : QStringLiteral("rgba(201,187,170,205)"),
                 error ? QStringLiteral("#7F2929") : QStringLiteral("#392518"))
            .arg(soa::ui::layout::scaled(24, window_size))
            .arg(soa::ui::layout::scaled(26, window_size))
            .arg(soa::ui::layout::scaled(16, window_size));
    }

    QString primary_style(const QSize window_size)
    {
        return QStringLiteral(
            "QPushButton { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #54D8FF,stop:1 #08A9D8);"
            " border:1px solid #159FC8; border-radius:%1px; color:white; }"
            "QPushButton:hover { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #77E2FF,stop:1 #18B9E8); }"
            "QPushButton:pressed { background:#0798C5; }"
            "QPushButton:disabled { background:#D8CDC0; border-color:#C9BBAA; color:#9E8E7E; }")
            .arg(soa::ui::layout::scaled(6, window_size));
    }

    QString secondary_style(const QSize window_size)
    {
        return QStringLiteral(
            "QPushButton { background:rgba(255,255,255,0.72); border:1px solid #C9BBAA;"
            " border-radius:%1px; color:#4F1717; }"
            "QPushButton:hover { border-color:#2FB4E0; background:rgba(255,255,255,0.94); }"
            "QPushButton:pressed { background:#EAF7FC; }")
            .arg(soa::ui::layout::scaled(6, window_size));
    }

    int runtime_score(const soa::runtime::WineInstall& runtime)
    {
        if (!runtime.usable)
            return -1;

        const QString name = runtime.name.toLower();
        int score = runtime.type == soa::runtime::RuntimeType::Proton ? 9000 : 8000;
        if (runtime.type == soa::runtime::RuntimeType::Proton)
        {
            if (name.contains(QStringLiteral("ge-proton")))
                score += 500;
            else if (name.contains(QStringLiteral("experimental")))
                score -= 500;
            else if (name.contains(QStringLiteral("hotfix")))
                score -= 750;
        }
        else if (name == QStringLiteral("system wine"))
        {
            score += 100;
        }

        const QString versionText = runtime.version + QLatin1Char(' ') + runtime.name;
        const QRegularExpression pattern(QStringLiteral(R"((\d+)(?:\.(\d+))?)"));
        const QRegularExpressionMatch match = pattern.match(versionText);

        if (match.hasMatch())
            score += qMin(match.captured(1).toInt(), 99) * 10
                + qMin(match.captured(2).toInt(), 9);

        return score;
    }

    QString joined_requirements(const QStringList& requirements)
    {
        if (requirements.isEmpty()) return {};
        if (requirements.size() == 1) return requirements.front();
        if (requirements.size() == 2)
            return soa::i18n::translate("%1 and %2")
                .arg(requirements.front(), requirements.back());
        QStringList leading = requirements;
        const QString last = leading.takeLast();
        return soa::i18n::translate("%1, and %2")
            .arg(leading.join(QStringLiteral(", ")), last);
    }
}

PrerequisitesIntro::PrerequisitesIntro(QWidget* parent)
    : ModalOverlay(parent),
      detector(new QFutureWatcher<DetectionResult>(this))
{
    set_keeps_chrome(true);
    setup_controls();
    connect(detector, &QFutureWatcher<DetectionResult>::finished,
            this, &PrerequisitesIntro::finish_detection);

    system_profile = soa::runtime::detect_system_profile();
    update_recommendation();

    connect(&soa::i18n::LanguageManager::instance(),
            &soa::i18n::LanguageManager::language_changed, this, [this]()
    {
        update_recommendation();
        update();
    });
}

void PrerequisitesIntro::setup_controls()
{
    const QSize w = window()->size();
    continue_button = new QPushButton(soa::i18n::translate("USE PROTON"), this);
    continue_button->setCursor(Qt::PointingHandCursor);
    continue_button->setStyleSheet(primary_style(w));
    QFont primary_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    primary_font.setPixelSize(soa::ui::layout::scaled(16, w));
    primary_font.setWeight(QFont::Black);
    continue_button->setFont(primary_font);
    continue_button->setGeometry(local_rect(w, {60, 268, 250, 50}));
    continue_button->setAccessibleName(soa::i18n::translate("Use Proton"));
    continue_button->setEnabled(system_profile.cpu_architecture != soa::runtime::CpuArchitecture::Unknown);

    connect(continue_button, &QPushButton::clicked,
            this, &PrerequisitesIntro::apply_recommendation);

    choose_own_button = new QPushButton(soa::i18n::translate("USE WINE"), this);
    choose_own_button->setCursor(Qt::PointingHandCursor);
    choose_own_button->setStyleSheet(secondary_style(w));
    QFont secondary_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    secondary_font.setPixelSize(soa::ui::layout::scaled(13, w));
    secondary_font.setWeight(QFont::Black);
    choose_own_button->setFont(secondary_font);
    choose_own_button->setGeometry(local_rect(w, {370, 268, 250, 50}));
    choose_own_button->setAccessibleName(soa::i18n::translate("Use Wine"));

    connect(choose_own_button, &QPushButton::clicked, this, [this]()
    {
        auto& config = soa::config::Config::instance();
        config.set_setup_runtime_preference(QStringLiteral("wine"));
        config.set_runtime_selected(false);
        emit choose_own_requested();
    });
}

void PrerequisitesIntro::start_detection()
{
    if (detector->isRunning()) return;
    detection_complete = false;

    const auto detection = []()
    {
        DetectionResult result;
        result.profile = soa::runtime::detect_system_profile();
        result.runtimes = soa::runtime::WineRegistry::scan();
        result.winetricks_ready = soa::runtime::winetricks_available();
        result.umu_ready = soa::runtime::umu_available();
        return result;
    };

    detector->setFuture(QtConcurrent::run(detection));
}

void PrerequisitesIntro::finish_detection()
{
    const DetectionResult result = detector->result();
    system_profile = result.profile;
    runtimes = result.runtimes;
    winetricks_ready = result.winetricks_ready;
    umu_ready = result.umu_ready;
    detection_complete = true;
    update_recommendation();
    update();
}

soa::runtime::RuntimeType PrerequisitesIntro::recommended_runtime() const
{
    const bool proton_found = best_runtime(soa::runtime::RuntimeType::Proton) != nullptr;
    const bool wine_found = best_runtime(soa::runtime::RuntimeType::Wine) != nullptr;
    const bool proton_ready = proton_found && umu_ready
        && (!host_winetricks_required(soa::runtime::RuntimeType::Proton) || winetricks_ready);
    const bool wine_ready = wine_found
        && (!host_winetricks_required(soa::runtime::RuntimeType::Wine) || winetricks_ready);
    if (proton_ready) return soa::runtime::RuntimeType::Proton;
    if (wine_ready) return soa::runtime::RuntimeType::Wine;
    if (proton_found) return soa::runtime::RuntimeType::Proton;
    return soa::runtime::RuntimeType::Wine;
}

const soa::runtime::WineInstall* PrerequisitesIntro::best_runtime(
    const soa::runtime::RuntimeType type) const
{
    const soa::runtime::WineInstall* best = nullptr;
    int best_score = -1;
    for (const auto& runtime : runtimes)
    {
        if (runtime.type != type) continue;
        const int score = runtime_score(runtime);
        if (score < 0)
            continue;
        if (!best || score > best_score)
        {
            best = &runtime;
            best_score = score;
        }
    }
    return best;
}

bool PrerequisitesIntro::host_winetricks_required(const soa::runtime::RuntimeType type) const
{
    const auto* runtime = best_runtime(type);
    QString runtime_path = runtime ? runtime->path : QString();
    if (type == soa::runtime::RuntimeType::Proton && runtime_path.isEmpty())
        runtime_path = soa::runtime::managed_proton_identifier();

    return soa::runtime::PrefixInspector::required_winetricks_backend(
               soa::config::Config::instance().prefix_root(), type, runtime_path,
               false)
        == soa::runtime::WinetricksBackend::Host;
}

QStringList PrerequisitesIntro::missing_requirements(const soa::runtime::RuntimeType type) const
{
    QStringList missing;
    if (!best_runtime(type))
    {
        missing << (type == soa::runtime::RuntimeType::Proton
                        ? QStringLiteral("Proton")
                        : QStringLiteral("Wine"));
    }

    if (type == soa::runtime::RuntimeType::Proton && !umu_ready)
        missing << QStringLiteral("UMU");
    if (host_winetricks_required(type) && !winetricks_ready)
        missing << QStringLiteral("Winetricks");
    return missing;
}

bool PrerequisitesIntro::profile_ready(QString* blocker) const
{
    if (!detection_complete)
    {
        if (blocker) *blocker = soa::i18n::translate("The system check is still running.");
        return false;
    }

    if (system_profile.cpu_architecture != soa::runtime::CpuArchitecture::X86_64)
    {
        if (blocker)
            *blocker = soa::i18n::translate("The Linux launcher and game currently require an x86_64 computer.");
        return false;
    }

    const auto runtime_type = recommended_runtime();
    const QStringList missing = missing_requirements(runtime_type);
    if (missing.isEmpty()) return true;
    if (!blocker) return false;

    const QString missing_text = joined_requirements(missing);
    const QString install_wording = missing.size() == 1
        ? soa::i18n::translate("Install it")
        : soa::i18n::translate("Install them");
    *blocker = soa::i18n::translate(
        "Missing: %1. %2, then restart the launcher.")
        .arg(missing_text, install_wording);
    return false;
}

void PrerequisitesIntro::update_recommendation()
{
    const bool supported = system_profile.cpu_architecture == soa::runtime::CpuArchitecture::X86_64;
    continue_button->setText(soa::i18n::translate("USE PROTON"));
    continue_button->setAccessibleName(soa::i18n::translate("Use Proton"));
    continue_button->setEnabled(supported);
    choose_own_button->setText(soa::i18n::translate("USE WINE"));
    choose_own_button->setAccessibleName(soa::i18n::translate("Use Wine"));
    choose_own_button->setEnabled(supported);
    choose_own_button->show();
    update();
}

void PrerequisitesIntro::apply_recommendation()
{
    auto& config = soa::config::Config::instance();
    config.begin_update();
    config.set_setup_runtime_preference(QStringLiteral("proton"));
    config.set_runtime_selected(false);
    config.end_update();
    emit choose_own_requested();
}

void PrerequisitesIntro::showEvent(QShowEvent* event)
{
    if (!detection_complete) start_detection();
    else update_recommendation();
    ModalOverlay::showEvent(event);
}

void PrerequisitesIntro::paint_content(QPainter& painter)
{
    const QSize w = window()->size();
    const QRect box = box_rect(w);
    painter.drawPixmap(box, soa::ui::assets::images[soa::ui::assets::Image::BoxSettings]);

    QFont title_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    title_font.setPixelSize(soa::ui::layout::scaled(29, w));
    title_font.setWeight(QFont::Black);
    painter.setFont(title_font);
    painter.setPen(soa::ui::colors::k_text_maroon);
    painter.drawText(local_rect(w, {20, 28, 640, 40}), Qt::AlignCenter,
                     soa::i18n::translate("EASY SETUP"));

    QFont body_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    body_font.setPixelSize(soa::ui::layout::scaled(14, w));
    body_font.setWeight(QFont::Medium);
    painter.setFont(body_font);
    painter.setPen(soa::ui::colors::k_text_body);
    painter.drawText(local_rect(w, {65, 72, 550, 24}),
                     Qt::AlignHCenter | Qt::AlignTop,
                     soa::i18n::translate("Choose how to run the game"));

    const QRect proton_card = local_rect(w, {42, 112, 286, 222});
    const QRect wine_card = local_rect(w, {352, 112, 286, 222});

    painter.drawPixmap(proton_card, soa::ui::assets::images[soa::ui::assets::Image::BoxCard]);
    painter.drawPixmap(wine_card, soa::ui::assets::images[soa::ui::assets::Image::BoxCard]);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(47, 180, 224), soa::ui::layout::scaled(2, w)));
    painter.drawRect(proton_card.adjusted(1, 1, -1, -1));

    QFont card_title = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    card_title.setPixelSize(soa::ui::layout::scaled(20, w));
    card_title.setWeight(QFont::Black);
    painter.setFont(card_title);
    painter.setPen(soa::ui::colors::k_text_maroon);
    painter.drawText(local_rect(w, {60, 128, 130, 30}), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("PROTON"));
    painter.drawText(local_rect(w, {370, 128, 130, 30}), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("WINE"));

    QFont badge_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    badge_font.setPixelSize(soa::ui::layout::scaled(10, w));
    badge_font.setWeight(QFont::Black);
    painter.setFont(badge_font);
    const QRect badge = local_rect(w, {192, 130, 116, 25});
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(47, 180, 224));
    painter.drawRoundedRect(badge, soa::ui::layout::scaled(12, w), soa::ui::layout::scaled(12, w));
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, soa::i18n::translate("RECOMMENDED"));

    const bool proton_installed = detection_complete
        && best_runtime(soa::runtime::RuntimeType::Proton) != nullptr;
    const bool wine_installed = detection_complete
        && best_runtime(soa::runtime::RuntimeType::Wine) != nullptr;

    const auto draw_package = [&](const QRect row, const QString& name, const bool ready)
    {
        const QRect icon_rect(row.left(), row.top() + soa::ui::layout::scaled(1, w),
                              soa::ui::layout::scaled(22, w), soa::ui::layout::scaled(22, w));
        const auto image = ready ? soa::ui::assets::Image::CheckboxTicked
                                 : soa::ui::assets::Image::Checkbox;
        painter.drawPixmap(icon_rect, soa::ui::assets::images[image]);
        QFont package_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
        package_font.setPixelSize(soa::ui::layout::scaled(13, w));
        package_font.setWeight(ready ? QFont::DemiBold : QFont::Medium);
        painter.setFont(package_font);
        painter.setPen(ready ? QColor(17, 157, 201) : soa::ui::colors::k_text_caption);
        painter.drawText(QRect(icon_rect.right() + soa::ui::layout::scaled(9, w), row.top(),
                               row.width() - icon_rect.width() - soa::ui::layout::scaled(9, w), row.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, name);
    };

    draw_package(local_rect(w, {68, 174, 220, 26}), QStringLiteral("UMU Launcher"), umu_ready);
    draw_package(local_rect(w, {68, 208, 220, 26}), QStringLiteral("Proton"), proton_installed);
    draw_package(local_rect(w, {378, 174, 220, 26}), QStringLiteral("Wine"), wine_installed);
    draw_package(local_rect(w, {378, 208, 220, 26}), QStringLiteral("Winetricks"), winetricks_ready);

    QFont note_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    note_font.setPixelSize(soa::ui::layout::scaled(11, w));
    note_font.setWeight(QFont::Medium);
    painter.setFont(note_font);
    painter.setPen(soa::ui::colors::k_text_caption);
    painter.drawText(local_rect(w, {60, 350, 560, 22}), Qt::AlignCenter,
                     soa::i18n::translate(
                         "Proton is recommended even if Wine and Winetricks are already installed."));

    if (system_profile.cpu_architecture != soa::runtime::CpuArchitecture::X86_64)
    {
        painter.setPen(QColor(127, 41, 41));
        painter.drawText(local_rect(w, {60, 376, 560, 22}), Qt::AlignCenter,
                         soa::i18n::translate(
                             "The Linux launcher and game currently require an x86_64 computer."));
    }
}

