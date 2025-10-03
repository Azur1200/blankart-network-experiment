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
/// \file  g_game.h
/// \brief Game loop, events handling.

#ifndef __G_GAME__
#define __G_GAME__

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "g_demo.h"
#include "m_cheat.h" // objectplacing

#ifdef __cplusplus
extern "C" {
#endif

extern char gamedatafilename[64];
extern char timeattackfolder[64];
extern char customversionstring[32];

extern char  player_names[MAXPLAYERS][MAXPLAYERNAME+1];
extern INT32 player_name_changes[MAXPLAYERS];

extern player_t players[MAXPLAYERS];
extern boolean playeringame[MAXPLAYERS];

// gametic at level start
extern tic_t levelstarttic;

// for modding?
extern INT16 prevmap, nextmap;

// see also G_MapNumber
typedef enum
{
	NEXTMAP_RESERVED = INT16_MAX, // so nextmap+1 doesn't roll over -- remove when gamemap is made 0-indexed
	NEXTMAP_TITLE = INT16_MAX-1,
	NEXTMAP_EVALUATION = INT16_MAX-2,
	NEXTMAP_CREDITS = INT16_MAX-3,
	NEXTMAP_CEREMONY = INT16_MAX-4,
	NEXTMAP_INVALID = INT16_MAX-5, // Always last (swap with NEXTMAP_RESERVED when removing that)
	NEXTMAP_SPECIAL = NEXTMAP_INVALID
} nextmapspecial_t;

extern INT16 kartmap2native[NEXTMAP_SPECIAL], nativemap2kart[NEXTMAP_SPECIAL];
extern INT16 nextexnum;

extern INT32 gameovertics;
extern UINT8 ammoremovaltics;
extern tic_t timeinmap; // Ticker for time spent in level (used for levelcard display)
extern INT32 pausedelay;
extern boolean pausebreakkey;

extern boolean promptactive;


extern consvar_t cv_tutorialprompt;

extern consvar_t cv_chatwidth, cv_chatnotifications, cv_chatheight, cv_chattime, cv_consolechat, cv_chatbacktint, cv_chatspamprotection;
extern consvar_t cv_shoutname, cv_shoutcolor, cv_autoshout;
extern consvar_t cv_songcredits;
extern consvar_t cv_showfreeplay;
extern consvar_t cv_growmusic, cv_supermusic;

extern consvar_t cv_pauseifunfocused;

extern consvar_t cv_invertmouse;

extern consvar_t cv_kickstartaccel[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_shrinkme[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_jitterlegacy[MAXSPLITSCREENPLAYERS];

extern consvar_t cv_deadzone[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_deadzonestyle[MAXSPLITSCREENPLAYERS];

extern consvar_t cv_ghost_besttime, cv_ghost_bestlap, cv_ghost_last, cv_ghost_guest, cv_ghost_staff;

extern consvar_t cv_invincmusicfade;
extern consvar_t cv_growmusicfade;

extern consvar_t cv_resetspecialmusic;

extern consvar_t cv_resume;

extern consvar_t cv_litesteer[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_turnsmooth[MAXSPLITSCREENPLAYERS];

void weaponPrefChange(void);
void weaponPrefChange2(void);
void weaponPrefChange3(void);
void weaponPrefChange4(void);

#define MAXPLMOVE (50)

const char *G_BuildMapName(INT32 map);
INT32 G_MapNumber(const char *mapname);
INT32 G_LevelTitleToMapNum(const char * leveltitle);
INT16 G_KartMapToNative(INT16 mapnum);
INT16 G_NativeMapToKart(INT16 mapnum);

void G_ResetAnglePrediction(player_t *player);
void G_BuildTiccmd(ticcmd_t *cmd, INT32 realtics, UINT8 ssplayer);

// copy ticcmd_t to and fro the normal way
ticcmd_t *G_CopyTiccmd(ticcmd_t* dest, const ticcmd_t* src, const size_t n);
// copy ticcmd_t to and fro network packets
ticcmd_t *G_MoveTiccmd(ticcmd_t* dest, const ticcmd_t* src, const size_t n);

// clip the console player aiming to the view
INT32 G_ClipAimingPitch(INT32 *aiming);
INT16 G_SoftwareClipAimingPitch(INT32 *aiming);
void G_FinalClipAimingPitch(INT32 *aiming, player_t *player, boolean skybox);

extern angle_t localangle[MAXSPLITSCREENPLAYERS];
extern INT32 localaiming[MAXSPLITSCREENPLAYERS]; // should be an angle_t but signed

INT32 G_PlayerInputAnalog(UINT8 p, INT32 gc, boolean digital);
boolean G_PlayerInputDown(UINT8 p, INT32 gc, boolean digital);
boolean G_ControlBoundToKey(UINT8 p, INT32 gc, INT32 key, boolean defaults);
boolean G_AxisInDeadzone(UINT8 p, event_t *ev);

//
// GAME
//
void G_ChangePlayerReferences(mobj_t *oldmo, mobj_t *newmo);
void G_DoReborn(INT32 playernum);
void G_PlayerReborn(INT32 player, boolean betweenmaps);
void G_InitNew(UINT8 pencoremode, INT32 map, boolean resetplayer,
	boolean skipprecutscene, boolean FLS);
char *G_BuildMapTitle(INT32 mapnum);

struct searchdim
{
	UINT8 pos;
	UINT8 siz;
};

struct mapsearchfreq_t
{
	INT16  mapnum;
	UINT8  matchc;
	struct searchdim *matchd;/* offset that a pattern was matched */
	UINT8  keywhc;
	struct searchdim *keywhd;/* ...in KEYWORD */
	UINT8  total;/* total hits */
};

INT32 G_FindMap(const char *query, char **foundmapnamep,
		mapsearchfreq_t **freqp, INT32 *freqc);
void G_FreeMapSearch(mapsearchfreq_t *freq, INT32 freqc);

/* Match map name by search + 2 digit map code or map number. */
INT32 G_FindMapByNameOrCode(const char *query, char **foundmapnamep);

// XMOD spawning
mapthing_t *G_FindTeamStart(INT32 playernum);
mapthing_t *G_FindBattleStart(INT32 playernum);
mapthing_t *G_FindRaceStart(INT32 playernum);
mapthing_t *G_FindMapStart(INT32 playernum);
void G_MovePlayerToSpawnOrStarpost(INT32 playernum, boolean starpost);
void G_SpawnPlayer(INT32 playernum, boolean starpost);

// Can be called by the startup code or M_Responder.
// A normal game starts at map 1, but a warp test can start elsewhere
void G_DeferedInitNew(boolean pencoremode, INT32 map, INT32 pickedchar,
	UINT8 ssplayers, boolean FLS);
void G_DoLoadLevel(boolean resetplayer);

void G_StartTitleCard(void);
void G_PreLevelTitleCard(void);
boolean G_IsTitleCardAvailable(void);

// Can be called by the startup code or M_Responder, calls P_SetupLevel.
void G_LoadGame(UINT32 slot, INT16 mapoverride);

void G_SaveGameData(void);

void G_SaveGame(UINT32 slot, INT16 mapnum);

void G_SaveGameOver(UINT32 slot, boolean modifylives);

extern UINT32 gametypedefaultrules[NUMGAMETYPES];
extern UINT32 gametypetol[NUMGAMETYPES];
extern INT32 gametypecolor[NUMGAMETYPES];
extern INT16 gametyperankings[NUMGAMETYPES];

void G_SetGametype(INT16 gametype);
INT16 G_AddGametype(UINT32 rules);
void G_AddGametypeConstant(INT16 gtype, const char *newgtconst);
void G_UpdateGametypeSelections(void);
void G_SetGametypeColor(INT16 gtype,INT32 color);
void G_AddTOL(UINT32 newtol, const char *tolname);
void G_AddGametypeTOL(INT16 gtype, UINT32 newtol);
INT32 G_GetGametypeByName(const char *gametypestr);
boolean G_IsSpecialStage(INT32 mapnum);
boolean G_GametypeUsesLives(void);
boolean G_GametypeHasTeams(void);
boolean G_GametypeHasSpectators(void);
#define VOTEMODIFIER_ENCORE 0x80
INT16 G_SometimesGetDifferentGametype(UINT8 prefgametype);
UINT8 G_GetGametypeColor(INT16 gt);
void G_BeginLevelExit(void);
void G_FinishExitLevel(void);
void G_NextLevel(void);
void G_Continue(void);
void G_UseContinue(void);
void G_AfterIntermission(void);
void G_EndGame(void); // moved from y_inter.c/h and renamed

void G_Ticker(boolean run);
boolean G_Responder(event_t *ev);

boolean G_CouldView(INT32 playernum);
boolean G_CanView(INT32 playernum, UINT8 viewnum, boolean onlyactive);

INT32 G_FindView(INT32 startview, UINT8 viewnum, boolean onlyactive, boolean reverse);
INT32 G_CountPlayersPotentiallyViewable(boolean active);

void G_FixCamera(UINT8 view);
void G_ResetViews(void);
void G_ResetView(UINT8 viewnum, INT32 playernum, boolean onlyactive);
void G_AdjustView(UINT8 viewnum, INT32 offset, boolean onlyactive);

void G_AddPlayer(INT32 playernum, INT32 console);

void G_SetExitGameFlag(void);
void G_ClearExitGameFlag(void);
boolean G_GetExitGameFlag(void);

void G_SetRetryFlag(void);
void G_ClearRetryFlag(void);
boolean G_GetRetryFlag(void);

void G_SetModeAttackRetryFlag(void);
void G_ClearModeAttackRetryFlag(void);
boolean G_GetModeAttackRetryFlag(void);

void G_LoadGameData(void);
void G_LoadGameSettings(void);

void G_SetGameModified(boolean silent, boolean major);

void G_SetGamestate(gamestate_t newstate);

boolean G_GamestateUsesLevel(void);

// Gamedata record shit

#define MAXPRESETNAME 16

typedef struct
{
	char name[MAXPRESETNAME+1];
	char *realname;
	UINT8 numversions;
	strbuf_t **versions;
	// yep, array of strbufs. byte 0 is version ID, then cvar NUL value NUL cvar ...
	// i'm honestly tired of multi-layered structs, let's simplify things...
	// we're also gonna need to compare these lists of cvars pretty often!
} recordpreset_t;

typedef enum
{
	RP_KART,
	RP_TECH,
	RP_BLAN,
	RP_FIRSTFREESLOT,
	RP_LASTFREESLOT = RP_FIRSTFREESLOT + 100,
	MAXRECORDPRESETS,
} ATTRPACK recordpresetnum_e;

extern recordpreset_t **recordpresets;
extern recordpresetnum_e numrecordpresets;
extern char currentrecordpreset[MAXPRESETNAME+1];
extern UINT8 currentrecordpresetversion;

recordpreset_t *G_AddRecordPreset(const char *name, const char *realname);
const char *G_GetRecordPresetName(const char *name);
strbuf_t *G_AddRecordPresetVersion(recordpreset_t *preset, UINT8 version);
strbuf_t *G_GetRecordPresetVersion(const char *name, UINT8 version);
void G_SetPresetCvars(strbuf_t *pv);
boolean G_CompareRecordPresetVersions(strbuf_t *pre1, strbuf_t *pre2);

typedef struct
{
	char prename[MAXPRESETNAME+1];
	UINT8 version;
	tic_t besttime;
	tic_t bestlap;
	tic_t playtime;
} maprecordpreset_t;

typedef struct
{
	char name[MAXMAPLUMPNAME];
	UINT8 visited;
	tic_t playtime;
	UINT32 roundsplayed;
	UINT32 roundswon;

	UINT16 numpresets;
	maprecordpreset_t *presets;
} maprecord_t;

extern maprecord_t **maprecords;
extern size_t nummaprecords;

maprecord_t *G_AllocateMapRecord(const char *mapname);
maprecord_t *G_GetMapRecord(const char *mapname);
maprecordpreset_t *G_AllocateMapRecordPreset(maprecord_t *record, const char *presetname, UINT8 version);
maprecordpreset_t *G_GetMapRecordPreset(maprecord_t *record, const char *presetname);
void G_ClearRecords(void);
boolean G_EmblemsEnabled(void);
tic_t G_GetBestTime(INT16 map);

FUNCMATH INT32 G_TicsToHours(tic_t tics);
FUNCMATH INT32 G_TicsToMinutes(tic_t tics, boolean full);
FUNCMATH INT32 G_TicsToSeconds(tic_t tics);
FUNCMATH INT32 G_TicsToCentiseconds(tic_t tics);
FUNCMATH INT32 G_TicsToMilliseconds(tic_t tics);

// Don't split up TOL handling
UINT32 G_TOLFlag(INT32 pgametype);
INT16 G_GetFirstMapOfGametype(UINT8 pgametype);

INT16 G_RandMap(UINT32 tolflags, INT16 pprevmap, UINT8 ignorebuffer, UINT8 maphell, INT16 *extbuffer);
void G_AddMapToBuffer(INT16 map);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
