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
/// \file  k_kartitem.hpp
/// \brief Kart item structure and systems.


#ifndef __K_KARTITEM__
#define __K_KARTITEM__

#include <vector>
#include <unordered_map> // kartitemtable

#include "command.h" // Need for player_t
#include "doomstat.h"
#include "doomdef.h"
#include "d_player.h" // Need for player_t


typedef enum kartitemflags_e
{
    IF_NONE          = 0,
    IF_POWERITEM     = (1 << 0), // This item is a "power item". This activates "frantic item" toggle related functionality.
    IF_STARTCOOLDOWN = (1 << 1), // This item should not appear at the beginning of a race. (Usually really powerful crowd-breaking items)
    IF_INDIRECTITEM  = (1 << 2), // This item affects others indirectly. A 20 second cooldown is applied to all other items with this flag when an indirect item is rolled.
    IF_NOTNEAREND    = (1 << 3), // This item should not appear at the end of a race. (Usually trap items that lose their effectiveness)
    IF_NOTFORBOTTOM  = (1 << 4), // After the first 30 seconds, this item stops appearing for losing players. (Usually items that feel less effective at these positions)
    IF_RUNNERAUGMENT = (1 << 5)  // This item's odds are affected by the severity of 1st place's frontrun. (Very likely only for SPBs, but who knows?)
} kartitemflags_t;

typedef enum kartitemfuncs_e
{
    IFUNC_UNIQUEODDS, // This item isn't bound to the odds table, and has unique odds based on certain conditions.
    IFUNC_ALTERNODDS, // Same as IFUNC_UNIQUEODDS, but strictly for the alternative version of an item.
    IFUNC_SETFORCEME, // This function, should it exist, controls the "forceme" variable for the given item.
    IFUNC_CALLONROLL, // When this item is rolled in a roulette, this function is called.
    IFUNC_USEITEM,    // Function to call on an item's use.
    NUMKARTITEMFUNCS
} kartitemfuncs_t;

struct kartitem_t
{
    UINT32 _id; // The item's ID in game memory.
    
    kartitemflags_t _flags; // Various flags that control odds behavior.

    boolean alternate; // Is this an alternative version of the same item?

    UINT8 forceme; // This item is being forced into a player's item slot for one reason or another. Higher value = higher priority.

    void *functions[NUMKARTITEMFUNCS]; // Item functions.

    // Per-gametype item odds, for both standard and alternative modes.
    std::vector<UINT8> odds[NUMGAMETYPES][2]; 
};

// Dynamic table containing all items.
extern std::unordered_map<UINT32, kartitem_t*> kartitemtable;

extern "C"
{
    kartitem_t *K_AddItemOdds(UINT32 id, kartitemflags_t flags);
    void K_InitializeKartItems(void);
}

#endif // __K_KARTITEM__