#pragma once
#if !defined(_WIN32)
#error "gtlibcpp/windows_discovery.hpp is available on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace gtlibcpp {

// Discovery only: never requests PROCESS_ALL_ACCESS or mutates a target.
// Returns zero/nullptr when no matching process/window exists.
inline std::uint32_t find_game_process(std::string_view executable_name) {
    if (executable_name.empty()) return 0;
    std::string wanted(executable_name);
    if (wanted.size() < 4 || _stricmp(wanted.c_str() + wanted.size() - 4, ".exe") != 0)
        wanted += ".exe";
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32A entry{};
    entry.dwSize = sizeof(entry);
    std::uint32_t result = 0;
    if (Process32FirstA(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, wanted.c_str()) == 0) {
                result = entry.th32ProcessID;
                break;
            }
        } while (Process32NextA(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

inline HWND find_game_window(std::string_view title) {
    if (title.empty()) return nullptr;
    const std::string title_string(title);
    return FindWindowA(nullptr, title_string.c_str());
}

inline std::uint32_t get_process_id_from_window(HWND window) {
    if (!window || !IsWindow(window)) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid;
}

inline std::string get_process_image_path(std::uint32_t pid) {
    if (!pid) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::string path(32768, '\0');
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL ok = QueryFullProcessImageNameA(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!ok || !length) return {};
    path.resize(length);
    return path;
}

} // namespace gtlibcpp
