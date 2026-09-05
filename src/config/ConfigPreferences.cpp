#include "ConfigPrivate.hpp"

namespace util::config
{
    bool Config::prerequisites_confirmed() const
    {
        return d->values.value(QStringLiteral("prerequisites_confirmed")).toBool()
            && d->values.value(QStringLiteral("setup_assistant_version")).toInt() >= 1;
    }

    bool Config::rules_accepted() const { return d->values.value(QStringLiteral("rules_accepted")).toBool(); }

    bool Config::launch_on_startup() const { return d->values.value(QStringLiteral("launch_on_startup")).toBool(); }

    QString Config::after_game_start() const
    {
        const QString value = d->values.value(QStringLiteral("after_game_start")).toString();
        return value.isEmpty() ? QStringLiteral("keep") : value;
    }

    QString Config::launcher_size() const
    {
        const QString value = d->values.value(QStringLiteral("launcher_size")).toString();
        return value.isEmpty() ? QStringLiteral("1400x846") : value;
    }

    QString Config::language() const
    {
        return normalized_language(d->values.value(QStringLiteral("language")).toString());
    }

    void Config::set_prerequisites_confirmed(const bool value)
    {
        const bool assistantAlreadyCurrent = d->values.value(QStringLiteral("setup_assistant_version")).toInt() == 1;
        if (prerequisites_confirmed() == value && (!value || assistantAlreadyCurrent))
            return;
        d->values[QStringLiteral("prerequisites_confirmed")] = value;
        if (value)
            d->values[QStringLiteral("setup_assistant_version")] = 1;
        persist_change();
    }

    void Config::set_rules_accepted(const bool value)
    {
        if (rules_accepted() == value) return;
        d->values[QStringLiteral("rules_accepted")] = value; persist_change();
    }

    void Config::set_launch_on_startup(const bool value)
    {
        if (launch_on_startup() == value) return;
        d->values[QStringLiteral("launch_on_startup")] = value; persist_change();
    }

    void Config::set_after_game_start(const QString& value)
    {
        const QString normalized = normalized_after_launch(value);
        if (after_game_start() == normalized) return;
        d->values[QStringLiteral("after_game_start")] = normalized; persist_change();
    }

    void Config::set_launcher_size(const QString& value)
    {
        const QString normalized = normalized_launcher_size(value);
        if (launcher_size() == normalized) return;
        d->values[QStringLiteral("launcher_size")] = normalized; persist_change();
    }

    void Config::set_language(const QString& value)
    {
        const QString normalized = normalized_language(value);
        if (language() == normalized) return;
        d->values[QStringLiteral("language")] = normalized; persist_change();
    }

}
