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
/// \file  k_odds.hpp
/// \brief Kart item odds systems.

#ifndef __K_ODDS__
#define __K_ODDS__

#include "command.h" // Need for player_t
#include "doomdef.h"
#include "doomtype.h"
#include "d_player.h" // Need for player_t
#include "d_ticcmd.h"
#include "m_fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

// Max odds count
#define MAXODDS 16

extern consvar_t *KartItemCVars[NUMKARTRESULTS-1];

UINT32 K_CalculateInitalPDIS(const player_t *player);
UINT8 K_FindUseodds(const player_t *player, fixed_t mashed, UINT32 pdis, UINT8 bestbumper, boolean spbrush);
UINT32 K_ScaleItemDistance(UINT32 distance, UINT8 numPlayers, boolean spbrush);
INT32 K_KartGetItemOdds(UINT8 pos, SINT8 item, UINT32 ourDist, UINT32 clusterDist, fixed_t mashed, boolean spbrush, boolean bot, boolean rival);
INT32 K_GetRollingRouletteItem(player_t *player);
SINT8 K_ItemResultToType(SINT8 getitem);
UINT8 K_ItemResultToAmount(SINT8 getitem);
void K_KartItemRoulette(player_t *player, ticcmd_t *cmd);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __K_ODDS__
