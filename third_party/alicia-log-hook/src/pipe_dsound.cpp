
// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>

#include "MinHook.h"

#include "soa_audio_bridge.hpp"

extern "C" void soa_audio_log(const char* tag, const char* message);

namespace
{
    using namespace soa_audio;

    constexpr unsigned short k_default_port = 57311;
    constexpr DWORD k_pump_interval_ms = 10;
    constexpr std::uint32_t k_pump_chunk = 16384;

    CRITICAL_SECTION g_lock;
    bool g_lock_ready = false;
    SOCKET g_socket = INVALID_SOCKET;
    HANDLE g_pump = nullptr;
    volatile LONG g_stop_pump = 0;
    constexpr std::uint32_t k_out_rate = 44100;
    constexpr std::uint16_t k_out_channels = 2;
    constexpr std::size_t k_max_buffers = 64;

    StreamingBuffer* g_buffers[k_max_buffers] {};
    std::size_t g_buffer_count = 0;
    std::uint64_t g_last_pump_us = 0;
    bool g_header_sent = false;
    bool g_winsock_ready = false;
    DWORD g_next_connect_ms = 0;
    bool g_connect_warned = false;

    void log_line(const char* message) { soa_audio_log("audio-pipe", message); }

    struct Guard
    {
        Guard() { if (g_lock_ready) { EnterCriticalSection(&g_lock); } }
        ~Guard() { if (g_lock_ready) { LeaveCriticalSection(&g_lock); } }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    };

    std::uint64_t now_microseconds()
    {
        static LARGE_INTEGER frequency {};
        if (frequency.QuadPart == 0)
        {
            QueryPerformanceFrequency(&frequency);
            if (frequency.QuadPart == 0)
            {
                frequency.QuadPart = 1;
            }
        }
        LARGE_INTEGER counter {};
        QueryPerformanceCounter(&counter);
        return static_cast<std::uint64_t>(counter.QuadPart) * 1000000ull /
               static_cast<std::uint64_t>(frequency.QuadPart);
    }

    unsigned short configured_port()
    {
        wchar_t value[16] {};
        const DWORD length = GetEnvironmentVariableW(
            L"SOA_AUDIO_PIPE_PORT", value, static_cast<DWORD>(std::size(value)));
        if (length == 0 || length >= std::size(value))
        {
            return k_default_port;
        }
        const int parsed = _wtoi(value);
        return (parsed > 0 && parsed < 65536)
                   ? static_cast<unsigned short>(parsed)
                   : k_default_port;
    }

    bool ensure_socket()
    {
        if (g_socket != INVALID_SOCKET)
        {
            return true;
        }
        const DWORD now_ms = GetTickCount();
        if (now_ms < g_next_connect_ms)
        {
            return false;
        }
        g_next_connect_ms = now_ms + 1000;
        if (!g_winsock_ready)
        {
            WSADATA data {};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                log_line("WSAStartup failed; audio disabled for this session");
                return false;
            }
            g_winsock_ready = true;
        }

        const SOCKET handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (handle == INVALID_SOCKET)
        {
            return false;
        }
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_port = htons(configured_port());
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        {
            closesocket(handle);
            if (!g_connect_warned)
            {
                log_line("soa-audio-host is not listening; retrying once a second");
                g_connect_warned = true;
            }
            return false;
        }
        int flag = 1;
        setsockopt(handle, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&flag), sizeof(flag));
        g_socket = handle;
        g_header_sent = false;
        g_connect_warned = false;
        log_line("connected to soa-audio-host");
        return true;
    }

    bool send_all(const void* data, int length)
    {
        const char* cursor = static_cast<const char*>(data);
        while (length > 0)
        {
            const int written = send(g_socket, cursor, length, 0);
            if (written <= 0)
            {
                closesocket(g_socket);
                g_socket = INVALID_SOCKET;
                log_line("audio host disconnected");
                return false;
            }
            cursor += written;
            length -= written;
        }
        return true;
    }

    DWORD WINAPI pump_thread(void*)
    {
        auto* scratch = static_cast<std::uint8_t*>(
            HeapAlloc(GetProcessHeap(), 0, k_pump_chunk));
        auto* accumulator = static_cast<std::int32_t*>(HeapAlloc(
            GetProcessHeap(), 0,
            (k_pump_chunk / 2u) * sizeof(std::int32_t)));
        if (scratch == nullptr || accumulator == nullptr)
        {
            if (scratch != nullptr) { HeapFree(GetProcessHeap(), 0, scratch); }
            if (accumulator != nullptr) { HeapFree(GetProcessHeap(), 0, accumulator); }
            return 0;
        }
        while (InterlockedCompareExchange(&g_stop_pump, 0, 0) == 0)
        {
            Sleep(k_pump_interval_ms);

            const std::uint64_t now = now_microseconds();
            if (g_last_pump_us == 0) { g_last_pump_us = now; }
            std::uint64_t frames =
                ((now - g_last_pump_us) * k_out_rate) / 1000000ull;
            const std::uint32_t frame_bytes = k_out_channels * 2u;
            const std::uint64_t max_frames = k_pump_chunk / frame_bytes;
            if (frames > max_frames) { frames = max_frames; }

            std::uint32_t produced = 0;
            if (frames > 0)
            {
                g_last_pump_us = now;
                std::memset(accumulator, 0,
                            static_cast<std::size_t>(frames) * k_out_channels *
                                sizeof(std::int32_t));
                {
                    Guard guard;
                    for (std::size_t i = 0; i < g_buffer_count; ++i)
                    {
                        if (g_buffers[i] != nullptr && g_buffers[i]->playing())
                        {
                            g_buffers[i]->mix_into(now, accumulator,
                                                   static_cast<std::uint32_t>(frames),
                                                   k_out_rate, k_out_channels);
                        }
                    }
                }
                auto* samples = reinterpret_cast<std::int16_t*>(scratch);
                const std::uint32_t count =
                    static_cast<std::uint32_t>(frames) * k_out_channels;
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    samples[i] = clamp_sample(accumulator[i]);
                }
                produced = count * 2u;
            }

            if (produced == 0)
            {
                continue;
            }
            if (!ensure_socket())
            {
                continue;
            }
            if (!g_header_sent)
            {
                StreamHeader header;
                header.sample_rate = k_out_rate;
                header.channels = k_out_channels;
                header.bits_per_sample = 16;
                if (!send_all(&header, sizeof(header)))
                {
                    continue;
                }
                g_header_sent = true;
            }
            send_all(scratch, static_cast<int>(produced));
        }
        HeapFree(GetProcessHeap(), 0, scratch);
        HeapFree(GetProcessHeap(), 0, accumulator);
        return 0;
    }

    void start_pump_once()
    {
        if (g_pump != nullptr)
        {
            return;
        }
        g_pump = CreateThread(nullptr, 0, pump_thread, nullptr, 0, nullptr);
    }

    class SoaSoundBuffer final : public IDirectSoundBuffer8
    {
    public:
        SoaSoundBuffer(const WaveFormat& format, std::uint32_t bytes, bool primary)
            : primary_(primary)
        {
            stream_.configure(format, bytes);
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
        {
            if (out == nullptr)
            {
                return E_POINTER;
            }
            if (IsEqualIID(iid, IID_IUnknown) ||
                IsEqualIID(iid, IID_IDirectSoundBuffer) ||
                IsEqualIID(iid, IID_IDirectSoundBuffer8))
            {
                AddRef();
                *out = static_cast<IDirectSoundBuffer8*>(this);
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&references_));
        }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const LONG remaining = InterlockedDecrement(&references_);
            if (remaining == 0)
            {
                {
                    Guard guard;
                    for (std::size_t i = 0; i < g_buffer_count; ++i)
                    {
                        if (g_buffers[i] == &stream_)
                        {
                            g_buffers[i] = g_buffers[--g_buffer_count];
                            g_buffers[g_buffer_count] = nullptr;
                            break;
                        }
                    }
                }
                delete this;
            }
            return static_cast<ULONG>(remaining);
        }

        HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS caps) override
        {
            if (caps == nullptr || caps->dwSize < sizeof(DSBCAPS))
            {
                return DSERR_INVALIDPARAM;
            }
            caps->dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCSOFTWARE;
            caps->dwBufferBytes = stream_.size();
            caps->dwUnlockTransferRate = 0;
            caps->dwPlayCpuOverhead = 0;
            return DS_OK;
        }

        HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD play, LPDWORD write) override
        {
            const std::uint64_t now = now_microseconds();
            if (play != nullptr)
            {
                *play = stream_.play_cursor(now);
            }
            if (write != nullptr)
            {
                *write = stream_.write_cursor(now);
            }
            return DS_OK;
        }

        HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX format, DWORD size,
                                            LPDWORD written) override
        {
            if (written != nullptr)
            {
                *written = sizeof(WAVEFORMATEX);
            }
            if (format == nullptr)
            {
                return DS_OK;
            }
            if (size < sizeof(WAVEFORMATEX))
            {
                return DSERR_INVALIDPARAM;
            }
            const WaveFormat& source = stream_.format();
            format->wFormatTag = WAVE_FORMAT_PCM;
            format->nChannels = source.channels;
            format->nSamplesPerSec = source.sample_rate;
            format->wBitsPerSample = source.bits_per_sample;
            format->nBlockAlign = static_cast<WORD>(source.block_align());
            format->nAvgBytesPerSec = source.bytes_per_second();
            format->cbSize = 0;
            return DS_OK;
        }

        HRESULT STDMETHODCALLTYPE GetVolume(LPLONG volume) override
        {
            if (volume != nullptr) { *volume = volume_; }
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE GetPan(LPLONG pan) override
        {
            if (pan != nullptr) { *pan = pan_; }
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD frequency) override
        {
            if (frequency != nullptr) { *frequency = stream_.format().sample_rate; }
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD status) override
        {
            if (status != nullptr)
            {
                *status = stream_.playing()
                              ? (DSBSTATUS_PLAYING |
                                 (looping_ ? DSBSTATUS_LOOPING : 0u))
                              : 0u;
            }
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) override
        {
            return DS_OK;
        }

        HRESULT STDMETHODCALLTYPE Lock(DWORD offset, DWORD bytes, LPVOID* ptr1,
                                       LPDWORD size1, LPVOID* ptr2, LPDWORD size2,
                                       DWORD flags) override
        {
            if (!stream_.configured() || ptr1 == nullptr || size1 == nullptr)
            {
                return DSERR_INVALIDPARAM;
            }
            if ((flags & DSBLOCK_ENTIREBUFFER) != 0)
            {
                offset = 0;
                bytes = stream_.size();
            }
            else if ((flags & DSBLOCK_FROMWRITECURSOR) != 0)
            {
                offset = stream_.write_cursor(now_microseconds());
            }
            const LockRegions regions = stream_.lock(offset, bytes);
            *ptr1 = stream_.data() + regions.offset1;
            *size1 = regions.size1;
            if (ptr2 != nullptr) { *ptr2 = stream_.data() + regions.offset2; }
            if (size2 != nullptr) { *size2 = regions.size2; }
            return DS_OK;
        }

        HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD flags) override
        {
            Guard guard;
            looping_ = (flags & DSBPLAY_LOOPING) != 0;
            stream_.play(now_microseconds(), looping_);
            if (!primary_)
            {
                bool known = false;
                for (std::size_t i = 0; i < g_buffer_count; ++i)
                {
                    if (g_buffers[i] == &stream_) { known = true; break; }
                }
                if (!known && g_buffer_count < k_max_buffers)
                {
                    g_buffers[g_buffer_count++] = &stream_;
                }
            }
            start_pump_once();
            return DS_OK;
        }

        HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD position) override
        {
            stream_.set_cursor(position, now_microseconds());
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) override { return DS_OK; }
        HRESULT STDMETHODCALLTYPE SetVolume(LONG volume) override
        {
            volume_ = volume;
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE SetPan(LONG pan) override
        {
            pan_ = pan;
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE SetFrequency(DWORD) override { return DS_OK; }
        HRESULT STDMETHODCALLTYPE Stop() override
        {
            Guard guard;
            stream_.stop(now_microseconds());
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override
        {
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE Restore() override { return DS_OK; }

        HRESULT STDMETHODCALLTYPE SetFX(DWORD, LPDSEFFECTDESC, LPDWORD) override
        {
            return DSERR_CONTROLUNAVAIL;
        }
        HRESULT STDMETHODCALLTYPE AcquireResources(DWORD, DWORD, LPDWORD) override
        {
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE GetObjectInPath(REFGUID, DWORD, REFGUID,
                                                  void**) override
        {
            return DSERR_CONTROLUNAVAIL;
        }

    private:
        LONG references_ = 1;
        bool primary_ = false;
        bool looping_ = false;
        LONG volume_ = DSBVOLUME_MAX;
        LONG pan_ = DSBPAN_CENTER;
        StreamingBuffer stream_;
    };

    class SoaDirectSound final : public IDirectSound8
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
        {
            if (out == nullptr)
            {
                return E_POINTER;
            }
            if (IsEqualIID(iid, IID_IUnknown) ||
                IsEqualIID(iid, IID_IDirectSound) ||
                IsEqualIID(iid, IID_IDirectSound8))
            {
                AddRef();
                *out = static_cast<IDirectSound8*>(this);
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&references_));
        }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const LONG remaining = InterlockedDecrement(&references_);
            if (remaining == 0)
            {
                delete this;
            }
            return static_cast<ULONG>(remaining);
        }

        HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC desc,
                                                    LPDIRECTSOUNDBUFFER* out,
                                                    LPUNKNOWN outer) override
        {
            if (desc == nullptr || out == nullptr)
            {
                return DSERR_INVALIDPARAM;
            }
            if (outer != nullptr)
            {
                return DSERR_NOAGGREGATION;
            }

            const bool primary = (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) != 0;
            WaveFormat format;
            if (desc->lpwfxFormat != nullptr)
            {
                format.sample_rate = desc->lpwfxFormat->nSamplesPerSec;
                format.channels = desc->lpwfxFormat->nChannels;
                format.bits_per_sample = desc->lpwfxFormat->wBitsPerSample;
            }
            if (!format.valid())
            {
                return DSERR_INVALIDPARAM;
            }

            std::uint32_t bytes = primary ? format.block_align() * 1024u
                                          : desc->dwBufferBytes;
            if (bytes == 0 || bytes > k_max_buffer_bytes)
            {
                return DSERR_INVALIDPARAM;
            }
            bytes -= bytes % format.block_align();

            auto* buffer = new (std::nothrow) SoaSoundBuffer(format, bytes, primary);
            if (buffer == nullptr)
            {
                return DSERR_OUTOFMEMORY;
            }
            *out = reinterpret_cast<LPDIRECTSOUNDBUFFER>(
                static_cast<IDirectSoundBuffer8*>(buffer));
            return DS_OK;
        }

        HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS caps) override
        {
            if (caps == nullptr || caps->dwSize < sizeof(DSCAPS))
            {
                return DSERR_INVALIDPARAM;
            }
            caps->dwFlags = DSCAPS_CONTINUOUSRATE | DSCAPS_SECONDARY16BIT |
                            DSCAPS_SECONDARYSTEREO | DSCAPS_PRIMARY16BIT |
                            DSCAPS_PRIMARYSTEREO;
            caps->dwMinSecondarySampleRate = 4000;
            caps->dwMaxSecondarySampleRate = 192000;
            caps->dwPrimaryBuffers = 1;
            caps->dwFreeHwMixingAllBuffers = 0;
            caps->dwFreeHw3DAllBuffers = 0;
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER,
                                                       LPDIRECTSOUNDBUFFER*) override
        {
            return DSERR_INVALIDCALL;
        }
        HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override
        {
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE Compact() override { return DS_OK; }
        HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD config) override
        {
            if (config != nullptr) { *config = DSSPEAKER_STEREO; }
            return DS_OK;
        }
        HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override { return DS_OK; }
        HRESULT STDMETHODCALLTYPE Initialize(LPCGUID) override { return DS_OK; }
        HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD certified) override
        {
            if (certified != nullptr) { *certified = DS_UNCERTIFIED; }
            return DS_OK;
        }

    private:
        LONG references_ = 1;
    };

    using DirectSoundCreateFn = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN);
    using DirectSoundCreate8Fn = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);

    DirectSoundCreateFn original_create = nullptr;
    DirectSoundCreate8Fn original_create8 = nullptr;

    HRESULT WINAPI pipe_direct_sound_create(LPCGUID, LPDIRECTSOUND* out, LPUNKNOWN outer)
    {
        if (out == nullptr) { return DSERR_INVALIDPARAM; }
        if (outer != nullptr) { *out = nullptr; return DSERR_NOAGGREGATION; }
        auto* device = new (std::nothrow) SoaDirectSound();
        if (device == nullptr) { *out = nullptr; return DSERR_OUTOFMEMORY; }
        *out = reinterpret_cast<LPDIRECTSOUND>(device);
        log_line("DirectSoundCreate served by the pipe backend");
        return DS_OK;
    }

    HRESULT WINAPI pipe_direct_sound_create8(LPCGUID, LPDIRECTSOUND8* out, LPUNKNOWN outer)
    {
        if (out == nullptr) { return DSERR_INVALIDPARAM; }
        if (outer != nullptr) { *out = nullptr; return DSERR_NOAGGREGATION; }
        auto* device = new (std::nothrow) SoaDirectSound();
        if (device == nullptr) { *out = nullptr; return DSERR_OUTOFMEMORY; }
        *out = device;
        log_line("DirectSoundCreate8 served by the pipe backend");
        return DS_OK;
    }
}

extern "C" bool soa_register_pipe_dsound_hooks()
{
    wchar_t enabled[8] {};
    if (GetEnvironmentVariableW(L"SOA_AUDIO_PIPE", enabled,
                                static_cast<DWORD>(std::size(enabled))) == 0 ||
        enabled[0] != L'1')
    {
        return true;
    }
    wchar_t silent[8] {};
    if (GetEnvironmentVariableW(L"SOA_AUDIO_NULL_BACKEND", silent,
                                static_cast<DWORD>(std::size(silent))) != 0 &&
        silent[0] == L'1')
    {
        return true;
    }

    if (!g_lock_ready)
    {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
    }

    if (MH_CreateHookApi(L"dsound.dll", "DirectSoundCreate",
                         reinterpret_cast<void*>(pipe_direct_sound_create),
                         reinterpret_cast<void**>(&original_create)) != MH_OK)
    {
        return false;
    }
    if (MH_CreateHookApi(L"dsound.dll", "DirectSoundCreate8",
                         reinterpret_cast<void*>(pipe_direct_sound_create8),
                         reinterpret_cast<void**>(&original_create8)) != MH_OK)
    {
        return false;
    }
    soa_audio_log("audio-pipe", "pipe DirectSound backend registered");
    return true;
}

extern "C" void soa_shutdown_pipe_dsound()
{
    InterlockedExchange(&g_stop_pump, 1);
    if (g_pump != nullptr)
    {
        WaitForSingleObject(g_pump, 500);
        CloseHandle(g_pump);
        g_pump = nullptr;
    }
    if (g_socket != INVALID_SOCKET)
    {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
    if (g_winsock_ready)
    {
        WSACleanup();
        g_winsock_ready = false;
    }
}
