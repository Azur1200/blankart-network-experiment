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
/// \file  g_demo.c
/// \brief Demo recording and playback

#include "command.h"
#include "doomdef.h"
#include "console.h"
#include "d_main.h"
#include "d_player.h"
#include "d_clisrv.h"
#include "p_setup.h"
#include "i_time.h"
#include "i_system.h"
#include "m_random.h"
#include "p_local.h"
#include "r_draw.h"
#include "r_main.h"
#include "g_game.h"
#include "g_demo.h"
#include "m_misc.h"
#include "m_menu.h"
#include "m_argv.h"
#include "m_textinput.h"
#include "hu_stuff.h"
#include "z_zone.h"
#include "i_video.h"
#include "byteptr.h"
#include "i_gamepad.h"
#include "r_local.h"
#include "r_skins.h"
#include "y_inter.h"
#include "v_video.h"
#include "lua_hook.h"
#include "p_saveg.h" // savebuffer_t
#include "g_party.h"

// SRB2Kart
#include "d_netfil.h" // nameonly

#include "lua_script.h" // LUA_Sync
#include "lua_libs.h" // gL (Lua state)

#include "k_kart.h"
#include "k_battle.h"
#include "k_bot.h"
#include "k_color.h"
#include "k_follower.h"
#include "k_grandprix.h"
#include "k_vote.h"

static CV_PossibleValue_t recordmultiplayerdemos_cons_t[] = {
    {DEMOSAVE_NONE, "Disabled"},
    {DEMOSAVE_MANUAL, "Manual Save"},
    {DEMOSAVE_AUTO_NETONLY, "Auto Save Online"},
    {DEMOSAVE_AUTO, "Auto Save All"},
    {0, NULL}};

consvar_t cv_recordmultiplayerdemos =
    CVAR_INIT("netdemo_record", "Manual Save", CV_SAVE, recordmultiplayerdemos_cons_t, NULL);

static CV_PossibleValue_t netdemosyncquality_cons_t[] = {{1, "MIN"}, {35, "MAX"}, {0, NULL}};
consvar_t cv_netdemosyncquality = CVAR_INIT ("netdemo_syncquality", "1", CV_SAVE, netdemosyncquality_cons_t, NULL);

consvar_t cv_netdemosize = CVAR_INIT ("netdemo_size", "6", CV_SAVE, CV_Natural, NULL);
consvar_t cv_resyncdemo = CVAR_INIT("democonsistency", "On", 0, CV_OnOff, NULL);

static int32_t ReplaySave(int32_t choice)
{
	(void)choice;
	demo.savemode = DSM_WILLSAVE;

	return true;
}

consvar_t cv_dummyreplaysave = CVAR_INIT ("dummyreplaysave", "", CV_HIDEN, NULL, NULL);

static char replaytext[] = "Enter the name of the replay";
menuitem_t replaysave = {.text = replaytext, .cvar = &cv_dummyreplaysave, .routine = &ReplaySave};

dboolean nodrawers; // for comparative timing purposes
dboolean noblit; // for comparative timing purposes
tic_t demostarttime; // for comparative timing purposes

static char demoname[MAX_WADPATH];
static savebuffer_t demobuf = {0};
static uint8_t *demotime_p, *demoinfo_p;
static uint8_t demoflags;
dboolean demosynced = true; // console warning message

struct demovars_s demo;

dboolean metalrecording; // recording as metal sonic
mobj_t *metalplayback;
static uint8_t *metalbuffer = NULL;
static uint8_t *metal_p;
static uint16_t metalversion;

// extra data stuff (events registered this frame while recording)
static struct {
	uint8_t flags; // EZT flags

	// EZT_COLOR
	uint8_t color, lastcolor;

	// EZT_SCALE
	fixed_t scale, lastscale;

	// EZT_KART
	int32_t kartitem, kartamount, kartbumpers;

	uint8_t desyncframes; // Don't try to resync unless we've been off for two frames, to monkeypatch a few trouble spots

	// EZT_HIT
	uint16_t hits;
	mobj_t **hitlist;
} ghostext[MAXPLAYERS];

// Your naming conventions are stupid and useless.
// There is no conflict here.
demoghost *ghosts = NULL;

//
// DEMO RECORDING
//

#define DEMOVERSION 0x0014
#define DEMOHEADER  "\xF0" "BlanReplay" "\x0F"

#define DF_GHOST        0x01 // This demo contains ghost data too!
#define DF_TIMEATTACK   0x02 // This demo is from Time Attack and contains its final completion time & best lap!
#define DF_ITEMBREAKER 0x04 // This demo is from Item Breaker and contains its final completion time!
#define DF_ATTACKMASK   0x06 // This demo is from ??? attack and contains ???

// 0x08 free

#define DF_NONETMP		0x10 // multiplayer but not netgame

#define DF_LUAVARS		0x20 // this demo contains extra lua vars

#define DF_ATTACKSHIFT  1
#define DF_ENCORE       0x40
#define DF_MULTIPLAYER  0x80 // This demo was recorded in multiplayer mode!
#define DF_GRANDPRIX    0x0100

#define DEMO_SPECTATOR	0x01
#define DEMO_KICKSTART	0x02
#define DEMO_SHRINKME	0x04
#define DEMO_BOT		0x08

// For demos
#define ZT_FWD		0x0001
#define ZT_SIDE		0x0002
#define ZT_TURNING	0x0004
#define ZT_ANGLE	0x0008
#define ZT_THROWDIR	0x0010
#define ZT_BUTTONS	0x0020
#define ZT_AIMING	0x0040
#define ZT_LATENCY	0x0080
#define ZT_FLAGS	0x0100
// Ziptics are uint16_t now, go nuts

#define DEMOMARKER 0x80 // demobuf.end

uint8_t demo_extradata[MAXPLAYERS];
uint8_t demo_writerng; // 0=no, 1=yes, 2=yes but on a timeout
static ticcmd_t oldcmd[MAXPLAYERS];

#define METALDEATH 0x44
#define METALSNICE 0x69

#define DW_END        0xFF // End of extradata block
#define DW_RNG        0xFE // Check RNG seed!

#define DW_EXTRASTUFF 0xFE // Numbers below this are reserved for writing player slot data

// Below consts are only used for demo extrainfo sections
#define DW_STANDING 0x00

// For Metal Sonic and time attack ghosts
#define GZT_XYZ    0x01
#define GZT_MOMXY  0x02
#define GZT_MOMZ   0x04
#define GZT_ANGLE  0x08
#define GZT_FRAME  0x10 // Animation frame
#define GZT_SPR2   0x20 // Player animations
#define GZT_EXTRA  0x40
#define GZT_FOLLOW 0x80 // Followmobj

// GZT_EXTRA flags
#define EZT_COLOR  0x001 // Changed color (Super transformation, Mario fireflowers/invulnerability, etc.)
#define EZT_FLIP   0x002 // Reversed gravity
#define EZT_SCALE  0x004 // Changed size
#define EZT_HIT    0x008 // Damaged a mobj
#define EZT_SPRITE 0x010 // Changed sprite set completely out of PLAY (NiGHTS, SOCs, whatever)
#define EZT_KART   0x020 // SRB2Kart: Changed current held item/quantity and bumpers for battle

// GZT_FOLLOW flags
#define FZT_SPAWNED 0x01 // just been spawned
#define FZT_SKIN 0x02 // has skin
#define FZT_LINKDRAW 0x04 // has linkdraw (combine with spawned only)
#define FZT_COLORIZED 0x08 // colorized (ditto)
#define FZT_SCALE 0x10 // different scale to object
// spare FZT slots 0x20 to 0x80

//
// Checks if auto-saving demos is allowed.
//
dboolean G_DemoWillAutoSave(void)
{
	if (cv_recordmultiplayerdemos.value == DEMOSAVE_AUTO_NETONLY)
	{
		return (netgame);
	}
	else if (cv_recordmultiplayerdemos.value == DEMOSAVE_AUTO)
	{
		return true;
	}

	return false;
}

typedef struct
{
	char *filename;
	uint64_t filehash;
	dboolean compatmode;
} demofile_t;

typedef struct
{
	char *name; // NULL = bad netid
	char *value;
	dboolean stealth;
	uint16_t legacynetid;
} democvar_t;

typedef struct
{
	uint8_t playernum;
	uint8_t flags;

	struct // DEMO_BOT
	{
		uint8_t difficulty;
		uint8_t diffincrease;
		dboolean rival;
	} bot;

	char name[21+1];
	char skin[16+1];
	char color[16+1];
	char follower[16+1];
	char followercolor[16+1];
	char voice[32+1];

	uint32_t score;
	uint16_t powerlevel;
	uint8_t kartspeed;
	uint8_t kartweight;
	mobjtype_t followitem;
	uint8_t availabilities[MAXAVAILABILITY];
} demoplayer_t;

typedef struct
{
	uint8_t version, subversion;
	uint16_t demoversion;
	char demotitle[64];
	uint64_t demochecksum;

	char maptitle[MAXMAPLUMPNAME];
	uint64_t mapchecksum;
	uint8_t demoflags;
	uint32_t raflags;
	char rapreset[MAXPRESETNAME+1];
	uint8_t rapresetversion;
	uint8_t gametype;
	uint8_t numlaps;
	char servermotd[MAXSERVERMOTD];
	char servername[MAXSERVERNAME];
	char servercontact[MAXSERVERCONTACT];
	char serverdescription[MAXSERVERDESCRIPTION];

	uint8_t numfiles;
	demofile_t *files;

	struct // DF_ATTACKMASK
	{
		uint32_t time;
		uint32_t lap;
	} attacking;

	uint32_t randseed[PRNUMCLASS];
	dboolean single_seed;
	uint32_t extrainfo;

	uint16_t numcvars;
	democvar_t *cvars;

	struct // DF_GRANDPRIX
	{
		dboolean masterbots;
		dboolean lunaticmode;
		dboolean ranked;
		uint8_t gamespeed;
		uint8_t eventmode;
	} grandprix;

	uint8_t mapmusrng;

	uint8_t numplayers;
	demoplayer_t *playerdata;

	dboolean empty;
	uint32_t endofs;
} demoheader_t;

typedef struct
{
	mobjtype_t type;
	uint16_t health;
	fixed_t x, y, z;
	angle_t angle;
} zipticdamage_t;

typedef struct
{
	uint8_t flags;
	fixed_t x, y, z; // or momx, momy, momz
	angle_t angle;
	uint8_t frame, spr2;

	// GZT_EXTRA
	uint8_t extraflags;
	uint16_t color;
	fixed_t scale;
	uint16_t damagecount;
	zipticdamage_t *damage;
	uint16_t sprite;
	int32_t kartitem, kartamount, kartbumpers;

	uint8_t followflags;
	struct // GZT_FOLLOW
	{
		fixed_t height;
		uint16_t skin;
		fixed_t scale;
		fixed_t xofs, yofs, zofs;
		uint16_t sprite2;
		uint16_t sprite;
		uint8_t frame;
		uint16_t color;
	} follow;
} ziptic_t;

typedef struct
{
	uint8_t playernum; // index of player, or >= DW_EXTRASTUFF for special data
	uint8_t flags; // DXD flags if playernum < DW_EXTRASTUFF

	// DW_RNG
	uint32_t randseed[PRNUMCLASS];

	struct // DXD_JOINDATA
	{
		dboolean bot;
		uint8_t difficulty;
		uint8_t diffincrease;
		dboolean rival;
	} joindata;

	// DXD_PLAYSTATE
	uint8_t playstate;

	// DXD_NAME
	char playername[21+1];

	// DXD_SKIN
	char skinname[17];
	char voicename[33];
	uint8_t kartspeed;
	uint8_t kartweight;

	// DXD_COLOR
	char colorname[17];

	// DXD_FOLLOWER
	char followername[17];
	char followercolor[17];

	// DXD_WEAPONPREF
	uint8_t weaponpref;
	uint8_t driftmode;
} extradata_t;

typedef enum
{
	HEADER_OK,
	HEADER_BADMAGIC,   // DEMOHEADER doesn't match
	HEADER_BADVERSION, // unsupported version
	HEADER_BADFORMAT,  // "PLAY" doesn't match, or some other error
} headerstatus_e;

static uint8_t *G_ReadZipTic(ticcmd_t *cmd, uint8_t *dp, uint16_t version)
{
	uint16_t ziptic;
	dboolean kart = version <= 0x0002;
	if (kart)
	{
		ziptic = READUINT8(dp);
		ziptic = (ziptic & 3) | ((ziptic & (4|64)) << 1) | ((ziptic & (8|16)) << 2) | ((ziptic & 32) >> 3);
	}
	else
		ziptic = READUINT16(dp);

	if (ziptic & ZT_FWD)
		cmd->forwardmove = READINT8(dp);
	if (ziptic & ZT_SIDE)
		cmd->sidemove = READINT8(dp);
	if (!kart && ziptic & ZT_TURNING)
		cmd->turning = READINT16(dp);
	if (ziptic & ZT_ANGLE)
		cmd->angle = READINT16(dp);
	if (ziptic & ZT_THROWDIR)
		cmd->throwdir = READINT16(dp);
	if (ziptic & ZT_BUTTONS)
		cmd->buttons = READUINT16(dp);
	if (ziptic & ZT_AIMING)
		cmd->aiming = READINT16(dp);
	if (kart && ziptic & ZT_TURNING)
		cmd->turning = READINT16(dp);
	if (ziptic & ZT_LATENCY)
		cmd->latency = READUINT8(dp);
	if (ziptic & ZT_FLAGS)
		cmd->flags = READUINT8(dp);

	if (version < 0x000a && ziptic & 0x8000) // ZT_BOT
	{
		uint16_t botziptic = READUINT16(dp);
		if (botziptic & 1) (void)READINT8(dp);
		if (botziptic & 2) (void)READINT8(dp);
		if (botziptic & 4) (void)READINT8(dp);
	}
	return dp;
}

// call G_FreeExtraZipTic, you get the idea
static uint8_t *G_ReadExtraZipTic(ziptic_t *zt, uint8_t *dp, uint16_t version, mobj_t *mo)
{
	dboolean kart = version <= 0x0002;
	uint8_t ziptic = READUINT8(dp);

	if (kart)
		// shift GZT_EXTRA to its correct position
		// mask out GZT_NIGHTS (this ain't SRB2!)
		ziptic = (ziptic & 0x1f) | ((ziptic & 0x20) << 1);

	zt->flags = ziptic;
	zt->damagecount = 0;

	if (ziptic & GZT_XYZ)
	{
		zt->x = READFIXED(dp);
		zt->y = READFIXED(dp);
		zt->z = READFIXED(dp);
	}
	else
	{
		if (ziptic & GZT_MOMXY)
		{
			zt->x = kart ? READINT16(dp)<<8 : READFIXED(dp);
			zt->y = kart ? READINT16(dp)<<8 : READFIXED(dp);
		}
		if (ziptic & GZT_MOMZ)
			zt->z = kart ? READINT16(dp)<<8 : READFIXED(dp);
	}

	if (ziptic & GZT_ANGLE)
		zt->angle = READUINT8(dp)<<24;

	if (ziptic & GZT_FRAME)
	{
		if (kart)
		{
			zt->spr2 = P_KartFrameToSprite2(mo->skin, READUINT8(dp), &zt->frame);
			zt->flags |= GZT_SPR2;
		}
		else
			zt->frame = READUINT8(dp);
	}

	if (ziptic & GZT_SPR2)
		zt->spr2 = READUINT8(dp);

	if (ziptic & GZT_EXTRA)
	{ // But wait, there's more!
		uint8_t xziptic = READUINT8(dp);

		// shifted down due to removal of EZT_THOKMASK
		// thoks don't have any extra bytes so just ignore them
		if (kart)
			xziptic >>= 2;

		zt->extraflags = xziptic;

		if (xziptic & EZT_COLOR)
			zt->color = kart ? READUINT8(dp) : READUINT16(dp);

		if (xziptic & EZT_SCALE)
			zt->scale = READFIXED(dp);

		if (xziptic & EZT_HIT)
		{ // Resync mob damage.
			zt->damagecount = READUINT16(dp);
			if (zt->damagecount)
				zt->damage = malloc(sizeof(zipticdamage_t) * zt->damagecount);
			for (uint16_t i = 0; i < zt->damagecount; i++)
			{
				zipticdamage_t *damage = &zt->damage[i];
				if (kart)
					(void)READUINT32(dp);
				damage->type = READUINT32(dp);
				damage->health = READUINT16(dp);
				damage->x = READFIXED(dp);
				damage->y = READFIXED(dp);
				damage->z = READFIXED(dp);
				damage->angle = READANGLE(dp);
			}
		}

		if (xziptic & EZT_SPRITE)
		{
			if (kart)
			{
				if (READUINT8(dp) != 3)
					CONS_Alert(CONS_WARNING, "sprite other than SPR_PLAY... oh dear");
				zt->sprite = SPR_PLAY;
			}
			else
				zt->sprite = READUINT16(dp);
		}

		if (xziptic & EZT_KART)
		{
			zt->kartitem = READINT32(dp);
			zt->kartamount = READINT32(dp);
			zt->kartbumpers = READINT32(dp);
		}
	}

	if (ziptic & GZT_FOLLOW)
	{ // Even more...
		uint8_t followtic = READUINT8(dp);
		if (followtic & FZT_SPAWNED)
		{
			zt->follow.height = READINT16(dp)<<FRACBITS;
			if (followtic & FZT_SKIN)
				zt->follow.skin = READUINT16(dp);
		}

		if (followtic & FZT_SCALE)
			zt->follow.scale = READFIXED(dp);

		zt->follow.xofs = READFIXED(dp);
		zt->follow.yofs = READFIXED(dp);
		zt->follow.zofs = READFIXED(dp);
		if (followtic & FZT_SKIN)
			zt->follow.sprite2 = READUINT16(dp);
		zt->follow.sprite = READUINT16(dp);
		zt->follow.frame = READUINT8(dp);
		zt->follow.color = READUINT16(dp);
	}

	return dp;
}

static void G_FreeExtraZipTic(ziptic_t *zt)
{
	if (zt->damagecount)
		free(zt->damage);

#ifdef PARANOIA
	memset(zt, 0xff, sizeof(*zt));
#endif
}

static uint8_t *G_ReadRawExtraData(extradata_t *extra, uint8_t *dp, uint16_t version)
{
	int32_t i;
	uint8_t extradata;
	extra->playernum = READUINT8(dp);
	dboolean kart = version <= 0x0002;
	dboolean singleseed = version <= 0x0012;

	if (extra->playernum >= DW_EXTRASTUFF)
	{
		extra->flags = 0;
		if (extra->playernum == DW_RNG)
		{
			if (singleseed)
			{
				extra->randseed[PR_LEGACY] = READUINT32(dp);
			}
			else
			{
				for (i = 0; i < PRNUMCLASS; i++)
				{
					extra->randseed[i] = READUINT32(dp);
				}
			}
		}
		// DW_END has no data
		return dp;
	}

	extradata = READUINT8(dp);

	if (kart)
	{
		//              kart  blan
		// DXD_JOINDATA  N/A    0
		// DXD_PLAYSTATE  4     1
		// DXD_NAME       2     2
		// DXD_SKIN       1     3
		// DXD_COLOR      3     4
		// DXD_FOLLOWER  N/A    5
		// DXD_RESPAWN    0     6
		// completely rearranged... UGH...
		extradata =
		  ((extradata & 0x01) << 6) // DXD_RESPAWN
		| ((extradata & 0x02) << 2) // DXD_SKIN
		| ((extradata & 0x04)     ) // DXD_NAME
		| ((extradata & 0x08) << 1) // DXD_COLOR
		| ((extradata & 0x10) >> 3) // DXD_PLAYSTATE
		;
	}

	extra->flags = extradata;

	if (extradata & DXD_JOINDATA)
	{
		if (version >= 0x000F)
		{
			for (i = 0; i < MAXAVAILABILITY; i++)
			{
				players[extra->playernum].availabilities[i] = READUINT8(dp);
			}
		}

		extra->joindata.bot = !!(READUINT8(dp));
		if (extra->joindata.bot)
		{
			extra->joindata.difficulty = READUINT8(dp);
			extra->joindata.diffincrease = READUINT8(dp); // needed to avoid having to duplicate logic
			extra->joindata.rival = (dboolean)READUINT8(dp);
		}
	}
	if (!kart) // READING ORDER MATTERS if that wasn't obvious
	{
		if (extradata & DXD_PLAYSTATE)
		{
			extra->playstate = READUINT8(dp);
		}
		if (extradata & DXD_NAME)
		{
			if (version <= 0x000C)
			{
				READMEM(dp, extra->playername, 16);
				extra->playername[16] = '\0';
			}
			else
				READSTRINGL(dp, extra->playername, 21+1);
		}
	}
	if (extradata & DXD_SKIN)
	{
		if (version <= 0x000C)
		{
			READMEM(dp, extra->skinname, 16);
			extra->skinname[16] = '\0';

			extra->voicename[0] = '\0';
		}
		else
		{
			READSTRINGL(dp, extra->skinname, 16+1);

			if (version > 0x000D)
				READSTRINGL(dp, extra->voicename, 32+1);
			else
				extra->voicename[0] = '\0';
		}
		extra->kartspeed = READUINT8(dp);
		extra->kartweight = READUINT8(dp);
	}
	if (extradata & DXD_COLOR)
	{
		if (version <= 0x000C)
		{
			READMEM(dp, extra->colorname, 16);
			extra->colorname[16] = '\0';
		}
		else
			READSTRINGL(dp, extra->colorname, 16+1);
	}
	if (kart)
	{
		if (extradata & DXD_NAME)
		{
			READMEM(dp, extra->playername, 16);
			extra->playername[16] = '\0';
		}
		if (extradata & DXD_PLAYSTATE)
		{
			extra->playstate = READUINT8(dp);
		}
	}
	if (extradata & DXD_FOLLOWER)
	{
		if (version <= 0x000C)
		{
			READMEM(dp, extra->followername, 16);
			extra->followername[16] = '\0';
			READMEM(dp, extra->followercolor, 16);
			extra->followercolor[16] = '\0';
		}
		else
		{
			READSTRINGL(dp, extra->followername, 16+1);
			READSTRINGL(dp, extra->followercolor, 16+1);
		}
	}
	if (extradata & DXD_WEAPONPREF)
	{
		extra->weaponpref = READUINT8(dp);
		extra->driftmode = READUINT8(dp);
	}

	return dp;
}

// parses a demo header from the given byte pointer
// remember to call G_FreeDemoHeader!
static headerstatus_e G_ReadDemoHeader(uint8_t *dp, demoheader_t *header)
{
	uint8_t *startdp = dp;
	uint8_t attack;
	dboolean kart = false, oldkart = false;
	dboolean raflag = false;
	dboolean serverinfo = true;
	dboolean servermotd = true;
	dboolean rapreset = true; // + extended serverinfo length
	dboolean dubs = true; // Multiple voices
	dboolean availabilities = true; // Store player availabilities
	dboolean gplunatic = true; // Grand Prix: Lunatic mode
	dboolean multiseed = true; // Multi-seeded RNG
	dboolean ranked_gp = true; // Grand Prix: Ranked races
	int32_t i;

	// these may not be present in old demo formats, so initialize them
	// also initialize them so the header can be free'd without issues
	header->numplayers = 0;
	header->numfiles = 0;
	header->numcvars = 0;
	header->mapmusrng = 0;
	header->numlaps = 0;
	header->raflags = 0;
	memset(header->servermotd, 0, MAXSERVERMOTD);
	memset(header->servername, 0, MAXSERVERNAME);
	memset(header->servercontact, 0, MAXSERVERCONTACT);
	memset(header->serverdescription, 0, MAXSERVERDESCRIPTION);
	header->empty = false;

	dp += 12;
	header->version = READUINT8(dp);
	header->subversion = READUINT8(dp);
	header->demoversion = READUINT16(dp);

	if (!memcmp(startdp, DEMOHEADER, 12)) // BlanReplay
	{
		switch (header->demoversion)
		{
		case DEMOVERSION: // latest always supported
			break;

		case 0x0009:
			serverinfo = false;
			/* FALLTHRU */
		case 0x000A:
		case 0x000B:
		case 0x000C:
			rapreset = false;
			raflag = true;
			/* FALLTHRU */
		case 0x000D:
			dubs = false;
			/* FALLTHRU */
		case 0x000E:
			availabilities = false;
			/* FALLTHRU */
		case 0x000F:
			gplunatic = false;
			/* FALLTHRU */
		case 0x0010:
			/* FALLTHRU */
		case 0x0011:
			servermotd = false;
			/* FALLTHRU */
		case 0x0012:
			multiseed = false;
			break;
		case 0x0013:
			ranked_gp = false;
			break;

		default: // too old, cannot support.
			return HEADER_BADVERSION;
		}
	}
	else if (!memcmp(startdp, "\xF0" "KartReplay" "\x0F", 12))
	{
		dubs = rapreset = raflag = false;
		gplunatic = serverinfo = availabilities = false;
		ranked_gp = multiseed = false;
		switch (header->demoversion)
		{
		case 0x0001: // SRB2Kart 1.0.x (only staff ghosts supported)
			oldkart = kart = true;
			break;

		case 0x0002: // SRB2Kart 1.1+
			kart = true;
			break;

		case 0x0008: // BlanKart (Pre-RA mod flags)
			break;

		default: // too old, cannot support.
			return HEADER_BADVERSION;
		}
	}
	else
		return HEADER_BADMAGIC;

	if (oldkart)
		header->demotitle[0] = '\0';
	else
		READMEM(dp, &header->demotitle, 64);

	header->demochecksum = READUINT64(dp);
	if (kart) (void)READUINT64(dp); // truncate the md5, this is only used for avoiding duped ghosts

	if (memcmp(dp, "PLAY", 4))// && memcmp(dp, "METL", 4))
		return HEADER_BADFORMAT;
	dp += 4;

	if (kart)
	{
		int16_t mapnum = READINT16(dp);
		if (mapnum < 100)
			snprintf(header->maptitle, sizeof(header->maptitle), "MAP%02d", mapnum);
		else
			snprintf(header->maptitle, sizeof(header->maptitle), "MAP%c%c", 'A' + (mapnum - 100)/36, ((mapnum - 100) % 36 < 10 ? '0' : 'A'-10) + (mapnum - 100) % 36);
	}
	else
	{
		READSTRINGL(dp, header->maptitle, MAXMAPLUMPNAME);
		if (dp[-1] != '\0') dp++;
	}

	header->mapchecksum = READUINT64(dp);
	if (kart) (void)READUINT64(dp); // not even used anywhere

	header->demoflags = READUINT8(dp);

	if (raflag)
	{
		header->raflags = READUINT32(dp);
	}

	if (rapreset)
	{
		READSTRINGL(dp, header->rapreset, sizeof(header->rapreset));
		header->rapresetversion = READUINT8(dp);
	}

	if (serverinfo)
	{
		size_t maxservercontact  = rapreset ? MAXSERVERCONTACT : 320;
		size_t maxdescription = rapreset ? MAXSERVERDESCRIPTION : 320;

		if (servermotd)
		{
			READSTRINGL(dp, header->servermotd, MAXSERVERMOTD);
			if (dp[-1] != '\0') dp++;

		}

		READSTRINGL(dp, header->servername, MAXSERVERNAME);
		if (dp[-1] != '\0') dp++;
		READSTRINGL(dp, header->servercontact, maxservercontact);
		if (dp[-1] != '\0') dp++;
		READSTRINGL(dp, header->serverdescription, maxdescription);
		if (dp[-1] != '\0') dp++;
	}

	if (oldkart)
	{
		if (header->demoflags & DF_MULTIPLAYER)
			return HEADER_BADFORMAT; // multiplayer not supported

		header->gametype = (header->demoflags & 0x30)>>4;

		if (header->demoflags & 0x08) // DF_FILELIST
			goto readfiles;
		else
			goto skipfiles;
	}
	else
	{
		header->gametype = READUINT8(dp);
		if (!kart)
			header->numlaps = READUINT8(dp);
	}

readfiles:
	header->numfiles = READUINT8(dp);
	if (header->numfiles)
		header->files = malloc(sizeof(demofile_t) * header->numfiles);
	for (i = 0; i < header->numfiles; i++)
	{
		char filename[MAX_WADPATH];
		READSTRINGL(dp, filename, MAX_WADPATH);
		header->files[i].filename = strdup(filename);

		if (kart)
		{
			dp += 16; // md5 is useless anyway
			header->files[i].filehash = 0;
			header->files[i].compatmode = true; // duh...
		}
		else
		{
			header->files[i].filehash = READUINT64(dp);
			header->files[i].compatmode = READUINT8(dp) & 1;
		}
	}

skipfiles:
	attack = (header->demoflags & DF_ATTACKMASK) >> DF_ATTACKSHIFT;
	header->attacking.time = attack == ATTACKING_TIME || attack == ATTACKING_ITEMBREAK ? READUINT32(dp) : UINT32_MAX;
	header->attacking.lap = attack == ATTACKING_TIME ? READUINT32(dp) : UINT32_MAX;

	// Random seed
	header->single_seed = false;

	if (multiseed)
	{
		for (i = 0; i < PRNUMCLASS; i++)
		{
			header->randseed[i] = READUINT32(dp);
		}
	}
	else
	{
		header->single_seed = true;
		header->randseed[PR_LEGACY] = READUINT32(dp);
	}

	if (oldkart)
	{
		header->numplayers = 1;
		header->playerdata = malloc(sizeof(demoplayer_t));
		demoplayer_t *plr = &header->playerdata[0];

		plr->playernum = 0;
		plr->flags = 0;
		READMEM(dp, plr->name, 16);
		plr->name[16] = '\0';
		READMEM(dp, plr->skin, 16);
		plr->skin[16] = '\0';
		READMEM(dp, plr->color, 16);
		plr->color[16] = '\0';
		strcpy(plr->follower, "None");
		strcpy(plr->followercolor, "Default");
		strcpy(plr->voice, "Default");
		plr->followitem = MT_NULL;

		dp += 5; // Backwards compat - some stats
		// SRB2kart
		plr->kartspeed = READUINT8(dp);
		plr->kartweight = READUINT8(dp);
		//
		dp += 9; // Backwards compat - more stats
	}
	else
		header->extrainfo = READUINT32(dp);

	// net var data
	header->numcvars = READUINT16(dp);
	if (header->numcvars)
		header->cvars = malloc(sizeof(democvar_t) * header->numcvars);
	for (i = 0; i < header->numcvars; i++)
	{
		if (kart)
		{
			uint16_t netid = READUINT16(dp);
			consvar_t *legacycvar = CV_FindLegacyNetVar(netid);
			header->cvars[i].name = legacycvar ? strdup(legacycvar->name) : NULL;
			header->cvars[i].legacynetid = netid;
		}
		else
		{
			header->cvars[i].name = strdup((char *)dp);
			SKIPSTRING(dp);
		}
		header->cvars[i].value = strdup((char *)dp);
		SKIPSTRING(dp);
		header->cvars[i].stealth = READUINT8(dp);
	}

	if (header->demoflags & DF_GRANDPRIX)
	{
		header->grandprix.gamespeed = READUINT8(dp);
		header->grandprix.masterbots = READUINT8(dp) != 0;

		if (gplunatic)
			header->grandprix.lunaticmode = READUINT8(dp) != 0;

		if (ranked_gp)
			header->grandprix.ranked = READUINT8(dp) != 0;

		header->grandprix.eventmode = READUINT8(dp);
	}

	// Load "mapmusrng" used for altmusic selection
	if (!kart)
		header->mapmusrng = READUINT8(dp);

	if (oldkart)
		goto end;

	// Sigh ... it's an empty demo.
	if (*dp == DEMOMARKER)
	{
		header->empty = true;
		goto end;
	}

	// Load players that were in-game when the map started
	uint8_t playernum;
	header->playerdata = NULL;
	while ((playernum = READUINT8(dp)) != 0xff)
	{
		header->playerdata = realloc(header->playerdata, sizeof(demoplayer_t) * ++header->numplayers);
		demoplayer_t *plr = &header->playerdata[header->numplayers - 1];
		plr->playernum = playernum;

		if (kart)
		{
			plr->flags = playernum & 0x40 ? DEMO_SPECTATOR : 0;
			plr->playernum &= ~0x40;
		}
		else
			plr->flags = READUINT8(dp);

		if (plr->flags & DEMO_BOT)
		{
			plr->bot.difficulty = READUINT8(dp);
			plr->bot.diffincrease = READUINT8(dp); // needed to avoid having to duplicate logic
			plr->bot.rival = (dboolean)READUINT8(dp);
		}

		if (rapreset)
		{
			READSTRINGL(dp, plr->name, 21+1);
			READSTRINGL(dp, plr->skin, 16+1);
			READSTRINGL(dp, plr->color, 16+1);

			if (dubs)
				READSTRINGL(dp, plr->voice, 32+1);

			READSTRINGL(dp, plr->follower, 16+1);
			READSTRINGL(dp, plr->followercolor, 16+1);
		}
		else
		{
			READMEM(dp, plr->name, 16);
			plr->name[16] = '\0';
			READMEM(dp, plr->skin, 16);
			plr->skin[16] = '\0';
			READMEM(dp, plr->color, 16);
			plr->color[16] = '\0';
			if (!kart)
			{
				READMEM(dp, plr->follower, 16);
				plr->follower[16] = '\0';
				READMEM(dp, plr->followercolor, 16);
				plr->followercolor[16] = '\0';
			}
			else
			{
				strcpy(plr->follower, "None");
				strcpy(plr->followercolor, "Default");
				strcpy(plr->voice, "Default");
			}
		}

		plr->score = READUINT32(dp);
		plr->powerlevel = !kart ? READUINT16(dp) : 0;
		plr->kartspeed = READUINT8(dp);
		plr->kartweight = READUINT8(dp);
		plr->followitem = !kart ? READUINT32(dp) : MT_NULL;

		for (i = 0; i < MAXAVAILABILITY; i++)
		{
			plr->availabilities[i] = availabilities ? READUINT8(dp) : 0;
		}
	}

	// Sigh ... it's an empty demo. Again.
	if (*dp == DEMOMARKER)
		header->empty = true;

end:
	header->endofs = dp - startdp;
	return HEADER_OK;
}

static void G_FreeDemoHeader(demoheader_t *header)
{
	if (header->numfiles)
	{
		for (uint8_t i = 0; i < header->numfiles; i++)
			free(header->files[i].filename);
		free(header->files);
	}

	if (header->numcvars)
	{
		for (uint16_t i = 0; i < header->numcvars; i++)
		{
			if (header->cvars[i].name)
				free(header->cvars[i].name);
			free(header->cvars[i].value);
		}
		free(header->cvars);
	}

	if (header->numplayers)
		free(header->playerdata);

#ifdef PARANOIA
	memset(header, 0xff, sizeof(*header));
#endif
}

enum // legacy RA flags
{
	// 0x0009
	RAF_RINGS         = 1<<0,
	RAF_STACKING      = 1<<1,
	RAF_CHAINING      = 1<<2,
	RAF_SLIPDASH      = 1<<3,
	RAF_PURPLEDRIFT   = 1<<4,
	RAF_SLOPEBOOST    = 1<<5,
	// 0x000C
	RAF_AIRDROP       = 1<<6,
	RAF_BUMPDRIFT     = 1<<7,
	RAF_BUMPSPARK     = 2<<7,
	RAF_BS_RESET100   = 3<<7,
	RAF_BUMPSPARKMASK = 0x3<<7,
};

static strbuf_t *G_GetRecordPresetVersionForDemo(demoheader_t *header)
{
	if (header->demoversion >= 0x000D)
		return G_GetRecordPresetVersion(header->rapreset, header->rapresetversion);

	// oh dear... this demo predates softcoded presets.
	// we'll have to try and figure out the corresponding preset
	uint32_t test, raflags = header->raflags;

	if (raflags == 0)
		return G_GetRecordPresetVersion("kart", 1);

	test = RAF_STACKING|RAF_CHAINING|RAF_SLOPEBOOST;
	if (header->demoversion == 0x000C)
		test |= RAF_AIRDROP|RAF_BUMPSPARK;
	if (raflags == test)
		return G_GetRecordPresetVersion("tech", header->demoversion == 0x000C ? 2 : 1);

	test = RAF_RINGS|RAF_STACKING|RAF_CHAINING|RAF_SLIPDASH|RAF_PURPLEDRIFT|RAF_SLOPEBOOST;
	if (header->demoversion == 0x000C)
		test |= RAF_AIRDROP|RAF_BUMPDRIFT;
	if (raflags == test)
		return G_GetRecordPresetVersion("blankart", header->demoversion == 0x000C ? 2 : 1);

	// supporting custom raflags is a pain in the ass, so no thanks
	// if it really is necessary, just add another preset to the defaults...
	return NULL;
}

// and now, the rest of g_demo.c

dboolean G_CompatLevel(uint16_t level)
{
	if (demo.playback)
	{
		// Check gameplay differences for older ghosts
		return (demo.version <= level);
	}

	return false;
}

static mobj_t oldmetal, oldghost[MAXPLAYERS];

void G_SaveMetal(uint8_t **buffer)
{
	I_Assert(buffer != NULL && *buffer != NULL);

	WRITEUINT32(*buffer, metal_p - metalbuffer);
}

void G_LoadMetal(uint8_t **buffer)
{
	I_Assert(buffer != NULL && *buffer != NULL);

	G_DoPlayMetal();
	metal_p = metalbuffer + READUINT32(*buffer);
}

// Finds a skin with the closest stats if the expected skin doesn't exist.
static int32_t GetSkinNumClosestToStats(uint8_t kartspeed, uint8_t kartweight)
{
	int32_t i, closest_skin = 0;
	uint8_t closest_stats = UINT8_MAX, stat_diff;

	for (i = 0; i < numskins; i++)
	{
		stat_diff = abs(skins[i]->kartspeed - kartspeed) + abs(skins[i]->kartweight - kartweight);
		if (stat_diff < closest_stats)
		{
			closest_stats = stat_diff;
			closest_skin = i;
		}
	}

	return closest_skin;
}

static void FindClosestSkinForStats(uint32_t p, uint8_t kartspeed, uint8_t kartweight)
{
	int32_t closest_skin = GetSkinNumClosestToStats(kartspeed, kartweight);

	//CONS_Printf("Using %s instead...\n", skins[closest_skin].name);
	SetPlayerSkinByNum(p, closest_skin);
}

void G_LoadDemoCvars(void **p, void (*setvalue)(consvar_t *, const char *, dboolean))
{
	demoheader_t *header = *p;
	for (uint16_t i = 0; i < header->numcvars; i++)
	{
		if (header->cvars[i].name == NULL)
		{
			CONS_Alert(CONS_WARNING, "Netvar not found with netid %d\n", header->cvars[i].legacynetid);
			continue;
		}

		consvar_t *cvar = CV_FindVar(header->cvars[i].name);
		if (cvar)
			setvalue(cvar, header->cvars[i].value, header->cvars[i].stealth);
		else
			CONS_Alert(CONS_WARNING, "Netvar not found with name %s\n", header->cvars[i].name);
	}
}

void G_ReadDemoExtraData(void)
{
	int32_t p, i;

	if (leveltime > starttime)
	{
		rewind_t *rewind = CL_SaveRewindPoint(demobuf.p - demobuf.buffer);
		if (rewind)
		{
			memcpy(rewind->oldcmd, oldcmd, sizeof (oldcmd));
			memcpy(rewind->oldghost, oldghost, sizeof (oldghost));
		}
	}

	while (true)
	{
		extradata_t extra = {0};
		demobuf.p = G_ReadRawExtraData(&extra, demobuf.p, demo.version);
		p = extra.playernum;

		if (p == DW_END)
			break;

		if (p >= DW_EXTRASTUFF) switch (p)
		{
		case DW_RNG:
			for (i = 0; i < PRNUMCLASS; i++)
			{
				if (P_GetRandSeed(i) != extra.randseed[i])
				{
					P_SetRandSeed(i, extra.randseed[i]);

					if (demosynced)
						CONS_Alert(CONS_WARNING, M_GetText("Demo playback has desynced (RNG class \"%s\")!\n"), pr_class_name[i]);
					demosynced = false;
				}
			}
			continue;
		}

		// FIXME: i have no fucking clue if this is fine
		// but some replays had this set to whatever
		if (extra.playernum < MAXPLAYERS)
		{
			player_t *player = &players[p];

			if (extra.flags & DXD_JOINDATA)
			{
				if (!playeringame[extra.playernum])
				{
					G_AddPlayer(extra.playernum, extra.playernum);
				}

				player->bot = extra.joindata.bot;
				if (player->bot)
				{
					player->botvars.difficulty = extra.joindata.difficulty;
					player->botvars.diffincrease = extra.joindata.diffincrease; // needed to avoid having to duplicate logic
					player->botvars.rival = extra.joindata.rival;
				}
			}
			if (extra.flags & DXD_PLAYSTATE)
			{
				switch (extra.playstate) {
					case DXD_PST_PLAYING:
						if (player->spectator == true)
						{
							if (player->bot)
							{
								player->spectator = false;
							}
							else
							{
								player->pflags |= PF_WANTSTOJOIN;
							}
						}
						//CONS_Printf("player %s is despectating on tic %d\n", player_names[p], leveltime);
						break;

					case DXD_PST_SPECTATING:
						if (player->spectator)
						{
							player->pflags &= ~PF_WANTSTOJOIN;
						}
						else
						{
							if (player->mo)
							{
								P_DamageMobj(player->mo, NULL, NULL, 1, DMG_SPECTATOR);
							}
							P_SetPlayerSpectator(p);
						}

						break;

					case DXD_PST_LEFT:
						CL_RemovePlayer(p, 0);
						break;
				}

				G_ResetViews(false); // dont reset our freecam pls thx!

				// maybe these are necessary?
				K_CheckBumpers();
				P_CheckRacers();
			}
			if (extra.flags & DXD_NAME)
			{
				// Name
				strcpy(player_names[p], extra.playername);
			}
			if (extra.flags & DXD_SKIN)
			{
				// Skin
				SetPlayerSkin(p, extra.skinname);

				if (!fasticmp(skins[player->skin]->name, extra.skinname))
					FindClosestSkinForStats(p, extra.kartspeed, extra.kartweight);

				// Voice
				const int32_t vce = R_FindIDForVoice(skins[player->skin], extra.voicename);

				if (vce == MAXSKINVOICES)
				{
					// The above function didn't find a voice, so we'll just fall back
					// to the skin's voice.
					player->voice_id = 0;
				}
				else
				{
					player->voice_id = (uint16_t)vce;
				}

				if (!P_MobjWasRemoved(player->mo))
				{
					// This player's mobj exists. So, attach the voice to the mobj!
					player->mo->voice = &skins[player->skin]->voices[player->voice_id];
				}

				player->kartspeed = extra.kartspeed;
				player->kartweight = extra.kartweight;
			}
			if (extra.flags & DXD_COLOR)
			{
				// Color
				for (i = 0; i < numskincolors; i++)
					if (fasticmp(skincolors[i].name, extra.colorname)) // SRB2kart
					{
						player->skincolor = i;
						if (player->mo)
							player->mo->color = i;
						break;
					}
			}
			if (extra.flags & DXD_FOLLOWER)
			{
				// Set our follower
				K_SetFollowerByName(p, extra.followername);

				// Follower's color
				for (i = 0; i < numskincolors +2; i++)	// +2 because of Match and Opposite
					if (fasticmp(Followercolor_cons_t[i].strvalue, extra.followercolor))
					{
						player->followercolor = i;
						break;
					}
			}
			if (extra.flags & DXD_RESPAWN)
			{
				if (player->mo)
				{
					// Is this how this should work..?
					P_DamageMobj(player->mo, NULL, NULL, 1, DMG_INSTAKILL);
				}
			}
			if (extra.flags & DXD_WEAPONPREF)
			{
				uint8_t *bruh = &extra.weaponpref;
				WeaponPref_Parse(&bruh, p);
				player->driftmode = extra.driftmode;
				//CONS_Printf("weaponpref is %d for player %d\n", i, p);
			}
		}
	}

	if (!(demoflags & DF_GHOST) && *demobuf.p == DEMOMARKER)
	{
		// end of demo data stream
		G_CheckDemoStatus();
		return;
	}
}

void G_WriteDemoExtraData(void)
{
	int32_t i, j;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (demo_extradata[i])
		{
			WRITEUINT8(demobuf.p, i);
			WRITEUINT8(demobuf.p, demo_extradata[i]);

			if (demo_extradata[i] & DXD_JOINDATA)
			{
				for (j = 0; j < MAXAVAILABILITY; j++)
				{
					WRITEUINT8(demobuf.p, players[i].availabilities[i]);
				}

				WRITEUINT8(demobuf.p, (uint8_t)players[i].bot);
				if (players[i].bot)
				{
					WRITEUINT8(demobuf.p, players[i].botvars.difficulty);
					WRITEUINT8(demobuf.p, players[i].botvars.diffincrease); // needed to avoid having to duplicate logic
					WRITEUINT8(demobuf.p, (uint8_t)players[i].botvars.rival);
				}
			}
			if (demo_extradata[i] & DXD_PLAYSTATE)
			{
				uint8_t pst = DXD_PST_PLAYING;

				demo_writerng = 1;

				if (!playeringame[i])
				{
					pst = DXD_PST_LEFT;
				}
				else if (
					players[i].spectator &&
					!(players[i].pflags & PF_WANTSTOJOIN) // <= fuck you specifically
				)
				{
					pst = DXD_PST_SPECTATING;
				}

				WRITEUINT8(demobuf.p, pst);
			}
			if (demo_extradata[i] & DXD_NAME)
			{
				// Name
				WRITESTRINGL(demobuf.p, player_names[i], 21+1);
			}
			if (demo_extradata[i] & DXD_SKIN)
			{
				// Skin
				WRITESTRINGL(demobuf.p, skins[players[i].skin]->name, 16+1);

				// Voice
				WRITESTRINGL(demobuf.p, skins[players[i].skin]->voices[players[i].voice_id].name, 32+1);

				// Stats
				WRITEUINT8(demobuf.p, players[i].kartspeed);
				WRITEUINT8(demobuf.p, players[i].kartweight);

			}
			if (demo_extradata[i] & DXD_COLOR)
			{
				// Color
				WRITESTRINGL(demobuf.p, skincolors[players[i].skincolor].name, 16+1);
			}
			if (demo_extradata[i] & DXD_FOLLOWER)
			{
				// write follower
				if (players[i].followerskin == -1)
					WRITESTRINGL(demobuf.p, "None", 16+1);
				else
					WRITESTRINGL(demobuf.p, followers[players[i].followerskin].skinname, 16+1);

				// write follower color
				WRITESTRINGL(demobuf.p, Followercolor_cons_t[(uint16_t)(players[i].followercolor+2)].strvalue, 16+1);

			}
			//if (demo_extradata[i] & DXD_RESPAWN) has no extra data
			if (demo_extradata[i] & DXD_WEAPONPREF)
			{
				WeaponPref_Save(&demobuf.p, i);
				WRITEUINT8(demobuf.p, players[i].driftmode);
			}
		}

		demo_extradata[i] = 0;
	}

	// May not be necessary, but might as well play it safe...
	if ((leveltime & 255) == 128)
		demo_writerng = 1;

	{
		static uint8_t timeout = 0;

		if (timeout) timeout--;

		if (demo_writerng == 1 || (demo_writerng == 2 && timeout == 0))
		{
			demo_writerng = 0;
			timeout = 16;
			WRITEUINT8(demobuf.p, DW_RNG);

			//WRITEUINT32(demobuf.p, P_GetRandSeed());
			for (i = 0; i < PRNUMCLASS; i++)
			{
				WRITEUINT32(demobuf.p, P_GetRandSeed(i));
			}
		}
	}

	WRITEUINT8(demobuf.p, DW_END);
}

void G_ReadDemoTiccmd(ticcmd_t *cmd, int32_t playernum)
{
	if (!demobuf.p || !demo.deferstart)
		return;

	demobuf.p = G_ReadZipTic(&oldcmd[playernum], demobuf.p, demo.version);
	G_CopyTiccmd(cmd, &oldcmd[playernum], 1);

	if (!(demoflags & DF_GHOST) && *demobuf.p == DEMOMARKER)
	{
		// end of demo data stream
		G_CheckDemoStatus();
		return;
	}
}

void G_WriteDemoTiccmd(ticcmd_t *cmd, int32_t playernum)
{
	uint16_t ziptic = 0;
	uint8_t *ziptic_p;
	//(void)playernum;

	if (!demobuf.p)
		return;

	ziptic_p = demobuf.p; // the ziptic, written at the end of this function
	demobuf.p += 2;

	if (cmd->forwardmove != oldcmd[playernum].forwardmove)
	{
		WRITEINT8(demobuf.p,cmd->forwardmove);
		oldcmd[playernum].forwardmove = cmd->forwardmove;
		ziptic |= ZT_FWD;
	}

	if (cmd->sidemove != oldcmd[playernum].sidemove)
	{
		WRITEINT8(demobuf.p,cmd->sidemove);
		oldcmd[playernum].sidemove = cmd->sidemove;
		ziptic |= ZT_SIDE;
	}

	if (cmd->turning != oldcmd[playernum].turning)
	{
		WRITEINT16(demobuf.p,cmd->turning);
		oldcmd[playernum].turning = cmd->turning;
		ziptic |= ZT_TURNING;
	}

	if (cmd->angle != oldcmd[playernum].angle)
	{
		WRITEINT16(demobuf.p,cmd->angle);
		oldcmd[playernum].angle = cmd->angle;
		ziptic |= ZT_ANGLE;
	}

	if (cmd->throwdir != oldcmd[playernum].throwdir)
	{
		WRITEINT16(demobuf.p,cmd->throwdir);
		oldcmd[playernum].throwdir = cmd->throwdir;
		ziptic |= ZT_THROWDIR;
	}

	if (cmd->buttons != oldcmd[playernum].buttons)
	{
		WRITEUINT16(demobuf.p,cmd->buttons);
		oldcmd[playernum].buttons = cmd->buttons;
		ziptic |= ZT_BUTTONS;
	}

	if (cmd->aiming != oldcmd[playernum].aiming)
	{
		WRITEINT16(demobuf.p,cmd->aiming);
		oldcmd[playernum].aiming = cmd->aiming;
		ziptic |= ZT_AIMING;
	}

	if (cmd->latency != oldcmd[playernum].latency)
	{
		WRITEUINT8(demobuf.p,cmd->latency);
		oldcmd[playernum].latency = cmd->latency;
		ziptic |= ZT_LATENCY;
	}

	if (cmd->flags != oldcmd[playernum].flags)
	{
		WRITEUINT8(demobuf.p,cmd->flags);
		oldcmd[playernum].flags = cmd->flags;
		ziptic |= ZT_FLAGS;
	}

	WRITEUINT16(ziptic_p, ziptic);

	// attention here for the ticcmd size!
	// latest demos with mouse aiming byte in ticcmd
	if (!(demoflags & DF_GHOST) && ziptic_p > demobuf.end - 9)
	{
		G_CheckDemoStatus(); // no more space
		return;
	}
}

void G_GhostAddFlip(int32_t playernum)
{
	if (!metalrecording && (!demo.recording || !(demoflags & DF_GHOST)))
		return;
	ghostext[playernum].flags |= EZT_FLIP;
}

void G_GhostAddColor(int32_t playernum, ghostcolor_t color)
{
	if (!demo.recording || !(demoflags & DF_GHOST))
		return;
	if (ghostext[playernum].lastcolor == (uint16_t)color)
	{
		ghostext[playernum].flags &= ~EZT_COLOR;
		return;
	}
	ghostext[playernum].flags |= EZT_COLOR;
	ghostext[playernum].color = (uint16_t)color;
}

void G_GhostAddScale(int32_t playernum, fixed_t scale)
{
	if (!metalrecording && (!demo.recording || !(demoflags & DF_GHOST)))
		return;
	if (ghostext[playernum].lastscale == scale)
	{
		ghostext[playernum].flags &= ~EZT_SCALE;
		return;
	}
	ghostext[playernum].flags |= EZT_SCALE;
	ghostext[playernum].scale = scale;
}

void G_GhostAddHit(int32_t playernum, mobj_t *victim)
{
	if (!demo.recording || !(demoflags & DF_GHOST))
		return;
	ghostext[playernum].flags |= EZT_HIT;
	ghostext[playernum].hits++;
	Z_Realloc(ghostext[playernum].hitlist, ghostext[playernum].hits * sizeof(mobj_t *), PU_STATIC, &ghostext[playernum].hitlist);
	P_SetTarget(ghostext[playernum].hitlist + (ghostext[playernum].hits-1), victim);
}

void G_WriteAllGhostTics(void)
{
	dboolean toobig = false;
	int32_t i, counter = leveltime;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!players[i].mo)
			continue;

		counter++;

		if (multiplayer && ((counter % cv_netdemosyncquality.value) != 0)) // Only write 1 in this many ghost datas per tic to cut down on multiplayer replay size.
			continue;

		WRITEUINT8(demobuf.p, i);
		G_WriteGhostTic(players[i].mo, i);

		// attention here for the ticcmd size!
		// latest demos with mouse aiming byte in ticcmd
		if (demobuf.p >= demobuf.end - (13 + 9 + 9))
		{
			toobig = true;
			break;
		}
	}
	WRITEUINT8(demobuf.p, 0xFF);

	if (toobig)
	{
		G_CheckDemoStatus(); // no more space
		return;
	}
}

void G_WriteGhostTic(mobj_t *ghost, int32_t playernum)
{
	char ziptic = 0;
	uint8_t *ziptic_p;
	uint32_t i;

	if (!demobuf.p)
		return;
	if (!(demoflags & DF_GHOST))
		return; // No ghost data to write.

	ziptic_p = demobuf.p++; // the ziptic, written at the end of this function

#define MAXMOM (0xFFFF<<8)

	// GZT_XYZ is only useful if you've moved 256 FRACUNITS or more in a single tic.
	if (abs(ghost->x-oldghost[playernum].x) > MAXMOM
	|| abs(ghost->y-oldghost[playernum].y) > MAXMOM
	|| abs(ghost->z-oldghost[playernum].z) > MAXMOM
	|| ((uint8_t)(leveltime & 255) > 0 && (uint8_t)(leveltime & 255) <= (uint8_t)cv_netdemosyncquality.value)) // Hack to enable slightly nicer resyncing
	{
		oldghost[playernum].x = ghost->x;
		oldghost[playernum].y = ghost->y;
		oldghost[playernum].z = ghost->z;
		ziptic |= GZT_XYZ;
		WRITEFIXED(demobuf.p,oldghost[playernum].x);
		WRITEFIXED(demobuf.p,oldghost[playernum].y);
		WRITEFIXED(demobuf.p,oldghost[playernum].z);
	}
	else
	{
		// For moving normally:
		fixed_t momx = ghost->x-oldghost[playernum].x;
		fixed_t momy = ghost->y-oldghost[playernum].y;

		if (momx != oldghost[playernum].momx
		|| momy != oldghost[playernum].momy)
		{
			oldghost[playernum].momx = momx;
			oldghost[playernum].momy = momy;
			ziptic |= GZT_MOMXY;
			WRITEFIXED(demobuf.p,momx);
			WRITEFIXED(demobuf.p,momy);
		}

		momx = ghost->z-oldghost[playernum].z;
		if (momx != oldghost[playernum].momz)
		{
			oldghost[playernum].momz = momx;
			ziptic |= GZT_MOMZ;
			WRITEFIXED(demobuf.p,momx);
		}

		// This SHOULD set oldghost.x/y/z to match ghost->x/y/z
		oldghost[playernum].x += oldghost[playernum].momx;
		oldghost[playernum].y += oldghost[playernum].momy;
		oldghost[playernum].z +=oldghost[playernum].momz;
	}

#undef MAXMOM

	// Only store the 8 most relevant bits of angle
	// because exact values aren't too easy to discern to begin with when only 8 angles have different sprites
	// and it does not affect this mode of movement at all anyway.
	if (ghost->player && ghost->player->drawangle>>24 != oldghost[playernum].angle)
	{
		oldghost[playernum].angle = ghost->player->drawangle>>24;
		ziptic |= GZT_ANGLE;
		WRITEUINT8(demobuf.p,oldghost[playernum].angle);
	}

	// Store the sprite frame.
	if ((ghost->frame & FF_FRAMEMASK) != oldghost[playernum].frame)
	{
		oldghost[playernum].frame = (ghost->frame & FF_FRAMEMASK);
		ziptic |= GZT_FRAME;
		WRITEUINT8(demobuf.p,oldghost[playernum].frame);
	}

	if (ghost->sprite == SPR_PLAY
	&& ghost->sprite2 != oldghost[playernum].sprite2)
	{
		oldghost[playernum].sprite2 = ghost->sprite2;
		ziptic |= GZT_SPR2;
		WRITEUINT8(demobuf.p,oldghost[playernum].sprite2);
	}

	// Check for sprite set changes
	if (ghost->sprite != oldghost[playernum].sprite)
	{
		oldghost[playernum].sprite = ghost->sprite;
		ghostext[playernum].flags |= EZT_SPRITE;
	}

	if (ghost->player && (
			ghostext[playernum].kartitem != ghost->player->itemtype ||
			ghostext[playernum].kartamount != ghost->player->itemamount ||
			ghostext[playernum].kartbumpers != ghost->player->bumper
		))
	{
		ghostext[playernum].flags |= EZT_KART;
		ghostext[playernum].kartitem = ghost->player->itemtype;
		ghostext[playernum].kartamount = ghost->player->itemamount;
		ghostext[playernum].kartbumpers = ghost->player->bumper;
	}

	if (ghostext[playernum].flags)
	{
		ziptic |= GZT_EXTRA;

		if (ghostext[playernum].color == ghostext[playernum].lastcolor)
			ghostext[playernum].flags &= ~EZT_COLOR;
		if (ghostext[playernum].scale == ghostext[playernum].lastscale)
			ghostext[playernum].flags &= ~EZT_SCALE;

		WRITEUINT8(demobuf.p,ghostext[playernum].flags);
		if (ghostext[playernum].flags & EZT_COLOR)
		{
			WRITEUINT16(demobuf.p,ghostext[playernum].color);
			ghostext[playernum].lastcolor = ghostext[playernum].color;
		}
		if (ghostext[playernum].flags & EZT_SCALE)
		{
			WRITEFIXED(demobuf.p,ghostext[playernum].scale);
			ghostext[playernum].lastscale = ghostext[playernum].scale;
		}
		if (ghostext[playernum].flags & EZT_HIT)
		{
			WRITEUINT16(demobuf.p,ghostext[playernum].hits);
			for (i = 0; i < ghostext[playernum].hits; i++)
			{
				mobj_t *mo = ghostext[playernum].hitlist[i];
				//WRITEUINT32(demobuf.p,UINT32_MAX); // reserved for some method of determining exactly which mobj this is. (mobjnum doesn't work here.)
				WRITEUINT32(demobuf.p,mo->type);
				WRITEUINT16(demobuf.p,(uint16_t)mo->health);
				WRITEFIXED(demobuf.p,mo->x);
				WRITEFIXED(demobuf.p,mo->y);
				WRITEFIXED(demobuf.p,mo->z);
				WRITEANGLE(demobuf.p,mo->angle);
				P_SetTarget(ghostext[playernum].hitlist+i, NULL);
			}
			Z_Free(ghostext[playernum].hitlist);
			ghostext[playernum].hits = 0;
		}
		if (ghostext[playernum].flags & EZT_SPRITE)
			WRITEUINT16(demobuf.p,oldghost[playernum].sprite);
		if (ghostext[playernum].flags & EZT_KART)
		{
			WRITEINT32(demobuf.p, ghostext[playernum].kartitem);
			WRITEINT32(demobuf.p, ghostext[playernum].kartamount);
			WRITEINT32(demobuf.p, ghostext[playernum].kartbumpers);
		}

		ghostext[playernum].flags = 0;
	}

	if (ghost->player && ghost->player->followmobj&& !(ghost->player->followmobj->sprite == SPR_NULL || (ghost->player->followmobj->renderflags & RF_DONTDRAW) == RF_DONTDRAW)) // bloats tails runs but what can ya do
	{
		fixed_t temp;
		uint8_t *followtic_p = demobuf.p++;
		uint8_t followtic = 0;

		ziptic |= GZT_FOLLOW;

		if (ghost->player->followmobj->skin)
			followtic |= FZT_SKIN;

		if (!(oldghost[playernum].flags2 & MF2_AMBUSH))
		{
			followtic |= FZT_SPAWNED;
			WRITEINT16(demobuf.p,ghost->player->followmobj->info->height>>FRACBITS);
			if (ghost->player->followmobj->flags2 & MF2_LINKDRAW)
				followtic |= FZT_LINKDRAW;
			if (ghost->player->followmobj->colorized)
				followtic |= FZT_COLORIZED;
			if (followtic & FZT_SKIN)
				WRITEUINT16(demobuf.p,(uint16_t)(((skin_t *)(ghost->player->followmobj->skin))->skinnum));
			oldghost[playernum].flags2 |= MF2_AMBUSH;
		}

		if (ghost->player->followmobj->scale != ghost->scale)
		{
			followtic |= FZT_SCALE;
			WRITEFIXED(demobuf.p,ghost->player->followmobj->scale);
		}

		temp = ghost->player->followmobj->x-ghost->x;
		WRITEFIXED(demobuf.p,temp);
		temp = ghost->player->followmobj->y-ghost->y;
		WRITEFIXED(demobuf.p,temp);
		temp = ghost->player->followmobj->z-ghost->z;
		WRITEFIXED(demobuf.p,temp);
		if (followtic & FZT_SKIN)
			WRITEUINT8(demobuf.p,ghost->player->followmobj->sprite2);
		WRITEUINT16(demobuf.p,ghost->player->followmobj->sprite);
		WRITEUINT8(demobuf.p,(ghost->player->followmobj->frame & FF_FRAMEMASK));
		WRITEUINT16(demobuf.p,ghost->player->followmobj->color);

		*followtic_p = followtic;
	}
	else
		oldghost[playernum].flags2 &= ~MF2_AMBUSH;

	*ziptic_p = ziptic;
}

void G_ConsAllGhostTics(void)
{
	uint8_t p;

	if (!demobuf.p || !demo.deferstart)
		return;

	p = READUINT8(demobuf.p);

	while (p != 0xFF)
	{
		G_ConsGhostTic(p);
		p = READUINT8(demobuf.p);
	}

	if (*demobuf.p == DEMOMARKER)
	{
		// end of demo data stream
		G_CheckDemoStatus();
		return;
	}
}

// Uses ghost data to do consistency checks on your position.
// This fixes desynchronising demos when fighting eggman.
void G_ConsGhostTic(int32_t playernum)
{
	int32_t px,py,pz,gx,gy,gz;
	mobj_t *testmo;
	uint32_t syncleeway;

	if (!(demoflags & DF_GHOST))
		return; // No ghost data to use.

	testmo = players[playernum].mo;

	ziptic_t zt;
	demobuf.p = G_ReadExtraZipTic(&zt, demobuf.p, demo.version, testmo);

	if (!cv_resyncdemo.value)
		goto skip;

	// Grab ghost data.
	if (zt.flags & GZT_XYZ)
	{
		oldghost[playernum].x = zt.x;
		oldghost[playernum].y = zt.y;
		oldghost[playernum].z = zt.z;
		syncleeway = 0;
	}
	else
	{
		if (zt.flags & GZT_MOMXY)
		{
			oldghost[playernum].momx = zt.x;
			oldghost[playernum].momy = zt.y;
		}
		if (zt.flags & GZT_MOMZ)
			oldghost[playernum].momz = zt.z;
		oldghost[playernum].x += oldghost[playernum].momx;
		oldghost[playernum].y += oldghost[playernum].momy;
		oldghost[playernum].z += oldghost[playernum].momz;
		syncleeway = FRACUNIT;
	}

	if (zt.flags & GZT_EXTRA)
	{ // But wait, there's more!
		if (zt.extraflags & EZT_HIT)
		{ // Resync mob damage.
			for (uint16_t i = 0; i < zt.damagecount; i++)
			{
				zipticdamage_t *damage = &zt.damage[i];
				mobj_t *mobj = NULL;
				thinker_t *th = NULL;
				for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
				{
					if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
						continue;
					mobj = (mobj_t *)th;
					if (mobj->type == damage->type && mobj->x == damage->x && mobj->y == damage->y && mobj->z == damage->z)
						break;
				}
				if (th != &thlist[THINK_MOBJ] && mobj->health != damage->health) // Wasn't damaged?! This is desync! Fix it!
				{
					if (demosynced)
						CONS_Alert(CONS_WARNING, M_GetText("Demo playback has desynced (health)!\n"));
					demosynced = false;
					P_DamageMobj(mobj, players[0].mo, players[0].mo, 1, DMG_NORMAL);
				}
			}
		}
		if (zt.extraflags & EZT_KART)
		{
			ghostext[playernum].kartitem = zt.kartitem;
			ghostext[playernum].kartamount = zt.kartamount;
			ghostext[playernum].kartbumpers = zt.kartbumpers;
		}
	}

	if (testmo)
	{
		// Re-synchronise
		px = testmo->x;
		py = testmo->y;
		pz = testmo->z;
		gx = oldghost[playernum].x;
		gy = oldghost[playernum].y;
		gz = oldghost[playernum].z;

		if ((uint32_t)abs(px-gx) > syncleeway || (uint32_t)abs(py-gy) > syncleeway || (uint32_t)abs(pz-gz) > syncleeway)
		{
			ghostext[playernum].desyncframes++;

			if (ghostext[playernum].desyncframes >= 2)
			{
				if (demosynced)
					CONS_Alert(CONS_WARNING, "Demo playback has desynced (player %s)!\n", player_names[playernum]);
				demosynced = false;

				P_UnsetThingPosition(testmo);
				testmo->x = oldghost[playernum].x;
				testmo->y = oldghost[playernum].y;
				P_SetThingPosition(testmo);
				testmo->z = oldghost[playernum].z;

				if (abs(testmo->z - testmo->floorz) < 4*FRACUNIT)
					testmo->z = testmo->floorz; // Sync players to the ground when they're likely supposed to be there...

				ghostext[playernum].desyncframes = 2;
			}
		}
		else
			ghostext[playernum].desyncframes = 0;

		if (!G_CompatLevel(0x0001) &&
		   (players[playernum].itemtype != ghostext[playernum].kartitem
		 || players[playernum].itemamount != ghostext[playernum].kartamount
		 || players[playernum].bumper != ghostext[playernum].kartbumpers))
		{
			if (demosynced)
				CONS_Alert(CONS_WARNING, M_GetText("Demo playback has desynced (item/bumpers)!\n"));
			demosynced = false;

			players[playernum].itemtype = ghostext[playernum].kartitem;
			players[playernum].itemamount = ghostext[playernum].kartamount;
			players[playernum].bumper = ghostext[playernum].kartbumpers;
		}
	}

skip:
	G_FreeExtraZipTic(&zt);
	if (*demobuf.p == DEMOMARKER)
	{
		// end of demo data stream
		G_CheckDemoStatus();
		return;
	}
}

void G_GhostTicker(void)
{
	demoghost *g,*p;
	for (g = ghosts, p = NULL; g; g = g->next)
	{
		// Skip normal demo data.
		ticcmd_t dummy;
		ziptic_t zt;

		if (g->done)
		{
			continue;
		}

		if (g->version == 0x0001)
			goto skipextradata;

		while (true) // Get rid of extradata stuff
		{
			extradata_t extra = {0};
			g->p = G_ReadRawExtraData(&extra, g->p, g->version);

			if (extra.playernum < MAXPLAYERS)
			{
				// We want to skip *any* player extradata because some demos have extradata for bogus players,
				// but if there is tic data later for those players *then* we'll consider it invalid.

				if (extra.flags & DXD_JOINDATA && extra.joindata.bot)
					I_Error("Ghost is not a record attack ghost (bot JOINDATA)");
#ifdef DEVELOP
				if (extra.flags & DXD_PLAYSTATE && extra.playstate != DXD_PST_PLAYING)
					CONS_Alert(CONS_WARNING, "Ghost demo has non-playing playstate for player %d\n", extra.playernum);
#endif
			}
			else if (extra.playernum == DW_END)
				break;
			else if (extra.playernum < DW_EXTRASTUFF)
				I_Error("Ghost is not a record attack ghost DXD (ziptic = %u)", extra.playernum); //@TODO lmao don't blow up like this
		}

	skipextradata:
		g->p = G_ReadZipTic(&dummy, g->p, g->version);
		if (g->version == 0x0001)
			goto skipmultiplayer;

		// Grab ghost data.
		uint8_t pnum = READUINT8(g->p);

		if (pnum == 0xFF)
			goto skippedghosttic; // Didn't write ghost info this frame
		else if (pnum != 0)
			I_Error("Ghost is not a record attack ghost ZIPTIC"); //@TODO lmao don't blow up like this

	skipmultiplayer:
		g->p = G_ReadExtraZipTic(&zt, g->p, g->version, g->mo);

		if (zt.flags & GZT_XYZ)
		{
			g->oldmo.x = zt.x;
			g->oldmo.y = zt.y;
			g->oldmo.z = zt.z;
		}
		else
		{
			if (zt.flags & GZT_MOMXY)
			{
				g->oldmo.momx = zt.x;
				g->oldmo.momy = zt.y;
			}
			if (zt.flags & GZT_MOMZ)
				g->oldmo.momz = zt.z;
			g->oldmo.x += g->oldmo.momx;
			g->oldmo.y += g->oldmo.momy;
			g->oldmo.z += g->oldmo.momz;
		}
		if (zt.flags & GZT_ANGLE)
			g->oldmo.angle = zt.angle;
		if (zt.flags & GZT_FRAME)
			g->oldmo.frame = zt.frame;
		if (zt.flags & GZT_SPR2)
			g->oldmo.sprite2 = zt.spr2;

		// Update ghost
		P_UnsetThingPosition(g->mo);
		g->mo->x = g->oldmo.x;
		g->mo->y = g->oldmo.y;
		g->mo->z = g->oldmo.z;
		P_SetThingPosition(g->mo);
		g->mo->angle = g->oldmo.angle;
		g->mo->frame = g->oldmo.frame | tr_trans30<<FF_TRANSSHIFT;
		if (g->fadein)
		{
			g->mo->frame += (((--g->fadein)/6)<<FF_TRANSSHIFT); // this calc never exceeds 9 unless g->fadein is bad, and it's only set once, so...
			g->mo->renderflags &= ~RF_DONTDRAW;
		}
		g->mo->sprite2 = g->oldmo.sprite2;

		if (zt.flags & GZT_EXTRA)
		{ // But wait, there's more!
			if (zt.extraflags & EZT_COLOR)
			{
				g->color = zt.color;
				switch(g->color)
				{
				default:
				case GHC_RETURNSKIN:
					R_ApplySkin(g->mo, g->oldmo.skin);
					/* FALLTHRU */
				case GHC_NORMAL: // Go back to skin color
					g->mo->color = g->oldmo.color;
					break;
				// Handled below
				case GHC_SUPER:
				case GHC_INVINCIBLE:
					break;
				case GHC_FIREFLOWER: // Fireflower
					g->mo->color = SKINCOLOR_WHITE;
					break;
				}
			}
			if (zt.extraflags & EZT_FLIP)
				g->mo->eflags ^= MFE_VERTICALFLIP;
			if (zt.extraflags & EZT_SCALE)
			{
				g->mo->destscale = zt.scale;
				if (g->mo->destscale != g->mo->scale)
					P_SetScale(g->mo, g->mo->destscale);
			}
			if (zt.extraflags & EZT_HIT)
			{ // Spawn hit poofs for killing things!
				for (uint16_t i = 0; i < zt.damagecount; i++)
				{
					zipticdamage_t *damage = &zt.damage[i];
					// MAP15S03 has a broken EZT_HIT at leveltime 3455 which is all 0xff... so bounds check damage->type
					if ((damage->type < nummobjtypes && !(mobjinfo[damage->type].flags & (MF_SHOOTABLE|MF_ENEMY|MF_MONITOR)))
					|| damage->health != 0 || i >= 4) // only spawn for the first 4 hits per frame, to prevent ghosts from splode-spamming too bad.
						continue;
					mobj_t *poof = P_SpawnMobj(damage->x, damage->y, damage->z, MT_GHOST);
					poof->angle = damage->angle;
					poof->flags = MF_NOBLOCKMAP|MF_NOCLIP|MF_NOCLIPHEIGHT|MF_NOGRAVITY; // make an ATTEMPT to curb crazy SOCs fucking stuff up...
					poof->health = 0;
					P_SetMobjStateNF(poof, S_XPLD1);
				}
			}
			if (zt.extraflags & EZT_SPRITE)
				g->mo->sprite = zt.sprite;
		}

		mobj_t *fmo = g->mo->tracer;
		if (zt.flags & GZT_FOLLOW)
		{ // Even more...
			if (zt.followflags & FZT_SPAWNED)
			{
				if (fmo)
					P_RemoveMobj(fmo);
				P_SetTarget(&fmo, P_SpawnMobjFromMobj(g->mo, 0, 0, 0, MT_GHOST));
				P_SetTarget(&fmo->tracer, g->mo);
				fmo->tics = -1;
				fmo->height = FixedMul(fmo->scale, zt.follow.height);

				if (zt.followflags & FZT_LINKDRAW)
					fmo->flags2 |= MF2_LINKDRAW;

				if (zt.followflags & FZT_COLORIZED)
					fmo->colorized = true;

				if (zt.followflags & FZT_SKIN)
					R_ApplySkin(fmo, skins[zt.follow.skin]);
			}
			if (fmo)
			{
				if (zt.followflags & FZT_SCALE)
					fmo->destscale = zt.follow.scale;
				else
					fmo->destscale = g->mo->destscale;
				if (fmo->destscale != fmo->scale)
					P_SetScale(fmo, fmo->destscale);

				P_UnsetThingPosition(fmo);
				fmo->x = g->mo->x + zt.follow.xofs;
				fmo->y = g->mo->y + zt.follow.yofs;
				fmo->z = g->mo->z + zt.follow.zofs;
				P_SetThingPosition(fmo);
				if (zt.followflags & FZT_SKIN)
					fmo->sprite2 = zt.follow.sprite2;
				else
					fmo->sprite2 = 0;
				fmo->sprite = zt.follow.sprite;
				fmo->frame = zt.follow.frame | (g->mo->frame & FF_TRANSMASK);
				fmo->angle = g->mo->angle;
				fmo->color = zt.follow.color;

				if (!(zt.followflags & FZT_SPAWNED))
				{
					if (zt.extraflags & EZT_FLIP)
					{
						fmo->flags2 ^= MF2_OBJECTFLIP;
						fmo->eflags ^= MFE_VERTICALFLIP;
					}
				}
			}
		}
		else if (fmo)
		{
			P_RemoveMobj(fmo);
			P_SetTarget(&fmo, NULL);
		}

		G_FreeExtraZipTic(&zt);

skippedghosttic:
		// Tick ghost colors (Super and Mario Invincibility flashing)
		switch(g->color)
		{
		case GHC_SUPER: // Super (P_DoSuperStuff)
			if (g->mo->skin)
			{
				skin_t *skin = (skin_t *)g->mo->skin;
				g->mo->color = skin->supercolor;
			}
			else
				g->mo->color = SKINCOLOR_SUPER1;
			g->mo->color += abs( ( (signed)( (unsigned)leveltime >> 1 ) % 9) - 4);
			break;
		case GHC_INVINCIBLE: // Mario invincibility (P_CheckInvincibilityTimer)
			g->mo->color = K_RainbowColor(leveltime); // Passes through all saturated colours
			break;
		default:
			break;
		}

		if (g->version != 0x0001 && READUINT8(g->p) != 0xFF) // Make sure there isn't other ghost data here.
			I_Error("Ghost is not a record attack ghost GHOSTEND"); //@TODO lmao don't blow up like this

		// Demo ends after ghost data.
		if (*g->p == DEMOMARKER)
		{
			g->mo->momx = g->mo->momy = g->mo->momz = 0;
#if 1 // freeze frame (maybe more useful for time attackers)
			g->mo->colorized = true;
			if (fmo)
				fmo->colorized = true;
#else // dissapearing act
			g->mo->fuse = TICRATE;
			if (follow)
				follow->fuse = TICRATE;
#endif
			g->done = true;
			if (p)
			{
				p->next = g->next;
			}
			continue;
		}

		p = g;
#undef follow
	}
}

// Demo rewinding functions
typedef struct rewindinfo_s {
	tic_t leveltime;

	struct {
		dboolean ingame;
		player_t player;
		mobj_t mobj;
	} playerinfo[MAXPLAYERS];

	struct rewindinfo_s *prev;
} rewindinfo_t;

static tic_t currentrewindnum;
static rewindinfo_t *rewindhead = NULL; // Reverse chronological order

void G_InitDemoRewind(void)
{
	CL_ClearRewinds();

	while (rewindhead)
	{
		rewindinfo_t *p = rewindhead->prev;
		Z_Free(rewindhead);
		rewindhead = p;
	}

	currentrewindnum = 0;
}

void G_StoreRewindInfo(void)
{
	static uint8_t timetolog = 8;
	rewindinfo_t *info;
	size_t i;

	if (timetolog-- > 0)
		return;
	timetolog = 8;

	info = Z_Calloc(sizeof(rewindinfo_t), PU_STATIC, NULL);

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
		{
			info->playerinfo[i].ingame = false;
			continue;
		}

		info->playerinfo[i].ingame = true;
		memcpy(&info->playerinfo[i].player, &players[i], sizeof(player_t));
		if (players[i].mo)
			memcpy(&info->playerinfo[i].mobj, players[i].mo, sizeof(mobj_t));
	}

	info->leveltime = leveltime;
	info->prev = rewindhead;
	rewindhead = info;
}

void G_PreviewRewind(tic_t previewtime)
{
	int8_t i;
	//size_t j;
	fixed_t tweenvalue = 0;
	rewindinfo_t *info = rewindhead, *next_info = rewindhead;

	if (!info)
		return;

	while (info->leveltime > previewtime && info->prev)
	{
		next_info = info;
		info = info->prev;
	}
	if (info != next_info)
		tweenvalue = FixedDiv(previewtime - info->leveltime, next_info->leveltime - info->leveltime);


	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
		{
			if (info->playerinfo[i].player.mo)
			{
				//@TODO spawn temp object to act as a player display
			}

			continue;
		}

		if (!info->playerinfo[i].ingame || !info->playerinfo[i].player.mo)
		{
			if (players[i].mo)
				players[i].mo->renderflags |= RF_DONTDRAW;

			continue;
		}

		if (!players[i].mo)
			continue; //@TODO spawn temp object to act as a player display

		players[i].mo->renderflags &= ~RF_DONTDRAW;

		P_UnsetThingPosition(players[i].mo);
#define TWEEN(pr) info->playerinfo[i].mobj.pr + FixedMul((int32_t) (next_info->playerinfo[i].mobj.pr - info->playerinfo[i].mobj.pr), tweenvalue)
		players[i].mo->x = TWEEN(x);
		players[i].mo->y = TWEEN(y);
		players[i].mo->z = TWEEN(z);
		players[i].mo->angle = TWEEN(angle);
#undef TWEEN
		P_SetThingPosition(players[i].mo);

		players[i].drawangle = info->playerinfo[i].player.drawangle + FixedMul((int32_t) (next_info->playerinfo[i].player.drawangle - info->playerinfo[i].player.drawangle), tweenvalue);

		players[i].mo->sprite = info->playerinfo[i].mobj.sprite;
		players[i].mo->sprite2 = info->playerinfo[i].mobj.sprite2;
		players[i].mo->frame = info->playerinfo[i].mobj.frame;

		players[i].realtime = info->playerinfo[i].player.realtime;
		// Genuinely CANNOT be fucked. I can redo lua and I can redo netsaves but I draw the line at this abysmal hack.
		/*for (j = 0; j < NUMKARTSTUFF; j++)
			players[i].kartstuff[j] = info->playerinfo[i].player.kartstuff[j];*/
	}

	for (i = splitscreen; i >= 0; i--)
		P_ResetCamera(&players[displayplayers[i]], &camera[i]);
}

void G_ConfirmRewind(tic_t rewindtime)
{
	int8_t i;
	tic_t j;
	dboolean oldsounddisabled = sound_disabled;

	int32_t olddp1 = displayplayers[0], olddp2 = displayplayers[1], olddp3 = displayplayers[2], olddp4 = displayplayers[3];
	uint8_t oldss = splitscreen;

	CV_StealthSetValue(&cv_renderview, 0);

	if (rewindtime <= starttime)
	{
		demo.rewinding = true; // this doesn't APPEAR to cause any misery, and it allows us to prevent running all the wipes again
		G_DoPlayDemo(NULL); // Restart the current demo
	}
	else
	{
		rewind_t *rewind;
		sound_disabled = true; // Prevent sound spam
		demo.rewinding = true;

		rewind = CL_RewindToTime(rewindtime);

		if (rewind)
		{
			demobuf.p = demobuf.buffer + rewind->demopos;
			memcpy(oldcmd, rewind->oldcmd, sizeof (oldcmd));
			memcpy(oldghost, rewind->oldghost, sizeof (oldghost));
			paused = false;
		}
		else
		{
			demo.rewinding = true;
			G_DoPlayDemo(NULL); // Restart the current demo
		}
	}

	for (j = 0; j < rewindtime && leveltime < rewindtime; j++)
	{
		G_Ticker((j % NEWTICRATERATIO) == 0);
	}

	demo.rewinding = false;
	sound_disabled = oldsounddisabled; // Re-enable SFX

	wipegamestate = gamestate; // No fading back in!

	COM_BufInsertText("renderview on\n");

	splitscreen = oldss;
	displayplayers[0] = olddp1;
	displayplayers[1] = olddp2;
	displayplayers[2] = olddp3;
	displayplayers[3] = olddp4;
	R_ExecuteSetViewSize();
	G_ResetViews(true);

	for (i = splitscreen; i >= 0; i--)
		P_ResetCamera(&players[displayplayers[i]], &camera[i]);
}

void G_ReadMetalTic(mobj_t *metal)
{
	uint8_t ziptic;
	uint8_t xziptic = 0;

	if (!metal_p)
		return;

	if (!metal->health)
	{
		G_StopMetalDemo();
		return;
	}

	switch (*metal_p)
	{
		case METALSNICE:
			break;
		case METALDEATH:
			if (metal->tracer)
				P_RemoveMobj(metal->tracer);
			P_KillMobj(metal, NULL, NULL, DMG_NORMAL);
			/* FALLTHRU */
		case DEMOMARKER:
		default:
			// end of demo data stream
			G_StopMetalDemo();
			return;
	}
	metal_p++;

	ziptic = READUINT8(metal_p);

	// Read changes from the tic
	if (ziptic & GZT_XYZ)
	{
		// make sure the values are read in the right order
		oldmetal.x = READFIXED(metal_p);
		oldmetal.y = READFIXED(metal_p);
		oldmetal.z = READFIXED(metal_p);
		P_MoveOrigin(metal, oldmetal.x, oldmetal.y, oldmetal.z);
		oldmetal.x = metal->x;
		oldmetal.y = metal->y;
		oldmetal.z = metal->z;
	}
	else
	{
		if (ziptic & GZT_MOMXY)
		{
			oldmetal.momx = (metalversion < 0x000e) ? READINT16(metal_p)<<8 : READFIXED(metal_p);
			oldmetal.momy = (metalversion < 0x000e) ? READINT16(metal_p)<<8 : READFIXED(metal_p);
		}
		if (ziptic & GZT_MOMZ)
			oldmetal.momz = (metalversion < 0x000e) ? READINT16(metal_p)<<8 : READFIXED(metal_p);
		oldmetal.x += oldmetal.momx;
		oldmetal.y += oldmetal.momy;
		oldmetal.z += oldmetal.momz;
	}
	if (ziptic & GZT_ANGLE)
		metal->angle = READUINT8(metal_p)<<24;
	if (ziptic & GZT_FRAME)
		oldmetal.frame = READUINT32(metal_p);
	if (ziptic & GZT_SPR2)
		oldmetal.sprite2 = READUINT8(metal_p);

	// Set movement, position, and angle
	// oldmetal contains where you're supposed to be.
	metal->momx = oldmetal.momx;
	metal->momy = oldmetal.momy;
	metal->momz = oldmetal.momz;
	P_UnsetThingPosition(metal);
	metal->x = oldmetal.x;
	metal->y = oldmetal.y;
	metal->z = oldmetal.z;
	P_SetThingPosition(metal);
	metal->frame = oldmetal.frame;
	metal->sprite2 = oldmetal.sprite2;

	if (ziptic & GZT_EXTRA)
	{ // But wait, there's more!
		xziptic = READUINT8(metal_p);
		if (xziptic & EZT_FLIP)
		{
			metal->eflags ^= MFE_VERTICALFLIP;
			metal->flags2 ^= MF2_OBJECTFLIP;
		}
		if (xziptic & EZT_SCALE)
		{
			metal->destscale = READFIXED(metal_p);
			if (metal->destscale != metal->scale)
				P_SetScale(metal, metal->destscale);
		}
		if (xziptic & EZT_SPRITE)
			metal->sprite = READUINT16(metal_p);
	}

#define follow metal->tracer
		if (ziptic & GZT_FOLLOW)
		{ // Even more...
			uint8_t followtic = READUINT8(metal_p);
			fixed_t temp;
			if (followtic & FZT_SPAWNED)
			{
				if (follow)
					P_RemoveMobj(follow);
				P_SetTarget(&follow, P_SpawnMobjFromMobj(metal, 0, 0, 0, MT_GHOST));
				P_SetTarget(&follow->tracer, metal);
				follow->tics = -1;
				temp = READINT16(metal_p)<<FRACBITS;
				follow->height = FixedMul(follow->scale, temp);

				if (followtic & FZT_LINKDRAW)
					follow->flags2 |= MF2_LINKDRAW;

				if (followtic & FZT_COLORIZED)
					follow->colorized = true;

				if (followtic & FZT_SKIN)
					R_ApplySkin(follow, skins[READUINT8(metal_p)]);
			}
			if (follow)
			{
				if (followtic & FZT_SCALE)
					follow->destscale = READFIXED(metal_p);
				else
					follow->destscale = metal->destscale;
				if (follow->destscale != follow->scale)
					P_SetScale(follow, follow->destscale);

				P_UnsetThingPosition(follow);
				temp = (metalversion < 0x000e) ? READINT16(metal_p)<<8 : READFIXED(metal_p);
				follow->x = metal->x + temp;
				temp = (metalversion < 0x000e) ? READINT16(metal_p)<<8 : READFIXED(metal_p);
				follow->y = metal->y + temp;
				temp = (metalversion < 0x000e) ? READINT16(metal_p)<<8 : READFIXED(metal_p);
				follow->z = metal->z + temp;
				P_SetThingPosition(follow);
				if (followtic & FZT_SKIN)
					follow->sprite2 = READUINT8(metal_p);
				else
					follow->sprite2 = 0;
				follow->sprite = READUINT16(metal_p);
				follow->frame = READUINT32(metal_p); // NOT & FF_FRAMEMASK here, so 32 bits
				follow->angle = metal->angle;
				follow->color = READUINT16(metal_p);

				if (!(followtic & FZT_SPAWNED))
				{
					if (xziptic & EZT_FLIP)
					{
						follow->flags2 ^= MF2_OBJECTFLIP;
						follow->eflags ^= MFE_VERTICALFLIP;
					}
				}
			}
		}
		else if (follow)
		{
			P_RemoveMobj(follow);
			P_SetTarget(&follow, NULL);
		}
#undef follow
}

void G_WriteMetalTic(mobj_t *metal)
{
	uint8_t ziptic = 0;
	uint8_t *ziptic_p;

	if (!demobuf.p) // demobuf.p will be NULL until the race start linedef executor is activated!
		return;

	WRITEUINT8(demobuf.p, METALSNICE);
	ziptic_p = demobuf.p++; // the ziptic, written at the end of this function

	#define MAXMOM (0xFFFF<<8)

	// GZT_XYZ is only useful if you've moved 256 FRACUNITS or more in a single tic.
	if (abs(metal->x-oldmetal.x) > MAXMOM
	|| abs(metal->y-oldmetal.y) > MAXMOM
	|| abs(metal->z-oldmetal.z) > MAXMOM)
	{
		oldmetal.x = metal->x;
		oldmetal.y = metal->y;
		oldmetal.z = metal->z;
		ziptic |= GZT_XYZ;
		WRITEFIXED(demobuf.p,oldmetal.x);
		WRITEFIXED(demobuf.p,oldmetal.y);
		WRITEFIXED(demobuf.p,oldmetal.z);
	}
	else
	{
		// For moving normally:
		// Store movement as a fixed value
		fixed_t momx = metal->x-oldmetal.x;
		fixed_t momy = metal->y-oldmetal.y;
		if (momx != oldmetal.momx
		|| momy != oldmetal.momy)
		{
			oldmetal.momx = momx;
			oldmetal.momy = momy;
			ziptic |= GZT_MOMXY;
			WRITEFIXED(demobuf.p,momx);
			WRITEFIXED(demobuf.p,momy);
		}
		momx = metal->z-oldmetal.z;
		if (momx != oldmetal.momz)
		{
			oldmetal.momz = momx;
			ziptic |= GZT_MOMZ;
			WRITEFIXED(demobuf.p,momx);
		}

		// This SHOULD set oldmetal.x/y/z to match metal->x/y/z
		oldmetal.x += oldmetal.momx;
		oldmetal.y += oldmetal.momy;
		oldmetal.z += oldmetal.momz;
	}

	#undef MAXMOM

	// Only store the 8 most relevant bits of angle
	// because exact values aren't too easy to discern to begin with when only 8 angles have different sprites
	// and it does not affect movement at all anyway.
	if (metal->player && metal->player->drawangle>>24 != oldmetal.angle)
	{
		oldmetal.angle = metal->player->drawangle>>24;
		ziptic |= GZT_ANGLE;
		WRITEUINT8(demobuf.p,oldmetal.angle);
	}

	// Store the sprite frame.
	if ((metal->frame & FF_FRAMEMASK) != oldmetal.frame)
	{
		oldmetal.frame = metal->frame; // NOT & FF_FRAMEMASK here, so 32 bits
		ziptic |= GZT_FRAME;
		WRITEUINT32(demobuf.p,oldmetal.frame);
	}

	if (metal->sprite == SPR_PLAY
	&& metal->sprite2 != oldmetal.sprite2)
	{
		oldmetal.sprite2 = metal->sprite2;
		ziptic |= GZT_SPR2;
		WRITEUINT8(demobuf.p,oldmetal.sprite2);
	}

	// Check for sprite set changes
	if (metal->sprite != oldmetal.sprite)
	{
		oldmetal.sprite = metal->sprite;
		ghostext[0].flags |= EZT_SPRITE;
	}

	if (ghostext[0].flags & ~(EZT_COLOR|EZT_HIT)) // these two aren't handled by metal ever
	{
		ziptic |= GZT_EXTRA;

		if (ghostext[0].scale == ghostext[0].lastscale)
			ghostext[0].flags &= ~EZT_SCALE;

		WRITEUINT8(demobuf.p,ghostext[0].flags);
		if (ghostext[0].flags & EZT_SCALE)
		{
			WRITEFIXED(demobuf.p,ghostext[0].scale);
			ghostext[0].lastscale = ghostext[0].scale;
		}
		if (ghostext[0].flags & EZT_SPRITE)
			WRITEUINT16(demobuf.p,oldmetal.sprite);
		ghostext[0].flags = 0;
	}

	if (metal->player && metal->player->followmobj && !(metal->player->followmobj->sprite == SPR_NULL || (metal->player->followmobj->renderflags & RF_DONTDRAW) == RF_DONTDRAW))
	{
		fixed_t temp;
		uint8_t *followtic_p = demobuf.p++;
		uint8_t followtic = 0;

		ziptic |= GZT_FOLLOW;

		if (metal->player->followmobj->skin)
			followtic |= FZT_SKIN;

		if (!(oldmetal.flags2 & MF2_AMBUSH))
		{
			followtic |= FZT_SPAWNED;
			WRITEINT16(demobuf.p,metal->player->followmobj->info->height>>FRACBITS);
			if (metal->player->followmobj->flags2 & MF2_LINKDRAW)
				followtic |= FZT_LINKDRAW;
			if (metal->player->followmobj->colorized)
				followtic |= FZT_COLORIZED;
			if (followtic & FZT_SKIN)
				WRITEUINT8(demobuf.p,(uint8_t)(((skin_t *)(metal->player->followmobj->skin))->skinnum));
			oldmetal.flags2 |= MF2_AMBUSH;
		}

		if (metal->player->followmobj->scale != metal->scale)
		{
			followtic |= FZT_SCALE;
			WRITEFIXED(demobuf.p,metal->player->followmobj->scale);
		}

		temp = metal->player->followmobj->x-metal->x;
		WRITEFIXED(demobuf.p,temp);
		temp = metal->player->followmobj->y-metal->y;
		WRITEFIXED(demobuf.p,temp);
		temp = metal->player->followmobj->z-metal->z;
		WRITEFIXED(demobuf.p,temp);
		if (followtic & FZT_SKIN)
			WRITEUINT8(demobuf.p,metal->player->followmobj->sprite2);
		WRITEUINT16(demobuf.p,metal->player->followmobj->sprite);
		WRITEUINT32(demobuf.p,metal->player->followmobj->frame); // NOT & FF_FRAMEMASK here, so 32 bits
		WRITEUINT16(demobuf.p,metal->player->followmobj->color);

		*followtic_p = followtic;
	}
	else
		oldmetal.flags2 &= ~MF2_AMBUSH;

	*ziptic_p = ziptic;

	// attention here for the ticcmd size!
	// latest demos with mouse aiming byte in ticcmd
	if (demobuf.p >= demobuf.end - 32)
	{
		G_StopMetalRecording(false); // no more space
		return;
	}
}

//
// G_RecordDemo
//
void G_RecordDemo(const char *name)
{
	int32_t maxsize;

	strcpy(demoname, name);
	maxsize = 1024 * 1024 * cv_netdemosize.value;

	P_SaveBufferAlloc(&demobuf, maxsize);
	demobuf.p = NULL;

	demo.recording = true;
	demo.buffer = &demobuf;

	/* FIXME: This whole file is in a wretched state. Take a
	look at G_WriteAllGhostTics and G_WriteDemoTiccmd, they
	write a lot of data. It's not realistic to refactor that
	code in order to know exactly HOW MANY bytes it can write
	out. So here's the deal. Reserve a decent block of memory
	at the end of the buffer and never use it. Those bastard
	functions will check if they overran the buffer, but it
	should be safe enough because they'll think there's less
	memory than there actually is and stop early. */
	const size_t deadspace = 1024;
	I_Assert(demobuf.size > deadspace);
	demobuf.size -= deadspace;
	demobuf.end -= deadspace;
}

void G_RecordMetal(void)
{
	int32_t maxsize;
	maxsize = 1024*1024;
	if (M_CheckParm("-maxdemo") && M_IsNextParm())
		maxsize = atoi(M_GetNextParm()) * 1024;

	P_SaveBufferAlloc(&demobuf, maxsize);
	demobuf.p = NULL;

	metalrecording = true;
}

void G_BeginRecording(void)
{
	uint8_t i, j, p;
	char name[MAXCOLORNAME+1];
	player_t *player = &players[consoleplayer];

	char *filename;
	uint8_t totalfiles;
	uint8_t *m;

	if (demobuf.p)
		return;
	memset(name,0,sizeof(name));

	demobuf.p = demobuf.buffer;
	demoflags = DF_GHOST|(multiplayer ? DF_MULTIPLAYER : (modeattacking<<DF_ATTACKSHIFT));

	if (multiplayer && !netgame)
		demoflags |= DF_NONETMP;

	if (encoremode)
		demoflags |= DF_ENCORE;

	if (multiplayer)
		demoflags |= DF_LUAVARS;

	// Setup header.
	memcpy(demobuf.p, DEMOHEADER, 12); demobuf.p += 12;
	WRITEUINT8(demobuf.p,VERSION);
	WRITEUINT8(demobuf.p,SUBVERSION);
	WRITEUINT16(demobuf.p,DEMOVERSION);

	// Full replay title
	demobuf.p += 64;
	{
		char demotitlename[65];
		char *title = G_BuildMapTitle(gamemap);
		// Print to a separate temp buffer instead of demo.titlename, so we can use it in M_TextInputSetString
		snprintf(demotitlename, sizeof(demotitlename), "%s - %s", title, modeattacking ? "Time Attack" : connectedservername);
		CV_StealthSet(&cv_dummyreplaysave, demotitlename); // Setup so its used when saving later.
		Z_Free(title);
	}

	// demo checksum
	demobuf.p += sizeof(uint64_t);

	// game data
	memcpy(demobuf.p, "PLAY", 4); demobuf.p += 4;
	WRITESTRINGL(demobuf.p, mapheaderinfo[gamemap-1]->lumpname, MAXMAPLUMPNAME);
	WRITEUINT64(demobuf.p, maphash);

	WRITEUINT8(demobuf.p, demoflags);
	WRITESTRINGL(demobuf.p, currentrecordpreset, MAXPRESETNAME+1);
	WRITEUINT8(demobuf.p, currentrecordpresetversion);
	WRITESTRINGL(demobuf.p, connectedservermotd, MAXSERVERMOTD);
	WRITESTRINGL(demobuf.p, connectedservername, MAXSERVERNAME);
	WRITESTRINGL(demobuf.p, connectedservercontact, MAXSERVERCONTACT);
	WRITESTRINGL(demobuf.p, connectedserverdescription, MAXSERVERDESCRIPTION);
	WRITEUINT8(demobuf.p, gametype & 0xFF);
	WRITEUINT8(demobuf.p, numlaps);

	// file list
	m = demobuf.p;/* file count */
	demobuf.p += 1;

	totalfiles = 0;
	for (i = NUMMAINWADS-1; ++i < numwadfiles; )
		if (wadfiles[i]->important)
	{
		nameonly(( filename = va("%s", wadfiles[i]->filename) ));
		WRITESTRINGL(demobuf.p, filename, MAX_WADPATH);
		WRITEUINT64(demobuf.p, wadfiles[i]->hash);
		WRITEUINT8(demobuf.p, !!wadfiles[i]->compatmode);

		totalfiles++;
	}

	WRITEUINT8(m, totalfiles);

	switch ((demoflags & DF_ATTACKMASK)>>DF_ATTACKSHIFT)
	{
		case ATTACKING_NONE: // 0
			break;
		case ATTACKING_TIME: // 1
			demotime_p = demobuf.p;
			WRITEUINT32(demobuf.p,UINT32_MAX); // time
			WRITEUINT32(demobuf.p,UINT32_MAX); // lap
			break;
		case ATTACKING_ITEMBREAK: // 2
			demotime_p = demobuf.p;
			WRITEUINT32(demobuf.p,UINT32_MAX); // time
			break;
		default: // 3
			break;
	}

	for (i = 0; i < PRNUMCLASS; i++)
	{
		WRITEUINT32(demobuf.p, P_GetInitSeed(i));
	}

	// Reserved for extrainfo location from start of file
	demoinfo_p = demobuf.p;
	WRITEUINT32(demobuf.p, 0);

	// Save netvar data
	CV_SaveDemoVars(&demobuf.p);

	if ((demoflags & DF_GRANDPRIX))
	{
		WRITEUINT8(demobuf.p, grandprixinfo.gamespeed);
		WRITEUINT8(demobuf.p, grandprixinfo.masterbots == true);
		WRITEUINT8(demobuf.p, grandprixinfo.lunaticmode == true);
		WRITEUINT8(demobuf.p, grandprixinfo.ranked == true);
		WRITEUINT8(demobuf.p, grandprixinfo.eventmode);
	}

	// Save "mapmusrng" used for altmusic selection
	WRITEUINT8(demobuf.p, mapmusrng);

	// Now store some info for each in-game player

	// Lat' 12/05/19: Do note that for the first game you load, everything that gets saved here is total garbage;
	// The name will always be Player <n>, the skin sonic, the color None and the follower 0. This is only correct on subsequent games.
	// In the case of said first game, the skin and the likes are updated with Got_NameAndColor, which are then saved in extradata for the demo with DXD_SKIN in r_things.c for instance.


	for (p = 0; p < MAXPLAYERS; p++) {
		if (playeringame[p]) {
			player = &players[p];
			WRITEUINT8(demobuf.p, p);

			i = 0;
			if (player->spectator)
				i |= DEMO_SPECTATOR;
			if (player->pflags & PF_KICKSTARTACCEL)
				i |= DEMO_KICKSTART;
			if (player->pflags & PF_SHRINKME)
				i |= DEMO_SHRINKME;
			if (player->bot == true)
				i |= DEMO_BOT;
			WRITEUINT8(demobuf.p, i);

			if (i & DEMO_BOT)
			{
				WRITEUINT8(demobuf.p, player->botvars.difficulty);
				WRITEUINT8(demobuf.p, player->botvars.diffincrease); // needed to avoid having to duplicate logic
				WRITEUINT8(demobuf.p, (uint8_t)player->botvars.rival);
			}

			// Name
			WRITESTRINGL(demobuf.p, player_names[p], 21+1);

			// Skin
			WRITESTRINGL(demobuf.p, skins[player->skin]->name, 16+1);

			// Color
			WRITESTRINGL(demobuf.p, skincolors[player->skincolor].name, 16+1);

			// Voice
			WRITESTRINGL(demobuf.p, skins[player->skin]->voices[player->voice_id].name, 32+1);

			// Save follower's skin name
			if (player->followerskin == -1)
				WRITESTRINGL(demobuf.p, "None", 16+1);
			else
				WRITESTRINGL(demobuf.p, followers[player->followerskin].name, 16+1);

			// Save follower's colour
			for (j = (numskincolors+2)-1; j > 0; j--)
			{
				if (Followercolor_cons_t[j].value == players[i].followercolor)
					break;
			}

			// Not KartColor_Names because followercolor has extra values such as "Match"
			WRITESTRINGL(demobuf.p, Followercolor_cons_t[j].strvalue, 16+1);

			// Score, since Kart uses this to determine where you start on the map
			WRITEUINT32(demobuf.p, player->score);

			// Power Levels
			WRITEUINT16(demobuf.p, clientpowerlevels[p][gametype == GT_BATTLE ? PWRLV_BATTLE : PWRLV_RACE]);

			// Kart speed and weight
			WRITEUINT8(demobuf.p, player->kartspeed);
			WRITEUINT8(demobuf.p, player->kartweight);

			// And mobjtype_t is best with uint32_t too...
			WRITEUINT32(demobuf.p, player->followitem);

			for (j = 0; j < MAXAVAILABILITY; j++)
			{
				WRITEUINT8(demobuf.p, player->availabilities[j]);
			}
		}
	}

	WRITEUINT8(demobuf.p, 0xFF); // Denote the end of the player listing

	// player lua vars, always saved even if empty
	if (demoflags & DF_LUAVARS)
	{
		demobuf.write = true;
		LUA_Sync(&demobuf, false, false);
	}

	memset(&oldcmd,0,sizeof(oldcmd));
	memset(&oldghost,0,sizeof(oldghost));
	memset(&ghostext,0,sizeof(ghostext));

	for (i = 0; i < MAXPLAYERS; i++)
	{
		ghostext[i].lastcolor = ghostext[i].color = GHC_NORMAL;
		ghostext[i].lastscale = ghostext[i].scale = FRACUNIT;

		if (players[i].mo)
		{
			oldghost[i].x = players[i].mo->x;
			oldghost[i].y = players[i].mo->y;
			oldghost[i].z = players[i].mo->z;
			oldghost[i].angle = players[i].mo->angle;

			// preticker started us gravity flipped
			if (players[i].mo->eflags & MFE_VERTICALFLIP)
				ghostext[i].flags |= EZT_FLIP;
		}
	}
}

void G_BeginMetal(void)
{
	mobj_t *mo = players[consoleplayer].mo;

#if 0
	if (demobuf.p)
		return;
#endif

	demobuf.p =  demobuf.buffer;

	// Write header.
	memcpy(demobuf.p, DEMOHEADER, 12); demobuf.p += 12;
	WRITEUINT8(demobuf.p,VERSION);
	WRITEUINT8(demobuf.p,SUBVERSION);
	WRITEUINT16(demobuf.p,DEMOVERSION);

	// demo checksum
	demobuf.p += sizeof(uint64_t);

	memcpy(demobuf.p, "METL", 4); demobuf.p += 4;

	memset(&ghostext,0,sizeof(ghostext));
	ghostext[0].lastscale = ghostext[0].scale = FRACUNIT;

	// Set up our memory.
	memset(&oldmetal,0,sizeof(oldmetal));
	oldmetal.x = mo->x;
	oldmetal.y = mo->y;
	oldmetal.z = mo->z;
	oldmetal.angle = mo->angle>>24;
}

void G_WriteStanding(uint8_t ranking, char *name, int32_t skinnum, uint16_t color, uint32_t val)
{
	if (demoinfo_p && *(uint32_t *)demoinfo_p == 0)
	{
		WRITEUINT8(demobuf.p, DEMOMARKER); // add the demo end marker
		*(uint32_t *)demoinfo_p = demobuf.p -  demobuf.buffer;
	}

	WRITEUINT8(demobuf.p, DW_STANDING);
	WRITEUINT8(demobuf.p, ranking);

	// Name
	WRITESTRINGL(demobuf.p, name, 21+1);

	// Skin
	WRITESTRINGL(demobuf.p, skins[skinnum]->name, 16+1);

	// Color
	WRITESTRINGL(demobuf.p, skincolors[color].name, 16+1);

	// Score/time/whatever
	WRITEUINT32(demobuf.p, val);
}

void G_SetDemoTime(uint32_t ptime, uint32_t plap)
{
	if (!demo.recording || !demotime_p)
		return;
	if (demoflags & DF_TIMEATTACK)
	{
		WRITEUINT32(demotime_p, ptime);
		WRITEUINT32(demotime_p, plap);
		demotime_p = NULL;
	}
	else if (demoflags & DF_ITEMBREAKER)
	{
		WRITEUINT32(demotime_p, ptime);
		(void)plap;
		demotime_p = NULL;
	}
}

static void G_LoadDemoExtraFiles(demoheader_t *header)
{
	filestatus_t ncs;
	dboolean toomany = false;
	dboolean alreadyloaded;
	uint8_t i, j;

	for (i = 0; i < header->numfiles; i++)
	{
		demofile_t *file = &header->files[i];
		if (!toomany)
		{
			alreadyloaded = false;

			for (j = 0; j < numwadfiles; ++j)
			{
				if (file->filehash == wadfiles[j]->hash)
				{
					alreadyloaded = true;
					break;
				}
			}

			if (alreadyloaded)
				continue;

			if (numwadfiles >= MAX_WADFILES)
				toomany = true;
			else
				ncs = findfile(file->filename, file->filehash, false);

			if (toomany)
			{
				CONS_Alert(CONS_WARNING, M_GetText("Too many files loaded to add anymore for demo playback\n"));
				if (!CON_Ready())
					M_StartMessage2("Replay", M_GetText("There are too many files loaded to add this demo's addons.\n\nDemo playback may desync.\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
			}
			else if (ncs != FS_FOUND)
			{
				if (ncs == FS_NOTFOUND)
					CONS_Alert(CONS_NOTICE, M_GetText("You do not have a copy of %s\n"), file->filename);
				else if (ncs == FS_BADHASH)
					CONS_Alert(CONS_NOTICE, M_GetText("Checksum mismatch on %s\n"), file->filename);
				else
					CONS_Alert(CONS_NOTICE, M_GetText("Unknown error finding file %s\n"), file->filename);

				if (!CON_Ready())
					M_StartMessage2("Replay", M_GetText("There were errors trying to add this demo's addons. Check the console for more information.\n\nDemo playback may desync.\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
			}
			else
			{
				P_PartialAddWadFile(file->filename, file->compatmode ? WC_ON : WC_OFF, false);
			}
		}
	}

	if (P_PartialAddGetStage() >= 0)
		P_MultiSetupWadFiles(true); // in case any partial adds were done
}

// G_CheckDemoExtraFiles: checks if our loaded WAD list matches the demo's.
// Enabling quick prevents filesystem checks to see if needed files are available to load.
static uint8_t G_CheckDemoExtraFiles(demoheader_t *header, dboolean quick)
{
	uint8_t filesloaded, nmusfilecount;
	dboolean toomany = false;
	dboolean alreadyloaded;
	uint8_t i, j;
	uint8_t error = 0;

	filesloaded = 0;
	for (i = 0; i < header->numfiles; ++i)
	{
		demofile_t *file = &header->files[i];

		if (!toomany)
		{
			alreadyloaded = false;
			nmusfilecount = 0;

			for (j = 0; j < numwadfiles; ++j)
			{
				if (wadfiles[j]->important && j >= NUMMAINWADS)
					nmusfilecount++;
				else
					continue;

				if (file->filehash == wadfiles[j]->hash)
				{
					alreadyloaded = true;

					if (i != nmusfilecount-1 && error < DFILE_ERROR_OUTOFORDER)
						error |= DFILE_ERROR_OUTOFORDER;

					break;
				}
			}

			if (alreadyloaded)
			{
				filesloaded++;
				continue;
			}

			if (numwadfiles >= MAX_WADFILES)
				error = DFILE_ERROR_CANNOTLOAD;
			else if (!quick && findfile(file->filename, file->filehash, false) != FS_FOUND)
				error = DFILE_ERROR_CANNOTLOAD;
			else if (error < DFILE_ERROR_INCOMPLETEOUTOFORDER)
				error |= DFILE_ERROR_NOTLOADED;
		} else
			error = DFILE_ERROR_CANNOTLOAD;
	}

	// Get final file count
	nmusfilecount = 0;

	for (j = 0; j < numwadfiles; ++j)
		if (wadfiles[j]->important && j >= NUMMAINWADS)
			nmusfilecount++;

	if (!error && filesloaded < nmusfilecount)
		error = DFILE_ERROR_EXTRAFILES;

	return error;
}

// Returns bitfield:
// 1 == new demo has lower time
// 2 == new demo has higher score
// 4 == new demo has higher rings
uint8_t G_CmpDemoTime(char *oldname, char *newname)
{
	uint8_t *buffer;
	uint32_t oldtime, newtime, oldlap, newlap;
	size_t bufsize ATTRUNUSED;
	uint8_t c;
	uint16_t s ATTRUNUSED;
	uint8_t aflags = 0;
	dboolean uselaps = false;

	// load the new file
	FIL_DefaultExtension(newname, ".lmp");
	bufsize = FIL_ReadFile(newname, &buffer);
	I_Assert(bufsize != 0);

	// read demo header
	CLEANUP(G_FreeDemoHeader) demoheader_t header;
	headerstatus_e status = G_ReadDemoHeader(buffer, &header);
	(void)status;
	I_Assert(status == HEADER_OK && header.version == VERSION
	      && header.subversion == SUBVERSION && header.demoversion == DEMOVERSION);

	Z_Free(buffer);

	aflags = header.demoflags & (DF_TIMEATTACK|DF_ITEMBREAKER);
	I_Assert(aflags);

	if (header.demoflags & DF_TIMEATTACK)
		uselaps = true; // get around uninitalized error

	newtime = header.attacking.time;
	if (uselaps)
		newlap = header.attacking.lap;
	else
		newlap = UINT32_MAX;

	// load old file
	FIL_DefaultExtension(oldname, ".lmp");
	if (!FIL_ReadFile(oldname, &buffer))
	{
		CONS_Alert(CONS_ERROR, M_GetText("Failed to read file '%s'.\n"), oldname);
		return UINT8_MAX;
	}

	// read demo header
	CLEANUP(G_FreeDemoHeader) demoheader_t oldheader;
	if (G_ReadDemoHeader(buffer, &oldheader) != HEADER_OK)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("File '%s' invalid format. It will be overwritten.\n"), oldname);
		Z_Free(buffer);
		return UINT8_MAX;
	}

	Z_Free(buffer);

	if (!(oldheader.demoflags & aflags))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("File '%s' not from same game mode. It will be overwritten.\n"), oldname);
		return UINT8_MAX;
	}

	oldtime = oldheader.attacking.time;
	if (uselaps)
		oldlap = oldheader.attacking.lap;
	else
		oldlap = 0;

	c = 0;

	if (uselaps)
	{
		if (newtime < oldtime
		|| (newtime == oldtime && (newlap < oldlap)))
			c |= 1; // Better time
		if (newlap < oldlap
		|| (newlap == oldlap && newtime < oldtime))
			c |= 1<<1; // Better lap time
	}
	else
	{
		if (newtime < oldtime)
			c |= 1; // Better time
	}

	return c;
}

void G_LoadDemoInfo(menudemo_t *pdemo)
{
	CLEANUP(Z_Pfree) uint8_t *infobuffer = NULL;
	const uint8_t *extrainfo_p;

	if (!FIL_ReadFile(pdemo->filepath, &infobuffer))
	{
		CONS_Alert(CONS_ERROR, M_GetText("Failed to read file '%s'.\n"), pdemo->filepath);
		pdemo->type = MD_INVALID;
		snprintf(pdemo->title, sizeof(pdemo->title), "INVALID REPLAY");
		return;
	}

	pdemo->type = MD_LOADED;

	CLEANUP(G_FreeDemoHeader) demoheader_t header;
	switch (G_ReadDemoHeader(infobuffer, &header))
	{
	case HEADER_OK:
		break;

	case HEADER_BADMAGIC:
		CONS_Alert(CONS_ERROR, M_GetText("%s is not a SRB2Kart replay file.\n"), pdemo->filepath);
		pdemo->type = MD_INVALID;
		snprintf(pdemo->title, sizeof(pdemo->title), "INVALID REPLAY");
		return;

	case HEADER_BADVERSION:
		CONS_Alert(CONS_ERROR, M_GetText("%s is an incompatible replay format and cannot be played.\n"), pdemo->filepath);
		pdemo->type = MD_INVALID;
		snprintf(pdemo->title, sizeof(pdemo->title), "INVALID REPLAY");
		return;

	case HEADER_BADFORMAT:
		CONS_Alert(CONS_ERROR, M_GetText("%s is the wrong type of recording and cannot be played.\n"), pdemo->filepath);
		pdemo->type = MD_INVALID;
		snprintf(pdemo->title, sizeof(pdemo->title), "INVALID REPLAY");
		return;
	}

	strlcpy(pdemo->title, header.demotitle, sizeof(pdemo->title));

	if (header.version != VERSION || header.subversion != SUBVERSION)
		pdemo->type = MD_OUTDATED;

	pdemo->map = G_MapNumber(header.maptitle);

	// temp?
	if (!(header.demoflags & DF_MULTIPLAYER))
	{
		CONS_Alert(CONS_ERROR, M_GetText("%s is not a multiplayer replay and can't be listed on this menu fully yet.\n"), pdemo->filepath);
		return;
	}

	pdemo->gametype = header.gametype;
	pdemo->numlaps = header.numlaps;

	pdemo->addonstatus = G_CheckDemoExtraFiles(&header, true);

	extrainfo_p = infobuffer + header.extrainfo; // The extra uint32_t read is for a blank 4 bytes?

	// Pared down version of CV_LoadNetVars to find the kart speed
	pdemo->kartspeed = KARTSPEED_NORMAL; // Default to normal speed
	for (uint16_t i = 0; i < header.numcvars; i++)
	{
		if (header.cvars[i].name == NULL)
			continue;
		if (fastcmp(header.cvars[i].name, cv_kartspeed.name))
		{
			uint8_t j;
			for (j = 0; kartspeed_cons_t[j].strvalue; j++)
				if (fasticmp(kartspeed_cons_t[j].strvalue, header.cvars[i].value))
					pdemo->kartspeed = kartspeed_cons_t[j].value;
		}
	}

	if (header.demoflags & DF_ENCORE)
		pdemo->kartspeed |= DF_ENCORE;

	// Read standings!
	uint16_t count = 0;

	while (READUINT8(extrainfo_p) == DW_STANDING) // Assume standings are always first in the extrainfo
	{
		int32_t i;
		char temp[17];

		pdemo->standings[count].ranking = READUINT8(extrainfo_p);

		// Name
		if (header.demoversion <= 0x000C)
		{
			READMEM(extrainfo_p, pdemo->standings[count].name, 16);
			pdemo->standings[count].name[16] = '\0';
		}
		else
			READSTRINGL(extrainfo_p, pdemo->standings[count].name, 21+1);

		// Skin
		if (header.demoversion <= 0x000C)
		{
			READMEM(extrainfo_p, temp, 16);
			temp[16] = '\0';
		}
		else
			READSTRINGL(extrainfo_p, temp, 16+1);
		pdemo->standings[count].skin = UINT16_MAX;

		uint32_t hash = HASH32(temp, strlen(temp));

		for (i = 0; i < numskins; i++)
		{
			if (skins[i]->namehash != hash)
				continue;

			if (fasticmp(skins[i]->name, temp))
			{
				pdemo->standings[count].skin = i;
				break;
			}
		}

		// Color
		if (header.demoversion <= 0x000C)
		{
			READMEM(extrainfo_p, temp, 16);
			temp[16] = '\0';
		}
		else
			READSTRINGL(extrainfo_p, temp, 16+1);
		pdemo->standings[count].color = SKINCOLOR_NONE;
		for (i = 0; i < numskincolors; i++)
			if (fasticmp(skincolors[i].name,temp))				// SRB2kart
			{
				pdemo->standings[count].color = i;
				break;
			}

		// Score/time/whatever
		pdemo->standings[count].timeorscore = READUINT32(extrainfo_p);

		count++;

		if (count >= MAXPLAYERS)
			break; //@TODO still cycle through the rest of these if extra demo data is ever used
	}
}

//
// G_PlayDemo
//
void G_DeferedPlayDemo(const char *name)
{
	COM_BufAddText("playdemo \"");
	COM_BufAddText(name);
	COM_BufAddText("\" -addfiles\n");
}

//
// Start a demo from a .LMP file or from a wad resource
//

#define SKIPERRORS

static void PrintDemoError(const char (*msg)[1024])
{
	if (*msg[0] == '\0')
		return;

	CONS_Alert(CONS_ERROR, "%s", *msg);
	demo.playback = false;
	demo.single_seed = false;
	demo.title = false;

	if (!CON_Ready()) // In the console they'll just see the notice there! No point pulling them out.
		M_StartMessage2("Replay", *msg, MB_NONE, MM_NOTHING, "Press any key", NULL);
}

static void FreeDemoBuffer(savebuffer_t **freebuffer)
{
	if (*freebuffer != NULL)
		P_SaveBufferFree(*freebuffer);
}

void G_DoPlayDemo(char *defdemoname)
{
	uint16_t i, j;
	lumpnum_t l;
	CLEANUP(Z_Pfree) char *pdemoname = NULL;
	const char *n;
	CLEANUP(PrintDemoError) char msg[1024];
	CLEANUP(FreeDemoBuffer) savebuffer_t *freebuffer = NULL;

#if defined(SKIPERRORS) && !defined(DEVELOP)
	dboolean skiperrors = false;
#endif

	msg[0] = '\0';
	M_ClearMenusEx(true, true);
	G_InitDemoRewind();
	gameaction = ga_nothing;

	// No demo name means we're restarting the current demo
	if (defdemoname == NULL)
	{
		demobuf.p = demobuf.buffer;
		pdemoname = ZZ_Alloc(1); // Easier than adding checks for this everywhere it's freed
	}
	else
	{
		n = defdemoname+strlen(defdemoname);
		while (*n != '/' && *n != '\\' && n != defdemoname)
			n--;
		if (n != defdemoname)
			n++;
		pdemoname = ZZ_Alloc(strlen(n)+1);
		strcpy(pdemoname,n);

		M_SetPlaybackMenuPointer();

		// Internal if no extension, external if one exists
		if (FIL_CheckExtension(defdemoname))
		{
			//FIL_DefaultExtension(defdemoname, ".lmp");
			if (P_SaveBufferFromFile(&demobuf, defdemoname) == false)
			{
				snprintf(msg, 1024, M_GetText("Failed to read file '%s'.\n"), defdemoname);
				return;
			}
		}
		// load demo resource from WAD
		else
		{
			if (n == defdemoname)
			{
				// Raw lump.
				if ((l = W_CheckNumForName(defdemoname)) == LUMPERROR)
				{
					snprintf(msg, 1024, M_GetText("Failed to read lump '%s'.\n"), defdemoname);
					return;
				}

				P_SaveBufferFromLump(&demobuf, l);
			}
			else
			{
				// vres GHOST_%u
				virtres_t *vRes;
				virtlump_t *vLump;
				mapnum_t mapnum;
				size_t step = 0;
				char mapname[MAXMAPLUMPNAME];

				step = 0;
				while (defdemoname+step < n-1)
				{
					mapname[step] = defdemoname[step];
					step++;
				}
				mapname[step] = '\0';

				mapnum = G_MapNumber(mapname);
				if (mapnum >= nummapheaders || mapheaderinfo[mapnum]->lumpnum == LUMPERROR)
				{
					snprintf(msg, 1024, M_GetText("Failed to read lump '%s (couldn't find map %s)'.\n"), defdemoname, mapname);
					return;
				}

				vRes = vres_GetMap(mapheaderinfo[mapnum]->lumpnum);
				vLump = vres_Find(vRes, pdemoname);

				if (vLump == NULL)
				{
					snprintf(msg, 1024, M_GetText("Failed to read lump '%s (couldn't find lump %s in %s)'.\n"), defdemoname, pdemoname, mapname);
					return;
				}

				P_SaveBufferAlloc(&demobuf, vLump->size);
				memcpy(demobuf.buffer, vLump->data, vLump->size);

				vres_Free(vRes);
			}
#if defined(SKIPERRORS) && !defined(DEVELOP)
			skiperrors = true; // RR: Don't print warnings for staff ghosts, since they'll inevitably happen when we make bugfixes/changes...
#endif
		}
	}

	// read demo header
	demo.playback = true;
	demo.buffer = freebuffer = &demobuf;

	CLEANUP(G_FreeDemoHeader) demoheader_t header;
	switch (G_ReadDemoHeader(demobuf.p, &header))
	{
	case HEADER_OK:
		demobuf.p += header.endofs;
		break;

	case HEADER_BADMAGIC:
		snprintf(msg, 1024, M_GetText("%s is not a SRB2Kart replay file.\n"), pdemoname);
		return;

	case HEADER_BADVERSION:
		snprintf(msg, 1024, M_GetText("%s is an incompatible replay format and cannot be played.\n"), pdemoname);
		return;

	case HEADER_BADFORMAT:
		snprintf(msg, 1024, M_GetText("%s is the wrong type of recording and cannot be played.\n"), pdemoname);
		return;
	}

	demo.version = header.demoversion;

	// demo title
	strlcpy(demo.titlename, header.demotitle, sizeof(demo.titlename));

	if (demo.title) // Titledemos should always play and ought to always be compatible with whatever wadlist is running.
		;//G_SkipDemoExtraFiles(&demobuf.p);
	else if (demo.loadfiles)
	{
		G_LoadDemoExtraFiles(&header);

		// Generate cvar names for missing Kart addon netids.
		for (j = 0; j < header.numcvars; j++)
		{
			if (header.cvars[j].name == NULL && header.cvars[j].legacynetid)
			{
				consvar_t *legacycvar = CV_FindLegacyNetVar(header.cvars[j].legacynetid);
				header.cvars[j].name = legacycvar ? strdup(legacycvar->name) : NULL;
			}
		}
	}
	else if (demo.ignorefiles)
		;//G_SkipDemoExtraFiles(&demobuf.p);
	else switch (G_CheckDemoExtraFiles(&header, false))
	{
	case DFILE_ERROR_NOTLOADED:
		snprintf(msg, 1024,
			M_GetText("Required files for this demo are not loaded.\n\nUse\n\"playdemo %s -addfiles\"\nto load them and play the demo.\n"),
		pdemoname);
		return;

	case DFILE_ERROR_OUTOFORDER:
		snprintf(msg, 1024,
			M_GetText("Required files for this demo are loaded out of order.\n\nUse\n\"playdemo %s -force\"\nto play the demo anyway.\n"),
		pdemoname);
		return;

	case DFILE_ERROR_INCOMPLETEOUTOFORDER:
		snprintf(msg, 1024,
			M_GetText("Required files for this demo are not loaded, and some are out of order.\n\nUse\n\"playdemo %s -addfiles\"\nto load needed files and play the demo.\n"),
		pdemoname);
		return;

	case DFILE_ERROR_CANNOTLOAD:
		snprintf(msg, 1024,
			M_GetText("Required files for this demo cannot be loaded.\n\nUse\n\"playdemo %s -force\"\nto play the demo anyway.\n"),
		pdemoname);
		return;

	case DFILE_ERROR_EXTRAFILES:
		snprintf(msg, 1024,
			M_GetText("You have additional files loaded beyond the demo's file list.\n\nUse\n\"playdemo %s -force\"\nto play the demo anyway.\n"),
		pdemoname);
		return;
	}

	// Moved here so these init properly.
	gamemap = G_MapNumber(header.maptitle)+1;

	demoflags = header.demoflags;
	gametype = header.gametype;
	G_SetGametype(gametype);
	numlaps = header.numlaps;

	// ...*map* not loaded?
	if (!gamemap || (gamemap > nummapheaders) || !mapheaderinfo[gamemap-1] || mapheaderinfo[gamemap-1]->lumpnum == LUMPERROR)
	{
		snprintf(msg, 1024, M_GetText("%s features a course that is not currently loaded.\n"), pdemoname);
		return;
	}

	// Sigh ... it's an empty demo.
	if (header.empty)
	{
		snprintf(msg, 1024, M_GetText("%s contains no data to be played.\n"), pdemoname);
		return;
	}

	// extra checks for RA replays
	if (demoflags & DF_ATTACKMASK)
	{
		const char *reason = NULL;
		if (header.numplayers != 1)
			reason = "multiple players";
		else if (header.playerdata[0].flags & DEMO_SPECTATOR)
			reason = "spectators";
		else if (header.playerdata[0].flags & DEMO_BOT)
			reason = "bots";

		if (reason)
		{
			snprintf(msg, 1024, M_GetText("%s is a Record Attack replay with %s, and is thus invalid.\n"), pdemoname, reason);
			return;
		}
	}

	// no more error checks at this point, don't free demobuf
	freebuffer = NULL;

	modeattacking = (demoflags & DF_ATTACKMASK)>>DF_ATTACKSHIFT;
	multiplayer = !!(demoflags & DF_MULTIPLAYER);

	demo.netgame = (multiplayer && !(demoflags & DF_NONETMP));
	CON_ToggleOff();

	hu_demotime = header.attacking.time;
	hu_demolap = header.attacking.lap;

	if (modeattacking != ATTACKING_TIME && modeattacking != ATTACKING_ITEMBREAK)
		modeattacking = ATTACKING_NONE; // is this really necessary?

	// net var data
	CV_LoadDemoVars(&header);

	if (modeattacking != ATTACKING_NONE)
	{
		strbuf_t *pv = G_GetRecordPresetVersionForDemo(&header);
		if (pv)
		{
			const char *faulted = G_CheckPresetCvars(pv);
			if (faulted && !G_CompatLevel(0x000C))
				M_StartMessage2("Replay", va("Demo cvar %s doesn't match the preset! This may be a mistake, or the demo is cheated!\n", faulted), MB_NONE, MM_NOTHING, "Press any key", NULL);
		}
		else
			CONS_Alert(CONS_WARNING, "Couldn't find record preset '%s' version %d\n", header.rapreset, header.rapresetversion);
	}

	memset(&grandprixinfo, 0, sizeof grandprixinfo);
	if ((demoflags & DF_GRANDPRIX))
	{
		grandprixinfo.gp = true;
		grandprixinfo.gamespeed = header.grandprix.gamespeed;
		grandprixinfo.masterbots = header.grandprix.masterbots;
		grandprixinfo.lunaticmode = header.grandprix.lunaticmode;
		grandprixinfo.ranked = header.grandprix.ranked;
		grandprixinfo.eventmode = header.grandprix.eventmode;
	}

	// Load "mapmusrng" used for altmusic selection
	mapmusrng = header.mapmusrng;

	memset(&oldcmd,0,sizeof(oldcmd));
	memset(&oldghost,0,sizeof(oldghost));
	memset(&ghostext,0,sizeof(ghostext));

#if defined(SKIPERRORS) && !defined(DEVELOP)
	if ((VERSION != header.version || SUBVERSION != header.subversion) && !skiperrors)
#else
	if (VERSION != header.version || SUBVERSION != header.subversion)
#endif
		CONS_Alert(CONS_WARNING, M_GetText("Demo version does not match game version. Desyncs may occur.\n"));

	// console warning messages
#if defined(SKIPERRORS) && !defined(DEVELOP)
	demosynced = (!skiperrors);
#else
	demosynced = true;
#endif

	// didn't start recording right away.
	demo.deferstart = false;

	consoleplayer = 0;
	memset(playeringame,0,sizeof(playeringame));
	memset(displayplayers,0,sizeof(displayplayers));
	memset(camera,0,sizeof(camera)); // reset freecam

	// Load players that were in-game when the map started
	for (uint8_t pnum = 0; pnum < header.numplayers; pnum++)
	{
		demoplayer_t *plr = &header.playerdata[pnum];
		uint8_t p = plr->playernum;
		player_t *player = &players[p];


		if (!playeringame[displayplayers[0]] || players[displayplayers[0]].spectator)
			displayplayers[0] = consoleplayer = serverplayer = p;

		G_AddPlayer(p, p);
		player->spectator = !!(plr->flags & DEMO_SPECTATOR);

		if (plr->flags & DEMO_KICKSTART)
			player->pflags |= PF_KICKSTARTACCEL;
		else
			player->pflags &= ~PF_KICKSTARTACCEL;

		if (plr->flags & DEMO_SHRINKME)
			player->pflags |= PF_SHRINKME;
		else
			player->pflags &= ~PF_SHRINKME;

		K_UpdateShrinkCheat(player);

		if ((player->bot = !!(plr->flags & DEMO_BOT)) == true)
		{
			player->botvars.difficulty = plr->bot.difficulty;
			player->botvars.diffincrease = plr->bot.diffincrease; // needed to avoid having to duplicate logic
			player->botvars.rival = plr->bot.rival;
		}

		// Name
		strcpy(player_names[p], plr->name);

		/*if (player->spectator)
		{
			CONS_Printf("player %s is spectator at start\n", player_names[p]);
		}*/

		// Skin
		SetPlayerSkin(p, plr->skin);

		// Color
		for (i = 0; i < numskincolors; i++)
			if (fasticmp(skincolors[i].name, plr->color)) // SRB2kart
			{
				player->skincolor = i;
				break;
			}

		// Voice
		// We'll only assign IDs, since the actual voice assignment happens during
		// player spawn.

		const int32_t vce = R_FindIDForVoice(skins[player->skin], plr->voice);

		if (vce == MAXSKINVOICES)
		{
			// The above function didn't find a voice, so we'll just fall back to
			// the skin's voice.
			player->voice_id = 0;
		}
		else
		{
			player->voice_id = (uint16_t)vce;
		}

		// Follower
		K_SetFollowerByName(p, plr->follower);

		// Follower colour
		for (i = 0; i < numskincolors +2; i++)	// +2 because of Match and Opposite
			if (fasticmp(Followercolor_cons_t[i].strvalue, plr->followercolor))
			{
				player->followercolor = i;
				break;
			}

		// Score, since Kart uses this to determine where you start on the map
		player->score = plr->score;

		// Power Levels
		clientpowerlevels[p][gametype == GT_BATTLE ? PWRLV_BATTLE : PWRLV_RACE] = plr->powerlevel;

		// Kart stats (set later)

		if (!fasticmp(skins[player->skin]->name, plr->skin))
			FindClosestSkinForStats(p, plr->kartspeed, plr->kartweight);

		// Followitem
		player->followitem = plr->followitem;

		for (i = 0; i < MAXAVAILABILITY; i++)
		{
			player->availabilities[i] = plr->availabilities[i];
		}
	}

	// end of player read (the 0xFF marker)
	// so this is where we are to read our lua variables (if possible!)
	if (demoflags & DF_LUAVARS)	// again, used for compability, lua shit will be saved to replays regardless of if it's even been loaded
	{
		if (!gL) // No Lua state! ...I guess we'll just start one...
			LUA_ClearState();

		// No modeattacking check, DF_LUAVARS won't be present here.
		demobuf.write = false;
		LUA_Sync(&demobuf, false, G_CompatLevel(0x0002));
	}

	splitscreen = 0;

	if (demo.title)
	{
		splitscreen = M_RandomKey(6)-1;
		splitscreen = min(min(3, header.numplayers-1), splitscreen); // Bias toward 1p and 4p views

		for (i = 0; i <= splitscreen; i++)
			G_ResetView(i+1, header.playerdata[M_RandomKey(header.numplayers)].playernum, false);
	}

	R_ExecuteSetViewSize();

	demo.single_seed = false;

	if (header.demoversion <= 0x0012)
	{
		demo.single_seed = true;
		P_SetRandSeed(PR_LEGACY, header.randseed[PR_LEGACY]);
	}
	else
	{
		for (i = 0; i < PRNUMCLASS; i++)
		{
			P_SetRandSeed(i, header.randseed[i]);
		}
	}
	G_InitNew(demoflags & DF_ENCORE, gamemap, true, true); // Doesn't matter whether you reset or not here, given changes to resetplayer.

	// Setup server name, contact and description.
	strncpy(connectedservermotd, header.servermotd, MAXSERVERMOTD);
	CopyCaretColors(connectedservername, header.servername, MAXSERVERNAME);
	CopyCaretColors(connectedservercontact, header.servercontact, MAXSERVERCONTACT);
	strncpy(connectedserverdescription, header.serverdescription, MAXSERVERDESCRIPTION);

	for (i = 0; i < header.numplayers; i++)
	{
		// oldghost init doesn't work here, players aren't immediately spawned anymore

		// Set saved attribute values
		// No cheat checking here, because even if they ARE wrong...
		// it would only break the replay if we clipped them.
		demoplayer_t *plr = &header.playerdata[i];
		players[plr->playernum].kartspeed = plr->kartspeed;
		players[plr->playernum].kartweight = plr->kartweight;
	}

	demo.deferstart = true;
}

void G_SetupDemoPlayer(int32_t i)
{
	players[i].mo->color = players[i].skincolor;
	oldghost[i].x = players[i].mo->x;
	oldghost[i].y = players[i].mo->y;
	oldghost[i].z = players[i].mo->z;
}

void G_AddGhost(char *defdemoname)
{
	int32_t i;
	lumpnum_t l;
	CLEANUP(Z_Pfree) char *pdemoname = NULL;
	const char *n;
	uint64_t demohash;
	demoghost *gh;
	uint8_t flags;
	CLEANUP(Z_Pfree) uint8_t *buffer = NULL;
	mapthing_t *mthing;
	skin_t *ghskin = skins[0];

	n = defdemoname+strlen(defdemoname);
	while (*n != '/' && *n != '\\' && n != defdemoname)
		n--;
	if (n != defdemoname)
		n++;
	pdemoname = ZZ_Alloc(strlen(n)+1);
	strcpy(pdemoname,n);

	// Internal if no extension, external if one exists
	if (FIL_CheckExtension(defdemoname))
	{
		//FIL_DefaultExtension(defdemoname, ".lmp");
		if (!FIL_ReadFileTag(defdemoname, &buffer, PU_LEVEL))
		{
			CONS_Alert(CONS_ERROR, M_GetText("Failed to read file '%s'.\n"), defdemoname);
			return;
		}
	}
	// load demo resource from WAD
	else if ((l = W_CheckNumForName(defdemoname)) == LUMPERROR)
	{
		CONS_Alert(CONS_ERROR, M_GetText("Failed to read lump '%s'.\n"), defdemoname);
		return;
	}
	else // it's an internal demo
		buffer = W_CacheLumpNum(l, PU_LEVEL);

	CLEANUP(G_FreeDemoHeader) demoheader_t header;
	switch (G_ReadDemoHeader(buffer, &header))
	{
	case HEADER_OK:
		break;

	case HEADER_BADMAGIC:
		CONS_Alert(CONS_NOTICE, M_GetText("Ghost %s: Not a SRB2 replay.\n"), pdemoname);
		return;

	case HEADER_BADVERSION:
		CONS_Alert(CONS_NOTICE, M_GetText("Ghost %s: Demo version incompatible.\n"), pdemoname);
		return;

	case HEADER_BADFORMAT:
		CONS_Alert(CONS_NOTICE, M_GetText("Ghost %s: Demo format unacceptable.\n"), pdemoname);
		return;
	}

	demohash = header.demochecksum;

	for (gh = ghosts; gh; gh = gh->next)
		if (demohash == gh->checksum) // another ghost in the game already has this checksum?
		{ // Don't add another one, then!
			CONS_Debug(DBG_SETUP, "Rejecting duplicate ghost %s (hash was matched)\n", pdemoname);
			return;
		}


	flags = header.demoflags;

	if (!(flags & DF_GHOST))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Ghost %s: No ghost data in this demo.\n"), pdemoname);
		return;
	}

	if (flags & DF_LUAVARS) // can't be arsed to add support for grinding away ported lua material
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Ghost %s: Replay data contains luavars, cannot continue.\n"), pdemoname);
		return;
	}

	if (header.empty)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Failed to add ghost %s: Replay is empty.\n"), pdemoname);
		return;
	}

	// Check record preset.
	{
		strbuf_t *ghostpre = G_GetRecordPresetVersionForDemo(&header);
		strbuf_t *ourpre = G_GetRecordPresetVersion(currentrecordpreset, currentrecordpresetversion);

		if (ghostpre == NULL || !G_CompareRecordPresetVersions(ghostpre, ourpre))
			return;
	}

	demoplayer_t *plr = &header.playerdata[0];

	// any invalidating flags?
	if ((plr->flags & (DEMO_SPECTATOR|DEMO_BOT)) != 0)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Failed to add ghost %s: Invalid player slot (spectator/bot)\n"), defdemoname);
		return;
	}

	uint32_t hash = HASH32(plr->skin, strlen(plr->skin));

	for (i = 0; i < numskins; i++)
	{
		if (skins[i]->namehash != hash)
			continue;

		if (fasticmp(skins[i]->name, plr->skin))
		{
			ghskin = skins[i];
			break;
		}
	}

	if (i == numskins)
	{
		if (plr->kartspeed != UINT8_MAX && plr->kartweight != UINT8_MAX)
			ghskin = skins[GetSkinNumClosestToStats(plr->kartspeed, plr->kartweight)];

		CONS_Alert(CONS_NOTICE, M_GetText("Ghost %s: Invalid character. Falling back to %s.\n"), pdemoname, ghskin->name);
	}

	gh = Z_Calloc(sizeof(demoghost), PU_LEVEL, NULL);
	gh->next = ghosts;
	gh->buffer = buffer;
	gh->checksum = demohash;
	gh->p = buffer + header.endofs;
	buffer = NULL; // buffer can't be freed now!

	ghosts = gh;

	gh->version = header.demoversion;
	mthing = playerstarts[0] ? playerstarts[0] : deathmatchstarts[0]; // todo not correct but out of scope
	I_Assert(mthing);
	{ // A bit more complex than P_SpawnPlayer because ghosts aren't solid and won't just push themselves out of the ceiling.
		fixed_t z,f,c;
		fixed_t offset = mthing->z << FRACBITS;
		gh->mo = P_SpawnMobj(mthing->x << FRACBITS, mthing->y << FRACBITS, 0, MT_GHOST);
		gh->mo->angle = FixedAngle(mthing->angle << FRACBITS);
		f = gh->mo->floorz;
		c = gh->mo->ceilingz - mobjinfo[MT_PLAYER].height;
		if (!!(mthing->args[0]) ^ !!(mthing->options & MTF_OBJECTFLIP))
		{
			z = c - offset;
			if (z < f)
				z = f;
		}
		else
		{
			z = f + offset;
			if (z > c)
				z = c;
		}
		gh->mo->z = z;
	}

	gh->oldmo.x = gh->mo->x;
	gh->oldmo.y = gh->mo->y;
	gh->oldmo.z = gh->mo->z;

	gh->mo->state = states + S_KART_STILL;
	gh->mo->sprite = gh->mo->state->sprite;
	gh->mo->sprite2 = (gh->mo->state->frame & FF_FRAMEMASK);
	//gh->mo->frame = tr_trans30<<FF_TRANSSHIFT;
	gh->mo->renderflags |= RF_DONTDRAW;
	gh->fadein = (9-3)*6; // fade from invisible to trans30 over as close to 35 tics as possible
	gh->mo->tics = -1;

	// Set skin
	R_ApplySkin(&gh->oldmo, ghskin);
	R_ApplySkin(gh->mo, gh->oldmo.skin);

	// Set color
	gh->mo->color = ((skin_t*)gh->mo->skin)->prefcolor;
	for (i = 0; i < numskincolors; i++)
		if (fasticmp(skincolors[i].name, plr->color))
		{
			gh->mo->color = (uint16_t)i;
			break;
		}
	gh->oldmo.color = gh->mo->color;

	CONS_Printf(M_GetText("Added ghost %s from %s\n"), plr->name, pdemoname);
}

// Clean up all ghosts
void G_FreeGhosts(void)
{
	while (ghosts)
	{
		demoghost *next = ghosts->next;
		Z_Free(ghosts);
		ghosts = next;
	}
	ghosts = NULL;
}

// A simplified version of G_AddGhost...
void G_UpdateStaffGhostName(lumpnum_t l)
{
	CLEANUP(Z_Pfree) uint8_t *buffer = W_CacheLumpNum(l, PU_CACHE);

	CLEANUP(G_FreeDemoHeader) demoheader_t header;
	if (G_ReadDemoHeader(buffer, &header) != HEADER_OK)
		return;

	if (!(header.demoflags & DF_GHOST))
		return; // we don't NEED to do it here, but whatever

	if (header.numplayers == 0 || header.playerdata[0].playernum != 0)
		return;
	if (header.playerdata[0].flags & (DEMO_SPECTATOR|DEMO_BOT))
		return;

	strcpy(dummystaffname, header.playerdata[0].name);
}

//
// G_TimeDemo
// NOTE: name is a full filename for external demos
//
static int32_t restorecv_vidwait;

void G_TimeDemo(const char *name)
{
	nodrawers = M_CheckParm("-nodraw");
	noblit = M_CheckParm("-noblit");
	restorecv_vidwait = cv_vidwait.value;
	if (cv_vidwait.value)
		CV_Set(&cv_vidwait, "0");
	demo.timing = true;
	singletics = true;
	framecount = 0;
	demostarttime = I_GetTime();
	G_DeferedPlayDemo(name);
}

void G_DoPlayMetal(void)
{
	lumpnum_t l;
	mobj_t *mo = NULL;
	thinker_t *th;

	// it's an internal demo
	// TODO: Use map header to determine lump name
	if ((l = W_CheckNumForName(va("%sMS",G_BuildMapName(gamemap)))) == LUMPERROR)
	{
		CONS_Alert(CONS_WARNING, M_GetText("No bot recording for this map.\n"));
		return;
	}
	else
		metalbuffer = metal_p = W_CacheLumpNum(l, PU_STATIC);

	// find metal sonic
	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo = (mobj_t *)th;
		if (mo->type != MT_METALSONIC_RACE)
			continue;

		break;
	}
	if (th == &thlist[THINK_MOBJ])
	{
		CONS_Alert(CONS_ERROR, M_GetText("Failed to find bot entity.\n"));
		Z_Free(metalbuffer);
		return;
	}

	// read demo header
	metal_p += 12; // DEMOHEADER
	metal_p++; // VERSION
	metal_p++; // SUBVERSION
	metalversion = READUINT16(metal_p);
	switch(metalversion)
	{
	case DEMOVERSION: // latest always supported
		break;
	// too old, cannot support.
	default:
		CONS_Alert(CONS_WARNING, M_GetText("Failed to load bot recording for this map, format version incompatible.\n"));
		Z_Free(metalbuffer);
		return;
	}
	metal_p += sizeof(uint64_t); // demo checksum
	if (memcmp(metal_p, "METL", 4))
	{
		CONS_Alert(CONS_WARNING, M_GetText("Failed to load bot recording for this map, wasn't recorded in Metal format.\n"));
		Z_Free(metalbuffer);
		return;
	} metal_p += 4; // "METL"

	// read initial tic
	memset(&oldmetal,0,sizeof(oldmetal));
	oldmetal.x = mo->x;
	oldmetal.y = mo->y;
	oldmetal.z = mo->z;
	metalplayback = mo;
}

void G_DoneLevelLoad(void)
{
	CONS_Printf(M_GetText("Loaded level in %f sec\n"), (double)(I_GetTime() - demostarttime) / TICRATE);
	framecount = 0;
	demostarttime = I_GetTime();
}

/*
===================
=
= G_CheckDemoStatus
=
= Called after a death or level completion to allow demos to be cleaned up
= Returns true if a new demo loop action will take place
===================
*/

// Writes the demo's checksum.
static void WriteDemoChecksum(void)
{
	uint8_t *p = demobuf.buffer+16; // checksum position
	WRITEUINT64(p, HASH64(p+16, demobuf.p - (p+16)));
}

// Stops metal sonic's demo. Separate from other functions because metal + replays can coexist
void G_StopMetalDemo(void)
{
	// Metal Sonic finishing doesn't end the game, dammit.
	Z_Free(metalbuffer);
	metalbuffer = NULL;
	metalplayback = NULL;
	metal_p = NULL;
}

// Stops metal sonic recording.
ATTRNORETURN void FUNCNORETURN G_StopMetalRecording(dboolean kill)
{
	dboolean saved = false;
	if (demobuf.p)
	{
		WRITEUINT8(demobuf.p, (kill) ? METALDEATH : DEMOMARKER); // add the demo end (or metal death) marker
		WriteDemoChecksum();
		saved = FIL_WriteFile(va("%sMS.LMP", G_BuildMapName(gamemap)), demobuf.buffer, demobuf.p - demobuf.buffer); // finally output the file.
	}
	P_SaveBufferFree(&demobuf);
	metalrecording = false;
	if (saved)
		I_Error("Saved to %sMS.LMP", G_BuildMapName(gamemap));
	I_Error("Failed to save demo!");
}

// Stops timing a demo.
static void G_StopTimingDemo(void)
{
	int32_t demotime;
	double f1, f2;
	demotime = I_GetTime() - demostarttime;
	if (!demotime)
		return;
	G_StopDemo();
	demo.timing = false;
	f1 = (double)demotime;
	f2 = (double)framecount*TICRATE;

	CONS_Printf(M_GetText("timed %u gametics in %d realtics - %u frames\n%f seconds, %f avg fps\n"),
		leveltime,demotime,(uint32_t)framecount,f1/TICRATE,f2/f1);

	// CSV-readable timedemo results, for external parsing
	if (timedemo_csv)
	{
		FILE *f;
		const char *csvpath = va("%s"PATHSEP"%s", srb2home, "timedemo.csv");
		const char *header = "id,demoname,seconds,avgfps,leveltime,demotime,framecount,ticrate,rendermode,vidmode,vidwidth,vidheight,procbits\n";
		const char *rowformat = "\"%s\",\"%s\",%f,%f,%u,%d,%u,%u,%u,%u,%u,%u,%u\n";
		dboolean headerrow = !FIL_FileExists(csvpath);
		uint8_t procbits = 0;

		// Bitness
		if (sizeof(void*) == 4)
			procbits = 32;
		else if (sizeof(void*) == 8)
			procbits = 64;

		f = fopen(csvpath, "a+");

		if (f)
		{
			if (headerrow)
				fputs(header, f);
			fprintf(f, rowformat,
				timedemo_csv_id,timedemo_name,f1/TICRATE,f2/f1,leveltime,demotime,(uint32_t)framecount,TICRATE,rendermode,vid.modenum,vid.width,vid.height,procbits);
			fclose(f);
			CONS_Printf("Timedemo results saved to '%s'\n", csvpath);
		}
		else
		{
			// Just print the CSV output to console
			CON_LogMessage(header);
			CONS_Printf(rowformat,
				timedemo_csv_id,timedemo_name,f1/TICRATE,f2/f1,leveltime,demotime,(uint32_t)framecount,TICRATE,rendermode,vid.modenum,vid.width,vid.height,procbits);
		}
	}

	if (restorecv_vidwait != cv_vidwait.value)
		CV_SetValue(&cv_vidwait, restorecv_vidwait);
	if (timedemo_quit)
		COM_ImmedExecute("quit");
	else
	{
		M_ClearMenusEx(false, true);
		D_ClearState();
		M_StartControlPanel();
	}
}

// reset engine variable set for the demos
// called from stopdemo command, map command, and g_checkdemoStatus.
void G_StopDemo(void)
{
	P_SaveBufferFree(&demobuf);
	demo.playback = false;
	demo.single_seed = false;
	if (demo.title)
		modeattacking = false;
	demo.timing = false;
	singletics = false;

	{
		uint8_t i;
		for (i = 0; i < MAXSPLITSCREENPLAYERS; ++i)
		{
			camera[i].freecam = false;
		}
	}

	D_ClearState();
}

dboolean G_CheckDemoStatus(void)
{
	G_FreeGhosts();

	// DO NOT end metal sonic demos here

	if (demo.timing)
	{
		G_StopTimingDemo();
		return true;
	}

	if (demo.playback)
	{
		if (demo.quitafterplaying)
			I_Quit();

		if (multiplayer && !demo.title)
			G_FinishExitLevel();
		else
		{
			uint8_t wasmodeattacking = modeattacking;
			dboolean wastitledemo = demo.title;
			G_StopDemo();

			if (timedemo_quit)
				COM_ImmedExecute("quit");
			else if (wastitledemo)
				D_StartTitle();
			else if (wasmodeattacking)
				MR_ModeAttackEndGame(0);
			else
			{
				M_ClearMenusEx(false, true);
				D_ClearState();
				M_StartControlPanel();
			}
		}

		return true;
	}

	if (demo.recording && (modeattacking || demo.savemode != DSM_NOTSAVING))
	{
		if (demobuf.p)
		{
			G_SetAutoSaveName();
			G_SaveDemo();
		}
		return true;
	}

	if (demo.recording)
		P_SaveBufferFree(&demobuf);
	demo.recording = false;
	return false;
}

void G_SetAutoSaveName(void)
{
	if (demo.savemode == DSM_WILLAUTOSAVE)
	{
		// Init just in case it isn't initialized already
		M_TextInputInit(&demo.titlenameinput, demo.titlename, sizeof(demo.titlename));

		// This will indirectly assign to demo.titlename too
		M_TextInputSetString(&demo.titlenameinput, va("%s", cv_dummyreplaysave.string));
	}
}

void G_SaveDemo(void)
{
	uint8_t *p = demobuf.buffer+16; // after version
	uint32_t length;

	// Ensure extrainfo pointer is always available, even if no info is present.
	if (demoinfo_p && *(uint32_t *)demoinfo_p == 0)
	{
		WRITEUINT8(demobuf.p, DEMOMARKER); // add the demo end marker
		*(uint32_t *)demoinfo_p = demobuf.p - demobuf.buffer;
	}
	WRITEUINT8(demobuf.p, DW_END); // Mark end of demo extra data.

	memcpy(p, demo.titlename, 64); // Write demo title here
	p += 64;

	if (multiplayer)
	{
		// Change the demo's name to be a slug of the title
		char demo_slug[128];
		char *writepoint;
		size_t i, strindex = 0;
		dboolean dash = true;

		//for (i = 0; demo.titlename[i] && i < 127; i++) ?????
		for (i = 0; i < 64 && demo.titlename[i]; i++)
		{
			if ((demo.titlename[i] >= 'a' && demo.titlename[i] <= 'z') ||
				(demo.titlename[i] >= '0' && demo.titlename[i] <= '9'))
			{
				demo_slug[strindex] = demo.titlename[i];
				strindex++;
				dash = false;
			}
			else if (demo.titlename[i] >= 'A' && demo.titlename[i] <= 'Z')
			{
				demo_slug[strindex] = demo.titlename[i] + 'a' - 'A';
				strindex++;
				dash = false;
			}
			else if (strindex && !dash)
			{
				demo_slug[strindex] = '-';
				strindex++;
				dash = true;
			}
		}

		if (dash && strindex)
		{
			strindex--;
		}
		demo_slug[strindex] = '\0';

		if (demo_slug[0] != '\0')
		{
			// Slug is valid, write the chosen filename.
			writepoint = strstr(strrchr(demoname, *PATHSEP), "-");
			if (!writepoint)
				return;

			writepoint++;

			size_t flen = 128 - (writepoint - demoname) - 4;

			if (flen > 0 && flen < 128)
			{
				demo_slug[flen] = '\0';
				snprintf(writepoint, sizeof(demoname) - (writepoint - demoname), "%s.lmp", demo_slug);
			}
		}
	}

	length = *(uint32_t *)demoinfo_p;
	WRITEUINT32(demoinfo_p, length);

	// Doesn't seem like I can use WriteDemoChecksum here, correct me if I'm wrong -Sal

	// Make a checksum of everything after the checksum in the file up to the end of the standard data. Extrainfo is freely modifiable.
	WRITEUINT64(p, HASH64(p+16, (demobuf.buffer + length) - (p+16)));

	if (FIL_WriteFile(demoname, demobuf.buffer, demobuf.p - demobuf.buffer)) // finally output the file.
		demo.savemode = DSM_SAVED;
	P_SaveBufferFree(&demobuf);
	demo.recording = false;

	if (!modeattacking)
	{
		if (demo.savemode == DSM_SAVED)
			CONS_Printf(M_GetText("Demo %s recorded\n"), demoname);
		else
			CONS_Alert(CONS_WARNING, M_GetText("Demo %s not saved\n"), demoname);
	}
}

void G_SyncDemoParty(int32_t rem, int32_t newsplitscreen)
{
	int r_splitscreen_copy = r_splitscreen;
	int32_t displayplayers_copy[MAXSPLITSCREENPLAYERS];
	memcpy(displayplayers_copy, displayplayers, sizeof displayplayers);

	// If we switch away from someone's view, that player
	// should be removed from the party.
	// However, it is valid to have the player on multiple
	// viewports.

	// Remove this player
	if (playeringame[rem])
		G_LeaveParty(rem);

	// And reset the rest of the party
	for (int i = 0; i <= r_splitscreen_copy; ++i)
		G_LeaveParty(displayplayers_copy[i]);

	// Restore the party, without the removed player, and
	// with the order matching displayplayers
	for (int i = 0; i <= newsplitscreen; ++i)
		G_JoinParty(consoleplayer, displayplayers_copy[i]);

	// memcpy displayplayers back to preserve duplicates
	// (G_JoinParty will not create duplicates itself)
	r_splitscreen = newsplitscreen;
	memcpy(displayplayers, displayplayers_copy, sizeof displayplayers);
	R_ExecuteSetViewSize();
}
