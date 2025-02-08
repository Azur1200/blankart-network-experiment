// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  deh_tables.c
/// \brief Define DeHackEd tables.

#include "d_player.h"
#include "d_ticcmd.h"
#include "doomdef.h" // Constants
#include "s_sound.h" // Sound constants
#include "info.h" // Mobj, state, sprite, etc constants
#include "m_menu.h" // Menu constants
#include "y_inter.h" // Intermission constants
#include "p_local.h" // some more constants
#include "r_draw.h" // Colormap constants
#include "lua_script.h" // Lua stuff
#include "m_cond.h" // Emblem constants
#include "v_video.h" // video flags (for lua)
#include "i_sound.h" // musictype_t (for lua)
#include "g_state.h" // gamestate_t (for lua)
#include "r_data.h" // patchalphastyle_t
#include "k_boss.h" // spottype_t (for lua)
#include "k_follower.h" // followermode_t (for lua)

#include "deh_tables.h"

char *FREE_STATES[NUMSTATEFREESLOTS];
char *FREE_MOBJS[NUMMOBJFREESLOTS];
char *FREE_SKINCOLORS[NUMCOLORFREESLOTS];
UINT8 used_spr[(NUMSPRITEFREESLOTS / 8) + 1]; // Bitwise flag for sprite freeslot in use! I would use ceil() here if I could, but it only saves 1 byte of memory anyway.

const char NIGHTSGRADE_LIST[] = {
	'F', // GRADE_F
	'E', // GRADE_E
	'D', // GRADE_D
	'C', // GRADE_C
	'B', // GRADE_B
	'A', // GRADE_A
	'S', // GRADE_S
	'\0'
};

struct flickytypes_s FLICKYTYPES[] = {
	{"BLUEBIRD", MT_FLICKY_01}, // Flicky (Flicky)
	{"RABBIT",   MT_FLICKY_02}, // Pocky (1)
	{"CHICKEN",  MT_FLICKY_03}, // Cucky (1)
	{"SEAL",     MT_FLICKY_04}, // Rocky (1)
	{"PIG",      MT_FLICKY_05}, // Picky (1)
	{"CHIPMUNK", MT_FLICKY_06}, // Ricky (1)
	{"PENGUIN",  MT_FLICKY_07}, // Pecky (1)
	{"FISH",     MT_FLICKY_08}, // Nicky (CD)
	{"RAM",      MT_FLICKY_09}, // Flocky (CD)
	{"PUFFIN",   MT_FLICKY_10}, // Wicky (CD)
	{"COW",      MT_FLICKY_11}, // Macky (SRB2)
	{"RAT",      MT_FLICKY_12}, // Micky (2)
	{"BEAR",     MT_FLICKY_13}, // Becky (2)
	{"DOVE",     MT_FLICKY_14}, // Docky (CD)
	{"CAT",      MT_FLICKY_15}, // Nyannyan (Flicky)
	{"CANARY",   MT_FLICKY_16}, // Lucky (CD)
	{"a", 0}, // End of normal flickies - a lower case character so will never fastcmp valid with uppercase tmp
	//{"FLICKER",          MT_FLICKER}, // Flacky (SRB2)
	{"SPIDER",   MT_SECRETFLICKY_01}, // Sticky (SRB2)
	{"BAT",      MT_SECRETFLICKY_02}, // Backy (SRB2)
	{"SEED",                MT_SEED}, // Seed (CD)
	{NULL, 0}
};

actionpointer_t actionpointers[] =
{
#define _(name, upper, ...) {{name}, "A_"#upper},
#include "info/actions.h"
#undef _

	{{NULL},                     "NONE"},

	// This NULL entry must be the last in the list
	{{NULL},                   NULL},
};

////////////////////////////////////////////////////////////////////////////////
// CRAZY LIST OF STATE NAMES AND ALL FROM HERE DOWN
// TODO: Make this all a seperate file or something, like part of info.c??
// TODO: Read the list from a text lump in a WAD as necessary instead
// or something, don't just keep it all in memory like this.
// TODO: Make the lists public so we can start using actual mobj
// and state names in warning and error messages! :D

const char *const STATE_LIST[] = { // array length left dynamic for sanity testing later.
#define _(name, ...) "S_"#name,
#include "info/states.h"
#undef _
};

struct int_const_s const STATE_ALIASES[] = {
	{"KART_STND1",    	S_KART_STILL},
	{"KART_STND2" ,   	S_KART_STILL},
	{"KART_STND1_L" , 	S_KART_STILL_L},
	{"KART_STND2_L",	S_KART_STILL_L},
	{"KART_STND1_R",	S_KART_STILL_R},
	{"KART_STND2_R",	S_KART_STILL_R},
	{"KART_WALK1",		S_KART_SLOW},
	{"KART_WALK2",		S_KART_SLOW},
	{"KART_WALK1_L",  	S_KART_SLOW_L},
	{"KART_WALK2_L",	S_KART_SLOW_L},
	{"KART_WALK1_R",	S_KART_SLOW_R},
	{"KART_WALK2_R",	S_KART_SLOW_R},
	{"KART_RUN1",	    S_KART_FAST},
	{"KART_RUN2",		S_KART_FAST},
	{"KART_RUN1_L",		S_KART_FAST_L},
	{"KART_RUN2_L",		S_KART_FAST_L},
	{"KART_RUN1_R",		S_KART_FAST_R},
	{"KART_RUN2_R",		S_KART_FAST_R},
	{"KART_DRIFT1_L",	S_KART_DRIFT_L},
	{"KART_DRIFT2_L",	S_KART_DRIFT_L},
	{"KART_DRIFT1_R",	S_KART_DRIFT_R},
	{"KART_DRIFT2_R",	S_KART_DRIFT_R},
	{"KART_DRIFT1_L",	S_KART_DRIFT_L_OUT},
	{"KART_DRIFT2_L",	S_KART_DRIFT_L_IN},
	{"KART_DRIFT1_R",	S_KART_DRIFT_R_OUT},
	{"KART_DRIFT2_R",	S_KART_DRIFT_R_IN},
	{"KART_SPIN",		S_KART_SPINOUT},
	{"KART_PAIN",		S_KART_DEAD},
	{"KART_SQUISH", 	S_KART_SPINOUT},
	{"PLAY_SIGN", 		S_KART_SIGN},
	{ NULL, 0 }
};

const char *const MOBJTYPE_LIST[] = {  // array length left dynamic for sanity testing later.
#define _(name, ...) "MT_"#name,
#include "info/mobjs.h"
#undef _
};

const char *const MOBJFLAG_LIST[] = {
	"SPECIAL",
	"SOLID",
	"SHOOTABLE",
	"NOSECTOR",
	"NOBLOCKMAP",
	"PAPERCOLLISION",
	"PUSHABLE",
	"BOSS",
	"SPAWNCEILING",
	"NOGRAVITY",
	"AMBIENT",
	"SLIDEME",
	"NOCLIP",
	"FLOAT",
	"BOXICON",
	"MISSILE",
	"SPRING",
	"MONITOR",
	"NOTHINK",
	"FIRE",
	"NOCLIPHEIGHT",
	"ENEMY",
	"SCENERY",
	"PAIN",
	"STICKY",
	"APPLYTERRAIN",
	"NOCLIPTHING",
	"GRENADEBOUNCE",
	"RUNSPAWNFUNC",
	"DONTENCOREMAP",
	"PICKUPFROMBELOW",
	"BOUNCE",
	NULL
};

// \tMF2_(\S+).*// (.+) --> \t"\1", // \2
const char *const MOBJFLAG2_LIST[] = {
	"AXIS",			  // It's a NiGHTS axis! (For faster checking)
	"\x01",			  // free: 1<<1 (name un-matchable)
	"DONTRESPAWN",	  // Don't respawn this object!
	"DONTDRAW",		  // alias for RF_DONTDRAW
	"AUTOMATIC",	  // Thrown ring has automatic properties
	"RAILRING",		  // Thrown ring has rail properties
	"BOUNCERING",	  // Thrown ring has bounce properties
	"EXPLOSION",	  // Thrown ring has explosive properties
	"SCATTER",		  // Thrown ring has scatter properties
	"BEYONDTHEGRAVE", // Source of this missile has died and has since respawned.
	"SLIDEPUSH",	  // MF_PUSHABLE that pushes continuously.
	"CLASSICPUSH",    // Drops straight down when object has negative momz.
	"INVERTAIMABLE",  // Flips whether it's targetable by A_LookForEnemies (enemies no, decoys yes)
	"INFLOAT",		  // Floating to a height for a move, don't auto float to target's height.
	"DEBRIS",		  // Splash ring from explosion ring
	"NIGHTSPULL",	  // Attracted from a paraloop
	"JUSTATTACKED",	  // can be pushed by other moving mobjs
	"FIRING",		  // turret fire
	"SUPERFIRE",	  // Firing something with Super Sonic-stopping properties. Or, if mobj has MF_MISSILE, this is the actual fire from it.
	"\x01",			  // free: 1<<20 (name un-matchable)
	"STRONGBOX",	  // Flag used for "strong" random monitors.
	"OBJECTFLIP",	  // Flag for objects that always have flipped gravity.
	"SKULLFLY",		  // Special handling: skull in flight.
	"FRET",			  // Flashing from a previous hit
	"BOSSNOTRAP",	  // No Egg Trap after boss
	"BOSSFLEE",		  // Boss is fleeing!
	"BOSSDEAD",		  // Boss is dead! (Not necessarily fleeing, if a fleeing point doesn't exist.)
	"AMBUSH",         // Alternate behaviour typically set by MTF_AMBUSH
	"LINKDRAW",       // Draw vissprite of mobj immediately before/after tracer's vissprite (dependent on dispoffset and position)
	"SHIELD",         // Thinker calls P_AddShield/P_ShieldLook (must be partnered with MF_SCENERY to use)
	NULL
};

const char *const MOBJEFLAG_LIST[] = {
	"ONGROUND", // The mobj stands on solid floor (not on another mobj or in air)
	"JUSTHITFLOOR", // The mobj just hit the floor while falling, this is cleared on next frame
	"TOUCHWATER", // The mobj stands in a sector with water, and touches the surface
	"UNDERWATER", // The mobj stands in a sector with water, and his waist is BELOW the water surface
	"JUSTSTEPPEDDOWN", // used for ramp sectors
	"VERTICALFLIP", // Vertically flip sprite/allow upside-down physics
	"GOOWATER", // Goo water
	"TOUCHLAVA", // The mobj is touching a lava block
	"PUSHED", // Mobj was already pushed this tic
	"SPRUNG", // Mobj was already sprung this tic
	"APPLYPMOMZ", // Platform movement
	"TRACERANGLE", // Compute and trigger on mobj angle relative to tracer
	"JUSTBOUNCEDWALL",
	"\x01",
	"SLOPELAUNCHED",
	"\x01",
	// eflags is only 16 bits wide...
	"\x01",
	"\x01",
	"\x01",
	"\x01",
	// but the RF_DONTDRAW flags are in bits 20-23...
	"DRAWONLYFORP1",
	"DRAWONLYFORP2",
	"DRAWONLYFORP3",
	"DRAWONLYFORP4",
	// cheeky!
	NULL
};

const char *const MAPTHINGFLAG_LIST[4] = {
	"EXTRA", // Extra flag for objects.
	"OBJECTFLIP", // Reverse gravity flag for objects.
	"OBJECTSPECIAL", // Special flag used with certain objects.
	"AMBUSH" // Deaf monsters/do not react to sound.
};

const char *const PLAYERFLAG_LIST[] = {
	"GODMODE",

	// free: 1<<1 and 1<<2 (name un-matchable)
	"\x01",
	"\x01",

	// Look back VFX has been spawned
	// TODO: Is there a better way to track this?
	"GAINAX",

	// Accessibility and cheats
	"KICKSTARTACCEL", // Is accelerate in kickstart mode?
	"GODMODE",
	"NOCLIP",

	"WANTSTOJOIN", // Spectator that wants to join

	"STASIS", // Player is not allowed to move
	"SKIDDOWN", // SKIDDOWN
	"ELIMINATED", // Battle-style elimination, no extra penalty
	"NOCONTEST", // Did not finish (last place explosion)
	"LOSTLIFE", // Do not lose life more than once

	"RINGLOCK", // Prevent picking up rings while SPB is locked on

	"DRIFTINPUT", // Drifting!
	"GETSPARKS", // Can get sparks
	"DRIFTEND", // Drift has ended, used to adjust character angle after drift
	"BRAKEDRIFT", // Helper for brake-drift spark spawning

	"AIRFAILSAFE", // Whenever or not try the air boost
	
	"UPDATEMYRESPAWN",
	"FLIPCAM",
	"\x01",

	"HITFINISHLINE", // Already hit the finish line this tic
	"WRONGWAY", // Moving the wrong way with respect to waypoints?

	"SHRINKME",
	"SHRINKACTIVE",

	NULL // stop loop here.
};

const char *const ITEMFLAG_LIST[] = {
	"USERINGS",
	"ITEMOUT",
	"EGGMANOUT",
	"HOLDREADY",

	NULL // stop loop here.
};

const char *const GAMETYPERULE_LIST[] = {
	"CAMPAIGN",
	"RINGSLINGER",
	"SPECTATORS",
	"LIVES",
	"TEAMS",
	"FIRSTPERSON",
	"POWERSTONES",
	"TEAMFLAGS",
	"FRIENDLY",
	"SPECIALSTAGES",
	"EMERALDTOKENS",
	"EMERALDHUNT",
	"RACE",
	"TAG",
	"POINTLIMIT",
	"TIMELIMIT",
	"OVERTIME",
	"HURTMESSAGES",
	"FRIENDLYFIRE",
	"STARTCOUNTDOWN",
	"HIDEFROZEN",
	"BLINDFOLDED",
	"RESPAWNDELAY",
	"PITYSHIELD",
	"DEATHPENALTY",
	"NOSPECTATORSPAWN",
	"DEATHMATCHSTARTS",
	"SPAWNINVUL",
	"SPAWNENEMIES",
	"ALLOWEXIT",
	"NOTITLECARD",
	"CUTSCENES",
	NULL
};

// Linedef flags
const char *const ML_LIST[] = {
	"IMPASSABLE",
	"BLOCKMONSTERS",
	"TWOSIDED",
	"DONTPEGTOP",
	"DONTPEGBOTTOM",
	"SKEWTD",
	"NOCLIMB",
	"NOSKEW",
	"MIDPEG",
	"MIDSOLID",
	"WRAPMIDTEX",
	"NOSONIC",
	"NOTAILS",
	"NOKNUX",
	"NETONLY",
	"NETONLY_OLD",
	"NONET",
	"BLOCKMONSTERS",
	"NOTBOUNCY",
	"TFERLINE",
	NULL
};

// Sector flags
const char *const MSF_LIST[] = {
	"FLIPSPECIAL_FLOOR",
	"FLIPSPECIAL_CEILING",
	"TRIGGERSPECIAL_TOUCH",
	"TRIGGERSPECIAL_HEADBUMP",
	"TRIGGERLINE_PLANE",
	"TRIGGERLINE_MOBJ",
	"INVERTPRECIP",
	"GRAVITYFLIP",
	"HEATWAVE",
	"NOCLIPCAMERA",
	"RIPPLE_FLOOR",
	"RIPPLE_CEILING",
	"INVERTENCORE",
	"FLATLIGHTING",
	"DIRECTIONLIGHTING",
	NULL
};

// Sector special flags
const char *const SSF_LIST[] = {
	"NOSTEPUP",
	"DOUBLESTEPUP",
	"NOSTEPDOWN",
	"WINDCURRENT",
	"CONVEYOR",
	"SPEEDPAD",
	"STARPOSTACTIVATOR",
	"EXIT",
	"DELETEITEMS",
	"SNEAKERPANEL" ,
	"REDPOGOSPRING",
	"YELLOWPOGOSPRING" ,
	"FAN",
	"FINISHLINE",
	"\x01",			  // free (name un-matchable)
	"ZOOMTUBESTART",
	"ZOOMTUBEEND",
	NULL
};

// Sector damagetypes
const char *const SD_LIST[] = {
	"NONE",
	"GENERIC",
	"LAVA",
	"DEATHPIT",
	"INSTAKILL",
	NULL
};

// Sector triggerer
const char *const TO_LIST[] = {
	"PLAYER",
	"ALLPLAYERS",
	"MOBJ",
	NULL
};

const char *COLOR_ENUMS[] = {
#define _(name, ...) #name,
#include "info/skincolors.h"
#undef _
};

const char *const POWERS_LIST[] = {
	"INVULNERABILITY",
	"SNEAKERS",
	"FLASHING",
	"SHIELD",
	"TAILSFLY", // tails flying
	"UNDERWATER", // underwater timer
	"SPACETIME", // In space, no one can hear you spin!
	"EXTRALIFE", // Extra Life timer

	"SUPER", // Are you super?
	"GRAVITYBOOTS", // gravity boots

	// Weapon ammunition
	"INFINITYRING",
	"AUTOMATICRING",
	"BOUNCERING",
	"SCATTERRING",
	"GRENADERING",
	"EXPLOSIONRING",
	"RAILRING",

	// Power Stones
	"EMERALDS", // stored like global 'emeralds' variable

	// NiGHTS powerups
	"NIGHTS_SUPERLOOP",
	"NIGHTS_HELPER",
	"NIGHTS_LINKFREEZE",

	//for linedef exec 427
	"NOCONTROL",
	"INGOOP" // In goop
};

const char *const KARTSTUFF_LIST[] = {
	"POSITION",
	"OLDPOSITION",
	"POSITIONDELAY",
	"PREVCHECK",
	"NEXTCHECK",
	"WAYPOINT",
	"STARPOSTWP",
	"STARPOSTFLIP",
	"RESPAWN",
	"DROPDASH",

	"THROWDIR",
	"LAPANIMATION",
	"LAPHAND",
	"CARDANIMATION",
	"VOICES",
	"TAUNTVOICES",
	"INSTASHIELD",
	"ENGINESND",

	"FLOORBOOST",
	"SPINOUTTYPE",

	"DRIFT",
	"DRIFTEND",
	"DRIFTCHARGE",
	"DRIFTBOOST",
	"BOOSTCHARGE",
	"STARTBOOST",
	"JMP",
	"OFFROAD",
	"POGOSPRING",
	"BRAKESTOP",
	"WATERSKIP",
	"DASHPADCOOLDOWN",
	"BOOSTPOWER",
	"SPEEDBOOST",
	"ACCELBOOST",
	"BOOSTANGLE",
	"BOOSTCAM",
	"DESTBOOSTCAM",
	"TIMEOVERCAM",
	"AIZDRIFTSTRAT",
	"BRAKEDRIFT",

	"ITEMROULETTE",
	"ROULETTETYPE",

	"ITEMTYPE",
	"ITEMAMOUNT",
	"ITEMHELD",

	"CURSHIELD",
	"HYUDOROTIMER",
	"STEALINGTIMER",
	"STOLENTIMER",
	"SNEAKERTIMER",
	"GROWSHRINKTIMER",
	"SQUISHEDTIMER",
	"ROCKETSNEAKERTIMER",
	"INVINCIBILITYTIMER",
	"EGGMANHELD",
	"EGGMANEXPLODE",
	"EGGMANBLAME",
	"LASTJAWZTARGET",
	"BANANADRAG",
	"SPINOUTTIMER",
	"WIPEOUTSLOW",
	"JUSTBUMPED",
	"COMEBACKTIMER",
	"SADTIMER",

	"BUMPER",
	"COMEBACKPOINTS",
	"COMEBACKMODE",
	"WANTED",
	"YOUGOTEM",

	"ITEMBLINK",
	"ITEMBLINKMODE",
	"GETSPARKS",
	"JAWZTARGETDELAY",
	"SPECTATEWAIT",
	"GROWCANCEL"
};

const char *const KARTHUD_LIST[] = {
	"ITEMBLINK",
	"ITEMBLINKMODE",

	"RINGFRAME",
	"RINGTICS",
	"RINGDELAY",
	"RINGSPBBLOCK",

	"LAPANIMATION",
	"LAPHAND",

	"FAULT",

	"BOOSTCAM",
	"DESTBOOSTCAM",
	"TIMEOVERCAM",

	"ENGINESND",
	"VOICES",
	"TAUNTVOICES",

	"CARDANIMATION",
	"YOUGOTEM",
};

const char *const HUDITEMS_LIST[] = {
	"LIVES",

	"RINGS",
	"RINGSNUM",
	"RINGSNUMTICS",

	"SCORE",
	"SCORENUM",

	"TIME",
	"MINUTES",
	"TIMECOLON",
	"SECONDS",
	"TIMETICCOLON",
	"TICS",

	"SS_TOTALRINGS",

	"GETRINGS",
	"GETRINGSNUM",
	"TIMELEFT",
	"TIMELEFTNUM",
	"TIMEUP",
	"HUNTPICS",
	"POWERUPS"
};

const char *const MENUTYPES_LIST[] = {
	"NONE",

	"MAIN",

	// Single Player
	"SP_MAIN",

	"SP_LOAD",
	"SP_PLAYER",

	"SP_LEVELSELECT",
	"SP_LEVELSTATS",

	"SP_TIMEATTACK",
	"SP_TIMEATTACK_LEVELSELECT",
	"SP_GUESTREPLAY",
	"SP_REPLAY",
	"SP_GHOST",

	"SP_NIGHTSATTACK",
	"SP_NIGHTS_LEVELSELECT",
	"SP_NIGHTS_GUESTREPLAY",
	"SP_NIGHTS_REPLAY",
	"SP_NIGHTS_GHOST",

	// Multiplayer
	"MP_MAIN",
	"MP_SPLITSCREEN", // SplitServer
	"MP_SERVER",
	"MP_CONNECT",
	"MP_ROOM",
	"MP_PLAYERSETUP", // MP_PlayerSetupDef shared with SPLITSCREEN if #defined NONET
	"MP_SERVER_OPTIONS",

	// Options
	"OP_MAIN",

	"OP_P1CONTROLS",
	"OP_CHANGECONTROLS", // OP_ChangeControlsDef shared with P2
	"OP_P1MOUSE",
	"OP_P1JOYSTICK",
	"OP_JOYSTICKSET", // OP_JoystickSetDef shared with P2
	"OP_P1CAMERA",

	"OP_P2CONTROLS",
	"OP_P2MOUSE",
	"OP_P2JOYSTICK",
	"OP_P2CAMERA",

	"OP_PLAYSTYLE",

	"OP_VIDEO",
	"OP_VIDEOMODE",
	"OP_COLOR",
	"OP_OPENGL",
	"OP_OPENGL_LIGHTING",

	"OP_SOUND",

	"OP_SERVER",
	"OP_MONITORTOGGLE",

	"OP_DATA",
	"OP_ADDONS",
	"OP_SCREENSHOTS",
	"OP_ERASEDATA",

	// Extras
	"SR_MAIN",
	"SR_PANDORA",
	"SR_LEVELSELECT",
	"SR_UNLOCKCHECKLIST",
	"SR_EMBLEMHINT",
	"SR_PLAYER",
	"SR_SOUNDTEST",

	// Addons (Part of MISC, but let's make it our own)
	"AD_MAIN",

	// MISC
	// "MESSAGE",
	// "SPAUSE",

	// "MPAUSE",
	// "SCRAMBLETEAM",
	// "CHANGETEAM",
	// "CHANGELEVEL",

	// "MAPAUSE",
	// "HELP",

	"SPECIAL"
};

struct int_const_s const INT_CONST[] = {
	// If a mod removes some variables here,
	// please leave the names in-tact and just set
	// the value to 0 or something.

	// integer type limits, from doomtype.h
	// INT64 and UINT64 limits not included, they're too big for most purposes anyway
	// signed
	{"INT8_MIN",INT8_MIN},
	{"INT16_MIN",INT16_MIN},
	{"INT32_MIN",INT32_MIN},
	{"INT8_MAX",INT8_MAX},
	{"INT16_MAX",INT16_MAX},
	{"INT32_MAX",INT32_MAX},
	// unsigned
	{"UINT8_MAX",UINT8_MAX},
	{"UINT16_MAX",UINT16_MAX},
	{"UINT32_MAX",UINT32_MAX},

	// fixed_t constants, from m_fixed.h
	{"FRACUNIT",FRACUNIT},
	{"FU"      ,FRACUNIT},
	{"FRACBITS",FRACBITS},
	{"M_TAU_FIXED",M_TAU_FIXED},

	// doomdef.h constants
	{"TICRATE",TICRATE},
	{"MUSICRATE",MUSICRATE},
	{"RING_DIST",RING_DIST},
	{"PUSHACCEL",PUSHACCEL},
	{"MODID",MODID}, // I don't know, I just thought it would be cool for a wad to potentially know what mod it was loaded into.
	{"MODVERSION",MODVERSION}, // or what version of the mod this is.
	{"CODEBASE",CODEBASE}, // or what release of SRB2 this is.
	{"NEWTICRATE",NEWTICRATE}, // TICRATE*NEWTICRATERATIO
	{"NEWTICRATERATIO",NEWTICRATERATIO},

	// Special linedef executor tag numbers!
	{"LE_PINCHPHASE",LE_PINCHPHASE}, // A boss entered pinch phase (and, in most cases, is preparing their pinch phase attack!)
	{"LE_ALLBOSSESDEAD",LE_ALLBOSSESDEAD}, // All bosses in the map are dead (Egg capsule raise)
	{"LE_BOSSDEAD",LE_BOSSDEAD}, // A boss in the map died (Chaos mode boss tally)
	{"LE_BOSS4DROP",LE_BOSS4DROP}, // CEZ boss dropped its cage
	{"LE_BRAKVILEATACK",LE_BRAKVILEATACK}, // Brak's doing his LOS attack, oh noes
	{"LE_TURRET",LE_TURRET}, // THZ turret
	{"LE_BRAKPLATFORM",LE_BRAKPLATFORM}, // v2.0 Black Eggman destroys platform
	{"LE_CAPSULE2",LE_CAPSULE2}, // Egg Capsule
	{"LE_CAPSULE1",LE_CAPSULE1}, // Egg Capsule
	{"LE_CAPSULE0",LE_CAPSULE0}, // Egg Capsule
	{"LE_KOOPA",LE_KOOPA}, // Distant cousin to Gay Bowser
	{"LE_AXE",LE_AXE}, // MKB Axe object
	{"LE_PARAMWIDTH",LE_PARAMWIDTH},  // If an object that calls LinedefExecute has a nonzero parameter value, this times the parameter will be subtracted. (Mostly for the purpose of coexisting bosses...)

	/// \todo Get all this stuff into its own sections, maybe. Maybe.

	// Frame settings
	{"FF_FRAMEMASK",FF_FRAMEMASK},
	{"FF_SPR2SUPER",FF_SPR2SUPER},
	{"FF_SPR2ENDSTATE",FF_SPR2ENDSTATE},
	{"FF_SPR2MIDSTART",FF_SPR2MIDSTART},
	{"FF_ANIMATE",FF_ANIMATE},
	{"FF_RANDOMANIM",FF_RANDOMANIM},
	{"FF_GLOBALANIM",FF_GLOBALANIM},
	{"FF_FULLBRIGHT",FF_FULLBRIGHT},
	{"FF_FULLDARK",FF_FULLDARK},
	{"FF_SEMIBRIGHT",FF_SEMIBRIGHT},
	{"FF_VERTICALFLIP",FF_VERTICALFLIP},
	{"FF_HORIZONTALFLIP",FF_HORIZONTALFLIP},
	{"FF_PAPERSPRITE",FF_PAPERSPRITE},
	{"FF_FLOORSPRITE",FF_FLOORSPRITE},
	{"FF_BLENDMASK",FF_BLENDMASK},
	{"FF_BLENDSHIFT",FF_BLENDSHIFT},
	{"FF_ADD",FF_ADD},
	{"FF_SUBTRACT",FF_SUBTRACT},
	{"FF_REVERSESUBTRACT",FF_REVERSESUBTRACT},
	{"FF_MODULATE",FF_MODULATE},
	{"FF_OVERLAY",FF_OVERLAY},
	{"FF_TRANSMASK",FF_TRANSMASK},
	{"FF_TRANSSHIFT",FF_TRANSSHIFT},
	// new preshifted translucency (used in source)
	{"FF_TRANS10",FF_TRANS10},
	{"FF_TRANS20",FF_TRANS20},
	{"FF_TRANS30",FF_TRANS30},
	{"FF_TRANS40",FF_TRANS40},
	{"FF_TRANS50",FF_TRANS50},
	{"FF_TRANS60",FF_TRANS60},
	{"FF_TRANS70",FF_TRANS70},
	{"FF_TRANS80",FF_TRANS80},
	{"FF_TRANS90",FF_TRANS90},
	// temporary, for testing
	{"FF_TRANSADD",FF_ADD},
	{"FF_TRANSSUB",FF_SUBTRACT},
	// compatibility
	// Transparency for SOCs is pre-shifted
	{"TR_TRANS10",tr_trans10<<FF_TRANSSHIFT},
	{"TR_TRANS20",tr_trans20<<FF_TRANSSHIFT},
	{"TR_TRANS30",tr_trans30<<FF_TRANSSHIFT},
	{"TR_TRANS40",tr_trans40<<FF_TRANSSHIFT},
	{"TR_TRANS50",tr_trans50<<FF_TRANSSHIFT},
	{"TR_TRANS60",tr_trans60<<FF_TRANSSHIFT},
	{"TR_TRANS70",tr_trans70<<FF_TRANSSHIFT},
	{"TR_TRANS80",tr_trans80<<FF_TRANSSHIFT},
	{"TR_TRANS90",tr_trans90<<FF_TRANSSHIFT},
	// Transparency for Lua is not, unless capitalized as above.
	{"tr_trans10",tr_trans10},
	{"tr_trans20",tr_trans20},
	{"tr_trans30",tr_trans30},
	{"tr_trans40",tr_trans40},
	{"tr_trans50",tr_trans50},
	{"tr_trans60",tr_trans60},
	{"tr_trans70",tr_trans70},
	{"tr_trans80",tr_trans80},
	{"tr_trans90",tr_trans90},
	{"NUMTRANSMAPS",NUMTRANSMAPS},

	// Alpha styles (blend modes)
	//{"AST_COPY",AST_COPY}, -- not useful in lua
	//{"AST_TRANSLUCENT",AST_TRANSLUCENT}, -- ditto
	{"AST_ADD",AST_ADD},
	{"AST_SUBTRACT",AST_SUBTRACT},
	{"AST_REVERSESUBTRACT",AST_REVERSESUBTRACT},
	{"AST_MODULATE",AST_MODULATE},
	{"AST_OVERLAY",AST_OVERLAY},
	{"AST_FOG",AST_FOG},

	// Render flags
	{"RF_HORIZONTALFLIP",RF_HORIZONTALFLIP},
	{"RF_VERTICALFLIP",RF_VERTICALFLIP},
	{"RF_ABSOLUTEOFFSETS",RF_ABSOLUTEOFFSETS},
	{"RF_FLIPOFFSETS",RF_FLIPOFFSETS},
	{"RF_SPLATMASK",RF_SPLATMASK},
	{"RF_SLOPESPLAT",RF_SLOPESPLAT},
	{"RF_OBJECTSLOPESPLAT",RF_OBJECTSLOPESPLAT},
	{"RF_NOSPLATBILLBOARD",RF_NOSPLATBILLBOARD},
	{"RF_NOSPLATROLLANGLE",RF_NOSPLATROLLANGLE},
	{"RF_BRIGHTMASK",RF_BRIGHTMASK},
	{"RF_FULLBRIGHT",RF_FULLBRIGHT},
	{"RF_FULLDARK",RF_FULLDARK},
	{"RF_SEMIBRIGHT",RF_SEMIBRIGHT},
	{"RF_NOCOLORMAPS",RF_NOCOLORMAPS},
	{"RF_ALWAYSONTOP",RF_ALWAYSONTOP},
	{"RF_SPRITETYPEMASK",RF_SPRITETYPEMASK},
	{"RF_PAPERSPRITE",RF_PAPERSPRITE},
	{"RF_FLOORSPRITE",RF_FLOORSPRITE},
	{"RF_SHADOWDRAW",RF_SHADOWDRAW},
	{"RF_SHADOWEFFECTS",RF_SHADOWEFFECTS},
	{"RF_DROPSHADOW",RF_DROPSHADOW},
	{"RF_ABSOLUTELIGHTLEVEL",RF_ABSOLUTELIGHTLEVEL},
	{"RF_HIDEINSKYBOX",RF_HIDEINSKYBOX},
	{"RF_DONTDRAW",RF_DONTDRAW},
	{"RF_DONTDRAWP1",RF_DONTDRAWP1},
	{"RF_DONTDRAWP2",RF_DONTDRAWP2},
	{"RF_DONTDRAWP3",RF_DONTDRAWP3},
	{"RF_DONTDRAWP4",RF_DONTDRAWP4},
	{"RF_BLENDMASK",RF_BLENDMASK},
	{"RF_BLENDSHIFT",RF_BLENDSHIFT},
	{"RF_ADD",RF_ADD},
	{"RF_SUBTRACT",RF_SUBTRACT},
	{"RF_REVERSESUBTRACT",RF_REVERSESUBTRACT},
	{"RF_MODULATE",RF_MODULATE},
	{"RF_OVERLAY",RF_OVERLAY},
	{"RF_TRANSMASK",RF_TRANSMASK},
	{"RF_TRANSSHIFT",RF_TRANSSHIFT},
	{"RF_TRANS10",RF_TRANS10},
	{"RF_TRANS20",RF_TRANS20},
	{"RF_TRANS30",RF_TRANS30},
	{"RF_TRANS40",RF_TRANS40},
	{"RF_TRANS50",RF_TRANS50},
	{"RF_TRANS60",RF_TRANS60},
	{"RF_TRANS70",RF_TRANS70},
	{"RF_TRANS80",RF_TRANS80},
	{"RF_TRANS90",RF_TRANS90},
	{"RF_GHOSTLY",RF_GHOSTLY},
	{"RF_GHOSTLYMASK",RF_GHOSTLYMASK},

	// Level flags
	{"LF_SCRIPTISFILE",LF_SCRIPTISFILE},
	{"LF_NOZONE",LF_NOZONE},
	{"LF_SECTIONRACE",LF_SECTIONRACE},
	{"LF_SUBTRACTNUM",LF_SUBTRACTNUM},
	// And map flags
	{"LF2_HIDEINMENU",LF2_HIDEINMENU},
	{"LF2_HIDEINSTATS",LF2_HIDEINSTATS},
	{"LF2_NOTIMEATTACK",LF2_NOTIMEATTACK},
	{"LF2_VISITNEEDED",LF2_VISITNEEDED},

	// Emeralds
	{"EMERALD1",EMERALD1},
	{"EMERALD2",EMERALD2},
	{"EMERALD3",EMERALD3},
	{"EMERALD4",EMERALD4},
	{"EMERALD5",EMERALD5},
	{"EMERALD6",EMERALD6},
	{"EMERALD7",EMERALD7},

	// SKINCOLOR_ doesn't include these..!
	// MAXSKINCOLORS has been made a global for lua usage :)
	//{"MAXSKINCOLORS",MAXSKINCOLORS},
	{"FIRSTSUPERCOLOR",FIRSTSUPERCOLOR},
	{"NUMSUPERCOLORS",NUMSUPERCOLORS},

	// Precipitation
	{"PRECIP_NONE",PRECIP_NONE},
	{"PRECIP_RAIN",PRECIP_RAIN},
	{"PRECIP_SNOW",PRECIP_SNOW},
	{"PRECIP_BLIZZARD",PRECIP_BLIZZARD},
	{"PRECIP_STORM",PRECIP_STORM},
	{"PRECIP_STORM_NORAIN",PRECIP_STORM_NORAIN},
	{"PRECIP_STORM_NOSTRIKES",PRECIP_STORM_NOSTRIKES},

	// Carrying
	{"CR_NONE",CR_NONE},
	{"CR_ZOOMTUBE",CR_ZOOMTUBE},

	// Character flags (skinflags_t)
	{"SF_HIRES",SF_HIRES},
	{"SF_MACHINE",SF_MACHINE},

	// Sound flags
	{"SF_TOTALLYSINGLE",SF_TOTALLYSINGLE},
	{"SF_NOMULTIPLESOUND",SF_NOMULTIPLESOUND},
	{"SF_OUTSIDESOUND",SF_OUTSIDESOUND},
	{"SF_X4AWAYSOUND",SF_X4AWAYSOUND},
	{"SF_X8AWAYSOUND",SF_X8AWAYSOUND},
	{"SF_NOINTERRUPT",SF_NOINTERRUPT},
	{"SF_X2AWAYSOUND",SF_X2AWAYSOUND},

	// Global emblem var flags
	// none in kart yet

	// Map emblem var flags
	{"ME_ENCORE",ME_ENCORE},

	// p_local.h constants
	{"FLOATSPEED",FLOATSPEED},
	{"MAXSTEPMOVE",MAXSTEPMOVE},
	{"USERANGE",USERANGE},
	{"MELEERANGE",MELEERANGE},
	{"MISSILERANGE",MISSILERANGE},
	{"ONFLOORZ",ONFLOORZ}, // INT32_MIN
	{"ONCEILINGZ",ONCEILINGZ}, //INT32_MAX
	{"MAXRADIUS",MAXRADIUS},

	// for P_FlashPal
	{"PAL_WHITE",PAL_WHITE},
	{"PAL_MIXUP",PAL_MIXUP},
	{"PAL_RECYCLE",PAL_RECYCLE},
	{"PAL_NUKE",PAL_NUKE},

	// for P_DamageMobj
	//// Damage types
	{"DMG_NORMAL",DMG_NORMAL},
	{"DMG_SQUISH",DMG_SQUISH},
	{"DMG_WIPEOUT",DMG_WIPEOUT},
	{"DMG_EXPLODE",DMG_EXPLODE},
	{"DMG_STING",DMG_STING},
	{"DMG_KARMA",DMG_KARMA},
	//// Death types
	{"DMG_INSTAKILL",DMG_INSTAKILL},
	{"DMG_DEATHPIT",DMG_DEATHPIT},
	{"DMG_CRUSHED",DMG_CRUSHED},
	{"DMG_SPECTATOR",DMG_SPECTATOR},
	{"DMG_TIMEOVER",DMG_TIMEOVER},
	//// Masks
	{"DMG_STEAL",DMG_STEAL},
	{"DMG_CANTHURTSELF",DMG_CANTHURTSELF},
	{"DMG_DEATHMASK",DMG_DEATHMASK},
	{"DMG_TYPEMASK",DMG_TYPEMASK},

	// Intermission types
	{"int_none",int_none},
	{"int_race",int_race},
	{"int_battle",int_battle},
	{"int_battletime", int_battletime},

	// Jingles (jingletype_t)
	{"JT_NONE",JT_NONE},
	{"JT_OTHER",JT_OTHER},
	{"JT_MASTER",JT_MASTER},
	{"JT_1UP",JT_1UP},
	{"JT_SHOES",JT_SHOES},
	{"JT_INV",JT_INV},
	{"JT_MINV",JT_MINV},
	{"JT_DROWN",JT_DROWN},
	{"JT_SUPER",JT_SUPER},
	{"JT_GOVER",JT_GOVER},
	{"JT_NIGHTSTIMEOUT",JT_NIGHTSTIMEOUT},
	{"JT_SSTIMEOUT",JT_SSTIMEOUT},
	// {"JT_LCLEAR",JT_LCLEAR},
	// {"JT_RACENT",JT_RACENT},
	// {"JT_CONTSC",JT_CONTSC},

	// Player state (playerstate_t)
	{"PST_LIVE",PST_LIVE}, // Playing or camping.
	{"PST_DEAD",PST_DEAD}, // Dead on the ground, view follows killer.
	{"PST_REBORN",PST_REBORN}, // Ready to restart/respawn???

	// Player animation (panim_t)
	{"PA_ETC",PA_ETC},
	{"PA_STILL",PA_STILL},
	{"PA_SLOW",PA_SLOW},
	{"PA_FAST",PA_FAST},
	{"PA_DRIFT",PA_DRIFT},
	{"PA_HURT",PA_HURT},

	// Customisable sounds for Skins, from sounds.h
	// SRB2kart
	{"SKSKWIN",SKSKWIN}, // Win quote
	{"SKSKLOSE",SKSKLOSE}, // Lose quote
	{"SKSKPAN1",SKSKPAN1}, // Pain
	{"SKSKPAN2",SKSKPAN2},
	{"SKSKATK1",SKSKATK1}, // Offense item taunt
	{"SKSKATK2",SKSKATK2},
	{"SKSKBST1",SKSKBST1}, // Boost item taunt
	{"SKSKBST2",SKSKBST2},
	{"SKSKSLOW",SKSKSLOW}, // Overtake taunt
	{"SKSKHITM",SKSKHITM}, // Hit confirm taunt
	{"SKSKPOWR",SKSKPOWR}, // Power item taunt

	// 3D Floor/Fake Floor/FOF/whatever flags
	{"FOF_EXISTS",FOF_EXISTS},                   ///< Always set, to check for validity.
	{"FOF_BLOCKPLAYER",FOF_BLOCKPLAYER},         ///< Solid to player, but nothing else
	{"FOF_BLOCKOTHERS",FOF_BLOCKOTHERS},         ///< Solid to everything but player
	{"FOF_SOLID",FOF_SOLID},                     ///< Clips things.
	{"FOF_RENDERSIDES",FOF_RENDERSIDES},         ///< Renders the sides.
	{"FOF_RENDERPLANES",FOF_RENDERPLANES},       ///< Renders the floor/ceiling.
	{"FOF_RENDERALL",FOF_RENDERALL},             ///< Renders everything.
	{"FOF_SWIMMABLE",FOF_SWIMMABLE},             ///< Is a water block.
	{"FOF_NOSHADE",FOF_NOSHADE},                 ///< Messes with the lighting?
	{"FOF_CUTSOLIDS",FOF_CUTSOLIDS},             ///< Cuts out hidden solid pixels.
	{"FOF_CUTEXTRA",FOF_CUTEXTRA},               ///< Cuts out hidden translucent pixels.
	{"FOF_CUTLEVEL",FOF_CUTLEVEL},               ///< Cuts out all hidden pixels.
	{"FOF_CUTSPRITES",FOF_CUTSPRITES},           ///< Final step in making 3D water.
	{"FOF_BOTHPLANES",FOF_BOTHPLANES},           ///< Render inside and outside planes.
	{"FOF_EXTRA",FOF_EXTRA},                     ///< Gets cut by ::FOF_CUTEXTRA.
	{"FOF_TRANSLUCENT",FOF_TRANSLUCENT},         ///< See through!
	{"FOF_FOG",FOF_FOG},                         ///< Fog "brush."
	{"FOF_INVERTPLANES",FOF_INVERTPLANES},       ///< Only render inside planes.
	{"FOF_ALLSIDES",FOF_ALLSIDES},               ///< Render inside and outside sides.
	{"FOF_INVERTSIDES",FOF_INVERTSIDES},         ///< Only render inside sides.
	{"FOF_DOUBLESHADOW",FOF_DOUBLESHADOW},       ///< Make two lightlist entries to reset light?
	{"FOF_FLOATBOB",FOF_FLOATBOB},               ///< Floats on water and bobs if you step on it.
	{"FOF_NORETURN",FOF_NORETURN},               ///< Used with ::FOF_CRUMBLE. Will not return to its original position after falling.
	{"FOF_CRUMBLE",FOF_CRUMBLE},                 ///< Falls 2 seconds after being stepped on, and randomly brings all touching crumbling 3dfloors down with it, providing their master sectors share the same tag (allows crumble platforms above or below, to also exist).
	{"FOF_GOOWATER",FOF_GOOWATER},               ///< Used with ::FOF_SWIMMABLE. Makes thick bouncey goop.
	{"FOF_MARIO",FOF_MARIO},                     ///< Acts like a question block when hit from underneath. Goodie spawned at top is determined by master sector.
	{"FOF_BUSTUP",FOF_BUSTUP},                   ///< You can spin through/punch this block and it will crumble!
	{"FOF_QUICKSAND",FOF_QUICKSAND},             ///< Quicksand!
	{"FOF_PLATFORM",FOF_PLATFORM},               ///< You can jump up through this to the top.
	{"FOF_REVERSEPLATFORM",FOF_REVERSEPLATFORM}, ///< A fall-through floor in normal gravity, a platform in reverse gravity.
	{"FOF_INTANGIBLEFLATS",FOF_INTANGIBLEFLATS}, ///< Both flats are intangible, but the sides are still solid.
	{"FOF_RIPPLE",FOF_RIPPLE},                   ///< Ripple the flats
	{"FOF_COLORMAPONLY",FOF_COLORMAPONLY},       ///< Only copy the colormap, not the lightlevel
	{"FOF_BOUNCY",FOF_BOUNCY},                   ///< Bounces players
	{"FOF_SPLAT",FOF_SPLAT},                     ///< Use splat flat renderer (treat cyan pixels as invisible)

	// Old FOF flags for backwards compatibility
	{"FF_EXISTS",FF_OLD_EXISTS},
	{"FF_BLOCKPLAYER",FF_OLD_BLOCKPLAYER},
	{"FF_BLOCKOTHERS",FF_OLD_BLOCKOTHERS},
	{"FF_SOLID",FF_OLD_SOLID},
	{"FF_RENDERSIDES",FF_OLD_RENDERSIDES},
	{"FF_RENDERPLANES",FF_OLD_RENDERPLANES},
	{"FF_RENDERALL",FF_OLD_RENDERALL},
	{"FF_SWIMMABLE",FF_OLD_SWIMMABLE},
	{"FF_NOSHADE",FF_OLD_NOSHADE},
	{"FF_CUTSOLIDS",FF_OLD_CUTSOLIDS},
	{"FF_CUTEXTRA",FF_OLD_CUTEXTRA},
	{"FF_CUTLEVEL",FF_OLD_CUTLEVEL},
	{"FF_CUTSPRITES",FF_OLD_CUTSPRITES},
	{"FF_BOTHPLANES",FF_OLD_BOTHPLANES},
	{"FF_EXTRA",FF_OLD_EXTRA},
	{"FF_TRANSLUCENT",FF_OLD_TRANSLUCENT},
	{"FF_FOG",FF_OLD_FOG},
	{"FF_INVERTPLANES",FF_OLD_INVERTPLANES},
	{"FF_ALLSIDES",FF_OLD_ALLSIDES},
	{"FF_INVERTSIDES",FF_OLD_INVERTSIDES},
	{"FF_DOUBLESHADOW",FF_OLD_DOUBLESHADOW},
	{"FF_FLOATBOB",FF_OLD_FLOATBOB},
	{"FF_NORETURN",FF_OLD_NORETURN},
	{"FF_CRUMBLE",FF_OLD_CRUMBLE},
	{"FF_SHATTERBOTTOM",FF_OLD_SHATTERBOTTOM},
	{"FF_GOOWATER",FF_OLD_GOOWATER},
	{"FF_MARIO",FF_OLD_MARIO},
	{"FF_BUSTUP",FF_OLD_BUSTUP},
	{"FF_QUICKSAND",FF_OLD_QUICKSAND},
	{"FF_PLATFORM",FF_OLD_PLATFORM},
	{"FF_REVERSEPLATFORM",FF_OLD_REVERSEPLATFORM},
	{"FF_INTANGIBLEFLATS",FF_OLD_INTANGIBLEFLATS},
	{"FF_INTANGABLEFLATS",FF_OLD_INTANGIBLEFLATS},
	{"FF_SHATTER",FF_OLD_SHATTER},
	{"FF_SPINBUST",FF_OLD_SPINBUST},
	{"FF_STRONGBUST",FF_OLD_STRONGBUST},
	{"FF_RIPPLE",FF_OLD_RIPPLE},
	{"FF_COLORMAPONLY",FF_OLD_COLORMAPONLY},

	// FOF bustable flags
	{"FB_PUSHABLES",FB_PUSHABLES},
	{"FB_EXECUTOR",FB_EXECUTOR},
	{"FB_ONLYBOTTOM",FB_ONLYBOTTOM},

	// Bustable FOF type
	{"BT_TOUCH",BT_TOUCH},
	{"BT_SPINBUST",BT_SPINBUST},
	{"BT_REGULAR",BT_REGULAR},
	{"BT_STRONG",BT_STRONG},

	// PolyObject flags
	{"POF_CLIPLINES",POF_CLIPLINES},               ///< Test against lines for collision
	{"POF_CLIPPLANES",POF_CLIPPLANES},             ///< Test against tops and bottoms for collision
	{"POF_SOLID",POF_SOLID},                       ///< Clips things.
	{"POF_TESTHEIGHT",POF_TESTHEIGHT},             ///< Test line collision with heights
	{"POF_RENDERSIDES",POF_RENDERSIDES},           ///< Renders the sides.
	{"POF_RENDERTOP",POF_RENDERTOP},               ///< Renders the top.
	{"POF_RENDERBOTTOM",POF_RENDERBOTTOM},         ///< Renders the bottom.
	{"POF_RENDERPLANES",POF_RENDERPLANES},         ///< Renders top and bottom.
	{"POF_RENDERALL",POF_RENDERALL},               ///< Renders everything.
	{"POF_INVERT",POF_INVERT},                     ///< Inverts collision (like a cage).
	{"POF_INVERTPLANES",POF_INVERTPLANES},         ///< Render inside planes.
	{"POF_INVERTPLANESONLY",POF_INVERTPLANESONLY}, ///< Only render inside planes.
	{"POF_PUSHABLESTOP",POF_PUSHABLESTOP},         ///< Pushables will stop movement.
	{"POF_LDEXEC",POF_LDEXEC},                     ///< This PO triggers a linedef executor.
	{"POF_ONESIDE",POF_ONESIDE},                   ///< Only use the first side of the linedef.
	{"POF_NOSPECIALS",POF_NOSPECIALS},             ///< Don't apply sector specials.
	{"POF_SPLAT",POF_SPLAT},                       ///< Use splat flat renderer (treat cyan pixels as invisible).

#ifdef HAVE_LUA_SEGS
	// Node flags
	{"NF_SUBSECTOR",NF_SUBSECTOR}, // Indicate a leaf.
#endif

	// Slope flags
	{"SL_NOPHYSICS",SL_NOPHYSICS},
	{"SL_DYNAMIC",SL_DYNAMIC},

	// Angles
	{"ANG1",ANG1},
	{"ANG2",ANG2},
	{"ANG10",ANG10},
	{"ANG15",ANG15},
	{"ANG20",ANG20},
	{"ANG30",ANG30},
	{"ANG60",ANG60},
	{"ANG64h",ANG64h},
	{"ANG105",ANG105},
	{"ANG210",ANG210},
	{"ANG255",ANG255},
	{"ANG340",ANG340},
	{"ANG350",ANG350},
	{"ANGLE_11hh",ANGLE_11hh},
	{"ANGLE_22h",ANGLE_22h},
	{"ANGLE_45",ANGLE_45},
	{"ANGLE_67h",ANGLE_67h},
	{"ANGLE_90",ANGLE_90},
	{"ANGLE_112h",ANGLE_112h},
	{"ANGLE_135",ANGLE_135},
	{"ANGLE_157h",ANGLE_157h},
	{"ANGLE_180",ANGLE_180},
	{"ANGLE_202h",ANGLE_202h},
	{"ANGLE_225",ANGLE_225},
	{"ANGLE_247h",ANGLE_247h},
	{"ANGLE_270",ANGLE_270},
	{"ANGLE_292h",ANGLE_292h},
	{"ANGLE_315",ANGLE_315},
	{"ANGLE_337h",ANGLE_337h},
	{"ANGLE_MAX",ANGLE_MAX},

	// P_Chase directions (dirtype_t)
	{"DI_NODIR",DI_NODIR},
	{"DI_EAST",DI_EAST},
	{"DI_NORTHEAST",DI_NORTHEAST},
	{"DI_NORTH",DI_NORTH},
	{"DI_NORTHWEST",DI_NORTHWEST},
	{"DI_WEST",DI_WEST},
	{"DI_SOUTHWEST",DI_SOUTHWEST},
	{"DI_SOUTH",DI_SOUTH},
	{"DI_SOUTHEAST",DI_SOUTHEAST},
	{"NUMDIRS",NUMDIRS},

	// Buttons (ticcmd_t)
	{"BT_ACCELERATE",BT_ACCELERATE},
	{"BT_DRIFT",BT_DRIFT},
	{"BT_BRAKE",BT_BRAKE},
	{"BT_ATTACK",BT_ATTACK},
	{"BT_FORWARD",BT_FORWARD},
	{"BT_BACKWARD",BT_BACKWARD},
	{"BT_LOOKBACK",BT_LOOKBACK},
	{"BT_CUSTOM1",BT_CUSTOM1}, // Lua customizable
	{"BT_CUSTOM2",BT_CUSTOM2}, // Lua customizable
	{"BT_CUSTOM3",BT_CUSTOM3}, // Lua customizable

	// Lua command registration flags
	{"COM_ADMIN",COM_ADMIN},
	{"COM_LOCAL",COM_LOCAL},
	{"COM_PLAYER2",COM_PLAYER2},
	{"COM_PLAYER3",COM_PLAYER3},
	{"COM_PLAYER4",COM_PLAYER4},
	{"COM_SPLITSCREEN",COM_SPLITSCREEN},
	{"COM_SSSHIFT",COM_SSSHIFT},

	// cvflags_t
	{"CV_SAVE",CV_SAVE},
	{"CV_CALL",CV_CALL},
	{"CV_NETVAR",CV_NETVAR},
	{"CV_NOINIT",CV_NOINIT},
	{"CV_FLOAT",CV_FLOAT},
	{"CV_NOTINNET",CV_NOTINNET},
	{"CV_MODIFIED",CV_MODIFIED},
	{"CV_SHOWMODIF",CV_SHOWMODIF},
	{"CV_SHOWMODIFONETIME",CV_SHOWMODIFONETIME},
	{"CV_NOSHOWHELP",CV_NOSHOWHELP},
	{"CV_HIDEN",CV_HIDEN},
	{"CV_HIDDEN",CV_HIDEN},
	{"CV_CHEAT",CV_CHEAT},
	{"CV_NOLUA",CV_NOLUA},

	// v_video flags
	{"V_NOSCALEPATCH",V_NOSCALEPATCH},
	{"V_SMALLSCALEPATCH",V_SMALLSCALEPATCH},
	{"V_MEDSCALEPATCH",V_MEDSCALEPATCH},
	{"V_6WIDTHSPACE",V_6WIDTHSPACE},
	{"V_OLDSPACING",V_OLDSPACING},
	{"V_MONOSPACE",V_MONOSPACE},

	{"V_PURPLEMAP",V_PURPLEMAP},
	{"V_YELLOWMAP",V_YELLOWMAP},
	{"V_GREENMAP",V_GREENMAP},
	{"V_BLUEMAP",V_BLUEMAP},
	{"V_REDMAP",V_REDMAP},
	{"V_GRAYMAP",V_GRAYMAP},
	{"V_ORANGEMAP",V_ORANGEMAP},
	{"V_SKYMAP",V_SKYMAP},
	{"V_LAVENDERMAP",V_LAVENDERMAP},
	{"V_GOLDMAP",V_GOLDMAP},
	{"V_AQUAMAP",V_AQUAMAP},
	{"V_MAGENTAMAP",V_MAGENTAMAP},
	{"V_PINKMAP",V_PINKMAP},
	{"V_BROWNMAP",V_BROWNMAP},
	{"V_TANMAP",V_TANMAP},

	{"V_TRANSLUCENT",V_TRANSLUCENT},
	{"V_10TRANS",V_10TRANS},
	{"V_20TRANS",V_20TRANS},
	{"V_30TRANS",V_30TRANS},
	{"V_40TRANS",V_40TRANS},
	{"V_50TRANS",V_TRANSLUCENT}, // alias
	{"V_60TRANS",V_60TRANS},
	{"V_70TRANS",V_70TRANS},
	{"V_80TRANS",V_80TRANS},
	{"V_90TRANS",V_90TRANS},
	{"V_HUDTRANSHALF",V_HUDTRANSHALF},
	{"V_HUDTRANS",V_HUDTRANS},
	{"V_BLENDSHIFT",V_BLENDSHIFT},
	{"V_BLENDMASK",V_BLENDMASK},
	{"V_ADD",V_ADD},
	{"V_SUBTRACT",V_SUBTRACT},
	{"V_REVERSESUBTRACT",V_REVERSESUBTRACT},
	{"V_MODULATE",V_MODULATE},
	{"V_OVERLAY",V_OVERLAY},
	{"V_ALLOWLOWERCASE",V_ALLOWLOWERCASE},
	{"V_FLIP",V_FLIP},
	{"V_SNAPTOTOP",V_SNAPTOTOP},
	{"V_SNAPTOBOTTOM",V_SNAPTOBOTTOM},
	{"V_SNAPTOLEFT",V_SNAPTOLEFT},
	{"V_SNAPTORIGHT",V_SNAPTORIGHT},
	{"V_NOSCALESTART",V_NOSCALESTART},
	{"V_SPLITSCREEN",V_SPLITSCREEN},
	{"V_SLIDEIN",V_SLIDEIN},

	{"V_PARAMMASK",V_PARAMMASK},
	{"V_SCALEPATCHMASK",V_SCALEPATCHMASK},
	{"V_SPACINGMASK",V_SPACINGMASK},
	{"V_CHARCOLORMASK",V_CHARCOLORMASK},
	{"V_ALPHAMASK",V_ALPHAMASK},

	{"V_CHARCOLORSHIFT",V_CHARCOLORSHIFT},
	{"V_ALPHASHIFT",V_ALPHASHIFT},

	//Kick Reasons
	{"KR_KICK",KR_KICK},
	{"KR_PINGLIMIT",KR_PINGLIMIT},
	{"KR_SYNCH",KR_SYNCH},
	{"KR_TIMEOUT",KR_TIMEOUT},
	{"KR_BAN",KR_BAN},
	{"KR_LEAVE",KR_LEAVE},

	// translation colormaps
	{"TC_DEFAULT",TC_DEFAULT},
	{"TC_BOSS",TC_BOSS},
	{"TC_METALSONIC",TC_METALSONIC},
	{"TC_ALLWHITE",TC_ALLWHITE},
	{"TC_RAINBOW",TC_RAINBOW},
	{"TC_BLINK",TC_BLINK},
	{"TC_DASHMODE",TC_DASHMODE},

	// marathonmode flags
	{"MA_INIT",MA_INIT},
	{"MA_RUNNING",MA_RUNNING},
	{"MA_NOCUTSCENES",MA_NOCUTSCENES},
	{"MA_INGAME",MA_INGAME},

	// music types
	{"MU_NONE", MU_NONE},
	{"MU_WAV", MU_WAV},
	{"MU_MOD", MU_MOD},
	{"MU_MID", MU_MID},
	{"MU_OGG", MU_OGG},
	{"MU_MP3", MU_MP3},
	{"MU_FLAC", MU_FLAC},
	{"MU_GME", MU_GME},
	{"MU_MOD_EX", MU_MOD_EX},
	{"MU_MID_EX", MU_MID_EX},

	// gamestates
	{"GS_NULL",GS_NULL},
	{"GS_LEVEL",GS_LEVEL},
	{"GS_INTERMISSION",GS_INTERMISSION},
	{"GS_CONTINUING",GS_CONTINUING},
	{"GS_TITLESCREEN",GS_TITLESCREEN},
	{"GS_TIMEATTACK",GS_TIMEATTACK},
	{"GS_CREDITS",GS_CREDITS},
	{"GS_EVALUATION",GS_EVALUATION},
	{"GS_GAMEEND",GS_GAMEEND},
	{"GS_INTRO",GS_INTRO},
	{"GS_CUTSCENE",GS_CUTSCENE},
	{"GS_DEDICATEDSERVER",GS_DEDICATEDSERVER},
	{"GS_WAITINGPLAYERS",GS_WAITINGPLAYERS},

	// SRB2Kart
	// kartitems_t
#define FOREACH( name, n ) { TOSTR (KITEM_ ## name), KITEM_ ## name }
	KART_ITEM_ITERATOR, // Actual items (can be set for k_itemtype)
#undef  FOREACH
	{"NUMKARTITEMS",NUMKARTITEMS},
	{"KRITEM_DUALSNEAKER",KRITEM_DUALSNEAKER}, // Additional roulette IDs (not usable for much in Lua besides K_GetItemPatch)
	{"KRITEM_TRIPLESNEAKER",KRITEM_TRIPLESNEAKER},
	{"KRITEM_TRIPLEBANANA",KRITEM_TRIPLEBANANA},
	{"KRITEM_TENFOLDBANANA",KRITEM_TENFOLDBANANA},
	{"KRITEM_TRIPLEORBINAUT",KRITEM_TRIPLEORBINAUT},
	{"KRITEM_QUADORBINAUT",KRITEM_QUADORBINAUT},
	{"KRITEM_DUALJAWZ",KRITEM_DUALJAWZ},
	{"NUMKARTRESULTS",NUMKARTRESULTS},

	// kartshields_t
	{"KSHIELD_NONE",KSHIELD_NONE},
	{"KSHIELD_THUNDER",KSHIELD_THUNDER},
	{"KSHIELD_LIGHTNING",KSHIELD_THUNDER},
	{"KSHIELD_BUBBLE",KSHIELD_BUBBLE},
	{"KSHIELD_FLAME",KSHIELD_FLAME},
	{"NUMKARTSHIELDS",NUMKARTSHIELDS},

	// kartitems_t
	{"KITEM_LIGHTNINGSHIELD",KITEM_THUNDERSHIELD},

	// kartspinoutflags_t
	{"KSPIN_THRUST",KSPIN_THRUST},
	{"KSPIN_IFRAMES",KSPIN_IFRAMES},
	{"KSPIN_AIRTIMER",KSPIN_AIRTIMER},

	{"KSPIN_TYPEBIT",KSPIN_TYPEBIT},
	{"KSPIN_TYPEMASK",KSPIN_TYPEMASK},

	{"KSPIN_SPINOUT",KSPIN_SPINOUT},
	{"KSPIN_WIPEOUT",KSPIN_WIPEOUT},
	{"KSPIN_STUNG",KSPIN_STUNG},
	{"KSPIN_EXPLOSION",KSPIN_EXPLOSION},

	// spottype_t
	{"SPOT_NONE",SPOT_NONE},
	{"SPOT_WEAK",SPOT_WEAK},
	{"SPOT_BUMP",SPOT_BUMP},

	// precipeffect_t
	{"PRECIPFX_THUNDER",PRECIPFX_THUNDER},
	{"PRECIPFX_LIGHTNING",PRECIPFX_LIGHTNING},
	{"PRECIPFX_WATERPARTICLES",PRECIPFX_WATERPARTICLES},
	
	// followermode_t
	{"FOLLOWERMODE_FLOAT",FOLLOWERMODE_FLOAT},
	{"FOLLOWERMODE_GROUND",FOLLOWERMODE_GROUND},
	
	// screen.h constants
	{"BASEVIDWIDTH", BASEVIDWIDTH},
	{"BASEVIDHEIGHT", BASEVIDHEIGHT},

	{NULL,0}
};

// For this to work compile-time without being in this file,
// this function would need to check sizes at runtime, without sizeof
void DEH_TableCheck(void)
{
#if defined(_DEBUG) || defined(PARANOIA)
	const size_t dehstates = sizeof(STATE_LIST)/sizeof(const char*);
	const size_t dehmobjs  = sizeof(MOBJTYPE_LIST)/sizeof(const char*);
	const size_t dehcolors = sizeof(COLOR_ENUMS)/sizeof(const char*);

	if (dehstates != S_FIRSTFREESLOT)
		I_Error("You forgot to update the Dehacked states list, you dolt!\n(%d states defined, versus %s in the Dehacked list)\n", S_FIRSTFREESLOT, sizeu1(dehstates));

	if (dehmobjs != MT_FIRSTFREESLOT)
		I_Error("You forgot to update the Dehacked mobjtype list, you dolt!\n(%d mobj types defined, versus %s in the Dehacked list)\n", MT_FIRSTFREESLOT, sizeu1(dehmobjs));

	if (dehcolors != SKINCOLOR_FIRSTFREESLOT)
		I_Error("You forgot to update the Dehacked colors list, you dolt!\n(%d colors defined, versus %s in the Dehacked list)\n", SKINCOLOR_FIRSTFREESLOT, sizeu1(dehcolors));
#endif
}
