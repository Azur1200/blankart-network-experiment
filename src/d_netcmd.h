// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  d_netcmd.h
/// \brief host/client network commands
///        commands are executed through the command buffer
///        like console commands

#ifndef __D_NETCMD__
#define __D_NETCMD__

#include "command.h"
#include "g_mapnum.h"

#ifdef __cplusplus
extern "C" {
#endif

// Big overshoot
#define PREFLISTSIZE (4096)

extern char prefvoiceliststr[PREFLISTSIZE];

extern consvar_t cv_showgremlins;

// console vars
extern consvar_t cv_playername[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_playercolor[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_skin[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_follower[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_followercolor[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_autovoxpref[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_dummyrestatspeed[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_dummyrestatweight[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_dummyrestatrandom[MAXSPLITSCREENPLAYERS];

// preferred number of players
extern consvar_t cv_splitplayers;

extern consvar_t cv_usemouse;
extern consvar_t cv_laglesscam;
extern consvar_t cv_usecontroller[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_controllerscale[MAXSPLITSCREENPLAYERS];

// normally in p_mobj but the .h is not read
extern consvar_t cv_itemrespawntime;
extern consvar_t cv_itemrespawn;

extern consvar_t cv_pointlimit;
extern consvar_t cv_timelimit;
extern consvar_t cv_numlaps;
extern UINT32 timelimitintics, extratimeintics, secretextratime;
extern consvar_t cv_allowexitlevel;

extern consvar_t cv_votemaxrows;

extern consvar_t cv_autobalance;
extern consvar_t cv_teamscramble;
extern consvar_t cv_scrambleonchange;

extern consvar_t cv_netstat;

extern consvar_t cv_countdowntime;
extern consvar_t cv_runscripts;
extern consvar_t cv_mute;
extern consvar_t cv_pause;

extern consvar_t cv_restrictskinchange, cv_allowteamchange, cv_ingamecap, cv_respawntime;
extern consvar_t cv_spectatorreentry, cv_antigrief;

#ifdef DEVELOP
	extern consvar_t cv_debugtraversemax;
#endif

//Save server ip
extern consvar_t cv_lastserver;

extern consvar_t cv_kartminimap;
extern consvar_t cv_kartcheck;
extern consvar_t cv_kartinvinsfx;
extern consvar_t cv_kartspeed, cv_kartbattlespeed;
extern consvar_t cv_kartbumpers;
extern consvar_t cv_kartfrantic;
extern consvar_t cv_kartcomeback;
extern consvar_t cv_kartencore;
extern consvar_t cv_kartvoterulechanges;
extern consvar_t cv_kartgametypepreference;
extern consvar_t cv_kartspeedometer;
extern consvar_t cv_kartvoices;
extern consvar_t cv_karthorns;
extern consvar_t cv_kartbot;
extern consvar_t cv_kartbot_cap;
extern consvar_t cv_kartbot_modifiermax;
extern consvar_t cv_kartbot_basetrackcomplexity;
extern consvar_t cv_kartbot_finish;
extern consvar_t cv_karteliminatelast, cv_kartdeathbounces;
extern consvar_t cv_kartusepwrlv;
extern consvar_t cv_kartrings;
extern consvar_t cv_kartringsmin;
extern consvar_t cv_kartringsmax;
extern consvar_t cv_kartringsstart;

extern consvar_t cv_kartstacking;
extern consvar_t cv_kartstacking_diminishparam;
extern consvar_t cv_kartstacking_maxvanillaboost;
extern consvar_t cv_kartstacking_speedboostdropoff;
extern consvar_t cv_kartstacking_speedboostdropoff_brake;
extern consvar_t cv_kartstacking_accelstack;

extern consvar_t cv_kartstacking_sneaker_easyspeedboost;
extern consvar_t cv_kartstacking_sneaker_normalspeedboost;
extern consvar_t cv_kartstacking_sneaker_hardspeedboost;
extern consvar_t cv_kartstacking_sneaker_expertspeedboost;
extern consvar_t cv_kartstacking_sneaker_accelboost;
extern consvar_t cv_kartstacking_sneaker_handleboost;
extern consvar_t cv_kartstacking_sneaker_maxgrade;
extern consvar_t cv_kartstacking_sneaker_stackable;
extern consvar_t cv_kartstacking_panel_separate;
extern consvar_t cv_kartstacking_panel_maxgrade;


extern consvar_t cv_kartstacking_invincibility_speedboost;
extern consvar_t cv_kartstacking_invincibility_accelboost;
extern consvar_t cv_kartstacking_invincibility_handleboost;
extern consvar_t cv_kartstacking_invincibility_stackable;

extern consvar_t cv_kartstacking_smonitor_speedboost;
extern consvar_t cv_kartstacking_smonitor_fastestspeed;
extern consvar_t cv_kartstacking_smonitor_accelboost;
extern consvar_t cv_kartstacking_smonitor_handleboost;
extern consvar_t cv_kartstacking_smonitor_stackable;

extern consvar_t cv_kartstacking_flame_speedval;
extern consvar_t cv_kartstacking_flame_accelboost;
extern consvar_t cv_kartstacking_flame_handleboost;
extern consvar_t cv_kartstacking_flame_stackable;

extern consvar_t cv_kartstacking_attraction_speedboost_himin;
extern consvar_t cv_kartstacking_attraction_speedboost_himax;
extern consvar_t cv_kartstacking_attraction_speedboost_normmin;
extern consvar_t cv_kartstacking_attraction_speedboost_normmax;
extern consvar_t cv_kartstacking_attraction_accelboost_hi;
extern consvar_t cv_kartstacking_attraction_accelboost_norm;
extern consvar_t cv_kartstacking_attraction_handleboost;
extern consvar_t cv_kartstacking_attraction_stackable;

extern consvar_t cv_kartstacking_grow_speedboost;
extern consvar_t cv_kartstacking_grow_accelboost;
extern consvar_t cv_kartstacking_grow_handleboost;
extern consvar_t cv_kartstacking_grow_stackable;

extern consvar_t cv_kartstacking_bubble_speedboost;
extern consvar_t cv_kartstacking_bubble_accelboost;
extern consvar_t cv_kartstacking_bubble_handleboost;
extern consvar_t cv_kartstacking_bubble_stackable;

extern consvar_t cv_kartaltshrinktime;
extern consvar_t cv_kartstacking_altshrink_speedboost;
extern consvar_t cv_kartstacking_altshrink_accelboost;
extern consvar_t cv_kartstacking_altshrink_handleboost;
extern consvar_t cv_kartstacking_altshrink_stackable;

extern consvar_t cv_kartstacking_start_speedboost;
extern consvar_t cv_kartstacking_start_accelboost;
extern consvar_t cv_kartstacking_start_handleboost;
extern consvar_t cv_kartstacking_start_stackable;

extern consvar_t cv_kartstacking_walltransfer_speedboost;
extern consvar_t cv_kartstacking_walltransfer_accelboost;
extern consvar_t cv_kartstacking_walltransfer_handleboost;
extern consvar_t cv_kartstacking_walltransfer_stackable;

extern consvar_t cv_kartstacking_drift_speedboost;
extern consvar_t cv_kartstacking_drift_accelboost;
extern consvar_t cv_kartstacking_drift_handleboost;
extern consvar_t cv_kartstacking_drift_stackable;

extern consvar_t cv_kartstacking_ring_speedboost;
extern consvar_t cv_kartstacking_ring_accelboost;
extern consvar_t cv_kartstacking_ring_handleboost;
extern consvar_t cv_kartstacking_ring_stackable;

extern consvar_t cv_kartstacking_heavydrop_speedboost;
extern consvar_t cv_kartstacking_heavydrop_accelboost;
extern consvar_t cv_kartstacking_heavydrop_handleboost;
extern consvar_t cv_kartstacking_heavydrop_stackable;
extern consvar_t cv_kartstacking_heavydrop_uniform;

extern consvar_t cv_kartstacking_ssmt_speedboost;
extern consvar_t cv_kartstacking_ssmt_accelboost;
extern consvar_t cv_kartstacking_ssmt_handleboost;

extern consvar_t cv_kartstacking_recspin_accelboost_lo;
extern consvar_t cv_kartstacking_recspin_handleboost_lo;

extern consvar_t cv_kartstacking_recspin_accelboost_hi;
extern consvar_t cv_kartstacking_recspin_handleboost_hi;

extern consvar_t cv_kartstacking_slope_decay;
extern consvar_t cv_kartstacking_slope_brakemod;
extern consvar_t cv_kartstacking_slope_speedboost_max;
extern consvar_t cv_kartstacking_slope_speedboost_cap;
extern consvar_t cv_kartstacking_slope_accelboost;
extern consvar_t cv_kartstacking_slope_stackable;

extern consvar_t cv_kartdrafting;
extern consvar_t cv_kartdrafting_closedraft;
extern consvar_t cv_kartdrafting_closedeadzone;
extern consvar_t cv_kartdrafting_basedistance;
extern consvar_t cv_kartstacking_drafting_minspeed;
extern consvar_t cv_kartstacking_drafting_maxspeed;

extern consvar_t cv_kartchaining;
extern consvar_t cv_kartchainingoffroad;

extern consvar_t cv_kartitembreaker;
//extern consvar_t cv_kartwalltransfer;
extern consvar_t cv_kartpurpledrift;
extern consvar_t cv_kartbumpspark;
extern consvar_t cv_kartbumpspring;
extern consvar_t cv_kartexplosion_limitlifetime;
extern consvar_t cv_kartexplosion_limitlifetime_cap;
extern consvar_t cv_kartslipdash;
extern consvar_t cv_kartslopeboost;
extern consvar_t cv_kartinvintheme;
extern consvar_t cv_kartinvindamage, cv_kartorbijawzdmg;
extern consvar_t cv_kartsmonitordist;
extern consvar_t cv_kartsmonitor_congadist;
extern consvar_t cv_kartsmonitor_decaydist;
extern consvar_t cv_kartsmonitor_decaytime;
extern consvar_t cv_kartsmonitor_intangible;

// opinionated stuff for testing
extern consvar_t cv_kartbubble_defense_canidle;
extern consvar_t cv_kartbubble_defense_damagerate;
extern consvar_t cv_kartbubble_boost_allow;
extern consvar_t cv_kartbubble_boost_offroadignore;
extern consvar_t cv_kartflame_fastfuel;
extern consvar_t cv_kartflame_offroadburn;
extern consvar_t cv_kartflame_shove;
extern consvar_t cv_kartattraction_assistpower;
extern consvar_t cv_kartattraction_aoedmg;
extern consvar_t cv_kartattraction_aoeemp;
extern consvar_t cv_kartaltshrink_arrowbullet;
extern consvar_t cv_kartaltshrink_arrowbulletthres;
extern consvar_t cv_kartaltshrink_runneranger;

extern consvar_t cv_kartairsquish;

extern consvar_t cv_kartairdrop;

extern consvar_t cv_kartairthrust;
extern consvar_t cv_kartairthrust_reductionrate, cv_kartairthrust_flashreductionrate;
extern consvar_t cv_kartairthrust_power1;
extern consvar_t cv_kartairthrust_power2;
extern consvar_t cv_kartairthrust_power3;
extern consvar_t cv_kartairthrust_power4;

extern consvar_t cv_kartairsuperring_reductionrate;

extern consvar_t cv_kartwaterskiplock;

extern consvar_t cv_kartitemlitter, cv_kartitempush;

extern consvar_t cv_kartantibump;

extern consvar_t cv_trailslow;

extern consvar_t cv_playerbump;

extern consvar_t cv_kartforcelegacyodds;

extern consvar_t cv_handleboostslip;

extern consvar_t cv_kartrecoverydash;
extern consvar_t cv_kartrecoverydash_spinspeed;

extern consvar_t cv_kartkeepstuff;

extern consvar_t cv_kartoddsdist;
extern consvar_t cv_kartlegacyoddsdist;

extern consvar_t cv_kartspbrush;

extern consvar_t cv_itemlist;

extern consvar_t cv_encorevotes;

extern consvar_t cv_votetime;

extern consvar_t cv_kartdebugitem, cv_kartdebugamount, cv_kartdebugdistribution, cv_kartdebughuddrop;
extern consvar_t cv_kartdebugshrink;
extern consvar_t cv_kartdebugcheckpoint, cv_kartdebugnodes, cv_kartdebugcolorize, cv_kartdebugdirector;
extern consvar_t cv_kartdebugwaypoints, cv_kartdebuglap, cv_kartdebugbot, cv_kartdebugcluster, cv_kartdebugrings;
extern consvar_t cv_gptest;

extern consvar_t cv_itemfinder;

extern consvar_t cv_inttime, cv_rolltime, cv_advancemap;
extern consvar_t cv_overtime;
extern consvar_t cv_startinglives;

// for F_finale.c
extern consvar_t cv_rollingdemos;

extern consvar_t cv_soundtest;

extern consvar_t cv_maxping;
extern consvar_t cv_lagless;
extern consvar_t cv_pingtimeout;
extern consvar_t cv_showping;
extern consvar_t cv_pingmeasurement;
extern consvar_t cv_pingicon;
extern consvar_t cv_pingstyle;

extern consvar_t cv_showminimapnames;
extern consvar_t cv_showminimapangle;
extern consvar_t cv_minihead;
extern consvar_t cv_spinoutroll;

extern consvar_t cv_showlapemblem;
extern consvar_t cv_lapemblemmode;
extern consvar_t cv_racesplits;
extern consvar_t cv_showviewpointtext;
extern consvar_t cv_rouletteonplayer;
extern consvar_t cv_rouletteplayertrans, cv_rouletteplayerboxtrans;

extern consvar_t cv_skipmapcheck;

extern consvar_t cv_sleep;

extern consvar_t cv_perfstats;

extern consvar_t cv_director;

extern consvar_t cv_schedule;

extern consvar_t cv_debugmappatch;

extern consvar_t cv_allowmismatchmappatch;

extern char timedemo_name[256];
extern boolean timedemo_csv;
extern char timedemo_csv_id[256];
extern boolean timedemo_quit;

typedef enum
{
	XD_NAMEANDCOLOR = 1,
	XD_WEAPONPREF,  // 2
	XD_KICK,        // 3
	XD_NETVAR,      // 4
	XD_SAY,         // 5
	XD_MAP,         // 6
	XD_EXITLEVEL,   // 7
	XD_ADDFILE,     // 8
	XD_PAUSE,       // 9
	XD_ADDPLAYER,   // 10
	XD_TEAMCHANGE,  // 11
	XD_CLEARSCORES, // 12
	XD_VERIFIED,    // 13
	XD_RANDOMSEED,  // 14
	XD_RUNSOC,      // 15
	XD_REQADDFILE,  // 16
	XD_RESPAWN,     // 17
	XD_DEMOTED,     // 18
	XD_LUACMD,      // 19
	XD_LUAVAR,      // 20
	XD_LUAFILE,     // 21

	// SRB2Kart
	XD_SETUPVOTE,   // 22
	XD_MODIFYVOTE,  // 23
	XD_PICKVOTE,    // 24
	XD_REMOVEPLAYER,// 25
	XD_POWERLEVEL,  // 26
	XD_PARTYINVITE, // 27
	XD_ACCEPTPARTYINVITE, // 28
	XD_LEAVEPARTY,  // 29
	XD_CANCELPARTYINVITE, // 30
	XD_ADDBOT,      // 31
	XD_DISCORD,     // 32
	XD_PLAYSOUND,   // 33
	XD_SCHEDULETASK, // 34
	XD_SCHEDULECLEAR, // 35
	XD_AUTOMATE,    // 36
	XD_CHEAT,       // 37
	XD_MAPQUEUE,	// 38
	XD_CLANTAGSET,	// 39
	XD_CLANTAGCLEAR,// 40

	MAXNETXCMD
} netxcmd_t;

extern const char *netxcmdnames[MAXNETXCMD - 1];

//Packet composition for Command_TeamChange_f() ServerTeamChange, etc.
//bitwise structs make packing bits a little easier, but byte alignment harder?
//todo: decide whether to make the other netcommands conform, or just get rid of this experiment.
struct changeteam_packet_t {
	UINT32 playernum    : 5;  // value 0 to 31
	UINT32 newteam      : 5;  // value 0 to 31
	UINT32 verification : 1;  // value 0 to 1
	UINT32 autobalance  : 1;  // value 0 to 1
	UINT32 scrambled    : 1;  // value 0 to 1
} ATTRPACK;

struct changeteam_value_t {
	UINT16 l; // liitle endian
	UINT16 b; // big enian
} ATTRPACK;

//Since we do not want other files/modules to know about this data buffer we union it here with a Short Int.
//Other files/modules will hand the INT16 back to us and we will decode it here.
//We don't have to use a union, but we would then send four bytes instead of two.
typedef union {
	changeteam_packet_t packet;
	changeteam_value_t value;
} ATTRPACK changeteam_union;

// add game commands, needs cleanup
void D_RegisterServerCommands(void);
void D_RegisterClientCommands(void);
void CleanupPlayerName(INT32 playernum, const char *newname);
boolean EnsurePlayerNameIsGood(char *name, INT32 playernum);
void WeaponPref_Send(UINT8 ssplayer);
void WeaponPref_Save(UINT8 **cp, INT32 playernum);
void WeaponPref_Parse(UINT8 **cp, INT32 playernum);
void D_SendPlayerConfig(UINT8 n);
void Command_ExitGame_f(void);
void Command_Retry_f(void);
void D_GameTypeChanged(INT32 lastgametype); // not a real _OnChange function anymore
void D_MapChange(mapnum_t pmapnum, INT32 pgametype, boolean pencoremode, boolean presetplayers, INT32 pdelay, boolean pskipprecutscene, boolean pforcespecialstage);
void D_SetupVote(void);
void D_ModifyClientVote(UINT8 player, SINT8 voted);
void D_PickVote(void);
void ObjectPlace_OnChange(void);
boolean IsPlayerAdmin(INT32 playernum);
void SetAdminPlayer(INT32 playernum);
void ClearAdminPlayers(void);
void RemoveAdminPlayer(INT32 playernum);
void ItemFinder_OnChange(void);
void D_SetPassword(const char *pw);
void D_ClearPassword(void);

void P_SetPlayerSpectator(INT32 playernum);

struct scheduleTask_t
{
	UINT16 basetime;
	UINT16 timer;
	char *command;
};

extern scheduleTask_t **schedule;
extern size_t schedule_size;
extern size_t schedule_len;

void Schedule_Run(void);
void Schedule_Insert(scheduleTask_t *addTask);
void Schedule_Add(INT16 basetime, INT16 timeleft, const char *command);
void Schedule_Clear(void);

typedef enum
{
	AEV_ROUNDSTART,
	AEV_INTERMISSIONSTART,
	AEV_VOTESTART,
	AEV_QUEUESTART,
	AEV_QUEUEEND,
	AEV_LEVELSTART,
	AEV__MAX
} automateEvents_t;

void Automate_Run(automateEvents_t type);
void Automate_Set(automateEvents_t type, const char *command);
void Automate_Clear(void);

void D_Cheat(INT32 playernum, INT32 cheat, ...);

// used for the player setup menu
UINT8 CanChangeSkin(INT32 playernum);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
