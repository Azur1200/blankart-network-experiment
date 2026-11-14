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
/// \file  doomstat.h
/// \brief All the global variables that store the internal state.
///
///        Theoretically speaking, the internal state of the engine
///        should be found by looking at the variables collected
///        here, and every relevant module will have to include
///        this header file. In practice... things are a bit messy.

#ifndef __DOOMSTAT__
#define __DOOMSTAT__

#ifdef __cplusplus
extern "C" {
#endif

#include "g_mapnum.h"

// We need globally shared data structures, for defining the global state variables.
#include "doomdata.h"

// We need the player data structure as well.
#include "d_player.h"

#include "i_sound.h"

// =============================
// Selected map etc.
// =============================

#define ROUNDQUEUE_MAX 20 // sane max? maybe make dynamically allocated later
// These two live in gametype field of packets
#define ROUNDQUEUE_CMD_CLEAR UINT16_MAX
#define ROUNDQUEUE_CMD_SHOW UINT16_MAX-1
// The roundqueue itself is resident in g_game.h

// Selected by user.
extern mapnum_t gamemap;
extern dboolean g_reloadingMap;
extern char mapmusname[7];
extern uint16_t mapmusflags;
extern uint32_t mapmusposition;
extern uint32_t mapmusresume;
extern uint8_t mapmusrng;
#define MUSIC_TRACKMASK   0x0FFF // ----************
#define MUSIC_RELOADRESET 0x8000 // *---------------
#define MUSIC_FORCERESET  0x4000 // -*--------------
// Use other bits if necessary.

extern uint32_t maptol;

extern int32_t cursaveslot;
//extern int16_t lastmapsaved;
extern int16_t lastmaploaded;
extern uint8_t gamecomplete;

// Extra abilities/settings for skins (combinable stuff)
typedef enum
{
	MA_RUNNING     = 1,    // In action
	MA_INIT        = 1<<1, // Initialisation
	MA_NOCUTSCENES = 1<<2, // No cutscenes
	MA_INGAME      = 1<<3  // Timer ignores loads
} marathonmode_t;

extern marathonmode_t marathonmode;
extern tic_t marathontime;

#define maxgameovers 13
extern uint8_t numgameovers;
extern int8_t startinglivesbalance[maxgameovers+1];

#define NUMPRECIPFREESLOTS 64

typedef enum
{
	PRECIP_NONE = 0,

	PRECIP_RAIN,
	PRECIP_SNOW,
	PRECIP_STORM,
	PRECIP_STORM_NORAIN,
	PRECIP_STORM_NOSTRIKES,

	PRECIP_FIRSTFREESLOT,
	PRECIP_LASTFREESLOT = PRECIP_FIRSTFREESLOT + NUMPRECIPFREESLOTS - 1,

	MAXPRECIP
} preciptype_t;

typedef enum
{
	PRECIPFX_THUNDER = 1,
	PRECIPFX_LIGHTNING = 1<<1,
	PRECIPFX_WATERPARTICLES = 1<<2
} precipeffect_t;

struct precipprops_t
{
	const char *name;
	mobjtype_t type;
	precipeffect_t effects;
};

extern precipprops_t precipprops[MAXPRECIP];
extern preciptype_t precip_freeslot;

extern preciptype_t globalweather;
extern preciptype_t curWeather;

/** Time attack information, currently a very small structure.
  */
struct recorddata_t
{
	tic_t time; ///< Time in which the level was finished.
	tic_t lap;  ///< Best lap time for this level.
	//uint32_t score; ///< Score when the level was finished.
	//uint16_t rings; ///< Rings when the level was finished.
};

// mapvisited is now a set of flags that says what we've done in the map.
#define MV_VISITED (1)
#define MV_BEATEN  (1<<1)
#define MV_ENCORE  (1<<2)
#define MV_MAX     (MV_VISITED|MV_BEATEN|MV_ENCORE)
#define MV_MP      ((MV_MAX+1)<<1)

// Set if homebrew PWAD stuff has been added.
extern dboolean modifiedgame;
extern dboolean majormods;
extern dboolean savemoddata; // This mod saves time/emblem data.
extern dboolean imcontinuing; // Temporary flag while continuing
extern dboolean metalrecording;

#define ATTACKING_NONE     0
#define ATTACKING_TIME     1
#define ATTACKING_ITEMBREAK 2
#define ATTACKING_MENU UINT8_MAX
extern uint8_t modeattacking;

// menu demo things
extern uint8_t  numDemos;
extern uint32_t demoDelayTime;
extern uint32_t demoIdleTime;

// Netgame? only true in a netgame
extern dboolean netgame;
extern dboolean addedtogame; // true after the server has added you
// Only true if >1 player. netgame => multiplayer but not (multiplayer=>netgame)
extern dboolean multiplayer;

extern uint8_t splitscreen;
extern int r_splitscreen;

extern dboolean circuitmap; // Does this level have 'circuit mode'?
extern dboolean forceresetplayers, deferencoremode, forcespecialstage;

// ========================================
// Internal parameters for sound rendering.
// ========================================

extern dboolean sound_disabled;
extern dboolean digital_disabled;

// =========================
// Status flags for refresh.
// =========================
//

extern uint8_t paused; // Game paused?
extern uint8_t window_notinfocus; // are we in focus? (backend independant -- handles auto pausing and display of "focus lost" message)

extern dboolean nodrawers;
extern dboolean noblit;
extern dboolean lastdraw;
extern int32_t postimgparam[MAXSPLITSCREENPLAYERS];

extern int32_t viewwindowx, viewwindowy;
extern int32_t viewwidth;

extern dboolean gamedataloaded;

// Player taking events, and displaying.
extern int32_t consoleplayer;
extern int32_t displayplayers[MAXSPLITSCREENPLAYERS];
/* g_localplayers[0] = consoleplayer */
extern int32_t g_localplayers[MAXSPLITSCREENPLAYERS];

extern char * titlemap;
extern dboolean hidetitlepics;
extern char * bootmap; //bootmap for loading a map on startup

extern char * tutorialmap; // map to load for tutorial
extern dboolean tutorialmode; // are we in a tutorial right now?

extern dboolean looptitle;

// CTF colors.
extern uint16_t skincolor_redteam, skincolor_blueteam, skincolor_redring, skincolor_bluering;

extern dboolean exitfadestarted;

struct scene_t
{
	uint8_t numpics;
	char picname[8][8];
	uint8_t pichires[8];
	char *text;
	uint16_t xcoord[8];
	uint16_t ycoord[8];
	uint16_t picduration[8];
	uint8_t musicloop;
	uint16_t textxpos;
	uint16_t textypos;

	char   musswitch[7];
	uint16_t musswitchflags;
	uint32_t musswitchposition;

	uint8_t fadecolor; // Color number for fade, 0 means don't do the first fade
	uint8_t fadeinid;  // ID of the first fade, to a color -- ignored if fadecolor is 0
	uint8_t fadeoutid; // ID of the second fade, to the new screen
}; // TODO: It would probably behoove us to implement subsong/track selection here, too, but I'm lazy -SH

struct cutscene_t
{
	scene_t scene[128]; // 128 scenes per cutscene.
	int32_t numscenes; // Number of scenes in this cutscene
};

extern cutscene_t *cutscenes[128];

// Reserve prompt space for tutorials
#define TUTORIAL_PROMPT 201 // one-based
#define TUTORIAL_AREAS 6
#define TUTORIAL_AREA_PROMPTS 5
#define MAX_PROMPTS (TUTORIAL_PROMPT+TUTORIAL_AREAS*TUTORIAL_AREA_PROMPTS*3) // 3 control modes
#define MAX_PAGES 128

#define PROMPT_PIC_PERSIST 0
#define PROMPT_PIC_LOOP 1
#define PROMPT_PIC_DESTROY 2
#define MAX_PROMPT_PICS 8
struct textpage_t
{
	uint8_t numpics;
	uint8_t picmode; // sequence mode after displaying last pic, 0 = persist, 1 = loop, 2 = destroy
	uint8_t pictoloop; // if picmode == loop, which pic to loop to?
	uint8_t pictostart; // initial pic number to show
	char picname[MAX_PROMPT_PICS][8];
	uint8_t pichires[MAX_PROMPT_PICS];
	uint16_t xcoord[MAX_PROMPT_PICS]; // gfx
	uint16_t ycoord[MAX_PROMPT_PICS]; // gfx
	uint16_t picduration[MAX_PROMPT_PICS];

	char   musswitch[7];
	uint16_t musswitchflags;
	uint8_t musicloop;

	char tag[33]; // page tag
	char name[34]; // narrator name, extra char for color
	char iconname[8]; // narrator icon lump
	dboolean rightside; // narrator side, false = left, true = right
	dboolean iconflip; // narrator flip icon horizontally
	uint8_t hidehud; // hide hud, 0 = show all, 1 = hide depending on prompt position (top/bottom), 2 = hide all
	uint8_t lines; // # of lines to show. If name is specified, name takes one of the lines. If 0, defaults to 4.
	int32_t backcolor; // see CON_SetupBackColormap: 0-11, INT32_MAX for user-defined (CONS_BACKCOLOR)
	uint8_t align; // text alignment, 0 = left, 1 = right, 2 = center
	uint8_t verticalalign; // vertical text alignment, 0 = top, 1 = bottom, 2 = middle
	uint8_t textspeed; // text speed, delay in tics between characters.
	sfxenum_t textsfx; // sfx_ id for printing text
	uint8_t nextprompt; // next prompt to jump to, one-based. 0 = current prompt
	uint8_t nextpage; // next page to jump to, one-based. 0 = next page within prompt->numpages
	char nexttag[33]; // next tag to jump to. If set, this overrides nextprompt and nextpage.
	int32_t timetonext; // time in tics to jump to next page automatically. 0 = don't jump automatically
	char *text;
};

struct textprompt_t
{
	textpage_t page[MAX_PAGES];
	int32_t numpages; // Number of pages in this prompt
};

extern textprompt_t *textprompts[MAX_PROMPTS];

// For the Custom Exit linedef.
extern mapnum_t nextmapoverride;
extern uint8_t skipstats;

// Fun extra stuff
extern int16_t lastmap; // Last level you were at (returning from special stages).

// A single point in space.
struct mappoint_t
{
	fixed_t x, y, z;
};

extern struct quake
{
	// camera offsets and duration
	fixed_t x,y,z;
	uint16_t time;

	// location, radius, and intensity...
	mappoint_t *epicenter;
	fixed_t radius, intensity;
} quake;

// Custom Lua values
struct customoption_t
{
	char option[32]; // 31 usable characters
	char value[256]; // 255 usable characters. If this seriously isn't enough then wtf.
};

struct mapheader_lighting_t
{
	dboolean use_custom_light;			///< Decides if this should use custom lighting.
	uint8_t light_contrast;				///< Range of wall lighting. 0 is no lighting.
	int8_t sprite_backlight;				///< Subtract from wall lighting for sprites only.
	dboolean use_light_angle;			///< When false, wall lighting is evenly distributed. When true, wall lighting is directional.
	angle_t light_angle;				///< Angle of directional wall lighting.
};

#define MAXMUSNAMES 3 // maximum definable music tracks per level

// This could support more, but is that a good idea?
// Keep in mind that it may encourage people making overly long cups just because they "can", and would be a waste of memory.
#define MAXLEVELLIST 5
#define CUPCACHE_BONUS MAXLEVELLIST
#define MAXBONUSLIST 2
#define CUPCACHE_SPECIAL (CUPCACHE_BONUS+MAXBONUSLIST)
#define CUPCACHE_PODIUM (CUPCACHE_SPECIAL+1)
#define CUPCACHE_MAX (CUPCACHE_PODIUM+1)

#define MAXCUPNAME 16 // includes \0, for cleaner savedata

struct cupheader_t
{
	uint16_t id;								///< Cup ID
	uint8_t monitor;							///< Monitor graphic 1-9 or A-Z

	char name[MAXCUPNAME];					///< Cup title
	uint32_t namehash;						///< Cup title hash
	uint16_t wadnum;							///< WAD that defined this cup

	char icon[9];							///< Name of the icon patch
	char *levellist[CUPCACHE_MAX];			///< List of levels that belong to this cup
	mapnum_t cachedlevels[CUPCACHE_MAX];		///< IDs in levellist, bonusgame, and specialstage
	uint8_t numlevels;						///< Number of levels defined in levellist
	uint8_t numbonus;							///< Number of bonus stages defined
	uint8_t emeraldnum;						///< ID of Emerald to use for special stage (1-7 for Chaos Emeralds, 8-14 for Super Emeralds, 0 for no emerald)
	int8_t unlockrequired;					///< An unlockable is required to select this cup. -1 for no unlocking required.
	cupheader_t *next;						///< Next cup in linked list
};

extern cupheader_t *kartcupheaders; // Start of cup linked list
extern uint16_t numkartcupheaders;

#define MAXMAPLUMPNAME 64 // includes \0, for cleaner savedata

/** Map header information.
  */
struct mapheader_t
{
	// Core game information, not user-modifiable directly
	char *lumpname;								///< Lump name can be really long
	uint32_t lumpnamehash;						///< FNV1a_QuickCaseHash(->lumpname, MAXMAPLUMPNAME)
	lumpnum_t lumpnum;							///< Lump number for the map, used by vres_GetMap

	void *thumbnailPic;							///< Lump data for the level select thumbnail.
	void *minimapPic;							///< Lump data for the minimap graphic.

	cupheader_t *cup;							///< Cached cup

	size_t justPlayed;							///< Prevent this map from showing up in votes if it was recently picked.

	// Titlecard information
	char lvlttl[22];							///< Level name without "Zone". (21 character limit instead of 32, 21 characters can display on screen max anyway)
	char subttl[33];							///< Subtitle for level
	char zonttl[22];							///< "ZONE" replacement name
	char actnum[3];								///< SRB2Kart: Now a 2 character long string.

	// Selection metadata
	char keywords[33];							///< Keywords separated by space to search for. 32 characters.

	int8_t unlockrequired;						///< Is an unlockable required to play this level? -1 if no.
	uint8_t levelselect;							///< Is this map available in the level select? If so, which map list is it available in?
	uint8_t menuflags;							///< LF2_flags: options that affect record attack menus

	// Operational metadata
	uint16_t levelflags;							///< LF_flags:  merged dbooleans into one uint16_t for space, see below
	uint32_t typeoflevel;							///< Combination of typeoflevel flags.
	uint8_t numlaps;								///< Number of laps in circuit mode, unless overridden.
	uint8_t lapspersection;						///< Number of laps per section in hybrid section-circuit maps.
	fixed_t gravity;							///< Map-wide gravity.

	// Music information
	char musname[MAXMUSNAMES][7];				///< Music tracks to play. First dimension is the track number, second is the music string. "" for no music.
	char encoremusname[MAXMUSNAMES][7];			///< Music tracks to play in Encore. First dimension is the track number, second is the music string. "" for no music.
	uint16_t mustrack;							///< Subsong to play. Only really relevant for music modules and specific formats supported by GME. 0 to ignore.
	uint32_t muspos;								///< Music position to jump to.
	uint8_t musname_size;							///< Number of music tracks defined
	uint8_t encoremusname_size;					///< Number of Encore music tracks defined

	// Sky information
	uint8_t weather;								///< See preciptype_t
	char skytexture[9];							///< Sky texture to use.
	int16_t skybox_scalex;						///< Skybox X axis scale. (0 = no movement, 1 = 1:1 movement, 16 = 16:1 slow movement, -4 = 1:4 fast movement, etc.)
	int16_t skybox_scaley;						///< Skybox Y axis scale.
	int16_t skybox_scalez;						///< Skybox Z axis scale.

	// Distance information
	fixed_t mobj_scale;							///< Defines the size all object calculations are relative to
	fixed_t default_waypoint_radius;			///< 0 is a special value for DEFAULT_WAYPOINT_RADIUS, but scaled with mobjscale

	// Visual information
	uint16_t palette;								///< PAL lump to use on this map
	uint16_t encorepal;							///< PAL for encore mode
	mapheader_lighting_t lighting;				///< Wall and sprite lighting
	mapheader_lighting_t lighting_encore;		///< Alternative lighting for Encore mode
	dboolean use_encore_lighting;				///< Whether to use separate Encore lighting

	// Freed animal information
	uint8_t numFlickies;							///< Internal. For freed flicky support.
	mobjtype_t *flickies;						///< List of freeable flickies in this level. Allocated dynamically for space reasons. Be careful.

	// Script information
	char runsoc[33];							///< SOC to execute at start of level (32 character limit instead of 63)
	char scriptname[33];						///< Script to use when the map is switched to. (32 character limit instead of 191)

	// Cutscene information
	uint8_t precutscenenum;						///< Cutscene number to play BEFORE a level starts.
	uint8_t cutscenenum;							///< Cutscene number to use, 0 for none.

	// Lua information
	uint8_t numCustomOptions;						///< Internal. For Lua custom value support.
	customoption_t *customopts;					///< Custom options. Allocated dynamically for space reasons. Be careful.

	// BlanKart
	dboolean use_walltransfer;					///< Whether to use RR style wall transfering or not
	dboolean use_terrain;						///< Whether to use gameplay affecting Terrain effects or not (leaves visuals alone)
	int32_t base_track_complexity;				///< Decides what the base track compexity for the current map is.
	fixed_t track_modifier_max;					///< Decides what the modifier max for the current map is.

	tic_t par_time[NUMKARTSPEEDS];				///< Par time for the map, used as part of the rank system.

	dboolean disable_encore_sky;				///< Prevents Encore from repaletting the sky in this map.

	// EFX
	efx_t *globalEFX;							///< Decides what global EFX for the current map is. ONLY USE FOR TYPE AND VARS.
};


// level flags
#define LF_SCRIPTISFILE       (1<<0) ///< True if the script is a file, not a lump.
#define LF_NOZONE             (1<<1) ///< Don't include "ZONE" on level title
#define LF_SECTIONRACE        (1<<2) ///< Section race level
#define LF_SUBTRACTNUM        (1<<3) ///< Use subtractive position number (for bright levels)

#define LF2_HIDEINMENU    (1<<0) ///< Hide in the multiplayer menu
#define LF2_HIDEINSTATS   (1<<1) ///< Hide in the statistics screen
#define LF2_NOTIMEATTACK  (1<<2) ///< Hide this map in Time Attack modes
#define LF2_VISITNEEDED   (1<<3) ///< Not available in Time Attack modes until you visit the level
#define LF2_HIDEINVOTE    (1<<4) ///< Hide in the vote screen

extern mapheader_t** mapheaderinfo;
extern int32_t nummapheaders, mapallocsize;

// Gametypes
#define NUMGAMETYPEFREESLOTS (MAXGAMETYPES-GT_FIRSTFREESLOT)

enum GameType
{
	GT_RACE = 0,
	GT_BATTLE,
	GT_FREEROAM,

	GT_FIRSTFREESLOT,
	GT_LASTFREESLOT = 127, // Previously (GT_FIRSTFREESLOT + NUMGAMETYPEFREESLOTS - 1) - it would be necessary to rewrite VOTEMODIFIER_ENCORE to go higher than this.
	MAXGAMETYPES
};
// If you alter this list, update defaultgametypes and *gametypes in g_game.c
#define MAXTOL             (1<<31)
#define NUMBASETOLNAMES    (10)
#define NUMTOLNAMES        (NUMBASETOLNAMES + NUMGAMETYPEFREESLOTS)

struct gametype_t
{
	const char *name;
	const char *constant;
	uint32_t rules;
	uint32_t tol;
	uint8_t intermission;
	int32_t pointlimit;
	int32_t timelimit;
	int32_t color;
};

extern gametype_t *gametypes[MAXGAMETYPES+1];
extern int16_t numgametypes;

extern int16_t gametype;

// Gametype rules
enum GameTypeRules
{
	// Race rules
	GTR_CIRCUIT				= 1,     // Enables the finish line, laps, and the waypoint system.
	GTR_RACEODDS			= 1<<1,  // ItemOdds used in this mode are for racing.
	GTR_BOTS				= 1<<2,  // Allows bots in this gametype. Combine with BotTiccmd hooks to make bots support your gametype.
	GTR_RINGS				= 1<<3,  // Allow Rings in this gametype.

	// Battle gametype rules
	GTR_BUMPERS				= 1<<4,  // Enables the bumper health system.
	GTR_POINTS				= 1<<5,  // Get awarded score when hitting people.
	GTR_BATTLEODDS			= 1<<6,  // ItemOdds used in this mode are for battling.
	GTR_PAPERITEMS			= 1<<7,  // Replaces item boxes with paper item spawners.
	GTR_WANTED				= 1<<8,  // Enables the wanted anti-camping system.
	GTR_WANTEDSPB           = 1<<9,  // Unleash Self-Propelled Bombs on Wanted players.
	GTR_KARMA				= 1<<10, // Enables the Karma system if you're out of bumpers.
	GTR_ITEMARROWS			= 1<<11, // Show item box arrows above players.
	GTR_ITEMBREAKER			= 1<<12, // Enables the use of Item Breaker in this Gamemode.
	GTR_BATTLESTARTS		= 1<<13, // Use Battle Mode start positions.
	GTR_CLOSERPLAYERS		= 1<<14, // Enables specfic gameplay tweaks with closer players.
	GTR_BATTLEBOXES			= 1<<15, // Itemboxes respawn differently.
	GTR_BATTLESPEED			= 1<<16, // Use battle gamespeed cvar.
	GTR_DOUBLEDFLASHTICS	= 1<<17, // Double your flashtics after taking damage.

	GTR_POINTLIMIT			= 1<<18, // Reaching point limit ends the round.
	GTR_TIMELIMIT			= 1<<19, // Reaching time limit ends the round.
	GTR_OVERTIME			= 1<<20, // Allow overtime behavior.

	// Custom gametype rules
	GTR_TEAMS				= 1<<21, // Teams are forced on.
	GTR_NOTEAMS				= 1<<22, // Teams are forced off.
	GTR_TEAMSTARTS			= 1<<23, // Use team-based start positions.

	// Grand Prix rules
	GTR_LIVES				= 1<<25, // Lives system, players are forced to spectate during Game Over.
	GTR_SPECIALBOTS			= 1<<26, // Bot difficulty gets stronger between rounds, and the rival system is enabled.
	
	// Misc
	GTR_NOCOUNTDOWN			= 1<<27, // Disables Countdown timer and control lock at the start of levels.
	GTR_ENCORE				= 1<<28, // Enable Encore mode.

	// free: to and including 1<<31
};

// TypeOfLevel things
enum TypeOfLevel
{
	// Gametypes
	TOL_RACE	= 0x0001, ///< Race
	TOL_BATTLE	= 0x0002, ///< Battle
	TOL_BOSS	= 0x0004, ///< Boss (variant of battle, but forbidden)
	TOL_SPECIAL	= 0x0008, ///< Special Stage (variant of race, but forbidden)
	TOL_FREEROAM = 0x0010, ///< Free Roam (varient of race, but not for racing)

	// Compat
	TOL_COMPAT1	= 0x0008, ///< For compat. Handles all the unused kart v1 types.
	TOL_COMPAT2	= 0x0010, ///< ^
	TOL_COMPAT3	= 0x0020, ///< ^
	TOL_COMPAT4	= 0x0040, ///< ^
	TOL_COMPAT5	= 0x0080, ///< ^
	TOL_COMPAT6	= 0x0100, ///< ^

	// Modifiers
	TOL_TV		= 0x0200 ///< Midnight Channel specific: draw TV like overlay on HUD
};

struct tolinfo_t
{
	const char *name;
	uint32_t flag;
};
extern tolinfo_t TYPEOFLEVEL[NUMTOLNAMES];
extern uint32_t lastcustomtol;

extern uint8_t stagefailed;

// Emeralds stored as bits to throw savegame hackers off.

extern uint16_t emeralds;

#define EMERALD1 1
#define EMERALD2 2
#define EMERALD3 4
#define EMERALD4 8
#define EMERALD5 16
#define EMERALD6 32
#define EMERALD7 64
#define ALL7EMERALDS(v) ((v & (EMERALD1|EMERALD2|EMERALD3|EMERALD4|EMERALD5|EMERALD6|EMERALD7)) == (EMERALD1|EMERALD2|EMERALD3|EMERALD4|EMERALD5|EMERALD6|EMERALD7))

#define NUM_LUABANKS 16 // please only make this number go up between versions, never down. you'll break saves otherwise. also, must fit in uint8_t
extern int32_t luabanks[NUM_LUABANKS];

extern int32_t nummaprings; //keep track of spawned rings/coins

extern uint32_t initial_seed; ///< Log the first randomization seed the server gets

extern uint32_t token; ///< Number of tokens collected in a level
extern uint32_t tokenlist; ///< List of tokens collected
extern dboolean gottoken; ///< Did you get a token? Used for end of act
extern int32_t tokenbits; ///< Used for setting token bits
extern uint32_t bluescore; ///< Blue Team Scores
extern uint32_t redscore;  ///< Red Team Scores

// Eliminates unnecessary searching.
extern dboolean CheckForBustableBlocks;
extern dboolean CheckForBouncySector;
extern dboolean CheckForQuicksand;
extern dboolean CheckForMarioBlocks;
extern dboolean CheckForFloatBob;
extern dboolean CheckForReverseGravity;

// Powerup durations
extern uint16_t invulntics;
extern uint16_t sneakertics;
extern uint16_t flashingtics;
extern uint16_t tailsflytics;
extern uint16_t underwatertics;
extern uint16_t spacetimetics;
extern uint16_t extralifetics;
extern uint16_t nightslinktics;

// SRB2kart
extern tic_t introtime;
extern tic_t starttime;
extern tic_t autopartime;
extern tic_t leaderfinishtime;

extern tic_t raceexittime;
extern tic_t battleexittime;

extern int32_t comebackscorecap;

extern int32_t hyudorotime;
extern int32_t stealtime;
extern int32_t sneakertime;
extern int32_t waterpaneltime;
extern int32_t itemtime;
extern int32_t bubbletime;
extern int32_t flametime;;
extern int32_t comebacktime;
extern int32_t bumptime;
extern int32_t shovetime;
extern int32_t greasetics;
extern int32_t wipeoutslowtime;
extern int32_t wantedreduce;
extern int32_t wantedfrequency;

extern uint8_t introtoplay;
extern uint8_t creditscutscene;

extern uint8_t use1upSound;
extern uint8_t maxXtraLife; // Max extra lives from rings

extern mobj_t *hunt1, *hunt2, *hunt3; // Emerald hunt locations

struct exitcondition_t
{
	dboolean losing;
	dboolean retry;
	dboolean hasfinished;
};

// For racing
extern tic_t racecountdown, exitcountdown;
extern exitcondition_t g_exit;

#define DEFAULT_GRAVITY (FRACUNIT*8)/10
extern fixed_t gravity;
extern fixed_t mapobjectscale;

extern struct maplighting
{
	uint8_t contrast;
	int8_t backlight;
	dboolean directional;
	angle_t angle;
} maplighting;

//for CTF balancing
extern int16_t autobalance;
extern int16_t teamscramble;
extern int16_t scrambleplayers[MAXPLAYERS]; //for CTF team scramble
extern int16_t scrambleteams[MAXPLAYERS]; //for CTF team scramble
extern int16_t scrambletotal; //for CTF team scramble
extern int16_t scramblecount; //for CTF team scramble

extern int32_t cheats;

// SRB2kart
extern uint8_t numlaps;
extern uint8_t gamespeed;
extern dboolean franticitems;
extern dboolean encoremode, prevencoremode;
extern dboolean comeback;

extern int8_t mostwanted;
extern int8_t battlewanted[4];
extern tic_t wantedcalcdelay;
extern tic_t mapreset;
extern dboolean thwompsactive;
extern uint8_t lastLowestLap;
extern int8_t spbplace;
extern dboolean startedInFreePlay;

extern tic_t bombflashtimer;	// Used to avoid causing seizures if multiple mines explode close to you :)
extern dboolean legitimateexit;
extern dboolean comebackshowninfo;
extern tic_t antibumptime;

extern int16_t g_voteLevels[12][2];
extern int8_t g_votes[MAXPLAYERS];
extern int8_t g_pickedVote;

extern uint32_t timesBeaten; // # of times the game has been beaten.

// ===========================
// Internal parameters, fixed.
// ===========================
// These are set by the engine, and not changed
//  according to user inputs. Partly load from
//  WAD, partly set at startup time.

extern tic_t gametic;
#define localgametic leveltime

// Player spawn spots.
extern mapthing_t *playerstarts[MAXPLAYERS]; // Cooperative
extern mapthing_t *bluectfstarts[MAXPLAYERS]; // CTF
extern mapthing_t *redctfstarts[MAXPLAYERS]; // CTF

#define TUBEWAYPOINTSEQUENCESIZE 256
#define NUMTUBEWAYPOINTSEQUENCES 256
extern mobj_t *tubewaypoints[NUMTUBEWAYPOINTSEQUENCES][TUBEWAYPOINTSEQUENCESIZE];
extern uint16_t numtubewaypoints[NUMTUBEWAYPOINTSEQUENCES];

void P_AddTubeWaypoint(uint8_t sequence, uint8_t id, mobj_t *waypoint);
mobj_t *P_GetFirstTubeWaypoint(uint8_t sequence);
mobj_t *P_GetLastTubeWaypoint(uint8_t sequence);
mobj_t *P_GetPreviousTubeWaypoint(mobj_t *current, dboolean wrap);
mobj_t *P_GetNextTubeWaypoint(mobj_t *current, dboolean wrap);
mobj_t *P_GetClosestTubeWaypoint(uint8_t sequence, mobj_t *mo);
dboolean P_IsDegeneratedTubeWaypointSequence(uint8_t sequence);

// =====================================
// Internal parameters, used for engine.
// =====================================

#if defined (macintosh)
#define DEBFILE(msg) I_OutputMsg(msg)
#else
#define DEBUGFILE
#ifdef DEBUGFILE
#define DEBFILE(msg) { if (debugfile) { fputs(msg, debugfile); fflush(debugfile); } }
#else
#define DEBFILE(msg) {}
#endif
#endif

#ifdef DEBUGFILE
extern FILE *debugfile;
extern int32_t debugload;
#endif

// wipegamestate can be set to -1
//  to force a wipe on the next draw
extern gamestate_t wipegamestate;
extern int16_t wipetypepre;
extern int16_t wipetypepost;

// debug flag to cancel adaptiveness
extern dboolean singletics;

// =============
// Netgame stuff
// =============

#include "d_clisrv.h"

extern consvar_t cv_forceskin; // force clients to use the server's skin
extern consvar_t cv_downloading; // allow clients to downloading WADs.
extern consvar_t cv_nettimeout; // SRB2Kart: Advanced server options menu
extern consvar_t cv_jointimeout;
extern consvar_t cv_validate_pubkey;
extern ticcmd_t netcmds[BACKUPTICS][MAXPLAYERS];
extern int32_t serverplayer;
extern int32_t adminplayers[MAXPLAYERS];

/// \note put these in d_clisrv outright?

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__DOOMSTAT__
