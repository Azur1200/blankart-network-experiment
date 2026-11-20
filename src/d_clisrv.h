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
#include "doomdef.h"
#include "tables.h"
#include "d_player.h"
#include "mserv.h"

#include "m_random.h" // PRNUMCLASS

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

#define HU_MAXMSGLEN 223

#define MAXSERVERMOTD 256
#define MAXSERVERNAME 32
#define MAXSERVERCONTACT 350
#define MAXSERVERDESCRIPTION 1024
#define MAXSERVERDESCRIPTIONLINE 80
#define DEFAULTDESCSTRING "Welcome to my BlanKart server!\n"

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
// anything that's explicitly set here is required to match what srb2ms-evo expects
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

	PT_ASKINFO = 12,    // Anyone can ask info of the server.
	PT_SERVERINFO = 13, // Send game & server info (gamespy).
	PT_PLAYERINFO = 14, // Send information for players in game (gamespy).
	PT_REQUESTFILE,   // Client requests a file transfer
	PT_ASKINFOVIAMS,  // Packet from the MS requesting info be sent to new client.
	                  // If this ID changes, update masterserver definition.

	PT_WILLRESENDGAMESTATE, // Hey Client, I am about to resend you the gamestate!
	PT_CANRECEIVEGAMESTATE, // Okay Server, I'm ready to receive it, you can go ahead.
	PT_RECEIVEDGAMESTATE,   // Thank you Server, I am ready to play again!

	PT_WILLDIAGNOSERNG, // Hey Client, I need to look at your seeds before we resend. Here are my seeds!
	PT_RNGDIAGNOSIS,	// Hey Server, I've got your request. Here are MY seeds!

	PT_SENDINGLUAFILE, // Server telling a client Lua needs to open a file
	PT_ASKLUAFILE,     // Client telling the server they don't have the file
	PT_HASLUAFILE,     // Client telling the server they have the file

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

	PT_TELLFILESNEEDED = 35, // Client, to server: "what other files do I need starting from this number?"
	PT_MOREFILESNEEDED = 36, // Server, to client: "you need these (+ more on top of those)"

	PT_CLIENTJOIN,    // Client wants to join; used in start game.
	PT_NODETIMEOUT,   // Packet sent to self if the connection times out.

	PT_LOGIN,         // Login attempt from the client.
	PT_LOGINCHALLENGE,// Challenge request sent to the client.
	PT_LOGINAUTH,     // Challenge response from the client.

	PT_PING,          // Packet sent to tell clients the other client's latency to server.

	PT_SERVERINFOUPDATE, // Update server info for clients.

	PT_SAY,				// "Hey server, please send this chat message to everyone via XD_SAY"
	PT_REQMAPQUEUE,		// Client requesting a roundqueue operation

	PT_CLIENTKEY,		// "Here's my public key"
	PT_SERVERCHALLENGE,		// "Prove it"

	PT_CHALLENGEALL,	// Prove to the other clients you are who you say you are, sign this random bullshit!
	PT_RESPONSEALL,		// OK, here is my signature on that random bullshit
	PT_RESULTSALL,		// Here's what everyone responded to PT_CHALLENGEALL with, if this is wrong or you don't receive it disconnect

	NUMPACKETTYPE
} packettype_t;

typedef enum
{
	SIGN_OK,
	SIGN_BADTIME, // Timestamp differs by too much, suspect reuse of an old challenge.
	SIGN_BADIP // Asked to sign the wrong IP by an external host, suspect reuse of another server's challenge.
} shouldsign_t;

typedef struct consistancy_s
{
	int16_t checksum; // Total consistancy checksum

	int32_t playerpos[2];
	int32_t position[3]; // Position checksum

	int32_t momentum[3]; // Speed checksum

	int32_t itemsum; // Player item checksum

	uint32_t rngseed[PRNUMCLASS];
	int32_t seed_checksum; // Sum of all RNG seeds

	// Object data checksum
	int32_t mobjtype;
	int32_t mobjangle;
	int32_t mobjflags;
	int32_t mobjflags2;
	int32_t mobjeflags;
	int32_t mobjstate;
	int32_t mobjtics;
	int32_t mobjsprite;
	int32_t mobjframe;
} consistancy_t;

void SPrintConsistancy(char *out, consistancy_t *c);

#ifdef PACKETDROP
void Command_Drop(void);
void Command_Droprate(void);
#endif
void Command_Numnodes(void);

// Client to server packet
struct clientcmd_pak
{
	uint8_t client_tic;
	uint8_t resendfrom;
	consistancy_t consistancy;
	ticcmd_t cmd;
} ATTRPACK;

// Splitscreen packet
// WARNING: must have the same format of clientcmd_pak, for more easy use
struct client2cmd_pak
{
	uint8_t client_tic;
	uint8_t resendfrom;
	consistancy_t consistancy;
	ticcmd_t cmd, cmd2;
} ATTRPACK;

// 3P Splitscreen packet
// WARNING: must have the same format of clientcmd_pak, for more easy use
struct client3cmd_pak
{
	uint8_t client_tic;
	uint8_t resendfrom;
	consistancy_t consistancy;
	ticcmd_t cmd, cmd2, cmd3;
} ATTRPACK;

// 4P Splitscreen packet
// WARNING: must have the same format of clientcmd_pak, for more easy use
struct client4cmd_pak
{
	uint8_t client_tic;
	uint8_t resendfrom;
	consistancy_t consistancy;
	ticcmd_t cmd, cmd2, cmd3, cmd4;
} ATTRPACK;

struct rngdiagnosis_pak
{
	uint32_t server_seeds[PRNUMCLASS];
	uint32_t client_seeds[PRNUMCLASS];
	uint32_t checksums[2];
	uint16_t client_id;
	uint8_t client_node;
	tic_t ticmemory;
	packettype_t nextpacket;
} ATTRPACK;

// Server to client packet
// this packet is too large
struct servertics_pak
{
	uint8_t starttic;
	uint8_t numtics;
	uint8_t numslots; // "Slots filled": Highest player number in use plus one.
	ticcmd_t cmds[45]; // Normally [BACKUPTIC][MAXPLAYERS] but too large
} ATTRPACK;

struct serverconfig_pak
{
	uint8_t version; // Different versions don't work
	uint8_t subversion; // Contains build version

	// Server launch stuffs
	uint8_t serverplayer;
	uint8_t totalslotnum; // "Slots": highest player number in use plus one.

	tic_t gametic;
	uint8_t clientnode;
	uint8_t gamestate;

	uint8_t gametype;
	uint8_t modifiedgame;
	dboolean dedicated;

	char server_context[8]; // Unique context id, generated at server startup.

	// Discord info (always defined for net compatibility)
	uint8_t maxplayer;
	dboolean allownewplayer;
	dboolean discordinvites;
} ATTRPACK;

struct filetx_pak
{
	uint8_t fileid;
	uint32_t filesize;
	uint8_t iteration;
	uint32_t position;
	uint16_t size;
	uint8_t data[]; // Size is variable using hardware_MAXPACKETLENGTH
} ATTRPACK;

struct fileacksegment_t
{
	uint32_t start;
	uint32_t acks;
} ATTRPACK;

struct fileack_pak
{
	uint8_t fileid;
	uint8_t iteration;
	uint8_t numsegments;
	fileacksegment_t segments[];
} ATTRPACK;

#define MAXAPPLICATION 16

struct clientconfig_pak
{
	uint8_t _255;/* see serverinfo_pak */
	uint8_t packetversion;
	char application[MAXAPPLICATION];
	uint8_t version; // Different versions don't work
	uint8_t subversion; // Contains build version
	uint8_t localplayers;	// number of splitscreen players
	uint8_t mode;
	char names[MAXSPLITSCREENPLAYERS][MAXPLAYERNAME];
	uint8_t availabilities[MAXAVAILABILITY];
	uint8_t challengeResponse[MAXSPLITSCREENPLAYERS][SIGNATURELENGTH];
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
	uint8_t _255;
	uint8_t packetversion;
	char  application[MAXAPPLICATION];
	uint8_t version;
	uint8_t subversion;
	uint8_t commit[GIT_SHA_ABBREV];
	uint8_t numberofplayer;
	uint8_t maxplayer;
	uint8_t refusereason; // 0: joinable, 1: joins disabled, 2: full
	char gametypename[24];
	uint8_t modifiedgame;
	uint8_t cheatsenabled;
	uint8_t kartvars; // Previously isdedicated, now appropriated for our own nefarious purposes
	uint8_t fileneedednum;
	tic_t time;
	tic_t leveltime;
	char servername[MAXSERVERNAME];
	char maptitle[33];
	uint64_t maphash;
	uint8_t actnum;
	uint8_t iszone;
	char httpsource[MAX_MIRROR_LENGTH]; // HTTP URL to download from, always defined for compatibility
	int16_t avgpwrlv; // Kart avg power level
	uint8_t fileneeded[MAXFILENEEDED]; // is filled with writexxx (byteptr.h)
} ATTRPACK;

struct serverinfoupdate_pak
{
	char servermotd[MAXSERVERMOTD];
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
	uint8_t version;
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
	uint8_t num;
	char name[MAXPLAYERNAME+1];
	uint8_t team;
	uint16_t skin;
	uint8_t data; // Color is first four bits, hasflag, isit and issuper have one bit each, the last is unused.
	uint32_t score;
	uint16_t timeinserver; // In seconds.
} ATTRPACK;

struct filesneededconfig_pak
{
	int32_t first;
	uint8_t num;
	uint8_t more;
	uint8_t files[MAXFILENEEDED]; // is filled with writexxx (byteptr.h)
} ATTRPACK;

struct reqmapqueue_pak
{
	uint16_t newmapnum;
	uint16_t newgametype;
	uint8_t flags;
	uint8_t source;
} ATTRPACK;

struct clientkey_pak
{
	uint8_t key[MAXSPLITSCREENPLAYERS][PUBKEYLENGTH];
} ATTRPACK;

struct serverchallenge_pak
{
	uint8_t secret[CHALLENGELENGTH];
} ATTRPACK;

struct challengeall_pak
{
	uint8_t secret[CHALLENGELENGTH];
} ATTRPACK;

struct responseall_pak
{
	uint8_t signature[MAXSPLITSCREENPLAYERS][SIGNATURELENGTH];
} ATTRPACK;

struct resultsall_pak
{
	// Maybe use a FAM in the future for space saving?
	uint8_t signature[MAXPLAYERS][SIGNATURELENGTH];
} ATTRPACK;

struct netinfo_pak
{
	uint32_t pingtable[MAXPLAYERS+1];
	uint32_t packetloss[MAXPLAYERS+1];
	uint32_t delay[MAXPLAYERS+1];
} ATTRPACK;

struct say_pak
{
	char message[HU_MAXMSGLEN + 1];
	uint8_t target;
	uint8_t flags;
	uint8_t source;
} ATTRPACK;

//
// Network packet data
//
struct doomdata_t
{
	uint32_t checksum;
	uint8_t ack; // If not zero the node asks for acknowledgement, the receiver must resend the ack
	uint8_t ackreturn; // The return of the ack number

	uint8_t packettype;
	uint8_t packetindex;
	union
	{
		// NOTICE: UPDATE THE COMMENT SIZES WHEN YOU CHANGE STUFF.
		clientcmd_pak clientpak; // 96 bytes
		client2cmd_pak client2pak; // 110 bytes
		client3cmd_pak client3pak; // 124 bytes
		client4cmd_pak client4pak; // 138 bytes
		servertics_pak serverpak; // 633 bytes
		rngdiagnosis_pak rngdiagnosis; // 412 bytes
		serverconfig_pak servercfg; // 33 bytes
		uint8_t textcmd[MAXTEXTCMD+2]; // 257 bytes
		char filetxpak[sizeof (filetx_pak)]; // 12 bytes
		char fileack[sizeof (fileack_pak)]; // 3 bytes
		uint8_t filereceived; // 1 byte
		clientconfig_pak clientcfg; // 378 bytes
		char salt[9]; // 9 bytes
		uint8_t sha256sum[32]; // 32 bytes
		serverinfo_pak serverinfo; // 1311 bytes
		serverrefuse_pak serverrefuse; // 255 bytes
		serverinfoupdate_pak serverinfoupdate; // 1662 bytes
		askinfo_pak askinfo; // 5 bytes
		msaskinfo_pak msaskinfo; // 26 bytes
		plrinfo playerinfo[MAXPLAYERINFO]; // 1056 bytes
		int32_t filesneedednum; // 4 bytes
		filesneededconfig_pak filesneededcfg; // 921 bytes
		netinfo_pak netinfo; // 396 bytes
		say_pak say; // 227 bytes
		reqmapqueue_pak reqmapqueue;			// 6 bytes. Formerly XD_REQMAPQUEUE
		clientkey_pak clientkey;				// 32 bytes
		serverchallenge_pak serverchallenge;	// 256 bytes
		//challengeall_pak challengeall;			// 256 bytes
		//responseall_pak responseall;			// 256 bytes
		//resultsall_pak resultsall;				// 1024 bytes (2048 bytes in 32P)
	} u; // This is needed to pack diff packet types data together
} ATTRPACK;

#define MAXSERVERLIST (MAXNETNODES-1)
struct serverelem_t
{
	int8_t node;
	serverinfo_pak info;
};

extern serverelem_t serverlist[MAXSERVERLIST];
extern uint32_t serverlistcount;
extern uint32_t serverlistultimatecount;
extern int32_t mapchangepending;

// Points inside doomcom
extern consvar_t cv_stunserver;
extern consvar_t cv_httpsource;
extern consvar_t cv_kicktime;

extern consvar_t cv_showjoinaddress;
extern consvar_t cv_shownodeip;
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
#define KICK_MSG_SIGFAIL     10

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

extern dboolean server;
extern dboolean serverrunning;
#define client (!server)
extern dboolean dedicated; // For dedicated server
extern dboolean connectedtodedicated; // Client that is connected to a dedicated server.
extern uint16_t software_MAXPACKETLENGTH;
extern dboolean acceptnewnode;
extern int8_t servernode;
extern char connectedservermotd[MAXSERVERMOTD];
extern char connectedservername[MAXSERVERNAME];
extern char connectedservercontact[MAXSERVERCONTACT];
extern char connectedserverdescription[MAXSERVERDESCRIPTION];
extern uint32_t ourIP;
extern uint8_t lastReceivedKey[MAXNETNODES][MAXSPLITSCREENPLAYERS][PUBKEYLENGTH];
extern uint8_t lastSentChallenge[MAXNETNODES][CHALLENGELENGTH];
extern uint8_t lastChallengeAll[CHALLENGELENGTH];
extern uint8_t lastReceivedSignature[MAXPLAYERS][SIGNATURELENGTH];
extern uint8_t knownWhenChallenged[MAXPLAYERS][PUBKEYLENGTH];
extern dboolean expectChallenge;

void Command_Ping_f(void);
extern tic_t connectiontimeout;
extern tic_t jointimeout;
extern tic_t freezetimeout[MAXNETNODES];
extern uint16_t pingmeasurecount;
extern uint32_t realpingtable[MAXPLAYERS];
extern uint32_t playerpingtable[MAXPLAYERS];
extern uint32_t playerpacketlosstable[MAXPLAYERS];
extern uint32_t playerdelaytable[MAXPLAYERS];
extern tic_t servermaxping;

extern int8_t server_lagless;
extern consvar_t cv_mindelay;

extern consvar_t cv_netticbuffer, cv_allownewplayer, cv_maxplayers, cv_joindelay;
extern consvar_t cv_resynchattempts, cv_blamecfail, cv_logconsistency;
extern consvar_t cv_maxsend, cv_noticedownload, cv_downloadspeed;

#ifdef VANILLAJOINNEXTROUND
extern consvar_t cv_joinnextround;
#endif

extern consvar_t cv_discordinvites;

#ifdef DEVELOP
	extern consvar_t cv_badjoin;
	extern consvar_t cv_badtraffic;
	extern consvar_t cv_badresponse;
	extern consvar_t cv_noresponse;
	extern consvar_t cv_nochallenge;
	extern consvar_t cv_badresults;
	extern consvar_t cv_noresults;
	extern consvar_t cv_badtime;
	extern consvar_t cv_badip;
#endif

// Used in d_net, the only dependence
tic_t ExpandTics(int32_t low, tic_t basetic);
void D_ClientServerInit(void);

void GenerateChallenge(uint8_t *buf);
shouldsign_t ShouldSignChallenge(uint8_t *message);

// Initialise the other field
void RegisterNetXCmd(netxcmd_t id, void (*cmd_f)(uint8_t **p, int32_t playernum));
void SendNetXCmdForPlayer(uint8_t playerid, netxcmd_t id, const void *param, size_t nparam);
#define SendNetXCmd(id, param, nparam) SendNetXCmdForPlayer(0, id, param, nparam) // Shortcut for P1
void SendKick(uint8_t playernum, uint8_t msg);

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
	CL_SENDKEY,
	CL_WAITCHALLENGE,
} cl_mode_t;

// Create any new ticcmds and broadcast to other players.
void NetKeepAlive(void);
void NetKeepAlive_NoTImeToWait(void);
void NetUpdate(void);

void SV_StartSinglePlayerServer(int32_t dogametype, dboolean donetgame);
dboolean SV_SpawnServer(void);
void SV_StopServer(void);
void SV_ResetServer(void);

/*--------------------------------------------------
	dboolean K_AddBotFromServer(uint16_t skin, uint8_t difficulty, botStyle_e style, uint8_t *newplayernum);

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

dboolean K_AddBotFromServer(uint16_t skin, uint8_t difficulty, botStyle_e style, uint8_t *p);

void CL_AddSplitscreenPlayer(void);
void CL_RemoveSplitscreenPlayer(uint8_t p);
void CL_Reset(void);
void CL_ClearPlayer(int32_t playernum);
void CL_RemovePlayer(int32_t playernum, kickreason_t reason);
void CL_QueryServerList(msg_server_t *list);
void CL_UpdateServerList(void);
void CL_TimeoutServerList(void);
// Is there a game running
dboolean Playing(void);
dboolean InADedicatedServer(void);

void CopyCaretColors (char *p, const char *s, int n);

// Broadcasts special packets to other players
//  to notify of game exit
void D_QuitNetGame(void);

//? How many ticks to run?
dboolean TryRunTics(tic_t realtic);

// extra data for lmps
// these functions scare me. they contain magic.
/*dboolean AddLmpExtradata(uint8_t **demo_p, int32_t playernum);
void ReadLmpExtraData(uint8_t **demo_pointer, int32_t playernum);*/

// translate a playername in a player number return -1 if not found and
// print a error message in the console
int8_t nametonum(const char *name);

extern char server_context[8];
extern uint8_t playernode[MAXPLAYERS];
/* consoleplayer of this player (splitscreen) */
extern uint8_t playerconsole[MAXPLAYERS];
extern plrinfo playerinfo[MAXPLAYERINFO];
extern int8_t joinnode;

#define SERVMUS_1 "SRVMS1"
#define SERVMUS_2 "SRVMS2"

void ChangeServMusic(const char* musname, dboolean fallback, dboolean keepPos);

int32_t GetClientMode(void);
void ChangeClientMode(int32_t mode);

int32_t D_NumPlayers(void);
dboolean D_IsPlayerHumanAndGaming(int32_t player_number);
void D_ResetTiccmds(void);
ticcmd_t *D_LocalTiccmd(uint8_t ss);

tic_t GetLag(int32_t node);
uint8_t GetFreeXCmdSize(uint8_t playerid);

#ifndef __cplusplus
void D_SHA256PasswordPass(const uint8_t *buffer, size_t len, const char *salt, uint8_t dest[static 32]);
#endif

extern uint8_t hu_redownloadinggamestate;

extern char *reqpass;
extern char **adminpass;
extern uint32_t adminpasscount;

extern dboolean hu_stopped;

//
// SRB2Kart
//

struct rewind_t {
	uint8_t savebuffer[NETSAVEGAMESIZE];
	tic_t leveltime;
	size_t demopos;

	ticcmd_t oldcmd[MAXPLAYERS];
	mobj_t oldghost[MAXPLAYERS];

	rewind_t *next;
};

void CL_ClearRewinds(void);
rewind_t *CL_SaveRewindPoint(size_t demopos);
rewind_t *CL_RewindToTime(tic_t time);

void HandleSigfail(const char *string);

void DoSayPacket(int8_t target, uint8_t flags, uint8_t source, char *message);
void DoSayPacketFromCommand(int8_t target, size_t usedargs, uint8_t flags);
void SendServerNotice(int8_t target, char *message);

// We give clients a chance to verify each other once per race.
// When is that challenge sent, and when should clients bail if they don't receive the responses?
#define CHALLENGEALL_START (TICRATE*10)
#define CHALLENGEALL_KICKUNRESPONSIVE (TICRATE*12)
#define CHALLENGEALL_SENDRESULTS (TICRATE*14)
#define CHALLENGEALL_CLIENTCUTOFF (TICRATE*16)

extern uint8_t awaitingChallenge[CHALLENGELENGTH]; // The message the server asked our client to sign when joining;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
