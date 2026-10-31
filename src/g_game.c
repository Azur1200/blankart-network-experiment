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
/// \file  g_game.c
/// \brief game loop functions, events handling

#include "d_netcmd.h"
#include "doomdef.h"
#include "console.h"
#include "d_main.h"
#include "d_clisrv.h"
#include "d_player.h"
#include "d_clisrv.h"
#include "f_finale.h"
#include "filesrch.h" // for refreshdirmenu
#include "g_input.h"
#include "m_fixed.h"
#include "p_setup.h"
#include "p_saveg.h"
#include "i_time.h"
#include "i_system.h"
#include "am_map.h"
#include "m_random.h"
#include "p_local.h"
#include "r_draw.h"
#include "r_main.h"
#include "s_sound.h"
#include "g_game.h"
#include "g_demo.h"
#include "m_cheat.h"
#include "m_misc.h"
#include "m_menu.h"
#include "m_argv.h"
#include "hu_stuff.h"
#include "st_stuff.h"
#include "z_zone.h"
#include "i_video.h"
#include "byteptr.h"
#include "i_gamepad.h"
#include "r_local.h"
#include "r_skins.h"
#include "y_inter.h"
#include "v_video.h"
#include "lua_hook.h"
#include "m_cond.h" // condition sets
#include "r_fps.h" // frame interpolation/uncapped
#include "lua_hud.h"

// SRB2kart
#include "k_kart.h"
#include "k_stats.h" // SRB2kart
#include "k_battle.h"
#include "k_pwrlv.h"
#include "k_color.h"
#include "k_grandprix.h"
#include "k_boss.h"
#include "k_specialstage.h"
#include "k_bot.h"
#include "k_items.h"
#include "doomstat.h"
#include "acs/interface.h"
#include "k_director.h"
#include "g_party.h"
#include "f_dscredits.hpp"
#include "strbuf.h"
#include "k_vote.h"
#include "d_clisrv.h"

#include <tracy/tracy/TracyC.h>

#ifdef HAVE_DISCORDRPC
#include "discord.h"
#endif

#ifdef HWRENDER
#include "hardware/hw_main.h" // for cv_glshearing
#endif

gameaction_t gameaction;
gamestate_t gamestate = GS_NULL;

JoyType_t Joystick[MAXSPLITSCREENPLAYERS];

// SRB2kart
char gamedatafilename[64] = "blandata.dat";
char timeattackfolder[64] = "blan";
char customversionstring[32] = "\0";

static void G_DoCompleted(void);
static void G_DoStartContinue(void);
static void G_DoContinued(void);
static void G_DoWorldDone(void);
static void G_DoStartVote(void);

char   mapmusname[7]; // Music name
uint16_t mapmusflags; // Track and reset bit
uint32_t mapmusposition; // Position to jump to
uint32_t mapmusresume;
uint8_t mapmusrng; // Random selection result

mapnum_t gamemap = 2; // GHZ
dboolean g_reloadingMap;
uint32_t maptol;

preciptype_t globalweather = PRECIP_NONE;
preciptype_t curWeather = PRECIP_NONE;

precipprops_t precipprops[MAXPRECIP] =
{
	{"NONE",				MT_NULL, 			0}, // PRECIP_NONE
	{"RAIN",				MT_RAIN, 			0}, // PRECIP_RAIN
	{"SNOW",				MT_SNOWFLAKE,		0}, // PRECIP_SNOW
	{"STORM",				MT_RAIN,			PRECIPFX_THUNDER|PRECIPFX_LIGHTNING}, // PRECIP_STORM
	{"STORM_NORAIN",		MT_NULL,			PRECIPFX_THUNDER|PRECIPFX_LIGHTNING}, // PRECIP_STORM_NORAIN
	{"STORM_NOSTRIKES",		MT_RAIN,			PRECIPFX_THUNDER} // PRECIP_STORM_NOSTRIKES
};

preciptype_t precip_freeslot = PRECIP_FIRSTFREESLOT;

int32_t cursaveslot = 0; // Auto-save 1p savegame slot
//int16_t lastmapsaved = 0; // Last map we auto-saved at
int16_t lastmaploaded = 0; // Last map the game loaded
uint8_t gamecomplete = 0;

marathonmode_t marathonmode = 0;
tic_t marathontime = 0;

uint8_t numgameovers = 0; // for startinglives balance
int8_t startinglivesbalance[maxgameovers+1] = {3, 5, 7, 9, 12, 15, 20, 25, 30, 40, 50, 75, 99, 0x7F};

dboolean modifiedgame = false; // Set if homebrew PWAD stuff has been added.
dboolean majormods = false; // Set if Lua/Gameplay SOC/replacement map has been added.
dboolean savemoddata = false;
uint8_t paused;
uint8_t modeattacking = ATTACKING_NONE;
dboolean imcontinuing = false;
dboolean runemeraldmanager = false;
uint16_t emeraldspawndelay = 60*TICRATE;

// menu demo things
uint8_t  numDemos      = 0;
uint32_t demoDelayTime = 15*TICRATE;
uint32_t demoIdleTime  = 3*TICRATE;

dboolean netgame; // only true if packets are broadcast
dboolean multiplayer;
dboolean playeringame[MAXPLAYERS];
dboolean addedtogame;
player_t players[MAXPLAYERS];

int32_t consoleplayer; // player taking events and displaying
int32_t displayplayers[MAXSPLITSCREENPLAYERS]; // view being displayed
int32_t g_localplayers[MAXSPLITSCREENPLAYERS];

tic_t gametic;
tic_t levelstarttic; // gametic at level start
int16_t lastmap; // last level you were at (returning from special stages)
tic_t timeinmap; // Ticker for time spent in level (used for levelcard display)

char * titlemap = NULL;
dboolean hidetitlepics = false;
char * bootmap = NULL; //bootmap for loading a map on startup

char * tutorialmap = NULL; // map to load for tutorial
dboolean tutorialmode = false; // are we in a tutorial right now?

dboolean looptitle = true;

uint16_t skincolor_redteam = SKINCOLOR_RED;
uint16_t skincolor_blueteam = SKINCOLOR_BLUE;
uint16_t skincolor_redring = SKINCOLOR_RASPBERRY;
uint16_t skincolor_bluering = SKINCOLOR_PERIWINKLE;

dboolean exitfadestarted = false;

cutscene_t *cutscenes[128] = {};
textprompt_t *textprompts[MAX_PROMPTS];

mapnum_t nextmapoverride;
uint8_t skipstats;

struct quake quake = {};

// Map Header Information
mapheader_t** mapheaderinfo = {NULL};
int32_t nummapheaders, mapallocsize = 0;

// Kart cup definitions
cupheader_t *kartcupheaders = NULL;
uint16_t numkartcupheaders = 0;

recordpreset_t **recordpresets;
recordpresetnum_e numrecordpresets;

maprecord_t **maprecords;
size_t nummaprecords;

char currentrecordpreset[MAXPRESETNAME+1] = "kart"; // if one has never been selected
uint8_t currentrecordpresetversion = 1;

static dboolean exitgame = false;
static dboolean retrying = false;
static dboolean retryingmodeattack = false;

uint8_t stagefailed; // Used for GEMS BONUS? Also to see if you beat the stage.

uint16_t emeralds;
int32_t luabanks[NUM_LUABANKS];
uint32_t token; // Number of tokens collected in a level
uint32_t tokenlist; // List of tokens collected
dboolean gottoken; // Did you get a token? Used for end of act
int32_t tokenbits; // Used for setting token bits

dboolean gamedataloaded = false;

// Temporary holding place for nights data for the current map
//nightsdata_t ntemprecords;

uint32_t bluescore, redscore; // CTF and Team Match team scores

// ring count... for PERFECT!
int32_t nummaprings = 0;

// Elminates unnecessary searching.
dboolean CheckForBustableBlocks;
dboolean CheckForBouncySector;
dboolean CheckForQuicksand;
dboolean CheckForMarioBlocks;
dboolean CheckForFloatBob;
dboolean CheckForReverseGravity;

// Powerup durations
uint16_t invulntics = 20*TICRATE;
uint16_t sneakertics = 20*TICRATE;
uint16_t flashingtics = 3*TICRATE/2; // SRB2kart
uint16_t tailsflytics = 8*TICRATE;
uint16_t underwatertics = 30*TICRATE;
uint16_t spacetimetics = 11*TICRATE + (TICRATE/2);
uint16_t extralifetics = 4*TICRATE;
uint16_t nightslinktics = 2*TICRATE;

int32_t gameovertics = 15*TICRATE;
uint8_t ammoremovaltics = 2*TICRATE;

// SRB2kart
tic_t introtime = 3;
tic_t starttime = 3;

tic_t raceexittime = 5*TICRATE + (2*TICRATE/3);
tic_t battleexittime = 8*TICRATE;

int32_t hyudorotime = 7*TICRATE;
int32_t stealtime = TICRATE/2;
int32_t sneakertime = TICRATE + (TICRATE/3);
int32_t waterpaneltime = TICRATE*2;
int32_t itemtime = 8*TICRATE;
int32_t bubbletime = TICRATE/2;
int32_t comebacktime = 10*TICRATE;
int32_t bumptime = 6;
int32_t shovetime = 4;
int32_t greasetics = 3*TICRATE;
int32_t wipeoutslowtime = 20;
int32_t wantedreduce = 5*TICRATE;
int32_t wantedfrequency = 10*TICRATE;
int32_t flametime = 25*TICRATE + (2*TICRATE/10); // (((8*TICRATE)*3) + (4*TICRATE))

uint8_t use1upSound = 0;
uint8_t maxXtraLife = 2; // Max extra lives from rings

uint8_t introtoplay;
uint8_t creditscutscene;

// Emerald locations
mobj_t *hunt1;
mobj_t *hunt2;
mobj_t *hunt3;

tic_t racecountdown, exitcountdown; // for racing
exitcondition_t g_exit;

fixed_t gravity;
fixed_t mapobjectscale;

struct maplighting maplighting = {};

int16_t autobalance; //for CTF team balance
int16_t teamscramble; //for CTF team scramble
int16_t scrambleplayers[MAXPLAYERS]; //for CTF team scramble
int16_t scrambleteams[MAXPLAYERS]; //for CTF team scramble
int16_t scrambletotal; //for CTF team scramble
int16_t scramblecount; //for CTF team scramble

int32_t cheats; //for multiplayer cheat commands

// SRB2Kart
// Cvars that we don't want changed mid-game
uint8_t numlaps; // Removed from Cvar hell
tic_t autopartime = 0; // Autogenerated by circuit length on race start
tic_t leaderfinishtime = 0; // When did first place finish?
uint8_t gamespeed; // Game's current speed (or difficulty, or cc, or etc); 0 for easy, 1 for normal, 2 for hard
dboolean encoremode = false; // Encore Mode currently enabled?
dboolean prevencoremode;
dboolean franticitems; // Frantic items currently enabled?
dboolean comeback; // Battle Mode's karma comeback is on/off

int32_t comebackscorecap = 0; // Highest the comeback score can raise a player's score

// Voting system
int16_t g_voteLevels[12][2]; // Levels that were rolled by the host
int8_t g_votes[MAXPLAYERS]; // Each player's vote
int8_t g_pickedVote; // What vote the host rolls

// Server-sided, synched variables
int8_t battlewanted[4]; // WANTED players in battle, worth x2 points
int8_t mostwanted; // The "most wanted" (first in line) player.
tic_t wantedcalcdelay; // Time before it recalculates WANTED
tic_t mapreset; // Map reset delay when enough players have joined an empty game
dboolean thwompsactive; // Thwomps activate on lap 2
uint8_t lastLowestLap; // Last lowest lap, for activating race lap executors
int8_t spbplace; // SPB exists, give the person behind better items
dboolean startedInFreePlay; // Map was started in free play

// Client-sided, unsynched variables (NEVER use in anything that needs to be synced with other players)
tic_t bombflashtimer = 0;	// Cooldown before another FlashPal can be intialized by a bomb exploding near a displayplayer. Avoids seizures.
dboolean legitimateexit; // Did this client actually finish the match?
dboolean comebackshowninfo; // Have you already seen the "ATTACK OR PROTECT" message?
tic_t antibumptime; // Delay before players start bumping into one another.

// Grading
uint32_t timesBeaten;

typedef struct joystickvector2_s
{
	int32_t xaxis;
	int32_t yaxis;
} joystickvector2_t;

mapnum_t prevmap, nextmap;

mapnum_t kartmap2native[NEXTMAP_SPECIAL] = {0}, nativemap2kart[NEXTMAP_SPECIAL] = {0};
mapnum_t nextexnum = NUMMAPS;

// don't mind me putting these here, I was lazy to figure out where else I could put those without blowing up the compiler.

// chat timer thingy
static CV_PossibleValue_t chattime_cons_t[] = {{5, "MIN"}, {999, "MAX"}, {0, NULL}};
consvar_t cv_chattime = CVAR_INIT ("chattime", "8", CV_SAVE, chattime_cons_t, NULL);

// chatwidth
static CV_PossibleValue_t chatwidth_cons_t[] = {{64, "MIN"}, {150, "MAX"}, {0, NULL}};
consvar_t cv_chatwidth = CVAR_INIT ("chatwidth", "150", CV_SAVE, chatwidth_cons_t, NULL);

// chatheight
static CV_PossibleValue_t chatheight_cons_t[] = {{6, "MIN"}, {22, "MAX"}, {0, NULL}};
consvar_t cv_chatheight = CVAR_INIT ("chatheight", "16", CV_SAVE, chatheight_cons_t, NULL);

// chat notifications (do you want to hear beeps? I'd understand if you didn't.)
consvar_t cv_chatnotifications = CVAR_INIT ("chatnotifications", "On", CV_SAVE, CV_OnOff, NULL);

// chat spam protection (why would you want to disable that???)
consvar_t cv_chatspamprotection = CVAR_INIT ("chatspamprotection", "On", CV_SAVE, CV_OnOff, NULL);

// minichat text background
consvar_t cv_chatbacktint = CVAR_INIT ("chatbacktint", "On", CV_SAVE, CV_OnOff, NULL);

// old shit console chat. (mostly exists for stuff like terminal, not because I cared if anyone liked the old chat.)
static CV_PossibleValue_t consolechat_cons_t[] = {{0, "Window"}, {1, "Console"}, {2, "Window (Hidden)"}, {0, NULL}};
consvar_t cv_consolechat = CVAR_INIT ("chatmode", "Window", CV_SAVE, consolechat_cons_t, NULL);

// Shout settings
// The relevant ones are CV_NETVAR because too lazy to send them any other way
consvar_t cv_shoutname = CVAR_INIT ("shout_name", "SERVER", CV_NETVAR, NULL, NULL);

static CV_PossibleValue_t shoutcolor_cons_t[] =
{
	{-1, "Player color"},
	{0, "White"},
	{1, "Yellow"},
	{2, "Purple"},
	{3, "Green"},
	{4, "Blue"},
	{5, "Red"},
	{6, "Gray"},
	{7, "Orange"},
	{8, "Sky-blue"},
	{9, "Gold"},
	{10, "Lavender"},
	{11, "Aqua-green"},
	{12, "Magenta"},
	{13, "Pink"},
	{14, "Brown"},
	{15, "Tan"},
	{0, NULL}
};
consvar_t cv_shoutcolor = CVAR_INIT ("shout_color", "Red", CV_NETVAR, shoutcolor_cons_t, NULL);

// If on and you're an admin, your messages will automatically become shouts.
consvar_t cv_autoshout = CVAR_INIT ("autoshout", "Off", CV_NETVAR, CV_OnOff, NULL);

// Pause game upon window losing focus
consvar_t cv_pauseifunfocused = CVAR_INIT ("pauseifunfocused", "Yes", CV_SAVE, CV_YesNo, NULL);

// Display song credits
consvar_t cv_songcredits = CVAR_INIT ("songcredits", "On", CV_SAVE, CV_OnOff, NULL);

// Show "FREE PLAY" when you're alone. :(
consvar_t cv_showfreeplay = CVAR_INIT ("showfreeplay", "On", CV_SAVE, CV_OnOff, NULL);

// We can disable special tunes!
static CV_PossibleValue_t powermusic_cons_t[] = {{0, "Off"}, {1, "On"}, {2, "SFX"}, {0, NULL}};
consvar_t cv_growmusic  = CVAR_INIT ("growmusic", "On", CV_SAVE, powermusic_cons_t, NULL);
consvar_t cv_supermusic = CVAR_INIT ("supermusic", "On", CV_SAVE, powermusic_cons_t, NULL);
consvar_t cv_altshrinkmusic = CVAR_INIT ("altshrinkmusic", "On", CV_SAVE, powermusic_cons_t, NULL);

consvar_t cv_invertmouse = CVAR_INIT ("invertmouse", "Off", CV_SAVE, CV_OnOff, NULL);

consvar_t cv_invincmusicfade = CVAR_INIT ("invincmusicfade", "300", CV_SAVE, CV_Unsigned, NULL);
consvar_t cv_growmusicfade = CVAR_INIT ("growmusicfade", "500", CV_SAVE, CV_Unsigned, NULL);
consvar_t cv_altshrinkmusicfade = CVAR_INIT ("altshrinkmusicfade", "500", CV_SAVE, CV_Unsigned, NULL);

consvar_t cv_resetspecialmusic = CVAR_INIT ("resetspecialmusic", "Yes", CV_SAVE, CV_YesNo, NULL);

consvar_t cv_resume = CVAR_INIT ("resume", "Yes", CV_SAVE, CV_YesNo, NULL);

static CV_PossibleValue_t mindelay_cons_t[] = {{0, "MIN"}, {30, "MAX"}, {0, NULL}};
consvar_t cv_mindelay = CVAR_INIT ("mindelay", "0", CV_SAVE|CV_CALL, mindelay_cons_t, weaponPrefChange);

consvar_t cv_kickstartaccel[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("kickstartaccel", "Off", CV_SAVE|CV_CALL, CV_OnOff, weaponPrefChange),
	CVAR_INIT ("kickstartaccel2", "Off", CV_SAVE|CV_CALL, CV_OnOff, weaponPrefChange2),
	CVAR_INIT ("kickstartaccel3", "Off", CV_SAVE|CV_CALL, CV_OnOff, weaponPrefChange3),
	CVAR_INIT ("kickstartaccel4", "Off", CV_SAVE|CV_CALL, CV_OnOff, weaponPrefChange4)
};

consvar_t cv_shrinkme[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("shrinkme", "Off", CV_CALL, CV_OnOff, weaponPrefChange),
	CVAR_INIT ("shrinkme2", "Off", CV_CALL, CV_OnOff, weaponPrefChange2),
	CVAR_INIT ("shrinkme3", "Off", CV_CALL, CV_OnOff, weaponPrefChange3),
	CVAR_INIT ("shrinkme4", "Off", CV_CALL, CV_OnOff, weaponPrefChange4)
};

static CV_PossibleValue_t driftjitter_cons_t[] = {{0, "SRB2Kart"}, {1, "Ring Racers"}, {0, NULL}};

// Drift jitter settings.
consvar_t cv_jitterlegacy[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("driftjitter", "Ring Racers", CV_SAVE|CV_CALL|CV_NOINIT, driftjitter_cons_t, weaponPrefChange),
	CVAR_INIT ("driftjitter2", "Ring Racers", CV_SAVE|CV_CALL|CV_NOINIT, driftjitter_cons_t, weaponPrefChange),
	CVAR_INIT ("driftjitter3", "Ring Racers", CV_SAVE|CV_CALL|CV_NOINIT, driftjitter_cons_t, weaponPrefChange),
	CVAR_INIT ("driftjitter4", "Ring Racers", CV_SAVE|CV_CALL|CV_NOINIT, driftjitter_cons_t, weaponPrefChange)
};


static CV_PossibleValue_t driftmode_cons_t[] = {{DRIFTMODE_CLASSIC, "Classic"}, {DRIFTMODE_SNAPSHOT, "Snapshot"}, {DRIFTMODE_INSTANT, "Instant"}, {0, NULL}};
consvar_t cv_driftmode[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("driftmode", "Classic", CV_SAVE|CV_CALL|CV_NOINIT, driftmode_cons_t, weaponPrefChange),
	CVAR_INIT ("driftmode2", "Classic", CV_SAVE|CV_CALL|CV_NOINIT, driftmode_cons_t, weaponPrefChange),
	CVAR_INIT ("driftmode3", "Classic", CV_SAVE|CV_CALL|CV_NOINIT, driftmode_cons_t, weaponPrefChange),
	CVAR_INIT ("driftmode4", "Classic", CV_SAVE|CV_CALL|CV_NOINIT, driftmode_cons_t, weaponPrefChange)
};

static CV_PossibleValue_t deadzone_cons_t[] = {{0, "MIN"}, {95*FRACUNIT/100, "MAX"}, {0, NULL}};
consvar_t cv_deadzonex[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("xdeadzone", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("xdeadzone2", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("xdeadzone3", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("xdeadzone4", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL)
};
consvar_t cv_deadzoney[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("ydeadzone", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("ydeadzone2", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("ydeadzone3", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("ydeadzone4", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL)
};

consvar_t cv_deadzonet[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("tdeadzone", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("tdeadzone2", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("tdeadzone3", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("tdeadzone4", "0.25", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL)
};

consvar_t cv_deadzonetwod[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("twoddeadzone", "0.8", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("twoddeadzone2", "0.8", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("twoddeadzone3", "0.8", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL),
	CVAR_INIT ("twoddeadzone4", "0.8", CV_FLOAT|CV_SAVE, deadzone_cons_t, NULL)
};

static CV_PossibleValue_t deadzonestyle_cons_t[] = {{0, "Kart"}, {1, "RR"}, {0, NULL}};
consvar_t cv_deadzonestyle[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("deadzonestyle", "Kart",  CV_SAVE, deadzonestyle_cons_t, NULL),
	CVAR_INIT ("deadzonestyle2", "Kart", CV_SAVE, deadzonestyle_cons_t, NULL),
	CVAR_INIT ("deadzonestyle3", "Kart", CV_SAVE, deadzonestyle_cons_t, NULL),
	CVAR_INIT ("deadzonestyle4", "Kart", CV_SAVE, deadzonestyle_cons_t, NULL)
};

static CV_PossibleValue_t freelookstyle_cons_t[] = {{FREELOOKSTYLE_OFF, "Off"}, {FREELOOKSTYLE_FLICK, "Flick Turn"}, {FREELOOKSTYLE_ROTATIONAL, "Rotational Turn"}, {0, NULL}};
consvar_t cv_freelookstyle[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("freelookstyle", "Flick Turn",  CV_SAVE, freelookstyle_cons_t, NULL),
	CVAR_INIT ("freelookstyle2", "Flick Turn", CV_SAVE, freelookstyle_cons_t, NULL),
	CVAR_INIT ("freelookstyle3", "Flick Turn", CV_SAVE, freelookstyle_cons_t, NULL),
	CVAR_INIT ("freelookstyle4", "Flick Turn", CV_SAVE, freelookstyle_cons_t, NULL)
};

consvar_t cv_freelookrequiresbutton[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("freelookrequiresbutton", "Off",  CV_SAVE, CV_YesNo, NULL),
	CVAR_INIT ("freelookrequiresbutton2", "Off", CV_SAVE, CV_YesNo, NULL),
	CVAR_INIT ("freelookrequiresbutton3", "Off", CV_SAVE, CV_YesNo, NULL),
	CVAR_INIT ("freelookrequiresbutton4", "Off", CV_SAVE, CV_YesNo, NULL)
};

// allows players to use restat (server toggle)
consvar_t cv_allowrestat = CVAR_INIT ("allowrestat", "Yes", CV_NETVAR, CV_YesNo, NULL);
consvar_t cv_notifyrestat = CVAR_INIT ("notifyrestat", "Yes", CV_NETVAR, CV_YesNo, NULL);

// now automatically allocated in D_RegisterClientCommands
// so that it doesn't have to be updated depending on the value of MAXPLAYERS
char player_names[MAXPLAYERS][MAXPLAYERNAME+1];
int32_t player_name_changes[MAXPLAYERS];

static recordpreset_t *G_GetRecordPreset(const char *name)
{
	for (recordpresetnum_e i = 0; i < numrecordpresets; i++)
		if (fastcmp(recordpresets[i]->name, name))
			return recordpresets[i];
	return NULL;
}

const char *G_GetRecordPresetName(const char *name)
{
	recordpreset_t *preset = G_GetRecordPreset(name);
	return preset ? preset->realname : NULL;
}

recordpreset_t *G_AddRecordPreset(const char *name, const char *realname)
{
	recordpresets = Z_Realloc(recordpresets, ++numrecordpresets * sizeof(recordpresets), PU_STATIC, NULL);
	recordpreset_t *new = Z_Malloc(sizeof(recordpreset_t), PU_STATIC, NULL);
	recordpresets[numrecordpresets - 1] = new;
	*new = (recordpreset_t){
		.name = {0},
		.realname = strdup(realname),
		.numversions = 0,
		.versions = NULL,
	};
	strlcpy(new->name, name, sizeof(new->name));
	return new;
}

strbuf_t *G_AddRecordPresetVersion(recordpreset_t *preset, uint8_t version)
{
	preset->versions = Z_Realloc(preset->versions, ++preset->numversions * sizeof(preset->versions), PU_STATIC, NULL);
	strbuf_t *pv = strbuf_alloc();
	preset->versions[preset->numversions - 1] = pv;
	strbuf_write(&pv, &version, 1);
	return pv;
}

strbuf_t *G_GetRecordPresetVersion(const char *name, uint8_t version)
{
	recordpreset_t *preset = G_GetRecordPreset(name);
	if (preset == NULL)
		return NULL;
	for (uint8_t i = 0; i < preset->numversions; i++)
		if (strbuf_byte(preset->versions[i], 0) == version)
			return preset->versions[i];
	return NULL;
}

void G_SetCurrentRecordPreset(recordpreset_t *preset)
{
	uint8_t highest = 0;
	for (uint8_t i = 0; i < preset->numversions; i++)
		highest = max(highest, strbuf_byte(preset->versions[i], 0));

	strcpy(currentrecordpreset, preset->name);
	currentrecordpresetversion = highest;
}

const char *G_CheckPresetCvars(strbuf_t *pv)
{
	for (consvar_t *cvar = consvar_vars; cvar; cvar = cvar->next)
	{
		if (!(cvar->flags & CV_GUARD))
			continue;

		// if the cvar is listed in the preset, its value must match
		const char *s = pv->buf + 1;
		while (s - pv->buf < strbuf_len(pv))
		{
			const char *name = s;
			s += strlen(s)+1;
			const char *value = s;
			s += strlen(s)+1;

			if (fastcmp(name, cvar->name))
			{
				if (fastcmp(value, cvar->string))
					goto next;
				else
					return cvar->name;
			}
		}

		// otherwise, the cvar must be set to its default value
		if (!fastcmp(cvar->string, cvar->defaultvalue))
			return cvar->name;
	next:;
	}

	return NULL;
}

void G_SetPresetCvars(strbuf_t *pv)
{
	CV_ResetGuardVars();

	// maximum memory efficiency, am i right?
	const char *s = pv->buf + 1;
	while (s - pv->buf < strbuf_len(pv))
	{
		consvar_t *cvar = CV_FindVar(s);
		s += strlen(s)+1;
		CV_Set(cvar, s);
		s += strlen(s)+1;
	}
}

maprecord_t *G_GetMapRecord(const char *mapname)
{
	for (size_t i = 0; i < nummaprecords; i++)
		if (fastcmp(maprecords[i]->name, mapname))
			return maprecords[i];
	return NULL;
}

maprecord_t *G_AllocateMapRecord(const char *mapname)
{
	maprecord_t *record = G_GetMapRecord(mapname);
	if (record != NULL)
		return record;

	maprecords = Z_Realloc(maprecords, sizeof(maprecords) * (nummaprecords+1), PU_STATIC, NULL);
	record = Z_Calloc(sizeof(maprecord_t), PU_STATIC, NULL);
	maprecords[nummaprecords++] = record;
	strlcpy(record->name, mapname, sizeof(record->name));
	return record;
}

maprecordpreset_t *G_GetMapRecordPreset(maprecord_t *record, const char *presetname)
{
	for (uint16_t k = 0; k < record->numpresets; k++)
		if (fastcmp(record->presets[k].prename, presetname))
			return &record->presets[k];
	return NULL;
}

maprecordpreset_t *G_AllocateMapRecordPreset(maprecord_t *record, const char *presetname, uint8_t version)
{
	maprecordpreset_t *preset = G_GetMapRecordPreset(record, presetname);
	if (preset != NULL)
	{
		preset->version = version; // upgrade (...or downgrade?)
		return preset;
	}

	record->presets = Z_Realloc(record->presets, sizeof(maprecordpreset_t) * ++record->numpresets, PU_STATIC, NULL);
	preset = &record->presets[record->numpresets - 1];
	*preset = (maprecordpreset_t){
		.prename = {0},
		.version = version,
		.bestlap = UINT32_MAX,
		.besttime = UINT32_MAX,
		.playtime = 0,
	};
	strlcpy(preset->prename, presetname, sizeof(preset->prename));
	return preset;
}

static void SetAltItemsForPreset(strbuf_t *preset, const char *value)
{
	for (uint8_t i = 0; i < KITEM_FIRSTFREESLOT; i++)
	{
		if (kartitems[i].altcvar != NULL)
		{
			strbuf_append(&preset, kartitems[i].altcvar->name);
			strbuf_append(&preset, value);
		}
	}
}

// MAKE SURE YOU SAVE DATA BEFORE CALLING THIS
void G_ClearRecords(void)
{
	for (size_t i = 0; i < nummaprecords; i++)
	{
		Z_Free(maprecords[i]->presets);
		Z_Free(maprecords[i]);
	}

	for (recordpresetnum_e i = 0; i < numrecordpresets; i++)
	{
		for (uint8_t j = 0; j < recordpresets[i]->numversions; j++)
			Z_Free(recordpresets[i]->versions[j]);
		free(recordpresets[i]->realname);
		Z_Free(recordpresets[i]->versions);
	}

	nummaprecords = numrecordpresets = 0;

	// initialize default record presets
	// TODO: SOC this shit
	recordpreset_t *preset;
	strbuf_t *pv;

	preset = G_AddRecordPreset("kart", "SRB2Kart Mode");

	pv = G_AddRecordPresetVersion(preset, 1);

	strbuf_append(&pv, "kartbumpspark");
	strbuf_append(&pv, "Off");
	strbuf_append(&pv, "kartbumpspring");
	strbuf_append(&pv, "No");

	strbuf_append(&pv, "kartbumpspark");
	strbuf_append(&pv, "Off");
	strbuf_append(&pv, "kartbumpspring");
	strbuf_append(&pv, "No");
	strbuf_append(&pv, "rockshove");
	strbuf_append(&pv, "Off");

	preset = G_AddRecordPreset("tech", "Tech Mode");

	pv = G_AddRecordPresetVersion(preset, 1);
	strbuf_append(&pv, "kartstacking");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartchaining");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartslopeboost");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartbumpspark");
	strbuf_append(&pv, "Off");
	strbuf_append(&pv, "kartbumpspring");
	strbuf_append(&pv, "No");

	pv = G_AddRecordPresetVersion(preset, 2);
	strbuf_append(&pv, "kartstacking");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartchaining");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartslopeboost");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartairdrop");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartbumpspark");
	strbuf_append(&pv, "On");
	strbuf_append(&pv, "kartbumpspring");
	strbuf_append(&pv, "Yes");

	pv = G_AddRecordPresetVersion(preset, 3);
	strbuf_append(&pv, "kartstacking");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartchaining");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartslopeboost");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartairdrop");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartbumpspark");
	strbuf_append(&pv, "On");
	strbuf_append(&pv, "kartbumpspring");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "rockshove");
	strbuf_append(&pv, "Off");

	preset = G_AddRecordPreset("blankart", "BlanKart Mode");

	pv = G_AddRecordPresetVersion(preset, 1);
	strbuf_append(&pv, "kartrings");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartstacking");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartchaining");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartslipdash");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartpurpledrift");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartslopeboost");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartbumpspark");
	strbuf_append(&pv, "Off");
	strbuf_append(&pv, "kartbumpspring");
	strbuf_append(&pv, "No");

	pv = G_AddRecordPresetVersion(preset, 2);
	strbuf_append(&pv, "kartrings");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartstacking");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartchaining");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartslipdash");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartpurpledrift");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartslopeboost");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartairdrop");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartbumpspark");
	strbuf_append(&pv, "Remove Charge Only");
	strbuf_append(&pv, "kartbumpspring");
	strbuf_append(&pv, "Yes");
	
	pv = G_AddRecordPresetVersion(preset, 3);
	strbuf_append(&pv, "kartrings");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartpurpledrift");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartslipdash");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartstacking");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartchaining");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartdrafting");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartrecoverydash");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartairthrust");
	strbuf_append(&pv, "Flash");
	strbuf_append(&pv, "kartairdrop");
	strbuf_append(&pv, "Fusion");
	strbuf_append(&pv, "kartslopeboost");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartbumpspark");
	strbuf_append(&pv, "Remove Charge Only");
	strbuf_append(&pv, "kartbumpspring");
	strbuf_append(&pv, "Yes");
	strbuf_append(&pv, "kartkeepstuff");
	strbuf_append(&pv, "Yes");
	SetAltItemsForPreset(pv, "Alternative");
}

// For easy retrieval of records
// TODO: should this work for unloaded maps?
tic_t G_GetBestTime(mapnum_t map)
{
	const char *mapname = G_BuildMapName(map + 1);
	tic_t besttime = UINT32_MAX;

	if (mapname == NULL)
		return besttime;

	maprecord_t *record = G_GetMapRecord(mapname);
	if (record == NULL)
		return besttime;

	for (uint16_t k = 0; k < record->numpresets; k++)
	{
		if (!fastcmp(record->presets[k].prename, "kart"))
			continue; // doesn't count
		if (record->presets[k].besttime < besttime)
			besttime = record->presets[k].besttime;
	}

	return besttime;
}

dboolean G_CompareRecordPresetVersions(strbuf_t *pre1, strbuf_t *pre2)
{
	return strbuf_cmp(pre1, pre2);
}

dboolean G_EmblemsEnabled(void)
{
	return !demo.playback && fastcmp(currentrecordpreset, "kart");
}

char *G_GetRecordReplayFolder(dboolean home, dboolean breaker)
{
	return strdup(va("%s%smedia"PATHSEP"replay"PATHSEP"%s"PATHSEP"%s",
		home ? srb2home : "",
		home ? PATHSEP : "",
		breaker ? "itembreaker" : "timeattack",
		currentrecordpreset
	));
}

const char *replaysuffixes[] = {
	[REPLAY_BESTTIME] = "time-best",
	[REPLAY_BESTLAP] = "lap-best",
	[REPLAY_LAST] = "last",
	[REPLAY_GUEST] = "guest",
};

char *G_GetRecordReplay(const char *folder, mapnum_t mapnum, uint16_t skinnum, recordreplay_e which)
{
	const char *map = G_BuildMapName(mapnum), *suf = replaysuffixes[which];
	if (which != REPLAY_GUEST)
		return strdup(va("%s"PATHSEP"%s-%s-%s.lmp", folder, map, skins[skinnum]->name, suf));
	else
		return strdup(va("%s"PATHSEP"%s-%s.lmp", folder, map, suf));
}

dboolean G_CheckRecordReplay(const char *folder, mapnum_t mapnum, uint16_t skinnum, recordreplay_e which)
{
	const char *map = G_BuildMapName(mapnum), *suf = replaysuffixes[which];
	if (which != REPLAY_GUEST)
		return FIL_FileExists(va("%s"PATHSEP"%s-%s-%s.lmp", folder, map, skins[skinnum]->name, suf));
	else
		return FIL_FileExists(va("%s"PATHSEP"%s-%s.lmp", folder, map, suf));
}

//
// G_UpdateRecordReplays
//
// Update replay files/data, etc. for Record Attack
//
static void G_UpdateRecordReplays(void)
{
	CLEANUP(pfree) char *gpath = NULL, *lastdemo = NULL;
	uint8_t earnedEmblems;

	// Record new best time
	maprecord_t *record = G_AllocateMapRecord(G_BuildMapName(gamemap));
	maprecordpreset_t *preset = G_AllocateMapRecordPreset(record, currentrecordpreset, currentrecordpresetversion);

	if (players[consoleplayer].pflags & PF_NOCONTEST)
	{
		players[consoleplayer].realtime = UINT32_MAX;
	}

	if ((preset->besttime == UINT32_MAX || players[consoleplayer].realtime < preset->besttime)
		&& (players[consoleplayer].realtime < UINT32_MAX)) // DNF
	{
		preset->besttime = players[consoleplayer].realtime;
	}

	if (modeattacking == ATTACKING_TIME)
	{
		if (preset->bestlap == UINT32_MAX || players[consoleplayer].laptime[LAP_BEST] < preset->bestlap)
			preset->bestlap = players[consoleplayer].laptime[LAP_BEST];
	}
	else
	{
		preset->bestlap = UINT32_MAX;
	}

	// Save demo!
	G_SetDemoTime(players[consoleplayer].realtime, players[consoleplayer].laptime[LAP_BEST]);
	G_CheckDemoStatus();

	gpath = G_GetRecordReplayFolder(true, modeattacking == ATTACKING_ITEMBREAK);
	M_MkdirEach(gpath, M_PathParts(gpath) - 4, 0755);

	lastdemo = G_GetRecordReplay(gpath, gamemap, cv_chooseskin.value, REPLAY_LAST);

	if (FIL_FileExists(lastdemo))
	{
		uint8_t *buf;
		size_t len = FIL_ReadFile(lastdemo, &buf);

		CLEANUP(pfree) char *bestdemo = G_GetRecordReplay(gpath, gamemap, cv_chooseskin.value, REPLAY_BESTTIME);
		if (!FIL_FileExists(bestdemo) || G_CmpDemoTime(bestdemo, lastdemo) & 1)
		{ // Better time, save this demo.
			if (FIL_FileExists(bestdemo))
				remove(bestdemo);
			FIL_WriteFile(bestdemo, buf, len);
			CONS_Printf("\x83%s\x80 %s '%s'\n", M_GetText("NEW RECORD TIME!"), M_GetText("Saved replay as"), bestdemo);
		}

		if (modeattacking == ATTACKING_TIME)
		{
			free(bestdemo);
			bestdemo = G_GetRecordReplay(gpath, gamemap, cv_chooseskin.value, REPLAY_BESTLAP);
			if (!FIL_FileExists(bestdemo) || G_CmpDemoTime(bestdemo, lastdemo) & (1<<1))
			{ // Better lap time, save this demo.
				if (FIL_FileExists(bestdemo))
					remove(bestdemo);
				FIL_WriteFile(bestdemo, buf, len);
				CONS_Printf("\x83%s\x80 %s '%s'\n", M_GetText("NEW RECORD LAP!"), M_GetText("Saved replay as"), bestdemo);
			}
		}

		//CONS_Printf("%s '%s'\n", M_GetText("Saved replay as"), lastdemo);

		Z_Free(buf);
	}

	// Check emblems when level data is updated
	if ((earnedEmblems = M_CheckLevelEmblems()))
		CONS_Printf(M_GetText("\x82" "Earned %hu medal%s for Record Attack records.\n"), (uint16_t)earnedEmblems, earnedEmblems > 1 ? "s" : "");

	if (M_UpdateUnlockablesAndExtraEmblems())
		S_StartSound(NULL, sfx_ncitem);

	// SRB2Kart - save here so you NEVER lose your earned times/medals.
	G_SaveGameData();
}

// kinda hacky way to do this, but this sets the game to use a seperate savefile if you have addons loaded
static void G_SetSaveGameModified(void)
{
	size_t filenamelen;

	if (savemoddata)
		return;

	// save vanilla data just to be sure
	G_SaveGameData();

	savemoddata = true;

	strlcpy(gamedatafilename, "modblandata.dat", sizeof (gamedatafilename));
	strlwr(gamedatafilename);

	// Also save a time attack folder
	filenamelen = strlen(gamedatafilename)-4;  // Strip off the extension
	filenamelen = min(filenamelen, sizeof (timeattackfolder));
	memcpy(timeattackfolder, gamedatafilename, filenamelen);
	timeattackfolder[min(filenamelen, sizeof (timeattackfolder) - 1)] = '\0';

	strcpy(savegamename, timeattackfolder);
	strlcat(savegamename, "%u.ssg", sizeof(savegamename));
	// can't use snprintf since there is %u in savegamename
	strcatbf(savegamename, srb2home, PATHSEP);

	G_LoadGameData();

	// unlock EVERYTHING.
	for (uint8_t i = 0; i < MAXUNLOCKABLES; i++)
	{
		if (!unlockables[i].conditionset)
			continue;
		if (!unlockables[i].unlocked)
		{
			unlockables[i].unlocked = true;
		}
	}
}

// for consistency among messages: this modifies the game and removes savemoddata.
void G_SetGameModified(dboolean silent, dboolean major)
{
	if ((majormods && modifiedgame) || (refreshdirmenu & REFRESHDIR_GAMEDATA)) // new gamedata amnesty?
		return;

	modifiedgame = true;

	if (!major)
		return;

	//savemoddata = false; -- there is literally no reason to do this anymore.
	majormods = true;

	// should this only be done when you load a "major" gameplay modifieng addon?
	G_SetSaveGameModified();

	if (!silent)
		CONS_Alert(CONS_NOTICE, M_GetText("Record Attack data will be saved to seperate save.\n"));

	// If in record attack recording, cancel it.
	if (gamestate != GS_MENU)
	{
		if (modeattacking)
			MR_ModeAttackEndGame(0);
		else if (marathonmode)
			Command_ExitGame_f();
	}
}

/** Builds an original game map name from a map number.
  *
  * \param map Map number.
  * \return Pointer to a static buffer containing the desired map name.
  * \sa G_MapNumber
  */
const char *G_BuildMapName(mapnum_t map)
{
	if (map > 0 && map <= nummapheaders && mapheaderinfo[map - 1] != NULL)
	{
		return mapheaderinfo[map - 1]->lumpname;
	}
	else
	{
		return NULL;
	}
}

/** Returns the map number for map lump name.
  *
  * \param name Map name;
  * \return Map number.
  * \sa G_BuildMapName, nextmapspecial_t
  */
mapnum_t G_MapNumber(const char * name)
{
#ifdef NEXTMAPINSOC
	if (strncasecmp("NEXTMAP_", name, 8) != 0)
#endif
	{
		mapnum_t map;
		uint32_t hash = FNV1a_QuickCaseHash(name, MAXMAPLUMPNAME);

		for (map = 0; map < nummapheaders; ++map)
		{
			if (hash != mapheaderinfo[map]->lumpnamehash)
				continue;

			if (strcasecmp(mapheaderinfo[map]->lumpname, name) != 0)
				continue;

			return map;
		}

		return NEXTMAP_INVALID;
	}

#ifdef NEXTMAPINSOC
	name += 8;

	if (strcasecmp("EVALUATION", name) == 0)
		return NEXTMAP_EVALUATION;
	if (strcasecmp("CREDITS", name) == 0)
		return NEXTMAP_CREDITS;
	//if (strcasecmp("CEREMONY", name) == 0)
		//return NEXTMAP_CEREMONY;
	//if (strcasecmp("TITLE", name) == 0)
		return NEXTMAP_TITLE;
#endif
}

/** Returns the map number from level title.
  *
  * \param name Map name;
  * \return Map number.
  * \sa G_BuildMapName, nextmapspecial_t
  */
mapnum_t G_LevelTitleToMapNum(const char * leveltitle)
{
	mapnum_t  map;
	char levelname[48];

	for (map = 0; map < nummapheaders; ++map)
	{
		snprintf(levelname, sizeof(levelname), "%s", mapheaderinfo[map]->lvlttl);
		if (!strcasecmp(leveltitle, levelname))
		{
			return map;
		}

		snprintf(levelname, sizeof(levelname), "%s %s", mapheaderinfo[map]->lvlttl, mapheaderinfo[map]->actnum);
		if (!strcasecmp(leveltitle, levelname))
		{
			return map;
		}

		snprintf(levelname, sizeof(levelname), "%s %s", mapheaderinfo[map]->lvlttl, mapheaderinfo[map]->zonttl);
		if (!strcasecmp(leveltitle, levelname))
		{
			return map;
		}

		snprintf(levelname, sizeof(levelname), "%s %s %s", mapheaderinfo[map]->lvlttl, mapheaderinfo[map]->zonttl, mapheaderinfo[map]->actnum);
		if (!strcasecmp(leveltitle, levelname))
		{
			return map;
		}

	}
	return NEXTMAP_INVALID;
}

// convert kart map number to native map number
mapnum_t G_KartMapToNative(mapnum_t mapnum)
{
	if (mapnum > 0 && mapnum < NEXTMAP_SPECIAL)
		return kartmap2native[mapnum-1];
	return 0;
}

// convert native map number to kart
mapnum_t G_NativeMapToKart(mapnum_t mapnum)
{
	if (mapnum > 0 && mapnum < NEXTMAP_SPECIAL)
		return nativemap2kart[mapnum-1]+1;
	return 0;
}

/** Clips the console player's mouse aiming to the current view.
  * Used whenever the player view is changed manually.
  *
  * \param aiming Pointer to the vertical angle to clip.
  * \return The clipped angle.
  */
int32_t G_ClipAimingPitch(int32_t *aiming)
{
	int32_t limitangle;

	limitangle = ANGLE_90 - 1;

	if (*aiming > limitangle)
		*aiming = limitangle;
	else if (*aiming < -limitangle)
		*aiming = -limitangle;

	return (*aiming);
}

int16_t G_SoftwareClipAimingPitch(int32_t *aiming)
{
	int32_t limitangle;

	// note: the current software mode implementation doesn't have true perspective
	limitangle = ANGLE_90 - ANG10; // Some viewing fun, but not too far down...

	if (*aiming > limitangle)
		*aiming = limitangle;
	else if (*aiming < -limitangle)
		*aiming = -limitangle;

	return (int16_t)((*aiming)>>16);
}

void G_FinalClipAimingPitch(int32_t *aiming, player_t *player, dboolean skybox)
{
#ifndef HWRENDER
	(void)player;
	(void)skybox;
#endif

	// clip it in the case we are looking a hardware 90 degrees full aiming
	// (lmps, network and use F12...)
	if (rendermode == render_soft
#ifdef HWRENDER
		|| (rendermode == render_opengl
			&& (cv_glshearing.value == 1
			|| (cv_glshearing.value == 2 && R_IsViewpointThirdPerson(player, skybox))))
#endif
		)
	{
		G_SoftwareClipAimingPitch(aiming);
	}
	else
	{
		G_ClipAimingPitch(aiming);
	}
}

// returns true if event's axis is within the deadzone for the given player
dboolean G_AxisInDeadzone(uint8_t p, event_t *ev)
{
	fixed_t deadzonetype = G_GetDeadZoneType(p, G_GetAxisTypeForData1(ev->data1));
	return abs(ev->data2) <= (JOYAXISRANGE * deadzonetype) / FRACUNIT;
}

// check if the given key is bound to the given gamecontrol
// if defaults is true, also check default controls if the gamecontrol has no binds
dboolean G_ControlBoundToKey(uint8_t p, int32_t gc, int32_t key, dboolean defaults)
{
	int32_t deviceID;
	int32_t i;
	int32_t (*map)[MAXINPUTMAPPING] = &gamecontrol[p][gc];

	if (key <= 0 || key >= NUMINPUTS)
		return false;

	if (defaults)
	{
		deviceID = I_GetControllerSlotfromID(I_GetControllerIDForPlayer(p));
		for (i = 0; i < MAXINPUTMAPPING; i++)
			if (G_KeyIsAvailable((*map)[i], deviceID, false))
				goto bound;

		map = &gamecontroldefault[gc];
	}
bound:

	for (i = 0; i < MAXINPUTMAPPING; i++)
		if ((*map)[i] == key)
			return true;

	return false;
}

int8_t G_GetAxisTypeForData1(int8_t data1)
{
	switch (data1)
	{
		case 0:
		case 2:
			return DEADZONE_X;
			break;
		case 1:
		case 3:
			return DEADZONE_Y;
			break;
		case 4:
		case 5:
		default:
			return DEADZONE_BUTTON;
			break;
	}
}

fixed_t G_GetDeadZoneType(int32_t p, int8_t type)
{
	switch (type)
	{
		case DEADZONE_X:
			return cv_deadzonex[p].value;
			break;
		case DEADZONE_Y:
			return cv_deadzoney[p].value;
			break;
		case DEADZONE_2D:
			return cv_deadzonetwod[p].value;
			break;
		case DEADZONE_NONE:
			return 0;
			break;
		case DEADZONE_BUTTON:
		default:
			return cv_deadzonet[p].value;
			break;
	}
}

dboolean keyboardcontrolled = false;

int32_t G_PlayerInputAnalog(uint8_t p, int32_t gc, dboolean digital, int8_t type)
{
	int32_t deviceID;
	int32_t i;
	int32_t deadzone = 0;
	int32_t value = 0;

	if (p >= MAXSPLITSCREENPLAYERS)
	{
#ifdef PARANOIA
		CONS_Debug(DBG_GAMELOGIC, "G_PlayerInputAnalog: Invalid player ID %d\n", p);
#endif
		return 0;
	}

	fixed_t deadzonetype = G_GetDeadZoneType(p, type);

	deadzone = (JOYAXISRANGE * deadzonetype) / FRACUNIT;

	deviceID = I_GetControllerSlotfromID(I_GetControllerIDForPlayer(p));
	if (deviceID >= MAXDEVICES)
		return 0;

	// not a controller?
	// then its a keyboard or mouse
	if (deviceID == INVALID_DEVICE)
		deviceID = KEYBOARD_MOUSE_DEVICE;

retrygetcontrol:
	for (i = 0; i < MAXINPUTMAPPING; i++)
	{
		int32_t key = gamecontrol[p][gc][i];
		int32_t keyvalue = 0;

		// Invalid key number.
		if (!G_KeyIsAvailable(key, deviceID, digital))
		{
			continue;
		}

		keyvalue = gamekeydown[deviceID][key];
		if (abs(keyvalue) > abs(value))
		{
			value = keyvalue;
		}
	}

	// If you're on controller, try your keyboard-based binds as an immediate backup.
	if (deviceID > 0)
	{
		deviceID = 0;
		goto retrygetcontrol;
	}
	
	if (value > deadzone)
	{
		return value;
	}
	return 0;
}

vector3_t G_PlayerInputSensor(uint8_t p, motionsensortype_e sensor)
{
	vector3_t out = {0};
	int32_t deviceID;

	if (p >= MAXSPLITSCREENPLAYERS)
	{
#ifdef PARANOIA
		CONS_Debug(DBG_GAMELOGIC, "G_PlayerInputAnalog: Invalid player ID %d\n", p);
#endif
		return out;
	}
	
	deviceID = I_GetControllerSlotfromID(I_GetControllerIDForPlayer(p));
	if (deviceID >= MAXDEVICES)
		return out;

	if (deviceID == INVALID_DEVICE || deviceID == KEYBOARD_MOUSE_DEVICE)
		return out;
	
	switch (sensor)
	{
		case ACCELEROMETER:
			FV3_Load(&out,
				gamekeydown[deviceID][KEY_ACCELEROMETER1+0],
				gamekeydown[deviceID][KEY_ACCELEROMETER1+1],
				gamekeydown[deviceID][KEY_ACCELEROMETER1+2]
			);
			break;
		case GYROSCOPE:
			FV3_Load(&out,
				gamekeydown[deviceID][KEY_GYROSCOPE1+0],
				gamekeydown[deviceID][KEY_GYROSCOPE1+1],
				gamekeydown[deviceID][KEY_GYROSCOPE1+2]
			);
			break;
	}
	return out;
}

dboolean G_PlayerInputDown(uint8_t p, int32_t gc, dboolean digital, int8_t type)
{
	return (abs(G_PlayerInputAnalog(p, gc, digital, type)) >= JOYAXISRANGE/2);
}

// Take a magnitude of two axes, and adjust it to take out the deadzone
// Will return a value between 0 and JOYAXISRANGE
static int32_t G_BasicDeadZoneCalculation(int32_t magnitude, fixed_t deadZone)
{
	const int32_t jdeadzone = (JOYAXISRANGE * deadZone) / FRACUNIT;
	int32_t deadzoneAppliedValue = 0;
	int32_t adjustedMagnitude = abs(magnitude);

	if (jdeadzone >= JOYAXISRANGE && adjustedMagnitude >= JOYAXISRANGE) // If the deadzone and magnitude are both 100%...
		return JOYAXISRANGE; // ...return 100% input directly, to avoid dividing by 0
	else if (adjustedMagnitude > jdeadzone) // Otherwise, calculate how much the magnitude exceeds the deadzone
	{
		adjustedMagnitude = min(adjustedMagnitude, JOYAXISRANGE);

		adjustedMagnitude -= jdeadzone;

		deadzoneAppliedValue = (adjustedMagnitude * JOYAXISRANGE) / (JOYAXISRANGE - jdeadzone);
	}

	return deadzoneAppliedValue;
}

// Get the actual sensible radial value for a joystick axis when accounting for a deadzone
static void G_HandleAxisDeadZone(uint8_t splitnum, joystickvector2_t *joystickvector)
{
	int32_t gamepadStyle = Joystick[splitnum].bGamepadStyle;
	fixed_t deadZoneX = cv_deadzonex[splitnum].value;
	fixed_t deadZoneY = cv_deadzoney[splitnum].value;
	int8_t deadZoneStyle = cv_deadzonestyle[splitnum].value;

	// When gamepadstyle is "true" the values are just -1, 0, or 1. This is done in the interface code.

	// v1 style deadzone == (deadZoneStyle == 0)
	// Deadzone doesn't scale so shallower angles are easier to hit.

	// RR style deadzone
	// Dead zone scales so shallower angles are harder to hit but you have more range.
	if (!gamepadStyle && (deadZoneStyle == 1))
	{
		// Get the total magnitude of the 2 axes
		int32_t magnitude = (joystickvector->xaxis * joystickvector->xaxis) + (joystickvector->yaxis * joystickvector->yaxis);
		int32_t normalisedXAxis;
		int32_t normalisedYAxis;
		int32_t normalisedMagnitudeX;
		int32_t normalisedMagnitudeY;
		double dMagnitude = sqrt((double)magnitude);
		magnitude = (int32_t)dMagnitude;

		// Get the normalised xy values from the magnitude
		normalisedXAxis = (joystickvector->xaxis * magnitude) / JOYAXISRANGE;
		normalisedYAxis = (joystickvector->yaxis * magnitude) / JOYAXISRANGE;

		// Apply the deadzone to the magnitude to give a correct value between 0 and JOYAXISRANGE
		normalisedMagnitudeX = G_BasicDeadZoneCalculation(magnitude, deadZoneX);
		normalisedMagnitudeY = G_BasicDeadZoneCalculation(magnitude, deadZoneY);

		// Apply the deadzone to the xy axes
		joystickvector->xaxis = (normalisedXAxis * normalisedMagnitudeX) / JOYAXISRANGE;
		joystickvector->yaxis = (normalisedYAxis * normalisedMagnitudeY) / JOYAXISRANGE;

		// Cap the values so they don't go above the correct maximum
		joystickvector->xaxis = min(joystickvector->xaxis, JOYAXISRANGE);
		joystickvector->xaxis = max(joystickvector->xaxis, -JOYAXISRANGE);
		joystickvector->yaxis = min(joystickvector->yaxis, JOYAXISRANGE);
		joystickvector->yaxis = max(joystickvector->yaxis, -JOYAXISRANGE);
	}
}

//
// G_BuildTiccmd
// Builds a ticcmd from all of the available inputs
// or reads it from the demo buffer.
// If recording a demo, write it out
//
//
int32_t localaiming[MAXSPLITSCREENPLAYERS];
angle_t localangle[MAXSPLITSCREENPLAYERS];

static void G_DoCameraTurn(ticcmd_t *cmd, int32_t realtics, uint8_t ssplayer, player_t *player)
{
	uint8_t viewnum = G_PartyPosition(g_localplayers[ssplayer-1]);

	if (player->mo)
		cmd->angle = K_GetKartTurnValue(player, cmd->angle);

	cmd->angle *= realtics;

	if (P_CanPlayerTurn(player, cmd))
		localangle[viewnum] += (cmd->angle<<TICCMD_REDUCE);

	cmd->angle = (int16_t)(localangle[viewnum] >> TICCMD_REDUCE);
}

void G_BuildTiccmd(ticcmd_t *cmd, int32_t realtics, uint8_t ssplayer)
{
	const uint8_t forplayer = ssplayer-1;

	static int32_t turnheld[MAXSPLITSCREENPLAYERS]; // for accelerative turning
	static dboolean resetdown[MAXSPLITSCREENPLAYERS]; // don't cam reset every frame

	int32_t forward, side, tspeed;

	joystickvector2_t joystickvector;
	int32_t accelerometertilt;

	// you'd BETTER not touch the player while freecamming...
	player_t *player = &players[g_localplayers[forplayer]];
	camera_t *thiscam = &camera[forplayer];
	dboolean *rd = &resetdown[forplayer];
	dboolean spectating = player->spectator || thiscam->freecam;
	// Is there any reason this can't just be I_BaseTiccmd?
	switch (ssplayer)
	{
		case 2:
			G_CopyTiccmd(cmd, I_BaseTiccmd2(), 1);
			break;
		case 3:
			G_CopyTiccmd(cmd, I_BaseTiccmd3(), 1);
			break;
		case 4:
			G_CopyTiccmd(cmd, I_BaseTiccmd4(), 1);
			break;
		case 1:
		default:
			G_CopyTiccmd(cmd, I_BaseTiccmd(), 1); // empty, or external driver
			break;
	}

	// update our gamepad gravity when applicable
	// this should always execute so we have an accurate state
	if (G_GetGamepadCanUseTilt(forplayer))
	{
		vector3_t accel = G_PlayerInputSensor(forplayer, ACCELEROMETER);
		vector3_t gyro = G_PlayerInputSensor(forplayer, GYROSCOPE);

		G_UpdateGamepadAutoCalibration(forplayer, accel, gyro,
			(menustack[0] == MN_OP_CONTROLSETUP));

		G_UpdateGamepadGyro(forplayer, gyro);
		G_UpdateGamepadGravity(forplayer, G_GetGamepadCalibratedGyro(forplayer), accel);
	}

	// why build a ticcmd if we're paused?
	// Or, for that matter, if we're being reborn.
	if (!thiscam->freecam && (paused || P_AutoPause() || (gamestate == GS_LEVEL && player->playerstate == PST_REBORN)))
	{
		return;
	}

	if (K_PlayerUsesBotMovement(player))
	{
		// Bot ticcmd is generated by K_BuildBotTiccmd
		return;
	}

	// This is what RR does so I guess its fine???
	if (menustack[0] || chat_on || CON_Ready())
	{
		cmd->flags |= TICCMD_TYPING;

		if (hu_keystrokes)
		{
			cmd->flags |= TICCMD_KEYSTROKE;
		}

		if (!(spectating || objectplacing))
		{
			if (player->sneakertimer || player->recoverydash)
				cmd->forwardmove = MAXPLMOVE;	// 50
		}

		G_DoCameraTurn(cmd, realtics, ssplayer, player);

		return;
	}

	joystickvector.xaxis = G_PlayerInputAnalog(forplayer, gc_turnright, false, DEADZONE_X) - G_PlayerInputAnalog(forplayer, gc_turnleft, false, DEADZONE_X);
	joystickvector.yaxis = 0;
	G_HandleAxisDeadZone(forplayer, &joystickvector);

	// tilt control never has deadzone
	// if stick input is used gradually cancel out tilt based on stick intensity
	if (cv_tiltcontrol[forplayer].value == TILTCONTROL_STEER || cv_tiltcontrol[forplayer].value == TILTCONTROL_ON)
	{
		fixed_t rate = FixedDiv(abs(joystickvector.xaxis), JOYAXISRANGE);
		accelerometertilt = 
			(FixedMul(rate, joystickvector.xaxis*FRACUNIT)/FRACUNIT) +
			(FixedMul(FRACUNIT-rate, G_GetGamepadTilt(forplayer)*FRACUNIT)/FRACUNIT);
	}

	// For kart, I've turned the aim axis into a digital axis because we only
	// use it for aiming to throw items forward/backward and the vote screen
	// This mean that the turn axis will still be gradient but up/down will be 0
	// until the stick is pushed far enough
	//
	joystickvector.yaxis = G_PlayerInputAnalog(forplayer, gc_aimbackward, false, DEADZONE_Y) - G_PlayerInputAnalog(forplayer, gc_aimforward, false, DEADZONE_Y);

	if (encoremode)
	{
		joystickvector.xaxis = -joystickvector.xaxis;
		accelerometertilt = -accelerometertilt;
	}

	forward = side = 0;
	if ((cv_tiltcontrol[forplayer].value == TILTCONTROL_STEER || cv_tiltcontrol[forplayer].value == TILTCONTROL_ON) && (!spectating))
	{
		tspeed = accelerometertilt;
	}
	else
	{
		tspeed = joystickvector.xaxis;
	}

	// use two stage accelerative turning
	// on the keyboard and (NOT!) joystick
	if (joystickvector.xaxis != 0)
	{
		turnheld[forplayer] += realtics;

		if (turnheld[forplayer] < cv_turnsmooth[forplayer].value * 3)
		{
			// check turn input again, but this time digital only
			// if it's false, an analog stick is inputting the turn; no smoothing!
			if (G_PlayerInputDown(forplayer, gc_turnleft, true, DEADZONE_X) || G_PlayerInputDown(forplayer, gc_turnright, true, DEADZONE_X))
			{
				I_Assert(cv_turnsmooth[forplayer].value);
				tspeed /= cv_turnsmooth[forplayer].value * 2;
			}
		}
	}
	else
	{
		turnheld[forplayer] = 0;
	}

	cmd->turning = 0;

	if ((!spectating) && (cv_tiltcontrol[forplayer].value == TILTCONTROL_STEER || cv_tiltcontrol[forplayer].value == TILTCONTROL_ON))
	{
		cmd->turning -= (tspeed * KART_FULLTURN) / JOYAXISRANGE;
		cmd->angle -= (tspeed * KART_FULLTURN) / JOYAXISRANGE;
		side += (accelerometertilt * 4) / JOYAXISRANGE;
	}
	else if (joystickvector.xaxis != 0)
	{
		cmd->turning -= (tspeed * KART_FULLTURN) / JOYAXISRANGE;
		cmd->angle -= (tspeed * KART_FULLTURN) / JOYAXISRANGE;
		side += (joystickvector.xaxis * 4) / JOYAXISRANGE;
	}

	// Specator mouse turning
	if (spectating)
	{
		int32_t mousex = gamekeydown[0][KEY_MOUSEMOVE+3] - gamekeydown[0][KEY_MOUSEMOVE+2];
		cmd->angle -= (mousex * 8) * (encoremode ? -1 : 1);
	}

	//control spectator camera using the gyro
	// player space gyro from http://gyrowiki.jibbsmart.com/blog:player-space-gyro-and-alternatives-explained
	if (spectating && (cv_tiltcontrol[forplayer].value == TILTCONTROL_CAME || cv_tiltcontrol[forplayer].value == TILTCONTROL_ON))
	{
		fixed_t deltaseconds = FixedDiv(FRACUNIT, max(cv_timescale.value, FRACUNIT/20))/TICRATE;
		fixed_t yawRelaxFactor = 141*FRACUNIT/100;
		vector3_t gyro = G_GetGamepadCalibratedGyro(forplayer);
		vector3_t gravnorm = G_GetGamepadGravity(forplayer);
		vector2_t gyroYZ = {gyro.y, gyro.z};

		// use world yaw for yaw direction, local combined yaw for magnitude
		FV3_Normalize(&gravnorm);
		fixed_t worldYaw = FixedMul(gyro.y, gravnorm.y) + FixedMul(gyro.z, gravnorm.z); // dot product but just yaw and roll

		fixed_t yaw = intsign(worldYaw) * (encoremode ? -1 : 1) *
			min(FixedMul(-abs(worldYaw), yawRelaxFactor), FV2_Magnitude(&gyroYZ));

		cmd->angle -= FixedMul(yaw, deltaseconds*180)/FRACUNIT;
		cmd->aiming -= FixedMul(gyro.x, deltaseconds*180)/FRACUNIT;
	}

	// Digital users can input diagonal-back for shallow turns.
	//
	// There's probably some principled way of doing this in the gamepad handler itself,
	// by only applying this filtering to inputs sourced from an axis. This is a little
	// ugly with the current abstractions, though, and there's a fortunate trick here:
	// if you can input full strength turns on both axes, either you're using a fucking
	// square gate, or you're not on an analog device.
	if (cv_litesteer[forplayer].value && joystickvector.yaxis >= JOYAXISRANGE && abs(cmd->angle) == KART_FULLTURN)
	{
		// >= beacuse some analog devices can go past JOYAXISRANGE (?!)
		cmd->turning /= 2;	// mine: make this show up in the input disp (we respect tilt control why not this too)
		cmd->angle /= 2;
	}

	if (spectating || objectplacing) // SRB2Kart: spectators need special controls
	{
		if (G_PlayerInputDown(forplayer, gc_accelerate, false, DEADZONE_BUTTON))
		{
			cmd->buttons |= BT_ACCELERATE;
		}

		if (G_PlayerInputDown(forplayer, gc_brake, false, DEADZONE_BUTTON))
		{
			cmd->buttons |= BT_BRAKE;
		}

		if (joystickvector.yaxis < 0)
		{
			forward += MAXPLMOVE;
		}

		if (joystickvector.yaxis > 0)
		{
			forward -= MAXPLMOVE;
		}
	}
	else
	{
		// forward with key or button // SRB2kart - we use an accel/brake instead of forward/backward.
		fixed_t value = G_PlayerInputAnalog(forplayer, gc_accelerate, false, DEADZONE_BUTTON);
		if (value != 0)
		{
			cmd->buttons |= BT_ACCELERATE;
			forward += (value * MAXPLMOVE) / JOYAXISRANGE;
		}

		if (player->sneakertimer || player->recoverydash)
			forward = MAXPLMOVE;	// 50

		value = G_PlayerInputAnalog(forplayer, gc_brake, false, DEADZONE_BUTTON);
		if (value != 0)
		{
			cmd->buttons |= BT_BRAKE;
			if (!(player->pflags & PF_RECOVERYSPIN))
			{
				forward -= (value * 25) / JOYAXISRANGE;
			}
		}

		// But forward/backward IS used for aiming.
		if (joystickvector.yaxis != 0)
		{
			cmd->throwdir -= (joystickvector.yaxis * KART_FULLTURN) / JOYAXISRANGE;
			cmd->buttons |= joystickvector.yaxis < 0 ? BT_FORWARD : BT_BACKWARD;
		}
	}

	// fire with any button/key
	if (G_PlayerInputDown(forplayer, gc_fire, false, DEADZONE_BUTTON))
	{
		cmd->buttons |= BT_ATTACK;
	}

	// drift with any button/key
	if (G_PlayerInputDown(forplayer, gc_drift, false, DEADZONE_BUTTON))
	{
		cmd->buttons |= BT_DRIFT;
	}

	// rear view with any button/key
	if (G_PlayerInputDown(forplayer, gc_lookback, false, DEADZONE_BUTTON))
	{
		cmd->buttons |= BT_LOOKBACK;
	}

	// horn with any button/key
	if (G_PlayerInputDown(forplayer, gc_horncode, false, DEADZONE_BUTTON))
	{
		cmd->buttons |= BT_HORN;
	}

	// Lua scriptable buttons
	if (G_PlayerInputDown(forplayer, gc_custom1, false, DEADZONE_BUTTON))
		cmd->buttons |= BT_CUSTOM1;
	if (G_PlayerInputDown(forplayer, gc_custom2, false, DEADZONE_BUTTON))
		cmd->buttons |= BT_CUSTOM2;
	if (G_PlayerInputDown(forplayer, gc_custom3, false, DEADZONE_BUTTON))
		cmd->buttons |= BT_CUSTOM3;

	// Reset camera
	if (G_PlayerInputDown(forplayer, gc_camreset, false, DEADZONE_BUTTON))
	{
		if (thiscam->chase && *rd == false)
			P_ResetCamera(player, thiscam);
		*rd = true;
	}
	else
		*rd = false;

	// spectator aiming shit, ahhhh...
	{
		int32_t screen_invert =
			(player->mo && (player->mo->eflags & MFE_VERTICALFLIP)
			 && (!thiscam->chase)) //because chasecam's not inverted
			 ? -1 : 1; // set to -1 or 1 to multiply

		int32_t axis = G_PlayerInputAnalog(forplayer, gc_lookup, false, DEADZONE_Y) - G_PlayerInputAnalog(forplayer, gc_lookdown, false, DEADZONE_Y);

		// either i allow mouse binds in controls... or i just hardcode it :^)
		int32_t mousey = gamekeydown[0][KEY_MOUSEMOVE+1] - gamekeydown[0][KEY_MOUSEMOVE+0];
		axis += mousey * 8*2;

		if (axis != 0 && spectating)
			cmd->aiming += axis/2 * screen_invert; // divide by 2 to match KB_LOOKSPEED

		if (G_PlayerInputDown(forplayer, gc_centerview, false, DEADZONE_BUTTON)) // // No need to put a spectator limit on this one though :V
			cmd->aiming = 0;
	}

	if (forward > MAXPLMOVE)
		forward = MAXPLMOVE;
	else if (forward < -MAXPLMOVE)
		forward = -MAXPLMOVE;

	if (side > MAXPLMOVE)
		side = MAXPLMOVE;
	else if (side < -MAXPLMOVE)
		side = -MAXPLMOVE;

	if (forward) cmd->forwardmove = (int8_t)(cmd->forwardmove + forward);
	if (side) cmd->sidemove = (int8_t)(cmd->sidemove + side);
	cmd->flags = 0;

	/* 	Lua: Allow this hook to overwrite ticcmd.
		We check if we're actually in a level because for some reason this Hook would run in menus and on the titlescreen otherwise.
		Be aware that within this hook, nothing but this player's cmd can be edited (otherwise we'd run in some pretty bad synching problems since this is clientsided, or something)

		Possible usages for this are:
			-Forcing the player to perform an action, which could otherwise require terrible, terrible hacking to replicate.
			-Preventing the player to perform an action, which would ALSO require some weirdo hacks.
			-Making some galaxy brain autopilot Lua if you're a masochist
			-Making a Mario Kart 8 Deluxe tier baby mode that steers you away from walls and whatnot. You know what, do what you want!
	*/
	if (addedtogame && gamestate == GS_LEVEL)
	{
		LUA_HookTiccmd(player, cmd, HOOK(PlayerCmd));

		// Send leveltime when this tic was generated to the server for control lag calculations.
		// Only do this when in a level. Also do this after the hook, so that it can't overwrite this.
		cmd->latency = (leveltime & TICCMD_LATENCYMASK);
	}

	if (cmd->forwardmove > MAXPLMOVE)
		cmd->forwardmove = MAXPLMOVE;
	else if (cmd->forwardmove < -MAXPLMOVE)
		cmd->forwardmove = -MAXPLMOVE;

	if (cmd->sidemove > MAXPLMOVE)
		cmd->sidemove = MAXPLMOVE;
	else if (cmd->sidemove < -MAXPLMOVE)
		cmd->sidemove = -MAXPLMOVE;

	if (cmd->turning > KART_FULLTURN)
		cmd->turning = KART_FULLTURN;
	else if (cmd->turning < -KART_FULLTURN)
		cmd->turning = -KART_FULLTURN;
		
	if (!spectating)
	{
		if (cmd->angle > KART_FULLTURN)
			cmd->angle = KART_FULLTURN;
		else if (cmd->angle < -KART_FULLTURN)
			cmd->angle = -KART_FULLTURN;
	}

	if (cmd->throwdir > KART_FULLTURN)
		cmd->throwdir = KART_FULLTURN;
	else if (cmd->throwdir < -KART_FULLTURN)
		cmd->throwdir = -KART_FULLTURN;

	if (G_GetGamepadCanUseTilt(forplayer) && (cv_tiltcontrol[forplayer].value == TILTCONTROL_STEER || cv_tiltcontrol[forplayer].value == TILTCONTROL_ON))
	{
		fixed_t tilt = G_GetGamepadGravity(forplayer).x;
		// vector3_t shake = G_GetGamepadShake(forplayer);

		cmd->flags |= TICCMD_USINGTILT;
		if (abs(tilt) > MAXGAMEPADTILT)
			cmd->flags |= TICCMD_EXCESSTILT;

		//cmd->shake = CLAMP((GAMEPADSHAKETHRESHOLD*FV3_Length(&shake))/FRACUNIT, 0, UINT8_MAX);

		//CONS_Debug(DBG_IMU, "CMD Tilt: %d, Shake: %d\n", cmd->tilt, cmd->shake);
	}

	G_DoCameraTurn(cmd, realtics, ssplayer, player);

	// Reset away view if a command is given.
	if ((cmd->forwardmove ||  cmd->sidemove || cmd->buttons)
		&& !r_splitscreen && displayplayers[0] != consoleplayer && ssplayer == 1)
	{
		// Call ViewpointSwitch hooks here.
		// The viewpoint was forcibly changed.
		LUA_HookViewpointSwitch(player, &players[consoleplayer], true);
		displayplayers[0] = consoleplayer;
	}
}

ticcmd_t *G_CopyTiccmd(ticcmd_t* dest, const ticcmd_t* src, const size_t n)
{
	return memcpy(dest, src, n*sizeof(*src));
}

ticcmd_t *G_MoveTiccmd(ticcmd_t* dest, const ticcmd_t* src, const size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
	{
		dest[i].forwardmove = src[i].forwardmove;
		dest[i].sidemove = src[i].sidemove;
		dest[i].turning = (int16_t)LSBF_SHORT(src[i].turning);
		dest[i].angle = (int16_t)LSBF_SHORT(src[i].angle);
		dest[i].throwdir = (int16_t)LSBF_SHORT(src[i].throwdir);
		dest[i].aiming = (int16_t)LSBF_SHORT(src[i].aiming);
		dest[i].buttons = (uint16_t)LSBF_SHORT(src[i].buttons);
		dest[i].latency = src[i].latency;
		dest[i].flags = src[i].flags;
	}
	return dest;
}

void weaponPrefChange(void)
{
	if (Playing())
		WeaponPref_Send(0);
}

void weaponPrefChange2(void)
{
	if (Playing())
		WeaponPref_Send(1);
}

void weaponPrefChange3(void)
{
	if (Playing())
		WeaponPref_Send(2);
}

void weaponPrefChange4(void)
{
	if (Playing())
		WeaponPref_Send(3);
}

//
// G_DoLoadLevelEx
//
void G_DoLoadLevelEx(dboolean resetplayer, gamestate_t newstate)
{
	dboolean doAutomate = false;
	int32_t i;

	// Make sure objectplace is OFF when you first start the level!
	OP_ResetObjectplace();

	levelstarttic = gametic; // for time calculation

	if (wipegamestate == GS_LEVEL)
		wipegamestate = -1; // force a wipe

	if (gamestate == GS_INTERMISSION)
		Y_EndIntermission();
	if (gamestate == GS_VOTING)
		Y_EndVote();

	// Is this actually necessary? Doesn't F_StartTitleScreen already do a significantly more comprehensive check?
	if (newstate == GS_TITLESCREEN)
	{
		if (gamemap < 1 || gamemap > nummapheaders)
		{
			G_SetGamestate(GS_TITLESCREEN);
			titlemapinaction = false;

			Z_Free(titlemap);
			titlemap = NULL; // let's not infinite recursion ok

			Command_ExitGame_f();
			return;
		}
	}

	// Doing this matches HOSTMOD behavior.
	// Is that desired? IDK
	doAutomate = (gamestate != GS_LEVEL && newstate == GS_LEVEL);

	G_SetGamestate(newstate);
	I_UpdateMouseGrab();

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (resetplayer || (playeringame[i] && players[i].playerstate == PST_DEAD))
			players[i].playerstate = PST_REBORN;
	}

	// Setup the level.
	if (!P_LoadLevel(false, false)) // this never returns false?
	{
		// fail so reset game stuff
		Command_ExitGame_f();
		return;
	}

	gameaction = ga_nothing;
#ifdef PARANOIA
	Z_CheckHeap(-2);
#endif

	if (roundqueue.writetextmap == true)
	{
		if (roundqueue.size > 0)
		{
			G_GetNextMap();

			// roundqueue is wiped after the last round, but
			// preserve this to track state into the Podium!
			roundqueue.writetextmap = true;

			G_NextLevel();
			return;
		}
		/*else // TODO: restore when we have podium.
		{
			// Podium: writetextmap is finished. Yay!
			HU_DoCEcho(va("Congratulations,\\%s!\\Check the console!", cv_playername[0].string));

			S_StartSound(NULL, sfx_yeeeah);

			CONS_Printf("\n\n\x83""writetextmap: Find your TEXTMAPs in %s\n", srb2home);

			roundqueue.writetextmap = false;
		}*/
	}

	for (i = 0; i <= r_splitscreen; i++)
	{
		if (camera[i].chase)
			P_ResetCamera(&players[displayplayers[i]], &camera[i]);
		
		// reset freelook stuff
		camera[i].freelook_held = false;
		camera[i].freelook_pitch = 0;
		camera[i].freelook_pitch_add = 0;
		camera[i].freelook_yaw = 0;
		camera[i].freelook_yaw_add = 0;
	}

	// clear cmd building stuff
	memset(gamekeydown, 0, sizeof (gamekeydown));

	// clear hud messages remains (usually from game startup)
	CON_ClearHUD();

	server_lagless = cv_lagless.value;

	G_ResetAllControllerRumbles();

	if (doAutomate == true)
	{
		if (roundqueue.size > 0 && roundqueue.position == 1)
		{
			Automate_Run(AEV_QUEUESTART);
		}

		Automate_Run(AEV_ROUNDSTART);
	}

	Automate_Run(AEV_LEVELSTART);
}

void G_DoLoadLevel(dboolean resetplayer)
{
	G_DoLoadLevelEx(resetplayer, GS_LEVEL);
}

//
// Start the title card.
//
void G_StartTitleCard(void)
{
	// The title card has been disabled for this map.
	// Oh well.
	if (!G_IsTitleCardAvailable() || demo.rewinding)
	{
		WipeStageTitle = false;
		return;
	}

	// clear the hud
	CON_ClearHUD();

	// prepare status bar
	ST_startTitleCard();

	// start the title card
	WipeStageTitle = (gamestate == GS_LEVEL);
}

//
// Run the title card before fading in to the level.
//
void G_PreLevelTitleCard(void)
{
#ifndef NOWIPE
	D_WipeLoop(WIPELOOP_TITLECARD, 0, false);
#endif
}

//
// Returns true if the current level has a title card.
//
dboolean G_IsTitleCardAvailable(void)
{
#if 0
	// The current level has no name.
	if (!mapheaderinfo[gamemap-1]->lvlttl[0])
		return false;
#endif

	// The title card is available.
	return true;
}

int32_t pausedelay = 0;
static int32_t camtoggledelay[MAXSPLITSCREENPLAYERS];
static int32_t spectatedelay[MAXSPLITSCREENPLAYERS];
static int32_t respawndelay[MAXSPLITSCREENPLAYERS];

//
// G_Responder
// Get info needed to make ticcmd_ts for the players.
//
dboolean G_Responder(event_t *ev)
{
	uint8_t i;

	// any other key pops up menu if in demos
	if (gameaction == ga_nothing && !demo.quitafterplaying &&
		((demo.playback && !modeattacking && !demo.title && !multiplayer) || gamestate == GS_TITLESCREEN))
	{
		if (ev->type == ev_keydown && ev->data1 != 301 && !(gamestate == GS_TITLESCREEN && finalecount < TICRATE))
		{
			M_StartControlPanel();
			return true;
		}
		return false;
	}
	else if (demo.playback && demo.title)
	{
		// Title demo uses intro responder
		if (F_IntroResponder(ev))
		{
			// stop the title demo
			G_CheckDemoStatus();
			return true;
		}
		return false;
	}

	if (Playing())
	{
		// If you're playing, chat is real.
		// Neatly sidesteps a class of bugs where whenever we add a
		// new gamestate accessible in netplay, chat was console-only.
		if (HU_Responder(ev))
		{
			hu_keystrokes = true;
			return true; // chat ate the event
		}
	}

	if (gamestate == GS_LEVEL)
	{
		if (AM_Responder(ev))
			return true; // automap ate it
		// map the event (key/mouse/joy) to a gamecontrol
	}
	// Intro
	else if (gamestate == GS_INTRO)
	{
		if (F_IntroResponder(ev) && !WipeInAction)
		{
			D_StartTitle();
			return true;
		}
	}
	else if (gamestate == GS_CUTSCENE)
	{
		if (F_CutsceneResponder(ev))
		{
			D_StartTitle();
			return true;
		}
	}
	else if (gamestate == GS_CREDITS) // todo: keep ending here?
	{
		if (F_CreditResponder(ev))
		{
			// Skip credits for everyone
			if (! netgame)
				F_StartGameEvaluation();
			else if (server || IsPlayerAdmin(consoleplayer))
				SendNetXCmd(XD_EXITLEVEL, NULL, 0);
			return true;
		}
	}
	else if (gamestate == GS_BLANCREDITS)
	{
		if (F_CreditResponder(ev))
		{
			// Skip credits for everyone
			if (! netgame)
				F_StartGameEvaluation();
			else if (server || IsPlayerAdmin(consoleplayer))
				SendNetXCmd(XD_EXITLEVEL, NULL, 0);
			return true;
		}
	}
	else if (gamestate == GS_SECRETCREDITS)
	{
		if (F_CreditResponder(ev))
		{
			// Skip credits for everyone
			if (! netgame)
				F_StartGameEvaluation();
			else if (server || IsPlayerAdmin(consoleplayer))
				SendNetXCmd(XD_EXITLEVEL, NULL, 0);
			return true;
		}
	}

	// allow spy mode changes even during the demo
	if (gamestate == GS_LEVEL && ev->type == ev_keydown
		&& G_ControlBoundToKey(0, gc_viewpoint, ev->data1, false))
	{
		if (!demo.playback && (r_splitscreen))
			g_localplayers[0] = consoleplayer;
		else
		{
			G_AdjustView(1, 1, true);

			// change statusbar also if playing back demo
			if (demo.quitafterplaying)
				ST_changeDemoView();

			return true;
		}
	}

	if (gamestate == GS_LEVEL && ev->type == ev_keydown && multiplayer && demo.playback)
	{
		if (G_ControlBoundToKey(1, gc_viewpoint, ev->data1, false))
		{
			G_AdjustView(2, 1, true);
			return true;
		}
		else if (G_ControlBoundToKey(2, gc_viewpoint, ev->data1, false))
		{
			G_AdjustView(3, 1, true);
			return true;
		}
		else if (G_ControlBoundToKey(3, gc_viewpoint, ev->data1, false))
		{
			G_AdjustView(4, 1, true);
			return true;
		}

		// Allow pausing
		if (G_ControlBoundToKey(0, gc_pause, ev->data1, true))
		{
			paused = !paused;

			if (demo.rewinding)
			{
				G_ConfirmRewind(leveltime);
				paused = true;
				S_PauseAudio();
			}
			else if (paused)
				S_PauseAudio();
			else
				S_ResumeAudio();

			return true;
		}

		// open menu but only w/ esc
		if (ev->data1 == 32)
		{
			M_StartControlPanel();

			return true;
		}
	}

	// update keys current state
	G_MapEventsToControls(ev);

	switch (ev->type)
	{
		case ev_keydown:
			if (G_ControlBoundToKey(0, gc_pause, ev->data1, true))
			{
				if (modeattacking && !demo.playback && (gamestate == GS_LEVEL))
				{
					if (menustack[0] || pausedelay < 0 || leveltime < 2)
						return true;

					if (pausedelay < 1+(NEWTICRATE/2))
						pausedelay = 1+(NEWTICRATE/2);
					else if (++pausedelay > 1+(NEWTICRATE/2)+(NEWTICRATE/3))
					{
						G_SetModeAttackRetryFlag();
						return true;
					}
					pausedelay++; // counteract subsequent subtraction this frame
				}
				else
				{
					int32_t oldpausedelay = pausedelay;
					pausedelay = (NEWTICRATE/7);
					if (!oldpausedelay)
					{
						// command will handle all the checks for us
						COM_ImmedExecute("pause");
						return true;
					}
				}
			}

			for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
			{
				if (G_ControlBoundToKey(i, gc_camtoggle, ev->data1, false))
				{
					if (!camtoggledelay[i])
					{
						camtoggledelay[i] = NEWTICRATE / 7;
						CV_SetValue(&cv_chasecam[i], cv_chasecam[i].value ? 0 : 1);
					}
				}

				if (G_ControlBoundToKey(i, gc_spectate, ev->data1, false))
				{
					if (!spectatedelay[i])
					{
						char *commandname = va("changeteam");

						if (i > 0)
						{
							// Add one for command names.
							commandname = va("changeteam%d", i+1);
						}

						spectatedelay[i] = NEWTICRATE / 7;
						COM_ImmedExecute(va("%s spectator", commandname));
					}
				}

				if (G_ControlBoundToKey(i, gc_respawn, ev->data1, false))
				{
					if (!respawndelay[i])
					{
						char *commandname = va("respawn");

						if (i > 0)
						{
							// Add one for command names.
							commandname = va("respawn%d", i+1);
						}

						respawndelay[i] = NEWTICRATE / 4;
						COM_ImmedExecute(commandname);
					}
				}

				if (G_ControlBoundToKey(i, gc_director, ev->data1, false))
				{
					K_ToggleDirector();
				}

			}

			return true;

		case ev_keyup:
			return false; // always let key up events filter down

		case ev_mouse:
			return true; // eat events

		case ev_joystick:
			return true; // eat events

		case ev_accelerometer:
			return true; // eat events

		case ev_gyroscope:
			return true; // eat events

		default:
			break;
	}

	return false;
}

//
// G_CouldView
// Return whether a player could be viewed by any means.
//
dboolean G_CouldView(int32_t playernum)
{
	player_t *player;

	if (playernum < 0 || playernum > MAXPLAYERS-1)
		return false;

	if (!playeringame[playernum])
		return false;

	player = &players[playernum];

	if (player->spectator)
		return false;

	// SRB2Kart: Only go through players who are actually playing
	if (player->exiting)
		return false;
	if (( player->pflags & PF_NOCONTEST ))
		return false;

	// I don't know if we want this actually, but I'll humor the suggestion anyway
	if ((gametypes[gametype]->rules & GTR_BUMPERS) && !demo.playback)
	{
		if (player->bumper <= 0)
			return false;
	}

	// SRB2Kart: we have no team-based modes, YET...
	if (G_GametypeHasTeams())
	{
		if (players[consoleplayer].ctfteam && player->ctfteam != players[consoleplayer].ctfteam)
			return false;
	}

	return true;
}

//
// G_CanView
// Return whether a player can be viewed on a particular view (splitscreen).
//
dboolean G_CanView(int32_t playernum, uint8_t viewnum, dboolean onlyactive)
{
	uint8_t splits;
	uint8_t viewd;
	int32_t *displayplayerp;

	if (!(onlyactive ? G_CouldView(playernum) : (playeringame[playernum] && !players[playernum].spectator)))
		return false;

	splits = r_splitscreen+1;
	if (viewnum > splits)
		viewnum = splits;

	for (viewd = 1; viewd < viewnum; ++viewd)
	{
		displayplayerp = (&displayplayers[viewd-1]);
		if ((*displayplayerp) == playernum)
			return false;
	}
	for (viewd = viewnum + 1; viewd <= splits; ++viewd)
	{
		displayplayerp = (&displayplayers[viewd-1]);
		if ((*displayplayerp) == playernum)
			return false;
	}

	return true;
}

//
// G_FindView
// Return the next player that can be viewed on a view, wraps forward.
// An out of range startview is corrected.
//
int32_t G_FindView(int32_t startview, uint8_t viewnum, dboolean onlyactive, dboolean reverse)
{
	int32_t i, dir = reverse ? -1 : 1;
	startview = min(max(startview, 0), MAXPLAYERS);
	for (i = startview; i < MAXPLAYERS && i >= 0; i += dir)
	{
		if (G_CanView(i, viewnum, onlyactive))
			return i;
	}
	for (i = (reverse ? MAXPLAYERS-1 : 0); i != startview; i += dir)
	{
		if (G_CanView(i, viewnum, onlyactive))
			return i;
	}
	return -1;
}

int32_t G_CountPlayersPotentiallyViewable(dboolean active)
{
	int32_t total = 0;
	int32_t i;
	for (i = 0; i < MAXPLAYERS; ++i)
	{
		if (active ? G_CouldView(i) : (playeringame[i] && !players[i].spectator))
			total++;
	}
	return total;
}

//
// G_ResetView
// Correct a viewpoint to playernum or the next available, wraps forward.
// Also promotes splitscreen up to available viewable players.
// An out of range playernum is corrected.
//
void G_ResetView(uint8_t viewnum, int32_t playernum, dboolean onlyactive)
{
	uint8_t splits;
	uint8_t viewd;

	int32_t    *displayplayerp;

	int32_t olddisplayplayer;
	int32_t playersviewable;

	splits = r_splitscreen+1;

	/* Promote splits */
	if (viewnum > splits)
	{
		playersviewable = G_CountPlayersPotentiallyViewable(onlyactive);
		if (playersviewable < splits)/* do not demote */
			return;

		if (viewnum > playersviewable)
			viewnum = playersviewable;
		r_splitscreen = viewnum-1;

		R_ExecuteSetViewSize();
	}

	displayplayerp = (&displayplayers[viewnum-1]);
	olddisplayplayer = (*displayplayerp);

	/* Check if anyone is available to view. */
	if (( playernum = G_FindView(playernum, viewnum, onlyactive, playernum < olddisplayplayer) ) == -1)
	{
		if (G_PartySize(consoleplayer) < viewnum)
		{
			return;
		}

		/* Fall back on true self */
		playernum = G_PartyMember(consoleplayer, viewnum - 1);
	}

	// Call ViewpointSwitch hooks here.
	// The viewpoint was forcibly changed.
	LUA_HookViewpointSwitch(&players[g_localplayers[viewnum - 1]], &players[playernum], true);

	/* Focus our target view first so that we don't take its player. */
	(*displayplayerp) = playernum;

	/* If a viewpoint changes, reset the camera to clear uninitialized memory. */
	if (viewnum > splits)
	{
		for (viewd = splits+1; viewd <= viewnum; ++viewd)
		{
			G_FixCamera(viewd);
		}
	}
	else
	{
		if ((*displayplayerp) != olddisplayplayer)
		{
			G_FixCamera(viewnum);
		}
	}

	if (demo.playback)
	{
		if (viewnum == 1)
			consoleplayer = displayplayers[0];

		G_SyncDemoParty(olddisplayplayer, r_splitscreen);
	}

	// change statusbar also if playing back demo
	if (demo.quitafterplaying)
		ST_changeDemoView();
}

//
// G_FixCamera
// Reset camera position, angle and interpolation on a view
// after changing state.
//
void G_FixCamera(uint8_t view)
{
	player_t *player = &players[displayplayers[view - 1]];

	// The order of displayplayers can change, which would
	// invalidate localangle.
	localangle[view - 1] = player->angleturn;

	P_ResetCamera(player, &camera[view - 1]);

	// Make sure the viewport doesn't interpolate at all into
	// its new position -- just snap instantly into place.
	R_ResetViewInterpolation(view);
}

//
// G_AdjustView
// Increment a viewpoint by offset from the current player. A negative value
// decrements.
//
void G_AdjustViewEx(uint8_t viewnum, int32_t offset, dboolean onlyactive, dboolean resetfreecam)
{
	int32_t *displayplayerp, oldview;
	displayplayerp = &displayplayers[viewnum-1];
	oldview = (*displayplayerp);

	// turn off the freecam
	if (resetfreecam)
		camera[viewnum-1].freecam = false;

	G_ResetView(viewnum, ( (*displayplayerp) + offset ), onlyactive);

	// If no other view could be found, go back to what we had.
	if ((*displayplayerp) == -1)
		(*displayplayerp) = oldview;
}

//
// G_ResetViews
// Ensures all viewpoints are valid
// Also demotes splitscreen down to one player.
//
void G_ResetViews(dboolean resetfreecam)
{
	uint8_t splits;
	uint8_t viewd;

	int32_t playersviewable;

	splits = r_splitscreen+1;

	playersviewable = G_CountPlayersPotentiallyViewable(false);
	/* Demote splits */
	if (playersviewable < splits)
	{
		splits = max(playersviewable, G_PartySize(consoleplayer)); // don't delete local players
		r_splitscreen = splits - 1;
		R_ExecuteSetViewSize();
	}

	/*
	Consider installing a method to focus the last
	view elsewhere if all players spectate?
	*/
	for (viewd = 1; viewd <= splits; ++viewd)
	{
		G_AdjustViewEx(viewd, 0, false, resetfreecam);
	}
}

//
// G_Ticker
// Make ticcmd_ts for the players.
//
void G_Ticker(dboolean run)
{
	TracyCZone(__zone, true);

	uint32_t i;
	int32_t buf;
	ticcmd_t *cmd;

	// see also SCR_DisplayMarathonInfo
	if ((marathonmode & (MA_INIT|MA_INGAME)) == MA_INGAME && gamestate == GS_LEVEL)
		marathontime++;

	P_MapStart();

	if (gamestate == GS_LEVEL && G_GetRetryFlag())
	{
		if (demo.playback == true)
		{
			// Stop playback!!
			G_ClearRetryFlag();
			// G_CheckDemoStatus() called here fails an I_Assert in g_party.cpp Console()!?
			// I'm sure there's a completely logical explanation and an elegant solution
			// where we can defer some sort of state change. However I'm tired, I've been
			// looking after my niece, my arm hurts a bit when using mouse/keyboard, and
			// we are ALMOST DONE. So I'm going to bodge this for the sake of release.
			// The minimal set of calls to dump you back to the menu as soon as possible
			// will have to do, so that everybody can have fun racing as rings. ~toast 050424
			G_StopDemo();
			Command_ExitGame_f();
		}
		else
		{
			// Or, alternatively, retry.
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (playeringame[i])
				{
					if (players[i].bot == true
						&& grandprixinfo.gp == true
						&& grandprixinfo.masterbots == false)
					{
						uint8_t bot_level_decrease = 3;

						if (grandprixinfo.gamespeed == KARTSPEED_EASY)
						{
							bot_level_decrease++;
						}
						else if (grandprixinfo.gamespeed == KARTSPEED_HARD)
						{
							bot_level_decrease--;
						}

						if (players[i].botvars.difficulty <= bot_level_decrease)
						{
							players[i].botvars.difficulty = 1;
						}
						else
						{
							players[i].botvars.difficulty -= bot_level_decrease;
						}
					}
					else
					{
						K_PlayerLoseLife(&players[i]);
					}
				}
			}

			D_MapChange(gamemap, gametype, encoremode, false, 1, false, false);
		}
	}

	// do player reborns if needed
	if (G_GamestateUsesLevel() == true)
	{
		dboolean changed = false;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && players[i].playerstate == PST_REBORN)
			{
				G_DoReborn(i);
				changed = true;
			}
		}

		if (changed == true)
		{
			K_GetPlayerNumber();
			K_UpdateAllPlayerPositions();
			K_UpdatePositionFactorAndPlayerRanks();
		}
	}

	P_MapEnd();

	// do things to change the game state
	while (gameaction != ga_nothing)
		switch (gameaction)
		{
			case ga_completed: G_DoCompleted(); break;
			case ga_startcont: G_DoStartContinue(); break;
			case ga_continued: G_DoContinued(); break;
			case ga_worlddone: G_DoWorldDone(); break;
			case ga_startvote: G_DoStartVote(); break;
			case ga_nothing: break;
			default: I_Error("gameaction = %d\n", gameaction);
		}

	buf = gametic % BACKUPTICS;

	if (!demo.playback)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			cmd = &players[i].cmd;

			if (playeringame[i])
			{
				G_CopyTiccmd(cmd, &netcmds[buf][i], 1);

				// Use the leveltime sent in the player's ticcmd to determine control lag
				if (K_PlayerUsesBotMovement(&players[i]))
				{
					// Never has lag
					cmd->latency = 0;
				}
				else
				{
					//@TODO add a cvar to allow setting this max
					cmd->latency = min(((leveltime & TICCMD_LATENCYMASK) - cmd->latency) & TICCMD_LATENCYMASK, MAXPREDICTTICS-1);
				}
			}
		}
	}

	// do main actions
	switch (gamestate)
	{
		case GS_LEVEL:
			if (demo.title)
				F_TitleDemoTicker();
			P_Ticker(run); // tic the game
			ST_Ticker(run);
			F_TextPromptTicker();
			AM_Ticker();
			HU_Ticker();
			break;

		case GS_INTERMISSION:
			if (run)
				Y_Ticker();
			HU_Ticker();

			break;

		case GS_VOTING:
			if (run)
				Y_VoteTicker();
			HU_Ticker();
			break;

		case GS_INTRO:
			if (run)
				F_IntroTicker();
			break;

		case GS_CUTSCENE:
			if (run)
				F_CutsceneTicker();
			HU_Ticker();
			break;

		case GS_EVALUATION:
			if (run)
				F_GameEvaluationTicker();
			HU_Ticker();
			break;

		case GS_CREDITS:
			if (run)
				F_CreditTicker();
			HU_Ticker();
			break;

		case GS_BLANCREDITS:
			if (run)
				F_BlanCreditTicker();
			HU_Ticker();
			break;

		case GS_SECRETCREDITS:
			if (run)
				F_SecretCreditsTicker();
			HU_Ticker();
			break;

		case GS_TITLESCREEN:
			if (titlemapinaction)
				P_Ticker(run);

			F_MenuPresTicker(run);
			F_TitleScreenTicker(run);
			break;

		case GS_MENU:
			F_MenuPresTicker(run);
			break;

		case GS_WAITINGPLAYERS:
			if (netgame)
				F_WaitingPlayersTicker();
			HU_Ticker();
			break;

		case GS_DEDICATEDSERVER:
		case GS_NULL:
		case FORCEWIPE:
			break; // do nothing
	}
	HU_TickSongCredits();

	if (run)
	{
		if (G_GametypeHasSpectators()
			&& (gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_VOTING // definitely good
			|| gamestate == GS_WAITINGPLAYERS)) // definitely a problem if we don't do it at all in this gamestate, but might need more protection?
		{
			K_CheckSpectateStatus(true);
		}

		if (pausedelay && pausedelay != INT32_MIN)
		{
			if (pausedelay > 0)
				pausedelay--;
			else
				pausedelay++;
		}

		for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
		{
			if (camtoggledelay[i])
				camtoggledelay[i]--;
			if (spectatedelay[i])
				spectatedelay[i]--;
			if (respawndelay[i])
				respawndelay[i]--;
		}

		if (gametic % NAMECHANGERATE == 0)
		{
			memset(player_name_changes, 0, sizeof player_name_changes);
		}
	}

	TracyCZoneEnd(__zone);
}

//
// PLAYER STRUCTURE FUNCTIONS
// also see P_SpawnPlayer in P_Things
//

//
// G_PlayerFinishLevel
// Called when a player completes a level.
//
static inline void G_PlayerFinishLevel(int32_t player)
{
	player_t *p;

	p = &players[player];

	p->mo->renderflags &= ~(RF_TRANSMASK|RF_BRIGHTMASK); // cancel invisibility
	P_FlashPal(p, 0, 0); // Resets

	memset(p->powers, 0, sizeof (p->powers));
	memset(p->kartstuff, 0, sizeof (p->kartstuff)); // SRB2kart
	p->starpostangle = 0;
	p->starposttime = 0;
	p->starpostx = 0;
	p->starposty = 0;
	p->starpostz = 0;
	p->starpostnum = 0;
	p->nextcheck = 0;
	p->prevcheck = 0;

	// SRB2kart: Increment the "matches played" counter.
	if (player == consoleplayer)
	{
		if (legitimateexit && !demo.playback && !mapreset) // (yes you're allowed to unlock stuff this way when the game is modified)
		{
			K_StatRound();
			if (M_UpdateUnlockablesAndExtraEmblems())
				S_StartSound(NULL, sfx_ncitem);
			G_SaveGameData();
		}

		legitimateexit = false;
	}

	p->spectatorreentry = 0; // Clean up any pending re-entry forbiddings
}

void G_HandleRestatMessage(restatmessage_t *rm)
{
	if (players[rm->player].jointime == 0)
		return;

	uint16_t chatcolor = skincolors[players[rm->player].skincolor].chatcolor;
	char color_prefix[2] = {};

	if (chatcolor > V_TANMAP)
	{
		snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80');
	}
	else
	{
		snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80' + (chatcolor >> V_CHARCOLORSHIFT));
	}


	if (rm->kartspeedrestat != 0 && rm->kartweightrestat != 0)
	{
		if (playeringame[rm->player] && rm->notifyrestat)
		{
			if ((splitscreen && rm->player == consoleplayer) || rm->player != consoleplayer)
			{
				HU_AddChatText(va("%s%s\x82 is now \x84%d speed\x82, \x87%d weight\x82.", color_prefix, player_names[rm->player], rm->kartspeed, rm->kartweight), true);
			}
			else
			{
				HU_AddChatText(va("%sYou\x82 are now \x84%d speed\x82, \x87%d weight\x82.", color_prefix, rm->kartspeed, rm->kartweight), true);
			}
		}
	}
	else
	{
		if (playeringame[rm->player] && rm->notifyrestat)
		{
			if ((splitscreen && rm->player == consoleplayer) || rm->player != consoleplayer)
			{
				HU_AddChatText(va("%s%s\x82 is now using their skin's default stats.", color_prefix, player_names[rm->player]), true);
			}
			else
			{
				HU_AddChatText(va("%sYou\x82 are now using your skin's default stats.", color_prefix), true);
			}
		}
	}
}

//
// G_PlayerReborn
// Called after a player dies. Almost everything is cleared and initialized.
//
void G_PlayerReborn(int32_t player, dboolean betweenmaps)
{
	player_t *p;

	uint8_t i;
	int32_t score, roundscore;
	int32_t lives;

	uint8_t kartspeed;
	uint8_t kartweight;
	uint8_t kartspeedrestat;
	uint8_t kartweightrestat;
	dboolean randomrestat;
	dboolean notifyrestat;

	dboolean followerready;
	int32_t followerskin;
	uint16_t followercolor;

	mobj_t *follower; // old follower, will probably be removed by the time we're dead but you never know.

	int32_t charflags;
	uint32_t followitem;

	int32_t pflags;

	uint8_t ctfteam;

	fixed_t starpostx;
	fixed_t starposty;
	fixed_t starpostz;
	angle_t starpostangle;
	dboolean starpostflip;
	int32_t starpostnum;
	tic_t starposttime; // The time of the last cheatcheck you hit
	int32_t prevcheck; // Distance from Previous Legacy Checkpoint
	int32_t nextcheck; // Distace to Next Legacy Checkpoint

	int32_t exiting;

	int32_t khudfinish;
	int32_t khudcardanimation;
	int32_t khudprevrankscore;
	int32_t khudprevrankscoredelay;
	int32_t khudprevranktier;

	int16_t totalring;
	uint8_t laps;
	uint8_t latestlap;
	uint16_t skincolor;
	int32_t skin;
	uint16_t voice;
	uint8_t availabilities[MAXAVAILABILITY];

	tic_t jointime;

	uint8_t splitscreenindex;
	dboolean spectator;
	dboolean bot;
	uint8_t botdifficulty;
	botStyle_e style;

	int16_t rings;
	int16_t minrings;
	int16_t maxrings;
	angle_t playerangleturn;

	uint8_t botdiffincrease;
	dboolean botrival;

	int8_t xtralife;

	uint8_t public_key[PUBKEYLENGTH];

	char clan[MAXCLANTAG];

	// SRB2kart
	int32_t itemtype;
	int32_t itemamount;
	int32_t subitemtype;
	int32_t subitemamount;
	int32_t itemroulette;
	int32_t previtemroulette;
	int32_t roulettetype;
	int32_t growshrinktimer;
	uint8_t bubblehealth;
	int32_t bumper;
	int32_t karmapoints;
	int32_t wanted;
	dboolean songcredit = false;
	tic_t spectatorreentry;
	tic_t grieftime;
	uint8_t griefstrikes;
	uint16_t nocontrol;
	int32_t kickstartaccel;
	dboolean enteredGame;
	uint8_t lastsafelap;
	uint8_t lastsafestarpost;
	uint16_t bigwaypointgap;
	dboolean jitterlegacy = false;

	ranklog_t rankdata = {0};
	rankscorefactor_t rankfactor = {0};
	int32_t rankpts = 0;
	int32_t real_rankpts = 0;

	uint32_t progress = 0;
	int32_t progdelta = 0;
	dboolean progupdate = false;

	tic_t laptime[LAP__MAX];

	// Keep stuff shit
	uint16_t ksflags;

	uint16_t ksrocketsneakertimer;

	uint16_t ksinvincibilitytimer;

	uint16_t kssmonitortimer;
	uint16_t kssmonitorexpiring;
	uint16_t ksmaxsmonitortime;

	int16_t ksgrowshrinktimer;

	uint16_t ksflametimer;

	// This needs to be first, to permit it to wipe extra information
	jointime = players[player].jointime;
	if (jointime <= 1)
	{
		// Now called in Got_AddPlayer. In case of weirdness, break glass.
		// G_SpectatePlayerOnJoin(player);
		betweenmaps = true;
	}

	score = players[player].score;
	lives = players[player].lives;
	ctfteam = players[player].ctfteam;

	jointime = players[player].jointime;

	splitscreenindex = players[player].splitscreenindex;
	spectator = players[player].spectator;

	playerangleturn = players[player].angleturn;

	skincolor = players[player].skincolor;
	skin = players[player].skin;
	voice = players[player].voice_id;

	// SRB2kart
	kartspeed = players[player].kartspeed;
	kartweight = players[player].kartweight;

	kartspeedrestat = players[player].kartspeedrestat;
	kartweightrestat = players[player].kartweightrestat;
	randomrestat = players[player].randomrestat;

	followerready = players[player].followerready;
	followercolor = players[player].followercolor;
	followerskin = players[player].followerskin;

	jitterlegacy = players[player].jitterlegacy;

	memcpy(availabilities, players[player].availabilities, sizeof(availabilities));

	charflags = players[player].charflags;

	followitem = players[player].followitem;

	bot = players[player].bot;
	style = players[player].botvars.style;
	botdifficulty = players[player].botvars.difficulty;

	botdiffincrease = players[player].botvars.diffincrease;
	botrival = players[player].botvars.rival;

	totalring = players[player].totalring;
	xtralife = players[player].xtralife;

	pflags = (players[player].pflags & (PF_WANTSTOJOIN|PF_KICKSTARTACCEL|PF_SHRINKME|PF_SHRINKACTIVE|PF_FLIPCAM));

	memcpy(&public_key, &players[player].public_key, sizeof(public_key));
	strncpy(clan, players[player].clan, MAXCLANTAG);

	// BlanKart: Ranks
	memcpy(&rankdata, &players[player].ranklog, sizeof(rankdata));
	memcpy(&rankfactor, &players[player].rankscorefactor, sizeof(rankfactor));
	rankpts = players[player].rankscore;
	real_rankpts = players[player].realrankscore;

	// SRB2kart
	if (betweenmaps || leveltime <= starttime || spectator == true)
	{
		itemroulette = 0;
		previtemroulette = 0;
		roulettetype = KROULETTETYPE_NORMAL;
		itemtype = 0;
		itemamount = 0;
		subitemtype = 0;
		subitemamount = 0;
		growshrinktimer = 0;
		bubblehealth = 0;
		bumper = ((gametypes[gametype]->rules & GTR_BUMPERS) ? K_StartingBumperCount() : 0);
		karmapoints = 0;
		wanted = 0;
		rings = cv_kartringsstart.value;
		minrings = cv_kartringsmin.value;
		maxrings = cv_kartringsmax.value;
		kickstartaccel = 0;
		lastsafelap = 0;
		lastsafestarpost = 0;
		bigwaypointgap = 0;
		nocontrol = 0;
		laps = 0;
		latestlap = 0;
		roundscore = 0;
		exiting = 0;
		khudfinish = 0;
		khudcardanimation = 0;
		khudprevrankscore = 0;
		khudprevrankscoredelay = 0;
		khudprevranktier = 0;
		starpostx = 0;
		starposty = 0;
		starpostz = 0;
		starpostangle = 0;
		starpostflip = 0;
		starpostnum = 0;
		starposttime = 0;
		prevcheck = 0;
		nextcheck = 0;
		progress = 0;
		progdelta = 0;
		progupdate = false;

		// keepstuff
		ksflags = 0;
		ksrocketsneakertimer = 0;
		ksinvincibilitytimer = 0;
		kssmonitortimer = 0;
		kssmonitorexpiring = 0;
		ksmaxsmonitortime = 0;
		ksgrowshrinktimer = 0;
		ksflametimer = 0;

		memset(&rankdata, 0, sizeof (rankdata));
		memset(&rankfactor, 0, sizeof(rankfactor));
		rankpts = 0;
		real_rankpts = 0;

		for (i = 0; i < LAP__MAX; i++)
		{
			laptime[i] = 0;
		}

		// might cause issues with weponpref sync?
		if (!cv_allowrestat.value)
		{
			kartspeedrestat = 0;
			kartweightrestat = 0;
			randomrestat = false;
		}

		if (randomrestat)
		{
			kartspeedrestat = P_RandomRange(PR_RESTAT, 1, 9);
			kartweightrestat = P_RandomRange(PR_RESTAT, 1, 9);
		}

		if (kartspeedrestat != 0 && kartweightrestat != 0)
		{
			kartspeed = kartspeedrestat;
			kartweight = kartweightrestat;

			notifyrestat = (
				randomrestat ||
				kartspeedrestat != players[player].kartspeed || 
				kartweightrestat != players[player].kartweight
			) && cv_notifyrestat.value;
		}
		else
		{
			kartspeed = skins[players[player].skin]->kartspeed;
			kartweight = skins[players[player].skin]->kartweight;
			
			notifyrestat = (
				players[player].kartspeed != skins[players[player].skin]->kartspeed ||
				players[player].kartweight != skins[players[player].skin]->kartweight
			) && cv_notifyrestat.value;
		}
	}
	else
	{
		itemroulette = players[player].itemroulette > 0 ? 1 : 0;
		previtemroulette = players[player].previtemroulette > 0 ? 1 : 0;
		roulettetype = players[player].roulettetype;

		if (players[player].itemflags & IF_ITEMOUT)
		{
			itemtype = 0;
			itemamount = 0;
		}
		else
		{
			itemtype = players[player].itemtype;
			itemamount = players[player].itemamount;
		}

		subitemtype = players[player].subitemtype;
		subitemamount = players[player].subitemamount;

		// Keep Shrink status, remove Grow status
		// Alt. Shrink is a powerup, so don't keep that.
		if (players[player].growshrinktimer < 0 && !K_IsKartItemAlternate(KITEM_SHRINK))
			growshrinktimer = players[player].growshrinktimer;
		else
			growshrinktimer = 0;

		// keepstuff
		ksflags = 0;
		ksrocketsneakertimer = players[player].rocketsneakertimer;
		ksinvincibilitytimer = players[player].invincibilitytimer;
		kssmonitortimer = players[player].smonitortimer;
		kssmonitorexpiring = players[player].smonitorexpiring;
		ksmaxsmonitortime = players[player].maxsmonitortime;
		ksgrowshrinktimer = players[player].growshrinktimer;
		ksflametimer = players[player].flametimer;

		if (ksrocketsneakertimer)
			ksflags |= KEEPSTUFF_ROCKET;
		if (ksinvincibilitytimer)
			ksflags |= KEEPSTUFF_INVIN;
		if (kssmonitortimer || kssmonitorexpiring || ksmaxsmonitortime)
			ksflags |= KEEPSTUFF_SMON;
		if (ksgrowshrinktimer)
			ksflags |= KEEPSTUFF_GROWSHRINK;
		if (ksflametimer)
			ksflags |= KEEPSTUFF_FLAME;

		// deplete your item stack if you died with zero bubble health
		bubblehealth = players[player].bubblehealth;
		if (bubblehealth == 0 && itemtype == KITEM_BUBBLESHIELD && itemamount > 0)
		{
			if (--itemamount == 0)
				itemtype = 0;
		}

		bumper = players[player].bumper;
		karmapoints = players[player].karmapoints;
		wanted = players[player].wanted;
		rings = players[player].rings;

		minrings = players[player].ringmin;
		maxrings = players[player].ringmax;

		kickstartaccel = players[player].kickstartaccel;

		nocontrol = players[player].nocontrol;

		laps = players[player].laps;
		latestlap = players[player].latestlap;

		roundscore = players[player].roundscore;

		exiting = players[player].exiting;
		if (exiting > 0)
		{
			khudfinish = players[player].karthud[khud_finish];
			khudcardanimation = players[player].karthud[khud_cardanimation];
		}
		else
		{
			khudfinish = 0;
			khudcardanimation = 0;
		}
		khudprevrankscore = players[player].karthud[khud_prevrankscore];
		khudprevrankscoredelay = players[player].karthud[khud_prevrankscoredelay];
		khudprevranktier = players[player].karthud[khud_prevranktier];

		starpostx = players[player].starpostx;
		starposty = players[player].starposty;
		starpostz = players[player].starpostz;
		starpostangle = players[player].starpostangle;
		starpostflip = players[player].starpostflip;
		starpostnum = players[player].starpostnum;
		starposttime = players[player].starposttime;
		prevcheck = players[player].prevcheck;
		nextcheck = players[player].nextcheck;
		lastsafelap = players[player].lastsafelap;
		lastsafestarpost = players[player].lastsafestarpost;
		bigwaypointgap = players[player].bigwaypointgap;
		progress = players[player].progressthrumap;
		progdelta = players[player].progressdelta;
		progupdate = players[player].progressupdate;

		for (i = 0; i < LAP__MAX; i++)
		{
			laptime[i] = players[player].laptime[i];
		}
	}

	if (!betweenmaps)
	{
		// Obliterate follower from existence (if valid memory)
		follower = players[player].follower;
		P_SetTarget(&players[player].follower, NULL);
		P_SetTarget(&players[player].awayviewmobj, NULL);
		P_SetTarget(&players[player].followmobj, NULL);
	}
	else
	{
		follower = NULL;

		// Nice and Tidy.
		restatmessage_t rm = {player, notifyrestat, kartspeed, kartweight, kartspeedrestat, kartweightrestat};
		G_HandleRestatMessage(&rm);
	}

	spectatorreentry = (betweenmaps ? 0 : players[player].spectatorreentry);

	grieftime = players[player].grieftime;
	griefstrikes = players[player].griefstrikes;

	enteredGame = players[player].enteredGame;

	p = &players[player];
	memset(p, 0, sizeof (*p));

	p->score = score;
	p->roundscore = roundscore;
	p->lives = lives;
	p->pflags = pflags;
	p->ctfteam = ctfteam;
	p->jointime = jointime;
	p->splitscreenindex = splitscreenindex;
	p->spectator = spectator;
	p->angleturn = playerangleturn;
	p->lastsafelap = lastsafelap;
	p->lastsafestarpost = lastsafestarpost;
	p->bigwaypointgap = bigwaypointgap;

	// save player config truth reborn
	p->skincolor = skincolor;
	p->skin = skin;
	p->voice_id = voice;
	p->kartspeed = kartspeed;
	p->kartweight = kartweight;

	p->kartspeedrestat = kartspeedrestat;
	p->kartweightrestat = kartweightrestat;
	p->randomrestat = randomrestat;
	//
	p->charflags = charflags;
	memcpy(players[player].availabilities, availabilities, sizeof(availabilities));
	p->followitem = followitem;

	p->starpostx = starpostx;
	p->starposty = starposty;
	p->starpostz = starpostz;
	p->starpostangle = starpostangle;
	p->starpostflip = starpostflip;
	p->starpostnum = starpostnum;
	p->starposttime = starposttime;
	p->prevcheck = prevcheck;
	p->nextcheck = nextcheck;

	p->exiting = exiting;
	p->karthud[khud_finish] = khudfinish;
	p->karthud[khud_cardanimation] = khudcardanimation;
	p->karthud[khud_prevrankscore] = khudprevrankscore;
	p->karthud[khud_prevrankscoredelay] = khudprevrankscoredelay;
	p->karthud[khud_prevranktier] = khudprevranktier;

	p->laps = laps;
	p->latestlap = latestlap;
	p->totalring = totalring;

	for (i = 0; i < LAP__MAX; i++)
	{
		p->laptime[i] = laptime[i];
	}

	p->bot = bot;
	p->botvars.style = style;
	p->botvars.difficulty = botdifficulty;
	p->rings = rings;
	p->ringmin = minrings;
	p->ringmax = maxrings;
	p->botvars.diffincrease = botdiffincrease;
	p->botvars.rival = botrival;
	p->xtralife = xtralife;

	// SRB2kart
	p->itemroulette = itemroulette;
	p->previtemroulette = previtemroulette;
	p->roulettetype = roulettetype;
	p->itemtype = itemtype;
	p->itemamount = itemamount;
	p->subitemtype = subitemtype;
	p->subitemamount = subitemamount;
	p->growshrinktimer = growshrinktimer;
	p->bubblehealth = bubblehealth;
	p->bumper = bumper;
	p->karmadelay = comebacktime;
	p->karmapoints = karmapoints;
	p->wanted = wanted;
	p->eggmanblame = -1;
	p->nocontrol = nocontrol;
	p->kickstartaccel = kickstartaccel;
	p->jitterlegacy = jitterlegacy;
	p->progressthrumap = progress;
	p->progressdelta = progdelta;
	p->progressupdate = progupdate;

	// keepstuff
	p->keepstuff.flags = ksflags;
	p->keepstuff.rocketsneakertimer = ksrocketsneakertimer;
	p->keepstuff.invincibilitytimer = ksinvincibilitytimer;
	p->keepstuff.smonitortimer = kssmonitortimer;
	p->keepstuff.smonitorexpiring = kssmonitorexpiring;
	p->keepstuff.maxsmonitortime = ksmaxsmonitortime;
	p->keepstuff.growshrinktimer = ksgrowshrinktimer;
	p->keepstuff.flametimer = ksflametimer;

	memcpy(&p->ranklog, &rankdata, sizeof(p->ranklog));
	memcpy(&p->rankscorefactor, &rankfactor, sizeof(p->rankscorefactor));
	p->rankscore = rankpts;
	p->realrankscore = real_rankpts;

	p->ringvolume = 255;
	p->ringtransparency = 255;
	p->spinoutrot = 0;

	p->pitblame = -1;

	p->spectatorreentry = spectatorreentry;
	p->grieftime = grieftime;
	p->griefstrikes = griefstrikes;

	p->botvars.rubberband = FRACUNIT;
	p->botvars.controller = UINT16_MAX;

	K_BotReborn(p);

	memcpy(&p->public_key, &public_key, sizeof(p->public_key));
	strncpy(p->clan, clan, MAXCLANTAG);

	if (follower)
		P_RemoveMobj(follower);

	p->followerready = followerready;
	p->followerskin = followerskin;
	p->followercolor = followercolor;
	//p->follower = NULL;	// respawn a new one with you, it looks better.
	// ^ Not necessary anyway since it will be respawned regardless considering it doesn't exist anymore.


	p->playerstate = PST_LIVE;
	p->panim = PA_STILL; // standing animation

	// Check to make sure their color didn't change somehow...
	if (G_GametypeHasTeams())
	{
		if (p->ctfteam == 1 && p->skincolor != skincolor_redteam)
		{
			for (i = 0; i <= splitscreen; i++)
			{
				if (p == &players[g_localplayers[i]])
				{
					CV_SetValue(&cv_playercolor[i], skincolor_redteam);
					break;
				}
			}
		}
		else if (p->ctfteam == 2 && p->skincolor != skincolor_blueteam)
		{
			for (i = 0; i <= splitscreen; i++)
			{
				if (p == &players[g_localplayers[i]])
				{
					CV_SetValue(&cv_playercolor[i], skincolor_blueteam);
					break;
				}
			}
		}
	}

	if (p->spectator == false && !betweenmaps)
	{
		if (enteredGame == true)
		{
			ACS_RunPlayerEnterScript(p);
		}
		else
		{
			ACS_RunPlayerRespawnScript(p);
		}
	}

	if (betweenmaps && !(p->jointime <= 1))
		return;

	if (leveltime < starttime)
		return;

	if (exiting)
		return;

	P_RestoreMusic(p);

	if (songcredit)
		S_ShowMusicCredit(0, 5*TICRATE, 0);

	if (leveltime > (starttime + (TICRATE/2)) && !p->spectator)
	{
		p->respawn = 48; // Respawn effect
		p->postspawnsectordamage = SD_UNDEFINED;
	}
}

//
// G_CheckSpot
// Returns false if the player cannot be respawned
// at the given mapthing_t spot
// because something is occupying it
//
static dboolean G_CheckSpot(int32_t playernum, mapthing_t *mthing)
{
	fixed_t x;
	fixed_t y;
	int32_t i;

	// maybe there is no player start
	if (!mthing)
		return false;

	if (!players[playernum].mo)
	{
		// first spawn of level
		for (i = 0; i < playernum; i++)
			if (playeringame[i] && players[i].mo
				&& players[i].mo->x == mthing->x << FRACBITS
				&& players[i].mo->y == mthing->y << FRACBITS)
			{
				return false;
			}
		return true;
	}

	x = mthing->x << FRACBITS;
	y = mthing->y << FRACBITS;

	if (!K_CheckPlayersRespawnColliding(playernum, x, y))
		return false;

	if (!P_CheckPosition(players[playernum].mo, x, y, NULL))
		return false;

	return true;
}

//
// G_SpawnPlayer
// Spawn a player in a spot appropriate for the gametype --
// or a not-so-appropriate spot, if it initially fails
// due to a lack of starts open or something.
//
void G_SpawnPlayer(int32_t playernum, dboolean starpost)
{
	if (!playeringame[playernum])
		return;

	P_SpawnPlayer(playernum);
	G_MovePlayerToSpawnOrStarpost(playernum, starpost);
	LUA_HookPlayer(&players[playernum], HOOK(PlayerSpawn)); // Lua hook for player spawning :)
}

void G_MovePlayerToSpawnOrStarpost(int32_t playernum, dboolean starpost)
{
	if ((leveltime > starttime) && starpost)
		P_MovePlayerToStarpost(playernum);
	else
		P_MovePlayerToSpawn(playernum, G_FindMapStart(playernum));
}

mapthing_t *G_FindTeamStart(int32_t playernum)
{
	const dboolean doprints = P_IsLocalPlayer(&players[playernum]);
	int32_t i,j;

	if (!numredctfstarts && !numbluectfstarts) //why even bother, eh?
	{
		if ((gametypes[gametype]->rules & GTR_TEAMSTARTS) && doprints)
			CONS_Alert(CONS_WARNING, M_GetText("No CTF starts in this map!\n"));
		return NULL;
	}

	if ((!players[playernum].ctfteam && numredctfstarts && (!numbluectfstarts || P_RandomChance(PR_PLAYERSTARTS, FRACUNIT/2))) || players[playernum].ctfteam == 1) //red
	{
		if (!numredctfstarts)
		{
			if (doprints)
				CONS_Alert(CONS_WARNING, M_GetText("No Red Team starts in this map!\n"));
			return NULL;
		}

		for (j = 0; j < 32; j++)
		{
			i = P_RandomKey(PR_PLAYERSTARTS, numredctfstarts);
			if (G_CheckSpot(playernum, redctfstarts[i]))
				return redctfstarts[i];
		}

		if (doprints)
			CONS_Alert(CONS_WARNING, M_GetText("Could not spawn at any Red Team starts!\n"));
		return NULL;
	}
	else if (!players[playernum].ctfteam || players[playernum].ctfteam == 2) //blue
	{
		if (!numbluectfstarts)
		{
			if (doprints)
				CONS_Alert(CONS_WARNING, M_GetText("No Blue Team starts in this map!\n"));
			return NULL;
		}

		for (j = 0; j < 32; j++)
		{
			i = P_RandomKey(PR_PLAYERSTARTS, numbluectfstarts);
			if (G_CheckSpot(playernum, bluectfstarts[i]))
				return bluectfstarts[i];
		}
		if (doprints)
			CONS_Alert(CONS_WARNING, M_GetText("Could not spawn at any Blue Team starts!\n"));
		return NULL;
	}
	//should never be reached but it gets stuff to shut up
	return NULL;
}

mapthing_t *G_FindBattleStart(int32_t playernum)
{
	const dboolean doprints = P_IsLocalPlayer(&players[playernum]);
	int32_t i, j;

	if (numdmstarts)
	{
		if (modeattacking == ATTACKING_ITEMBREAK)
		{
			if (G_CheckSpot(playernum, deathmatchstarts[0]))
				return deathmatchstarts[0];

			if (doprints)
				CONS_Alert(CONS_WARNING, M_GetText("Could not spawn at any Deathmatch starts for Item Breaker!\n"));

			return NULL;
		}
		else
		{

			for (j = 0; j < 64; j++)
			{
				i = P_RandomKey(PR_PLAYERSTARTS, numdmstarts);
				if (G_CheckSpot(playernum, deathmatchstarts[i]))
					return deathmatchstarts[i];
			}
			if (doprints)
				CONS_Alert(CONS_WARNING, M_GetText("Could not spawn at any Deathmatch starts!\n"));
			return NULL;
		}
	}

	if ((gametypes[gametype]->rules & GTR_BATTLESTARTS) && doprints)
		CONS_Alert(CONS_WARNING, M_GetText("No Deathmatch starts in this map!\n"));
	return NULL;
}

mapthing_t *G_FindRaceStart(int32_t playernum)
{
	const dboolean doprints = P_IsLocalPlayer(&players[playernum]) && gametype !=  GT_FREEROAM;

	if (numcoopstarts)
	{
		uint8_t i;
		uint8_t pos = 0;

		// SRB2Kart: figure out player spawn pos from points
		if (!playeringame[playernum] || players[playernum].spectator)
			return playerstarts[0]; // go to first spot if you're a spectator

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;
			if (i == playernum)
				continue;

			if (players[i].score < players[playernum].score)
			{
				uint8_t j;
				uint8_t num = 0;

				for (j = 0; j < MAXPLAYERS; j++) // I hate similar loops inside loops... :<
				{
					if (!playeringame[j] || players[j].spectator)
						continue;
					if (j == playernum)
						continue;
					if (j == i)
						continue;

					if ((netgame || (demo.playback && demo.netgame)) && cv_kartusepwrlv.value)
					{
						if (clientpowerlevels[j][PWRLV_RACE] == clientpowerlevels[i][PWRLV_RACE])
							num++;
					}
					else
					{
						if (players[j].score == players[i].score)
							num++;
					}
				}

				if (num > 1) // found dupes
					pos++;
			}
			else
			{
				if (i < playernum)
					pos++;
				else
				{
					if ((netgame || (demo.playback && demo.netgame)) && cv_kartusepwrlv.value)
					{
						if (clientpowerlevels[i][PWRLV_RACE] > clientpowerlevels[playernum][PWRLV_RACE])
							pos++;
					}
					else
					{
						if (players[i].score > players[playernum].score)
							pos++;
					}
				}
			}
		}

		if (G_CheckSpot(playernum, playerstarts[pos % numcoopstarts]))
			return playerstarts[pos % numcoopstarts];

		// Your spot isn't available? Find whatever you can get first.
		for (i = 0; i < numcoopstarts; i++)
		{
			if (G_CheckSpot(playernum, playerstarts[i]))
				return playerstarts[i];
		}

		// SRB2Kart: We have solid players, so this behavior is less ideal.
		// Don't bother checking to see if the player 1 start is open.
		// Just spawn there.
		//return playerstarts[0];

		//this section courtesy of fickle - v1.1 battle royale
		// screw collision chex
		return playerstarts[pos % numcoopstarts];

		// if (doprints)
		// 	CONS_Alert(CONS_WARNING, M_GetText("Could not spawn at any Race starts!\n"));
		// return NULL;
	}

	if (modeattacking != ATTACKING_ITEMBREAK)
	{
		if (doprints)
			CONS_Alert(CONS_WARNING, M_GetText("No Race starts in this map!\n"));
	}
	return NULL;
}

// Find a Co-op start, or fallback into other types of starts.
static inline mapthing_t *G_FindRaceStartOrFallback(int32_t playernum)
{
	mapthing_t *spawnpoint = NULL;
	if (!(spawnpoint = G_FindRaceStart(playernum)) // find a Race start
	&& !(spawnpoint = G_FindBattleStart(playernum))) // find a DM start
		spawnpoint = G_FindTeamStart(playernum); // fallback
	return spawnpoint;
}

// Find a Match start, or fallback into other types of starts.
static inline mapthing_t *G_FindBattleStartOrFallback(int32_t playernum)
{
	mapthing_t *spawnpoint = NULL;
	if (!(spawnpoint = G_FindBattleStart(playernum)) // find a DM start
	&& !(spawnpoint = G_FindTeamStart(playernum))) // find a CTF start
		spawnpoint = G_FindRaceStart(playernum); // fallback
	return spawnpoint;
}

static inline mapthing_t *G_FindTeamStartOrFallback(int32_t playernum)
{
	mapthing_t *spawnpoint = NULL;
	if (!(spawnpoint = G_FindTeamStart(playernum)) // find a CTF start
	&& !(spawnpoint = G_FindBattleStart(playernum))) // find a DM start
		spawnpoint = G_FindRaceStart(playernum); // fallback
	return spawnpoint;
}

mapthing_t *G_FindMapStart(int32_t playernum)
{
	mapthing_t *spawnpoint;

	if (!playeringame[playernum])
		return NULL;

	// -- Spectators --
	// Order in platform gametypes: Race->DM->CTF
	// And, with deathmatch starts: DM->CTF->Race
	if (players[playernum].spectator)
	{
		// In platform gametypes, spawn in Co-op starts first
		// Overriden by GTR_BATTLESTARTS.
		if (gametypes[gametype]->rules & GTR_BATTLESTARTS && bossinfo.boss == false)
			spawnpoint = G_FindBattleStartOrFallback(playernum);
		else
			spawnpoint = G_FindRaceStartOrFallback(playernum);
	}
	else if (gametype == GT_FREEROAM)
		spawnpoint = G_FindRaceStartOrFallback(playernum);

	// -- Grand Prix / Time Attack --
	// Order: Race->DM->CTF
	else if (grandprixinfo.gp || modeattacking)
		spawnpoint = G_FindRaceStartOrFallback(playernum);

	// -- CTF --
	// Order: CTF->DM->Race
	else if ((gametypes[gametype]->rules & GTR_TEAMSTARTS) && players[playernum].ctfteam)
		spawnpoint = G_FindTeamStartOrFallback(playernum);

	// -- DM/Tag/CTF-spectator/etc --
	// Order: DM->CTF->Race
	else if (gametypes[gametype]->rules & GTR_BATTLESTARTS)
		spawnpoint = G_FindBattleStartOrFallback(playernum);

	// -- Other game modes --
	// Order: Race->DM->CTF
	else
		spawnpoint = G_FindRaceStartOrFallback(playernum);

	//No spawns found. ANYWHERE.
	if (!spawnpoint)
	{
		if (nummapthings)
		{
			if (P_IsLocalPlayer(&players[playernum]))
				CONS_Alert(CONS_ERROR, M_GetText("No player spawns found, spawning at the first mapthing!\n"));
			spawnpoint = &mapthings[0];
		}
		else
		{
			if (P_IsLocalPlayer(&players[playernum]))
				CONS_Alert(CONS_ERROR, M_GetText("No player spawns found, spawning at the origin!\n"));
		}
	}

	return spawnpoint;
}

// Go back through all the projectiles and remove all references to the old
// player mobj, replacing them with the new one.
void G_ChangePlayerReferences(mobj_t *oldmo, mobj_t *newmo)
{
	thinker_t *th;
	mobj_t *mo2;

	I_Assert((oldmo != NULL) && (newmo != NULL));

	// scan all thinkers
	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo2 = (mobj_t *)th;

		if (!(mo2->flags & MF_MISSILE))
			continue;

		if (mo2->target == oldmo)
		{
			P_SetTarget(&mo2->target, newmo);
			mo2->flags2 |= MF2_BEYONDTHEGRAVE; // this mobj belongs to a player who has reborn
		}
	}
}

//
// G_DoReborn
//
void G_DoReborn(int32_t playernum)
{
	player_t *player = &players[playernum];

	if (!K_UsingLegacyCheckpoints() && player->lastsafelap != player->laps)
	{
		// no lap cheating! but more importantly, make sure your lap count matches your respawn point
		player->laps = player->lastsafelap;
		player->starpostnum = player->lastsafestarpost;
	}

	// waypoint maps don't need to force respawning at player starts nearly as often
	const dboolean sprint = K_UsingLegacyCheckpoints() ? mapheaderinfo[gamemap - 1]->levelflags & LF_SECTIONRACE && player->laps > 1 : player->laps > 0;
	const dboolean starpost = !player->spectator && (sprint || player->starpostnum || (!K_UsingLegacyCheckpoints() && player->starposttime));

	// Make sure objectplace is OFF when you first start the level!
	OP_ResetObjectplace();

	{
		// respawn at the start
		mobj_t *oldmo = NULL;

		// first dissasociate the corpse
		if (player->mo)
		{
			oldmo = player->mo;
			// Don't leave your carcass stuck 10-billion feet in the ground!
			P_RemoveMobj(player->mo);
			P_SetTarget(&player->mo, NULL);
		}

		G_SpawnPlayer(playernum, starpost);
		if (oldmo)
			G_ChangePlayerReferences(oldmo, players[playernum].mo);

		if (!demo.playback && playernum == consoleplayer)
			kartstats.respawns++;
	}

	ACS_RunPlayerEnterScript(player);
}

void G_AddPlayer(int32_t playernum, int32_t console)
{
	CL_ClearPlayer(playernum);
	G_DestroyParty(playernum);

	playeringame[playernum] = true;

	playerconsole[playernum] = console;
	G_BuildLocalSplitscreenParty(playernum);

	player_t *newplayer = &players[playernum];

	newplayer->playerstate = PST_REBORN;
	newplayer->jointime = 0;

	demo_extradata[playernum] |= DXD_ADDPLAYER; // Set everything
}

void G_SpectatePlayerOnJoin(int32_t playernum)
{
	// This is only ever called shortly after the above.
	// That calls CL_ClearPlayer, so spectator is false by default

	if (!netgame && !G_GametypeHasTeams() && !G_GametypeHasSpectators())
		return;

	// These are handled automatically elsewhere
	if (demo.playback || players[playernum].bot)
		return;

	uint8_t i;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;

		// Spectators are of no consequence
		if (players[i].spectator)
			continue;

		// Prevent splitscreen hosters/joiners from only adding 1 player at a time in empty servers (this will also catch yourself)
		if (!players[i].jointime)
			continue;

		// A ha! An established player! It's time to spectate
		players[playernum].spectator = true;
		break;
	}
}

void G_BeginLevelExit(void)
{
	if (g_exit.hasfinished)
		return; // the round has already finished!

	g_exit.hasfinished = true;
	g_exit.losing = true;
	g_exit.retry = false;

	if (!G_GametypeUsesLives() || skipstats != 0)
	{
		g_exit.losing = false; // never force a retry
	}
	else
	{
		uint8_t i;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && !players[i].spectator && !players[i].bot)
			{
				if (!K_IsPlayerLosing(&players[i]))
				{
					g_exit.losing = false;
					break;
				}
			}
		}
	}

	if (g_exit.losing)
	{
		// You didn't win...

		uint8_t i;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && !players[i].spectator && !players[i].bot)
			{
				if (players[i].lives > 0)
				{
					g_exit.retry = true;
					break;
				}
			}
		}

		if (!g_exit.retry)
		{
			ACS_RunGameOverScript();
		}
	}
}

void G_FinishExitLevel(void)
{
	if (gamestate == GS_LEVEL)
	{
		G_ResetAllControllerRumbles();

		if (g_exit.retry)
		{
			// Restart cup here whenever we do Online GP
			if (!netgame)
			{
				// We have lives, just redo this one course.
				G_SetRetryFlag();
				return;
			}
		}
		else if (g_exit.losing)
		{
			// We were in a Special Stage.
			// We can still progress to the podium when we game over here.
			const dboolean special = grandprixinfo.gp == true && grandprixinfo.eventmode == GPEVENT_SPECIAL;

			if (!netgame && !special)
			{
				// Back to the menu with you.
				D_QuitNetGame();
				CL_Reset();
				D_StartTitle();
				return;
			}
		}

		gameaction = ga_completed;
		lastdraw = true;

		// If you want your teams scrambled on map change, start the process now.
		// The teams will scramble at the start of the next round.
		if (cv_scrambleonchange.value && G_GametypeHasTeams())
		{
			if (server)
				CV_SetValue(&cv_teamscramble, cv_scrambleonchange.value);
		}

		CON_LogMessage(M_GetText("The round has ended.\n"));

		// Remove CEcho text on round end.
		HU_ClearCEcho();

		// Don't save demos immediately here! Let standings write first
	}
	else if (gamestate == GS_CREDITS || gamestate == GS_BLANCREDITS || gamestate == GS_SECRETCREDITS)
	{
		F_StartGameEvaluation();
	}
}

static gametype_t defaultgametypes[] =
{
	// GT_RACE
	{
		"Race",
		"GT_RACE",
		GTR_CIRCUIT|GTR_BOTS|GTR_RINGS|GTR_ENCORE|GTR_RACEODDS,
		TOL_RACE,
		int_race,
		0,
		0,
		V_SKYMAP,
	},
	{
		"Battle",
		"GT_BATTLE",
		GTR_BUMPERS|GTR_POINTS|GTR_RINGS|GTR_KARMA|GTR_WANTED|GTR_WANTEDSPB|GTR_ITEMARROWS|GTR_ITEMBREAKER|GTR_BATTLESTARTS|GTR_TIMELIMIT|GTR_POINTLIMIT|GTR_BATTLEODDS|GTR_CLOSERPLAYERS|GTR_BATTLEBOXES|GTR_BATTLESPEED|GTR_DOUBLEDFLASHTICS,
		TOL_BATTLE,
		int_battle,
		0,
		2,
		V_REDMAP,
	},
	{
		"Free Roam",
		"GT_FREEROAM",
		GTR_RINGS|GTR_ENCORE|GTR_NOCOUNTDOWN,
		TOL_RACE|TOL_BATTLE|TOL_SPECIAL,
		int_none,
		0,
		0,
		V_PURPLEMAP,
	},
};

gametype_t *gametypes[MAXGAMETYPES+1] =
{
	&defaultgametypes[GT_RACE],
	&defaultgametypes[GT_BATTLE],
	&defaultgametypes[GT_FREEROAM],
};

//
// G_GetGametypeByName
//
// Returns the number for the given gametype name string, or -1 if not valid.
//
int32_t G_GetGametypeByName(const char *gametypestr)
{
	int32_t i = 0;

	while (gametypes[i] != NULL)
	{
		if (!stricmp(gametypestr, gametypes[i]->name))
			return i;
		i++;
	}

	return -1; // unknown gametype
}

//
// G_GuessGametypeByTOL
//
// Returns the first valid number for the given typeoflevel, or -1 if not valid.
//
int32_t G_GuessGametypeByTOL(uint32_t tol)
{
	int32_t i = 0;

	while (gametypes[i] != NULL)
	{
		if (tol & gametypes[i]->tol)
			return i;
		i++;
	}

	return -1; // unknown gametype
}

//
// G_SetGametype
//
// Set a new gametype, also setting gametype rules accordingly. Yay!
//
void G_SetGametype(int16_t gtype)
{
	if (gtype < 0 || gtype > numgametypes)
	{
		I_Error("G_SetGametype: Bad gametype change %d (was %d/\"%s\")", gtype, gametype, gametypes[gametype]->name);
	}

	gametype = gtype;
}

//
// G_PrepareGametypeConstant
//
// Self-explanatory. Filters out "bad" characters.
//
char *G_PrepareGametypeConstant(const char *newgtconst)
{
	size_t r = 0; // read
	size_t w = 0; // write
	size_t len = strlen(newgtconst);
	char *gtconst = Z_Calloc(len + 4, PU_STATIC, NULL);
	char *tmpconst = Z_Calloc(len + 1, PU_STATIC, NULL);

	// Copy the gametype name.
	strcpy(tmpconst, newgtconst);

	// Make uppercase.
	strupr(tmpconst);

	// Prepare to write the new constant string now.
	strcpy(gtconst, "GT_");

	// Remove characters that will not be allowed in the constant string.
	for (; r < strlen(tmpconst); r++)
	{
		dboolean writechar = true;
		char rc = tmpconst[r];
		switch (rc)
		{
			// Space, at sign and question mark
			case ' ':
			case '@':
			case '?':
				// Used for operations
			case '+':
			case '-':
			case '*':
			case '/':
			case '%':
			case '^':
			case '&':
			case '!':
				// Part of Lua's syntax
			case '#':
			case '=':
			case '~':
			case '<':
			case '>':
			case '(':
			case ')':
			case '{':
			case '}':
			case '[':
			case ']':
			case ':':
			case ';':
			case ',':
			case '.':
				writechar = false;
				break;
		}
		if (writechar)
		{
			gtconst[3 + w] = rc;
			w++;
		}
	}

	// Free the temporary string.
	Z_Free(tmpconst);

	// Finally, return the constant string.
	return gtconst;
}

//
// G_UpdateGametypeSelections
//
// Updates gametype_cons_t.
//
void G_UpdateGametypeSelections(void)
{
	int32_t i;
	for (i = 0; i < numgametypes; i++)
	{
		gametype_cons_t[i].value = i;
		gametype_cons_t[i].strvalue = gametypes[i]->name;
	}
	gametype_cons_t[numgametypes].value = 0;
	gametype_cons_t[numgametypes].strvalue = NULL;
}

tolinfo_t TYPEOFLEVEL[NUMTOLNAMES] = {
	{"RACE",TOL_RACE},
	{"BATTLE",TOL_BATTLE},
	{"BOSS",TOL_BOSS},
	{"SPECIAL",TOL_SPECIAL},
	{"TV",TOL_TV},

	// Compat stuff
	{"MATCH",TOL_BATTLE},
	{"SOLO",TOL_COMPAT1},
	{"SP",TOL_COMPAT1},
	{"SINGLEPLAYER",TOL_COMPAT1},
	{"SINGLE",TOL_COMPAT1},
	{"COOP",TOL_COMPAT2},
	{"CO-OP",TOL_COMPAT2},
	{"COMPETITION",TOL_COMPAT3},
	{"TAG",TOL_COMPAT4},
	{"CTF",TOL_COMPAT5},
	{"CUSTOM",TOL_COMPAT6},

	{NULL, 0}
};

uint32_t lastcustomtol = (TOL_TV<<1);

//
// G_AddTOL
//
// Adds a type of level.
//
void G_AddTOL(uint32_t newtol, const char *tolname)
{
	int32_t i;
	for (i = 0; TYPEOFLEVEL[i].name; i++)
		;

	TYPEOFLEVEL[i].name = Z_StrDup(tolname);
	TYPEOFLEVEL[i].flag = newtol;
}

//
// G_IsSpecialStage
//
// Returns TRUE if
// the given map is a special stage.
//
dboolean G_IsSpecialStage(mapnum_t mapnum)
{
	mapnum--; // gamemap-based to 0 indexed

	if (mapnum > nummapheaders || !mapheaderinfo[mapnum])
		return false;

	if (!mapheaderinfo[mapnum]->cup || mapheaderinfo[mapnum]->cup->cachedlevels[CUPCACHE_SPECIAL] != mapnum)
		return false;

	return true;
}

//
// G_GametypeUsesLives
//
// Returns true if the current gametype uses
// the lives system.  False otherwise.
//
dboolean G_GametypeUsesLives(void)
{
	if (modeattacking || metalrecording) // NOT in Record Attack
		return false;

	if ((grandprixinfo.gp == true) // In Grand Prix
		&& (gametype == GT_RACE) // NOT in bonus round
		&& grandprixinfo.eventmode == GPEVENT_NONE) // NOT in bonus
	{
		return true;
	}

	if (bossinfo.boss == true) // Fighting a boss?
	{
		return true;
	}

	return false;
}

//
// G_GametypeHasTeams
//
// Returns true if the current gametype uses
// Red/Blue teams.  False otherwise.
//
dboolean G_GametypeHasTeams(void)
{
	if (gametypes[gametype]->rules & GTR_TEAMS)
	{
		// Teams forced on by this gametype
		return true;
	}
	else if (gametypes[gametype]->rules & GTR_NOTEAMS)
	{
		// Teams forced off by this gametype
		return false;
	}

	// Teams are determined by the "teamplay" modifier!
	return false; // teamplay
}

//
// G_GametypeHasSpectators
//
// Returns true if the current gametype supports
// spectators.  False otherwise.
//
dboolean G_GametypeHasSpectators(void)
{
	return (netgame || (multiplayer && demo.netgame));
}

//
// G_SometimesGetDifferentGametype
//
// Oh, yeah, and we sometimes flip encore mode on here too.
//
int16_t G_SometimesGetDifferentGametype(uint8_t prefgametype)
{
	// Most of the gametype references in this condition are intentionally not prefgametype.
	// This is so a server CAN continue playing a gametype if they like the taste of it.
	// The encore check needs prefgametype so can't use G_RaceGametype...
	dboolean encorepossible = ((M_SecretUnlocked(SECRET_ENCORE) || encorescramble == 1)
		&& (gametypes[prefgametype]->rules & GTR_CIRCUIT));
	uint8_t encoremodifier = 0;

	if (encorepossible)
	{
		if (encorescramble != -1)
		{
			encorepossible = (dboolean)encorescramble; // FORCE to what was scrambled on intermission
		}
		else
		{
			switch (cv_kartvoterulechanges.value)
			{
				case 3: // always
					encorepossible = true;
					break;
				case 2: // frequent
					encorepossible = M_RandomChance(FRACUNIT>>1);
					break;
				case 1: // sometimes
					encorepossible = M_RandomChance(FRACUNIT>>2);
					break;
				default:
					break;
			}
		}
		if (encorepossible != (cv_kartencore.value == 0))
			encoremodifier = VOTEMODIFIER_ENCORE;
	}

	if (!cv_kartvoterulechanges.value) // never
		return (gametype|encoremodifier);

	if (g_countToGametype > 0 && (cv_kartvoterulechanges.value != 3))
	{
			g_countToGametype--;
			return (gametype|encoremodifier);
	}

	switch (cv_kartvoterulechanges.value) // okay, we're having a gametype change! when's the next one, luv?
	{
			case 1: // sometimes
				g_countToGametype = 5; // per "cup"
				break;
			default:
				// fallthrough - happens when clearing buffer, but needs a reasonable countdown if cvar is modified
			case 2: // frequent
				g_countToGametype = 2; // ...every 1/2th-ish cup?
				break;
	}

	// Only this response is prefgametype-based.
	// todo custom gametypes
	if (prefgametype == GT_BATTLE)
	{
		// Intentionally does not use encoremodifier!
		if (cv_kartencore.value == 1)
			return (GT_RACE|VOTEMODIFIER_ENCORE);
		return (GT_RACE);
	}
	// This might appear wrong HERE, but the game will display the Encore possibility on the second voting choice instead.
	return (GT_BATTLE|encoremodifier);
}

//
// G_GetGametypeColor
//
// Pretty and consistent ^u^
// See also M_GetGametypeColor.
//
uint8_t G_GetGametypeColor(int16_t gt)
{
	if (modeattacking)
		return orangemap[0];

	if (gametypes[gt]->color)
		return *V_GetStringColormap(gametypes[gt]->color);

	return yellowmap[0]; // FALLBACK
}

/** Get the typeoflevel flag needed to indicate support of a gametype.
  * \param gametype The gametype for which support is desired.
  * \return The typeoflevel flag to check for that gametype.
  * \author Graue <graue@oceanbase.org>
  */
uint32_t G_TOLFlag(int32_t pgametype)
{
	if (pgametype >= 0 && pgametype < numgametypes)
		return gametypes[pgametype]->tol;
	return 0;
}

mapnum_t G_GetFirstMapOfGametype(uint8_t pgametype)
{
	mapnum_t mapnum = NEXTMAP_INVALID;

	/* G: not sure what to do with this
	if ((gametypes[pgametype]->rules & GTR_CAMPAIGN) && kartcupheaders)
	{
		mapnum = kartcupheaders->cachedlevels[0];
	}
	*/

	if (mapnum >= nummapheaders)
	{
		uint32_t tolflag = G_TOLFlag(pgametype);
		for (mapnum = 0; mapnum < nummapheaders; mapnum++)
		{
			if (!mapheaderinfo[mapnum])
				continue;
			if (mapheaderinfo[mapnum]->lumpnum == LUMPERROR)
				continue;
			if (!(mapheaderinfo[mapnum]->typeoflevel & tolflag))
				continue;
			if (mapheaderinfo[mapnum]->menuflags & LF2_HIDEINMENU)
				continue;
			if (mapheaderinfo[mapnum]->menuflags & LF2_HIDEINVOTE)
				continue;
			break;
		}
	}

	return mapnum;
}

static int32_t TOLMaps(uint8_t pgametype)
{
	int32_t num = 0;
	int32_t i;

	uint32_t tolflag = G_TOLFlag(pgametype);

	// Find all the maps that are ok
	for (i = 0; i < nummapheaders; i++)
	{
		if (!mapheaderinfo[i])
			continue;
		if (mapheaderinfo[i]->lumpnum == LUMPERROR)
			continue;
		if (!(mapheaderinfo[i]->typeoflevel & tolflag))
			continue;
		if (mapheaderinfo[i]->menuflags & LF2_HIDEINMENU) // Don't include Map Hell
			continue;
		if (mapheaderinfo[i]->menuflags & LF2_HIDEINVOTE)
			continue;
		num++;
	}

	return num;
}

/** Select a random map with the given typeoflevel flags.
  * If no map has those flags, this arbitrarily gives you map 1.
  * \param tolflags The typeoflevel flags to insist on. Other bits may
  *                 be on too, but all of these must be on.
  * \return A random map with those flags, 1-based, or 1 if no map
  *         has those flags.
  * \author Graue <graue@oceanbase.org>
  */
static mapnum_t *g_allowedMaps = NULL;

uint8_t g_countToGametype = 0;

#ifdef PARANOIA
static int32_t g_randMapStack = 0;
#endif

mapnum_t G_RandMap(uint32_t tolflags, mapnum_t pprevmap, dboolean ignoreBuffers, uint8_t maphell, dboolean callAgainSoon, mapnum_t *extBuffer)
{
	int32_t allowedMapsCount = 0;
	int32_t extBufferCount = 0;
	mapnum_t ret = 0;
	int32_t i, j;
	dboolean usehellmaps; // Only consider Hell maps in this pick

#ifdef PARANOIA
	g_randMapStack++;
#endif

	if (g_allowedMaps == NULL)
	{
		g_allowedMaps = Z_Malloc(nummapheaders * sizeof(int16_t), PU_STATIC, NULL);
	}

	if (extBuffer != NULL)
	{
		for (i = 0; extBuffer[i] != 0; i++)
		{
			extBufferCount++;
		}
	}

tryAgain:

	usehellmaps = (maphell == 0 ? false : (maphell == 2 || M_RandomChance(FRACUNIT/100))); // 1% chance of Hell

	// Find all the maps that are ok and and put them in an array.
	for (i = 0; i < nummapheaders; i++)
	{
		if (mapheaderinfo[i] == NULL || mapheaderinfo[i]->lumpnum == LUMPERROR)
		{
			// Doesn't exist?
			continue;
		}

		if (i == pprevmap)
		{
			// We were just here.
			continue;
		}

		if ((mapheaderinfo[i]->typeoflevel & tolflags) == 0)
		{
			// Doesn't match our gametype.
			continue;
		}

		if (pprevmap == UINT16_MAX)  // title demo hack
		{
			// vres GHOST_%u
			virtres_t *vRes;
			virtlump_t *vLump;
			lumpnum_t l;

			vRes = vres_GetMap(mapheaderinfo[i]->lumpnum);

			for (int k = 0; k < 10; k++)
			{
				vLump = vres_Find(vRes, va("%s/GHOST_%u",mapheaderinfo[i]->lumpname,k));

				if (vLump != NULL)
					break;
			}

			if (vLump == NULL && ((l = W_CheckNumForLongName(va("%sS01",mapheaderinfo[i]->lumpname))) == LUMPERROR))
			{
				vres_Free(vRes);
				continue;
			}

			vres_Free(vRes);
		}

		if ((!usehellmaps && ((mapheaderinfo[i]->menuflags & LF2_HIDEINMENU) == LF2_HIDEINMENU))
			|| (usehellmaps && usehellmaps != ((mapheaderinfo[i]->menuflags & LF2_HIDEINMENU) == LF2_HIDEINMENU)))
		{
			// THIS IS BAD
			continue;
		}

		if (mapheaderinfo[i]->menuflags & LF2_HIDEINVOTE)
		{
			// Sorry no voting pal.
			continue;
		}

		if (M_MapLocked(i + 1) == true)
		{
			// We haven't earned this one.
			continue;
		}

		if (ignoreBuffers == false)
		{
			if (mapheaderinfo[i]->justPlayed > 0)
			{
				// We just played this map, don't play it again.
				continue;
			}

			if (extBufferCount > 0)
			{
				// An optional additional buffer,
				// to avoid duplicates on the voting screen.
				for (j = 0; j < (maphell ? 3 : extBufferCount); j++)
				{
					if (/*extBuffer[j] < 0 ||*/extBuffer[j] >= nummapheaders)
					{
						// Rest of buffer SHOULD be empty.
						break;
					}

					if (i == extBuffer[j])
					{
						// Map is in this other buffer, don't duplicate.
						break;
					}
				}

				if (j < extBufferCount)
				{
					// Didn't make it out of this buffer, so don't add this map.
					continue;
				}
			}
		}

		// Got past the gauntlet, so we can allow this one.
		g_allowedMaps[ allowedMapsCount++ ] = i;
	}

	if (allowedMapsCount == 0)
	{
		// No maps are available.
		if (ignoreBuffers == false)
		{
			// Try again with ignoring the buffer before giving up.
			ignoreBuffers = true;
			goto tryAgain;
		}

		if (maphell)
		{
			// Any wiggle room to loosen our restrictions here?
			maphell--;
			goto tryAgain;
		}

		// Nothing else actually worked. Welp!
		// You just get whatever was added first.
		ret = 0;
	}
	else
	{
		ret = g_allowedMaps[ M_RandomKey(allowedMapsCount) ];
	}

	if (callAgainSoon == false)
	{
		Z_Free(g_allowedMaps);
		g_allowedMaps = NULL;

#ifdef PARANOIA
		// Crash if callAgainSoon was mishandled.
		I_Assert(g_randMapStack == 1);
#endif
	}

#ifdef PARANOIA
	g_randMapStack--;
#endif

	return ret;
}

#define VOTEROWSADDSONE ((cv_votemaxrows.value*3) + 1 + ((cv_votemaxrows.value > 1) ? (cv_votemaxrows.value - 1) : 0))


void G_AddMapToBuffer(mapnum_t map)
{
#if 0
	// DEBUG: make nearly everything but four race levels full justPlayed
	// to look into what happens when a dedicated runs for seven million years.
	int32_t justplayedvalue = TOLMaps(gametype) - VOTE_NUM_LEVELS;
	uint32_t tolflag = G_TOLFlag(gametype);

	// Find all the maps that are ok
	int32_t i;
	for (i = 0; i < nummapheaders; i++)
	{
		if (mapheaderinfo[i] == NULL)
		{
			continue;
		}

		if (mapheaderinfo[i]->lumpnum == LUMPERROR)
		{
			continue;
		}

		if ((mapheaderinfo[i]->typeoflevel & tolflag) == 0)
		{
			continue;
		}

		if (mapheaderinfo[i]->menuflags & LF2_HIDEINMENU)
		{
			// Don't include hidden
			continue;
		}

		if (mapheaderinfo[i]->menuflags & LF2_HIDEINVOTE)
		{
			// Don't include hidden
			continue;
		}

		// Only care about restrictions if the host is a listen server.
		if (!dedicated)
		{
			if (!(mapheaderinfo[i]->menuflags & LF2_NOVISITNEEDED)
			&& !(mapheaderinfo[i]->records.mapvisited & MV_VISITED)
			&& !(
				mapheaderinfo[i]->cup
				&& mapheaderinfo[i]->cup->cachedlevels[0] == i
			))
			{
				// Not visited OR head of cup
				continue;
			}

			if ((mapheaderinfo[i]->menuflags & LF2_FINISHNEEDED)
			&& !(mapheaderinfo[i]->records.mapvisited & MV_BEATEN))
			{
				// Not completed
				continue;
			}
		}

		if (M_MapLocked(i + 1) == true)
		{
			// We haven't earned this one.
			continue;
		}

		mapheaderinfo[i]->justPlayed = justplayedvalue;
		justplayedvalue -= 1;
		if (justplayedvalue <= 0)
			break;
	}
#else
	if (dedicated && D_NumPlayers() == 0)
		return;

	const size_t upperJustPlayedLimit = TOLMaps(gametype) - VOTEROWSADDSONE - 1;

	if (mapheaderinfo[map]->justPlayed == 0) // Started playing a new map.
	{
		// Decrement every maps' justPlayed value.
		int32_t i;
		for (i = 0; i < nummapheaders; i++)
		{
			// If the map's justPlayed value is higher
			// than what it should be, clamp it.
			// (Usually a result of SOC files
			// manipulating which levels are hidden.)
			if (mapheaderinfo[i]->justPlayed > upperJustPlayedLimit)
			{
				mapheaderinfo[i]->justPlayed = upperJustPlayedLimit;
			}
			
			if (mapheaderinfo[i]->justPlayed > 0)
			{
				mapheaderinfo[i]->justPlayed--;
			}
		}
	}

	// Set our map's justPlayed value.
	mapheaderinfo[map]->justPlayed = upperJustPlayedLimit;
#endif
}
#undef VOTEROWSADDSONE

//
// G_UpdateVisited
//
static void G_UpdateVisited(void)
{
	uint8_t i;
	//uint8_t earnedEmblems;

	// No demos.
	if (demo.playback)
		return;

	// Check if every local player wiped out.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i]) // Not here.
			continue;

		if (!P_IsLocalPlayer(&players[i])) // Not local.
			continue;

		if (players[i].spectator) // Not playing.
			continue;

		if (players[i].pflags & PF_NOCONTEST) // Sonic after not surviving.
			continue;
		break;
	}

	if (i == MAXPLAYERS) // Not a single living local soul?
		return;

	// Update visitation flags
	maprecord_t *record = G_AllocateMapRecord(G_BuildMapName(prevmap+1));
	record->visited |= MV_BEATEN;
	if (encoremode == true)
		record->visited |= MV_ENCORE;

	if (modeattacking)
		G_UpdateRecordReplays();

	// Unseal when we add more emblems
	//if ((earnedEmblems = M_CompletionEmblems()))
		//CONS_Printf(M_GetText("\x82" "Earned %hu emblem%s for level completion.\n"), (uint16_t)earnedEmblems, earnedEmblems > 1 ? "s" : "");
}

static dboolean CanSaveLevel(mapnum_t mapnum)
{
	// SRB2Kart: No save files yet
	(void)mapnum;
	return false;
}

static void G_HandleSaveLevel(void)
{
	// do this before running the intermission or custom cutscene, mostly for the sake of marathon mode but it also massively reduces redundant file save events in f_finale.c
	if (nextmap >= NEXTMAP_SPECIAL)
	{
		if (!gamecomplete)
			gamecomplete = 2; // special temporary mode to prevent using SP level select in pause menu until the intermission is over without restricting it in every intermission
		if (cursaveslot > 0)
		{
			if (marathonmode)
			{
				// don't keep a backup around when the run is done!
				if (FIL_FileExists(liveeventbackup))
					remove(liveeventbackup);
				cursaveslot = 0;
			}
			else if ((!modifiedgame || savemoddata) && !(netgame || multiplayer || demo.recording || metalrecording || modeattacking))
				G_SaveGame((uint32_t)cursaveslot, 0); // TODO when we readd a campaign one day
		}
	}
	// and doing THIS here means you don't lose your progress if you close the game mid-intermission
	else if (!(netgame || multiplayer || demo.playback || demo.recording || metalrecording || modeattacking)
		&& (!modifiedgame || savemoddata) && cursaveslot > 0 && CanSaveLevel(lastmap+1))
		G_SaveGame((uint32_t)cursaveslot, lastmap+1); // not nextmap+1 to route around special stages
}

// Next map apparatus
struct roundqueue roundqueue;

void G_MapSlipIntoRoundQueue(uint8_t position, uint16_t map, uint8_t setgametype, dboolean setencore, dboolean rankrestricted)
{
	I_Assert(position < ROUNDQUEUE_MAX);

	roundqueue.entries[position].mapnum = map;
	roundqueue.entries[position].gametype = setgametype;
	roundqueue.entries[position].encore = setencore;
	roundqueue.entries[position].rankrestricted = rankrestricted;
}

void G_MapIntoRoundQueue(uint16_t map, uint8_t setgametype, dboolean setencore, dboolean rankrestricted)
{
	if (roundqueue.size >= ROUNDQUEUE_MAX)
	{
		CONS_Alert(CONS_ERROR, "G_MapIntoRoundQueue: Unable to add map beyond %u\n", roundqueue.size);
		return;
	}

	G_MapSlipIntoRoundQueue(roundqueue.size, map, setgametype, setencore, rankrestricted);

	roundqueue.size++;
}

void G_GPCupIntoRoundQueue(cupheader_t *cup, uint8_t setgametype, dboolean setencore)
{
	uint8_t i, levelindex = 0, bonusindex = 0;
	uint8_t bonusmodulo = max(1, (cup->numlevels+1)/(cup->numbonus+1));
	uint16_t cupLevelNum;

	// Levels are added to the queue in the following pattern.
	// For 5 Race rounds and 2 Bonus rounds, the most common case:
	//    race - race - BONUS - race - race - BONUS - race
	// The system is flexible enough to permit other arrangements.
	// However, we just want to keep the pacing even & consistent.
	while (levelindex < cup->numlevels)
	{
		// Fill like two or three Race maps.
		for (i = 0; i < bonusmodulo; i++)
		{
			cupLevelNum = cup->cachedlevels[levelindex];

			if (cupLevelNum >= nummapheaders)
			{
				// For invalid Race maps, we keep the pacing by going to TEST RUN.
				// It transparently lets the user know something is wrong.
				cupLevelNum = 0;
			}

			G_MapIntoRoundQueue(
				cupLevelNum,
				setgametype,
				setencore, // *probably* correct
				false
			);

			levelindex++;
			if (levelindex >= cup->numlevels)
				break;
		}

		// Attempt to add an interstitial Bonus round.
		if (levelindex < cup->numlevels
			&& bonusindex < cup->numbonus)
		{
			cupLevelNum = cup->cachedlevels[CUPCACHE_BONUS + bonusindex];

			if (cupLevelNum < nummapheaders)
			{
				// In the case of Bonus rounds, we simply skip invalid maps.
				G_MapIntoRoundQueue(
					cupLevelNum,
					G_GuessGametypeByTOL(mapheaderinfo[cupLevelNum]->typeoflevel),
					setencore, // if this isn't correct, Got_Mapcmd will fix it
					false
				);
			}

			bonusindex++;
		}
	}

	// ...but there's one last trick up our sleeves.
	// At the end of the Cup is a Rank-restricted treat.
	// So we append it to the end of the roundqueue.
	// (as long as it exists, of course!)
	cupLevelNum = cup->cachedlevels[CUPCACHE_SPECIAL];
	if (cupLevelNum < nummapheaders)
	{
		G_MapIntoRoundQueue(
			cupLevelNum,
			G_GuessGametypeByTOL(mapheaderinfo[cupLevelNum]->typeoflevel),
			setencore, // if this isn't correct, Got_Mapcmd will fix it
			true // Rank-restricted!
		);
	}

	if (roundqueue.size == 0)
	{
		I_Error("G_CupToRoundQueue: roundqueue size is 0 after population!?");
	}
}

static uint8_t G_GetRoundQueuePosition(void)
{
	uint8_t position = roundqueue.position;
	dboolean permitrank = false;
	/*if (grandprixinfo.gp == true
		&& grandprixinfo.gamespeed >= KARTSPEED_NORMAL)
	{
		// On A rank pace? Then you get a chance for S rank!
		permitrank = (K_CalculateGPPercent(&grandprixinfo.rank) >= K_SealedStarEntryRequirement(&grandprixinfo.rank));

		// If you're on Master, a win floats you to rank-restricted levels for free.
		// (This is a different class of challenge!)
		if (grandprixinfo.masterbots && grandprixinfo.rank.position <= 1)
			permitrank = true;
	}*/

	while (position < roundqueue.size
		&& (roundqueue.entries[position].mapnum >= nummapheaders
		|| mapheaderinfo[roundqueue.entries[position].mapnum] == NULL
		|| (permitrank == false && roundqueue.entries[position].rankrestricted == true)))
	{
		// Skip all restricted queue entries.
		position++;
	}

	return position;
}

// gets the nextmap number WITHOUT ANY GODDAMN SIDE EFFECTS!
// any gametype changes or roundqueue updates should happen in G_GetNextMap or elsewhere
mapnum_t G_CheckNextMap(dboolean noadvancemap)
{
	mapnum_t currentmap = G_GamestateUsesLevel() ? gamemap-1 : prevmap;
	//dboolean spec = G_IsSpecialStage(currentmap);
	int32_t i;

	// Kill nextmapoverride if it's not a safe move
	if (!G_MapIsSafe(nextmapoverride))
	{
		// At least beat the programmer/host over the head for it
		CONS_Alert(CONS_WARNING, "Overriding map ID %d is unsafe!", nextmapoverride);
		nextmapoverride = 0;
	}

	if (nextmapoverride != 0)
	{
		return nextmapoverride-1;
	}
	else if (roundqueue.size > 0)
	{
		uint8_t position = G_GetRoundQueuePosition();
		if (position < roundqueue.size)
		{
			// The next entry in the queue is valid; set it as nextmap!
			return roundqueue.entries[position].mapnum;
		}
		else if (grandprixinfo.gp == true)
		{
			// In GP, we're now ready to go to the ceremony.
			return NEXTMAP_CEREMONY;
		}
	}
	else if (grandprixinfo.gp == true)
	{
		// Fast And Rapid Testing
		// this codepath is exclusively accessible through console/command line
		return currentmap;
	}

	// no special cases? time to cycle maps!
	mapnum_t newmap = NEXTMAP_INVALID;
	uint32_t tolflag = G_TOLFlag(gametype);
	mapnum_t cm;

	if (true/*!(gametyperules & GTR_NOCUPSELECT)*/)
	{
		cupheader_t *cup = mapheaderinfo[gamemap-1]->cup;
		uint8_t gettingresult = 0;

		while (cup)
		{
			// Not unlocked? Grab the next result afterwards
			/*if (!marathonmode && M_CupLocked(cup))
			{
				cup = cup->next;
				gettingresult = 1;
				continue;
			}*/

			for (i = 0; i < cup->numlevels; i++)
			{
				cm = cup->cachedlevels[i];

				// Not valid?
				if (cm >= nummapheaders
					|| !mapheaderinfo[cm]
					|| mapheaderinfo[cm]->lumpnum == LUMPERROR
					|| !(mapheaderinfo[cm]->typeoflevel & tolflag)
					|| (!marathonmode && M_MapLocked(cm+1)))
					continue;

				// If the map is in multiple cups, only consider the first one valid.
				if (mapheaderinfo[cm]->cup != cup)
				{
					continue;
				}

				// Grab the first valid after the map you're on
				if (gettingresult)
				{
					newmap = cm;
					gettingresult = 2;
					break;
				}

				// Not the map you're on?
				if (cm != currentmap)
				{
					continue;
				}

				// Ok, this is the current map, time to get the next
				gettingresult = 1;
			}

			// We have a good nextmap?
			if (gettingresult == 2)
			{
				break;
			}

			// Ok, iterate to the next
			cup = cup->next;
		}
	}

	// still nothing? just go through maps sequentially...
	if (newmap == NEXTMAP_INVALID)
	{
		cm = currentmap;

		do
		{
			if (++cm >= nummapheaders)
				cm = 0;

			if (!mapheaderinfo[cm]
				|| mapheaderinfo[cm]->lumpnum == LUMPERROR
				|| !(mapheaderinfo[cm]->typeoflevel & tolflag)
				|| (mapheaderinfo[cm]->menuflags & LF2_HIDEINMENU)
				|| (mapheaderinfo[cm]->menuflags & LF2_HIDEINVOTE))
			{
				continue;
			}

			if (M_MapLocked(cm + 1) == true)
			{
				// We haven't earned this one.
				continue;
			}

			break;
		} while (cm != currentmap);

		newmap = cm;
	}

	if (!noadvancemap && K_CanChangeRules(true))
	{
		switch (cv_advancemap.value)
		{
			case 0: // Stay on same map.
				return currentmap;
			case 3: // Voting screen.
				for (i = 0; i < MAXPLAYERS; i++)
				{
					if (!playeringame[i])
						continue;
					if (players[i].spectator)
						continue;
					break;
				}
				if (i != MAXPLAYERS)
					return NEXTMAP_VOTING;
			/* FALLTHRU */
			case 2: // Go to random map.
				return NEXTMAP_RANDOM;
			default:
				// Loop back around
				return newmap < NEXTMAP_SPECIAL ? newmap : G_GetFirstMapOfGametype(gametype);
		}
	}
	else
	{
		return newmap;
	}
}

// Tests if a given map number is safe to go to. If not, this returns false.
dboolean G_MapIsSafe(mapnum_t map)
{
	return (map != NEXTMAP_INVALID &&
		(map >= NEXTMAP_SPECIAL || (map < nummapheaders && mapheaderinfo[map] &&
					    mapheaderinfo[map]->lumpnum != LUMPERROR)));
}

void G_GetNextMap(void)
{
	if (!server)
	{
		// Server is authoriative, not you
		return;
	}

	if (grandprixinfo.gp)
	{
		// Inherit from GP
		deferencoremode = grandprixinfo.encore;
	}
	else if (K_CanChangeRules(true))
	{
		// Use cvar
		deferencoremode = (cv_kartencore.value == 1);
	}
	else
	{
		// Inherit from current state
		deferencoremode = encoremode;
	}

	forceresetplayers = forcespecialstage = false;

	// go to next level
	// nextmap is 0-based, unlike gamemap
	nextmap = G_CheckNextMap(false);

	if (nextmapoverride != 0)
	{
		if (nextmap < nummapheaders && mapheaderinfo[nextmap])
		{
			if ((mapheaderinfo[nextmap]->typeoflevel & G_TOLFlag(gametype)) == 0)
			{
				int32_t lastgametype = gametype;
				int32_t newgametype = G_GuessGametypeByTOL(mapheaderinfo[nextmap]->typeoflevel);
				if (newgametype == -1)
					newgametype = GT_RACE; // sensible default

				G_SetGametype(newgametype);
				D_GameTypeChanged(lastgametype);
			}

			// Roundqueue integration: Override the current entry!
			if (roundqueue.position > 0
				&& roundqueue.position <= roundqueue.size)
			{
				uint8_t entry = roundqueue.position-1;

				/*if (grandprixinfo.gp)
				{
					K_RejiggerGPRankData(
						&grandprixinfo.rank,
						roundqueue.entries[entry].mapnum,
						roundqueue.entries[entry].gametype,
						nextmap,
						gametype);
				}*/

				roundqueue.entries[entry].mapnum = nextmap;
				roundqueue.entries[entry].gametype = gametype;
				//roundqueue.entries[entry].overridden = true;
			}
		}
	}
	else if (roundqueue.size > 0)
	{
		uint8_t position = G_GetRoundQueuePosition();

		if (position < roundqueue.size)
		{
			deferencoremode = roundqueue.entries[position].encore;

			// Handle gametype changes.
			if (roundqueue.entries[position].gametype != gametype)
			{
				int32_t lastgametype = gametype;
				G_SetGametype(roundqueue.entries[position].gametype);
				D_GameTypeChanged(lastgametype);
			}

			// Is this special..?
			forcespecialstage = roundqueue.entries[position].rankrestricted;

			// On entering roundqueue mode, kill the non-PWR between-round scores.
			// This makes it viable as a future tournament mode base.
			if (position == 0)
			{
				forceresetplayers = true;
			}

			// Handle primary queue position update.
			roundqueue.position = position + 1;
			if (grandprixinfo.gp == false || gametype == GT_RACE) // roundqueue.entries[0].gametype
			{
				roundqueue.roundnum++;
			}
		}
		else
		{
			// Wipe the queue info.
			memset(&roundqueue, 0, sizeof(struct roundqueue));

			if (grandprixinfo.gp == false)
			{
				// On exiting roundqueue mode, kill the non-PWR between-round scores.
				// This prevents future tournament winners from carrying their wins out.
				forceresetplayers = true;
			}
		}

		// Make sure the next D_MapChange sends updated roundqueue state.
		roundqueue.netcommunicate = true;
	}

	// We are committed to this map now.
	if (!G_MapIsSafe(nextmap))
		I_Error("G_GetNextMap: Internal map ID %d not found (nummapheaders = %d)\n", nextmap, nummapheaders);

#if 0 // This is a surprise tool that will help us later.
	if (!spec)
#endif //#if 0
		lastmap = nextmap;
}

//
// G_DoCompleted
//
static void G_DoCompleted(void)
{
	int32_t i, j = 0;

	if (modeattacking && pausedelay)
		pausedelay = 0;

	gameaction = ga_nothing;

	if (metalplayback)
		G_StopMetalDemo();
	if (metalrecording)
		G_StopMetalRecording(false);

	G_SetGamestate(GS_NULL);
	wipegamestate = GS_NULL;

	int32_t numplayers = 0;
	int32_t numforfeit = 0;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			if ((!players[i].spectator) && (!P_MobjWasRemoved(players[i].mo)))
			{
				// Generic headcount routine
				numplayers++;
			}

			// SRB2Kart: exitlevel shouldn't get you the points
			if (!players[i].exiting && !(players[i].pflags & PF_NOCONTEST))
			{
				clientPowerAdd[i] = 0;

				// Add to the "forfeit players" counter
				numforfeit++;

				if (players[i].bot)
				{
					K_FakeBotResults(&players[i]);
				}
				else
				{
					players[i].pflags |= PF_NOCONTEST;

					if (P_IsLocalPlayer(&players[i]))
					{
						j++;
					}
				}
			}

			G_PlayerFinishLevel(i); // take away cards and stuff
		}
	}

	if (numforfeit && (numforfeit >= numplayers))
	{
		// This was likely an "exitlevel" call or RTV; DO NOT rank this race!
		cannotrank = true;
	}

	if (grandprixinfo.gp && (!grandprixinfo.ranked))
	{
		// In a Grand Prix, unless ranked races are on for it, the rank is just for show. Don't rank!
		cannotrank = true;
	}

	if (!cannotrank)
		K_GetRankBasedPositions();

	// See Y_StartIntermission timer handling
	if ((gametypes[gametype]->rules & GTR_CIRCUIT) && ((multiplayer && demo.playback) || j == r_splitscreen+1) && (!K_CanChangeRules(false) || cv_inttime.value > 0))
	// play some generic music if there's no win/cool/lose music going on (for exitlevel commands)
	{
		S_ChangeMusicInternal("racent", true);
		S_ShowMusicCredit(-30*FRACUNIT, 5*TICRATE, V_SNAPTOTOP);
	}

	if (automapactive)
		AM_Stop();

	S_StopSounds();

	prevmap = (mapnum_t)(gamemap-1);

	if (!demo.playback)
	{
		// Set up power level gametype scrambles
		K_SetPowerLevelScrambles(K_UsingPowerLevels());
	}

	// If the current gametype has no intermission screen set, then don't start it.
	Y_DetermineIntermissionType();

	if ((skipstats && !modeattacking)
		|| (modeattacking && (players[consoleplayer].pflags & PF_NOCONTEST))
		|| (intertype == int_none))
	{
		G_UpdateVisited();
		G_AfterIntermission();
	}
	else
	{
		G_SetGamestate(GS_INTERMISSION);
		Y_StartIntermission();
		G_UpdateVisited();
	}
}

// See also F_EndCutscene, the only other place which handles intra-map/ending transitions
void G_AfterIntermission(void)
{
	Y_CleanupScreenBuffer();

	if (gamecomplete == 2) // special temporary mode to prevent using SP level select in pause menu until the intermission is over without restricting it in every intermission
		gamecomplete = 1;

	HU_ClearCEcho();

	if (demo.savemode == DSM_TITLEENTRY)
		M_AbortVirtualKeyboard();

	if (demo.playback)
	{
		G_StopDemo();

		if (demo.inreplayhut)
		{
			// Setup menu stack so we can back out of the menu as per usual.
			M_RestoreMenuStack(false);
			modeattacking = ATTACKING_MENU;
		}
		else
		{
			M_ClearMenusEx(false, true);
			D_ClearState();
			M_StartControlPanel();
		}

		return;
	}
	else if (demo.recording && (modeattacking || demo.savemode != DSM_NOTSAVING))
	{
		G_SetAutoSaveName();
		G_SaveDemo();
	}

	if (modeattacking) // End the run.
	{
		MR_ModeAttackEndGame(0);
		return;
	}

	if (gamestate != GS_VOTING)
	{
		G_GetNextMap();
		G_HandleSaveLevel();
	}

	if (grandprixinfo.gp == true && mapheaderinfo[prevmap]->cutscenenum && !modeattacking && skipstats <= 1 && (gamecomplete || !(marathonmode & MA_NOCUTSCENES))) // Start a custom cutscene.
		F_StartCustomCutscene(mapheaderinfo[prevmap]->cutscenenum-1, false, false);
	else
	{
		G_NextLevel();
	}
}

//
// G_NextLevel (WorldDone)
//
// init next level or go to the final scene
// called by end of intermission screen (y_inter)
//
void G_NextLevel(void)
{
	if (nextmap == NEXTMAP_RANDOM)
	{
		nextmap = G_RandMap(G_TOLFlag(gametype), prevmap, false, 0, false, NULL);
	}
	else if (nextmap >= NEXTMAP_SPECIAL)
	{
		// TODO: remove when we have podium
		if (roundqueue.writetextmap == true && roundqueue.size == 0)
		{
			S_StartSound(NULL, sfx_yeeeah);

			CONS_Printf("\n\n\x83""writetextmap: Find your TEXTMAPs in %s\n", srb2home);

			roundqueue.writetextmap = false;
		}

		G_EndGame();
		return;
	}

	gameaction = ga_worlddone;
}

static void G_DoWorldDone(void)
{
	if (server)
	{
		// SRB2Kart
		D_MapChange(nextmap+1,
			gametype,
			deferencoremode,
			forceresetplayers,
			0,
			false,
			forcespecialstage);
	}

	gameaction = ga_nothing;
}

//
// G_DoStartVote
//
static void G_DoStartVote(void)
{
	if (server)
	{
		if (gamestate == GS_VOTING)
			I_Error("G_DoStartVote: NEXTMAP_VOTING causes recursive vote!");
		D_SetupVote();
	}
	gameaction = ga_nothing;
}

//
// G_UseContinue
//
void G_UseContinue(void)
{
	if (gamestate == GS_LEVEL && !netgame && !multiplayer)
	{
		gameaction = ga_startcont;
		lastdraw = true;
	}
}

static void G_DoStartContinue(void)
{
	I_Assert(!netgame && !multiplayer);

	legitimateexit = false;
	G_PlayerFinishLevel(consoleplayer); // take away cards and stuff

	//F_StartContinue();
	gameaction = ga_nothing;
}

//
// G_Continue
//
// re-init level, used by continue and possibly countdowntimeup
//
void G_Continue(void)
{
	if (!netgame && !multiplayer)
		gameaction = ga_continued;
}

static void G_DoContinued(void)
{
	player_t *pl = &players[consoleplayer];
	I_Assert(!netgame && !multiplayer);
	//I_Assert(pl->continues > 0);

	/*if (pl->continues)
		pl->continues--;*/

	// Reset score
	pl->score = 0;

	// Allow tokens to come back
	tokenlist = 0;
	token = 0;

	if (!(netgame || multiplayer || demo.playback || demo.recording || metalrecording || modeattacking) && (!modifiedgame || savemoddata) && cursaveslot > 0)
		G_SaveGameOver((uint32_t)cursaveslot, true);

	// Reset # of lives
	pl->lives = 3;

	D_MapChange(gamemap, gametype, false, false, 0, false, false);

	gameaction = ga_nothing;
}

//
// G_EndGame (formerly Y_EndGame)
// Frankly this function fits better in g_game.c than it does in y_inter.c
//
// ...Gee, (why) end the game?
// Because G_AfterIntermission and F_EndCutscene would
// both do this exact same thing *in different ways* otherwise,
// which made it so that you could only unlock Ultimate mode
// if you had a cutscene after the final level and crap like that.
// This function simplifies it so only one place has to be updated
// when something new is added.
void G_EndGame(void)
{
	// Handle voting
	if (nextmap == NEXTMAP_VOTING)
	{
		gameaction = ga_startvote;
		return;
	}

	// Only do evaluation and credits in singleplayer contexts
	if (!netgame && grandprixinfo.gp == true)
	{
		if (nextmap == NEXTMAP_CEREMONY) // end game with ceremony
		{
			//D_StartTitle(); //F_StartEnding(); -- temporary
			M_RestoreMenuStack(false);
			return;
		}
		if (nextmap == NEXTMAP_CREDITS) // end game with credits
		{
			F_StartCredits();
			return;
		}
		if (nextmap == NEXTMAP_EVALUATION) // end game with evaluation
		{
			F_StartGameEvaluation();
			return;
		}
	}

	// In a netgame, don't unwittingly boot everyone.
	if (netgame)
	{
		S_StopMusic();
		G_SetGamestate(GS_WAITINGPLAYERS); // hack to prevent a command repeat

		if (server)
		{
			uint16_t map = G_GetFirstMapOfGametype(gametype)+1;

			if (map > nummapheaders)
				I_Error("G_EndGame: No valid map ID found!?");

			COM_BufAddText(va("map %s\n", G_BuildMapName(map)));
		}

		return;
	}

	// Time to return to the menu.
	M_ClearMenusEx(false, true);
	D_ClearState();
	M_StartControlPanel();
}

//
// G_LoadGameSettings
//
// Sets a tad of default info we need.
void G_LoadGameSettings(void)
{
	// initialize free sfx slots for skin sounds
	S_InitRuntimeSounds();
}

#define GD_VERSIONCHECK 0xAAAAA002

static void DataCorrupt(uint8_t *corrupted)
{
	if (*corrupted)
	{
		const char *gdfolder = "the SRB2Kart folder";
		if (!fastcmp(srb2home,"."))
			gdfolder = srb2home;

		I_Error(*corrupted == 1
		      ? "Game data is not for BlanKart.\nDelete %s (maybe in %s) and try again."
		      : "Corrupt game data file.\nDelete %s (maybe in %s) and try again.",
		        gamedatafilename, gdfolder);
	}
}

static dboolean LoadLegacyRecords(savebuffer_t *save)
{
	uint32_t numgamedatamapheaders = READUINT32(save->p);
	if (numgamedatamapheaders >= NEXTMAP_SPECIAL)
		return false;

	for (uint32_t i = 0; i < numgamedatamapheaders; i++)
	{
		char mapname[MAXMAPLUMPNAME];
		//mapnum_t mapnum;
		tic_t rectime;
		tic_t reclap;

		READSTRINGN(save->p, mapname, sizeof(mapname));
		//mapnum = G_MapNumber(mapname);

		uint8_t rtemp = READUINT8(save->p);

		maprecord_t *record = G_GetMapRecord(mapname);
		if (record != NULL)
			return false; // duped
		record = G_AllocateMapRecord(mapname);
		record->visited = rtemp;
		record->playtime = record->roundsplayed = record->roundswon = 0;
		if (record->visited & ~MV_MAX)
			return false; // invalid flags

		for (int8_t k = 0; k < 4; k++) // MAXMAPRECORDS
		{
			rectime = (tic_t)READUINT32(save->p);
			reclap  = (tic_t)READUINT32(save->p);
			if (k == 3)
				continue;

			//if (mapnum < nummapheaders && mapheaderinfo[mapnum])
			{
				// Valid mapheader, time to populate with record data.

				//if ((mapheaderinfo[mapnum]->mapvisited = rtemp) & ~MV_MAX)
					//return false;

				if (rectime || reclap)
				{
					const char *prename = k == 0 ? "kart" : k == 1 ? "tech" : "blankart";
					uint8_t version = k == 0 ? 1 : 2;

					maprecordpreset_t *preset = G_GetMapRecordPreset(record, prename);
					if (preset != NULL)
						return false; // duped
					preset = G_AllocateMapRecordPreset(record, prename, version);
					preset->besttime = rectime;
					preset->bestlap = reclap;
					preset->playtime = 0;
					CONS_Printf("Map Record %d, ID %d, Time = %d, Lap = %d\n", k, i, rectime/35, reclap/35);
				}
			}
			//else
			{
				// Since it's not worth declaring the entire gamedata
				// corrupt over extra maps, we report and move on.
				//CONS_Alert(CONS_WARNING, "Map with lumpname %s does not exist, time record data will be discarded\n", mapname);
			}
		}
	}
	return true;
}

// G_LoadGameData
// Loads the main data file, which stores information such as emblems found, etc.
void G_LoadGameData(void)
{
	CLEANUP(DataCorrupt) uint8_t corrupted = 0; // calls I_Error if non-zero on return
	uint32_t i, j;
	uint8_t modded = false;
	uint8_t rtemp;
	CLEANUP(P_SaveBufferFree) savebuffer_t save = {0};
	dboolean beforepresets = false;
	dboolean beforemorestats = false;

	// Clear things so previously read gamedata doesn't transfer
	// to new gamedata
	G_ClearRecords(); // main and nights records
	M_ClearSecrets(); // emblems, unlocks, maps visited, etc
	K_EraseStats(); // stats

	for (i = 0; i < PWRLV_NUMTYPES; i++) // SRB2Kart: online rank system
		vspowerlevel[i] = PWRLVRECORD_START;

	if (M_CheckParm("-nodata"))
		return; // Don't load.

	// Allow saving of gamedata beyond this point
	gamedataloaded = true;

	if (M_CheckParm("-gamedata") && M_IsNextParm())
	{
		strlcpy(gamedatafilename, M_GetNextParm(), sizeof gamedatafilename);
	}

	if (M_CheckParm("-resetdata"))
		return; // Don't load (essentially, reset).

	if (P_SaveBufferFromFile(&save, va(pandf, srb2home, gamedatafilename)) == false)
		return;

	corrupted = 1; // not for this game

	// Version check
	uint32_t version = READUINT32(save.p);
	if (version == 0xBA5ED444)
	{
		beforepresets = true;
		beforemorestats = true;
	}
	else if (version == 0xAAAAA001)
	{
		beforemorestats = true;
	}
	else if (version != GD_VERSIONCHECK)
		return;

	corrupted = 2; // data is corrupted!?

	// well no clue but dont think it would like reading garbage from vanilla files
	K_ReadStats(&save, beforemorestats ? !savemoddata : false);

	for (i = 0; i < PWRLV_NUMTYPES; i++)
	{
		vspowerlevel[i] = READUINT16(save.p);
		if (vspowerlevel[i] < PWRLVRECORD_MIN || vspowerlevel[i] > PWRLVRECORD_MAX)
			return;
	}

	modded = READUINT8(save.p);

	// Aha! Someone's been screwing with the save file!
	if ((modded && !savemoddata))
		return;
	else if (modded != true && modded != false)
		return;

	// To save space, use one bit per collected/achieved/unlocked flag
	for (i = 0; i < MAXEMBLEMS;)
	{
		rtemp = READUINT8(save.p);
		for (j = 0; j < 8 && j+i < MAXEMBLEMS; ++j)
			emblemlocations[j+i].collected = ((rtemp >> j) & 1);
		i += j;
	}
	for (i = 0; i < MAXEXTRAEMBLEMS;)
	{
		rtemp = READUINT8(save.p);
		for (j = 0; j < 8 && j+i < MAXEXTRAEMBLEMS; ++j)
			extraemblems[j+i].collected = ((rtemp >> j) & 1);
		i += j;
	}
	for (i = 0; i < MAXUNLOCKABLES;)
	{
		rtemp = READUINT8(save.p);
		for (j = 0; j < 8 && j+i < MAXUNLOCKABLES; ++j)
			unlockables[j+i].unlocked = ((rtemp >> j) & 1);
		i += j;
	}
	for (i = 0; i < MAXCONDITIONSETS;)
	{
		rtemp = READUINT8(save.p);
		for (j = 0; j < 8 && j+i < MAXCONDITIONSETS; ++j)
			conditionSets[j+i].achieved = ((rtemp >> j) & 1);
		i += j;
	}

	timesBeaten = READUINT32(save.p);

	if (beforepresets)
	{
		if (LoadLegacyRecords(&save))
		{
			// whatever, this will be deleted for beta 1... right?
			corrupted = 0;
			M_SilentUpdateUnlockablesAndEmblems();
		}
		return;
	}

	// Main records
	uint16_t gdnumpresets = READUINT16(save.p);
	CLEANUP(Z_Pfree) char (*gdpresets)[MAXPRESETNAME+1] = ZZ_Alloc(gdnumpresets * sizeof(*gdpresets));
	for (i = 0; i < gdnumpresets; i++)
		READSTRINGL(save.p, gdpresets[i], sizeof(*gdpresets));

	uint32_t gdmaps = READUINT32(save.p);
	for (i = 0; i < gdmaps; i++)
	{
		char mapname[MAXMAPLUMPNAME];
		READSTRINGL(save.p, mapname, sizeof(mapname));
		maprecord_t *record = G_GetMapRecord(mapname);
		if (record != NULL)
			return; // duped
		record = G_AllocateMapRecord(mapname);

		// read statistics about this map
		record->visited = READUINT8(save.p);
		record->playtime = READUINT32(save.p);
		record->roundsplayed = READUINT32(save.p);
		record->roundswon = READUINT32(save.p);

		if (record->visited & ~MV_MAX)
			return; // invalid flags

		// now read the time attack records
		uint16_t numpresets = READUINT16(save.p);
		for (uint16_t k = 0; k < numpresets; k++)
		{
			uint16_t whichpreset = READUINT16(save.p);
			if (whichpreset >= gdnumpresets)
				return; // out of bounds

			char *prename = gdpresets[whichpreset];
			uint8_t presetversion = READUINT8(save.p);

			maprecordpreset_t *preset = G_GetMapRecordPreset(record, prename);
			if (preset != NULL)
				return; // duped
			preset = G_AllocateMapRecordPreset(record, prename, presetversion);
			preset->besttime = READUINT32(save.p);
			preset->bestlap = READUINT32(save.p);
			preset->playtime = READUINT32(save.p);
		}
	}

	corrupted = 0; // OK!

	// Silent update unlockables in case they're out of sync with conditions
	M_SilentUpdateUnlockablesAndEmblems();
}

// G_SaveGameData
// Saves the main data file, which stores information such as emblems found, etc.
void G_SaveGameData(void)
{
	size_t length, i, j;
	uint8_t btemp;
	CLEANUP(P_SaveBufferFree) savebuffer_t save = {0};
	char backupfile[MAX_WADPATH+4];

	if (!gamedataloaded)
		return; // If never loaded (-nodata), don't save

	// TODO: save buffers need to be made dynamic.
	// in the interest of not randomly deleting people's gamedata due to arbitrary limitations,
	// i choose a buffer size of... hmm... 64 megabytes?
	// yeah, just rely on the pages being faulted in. who cares
	length = 64*1024*1024;

	if (P_SaveBufferAlloc(&save, length) == false)
	{
		CONS_Alert(CONS_ERROR, M_GetText("No more free memory for saving game data\n"));
		return;
	}

	// Create backup of the save data
	snprintf(backupfile, sizeof(backupfile), "%s.bak", gamedatafilename);
	backupfile[sizeof(backupfile) - 1] = '\0';

	FILE *gamedata = fopen(gamedatafilename, "r");

	if (gamedata != NULL)
	{
		fclose(gamedata);

		if (!FIL_CopyFile(gamedatafilename, backupfile))
		{
			CONS_Alert(CONS_WARNING,"Failed to create a backup of save data. Will not attempt to write to save data\n");
			return;
		}
	}

	// Version test
	WRITEUINT32(save.p, GD_VERSIONCHECK); // 4

	K_WriteStats(&save, false);

	for (i = 0; i < PWRLV_NUMTYPES; i++)
		WRITEUINT16(save.p, vspowerlevel[i]);

	WRITEUINT8(save.p, (uint8_t)savemoddata); // 1

	// To save space, use one bit per collected/achieved/unlocked flag
	for (i = 0; i < MAXEMBLEMS;) // BIT_ARRAY_SIZE(MAXEMBLEMS)
	{
		btemp = 0;
		for (j = 0; j < 8 && j+i < MAXEMBLEMS; ++j)
			btemp |= (emblemlocations[j+i].collected << j);
		WRITEUINT8(save.p, btemp);
		i += j;
	}
	for (i = 0; i < MAXEXTRAEMBLEMS;) // BIT_ARRAY_SIZE(MAXEXTRAEMBLEMS)
	{
		btemp = 0;
		for (j = 0; j < 8 && j+i < MAXEXTRAEMBLEMS; ++j)
			btemp |= (extraemblems[j+i].collected << j);
		WRITEUINT8(save.p, btemp);
		i += j;
	}
	for (i = 0; i < MAXUNLOCKABLES;) // BIT_ARRAY_SIZE(MAXUNLOCKABLES)
	{
		btemp = 0;
		for (j = 0; j < 8 && j+i < MAXUNLOCKABLES; ++j)
			btemp |= (unlockables[j+i].unlocked << j);
		WRITEUINT8(save.p, btemp);
		i += j;
	}
	for (i = 0; i < MAXCONDITIONSETS;) // BIT_ARRAY_SIZE(MAXCONDITIONSETS)
	{
		btemp = 0;
		for (j = 0; j < 8 && j+i < MAXCONDITIONSETS; ++j)
			btemp |= (conditionSets[j+i].achieved << j);
		WRITEUINT8(save.p, btemp);
		i += j;
	}

	WRITEUINT32(save.p, timesBeaten); // 4

	// keep record data small by first writing an array of preset names,
	// and then writing indices into the array in the record data
	// using recordpresets is not an option, we MUST preserve unknown preset names!

	CLEANUP(P_SaveBufferFree) savebuffer_t rec = {0}, names = {0};
	const char *knownpre[UINT16_MAX+1]; // ...ouch
	uint16_t knownprecount = 0;
	P_SaveBufferAlloc(&rec, NEXTMAP_SPECIAL*1024);
	P_SaveBufferAlloc(&names, (MAXPRESETNAME+1)*65536);

	for (i = 0; i < nummaprecords; i++)
	{
		maprecord_t *record = maprecords[i];
		WRITESTRING(rec.p, record->name);

		// write statistics first
		WRITEUINT8(rec.p, record->visited & MV_MAX);
		WRITEUINT32(rec.p, record->playtime);
		WRITEUINT32(rec.p, record->roundsplayed);
		WRITEUINT32(rec.p, record->roundswon);

		// then time attack records
		WRITEUINT16(rec.p, record->numpresets);
		for (j = 0; j < record->numpresets; j++)
		{
			uint16_t k;
			maprecordpreset_t *preset = &record->presets[j];

			for (k = 0; k < knownprecount; k++)
				if (fastcmp(knownpre[k], preset->prename))
					break;
			if (k == knownprecount)
			{
				knownpre[k] = preset->prename;
				WRITESTRINGL(names.p, knownpre[k], MAXPRESETNAME+1);
				knownprecount++;
			}
			WRITEUINT16(rec.p, k);

			WRITEUINT8(rec.p, preset->version);
			WRITEUINT32(rec.p, preset->besttime);
			WRITEUINT32(rec.p, preset->bestlap);
			WRITEUINT32(rec.p, preset->playtime);
		}
	}

	WRITEUINT16(save.p, knownprecount);
	WRITEMEM(save.p, names.buffer, names.p - names.buffer);
	WRITEUINT32(save.p, nummaprecords);
	WRITEMEM(save.p, rec.buffer, rec.p - rec.buffer);

	FIL_WriteFile(va(pandf, srb2home, gamedatafilename), save.buffer, save.p - save.buffer);
}

#define VERSIONSIZE 16

//
// G_InitFromSavegame
// Can be called by the startup code or the menu task.
//
void G_LoadGame(uint32_t slot, mapnum_t mapoverride)
{
	char vcheck[VERSIONSIZE];
	char savename[256];
	savebuffer_t save = {0};

	// memset savedata to all 0, fixes calling perfectly valid saves corrupt because of bots
	memset(&savedata, 0, sizeof(savedata));

#ifdef SAVEGAME_OTHERVERSIONS
	//Oh christ.  The force load response needs access to mapoverride too...
	startonmapnum = mapoverride;
#endif

	if (marathonmode)
		snprintf(savename, sizeof(savename), "%s", liveeventbackup);
	else
		snprintf(savename, sizeof(savename), savegamename, slot);

	if (P_SaveBufferFromFile(&save, savename) == false)
	{
		CONS_Printf(M_GetText("Couldn't read file %s\n"), savename);
		return;
	}

	memset(vcheck, 0, sizeof (vcheck));
	snprintf(vcheck, sizeof(vcheck), (marathonmode ? "back-up %d" : "version %d"), VERSION);
	if (!fastcmp((const char *)save.p, (const char *)vcheck))
	{
#ifdef SAVEGAME_OTHERVERSIONS
		M_StartMessage2("Save Version Mismatch", M_GetText("Save game from different version.\nYou can load this savegame, but\nsaving afterwards will be disabled.\n\nDo you want to continue anyway?\n"),
		               M_ForceLoadGameResponse, MM_YESNO, M_MessageConfirmText(false, false), M_MessageCancelText(false, false));
		//Freeing done by the callback function of the above message
#else
		M_ClearMenus(true); // so ESC backs out to title
		M_StartMessage2("Invalid Save", M_GetText("Save game from different version\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
		Command_ExitGame_f();
		P_SaveBufferFree(&save);

		// no cheating!
		memset(&savedata, 0, sizeof(savedata));
#endif
		return; // bad version
	}
	save.p += VERSIONSIZE;

	if (demo.playback) // reset game engine
		G_StopDemo();

//	paused = false;
//	automapactive = false;

	// dearchive all the modifications
	if (!P_LoadGame(&save, mapoverride))
	{
		M_ClearMenus(true); // so ESC backs out to title
		M_StartMessage2("Invalid Save", M_GetText("Savegame file corrupted\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
		Command_ExitGame_f();
		Z_Free(save.buffer);
		save.p = save.buffer = NULL;

		// no cheating!
		memset(&savedata, 0, sizeof(savedata));
		return;
	}
	if (marathonmode)
	{
		marathontime = READUINT32(save.p);
		marathonmode |= READUINT8(save.p);
	}

	// done
	P_SaveBufferFree(&save);

//	gameaction = ga_nothing;
//	G_SetGamestate(GS_LEVEL);
	displayplayers[0] = consoleplayer;
	multiplayer = false;
	splitscreen = 0;
	SplitScreen_OnChange(); // not needed?

//	G_DeferedInitNew(sk_medium, G_BuildMapName(1), 0, 0, 1);
	if (setsizeneeded)
		R_ExecuteSetViewSize();

	M_ClearMenus(true);
	CON_ToggleOff();
}

//
// G_SaveGame
// Saves your game.
//
void G_SaveGame(uint32_t slot, mapnum_t mapnum)
{
	dboolean saved;
	char savename[256] = "";
	const char *backup;
	savebuffer_t save = {0};

	if (marathonmode)
		snprintf(savename, sizeof(savename), "%s", liveeventbackup);
	else
		snprintf(savename, sizeof(savename), savegamename, slot);
	backup = va("%s",savename);

	gameaction = ga_nothing;
	{
		char name[VERSIONSIZE];
		size_t length;

		if (P_SaveBufferAlloc(&save, SAVEGAMESIZE) == false)
		{
			CONS_Alert(CONS_ERROR, M_GetText("No more free memory for saving game data\n"));
			return;
		}

		memset(name, 0, sizeof (name));
		snprintf(name, sizeof(name), (marathonmode ? "back-up %d" : "version %d"), VERSION);
		WRITEMEM(save.p, name, VERSIONSIZE);

		P_SaveGame(&save, mapnum);
		if (marathonmode)
		{
			uint32_t writetime = marathontime;
			if (!(marathonmode & MA_INGAME))
				writetime += TICRATE*5; // live event backup penalty because we don't know how long it takes to get to the next map
			WRITEUINT32(save.p, writetime);
			WRITEUINT8(save.p, (marathonmode & ~MA_INIT));
		}

		length = save.p - save.buffer;
		saved = FIL_WriteFile(backup, save.buffer, length);
		P_SaveBufferFree(&save);
	}

	gameaction = ga_nothing;

	if (cht_debug && saved)
		CONS_Printf(M_GetText("Game saved.\n"));
	else if (!saved)
		CONS_Alert(CONS_ERROR, M_GetText("Error while writing to %s for save slot %u, base: %s\n"), backup, slot, (marathonmode ? liveeventbackup : savegamename));
}

#define BADSAVE goto cleanup;
#define CHECKPOS if (save.p >= save.end) BADSAVE
void G_SaveGameOver(uint32_t slot, dboolean modifylives)
{
	dboolean saved = false;
	size_t length;
	char vcheck[VERSIONSIZE];
	char savename[256];
	const char *backup;
	savebuffer_t save = {0};

	if (marathonmode)
		snprintf(savename, sizeof(savename), "%s", liveeventbackup);
	else
		snprintf(savename, sizeof(savename), savegamename, slot);
	backup = va("%s",savename);

	if (P_SaveBufferFromFile(&save, savename) == false)
	{
		CONS_Printf(M_GetText("Couldn't read file %s\n"), savename);
		return;
	}

	length = save.size;

	{
		char temp[sizeof(timeattackfolder)];
		uint8_t *lives_p;
		int8_t pllives;

		// Version check
		memset(vcheck, 0, sizeof (vcheck));
		snprintf(vcheck, sizeof(vcheck), (marathonmode ? "back-up %d" : "version %d"), VERSION);
		if (!fastcmp((const char *)save.p, (const char *)vcheck)) BADSAVE
		save.p += VERSIONSIZE;

		// P_UnArchiveMisc()
		(void)READINT16(save.p);
		CHECKPOS
		(void)READUINT16(save.p); // emeralds
		CHECKPOS
		READSTRINGN(save.p, temp, sizeof(temp)); // mod it belongs to
		if (!fastcmp(temp, timeattackfolder)) BADSAVE

		// P_UnArchivePlayer()
		CHECKPOS
		(void)READUINT16(save.p);
		CHECKPOS

		WRITEUINT8(save.p, numgameovers);
		CHECKPOS

		lives_p = save.p;
		pllives = READINT8(save.p); // lives
		CHECKPOS
		if (modifylives && pllives < startinglivesbalance[numgameovers])
		{
			pllives = startinglivesbalance[numgameovers];
			WRITEINT8(lives_p, pllives);
		}

		(void)READINT32(save.p); // Score
		CHECKPOS
		(void)READINT32(save.p); // continues

		// File end marker check
		CHECKPOS
		switch (READUINT8(save.p))
		{
			case 0xb7:
				{
					uint8_t i, banksinuse;
					CHECKPOS
					banksinuse = READUINT8(save.p);
					CHECKPOS
					if (banksinuse > NUM_LUABANKS)
						BADSAVE
					for (i = 0; i < banksinuse; i++)
					{
						(void)READINT32(save.p);
						CHECKPOS
					}
					if (READUINT8(save.p) != 0x1d)
						BADSAVE
				}
			case 0x1d:
				break;
			default:
				BADSAVE
		}

		// done
		saved = FIL_WriteFile(backup, save.buffer, length);
	}

cleanup:
	if (cht_debug && saved)
		CONS_Printf(M_GetText("Game saved.\n"));
	else if (!saved)
		CONS_Alert(CONS_ERROR, M_GetText("Error while writing to %s for save slot %u, base: %s\n"), backup, slot, (marathonmode ? liveeventbackup : savegamename));

	P_SaveBufferFree(&save);
}
#undef CHECKPOS
#undef BADSAVE

//
// G_DeferedInitNew
// Can be called by the startup code or the menu task,
// consoleplayer, displayplayers[], playeringame[] should be set.
//
void G_DeferedInitNew(dboolean pencoremode, mapnum_t map, int32_t pickedchar, uint8_t ssplayers, dboolean FLS)
{
	uint16_t color = SKINCOLOR_NONE;
	int32_t dogametype;

	paused = false;

	if (demo.playback)
		COM_BufAddText("stopdemo\n");

	G_FreeGhosts(); // TODO: do we actually need to do this?

	if ((modeattacking == ATTACKING_ITEMBREAK) || (bossinfo.boss == true))
	{
		dogametype = GT_BATTLE;
	}
	else
	{
		dogametype = GT_RACE;
	}

	// this leave the actual game if needed
	SV_StartSinglePlayerServer(dogametype, false);

	if (splitscreen != ssplayers)
	{
		splitscreen = ssplayers;
		SplitScreen_OnChange();
	}

	SetPlayerSkinByNum(consoleplayer, pickedchar);
	CV_StealthSet(&cv_skin[0], skins[pickedchar]->name);

	if (color != SKINCOLOR_NONE)
	{
		CV_StealthSetValue(&cv_playercolor[0], color);
	}

	D_MapChange(map, gametype, pencoremode, true, 1, false, FLS);
}

//
// This is the map command interpretation something like Command_Map_f
//
// called at: map cmd execution, doloadgame, doplaydemo
void G_InitNew(uint8_t pencoremode, mapnum_t map, dboolean resetplayer, dboolean skipprecutscene)
{
	const char * mapname = G_BuildMapName(map);

	int32_t i;

	Y_CleanupScreenBuffer();

	if (paused)
	{
		paused = false;
		S_ResumeAudio();
	}

	prevencoremode = ((!Playing()) ? false : encoremode);
	encoremode = pencoremode;

	legitimateexit = false; // SRB2Kart
	comebackshowninfo = false;

	if (!demo.playback && !netgame) // Netgame sets random seed elsewhere, demo playback sets seed just before us!
	{
		initial_seed = M_RandomizedSeed(); // Use a more "Random" random seed
		I_OutputMsg("[G_InitNew] initial seed: %u\n", initial_seed);

		P_ClearRandom(initial_seed);
	}

	// Clear a bunch of variables
	redscore = bluescore = lastmap = 0;
	racecountdown = exitcountdown = mapreset = exitfadestarted = 0;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		players[i].playerstate = PST_REBORN;

		players[i].roundscore = 0;

		if (resetplayer && !demo.playback) // SRB2Kart
		{
			players[i].lives = 3;
			players[i].xtralife = 0;
			players[i].totalring = 0;
			players[i].score = 0;
		}

		players[i].interpoints = 0;
	}

	// Reset unlockable triggers
	unlocktriggers = 0;

	// clear itemfinder, just in case
	if (!dedicated)	// except in dedicated servers, where it is not registered and can actually I_Error debug builds
		CV_StealthSetValue(&cv_itemfinder, 0);

	// internal game map
	// well this check is useless because it is done before (d_netcmd.c::command_map_f)
	// but in case of for demos....
	if (!mapname)
	{
		I_Error("Internal game map with ID %d not found\n", map);
		return;
	}
	if (mapheaderinfo[map-1]->lumpnum == LUMPERROR)
	{
		I_Error("Internal game map '%s' not found\n", mapname);
		return;
	}

	g_reloadingMap = (map == gamemap);
	gamemap = map;

	// Don't carry over custom music change to another map.
	mapmusflags |= MUSIC_RELOADRESET;
	automapactive = false;
	imcontinuing = false;

	if (!skipprecutscene && mapheaderinfo[gamemap-1]->precutscenenum && !modeattacking && !(marathonmode & MA_NOCUTSCENES)) // Start a custom cutscene.
		F_StartCustomCutscene(mapheaderinfo[gamemap-1]->precutscenenum-1, true, resetplayer);
	else
	{
		LUA_HookGamemap(HOOK(MapChange));
		G_DoLoadLevel(resetplayer);
	}

	if (netgame)
	{
		char *title = G_BuildMapTitle(gamemap);

		CON_LogMessage(va(M_GetText("Map is now \"%s"), mapname));
		if (title)
		{
			CON_LogMessage(va(": %s", title));
			Z_Free(title);
		}
		CON_LogMessage("\"\n");
	}
}


char *G_BuildMapTitle(mapnum_t mapnum)
{
	char *title = NULL;

	if (!mapnum || mapnum > nummapheaders || !mapheaderinfo[mapnum-1])
		I_Error("G_BuildMapTitle: Internal map ID %d not found (nummapheaders = %d)", mapnum-1, nummapheaders);

	if (mapheaderinfo[mapnum-1]->lvlttl[0] != '\0')
	{
		size_t len = 1;
		const char *zonetext = NULL;
		const char *actnum = NULL;

		len += strlen(mapheaderinfo[mapnum-1]->lvlttl);
		if (strlen(mapheaderinfo[mapnum-1]->zonttl) > 0)
		{
			zonetext = M_GetText(mapheaderinfo[mapnum-1]->zonttl);
			len += strlen(zonetext) + 1;	// ' ' + zonetext
		}
		else if (!(mapheaderinfo[mapnum-1]->levelflags & LF_NOZONE))
		{
			zonetext = M_GetText("Zone");
			len += strlen(zonetext) + 1;	// ' ' + zonetext
		}
		if (strlen(mapheaderinfo[mapnum-1]->actnum) > 0)
		{
			actnum = M_GetText(mapheaderinfo[mapnum-1]->actnum);
			len += strlen(actnum) + 1;	// ' ' + actnum
		}

		title = Z_Malloc(len, PU_STATIC, NULL);

		if (!title)
			return NULL;

		snprintf(title, len, "%s", mapheaderinfo[mapnum-1]->lvlttl);
		if (zonetext) snprintf(title + strlen(title), len - strlen(title), " %s", zonetext);
		if (actnum) snprintf(title + strlen(title), len - strlen(title), " %s", actnum);
	}

	return title;
}

static void measurekeywords(mapsearchfreq_t *fr,
		struct searchdim **dimp, uint8_t *cuntp,
		const char *s, const char *q, dboolean wanttable)
{
	char *qp;
	char *sp;
	if (wanttable)
		(*dimp) = Z_Realloc((*dimp), 255 * sizeof (struct searchdim),
				PU_STATIC, NULL);
	for (qp = strtok(va("%s", q), " ");
			qp && fr->total < 255;
			qp = strtok(0, " "))
	{
		if (( sp = strcasestr(s, qp) ))
		{
			if (wanttable)
			{
				(*dimp)[(*cuntp)].pos = sp - s;
				(*dimp)[(*cuntp)].siz = strlen(qp);
			}
			(*cuntp)++;
			fr->total++;
		}
	}
	if (wanttable)
		(*dimp) = Z_Realloc((*dimp), (*cuntp) * sizeof (struct searchdim),
				PU_STATIC, NULL);
}

static void writesimplefreq(mapsearchfreq_t *fr, int32_t *frc,
		mapnum_t mapnum, uint8_t pos, uint8_t siz)
{
	fr[(*frc)].mapnum = mapnum;
	fr[(*frc)].matchd = ZZ_Alloc(sizeof (struct searchdim));
	fr[(*frc)].matchd[0].pos = pos;
	fr[(*frc)].matchd[0].siz = siz;
	fr[(*frc)].matchc = 1;
	fr[(*frc)].total = 1;
	(*frc)++;
}

mapnum_t G_FindMap(const char *mapname, char **foundmapnamep,
		mapsearchfreq_t **freqp, int32_t *freqcp)
{
	mapnum_t newmapnum = 0;
	mapnum_t mapnum;
	mapnum_t apromapnum = 0;

	size_t      mapnamelen;
	char   *realmapname = NULL;
	char   *newmapname = NULL;
	char   *apromapname = NULL;
	char   *aprop = NULL;

	mapsearchfreq_t *freq;
	dboolean wanttable;
	int32_t freqc;
	uint8_t frequ;

	int32_t i;

	mapnamelen = strlen(mapname);

	freq = ZZ_Calloc(nummapheaders * sizeof (mapsearchfreq_t));

	wanttable = !!( freqp );

	freqc = 0;

	for (i = 0, mapnum = 1; i < nummapheaders; ++i, ++mapnum)
	{
		if (!mapheaderinfo[i] || mapheaderinfo[i]->lumpnum == LUMPERROR)
			continue;

		if (!( realmapname = G_BuildMapTitle(mapnum) ))
			continue;

		/* Now that we found a perfect match no need to fucking guess. */
		if (strnicmp(realmapname, mapname, mapnamelen) == 0)
		{
			if (wanttable)
			{
				writesimplefreq(freq, &freqc, mapnum, 0, mapnamelen);
			}
			if (newmapnum == 0)
			{
				newmapnum = mapnum;
				newmapname = realmapname;
				realmapname = 0;
				Z_Free(apromapname);
				if (!wanttable)
					break;
			}
		}
		else
		if (apromapnum == 0 || wanttable)
		{
			/* LEVEL 1--match keywords verbatim */
			if (( aprop = strcasestr(realmapname, mapname) ))
			{
				if (wanttable)
				{
					writesimplefreq(freq, &freqc,
							mapnum, aprop - realmapname, mapnamelen);
				}

				if (apromapnum == 0)
				{
					apromapnum = mapnum;
					apromapname = realmapname;
					realmapname = 0;
				}
			}
			else/* ...match individual keywords */
			{
				freq[freqc].mapnum = mapnum;
				measurekeywords(&freq[freqc],
						&freq[freqc].matchd, &freq[freqc].matchc,
						realmapname, mapname, wanttable);
				measurekeywords(&freq[freqc],
						&freq[freqc].keywhd, &freq[freqc].keywhc,
						mapheaderinfo[i]->keywords, mapname, wanttable);
				if (freq[freqc].total)
					freqc++;
			}
		}

		Z_Free(realmapname);/* leftover old name */
	}

	if (newmapnum == 0)/* no perfect match--try a substring */
	{
		newmapnum = apromapnum;
		newmapname = apromapname;
	}

	if (newmapnum == 0)/* calculate most queries met! */
	{
		frequ = 0;
		for (i = 0; i < freqc; ++i)
		{
			if (freq[i].total > frequ)
			{
				frequ = freq[i].total;
				newmapnum = freq[i].mapnum;
			}
		}
		if (newmapnum)
		{
			newmapname = G_BuildMapTitle(newmapnum);
		}
	}

	if (freqp)
		(*freqp) = freq;
	else
		Z_Free(freq);

	if (freqcp)
		(*freqcp) = freqc;

	if (foundmapnamep)
		(*foundmapnamep) = newmapname;
	else
		Z_Free(newmapname);

	return newmapnum;
}

void G_FreeMapSearch(mapsearchfreq_t *freq, int32_t freqc)
{
	int32_t i;
	for (i = 0; i < freqc; ++i)
	{
		Z_Free(freq[i].matchd);
	}
	Z_Free(freq);
}

mapnum_t G_FindMapByNameOrCode(const char *mapname, char **realmapnamep)
{
	mapnum_t newmapnum;

	size_t mapnamelen;

	char *p;

	mapnamelen = strlen(mapname);

	if (mapnamelen == 1)
	{
		if (mapname[0] == '*') // current map
			return gamemap;
		else if (mapname[0] == '+') // next map
		{
			newmapnum = G_CheckNextMap(true);
			return newmapnum >= NEXTMAP_SPECIAL ? 0 : newmapnum+1;
		}
	}

	/* Now detect map number in base 10, which no one asked for. */
	newmapnum = strtol(mapname, &p, 10);

	if (*p == '\0')/* we got it */
	{
		if (newmapnum < 1 || newmapnum > nummapheaders)
			return 0;

		if (!mapheaderinfo[newmapnum-1] || mapheaderinfo[newmapnum-1]->lumpnum == LUMPERROR)
			return 0;
	}
	else
	{
		newmapnum = G_MapNumber(mapname)+1;

		if (newmapnum > nummapheaders)
			return G_FindMap(mapname, realmapnamep, NULL, NULL);
	}

	if (realmapnamep)
		(*realmapnamep) = G_BuildMapTitle(newmapnum);

	return newmapnum;
}

static dboolean G_ShouldntClearQueue(gamestate_t _gamestate)
{
	return ((_gamestate == GS_INTERMISSION));
}

//
// G_SetGamestate
//
// Use this to set the gamestate, please.
//
void G_SetGamestate(gamestate_t newstate)
{
	gamestate = newstate;

	if (!G_ShouldntClearQueue(gamestate))
	{
		// Let's prevent uphill battles; only let specific gamestates clear the queue
		S_ClearMusicQueue();
	}

#ifdef HAVE_DISCORDRPC
	DRPC_UpdatePresence();
#endif
}

dboolean G_GamestateUsesLevel(void)
{
	switch (gamestate)
	{
		case GS_TITLESCREEN:
			return titlemapinaction;

		case GS_LEVEL:
			return true;

		default:
			return false;
	}
}

/* These functions handle the exitgame flag. Before, when the user
   chose to end a game, it happened immediately, which could cause
   crashes if the game was in the middle of something. Now, a flag
   is set, and the game can then be stopped when it's safe to do
   so.
*/

// Used as a callback function.
void G_SetExitGameFlag(void)
{
	exitgame = true;
}

void G_ClearExitGameFlag(void)
{
	exitgame = false;
}

dboolean G_GetExitGameFlag(void)
{
	return exitgame;
}

// Same deal with retrying.
void G_SetRetryFlag(void)
{
	retrying = true;
}

void G_ClearRetryFlag(void)
{
	retrying = false;
}

dboolean G_GetRetryFlag(void)
{
	return retrying;
}

void G_SetModeAttackRetryFlag(void)
{
	retryingmodeattack = true;
	G_SetRetryFlag();
}

void G_ClearModeAttackRetryFlag(void)
{
	retryingmodeattack = false;
}

dboolean G_GetModeAttackRetryFlag(void)
{
	return retryingmodeattack;
}

// Time utility functions
int32_t G_TicsToHours(tic_t tics)
{
	return tics/(3600*TICRATE);
}

int32_t G_TicsToMinutes(tic_t tics, dboolean full)
{
	if (full)
		return tics/(60*TICRATE);
	else
		return tics/(60*TICRATE)%60;
}

int32_t G_TicsToSeconds(tic_t tics)
{
	return (tics/TICRATE)%60;
}

int32_t G_TicsToCentiseconds(tic_t tics)
{
	return (int32_t)((tics%TICRATE) * (100.00f/TICRATE));
}

int32_t G_TicsToMilliseconds(tic_t tics)
{
	return (int32_t)((tics%TICRATE) * (1000.00f/TICRATE));
}

