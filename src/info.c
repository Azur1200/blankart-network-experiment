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
/// \file  info.c
/// \brief Thing frame/state LUT

// Data.
#include "info.h"
#include "doomdef.h"
#include "z_zone.h"
#include "w_wad.h"
#include "dehacked.h"
#include "d_main.h"
#include "m_menu.h"
#include "deh_tables.h"
#include "k_items.h"

// Hey, moron! If you wanna change this table, you can just change the sprite enum in info/sprites.h,
// so you don't have to copy and paste the list of sprite names back in here :^)
char sprnames[NUMSPRITES + 1][5] =
{
#define _(name, ...) #name,
#include "info/sprites.h"
#undef _
};

char spr2names[NUMPLAYERSPRITES][5] =
{
#define _(name, ...) #name,
#include "info/sprite2.h"
#undef _
};
playersprite_t free_spr2 = SPR2_FIRSTFREESLOT;

playersprite_t spr2defaults[NUMPLAYERSPRITES] = {
	0, // SPR2_STIN
	SPR2_STIN, // SPR2_STIL
	SPR2_STIN, // SPR2_STIR
	SPR2_STIN, // SPR2_STGL
	SPR2_STIN, // SPR2_STGR
	SPR2_STGL, // SPR2_STLL
	SPR2_STGR, // SPR2_STLR

	0, // SPR2_SLWN
	SPR2_SLWN, // SPR2_SLWL
	SPR2_SLWN, // SPR2_SLWR
	SPR2_SLWN, // SPR2_SLGL
	SPR2_SLWN, // SPR2_SLGR
	SPR2_SLGL, // SPR2_SLLL
	SPR2_SLGR, // SPR2_SLLR

	0, // SPR2_FSTN
	SPR2_FSTN, // SPR2_FSTL
	SPR2_FSTN, // SPR2_FSTR
	SPR2_FSTN, // SPR2_FSGL
	SPR2_FSTN, // SPR2_FSGR
	SPR2_FSGL, // SPR2_FSLL
	SPR2_FSGR, // SPR2_FSLR

	0, // SPR2_DRLN
	SPR2_DRLN, // SPR2_DRLO
	SPR2_DRLN, // SPR2_DRLI

	0, // SPR2_DRRN
	SPR2_DRRN, // SPR2_DRRO
	SPR2_DRRN, // SPR2_DRRI

	0, // SPR2_SPIN
	0, // SPR2_DEAD

	0, // SPR2_SIGN
	0, // SPR2_XTRA
	0, // SPR2_KART
};

// Doesn't work with g++, needs actionf_p1 (don't modify this comment)
state_t states[NUMSTATES] = {0};
uint16_t numstates;

mobjinfo_t mobjinfo[NUMMOBJTYPES] = {0};
uint16_t nummobjtypes;

skincolor_t skincolors[MAXSKINCOLORS] = {0};

#if NUMSPRITEFREESLOTS > 9999 //tempname numbering actually starts at SPR_FIRSTFREESLOT, so the limit is actually 9999 + SPR_FIRSTFREESLOT-1, but the preprocessor doesn't understand enums, so its left at 9999 for safety
#error "Update P_PatchInfoTables, you big dumb head"
#endif

static const char *hardcodemobjs =
#define _(name, ...) #name "\0"
#include "info/mobjs.h"
#undef _
;

static const char *hardcodestates =
#define _(name, ...) #name "\0"
#include "info/states.h"
#undef _
;

static const char *hardcodeskincolors =
#define _(name, ...) #name "\0"
#include "info/skincolors.h"
#undef _
;

static const char *hardcodemenus =
#define _(name, ...) #name "\0"
#include "info/menus.h"
#undef _
;

static const char *hardcodeitems =
#define _(name, ...) #name "\0"
#include "info/kartitems.h"
#undef _
;

void P_ResetData(int32_t flags)
{
	int32_t i;
	uint16_t lumpnum;
	dboolean init = numwadfiles == 0;
	const char *name;

	// sprnames
	if (flags & 1)
	{
		memset(used_spr,0,sizeof(uint8_t) * ((NUMSPRITEFREESLOTS / 8) + 1));

		// vanilla sprnames are immutable
		for (i = SPR_FIRSTFREESLOT; i <= SPR_LASTFREESLOT; i++)
		{
			char *tempname = sprnames[i];
			tempname[0] = (char)('0' + (char)((i-SPR_FIRSTFREESLOT+1)/1000));
			tempname[1] = (char)('0' + (char)(((i-SPR_FIRSTFREESLOT+1)/100)%10));
			tempname[2] = (char)('0' + (char)(((i-SPR_FIRSTFREESLOT+1)/10)%10));
			tempname[3] = (char)('0' + (char)((i-SPR_FIRSTFREESLOT+1)%10));
			tempname[4] = '\0';
		}
		sprnames[i][0] = '\0'; // i == NUMSPRITES
	}

	// states
	if (flags & 2)
	{
		if (!init)
			memset(states, 0, sizeof(states));

		numstates = S_FIRSTFREESLOT;

		if (statenames)
			Z_Free(statenames);
		statenames = strbuf_alloc();
		name = hardcodestates;
		for (i = 0; i < S_FIRSTFREESLOT; i++, name += strlen(name)+1)
			DEH_Link(name, &states[i].info, &statenames);

		if (!init)
		{
			lumpnum = W_CheckNumForLongNamePwad("states", MAINWAD_MAIN, 0);
			DEH_LoadDehackedLumpPwad(MAINWAD_MAIN, lumpnum, true);
		}
	}

	// mobjinfo
	if (flags & 4)
	{
		if (!init)
			memset(mobjinfo, 0, sizeof(mobjinfo));

		nummobjtypes = MT_FIRSTFREESLOT;

		if (mobjnames)
			Z_Free(mobjnames);
		mobjnames = strbuf_alloc();
		name = hardcodemobjs;
		for (i = 0; i < MT_FIRSTFREESLOT; i++, name += strlen(name)+1)
			DEH_Link(name, &mobjinfo[i].info, &mobjnames);

		for (i = MT_FIRSTFREESLOT; i <= MT_LASTFREESLOT; i++)
			mobjinfo[i].doomednum = -1;

		if (!init)
		{
			lumpnum = W_CheckNumForLongNamePwad("mobjs", MAINWAD_MAIN, 0);
			DEH_LoadDehackedLumpPwad(MAINWAD_MAIN, lumpnum, true);
		}
	}

	// skincolors
	if (flags & 8)
	{
		if (!init)
			memset(skincolors, 0, sizeof(skincolors));

		numskincolors = SKINCOLOR_FIRSTFREESLOT;

		if (skincolornames)
			Z_Free(skincolornames);
		skincolornames = strbuf_alloc();
		name = hardcodeskincolors;
		for (i = 0; i < SKINCOLOR_FIRSTFREESLOT; i++, name += strlen(name)+1)
			DEH_Link(name, &skincolors[i].info, &skincolornames);

		for (i = SKINCOLOR_FIRSTFREESLOT; i <= SKINCOLOR_LASTFREESLOT; i++)
		{
			skincolors[i].accessible = false;
			skincolors[i].name[0] = '\0';
		}

		if (!init)
		{
			lumpnum = W_CheckNumForLongNamePwad("skincolors", MAINWAD_MAIN, 0);
			DEH_LoadDehackedLumpPwad(MAINWAD_MAIN, lumpnum, true);
		}
	}

	// menudefs (TODO: actually reset this with resetdata?)
	if (init)
	{
		memset(menudefs, 0, sizeof(menudefs));
		M_InitMenuPresTables();

		nummenutypes = MN_FIRSTFREESLOT;

		if (menunames)
			Z_Free(menunames);
		menunames = strbuf_alloc();
		name = hardcodemenus;
		for (i = 0; i < MN_FIRSTFREESLOT; i++, name += strlen(name)+1)
			DEH_Link(name, &menudefs[i].info, &menunames);
	}

	// kartitems
	if (init)
	{
		memset(kartitems, 0, sizeof(kartitems));

		numkartitems = KITEM_FIRSTFREESLOT;

		if (kartitemnames)
			Z_Free(kartitemnames);
		kartitemnames = strbuf_alloc();
		name = hardcodeitems;
		for (i = 0; i < KITEM_FIRSTFREESLOT; i++, name += strlen(name)+1)
		{
			DEH_Link(name, &kartitems[i].info, &kartitemnames);
			K_RegisterItem(i);
		}
	}
}
