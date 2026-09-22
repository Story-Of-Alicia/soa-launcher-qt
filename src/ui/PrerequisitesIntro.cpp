#include "ui/PrerequisitesIntro.hpp"
#include "i18n/LanguageManager.hpp"

#include "ui/Assets.hpp"
#include "ui/Colors.hpp"
#include "config/Config.hpp"
#include "ui/Layout.hpp"

#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPainter>
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
#if defined(Q_OS_MACOS)
        if (match.hasMatch())
            score += qMin(match.captured(1).toInt(), 99) * 100
                + qMin(match.captured(2).toInt(), 99);
        if (name.contains(QStringLiteral("crossover"))) score += 300;
        if (name.contains(QStringLiteral("whisky"))) score += 100;
        if (runtime.requires_rosetta && runtime.rosetta_available) score += 50;
#else
        if (match.hasMatch())
            score += qMin(match.captured(1).toInt(), 99) * 10
                + qMin(match.captured(2).toInt(), 9);
#endif
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
    connect(&soa::i18n::LanguageManager::instance(),
            &soa::i18n::LanguageManager::language_changed, this, [this]()
    {
        if (detection_complete)
            update_recommendation();
        else
        {
            recommendation_title->setText(soa::i18n::translate("CHECKING"));
#if defined(Q_OS_MACOS)
            recommendation_body->setText(soa::i18n::translate(
                "Looking for a usable Wine setup. Nothing will be installed automatically."));
#else
            recommendation_body->setText(soa::i18n::translate(
                "Looking for a usable Wine or Proton setup. Nothing will be installed automatically."));
#endif
            continue_button->setText(soa::i18n::translate("CHECKING..."));
        }
        update();
    });
}

void PrerequisitesIntro::setup_controls()
{
    const QSize w = window()->size();

    recommendation_body = new QLabel(this);
    recommendation_body->setTextFormat(Qt::PlainText);
    recommendation_body->setWordWrap(true);
    recommendation_body->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    recommendation_body->setStyleSheet(recommendation_style(window()->size()));
    QFont recommendation_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    recommendation_font.setPixelSize(soa::ui::layout::scaled(14, w));
    recommendation_font.setWeight(QFont::Medium);
    recommendation_body->setFont(recommendation_font);
    recommendation_body->setGeometry(local_rect(w, {50, 116, 580, 162}));
    auto* shadow = new QGraphicsDropShadowEffect(recommendation_body);
    shadow->setBlurRadius(soa::ui::layout::scaled(18, w));
    shadow->setOffset(0, soa::ui::layout::scaled(5, w));
    shadow->setColor(QColor(79, 23, 23, 48));
    recommendation_body->setGraphicsEffect(shadow);

    recommendation_title = new QLabel(this);
    recommendation_title->setTextFormat(Qt::PlainText);
    recommendation_title->setAlignment(Qt::AlignCenter);
    recommendation_title->setStyleSheet(QStringLiteral(
        "color:#4F1717; background:rgba(247,239,230,0.96); border:1px solid #D8C8B6;"
        " border-radius:%1px; padding:%2px %3px;")
        .arg(soa::ui::layout::scaled(12, w))
        .arg(soa::ui::layout::scaled(2, w))
        .arg(soa::ui::layout::scaled(14, w)));
    QFont recommendation_title_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    recommendation_title_font.setPixelSize(soa::ui::layout::scaled(15, w));
    recommendation_title_font.setWeight(QFont::Black);
    recommendation_title->setFont(recommendation_title_font);
    recommendation_title->setGeometry(local_rect(w, {160, 102, 360, 34}));
    recommendation_title->raise();

    continue_button = new QPushButton(QStringLiteral("CHECKING..."), this);
    continue_button->setCursor(Qt::PointingHandCursor);
    continue_button->setStyleSheet(primary_style(w));
    QFont primary_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    primary_font.setPixelSize(soa::ui::layout::scaled(16, w));
    primary_font.setWeight(QFont::Black);
    continue_button->setFont(primary_font);
    continue_button->setGeometry(local_rect(w, {60, 330, 370, 54}));
    continue_button->setAccessibleName(QStringLiteral("Use recommended setup"));
    continue_button->setEnabled(false);
    connect(continue_button, &QPushButton::clicked,
            this, &PrerequisitesIntro::apply_recommendation);

    choose_own_button = new QPushButton(QStringLiteral("CHOOSE MY OWN"), this);
    choose_own_button->setCursor(Qt::PointingHandCursor);
    choose_own_button->setStyleSheet(secondary_style(w));
    QFont secondary_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    secondary_font.setPixelSize(soa::ui::layout::scaled(13, w));
    secondary_font.setWeight(QFont::Black);
    choose_own_button->setFont(secondary_font);
    choose_own_button->setGeometry(local_rect(w, {440, 330, 180, 54}));
    choose_own_button->setAccessibleName(QStringLiteral("Choose Wine manually"));
    connect(choose_own_button, &QPushButton::clicked,
            this, &PrerequisitesIntro::choose_own_requested);
}

void PrerequisitesIntro::start_detection()
{
    if (detector->isRunning()) return;
    detection_complete = false;
    recommendation_title->setText(soa::i18n::translate("CHECKING"));
    recommendation_body->setStyleSheet(recommendation_style(window()->size()));
#if defined(Q_OS_MACOS)
    recommendation_body->setText(soa::i18n::translate(
        "Looking for a usable Wine setup. Nothing will be installed automatically."));
#else
    recommendation_body->setText(soa::i18n::translate(
        "Looking for a usable Wine or Proton setup. Nothing will be installed automatically."));
#endif
    continue_button->setText(soa::i18n::translate("CHECKING..."));
    continue_button->setEnabled(false);
    const auto detection = []()
    {
        DetectionResult result;
        result.profile = soa::runtime::detect_system_profile();
        result.runtimes = soa::runtime::WineRegistry::scan();
#if defined(Q_OS_MACOS)
        result.runtimes.erase(
            std::remove_if(result.runtimes.begin(), result.runtimes.end(),
                           [](const soa::runtime::WineInstall& runtime)
                           {
                               return runtime.type == soa::runtime::RuntimeType::Proton;
                           }),
            result.runtimes.end());
#endif
        result.winetricks_ready = soa::runtime::winetricks_available();
#if !defined(Q_OS_MACOS)
        result.umu_ready = soa::runtime::umu_available();
#endif
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
}

soa::runtime::RuntimeType PrerequisitesIntro::recommended_runtime() const
{
#if defined(Q_OS_MACOS)
    return soa::runtime::RuntimeType::Wine;
#else
    const bool proton_found = best_runtime(soa::runtime::RuntimeType::Proton) != nullptr;
    const bool wine_found = best_runtime(soa::runtime::RuntimeType::Wine) != nullptr;
    const bool proton_ready = proton_found && umu_ready && winetricks_ready;
    const bool wine_ready = wine_found && winetricks_ready;
    if (proton_ready) return soa::runtime::RuntimeType::Proton;
    if (wine_ready) return soa::runtime::RuntimeType::Wine;
    if (proton_found) return soa::runtime::RuntimeType::Proton;
    return soa::runtime::RuntimeType::Wine;
#endif
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

QStringList PrerequisitesIntro::missing_requirements(const soa::runtime::RuntimeType type) const
{
    QStringList missing;
    if (!best_runtime(type))
    {
        missing << (type == soa::runtime::RuntimeType::Proton
                        ? soa::i18n::translate("Proton")
                        : soa::i18n::translate("Wine"));
    }
#if !defined(Q_OS_MACOS)
    if (type == soa::runtime::RuntimeType::Proton && !umu_ready)
        missing << soa::i18n::translate("UMU");
#endif
    if (!winetricks_ready)
        missing << soa::i18n::translate("Winetricks");
    return missing;
}

bool PrerequisitesIntro::profile_ready(QString* blocker) const
{
    if (!detection_complete)
    {
        if (blocker) *blocker = QStringLiteral("The system check is still running.");
        return false;
    }

#if defined(Q_OS_LINUX)
    if (system_profile.cpu_architecture != soa::runtime::CpuArchitecture::X86_64)
    {
        if (blocker)
            *blocker = QStringLiteral("The Linux launcher and game currently require an x86_64 computer.");
        return false;
    }
#endif

#if defined(Q_OS_MACOS)
    if (system_profile.cpu_architecture == soa::runtime::CpuArchitecture::Arm64
        && !system_profile.rosetta_available)
    {
        const auto* runtime = best_runtime(soa::runtime::RuntimeType::Wine);
        if (runtime && runtime->requires_rosetta)
        {
            if (blocker)
                *blocker = QStringLiteral(
                    "An Intel Wine installation was found, but Rosetta is unavailable. "
                    "Request Rosetta, complete the macOS prompt, then rescan.");
            return false;
        }
    }
#endif

    const auto runtime_type = recommended_runtime();
    const QStringList missing = missing_requirements(runtime_type);
    if (missing.isEmpty()) return true;
    if (!blocker) return false;

    const QString missing_text = joined_requirements(missing);
    const QString install_wording = missing.size() == 1
        ? soa::i18n::translate("Install it")
        : soa::i18n::translate("Install them");
    if (runtime_type == soa::runtime::RuntimeType::Proton)
    {
        *blocker = QStringLiteral(
            "Proton needs Proton and UMU. Winetricks is also required for Alicia's Windows components.\n\nMissing: %1. %2, then restart the launcher.")
            .arg(missing_text, install_wording);
    }
    else
    {
#if defined(Q_OS_MACOS)
        *blocker = QStringLiteral(
            "Wine and Winetricks are required to prepare Alicia's Windows components.\n\nMissing: %1. %2, then restart the launcher.")
            .arg(missing_text, install_wording);
#else
        *blocker = QStringLiteral(
            "Pure Wine needs both Wine and Winetricks.\n\nMissing: %1. %2, then restart the launcher.")
            .arg(missing_text, install_wording);
#endif
    }
    return false;
}

void PrerequisitesIntro::update_recommendation()
{
    if (!detection_complete) return;

    const auto runtime_type = recommended_runtime();
    const QString runtime_label = runtime_type == soa::runtime::RuntimeType::Proton
        ? soa::i18n::translate("PROTON")
        : soa::i18n::translate("WINE");

    QString blocker;
    if (!profile_ready(&blocker))
    {
        recommendation_title->setText(soa::i18n::translate("%1 NEEDED").arg(runtime_label));
        recommendation_body->setStyleSheet(recommendation_style(window()->size(), true));
        recommendation_body->setText(soa::i18n::translate(blocker));
        continue_button->setText(soa::i18n::translate("USE THIS SETUP"));
        continue_button->setEnabled(false);
        choose_own_button->show();
        return;
    }

    const auto* runtime = best_runtime(runtime_type);
    choose_own_button->show();
    recommendation_title->setText(soa::i18n::translate("%1 READY").arg(runtime_label));
    recommendation_body->setStyleSheet(recommendation_style(window()->size()));
#if defined(Q_OS_MACOS)
    recommendation_body->setText(soa::i18n::translate(
        "%1 is the recommended Wine setup for this Mac. Alicia will use "
        "compatibility graphics and a 64-bit Wine prefix. Nothing will be "
        "installed automatically.")
        .arg(runtime ? runtime->name : runtime_label));
#else
    recommendation_body->setText(soa::i18n::translate(
        "%1 is the recommended setup for this computer. Alicia will use "
        "compatibility graphics by default.\n\nDXVK stays optional and can be "
        "enabled later in Settings. Nothing will be installed automatically.")
        .arg(runtime ? runtime->name : runtime_label));
#endif
    continue_button->setText(soa::i18n::translate("USE THIS SETUP"));
    continue_button->setEnabled(true);
}

void PrerequisitesIntro::apply_recommendation()
{
    QString blocker;
    if (!profile_ready(&blocker))
    {
        recommendation_body->setStyleSheet(recommendation_style(window()->size(), true));
        recommendation_body->setText(soa::i18n::translate(blocker));
        return;
    }

    const auto runtime_type = recommended_runtime();
    const auto* runtime = best_runtime(runtime_type);
    if (!runtime) return;

    auto& config = soa::config::Config::instance();
    config.begin_update();
    config.set_setup_runtime_preference(
        runtime_type == soa::runtime::RuntimeType::Proton
            ? QStringLiteral("proton") : QStringLiteral("wine"));
#if defined(Q_OS_MACOS)
    config.set_wine_arch(QStringLiteral("win64"));
    config.set_macos_compatibility_profile(QStringLiteral("default"));
#endif
    config.set_wine_binary(runtime->path);
    config.set_runtime_selected(true);
    config.end_update();
    emit accepted();
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
                     soa::i18n::translate("Recommended setup for this computer"));

    QFont note_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    note_font.setPixelSize(soa::ui::layout::scaled(12, w));
    note_font.setWeight(QFont::Medium);
    painter.setFont(note_font);
    painter.setPen(soa::ui::colors::k_text_caption);
#if defined(Q_OS_MACOS)
    const QString note = QStringLiteral(
        "You can still choose a different Wine installation manually.");
#else
    const QString note = QStringLiteral(
        "You can still choose a different runtime manually.");
#endif
    painter.drawText(local_rect(w, {80, 292, 520, 24}), Qt::AlignCenter,
                     soa::i18n::translate(note));
}
