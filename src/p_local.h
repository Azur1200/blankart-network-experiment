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
/// \file  p_local.h
/// \brief Play functions, animation, global header

#ifndef __P_LOCAL__
#define __P_LOCAL__

#include "command.h"
#include "d_player.h"
#include "d_think.h"
#include "m_fixed.h"
#include "m_bbox.h"
#include "p_tick.h"
#include "r_defs.h"
#include "p_maputl.h"
#include "doomstat.h" // MAXSPLITSCREENPLAYERS

#ifdef __cplusplus
extern "C" {
#endif

extern consvar_t cv_naturalcamera;
extern consvar_t cv_lookbackinreplays;

#define FLOATSPEED (FRACUNIT*4)

//#define VIEWHEIGHTS "41"

// Maximum laps per map.
#define MAX_LAPS 99

// Maximum player score.
#define MAXSCORE 99999990 // 999999990

// mapblocks are used to check movement
// against lines and things
#define MAPBLOCKUNITS 128
#define MAPBLOCKSIZE  (MAPBLOCKUNITS*FRACUNIT)
#define MAPBLOCKSHIFT (FRACBITS+7)
#define MAPBMASK      (MAPBLOCKSIZE-1)
#define MAPBTOFRAC    (MAPBLOCKSHIFT-FRACBITS)

// Convenience macro to fix issue with collision along bottom/left edges of blockmap -Red
#define BMBOUNDFIX(xl, xh, yl, yh) {if (xl > xh) xl = 0; if (yl > yh) yl = 0;}

// MAXRADIUS is for precalculated sector block boxes
#define MAXRADIUS (MAPBLOCKSIZE >> 1)

// max Z move up or down without jumping
// above this, a height difference is considered as a 'dropoff'
#define MAXSTEPMOVE (24*FRACUNIT)

#define USERANGE (64*FRACUNIT)
#define MELEERANGE (64*FRACUNIT)
#define MISSILERANGE (32*64*FRACUNIT)

#define AIMINGTOSLOPE(aiming) FINESINE((aiming>>ANGLETOFINESHIFT) & FINEMASK)

#define P_GetPlayerViewHeight(player) (41*player->mo->height/48)

typedef enum
{
	THINK_DYNSLOPE,
	THINK_POLYOBJ,
	THINK_MAIN,
	THINK_MOBJ,
	THINK_FLOORS, // thanks diamond square

	// Lists after this may exist but they do not call an
	// action in P_RunThinkers
	NUM_ACTIVETHINKERLISTS,

	THINK_PRECIP = NUM_ACTIVETHINKERLISTS,

	NUM_THINKERLISTS
} thinklistnum_t; /**< Thinker lists. */
extern thinker_t thlist[];

void P_InitThinkers(void);
void P_InvalidateThinkersWithoutInit(void);
void P_AddThinker(const thinklistnum_t n, thinker_t *thinker);
void P_RemoveThinker(thinker_t *thinker);
void P_UnlinkThinker(thinker_t *thinker);

//
// P_USER
//
struct camera_t
{
	dboolean chase;
	dboolean freecam;

	angle_t aiming;

	// Things used by FS cameras.
	fixed_t viewheight;
	angle_t startangle;

	// Camera demobjerization
	// Info for drawing: position.
	fixed_t x, y, z;

	//More drawing info: to determine current sprite.
	angle_t angle; // orientation

	subsector_t *subsector;

	// The closest interval over all contacted Sectors (or Things).
	fixed_t floorz;
	fixed_t ceilingz;

	// For movement checking.
	fixed_t radius;
	fixed_t height;

	fixed_t relativex;

	// Momentums, used to update position.
	fixed_t momx, momy, momz;
	fixed_t pmomz;

	// SRB2Kart: camera pans while drifting
	fixed_t pan;
	// SRB2Kart: camera pitches on slopes
	angle_t pitch;
	
	// Freecam: A button was held since entering from menu, so don't move camera
	uint8_t button_a_held;
	
	dboolean reset_aiming;	// camera aiming needs to be reset from chase camera
	
	dboolean freelook_held;
	// pitch freelook
	// between -45 deg and 45 deg
	fixed_t freelook_pitch;
	// added influence from relative control methods (gyro)
	fixed_t freelook_pitch_add;
	// yaw freelook
	fixed_t freelook_yaw;
	// added influence from relative control methods (gyro)
	fixed_t freelook_yaw_add;

	// Interpolation data
	fixed_t old_x, old_y, old_z;
	angle_t old_angle, old_aiming;

	// postproccess effects
	uint8_t postimgflags;
};

// post process types
enum
{
	POSTIMG_WATER	= 1,	// Underwater screen effect.
	POSTIMG_MOTION	= 1<<1, // Unused motion blur effect.
	POSTIMG_FLIP	= 1<<2, // Flipcam screen effect.
	POSTIMG_HEAT	= 1<<3, // Heatwave screen effect.
	POSTIMG_MIRROR	= 1<<4, // encore screen effect.
};

extern camera_t camera[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_cam_dist[MAXSPLITSCREENPLAYERS], cv_cam_still[MAXSPLITSCREENPLAYERS], cv_cam_height[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_cam_speed[MAXSPLITSCREENPLAYERS], cv_cam_rotate[MAXSPLITSCREENPLAYERS];

extern consvar_t cv_tilting;
extern consvar_t cv_actionmovie;

extern fixed_t t_cam_dist[MAXSPLITSCREENPLAYERS], t_cam_height[MAXSPLITSCREENPLAYERS], t_cam_rotate[MAXSPLITSCREENPLAYERS];

void P_AddPlayerScore(player_t *player, uint32_t amount);
void P_ResetCamera(player_t *player, camera_t *thiscam);
dboolean P_TryCameraMove(fixed_t x, fixed_t y, camera_t *thiscam);
void P_DemoCameraMovement(camera_t *cam, uint8_t num);
void P_ToggleDemoCamera(uint8_t viewnum);
dboolean P_MoveChaseCamera(player_t *player, camera_t *thiscam, dboolean resetcalled);
void P_CalcChasePostImg(player_t *player, camera_t *thiscam);

dboolean P_CanPlayerTurn(const player_t *player, ticcmd_t *cmd);

dboolean P_PlayerInPain(const player_t *player);
void P_ResetPlayer(player_t *player);
dboolean P_PlayerCanDamage(player_t *player, mobj_t *thing);

fixed_t P_GetPlayerHeight(player_t *player);
fixed_t P_GetPlayerSpinHeight(player_t *player);
dboolean P_IsLocalPlayer(const player_t *player);
dboolean P_IsMachineLocalPlayer(player_t *player);
dboolean P_IsDisplayPlayer(player_t *player);

void P_SetPlayerAngle(player_t *player, angle_t angle);
angle_t P_GetLocalAngle(player_t *player);
void P_ForceLocalAngle(player_t *player, angle_t angle, dboolean interp);
dboolean P_PlayerFullbright(player_t *player);

dboolean P_IsObjectInGoop(const mobj_t *mo);
dboolean P_IsObjectOnGround(const mobj_t *mo);
dboolean P_IsObjectOnGroundIn(const mobj_t *mo, const sector_t *sec);
dboolean P_IsObjectOnRealGround(const mobj_t *mo, const sector_t *sec); // SRB2Kart
#define P_IsObjectFlipped(o) (((o)->eflags & MFE_VERTICALFLIP) == MFE_VERTICALFLIP)
dboolean P_InQuicksand(const mobj_t *mo);
dboolean P_PlayerHitFloor(player_t *player, dboolean fromAir);

void P_SetObjectMomZ(mobj_t *mo, fixed_t value, dboolean relative);
void P_RestoreMusic(player_t *player);
dboolean P_EndingMusic(player_t *player);
mobj_t *P_SpawnGhostMobjEX(mobj_t *mobj, dboolean legacy);
#define P_SpawnGhostMobj(mobj) P_SpawnGhostMobjEX(mobj, false)
int32_t P_GivePlayerRings(player_t *player, int32_t num_rings);
void P_GivePlayerLives(player_t *player, int32_t numlives);
uint8_t P_GetNextEmerald(void);
void P_GiveEmerald(dboolean spawnObj);
void P_GiveFinishFlags(player_t *player);
dboolean P_AutoPause(void);

void P_ElementalFire(player_t *player, dboolean cropcircle);
void P_SpawnSkidDust(player_t *player, fixed_t radius, dboolean sound);

void P_HaltPlayerOrbit(player_t *player);
void P_ExitPlayerOrbit(player_t *player);
dboolean P_PlayerOrbit(player_t *player);

fixed_t P_KartAltShrinkAuraScalar(player_t *player);
void P_MovePlayer(player_t *player);
void P_PlayerThink(player_t *player);
void P_PlayerAfterThink(player_t *player);
void P_DoPlayerExit(player_t *player, pflags_t flags);
void P_DoAllPlayersExit(pflags_t flags, dboolean trygivelife);
void P_DoTimeOver(player_t *player);

void P_InstaThrust(mobj_t *mo, angle_t angle, fixed_t move);
fixed_t P_ReturnThrustX(mobj_t *mo, angle_t angle, fixed_t move);
fixed_t P_ReturnThrustY(mobj_t *mo, angle_t angle, fixed_t move);

mobj_t *P_LookForFocusTarget(player_t *player, mobj_t *exclude, int8_t direction, uint8_t lockonflags);

void P_NukeEnemies(mobj_t *inflictor, mobj_t *source, fixed_t radius);

uint8_t P_FindLowestLap(void);
uint8_t P_FindHighestLap(void);

dboolean P_PlayerMoving(int32_t pnum);

void P_PlayLivesJingle(player_t *player);
void P_PlayRinglossSound(mobj_t *source);
void P_PlayDeathSound(mobj_t *source);
void P_PlayVictorySound(mobj_t *source);

dboolean P_GetLives(player_t *player);
dboolean P_SpectatorJoinGame(player_t *player);
void P_RestoreMultiMusic(player_t *player);

/// ------------------------
/// Jingle stuff
/// ------------------------

typedef enum
{
	JT_NONE,   // Null state
	JT_OTHER,  // Other state
	JT_MASTER, // Main level music
	JT_1UP, // Extra life
	JT_SHOES,  // Speed shoes
	JT_INV, // Invincibility
	JT_MINV, // Mario Invincibility
	JT_DROWN,  // Drowning
	JT_SUPER,  // Super Sonic
	JT_GOVER, // Game Over
	JT_NIGHTSTIMEOUT, // NiGHTS Time Out (10 seconds)
	JT_SSTIMEOUT, // NiGHTS Special Stage Time Out (10 seconds)

	// these are not jingles
	// JT_LCLEAR, // Level Clear
	// JT_RACENT, // Multiplayer Intermission
	// JT_CONTSC, // Continue

	NUMJINGLES
} jingletype_t;

struct jingle_t
{
	char musname[7];
	dboolean looping;
};

extern jingle_t jingleinfo[NUMJINGLES];

#define JINGLEPOSTFADE 1000

void P_PlayJingle(player_t *player, jingletype_t jingletype);
dboolean P_EvaluateMusicStatus(uint16_t status, const char *musname);
void P_PlayJingleMusic(player_t *player, const char *musname, uint16_t musflags, dboolean looping, uint16_t status);

//
// P_MOBJ
//
#define ONFLOORZ INT32_MIN
#define ONCEILINGZ INT32_MAX

// Time interval for item respawning.
// WARNING MUST be a power of 2
#define ITEMQUESIZE 1024

extern mapthing_t *itemrespawnque[ITEMQUESIZE];
extern tic_t itemrespawntime[ITEMQUESIZE];
extern size_t iquehead, iquetail;
extern consvar_t cv_gravity, cv_movebob;

mobjtype_t P_GetMobjtype(uint16_t mthingtype);

void P_RespawnSpecials(void);

mobj_t *P_AllocateMobj(void);

mobj_t *P_SpawnMobj(fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);

void P_CalculatePrecipFloor(precipmobj_t *mobj);
void P_RecalcPrecipInSector(sector_t *sector);
void P_PrecipitationEffects(void);

void P_RemoveMobj(mobj_t *th);
void P_RemoveSavegameMobj(mobj_t *th);
dboolean P_SetPlayerMobjState(mobj_t *mobj, statenum_t state);
dboolean P_SetMobjState(mobj_t *mobj, statenum_t state);

void P_RunOverlays(void);
#define OV_DONTSCREENOFFSET		1
#define OV_DONT3DOFFSET			1<<1
#define OV_DONTXYSCALE			1<<2
#define OV_DONTROLL				1<<3
#define OV_DONTBAKEOFFSET		1<<4
#define OV_DONTCOPYANGLE		1<<5
#define OV_DONTFORCEDISPOFFSET	1<<6

void P_HandleMinecartSegments(mobj_t *mobj);
void P_MobjThinker(mobj_t *mobj);
dboolean P_RailThinker(mobj_t *mobj);
void P_PushableThinker(mobj_t *mobj);
void P_SceneryThinker(mobj_t *mobj);

// This does not need to be added to Lua.
// To test it in Lua, check mobj.valid
FUNCINLINE static ATTRINLINE dboolean P_MobjWasRemoved(const mobj_t *mobj)
{
	return mobj == NULL || mobj->thinker.function != (actionf_p1)P_MobjThinker;
}

fixed_t P_MobjFloorZ(const mobj_t *mobj, const sector_t *sector, const sector_t *boundsec, fixed_t x, fixed_t y, const line_t *line, dboolean lowest, dboolean perfect);
fixed_t P_MobjCeilingZ(const mobj_t *mobj, const sector_t *sector, const sector_t *boundsec, fixed_t x, fixed_t y, const line_t *line, dboolean lowest, dboolean perfect);
#define P_GetFloorZ(mobj, sector, x, y, line) P_MobjFloorZ(mobj, sector, NULL, x, y, line, false, false)
#define P_GetCeilingZ(mobj, sector, x, y, line) P_MobjCeilingZ(mobj, sector, NULL, x, y, line, true, false)
#define P_GetFOFTopZ(mobj, sector, fof, x, y, line) P_MobjCeilingZ(mobj, sectors + fof->secnum, sector, x, y, line, false, false)
#define P_GetFOFBottomZ(mobj, sector, fof, x, y, line) P_MobjFloorZ(mobj, sectors + fof->secnum, sector, x, y, line, true, false)
#define P_GetSpecialBottomZ(mobj, src, bound) P_MobjFloorZ(mobj, src, bound, mobj->x, mobj->y, NULL, src != bound, true)
#define P_GetSpecialTopZ(mobj, src, bound) P_MobjCeilingZ(mobj, src, bound, mobj->x, mobj->y, NULL, src == bound, true)

fixed_t P_CameraFloorZ(camera_t *mobj, sector_t *sector, sector_t *boundsec, fixed_t x, fixed_t y, line_t *line, dboolean lowest, dboolean perfect);
fixed_t P_CameraCeilingZ(camera_t *mobj, sector_t *sector, sector_t *boundsec, fixed_t x, fixed_t y, line_t *line, dboolean lowest, dboolean perfect);
#define P_CameraGetFloorZ(mobj, sector, x, y, line) P_CameraFloorZ(mobj, sector, NULL, x, y, line, false, false)
#define P_CameraGetCeilingZ(mobj, sector, x, y, line) P_CameraCeilingZ(mobj, sector, NULL, x, y, line, true, false)
#define P_CameraGetFOFTopZ(mobj, sector, fof, x, y, line) P_CameraCeilingZ(mobj, sectors + fof->secnum, sector, x, y, line, false, false)
#define P_CameraGetFOFBottomZ(mobj, sector, fof, x, y, line) P_CameraFloorZ(mobj, sectors + fof->secnum, sector, x, y, line, true, false)

int32_t P_FloorPicAtPos(fixed_t x, fixed_t y, fixed_t z, fixed_t height);

dboolean P_InsideANonSolidFFloor(mobj_t *mobj, ffloor_t *rover);
dboolean P_CheckDeathPitCollide(const mobj_t *mo);
dboolean P_CheckSolidLava(ffloor_t *rover);
dboolean P_CheckDeathPitFOFs(const mobj_t *mo);
void P_AdjustMobjFloorZ_FFloors(mobj_t *mo, sector_t *sector, uint8_t motype);

mobj_t *P_SpawnMobjFromMobjUnscaled(mobj_t *mobj, fixed_t xofs, fixed_t yofs, fixed_t zofs, mobjtype_t type);
mobj_t *P_SpawnMobjFromMobj(mobj_t *mobj, fixed_t xofs, fixed_t yofs, fixed_t zofs, mobjtype_t type);

mobj_t *P_SpawnMissile(mobj_t *source, mobj_t *dest, mobjtype_t type);
mobj_t *P_SpawnXYZMissile(mobj_t *source, mobj_t *dest, mobjtype_t type, fixed_t x, fixed_t y, fixed_t z);
mobj_t *P_SpawnPointMissile(mobj_t *source, fixed_t xa, fixed_t ya, fixed_t za, mobjtype_t type, fixed_t x, fixed_t y, fixed_t z);
mobj_t *P_SpawnAlteredDirectionMissile(mobj_t *source, mobjtype_t type, fixed_t x, fixed_t y, fixed_t z, int32_t shiftingAngle);
mobj_t *P_SPMAngle(mobj_t *source, mobjtype_t type, angle_t angle, uint8_t aimtype, uint32_t flags2);
#define P_SpawnPlayerMissile(s,t,f) P_SPMAngle(s,t,s->angle,true,f)
#define P_SpawnNameFinder(s,t) P_SPMAngle(s,t,s->angle,true,0)
void P_ColorTeamMissile(mobj_t *missile, player_t *source);
int8_t P_MobjFlip(mobj_t *mobj);
fixed_t P_GetMobjGravity(mobj_t *mo);

void P_CalcChasePostImg(player_t *player, camera_t *thiscam);
dboolean P_CameraThinker(player_t *player, camera_t *thiscam, dboolean resetcalled);

void P_Attract(mobj_t *source, mobj_t *enemy, dboolean nightsgrab);
mobj_t *P_GetClosestAxis(mobj_t *source);

dboolean P_CanRunOnWater(mobj_t *mobj, ffloor_t *rover);
dboolean P_CheckSolidFFloorSurface(mobj_t *mobj, ffloor_t *rover);

void P_MaceRotate(mobj_t *center, int32_t baserot, int32_t baseprevrot);

void P_FlashPal(player_t *pl, uint16_t type, uint16_t duration);
#define PAL_WHITE    1
#define PAL_MIXUP    2
#define PAL_RECYCLE  3
#define PAL_NUKE     4

//
// P_ENEMY
//

// Is there a better place for these?
extern int32_t var1;
extern int32_t var2;

dboolean P_CheckMeleeRange(mobj_t *actor);
dboolean P_JetbCheckMeleeRange(mobj_t *actor);
dboolean P_FaceStabCheckMeleeRange(mobj_t *actor);
dboolean P_SkimCheckMeleeRange(mobj_t *actor);
dboolean P_CheckMissileRange(mobj_t *actor);

void P_NewChaseDir(mobj_t *actor);
dboolean P_LookForPlayers(mobj_t *actor, dboolean allaround, dboolean tracer, fixed_t dist);

void P_HomingAttack(mobj_t *source, mobj_t *enemy);

mobj_t *P_InternalFlickySpawn(mobj_t *actor, mobjtype_t flickytype, fixed_t momz, dboolean lookforplayers, int8_t moveforward);
void P_InternalFlickySetColor(mobj_t *actor, uint8_t color);
#define P_IsFlickyCenter(type) (type > MT_FLICKY_01 && type < MT_SEED && (type - MT_FLICKY_01) % 2 ? 1 : 0)
void P_InternalFlickyBubble(mobj_t *actor);
void P_InternalFlickyFly(mobj_t *actor, fixed_t flyspeed, fixed_t targetdist, fixed_t chasez);
void P_InternalFlickyHop(mobj_t *actor, fixed_t momz, fixed_t momh, angle_t angle);

//
// P_MAP
//

struct tm_t
{
	mobj_t *thing;
	fixed_t x, y;
	fixed_t bbox[4];
	int32_t flags;

	precipmobj_t *precipthing;
	fixed_t precipbbox[4];

	// If "floatok" true, move would be ok
	// if within "g_tm.floorz - g_tm.ceilingz".
	dboolean floatok;

	fixed_t floorz, ceilingz;
	fixed_t dropoffz, drpoffceilz; // drop-off floor/ceiling heights
	mobj_t *floorthing; // the thing corresponding to g_tm.floorz or NULL if g_tm.floorz is from a sector
	mobj_t *hitthing; // the solid thing you bumped into (for collisions)
	ffloor_t *floorrover, *ceilingrover;
	pslope_t *floorslope, *ceilingslope;
	int32_t floorpic, ceilingpic;
	fixed_t floorstep, ceilingstep;

	// keep track of the line that lowers the ceiling,
	// so missiles don't explode against sky hack walls
	line_t *ceilingline;

	// P_CheckPosition: this position blocks movement
	dboolean blocking;

	// P_CheckPosition: set this before each call to
	// P_CheckPosition to enable a line sweep on collided
	// lines
	dboolean sweep;

	// sweep: max step up at g_tm.x, g_tm.y
	fixed_t maxstep;
};

extern tm_t g_tm;

void P_RestoreTMStruct(tm_t tmrestore);

extern camera_t *mapcampointer;

/* cphipps 2004/08/30 */
extern void P_MapStart(void);
extern void P_MapEnd(void);

extern msecnode_t *sector_list;

extern mprecipsecnode_t *precipsector_list;

void P_UnsetBlockmapEntry(mobj_t *thing);
void P_SetBlockmapEntry(mobj_t *thing, blocknode_t **bmap);
void P_UnsetThingPosition(mobj_t *thing);
void P_SetThingPosition(mobj_t *thing);
void P_SetUnderlayPosition(mobj_t *thing);

struct TryMoveResult_t
{
	dboolean success;
	line_t *line;
	mobj_t *mo;
	dboolean slideout; // set if bouncing failed, and the moving mobj should slide
	vector2_t normal;
};

dboolean P_CheckPosition(mobj_t *thing, fixed_t x, fixed_t y, TryMoveResult_t *result);
dboolean P_CheckMove(mobj_t *thing, fixed_t x, fixed_t y, dboolean allowdropoff, TryMoveResult_t *result);
dboolean P_TryMove(mobj_t *thing, fixed_t x, fixed_t y, dboolean allowdropoff, TryMoveResult_t *result);
dboolean P_SceneryTryMove(mobj_t *thing, fixed_t x, fixed_t y, TryMoveResult_t *result);

void P_TestLine(line_t *ld);
void P_ClearTestLines(void);
line_t *P_SweepTestLines(fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by, fixed_t r, vector2_t *return_normal);

dboolean P_IsLineBlocking(const line_t *ld, const mobj_t *thing);
dboolean P_IsLineTripWire(const line_t *ld);
dboolean P_CheckCameraPosition(fixed_t x, fixed_t y, camera_t *thiscam);
fixed_t P_BaseStepUp(void);
fixed_t P_GetThingStepUp(mobj_t *thing, fixed_t destX, fixed_t destY);
dboolean P_Move(mobj_t *actor, fixed_t speed);
dboolean P_SetOrigin(mobj_t *thing, fixed_t x, fixed_t y, fixed_t z);
dboolean P_MoveOrigin(mobj_t *thing, fixed_t x, fixed_t y, fixed_t z);
void P_SlideMove(mobj_t *mo, TryMoveResult_t *result);
void P_BounceMove(mobj_t *mo, TryMoveResult_t *result);
dboolean P_CheckSight(mobj_t *t1, mobj_t *t2);
dboolean P_TraceBlockingLines(mobj_t *t1, mobj_t *t2);
dboolean P_TraceBotTraversal(mobj_t *t1, mobj_t *t2);
dboolean P_TraceWaypointTraversal(mobj_t *t1, mobj_t *t2);
void P_CheckHoopPosition(mobj_t *hoopthing, fixed_t x, fixed_t y, fixed_t z, fixed_t radius);

dboolean P_CheckSector(sector_t *sector, dboolean crunch);

void P_DelSeclist(msecnode_t *node);
void P_DelPrecipSeclist(mprecipsecnode_t *node);

void P_CreateSecNodeList(mobj_t *thing, fixed_t x, fixed_t y);
void P_Initsecnode(void);

void P_RadiusAttack(mobj_t *spot, mobj_t *source, fixed_t damagedist, uint8_t damagetype, dboolean sightcheck);

fixed_t P_FloorzAtPos(fixed_t x, fixed_t y, fixed_t z, fixed_t height);
fixed_t P_CeilingzAtPos(fixed_t x, fixed_t y, fixed_t z, fixed_t height);
BlockItReturn_t PIT_PushableMoved(mobj_t *thing);

void P_DoSpringRR(mobj_t * object, fixed_t scaleVal, fixed_t vertispeed, fixed_t horizspeed, angle_t finalAngle);
dboolean P_DoSpring(mobj_t *spring, mobj_t *object);

int32_t P_GetSectorLightNumAt(sector_t *sector, fixed_t x, fixed_t y, fixed_t z);
int32_t P_GetLightLevelFromSectorAt(sector_t *sector, fixed_t x, fixed_t y, fixed_t z);
int32_t P_GetSectorLightLevelAt(fixed_t x, fixed_t y, fixed_t z);
extracolormap_t *P_GetColormapFromSectorAt(sector_t *sector, fixed_t x, fixed_t y, fixed_t z);
extracolormap_t *P_GetSectorColormapAt(fixed_t x, fixed_t y, fixed_t z);

fixed_t P_GetFOFTopZAt (ffloor_t *rover, fixed_t x, fixed_t y);
fixed_t P_GetFOFBottomZAt (ffloor_t *rover, fixed_t x, fixed_t y);

fixed_t P_VeryTopOfFOF (ffloor_t *rover);
fixed_t P_VeryBottomOfFOF (ffloor_t *rover);

//
// P_SETUP
//
extern uint8_t *rejectmatrix; // for fast sight rejection
extern int32_t *blockmaplump; // offsets in blockmap are from here
extern int32_t *blockmap; // Big blockmap
extern int32_t bmapwidth;
extern int32_t bmapheight; // in mapblocks
extern fixed_t bmaporgx;
extern fixed_t bmaporgy; // origin of block map
extern blocknode_t **blocklinks; // for thing chains
extern blocknode_t **precipblocklinks; // special blockmap for precip rendering

extern struct minimapinfo
{
	patch_t *minimap_pic;
	int32_t min_x, min_y;
	int32_t max_x, max_y;
	int32_t map_w, map_h;
	int32_t minimap_w, minimap_h;
	fixed_t offs_x, offs_y;
	fixed_t zoom;
} minimapinfo;

//
// P_INTER
//

/* Damage/death types, for P_DamageMobj and related */
//// Damage types
#define DMG_NORMAL   0x00
#define DMG_WIPEOUT  0x01 // Normal, but with extra flashy effects
#define DMG_EXPLODE  0x02
#define DMG_SQUISH   0x03
#define DMG_VOLTAGE  0x04 // Normal but for killing the spb
#define DMG_KARMA    0x05 // Karma Bomb explosion -- works like DMG_EXPLODE, but steals half of their bumpers & deletes the rest
#define DMG_FLIPOVER 0x06 // Makes a player flip around in the air at a fixed speed.
#define DMG_CRASH    0x07 // Makes a player tumble and roll along the ground
//// Death types - cannot be combined with damage types
#define DMG_INSTAKILL  0x80
#define DMG_DEATHPIT   0x81
#define DMG_CRUSHED    0x82
#define DMG_SPECTATOR  0x83
#define DMG_TIMEOVER   0x84
// Masks
//free		 0x10
#define DMG_STEAL        0x20 // Flag - can steal bumpers, will only deal damage to players, and will not deal damage outside Battle Mode.
#define DMG_CANTHURTSELF 0x40 // Flag - cannot hurt your self or your team
#define DMG_DEATHMASK    DMG_INSTAKILL // if bit 7 is set, this is a death type instead of a damage type
#define DMG_TYPEMASK     0x0F // Get type without any flags

void P_SpecialStageDamage(player_t *player, mobj_t *inflictor, mobj_t *source);
dboolean P_DamageMobj(mobj_t *target, mobj_t *inflictor, mobj_t *source, int32_t damage, uint8_t damagetype);
void P_KillMobj(mobj_t *target, mobj_t *inflictor, mobj_t *source, uint8_t damagetype);
void P_PlayerRingBurst(player_t *player, int32_t num_rings); /// \todo better fit in p_user.c

void P_TouchSpecialThing(mobj_t *special, mobj_t *toucher, dboolean heightcheck);
void P_TouchStarPost(mobj_t *starpost, player_t *player, dboolean snaptopost);
void P_CheckTimeLimit(void);
void P_CheckPointLimit(void);
dboolean P_CheckRacers(void);

typedef enum
{
	PICKUPITEM_RING,
	PICKUPITEM_ITEM,
	PICKUPITEM_EGGMAN,
	PICKUPITEM_FLOATING,
	PICKUPITEM_EGGMINE,
	PICKUPITEM_SUBITEM,
} pickupitem_e;

dboolean P_CanPickupItem(player_t *player, uint8_t weapon);

//
// P_SPEC
//
#include "p_spec.h"

extern int32_t ceilmovesound;

// Factor to scale scrolling effect into mobj-carrying properties = 3/32.
// (This is so scrolling floors and objects on them can move at same speed.)
#define CARRYFACTOR (FRACUNIT-ORIG_FRICTION)

void P_MixUp(mobj_t *thing, fixed_t x, fixed_t y, fixed_t z, angle_t angle,
			fixed_t starpostx, fixed_t starposty, fixed_t starpostz,
			int32_t starpostnum, tic_t starposttime, angle_t starpostangle,
			fixed_t starpostscale, angle_t drawangle, int32_t flags2);
dboolean P_Teleport(mobj_t *thing, fixed_t x, fixed_t y, fixed_t z, angle_t angle, dboolean flash, dboolean dontstopmove);
dboolean P_SetMobjStateNF(mobj_t *mobj, statenum_t state);
dboolean P_CheckMissileSpawn(mobj_t *th);
void P_Thrust(mobj_t *mo, angle_t angle, fixed_t move);
void P_ExplodeMissile(mobj_t *mo);
void P_CheckGravity(mobj_t *mo, dboolean affect);
void P_UpdatePitchRollFromSlope(mobj_t *mo, pslope_t *slope);
void P_SetPitchRollFromSlope(mobj_t *mo, pslope_t *slope);
void P_SetPitchRoll(mobj_t *mo, angle_t pitch, angle_t yaw);
fixed_t P_ScaleFromMap(fixed_t n, fixed_t scale);
fixed_t P_GetMobjHead(const mobj_t *);
fixed_t P_GetMobjFeet(const mobj_t *);
fixed_t P_GetMobjGround(const mobj_t *);
fixed_t P_GetMobjZMovement(mobj_t *mo);
dboolean P_WaterRunning(mobj_t *thing);

void P_InitTIDHash(void);
void P_AddThingTID(mobj_t *mo);
void P_SetThingTID(mobj_t *mo, mtag_t tid);
void P_RemoveThingTID(mobj_t *mo);
mobj_t *P_FindMobjFromTID(mtag_t tid, mobj_t *i, mobj_t *activator);
void P_DeleteMobjStringArgs(mobj_t *mobj);

void P_ResetPlayerCheats(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __P_LOCAL__
