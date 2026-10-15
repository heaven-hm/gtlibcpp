/*
Brief: IGI Trainer +3 (Unlimited Health, Ammo, Clips).
It is a simple trainer for the game IGI 1 using the GTLibc library.
Author: HeavenHM
Date: 24/04/2023
*/

// Include the GTLibc header file.
#include "GTLibc.hpp"
using namespace GTLIBC;

GTLibc gtlibc(true);
DWORD gameBaseAddress;

DWORD GetClipsAddress()
{
    const DWORD clipsStaticPointer = (DWORD)0x00671890;
    const std::vector<DWORD> clipsAddrOff = {0x0, 0x4C4};

    DWORD clipsBasePointer = gtlibc.ReadPointerOffset<DWORD>(gameBaseAddress, clipsStaticPointer);
    DWORD clipsAddress = gtlibc.ReadPointerOffsets<DWORD>(clipsBasePointer, clipsAddrOff) + 0x144;
    return clipsAddress;
}

DWORD GetHumanBaseAddress()
{
    const DWORD humanStaticPointer = (DWORD)0x0016E210;
    const std::vector<DWORD> humanAddressOffsets = {0x8, 0x7CC, 0x14};

    DWORD humanBasePointer = gtlibc.ReadPointerOffset<DWORD>(gameBaseAddress, humanStaticPointer);
    DWORD humanBaseAddress = gtlibc.ReadPointerOffsets<DWORD>(humanBasePointer, humanAddressOffsets) + 0x348;

    return humanBaseAddress;
}

void SetAmmo()
{
    int unlimitedAmmo = 0x7FFFFFFF;
    DWORD weaponBaseAddress = GetHumanBaseAddress();

    std::vector<DWORD> weaponsOffsets = {0x0, 0xC, 0x18, 0x24, 0x30, 0x3C, 0x48, 0x54, 0x60, 0x6C};
    if (gtlibc.WriteAddressOffsets(weaponBaseAddress, weaponsOffsets, unlimitedAmmo))
    {
        std::cout << "[+] Unlimited ammo enabled" << std::endl;
    }
}

void SetClips()
{
    int unlimitedClips = 0xFFFFFF;
    DWORD clipsAddress = GetClipsAddress();

    if (gtlibc.WriteAddress(clipsAddress, unlimitedClips))
    {
        std::cout << "[+] Unlimited clips enabled" << std::endl;
    }
}

void SetHealth()
{
    int unlimitedHealth = 0xFFFFFFFF;
    DWORD healthAddress = GetHumanBaseAddress() - 0xF4;
    std::cout << "Health address: " << to_hex_string(healthAddress) << std::endl;
    if (gtlibc.WriteAddress(healthAddress, unlimitedHealth))
    {
        std::cout << "[+] Unlimited health enabled" << std::endl;
    }
}

int main()
{
    std::cout << "IGI Trainer +3." << std::endl;
    std::cout << "F1 - Unlimited health" << std::endl;
    std::cout << "F2 - Unlimited ammo" << std::endl;
    std::cout << "F3 - Unlimited clips" << std::endl;
    std::cout << "F5 - Exit" << std::endl;

    std::string gameName = "igi";
    HANDLE gameHandle = gtlibc.FindGameProcess(gameName);

    if (gameHandle == NULL)
    {
        return EXIT_FAILURE;
    }

    gameBaseAddress = gtlibc.GetGameBaseAddress();

    while (true)
    {
        if (gtlibc.IsKeyToggled(VK_F1))
        {
            SetHealth();
        }
        else if (gtlibc.IsKeyToggled(VK_F2))
        {
            SetAmmo();
        }
        else if (gtlibc.IsKeyToggled(VK_F3))
        {
            SetClips();
        }

        else if (gtlibc.IsKeyToggled(VK_F5))
        {
            break;
        }

        // Sleep for 100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Trainer made using GTLibc :)" << std::endl;
    return 0;
}
