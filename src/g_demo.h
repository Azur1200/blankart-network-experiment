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
/// \file  g_demo.h
/// \brief Demo recording and playback

#ifndef __G_DEMO__
#define __G_DEMO__

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "m_textinput.h"
#include "filesrch.h"

#ifdef __cplusplus
extern "C" {
#endif

// ======================================
// DEMO playback/recording related stuff.
// ======================================

typedef enum {
	DEMOSAVE_NONE = 0,
	DEMOSAVE_MANUAL,
	DEMOSAVE_AUTO_NETONLY,
	DEMOSAVE_AUTO
} demosavemode_e;

extern consvar_t cv_recordmultiplayerdemos, cv_netdemosyncquality, cv_netdemosize;
extern consvar_t cv_resyncdemo;
extern consvar_t cv_dummyreplaysave;
extern menuitem_t replaysave;

extern tic_t demostarttime;

// Publicly-accessible demo vars
struct demovars_s {
	char titlename[65];
	textinput_t titlenameinput;
	dboolean recording, playback, timing;
	uint16_t version; // Current file format of the demo being played
	dboolean title; // Title Screen demo can be cancelled by any key
	dboolean rewinding; // Rewind in progress

	dboolean loadfiles, ignorefiles; // Demo file loading options
	dboolean fromtitle; // SRB2Kart: Don't stop the music
	dboolean inreplayhut; // Go back to replayhut after demos
	dboolean quitafterplaying; // quit after playing a demo from cmdline
	dboolean deferstart; // don't start playing demo right away
	dboolean netgame; // multiplayer netgame
	dboolean single_seed; // During playback, only has one RNG seed

	tic_t savebutton; // Used to determine when the local player can choose to save the replay while the race is still going
	enum {
		DSM_NOTSAVING,
		DSM_WILLAUTOSAVE,
		DSM_TITLEENTRY,
		DSM_WILLSAVE,
		DSM_SAVED
	} savemode;

	dboolean freecam;

	const savebuffer_t *buffer; // debug, valid only if recording or playback
};

extern struct demovars_s demo;

typedef enum {
	MD_NOTLOADED,
	MD_LOADED,
	MD_SUBDIR,
	MD_OUTDATED,
	MD_INVALID
} menudemotype_e;

struct menudemo_t {
	char filepath[MAXFILEPATH];
	menudemotype_e type;

	char title[65]; // Null-terminated for string prints
	char date[11]; // date when replay was recorded

	uint16_t map;
	uint8_t addonstatus; // What do we need to do addon-wise to play this demo?
	uint8_t gametype;
	int8_t kartspeed; // Add OR DF_ENCORE for encore mode, idk
	uint8_t numlaps;

	struct {
		uint8_t ranking;
		char name[21+1];
		uint16_t skin, color;
		uint32_t timeorscore;
	} standings[MAXPLAYERS];
};

dboolean G_CompatLevel(uint16_t level);

extern mobj_t *metalplayback;

// Only called by startup code.
void G_RecordDemo(const char *name);
void G_RecordMetal(void);
void G_BeginRecording(void);
void G_BeginMetal(void);

// Only called by shutdown code.
void G_WriteStanding(uint8_t ranking, char *name, int32_t skinnum, uint16_t color, uint32_t val);
void G_SetDemoTime(uint32_t ptime, uint32_t plap);
uint8_t G_CmpDemoTime(char *oldname, char *newname);

typedef enum
{
	GHC_NORMAL = 0,
	GHC_SUPER,
	GHC_FIREFLOWER,
	GHC_INVINCIBLE,
	GHC_RETURNSKIN // not actually a colour
} ghostcolor_t;

extern uint8_t demo_extradata[MAXPLAYERS];
extern uint8_t demo_writerng;

#define DXD_JOINDATA   0x01 // join-specific data
#define DXD_PLAYSTATE	0x02 // state changed between playing, spectating, or not in-game
#define DXD_NAME		0x04 // name changed
#define DXD_SKIN		0x08 // skin changed
#define DXD_COLOR		0x10 // color changed
#define DXD_FOLLOWER	0x20 // follower was changed
#define DXD_RESPAWN		0x40  // "respawn" command in console

#define DXD_ADDPLAYER (DXD_JOINDATA|DXD_PLAYSTATE|DXD_COLOR|DXD_NAME|DXD_SKIN|DXD_FOLLOWER)

#define DXD_WEAPONPREF	0x80 // netsynced playsim settings were changed

#define DXD_PST_PLAYING    0x01
#define DXD_PST_SPECTATING 0x02
#define DXD_PST_LEFT       0x03

void G_LoadDemoCvars(void **p, void (*setvalue)(consvar_t *, const char *, dboolean));

// Record/playback tics
void G_ReadDemoExtraData(void);
void G_WriteDemoExtraData(void);
void G_ReadDemoTiccmd(ticcmd_t *cmd, int32_t playernum);
void G_WriteDemoTiccmd(ticcmd_t *cmd, int32_t playernum);
void G_GhostAddColor(int32_t playernum, ghostcolor_t color);
void G_GhostAddFlip(int32_t playernum);
void G_GhostAddScale(int32_t playernum, fixed_t scale);
void G_GhostAddHit(int32_t playernum, mobj_t *victim);
void G_WriteAllGhostTics(void);
void G_WriteGhostTic(mobj_t *ghost, int32_t playernum);
void G_ConsAllGhostTics(void);
void G_ConsGhostTic(int32_t playernum);
void G_GhostTicker(void);

void G_InitDemoRewind(void);
void G_StoreRewindInfo(void);
void G_PreviewRewind(tic_t previewtime);
void G_ConfirmRewind(tic_t rewindtime);

void G_ReadMetalTic(mobj_t *metal);
void G_WriteMetalTic(mobj_t *metal);
void G_SaveMetal(uint8_t **buffer);
void G_LoadMetal(uint8_t **buffer);

// Your naming conventions are stupid and useless.
// There is no conflict here.
struct demoghost {
	uint64_t checksum;
	uint8_t *buffer, *p, color;
	uint8_t fadein;
	uint16_t version;
	dboolean linecrossed;
	dboolean done;
	mobj_t oldmo, *mo;
	struct demoghost *next;
};
extern demoghost *ghosts;

// G_CheckDemoExtraFiles: checks if our loaded WAD list matches the demo's.
#define DFILE_ERROR_NOTLOADED            0x01 // Files are not loaded, but can be without a restart.
#define DFILE_ERROR_OUTOFORDER           0x02 // Files are loaded, but out of order.
#define DFILE_ERROR_INCOMPLETEOUTOFORDER 0x03 // Some files are loaded out of order, but others are not.
#define DFILE_ERROR_CANNOTLOAD           0x04 // Files are missing and cannot be loaded.
#define DFILE_ERROR_EXTRAFILES           0x05 // Extra files outside of the replay's file list are loaded.

dboolean G_DemoWillAutoSave(void);
void G_DeferedPlayDemo(const char *demo);
void G_DoPlayDemo(char *defdemoname);
void G_SetupDemoPlayer(int32_t i);
void G_TimeDemo(const char *name);
void G_AddGhost(char *defdemoname);
void G_UpdateStaffGhostName(lumpnum_t l);
void G_FreeGhosts(void);
void G_DoneLevelLoad(void);

void G_DoPlayMetal(void);
void G_StopMetalDemo(void);
ATTRNORETURN void FUNCNORETURN G_StopMetalRecording(dboolean kill);

void G_StopDemo(void);
dboolean G_CheckDemoStatus(void);

void G_LoadDemoInfo(menudemo_t *pdemo, dboolean nameonly);
void G_DeferedPlayDemo(const char *demo);

void G_SaveDemo(void);

void G_SetAutoSaveName(void);

void G_SyncDemoParty(int32_t rem, int32_t newsplitscreen);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __G_DEMO__
