#include <QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFontMetrics>
#include <QLabel>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QWidget>

#include "ui/Assets.hpp"
#include "ui/DownloadProgress.hpp"
#include "ui/GameInstall.hpp"
#include "ui/ImageDropdown.hpp"
#include "ui/LanguageSelection.hpp"
#include "ui/LauncherUpdate.hpp"
#include "ui/Layout.hpp"
#include "ui/PrerequisitesIntro.hpp"
#include "ui/RepairFiles.hpp"
#include "ui/RulesAgreement.hpp"
#include "ui/Settings.hpp"
#include "ui/WineInstall.hpp"
#include "ui/WineSelectMenu.hpp"

namespace
{
    void add_supported_sizes()
    {
        QTest::addColumn<QSize>("launcher_size");
        QTest::newRow("small") << soa::ui::layout::win::k_small;
        QTest::newRow("default") << soa::ui::layout::win::k_default;
        QTest::newRow("large") << soa::ui::layout::win::k_large;
        QTest::newRow("extra-large") << soa::ui::layout::win::k_4k;
    }

    QString widget_name(const QWidget* widget)
    {
        if (!widget)
            return QStringLiteral("<null>");
        if (!widget->objectName().isEmpty())
            return QStringLiteral("%1[%2]")
                .arg(QString::fromLatin1(widget->metaObject()->className()), widget->objectName());
        return QString::fromLatin1(widget->metaObject()->className());
    }

    QString failure_image_path(const QString& screen, const QSize size)
    {
        QString name = screen.toLower();
        for (qsizetype index = 0; index < name.size(); ++index)
        {
            if (!name[index].isLetterOrNumber())
                name[index] = QLatin1Char('-');
        }

        const QString configured = qEnvironmentVariable("SOA_UI_TEST_OUTPUT_DIR");
        const QString directory = configured.isEmpty()
            ? QDir(QDir::tempPath()).filePath(QStringLiteral("soa-ui-layout-failures"))
            : configured;
        QDir().mkpath(directory);
        return QDir(directory).filePath(
            QStringLiteral("%1-%2x%3.png").arg(name).arg(size.width()).arg(size.height()));
    }

    bool fail_layout(QWidget* root, const QString& screen, const QSize size,
                     const QString& detail, const char* file, const int line)
    {
        const QString screenshot = failure_image_path(screen, size);
        if (root)
            root->grab().save(screenshot);
        const QString message = QStringLiteral("[%1 %2x%3] %4\nScreenshot: %5")
            .arg(screen)
            .arg(size.width())
            .arg(size.height())
            .arg(detail, screenshot);
        QTest::qFail(qPrintable(message), file, line);
        return false;
    }

#define SOA_LAYOUT_FAIL(root, screen, size, detail) \
    fail_layout((root), (screen), (size), (detail), __FILE__, __LINE__)


    QString rect_text(const QRect& rect)
    {
        return QStringLiteral("[%1,%2 %3x%4]")
            .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
    }

    bool rect_inside(const QRect& outer, const QRect& inner)
    {
        return inner.width() > 0 && inner.height() > 0
            && outer.contains(inner.topLeft()) && outer.contains(inner.bottomRight());
    }

    bool verify_plain_label(QLabel* label, QWidget* root, const QString& screen,
                            const QSize size)
    {
        if (!label || label->text().isEmpty() || label->textFormat() == Qt::RichText)
            return true;
        if (label->text().contains(QLatin1Char('<')) || label->text().contains(QLatin1Char('\n')))
            return true;

        const QRect contents = label->contentsRect();
        if (contents.width() <= 0 || contents.height() <= 0)
            return SOA_LAYOUT_FAIL(root, screen, size,
                QStringLiteral("%1 has no usable text area: %2")
                    .arg(widget_name(label), rect_text(label->geometry())));

        const QFontMetrics metrics(label->font());
        if (label->wordWrap())
        {
            const QRect needed = metrics.boundingRect(
                QRect(0, 0, contents.width(), 10000),
                static_cast<int>(label->alignment()) | Qt::TextWordWrap,
                label->text());
            if (needed.height() > contents.height() + 2)
            {
                return SOA_LAYOUT_FAIL(root, screen, size,
                    QStringLiteral("%1 text is clipped: needs %2px high, has %3px. Text: %4")
                        .arg(widget_name(label))
                        .arg(needed.height())
                        .arg(contents.height())
                        .arg(label->text()));
            }
            return true;
        }

        if (metrics.horizontalAdvance(label->text()) > contents.width() + 2)
        {
            return SOA_LAYOUT_FAIL(root, screen, size,
                QStringLiteral("%1 text is clipped: needs %2px wide, has %3px. Text: %4")
                    .arg(widget_name(label))
                    .arg(metrics.horizontalAdvance(label->text()))
                    .arg(contents.width())
                    .arg(label->text()));
        }
        return true;
    }

    bool verify_button_text(QAbstractButton* button, QWidget* root, const QString& screen,
                            const QSize size)
    {
        if (!button || button->text().isEmpty() || button->text() == QStringLiteral("..."))
            return true;
        const QFontMetrics metrics(button->font());
        const int available = qMax(1, button->contentsRect().width()
            - soa::ui::layout::scaled(12, size));
        if (metrics.horizontalAdvance(button->text()) <= available)
            return true;
        return SOA_LAYOUT_FAIL(root, screen, size,
            QStringLiteral("%1 text is clipped: needs %2px wide, has %3px. Text: %4")
                .arg(widget_name(button))
                .arg(metrics.horizontalAdvance(button->text()))
                .arg(available)
                .arg(button->text()));
    }

    bool verify_widget_tree(QWidget* root, const QString& screen, const QSize size)
    {
        if (!root)
            return false;

        root->ensurePolished();
        QApplication::sendPostedEvents();
        QApplication::processEvents(QEventLoop::AllEvents, 10);

        if (root->parentWidget() && root->geometry() != root->parentWidget()->rect())
        {
            return SOA_LAYOUT_FAIL(root, screen, size,
                QStringLiteral("root geometry %1 does not match launcher rect %2")
                    .arg(rect_text(root->geometry()), rect_text(root->parentWidget()->rect())));
        }

        const auto widgets = root->findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively);
        for (QWidget* widget : widgets)
        {
            if (!widget || widget->isWindow() || widget->isHidden())
                continue;

            QWidget* parent = widget->parentWidget();
            if (!parent)
                continue;

            const QRect geometry = widget->geometry();
            if (geometry.width() <= 0 || geometry.height() <= 0)
            {
                return SOA_LAYOUT_FAIL(root, screen, size,
                    QStringLiteral("%1 has invalid geometry %2")
                        .arg(widget_name(widget), rect_text(geometry)));
            }

            if (!rect_inside(parent->rect(), geometry))
            {
                return SOA_LAYOUT_FAIL(root, screen, size,
                    QStringLiteral("%1 geometry %2 escapes parent %3 rect %4")
                        .arg(widget_name(widget), rect_text(geometry),
                             widget_name(parent), rect_text(parent->rect())));
            }

            if (auto* label = qobject_cast<QLabel*>(widget))
            {
                if (!verify_plain_label(label, root, screen, size))
                    return false;
            }
            if (auto* button = qobject_cast<QAbstractButton*>(widget))
            {
                if (!verify_button_text(button, root, screen, size))
                    return false;
            }
        }
        return true;
    }

    bool verify_dropdowns_expanded(QWidget* root, const QString& screen, const QSize size)
    {
        const auto dropdowns = root->findChildren<ImageDropdown*>();
        for (ImageDropdown* dropdown : dropdowns)
        {
            if (!dropdown || dropdown->isHidden() || !dropdown->isEnabled())
                continue;

            QTest::mouseClick(dropdown, Qt::LeftButton, Qt::NoModifier, dropdown->rect().center());
            QApplication::processEvents(QEventLoop::AllEvents, 10);
            if (!verify_widget_tree(root, screen + QStringLiteral("DropdownOpen"), size))
                return false;
            QTest::keyClick(dropdown, Qt::Key_Escape);
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return true;
    }

    bool verify_absolute_rect(QWidget* root, const QString& screen, const QSize size,
                              const QString& name, const QRect& rect)
    {
        const QRect window_rect(QPoint(0, 0), size);
        if (rect_inside(window_rect, rect))
            return true;
        return SOA_LAYOUT_FAIL(root, screen, size,
            QStringLiteral("layout rect %1=%2 escapes launcher rect %3")
                .arg(name, rect_text(rect), rect_text(window_rect)));
    }

    bool verify_local_rect(QWidget* root, const QString& screen, const QSize size,
                           const QString& name, const QSize& parent_size, const QRect& rect)
    {
        const QRect parent_rect(QPoint(0, 0), parent_size);
        if (rect_inside(parent_rect, rect))
            return true;
        return SOA_LAYOUT_FAIL(root, screen, size,
            QStringLiteral("local layout rect %1=%2 escapes parent rect %3")
                .arg(name, rect_text(rect), rect_text(parent_rect)));
    }
}

class UiLayoutTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        soa::ui::assets::load_all();
    }

    void layout_contracts_fit_supported_sizes_data()
    {
        add_supported_sizes();
    }

    void layout_contracts_fit_supported_sizes()
    {
        QFETCH(QSize, launcher_size);
        QWidget host;
        host.resize(launcher_size);

        const QString screen = QStringLiteral("LayoutContracts");
        const auto absolute = [&](const QString& name, const QRect& rect)
        {
            return verify_absolute_rect(&host, screen, launcher_size, name, rect);
        };

        if (!absolute(QStringLiteral("region"), soa::ui::layout::region::rect(launcher_size))) return;
        if (!absolute(QStringLiteral("chrome.menu"), soa::ui::layout::chrome::menu(launcher_size))) return;
        if (!absolute(QStringLiteral("chrome.close"), soa::ui::layout::chrome::close(launcher_size))) return;
        if (!absolute(QStringLiteral("chrome.minimize"), soa::ui::layout::chrome::minimize(launcher_size))) return;
        if (!absolute(QStringLiteral("chrome.playtest"), soa::ui::layout::chrome::playtest_button(launcher_size))) return;
        if (!absolute(QStringLiteral("chrome.alicia2"), soa::ui::layout::chrome::alicia_2_button(launcher_size))) return;
        if (!absolute(QStringLiteral("chrome.version"), soa::ui::layout::chrome::version(launcher_size))) return;
        if (!absolute(QStringLiteral("chrome.versionArt"), soa::ui::layout::chrome::version_art(launcher_size))) return;
        if (!absolute(QStringLiteral("aliciaChooser"), soa::ui::layout::alicia_chooser::rect(launcher_size))) return;
        if (!absolute(QStringLiteral("settings"), soa::ui::layout::settings::box_rect(launcher_size))) return;
        if (!absolute(QStringLiteral("settings.expanded"), soa::ui::layout::settings::box_rect(launcher_size, true))) return;
        for (int tab = 0; tab < 3; ++tab)
        {
            if (!absolute(QStringLiteral("settings.tab%1").arg(tab),
                          soa::ui::layout::settings::tab_rect(launcher_size, tab))) return;
            if (!absolute(QStringLiteral("settings.expandedTab%1").arg(tab),
                          soa::ui::layout::settings::tab_rect(launcher_size, tab, true))) return;
        }
        if (!absolute(QStringLiteral("installModal"), soa::ui::layout::install_modal::rect(launcher_size))) return;
        if (!absolute(QStringLiteral("installModal.close"), soa::ui::layout::install_modal::close(launcher_size))) return;
        if (!absolute(QStringLiteral("installModal.cancel"), soa::ui::layout::install_modal::cancel_button(launcher_size))) return;
        if (!absolute(QStringLiteral("installModal.install"), soa::ui::layout::install_modal::install_button(launcher_size))) return;
        if (!absolute(QStringLiteral("progressModal"), soa::ui::layout::progress_modal::rect(launcher_size))) return;
        if (!absolute(QStringLiteral("progressModal.close"), soa::ui::layout::progress_modal::close(launcher_size))) return;
        if (!absolute(QStringLiteral("progressModal.pause"), soa::ui::layout::progress_modal::pause(launcher_size))) return;
        if (!absolute(QStringLiteral("progressModal.retry"), soa::ui::layout::progress_modal::retry_button(launcher_size))) return;
        if (!absolute(QStringLiteral("progressModal.details"), soa::ui::layout::progress_modal::details_button(launcher_size))) return;

        const QSize chooser = soa::ui::layout::alicia_chooser::box(launcher_size);
        const auto chooser_local = [&](const QString& name, const QRect& rect)
        {
            return verify_local_rect(&host, screen, launcher_size, name, chooser, rect);
        };
        if (!chooser_local(QStringLiteral("chooser.title"), soa::ui::layout::alicia_chooser::title(launcher_size))) return;
        if (!chooser_local(QStringLiteral("chooser.settings"), soa::ui::layout::alicia_chooser::settings_button(launcher_size))) return;
        if (!chooser_local(QStringLiteral("chooser.message"), soa::ui::layout::alicia_chooser::message(launcher_size))) return;
        if (!chooser_local(QStringLiteral("chooser.discord"), soa::ui::layout::alicia_chooser::discord_button(launcher_size))) return;
        if (!chooser_local(QStringLiteral("chooser.disclaimer"), soa::ui::layout::alicia_chooser::disclaimer(launcher_size))) return;
        if (!chooser_local(QStringLiteral("chooser.enter"), soa::ui::layout::alicia_chooser::enter_button(launcher_size))) return;
        if (!chooser_local(QStringLiteral("chooser.reset"), soa::ui::layout::alicia_chooser::reset(launcher_size))) return;

        const QSize settings = soa::ui::layout::settings::box(launcher_size);
        const auto settings_local = [&](const QString& name, const QRect& rect)
        {
            return verify_local_rect(&host, screen, launcher_size, name, settings, rect);
        };
        for (int row = 0; row < 4; ++row)
        {
            const int y = soa::ui::layout::launcher_settings::row(row);
            if (!settings_local(QStringLiteral("launcherSettings.row%1.title").arg(row),
                                soa::ui::layout::settings::row_title(launcher_size, y))) return;
            if (!settings_local(QStringLiteral("launcherSettings.row%1.description").arg(row),
                                soa::ui::layout::settings::row_desc(launcher_size, y))) return;
        }
        if (!settings_local(QStringLiteral("launcherSettings.connectivity"),
                            soa::ui::layout::launcher_settings::connectivity_results(launcher_size))) return;
    }

    void real_screens_fit_supported_sizes_data()
    {
        add_supported_sizes();
    }

    void real_screens_fit_supported_sizes()
    {
        QFETCH(QSize, launcher_size);
        QWidget host;
        host.resize(launcher_size);

        {
            LanguageSelection screen(&host);
            if (!verify_widget_tree(&screen, QStringLiteral("LanguageSelection"), launcher_size)) return;
            if (!verify_dropdowns_expanded(&screen, QStringLiteral("LanguageSelection"), launcher_size)) return;
        }
        {
            PrerequisitesIntro screen(&host);
            if (!verify_widget_tree(&screen, QStringLiteral("PrerequisitesIntro"), launcher_size)) return;
        }
        {
            Settings screen(nullptr, &host);
            auto* stack = screen.findChild<QStackedWidget*>();
            if (!stack)
            {
                SOA_LAYOUT_FAIL(&screen, QStringLiteral("Settings"), launcher_size,
                                QStringLiteral("settings page stack was not created"));
                return;
            }
            for (int index = 0; index < stack->count(); ++index)
            {
                stack->setCurrentIndex(index);
                QApplication::processEvents(QEventLoop::AllEvents, 10);
                const QString page = QStringLiteral("SettingsPage%1").arg(index);
                if (!verify_widget_tree(&screen, page, launcher_size)) return;
                if (!verify_dropdowns_expanded(&screen, page, launcher_size)) return;
            }
        }
        {
            DownloadProgress screen(DownloadProgress::Mode::Download, &host);
            if (!verify_widget_tree(&screen, QStringLiteral("DownloadProgress"), launcher_size)) return;
        }
        {
            DownloadProgress screen(DownloadProgress::Mode::Repair, &host);
            if (!verify_widget_tree(&screen, QStringLiteral("RepairProgress"), launcher_size)) return;
        }
        {
            GameInstall screen(nullptr, &host);
            if (!verify_widget_tree(&screen, QStringLiteral("GameInstall"), launcher_size)) return;
        }
        {
            WineInstall screen(nullptr, &host);
            if (!verify_widget_tree(&screen, QStringLiteral("WineInstall"), launcher_size)) return;
        }
        {
            RepairFiles screen(&host);
            if (!verify_widget_tree(&screen, QStringLiteral("RepairFiles"), launcher_size)) return;
        }
        {
            LauncherUpdate screen(&host);
            if (!verify_widget_tree(&screen, QStringLiteral("LauncherUpdate"), launcher_size)) return;
        }
        {
            WineSelectMenu screen(&host);
            if (!verify_widget_tree(&screen, QStringLiteral("WineSelectMenu"), launcher_size)) return;
        }
        {
            RulesAgreement screen(&host);
            if (!verify_widget_tree(&screen, QStringLiteral("RulesAgreement"), launcher_size)) return;
        }
    }
};

QTEST_MAIN(UiLayoutTests)
#include "ui_layout_tests.moc"
