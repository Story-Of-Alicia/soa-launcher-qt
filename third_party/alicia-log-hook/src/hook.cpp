
// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d9.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <iterator>

#include "MinHook.h"

namespace
{
    constexpr std::size_t k_max_hook_slots = 24;
    constexpr std::size_t k_max_message_bytes = 8192;
    constexpr LONGLONG k_max_log_bytes = 32LL * 1024LL * 1024LL;

    using VsnprintfFunction = int(__cdecl*)(char*, std::size_t, const char*, va_list);
    using OutputDebugStringAFunction = void(WINAPI*)(LPCSTR);
    using OutputDebugStringWFunction = void(WINAPI*)(LPCWSTR);
    using LoadLibraryAFunction = HMODULE(WINAPI*)(LPCSTR);
    using LoadLibraryWFunction = HMODULE(WINAPI*)(LPCWSTR);
    using Direct3DCreate9Function = IDirect3D9* (WINAPI*)(UINT);
    using CreateDeviceFunction = HRESULT (WINAPI*)(
        IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
        D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    using ResetFunction = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    using PresentFunction = HRESULT (WINAPI*)(
        IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    using GetAdapterModeCountFunction = UINT (WINAPI*)(IDirect3D9*, UINT, D3DFORMAT);
    using EnumAdapterModesFunction = HRESULT (WINAPI*)(
        IDirect3D9*, UINT, D3DFORMAT, UINT, D3DDISPLAYMODE*);

    HANDLE g_log = INVALID_HANDLE_VALUE;
    CRITICAL_SECTION g_log_lock;
    bool g_log_lock_ready = false;
    bool g_log_all = false;
    bool g_limit_reported = false;
    LONG g_initialization = 0;
    char g_redact[2048] {};
    VsnprintfFunction g_vsnprintf_originals[k_max_hook_slots] {};
    void* g_vsnprintf_targets[k_max_hook_slots] {};
    std::size_t g_vsnprintf_count = 0;
    OutputDebugStringAFunction g_output_debug_a = nullptr;
    OutputDebugStringWFunction g_output_debug_w = nullptr;
    LoadLibraryAFunction g_load_library_a = nullptr;
    LoadLibraryWFunction g_load_library_w = nullptr;
    Direct3DCreate9Function g_direct3d_create9 = nullptr;
    bool g_force_windowed = false;
    UINT g_window_width = 1024;
    UINT g_window_height = 720;

    constexpr std::size_t k_d3d9_vtable_entries = 17;
    constexpr std::size_t k_device_vtable_entries = 119;
    constexpr std::size_t k_max_d3d9_objects = 8;
    constexpr std::size_t k_max_device_objects = 16;
    constexpr std::size_t k_max_modes = 128;

    struct ModeEntry
    {
        D3DDISPLAYMODE mode;
    };

    struct D3D9HookRecord
    {
        IDirect3D9* object = nullptr;
        void** original_vtable = nullptr;
        void** hooked_vtable = nullptr;
        GetAdapterModeCountFunction get_adapter_mode_count = nullptr;
        EnumAdapterModesFunction enum_adapter_modes = nullptr;
        CreateDeviceFunction create_device = nullptr;
        ModeEntry modes[k_max_modes] {};
        std::size_t mode_count = 0;
        bool modes_ready = false;
        UINT mode_adapter = 0;
        D3DFORMAT mode_format = D3DFMT_UNKNOWN;
    };

    struct DeviceHookRecord
    {
        IDirect3DDevice9* object = nullptr;
        void** original_vtable = nullptr;
        void** hooked_vtable = nullptr;
        ResetFunction reset = nullptr;
        PresentFunction present = nullptr;
        LONG present_count = 0;
    };

    D3D9HookRecord g_d3d9_records[k_max_d3d9_objects] {};
    DeviceHookRecord g_device_records[k_max_device_objects] {};
    CRITICAL_SECTION g_d3d_lock;
    bool g_d3d_lock_ready = false;

    std::size_t bounded_length(const char* text, const std::size_t maximum)
    {
        if (!text)
            return 0;
        std::size_t length = 0;
        while (length < maximum && text[length] != '\0')
            ++length;
        return length;
    }

    void append_decimal(char*& output, const unsigned value, const unsigned width)
    {
        unsigned divisor = 1;
        for (unsigned index = 1; index < width; ++index)
            divisor *= 10;
        for (unsigned index = 0; index < width; ++index)
        {
            *output++ = static_cast<char>('0' + (value / divisor) % 10);
            divisor = divisor > 1 ? divisor / 10 : 1;
        }
    }

    void write_bytes_locked(const char* data, const std::size_t length)
    {
        if (g_log == INVALID_HANDLE_VALUE || !data || length == 0)
            return;

        LARGE_INTEGER size {};
        if (GetFileSizeEx(g_log, &size) && size.QuadPart >= k_max_log_bytes)
        {
            if (!g_limit_reported)
            {
                static constexpr char limit[] =
                    "[hook] log limit reached; further Alicia output was discarded\r\n";
                DWORD written = 0;
                WriteFile(g_log, limit, sizeof(limit) - 1, &written, nullptr);
                g_limit_reported = true;
            }
            return;
        }

        DWORD written = 0;
        WriteFile(g_log, data, static_cast<DWORD>(length), &written, nullptr);
    }

    void redact_copy(char* destination, const std::size_t capacity,
                     const char* source, const std::size_t source_length)
    {
        if (!destination || capacity == 0)
            return;
        destination[0] = '\0';
        if (!source || source_length == 0)
            return;

        static constexpr char replacement[] = "[REDACTED]";
        const std::size_t secret_length = bounded_length(g_redact, sizeof(g_redact));
        std::size_t input = 0;
        std::size_t output = 0;
        while (input < source_length && output + 1 < capacity)
        {
            if (secret_length > 0 && input + secret_length <= source_length &&
                std::memcmp(source + input, g_redact, secret_length) == 0)
            {
                const std::size_t available = capacity - output - 1;
                const std::size_t copied = std::min(available, sizeof(replacement) - 1);
                std::memcpy(destination + output, replacement, copied);
                output += copied;
                input += secret_length;
                continue;
            }
            const unsigned char byte = static_cast<unsigned char>(source[input++]);
            destination[output++] = byte == 0 ? '?' : static_cast<char>(byte);
        }
        destination[output] = '\0';
    }

    void log_message(const char* source, const char* message, std::size_t length,
                     bool require_bracket);

    void log_literal(const char* source, const char* message)
    {
        log_message(source, message, bounded_length(message, k_max_message_bytes - 1), false);
    }

    UINT read_window_dimension(const wchar_t* name, const UINT fallback)
    {
        wchar_t value[16] {};
        const DWORD length = GetEnvironmentVariableW(
            name, value, static_cast<DWORD>(std::size(value)));
        if (length == 0 || length >= std::size(value))
            return fallback;

        UINT parsed = 0;
        for (DWORD index = 0; index < length; ++index)
        {
            if (value[index] < L'0' || value[index] > L'9')
                return fallback;
            parsed = parsed * 10u + static_cast<UINT>(value[index] - L'0');
            if (parsed > 4096u)
                return fallback;
        }
        return parsed >= 320u ? parsed : fallback;
    }

    void force_windowed_parameters(D3DPRESENT_PARAMETERS& parameters, const HWND fallback_window)
    {
        parameters.Windowed = TRUE;
        parameters.FullScreen_RefreshRateInHz = 0;
        if (parameters.BackBufferWidth == 0 || parameters.BackBufferHeight == 0)
        {
            parameters.BackBufferWidth = g_window_width;
            parameters.BackBufferHeight = g_window_height;
        }
        if (!parameters.hDeviceWindow)
            parameters.hDeviceWindow = fallback_window;
    }

    D3D9HookRecord* find_d3d9_record(IDirect3D9* object)
    {
        for (D3D9HookRecord& record : g_d3d9_records)
        {
            if (record.object == object)
                return &record;
        }
        return nullptr;
    }

    DeviceHookRecord* find_device_record(IDirect3DDevice9* object)
    {
        for (DeviceHookRecord& record : g_device_records)
        {
            if (record.object == object)
                return &record;
        }
        return nullptr;
    }

    void add_mode(D3D9HookRecord& record, const D3DDISPLAYMODE& mode)
    {
        if (mode.Width < 640 || mode.Height < 480)
            return;
        for (std::size_t index = 0; index < record.mode_count; ++index)
        {
            if (record.modes[index].mode.Width == mode.Width &&
                record.modes[index].mode.Height == mode.Height)
            {
                return;
            }
        }
        if (record.mode_count >= k_max_modes)
            return;
        record.modes[record.mode_count++].mode = mode;
    }

    void add_synthetic_mode(D3D9HookRecord& record, const UINT width, const UINT height,
                            const D3DFORMAT format)
    {
        D3DDISPLAYMODE mode {};
        mode.Width = width;
        mode.Height = height;
        mode.RefreshRate = 60;
        mode.Format = format;
        add_mode(record, mode);
    }

    void build_mode_table(D3D9HookRecord& record, const UINT adapter,
                          const D3DFORMAT format)
    {
        record.mode_count = 0;
        if (record.get_adapter_mode_count && record.enum_adapter_modes)
        {
            const UINT real_count = record.get_adapter_mode_count(record.object, adapter, format);
            for (UINT index = 0; index < real_count; ++index)
            {
                D3DDISPLAYMODE mode {};
                if (SUCCEEDED(record.enum_adapter_modes(
                        record.object, adapter, format, index, &mode)))
                {
                    add_mode(record, mode);
                }
            }
        }

        D3DDISPLAYMODE desktop {};
        const bool desktop_known =
            record.object &&
            SUCCEEDED(record.object->GetAdapterDisplayMode(adapter, &desktop)) &&
            desktop.Width != 0 && desktop.Height != 0;

        struct ModeSize { UINT width; UINT height; };
        static const ModeSize standard[] = {
            {640, 480},   {800, 600},   {1024, 768},  {1152, 864},  {1280, 720},
            {1280, 768},  {1280, 800},  {1280, 960},  {1280, 1024}, {1360, 768},
            {1366, 768},  {1440, 900},  {1600, 900},  {1600, 1200}, {1680, 1050},
            {1920, 1080}, {1920, 1200}};
        for (const ModeSize& mode : standard)
        {
            if (desktop_known && (mode.width > desktop.Width || mode.height > desktop.Height))
                continue;
            add_synthetic_mode(record, mode.width, mode.height, format);
        }

        if (desktop_known)
        {
            static const UINT widths[] = {640, 800, 1024, 1152, 1280, 1440, 1600, 1920};
            for (const UINT width : widths)
            {
                if (width > desktop.Width)
                    continue;
                const unsigned long long numerator =
                    static_cast<unsigned long long>(width) * desktop.Height;
                const unsigned long long denominator = desktop.Width;
                const unsigned long long floored = numerator / denominator;
                const UINT rounded =
                    static_cast<UINT>((numerator + denominator / 2) / denominator);
                if (floored <= desktop.Height)
                    add_synthetic_mode(record, width, static_cast<UINT>(floored), format);
                if (rounded <= desktop.Height)
                    add_synthetic_mode(record, width, rounded, format);
                if (numerator % denominator != 0 && floored + 1 <= desktop.Height)
                    add_synthetic_mode(record, width, static_cast<UINT>(floored + 1), format);
            }
            add_mode(record, desktop);
        }

        record.modes_ready = true;
        record.mode_adapter = adapter;
        record.mode_format = format;
    }

    void ensure_mode_table(D3D9HookRecord& record, const UINT adapter,
                           const D3DFORMAT format)
    {
        if (!record.modes_ready || record.mode_adapter != adapter ||
            record.mode_format != format)
        {
            build_mode_table(record, adapter, format);
        }
    }

    UINT WINAPI hook_get_adapter_mode_count(
        IDirect3D9* direct3d, const UINT adapter, const D3DFORMAT format)
    {
        if (!g_d3d_lock_ready)
            return 0;
        EnterCriticalSection(&g_d3d_lock);
        D3D9HookRecord* record = find_d3d9_record(direct3d);
        if (!record || !record->get_adapter_mode_count)
        {
            LeaveCriticalSection(&g_d3d_lock);
            return 0;
        }
        if (!g_force_windowed)
        {
            GetAdapterModeCountFunction original = record->get_adapter_mode_count;
            LeaveCriticalSection(&g_d3d_lock);
            return original(direct3d, adapter, format);
        }
        ensure_mode_table(*record, adapter, format);
        const UINT count = static_cast<UINT>(record->mode_count);
        LeaveCriticalSection(&g_d3d_lock);
        return count;
    }

    HRESULT WINAPI hook_enum_adapter_modes(
        IDirect3D9* direct3d, const UINT adapter, const D3DFORMAT format,
        const UINT index, D3DDISPLAYMODE* mode)
    {
        if (!g_d3d_lock_ready)
            return D3DERR_INVALIDCALL;
        EnterCriticalSection(&g_d3d_lock);
        D3D9HookRecord* record = find_d3d9_record(direct3d);
        if (!record || !record->enum_adapter_modes)
        {
            LeaveCriticalSection(&g_d3d_lock);
            return D3DERR_INVALIDCALL;
        }
        if (!g_force_windowed)
        {
            EnumAdapterModesFunction original = record->enum_adapter_modes;
            LeaveCriticalSection(&g_d3d_lock);
            return original(direct3d, adapter, format, index, mode);
        }
        ensure_mode_table(*record, adapter, format);
        if (!mode || index >= record->mode_count)
        {
            LeaveCriticalSection(&g_d3d_lock);
            return D3DERR_INVALIDCALL;
        }
        *mode = record->modes[index].mode;
        LeaveCriticalSection(&g_d3d_lock);
        return D3D_OK;
    }

    const char* d3d_hresult_name(const HRESULT result)
    {
        switch (result)
        {
            case D3D_OK: return "D3D_OK";
            case D3DERR_CONFLICTINGTEXTUREFILTER: return "D3DERR_CONFLICTINGTEXTUREFILTER";
            case D3DERR_CONFLICTINGTEXTUREPALETTE: return "D3DERR_CONFLICTINGTEXTUREPALETTE";
            case D3DERR_DEVICELOST: return "D3DERR_DEVICELOST";
            case D3DERR_DEVICENOTRESET: return "D3DERR_DEVICENOTRESET";
            case D3DERR_DRIVERINTERNALERROR: return "D3DERR_DRIVERINTERNALERROR";
            case D3DERR_INVALIDCALL: return "D3DERR_INVALIDCALL";
            case D3DERR_NOTAVAILABLE: return "D3DERR_NOTAVAILABLE";
            case D3DERR_OUTOFVIDEOMEMORY: return "D3DERR_OUTOFVIDEOMEMORY";
            case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
            default: return "UNKNOWN";
        }
    }

    HRESULT WINAPI hook_reset(IDirect3DDevice9* device,
                              D3DPRESENT_PARAMETERS* requested)
    {
        if (!requested || !g_d3d_lock_ready)
            return D3DERR_INVALIDCALL;
        EnterCriticalSection(&g_d3d_lock);
        DeviceHookRecord* record = find_device_record(device);
        ResetFunction original = record ? record->reset : nullptr;
        LeaveCriticalSection(&g_d3d_lock);
        if (!original)
            return D3DERR_INVALIDCALL;

        {
            char buffer[512] {};
            std::snprintf(buffer, sizeof(buffer),
                "Reset requested windowed=%u size=%ux%u format=%u depth=%u refresh=%u interval=%u",
                requested->Windowed ? 1u : 0u,
                requested->BackBufferWidth,
                requested->BackBufferHeight,
                static_cast<unsigned>(requested->BackBufferFormat),
                static_cast<unsigned>(requested->AutoDepthStencilFormat),
                requested->FullScreen_RefreshRateInHz,
                requested->PresentationInterval);
            log_literal("d3d9", buffer);
        }

        D3DPRESENT_PARAMETERS effective = *requested;
        const bool rewrite = g_force_windowed && effective.Windowed == FALSE;
        if (rewrite)
        {
            force_windowed_parameters(effective, effective.hDeviceWindow);
            log_literal("d3d9-window",
                        "rewrote fullscreen Reset to requested-size windowed Reset with original formats and swap settings preserved");
        }
        const HRESULT result = original(device, rewrite ? &effective : requested);
        {
            char buffer[192] {};
            std::snprintf(buffer, sizeof(buffer),
                "Reset result=0x%08lx name=%s success=%u",
                static_cast<unsigned long>(result), d3d_hresult_name(result),
                SUCCEEDED(result) ? 1u : 0u);
            log_literal("d3d9", buffer);
        }
        if (rewrite && SUCCEEDED(result))
        {
            log_literal("d3d9-window",
                        "rewritten windowed Reset completed; preserved Alicia fullscreen bookkeeping");
        }
        else if (rewrite)
        {
            log_literal("d3d9-window", "rewritten windowed Reset failed");
        }
        return result;
    }

    HRESULT WINAPI hook_present(IDirect3DDevice9* device,
                                const RECT* source_rect,
                                const RECT* destination_rect,
                                const HWND destination_window,
                                const RGNDATA* dirty_region)
    {
        if (!g_d3d_lock_ready)
            return D3DERR_INVALIDCALL;
        EnterCriticalSection(&g_d3d_lock);
        DeviceHookRecord* record = find_device_record(device);
        PresentFunction original = record ? record->present : nullptr;
        const LONG count = record ? InterlockedIncrement(&record->present_count) : 0;
        LeaveCriticalSection(&g_d3d_lock);
        if (!original)
            return D3DERR_INVALIDCALL;

        const HRESULT result = original(
            device, source_rect, destination_rect, destination_window, dirty_region);
        if (count <= 5 || FAILED(result))
        {
            char buffer[192] {};
            std::snprintf(buffer, sizeof(buffer),
                "Present count=%ld result=0x%08lx name=%s success=%u",
                count, static_cast<unsigned long>(result), d3d_hresult_name(result),
                SUCCEEDED(result) ? 1u : 0u);
            log_literal("d3d9", buffer);
        }
        return result;
    }

    bool install_device_vtable(IDirect3DDevice9* device)
    {
        if (!device || !g_d3d_lock_ready)
            return false;
        EnterCriticalSection(&g_d3d_lock);
        if (find_device_record(device))
        {
            LeaveCriticalSection(&g_d3d_lock);
            return true;
        }
        DeviceHookRecord* slot = nullptr;
        for (DeviceHookRecord& record : g_device_records)
        {
            if (!record.object)
            {
                slot = &record;
                break;
            }
        }
        void** original = *reinterpret_cast<void***>(device);
        if (!slot || !original || !original[16] || !original[17])
        {
            LeaveCriticalSection(&g_d3d_lock);
            return false;
        }
        void** copy = static_cast<void**>(VirtualAlloc(
            nullptr, sizeof(void*) * k_device_vtable_entries,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!copy)
        {
            LeaveCriticalSection(&g_d3d_lock);
            return false;
        }
        std::memcpy(copy, original, sizeof(void*) * k_device_vtable_entries);
        slot->object = device;
        slot->original_vtable = original;
        slot->hooked_vtable = copy;
        slot->reset = reinterpret_cast<ResetFunction>(original[16]);
        slot->present = reinterpret_cast<PresentFunction>(original[17]);
        copy[16] = reinterpret_cast<void*>(hook_reset);
        copy[17] = reinterpret_cast<void*>(hook_present);
        InterlockedExchangePointer(
            reinterpret_cast<PVOID volatile*>(device), copy);
        LeaveCriticalSection(&g_d3d_lock);
        log_literal("d3d9", "installed per-device Reset and Present vtable hooks");
        return true;
    }

    HRESULT WINAPI hook_create_device(
        IDirect3D9* direct3d, const UINT adapter, const D3DDEVTYPE device_type,
        const HWND focus_window, const DWORD behavior_flags,
        D3DPRESENT_PARAMETERS* requested, IDirect3DDevice9** device)
    {
        {
            char buffer[512] {};
            std::snprintf(buffer, sizeof(buffer),
                "CreateDevice adapter=%u device_type=%u flags=0x%08lx windowed=%u size=%ux%u format=%u depth=%u",
                adapter, static_cast<unsigned>(device_type),
                static_cast<unsigned long>(behavior_flags),
                requested && requested->Windowed ? 1u : 0u,
                requested ? requested->BackBufferWidth : 0u,
                requested ? requested->BackBufferHeight : 0u,
                requested ? static_cast<unsigned>(requested->BackBufferFormat) : 0u,
                requested ? static_cast<unsigned>(requested->AutoDepthStencilFormat) : 0u);
            log_literal("d3d9", buffer);
        }
        if (!requested || !device || !g_d3d_lock_ready)
            return D3DERR_INVALIDCALL;
        EnterCriticalSection(&g_d3d_lock);
        D3D9HookRecord* record = find_d3d9_record(direct3d);
        CreateDeviceFunction original = record ? record->create_device : nullptr;
        LeaveCriticalSection(&g_d3d_lock);
        if (!original)
            return D3DERR_INVALIDCALL;

        D3DPRESENT_PARAMETERS effective = *requested;
        const bool rewrite = g_force_windowed && effective.Windowed == FALSE;
        if (rewrite)
        {
            force_windowed_parameters(effective, focus_window);
            log_literal("d3d9-window",
                        "rewrote initial CreateDevice to requested-size windowed mode with original formats and swap settings preserved");
        }
        const HRESULT result = original(
            direct3d, adapter, device_type, focus_window, behavior_flags,
            rewrite ? &effective : requested, device);
        if (rewrite && SUCCEEDED(result))
            log_literal("d3d9-window", "rewritten initial windowed CreateDevice completed");
        else if (rewrite)
            log_literal("d3d9-window", "rewritten initial windowed CreateDevice failed");
        {
            char buffer[128] {};
            std::snprintf(buffer, sizeof(buffer),
                "CreateDevice result=0x%08lx name=%s success=%u",
                static_cast<unsigned long>(result), d3d_hresult_name(result),
                SUCCEEDED(result) ? 1u : 0u);
            log_literal("d3d9", buffer);
        }
        if (SUCCEEDED(result) && *device)
            install_device_vtable(*device);
        return result;
    }

    bool install_d3d9_vtable(IDirect3D9* direct3d)
    {
        if (!direct3d || !g_d3d_lock_ready)
            return false;
        EnterCriticalSection(&g_d3d_lock);
        if (find_d3d9_record(direct3d))
        {
            LeaveCriticalSection(&g_d3d_lock);
            return true;
        }
        D3D9HookRecord* slot = nullptr;
        for (D3D9HookRecord& record : g_d3d9_records)
        {
            if (!record.object)
            {
                slot = &record;
                break;
            }
        }
        void** original = *reinterpret_cast<void***>(direct3d);
        if (!slot || !original || !original[6] || !original[7] || !original[16])
        {
            LeaveCriticalSection(&g_d3d_lock);
            return false;
        }
        void** copy = static_cast<void**>(VirtualAlloc(
            nullptr, sizeof(void*) * k_d3d9_vtable_entries,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!copy)
        {
            LeaveCriticalSection(&g_d3d_lock);
            return false;
        }
        std::memcpy(copy, original, sizeof(void*) * k_d3d9_vtable_entries);
        slot->object = direct3d;
        slot->original_vtable = original;
        slot->hooked_vtable = copy;
        slot->get_adapter_mode_count =
            reinterpret_cast<GetAdapterModeCountFunction>(original[6]);
        slot->enum_adapter_modes =
            reinterpret_cast<EnumAdapterModesFunction>(original[7]);
        slot->create_device = reinterpret_cast<CreateDeviceFunction>(original[16]);
        copy[6] = reinterpret_cast<void*>(hook_get_adapter_mode_count);
        copy[7] = reinterpret_cast<void*>(hook_enum_adapter_modes);
        copy[16] = reinterpret_cast<void*>(hook_create_device);
        InterlockedExchangePointer(
            reinterpret_cast<PVOID volatile*>(direct3d), copy);
        LeaveCriticalSection(&g_d3d_lock);
        log_literal("d3d9", "installed per-object IDirect3D9 vtable hooks");
        return true;
    }

    IDirect3D9* WINAPI hook_direct3d_create9(const UINT sdk_version)
    {
        {
            char buffer[128] {};
            std::snprintf(buffer, sizeof(buffer), "Direct3DCreate9 sdk=%u", sdk_version);
            log_literal("d3d9", buffer);
        }
        if (!g_direct3d_create9)
            return nullptr;
        IDirect3D9* direct3d = g_direct3d_create9(sdk_version);
        log_literal("d3d9", direct3d ? "Direct3DCreate9 returned object" : "Direct3DCreate9 returned null");
        if (direct3d)
            install_d3d9_vtable(direct3d);
        return direct3d;
    }

    bool patch_direct3d_create9_iat()
    {
        HMODULE module = GetModuleHandleW(nullptr);
        if (!module)
            return false;
        auto* base = reinterpret_cast<unsigned char*>(module);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;
        const IMAGE_DATA_DIRECTORY& directory =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress)
            return false;
        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            base + directory.VirtualAddress);
        for (; descriptor->Name; ++descriptor)
        {
            const char* module_name = reinterpret_cast<const char*>(base + descriptor->Name);
            if (lstrcmpiA(module_name, "d3d9.dll") != 0)
                continue;
            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                base + descriptor->FirstThunk);
            auto* names = descriptor->OriginalFirstThunk
                ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk)
                : nullptr;
            HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
            if (!d3d9)
                d3d9 = LoadLibraryW(L"d3d9.dll");
            const ULONG_PTR exported = reinterpret_cast<ULONG_PTR>(
                d3d9 ? GetProcAddress(d3d9, "Direct3DCreate9") : nullptr);
            for (UINT index = 0; thunk[index].u1.Function; ++index)
            {
                bool matches = false;
                if (names)
                {
                    if (!IMAGE_SNAP_BY_ORDINAL(names[index].u1.Ordinal))
                    {
                        auto* imported = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                            base + names[index].u1.AddressOfData);
                        matches = std::strcmp(
                            reinterpret_cast<const char*>(imported->Name),
                            "Direct3DCreate9") == 0;
                    }
                }
                else
                {
                    matches = exported != 0 && thunk[index].u1.Function == exported;
                }
                if (!matches)
                    continue;
                IMAGE_THUNK_DATA* entry = &thunk[index];
                DWORD old_protection = 0;
                if (!VirtualProtect(&entry->u1.Function, sizeof(entry->u1.Function),
                                    PAGE_READWRITE, &old_protection))
                    return false;
                g_direct3d_create9 = reinterpret_cast<Direct3DCreate9Function>(
                    static_cast<ULONG_PTR>(entry->u1.Function));
                entry->u1.Function = reinterpret_cast<ULONG_PTR>(hook_direct3d_create9);
                DWORD ignored = 0;
                VirtualProtect(&entry->u1.Function, sizeof(entry->u1.Function),
                               old_protection, &ignored);
                FlushInstructionCache(GetCurrentProcess(), &entry->u1.Function,
                                      sizeof(entry->u1.Function));
                log_literal("d3d9", "patched Alicia Direct3DCreate9 IAT entry");
                return g_direct3d_create9 != nullptr;
            }
        }
        return false;
    }

    bool register_d3d9_hooks()
    {
        return patch_direct3d_create9_iat();
    }

    void log_message(const char* source, const char* message, std::size_t length,
                     const bool require_bracket)
    {
        if (!g_log_lock_ready || !message || length == 0)
            return;
        length = std::min(length, k_max_message_bytes - 1);

        if (require_bracket && !g_log_all)
        {
            std::size_t first = 0;
            while (first < length &&
                   (message[first] == ' ' || message[first] == '\t' ||
                    message[first] == '\r' || message[first] == '\n'))
            {
                ++first;
            }
            if (first >= length || message[first] != '[')
                return;
        }

        char safe[k_max_message_bytes] {};
        redact_copy(safe, sizeof(safe), message, length);
        const std::size_t safe_length = bounded_length(safe, sizeof(safe) - 1);

        char prefix[96] {};
        char* cursor = prefix;
        SYSTEMTIME time {};
        GetLocalTime(&time);
        *cursor++ = '[';
        append_decimal(cursor, time.wHour, 2);
        *cursor++ = ':';
        append_decimal(cursor, time.wMinute, 2);
        *cursor++ = ':';
        append_decimal(cursor, time.wSecond, 2);
        *cursor++ = '.';
        append_decimal(cursor, time.wMilliseconds, 3);
        *cursor++ = ']';
        *cursor++ = ' ';
        *cursor++ = '[';
        const std::size_t source_length = bounded_length(source, 24);
        std::memcpy(cursor, source, source_length);
        cursor += source_length;
        *cursor++ = ']';
        *cursor++ = ' ';

        EnterCriticalSection(&g_log_lock);
        write_bytes_locked(prefix, static_cast<std::size_t>(cursor - prefix));
        write_bytes_locked(safe, safe_length);
        if (safe_length == 0 || (safe[safe_length - 1] != '\n' && safe[safe_length - 1] != '\r'))
            write_bytes_locked("\r\n", 2);
        FlushFileBuffers(g_log);
        LeaveCriticalSection(&g_log_lock);
    }

    void log_vsnprintf(const std::size_t index, const char* buffer,
                       const std::size_t count, const int result)
    {
        if (!buffer || count == 0)
            return;
        std::size_t maximum = std::min(count, k_max_message_bytes - 1);
        std::size_t length = result > 0
            ? std::min<std::size_t>(static_cast<std::size_t>(result), maximum)
            : bounded_length(buffer, maximum);
        if (length == 0)
            return;
        char source[16] {'v', 's', 'n', 'p', 'r', 'i', 'n', 't', 'f', '-', 0};
        char* position = source + 10;
        if (index >= 10)
            *position++ = static_cast<char>('0' + (index / 10) % 10);
        *position++ = static_cast<char>('0' + index % 10);
        *position = '\0';
        log_message(source, buffer, length, true);
    }

#define SOA_DEFINE_VSNPRINTF_HOOK(index)                                                     \
    int __cdecl hook_vsnprintf_##index(char* buffer, std::size_t count,                    \
                                        const char* format, va_list arguments)              \
    {                                                                                        \
        const VsnprintfFunction original = g_vsnprintf_originals[index];                    \
        if (!original)                                                                       \
            return -1;                                                                       \
        const int result = original(buffer, count, format, arguments);                       \
        log_vsnprintf(index, buffer, count, result);                                         \
        return result;                                                                       \
    }

    SOA_DEFINE_VSNPRINTF_HOOK(0)  SOA_DEFINE_VSNPRINTF_HOOK(1)
    SOA_DEFINE_VSNPRINTF_HOOK(2)  SOA_DEFINE_VSNPRINTF_HOOK(3)
    SOA_DEFINE_VSNPRINTF_HOOK(4)  SOA_DEFINE_VSNPRINTF_HOOK(5)
    SOA_DEFINE_VSNPRINTF_HOOK(6)  SOA_DEFINE_VSNPRINTF_HOOK(7)
    SOA_DEFINE_VSNPRINTF_HOOK(8)  SOA_DEFINE_VSNPRINTF_HOOK(9)
    SOA_DEFINE_VSNPRINTF_HOOK(10) SOA_DEFINE_VSNPRINTF_HOOK(11)
    SOA_DEFINE_VSNPRINTF_HOOK(12) SOA_DEFINE_VSNPRINTF_HOOK(13)
    SOA_DEFINE_VSNPRINTF_HOOK(14) SOA_DEFINE_VSNPRINTF_HOOK(15)
    SOA_DEFINE_VSNPRINTF_HOOK(16) SOA_DEFINE_VSNPRINTF_HOOK(17)
    SOA_DEFINE_VSNPRINTF_HOOK(18) SOA_DEFINE_VSNPRINTF_HOOK(19)
    SOA_DEFINE_VSNPRINTF_HOOK(20) SOA_DEFINE_VSNPRINTF_HOOK(21)
    SOA_DEFINE_VSNPRINTF_HOOK(22) SOA_DEFINE_VSNPRINTF_HOOK(23)

    void* const g_vsnprintf_detours[k_max_hook_slots] {
        reinterpret_cast<void*>(hook_vsnprintf_0),  reinterpret_cast<void*>(hook_vsnprintf_1),
        reinterpret_cast<void*>(hook_vsnprintf_2),  reinterpret_cast<void*>(hook_vsnprintf_3),
        reinterpret_cast<void*>(hook_vsnprintf_4),  reinterpret_cast<void*>(hook_vsnprintf_5),
        reinterpret_cast<void*>(hook_vsnprintf_6),  reinterpret_cast<void*>(hook_vsnprintf_7),
        reinterpret_cast<void*>(hook_vsnprintf_8),  reinterpret_cast<void*>(hook_vsnprintf_9),
        reinterpret_cast<void*>(hook_vsnprintf_10), reinterpret_cast<void*>(hook_vsnprintf_11),
        reinterpret_cast<void*>(hook_vsnprintf_12), reinterpret_cast<void*>(hook_vsnprintf_13),
        reinterpret_cast<void*>(hook_vsnprintf_14), reinterpret_cast<void*>(hook_vsnprintf_15),
        reinterpret_cast<void*>(hook_vsnprintf_16), reinterpret_cast<void*>(hook_vsnprintf_17),
        reinterpret_cast<void*>(hook_vsnprintf_18), reinterpret_cast<void*>(hook_vsnprintf_19),
        reinterpret_cast<void*>(hook_vsnprintf_20), reinterpret_cast<void*>(hook_vsnprintf_21),
        reinterpret_cast<void*>(hook_vsnprintf_22), reinterpret_cast<void*>(hook_vsnprintf_23),
    };

    void WINAPI hook_output_debug_a(const char* text)
    {
        if (g_output_debug_a)
            g_output_debug_a(text);
        log_message("debug-a", text, bounded_length(text, k_max_message_bytes - 1), false);
    }

    void WINAPI hook_output_debug_w(const wchar_t* text)
    {
        if (g_output_debug_w)
            g_output_debug_w(text);
        if (!text)
            return;
        const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                                 nullptr, nullptr);
        if (required <= 1)
            return;
        char utf8[k_max_message_bytes] {};
        WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8,
                            std::min<int>(required, static_cast<int>(sizeof(utf8))),
                            nullptr, nullptr);
        utf8[sizeof(utf8) - 1] = '\0';
        log_message("debug-w", utf8, bounded_length(utf8, sizeof(utf8) - 1), false);
    }

    bool target_already_registered(void* target)
    {
        for (std::size_t index = 0; index < g_vsnprintf_count; ++index)
        {
            if (g_vsnprintf_targets[index] == target)
                return true;
        }
        return false;
    }

    HMODULE WINAPI hook_load_library_a(LPCSTR name)
    {
        HMODULE module = g_load_library_a ? g_load_library_a(name) : LoadLibraryA(name);
        if (name)
        {
            char message[768] {};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                        "LoadLibraryA(%s) -> %s", name, module ? "OK" : "FAILED");
            log_literal("loader", message);
        }
        return module;
    }

    HMODULE WINAPI hook_load_library_w(LPCWSTR name)
    {
        HMODULE module = g_load_library_w ? g_load_library_w(name) : LoadLibraryW(name);
        if (name)
        {
            char utf8[512] {};
            WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
            char message[768] {};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                        "LoadLibraryW(%s) -> %s", utf8, module ? "OK" : "FAILED");
            log_literal("loader", message);
        }
        return module;
    }

    void register_vsnprintf_hooks()
    {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                                         GetCurrentProcessId());
        if (snapshot == INVALID_HANDLE_VALUE)
            return;

        MODULEENTRY32W entry {};
        entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot, &entry))
        {
            do
            {
                void* target = reinterpret_cast<void*>(GetProcAddress(entry.hModule, "_vsnprintf"));
                if (!target || target_already_registered(target) ||
                    g_vsnprintf_count >= k_max_hook_slots)
                {
                    continue;
                }
                void* original = nullptr;
                const MH_STATUS status = MH_CreateHook(
                    target, g_vsnprintf_detours[g_vsnprintf_count], &original);
                if (status == MH_OK)
                {
                    g_vsnprintf_targets[g_vsnprintf_count] = target;
                    g_vsnprintf_originals[g_vsnprintf_count] =
                        reinterpret_cast<VsnprintfFunction>(original);
                    ++g_vsnprintf_count;
                }
            } while (Module32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    bool read_environment()
    {
        wchar_t force_windowed[8] {};
        GetEnvironmentVariableW(L"SOA_D3D9_FORCE_WINDOWED", force_windowed,
                                static_cast<DWORD>(std::size(force_windowed)));
        g_force_windowed = force_windowed[0] == L'1';
        if (g_force_windowed)
        {
            g_window_width = read_window_dimension(
                L"SOA_D3D9_WINDOW_WIDTH", g_window_width);
            g_window_height = read_window_dimension(
                L"SOA_D3D9_WINDOW_HEIGHT", g_window_height);
        }

        wchar_t path[32768] {};
        const DWORD path_length = GetEnvironmentVariableW(
            L"SOA_ALICIA_LOG_PATH", path, static_cast<DWORD>(std::size(path)));

        wchar_t redact[2048] {};
        const DWORD redact_length = GetEnvironmentVariableW(
            L"SOA_ALICIA_LOG_REDACT", redact, static_cast<DWORD>(std::size(redact)));
        if (redact_length > 0 && redact_length < std::size(redact))
        {
            WideCharToMultiByte(CP_UTF8, 0, redact, -1, g_redact,
                                static_cast<int>(sizeof(g_redact)), nullptr, nullptr);
            g_redact[sizeof(g_redact) - 1] = '\0';
        }

        wchar_t log_all[8] {};
        GetEnvironmentVariableW(L"SOA_ALICIA_LOG_ALL", log_all,
                                static_cast<DWORD>(std::size(log_all)));
        g_log_all = log_all[0] == L'1';

        SetEnvironmentVariableW(L"SOA_ALICIA_LOG_REDACT", nullptr);
        SetEnvironmentVariableW(L"SOA_ALICIA_LOG_PATH", nullptr);
        SetEnvironmentVariableW(L"SOA_ALICIA_LOG_ALL", nullptr);
        SetEnvironmentVariableW(L"SOA_D3D9_FORCE_WINDOWED", nullptr);
        SetEnvironmentVariableW(L"SOA_D3D9_WINDOW_WIDTH", nullptr);
        SetEnvironmentVariableW(L"SOA_D3D9_WINDOW_HEIGHT", nullptr);

        if (path_length > 0 && path_length < std::size(path))
        {
            g_log = CreateFileW(path, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
        wchar_t pipe_on[8] {};
        const bool pipe_requested =
            GetEnvironmentVariableW(L"SOA_AUDIO_PIPE", pipe_on,
                                    static_cast<DWORD>(std::size(pipe_on))) != 0 &&
            pipe_on[0] == L'1';
        wchar_t silent[8] {};
        const bool silence_forced =
            GetEnvironmentVariableW(L"SOA_AUDIO_NULL_BACKEND", silent,
                                    static_cast<DWORD>(std::size(silent))) != 0 &&
            silent[0] == L'1';
        const bool audio_wanted = pipe_requested || silence_forced;

        return g_log != INVALID_HANDLE_VALUE || g_force_windowed || audio_wanted;
    }

}

extern "C" __declspec(dllexport) DWORD WINAPI SoaLogHookInitialize(void*);
extern "C" bool soa_register_null_dsound_hooks();
extern "C" bool soa_register_pipe_dsound_hooks();
extern "C" void soa_shutdown_pipe_dsound();

extern "C" void soa_audio_log(const char* tag, const char* message)
{
    if (tag != nullptr && message != nullptr)
        log_literal(tag, message);
}

namespace
{
    DWORD WINAPI initialize_after_attach(void*)
    {
        const HMODULE self = GetModuleHandleW(L"kernel32.dll");
        (void)self;
        DWORD settled = 0;
        for (DWORD attempt = 0; attempt < 200 && settled < 3; ++attempt)
        {
            Sleep(25);
            HMODULE probe = nullptr;
            if (GetModuleHandleExW(0, L"ntdll.dll", &probe) && probe)
            {
                ++settled;
                FreeLibrary(probe);
            }
            else
            {
                settled = 0;
            }
        }
        return SoaLogHookInitialize(nullptr);
    }
}

extern "C" __declspec(dllexport) DWORD WINAPI SoaLogHookInitialize(void*)
{
    const LONG previous = InterlockedCompareExchange(&g_initialization, 1, 0);
    if (previous != 0)
        return previous == 2 ? 1 : 0;

    InitializeCriticalSection(&g_log_lock);
    g_log_lock_ready = true;
    InitializeCriticalSection(&g_d3d_lock);
    g_d3d_lock_ready = true;
    if (!read_environment())
        return 0;

    if (g_log != INVALID_HANDLE_VALUE)
    {
        static constexpr char header[] =
            "Story of Alicia internal log\r\n"
            "Upstream: SergeantSerk/log-hook\r\n"
            "Adaptation: Story of Alicia launcher (Wine on Linux/macOS)\r\n"
            "Only bracket-prefixed printf output is recorded unless SOA_ALICIA_LOG_ALL=1.\r\n"
            "--- Alicia output ---\r\n";
        EnterCriticalSection(&g_log_lock);
        write_bytes_locked(header, sizeof(header) - 1);
        FlushFileBuffers(g_log);
        LeaveCriticalSection(&g_log_lock);
    }

    if (MH_Initialize() != MH_OK)
        return 0;

    if (g_log != INVALID_HANDLE_VALUE)
    {
        MH_CreateHookApi(L"kernel32.dll", "OutputDebugStringA",
                         reinterpret_cast<void*>(hook_output_debug_a),
                         reinterpret_cast<void**>(&g_output_debug_a));
        MH_CreateHookApi(L"kernel32.dll", "OutputDebugStringW",
                         reinterpret_cast<void*>(hook_output_debug_w),
                         reinterpret_cast<void**>(&g_output_debug_w));
        MH_CreateHookApi(L"kernel32.dll", "LoadLibraryA",
                         reinterpret_cast<void*>(hook_load_library_a),
                         reinterpret_cast<void**>(&g_load_library_a));
        MH_CreateHookApi(L"kernel32.dll", "LoadLibraryW",
                         reinterpret_cast<void*>(hook_load_library_w),
                         reinterpret_cast<void**>(&g_load_library_w));
        register_vsnprintf_hooks();
    }

    if (!soa_register_pipe_dsound_hooks())
    {
        log_literal("audio-pipe", "failed to register pipe DirectSound backend");
        return 0;
    }
    if (!soa_register_null_dsound_hooks())
    {
        log_literal("audio-null", "failed to register controlled DirectSound failure hooks");
        return 0;
    }
    {
        wchar_t audio_null[8] {};
        const DWORD length = GetEnvironmentVariableW(
            L"SOA_AUDIO_NULL_BACKEND", audio_null,
            static_cast<DWORD>(std::size(audio_null)));
        if (length > 0 && audio_null[0] == L'1')
            log_literal("audio-null", "registered controlled DirectSound DSERR_NODRIVER hooks");
        else
            log_literal("audio-null", "null DirectSound disabled; real audio path in use");
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
        return 0;

    InterlockedExchange(&g_initialization, 2);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        const HANDLE thread = CreateThread(nullptr, 0, initialize_after_attach, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
