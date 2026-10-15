/*
Brief : GTLibc is a library designed to facilitate the creation of game trainers in C++. with latest C++17 features.
It offers a comprehensive set of methods that enable developers to develop simple game trainers for the Windows operating system using the Win32 API with ease.
Notably, GTLibc exclusively employs Win32 API methods and eschews CRT methods as its primary aim is to operate exclusively on Windows systems and not to be portable to other operating systems such as Linux or Mac OS.
GTLibc provides all the requisite methods necessary for game trainer development from the inception of the project to its completion.
It streamlines the development process, making it less cumbersome for developers.

Author : HeavenHM
Date :  24/04/2023
GitHub : https://github.com/haseeb-heaven/GTLibCpp
Version : 1.0.0
License : MIT License
*/

#pragma once
#ifndef GTLIBC_H
#define GTLIBC_H

// Including the standard libraries
#include <iostream>      // Provides basic input/output
#include <string>        // Provides string class
#include <sstream>       // Provides stringstream class
#include <vector>        // Provides vector class
#include <stdexcept>     // Provides exceptions
#include <ctime>         // Provides time functions
#include <iomanip>       // Provides functions for formatting output
#include <chrono>        // Provides high resolution clock
#include <fstream>       // Provides file stream classes
#include <ostream>       // Provides ostream class
#include <regex>         // Provides regular expression classes
#include <variant>       // Provides variant class
#include <thread>        // Provides thread class
#include <optional>      // Provides optional class
#include <type_traits>   // Provides type traits
#include <array>         // Provides array class
#include <filesystem>    // Provides filesystem classes
#include <cstdint>       // Provides integer types
#include <charconv>      // Provides character conversion
#include <unordered_map> // Provides unordered_map class
#include <future>        // Provides the ability to create asynchronous tasks
#include <mutex>         // Provides the ability to create mutexes
#include <system_error> // Provides the ability to create system errors

/*Defining WIN32 Constants*/
#define WINVER 0x0500       // Sets the minimum required platform to Windows 2000.
#define _WIN32_WINNT 0x0501 // Sets the minimum required platform to Windows XP.

/*Including WIN32 libraries*/
#include <windows.h>
#include <tlhelp32.h>

/*Including Conditional Process library*/
#ifdef GT_USE_PROC_MODULES
#include <psapi.h>
#endif

// Including the CE Parser library.
#ifdef GT_USE_CE_PARSER
#include "CEParser.hpp"
#endif

// Re-Defining standard constants
#if !defined(FILE_NAME) && !defined(LINE_NO) && !defined(FUNC_NAME)
#define FILE_NAME __FILE__
#define LINE_NO __LINE__
#define FUNC_NAME __FUNCTION__
#endif

// Helper macros
#define to_hex_string(hex_val) (static_cast<std::stringstream const &>(std::stringstream() << std::uppercase << std::hex << hex_val)).str()
using DataType = std::variant<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, float, double, std::string>;
#define EXIT_TRAINER_KEY VK_F12 // Default exit trainer key is F12.

namespace GTLIBC
{
    class CheatTable; // Forward declaration of CheatTable class

    class GTLibc
    {
    public:
        GTLibc();                                             // Default constructor
        GTLibc(bool enableLogs);                              // Constructor
        GTLibc(const std::string &gameName);                  // Constructor
        GTLibc(const std::string &gameName, bool enableLogs); // Constructor
        ~GTLibc();                                            // Destructor
        GTLibc(const GTLibc &) = default;                     // Copy constructor
        GTLibc(GTLibc &&) = default;                          // Move constructor

        // GTLibc public methods.
        // Find game process and window.
        HANDLE FindGameProcess(const std::string &gameName);
        HWND FindGameWindow(const std::string &windowName);

        // Read and write memory.
        template <typename T>
        T ReadAddress(DWORD address);
        template <typename T>
        bool WriteAddress(DWORD address, const T &value);

        // Read  and write memory with offsets.
        template <typename T>
        T ReadAddressOffset(DWORD address, const DWORD offset);
        template <typename T>
        bool WriteAddressOffset(DWORD address, DWORD offset, const T &value);
        template <typename T>
        T ReadAddressOffsets(DWORD address, const std::vector<DWORD> &offsets);
        template <typename T>
        bool WriteAddressOffsets(DWORD address, const std::vector<DWORD> &offsets, const T &value);

        // Read and write pointers with offsets.
        template <typename T>
        T ReadPointer(DWORD address);
        template <typename T>
        bool WritePointer(DWORD address, DWORD pointerOffset, const T &value);

        template <typename T>
        T ReadPointerOffset(DWORD address, const DWORD offset);
        template <typename T>
        bool WritePointerOffset(DWORD address, const std::vector<DWORD> &offsets, const T &value);

        template <typename T>
        T ReadPointerOffsets(DWORD address, const std::vector<DWORD> &offsets);
        template <typename T>
        bool WritePointerOffsets(DWORD address, const std::vector<DWORD> &offsets, const T &value);

        // Reading and writing strings.
        std::string ReadString(DWORD address, size_t size);
        bool WriteString(DWORD address, const std::string &str);

        // Detecting Hotkeys.
        bool HotKeysDown(const std::vector<int> &keys);
        bool IsKeyPressed(int keycode);
        bool IsKeyToggled(int keycode);

        // Game process and window information.
        std::string GetGameName();
        DWORD GetProcessId();
        HANDLE GetGameHandle4mHWND(HWND hwnd);
        DWORD GetProcessID4mHWND(HWND hwnd);
        HANDLE GetGameHandle();
        DWORD GetGameBaseAddress();

        // Log methods.
        void EnableLogs(bool status);

// Cheat Engine variables. - Public
#ifdef GT_USE_CE_PARSER
        CheatTable ReadCheatTable(const std::string &cheatTableFile, int entries = -1);
        void AddCheatTableEntry(const std::string &description, const std::string &dataType, const DWORD address,
                                const std::vector<DWORD> &offsets, const std::vector<int> &hotkeys, const std::string &hotkeyAction,
                                const std::string hotkeyValue);
        void DisplayCheatTable(bool showMenuIndex = true, bool showMenuDescription = true, bool showMenuAction = false, bool showMenuHotkeys = true, bool showMenuValue = false);
        void ReadCheatTableEntries();
        void ActivateCheatTableEntries(const std::vector<int> &cheatEntryIndex);
        void ExecuteCheatTable(bool showTrainerOutput = false, int exitTrainerKey = EXIT_TRAINER_KEY, bool showMenuIndex = true, bool showMenuDescription = true, bool showMenuAction = false, bool showMenuHotkeys = true);
#endif

    private:
// Cheat Engine variables. - Private
#ifdef GT_USE_CE_PARSER
        void DisplayCheatValue(DataType &value);
        DataType ReadAddressGeneric(const std::string &dataType, DWORD address, const std::vector<DWORD> &offsets = {});
        DWORD ResolveAddressGeneric(DWORD address, const std::vector<DWORD> &offsets);
        bool IsValidCheatTable(const std::string &xmlData);
        void DisplayCheatTableMenu(bool showIndex = true, bool showDescription = true, bool showAction = false, bool showHotkeys = true, int exitTrainerKey = EXIT_TRAINER_KEY);
        template <typename T>
        void ExecuteCheatAction(const std::string &cheatAction, DWORD &address, const T &value);
        void ExecuteCheatActionForType(const std::string &cheatAction, DWORD &address, DataType &value, const string &variableType);
        void ExecuteCheatActionType(const std::string &cheatAction, DWORD &address, const std::string &value, const std::string &variableType);
        template <typename T>
        void ExecuteCheatActionType(const std::string &cheatAction, DWORD &address, const std::string &valueStr);
        // Cheat Action functions.
        template <typename T>
        void CheatAction_SetValue(DWORD address, T value);
        template <typename T>
        void CheatAction_IncreaseValue(DWORD address, T value);
        template <typename T>
        void CheatAction_DecreaseValue(DWORD address, T value);
        template <typename T>
        bool CheatAction_FreezeValue(DWORD address, T value);
        void CheatAction_UnfreezeValue(DWORD address);

#endif
        // GTLibc private methods.
        void AddLog(const std::string &methodName, const std::string &logMessage);
        void ShowError(const std::string &errorMessage);
        void ShowWarning(const std::string &warningMessage);
        void ShowInfo(const std::string &infoMessage);
        void ShowErrorLog(const std::string &methodName, const std::string &errorMessage);
        friend class CheatTable; // Make CheatTable class a friend of GTLibc class

        // Utility functions
        template <typename T>
        std::optional<T> TryParse(const std::string &str);
        DataType ConvertStringToDataType(const std::string &str);
        std::string KeyCodeToName(int keyCode);
        std::string GetHotKeysName(const std::vector<int> &keys);
        bool CheckGameTrainerArch();
        std::string GetArchitectureString(WORD wProcessorArchitecture);
        std::string ShellExec(const std::string &cmdArgs, bool runAsAdmin = false, bool waitForExit = true, const std::string &shell = "cmd");
        template <typename T>
        std::string ValueToString(const T &value);
        template <typename T>
        std::string GetDataTypeInfo(T type);
        std::string GetLastErrorAsString();
        bool IsElevatedProcess();

        std::string gameName;
        HWND gameWindow;
        DWORD processId;
        HANDLE gameHandle;
        bool enableLogs;
        DWORD gameBaseAddress;
        std::string logFile;
        bool showTrainerOutput;
        int cheatEntryId;
        std::unordered_map<DWORD, std::shared_ptr<std::atomic_bool>> freezeTokenSrcs;
    };
    inline GTLibc *g_GTLibc{};
#ifdef GT_USE_CE_PARSER
    inline static CheatTable g_CheatTable;
#endif
} // namespace GTLibc

#include "GTLibc.tpp"
#endif