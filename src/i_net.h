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
/// \file  i_net.h
/// \brief System specific network interface stuff.

#ifndef __I_NET__
#define __I_NET__

#include <time.h>

#include "doomdef.h"
#include "command.h"

/// \brief program net id
#define DOOMCOM_ID (int32_t)0x12345678l

/// \def MAXPACKETLENGTH
/// For use in a LAN
#define MAXPACKETLENGTH 2063
/// \def INETPACKETLENGTH
///  For use on the internet
#define INETPACKETLENGTH 1450

#define NO_BAN_TIME (time_t)(-1)

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t hardware_MAXPACKETLENGTH;
extern int32_t net_bandwidth; // in byte/s

struct doomcom_t
{
	/// Supposed to be DOOMCOM_ID
	int32_t id;

	/// SRB2 executes an int32_t to execute commands.
	int16_t intnum;
	/// Communication between SRB2 and the driver.
	/// Is CMD_SEND or CMD_GET.
	int16_t command;
	/// Is dest for send, set by get (-1 = no packet).
	int16_t remotenode;

	/// Number of bytes in doomdata to be sent
	int16_t datalength;

	/// Info common to all nodes.
	/// Console is always node 0.
	int16_t numnodes;
	/// Flag: 1 = no duplication, 2-5 = dup for slow nets.
	int16_t ticdup;
	/// Flag: 1 = send a backup tic in every packet.
	int16_t extratics;
	/// kind of game
	int16_t gametype;
	/// Flag: -1 = new game, 0-5 = load savegame
	int16_t savegame;
	/// currect map
	int16_t map;

	/// Info specific to this node.
	int16_t consoleplayer;
	/// Number of "slots": the highest player number in use plus one.
	int16_t numslots;

	/// The packet data to be sent.
	char data[MAXPACKETLENGTH];
} ATTRPACK;

#define DOOMCOM_DATA(d) (doomdata_t *)&(d)->data

struct holepunch_t
{
	int32_t magic;
	int32_t addr;
	int16_t port;
} ATTRPACK;

#define HOLEPUNCH_DATA(d) (holepunch_t *)&(d)->data

extern doomcom_t *doomcom;

/**	\brief return packet in doomcom struct
*/
extern dboolean (*I_NetGet)(void);

/**	\brief ask to driver if there is data waiting
*/
extern dboolean (*I_NetCanGet)(void);

/**	\brief send packet within doomcom struct
*/
extern void (*I_NetSend)(void);

/**	\brief ask to driver if all is ok to send data now
*/
extern dboolean (*I_NetCanSend)(void);

/**	\brief	close a connection

	\param	nodenum	node to be closed

	\return	void


*/
extern void (*I_NetFreeNodenum)(int32_t nodenum);

/**	\brief	open a connection with specified address

	\param	address	address to connect to

	\return	number of node


*/
extern int8_t I_NetMakeNode(const char *address);

/**	\brief	open a connection with specified address and port

	\param	address	address to connect to

	\param	port	port to connect to

	\return	number of node


*/
extern int8_t (*I_NetMakeNodewPort)(const char *address, const char *port);

/**	\brief open connection
*/
extern dboolean (*I_NetOpenSocket)(void);

/**	\brief close all connections no more allow geting any packet
*/
extern void (*I_NetCloseSocket)(void);


/**	\brief send a hole punching request
*/
extern void (*I_NetRequestHolePunch)(int32_t node);

/**	\brief register this machine on the hole punching server
*/
extern void (*I_NetRegisterHolePunch)(void);


extern dboolean (*I_Ban) (int32_t node);
extern void (*I_ClearBans)(void);
extern const char *(*I_GetNodeAddress) (int32_t node);
extern uint32_t (*I_GetNodeAddressInt) (int32_t node);
extern const char *(*I_GetBanAddress) (size_t ban);
extern const char *(*I_GetBanMask) (size_t ban);
extern const char *(*I_GetBanUsername) (size_t ban);
extern const char *(*I_GetBanReason) (size_t ban);
extern time_t (*I_GetUnbanTime) (size_t ban);
extern dboolean (*I_SetBanAddress) (const char *address,const char *mask);
extern dboolean (*I_SetBanUsername) (const char *username);
extern dboolean (*I_SetBanReason) (const char *reason);
extern dboolean (*I_SetUnbanTime) (time_t timestamp);
extern dboolean (*I_IsExternalAddress) (const void *p);

struct bannednode_t
{
	size_t banid;
	time_t timeleft;
};
extern bannednode_t *bannednode;

/// \brief Called by D_SRB2Main to be defined by extern network driver
dboolean I_InitNetwork(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
