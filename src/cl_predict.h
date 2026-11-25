// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by Antigravity / BlanKart Contributors.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  cl_predict.h
/// \brief Local-kart client-side movement prediction (DarkPlaces-style)
///        and native input ACK / redundant repeat protocol.

#ifndef __CL_PREDICT__
#define __CL_PREDICT__

#include "doomdef.h"
#include "command.h"
#include "d_ticcmd.h"
#include "d_clisrv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLPRED_QUEUE_CAP 32

// Fast read-only prediction guard checked across physics / map / sound / Lua
extern dboolean cl_pred_active_flag;

static inline dboolean P_IsPredicting(void)
{
	return cl_pred_active_flag;
}

typedef struct cl_pred_cmd_s
{
	uint32_t seq;
	tic_t client_leveltime;
	ticcmd_t cmd;
} cl_pred_cmd_t;

// Prediction CVars
extern consvar_t cv_cl_movement;
extern consvar_t cv_cl_movement_maxtics;
extern consvar_t cv_cl_movement_smooth;
extern consvar_t cv_cl_movement_smoothrate;
extern consvar_t cv_cl_netrepeatinput;
extern consvar_t cv_cl_movement_debug;
extern consvar_t cv_cl_movement_selftest;

// Registration (called from D_ClientServerInit)
void CL_PredRegisterVars(void);

// ---- Server-side native input queue & ACK helpers ----
void SV_PredResetAll(void);
void SV_PredResetNode(int32_t node);
dboolean SV_PredNodeEnabled(int32_t node);
dboolean SV_PredNodeUsesPredCmd(int32_t node);
void SV_PredOnVanillaCmd(int8_t node);
void SV_PredReceiveCmds(int8_t node, uint32_t seq, uint8_t count, const ticcmd_t *cmds);
void SV_PredPopPlayerCmd(int32_t node, tic_t tic, ticcmd_t *out_cmd);
uint8_t *SV_PredWriteInlineAcks(int32_t node, tic_t realfirsttic, tic_t lasttictosend, uint8_t *bufpos);

// ---- Client-side native protocol helpers ----
void CL_PredResetSession(void);
void CL_PredResetState(void);
void CL_PredReadInlineAcks(tic_t starttic, uint32_t numtics, const uint8_t *data, size_t avail_bytes);
void CL_PredOnSplitscreenFallback(void);
uint32_t CL_PredRecordOutgoingCmd(const ticcmd_t *cmd);
dboolean CL_PredUseEnhancedCmd(void);
void CL_PredBuildCmdPacket(dboolean mis, uint8_t client_tic, uint8_t resendfrom,
	const consistancy_t *cons, size_t *out_packetsize);
void CL_PredOnConfirmedTic(tic_t executed_tic, const ticcmd_t *confirmed_cmd);
void CL_PredOnLocalTic(tic_t realtics);

// ---- Read-only shadow kart physics replay ----
void CL_PredUpdateShadow(void);
void CL_PredSelfTestOnTic(tic_t executed_tic, const ticcmd_t *raw_cmd);

// Engine internal shadow-step entrypoints (called only while P_IsPredicting())
struct sector_t;
struct subsector_t;
struct interpmobjstate_t;
struct camera_t;
void P_PredPushMapState(void);
void P_PredPopMapState(void);
int32_t P_PredCollectTouchingSectors(mobj_t *thing, struct sector_t **out_sectors, int32_t max_sectors);
void P_PredInitTouchingSectorsFromMobj(const mobj_t *base_mo);
void P_PredInitTouchingSectorsFromList(struct sector_t *const *secs, int32_t count);
void P_PredUpdateTouchingSectors(mobj_t *thing);
int32_t P_PredGetTouchingSectors(struct sector_t **out_sectors, int32_t max_sectors);
void P_PredPlayerThink(player_t *player);
void P_PredPlayerMobjThink(mobj_t *mobj);
void P_PredCheckDashPanels(player_t *player);
void P_PredCheckSneakerPanelsAtPos(mobj_t *thing);

// ---- Turn-rate, visual pose, chase camera, attached VFX/items, HUD & sound ----
const player_t *CL_PredGetTurnPlayer(void);
const player_t *CL_PredGetHudPlayer(const player_t *stplyr);
dboolean CL_PredIsDrivingCamera0(void);

void CL_PredUpdateVisualSmoothing(fixed_t deltatics_fixed);
dboolean CL_PredShouldRenderMobj(const mobj_t *mobj);
void CL_PredInterpolateMobjState(fixed_t frac, struct interpmobjstate_t *out);
dboolean CL_PredGetInterpolatedMobjPos(const mobj_t *mobj, fixed_t frac, fixed_t *out_x, fixed_t *out_y, fixed_t *out_z);
void CL_PredGetRenderOffsets(const mobj_t *mobj, fixed_t *sprxoff, fixed_t *spryoff, fixed_t *sprzoff, fixed_t *crash_zoffset);
void CL_PredGetRenderSprite(const mobj_t *mobj, uint8_t *out_sprite2, uint32_t *out_frame);
struct sector_t *CL_PredGetRenderSector(void);
void CL_PredGetSmoothViewOffset(uint8_t viewnum, fixed_t *ox, fixed_t *oy, fixed_t *oz);
dboolean CL_PredGetFirstPersonView(uint8_t viewnum, fixed_t *out_x, fixed_t *out_y, fixed_t *out_z, struct subsector_t **out_subsector);

// Local kart-attached VFX (sparks/dust/trails/flames) & held/orbiting shield items
dboolean CL_PredShouldAttachMobj(const mobj_t *mobj);
void CL_PredTransformAttachedState(const mobj_t *mobj, fixed_t frac, struct interpmobjstate_t *out);
void CL_PredPrepareAttachedFrame(void);
struct sector_t *CL_PredGetAttachedRenderSector(const mobj_t *mobj);
uint32_t CL_PredGetAttachedCount(void);
mobj_t *CL_PredGetAttachedMobj(uint32_t idx, struct sector_t **out_sec);

// Drift spark tier sound cue suppression (plays early on shadow kart, suppresses delayed confirmed duplicate)
dboolean CL_PredConsumeSuppressedDriftSound(const player_t *player, uint8_t newstage);

dboolean CL_PredStepChaseCamera(player_t *player, struct camera_t *thiscam, dboolean resetcalled);

void CL_PredDrawDebugHUD(void);

// Inspection helpers
dboolean CL_PredIsProtoActive(void);
dboolean CL_PredHasShadow(void);
const player_t *CL_PredGetShadowPlayer(void);
const mobj_t *CL_PredGetShadowMobj(void);
uint32_t CL_PredGetQueueLen(void);
uint32_t CL_PredGetPredictedTics(void);
const cl_pred_cmd_t *CL_PredGetQueueEntry(uint32_t idx);
uint32_t CL_PredGetLastSentSeq(void);
uint32_t CL_PredGetConfirmedSeq(void);
fixed_t CL_PredGetLastDeltaXY(void);
fixed_t CL_PredGetLastDeltaZ(void);
angle_t CL_PredGetLastDeltaAngle(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __CL_PREDICT__
