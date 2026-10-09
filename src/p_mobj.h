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
/// \file  p_mobj.h
/// \brief Map Objects, MObj, definition and handling

#ifndef __P_MOBJ__
#define __P_MOBJ__

// Basics.
#include "tables.h"
#include "m_fixed.h"

// We need the thinker_t stuff.
#include "d_think.h"

// We need the WAD data structure for Map things, from the THINGS lump.
#include "doomdata.h"

// States are tied to finite states are tied to animation frames.
// Needs precompiled tables/data structures.
#include "info.h"

#ifdef __cplusplus
extern "C" {
#endif

// From r_defs.h; need to shove it here so that d_player.h picks it up, now that it has elevated
// importance
//
// Literally just defines sector damage types
typedef enum
{
	SD_UNDEFINED = -1,
	SD_NONE = 0,
	SD_GENERIC = 1,
	SD_LAVA = 2,
	SD_DEATHPIT = 3,
	SD_INSTAKILL = 4,
	SD_FLIPOVER = 5,
} sectordamage_t;

//
// NOTES: mobj_t
//
// mobj_ts are used to tell the refresh where to draw an image,
// tell the world simulation when objects are contacted,
// and tell the sound driver how to position a sound.
//
// The refresh uses the next and prev links to follow
// lists of things in sectors as they are being drawn.
// The sprite, frame, and angle elements determine which patch_t
// is used to draw the sprite if it is visible.
// The sprite and frame values are allmost allways set
// from state_t structures.
// The statescr.exe utility generates the states.h and states.c
// files that contain the sprite/frame numbers from the
// statescr.txt source file.
// The xyz origin point represents a point at the bottom middle
// of the sprite (between the feet of a biped).
// This is the default origin position for patch_ts grabbed
// with lumpy.exe.
// A walking creature will have its z equal to the floor
// it is standing on.
//
// The sound code uses the x,y, and subsector fields
// to do stereo positioning of any sound effited by the mobj_t.
//
// The play simulation uses the blocklinks, x,y,z, radius, height
// to determine when mobj_ts are touching each other,
// touching lines in the map, or hit by trace lines (gunshots,
// lines of sight, etc).
// The mobj_t->flags element has various bit flags
// used by the simulation.
//
// Every mobj_t is linked into a single sector
// based on its origin coordinates.
// The subsector_t is found with R_PointInSubsector(x,y),
// and the sector_t can be found with subsector->sector.
// The sector links are only used by the rendering code,
// the play simulation does not care about them at all.
//
// Any mobj_t that needs to be acted upon by something else
// in the play world (block movement, be shot, etc) will also
// need to be linked into the blockmap.
// If the thing has the MF_NOBLOCK flag set, it will not use
// the block links. It can still interact with other things,
// but only as the instigator (missiles will run into other
// things, but nothing can run into a missile).
// Each block in the grid is 128*128 units, and knows about
// every line_t that it contains a piece of, and every
// interactable mobj_t that has its origin contained.
//
// A valid mobj_t is a mobj_t that has the proper subsector_t
// filled in for its xy coordinates and is linked into the
// sector from which the subsector was made, or has the
// MF_NOSECTOR flag set (the subsector_t needs to be valid
// even if MF_NOSECTOR is set), and is linked into a blockmap
// block or has the MF_NOBLOCKMAP flag set.
// Links should only be modified by the P_[Un]SetThingPosition()
// functions.
// Do not change the MF_NO? flags while a thing is valid.
//
// Any questions?
//

//
// Misc. mobj flags
//
typedef enum
{
	// Call P_TouchSpecialThing when touched.
	MF_SPECIAL          = 1,
	// Blocks.
	MF_SOLID            = 1<<1,
	// Can be hit.
	MF_SHOOTABLE        = 1<<2,
	// Don't use the sector links (invisible but touchable).
	MF_NOSECTOR         = 1<<3,
	// Don't use the blocklinks (inert but displayable)
	MF_NOBLOCKMAP       = 1<<4,
	// Thin, paper-like collision bound (for visual equivalent, see FF_PAPERSPRITE)
	MF_PAPERCOLLISION   = 1<<5,
	// You can push this object. It can activate switches and things by pushing it on top.
	MF_PUSHABLE         = 1<<6,
	// Object is a boss.
	MF_BOSS             = 1<<7,
	// On level spawning (initial position), hang from ceiling instead of stand on floor.
	MF_SPAWNCEILING     = 1<<8,
	// Don't apply gravity (every tic); object will float, keeping current height
	//  or changing it actively.
	MF_NOGRAVITY        = 1<<9,
	// This object is an ambient sound.
	MF_AMBIENT          = 1<<10,
	// Slide this object when it hits a wall.
	MF_SLIDEME          = 1<<11,
	// Don't collide with walls or solid objects. Two MF_NOCLIP objects can't touch each other at all!
	MF_NOCLIP           = 1<<12,
	// Allow moves to any height, no gravity. For active floaters.
	MF_FLOAT            = 1<<13,
	// Monitor powerup icon. These rise a bit.
	MF_BOXICON          = 1<<14,
	// Don't hit same species, explode on block.
	// Player missiles as well as fireballs of various kinds.
	MF_MISSILE          = 1<<15,
	// Item is a spring.
	MF_SPRING           = 1<<16,
	// Item box
	MF_MONITOR          = 1<<17,
	// Don't run the thinker for this object.
	MF_NOTHINK          = 1<<18,
	// Fire object. Doesn't harm if you have fire shield.
	MF_FIRE             = 1<<19,
	// Don't adjust z if below or above floorz/ceilingz
	MF_NOCLIPHEIGHT     = 1<<20,
	// This mobj is an enemy!
	MF_ENEMY            = 1<<21,
	// Scenery (uses scenery thinker).
	MF_SCENERY          = 1<<22,
	// Painful (shit hurts).
	MF_PAIN             = 1<<23,
	// This mobj will stick to any surface or solid object it touches.
	MF_STICKY           = 1<<24,
	// Object uses terrain effects. (Overlays, footsteps, etc)
	MF_APPLYTERRAIN     = 1<<25,
	// for chase camera, don't be blocked by things (partial clipping)
	MF_NOCLIPTHING      = 1<<26,
	// Missile bounces like a grenade.
	MF_GRENADEBOUNCE    = 1<<27,
	// Run the action thinker on spawn.
	MF_RUNSPAWNFUNC     = 1<<28,
	// Don't remap in Encore mode. (Not a drawflag so that it's settable by mobjinfo.)
	MF_DONTENCOREMAP    = 1<<29,
	// Hitbox extends just as far below as above.
	MF_PICKUPFROMBELOW  = 1<<30,
	// Bounce off walls and things.
	MF_BOUNCE   		= 1<<31,
	// no more free slots, next up I suppose we can get rid of shit like MF_BOXICON?
} mobjflag_t;

typedef enum
{
	MF2_AXIS           = 1,     // It's a NiGHTS axis! (For faster checking)
	MF2_SHADOW         = 1<<1,  // DO NOT USE: for lua compatibility only
	MF2_DONTRESPAWN    = 1<<2,  // Don't respawn this object!
	MF2_DONTDRAW       = 1<<3,  // DO NOT USE: for lua compatibility only
	MF2_AUTOMATIC      = 1<<4,  // Thrown ring has automatic properties
	MF2_RAILRING       = 1<<5,  // Thrown ring has rail properties
	MF2_BOUNCERING     = 1<<6,  // Thrown ring has bounce properties
	MF2_EXPLOSION      = 1<<7,  // Thrown ring has explosive properties
	MF2_SCATTER        = 1<<8,  // Thrown ring has scatter properties
	MF2_BEYONDTHEGRAVE = 1<<9,  // Source of this missile has died and has since respawned.
	MF2_SLIDEPUSH      = 1<<10, // MF_PUSHABLE that pushes continuously.
	MF2_CLASSICPUSH    = 1<<11, // Drops straight down when object has negative momz.
	MF2_INVERTAIMABLE  = 1<<12, // Flips whether it's targetable by A_LookForEnemies (enemies no, decoys yes)
	MF2_INFLOAT        = 1<<13, // Floating to a height for a move, don't auto float to target's height.
	MF2_DEBRIS         = 1<<14, // Splash ring from explosion ring
	MF2_NIGHTSPULL     = 1<<15, // Attracted from a paraloop
	MF2_JUSTATTACKED   = 1<<16, // can be pushed by other moving mobjs
	MF2_FIRING         = 1<<17, // turret fire
	MF2_SUPERFIRE      = 1<<18, // Firing something with Super Sonic-stopping properties. Or, if mobj has MF_MISSILE, this is the actual fire from it.
	MF2_DONTSYNC   = 1<<19, // Prevents this mobj from triggering stuff or being sent. Used for sync safety on toggle effects.
	MF2_STRONGBOX      = 1<<20, // Flag used for "strong" random monitors.
	MF2_OBJECTFLIP     = 1<<21, // Flag for objects that always have flipped gravity.
	MF2_SKULLFLY       = 1<<22, // Special handling: skull in flight.
	MF2_FRET           = 1<<23, // Flashing from a previous hit
	MF2_BOSSNOTRAP     = 1<<24, // No Egg Trap after boss
	MF2_BOSSFLEE       = 1<<25, // Boss is fleeing!
	MF2_BOSSDEAD       = 1<<26, // Boss is dead! (Not necessarily fleeing, if a fleeing point doesn't exist.)
	MF2_AMBUSH         = 1<<27, // Alternate behaviour typically set by MTF_AMBUSH
	MF2_LINKDRAW       = 1<<28, // Draw vissprite of mobj immediately before/after tracer's vissprite (dependent on dispoffset and position)
	MF2_SHIELD         = 1<<29, // Thinker calls P_AddShield/P_ShieldLook (must be partnered with MF_SCENERY to use)
	MF2_SPLAT          = 1<<30, // Renders as a splat
	MF2_WATERRUN	   = 1<<31, // Mobj is able to run on Water!
} mobjflag2_t;

typedef enum
{
	DI_NODIR = -1,
	DI_EAST = 0,
	DI_NORTHEAST = 1,
	DI_NORTH = 2,
	DI_NORTHWEST = 3,
	DI_WEST = 4,
	DI_SOUTHWEST = 5,
	DI_SOUTH = 6,
	DI_SOUTHEAST = 7,
	NUMDIRS = 8,
} dirtype_t;

//
// Mobj extra flags
//
typedef enum
{
	// The mobj stands on solid floor (not on another mobj or in air)
	MFE_ONGROUND          = 1,
	// The mobj just hit the floor while falling, this is cleared on next frame
	// (instant damage in lava/slime sectors to prevent jump cheat..)
	MFE_JUSTHITFLOOR      = 1<<1,
	// The mobj stands in a sector with water, and touches the surface
	// this bit is set once and for all at the start of mobjthinker
	MFE_TOUCHWATER        = 1<<2,
	// The mobj stands in a sector with water, and his waist is BELOW the water surface
	// (for player, allows swimming up/down)
	MFE_UNDERWATER        = 1<<3,
	// used for ramp sectors
	MFE_JUSTSTEPPEDDOWN   = 1<<4,
	// Vertically flip sprite/allow upside-down physics
	MFE_VERTICALFLIP      = 1<<5,
	// Goo water
	MFE_GOOWATER          = 1<<6,
	// The mobj is touching a lava block
	MFE_TOUCHLAVA         = 1<<7,
	// Mobj was already pushed this tic
	MFE_PUSHED            = 1<<8,
	// Mobj was already sprung this tic
	MFE_SPRUNG            = 1<<9,
	// Platform movement
	MFE_APPLYPMOMZ        = 1<<10,
	// Compute and trigger on mobj angle relative to tracer
	// See Linedef Exec 457 (Track mobj angle to point)
	MFE_TRACERANGLE       = 1<<11,
	// SRB2Kart: The mobj just hit & bounced off a wall, this is cleared on next frame
	MFE_JUSTBOUNCEDWALL   = 1<<12,
	// SRB2Kart: free
	//      = 1<<13,
	// Slope physics sent you airborne
	MFE_SLOPELAUNCHED     = 1<<14,
	// free: to and including 1<<15
} mobjeflag_t;

//
// PRECIPITATION flags ?! ?! ?!
//
typedef enum {
	PCF_THUNK		= 1,		// Ran the thinker this tic.
	PCF_SPLASH		= 1<<1,		// Splashed on the ground, return to the ceiling after the animation's over
	PCF_INVISIBLE	= 1<<2,		// Don't draw.
	PCF_PIT			= 1<<3,		// Above pit.
	PCF_FLIP		= 1<<4,		// Spawning from floor, moving upwards.
} precipflag_t;

// Map Object definition.
struct mobj_t
{
	// List: thinker links.
	thinker_t thinker;

	// Info for drawing: position.
	fixed_t x, y, z;
	fixed_t old_x, old_y, old_z; // position interpolation
	fixed_t old_x2, old_y2, old_z2;
	
	mobjtype_t type;
	const mobjinfo_t *info; // &mobjinfo[mobj->type]

	// Interaction info, by BLOCKMAP.
	// Links in blocks (if needed).
	mobj_t *bnext;
	mobj_t **bprev; // killough 8/11/98: change to ptr-to-ptr

	// More drawing info: to determine current sprite.
	angle_t angle, pitch, roll; // orientation
	angle_t slopepitch, sloperoll;
	angle_t old_slopepitch, old_sloperoll;
	angle_t old_slopepitch2, old_sloperoll2;
	angle_t old_angle, old_pitch, old_roll; // orientation interpolation
	angle_t old_angle2, old_pitch2, old_roll2;
	angle_t rollangle;
	spritenum_t sprite; // used to find patch_t and flip value
	uint32_t frame; // frame number, plus bits see p_pspr.h
	uint8_t sprite2; // player sprites
	uint16_t anim_duration; // for FF_ANIMATE states

	uint32_t renderflags; // render flags
	uint32_t renderflags2; // More render flags
	fixed_t spritexscale, spriteyscale;
	fixed_t spritexoffset, spriteyoffset;
	fixed_t old_spritexscale, old_spriteyscale;
	fixed_t old_spritexscale2, old_spriteyscale2;
	fixed_t old_spritexoffset, old_spriteyoffset;
	fixed_t old_spritexoffset2, old_spriteyoffset2;

	// X and Y offsets that rotate alongside the sprite.
	int16_t rollingxoffset, rollingyoffset;

	fixed_t realxscale, realyscale; // funn-E streeetch
	fixed_t realxoffset, realyoffset;

	// squishssss
	fixed_t stretchslam; // "squish" effect when you land
	tic_t slamsoundtimer; // Funni slam sound when landing
	
	pslope_t *floorspriteslope; // The slope that the floorsprite is rotated by
	int16_t lightlevel; // Add to sector lightlevel, -255 - 255

	msecnode_t *touching_sectorlist; // a linked list of sectors where this object appears

	subsector_t *subsector; // Subsector the mobj resides in.

	// The closest interval over all contacted sectors (or things).
	fixed_t floorz; // Nearest floor below.
	fixed_t ceilingz; // Nearest ceiling above.
	ffloor_t *floorrover; // FOF referred by floorz
	ffloor_t *ceilingrover; // FOF referred by ceilingz
	fixed_t floordrop;
	fixed_t ceilingdrop;

	// For movement checking.
	fixed_t radius;
	fixed_t height;

	// Momentums, used to update position.
	fixed_t momx, momy, momz;
	fixed_t pmomz; // If you're on a moving floor, its "momz" would be here

	int32_t tics; // state tic counter
	state_t *state;
	uint32_t flags; // flags from mobjinfo tables
	uint32_t flags2; // MF2_ flags
	uint16_t eflags; // extra flags

	mtag_t tid;
	mobj_t *tid_next;
	mobj_t **tid_prev; // killough 8/11/98: change to ptr-to-ptr

	void *skin; // overrides 'sprite' when non-NULL (for player bodies to 'remember' the skin)
	// Player and mobj sprites in multiplayer modes are modified
	//  using an internal color lookup table for re-indexing.

	// Pointer to a set of voice values. If this doesn't exist,
	// the game attempts to use tke skin's voice, should this object have a skin assigned.
	kartvoice_t *voice;

	uint16_t color; // This replaces MF_TRANSLATION. Use 0 for default (no translation).

	// More list: links in sector (if needed)
	mobj_t *snext;
	mobj_t **sprev; // killough 8/11/98: change to ptr-to-ptr

	// Additional pointers for NiGHTS hoops
	mobj_t *hnext;
	mobj_t *hprev;

	// One last pointer for kart item lists
	mobj_t *itnext;

	int32_t health; // for player this is rings + 1 -- no it isn't, not any more!!

	// Movement direction, movement generation (zig-zagging).
	angle_t movedir; // dirtype_t 0-7; also used by Deton for up/down angle
	int32_t movecount; // when 0, select a new dir

	mobj_t *target; // Thing being chased/attacked (or NULL), and originator for missiles.

	int32_t reactiontime; // If not 0, don't attack yet.

	int32_t threshold; // If >0, the target will be chased no matter what.

	// Additional info record for player avatars only.
	// Only valid if type == MT_PLAYER
	player_t *player;

	int32_t lastlook; // Player number last looked for.

	mapthing_t *spawnpoint; // Used for CTF flags, objectplace, and a handful other applications.

	mobj_t *tracer; // Thing being chased/attacked for tracers.

	fixed_t friction;
	fixed_t movefactor;
	fixed_t gravity;
	fixed_t lastmomz;

	int32_t fuse; // Does something in P_MobjThinker on reaching 0.
	fixed_t watertop; // top of the water FOF the mobj is in
	fixed_t waterbottom; // bottom of the water FOF the mobj is in

	uint32_t mobjnum; // A unique number for this mobj. Used for restoring pointers on save games.

	fixed_t scale;
	fixed_t old_scale; // interpolation
	fixed_t old_scale2;
	fixed_t destscale;
	fixed_t scalespeed;

	// Extra values are for internal use for whatever you want
	int32_t extravalue1;
	int32_t extravalue2;
	int32_t extravalue3;

	// Custom values are not to be altered by us!
	// They are for SOCs to store things in.
	int32_t cusval;
	int32_t cvmem;

	// Time Mobj has been alive for, useful for stats or gameplay tracking purposes.
	uint16_t mobjlifetime;

	pslope_t *standingslope; // The slope that the object is standing on (shouldn't need synced in savegames, right?)

	dboolean resetinterp; // if true, some fields should not be interpolated (see R_InterpolateMobjState implementation)
	dboolean colorized; // Whether the mobj uses the rainbow colormap
	dboolean mirrored; // The object's rotations will be mirrored left to right, e.g., see frame AL from the right and AR from the left

	fixed_t shadowscale; // If this object casts a shadow, and the size relative to radius
	dboolean whiteshadow; // Use white shadow, set to true by default for fullbright objects
	uint8_t shadowcolor; // Palette index to use for rendering the shadow

	fixed_t sprxoff, spryoff, sprzoff; // Sprite offsets in real space, does NOT affect position or collision
	fixed_t bakexoff, bakeyoff, bakezoff; // BAKED sprite offsets. Simulates visuals in real space, and rotates along the object's sprite
	fixed_t bakexpiv, bakeypiv, bakezpiv; // Pivot points for baked offsets. These are *not* rotated with a sprite

	terrain_t *terrain; // Terrain definition of the floor this object last hit. NULL when in the air.
	mobj_t *terrainOverlay; // Overlay sprite object for terrain

	int32_t dispoffset;

	int32_t args[NUM_MAPTHING_ARGS];
	char *stringargs[NUM_MAPTHING_STRINGARGS];

	int16_t special;
	int32_t script_args[NUM_SCRIPT_ARGS];
	char *script_stringargs[NUM_SCRIPT_STRINGARGS];

	int32_t po_movecount; // Polyobject carrying (NOT savegame, NOT Lua)

	// WARNING: New fields must be added separately to savegame and Lua.
};

//
// For precipitation
//
// Sometimes this is casted to a mobj_t,
// so please keep the start of the
// structure the same.
//
struct precipmobj_t
{
	// List: thinker links.
	thinker_t thinker;

	// Info for drawing: position.
	fixed_t x, y, z;
	// --- Please make sure you keep the fields up to this
	// --- point in sync with degenmobj_t.

	fixed_t old_x, old_y, old_z; // position interpolation
	fixed_t old_x2, old_y2, old_z2;
	
	mobjtype_t type;
	const mobjinfo_t *info; // &mobjinfo[mobj->type]

	// Links in blocks (if needed).
	// The blockmap is only used by precip to render.
	precipmobj_t *bnext;
	precipmobj_t **bprev; // killough 8/11/98: change to ptr-to-ptr

	// More drawing info: to determine current sprite.
	angle_t angle, pitch, roll; // orientation
	angle_t slopepitch, sloperoll;
	angle_t old_slopepitch, old_sloperoll;
	angle_t old_slopepitch2, old_sloperoll2;
	angle_t old_angle, old_pitch, old_roll; // orientation interpolation
	angle_t old_angle2, old_pitch2, old_roll2;
	angle_t rollangle;
	spritenum_t sprite; // used to find patch_t and flip value
	uint32_t frame; // frame number, plus bits see p_pspr.h
	uint8_t sprite2; // player sprites
	uint16_t anim_duration; // for FF_ANIMATE states

	uint32_t renderflags; // render flags
	uint32_t renderflags2; // More render flags
	fixed_t spritexscale, spriteyscale;
	fixed_t spritexoffset, spriteyoffset;
	fixed_t old_spritexscale, old_spriteyscale;
	fixed_t old_spritexscale2, old_spriteyscale2;
	fixed_t old_spritexoffset, old_spriteyoffset;
	fixed_t old_spritexoffset2, old_spriteyoffset2;

	// X and Y offsets that rotate alongside the sprite.
	int16_t rollingxoffset, rollingyoffset;

	fixed_t realxscale, realyscale; // funn-E streeetch
	fixed_t realxoffset, realyoffset;

	// squishssss
	fixed_t stretchslam; // "squish" effect when you land
	tic_t slamsoundtimer; // Funni slam sound when landing

	pslope_t *floorspriteslope; // The slope that the floorsprite is rotated by
	int16_t lightlevel; // Add to sector lightlevel, -255 - 255

	mprecipsecnode_t *touching_sectorlist; // a linked list of sectors where this object appears

	subsector_t *subsector; // Subsector the mobj resides in.

	// The closest interval over all contacted sectors (or things).
	fixed_t floorz; // Nearest floor below.
	fixed_t ceilingz; // Nearest ceiling above.
	ffloor_t *floorrover; // FOF referred by floorz
	ffloor_t *ceilingrover; // FOF referred by ceilingz
	fixed_t floordrop;
	fixed_t ceilingdrop;

	// For movement checking.
	fixed_t radius; // Fixed at 2*FRACUNIT
	fixed_t height; // Fixed at 4*FRACUNIT

	// Momentums, used to update position.
	fixed_t momx, momy, momz;
	fixed_t precipflags; // fixed_t so it uses the same spot as "pmomz" even as we use precipflags_t for it

	int32_t tics; // state tic counter
	state_t *state;
	uint32_t flags; // flags from mobjinfo tables
	
	tic_t lastThink;
};

struct actioncache_t
{
	actioncache_t *next;
	actioncache_t *prev;
	mobj_t *mobj;
	int32_t statenum;
};

extern actioncache_t actioncachehead;

extern mobj_t *kitemcap;
extern mobj_t *waypointcap;
extern mobj_t *boss3cap;
extern mobj_t *misccap;

void P_InitCachedActions(void);
void P_RunCachedActions(void);
void P_AddCachedAction(mobj_t *mobj, int32_t statenum);

// kartitem stuff: Returns true if the specified 'type' is one of the kart item constants we want in the kitemcap list
dboolean P_IsKartFieldItem(int32_t type);
dboolean P_IsKartEquippedItem(int32_t type);
dboolean P_IsKartItem(int32_t type);
dboolean K_IsMissileOrKartItem(mobj_t *mo);
dboolean P_CanDeleteKartItem(int32_t type);
void P_AddKartItem(mobj_t *thing);	// needs to be called in k_kart.c
void P_RunKartItems(void);

void P_AddMiscCap(mobj_t *thing);
void P_RunMiscCap(void);

// check mobj against water content, before movement code
void P_MobjCheckWater(mobj_t *mobj);

// Player spawn points
void P_SpawnPlayer(int32_t playernum);
void P_MovePlayerToSpawn(int32_t playernum, mapthing_t *mthing);
void P_MovePlayerToStarpost(int32_t playernum);
void P_AfterPlayerSpawn(int32_t playernum, dboolean starpost);

fixed_t P_GetMobjSpawnHeight(const mobjtype_t mobjtype, const fixed_t x, const fixed_t y, const fixed_t dz, const fixed_t offset, const size_t layer, const dboolean flip, const fixed_t scale);
fixed_t P_GetMapThingSpawnHeight(const mobjtype_t mobjtype, const mapthing_t* mthing, const fixed_t x, const fixed_t y);

mobj_t *P_SpawnMapThing(mapthing_t *mthing);
void P_CopyMapThingBehaviorFieldsToMobj(const mapthing_t *mthing, mobj_t *mobj);
void P_CopyMapThingSpecialFieldsToMobj(const mapthing_t *mthing, mobj_t *mobj);
void P_SpawnHoop(mapthing_t *mthing);
void P_SpawnItemPattern(mapthing_t *mthing);
void P_SpawnItemLine(mapthing_t *mt1, mapthing_t *mt2);
void P_SpawnHoopOfSomething(fixed_t x, fixed_t y, fixed_t z, fixed_t radius, int32_t number, mobjtype_t type, angle_t rotangle);
void P_SpawnPrecipitation(void);
void P_SpawnParaloop(fixed_t x, fixed_t y, fixed_t z, fixed_t radius, int32_t number, mobjtype_t type, statenum_t nstate, angle_t rotangle, dboolean spawncenter);
void *P_CreateFloorSpriteSlope(mobj_t *mobj);
void P_RemoveFloorSpriteSlope(mobj_t *mobj);
dboolean P_BossTargetPlayer(mobj_t *actor, dboolean closest);
dboolean P_SupermanLook4Players(mobj_t *actor);
dboolean P_PrecipThinker(precipmobj_t *mobj);
void P_NullPrecipThinker(precipmobj_t *mobj);
void P_FreePrecipMobj(precipmobj_t *mobj);
#define P_SetScale(mobj, newscale) P_SetScale2(mobj, newscale, false)
void P_SetScale2(mobj_t *mobj, fixed_t newscale, dboolean compat);
void P_XYMovement(mobj_t *mo);
void P_RingXYMovement(mobj_t *mo);
void P_SceneryXYMovement(mobj_t *mo);
dboolean P_ZMovement(mobj_t *mo);
void P_RingZMovement(mobj_t *mo);
dboolean P_SceneryZMovement(mobj_t *mo);
void P_PlayerZMovement(mobj_t *mo);

kartvoice_t *P_GetMobjVoice(const mobj_t *mo);

extern int32_t modulothing;

#define MAXHUNTEMERALDS 64
extern mapthing_t *huntemeralds[MAXHUNTEMERALDS];
extern int32_t numhuntemeralds;
extern dboolean runemeraldmanager;
extern uint16_t emeraldspawndelay;
extern int32_t numstarposts;
extern int32_t numbosswaypoints;
extern dboolean ringsactive;
extern dboolean stackingactive;
extern dboolean chainingactive;
extern dboolean slipdashactive;
extern dboolean purpledriftactive;
extern dboolean slopeboostactive;
extern dboolean draftingactive;
extern uint8_t airdropactive;
extern uint8_t airthrustactive;
extern dboolean recoverydashactive;
extern dboolean keepstuffactive;
extern dboolean waterskipbricks;
extern dboolean itemlittering;
extern dboolean itempushing;
extern uint8_t   bumpsparkactive;
extern dboolean itemlistactive;
extern dboolean trailslow_active;
extern dboolean rankingactive;
extern dboolean ranking_penaltyactive, ranking_hitbonusactive, ranking_driftbonusactive;
extern dboolean cannotrank;
extern uint16_t bossdisabled;
extern dboolean stoppedclock;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
