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
#include <vector>
#include "gtlibcpp/types.hpp"

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
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    std::uint32_t result = 0;
    if (Process32First(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, wanted.c_str()) == 0) {
                result = entry.th32ProcessID;
                break;
            }
        } while (Process32Next(snapshot, &entry));
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

struct MemoryRegion {
    Address base_address{0};
    std::size_t size{0};
    bool is_readable{false};
    bool is_writable{false};
    bool is_executable{false};
};

inline std::vector<MemoryRegion> enumerate_memory_regions(HANDLE process, bool writable_only = false) {
    std::vector<MemoryRegion> regions;
    if (!process) return regions;

    std::uintptr_t current = 0;
    MEMORY_BASIC_INFORMATION mbi{};

    while (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(current), &mbi, sizeof(mbi)) != 0) {
        const bool is_commit = (mbi.State & MEM_COMMIT) != 0;
        const bool is_guard = (mbi.Protect & PAGE_GUARD) != 0;
        const bool is_noaccess = (mbi.Protect & PAGE_NOACCESS) != 0;

        if (is_commit && !is_guard && !is_noaccess) {
            const DWORD prot = mbi.Protect & 0xFF;
            const bool readable = (prot == PAGE_READONLY || prot == PAGE_READWRITE ||
                                   prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ ||
                                   prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY);
            const bool writable = (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
                                   prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY);
            const bool executable = (prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
                                     prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY);

            if (readable && (!writable_only || writable)) {
                MemoryRegion r{};
                r.base_address = reinterpret_cast<Address>(mbi.BaseAddress);
                r.size = mbi.RegionSize;
                r.is_readable = readable;
                r.is_writable = writable;
                r.is_executable = executable;
                regions.push_back(r);
            }
        }

        const std::uintptr_t next = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= current) break;
        current = next;
    }

    return regions;
}

inline std::vector<MemoryRegion> enumerate_process_regions(std::uint32_t pid, bool writable_only = false) {
    if (!pid) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    if (!process) return {};
    auto regions = enumerate_memory_regions(process, writable_only);
    CloseHandle(process);
    return regions;
}

} // namespace gtlibcpp
