#include "i18n/LanguageManager.hpp"
#include "TranslationPrivate.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QEvent>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QTabWidget>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace
{
    constexpr auto k_text_source = "soa_i18n_text_source";
    constexpr auto k_window_title_source = "soa_i18n_window_title_source";
    constexpr auto k_tool_tip_source = "soa_i18n_tool_tip_source";
    constexpr auto k_status_tip_source = "soa_i18n_status_tip_source";
    constexpr auto k_accessible_name_source = "soa_i18n_accessible_name_source";
    constexpr auto k_accessible_description_source = "soa_i18n_accessible_description_source";
    constexpr auto k_placeholder_source = "soa_i18n_placeholder_source";
    constexpr auto k_group_title_source = "soa_i18n_group_title_source";
    constexpr auto k_combo_sources = "soa_i18n_combo_sources";
    constexpr auto k_tab_sources = "soa_i18n_tab_sources";

    void capture_property(QObject* object, const char* property_name,
                          const char* source_property_name)
    {
        if (object->property(source_property_name).isValid())
            return;
        const QString value = object->property(property_name).toString();
        if (!value.isEmpty())
            object->setProperty(source_property_name, value);
    }

    void apply_property(QObject* object, const char* property_name,
                        const char* source_property_name)
    {
        const QVariant source = object->property(source_property_name);
        if (source.isValid())
            object->setProperty(property_name, util::i18n::detail::translated(source.toString()));
    }
}

namespace util::i18n
{
    bool LanguageManager::eventFilter(QObject* watched, QEvent* event)
    {
        if (event && (event->type() == QEvent::Show || event->type() == QEvent::Polish))
        {
            if (auto* widget = qobject_cast<QWidget*>(watched))
            {
                QPointer<QWidget> guarded(widget);
                QTimer::singleShot(0, this, [this, guarded]()
                {
                    if (!guarded)
                        return;
                    capture_tree(guarded);
                    retranslate_tree(guarded);
                });
            }
        }
        return QObject::eventFilter(watched, event);
    }

    void LanguageManager::register_tree(QObject* root)
    {
        if (!root)
            return;
        QObject* const registered_root = root;
        for (QObject* registered : std::as_const(registered_roots))
        {
            if (registered == root)
                return;
        }
        registered_roots.push_back(root);
        connect(root, &QObject::destroyed, this, [this, registered_root]()
        {
            registered_roots.erase(
                std::remove(registered_roots.begin(), registered_roots.end(), registered_root),
                registered_roots.end());
        });
        capture_tree(root);
        retranslate_tree(root);
    }

    void LanguageManager::capture_tree(QObject* root)
    {
        capture_object(root);
        const auto descendants = root->findChildren<QObject*>();
        for (QObject* object : descendants)
            capture_object(object);
    }

    void LanguageManager::capture_object(QObject* object)
    {
        if (!object)
            return;

        capture_property(object, "windowTitle", k_window_title_source);
        capture_property(object, "toolTip", k_tool_tip_source);
        capture_property(object, "statusTip", k_status_tip_source);
        capture_property(object, "accessibleName", k_accessible_name_source);
        capture_property(object, "accessibleDescription", k_accessible_description_source);

        if (qobject_cast<QLabel*>(object) || qobject_cast<QAbstractButton*>(object)
            || qobject_cast<QAction*>(object))
        {
            capture_property(object, "text", k_text_source);
        }
        if (qobject_cast<QGroupBox*>(object) || qobject_cast<QMenu*>(object))
            capture_property(object, "title", k_group_title_source);
        if (qobject_cast<QLineEdit*>(object))
            capture_property(object, "placeholderText", k_placeholder_source);

        if (auto* combo = qobject_cast<QComboBox*>(object))
        {
            if (!combo->property(k_combo_sources).isValid())
            {
                QStringList sources;
                for (int index = 0; index < combo->count(); ++index)
                    sources.push_back(combo->itemText(index));
                combo->setProperty(k_combo_sources, sources);
            }
        }

        if (auto* tabs = qobject_cast<QTabWidget*>(object))
        {
            if (!tabs->property(k_tab_sources).isValid())
            {
                QStringList sources;
                for (int index = 0; index < tabs->count(); ++index)
                    sources.push_back(tabs->tabText(index));
                tabs->setProperty(k_tab_sources, sources);
            }
        }
    }

    void LanguageManager::retranslate_registered()
    {
        applying = true;
        for (QObject* root : std::as_const(registered_roots))
        {
            if (root)
            {
                capture_tree(root);
                retranslate_tree(root);
            }
        }
        applying = false;
    }

    void LanguageManager::retranslate_tree(QObject* root)
    {
        retranslate_object(root);
        const auto descendants = root->findChildren<QObject*>();
        for (QObject* object : descendants)
            retranslate_object(object);
        if (auto* widget = qobject_cast<QWidget*>(root))
        {
            widget->update();
            const auto child_widgets = widget->findChildren<QWidget*>();
            for (QWidget* child : child_widgets)
                child->update();
        }
    }

    void LanguageManager::retranslate_object(QObject* object)
    {
        if (!object)
            return;

        apply_property(object, "windowTitle", k_window_title_source);
        apply_property(object, "toolTip", k_tool_tip_source);
        apply_property(object, "statusTip", k_status_tip_source);
        apply_property(object, "accessibleName", k_accessible_name_source);
        apply_property(object, "accessibleDescription", k_accessible_description_source);
        apply_property(object, "text", k_text_source);
        apply_property(object, "title", k_group_title_source);
        apply_property(object, "placeholderText", k_placeholder_source);

        if (auto* combo = qobject_cast<QComboBox*>(object))
        {
            const QStringList sources = combo->property(k_combo_sources).toStringList();
            for (int index = 0; index < qMin(combo->count(), sources.size()); ++index)
                combo->setItemText(index, detail::translated(sources[index]));
        }

        if (auto* tabs = qobject_cast<QTabWidget*>(object))
        {
            const QStringList sources = tabs->property(k_tab_sources).toStringList();
            for (int index = 0; index < qMin(tabs->count(), sources.size()); ++index)
                tabs->setTabText(index, detail::translated(sources[index]));
        }
    }

}
