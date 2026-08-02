// BLANKART
//-----------------------------------------------------------------------------
/// \file  k_kart.h
/// \brief SRB2kart stuff.

#ifndef __K_KART__
#define __K_KART__

#include "doomdef.h"
#include "d_player.h" // Need for player_t
#include "command.h" // Need for player_t
#include "g_game.h" // Need for mapnum_t

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
#define INVINTIME (10 * TICRATE)
#define SMONITORTIME (7 * TICRATE)

#define GROW_SCALE ((3*FRACUNIT)/2)
#define SHRINK_SCALE ((6*FRACUNIT)/8)

#define AUTORESPAWN_TIME (TICRATE/2)
#define AUTORESPAWN_THRESHOLD (TICRATE/4)

#define FLAMESTOREMAX TICRATE*2

// Fixed distance of a flipover.
#define FLIPOVERDIST (336<<FRACBITS)

// Time (in tics) of a flipover.
// (TICRATE * 0.56666)
#define FLIPOVERTICS (19)
#define FLIPOVERHALFTICS (FLIPOVERTICS>>1)

#define FLIPOVER_90DEGTIME ((FLIPOVERTICS>>2)+1) // TICRATE * 0.1666
#define FLIPOVER_360DEGTIME (FLIPOVER_90DEGTIME * 4)

// Force-stops a flipover if the timer exceeds this limit.
// Initially enough to last 2 rotations.
#define FLIPOVERLIMIT (FLIPOVER_360DEGTIME * 2)

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

#define CRASHJUMPHEIGHT (36 << FRACBITS)

#define CRASHJUMPTICS (14)
#define CRASHJUMPHALFTICS (7)

#define CRASHFINISHHEIGHT (675840) // 10.3125 * FRACUNIT

#define CRASH_TUMBLETIME (9 * TICRATE / 10)

#define CRASH_TUMBLE_ROTRETURNDELTA (420 * FRACUNIT / TICRATE)
#define CRASH_TUMBLE_ROTBOUNCEDELTA (166 * FRACUNIT / TICRATE)
#define CRASH_TUMBLE_ROTDELTA (180 * FRACUNIT / TICRATE)
#define CRASH_TUMBLE_YAWDELTA (13 * ANG1)

#define CRASH_TUMBLE_STARTYAW (56 * ANG1)
#define CRASH_TUMBLE_STARTROT (-60 * FRACUNIT)
#define CRASH_TUMBLE_MAXROT (115 * FRACUNIT)
#define CRASH_TUMBLE_MAXROT_AIR (160 * FRACUNIT)

// The initial "crash jump" after a DMG_CRASH tumble starts
#define CrashZMomentum(grav, scale)                                     \
    ((FixedMul(CRASHJUMPHEIGHT, scale) +                                          \
      ((grav >> 1) * (CRASHJUMPHALFTICS * CRASHJUMPHALFTICS))) / \
     CRASHJUMPHALFTICS)

#define CrashFinishZMomentum(grav, scale)                                   \
    ((FixedMul(CRASHFINISHHEIGHT, scale) +                                            \
      ((grav >> 1) * (CRASHJUMPHALFTICS * CRASHJUMPHALFTICS))) /     \
     CRASHJUMPHALFTICS)

typedef enum
{
	CLSMODE_AVERAGES,
	CLSMODE_BESTFIT
} ATTRPACK clustermode_e;

#define CLUSTERCHANGEDTIME (6 * TICRATE)

extern boolean clusterplayer[MAXPLAYERS];
extern UINT32 clusterid; // ID of the "cluster player", the one closest to the cluster point.
extern clustermode_e clustermode;
extern tic_t clusterjustchanged;
extern vector3_t clusterpoint, clusterdtf;

// Bubble Shield's maximum health (determines how many times it can be fully inflated)
#define MAXBUBBLEHEALTH (bubbletime * 3)

// Chip damage done to a Bubble Shield upon bumping a player when not inflated (rounded up)
#define BUBBLEBUMPCHIP ((MAXBUBBLEHEALTH + 1) / 2)

// Damage done to an inflated Bubble Shield (rounded up)
#define BUBBLEITMDAMAGE ((MAXBUBBLEHEALTH + 8) / 9)

// Bump weight for a Bubble Shield
#define BUBBLEMINWEIGHT (5 * FRACUNIT)

// Ranks: Worst possible score
#define LOWESTRANKSCORE (-200)

// Ranks: Best possible score
#define HIGHESTRANKSCORE (1000)

#if (LOWESTRANKSCORE < 0)
#define LOWESTRANKSCORE_ABS (-LOWESTRANKSCORE)
#else
#define LOWESTRANKSCORE_ABS (LOWESTRANKSCORE)
#endif

// "Mercy value" for par times
#define PARTIMEMERCY (33 * TICRATE)

#define RANKPOINTS_PARTIME CV_Get(&cv_kartrank_partimepoints)

#define RANKPOINTS_LEADERTIME CV_Get(&cv_kartrank_leadertimepoints)
#define RANKPOINTS_SPBTIME CV_Get(&cv_kartrank_spbtimepoints)
#define RANKPOINTS_STARTBOOST CV_Get(&cv_kartrank_startboostpoints)
#define RANKPOINTS_DRIFTBOOST CV_Get(&cv_kartrank_driftboostpoints)
#define RANKPOINTS_HITCONFIRM CV_Get(&cv_kartrank_hitconfirmpoints)

// Ranks: The value that the tics spent offroad are divided by, for score penalties.
// Was TICRATE / 20 rounded up, but it seemed too aggressive of a penalty.
#define OFFROADPENALTYDIV CV_Get(&cv_kartrank_offroad_divisor)

// Ranks: At this point, the game thinks you're mowing the offroad and penalizes you more harshly.
#define LAWNMOWERTHRESHOLD \
	((INT32)(FixedToFloat(CV_Get(&cv_kartrank_lawnmowerthreshold)) * TICRATE))

#define RANKPOINTS_WALLPENALTY CV_Get(&cv_kartrank_wallbonkpenalty)
#define RANKPOINTS_HZRDPENALTY CV_Get(&cv_kartrank_hazardpenalty)
#define RANKPOINTS_RSPWPENALTY CV_Get(&cv_kartrank_respawnpenalty)

#define RANKPOINTS_HZRDMERCY CV_Get(&cv_kartrank_hazardmercy)
#define RANKPOINTS_RSPWMERCY CV_Get(&cv_kartrank_respawnmercy)

#define RANKPOINTS_LAPGRADEMUL_WITHPENALTIES CV_Get(&cv_kartrank_lapgrademul)
#define RANKPOINTS_LAPGRADEMUL_NOPENALTIES CV_Get(&cv_kartrank_lapgrademul_penaltiesoff)

// Invincibility damage value
#define INVINDAMAGE CV_Get(&cv_kartinvindamage)

// S-Monitor distance value
#define SMONITORDIST CV_Get(&cv_kartsmonitordist)
#define SMONITORCONGADIST CV_Get(&cv_kartsmonitor_congadist)
#define SMONITORDECAYDIST CV_Get(&cv_kartsmonitor_decaydist)
#define SMONITORDECAYTIME CV_Get(&cv_kartsmonitor_decaytime)
#define SMONITORINTANGIBLE CV_Get(&cv_kartsmonitor_intangible)

// Precalculated constants for stacked boost diminishing
// *Somewhat* matches old calc but doesn't use arrays, which makes it faster and more memory efficent
#define DIMINISHPARAM CV_Get(&cv_kartstacking_diminishparam)
#define MAXVANILLABOOST CV_Get(&cv_kartstacking_maxvanillaboost)
#define SPEEDBOOSTDROPOFF CV_Get(&cv_kartstacking_speedboostdropoff)
#define SPEEDBOOSTDROPOFF_BRAKE CV_Get(&cv_kartstacking_speedboostdropoff_brake)
#define ACCELSTACK CV_Get(&cv_kartstacking_accelstack)

// Vanilla Boosts
#define EASYSNEAKERSPEEDBOOST CV_Get(&cv_kartstacking_sneaker_easyspeedboost)
#define NORMALSNEAKERSPEEDBOOST CV_Get(&cv_kartstacking_sneaker_normalspeedboost)
#define HARDSNEAKERSPEEDBOOST CV_Get(&cv_kartstacking_sneaker_hardspeedboost)
#define EXPERTSNEAKERSPEEDBOOST CV_Get(&cv_kartstacking_sneaker_expertspeedboost)
#define SNEAKERACCELBOOST CV_Get(&cv_kartstacking_sneaker_accelboost)
#define SNEAKERHANDLEBOOST CV_Get(&cv_kartstacking_sneaker_handleboost)
#define MAXSNEAKERSTACK CV_Get(&cv_kartstacking_sneaker_maxgrade)
#define SNEAKERSTACKABLE CV_Get(&cv_kartstacking_sneaker_stackable)
#define SEPARATEPANELS CV_Get(&cv_kartstacking_panel_separate)
#define MAXPANELSTACK CV_Get(&cv_kartstacking_panel_maxgrade)

#define INVINSPEEDBOOST CV_Get(&cv_kartstacking_invincibility_speedboost)
#define INVINACCELBOOST CV_Get(&cv_kartstacking_invincibility_accelboost)
#define INVINHANDLEBOOST CV_Get(&cv_kartstacking_invincibility_handleboost)
#define INVINSTACKABLE CV_Get(&cv_kartstacking_invincibility_stackable)

#define SMNTRSPEEDBOOST CV_Get(&cv_kartstacking_smonitor_speedboost)
#define SMNTRFASTESTSPEED CV_Get(&cv_kartstacking_smonitor_fastestspeed)
#define SMNTRACCELBOOST CV_Get(&cv_kartstacking_smonitor_accelboost)
#define SMNTRHANDLEBOOST CV_Get(&cv_kartstacking_smonitor_handleboost)
#define SMNTRSTACKABLE CV_Get(&cv_kartstacking_smonitor_stackable)

#define GROWSPEEDBOOST CV_Get(&cv_kartstacking_grow_speedboost)
#define GROWACCELBOOST CV_Get(&cv_kartstacking_grow_accelboost)
#define GROWHANDLEBOOST CV_Get(&cv_kartstacking_grow_handleboost)
#define GROWSTACKABLE CV_Get(&cv_kartstacking_grow_stackable)

#define BUBBLESPEEDBOOST CV_Get(&cv_kartstacking_bubble_speedboost)
#define BUBBLEACCELBOOST CV_Get(&cv_kartstacking_bubble_accelboost)
#define BUBBLEHANDLEBOOST CV_Get(&cv_kartstacking_bubble_handleboost)
#define BUBBLESTACKABLE CV_Get(&cv_kartstacking_bubble_stackable)

#define FLAMESPEEDVAL CV_Get(&cv_kartstacking_flame_speedval)
#define FLAMEACCELBOOST CV_Get(&cv_kartstacking_flame_accelboost)
#define FLAMEHANDLEBOOST CV_Get(&cv_kartstacking_flame_handleboost)
#define FLAMESTACKABLE CV_Get(&cv_kartstacking_flame_stackable)

#define ATTRACTIONAOEEMP CV_Get(&cv_kartattraction_aoeemp)
#define ATTRACTIONAOEDAMAGE CV_Get(&cv_kartattraction_aoedmg)
#define ATTRACTIONAOESHOVE CV_Get(&cv_kartattraction_aoeshove)
#define ATTRACTIONSPEEDHIMIN CV_Get(&cv_kartstacking_attraction_speedboost_himin)
#define ATTRACTIONSPEEDHIMAX CV_Get(&cv_kartstacking_attraction_speedboost_himax)
#define ATTRACTIONSPEEDNORMMIN CV_Get(&cv_kartstacking_attraction_speedboost_normmin)
#define ATTRACTIONSPEEDNORMMAX CV_Get(&cv_kartstacking_attraction_speedboost_normmax)
#define ATTRACTIONACCELHI CV_Get(&cv_kartstacking_attraction_accelboost_hi)
#define ATTRACTIONACCELNORM CV_Get(&cv_kartstacking_attraction_accelboost_norm)
#define ATTRACTIONHANDLEBOOST CV_Get(&cv_kartstacking_attraction_handleboost)
#define ATTRACTIONSTACKABLE CV_Get(&cv_kartstacking_attraction_stackable)

#define ALTSHRINKTIME CV_Get(&cv_kartaltshrinktime)
#define SHRINKSPEEDBOOST CV_Get(&cv_kartstacking_altshrink_speedboost)
#define SHRINKACCELBOOST CV_Get(&cv_kartstacking_altshrink_accelboost)
#define SHRINKHANDLEBOOST CV_Get(&cv_kartstacking_altshrink_handleboost)
#define SHRINKSTACKABLE CV_Get(&cv_kartstacking_altshrink_stackable)

#define STARTSPEEDBOOST CV_Get(&cv_kartstacking_start_speedboost)
#define STARTACCELBOOST CV_Get(&cv_kartstacking_start_accelboost)
#define STARTHANDLEBOOST CV_Get(&cv_kartstacking_start_handleboost)
#define STARTSTACKABLE CV_Get(&cv_kartstacking_start_stackable)

#define WALLTRANSFERSPEEDBOOST CV_Get(&cv_kartstacking_walltransfer_speedboost)
#define WALLTRANSFERACCELBOOST CV_Get(&cv_kartstacking_walltransfer_accelboost)
#define WALLTRANSFERHANDLEBOOST CV_Get(&cv_kartstacking_walltransfer_handleboost)
#define WALLTRANSFERSTACKABLE CV_Get(&cv_kartstacking_walltransfer_stackable)

#define DRIFTSPEEDBOOST CV_Get(&cv_kartstacking_drift_speedboost)
#define DRIFTACCELBOOST CV_Get(&cv_kartstacking_drift_accelboost)
#define DRIFTHANDLEBOOST CV_Get(&cv_kartstacking_drift_handleboost)
#define DRIFTSTACKABLE CV_Get(&cv_kartstacking_drift_stackable)

#define RINGSPEEDBOOST CV_Get(&cv_kartstacking_ring_speedboost)
#define RINGACCELBOOST CV_Get(&cv_kartstacking_ring_accelboost)
#define RINGHANDLEBOOST CV_Get(&cv_kartstacking_ring_handleboost)
#define RINGSTACKABLE CV_Get(&cv_kartstacking_ring_stackable)

#define HEAVYDROPSPEEDBOOST CV_Get(&cv_kartstacking_heavydrop_speedboost)
#define HEAVYDROPACCELBOOST CV_Get(&cv_kartstacking_heavydrop_accelboost)
#define HEAVYDROPHANDLEBOOST CV_Get(&cv_kartstacking_heavydrop_handleboost)
#define HEAVYDROPSTACKABLE CV_Get(&cv_kartstacking_heavydrop_stackable)

#define SSMTSPEEDBOOST CV_Get(&cv_kartstacking_ssmt_speedboost)
#define SSMTACCELBOOST CV_Get(&cv_kartstacking_ssmt_accelboost)
#define SSMTHANDLEBOOST CV_Get(&cv_kartstacking_ssmt_handleboost)

#define RECSPINACCELBOOSTLO CV_Get(&cv_kartstacking_recspin_accelboost_lo)
#define RECSPINHANDLEBOOSTLO CV_Get(&cv_kartstacking_recspin_handleboost_lo)

#define RECSPINACCELBOOSTHI CV_Get(&cv_kartstacking_recspin_accelboost_hi)
#define RECSPINHANDLEBOOSTHI CV_Get(&cv_kartstacking_recspin_handleboost_hi)

#define SLOPEDECAY CV_Get(&cv_kartstacking_slope_decay)
#define SLOPEBRAKEMOD CV_Get(&cv_kartstacking_slope_brakemod)
#define SLOPESPEEDBOOSTMAX CV_Get(&cv_kartstacking_slope_speedboost_max)
#define SLOPESPEEDBOOSTCAP CV_Get(&cv_kartstacking_slope_speedboost_cap)
#define SLOPEACCELBOOST CV_Get(&cv_kartstacking_slope_accelboost)
#define SLOPESTACKABLE CV_Get(&cv_kartstacking_slope_stackable)

#define DRAFTMINSPEED CV_Get(&cv_kartstacking_drafting_minspeed)
#define DRAFTMAXSPEED CV_Get(&cv_kartstacking_drafting_maxspeed)

#define CANTCHAINOFFROAD (cv_kartchaining.value && !cv_kartchainingoffroad.value)

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

#define POSGRADEFACTOR (1 << 24)
extern fixed_t positionfactor;

// Number of all players currently in the race
extern INT32 players_racing;

angle_t K_ReflectAngle(angle_t angle, angle_t against, fixed_t maxspeed, fixed_t yourspeed);

void K_RegisterKartStuff(void);

// Clears all rankscore tracker arrays.
void K_InitRankScoreTrackers(void);

// Adds the provided point value to the `rankscore_trackers` array of the given player ID.
void K_AddScoreToTracker(UINT16 pid, INT32 score);

// Logs all players' current rank-scores over a set interval once the race starts.
// External factors (such as laps and race finishes) are instead handled in their
// respective functions.
void K_LogPlayerRankScores(void);

void K_TimerReset(void);
void K_TimerInit(void);
UINT32 K_GetPlayerDontDrawFlag(player_t *player);
boolean K_IsPlayerLosing(const player_t *player);
fixed_t K_GetKartGameSpeedScalar(SINT8 value);

fixed_t K_GetMobjWeight(mobj_t *mobj, mobj_t *against);
boolean K_KartBouncing(mobj_t *mobj1, mobj_t *mobj2, boolean bounce, boolean solid);
void K_KartPainEnergyFling(player_t *player);
void K_FlipFromObject(mobj_t *mo, mobj_t *master);
void K_MatchGenericExtraFlags(mobj_t *mo, mobj_t *master);
void K_GenericExtraFlagsNoZAdjust(mobj_t *mo, mobj_t *master);
void K_SpawnDashDustRelease(player_t *player, boolean force);
void K_SpawnNormalSpeedLines(player_t *player);
void K_SpawnDraftSpeedLines(player_t *player, fixed_t scale, skincolornum_t color, boolean translucent);
void K_SpawnDashAreaSpeedLines(mobj_t *mo);
void K_SpawnBumpEffect(mobj_t *mo);
void K_KartMoveAnimation(player_t *player);
void K_KartPlayerHUDUpdate(player_t *player);
void K_KartResetPlayerColor(player_t *player);
UINT8 K_RaceLapCount(mapnum_t mapNum);
void K_KartPlayerThink(player_t *player, ticcmd_t *cmd);
void K_KartPlayerAfterThink(player_t *player);
angle_t K_MomentumAngle(mobj_t *mo);

fixed_t K_GetMomentum(mobj_t *mo, boolean twodee);
#define K_Momentum2D(mo) (K_GetMomentum(mo, true))
#define K_Momentum3D(mo) (K_GetMomentum(mo, false))

void K_AwardPlayerRings(player_t *player, UINT16 rings, boolean overload);
void K_HandleRaceSplits(player_t *player, tic_t time, UINT8 checkpoint);
void K_DoInstashield(player_t *player);
void K_BattleAwardHit(player_t *player, player_t *victim, mobj_t *inflictor, UINT8 bumpersRemoved);
void K_SpinPlayer(player_t *player, mobj_t *inflictor, mobj_t *source, INT32 type);
void K_FlipPlayer(player_t *player, mobj_t *inflictor, mobj_t *source);
void K_TumblePlayer(player_t *player, mobj_t *inflictor, mobj_t *source);
INT32 K_ExplodePlayer(player_t *player, mobj_t *inflictor, mobj_t *source);
boolean K_IsPlayerDamaged(const player_t *player);
void K_ResetCrashOffsetData(player_t *player);
void K_ResetCrashData(player_t *player);
void K_Player_CrashThinker(player_t *player);
void K_HandleBumperChanges(player_t *player, UINT8 prevBumpers);
void K_DestroyBumpers(player_t *player, UINT8 amount);
void K_TakeBumpersFromPlayer(player_t *player, player_t *victim, UINT8 amount);
void K_SpawnKartExplosion(fixed_t x, fixed_t y, fixed_t z, fixed_t radius, INT32 number, mobjtype_t type, angle_t rotangle, boolean spawncenter, boolean ghostit, mobj_t *source);
void K_SpawnMineExplosion(mobj_t *source, UINT8 color);
UINT16 K_DriftSparkColor(player_t *player, INT32 charge);
void K_SpawnBoostTrail(player_t *player);
void K_SpawnSparkleTrail(mobj_t *mo);
mobj_t *K_SpawnWipeoutTrail(mobj_t *mo, boolean translucent);
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
fixed_t K_GetSpeedPercentage(const player_t *player);
boolean K_AltShrinkArrowBulletCondition(const player_t *player);
boolean K_InterceptArrowBullet(player_t *player);
void K_ResetPogoSpring(player_t *player);

extern boolean forcefullinvintheme;
boolean K_PlayFullInvinTheme(void);
void K_DoInvincibility(player_t *player, tic_t time);
void K_DoSMonitor(player_t *player, tic_t time);
#define SMONITORWARNINGTIME (5*FRACUNIT/6)
fixed_t K_SMonitorGradient(UINT16 time);
fixed_t K_GetSMonitorSpeed(UINT16 time);
fixed_t K_GetSMonitorAccel(UINT16 time);
fixed_t K_GetSMonitorHandling(UINT16 time);
boolean K_SMonitorSlotHogging(player_t *player);

void K_KillBananaChain(mobj_t *banana, mobj_t *inflictor, mobj_t *source);
void K_UpdateHnextList(player_t *player, boolean clean);
void K_DropHnextList(player_t *player);
void K_PopPlayerShield(player_t *player);
boolean K_IsBubbleDefending(const player_t *player);
void K_RemoveBubbleHealth(player_t *player, INT16 sub);
boolean K_CheckBubbleChip(const mobj_t *mobj);
void K_RepairOrbitChain(mobj_t *orbit);
void K_CalculateBananaSlope(mobj_t *mobj, fixed_t x, fixed_t y, fixed_t z, fixed_t radius, fixed_t height, boolean flip, boolean player);
INT32 K_GetKartBaseRingPower(const player_t *player, boolean boosted);
UINT8 K_GetKartRingCap(const player_t *player);
boolean K_IsPlayerRingBurnt(const player_t *player, UINT16 ringpower);
INT32 K_GetKartRingPower(const player_t *player, boolean boosted);
size_t K_NextRespawnWaypointIndex(waypoint_t *waypoint);
boolean K_CheckPlayersRespawnColliding(INT32 playernum, fixed_t x, fixed_t y);
boolean K_SafeRespawnWaypoint(waypoint_t *waypoint);
void K_SetRespawnAtNextWaypoint(player_t * player);
INT16 K_GetKartTurnValue(const player_t *player, INT16 turnvalue);
INT32 K_GetKartDriftSparkValue(const player_t *player);
INT32 K_GetKartDriftSparkValueForStage(const player_t *player, UINT8 stage);
UINT8 K_GetKartDriftSparkStageForValue(const player_t *player, INT32 value);
INT32 K_GetDriftAngleOffset(player_t *player);
void K_KartUpdatePosition(player_t *player);
void K_KartLegacyUpdatePosition(player_t *player);
void K_UpdateClusterPoints(void);
UINT32 K_GetClusterDistanceForPlayer(player_t * player);
void K_UpdateAllPlayerPositions(void);
void K_GetPlayerNumber(void);
mobj_t *K_CreatePaperItem(fixed_t x, fixed_t y, fixed_t z, angle_t angle, SINT8 flip, UINT8 type, UINT8 amount);
void K_DropItems(player_t *player);
void K_DropRocketSneaker(player_t *player);
void K_DropKitchenSink(player_t *player);
void K_StripItems(player_t *player);
void K_StripOther(player_t *player);
void K_MomentumToFacing(player_t *player);
boolean K_ApplyOffroad(const player_t *player);
fixed_t K_GetSneakerBoostSpeed(void);
fixed_t K_PlayerTripwireSpeedThreshold(const player_t *player);
tripwirepass_t K_TripwirePassConditions(const player_t *player);
boolean K_TripwirePass(const player_t *player);
boolean K_PlayerCanPunt(const player_t *player);
boolean K_WaterRun(mobj_t *mobj);
void K_SpawnWaterTrail(mobj_t *mobj);
boolean K_ItemMobjAllowedtoWaterRun(mobj_t *item);
void K_SetTireGrease(player_t *player, tic_t tics);
boolean K_SlopeResistance(const player_t *player);
void K_RemoveGrowShrink(player_t *player);
void K_SquishPlayer(player_t *player, mobj_t *inflictor, mobj_t *source);
void K_ApplyTripWire(player_t *player, tripwirestate_t state);
fixed_t K_GetKartSpeedFromStat(UINT8 kartspeed, boolean karmabomb, fixed_t extrapct);
fixed_t K_GetKartSpeed(const player_t *player, boolean doboostpower, boolean dorubberbanding);
fixed_t K_GetKartAccel(const player_t *player);
void K_DoKartMagnetGradient(player_t *player);
void K_KartMagnetThinker(player_t *player);
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
boolean K_IsSPBInGame(void);

// sound stuff for lua
void K_PlayAttackTaunt(mobj_t *source);
void K_PlayBoostTaunt(mobj_t *source);
void K_PlayOvertakeSound(mobj_t *source);
void K_PlayPainSound(mobj_t *source, mobj_t *other);
void K_PlayHitEmSound(mobj_t *source, mobj_t *other);
void K_TryHurtSoundExchange(mobj_t *victim, mobj_t *attacker);
void K_PlayPowerGloatSound(mobj_t *source);

INT32 K_GetShieldFromPlayer(const player_t *player);
INT32 K_GetShieldFromItem(INT32 item);

boolean K_SafeRespawnPositionEx(fixed_t x, fixed_t y, fixed_t z, boolean flip);
#define K_SafeRespawnPosition(mobj) K_SafeRespawnPositionEx(mobj->x, mobj->y, mobj->z, (mobj->eflags & MFE_VERTICALFLIP) ? true : false)

boolean K_RingsActive(void);
boolean K_PurpleDriftActive(void);
boolean K_StackingActive(void);
boolean K_ChainingActive(void);
boolean K_SlipdashActive(void);
boolean K_SlopeBoostActive(void);
boolean K_DraftingActive(void);
SINT8 K_AirDropActive(void);
SINT8 K_AirThrustActive(void);
boolean K_RecoveryDashActive(void);
boolean K_WaterskipBricksActive(void);
boolean K_KeepStuffActive(void);
boolean K_ItemLitterActive(void);
boolean K_ItemListActive(void);
boolean K_ItemPushingActive(void);
boolean K_TrailSlowActive(void);
boolean K_RankingsActive(void);
boolean K_RankPenaltiesActive(void);
INT32 K_GetBumpSpark(void);
playerbumptype_t K_GetPlayerBump(void);
boolean K_BoostChain(player_t *player, INT32 timer, boolean chainsound);
INT32 K_ChainOrDeincrementTime(player_t *player, INT32 timer, INT32 deincrement, boolean chainsound);
boolean K_UsingLegacyCheckpoints(void);
boolean K_UsingPatchedMap(void);
INT32 K_CheckpointThreshold(boolean roundup);

ranktier_e K_GetRankTier(INT32 rank_score, SINT8 prankoverride);
void K_UpdateIndividualPlayerRank(player_t *player);
void K_UpdateRankScore(void);
void K_UpdatePositionFactorAndPlayerRanks(void);
void K_CalculateLapGrading(player_t *player);

fixed_t K_BoostRescale(fixed_t value,fixed_t oldmin,fixed_t oldmax,fixed_t newmin,fixed_t newmax);
void K_DoBoost(player_t *player, fixed_t speedboost, fixed_t accelboost, fixed_t handleboost, boolean stack, boolean visible);
void K_ClearBoost(player_t *player);

boolean K_NotFreePlay(void);
typedef enum
{
	ASR_ITEMBOX = 0,
	ASR_SUPERRING = 1,
} awardscaledrings_t;

void K_AwardScaledPlayerRings(player_t *player, SINT8 mode);

void K_QuiteSaltyHop(player_t *player);
boolean K_CheckWaterskipLockout(player_t *player);

typedef enum
{
	BUMPSPARK_NONE = 0,
	BUMPSPARK_NOCHARGE,
	BUMPSPARK_RESET100, // Reset to 100 (blue); the Sunflower's Garden system
	BUMPSPARK_ALL
} bumpsparktype_t;

typedef enum
{
	AIRDROP_NONE = 0,
	AIRDROP_LIGHT,
	AIRDROP_HEAVY,
	AIRDROP_FUSION,
} airdroptype_t;

typedef enum
{
	AIRTHRUST_NONE = 0,
	AIRTHRUST_BURST,
	AIRTHRUST_FLASH,
} airthrusttype_t;

#define AIRTHRUST_SPAMFACTOR 6 // higher makes it harder to spam
#define AIRTHRUST_FLASHEFXTIME (TICRATE/3) // base timer for vfx

void K_KillAirDrop(player_t *player, p_airdropflags_t airdropflags);

boolean K_NullDriftTiltEnabled(void);

void K_AddTiltMomentum(player_t *player, fixed_t angularvelocity);
void K_AddTiltImpulse(player_t *player, fixed_t distance, fixed_t force);

#define RECOVERYDASHADD (TICRATE/2)
#define RECOVERYDASHCHARGETIMEMIN (12)
//og val 2*TICRATE
#define RECOVERYDASHCHARGETIME (3*TICRATE/2)
//og val 3*TICRATE/2
#define RECOVERYDASHWIPETIME (RECOVERYDASHCHARGETIME - RECOVERYDASHADD)

void K_KartAttractHomingAttack(player_t *player);

void K_KartPlayerBumpyRoad(player_t *player);

fixed_t K_HandleAirDrag(player_t *player, boolean onground, fixed_t speed, fixed_t airdrag);

void K_ShoveObject(mobj_t *victim, mobj_t *source, fixed_t force);

// Array containing each player's rank-scores over the course of the race.
// During end-of-race intermissions, this is used for a chart display.
extern INT32 *rankscore_tracker[MAXPLAYERS];

#ifdef __cplusplus
} // extern "C"
#endif

// =========================================================================
#endif  // __K_KART__
