/*
This is the GTLibc header file containing only Template function declarations.
for full implementation, see GTLibc.cpp
*/

#pragma once
#include "GTLibc.hpp"
using namespace GTLIBC;

/*
 * @brief Read address
 * @param address - Address to read
 * @return T - Value read from address
 *
 */
template <typename T>
T GTLibc::ReadAddress(DWORD address)
{
    AddLog("ReadAddress", "Trying to read address: " + to_hex_string(address));
    try
    {
        T value;
        SIZE_T bytesRead;
        if (ReadProcessMemory(gameHandle, (LPVOID)address, &value, sizeof(T), &bytesRead) && bytesRead == sizeof(T))
        {
            AddLog("ReadAddress", "Read value: " + to_hex_string(value));
            return value;
        }
        throw std::runtime_error("Failed to read address \n" + GetLastErrorAsString());
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("ReadAddress", "Error: " + std::string(e.what()));
        // Return a default value or handle the error as needed
        return T();
    }
    // return default value for type T
    return T();
}

/*
 * @brief Write address
 * @param address - Address to write
 * @param value - Value to write to address
 * @return bool - True if write was successful else false
 *
 */

template <typename T>
bool GTLibc::WriteAddress(DWORD address, const T &value)
{
    AddLog("WriteAddress", "Trying to write to address: " + to_hex_string(address) + " with value: " + to_hex_string(value));
    try
    {
        SIZE_T bytesWritten;
        if (WriteProcessMemory(gameHandle, (LPVOID)address, &value, sizeof(T), &bytesWritten) && bytesWritten == sizeof(T))
        {
            return true;
        }
        throw std::runtime_error("Failed to write address \n" + GetLastErrorAsString());
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("WriteAddress", "Error: " + std::string(e.what()));
        return false;
    }
    return false;
}

/*
 * @brief Read address with offset
 * @param address - Address to read
 * @param offset - Offset to add to address
 * @return T - Value read from address
 *
 */
template <typename T>
T GTLibc::ReadAddressOffset(DWORD address, DWORD offset)
{
    AddLog("ReadAddressOffset", "Trying to read address with offset: " + to_hex_string(address) + " + " + to_hex_string(offset));
    try
    {
        DWORD newAddress = address + offset;
        T value = ReadAddress<T>(newAddress);
        AddLog("ReadAddressOffset", "Read value: " + to_hex_string(value));
        return value;
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("ReadAddressOffset", "Error: " + std::string(e.what()));
        // Return a default value or handle the error as needed
        return T();
    }
    // return default value for type T
    return T();
}

/*
 * @brief Read address with offsets
 * @param address - Address to read
 * @param offsets - Offsets to add to address
 * @return T - Value read from address
 *
 */
template <typename T>
T GTLibc::ReadAddressOffsets(DWORD address, const std::vector<DWORD> &offsets)
{
    AddLog("ReadAddressOffsets", "Trying to read address with offsets: " + to_hex_string(address));

    std::ostringstream oss;
    for (auto d : offsets)
        oss << to_hex_string(d) << ",";

    std::string result = oss.str();
    result.pop_back();
    AddLog("ReadPointerOffsets", "Offsets: " + result);

    try
    {
        DWORD currentAddress = address;
        for (DWORD offset : offsets)
        {
            currentAddress = ReadAddress<DWORD>(currentAddress + offset);
        }
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("ReadAddressOffsets", "Error: " + std::string(e.what()));
        // Return a default value or handle the error as needed
        return T();
    }

    return T();
}



/*
 * @brief Write address with offset
 * @param address - Address to write
 * @param offset - Offset to add to address
 * @param value - Value to write to address
 * @return bool - True if write was successful else false
 *
 */

template <typename T>
bool GTLibc::WriteAddressOffset(DWORD address, DWORD offset, const T &value)
{
    AddLog("WriteAddressOffset", "Trying to write to address: " + to_hex_string(address) + " with offset: " + to_hex_string(offset) + " with value: " + std::to_string(value));
    try
    {
        DWORD newAddress = address + offset;
        return WriteAddress(newAddress, value);
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("WriteAddressOffset", "Error: " + std::string(e.what()));
        return false;
    }
    return false;
}

/*
 * @brief Write address with offsets
 * @param address - Address to write
 * @param offsets - Offsets to add to address
 * @param value - Value to write to address
 * @return bool - True if write was successful else false
 *
 */

template <typename T>
bool GTLibc::WriteAddressOffsets(DWORD address, const std::vector<DWORD> &offsets, const T &value)
{
    AddLog("WriteAddressOffsets", "Trying to write to address: " + to_hex_string(address) + " with offsets: " + to_hex_string(offsets.size()) + " with value: " + std::to_string(value));
    try
    {
        for (const DWORD &offset : offsets)
        {
            WriteAddressOffset(address, offset, value);
        }
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("WriteAddressOffsets", "Error: " + std::string(e.what()));
        return false;
    }
    return false;
}

/*
 * @brief Write pointer to address
 * @param address - base address
 * @param offset - offset from pointer address
 * @param value - value to write
 * @return bool - true if successful or false if not
 *
 */
template <typename T>
bool GTLibc::WritePointer(DWORD address, DWORD offset, const T &value)
{
    AddLog("WritePointer", "Trying to write to pointer at base address: " + to_hex_string(address) + " with offset: " + to_hex_string(offset) + " with value: " + std::to_string(value));
    try
    {
        DWORD pointerAddress = ReadAddress<DWORD>(address);
        return WriteAddressOffset(pointerAddress, offset, value);
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("WritePointer", "Error: " + std::string(e.what()));
        return false;
    }
    return false;
}

/*
 * @brief Read pointer from address
 * @param address - base address
 * @return T
 *
 */
template <typename T>
T GTLibc::ReadPointer(DWORD address)
{
    const DWORD offset = 0;
    AddLog("ReadPointer", "Trying to read pointer at base address: " + to_hex_string(address) + " with offset: " + to_hex_string(offset));
    try
    {
        DWORD pointerAddress = ReadAddress<DWORD>(address);
        T returnValue = ReadPointerOffset<T>(pointerAddress, offset);
        AddLog("ReadPointer", "Return value: " + to_hex_string(returnValue));
        return returnValue;
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("ReadPointer", "Error: " + std::string(e.what()));
        // Return a default value or handle the error as needed
        return T();
    }
    // return default value for type T
    return T();
}

/*
 * @brief Read pointer with offset
 * @param address - base address
 * @param offset - offset from pointer address
 * @return T - value at pointer address
 *
 */
template <typename T>
T GTLibc::ReadPointerOffset(DWORD address, const DWORD offset)
{
    AddLog("ReadPointerOffset", "Trying to read pointer at base address: " + to_hex_string(address) + " with offset: " + to_hex_string(offset));
    try
    {
        DWORD pointerAddress = address + offset;
        pointerAddress = ReadAddress<DWORD>(pointerAddress);
        AddLog("ReadPointerOffset", "Return value: " + to_hex_string(pointerAddress));
        return pointerAddress;
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("ReadPointerOffset", "Error: " + std::string(e.what()));
        // Return a default value or handle the error as needed
        return T();
    }
    // return default value for type T
    return T();
}

/*
 * @brief Read pointer with offsets
 * @param address - base address
 * @param offsets - offsets from pointer address
 * @return T - value at pointer address
 *
 */

template <typename T>
T GTLibc::ReadPointerOffsets(DWORD address, const std::vector<DWORD> &offsets)
{
    AddLog("ReadPointerOffsets", "Trying to read pointer at base address: " + to_hex_string(address) + " with offsets size: " + std::to_string(offsets.size()));

    std::ostringstream oss;
    for (auto d : offsets)
        oss << std::hex << to_hex_string(d) << ",";

    std::string result = oss.str();
    result.pop_back();
    AddLog("ReadPointerOffsets", "Offsets: " + result);

    try
    {
        DWORD pointerAddress = address;
        for (const DWORD offsets : offsets)
        {
            pointerAddress = ReadPointerOffset<DWORD>(pointerAddress, offsets);
        }
        AddLog("ReadPointerOffsets", "Return value: " + to_hex_string(pointerAddress));
        return pointerAddress;
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("ReadPointerOffsets", "Error: " + std::string(e.what()));
        // Return a default value or handle the error as needed
        return T();
    }
    // return default value for type T
    return T();
}

/*
 * @brief Write pointer with offset
 * @param address - base address
 * @param offset - offset from pointer address
 * @param value - value to write
 * @return bool
 *
 */
template <typename T>
bool GTLibc::WritePointerOffset(DWORD address, const std::vector<DWORD> &offsets, const T &value)
{
    AddLog("WritePointerOffset", "Trying to write to pointer at base address: " + to_hex_string(address) + " with offsets: " + std::to_string(offsets.size()) + " with value: " + std::to_string(value));
    try
    {
        DWORD pointerAddress = ReadPointerOffsets<DWORD>(address, {offsets});
        return WriteAddress(pointerAddress, value);
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("WritePointerOffset", "Error: " + std::string(e.what()));
        return false;
    }
    return false;
}

/*
 * @brief Write pointer with offsets
 * @param address - base address
 * @param offsets - offsets from pointer address
 * @param value - value to write
 * @return bool - true if write was successful
 *
 */
template <typename T>
bool GTLibc::WritePointerOffsets(DWORD address, const std::vector<DWORD> &offsets, const T &value)
{
    AddLog("WritePointerOffsets", "Trying to write to pointer at base address: " + to_hex_string(address) + " with offsets: " + std::to_string(offsets.size()) + " with value: " + std::to_string(value));
    try
    {
        DWORD pointerAddress = ReadPointerOffsets<DWORD>(address, offsets);
        return WriteAddress(pointerAddress, value);
    }
    catch (const std::exception &e)
    {
        ShowErrorLog("WritePointerOffsets", "Error: " + std::string(e.what()));
        return false;
    }
    return false;
}