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
/// \file  d_net.c
/// \brief SRB2 network game communication and protocol, all OS independent parts.
//
///        Implement a Sliding window protocol without receiver window
///        (out of order reception)
///        This protocol uses a mix of "goback n" and "selective repeat" implementation
///        The NOTHING packet is sent when connection is idle to acknowledge packets

#include "doomdef.h"
#include "g_game.h"
#include "i_time.h"
#include "i_net.h"
#include "i_system.h"
#include "m_argv.h"
#include "d_net.h"
#include "w_wad.h"
#include "d_netfil.h"
#include "d_clisrv.h"
#include "z_zone.h"
#include "i_tcp.h"
#include "d_main.h" // srb2home
#include "stun.h"
#include "byteptr.h"
#include "monocypher/monocypher.h"

#include "qs22j.h"

//
// NETWORKING
//
// gametic is the tic about to (or currently being) run
// Server:
//   maketic is the tic that hasn't had control made for it yet
//   nettics is the tic for each node
//   firstticstosend is the lowest value of nettics
// Client:
//   neededtic is the tic needed by the client to run the game
//   firstticstosend is used to optimize a condition
// Normally maketic >= gametic > 0

#define FORCECLOSE 0x8000
tic_t connectiontimeout = (10*TICRATE);

/// \brief network packet
doomcom_t *doomcom = NULL;

#ifdef DEBUGFILE
FILE *debugfile = NULL; // put some net info in a file during the game
#endif

#define MAXREBOUND 8
static doomdata_t reboundstore[MAXREBOUND];
static int16_t reboundsize[MAXREBOUND];
static int32_t rebound_head, rebound_tail;

/// \brief bandwith of netgame
int32_t net_bandwidth;

/// \brief max length per packet
int16_t hardware_MAXPACKETLENGTH = 0;

dboolean (*I_NetGet)(void) = NULL;
void (*I_NetSend)(void) = NULL;
dboolean (*I_NetCanSend)(void) = NULL;
dboolean (*I_NetCanGet)(void) = NULL;
void (*I_NetCloseSocket)(void) = NULL;
void (*I_NetFreeNodenum)(int32_t nodenum) = NULL;
int8_t (*I_NetMakeNodewPort)(const char *address, const char* port) = NULL;
void (*I_NetRequestHolePunch)(int32_t node) = NULL;
void (*I_NetRegisterHolePunch)(void) = NULL;
dboolean (*I_NetOpenSocket)(void) = NULL;
dboolean (*I_Ban) (int32_t node) = NULL;
void (*I_ClearBans)(void) = NULL;
const char *(*I_GetNodeAddress) (int32_t node) = NULL;
uint32_t (*I_GetNodeAddressInt) (int32_t node) = NULL;
const char *(*I_GetBanAddress) (size_t ban) = NULL;
const char *(*I_GetBanMask) (size_t ban) = NULL;
const char *(*I_GetBanUsername) (size_t ban) = NULL;
const char *(*I_GetBanReason) (size_t ban) = NULL;
time_t (*I_GetUnbanTime) (size_t ban) = NULL;
dboolean (*I_SetBanAddress) (const char *address, const char *mask) = NULL;
dboolean (*I_SetBanUsername) (const char *username) = NULL;
dboolean (*I_SetBanReason) (const char *reason) = NULL;
dboolean (*I_SetUnbanTime) (time_t timestamp) = NULL;
dboolean (*I_IsExternalAddress) (const void *p) = NULL;
bannednode_t *bannednode = NULL;


// network stats
static tic_t statstarttic;
int32_t getbytes = 0;
int64_t sendbytes = 0;
uint8_t plcycle = 0;
uint32_t sentpackets[PACKETLOSSCYCLES][MAXNETNODES];
uint32_t lostpackets[PACKETLOSSCYCLES][MAXNETNODES];
static int32_t retransmit = 0, duppacket = 0;
static int32_t sendackpacket = 0, getackpacket = 0;
int32_t ticruned = 0, ticmiss = 0;

// globals
int32_t getbps, sendbps;
float lostpercent = 0.0f, duppercent = 0.0f, gamelostpercent = 0.0f;
int32_t packetheaderlength;

dboolean Net_GetNetStat(void)
{
	const tic_t t = I_GetTime();
	static int64_t oldsendbyte = 0;
	if (statstarttic+STATLENGTH <= t)
	{
		const tic_t df = t-statstarttic;
		const int64_t newsendbyte = sendbytes - oldsendbyte;
		sendbps = (int32_t)(newsendbyte*TICRATE)/df;
		getbps = (getbytes*TICRATE)/df;
		if (sendackpacket)
			lostpercent = 100.0f*(float)retransmit/(float)sendackpacket;
		else
			lostpercent = 0.0f;
		if (getackpacket)
			duppercent = 100.0f*(float)duppacket/(float)getackpacket;
		else
			duppercent = 0.0f;
		if (ticruned)
			gamelostpercent = 100.0f*(float)ticmiss/(float)ticruned;
		else
			gamelostpercent = 0.0f;

		ticmiss = ticruned = 0;
		oldsendbyte = sendbytes;
		getbytes = 0;
		sendackpacket = getackpacket = duppacket = retransmit = 0;
		statstarttic = t;

		return 1;
	}
	return 0;
}

// -----------------------------------------------------------------
// Some structs and functions for acknowledgement of packets
// -----------------------------------------------------------------
#define MAXACKPACKETS 96 // Minimum number of nodes (wat)
#define MAXACKTOSEND 96
#define URGENTFREESLOTNUM 10
#define ACKTOSENDTIMEOUT (TICRATE/11)

typedef struct
{
	uint8_t acknum;
	uint8_t nextacknum;
	uint8_t destinationnode; // The node to send the ack to
	tic_t senttime; // The time when the ack was sent
	uint16_t length; // The packet size
	uint16_t resentnum; // The number of times the ack has been resent
	union {
		int8_t raw[MAXPACKETLENGTH];
		doomdata_t data;
	} pak;
} ackpak_t;

typedef enum
{
	NF_CLOSE = 1, // Flag is set when connection is closing
	NF_TIMEOUT = 2, // Flag is set when the node got a timeout
} node_flags_t;

// Table of packets that were not acknowleged can be resent (the sender window)
static ackpak_t ackpak[MAXACKPACKETS];

typedef struct
{
	// ack return to send (like sliding window protocol)
	uint8_t firstacktosend;

	// automatically send keep alive packet when not enough trafic
	tic_t lasttimeacktosend_sent;
	// detect connection lost
	tic_t lasttimepacketreceived;

	// flow control: do not send too many packets with ack
	uint8_t remotefirstack;
	uint8_t nextacknum;

	uint8_t flags;
	uint8_t sendnum;
	uint8_t recvnum;
} netnode_t;

static netnode_t nodes[MAXNETNODES];
#define NODETIMEOUT 14

// return <0 if a < b (mod 256)
//         0 if a = n (mod 256)
//        >0 if a > b (mod 256)
// mnemonic: to use it compare to 0: cmpack(a,b)<0 is "a < b" ...
FUNCMATH static inline int32_t cmpack(uint8_t a, uint8_t b)
{
	return (int8_t)(a - b);
}

/** Sets freeack to a free acknum and copies the netbuffer in the ackpak table
  *
  * \param freeack  The address to store the free acknum at
  * \return True if a free acknum was found
  */
static dboolean GetFreeAcknum(uint8_t *freeack)
{
	netnode_t *node = &nodes[doomcom->remotenode];
	int32_t i, numfreeslot = 0;

	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (cmpack((uint8_t)((node->remotefirstack + MAXACKTOSEND) % 256), node->nextacknum) < 0)
	{
		DEBFILE(va("too fast %d %d\n",node->remotefirstack,node->nextacknum));
		return false;
	}

	for (i = 0; i < MAXACKPACKETS; i++)
		if (!ackpak[i].acknum)
		{
			// For low priority packets, make sure to let freeslots so urgent packets can be sent
			if (netbuffer->packettype >= PT_CANFAIL)
			{
				numfreeslot++;
				if (numfreeslot <= URGENTFREESLOTNUM)
					continue;
			}

			ackpak[i].acknum = node->nextacknum;
			ackpak[i].nextacknum = node->nextacknum;
			node->nextacknum++;
			if (!node->nextacknum)
				node->nextacknum++;
			ackpak[i].destinationnode = (uint8_t)(node - nodes);
			ackpak[i].length = doomcom->datalength;
			ackpak[i].senttime = I_GetTime();
			ackpak[i].resentnum = 0;
			memcpy(ackpak[i].pak.raw, netbuffer, ackpak[i].length);

			*freeack = ackpak[i].acknum;

			sendackpacket++; // For stat

			return true;
		}
#ifdef PARANOIA
	CONS_Debug(DBG_NETPLAY, "No more free ackpacket\n");
#endif
	if (netbuffer->packettype < PT_CANFAIL)
		I_Error("Connection lost\n");
	return false;
}

// Get a ack to send in the queue of this node
static uint8_t GetAcktosend(int32_t node)
{
	nodes[node].lasttimeacktosend_sent = I_GetTime();
	return nodes[node].firstacktosend;
}

static void RemoveAck(int32_t i)
{
	int32_t node = ackpak[i].destinationnode;
	DEBFILE(va("Remove ack %d\n",ackpak[i].acknum));
	ackpak[i].acknum = 0;
	if (nodes[node].flags & NF_CLOSE)
		Net_CloseConnection(node);
}

// We have got a packet, proceed the ack request and ack return
static dboolean Processackpak(void)
{
	int32_t i;
	dboolean goodpacket = true;
	netnode_t *node = &nodes[doomcom->remotenode];
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	// Received an ack return, so remove the ack in the list
	if (netbuffer->ackreturn && cmpack(node->remotefirstack, netbuffer->ackreturn) < 0)
	{
		node->remotefirstack = netbuffer->ackreturn;
		// Search the ackbuffer and free it
		for (i = 0; i < MAXACKPACKETS; i++)
			if (ackpak[i].acknum && ackpak[i].destinationnode == doomcom->remotenode
				&& cmpack(ackpak[i].acknum, netbuffer->ackreturn) <= 0)
			{
				RemoveAck(i);
			}
	}

	// Received a packet with ack, queue it to send the ack back
	if (netbuffer->ack)
	{
		uint8_t ack = netbuffer->ack;
		getackpacket++;
		if (cmpack(ack, node->firstacktosend) <= 0)
		{
			DEBFILE(va("Discard(1) ack %d (duplicated)\n", ack));
			duppacket++;
			goodpacket = false; // Discard packet (duplicate)
		}
		else
		{
			// Is a good packet so increment the acknowledge number,
			// Then search for a "hole" in the queue
			uint8_t nextfirstack = (uint8_t)(node->firstacktosend + 1);
			if (!nextfirstack)
				nextfirstack = 1;

			if (ack == nextfirstack)
			{
				node->firstacktosend = nextfirstack;
			}
			else // Out of order packet
			{
				// Don't increment firsacktosend, put it in asktosend queue
				// Will be incremented when the nextfirstack comes (code above)
				DEBFILE(va("out of order packet (%d expected)\n", nextfirstack));
				goodpacket = false;
			}
		}
	}
	return goodpacket;
}

void Net_ConnectionTimeout(int32_t node)
{
	// Don't timeout several times
	if (nodes[node].flags & NF_TIMEOUT)
		return;
	nodes[node].flags |= NF_TIMEOUT;

	// Send a very special packet to self (hack the reboundstore queue)
	// Main code will handle it
	reboundstore[rebound_head].packettype = PT_NODETIMEOUT;
	reboundstore[rebound_head].ack = 0;
	reboundstore[rebound_head].ackreturn = 0;
	reboundstore[rebound_head].u.textcmd[0] = (uint8_t)node;
	reboundsize[rebound_head] = (int16_t)(BASEPACKETSIZE + 1);
	rebound_head = (rebound_head+1) % MAXREBOUND;

	// Do not redo it quickly (if we do not close connection it is
	// for a good reason!)
	nodes[node].lasttimepacketreceived = I_GetTime();
}

// Resend the data if needed
void Net_AckTicker(void)
{
	int32_t i;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	for (i = 0; i < MAXACKPACKETS; i++)
	{
		const int32_t nodei = ackpak[i].destinationnode;
		netnode_t *node = &nodes[nodei];
		if (ackpak[i].acknum && ackpak[i].senttime + NODETIMEOUT < I_GetTime())
		{
			if (ackpak[i].resentnum > 20 && (node->flags & NF_CLOSE))
			{
				DEBFILE(va("ack %d sent 20 times so connection is supposed lost: node %d\n",
					i, nodei));
				Net_CloseConnection(nodei | FORCECLOSE);

				ackpak[i].acknum = 0;
				continue;
			}
			DEBFILE(va("Resend ack %d, %u<%d at %u\n", ackpak[i].acknum, ackpak[i].senttime,
				NODETIMEOUT, I_GetTime()));
			memcpy(netbuffer, ackpak[i].pak.raw, ackpak[i].length);
			ackpak[i].senttime = I_GetTime();
			ackpak[i].resentnum++;
			ackpak[i].nextacknum = node->nextacknum;
			netbuffer->packetindex = node->sendnum++;
			retransmit++; // For stat
			HSendPacket((int32_t)(node - nodes), false, ackpak[i].acknum,
				(size_t)(ackpak[i].length - BASEPACKETSIZE));
		}
	}

	for (i = 1; i < MAXNETNODES; i++)
	{
		// This is something like node open flag
		if (nodes[i].firstacktosend)
		{
			if (!(nodes[i].flags & NF_CLOSE)
				&& nodes[i].lasttimepacketreceived + connectiontimeout < I_GetTime())
			{
				Net_ConnectionTimeout(i);
			}
		}
	}
}

// Remove last packet received ack before resending the ackreturn
// (the higher layer doesn't have room, or something else ....)
void Net_UnAcknowledgePacket(int32_t node)
{
	DEBFILE(va("UnAcknowledge node %d\n", node));
	if (!node)
		return;
	nodes[node].firstacktosend--;
	if (!nodes[node].firstacktosend)
		nodes[node].firstacktosend = UINT8_MAX;
}

/** Checks if all acks have been received
  *
  * \return True if all acks have been received
  *
  */
static dboolean Net_AllAcksReceived(void)
{
	int32_t i;

	for (i = 0; i < MAXACKPACKETS; i++)
		if (ackpak[i].acknum)
			return false;

	return true;
}

/** Waits for all ackreturns
  *
  * \param timeout Timeout in seconds
  *
  */
void Net_WaitAllAckReceived(uint32_t timeout)
{
	tic_t tictac = I_GetTime();
	timeout = tictac + timeout*NEWTICRATE;

	HGetPacket();
	while (timeout > I_GetTime() && !Net_AllAcksReceived())
	{
		while (tictac == I_GetTime())
		{
			I_Sleep(cv_sleep.value);
			I_UpdateTime(cv_timescale.value);
		}
		tictac = I_GetTime();
		HGetPacket();
		Net_AckTicker();
	}
}

static void InitNode(netnode_t *node)
{
	node->firstacktosend = 0;
	node->nextacknum = 1;
	node->remotefirstack = 0;
	node->flags = 0;
	node->sendnum = 0;
	node->recvnum = 0;
}

static void InitAck(void)
{
	int32_t i;

	for (i = 0; i < MAXACKPACKETS; i++)
		ackpak[i].acknum = 0;

	for (i = 0; i < MAXNETNODES; i++)
	{
		InitNode(&nodes[i]);

		csprng(lastSentChallenge[i], sizeof(lastSentChallenge[i]));
		csprng(lastReceivedKey[i], sizeof(lastReceivedKey[i]));
	}
}

/** Removes all acks of a given packet type
  *
  * \param packettype The packet type to forget
  *
  */
void Net_AbortPacketType(uint8_t packettype)
{
	int32_t i;
	for (i = 0; i < MAXACKPACKETS; i++)
		if (ackpak[i].acknum && (ackpak[i].pak.data.packettype == packettype
			|| packettype == UINT8_MAX))
		{
			ackpak[i].acknum = 0;
		}
}

// -----------------------------------------------------------------
// end of acknowledge function
// -----------------------------------------------------------------

// remove a node, clear all ack from this node and reset askret
void Net_CloseConnection(int32_t node)
{
	int32_t i;
	dboolean forceclose = (node & FORCECLOSE) != 0;

	if (node == -1)
	{
		DEBFILE(M_GetText("Net_CloseConnection: node -1 detected!\n"));
		return; // nope, just ignore it
	}

	node &= ~FORCECLOSE;

	if (!node)
		return;

	if (node < 0 || node >= MAXNETNODES) // prevent invalid nodes from crashing the game
	{
		DEBFILE(va(M_GetText("Net_CloseConnection: invalid node %d detected!\n"), node));
		return;
	}

	nodes[node].flags |= NF_CLOSE;

	if (nodes[node].firstacktosend)
	{
		doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
		// send a PT_NOTHING back to acknowledge the packet
		netbuffer->packettype = PT_NOTHING;
		HSendPacket(node, false, 0, 0);
	}
	else
	{
		// if the connection hasn't acknowledged anything, it's possible for it to permanently occupy the node.
		// to prevent this, drop the connection.
		forceclose = true;
 	}

	// check if we are waiting for an ack from this node
	for (i = 0; i < MAXACKPACKETS; i++)
		if (ackpak[i].acknum && ackpak[i].destinationnode == node)
		{
			if (!forceclose)
				return; // connection will be closed when ack is returned
			else
				ackpak[i].acknum = 0;
		}

	InitNode(&nodes[node]);
	SV_AbortSendFiles(node);
	if (server)
		SV_AbortLuaFileTransfer(node);
	I_NetFreeNodenum(node);

	csprng(lastSentChallenge[node], sizeof(lastSentChallenge[node]));
	csprng(lastReceivedKey[node], sizeof(lastReceivedKey[node]));
}

//
// Checksum
//
static uint32_t NetbufferChecksum(void)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	uint32_t c = 0x1234567;
	const int32_t l = doomcom->datalength - 4;
	const uint8_t *buf = (uint8_t *)netbuffer + 4;
	int32_t i;

	for (i = 0; i < l; i++, buf++)
		c += (*buf) * (i+1);

	return LSBF_LONG(c);
}

#ifdef DEBUGFILE

static void fprintfstring(char *s, size_t len)
{
	int32_t mode = 0;
	size_t i;

	for (i = 0; i < len; i++)
		if (s[i] < 32)
		{
			if (!mode)
			{
				fprintf(debugfile, "[%d", (uint8_t)s[i]);
				mode = 1;
			}
			else
				fprintf(debugfile, ",%d", (uint8_t)s[i]);
		}
		else
		{
			if (mode)
			{
				fprintf(debugfile, "]");
				mode = 0;
			}
			fprintf(debugfile, "%c", s[i]);
		}
	if (mode)
		fprintf(debugfile, "]");
}

static void fprintfstringnewline(char *s, size_t len)
{
	fprintfstring(s, len);
	fprintf(debugfile, "\n");
}

/// \warning Keep this up-to-date if you add/remove/rename packet types
static const char *packettypename[NUMPACKETTYPE] =
{
	"NOTHING",
	"SERVERCFG",
	"CLIENTCMD",
	"CLIENTMIS",
	"CLIENT2CMD",
	"CLIENT2MIS",
	"NODEKEEPALIVE",
	"NODEKEEPALIVEMIS",
	"SERVERTICS",
	"SERVERREFUSE",
	"SERVERSHUTDOWN",
	"CLIENTQUIT",

	"ASKINFO",
	"SERVERINFO",
	"PLAYERINFO",
	"REQUESTFILE",
	"ASKINFOVIAMS",

	"WILLRESENDGAMESTATE",
	"CANRECEIVEGAMESTATE",
	"RECEIVEDGAMESTATE",

	"SENDINGLUAFILE",
	"ASKLUAFILE",
	"HASLUAFILE",

	"CLIENT3CMD",
	"CLIENT3MIS",
	"CLIENT4CMD",
	"CLIENT4MIS",
	"BASICKEEPALIVE",

	"FILEFRAGMENT",
	"FILEACK",
	"FILERECEIVED",

	"TEXTCMD",
	"TEXTCMD2",
	"TEXTCMD3",
	"TEXTCMD4",

	"TELLFILESNEEDED",
	"MOREFILESNEEDED",

	"CLIENTJOIN",
	"NODETIMEOUT",

	"LOGIN",
	"LOGINCHALLENGE",
	"LOGINAUTH",

	"PING",

	"SERVERINFOUPDATE",

	"SAY",

	"REQMAPQUEUE",

	"CLIENTKEY",
	"SERVERCHALLENGE",

	"CHALLENGEALL",
	"RESPONSEALL",
	"RESULTSALL"
};

static void DebugPrintpacket(const char *header)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	fprintf(debugfile, "%-12s (node %d,ack %d,ackret %d,size %d) type(%d) : %s\n",
		header, doomcom->remotenode, netbuffer->ack, netbuffer->ackreturn, doomcom->datalength,
		netbuffer->packettype, packettypename[netbuffer->packettype]);

	switch (netbuffer->packettype)
	{
		case PT_ASKINFO:
		case PT_ASKINFOVIAMS:
			fprintf(debugfile, "    time %u\n", (tic_t)LSBF_LONG(netbuffer->u.askinfo.time));
			break;
		case PT_CLIENTJOIN:
			fprintf(debugfile, "    number %d mode %d\n", netbuffer->u.clientcfg.localplayers,
				netbuffer->u.clientcfg.mode);
			break;
		case PT_SERVERTICS:
		{
			servertics_pak *serverpak = &netbuffer->u.serverpak;
			uint8_t *cmd = (uint8_t *)(&serverpak->cmds[serverpak->numslots * serverpak->numtics]);
			size_t ntxtcmd = &((uint8_t *)netbuffer)[doomcom->datalength] - cmd;

			fprintf(debugfile, "    firsttic %u ply %d tics %d ntxtcmd %s\n    ",
				(uint32_t)serverpak->starttic, serverpak->numslots, serverpak->numtics, sizeu1(ntxtcmd));
			/// \todo Display more readable information about net commands
			fprintfstringnewline((char *)cmd, ntxtcmd);
			/*fprintfstring((char *)cmd, 3);
			if (ntxtcmd > 4)
			{
				fprintf(debugfile, "[%s]", netxcmdnames[*((cmd) + 3) - 1]);
				fprintfstring(((char *)cmd) + 4, ntxtcmd - 4);
			}
			fprintf(debugfile, "\n");*/
			break;
		}
		case PT_CLIENTCMD:
		case PT_CLIENT2CMD:
		case PT_CLIENT3CMD:
		case PT_CLIENT4CMD:
		case PT_CLIENTMIS:
		case PT_CLIENT2MIS:
		case PT_CLIENT3MIS:
		case PT_CLIENT4MIS:
		case PT_NODEKEEPALIVE:
		case PT_NODEKEEPALIVEMIS:
			fprintf(debugfile, "    tic %4u resendfrom %u\n",
				(uint32_t)netbuffer->u.clientpak.client_tic,
				(uint32_t)netbuffer->u.clientpak.resendfrom);
			break;
		case PT_BASICKEEPALIVE:
			fprintf(debugfile, "    keep alive\n");
			break;
		case PT_TEXTCMD:
		case PT_TEXTCMD2:
		case PT_TEXTCMD3:
		case PT_TEXTCMD4:
			fprintf(debugfile, "    length %d\n", netbuffer->u.textcmd[0]);
			fprintf(debugfile, "[%s]", netxcmdnames[netbuffer->u.textcmd[1] - 1]);
			fprintfstringnewline((char *)netbuffer->u.textcmd + 2, netbuffer->u.textcmd[0] - 1);
			break;
		case PT_SERVERCFG:
			fprintf(debugfile, "    playerslots %d clientnode %d serverplayer %d "
				"gametic %u gamestate %d gametype %d modifiedgame %d\n",
				netbuffer->u.servercfg.totalslotnum, netbuffer->u.servercfg.clientnode,
				netbuffer->u.servercfg.serverplayer, (uint32_t)LSBF_LONG(netbuffer->u.servercfg.gametic),
				netbuffer->u.servercfg.gamestate, netbuffer->u.servercfg.gametype,
				netbuffer->u.servercfg.modifiedgame);
			break;
		case PT_SERVERINFO:
			fprintf(debugfile, "    '%s' player %d/%d, filenum %d, time %u \n",
				netbuffer->u.serverinfo.servername, netbuffer->u.serverinfo.numberofplayer,
				netbuffer->u.serverinfo.maxplayer,
				netbuffer->u.serverinfo.fileneedednum,
				(uint32_t)LSBF_LONG(netbuffer->u.serverinfo.time));
			fprintfstringnewline((char *)netbuffer->u.serverinfo.fileneeded,
				(uint8_t)((uint8_t *)netbuffer + doomcom->datalength
				- (uint8_t *)netbuffer->u.serverinfo.fileneeded));
			break;
		case PT_SERVERREFUSE:
			fprintf(debugfile, "    reason %s\n", netbuffer->u.serverrefuse.reason);
			break;
		case PT_FILEFRAGMENT: {
			filetx_pak *pak = (void*)&netbuffer->u.filetxpak;
			fprintf(debugfile, "    fileid %d datasize %d position %u\n",
				pak->fileid, (uint16_t)LSBF_SHORT(pak->size),
				(uint32_t)LSBF_LONG(pak->position));
			break;
		}
		case PT_REQUESTFILE:
		default: // write as a raw packet
			fprintfstringnewline((char *)netbuffer->u.textcmd,
				(uint8_t)((uint8_t *)netbuffer + doomcom->datalength - (uint8_t *)netbuffer->u.textcmd));
			break;
	}
}
#endif

#ifdef PACKETDROP
static int32_t packetdropquantity[NUMPACKETTYPE] = {0};
static int32_t packetdroprate = 0;

void Command_Drop(void)
{
	int32_t packetquantity;
	const char *packetname;
	size_t i;

	if (COM_Argc() < 2)
	{
		CONS_Printf("drop <packettype> [quantity]: drop packets\n"
					"drop reset: cancel all packet drops\n");
		return;
	}

	if (fasticmp(COM_Argv(1), "reset") || fasticmp(COM_Argv(1), "cancel") || fasticmp(COM_Argv(1), "stop"))
	{
		memset(packetdropquantity, 0, sizeof(packetdropquantity));
		return;
	}

	if (COM_Argc() >= 3)
	{
		packetquantity = atoi(COM_Argv(2));
		if (packetquantity <= 0 && COM_Argv(2)[0] != '0')
		{
			CONS_Printf("Invalid quantity\n");
			return;
		}
	}
	else
		packetquantity = -1;

	packetname = COM_Argv(1);

	if (fasticmp(packetname, "all") || fasticmp(packetname, "any"))
		for (i = 0; i < NUMPACKETTYPE; i++)
			packetdropquantity[i] = packetquantity;
	else
	{
		for (i = 0; i < NUMPACKETTYPE; i++)
			if (fasticmp(packetname, packettypename[i]))
			{
				packetdropquantity[i] = packetquantity;
				return;
			}

		CONS_Printf("Unknown packet name\n");
	}
}

void Command_Droprate(void)
{
	int32_t droprate;

	if (COM_Argc() < 2)
	{
		CONS_Printf("Packet drop rate: %d%%\n", packetdroprate);
		return;
	}

	droprate = atoi(COM_Argv(1));
	if ((droprate <= 0 && COM_Argv(1)[0] != '0') || droprate > 100)
	{
		CONS_Printf("Packet drop rate must be between 0 and 100!\n");
		return;
	}

	packetdroprate = droprate;
}

static dboolean ShouldDropPacket(void)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	return (packetdropquantity[netbuffer->packettype])
		|| (packetdroprate != 0 && rand() < (RAND_MAX * (packetdroprate / 100))) || packetdroprate == 100;
}
#endif

// Unused because Eidolon correctly pointed out that +512b on every packet was scary.
#ifdef SIGNGAMETRAFFIC
dboolean IsPacketSigned(int packettype)
{
	switch (packettype)
	{
		case PT_CLIENTCMD:
		case PT_CLIENT2CMD:
		case PT_CLIENT3CMD:
		case PT_CLIENT4CMD:
		case PT_CLIENTMIS:
		case PT_CLIENT2MIS:
		case PT_CLIENT3MIS:
		case PT_CLIENT4MIS:
		case PT_TEXTCMD:
		case PT_TEXTCMD2:
		case PT_TEXTCMD3:
		case PT_TEXTCMD4:
		case PT_LOGIN:
		case PT_ASKLUAFILE:
		case PT_SENDINGLUAFILE:
			return true;
		default:
			return false;
	}
}
#endif

//
// HSendPacket
//
dboolean HSendPacket(int32_t node, dboolean reliable, uint8_t acknum, size_t packetlength)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	doomcom->datalength = (int16_t)(packetlength + BASEPACKETSIZE);

#ifdef SIGNGAMETRAFFIC
	if (IsPacketSigned(netbuffer->packettype))
	{
		int i;

		for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
		{
			const void* message = &netbuffer->u;
			//CONS_Printf("Signing packet type %d of length %d\n", netbuffer->packettype, packetlength);
			crypto_eddsa_sign(netbuffer->signature[i], g_secret_key[i], message, packetlength);
		}

#ifdef DEVELOP
		if (cv_badtraffic.value)
		{
			CV_AddValue(&cv_badtraffic, -1);
			CONS_Alert(CONS_WARNING, "cv_badtraffic enabled, scrubbing signature from HSendPacket\n");
			memset(netbuffer->signature, 0, sizeof(netbuffer->signature));
		}

#endif
	}
	else
	{
		//CONS_Printf("NOT signing PT_%d of length %d, it doesn't need to be\n", netbuffer->packettype, packetlength);
		memset(netbuffer->signature, 0, sizeof(netbuffer->signature));
	}
#endif

	if (node == 0) // Packet is to go back to us
	{
		if ((rebound_head+1) % MAXREBOUND == rebound_tail)
		{
#ifdef PARANOIA
			CONS_Debug(DBG_NETPLAY, "No more rebound buf\n");
#endif
			return false;
		}
		netbuffer->ack = netbuffer->ackreturn = 0; // don't hold over values from last packet sent/received
		memcpy(&reboundstore[rebound_head], netbuffer,
			doomcom->datalength);
		reboundsize[rebound_head] = doomcom->datalength;
		rebound_head = (rebound_head+1) % MAXREBOUND;
#ifdef DEBUGFILE
		if (debugfile)
		{
			doomcom->remotenode = (int16_t)node;
			DebugPrintpacket("SENDLOCAL");
		}
#endif
		return true;
	}

	if (!netgame)
		I_Error("Tried to transmit to another node");

	// do this before GetFreeAcknum because this function backups
	// the current packet
	doomcom->remotenode = (int16_t)node;
	if (doomcom->datalength <= 0)
	{
		DEBFILE("HSendPacket: nothing to send\n");
#ifdef DEBUGFILE
		if (debugfile)
			DebugPrintpacket("TRISEND");
#endif
		return false;
	}

	if (node >= 0 && node < MAXNETNODES) // Can be a broadcast
		netbuffer->ackreturn = GetAcktosend(node);
	else
		netbuffer->ackreturn = 0;
	if (reliable)
	{
		if (!GetFreeAcknum(&netbuffer->ack))
			return false;
	}
	else
		netbuffer->ack = acknum;

	if (node >= 0 && node < MAXNETNODES) // Can be a broadcast
		netbuffer->packetindex = nodes[doomcom->remotenode].sendnum++;
	netbuffer->checksum = NetbufferChecksum();
	sendbytes += packetheaderlength + doomcom->datalength; // For stat

#ifdef PACKETDROP
	// Simulate internet :)
	//if (rand() >= (int32_t)(RAND_MAX * (PACKETLOSSRATE / 100.f)))
	if (!ShouldDropPacket())
	{
#endif
#ifdef DEBUGFILE
		if (debugfile)
			DebugPrintpacket("SENT");
#endif
		I_NetSend();
#ifdef PACKETDROP
	}
	else
	{
		if (packetdropquantity[netbuffer->packettype] > 0)
			packetdropquantity[netbuffer->packettype]--;
#ifdef DEBUGFILE
		if (debugfile)
			DebugPrintpacket("NOT SENT");
#endif
	}
#endif

	return true;
}

//
// HGetPacket
// Returns false if no packet is waiting
// Check Datalength and checksum
//
dboolean HGetPacket(void)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	// Get a packet from self
	if (rebound_tail != rebound_head)
	{
		memcpy(netbuffer, &reboundstore[rebound_tail], reboundsize[rebound_tail]);
		doomcom->datalength = reboundsize[rebound_tail];
		if (netbuffer->packettype == PT_NODETIMEOUT)
			doomcom->remotenode = netbuffer->u.textcmd[0];
		else
			doomcom->remotenode = 0;

		rebound_tail = (rebound_tail+1) % MAXREBOUND;
#ifdef DEBUGFILE
		if (debugfile)
			DebugPrintpacket("GETLOCAL");
#endif
		return true;
	}

	if (!netgame)
		return false;

	while(true)
	{
		//nodejustjoined = I_NetGet();
		I_NetGet();

		if (doomcom->remotenode == -1) // No packet received
			return false;

		getbytes += packetheaderlength + doomcom->datalength; // For stat

		if (doomcom->remotenode >= MAXNETNODES)
		{
			DEBFILE(va("Received packet from node %d!\n", doomcom->remotenode));
			continue;
		}

		nodes[doomcom->remotenode].lasttimepacketreceived = I_GetTime();

		if (netbuffer->checksum != NetbufferChecksum())
		{
			DEBFILE("Bad packet checksum\n");
			// Do not disconnect or anything, just ignore the packet.
			// Bad checksums with UDP tend to happen very scarcely
			// so they are not normally an issue.
			continue;
		}

#ifdef DEBUGFILE
		if (debugfile)
			DebugPrintpacket("GET");
#endif

		/*// If a new node sends an unexpected packet, just ignore it
		if (nodejustjoined && server
			&& !(netbuffer->packettype == PT_ASKINFO
				|| netbuffer->packettype == PT_SERVERINFO
				|| netbuffer->packettype == PT_PLAYERINFO
				|| netbuffer->packettype == PT_REQUESTFILE
				|| netbuffer->packettype == PT_ASKINFOVIAMS
				|| netbuffer->packettype == PT_CLIENTJOIN))
		{
			DEBFILE(va("New node sent an unexpected %s packet\n", packettypename[netbuffer->packettype]));
			//CONS_Alert(CONS_NOTICE, "New node sent an unexpected %s packet\n", packettypename[netbuffer->packettype]);
			Net_CloseConnection(doomcom->remotenode | FORCECLOSE);
			continue;
		}*/

		// Proceed the ack and ackreturn field
		if (!Processackpak())
			continue; // discarded (duplicated)

		// measure packet loss
		if ((int8_t)(netbuffer->packetindex - nodes[doomcom->remotenode].recvnum) <= 0)
		{
			// got out of order packet, so compensate
			lostpackets[plcycle][doomcom->remotenode]--;
		}
		else
		{
			sentpackets[plcycle][doomcom->remotenode] += (int8_t)(netbuffer->packetindex - nodes[doomcom->remotenode].recvnum);
			lostpackets[plcycle][doomcom->remotenode] += (int8_t)(netbuffer->packetindex - nodes[doomcom->remotenode].recvnum) - 1;
			nodes[doomcom->remotenode].recvnum = netbuffer->packetindex;
		}

		// A packet with just ackreturn
		if (netbuffer->packettype == PT_NOTHING)
			continue;
		break;
	}

	return true;
}

static dboolean Internal_Get(void)
{
	doomcom->remotenode = -1;
	return false;
}

FUNCNORETURN static ATTRNORETURN void Internal_Send(void)
{
	I_Error("Send without netgame\n");
}

static void Internal_FreeNodenum(int32_t nodenum)
{
	(void)nodenum;
}

static char *I_NetSplitAddress(char *host, char **port)
{
	dboolean v4 = (host[0] != '[');

	host = strtok(host, v4 ? ":" : "[]");

	if (port)
		*port = strtok(NULL, ":");

	return host;
}

int8_t I_NetMakeNode(const char *hostname)
{
	int8_t newnode = -1;
	if (I_NetMakeNodewPort)
	{
		char *localhostname = strdup(hostname);
		char *port;
		if (!localhostname)
			return newnode;

		// retrieve portnum from address!
		hostname = I_NetSplitAddress(localhostname, &port);

		newnode = I_NetMakeNodewPort(hostname, port);
		free(localhostname);
	}
	return newnode;
}

void D_SetDoomcom(void)
{
	if (doomcom) return;
	doomcom = Z_Calloc(sizeof (doomcom_t), PU_STATIC, NULL);
	doomcom->id = DOOMCOM_ID;
	doomcom->numslots = doomcom->numnodes = 1;
	doomcom->gametype = 0;
	doomcom->consoleplayer = 0;
	doomcom->extratics = 0;
}

//
// D_CheckNetGame
// Works out player numbers among the net participants
//
dboolean D_CheckNetGame(void)
{
	dboolean ret = false;

	InitAck();
	rebound_tail = rebound_head = 0;

	statstarttic = I_GetTime();

	I_NetGet = Internal_Get;
	I_NetSend = Internal_Send;
	I_NetCanSend = NULL;
	I_NetCloseSocket = NULL;
	I_NetFreeNodenum = Internal_FreeNodenum;
	I_NetMakeNodewPort = NULL;

	hardware_MAXPACKETLENGTH = MAXPACKETLENGTH;
	net_bandwidth = 30000;
	// I_InitNetwork sets doomcom and netgame
	// check and initialize the network driver
	multiplayer = false;

	// only dos version with external driver will return true
	netgame = I_InitNetwork();
	if (!netgame && !I_NetOpenSocket)
	{
		D_SetDoomcom();
		netgame = I_InitTcpNetwork();
	}

	if (netgame)
		ret = true;
	if (client && netgame)
		netgame = false;
	server = true; // WTF? server always true???
		// no! The deault mode is server. Client is set elsewhere
		// when the client executes connect command.
	doomcom->ticdup = 1;

	if (M_CheckParm("-extratic"))
	{
		if (M_IsNextParm())
			doomcom->extratics = (int16_t)atoi(M_GetNextParm());
		else
			doomcom->extratics = 1;
		CONS_Printf(M_GetText("Set extratics to %d\n"), doomcom->extratics);
	}

	if (M_CheckParm("-bandwidth"))
	{
		if (M_IsNextParm())
		{
			net_bandwidth = atoi(M_GetNextParm());
			if (net_bandwidth < 1000)
				net_bandwidth = 1000;
			if (net_bandwidth > 100000)
				hardware_MAXPACKETLENGTH = MAXPACKETLENGTH;
			CONS_Printf(M_GetText("Network bandwidth set to %d\n"), net_bandwidth);
		}
		else
			I_Error("usage: -bandwidth <byte_per_sec>");
	}

#if 0
	software_MAXPACKETLENGTH = hardware_MAXPACKETLENGTH;
#else
	// MTU, IPV4, UDP header.
	// 1500 - 20 - 8;
	software_MAXPACKETLENGTH = 1000;
#endif
	if (M_CheckParm("-packetsize"))
	{
		if (M_IsNextParm())
		{
			int32_t p = atoi(M_GetNextParm());
			if (p < 75)
				p = 75;
			if (p > hardware_MAXPACKETLENGTH)
				p = hardware_MAXPACKETLENGTH;
			software_MAXPACKETLENGTH = (uint16_t)p;
		}
		else
			I_Error("usage: -packetsize <bytes_per_packet>");
	}

	if (netgame)
		multiplayer = true;

	if (doomcom->id != DOOMCOM_ID)
		I_Error("Doomcom buffer invalid!");
	if (doomcom->numnodes > MAXNETNODES)
		I_Error("Too many nodes (%d), max:%d", doomcom->numnodes, MAXNETNODES);

#ifdef DEBUGFILE
	if (M_CheckParm("-debugfile"))
	{
		char filename[21];
		int32_t k = doomcom->consoleplayer - 1;
		if (M_IsNextParm())
			k = atoi(M_GetNextParm()) - 1;
		while (!debugfile && k < MAXPLAYERS)
		{
			k++;
			sprintf(filename, "debug%d.txt", k);
			debugfile = fopen(va("%s" PATHSEP "%s", srb2home, filename), "w");
		}
		if (debugfile)
			CONS_Printf(M_GetText("debug output to: %s\n"), va("%s" PATHSEP "%s", srb2home, filename));
		else
			CONS_Alert(CONS_WARNING, M_GetText("cannot debug output to file %s!\n"), va("%s" PATHSEP "%s", srb2home, filename));
	}
#endif

	D_ClientServerInit();

	return ret;
}

struct pingcell
{
	int32_t num;
	int32_t ms;
	int32_t f;
};

static int pingcellcmp(const void *va, const void *vb)
{
	const struct pingcell *a, *b;
	a = va;
	b = vb;
	return ( a->ms - b->ms );
}

/*
New ping command formatted nicely to present ping in
ascending order. And with equally spaced columns.
The caller's ping is presented at the bottom too, for
convenience.
*/

void Command_Ping_f(void)
{
	struct pingcell pingv[MAXPLAYERS];
	int32_t           pingc;

	int name_width = 0;
	int    f_width = 0;
	int   ms_width = 0;

	int n;
	int32_t i;

	pingc = 0;
	for (i = 1; i < MAXPLAYERS; ++i)
	{
		if (playeringame[i])
		{
			int32_t ms;

			n = strlen(player_names[i]);
			if (n > name_width)
				name_width = n;

			n = playerpingtable[i];
			if (n > f_width)
				f_width = n;

			ms = (int32_t)(playerpingtable[i] * (1000.00f / TICRATE));
			n = ms;
			if (n > ms_width)
				ms_width = n;

			pingv[pingc].num = i;
			pingv[pingc].f   = playerpingtable[i];
			pingv[pingc].ms  = ms;
			pingc++;
		}
	}

	     if (f_width < 10)  f_width = 1;
	else if (f_width < 100) f_width = 2;
	else                    f_width = 3;

	     if (ms_width < 10)  ms_width = 1;
	else if (ms_width < 100) ms_width = 2;
	else                     ms_width = 3;

	qs22j(pingv, pingc, sizeof (struct pingcell), &pingcellcmp);

	for (i = 0; i < pingc; ++i)
	{
		CONS_Printf("%02d : %-*s %*d frames (%*d ms)\n",
				pingv[i].num,
				name_width, player_names[pingv[i].num],
				f_width,    pingv[i].f,
				ms_width,   pingv[i].ms);
	}

	if (!server && playeringame[consoleplayer])
	{
		CONS_Printf("\nYour ping is %d frames (%d ms)\n", playerpingtable[consoleplayer], (int32_t)(playerpingtable[i] * (1000.00f / TICRATE)));
	}
}

void D_CloseConnection(void)
{
	int32_t i;

	if (netgame)
	{
		// wait the ackreturn with timout of 5 Sec
		Net_WaitAllAckReceived(5);

		// close all connection
		for (i = 0; i < MAXNETNODES; i++)
			Net_CloseConnection(i|FORCECLOSE);

		InitAck();

		if (I_NetCloseSocket)
			I_NetCloseSocket();

		I_NetGet = Internal_Get;
		I_NetSend = Internal_Send;
		I_NetCanSend = NULL;
		I_NetCloseSocket = NULL;
		I_NetFreeNodenum = Internal_FreeNodenum;
		I_NetMakeNodewPort = NULL;
		netgame = false;
		addedtogame = false;
	}

	D_ResetTiccmds();
}
