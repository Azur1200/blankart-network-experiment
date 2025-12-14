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

void Y_VoteDrawer(void);
void Y_VoteTicker(void);
void Y_StartVote(void);
void Y_EndVote(void);
void Y_SetupVoteFinish(SINT8 pick, SINT8 level);

typedef enum
{
	int_none,
	int_race,		// Race
	int_battle,		// Battle (score-based)
	int_battletime,	// Battle (time-based)
} intertype_t;

extern intertype_t intertype;
extern intertype_t intermissiontypes[NUMGAMETYPES];

// Votescreen stuff
typedef struct
{
	boolean race;       // non lua race patch replaced
	boolean widerace;   // non lua widescreen race patch replaced
	boolean battle;     // non lua battle patch replaced
	boolean widebattle; // non lua widescreen battle patch replaced
} votereplace_t;

// VEXTRN - Vote (V) Extra (EXT) Race (R) Normal (N - Normal sized patch)
// VEXTRW - Vote (V) Extra (EXT) Race (R) Normal (W - Wide patch)
// VEXTBN - Vote (V) Extra (EXT) Battle (B) Normal (N - Normal sized patch)
// VEXTBW - Vote (V) Extra (EXT) Battle (B) Normal (W - Wide patch)
typedef struct
{
	char Prefix[5];                 // Race = INTSX, Battle = BTLSX
	char luaPrefix[5];              // prefix for lua votescreens

	INT32 currentAnimFrame;         // current animated background frame
	INT32 timePerAnimFrame;         // time for 1 frame in ticks, 2 by default

	INT32 foundLuaVoteFrames;       // normal lua patch frames
	INT32 foundLuaVoteWideFrames;   // widescreen lua patch frames

	votereplace_t replaced;         // checks which non lua patch has been replaced

	patch_t *bgpatch;               // votebackground patch
	patch_t *widebgpatch;           // wide votebackground patch

	patch_t *cursor[5];             // cursor patches
	patch_t *rubyicon;              // encore ruby patch
} votescreen_t;
extern votescreen_t VoteScreen;

boolean Y_PlayerIDCanVote(const UINT8 id);

typedef enum
{
	VOTE_NOT_PICKED = -1,
} votetype_e;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __Y_INTER_H__
