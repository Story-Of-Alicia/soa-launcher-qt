// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    static_assert(sizeof(void*) == 4, "The Alicia injector must be built as Windows x86");

    constexpr wchar_t k_hook_name[] = L"SoaAliciaLogHook.dll";
    constexpr const char* k_initializer_names[] {
        "SoaLogHookInitialize",
        "SoaLogHookInitialize@4",
        "_SoaLogHookInitialize@4",
    };

    std::wstring module_directory()
    {
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
            return {};
        std::wstring path(buffer.data(), length);
        const std::wstring::size_type slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    }

    std::wstring quote_argument(const std::wstring& argument)
    {
        if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
            return argument;

        std::wstring quoted(1, L'"');
        std::size_t backslashes = 0;
        for (const wchar_t character : argument)
        {
            if (character == L'\\')
            {
                ++backslashes;
                continue;
            }
            if (character == L'"')
            {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted.push_back(L'"');
                backslashes = 0;
                continue;
            }
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
        quoted.append(backslashes * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    std::wstring command_line(const int argc, wchar_t* argv[])
    {
        std::wstring result;
        for (int index = 1; index < argc; ++index)
        {
            if (!result.empty())
                result.push_back(L' ');
            result += quote_argument(argv[index]);
        }
        return result;
    }

    void append_status(const wchar_t* message, const DWORD error = ERROR_SUCCESS)
    {
        wchar_t line[1024] {};
        const int count = error == ERROR_SUCCESS
            ? wsprintfW(line, L"[injector] %s\r\n", message)
            : wsprintfW(line, L"[injector] %s (win32_error=%lu)\r\n", message, error);
        if (count <= 0)
            return;

        const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, line, count, nullptr, 0,
                                                  nullptr, nullptr);
        if (utf8_size <= 0)
            return;
        std::vector<char> utf8(static_cast<std::size_t>(utf8_size));
        WideCharToMultiByte(CP_UTF8, 0, line, count, utf8.data(), utf8_size,
                            nullptr, nullptr);

        const HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
        if (stderr_handle && stderr_handle != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(stderr_handle, utf8.data(), static_cast<DWORD>(utf8.size()),
                      &written, nullptr);
        }

        wchar_t path[32768] {};
        const DWORD length = GetEnvironmentVariableW(
            L"SOA_ALICIA_LOG_PATH", path, static_cast<DWORD>(std::size(path)));
        if (length == 0 || length >= std::size(path))
            return;

        const HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(file);
    }

    FARPROC same_bitness_kernel_proc(const char* procedure)
    {
        const HMODULE local_kernel = GetModuleHandleW(L"kernel32.dll");
        return local_kernel ? GetProcAddress(local_kernel, procedure) : nullptr;
    }

    bool inject_and_initialize(const PROCESS_INFORMATION& process, const std::wstring& dll_path)
    {
        const std::size_t bytes = (dll_path.size() + 1) * sizeof(wchar_t);
        void* remote_path = VirtualAllocEx(process.hProcess, nullptr, bytes,
                                           MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote_path)
        {
            append_status(L"VirtualAllocEx failed", GetLastError());
            return false;
        }

        SIZE_T written = 0;
        if (!WriteProcessMemory(process.hProcess, remote_path, dll_path.c_str(), bytes, &written) ||
            written != bytes)
        {
            append_status(L"WriteProcessMemory failed", GetLastError());
            VirtualFreeEx(process.hProcess, remote_path, 0, MEM_RELEASE);
            return false;
        }

        const FARPROC remote_load_library = same_bitness_kernel_proc("LoadLibraryW");
        if (!remote_load_library)
        {
            append_status(L"could not resolve same-bitness LoadLibraryW", GetLastError());
            VirtualFreeEx(process.hProcess, remote_path, 0, MEM_RELEASE);
            return false;
        }
        append_status(L"using same-bitness LoadLibraryW injection path");

        const HANDLE load_thread = CreateRemoteThread(
            process.hProcess, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_load_library), remote_path, 0, nullptr);
        if (!load_thread)
        {
            append_status(L"CreateRemoteThread(LoadLibraryW) failed", GetLastError());
            VirtualFreeEx(process.hProcess, remote_path, 0, MEM_RELEASE);
            return false;
        }

        const DWORD load_wait = WaitForSingleObject(load_thread, 30000);
        DWORD remote_module_value = 0;
        if (load_wait != WAIT_OBJECT_0)
        {
            append_status(load_wait == WAIT_TIMEOUT
                              ? L"remote LoadLibraryW timed out"
                              : L"waiting for remote LoadLibraryW failed",
                          load_wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
            CloseHandle(load_thread);
            VirtualFreeEx(process.hProcess, remote_path, 0, MEM_RELEASE);
            return false;
        }
        if (!GetExitCodeThread(load_thread, &remote_module_value))
        {
            append_status(L"could not read remote LoadLibraryW result", GetLastError());
            CloseHandle(load_thread);
            VirtualFreeEx(process.hProcess, remote_path, 0, MEM_RELEASE);
            return false;
        }
        CloseHandle(load_thread);
        VirtualFreeEx(process.hProcess, remote_path, 0, MEM_RELEASE);
        if (remote_module_value == 0)
        {
            append_status(L"remote LoadLibraryW returned null");
            return false;
        }
        append_status(L"hook DLL loaded into Alicia");

        append_status(L"hook initialization delegated to loaded DLL");
        return true;
    }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    append_status(L"injector process started");
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 2)
    {
        if (argv)
            LocalFree(argv);
        return 2;
    }

    const std::wstring target = argv[1];
    std::wstring mutable_command = command_line(argc, argv);
    LocalFree(argv);
    if (target.empty() || mutable_command.empty())
        return 2;

    const std::wstring directory = module_directory();
    const std::wstring dll_path = directory.empty() ? std::wstring(k_hook_name)
                                                     : directory + L"\\" + k_hook_name;
    if (GetFileAttributesW(dll_path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        append_status(L"adjacent hook DLL is missing", GetLastError());
        return 3;
    }

    std::vector<wchar_t> command_buffer(mutable_command.begin(), mutable_command.end());
    command_buffer.push_back(L'\0');

    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(target.c_str(), command_buffer.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                        &startup, &process))
    {
        append_status(L"CreateProcessW(Alicia.exe) failed", GetLastError());
        return 4;
    }
    append_status(L"Alicia.exe created suspended; loading hook DLL");
    SetEnvironmentVariableW(L"SOA_ALICIA_LOG_REDACT", nullptr);
    SetEnvironmentVariableW(L"SOA_ALICIA_LOG_ALL", nullptr);

    if (!inject_and_initialize(process, dll_path))
    {
        TerminateProcess(process.hProcess, 5);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 5;
    }

    append_status(L"Alicia log hook initialized");
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1))
    {
        append_status(L"ResumeThread failed", GetLastError());
        TerminateProcess(process.hProcess, 6);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 6;
    }

    append_status(L"Alicia.exe resumed");
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}
