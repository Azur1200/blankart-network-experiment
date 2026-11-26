// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by Antigravity / BlanKart Contributors.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  cl_predict.c
/// \brief Local-kart client-side movement prediction (DarkPlaces-style)
///        and native input ACK / redundant repeat protocol.

#include "cl_predict.h"
#include "doomstat.h"
#include "d_net.h"
#include "i_net.h"
#include "g_game.h"
#include "p_tick.h"
#include "p_local.h"
#include "p_mobj.h"
#include "k_kart.h"
#include "k_hud.h"
#include "k_bot.h"
#include "m_random.h"
#include "r_main.h"
#include "r_fps.h"
#include "r_state.h"
#include "r_things.h"
#include "w_wad.h"
#include "z_zone.h"
#include "v_video.h"
#include "i_video.h"
#include "i_time.h"
#include "s_sound.h"
#include "console.h"
#include "byteptr.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// Global read-only prediction guard
dboolean cl_pred_active_flag = false;

// =========================================================================
// CVars
// =========================================================================

static CV_PossibleValue_t cl_movement_maxtics_cons_t[] = {{0, "MIN"}, {16, "MAX"}, {0, NULL}};
static CV_PossibleValue_t cl_movement_smoothrate_cons_t[] = {{1, "MIN"}, {100, "MAX"}, {0, NULL}};
static CV_PossibleValue_t cl_netrepeatinput_cons_t[] = {{0, "MIN"}, {CLPRED_MAX_REPEAT, "MAX"}, {0, NULL}};
static CV_PossibleValue_t cl_movement_debug_cons_t[] = {{0, "MIN"}, {2, "MAX"}, {0, NULL}};
static CV_PossibleValue_t cl_movement_selftest_cons_t[] = {{0, "MIN"}, {12, "MAX"}, {0, NULL}};

consvar_t cv_cl_movement = CVAR_INIT("cl_movement", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_cl_movement_maxtics = CVAR_INIT("cl_movement_maxtics", "8", CV_SAVE, cl_movement_maxtics_cons_t, NULL);
consvar_t cv_cl_movement_smooth = CVAR_INIT("cl_movement_smooth", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_cl_movement_smoothrate = CVAR_INIT("cl_movement_smoothrate", "35", CV_SAVE, cl_movement_smoothrate_cons_t, NULL);
consvar_t cv_cl_netrepeatinput = CVAR_INIT("cl_netrepeatinput", "2", CV_SAVE, cl_netrepeatinput_cons_t, NULL);
consvar_t cv_cl_movement_debug = CVAR_INIT("cl_movement_debug", "0", 0, cl_movement_debug_cons_t, NULL);
consvar_t cv_cl_movement_selftest = CVAR_INIT("cl_movement_selftest", "0", 0, cl_movement_selftest_cons_t, NULL);
consvar_t cv_cl_newcollision = CVAR_INIT("cl_newcollision", "On", CV_NETVAR, CV_OnOff, NULL);
consvar_t cv_cl_movement_extrapitems = CVAR_INIT("cl_movement_extrapitems", "On", CV_SAVE, CV_OnOff, NULL);

// =========================================================================
// Sequence helpers (0 is reserved as invalid/unset)
// =========================================================================

static inline uint32_t SeqNext(uint32_t seq)
{
	seq++;
	if (seq == 0)
		seq = 1;
	return seq;
}

static inline uint32_t SeqPrev(uint32_t seq, uint32_t steps)
{
	uint32_t i;
	for (i = 0; i < steps; i++)
		seq = (seq <= 1) ? UINT32_MAX : (seq - 1);
	return seq;
}

// Returns true if sequence number a is strictly newer than b (wrap-safe).
static inline dboolean SeqAfter(uint32_t a, uint32_t b)
{
	if (a == 0)
		return false;
	if (b == 0)
		return true;
	return (int32_t)(a - b) > 0;
}

static inline dboolean SeqAtLeast(uint32_t a, uint32_t b)
{
	return (a != 0 && (a == b || SeqAfter(a, b)));
}

// =========================================================================
// Server-side per-node native input queue & ACK state
// =========================================================================

#define CLPRED_SV_QUEUE_CAP      8
#define CLPRED_SV_MAX_QUEUE      5
#define CLPRED_SV_DRAIN_TICS     12

typedef struct sv_pred_entry_s
{
	uint32_t seq;
	ticcmd_t cmd;
} sv_pred_entry_t;

typedef struct sv_pred_node_s
{
	dboolean enabled;
	dboolean used_pred_cmd;
	dboolean last_underrun;

	uint32_t last_recv_seq;
	uint32_t last_exec_seq;

	sv_pred_entry_t queue[CLPRED_SV_QUEUE_CAP];
	uint8_t queue_len;
	uint8_t high_queue_tics;

	tic_t tic_ack_tic[BACKUPTICS];
	uint32_t tic_ack_seq[BACKUPTICS];
	dboolean tic_ack_valid[BACKUPTICS];

	// Telemetry
	uint32_t packets_recv;
	uint32_t cmds_queued;
	uint32_t cmds_recovered;
	uint32_t underruns;
	uint32_t trimmed;
} sv_pred_node_t;

static sv_pred_node_t sv_pred[MAXNETNODES];

void SV_PredResetNode(int32_t node)
{
	if (node < 0 || node >= MAXNETNODES)
		return;
	memset(&sv_pred[node], 0, sizeof(sv_pred[node]));
}

void SV_PredResetAll(void)
{
	memset(sv_pred, 0, sizeof(sv_pred));
}

dboolean SV_PredNodeEnabled(int32_t node)
{
	return (node > 0 && node < MAXNETNODES && sv_pred[node].enabled);
}

dboolean SV_PredNodeUsesPredCmd(int32_t node)
{
	return (node > 0 && node < MAXNETNODES && sv_pred[node].enabled && sv_pred[node].used_pred_cmd);
}

void SV_PredOnVanillaCmd(int8_t node)
{
	// Used when a client falls back to PT_CLIENT2CMD..PT_CLIENT4CMD for splitscreen
	if (node <= 0 || node >= MAXNETNODES || !sv_pred[node].enabled)
		return;

	sv_pred[node].used_pred_cmd = false;
	sv_pred[node].queue_len = 0;
	sv_pred[node].high_queue_tics = 0;
}

static void SV_PredQueueShiftOldest(sv_pred_node_t *pn)
{
	if (pn->queue_len == 0)
		return;
	if (pn->queue_len > 1)
		memmove(&pn->queue[0], &pn->queue[1], (pn->queue_len - 1) * sizeof(pn->queue[0]));
	pn->queue_len--;
}

static dboolean SV_PredQueueInsert(sv_pred_node_t *pn, uint32_t seq, const ticcmd_t *cmd)
{
	uint8_t i, pos;

	for (i = 0; i < pn->queue_len; i++)
	{
		if (pn->queue[i].seq == seq)
			return false; // already queued
	}

	if (pn->queue_len >= CLPRED_SV_QUEUE_CAP)
	{
		SV_PredQueueShiftOldest(pn);
		pn->trimmed++;
	}

	// Keep queue sorted in ascending seq order
	pos = pn->queue_len;
	while (pos > 0 && SeqAfter(pn->queue[pos - 1].seq, seq))
	{
		pn->queue[pos] = pn->queue[pos - 1];
		pos--;
	}

	pn->queue[pos].seq = seq;
	pn->queue[pos].cmd = *cmd;
	pn->queue[pos].cmd.flags |= TICCMD_RECEIVED;
	pn->queue_len++;
	return true;
}

void SV_PredReceiveCmds(int8_t node, uint32_t seq, uint8_t count, const ticcmd_t *cmds)
{
	sv_pred_node_t *pn;
	int32_t k;

	if (node <= 0 || node >= MAXNETNODES || !cmds || count == 0 || seq == 0)
		return;

	if (count > CLPRED_MAX_CMDS)
		count = CLPRED_MAX_CMDS;

	pn = &sv_pred[node];
	if (!pn->enabled)
	{
		SV_PredResetNode(node);
		pn->enabled = true;
	}

	pn->used_pred_cmd = true;
	pn->packets_recv++;

	// Insert oldest-to-newest so seq order is preserved and redundant copies recover drops.
	for (k = (int32_t)count - 1; k >= 0; k--)
	{
		uint32_t s = SeqPrev(seq, (uint32_t)k);

		if (pn->last_exec_seq != 0 && !SeqAfter(s, pn->last_exec_seq))
			continue; // Already executed on a previous server tic

		if (SV_PredQueueInsert(pn, s, &cmds[k]))
		{
			pn->cmds_queued++;
			if (k > 0)
				pn->cmds_recovered++;
		}
	}

	if (SeqAfter(seq, pn->last_recv_seq))
		pn->last_recv_seq = seq;

	// Prevent runaway queue buildup from bursts
	while (pn->queue_len > CLPRED_SV_MAX_QUEUE)
	{
		SV_PredQueueShiftOldest(pn);
		pn->trimmed++;
	}
}

void SV_PredPopPlayerCmd(int32_t node, tic_t tic, ticcmd_t *out_cmd)
{
	sv_pred_node_t *pn;
	uint32_t slot;

	if (node <= 0 || node >= MAXNETNODES || !out_cmd)
		return;

	pn = &sv_pred[node];
	if (!pn->enabled || !pn->used_pred_cmd)
		return;

	if (pn->queue_len > 0)
	{
		// Maintain a 1-tic post-pop jitter/loss cushion (queue_len == 2 before pop)
		// so +-14ms jitter or single dropped packets recovered via cl_netrepeatinput
		// cause zero underruns and zero reconciliations. Hard ceiling: 2 post-pop tics.
		while (pn->queue_len > 3)
		{
			SV_PredQueueShiftOldest(pn);
			pn->trimmed++;
		}

		if (pn->queue_len >= 3)
		{
			pn->high_queue_tics++;
			if (pn->high_queue_tics >= CLPRED_SV_DRAIN_TICS)
			{
				SV_PredQueueShiftOldest(pn);
				pn->trimmed++;
				pn->high_queue_tics = 0;
			}
		}
		else
		{
			pn->high_queue_tics = 0;
		}

		*out_cmd = pn->queue[0].cmd;
		out_cmd->flags |= TICCMD_RECEIVED;
		pn->last_exec_seq = pn->queue[0].seq;
		pn->last_underrun = false;
		SV_PredQueueShiftOldest(pn);
	}
	else
	{
		// True underrun: hold previous controls, mark not received
		pn->high_queue_tics = 0;
		out_cmd->flags &= ~TICCMD_RECEIVED;
		pn->last_underrun = true;
		pn->underruns++;
	}

	slot = tic % BACKUPTICS;
	pn->tic_ack_tic[slot] = tic;
	pn->tic_ack_seq[slot] = pn->last_exec_seq;
	pn->tic_ack_valid[slot] = true;

	if (cv_cl_movement_debug.value >= 2 && (tic % TICRATE) == 0)
	{
		CONS_Printf("[SV_PRED] node=%d tic=%u exec_seq=%u recv_seq=%u qlen=%u pkts=%u queued=%u recov=%u under=%u trim=%u\n",
			node, tic, pn->last_exec_seq, pn->last_recv_seq, pn->queue_len,
			pn->packets_recv, pn->cmds_queued, pn->cmds_recovered, pn->underruns, pn->trimmed);
	}
}

uint8_t *SV_PredWriteInlineAcks(int32_t node, tic_t realfirsttic, tic_t lasttictosend, uint8_t *bufpos)
{
	sv_pred_node_t *pn;
	tic_t t;
	uint8_t flags;

	if (node <= 0 || node >= MAXNETNODES || lasttictosend <= realfirsttic || !bufpos)
		return bufpos;

	pn = &sv_pred[node];
	if (!pn->enabled || !pn->used_pred_cmd)
		return bufpos;

	flags = pn->last_underrun ? CLPRED_ACKF_UNDERRUN : 0;
	WRITEUINT8(bufpos, flags);

	for (t = realfirsttic; t < lasttictosend; t++)
	{
		uint32_t slot = t % BACKUPTICS;
		uint32_t seq;

		if (pn->tic_ack_valid[slot] && pn->tic_ack_tic[slot] == t)
			seq = pn->tic_ack_seq[slot];
		else
			seq = pn->last_exec_seq;

		WRITEUINT32(bufpos, seq);
	}

	return bufpos;
}

// =========================================================================
// Client-side prediction & native ACK state
// =========================================================================

#define CLPRED_MAX_ATTACHED 64
#define CLPRED_SNAP_THRESHOLD (256 * FRACUNIT)

typedef struct cl_pred_tx_s
{
	uint32_t seq;
	ticcmd_t cmd;
} cl_pred_tx_t;

typedef struct cl_pred_pose_s
{
	dboolean valid;
	fixed_t x, y, z;
	fixed_t scale, radius, height;
	fixed_t spritexscale, spriteyscale, spritexoffset, spriteyoffset;
	fixed_t sprxoff, spryoff, sprzoff;
	fixed_t crash_zoffset;
	angle_t angle;
	angle_t drawangle;
	angle_t pitch, roll, slopepitch, sloperoll;
	uint8_t sprite2;
	uint32_t frame;
	subsector_t *subsector;
} cl_pred_pose_t;

typedef struct cl_pred_attached_s
{
	mobj_t *mo;
	sector_t *vis_sec;
} cl_pred_attached_t;

typedef struct cl_pred_state_s
{
	uint32_t next_seq;
	uint32_t proto_start_seq;
	uint32_t last_sent_seq;
	uint32_t confirmed_seq;
	tic_t last_leveltime;

	// Transmit history (newest at index 0) for redundant input repeat
	cl_pred_tx_t tx_history[CLPRED_MAX_CMDS];
	uint8_t tx_count;

	// Unconfirmed local commands awaiting server confirmation (oldest at index 0)
	cl_pred_cmd_t queue[CLPRED_QUEUE_CAP];
	uint32_t queue_len;

	// Exact server-tic -> ack_seq ring populated by PT_PREDACK
	tic_t tic_ack_tic[BACKUPTICS];
	uint32_t tic_ack_seq[BACKUPTICS];
	dboolean tic_ack_valid[BACKUPTICS];

	// Telemetry
	uint32_t ack_packets_recv;
	uint32_t exact_ack_ticks;
	uint32_t missing_ack_ticks;
	uint32_t queue_overflows;
	uint32_t max_in_flight;
	uint32_t server_underrun_flags;

	// Read-only shadow kart state
	player_t shadow_player;
	mobj_t shadow_mo;
	dboolean shadow_valid;
	uint32_t predicted_tics;
	fixed_t last_delta_xy;
	fixed_t last_delta_z;
	angle_t last_delta_angle;
	uint32_t shadow_replays;
	uint32_t rng_violations;

	// Visual pose, sub-tic lerp endpoints, smooth reconciliation
	cl_pred_pose_t pose_old;
	cl_pred_pose_t pose_new;
	uint32_t last_head_seq;

	fixed_t smooth_err_x;
	fixed_t smooth_err_y;
	fixed_t smooth_err_z;
	int32_t smooth_err_angle;
	int32_t smooth_err_drawangle;

	uint32_t smooth_reconciles;
	uint32_t smooth_snaps;
	fixed_t max_reconcile_xy;

	// Instant drift spark tier audio cue state
	uint8_t drift_sound_stage;
	uint8_t drift_sound_suppress_stage;
	tic_t drift_sound_suppress_until;

	// Instant wall-bonk audio + 3D visual burst state (suppresses late confirmed MT_BUMP behind kart)
	uint16_t prev_shadow_bumpunstuck;
	tic_t wall_bump_suppress_until;
	uint8_t bump_fx_tics;
	fixed_t bump_fx_x;
	fixed_t bump_fx_y;
	fixed_t bump_fx_z;
	fixed_t bump_fx_scale;

	// Instant sneaker / dash-panel boost sound cue state
	tic_t prev_shadow_sneakertimer;
	tic_t sneaker_sound_suppress_until;

	// Instant lap / finish-line sound cue state
	uint8_t prev_shadow_laps;
	uint8_t lap_sound_suppress_lap;
	tic_t lap_sound_suppress_until;

	// Extrapolated item impact cue state
	dboolean shadow_item_hit;
	dboolean prev_shadow_item_hit;
	tic_t item_hit_suppress_until;

	// Per-frame cache of attached VFX & held/orbiting items for cross-sector rendering
	cl_pred_attached_t attached[CLPRED_MAX_ATTACHED];
	uint32_t attached_count;

	// Nearby moving offensive items extrapolated by RTT (cl_movement_extrapitems)
#define CLPRED_MAX_EXTRAP_ITEMS 32
#define CLPRED_EXTRAP_STEPS     12
	struct
	{
		mobj_t *mo;
		fixed_t old_x, old_y, old_z;
		fixed_t new_x, new_y, new_z;
		fixed_t step_x[CLPRED_EXTRAP_STEPS];
		fixed_t step_y[CLPRED_EXTRAP_STEPS];
		fixed_t step_z[CLPRED_EXTRAP_STEPS];
		uint32_t step_count;
		sector_t *vis_sec;
	} extrap_items[CLPRED_MAX_EXTRAP_ITEMS];
	uint32_t extrap_count;

	// Nearby static hazards (Bananas / Mines) cached alongside extrap_items for O(1) shadow checks
	mobj_t *near_hazards[CLPRED_MAX_EXTRAP_ITEMS];
	uint32_t near_hazard_count;
} cl_pred_state_t;

static cl_pred_state_t cl_pred;

static dboolean CL_PredIsPlayerEligible(const player_t *p);
static void CL_PredRefreshShadowPhysics(void);
static void CL_PredUpdateExtrapItems(void);
static dboolean CL_PredCheckShadowItemImpact(player_t *player, uint32_t step_idx);

static void CL_PredResetVisualState(void)
{
	cl_pred.shadow_valid = false;
	cl_pred.predicted_tics = 0;
	cl_pred.last_delta_xy = 0;
	cl_pred.last_delta_z = 0;
	cl_pred.last_delta_angle = 0;
	cl_pred.pose_old.valid = false;
	cl_pred.pose_new.valid = false;
	cl_pred.last_head_seq = 0;
	cl_pred.smooth_err_x = 0;
	cl_pred.smooth_err_y = 0;
	cl_pred.smooth_err_z = 0;
	cl_pred.smooth_err_angle = 0;
	cl_pred.smooth_err_drawangle = 0;
	cl_pred.drift_sound_stage = 0;
	cl_pred.drift_sound_suppress_stage = 0;
	cl_pred.drift_sound_suppress_until = 0;
	cl_pred.prev_shadow_bumpunstuck = 0;
	cl_pred.wall_bump_suppress_until = 0;
	cl_pred.bump_fx_tics = 0;
	cl_pred.prev_shadow_sneakertimer = 0;
	cl_pred.sneaker_sound_suppress_until = 0;
	cl_pred.prev_shadow_laps = (playeringame[consoleplayer] ? players[consoleplayer].laps : 0);
	cl_pred.lap_sound_suppress_lap = 0;
	cl_pred.lap_sound_suppress_until = 0;
	cl_pred.shadow_item_hit = false;
	cl_pred.prev_shadow_item_hit = false;
	cl_pred.item_hit_suppress_until = 0;
	cl_pred.attached_count = 0;
	cl_pred.extrap_count = 0;
	cl_pred.near_hazard_count = 0;
}

void CL_PredResetSession(void)
{
	memset(&cl_pred, 0, sizeof(cl_pred));
	cl_pred.next_seq = 1;
	cl_pred.proto_start_seq = 1;
}

void CL_PredResetState(void)
{
	cl_pred.queue_len = 0;
	cl_pred.last_leveltime = 0;
	CL_PredResetVisualState();
}

void CL_PredReadInlineAcks(tic_t starttic, uint32_t numtics, const uint8_t *data, size_t avail_bytes)
{
	uint8_t flags;
	uint32_t k;
	const uint8_t *p = data;

	if (!client || !data || numtics == 0)
		return;

	if (avail_bytes < 1 + (size_t)numtics * sizeof(uint32_t))
		return;

	flags = READUINT8(p);

	for (k = 0; k < numtics; k++)
	{
		tic_t t = starttic + k;
		uint32_t slot = t % BACKUPTICS;
		cl_pred.tic_ack_tic[slot] = t;
		cl_pred.tic_ack_seq[slot] = READUINT32(p);
		cl_pred.tic_ack_valid[slot] = true;
	}

	cl_pred.ack_packets_recv++;
	if (flags & CLPRED_ACKF_UNDERRUN)
		cl_pred.server_underrun_flags++;
}

void CL_PredOnSplitscreenFallback(void)
{
	cl_pred.proto_start_seq = cl_pred.next_seq;
}

uint32_t CL_PredRecordOutgoingCmd(const ticcmd_t *cmd)
{
	uint32_t seq;
	int32_t i;

	if (!cmd)
		return 0;

	if (cl_pred.next_seq == 0)
	{
		cl_pred.next_seq = 1;
		cl_pred.proto_start_seq = 1;
	}

	seq = cl_pred.next_seq;
	cl_pred.next_seq = SeqNext(cl_pred.next_seq);
	cl_pred.last_sent_seq = seq;

	// Push into transmit history (index 0 = newest)
	for (i = CLPRED_MAX_CMDS - 1; i > 0; i--)
		cl_pred.tx_history[i] = cl_pred.tx_history[i - 1];
	cl_pred.tx_history[0].seq = seq;
	cl_pred.tx_history[0].cmd = *cmd;
	if (cl_pred.tx_count < CLPRED_MAX_CMDS)
		cl_pred.tx_count++;

	// Push into prediction queue during active gameplay.
	// Only refresh shadow physics here (NOT visual pose_old/pose_new or camera[0]),
	// so mid-frame NetUpdate() calls (e.g. inside HWR_RenderPlayerView) never jitter rendering,
	// while G_DoCameraTurn always reads the latest shadow state even across multi-tic catch-up.
	if (gamestate == GS_LEVEL && addedtogame && playeringame[consoleplayer] && leveltime >= 1)
	{
		if (cl_pred.queue_len >= CLPRED_QUEUE_CAP)
		{
			memmove(&cl_pred.queue[0], &cl_pred.queue[1], (CLPRED_QUEUE_CAP - 1) * sizeof(cl_pred.queue[0]));
			cl_pred.queue_len = CLPRED_QUEUE_CAP - 1;
			cl_pred.queue_overflows++;
		}

		cl_pred.queue[cl_pred.queue_len].seq = seq;
		cl_pred.queue[cl_pred.queue_len].client_leveltime = leveltime;
		cl_pred.queue[cl_pred.queue_len].cmd = *cmd;
		cl_pred.queue_len++;

		if (cl_pred.queue_len > cl_pred.max_in_flight)
			cl_pred.max_in_flight = cl_pred.queue_len;

		CL_PredRefreshShadowPhysics();
	}

	return seq;
}

dboolean CL_PredUseEnhancedCmd(void)
{
	return (!splitscreen && cl_pred.tx_count > 0);
}

void CL_PredBuildCmdPacket(dboolean mis, uint8_t client_tic, uint8_t resendfrom,
	const consistancy_t *cons, size_t *out_packetsize)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	uint8_t want_repeat, max_cmds, count, k;
	uint32_t head_seq;

	netbuffer->packettype = (mis ? PT_CLIENTMIS_PRED : PT_CLIENTCMD_PRED);
	netbuffer->u.clientpredpak.client_tic = client_tic;
	netbuffer->u.clientpredpak.resendfrom = resendfrom;
	if (cons)
		netbuffer->u.clientpredpak.consistancy = *cons;
	else
		memset(&netbuffer->u.clientpredpak.consistancy, 0, sizeof(consistancy_t));

	want_repeat = (uint8_t)CLAMP(cv_cl_netrepeatinput.value, 0, (int32_t)CLPRED_MAX_REPEAT);
	max_cmds = 1 + want_repeat;
	if (max_cmds > cl_pred.tx_count)
		max_cmds = cl_pred.tx_count;
	if (max_cmds > CLPRED_MAX_CMDS)
		max_cmds = CLPRED_MAX_CMDS;

	head_seq = cl_pred.tx_history[0].seq;
	count = 0;

	for (k = 0; k < max_cmds; k++)
	{
		uint32_t expected_seq = SeqPrev(head_seq, k);
		if (cl_pred.tx_history[k].seq != expected_seq)
			break;
		if (cl_pred.proto_start_seq != 0 && !SeqAtLeast(expected_seq, cl_pred.proto_start_seq))
			break;
		G_MoveTiccmd(&netbuffer->u.clientpredpak.cmds[k], &cl_pred.tx_history[k].cmd, 1);
		count++;
	}

	if (count == 0)
	{
		G_MoveTiccmd(&netbuffer->u.clientpredpak.cmds[0], &cl_pred.tx_history[0].cmd, 1);
		count = 1;
	}

	netbuffer->u.clientpredpak.seq = LSBF_LONG(head_seq);
	netbuffer->u.clientpredpak.count = count;
	netbuffer->u.clientpredpak.reserved = 0;

	if (out_packetsize)
		*out_packetsize = offsetof(clientcmd_pred_pak, cmds) + (size_t)count * sizeof(ticcmd_t);
}

static void CL_PredPruneUpToSeq(uint32_t ack_seq)
{
	uint32_t keep_from = 0;

	if (ack_seq == 0 || cl_pred.queue_len == 0)
		return;

	while (keep_from < cl_pred.queue_len && !SeqAfter(cl_pred.queue[keep_from].seq, ack_seq))
		keep_from++;

	if (keep_from > 0)
	{
		uint32_t remaining = cl_pred.queue_len - keep_from;
		if (remaining > 0)
			memmove(&cl_pred.queue[0], &cl_pred.queue[keep_from], remaining * sizeof(cl_pred.queue[0]));
		cl_pred.queue_len = remaining;
	}
}

// =========================================================================
// Read-only shadow kart physics replay
// =========================================================================

static dboolean CL_PredIsPlayerEligible(const player_t *p)
{
	if (gamestate != GS_LEVEL || !p || !p->mo || P_MobjWasRemoved(p->mo))
		return false;
	if (p->playerstate != PST_LIVE || p->mo->health <= 0)
		return false;
	if (p->spectator || p->exiting || mapreset)
		return false;
	if (p->respawn != 0)
		return false;
	if (p->carry == CR_ZOOMTUBE || p->loop.radius != 0)
		return false;
	if (p->dashRingPullTics != 0 || p->dashRingPushTics != 0)
		return false;
	if (p->bot || K_PlayerUsesBotMovement(p))
		return false;
	if (!p->mo->subsector || !p->mo->subsector->sector)
		return false;
	return true;
}

static void CL_PredDetachShadowMobj(mobj_t *mo, player_t *owner)
{
	mo->snext = NULL;
	mo->sprev = NULL;
	mo->blocknode = NULL;
	mo->touching_sectorlist = NULL;
	mo->thinker.prev = NULL;
	mo->thinker.next = NULL;
	mo->thinker.references = 0;
	mo->target = NULL;
	mo->tracer = NULL;
	mo->hnext = NULL;
	mo->hprev = NULL;
	mo->itnext = NULL;
	mo->bubbleplow = NULL;
	mo->player = owner;
}

static void CL_PredReplayOnConsolePlayer(
	const player_t *base_player,
	const mobj_t *base_mo,
	sector_t *const *init_secs,
	int32_t init_sec_count,
	tic_t start_leveltime,
	const ticcmd_t *cmds,
	uint32_t count,
	dboolean allow_dynamic_extrap,
	player_t *out_player,
	mobj_t *out_mo,
	dboolean *out_hit_item)
{
	player_t saved_player;
	mobj_t work_mo;
	tic_t saved_leveltime;
	uint32_t saved_rng[PRNUMCLASS];
	uint32_t k;
	int32_t c;
	dboolean hit_item = false;

	I_Assert(!cl_pred_active_flag);
	I_Assert(base_player != NULL && base_mo != NULL && out_player != NULL && out_mo != NULL);

	saved_player = players[consoleplayer];
	saved_leveltime = leveltime;
	for (c = 0; c < PRNUMCLASS; c++)
		saved_rng[c] = P_GetRandSeed((pr_class_t)c);

	cl_pred_active_flag = true;
	P_PredPushMapState();

	if (init_secs != NULL && init_sec_count > 0)
		P_PredInitTouchingSectorsFromList(init_secs, init_sec_count);
	else
		P_PredInitTouchingSectorsFromMobj(base_mo);

	// Run directly on players[consoleplayer] so any (player - players) index
	// arithmetic inside kart helpers resolves to consoleplayer cleanly.
	players[consoleplayer] = *base_player;
	work_mo = *base_mo;
	CL_PredDetachShadowMobj(&work_mo, &players[consoleplayer]);
	players[consoleplayer].mo = &work_mo;

	for (k = 0; k < count; k++)
	{
		ticcmd_t *pcmd = &players[consoleplayer].cmd;
		fixed_t prev_x = work_mo.x, prev_y = work_mo.y;
		leveltime = start_leveltime + k;

		*pcmd = cmds[k];
		pcmd->latency = min(((leveltime & TICCMD_LATENCYMASK) - pcmd->latency) & TICCMD_LATENCYMASK, MAXPREDICTTICS - 1);

		P_PredClearCrossedFinishLines();
		P_PredPlayerThink(&players[consoleplayer]);
		P_PredPlayerMobjThink(&work_mo);

		if (allow_dynamic_extrap)
		{
			K_PredApplyNewKartCollision(&players[consoleplayer], k);
			if (CL_PredCheckShadowItemImpact(&players[consoleplayer], k))
				hit_item = true;
		}

		// Mirror P_PlayerAfterThink physics tail (SECTORSPECIALSAFTERTHINK order)
		if (P_IsObjectOnGround(&work_mo))
		{
			players[consoleplayer].outrun = 0;
			work_mo.pmomz = 0;
		}
		if (players[consoleplayer].onconveyor != 1 || !P_IsObjectOnGround(&work_mo))
		{
			players[consoleplayer].onconveyor = 0;
		}

		// Read-only static ground dash panel & finish-line prediction
		P_PredCheckDashPanels(&players[consoleplayer]);
		P_PredCheckFinishLine(&players[consoleplayer], prev_x, prev_y);

		players[consoleplayer].oldcmd = *pcmd;
	}

	*out_player = players[consoleplayer];
	*out_mo = work_mo;
	out_player->mo = out_mo;
	out_mo->player = out_player;
	if (out_hit_item)
		*out_hit_item = hit_item;

	P_PredPopMapState();
	cl_pred_active_flag = false;

	// Restore authoritative player state and leveltime bit-for-bit
	players[consoleplayer] = saved_player;
	leveltime = saved_leveltime;

	// Verify zero RNG side effects
	for (c = 0; c < PRNUMCLASS; c++)
	{
		uint32_t cur = P_GetRandSeed((pr_class_t)c);
		if (cur != saved_rng[c])
		{
			cl_pred.rng_violations++;
			P_SetRandSeed((pr_class_t)c, saved_rng[c]);
			CONS_Alert(CONS_WARNING, "[CL_PRED] RNG class %s mutated during prediction (%u -> %u)\n",
				pr_class_name[c], saved_rng[c], cur);
#ifdef PARANOIA
			I_Error("[CL_PRED] RNG class %s mutated during prediction!", pr_class_name[c]);
#endif
		}
	}
}

static void CL_PredCapturePose(const player_t *p, const mobj_t *mo, cl_pred_pose_t *out)
{
	out->valid = true;
	out->x = mo->x;
	out->y = mo->y;
	out->z = mo->z;
	out->scale = mo->scale;
	out->radius = mo->radius;
	out->height = mo->height;
	out->spritexscale = mo->spritexscale;
	out->spriteyscale = mo->spriteyscale;
	out->spritexoffset = mo->spritexoffset;
	out->spriteyoffset = mo->spriteyoffset;
	out->sprxoff = mo->sprxoff;
	out->spryoff = mo->spryoff;
	out->sprzoff = mo->sprzoff;
	out->crash_zoffset = p->crash_zoffset;
	out->angle = mo->angle;
	out->drawangle = p->drawangle;
	out->pitch = mo->pitch;
	out->roll = mo->roll;
	out->slopepitch = mo->slopepitch;
	out->sloperoll = mo->sloperoll;
	out->sprite2 = mo->sprite2;
	out->frame = mo->frame;
	out->subsector = mo->subsector;
}

// Replays shadow_player / shadow_mo from confirmed state + current queue.
// Does NOT mutate pose_old / pose_new or camera[0].
static void CL_PredRefreshShadowPhysics(void)
{
	const player_t *p;
	uint32_t maxtics, n, i;
	ticcmd_t cmds[CLPRED_QUEUE_CAP];

	if (!client || !netgame || demo.playback || splitscreen
		|| !cv_cl_movement.value || cv_cl_movement_maxtics.value <= 0
		|| !addedtogame || !playeringame[consoleplayer])
	{
		CL_PredResetVisualState();
		return;
	}

	p = &players[consoleplayer];
	if (!CL_PredIsPlayerEligible(p))
	{
		CL_PredResetVisualState();
		return;
	}

	maxtics = (uint32_t)cv_cl_movement_maxtics.value;
	n = min(cl_pred.queue_len, maxtics);
	if (n > CLPRED_QUEUE_CAP)
		n = CLPRED_QUEUE_CAP;

	if (n == 0)
	{
		cl_pred.shadow_player = *p;
		cl_pred.shadow_mo = *p->mo;
		CL_PredDetachShadowMobj(&cl_pred.shadow_mo, &cl_pred.shadow_player);
		cl_pred.shadow_player.mo = &cl_pred.shadow_mo;
		cl_pred.shadow_valid = true;
		cl_pred.shadow_item_hit = false;
		cl_pred.predicted_tics = 0;
		cl_pred.last_delta_xy = 0;
		cl_pred.last_delta_z = 0;
		cl_pred.last_delta_angle = 0;
		return;
	}

	for (i = 0; i < n; i++)
		cmds[i] = cl_pred.queue[i].cmd;

	CL_PredReplayOnConsolePlayer(p, p->mo, NULL, 0, leveltime, cmds, n, true,
		&cl_pred.shadow_player, &cl_pred.shadow_mo, &cl_pred.shadow_item_hit);

	cl_pred.shadow_valid = true;
	cl_pred.predicted_tics = n;
	cl_pred.shadow_replays++;
	cl_pred.last_delta_xy = R_PointToDist2(cl_pred.shadow_mo.x, cl_pred.shadow_mo.y, p->mo->x, p->mo->y);
	cl_pred.last_delta_z = abs(cl_pred.shadow_mo.z - p->mo->z);
	cl_pred.last_delta_angle = (angle_t)abs((int32_t)(cl_pred.shadow_mo.angle - p->mo->angle));
}

// Reconcile the currently rendered visual pose (at horizon cl_pred.last_head_seq)
// against freshly confirmed authoritative state without advancing the visual tick.
static void CL_PredReconcileAtCurrentHorizon(const player_t *p)
{
	uint32_t maxtics, recon_n = 0, i;
	ticcmd_t cmds[CLPRED_QUEUE_CAP];
	player_t recon_p;
	mobj_t recon_mo;
	cl_pred_pose_t recon_pose;
	dboolean force_snap;

	if (!cl_pred.pose_new.valid || !cv_cl_movement.value || cv_cl_movement_maxtics.value <= 0)
		return;

	maxtics = (uint32_t)cv_cl_movement_maxtics.value;
	if (maxtics > CLPRED_QUEUE_CAP)
		maxtics = CLPRED_QUEUE_CAP;

	// Replay ONLY queued commands up to cl_pred.last_head_seq so we compare apples-to-apples
	// at the exact same timeline point that pose_new represents (excluding any brand-new
	// command queued by NetUpdate() earlier in this frame).
	if (cl_pred.last_head_seq != 0)
	{
		for (i = 0; i < cl_pred.queue_len && recon_n < maxtics; i++)
		{
			if (SeqAfter(cl_pred.queue[i].seq, cl_pred.last_head_seq))
				break;
			cmds[recon_n++] = cl_pred.queue[i].cmd;
		}
	}

	if (recon_n == 0)
	{
		recon_p = *p;
		recon_mo = *p->mo;
		CL_PredDetachShadowMobj(&recon_mo, &recon_p);
		recon_p.mo = &recon_mo;
	}
	else
	{
		CL_PredReplayOnConsolePlayer(p, p->mo, NULL, 0, leveltime, cmds, recon_n, true, &recon_p, &recon_mo, NULL);
	}

	CL_PredCapturePose(&recon_p, &recon_mo, &recon_pose);

	force_snap = (!cv_cl_movement_smooth.value
		|| p->mo->resetinterp
		|| P_PlayerInPain(p));

	if (force_snap)
	{
		cl_pred.pose_old = recon_pose;
		cl_pred.pose_new = recon_pose;
		cl_pred.smooth_err_x = 0;
		cl_pred.smooth_err_y = 0;
		cl_pred.smooth_err_z = 0;
		cl_pred.smooth_err_angle = 0;
		cl_pred.smooth_err_drawangle = 0;
		cl_pred.smooth_snaps++;
		return;
	}

	{
		fixed_t dx = cl_pred.pose_new.x - recon_pose.x;
		fixed_t dy = cl_pred.pose_new.y - recon_pose.y;
		fixed_t dz = cl_pred.pose_new.z - recon_pose.z;
		int32_t dang = (int32_t)(cl_pred.pose_new.angle - recon_pose.angle);
		int32_t ddang = (int32_t)(cl_pred.pose_new.drawangle - recon_pose.drawangle);

		if (dx || dy || dz || dang || ddang)
		{
			fixed_t err_xy = R_PointToDist2(dx, dy, 0, 0);
			if (err_xy > cl_pred.max_reconcile_xy)
				cl_pred.max_reconcile_xy = err_xy;

			if (err_xy > CLPRED_SNAP_THRESHOLD || abs(dz) > CLPRED_SNAP_THRESHOLD)
			{
				cl_pred.pose_old = recon_pose;
				cl_pred.pose_new = recon_pose;
				cl_pred.smooth_err_x = 0;
				cl_pred.smooth_err_y = 0;
				cl_pred.smooth_err_z = 0;
				cl_pred.smooth_err_angle = 0;
				cl_pred.smooth_err_drawangle = 0;
				cl_pred.smooth_snaps++;
				R_ResetViewInterpolation(1);
			}
			else
			{
				// Shift lerp endpoints and camera into reconciled reference frame,
				// placing the exact opposite delta into smooth_err for C0-continuous decay.
				cl_pred.pose_old.x -= dx;
				cl_pred.pose_old.y -= dy;
				cl_pred.pose_old.z -= dz;
				cl_pred.pose_old.angle -= (angle_t)dang;
				cl_pred.pose_old.drawangle -= (angle_t)ddang;

				if (displayplayers[0] == consoleplayer && camera[0].chase)
				{
					camera[0].x -= dx;
					camera[0].y -= dy;
					camera[0].z -= dz;
					camera[0].old_x -= dx;
					camera[0].old_y -= dy;
					camera[0].old_z -= dz;
					R_PredShiftViewInterpolation(0, -dx, -dy, -dz);
				}

				cl_pred.pose_new = recon_pose;

				cl_pred.smooth_err_x = CLAMP(cl_pred.smooth_err_x + dx, -CLPRED_SNAP_THRESHOLD, CLPRED_SNAP_THRESHOLD);
				cl_pred.smooth_err_y = CLAMP(cl_pred.smooth_err_y + dy, -CLPRED_SNAP_THRESHOLD, CLPRED_SNAP_THRESHOLD);
				cl_pred.smooth_err_z = CLAMP(cl_pred.smooth_err_z + dz, -CLPRED_SNAP_THRESHOLD, CLPRED_SNAP_THRESHOLD);
				cl_pred.smooth_err_angle += dang;
				cl_pred.smooth_err_drawangle += ddang;
				cl_pred.smooth_reconciles++;
			}
		}
		else
		{
			cl_pred.pose_new = recon_pose;
		}
	}
}

void CL_PredUpdateShadow(void)
{
	CL_PredRefreshShadowPhysics();
}

void CL_PredOnConfirmedTic(tic_t executed_tic, const ticcmd_t *confirmed_cmd)
{
	uint32_t slot;
	const player_t *p;

	(void)confirmed_cmd;

	if (!client || !netgame || demo.playback)
		return;

	if (gamestate != GS_LEVEL || !addedtogame || !playeringame[consoleplayer]
		|| leveltime == 0 || leveltime < cl_pred.last_leveltime)
	{
		CL_PredResetState();
		cl_pred.last_leveltime = (gamestate == GS_LEVEL) ? leveltime : 0;
		return;
	}

	cl_pred.last_leveltime = leveltime;
	slot = executed_tic % BACKUPTICS;

	if (!splitscreen && cl_pred.tic_ack_valid[slot] && cl_pred.tic_ack_tic[slot] == executed_tic)
	{
		uint32_t ack_seq = cl_pred.tic_ack_seq[slot];
		cl_pred.tic_ack_valid[slot] = false;

		if (ack_seq != 0 && SeqAfter(ack_seq, cl_pred.confirmed_seq))
		{
			cl_pred.confirmed_seq = ack_seq;
			CL_PredPruneUpToSeq(ack_seq);
		}
		else if (ack_seq != 0 && ack_seq == cl_pred.confirmed_seq)
		{
			// Server repeated last_exec_seq (underrun) — prune nothing, queue stays exact
			CL_PredPruneUpToSeq(ack_seq);
		}

		cl_pred.exact_ack_ticks++;
	}
	else if (!splitscreen && cl_pred.queue_len > 0 && cl_pred.exact_ack_ticks > 0)
	{
		cl_pred.missing_ack_ticks++;

		// Safety guard against unbounded queue growth if inline ACKs were skipped
		if (cl_pred.queue_len > 20)
		{
			uint32_t drop = cl_pred.queue_len - 16;
			memmove(&cl_pred.queue[0], &cl_pred.queue[drop], 16 * sizeof(cl_pred.queue[0]));
			cl_pred.queue_len = 16;
		}
	}

	p = &players[consoleplayer];
	if (cv_cl_movement.value && !splitscreen && CL_PredIsPlayerEligible(p))
	{
		CL_PredReconcileAtCurrentHorizon(p);
		CL_PredRefreshShadowPhysics();
	}
	else
	{
		CL_PredResetVisualState();
	}

	if (cv_cl_movement_debug.value >= 2 && (executed_tic % TICRATE) == 0)
	{
		fixed_t err_xy = R_PointToDist2(cl_pred.smooth_err_x, cl_pred.smooth_err_y, 0, 0);
		fixed_t cam_s = (cl_pred.shadow_valid && camera[0].chase)
			? R_PointToDist2(camera[0].x, camera[0].y, cl_pred.shadow_mo.x, cl_pred.shadow_mo.y) : 0;
		fixed_t cam_c = (playeringame[consoleplayer] && players[consoleplayer].mo && camera[0].chase)
			? R_PointToDist2(camera[0].x, camera[0].y, players[consoleplayer].mo->x, players[consoleplayer].mo->y) : 0;
		CONS_Printf("[CL_PRED] tic=%u sent=%u conf=%u qlen=%u maxq=%u exact=%u miss=%u ackpkts=%u ovf=%u svunder=%u pred=%u dxy=%d dz=%d dang=%u replays=%u rec=%u snap=%u errxy=%d maxrec=%d cams=%d camc=%d att=%u exi=%u rngv=%u\n",
			executed_tic, cl_pred.last_sent_seq, cl_pred.confirmed_seq,
			cl_pred.queue_len, cl_pred.max_in_flight,
			cl_pred.exact_ack_ticks, cl_pred.missing_ack_ticks,
			cl_pred.ack_packets_recv, cl_pred.queue_overflows, cl_pred.server_underrun_flags,
			cl_pred.predicted_tics,
			cl_pred.last_delta_xy >> FRACBITS, cl_pred.last_delta_z >> FRACBITS,
			(uint32_t)(((uint64_t)cl_pred.last_delta_angle * 360ULL) >> 32),
			cl_pred.shadow_replays,
			cl_pred.smooth_reconciles, cl_pred.smooth_snaps,
			err_xy >> FRACBITS, cl_pred.max_reconcile_xy >> FRACBITS,
			cam_s >> FRACBITS, cam_c >> FRACBITS,
			cl_pred.attached_count,
			cl_pred.extrap_count,
			cl_pred.rng_violations);
	}
}

// Instant drift spark tier chime on predicted shadow kart (suppresses delayed confirmed duplicate)
static void CL_PredUpdateDriftSound(void)
{
	const player_t *sp = &cl_pred.shadow_player;
	uint8_t cur_stage;

	if (!cl_pred.shadow_valid || !cv_cl_movement.value || dedicated || rendermode == render_none)
	{
		cl_pred.drift_sound_stage = 0;
		return;
	}

	if ((sp->drift == 0 && sp->airthrustdir == 0) || P_PlayerInPain(sp))
	{
		cl_pred.drift_sound_stage = 0;
		return;
	}

	if (leveltime > cl_pred.drift_sound_suppress_until)
		cl_pred.drift_sound_suppress_stage = 0;

	cur_stage = K_GetKartDriftSparkStageForValue(sp, sp->driftcharge);
	if (cur_stage > cl_pred.drift_sound_stage)
	{
		if (playeringame[consoleplayer] && players[consoleplayer].mo && displayplayers[0] == consoleplayer)
		{
			S_StartSoundAtVolume(players[consoleplayer].mo, sfx_s3ka2, 192);
		}
		cl_pred.drift_sound_stage = cur_stage;
		if (cur_stage > cl_pred.drift_sound_suppress_stage)
			cl_pred.drift_sound_suppress_stage = cur_stage;
		cl_pred.drift_sound_suppress_until = leveltime + (tic_t)max(cv_cl_movement_maxtics.value, 4) + 6;
	}
	else if (cur_stage < cl_pred.drift_sound_stage)
	{
		cl_pred.drift_sound_stage = cur_stage;
	}
}

dboolean CL_PredConsumeSuppressedDriftSound(const player_t *player, uint8_t newstage)
{
	if (!player || !cv_cl_movement.value || !cl_pred.shadow_valid
		|| !client || !netgame || demo.playback || splitscreen
		|| !playeringame[consoleplayer] || player != &players[consoleplayer])
	{
		return false;
	}

	if (leveltime <= cl_pred.drift_sound_suppress_until && newstage <= cl_pred.drift_sound_suppress_stage)
	{
		return true;
	}

	return false;
}

// Instant wall-bonk audio + 3D-tracked starburst feedback on predicted wall bounce
static void CL_PredUpdateWallBumpFeedback(void)
{
	const player_t *sp = &cl_pred.shadow_player;
	const mobj_t *smo = &cl_pred.shadow_mo;
	uint16_t cur_bu;

	if (cl_pred.bump_fx_tics > 0)
		cl_pred.bump_fx_tics--;

	if (!cl_pred.shadow_valid || !cv_cl_movement.value || dedicated || rendermode == render_none)
	{
		cl_pred.prev_shadow_bumpunstuck = 0;
		return;
	}

	cur_bu = sp->bumpUnstuck;
	if (cur_bu > cl_pred.prev_shadow_bumpunstuck)
	{
		if (playeringame[consoleplayer] && players[consoleplayer].mo && displayplayers[0] == consoleplayer)
		{
			S_StartSound(players[consoleplayer].mo, sfx_s3k49);
		}
		cl_pred.wall_bump_suppress_until = leveltime + (tic_t)max(cv_cl_movement_maxtics.value, 4) + 6;
		cl_pred.bump_fx_tics = 8;
		cl_pred.bump_fx_x = smo->x + cl_pred.smooth_err_x;
		cl_pred.bump_fx_y = smo->y + cl_pred.smooth_err_y;
		cl_pred.bump_fx_z = smo->z + cl_pred.smooth_err_z + (smo->height >> 1);
		cl_pred.bump_fx_scale = smo->scale;
	}

	cl_pred.prev_shadow_bumpunstuck = cur_bu;
}

dboolean CL_PredConsumeSuppressedWallBump(const mobj_t *mo)
{
	if (!mo || !cv_cl_movement.value || !cl_pred.shadow_valid
		|| !client || !netgame || demo.playback || splitscreen
		|| !playeringame[consoleplayer] || mo != players[consoleplayer].mo)
	{
		return false;
	}

	return (leveltime <= cl_pred.wall_bump_suppress_until);
}

dboolean CL_PredShouldHideMobj(const mobj_t *mobj)
{
	const mobj_t *pmo;

	if (!mobj || mobj->type != MT_BUMP || !cv_cl_movement.value || !cl_pred.shadow_valid
		|| !client || !netgame || demo.playback || splitscreen
		|| gamestate != GS_LEVEL || !playeringame[consoleplayer])
	{
		return false;
	}

	if (leveltime > cl_pred.wall_bump_suppress_until)
		return false;

	pmo = players[consoleplayer].mo;
	if (!pmo || P_MobjWasRemoved(pmo))
		return false;

	// Hide only our own delayed confirmed wall-bump starburst spawned behind us
	return (R_PointToDist2(mobj->x, mobj->y, pmo->x, pmo->y) < FixedMul(160 * FRACUNIT, pmo->scale));
}

// Instant Sneaker / dash-panel boost sound cue (suppresses delayed confirmed duplicate)
static void CL_PredUpdateSneakerFeedback(void)
{
	const player_t *sp = &cl_pred.shadow_player;
	tic_t cur_snk;

	if (!cl_pred.shadow_valid || !cv_cl_movement.value || dedicated || rendermode == render_none)
	{
		cl_pred.prev_shadow_sneakertimer = 0;
		return;
	}

	cur_snk = sp->sneakertimer;
	if (cur_snk > cl_pred.prev_shadow_sneakertimer + 2)
	{
		if (playeringame[consoleplayer] && players[consoleplayer].mo && displayplayers[0] == consoleplayer)
		{
			sfxenum_t sfx = sfx_cdfm01;
			if (((sp->numsneakers + sp->numpanels) > 1) && K_StackingActive() && cv_kartstacking_sneakerstacksound.value)
				sfx = sfx_cdfm40;

			S_StopSoundByID(players[consoleplayer].mo, sfx_cdfm01);
			S_StopSoundByID(players[consoleplayer].mo, sfx_cdfm40);
			S_StartSound(players[consoleplayer].mo, sfx);
		}
		cl_pred.sneaker_sound_suppress_until = leveltime + (tic_t)max(cv_cl_movement_maxtics.value, 4) + 6;
	}

	cl_pred.prev_shadow_sneakertimer = cur_snk;
}

dboolean CL_PredConsumeSuppressedSneakerSound(const player_t *player)
{
	if (!player || !cv_cl_movement.value || !cl_pred.shadow_valid
		|| !client || !netgame || demo.playback || splitscreen
		|| !playeringame[consoleplayer] || player != &players[consoleplayer])
	{
		return false;
	}

	return (leveltime <= cl_pred.sneaker_sound_suppress_until);
}

// Instant lap / finish-line audio cue (suppresses delayed confirmed duplicate)
static void CL_PredUpdateLapFeedback(void)
{
	const player_t *sp = &cl_pred.shadow_player;
	uint8_t cur_laps;

	if (!cl_pred.shadow_valid || !cv_cl_movement.value || dedicated || rendermode == render_none
		|| !(gametypes[gametype]->rules & GTR_CIRCUIT))
	{
		cl_pred.prev_shadow_laps = playeringame[consoleplayer] ? players[consoleplayer].laps : 0;
		return;
	}

	if (leveltime > cl_pred.lap_sound_suppress_until)
		cl_pred.lap_sound_suppress_lap = 0;

	cur_laps = sp->laps;
	if (cur_laps > cl_pred.prev_shadow_laps && cl_pred.prev_shadow_laps > 0)
	{
		if (displayplayers[0] == consoleplayer)
		{
			if (cur_laps == numlaps)
				S_StartSound(NULL, cv_usegenericlapsounds.value ? sfx_s3k68 : sfx_finlap);
			else if (cur_laps < numlaps)
				S_StartSound(NULL, cv_usegenericlapsounds.value ? sfx_s221 : sfx_nxtlap);
			else
				S_StartSound(NULL, cv_usegenericlapsounds.value ? sfx_s3k6a : sfx_rcefin);
		}
		if (cur_laps > cl_pred.lap_sound_suppress_lap)
			cl_pred.lap_sound_suppress_lap = cur_laps;
		cl_pred.lap_sound_suppress_until = leveltime + (tic_t)max(cv_cl_movement_maxtics.value, 4) + 8;
	}

	cl_pred.prev_shadow_laps = cur_laps;
}

dboolean CL_PredConsumeSuppressedLapSound(const player_t *player, uint8_t new_laps)
{
	if (!player || !cv_cl_movement.value || !cl_pred.shadow_valid
		|| !client || !netgame || demo.playback || splitscreen
		|| !playeringame[consoleplayer] || player != &players[consoleplayer])
	{
		return false;
	}

	return (leveltime <= cl_pred.lap_sound_suppress_until && new_laps <= cl_pred.lap_sound_suppress_lap);
}

// Instant extrapolated item-impact audio cue (suppresses delayed confirmed sfx_s3k7b)
static void CL_PredUpdateItemHitFeedback(void)
{
	if (!cl_pred.shadow_valid || !cv_cl_movement.value || !cv_cl_movement_extrapitems.value
		|| dedicated || rendermode == render_none)
	{
		cl_pred.prev_shadow_item_hit = false;
		return;
	}

	if (cl_pred.shadow_item_hit && !cl_pred.prev_shadow_item_hit)
	{
		if (playeringame[consoleplayer] && players[consoleplayer].mo && displayplayers[0] == consoleplayer)
		{
			S_StartSound(players[consoleplayer].mo, sfx_s3k7b);
		}
		cl_pred.item_hit_suppress_until = leveltime + (tic_t)max(cv_cl_movement_maxtics.value, 4) + 6;
	}

	cl_pred.prev_shadow_item_hit = cl_pred.shadow_item_hit;
}

dboolean CL_PredConsumeSuppressedItemHitSound(const mobj_t *mo)
{
	if (!mo || !cv_cl_movement.value || !cv_cl_movement_extrapitems.value || !cl_pred.shadow_valid
		|| !client || !netgame || demo.playback || splitscreen
		|| !playeringame[consoleplayer] || mo != players[consoleplayer].mo)
	{
		return false;
	}

	return (leveltime <= cl_pred.item_hit_suppress_until);
}

dboolean CL_PredIsDrivingCamera0(void)
{
	if (!client || !netgame || demo.playback || splitscreen
		|| !cv_cl_movement.value || cv_cl_movement_maxtics.value <= 0
		|| gamestate != GS_LEVEL || !addedtogame || !playeringame[consoleplayer]
		|| displayplayers[0] != consoleplayer || leveltime < 1)
	{
		return false;
	}

	return CL_PredIsPlayerEligible(&players[consoleplayer]);
}

// Called ONCE per local 35 Hz frame right after TryRunTics() in D_SRB2Loop().
// Advances BOTH Sonic's visual pose (pose_old -> pose_new) AND camera[0]
// (pview_old[0] -> pview_new[0]) together in strict 1:1 lockstep with I_GetTimeFrac(),
// eliminating jitter from 0-tic or 2-tic server packet bursts and mid-frame NetUpdate().
void CL_PredOnLocalTic(tic_t realtics)
{
	const player_t *p;
	uint32_t maxtics, n, head_seq;
	cl_pred_pose_t next_pose;
	dboolean force_snap;

	(void)realtics;

	if (!client || !netgame || demo.playback || splitscreen
		|| !cv_cl_movement.value || cv_cl_movement_maxtics.value <= 0
		|| gamestate != GS_LEVEL || !addedtogame || !playeringame[consoleplayer]
		|| leveltime < 1)
	{
		CL_PredResetVisualState();
		return;
	}

	p = &players[consoleplayer];
	if (!CL_PredIsPlayerEligible(p))
	{
		CL_PredResetVisualState();
		return;
	}

	CL_PredUpdateExtrapItems();
	CL_PredRefreshShadowPhysics();
	if (!cl_pred.shadow_valid)
		return;

	maxtics = (uint32_t)cv_cl_movement_maxtics.value;
	n = min(cl_pred.queue_len, maxtics);
	if (n > CLPRED_QUEUE_CAP)
		n = CLPRED_QUEUE_CAP;
	head_seq = (n > 0) ? cl_pred.queue[n - 1].seq : cl_pred.confirmed_seq;

	CL_PredCapturePose(&cl_pred.shadow_player, &cl_pred.shadow_mo, &next_pose);

	force_snap = (!cl_pred.pose_new.valid
		|| !cv_cl_movement_smooth.value
		|| p->mo->resetinterp
		|| P_PlayerInPain(p));

	if (force_snap)
	{
		cl_pred.pose_old = next_pose;
		cl_pred.pose_new = next_pose;
		cl_pred.smooth_err_x = 0;
		cl_pred.smooth_err_y = 0;
		cl_pred.smooth_err_z = 0;
		cl_pred.smooth_err_angle = 0;
		cl_pred.smooth_err_drawangle = 0;
		cl_pred.smooth_snaps++;
	}
	else
	{
		cl_pred.pose_old = cl_pred.pose_new;
		cl_pred.pose_new = next_pose;
	}

	cl_pred.last_head_seq = head_seq;
	CL_PredUpdateDriftSound();
	CL_PredUpdateWallBumpFeedback();
	CL_PredUpdateSneakerFeedback();
	CL_PredUpdateLapFeedback();
	CL_PredUpdateItemHitFeedback();

	if (CL_PredIsDrivingCamera0())
	{
		R_UpdateViewInterpolationSingle(0);

		if (camera[0].chase)
		{
			CL_PredStepChaseCamera(&players[consoleplayer], &camera[0], false);
		}

		if (rendermode != render_none && players[consoleplayer].mo)
		{
			const dboolean skybox = (skyboxmo[0] && cv_skybox.value);
			if (skyVisiblePerPlayer[0] && skybox)
				R_SkyboxFrame(0);
			R_SetupFrame(0, skybox);
		}
	}
}

// =========================================================================
// Deterministic 0-ms exactness self-test (cl_movement_selftest > 0)
// =========================================================================

#define CLPRED_SELFTEST_RING 16

typedef struct cl_pred_st_slot_s
{
	dboolean valid;
	tic_t lt;
	player_t player;
	mobj_t mo;
	sector_t *secs[32];
	int32_t sec_count;
	ticcmd_t raw_cmd; // command executed at (lt - 1) to reach state at lt
} cl_pred_st_slot_t;

typedef struct cl_pred_st_state_s
{
	cl_pred_st_slot_t ring[CLPRED_SELFTEST_RING];
	tic_t last_lt;
	uint32_t samples;
	uint32_t exact;
	uint32_t mismatches;
	fixed_t max_xy;
	fixed_t max_z;
	uint32_t max_ang;
	fixed_t max_mom;
	int32_t max_drift;
	int32_t max_charge;
} cl_pred_st_state_t;

static cl_pred_st_state_t cl_pred_st;

void CL_PredSelfTestOnTic(tic_t executed_tic, const ticcmd_t *raw_cmd)
{
	const player_t *p;
	uint32_t horizon, k;
	cl_pred_st_slot_t *cur_slot;

	(void)executed_tic;

	if (cv_cl_movement_selftest.value <= 0)
		return;

	if (gamestate != GS_LEVEL || !playeringame[consoleplayer] || !raw_cmd || leveltime == 0)
	{
		memset(&cl_pred_st, 0, sizeof(cl_pred_st));
		return;
	}

	p = &players[consoleplayer];
	if (leveltime <= cl_pred_st.last_lt || !CL_PredIsPlayerEligible(p))
	{
		memset(&cl_pred_st, 0, sizeof(cl_pred_st));
		cl_pred_st.last_lt = leveltime;
		return;
	}

	cl_pred_st.last_lt = leveltime;

	cur_slot = &cl_pred_st.ring[leveltime % CLPRED_SELFTEST_RING];
	cur_slot->valid = true;
	cur_slot->lt = leveltime;
	cur_slot->player = *p;
	cur_slot->mo = *p->mo;
	cur_slot->sec_count = 0;
	{
		const msecnode_t *node;
		for (node = p->mo->touching_sectorlist; node && cur_slot->sec_count < 32; node = node->m_sectorlist_next)
		{
			if (node->m_sector)
				cur_slot->secs[cur_slot->sec_count++] = node->m_sector;
		}
	}
	cur_slot->raw_cmd = *raw_cmd;

	horizon = (uint32_t)cv_cl_movement_selftest.value;
	if (horizon > 12)
		horizon = 12;

	if (leveltime > horizon)
	{
		tic_t start_lt = leveltime - horizon;
		const cl_pred_st_slot_t *start_slot = &cl_pred_st.ring[start_lt % CLPRED_SELFTEST_RING];
		dboolean chain_ok = (start_slot->valid && start_slot->lt == start_lt);
		ticcmd_t cmds[12];

		for (k = 0; chain_ok && k < horizon; k++)
		{
			tic_t step_lt = start_lt + 1 + k;
			const cl_pred_st_slot_t *step_slot = &cl_pred_st.ring[step_lt % CLPRED_SELFTEST_RING];
			if (!step_slot->valid || step_slot->lt != step_lt)
				chain_ok = false;
			else
				cmds[k] = step_slot->raw_cmd;
		}

		if (chain_ok)
		{
			player_t pred_p;
			mobj_t pred_mo;
			fixed_t ex, ey, ez, emx, emy, emz, xy_err, mom_err;
			uint32_t ang_err;
			int32_t drift_err, chg_err;

			CL_PredReplayOnConsolePlayer(&start_slot->player, &start_slot->mo,
				start_slot->secs, start_slot->sec_count,
				start_lt, cmds, horizon, false, &pred_p, &pred_mo, NULL);

			ex = abs(pred_mo.x - p->mo->x);
			ey = abs(pred_mo.y - p->mo->y);
			ez = abs(pred_mo.z - p->mo->z);
			xy_err = max(ex, ey);
			ang_err = (uint32_t)abs((int32_t)(pred_mo.angle - p->mo->angle));
			emx = abs(pred_mo.momx - p->mo->momx);
			emy = abs(pred_mo.momy - p->mo->momy);
			emz = abs(pred_mo.momz - p->mo->momz);
			mom_err = max(max(emx, emy), emz);
			drift_err = abs((int32_t)pred_p.drift - (int32_t)p->drift);
			chg_err = abs((int32_t)pred_p.driftcharge - (int32_t)p->driftcharge);

			cl_pred_st.samples++;
			if (xy_err > cl_pred_st.max_xy) cl_pred_st.max_xy = xy_err;
			if (ez > cl_pred_st.max_z) cl_pred_st.max_z = ez;
			if (ang_err > cl_pred_st.max_ang) cl_pred_st.max_ang = ang_err;
			if (mom_err > cl_pred_st.max_mom) cl_pred_st.max_mom = mom_err;
			if (drift_err > cl_pred_st.max_drift) cl_pred_st.max_drift = drift_err;
			if (chg_err > cl_pred_st.max_charge) cl_pred_st.max_charge = chg_err;

			if (xy_err == 0 && ez == 0 && ang_err == 0 && mom_err == 0 && drift_err == 0 && chg_err == 0)
			{
				cl_pred_st.exact++;
			}
			else
			{
				cl_pred_st.mismatches++;
				if (cl_pred_st.mismatches <= 15)
				{
					CONS_Printf("[CL_PRED_SELFTEST] MISMATCH lt=%u H=%u dx=%d dy=%d dz=%d dang=%u dmx=%d dmy=%d dmz=%d ddrift=%d dchg=%d (spd=%d drift=%d chg=%d snk=%d/%d fb=%d/%d ns=%u/%u np=%u/%u sb=%d/%d off=%d/%d)\n",
						leveltime, horizon,
						pred_mo.x - p->mo->x, pred_mo.y - p->mo->y, pred_mo.z - p->mo->z,
						ang_err,
						pred_mo.momx - p->mo->momx, pred_mo.momy - p->mo->momy, pred_mo.momz - p->mo->momz,
						(int32_t)pred_p.drift - (int32_t)p->drift,
						(int32_t)pred_p.driftcharge - (int32_t)p->driftcharge,
						p->speed >> FRACBITS, p->drift, p->driftcharge,
						pred_p.sneakertimer, p->sneakertimer,
						pred_p.floorboost, p->floorboost,
						pred_p.numsneakers, p->numsneakers,
						pred_p.numpanels, p->numpanels,
						pred_p.speedboost, p->speedboost,
						pred_p.offroad, p->offroad);
				}
			}
		}
	}

	if ((leveltime % TICRATE) == 0)
	{
		CONS_Printf("[CL_PRED_SELFTEST] lt=%u H=%u samples=%u exact=%u mis=%u max_xy=%d max_z=%d max_ang=%u max_mom=%d max_drift=%d max_chg=%d rng_viol=%u\n",
			leveltime, horizon,
			cl_pred_st.samples, cl_pred_st.exact, cl_pred_st.mismatches,
			cl_pred_st.max_xy, cl_pred_st.max_z, cl_pred_st.max_ang,
			cl_pred_st.max_mom, cl_pred_st.max_drift, cl_pred_st.max_charge,
			cl_pred.rng_violations);
	}
}

// =========================================================================
// Turn-rate, HUD player, visual pose interpolation, attached VFX/items & camera
// =========================================================================

static inline fixed_t CL_PredLerpFixed(fixed_t from, fixed_t to, fixed_t frac)
{
	return from + FixedMul(frac, to - from);
}

static inline angle_t CL_PredLerpAngle(angle_t from, angle_t to, fixed_t frac)
{
	return from + (angle_t)FixedMul(frac, (fixed_t)(to - from));
}

const player_t *CL_PredGetTurnPlayer(void)
{
	if (!cv_cl_movement.value || !cl_pred.shadow_valid || cl_pred.predicted_tics == 0
		|| !client || !netgame || demo.playback || splitscreen
		|| gamestate != GS_LEVEL || !addedtogame || !playeringame[consoleplayer]
		|| P_IsPredicting())
	{
		return NULL;
	}

	return &cl_pred.shadow_player;
}

const player_t *CL_PredGetHudPlayer(const player_t *stplyr)
{
	if (stplyr && cv_cl_movement.value && cl_pred.shadow_valid
		&& client && netgame && !demo.playback && !splitscreen
		&& gamestate == GS_LEVEL && playeringame[consoleplayer]
		&& stplyr == &players[consoleplayer])
	{
		return &cl_pred.shadow_player;
	}

	return stplyr;
}

void CL_PredUpdateVisualSmoothing(fixed_t deltatics_fixed)
{
	double dt_sec, rate, factor;

	if (!cl_pred.shadow_valid || !cv_cl_movement.value || !cv_cl_movement_smooth.value)
	{
		cl_pred.smooth_err_x = 0;
		cl_pred.smooth_err_y = 0;
		cl_pred.smooth_err_z = 0;
		cl_pred.smooth_err_angle = 0;
		cl_pred.smooth_err_drawangle = 0;
		return;
	}

	if (!cl_pred.smooth_err_x && !cl_pred.smooth_err_y && !cl_pred.smooth_err_z
		&& !cl_pred.smooth_err_angle && !cl_pred.smooth_err_drawangle)
	{
		return;
	}

	dt_sec = FIXED_TO_FLOAT(deltatics_fixed) / (double)TICRATE;
	if (dt_sec <= 0.0)
		return;
	if (dt_sec > 0.25)
		dt_sec = 0.25;

	rate = (double)cv_cl_movement_smoothrate.value * 0.5;
	if (rate < 1.0)
		rate = 1.0;

	factor = exp(-rate * dt_sec);

	cl_pred.smooth_err_x = (fixed_t)((double)cl_pred.smooth_err_x * factor);
	cl_pred.smooth_err_y = (fixed_t)((double)cl_pred.smooth_err_y * factor);
	cl_pred.smooth_err_z = (fixed_t)((double)cl_pred.smooth_err_z * factor);
	cl_pred.smooth_err_angle = (int32_t)((double)cl_pred.smooth_err_angle * factor);
	cl_pred.smooth_err_drawangle = (int32_t)((double)cl_pred.smooth_err_drawangle * factor);

	if (abs(cl_pred.smooth_err_x) < (FRACUNIT >> 3)) cl_pred.smooth_err_x = 0;
	if (abs(cl_pred.smooth_err_y) < (FRACUNIT >> 3)) cl_pred.smooth_err_y = 0;
	if (abs(cl_pred.smooth_err_z) < (FRACUNIT >> 3)) cl_pred.smooth_err_z = 0;
	if (abs(cl_pred.smooth_err_angle) < (int32_t)(ANG1 >> 3)) cl_pred.smooth_err_angle = 0;
	if (abs(cl_pred.smooth_err_drawangle) < (int32_t)(ANG1 >> 3)) cl_pred.smooth_err_drawangle = 0;
}

dboolean CL_PredShouldRenderMobj(const mobj_t *mobj)
{
	return (cl_pred.shadow_valid
		&& cl_pred.pose_new.valid
		&& cv_cl_movement.value
		&& client && netgame && !demo.playback && !splitscreen
		&& gamestate == GS_LEVEL
		&& playeringame[consoleplayer]
		&& mobj != NULL
		&& mobj == players[consoleplayer].mo
		&& !P_IsPredicting());
}

void CL_PredInterpolateMobjState(fixed_t frac, interpmobjstate_t *out)
{
	const cl_pred_pose_t *a = cl_pred.pose_old.valid ? &cl_pred.pose_old : &cl_pred.pose_new;
	const cl_pred_pose_t *b = &cl_pred.pose_new;

	if (frac >= FRACUNIT)
	{
		out->x = b->x + cl_pred.smooth_err_x;
		out->y = b->y + cl_pred.smooth_err_y;
		out->z = b->z + cl_pred.smooth_err_z;
		out->scale = b->scale;
		out->radius = b->radius;
		out->height = b->height;
		out->angle = b->drawangle + (angle_t)cl_pred.smooth_err_drawangle;
		out->pitch = b->pitch;
		out->roll = b->roll;
		out->slopepitch = b->slopepitch;
		out->sloperoll = b->sloperoll;
		out->spritexscale = b->spritexscale;
		out->spriteyscale = b->spriteyscale;
		out->spritexoffset = b->spritexoffset;
		out->spriteyoffset = b->spriteyoffset;
		out->subsector = R_PointInSubsector(out->x, out->y);
		return;
	}

	if (frac < 0)
		frac = 0;

	out->x = CL_PredLerpFixed(a->x, b->x, frac) + cl_pred.smooth_err_x;
	out->y = CL_PredLerpFixed(a->y, b->y, frac) + cl_pred.smooth_err_y;
	out->z = CL_PredLerpFixed(a->z, b->z, frac) + cl_pred.smooth_err_z;
	out->spritexscale = CL_PredLerpFixed(a->spritexscale, b->spritexscale, frac);
	out->spriteyscale = CL_PredLerpFixed(a->spriteyscale, b->spriteyscale, frac);
	out->spritexoffset = CL_PredLerpFixed(a->spritexoffset, b->spritexoffset, frac);
	out->spriteyoffset = CL_PredLerpFixed(a->spriteyoffset, b->spriteyoffset, frac);

	if (a->scale == b->scale)
	{
		out->scale = b->scale;
		out->radius = b->radius;
		out->height = b->height;
	}
	else
	{
		out->scale = CL_PredLerpFixed(a->scale, b->scale, frac);
		out->radius = (b->scale != 0) ? FixedMul(b->radius, FixedDiv(out->scale, b->scale)) : b->radius;
		out->height = (b->scale != 0) ? FixedMul(b->height, FixedDiv(out->scale, b->scale)) : b->height;
	}

	out->subsector = R_PointInSubsector(out->x, out->y);
	out->angle = CL_PredLerpAngle(a->drawangle, b->drawangle, frac) + (angle_t)cl_pred.smooth_err_drawangle;

#ifdef HWRENDER
	if (rendermode == render_opengl && cv_glmodels.value)
	{
		out->pitch = CL_PredLerpAngle(a->pitch, b->pitch, frac);
		out->roll = CL_PredLerpAngle(a->roll, b->roll, frac);
		out->slopepitch = CL_PredLerpAngle(a->slopepitch, b->slopepitch, frac);
		out->sloperoll = CL_PredLerpAngle(a->sloperoll, b->sloperoll, frac);
	}
	else
#endif
	{
		out->pitch = b->pitch;
		out->roll = b->roll;
		out->slopepitch = b->slopepitch;
		out->sloperoll = b->sloperoll;
	}
}

dboolean CL_PredGetInterpolatedMobjPos(const mobj_t *mobj, fixed_t frac, fixed_t *out_x, fixed_t *out_y, fixed_t *out_z)
{
	const cl_pred_pose_t *a, *b;

	if (!CL_PredShouldRenderMobj(mobj))
		return false;

	a = cl_pred.pose_old.valid ? &cl_pred.pose_old : &cl_pred.pose_new;
	b = &cl_pred.pose_new;

	if (frac >= FRACUNIT)
	{
		*out_x = b->x + cl_pred.smooth_err_x;
		*out_y = b->y + cl_pred.smooth_err_y;
		*out_z = b->z + cl_pred.smooth_err_z;
		return true;
	}

	if (frac < 0)
		frac = 0;

	*out_x = CL_PredLerpFixed(a->x, b->x, frac) + cl_pred.smooth_err_x;
	*out_y = CL_PredLerpFixed(a->y, b->y, frac) + cl_pred.smooth_err_y;
	*out_z = CL_PredLerpFixed(a->z, b->z, frac) + cl_pred.smooth_err_z;
	return true;
}

void CL_PredGetRenderOffsets(const mobj_t *mobj, fixed_t *sprxoff, fixed_t *spryoff, fixed_t *sprzoff, fixed_t *crash_zoffset)
{
	if (CL_PredShouldRenderMobj(mobj))
	{
		*sprxoff = cl_pred.pose_new.sprxoff;
		*spryoff = cl_pred.pose_new.spryoff;
		*sprzoff = cl_pred.pose_new.sprzoff;
		*crash_zoffset = cl_pred.pose_new.crash_zoffset;
	}
	else
	{
		*sprxoff = mobj->sprxoff;
		*spryoff = mobj->spryoff;
		*sprzoff = mobj->sprzoff;
		*crash_zoffset = mobj->player ? mobj->player->crash_zoffset : 0;
	}
}

void CL_PredGetRenderSprite(const mobj_t *mobj, uint8_t *out_sprite2, uint32_t *out_frame)
{
	if (CL_PredShouldRenderMobj(mobj))
	{
		*out_sprite2 = cl_pred.pose_new.sprite2;
		*out_frame = (mobj->frame & ~FF_FRAMEMASK) | (cl_pred.pose_new.frame & FF_FRAMEMASK);
	}
	else
	{
		*out_sprite2 = mobj->sprite2;
		*out_frame = mobj->frame;
	}
}

sector_t *CL_PredGetRenderSector(void)
{
	fixed_t rx, ry;
	subsector_t *ss;

	if (!cl_pred.shadow_valid || !cl_pred.pose_new.valid)
		return NULL;

	rx = cl_pred.pose_new.x + cl_pred.smooth_err_x;
	ry = cl_pred.pose_new.y + cl_pred.smooth_err_y;
	ss = R_PointInSubsector(rx, ry);
	return ss ? ss->sector : NULL;
}

// =========================================================================
// Local kart-attached VFX (sparks/dust/flames) & held/orbiting shield items
// =========================================================================

dboolean CL_PredShouldAttachMobj(const mobj_t *mobj)
{
	const mobj_t *pmo;
	dboolean owned = false;

	if (!cl_pred.shadow_valid || !cl_pred.pose_new.valid || !cv_cl_movement.value
		|| !client || !netgame || demo.playback || splitscreen
		|| gamestate != GS_LEVEL || !playeringame[consoleplayer] || P_IsPredicting())
	{
		return false;
	}

	pmo = players[consoleplayer].mo;
	if (!pmo || P_MobjWasRemoved(pmo) || !mobj || P_MobjWasRemoved(mobj) || mobj == pmo)
		return false;

	switch (mobj->type)
	{
		case MT_BATTLEBUMPER:
			// Only attach active orbiting bumpers; popped bumpers (health <= 0) fly off freely
			if (mobj->health <= 0)
				return false;
			break;
		case MT_KARMAHITBOX:
		case MT_DRIFTSPARK:
		case MT_DRIFTDUST:
		case MT_BRAKEDRIFT:
		case MT_AIZDRIFTSTRAT:
		case MT_BOOSTFLAME:
		case MT_BOOSTSTACK:
		case MT_FASTLINE:
		case MT_INVULNFLASH:
		case MT_OVERLAY:
		case MT_INSTASHIELDA:
		case MT_INSTASHIELDB:
		case MT_GAINAX:
		case MT_SMOLDERING:
		case MT_BANANA_SHIELD:
		case MT_ORBINAUT_SHIELD:
		case MT_JAWZ_SHIELD:
		case MT_EGGMANITEM_SHIELD:
		case MT_EGGMINE_SHIELD:
		case MT_SSMINE_SHIELD:
		case MT_SINK_SHIELD:
		case MT_ROCKETSNEAKER:
		case MT_FLAMESHIELD:
		case MT_THUNDERSHIELD:
		case MT_ATTRACTIONSHIELD:
		case MT_BUBBLESHIELD:
			break;
		default:
			return false;
	}

	if (mobj->target == pmo)
	{
		owned = true;
	}
	else if (mobj->type == MT_BOOSTFLAME && mobj->target && !P_MobjWasRemoved(mobj->target)
		&& mobj->target->type == MT_ROCKETSNEAKER && mobj->target->target == pmo)
	{
		owned = true;
	}

	if (!owned)
		return false;

	// Don't swing distant stray dust/fastlines that already drifted far away from the kart
	if (mobj->type == MT_DRIFTDUST || mobj->type == MT_FASTLINE)
	{
		fixed_t maxd = FixedMul(180 * FRACUNIT, pmo->scale);
		if (R_PointToDist2(mobj->x, mobj->y, pmo->x, pmo->y) > maxd)
			return false;
	}

	return true;
}

void CL_PredTransformAttachedState(const mobj_t *mobj, fixed_t frac, interpmobjstate_t *out)
{
	const mobj_t *pmo;
	const cl_pred_pose_t *a, *b;
	fixed_t conf_x, conf_y, conf_z;
	fixed_t pred_x, pred_y, pred_z;
	angle_t conf_ang, pred_ang, delta_ang;
	fixed_t rx, ry, rz, cos_d, sin_d;

	if (!cl_pred.shadow_valid || !cl_pred.pose_new.valid || !playeringame[consoleplayer])
		return;

	pmo = players[consoleplayer].mo;
	if (!pmo || P_MobjWasRemoved(pmo))
		return;

	a = cl_pred.pose_old.valid ? &cl_pred.pose_old : &cl_pred.pose_new;
	b = &cl_pred.pose_new;

	if (frac >= FRACUNIT)
	{
		conf_x = pmo->x;
		conf_y = pmo->y;
		conf_z = pmo->z;
		conf_ang = pmo->angle;

		pred_x = b->x + cl_pred.smooth_err_x;
		pred_y = b->y + cl_pred.smooth_err_y;
		pred_z = b->z + cl_pred.smooth_err_z;
		pred_ang = b->angle + (angle_t)cl_pred.smooth_err_angle;
	}
	else
	{
		if (frac < 0)
			frac = 0;

		conf_x = CL_PredLerpFixed(pmo->old_x, pmo->x, frac);
		conf_y = CL_PredLerpFixed(pmo->old_y, pmo->y, frac);
		conf_z = CL_PredLerpFixed(pmo->old_z, pmo->z, frac);
		conf_ang = pmo->resetinterp ? pmo->angle : CL_PredLerpAngle(pmo->old_angle, pmo->angle, frac);

		pred_x = CL_PredLerpFixed(a->x, b->x, frac) + cl_pred.smooth_err_x;
		pred_y = CL_PredLerpFixed(a->y, b->y, frac) + cl_pred.smooth_err_y;
		pred_z = CL_PredLerpFixed(a->z, b->z, frac) + cl_pred.smooth_err_z;
		pred_ang = CL_PredLerpAngle(a->angle, b->angle, frac) + (angle_t)cl_pred.smooth_err_angle;
	}

	rx = out->x - conf_x;
	ry = out->y - conf_y;
	rz = out->z - conf_z;

	// World-space orbiters (like Battle Bumpers) translate cleanly with the kart without yaw-twisting
	if (mobj && mobj->type == MT_BATTLEBUMPER)
	{
		out->x = pred_x + rx;
		out->y = pred_y + ry;
		out->z = pred_z + rz;
		out->subsector = R_PointInSubsector(out->x, out->y);
		return;
	}

	delta_ang = pred_ang - conf_ang;

	cos_d = FINECOSINE((delta_ang >> ANGLETOFINESHIFT) & FINEMASK);
	sin_d = FINESINE((delta_ang >> ANGLETOFINESHIFT) & FINEMASK);

	out->x = pred_x + FixedMul(rx, cos_d) - FixedMul(ry, sin_d);
	out->y = pred_y + FixedMul(rx, sin_d) + FixedMul(ry, cos_d);
	out->z = pred_z + rz;
	out->angle += delta_ang;
	out->subsector = R_PointInSubsector(out->x, out->y);
}

void CL_PredPrepareAttachedFrame(void)
{
	thinker_t *th;
	fixed_t frac;

	cl_pred.attached_count = 0;

	if (!playeringame[consoleplayer] || !CL_PredShouldRenderMobj(players[consoleplayer].mo))
		return;

	frac = R_GetTimeFrac(RTF_LEVEL);

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		mobj_t *mo;
		interpmobjstate_t tmp;

		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo = (mobj_t *)th;
		if (!CL_PredShouldAttachMobj(mo))
			continue;

		if (cl_pred.attached_count >= CLPRED_MAX_ATTACHED)
			break;

		tmp.x = (frac >= FRACUNIT) ? mo->x : CL_PredLerpFixed(mo->old_x, mo->x, frac);
		tmp.y = (frac >= FRACUNIT) ? mo->y : CL_PredLerpFixed(mo->old_y, mo->y, frac);
		tmp.z = (frac >= FRACUNIT) ? mo->z : CL_PredLerpFixed(mo->old_z, mo->z, frac);
		tmp.angle = mo->angle;
		tmp.subsector = mo->subsector;
		CL_PredTransformAttachedState(mo, frac, &tmp);

		cl_pred.attached[cl_pred.attached_count].mo = mo;
		cl_pred.attached[cl_pred.attached_count].vis_sec = tmp.subsector ? tmp.subsector->sector : NULL;
		cl_pred.attached_count++;
	}
}

sector_t *CL_PredGetAttachedRenderSector(const mobj_t *mobj)
{
	uint32_t i;

	for (i = 0; i < cl_pred.attached_count; i++)
	{
		if (cl_pred.attached[i].mo == mobj)
			return cl_pred.attached[i].vis_sec;
	}

	return (mobj && mobj->subsector) ? mobj->subsector->sector : NULL;
}

uint32_t CL_PredGetAttachedCount(void)
{
	return cl_pred.attached_count;
}

mobj_t *CL_PredGetAttachedMobj(uint32_t idx, sector_t **out_sec)
{
	if (idx >= cl_pred.attached_count)
		return NULL;
	if (out_sec)
		*out_sec = cl_pred.attached[idx].vis_sec;
	return cl_pred.attached[idx].mo;
}

// =========================================================================
// Nearby moving offensive item extrapolation (cl_movement_extrapitems)
// =========================================================================

static dboolean CL_PredIsExtrapItemType(mobjtype_t type)
{
	switch (type)
	{
		case MT_ORBINAUT:
		case MT_JAWZ:
		case MT_JAWZ_DUD:
		case MT_BALLHOG:
			return true;
		default:
			return false;
	}
}

static dboolean CL_PredIsStaticHazardType(mobjtype_t type)
{
	switch (type)
	{
		case MT_BANANA:
		case MT_SSMINE:
		case MT_LANDMINE:
		case MT_EGGMINE:
			return true;
		default:
			return false;
	}
}

static void CL_PredUpdateExtrapItems(void)
{
	typedef struct prev_ex_s
	{
		mobj_t *mo;
		fixed_t x, y, z;
	} prev_ex_t;

	prev_ex_t prev[CLPRED_MAX_EXTRAP_ITEMS];
	uint32_t prev_count = 0;
	const mobj_t *pmo;
	uint32_t maxtics, n, i, s;
	fixed_t range_moving, range_static;
	thinker_t *th;
	uint32_t saved_rng[PRNUMCLASS];
	int32_t c;

	for (i = 0; i < cl_pred.extrap_count; i++)
	{
		prev[prev_count].mo = cl_pred.extrap_items[i].mo;
		prev[prev_count].x = cl_pred.extrap_items[i].new_x;
		prev[prev_count].y = cl_pred.extrap_items[i].new_y;
		prev[prev_count].z = cl_pred.extrap_items[i].new_z;
		prev_count++;
	}

	cl_pred.extrap_count = 0;
	cl_pred.near_hazard_count = 0;

	if (!cv_cl_movement.value || !cv_cl_movement_extrapitems.value
		|| !client || !netgame || demo.playback || splitscreen
		|| gamestate != GS_LEVEL || !playeringame[consoleplayer])
	{
		return;
	}

	pmo = players[consoleplayer].mo;
	if (!pmo || P_MobjWasRemoved(pmo))
		return;

	maxtics = (uint32_t)cv_cl_movement_maxtics.value;
	n = min(cl_pred.queue_len, maxtics);
	if (n > CLPRED_EXTRAP_STEPS)
		n = CLPRED_EXTRAP_STEPS;
	if (n == 0)
		return;

	range_moving = FixedMul(2048 * FRACUNIT, mapobjectscale > 0 ? mapobjectscale : FRACUNIT);
	range_static = FixedMul(1024 * FRACUNIT, mapobjectscale > 0 ? mapobjectscale : FRACUNIT);

	for (c = 0; c < PRNUMCLASS; c++)
		saved_rng[c] = P_GetRandSeed((pr_class_t)c);

	cl_pred_active_flag = true;
	P_PredPushMapState();

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		mobj_t *mo;

		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo = (mobj_t *)th;
		if (P_MobjWasRemoved(mo) || mo->health <= 0)
			continue;

		if (CL_PredIsStaticHazardType(mo->type))
		{
			if (cl_pred.near_hazard_count < CLPRED_MAX_EXTRAP_ITEMS
				&& R_PointToDist2(mo->x, mo->y, pmo->x, pmo->y) <= range_static)
			{
				cl_pred.near_hazards[cl_pred.near_hazard_count++] = mo;
			}
			continue;
		}

		if (!CL_PredIsExtrapItemType(mo->type))
			continue;
		if (mo->momx == 0 && mo->momy == 0)
			continue;
		if (cl_pred.extrap_count >= CLPRED_MAX_EXTRAP_ITEMS)
			continue;
		if (R_PointToDist2(mo->x, mo->y, pmo->x, pmo->y) > range_moving)
			continue;

		// Read-only wall-bounce + floor-follow extrapolation of detached item copy
		{
			mobj_t tmp = *mo;
			uint32_t idx = cl_pred.extrap_count;
			dboolean found_prev = false;

			CL_PredDetachShadowMobj(&tmp, NULL);

			for (s = 0; s < n; s++)
			{
				TryMoveResult_t res;
				memset(&res, 0, sizeof(res));

				if (!P_TryMove(&tmp, tmp.x + tmp.momx, tmp.y + tmp.momy, true, &res))
				{
					if (res.line != NULL && (res.normal.x != 0 || res.normal.y != 0))
					{
						fixed_t dot = FixedMul(tmp.momx, res.normal.x) + FixedMul(tmp.momy, res.normal.y);
						tmp.momx -= 2 * FixedMul(dot, res.normal.x);
						tmp.momy -= 2 * FixedMul(dot, res.normal.y);
						P_TryMove(&tmp, tmp.x + tmp.momx, tmp.y + tmp.momy, true, NULL);
					}
				}

				if (!(tmp.flags & MF_NOGRAVITY))
				{
					if (tmp.eflags & MFE_VERTICALFLIP)
					{
						if (tmp.z + tmp.height < tmp.ceilingz)
						{
							tmp.momz += FixedMul((5 * gravity) / 2, tmp.scale);
							tmp.z += tmp.momz;
						}
						if (tmp.z + tmp.height > tmp.ceilingz)
						{
							tmp.z = tmp.ceilingz - tmp.height;
							tmp.momz = 0;
						}
					}
					else
					{
						if (tmp.z > tmp.floorz)
						{
							tmp.momz -= FixedMul((5 * gravity) / 2, tmp.scale);
							tmp.z += tmp.momz;
						}
						if (tmp.z < tmp.floorz)
						{
							tmp.z = tmp.floorz;
							tmp.momz = 0;
						}
					}
				}

				cl_pred.extrap_items[idx].step_x[s] = tmp.x;
				cl_pred.extrap_items[idx].step_y[s] = tmp.y;
				cl_pred.extrap_items[idx].step_z[s] = tmp.z;
			}

			cl_pred.extrap_items[idx].mo = mo;
			cl_pred.extrap_items[idx].step_count = n;
			cl_pred.extrap_items[idx].new_x = tmp.x;
			cl_pred.extrap_items[idx].new_y = tmp.y;
			cl_pred.extrap_items[idx].new_z = tmp.z;
			cl_pred.extrap_items[idx].vis_sec = tmp.subsector ? tmp.subsector->sector : NULL;

			for (i = 0; i < prev_count; i++)
			{
				if (prev[i].mo == mo)
				{
					if (R_PointToDist2(prev[i].x, prev[i].y, tmp.x, tmp.y) <= FixedMul(256 * FRACUNIT, mapobjectscale > 0 ? mapobjectscale : FRACUNIT))
					{
						cl_pred.extrap_items[idx].old_x = prev[i].x;
						cl_pred.extrap_items[idx].old_y = prev[i].y;
						cl_pred.extrap_items[idx].old_z = prev[i].z;
						found_prev = true;
					}
					break;
				}
			}

			if (!found_prev)
			{
				if (n >= 2)
				{
					cl_pred.extrap_items[idx].old_x = cl_pred.extrap_items[idx].step_x[n - 2];
					cl_pred.extrap_items[idx].old_y = cl_pred.extrap_items[idx].step_y[n - 2];
					cl_pred.extrap_items[idx].old_z = cl_pred.extrap_items[idx].step_z[n - 2];
				}
				else
				{
					cl_pred.extrap_items[idx].old_x = mo->x;
					cl_pred.extrap_items[idx].old_y = mo->y;
					cl_pred.extrap_items[idx].old_z = mo->z;
				}
			}

			cl_pred.extrap_count++;
		}
	}

	P_PredPopMapState();
	cl_pred_active_flag = false;

	for (c = 0; c < PRNUMCLASS; c++)
	{
		uint32_t cur = P_GetRandSeed((pr_class_t)c);
		if (cur != saved_rng[c])
		{
			cl_pred.rng_violations++;
			P_SetRandSeed((pr_class_t)c, saved_rng[c]);
		}
	}
}

static dboolean CL_PredCheckShadowItemImpact(player_t *player, uint32_t step_idx)
{
	const mobj_t *pmo;
	mobj_t *smo;
	uint32_t i;
	dboolean hit = false;

	if (!cv_cl_movement_extrapitems.value || !player || !player->mo)
		return false;
	if (P_PlayerInPain(player) || player->flashing > 0 || player->hyudorotimer > 0
		|| player->smonitortimer > 0 || player->invincibilitytimer > 0 || player->growshrinktimer > 0)
	{
		return false;
	}

	pmo = playeringame[consoleplayer] ? players[consoleplayer].mo : NULL;
	smo = player->mo;

	// 1. Check extrapolated moving items (Orbinaut / Jawz / Ballhog) at step_idx
	for (i = 0; i < cl_pred.extrap_count; i++)
	{
		const mobj_t *imo = cl_pred.extrap_items[i].mo;
		uint32_t s;
		fixed_t ix, iy, iz, bd;

		if (!imo || P_MobjWasRemoved(imo) || imo->health <= 0)
			continue;
		if (imo->target == pmo && imo->threshold > 0)
			continue;
		if (cl_pred.extrap_items[i].step_count == 0)
			continue;

		s = (step_idx < cl_pred.extrap_items[i].step_count) ? step_idx : (cl_pred.extrap_items[i].step_count - 1);
		ix = cl_pred.extrap_items[i].step_x[s];
		iy = cl_pred.extrap_items[i].step_y[s];
		iz = cl_pred.extrap_items[i].step_z[s];

		bd = smo->radius + imo->radius;
		if (abs(smo->x - ix) >= bd || abs(smo->y - iy) >= bd)
			continue;
		if (smo->z > iz + imo->height || smo->z + smo->height < iz)
			continue;

		hit = true;
		break;
	}

	// 2. Check nearby static hazards (Banana / Mines)
	if (!hit)
	{
		for (i = 0; i < cl_pred.near_hazard_count; i++)
		{
			const mobj_t *hmo = cl_pred.near_hazards[i];
			fixed_t bd;

			if (!hmo || P_MobjWasRemoved(hmo) || hmo->health <= 0)
				continue;
			if (hmo->target == pmo && hmo->threshold > 0)
				continue;

			bd = smo->radius + hmo->radius;
			if (abs(smo->x - hmo->x) >= bd || abs(smo->y - hmo->y) >= bd)
				continue;
			if (smo->z > hmo->z + hmo->height || smo->z + smo->height < hmo->z)
				continue;

			hit = true;
			break;
		}
	}

	if (hit)
	{
		// Soft impact brake on shadow kart: prevents driving 150u past the item before server confirms
		// while keeping rare false-hit recovery gentle and non-jarring.
		smo->momx = (smo->momx * 3) / 4;
		smo->momy = (smo->momy * 3) / 4;
		player->speed = R_PointToDist2(0, 0, smo->momx, smo->momy);
	}

	return hit;
}

dboolean CL_PredShouldExtrapItem(const mobj_t *mobj)
{
	uint32_t i;

	if (!mobj || cl_pred.extrap_count == 0 || !cv_cl_movement.value || !cv_cl_movement_extrapitems.value
		|| P_IsPredicting())
	{
		return false;
	}

	for (i = 0; i < cl_pred.extrap_count; i++)
	{
		if (cl_pred.extrap_items[i].mo == mobj)
			return true;
	}

	return false;
}

dboolean CL_PredApplyExtrapItemState(const mobj_t *mobj, fixed_t frac, interpmobjstate_t *out)
{
	uint32_t i;

	if (!mobj || !out || cl_pred.extrap_count == 0 || !cv_cl_movement.value || !cv_cl_movement_extrapitems.value)
		return false;

	for (i = 0; i < cl_pred.extrap_count; i++)
	{
		if (cl_pred.extrap_items[i].mo == mobj)
		{
			if (frac >= FRACUNIT)
			{
				out->x = cl_pred.extrap_items[i].new_x;
				out->y = cl_pred.extrap_items[i].new_y;
				out->z = cl_pred.extrap_items[i].new_z;
			}
			else
			{
				if (frac < 0)
					frac = 0;
				out->x = CL_PredLerpFixed(cl_pred.extrap_items[i].old_x, cl_pred.extrap_items[i].new_x, frac);
				out->y = CL_PredLerpFixed(cl_pred.extrap_items[i].old_y, cl_pred.extrap_items[i].new_y, frac);
				out->z = CL_PredLerpFixed(cl_pred.extrap_items[i].old_z, cl_pred.extrap_items[i].new_z, frac);
			}
			out->subsector = R_PointInSubsector(out->x, out->y);
			return true;
		}
	}

	return false;
}

sector_t *CL_PredGetExtrapItemRenderSector(const mobj_t *mobj)
{
	uint32_t i;

	for (i = 0; i < cl_pred.extrap_count; i++)
	{
		if (cl_pred.extrap_items[i].mo == mobj)
			return cl_pred.extrap_items[i].vis_sec;
	}

	return (mobj && mobj->subsector) ? mobj->subsector->sector : NULL;
}

uint32_t CL_PredGetExtrapItemCount(void)
{
	return cl_pred.extrap_count;
}

mobj_t *CL_PredGetExtrapItemMobj(uint32_t idx, sector_t **out_sec)
{
	if (idx >= cl_pred.extrap_count)
		return NULL;
	if (out_sec)
		*out_sec = cl_pred.extrap_items[idx].vis_sec;
	return cl_pred.extrap_items[idx].mo;
}

void CL_PredDrawHUDFeedback(void)
{
	trackingResult_t res;
	vector3_t pos;
	const state_t *st;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	uint32_t frame_idx;
	patch_t *patch;
	fixed_t draw_scale;

	if (cl_pred.bump_fx_tics == 0 || !cv_cl_movement.value || !cl_pred.shadow_valid
		|| splitscreen || gamestate != GS_LEVEL || displayplayers[0] != consoleplayer)
	{
		return;
	}

	pos.x = cl_pred.bump_fx_x;
	pos.y = cl_pred.bump_fx_y;
	pos.z = cl_pred.bump_fx_z;
	memset(&res, 0, sizeof(res));
	K_ObjectTracking(&res, &pos, false);
	if (!res.onScreen || res.scale <= 0)
		return;

	st = &states[mobjinfo[MT_BUMP].spawnstate];
	if (st->sprite >= numsprites)
		return;
	sprdef = &sprites[st->sprite];
	if (!sprdef->spriteframes || sprdef->numframes == 0)
		return;

	frame_idx = (uint32_t)((8 - cl_pred.bump_fx_tics) / 2);
	if (frame_idx >= (uint32_t)sprdef->numframes)
		frame_idx = (uint32_t)sprdef->numframes - 1;
	sprframe = &sprdef->spriteframes[frame_idx];

	patch = W_CachePatchNum(sprframe->lumppat[0], PU_CACHE);
	if (!patch)
		return;

	draw_scale = FixedMul(res.scale, cl_pred.bump_fx_scale > 0 ? cl_pred.bump_fx_scale : FRACUNIT);
	if (draw_scale > 3 * FRACUNIT)
		draw_scale = 3 * FRACUNIT;
	V_DrawFixedPatch(res.x, res.y, draw_scale, V_SPLITSCREEN, patch, NULL);
}

void CL_PredGetSmoothViewOffset(uint8_t viewnum, fixed_t *ox, fixed_t *oy, fixed_t *oz)
{
	if (viewnum == 0 && cl_pred.shadow_valid && cl_pred.pose_new.valid
		&& cv_cl_movement.value && client && netgame && !demo.playback && !splitscreen
		&& gamestate == GS_LEVEL && displayplayers[0] == consoleplayer
		&& !camera[0].freecam && !players[consoleplayer].awayviewtics)
	{
		*ox = cl_pred.smooth_err_x;
		*oy = cl_pred.smooth_err_y;
		*oz = cl_pred.smooth_err_z;
	}
	else
	{
		*ox = 0;
		*oy = 0;
		*oz = 0;
	}
}

dboolean CL_PredGetFirstPersonView(uint8_t viewnum, fixed_t *out_x, fixed_t *out_y, fixed_t *out_z, subsector_t **out_subsector)
{
	fixed_t vx, vy, vz;
	const mobj_t *smo;
	const player_t *sp;

	if (viewnum != 0 || !cl_pred.shadow_valid || !cl_pred.pose_new.valid
		|| !cv_cl_movement.value || !client || !netgame || demo.playback || splitscreen
		|| gamestate != GS_LEVEL || displayplayers[0] != consoleplayer)
		return false;

	sp = &cl_pred.shadow_player;
	smo = &cl_pred.shadow_mo;

	vx = cl_pred.pose_new.x;
	vy = cl_pred.pose_new.y;
	if (smo->eflags & MFE_VERTICALFLIP)
		vz = cl_pred.pose_new.z + smo->height - sp->viewheight;
	else
		vz = cl_pred.pose_new.z + sp->viewheight;

	*out_x = vx;
	*out_y = vy;
	*out_z = vz;
	if (out_subsector)
		*out_subsector = R_PointInSubsector(vx, vy);
	return true;
}

dboolean CL_PredStepChaseCamera(player_t *player, camera_t *thiscam, dboolean resetcalled)
{
	player_t saved_player;
	mobj_t cam_mo;
	dboolean ret;

	if (!player || !thiscam || thiscam != &camera[0]
		|| !cl_pred.shadow_valid || !cl_pred.pose_new.valid
		|| !cv_cl_movement.value || !client || !netgame || demo.playback || splitscreen
		|| player != &players[consoleplayer])
	{
		return P_MoveChaseCamera(player, thiscam, resetcalled);
	}

	// Swap players[consoleplayer] temporarily with the read-only shadow kart so
	// P_IsMachineLocalPlayer(player) and (player - players) stay exact inside
	// P_MoveChaseCamera, then restore the authoritative player bit-for-bit.
	saved_player = players[consoleplayer];
	players[consoleplayer] = cl_pred.shadow_player;
	cam_mo = cl_pred.shadow_mo;
	cam_mo.player = &players[consoleplayer];
	players[consoleplayer].mo = &cam_mo;

	ret = P_MoveChaseCamera(&players[consoleplayer], thiscam, resetcalled);

	players[consoleplayer] = saved_player;
	return ret;
}

void CL_PredDrawDebugHUD(void)
{
	fixed_t err_xy;
	int32_t deg;
	char line1[128];
	char line2[128];

	if (cv_cl_movement_debug.value <= 0 || gamestate != GS_LEVEL || !client || !netgame || demo.playback)
		return;

	err_xy = R_PointToDist2(cl_pred.smooth_err_x, cl_pred.smooth_err_y, 0, 0);
	deg = (int32_t)(((uint64_t)cl_pred.last_delta_angle * 360ULL) >> 32);

	snprintf(line1, sizeof(line1), "PRED %s  tics:%u/%d  q:%u  rec:%u  snap:%u",
		(cv_cl_movement.value && cl_pred.shadow_valid) ? "ON" : "OFF",
		cl_pred.predicted_tics,
		cv_cl_movement_maxtics.value,
		cl_pred.queue_len,
		cl_pred.smooth_reconciles,
		cl_pred.smooth_snaps);

	snprintf(line2, sizeof(line2), "dXY:%d  dZ:%d  dAng:%d  errXY:%d  maxRec:%d",
		cl_pred.last_delta_xy >> FRACBITS,
		cl_pred.last_delta_z >> FRACBITS,
		deg,
		err_xy >> FRACBITS,
		cl_pred.max_reconcile_xy >> FRACBITS);

	V_DrawThinString(4, 4, V_SNAPTOTOP|V_SNAPTOLEFT|V_ALLOWLOWERCASE, line1);
	V_DrawThinString(4, 14, V_SNAPTOTOP|V_SNAPTOLEFT|V_ALLOWLOWERCASE, line2);

	if (cv_cl_movement_debug.value >= 2)
	{
		char line3[128];
		snprintf(line3, sizeof(line3), "seq:%u/%u  ex:%u  miss:%u  ovf:%u  und:%u  att:%u",
			cl_pred.last_sent_seq,
			cl_pred.confirmed_seq,
			cl_pred.exact_ack_ticks,
			cl_pred.missing_ack_ticks,
			cl_pred.queue_overflows,
			cl_pred.server_underrun_flags,
			cl_pred.attached_count);
		V_DrawThinString(4, 24, V_SNAPTOTOP|V_SNAPTOLEFT|V_ALLOWLOWERCASE, line3);
	}
}

// =========================================================================
// Inspection & Console Command
// =========================================================================

dboolean CL_PredIsProtoActive(void)
{
	return (!splitscreen && client && netgame && !demo.playback);
}

dboolean CL_PredHasShadow(void)
{
	return cl_pred.shadow_valid;
}

const player_t *CL_PredGetShadowPlayer(void)
{
	return cl_pred.shadow_valid ? &cl_pred.shadow_player : NULL;
}

const mobj_t *CL_PredGetShadowMobj(void)
{
	return cl_pred.shadow_valid ? &cl_pred.shadow_mo : NULL;
}

uint32_t CL_PredGetQueueLen(void)
{
	return cl_pred.queue_len;
}

uint32_t CL_PredGetPredictedTics(void)
{
	return cl_pred.predicted_tics;
}

const cl_pred_cmd_t *CL_PredGetQueueEntry(uint32_t idx)
{
	if (idx >= cl_pred.queue_len)
		return NULL;
	return &cl_pred.queue[idx];
}

uint32_t CL_PredGetLastSentSeq(void)
{
	return cl_pred.last_sent_seq;
}

uint32_t CL_PredGetConfirmedSeq(void)
{
	return cl_pred.confirmed_seq;
}

fixed_t CL_PredGetLastDeltaXY(void)
{
	return cl_pred.last_delta_xy;
}

fixed_t CL_PredGetLastDeltaZ(void)
{
	return cl_pred.last_delta_z;
}

angle_t CL_PredGetLastDeltaAngle(void)
{
	return cl_pred.last_delta_angle;
}

static void Command_CLMovementStatus_f(void)
{
	if (server)
	{
		int32_t n;
		int32_t active = 0;
		CONS_Printf("[SV_PRED] Server prediction status:\n");
		for (n = 1; n < MAXNETNODES; n++)
		{
			if (!nodeingame[n])
				continue;
			if (sv_pred[n].enabled)
			{
				CONS_Printf("  node %d: native (used=%d, exec_seq=%u, recv_seq=%u, qlen=%u, pkts=%u, queued=%u, recov=%u, under=%u, trim=%u)\n",
					n, sv_pred[n].used_pred_cmd ? 1 : 0,
					sv_pred[n].last_exec_seq, sv_pred[n].last_recv_seq,
					sv_pred[n].queue_len, sv_pred[n].packets_recv,
					sv_pred[n].cmds_queued, sv_pred[n].cmds_recovered,
					sv_pred[n].underruns, sv_pred[n].trimmed);
				active++;
			}
			else
			{
				CONS_Printf("  node %d: splitscreen/inactive\n", n);
			}
		}
		if (!active)
			CONS_Printf("  (no native prediction clients connected)\n");
	}
	else if (client)
	{
		fixed_t err_xy = R_PointToDist2(cl_pred.smooth_err_x, cl_pred.smooth_err_y, 0, 0);
		CONS_Printf("[CL_PRED] enabled=%d maxtics=%d repeat=%d newcol=%d extrapitems=%d\n",
			cv_cl_movement.value, cv_cl_movement_maxtics.value, cv_cl_netrepeatinput.value,
			cv_cl_newcollision.value, cv_cl_movement_extrapitems.value);
		CONS_Printf("[CL_PRED] seq: sent=%u conf=%u qlen=%u maxq=%u exact_acks=%u missing_acks=%u ack_pkts=%u ovf=%u sv_underrun=%u\n",
			cl_pred.last_sent_seq, cl_pred.confirmed_seq,
			cl_pred.queue_len, cl_pred.max_in_flight,
			cl_pred.exact_ack_ticks, cl_pred.missing_ack_ticks,
			cl_pred.ack_packets_recv, cl_pred.queue_overflows, cl_pred.server_underrun_flags);
		CONS_Printf("[CL_PRED] shadow: valid=%d pred_tics=%u dxy=%d dz=%d dang=%u replays=%u rng_viol=%u att=%u exi=%u\n",
			cl_pred.shadow_valid ? 1 : 0, cl_pred.predicted_tics,
			cl_pred.last_delta_xy >> FRACBITS, cl_pred.last_delta_z >> FRACBITS,
			(uint32_t)(((uint64_t)cl_pred.last_delta_angle * 360ULL) >> 32),
			cl_pred.shadow_replays, cl_pred.rng_violations, cl_pred.attached_count, cl_pred.extrap_count);
		CONS_Printf("[CL_PRED] smooth: on=%d rate=%d rec=%u snap=%u errxy=%d maxrec=%d\n",
			cv_cl_movement_smooth.value, cv_cl_movement_smoothrate.value,
			cl_pred.smooth_reconciles, cl_pred.smooth_snaps,
			err_xy >> FRACBITS, cl_pred.max_reconcile_xy >> FRACBITS);
	}
	else
	{
		CONS_Printf("[CL_PRED] offline (cl_movement=%d maxtics=%d repeat=%d)\n",
			cv_cl_movement.value, cv_cl_movement_maxtics.value, cv_cl_netrepeatinput.value);
	}
}

void CL_PredRegisterVars(void)
{
	CV_RegisterVar(&cv_cl_movement);
	CV_RegisterVar(&cv_cl_movement_maxtics);
	CV_RegisterVar(&cv_cl_movement_smooth);
	CV_RegisterVar(&cv_cl_movement_smoothrate);
	CV_RegisterVar(&cv_cl_netrepeatinput);
	CV_RegisterVar(&cv_cl_movement_debug);
	CV_RegisterVar(&cv_cl_movement_selftest);
	CV_RegisterVar(&cv_cl_newcollision);
	CV_RegisterVar(&cv_cl_movement_extrapitems);
	COM_AddCommand("cl_movement_status", Command_CLMovementStatus_f);
	CL_PredResetSession();
	SV_PredResetAll();
}
