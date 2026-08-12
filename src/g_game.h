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

#include "g_mapnum.h"
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
extern int32_t player_name_changes[MAXPLAYERS];

extern player_t players[MAXPLAYERS];
extern dboolean playeringame[MAXPLAYERS];

// gametic at level start
extern tic_t levelstarttic;

// for modding?
extern mapnum_t prevmap, nextmap;

struct roundentry_t
{
	mapnum_t mapnum;				// Map number at this position
	uint16_t gametype;				// Gametype we want to play this in
	dboolean encore;				// Whether this will be flipped
	dboolean rankrestricted;		// For grand prix progression
};

extern struct roundqueue
{
	uint8_t roundnum;							// Visible number on HUD
	uint8_t position;							// Head position in the round queue
	uint8_t size;								// Number of entries in the round queue
	dboolean netcommunicate;					// As server, should we net-communicate this in XD_MAP?
	dboolean writetextmap;					// This queue is for automated map conversion
	roundentry_t entries[ROUNDQUEUE_MAX];	// Entries in the round queue
} roundqueue;

void G_MapSlipIntoRoundQueue(uint8_t position, uint16_t map, uint8_t setgametype, dboolean setencore, dboolean rankrestricted);
void G_MapIntoRoundQueue(uint16_t map, uint8_t setgametype, dboolean setencore, dboolean rankrestricted);
void G_GPCupIntoRoundQueue(cupheader_t *cup, uint8_t setgametype, dboolean setencore);

extern mapnum_t kartmap2native[NEXTMAP_SPECIAL], nativemap2kart[NEXTMAP_SPECIAL];
extern mapnum_t nextexnum;

extern int32_t gameovertics;
extern uint8_t ammoremovaltics;
extern tic_t timeinmap; // Ticker for time spent in level (used for levelcard display)
extern int32_t pausedelay;

extern dboolean promptactive;


extern consvar_t cv_tutorialprompt;

extern consvar_t cv_chatwidth, cv_chatnotifications, cv_chatheight, cv_chattime, cv_consolechat, cv_chatbacktint, cv_chatspamprotection;
extern consvar_t cv_shoutname, cv_shoutcolor, cv_autoshout;
extern consvar_t cv_songcredits;
extern consvar_t cv_showfreeplay;
extern consvar_t cv_growmusic, cv_supermusic, cv_altshrinkmusic;

extern consvar_t cv_pauseifunfocused;

extern consvar_t cv_invertmouse;

extern consvar_t cv_kickstartaccel[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_shrinkme[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_jitterlegacy[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_driftmode[MAXSPLITSCREENPLAYERS];

extern consvar_t cv_deadzonex[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_deadzoney[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_deadzonet[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_deadzonetwod[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_deadzonestyle[MAXSPLITSCREENPLAYERS];

typedef enum
{
	FREELOOKSTYLE_OFF = 0,
	FREELOOKSTYLE_FLICK = 1,
	FREELOOKSTYLE_ROTATIONAL = 2
} freelookstyle_e;
extern consvar_t cv_freelookstyle[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_freelookrequiresbutton[MAXSPLITSCREENPLAYERS];

extern consvar_t cv_ghost_besttime, cv_ghost_bestlap, cv_ghost_last, cv_ghost_guest, cv_ghost_staff;

extern consvar_t cv_invincmusicfade;
extern consvar_t cv_growmusicfade;
extern consvar_t cv_altshrinkmusicfade;

extern consvar_t cv_resetspecialmusic;

extern consvar_t cv_resume;

extern consvar_t cv_allowrestat, cv_notifyrestat;

void weaponPrefChange(void);
void weaponPrefChange2(void);
void weaponPrefChange3(void);
void weaponPrefChange4(void);

#define MAXPLMOVE (50)

const char *G_BuildMapName(mapnum_t map);
mapnum_t G_MapNumber(const char *mapname);
mapnum_t G_LevelTitleToMapNum(const char * leveltitle);
mapnum_t G_KartMapToNative(mapnum_t mapnum);
mapnum_t G_NativeMapToKart(mapnum_t mapnum);

#define GAMEPADSHAKETHRESHOLD (UINT8_MAX/2)
#define TILTTOSTICKEASE 6

void G_ResetAnglePrediction(player_t *player);
void G_BuildTiccmd(ticcmd_t *cmd, int32_t realtics, uint8_t ssplayer);

// copy ticcmd_t to and fro the normal way
ticcmd_t *G_CopyTiccmd(ticcmd_t* dest, const ticcmd_t* src, const size_t n);
// copy ticcmd_t to and fro network packets
ticcmd_t *G_MoveTiccmd(ticcmd_t* dest, const ticcmd_t* src, const size_t n);

// clip the console player aiming to the view
int32_t G_ClipAimingPitch(int32_t *aiming);
int16_t G_SoftwareClipAimingPitch(int32_t *aiming);
void G_FinalClipAimingPitch(int32_t *aiming, player_t *player, dboolean skybox);

extern angle_t localangle[MAXSPLITSCREENPLAYERS];
extern int32_t localaiming[MAXSPLITSCREENPLAYERS]; // should be an angle_t but signed

fixed_t G_GetDeadZoneType(int32_t p, int8_t type);
int32_t G_PlayerInputAnalog(uint8_t p, int32_t gc, dboolean digital, int8_t type);
dboolean G_PlayerInputDown(uint8_t p, int32_t gc, dboolean digital, int8_t type);
dboolean G_ControlBoundToKey(uint8_t p, int32_t gc, int32_t key, dboolean defaults);
int8_t G_GetAxisTypeForData1(int8_t data1);
dboolean G_AxisInDeadzone(uint8_t p, event_t *ev);

//
// GAME
//
void G_ChangePlayerReferences(mobj_t *oldmo, mobj_t *newmo);
void G_DoReborn(int32_t playernum);
void G_PlayerReborn(int32_t player, dboolean betweenmaps);
void G_InitNew(uint8_t pencoremode, mapnum_t map, dboolean resetplayer,
	dboolean skipprecutscene);
char *G_BuildMapTitle(mapnum_t mapnum);

struct searchdim
{
	uint8_t pos;
	uint8_t siz;
};

struct mapsearchfreq_t
{
	int16_t  mapnum;
	uint8_t  matchc;
	struct searchdim *matchd;/* offset that a pattern was matched */
	uint8_t  keywhc;
	struct searchdim *keywhd;/* ...in KEYWORD */
	uint8_t  total;/* total hits */
};

mapnum_t G_FindMap(const char *query, char **foundmapnamep,
		mapsearchfreq_t **freqp, int32_t *freqc);
void G_FreeMapSearch(mapsearchfreq_t *freq, int32_t freqc);

/* Match map name by search + 2 digit map code or map number. */
mapnum_t G_FindMapByNameOrCode(const char *query, char **foundmapnamep);

// XMOD spawning
mapthing_t *G_FindTeamStart(int32_t playernum);
mapthing_t *G_FindBattleStart(int32_t playernum);
mapthing_t *G_FindRaceStart(int32_t playernum);
mapthing_t *G_FindMapStart(int32_t playernum);
void G_MovePlayerToSpawnOrStarpost(int32_t playernum, dboolean starpost);
void G_SpawnPlayer(int32_t playernum, dboolean starpost);

// Can be called by the startup code or M_Responder.
// A normal game starts at map 1, but a warp test can start elsewhere
void G_DeferedInitNew(dboolean pencoremode, mapnum_t map, int32_t pickedchar,
	uint8_t ssplayers, dboolean FLS);
void G_DoLoadLevel(dboolean resetplayer);
void G_DoLoadLevelEx(dboolean resetplayer, gamestate_t newstate);

void G_StartTitleCard(void);
void G_PreLevelTitleCard(void);
dboolean G_IsTitleCardAvailable(void);

// Can be called by the startup code or M_Responder, calls P_SetupLevel.
void G_LoadGame(uint32_t slot, mapnum_t mapoverride);

void G_SaveGameData(void);

void G_SaveGame(uint32_t slot, mapnum_t mapnum);

void G_SaveGameOver(uint32_t slot, dboolean modifylives);

void G_SetGametype(int16_t gametype);
char *G_PrepareGametypeConstant(const char *newgtconst);
void G_AddTOL(uint32_t newtol, const char *tolname);
void G_UpdateGametypeSelections(void);
int32_t G_GetGametypeByName(const char *gametypestr);
int32_t G_GuessGametypeByTOL(uint32_t tol);

dboolean G_IsSpecialStage(mapnum_t mapnum);
dboolean G_GametypeUsesLives(void);
dboolean G_GametypeHasTeams(void);
dboolean G_GametypeHasSpectators(void);
#define VOTEMODIFIER_ENCORE 0x80
int16_t G_SometimesGetDifferentGametype(uint8_t prefgametype);
uint8_t G_GetGametypeColor(int16_t gt);
void G_BeginLevelExit(void);
void G_FinishExitLevel(void);
void G_NextLevel(void);
dboolean G_MapIsSafe(mapnum_t map);
mapnum_t G_CheckNextMap(dboolean noadvancemap);
void G_GetNextMap(void);
void G_Continue(void);
void G_UseContinue(void);
void G_AfterIntermission(void);
void G_EndGame(void); // moved from y_inter.c/h and renamed

void G_Ticker(dboolean run);
dboolean G_Responder(event_t *ev);

dboolean G_CouldView(int32_t playernum);
dboolean G_CanView(int32_t playernum, uint8_t viewnum, dboolean onlyactive);

int32_t G_FindView(int32_t startview, uint8_t viewnum, dboolean onlyactive, dboolean reverse);
int32_t G_CountPlayersPotentiallyViewable(dboolean active);

void G_FixCamera(uint8_t view);
void G_ResetViews(dboolean resetfreecam);
void G_ResetView(uint8_t viewnum, int32_t playernum, dboolean onlyactive);
void G_AdjustViewEx(uint8_t viewnum, int32_t offset, dboolean onlyactive, dboolean resetfreecam);
#define G_AdjustView(v,o,oa) G_AdjustViewEx(v,o,oa, true)

void G_AddPlayer(int32_t playernum, int32_t console);
void G_SpectatePlayerOnJoin(int32_t playernum);

void G_SetExitGameFlag(void);
void G_ClearExitGameFlag(void);
dboolean G_GetExitGameFlag(void);

void G_SetRetryFlag(void);
void G_ClearRetryFlag(void);
dboolean G_GetRetryFlag(void);

void G_SetModeAttackRetryFlag(void);
void G_ClearModeAttackRetryFlag(void);
dboolean G_GetModeAttackRetryFlag(void);

void G_LoadGameData(void);
void G_LoadGameSettings(void);

void G_SetGameModified(dboolean silent, dboolean major);

dboolean G_GamestateUsesLevel(void);

// Gamedata record shit

#define MAXPRESETNAME 16

typedef struct
{
	char name[MAXPRESETNAME+1];
	char *realname;
	uint8_t numversions;
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
extern uint8_t currentrecordpresetversion;

recordpreset_t *G_AddRecordPreset(const char *name, const char *realname);
const char *G_GetRecordPresetName(const char *name);
strbuf_t *G_AddRecordPresetVersion(recordpreset_t *preset, uint8_t version);
strbuf_t *G_GetRecordPresetVersion(const char *name, uint8_t version);
void G_SetCurrentRecordPreset(recordpreset_t *preset);
const char *G_CheckPresetCvars(strbuf_t *pv);
void G_SetPresetCvars(strbuf_t *pv);
dboolean G_CompareRecordPresetVersions(strbuf_t *pre1, strbuf_t *pre2);

typedef struct
{
	char prename[MAXPRESETNAME+1];
	uint8_t version;
	tic_t besttime;
	tic_t bestlap;
	tic_t playtime;
} maprecordpreset_t;

typedef struct
{
	char name[MAXMAPLUMPNAME];
	uint8_t visited;
	tic_t playtime;
	uint32_t roundsplayed;
	uint32_t roundswon;

	uint16_t numpresets;
	maprecordpreset_t *presets;
} maprecord_t;

typedef enum
{
	REPLAY_BESTTIME,
	REPLAY_BESTLAP,
	REPLAY_LAST,
	REPLAY_GUEST,
	REPLAY__MAX,
} recordreplay_e;

extern maprecord_t **maprecords;
extern size_t nummaprecords;

maprecord_t *G_AllocateMapRecord(const char *mapname);
maprecord_t *G_GetMapRecord(const char *mapname);
maprecordpreset_t *G_AllocateMapRecordPreset(maprecord_t *record, const char *presetname, uint8_t version);
maprecordpreset_t *G_GetMapRecordPreset(maprecord_t *record, const char *presetname);
void G_ClearRecords(void);
tic_t G_GetBestTime(mapnum_t map);
dboolean G_EmblemsEnabled(void);
char *G_GetRecordReplayFolder(dboolean home, dboolean breaker);
char *G_GetRecordReplay(const char *folder, mapnum_t mapnum, uint16_t skinnum, recordreplay_e which);
dboolean G_CheckRecordReplay(const char *folder, mapnum_t mapnum, uint16_t skinnum, recordreplay_e which);

FUNCMATH int32_t G_TicsToHours(tic_t tics);
FUNCMATH int32_t G_TicsToMinutes(tic_t tics, dboolean full);
FUNCMATH int32_t G_TicsToSeconds(tic_t tics);
FUNCMATH int32_t G_TicsToCentiseconds(tic_t tics);
FUNCMATH int32_t G_TicsToMilliseconds(tic_t tics);

// Don't split up TOL handling
uint32_t G_TOLFlag(int32_t pgametype);
mapnum_t G_GetFirstMapOfGametype(uint8_t pgametype);

void G_AddMapToBuffer(mapnum_t map);

extern uint8_t g_countToGametype;

mapnum_t G_RandMap(uint32_t tolflags, mapnum_t pprevmap, dboolean ignoreBuffers, uint8_t maphell, dboolean callAgainSoon, mapnum_t *extBuffer);

typedef struct 
{
	int32_t player;
	dboolean notifyrestat;
	uint8_t kartspeed, kartweight, kartspeedrestat, kartweightrestat;
} restatmessage_t;
void G_HandleRestatMessage(restatmessage_t *rm);

typedef enum
{
	PBUMP_STANDARD = 0,
	PBUMP_GNDONLY,
	PBUMP_NONE,
	NUMPLAYERBUMPTYPES
} ATTRPACK playerbumptype_t;

extern playerbumptype_t playerbumpactive;

extern dboolean keyboardcontrolled;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
