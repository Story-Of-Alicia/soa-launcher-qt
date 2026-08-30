#include "ui/LauncherLog.hpp"
#include "common/QtLogSink.hpp"
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QClipboard>
#include <QApplication>
#include <QScrollBar>
#include <QTextDocument>
#include <QStringList>
#include "i18n/LanguageManager.hpp"

#include "config/Config.hpp"
#include "ui/Layout.hpp"

namespace
{
    QSize launcher_reference_size()
    {
        if (QWidget* active = QApplication::activeWindow();
            active && active->width() >= 640 && active->height() >= 360)
        {
            return active->size();
        }

        const QString configured = util::config::Config::instance().launcher_size();
        const QStringList parts = configured.split(QLatin1Char('x'));
        if (parts.size() == 2)
        {
            bool width_ok = false;
            bool height_ok = false;
            const int width = parts[0].toInt(&width_ok);
            const int height = parts[1].toInt(&height_ok);
            if (width_ok && height_ok && width > 0 && height > 0)
                return {width, height};
        }
        return util::layout::win::k_default;
    }
}

LauncherLog* LauncherLog::instance()
{
    static LauncherLog* s_instance = nullptr;
    if (!s_instance)
    {
        s_instance = new LauncherLog();
        connect(&LogBridge::instance(), &LogBridge::message, s_instance, &LauncherLog::append_line);
    }
    return s_instance;
}

LauncherLog::LauncherLog(QWidget* parent) : QDialog(parent)
{
    const QSize reference = launcher_reference_size();
    resize(util::layout::scaled(QSize(680, 420), reference));

    output = new QPlainTextEdit(this);
    output->setReadOnly(true);
    output->document()->setMaximumBlockCount(5000);
    output->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background:#1E1B17; color:#D8C9B8;"
        " font-family:'monospace'; font-size:%1px; border:none; }")
        .arg(qMax(9, util::layout::scaled(12, reference))));

    clear_button = new QPushButton(this);
    copy_button = new QPushButton(this);
    autoscroll_button = new QPushButton(this);

    QFont control_font;
    control_font.setPixelSize(qMax(9, util::layout::scaled(12, reference)));
    clear_button->setFont(control_font);
    copy_button->setFont(control_font);
    autoscroll_button->setFont(control_font);

    verbosity = new QComboBox(this);
    verbosity->setFont(control_font);
    verbosity->addItems({QString(), QString(), QString()});
    verbosity->setCurrentIndex(1);
    connect(verbosity, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int i)
        {
            min_level = (i == 0) ? 4 : (i == 1) ? 2 : 1;
            rerender();
        });

    connect(clear_button, &QPushButton::clicked, this, [this]()
    {
        entries.clear();
        output->clear();
    });
    connect(copy_button, &QPushButton::clicked, this, [this]()
    {
        QApplication::clipboard()->setText(output->toPlainText());
    });
    connect(autoscroll_button, &QPushButton::clicked, this, [this]()
    {
        autoscroll = !autoscroll;
        retranslate();
        if (autoscroll)
            output->verticalScrollBar()->setValue(output->verticalScrollBar()->maximum());
    });

    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed, this, [this]()
    {
        retranslate();
    });

    auto* bar = new QHBoxLayout;
    bar->setSpacing(util::layout::scaled(8, reference));
    bar->addWidget(clear_button);
    bar->addWidget(copy_button);
    bar->addWidget(autoscroll_button);
    bar->addStretch();
    bar->addWidget(verbosity);

    auto* root = new QVBoxLayout(this);
    const int margin = util::layout::scaled(10, reference);
    root->setContentsMargins(margin, margin, margin, margin);
    root->setSpacing(util::layout::scaled(8, reference));
    root->addLayout(bar);
    root->addWidget(output);
    retranslate();
}

void LauncherLog::retranslate()
{
    using util::i18n::translate;
    setWindowTitle(translate("Launcher Log"));
    clear_button->setText(translate("Clear"));
    copy_button->setText(translate("Copy"));
    autoscroll_button->setText(translate(
        autoscroll ? "Autoscroll: On" : "Autoscroll: Off"));
    const int selected = verbosity->currentIndex();
    verbosity->blockSignals(true);
    verbosity->setItemText(0, translate("Errors only"));
    verbosity->setItemText(1, translate("Normal"));
    verbosity->setItemText(2, translate("Verbose"));
    verbosity->setCurrentIndex(selected);
    verbosity->blockSignals(false);
}

void LauncherLog::append_line(int level, const QString& text)
{
    QString safeText = text;
    const QString token = util::config::Config::instance().token();
    if (!token.isEmpty())
        safeText.replace(token, QStringLiteral("[REDACTED]"));

    entries.push_back({ level, safeText });
    constexpr qsizetype k_max_entries = 5000;
    if (entries.size() > k_max_entries)
        entries.remove(0, entries.size() - k_max_entries);

    if (passes(level))
    {
        output->appendPlainText(safeText);
        if (autoscroll)
            output->verticalScrollBar()->setValue(output->verticalScrollBar()->maximum());
    }

    constexpr int k_error_level = 4;
    if (level >= k_error_level)
    {
        showNormal();
        raise();
        activateWindow();
    }
}

void LauncherLog::rerender()
{
    output->clear();
    for (const Entry& e : entries)
        if (passes(e.level)) output->appendPlainText(e.text);
    if (autoscroll)
        output->verticalScrollBar()->setValue(output->verticalScrollBar()->maximum());
}
