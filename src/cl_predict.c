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
///        and backward-compatible Option B input ACK / repeat protocol.

#include "cl_predict.h"
#include "doomstat.h"
#include "d_net.h"
#include "i_net.h"
#include "g_game.h"
#include "p_tick.h"
#include "p_local.h"
#include "p_mobj.h"
#include "k_kart.h"
#include "k_bot.h"
#include "m_random.h"
#include "r_main.h"
#include "r_fps.h"
#include "r_state.h"
#include "v_video.h"
#include "i_video.h"
#include "i_time.h"
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
consvar_t cv_cl_movement_proto = CVAR_INIT("cl_movement_proto", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_cl_movement_debug = CVAR_INIT("cl_movement_debug", "0", 0, cl_movement_debug_cons_t, NULL);
consvar_t cv_cl_movement_selftest = CVAR_INIT("cl_movement_selftest", "0", 0, cl_movement_selftest_cons_t, NULL);

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
// Server-side per-node Option B state
// =========================================================================

#define CLPRED_SV_QUEUE_CAP      8
#define CLPRED_SV_MAX_QUEUE      4
#define CLPRED_SV_DRAIN_TICS     4
#define CLPRED_ACK_REDUNDANCY    8

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
	uint8_t max_repeat;

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

void SV_PredHandleCap(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	predcap_pak incoming;

	if (client || node <= 0 || node >= MAXNETNODES || !nodeingame[node])
		return;

	if ((size_t)doomcom->datalength < BASEPACKETSIZE + sizeof(predcap_pak))
		return;

	incoming = netbuffer->u.predcap;
	if (LSBF_LONG(incoming.magic) != CLPRED_MAGIC || incoming.version != CLPRED_PROTO_VERSION)
		return;

	if (!sv_pred[node].enabled)
	{
		SV_PredResetNode(node);
		sv_pred[node].enabled = true;
		sv_pred[node].max_repeat = min(incoming.max_repeat, (uint8_t)CLPRED_MAX_REPEAT);

		CONS_Debug(DBG_NETPLAY, "[SV_PRED] Node %d negotiated Option B (v%u, max_repeat=%u)\n",
			node, incoming.version, sv_pred[node].max_repeat);
		if (cv_cl_movement_debug.value >= 1)
			CONS_Printf("[SV_PRED] Node %d negotiated Option B (v%u, max_repeat=%u)\n",
				node, incoming.version, sv_pred[node].max_repeat);
	}

	// Always reply with our capability packet (handles dropped reply retries cleanly)
	netbuffer->packettype = PT_PREDCAP;
	netbuffer->u.predcap.magic = LSBF_LONG(CLPRED_MAGIC);
	netbuffer->u.predcap.version = CLPRED_PROTO_VERSION;
	netbuffer->u.predcap.flags = CLPRED_CAP_REPEAT | CLPRED_CAP_EXACT_ACK;
	netbuffer->u.predcap.max_repeat = CLPRED_MAX_REPEAT;
	netbuffer->u.predcap.reserved = 0;

	HSendPacket(node, false, 0, sizeof(predcap_pak));
}

void SV_PredOnVanillaCmd(int8_t node)
{
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
		// Client somehow sent PT_CLIENTCMD_PRED before/without PT_PREDCAP finishing;
		// enable cleanly so inputs are never dropped.
		SV_PredResetNode(node);
		pn->enabled = true;
		pn->max_repeat = CLPRED_MAX_REPEAT;
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
		// Hard latency ceiling of 2 tics, plus gentle self-drain if a persistent
		// surplus of >= 2 tics remains after a drop or minor clock skew.
		while (pn->queue_len > 2)
		{
			SV_PredQueueShiftOldest(pn);
			pn->trimmed++;
		}

		if (pn->queue_len >= 2)
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
		// True underrun: hold previous controls just like vanilla, mark not received
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

void SV_PredSendAck(int32_t node, tic_t realfirsttic, tic_t lasttictosend)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	sv_pred_node_t *pn;
	tic_t ack_start, t;
	uint32_t numtics, k;
	size_t packetsize;

	if (node <= 0 || node >= MAXNETNODES || lasttictosend <= realfirsttic)
		return;

	pn = &sv_pred[node];
	if (!pn->enabled || !pn->used_pred_cmd)
		return;

	ack_start = realfirsttic;

	// Include up to CLPRED_ACK_REDUNDANCY recent tics so a single dropped PT_PREDACK
	// packet is seamlessly recovered on the very next tic.
	if (lasttictosend - ack_start < CLPRED_ACK_REDUNDANCY)
	{
		tic_t min_start = (lasttictosend > CLPRED_ACK_REDUNDANCY)
			? (lasttictosend - CLPRED_ACK_REDUNDANCY)
			: 0;
		while (ack_start > min_start)
		{
			tic_t prev = ack_start - 1;
			uint32_t slot = prev % BACKUPTICS;
			if (!pn->tic_ack_valid[slot] || pn->tic_ack_tic[slot] != prev)
				break;
			ack_start = prev;
		}
	}

	if (lasttictosend - ack_start > CLPRED_MAX_ACK_TICS)
		ack_start = lasttictosend - CLPRED_MAX_ACK_TICS;

	numtics = (uint32_t)(lasttictosend - ack_start);
	if (numtics == 0)
		return;

	netbuffer->packettype = PT_PREDACK;
	netbuffer->u.predack.starttic = LSBF_LONG(ack_start);
	netbuffer->u.predack.numtics = (uint8_t)numtics;
	netbuffer->u.predack.flags = pn->last_underrun ? CLPRED_ACKF_UNDERRUN : 0;
	netbuffer->u.predack.reserved = 0;

	for (k = 0; k < numtics; k++)
	{
		uint32_t slot;
		uint32_t seq;
		t = ack_start + k;
		slot = t % BACKUPTICS;
		if (pn->tic_ack_valid[slot] && pn->tic_ack_tic[slot] == t)
			seq = pn->tic_ack_seq[slot];
		else
			seq = pn->last_exec_seq;
		netbuffer->u.predack.ack_seq[k] = LSBF_LONG(seq);
	}

	packetsize = offsetof(predack_pak, ack_seq) + numtics * sizeof(uint32_t);
	HSendPacket(node, false, 0, packetsize);
}

// =========================================================================
// Client-side Option B + Option A state
// =========================================================================

#define CLPRED_CAP_MAX_ATTEMPTS 5
#define CLPRED_CAP_RETRY_TICS   10

typedef enum
{
	CLPRED_CAP_IDLE = 0,
	CLPRED_CAP_PROBING,
	CLPRED_CAP_ACTIVE,
	CLPRED_CAP_UNSUPPORTED
} cl_pred_cap_state_t;

typedef struct cl_pred_tx_s
{
	uint32_t seq;
	ticcmd_t cmd;
} cl_pred_tx_t;

typedef struct cl_pred_state_s
{
	cl_pred_cap_state_t cap_state;
	dboolean proto_active;
	uint8_t server_max_repeat;
	uint8_t cap_attempts;
	tic_t cap_last_send_tic;

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
	uint32_t fallback_ack_ticks;
	uint32_t queue_overflows;
	uint32_t max_in_flight;
	uint32_t server_underrun_flags;

	// Step 3: Read-only shadow kart state
	player_t shadow_player;
	mobj_t shadow_mo;
	dboolean shadow_valid;
	uint32_t predicted_tics;
	fixed_t last_delta_xy;
	fixed_t last_delta_z;
	angle_t last_delta_angle;
	uint32_t shadow_replays;
	uint32_t rng_violations;

	// Step 4: Visual pose, sub-tic lerp endpoints, smooth reconciliation & camera state
	struct cl_pred_pose_s
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
	} pose_old, pose_new;
	uint32_t last_head_seq;

	fixed_t smooth_err_x;
	fixed_t smooth_err_y;
	fixed_t smooth_err_z;
	int32_t smooth_err_angle;
	int32_t smooth_err_drawangle;

	uint32_t smooth_reconciles;
	uint32_t smooth_snaps;
	fixed_t max_reconcile_xy;

	dboolean defer_cam0;
	dboolean cam0_was_deferred;
} cl_pred_state_t;

static cl_pred_state_t cl_pred;

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
}

void CL_PredResetSession(void)
{
	memset(&cl_pred, 0, sizeof(cl_pred));
	cl_pred.next_seq = 1;
}

void CL_PredResetState(void)
{
	cl_pred.queue_len = 0;
	cl_pred.last_leveltime = 0;
	CL_PredResetVisualState();
}

static void CL_PredSendCap(void)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	netbuffer->packettype = PT_PREDCAP;
	netbuffer->u.predcap.magic = LSBF_LONG(CLPRED_MAGIC);
	netbuffer->u.predcap.version = CLPRED_PROTO_VERSION;
	netbuffer->u.predcap.flags = CLPRED_CAP_REPEAT | CLPRED_CAP_EXACT_ACK;
	netbuffer->u.predcap.max_repeat = CLPRED_MAX_REPEAT;
	netbuffer->u.predcap.reserved = 0;

	HSendPacket(servernode, false, 0, sizeof(predcap_pak));
}

void CL_PredTickHandshake(void)
{
	tic_t now;

	if (!client || !netgame || demo.playback)
		return;

	if (!cv_cl_movement_proto.value)
	{
		if (cl_pred.proto_active || cl_pred.cap_state != CLPRED_CAP_IDLE)
		{
			cl_pred.proto_active = false;
			cl_pred.cap_state = CLPRED_CAP_IDLE;
			cl_pred.proto_start_seq = cl_pred.next_seq;
		}
		return;
	}

	if (cl_pred.cap_state == CLPRED_CAP_ACTIVE || cl_pred.cap_state == CLPRED_CAP_UNSUPPORTED)
		return;

	now = I_GetTime();
	if (cl_pred.cap_state == CLPRED_CAP_IDLE)
	{
		CL_PredSendCap();
		cl_pred.cap_state = CLPRED_CAP_PROBING;
		cl_pred.cap_attempts = 1;
		cl_pred.cap_last_send_tic = now;
	}
	else if (cl_pred.cap_state == CLPRED_CAP_PROBING && now >= cl_pred.cap_last_send_tic + CLPRED_CAP_RETRY_TICS)
	{
		if (cl_pred.cap_attempts < CLPRED_CAP_MAX_ATTEMPTS)
		{
			CL_PredSendCap();
			cl_pred.cap_attempts++;
			cl_pred.cap_last_send_tic = now;
		}
		else
		{
			cl_pred.cap_state = CLPRED_CAP_UNSUPPORTED;
			cl_pred.proto_active = false;
			CONS_Debug(DBG_NETPLAY, "[CL_PRED] Server did not reply to PT_PREDCAP; using Option A vanilla fallback\n");
			if (cv_cl_movement_debug.value >= 1)
				CONS_Printf("[CL_PRED] Server did not reply to PT_PREDCAP; using Option A vanilla fallback\n");
		}
	}
}

void CL_PredHandleCap(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	predcap_pak incoming;

	if (!client || node != servernode)
		return;

	if ((size_t)doomcom->datalength < BASEPACKETSIZE + sizeof(predcap_pak))
		return;

	if (!cv_cl_movement_proto.value)
		return;

	incoming = netbuffer->u.predcap;
	if (LSBF_LONG(incoming.magic) != CLPRED_MAGIC || incoming.version != CLPRED_PROTO_VERSION)
		return;

	if (!cl_pred.proto_active)
	{
		cl_pred.proto_active = true;
		cl_pred.cap_state = CLPRED_CAP_ACTIVE;
		cl_pred.server_max_repeat = min(incoming.max_repeat, (uint8_t)CLPRED_MAX_REPEAT);
		cl_pred.proto_start_seq = cl_pred.next_seq;

		CONS_Debug(DBG_NETPLAY, "[CL_PRED] Negotiated Option B protocol (v%u, max_repeat=%u, start_seq=%u)\n",
			incoming.version, cl_pred.server_max_repeat, cl_pred.proto_start_seq);
		if (cv_cl_movement_debug.value >= 1)
			CONS_Printf("[CL_PRED] Negotiated Option B protocol (v%u, max_repeat=%u, start_seq=%u)\n",
				incoming.version, cl_pred.server_max_repeat, cl_pred.proto_start_seq);
	}
}

void CL_PredHandleAck(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	tic_t starttic;
	uint8_t numtics, k;
	size_t needed;

	if (!client || node != servernode)
		return;

	if ((size_t)doomcom->datalength < BASEPACKETSIZE + offsetof(predack_pak, ack_seq))
		return;

	numtics = netbuffer->u.predack.numtics;
	if (numtics == 0 || numtics > CLPRED_MAX_ACK_TICS)
		return;

	needed = BASEPACKETSIZE + offsetof(predack_pak, ack_seq) + (size_t)numtics * sizeof(uint32_t);
	if ((size_t)doomcom->datalength < needed)
		return;

	starttic = (tic_t)LSBF_LONG(netbuffer->u.predack.starttic);

	for (k = 0; k < numtics; k++)
	{
		tic_t t = starttic + k;
		uint32_t slot = t % BACKUPTICS;
		cl_pred.tic_ack_tic[slot] = t;
		cl_pred.tic_ack_seq[slot] = LSBF_LONG(netbuffer->u.predack.ack_seq[k]);
		cl_pred.tic_ack_valid[slot] = true;
	}

	cl_pred.ack_packets_recv++;
	if (netbuffer->u.predack.flags & CLPRED_ACKF_UNDERRUN)
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
		cl_pred.next_seq = 1;

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

	// Push into prediction queue during active gameplay
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

		CL_PredUpdateShadow();
	}

	return seq;
}

dboolean CL_PredUseEnhancedCmd(void)
{
	return (cl_pred.proto_active && cv_cl_movement_proto.value && !splitscreen && cl_pred.tx_count > 0);
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
	netbuffer->u.clientpredpak.consistancy = *cons;

	head_seq = cl_pred.tx_history[0].seq;
	want_repeat = (uint8_t)min((int32_t)cv_cl_netrepeatinput.value, (int32_t)cl_pred.server_max_repeat);
	if (want_repeat > CLPRED_MAX_REPEAT)
		want_repeat = CLPRED_MAX_REPEAT;
	max_cmds = 1 + want_repeat;
	if (max_cmds > cl_pred.tx_count)
		max_cmds = cl_pred.tx_count;

	count = 0;
	for (k = 0; k < max_cmds; k++)
	{
		uint32_t expected_seq = SeqPrev(head_seq, k);
		uint32_t entry_seq = cl_pred.tx_history[k].seq;

		// Never repeat cmds sent before Option B activation or across sequence gaps
		if (entry_seq != expected_seq)
			break;
		if (k > 0 && cl_pred.proto_start_seq != 0 && !SeqAtLeast(entry_seq, cl_pred.proto_start_seq))
			break;

		G_MoveTiccmd(&netbuffer->u.clientpredpak.cmds[count], &cl_pred.tx_history[k].cmd, 1);
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
	uint32_t keep = 0;

	while (keep < cl_pred.queue_len && !SeqAfter(cl_pred.queue[keep].seq, ack_seq))
		keep++;

	if (keep > 0)
	{
		uint32_t rem = cl_pred.queue_len - keep;
		if (rem > 0)
			memmove(&cl_pred.queue[0], &cl_pred.queue[keep], rem * sizeof(cl_pred.queue[0]));
		cl_pred.queue_len = rem;
	}
}

static dboolean CL_PredCmdMatches(const ticcmd_t *a, const ticcmd_t *b)
{
	return (a->forwardmove == b->forwardmove
		&& a->sidemove == b->sidemove
		&& a->turning == b->turning
		&& a->angle == b->angle
		&& a->throwdir == b->throwdir
		&& a->aiming == b->aiming
		&& a->buttons == b->buttons
		&& a->latency == b->latency
		&& ((a->flags & ~TICCMD_RECEIVED) == (b->flags & ~TICCMD_RECEIVED)));
}

static void CL_PredFinishDeferredCamera0(void);

void CL_PredOnConfirmedTic(tic_t executed_tic, const ticcmd_t *confirmed_cmd)
{
	uint32_t slot;

	if (!client || !netgame || demo.playback)
	{
		CL_PredFinishDeferredCamera0();
		return;
	}

	if (gamestate != GS_LEVEL || !addedtogame || !playeringame[consoleplayer]
		|| leveltime == 0 || leveltime < cl_pred.last_leveltime)
	{
		CL_PredResetState();
		cl_pred.last_leveltime = (gamestate == GS_LEVEL) ? leveltime : 0;
		CL_PredFinishDeferredCamera0();
		return;
	}

	cl_pred.last_leveltime = leveltime;
	slot = executed_tic % BACKUPTICS;

	if (cl_pred.proto_active && cl_pred.tic_ack_valid[slot] && cl_pred.tic_ack_tic[slot] == executed_tic)
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
	else if (confirmed_cmd && (confirmed_cmd->flags & TICCMD_RECEIVED))
	{
		// Option A vanilla-server fallback (or rare missed PT_PREDACK before next redundant ack)
		uint32_t idx;
		dboolean matched = false;

		for (idx = 0; idx < cl_pred.queue_len; idx++)
		{
			if (CL_PredCmdMatches(&cl_pred.queue[idx].cmd, confirmed_cmd))
			{
				uint32_t ack_seq = cl_pred.queue[idx].seq;
				if (SeqAfter(ack_seq, cl_pred.confirmed_seq))
					cl_pred.confirmed_seq = ack_seq;
				CL_PredPruneUpToSeq(ack_seq);
				cl_pred.fallback_ack_ticks++;
				matched = true;
				break;
			}
		}

		// Safety guard against unbounded queue growth if Option A misses a match during a spike
		if (!matched && cl_pred.queue_len > 20)
		{
			uint32_t drop = 0;
			while (drop < cl_pred.queue_len && (cl_pred.queue_len - drop) > 16)
				drop++;
			if (drop > 0)
			{
				uint32_t rem = cl_pred.queue_len - drop;
				memmove(&cl_pred.queue[0], &cl_pred.queue[drop], rem * sizeof(cl_pred.queue[0]));
				cl_pred.queue_len = rem;
			}
		}
	}

	CL_PredUpdateShadow();
	CL_PredFinishDeferredCamera0();

	if (cv_cl_movement_debug.value >= 2 && (executed_tic % TICRATE) == 0)
	{
		fixed_t err_xy = R_PointToDist2(cl_pred.smooth_err_x, cl_pred.smooth_err_y, 0, 0);
		fixed_t cam_s = (cl_pred.shadow_valid && camera[0].chase)
			? R_PointToDist2(camera[0].x, camera[0].y, cl_pred.shadow_mo.x, cl_pred.shadow_mo.y) : 0;
		fixed_t cam_c = (playeringame[consoleplayer] && players[consoleplayer].mo && camera[0].chase)
			? R_PointToDist2(camera[0].x, camera[0].y, players[consoleplayer].mo->x, players[consoleplayer].mo->y) : 0;
		CONS_Printf("[CL_PRED] proto=%s tic=%u sent=%u conf=%u qlen=%u maxq=%u exact=%u fall=%u ackpkts=%u ovf=%u svunder=%u pred=%u dxy=%d dz=%d dang=%u replays=%u rec=%u snap=%u errxy=%d maxrec=%d cams=%d camc=%d rngv=%u\n",
			cl_pred.proto_active ? "B" : "A",
			executed_tic,
			cl_pred.last_sent_seq,
			cl_pred.confirmed_seq,
			cl_pred.queue_len,
			cl_pred.max_in_flight,
			cl_pred.exact_ack_ticks,
			cl_pred.fallback_ack_ticks,
			cl_pred.ack_packets_recv,
			cl_pred.queue_overflows,
			cl_pred.server_underrun_flags,
			cl_pred.predicted_tics,
			cl_pred.last_delta_xy >> FRACBITS,
			cl_pred.last_delta_z >> FRACBITS,
			(uint32_t)(cl_pred.last_delta_angle >> ANGLETOFINESHIFT),
			cl_pred.shadow_replays,
			cl_pred.smooth_reconciles,
			cl_pred.smooth_snaps,
			err_xy >> FRACBITS,
			cl_pred.max_reconcile_xy >> FRACBITS,
			cam_s >> FRACBITS,
			cam_c >> FRACBITS,
			cl_pred.rng_violations);
	}
}

// =========================================================================
// Step 3: Read-only shadow kart physics replay & exactness self-test
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
	tic_t start_leveltime,
	const ticcmd_t *cmds,
	uint32_t count,
	player_t *out_player,
	mobj_t *out_mo)
{
	player_t saved_player;
	mobj_t work_mo;
	tic_t saved_leveltime;
	uint32_t saved_rng[PRNUMCLASS];
	uint32_t k;
	int32_t c;

	I_Assert(!cl_pred_active_flag);
	I_Assert(base_player != NULL && base_mo != NULL && out_player != NULL && out_mo != NULL);

	saved_player = players[consoleplayer];
	saved_leveltime = leveltime;
	for (c = 0; c < PRNUMCLASS; c++)
		saved_rng[c] = P_GetRandSeed((pr_class_t)c);

	cl_pred_active_flag = true;
	P_PredPushMapState();

	// Run directly on players[consoleplayer] so any (player - players) index
	// arithmetic inside kart helpers resolves to consoleplayer cleanly.
	players[consoleplayer] = *base_player;
	work_mo = *base_mo;
	CL_PredDetachShadowMobj(&work_mo, &players[consoleplayer]);
	players[consoleplayer].mo = &work_mo;

	for (k = 0; k < count; k++)
	{
		ticcmd_t *pcmd = &players[consoleplayer].cmd;
		leveltime = start_leveltime + k;

		*pcmd = cmds[k];
		pcmd->latency = min(((leveltime & TICCMD_LATENCYMASK) - pcmd->latency) & TICCMD_LATENCYMASK, MAXPREDICTTICS - 1);

		P_PredPlayerThink(&players[consoleplayer]);
		P_PredPlayerMobjThink(&work_mo);

		// Mirror P_PlayerAfterThink physics tail
		if (P_IsObjectOnGround(&work_mo))
		{
			players[consoleplayer].outrun = 0;
			work_mo.pmomz = 0;
		}
		if (players[consoleplayer].onconveyor != 1 || !P_IsObjectOnGround(&work_mo))
		{
			players[consoleplayer].onconveyor = 0;
		}

		players[consoleplayer].oldcmd = *pcmd;
	}

	*out_player = players[consoleplayer];
	*out_mo = work_mo;
	out_player->mo = out_mo;
	out_mo->player = out_player;

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

#define CLPRED_SNAP_THRESHOLD (256 * FRACUNIT)

static void CL_PredCapturePose(const player_t *p, const mobj_t *mo, struct cl_pred_pose_s *out)
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

static void CL_PredCommitVisualPose(const player_t *confirmed_p, uint32_t head_seq)
{
	struct cl_pred_pose_s next_pose;
	dboolean force_snap;

	CL_PredCapturePose(&cl_pred.shadow_player, &cl_pred.shadow_mo, &next_pose);

	force_snap = (!cl_pred.pose_new.valid
		|| !cv_cl_movement_smooth.value
		|| confirmed_p->mo->resetinterp
		|| P_PlayerInPain(confirmed_p));

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
	else if (head_seq != cl_pred.last_head_seq)
	{
		// Normal forward 1-tic advance along local input stream
		cl_pred.pose_old = cl_pred.pose_new;
		cl_pred.pose_new = next_pose;
	}
	else
	{
		// Same-horizon reconciliation after authoritative server tic confirmation
		fixed_t dx = cl_pred.pose_new.x - next_pose.x;
		fixed_t dy = cl_pred.pose_new.y - next_pose.y;
		fixed_t dz = cl_pred.pose_new.z - next_pose.z;
		int32_t dang = (int32_t)(cl_pred.pose_new.angle - next_pose.angle);
		int32_t ddang = (int32_t)(cl_pred.pose_new.drawangle - next_pose.drawangle);

		if (dx || dy || dz || dang || ddang)
		{
			fixed_t err_xy = R_PointToDist2(dx, dy, 0, 0);
			if (err_xy > cl_pred.max_reconcile_xy)
				cl_pred.max_reconcile_xy = err_xy;

			if (err_xy > CLPRED_SNAP_THRESHOLD || abs(dz) > CLPRED_SNAP_THRESHOLD)
			{
				cl_pred.pose_old = next_pose;
				cl_pred.pose_new = next_pose;
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
				// placing the delta into smooth_err for C0-continuous decay.
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

				cl_pred.pose_new = next_pose;

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
			// Exact match: keep pose_old intact and refresh latest visual attributes
			cl_pred.pose_new = next_pose;
		}
	}

	cl_pred.last_head_seq = head_seq;
}

void CL_PredUpdateShadow(void)
{
	const player_t *p;
	uint32_t maxtics, n, i, head_seq;
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

	head_seq = (n > 0) ? cl_pred.queue[n - 1].seq : cl_pred.confirmed_seq;

	if (n == 0)
	{
		cl_pred.shadow_player = *p;
		cl_pred.shadow_mo = *p->mo;
		CL_PredDetachShadowMobj(&cl_pred.shadow_mo, &cl_pred.shadow_player);
		cl_pred.shadow_player.mo = &cl_pred.shadow_mo;
		cl_pred.shadow_valid = true;
		cl_pred.predicted_tics = 0;
		cl_pred.last_delta_xy = 0;
		cl_pred.last_delta_z = 0;
		cl_pred.last_delta_angle = 0;
		CL_PredCommitVisualPose(p, head_seq);
		return;
	}

	for (i = 0; i < n; i++)
		cmds[i] = cl_pred.queue[i].cmd;

	CL_PredReplayOnConsolePlayer(p, p->mo, leveltime, cmds, n,
		&cl_pred.shadow_player, &cl_pred.shadow_mo);

	cl_pred.shadow_valid = true;
	cl_pred.predicted_tics = n;
	cl_pred.shadow_replays++;
	cl_pred.last_delta_xy = R_PointToDist2(cl_pred.shadow_mo.x, cl_pred.shadow_mo.y, p->mo->x, p->mo->y);
	cl_pred.last_delta_z = abs(cl_pred.shadow_mo.z - p->mo->z);
	cl_pred.last_delta_angle = (angle_t)abs((int32_t)(cl_pred.shadow_mo.angle - p->mo->angle));
	CL_PredCommitVisualPose(p, head_seq);
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
				start_lt, cmds, horizon, &pred_p, &pred_mo);

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
					CONS_Printf("[CL_PRED_SELFTEST] MISMATCH lt=%u H=%u dx=%d dy=%d dz=%d dang=%u dmx=%d dmy=%d dmz=%d ddrift=%d dchg=%d (spd=%d drift=%d chg=%d)\n",
						leveltime, horizon,
						pred_mo.x - p->mo->x, pred_mo.y - p->mo->y, pred_mo.z - p->mo->z,
						ang_err,
						pred_mo.momx - p->mo->momx, pred_mo.momy - p->mo->momy, pred_mo.momz - p->mo->momz,
						(int32_t)pred_p.drift - (int32_t)p->drift,
						(int32_t)pred_p.driftcharge - (int32_t)p->driftcharge,
						p->speed >> FRACBITS, p->drift, p->driftcharge);
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
// Step 4: Visual pose interpolation, smooth error decay, chase cam & HUD
// =========================================================================

static inline fixed_t CL_PredLerpFixed(fixed_t from, fixed_t to, fixed_t frac)
{
	return from + FixedMul(frac, to - from);
}

static inline angle_t CL_PredLerpAngle(angle_t from, angle_t to, fixed_t frac)
{
	return from + (angle_t)FixedMul(frac, (fixed_t)(to - from));
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
	const struct cl_pred_pose_s *a = cl_pred.pose_old.valid ? &cl_pred.pose_old : &cl_pred.pose_new;
	const struct cl_pred_pose_s *b = &cl_pred.pose_new;

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

void CL_PredSetDeferCamera0(dboolean defer)
{
	cl_pred.defer_cam0 = defer;
	if (defer)
		cl_pred.cam0_was_deferred = false;
}

dboolean CL_PredIsDeferringCamera0(void)
{
	return cl_pred.defer_cam0;
}

void CL_PredMarkCamera0Deferred(void)
{
	cl_pred.cam0_was_deferred = true;
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

static void CL_PredFinishDeferredCamera0(void)
{
	if (!cl_pred.cam0_was_deferred)
		return;

	cl_pred.cam0_was_deferred = false;

	if (gamestate != GS_LEVEL || !playeringame[displayplayers[0]])
		return;

	if (camera[0].chase)
	{
		CL_PredStepChaseCamera(&players[displayplayers[0]], &camera[0], false);
	}

	if (rendermode != render_none && players[displayplayers[0]].mo)
	{
		const dboolean skybox = (skyboxmo[0] && cv_skybox.value);
		if (skyVisiblePerPlayer[0] && skybox)
			R_SkyboxFrame(0);
		R_SetupFrame(0, skybox);
	}
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

	snprintf(line1, sizeof(line1), "PRED %s [%s]  tics:%u/%d  q:%u  rec:%u  snap:%u",
		(cv_cl_movement.value && cl_pred.shadow_valid) ? "ON" : "OFF",
		cl_pred.proto_active ? "B" : "A",
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
		snprintf(line3, sizeof(line3), "seq:%u/%u  ex:%u  fl:%u  ovf:%u  und:%u",
			cl_pred.last_sent_seq,
			cl_pred.confirmed_seq,
			cl_pred.exact_ack_ticks,
			cl_pred.fallback_ack_ticks,
			cl_pred.queue_overflows,
			cl_pred.server_underrun_flags);
		V_DrawThinString(4, 24, V_SNAPTOTOP|V_SNAPTOLEFT|V_ALLOWLOWERCASE, line3);
	}
}

// =========================================================================
// Inspection & Console Command
// =========================================================================

dboolean CL_PredIsProtoActive(void)
{
	return cl_pred.proto_active;
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
				CONS_Printf("  node %d: Option B (used=%d, exec_seq=%u, recv_seq=%u, qlen=%u, pkts=%u, queued=%u, recov=%u, under=%u, trim=%u)\n",
					n, sv_pred[n].used_pred_cmd ? 1 : 0,
					sv_pred[n].last_exec_seq, sv_pred[n].last_recv_seq,
					sv_pred[n].queue_len, sv_pred[n].packets_recv,
					sv_pred[n].cmds_queued, sv_pred[n].cmds_recovered,
					sv_pred[n].underruns, sv_pred[n].trimmed);
				active++;
			}
			else
			{
				CONS_Printf("  node %d: Vanilla (Option A)\n", n);
			}
		}
		if (active == 0)
			CONS_Printf("  (no Option B nodes connected)\n");
	}
	else
	{
		CONS_Printf("[CL_PRED] proto=%s cl_movement=%d maxtics=%d repeat=%d sent=%u conf=%u qlen=%u maxq=%u exact=%u fall=%u ackpkts=%u ovf=%u svunder=%u shadow=%d pred=%u dxy=%d dz=%d replays=%u rngv=%u\n",
			cl_pred.proto_active ? "B" : "A",
			cv_cl_movement.value,
			cv_cl_movement_maxtics.value,
			cv_cl_netrepeatinput.value,
			cl_pred.last_sent_seq,
			cl_pred.confirmed_seq,
			cl_pred.queue_len,
			cl_pred.max_in_flight,
			cl_pred.exact_ack_ticks,
			cl_pred.fallback_ack_ticks,
			cl_pred.ack_packets_recv,
			cl_pred.queue_overflows,
			cl_pred.server_underrun_flags,
			cl_pred.shadow_valid ? 1 : 0,
			cl_pred.predicted_tics,
			cl_pred.last_delta_xy >> FRACBITS,
			cl_pred.last_delta_z >> FRACBITS,
			cl_pred.shadow_replays,
			cl_pred.rng_violations);
	}
}

void CL_PredRegisterVars(void)
{
	static dboolean registered = false;
	if (registered)
		return;
	registered = true;

	CV_RegisterVar(&cv_cl_movement);
	CV_RegisterVar(&cv_cl_movement_maxtics);
	CV_RegisterVar(&cv_cl_movement_smooth);
	CV_RegisterVar(&cv_cl_movement_smoothrate);
	CV_RegisterVar(&cv_cl_netrepeatinput);
	CV_RegisterVar(&cv_cl_movement_proto);
	CV_RegisterVar(&cv_cl_movement_debug);
	CV_RegisterVar(&cv_cl_movement_selftest);

	COM_AddCommand("cl_movement_status", Command_CLMovementStatus_f);

	SV_PredResetAll();
	CL_PredResetSession();
}
