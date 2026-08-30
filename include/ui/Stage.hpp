#pragma once

namespace core::state
{
    enum class Stage
    {
        Probing,
        NeedsPrerequisites,
        NeedsRuntime,
        NeedsPrefix,
        PrefixBroken,
        SettingUpPrefix,
        NeedsDownload,
        CheckingUpdate,
        Downloading,
        NeedsUpdate,
        Updating,
        NeedsRules,
        NeedsAuth,
        Authenticating,
        Launching,
        Running,
        Ready,
        Failed
    };

    inline const char* to_string(const Stage stage)
    {
        switch (stage)
        {
            case Stage::Probing:         return "Probing";
            case Stage::NeedsPrerequisites: return "NeedsPrerequisites";
            case Stage::NeedsRuntime:    return "NeedsRuntime";
            case Stage::NeedsPrefix:     return "NeedsPrefix";
            case Stage::PrefixBroken:    return "PrefixBroken";
            case Stage::SettingUpPrefix: return "SettingUpPrefix";
            case Stage::NeedsDownload:   return "NeedsDownload";
            case Stage::CheckingUpdate:  return "CheckingUpdate";
            case Stage::Downloading:     return "Downloading";
            case Stage::NeedsUpdate:     return "NeedsUpdate";
            case Stage::Updating:        return "Updating";
            case Stage::NeedsRules:      return "NeedsRules";
            case Stage::NeedsAuth:       return "NeedsAuth";
            case Stage::Authenticating:  return "Authenticating";
            case Stage::Launching:       return "Launching";
            case Stage::Running:         return "Running";
            case Stage::Ready:           return "Ready";
            case Stage::Failed:          return "Failed";
        }
        return "Probing";
    }
}
