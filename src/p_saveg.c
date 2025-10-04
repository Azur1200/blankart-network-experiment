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
/// \file  p_saveg.c
/// \brief Archiving: SaveGame I/O

#include "doomdef.h"
#include "byteptr.h"
#include "d_main.h"
#include "doomstat.h"
#include "g_game.h"
#include "m_random.h"
#include "m_misc.h"
#include "p_local.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_saveg.h"
#include "r_data.h"
#include "r_fps.h"
#include "r_textures.h"
#include "r_things.h"
#include "r_skins.h"
#include "r_state.h"
#include "w_wad.h"
#include "y_inter.h"
#include "z_zone.h"
#include "r_main.h"
#include "r_sky.h"
#include "p_polyobj.h"
#include "lua_script.h"
#include "p_slopes.h"

// SRB2Kart
#include "k_battle.h"
#include "k_pwrlv.h"
#include "k_terrain.h"
#include "acs/interface.h"
#include "g_party.h"

#include <tracy/tracy/TracyC.h>

savedata_t savedata;

UINT8 P_SyncUINT8(savebuffer_t *save, UINT8 v)
{
	if (save->write)
	{
		WRITEUINT8(save->p,v);
		return v;
	}
	else
		return READUINT8(save->p);
}

SINT8 P_SyncSINT8(savebuffer_t *save, SINT8 v)
{
	if (save->write)
	{
		WRITESINT8(save->p,v);
		return v;
	}
	else
		return READSINT8(save->p);
}

UINT16 P_SyncUINT16(savebuffer_t *save, UINT16 v)
{
	if (save->write)
	{
		WRITEUINT16(save->p, v);
		return v;
	}
	else
		return READUINT16(save->p);
}

INT16 P_SyncINT16(savebuffer_t *save, INT16 v)
{
	if (save->write)
	{
		WRITEINT16(save->p, v);
		return v;
	}
	else
		return READINT16(save->p);
}

UINT32 P_SyncUINT32(savebuffer_t *save, UINT32 v)
{
	if (save->write)
	{
		WRITEUINT32(save->p, v);
		return v;
	}
	else
		return READUINT32(save->p);
}

INT32 P_SyncINT32(savebuffer_t *save, INT32 v)
{
	if (save->write)
	{
		WRITEINT32(save->p, v);
		return v;
	}
	else
		return READINT32(save->p);
}

char P_SyncChar(savebuffer_t *save, char v)
{
	if (save->write)
	{
		WRITECHAR(save->p, v);
		return v;
	}
	else
		return READCHAR(save->p);
}

fixed_t P_SyncFixed(savebuffer_t *save, fixed_t v)
{
	if (save->write)
	{
		WRITEFIXED(save->p, v);
		return v;
	}
	else
		return READFIXED(save->p);
}

angle_t P_SyncAngle(savebuffer_t *save, angle_t v)
{
	if (save->write)
	{
		WRITEANGLE(save->p, v);
		return v;
	}
	else
		return READANGLE(save->p);
}

void P_SyncStringN(savebuffer_t *save, char *s, size_t n)
{
	if (save->write)
		WRITESTRINGN(save->p, s, n);
	else
		READSTRINGN(save->p, s, n);
}

void P_SyncStringL(savebuffer_t *save, char *s, size_t n)
{
	if (save->write)
		WRITESTRINGL(save->p, s, n);
	else
		READSTRINGL(save->p, s, n);
}

void P_SyncString(savebuffer_t *save, char *s)
{
	if (save->write)
		WRITESTRING(save->p, s);
	else
		READSTRING(save->p, s);
}

void P_SyncMem(savebuffer_t *save, void *s, size_t n)
{
	if (save->write)
		WRITEMEM(save->p, s, n);
	else
		READMEM(save->p, s, n);
}

// Block UINT32s to attempt to ensure that the correct data is
// being sent and received
#define ARCHIVEBLOCK_MISC     0x7FEEDEED
#define ARCHIVEBLOCK_PLAYERS  0x7F448008
#define ARCHIVEBLOCK_PARTIES  0x7F87AF0C
#define ARCHIVEBLOCK_WORLD    0x7F8C08C0
#define ARCHIVEBLOCK_POBJS    0x7F928546
#define ARCHIVEBLOCK_THINKERS 0x7F37037C
#define ARCHIVEBLOCK_SPECIALS 0x7F228378
#define ARCHIVEBLOCK_WAYPOINTS 0x7F46498F

// Note: This cannot be bigger
// than an UINT16
typedef enum
{
	AWAYVIEW   = 0x01,
	FOLLOWITEM = 0x02,
	FOLLOWER   = 0x04,
} player_saveflags;

static inline void P_ArchivePlayer(savebuffer_t *save)
{
	const player_t *player = &players[consoleplayer];
	INT16 skininfo = player->skin;
	SINT8 pllives = player->lives;
	if (pllives < startinglivesbalance[numgameovers]) // Bump up to 3 lives if the player
		pllives = startinglivesbalance[numgameovers]; // has less than that.

	WRITEUINT16(save->p, skininfo);
	WRITEUINT8(save->p, numgameovers);
	WRITESINT8(save->p, pllives);
	WRITEUINT32(save->p, player->score);
}

static inline void P_UnArchivePlayer(savebuffer_t *save)
{
	INT16 skininfo = READUINT16(save->p);
	savedata.skin = skininfo;

	savedata.numgameovers = READUINT8(save->p);
	savedata.lives = READSINT8(save->p);
	savedata.score = READUINT32(save->p);
}

static void P_NetSyncPlayers(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	INT32 i, j;
	UINT16 flags;

	if (P_SyncUINT32(save, ARCHIVEBLOCK_PLAYERS) != ARCHIVEBLOCK_PLAYERS)
		I_Error("Bad $$$.sav at archive block Players");

	for (i = 0; i < MAXPLAYERS; i++)
	{
		adminplayers[i] = (INT32)P_SyncSINT8(save, adminplayers[i]);

		for (j = 0; j < PWRLV_NUMTYPES; j++)
		{
			clientpowerlevels[i][j] = P_SyncINT16(save, clientpowerlevels[i][j]);
		}
		clientPowerAdd[i] = P_SyncINT16(save, clientPowerAdd[i]);

		if (!playeringame[i])
			continue;

		flags = 0;

		// no longer send ticcmds

		P_SyncStringN(save, player_names[i], MAXPLAYERNAME);

		playerconsole[i] = P_SyncUINT8(save, playerconsole[i]);
		splitscreen_invitations[i] = P_SyncINT32(save, splitscreen_invitations[i]);

		players[i].angleturn = P_SyncAngle(save, players[i].angleturn);
		players[i].aiming = P_SyncAngle(save, players[i].aiming);
		players[i].drawangle = P_SyncAngle(save, players[i].drawangle);
		players[i].viewrollangle = P_SyncAngle(save, players[i].viewrollangle);
		players[i].tilt = P_SyncAngle(save, players[i].tilt);
		players[i].awayviewaiming = P_SyncAngle(save, players[i].awayviewaiming);
		players[i].awayviewtics = P_SyncINT32(save, players[i].awayviewtics);

		players[i].playerstate = P_SyncUINT8(save, players[i].playerstate);
		players[i].pflags = P_SyncUINT32(save, players[i].pflags);
		players[i].panim = P_SyncUINT8(save, players[i].panim);
		players[i].spectator = P_SyncUINT8(save, players[i].spectator);
		players[i].spectatewait = P_SyncUINT32(save, players[i].spectatewait);

		players[i].flashpal = P_SyncUINT16(save, players[i].flashpal);
		players[i].flashcount = P_SyncUINT16(save, players[i].flashcount);

		players[i].skincolor = P_SyncUINT8(save, players[i].skincolor);
		players[i].skin = P_SyncINT32(save, players[i].skin);
		players[i].availabilities = P_SyncUINT32(save, players[i].availabilities);
		players[i].score = P_SyncUINT32(save, players[i].score);
		players[i].lives = P_SyncSINT8(save, players[i].lives);
		players[i].xtralife = P_SyncSINT8(save, players[i].xtralife);
		players[i].speed = P_SyncFixed(save, players[i].speed);
		players[i].lastspeed = P_SyncFixed(save, players[i].lastspeed);
		players[i].deadtimer = P_SyncINT32(save, players[i].deadtimer);
		players[i].exiting = P_SyncUINT32(save, players[i].exiting);

		////////////////////////////
		// Conveyor Belt Movement //
		////////////////////////////
		players[i].cmomx = P_SyncFixed(save, players[i].cmomx); // Conveyor momx
		players[i].cmomy = P_SyncFixed(save, players[i].cmomy); // Conveyor momy
		players[i].rmomx = P_SyncFixed(save, players[i].rmomx); // "Real" momx (momx - cmomx)
		players[i].rmomy = P_SyncFixed(save, players[i].rmomy); // "Real" momy (momy - cmomy)

		players[i].totalring = P_SyncINT16(save, players[i].totalring);
		players[i].realtime = P_SyncUINT32(save, players[i].realtime);
		for (j = 0; j < LAP__MAX; j++)
		{
			players[i].laptime[j] = P_SyncUINT32(save, players[i].laptime[j]);
		}
		players[i].laps = P_SyncUINT8(save, players[i].laps);
		players[i].latestlap = P_SyncUINT8(save, players[i].latestlap);

		players[i].starposttime = P_SyncUINT32(save, players[i].starposttime);
		players[i].starpostx = P_SyncINT16(save, players[i].starpostx);
		players[i].starposty = P_SyncINT16(save, players[i].starposty);
		players[i].starpostz = P_SyncINT16(save, players[i].starpostz);
		players[i].starpostnum = P_SyncINT32(save, players[i].starpostnum);
		players[i].starpostangle = P_SyncAngle(save, players[i].starpostangle);
		players[i].starpostflip = P_SyncUINT8(save, (UINT8)players[i].starpostflip);
		players[i].prevcheck = P_SyncINT32(save, players[i].prevcheck);
		players[i].nextcheck = P_SyncINT32(save, players[i].nextcheck);

		players[i].ctfteam = P_SyncUINT8(save, players[i].ctfteam);

		players[i].checkskip = P_SyncUINT8(save, players[i].checkskip);

		players[i].lastsidehit = P_SyncINT16(save, players[i].lastsidehit);
		players[i].lastlinehit = P_SyncINT16(save, players[i].lastlinehit);

		players[i].onconveyor = P_SyncINT32(save, players[i].onconveyor);

		players[i].jointime = P_SyncUINT32(save, players[i].jointime);
		players[i].spectatorreentry = P_SyncUINT32(save, players[i].spectatorreentry);

		players[i].grieftime = P_SyncUINT32(save, players[i].grieftime);
		players[i].griefstrikes = P_SyncUINT8(save, players[i].griefstrikes);

		players[i].splitscreenindex = P_SyncUINT8(save, players[i].splitscreenindex);

		if (save->write)
		{
			if (players[i].awayviewmobj)
				flags |= AWAYVIEW;

			if (players[i].followmobj)
				flags |= FOLLOWITEM;

			if (players[i].follower)
				flags |= FOLLOWER;

			WRITEUINT16(save->p, flags);

			if (flags & AWAYVIEW)
				WRITEUINT32(save->p, players[i].awayviewmobj->mobjnum);

			if (flags & FOLLOWITEM)
				WRITEUINT32(save->p, players[i].followmobj->mobjnum);
		}
		else
		{
			flags = READUINT16(save->p);

			if (flags & AWAYVIEW)
				players[i].awayviewmobj = (mobj_t *)(size_t)READUINT32(save->p);

			if (flags & FOLLOWITEM)
				players[i].followmobj = (mobj_t *)(size_t)READUINT32(save->p);
		}

		players[i].followitem = P_SyncUINT32(save, (UINT32)players[i].followitem);

		players[i].charflags = P_SyncUINT32(save, players[i].charflags);

		// SRB2kart
		players[i].kartspeed = P_SyncUINT8(save, players[i].kartspeed);
		players[i].kartweight = P_SyncUINT8(save, players[i].kartweight);

		for (j = 0; j < MAXPREDICTTICS; j++)
		{
			players[i].lturn_max[j] = P_SyncINT16(save, players[i].lturn_max[j]);
			players[i].rturn_max[j] = P_SyncINT16(save, players[i].rturn_max[j]);
		}

		players[i].followerskin = P_SyncUINT8(save, players[i].followerskin);
		players[i].followerready = P_SyncUINT8(save, players[i].followerready);	// booleans are really just numbers eh??
		players[i].followercolor = P_SyncUINT16(save, players[i].followercolor);

		if (save->write)
		{
			if (flags & FOLLOWER)
				WRITEUINT32(save->p, players[i].follower->mobjnum);
		}
		else
		{
			if (flags & FOLLOWER)
				players[i].follower->mobjnum = READUINT32(save->p);
		}

		for (j = 0; j < NUMPOWERS; j++)
			players[i].powers[j] = P_SyncUINT16(save, players[i].powers[j]);
		for (j = 0; j < NUMKARTSTUFF; j++)
			players[i].kartstuff[j] = P_SyncINT32(save, players[i].kartstuff[j]);

		players[i].nocontrol = P_SyncUINT16(save, players[i].nocontrol);
		players[i].carry = P_SyncUINT8(save, players[i].carry);
		players[i].dye = P_SyncUINT16(save, players[i].dye);

		players[i].position = P_SyncUINT8(save, players[i].position);
		players[i].oldposition = P_SyncUINT8(save, players[i].oldposition);
		players[i].positiondelay = P_SyncUINT8(save, players[i].positiondelay);
		players[i].distancetofinish = P_SyncUINT32(save, players[i].distancetofinish);
		players[i].distancetofinishprev = P_SyncUINT32(save, players[i].distancetofinishprev);
		if (save->write)
		{
			WRITEUINT32(save->p, K_GetWaypointHeapIndex(players[i].currentwaypoint));
			WRITEUINT32(save->p, K_GetWaypointHeapIndex(players[i].nextwaypoint));
		}
		else
		{
			players[i].currentwaypoint = (waypoint_t *)(size_t)READUINT32(save->p);
			players[i].nextwaypoint = (waypoint_t *)(size_t)READUINT32(save->p);
		}
		players[i].bigwaypointgap = P_SyncUINT16(save, players[i].bigwaypointgap);
		players[i].airtime = P_SyncUINT32(save, players[i].airtime);
		players[i].startboost = P_SyncUINT8(save, players[i].startboost);

		players[i].dropdash = P_SyncUINT8(save, players[i].dropdash);
		players[i].respawn = P_SyncUINT8(save, players[i].respawn);

		players[i].flashing = P_SyncUINT16(save, players[i].flashing);
		players[i].spinouttimer = P_SyncUINT16(save, players[i].spinouttimer);
		players[i].spinoutrot = P_SyncINT32(save, players[i].spinoutrot);
		players[i].spinouttype = P_SyncUINT8(save, players[i].spinouttype);
		players[i].flipovertimer = P_SyncUINT16(save, players[i].flipovertimer);
		players[i].flipoverangle = P_SyncAngle(save, players[i].flipoverangle);
		players[i].squishedtimer = P_SyncINT16(save, players[i].squishedtimer);
		players[i].instashield = P_SyncUINT8(save, players[i].instashield);
		players[i].wipeoutslow = P_SyncUINT8(save, players[i].wipeoutslow);
		players[i].justbumped = P_SyncUINT8(save, players[i].justbumped);
		players[i].noclip = P_SyncUINT8(save, players[i].noclip);

		players[i].drift = P_SyncSINT8(save, players[i].drift);
		players[i].driftlock = P_SyncSINT8(save, players[i].driftlock);
		players[i].driftcharge = P_SyncFixed(save, players[i].driftcharge);
		players[i].driftboost = P_SyncUINT8(save, players[i].driftboost);

		players[i].aizdriftstrat = P_SyncSINT8(save, players[i].aizdriftstrat);
		players[i].aizdrifttilt = P_SyncINT32(save, players[i].aizdrifttilt);
		players[i].aizdriftturn = P_SyncINT32(save, players[i].aizdriftturn);
		players[i].slipdashcharge = P_SyncFixed(save, players[i].slipdashcharge);
		players[i].slipdashdir = P_SyncSINT8(save, players[i].slipdashdir);

		players[i].offroad = P_SyncFixed(save, players[i].offroad);
		players[i].tiregrease = P_SyncFixed(save, players[i].tiregrease);
		players[i].pogospring = P_SyncUINT8(save, players[i].pogospring);
		players[i].brakestop = P_SyncUINT8(save, players[i].brakestop);
		players[i].waterskip = P_SyncUINT8(save, players[i].waterskip);

		players[i].dashpadcooldown = P_SyncUINT8(save, players[i].dashpadcooldown);

		players[i].boostpower = P_SyncFixed(save, players[i].boostpower);
		players[i].speedboost = P_SyncFixed(save, players[i].speedboost);
		players[i].prevspeedboost = P_SyncFixed(save, players[i].prevspeedboost);
		players[i].accelboost = P_SyncFixed(save, players[i].accelboost);
		players[i].boostangle = P_SyncAngle(save, players[i].boostangle);
		players[i].numsneakers = P_SyncUINT8(save, players[i].numsneakers);
		players[i].numboosts = P_SyncUINT8(save, players[i].numboosts);

		players[i].draftpower = P_SyncFixed(save, players[i].draftpower);
		players[i].draftleeway = P_SyncUINT16(save, players[i].draftleeway);
		players[i].lastdraft = P_SyncSINT8(save, players[i].lastdraft);

		// boostinfo_t
		players[i].boostinfo.stackspeedboost = P_SyncFixed(save, players[i].boostinfo.stackspeedboost);
		players[i].boostinfo.nonstackspeedboost = P_SyncFixed(save, players[i].boostinfo.nonstackspeedboost);
		players[i].boostinfo.accelboost = P_SyncFixed(save, players[i].boostinfo.accelboost);
		players[i].boostinfo.grade = P_SyncUINT8(save, players[i].boostinfo.grade);

		players[i].tripwireState = P_SyncUINT8(save, players[i].tripwireState);
		players[i].tripwirePass = P_SyncUINT8(save, players[i].tripwirePass);
		players[i].tripwireLeniency = P_SyncUINT16(save, players[i].tripwireLeniency);
		players[i].tripwireReboundDelay = P_SyncUINT8(save, players[i].tripwireReboundDelay);

		players[i].itemroulette = P_SyncUINT16(save, players[i].itemroulette);
		players[i].previtemroulette = P_SyncUINT16(save, players[i].previtemroulette);
		players[i].itemblink = P_SyncUINT16(save, players[i].itemblink);
		players[i].itemblinkmode = P_SyncUINT16(save, players[i].itemblinkmode);
		players[i].roulettetype = P_SyncUINT8(save, players[i].roulettetype);

		players[i].itemtype = P_SyncSINT8(save, players[i].itemtype);
		players[i].itemamount = P_SyncUINT8(save, players[i].itemamount);
		players[i].throwdir = P_SyncSINT8(save, players[i].throwdir);

		players[i].sadtimer = P_SyncUINT8(save, players[i].sadtimer);

		players[i].rings = P_SyncSINT8(save, players[i].rings);
		players[i].ringmin = P_SyncSINT8(save, players[i].ringmin);
		players[i].ringmax = P_SyncSINT8(save, players[i].ringmax);
		players[i].pickuprings = P_SyncUINT8(save, players[i].pickuprings);
		players[i].ringdelay = P_SyncUINT8(save, players[i].ringdelay);
		players[i].ringlock = P_SyncUINT8(save, players[i].ringlock);
		players[i].ringboost = P_SyncUINT16(save, players[i].ringboost);
		players[i].ringtime = P_SyncUINT16(save, players[i].ringtime);
		players[i].superring = P_SyncUINT16(save, players[i].superring);
		players[i].nextringaward = P_SyncUINT8(save, players[i].nextringaward);
		players[i].ringvolume = P_SyncUINT8(save, players[i].ringvolume);
		players[i].ringtransparency = P_SyncUINT8(save, players[i].ringtransparency);

		players[i].airdroptime = P_SyncUINT8(save, players[i].airdroptime);
		players[i].ringdrop = P_SyncUINT8(save, players[i].ringdrop);

		players[i].curshield = P_SyncUINT8(save, players[i].curshield);

		players[i].bubblecool = P_SyncUINT8(save, players[i].bubblecool);
		players[i].bubbleblowup = P_SyncUINT8(save, players[i].bubbleblowup);
		players[i].bubblehealth = P_SyncUINT8(save, players[i].bubblehealth);
		players[i].bubbleboost = P_SyncUINT16(save, players[i].bubbleboost);

		players[i].flamedash = P_SyncUINT16(save, players[i].flamedash);
		players[i].flametimer = P_SyncINT32(save, players[i].flametimer);
		players[i].flamestore = P_SyncUINT8(save, players[i].flamestore);

		players[i].hyudorotimer = P_SyncUINT16(save, players[i].hyudorotimer);
		players[i].stealingtimer = P_SyncSINT8(save, players[i].stealingtimer);

		players[i].sneakertimer = P_SyncUINT16(save, players[i].sneakertimer);
		players[i].realsneakertimer = P_SyncUINT16(save, players[i].realsneakertimer);
		players[i].floorboost = P_SyncUINT8(save, players[i].floorboost);
		players[i].chaintimer = P_SyncUINT16(save, players[i].chaintimer);

		players[i].boostcharge = P_SyncUINT8(save, players[i].boostcharge);

		players[i].slopeboost = P_SyncFixed(save, players[i].slopeboost);
		players[i].prevslopeboost = P_SyncFixed(save, players[i].prevslopeboost);
		players[i].slopeaccel = P_SyncFixed(save, players[i].slopeaccel);

		players[i].growshrinktimer = P_SyncINT16(save, players[i].growshrinktimer);
		players[i].growcancel = P_SyncINT16(save, players[i].growcancel);

		players[i].rocketsneakertimer = P_SyncUINT16(save, players[i].rocketsneakertimer);

		players[i].invincibilitytimer = P_SyncUINT16(save, players[i].invincibilitytimer);
		players[i].maxinvincibilitytime = P_SyncUINT16(save, players[i].maxinvincibilitytime);
		players[i].invincibilitybottleneck = P_SyncUINT16(save, players[i].invincibilitybottleneck);
		players[i].invincibilitycancel = P_SyncINT16(save, players[i].invincibilitycancel);

		players[i].eggmanexplode = P_SyncUINT8(save, players[i].eggmanexplode);
		players[i].eggmanblame = P_SyncSINT8(save, players[i].eggmanblame);

		players[i].bananadrag = P_SyncUINT16(save, players[i].bananadrag);

		players[i].lastjawztarget = P_SyncSINT8(save, players[i].lastjawztarget);
		players[i].jawztargetdelay = P_SyncUINT8(save, players[i].jawztargetdelay);

		players[i].confirmVictim = P_SyncUINT8(save, players[i].confirmVictim);
		players[i].confirmVictimDelay = P_SyncUINT8(save, players[i].confirmVictimDelay);

		players[i].interpoints = P_SyncINT32(save, players[i].interpoints);
		players[i].roundscore = P_SyncUINT32(save, players[i].roundscore);
		players[i].emeralds = P_SyncUINT8(save, players[i].emeralds);
		players[i].bumper = P_SyncUINT8(save, players[i].bumper);
		players[i].karmadelay = P_SyncINT16(save, players[i].karmadelay);
		players[i].karmamode = P_SyncINT16(save, players[i].karmamode);
		players[i].karmapoints = P_SyncINT16(save, players[i].karmapoints);
		players[i].wanted = P_SyncINT16(save, players[i].wanted);

		players[i].glanceDir = P_SyncSINT8(save, players[i].glanceDir);

		players[i].breathTimer = P_SyncUINT16(save, players[i].breathTimer);

		players[i].lastsafelap = P_SyncUINT8(save, players[i].lastsafelap);
		players[i].lastsafestarpost = P_SyncUINT8(save, players[i].lastsafestarpost);

		players[i].typing_timer = P_SyncUINT8(save, players[i].typing_timer);
		players[i].typing_duration = P_SyncUINT8(save, players[i].typing_duration);

		players[i].kickstartaccel = P_SyncUINT8(save, players[i].kickstartaccel);

		players[i].itemflags = P_SyncUINT8(save, players[i].itemflags);

		// botvars_t
		players[i].bot = P_SyncUINT8(save, players[i].bot);
		players[i].botvars.style = P_SyncUINT8(save, players[i].botvars.style);
		players[i].botvars.difficulty = P_SyncUINT8(save, players[i].botvars.difficulty);
		players[i].botvars.diffincrease = P_SyncUINT8(save, players[i].botvars.diffincrease);
		players[i].botvars.rival = P_SyncUINT8(save, players[i].botvars.rival);
		players[i].botvars.rubberband = P_SyncFixed(save, players[i].botvars.rubberband);
		players[i].botvars.controller = P_SyncUINT16(save, players[i].botvars.controller);

		players[i].outrun = P_SyncFixed(save, players[i].outrun);
		players[i].outruntime = P_SyncUINT8(save, players[i].outruntime);

		// sonicloopsvars_t
		players[i].loop.radius = P_SyncFixed(save, players[i].loop.radius);
		players[i].loop.revolution = P_SyncFixed(save, players[i].loop.revolution);
		players[i].loop.min_revolution = P_SyncFixed(save, players[i].loop.min_revolution);
		players[i].loop.max_revolution = P_SyncFixed(save, players[i].loop.max_revolution);
		players[i].loop.yaw = P_SyncAngle(save, players[i].loop.yaw);
		players[i].loop.origin.x = P_SyncFixed(save, players[i].loop.origin.x);
		players[i].loop.origin.y = P_SyncFixed(save, players[i].loop.origin.y);
		players[i].loop.origin.z = P_SyncFixed(save, players[i].loop.origin.z);
		players[i].loop.origin_shift.x = P_SyncFixed(save, players[i].loop.origin_shift.x);
		players[i].loop.origin_shift.y = P_SyncFixed(save, players[i].loop.origin_shift.y);
		players[i].loop.shift.x = P_SyncFixed(save, players[i].loop.shift.x);
		players[i].loop.shift.y = P_SyncFixed(save, players[i].loop.shift.y);
		players[i].loop.flip = P_SyncUINT8(save, players[i].loop.flip);

		// sonicloopcamvars_t
		players[i].loop.camera.enter_tic = P_SyncUINT32(save, players[i].loop.camera.enter_tic);
		players[i].loop.camera.exit_tic = P_SyncUINT32(save, players[i].loop.camera.exit_tic);
		players[i].loop.camera.zoom_in_speed = P_SyncUINT32(save, players[i].loop.camera.zoom_in_speed);
		players[i].loop.camera.zoom_out_speed = P_SyncUINT32(save, players[i].loop.camera.zoom_out_speed);
		players[i].loop.camera.dist = P_SyncFixed(save, players[i].loop.camera.dist);
		players[i].loop.camera.pan = P_SyncAngle(save, players[i].loop.camera.pan);
		players[i].loop.camera.pan_speed = P_SyncFixed(save, players[i].loop.camera.pan_speed);
		players[i].loop.camera.pan_accel = P_SyncUINT32(save, players[i].loop.camera.pan_accel);
		players[i].loop.camera.pan_back = P_SyncUINT32(save, players[i].loop.camera.pan_back);

		// Restored NiGHTS stuff
		players[i].bumpertime = P_SyncUINT32(save, players[i].bumpertime);
		players[i].linkcount = P_SyncINT32(save, players[i].linkcount);
		players[i].linktimer = P_SyncUINT32(save, players[i].linktimer);
		players[i].maxlink = P_SyncINT32(save, players[i].maxlink);

		// Fix janky landing particle
		players[i].prevonground = P_SyncUINT8(save, players[i].prevonground);

		// Wall Transfer bonus
		players[i].walltransfered = P_SyncUINT8(save, players[i].walltransfered);
		players[i].walltransferboost = P_SyncUINT8(save, players[i].walltransferboost);

	}
	TracyCZoneEnd(__zone);
}

static void P_NetSyncParties(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	INT32 i, k;
	UINT8 partySize;

	if (P_SyncUINT32(save, ARCHIVEBLOCK_PARTIES) != ARCHIVEBLOCK_PARTIES)
		I_Error("Bad $$$.sav at archive block Parties");

	if (!save->write)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			G_DestroyParty(i);
			G_BuildLocalSplitscreenParty(i);
		}
	}

	if (save->write)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			partySize = G_PartySize(i);

			WRITEUINT8(save->p, partySize);

			for (k = 0; k < partySize; ++k)
			{
				WRITEUINT8(save->p, G_PartyMember(i, k));
			}
		}
	}
	else
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			partySize = READUINT8(save->p);

			for (k = 0; k < partySize; ++k)
			{
				G_JoinParty(i, READUINT8(save->p));
			}
		}
	}

	TracyCZoneEnd(__zone);
}

///
/// Colormaps
///

static extracolormap_t *net_colormaps = NULL;
static UINT32 num_net_colormaps = 0;
static UINT32 num_ffloors = 0; // for loading

// Copypasta from r_data.c AddColormapToList
// But also check for equality and return the matching index
static UINT32 CheckAddNetColormapToList(extracolormap_t *extra_colormap)
{
	extracolormap_t *exc, *exc_prev = NULL;
	UINT32 i = 0;

	if (!net_colormaps)
	{
		net_colormaps = R_CopyColormap(extra_colormap, false);
		net_colormaps->next = 0;
		net_colormaps->prev = 0;
		num_net_colormaps = i+1;
		return i;
	}

	for (exc = net_colormaps; exc; exc_prev = exc, exc = exc->next)
	{
		if (R_CheckEqualColormaps(exc, extra_colormap, true, true, true))
			return i;
		i++;
	}

	exc_prev->next = R_CopyColormap(extra_colormap, false);
	extra_colormap->prev = exc_prev;
	extra_colormap->next = 0;

	num_net_colormaps = i+1;
	return i;
}

static extracolormap_t *GetNetColormapFromList(UINT32 index)
{
	// For loading, we have to be tricky:
	// We load the sectors BEFORE knowing the colormap values
	// So if an index doesn't exist, fill our list with dummy colormaps
	// until we get the index we want
	// Then when we load the color data, we set up the dummy colormaps

	extracolormap_t *exc, *last_exc = NULL;
	UINT32 i = 0;

	if (!net_colormaps) // initialize our list
		net_colormaps = R_CreateDefaultColormap(false);

	for (exc = net_colormaps; exc; last_exc = exc, exc = exc->next)
	{
		if (i++ == index)
			return exc;
	}


	// LET'S HOPE that index is a sane value, because we create up to [index]
	// entries in net_colormaps. At this point, we don't know
	// what the total colormap count is
	if (index >= numsectors*3 + num_ffloors)
		// if every sector had a unique colormap change AND a fade color thinker which has two colormap entries
		// AND every ffloor had a fade FOF thinker with one colormap entry
		I_Error("Colormap %d from server is too high for sectors %d", index, (UINT32)numsectors);

	// our index doesn't exist, so just make the entry
	for (; i <= index; i++)
	{
		exc = R_CreateDefaultColormap(false);
		if (last_exc)
			last_exc->next = exc;
		exc->prev = last_exc;
		exc->next = NULL;
		last_exc = exc;
	}
	return exc;
}

static void ClearNetColormaps(void)
{
	// We're actually Z_Freeing each entry here,
	// so don't call this in P_NetUnArchiveColormaps (where entries will be used in-game)
	extracolormap_t *exc, *exc_next;

	for (exc = net_colormaps; exc; exc = exc_next)
	{
		exc_next = exc->next;
		Z_Free(exc);
	}
	num_net_colormaps = 0;
	num_ffloors = 0;
	net_colormaps = NULL;
}

static void P_NetArchiveColormaps(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	// We save and then we clean up our colormap mess
	extracolormap_t *exc, *exc_next;
	UINT32 i = 0;
	WRITEUINT32(save->p, num_net_colormaps); // save for safety

	for (exc = net_colormaps; i < num_net_colormaps; i++, exc = exc_next)
	{
		// We must save num_net_colormaps worth of data
		// So fill non-existent entries with default.
		if (!exc)
			exc = R_CreateDefaultColormap(false);

		WRITEUINT8(save->p, exc->fadestart);
		WRITEUINT8(save->p, exc->fadeend);
		WRITEUINT8(save->p, exc->flags);

		WRITEINT32(save->p, exc->rgba);
		WRITEINT32(save->p, exc->fadergba);

#ifdef EXTRACOLORMAPLUMPS
		WRITESTRINGN(save->p, exc->lumpname, 9);
#endif

		exc_next = exc->next;
		Z_Free(exc); // don't need anymore
	}

	num_net_colormaps = 0;
	num_ffloors = 0;
	net_colormaps = NULL;

	TracyCZoneEnd(__zone);
}

static void P_NetUnArchiveColormaps(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	// When we reach this point, we already populated our list with
	// dummy colormaps. Now that we are loading the color data,
	// set up the dummies.
	extracolormap_t *exc, *existing_exc, *exc_next = NULL;
	UINT32 i = 0;

	num_net_colormaps = READUINT32(save->p);

	for (exc = net_colormaps; i < num_net_colormaps; i++, exc = exc_next)
	{
		UINT8 fadestart, fadeend, flags;
		INT32 rgba, fadergba;
#ifdef EXTRACOLORMAPLUMPS
		char lumpname[9];
#endif

		fadestart = READUINT8(save->p);
		fadeend = READUINT8(save->p);
		flags = READUINT8(save->p);

		rgba = READINT32(save->p);
		fadergba = READINT32(save->p);

#ifdef EXTRACOLORMAPLUMPS
		READSTRINGN(save->p, lumpname, 9);

		if (lumpname[0])
		{
			if (!exc)
				// no point making a new entry since nothing points to it,
				// but we needed to read the data so now continue
				continue;

			exc_next = exc->next; // this gets overwritten during our operations here, so get it now
			existing_exc = R_ColormapForName(lumpname);
			*exc = *existing_exc;
			R_AddColormapToList(exc); // see HACK note below on why we're adding duplicates
			continue;
		}
#endif

		if (!exc)
			// no point making a new entry since nothing points to it,
			// but we needed to read the data so now continue
			continue;

		exc_next = exc->next; // this gets overwritten during our operations here, so get it now

		exc->fadestart = fadestart;
		exc->fadeend = fadeend;
		exc->flags = flags;

		exc->rgba = rgba;
		exc->fadergba = fadergba;

#ifdef EXTRACOLORMAPLUMPS
		exc->lump = LUMPERROR;
		exc->lumpname[0] = 0;
#endif

		existing_exc = R_GetColormapFromListByValues(rgba, fadergba, fadestart, fadeend, flags);

		if (existing_exc)
			exc->colormap = existing_exc->colormap;
		else
			// CONS_Debug(DBG_RENDER, "Creating Colormap: rgba(%d,%d,%d,%d) fadergba(%d,%d,%d,%d)\n",
			// 	R_GetRgbaR(rgba), R_GetRgbaG(rgba), R_GetRgbaB(rgba), R_GetRgbaA(rgba),
			//	R_GetRgbaR(fadergba), R_GetRgbaG(fadergba), R_GetRgbaB(fadergba), R_GetRgbaA(fadergba));
			exc->colormap = R_CreateLightTable(exc);

		// HACK: If this dummy is a duplicate, we're going to add it
		// to the extra_colormaps list anyway. I think this is faster
		// than going through every loaded sector and correcting their
		// colormap address to the pre-existing one, PER net_colormap entry
		R_AddColormapToList(exc);

		if (i < num_net_colormaps-1 && !exc_next)
			exc_next = R_CreateDefaultColormap(false);
	}

	// if we still have a valid net_colormap after iterating up to num_net_colormaps,
	// some sector had a colormap index higher than num_net_colormaps. We done goofed or $$$ was corrupted.
	// In any case, add them to the colormap list too so that at least the sectors' colormap
	// addresses are valid and accounted properly
	if (exc_next)
	{
		existing_exc = R_GetDefaultColormap();
		for (exc = exc_next; exc; exc = exc->next)
		{
			exc->colormap = existing_exc->colormap; // all our dummies are default values
			R_AddColormapToList(exc);
		}
	}

	// Don't need these anymore
	num_net_colormaps = 0;
	num_ffloors = 0;
	net_colormaps = NULL;

	TracyCZoneEnd(__zone);
}

///
/// World Archiving
///

#define SD_FLOORHT  0x01
#define SD_CEILHT   0x02
#define SD_FLOORPIC 0x04
#define SD_CEILPIC  0x08
#define SD_LIGHT    0x10
#define SD_SPECIAL  0x20
#define SD_DIFF2    0x40
#define SD_FFLOORS  0x80

// diff2 flags
#define SD_FXOFFS    0x01
#define SD_FYOFFS    0x02
#define SD_CXOFFS    0x04
#define SD_CYOFFS    0x08
#define SD_FLOORANG  0x10
#define SD_CEILANG   0x20
#define SD_TAG       0x40
#define SD_DIFF3     0x80

// diff3 flags
#define SD__UNUSED   0x01
#define SD_COLORMAP  0x02
#define SD_CRUMBLESTATE 0x04
#define SD_FLOORLIGHT 0x08
#define SD_CEILLIGHT 0x10
#define SD_FLAG      0x20
#define SD_SPECIALFLAG 0x40
#define SD_DIFF4     0x80

//diff4 flags
#define SD_DAMAGETYPE 0x01
#define SD_TRIGGERTAG 0x02
#define SD_TRIGGERER  0x04
#define SD_GRAVITY    0x08
#define SD_ACTION     0x10
#define SD_ARGS       0x20
#define SD_STRINGARGS 0x40
#define SD_DIFF5      0x80

//diff5 flags
#define SD_ACTIVATION 0x01
#define SD_BOTCONTROLLER 0x02

static boolean P_SectorArgsEqual(const sector_t *sc, const sector_t *spawnsc)
{
	UINT8 i;
	for (i = 0; i < NUM_SCRIPT_ARGS; i++)
		if (sc->args[i] != spawnsc->args[i])
			return false;

	return true;
}

static boolean P_SectorStringArgsEqual(const sector_t *sc, const sector_t *spawnsc)
{
	UINT8 i;
	for (i = 0; i < NUM_SCRIPT_STRINGARGS; i++)
	{
		if (!sc->stringargs[i])
			return !spawnsc->stringargs[i];

		if (strcmp(sc->stringargs[i], spawnsc->stringargs[i]))
			return false;
	}

	return true;
}

#define LD_FLAG     0x01
#define LD_SPECIAL  0x02
#define LD_TAG      0x04
#define LD_S1TEXOFF 0x08
#define LD_S1TOPTEX 0x10
#define LD_S1BOTTEX 0x20
#define LD_S1MIDTEX 0x40
#define LD_DIFF2    0x80

// diff2 flags
#define LD_S2TEXOFF      0x01
#define LD_S2TOPTEX      0x02
#define LD_S2BOTTEX      0x04
#define LD_S2MIDTEX      0x08
#define LD_ARGS          0x10
#define LD_STRINGARGS    0x20
#define LD__UNUSED       0x40
#define LD_DIFF3         0x80

// diff3 flags
#define LD_ACTIVATION    0x01

static boolean P_LineArgsEqual(const line_t *li, const line_t *spawnli)
{
	UINT8 i;
	for (i = 0; i < NUM_SCRIPT_ARGS; i++)
		if (li->args[i] != spawnli->args[i])
			return false;

	return true;
}

static boolean P_LineStringArgsEqual(const line_t *li, const line_t *spawnli)
{
	UINT8 i;
	for (i = 0; i < NUM_SCRIPT_STRINGARGS; i++)
	{
		if (!li->stringargs[i])
			return !spawnli->stringargs[i];

		if (strcmp(li->stringargs[i], spawnli->stringargs[i]))
			return false;
	}

	return true;
}

#define FD_FLAGS 0x01
#define FD_ALPHA 0x02

// Check if any of the sector's FOFs differ from how they spawned
static boolean CheckFFloorDiff(const sector_t *ss)
{
	ffloor_t *rover;

	for (rover = ss->ffloors; rover; rover = rover->next)
	{
		if (rover->fofflags != rover->spawnflags
		|| rover->alpha != rover->spawnalpha)
			{
				return true; // we found an FOF that changed!
				// don't bother checking for more, we do that later
			}
	}
	return false;
}

// Special case: save the stats of all modified ffloors along with their ffloor "number"s
// we don't bother with ffloors that haven't changed, that would just add to savegame even more than is really needed
static void ArchiveFFloors(savebuffer_t *save, const sector_t *ss)
{
	size_t j = 0; // ss->ffloors is saved as ffloor #0, ss->ffloors->next is #1, etc
	ffloor_t *rover;
	UINT8 fflr_diff;
	for (rover = ss->ffloors; rover; rover = rover->next)
	{
		fflr_diff = 0; // reset diff flags
		if (rover->fofflags != rover->spawnflags)
			fflr_diff |= FD_FLAGS;
		if (rover->alpha != rover->spawnalpha)
			fflr_diff |= FD_ALPHA;

		if (fflr_diff)
		{
			WRITEUINT16(save->p, j); // save ffloor "number"
			WRITEUINT8(save->p, fflr_diff);
			if (fflr_diff & FD_FLAGS)
				WRITEUINT32(save->p, rover->fofflags);
			if (fflr_diff & FD_ALPHA)
				WRITEINT16(save->p, rover->alpha);
		}
		j++;
	}
	WRITEUINT16(save->p, 0xffff);
}

static void UnArchiveFFloors(savebuffer_t *save, const sector_t *ss)
{
	UINT16 j = 0; // number of current ffloor in loop
	UINT16 fflr_i; // saved ffloor "number" of next modified ffloor
	UINT16 fflr_diff; // saved ffloor diff
	ffloor_t *rover;

	rover = ss->ffloors;
	if (!rover) // it is assumed sectors[i].ffloors actually exists, but just in case...
		I_Error("Sector does not have any ffloors!");

	fflr_i = READUINT16(save->p); // get first modified ffloor's number ready
	for (;;) // for some reason the usual for (rover = x; ...) thing doesn't work here?
	{
		if (fflr_i == 0xffff) // end of modified ffloors list, let's stop already
			break;
		// should NEVER need to be checked
		//if (rover == NULL)
			//break;
		if (j != fflr_i) // this ffloor was not modified
		{
			j++;
			rover = rover->next;
			continue;
		}

		fflr_diff = READUINT8(save->p);

		if (fflr_diff & FD_FLAGS)
			rover->fofflags = READUINT32(save->p);
		if (fflr_diff & FD_ALPHA)
			rover->alpha = READINT16(save->p);

		fflr_i = READUINT16(save->p); // get next ffloor "number" ready

		j++;
		rover = rover->next;
	}
}

static void ArchiveSectors(savebuffer_t *save)
{
	size_t i, j;
	const sector_t *ss = sectors;
	const sector_t *spawnss = spawnsectors;
	UINT8 diff, diff2, diff3, diff4, diff5;

	for (i = 0; i < numsectors; i++, ss++, spawnss++)
	{
		diff = diff2 = diff3 = diff4 = diff5 = 0;
		if (ss->floorheight != spawnss->floorheight)
			diff |= SD_FLOORHT;
		if (ss->ceilingheight != spawnss->ceilingheight)
			diff |= SD_CEILHT;
		//
		// flats
		//
		if (ss->floorpic != spawnss->floorpic)
			diff |= SD_FLOORPIC;
		if (ss->ceilingpic != spawnss->ceilingpic)
			diff |= SD_CEILPIC;

		if (ss->lightlevel != spawnss->lightlevel)
			diff |= SD_LIGHT;
		if (ss->special != spawnss->special)
			diff |= SD_SPECIAL;

		if (ss->floor_xoffs != spawnss->floor_xoffs)
			diff2 |= SD_FXOFFS;
		if (ss->floor_yoffs != spawnss->floor_yoffs)
			diff2 |= SD_FYOFFS;
		if (ss->ceiling_xoffs != spawnss->ceiling_xoffs)
			diff2 |= SD_CXOFFS;
		if (ss->ceiling_yoffs != spawnss->ceiling_yoffs)
			diff2 |= SD_CYOFFS;
		if (ss->floorpic_angle != spawnss->floorpic_angle)
			diff2 |= SD_FLOORANG;
		if (ss->ceilingpic_angle != spawnss->ceilingpic_angle)
			diff2 |= SD_CEILANG;

		if (!Tag_Compare(&ss->tags, &spawnss->tags))
			diff2 |= SD_TAG;

		if (ss->extra_colormap != spawnss->extra_colormap)
			diff3 |= SD_COLORMAP;
		if (ss->crumblestate)
			diff3 |= SD_CRUMBLESTATE;

		if (ss->floorlightlevel != spawnss->floorlightlevel || ss->floorlightabsolute != spawnss->floorlightabsolute)
			diff3 |= SD_FLOORLIGHT;
		if (ss->ceilinglightlevel != spawnss->ceilinglightlevel || ss->ceilinglightabsolute != spawnss->ceilinglightabsolute)
			diff3 |= SD_CEILLIGHT;
		if (ss->flags != spawnss->flags)
			diff3 |= SD_FLAG;
		if (ss->specialflags != spawnss->specialflags)
			diff3 |= SD_SPECIALFLAG;
		if (ss->damagetype != spawnss->damagetype)
			diff4 |= SD_DAMAGETYPE;
		if (ss->triggertag != spawnss->triggertag)
			diff4 |= SD_TRIGGERTAG;
		if (ss->triggerer != spawnss->triggerer)
			diff4 |= SD_TRIGGERER;
		if (ss->gravity != spawnss->gravity)
			diff4 |= SD_GRAVITY;

		if (ss->action != spawnss->action)
			diff4 |= SD_ACTION;
		if (!P_SectorArgsEqual(ss, spawnss))
			diff4 |= SD_ARGS;
		if (!P_SectorStringArgsEqual(ss, spawnss))
			diff4 |= SD_STRINGARGS;
		if (ss->activation != spawnss->activation)
			diff5 |= SD_ACTIVATION;
		if (/*ss->botController.trick != spawnss->botController.trick
			||*/ss->botController.flags != spawnss->botController.flags
			|| ss->botController.forceAngle != spawnss->botController.forceAngle)
		{
			diff5 |= SD_BOTCONTROLLER;
		}

		if (ss->ffloors && CheckFFloorDiff(ss))
			diff |= SD_FFLOORS;

		if (diff5)
			diff4 |= SD_DIFF5;

		if (diff4)
			diff3 |= SD_DIFF4;

		if (diff3)
			diff2 |= SD_DIFF3;

		if (diff2)
			diff |= SD_DIFF2;

		if (diff)
		{
			WRITEUINT16(save->p, i);
			WRITEUINT8(save->p, diff);
			if (diff & SD_DIFF2)
				WRITEUINT8(save->p, diff2);
			if (diff2 & SD_DIFF3)
				WRITEUINT8(save->p, diff3);
			if (diff3 & SD_DIFF4)
				WRITEUINT8(save->p, diff4);
			if (diff4 & SD_DIFF5)
				WRITEUINT8(save->p, diff5);
			if (diff & SD_FLOORHT)
				WRITEFIXED(save->p, ss->floorheight);
			if (diff & SD_CEILHT)
				WRITEFIXED(save->p, ss->ceilingheight);
			if (diff & SD_FLOORPIC)
				WRITEMEM(save->p, levelflats[ss->floorpic].name, 8);
			if (diff & SD_CEILPIC)
				WRITEMEM(save->p, levelflats[ss->ceilingpic].name, 8);
			if (diff & SD_LIGHT)
				WRITEINT16(save->p, ss->lightlevel);
			if (diff & SD_SPECIAL)
				WRITEINT16(save->p, ss->special);
			if (diff2 & SD_FXOFFS)
				WRITEFIXED(save->p, ss->floor_xoffs);
			if (diff2 & SD_FYOFFS)
				WRITEFIXED(save->p, ss->floor_yoffs);
			if (diff2 & SD_CXOFFS)
				WRITEFIXED(save->p, ss->ceiling_xoffs);
			if (diff2 & SD_CYOFFS)
				WRITEFIXED(save->p, ss->ceiling_yoffs);
			if (diff2 & SD_FLOORANG)
				WRITEANGLE(save->p, ss->floorpic_angle);
			if (diff2 & SD_CEILANG)
				WRITEANGLE(save->p, ss->ceilingpic_angle);
			if (diff2 & SD_TAG)
			{
				WRITEUINT32(save->p, ss->tags.count);
				for (j = 0; j < ss->tags.count; j++)
					WRITEINT16(save->p, ss->tags.tags[j]);
			}

			if (diff3 & SD_COLORMAP)
				WRITEUINT32(save->p, CheckAddNetColormapToList(ss->extra_colormap));
					// returns existing index if already added, or appends to net_colormaps and returns new index
			if (diff3 & SD_CRUMBLESTATE)
				WRITEINT32(save->p, ss->crumblestate);
			if (diff3 & SD_FLOORLIGHT)
			{
				WRITEINT16(save->p, ss->floorlightlevel);
				WRITEUINT8(save->p, ss->floorlightabsolute);
			}
			if (diff3 & SD_CEILLIGHT)
			{
				WRITEINT16(save->p, ss->ceilinglightlevel);
				WRITEUINT8(save->p, ss->ceilinglightabsolute);
			}
			if (diff3 & SD_FLAG)
				WRITEUINT32(save->p, ss->flags);
			if (diff3 & SD_SPECIALFLAG)
				WRITEUINT32(save->p, ss->specialflags);
			if (diff4 & SD_DAMAGETYPE)
				WRITEUINT8(save->p, ss->damagetype);
			if (diff4 & SD_TRIGGERTAG)
				WRITEINT16(save->p, ss->triggertag);
			if (diff4 & SD_TRIGGERER)
				WRITEUINT8(save->p, ss->triggerer);
			if (diff4 & SD_GRAVITY)
				WRITEFIXED(save->p, ss->gravity);

			if (diff4 & SD_ACTION)
				WRITEINT16(save->p, ss->action);
			if (diff4 & SD_ARGS)
			{
				for (j = 0; j < NUM_SCRIPT_ARGS; j++)
					WRITEINT32(save->p, ss->args[j]);
			}
			if (diff4 & SD_STRINGARGS)
			{
				for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
				{
					size_t len, k;

					if (!ss->stringargs[j])
					{
						WRITEINT32(save->p, 0);
						continue;
					}

					len = strlen(ss->stringargs[j]);
					WRITEINT32(save->p, len);
					for (k = 0; k < len; k++)
						WRITECHAR(save->p, ss->stringargs[j][k]);
				}
			}
			if (diff5 & SD_ACTIVATION)
				WRITEUINT32(save->p, ss->activation);

			if (diff5 & SD_BOTCONTROLLER)
			{
				//WRITEUINT8(save->p, ss->botController.trick);
				WRITEUINT32(save->p, ss->botController.flags);
				WRITEANGLE(save->p, ss->botController.forceAngle);
			}

			if (diff & SD_FFLOORS)
				ArchiveFFloors(save, ss);
		}
	}

	WRITEUINT16(save->p, 0xffff);
}

static void UnArchiveSectors(savebuffer_t *save)
{
	UINT16 i, j;
	UINT8 diff, diff2, diff3, diff4, diff5;
	for (;;)
	{
		i = READUINT16(save->p);

		if (i == 0xffff)
			break;

		if (i > numsectors)
			I_Error("Invalid sector number %u from server (expected end at %s)", i, sizeu1(numsectors));

		diff = READUINT8(save->p);
		if (diff & SD_DIFF2)
			diff2 = READUINT8(save->p);
		else
			diff2 = 0;
		if (diff2 & SD_DIFF3)
			diff3 = READUINT8(save->p);
		else
			diff3 = 0;
		if (diff3 & SD_DIFF4)
			diff4 = READUINT8(save->p);
		else
			diff4 = 0;

		if (diff4 & SD_DIFF5)
			diff5 = READUINT8(save->p);
		else
			diff5 = 0;

		if (diff & SD_FLOORHT)
			sectors[i].floorheight = READFIXED(save->p);
		if (diff & SD_CEILHT)
			sectors[i].ceilingheight = READFIXED(save->p);
		if (diff & SD_FLOORPIC)
		{
			sectors[i].floorpic = P_AddLevelFlatRuntime((char *)save->p);
			save->p += 8;
		}
		if (diff & SD_CEILPIC)
		{
			sectors[i].ceilingpic = P_AddLevelFlatRuntime((char *)save->p);
			save->p += 8;
		}
		if (diff & SD_LIGHT)
			sectors[i].lightlevel = READINT16(save->p);
		if (diff & SD_SPECIAL)
			sectors[i].special = READINT16(save->p);

		if (diff2 & SD_FXOFFS)
			sectors[i].floor_xoffs = READFIXED(save->p);
		if (diff2 & SD_FYOFFS)
			sectors[i].floor_yoffs = READFIXED(save->p);
		if (diff2 & SD_CXOFFS)
			sectors[i].ceiling_xoffs = READFIXED(save->p);
		if (diff2 & SD_CYOFFS)
			sectors[i].ceiling_yoffs = READFIXED(save->p);
		if (diff2 & SD_FLOORANG)
			sectors[i].floorpic_angle  = READANGLE(save->p);
		if (diff2 & SD_CEILANG)
			sectors[i].ceilingpic_angle = READANGLE(save->p);
		if (diff2 & SD_TAG)
		{
			size_t ncount = READUINT32(save->p);

			// Remove entries from global lists.
			for (j = 0; j < sectors[i].tags.count; j++)
				Taggroup_Remove(tags_sectors, sectors[i].tags.tags[j], i);

			// Reallocate if size differs.
			if (ncount != sectors[i].tags.count)
			{
				sectors[i].tags.count = ncount;
				sectors[i].tags.tags = Z_Realloc(sectors[i].tags.tags, ncount*sizeof(mtag_t), PU_LEVEL, NULL);
			}

			for (j = 0; j < ncount; j++)
				sectors[i].tags.tags[j] = READINT16(save->p);

			// Add new entries.
			for (j = 0; j < sectors[i].tags.count; j++)
				Taggroup_Add(tags_sectors, sectors[i].tags.tags[j], i);
		}


		if (diff3 & SD_COLORMAP)
			sectors[i].extra_colormap = GetNetColormapFromList(READUINT32(save->p));
		if (diff3 & SD_CRUMBLESTATE)
			sectors[i].crumblestate = READINT32(save->p);
		if (diff3 & SD_FLOORLIGHT)
		{
			sectors[i].floorlightlevel = READINT16(save->p);
			sectors[i].floorlightabsolute = READUINT8(save->p);
		}
		if (diff3 & SD_CEILLIGHT)
		{
			sectors[i].ceilinglightlevel = READINT16(save->p);
			sectors[i].ceilinglightabsolute = READUINT8(save->p);
		}
		if (diff3 & SD_FLAG)
		{
			sectors[i].flags = READUINT32(save->p);
			CheckForReverseGravity |= (sectors[i].flags & MSF_GRAVITYFLIP);
		}
		if (diff3 & SD_SPECIALFLAG)
			sectors[i].specialflags = READUINT32(save->p);
		if (diff4 & SD_DAMAGETYPE)
			sectors[i].damagetype = READUINT8(save->p);
		if (diff4 & SD_TRIGGERTAG)
			sectors[i].triggertag = READINT16(save->p);
		if (diff4 & SD_TRIGGERER)
			sectors[i].triggerer = READUINT8(save->p);
		if (diff4 & SD_GRAVITY)
			sectors[i].gravity = READFIXED(save->p);

		if (diff4 & SD_ACTION)
			sectors[i].action = READINT16(save->p);
		if (diff4 & SD_ARGS)
		{
			for (j = 0; j < NUM_SCRIPT_ARGS; j++)
				sectors[i].args[j] = READINT32(save->p);
		}
		if (diff4 & SD_STRINGARGS)
		{
			for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
			{
				size_t len = READINT32(save->p);
				size_t k;

				if (!len)
				{
					Z_Free(sectors[i].stringargs[j]);
					sectors[i].stringargs[j] = NULL;
					continue;
				}

				sectors[i].stringargs[j] = Z_Realloc(sectors[i].stringargs[j], len + 1, PU_LEVEL, NULL);
				for (k = 0; k < len; k++)
					sectors[i].stringargs[j][k] = READCHAR(save->p);
				sectors[i].stringargs[j][len] = '\0';
			}
		}
		if (diff5 & SD_ACTIVATION)
			sectors[i].activation = READUINT32(save->p);

		if (diff5 & SD_BOTCONTROLLER)
		{
			//sectors[i].botController.trick = READUINT8(save->p);
			sectors[i].botController.flags = READUINT32(save->p);
			sectors[i].botController.forceAngle = READANGLE(save->p);
		}

		if (diff & SD_FFLOORS)
			UnArchiveFFloors(save, &sectors[i]);
	}
}

static void ArchiveLines(savebuffer_t *save)
{
	size_t i, j;
	const line_t *li = lines;
	const line_t *spawnli = spawnlines;
	const side_t *si;
	const side_t *spawnsi;
	UINT8 diff, diff2, diff3;

	for (i = 0; i < numlines; i++, spawnli++, li++)
	{
		diff = diff2 = diff3 = 0;

		if (li->flags != spawnli->flags)
			diff |= LD_FLAG;

		if (li->special != spawnli->special)
			diff |= LD_SPECIAL;

		if (!Tag_Compare(&li->tags, &spawnli->tags))
			diff |= LD_TAG;

		if (!P_LineArgsEqual(li, spawnli))
			diff2 |= LD_ARGS;

		if (!P_LineStringArgsEqual(li, spawnli))
			diff2 |= LD_STRINGARGS;

		if (li->activation != spawnli->activation)
			diff3 |= LD_ACTIVATION;

		if (li->sidenum[0] != 0xffff)
		{
			si = &sides[li->sidenum[0]];
			spawnsi = &spawnsides[li->sidenum[0]];
			if (si->textureoffset != spawnsi->textureoffset)
				diff |= LD_S1TEXOFF;
			//SoM: 4/1/2000: Some textures are colormaps. Don't worry about invalid textures.
			if (si->toptexture != spawnsi->toptexture)
				diff |= LD_S1TOPTEX;
			if (si->bottomtexture != spawnsi->bottomtexture)
				diff |= LD_S1BOTTEX;
			if (si->midtexture != spawnsi->midtexture)
				diff |= LD_S1MIDTEX;
		}
		if (li->sidenum[1] != 0xffff)
		{
			si = &sides[li->sidenum[1]];
			spawnsi = &spawnsides[li->sidenum[1]];
			if (si->textureoffset != spawnsi->textureoffset)
				diff2 |= LD_S2TEXOFF;
			if (si->toptexture != spawnsi->toptexture)
				diff2 |= LD_S2TOPTEX;
			if (si->bottomtexture != spawnsi->bottomtexture)
				diff2 |= LD_S2BOTTEX;
			if (si->midtexture != spawnsi->midtexture)
				diff2 |= LD_S2MIDTEX;
		}

		if (diff3)
			diff2 |= LD_DIFF3;

		if (diff2)
			diff |= LD_DIFF2;

		if (diff)
		{
			WRITEINT16(save->p, i);
			WRITEUINT8(save->p, diff);
			if (diff & LD_DIFF2)
				WRITEUINT8(save->p, diff2);
			if (diff2 & LD_DIFF3)
				WRITEUINT8(save->p, diff3);
			if (diff & LD_FLAG)
				WRITEUINT32(save->p, li->flags);
			if (diff & LD_SPECIAL)
				WRITEINT16(save->p, li->special);
			if (diff & LD_TAG)
			{
				WRITEUINT32(save->p, li->tags.count);
				for (j = 0; j < li->tags.count; j++)
					WRITEINT16(save->p, li->tags.tags[j]);
			}

			si = &sides[li->sidenum[0]];
			if (diff & LD_S1TEXOFF)
				WRITEFIXED(save->p, si->textureoffset);
			if (diff & LD_S1TOPTEX)
				WRITEINT32(save->p, si->toptexture);
			if (diff & LD_S1BOTTEX)
				WRITEINT32(save->p, si->bottomtexture);
			if (diff & LD_S1MIDTEX)
				WRITEINT32(save->p, si->midtexture);

			si = &sides[li->sidenum[1]];
			if (diff2 & LD_S2TEXOFF)
				WRITEFIXED(save->p, si->textureoffset);
			if (diff2 & LD_S2TOPTEX)
				WRITEINT32(save->p, si->toptexture);
			if (diff2 & LD_S2BOTTEX)
				WRITEINT32(save->p, si->bottomtexture);
			if (diff2 & LD_S2MIDTEX)
				WRITEINT32(save->p, si->midtexture);
			if (diff2 & LD_ARGS)
			{
				for (j = 0; j < NUM_SCRIPT_ARGS; j++)
					WRITEINT32(save->p, li->args[j]);
			}
			if (diff2 & LD_STRINGARGS)
			{
				for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
				{
					size_t len, k;

					if (!li->stringargs[j])
					{
						WRITEINT32(save->p, 0);
						continue;
					}

					len = strlen(li->stringargs[j]);
					WRITEINT32(save->p, len);
					for (k = 0; k < len; k++)
						WRITECHAR(save->p, li->stringargs[j][k]);
				}
			}
			if (diff3 & LD_ACTIVATION)
				WRITEUINT32(save->p, li->activation);
		}
	}
	WRITEUINT16(save->p, 0xffff);
}

static void UnArchiveLines(savebuffer_t *save)
{
	UINT16 i, j;
	line_t *li;
	side_t *si;
	UINT8 diff, diff2, diff3;

	for (;;)
	{
		i = READUINT16(save->p);

		if (i == 0xffff)
			break;
		if (i > numlines)
			I_Error("Invalid line number %u from server", i);

		diff = READUINT8(save->p);
		li = &lines[i];

		if (diff & LD_DIFF2)
			diff2 = READUINT8(save->p);
		else
			diff2 = 0;

		if (diff2 & LD_DIFF3)
			diff3 = READUINT8(save->p);
		else
			diff3 = 0;

		if (diff & LD_FLAG)
			li->flags = READUINT32(save->p);
		if (diff & LD_SPECIAL)
			li->special = READINT16(save->p);
		if (diff & LD_TAG)
		{
			size_t ncount = READUINT32(save->p);

			// Remove entries from global lists.
			for (j = 0; j < lines[i].tags.count; j++)
				Taggroup_Remove(tags_lines, lines[i].tags.tags[j], i);

			// Reallocate if size differs.
			if (ncount != lines[i].tags.count)
			{
				lines[i].tags.count = ncount;
				lines[i].tags.tags = (mtag_t*)Z_Realloc(lines[i].tags.tags, ncount*sizeof(mtag_t), PU_LEVEL, NULL);
			}

			for (j = 0; j < ncount; j++)
				lines[i].tags.tags[j] = READINT16(save->p);

			// Add new entries.
			for (j = 0; j < lines[i].tags.count; j++)
				Taggroup_Add(tags_lines, lines[i].tags.tags[j], i);
		}

		si = &sides[li->sidenum[0]];
		if (diff & LD_S1TEXOFF)
			si->textureoffset = READFIXED(save->p);
		if (diff & LD_S1TOPTEX)
			si->toptexture = READINT32(save->p);
		if (diff & LD_S1BOTTEX)
			si->bottomtexture = READINT32(save->p);
		if (diff & LD_S1MIDTEX)
			si->midtexture = READINT32(save->p);

		si = &sides[li->sidenum[1]];
		if (diff2 & LD_S2TEXOFF)
			si->textureoffset = READFIXED(save->p);
		if (diff2 & LD_S2TOPTEX)
			si->toptexture = READINT32(save->p);
		if (diff2 & LD_S2BOTTEX)
			si->bottomtexture = READINT32(save->p);
		if (diff2 & LD_S2MIDTEX)
			si->midtexture = READINT32(save->p);
		if (diff2 & LD_ARGS)
		{
			for (j = 0; j < NUM_SCRIPT_ARGS; j++)
				li->args[j] = READINT32(save->p);
		}
		if (diff2 & LD_STRINGARGS)
		{
			for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
			{
				size_t len = READINT32(save->p);
				size_t k;

				if (!len)
				{
					Z_Free(li->stringargs[j]);
					li->stringargs[j] = NULL;
					continue;
				}

				li->stringargs[j] = Z_Realloc(li->stringargs[j], len + 1, PU_LEVEL, NULL);
				for (k = 0; k < len; k++)
					li->stringargs[j][k] = READCHAR(save->p);
				li->stringargs[j][len] = '\0';
			}
		}
		if (diff3 & LD_ACTIVATION)
			li->activation = READUINT32(save->p);

	}
}

static void P_NetArchiveWorld(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	// initialize colormap vars because paranoia
	ClearNetColormaps();

	WRITEUINT32(save->p, ARCHIVEBLOCK_WORLD);

	ArchiveSectors(save);
	ArchiveLines(save);
	R_ClearTextureNumCache(false);

	TracyCZoneEnd(__zone);
}

static void P_NetUnArchiveWorld(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	UINT16 i;

	if (READUINT32(save->p) != ARCHIVEBLOCK_WORLD)
		I_Error("Bad $$$.sav at archive block World");

	// initialize colormap vars because paranoia
	ClearNetColormaps();

	// count the level's ffloors so that colormap loading can have an upper limit
	for (i = 0; i < numsectors; i++)
	{
		ffloor_t *rover;
		for (rover = sectors[i].ffloors; rover; rover = rover->next)
			num_ffloors++;
	}

	UnArchiveSectors(save);
	UnArchiveLines(save);

	TracyCZoneEnd(__zone);
}

//
// Thinkers
//

static boolean P_ThingArgsEqual(const mobj_t *mobj, const mapthing_t *mapthing)
{
	UINT8 i;
	for (i = 0; i < NUM_MAPTHING_ARGS; i++)
		if (mobj->args[i] != mapthing->args[i])
			return false;

	return true;
}

static boolean P_ThingStringArgsEqual(const mobj_t *mobj, const mapthing_t *mapthing)
{
	UINT8 i;
	for (i = 0; i < NUM_MAPTHING_STRINGARGS; i++)
	{
		if (!mobj->stringargs[i])
			return !mapthing->stringargs[i];

		if (strcmp(mobj->stringargs[i], mapthing->stringargs[i]))
			return false;
	}

	return true;
}

static boolean P_ThingScriptEqual(const mobj_t *mobj, const mapthing_t *mapthing)
{
	UINT8 i;
	if (mobj->special != mapthing->special)
		return false;

	for (i = 0; i < NUM_SCRIPT_ARGS; i++)
		if (mobj->script_args[i] != mapthing->script_args[i])
			return false;

	for (i = 0; i < NUM_SCRIPT_STRINGARGS; i++)
	{
		if (!mobj->script_stringargs[i])
			return !mapthing->script_stringargs[i];

		if (strcmp(mobj->script_stringargs[i], mapthing->script_stringargs[i]))
			return false;
	}

	return true;
}

typedef enum
{
	MD_SPAWNPOINT  = 1,
	MD_POS         = 1<<1,
	MD_TYPE        = 1<<2,
	MD_MOM         = 1<<3,
	MD_RADIUS      = 1<<4,
	MD_HEIGHT      = 1<<5,
	MD_FLAGS       = 1<<6,
	MD_HEALTH      = 1<<7,
	MD_RTIME       = 1<<8,
	MD_STATE       = 1<<9,
	MD_TICS        = 1<<10,
	MD_SPRITE      = 1<<11,
	MD_FRAME       = 1<<12,
	MD_EFLAGS      = 1<<13,
	MD_PLAYER      = 1<<14,
	MD_MOVEDIR     = 1<<15,
	MD_MOVECOUNT   = 1<<16,
	MD_THRESHOLD   = 1<<17,
	MD_LASTLOOK    = 1<<18,
	MD_TARGET      = 1<<19,
	MD_TRACER      = 1<<20,
	MD_FRICTION    = 1<<21,
	MD_MOVEFACTOR  = 1<<22,
	MD_FLAGS2      = 1<<23,
	MD_FUSE        = 1<<24,
	MD_WATERTOP    = 1<<25,
	MD_WATERBOTTOM = 1<<26,
	MD_SCALE       = 1<<27,
	MD_DSCALE      = 1<<28,
	MD_ARGS        = 1<<29,
	MD_STRINGARGS  = 1<<30,
	MD_MORE        = (INT32)(1U<<31)
} mobj_diff_t;

typedef enum
{
	MD2_CUSVAL       = 1,
	MD2_CVMEM        = 1<<1,
	MD2_SKIN         = 1<<2,
	MD2_COLOR        = 1<<3,
	MD2_SCALESPEED   = 1<<4,
	MD2_EXTVAL1      = 1<<5,
	MD2_EXTVAL2      = 1<<6,
	MD2_HNEXT        = 1<<7,
	MD2_HPREV        = 1<<8,
	MD2_FLOORROVER   = 1<<9,
	MD2_CEILINGROVER = 1<<10,
	MD2_SLOPE        = 1<<11,
	MD2_COLORIZED    = 1<<12,
	MD2_MIRRORED     = 1<<13,
	MD2_ROLLANGLE    = 1<<14,
	MD2_SHADOWSCALE  = 1<<15,
	MD2_RENDERFLAGS  = 1<<16,
	MD2_TID          = 1<<17,
	MD2_SPRITESCALE  = 1<<18,
	MD2_SPRITEOFFSET = 1<<19,
	MD2_WORLDOFFSET  = 1<<20,
	MD2_SPECIAL      = 1<<21,
	MD2_FLOORSPRITESLOPE = 1<<22,
	MD2_DISPOFFSET   = 1<<23,
	MD2_BOSS3CAP  = 1<<24,
	MD2_WAYPOINTCAP  = 1<<25,
	MD2_KITEMCAP     = 1<<26,
	MD2_ITNEXT       = 1<<27,
	MD2_LASTMOMZ     = 1<<28,
	MD2_TERRAIN      = 1<<29,
	MD2_LIGHTLEVEL   = 1<<30,
	MD2_MORE         = (INT32)(1U<<31)
} mobj_diff2_t;

typedef enum
{
	MD3_GRAVITY      = 1,
	MD3_MISCCAP      = 1<<1,
	MD3_BAKEDOFFSET	 = 1<<2,
	MD3_EXTVAL3		 = 1<<3,
	MD3_SHIELDTRACER = 1<<4,
} mobj_diff3_t;

typedef enum
{
	tc_mobj,
	tc_ceiling,
	tc_floor,
	tc_flash,
	tc_strobe,
	tc_glow,
	tc_fireflicker,
	tc_thwomp,
	tc_camerascanner,
	tc_elevator,
	tc_continuousfalling,
	tc_bouncecheese,
	tc_startcrumble,
	tc_marioblock,
	tc_marioblockchecker,
	tc_floatsector,
	tc_crushceiling,
	tc_scroll,
	tc_friction,
	tc_pusher,
	tc_laserflash,
	tc_lightfade,
	tc_executor,
	tc_raisesector,
	tc_noenemies,
	tc_eachtime,
	tc_disappear,
	tc_fade,
	tc_fadecolormap,
	tc_planedisplace,
	tc_dynslopeline,
	tc_dynslopevert,
	tc_polyrotate, // haleyjd 03/26/06: polyobjects
	tc_polymove,
	tc_polywaypoint,
	tc_polyslidedoor,
	tc_polyswingdoor,
	tc_polyflag,
	tc_polydisplace,
	tc_polyrotdisplace,
	tc_polyfade,
	tc_end
} specials_e;

static inline UINT32 SaveMobjnum(const mobj_t *mobj)
{
	if (mobj) return mobj->mobjnum;
	return 0;
}

static UINT32 SaveSector(const sector_t *sector)
{
	if (sector) return (UINT32)(sector - sectors);
	return 0xFFFFFFFF;
}

static UINT32 SaveLine(const line_t *line)
{
	if (line) return (UINT32)(line - lines);
	return 0xFFFFFFFF;
}

static inline UINT32 SavePlayer(const player_t *player)
{
	if (player) return (UINT32)(player - players);
	return 0xFFFFFFFF;
}

static UINT32 SaveSlope(const pslope_t *slope)
{
	if (slope) return (UINT32)(slope->id);
	return 0xFFFFFFFF;
}

static void SaveMobjThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const mobj_t *mobj = (const mobj_t *)th;
	UINT32 diff;
	UINT32 diff2;
	UINT32 diff3;
	size_t j;

	// Ignore stationary hoops - these will be respawned from mapthings.
	if (mobj->type == MT_HOOP)
		return;

	// These are NEVER saved.
	if (mobj->type == MT_HOOPCOLLIDE)
		return;

	// This hoop has already been collected.
	if (mobj->type == MT_HOOPCENTER && mobj->threshold == 4242)
		return;

	// MT_SPARK: used for debug stuff
	if (mobj->type == MT_SPARK)
		return;

	// This is a non-synched visual effect mobj
	if (mobj->flags2 & MF2_DONTSYNC)
		return;

	diff = diff2 = diff3 = 0;

	if (mobj->spawnpoint)
	{
		// spawnpoint is not modified but we must save it since it is an identifier
		diff = MD_SPAWNPOINT;

		if ((mobj->x != mobj->spawnpoint->x << FRACBITS) ||
			(mobj->y != mobj->spawnpoint->y << FRACBITS) ||
			(mobj->angle != FixedAngle(mobj->spawnpoint->angle*FRACUNIT)) ||
			(mobj->pitch != FixedAngle(mobj->spawnpoint->pitch*FRACUNIT)) ||
			(mobj->roll != FixedAngle(mobj->spawnpoint->roll*FRACUNIT)) )
			diff |= MD_POS;

		if (mobj->info->doomednum != mobj->spawnpoint->type)
			diff |= MD_TYPE;
		
		if (!P_ThingArgsEqual(mobj, mobj->spawnpoint))
			diff |= MD_ARGS;

		if (!P_ThingStringArgsEqual(mobj, mobj->spawnpoint))
			diff |= MD_STRINGARGS;

		if (!P_ThingScriptEqual(mobj, mobj->spawnpoint))
			diff2 |= MD2_SPECIAL;
	}
	else
	{
		// not a map spawned thing, so make it from scratch
		diff = MD_POS | MD_TYPE;

		for (j = 0; j < NUM_MAPTHING_ARGS; j++)
		{
			if (mobj->args[j] != 0)
			{
				diff |= MD_ARGS;
				break;
			}
		}

		for (j = 0; j < NUM_MAPTHING_STRINGARGS; j++)
		{
			if (mobj->stringargs[j] != NULL)
			{
				diff |= MD_STRINGARGS;
				break;
			}
		}

		if (mobj->special != 0)
		{
			diff2 |= MD2_SPECIAL;
		}

		for (j = 0; j < NUM_SCRIPT_ARGS; j++)
		{
			if (mobj->script_args[j] != 0)
			{
				diff2 |= MD2_SPECIAL;
				break;
			}
		}

		for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
		{
			if (mobj->stringargs[j] != NULL)
			{
				diff2 |= MD2_SPECIAL;
				break;
			}
		}
	}

	// not the default but the most probable
	if (mobj->momx != 0 || mobj->momy != 0 || mobj->momz != 0 || mobj->pmomz != 0)
		diff |= MD_MOM;
	if (mobj->radius != FixedMul(mapobjectscale, mobj->info->radius))
		diff |= MD_RADIUS;
	if (mobj->height != FixedMul(mapobjectscale, mobj->info->height))
		diff |= MD_HEIGHT;
	if (mobj->flags != mobj->info->flags)
		diff |= MD_FLAGS;
	if (mobj->flags2)
		diff |= MD_FLAGS2;
	if (mobj->health != mobj->info->spawnhealth)
		diff |= MD_HEALTH;
	if (mobj->reactiontime != mobj->info->reactiontime)
		diff |= MD_RTIME;
	if ((statenum_t)(mobj->state-states) != mobj->info->spawnstate)
		diff |= MD_STATE;
	if (mobj->tics != mobj->state->tics)
		diff |= MD_TICS;
	if (mobj->sprite != mobj->state->sprite)
		diff |= MD_SPRITE;
	if (mobj->sprite == SPR_PLAY && mobj->sprite2 != (mobj->state->frame&FF_FRAMEMASK))
		diff |= MD_SPRITE;
	if (mobj->frame != mobj->state->frame)
		diff |= MD_FRAME;
	if (mobj->anim_duration != (UINT16)mobj->state->var2)
		diff |= MD_FRAME;
	if (mobj->eflags)
		diff |= MD_EFLAGS;
	if (mobj->player)
		diff |= MD_PLAYER;

	if (mobj->movedir)
		diff |= MD_MOVEDIR;
	if (mobj->movecount)
		diff |= MD_MOVECOUNT;
	if (mobj->threshold)
		diff |= MD_THRESHOLD;
	if (mobj->lastlook != -1)
		diff |= MD_LASTLOOK;
	if (mobj->target)
		diff |= MD_TARGET;
	if (mobj->tracer)
		diff |= MD_TRACER;
	if (mobj->friction != ORIG_FRICTION)
		diff |= MD_FRICTION;
	if (mobj->movefactor != FRACUNIT)
		diff |= MD_MOVEFACTOR;
	if (mobj->fuse)
		diff |= MD_FUSE;
	if (mobj->watertop != INT32_MAX)
		diff |= MD_WATERTOP;
	if (mobj->waterbottom)
		diff |= MD_WATERBOTTOM;
	if (mobj->scale != mapobjectscale)
		diff |= MD_SCALE;
	if (mobj->destscale != mobj->scale)
		diff |= MD_DSCALE;
	if (mobj->scalespeed != mapobjectscale/12)
		diff2 |= MD2_SCALESPEED;

	if (mobj->cusval)
		diff2 |= MD2_CUSVAL;
	if (mobj->cvmem)
		diff2 |= MD2_CVMEM;
	if (mobj->color)
		diff2 |= MD2_COLOR;
	if (mobj->skin)
		diff2 |= MD2_SKIN;
	if (mobj->extravalue1)
		diff2 |= MD2_EXTVAL1;
	if (mobj->extravalue2)
		diff2 |= MD2_EXTVAL2;
	if (mobj->hnext)
		diff2 |= MD2_HNEXT;
	if (mobj->hprev)
		diff2 |= MD2_HPREV;
	if (mobj->standingslope)
		diff2 |= MD2_SLOPE;
	if (mobj->colorized)
		diff2 |= MD2_COLORIZED;
	if (mobj->floorrover)
		diff2 |= MD2_FLOORROVER;
	if (mobj->ceilingrover)
		diff2 |= MD2_CEILINGROVER;
	if (mobj->mirrored)
		diff2 |= MD2_MIRRORED;
	if (mobj->rollangle)
		diff2 |= MD2_ROLLANGLE;
	if (mobj->shadowscale)
		diff2 |= MD2_SHADOWSCALE;
	if (mobj->renderflags)
		diff2 |= MD2_RENDERFLAGS;
	if (mobj->tid != 0)
		diff2 |= MD2_TID;
	if (mobj->spritexscale != FRACUNIT || mobj->spriteyscale != FRACUNIT)
		diff2 |= MD2_SPRITESCALE;
	if (mobj->spritexoffset || mobj->spriteyoffset ||
		mobj->rollingxoffset || mobj->rollingyoffset)
		diff2 |= MD2_SPRITEOFFSET;
	if (mobj->sprxoff || mobj->spryoff || mobj->sprzoff)
		diff2 |= MD2_WORLDOFFSET;
	if (mobj->floorspriteslope)
	{
		pslope_t *slope = mobj->floorspriteslope;
		if (slope->zangle || slope->zdelta || slope->xydirection
		|| slope->o.x || slope->o.y || slope->o.z
		|| slope->d.x || slope->d.y
		|| slope->normal.x || slope->normal.y
		|| (slope->normal.z != FRACUNIT))
			diff2 |= MD2_FLOORSPRITESLOPE;
	}
	if (mobj->lightlevel)
		diff2 |= MD2_LIGHTLEVEL;
	if (mobj->dispoffset)
		diff2 |= MD2_DISPOFFSET;
	if (mobj == boss3cap)
		diff2 |= MD2_BOSS3CAP;
	if (mobj == waypointcap)
		diff2 |= MD2_WAYPOINTCAP;
	if (mobj == kitemcap)
		diff2 |= MD2_KITEMCAP;
	if (mobj->itnext)
		diff2 |= MD2_ITNEXT;
	if (mobj->lastmomz)
		diff2 |= MD2_LASTMOMZ;
	if (mobj->terrain != NULL || mobj->terrainOverlay != NULL)
		diff2 |= MD2_TERRAIN;

	if (mobj->gravity != FRACUNIT)
		diff3 |= MD3_GRAVITY;
	if (mobj == misccap)
		diff3 |= MD3_MISCCAP;
	if (mobj->bakexoff || mobj->bakeyoff || mobj->bakezoff || mobj->bakexpiv ||
		mobj->bakeypiv || mobj->bakezpiv)
		diff3 |= MD3_BAKEDOFFSET;
	if (mobj->extravalue3)
		diff3 |= MD3_EXTVAL3;
	if (mobj->shieldtracer)
		diff3 |= MD3_SHIELDTRACER;

	if (diff3 != 0)
		diff2 |= MD2_MORE;

	if (diff2 != 0)
		diff |= MD_MORE;

	// Scrap all of that. If we're a hoop center, this is ALL we're saving.
	if (mobj->type == MT_HOOPCENTER)
		diff = MD_SPAWNPOINT;

	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, diff);
	if (diff & MD_MORE)
		WRITEUINT32(save->p, diff2);
	if (diff2 & MD2_MORE)
		WRITEUINT32(save->p, diff3);

	WRITEFIXED(save->p, mobj->z); // Force this so 3dfloor problems don't arise.
	WRITEFIXED(save->p, mobj->floorz);
	WRITEFIXED(save->p, mobj->ceilingz);

	if (diff2 & MD2_FLOORROVER)
	{
		WRITEUINT32(save->p, SaveSector(mobj->floorrover->target));
		WRITEUINT16(save->p, P_GetFFloorID(mobj->floorrover));
	}

	if (diff2 & MD2_CEILINGROVER)
	{
		WRITEUINT32(save->p, SaveSector(mobj->ceilingrover->target));
		WRITEUINT16(save->p, P_GetFFloorID(mobj->ceilingrover));
	}

	if (diff & MD_SPAWNPOINT)
	{
		size_t z;

		for (z = 0; z < nummapthings; z++)
		{
			if (&mapthings[z] != mobj->spawnpoint)
				continue;
			WRITEUINT16(save->p, z);
			break;
		}
		if (mobj->type == MT_HOOPCENTER)
			return;
	}

	if (diff & MD_TYPE)
		WRITEUINT32(save->p, mobj->type);
	if (diff & MD_POS)
	{
		WRITEFIXED(save->p, mobj->x);
		WRITEFIXED(save->p, mobj->y);
		WRITEANGLE(save->p, mobj->angle);
		WRITEANGLE(save->p, mobj->pitch);
		WRITEANGLE(save->p, mobj->roll);
	}
	if (diff & MD_MOM)
	{
		WRITEFIXED(save->p, mobj->momx);
		WRITEFIXED(save->p, mobj->momy);
		WRITEFIXED(save->p, mobj->momz);
		WRITEFIXED(save->p, mobj->pmomz);
	}
	if (diff & MD_RADIUS)
		WRITEFIXED(save->p, mobj->radius);
	if (diff & MD_HEIGHT)
		WRITEFIXED(save->p, mobj->height);
	if (diff & MD_FLAGS)
		WRITEUINT32(save->p, mobj->flags);
	if (diff & MD_FLAGS2)
		WRITEUINT32(save->p, mobj->flags2);
	if (diff & MD_HEALTH)
		WRITEINT32(save->p, mobj->health);
	if (diff & MD_RTIME)
		WRITEINT32(save->p, mobj->reactiontime);
	if (diff & MD_STATE)
		WRITEUINT16(save->p, mobj->state-states);
	if (diff & MD_TICS)
		WRITEINT32(save->p, mobj->tics);
	if (diff & MD_SPRITE) {
		WRITEUINT16(save->p, mobj->sprite);
		if (mobj->sprite == SPR_PLAY)
			WRITEUINT8(save->p, mobj->sprite2);
	}
	if (diff & MD_FRAME)
	{
		WRITEUINT32(save->p, mobj->frame);
		WRITEUINT16(save->p, mobj->anim_duration);
	}
	if (diff & MD_EFLAGS)
		WRITEUINT16(save->p, mobj->eflags);
	if (diff & MD_PLAYER)
		WRITEUINT8(save->p, mobj->player-players);
	if (diff & MD_MOVEDIR)
		WRITEANGLE(save->p, mobj->movedir);
	if (diff & MD_MOVECOUNT)
		WRITEINT32(save->p, mobj->movecount);
	if (diff & MD_THRESHOLD)
		WRITEINT32(save->p, mobj->threshold);
	if (diff & MD_LASTLOOK)
		WRITEINT32(save->p, mobj->lastlook);
	if (diff & MD_TARGET)
		WRITEUINT32(save->p, mobj->target->mobjnum);
	if (diff & MD_TRACER)
		WRITEUINT32(save->p, mobj->tracer->mobjnum);
	if (diff & MD_FRICTION)
		WRITEFIXED(save->p, mobj->friction);
	if (diff & MD_MOVEFACTOR)
		WRITEFIXED(save->p, mobj->movefactor);
	if (diff & MD_FUSE)
		WRITEINT32(save->p, mobj->fuse);
	if (diff & MD_WATERTOP)
		WRITEFIXED(save->p, mobj->watertop);
	if (diff & MD_WATERBOTTOM)
		WRITEFIXED(save->p, mobj->waterbottom);
	if (diff & MD_SCALE)
		WRITEFIXED(save->p, mobj->scale);
	if (diff & MD_DSCALE)
		WRITEFIXED(save->p, mobj->destscale);
	if (diff2 & MD2_SCALESPEED)
		WRITEFIXED(save->p, mobj->scalespeed);
	if (diff & MD_ARGS)
	{
		for (j = 0; j < NUM_MAPTHING_ARGS; j++)
			WRITEINT32(save->p, mobj->args[j]);
	}
	if (diff & MD_STRINGARGS)
	{
		for (j = 0; j < NUM_MAPTHING_STRINGARGS; j++)
		{
			size_t len, k;

			if (!mobj->stringargs[j])
			{
				WRITEINT32(save->p, 0);
				continue;
			}

			len = strlen(mobj->stringargs[j]);
			WRITEINT32(save->p, len);
			for (k = 0; k < len; k++)
				WRITECHAR(save->p, mobj->stringargs[j][k]);
		}
	}
	if (diff2 & MD2_CUSVAL)
		WRITEINT32(save->p, mobj->cusval);
	if (diff2 & MD2_CVMEM)
		WRITEINT32(save->p, mobj->cvmem);
	if (diff2 & MD2_SKIN)
		WRITEUINT16(save->p, (UINT16)((skin_t *)mobj->skin - skins));
	if (diff2 & MD2_COLOR)
		WRITEUINT16(save->p, mobj->color);
	if (diff2 & MD2_EXTVAL1)
		WRITEINT32(save->p, mobj->extravalue1);
	if (diff2 & MD2_EXTVAL2)
		WRITEINT32(save->p, mobj->extravalue2);
	if (diff2 & MD2_HNEXT)
		WRITEUINT32(save->p, mobj->hnext->mobjnum);
	if (diff2 & MD2_HPREV)
		WRITEUINT32(save->p, mobj->hprev->mobjnum);
	if (diff2 & MD2_ITNEXT)
		WRITEUINT32(save->p, mobj->itnext->mobjnum);
	if (diff2 & MD2_SLOPE)
		WRITEUINT16(save->p, mobj->standingslope->id);
	if (diff2 & MD2_COLORIZED)
		WRITEUINT8(save->p, mobj->colorized);
	if (diff2 & MD2_MIRRORED)
		WRITEUINT8(save->p, mobj->mirrored);
	if (diff2 & MD2_ROLLANGLE)
		WRITEANGLE(save->p, mobj->rollangle);
	if (diff2 & MD2_SHADOWSCALE)
	{
		WRITEFIXED(save->p, mobj->shadowscale);
		WRITEUINT8(save->p, mobj->whiteshadow);
		WRITEUINT8(save->p, mobj->shadowcolor);
	}
	if (diff2 & MD2_RENDERFLAGS)
	{
		UINT32 rf = mobj->renderflags;
		UINT32 q = rf & RF_DONTDRAW;

		if (q != RF_DONTDRAW // visible for more than one local player
		&& q != (RF_DONTDRAWP1|RF_DONTDRAWP2|RF_DONTDRAWP3)
		&& q != (RF_DONTDRAWP4|RF_DONTDRAWP1|RF_DONTDRAWP2)
		&& q != (RF_DONTDRAWP4|RF_DONTDRAWP1|RF_DONTDRAWP3)
		&& q != (RF_DONTDRAWP4|RF_DONTDRAWP2|RF_DONTDRAWP3))
			rf &= ~q;

		WRITEUINT32(save->p, rf);
	}
	if (diff2 & MD2_TID)
		WRITEINT16(save->p, mobj->tid);
	if (diff2 & MD2_SPRITESCALE)
	{
		WRITEFIXED(save->p, mobj->spritexscale);
		WRITEFIXED(save->p, mobj->spriteyscale);
	}
	if (diff2 & MD2_SPRITEOFFSET)
	{
		WRITEFIXED(save->p, mobj->spritexoffset);
		WRITEFIXED(save->p, mobj->spriteyoffset);
		WRITEINT16(save->p, mobj->rollingxoffset);
		WRITEINT16(save->p, mobj->rollingyoffset);
	}
	if (diff2 & MD2_WORLDOFFSET)
	{
		WRITEFIXED(save->p, mobj->sprxoff);
		WRITEFIXED(save->p, mobj->spryoff);
		WRITEFIXED(save->p, mobj->sprzoff);
	}
	if (diff2 & MD2_SPECIAL)
	{
		WRITEINT16(save->p, mobj->special);

		for (j = 0; j < NUM_SCRIPT_ARGS; j++)
			WRITEINT32(save->p, mobj->script_args[j]);

		for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
		{
			size_t len, k;

			if (!mobj->script_stringargs[j])
			{
				WRITEINT32(save->p, 0);
				continue;
			}

			len = strlen(mobj->script_stringargs[j]);
			WRITEINT32(save->p, len);
			for (k = 0; k < len; k++)
				WRITECHAR(save->p, mobj->script_stringargs[j][k]);
		}
	}
	if (diff2 & MD2_FLOORSPRITESLOPE)
	{
		pslope_t *slope = mobj->floorspriteslope;

		WRITEFIXED(save->p, slope->zdelta);
		WRITEANGLE(save->p, slope->zangle);
		WRITEANGLE(save->p, slope->xydirection);

		WRITEFIXED(save->p, slope->o.x);
		WRITEFIXED(save->p, slope->o.y);
		WRITEFIXED(save->p, slope->o.z);

		WRITEFIXED(save->p, slope->d.x);
		WRITEFIXED(save->p, slope->d.y);

		WRITEFIXED(save->p, slope->normal.x);
		WRITEFIXED(save->p, slope->normal.y);
		WRITEFIXED(save->p, slope->normal.z);
		
	}
	if (diff2 & MD2_LIGHTLEVEL)
	{
		WRITEINT16(save->p, mobj->lightlevel);
	}
	if (diff2 & MD2_DISPOFFSET)
	{
		WRITEINT32(save->p, mobj->dispoffset);
	}
	if (diff2 & MD2_LASTMOMZ)
	{
		WRITEINT32(save->p, mobj->lastmomz);
	}
	if (diff2 & MD2_TERRAIN)
	{
		WRITEUINT32(save->p, K_GetTerrainHeapIndex(mobj->terrain) + 1);
		WRITEUINT32(save->p, SaveMobjnum(mobj->terrainOverlay));
	}
	if (diff3 & MD3_GRAVITY)
	{
		WRITEFIXED(save->p, mobj->gravity);
	}
	if (diff3 & MD3_BAKEDOFFSET)
	{
		WRITEFIXED(save->p, mobj->bakexoff);
		WRITEFIXED(save->p, mobj->bakeyoff);
		WRITEFIXED(save->p, mobj->bakezoff);
		WRITEFIXED(save->p, mobj->bakexpiv);
		WRITEFIXED(save->p, mobj->bakeypiv);
		WRITEFIXED(save->p, mobj->bakezpiv);
	}
	if (diff3 & MD3_EXTVAL3)
	{
		WRITEINT32(save->p, mobj->extravalue3);
	}
	if (diff3 & MD3_SHIELDTRACER)
	{
		WRITEUINT32(save->p, mobj->shieldtracer->mobjnum);
	}

	WRITEUINT32(save->p, mobj->mobjnum);
}

static void SaveNoEnemiesThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const noenemies_t *ht  = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveLine(ht->sourceline));
}

static void SaveBounceCheeseThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const bouncecheese_t *ht  = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveLine(ht->sourceline));
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEFIXED(save->p, ht->speed);
	WRITEFIXED(save->p, ht->distance);
	WRITEFIXED(save->p, ht->floorwasheight);
	WRITEFIXED(save->p, ht->ceilingwasheight);
	WRITECHAR(save->p, ht->low);
}

static void SaveContinuousFallThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const continuousfall_t *ht  = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEFIXED(save->p, ht->speed);
	WRITEINT32(save->p, ht->direction);
	WRITEFIXED(save->p, ht->floorstartheight);
	WRITEFIXED(save->p, ht->ceilingstartheight);
	WRITEFIXED(save->p, ht->destheight);
}

static void SaveMarioBlockThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const mariothink_t *ht  = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEFIXED(save->p, ht->speed);
	WRITEINT32(save->p, ht->direction);
	WRITEFIXED(save->p, ht->floorstartheight);
	WRITEFIXED(save->p, ht->ceilingstartheight);
	WRITEINT16(save->p, ht->tag);
}

static void SaveMarioCheckThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const mariocheck_t *ht  = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveLine(ht->sourceline));
	WRITEUINT32(save->p, SaveSector(ht->sector));
}

static void SaveThwompThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const thwomp_t *ht  = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveLine(ht->sourceline));
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEFIXED(save->p, ht->crushspeed);
	WRITEFIXED(save->p, ht->retractspeed);
	WRITEINT32(save->p, ht->direction);
	WRITEFIXED(save->p, ht->floorstartheight);
	WRITEFIXED(save->p, ht->ceilingstartheight);
	WRITEINT32(save->p, ht->delay);
	WRITEINT16(save->p, ht->tag);
	WRITEUINT16(save->p, ht->sound);
	WRITEINT32(save->p, ht->initDelay);
}

static void SaveFloatThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const floatthink_t *ht  = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveLine(ht->sourceline));
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEINT16(save->p, ht->tag);
}

static void SaveEachTimeThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const eachtime_t *ht  = (const void *)th;
	size_t i;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveLine(ht->sourceline));
	for (i = 0; i < MAXPLAYERS; i++)
	{
		WRITECHAR(save->p, ht->playersInArea[i]);
	}
	WRITECHAR(save->p, ht->triggerOnExit);
}

static void SaveRaiseThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const raise_t *ht  = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT16(save->p, ht->tag);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEFIXED(save->p, ht->ceilingbottom);
	WRITEFIXED(save->p, ht->ceilingtop);
	WRITEFIXED(save->p, ht->basespeed);
	WRITEFIXED(save->p, ht->extraspeed);
	WRITEUINT8(save->p, ht->shaketimer);
	WRITEUINT8(save->p, ht->flags);
}

static void SaveCeilingThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const ceiling_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT8(save->p, ht->type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEFIXED(save->p, ht->bottomheight);
	WRITEFIXED(save->p, ht->topheight);
	WRITEFIXED(save->p, ht->speed);
	WRITEFIXED(save->p, ht->delay);
	WRITEFIXED(save->p, ht->delaytimer);
	WRITEUINT8(save->p, ht->crush);
	WRITEINT32(save->p, ht->texture);
	WRITEINT32(save->p, ht->direction);
	WRITEINT16(save->p, ht->tag);
	WRITEFIXED(save->p, ht->sourceline);
	WRITEFIXED(save->p, ht->origspeed);
	WRITEFIXED(save->p, ht->crushHeight);
	WRITEFIXED(save->p, ht->crushSpeed);
	WRITEFIXED(save->p, ht->returnHeight);
	WRITEFIXED(save->p, ht->returnSpeed);
}

static void SaveFloormoveThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const floormove_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT8(save->p, ht->type);
	WRITEUINT8(save->p, ht->crush);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEINT32(save->p, ht->direction);
	WRITEINT32(save->p, ht->texture);
	WRITEFIXED(save->p, ht->floordestheight);
	WRITEFIXED(save->p, ht->speed);
	WRITEFIXED(save->p, ht->origspeed);
	WRITEFIXED(save->p, ht->delay);
	WRITEFIXED(save->p, ht->delaytimer);
	WRITEINT16(save->p, ht->tag);
	WRITEFIXED(save->p, ht->sourceline);
	WRITEFIXED(save->p, ht->crushHeight);
	WRITEFIXED(save->p, ht->crushSpeed);
	WRITEFIXED(save->p, ht->returnHeight);
	WRITEFIXED(save->p, ht->returnSpeed);
}

static void SaveLightflashThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const lightflash_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEINT32(save->p, ht->maxlight);
	WRITEINT32(save->p, ht->minlight);
}

static void SaveStrobeThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const strobe_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEINT32(save->p, ht->count);
	WRITEINT16(save->p, ht->minlight);
	WRITEINT16(save->p, ht->maxlight);
	WRITEINT32(save->p, ht->darktime);
	WRITEINT32(save->p, ht->brighttime);
}

static void SaveGlowThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const glow_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEINT16(save->p, ht->minlight);
	WRITEINT16(save->p, ht->maxlight);
	WRITEINT16(save->p, ht->direction);
	WRITEINT16(save->p, ht->speed);
}

static inline void SaveFireflickerThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const fireflicker_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEINT32(save->p, ht->count);
	WRITEINT32(save->p, ht->resetcount);
	WRITEINT16(save->p, ht->maxlight);
	WRITEINT16(save->p, ht->minlight);
}

static void SaveElevatorThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const elevator_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT8(save->p, ht->type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEUINT32(save->p, SaveSector(ht->actionsector));
	WRITEINT32(save->p, ht->direction);
	WRITEFIXED(save->p, ht->floordestheight);
	WRITEFIXED(save->p, ht->ceilingdestheight);
	WRITEFIXED(save->p, ht->speed);
	WRITEFIXED(save->p, ht->origspeed);
	WRITEFIXED(save->p, ht->low);
	WRITEFIXED(save->p, ht->high);
	WRITEFIXED(save->p, ht->distance);
	WRITEFIXED(save->p, ht->delay);
	WRITEFIXED(save->p, ht->delaytimer);
	WRITEFIXED(save->p, ht->floorwasheight);
	WRITEFIXED(save->p, ht->ceilingwasheight);
}

static void SaveCrumbleThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const crumble_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveLine(ht->sourceline));
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEUINT32(save->p, SaveSector(ht->actionsector));
	WRITEUINT32(save->p, SavePlayer(ht->player)); // was dummy
	WRITEINT32(save->p, ht->direction);
	WRITEINT32(save->p, ht->origalpha);
	WRITEINT32(save->p, ht->timer);
	WRITEFIXED(save->p, ht->speed);
	WRITEFIXED(save->p, ht->floorwasheight);
	WRITEFIXED(save->p, ht->ceilingwasheight);
	WRITEUINT8(save->p, ht->flags);
}

static inline void SaveScrollThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const scroll_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEFIXED(save->p, ht->dx);
	WRITEFIXED(save->p, ht->dy);
	WRITEINT32(save->p, ht->affectee);
	WRITEINT32(save->p, ht->control);
	WRITEFIXED(save->p, ht->last_height);
	WRITEFIXED(save->p, ht->vdx);
	WRITEFIXED(save->p, ht->vdy);
	WRITEINT32(save->p, ht->accel);
	WRITEINT32(save->p, ht->exclusive);
	WRITEUINT8(save->p, ht->type);
}

static inline void SaveFrictionThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const friction_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->friction);
	WRITEINT32(save->p, ht->movefactor);
	WRITEINT32(save->p, ht->affectee);
	WRITEINT32(save->p, ht->referrer);
	WRITEUINT8(save->p, ht->roverfriction);
}

static inline void SavePusherThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const pusher_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT8(save->p, ht->type);
	WRITEFIXED(save->p, ht->x_mag);
	WRITEFIXED(save->p, ht->y_mag);
	WRITEFIXED(save->p, ht->z_mag);
	WRITEINT32(save->p, ht->affectee);
	WRITEUINT8(save->p, ht->roverpusher);
	WRITEINT32(save->p, ht->referrer);
	WRITEINT32(save->p, ht->exclusive);
	WRITEINT32(save->p, ht->slider);
}

static void SaveLaserThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const laserthink_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT16(save->p, ht->tag);
	WRITEUINT32(save->p, SaveLine(ht->sourceline));
	WRITEUINT8(save->p, ht->nobosses);
}

static void SaveLightlevelThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const lightlevel_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEINT16(save->p, ht->sourcelevel);
	WRITEINT16(save->p, ht->destlevel);
	WRITEFIXED(save->p, ht->fixedcurlevel);
	WRITEFIXED(save->p, ht->fixedpertic);
	WRITEINT32(save->p, ht->timer);
}

static void SaveExecutorThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const executor_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveLine(ht->line));
	WRITEUINT32(save->p, SaveMobjnum(ht->caller));
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEINT32(save->p, ht->timer);
}

static void SaveDisappearThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const disappear_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, ht->appeartime);
	WRITEUINT32(save->p, ht->disappeartime);
	WRITEUINT32(save->p, ht->offset);
	WRITEUINT32(save->p, ht->timer);
	WRITEINT32(save->p, ht->affectee);
	WRITEINT32(save->p, ht->sourceline);
	WRITEINT32(save->p, ht->exists);
}

static void SaveFadeThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const fade_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, CheckAddNetColormapToList(ht->dest_exc));
	WRITEUINT32(save->p, ht->sectornum);
	WRITEUINT32(save->p, ht->ffloornum);
	WRITEINT32(save->p, ht->alpha);
	WRITEINT16(save->p, ht->sourcevalue);
	WRITEINT16(save->p, ht->destvalue);
	WRITEINT16(save->p, ht->destlightlevel);
	WRITEINT16(save->p, ht->speed);
	WRITEUINT8(save->p, (UINT8)ht->ticbased);
	WRITEINT32(save->p, ht->timer);
	WRITEUINT8(save->p, ht->doexists);
	WRITEUINT8(save->p, ht->dotranslucent);
	WRITEUINT8(save->p, ht->dolighting);
	WRITEUINT8(save->p, ht->docolormap);
	WRITEUINT8(save->p, ht->docollision);
	WRITEUINT8(save->p, ht->doghostfade);
	WRITEUINT8(save->p, ht->exactalpha);
}

static void SaveFadeColormapThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const fadecolormap_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveSector(ht->sector));
	WRITEUINT32(save->p, CheckAddNetColormapToList(ht->source_exc));
	WRITEUINT32(save->p, CheckAddNetColormapToList(ht->dest_exc));
	WRITEUINT8(save->p, (UINT8)ht->ticbased);
	WRITEINT32(save->p, ht->duration);
	WRITEINT32(save->p, ht->timer);
}

static void SavePlaneDisplaceThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const planedisplace_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->affectee);
	WRITEINT32(save->p, ht->control);
	WRITEFIXED(save->p, ht->last_height);
	WRITEFIXED(save->p, ht->speed);
	WRITEUINT8(save->p, ht->type);
}

static inline void SaveDynamicLineSlopeThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const dynlineplanethink_t* ht = (const void*)th;

	WRITEUINT8(save->p, type);
	WRITEUINT8(save->p, ht->type);
	WRITEUINT32(save->p, SaveSlope(ht->slope));
	WRITEUINT32(save->p, SaveLine(ht->sourceline));
	WRITEFIXED(save->p, ht->extent);
}

static inline void SaveDynamicVertexSlopeThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	size_t i;
	const dynvertexplanethink_t* ht = (const void*)th;

	WRITEUINT8(save->p, type);
	WRITEUINT32(save->p, SaveSlope(ht->slope));
	for (i = 0; i < 3; i++)
		WRITEUINT32(save->p, SaveSector(ht->secs[i]));
	WRITEMEM(save->p, ht->vex, sizeof(ht->vex));
	WRITEMEM(save->p, ht->origsecheights, sizeof(ht->origsecheights));
	WRITEMEM(save->p, ht->origvecheights, sizeof(ht->origvecheights));
	WRITEUINT8(save->p, ht->relative);
}

static inline void SavePolyrotatetThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const polyrotate_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->polyObjNum);
	WRITEINT32(save->p, ht->speed);
	WRITEINT32(save->p, ht->distance);
	WRITEUINT8(save->p, ht->turnobjs);
}

static void SavePolymoveThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const polymove_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->polyObjNum);
	WRITEINT32(save->p, ht->speed);
	WRITEFIXED(save->p, ht->momx);
	WRITEFIXED(save->p, ht->momy);
	WRITEINT32(save->p, ht->distance);
	WRITEANGLE(save->p, ht->angle);
}

static void SavePolywaypointThinker(savebuffer_t *save, const thinker_t *th, UINT8 type)
{
	const polywaypoint_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->polyObjNum);
	WRITEINT32(save->p, ht->speed);
	WRITEINT32(save->p, ht->sequence);
	WRITEINT32(save->p, ht->pointnum);
	WRITEINT32(save->p, ht->direction);
	WRITEUINT8(save->p, ht->returnbehavior);
	WRITEUINT8(save->p, ht->continuous);
	WRITEUINT8(save->p, ht->stophere);
}

static void SavePolyslidedoorThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const polyslidedoor_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->polyObjNum);
	WRITEINT32(save->p, ht->delay);
	WRITEINT32(save->p, ht->delayCount);
	WRITEINT32(save->p, ht->initSpeed);
	WRITEINT32(save->p, ht->speed);
	WRITEINT32(save->p, ht->initDistance);
	WRITEINT32(save->p, ht->distance);
	WRITEUINT32(save->p, ht->initAngle);
	WRITEUINT32(save->p, ht->angle);
	WRITEUINT32(save->p, ht->revAngle);
	WRITEFIXED(save->p, ht->momx);
	WRITEFIXED(save->p, ht->momy);
	WRITEUINT8(save->p, ht->closing);
}

static void SavePolyswingdoorThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const polyswingdoor_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->polyObjNum);
	WRITEINT32(save->p, ht->delay);
	WRITEINT32(save->p, ht->delayCount);
	WRITEINT32(save->p, ht->initSpeed);
	WRITEINT32(save->p, ht->speed);
	WRITEINT32(save->p, ht->initDistance);
	WRITEINT32(save->p, ht->distance);
	WRITEUINT8(save->p, ht->closing);
}

static void SavePolydisplaceThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const polydisplace_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->polyObjNum);
	WRITEUINT32(save->p, SaveSector(ht->controlSector));
	WRITEFIXED(save->p, ht->dx);
	WRITEFIXED(save->p, ht->dy);
	WRITEFIXED(save->p, ht->oldHeights);
}

static void SavePolyrotdisplaceThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const polyrotdisplace_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->polyObjNum);
	WRITEUINT32(save->p, SaveSector(ht->controlSector));
	WRITEFIXED(save->p, ht->rotscale);
	WRITEUINT8(save->p, ht->turnobjs);
	WRITEFIXED(save->p, ht->oldHeights);
}

static void SavePolyfadeThinker(savebuffer_t *save, const thinker_t *th, const UINT8 type)
{
	const polyfade_t *ht = (const void *)th;
	WRITEUINT8(save->p, type);
	WRITEINT32(save->p, ht->polyObjNum);
	WRITEINT32(save->p, ht->sourcevalue);
	WRITEINT32(save->p, ht->destvalue);
	WRITEUINT8(save->p, (UINT8)ht->docollision);
	WRITEUINT8(save->p, (UINT8)ht->doghostfade);
	WRITEUINT8(save->p, (UINT8)ht->ticbased);
	WRITEINT32(save->p, ht->duration);
	WRITEINT32(save->p, ht->timer);
}

static void P_NetArchiveThinkers(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	const thinker_t *th;
	UINT32 i;

	WRITEUINT32(save->p, ARCHIVEBLOCK_THINKERS);

	for (i = 0; i < NUM_THINKERLISTS; i++)
	{
		UINT32 numsaved = 0;
		// save off the current thinkers
		for (th = thlist[i].next; th != &thlist[i]; th = th->next)
		{
			if (!(th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed
			 || th->function.acp1 == (actionf_p1)P_NullPrecipThinker))
				numsaved++;

			if (th->function.acp1 == (actionf_p1)P_MobjThinker)
			{
				SaveMobjThinker(save, th, tc_mobj);
				continue;
			}
	#ifdef PARANOIA
			else if (th->function.acp1 == (actionf_p1)P_NullPrecipThinker);
	#endif
			else if (th->function.acp1 == (actionf_p1)T_MoveCeiling)
			{
				SaveCeilingThinker(save, th, tc_ceiling);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_CrushCeiling)
			{
				SaveCeilingThinker(save, th, tc_crushceiling);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_MoveFloor)
			{
				SaveFloormoveThinker(save, th, tc_floor);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_LightningFlash)
			{
				SaveLightflashThinker(save, th, tc_flash);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_StrobeFlash)
			{
				SaveStrobeThinker(save, th, tc_strobe);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_Glow)
			{
				SaveGlowThinker(save, th, tc_glow);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_FireFlicker)
			{
				SaveFireflickerThinker(save, th, tc_fireflicker);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_MoveElevator)
			{
				SaveElevatorThinker(save, th, tc_elevator);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_ContinuousFalling)
			{
				SaveContinuousFallThinker(save, th, tc_continuousfalling);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_ThwompSector)
			{
				SaveThwompThinker(save, th, tc_thwomp);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_NoEnemiesSector)
			{
				SaveNoEnemiesThinker(save, th, tc_noenemies);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_EachTimeThinker)
			{
				SaveEachTimeThinker(save, th, tc_eachtime);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_RaiseSector)
			{
				SaveRaiseThinker(save, th, tc_raisesector);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_CameraScanner)
			{
				SaveElevatorThinker(save, th, tc_camerascanner);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_Scroll)
			{
				SaveScrollThinker(save, th, tc_scroll);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_Friction)
			{
				SaveFrictionThinker(save, th, tc_friction);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_Pusher)
			{
				SavePusherThinker(save, th, tc_pusher);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_BounceCheese)
			{
				SaveBounceCheeseThinker(save, th, tc_bouncecheese);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_StartCrumble)
			{
				SaveCrumbleThinker(save, th, tc_startcrumble);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_MarioBlock)
			{
				SaveMarioBlockThinker(save, th, tc_marioblock);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_MarioBlockChecker)
			{
				SaveMarioCheckThinker(save, th, tc_marioblockchecker);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_FloatSector)
			{
				SaveFloatThinker(save, th, tc_floatsector);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_LaserFlash)
			{
				SaveLaserThinker(save, th, tc_laserflash);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_LightFade)
			{
				SaveLightlevelThinker(save, th, tc_lightfade);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_ExecutorDelay)
			{
				SaveExecutorThinker(save, th, tc_executor);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_Disappear)
			{
				SaveDisappearThinker(save, th, tc_disappear);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_Fade)
			{
				SaveFadeThinker(save, th, tc_fade);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_FadeColormap)
			{
				SaveFadeColormapThinker(save, th, tc_fadecolormap);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PlaneDisplace)
			{
				SavePlaneDisplaceThinker(save, th, tc_planedisplace);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PolyObjRotate)
			{
				SavePolyrotatetThinker(save, th, tc_polyrotate);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PolyObjMove)
			{
				SavePolymoveThinker(save, th, tc_polymove);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PolyObjWaypoint)
			{
				SavePolywaypointThinker(save, th, tc_polywaypoint);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PolyDoorSlide)
			{
				SavePolyslidedoorThinker(save, th, tc_polyslidedoor);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PolyDoorSwing)
			{
				SavePolyswingdoorThinker(save, th, tc_polyswingdoor);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PolyObjFlag)
			{
				SavePolymoveThinker(save, th, tc_polyflag);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PolyObjDisplace)
			{
				SavePolydisplaceThinker(save, th, tc_polydisplace);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PolyObjRotDisplace)
			{
				SavePolyrotdisplaceThinker(save, th, tc_polyrotdisplace);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_PolyObjFade)
			{
				SavePolyfadeThinker(save, th, tc_polyfade);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_DynamicSlopeLine)
			{
				SaveDynamicLineSlopeThinker(save, th, tc_dynslopeline);
				continue;
			}
			else if (th->function.acp1 == (actionf_p1)T_DynamicSlopeVert)
			{
				SaveDynamicVertexSlopeThinker(save, th, tc_dynslopevert);
				continue;
			}
#ifdef PARANOIA
			else
				I_Assert(th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed); // wait garbage collection
#endif
		}

		CONS_Debug(DBG_NETPLAY, "%u thinkers saved in list %d\n", numsaved, i);

		WRITEUINT8(save->p, tc_end);
	}
	TracyCZoneEnd(__zone);
}

static void P_NetArchiveWaypoints(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	waypoint_t *waypoint;
	size_t i;
	size_t numWaypoints = K_GetNumWaypoints();

	WRITEUINT32(save->p, ARCHIVEBLOCK_WAYPOINTS);
	WRITEUINT32(save->p, numWaypoints);

	for (i = 0U; i < numWaypoints; i++) {
		waypoint = K_GetWaypointFromIndex(i);

		// The only thing we save for each waypoint is the mobj.
		// Waypoints should NEVER be completely created or destroyed mid-race as a result of this
		WRITEUINT32(save->p, waypoint->mobj->mobjnum);
	}
	TracyCZoneEnd(__zone);
}

static void P_NetUnArchiveWaypoints(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	if (READUINT32(save->p) != ARCHIVEBLOCK_WAYPOINTS)
		I_Error("Bad $$$.sav at archive block Waypoints!");
	else {
		UINT32 numArchiveWaypoints = READUINT32(save->p);
		size_t numSpawnedWaypoints = K_GetNumWaypoints();

		if (numArchiveWaypoints != numSpawnedWaypoints) {
			I_Error("Bad $$$.sav: More saved waypoints than created!");
		} else {
			waypoint_t *waypoint;
			UINT32 i;
			UINT32 temp;
			for (i = 0U; i < numArchiveWaypoints; i++) {
				waypoint = K_GetWaypointFromIndex(i);
				temp = READUINT32(save->p);
				waypoint->mobj = NULL;
				if (!P_SetTarget(&waypoint->mobj, P_FindNewPosition(temp))) {
					CONS_Debug(DBG_GAMELOGIC, "waypoint mobj not found for %d\n", i);
				}
			}
		}
	}

	TracyCZoneEnd(__zone);
}

static void P_NetSyncTubeWaypoints(savebuffer_t *save)
{
	TracyCZone(__zone, true);
	INT32 i, j;
	UINT32 mobjnum;

	for (i = 0; i < NUMTUBEWAYPOINTSEQUENCES; i++)
	{
		numtubewaypoints[i] = P_SyncUINT16(save, numtubewaypoints[i]);
		for (j = 0; j < numtubewaypoints[i]; j++)
		{
			mobjnum = tubewaypoints[i][j] ? tubewaypoints[i][j]->mobjnum : 0;
			mobjnum = P_SyncUINT32(save, mobjnum);
			if (!save->write)
				tubewaypoints[i][j] = (mobjnum == 0) ? NULL : P_FindNewPosition(mobjnum);
		}
	}
	TracyCZoneEnd(__zone);
}

// Now save the pointers, tracer and target, but at load time we must
// relink to this; the savegame contains the old position in the pointer
// field copyed in the info field temporarily, but finally we just search
// for the old position and relink to it.
mobj_t *P_FindNewPosition(UINT32 oldposition)
{
	thinker_t *th;
	mobj_t *mobj;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mobj = (mobj_t *)th;
		if (mobj->mobjnum != oldposition)
			continue;

		return mobj;
	}
	CONS_Debug(DBG_GAMELOGIC, "mobj %d not found\n", oldposition);
	return NULL;
}

static inline mobj_t *LoadMobj(UINT32 mobjnum)
{
	if (mobjnum == 0) return NULL;
	return (mobj_t *)(size_t)mobjnum;
}

static sector_t *LoadSector(UINT32 sector)
{
	if (sector >= numsectors) return NULL;
	return &sectors[sector];
}

static line_t *LoadLine(UINT32 line)
{
	if (line >= numlines) return NULL;
	return &lines[line];
}

static inline player_t *LoadPlayer(UINT32 player)
{
	if (player >= MAXPLAYERS) return NULL;
	return &players[player];
}

static inline pslope_t *LoadSlope(UINT32 slopeid)
{
	pslope_t *p = slopelist;
	if (slopeid > slopecount) return NULL;
	do
	{
		if (p->id == slopeid)
			return p;
	} while ((p = p->next));
	return NULL;
}

static mobjtype_t g_doomednum_to_mobjtype[UINT16_MAX];

static void CalculateDoomednumToMobjtype(void)
{
	memset(g_doomednum_to_mobjtype, MT_NULL, sizeof(g_doomednum_to_mobjtype));

	for (size_t i = MT_NULL+1; i < NUMMOBJTYPES; i++)
	{
		if (mobjinfo[i].doomednum > 0 && mobjinfo[i].doomednum <= UINT16_MAX)
		{
			g_doomednum_to_mobjtype[ mobjinfo[i].doomednum ] = i;
		}
	}
}

static thinker_t* LoadMobjThinker(savebuffer_t *save, actionf_p1 thinker, UINT8 tclass)
{
	mobj_t *mobj;
	UINT32 diff;
	UINT32 diff2;
	UINT32 diff3;
	INT32 i;
	fixed_t z, floorz, ceilingz;
	ffloor_t *floorrover = NULL, *ceilingrover = NULL;
	size_t j;

	diff = READUINT32(save->p);
	if (diff & MD_MORE)
		diff2 = READUINT32(save->p);
	else
		diff2 = 0;

	if (diff2 & MD2_MORE)
		diff3 = READUINT32(save->p);
	else
		diff3 = 0;

	z = READFIXED(save->p); // Force this so 3dfloor problems don't arise.
	floorz = READFIXED(save->p);
	ceilingz = READFIXED(save->p);

	if (diff2 & MD2_FLOORROVER)
	{
		sector_t *sec = LoadSector(READUINT32(save->p));
		UINT16 id = READUINT16(save->p);
		floorrover = P_GetFFloorByID(sec, id);
	}

	if (diff2 & MD2_CEILINGROVER)
	{
		sector_t *sec = LoadSector(READUINT32(save->p));
		UINT16 id = READUINT16(save->p);
		ceilingrover = P_GetFFloorByID(sec, id);
	}

	if (diff & MD_SPAWNPOINT)
	{
		UINT16 spawnpointnum = READUINT16(save->p);

		if (mapthings[spawnpointnum].type == 1713) // NiGHTS Hoop special case
		{
			P_SpawnHoop(&mapthings[spawnpointnum]);
			return NULL;
		}

		mobj = P_AllocateMobj();

		mobj->spawnpoint = &mapthings[spawnpointnum];
		mapthings[spawnpointnum].mobj = mobj;
	}
	else
		mobj = P_AllocateMobj();

	// declare this as a valid mobj as soon as possible.
	mobj->thinker.function.acp1 = thinker;

	// manually link to thinkerlist, since the thinker isn't returned anymore
	P_AddThinker(tclass, &mobj->thinker);

	mobj->z = z;
	mobj->floorz = floorz;
	mobj->ceilingz = ceilingz;
	mobj->floorrover = floorrover;
	mobj->ceilingrover = ceilingrover;

	if (diff & MD_TYPE)
		mobj->type = READUINT32(save->p);
	else
	{
		mobjtype_t new_type = MT_NULL;
		if (mobj->spawnpoint)
		{
			new_type = g_doomednum_to_mobjtype[mobj->spawnpoint->type];
		}

		if (new_type <= MT_NULL || new_type >= NUMMOBJTYPES)
		{
			if (mobj->spawnpoint)
				CONS_Alert(CONS_ERROR, "Found mobj with unknown map thing doomednum %d\n", mobj->spawnpoint->type);
			else
				CONS_Alert(CONS_ERROR, "Found mobj with unknown map thing doomednum NULL\n");

			I_Error("Netsave corrupted");
		}

		mobj->type = new_type;
	}
	mobj->info = &mobjinfo[mobj->type];

	if (diff & MD_POS)
	{
		mobj->x = mobj->old_x = READFIXED(save->p);
		mobj->y = mobj->old_y = READFIXED(save->p);
		mobj->angle = mobj->old_angle = READANGLE(save->p);
		mobj->pitch = mobj->old_pitch = READANGLE(save->p);
		mobj->roll = mobj->old_roll = READANGLE(save->p);
	}
	else
	{
		mobj->x = mobj->old_x = mobj->spawnpoint->x << FRACBITS;
		mobj->y = mobj->old_y = mobj->spawnpoint->y << FRACBITS;
		mobj->angle = mobj->old_angle = FixedAngle(mobj->spawnpoint->angle*FRACUNIT);
		mobj->pitch = mobj->old_pitch = FixedAngle(mobj->spawnpoint->pitch*FRACUNIT);
		mobj->roll = mobj->old_roll = FixedAngle(mobj->spawnpoint->roll*FRACUNIT);
	}
	if (diff & MD_MOM)
	{
		mobj->momx = READFIXED(save->p);
		mobj->momy = READFIXED(save->p);
		mobj->momz = READFIXED(save->p);
		mobj->pmomz = READFIXED(save->p);
	} // otherwise they're zero, and the memset took care of it

	if (diff & MD_RADIUS)
		mobj->radius = READFIXED(save->p);
	else
		mobj->radius = FixedMul(mobj->info->radius, mapobjectscale);
	if (diff & MD_HEIGHT)
		mobj->height = READFIXED(save->p);
	else
		mobj->height = FixedMul(mobj->info->height, mapobjectscale);
	if (diff & MD_FLAGS)
		mobj->flags = READUINT32(save->p);
	else
		mobj->flags = mobj->info->flags;
	if (diff & MD_FLAGS2)
		mobj->flags2 = READUINT32(save->p);
	if (diff & MD_HEALTH)
		mobj->health = READINT32(save->p);
	else
		mobj->health = mobj->info->spawnhealth;
	if (diff & MD_RTIME)
		mobj->reactiontime = READINT32(save->p);
	else
		mobj->reactiontime = mobj->info->reactiontime;

	if (diff & MD_STATE)
		mobj->state = &states[READUINT16(save->p)];
	else
		mobj->state = &states[mobj->info->spawnstate];
	if (diff & MD_TICS)
		mobj->tics = READINT32(save->p);
	else
		mobj->tics = mobj->state->tics;
	if (diff & MD_SPRITE) {
		mobj->sprite = READUINT16(save->p);
		if (mobj->sprite == SPR_PLAY)
			mobj->sprite2 = READUINT8(save->p);
	}
	else {
		mobj->sprite = mobj->state->sprite;
		if (mobj->sprite == SPR_PLAY)
			mobj->sprite2 = mobj->state->frame&FF_FRAMEMASK;
	}
	if (diff & MD_FRAME)
	{
		mobj->frame = READUINT32(save->p);
		mobj->anim_duration = READUINT16(save->p);
	}
	else
	{
		mobj->frame = mobj->state->frame;
		mobj->anim_duration = (UINT16)mobj->state->var2;
	}
	if (diff & MD_EFLAGS)
		mobj->eflags = READUINT16(save->p);
	if (diff & MD_PLAYER)
	{
		i = READUINT8(save->p);
		mobj->player = &players[i];
		mobj->player->mo = mobj;
	}
	if (diff & MD_MOVEDIR)
		mobj->movedir = READANGLE(save->p);
	if (diff & MD_MOVECOUNT)
		mobj->movecount = READINT32(save->p);
	if (diff & MD_THRESHOLD)
		mobj->threshold = READINT32(save->p);
	if (diff & MD_LASTLOOK)
		mobj->lastlook = READINT32(save->p);
	else
		mobj->lastlook = -1;
	if (diff & MD_TARGET)
		mobj->target = (mobj_t *)(size_t)READUINT32(save->p);
	if (diff & MD_TRACER)
		mobj->tracer = (mobj_t *)(size_t)READUINT32(save->p);
	if (diff & MD_FRICTION)
		mobj->friction = READFIXED(save->p);
	else
		mobj->friction = ORIG_FRICTION;
	if (diff & MD_MOVEFACTOR)
		mobj->movefactor = READFIXED(save->p);
	else
		mobj->movefactor = FRACUNIT;
	if (diff & MD_FUSE)
		mobj->fuse = READINT32(save->p);
	if (diff & MD_WATERTOP)
		mobj->watertop = READFIXED(save->p);
	else
		mobj->watertop = INT32_MAX;
	if (diff & MD_WATERBOTTOM)
		mobj->waterbottom = READFIXED(save->p);
	if (diff & MD_SCALE)
		mobj->scale = READFIXED(save->p);
	else
		mobj->scale = mapobjectscale;
	if (diff & MD_DSCALE)
		mobj->destscale = READFIXED(save->p);
	else
		mobj->destscale = mobj->scale;
	if (diff2 & MD2_SCALESPEED)
		mobj->scalespeed = READFIXED(save->p);
	else
		mobj->scalespeed = mapobjectscale/12;
	if (diff & MD_ARGS)
	{
		for (j = 0; j < NUM_MAPTHING_ARGS; j++)
			mobj->args[j] = READINT32(save->p);
	}
	if (diff & MD_STRINGARGS)
	{
		for (j = 0; j < NUM_MAPTHING_STRINGARGS; j++)
		{
			size_t len = READINT32(save->p);
			size_t k;

			if (!len)
			{
				Z_Free(mobj->stringargs[j]);
				mobj->stringargs[j] = NULL;
				continue;
			}

			mobj->stringargs[j] = Z_Realloc(mobj->stringargs[j], len + 1, PU_LEVEL, NULL);
			for (k = 0; k < len; k++)
				mobj->stringargs[j][k] = READCHAR(save->p);
			mobj->stringargs[j][len] = '\0';
		}
	}
	if (diff2 & MD2_CUSVAL)
		mobj->cusval = READINT32(save->p);
	if (diff2 & MD2_CVMEM)
		mobj->cvmem = READINT32(save->p);
	if (diff2 & MD2_SKIN)
		mobj->skin = &skins[READUINT16(save->p)];
	if (diff2 & MD2_COLOR)
		mobj->color = READUINT16(save->p);
	if (diff2 & MD2_EXTVAL1)
		mobj->extravalue1 = READINT32(save->p);
	if (diff2 & MD2_EXTVAL2)
		mobj->extravalue2 = READINT32(save->p);
	if (diff2 & MD2_HNEXT)
		mobj->hnext = (mobj_t *)(size_t)READUINT32(save->p);
	if (diff2 & MD2_HPREV)
		mobj->hprev = (mobj_t *)(size_t)READUINT32(save->p);
	if (diff2 & MD2_ITNEXT)
		mobj->itnext = (mobj_t *)(size_t)READUINT32(save->p);
	if (diff2 & MD2_SLOPE)
		mobj->standingslope = P_SlopeById(READUINT16(save->p));
	if (diff2 & MD2_COLORIZED)
		mobj->colorized = READUINT8(save->p);
	if (diff2 & MD2_MIRRORED)
		mobj->mirrored = READUINT8(save->p);
	if (diff2 & MD2_ROLLANGLE)
		mobj->rollangle = READANGLE(save->p);
	if (diff2 & MD2_SHADOWSCALE)
	{
		mobj->shadowscale = READFIXED(save->p);
		mobj->whiteshadow = READUINT8(save->p);
		mobj->shadowcolor = READUINT8(save->p);
	}
	if (diff2 & MD2_RENDERFLAGS)
		mobj->renderflags = READUINT32(save->p);
	if (diff2 & MD2_TID)
		mobj->tid = READINT16(save->p);
	if (diff2 & MD2_SPRITESCALE)
	{
		mobj->spritexscale = READFIXED(save->p);
		mobj->spriteyscale = READFIXED(save->p);
	}
	else
	{
		mobj->spritexscale = mobj->spriteyscale = FRACUNIT;
	}
	if (diff2 & MD2_SPRITEOFFSET)
	{
		mobj->spritexoffset = READFIXED(save->p);
		mobj->spriteyoffset = READFIXED(save->p);
		mobj->rollingxoffset = READINT16(save->p);
		mobj->rollingyoffset = READINT16(save->p);
	}
	else
	{
		mobj->spritexoffset = mobj->spriteyoffset = 0;
		mobj->rollingxoffset = mobj->rollingyoffset = 0;
	}
	if (diff2 & MD2_WORLDOFFSET)
	{
		mobj->sprxoff = READFIXED(save->p);
		mobj->spryoff = READFIXED(save->p);
		mobj->sprzoff = READFIXED(save->p);
	}
	else
	{
		mobj->sprxoff = mobj->spryoff = mobj->sprzoff = 0;
	}
	if (diff2 & MD2_SPECIAL)
	{
		mobj->special = READINT16(save->p);

		for (j = 0; j < NUM_SCRIPT_ARGS; j++)
			mobj->script_args[j] = READINT32(save->p);

		for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
		{
			size_t len = READINT32(save->p);
			size_t k;

			if (!len)
			{
				Z_Free(mobj->script_stringargs[j]);
				mobj->script_stringargs[j] = NULL;
				continue;
			}

			mobj->script_stringargs[j] = Z_Realloc(mobj->script_stringargs[j], len + 1, PU_LEVEL, NULL);
			for (k = 0; k < len; k++)
				mobj->script_stringargs[j][k] = READCHAR(save->p);
			mobj->script_stringargs[j][len] = '\0';
		}
	}
	if (diff2 & MD2_FLOORSPRITESLOPE)
	{
		pslope_t *slope = (pslope_t *)P_CreateFloorSpriteSlope(mobj);

		slope->zdelta = READFIXED(save->p);
		slope->zangle = READANGLE(save->p);
		slope->xydirection = READANGLE(save->p);

		slope->o.x = READFIXED(save->p);
		slope->o.y = READFIXED(save->p);
		slope->o.z = READFIXED(save->p);

		slope->d.x = READFIXED(save->p);
		slope->d.y = READFIXED(save->p);

		slope->normal.x = READFIXED(save->p);
		slope->normal.y = READFIXED(save->p);
		slope->normal.z = READFIXED(save->p);

		P_UpdateSlopeLightOffset(slope);
	}
	if (diff2 & MD2_LIGHTLEVEL)
	{
		mobj->lightlevel = READINT16(save->p);
	}
	if (diff2 & MD2_DISPOFFSET)
	{
		mobj->dispoffset = READINT32(save->p);
	}
	if (diff2 & MD2_LASTMOMZ)
	{
		mobj->lastmomz = READINT32(save->p);
	}
	if (diff2 & MD2_TERRAIN)
	{
		UINT32 terrain_index = READUINT32(save->p);
		if (terrain_index > 0)
			mobj->terrain = K_GetTerrainByIndex(terrain_index - 1);
		mobj->terrainOverlay = (mobj_t *)(size_t)READUINT32(save->p);
	}
	else
	{
		mobj->terrain = NULL;
	}
	if (diff3 & MD3_GRAVITY)
	{
		mobj->gravity = READFIXED(save->p);
	}
	else
	{
		mobj->gravity = FRACUNIT;
	}
	if (diff3 & MD3_BAKEDOFFSET)
	{
		mobj->bakexoff = READFIXED(save->p);
		mobj->bakeyoff = READFIXED(save->p);
		mobj->bakezoff = READFIXED(save->p);
		mobj->bakexpiv = READFIXED(save->p);
		mobj->bakeypiv = READFIXED(save->p);
		mobj->bakezpiv = READFIXED(save->p);
	}
	else
	{
		mobj->bakexoff = mobj->bakeyoff = mobj->bakezoff = 0;
		mobj->bakexpiv = mobj->bakeypiv = mobj->bakezpiv = 0;
	}
	if (diff3 & MD3_EXTVAL3)
	{
		mobj->extravalue3 = READINT32(save->p);
	}
	if (diff3 & MD3_SHIELDTRACER)
	{
		mobj->shieldtracer = (mobj_t *)(size_t)READUINT32(save->p);
	}

	// Reset some non-synch values
	mobj->sloperoll = 0;
	mobj->slopepitch = 0;

	// link tid set earlier
	P_AddThingTID(mobj);

	// set sprev, snext, bprev, bnext, subsector
	P_SetThingPosition(mobj);

	mobj->mobjnum = READUINT32(save->p);

	if (mobj->player)
	{
		if (mobj->eflags & MFE_VERTICALFLIP)
			mobj->player->viewz = mobj->z + mobj->height - mobj->player->viewheight;
		else
			mobj->player->viewz = mobj->player->mo->z + mobj->player->viewheight;
	}

	if (mobj->type == MT_SKYBOX && mobj->spawnpoint)
	{
		P_InitSkyboxPoint(mobj, mobj->spawnpoint);
	}

	if (diff2 & MD2_BOSS3CAP)
		P_SetTarget(&boss3cap, mobj);
	
	if (diff2 & MD2_WAYPOINTCAP)
		P_SetTarget(&waypointcap, mobj);

	if (diff2 & MD2_KITEMCAP)
		P_SetTarget(&kitemcap, mobj);

	if (diff3 & MD3_MISCCAP)
		P_SetTarget(&misccap, mobj);

	R_AddMobjInterpolator(mobj);

	// don't allow the mobj's refcount to be reset by P_AddThinker
	// we might've already called P_SetTarget!
	return NULL;//&mobj->thinker;
}

static thinker_t* LoadNoEnemiesThinker(savebuffer_t *save, actionf_p1 thinker)
{
	noenemies_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sourceline = LoadLine(READUINT32(save->p));
	return &ht->thinker;
}

static thinker_t* LoadBounceCheeseThinker(savebuffer_t *save, actionf_p1 thinker)
{
	bouncecheese_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sourceline = LoadLine(READUINT32(save->p));
	ht->sector = LoadSector(READUINT32(save->p));
	ht->speed = READFIXED(save->p);
	ht->distance = READFIXED(save->p);
	ht->floorwasheight = READFIXED(save->p);
	ht->ceilingwasheight = READFIXED(save->p);
	ht->low = READCHAR(save->p);

	if (ht->sector)
		ht->sector->ceilingdata = ht;

	return &ht->thinker;
}

static thinker_t* LoadContinuousFallThinker(savebuffer_t *save, actionf_p1 thinker)
{
	continuousfall_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sector = LoadSector(READUINT32(save->p));
	ht->speed = READFIXED(save->p);
	ht->direction = READINT32(save->p);
	ht->floorstartheight = READFIXED(save->p);
	ht->ceilingstartheight = READFIXED(save->p);
	ht->destheight = READFIXED(save->p);

	if (ht->sector)
	{
		ht->sector->ceilingdata = ht;
		ht->sector->floordata = ht;
	}

	return &ht->thinker;
}

static thinker_t* LoadMarioBlockThinker(savebuffer_t *save, actionf_p1 thinker)
{
	mariothink_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sector = LoadSector(READUINT32(save->p));
	ht->speed = READFIXED(save->p);
	ht->direction = READINT32(save->p);
	ht->floorstartheight = READFIXED(save->p);
	ht->ceilingstartheight = READFIXED(save->p);
	ht->tag = READINT16(save->p);

	if (ht->sector)
	{
		ht->sector->ceilingdata = ht;
		ht->sector->floordata = ht;
	}

	return &ht->thinker;
}

static thinker_t* LoadMarioCheckThinker(savebuffer_t *save, actionf_p1 thinker)
{
	mariocheck_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sourceline = LoadLine(READUINT32(save->p));
	ht->sector = LoadSector(READUINT32(save->p));
	return &ht->thinker;
}

static thinker_t* LoadThwompThinker(savebuffer_t *save, actionf_p1 thinker)
{
	thwomp_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sourceline = LoadLine(READUINT32(save->p));
	ht->sector = LoadSector(READUINT32(save->p));
	ht->crushspeed = READFIXED(save->p);
	ht->retractspeed = READFIXED(save->p);
	ht->direction = READINT32(save->p);
	ht->floorstartheight = READFIXED(save->p);
	ht->ceilingstartheight = READFIXED(save->p);
	ht->delay = READINT32(save->p);
	ht->tag = READINT16(save->p);
	ht->sound = READUINT16(save->p);
	ht->initDelay = READINT32(save->p);

	if (ht->sector)
	{
		ht->sector->ceilingdata = ht;
		ht->sector->floordata = ht;
	}

	return &ht->thinker;
}

static thinker_t* LoadFloatThinker(savebuffer_t *save, actionf_p1 thinker)
{
	floatthink_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sourceline = LoadLine(READUINT32(save->p));
	ht->sector = LoadSector(READUINT32(save->p));
	ht->tag = READINT16(save->p);
	return &ht->thinker;
}

static thinker_t* LoadEachTimeThinker(savebuffer_t *save, actionf_p1 thinker)
{
	size_t i;
	eachtime_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sourceline = LoadLine(READUINT32(save->p));
	for (i = 0; i < MAXPLAYERS; i++)
	{
		ht->playersInArea[i] = READCHAR(save->p);
	}
	ht->triggerOnExit = READCHAR(save->p);
	return &ht->thinker;
}

static thinker_t* LoadRaiseThinker(savebuffer_t *save, actionf_p1 thinker)
{
	raise_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->tag = READINT16(save->p);
	ht->sector = LoadSector(READUINT32(save->p));
	ht->ceilingbottom = READFIXED(save->p);
	ht->ceilingtop = READFIXED(save->p);
	ht->basespeed = READFIXED(save->p);
	ht->extraspeed = READFIXED(save->p);
	ht->shaketimer = READUINT8(save->p);
	ht->flags = READUINT8(save->p);
	return &ht->thinker;
}

static thinker_t* LoadCeilingThinker(savebuffer_t *save, actionf_p1 thinker)
{
	ceiling_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->type = READUINT8(save->p);
	ht->sector = LoadSector(READUINT32(save->p));
	ht->bottomheight = READFIXED(save->p);
	ht->topheight = READFIXED(save->p);
	ht->speed = READFIXED(save->p);
	ht->delay = READFIXED(save->p);
	ht->delaytimer = READFIXED(save->p);
	ht->crush = READUINT8(save->p);
	ht->texture = READINT32(save->p);
	ht->direction = READINT32(save->p);
	ht->tag = READINT16(save->p);
	ht->sourceline = READFIXED(save->p);
	ht->origspeed = READFIXED(save->p);
	ht->crushHeight = READFIXED(save->p);
	ht->crushSpeed = READFIXED(save->p);
	ht->returnHeight = READFIXED(save->p);
	ht->returnSpeed = READFIXED(save->p);
	if (ht->sector)
		ht->sector->ceilingdata = ht;
	return &ht->thinker;
}

static thinker_t* LoadFloormoveThinker(savebuffer_t *save, actionf_p1 thinker)
{
	floormove_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->type = READUINT8(save->p);
	ht->crush = READUINT8(save->p);
	ht->sector = LoadSector(READUINT32(save->p));
	ht->direction = READINT32(save->p);
	ht->texture = READINT32(save->p);
	ht->floordestheight = READFIXED(save->p);
	ht->speed = READFIXED(save->p);
	ht->origspeed = READFIXED(save->p);
	ht->delay = READFIXED(save->p);
	ht->delaytimer = READFIXED(save->p);
	ht->tag = READINT16(save->p);
	ht->sourceline = READFIXED(save->p);
	ht->crushHeight = READFIXED(save->p);
	ht->crushSpeed = READFIXED(save->p);
	ht->returnHeight = READFIXED(save->p);
	ht->returnSpeed = READFIXED(save->p);
	if (ht->sector)
		ht->sector->floordata = ht;
	return &ht->thinker;
}

static thinker_t* LoadLightflashThinker(savebuffer_t *save, actionf_p1 thinker)
{
	lightflash_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sector = LoadSector(READUINT32(save->p));
	ht->maxlight = READINT32(save->p);
	ht->minlight = READINT32(save->p);
	if (ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t* LoadStrobeThinker(savebuffer_t *save, actionf_p1 thinker)
{
	strobe_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sector = LoadSector(READUINT32(save->p));
	ht->count = READINT32(save->p);
	ht->minlight = READINT16(save->p);
	ht->maxlight = READINT16(save->p);
	ht->darktime = READINT32(save->p);
	ht->brighttime = READINT32(save->p);
	if (ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t* LoadGlowThinker(savebuffer_t *save, actionf_p1 thinker)
{
	glow_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sector = LoadSector(READUINT32(save->p));
	ht->minlight = READINT16(save->p);
	ht->maxlight = READINT16(save->p);
	ht->direction = READINT16(save->p);
	ht->speed = READINT16(save->p);
	if (ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t* LoadFireflickerThinker(savebuffer_t *save, actionf_p1 thinker)
{
	fireflicker_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sector = LoadSector(READUINT32(save->p));
	ht->count = READINT32(save->p);
	ht->resetcount = READINT32(save->p);
	ht->maxlight = READINT16(save->p);
	ht->minlight = READINT16(save->p);
	if (ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t* LoadElevatorThinker(savebuffer_t *save, actionf_p1 thinker, boolean setplanedata)
{
	elevator_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->type = READUINT8(save->p);
	ht->sector = LoadSector(READUINT32(save->p));
	ht->actionsector = LoadSector(READUINT32(save->p));
	ht->direction = READINT32(save->p);
	ht->floordestheight = READFIXED(save->p);
	ht->ceilingdestheight = READFIXED(save->p);
	ht->speed = READFIXED(save->p);
	ht->origspeed = READFIXED(save->p);
	ht->low = READFIXED(save->p);
	ht->high = READFIXED(save->p);
	ht->distance = READFIXED(save->p);
	ht->delay = READFIXED(save->p);
	ht->delaytimer = READFIXED(save->p);
	ht->floorwasheight = READFIXED(save->p);
	ht->ceilingwasheight = READFIXED(save->p);

	if (ht->sector && setplanedata)
	{
		ht->sector->ceilingdata = ht;
		ht->sector->floordata = ht;
	}

	return &ht->thinker;
}

static thinker_t* LoadCrumbleThinker(savebuffer_t *save, actionf_p1 thinker)
{
	crumble_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sourceline = LoadLine(READUINT32(save->p));
	ht->sector = LoadSector(READUINT32(save->p));
	ht->actionsector = LoadSector(READUINT32(save->p));
	ht->player = LoadPlayer(READUINT32(save->p));
	ht->direction = READINT32(save->p);
	ht->origalpha = READINT32(save->p);
	ht->timer = READINT32(save->p);
	ht->speed = READFIXED(save->p);
	ht->floorwasheight = READFIXED(save->p);
	ht->ceilingwasheight = READFIXED(save->p);
	ht->flags = READUINT8(save->p);

	if (ht->sector)
		ht->sector->floordata = ht;

	return &ht->thinker;
}

static thinker_t* LoadScrollThinker(savebuffer_t *save, actionf_p1 thinker)
{
	scroll_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->dx = READFIXED(save->p);
	ht->dy = READFIXED(save->p);
	ht->affectee = READINT32(save->p);
	ht->control = READINT32(save->p);
	ht->last_height = READFIXED(save->p);
	ht->vdx = READFIXED(save->p);
	ht->vdy = READFIXED(save->p);
	ht->accel = READINT32(save->p);
	ht->exclusive = READINT32(save->p);
	ht->type = READUINT8(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadFrictionThinker(savebuffer_t *save, actionf_p1 thinker)
{
	friction_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->friction = READINT32(save->p);
	ht->movefactor = READINT32(save->p);
	ht->affectee = READINT32(save->p);
	ht->referrer = READINT32(save->p);
	ht->roverfriction = READUINT8(save->p);
	return &ht->thinker;
}

static thinker_t* LoadPusherThinker(savebuffer_t *save, actionf_p1 thinker)
{
	pusher_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->type = READUINT8(save->p);
	ht->x_mag = READFIXED(save->p);
	ht->y_mag = READFIXED(save->p);
	ht->z_mag = READFIXED(save->p);
	ht->affectee = READINT32(save->p);
	ht->roverpusher = READUINT8(save->p);
	ht->referrer = READINT32(save->p);
	ht->exclusive = READINT32(save->p);
	ht->slider = READINT32(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadLaserThinker(savebuffer_t *save, actionf_p1 thinker)
{
	laserthink_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->tag = READINT16(save->p);
	ht->sourceline = LoadLine(READUINT32(save->p));
	ht->nobosses = READUINT8(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadLightlevelThinker(savebuffer_t *save, actionf_p1 thinker)
{
	lightlevel_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sector = LoadSector(READUINT32(save->p));
	ht->sourcelevel = READINT16(save->p);
	ht->destlevel = READINT16(save->p);
	ht->fixedcurlevel = READFIXED(save->p);
	ht->fixedpertic = READFIXED(save->p);
	ht->timer = READINT32(save->p);
	if (ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static inline thinker_t* LoadExecutorThinker(savebuffer_t *save, actionf_p1 thinker)
{
	executor_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->line = LoadLine(READUINT32(save->p));
	ht->caller = LoadMobj(READUINT32(save->p));
	ht->sector = LoadSector(READUINT32(save->p));
	ht->timer = READINT32(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadDisappearThinker(savebuffer_t *save, actionf_p1 thinker)
{
	disappear_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->appeartime = READUINT32(save->p);
	ht->disappeartime = READUINT32(save->p);
	ht->offset = READUINT32(save->p);
	ht->timer = READUINT32(save->p);
	ht->affectee = READINT32(save->p);
	ht->sourceline = READINT32(save->p);
	ht->exists = READINT32(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadFadeThinker(savebuffer_t *save, actionf_p1 thinker)
{
	sector_t *ss;
	fade_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->dest_exc = GetNetColormapFromList(READUINT32(save->p));
	ht->sectornum = READUINT32(save->p);
	ht->ffloornum = READUINT32(save->p);
	ht->alpha = READINT32(save->p);
	ht->sourcevalue = READINT16(save->p);
	ht->destvalue = READINT16(save->p);
	ht->destlightlevel = READINT16(save->p);
	ht->speed = READINT16(save->p);
	ht->ticbased = (boolean)READUINT8(save->p);
	ht->timer = READINT32(save->p);
	ht->doexists = READUINT8(save->p);
	ht->dotranslucent = READUINT8(save->p);
	ht->dolighting = READUINT8(save->p);
	ht->docolormap = READUINT8(save->p);
	ht->docollision = READUINT8(save->p);
	ht->doghostfade = READUINT8(save->p);
	ht->exactalpha = READUINT8(save->p);

	ss = LoadSector(ht->sectornum);
	if (ss)
	{
		size_t j = 0; // ss->ffloors is saved as ffloor #0, ss->ffloors->next is #1, etc
		ffloor_t *rover;
		for (rover = ss->ffloors; rover; rover = rover->next)
		{
			if (j == ht->ffloornum)
			{
				ht->rover = rover;
				rover->fadingdata = ht;
				break;
			}
			j++;
		}
	}
	return &ht->thinker;
}

static inline thinker_t* LoadFadeColormapThinker(savebuffer_t *save, actionf_p1 thinker)
{
	fadecolormap_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->sector = LoadSector(READUINT32(save->p));
	ht->source_exc = GetNetColormapFromList(READUINT32(save->p));
	ht->dest_exc = GetNetColormapFromList(READUINT32(save->p));
	ht->ticbased = (boolean)READUINT8(save->p);
	ht->duration = READINT32(save->p);
	ht->timer = READINT32(save->p);
	if (ht->sector)
		ht->sector->fadecolormapdata = ht;
	return &ht->thinker;
}

static inline thinker_t* LoadPlaneDisplaceThinker(savebuffer_t *save, actionf_p1 thinker)
{
	planedisplace_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;

	ht->affectee = READINT32(save->p);
	ht->control = READINT32(save->p);
	ht->last_height = READFIXED(save->p);
	ht->speed = READFIXED(save->p);
	ht->type = READUINT8(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadDynamicLineSlopeThinker(savebuffer_t *save, actionf_p1 thinker)
{
	dynlineplanethink_t* ht = Z_Malloc(sizeof(*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;

	ht->type = READUINT8(save->p);
	ht->slope = LoadSlope(READUINT32(save->p));
	ht->sourceline = LoadLine(READUINT32(save->p));
	ht->extent = READFIXED(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadDynamicVertexSlopeThinker(savebuffer_t *save, actionf_p1 thinker)
{
	size_t i;
	dynvertexplanethink_t* ht = Z_Malloc(sizeof(*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;

	ht->slope = LoadSlope(READUINT32(save->p));
	for (i = 0; i < 3; i++)
		ht->secs[i] = LoadSector(READUINT32(save->p));
	READMEM(save->p, ht->vex, sizeof(ht->vex));
	READMEM(save->p, ht->origsecheights, sizeof(ht->origsecheights));
	READMEM(save->p, ht->origvecheights, sizeof(ht->origvecheights));
	ht->relative = READUINT8(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadPolyrotatetThinker(savebuffer_t *save, actionf_p1 thinker)
{
	polyrotate_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->polyObjNum = READINT32(save->p);
	ht->speed = READINT32(save->p);
	ht->distance = READINT32(save->p);
	ht->turnobjs = READUINT8(save->p);
	return &ht->thinker;
}

static thinker_t* LoadPolymoveThinker(savebuffer_t *save, actionf_p1 thinker)
{
	polymove_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->polyObjNum = READINT32(save->p);
	ht->speed = READINT32(save->p);
	ht->momx = READFIXED(save->p);
	ht->momy = READFIXED(save->p);
	ht->distance = READINT32(save->p);
	ht->angle = READANGLE(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadPolywaypointThinker(savebuffer_t *save, actionf_p1 thinker)
{
	polywaypoint_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->polyObjNum = READINT32(save->p);
	ht->speed = READINT32(save->p);
	ht->sequence = READINT32(save->p);
	ht->pointnum = READINT32(save->p);
	ht->direction = READINT32(save->p);
	ht->returnbehavior = READUINT8(save->p);
	ht->continuous = READUINT8(save->p);
	ht->stophere = READUINT8(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadPolyslidedoorThinker(savebuffer_t *save, actionf_p1 thinker)
{
	polyslidedoor_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->polyObjNum = READINT32(save->p);
	ht->delay = READINT32(save->p);
	ht->delayCount = READINT32(save->p);
	ht->initSpeed = READINT32(save->p);
	ht->speed = READINT32(save->p);
	ht->initDistance = READINT32(save->p);
	ht->distance = READINT32(save->p);
	ht->initAngle = READUINT32(save->p);
	ht->angle = READUINT32(save->p);
	ht->revAngle = READUINT32(save->p);
	ht->momx = READFIXED(save->p);
	ht->momy = READFIXED(save->p);
	ht->closing = READUINT8(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadPolyswingdoorThinker(savebuffer_t *save, actionf_p1 thinker)
{
	polyswingdoor_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->polyObjNum = READINT32(save->p);
	ht->delay = READINT32(save->p);
	ht->delayCount = READINT32(save->p);
	ht->initSpeed = READINT32(save->p);
	ht->speed = READINT32(save->p);
	ht->initDistance = READINT32(save->p);
	ht->distance = READINT32(save->p);
	ht->closing = READUINT8(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadPolydisplaceThinker(savebuffer_t *save, actionf_p1 thinker)
{
	polydisplace_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->polyObjNum = READINT32(save->p);
	ht->controlSector = LoadSector(READUINT32(save->p));
	ht->dx = READFIXED(save->p);
	ht->dy = READFIXED(save->p);
	ht->oldHeights = READFIXED(save->p);
	return &ht->thinker;
}

static inline thinker_t* LoadPolyrotdisplaceThinker(savebuffer_t *save, actionf_p1 thinker)
{
	polyrotdisplace_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->polyObjNum = READINT32(save->p);
	ht->controlSector = LoadSector(READUINT32(save->p));
	ht->rotscale = READFIXED(save->p);
	ht->turnobjs = READUINT8(save->p);
	ht->oldHeights = READFIXED(save->p);
	return &ht->thinker;
}

static thinker_t* LoadPolyfadeThinker(savebuffer_t *save, actionf_p1 thinker)
{
	polyfade_t *ht = Z_Malloc(sizeof (*ht), PU_LEVSPEC, NULL);
	ht->thinker.function.acp1 = thinker;
	ht->polyObjNum = READINT32(save->p);
	ht->sourcevalue = READINT32(save->p);
	ht->destvalue = READINT32(save->p);
	ht->docollision = (boolean)READUINT8(save->p);
	ht->doghostfade = (boolean)READUINT8(save->p);
	ht->ticbased = (boolean)READUINT8(save->p);
	ht->duration = READINT32(save->p);
	ht->timer = READINT32(save->p);
	return &ht->thinker;
}

static void P_NetUnArchiveThinkers(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	thinker_t *currentthinker;
	thinker_t *next;
	UINT8 tclass;
	UINT8 restoreNum = false;
	UINT32 i;
	UINT32 numloaded = 0;

	if (READUINT32(save->p) != ARCHIVEBLOCK_THINKERS)
		I_Error("Bad $$$.sav at archive block Thinkers");

	// Pre-calculate this lookup, because it was wasting
	// a shit ton of time loading mobj thinkers.
	CalculateDoomednumToMobjtype();

	// remove all the current thinkers
	for (i = 0; i < NUM_THINKERLISTS; i++)
	{
		for (currentthinker = thlist[i].next; currentthinker != &thlist[i]; currentthinker = next)
		{
			next = currentthinker->next;

			currentthinker->references = 0; // Heinous but this is the only place the assertion in P_UnlinkThinkers is wrong

			if (currentthinker->function.acp1 == (actionf_p1)P_MobjThinker || currentthinker->function.acp1 == (actionf_p1)P_NullPrecipThinker)
				P_RemoveSavegameMobj((mobj_t *)currentthinker); // item isn't saved, don't remove it
			else
			{
				(next->prev = currentthinker->prev)->next = next;
				R_DestroyLevelInterpolators(currentthinker);
				Z_Free(currentthinker);
			}
		}
	}

	// we don't want the removed mobjs to come back
	P_InitThinkers();

	// clear sector thinker pointers so they don't point to non-existant thinkers for all of eternity
	for (i = 0; i < numsectors; i++)
	{
		sectors[i].floordata = sectors[i].ceilingdata = sectors[i].lightingdata = sectors[i].fadecolormapdata = NULL;
	}

	// read in saved thinkers
	for (i = 0; i < NUM_THINKERLISTS; i++)
	{
		for (;;)
		{
			thinker_t* th = NULL;
			tclass = READUINT8(save->p);

			if (tclass == tc_end)
				break; // leave the saved thinker reading loop
			numloaded++;

			switch (tclass)
			{
				case tc_mobj:
					th = LoadMobjThinker(save, (actionf_p1)P_MobjThinker, i);
					break;

				case tc_ceiling:
					th = LoadCeilingThinker(save, (actionf_p1)T_MoveCeiling);
					break;

				case tc_crushceiling:
					th = LoadCeilingThinker(save, (actionf_p1)T_CrushCeiling);
					break;

				case tc_floor:
					th = LoadFloormoveThinker(save, (actionf_p1)T_MoveFloor);
					break;

				case tc_flash:
					th = LoadLightflashThinker(save, (actionf_p1)T_LightningFlash);
					break;

				case tc_strobe:
					th = LoadStrobeThinker(save, (actionf_p1)T_StrobeFlash);
					break;

				case tc_glow:
					th = LoadGlowThinker(save, (actionf_p1)T_Glow);
					break;

				case tc_fireflicker:
					th = LoadFireflickerThinker(save, (actionf_p1)T_FireFlicker);
					break;

				case tc_elevator:
					th = LoadElevatorThinker(save, (actionf_p1)T_MoveElevator, true);
					break;

				case tc_continuousfalling:
					th = LoadContinuousFallThinker(save, (actionf_p1)T_ContinuousFalling);
					break;

				case tc_thwomp:
					th = LoadThwompThinker(save, (actionf_p1)T_ThwompSector);
					break;

				case tc_noenemies:
					th = LoadNoEnemiesThinker(save, (actionf_p1)T_NoEnemiesSector);
					break;

				case tc_eachtime:
					th = LoadEachTimeThinker(save, (actionf_p1)T_EachTimeThinker);
					break;

				case tc_raisesector:
					th = LoadRaiseThinker(save, (actionf_p1)T_RaiseSector);
					break;

				case tc_camerascanner:
					th = LoadElevatorThinker(save, (actionf_p1)T_CameraScanner, false);
					break;

				case tc_bouncecheese:
					th = LoadBounceCheeseThinker(save, (actionf_p1)T_BounceCheese);
					break;

				case tc_startcrumble:
					th = LoadCrumbleThinker(save, (actionf_p1)T_StartCrumble);
					break;

				case tc_marioblock:
					th = LoadMarioBlockThinker(save, (actionf_p1)T_MarioBlock);
					break;

				case tc_marioblockchecker:
					th = LoadMarioCheckThinker(save, (actionf_p1)T_MarioBlockChecker);
					break;

				case tc_floatsector:
					th = LoadFloatThinker(save, (actionf_p1)T_FloatSector);
					break;

				case tc_laserflash:
					th = LoadLaserThinker(save, (actionf_p1)T_LaserFlash);
					break;

				case tc_lightfade:
					th = LoadLightlevelThinker(save, (actionf_p1)T_LightFade);
					break;

				case tc_executor:
					th = LoadExecutorThinker(save, (actionf_p1)T_ExecutorDelay);
					restoreNum = true;
					break;

				case tc_disappear:
					th = LoadDisappearThinker(save, (actionf_p1)T_Disappear);
					break;

				case tc_fade:
					th = LoadFadeThinker(save, (actionf_p1)T_Fade);
					break;

				case tc_fadecolormap:
					th = LoadFadeColormapThinker(save, (actionf_p1)T_FadeColormap);
					break;

				case tc_planedisplace:
					th = LoadPlaneDisplaceThinker(save, (actionf_p1)T_PlaneDisplace);
					break;
				case tc_polyrotate:
					th = LoadPolyrotatetThinker(save, (actionf_p1)T_PolyObjRotate);
					break;

				case tc_polymove:
					th = LoadPolymoveThinker(save, (actionf_p1)T_PolyObjMove);
					break;

				case tc_polywaypoint:
					th = LoadPolywaypointThinker(save, (actionf_p1)T_PolyObjWaypoint);
					break;

				case tc_polyslidedoor:
					th = LoadPolyslidedoorThinker(save, (actionf_p1)T_PolyDoorSlide);
					break;

				case tc_polyswingdoor:
					th = LoadPolyswingdoorThinker(save, (actionf_p1)T_PolyDoorSwing);
					break;

				case tc_polyflag:
					th = LoadPolymoveThinker(save, (actionf_p1)T_PolyObjFlag);
					break;

				case tc_polydisplace:
					th = LoadPolydisplaceThinker(save, (actionf_p1)T_PolyObjDisplace);
					break;

				case tc_polyrotdisplace:
					th = LoadPolyrotdisplaceThinker(save, (actionf_p1)T_PolyObjRotDisplace);
					break;

				case tc_polyfade:
					th = LoadPolyfadeThinker(save, (actionf_p1)T_PolyObjFade);
					break;

				case tc_dynslopeline:
					th = LoadDynamicLineSlopeThinker(save, (actionf_p1)T_DynamicSlopeLine);
					break;

				case tc_dynslopevert:
					th = LoadDynamicVertexSlopeThinker(save, (actionf_p1)T_DynamicSlopeVert);
					break;

				case tc_scroll:
					th = LoadScrollThinker(save, (actionf_p1)T_Scroll);
					break;

				case tc_friction:
					th = LoadFrictionThinker(save, (actionf_p1)T_Friction);
					break;

				case tc_pusher:
					th = LoadPusherThinker(save, (actionf_p1)T_Pusher);
					break;

				default:
					I_Error("P_UnarchiveSpecials: Unknown tclass %d in savegame", tclass);
			}
			if (th)
				P_AddThinker(i, th);
		}

		CONS_Debug(DBG_NETPLAY, "%u thinkers loaded in list %d\n", numloaded, i);
	}
	
	// Set each skyboxmo to the first skybox (or NULL)
	skyboxmo[0] = skyboxviewpnts[0];
	skyboxmo[1] = skyboxcenterpnts[0];

	if (restoreNum)
	{
		executor_t *delay = NULL;
		UINT32 mobjnum;
		for (currentthinker = thlist[THINK_MAIN].next; currentthinker != &thlist[THINK_MAIN]; currentthinker = currentthinker->next)
		{
			if (currentthinker->function.acp1 != (actionf_p1)T_ExecutorDelay)
				continue;
			delay = (void *)currentthinker;
			if (!(mobjnum = (UINT32)(size_t)delay->caller))
				continue;
			delay->caller = P_FindNewPosition(mobjnum);
		}
	}
	TracyCZoneEnd(__zone);
}

///////////////////////////////////////////////////////////////////////////////
//
// haleyjd 03/26/06: PolyObject saving code
//
#define PD_FLAGS  0x01
#define PD_TRANS   0x02

static inline void P_ArchivePolyObj(savebuffer_t *save, polyobj_t *po)
{
	TracyCZone(__zone, true);

	UINT8 diff = 0;
	WRITEINT32(save->p, po->id);
	WRITEANGLE(save->p, po->angle);

	WRITEFIXED(save->p, po->spawnSpot.x);
	WRITEFIXED(save->p, po->spawnSpot.y);

	if (po->flags != po->spawnflags)
		diff |= PD_FLAGS;
	if (po->translucency != po->spawntrans)
		diff |= PD_TRANS;

	WRITEUINT8(save->p, diff);

	if (diff & PD_FLAGS)
		WRITEINT32(save->p, po->flags);
	if (diff & PD_TRANS)
		WRITEINT32(save->p, po->translucency);

	TracyCZoneEnd(__zone);
}

static inline void P_UnArchivePolyObj(savebuffer_t *save, polyobj_t *po)
{
	TracyCZone(__zone, true);

	INT32 id;
	UINT32 angle;
	fixed_t x, y;
	UINT8 diff;

	// nullify all polyobject thinker pointers;
	// the thinkers themselves will fight over who gets the field
	// when they first start to run.
	po->thinker = NULL;

	id = READINT32(save->p);

	angle = READANGLE(save->p);

	x = READFIXED(save->p);
	y = READFIXED(save->p);

	diff = READUINT8(save->p);

	if (diff & PD_FLAGS)
		po->flags = READINT32(save->p);
	if (diff & PD_TRANS)
		po->translucency = READINT32(save->p);

	// if the object is bad or isn't in the id hash, we can do nothing more
	// with it, so return now
	if (po->isBad || po != Polyobj_GetForNum(id))
		return;

	// rotate and translate polyobject
	Polyobj_MoveOnLoad(po, angle, x, y);

	TracyCZoneEnd(__zone);
}

static inline void P_ArchivePolyObjects(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	INT32 i;

	WRITEUINT32(save->p, ARCHIVEBLOCK_POBJS);

	// save number of polyobjects
	WRITEINT32(save->p, numPolyObjects);

	for (i = 0; i < numPolyObjects; ++i)
		P_ArchivePolyObj(save, &PolyObjects[i]);

	TracyCZoneEnd(__zone);
}

static inline void P_UnArchivePolyObjects(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	INT32 i, numSavedPolys;

	if (READUINT32(save->p) != ARCHIVEBLOCK_POBJS)
		I_Error("Bad $$$.sav at archive block Pobjs");

	numSavedPolys = READINT32(save->p);

	if (numSavedPolys != numPolyObjects)
		I_Error("P_UnArchivePolyObjects: polyobj count inconsistency\n");

	for (i = 0; i < numSavedPolys; ++i)
		P_UnArchivePolyObj(save, &PolyObjects[i]);

	TracyCZoneEnd(__zone);
}

static mobj_t *RelinkMobj(mobj_t **ptr)
{
	UINT32 temp = (UINT32)(size_t)*ptr;
	*ptr = NULL;
	return P_SetTarget(ptr, P_FindNewPosition(temp));
}

static void P_RelinkPointers(void)
{
	thinker_t *currentthinker;
	mobj_t *mobj;
	UINT32 temp, i;

	// use info field (value = oldposition) to relink mobjs
	for (currentthinker = thlist[THINK_MOBJ].next; currentthinker != &thlist[THINK_MOBJ];
		currentthinker = currentthinker->next)
	{
		if (currentthinker->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mobj = (mobj_t *)currentthinker;

		if (UNLIKELY(mobj->type == MT_HOOP || mobj->type == MT_HOOPCOLLIDE || mobj->type == MT_HOOPCENTER
			// MT_SPARK: used for debug stuff
			|| mobj->type == MT_SPARK))
			continue;

		if (mobj->tracer)
		{
			if (!RelinkMobj(&mobj->tracer))
				CONS_Debug(DBG_GAMELOGIC, "tracer not found on %d\n", mobj->type);
		}
		if (mobj->target)
		{
			if (!RelinkMobj(&mobj->target))
				CONS_Debug(DBG_GAMELOGIC, "target not found on %d\n", mobj->type);
		}
		if (mobj->hnext)
		{
			if (!RelinkMobj(&mobj->hnext))
				CONS_Debug(DBG_GAMELOGIC, "hnext not found on %d\n", mobj->type);
		}
		if (mobj->hprev)
		{
			if (!RelinkMobj(&mobj->hprev))
				CONS_Debug(DBG_GAMELOGIC, "hprev not found on %d\n", mobj->type);
		}
		if (mobj->itnext)
		{
			if (!RelinkMobj(&mobj->itnext))
				CONS_Debug(DBG_GAMELOGIC, "itnext not found on %d\n", mobj->type);
		}
		if (mobj->terrainOverlay)
		{
			if (!RelinkMobj(&mobj->terrainOverlay))
				CONS_Debug(DBG_GAMELOGIC, "terrainOverlay not found on %d\n", mobj->type);
		}
		if (mobj->shieldtracer)
		{
			if (!RelinkMobj(&mobj->shieldtracer))
				CONS_Debug(DBG_GAMELOGIC, "shieldtracer not found on %d\n", mobj->type);
		}
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;
			if ( players[i].awayviewmobj)
			{
				if (!RelinkMobj(&players[i].awayviewmobj))
					CONS_Debug(DBG_GAMELOGIC, "awayviewmobj not found on player %d\n", i);
			}
			if (players[i].followmobj)
			{
				if (!RelinkMobj(&players[i].followmobj))
					CONS_Debug(DBG_GAMELOGIC, "followmobj not found on player %d\n", i);
			}
			if (players[i].follower)
			{
				if (!RelinkMobj(&players[i].follower))
					CONS_Debug(DBG_GAMELOGIC, "follower not found on player %d\n", i);
			}
			if (players[i].currentwaypoint)
			{
				temp = (UINT32)(size_t)players[i].currentwaypoint;
				players[i].currentwaypoint = K_GetWaypointFromIndex(temp);
				if (players[i].currentwaypoint == NULL)
				{
					CONS_Debug(DBG_GAMELOGIC, "currentwaypoint not found on player %d\n", i);
				}
			}
			if (players[i].nextwaypoint)
			{
				temp = (UINT32)(size_t)players[i].nextwaypoint;
				players[i].nextwaypoint = K_GetWaypointFromIndex(temp);
				if (players[i].nextwaypoint == NULL)
				{
					CONS_Debug(DBG_GAMELOGIC, "nextwaypoint not found on player %d\n", i);
				}
			}
		}
	}
}

static inline void P_NetSyncSpecials(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	size_t i, z;
	char skytex[9];

	if (P_SyncUINT32(save, ARCHIVEBLOCK_SPECIALS) != ARCHIVEBLOCK_SPECIALS)
		I_Error("Bad $$$.sav at archive block Specials");

	if (save->write)
	{
		// itemrespawn queue for deathmatch
		i = iquetail;
		while (iquehead != i)
		{
			for (z = 0; z < nummapthings; z++)
			{
				if (&mapthings[z] == itemrespawnque[i])
				{
					WRITEUINT32(save->p, z);
					break;
				}
			}
			WRITEUINT32(save->p, itemrespawntime[i]);
			i = (i + 1) & (ITEMQUESIZE-1);
		}

		// end delimiter
		WRITEUINT32(save->p, 0xffffffff);
	}
	else
	{
		// BP: added save itemrespawn queue for deathmatch
		iquetail = iquehead = 0;
		while ((i = READUINT32(save->p)) != 0xffffffff)
		{
			itemrespawnque[iquehead] = &mapthings[i];
			itemrespawntime[iquehead++] = READINT32(save->p);
		}
	}

	// Sky number
	P_SyncStringN(save, globallevelskytexture, 9);

	if (!save->write && strcmp(skytex, globallevelskytexture))
		P_SetupLevelSky(skytex, true);

	// Current global weather type
	globalweather = P_SyncUINT8(save, globalweather);

	if (!save->write)
	{
		if (globalweather)
		{
			if (curWeather == globalweather)
				curWeather = PRECIP_NONE;

			P_SwitchWeather(globalweather);
		}
		else // PRECIP_NONE
		{
			if (curWeather != PRECIP_NONE)
				P_SwitchWeather(globalweather);
		}
	}

	if (save->write)
	{
		if (metalplayback) // Is metal sonic running?
		{
			WRITEUINT8(save->p, 0x01);
			G_SaveMetal(&save->p);
		}
		else
			WRITEUINT8(save->p, 0x00);
	}
	else
	{
		if (READUINT8(save->p) == 0x01) // metal sonic
			G_LoadMetal(&save->p);
	}
	TracyCZoneEnd(__zone);
}

// =======================================================================
//          Misc
// =======================================================================
static inline void P_ArchiveMisc(savebuffer_t *save, INT16 mapnum)
{
	TracyCZone(__zone, true);

	//lastmapsaved = mapnum;
	lastmaploaded = mapnum;

	if (gamecomplete)
		mapnum |= 8192;

	WRITEINT16(save->p, mapnum);
	WRITEUINT16(save->p, emeralds+357);
	WRITESTRINGN(save->p, timeattackfolder, sizeof(timeattackfolder));

	TracyCZoneEnd(__zone);
}

static inline void P_UnArchiveSPGame(savebuffer_t *save, INT16 mapoverride)
{
	TracyCZone(__zone, true);

	char testname[sizeof(timeattackfolder)];

	gamemap = READINT16(save->p);

	if (mapoverride != 0)
	{
		gamemap = mapoverride;
		gamecomplete = 1;
	}
	else
		gamecomplete = 0;

	// gamemap changed; we assume that its map header is always valid,
	// so make it so
	if (!gamemap || gamemap > nummapheaders || !mapheaderinfo[gamemap-1])
		I_Error("P_UnArchiveSPGame: Internal map ID %d not found (nummapheaders = %d)", gamemap-1, nummapheaders);

	//lastmapsaved = gamemap;
	lastmaploaded = gamemap;

	tokenlist = 0;
	token = 0;

	savedata.emeralds = READUINT16(save->p)-357;

	READSTRINGN(save->p, testname, sizeof(testname));

	if (strcmp(testname, timeattackfolder))
	{
		if (modifiedgame)
			I_Error("Save game not for this modification.");
		else
			I_Error("This save file is for a particular mod, it cannot be used with the regular game.");
	}

	memset(playeringame, 0, sizeof(*playeringame));
	playeringame[consoleplayer] = true;

	TracyCZoneEnd(__zone);
}

static boolean P_NetSyncMisc(savebuffer_t *save, boolean resending)
{
	TracyCZone(__zone, true);

	size_t i, j;
	size_t numTasks;
	const INT16 prevgamemap = gamemap;

	if (P_SyncUINT32(save, ARCHIVEBLOCK_MISC) != ARCHIVEBLOCK_MISC)
		I_Error("Bad $$$.sav at archive block Misc");

	if (resending)
		gametic = P_SyncUINT32(save, gametic);

	gamemap = P_SyncINT16(save, gamemap);

	if (save->write)
	{
		if (gamestate != GS_LEVEL)
			WRITEINT16(save->p, GS_WAITINGPLAYERS); // nice hack to put people back into waitingplayers
		else
			WRITEINT16(save->p, gamestate);
	}
	else
	{
		if (!gamemap || gamemap > nummapheaders || !mapheaderinfo[gamemap-1])
			I_Error("P_NetUnArchiveMisc: Internal map ID %d not found (nummapheaders = %d)", gamemap-1, nummapheaders);

		// tell the sound code to reset the music since we're skipping what
		// normally sets this flag
		if (!resending)
			mapmusflags |= MUSIC_RELOADRESET;
		G_SetGamestate(READINT16(save->p));
	}

	gametype = P_SyncINT16(save, gametype);

	if (save->write)
	{
		UINT32 pig = 0;
		for (i = 0; i < MAXPLAYERS; i++)
			pig |= (playeringame[i] != 0)<<i;
		WRITEUINT32(save->p, pig);
	}
	else
	{
		UINT32 pig = READUINT32(save->p);
		for (i = 0; i < MAXPLAYERS; i++)
		{
			playeringame[i] = (pig & (1<<i)) != 0;
			// playerstate is set in unarchiveplayers
		}
	}

	P_SetRandSeed(P_SyncUINT32(save, P_GetRandSeed()));

	tokenlist = P_SyncUINT32(save, tokenlist);

	encoremode = P_SyncUINT8(save, encoremode);
	mapmusrng = P_SyncUINT8(save, mapmusrng);

	if (!save->write)
	{
		// Only reload the level during a gamestate reload
		// if the map is horribly mismatched somehow. Minor
		// differences in level state are already handled
		// by other parts of the reload, so doing this
		// on *every* reload wastes lots of time that we
		// will need for rollback down the road.
		if (!resending || prevgamemap != gamemap)
		{
			if (!P_LoadLevel(true, resending))
			{
				CONS_Alert(CONS_ERROR, M_GetText("Can't load the level!\n"));
				return false;
			}
		}
		else
		{
			// Only reload stuff that can we modify in the save states themselves.
			// This is still orders of magnitude faster than a full level reload.
			// Considered memcpy, but it's complicated -- save that for local saves.

			sector_t *ss = sectors;
			sector_t *spawnss = spawnsectors;
			for (i = 0; i < numsectors; i++, ss++, spawnss++)
			{
				ss->floorheight = spawnss->floorheight;
				ss->ceilingheight = spawnss->ceilingheight;
				ss->floorpic = spawnss->floorpic;
				ss->ceilingpic = spawnss->ceilingpic;
				ss->lightlevel = spawnss->lightlevel;
				ss->special = spawnss->special;
				ss->floor_xoffs = spawnss->floor_xoffs;
				ss->floor_yoffs = spawnss->floor_yoffs;
				ss->ceiling_xoffs = spawnss->ceiling_xoffs;
				ss->ceiling_yoffs = spawnss->ceiling_yoffs;
				ss->floorpic_angle = spawnss->floorpic_angle;
				ss->ceilingpic_angle = spawnss->ceilingpic_angle;

				if (Tag_Compare(&ss->tags, &spawnss->tags) == false)
				{
					if (spawnss->tags.count)
					{
						ss->tags.count = spawnss->tags.count;
						ss->tags.tags =
							memcpy(
								Z_Realloc(
									ss->tags.tags,
									spawnss->tags.count * sizeof(mtag_t),
									PU_LEVEL,
									NULL
								),
								spawnss->tags.tags,
								spawnss->tags.count * sizeof(mtag_t)
							);
					}
					else
					{
						ss->tags.count = 0;
						Z_Free(ss->tags.tags);
					}
				}

				ss->extra_colormap = ss->spawn_extra_colormap;
				ss->crumblestate = CRUMBLE_NONE;
				ss->floorlightlevel = spawnss->floorlightlevel;
				ss->floorlightabsolute = spawnss->floorlightabsolute;
				ss->ceilinglightlevel = spawnss->ceilinglightlevel;
				ss->ceilinglightabsolute = spawnss->ceilinglightabsolute;
				ss->flags = spawnss->flags;
				ss->specialflags = spawnss->specialflags;
				ss->damagetype = spawnss->damagetype;
				ss->triggertag = spawnss->triggertag;
				ss->triggerer = spawnss->triggerer;
				ss->gravity = spawnss->gravity;
				ss->action = spawnss->action;

				memcpy(ss->args, spawnss->args, NUM_SCRIPT_ARGS * sizeof(*ss->args));

				for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
				{
					size_t len = 0;

					if (spawnss->stringargs[j])
					{
						len = strlen(spawnss->stringargs[j]);
					}

					if (!len)
					{
						Z_Free(ss->stringargs[j]);
						ss->stringargs[j] = NULL;
					}
					else
					{
						ss->stringargs[j] = Z_Realloc(ss->stringargs[j], len + 1, PU_LEVEL, NULL);
						M_Memcpy(ss->stringargs[j], spawnss->stringargs[j], len);
						ss->stringargs[j][len] = '\0';
					}
				}

				ss->activation = spawnss->activation;
				ss->botController.flags = spawnss->botController.flags;
				ss->botController.forceAngle = spawnss->botController.forceAngle;

				if (ss->ffloors)
				{
					ffloor_t *rover;
					for (rover = ss->ffloors; rover; rover = rover->next)
					{
						rover->fofflags = rover->spawnflags;
						rover->alpha = rover->spawnalpha;
					}
				}
			}

			line_t *li = lines;
			line_t *spawnli = spawnlines;
			side_t *si = NULL;
			side_t *spawnsi = NULL;
			for (i = 0; i < numlines; i++, spawnli++, li++)
			{
				li->flags = spawnli->flags;
				li->special = spawnli->special;
				li->callcount = 0;

				if (Tag_Compare(&li->tags, &spawnli->tags) == false)
				{
					if (spawnli->tags.count)
					{
						li->tags.count = spawnli->tags.count;
						li->tags.tags =
							memcpy(
								Z_Realloc(
									li->tags.tags,
									spawnli->tags.count * sizeof(mtag_t),
									PU_LEVEL,
									NULL
								),
								spawnli->tags.tags,
								spawnli->tags.count * sizeof(mtag_t)
							);

					}
					else
					{
						li->tags.count = 0;
						Z_Free(li->tags.tags);
					}
				}

				memcpy(li->args, spawnli->args, NUM_SCRIPT_ARGS * sizeof(*li->args));

				for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
				{
					size_t len = 0;

					if (spawnli->stringargs[j])
					{
						len = strlen(spawnli->stringargs[j]);
					}

					if (!len)
					{
						Z_Free(li->stringargs[j]);
						li->stringargs[j] = NULL;
					}
					else
					{
						li->stringargs[j] = Z_Realloc(li->stringargs[j], len + 1, PU_LEVEL, NULL);
						M_Memcpy(li->stringargs[j], spawnli->stringargs[j], len);
						li->stringargs[j][len] = '\0';
					}
				}

				li->executordelay = spawnli->executordelay;
				li->activation = spawnli->activation;

				if (li->sidenum[0] != 0xffff)
				{
					si = &sides[li->sidenum[0]];
					spawnsi = &spawnsides[li->sidenum[0]];

					si->textureoffset = spawnsi->textureoffset;
					si->toptexture = spawnsi->toptexture;
					si->bottomtexture = spawnsi->bottomtexture;
					si->midtexture = spawnsi->midtexture;
				}

				if (li->sidenum[1] != 0xffff)
				{
					si = &sides[li->sidenum[1]];
					spawnsi = &spawnsides[li->sidenum[1]];

					si->textureoffset = spawnsi->textureoffset;
					si->toptexture = spawnsi->toptexture;
					si->bottomtexture = spawnsi->bottomtexture;
					si->midtexture = spawnsi->midtexture;
				}
			}

			Taglist_InitGlobalTables();
		}
	}

	leveltime = P_SyncUINT32(save, leveltime);
	lastmap = P_SyncINT16(save, lastmap);
	bossdisabled = P_SyncUINT16(save, bossdisabled);
	ringsactive = P_SyncUINT8(save, ringsactive);
	stackingactive = P_SyncUINT8(save, stackingactive);
	chainingactive = P_SyncUINT8(save, chainingactive);
	slipdashactive = P_SyncUINT8(save, slipdashactive);
	purpledriftactive = P_SyncUINT8(save, purpledriftactive);
	slopeboostactive = P_SyncUINT8(save, slopeboostactive);
	draftingactive = P_SyncUINT8(save, draftingactive);
	airdropactive = P_SyncUINT8(save, airdropactive);
	bumpsparkactive = P_SyncUINT8(save, bumpsparkactive);

	for (i = 0; i < 12; i++)
	{
		votelevels[i][0] = P_SyncINT16(save, votelevels[i][0]);
		votelevels[i][1] = P_SyncINT16(save, votelevels[i][1]);
	}

	for (i = 0; i < MAXPLAYERS; i++)
		votes[i] = P_SyncSINT8(save, votes[i]);

	pickedvote = P_SyncSINT8(save, pickedvote);

	emeralds = P_SyncUINT16(save, emeralds);
	if (save->write)
	{
		UINT8 globools = 0;
		if (stagefailed)
			globools |= 1;
		if (stoppedclock)
			globools |= (1<<1);
		WRITEUINT8(save->p, globools);
	}
	else
	{
		UINT8 globools = READUINT8(save->p);
		stagefailed = !!(globools & 1);
		stoppedclock = !!(globools & (1<<1));
	}

	token = P_SyncUINT32(save, token);
	bluescore = P_SyncUINT32(save, bluescore);
	redscore = P_SyncUINT32(save, redscore);

	skincolor_redteam = P_SyncUINT16(save, skincolor_redteam);
	skincolor_blueteam = P_SyncUINT16(save, skincolor_blueteam);
	skincolor_redring = P_SyncUINT16(save, skincolor_redring);
	skincolor_bluering = P_SyncUINT16(save, skincolor_bluering);

	modulothing = P_SyncINT32(save, modulothing);

	autobalance = P_SyncINT16(save, autobalance);
	teamscramble = P_SyncINT16(save, teamscramble);

	for (i = 0; i < MAXPLAYERS; i++)
		scrambleplayers[i] = P_SyncINT16(save, scrambleplayers[i]);

	for (i = 0; i < MAXPLAYERS; i++)
		scrambleteams[i] = P_SyncINT16(save, scrambleteams[i]);

	scrambletotal = P_SyncINT16(save, scrambletotal);
	scramblecount = P_SyncINT16(save, scramblecount);

	racecountdown = P_SyncUINT32(save, racecountdown);
	exitcountdown = P_SyncUINT32(save, exitcountdown);

	// exitcondition_t
	g_exit.losing = P_SyncUINT8(save, g_exit.losing);
	g_exit.retry = P_SyncUINT8(save, g_exit.retry);
	g_exit.hasfinished = P_SyncUINT8(save, g_exit.hasfinished);

	gravity = P_SyncFixed(save, gravity);
	mapobjectscale = P_SyncFixed(save, mapobjectscale);

	// SRB2kart
	nummapboxes = P_SyncINT32(save, nummapboxes);
	numgotboxes = P_SyncINT32(save, numgotboxes);
	numtargets = P_SyncUINT8(save, numtargets);
	itembreaker = P_SyncUINT8(save, itembreaker);

	gamespeed = P_SyncUINT8(save, gamespeed);
	numlaps = P_SyncUINT8(save, numlaps);
	franticitems = P_SyncUINT8(save, franticitems);
	comeback = P_SyncUINT8(save, comeback);

	speedscramble = P_SyncSINT8(save, speedscramble);
	encorescramble = P_SyncSINT8(save, encorescramble);

	// WANTED system
	mostwanted = P_SyncSINT8(save, mostwanted);
	for (i = 0; i < 4; i++)
		battlewanted[i] = P_SyncSINT8(save, battlewanted[i]);

	wantedcalcdelay = P_SyncUINT32(save, wantedcalcdelay);
	indirectitemcooldown = P_SyncUINT32(save, indirectitemcooldown);
	hyubgone = P_SyncUINT32(save, hyubgone);
	mapreset = P_SyncUINT32(save, mapreset);

	spectateGriefed = P_SyncUINT8(save, spectateGriefed);

	thwompsactive = P_SyncUINT8(save, thwompsactive);
	lastLowestLap = P_SyncUINT8(save, lastLowestLap);
	spbplace = P_SyncSINT8(save, spbplace);
	startedInFreePlay = P_SyncUINT8(save, startedInFreePlay);

	introtime = P_SyncUINT32(save, introtime);
	starttime = P_SyncUINT32(save, starttime);

	hyudorotime = P_SyncINT32(save, hyudorotime);
	stealtime = P_SyncINT32(save, stealtime);
	sneakertime = P_SyncINT32(save, sneakertime);
	waterpaneltime = P_SyncINT32(save, waterpaneltime);
	itemtime = P_SyncINT32(save, itemtime);
	bubbletime = P_SyncINT32(save, bubbletime);
	comebacktime = P_SyncINT32(save, comebacktime);
	bumptime = P_SyncINT32(save, bumptime);
	greasetics = P_SyncINT32(save, greasetics);
	wipeoutslowtime = P_SyncINT32(save, wipeoutslowtime);
	wantedreduce = P_SyncINT32(save, wantedreduce);
	wantedfrequency = P_SyncINT32(save, wantedfrequency);

	timelimitintics = P_SyncUINT32(save, timelimitintics);
	extratimeintics = P_SyncUINT32(save, extratimeintics);
	secretextratime = P_SyncUINT32(save, secretextratime);

	paused = P_SyncUINT8(save, paused);

	if (save->write)
	{
		// Only the server uses this, but it
		// needs synched for remote admins anyway.
		WRITEUINT32(save->p, schedule_len);
		for (i = 0; i < schedule_len; i++)
		{
			scheduleTask_t *task = schedule[i];
			WRITEINT16(save->p, task->basetime);
			WRITEINT16(save->p, task->timer);
			WRITESTRING(save->p, task->command);
		}
	}
	else
	{
		// Only the server uses this, but it
		// needs synched for remote admins anyway.
		Schedule_Clear();

		numTasks = READUINT32(save->p);
		for (i = 0; i < numTasks; i++)
		{
			INT16 basetime;
			INT16 timer;
			char command[MAXTEXTCMD];

			basetime = READINT16(save->p);
			timer = READINT16(save->p);
			READSTRING(save->p, command);

			Schedule_Add(basetime, timer, command);
		}
	}

	cht_debug = P_SyncUINT32(save, cht_debug);

	return true;
	TracyCZoneEnd(__zone);
}

static inline void P_ArchiveLuabanksAndConsistency(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	UINT8 i, banksinuse = NUM_LUABANKS;

	while (banksinuse && !luabanks[banksinuse-1])
		banksinuse--; // get the last used bank

	if (banksinuse)
	{
		WRITEUINT8(save->p, 0xb7); // luabanks marker
		WRITEUINT8(save->p, banksinuse);
		for (i = 0; i < banksinuse; i++)
			WRITEINT32(save->p, luabanks[i]);
	}

	WRITEUINT8(save->p, 0x1d); // consistency marker

	TracyCZoneEnd(__zone);
}

static inline boolean P_UnArchiveLuabanksAndConsistency(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	switch (READUINT8(save->p))
	{
		case 0xb7: // luabanks marker
			{
				UINT8 i, banksinuse = READUINT8(save->p);
				if (banksinuse > NUM_LUABANKS)
				{
					CONS_Alert(CONS_ERROR, M_GetText("Corrupt Luabanks! (Too many banks in use)\n"));
					return false;
				}
				for (i = 0; i < banksinuse; i++)
					luabanks[i] = READINT32(save->p);
				if (READUINT8(save->p) != 0x1d) // consistency marker
				{
					CONS_Alert(CONS_ERROR, M_GetText("Corrupt Luabanks! (Failed consistency check)\n"));
					return false;
				}
			}
		case 0x1d: // consistency marker
			break;
		default: // anything else is nonsense
			CONS_Alert(CONS_ERROR, M_GetText("Failed consistency check (???)\n"));
			return false;
	}

	TracyCZoneEnd(__zone);
	return true;
}

void P_SaveGame(savebuffer_t *save, INT16 mapnum)
{
	P_ArchiveMisc(save, mapnum);
	P_ArchivePlayer(save);
	P_ArchiveLuabanksAndConsistency(save);
}

void P_SaveNetGame(savebuffer_t *save, boolean resending)
{
	TracyCZone(__zone, true);

	thinker_t *th;
	mobj_t *mobj;
	UINT32 i = 1; // don't start from 0, it'd be confused with a blank pointer otherwise

	save->write = true;

	CV_SaveNetVars(&save->p);
	P_NetSyncMisc(save, resending);

	// Assign the mobjnumber for pointer tracking
	if (gamestate == GS_LEVEL)
	{
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mobj = (mobj_t *)th;
			if (UNLIKELY(mobj->type == MT_HOOP || mobj->type == MT_HOOPCOLLIDE || mobj->type == MT_HOOPCENTER
				// MT_SPARK: used for debug stuff
				|| mobj->type == MT_SPARK || mobj->flags2 & MF2_DONTSYNC))
				continue;
			mobj->mobjnum = i++;
		}
	}

	P_NetSyncPlayers(save);
	P_NetSyncParties(save);

	if (gamestate == GS_LEVEL)
	{
		P_NetArchiveWorld(save);
		P_ArchivePolyObjects(save);
		P_NetArchiveThinkers(save);
		P_NetSyncSpecials(save);
		P_NetArchiveColormaps(save);
		P_NetSyncTubeWaypoints(save);
		P_NetArchiveWaypoints(save);
	}

	ACS_Archive(save);
	LUA_Archive(save, true);

	P_ArchiveLuabanksAndConsistency(save);

	TracyCZoneEnd(__zone);
}

boolean P_LoadGame(savebuffer_t *save, INT16 mapoverride)
{
	if (gamestate == GS_INTERMISSION)
		Y_EndIntermission();
	if (gamestate == GS_VOTING)
		Y_EndVote();
	G_SetGamestate(GS_NULL); // should be changed in P_UnArchiveMisc

	P_UnArchiveSPGame(save, mapoverride);
	P_UnArchivePlayer(save);

	if (!P_UnArchiveLuabanksAndConsistency(save))
		return false;

	// Only do this after confirming savegame is ok
	G_DeferedInitNew(false, gamemap, savedata.skin, 0, true);
	COM_BufAddText("dummyconsvar 1\n"); // G_DeferedInitNew doesn't do this

	return true;
}

boolean P_LoadNetGame(savebuffer_t *save, boolean reloading)
{
	TracyCZone(__zone, true);

	save->write = false;

	CV_LoadNetVars(&save->p);
	if (!P_NetSyncMisc(save,reloading))
		return false;
	P_NetSyncPlayers(save);
	P_NetSyncParties(save);

	if (gamestate == GS_LEVEL)
	{
		P_NetUnArchiveWorld(save);
		P_UnArchivePolyObjects(save);
		P_NetUnArchiveThinkers(save);
		P_NetSyncSpecials(save);
		P_NetUnArchiveColormaps(save);
		P_NetSyncTubeWaypoints(save);
		P_NetUnArchiveWaypoints(save);
		P_RelinkPointers();
	}

	ACS_UnArchive(save);
	LUA_UnArchive(save, true, false);

	// This is stupid and hacky, but maybe it'll work!
	P_SetRandSeed(P_GetInitSeed());

	// The precipitation would normally be spawned in P_SetupLevel, which is called by
	// P_NetUnArchiveMisc above. However, that would place it up before P_NetUnArchiveThinkers,
	// so the thinkers would be deleted later. Therefore, P_SetupLevel will *not* spawn
	// precipitation when loading a netgame save. Instead, precip has to be spawned here.
	// This is done in P_NetUnArchiveSpecials now.

	TracyCZoneEnd(__zone);
	return P_UnArchiveLuabanksAndConsistency(save);
}

boolean P_SaveBufferZAlloc(savebuffer_t *save, size_t alloc_size, INT32 tag, void *user)
{
	I_Assert(save->buffer == NULL);
	save->buffer = (UINT8 *)Z_Malloc(alloc_size, tag, user);

	if (save->buffer == NULL)
	{
		return false;
	}

	save->size = alloc_size;
	save->p = save->buffer;
	save->end = save->buffer + save->size;

	return true;
}

boolean P_SaveBufferFromExisting(savebuffer_t *save, UINT8 *existing_buffer, size_t existing_size)
{
	I_Assert(save->buffer == NULL);

	if (existing_buffer == NULL || existing_size == 0)
	{
		return false;
	}

	save->buffer = existing_buffer;
	save->size = existing_size;

	save->p = save->buffer;
	save->end = save->buffer + save->size;

	return true;
}

boolean P_SaveBufferFromLump(savebuffer_t *save, lumpnum_t lump)
{
	I_Assert(save->buffer == NULL);

	if (lump == LUMPERROR)
	{
		return false;
	}

	save->buffer = (UINT8 *)W_CacheLumpNum(lump, PU_STATIC);

	if (save->buffer == NULL)
	{
		return false;
	}

	save->size = W_LumpLength(lump);

	save->p = save->buffer;
	save->end = save->buffer + save->size;

	return true;
}

size_t P_SaveBufferFromFile(savebuffer_t *save, char const *name)
{
	size_t len = 0;

	I_Assert(save->buffer == NULL);
	len = FIL_ReadFile(name, &save->buffer);

	if (len != 0)
	{
		save->size = len;

		save->p = save->buffer;
		save->end = save->buffer + save->size;
	}

	return len;
}

static void P_SaveBufferInvalidate(savebuffer_t *save)
{
	save->buffer = save->p = save->end = NULL;
	save->size = 0;
}

void P_SaveBufferFree(savebuffer_t *save)
{
	I_Assert(save->buffer != NULL);
	Z_Free(save->buffer);
	P_SaveBufferInvalidate(save);
}
