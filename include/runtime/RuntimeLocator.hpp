#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>

namespace core::wine
{




    bool repair_doubled_proton_prefix(const QString& compat_data_root);

    struct RuntimeSettings
    {
        QString configured_runtime;
        QString prefix_root;
        QString proton_compat_data_root;
        QString wine_arch;
        QString wine_args;
        bool use_dxvk {};
    };

    class RuntimeLocator final
    {
    public:
        using SettingsProvider = std::function<RuntimeSettings()>;

        explicit RuntimeLocator(SettingsProvider settings_provider = SettingsProvider {});

        [[nodiscard]] RuntimeSettings settings() const;
        [[nodiscard]] QString wine_binary() const;
        [[nodiscard]] QString wineboot_binary() const;
        [[nodiscard]] bool runtime_is_proton() const;
        [[nodiscard]] QString proton_root() const;
        [[nodiscard]] QString proton_binary() const;
        [[nodiscard]] QString proton_wine_binary() const;
        [[nodiscard]] QString proton_wineserver_binary() const;
        [[nodiscard]] QString wineserver_binary() const;
        [[nodiscard]] QString steam_root() const;
        [[nodiscard]] QProcessEnvironment umu_environment() const;
        [[nodiscard]] QProcessEnvironment winetricks_environment() const;
        [[nodiscard]] static QProcessEnvironment make_umu_environment(
            const RuntimeSettings& settings, const QString& proton_root);
        [[nodiscard]] QString resolved_executable(const QString& program) const;
        [[nodiscard]] bool is_wine_installed() const;

        void apply_wine_environment(QProcessEnvironment& environment) const;

        static void apply_wine_environment_entries(QProcessEnvironment& environment,
                                                   const QString& entries);
        static void apply_runtime_environment_entries(QProcessEnvironment& environment,
                                                      const QStringList& entries);

    private:
        SettingsProvider settings_provider_;
    };
}
