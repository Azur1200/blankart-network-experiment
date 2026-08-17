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
/// \file  r_defs.h
/// \brief Refresh/rendering module, shared data struct definitions

#ifndef __R_DEFS__
#define __R_DEFS__

// Some more or less basic data types we depend on.
#include "m_fixed.h"

// We rely on the thinker data struct to handle sound origins in sectors.
#include "d_think.h"
// SECTORS do store MObjs anyway.
#include "p_mobj.h"

#include "screen.h" // MAXVIDWIDTH, MAXVIDHEIGHT

#ifdef HWRENDER
#include "m_aatree.h"
#endif

#include "taglist.h"

#include "k_mapuser.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// ClipWallSegment
// Clips the given range of columns
// and includes it in the new clip list.
//
struct cliprange_t
{
	int32_t first;
	int32_t last;
};

// Silhouette, needed for clipping segs (mainly) and sprites representing things.
#define SIL_NONE   0
#define SIL_BOTTOM 1
#define SIL_TOP    2
#define SIL_BOTH   3

// This could be wider for >8 bit display.
// Indeed, true color support is possible precalculating 24bpp lightmap/colormap LUT
// from darkening PLAYPAL to all black.
// Could even use more than 32 levels.
typedef uint8_t lighttable_t;

#define CMF_FADEFULLBRIGHTSPRITES  1
#define CMF_FOG (mapnamespace == MNS_SRB2KART ? 4 : 1)
#define CMF_NEWFADE 8

// ExtraColormap type. Use for extra_colormaps from now on.
struct extracolormap_t
{
	uint8_t fadestart, fadeend;
	uint8_t flags;

	// store rgba values in combined bitwise
	// also used in OpenGL instead lighttables
	int32_t rgba; // similar to maskcolor in sw mode
	int32_t fadergba; // The colour the colourmaps fade to

	lighttable_t *colormap;

#ifdef HWRENDER
	// The id of the hardware lighttable. Zero means it does not exist yet.
	uint32_t gl_lighttable_id;
#endif

#ifdef EXTRACOLORMAPLUMPS
	lumpnum_t lump; // for colormap lump matching, init to LUMPERROR
	char lumpname[9]; // for netsyncing
#endif

	extracolormap_t *next;
	extracolormap_t *prev;
};

//
// INTERNAL MAP TYPES used by play and refresh
//

/** Your plain vanilla vertex.
  */
struct vertex_t
{
	fixed_t x, y;
	dboolean floorzset, ceilingzset;
	fixed_t floorz, ceilingz;
};

/** Degenerate version of ::mobj_t, storing only a location.
  * Used for sound origins in sectors, hoop centers, and the like. Does not
  * handle sound from moving objects (doppler), because position is probably
  * just buffered, not updated.
  */
struct degenmobj_t
{
	thinker_t thinker; ///< Not used for anything.
	fixed_t x;         ///< X coordinate.
	fixed_t y;         ///< Y coordinate.
	fixed_t z;         ///< Z coordinate.
};

#include "p_polyobj.h"

// Store fake planes in a resizable array insted of just by
// heightsec. Allows for multiple fake planes.
/** Flags describing 3Dfloor behavior and appearance.
  */
typedef enum
{
	FOF_EXISTS            = 0x1,        ///< Always set, to check for validity.
	FOF_BLOCKPLAYER       = 0x2,        ///< Solid to player, but nothing else
	FOF_BLOCKOTHERS       = 0x4,        ///< Solid to everything but player
	FOF_SOLID             = 0x6,        ///< Clips things.
	FOF_RENDERSIDES       = 0x8,        ///< Renders the sides.
	FOF_RENDERPLANES      = 0x10,       ///< Renders the floor/ceiling.
	FOF_RENDERALL         = 0x18,       ///< Renders everything.
	FOF_SWIMMABLE         = 0x20,       ///< Is a water block.
	FOF_NOSHADE           = 0x40,       ///< Messes with the lighting?
	FOF_CUTSOLIDS         = 0x80,       ///< Cuts out hidden solid pixels.
	FOF_CUTEXTRA          = 0x100,      ///< Cuts out hidden translucent pixels.
	FOF_CUTLEVEL          = 0x180,      ///< Cuts out all hidden pixels.
	FOF_CUTSPRITES        = 0x200,      ///< Final step in making 3D water.
	FOF_BOTHPLANES        = 0x400,      ///< Render inside and outside planes.
	FOF_EXTRA             = 0x800,      ///< Gets cut by ::FOF_CUTEXTRA.
	FOF_TRANSLUCENT       = 0x1000,     ///< See through!
	FOF_FOG               = 0x2000,     ///< Fog "brush."
	FOF_INVERTPLANES      = 0x4000,     ///< Only render inside planes.
	FOF_ALLSIDES          = 0x8000,     ///< Render inside and outside sides.
	FOF_INVERTSIDES       = 0x10000,    ///< Only render inside sides.
	FOF_DOUBLESHADOW      = 0x20000,    ///< Make two lightlist entries to reset light?
	FOF_FLOATBOB          = 0x40000,    ///< Floats on water and bobs if you step on it.
	FOF_NORETURN          = 0x80000,    ///< Used with ::FOF_CRUMBLE. Will not return to its original position after falling.
	FOF_CRUMBLE           = 0x100000,   ///< Falls 2 seconds after being stepped on, and randomly brings all touching crumbling 3dfloors down with it, providing their master sectors share the same tag (allows crumble platforms above or below, to also exist).
	FOF_GOOWATER          = 0x200000,   ///< Used with ::FOF_SWIMMABLE. Makes thick bouncey goop.
	FOF_MARIO             = 0x400000,   ///< Acts like a question block when hit from underneath. Goodie spawned at top is determined by master sector.
	FOF_BUSTUP            = 0x800000,   ///< You can spin through/punch this block and it will crumble!
	FOF_QUICKSAND         = 0x1000000,  ///< Quicksand!
	FOF_PLATFORM          = 0x2000000,  ///< You can jump up through this to the top.
	FOF_REVERSEPLATFORM   = 0x4000000,  ///< A fall-through floor in normal gravity, a platform in reverse gravity.
	FOF_INTANGIBLEFLATS   = 0x6000000,  ///< Both flats are intangible, but the sides are still solid.
	FOF_RIPPLE            = 0x8000000,  ///< Ripple the flats
	FOF_COLORMAPONLY      = 0x10000000, ///< Only copy the colormap, not the lightlevel
	FOF_BOUNCY            = 0x20000000, ///< Bounces players
	FOF_SPLAT             = 0x40000000, ///< Use splat flat renderer (treat cyan pixels as invisible)
} ffloortype_e;

typedef enum
{
	FF_OLD_EXISTS            = 0x1,
	FF_OLD_BLOCKPLAYER       = 0x2,
	FF_OLD_BLOCKOTHERS       = 0x4,
	FF_OLD_SOLID             = 0x6,
	FF_OLD_RENDERSIDES       = 0x8,
	FF_OLD_RENDERPLANES      = 0x10,
	FF_OLD_RENDERALL         = 0x18,
	FF_OLD_SWIMMABLE         = 0x20,
	FF_OLD_NOSHADE           = 0x40,
	FF_OLD_CUTSOLIDS         = 0x80,
	FF_OLD_CUTEXTRA          = 0x100,
	FF_OLD_CUTLEVEL          = 0x180,
	FF_OLD_CUTSPRITES        = 0x200,
	FF_OLD_BOTHPLANES        = 0x400,
	FF_OLD_EXTRA             = 0x800,
	FF_OLD_TRANSLUCENT       = 0x1000,
	FF_OLD_FOG               = 0x2000,
	FF_OLD_INVERTPLANES      = 0x4000,
	FF_OLD_ALLSIDES          = 0x8000,
	FF_OLD_INVERTSIDES       = 0x10000,
	FF_OLD_DOUBLESHADOW      = 0x20000,
	FF_OLD_FLOATBOB          = 0x40000,
	FF_OLD_NORETURN          = 0x80000,
	FF_OLD_CRUMBLE           = 0x100000,
	FF_OLD_SHATTERBOTTOM     = 0x200000,
	FF_OLD_GOOWATER          = 0x200000,
	FF_OLD_MARIO             = 0x400000,
	FF_OLD_BUSTUP            = 0x800000,
	FF_OLD_QUICKSAND         = 0x1000000,
	FF_OLD_PLATFORM          = 0x2000000,
	FF_OLD_REVERSEPLATFORM   = 0x4000000,
	FF_OLD_INTANGIBLEFLATS   = 0x6000000,
	FF_OLD_SHATTER           = 0x8000000,
	FF_OLD_SPINBUST          = 0x10000000,
	FF_OLD_STRONGBUST        = 0x20000000,
	FF_OLD_RIPPLE            = 0x40000000,
	FF_OLD_COLORMAPONLY      = (int32_t)0x80000000,
} oldffloortype_e;

typedef enum
{
	FB_PUSHABLES   = 0x1, // Bustable by pushables
	FB_EXECUTOR    = 0x2, // Trigger linedef executor
	FB_ONLYBOTTOM  = 0x4, // Only bustable from below
} ffloorbustflags_e;

typedef enum
{
	BT_TOUCH,
	BT_SPINBUST,
	BT_REGULAR,
	BT_STRONG,
} busttype_e;

struct ffloor_t
{
	fixed_t *topheight;
	int32_t *toppic;
	int16_t *toplightlevel;
	fixed_t *topxoffs;
	fixed_t *topyoffs;
	angle_t *topangle;

	fixed_t *bottomheight;
	int32_t *bottompic;
	fixed_t *bottomxoffs;
	fixed_t *bottomyoffs;
	angle_t *bottomangle;

	// Pointers to pointers. Yup.
	pslope_t **t_slope;
	pslope_t **b_slope;

	size_t secnum;
	ffloortype_e fofflags;
	line_t *master;

	sector_t *target;

	ffloor_t *next;
	ffloor_t *prev;

	int32_t lastlight;
	int32_t alpha;
	uint8_t blend;
	tic_t norender; // for culling

	// Only relevant for FOF_BUSTUP
	ffloorbustflags_e bustflags;
	uint8_t busttype;
	int16_t busttag;

	// Only relevant for FOF_QUICKSAND
	fixed_t sinkspeed;
	fixed_t friction;

	// Only relevant for FOF_BOUNCY
	fixed_t bouncestrength;

	// these are saved for netgames, so do not let Lua touch these!
	ffloortype_e spawnflags; // flags the 3D floor spawned with
	int32_t spawnalpha; // alpha the 3D floor spawned with

	void *fadingdata; // fading FOF thinker
};


// This struct holds information for shadows casted by 3D floors.
// This information is contained inside the sector_t and is used as the base
// information for casted shadows.
struct lightlist_t
{
	fixed_t height;
	int16_t *lightlevel;
	extracolormap_t **extra_colormap; // pointer-to-a-pointer, so we can react to colormap changes
	int32_t flags;
	ffloor_t *caster;
	pslope_t *slope; // FOF_DOUBLESHADOW makes me have to store this pointer here. Bluh bluh.
};


// This struct is used for rendering walls with shadows casted on them...
struct r_lightlist_t
{
	fixed_t height;
	fixed_t heightstep;
	fixed_t botheight;
	fixed_t botheightstep;
	fixed_t startheight; // for repeating midtextures
	int16_t lightlevel;
	extracolormap_t *extra_colormap;
	lighttable_t *rcolormap;
	ffloortype_e flags;
	int32_t lightnum;
};

// Slopes
typedef enum {
	SL_NOPHYSICS = 1, /// This plane will have no physics applied besides the positioning.
	SL_DYNAMIC = 1<<1, /// This plane slope will be assigned a thinker to make it dynamic.
} slopeflags_t;

struct pslope_t
{
	uint16_t id; // The number of the slope, mostly used for netgame syncing purposes
	pslope_t *next; // Make a linked list of dynamic slopes, for easy reference later

	// The plane's definition.
	vector3_t o;		/// Plane origin.
	vector3_t normal;	/// Plane normal.

	vector2_t d;		/// Precomputed normalized projection of the normal over XY.
	fixed_t zdelta;		/// Precomputed Z unit increase per XY unit.

	// This values only check and must be updated if the slope itself is modified
	angle_t zangle;		/// Precomputed angle of the plane going up from the ground (not measured in degrees).
	angle_t xydirection;/// Precomputed angle of the normal's projection on the XY plane.

	uint8_t flags; // Slope options

	// SRB2Kart: For P_VeryTopOfFOF & P_VeryBottomOfFOF
	fixed_t lowz;
	fixed_t highz;

	// The ABCD constants used to define this slope
	fixed_t constants[4];

	// Light offsets (see seg_t)
	int8_t lightOffset;
#ifdef HWRENDER
	int16_t hwLightOffset;
#endif
};

typedef enum
{
	// flipspecial - planes with effect
	MSF_FLIPSPECIAL_FLOOR       =  1,
	MSF_FLIPSPECIAL_CEILING     =  1<<1,
	MSF_FLIPSPECIAL_BOTH        =  (MSF_FLIPSPECIAL_FLOOR|MSF_FLIPSPECIAL_CEILING),
	// triggerspecial - conditions under which plane touch causes effect
	MSF_TRIGGERSPECIAL_TOUCH    =  1<<2,
	MSF_TRIGGERSPECIAL_HEADBUMP =  1<<3,
	// triggerline - conditions for linedef executor triggering
	MSF_TRIGGERLINE_PLANE       =  1<<4, // require plane touch
	MSF_TRIGGERLINE_MOBJ        =  1<<5, // allow non-pushable mobjs to trigger
	// invertprecip - inverts presence of precipitation
	MSF_INVERTPRECIP            =  1<<6,
	MSF_GRAVITYFLIP             =  1<<7,
	MSF_HEATWAVE                =  1<<8,
	MSF_NOCLIPCAMERA            =  1<<9,
	// water ripple
	MSF_RIPPLE_FLOOR            =  1<<10,
	MSF_RIPPLE_CEILING          =  1<<11,
	// invert encore color remap status
	MSF_INVERTENCORE            =  1<<12,
	// turn off directional lighting
	MSF_FLATLIGHTING            =  1<<13,
	// force it on (even if it was disabled)
	MSF_DIRECTIONLIGHTING       =  1<<14,
	// Disable Audio EFX
	MSF_NOEFX                   =  1<<15,
} sectorflags_t;

// Per-sector bot controller override
struct botcontroller_t
{
	//uint8_t trick;
	uint32_t flags;
	angle_t forceAngle;
};

typedef enum
{
	SSF_NOSTEPUP = 1,
	SSF_DOUBLESTEPUP = 1<<1,
	SSF_NOSTEPDOWN = 1<<2,
	SSF_WINDCURRENT = 1<<3,
	SSF_CONVEYOR = 1<<4,
	SSF_SPEEDPAD = 1<<5,
	SSF_STARPOSTACTIVATOR = 1<<6,
	SSF_EXIT = 1<<7,
	SSF_DELETEITEMS = 1<<8,
	SSF_SNEAKERPANEL = 1<<9,
	SSF_WATERPANEL = 1<<10,
	SSF_REDPOGOSPRING = 1<<11,
	SSF_YELLOWPOGOSPRING = 1<<12,
	SSF_FAN = 1<<13,
	SSF_FINISHLINE = 1<<14,
	SSF_ZOOMTUBESTART = 1<<15,
	SSF_ZOOMTUBEEND = 1<<16,
	SSF_NOPHYSICSFLOOR = 1<<17,
	SSF_NOPHYSICSCEILING = 1<<18,
	SSF_GRAVITYOVERRIDE = 1<<19, // combine with MSF_GRAVITYFLIP
	SSF_WALLTRANSFER = 1<<20,
	SSF_BUMPYROAD = 1<<21,
	SSF_DASHAREA = 1<<22,
} sectorspecialflags_t;

typedef enum
{
	// Mask to get trigger type.
	SECSPAC_TRIGGERMASK			= 0x0000000F,

	// Special action is activated once.
	SECSPAC_ONCESPECIAL			= 0x00000000,

	// Special action is repeatable.
	SECSPAC_REPEATSPECIAL		= 0x00000001,

	// Special action is activated continously.
	SECSPAC_CONTINUOUSSPECIAL	= 0x00000002,

	// When a player enters this sector.
	SECSPAC_ENTER				= 0x00000010,

	// When a player touches the floor of this sector.
	SECSPAC_FLOOR				= 0x00000020,

	// When a player touches the ceiling of this sector.
	SECSPAC_CEILING				= 0x00000040,

	// When an enemy enters this sector.
	SECSPAC_ENTERMONSTER		= 0x00000080,

	// When an enemy touches the floor of this sector.
	SECSPAC_FLOORMONSTER		= 0x00000100,

	// When an enemy touches the ceiling of this sector.
	SECSPAC_CEILINGMONSTER		= 0x00000200,

	// When a projectile enters this sector.
	SECSPAC_ENTERMISSILE		= 0x00000400,

	// When a projectile touches the floor of this sector.
	SECSPAC_FLOORMISSILE		= 0x00000800,

	// When a projectile touches the ceiling of this sector.
	SECSPAC_CEILINGMISSILE		= 0x00001000,
} sectoractionflags_t;

// sectordamage_t moved to p_mobj.h

typedef enum
{
	TO_PLAYER = 0,
	TO_ALLPLAYERS = 1,
	TO_MOBJ = 2,
} triggerobject_t;

typedef enum
{
	CRUMBLE_NONE, // No crumble thinker
	CRUMBLE_WAIT, // Don't float on water because this is supposed to wait for a crumble
	CRUMBLE_ACTIVATED, // Crumble thinker activated, but hasn't fallen yet
	CRUMBLE_FALL, // Crumble thinker is falling
	CRUMBLE_RESTORE, // Crumble thinker is about to restore to original position
} crumblestate_t;

// enums for new SSF flags to prevent magic numbers!
typedef enum
{
	SBPT_SNEAKER,
	SBPT_WATER,
} specialboostpaneltype_t;

typedef enum
{
	SSPT_RED,
	SSPT_YELLOW,
} specialspringpaneltype_t;

//
// The SECTORS record, at runtime.
// Stores things/mobjs.
//
struct sector_t
{
	fixed_t floorheight;
	fixed_t ceilingheight;
	int32_t floorpic;
	int32_t ceilingpic;
	int16_t lightlevel;
	int16_t special;
	taglist_t tags;

	// origin for any sounds played by the sector
	// also considered the center for e.g. Mario blocks
	degenmobj_t soundorg;

	// if == validcount, already checked
	size_t validcount;

	// list of mobjs in sector
	mobj_t *thinglist;

	// thinker_ts for reversable actions
	void *floordata; // floor move thinker
	void *ceilingdata; // ceiling move thinker
	void *lightingdata; // lighting change thinker
	void *fadecolormapdata; // fade colormap thinker

	// floor and ceiling texture offsets
	fixed_t floor_xoffs, floor_yoffs;
	fixed_t ceiling_xoffs, ceiling_yoffs;

	// flat angle
	angle_t floorpic_angle;
	angle_t ceilingpic_angle;

	int32_t heightsec; // other sector, or -1 if no other sector
	int32_t camsec; // used for camera clipping

	// floor and ceiling lighting
	int16_t floorlightlevel, ceilinglightlevel;
	dboolean floorlightabsolute, ceilinglightabsolute; // absolute or relative to sector's light level?
	int32_t floorlightsec, ceilinglightsec; // take floor/ceiling light level from another sector

	int32_t crumblestate; // used for crumbling and bobbing

	// list of mobjs that are at least partially in the sector
	// thinglist is a subset of touching_thinglist
	msecnode_t *touching_thinglist;

	size_t linecount;
	line_t **lines; // [linecount] size

	// Improved fake floor hack
	ffloor_t *ffloors;
	size_t *attached;
	dboolean *attachedsolid;
	size_t numattached;
	size_t maxattached;
	lightlist_t *lightlist;
	int32_t numlights;
	dboolean moved;

	// per-sector colormaps!
	extracolormap_t *extra_colormap;
	dboolean colormap_protected;

	fixed_t gravity; // per-sector gravity factor
	fixed_t *gravityptr; // For binary format: Read gravity from floor height of master sector

	sectorflags_t flags;
	sectorspecialflags_t specialflags;
	uint8_t damagetype;

	fixed_t offroad; // Ring Racers

	fixed_t dashareapower;

	// Linedef executor triggering
	mtag_t triggertag; // tag to call upon triggering
	uint8_t triggerer; // who can trigger?

	fixed_t friction;

	// Sprite culling feature
	line_t *cullheight;

	// Current speed of ceiling/floor. For Knuckles to hold onto stuff.
	fixed_t floorspeed, ceilspeed;

	// list of precipitation mobjs in sector
	mprecipsecnode_t *touching_preciplist;

	// Eternity engine slope
	pslope_t *f_slope; // floor slope
	pslope_t *c_slope; // ceiling slope
	dboolean hasslope; // The sector, or one of its visible FOFs, contains a slope

	// for fade thinker
	int16_t spawn_lightlevel;

	// colormap structure
	extracolormap_t *spawn_extra_colormap;

	// Ring Racers bots
	botcontroller_t botController;

	// Action specials
	int16_t action;
	int32_t args[NUM_SCRIPT_ARGS];
	char *stringargs[NUM_SCRIPT_STRINGARGS];
	sectoractionflags_t activation;

	// UDMF user-defined custom properties.
	mapUserProperties_t user;
};

//
// Move clipping aid for linedefs.
//
typedef enum
{
	ST_HORIZONTAL,
	ST_VERTICAL,
	ST_POSITIVE,
	ST_NEGATIVE
} slopetype_t;

#define HORIZONSPECIAL (41)

struct line_t
{
	// Vertices, from v1 to v2.
	vertex_t *v1;
	vertex_t *v2;

	fixed_t dx, dy; // Precalculated v2 - v1 for side checking.
	angle_t angle; // Precalculated angle between dx and dy

	// Animation related.
	uint32_t flags;
	uint32_t activation;
	int16_t special;
	taglist_t tags;
	int32_t args[NUM_SCRIPT_ARGS];
	char *stringargs[NUM_SCRIPT_STRINGARGS];

	// Visual appearance: sidedefs.
	uint16_t sidenum[2]; // sidenum[1] will be NO_INDEX if one-sided
	fixed_t alpha; // translucency
	uint8_t blendmode; // blendmode
	int32_t executordelay;

	fixed_t bbox[4]; // bounding box for the extent of the linedef

	// To aid move clipping.
	slopetype_t slopetype;

	// Front and back sector.
	// Note: redundant? Can be retrieved from SideDefs.
	sector_t *frontsector;
	sector_t *backsector;

	size_t validcount; // if == validcount, already checked
	polyobj_t *polyobj; // Belongs to a polyobject?

	dboolean tripwire;
	dboolean blockplayers;
	dboolean blockintransfer;

	char *text; // a concatenation of all front and back texture names, for linedef specials that require a string.
	int16_t callcount; // no. of calls left before triggering, for the "X calls" linedef specials, defaults to 0

	// UDMF user-defined custom properties.
	mapUserProperties_t user;
};

struct side_t
{
	// add this to the calculated texture column
	fixed_t textureoffset;

	// add this to the calculated texture top
	fixed_t rowoffset;

	// per-texture offsets for UDMF
	fixed_t offsetx_top, offsetx_mid, offsetx_bot;
	fixed_t offsety_top, offsety_mid, offsety_bot;

	// Texture indices.
	// We do not maintain names here.
	int32_t toptexture, bottomtexture, midtexture;

	// Interpolator installed? (R_CreateInterpolator_SideScroll)
	dboolean acs_interpolated;

	// Linedef the sidedef belongs to
	line_t *line;

	// Sector the sidedef is facing.
	sector_t *sector;

	int16_t special; // the special of the linedef this side belongs to
	int16_t repeatcnt; // # of times to repeat midtexture

	char *text; // a concatenation of all top, bottom, and mid texture names, for linedef specials that require a string.

	extracolormap_t *colormap_data; // storage for colormaps; not applied to sectors.

	// UDMF user-defined custom properties.
	mapUserProperties_t user;
};

//
// A subsector.
// References a sector.
// Basically, this is a list of linesegs, indicating the visible walls that define
//  (all or some) sides of a convex BSP leaf.
//
struct subsector_t
{
	sector_t *sector;
	int16_t numlines;
	uint32_t firstline;
	polyobj_t *polyList; // haleyjd 02/19/06: list of polyobjects
	size_t validcount;
};

// Sector list node showing all sectors an object appears in.
//
// There are two threads that flow through these nodes. The first thread
// starts at touching_thinglist in a sector_t and flows through the m_thinglist_next
// links to find all mobjs that are entirely or partially in the sector.
// The second thread starts at touching_sectorlist in an mobj_t and flows
// through the m_sectorlist_next links to find all sectors a thing touches. This is
// useful when applying friction or push effects to sectors. These effects
// can be done as thinkers that act upon all objects touching their sectors.
// As an mobj moves through the world, these nodes are created and
// destroyed, with the links changed appropriately.
//
// For the links, NULL means top or end of list.

struct msecnode_t
{
	sector_t *m_sector; // a sector containing this object
	mobj_t *m_thing;  // this object
	msecnode_t *m_sectorlist_prev;  // prev msecnode_t for this thing
	msecnode_t *m_sectorlist_next;  // next msecnode_t for this thing
	msecnode_t *m_thinglist_prev;  // prev msecnode_t for this sector
	msecnode_t *m_thinglist_next;  // next msecnode_t for this sector
	dboolean visited; // used in search algorithms
};

struct mprecipsecnode_t
{
	sector_t *m_sector; // a sector containing this object
	precipmobj_t *m_thing;  // this object
	mprecipsecnode_t *m_sectorlist_prev;  // prev msecnode_t for this thing
	mprecipsecnode_t *m_sectorlist_next;  // next msecnode_t for this thing
	mprecipsecnode_t *m_thinglist_prev;  // prev msecnode_t for this sector
	mprecipsecnode_t *m_thinglist_next;  // next msecnode_t for this sector
	dboolean visited; // used in search algorithms
};

// for now, only used in hardware mode
// maybe later for software as well?
// that's why it's moved here
struct light_t
{
	uint16_t type;          // light,... (cfr #define in hwr_light.c)

	float light_xoffset;
	float light_yoffset;  // y offset to adjust corona's height

	uint32_t corona_color;   // color of the light for static lighting
	float corona_radius;  // radius of the coronas

	uint32_t dynamic_color;  // color of the light for dynamic lighting
	float dynamic_radius; // radius of the light ball
	float dynamic_sqrradius; // radius^2 of the light ball
};

struct lightmap_t
{
	float s[2], t[2];
	light_t *light;
	lightmap_t *next;
};

//
// The lineseg.
//
struct seg_t
{
	vertex_t *v1;
	vertex_t *v2;

	int32_t side;

	fixed_t offset;

	angle_t angle;

	side_t *sidedef;
	line_t *linedef;

	// Sector references.
	// Could be retrieved from linedef, too. backsector is NULL for one sided lines
	sector_t *frontsector;
	sector_t *backsector;

	fixed_t length;	// precalculated seg length
#ifdef HWRENDER
	// new pointers so that AdjustSegs doesn't mess with v1/v2
	void *pv1; // polyvertex_t
	void *pv2; // polyvertex_t
	float flength; // length of the seg, used by hardware renderer

	lightmap_t *lightmaps; // for static lightmap
#endif

	// Why slow things down by calculating lightlists for every thick side?
	size_t numlights;
	r_lightlist_t *rlights;
	polyobj_t *polyseg;
	dboolean dontrenderme;
	dboolean glseg;

	// Fake contrast calculated on level load
	int8_t lightOffset;
#ifdef HWRENDER
	int16_t hwLightOffset;
#endif
};

//
// BSP node.
//
struct node_t
{
	// Partition line.
	fixed_t x, y;
	fixed_t dx, dy;

	// Bounding box for each child.
	fixed_t bbox[2][4];

	// If NF_SUBSECTOR its a subsector.
	uint16_t children[2];
};

// posts are runs of non masked source pixels
struct post_t
{
	uint8_t topdelta; // -1 is the last post in a column
	uint8_t length;   // length data bytes follows
} ATTRPACK;

// column_t is a list of 0 or more post_t, (uint8_t)-1 terminated
typedef post_t column_t;

//
// OTHER TYPES
//

#ifndef MAXFFLOORS
#define MAXFFLOORS 40
#endif

//
// ?
//
struct drawseg_t
{
	seg_t *curline;
	int32_t x1;
	int32_t x2;

	fixed_t scale1;
	fixed_t scale2;
	fixed_t scalestep;

	int32_t silhouette; // 0 = none, 1 = bottom, 2 = top, 3 = both

	fixed_t bsilheight; // do not clip sprites above this
	fixed_t tsilheight; // do not clip sprites below this

	// Pointers to lists for sprite clipping, all three adjusted so [x1] is first value.
	int16_t *sprtopclip;
	int16_t *sprbottomclip;
	fixed_t *maskedtexturecol;

	visplane_t *ffloorplanes[MAXFFLOORS];
	int32_t numffloorplanes;
	ffloor_t *thicksides[MAXFFLOORS];
	fixed_t *thicksidecol;
	int32_t numthicksides;
	fixed_t frontscale[MAXVIDWIDTH];

	uint8_t portalpass; // if > 0 and <= portalrender, do not affect sprite clipping

	fixed_t maskedtextureheight[MAXVIDWIDTH]; // For handling sloped midtextures

	vertex_t leftpos, rightpos; // Used for rendering FOF walls with slopes
};

typedef enum
{
	PALETTE         = 0,  // 1 byte is the index in the doom palette (as usual)
	INTENSITY       = 1,  // 1 byte intensity
	INTENSITY_ALPHA = 2,  // 2 byte: alpha then intensity
	RGB24           = 3,  // 24 bit rgb
	RGBA32          = 4,  // 32 bit rgba
} pic_mode_t;

#ifdef ROTSPRITE
struct pivotvector_t
{
	int16_t x, y;
};

struct rotsprite_t
{
	int32_t angles;
	void **patches;
};
#endif

// Patches.
// A patch holds one or more columns.
// Patches are used for sprites and all masked pictures, and we compose
// textures from the TEXTURES list of patches.
//
typedef enum
{
	PATCHALIGN_AUTOCENTER = 1<<0,
	PATCHALIGN_USEPIVOTS  = 1<<1,
	// free: up to 1<<7
} patchalignflags_t;

struct patch_t
{
	int16_t width, height;

	// Offsets and rotation pivots.
	int16_t leftoffset, topoffset;
	pivotvector_t pivot;

	uint8_t alignflags; // Alignment-related flags.

	int32_t *columnofs; // Column offsets. This is relative to patch->columns
	uint8_t *columns; // Software column data

	void *hardware; // OpenGL patch, allocated whenever necessary
	void *flats[4]; // The patch as flats

#ifdef ROTSPRITE
	rotsprite_t *rotated; // Rotated patches
#endif
};

extern patch_t *missingpat;
extern patch_t *blanklvl;
extern patch_t *randomlvl;
extern patch_t *nolvl;

struct softwarepatch_t
{
	int16_t width;          // bounding box size
	int16_t height;
	int16_t leftoffset;     // pixels to the left of origin
	int16_t topoffset;      // pixels below the origin
	int32_t columnofs[];     // only [width] used
	// the [0] is &columnofs[width]
} ATTRPACK;

// a pic is an unmasked block of pixels, stored in horizontal way
struct pic_t
{
	int16_t width;
	uint8_t zero;       // set to 0 allow autodetection of pic_t
	                 // mode instead of patch or raw
	uint8_t mode;       // see pic_mode_t above
	int16_t height;
	int16_t reserved1; // set to 0
	uint8_t data[];
} ATTRPACK;

// Possible alpha types for a patch.
typedef enum {AST_COPY, AST_TRANSLUCENT, AST_ADD, AST_SUBTRACT, AST_REVERSESUBTRACT, AST_MODULATE, AST_OVERLAY, AST_FOG} patchalphastyle_t;

typedef enum
{
	RF_HORIZONTALFLIP   = 0x00000001,   // Flip sprite horizontally
	RF_VERTICALFLIP     = 0x00000002,   // Flip sprite vertically
	RF_ABSOLUTEOFFSETS  = 0x00000004,   // Sprite uses the object's offsets absolutely, instead of relatively
	RF_FLIPOFFSETS      = 0x00000008,   // Relative object offsets are flipped with the sprite

	RF_SPLATMASK        = 0x000000F0,   // --Floor sprite flags
	RF_SLOPESPLAT       = 0x00000010,   // Rotate floor sprites by a slope
	RF_OBJECTSLOPESPLAT = 0x00000020,   // Rotate floor sprites by the object's standing slope
	RF_NOSPLATBILLBOARD = 0x00000040,   // Don't billboard floor sprites (faces forward from the view angle)
	RF_NOSPLATROLLANGLE = 0x00000080,   // Don't rotate floor sprites by the object's rollangle (uses rotated patches instead)

	RF_BRIGHTMASK       = 0x00000300,   // --Bright modes
	RF_FULLBRIGHT       = 0x00000100,   // Sprite is drawn at full brightness
	RF_FULLDARK         = 0x00000200,   // Sprite is drawn completely dark
	RF_SEMIBRIGHT       = (RF_FULLBRIGHT | RF_FULLDARK), // between sector bright and full bright

	RF_NOCOLORMAPS      = 0x00000400,   // Sprite is not drawn with colormaps

	RF_ALWAYSONTOP      = 0x00000800,   // Sprite is drawn on top of level geometry

	RF_SPRITETYPEMASK   = 0x00003000,   // --Different sprite types
	RF_PAPERSPRITE      = 0x00001000,   // Paper sprite
	RF_FLOORSPRITE      = 0x00002000,   // Floor sprite

	RF_SHADOWDRAW       = 0x00004000,  // Stretches and skews the sprite like a shadow.
	RF_SHADOWEFFECTS    = 0x00008000,  // Scales and becomes transparent like a shadow.
	RF_DROPSHADOW       = (RF_SHADOWDRAW | RF_SHADOWEFFECTS | RF_FULLDARK),

	RF_ABSOLUTELIGHTLEVEL = 0x00010000, //  mobj_t.lightlevel is absolute instead of relative
	RF_HIDEINSKYBOX       = 0x00020000, //  do not render in skybox
	RF_AFFINEPRESCALE     = 0x00040000, // Makes affines scale before rotating, instead of rotating before scaling
	RF_NOAFFINE           = 0x00080000,  // Disables affine drawing for this sprite

	RF_HIDESHIFT        = (20),
	RF_DONTDRAW         = 0x0F << RF_HIDESHIFT,   // --Don't generate a vissprite
	RF_DONTDRAWP1       = 0x01 << RF_HIDESHIFT,   // No P1
	RF_DONTDRAWP2       = 0x02 << RF_HIDESHIFT,   // No P2
	RF_DONTDRAWP3       = 0x04 << RF_HIDESHIFT,   // No P3
	RF_DONTDRAWP4       = 0x08 << RF_HIDESHIFT,   // No P4

	RF_BLENDMASK       	= 0x07000000,   // --Blending override - see patchalphastyle_t
	RF_BLENDSHIFT		= (6*4),
	// minus 1 as effects don't distinguish between AST_COPY and AST_TRANSLUCENT
	RF_ADD				= ((AST_ADD-1)<<RF_BLENDSHIFT),
	RF_SUBTRACT			= ((AST_SUBTRACT-1)<<RF_BLENDSHIFT),
	RF_REVERSESUBTRACT	= ((AST_REVERSESUBTRACT-1)<<RF_BLENDSHIFT),
	RF_MODULATE			= ((AST_MODULATE-1)<<RF_BLENDSHIFT),
	RF_OVERLAY			= ((AST_OVERLAY-1)<<RF_BLENDSHIFT),

	// free: 0x08000000

	RF_TRANSMASK       	= (int32_t)0xF0000000,   // --Transparency override
	RF_TRANSSHIFT		= (7*4),
	RF_TRANS10       	= (1<<RF_TRANSSHIFT),   // 10%
	RF_TRANS20       	= (2<<RF_TRANSSHIFT),   // 20%
	RF_TRANS30       	= (3<<RF_TRANSSHIFT),   // 30%
	RF_TRANS40       	= (4<<RF_TRANSSHIFT),   // 40%
	RF_TRANS50       	= (5<<RF_TRANSSHIFT),   // 50%
	RF_TRANS60       	= (6<<RF_TRANSSHIFT),   // 60%
	RF_TRANS70       	= (7<<RF_TRANSSHIFT),   // 70%
	RF_TRANS80       	= (int32_t)(8U<<RF_TRANSSHIFT),   // 80%
	RF_TRANS90       	= (int32_t)(9U<<RF_TRANSSHIFT),   // 90%
	RF_GHOSTLY			= (RF_TRANS80 | RF_FULLBRIGHT),
	RF_GHOSTLYMASK		= (RF_TRANSMASK | RF_FULLBRIGHT),
} renderflags_t;

#if 0
typedef enum
{
	RF2_NOAFFINE          = 0x00000001,  // Disables affine drawing for this sprite
	RF2_AFFINEPRESCALE    = 0x00000002,  // Makes affines scale before rotating, instead of rotating before scaling
} renderflags2_t;
#endif

typedef enum
{
	SRF_SINGLE      = 0,   // 0-angle for all rotations
	SRF_3D          = 1,   // Angles 1-8
	SRF_3DGE        = 2,   // 3DGE, ZDoom and Doom Legacy all have 16-angle support. Why not us?
	SRF_3DMASK      = SRF_3D|SRF_3DGE, // 3
	SRF_LEFT        = 4,   // Left side uses single patch
	SRF_RIGHT       = 8,   // Right side uses single patch
	SRF_2D          = SRF_LEFT|SRF_RIGHT, // 12
	SRF_NONE        = 0xff // Initial value
} spriterotateflags_t;     // SRF's up!

//
// Sprites are patches with a special naming convention so they can be
//  recognized by R_InitSprites.
// The base name is NNNNFx or NNNNFxFx, with x indicating the rotation,
//  x = 0, 1-8, 9+A-G, L/R
// The sprite and frame specified by a thing_t is range checked at run time.
// A sprite is a patch_t that is assumed to represent a three dimensional
//  object and may have multiple rotations predrawn.
// Horizontal flipping is used to save space, thus NNNNF2F5 defines a mirrored patch.
// Some sprites will only have one picture used for all views: NNNNF0
// Some sprites will take the entirety of the left side: NNNNFL
// Or the right side: NNNNFR
// Or both, mirrored: NNNNFLFR
//
struct spriteframe_t
{
	// If false use 0 for any position.
	// Note: as eight entries are available, we might as well insert the same
	//  name eight times.
	uint8_t rotate; // see spriterotateflags_t above

	// Lump to use for view angles 0-7/15.
	lumpnum_t lumppat[16]; // lump number 16 : 16 wad : lump
	size_t lumpid[16]; // id in the spriteoffset, spritewidth, etc. tables

	// Flip bits (1 = flip) to use for view angles 0-7/15.
	uint16_t flip;

#ifdef ROTSPRITE
	rotsprite_t *rotated[2][16]; // Rotated patches
#endif
};

//
// A sprite definition:  a number of animation frames.
//
struct spritedef_t
{
	size_t numframes;
	spriteframe_t *spriteframes;
};

// Affine transformation bounding positions, in pixels. Used for Software to determine
// the dimensions of an affine patch.
struct affine_bounding_t
{
	vector2_t pivot;

	// Differences between the "pivot" of the bound and each corner position.
	int32_t xleft, yup, xright, ydown;

	// General length between each bounding position
	int32_t xlen, ylen;

	int32_t l;  // Leftmost bounding
	int32_t r;  // Rightmost bounding

	int32_t t;  // Highest bounding
	int32_t b;  // Lowest bounding
};

// an affine transformation matrix. maps screen coordinates to texture coordinates
struct affine_t
{
	fixed_t a; // horizontal step per pixel (M7A)
	fixed_t b; // horizontal step per scanline (M7B)
	fixed_t c; // vertical step per pixel (M7C)
	fixed_t d; // vertical step per scanline (M7D)
	fixed_t ox, oy; // transform origin in texture coordinates (M7X/M7Y)
};

// Column and span drawing data bundles

typedef struct
{
	lighttable_t* colormap;
	lighttable_t* fullbright;
	int32_t x;
	int32_t yl;
	int32_t yh;
	fixed_t iscale;
	fixed_t texturemid;
	uint8_t shadowcolor;

	uint8_t* source; // first pixel in a column
	uint8_t* brightmap; // brightmap texture column, can be NULL
	uint8_t* lightmap; // lighting only

	// translucency stuff here
	uint8_t* transmap;

	// translation stuff here
	uint8_t* translation;

	struct r_lightlist_t* lightlist;

	int32_t numlights;
	int32_t maxlights;

	//Fix TUTIFRUTI
	int32_t texheight;
	int32_t sourcelength;

	uint8_t r8_flatcolor;

	affine_bounding_t affinebound;
	affine_t affine;
	vector2_t affineoffset;
	fixed_t affineystep;
	f_vector2_t affinemosaic; // Truncates how columndrawers "move" across the screen
	fixed_t frac;
} drawcolumndata_t;

extern drawcolumndata_t g_dc;

typedef struct {
	float x, y, z;
} floatv3_t;

typedef struct
{
	int32_t y;
	int32_t x1;
	int32_t x2;
	lighttable_t* colormap;
	lighttable_t* fullbright;
	lighttable_t* translation;
	lighttable_t* flatlighting;

	fixed_t xfrac;
	fixed_t yfrac;
	fixed_t xstep;
	fixed_t ystep;
	int32_t waterofs;
	int32_t bgofs;

	fixed_t xoffs;
	fixed_t yoffs;

	uint16_t flatwidth;
	uint16_t flatheight;
	dboolean powersoftwo;

	visplane_t *currentplane;
	uint8_t *source;
	uint8_t *brightmap;
	uint8_t *transmap;

	uint8_t flatcolor;

	float zeroheight;

	// Vectors for Software's tilted slope drawers
	floatv3_t sup;
	floatv3_t svp;
	floatv3_t szp;
	floatv3_t slope_origin;
	floatv3_t slope_u;
	floatv3_t slope_v;

	// Variable flat sizes
	uint32_t nflatxshift;
	uint32_t nflatyshift;
	uint32_t nflatshiftup;
	uint32_t nflatmask;

	fixed_t planeheight;
	lighttable_t **planezlight;

	//
	// Water ripple effect
	// Needs the height of the plane, and the vertical position of the span.
	// Sets planeripple.xfrac and planeripple.yfrac, added to ds->xfrac and ds->yfrac, if the span is not tilted.
	//
	struct
	{
		int32_t offset;
		fixed_t xfrac, yfrac;
		dboolean active;
	} planeripple;

	uint8_t r8_flatcolor;
} drawspandata_t;

extern drawspandata_t g_ds;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
