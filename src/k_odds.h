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

// Distance variables
#define DISTVAR CV_Get(&cv_kartoddsdist)
#define DISTVAR_LEGACY CV_Get(&cv_kartlegacyoddsdist)
#define SPBDISTVAR CV_Get(&cv_kartspbdist)
#define SPBDISTVAR_LEGACY CV_Get(&cv_kartlegacyspbdist)

extern consvar_t *KartItemCVars[NUMKARTRESULTS-1];

// Goner; tracks how long an item should be "gone" for.
// AKA a time-based cooldown so Hyudoro and whatever else piss off to prevent
// spamming.

typedef enum goner_sect_e {
	GONER_BASECOOLDOWN = 0,
	GONER_CURRCOOLDOWN = 1,
} goner_sect_t;

extern tic_t ItemBGone[NUMKARTRESULTS][2];

void K_SetBGone(SINT8 item, tic_t time);
void K_SetBGoneToBase(SINT8 item);
tic_t K_GetBGone(SINT8 item, boolean base);
void K_KartHandleShieldCooldown(SINT8 item);

UINT32 K_CalculateInitalPDIS(const player_t *player, UINT8 pingame);
UINT32 K_CalculatePDIS(const player_t *player, UINT8 numPlayers, boolean *spbrush);
UINT8 K_FindUseodds(const player_t *player, fixed_t mashed, UINT32 pdis, UINT8 bestbumper, boolean spbrush);
UINT32 K_ScaleItemDistance(UINT32 distance, UINT8 numPlayers, boolean spbrush);
INT32 K_KartGetItemOdds(UINT8 pos, SINT8 item, UINT32 ourDist, UINT32 clusterDist, fixed_t mashed, boolean spbrush, boolean bot, boolean rival, boolean inBottom);
INT32 K_GetRollingRouletteItem(player_t *player);
SINT8 K_ItemResultToType(SINT8 getitem);
UINT8 K_ItemResultToAmount(SINT8 getitem);
void K_KartItemRoulette(player_t *player, ticcmd_t *cmd);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __K_ODDS__
