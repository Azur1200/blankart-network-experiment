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
///        and backward-compatible Option B input ACK / repeat protocol.

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
extern consvar_t cv_cl_movement_proto;
extern consvar_t cv_cl_movement_debug;
extern consvar_t cv_cl_movement_selftest;

// Registration (called from D_ClientServerInit)
void CL_PredRegisterVars(void);

// ---- Server-side Option B helpers ----
void SV_PredResetAll(void);
void SV_PredResetNode(int32_t node);
dboolean SV_PredNodeEnabled(int32_t node);
dboolean SV_PredNodeUsesPredCmd(int32_t node);
void SV_PredHandleCap(int8_t node);
void SV_PredOnVanillaCmd(int8_t node);
void SV_PredReceiveCmds(int8_t node, uint32_t seq, uint8_t count, const ticcmd_t *cmds);
void SV_PredPopPlayerCmd(int32_t node, tic_t tic, ticcmd_t *out_cmd);
void SV_PredSendAck(int32_t node, tic_t realfirsttic, tic_t lasttictosend);

// ---- Client-side Option B + Option A helpers ----
void CL_PredResetSession(void);
void CL_PredResetState(void);
void CL_PredTickHandshake(void);
void CL_PredHandleCap(int8_t node);
void CL_PredHandleAck(int8_t node);
void CL_PredOnSplitscreenFallback(void);
uint32_t CL_PredRecordOutgoingCmd(const ticcmd_t *cmd);
dboolean CL_PredUseEnhancedCmd(void);
void CL_PredBuildCmdPacket(dboolean mis, uint8_t client_tic, uint8_t resendfrom,
	const consistancy_t *cons, size_t *out_packetsize);
void CL_PredOnConfirmedTic(tic_t executed_tic, const ticcmd_t *confirmed_cmd);

// ---- Step 3: Read-only shadow kart physics replay ----
void CL_PredUpdateShadow(void);
void CL_PredSelfTestOnTic(tic_t executed_tic, const ticcmd_t *raw_cmd);

// Engine internal shadow-step entrypoints (called only while P_IsPredicting())
struct sector_t;
void P_PredPushMapState(void);
void P_PredPopMapState(void);
int32_t P_PredCollectTouchingSectors(mobj_t *thing, struct sector_t **out_sectors, int32_t max_sectors);
void P_PredPlayerThink(player_t *player);
void P_PredPlayerMobjThink(mobj_t *mobj);

// Inspection helpers (used by Step 4 rendering / camera / HUD)
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
