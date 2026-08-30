#include "ui/ViewRouter.hpp"

namespace core::state
{
    View view_for(const Stage stage)
    {
        switch (stage)
        {
            case Stage::Probing:
            case Stage::CheckingUpdate:
                return View::Loading;

            case Stage::NeedsPrerequisites:
                return View::Prerequisites;

            case Stage::NeedsRuntime:
                return View::WineSelect;

            case Stage::NeedsPrefix:
            case Stage::PrefixBroken:
            case Stage::SettingUpPrefix:
                return View::WineInstall;

            case Stage::NeedsDownload:
            case Stage::NeedsUpdate:
            case Stage::Downloading:
            case Stage::Updating:
                return View::GameInstall;

            case Stage::NeedsRules:
                return View::Rules;

            case Stage::NeedsAuth:
            case Stage::Authenticating:
            case Stage::Launching:
            case Stage::Running:
            case Stage::Ready:
                return View::AliciaChooser;

            case Stage::Failed:
                return View::Error;
        }
        return View::Loading;
    }
}
