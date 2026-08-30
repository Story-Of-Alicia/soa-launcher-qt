#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace core::wine::macos
{
    struct RuntimeProbe
    {
        QString selected_path;
        QString executable;
        QString runtime_root;
        QString version;
        QStringList architectures;
        QString failure;
        bool exists {};
        bool executable_file {};
        bool apple_silicon_host {};
        bool requires_rosetta {};
        bool rosetta_available {};
        bool usable {};
    };

    [[nodiscard]] QString application_support_root();
    [[nodiscard]] QString default_prefix_root();
    [[nodiscard]] QString default_log_root();

    [[nodiscard]] QString resolve_wine_executable(const QString& selected_path);
    [[nodiscard]] QString runtime_root_for_executable(const QString& executable);
    [[nodiscard]] QStringList executable_architectures(const QString& executable);
    [[nodiscard]] bool executable_requires_rosetta(const QString& executable);
    [[nodiscard]] bool rosetta_is_available();
    [[nodiscard]] RuntimeProbe probe_runtime(const QString& selected_path,
                                             int timeout_ms = 5000);



    [[nodiscard]] QString prepare_host_launch(const QString& executable,
                                              QStringList& arguments);



    void apply_runtime_environment(QProcessEnvironment& environment,
                                   const QString& executable);



    [[nodiscard]] bool request_rosetta_install_prompt();
}
