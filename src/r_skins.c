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
/// \file  r_skins.c
/// \brief Loading skins

#include "doomdef.h"
#include "console.h"
#include "g_game.h"
#include "r_local.h"
#include "st_stuff.h"
#include "w_wad.h"
#include "z_zone.h"
#include "m_misc.h"
#include "info.h" // spr2names
#include "i_video.h" // rendermode
#include "i_system.h"
#include "r_things.h"
#include "r_skins.h"
#include "p_local.h"
#include "dehacked.h" // get_number (for thok)
#include "m_cond.h"
#include "d_main.h" // MAINWAD_CHARS
#if 0
#include "k_kart.h" // K_KartResetPlayerColor
#endif
#include "qs22j.h"
#ifdef HWRENDER
#include "hardware/hw_md2.h"
#endif

#include "discord.h"

INT32 numskins = 0;
skin_t skins[MAXSKINS];
INT32 skinsorted[MAXSKINS];

// FIXTHIS: don't work because it must be inistilised before the config load
//#define SKINVALUES
#ifdef SKINVALUES
CV_PossibleValue_t skin_cons_t[MAXSKINS+1];
#endif

CV_PossibleValue_t Forceskin_cons_t[MAXSKINS+2];

//
// P_GetSkinSprite2
// For non-super players, tries each sprite2's immediate predecessor until it finds one with a number of frames or ends up at standing.
// For super players, does the same as above - but tries the super equivalent for each sprite2 before the non-super version.
//

UINT8 P_GetSkinSprite2(skin_t *skin, UINT8 spr2, player_t *player)
{
	UINT8 super = 0, i = 0;

	(void)player;

	if (!skin)
		return 0;

	if ((playersprite_t)(spr2 & ~FF_SPR2SUPER) >= free_spr2)
		return 0;

	while (!skin->sprites[spr2].numframes
		&& spr2 != SPR2_STIL
		&& ++i < 32) // recursion limiter
	{
		if (spr2 & FF_SPR2SUPER)
		{
			super = FF_SPR2SUPER;
			spr2 &= ~FF_SPR2SUPER;
			continue;
		}

		switch(spr2)
		{
			// Normal special cases.
			// (none in kart)

			// Use the handy list, that's what it's there for!
			default:
				spr2 = spr2defaults[spr2];
				break;
		}

		spr2 |= super;
	}

	if (i >= 32) // probably an infinite loop...
		return 0;

	return spr2;
}

// converts the kart player frame in inframe to the equivalent sprite2 + frame
UINT8 P_KartFrameToSprite2(skin_t *skin, UINT8 inframe, UINT8 *outframe)
{
	UINT8 spr2, frame = 0;
#define F(x) case x - 'A':
	switch (inframe & FF_FRAMEMASK)
	{
	F('A') F('B')
		spr2 = SPR2_STIN;
		frame = inframe & 1;
		break;
	F('C') F('D')
		spr2 = SPR2_STIL;
		frame = inframe & 1;
		break;
	F('E') F('F')
		spr2 = SPR2_STIR;
		frame = inframe & 1;
		break;
	F('G')
		spr2 = SPR2_SLWN;
		frame = 1;
		break;
	F('H')
		spr2 = SPR2_SLWL;
		frame = 1;
		break;
	F('I')
		spr2 = SPR2_SLWR;
		frame = 1;
		break;
	F('J')
		spr2 = SPR2_FSTN;
		frame = 1;
		break;
	F('K')
		spr2 = SPR2_FSTL;
		frame = 1;
		break;
	F('L')
		spr2 = SPR2_FSTR;
		frame = 1;
		break;
	F('M') F('N')
		spr2 = SPR2_DRLN;
		frame = inframe & 1;
		break;
	F('O') F('P')
		spr2 = SPR2_DRRN;
		frame = inframe & 1;
		break;
	F('Q')
		spr2 = SPR2_SPIN;
		break;
	F('R')
		spr2 = SPR2_KART;
		break;
	F('S')
		spr2 = SPR2_SIGN;
		break;
	default:
		spr2 = SPR2_KART;
		frame = (inframe & FF_FRAMEMASK) - 18; // not 19! frame 0 is squish
		break;
	}
#undef F
	if (outframe)
		*outframe = frame;
	return P_GetSkinSprite2(skin, spr2, NULL);
}

static void Sk_SetDefaultValue(skin_t *skin)
{
	INT32 i;
	//
	// set default skin values
	//
	memset(skin, 0, sizeof (skin_t));
	snprintf(skin->name,
		sizeof skin->name, "skin %u", (UINT32)(skin-skins));
	skin->name[sizeof skin->name - 1] = '\0';
	skin->wadnum = INT16_MAX;

	skin->flags = 0;

	strcpy(skin->realname, "Someone");
	strncpy(skin->facerank, "MISSING", 9);
	strncpy(skin->facewant, "MISSING", 9);
	strncpy(skin->facemmap, "MISSING", 9);
	skin->starttranscolor = 96;
	skin->prefcolor = SKINCOLOR_GREEN;
	skin->supercolor = SKINCOLOR_SUPER1;
	skin->prefoppositecolor = 0; // use tables

	skin->kartspeed = 5;
	skin->kartweight = 5;

	skin->followitem = 0;

	skin->highresscale = FRACUNIT;

	for (i = 0; i < sfx_skinsoundslot0; i++)
		if (S_sfx[i].skinsound != -1)
			skin->soundsid[S_sfx[i].skinsound] = i;
}

static void R_IHateThatHedgehog(UINT16 wadnum);

//
// Initialize the basic skins
//
void R_InitSkins(void)
{
	size_t i;

	// it can be is do before loading config for skin cvar possible value
	// (... what the fuck did you just say to me? "it can be is do"?)
#ifdef SKINVALUES
	for (i = 0; i <= MAXSKINS; i++)
	{
		skin_cons_t[i].value = 0;
		skin_cons_t[i].strvalue = NULL;
	}
#endif

	// doesn't seem right but it's what the original did
	memset(skinsorted, 0, sizeof(skinsorted));
	// yes default skin!
	numskins = 1;

	for (i = 0; i < numwadfiles; i++)
	{
		if (i == MAINWAD_CHARS)
			R_IHateThatHedgehog((UINT16)i);
		R_AddSkins((UINT16)i);
		if (!wadfiles[i]->compatmode)
			R_PatchSkins((UINT16)i);
		R_LoadSpriteInfoLumps(i, wadfiles[i]->numlumps);

		if (i < NUMMAINWADS)
		{
			g_discord_skins = numskins;
		}
	}
	ST_ReloadSkinFaceGraphics();
}

UINT32 R_GetSkinAvailabilities(void)
{
	UINT8 i;
	UINT32 response = 0;

	for (i = 0; i < MAXUNLOCKABLES; i++)
	{
		if (unlockables[i].type == SECRET_SKIN && unlockables[i].unlocked)
		{
			UINT16 s = min(unlockables[i].variable, MAXSKINS);
			response |= (1 << s);
		}
	}

	return response;
}

//
// From callmore's skin select
// Sort function(s) for sorting skin names
//
static int CompareSkinIds(const void *a, const void *b)
{
	const INT32 val_a = *((const INT32 *)a);
	const INT32 val_b = *((const INT32 *)b);
	if (val_a > val_b)
			return 1;
	else if (val_a < val_b)
		return -1;
	else
		return 0;
}

static int CompareSkinNames(const void *a, const void *b)
{
	const skin_t *in1 = &skins[*(const INT32 *)a];
	const skin_t *in2 = &skins[*(const INT32 *)b];
	return strcmp(in1->realname, in2->realname);
}

static int CompareSkinSpeeds(const void *a, const void *b)
{
	const skin_t *in1 = &skins[*(const INT32 *)a];
	const skin_t *in2 = &skins[*(const INT32 *)b];
	INT32 temp = 0;
	// check speed
	if (in1->kartspeed < in2->kartspeed)
		return -1;
	else if (in2->kartspeed < in1->kartspeed)
		return 1;
	// then check weight
	if (in1->kartweight < in2->kartweight)
		return -1;
	else if (in2->kartweight < in1->kartweight)
		return 1;
	// then check name
	if ((temp = strcmp(in1->realname, in2->realname)))
		return temp;
	// sort by internal name
	return strcmp(in1->name, in2->name);
}

static int CompareSkinWeights(const void *a, const void *b)
{
	const skin_t *in1 = &skins[*(const INT32 *)a];
	const skin_t *in2 = &skins[*(const INT32 *)b];
	INT32 temp = 0;
	// check weight
	if (in1->kartweight < in2->kartweight)
	return -1;
	else if (in2->kartweight < in1->kartweight)
	return 1;
	// then check speed
	if (in1->kartspeed < in2->kartspeed)
	return -1;
	else if (in2->kartspeed < in1->kartspeed)
	return 1;
	// then check name
	if ((temp = strcmp(in1->realname, in2->realname)))
	return temp;
	// sort by internal name
	return strcmp(in1->name, in2->name);
}

static int CompareSkinAccels(const void *a, const void *b)
{
	return CompareSkinSpeeds(b, a);
}

static int CompareSkinHandlings(const void *a, const void *b)
{
	return CompareSkinWeights(b, a);
}

static int CompareSkinColours(const void *a, const void *b)
{
	const skin_t *in1 = &skins[*(const INT32 *)a];
	const skin_t *in2 = &skins[*(const INT32 *)b];
	INT32 temp = 0;

	// check prefcolor
	if (in1->prefcolor < in2->prefcolor)
		return -1;
	else if (in2->prefcolor < in1->prefcolor)
		return 1;
	// then check name
	if ((temp = strcmp(in1->realname, in2->realname)))
		return temp;
	// sort by internal name
	return strcmp(in1->name, in2->name);
}

void SortSkins(void)
{
	int (*_sortingFunc)(const void *, const void *);
	switch (cv_skinselectsort.value)
	{
		case SKINMENUSORT_NAME:
			_sortingFunc = CompareSkinNames;
			break;
		case SKINMENUSORT_SPEED:
			_sortingFunc = CompareSkinSpeeds;
			break;
		case SKINMENUSORT_WEIGHT:
			_sortingFunc = CompareSkinWeights;
			break;
		case SKINMENUSORT_ACCEL:
			_sortingFunc = CompareSkinAccels;
			break;
		case SKINMENUSORT_HANDLING:
			_sortingFunc = CompareSkinHandlings;
			break;
		case SKINMENUSORT_PREFCOLOR:
			_sortingFunc = CompareSkinColours;
			break;
		default:
			_sortingFunc = CompareSkinIds;
			break;
	}
	qs22j(skinsorted, numskins, sizeof(INT32), _sortingFunc);
}

INT32 FindSortedSkinIndex(INT32 skinnum)
{
	for (INT32 i = 0; i < numskins; i++)
	{
		if (skinsorted[i] == skinnum) return i;
	}
	return 0;
}

// returns true if available in circumstances, otherwise nope
// warning don't use with an invalid skinnum other than -1 which always returns true
boolean R_SkinUsable(INT32 playernum, INT32 skinnum)
{
	boolean needsunlocked = false;
	UINT8 i;

	if (skinnum == -1)
	{
		// Simplifies things elsewhere, since there's already plenty of checks for less-than-0...
		return true;
	}

	if (modeattacking)
	{
		// If you have someone else's run, you should be able to take a look
		return true;
	}

	if (netgame && (cv_forceskin.value == skinnum))
	{
		// Being forced to play as this character by the server
		return true;
	}

	if (metalrecording)
	{
		// Recording a Metal Sonic race
		const INT32 metalskin = R_SkinAvailable("metalsonic");
		return (skinnum == metalskin);
	}

	// Determine if this character is supposed to be unlockable or not
	for (i = 0; i < MAXUNLOCKABLES; i++)
	{
		if (unlockables[i].type == SECRET_SKIN && unlockables[i].variable == skinnum)
		{
			// i is now the unlockable index, we can use this later
			needsunlocked = true;
			break;
		}
	}

	if (needsunlocked == true)
	{
		// You can use this character IF you have it unlocked.
		if ((netgame || multiplayer) && playernum != -1)
		{
			// Use the netgame synchronized unlocks.
			return (boolean)(!(players[playernum].availabilities & (1 << skinnum)));
		}
		else
		{
			// Use the unlockables table directly
			return (boolean)(unlockables[i].unlocked);
		}
	}
	else
	{
		// Didn't trip anything, so we can use this character.
		return true;
	}
}

// returns true if the skin name is found (loaded from pwad)
// warning return -1 if not found
INT32 R_SkinAvailable(const char *name)
{
	INT32 i;

	for (i = 0; i < numskins; i++)
	{
		// search in the skin list
		if (stricmp(skins[i].name,name)==0)
			return i;
	}
	return -1;
}

// network code calls this when a 'skin change' is received
void SetPlayerSkin(INT32 playernum, const char *skinname)
{
	INT32 i = R_SkinAvailable(skinname);
	player_t *player = &players[playernum];

	if ((i != -1) && R_SkinUsable(playernum, i))
	{
		SetPlayerSkinByNum(playernum, i);
		return;
	}

	if (P_IsLocalPlayer(player))
		CONS_Alert(CONS_WARNING, M_GetText("Skin '%s' not found.\n"), skinname);
	else if(server || IsPlayerAdmin(consoleplayer))
		CONS_Alert(CONS_WARNING, M_GetText("Player %d (%s) skin '%s' not found\n"), playernum, player_names[playernum], skinname);

	SetPlayerSkinByNum(playernum, 0);
}

// Same as SetPlayerSkin, but uses the skin #.
// network code calls this when a 'skin change' is received
void SetPlayerSkinByNum(INT32 playernum, INT32 skinnum)
{
	player_t *player = &players[playernum];
	skin_t *skin = &skins[skinnum];
	//UINT16 newcolor = 0;
	//UINT8 i;

	if (skinnum >= 0 && skinnum < numskins && R_SkinUsable(playernum, skinnum)) // Make sure it exists!
	{
		player->skin = skinnum;

		player->charflags = (UINT32)skin->flags;

		player->followitem = skin->followitem;

		player->kartspeed = skin->kartspeed;
		player->kartweight = skin->kartweight;

#if 0
		if (!(cht_debug || devparm) && !(netgame || multiplayer || demo.playback))
		{
			for (i = 0; i <= r_splitscreen; i++)
			{
				if (playernum == g_localplayers[i])
				{
					CV_StealthSetValue(&cv_playercolor[i], skin->prefcolor);
				}
			}

			player->skincolor = newcolor = skin->prefcolor;
			K_KartResetPlayerColor(player);
		}
#endif

		if (player->followmobj)
		{
			P_RemoveMobj(player->followmobj);
			P_SetTarget(&player->followmobj, NULL);
		}

		if (player->mo)
		{
			player->mo->skin = skin;
			P_SetScale(player->mo, player->mo->scale);
			P_SetPlayerMobjState(player->mo, player->mo->state-states); // Prevent visual errors when switching between skins with differing number of frames
		}

		// for replays: We have changed our skin mid-game; let the game know so it can do the same in the replay!
		demo_extradata[playernum] |= DXD_SKIN;

#ifdef HAVE_DISCORDRPC
		if (player - players == consoleplayer)
			DRPC_UpdatePresence();
#endif

		return;
	}

	if (P_IsLocalPlayer(player))
		CONS_Alert(CONS_WARNING, M_GetText("Requested skin %d not found\n"), skinnum);
	else if(server || IsPlayerAdmin(consoleplayer))
		CONS_Alert(CONS_WARNING, "Player %d (%s) skin %d not found\n", playernum, player_names[playernum], skinnum);

	SetPlayerSkinByNum(playernum, 0); // not found, put in the default skin
}

//
// Add skins from a pwad, each skin preceded by 'S_SKIN' marker
//

// Does the same is in w_wad, but check only for
// the first 6 characters (this is so we can have S_SKIN1, S_SKIN2..
// for wad editors that don't like multiple resources of the same name)
//
static UINT16 W_CheckForSkinMarkerInPwad(UINT16 wadid, UINT16 startlump)
{
	UINT16 i;
	const char *S_SKIN = "S_SKIN";
	lumpinfo_t *lump_p;

	// scan forward, start at <startlump>
	if (startlump < wadfiles[wadid]->numlumps)
	{
		lump_p = wadfiles[wadid]->lumpinfo + startlump;
		for (i = startlump; i < wadfiles[wadid]->numlumps; i++, lump_p++)
			if (memcmp(lump_p->name,S_SKIN,6)==0)
				return i;
	}
	return INT16_MAX; // not found
}

// turn _ into spaces and . into katana dot
#define SYMBOLCONVERT(name) for (value = name; *value; value++)\
	{\
		if (*value == '_') *value = ' ';\
	}

//
// Patch skins from a pwad, each skin preceded by 'P_SKIN' marker
//

// Does the same is in w_wad, but check only for
// the first 6 characters (this is so we can have P_SKIN1, P_SKIN2..
// for wad editors that don't like multiple resources of the same name)
//
static UINT16 W_CheckForPatchSkinMarkerInPwad(UINT16 wadid, UINT16 startlump)
{
	UINT16 i;
	const char *P_SKIN = "P_SKIN";
	lumpinfo_t *lump_p;

	// scan forward, start at <startlump>
	if (startlump < wadfiles[wadid]->numlumps)
	{
		lump_p = wadfiles[wadid]->lumpinfo + startlump;
		for (i = startlump; i < wadfiles[wadid]->numlumps; i++, lump_p++)
			if (memcmp(lump_p->name,P_SKIN,6)==0)
				return i;
	}
	return INT16_MAX; // not found
}

#define NUMKARTFRAMES 19
#define S(f, s) { f - 'A', SPR2_##s },
const UINT8 kart2spr2[26][2] = {
	S('A', STIN)
	S('B', STIN)
	S('C', STIL)
	S('D', STIL)
	S('E', STIR)
	S('F', STIR)
	S('J', SLWN)
	S('G', SLWN)
	S('K', SLWL)
	S('H', SLWL)
	S('L', SLWR)
	S('I', SLWR)
	S('A', FSTN)
	S('J', FSTN)
	S('C', FSTL)
	S('K', FSTL)
	S('E', FSTR)
	S('L', FSTR)
	S('M', DRLN)
	S('N', DRLN)
	S('O', DRRN)
	S('P', DRRN)
	S('Q', SPIN)
	S('Q', DEAD)
	S('R', KART)
	S('S', SIGN)
};
#undef S

static void R_LoadSkinSprites(UINT16 wadnum, UINT16 *lump, UINT16 *lastlump, skin_t *skin)
{
	UINT16 newlastlump;
	UINT8 sprite2;

	*lump += 1; // start after S_SKIN
	*lastlump = W_CheckNumForNamePwad("S_END",wadnum,*lump); // stop at S_END
	*lastlump = min(*lastlump, wadfiles[wadnum]->numlumps); // or end of WAD

	// old wadding practices die hard -- stop at S_SKIN (or P_SKIN) or S_START if they come before S_END.
	newlastlump = W_FindNextEmptyInPwad(wadnum,*lump);
	if (newlastlump < *lastlump) *lastlump = newlastlump;
	newlastlump = W_CheckForSkinMarkerInPwad(wadnum,*lump);
	if (newlastlump < *lastlump) *lastlump = newlastlump;
	newlastlump = W_CheckForPatchSkinMarkerInPwad(wadnum,*lump);
	if (newlastlump < *lastlump) *lastlump = newlastlump;
	newlastlump = W_CheckNumForNamePwad("S_START",wadnum,*lump);
	if (newlastlump < *lastlump) *lastlump = newlastlump;

	/*// ...and let's handle super, too
	newlastlump = W_CheckNumForNamePwad("S_SUPER",wadnum,*lump);
	if (newlastlump < *lastlump)
	{
		newlastlump++;
		// load all sprite sets we are aware of... for super!
		for (sprite2 = 0; sprite2 < free_spr2; sprite2++)
			R_AddSingleSpriteDef(spr2names[sprite2], &skin->sprites[FF_SPR2SUPER|sprite2], wadnum, newlastlump, *lastlump);

		newlastlump--;
		*lastlump = newlastlump; // okay, make the normal sprite set loading end there
	}*/

	boolean sonic = !strcmp(skin->name, "sonic") && wadnum == MAINWAD_MAIN;

	if (!wadfiles[wadnum]->compatmode && !sonic)
	{
		// load all sprite sets we are aware of... for normal stuff.
		for (sprite2 = 0; sprite2 < free_spr2; sprite2++)
			R_AddSingleSpriteDef(spr2names[sprite2], &skin->sprites[sprite2], wadnum, *lump, *lastlump);
	}
	else
	{
		// okay, now we have to get fancy...
		// first, just dump all the frames in a temporary def
		size_t i;
		spritedef_t *sd, tmp = sonic ? sprites[SPR_PLAY] : (spritedef_t){0};
		spriteframe_t *sf;
		if (!sonic)
		{
			const char *sprname = W_CheckNameForNumPwad(wadnum, *lump);
			for (newlastlump = *lump; newlastlump < *lastlump; newlastlump++)
				if (memcmp(W_CheckNameForNumPwad(wadnum, newlastlump), sprname, 4))
					break;
			*lastlump = newlastlump;
			R_AddSingleSpriteDef(sprname, &tmp, wadnum, *lump, *lastlump);
		}

		if (tmp.numframes < NUMKARTFRAMES)
			I_Error("R_LoadSkinSprites: too few frames for kart skin");

		// now shuffle them around to fit the new SPR2 system
		// iterate over kart2spr2, then add any excess frames to SPR2_KART
#define SPR2COUNT sizeof(kart2spr2)/sizeof(*kart2spr2)
		for (i = 0; i < SPR2COUNT - NUMKARTFRAMES + tmp.numframes; i++)
		{
			boolean excess = i >= SPR2COUNT;
			sf = &tmp.spriteframes[excess ? i - SPR2COUNT + NUMKARTFRAMES : kart2spr2[i][0]];
			sd = &skin->sprites[excess ? SPR2_KART : kart2spr2[i][1]];
			sd->spriteframes = Z_Realloc(sd->spriteframes, sizeof(*sd->spriteframes) * (sd->numframes+1), PU_STATIC, NULL);
			sd->spriteframes[sd->numframes++] = *sf;
		}
		if (!sonic)
			Z_Free(tmp.spriteframes);
#undef SPR2COUNT

		// now we have to rotate the drift frames. i hate this.
		sd = &skin->sprites[SPR2_DRRN];
		for (i = 0; i < sd->numframes; i++)
		{
			sf = &sd->spriteframes[i];
			if (sf->rotate != SRF_3D)
				continue;
			lumpnum_t lastpat = sf->lumppat[7];
			size_t lastid = sf->lumpid[7];
			for (int r = 7; r >= 1; r--)
			{
				sf->lumppat[r] = sf->lumppat[r-1];
				sf->lumpid[r] = sf->lumpid[r-1];
			}
			sf->lumppat[0] = lastpat;
			sf->lumpid[0] = lastid;
			sf->flip = (sf->flip & 0xff00) | ((sf->flip & 0x80) >> 7) | ((sf->flip & 0x7f) << 1);
		}
		sd = &skin->sprites[SPR2_DRLN];
		for (i = 0; i < sd->numframes; i++)
		{
			sf = &sd->spriteframes[i];
			if (sf->rotate != SRF_3D)
				continue;
			lumpnum_t firstpat = sf->lumppat[0];
			size_t firstid = sf->lumpid[0];
			for (int r = 0; r < 8; r++)
			{
				sf->lumppat[r] = sf->lumppat[r+1];
				sf->lumpid[r] = sf->lumpid[r+1];
			}
			sf->lumppat[7] = firstpat;
			sf->lumpid[7] = firstid;
			sf->flip = (sf->flip & 0xff00) | ((sf->flip & 0x01) << 7) | ((sf->flip & 0xfe) >> 1);
		}

		R_AddKartFaces(skin);

		skin->flags |= SF_OLDDEATH;
	}

	if (skin->sprites[0].numframes == 0)
		I_Error("R_LoadSkinSprites: no frames found for sprite SPR2_%s\n", spr2names[0]);
}
#undef NUMKARTFRAMES

static void R_IHateThatHedgehog(UINT16 wadnum)
{
	skin_t *skin = &skins[0];
	Sk_SetDefaultValue(skin);
	skin->wadnum = wadnum;
	strcpy(skin->name, "sonic");

#ifdef SKINVALUES
	skin_cons_t[0].value = 0;
	skin_cons_t[0].strvalue = skin->name;
#endif
	Forceskin_cons_t[1].value = 0;
	Forceskin_cons_t[1].strvalue = skin->name;

#ifdef HWRENDER
	if (rendermode == render_opengl)
		HWR_AddPlayerModel(0);
#endif
}

// returns whether found appropriate property
static boolean R_ProcessPatchableFields(skin_t *skin, char *stoken, char *value)
{
	boolean compat = wadfiles[skin->wadnum]->compatmode;

	if (!stricmp(stoken, "rivals"))
	{
		size_t len = strlen(value);
		size_t i;
		char rivalname[SKINNAMESIZE+1] = {0};
		UINT8 pos = 0;
		UINT8 numrivals = 0;

		// Can't use strtok, because the above function's already using it.
		// Using it causes it to upset the saved pointer,
		// corrupting the reading for the rest of the file.

		// So instead we get to crawl through the value, character by character,
		// and write it down as we go, until we hit a comma or the end of the string.
		// Yaaay.

		for (i = 0; i <= len; i++)
		{
			if (numrivals >= SKINRIVALS)
			{
				break;
			}

			if (value[i] == ',' || i == len)
			{
				if (pos == 0)
					continue;

				STRBUFCPY(skin->rivals[numrivals], rivalname);
				strlwr(skin->rivals[numrivals]);
				numrivals++;

				if (i == len)
					break;

				for (; pos > 0; pos--)
				{
					rivalname[pos] = '\0';
				}

				continue;
			}

			if (pos > SKINNAMESIZE)
			{
				CONS_Alert(CONS_ERROR, "Rival names for skin %s are too long.\n", skin->name);
				break;
			}

			rivalname[pos] = value[i];
			pos++;
		}
	}

	// custom translation table
	else if (!stricmp(stoken, "startcolor"))
		skin->starttranscolor = compat ? R_GetPaletteRemap(atoi(value)) : atoi(value);

#define FULLPROCESS(field) else if (!stricmp(stoken, #field)) skin->field = get_number(value);
	// character type identification
	FULLPROCESS(flags)
	FULLPROCESS(followitem)
#undef FULLPROCESS

#define GETSKINCOLOR(field) else if (!stricmp(stoken, #field)) \
{ \
	UINT16 color = R_GetColorByName(value); \
	skin->field = (color ? color : SKINCOLOR_GREEN); \
}
	GETSKINCOLOR(prefcolor)
	GETSKINCOLOR(prefoppositecolor)
#undef GETSKINCOLOR
	else if (!stricmp(stoken, "supercolor"))
	{
		UINT16 color = R_GetSuperColorByName(value);
		skin->supercolor = (color ? color : SKINCOLOR_SUPER1);
	}

#define GETFLOAT(field) else if (!stricmp(stoken, #field)) skin->field = FLOAT_TO_FIXED(atof(value));
	GETFLOAT(highresscale)
#undef GETFLOAT

#define GETKARTSTAT(field) \
	else if (!stricmp(stoken, #field)) \
	{ \
		skin->field = atoi(value); \
		if (skin->field < 1) skin->field = 1; \
		if (skin->field > 9) skin->field = 9; \
	}
	GETKARTSTAT(kartspeed)
	GETKARTSTAT(kartweight)
#undef GETKARTSTAT


#define GETFLAG(field) else if (!stricmp(stoken, #field)) { \
	strupr(value); \
	if (atoi(value) || value[0] == 'T' || value[0] == 'Y') \
		skin->flags |= (SF_##field); \
	else \
		skin->flags &= ~(SF_##field); \
}
	// parameters for individual character flags
	// these are uppercase so they can be concatenated with SF_
	// 1, true, yes are all valid values
	GETFLAG(MACHINE)
	GETFLAG(OLDDEATH)
#undef GETFLAG

#define GETPATCH(field) else if (compat && !stricmp(stoken, #field)) strncpy(skin->field, value, 8);
	GETPATCH(facerank)
	GETPATCH(facewant)
	GETPATCH(facemmap)
#undef GETPATCH

	else // let's check if it's a sound, otherwise error out
	{
		boolean found = false;
		sfxenum_t i;
		size_t stokenadjust;

		// Remove the prefix. (We need to affect an adjusting variable so that we can print error messages if it's not actually a sound.)
		if ((stoken[0] == 'D' || stoken[0] == 'd') && (stoken[1] == 'S' || stoken[1] == 's')) // DS*
			stokenadjust = 2;
		else // sfx_*
			stokenadjust = 4;

		// Remove the prefix. (We can affect this directly since we're not going to use it again.)
		if ((value[0] == 'D' || value[0] == 'd') && (value[1] == 'S' || value[1] == 's')) // DS*
			value += 2;
		else // sfx_*
			value += 4;

		// copy name of sounds that are remapped
		// for this skin
		for (i = 0; i < sfx_skinsoundslot0; i++)
		{
			if (!S_sfx[i].name)
				continue;
			if (S_sfx[i].skinsound != -1
				&& !stricmp(S_sfx[i].name,
					stoken + stokenadjust))
			{
				skin->soundsid[S_sfx[i].skinsound] =
					S_AddSoundFx(value, S_sfx[i].singularity, S_sfx[i].flags, true);
				found = true;
			}
		}
		return found;
	}
	return true;
}

//
// Find skin sprites, sounds & optional status bar face, & add them
//
void R_AddSkins(UINT16 wadnum)
{
	UINT16 lump, lastlump = 0;
	char *buf;
	char *buf2;
	char *stoken;
	char *value;
	size_t size;
	skin_t *skin;
	boolean realname;
	boolean iscompatskin = false;

	//
	// search for all skin markers in pwad
	//

	while ((lump = W_CheckForSkinMarkerInPwad(wadnum, lastlump)) != INT16_MAX)
	{
		// advance by default
		lastlump = lump + 1;

		if (numskins >= MAXSKINS)
		{
			CONS_Debug(DBG_RENDER, "ignored skin (%d skins maximum)\n", MAXSKINS);
			continue; // so we know how many skins couldn't be added
		}
		buf = W_CacheLumpNumPwad(wadnum, lump, PU_CACHE);
		size = W_LumpLengthPwad(wadnum, lump);

		// for strtok
		buf2 = malloc(size+1);
		if (!buf2)
			I_Error("R_AddSkins: No more free memory\n");
		M_Memcpy(buf2,buf,size);
		buf2[size] = '\0';

		// set defaults
		skin = &skins[numskins];
		Sk_SetDefaultValue(skin);
		skin->wadnum = wadnum;
		realname = false;
		// parse
		stoken = strtok (buf2, "\r\n= ");
		while (stoken)
		{
			if ((stoken[0] == '/' && stoken[1] == '/')
				|| (stoken[0] == '#'))// skip comments
			{
				strtok(NULL, "\r\n"); // skip end of line
				stoken = strtok(NULL, "\r\n= ");
				continue; // find the real next token
			}

			value = strtok(NULL, "\r\n= ");

			if (!value)
				I_Error("R_AddSkins: syntax error in S_SKIN lump# %d(%s) in WAD %s\n", lump, W_CheckNameForNumPwad(wadnum,lump), wadfiles[wadnum]->filename);

			// Some of these can't go in R_ProcessPatchableFields because they have side effects for future lines.
			// Others can't go in there because we don't want them to be patchable.
			if (!stricmp(stoken, "name"))
			{
				INT32 skinnum = R_SkinAvailable(value);
				strlwr(value);
				if (skinnum == -1)
					STRBUFCPY(skin->name, value);
				// the skin name must uniquely identify a single skin
				// if the name is already used I make the name 'namex'
				// using the default skin name's number set above
				else
				{
					const size_t stringspace =
						strlen(value) + sizeof (numskins) + 1;
					char *value2 = Z_Malloc(stringspace, PU_STATIC, NULL);
					snprintf(value2, stringspace,
						"%s%d", value, numskins);
					value2[stringspace - 1] = '\0';
					if (R_SkinAvailable(value2) == -1)
						// I'm lazy so if NEW name is already used I leave the 'skin x'
						// default skin name set in Sk_SetDefaultValue
						STRBUFCPY(skin->name, value2);
					Z_Free(value2);
				}

				// copy to hudname and fullname as a default.
				if (!realname)
				{
					STRBUFCPY(skin->realname, skin->name);
					SYMBOLCONVERT(skin->realname);
				}
			}
			else if (!stricmp(stoken, "realname"))
			{ // Display name (eg. "Knuckles")
				realname = true;
				STRBUFCPY(skin->realname, value);
				SYMBOLCONVERT(skin->realname)
			}
			else if (!R_ProcessPatchableFields(skin, stoken, value))
				CONS_Debug(DBG_SETUP, "R_AddSkins: Unknown keyword '%s' in S_SKIN lump #%d (WAD %s)\n", stoken, lump, wadfiles[wadnum]->filename);

			stoken = strtok(NULL, "\r\n= ");
		}
		free(buf2);

		// Add sprites
		R_LoadSkinSprites(wadnum, &lump, &lastlump, skin);
		//ST_LoadFaceGraphics(numskins); -- nah let's do this elsewhere

		R_FlushTranslationColormapCache();

		iscompatskin = wadfiles[skin->wadnum]->compatmode;

		CONS_Printf(M_GetText("Added skin '%s' %s\n"), skin->name, iscompatskin ? "in compatibility mode" : "");

#ifdef SKINVALUES
		skin_cons_t[numskins].value = numskins;
		skin_cons_t[numskins].strvalue = skin->name;
#endif

		// Update the forceskin possiblevalues
		Forceskin_cons_t[numskins+1].value = numskins;
		Forceskin_cons_t[numskins+1].strvalue = skins[numskins].name;

#ifdef HWRENDER
		if (rendermode == render_opengl)
			HWR_AddPlayerModel(numskins);
#endif

		skinsorted[numskins] = numskins;
		numskins++;
	}
	SortSkins();
	return;
}

//
// Patch skin sprites
//
void R_PatchSkins(UINT16 wadnum)
{
	UINT16 lump, lastlump = 0;
	char *buf;
	char *buf2;
	char *stoken;
	char *value;
	size_t size;
	skin_t *skin;
	boolean noskincomplain, realname;

	//
	// search for all skin patch markers in pwad
	//

	while ((lump = W_CheckForPatchSkinMarkerInPwad(wadnum, lastlump)) != INT16_MAX)
	{
		INT32 skinnum = 0;

		// advance by default
		lastlump = lump + 1;

		buf = W_CacheLumpNumPwad(wadnum, lump, PU_CACHE);
		size = W_LumpLengthPwad(wadnum, lump);

		// for strtok
		buf2 = malloc(size+1);
		if (!buf2)
			I_Error("R_PatchSkins: No more free memory\n");
		M_Memcpy(buf2,buf,size);
		buf2[size] = '\0';

		skin = NULL;
		noskincomplain = realname = false;

		/*
		Parse. Has more phases than the parser in R_AddSkins because it needs to have the patching name first (no default skin name is acceptible for patching, unlike skin creation)
		*/

		stoken = strtok(buf2, "\r\n= ");
		while (stoken)
		{
			if ((stoken[0] == '/' && stoken[1] == '/')
				|| (stoken[0] == '#'))// skip comments
			{
				strtok(NULL, "\r\n"); // skip end of line
				stoken = strtok(NULL, "\r\n= ");
				continue; // find the real next token
			}

			value = strtok(NULL, "\r\n= ");

			if (!value)
				I_Error("R_PatchSkins: syntax error in P_SKIN lump# %d(%s) in WAD %s\n", lump, W_CheckNameForNumPwad(wadnum,lump), wadfiles[wadnum]->filename);

			if (!skin) // Get the name!
			{
				if (!stricmp(stoken, "name"))
				{
					strlwr(value);
					skinnum = R_SkinAvailable(value);
					if (skinnum != -1)
						skin = &skins[skinnum];
					else
					{
						CONS_Debug(DBG_SETUP, "R_PatchSkins: unknown skin name in P_SKIN lump# %d(%s) in WAD %s\n", lump, W_CheckNameForNumPwad(wadnum,lump), wadfiles[wadnum]->filename);
						noskincomplain = true;
					}
				}
			}
			else // Get the properties!
			{
				// Some of these can't go in R_ProcessPatchableFields because they have side effects for future lines.
				if (!stricmp(stoken, "realname"))
				{ // Display name (eg. "Knuckles")
					realname = true;
					STRBUFCPY(skin->realname, value);
					SYMBOLCONVERT(skin->realname)
				}
				else if (!stricmp(stoken, "rivals"))
				{
					size_t len = strlen(value);
					size_t i;
					char rivalname[SKINNAMESIZE] = "";
					UINT8 pos = 0;
					UINT8 numrivals = 0;

					// Can't use strtok, because this function's already using it.
					// Using it causes it to upset the saved pointer,
					// corrupting the reading for the rest of the file.

					// So instead we get to crawl through the value, character by character,
					// and write it down as we go, until we hit a comma or the end of the string.
					// Yaaay.

					for (i = 0; i <= len; i++)
					{
						if (numrivals >= SKINRIVALS)
						{
							break;
						}

						if (value[i] == ',' || i == len)
						{
							STRBUFCPY(skin->rivals[numrivals], rivalname);
							strlwr(skin->rivals[numrivals]);
							numrivals++;

							memset(rivalname, 0, sizeof (rivalname));
							pos = 0;

							continue;
						}

						rivalname[pos] = value[i];
						pos++;
					}
				}
				else if (!R_ProcessPatchableFields(skin, stoken, value))
					CONS_Debug(DBG_SETUP, "R_PatchSkins: Unknown keyword '%s' in P_SKIN lump #%d (WAD %s)\n", stoken, lump, wadfiles[wadnum]->filename);
			}

			if (!skin)
				break;

			stoken = strtok(NULL, "\r\n= ");
		}
		free(buf2);

		if (!skin) // Didn't include a name parameter? What a waste.
		{
			if (!noskincomplain)
				CONS_Debug(DBG_SETUP, "R_PatchSkins: no skin name given in P_SKIN lump #%d (WAD %s)\n", lump, wadfiles[wadnum]->filename);
			continue;
		}

		// Patch sprites
		R_LoadSkinSprites(wadnum, &lump, &lastlump, skin);
		//ST_LoadFaceGraphics(skinnum); -- nah let's do this elsewhere

		R_FlushTranslationColormapCache();

		CONS_Printf(M_GetText("Patched skin '%s'\n"), skin->name);
	}
	return;
}

#undef SYMBOLCONVERT
