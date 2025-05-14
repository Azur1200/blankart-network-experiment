// SONIC ROBO BLAST 2
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
#include "doomstat.h"
#include "sounds.h"
#include "p_mobj.h"
#include "p_local.h" // DMG_ constants
#include "m_misc.h"
#include "z_zone.h"
#include "d_player.h"
#include "v_video.h" // V_*MAP constants
#include "lzf.h"
#include "m_menu.h"

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

mobjinfo_t mobjinfo[NUMMOBJTYPES] = {0};

skincolor_t skincolors[MAXSKINCOLORS] = {0};

/** Patches the mobjinfo, state, and skincolor tables.
  * Free slots are emptied out and set to initial values.
  */
void P_PatchInfoTables(void)
{
	INT32 i;
	char *tempname;

#if NUMSPRITEFREESLOTS > 9999 //tempname numbering actually starts at SPR_FIRSTFREESLOT, so the limit is actually 9999 + SPR_FIRSTFREESLOT-1, but the preprocessor doesn't understand enums, so its left at 9999 for safety
#error "Update P_PatchInfoTables, you big dumb head"
#endif

	// empty out free slots
	for (i = SPR_FIRSTFREESLOT; i <= SPR_LASTFREESLOT; i++)
	{
		tempname = sprnames[i];
		tempname[0] = (char)('0' + (char)((i-SPR_FIRSTFREESLOT+1)/1000));
		tempname[1] = (char)('0' + (char)(((i-SPR_FIRSTFREESLOT+1)/100)%10));
		tempname[2] = (char)('0' + (char)(((i-SPR_FIRSTFREESLOT+1)/10)%10));
		tempname[3] = (char)('0' + (char)((i-SPR_FIRSTFREESLOT+1)%10));
		tempname[4] = '\0';
	}
	sprnames[i][0] = '\0'; // i == NUMSPRITES
	memset(&states[S_FIRSTFREESLOT], 0, sizeof (state_t) * NUMSTATEFREESLOTS);
	memset(&mobjinfo[MT_FIRSTFREESLOT], 0, sizeof (mobjinfo_t) * NUMMOBJFREESLOTS);
	memset(&skincolors[SKINCOLOR_FIRSTFREESLOT], 0, sizeof (skincolor_t) * NUMCOLORFREESLOTS);
	memset(&menudefs[MN_FIRSTFREESLOT], 0, sizeof (menu_t) * NUMMENUFREESLOTS);
	for (i = SKINCOLOR_FIRSTFREESLOT; i <= SKINCOLOR_LASTFREESLOT; i++) {
		skincolors[i].accessible = false;
		skincolors[i].name[0] = '\0';
	}
	for (i = MT_FIRSTFREESLOT; i <= MT_LASTFREESLOT; i++)
		mobjinfo[i].doomednum = -1;
	for (i = 0; i < NUMMENUTYPES; i++)
		menudefs[i].drawroutine = M_DrawGenericMenu;
}

#ifdef ALLOW_RESETDATA
static char *sprnamesbackup;
static state_t *statesbackup;
static mobjinfo_t *mobjinfobackup;
static skincolor_t *skincolorsbackup;
static size_t sprnamesbackupsize, statesbackupsize, mobjinfobackupsize, skincolorsbackupsize;
#endif

void P_BackupTables(void)
{
#ifdef ALLOW_RESETDATA
	// Allocate buffers in size equal to that of the uncompressed data to begin with
	sprnamesbackup = Z_Malloc(sizeof(sprnames), PU_STATIC, NULL);
	statesbackup = Z_Malloc(sizeof(states), PU_STATIC, NULL);
	mobjinfobackup = Z_Malloc(sizeof(mobjinfo), PU_STATIC, NULL);
	skincolorsbackup = Z_Malloc(sizeof(skincolors), PU_STATIC, NULL);

	// Sprite names
	sprnamesbackupsize = lzf_compress(sprnames, sizeof(sprnames), sprnamesbackup, sizeof(sprnames));
	if (sprnamesbackupsize > 0)
		sprnamesbackup = Z_Realloc(sprnamesbackup, sprnamesbackupsize, PU_STATIC, NULL);
	else
		M_Memcpy(sprnamesbackup, sprnames, sizeof(sprnames));

	// States
	statesbackupsize = lzf_compress(states, sizeof(states), statesbackup, sizeof(states));
	if (statesbackupsize > 0)
		statesbackup = Z_Realloc(statesbackup, statesbackupsize, PU_STATIC, NULL);
	else
		M_Memcpy(statesbackup, states, sizeof(states));

	// Mobj info
	mobjinfobackupsize = lzf_compress(mobjinfo, sizeof(mobjinfo), mobjinfobackup, sizeof(mobjinfo));
	if (mobjinfobackupsize > 0)
		mobjinfobackup = Z_Realloc(mobjinfobackup, mobjinfobackupsize, PU_STATIC, NULL);
	else
		M_Memcpy(mobjinfobackup, mobjinfo, sizeof(mobjinfo));

	//Skincolor info
	skincolorsbackupsize = lzf_compress(skincolors, sizeof(skincolors), skincolorsbackup, sizeof(skincolors));
	if (skincolorsbackupsize > 0)
		skincolorsbackup = Z_Realloc(skincolorsbackup, skincolorsbackupsize, PU_STATIC, NULL);
	else
		M_Memcpy(skincolorsbackup, skincolors, sizeof(skincolors));
#endif
}

void P_ResetData(INT32 flags)
{
#ifndef ALLOW_RESETDATA
	(void)flags;
	CONS_Alert(CONS_NOTICE, M_GetText("P_ResetData(): not supported in this build.\n"));
#else
	if (flags & 1)
	{
		if (sprnamesbackupsize > 0)
			lzf_decompress(sprnamesbackup, sprnamesbackupsize, sprnames, sizeof(sprnames));
		else
			M_Memcpy(sprnames, sprnamesbackup, sizeof(sprnamesbackup));
	}

	if (flags & 2)
	{
		if (statesbackupsize > 0)
			lzf_decompress(statesbackup, statesbackupsize, states, sizeof(states));
		else
			M_Memcpy(states, statesbackup, sizeof(statesbackup));
	}

	if (flags & 4)
	{
		if (mobjinfobackupsize > 0)
			lzf_decompress(mobjinfobackup, mobjinfobackupsize, mobjinfo, sizeof(mobjinfo));
		else
			M_Memcpy(mobjinfo, mobjinfobackup, sizeof(mobjinfobackup));
	}

	if (flags & 8)
	{
		if (skincolorsbackupsize > 0)
			lzf_decompress(skincolorsbackup, skincolorsbackupsize, skincolors, sizeof(skincolors));
		else
			M_Memcpy(skincolors, skincolorsbackup, sizeof(skincolorsbackup));
	}
#endif
}
