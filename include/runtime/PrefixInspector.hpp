#pragma once

#include <QString>
#include <QtGlobal>
#include <QStringList>

namespace core::wine
{
    enum class PrefixArchitecture
    {
        Unknown,
        Win32,
        Win64
    };

    struct PrefixInspection
    {
        bool exists {};
        bool marker_valid {};
        bool d3dx9_31 {};
        bool d3dx9_42 {};
        bool d3dx9_43 {};
        bool d3dcompiler_42 {};
        bool d3dcompiler_47 {};
        bool msvc2010_runtime {};
        bool physx_runtime {};
        bool msvc_runtime {};
        bool dxvk_files_present {};
        bool dxvk_overrides_present {};
        bool dxvk_installed {};
        bool structure_valid {};
        PrefixArchitecture architecture {PrefixArchitecture::Unknown};

        [[nodiscard]] bool required_components_present(bool proton) const
        {
#if defined(Q_OS_MACOS)
            Q_UNUSED(proton);




            return structure_valid && architecture != PrefixArchitecture::Win32;
#else
            return d3dx9_43 && d3dcompiler_47 && (proton || msvc_runtime);
#endif
        }
    };

    class PrefixInspector
    {
    public:
        static PrefixArchitecture architecture(const QString& prefix);
        static QString game_dll_directory(const QString& prefix);
        static bool component_exists(const QString& prefix, const QString& file_name);
        static bool dxvk_installed(const QString& prefix);
        static QString dxvk_winetricks_verb();
        static bool marker_valid(const QString& prefix, const QString& runtime);
        static bool write_marker(const QString& prefix, const QString& runtime);
        static bool remove_marker(const QString& prefix);
        static PrefixInspection inspect(const QString& prefix, const QString& runtime, bool proton);
        static QStringList missing_packages(const QString& prefix, bool proton, bool request_dxvk);
    };
}
