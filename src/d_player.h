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

#include <stdint.h>

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

typedef uint16_t kartvoicenum_t;

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

	KSPIN_CRASH = KSPIN_IFRAMES,
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

	uint8_t difficulty; // Bot's difficulty setting
	uint8_t diffincrease; // In GP: bot difficulty will increase this much next round
	dboolean rival; // If true, they're the GP rival

	fixed_t rubberband; // Bot rubberband value
	uint16_t controller; // Special bot controller linedef ID
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
	dboolean flip;
	sonicloopcamvars_t camera;
};

struct boostinfo_t {
  fixed_t stackspeedboost;
  fixed_t nonstackspeedboost;
  fixed_t accelboost;
  fixed_t handleboost;
  uint8_t grade;
};

// player_t struct for saltyhop related variables
struct saltyhop_t
{
	dboolean jump; // Player has the ability to do the hop
	dboolean ready; // Saltyhop is ready
	dboolean tapping; // Player is tapping the drift button

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
	uint16_t flags;

	uint16_t rocketsneakertimer;

	uint16_t invincibilitytimer;

	uint16_t smonitortimer;
	uint16_t smonitorexpiring;
	uint16_t maxsmonitortime;

	int16_t growshrinktimer;

	uint16_t flametimer;
};

typedef enum
{
	KARMAMODE_DEFAULT = 0,
	KARMAMODE_ITEM,
	KARMAMODE_EGGMAN,
} karmamode_e;

typedef enum
{
	CRASH_TURNTOSIDE = 1,
	CRASH_TUMBLING,
	CRASH_REORIENT,
	CRASH_FINISH
} ATTRPACK crashstate_e;

struct playermagnet_t
{
	fixed_t factor; // How much extra speed do we want to give the player?
	fixed_t base_speed; // Base speed; adjusts `magnet.factor` to attempt to match this speed
	fixed_t target_speed; // Desired speed; Adjusts `magnet.factor` to attempt to match this speed
	fixed_t target_gradient; // Gradient value for moving `factor` towards `target_speed`
	fixed_t friction_weights; // So you don't go sliding like mad when going too fast.
};

// Anything but the P-rank is divided up into multiples of 120

typedef enum
{
	RTIER_E = 0,				// Good job! That was... AWFUL
	RTIER_D,
	RTIER_C,
	RTIER_B,
	RTIER_A,
	RTIER_ONESTAR,
	RTIER_TWOSTAR,		
	RTIER_THREESTAR,
	NUMSTDRANKTIERS,
	RTIER_P = NUMSTDRANKTIERS,	// Go for a Perfect!
	NUMRANKTIERS
} ATTRPACK ranktier_e;

struct ranklog_t
{
	//
	// NON-GRADING
	//

	// "I JUST GOT HIT, ASSHOLE!"
	// Prevents certain penalties if the player was hit or bumped recently.
	int32_t ijustgothitasshole;

	uint16_t numlapgrades; // How many times did we grade a lap?
	dboolean lapwasgraded; // Was a lap graded?
	dboolean prankeligible; // (Grand Prix) This player knows what they're doing; they're contending for a P-Rank!

	//
	// BONUSES
	//

	uint16_t numhitconfirms; // How many times did you hit other players with items?
	int32_t driftboostcount[4]; // Number of driftboosts performed, separated by tier.
	int32_t leaderscore; // Points earned based on position. Per the name, the leader (1st place) gets the most!
	int32_t spbattack; // Time spent being tailed by a Self-Propelled Bomb. The closer it is, the faster this increases.
	fixed_t startboostgrading; // (Currently unused) How well did this player perform the startboost?

	//
	// VOLATILE (can increment or decrement)
	//

	int32_t posgrading; // "Pseudo-EXP"; grading points calculated based on a percentage value (race position) each lap.
	int32_t gaining; // Gaining/losing value

	//
	// PENALTIES
	//

	int32_t timeoffroadcurrent; // Current time (in tics) spent offroad. Affects timeoffroadtotal
	int32_t timeoffroadtotal; // Time in tics spent offroad.
	int32_t wallbonks; // Number of times the player hit a wall.
	uint16_t hitdamagingsector; // Hit a damaging sector or stray course hazard.
	uint16_t numrespawns; // If you manage to respawn over 65535 times, color me impressed.
};

struct rankscorefactor_t
{
	//
	// VOLATILE (can increment or decrement)
	//

	int32_t posgradescore; // Points earned/lost dependent on the player's position over time
	int32_t comebackscore;

	//
	// BONUSES
	//

	int32_t hitbonus; // Points earned for each hitconfirm
	int32_t driftbonus; // Points earned for driftboosts performed
	int32_t spbattackbonus; // Points earned for having a Self-Propelled Bomb trail you

	//
	// PENALTIES
	//

	int32_t offroadpenalty; // Points lost based on time spent offroad
	int32_t wallpenalty; // Points lost for hitting walls past a speed threshold
	int32_t hazardpenalty; // Points lost for running into course hazards or damaging sectors
	int32_t respawnpenalty; // Points lost for respawning past a grace threshold
	int32_t dnfpenalty; // Points lost when the player retires (NO CONTEST)
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
	
	int16_t lturn_max[MAXPREDICTTICS]; // What's the expected turn value for full-left for a number of frames back (to account for netgame latency)?
	int16_t rturn_max[MAXPREDICTTICS]; // Ditto but for full-right

	// Bit flags.
	// See pflags_t, above.
	pflags_t pflags;

	// playing animation.
	panim_t panim;

	// For screen flashing (bright).
	uint16_t flashcount;
	uint16_t flashpal;

	// Player skin colorshift, 0-15 for which color to draw player.
	uint16_t skincolor;

	int32_t skin;
	uint16_t voice_id;
	uint8_t availabilities[MAXAVAILABILITY];

	uint8_t kartspeed; // Kart speed stat between 1 and 9
	uint8_t kartweight; // Kart weight stat between 1 and 9

	uint8_t kartspeedrestat; // Player's wanted kart speed from restat
	uint8_t kartweightrestat; // Player's wanted kart weight from restat
	dboolean randomrestat; // Randomly set the restat values of the player every game

	// Blan: Adjusts speeds in an attempt to "magnetize" the player towards a target position.
	// This affects the BASE SPEED, and is not a traditional speed boost.
	playermagnet_t kartmagnet;

	int32_t followerskin;		// Kart: This player's follower "skin"
	dboolean followerready;	// Kart: Used to know when we can have a follower or not. (This is set on the first NameAndColor follower update)
	uint16_t followercolor;	// Kart: Used to store the follower colour the player wishes to use
	mobj_t *follower;		// Kart: This is the follower object we have. (If any)

	uint32_t charflags; // Extra abilities/settings for skins (combinable stuff)
	                 // See SF_ flags

	mobjtype_t followitem; // Object # to spawn for Smiles
	mobj_t *followmobj; // Smiles all around

	uint32_t score; // player score

	uint16_t nocontrol; //for linedef exec 427
	uint8_t carry;
	uint16_t dye;

	// Power ups. invinc and invis are tic counters.
	uint16_t powers[NUMPOWERS];

	// SRB2kart stuff
	int32_t kartstuff[NUMKARTSTUFF];
	int32_t karthud[NUMKARTHUD];
	dboolean jitterlegacy; // If true, makes Ring Racers characters jitter during drifts like
						  // SRB2Kart.


	// Basic gameplay things
	uint8_t position;			// Used for Kart positions, mostly for deterministic stuff
	uint8_t oldposition;		// Used for taunting when you pass someone
	uint8_t positiondelay;	// Used for position number, so it can grow when passing/being passed
	uint32_t distancetofinish;
	uint32_t distancetofinishprev;

	uint32_t distancefromcluster;
	uint32_t distancefrombestfit;

	// Progress through the map, based on waypoints/checkpoints
	// Checkpoints use the total count, waypoints use the pathfinding distance
	uint32_t progressthrumap;
	int32_t progressdelta;

	dboolean progressupdate;

	waypoint_t *currentwaypoint;
	waypoint_t *nextwaypoint;
	dboolean onbadwaypoint;	// This waypoint is bad, so we need to find a better one
	uint16_t bigwaypointgap;
	tic_t airtime; 			// Keep track of how long you've been in the air
	uint8_t startboost;		// (0 to 125) - Boost you get from start of race or respawn drop dash

	// Respawn
	uint8_t dropdash;			// Gain a burst of speed when you drop while respawning
	uint8_t respawn;			// Timer given when you die.
	
	uint16_t flashing;

	// Damage
	uint16_t spinouttimer;	// Spin-out from a banana peel or oil slick (was "pw_bananacam")
	uint8_t spinouttype;		// Determines the mode of spinout/wipeout, see kartspinoutflags_t

	uint16_t flipovertimer;   // Flipped over by a player.
	angle_t flipoverangle;  // Movement angle for a flipped-over player.

	crashstate_e crash_state; // Current state of the crash animation
	uint16_t crashtimer;		// Crashing and tumbling due to an Orbinaut or other wipeout item
	tic_t crash_elapsed;	// How long you've spent tumbling
	tic_t crash_reorient_start;
	fixed_t crash_rot;		// Current tumble rotation. Need to rotate 360 degrees before you reorient!
	angle_t crash_yaw;		// Current angle offset for the tumble
	fixed_t crash_zoffset;	// Not using sprzoff because it'd just lead to upkeep tedium
	fixed_t crash_zoffset_delta;	// How much we should modify the Z offset while crashing
	fixed_t crash_zoffset_lerp;		// When reorienting, play a fancy animation of our offset "fixing" itself
	int8_t crash_side;		// Side from which the crash victim was attacked from
	dboolean crash_bumpme;	// "Bump me!" This crashing player should bump for this interaction only

	uint8_t instashield;		// Instashield no-damage animation timer
	uint8_t wipeoutslow;		// Timer before you slowdown when getting wiped out
	uint8_t justbumped;		// Prevent players from endlessly bumping into each other
	dboolean noclip;			// Fix Grow breaking the "noclip" cheat. Also applies noclip as a bonus.

	ranklog_t ranklog;		// Data relevant to a player's rank.
	rankscorefactor_t rankscorefactor;	// The point values that factor into your final rank
	int32_t realrankscore;    // Un-clamped ranking score.
	int32_t rankscore;		// Final rank score.
	ranktier_e ranktier;         // The actual rank.

	driftmode_e driftmode;
	int8_t drift;			// (-5 to 5) - Drifting Left or Right, plus a bigger counter = sharper turn
	int16_t driftturnsnapshot;
	int8_t driftlock;		// Prevents you from using drift angle for x tics. Used for zipper fix.
	fixed_t driftcharge;	// Charge your drift so you can release a burst of speed
	uint8_t driftboost;		// (0 to 125) - Boost you get from drifting
	fixed_t airdriftspeed;	// target velocity to reduce player velocity to while air trusting
	tic_t driftsparkGrowTimer;	// CLIENT ONLY -
	tic_t driftelapsed;     	// CLIENT ONLY - Elapsed time spent during a drift.
	int8_t driftlevel; 			// CLIENT ONLY - handles controller rumble and air thrust power, cleared at the start of every tick.

	int32_t drift_wannaturn;  // Turn values the game uses to determine the direction you want to drift.

	int32_t nulldrift;        // When you drift without accelerating, this value ticks up/down depending on your drift's angle.
	tic_t nulldrifttime;

	fixed_t karttilt;    // Generalized sliptide-like kart tilting as an angle_t (Can be toggled off by clients)
	fixed_t karttiltmomentum; // angular velocity in rad/sec

	int8_t bumpyroadside; // last side lifted up by bumpy road

	// (Delay-drift) - Delay in tics before the final drift angle is determined.
	// Potentially influenced by player lag.
	tic_t driftdelay;

	tic_t recoverydashcharge;		// Time spent charging SSMT
	uint16_t recoverydash;		// SSMT boost timer (weak boost that ignores offroad)

	fixed_t spinoutrot;     // When a player spins out, this value increments modulus 360.

	int8_t aizdriftstrat;	// (-1 to 1) - Let go of your drift while boosting? Helper for the SICK STRATZ (sliptiding!) you have just unlocked
	int32_t aizdrifttilt;
	int32_t aizdriftturn;
	fixed_t slipdashcharge;	// charge up a dash with a sliptide!
	int8_t slipdashdir;		// no snaking allowed :^)
	int8_t airthrustdir;		// player->drift before airthrust was released

	fixed_t offroad;		// In Super Mario Kart, going offroad has lee-way of about 1 second before you start losing speed
	uint8_t pogospring;		// Pogo spring bounce effect
	uint8_t brakestop;		// Wait until you've made a complete stop for a few tics before letting brake go in reverse.
	uint8_t waterskip;		// Water skipping counter
	uint8_t tiregrease;		// Adjusts friction

	uint8_t dashpadcooldown;	// Separate the vanilla SA-style dash pads from using flashing
	
	fixed_t boostpower;		// Base boost value, for offroad
	fixed_t speedboost;		// Boost value smoothing for max speed
	fixed_t prevspeedboost;	// Max speed boost value from the last frame
	fixed_t accelboost;		// Boost value smoothing for acceleration
	fixed_t handleboost;	// Boost value smoothing for handling
	fixed_t forcedtopspeed; // Force this top speed while charging SSMT
	angle_t boostangle;		// angle set when not spun out OR boosted to determine what direction you should keep going at if you're spun out and boosted.
	boostinfo_t boostinfo;	// Stores values used for setting speed and accel boosts.
	uint8_t numsneakers;		// Number of stacked sneakers
	uint8_t numpanels;		// Number of stacked panels, used for panel stack toggle
	uint8_t numboosts;		// Number of Boosts stacking this frame

	fixed_t draftpower;		// (0 to FRACUNIT) - Drafting power, doubles your top speed & acceleration at max
	uint16_t draftleeway;		// Leniency timer before removing draft power
	int8_t lastdraft;		// (-1 to 15) - Last player being drafted

	uint8_t tripwireState; 		// see tripwirestate_t
	uint8_t tripwirePass; 		// see tripwirepass_t
	uint16_t tripwireLeniency;	// When reaching a state that lets you go thru tripwire, you get an extra second leniency after it ends to still go through it.
	uint8_t tripwireReboundDelay; // When failing Tripwire, brieftly lock out speed-based tripwire pass (anti-cheese)

	uint16_t itemroulette;			// Used for the roulette when deciding what item to give you (was "pw_kartitem")
	uint16_t previtemroulette;		// Used determining when to give rings after hitting item boxes
	uint16_t itemblink;				// Item flashing after roulette, serves as a mashing indicator. Also prevents item from being stolen.
	uint16_t itemblinkmode;			// Type of flashing: 0 = white (normal), 1 = red (mashing), 2 = rainbow (enhanced items)
	uint8_t roulettetype;				// Used for the roulette, for deciding type (0 = normal, 1 = better, 2 = eggman mark)

	// Item held stuff
	uint8_t itemtype;				// KITEM_ constant for item number
	uint8_t itemamount;			// Amount of said item
	uint8_t itemreservetype;		// KITEM_ constant for item number in the reserve slot
	uint8_t itemreserveamount;	// Amount of said item in the reserve slot
	uint8_t equippeditem;			// Item type that is currently in the equip slot, amount is handled by the player's hnext linked list of mobjs
	itemflags_t itemflags; 		// holds IF_flags (see itemstate_t)
	tic_t itemusecooldown;		// timer for which the player is locked out of using an item or mashing the item roulette (implementation of xItemLib setPlayerItemCooldown), doesn't block ring usage
	tic_t itemusecooldownmax;	// most recent highest value for itemusecooldown, mainly used for HUD rendering currently
	
	int8_t throwdir; 	// Held dir of controls; 1 = forward, 0 = none, -1 = backward (was "player->heldDir")

	uint8_t sadtimer;		// How long you've been sad
	uint8_t bricktimer;		// How long you've been brick'ed up

	// player's ring count
	int8_t rings;
	int8_t ringmin;		// minimum rings
	int8_t ringmax;		// maximum rings
	uint8_t pickuprings;	// Number of rings being picked up before added to the counter (prevents rings from being deleted forever over 20)
	uint8_t ringdelay;	// (0 to 3) - 3 tic delay between every ring usage
	uint8_t ringlock;		// Timer for automatic handeling of ringlock
	uint16_t ringboost;	// Ring boost timer
	uint16_t ringtime;	// The current Ring boost timer if it wasn't capped. Used for spam prevention measures.
	uint16_t superring;	// Spawn rings on top of you every tic!
	uint8_t superringleeway; // how long can you be on the ground before airdrag is reset.
	fixed_t superringdrag; // Slow down the superring boost when doing ground to air (ie ramp).
	uint8_t nextringaward;	// When should we spawn our next superring ring?
	uint8_t ringvolume;		// When consuming lots of rings, lower the sound a little.
	uint8_t ringtransparency; // When consuming lots of rings, fade out the rings again.

	int32_t airdroppredelay;			// Handles the delay before airdrop can be activated once going airborne.
	int32_t airdroptime;				// Tracks how long the player has been in airdrop.
	int32_t airdropbuffer;			// Mercy time for when heavy air drop will instantly activate once the pre-delay is over.
	int32_t airdropheavydash;		// Heavy Air Drop acceleration assist time
	p_airdropflags_t airdropflags;	// Airdrop-exclusive bitflags.

	mobj_t *shieldtracer;	// Blankart: Shield mobj

	tic_t attractioncharge; // charge of Attraction Shield
	tic_t attractionattack; // attack timer for Attraction Shield
	dboolean attractionattack_hipower; // Attraction Shield attack's power is maximum
	fixed_t attractionboost; // while using the Attraction Shield attack, controls the intensity of the boost recieved (stronger when facing towards the target)

	uint8_t bubblecool;	// Bubble Shield use cooldown
	uint8_t bubbleblowup;	// Bubble Shield usage blowup
	uint8_t bubblehealth;	// Bubble Shield health
	uint16_t bubbleboost; // Bubble shield boost timer

	// Specific variant of justbumped where skimming a player with an inflated
	// Bubble Shield increments this.
	uint32_t bubblegraze;

	tic_t bubblebuffer; // Prevents Bubble Shield from taking damage

	uint16_t flamedash;	 // Flame Shield dash power
	int32_t flametimer;	 // Flame Shield dash meter left ("fuel")
	uint8_t flamestore;	 // Flame Shield reserve boost ("temperature")
	uint8_t flameburnstop; // Time Flame Shield can't increase temperature for
	tic_t flameoverheat; // Time the Flame Shield spends overheating

	uint16_t hyudorotimer;	// Duration of the Hyudoro offroad effect itself
	int8_t stealingtimer;	// you are stealing
	int8_t stolentimer;		// you are being stolen from

	uint16_t sneakertimer;	// Duration of a Sneaker Boost (from Sneakers or level boosters)
	uint16_t realsneakertimer;// Duration of a Sneaker Boosts acutal time that doesn't get chained.(used for chainoffroad and tripwires)
	uint8_t floorboost;		// (0 to 3) - Prevents Sneaker sounds for a brief duration when triggered by a floor panel
	uint16_t chaintimer;		// Stores current chain timer length.

	uint8_t boostcharge; 		// Charge during race start

	fixed_t slopeboost; // Handle slopeboost speed
	fixed_t prevslopeboost; // Last frame slopeboost speed
	fixed_t slopeaccel; // Handle slopeboost accel
	
	int16_t growshrinktimer;			// > 0 = Big, < 0 = small
	int16_t growcancel;				// Duration of grow canceling
	int16_t squishedtimer;			// Duration of being squished

	int16_t altshrinktimeshit;		// (Alt. Shrink) How many times were you flipped over while shrunk?
	dboolean inarrowbullet;			// (Alt. Shrink) Are we in the Arrow Bullet state?
	mobj_t *arrowbullet;			// (Alt. Shrink) Arrow Bullet aura visual

	uint16_t rocketsneakertimer;		// Rocket Sneaker duration timer

	uint16_t invincibilitytimer;      // Invincibility timer

	uint16_t smonitortimer;			// S-Monitor timer
	uint16_t maxsmonitortime;    		// Initial time for the S-Monitor, used for the item bar.
	uint16_t smonitorexpiring; 		// Once you pass the cluster player, this starts the time limit for your S-Monitor invincibility.
	int16_t smonitorcancel;           // Duration of S-Monitor canceling.
	uint8_t smonitorwarning;          // "Timer warning" dboolean to signal the S-Monitor is running out.

	uint8_t eggmanexplode;	// Fake item recieved, explode in a few seconds
	int8_t eggmanblame;		// (-1 to 15) - Fake item recieved, who set this fake

	uint16_t bananadrag;		// After a second of holding a banana behind you, you start to slow down

	int8_t lastitemtarget;	// (-1 to 15) - Last person you target with jawz, for playing the target switch sfx
	uint8_t itemtargetdelay;	// (0 to 5) - Delay for Jawz target switching, to make it less twitchy
	// int8_t lastjawztarget;	// (-1 to 15) - Last person you target with jawz, for playing the target switch sfx
	// uint8_t jawztargetdelay;	// (0 to 5) - Delay for Jawz target switching, to make it less twitchy

	uint8_t dashRingPullTics; // Timer during which the player is pulled towards a dash ring
	uint8_t dashRingPushTics; // Timer during which the player displays effects and has no gravity after being thrust by a dash ring
	uint8_t dashRainbowPogo; // Determines when to disable pogospring extra gravity after using a rainbow dash ring

	int32_t interpoints; // BlanKart (port from SRB2Kart CEP): override for number of points earned in intermission
	uint32_t roundscore; // battle score this round
	uint8_t emeralds;
	uint8_t bumper;
	int16_t wanted;
	int16_t karmadelay;
	int16_t karmamode;
	int16_t karmapoints;

	int8_t glanceDir; // Direction the player is trying to look backwards in

	uint16_t breathTimer; // Holding your breath underwater

	uint8_t lastsafelap;
	uint8_t lastsafestarpost;

	tic_t splits[MAXRACESPLITS]; // Times we crossed checkpoint
	int32_t pace; // Last split delta, used for checking whether gaining or losing time

	uint8_t tripwireUnstuck;
	uint8_t bumpUnstuck;

	//

	int8_t lives;

	int8_t xtralife; // Ring Extra Life counter

	fixed_t speed; // Player's speed (distance formula of MOMX and MOMY values)
	fixed_t lastspeed;

	int32_t deadtimer; // End game if game over lasts too long
	int32_t deadbounces; // Bounce like a beach ball all over
	dboolean extendeddeath; // Bouncing more than one time
	tic_t exiting; // Exitlevel timer

	////////////////////////////
	// Conveyor Belt Movement //
	////////////////////////////
	fixed_t cmomx; // Conveyor momx
	fixed_t cmomy; // Conveyor momy
	fixed_t rmomx; // "Real" momx (momx - cmomx)
	fixed_t rmomy; // "Real" momy (momy - cmomy)

	int16_t totalring; // Total number of rings obtained for GP
	tic_t realtime; // integer replacement for leveltime
	tic_t laptime[LAP__MAX];
	uint8_t laps; // Number of laps (optional)
	uint8_t latestlap;
	
	// Starpost information
	fixed_t starpostx;
	fixed_t starposty;
	fixed_t starpostz;
	angle_t starpostangle; // Angle that the starpost is facing - you respawn facing this way
	int32_t starpostnum; // The number of the last starpost you hit
	tic_t starposttime; // NOIRE: The time of the last cheatcheck you hit
	dboolean starpostflip;
	
	int32_t prevcheck; // Distance from Previous Legacy Checkpoint
	int32_t nextcheck; // Distace to Next Legacy Checkpoint

	uint8_t ctfteam; // 0 == Spectator, 1 == Red, 2 == Blue

	uint8_t checkskip; // Skipping checkpoints? Oh no no no

	int16_t lastsidehit, lastlinehit;

	//uint8_t timeshit; // That's TIMES HIT, not TIME SHIT, you doofus! -- in memoriam

	int32_t onconveyor; // You are on a conveyor belt if nonzero

	mobj_t *awayviewmobj;
	int32_t awayviewtics;
	angle_t awayviewaiming; // Used for cut-away view

	dboolean spectator;
	tic_t spectatewait;		// reimplementable as uint8_t queue - How long have you been waiting as a spectator
	dboolean enteredGame;

	dboolean bot;
	botvars_t botvars;

	uint8_t splitscreenindex;

	tic_t jointime; // Timer when player joins game to change skin/color
	tic_t spectatorreentry;

	tic_t grieftime;
	uint8_t griefstrikes;

	uint8_t typing_timer; // Counts down while keystrokes are not emitted
	uint8_t typing_duration; // How long since resumed timer

	uint8_t kickstartaccel;

	fixed_t outrun; // Milky Way road effect
	uint8_t outruntime; // Used to bypass the speed cap for fall off

	fixed_t dashareapower; // boosts speed, acceleration, and handling
	
	sonicloopvars_t loop;

	tic_t bumpertime; // Currently being bounched by MT_NIGHTSBUMPER
	int32_t linkcount;
	tic_t linktimer;
	int32_t maxlink; // maximum link obtained

	saltyhop_t salty;

	dboolean prevonground;

	dboolean walltransfered;
	uint8_t walltransferboost;

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
