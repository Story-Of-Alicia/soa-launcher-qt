#pragma once

#include <QString>

namespace core::game
{
    enum class GameVersion
    {
        Playtest,
        Alicia2
    };

    struct GameVersionProfile
    {
        const char* display_name;
        const char* cdn_base_url;
        const char* default_install_directory;
        const char* default_install_subdirectory;
        const char* executable_name;
        const char* install_marker_file;
        const char* launch_game_id;
        const char* first_launch_registry_value;
        const char* video_settings_registry_key;
        const char* audio_settings_registry_key;
        const char* audio_settings_setup_registry_value;
        bool supports_dxvk;
    };

    inline constexpr GameVersionProfile k_playtest_profile
    {
        "Alicia",
        "https://r2.storyofalicia.com/game",
        "Story Of Alicia",
        "game",
        "Alicia.exe",
        "version.json",
        "4",
        "WasLaunchedPreviously",
        "Software\\Ntreev\\Alicia\\Window",
        "",
        "",
        true
    };

    inline constexpr GameVersionProfile k_alicia_2_profile
    {
        "Alicia 2.0",
        "https://r2.storyofalicia.com/game2",
        "Story Of Alicia 2.0",
        "game",
        "Alicia.exe",
        "version.json",
        "4",
        "WasLaunchedPreviously2_0",
        "Software\\Ntreev\\Alicia20\\Window",
        "Software\\Ntreev\\Alicia20\\Option",
        "WereAudioDefaultsApplied2_0",
        true
    };

    inline const GameVersionProfile& profile(const GameVersion version)
    {
        return version == GameVersion::Alicia2
            ? k_alicia_2_profile
            : k_playtest_profile;
    }

    inline QString to_string(const GameVersion version)
    {
        switch (version)
        {
            case GameVersion::Playtest: return QStringLiteral("1.0");
            case GameVersion::Alicia2:  return QStringLiteral("2.0");
        }
        return QStringLiteral("1.0");
    }

    inline GameVersion game_version_from_string(const QString& value)
    {
        return value.compare(QStringLiteral("2.0"), Qt::CaseInsensitive) == 0
            ? GameVersion::Alicia2
            : GameVersion::Playtest;
    }
}
