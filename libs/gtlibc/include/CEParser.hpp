#pragma once

#ifndef GT_USE_CE_PARSER
#error "GT_USE_CE_PARSER must be defined to use this header"
#endif

#ifndef CE_PARSER_H
#define CE_PARSER_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <memory>
#include <cstdint>
#include <variant>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>
#include <ranges>
#include <tuple>
#include <variant>
#include <windows.h>

using std::string;
using std::vector;
using std::tuple;

typedef tuple<string, vector<int>, string, int> HOTKEY;
typedef vector<tuple<string, vector<int>, string, int>> HOTKEYS;
using DataType = std::variant<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, float, double, std::string>;

namespace GTLIBC
{
    // Define HotkeyAction struct
    struct CheatAction
    {
        string SetValue;
        string IncreaseValue;
        string DecreaseValue;
        string Freeze;
        string Unfreeze;
        string ToggleFreeze;
    };

    // Define CheatDataType struct
    struct CheatType
    {
        string Byte;
        string Short;
        string Integer;
        string Long;
        string Float;
        string Double;
        string String;
    };
    
    inline CheatAction CheatActions = {"Set Value", "Increase Value", "Decrease Value", "Activate", "Deactivate","Toggle Activation"};
    inline CheatType CheatTypes = {"Byte", "2 Bytes", "4 Bytes", "8 Bytes", "Float", "Double", "String"};

    // Define CheatEntry class
    class CheatEntry
    {
    public:
        string Description;
        int Id;
        string VariableType;
        DWORD Address;
        vector<DWORD> Offsets;
        HOTKEYS Hotkeys;
        vector<std::shared_ptr<CheatEntry>> NestedEntries;
        DataType Value;
        string Action;
        vector<int> HotkeyIds;

        CheatEntry(const std::string &description, int id, const std::string &variableType, DWORD address,
                   const vector<DWORD> &offsets, const HOTKEYS &hotkeys)
            : Description(description), Id(id), VariableType(variableType), Address(address),
              Offsets(offsets), Hotkeys(hotkeys) {}
    };

    class CheatTable
    {
    public:
        // Create default constructor and parameterized constructor with setting game base address.
        CheatTable() = default;
        CheatTable(DWORD gameBaseAddress) : gameBaseAddress(gameBaseAddress) {}
        
        bool IsEmpty() const
        {
            return cheatEntries.empty();
        }
        
        vector<std::shared_ptr<CheatEntry>> cheatEntries;

        void SetGameBaseAddress(DWORD gameBaseAddress)
        {
            this->gameBaseAddress = gameBaseAddress;
        }

        CheatTable ParseCheatTable(const std::string &cheatTableData,int entries);
        void AddCheatEntry(std::shared_ptr<CheatEntry> entry);
        void AddCheatEntry(const std::string &description, int id, const std::string &dataType, const DWORD address,
                           const vector<DWORD> &offsets, const HOTKEYS &hotkeys);
    private:
        // Create variable and method to get base address of the game.
        DWORD gameBaseAddress = 0x00400000;
        
        DWORD getGameBaseAddress()
        {
            return gameBaseAddress;
        }

        DWORD ParseAddress(const std::string &address);
        vector<DWORD> ParseOffsets(const std::string &offsets);
        HOTKEYS ParseHotkeys(const std::string &hotkeys);
        void ParseNestedCheatEntries(const std::string &parentNode, std::shared_ptr<CheatEntry> &parentEntry);
        std::string AddCheatEntryLog(std::shared_ptr<CheatEntry> &entry);
    };
} // namespace GTLibc

#endif