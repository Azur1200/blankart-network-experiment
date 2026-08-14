// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2020 by Kart Krew.
// Copyright (C) 2026 by Team BlanKart.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_vote.h
/// \brief MK online style voting screen

#ifndef __K_VOTE_H__
#define __K_VOTE_H__

#include "doomtype.h"

#ifdef __cplusplus
extern "C" {
#endif

void Y_VoteDrawer(void);
void Y_VoteTicker(void);
void Y_StartVote(void);
void Y_EndVote(void);
void Y_SetupVoteFinish(int8_t pick, int8_t level);

// Votescreen stuff
typedef struct
{
	dboolean race;       // non lua race patch replaced
	dboolean widerace;   // non lua widescreen race patch replaced
	dboolean battle;     // non lua battle patch replaced
	dboolean widebattle; // non lua widescreen battle patch replaced
} votereplace_t;

// VEXTRN - Vote (V) Extra (EXT) Race (R) Normal (N - Normal sized patch)
// VEXTRW - Vote (V) Extra (EXT) Race (R) Normal (W - Wide patch)
// VEXTBN - Vote (V) Extra (EXT) Battle (B) Normal (N - Normal sized patch)
// VEXTBW - Vote (V) Extra (EXT) Battle (B) Normal (W - Wide patch)
typedef struct
{
	char Prefix[5];                 // Race = INTSX, Battle = BTLSX
	char luaPrefix[5];              // prefix for lua votescreens

	int32_t currentAnimFrame;         // current animated background frame
	int32_t timePerAnimFrame;         // time for 1 frame in ticks, 2 by default

	int32_t foundLuaVoteFrames;       // normal lua patch frames
	int32_t foundLuaVoteWideFrames;   // widescreen lua patch frames

	votereplace_t replaced;         // checks which non lua patch has been replaced

	patch_t *bgpatch;               // votebackground patch
	patch_t *widebgpatch;           // wide votebackground patch

	patch_t *cursor[5];             // cursor patches
	patch_t *rubyicon;              // encore ruby patch
} votescreen_t;
extern votescreen_t VoteScreen;

dboolean Y_PlayerIDCanVote(const uint8_t id);

typedef enum
{
	VOTE_NOT_PICKED = -1,
} votetype_e;

void Y_VoteScreenCheck(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __K_VOTE_H__
