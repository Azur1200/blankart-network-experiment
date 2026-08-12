// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Vivian "toastergrl" Grannell.
// Copyright (C) 2025 by Kart Krew.
// Copyright (C) 2026 by "yama".
// Copyright (C) 2026 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_dscredits.hpp
/// \brief (Mostly) secret MKDS credits :)

#ifndef __F_DSCREDITS__
#define __F_DSCREDITS__

#include "doomdef.h"
#include "m_fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAXDSOBJECTS (MAXPLAYERS + (48 - MAXPLAYERS))
#define DSOBJOFFSCREEN 48

#define FRAMETOSEC (FRACUNIT / TICRATE)

#define CREDITSDELAY (TICRATE / 2)

// "Special" players, such as us, our rival, and the various unique characters in Act 2.
#define DSUNIQUEPLAYERS 3

extern longtic_t creditstimings[];

typedef struct dscredits_s {
	longtic_t ticker;
	tic_t delaytoplayer;
    tic_t delaytobg;
    uint16_t creditsscene; // "Scene" number of credits, iterated by 1 for each timing.
	struct dscreditsobj_s
	{
        dboolean active;
        dboolean player; // Are we a player?
		uint16_t skin;
        uint16_t color;
        patch_t *patch; // Patch.
		int32_t position[2]; // Position
        fixed_t scale; // Scale
        int32_t speed[2]; // Speed

        int64_t movestop; // Stops our movement after
        int64_t movestart; // Restarts our movement
        int32_t movestartspd[2]; // Speed to apply on movestart

        uint8_t glance; // They look at one another as they drive
		dboolean spinout; // Spinning out?
	} creditsobj[MAXDSOBJECTS + DSUNIQUEPLAYERS];
    
} dscredits_t;

extern dscredits_t dscredits;

extern tic_t dscreditsticks;

void F_SecretCreditsTicker(void);
void F_SecretCreditsDrawer(void);

#ifdef __cplusplus
}
#endif

#endif // __F_DSCREDITS__
