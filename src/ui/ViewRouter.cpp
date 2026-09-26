#include "ui/ViewRouter.hpp"

namespace soa::ui
{
    View view_for(const Stage stage)
    {
        switch (stage)
        {
            case Stage::Probing:
                return View::Loading;

            case Stage::CheckingUpdate:
                return View::AliciaChooser;

            case Stage::NeedsPrerequisites:
                return View::Prerequisites;

            case Stage::NeedsRuntime:
                return View::WineSelect;

            case Stage::NeedsPrefix:
            case Stage::PrefixBroken:
            case Stage::SettingUpPrefix:
                return View::WineInstall;

            case Stage::NeedsDownload:
                return View::GameInstall;

            case Stage::Downloading:
                return View::Loading;

            case Stage::NeedsUpdate:
                return View::AliciaChooser;

            case Stage::Updating:
                return View::Loading;

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
