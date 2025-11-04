// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  d_clisrv.h
/// \brief high level networking stuff

#ifndef __D_CLISRV__
#define __D_CLISRV__

#include "d_ticcmd.h"
#include "d_net.h"
#include "d_netcmd.h"
#include "d_net.h"
#include "tables.h"
#include "d_player.h"
#include "mserv.h"

#include "k_pwrlv.h" // PWRLV_NUMTYPES
#include "p_saveg.h" // NETSAVEGAMESIZE

#ifdef __cplusplus
extern "C" {
#endif

/*
The 'packet version' is used to distinguish packet formats.
This version is independent of VERSION and SUBVERSION. Different
applications may follow different packet versions.
*/
#define PACKETVERSION 2

// Network play related stuff.
// There is a data struct that stores network
//  communication related stuff, and another
//  one that defines the actual packets to
//  be transmitted.

#define MAXSERVERNAME 32
#define MAXSERVERCONTACT 350
#define MAXSERVERDESCRIPTION 1024
#define MAXSERVERDESCRIPTIONLINE 80
#define DEFAULTDESCSTRING "Welcome to my SRB2Kart server!\n"

// Sends over info via packets to client players.
void ServerInfoUpdateSend(void);

// Networking and tick handling related.
#define BACKUPTICS 512 // more than enough for most timeouts....
#define CLIENTBACKUPTICS 32
#define MAXTEXTCMD 512

// No. of tics your controls can be delayed by.

// TODO: Instead of storing a ton of extra cmds for gentlemens' delay,
// keep them in a linked-list, with timestamps to discard everything that's older than already sent.
// That will support any amount of lag, and be less wasteful for clients who don't use it.
// This just works as a quick implementation.
#define MAXGENTLEMENDELAY TICRATE

//
// Packet structure
//
typedef enum
{
	PT_NOTHING,       // To send a nop through the network. ^_~
	PT_SERVERCFG,     // Server config used in start game
	                  // (must stay 1 for backwards compatibility).
	                  // This is a positive response to a CLIENTJOIN request.
	PT_CLIENTCMD,     // Ticcmd of the client.
	PT_CLIENTMIS,     // Same as above with but saying resend from.
	PT_CLIENT2CMD,    // 2 cmds in the packet for splitscreen.
	PT_CLIENT2MIS,    // Same as above with but saying resend from
	PT_NODEKEEPALIVE, // Same but without ticcmd and consistancy
	PT_NODEKEEPALIVEMIS,
	PT_SERVERTICS,    // All cmds for the tic.
	PT_SERVERREFUSE,  // Server refuses joiner (reason inside).
	PT_SERVERSHUTDOWN,
	PT_CLIENTQUIT,    // Client closes the connection.

	PT_ASKINFO,       // Anyone can ask info of the server.
	PT_SERVERINFO,    // Send game & server info (gamespy).
	PT_PLAYERINFO,    // Send information for players in game (gamespy).
	PT_REQUESTFILE,   // Client requests a file transfer
	PT_ASKINFOVIAMS,  // Packet from the MS requesting info be sent to new client.
	                  // If this ID changes, update masterserver definition.

	PT_WILLRESENDGAMESTATE, // Hey Client, I am about to resend you the gamestate!
	PT_CANRECEIVEGAMESTATE, // Okay Server, I'm ready to receive it, you can go ahead.
	PT_RECEIVEDGAMESTATE,   // Thank you Server, I am ready to play again!

	PT_SENDINGLUAFILE, // Server telling a client Lua needs to open a file
	PT_ASKLUAFILE,     // Client telling the server they don't have the file
	PT_HASLUAFILE,     // Client telling the server they have the file

	// Add non-PT_CANFAIL packet types here to avoid breaking MS compatibility.

	// Kart-specific packets
	PT_CLIENT3CMD,    // 3P
	PT_CLIENT3MIS,
	PT_CLIENT4CMD,    // 4P
	PT_CLIENT4MIS,
	PT_BASICKEEPALIVE,// Keep the network alive during wipes, as tics aren't advanced and NetUpdate isn't called

	PT_CANFAIL,       // This is kind of a priority. Anything bigger than CANFAIL
	                  // allows HSendPacket(*, true, *, *) to return false.
	                  // In addition, this packet can't occupy all the available slots.

	PT_FILEFRAGMENT = PT_CANFAIL, // A part of a file.
	PT_FILEACK,
	PT_FILERECEIVED,

	PT_TEXTCMD,       // Extra text commands from the client.
	PT_TEXTCMD2,      // Splitscreen text commands.
	PT_TEXTCMD3,      // 3P
	PT_TEXTCMD4,      // 4P
	PT_CLIENTJOIN,    // Client wants to join; used in start game.
	PT_NODETIMEOUT,   // Packet sent to self if the connection times out.

	PT_TELLFILESNEEDED, // Client, to server: "what other files do I need starting from this number?"
	PT_MOREFILESNEEDED, // Server, to client: "you need these (+ more on top of those)"

	PT_LOGIN,         // Login attempt from the client.
	PT_LOGINCHALLENGE,// Challenge request sent to the client.
	PT_LOGINAUTH,     // Challenge response from the client.

	PT_PING,          // Packet sent to tell clients the other client's latency to server.

	PT_SERVERINFOUPDATE, // Update server info for clients.
	NUMPACKETTYPE
} packettype_t;

#ifdef PACKETDROP
void Command_Drop(void);
void Command_Droprate(void);
#endif
#ifdef _DEBUG
void Command_Numnodes(void);
#endif

// Client to server packet
struct clientcmd_pak
{
	UINT8 client_tic;
	UINT8 resendfrom;
	INT16 consistancy;
	ticcmd_t cmd;
} ATTRPACK;

// Splitscreen packet
// WARNING: must have the same format of clientcmd_pak, for more easy use
struct client2cmd_pak
{
	UINT8 client_tic;
	UINT8 resendfrom;
	INT16 consistancy;
	ticcmd_t cmd, cmd2;
} ATTRPACK;

// 3P Splitscreen packet
// WARNING: must have the same format of clientcmd_pak, for more easy use
struct client3cmd_pak
{
	UINT8 client_tic;
	UINT8 resendfrom;
	INT16 consistancy;
	ticcmd_t cmd, cmd2, cmd3;
} ATTRPACK;

// 4P Splitscreen packet
// WARNING: must have the same format of clientcmd_pak, for more easy use
struct client4cmd_pak
{
	UINT8 client_tic;
	UINT8 resendfrom;
	INT16 consistancy;
	ticcmd_t cmd, cmd2, cmd3, cmd4;
} ATTRPACK;

// Server to client packet
// this packet is too large
struct servertics_pak
{
	UINT8 starttic;
	UINT8 numtics;
	UINT8 numslots; // "Slots filled": Highest player number in use plus one.
	ticcmd_t cmds[45]; // Normally [BACKUPTIC][MAXPLAYERS] but too large
} ATTRPACK;

struct serverconfig_pak
{
	UINT8 version; // Different versions don't work
	UINT8 subversion; // Contains build version

	// Server launch stuffs
	UINT8 serverplayer;
	UINT8 totalslotnum; // "Slots": highest player number in use plus one.

	tic_t gametic;
	UINT8 clientnode;
	UINT8 gamestate;

	UINT8 gametype;
	UINT8 modifiedgame;
	boolean dedicated;

	char server_context[8]; // Unique context id, generated at server startup.

	// Discord info (always defined for net compatibility)
	UINT8 maxplayer;
	boolean allownewplayer;
	boolean discordinvites;

	char server_name[MAXSERVERNAME];
	char server_contact[MAXSERVERCONTACT];
	char server_description[MAXSERVERDESCRIPTION];
} ATTRPACK;

struct filetx_pak
{
	UINT8 fileid;
	UINT32 filesize;
	UINT8 iteration;
	UINT32 position;
	UINT16 size;
	UINT8 data[]; // Size is variable using hardware_MAXPACKETLENGTH
} ATTRPACK;

struct fileacksegment_t
{
	UINT32 start;
	UINT32 acks;
} ATTRPACK;

struct fileack_pak
{
	UINT8 fileid;
	UINT8 iteration;
	UINT8 numsegments;
	fileacksegment_t segments[];
} ATTRPACK;

#define MAXAPPLICATION 16

struct clientconfig_pak
{
	UINT8 _255;/* see serverinfo_pak */
	UINT8 packetversion;
	char application[MAXAPPLICATION];
	UINT8 version; // Different versions don't work
	UINT8 subversion; // Contains build version
	UINT8 localplayers;	// number of splitscreen players
	UINT8 mode;
	char names[MAXSPLITSCREENPLAYERS][MAXPLAYERNAME];
} ATTRPACK;

#define SV_SPEEDMASK 0x03		// used to send kartspeed
#define SV_DEDICATED 0x40		// server is dedicated
#define SV_LOTSOFADDONS 0x20	// flag used to ask for full file list in d_netfil

#define MAXFILENEEDED 915
#define MAX_MIRROR_LENGTH 256
// This packet is too large
struct serverinfo_pak
{
	/*
	In the old packet, 'version' is the first field. Now that field is set
	to 255 always, so older versions won't be confused with the new
	versions or vice-versa.
	*/
	UINT8 _255;
	UINT8 packetversion;
	char  application[MAXAPPLICATION];
	UINT8 version;
	UINT8 subversion;
	UINT8 commit[GIT_SHA_ABBREV];
	UINT8 numberofplayer;
	UINT8 maxplayer;
	UINT8 refusereason; // 0: joinable, 1: joins disabled, 2: full
	char gametypename[24];
	UINT8 modifiedgame;
	UINT8 cheatsenabled;
	UINT8 kartvars; // Previously isdedicated, now appropriated for our own nefarious purposes
	UINT8 fileneedednum;
	tic_t time;
	tic_t leveltime;
	char servername[MAXSERVERNAME];
	char maptitle[33];
	UINT64 maphash;
	UINT8 actnum;
	UINT8 iszone;
	char httpsource[MAX_MIRROR_LENGTH]; // HTTP URL to download from, always defined for compatibility
	INT16 avgpwrlv; // Kart avg power level
	UINT8 fileneeded[MAXFILENEEDED]; // is filled with writexxx (byteptr.h)
} ATTRPACK;

struct serverinfoupdate_pak
{
	char servername[MAXSERVERNAME];
	char servercontact[MAXSERVERCONTACT];
	char serverdescription[MAXSERVERDESCRIPTION];
};

struct serverrefuse_pak
{
	char reason[255];
} ATTRPACK;

struct askinfo_pak
{
	UINT8 version;
	tic_t time; // used for ping evaluation
} ATTRPACK;

struct msaskinfo_pak
{
	char clientaddr[22];
	tic_t time; // used for ping evaluation
} ATTRPACK;

// Shorter player information for external use.
struct plrinfo
{
	UINT8 num;
	char name[MAXPLAYERNAME+1];
	UINT8 team;
	UINT16 skin;
	UINT8 data; // Color is first four bits, hasflag, isit and issuper have one bit each, the last is unused.
	UINT32 score;
	UINT16 timeinserver; // In seconds.
} ATTRPACK;

struct filesneededconfig_pak
{
	INT32 first;
	UINT8 num;
	UINT8 more;
	UINT8 files[MAXFILENEEDED]; // is filled with writexxx (byteptr.h)
} ATTRPACK;

struct netinfo_pak
{
	UINT32 pingtable[MAXPLAYERS+1];
	UINT32 packetloss[MAXPLAYERS+1];
	UINT32 delay[MAXPLAYERS+1];
} ATTRPACK;

//
// Network packet data
//
struct doomdata_t
{
	UINT32 checksum;
	UINT8 ack; // If not zero the node asks for acknowledgement, the receiver must resend the ack
	UINT8 ackreturn; // The return of the ack number

	UINT8 packettype;
	UINT8 reserved; // Padding
	union
	{
		clientcmd_pak clientpak;
		client2cmd_pak client2pak;
		client3cmd_pak client3pak;
		client4cmd_pak client4pak;
		servertics_pak serverpak;
		serverconfig_pak servercfg;
		UINT8 textcmd[MAXTEXTCMD+2];
		char filetxpak[sizeof (filetx_pak)];
		char fileack[sizeof (fileack_pak)];
		UINT8 filereceived;
		clientconfig_pak clientcfg;
		char salt[9];
		UINT8 sha256sum[32];
		serverinfo_pak serverinfo;
		serverrefuse_pak serverrefuse;
		serverinfoupdate_pak serverinfoupdate;
		askinfo_pak askinfo;
		msaskinfo_pak msaskinfo;
		plrinfo playerinfo[MAXPLAYERINFO];
		INT32 filesneedednum;
		filesneededconfig_pak filesneededcfg;
		netinfo_pak netinfo;
	} u; // This is needed to pack diff packet types data together
} ATTRPACK;

#define MAXSERVERLIST (MAXNETNODES-1)
struct serverelem_t
{
	SINT8 node;
	serverinfo_pak info;
};

extern serverelem_t serverlist[MAXSERVERLIST];
extern UINT32 serverlistcount;
extern UINT32 serverlistultimatecount;
extern INT32 mapchangepending;

// Points inside doomcom
extern consvar_t cv_stunserver;
extern consvar_t cv_httpsource;
extern consvar_t cv_kicktime;

extern consvar_t cv_showjoinaddress;
extern consvar_t cv_playbackspeed;

extern consvar_t cv_connectawaittime;
extern consvar_t cv_serverinfoscreen;


#define BASEPACKETSIZE      offsetof(doomdata_t, u)
#define FILETXHEADER        offsetof(filetx_pak, data)
#define BASESERVERTICSSIZE  offsetof(doomdata_t, u.serverpak.cmds[0])

#define KICK_MSG_GO_AWAY     1
#define KICK_MSG_CON_FAIL    2
#define KICK_MSG_PLAYER_QUIT 3
#define KICK_MSG_TIMEOUT     4
#define KICK_MSG_BANNED      5
#define KICK_MSG_PING_HIGH   6
#define KICK_MSG_GRIEF       7
#define KICK_MSG_CUSTOM_KICK 8
#define KICK_MSG_CUSTOM_BAN  9

typedef enum
{
	KR_KICK = 1, //Kicked by server
	KR_PINGLIMIT, //Broke Ping Limit
	KR_SYNCH, //Synch Failure
	KR_TIMEOUT, //Connection Timeout
	KR_BAN, //Banned by server
	KR_LEAVE, //Quit the game
} kickreason_t;

/* the max number of name changes in some time period */
#define MAXNAMECHANGES (5)
#define NAMECHANGERATE (60*TICRATE)

extern boolean server;
extern boolean serverrunning;
#define client (!server)
extern boolean dedicated; // For dedicated server
extern boolean connectedtodedicated; // Client that is connected to a dedicated server.
extern UINT16 software_MAXPACKETLENGTH;
extern boolean acceptnewnode;
extern SINT8 servernode;
extern char connectedservername[MAXSERVERNAME];
extern char connectedservercontact[MAXSERVERCONTACT];
extern char connectedserverdescription[MAXSERVERDESCRIPTION];

void Command_Ping_f(void);
extern tic_t connectiontimeout;
extern tic_t jointimeout;
extern UINT16 pingmeasurecount;
extern UINT32 realpingtable[MAXPLAYERS];
extern UINT32 playerpingtable[MAXPLAYERS];
extern UINT32 playerpacketlosstable[MAXPLAYERS];
extern UINT32 playerdelaytable[MAXPLAYERS];
extern tic_t servermaxping;

extern SINT8 server_lagless;
extern consvar_t cv_mindelay;

extern consvar_t cv_netticbuffer, cv_allownewplayer, cv_maxplayers, cv_joindelay;
extern consvar_t cv_resynchattempts, cv_blamecfail;
extern consvar_t cv_maxsend, cv_noticedownload, cv_downloadspeed;

#ifdef VANILLAJOINNEXTROUND
extern consvar_t cv_joinnextround;
#endif

extern consvar_t cv_discordinvites;

// Used in d_net, the only dependence
tic_t ExpandTics(INT32 low, tic_t basetic);
void D_ClientServerInit(void);

// Initialise the other field
void RegisterNetXCmd(netxcmd_t id, void (*cmd_f)(UINT8 **p, INT32 playernum));
void SendNetXCmdForPlayer(UINT8 playerid, netxcmd_t id, const void *param, size_t nparam);
#define SendNetXCmd(id, param, nparam) SendNetXCmdForPlayer(0, id, param, nparam) // Shortcut for P1
void SendKick(UINT8 playernum, UINT8 msg);

typedef enum
{
	CL_SEARCHING,
	CL_CHECKFILES,
	CL_DOWNLOADFILES,
	CL_DOWNLOADFAILED,
	CL_ASKJOIN,
	CL_LOADFILES,
	CL_SETUPFILES,
	CL_WAITJOINRESPONSE,
	CL_DOWNLOADSAVEGAME,
	CL_CONNECTED,
	CL_ABORTED,
	CL_ASKFULLFILELIST,
	CL_CONFIRMCONNECT,
	CL_VIEWSERVER,
#ifdef HAVE_CURL
	CL_PREPAREHTTPFILES,
	CL_DOWNLOADHTTPFILES,
#endif
} cl_mode_t;

// Create any new ticcmds and broadcast to other players.
void NetKeepAlive(void);
void NetUpdate(void);

void SV_StartSinglePlayerServer(INT32 dogametype, boolean donetgame);
boolean SV_SpawnServer(void);
void SV_StopServer(void);
void SV_ResetServer(void);

/*--------------------------------------------------
	boolean K_AddBotFromServer(UINT16 skin, UINT8 difficulty, botStyle_e style, UINT8 *newplayernum);

		Adds a new bot, using a server-sided packet sent to all clients.
		Using regular K_AddBot wherever possible is better, but this is kept
		as a back-up measure if this is the only option.

	Input Arguments:-
		skin - Skin number that the bot will use.
		difficulty - Difficulty level this bot will use.
		style - Bot style to spawn this bot with, see botStyle_e.
		newplayernum - Pointer to the last valid player slot number.
			Is a pointer so that this function can be called multiple times to add more than one bot.

	Return:-
		true if a bot can be added via a packet later, otherwise false.
--------------------------------------------------*/

boolean K_AddBotFromServer(UINT16 skin, UINT8 difficulty, botStyle_e style, UINT8 *p);

void CL_AddSplitscreenPlayer(void);
void CL_RemoveSplitscreenPlayer(UINT8 p);
void CL_Reset(void);
void CL_ClearPlayer(INT32 playernum);
void CL_RemovePlayer(INT32 playernum, kickreason_t reason);
void CL_QueryServerList(msg_server_t *list);
void CL_UpdateServerList(void);
void CL_TimeoutServerList(void);
// Is there a game running
boolean Playing(void);
boolean InADedicatedServer(void);

void CopyCaretColors (char *p, const char *s, int n);

// Broadcasts special packets to other players
//  to notify of game exit
void D_QuitNetGame(void);

//? How many ticks to run?
boolean TryRunTics(tic_t realtic);

// extra data for lmps
// these functions scare me. they contain magic.
/*boolean AddLmpExtradata(UINT8 **demo_p, INT32 playernum);
void ReadLmpExtraData(UINT8 **demo_pointer, INT32 playernum);*/

// translate a playername in a player number return -1 if not found and
// print a error message in the console
SINT8 nametonum(const char *name);

extern char motd[254], server_context[8];
extern UINT8 playernode[MAXPLAYERS];
/* consoleplayer of this player (splitscreen) */
extern UINT8 playerconsole[MAXPLAYERS];
extern plrinfo playerinfo[MAXPLAYERINFO];
extern SINT8 joinnode;

#define SERVMUS_1 "SRVMS1"
#define SERVMUS_2 "SRVMS2"

void ChangeServMusic(const char* musname, boolean fallback, boolean keepPos);

INT32 GetClientMode(void);
void ChangeClientMode(INT32 mode);

INT32 D_NumPlayers(void);
boolean D_IsPlayerHumanAndGaming(INT32 player_number);
void D_ResetTiccmds(void);
ticcmd_t *D_LocalTiccmd(UINT8 ss);

tic_t GetLag(INT32 node);
UINT8 GetFreeXCmdSize(UINT8 playerid);

#ifndef __cplusplus
void D_SHA256PasswordPass(const UINT8 *buffer, size_t len, const char *salt, UINT8 dest[static 32]);
#endif

extern UINT8 hu_redownloadinggamestate;

extern char *reqpass;
extern char **adminpass;
extern UINT32 adminpasscount;

extern boolean hu_stopped;

//
// SRB2Kart
//

struct rewind_t {
	UINT8 savebuffer[NETSAVEGAMESIZE];
	tic_t leveltime;
	size_t demopos;

	ticcmd_t oldcmd[MAXPLAYERS];
	mobj_t oldghost[MAXPLAYERS];

	rewind_t *next;
};

void CL_ClearRewinds(void);
rewind_t *CL_SaveRewindPoint(size_t demopos);
rewind_t *CL_RewindToTime(tic_t time);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
