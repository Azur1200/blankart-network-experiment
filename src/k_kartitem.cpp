// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2025 by Kart Krew.
// Copyright (C) 2025 by "Anonimus".
// Copyright (C) 2025 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_kartitem.cpp
/// \brief Kart item structure and systems.

#include <vector>
#include <unordered_map> // kartitemtable

#include "command.h" // Need for player_t
#include "doomdef.h"
#include "d_player.h" // Need for player_t
#include "k_kartitem.hpp"

std::unordered_map<UINT32, kartitem_t*> kartitemtable;

extern "C"
{
kartitem_t *K_AddItemOdds(UINT32 id, kartitemflags_t flags)
{
    kartitem_t *newitem = new kartitem_t();

    newitem->_id = id;
    newitem->_flags = flags;

    newitem->alternate = false;
    newitem->forceme = 0;

    // Initialize race and battle odds.
    newitem->odds[GT_RACE][KARTITEM_NORMAL] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    newitem->odds[GT_RACE][KARTITEM_ALTERN] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    newitem->odds[GT_BATTLE][KARTITEM_NORMAL] = { 0, 0 };
    newitem->odds[GT_BATTLE][KARTITEM_ALTERN] = { 0, 0 };

    kartitemtable.insert({ id, newitem });

    return newitem;
}

#define AssertItemExists(kitem, id) { \
    if (auto search = kartitemtable.find(id); search != kartitemtable.end()) \
    { \
        const kartitem_t *tempitem = search->second; \
        I_Assert(tempitem != nullptr); \
        I_Assert(tempitem->_id == kitem->_id); \
        I_Assert(tempitem->_id == id); \
        CONS_Printf("Item no. %d most certainly exists.\n", id); \
    } \
}

void K_InitializeKartItems(void)
{
    kartitem_t *kitem;

    // FIXME: Handle all this by SOC at some point!

    kitem = K_AddItemOdds(KITEM_SNEAKER, static_cast<kartitemflags_t>(IF_NONE));
    
    if (kitem)
    {
        CONS_Printf("Allocation OK; setting up KITEM_SNEAKER...\n");
        AssertItemExists(kitem, KITEM_SNEAKER);

        // Sneaker
        kitem->odds[GT_RACE][KARTITEM_NORMAL] =
            { 0,  0,  0,  0,  8, 24, 60,  0,  0,  0,  0,  0,  0,  0,  0,  0};
            //0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
            //B   C   D   E   F   G   H   I   J   K   L   M   N   O   P   Q

        kitem->odds[GT_BATTLE][KARTITEM_NORMAL] = { 2, 1 };
                                                  //R  S

        //CONS_Printf("%d\n", kitem->odds[GT_RACE][KARTITEM_NORMAL][5]); // Should print 24
    }
    else
    {
        // Oh no.
        I_Error("Failed to allocate required item KITEM_SNEAKER!");
    }

    kitem = K_AddItemOdds(KITEM_ROCKETSNEAKER, static_cast<kartitemflags_t>(IF_NONE));

    if (kitem)
    {
        CONS_Printf("Allocation OK; setting up KITEM_ROCKETSNEAKER...\n");
        AssertItemExists(kitem, KITEM_ROCKETSNEAKER);

        // Rocket Sneaker
        kitem->odds[GT_RACE][KARTITEM_NORMAL] =
            { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 28, 35, 42, 48, 37, 37};
            //0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
            //B   C   D   E   F   G   H   I   J   K   L   M   N   O   P   Q

        //CONS_Printf("%d\n", kitem->odds[GT_RACE][KARTITEM_NORMAL][14]); // Should print 37
    }
    else
    {
        // Oh no.
        I_Error("Failed to allocate required item KITEM_ROCKETSNEAKER!");
    }

    // Paranoia ID test for items allocated with this function.
    CONS_Printf("ID test\n");

    UINT32 i = 0;
    for (auto& iterator : kartitemtable)
    {
        const kartitem_t * _item = iterator.second;

        if (_item)
        {
            CONS_Printf("Item no. %d exists.\n", _item->_id);
        }
        else
        {
            I_Error("An allocated item is NULL or invalid!");
        }

        i++;
    }
}
}