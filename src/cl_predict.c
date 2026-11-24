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
#include "i_time.h"
#include "console.h"
#include "byteptr.h"

#include <stddef.h>
#include <string.h>

// =========================================================================
// CVars
// =========================================================================

static CV_PossibleValue_t cl_movement_maxtics_cons_t[] = {{0, "MIN"}, {16, "MAX"}, {0, NULL}};
static CV_PossibleValue_t cl_movement_smoothrate_cons_t[] = {{1, "MIN"}, {100, "MAX"}, {0, NULL}};
static CV_PossibleValue_t cl_netrepeatinput_cons_t[] = {{0, "MIN"}, {CLPRED_MAX_REPEAT, "MAX"}, {0, NULL}};
static CV_PossibleValue_t cl_movement_debug_cons_t[] = {{0, "MIN"}, {2, "MAX"}, {0, NULL}};

consvar_t cv_cl_movement = CVAR_INIT("cl_movement", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_cl_movement_maxtics = CVAR_INIT("cl_movement_maxtics", "8", CV_SAVE, cl_movement_maxtics_cons_t, NULL);
consvar_t cv_cl_movement_smooth = CVAR_INIT("cl_movement_smooth", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_cl_movement_smoothrate = CVAR_INIT("cl_movement_smoothrate", "35", CV_SAVE, cl_movement_smoothrate_cons_t, NULL);
consvar_t cv_cl_netrepeatinput = CVAR_INIT("cl_netrepeatinput", "2", CV_SAVE, cl_netrepeatinput_cons_t, NULL);
consvar_t cv_cl_movement_proto = CVAR_INIT("cl_movement_proto", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_cl_movement_debug = CVAR_INIT("cl_movement_debug", "0", 0, cl_movement_debug_cons_t, NULL);

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
} cl_pred_state_t;

static cl_pred_state_t cl_pred;

void CL_PredResetSession(void)
{
	memset(&cl_pred, 0, sizeof(cl_pred));
	cl_pred.next_seq = 1;
}

void CL_PredResetState(void)
{
	cl_pred.queue_len = 0;
	cl_pred.last_leveltime = 0;
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

void CL_PredOnConfirmedTic(tic_t executed_tic, const ticcmd_t *confirmed_cmd)
{
	uint32_t slot;

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

	if (cv_cl_movement_debug.value >= 2 && (executed_tic % TICRATE) == 0)
	{
		CONS_Printf("[CL_PRED] proto=%s tic=%u sent=%u conf=%u qlen=%u maxq=%u exact=%u fall=%u ackpkts=%u ovf=%u svunder=%u\n",
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
			cl_pred.server_underrun_flags);
	}
}

// =========================================================================
// Inspection & Console Command
// =========================================================================

dboolean CL_PredIsProtoActive(void)
{
	return cl_pred.proto_active;
}

uint32_t CL_PredGetQueueLen(void)
{
	return cl_pred.queue_len;
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
		CONS_Printf("[CL_PRED] proto=%s cl_movement=%d maxtics=%d repeat=%d sent=%u conf=%u qlen=%u maxq=%u exact=%u fall=%u ackpkts=%u ovf=%u svunder=%u\n",
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
			cl_pred.server_underrun_flags);
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

	COM_AddCommand("cl_movement_status", Command_CLMovementStatus_f);

	SV_PredResetAll();
	CL_PredResetSession();
}
