/*
Brief: This shows how to use the GTLibc to generate a generic trainer using Cheat Table.
This is automated trainer generation using Cheat Table.
Author: HeavenHM
Date: 24/04/2023
*/

// Define this to use CEParser. - Cheat Engine Parser.
#define GT_USE_CE_PARSER

// Include the GTLibc header file.
#include "GTLibc.hpp"

using namespace GTLIBC;
GTLibc gtlibc;

// Generates a generic trainer using Cheat Table.
void GenerateGenericTrainer(std::string &cheatTableFile, bool printCheat = false, bool readCheat = false, bool activateCheat = false, std::vector<int> cheatIds = {})
{
    // Read the cheat table file. [Required]
    CheatTable cheatTable = gtlibc.ReadCheatTable(cheatTableFile);

    // Check if the cheat table is empty.
    if (cheatTable.IsEmpty())
    {
        return;
    }

    // Print the cheat table. [Optional]
    if (printCheat)
    {
        gtlibc.DisplayCheatTable();
    }

    // Read the cheat table entries. [Optional]
    if (readCheat)
    {
        gtlibc.ReadCheatTableEntries();
    }

    // Activate the cheat entries. [Optional]
    if (activateCheat)
    {
        gtlibc.ActivateCheatTableEntries(cheatIds);
    }

    // Execute the cheat Table and generate the trainer. [Required]
    gtlibc.ExecuteCheatTable();
}

int main()
{
    // Instantiate the GTLibc and Enable the logs.
    gtlibc.EnableLogs(true);

    std::string gameName = "ac_client";
    // Finding the game process
    auto gameHandle = gtlibc.FindGameProcess(gameName);

    // Selecting the cheat table file.
    std::string cheatTableFile = "CheatTable/assaultcube.ct";

    // Generate the cheat table demo with read and execute.
    GenerateGenericTrainer(cheatTableFile);

    return 0;
}
