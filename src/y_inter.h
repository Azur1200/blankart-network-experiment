// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2004-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  y_inter.h
/// \brief Tally screens, or "Intermissions" as they were formally called in Doom

#ifndef __Y_INTER_H__
#define __Y_INTER_H__

#ifdef __cplusplus
extern "C" {
#endif

extern boolean usebuffer;

boolean Y_ItemListActive(void);
void Y_IntermissionDrawer(void);
void Y_Ticker(void);

void Y_StartIntermission(void);
void Y_EndIntermission(void);

void Y_ConsiderScreenBuffer(void);
void Y_CleanupScreenBuffer(void);

void Y_DetermineIntermissionType(void);

typedef enum
{
	INTERSCREEN_SCORETALLY = 0, // The fancy score tally; interpoints affect this screen
	INTERSCREEN_FINALRESULTS, // Finalized results, appears before the score tally
	INTERSCREEN_ITEMROLLS, // The item rolls list
	INTERSCREEN_RANKCHART, // The big graph representing players' ranks across the race
	INTERSCREEN_RANKTALLY, // The total tally for all player ranks
} interscreen_t;

typedef enum
{
	int_none,
	int_race,		// Race
	int_battle,		// Battle (score-based)
	int_battletime,	// Battle (time-based)
} intertype_t;

extern intertype_t intertype;

void Y_FollowIntermission(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __Y_INTER_H__
