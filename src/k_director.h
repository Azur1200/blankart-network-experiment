// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by AJ "Tyron" Martinez.
// Copyright (C) 2024 by James Robert Roman.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_director.h
/// \brief SRB2kart automatic spectator camera.

#ifndef __K_DIRECTOR_H__
#define __K_DIRECTOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "doomtype.h"
#include "doomdef.h"

#define DIRECTORSWITCHTIME TICRATE * 5		// cooldown between unforced switches
#define DIRECTORBOREDOMTIME 3 * TICRATE / 2 // how long until players considered far apart?
#define DIRECTORTRANSFERTIME TICRATE		// how long to delay reaction shots?
#define DIRECTORBREAKAWAYDIST 2000			// how *far* until players considered far apart?
#define DIRECTORWALKBACKDIST 400			// how close should a trailing player be before we switch?
#define DIRECTORPINCHDIST 20000				// how close should the leader be to be considered "end of race"?

dboolean K_DirectorIsAvailable(void);
void K_InitDirector(void);
void K_UpdateDirector(void);
void K_DrawDirectorDebugger(void);
void K_DirectorFollowAttack(player_t *player, mobj_t *inflictor, mobj_t *source);
void K_ToggleDirector(void);
void K_DirectorSwitch(INT32 player, dboolean force);
void K_DirectorForceSwitch(INT32 player, INT32 time);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // __K_DIRECTOR_H__
