#pragma once

#include <QString>
#include <QVector>

namespace core::wine
{
    enum class RuntimeType
    {
        Wine,
        Proton
    };

    struct WineInstall
    {
        QString     name;
        QString     path;
        RuntimeType type { RuntimeType::Wine };
        QString version;
        QString architectures;
        QString issue;
        bool requires_rosetta {};
        bool rosetta_available {true};
        bool usable {};
    };

    QString winetricks_path();
    QString umu_path();
    bool    winetricks_available();
    bool    umu_available();

    class WineRegistry
    {
        public:
            static QVector<WineInstall> scan();
            static QVector<QString>& extra_search_dirs();
            static RuntimeType identify(const QString& path, bool* ok = nullptr);
            static bool inspect_path(const QString& path, WineInstall& install,
                                     QString* error = nullptr);
            static QString resolve_wine_executable(const QString& path);
            static bool proton_supports_umu_winetricks(const QString& path);
    };
}
