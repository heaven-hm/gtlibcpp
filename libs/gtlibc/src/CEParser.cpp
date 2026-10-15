#include "CEParser.hpp"
#include "GTLibc.hpp"

using namespace GTLIBC;

void CheatTable::AddCheatEntry(std::shared_ptr<CheatEntry> entry)
{
    cheatEntries.push_back(entry);
}

void CheatTable::AddCheatEntry(const std::string &description, int id, const string &dataType, const DWORD address,
                               const vector<DWORD> &offsets, const HOTKEYS &hotkeys)
{
    // Create a cheat entry object and pass the parameters to it.
    auto entry = std::make_shared<CheatEntry>(description, id, dataType, address, offsets, hotkeys);

    // Add the cheat entry to the cheat table.
    AddCheatEntry(entry);
}

DWORD CheatTable::ParseAddress(const string &address)
{
    std::smatch matches;
    regex_search(address, matches, std::regex("(\"([^\"]+)\")?([^+]+)?\\s*(\\+)?\\s*(0x)?([0-9A-Fa-f]+)?"));
    string moduleName = matches[2].str().empty() ? matches[3].str() : matches[2].str();
    string offsetStr = matches[6].str();

    bool isModuleNameHex = std::all_of(moduleName.begin(), moduleName.end(), [](char c)
                                       { return std::isxdigit(c); });

    if (isModuleNameHex)
    {
        DWORD absoluteAddress = stoul(moduleName, nullptr, 16);
        return absoluteAddress;
    }

    DWORD offset = offsetStr.empty() ? 0 : stoul(offsetStr, nullptr, 16);
    DWORD baseAddress = moduleName.empty() ? 0 : gameBaseAddress;
    return baseAddress + offset;
}

vector<DWORD> CheatTable::ParseOffsets(const string &offsets)
{
    vector<DWORD> result;
    std::smatch matches;
    std::regex offsetRegex("<Offset>([0-9a-fA-F]+)</Offset>");

    auto offsetsBegin = std::__cxx11::sregex_iterator(offsets.begin(), offsets.end(), offsetRegex);
    auto offsetsEnd = std::__cxx11::sregex_iterator();

    for (auto i = offsetsBegin; i != offsetsEnd; ++i)
    {
        result.push_back(stoul((*i)[1].str(), nullptr, 16));
    }

    return result;
}

HOTKEYS CheatTable::ParseHotkeys(const string &hotkeys)
{
    HOTKEYS result;
    std::smatch matches;
    std::regex hotkeyRegex("<Hotkey>([\\s\\S]*?)<Action>([\\s\\S]*?)</Action>([\\s\\S]*?)<Keys>([\\s\\S]*?)</Keys>(?:[\\s\\S]*?<Value>([\\s\\S]*?)</Value>)?[\\s\\S]*?<ID>([\\s\\S]*?)</ID>([\\s\\S]*?)</Hotkey>");

    auto hotkeysBegin = std::sregex_iterator(hotkeys.begin(), hotkeys.end(), hotkeyRegex);
    auto hotkeysEnd = std::sregex_iterator();

    for (auto i = hotkeysBegin; i != hotkeysEnd; ++i)
    {
        string action = (*i)[2].str();
        vector<int> keys;
        string keysStr = (*i)[4].str();
        // trim the keysStr.
        keysStr = keysStr.substr(1, keysStr.size() - 2);

        std::smatch keyMatches;
        std::regex keyRegex("<Key>([0-9]+)</Key>");

        auto keysBegin = std::sregex_iterator(keysStr.begin(), keysStr.end(), keyRegex);
        auto keysEnd = std::sregex_iterator();

        for (auto j = keysBegin; j != keysEnd; ++j)
        {
            keys.push_back(stoul((*j)[1].str()));
        }

        string value = (*i)[5].str();
        int id = std::stoi((*i)[6].str());
        result.push_back(make_tuple(action, keys, value, id));
    }
    return result;
}

void CheatTable::ParseNestedCheatEntries(const string &parentNode, std::shared_ptr<CheatEntry> &parentEntry)
{
    std::smatch entryMatches;
    std::regex entryRegex("<CheatEntry>.*?</CheatEntry>");
    auto entriesBegin = std::sregex_iterator(parentNode.begin(), parentNode.end(), entryRegex);
    auto entriesEnd = std::sregex_iterator();

    for (auto i = entriesBegin; i != entriesEnd; ++i)
    {
        string entryStr = (*i).str();
        std::smatch matches;

        regex_search(entryStr, matches, std::regex("<Description>(.*?)</Description>"));
        string description = matches[1].str();

        regex_search(entryStr, matches, std::regex("<ID>(\\d+)</ID>"));
        int id = std::stoi(matches[1].str());

        regex_search(entryStr, matches, std::regex("<VariableType>(.*?)</VariableType>"));
        string variableType = matches[1].str();

        regex_search(entryStr, matches, std::regex("<Address>(.*?)</Address>"));
        DWORD address = variableType == "Auto Assembler Script" ? 0 : ParseAddress(matches[1].str());

        vector<DWORD> offsets = ParseOffsets(entryStr);
        auto hotkeys = ParseHotkeys(entryStr);

        std::shared_ptr<CheatEntry> entry = std::make_shared<CheatEntry>(description, id, variableType, address, offsets, hotkeys);
        parentEntry->NestedEntries.push_back(entry);

        ParseNestedCheatEntries(entryStr, entry);
    }
}

std::string CheatTable::AddCheatEntryLog(std::shared_ptr<CheatEntry> &entry)
{
        // Adding log entry data to log.
        std::stringstream logEntry;
        logEntry << "Description: " << entry->Description << " "
                 << "ID: " << entry->Id << " "
                 << "VariableType: " << entry->VariableType << " "
                 << "Address: " << entry->Address << " "
                 << "Offsets: ";

        for (auto &offset : entry->Offsets)
        {
            logEntry << offset << " ";
        }

        logEntry << "Hotkeys: ";
        for (auto &hotkey : entry->Hotkeys)
        {
            logEntry << "Action: " << std::get<0>(hotkey) << " "
                     << "Keys: [";

            for (auto &key : std::get<1>(hotkey))
            {
                logEntry << g_GTLibc->KeyCodeToName(key) << " ";
            }

            logEntry << "] "
                     << "Value: " << std::get<2>(hotkey) << " "
                     << "ID: " << std::get<3>(hotkey) << " ";
        }
    return logEntry.str();
}

CheatTable CheatTable::ParseCheatTable(const string &cheatTableData, int entries = -1)
{
    g_GTLibc->AddLog("ParseCheatTable", "Parsing cheat table with data size: " + std::to_string(cheatTableData.size()) + " and entries: " + std::to_string(entries));
    CheatTable cheatTable;
    std::smatch entryMatches;
    std::regex entryRegex("<CheatEntry>([\\s\\S]*?)</CheatEntry>");
    auto entriesBegin = std::sregex_iterator(cheatTableData.begin(), cheatTableData.end(), entryRegex);
    auto entriesEnd = std::sregex_iterator();

    for (auto i = entriesBegin; i != entriesEnd; ++i)
    {
        string entryStr = (*i).str();
        std::smatch matches;

        regex_search(entryStr, matches, std::regex("<Description>(.*?)</Description>"));
        string description = matches[1].str();

        regex_search(entryStr, matches, std::regex("<ID>(\\d+)</ID>"));
        int id = std::stoi(matches[1].str());

        regex_search(entryStr, matches, std::regex("<VariableType>(.*?)</VariableType>"));
        string variableType = matches[1].str();

        regex_search(entryStr, matches, std::regex("<Address>(.*?)</Address>"));
        DWORD address = variableType == "Auto Assembler Script" ? 0 : ParseAddress(matches[1].str());

        vector<DWORD> offsets = ParseOffsets(entryStr);
        auto hotkeys = ParseHotkeys(entryStr);
        std::shared_ptr<CheatEntry> entry = std::make_shared<CheatEntry>(description, id, variableType, address, offsets, hotkeys);
        std::string cheatEntryLog = AddCheatEntryLog(entry);
        g_GTLibc->AddLog("ParseCheatTable", "Parsed entry: " + cheatEntryLog);

        cheatTable.AddCheatEntry(entry);
        ParseNestedCheatEntries(entryStr, entry);

        if (entries > 0 && cheatTable.cheatEntries.size() >= entries)
        {
            g_GTLibc->AddLog("ParseCheatTable", "Reached max entries: " + std::to_string(entries));
            break;
        }
        g_GTLibc->AddLog("ParseCheatTable", "Finished parsing entry: " + entry->Description);
    }

    return cheatTable;
}