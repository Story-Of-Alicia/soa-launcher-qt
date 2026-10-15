// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include "MinHook.h"

namespace {
using DirectSoundCreateFn = HRESULT (WINAPI*)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN);
using DirectSoundCreate8Fn = HRESULT (WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);

DirectSoundCreateFn original_create = nullptr;
DirectSoundCreate8Fn original_create8 = nullptr;

HRESULT WINAPI reject_direct_sound_create(
    LPCGUID,
    LPDIRECTSOUND* output,
    LPUNKNOWN outer)
{
    if (output != nullptr) {
        *output = nullptr;
    }

    if (outer != nullptr) {
        return DSERR_INVALIDPARAM;
    }

    return DSERR_NODRIVER;
}

HRESULT WINAPI reject_direct_sound_create8(
    LPCGUID,
    LPDIRECTSOUND8* output,
    LPUNKNOWN outer)
{
    if (output != nullptr) {
        *output = nullptr;
    }

    if (outer != nullptr) {
        return DSERR_INVALIDPARAM;
    }

    return DSERR_NODRIVER;
}
}

extern "C" bool soa_register_null_dsound_hooks()
{
    wchar_t enabled[8]{};
    if (GetEnvironmentVariableW(L"SOA_AUDIO_NULL_BACKEND", enabled, 8) == 0 || enabled[0] != L'1') {
        return true;
    }

    const MH_STATUS create_status = MH_CreateHookApi(
        L"dsound.dll",
        "DirectSoundCreate",
        reinterpret_cast<void*>(reject_direct_sound_create),
        reinterpret_cast<void**>(&original_create));

    const MH_STATUS create8_status = MH_CreateHookApi(
        L"dsound.dll",
        "DirectSoundCreate8",
        reinterpret_cast<void*>(reject_direct_sound_create8),
        reinterpret_cast<void**>(&original_create8));

    const bool create_ok = create_status == MH_OK || create_status == MH_ERROR_ALREADY_CREATED;
    const bool create8_ok = create8_status == MH_OK ||
                            create8_status == MH_ERROR_ALREADY_CREATED ||
                            create8_status == MH_ERROR_FUNCTION_NOT_FOUND;

    return create_ok && create8_ok;
}
