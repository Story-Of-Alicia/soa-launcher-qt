#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace core::wine
{
    struct WindowsProcessInfo
    {
        QString image_name;
        qint64 pid {-1};
        QString source_line;
    };



    [[nodiscard]] QString windows_path_for_prefix_file(const QString& prefix,
                                                       const QString& host_path,
                                                       QString* error = nullptr);

    [[nodiscard]] bool host_path_is_inside_prefix(const QString& prefix,
                                                  const QString& host_path);



    [[nodiscard]] QString decode_windows_process_output(const QByteArray& output);


    [[nodiscard]] std::optional<WindowsProcessInfo> find_windows_process(
        const QString& tasklist_output, const QString& image_name);



    [[nodiscard]] std::optional<WindowsProcessInfo> find_host_process(
        const QString& process_list_output, const QString& image_name);
}
