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
/// \file  d_player.h
/// \brief player data structures

#ifndef __D_PLAYER__
#define __D_PLAYER__

// The player data structure depends on a number
// of other structs: items (internal inventory),
// animation states (closely tied to the sprites
// used to represent them, unfortunately).
#include "p_pspr.h"

// In addition, the player is just a special
// case of the generic moving object/actor.
#include "p_mobj.h"

// Finally, for odd reasons, the player input
// is buffered within the player data struct,
// as commands per game tick.
#include "d_ticcmd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef UINT16 kartvoicenum_t;

// Extra abilities/settings for skins (combinable stuff)
typedef enum
{
	SF_MACHINE          = 1, // Beep boop. Are you a robot?
	SF_OLDDEATH			= 1<<1, // Kart V1 styled death animation
	SF_NOCOMPATROTATE	= 1<<2, // Kart V1 skins that should not be rotated (simple animal, etc)
	// free up to and including 1<<31
} skinflags_t;

// Splits are per-lap in Blankart, but let's keep this as-is.
// mine: pedantic; isn't MAX_LAPS = 99 here?
#define MAXRACESPLITS 32

//
// Player states.
//
typedef enum
{
	// Playing or camping.
	PST_LIVE,
	// Dead on the ground, view follows killer.
	PST_DEAD,
	// Ready to restart/respawn???
	PST_REBORN
} playerstate_t;

//
// Player internal flags
//
typedef enum
{
	PF_GODMODE			= 1<<0, // Immortal.

	// free: 1<<1 and 1<<2

	// Look back VFX has been spawned
	// TODO: Is there a better way to track this?
	PF_GAINAX			= 1<<3,
	PF_KICKSTARTACCEL	= 1<<4, // Accessibility feature: Is accelerate in kickstart mode?

	// free: 1<<5

	PF_NOSCALEGRAVITY	= 1<<6, // Don't scale gravity by player object scale (cleared when grounded)

	PF_WANTSTOJOIN		= 1<<7, // Spectator that wants to join

	PF_STASIS			= 1<<8, // Player is not allowed to move
	PF_FAILEDSTART		= 1<<9, // Player failed the startboost
	PF_ELIMINATED		= 1<<10, // Battle-style elimination, no extra penalty
	PF_NOCONTEST 		= 1<<11, // Did not finish (last place explosion)
	PF_LOSTLIFE			= 1<<12, // Do not lose life more than once

	PF_RINGLOCK			= 1<<13, // Prevent picking up rings while locked. Mostly for lua use since SPB no longer locks

	PF_DRIFTINPUT		= 1<<14, // Drifting!
	PF_GETSPARKS		= 1<<15, // Can get sparks
	PF_DRIFTEND			= 1<<16, // Drift has ended, used to adjust character angle after drift
	PF_BRAKEDRIFT		= 1<<17, // Helper for brake-drift spark spawning

	PF_AIRFAILSAFE		= 1<<18, // Whenever or not try the air boost
	PF_UPDATEMYRESPAWN	= 1<<19,

	PF_FLIPCAM	= 1<<20,

	PF_TRUSTWAYPOINTS	= 1<<21, // Do not activate lap cheat prevention next time finish line distance is updated
	PF_FREEZEWAYPOINTS	= 1<<22, // Skip the next waypoint/finish line distance update
	PF_HITFINISHLINE	= 1<<23, // Already hit the finish line this tic
	PF_WRONGWAY			= 1<<24, // Moving the wrong way with respect to waypoints?

	PF_SHRINKME			= 1<<25, // "Shrink me" cheat preference
	PF_SHRINKACTIVE		= 1<<26, // "Shrink me" cheat is in effect. (Can't be disabled mid-race)

	PF_JUSTFLIPPED      = 1<<27, // Just got flipped over, handle the bump interaction.
	
	PF_RECOVERYSPIN     = 1<<28, // Player was charging SSMT last tick

	PF_USEDOWN			= 1<<29, // For lua compat, don't use!
	PF_ATTACKDOWN		= 1<<30, // For lua compat, don't use!
	PF_SLIDING			= 1<<31, // For lua compat, don't use!
} pflags_t;

typedef enum
{
	PAF_AIRDROPINPUT	= 1<<0, // Air drop input held
	PAF_WANTSAIRDROP	= 1<<1, // Wants Air drop (input pressed and buffered)
	PAF_AIRDROP_LIGHT	= 1<<2, // Is in Light Air Drop
	PAF_AIRDROP_HEAVY	= 1<<3, // Is in Heavy Air Drop

	PAF_AIRDROP_MASK	= (PAF_AIRDROP_LIGHT|PAF_AIRDROP_HEAVY), // Is in any Air Drop state
} p_airdropflags_t;

typedef enum
{
	// Are animation frames playing?
	PA_ETC=0,
	PA_STILL,
	PA_SLOW,
	PA_FAST,
	PA_DRIFT,
	PA_HURT
} panim_t;

typedef enum
{
	CR_NONE = 0,
	// Specific level gimmicks.
	CR_SLIDING,
	CR_ZOOMTUBE,
	CR_DASHRING,
} carrytype_t; // carry

typedef enum
{
	IF_USERINGS			= 1,	// Have to be not holding the item button to change from using rings to using items (or vice versa) - prevents weirdness
	IF_ITEMOUT			= 1<<1,	// Are you holding an item out?
	IF_EGGMANOUT		= 1<<2,	// Eggman mark held, separate from IF_ITEMOUT so it doesn't stop you from getting items
	IF_HOLDREADY		= 1<<3,	// Hold button-style item is ready to activate
	IF_SEEKING			= 1<<4,	// Item runs seeking / homing behaviour and needs a reticule
	IF_PASSIVESEEKING	= 1<<5,	// Item is seeking but doesn't switch targets (needs IF_SEEKING to stay active)
} itemflags_t;

typedef enum
{
	KSHIELD_NONE = 0,
	KSHIELD_THUNDER = 1,
	KSHIELD_BUBBLE = 2,
	KSHIELD_FLAME = 3,
	KSHIELD_ATTRACTION = 4,
	NUMKARTSHIELDS
} kartshields_t;

typedef enum
{
	KSPIN_THRUST    = (1<<0),
	KSPIN_IFRAMES   = (1<<1),
	KSPIN_AIRTIMER  = (1<<2),

	KSPIN_TYPEBIT   = (1<<3),
	KSPIN_TYPEMASK  = ~( KSPIN_TYPEBIT - 1 ),

#define KSPIN_TYPE( type ) ( KSPIN_TYPEBIT << type )

	KSPIN_SPINOUT   = KSPIN_TYPE(0)|KSPIN_IFRAMES|KSPIN_THRUST,
	KSPIN_WIPEOUT   = KSPIN_TYPE(1)|KSPIN_IFRAMES|KSPIN_AIRTIMER,
	KSPIN_EXPLOSION = KSPIN_TYPE(2)|KSPIN_IFRAMES|KSPIN_AIRTIMER,

#undef KSPIN_TYPE
} kartspinoutflags_t;

typedef enum
{
	TRIPSTATE_NONE,
	TRIPSTATE_PASSED,
	TRIPSTATE_BLOCKED,
} tripwirestate_t;

typedef enum
{
	TRIPWIRE_NONE,
	TRIPWIRE_IGNORE,
	TRIPWIRE_BOOST,
	TRIPWIRE_BLASTER,
} tripwirepass_t;

typedef enum
{
	// Unsynced, HUD or clientsided effects

	// Rings
	khud_ringlock,		// Ring lock

	// Lap finish
	khud_lapanimation,	// Used to show the lap start wing logo animation
	khud_laphand,		// Lap hand gfx to use; 0 = none, 1 = :ok_hand:, 2 = :thumbs_up:, 3 = :thumps_down:

	// Big text
	khud_finish,		// Set when completing a round

	// Camera
	khud_boostcam,		// Camera push forward on boost
	khud_destboostcam,	// Ditto
	khud_timeovercam,	// Camera timer for leaving behind or not
	khud_heavydropcam,	// Camera timer for heavy air drop
	khud_postdropcam,	// Camera timer for landing after heavy air drop (both timers superposition for feel)
	khud_flamecamtime,  // Timer for flame shield usage camera effect

	// Sounds
	khud_enginesnd,		// Engine sound offset this player is using.
	khud_voices,		// Used to stop the player saying more voices than it should
	khud_tauntvoices,	// Used to specifically stop taunt voice spam
	khud_confirmvictim,		// Player ID that you dealt damage to
	khud_confirmvictimdelay,	// Delay before playing the sound
	khud_taunthorns,	// Used to specifically stop taunt horn spam

	// Battle
	khud_cardanimation,	// Used to determine the position of some full-screen Battle Mode graphics
	khud_yougotem, 		// "You Got Em" gfx when hitting someone as a karma player via a method that gets you back in the game instantly

	// Splits
	khud_splittime, // Delta between you and highest split
	khud_splitwin, // How to color/flag the split based on gaining/losing | ahead/behind
	khud_splittimer, // How long to show splits HUD
	khud_splitskin, // Skin index of the leading player
	khud_splitcolor, // Skincolor of the leading player
	khud_splitposition, // Who are we comparing to?

	// Drifting
	khud_afterimagetime, // tics left for displaying afterimage
	khud_afterimagevalue, // what value the drift ended at

	khud_itemtimer_xoff, // used to move itemtimers, useful for mods that want stuff in the middle.
	khud_itemtimer_yoff, // ^

	NUMKARTHUD
} karthudtype_t;

// Used for lua compat with older kart v1 scripts.
typedef enum
{
	// Basic gameplay things
	k_position,			// Used for Kart positions, mostly for deterministic stuff
	k_oldposition,		// Used for taunting when you pass someone
	k_positiondelay,	// Used for position number, so it can grow when passing/being passed
	k_prevcheck,		// Previous checkpoint distance; for p_user.c (was "pw_pcd")
	k_nextcheck,		// Next checkpoint distance; for p_user.c (was "pw_ncd")
	k_waypoint,			// Waypoints.
	k_starpostwp,		// Temporarily stores player waypoint for... some reason. Used when respawning and finishing.
	k_starpostflip,		// the last starpost we hit requires flipping?
	k_respawn,			// Timer for the DEZ laser respawn effect
	k_dropdash,			// Charge up for respawn Drop Dash

	k_throwdir, 		// Held dir of controls; 1 = forward, 0 = none, -1 = backward (was "player->heldDir")
	k_lapanimation,		// Used to show the lap start wing logo animation
	k_laphand,			// Lap hand gfx to use; 0 = none, 1 = :ok_hand:, 2 = :thumbs_up:, 3 = :thumps_down:
	k_cardanimation,	// Used to determine the position of some full-screen Battle Mode graphics
	k_voices,			// Used to stop the player saying more voices than it should
	k_tauntvoices,		// Used to specifically stop taunt voice spam
	k_instashield,		// Instashield no-damage animation timer
	k_enginesnd,		// Engine sound number you're on.

	k_floorboost,		// Prevents Sneaker sounds for a breif duration when triggered by a floor panel
	k_spinouttype,		// Determines whether to thrust forward or not while spinning out; 0 = move forwards, 1 = stay still

	k_drift,			// Drifting Left or Right, plus a bigger counter = sharper turn
	k_driftend,			// Drift has ended, used to adjust character angle after drift
	k_driftcharge,		// Charge your drift so you can release a burst of speed
	k_driftboost,		// Boost you get from drifting
	k_boostcharge,		// Charge-up for boosting at the start of the race
	k_startboost,		// Boost you get from start of race or respawn drop dash
	k_jmp,				// In Mario Kart, letting go of the jump button stops the drift
	k_offroad,			// In Super Mario Kart, going offroad has lee-way of about 1 second before you start losing speed
	k_pogospring,		// Pogo spring bounce effect
	k_brakestop,		// Wait until you've made a complete stop for a few tics before letting brake go in reverse.
	k_waterskip,		// Water skipping counter
	k_dashpadcooldown,	// Separate the vanilla SA-style dash pads from using pw_flashing
	k_boostpower,		// Base boost value, for offroad
	k_speedboost,		// Boost value smoothing for max speed
	k_accelboost,		// Boost value smoothing for acceleration
	k_boostangle,		// angle set when not spun out OR boosted to determine what direction you should keep going at if you're spun out and boosted.
	k_boostcam,			// Camera push forward on boost
	k_destboostcam,		// Ditto
	k_timeovercam,		// Camera timer for leaving behind or not
	k_aizdriftstrat,	// Let go of your drift while boosting? Helper for the SICK STRATZ you have just unlocked
	k_brakedrift,		// Helper for brake-drift spark spawning

	k_itemroulette,		// Used for the roulette when deciding what item to give you (was "pw_kartitem")
	k_roulettetype,		// Used for the roulette, for deciding type (currently only used for Battle, to give you better items from Karma items)

	// Item held stuff
	k_itemtype,		// KITEM_ constant for item number
	k_itemamount,	// Amount of said item
	k_itemheld,		// Are you holding an item?

	// Some items use timers for their duration or effects
	//k_thunderanim,			// Duration of Thunder Shield's use animation
	k_curshield,			// 0 = no shield, 1 = thunder shield
	k_hyudorotimer,			// Duration of the Hyudoro offroad effect itself
	k_stealingtimer,		// You are stealing an item, this is your timer
	k_stolentimer,			// You are being stolen from, this is your timer
	k_sneakertimer,			// Duration of the Sneaker Boost itself
	k_growshrinktimer,		// > 0 = Big, < 0 = small
	k_squishedtimer,		// Squished frame timer
	k_rocketsneakertimer,	// Rocket Sneaker duration timer
	k_invincibilitytimer,	// Invincibility timer
	k_eggmanheld,			// Eggman monitor held, separate from k_itemheld so it doesn't stop you from getting items
	k_eggmanexplode,		// Fake item recieved, explode in a few seconds
	k_eggmanblame,			// Fake item recieved, who set this fake
	k_lastjawztarget,		// Last person you target with jawz, for playing the target switch sfx
	k_bananadrag,			// After a second of holding a banana behind you, you start to slow down
	k_spinouttimer,			// Spin-out from a banana peel or oil slick (was "pw_bananacam")
	k_wipeoutslow,			// Timer before you slowdown when getting wiped out
	k_justbumped,			// Prevent players from endlessly bumping into each other
	k_comebacktimer,		// Battle mode, how long before you become a bomb after death
	k_sadtimer,				// How long you've been sad

	// Battle Mode vars
	k_bumper,			// Number of bumpers left
	k_comebackpoints,	// Number of times you've bombed or gave an item to someone; once it's 3 it gets set back to 0 and you're given a bumper
	k_comebackmode, 	// 0 = bomb, 1 = item
	k_wanted, 			// Timer for determining WANTED status, lowers when hitting people, prevents the game turning into Camp Lazlo
	k_yougotem, 		// "You Got Em" gfx when hitting someone as a karma player via a method that gets you back in the game instantly

	// v1.0.2+ vars
	k_itemblink,		// Item flashing after roulette, prevents Hyudoro stealing AND serves as a mashing indicator
	k_itemblinkmode,	// Type of flashing: 0 = white (normal), 1 = red (mashing), 2 = rainbow (enhanced items)
	k_getsparks,		// Disable drift sparks at low speed, JUST enough to give acceleration the actual headstart above speed
	k_jawztargetdelay,	// Delay for Jawz target switching, to make it less twitchy
	k_spectatewait,		// How long have you been waiting as a spectator
	k_growcancel,		// Hold the item button down to cancel Grow

	NUMKARTSTUFF
} kartstufftype_t;

// Player powers. (don't edit this comment)
// What are you gonna do about it :^) - Nep
typedef enum
{
	pw_invulnerability,
	pw_sneakers,
	pw_flashing,
	pw_shield,
	pw_tailsfly, // tails flying
	pw_underwater, // underwater timer
	pw_spacetime, // In space, no one can hear you spin!
	pw_extralife, // Extra Life timer

	pw_super, // Are you super?
	pw_gravityboots, // gravity boots

	// Weapon ammunition
	pw_infinityring,
	pw_automaticring,
	pw_bouncering,
	pw_scatterring,
	pw_grenadering,
	pw_explosionring,
	pw_railring,

	// Power Stones
	pw_emeralds, // stored like global 'emeralds' variable

	// NiGHTS powerups
	pw_nights_superloop,
	pw_nights_helper,
	pw_nights_linkfreeze,

	//for linedef exec 427
	pw_nocontrol,
	pw_ingoop, // In goop

	NUMPOWERS
} powertype_t;

// QUICKLY GET RING TOTAL, INCLUDING RINGS CURRENTLY IN THE PICKUP ANIMATION
#define RINGTOTAL(p) (p->rings + p->pickuprings)

#define TRIPWIRETIME (7)

//}

// for kickstartaccel
#define ACCEL_KICKSTART (TICRATE)

typedef enum
{
	BOT_STYLE_NORMAL,
	BOT_STYLE_STAY,
	//BOT_STYLE_CHASE,
	//BOT_STYLE_ESCAPE,
	BOT_STYLE__MAX
} botStyle_e;

// player_t struct for all bot variables
struct botvars_t
{
	botStyle_e style; // Training mode-style CPU mode

	UINT8 difficulty; // Bot's difficulty setting
	UINT8 diffincrease; // In GP: bot difficulty will increase this much next round
	boolean rival; // If true, they're the GP rival

	fixed_t rubberband; // Bot rubberband value
	UINT16 controller; // Special bot controller linedef ID
};

struct sonicloopcamvars_t
{
	tic_t enter_tic, exit_tic;
	tic_t zoom_in_speed, zoom_out_speed;
	fixed_t dist;
	angle_t pan;
	fixed_t pan_speed; // in degrees
	tic_t pan_accel, pan_back;
};

// player_t struct for loop state
struct sonicloopvars_t 
{
	fixed_t radius;
	fixed_t revolution, min_revolution, max_revolution;
	angle_t yaw;
	vector3_t origin;
	vector2_t origin_shift;
	vector2_t shift;
	boolean flip;
	sonicloopcamvars_t camera;
};

struct boostinfo_t {
  fixed_t stackspeedboost;
  fixed_t nonstackspeedboost;
  fixed_t accelboost;
  fixed_t handleboost;
  UINT8 grade;
};

// player_t struct for saltyhop related variables
struct saltyhop_t
{
	boolean jump; // Player has the ability to do the hop
	boolean ready; // Saltyhop is ready
	boolean tapping; // Player is tapping the drift button

	fixed_t momz, zoffset; // erm... the mechanism....
};

// this WILL conflict with a future commit I just need this to proto
// yama: Nah, I'mma do my own thing
typedef enum
{
	DRIFTMODE_CLASSIC = 0,  // SRB2Kart drifting; no changes
	DRIFTMODE_SNAPSHOT,     // AKA "Runahead" - Use a prediction system to reduce the chances of a wrongdrift
	DRIFTMODE_INSTANT,      // Instantly decide your drift on the tic it happens
	// DRIFTMODE_DELAY,        // Delay the time before a drift is decided
	NUMDRIFTMODES
} driftmode_e;

// (12 frames / 60fps) * 35tps
// Delay-drift: Time the game is given to predict a "wanted" drift
#define DRIFT_DELAY_TIME (7)

#define BUBBLEGRAZE_INCREMENT (2)

#if ((BUBBLEGRAZE_INCREMENT / 2) < 1)
#define BUBBLEGRAZE_SOFTINCREMENT (1)
#else
#define BUBBLEGRAZE_SOFTINCREMENT (BUBBLEGRAZE_INCREMENT / 2)
#endif

#define BUBBLEGRAZE_DECREMENT (1)
#define BUBBLEGRAZE_MAX (0x3FFFFFFF)

// enum for saved lap times
typedef enum
{
	LAP_CUR,
	LAP_BEST,
	LAP_LAST,
	LAP__MAX
} laptime_e;

typedef enum
{
	KEEPSTUFF_ROCKET			= 1,
	KEEPSTUFF_INVIN				= 1<<1,
	KEEPSTUFF_SMON				= 1<<2,
	KEEPSTUFF_GROWSHRINK		= 1<<3,
	KEEPSTUFF_FLAME				= 1<<4,
} keepstuffflags_t;

struct keepstuff_t
{
	UINT16 flags;

	UINT16 rocketsneakertimer;

	UINT16 invincibilitytimer;

	UINT16 smonitortimer;
	UINT16 smonitorexpiring;
	UINT16 maxsmonitortime;

	INT16 growshrinktimer;

	UINT16 flametimer;
};

typedef enum
{
	KARMAMODE_DEFAULT = 0,
	KARMAMODE_ITEM,
	KARMAMODE_EGGMAN,
} karmamode_e;

struct playermagnet_t
{
	fixed_t factor; // How much extra speed do we want to give the player?
	fixed_t base_speed; // Base speed; adjusts `magnet.factor` to attempt to match this speed
	fixed_t target_speed; // Desired speed; Adjusts `magnet.factor` to attempt to match this speed
	fixed_t target_gradient; // Gradient value for moving `factor` towards `target_speed`
	fixed_t friction_weights; // So you don't go sliding like mad when going too fast.
};

// ========================================================================
//                          PLAYER STRUCTURE
// ========================================================================
struct player_t
{
	mobj_t *mo;

	// Caveat: ticcmd_t is ATTRPACK! Be careful what precedes it.
	ticcmd_t cmd;
	ticcmd_t oldcmd; // from the previous tic

	playerstate_t playerstate;

	// Focal origin above r.z
	fixed_t viewz;
	// Base height above floor for viewz.
	fixed_t viewheight;
	// Bob/squat speed.
	fixed_t deltaviewheight;
	// bounded/scaled total momentum.
	fixed_t bob;
	fixed_t cameraOffset;

	angle_t viewrollangle;
	// camera tilt
	angle_t tilt;

	angle_t angleturn;

	// Mouse aiming, where the guy is looking at!
	// It is updated with cmd->aiming.
	angle_t aiming;

	// fun thing for player sprite
	angle_t drawangle;
	angle_t old_drawangle; // interp
	angle_t old_drawangle2;
	
	INT16 lturn_max[MAXPREDICTTICS]; // What's the expected turn value for full-left for a number of frames back (to account for netgame latency)?
	INT16 rturn_max[MAXPREDICTTICS]; // Ditto but for full-right

	// Bit flags.
	// See pflags_t, above.
	pflags_t pflags;

	// playing animation.
	panim_t panim;

	// For screen flashing (bright).
	UINT16 flashcount;
	UINT16 flashpal;

	// Player skin colorshift, 0-15 for which color to draw player.
	UINT16 skincolor;

	INT32 skin;
	UINT16 voice_id;
	UINT8 availabilities[MAXAVAILABILITY];

	UINT8 kartspeed; // Kart speed stat between 1 and 9
	UINT8 kartweight; // Kart weight stat between 1 and 9

	UINT8 kartspeedrestat; // Player's wanted kart speed from restat
	UINT8 kartweightrestat; // Player's wanted kart weight from restat
	boolean randomrestat; // Randomly set the restat values of the player every game

	// Blan: Adjusts speeds in an attempt to "magnetize" the player towards a target position.
	// This affects the BASE SPEED, and is not a traditional speed boost.
	playermagnet_t kartmagnet;

	INT32 followerskin;		// Kart: This player's follower "skin"
	boolean followerready;	// Kart: Used to know when we can have a follower or not. (This is set on the first NameAndColor follower update)
	UINT16 followercolor;	// Kart: Used to store the follower colour the player wishes to use
	mobj_t *follower;		// Kart: This is the follower object we have. (If any)

	UINT32 charflags; // Extra abilities/settings for skins (combinable stuff)
	                 // See SF_ flags

	mobjtype_t followitem; // Object # to spawn for Smiles
	mobj_t *followmobj; // Smiles all around

	UINT32 score; // player score

	UINT16 nocontrol; //for linedef exec 427
	UINT8 carry;
	UINT16 dye;

	// Power ups. invinc and invis are tic counters.
	UINT16 powers[NUMPOWERS];

	// SRB2kart stuff
	INT32 kartstuff[NUMKARTSTUFF];
	INT32 karthud[NUMKARTHUD];
	boolean jitterlegacy; // If true, makes Ring Racers characters jitter during drifts like
						  // SRB2Kart.


	// Basic gameplay things
	UINT8 position;			// Used for Kart positions, mostly for deterministic stuff
	UINT8 oldposition;		// Used for taunting when you pass someone
	UINT8 positiondelay;	// Used for position number, so it can grow when passing/being passed
	UINT32 distancetofinish;
	UINT32 distancetofinishprev;

	UINT32 distancefromcluster;
	UINT32 distancefrombestfit;

	waypoint_t *currentwaypoint;
	waypoint_t *nextwaypoint;
	boolean onbadwaypoint;	// This waypoint is bad, so we need to find a better one
	UINT16 bigwaypointgap;
	tic_t airtime; 			// Keep track of how long you've been in the air
	UINT8 startboost;		// (0 to 125) - Boost you get from start of race or respawn drop dash

	// Respawn
	UINT8 dropdash;			// Gain a burst of speed when you drop while respawning
	UINT8 respawn;			// Timer given when you die.
	
	UINT16 flashing;
	UINT16 spinouttimer;	// Spin-out from a banana peel or oil slick (was "pw_bananacam")
	UINT8 spinouttype;		// Determines the mode of spinout/wipeout, see kartspinoutflags_t

	UINT16 flipovertimer;   // Flipped over by a player using Invincibility.
	angle_t flipoverangle;  // Movement angle for a flipped-over player.

	UINT8 instashield;		// Instashield no-damage animation timer
	UINT8 wipeoutslow;		// Timer before you slowdown when getting wiped out
	UINT8 justbumped;		// Prevent players from endlessly bumping into each other
	boolean noclip;			// Fix Grow breaking the "noclip" cheat. Also applies noclip as a bonus.

	driftmode_e driftmode;
	SINT8 drift;			// (-5 to 5) - Drifting Left or Right, plus a bigger counter = sharper turn
	INT16 driftturnsnapshot;
	SINT8 driftlock;		// Prevents you from using drift angle for x tics. Used for zipper fix.
	fixed_t driftcharge;	// Charge your drift so you can release a burst of speed
	UINT8 driftboost;		// (0 to 125) - Boost you get from drifting
	fixed_t airdriftspeed;	// target velocity to reduce player velocity to while air trusting
	tic_t driftsparkGrowTimer;	// CLIENT ONLY -
	tic_t driftelapsed;     	// CLIENT ONLY - Elapsed time spent during a drift.
	SINT8 driftlevel; 			// CLIENT ONLY - handles controller rumble and air thrust power, cleared at the start of every tick.

	INT32 drift_wannaturn;  // Turn values the game uses to determine the direction you want to drift.

	INT32 nulldrift;        // When you drift without accelerating, this value ticks up/down depending on your drift's angle.
	tic_t nulldrifttime;

	fixed_t karttilt;    // Generalized sliptide-like kart tilting as an angle_t (Can be toggled off by clients)
	fixed_t karttiltmomentum; // angular velocity in rad/sec

	SINT8 bumpyroadside; // last side lifted up by bumpy road

	// (Delay-drift) - Delay in tics before the final drift angle is determined.
	// Potentially influenced by player lag.
	tic_t driftdelay;

	tic_t recoverydashcharge;		// Time spent charging SSMT
	UINT16 recoverydash;		// SSMT boost timer (weak boost that ignores offroad)

	fixed_t spinoutrot;     // When a player spins out, this value increments modulus 360.

	SINT8 aizdriftstrat;	// (-1 to 1) - Let go of your drift while boosting? Helper for the SICK STRATZ (sliptiding!) you have just unlocked
	INT32 aizdrifttilt;
	INT32 aizdriftturn;
	fixed_t slipdashcharge;	// charge up a dash with a sliptide!
	SINT8 slipdashdir;		// no snaking allowed :^)
	SINT8 airthrustdir;		// player->drift before airthrust was released

	fixed_t offroad;		// In Super Mario Kart, going offroad has lee-way of about 1 second before you start losing speed
	UINT8 pogospring;		// Pogo spring bounce effect
	UINT8 brakestop;		// Wait until you've made a complete stop for a few tics before letting brake go in reverse.
	UINT8 waterskip;		// Water skipping counter
	UINT8 tiregrease;		// Adjusts friction

	UINT8 dashpadcooldown;	// Separate the vanilla SA-style dash pads from using flashing
	
	fixed_t boostpower;		// Base boost value, for offroad
	fixed_t speedboost;		// Boost value smoothing for max speed
	fixed_t prevspeedboost;	// Max speed boost value from the last frame
	fixed_t accelboost;		// Boost value smoothing for acceleration
	fixed_t handleboost;	// Boost value smoothing for handling
	fixed_t forcedtopspeed; // Force this top speed while charging SSMT
	angle_t boostangle;		// angle set when not spun out OR boosted to determine what direction you should keep going at if you're spun out and boosted.
	boostinfo_t boostinfo;	// Stores values used for setting speed and accel boosts.
	UINT8 numsneakers;		// Number of stacked sneakers
	UINT8 numpanels;		// Number of stacked panels, used for panel stack toggle
	UINT8 numboosts;		// Number of Boosts stacking this frame

	fixed_t draftpower;		// (0 to FRACUNIT) - Drafting power, doubles your top speed & acceleration at max
	UINT16 draftleeway;		// Leniency timer before removing draft power
	SINT8 lastdraft;		// (-1 to 15) - Last player being drafted

	UINT8 tripwireState; 		// see tripwirestate_t
	UINT8 tripwirePass; 		// see tripwirepass_t
	UINT16 tripwireLeniency;	// When reaching a state that lets you go thru tripwire, you get an extra second leniency after it ends to still go through it.
	UINT8 tripwireReboundDelay; // When failing Tripwire, brieftly lock out speed-based tripwire pass (anti-cheese)

	UINT16 itemroulette;			// Used for the roulette when deciding what item to give you (was "pw_kartitem")
	UINT16 previtemroulette;		// Used determining when to give rings after hitting item boxes
	UINT16 itemblink;				// Item flashing after roulette, serves as a mashing indicator. Also prevents item from being stolen.
	UINT16 itemblinkmode;			// Type of flashing: 0 = white (normal), 1 = red (mashing), 2 = rainbow (enhanced items)
	UINT8 roulettetype;				// Used for the roulette, for deciding type (0 = normal, 1 = better, 2 = eggman mark)

	// Item held stuff
	UINT8 itemtype;				// KITEM_ constant for item number
	UINT8 itemamount;			// Amount of said item
	UINT8 itemreservetype;		// KITEM_ constant for item number in the reserve slot
	UINT8 itemreserveamount;	// Amount of said item in the reserve slot
	UINT8 equippeditem;			// Item type that is currently in the equip slot, amount is handled by the player's hnext linked list of mobjs
	itemflags_t itemflags; 		// holds IF_flags (see itemstate_t)
	tic_t itemusecooldown;		// timer for which the player is locked out of using an item or mashing the item roulette (implementation of xItemLib setPlayerItemCooldown), doesn't block ring usage
	tic_t itemusecooldownmax;	// most recent highest value for itemusecooldown, mainly used for HUD rendering currently
	
	SINT8 throwdir; 	// Held dir of controls; 1 = forward, 0 = none, -1 = backward (was "player->heldDir")

	UINT8 sadtimer;		// How long you've been sad
	UINT8 bricktimer;		// How long you've been brick'ed up

	// player's ring count
	SINT8 rings;
	SINT8 ringmin;		// minimum rings
	SINT8 ringmax;		// maximum rings
	UINT8 pickuprings;	// Number of rings being picked up before added to the counter (prevents rings from being deleted forever over 20)
	UINT8 ringdelay;	// (0 to 3) - 3 tic delay between every ring usage
	UINT8 ringlock;		// Timer for automatic handeling of ringlock
	UINT16 ringboost;	// Ring boost timer
	UINT16 ringtime;	// The current Ring boost timer if it wasn't capped. Used for spam prevention measures.
	UINT16 superring;	// Spawn rings on top of you every tic!
	UINT8 superringleeway; // how long can you be on the ground before airdrag is reset.
	fixed_t superringdrag; // Slow down the superring boost when doing ground to air (ie ramp).
	UINT8 nextringaward;	// When should we spawn our next superring ring?
	UINT8 ringvolume;		// When consuming lots of rings, lower the sound a little.
	UINT8 ringtransparency; // When consuming lots of rings, fade out the rings again.

	INT32 airdroppredelay;			// Handles the delay before airdrop can be activated once going airborne.
	INT32 airdroptime;				// Tracks how long the player has been in airdrop.
	INT32 airdropbuffer;			// Mercy time for when heavy air drop will instantly activate once the pre-delay is over.
	INT32 airdropheavydash;		// Heavy Air Drop acceleration assist time
	p_airdropflags_t airdropflags;	// Airdrop-exclusive bitflags.

	mobj_t *shieldtracer;	// Blankart: Shield mobj

	tic_t attractioncharge; // charge of Attraction Shield
	tic_t attractionattack; // attack timer for Attraction Shield
	boolean attractionattack_hipower; // Attraction Shield attack's power is maximum
	fixed_t attractionboost; // while using the Attraction Shield attack, controls the intensity of the boost recieved (stronger when facing towards the target)

	UINT8 bubblecool;	// Bubble Shield use cooldown
	UINT8 bubbleblowup;	// Bubble Shield usage blowup
	UINT8 bubblehealth;	// Bubble Shield health
	UINT16 bubbleboost; // Bubble shield boost timer

	// Specific variant of justbumped where skimming a player with an inflated
	// Bubble Shield increments this.
	UINT32 bubblegraze;

	tic_t bubblebuffer; // Prevents Bubble Shield from taking damage

	UINT16 flamedash;	 // Flame Shield dash power
	INT32 flametimer;	 // Flame Shield dash meter left ("fuel")
	UINT8 flamestore;	 // Flame Shield reserve boost ("temperature")
	UINT8 flameburnstop; // Time Flame Shield can't increase temperature for
	tic_t flameoverheat; // Time the Flame Shield spends overheating

	UINT16 hyudorotimer;	// Duration of the Hyudoro offroad effect itself
	SINT8 stealingtimer;	// you are stealing
	SINT8 stolentimer;		// you are being stolen from

	UINT16 sneakertimer;	// Duration of a Sneaker Boost (from Sneakers or level boosters)
	UINT16 realsneakertimer;// Duration of a Sneaker Boosts acutal time that doesn't get chained.(used for chainoffroad and tripwires)
	UINT8 floorboost;		// (0 to 3) - Prevents Sneaker sounds for a brief duration when triggered by a floor panel
	UINT16 chaintimer;		// Stores current chain timer length.

	UINT8 boostcharge; 		// Charge during race start

	fixed_t slopeboost; // Handle slopeboost speed
	fixed_t prevslopeboost; // Last frame slopeboost speed
	fixed_t slopeaccel; // Handle slopeboost accel
	
	INT16 growshrinktimer;			// > 0 = Big, < 0 = small
	INT16 growcancel;				// Duration of grow canceling
	INT16 squishedtimer;			// Duration of being squished

	INT16 altshrinktimeshit;		// (Alt. Shrink) How many times were you flipped over while shrunk?
	boolean inarrowbullet;			// (Alt. Shrink) Are we in the Arrow Bullet state?
	mobj_t *arrowbullet;			// (Alt. Shrink) Arrow Bullet aura visual

	UINT16 rocketsneakertimer;		// Rocket Sneaker duration timer

	UINT16 invincibilitytimer;      // Invincibility timer

	UINT16 smonitortimer;			// S-Monitor timer
	UINT16 maxsmonitortime;    		// Initial time for the S-Monitor, used for the item bar.
	UINT16 smonitorexpiring; 		// Once you pass the cluster player, this starts the time limit for your S-Monitor invincibility.
	INT16 smonitorcancel;           // Duration of S-Monitor canceling.
	UINT8 smonitorwarning;          // "Timer warning" boolean to signal the S-Monitor is running out.

	UINT8 eggmanexplode;	// Fake item recieved, explode in a few seconds
	SINT8 eggmanblame;		// (-1 to 15) - Fake item recieved, who set this fake

	UINT16 bananadrag;		// After a second of holding a banana behind you, you start to slow down

	SINT8 lastitemtarget;	// (-1 to 15) - Last person you target with jawz, for playing the target switch sfx
	UINT8 itemtargetdelay;	// (0 to 5) - Delay for Jawz target switching, to make it less twitchy
	// SINT8 lastjawztarget;	// (-1 to 15) - Last person you target with jawz, for playing the target switch sfx
	// UINT8 jawztargetdelay;	// (0 to 5) - Delay for Jawz target switching, to make it less twitchy

	UINT8 dashRingPullTics; // Timer during which the player is pulled towards a dash ring
	UINT8 dashRingPushTics; // Timer during which the player displays effects and has no gravity after being thrust by a dash ring
	UINT8 dashRainbowPogo; // Determines when to disable pogospring extra gravity after using a rainbow dash ring

	INT32 interpoints; // BlanKart (port from SRB2Kart CEP): override for number of points earned in intermission
	UINT32 roundscore; // battle score this round
	UINT8 emeralds;
	UINT8 bumper;
	INT16 wanted;
	INT16 karmadelay;
	INT16 karmamode;
	INT16 karmapoints;

	SINT8 glanceDir; // Direction the player is trying to look backwards in

	UINT16 breathTimer; // Holding your breath underwater

	UINT8 lastsafelap;
	UINT8 lastsafestarpost;

	tic_t splits[MAXRACESPLITS]; // Times we crossed checkpoint
	INT32 pace; // Last split delta, used for checking whether gaining or losing time

	UINT8 tripwireUnstuck;
	UINT8 bumpUnstuck;

	//

	SINT8 lives;

	SINT8 xtralife; // Ring Extra Life counter

	fixed_t speed; // Player's speed (distance formula of MOMX and MOMY values)
	fixed_t lastspeed;

	INT32 deadtimer; // End game if game over lasts too long
	INT32 deadbounces; // Bounce like a beach ball all over
	boolean extendeddeath; // Bouncing more than one time
	tic_t exiting; // Exitlevel timer

	////////////////////////////
	// Conveyor Belt Movement //
	////////////////////////////
	fixed_t cmomx; // Conveyor momx
	fixed_t cmomy; // Conveyor momy
	fixed_t rmomx; // "Real" momx (momx - cmomx)
	fixed_t rmomy; // "Real" momy (momy - cmomy)

	INT16 totalring; // Total number of rings obtained for GP
	tic_t realtime; // integer replacement for leveltime
	tic_t laptime[LAP__MAX];
	UINT8 laps; // Number of laps (optional)
	UINT8 latestlap;
	
	// Starpost information
	fixed_t starpostx;
	fixed_t starposty;
	fixed_t starpostz;
	angle_t starpostangle; // Angle that the starpost is facing - you respawn facing this way
	INT32 starpostnum; // The number of the last starpost you hit
	tic_t starposttime; // NOIRE: The time of the last cheatcheck you hit
	boolean starpostflip;
	
	INT32 prevcheck; // Distance from Previous Legacy Checkpoint
	INT32 nextcheck; // Distace to Next Legacy Checkpoint

	UINT8 ctfteam; // 0 == Spectator, 1 == Red, 2 == Blue

	UINT8 checkskip; // Skipping checkpoints? Oh no no no

	INT16 lastsidehit, lastlinehit;

	//UINT8 timeshit; // That's TIMES HIT, not TIME SHIT, you doofus! -- in memoriam

	INT32 onconveyor; // You are on a conveyor belt if nonzero

	mobj_t *awayviewmobj;
	INT32 awayviewtics;
	angle_t awayviewaiming; // Used for cut-away view

	boolean spectator;
	tic_t spectatewait;		// reimplementable as UINT8 queue - How long have you been waiting as a spectator
	boolean enteredGame;

	boolean bot;
	botvars_t botvars;

	UINT8 splitscreenindex;

	tic_t jointime; // Timer when player joins game to change skin/color
	tic_t spectatorreentry;

	tic_t grieftime;
	UINT8 griefstrikes;

	UINT8 typing_timer; // Counts down while keystrokes are not emitted
	UINT8 typing_duration; // How long since resumed timer

	UINT8 kickstartaccel;

	fixed_t outrun; // Milky Way road effect
	UINT8 outruntime; // Used to bypass the speed cap for fall off
	
	sonicloopvars_t loop;

	tic_t bumpertime; // Currently being bounched by MT_NIGHTSBUMPER
	INT32 linkcount;
	tic_t linktimer;
	INT32 maxlink; // maximum link obtained

	saltyhop_t salty;

	boolean prevonground;

	boolean walltransfered;
	UINT8 walltransferboost;

	uint8_t public_key[PUBKEYLENGTH];

	keepstuff_t keepstuff;

	char clan[MAXCLANTAG];

#ifdef HWRENDER
	fixed_t fovadd; // adjust FOV for hw rendering
#endif
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif
