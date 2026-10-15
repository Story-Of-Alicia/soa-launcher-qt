// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

namespace soa::ui
{
    enum class View
    {
        LanguageSelection,
        Loading,
        Prerequisites,
        WineSelect,
        WineInstall,
        GameInstall,
        Rules,
        AliciaChooser,
        Error
    };

    inline const char* to_string(View v)
    {
        switch (v)
        {
            case View::LanguageSelection: return "LanguageSelection";
            case View::Loading:       return "Loading";
            case View::Prerequisites: return "Prerequisites";
            case View::WineSelect:  return "WineSelect";
            case View::WineInstall: return "WineInstall";
            case View::GameInstall: return "GameInstall";
            case View::Rules:         return "Rules";
            case View::AliciaChooser: return "AliciaChooser";
            case View::Error:       return "Error";
        }
        return "Loading";
    }
}
