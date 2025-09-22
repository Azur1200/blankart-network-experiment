// BLANKART
//-----------------------------------------------------------------------------
/// \file  k_kart.h
/// \brief SRB2kart stuff.

#ifndef __K_KART__
#define __K_KART__

#include "doomdef.h"
#include "d_player.h" // Need for player_t
#include "command.h" // Need for player_t

#ifdef __cplusplus
extern "C" {
#endif

extern consvar_t cv_kartstacking_colorflame;
extern consvar_t cv_kartstacking_sneakerstacksound;
extern consvar_t cv_kartchainingsound;
extern consvar_t cv_kartdriftsounds;
extern consvar_t cv_kartdriftefx;

#define KART_FULLTURN 800

/*
Number of animations for the invincibility sparkles
If states are ever added or removed
Make sure this matches the actual number of states
*/
#define KART_NUMINVSPARKLESANIM 12

#define GROW_SCALE ((3*FRACUNIT)/2)
#define SHRINK_SCALE ((6*FRACUNIT)/8)

#define BASEINVINTIME (10 * TICRATE)

#define AUTORESPAWN_TIME (10 * TICRATE)
#define AUTORESPAWN_THRESHOLD (7 * TICRATE)

#define FLAMESTOREMAX TICRATE*2

// Fixed distance of a flipover.
#define FLIPOVERDIST (336<<FRACBITS)

// Time (in tics) of a flipover.
// (TICRATE * (0.56666 * FRACUNIT))
#define FLIPOVERTICS 19
#define FLIPOVERHALFTICS (FLIPOVERTICS>>1)

#define FLIPOVER_90DEGTIME ((FLIPOVERTICS>>2)+1) // 0.1666 * FRACUNIT

// Angle movement for a flipover
// ANGLE_90 / FLIPOVER_90DEGTIME
// Precalculating it here for some slight optimization.
#define FLIPOVERANG 0x0CCCCCCC

#define FLIPOVERSPEED (FLIPOVERDIST / FLIPOVERTICS)

#define FLIPOVERHEIGHT (32 << FRACBITS)

// By the middle of a jump arc, you'll reach the apex.
#define FlipOverZMomentum(grav)                                \
    ((FLIPOVERHEIGHT +                                         \
      ((grav >> 1) * (FLIPOVERHALFTICS * FLIPOVERHALFTICS))) / \
     FLIPOVERHALFTICS)

extern boolean clusterplayer[MAXPLAYERS];
extern UINT32 clusterid; // ID of the "cluster player", the one closest to the cluster point.
extern vector3_t clusterpoint, clusterdtf;

// Bubble Shield's maximum health
#define MAXBUBBLEHEALTH 6

// Chip damage done to a Bubble Shield upon bumping a player
#define BUBBLEBUMPCHIP 1

// HP cost for a Bubble Shield to inflate
#define BUBBLEUSECOST (MAXBUBBLEHEALTH / 3)

// Damage done to an inflated Bubble Shield by items. By default, this is the maximum health,
// so that the shield immediately breaks
#define BUBBLEITMDAMAGE (MAXBUBBLEHEALTH)

// Invincibility-related constants
#define INVINDIST K_RAGuard(cv_kartinvindist)
#define INVINDISTMUL K_RAGuard(cv_kartinvindistmul)
#define INVINMIDTIME K_RAGuard(cv_kartinvin_midtime)
#define INVINMAXTIME K_RAGuard(cv_kartinvin_maxtime)

// Precalculated constants for stacked boost diminishing
// *Somewhat* matches old calc but doesn't use arrays, which makes it faster and more memory efficent
#define DIMINISHPARAM K_RAGuard(cv_kartstacking_diminishparam)
#define MAXVANILLABOOST K_RAGuard(cv_kartstacking_maxvanillaboost)
#define SPEEDBOOSTDROPOFF K_RAGuard(cv_kartstacking_speedboostdropoff)
#define SPEEDBOOSTDROPOFF_BRAKE K_RAGuard(cv_kartstacking_speedboostdropoff_brake)
#define ACCELSTACK K_RAGuard(cv_kartstacking_accelstack)

// Vanilla Boosts
#define EASYSNEAKERSPEEDBOOST K_RAGuard(cv_kartstacking_sneaker_easyspeedboost)
#define NORMALSNEAKERSPEEDBOOST K_RAGuard(cv_kartstacking_sneaker_normalspeedboost)
#define HARDSNEAKERSPEEDBOOST K_RAGuard(cv_kartstacking_sneaker_hardspeedboost)
#define EXPERTSNEAKERSPEEDBOOST K_RAGuard(cv_kartstacking_sneaker_expertspeedboost)
#define SNEAKERACCELBOOST K_RAGuard(cv_kartstacking_sneaker_accelboost)
#define MAXSNEAKERSTACK K_RAGuard(cv_kartstacking_sneaker_maxgrade)
#define SNEAKERSTACKABLE K_RAGuard(cv_kartstacking_sneaker_stackable)

#define INVINSPEEDBOOSTLGC K_RAGuard(cv_kartstacking_invincibility_legacyspeedboost)
#define INVINACCELBOOSTLGC K_RAGuard(cv_kartstacking_invincibility_legacyaccelboost)
#define INVINSPEEDBOOSTALT K_RAGuard(cv_kartstacking_invincibility_alternatespeedboost)
#define INVINACCELBOOSTALT K_RAGuard(cv_kartstacking_invincibility_alternateaccelboost)
#define INVINSTACKABLE K_RAGuard(cv_kartstacking_invincibility_stackable)

#define GROWSPEEDBOOST K_RAGuard(cv_kartstacking_grow_speedboost)
#define GROWACCELBOOST K_RAGuard(cv_kartstacking_grow_accelboost)
#define GROWSTACKABLE K_RAGuard(cv_kartstacking_grow_stackable)

#define FLAMESPEEDVAL K_RAGuard(cv_kartstacking_flame_speedval)
#define FLAMEACCELBOOST K_RAGuard(cv_kartstacking_flame_accelboost)
#define FLAMESTACKABLE K_RAGuard(cv_kartstacking_flame_stackable)

#define STARTSPEEDBOOST K_RAGuard(cv_kartstacking_start_speedboost)
#define STARTACCELBOOST K_RAGuard(cv_kartstacking_start_accelboost)
#define STARTSTACKABLE K_RAGuard(cv_kartstacking_start_stackable)

#define DRIFTSPEEDBOOST K_RAGuard(cv_kartstacking_drift_speedboost)
#define DRIFTACCELBOOST K_RAGuard(cv_kartstacking_drift_accelboost)
#define DRIFTSTACKABLE K_RAGuard(cv_kartstacking_drift_stackable)

#define RINGSPEEDBOOST K_RAGuard(cv_kartstacking_ring_speedboost)
#define RINGACCELBOOST K_RAGuard(cv_kartstacking_ring_accelboost)
#define RINGSTACKABLE K_RAGuard(cv_kartstacking_ring_stackable)

#define SLOPEDECAY K_RAGuard(cv_kartstacking_slope_decay)
#define SLOPEBRAKEMOD K_RAGuard(cv_kartstacking_slope_brakemod)
#define SLOPESPEEDBOOSTMAX K_RAGuard(cv_kartstacking_slope_speedboost_max)
#define SLOPESPEEDBOOSTCAP K_RAGuard(cv_kartstacking_slope_speedboost_cap)
#define SLOPEACCELBOOST K_RAGuard(cv_kartstacking_slope_accelboost)
#define SLOPESTACKABLE K_RAGuard(cv_kartstacking_slope_stackable)

#define DRAFTMINSPEED K_RAGuard(cv_kartstacking_drafting_minspeed)
#define DRAFTMAXSPEED K_RAGuard(cv_kartstacking_drafting_maxspeed)

#define CANTCHAINOFFROAD (cv_kartchaining.value && !cv_kartchainingoffroad.value)

// Used for respawning checks.

typedef struct respawnresult_s
{
	sector_t *cursec;
	fixed_t bestsectorheight;
	fixed_t bestfofheight;
	ffloor_t *bestrover;
	fixed_t bestpoheight;
	polyobj_t *bestpo;
	fixed_t finaldistances[3];

	SINT8 result;
} respawnresult_t;

typedef enum
{
	RESPAWNRS_SECTOR,
	RESPAWNRS_ROVER,
	RESPAWNRS_PO,
} respawnresult_e;

#define MAXRINGVOLUME 255
#define MAXRINGTRANSPARENCY 255
#define MINRINGVOLUME 100
#define MINRINGTRANSPARENCY 100
#define RINGVOLUMECOLLECTPENALTY 3
#define RINGTRANSPARENCYCOLLECTPENALTY 0
#define RINGVOLUMEUSEPENALTY 15
#define RINGTRANSPARENCYUSEPENALTY 15
#define RINGVOLUMEREGEN 1
#define RINGTRANSPARENCYREGEN 3

INT32 K_RAGuard(consvar_t cvar);

angle_t K_ReflectAngle(angle_t angle, angle_t against, fixed_t maxspeed, fixed_t yourspeed);

void K_RegisterKartStuff(void);

void K_TimerInit(void);
UINT32 K_GetPlayerDontDrawFlag(player_t *player);
boolean K_IsPlayerLosing(player_t *player);
fixed_t K_GetKartGameSpeedScalar(SINT8 value);

fixed_t K_GetMobjWeight(mobj_t *mobj, mobj_t *against);
boolean K_KartBouncing(mobj_t *mobj1, mobj_t *mobj2, boolean bounce, boolean solid);
void K_KartPainEnergyFling(player_t *player);
void K_FlipFromObject(mobj_t *mo, mobj_t *master);
void K_MatchGenericExtraFlags(mobj_t *mo, mobj_t *master);
void K_GenericExtraFlagsNoZAdjust(mobj_t *mo, mobj_t *master);
void K_SpawnDashDustRelease(player_t *player);
void K_SpawnNormalSpeedLines(player_t *player);
void K_SpawnDraftSpeedLines(player_t *player, fixed_t scale, skincolornum_t color, boolean translucent);
void K_SpawnInvincibilitySpeedLines(mobj_t *mo);
void K_SpawnBumpEffect(mobj_t *mo);
void K_KartMoveAnimation(player_t *player);
void K_KartPlayerHUDUpdate(player_t *player);
void K_KartResetPlayerColor(player_t *player);
UINT8 K_RaceLapCount(INT16 mapNum);
void K_KartPlayerThink(player_t *player, ticcmd_t *cmd);
void K_KartPlayerAfterThink(player_t *player);
angle_t K_MomentumAngle(mobj_t *mo);
void K_AwardPlayerRings(player_t *player, UINT16 rings, boolean overload);
void K_HandleRaceSplits(player_t *player, tic_t time, UINT8 checkpoint);
void K_DoInstashield(player_t *player);
void K_BattleAwardHit(player_t *player, player_t *victim, mobj_t *inflictor, UINT8 bumpersRemoved);
void K_SpinPlayer(player_t *player, mobj_t *inflictor, mobj_t *source, INT32 type);
void K_FlipPlayer(player_t *player, mobj_t *inflictor, mobj_t *source);
INT32 K_ExplodePlayer(player_t *player, mobj_t *inflictor, mobj_t *source);
boolean K_IsPlayerDamaged(const player_t *player);
void K_HandleBumperChanges(player_t *player, UINT8 prevBumpers);
void K_DestroyBumpers(player_t *player, UINT8 amount);
void K_TakeBumpersFromPlayer(player_t *player, player_t *victim, UINT8 amount);
void K_SpawnKartExplosion(fixed_t x, fixed_t y, fixed_t z, fixed_t radius, INT32 number, mobjtype_t type, angle_t rotangle, boolean spawncenter, boolean ghostit, mobj_t *source);
void K_SpawnMineExplosion(mobj_t *source, UINT8 color);
UINT16 K_DriftSparkColor(player_t *player, INT32 charge);
void K_SpawnBoostTrail(player_t *player);
void K_SpawnSparkleTrail(mobj_t *mo);
void K_SpawnWipeoutTrail(mobj_t *mo, boolean translucent);
void K_DriftDustHandling(mobj_t *spawner);
mobj_t *K_ThrowKartItem(player_t *player, boolean missile, mobjtype_t mapthing, INT32 defaultDir, INT32 altthrow);
void K_PuntMine(mobj_t *mine, mobj_t *punter);

typedef enum
{
	SNEAKERTYPE_PANEL = 0,
	SNEAKERTYPE_SNEAKER,
	SNEAKERTYPE_ROCKETSNEAKER,
	SNEAKERTYPE_WATERPANEL,
} sneakertype_t;

void K_DoSneaker(player_t *player, INT32 type);

void K_DoPogoSpring(mobj_t *mo, fixed_t vertispeed, UINT8 sound);
fixed_t K_InvincibilityGradient(UINT16 time);
UINT16 K_GetInvincibilityTime(player_t *player);
void K_DoInvincibility(player_t *player, tic_t time);
void K_KillBananaChain(mobj_t *banana, mobj_t *inflictor, mobj_t *source);
void K_UpdateHnextList(player_t *player, boolean clean);
void K_DropHnextList(player_t *player);
void K_PopPlayerShield(player_t *player);
void K_RemoveBubbleHealth(player_t *player, INT16 sub);
void K_RepairOrbitChain(mobj_t *orbit);
void K_CalculateBananaSlope(mobj_t *mobj, fixed_t x, fixed_t y, fixed_t z, fixed_t radius, fixed_t height, boolean flip, boolean player);
player_t *K_FindJawzTarget(mobj_t *actor, player_t *source);
INT32 K_GetKartRingPower(const player_t *player, boolean boosted);
size_t K_NextRespawnWaypointIndex(waypoint_t *waypoint);
boolean K_CheckPlayersRespawnColliding(INT32 playernum, fixed_t x, fixed_t y);
void K_SetRespawnAtNextWaypoint(player_t * player);
INT16 K_GetKartTurnValue(const player_t *player, INT16 turnvalue);
INT32 K_GetKartDriftSparkValue(const player_t *player);
INT32 K_GetKartDriftSparkValueForStage(const player_t *player, UINT8 stage);
UINT8 K_GetKartDriftSparkStageForValue(const player_t *player, INT32 value);
INT32 K_GetDriftAngleOffset(player_t *player);
void K_KartUpdatePosition(player_t *player);
void K_KartLegacyUpdatePosition(player_t *player);
void K_UpdateClusterPoints(void);
void K_UpdateAllPlayerPositions(void);
mobj_t *K_CreatePaperItem(fixed_t x, fixed_t y, fixed_t z, angle_t angle, SINT8 flip, UINT8 type, UINT8 amount);
void K_DropItems(player_t *player);
void K_DropRocketSneaker(player_t *player);
void K_DropKitchenSink(player_t *player);
void K_StripItems(player_t *player);
void K_StripOther(player_t *player);
void K_MomentumToFacing(player_t *player);
boolean K_ApplyOffroad(const player_t *player);
fixed_t K_PlayerTripwireSpeedThreshold(const player_t *player);
tripwirepass_t K_TripwirePassConditions(const player_t *player);
boolean K_TripwirePass(const player_t *player);
boolean K_WaterRun(mobj_t *mobj);
void K_SpawnWaterTrail(mobj_t *mobj);
boolean K_ItemMobjAllowedtoWaterRun(mobj_t *item);
void K_SetTireGrease(player_t *player, tic_t tics);
boolean K_SlopeResistance(const player_t *player);
void K_SquishPlayer(player_t *player, mobj_t *inflictor, mobj_t *source);
void K_ApplyTripWire(player_t *player, tripwirestate_t state);
fixed_t K_GetKartSpeedFromStat(UINT8 kartspeed, boolean karmabomb);
fixed_t K_GetKartSpeed(const player_t *player, boolean doboostpower, boolean dorubberbanding);
fixed_t K_GetKartAccel(const player_t *player);
UINT16 K_GetKartFlashing(const player_t *player);
boolean K_PlayerShrinkCheat(const player_t *player);
void K_UpdateShrinkCheat(player_t *player);
boolean K_KartKickstart(const player_t *player);
UINT16 K_GetKartButtons(const player_t *player);
SINT8 K_GetForwardMove(const player_t *player);
SINT8 K_GetSideMove(const player_t *player);
fixed_t K_GetNewSpeed(const player_t *player);
fixed_t K_3dKartMovement(const player_t *player, boolean onground, SINT8 fowardmove);
SINT8 K_Sliptiding(const player_t *player);
fixed_t K_PlayerBaseFriction(const player_t *player, fixed_t original);
void K_MoveKartPlayer(player_t *player, boolean onground);
void K_CheckSpectateStatus(boolean considermapreset);
UINT8 K_GetInvincibilityItemFrame(void);
UINT8 K_GetMultItemFrame(UINT8 count, UINT8 max);
boolean K_IsSPBInGame(void);

// sound stuff for lua
void K_PlayAttackTaunt(mobj_t *source);
void K_PlayBoostTaunt(mobj_t *source);
void K_PlayOvertakeSound(mobj_t *source);
void K_PlayPainSound(mobj_t *source, mobj_t *other);
void K_PlayHitEmSound(mobj_t *source, mobj_t *other);
void K_TryHurtSoundExchange(mobj_t *victim, mobj_t *attacker);
void K_PlayPowerGloatSound(mobj_t *source);

INT32 K_GetShieldFromPlayer(player_t *player);
INT32 K_GetShieldFromItem(INT32 item);

void K_SetItemOut(player_t *player);
void K_UnsetItemOut(player_t *player);

boolean K_SafeRespawnPosition(mobj_t * mo);

boolean K_RingsActive(void);
boolean K_PurpleDriftActive(void);
boolean K_StackingActive(void);
boolean K_ChainingActive(void);
boolean K_SlipdashActive(void);
boolean K_SlopeBoostActive(void);
boolean K_DraftingActive(void);
boolean K_GetKartInvinType(void);
boolean K_BoostChain(player_t *player, INT32 timer, boolean chainsound);
INT32 K_ChainOrDeincrementTime(player_t *player, INT32 timer, INT32 deincrement, boolean chainsound);
boolean K_UsingLegacyCheckpoints(void);
boolean K_UsingPatchedMap(void);
INT32 K_CheckpointThreshold(boolean roundup);

void K_UpdateMobjItemOverlay(mobj_t *part, SINT8 itemType, UINT8 itemCount);

fixed_t K_BoostRescale(fixed_t value,fixed_t oldmin,fixed_t oldmax,fixed_t newmin,fixed_t newmax);
void K_DoBoost(player_t *player, fixed_t speedboost, fixed_t accelboost, boolean stack, boolean visible);
void K_ClearBoost(player_t *player);

boolean K_NotFreePlay(void);
typedef enum
{
	ASR_ITEMBOX = 0,
	ASR_SUPERRING = 1,
} awardscaledrings_t;

void K_AwardScaledPlayerRings(player_t *player, SINT8 mode);

void K_QuiteSaltyHop(player_t *player);

typedef enum
{
	BUMPSPARK_NONE = 0,
	BUMPSPARK_NOCHARGE,
	BUMPSPARK_ALL
} bumpsparktype_t;

INT32 K_GetBumpSpark(void);

#ifdef __cplusplus
} // extern "C"
#endif

// =========================================================================
#endif  // __K_KART__
