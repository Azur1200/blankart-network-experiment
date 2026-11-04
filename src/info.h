// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  info.h
/// \brief Thing frame/state LUT

#ifndef __INFO__
#define __INFO__

// Needed for action function pointer handling.
#include "d_think.h"
#include "sounds.h"
#include "m_fixed.h"
#include "dehacked.h"

#ifdef __cplusplus
extern "C" {
#endif

// deh_tables.c now has lists for the more named enums! PLEASE keep them up to date!
// For great modding!!
// G: good news! they are now macro'd. how long did you guys put up with this shit? christ...

enum actionnum {
#define _(name, upper, ...) A_##upper,
#include "info/actions.h"
#undef _
	NUMACTIONS
};

// function prototypes for actions
#define _(name, upper, ...) void name(void *thing);
#include "info/actions.h"
#undef _

extern dboolean actionsoverridden[NUMACTIONS];

// ratio of states to sprites to mobj types is roughly 6 : 1 : 1
#define NUMMOBJFREESLOTS 4096
#define NUMSPRITEFREESLOTS NUMMOBJFREESLOTS
#define NUMSTATEFREESLOTS (NUMMOBJFREESLOTS*8)

// Hey, moron! Don't change this table, change the one in info/sprites.h :^)
typedef enum sprite
{
#define _(name, ...) SPR_##name,
#include "info/sprites.h"
#undef _
	SPR_FIRSTFREESLOT,
	SPR_LASTFREESLOT = SPR_FIRSTFREESLOT + NUMSPRITEFREESLOTS - 1,
	NUMSPRITES
} spritenum_t;

typedef enum playersprite
{
#define _(name, ...) SPR2_##name,
#include "info/sprite2.h"
#undef _
	SPR2_FIRSTFREESLOT,
	SPR2_LASTFREESLOT = 0x7f,
	NUMPLAYERSPRITES
} playersprite_t;

typedef enum state
{
#define _(name, ...) S_##name,
#include "info/states.h"
#undef _
	S_FIRSTFREESLOT,
	S_LASTFREESLOT = S_FIRSTFREESLOT + NUMSTATEFREESLOTS - 1,
	NUMSTATES
} statenum_t;

struct state_t
{
	dehinfo_t info;
	spritenum_t sprite;
	uint32_t frame; // we use the upper 16 bits for translucency and other shade effects
	int32_t tics;
	actionf_p1 action;
	int32_t var1;
	int32_t var2;
	statenum_t nextstate;
};

extern state_t states[NUMSTATES];
extern uint16_t numstates;
extern char sprnames[NUMSPRITES + 1][5];
extern char spr2names[NUMPLAYERSPRITES][5];
extern playersprite_t spr2defaults[NUMPLAYERSPRITES];
extern state_t *astate;
extern playersprite_t free_spr2;

typedef enum mobj_type
{
#define _(name, ...) MT_##name,
#include "info/mobjs.h"
#undef _
	MT_FIRSTFREESLOT,
	MT_LASTFREESLOT = MT_FIRSTFREESLOT + NUMMOBJFREESLOTS - 1,
	NUMMOBJTYPES
} mobjtype_t;

struct mobjinfo_t
{
	dehinfo_t info;
	int32_t doomednum;
	statenum_t spawnstate;
	int32_t spawnhealth;
	statenum_t seestate;
	sfxenum_t seesound;
	int32_t reactiontime;
	sfxenum_t attacksound;
	statenum_t painstate;
	int32_t painchance;
	sfxenum_t painsound;
	statenum_t meleestate;
	statenum_t missilestate;
	statenum_t deathstate;
	statenum_t xdeathstate;
	sfxenum_t deathsound;
	fixed_t speed;
	fixed_t radius;
	fixed_t height;
	int32_t dispoffset;
	int32_t mass;
	int32_t damage;
	sfxenum_t activesound;
	uint32_t flags;
	uint32_t flags2;
	uint16_t eflags;
	uint32_t renderflags;
	uint32_t renderflags2;
	statenum_t raisestate;
	dboolean compatmode;
};

extern mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern uint16_t nummobjtypes;

typedef enum
{
#define _(name, ...) SKINCOLOR_##name,
#include "info/skincolors.h"
#undef _
	SKINCOLOR_FIRSTFREESLOT,
	SKINCOLOR_LASTFREESLOT = SKINCOLOR_FIRSTFREESLOT + NUMCOLORFREESLOTS - 1,
	MAXSKINCOLORS,

	FIRSTRAINBOWCOLOR = SKINCOLOR_PINK,
	FIRSTSUPERCOLOR = SKINCOLOR_SUPER1,
	NUMSUPERCOLORS = ((SKINCOLOR_FIRSTFREESLOT - FIRSTSUPERCOLOR)/5)
} skincolornum_t;

struct skincolor_t
{
	dehinfo_t info;             // Internal name hash and offset
	char name[MAXCOLORNAME+1];  // Skincolor name
	uint8_t ramp[COLORRAMPSIZE];  // Colormap ramp
	uint16_t invcolor;            // Signpost color
	uint8_t invshade;             // Signpost color shade
	uint16_t chatcolor;           // Chat color
	dboolean accessible;         // Accessible by the color command + setup menu
};

extern skincolor_t skincolors[MAXSKINCOLORS];
extern uint16_t numskincolors;

void P_ResetData(int32_t flags);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
