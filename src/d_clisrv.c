// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  d_clisrv.c
/// \brief SRB2 Network game communication and protocol, all OS independent parts.

#include "d_clisrv.h"
#include "doomdef.h"
#include "inttypes.h" // PRIX64
#include <time.h>
#ifdef __GNUC__
#include <unistd.h> //for unlink
#endif

#include "i_time.h"
#include "i_net.h"
#include "i_system.h"
#include "i_video.h"
#include "d_net.h"
#include "d_netfil.h" // fileneedednum
#include "d_main.h"
#include "g_game.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "keys.h"
#include "g_input.h" // JOY1
#include "m_menu.h"
#include "console.h"
#include "d_netfil.h"
#include "byteptr.h"
#include "p_saveg.h"
#include "z_zone.h"
#include "p_local.h"
#include "m_misc.h"
#include "am_map.h"
#include "m_random.h"
#include "mserv.h"
#include "y_inter.h"
#include "r_local.h"
#include "m_argv.h"
#include "p_setup.h"
#include "lua_script.h"
#include "lua_hook.h"
#include "lonesha256.h"
#include "m_perfstats.h"

#include "monocypher/monocypher.h"

// SRB2Kart
#include "k_kart.h"
#include "k_battle.h"
#include "k_pwrlv.h"
#include "k_bot.h"
#include "k_grandprix.h"
#include "k_boss.h"
#include "doomstat.h"
#include "s_sound.h" // sfx_syfail
#include "r_fps.h"
#include "m_cond.h" // netUnlocked
#include "g_party.h"
#include "k_vote.h"
#include "stun.h"
#include "m_menu.h"

// cl loading screen
#include "v_video.h"
#include "f_finale.h"

#ifdef HAVE_DISCORDRPC
#include "discord.h"
#endif

#ifdef USE_LZ4
#include "lz4.h"
#elif defined HAVE_ZLIB
#include <zlib.h>
#else
#include "lzf.h"
#endif

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

#define MAX_REASONLENGTH 30
#define FORCECLOSE 0x8000

I_StaticAssert(sizeof(doomdata_t) < MAXPACKETLENGTH); // can't send a packet larger than the max packet length!

dboolean server = true; // true or false but !server == client
#define client (!server)
dboolean nodownload = false;
dboolean serverrunning = false;
dboolean connectedtodedicated = false;
int32_t serverplayer = 0;
char server_context[8]; // Unique Context (even without Mumble support)

uint8_t playerconsole[MAXPLAYERS];
plrinfo playerinfo[MAXPLAYERINFO];

// Server specific vars
uint8_t playernode[MAXPLAYERS] = {UINT8_MAX};

// Minimum timeout for sending the savegame
// The actual timeout will be longer depending on the savegame length
tic_t jointimeout = (3*TICRATE);
static dboolean sendingsavegame[MAXNETNODES]; // Are we sending the savegame?
static dboolean resendingsavegame[MAXNETNODES]; // Are we resending the savegame?
static tic_t savegameresendcooldown[MAXNETNODES]; // How long before we can resend again?
tic_t freezetimeout[MAXNETNODES]; // Until when can this node freeze the server before getting a timeout?

// Incremented by cv_joindelay when a client joins, decremented each tic.
// If higher than cv_joindelay * 2 (3 joins in a short timespan), joins are temporarily disabled.
static tic_t joindelay = 0;

uint16_t pingmeasurecount = 1;
uint32_t realpingtable[MAXPLAYERS]; //the base table of ping where an average will be sent to everyone.
uint32_t playerpingtable[MAXPLAYERS]; //table of player latency values.
uint32_t playerpacketlosstable[MAXPLAYERS];
uint32_t playerdelaytable[MAXPLAYERS]; // mindelay values.

static tic_t lowest_lag;
int8_t server_lagless;

int8_t nodetoplayer[MAXNETNODES];
int8_t nodetoplayer2[MAXNETNODES]; // say the numplayer for this node if any (splitscreen)
int8_t nodetoplayer3[MAXNETNODES]; // say the numplayer for this node if any (splitscreen == 2)
int8_t nodetoplayer4[MAXNETNODES]; // say the numplayer for this node if any (splitscreen == 3)
uint8_t playerpernode[MAXNETNODES]; // used specialy for splitscreen
dboolean nodeingame[MAXNETNODES]; // set false as nodes leave game
dboolean nodeneedsauth[MAXNETNODES];

dboolean receivedplayerinfo = false;

tic_t servermaxping = 20; // server's max delay, in frames. Defaults to 20
static tic_t nettics[MAXNETNODES]; // what tic the client have received
static tic_t supposedtics[MAXNETNODES]; // nettics prevision for smaller packet
static uint8_t nodewaiting[MAXNETNODES];
static tic_t firstticstosend; // min of the nettics
static tic_t tictoclear = 0; // optimize d_clearticcmd
static tic_t maketic;

static consistancy_t consistancy[BACKUPTICS];
static uint32_t serverseedarchive[BACKUPTICS][PRNUMCLASS] = {0};

static int32_t numdesynchingseeds = 0;

#define CONSISTANCYDBGSIZE 4096

static uint8_t player_joining = false;
uint8_t hu_redownloadinggamestate = 0;

// kart, true when a player is connecting or disconnecting so that the gameplay has stopped in its tracks
dboolean hu_stopped = false;

char *reqpass;
char **adminpass;
uint32_t adminpasscount = 0;
static char adminsalt[MAXNETNODES][9];

// Client specific
static ticcmd_t localcmds[MAXSPLITSCREENPLAYERS][MAXGENTLEMENDELAY];
static dboolean cl_packetmissed;
// here it is for the secondary local player (splitscreen)
static uint8_t mynode; // my address pointofview server
static dboolean cl_redownloadinggamestate = false;

static uint8_t localtextcmd[MAXSPLITSCREENPLAYERS][MAXTEXTCMD];
static tic_t neededtic;
int8_t servernode = 0; // the number of the server node
int8_t joinnode = 0; // used for CL_VIEWSERVER

char connectedservername[MAXSERVERNAME];
char connectedservercontact[MAXSERVERCONTACT];
char connectedserverdescription[MAXSERVERDESCRIPTION];
char connectedservermotd[MAXSERVERMOTD];

/// \brief do we accept new players?
/// \todo WORK!
dboolean acceptnewnode = true;

uint32_t ourIP; // Used when populating PT_SERVERCHALLENGE (guards against signature reuse)
uint8_t lastReceivedKey[MAXNETNODES][MAXSPLITSCREENPLAYERS][PUBKEYLENGTH]; // Player's public key (join process only! active players have it on player_t)
uint8_t lastSentChallenge[MAXNETNODES][CHALLENGELENGTH]; // The random message we asked them to sign in PT_SERVERCHALLENGE, check it in PT_CLIENTJOIN
uint8_t awaitingChallenge[CHALLENGELENGTH]; // The message the server asked our client to sign when joining
uint8_t lastChallengeAll[CHALLENGELENGTH]; // The message we asked EVERYONE to sign for client-to-client identity proofs
uint8_t lastReceivedSignature[MAXPLAYERS][SIGNATURELENGTH]; // Everyone's response to lastChallengeAll
uint8_t knownWhenChallenged[MAXPLAYERS][PUBKEYLENGTH]; // Everyone a client saw at the moment a challenge should be initiated
dboolean expectChallenge = false; // Were we in-game before a client-to-client challenge should have been sent?

uint8_t priorKeys[MAXPLAYERS][PUBKEYLENGTH]; // Make a note of keys before consuming a new gamestate, and if the server tries to send us a gamestate where keys differ, assume shenanigans

dboolean serverisfull = false; //lets us be aware if the server was full after we check files, but before downloading, so we can ask if the user still wants to download or not
tic_t firstconnectattempttime = 0;

#ifdef DEVELOP
	consvar_t cv_badjoin = CVAR_INIT ("badjoin", "0", 0, CV_Unsigned, NULL);
	consvar_t cv_badtraffic = CVAR_INIT ("badtraffic", "0", 0, CV_Unsigned, NULL);
	consvar_t cv_badresponse = CVAR_INIT ("badresponse", "0", 0, CV_Unsigned, NULL);
	consvar_t cv_noresponse = CVAR_INIT ("noresponse", "0", 0, CV_Unsigned, NULL);
	consvar_t cv_nochallenge = CVAR_INIT ("nochallenge", "0", 0, CV_Unsigned, NULL);
	consvar_t cv_badresults = CVAR_INIT ("badresults", "0", 0, CV_Unsigned, NULL);
	consvar_t cv_noresults = CVAR_INIT ("noresults", "0", 0, CV_Unsigned, NULL);
	consvar_t cv_badtime = CVAR_INIT ("badtime", "0", 0, CV_Unsigned, NULL);
	consvar_t cv_badip = CVAR_INIT ("badip", "0", 0, CV_Unsigned, NULL);
#endif


// engine

// Must be a power of two
#define TEXTCMD_HASH_SIZE 4

typedef struct textcmdtic_s
{
	tic_t tic;
	uint8_t *playercmds[MAXPLAYERS];
	struct textcmdtic_s *next;
} textcmdtic_t;

typedef struct textcmdbuf_s textcmdbuf_t;

struct textcmdbuf_s
{
	textcmdbuf_t *next;
	uint8_t cmd[MAXTEXTCMD];
};

static textcmdbuf_t *textcmdbuf[MAXSPLITSCREENPLAYERS] = {NULL};

static ticcmd_t playercmds[MAXPLAYERS];
ticcmd_t netcmds[BACKUPTICS][MAXPLAYERS];
static textcmdtic_t *textcmds[TEXTCMD_HASH_SIZE] = {NULL};


consvar_t cv_showjoinaddress = CVAR_INIT ("showjoinaddress", "Off", CV_SAVE, CV_OnOff, NULL);

consvar_t cv_shownodeip = CVAR_INIT ("showipinnodelist", "Off", CV_SAVE, CV_OnOff, NULL);

static CV_PossibleValue_t playbackspeed_cons_t[] = {{1, "MIN"}, {10, "MAX"}, {0, NULL}};
consvar_t cv_playbackspeed = CVAR_INIT ("playbackspeed", "1", 0, playbackspeed_cons_t, NULL);

consvar_t cv_httpsource = CVAR_INIT ("http_source", "", CV_SAVE, NULL, NULL);

consvar_t cv_kicktime = CVAR_INIT ("kicktime", "10", CV_SAVE, CV_Unsigned, NULL);

static tic_t stop_spamming[MAXPLAYERS];

// Generate a message for an authenticating client to sign, with some guarantees about who we are.
void GenerateChallenge(uint8_t *buf)
{
	uint64_t now = LSBF_LONGLONG(time(NULL));
#if 0 // im sorry I have to do this but....
	uint32_t our_ip_le = LSBF_LONG(ourIP);
#endif
	csprng(buf, CHALLENGELENGTH); // Random noise as a baseline, but...
	memcpy(buf, &now, sizeof(now)); // Timestamp limits the reuse window.
#if 0 // im sorry I have to do this but....
	memcpy(buf + sizeof(now), &our_ip_le, sizeof(our_ip_le)); // IP prevents captured signatures from being used elsewhere.
#else
	uint32_t dummy = UINT32_MAX;
	memcpy(buf + sizeof(now), &dummy, sizeof(dummy)); // Dummy since ip breaks VPN and other check is not good enough.
#endif

	#ifdef DEVELOP
	if (cv_badtime.value)
	{
		CV_AddValue(&cv_badtime, -1);
		CONS_Alert(CONS_WARNING, "cv_badtime enabled, trashing time in auth message\n");
		memset(buf, 0, sizeof(now));
	}

	if (cv_badip.value)
	{
		CV_AddValue(&cv_badip, -1);
		CONS_Alert(CONS_WARNING, "cv_badip enabled, trashing IP in auth message\n");
		memset(buf + sizeof(now), 0, sizeof(ourIP));
	}
	#endif
}

// Modified servers can throw softballs or reuse challenges.
// Don't sign anything that wasn't generated just for us!
shouldsign_t ShouldSignChallenge(uint8_t *message)
{
#if 1
	(void)message;
#endif

#if 0 // Sorry.....
	uint64_t then, now;
	uint32_t claimedIP, realIP;

	now = time(NULL);
	memcpy(&then, message, sizeof(then));
	then = LSBF_LONGLONG(then); // LE 64
	memcpy(&claimedIP, message + sizeof(then), sizeof(claimedIP));
	claimedIP = LSBF_LONG(claimedIP); // LE 32 - TODO needs to be BE 128 bit for ipv6
	realIP = I_GetNodeAddressInt(servernode);

	if ((max(now, then) - min(now, then)) > 60*15)
		return SIGN_BADTIME;


	if (realIP != claimedIP && I_IsExternalAddress(&realIP))
		return SIGN_BADIP;
#endif

	return SIGN_OK;
}

static inline void *G_DcpyTiccmd(void* dest, const ticcmd_t* src, const size_t n)
{
	const size_t d = n / sizeof(ticcmd_t);
	const size_t r = n % sizeof(ticcmd_t);
	uint8_t *ret = dest;

	if (r)
		memcpy(dest, src, n);
	else if (d)
		G_MoveTiccmd(dest, src, d);
	return ret+n;
}

static inline void *G_ScpyTiccmd(ticcmd_t* dest, void* src, const size_t n)
{
	const size_t d = n / sizeof(ticcmd_t);
	const size_t r = n % sizeof(ticcmd_t);
	uint8_t *ret = src;

	if (r)
		memcpy(dest, src, n);
	else if (d)
		G_MoveTiccmd(dest, src, d);
	return ret+n;
}



// Some software don't support largest packet
// (original sersetup, not exactely, but the probability of sending a packet
// of 512 bytes is like 0.1)
uint16_t software_MAXPACKETLENGTH;

/** Guesses the full value of a tic from its lowest byte, for a specific node
  *
  * \param low The lowest byte of the tic value
  * \param basetic The last full tic value to compare against
  * \return The full tic value
  *
  */
tic_t ExpandTics(int32_t low, tic_t basetic)
{
	int32_t delta;

	delta = low - (basetic & UINT8_MAX);

	if (delta >= -64 && delta <= 64)
		return (basetic & ~UINT8_MAX) + low;
	else if (delta > 64)
		return (basetic & ~UINT8_MAX) - 256 + low;
	else //if (delta < -64)
		return (basetic & ~UINT8_MAX) + 256 + low;
}

// -----------------------------------------------------------------
// Some extra data function for handle textcmd buffer
// -----------------------------------------------------------------

static void (*listnetxcmd[MAXNETXCMD])(uint8_t **p, int32_t playernum);

void RegisterNetXCmd(netxcmd_t id, void (*cmd_f)(uint8_t **p, int32_t playernum))
{
#ifdef PARANOIA
	if (id >= MAXNETXCMD)
		I_Error("Command id %d too big", id);
	if (listnetxcmd[id] != 0)
		I_Error("Command id %d already used", id);
#endif
	listnetxcmd[id] = cmd_f;
}

static void WriteNetXCmd(uint8_t *cmd, netxcmd_t id, const void *param, size_t nparam)
{
	cmd[0]++;
	cmd[cmd[0]] = (uint8_t)id;
	if (param && nparam)
	{
		memcpy(&cmd[cmd[0]+1], param, nparam);
		cmd[0] = (uint8_t)(cmd[0] + (uint8_t)nparam);
	}
}

void SendNetXCmdForPlayer(uint8_t playerid, netxcmd_t id, const void *param, size_t nparam)
{
	if (localtextcmd[playerid][0]+2+nparam > MAXTEXTCMD)
	{
		textcmdbuf_t *buf = textcmdbuf[playerid];

		if (2+nparam > MAXTEXTCMD)
		{
			CONS_Alert(CONS_ERROR, M_GetText("packet too large to fit NetXCmd, cannot add netcmd %s with id %d! (size: %s, max: %d)\n"), netxcmdnames[id-1], id, sizeu1(2+nparam), MAXTEXTCMD);
			return;
		}

		// for future reference: if (cv_debug) != debug disabled.
		CONS_Alert(CONS_NOTICE, M_GetText("NetXCmd buffer full, delaying netcmd %s with id %d... (size: %d, needed: %s)\n"), netxcmdnames[id-1], id, localtextcmd[playerid][0], sizeu1(nparam));

		if (buf == NULL)
		{
			textcmdbuf[playerid] = Z_Malloc(sizeof(textcmdbuf_t), PU_STATIC, NULL);
			textcmdbuf[playerid]->cmd[0] = 0;
			textcmdbuf[playerid]->next = NULL;
			WriteNetXCmd(textcmdbuf[playerid]->cmd, id, param, nparam);
			return;
		}

		while (buf->next != NULL)
			buf = buf->next;

		if (buf->cmd[0]+2+nparam > MAXTEXTCMD)
		{
			buf->next = Z_Malloc(sizeof(textcmdbuf_t), PU_STATIC, NULL);
			buf->next->cmd[0] = 0;
			buf->next->next = NULL;
			WriteNetXCmd(buf->next->cmd, id, param, nparam);
		}
		else
		{
			WriteNetXCmd(buf->cmd, id, param, nparam);
		}
		return;
	}
	WriteNetXCmd(localtextcmd[playerid], id, param, nparam);
}

uint8_t GetFreeXCmdSize(uint8_t playerid)
{
	// -1 for the size and another -1 for the ID.
	return (uint8_t)(localtextcmd[playerid][0] - 2);
}

// Frees all textcmd memory for the specified tic
static void D_FreeTextcmd(tic_t tic)
{
	textcmdtic_t **tctprev = &textcmds[tic & (TEXTCMD_HASH_SIZE - 1)];
	textcmdtic_t *textcmdtic = *tctprev;

	while (textcmdtic && textcmdtic->tic != tic)
	{
		tctprev = &textcmdtic->next;
		textcmdtic = textcmdtic->next;
	}

	if (textcmdtic)
	{
		// Remove this tic from the list.
		*tctprev = textcmdtic->next;

		// Free all players.
		for (int32_t i = 0; i < MAXPLAYERS; i++)
		{
			Z_Free(textcmdtic->playercmds[i]);
		}

		// Free this tic's own memory.
		Z_Free(textcmdtic);
	}
}

// Gets the buffer for the specified ticcmd, or NULL if there isn't one
static uint8_t* D_GetExistingTextcmd(tic_t tic, int32_t playernum)
{
	textcmdtic_t *textcmdtic = textcmds[tic & (TEXTCMD_HASH_SIZE - 1)];
	while (textcmdtic && textcmdtic->tic != tic) textcmdtic = textcmdtic->next;

	// Do we have an entry for the tic? If so, look for player.
	if (textcmdtic)
	{
		uint8_t *cmd = textcmdtic->playercmds[playernum];
		if (cmd) return cmd;
	}

	return NULL;
}

// Gets the buffer for the specified ticcmd, creating one if necessary
static uint8_t* D_GetTextcmd(tic_t tic, int32_t playernum)
{
	textcmdtic_t *textcmdtic = textcmds[tic & (TEXTCMD_HASH_SIZE - 1)];
	textcmdtic_t **tctprev = &textcmds[tic & (TEXTCMD_HASH_SIZE - 1)];

	// Look for the tic.
	while (textcmdtic && textcmdtic->tic != tic)
	{
		tctprev = &textcmdtic->next;
		textcmdtic = textcmdtic->next;
	}

	// If we don't have an entry for the tic, make it.
	if (!textcmdtic)
	{
		textcmdtic = *tctprev = Z_Calloc(sizeof (textcmdtic_t), PU_STATIC, NULL);
		textcmdtic->tic = tic;
	}

	// If we don't have an entry for the player, make it.
	if (!textcmdtic->playercmds[playernum])
		textcmdtic->playercmds[playernum] = Z_Calloc(MAXTEXTCMD, PU_STATIC, NULL);

	return textcmdtic->playercmds[playernum];
}

static dboolean ExtraDataTicker(void)
{
	dboolean anyNetCmd = false;
	int32_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] || i == 0)
		{
			uint8_t *bufferstart = D_GetExistingTextcmd(gametic, i);

			if (bufferstart)
			{
				uint8_t *curpos = bufferstart;
				uint8_t *bufferend = &curpos[curpos[0]+1];

				curpos++;
				while (curpos < bufferend)
				{
					if (*curpos < MAXNETXCMD && listnetxcmd[*curpos])
					{
						const uint8_t id = *curpos;
						curpos++;
						DEBFILE(va("executing x_cmd %s ply %u ", netxcmdnames[id - 1], i));
						(listnetxcmd[id])(&curpos, i);
						DEBFILE("done\n");
						anyNetCmd = true;
					}
					else
					{
						if (server)
						{
							SendKick(i, KICK_MSG_CON_FAIL);
							DEBFILE(va("player %d kicked [gametic=%u] reason as follows:\n", i, gametic));
						}
						CONS_Alert(CONS_WARNING, M_GetText("Got unknown net command [%s]=%d (max %d)\n"), sizeu1(curpos - bufferstart), *curpos, bufferstart[0]);
						break;
					}
				}
			}
		}
	}

	// If you are a client, you can safely forget the net commands for this tic
	// If you are the server, you need to remember them until every client has been acknowledged,
	// because if you need to resend a PT_SERVERTICS packet, you will need to put the commands in it
	if (client)
	{
		D_FreeTextcmd(gametic);
	}
	return anyNetCmd;
}

static void D_Clearticcmd(tic_t tic)
{
	int32_t i;

	D_FreeTextcmd(tic);

	for (i = 0; i < MAXPLAYERS; i++)
		netcmds[tic%BACKUPTICS][i].flags = 0;

	DEBFILE(va("clear tic %5u (%2u)\n", tic, tic%BACKUPTICS));
}

void D_ResetTiccmds(void)
{
	int32_t i, j;

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		for (j = 0; j < MAXGENTLEMENDELAY; j++)
		{
			memset(&localcmds[i][j], 0, sizeof(ticcmd_t));
		}
	}

	// Reset the net command list
	for (i = 0; i < TEXTCMD_HASH_SIZE; i++)
		while (textcmds[i])
			D_Clearticcmd(textcmds[i]->tic);
}

ticcmd_t *D_LocalTiccmd(uint8_t ss)
{
	return &localcmds[ss][0];
}

void SendKick(uint8_t playernum, uint8_t msg)
{
	uint8_t buf[2];

	buf[0] = playernum;
	buf[1] = msg;
	SendNetXCmd(XD_KICK, &buf, 2);
}

// -----------------------------------------------------------------
// end of extra data function
// -----------------------------------------------------------------

// -----------------------------------------------------------------
// extra data function for lmps
// -----------------------------------------------------------------

// if extradatabit is set, after the ziped tic you find this:
//
//   type   |  description
// ---------+--------------
//   byte   | size of the extradata
//   byte   | the extradata (xd) bits: see XD_...
//            with this byte you know what parameter folow
// if (xd & XDNAMEANDCOLOR)
//   byte   | color
//   char[MAXPLAYERNAME] | name of the player
// endif
// if (xd & XD_WEAPON_PREF)
//   byte   | original weapon switch: dboolean, true if use the old
//          | weapon switch methode
//   char[NUMWEAPONS] | the weapon switch priority
//   byte   | autoaim: true if use the old autoaim system
// endif
/*dboolean AddLmpExtradata(uint8_t **demo_point, int32_t playernum)
{
	uint8_t *textcmd = D_GetExistingTextcmd(gametic, playernum);

	if (!textcmd)
		return false;

	memcpy(*demo_point, textcmd, textcmd[0]+1);
	*demo_point += textcmd[0]+1;
	return true;
}

void ReadLmpExtraData(uint8_t **demo_pointer, int32_t playernum)
{
	uint8_t nextra;
	uint8_t *textcmd;

	if (!demo_pointer)
		return;

	textcmd = D_GetTextcmd(gametic, playernum);
	nextra = **demo_pointer;
	memcpy(textcmd, *demo_pointer, nextra + 1);
	// increment demo pointer
	*demo_pointer += nextra + 1;
}*/

// -----------------------------------------------------------------
// end extra data function for lmps
// -----------------------------------------------------------------

static int16_t Consistancy(consistancy_t *c, uint32_t idx);

static void GetPackets(void);

static cl_mode_t cl_mode = CL_SEARCHING;

#ifdef HAVE_CURL
char http_source[MAX_MIRROR_LENGTH];
#endif

static uint16_t cl_lastcheckedfilecount = 0;	// used for full file list

void ChangeServMusic(const char* musname, dboolean fallback, dboolean keepPos)
{
	dboolean viewserver = (cl_mode == CL_VIEWSERVER);
	if (S_MusicExists(musname))
		if (keepPos)
			S_ChangeMusicEx(musname,0,true, S_GetMusicPosition(), 0,0);
		else
			S_ChangeMusicInternal(musname,true);
	else if (fallback)
		S_ChangeMusicInternal("BLANCD",true);
	S_ShowMusicCredit(viewserver ? -28*FRACUNIT : 0, 5*TICRATE, 0);
}

// Let external code read and modify this stuff.
int32_t GetClientMode(void)
{
	return cl_mode;
}

void ChangeClientMode(int32_t mode)
{
	cl_mode = mode;
}

//
// CL_DrawConnectionStatus
//
// Keep the local client informed of our status.
//
static inline void CL_DrawConnectionStatus(void)
{
	int32_t ccstime = I_GetTime();

	// Draw background fade
	if (!menustack[0]) // menu already draws its own fade
		V_DrawFadeScreen(0xFF00, 16); // force default

	if (cl_mode != CL_DOWNLOADFILES && cl_mode != CL_LOADFILES && cl_mode != CL_CHECKFILES
#ifdef HAVE_CURL
	&& cl_mode != CL_DOWNLOADHTTPFILES
#endif
	&& cl_mode != CL_VIEWSERVER
	)
	{
		int32_t i, animtime = ((ccstime / 4) & 15) + 16;
		uint8_t palstart = (cl_mode == CL_SEARCHING) ? 32 : 96;
		// 15 pal entries total.
		const char *cltext;

		// Draw bottom box
		M_DrawTextBox(BASEVIDWIDTH/2-128-8, BASEVIDHEIGHT-24-8, 32, 1, 0);
		V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24-24, MENUCAPS|V_YELLOWMAP, "Press ESC to abort");

		for (i = 0; i < 16; ++i)
			V_DrawFill((BASEVIDWIDTH/2-128) + (i * 16), BASEVIDHEIGHT-24, 16, 8, palstart + ((animtime - i) & 15));

		switch (cl_mode)
		{
			case CL_DOWNLOADSAVEGAME:
				if (lastfilenum != -1)
				{
					uint32_t currentsize = fileneeded[lastfilenum].currentsize;
					uint32_t totalsize = fileneeded[lastfilenum].totalsize;
					int32_t dldlength = 0;

					cltext = M_GetText("Downloading game state...");
					Net_GetNetStat();

					if (totalsize != 0)
						dldlength = (int32_t)((currentsize/(double)totalsize) * 256);
					if (dldlength > 256)
						dldlength = 256;
					V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-16, 256, 8, 111);
					V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-16, dldlength, 8, 96);

					V_DrawString(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-16, V_20TRANS|V_MONOSPACE,
						va(" %4uK/%4uK",currentsize>>10,totalsize>>10));

					V_DrawRightAlignedString(BASEVIDWIDTH/2+128, BASEVIDHEIGHT-16, V_20TRANS|V_MONOSPACE,
						va("%3.1fK/s ", ((double)getbps)/1024));
				}
				else
					cltext = M_GetText("Waiting to download game state...");
				break;
			case CL_ASKFULLFILELIST:
			case CL_CONFIRMCONNECT:
			case CL_DOWNLOADFAILED:
				cltext = "";
				break;
			case CL_SETUPFILES:
				cltext = M_GetText("Configuring addons...");
				break;
			case CL_ASKJOIN:
			case CL_WAITJOINRESPONSE:
				if (serverisfull)
					cltext = M_GetText("Server full, waiting for a slot...");
				else
					cltext = M_GetText("Requesting to join...");

				break;
#ifdef HAVE_CURL
			case CL_PREPAREHTTPFILES:
				cltext = M_GetText("Waiting to download files...");
				break;
#endif
			default:
				cltext = M_GetText("Connecting to server...");
				break;
		}
		V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-16-24, MENUCAPS|V_YELLOWMAP, cltext);
	}
	else
	{
		if (cl_mode == CL_CHECKFILES)
		{
			int32_t totalfileslength = 0;
			int32_t checkednum = 0;
			int32_t i;

			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24-24, MENUCAPS|V_YELLOWMAP, "Press ESC to abort");

			// ima just count files here
			for (i = 0; i < fileneedednum; i++)
				if (fileneeded[i].status != FS_NOTCHECKED)
					checkednum++;

			// Loading progress
			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24-32, MENUCAPS|V_YELLOWMAP, "Checking server addons...");
			if (fileneedednum != 0)
				totalfileslength = (int32_t)((checkednum/(double)fileneedednum) * 256);
			M_DrawTextBox(BASEVIDWIDTH/2-128-8, BASEVIDHEIGHT-24-8, 32, 1, 0);
			V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-24, 256, 8, 111);
			V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-24, totalfileslength, 8, 96);
			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24, V_20TRANS|V_MONOSPACE,
				va(" %2u/%2u Files", checkednum, fileneedednum));
		}
		else if (cl_mode == CL_LOADFILES)
		{
			int32_t totalfileslength = 0;
			int32_t loadcompletednum = 0;
			int32_t i;

			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24-24,  MENUCAPS|V_YELLOWMAP, "Press ESC to abort");

			// ima just count files here
			for (i = 0; i < fileneedednum; i++)
				if (fileneeded[i].status == FS_OPEN)
					loadcompletednum++;

			// Loading progress
			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24-32, MENUCAPS|V_YELLOWMAP, "Loading server addons...");
			if (fileneedednum != 0)
				totalfileslength = (int32_t)((loadcompletednum/(double)(fileneedednum)) * 256);
			M_DrawTextBox(BASEVIDWIDTH/2-128-8, BASEVIDHEIGHT-24-8, 32, 1, 0);
			V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-24, 256, 8, 111);
			V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-24, totalfileslength, 8, 96);
			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24, MENUCAPS|V_20TRANS|V_MONOSPACE,
				va(" %2u/%2u Files", loadcompletednum,fileneedednum));
		}
		else if (cl_mode == CL_VIEWSERVER)
		{
			if (receivedplayerinfo && !menustack[0])
			{
				M_StartControlPanel();
				M_EnterMenu(MN_VIEWSERVER, true, 0);
			}
		}
		else if (lastfilenum != -1)
		{
			int32_t dldlength = 0;
			int32_t totalfileslength = 0;
			uint32_t totaldldsize = 0;
			static char tempname[28];
			fileneeded_t *file = &fileneeded[lastfilenum];
			char *filename = file->filename;
			uint32_t totalsize   = file->totalsize;
			uint32_t currentsize = min(file->currentsize, file->totalsize);

			// Draw the bottom box.
			M_DrawTextBox(BASEVIDWIDTH/2-128-8, BASEVIDHEIGHT-58-8, 32, 1, 0);
			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-58-14,  MENUCAPS|V_YELLOWMAP, "Press ESC to abort");

			Net_GetNetStat();
			if (totalsize != 0)
				dldlength = (int32_t)((file->currentsize/(double)file->totalsize) * 256);
			if (dldlength > 256)
				dldlength = 256;

			V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-58, 256, 8, 111);
			V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-58, dldlength, 8, 96);

			memset(tempname, 0, sizeof(tempname));
			// offset filename to just the name only part
			filename += strlen(filename) - nameonlylength(filename);

			if (strlen(filename) > sizeof(tempname)-1) // too long to display fully
			{
				size_t endhalfpos = strlen(filename)-10;
				// display as first 14 chars + ... + last 10 chars
				// which should add up to 27 if our math(s) is correct
				snprintf(tempname, sizeof(tempname), "%.14s...%.10s", filename, filename+endhalfpos);
			}
			else // we can copy the whole thing in safely
			{
				strncpy(tempname, filename, sizeof(tempname)-1);
			}

			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-58-30, 0,
#ifdef HAVE_CURL
				va(M_GetText("%s downloading"), ((cl_mode == CL_DOWNLOADHTTPFILES) ? "\x82""HTTP" : "\x85""Direct")));
#else
				M_GetText("Direct downloading"));
#endif
			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-58-22,  MENUCAPS|V_YELLOWMAP,
				va(M_GetText("\"%s\""), tempname));
			V_DrawString(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-58, V_20TRANS|V_MONOSPACE,
				va(" %4uK/%4uK",currentsize>>10,totalsize>>10));
			V_DrawRightAlignedString(BASEVIDWIDTH/2+128, BASEVIDHEIGHT-58, V_20TRANS|V_MONOSPACE,
				va("%3.1fK/s ", ((double)getbps)/1024.0));

			// Download progress

			if (currentsize != totalsize)
				totaldldsize = downloadcompletedsize+fileneeded[lastfilenum].currentsize; // Add in single file progress download if applicable
			else
				totaldldsize = downloadcompletedsize;

			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24-14,  MENUCAPS|V_YELLOWMAP, "Overall Download Progress");
			if (totaldldsize != 0)
				totalfileslength = (int32_t)((totaldldsize/(double)totalfilesrequestedsize) * 256);
			M_DrawTextBox(BASEVIDWIDTH/2-128-8, BASEVIDHEIGHT-24-8, 32, 1, 0);
			V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-24, 256, 8, 111);
			V_DrawFill(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-24, totalfileslength, 8, 96);

			if (totalfilesrequestedsize>>20 >= 10) // display in MB if over 10MB
				V_DrawString(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-24,  MENUCAPS|V_20TRANS|V_MONOSPACE,
					va(" %4uM/%4uM",totaldldsize>>20,totalfilesrequestedsize>>20));
			else
				V_DrawString(BASEVIDWIDTH/2-128, BASEVIDHEIGHT-24,  MENUCAPS|V_20TRANS|V_MONOSPACE,
					va(" %4uK/%4uK",totaldldsize>>10,totalfilesrequestedsize>>10));

			V_DrawRightAlignedString(BASEVIDWIDTH/2+128, BASEVIDHEIGHT-24, MENUCAPS|V_20TRANS|V_MONOSPACE,
					va("%2u/%2u Files ",downloadcompletednum,totalfilesrequestednum));
		}
		else
		{
			int32_t i, animtime = ((ccstime / 4) & 15) + 16;
			uint8_t palstart = (cl_mode == CL_SEARCHING) ? 128 : 160;
			// 15 pal entries total.

			//Draw bottom box
			M_DrawTextBox(BASEVIDWIDTH/2-128-8, BASEVIDHEIGHT-24-8, 32, 1, 0);
			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24-24,  MENUCAPS|V_YELLOWMAP, "Press ESC to abort");

			for (i = 0; i < 16; ++i)
				V_DrawFill((BASEVIDWIDTH/2-128) + (i * 16), BASEVIDHEIGHT-24, 16, 8, palstart + ((animtime - i) & 15));

			V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24-32,  MENUCAPS|V_YELLOWMAP,
				M_GetText("Waiting to download files..."));
		}
	}
}

static dboolean CL_AskFileList(int32_t firstfile)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	netbuffer->packettype = PT_TELLFILESNEEDED;
	netbuffer->u.filesneedednum = firstfile;

	return HSendPacket(servernode, false, 0, sizeof (int32_t));
}

/** Sends a special packet to declare how many players in local
  * Used only in arbitratrenetstart()
  * Sends a PT_CLIENTJOIN packet to the server
  *
  * \return True if the packet was successfully sent
  * \todo Improve the description...
  *       Because to be honest, I have no idea what arbitratrenetstart is...
  *       Is it even used...?
  *
  */
static dboolean CL_SendJoin(void)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	uint8_t localplayers = 1;
	uint8_t i;

	if (netgame)
		CONS_Printf(M_GetText("Sending join request...\n"));
	netbuffer->packettype = PT_CLIENTJOIN;

	if (splitscreen)
		localplayers += splitscreen;

	netbuffer->u.clientcfg.localplayers = localplayers;
	netbuffer->u.clientcfg._255 = 255;
	netbuffer->u.clientcfg.packetversion = PACKETVERSION;
	netbuffer->u.clientcfg.version = VERSION;
	netbuffer->u.clientcfg.subversion = SUBVERSION;
	strncpy(netbuffer->u.clientcfg.application, SRB2APPLICATION,
			sizeof netbuffer->u.clientcfg.application);

	for (i = 0; i <= splitscreen; i++)
	{
		// the MAXPLAYERS addition is necessary to communicate that g_localplayers is not yet safe to reference
		CleanupPlayerName(MAXPLAYERS+i, cv_playername[i].zstring);
		strncpy(netbuffer->u.clientcfg.names[i], cv_playername[i].zstring, MAXPLAYERNAME);
	}
	// privacy shield for the local players not joining this session
	for (; i < MAXSPLITSCREENPLAYERS; i++)
		strncpy(netbuffer->u.clientcfg.names[i], va("Player %c", 'A' + i), MAXPLAYERNAME);

	memcpy(&netbuffer->u.clientcfg.availabilities, R_GetSkinAvailabilities(), MAXAVAILABILITY*sizeof(uint8_t));

	// Don't leak old signatures from prior sessions.
	memset(&netbuffer->u.clientcfg.challengeResponse, 0, sizeof(((clientconfig_pak *)0)->challengeResponse));

#if 0
	if (client && netgame)
	{
		shouldsign_t safe = ShouldSignChallenge(awaitingChallenge);

		if (safe != SIGN_OK)
		{
			if (safe == SIGN_BADIP)
			{
				I_Error("External server IP didn't match the message it sent.");
			}
			else if (safe == SIGN_BADTIME)
			{
				I_Error("External server sent a message with an unusual timestamp.\nMake sure your system time is set correctly.");
			}
			else
			{
				I_Error("External server asked for a signature on something strange.\nPlease notify a developer if you've seen this more than once.");
			}
			return false;
		}
	}
#endif

	for (i = 0; i <= splitscreen; i++)
	{
		uint8_t signature[SIGNATURELENGTH];

		// If our keys are garbage (corrupted profile?), fail here instead of when the server boots us, so the player knows what's going on.
		crypto_eddsa_sign(signature, g_secret_key[i], awaitingChallenge, sizeof(awaitingChallenge));
		if (crypto_eddsa_check(signature, g_public_key[i], awaitingChallenge, sizeof(awaitingChallenge)) != 0)
			I_Error("Couldn't self-verify key associated with player %d.\nKey may be corrupted.\n", i); // I guess this is the most reasonable way to catch a malformed key.

#ifdef DEVELOP
			if (cv_badjoin.value)
			{
				CV_AddValue(&cv_badjoin, -1);
				CONS_Alert(CONS_WARNING, "cv_badjoin enabled, scrubbing signature from CL_SendJoin\n");
				memset(signature, 0, sizeof(signature));
			}
#endif

		// Testing
		// memset(signature, 0, sizeof(signature));

		memcpy(&netbuffer->u.clientcfg.challengeResponse[i], signature, sizeof(signature));
	}

	return HSendPacket(servernode, false, 0, sizeof (clientconfig_pak));
}

static dboolean CL_SendKey(void)
{
	int i;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	netbuffer->packettype = PT_CLIENTKEY;

	memset(netbuffer->u.clientkey.key, 0, sizeof(((clientkey_pak *)0)->key));
	for (i = 0; i <= splitscreen; i++)
	{
		memcpy(netbuffer->u.clientkey.key[i], g_public_key[i], 32);
	}
	return HSendPacket(servernode, false, 0, sizeof (clientkey_pak) );
}

void
CopyCaretColors (char *p, const char *s, int n)
{
	char *t;
	int   m;
	int   c;
	if (!n)
		return;
	while (( t = strchr(s, '^') ))
	{
		m = ( t - s );

		if (m >= n)
		{
			memcpy(p, s, n);
			return;
		}
		else
			memcpy(p, s, m);

		p += m;
		n -= m;
		s += m;

		if (!n)
			return;

		if (s[1])
		{
			c = toupper(s[1]);
			if (isdigit(c))
				c = 0x80 + ( c - '0' );
			else if (c >= 'A' && c <= 'F')
				c = 0x80 + ( c - 'A' );
			else
				c = 0;

			if (c)
			{
				*p++ = c;
				n--;

				if (!n)
					return;
			}
			else
			{
				if (n < 2)
					break;

				memcpy(p, s, 2);

				p += 2;
				n -= 2;
			}

			s += 2;
		}
		else
			break;
	}
	strlcpy(p, s, n);
}

static void SV_SendServerInfo(int32_t node, tic_t servertime)
{
	uint8_t *p;
	size_t mirror_length;
	const char *httpurl = cv_httpsource.string;
	uint8_t prefgametype = (cv_kartgametypepreference.value == -1)
		? gametype
		: cv_kartgametypepreference.value;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	netbuffer->packettype = PT_SERVERINFO;
	netbuffer->u.serverinfo._255 = 255;
	netbuffer->u.serverinfo.packetversion = PACKETVERSION;
	netbuffer->u.serverinfo.version = VERSION;
	netbuffer->u.serverinfo.subversion = SUBVERSION;
	memcpy(netbuffer->u.serverinfo.commit,
			comprevision_abbrev_bin, GIT_SHA_ABBREV);
	strncpy(netbuffer->u.serverinfo.application, SRB2APPLICATION,
			sizeof netbuffer->u.serverinfo.application);
	// return back the time value so client can compute their ping
	netbuffer->u.serverinfo.time = (tic_t)LSBF_LONG(servertime);
	netbuffer->u.serverinfo.leveltime = (tic_t)LSBF_LONG(leveltime);

	netbuffer->u.serverinfo.numberofplayer = (uint8_t)D_NumPlayers();
	netbuffer->u.serverinfo.maxplayer = (uint8_t)(min((dedicated ? MAXPLAYERS-1 : MAXPLAYERS), cv_maxplayers.value));

	if (!node)
		netbuffer->u.serverinfo.refusereason = 0;
	else if (!cv_allownewplayer.value)
		netbuffer->u.serverinfo.refusereason = 1;
	else if (D_NumPlayers() >= cv_maxplayers.value)
		netbuffer->u.serverinfo.refusereason = 2;
	else
		netbuffer->u.serverinfo.refusereason = 0;

	strncpy(netbuffer->u.serverinfo.gametypename, gametypes[prefgametype]->name,
			sizeof netbuffer->u.serverinfo.gametypename);
	netbuffer->u.serverinfo.modifiedgame = (uint8_t)modifiedgame;
	netbuffer->u.serverinfo.cheatsenabled = CV_CheatsEnabled();

	netbuffer->u.serverinfo.kartvars = (uint8_t) (
		(gamespeed & SV_SPEEDMASK) |
		(dedicated ? SV_DEDICATED : 0)
	);

	CopyCaretColors(netbuffer->u.serverinfo.servername, cv_servername.string,
		MAXSERVERNAME);

	netbuffer->u.serverinfo.maphash = maphash;

	if (!(mapheaderinfo[gamemap-1]->levelflags & LF_NOZONE) && !(mapheaderinfo[prevmap]->zonttl[0]))
		netbuffer->u.serverinfo.iszone = 1;
	else
		netbuffer->u.serverinfo.iszone = 0;

	memset(netbuffer->u.serverinfo.maptitle, 0, sizeof netbuffer->u.serverinfo.maptitle);

	if (!(mapheaderinfo[gamemap-1]->menuflags & LF2_HIDEINMENU) && mapheaderinfo[gamemap-1]->lvlttl[0])
	{
		//strncpy(netbuffer->u.serverinfo.maptitle, (char *)mapheaderinfo[gamemap-1]->lvlttl, sizeof netbuffer->u.serverinfo.maptitle);
		// set up the levelstring
		if (!netbuffer->u.serverinfo.iszone || (mapheaderinfo[gamemap-1]->levelflags & LF_NOZONE))
		{
			if (mapheaderinfo[gamemap-1]->actnum[0])
				snprintf(netbuffer->u.serverinfo.maptitle,
					33,
					"%s %s",
					mapheaderinfo[gamemap-1]->lvlttl, mapheaderinfo[gamemap-1]->actnum);
			else
				snprintf(netbuffer->u.serverinfo.maptitle,
					33,
					"%s",
					mapheaderinfo[gamemap-1]->lvlttl);
		}
		else
		{
			if (mapheaderinfo[gamemap-1]->actnum[0])
			{
				if (snprintf(netbuffer->u.serverinfo.maptitle,
					sizeof netbuffer->u.serverinfo.maptitle,
					"%s %s %s",
					mapheaderinfo[gamemap-1]->lvlttl, mapheaderinfo[gamemap-1]->zonttl, mapheaderinfo[gamemap-1]->actnum) < 0)
				{
					// If there's an encoding error, send UNKNOWN, we accept that the above may be truncated
					strncpy(netbuffer->u.serverinfo.maptitle, "Unknown", sizeof netbuffer->u.serverinfo.maptitle);
				}
			}
			else
			{
				if (snprintf(netbuffer->u.serverinfo.maptitle,
					sizeof netbuffer->u.serverinfo.maptitle,
					"%s %s",
					mapheaderinfo[gamemap-1]->lvlttl, mapheaderinfo[gamemap-1]->zonttl) < 0)
				{
					// If there's an encoding error, send "Unknown", we accept that the above may be truncated
					strncpy(netbuffer->u.serverinfo.maptitle, "Unknown", sizeof netbuffer->u.serverinfo.maptitle);
				}
			}
		}
	}
	else
		strncpy(netbuffer->u.serverinfo.maptitle, "Unknown", sizeof netbuffer->u.serverinfo.maptitle);

	netbuffer->u.serverinfo.actnum = 0;  //mapheaderinfo[gamemap-1]->actnum

	memset(netbuffer->u.serverinfo.httpsource, 0, MAX_MIRROR_LENGTH);

	mirror_length = strlen(httpurl);
	if (mirror_length > MAX_MIRROR_LENGTH)
		mirror_length = MAX_MIRROR_LENGTH;

	if (snprintf(netbuffer->u.serverinfo.httpsource, mirror_length+1, "%s", httpurl) < 0)
		// If there's an encoding error, send nothing, we accept that the above may be truncated
		strncpy(netbuffer->u.serverinfo.httpsource, "", mirror_length);

	netbuffer->u.serverinfo.httpsource[MAX_MIRROR_LENGTH-1] = '\0';

	if (cv_kartusepwrlv.value)
		netbuffer->u.serverinfo.avgpwrlv = K_CalculatePowerLevelAvg();
	else
		netbuffer->u.serverinfo.avgpwrlv = -1;

	p = PutFileNeeded(0);

	HSendPacket(node, false, 0, p - ((uint8_t *)&netbuffer->u));
}

static void SV_SendPlayerInfo(int32_t node)
{
	uint8_t i;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	netbuffer->packettype = PT_PLAYERINFO;

	for (i = 0; i < MAXPLAYERINFO; i++)
	{
		if (i >= MAXPLAYERS || playernode[i] == UINT8_MAX || !playeringame[i])
		{
			netbuffer->u.playerinfo[i].num = 255; // This slot is empty.
			continue;
		}

		netbuffer->u.playerinfo[i].num = i;
		memset(netbuffer->u.playerinfo[i].name, 0x00, sizeof(netbuffer->u.playerinfo[i].name));
		memcpy(netbuffer->u.playerinfo[i].name, player_names[i], sizeof(player_names[i]));

		netbuffer->u.playerinfo[i].name[MAXPLAYERNAME] = '\0';

		if (G_GametypeHasTeams())
		{
			if (!players[i].ctfteam)
				netbuffer->u.playerinfo[i].team = 255;
			else
				netbuffer->u.playerinfo[i].team = (uint8_t)players[i].ctfteam;
		}
		else
		{
			if (players[i].spectator)
				netbuffer->u.playerinfo[i].team = 255;
			else
				netbuffer->u.playerinfo[i].team = 0;
		}

		netbuffer->u.playerinfo[i].score = LSBF_LONG(players[i].score);
		netbuffer->u.playerinfo[i].timeinserver = LSBF_SHORT((uint16_t)(players[i].jointime / TICRATE));
		netbuffer->u.playerinfo[i].skin = (uint16_t)(players[i].skin);

		// Extra data
		netbuffer->u.playerinfo[i].data = 0; //players[i].skincolor;
	}

	HSendPacket(node, false, 0, sizeof(plrinfo) * MAXPLAYERINFO);
}

/** Sends a PT_SERVERCFG packet
  *
  * \param node The destination
  * \return True if the packet was successfully sent
  *
  */
static dboolean SV_SendServerConfig(int32_t node)
{
	dboolean waspacketsent;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	memset(&netbuffer->u.servercfg, 0, sizeof netbuffer->u.servercfg);

	netbuffer->packettype = PT_SERVERCFG;

	netbuffer->u.servercfg.version = VERSION;
	netbuffer->u.servercfg.subversion = SUBVERSION;

	netbuffer->u.servercfg.serverplayer = (uint8_t)serverplayer;
	netbuffer->u.servercfg.totalslotnum = (uint8_t)(doomcom->numslots);
	netbuffer->u.servercfg.gametic = (tic_t)LSBF_LONG(gametic);
	netbuffer->u.servercfg.clientnode = (uint8_t)node;
	netbuffer->u.servercfg.gamestate = (uint8_t)gamestate;
	netbuffer->u.servercfg.gametype = (uint8_t)gametype;
	netbuffer->u.servercfg.modifiedgame = (uint8_t)modifiedgame;
	netbuffer->u.servercfg.dedicated = (dboolean)dedicated;

	netbuffer->u.servercfg.maxplayer = (uint8_t)(min((dedicated ? MAXPLAYERS-1 : MAXPLAYERS), cv_maxplayers.value));
	netbuffer->u.servercfg.allownewplayer = cv_allownewplayer.value;
	netbuffer->u.servercfg.discordinvites = (dboolean)cv_discordinvites.value;

	memcpy(netbuffer->u.servercfg.server_context, server_context, 8);

	{
		const size_t len = sizeof (serverconfig_pak);

#ifdef DEBUGFILE
		if (debugfile)
		{
			fprintf(debugfile, "ServerConfig Packet about to be sent, size of packet:%s to node:%d\n",
				sizeu1(len), node);
		}
#endif

		waspacketsent = HSendPacket(node, true, 0, len);
	}

#ifdef DEBUGFILE
	if (debugfile)
	{
		if (waspacketsent)
		{
			fprintf(debugfile, "ServerConfig Packet was sent\n");
		}
		else
		{
			fprintf(debugfile, "ServerConfig Packet could not be sent right now\n");
		}
	}
#endif

	return waspacketsent;
}

static dboolean SV_ResendingSavegameToAnyone(void)
{
	int32_t i;

	for (i = 0; i < MAXNETNODES; i++)
		if (resendingsavegame[i])
			return true;
	return false;
}

static void SV_SendSaveGame(int32_t node, dboolean resending)
{
	size_t length, compressedlen;
	savebuffer_t save = {0};
	uint8_t *compressedsave;
	uint8_t *buffertosend;

	// first save it in a malloced buffer
	if (P_SaveBufferAlloc(&save, NETSAVEGAMESIZE) == false)
	{
		CONS_Alert(CONS_ERROR, M_GetText("No more free memory for savegame\n"));
		return;
	}

	// Leave room for the uncompressed length.
	save.p += sizeof(uint32_t);

	P_SaveNetGame(&save, resending);

	length = save.p - save.buffer;
	if (length > NETSAVEGAMESIZE)
	{
		P_SaveBufferFree(&save);
		I_Error("Savegame buffer overrun");
	}

	// Allocate space for compressed save: one byte fewer than for the
	// uncompressed data to ensure that the compression is worthwhile.
	compressedsave = Z_Malloc(length - 1, PU_STATIC, NULL);
	if (!compressedsave)
	{
		CONS_Alert(CONS_ERROR, M_GetText("No more free memory for savegame\n"));
		return;
	}

	// Attempt to compress it.
#if defined USE_LZ4
	if ((compressedlen = LZ4_compress_fast((char*)(save.buffer + sizeof(uint32_t)), (char*)(compressedsave + sizeof(uint32_t)), length - sizeof(uint32_t), length - sizeof(uint32_t) - 1, LZ4_ACCEL)))
#elif defined HAVE_ZLIB
	int compresstatus = compress2(compressedsave + sizeof(uint32_t), &compressedlen, save.buffer + sizeof(uint32_t), length - sizeof(uint32_t), ZLIB_LEVEL);
	if (compresstatus == Z_OK)
#else
	if ((compressedlen = lzf_compress(save.buffer + sizeof(uint32_t), length - sizeof(uint32_t), compressedsave + sizeof(uint32_t), length - sizeof(uint32_t) - 1)))
#endif
	{
		CONS_Printf("Compressed savegame of size %zu\n", length - sizeof(uint32_t));
		CONS_Printf("Compressed savegame is %zu\n", compressedlen);

		// Compressing succeeded; send compressed data
		P_SaveBufferFree(&save);

		// State that we're compressed.
		buffertosend = compressedsave;
		WRITEUINT32(compressedsave, length - sizeof(uint32_t));
		length = compressedlen + sizeof(uint32_t);
	}
	else
	{
		CONS_Alert(CONS_WARNING, "Compressing failed, sending original save game\n");

		// Compression failed to make it smaller; send original
		Z_Free(compressedsave);

		// State that we're not compressed
		buffertosend = save.buffer;
		WRITEUINT32(save.buffer, 0);
	}

	AddRamToSendQueue(node, buffertosend, length, SF_Z_RAM, 0);

	// Remember when we started sending the savegame so we can handle timeouts
	sendingsavegame[node] = true;
	freezetimeout[node] = I_GetTime() + jointimeout + length / 1024; // 1 extra tic for each kilobyte
}

#ifdef DUMPCONSISTENCY
#define TMPSAVENAME "badmath.sav"
static consvar_t cv_dumpconsistency = CVAR_INIT ("dumpconsistency", "Off", CV_SAVE|CV_NETVAR, CV_OnOff, NULL);

static void SV_SavedGame(void)
{
	size_t length;
	savebuffer_t save = {0};
	char tmpsave[264];

	if (!cv_dumpconsistency.value)
		return;

	snprintf(tmpsave, sizeof(tmpsave), "%s" PATHSEP TMPSAVENAME, srb2home);

	// first save it in a malloced buffer
	if (P_SaveBufferAlloc(&save, NETSAVEGAMESIZE) == false)
	{
		CONS_Alert(CONS_ERROR, M_GetText("No more free memory for savegame\n"));
		return;
	}

	P_SaveNetGame(&save, false);

	length = save.p - save.buffer;
	if (length > NETSAVEGAMESIZE)
	{
		P_SaveBufferFree(&save);
		I_Error("Savegame buffer overrun");
	}

	// then save it!
	if (!FIL_WriteFile(tmpsave, save.buffer, length))
		CONS_Printf(M_GetText("Didn't save %s for netgame"), tmpsave);

	P_SaveBufferFree(&save);
}

#undef  TMPSAVENAME
#endif
#define TMPSAVENAME "$$$.sav"


static void CL_LoadReceivedSavegame(dboolean reloading)
{
	savebuffer_t save = {0};
	size_t length, decompressedlen;
	char tmpsave[264];
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	snprintf(tmpsave, sizeof(tmpsave), "%s" PATHSEP TMPSAVENAME, srb2home);

	if (P_SaveBufferFromFile(&save, tmpsave) == false)
	{
		I_Error("Can't read savegame sent");
		return;
	}

	length = save.size;
	CONS_Printf(M_GetText("Loading savegame length %s\n"), sizeu1(length));

	// Decompress saved game if necessary.
	decompressedlen = READUINT32(save.p);
	if (decompressedlen > 0)
	{
		uint8_t *decompressedbuffer = Z_Malloc(decompressedlen, PU_STATIC, NULL);

		CONS_Printf("Decompressing compressed savegame of size %zu\n", length - sizeof(uint32_t));

#if defined USE_LZ4
		LZ4_decompress_safe((char*)save.p, (char*)decompressedbuffer, length - sizeof(uint32_t), decompressedlen);
#elif defined HAVE_ZLIB
		uncompress(decompressedbuffer, &decompressedlen, save.p, length - sizeof(uint32_t));
#else
		lzf_decompress(save.p, length - sizeof(uint32_t), decompressedbuffer, decompressedlen);
#endif

		CONS_Printf("Decompressed save is %zu\n", decompressedlen);

		P_SaveBufferFree(&save);
		P_SaveBufferFromExisting(&save, decompressedbuffer, decompressedlen);
	}

	paused = false;
	demo.playback = false;
	demo.single_seed = false;
	demo.title = false;
	titlemapinaction = false;
	automapactive = false;

	// load a base level
	if (P_LoadNetGame(&save, reloading))
	{
		if (!reloading)
		{
			CON_LogMessage(va(M_GetText("Map is now \"%s"), G_BuildMapName(gamemap)));

			if (strlen(mapheaderinfo[gamemap-1]->lvlttl) > 0)
			{
				CON_LogMessage(va(": %s", mapheaderinfo[gamemap-1]->lvlttl));
				if (strlen(mapheaderinfo[gamemap-1]->zonttl) > 0)
					CON_LogMessage(va(" %s", mapheaderinfo[gamemap-1]->zonttl));
				else if (!(mapheaderinfo[gamemap-1]->levelflags & LF_NOZONE))
					CON_LogMessage(M_GetText(" Zone"));
				if (mapheaderinfo[gamemap-1]->actnum[0])
					CON_LogMessage(va(" %s", mapheaderinfo[gamemap-1]->actnum));
			}

			CON_LogMessage("\"\n");
		}
	}

	// done
	P_SaveBufferFree(&save);

	if (unlink(tmpsave) == -1)
	{
		CONS_Alert(CONS_ERROR, M_GetText("Can't delete %s\n"), tmpsave);
	}

	Consistancy(&consistancy[gametic%BACKUPTICS], gametic%BACKUPTICS);
	if (!reloading)
		CON_ToggleOff();

	// Tell the server we have received and reloaded the gamestate
	// so they know they can resume the game
	netbuffer->packettype = PT_RECEIVEDGAMESTATE;
	HSendPacket(servernode, true, 0, 0);

	if (reloading)
	{
		int i;
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (memcmp(priorKeys[i], players[i].public_key, sizeof(priorKeys[i])) != 0)
			{
				HandleSigfail("Gamestate reload contained new keys");
				break;
			}
		}
	}
}

static void CL_ReloadReceivedSavegame(void)
{
	int32_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		LUA_InvalidatePlayer(&players[i]);
		snprintf(player_names[i], MAXPLAYERNAME+1, "Player %c", 'A' + i);
	}

	CL_LoadReceivedSavegame(true);

	neededtic = max(neededtic, gametic);
	maketic = neededtic;

	for (i = 0; i <= r_splitscreen; i++)
	{
		P_ForceLocalAngle(&players[displayplayers[i]], players[displayplayers[i]].angleturn, false);
	}

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		camera[i].subsector = R_PointInSubsectorFast(camera[i].x, camera[i].y);
	}

	cl_redownloadinggamestate = false;

	CONS_Printf(M_GetText("Game state reloaded\n"));
}

static void SendAskInfo(int32_t node)
{
	tic_t asktime;
	doomdata_t *netbuffer;

	if (node == -1)
		return;

	netbuffer = DOOMCOM_DATA(doomcom);

	if (node != 0 && node != BROADCASTADDR &&
			cv_rendezvousserver.string[0])
	{
		if (node > 0)
			I_NetRequestHolePunch(node);
	}

	asktime = I_GetTime();

	netbuffer->packettype = PT_ASKINFO;
	netbuffer->u.askinfo.version = VERSION;
	netbuffer->u.askinfo.time = (tic_t)LSBF_LONG(asktime);

	// Even if this never arrives due to the host being firewalled, we've
	// now allowed traffic from the host to us in, so once the MS relays
	// our address to the host, it'll be able to speak to us.
	HSendPacket(node, false, 0, sizeof (askinfo_pak));
}

serverelem_t serverlist[MAXSERVERLIST];
uint32_t serverlistcount = 0;
uint32_t serverlistultimatecount = 0;

static dboolean resendserverlistnode[MAXNETNODES];
static tic_t serverlistepoch;

static void SL_ClearServerList(int32_t connectedserver)
{
	uint32_t i;

	for (i = 0; i < serverlistcount; i++)
		if (connectedserver != serverlist[i].node)
		{
			Net_CloseConnection(serverlist[i].node|FORCECLOSE);
			serverlist[i].node = 0;
		}
	serverlistcount = 0;
	memset(resendserverlistnode, 0, sizeof resendserverlistnode);
	M_UpdateNumServerPages();
}

static uint32_t SL_SearchServer(int32_t node)
{
	uint32_t i;
	for (i = 0; i < serverlistcount; i++)
		if (serverlist[i].node == node)
			return i;

	return UINT32_MAX;
}

static void SL_InsertServer(serverinfo_pak* info, int8_t node)
{
	uint32_t i;

	resendserverlistnode[node] = false;

	// search if not already on it
	i = SL_SearchServer(node);
	if (i == UINT32_MAX)
	{
		// not found add it
		if (serverlistcount >= MAXSERVERLIST)
			return; // list full

		if (info->_255 != 255)
			return;/* old packet format */

		if (info->packetversion != PACKETVERSION)
			return;/* old new packet format */

#ifndef COMMITVERSION
		if (info->version != VERSION)
			return; // Not same version.

		if (info->subversion != SUBVERSION)
			return; // Close, but no cigar.
#endif

		if (!fastcmp(info->application, SRB2APPLICATION))
			return;/* that's a different mod */

		i = serverlistcount++;
		M_UpdateNumServerPages();
	}

	serverlist[i].info = *info;
	serverlist[i].node = node;

	// resort server list
	M_SortServerList();
}

void CL_QueryServerList (msg_server_t *server_list)
{
	int32_t i;

	CL_UpdateServerList();

	serverlistepoch = I_GetTime();

	for (i = 0; server_list[i].header.buffer[0]; i++)
	{
		// Make sure MS version matches our own, to
		// thwart nefarious servers who lie to the MS.

		/* lol bruh, that version COMES from the servers */
		//if (fastcmp(version, server_list[i].version))
		{
			int32_t node = I_NetMakeNodewPort(server_list[i].ip, server_list[i].port);
			if (node == -1)
				break; // no more node free
			SendAskInfo(node);
			resendserverlistnode[node] = true;
			// Leave this node open. It'll be closed if the
			// request times out (CL_TimeoutServerList).
		}
	}

	serverlistultimatecount = i;
}

#define SERVERLISTRESENDRATE NEWTICRATE

void CL_TimeoutServerList(void)
{
	if (netgame && serverlistultimatecount > serverlistcount)
	{
		const tic_t timediff = I_GetTime() - serverlistepoch;
		const tic_t timetoresend = timediff % SERVERLISTRESENDRATE;
		const dboolean timedout = timediff > connectiontimeout;

		if (timedout || (timediff > 0 && timetoresend == 0))
		{
			int32_t node;

			for (node = 1; node < MAXNETNODES; ++node)
			{
				if (resendserverlistnode[node])
				{
					if (timedout)
						Net_CloseConnection(node|FORCECLOSE);
					else
						SendAskInfo(node);
				}
			}

			if (timedout)
				serverlistultimatecount = serverlistcount;
		}
	}
}

void CL_UpdateServerList (void)
{
	SL_ClearServerList(0);

	if (!netgame && I_NetOpenSocket)
	{
		if (I_NetOpenSocket())
		{
			netgame = true;
			multiplayer = true;
		}
	}

	// search for local servers
	if (netgame)
		SendAskInfo(BROADCASTADDR);
}

static void M_ConfirmConnect(int32_t choice)
{
	if (choice == MA_YES)
	{
		if (totalfilesrequestednum > 0)
		{
#ifdef HAVE_CURL
			if (http_source[0] == '\0' || curl_failedwebdownload)
#endif
			{
				if (CL_SendFileRequest())
				{
					cl_mode = CL_DOWNLOADFILES;
				}
				else
				{
					cl_mode = CL_DOWNLOADFAILED;
				}
			}
#ifdef HAVE_CURL
			else
				cl_mode = CL_PREPAREHTTPFILES;
#endif
		}
		else
			cl_mode = CL_LOADFILES;

		M_ClearMenusEx(false, true);
	}
	else if (choice == MA_NO)
	{
		cl_mode = CL_ABORTED;
	}
}

static dboolean CL_FinishedFileList(void)
{
	int32_t i;
	char *downloadsize = NULL;
	//CONS_Printf(M_GetText("Checking files...\n"));
	i = CL_CheckFiles();
	if (i == 4) // still checking ...
	{
		return true;
	}
	else if (i == 3) // too many files
	{
		D_QuitNetGame();
		CL_Reset();
		M_ReturnToConnectMenu();
		M_StartMessage2("Too many addons", M_GetText(
			"You have too many addons loaded\n"
			"to add ones the server is using.\n"
			"Please restart SRB2Kart before connecting.\n\n"
		), MB_NONE, MM_NOTHING, "Press any key", NULL);
		return false;
	}
	else if (i == 2) // cannot join for some reason
	{
		D_QuitNetGame();
		CL_Reset();
		M_ReturnToConnectMenu();
		M_StartMessage2("Wrong Addons", M_GetText(
			"You have the wrong addons loaded.\n\n"
			"To play on this server, restart\n"
			"the game and don't load any addons.\n"
			"SRB2Kart will automatically add\n"
			"everything you need when you join.\n\n"
		), MB_NONE, MM_NOTHING, "Press any key", NULL);
		return false;
	}
	else if (i == 1)
	{
		if (serverisfull)
		{
			M_StartMessage2("Full Server" ,M_GetText(
				"This server is full!\n"
				"\n"
				"You may load server addons (if any), and wait for a slot.\n"
			), MB_ROUTINE(M_ConfirmConnect), MM_YESNO, M_MessageConfirmText(false, keyboardcontrolled), M_MessageCancelText(false, keyboardcontrolled));
			cl_mode = CL_CONFIRMCONNECT;
			ChangeServMusic(SERVMUS_2, true,true);
		}
		else
			cl_mode = CL_LOADFILES;
	}
	else
	{
		// must download something
		// can we, though?
#ifdef HAVE_CURL
		if (http_source[0] == '\0' || curl_failedwebdownload)
#endif
		{
			if (!CL_CheckDownloadable()) // nope!
			{
				D_QuitNetGame();
				CL_Reset();
				M_ReturnToConnectMenu();
				M_StartMessage2("Download Failed", M_GetText(
					"An error occured when trying to\n"
					"download missing addons.\n"
					"(This is almost always a problem\n"
					"with the server, not your game.)\n\n"
					"See the console or log file\n"
					"for additional details.\n\n"
				), MB_NONE, MM_NOTHING, "Press any key", NULL);
				return false;
			}
		}

#ifdef HAVE_CURL
		if (!curl_failedwebdownload)
#endif
		{
			downloadcompletednum = 0;
			downloadcompletedsize = 0;
			totalfilesrequestednum = 0;
			totalfilesrequestedsize = 0;

			for (i = 0; i < fileneedednum; i++)
				if (fileneeded[i].status == FS_NOTFOUND || fileneeded[i].status == FS_BADHASH)
				{
					totalfilesrequestednum++;
					totalfilesrequestedsize += fileneeded[i].totalsize;
				}

			if (totalfilesrequestedsize>>20 >= 10)
				downloadsize = Z_StrDup(va("%uM",totalfilesrequestedsize>>20));
			else
				downloadsize = Z_StrDup(va("%uK",totalfilesrequestedsize>>10));

			if (serverisfull)
				M_StartMessage2("Full Server", va(M_GetText(
					"This server is full!\n"
					"Download of %s additional content is required to join.\n"
					"\n"
					"You may download, load server addons, and wait for a slot.\n"
				), downloadsize), MB_ROUTINE(M_ConfirmConnect), MM_YESNO, M_MessageConfirmText(false, keyboardcontrolled), M_MessageCancelText(false, keyboardcontrolled));
			else
				M_StartMessage2("Downloading Content", va(M_GetText(
					"Download of %s additional content is required to join.\n"
				), downloadsize), MB_ROUTINE(M_ConfirmConnect), MM_YESNO, M_MessageConfirmText(false, keyboardcontrolled), M_MessageCancelText(false, keyboardcontrolled));

			Z_Free(downloadsize);
			cl_mode = CL_CONFIRMCONNECT;
			ChangeServMusic(SERVMUS_2, true,true);
		}
#ifdef HAVE_CURL
		else
		{
			if (CL_SendFileRequest())
			{
				cl_mode = CL_DOWNLOADFILES;
			}
			else
			{
				cl_mode = CL_DOWNLOADFAILED;
			}
		}
#endif
	}
	return true;
}

/** Called by CL_ServerConnectionTicker
  *
  * \param asksent The last time we asked the server to join. We re-ask every second in case our request got lost in transmit.
  * \return False if the connection was aborted
  * \sa CL_ServerConnectionTicker
  * \sa CL_ConnectToServer
  *
  */
static dboolean CL_ServerConnectionSearchTicker(tic_t *asksent)
{
	int32_t i;

	// serverlist is updated by GetPacket function
	if (serverlistcount > 0)
	{
		// this can be a responce to our broadcast request
		if (servernode == -1 || servernode >= MAXNETNODES)
		{
			i = 0;
			servernode = serverlist[i].node;
			CONS_Printf(M_GetText("Found, "));
		}
		else
		{
			i = SL_SearchServer(servernode);
			if (i < 0)
				return true;
		}

		// Quit here rather than downloading files and being refused later.
		if (serverlist[i].info.refusereason)
		{
			serverisfull = true;
		}

		if (client)
		{
#ifdef COMMITVERSION
			// Commits do not match? Do not connect!
			if (memcmp(serverlist[i].info.commit,
						comprevision_abbrev_bin,
						GIT_SHA_ABBREV))
			{
				char theirs[GIT_SHA_ABBREV * 2 + 1];
				uint8_t n;

				for (n = 0; n < GIT_SHA_ABBREV; ++n)
				{
					snprintf(&theirs[n * 2], sizeof(theirs) - n * 2, "%02hhx",
							serverlist[i].info.commit[n]);
				}

				D_QuitNetGame();
				CL_Reset();
				M_ReturnToConnectMenu();
				M_StartMessage2("Connection Failed", va(
							"Your binary differs from the server.\n"
							"  Yours: %.*s\n"
							"Theirs: %s\n",
							GIT_SHA_ABBREV * 2, comprevision, theirs), MB_NONE, MM_NOTHING, "Press any key", NULL);
				return false;
			}
#endif
#ifdef HAVE_CURL
			if (serverlist[i].info.httpsource[0])
				strncpy(http_source, serverlist[i].info.httpsource, MAX_MIRROR_LENGTH);
			else
				http_source[0] = '\0';
#else
			if (serverlist[i].info.httpsource[0])
				CONS_Printf("We received a http url from the server, however it will not be used as this build lacks curl support (%s)\n", serverlist[i].info.httpsource);
#endif
			D_ParseFileneeded(serverlist[i].info.fileneedednum, serverlist[i].info.fileneeded, 0);
			if (serverlist[i].info.kartvars & SV_LOTSOFADDONS)
			{
				cl_mode = CL_ASKFULLFILELIST;
				cl_lastcheckedfilecount = 0;
				return true;
			}

			cl_mode = cv_serverinfoscreen.value ? CL_VIEWSERVER : CL_CHECKFILES;
			ChangeServMusic(SERVMUS_1, true,false);
		}
		else
		{
			cl_mode = CL_ASKJOIN; // files need not be checked for the server.
			*asksent = 0;
		}

		return true;
	}

	// Ask the info to the server (askinfo packet)
	if (I_GetTime() >= *asksent)
	{
		SendAskInfo(servernode);
		*asksent = I_GetTime() + NEWTICRATE;
	}

	return true;
}

/** Called by CL_ConnectToServer
  *
  * \param tmpsave The name of the gamestate file???
  * \param oldtic Used for knowing when to poll events and redraw
  * \param asksent The last time we asked the server to join. We re-ask every second in case our request got lost in transmit.
  * \return False if the connection was aborted
  * \sa CL_ServerConnectionSearchTicker
  * \sa CL_ConnectToServer
  *
  */
static dboolean CL_ServerConnectionTicker(const char *tmpsave, tic_t *oldtic, tic_t *asksent)
{
	dboolean waitmore;
	int32_t i;

	switch (cl_mode)
	{
		case CL_SEARCHING:
			if (!CL_ServerConnectionSearchTicker(asksent))
				return false;
			break;

		case CL_ASKFULLFILELIST:
			if (cl_lastcheckedfilecount == UINT16_MAX) // All files retrieved
			{
				cl_mode = cv_serverinfoscreen.value ? CL_VIEWSERVER : CL_CHECKFILES;
				ChangeServMusic(SERVMUS_1, true,false);
			}
			else if (fileneedednum != cl_lastcheckedfilecount || I_GetTime() >= *asksent)
			{
				if (CL_AskFileList(fileneedednum))
				{
					cl_lastcheckedfilecount = fileneedednum;
					*asksent = I_GetTime() + NEWTICRATE;
				}
			}
			break;
		case CL_CHECKFILES:
			if (!CL_FinishedFileList())
				return false;
			break;
#ifdef HAVE_CURL
		case CL_PREPAREHTTPFILES:
			if (http_source[0])
			{
				for (i = 0; i < fileneedednum; i++)
					if (fileneeded[i].status == FS_NOTFOUND || fileneeded[i].status == FS_BADHASH)
					{
						curl_transfers++;
					}

				cl_mode = CL_DOWNLOADHTTPFILES;
			}
			break;

		case CL_DOWNLOADHTTPFILES:
			waitmore = false;
			for (i = 0; i < fileneedednum; i++)
				if (fileneeded[i].status == FS_NOTFOUND || fileneeded[i].status == FS_BADHASH)
				{
					if (!curl_running)
						CURLPrepareFile(http_source, i);
					waitmore = true;
					break;
				}

#ifndef HAVE_THREADS
			if (curl_running)
				CURLGetFile();
#endif

			if (waitmore)
				break; // exit the case

			if (curl_failedwebdownload && !curl_transfers)
			{
				CONS_Printf("One or more files failed to download, falling back to internal downloader\n");
				cl_mode = CL_CHECKFILES;
				break;
			}

			if (!curl_transfers)
				cl_mode = CL_LOADFILES;

			break;
#endif
		case CL_DOWNLOADFILES:
			waitmore = false;
			for (i = 0; i < fileneedednum; i++)
				if (fileneeded[i].status == FS_DOWNLOADING
					|| fileneeded[i].status == FS_REQUESTED)
				{
					waitmore = true;
					break;
				}
			if (waitmore)
				break; // exit the case

			cl_mode = CL_LOADFILES;
			break;
		case CL_DOWNLOADFAILED:
			{
				CONS_Printf(M_GetText("Legacy downloader request packet failed.\n"));
				CONS_Printf(M_GetText("Network game synchronization aborted.\n"));
				D_QuitNetGame();
				CL_Reset();
				M_ReturnToConnectMenu();
				M_StartMessage2("Download Failed", M_GetText(
					"The direct download encountered an error.\n"
					"See the logfile for more info.\n"
				), MB_NONE, MM_NOTHING, "Press any key", NULL);
				return false;
			}
		case CL_LOADFILES:
			if (CL_LoadServerFiles())
				cl_mode = CL_SETUPFILES;

			break;
		case CL_SETUPFILES:
			if (P_PartialAddGetStage() < 0 || P_MultiSetupWadFiles(false))
			{
				*asksent = 0; //This ensure the first join ask is right away
				firstconnectattempttime = I_GetTime();
				cl_mode = CL_SENDKEY;
			}
			break;
		case CL_ASKJOIN:
			if (cv_connectawaittime.value && firstconnectattempttime + NEWTICRATE*60*cv_connectawaittime.value < I_GetTime() && !server)
			{
				CONS_Printf("%d minute wait time exceeded.\n", cv_connectawaittime.value);
				CONS_Printf(M_GetText("Network game synchronization aborted.\n"));
				D_QuitNetGame();
				CL_Reset();
				M_ReturnToConnectMenu();
				M_StartMessage2("Time Exceeded", va(
					"%d minute wait time exceeded.\n"
					"You may retry connection.\n",
					cv_connectawaittime.value
				), MB_NONE, MM_NOTHING, "Press any key", NULL);
				return false;
			}
			// prepare structures to save the file
			// WARNING: this can be useless in case of server not in GS_LEVEL
			// but since the network layer doesn't provide ordered packets...
			CL_PrepareDownloadSaveGame(tmpsave);
			if (I_GetTime() >= *asksent && CL_SendJoin())
			{
				*asksent = I_GetTime() + NEWTICRATE*3;
				cl_mode = CL_WAITJOINRESPONSE;
			}
			break;
		case CL_WAITJOINRESPONSE:
			if (I_GetTime() >= *asksent)
			{
				cl_mode = CL_SENDKEY;
			}
			break;
		case CL_SENDKEY:
			if (I_GetTime() >= *asksent && CL_SendKey())
			{
				*asksent = I_GetTime() + NEWTICRATE*3;
				cl_mode = CL_WAITCHALLENGE;
			}
			break;
		case CL_WAITCHALLENGE:
			if (I_GetTime() >= *asksent)
			{
				cl_mode = CL_SENDKEY;
			}
			break;
		case CL_DOWNLOADSAVEGAME:
			// At this state, the first (and only) needed file is the gamestate
			if (fileneeded[0].status == FS_FOUND)
			{
				// Gamestate is now handled within CL_LoadReceivedSavegame()
				CL_LoadReceivedSavegame(false);
				cl_mode = CL_CONNECTED;
				break;
			} // don't break case continue to CL_CONNECTED
			else
				break;
		case CL_CONNECTED:
		case CL_CONFIRMCONNECT: //logic is handled by M_ConfirmConnect
		default:
			break;

		// Connection closed by cancel, timeout or refusal.
		case CL_ABORTED:
			cl_mode = CL_SEARCHING;
			return false;

	}

	GetPackets();
	Net_AckTicker();

	// Call it only once by tic
	if (*oldtic != I_GetTime())
	{
		if (I_Interrupted())
		{
			I_Quit();
		}

		I_OsPolling();

		// Needs to be updated here for M_DrawEggaChannel
		renderdeltatics = FRACUNIT;
		R_SetTimeFrac(FRACUNIT);

		if (netgame)
		{
			event_t *ev;
			for (; eventtail != eventhead; eventtail = (eventtail+1) & (MAXEVENTS-1))
			{
				ev = &events[eventtail];

				G_MapEventsToControls(ev);

				if (ev->type == ev_keydown && ev->data1 > 0 && ev->data1 < NUMKEYS)
				{
					// Record keyboard presses
					menuKey = ev->data1;
				}
			}

#ifdef HAVE_THREADS
			I_lock_mutex(&m_menu_mutex);
#endif
			M_UpdateMenuCMD(0, true);

			if (cl_mode == CL_CONFIRMCONNECT)
			{
				if (messagebox.active)
					M_HandleMessageBox(menuKey);
			}
			else
			{
				if (menustack[0] && cl_mode == CL_VIEWSERVER)
				{
					M_Ticker();
				}
				else if (M_MenuButtonPressed(0, MBT_CANCEL))
					cl_mode = CL_ABORTED;
			}

#ifdef HAVE_THREADS
			I_unlock_mutex(m_menu_mutex);
#endif
		}

		if (cl_mode == CL_ABORTED)
		{
			CONS_Printf(M_GetText("Network game synchronization aborted.\n"));
//			M_StartMessage2("Sync Aborted", M_GetText("Network game synchronization aborted.\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);

			D_QuitNetGame();
			CL_Reset();
			M_ReturnToConnectMenu();
			return false;
		}

		if (client && (cl_mode == CL_DOWNLOADFILES || cl_mode == CL_DOWNLOADSAVEGAME))
			FileReceiveTicker();

		// why are these here? this is for servers, we're a client
		//if (key == 's' && server)
		//	doomcom->numnodes = (int16_t)pnumnodes;
		//FileSendTicker();
		*oldtic = I_GetTime();

		if (client && cl_mode != CL_CONNECTED && cl_mode != CL_ABORTED)
		{
			if (cl_mode != CL_DOWNLOADFILES && cl_mode != CL_DOWNLOADSAVEGAME)
			{
				// Draw something please!
				F_MenuPresTicker(true);

				if (!menustack[0] && fastcmp(curbgname, "SRB2KTTL"))
					F_TitleBGScroll(5, menuanimtimer);
				else if (!menustack[0])
					F_SkyScroll(curbgxspeed, curbgyspeed, curbgname); // title sky
			}
			CL_DrawConnectionStatus();
			HU_TickSongCredits();
#ifdef HAVE_THREADS
			I_lock_mutex(&m_menu_mutex);
#endif
			M_Drawer(); // Needed for drawing messageboxes on the connection screen
			if (cv_songcredits.value && !( hu_showscores && (netgame || multiplayer) ))
				HU_DrawSongCredits(); // Needed for music credits on the connection screen
#ifdef HAVE_THREADS
			I_unlock_mutex(m_menu_mutex);
#endif
			I_UpdateNoVsync(); // page flip or blit buffer
			if (moviemode)
				M_SaveFrame();
			S_UpdateSounds();
			S_UpdateClosedCaptions();
		}
	}
	else
	{
		I_Sleep(cv_sleep.value);
		I_UpdateTime(cv_timescale.value);
	}

	return true;
}

/** Use adaptive send using net_bandwidth and stat.sendbytes
  *
  * \todo Better description...
  *
  */
static void CL_ConnectToServer(void)
{
	int32_t pnumnodes, nodewaited = doomcom->numnodes, i;
	tic_t oldtic;
	tic_t asksent;
	char tmpsave[264];

	snprintf(tmpsave, sizeof(tmpsave), "%s" PATHSEP TMPSAVENAME, srb2home);

	lastfilenum = -1;
	receivedplayerinfo = false;

	cl_mode = CL_SEARCHING;

	// Don't get a corrupt savegame error because tmpsave already exists
	if (FIL_FileExists(tmpsave) && unlink(tmpsave) == -1)
		I_Error("Can't delete %s\n", tmpsave);

	if (netgame)
	{
		if (servernode < 0 || servernode >= MAXNETNODES)
			CONS_Printf(M_GetText("Searching for a server...\n"));
		else
			CONS_Printf(M_GetText("Contacting the server...\n"));
	}

	if (gamestate == GS_INTERMISSION)
		Y_EndIntermission(); // clean up intermission graphics etc
	if (gamestate == GS_VOTING)
		Y_EndVote();

	DEBFILE(va("waiting %d nodes\n", doomcom->numnodes));
	G_SetGamestate(GS_WAITINGPLAYERS);
	if (wipegamestate == GS_MENU)
		M_ClearMenus(true);
	wipegamestate = GS_WAITINGPLAYERS;

	ClearAdminPlayers();
	Schedule_Clear();
	Automate_Clear();
	K_ClearClientPowerLevels();

	pnumnodes = 1;
	oldtic = 0;
	asksent = 0;
	firstconnectattempttime = I_GetTime();

	i = SL_SearchServer(servernode);

	if (i != -1)
	{
		char *gametypestr = serverlist[i].info.gametypename;
		CONS_Printf(M_GetText("Connecting to: %s\n"), serverlist[i].info.servername);
		gametypestr[sizeof serverlist[i].info.gametypename - 1] = '\0';
		CONS_Printf(M_GetText("Gametype: %s\n"), gametypestr);
		CONS_Printf(M_GetText("Version: %d.%d\n"),
		 serverlist[i].info.version, serverlist[i].info.subversion);
	}
	SL_ClearServerList(servernode);

	for (i = 0; i < MAXPLAYERS; i++)
	{
		CL_ClearPlayer(i);
	}

	do
	{
		// If the connection was aborted for some reason, leave
		if (!CL_ServerConnectionTicker(tmpsave, &oldtic, &asksent))
		{
			if (P_PartialAddGetStage() >= 0)
				P_MultiSetupWadFiles(true); // in case any partial adds were done

			return;
		}

		if (server)
		{
			pnumnodes = 0;
			for (i = 0; i < MAXNETNODES; i++)
				if (nodeingame[i])
					pnumnodes++;
		}
	}
	while (!(cl_mode == CL_CONNECTED && (client || (server && nodewaited <= pnumnodes))));

	if (netgame)
		F_StartWaitingPlayers();
	DEBFILE(va("Synchronisation Finished\n"));

	displayplayers[0] = consoleplayer;
}

static void Command_ShowBan(void) //Print out ban list
{
	size_t i;
	const char *address, *mask, *reason, *username;
	time_t unbanTime = NO_BAN_TIME;
	const time_t curTime = time(NULL);

	if (I_GetBanAddress)
		CONS_Printf(M_GetText("Ban List:\n"));
	else
		return;

	for (i = 0; (address = I_GetBanAddress(i)) != NULL; i++)
	{
		unbanTime = NO_BAN_TIME;
		if (I_GetUnbanTime)
			unbanTime = I_GetUnbanTime(i);

		if (unbanTime != NO_BAN_TIME && curTime >= unbanTime)
			continue;

		CONS_Printf("%s: ", sizeu1(i+1));

		if (I_GetBanUsername && (username = I_GetBanUsername(i)) != NULL)
			CONS_Printf("%s - ", username);

		if (!I_GetBanMask || (mask = I_GetBanMask(i)) == NULL)
			CONS_Printf("%s", address);
		else
			CONS_Printf("%s/%s", address, mask);

		if (I_GetBanReason && (reason = I_GetBanReason(i)) != NULL)
			CONS_Printf(" - %s", reason);

		if (unbanTime != NO_BAN_TIME)
		{
			 // these are fudged a little to match what a joiner sees
			int minutes = ((unbanTime - curTime) + 30) / 60;
			int hours = (minutes + 1) / 60;
			int days = (hours + 1) / 24;
			if (days)
				CONS_Printf(" (%d day%s)", days, days > 1 ? "s" : "");
			else if (hours)
				CONS_Printf(" (%d hour%s)", hours, hours > 1 ? "s" : "");
			else if (minutes)
				CONS_Printf(" (%d minute%s)", minutes, minutes > 1 ? "s" : "");
			else
				CONS_Printf(" (<1 minute)");
		}

		CONS_Printf("\n");
	}

	if (i == 0 && !address)
		CONS_Printf(M_GetText("(empty)\n"));
}

static dboolean bansLoaded = false;
// If you're a community contributor looking to improve how bans are written, please
// offer your changes back to our Git repository. Kart Krew reserve the right to
// utilise format numbers in use by community builds for different layouts.
#define BANFORMAT 1

void D_SaveBan(void)
{
	FILE *f;
	size_t i;
	const char *address, *mask;
	const char *username, *reason;
	const time_t curTime = time(NULL);
	time_t unbanTime = NO_BAN_TIME;
	const char *path = va("%s"PATHSEP"%s", srb2home, "ban.txt");

	if (bansLoaded != true)
	{
		// You didn't even get to ATTEMPT to load bans.txt.
		// Don't immediately save nothing over it.
		return;
	}

	f = fopen(path, "w");

	if (!f)
	{
		CONS_Alert(CONS_WARNING, M_GetText("Could not save ban list into ban.txt\n"));
		return;
	}

	// Add header.
	fprintf(f, "BANFORMAT %d\n", BANFORMAT);

	for (i = 0; (address = I_GetBanAddress(i)) != NULL; i++)
	{
		if (I_GetUnbanTime)
		{
			unbanTime = I_GetUnbanTime(i);
		}
		else
		{
			unbanTime = NO_BAN_TIME;
		}

		if (unbanTime != NO_BAN_TIME && curTime >= unbanTime)
		{
			// This one has served their sentence.
			// We don't need to save them in the file anymore.
			continue;
		}

		mask = NULL;
		if (!I_GetBanMask || (mask = I_GetBanMask(i)) == NULL)
			fprintf(f, "%s/0", address);
		else
			fprintf(f, "%s/%s", address, mask);

		// TODO: it'd be nice to convert this to an actual date-time,
		// so it'd be easier to edit outside of the game.
		fprintf(f, " %ld", (long)unbanTime);

		username = NULL;
		if (I_GetBanUsername && (username = I_GetBanUsername(i)) != NULL)
			fprintf(f, " \"%s\"", username);
		else
			fprintf(f, " \"%s\"", "Direct IP ban");

		reason = NULL;
		if (I_GetBanReason && (reason = I_GetBanReason(i)) != NULL)
			fprintf(f, " \"%s\"\n", reason);
		else
			fprintf(f, " \"%s\"\n", "No reason given");
	}

	fclose(f);
}

static void Command_ClearBans(void)
{
	if (!I_ClearBans)
		return;

	I_ClearBans();
	D_SaveBan();
}

void D_LoadBan(dboolean warning)
{
	FILE *f;
	size_t i, j;
	char *address, *mask;
	char *username, *reason;
	time_t unbanTime = NO_BAN_TIME;
	char buffer[MAX_WADPATH];
	uint8_t banmode = 0;
	dboolean malformed = false;

	if (!I_ClearBans)
		return;

	// We at least attempted loading bans.txt
	bansLoaded = true;

	f = fopen(va("%s"PATHSEP"%s", srb2home, "ban.txt"), "r");

	if (!f)
	{
		if (warning)
			CONS_Alert(CONS_WARNING, M_GetText("Could not open ban.txt for ban list\n"));
		return;
	}

	I_ClearBans();

	for (i = 0; fgets(buffer, (int)sizeof(buffer), f); i++)
	{
		address = strtok(buffer, " /\t\r\n");
		mask = strtok(NULL, " \t\r\n");

		if (i == 0 && !strncmp(address, "BANFORMAT", 9))
		{
			if (mask)
			{
				banmode = atoi(mask);
			}
			switch (banmode)
			{
				case BANFORMAT: // currently supported format
				//case 0: -- permitted only when BANFORMAT string not present
					break;
				default:
				{
					fclose(f);
					CONS_Alert(CONS_WARNING, "Could not load unknown ban.txt for ban list (BANFORMAT %s, expected %d)\n", mask, BANFORMAT);
					return;
				}
			}
			continue;
		}

		if (I_SetBanAddress(address, mask) == false) // invalid IP input?
		{
			CONS_Alert(CONS_WARNING, "\"%s/%s\" is not a valid IP address, discarding...\n", address, mask);
			continue;
		}

		// One-way legacy format conversion -- the game will crash otherwise
		if (banmode == 0)
		{
			unbanTime = NO_BAN_TIME;
			username = NULL; // not guaranteed to be accurate, but only sane substitute
			reason = strtok(NULL, "\r\n");
			if (reason && reason[0] == 'N' && reason[1] == 'A' && reason[2] == '\0')
			{
				reason = NULL;
			}
		}
		else
		{
			reason = strtok(NULL, " \"\t\r\n");
			if (reason)
			{
				unbanTime = atoi(reason);
				reason = NULL;
			}
			else
			{
				unbanTime = NO_BAN_TIME;
				malformed = true;
			}

			username = strtok(NULL, "\"\t\r\n"); // go until next "
			if (!username)
			{
				malformed = true;
			}

			strtok(NULL, "\"\t\r\n"); // remove first "
			reason = strtok(NULL, "\"\r\n"); // go until next "
			if (!reason)
			{
				malformed = true;
			}
		}

		// Enforce MAX_REASONLENGTH.
		if (reason)
		{
			j = 0;
			while (reason[j] != '\0')
			{
				if ((j++) < MAX_REASONLENGTH)
					continue;
				reason[j] = '\0';
				break;
			}
		}

		if (I_SetUnbanTime)
			I_SetUnbanTime(unbanTime);

		if (I_SetBanUsername)
			I_SetBanUsername(username);

		if (I_SetBanReason)
			I_SetBanReason(reason);
	}

	if (malformed)
	{
		CONS_Alert(CONS_WARNING, "One or more lines of ban.txt are malformed. The game can correct for this, but some data may be lost.\n");
	}

	fclose(f);
}

#undef BANFORMAT

static void Command_ReloadBan(void)  //recheck ban.txt
{
	D_LoadBan(true);
}

static void Command_connect(void)
{
	if (COM_Argc() < 2 || *COM_Argv(1) == 0)
	{
		CONS_Printf(M_GetText(
			"Connect <serveraddress> (port): connect to a server\n"
			"Connect ANY: connect to the first lan server found\n"
			//"Connect SELF: connect to your own server.\n"
			));
		return;
	}

	if (Playing() || demo.title)
	{
		if (demo.playback && demo.title)
			G_CheckDemoStatus();

		if (netgame)
		{
			D_QuitNetGame();
			CL_Reset();
		}

		M_ReturnToConnectMenu();
	}
	else
	{
		M_ClearMenusEx(true, true);
	}

	// modified game check: no longer handled
	// we don't request a restart unless the filelist differs

	server = false;
	connectedtodedicated = false;

	{
		// used in menu to connect to a server in the list
		if (netgame && fasticmp(COM_Argv(1), "node"))
		{
			servernode = (int8_t)atoi(COM_Argv(2));
		}
		else if (netgame)
		{
			D_QuitNetGame();
			CL_Reset();
			M_ReturnToConnectMenu();
		}
		else if (I_NetOpenSocket)
		{
			I_NetOpenSocket();
			netgame = true;
			multiplayer = true;

			if (fasticmp(COM_Argv(1), "any"))
				servernode = BROADCASTADDR;
			else if (I_NetMakeNodewPort)
			{
				if (COM_Argc() >= 3) // address AND port
					servernode = I_NetMakeNodewPort(COM_Argv(1), COM_Argv(2));
				else // address only, or address:port
					servernode = I_NetMakeNode(COM_Argv(1));
			}
			else
			{
				CONS_Alert(CONS_ERROR, M_GetText("There is no server identification with this network driver\n"));
				D_CloseConnection();
				return;
			}
		}
		else
			CONS_Alert(CONS_ERROR, M_GetText("There is no network driver\n"));

		// invalid address
		if (servernode == -1)
		{
			D_QuitNetGame(); // this will also call D_CloseConnection for us
			CL_Reset();
			servernode = 0; // not sure if this is cool, but if this is -1 then bad things happen in alot of networking code
			M_ReturnToConnectMenu();
			M_StartMessage2("Connection failed", M_GetText("Failed to connect to server\nMake sure you put in a valid Address!\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
			return;
		}
	}

	if (I_GetNodeAddress)
		CV_Set(&cv_lastserver, I_GetNodeAddress(servernode));

	if (splitscreen != cv_splitplayers.value-1)
	{
		splitscreen = cv_splitplayers.value-1;
		SplitScreen_OnChange();
	}

	CL_ConnectToServer();
}

static void ResetNode(int32_t node);

//
// CL_ClearPlayer
//
// Clears the player data so that a future client can use this slot
//
void CL_ClearPlayer(int32_t playernum)
{
	int i;

	// Handle mobj_t pointers.
	if (G_GamestateUsesLevel() == true)
	{
		if (players[playernum].follower)
		{
			//K_RemoveFollower(&players[playernum]);
		}

#define PlayerPointerRemove(field) \
		if (P_MobjWasRemoved(field) == false) \
		{ \
			P_RemoveMobj(field); \
			P_SetTarget(&field, NULL); \
		}

		// These are mostly subservient to the player, and may not clean themselves up.
		PlayerPointerRemove(players[playernum].mo);
		PlayerPointerRemove(players[playernum].followmobj);

#undef PlayerPointerRemove

	}

	// Handle parties.
	for (i = 0; i < MAXPLAYERS; ++i)
	{
		if (splitscreen_invitations[i] == playernum)
			splitscreen_invitations[i] = -1;
	}
	splitscreen_invitations[playernum] = -1;

	playerconsole[playernum] = playernum;

	// Wipe the struct.
	memset(&players[playernum], 0, sizeof (player_t));

	// Handle values which should not be initialised to 0.
	players[playernum].followerskin = -1; // don't have a ghost follower

	// Handle post-cleanup.
	RemoveAdminPlayer(playernum); // don't stay admin after you're gone
}

//
// CL_RemovePlayer
//
// Removes a player from the current game
//
void CL_RemovePlayer(int32_t playernum, kickreason_t reason)
{
	// Sanity check: exceptional cases (i.e. c-fails) can cause multiple
	// kick commands to be issued for the same player.
	if (!playeringame[playernum])
		return;

	if (server && !demo.playback && playernode[playernum] != UINT8_MAX && !players[playernum].bot)
	{
		int32_t node = playernode[playernum];
		//playerpernode[node] = 0; // It'd be better to remove them all at once, but ghosting happened, so continue to let CL_RemovePlayer do it one-by-one
		playerpernode[node]--;
		if (playerpernode[node] <= 0)
		{
			nodeingame[node] = false;
			Net_CloseConnection(node);
			ResetNode(node);
		}
	}

	if (K_IsPlayerWanted(&players[playernum]))
		K_CalculateBattleWanted();

	LUA_HookPlayerQuit(&players[playernum], reason); // Lua hook for player quitting

	G_LeaveParty(playernum);

	// Reset player data
	CL_ClearPlayer(playernum);

	// remove avatar of player
	playeringame[playernum] = false;
	demo_extradata[playernum] |= DXD_PLAYSTATE;
	playernode[playernum] = UINT8_MAX;
	while (doomcom->numslots > 1 && !playeringame[doomcom->numslots-1])
		doomcom->numslots--;

	// Reset the name
	snprintf(player_names[playernum], MAXPLAYERNAME+1, "Player %c", 'A' + playernum);

	player_name_changes[playernum] = 0;

	LUA_InvalidatePlayer(&players[playernum]);

	// don't look through someone's view who isn't there
	G_ResetViews(false);

	K_CheckBumpers();
	P_CheckRacers();

	// Reset startedInFreePlay
	{
		int32_t i;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && !players[i].spectator)
				break;
		}

		if (i == MAXPLAYERS)
		{
			// Server was emptied, consider it FREE PLAY.
			startedInFreePlay = true;
		}
	}

	// Reset map headers' justPlayed and anger records
	// when there are no players in a dedicated server.
	// Otherwise maps get angry at newly-joined players
	// that don't deserve it.
	if (dedicated && D_NumPlayers() == 0)
	{
		for (int32_t i = 0; i < nummapheaders; i++)
		{
			mapheaderinfo[i]->justPlayed = 0;
		}
	}
}

void CL_Reset(void)
{
	if (metalrecording)
		G_StopMetalRecording(false);
	if (metalplayback)
		G_StopMetalDemo();
	if (demo.recording)
		G_CheckDemoStatus();

	// reset client/server code
	DEBFILE(va("\n-=-=-=-=-=-=-= Client reset =-=-=-=-=-=-=-\n\n"));

	if (servernode > 0 && servernode < MAXNETNODES)
	{
		nodeingame[(uint8_t)servernode] = false;
		Net_CloseConnection(servernode);
	}
	D_CloseConnection(); // netgame = false
	multiplayer = false;
	servernode = 0;
	server = true;
	connectedtodedicated = false;
	doomcom->numnodes = 1;
	doomcom->numslots = 1;
	SV_StopServer();
	SV_ResetServer();

	// make sure we don't leave any fileneeded gunk over from a failed join
	fileneedednum = 0;
	memset(fileneeded, 0, sizeof(fileneeded));

	totalfilesrequestednum = 0;
	totalfilesrequestedsize = 0;
	firstconnectattempttime = 0;
	serverisfull = false;
	connectiontimeout = (tic_t)cv_nettimeout.value; //reset this temporary hack

	expectChallenge = false;

#ifdef HAVE_CURL
	curl_failedwebdownload = false;
	curl_transfers = 0;
	curl_running = false;
	http_source[0] = '\0';
#endif

	G_ResetAllControllerRumbles();

	// D_StartTitle should get done now, but the calling function will handle it
}

static void Command_GetPlayerNum(void)
{
	int32_t i;

	for (i = 0; i < MAXPLAYERS; i++)
		if (playeringame[i])
		{
			if (serverplayer == i)
				CONS_Printf(M_GetText("num:%2d  node:%2d  %s\n"), i, playernode[i], player_names[i]);
			else
				CONS_Printf(M_GetText("\x82num:%2d  node:%2d  %s\n"), i, playernode[i], player_names[i]);
		}
}

int8_t nametonum(const char *name)
{
	int32_t playernum, i;

	if (fastcmp(name, "0"))
		return 0;

	playernum = (int8_t)atoi(name);

	if (playernum < 0 || playernum >= MAXPLAYERS)
		return -1;

	if (playernum)
	{
		if (playeringame[playernum])
			return (int8_t)playernum;
		else
			return -1;
	}

	for (i = 0; i < MAXPLAYERS; i++)
		if (playeringame[i] && fasticmp(player_names[i], name))
			return (int8_t)i;

	CONS_Printf(M_GetText("There is no player named \"%s\"\n"), name);

	return -1;
}

/** Lists all players and their player numbers.
  *
  * \sa Command_GetPlayerNum
  */
static void Command_Nodes(void)
{
	int32_t i;
	size_t maxlen = 0;
	const char *address;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		const size_t plen = strlen(player_names[i]);
		if (playeringame[i] && plen > maxlen)
			maxlen = plen;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			CONS_Printf("%.2u: %*s", i, (int)maxlen, player_names[i]);

			if (playernode[i] != UINT8_MAX)
			{
				CONS_Printf(" - node %.2d", playernode[i]);
				if ((I_GetNodeAddress && (address = I_GetNodeAddress(playernode[i])) != NULL) && (cv_shownodeip.value))
					CONS_Printf(" - %s", address);
			}

			CONS_Printf(" [BKID-%s] ", GetPrettyRRID(players[i].public_key, true));

			if (IsPlayerAdmin(i))
				CONS_Printf(M_GetText(" (verified admin)"));

			if (players[i].spectator)
				CONS_Printf(M_GetText(" (spectator)"));

			CONS_Printf("\n");
		}
	}
}

static void Command_Listplayers(void)
{
	const char *address;
	int width = 0;

	dboolean admin;
	dboolean spectator;

	/*
	Mode of player status for an individual player (admin, spectator).
	1 for admin
	2 for spectator
	4 for both
	*/
	int mode = 0;

	int32_t totalplayers = 0;

	const char *cc;
	char pcc[2];

	int32_t i;
	int n;

	for (i = 0; i < MAXPLAYERS; ++i)
		if (playeringame[i])
	{
		n = strlen(player_names[i]);
		if (n > width)
			width = n;

		if (mode != 7)
		{
			admin     = IsPlayerAdmin(i);
			spectator = players[i].spectator;

			if (admin)
				mode |= 1;
			if (spectator)
				mode |= 2;
			if (admin && spectator)
				mode |= 4;
		}
	}

	for (i = 0; i < MAXPLAYERS; ++i)
		if (playeringame[i])
	{
		admin     = IsPlayerAdmin(i);
		spectator = players[i].spectator;

		if (admin)
			cc = "\x85";/* red */
		else if (spectator)
			cc = "\x86";/* gray */
		else
			cc = "";

		uint16_t chatcolor = skincolors[players[i].skincolor].chatcolor;

		if (chatcolor > V_TANMAP)
		{
			snprintf(pcc, sizeof(pcc), "%c", '\x80');
		}
		else
		{
			snprintf(pcc, sizeof(pcc), "%c", '\x80' + (chatcolor >> V_CHARCOLORSHIFT));
		}

		CONS_Printf("%.2d: ""%s""%-*s""\x80", i, pcc,width, player_names[i]);

		if ((I_GetNodeAddress && (address = I_GetNodeAddress(playernode[i])) != NULL) && (cv_shownodeip.value))
			CONS_Printf(" -- %s", address);
		else/* print spacer */
		{
			/* ...but not if there's a crammed status and were admin */
			if (mode != 7 || !admin)
				CONS_Printf(" --     ");/* -- self */
		}

		if (admin)
			CONS_Printf(M_GetText("%s"" (admin)"),cc);
		if (spectator)
			CONS_Printf(M_GetText("%s"" (spectator)"),cc);

		CONS_Printf("\n");

		totalplayers++;
	}

	if (totalplayers == 1)
		CONS_Printf("\nThere is 1 player in the game.\n");
	else
		CONS_Printf("\nThere are %d players in the game.\n", totalplayers);
}

static void Command_Ban(void)
{
	if (COM_Argc() < 2)
	{
		CONS_Printf(M_GetText("ban <playername/playernum> <reason>: ban and kick a player\n"));
		return;
	}

	if (!netgame) // Don't kick Tails in splitscreen!
	{
		CONS_Printf(M_GetText("This only works in a netgame.\n"));
		return;
	}

	if (server || IsPlayerAdmin(consoleplayer))
	{
		uint8_t buf[3 + MAX_REASONLENGTH];
		uint8_t *p = buf;
		const int8_t pn = nametonum(COM_Argv(1));

		if (pn == -1 || pn == 0)
			return;

		// player does not exist
		if (!playeringame[pn])
			return;

		WRITEUINT8(p, pn);

		if (COM_Argc() == 2)
		{
			WRITEUINT8(p, KICK_MSG_BANNED);
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		else
		{
			size_t i, j = COM_Argc();
			char message[MAX_REASONLENGTH];

			//Steal from the motd code so you don't have to put the reason in quotes.
			strlcpy(message, COM_Argv(2), sizeof message);
			for (i = 3; i < j; i++)
			{
				strlcat(message, " ", sizeof message);
				strlcat(message, COM_Argv(i), sizeof message);
			}

			WRITEUINT8(p, KICK_MSG_CUSTOM_BAN);
			WRITESTRINGN(p, message, MAX_REASONLENGTH);
			SendNetXCmd(XD_KICK, &buf, p - buf);
		}
	}
	else
		CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
}

static void Command_BanIP(void)
{
	size_t ac = COM_Argc();

	if (ac < 2)
	{
		CONS_Printf(M_GetText("banip <ip> [<reason>]: ban an ip address\n"));
		return;
	}

	if (server) // Only the server can use this, otherwise does nothing.
	{
		char *addressInput = Z_StrDup(COM_Argv(1));

		const char *address = NULL;
		const char *mask = NULL;

		const char *reason = NULL;

		address = strtok(addressInput, "/");
		mask = strtok(NULL, "");

		if (ac > 2)
		{
			reason = COM_Argv(2);
		}

		if (I_SetBanAddress && I_SetBanAddress(address, mask))
		{
			if (reason)
			{
				CONS_Printf(
					"Banned IP address %s%s for: %s\n",
					address,
					(mask && (strlen(mask) > 0)) ? va("/%s", mask) : "",
					reason
				);
			}
			else
			{
				CONS_Printf(
					"Banned IP address %s%s\n",
					address,
					(mask && (strlen(mask) > 0)) ? va("/%s", mask) : ""
				);
			}

			if (I_SetUnbanTime)
				I_SetUnbanTime(NO_BAN_TIME);

			if (I_SetBanUsername)
				I_SetBanUsername(NULL);

			if (I_SetBanReason)
				I_SetBanReason(reason);

			D_SaveBan();
		}
		else
		{
			return;
		}
	}
}

static void Command_Kick(void)
{
	if (COM_Argc() < 2)
	{
		CONS_Printf(M_GetText("kick <playername/playernum> <reason>: kick a player\n"));
		return;
	}

	if (!netgame) // Don't kick Tails in splitscreen!
	{
		CONS_Printf(M_GetText("This only works in a netgame.\n"));
		return;
	}

	if (server || IsPlayerAdmin(consoleplayer))
	{
		uint8_t buf[3 + MAX_REASONLENGTH];
		uint8_t *p = buf;
		const int8_t pn = nametonum(COM_Argv(1));

		if (pn == -1 || pn == 0)
			return;

		// player does not exist
		if (!playeringame[pn])
			return;

		// Special case if we are trying to kick a player who is downloading the game state:
		// trigger a timeout instead of kicking them, because a kick would only
		// take effect after they have finished downloading
		if (server && playernode[pn] != UINT8_MAX && sendingsavegame[playernode[pn]])
		{
			Net_ConnectionTimeout(playernode[pn]);
			return;
		}

		WRITEINT8(p, pn);

		if (COM_Argc() == 2)
		{
			WRITEUINT8(p, KICK_MSG_GO_AWAY);
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		else
		{
			size_t i, j = COM_Argc();
			char message[MAX_REASONLENGTH];

			//Steal from the motd code so you don't have to put the reason in quotes.
			strlcpy(message, COM_Argv(2), sizeof message);
			for (i = 3; i < j; i++)
			{
				strlcat(message, " ", sizeof message);
				strlcat(message, COM_Argv(i), sizeof message);
			}

			WRITEUINT8(p, KICK_MSG_CUSTOM_KICK);
			WRITESTRINGN(p, message, MAX_REASONLENGTH);
			SendNetXCmd(XD_KICK, &buf, p - buf);
		}
	}
	else
		CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
}

static void Got_KickCmd(uint8_t **p, int32_t playernum)
{
	int32_t pnum, msg;
	char buf[3 + MAX_REASONLENGTH];
	char *reason = buf;
	kickreason_t kickreason = KR_KICK;
	uint32_t banMinutes = 0;

	pnum = READUINT8(*p);
	msg = READUINT8(*p);

	if (pnum >= MAXPLAYERS)
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal kick command received from %s for player %d\n"), player_names[playernum], pnum);
		return;
	}

	// FIXME: this does not work at all on dedi, servernode == 0 which is prevented from kick/ban command
	if (pnum == serverplayer && IsPlayerAdmin(playernum))
	{
		CONS_Printf(M_GetText("Server is being shut down remotely. Goodbye!\n"));

		if (server)
			COM_BufAddText("quit\n");

		return;
	}

	// Is playernum authorized to make this kick?
	if (playernum != serverplayer && !IsPlayerAdmin(playernum)
		/*&& !(playernode[playernum] != UINT8_MAX && playerpernode[playernode[playernum]] == 2
		&& nodetoplayer2[playernode[playernum]] == pnum)*/)
	{
		// We received a kick command from someone who isn't the
		// server or admin, and who isn't in splitscreen removing
		// player 2. Thus, it must be someone with a modified
		// binary, trying to kick someone but without having
		// authorization.

		// We deal with this by changing the kick reason to
		// "consistency failure" and kicking the offending user
		// instead.

		CONS_Alert(CONS_WARNING, M_GetText("Illegal kick command received from %s for player %d\n"), player_names[playernum], pnum);

		// In debug, print a longer message with more details.
		// TODO Callum: Should we translate this?
/*
		CONS_Debug(DBG_NETPLAY,
			"So, you must be asking, why is this an illegal kick?\n"
			"Well, let's take a look at the facts, shall we?\n"
			"\n"
			"playernum (this is the guy who did it), he's %d.\n"
			"pnum (the guy he's trying to kick) is %d.\n"
			"playernum's node is %d.\n"
			"That node has %d players.\n"
			"Player 2 on that node is %d.\n"
			"pnum's node is %d.\n"
			"That node has %d players.\n"
			"Player 2 on that node is %d.\n"
			"\n"
			"If you think this is a bug, please report it, including all of the details above.\n",
				playernum, pnum,
				playernode[playernum], playerpernode[playernode[playernum]],
				nodetoplayer2[playernode[playernum]],
				playernode[pnum], playerpernode[playernode[pnum]],
				nodetoplayer2[playernode[pnum]]);
*/
		pnum = playernum;
		msg = KICK_MSG_CON_FAIL;
	}

	// without this the server will remove itself and thus self destruct on dedicated
	if (server && (playernode[pnum] == UINT8_MAX || !playeringame[pnum]))
	{
		CONS_Alert(CONS_WARNING, M_GetText("Attempting to kick player %d which is not in game received from %s\n"), pnum, player_names[playernum]);
		return;
	}

	// make sure we dont read garbage anywhere
	memset(reason, 0, sizeof(buf));

	if (msg == KICK_MSG_CUSTOM_BAN || msg == KICK_MSG_CUSTOM_KICK)
	{
		READSTRINGN(*p, reason, MAX_REASONLENGTH+1);
	}

	if (playernode[pnum] == servernode)
	{
		CONS_Printf(M_GetText("Ignoring kick attempt from %s on node %d (it's the server)\n"), player_names[playernum], servernode);
		return;
	}

	//CONS_Printf("\x82%s ", player_names[pnum]);

	// Save bans here. Used to be split between here and the actual command, depending on
	// whenever the server did it or a remote admin did it, but it's simply more convenient
	// to keep it all in one place.
	if (server)
	{
		if (msg == KICK_MSG_GO_AWAY || msg == KICK_MSG_CUSTOM_KICK)
		{
			// Kick as a temporary ban.
			banMinutes = cv_kicktime.value;
		}

		if (msg == KICK_MSG_BANNED || msg == KICK_MSG_CUSTOM_BAN || banMinutes)
		{
			if (I_Ban && !I_Ban(playernode[(int32_t)pnum]))
			{
				CONS_Alert(CONS_WARNING, M_GetText("Ban failed. Invalid node?\n"));
			}
			else
			{
				if (I_SetBanUsername)
					I_SetBanUsername(player_names[pnum]);

				if (I_SetBanReason)
					I_SetBanReason(reason);

				if (I_SetUnbanTime)
				{
					if (banMinutes)
						I_SetUnbanTime(time(NULL) + (banMinutes * 60));
					else
						I_SetUnbanTime(NO_BAN_TIME);
				}

				D_SaveBan();
			}
		}
	}

	if (msg == KICK_MSG_PLAYER_QUIT)
		S_StartSound(NULL, sfx_leave); // intended leave
	else
		S_StartSound(NULL, sfx_syfail); // he he he

	switch (msg)
	{
		case KICK_MSG_GO_AWAY:
			HU_AddChatText(va("\x82*%s has been kicked (No reason given)", player_names[pnum]), false);
			kickreason = KR_KICK;
			break;
		case KICK_MSG_PING_HIGH:
			HU_AddChatText(va("\x82*%s left the game (Broke delay limit)", player_names[pnum]), false);
			kickreason = KR_PINGLIMIT;
			break;
		case KICK_MSG_CON_FAIL:
			HU_AddChatText(va("\x82*%s left the game (Synch failure)", player_names[pnum]), false);
			kickreason = KR_SYNCH;

			if ((M_CheckParm("-consisdump")) || (cv_blamecfail.value)) // Helps debugging some problems
			{
				int32_t i;

				CONS_Printf(M_GetText("Player kicked is #%d, dumping consistency...\n"), pnum);

				for (i = 0; i < MAXPLAYERS; i++)
				{
					if (!playeringame[i])
						continue;
					CONS_Printf("-------------------------------------\n");
					CONS_Printf("Player %d: %s\n", i, player_names[i]);
					CONS_Printf("Skin: %d\n", players[i].skin);
					CONS_Printf("Color: %d\n", players[i].skincolor);
					CONS_Printf("Speed: %d\n",players[i].speed>>FRACBITS);
					if (players[i].mo)
					{
						if (!players[i].mo->skin)
							CONS_Printf("Mobj skin: NULL!\n");
						else
							CONS_Printf("Mobj skin: %s\n", ((skin_t *)players[i].mo->skin)->name);
						CONS_Printf("Position: %d, %d, %d\n", players[i].mo->x, players[i].mo->y, players[i].mo->z);
						if (!players[i].mo->state)
							CONS_Printf("State: S_NULL\n");
						else
							CONS_Printf("State: %d\n", (statenum_t)(players[i].mo->state-states));
					}
					else
						CONS_Printf("Mobj: NULL\n");
					CONS_Printf("-------------------------------------\n");
				}
			}
			break;
		case KICK_MSG_TIMEOUT:
			HU_AddChatText(va("\x82*%s left the game (Connection timeout)", player_names[pnum]), false);
			kickreason = KR_TIMEOUT;
			break;
		case KICK_MSG_SIGFAIL:
			HU_AddChatText(va("\x82*%s left the game (Invalid signature)", player_names[pnum]), false);
			kickreason = KR_TIMEOUT;
			break;

		case KICK_MSG_PLAYER_QUIT:
			if (netgame) // not splitscreen/bots
				HU_AddChatText(va("\x82*%s left the game", player_names[pnum]), false);
			kickreason = KR_LEAVE;
			break;
		case KICK_MSG_GRIEF:
			HU_AddChatText(va("\x82*%s has been kicked (Automatic grief detection)", player_names[pnum]), false);
			kickreason = KR_KICK;
			break;
		case KICK_MSG_BANNED:
			HU_AddChatText(va("\x82*%s has been banned (No reason given)", player_names[pnum]), false);
			kickreason = KR_BAN;
			break;
		case KICK_MSG_CUSTOM_KICK:
			HU_AddChatText(va("\x82*%s has been kicked (%s)", player_names[pnum], reason), false);
			kickreason = KR_KICK;
			break;
		case KICK_MSG_CUSTOM_BAN:
			HU_AddChatText(va("\x82*%s has been banned (%s)", player_names[pnum], reason), false);
			kickreason = KR_BAN;
			break;
	}

	// SRB2Kart: kicks count as forfeit
	switch (kickreason)
	{
		case KR_KICK:
		case KR_BAN:
		case KR_LEAVE:
			// Intentional removals should be hit with a true forfeit.
			K_PlayerForfeit(pnum, true);
			break;
		default:
			// Otherwise, give remaining players the point compensation, but doesn't penalize who left.
			K_PlayerForfeit(pnum, false);
			break;
	}

	if (playernode[pnum] == playernode[consoleplayer])
	{
#ifdef DUMPCONSISTENCY
		if (msg == KICK_MSG_CON_FAIL) SV_SavedGame();
#endif
		LUA_HookBool(false, HOOK(GameQuit)); //Lua hooks handled differently now

		D_QuitNetGame();
		CL_Reset();
		M_ReturnToConnectMenu();

		if (msg == KICK_MSG_CON_FAIL)
			M_StartMessage2("Game Kick", M_GetText("Server closed connection\n(Synch failure)\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
		else if (msg == KICK_MSG_PING_HIGH)
			M_StartMessage2("Game Kick", M_GetText("Server closed connection\n(Broke delay limit)\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
		else if (msg == KICK_MSG_BANNED)
			M_StartMessage2("Game Kick", M_GetText("You have been banned by the server\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
		else if (msg == KICK_MSG_CUSTOM_KICK)
			M_StartMessage2("Game Kick", va(M_GetText("You have been kicked\n(%s)\n"), reason), MB_NONE, MM_NOTHING, "Press any key", NULL);
		else if (msg == KICK_MSG_CUSTOM_BAN)
			M_StartMessage2("Game Kick", va(M_GetText("You have been banned\n(%s)\n"), reason), MB_NONE, MM_NOTHING, "Press any key", NULL);
		else if (msg == KICK_MSG_SIGFAIL)
			M_StartMessage2("Game Kick", M_GetText("Server closed connection\n(Invalid signature)\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
		else
			M_StartMessage2("Game Kick", M_GetText("You have been kicked by the server\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
	}
	else if (server)
	{
		// Sal: Because kicks (and a lot of other commands) are player-based, we can't tell which player pnum is on the node from a glance.
		// When we want to remove everyone from a node, we have to get the kicked player's node, then remove everyone on that node manually so we don't miss any.
		// This avoids the bugs with older SRB2 version's online splitscreen kicks, specifically ghosting.
		// On top of this, it can't just be a CL_RemovePlayer call; it has to be a server-sided.
		// Clients don't bother setting any nodes for anything but THE server player (even ignoring the server's extra players!), so it'll often remove everyone because they all have node -1/255, insta-desync!
		// And yes. This is a netxcmd wrap for just CL_RemovePlayer! :V

#define removethisplayer(otherp) \
	if (otherp >= 0) \
	{ \
		buf[0] = (uint8_t)otherp; \
		if (otherp != pnum) \
		{ \
			HU_AddChatText(va("\x82*%s left the game (Joined with %s)", player_names[otherp], player_names[pnum]), false); \
			buf[1] = KR_LEAVE; \
		} \
		else \
			buf[1] = (uint8_t)kickreason; \
		SendNetXCmd(XD_REMOVEPLAYER, &buf, 2); \
		otherp = -1; \
	}
		removethisplayer(nodetoplayer[playernode[pnum]])
		removethisplayer(nodetoplayer2[playernode[pnum]])
		removethisplayer(nodetoplayer3[playernode[pnum]])
		removethisplayer(nodetoplayer4[playernode[pnum]])
#undef removethisplayer
	}
}

#ifdef HAVE_CURL
/** Add a login for HTTP downloads. If the
  * user/password is missing, remove it.
  *
  * \sa Command_list_http_logins
  */
static void Command_set_http_login (void)
{
	HTTP_login  *login;
	HTTP_login **prev_next;

	if (COM_Argc() < 2)
	{
		CONS_Printf(
				"set_http_login <URL> [user:password]: Set or remove a login to "
				"authenticate HTTP downloads.\n"
		);
		return;
	}

	login = CURLGetLogin(COM_Argv(1), &prev_next);

	if (COM_Argc() == 2)
	{
		if (login)
		{
			(*prev_next) = login->next;
			CONS_Printf("Login for '%s' removed.\n", login->url);
			Z_Free(login);
		}
	}
	else
	{
		if (login)
			Z_Free(login->auth);
		else
		{
			login = ZZ_Alloc(sizeof *login);
			login->url  = Z_StrDup(COM_Argv(1));
		}

		login->auth = Z_StrDup(COM_Argv(2));

		login->next = curl_logins;
		curl_logins = login;
	}
}

/** List logins for HTTP downloads.
  *
  * \sa Command_set_http_login
  */
static void Command_list_http_logins (void)
{
	HTTP_login *login;

	for (
			login = curl_logins;
			login;
			login = login->next
	){
		CONS_Printf(
				"'%s' -> '%s'\n",
				login->url,
				login->auth
		);
	}
}
#endif/*HAVE_CURL*/

static void Command_ResendGamestate(void)
{
	int8_t playernum;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (COM_Argc() == 1)
	{
		CONS_Printf(M_GetText("resendgamestate <playername/playernum>: resend the game state to a player\n"));
		return;
	}
	else if (client)
	{
		CONS_Printf(M_GetText("Only the server can use this.\n"));
		return;
	}

	playernum = nametonum(COM_Argv(1));
	if (playernum == -1 || playernum == 0)
		return;

	// Send a PT_WILLRESENDGAMESTATE packet to the client so they know what's going on
	netbuffer->packettype = PT_WILLRESENDGAMESTATE;
	if (!HSendPacket(playernode[playernum], true, 0, 0))
	{
		CONS_Alert(CONS_ERROR, M_GetText("A problem occured, please try again.\n"));
		return;
	}
}

static CV_PossibleValue_t netticbuffer_cons_t[] = {{0, "MIN"}, {3, "MAX"}, {0, NULL}};
consvar_t cv_netticbuffer = CVAR_INIT ("netticbuffer", "1", CV_SAVE, netticbuffer_cons_t, NULL);

static void Joinable_OnChange(void);

consvar_t cv_allownewplayer = CVAR_INIT ("allowjoin", "On", CV_SAVE|CV_CALL, CV_OnOff, Joinable_OnChange);

#ifdef VANILLAJOINNEXTROUND
consvar_t cv_joinnextround = CVAR_INIT ("joinnextround", "Off", CV_NETVAR, CV_OnOff, NULL); /// \todo not done
#endif

static CV_PossibleValue_t maxplayers_cons_t[] = {{2, "MIN"}, {MAXPLAYERS, "MAX"}, {0, NULL}};
consvar_t cv_maxplayers = CVAR_INIT ("maxplayers", "8", CV_SAVE|CV_CALL, maxplayers_cons_t, Joinable_OnChange);

static CV_PossibleValue_t joindelay_cons_t[] = {{1, "MIN"}, {3600, "MAX"}, {0, "Off"}, {0, NULL}};
consvar_t cv_joindelay = CVAR_INIT ("joindelay", "10", CV_SAVE, joindelay_cons_t, NULL);

// Here for dedicated servers
static CV_PossibleValue_t discordinvites_cons_t[] = {{0, "Admins Only"}, {1, "Everyone"}, {0, NULL}};
consvar_t cv_discordinvites = CVAR_INIT ("discordinvites", "Everyone", CV_SAVE|CV_CALL, discordinvites_cons_t, Joinable_OnChange);

static CV_PossibleValue_t resynchattempts_cons_t[] = {{1, "MIN"}, {20, "MAX"}, {0, "No"}, {0, NULL}};

consvar_t cv_resynchattempts = CVAR_INIT ("resynchattempts", "5", CV_SAVE, resynchattempts_cons_t, NULL);
consvar_t cv_blamecfail = CVAR_INIT ("blamecfail", "Off", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_logconsistency = CVAR_INIT ("logconsistency", "Off", CV_SAVE, CV_OnOff, NULL);

// max file size to send to a player (in kilobytes)
static CV_PossibleValue_t maxsend_cons_t[] = {{-1, "MIN"}, {999999999, "MAX"}, {0, NULL}};
consvar_t cv_maxsend = CVAR_INIT ("maxsend", "204800", CV_SAVE, maxsend_cons_t, NULL);
consvar_t cv_noticedownload = CVAR_INIT ("noticedownload", "Off", CV_SAVE, CV_OnOff, NULL);

// Speed of file downloading (in packets per tic)
static CV_PossibleValue_t downloadspeed_cons_t[] = {{1, "MIN"}, {300, "MAX"}, {0, NULL}};
consvar_t cv_downloadspeed = CVAR_INIT ("downloadspeed", "32", CV_SAVE, downloadspeed_cons_t, NULL);

static CV_PossibleValue_t connectawaittime_cons_t[] = {{1, "MIN"}, {60, "MAX"}, {0, "Inf"}, {0, NULL}};
consvar_t cv_connectawaittime = CVAR_INIT ("connectawaittime", "5", CV_SAVE, connectawaittime_cons_t, NULL);

consvar_t cv_serverinfoscreen = CVAR_INIT ("serverinfoscreen", "On", CV_SAVE, CV_OnOff, NULL);

static void Got_AddPlayer(uint8_t **p, int32_t playernum);
static void Got_RemovePlayer(uint8_t **p, int32_t playernum);
static void Got_AddBot(uint8_t **p, int32_t playernum);

static void Joinable_OnChange(void)
{
	uint8_t buf[3];
	uint8_t *p = buf;
	uint8_t maxplayer;

	if (!server)
		return;

	maxplayer = (uint8_t)(min((dedicated ? MAXPLAYERS-1 : MAXPLAYERS), cv_maxplayers.value));

	WRITEUINT8(p, maxplayer);
	WRITEUINT8(p, cv_allownewplayer.value);
	WRITEUINT8(p, cv_discordinvites.value);

	SendNetXCmd(XD_DISCORD, &buf, 3);
}

// called one time at init
void D_ClientServerInit(void)
{
	DEBFILE(va("- - -== SRB2Kart v%d.%d "VERSIONSTRING" debugfile ==- - -\n",
		VERSION, SUBVERSION));

	COM_AddCommand("getplayernum", Command_GetPlayerNum);
	COM_AddCommand("kick", Command_Kick);
	COM_AddCommand("ban", Command_Ban);
	COM_AddCommand("banip", Command_BanIP);
	COM_AddCommand("clearbans", Command_ClearBans);
	COM_AddCommand("showbanlist", Command_ShowBan);
	COM_AddCommand("reloadbans", Command_ReloadBan);
	COM_AddCommand("connect", Command_connect);
	COM_AddCommand("nodes", Command_Nodes);
	COM_AddCommand("listplayers", Command_Listplayers);
#ifdef HAVE_CURL
	COM_AddCommand("set_http_login", Command_set_http_login);
	COM_AddCommand("list_http_logins", Command_list_http_logins);
#endif
	COM_AddCommand("resendgamestate", Command_ResendGamestate);
#ifdef PACKETDROP
	COM_AddCommand("drop", Command_Drop);
	COM_AddCommand("droprate", Command_Droprate);
#endif
	COM_AddCommand("numnodes", Command_Numnodes);

	RegisterNetXCmd(XD_KICK, Got_KickCmd);
	RegisterNetXCmd(XD_ADDPLAYER, Got_AddPlayer);
	RegisterNetXCmd(XD_REMOVEPLAYER, Got_RemovePlayer);
	RegisterNetXCmd(XD_ADDBOT, Got_AddBot);
#ifdef DUMPCONSISTENCY
	CV_RegisterVar(&cv_dumpconsistency);
#endif
	D_LoadBan(false);

	gametic = 0;
	localgametic = 0;

	// do not send anything before the real begin
	SV_StopServer();
	SV_ResetServer();
	if (dedicated)
		SV_SpawnServer();
}

static void ResetNode(int32_t node)
{
	nodeingame[node] = false;
	nodewaiting[node] = 0;
	nodeneedsauth[node] = false;

	nettics[node] = gametic;
	supposedtics[node] = gametic;

	nodetoplayer[node] = -1;
	nodetoplayer2[node] = -1;
	nodetoplayer3[node] = -1;
	nodetoplayer4[node] = -1;
	playerpernode[node] = 0;

	sendingsavegame[node] = false;
	resendingsavegame[node] = false;
	savegameresendcooldown[node] = 0;

	bannednode[node].banid = SIZE_MAX;
	bannednode[node].timeleft = NO_BAN_TIME;
}

void SV_ResetServer(void)
{
	int32_t i;

	// +1 because this command will be executed in com_executebuffer in
	// tryruntic so gametic will be incremented, anyway maketic > gametic
	// is not an issue

	maketic = gametic + 1;
	neededtic = maketic;
	tictoclear = maketic;

	joindelay = 0;

	for (i = 0; i < MAXNETNODES; i++)
		ResetNode(i);

	for (i = 0; i < MAXPLAYERS; i++)
	{
		LUA_InvalidatePlayer(&players[i]);
		snprintf(player_names[i], MAXPLAYERNAME+1, "Player %c", 'A' + i);
	}

	memset(playeringame, false, sizeof playeringame);
	memset(playernode, UINT8_MAX, sizeof playernode);

	pingmeasurecount = 1;
	memset(realpingtable, 0, sizeof realpingtable);
	memset(playerpingtable, 0, sizeof playerpingtable);
	memset(playerpacketlosstable, 0, sizeof playerpacketlosstable);
	memset(playerdelaytable, 0, sizeof playerdelaytable);

	ClearAdminPlayers();
	Schedule_Clear();
	Automate_Clear();
	K_ClearClientPowerLevels();
	G_ObliterateParties();

	memset(splitscreen_invitations, -1, sizeof splitscreen_invitations);
	memset(player_name_changes, 0, sizeof player_name_changes);

	mynode = 0;
	cl_packetmissed = false;
	cl_redownloadinggamestate = false;

	if (dedicated)
	{
		nodeingame[0] = true;
		serverplayer = 0;
	}
	else
		serverplayer = consoleplayer;

	if (server)
		servernode = 0;

	doomcom->numslots = 0;

	// clear server_context
	memset(server_context, '-', 8);

	memset(connectedservermotd, 0, MAXSERVERNAME);
	memset(connectedservername, 0, MAXSERVERNAME);
	memset(connectedservercontact, 0, MAXSERVERCONTACT);
	memset(connectedserverdescription, 0, MAXSERVERDESCRIPTION);

	CV_RevertNetVars();

	expectChallenge = false;

	DEBFILE("\n-=-=-=-=-=-=-= Server Reset =-=-=-=-=-=-=-\n\n");
}

static void SV_GenContext(void)
{
	uint8_t i;

	// generate server_context, as exactly 8 bytes of randomly mixed A-Z and a-z
	// (hopefully M_Random is initialized!! if not this will be awfully silly!)
	for (i = 0; i < 8; i++)
	{
		const char a = M_RandomKey(26*2);
		if (a < 26) // uppercase
			server_context[i] = 'A'+a;
		else // lowercase
			server_context[i] = 'a'+(a-26);
	}

	CopyCaretColors(connectedservername, cv_servername.string, MAXSERVERNAME);
	CopyCaretColors(connectedservercontact, cv_server_contact.string, MAXSERVERCONTACT);
	strncpy(connectedserverdescription, DEFAULTDESCSTRING, MAXSERVERDESCRIPTION);
}

//
// D_QuitNetGame
// Called before quitting to leave a net game
// without hanging the other players
//
void D_QuitNetGame(void)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	if (!netgame || !doomcom)
		return;

	DEBFILE("===========================================================================\n"
	        "                  Quitting Game, closing connection\n"
	        "===========================================================================\n");

	// abort send/receive of files
	CloseNetFile();
	RemoveAllLuaFileTransfers();
	waitingforluafiletransfer = false;
	waitingforluafilecommand = false;

	if (server)
	{
		int32_t i;

		netbuffer->packettype = PT_SERVERSHUTDOWN;
		for (i = 0; i < MAXNETNODES; i++)
			if (nodeingame[i])
				HSendPacket(i, true, 0, 0);
#ifdef MASTERSERVER
		if (serverrunning && netgame && cv_advertise.value) // see mserv.c Online()
			UnregisterServer();
#endif
	}
	else if (servernode > 0 && servernode < MAXNETNODES && nodeingame[(uint8_t)servernode])
	{
		netbuffer->packettype = PT_CLIENTQUIT;
		HSendPacket(servernode, true, 0, 0);
	}

	D_CloseConnection();
	ClearAdminPlayers();
	Schedule_Clear();
	Automate_Clear();
	K_ClearClientPowerLevels();
	G_ObliterateParties();

	DEBFILE("===========================================================================\n"
	        "                         Log finish\n"
	        "===========================================================================\n");
#ifdef DEBUGFILE
	if (debugfile)
	{
		fclose(debugfile);
		debugfile = NULL;
	}
#endif
}

// Adds a node to the game (player will follow at map change or at savegame....)
static inline void SV_AddNode(int32_t node)
{
	nettics[node] = gametic;
	supposedtics[node] = gametic;
	// little hack because the server connects to itself and puts
	// nodeingame when connected not here
	if (node)
		nodeingame[node] = true;

	nodeneedsauth[node] = false;
}

// Xcmd XD_ADDPLAYER
static void Got_AddPlayer(uint8_t **p, int32_t playernum)
{
	int16_t node, newplayernum;
	uint8_t console;
	uint8_t splitscreenplayer = 0;
	uint8_t i;
	player_t *newplayer;
	uint8_t public_key[PUBKEYLENGTH];

	if (playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		// protect against hacked/buggy client
		CONS_Alert(CONS_WARNING, M_GetText("Illegal add player command received from %s\n"), player_names[playernum]);
		if (server)
			SendKick(playernum, KICK_MSG_CON_FAIL);
		return;
	}

	node = READUINT8(*p);
	newplayernum = READUINT8(*p);

	CONS_Debug(DBG_NETPLAY, "addplayer: %d %d\n", node, newplayernum);

	if (newplayernum+1 > doomcom->numslots)
		doomcom->numslots = (int16_t)(newplayernum+1);

	newplayer = &players[newplayernum];

	newplayer->jointime = 0;

	READSTRINGN(*p, player_names[newplayernum], MAXPLAYERNAME);
	READMEM(*p, public_key, PUBKEYLENGTH);

	console = READUINT8(*p);
	splitscreenplayer = READUINT8(*p);

	G_AddPlayer(newplayernum, console);
	memcpy(players[newplayernum].public_key, public_key, PUBKEYLENGTH);

	for (i = 0; i < MAXAVAILABILITY; i++)
	{
		newplayer->availabilities[i] = READUINT8(*p);
	}

	// the server is creating my player
	if (node == mynode)
	{
		playernode[newplayernum] = 0; // for information only

		if (splitscreenplayer)
		{
			displayplayers[splitscreenplayer] = newplayernum;
			g_localplayers[splitscreenplayer] = newplayernum;
			DEBFILE(va("spawning sister # %d\n", splitscreenplayer));
		}
		else
		{
			consoleplayer = newplayernum;
			for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
			{
				displayplayers[i] = newplayernum;
				g_localplayers[i] = newplayernum;
			}
			DEBFILE("spawning me\n");
		}

		P_ForceLocalAngle(newplayer, newplayer->angleturn, false);

		D_SendPlayerConfig(splitscreenplayer);
		addedtogame = true;
	}

	players[newplayernum].splitscreenindex = splitscreenplayer;
	players[newplayernum].bot = false;

	// Previously called at the top of this function, commented as
	// "caused desyncs in this spot :(". But we can't do this in
	// G_PlayerReborn, since that only runs for level contexts and
	// allows people to party-crash the vote screen even when
	// maxplayers is too low for them. Let's try it here...?
	G_SpectatePlayerOnJoin(newplayernum);

	if (netgame)
	{
		char joinmsg[256];

		strcpy(joinmsg, M_GetText("\x82*%s has joined the game (player %d)"));
		strcpy(joinmsg, va(joinmsg, player_names[newplayernum], newplayernum));

		if (node != mynode)
			S_StartSound(NULL, sfx_join);

		// Merge join notification + IP to avoid clogging console/chat
		if (server && cv_showjoinaddress.value && I_GetNodeAddress)
		{
			const char *address = I_GetNodeAddress(node);
			if (address)
				strcat(joinmsg, va(" (%s)", address));
		}

		HU_AddChatText(joinmsg, false);
	}

	if (server && multiplayer && connectedservermotd[0] != '\0')
		COM_BufAddText(va("sayto %d %s\n", newplayernum, connectedservermotd));

	LUA_HookInt(newplayernum, HOOK(PlayerJoin));

#ifdef HAVE_DISCORDRPC
	DRPC_UpdatePresence();
#endif
}

// Xcmd XD_REMOVEPLAYER
static void Got_RemovePlayer(uint8_t **p, int32_t playernum)
{
	int8_t pnum, reason;

	if (playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		// protect against hacked/buggy client
		CONS_Alert(CONS_WARNING, M_GetText("Illegal remove player command received from %s\n"), player_names[playernum]);
		if (server)
		{
			SendKick(playernum, KICK_MSG_CON_FAIL);
		}
		return;
	}

	pnum = READUINT8(*p);
	reason = READUINT8(*p);

	CL_RemovePlayer(pnum, reason);

#ifdef HAVE_DISCORDRPC
	DRPC_UpdatePresence();
#endif
}

// Xcmd XD_ADDBOT
// Compacted version of XD_ADDPLAYER for simplicity
static void Got_AddBot(uint8_t **p, int32_t playernum)
{
	int16_t newplayernum;
	uint16_t skinnum = 0;
	uint8_t difficulty = DIFFICULTBOT;
	botStyle_e style = BOT_STYLE_NORMAL;

	if (playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		// protect against hacked/buggy client
		CONS_Alert(CONS_WARNING, M_GetText("Illegal add player command received from %s\n"), player_names[playernum]);
		if (server)
		{
			SendKick(playernum, KICK_MSG_CON_FAIL);
		}
		return;
	}

	newplayernum = READUINT8(*p);
	skinnum = READUINT16(*p);
	difficulty = READUINT8(*p);
	style = READUINT8(*p);

	K_SetBot(newplayernum, skinnum, difficulty, style);
}

static dboolean SV_AddWaitingPlayers(int8_t node, uint8_t *availabilities,
const char *name, uint8_t *key, const char *name2, uint8_t *key2,
const char *name3, uint8_t *key3, const char *name4, uint8_t *key4)
{
	int32_t n, newplayernum, i;
	uint8_t buf[4 + MAXPLAYERNAME + PUBKEYLENGTH + MAXAVAILABILITY];
	uint8_t *buf_p = buf;
	dboolean newplayer = false;

	{
		// splitscreen can allow 2+ players in one node
		for (; nodewaiting[node] > 0; nodewaiting[node]--)
		{
			newplayer = true;

			{
				uint8_t nobotoverwrite;

				// search for a free playernum
				// we can't solely use playeringame since it is not updated here
				for (newplayernum = dedicated ? 1 : 0; newplayernum < MAXPLAYERS; newplayernum++)
				{
					if (playeringame[newplayernum])
						continue;

					for (n = 0; n < MAXNETNODES; n++)
						if (nodetoplayer[n] == newplayernum
						|| nodetoplayer2[n] == newplayernum
						|| nodetoplayer3[n] == newplayernum
						|| nodetoplayer4[n] == newplayernum)
							break;

					if (n == MAXNETNODES)
						break;
				}

				nobotoverwrite = newplayernum;

				// FIXME This code here can access out of bounds equal to MAXPLAYERS
				while (playeringame[nobotoverwrite]
				&& players[nobotoverwrite].bot
				&& nobotoverwrite < MAXPLAYERS)
				{
					// Overwrite bots if there are NO other slots available.
					nobotoverwrite++;
				}

				if (nobotoverwrite < MAXPLAYERS)
				{
					newplayernum = nobotoverwrite;
				}
			}

			// should never happen since we check the playernum
			// before accepting the join
			// FIXME Possibly related to above?
			I_Assert(newplayernum < MAXPLAYERS);

			playernode[newplayernum] = (uint8_t)node;

			// Reset the buffer to the start for multiple joiners
			buf_p = buf;

			WRITEUINT8(buf_p, (uint8_t)node);
			WRITEUINT8(buf_p, newplayernum);

			if (playerpernode[node] < 1)
			{
				nodetoplayer[node] = newplayernum;
				WRITESTRINGN(buf_p, name, MAXPLAYERNAME);
				WRITEMEM(buf_p, key, PUBKEYLENGTH);
			}
			else if (playerpernode[node] < 2)
			{
				nodetoplayer2[node] = newplayernum;
				WRITESTRINGN(buf_p, name2, MAXPLAYERNAME);
				WRITEMEM(buf_p, key2, PUBKEYLENGTH);
			}
			else if (playerpernode[node] < 3)
			{
				nodetoplayer3[node] = newplayernum;
				WRITESTRINGN(buf_p, name3, MAXPLAYERNAME);
				WRITEMEM(buf_p, key3, PUBKEYLENGTH);
			}
			else if (playerpernode[node] < 4)
			{
				nodetoplayer4[node] = newplayernum;
				WRITESTRINGN(buf_p, name4, MAXPLAYERNAME);
				WRITEMEM(buf_p, key4, PUBKEYLENGTH);
			}

			WRITEUINT8(buf_p, nodetoplayer[node]); // consoleplayer
			WRITEUINT8(buf_p, playerpernode[node]); // splitscreen num

			for (i = 0; i < MAXAVAILABILITY; i++)
			{
				WRITEUINT8(buf_p, availabilities[i]);
			}

			playerpernode[node]++;

			SendNetXCmd(XD_ADDPLAYER, buf, buf_p - buf);
			DEBFILE(va("Server added player %d node %d\n", newplayernum, node));
		}
	}

	return newplayer;
}

/*--------------------------------------------------
	dboolean K_AddBotFromServer(uint16_t skin, uint8_t difficulty, botStyle_e style, uint8_t *p)

		See header file for description.
--------------------------------------------------*/
dboolean K_AddBotFromServer(uint16_t skin, uint8_t difficulty, botStyle_e style, uint8_t *p)
{
	uint8_t newplayernum = *p;

	// search for a free playernum
	// we can't use playeringame since it is not updated here
	for (; newplayernum < MAXPLAYERS; newplayernum++)
	{
		uint8_t n;

		for (n = 0; n < MAXNETNODES; n++)
		{
			if (nodetoplayer[n] == newplayernum
			|| nodetoplayer2[n] == newplayernum
			|| nodetoplayer3[n] == newplayernum
			|| nodetoplayer4[n] == newplayernum)
				break;
		}

		if (n == MAXNETNODES)
			break;
	}

	for (; newplayernum < MAXPLAYERS; newplayernum++)
	{
		if (playeringame[newplayernum] == false)
		{
			// free player slot
			break;
		}
	}

	if (newplayernum >= MAXPLAYERS)
	{
		// nothing is free
		*p = MAXPLAYERS;
		return false;
	}

	if (server)
	{
		uint8_t buf[5];
		uint8_t *buf_p = buf;

		WRITEUINT8(buf_p, newplayernum);

		if (skin > numskins)
		{
			skin = numskins;
		}

		WRITEUINT16(buf_p, skin);

		if (difficulty < 1)
		{
			difficulty = 1;
		}
		else if (difficulty > MAXBOTDIFFICULTY)
		{
			difficulty = MAXBOTDIFFICULTY;
		}

		WRITEUINT8(buf_p, difficulty);
		WRITEUINT8(buf_p, style);

		SendNetXCmd(XD_ADDBOT, buf, buf_p - buf);
		DEBFILE(va("Server added bot %d\n", newplayernum));
	}

	// use the next free slot (we can't put playeringame[newplayernum] = true here)
	*p = newplayernum+1;
	return true;
}

void CL_AddSplitscreenPlayer(void)
{
	if (cl_mode == CL_CONNECTED)
		CL_SendJoin();
}

void CL_RemoveSplitscreenPlayer(uint8_t p)
{
	if (cl_mode != CL_CONNECTED)
		return;

	SendKick(p, KICK_MSG_PLAYER_QUIT);
}

static void GotOurIP(uint32_t address)
{
#ifdef DEVELOP
	const unsigned char * p = (const unsigned char *)&address;
	CONS_Printf("Got IP of %u.%u.%u.%u\n", p[0], p[1], p[2], p[3]);
#endif
	ourIP = address;
}

// is there a game running
dboolean Playing(void)
{
	return (server && serverrunning) || (client && cl_mode == CL_CONNECTED);
}

dboolean InADedicatedServer(void)
{
	return Playing() && (dedicated || connectedtodedicated);
}

dboolean SV_SpawnServer(void)
{
	// Screw Testers mode, worst idea ever.
	dboolean result = false;
	if (demo.playback)
		G_StopDemo(); // reset engine parameter
	if (metalplayback)
		G_StopMetalDemo();

	if (!serverrunning)
	{
		CONS_Printf(M_GetText("Starting Server....\n"));
		serverrunning = true;
		SV_ResetServer();
		SV_GenContext();
		if (netgame && I_NetOpenSocket)
		{
			I_NetOpenSocket();
		}

		// non dedicated server just connect to itself
		if (!dedicated)
			CL_ConnectToServer();
		else doomcom->numslots = 1;
	}

	ourIP = 0;
	if (netgame && server)
		STUN_bind(GotOurIP);

	// strictly speaking, i'm not convinced the following is necessary
	// but I'm not confident enough to remove it entirely in case it breaks something

	// in a listen server, this sends the local player information to the server so
	// that the client has players replicated on the server. in dedicated, there is
	// no local player!
	if (!dedicated) {
		uint8_t *availabilitiesbuffer = R_GetSkinAvailabilities();
		int8_t node = 0;
		for (; node < MAXNETNODES; node++)
			result |= SV_AddWaitingPlayers(node, availabilitiesbuffer, cv_playername[0].zstring, g_public_key[0], cv_playername[1].zstring, g_public_key[1],
			cv_playername[2].zstring, g_public_key[2], cv_playername[3].zstring, g_public_key[3]);

	}
	return result;
}

void SV_StopServer(void)
{
	tic_t i;

	if (gamestate == GS_INTERMISSION)
		Y_EndIntermission();
	if (gamestate == GS_VOTING)
		Y_EndVote();
	gamestate = wipegamestate = GS_NULL;
	P_UnloadLevel();

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		localtextcmd[i][0] = 0;

		while (textcmdbuf[i] != NULL)
		{
			textcmdbuf_t *buf = textcmdbuf[i];
			textcmdbuf[i] = textcmdbuf[i]->next;
			Z_Free(buf);
		}
	}

	for (i = firstticstosend; i < firstticstosend + BACKUPTICS; i++)
		D_Clearticcmd(i);

	consoleplayer = 0;
	cl_mode = CL_ABORTED;
	maketic = gametic+1;
	neededtic = maketic;
	serverrunning = false;
	titlemapinaction = false;
}

// called at singleplayer start and stopdemo
void SV_StartSinglePlayerServer(int32_t dogametype, dboolean donetgame)
{
	int32_t lastgametype = gametype;
	server = true;
	multiplayer = (modeattacking == ATTACKING_NONE); // N: multiplayer in GP!
	connectedtodedicated = false;

	netgame = false; // so setting timelimit works... (XD_NETVAR doesn't play nice with SV_StopServer)

	G_SetGametype(dogametype);
	if (gametype != lastgametype)
		D_GameTypeChanged(lastgametype);

	netgame = donetgame;

	// no more tic the game with this settings!
	SV_StopServer();
}

static void SV_SendRefuse(int32_t node, const char *reason)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	strcpy(netbuffer->u.serverrefuse.reason, reason);

	netbuffer->packettype = PT_SERVERREFUSE;
	HSendPacket(node, false, 0, strlen(netbuffer->u.serverrefuse.reason) + 1);
	Net_CloseConnection(node);
}

// used at txtcmds received to check packetsize bound
static size_t TotalTextCmdPerTic(tic_t tic)
{
	int32_t i;
	size_t total = 1; // num of textcmds in the tic (ntextcmd byte)

	for (i = 0; i < MAXPLAYERS; i++)
	{
		uint8_t *textcmd = D_GetExistingTextcmd(tic, i);
		if ((!i || playeringame[i]) && textcmd)
			total += 2 + textcmd[0]; // "+2" for size and playernum
	}

	return total;
}

/** Called when a PT_CLIENTJOIN packet is received
  *
  * \param node The packet sender
  *
  */
static void HandleConnect(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	char names[MAXSPLITSCREENPLAYERS][MAXPLAYERNAME + 1];
	int32_t i, j;
	uint8_t availabilitiesbuffer[MAXAVAILABILITY];

	// Sal: Dedicated mode is INCREDIBLY hacked together.
	// If a server filled out, then it'd overwrite the host and turn everyone into weird husks.....
	// It's too much effort to legimately fix right now. Just prevent it from reaching that state.
	uint8_t maxplayers = min((dedicated ? MAXPLAYERS-1 : MAXPLAYERS), cv_maxplayers.value);
	uint8_t connectedplayers = 0;

	for (i = dedicated ? 1 : 0; i < MAXPLAYERS; i++)
	{
		// We use this to count players because it is affected by SV_AddWaitingPlayers when
		// more than one client joins on the same tic, unlike playeringame and D_NumPlayers.
		// UINT8_MAX denotes no node for that player.

		if (playernode[i] != UINT8_MAX)
		{
			// Sal: This hack sucks, but it should be safe.
			// playeringame is set for bots immediately; they are deterministic instead of a netxcmd.
			// If a bot is added with netxcmd in the future, then the node check is still here too.
			// So at worst, a theoretical netxcmd added bot will block real joiners for the time
			// it takes for the command to process, but not cause any horrifying player overwriting.
			if (playeringame[i] && players[i].bot)
			{
				continue;
			}

			connectedplayers++;
		}
	}

	if (bannednode && bannednode[node].banid != SIZE_MAX)
	{
		const char *reason = NULL;

		// Get the reason...
		if (!I_GetBanReason || (reason = I_GetBanReason(bannednode[node].banid)) == NULL)
			reason = "No reason given";

		if (bannednode[node].timeleft != NO_BAN_TIME)
		{
			 // these are fudged a little to allow it to sink in for impatient rejoiners
			int minutes = (bannednode[node].timeleft + 30) / 60;
			int hours = (minutes + 1) / 60;
			int days = (hours + 1) / 24;

			if (days)
			{
				SV_SendRefuse(node, va("K|%s\n(Time remaining: %d day%s)", reason, days, days > 1 ? "s" : ""));
			}
			else if (hours)
			{
				SV_SendRefuse(node, va("K|%s\n(Time remaining: %d hour%s)", reason, hours, hours > 1 ? "s" : ""));
			}
			else if (minutes)
			{
				SV_SendRefuse(node, va("K|%s\n(Time remaining: %d minute%s)", reason, minutes, minutes > 1 ? "s" : ""));
			}
			else
			{
				SV_SendRefuse(node, va("K|%s\n(Time remaining: <1 minute)", reason));
			}
		}
		else
		{
			SV_SendRefuse(node, va("B|%s", reason));
		}
	}
	else if (netbuffer->u.clientcfg._255 != 255 ||
			netbuffer->u.clientcfg.packetversion != PACKETVERSION)
	{
		SV_SendRefuse(node, "Incompatible packet formats.");
	}
	else if (strncmp(netbuffer->u.clientcfg.application, SRB2APPLICATION,
				sizeof netbuffer->u.clientcfg.application))
	{
		SV_SendRefuse(node, "Different SRB2Kart modifications\nare not compatible.");
	}
	else if (netbuffer->u.clientcfg.version != VERSION
		|| netbuffer->u.clientcfg.subversion != SUBVERSION)
	{
		SV_SendRefuse(node, va(M_GetText("Different SRB2Kart versions cannot\nplay a netgame!\n(server version %d.%d)"), VERSION, SUBVERSION));
	}
	else if (!cv_allownewplayer.value && node)
	{
		SV_SendRefuse(node, M_GetText("The server is not accepting\njoins for the moment."));
	}
	else if (connectedplayers >= maxplayers)
	{
		SV_SendRefuse(node, va(M_GetText("Maximum players reached: %d"), maxplayers));
	}
	else if (netgame && netbuffer->u.clientcfg.localplayers > MAXSPLITSCREENPLAYERS) // Hacked client?
	{
		SV_SendRefuse(node, M_GetText("Too many players from\nthis node."));
	}
	else if (netgame && connectedplayers + netbuffer->u.clientcfg.localplayers > maxplayers)
	{
		SV_SendRefuse(node, va(M_GetText("Number of local players\nwould exceed maximum: %d"), maxplayers));
	}
	else if (netgame && !netbuffer->u.clientcfg.localplayers) // Stealth join?
	{
		SV_SendRefuse(node, M_GetText("No players from\nthis node."));
	}
	else if (luafiletransfers)
	{
		SV_SendRefuse(node, M_GetText("The server is broadcasting a file\nrequested by a Lua script.\nPlease wait a bit and then\ntry rejoining."));
	}
	else if (netgame && joindelay > 2 * (tic_t)cv_joindelay.value * TICRATE)
	{
		SV_SendRefuse(node, va(M_GetText("Too many people are connecting.\nPlease wait %d seconds and then\ntry rejoining."),
			(joindelay - 2 * cv_joindelay.value * TICRATE) / TICRATE));
	}
	else
	{
		int sigcheck;
		dboolean newnode = false;
		char allZero[PUBKEYLENGTH];
		memset(allZero, 0, sizeof(allZero));

		for (i = 0; i < netbuffer->u.clientcfg.localplayers - playerpernode[node]; i++)
		{
			strlcpy(names[i], netbuffer->u.clientcfg.names[i], MAXPLAYERNAME + 1);
			if (!EnsurePlayerNameIsGood(names[i], -1))
			{
				SV_SendRefuse(node, "Bad player name");
				return;
			}

			if (node == 0) // Hey, that's us. We're always allowed to do what we want.
			{
				memcpy(lastReceivedKey[node][i], g_public_key[i], sizeof(lastReceivedKey[node][i]));
			}
			else // Remote player, gotta check their signature.
			{
				sigcheck = crypto_eddsa_check(netbuffer->u.clientcfg.challengeResponse[i], lastReceivedKey[node][i], lastSentChallenge[node], CHALLENGELENGTH);

				if (netgame && sigcheck != 0)
				{
					SV_SendRefuse(node, M_GetText("Signature verification failed."));
					return;
				}
			}

			if (cv_validate_pubkey.value)
			{
				// Check non-GUESTS for duplicate pubkeys, they'll create nonsense stats
				if (memcmp(lastReceivedKey[node][i], allZero, PUBKEYLENGTH) != 0)
				{
					// Players already here
					for (j = 0; j < MAXPLAYERS; j++)
					{
						if (!playeringame[j])
							continue;

						if (memcmp(lastReceivedKey[node][i], players[j].public_key, PUBKEYLENGTH) == 0)
						{
							SV_SendRefuse(node, M_GetText("This server disallows\nduplicate pubkeys,\n(Did you share your keys?)"));
							return;
						}
					}

					// Players we're trying to add
					for (j = 0; j < netbuffer->u.clientcfg.localplayers - playerpernode[node]; j++)
					{
						if (memcmp(lastReceivedKey[node][i], allZero, PUBKEYLENGTH) == 0)
							continue;
						if (i == j)
							continue;
						if (memcmp(lastReceivedKey[node][i], lastReceivedKey[node][j], PUBKEYLENGTH) == 0)
						{
							SV_SendRefuse(node, M_GetText("Duplicate pubkey in local party.\n(How did you even do this?)"));
							return;
						}
					}
				}
			}
		}

		memcpy(availabilitiesbuffer, netbuffer->u.clientcfg.availabilities, sizeof(availabilitiesbuffer));

		// client authorised to join
		nodewaiting[node] = (uint8_t)(netbuffer->u.clientcfg.localplayers - playerpernode[node]);
		if (!nodeingame[node])
		{
			gamestate_t backupstate = gamestate;
			newnode = true;

			SV_AddNode(node);

#ifdef VANILLAJOINNEXTROUND
			if (cv_joinnextround.value && gameaction == ga_nothing)
				G_SetGamestate(GS_WAITINGPLAYERS);
#endif
			if (!SV_SendServerConfig(node))
			{
				G_SetGamestate(backupstate);
				/// \note Shouldn't SV_SendRefuse be called before ResetNode?
				SV_SendRefuse(node, M_GetText("Server couldn't send info, please try again"));
				ResetNode(node); // Yeah, lets try it!
				/// \todo fix this !!!
				return; // restart the while
			}

			ServerInfoUpdateSend();
			//if (gamestate != GS_LEVEL) // GS_INTERMISSION, etc?
			//	SV_SendPlayerConfigs(node); // send bare minimum player info
			G_SetGamestate(backupstate);
			DEBFILE("new node joined\n");
		}
		if (nodewaiting[node])
		{
			if (node && newnode)
			{
				SV_SendSaveGame(node, false); // send a complete game state
				DEBFILE("send savegame\n");
			}
			SV_AddWaitingPlayers(node, availabilitiesbuffer, names[0], lastReceivedKey[node][0], names[1], lastReceivedKey[node][1],
				names[2], lastReceivedKey[node][2], names[3], lastReceivedKey[node][3]);
			joindelay += cv_joindelay.value * TICRATE;
			player_joining = true;
		}
	}
}

/** Called when a PT_SERVERSHUTDOWN packet is received
  *
  * \param node The packet sender (should be the server)
  *
  */
static void HandleShutdown(int8_t node)
{
	(void)node;
	LUA_HookBool(false, HOOK(GameQuit));
	D_QuitNetGame();
	CL_Reset();
	M_ReturnToConnectMenu();
	M_StartMessage2("Server Shutdown", M_GetText("Server has shutdown\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
}

void ServerInfoUpdateSend(void)
{
	uint32_t n;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	for (n = 1; n < MAXNETNODES; n++)
	{
		if (!nodeingame[n])
			continue;

		strncpy(netbuffer->u.serverinfoupdate.servermotd, connectedservermotd, MAXSERVERMOTD);
		CopyCaretColors(netbuffer->u.serverinfoupdate.servername, cv_servername.string, MAXSERVERNAME);
		CopyCaretColors(netbuffer->u.serverinfoupdate.servercontact, cv_server_contact.string, MAXSERVERCONTACT);
		strncpy(netbuffer->u.serverinfoupdate.serverdescription, connectedserverdescription, MAXSERVERDESCRIPTION);

		netbuffer->packettype = PT_SERVERINFOUPDATE;
		HSendPacket(n, true, 0, sizeof(netbuffer->u.serverinfoupdate));
	}
}

static void PT_ServerInfoUpdate(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (node != servernode)
		return;

	if ((size_t)doomcom->datalength < sizeof(netbuffer->u.serverinfoupdate))/* ignore partial sends */
		return;

	strncpy(connectedservermotd, netbuffer->u.serverinfoupdate.servermotd, MAXSERVERMOTD);
	CopyCaretColors(connectedservername, netbuffer->u.serverinfoupdate.servername, MAXSERVERNAME);
	CopyCaretColors(connectedservercontact, netbuffer->u.serverinfoupdate.servercontact, MAXSERVERCONTACT);
	strncpy(connectedserverdescription, netbuffer->u.serverinfoupdate.serverdescription, MAXSERVERDESCRIPTION);
}

/** Called when a PT_NODETIMEOUT packet is received
  *
  * \param node The packet sender (should be the server)
  *
  */
static void HandleTimeout(int8_t node)
{
	(void)node;
	LUA_HookBool(false, HOOK(GameQuit));
	D_QuitNetGame();
	CL_Reset();
	M_ReturnToConnectMenu();
	M_StartMessage2("Server Timeout", M_GetText("Server Timeout\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
}

void HandleSigfail(const char *string)
{
	if (server) // This situation is basically guaranteed to be nonsense.
	{
		CONS_Alert(CONS_ERROR, "Auth error! %s\n", string);
		return; // Keep the game running, you're probably testing.
	}

	LUA_HookBool(false, HOOK(GameQuit));
	D_QuitNetGame();
	CL_Reset();
	M_ReturnToConnectMenu();
	M_StartMessage2("Signature Failed", va(M_GetText("Signature check failed\n(%s)\n"),string), MB_NONE, MM_NOTHING, "Press any key", NULL);
}

/** Called when a PT_SERVERINFO packet is received
  *
  * \param node The packet sender
  * \note What happens if the packet comes from a client or something like that?
  *
  */
static void HandleServerInfo(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	char servername[MAXSERVERNAME];
	// compute ping in ms
	const tic_t ticnow = I_GetTime();
	const tic_t ticthen = (tic_t)LSBF_LONG(netbuffer->u.serverinfo.time);
	const tic_t ticdiff = (ticnow - ticthen)*1000/NEWTICRATE;
	netbuffer->u.serverinfo.time = (tic_t)LSBF_LONG(ticdiff);
	netbuffer->u.serverinfo.servername[MAXSERVERNAME-1] = 0;
	netbuffer->u.serverinfo.application
		[sizeof netbuffer->u.serverinfo.application - 1] = '\0';
	netbuffer->u.serverinfo.gametypename
		[sizeof netbuffer->u.serverinfo.gametypename - 1] = '\0';
	memcpy(servername, netbuffer->u.serverinfo.servername, MAXSERVERNAME);
	CopyCaretColors(netbuffer->u.serverinfo.servername, servername, MAXSERVERNAME);

	SL_InsertServer(&netbuffer->u.serverinfo, node);
}

// Helper function for packets that should only be sent by the server
// If it is NOT from the server, bail out and close the connection!
static dboolean ServerOnly(int8_t node)
{
	if (node == servernode)
		return false;

	Net_CloseConnection(node);
	return true;
}

static void PT_AskInfoViaMS(int8_t node)
{
	Net_CloseConnection(node);
}

static void PT_TellFilesNeeded(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (server && serverrunning)
	{
		uint8_t *p;
		int32_t firstfile = netbuffer->u.filesneedednum;

		netbuffer->packettype = PT_MOREFILESNEEDED;
		netbuffer->u.filesneededcfg.first = firstfile;
		netbuffer->u.filesneededcfg.more = 0;

		p = PutFileNeeded(firstfile);

		HSendPacket(node, false, 0, p - ((uint8_t *)&netbuffer->u));
	}
	else // Shouldn't get this if you aren't the server...?
		Net_CloseConnection(node);
}

static void PT_MoreFilesNeeded(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	if (server && serverrunning)
	{ // But wait I thought I'm the server?
		Net_CloseConnection(node);
		return;
	}
	if (ServerOnly(node))
		return;
	if (cl_mode == CL_ASKFULLFILELIST && netbuffer->u.filesneededcfg.first == fileneedednum)
	{
		D_ParseFileneeded(netbuffer->u.filesneededcfg.num, netbuffer->u.filesneededcfg.files, netbuffer->u.filesneededcfg.first);
		if (!netbuffer->u.filesneededcfg.more)
			cl_lastcheckedfilecount = UINT16_MAX; // Got the whole file list
	}
}

static void PT_AskInfo(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (server && serverrunning)
	{
		SV_SendServerInfo(node, (tic_t)LSBF_LONG(netbuffer->u.askinfo.time));
		SV_SendPlayerInfo(node); // Send extra info
	}
	Net_CloseConnection(node);
}

// Negative response of client join request
static void PT_ServerRefuse(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	if (server && serverrunning)
	{ // But wait I thought I'm the server?
		Net_CloseConnection(node);
		return;
	}
	if (ServerOnly(node))
		return;
	if (cl_mode == CL_WAITJOINRESPONSE)
	{
		// Save the reason so it can be displayed after quitting the netgame
		char *reason = strdup(netbuffer->u.serverrefuse.reason);
		if (!reason)
			I_Error("Out of memory!\n");

		if (strstr(reason, "Maximum players reached"))
		{
			serverisfull = true;
			//Special timeout for when refusing due to player cap. The client will wait 3 seconds between join requests when waiting for a slot, so we need this to be much longer
			//We set it back to the value of cv_nettimeout.value in CL_Reset
			connectiontimeout = NEWTICRATE*7;
			cl_mode = CL_SENDKEY;
			free(reason);
			return;
		}

		D_QuitNetGame();
		CL_Reset();
		M_ReturnToConnectMenu();

		if (reason[1] == '|')
		{
			M_StartMessage2("Join Failed", va("You have been %sfrom the server\n\nReason:\n%s",
							  (reason[0] == 'B') ? "banned\n" : "temporarily\nkicked ",
							  reason+2), MB_NONE, MM_NOTHING, "Press any key", NULL);
			CONS_Alert(CONS_NOTICE, "You have been %sfrom the server\n\nReason:\n%s",
						 (reason[0] == 'B') ? "banned\n" : "temporarily\nkicked ",
						 reason+2);
		}
		else
		{
			M_StartMessage2("Join Failed", va(M_GetText("Server refuses connection\n\nReason:\n%s"),
							  reason), MB_NONE, MM_NOTHING, "Press any key", NULL);
			CONS_Alert(CONS_NOTICE, "Server refuses connection\n\nReason:\n%s",
						   reason);
		}

		free(reason);

		// Will be reset by caller. Signals refusal.
		cl_mode = CL_ABORTED;
	}
}

// Positive response of client join request
static void PT_ServerCFG(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	if (server && serverrunning && node != servernode)
	{ // but wait I thought I'm the server?
		Net_CloseConnection(node);
		return;
	}
	if (ServerOnly(node))
		return;
	/// \note how would this happen? and is it doing the right thing if it does?
	if (!(cl_mode == CL_WAITJOINRESPONSE || cl_mode == CL_ASKJOIN))
		return;

	if (client)
	{
		maketic = gametic = neededtic = (tic_t)LSBF_LONG(netbuffer->u.servercfg.gametic);

		G_SetGametype(netbuffer->u.servercfg.gametype);

		modifiedgame = netbuffer->u.servercfg.modifiedgame;
		connectedtodedicated = netbuffer->u.servercfg.dedicated;

		memcpy(server_context, netbuffer->u.servercfg.server_context, 8);
	}

#ifdef HAVE_DISCORDRPC
	discordInfo.maxPlayers = netbuffer->u.servercfg.maxplayer;
	discordInfo.joinsAllowed = netbuffer->u.servercfg.allownewplayer;
	discordInfo.everyoneCanInvite = netbuffer->u.servercfg.discordinvites;
#endif

	nodeingame[(uint8_t)servernode] = true;
	serverplayer = netbuffer->u.servercfg.serverplayer;
	doomcom->numslots = LSBF_SHORT(netbuffer->u.servercfg.totalslotnum);
	mynode = netbuffer->u.servercfg.clientnode;
	if (serverplayer >= 0)
		playernode[(uint8_t)serverplayer] = servernode;

	if (netgame)
		CONS_Printf(M_GetText("Join accepted, waiting for complete game state...\n"));
	DEBFILE(va("Server accept join gametic=%u mynode=%d\n", gametic, mynode));

	/// \note Wait. What if a Lua script uses some global custom variables synched with the NetVars hook?
	///       Shouldn't them be downloaded even at intermission time?
	///       Also, according to HandleConnect, the server will send the savegame even during intermission...
	/// Sryder 2018-07-05: If we don't want to send the player config another way we need to send the gamestate
	///                    At almost any gamestate there could be joiners... So just always send gamestate?
	cl_mode = ((server) ? CL_CONNECTED : CL_DOWNLOADSAVEGAME);
}

/** Checks ticcmd for "speed hacks"
 *
 * \param p Which player
 * \return True if player is hacking
 * \sa HandlePacketFromPlayer
 *
 */
static dboolean CheckForSpeedHacks(uint8_t p)
{
	if (netcmds[maketic%BACKUPTICS][p].forwardmove > MAXPLMOVE || netcmds[maketic%BACKUPTICS][p].forwardmove < -MAXPLMOVE
		|| netcmds[maketic%BACKUPTICS][p].sidemove > MAXPLMOVE || netcmds[maketic%BACKUPTICS][p].sidemove < -MAXPLMOVE
		|| netcmds[maketic%BACKUPTICS][p].throwdir > KART_FULLTURN || netcmds[maketic%BACKUPTICS][p].throwdir < -KART_FULLTURN
		|| netcmds[maketic%BACKUPTICS][p].turning > KART_FULLTURN || netcmds[maketic%BACKUPTICS][p].turning < -KART_FULLTURN
	)
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal movement value received from node %d\n"), playernode[p]);
		//D_Clearticcmd(k);

		SendKick(p, KICK_MSG_CON_FAIL);
		return true;
	}

	return false;
}

static void PT_Say(int node)
{
	if (client)
		return; // Only sent to servers, why are we receiving this?

	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	say_pak say = netbuffer->u.say;

	// Check for a spoofed source.
	if (say.source == serverplayer)
	{
		// Servers aren't guaranteed to have a playernode, dedis exist.
		if (node != servernode)
			return;
	}
	else
	{
		if (playernode[say.source] != node)
			return;
	}

	if ((cv_mute.value || say.flags & (HU_CSAY|HU_SHOUT)) && say.source != serverplayer && !(IsPlayerAdmin(say.source)))
	{
		CONS_Debug(DBG_NETPLAY,"Received SAY cmd from Player %d (%s), but cv_mute is on.\n", say.source+1, player_names[say.source]);
		return;
	}

	if ((say.flags & HU_PRIVNOTICE) && !(IsPlayerAdmin(say.source)))
	{
		CONS_Debug(DBG_NETPLAY,"Received SAY cmd from Player %d (%s) with an illegal HU_PRIVNOTICE flag.\n", say.source+1, player_names[say.source]);
		SendKick(say.source, KICK_MSG_CON_FAIL);
		return;
	}

	{
		size_t i;
		const size_t j = strlen(say.message);
		for (i = 0; i < j; i++)
		{
			if (say.message[i] & 0x80)
			{
				CONS_Alert(CONS_WARNING, M_GetText("Illegal say command received from %s containing invalid characters\n"), player_names[say.source]);
				SendKick(say.source, KICK_MSG_CON_FAIL);
				return;
			}
		}
	}

	if (stop_spamming[say.source] != 0 && consoleplayer != say.source && cv_chatspamprotection.value && !(say.flags & (HU_CSAY|HU_SHOUT)))
	{
		CONS_Debug(DBG_NETPLAY,"Received SAY cmd too quickly from Player %d (%s), assuming as spam and blocking message.\n", say.source+1, player_names[say.source]);
		stop_spamming[say.source] = 4;
		return;
	}

	stop_spamming[say.source] = 4;

	DoSayCommand(say.message, say.target, say.flags, say.source);
}

static void PT_ClientCmd(int8_t node, int32_t netconsole)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	tic_t realend, realstart;

	if (client)
		return;

	// To save bytes, only the low byte of tic numbers are sent
	// Use ExpandTics to figure out what the rest of the bytes are

	realstart = ExpandTics(netbuffer->u.clientpak.client_tic, nettics[node]);
	realend = ExpandTics(netbuffer->u.clientpak.resendfrom, nettics[node]);

	if (   netbuffer->packettype == PT_CLIENTMIS  || netbuffer->packettype == PT_CLIENT2MIS
		|| netbuffer->packettype == PT_CLIENT3MIS || netbuffer->packettype == PT_CLIENT4MIS
		|| netbuffer->packettype == PT_NODEKEEPALIVEMIS
		|| supposedtics[node] < realend)
	{
		supposedtics[node] = realend;
	}
	// Discard out of order packet
	if (nettics[node] > realend)
	{
		DEBFILE(va("out of order ticcmd discarded nettics = %u\n", nettics[node]));
		return;
	}

	// Update the nettics
	nettics[node] = realend;

	// This should probably still timeout though, as the node should always have a player 1 number
	if (netconsole == -1)
		return;

	// As long as clients send valid ticcmds, the server can keep running, so reset the timeout
	/// \todo Use a separate cvar for that kind of timeout?
	freezetimeout[node] = I_GetTime() + connectiontimeout;

	// Don't do anything for packets of type NODEKEEPALIVE?
	// Sryder 2018/07/01: Update the freezetimeout still!
	if (netbuffer->packettype == PT_NODEKEEPALIVE
		|| netbuffer->packettype == PT_NODEKEEPALIVEMIS)
		return;

	// store it in an internal buffer so the last packet takes precedence, which minimizes input lag
	G_MoveTiccmd(&playercmds[netconsole], &netbuffer->u.clientpak.cmd, 1);

	// Check ticcmd for "speed hacks"
	if (CheckForSpeedHacks((uint8_t)netconsole))
		return;

	// Splitscreen cmd
	if ((nodetoplayer2[node] >= 0) &&
		  ((netbuffer->packettype == PT_CLIENT2CMD || netbuffer->packettype == PT_CLIENT2MIS)
		|| (netbuffer->packettype == PT_CLIENT3CMD || netbuffer->packettype == PT_CLIENT3MIS)
		|| (netbuffer->packettype == PT_CLIENT4CMD || netbuffer->packettype == PT_CLIENT4MIS)))
	{
		G_MoveTiccmd(&playercmds[(uint8_t)nodetoplayer2[node]], &netbuffer->u.client2pak.cmd2, 1);

		if (CheckForSpeedHacks((uint8_t)nodetoplayer2[node]))
			return;
	}

	if ((nodetoplayer3[node] >= 0) &&
		  ((netbuffer->packettype == PT_CLIENT3CMD || netbuffer->packettype == PT_CLIENT3MIS)
		|| (netbuffer->packettype == PT_CLIENT4CMD || netbuffer->packettype == PT_CLIENT4MIS)))
	{
		G_MoveTiccmd(&playercmds[(uint8_t)nodetoplayer3[node]], &netbuffer->u.client3pak.cmd3, 1);

		if (CheckForSpeedHacks((uint8_t)nodetoplayer3[node]))
			return;
	}

	if ((nodetoplayer4[node] >= 0) &&
		(netbuffer->packettype == PT_CLIENT4CMD || netbuffer->packettype == PT_CLIENT4MIS))
	{
		G_MoveTiccmd(&playercmds[(uint8_t)nodetoplayer4[node]], &netbuffer->u.client4pak.cmd4, 1);

		if (CheckForSpeedHacks((uint8_t)nodetoplayer4[node]))
			return;
	}

	consistancy_t clientconsistancy = netbuffer->u.clientpak.consistancy;

	if (cv_logconsistency.value)
	{
		if (realstart <= gametic && realstart + BACKUPTICS - 1 > gametic && gamestate == GS_LEVEL && dedicated && (netconsole != serverplayer))
		{
			I_OutputMsg("------------\n");
			I_OutputMsg(M_GetText("Testing consistency of player %d (%s)...\n"), netconsole+1, player_names[netconsole]);
			I_OutputMsg("Synching: %s\n", (consistancy[realstart%BACKUPTICS].checksum != LSBF_SHORT(clientconsistancy.checksum)) ? "false" : "true");
			I_OutputMsg("Checksum (Server): %d\n", consistancy[realstart%BACKUPTICS].checksum);
			I_OutputMsg("Checksum (Player %d): %d\n", netconsole+1, LSBF_SHORT(clientconsistancy.checksum));
			I_OutputMsg("------------\n");
		}
	}

	// Check player consistancy during the level
	if (realstart <= gametic && realstart + BACKUPTICS - 1 > gametic && gamestate == GS_LEVEL
		&& consistancy[realstart%BACKUPTICS].checksum != LSBF_SHORT(clientconsistancy.checksum)
		&& !resendingsavegame[node] && savegameresendcooldown[node] <= I_GetTime()
		&& !SV_ResendingSavegameToAnyone())
	{
		if (cv_blamecfail.value)
		{
			netbuffer->packettype = PT_WILLDIAGNOSERNG;

			// Free up some space for the RNG diagnosis
			memset(&netbuffer->u.rngdiagnosis, 0, sizeof(netbuffer->u.rngdiagnosis));

			// Copy over OUR seeds...
			int32_t _seed = 0;
			for (_seed = 0; _seed < PRNUMCLASS; _seed++)
			{
				netbuffer->u.rngdiagnosis.server_seeds[_seed] = P_GetRandSeed(_seed);
			}

			// ...and our current checksum...
			netbuffer->u.rngdiagnosis.checksums[0] = consistancy[realstart%BACKUPTICS].seed_checksum;

			// ...and where we remember the client being...
			netbuffer->u.rngdiagnosis.client_id = netconsole;

			if (cv_resynchattempts.value)
			{
				// Tell the client we are about to resend them the gamestate
				netbuffer->u.rngdiagnosis.nextpacket = PT_WILLRESENDGAMESTATE;
			}

			// ...and send that over to the client
			HSendPacket(node, true, 0, sizeof(netbuffer->u.rngdiagnosis));
		}

		if (cv_resynchattempts.value)
		{
			// Tell the client we are about to resend them the gamestate

			if (netbuffer->packettype != PT_WILLDIAGNOSERNG)
			{
				netbuffer->packettype = PT_WILLRESENDGAMESTATE;
				HSendPacket(node, true, 0, 0);

				resendingsavegame[node] = true;
			}

			if (cv_blamecfail.value)
			{
				char cfailrecieved[CONSISTANCYDBGSIZE]; // This one's huge!
				char cfailexpected[CONSISTANCYDBGSIZE];

				SPrintConsistancy(&cfailrecieved[0], &clientconsistancy);
				SPrintConsistancy(&cfailexpected[0], &consistancy[realstart%BACKUPTICS]);

				CONS_Printf(M_GetText("Synch failure for player %d (%s); expected %hd, got %hd\n"),
					netconsole+1, player_names[netconsole],
					consistancy[realstart%BACKUPTICS].checksum,
					LSBF_SHORT(clientconsistancy.checksum));

				// Extremely elaborate debug!
				CONS_Printf(M_GetText("EXPECTED\n%s"), cfailexpected);
				CONS_Printf(M_GetText("RECEIVED\n%s"), cfailrecieved);
			}
			DEBFILE(va("Restoring player %d (synch failure) [%update] %d!=%d\n",
				netconsole, realstart, consistancy[realstart%BACKUPTICS].checksum,
			LSBF_SHORT(clientconsistancy.checksum)));
			return;
		}
		else
		{
			if (cv_blamecfail.value)
			{
				char cfailrecieved[CONSISTANCYDBGSIZE]; // This one's huge!
				char cfailexpected[CONSISTANCYDBGSIZE];

				SPrintConsistancy(&cfailrecieved[0], &clientconsistancy);
				SPrintConsistancy(&cfailexpected[0], &consistancy[realstart%BACKUPTICS]);

				CONS_Printf(M_GetText("Kicking desynching player %d (%s); expected %hd, got %hd\n"),
					netconsole+1, player_names[netconsole],
					consistancy[realstart%BACKUPTICS].checksum,
					LSBF_SHORT(clientconsistancy.checksum));

				// Extremely elaborate debug!
				CONS_Printf(M_GetText("EXPECTED\n%s"), cfailexpected);
				CONS_Printf(M_GetText("RECEIVED\n%s"), cfailrecieved);

				/*
				// RNG debug...
				numdesynchingseeds = 0;
				{RNGSEEDS(PRINTSEEDSMISMATCH)}

				if (numdesynchingseeds < 1)
				{
					if (consistancy[realstart%BACKUPTICS].seed_checksum !=
					    clientconsistancy.seed_checksum)
					{
						CONS_Alert(CONS_ERROR, "Checksums mismatch, but"
							   "  no RNG seed mismatches found!\n");
					}
					else
					{
						CONS_Printf("No RNG seed mismatches found.\n");
					}
				}
				CONS_Printf("------------\n");
				*/
			}
			SendKick(netconsole, KICK_MSG_CON_FAIL);
			DEBFILE(va("player %d kicked (synch failure) [%u] %d!=%d\n",
						netconsole, realstart, consistancy[realstart%BACKUPTICS].checksum,
						LSBF_SHORT(clientconsistancy.checksum)));
			return;
		}
	}
}

static void PT_BasicKeepAlive(int8_t node, int32_t netconsole)
{
	if (client)
		return;

	// This should probably still timeout though, as the node should always have a player 1 number
	if (netconsole == -1)
		return;

	// If a client sends this it should mean they are done receiving the savegame
	sendingsavegame[node] = false;

	// As long as clients send keep alives, the server can keep running, so reset the timeout
	/// \todo Use a separate cvar for that kind of timeout?
	freezetimeout[node] = I_GetTime() + connectiontimeout;
}

static void PT_TextCmd(int8_t node, int32_t netconsole)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (client)
		return;

	if (netbuffer->packettype == PT_TEXTCMD2) // splitscreen special
		netconsole = nodetoplayer2[node];
	else if (netbuffer->packettype == PT_TEXTCMD3)
		netconsole = nodetoplayer3[node];
	else if (netbuffer->packettype == PT_TEXTCMD4)
		netconsole = nodetoplayer4[node];

	if (netconsole < 0 || netconsole >= MAXPLAYERS)
		Net_UnAcknowledgePacket(node);
	else
	{
		size_t j;
		tic_t tic = maketic;
		uint8_t *textcmd;

		// ignore if the textcmd has a reported size of zero
		// this shouldn't be sent at all
		if (!netbuffer->u.textcmd[0])
		{
			DEBFILE(va("GetPacket: Textcmd with size 0 detected! (node %u, player %d)\n",
					   node, netconsole));
			Net_UnAcknowledgePacket(node);
			return;
		}

		// ignore if the textcmd size var is actually larger than it should be
		// BASEPACKETSIZE + 1 (for size) + textcmd[0] should == datalength
		if (netbuffer->u.textcmd[0] > (size_t)doomcom->datalength-BASEPACKETSIZE-1)
		{
			DEBFILE(va("GetPacket: Bad Textcmd packet size! (expected %d, actual %s, node %u, player %d)\n",
					   netbuffer->u.textcmd[0], sizeu1((size_t)doomcom->datalength-BASEPACKETSIZE-1),
					   node, netconsole));
			Net_UnAcknowledgePacket(node);
			return;
		}

		// check if tic that we are making isn't too large else we cannot send it :(
		// doomcom->numslots+1 "+1" since doomcom->numslots can change within this time and sent time
		j = software_MAXPACKETLENGTH
		- (netbuffer->u.textcmd[0]+2+BASESERVERTICSSIZE
		+ (doomcom->numslots+1)*sizeof(ticcmd_t));

		// search a tic that have enougth space in the ticcmd
		while ((textcmd = D_GetExistingTextcmd(tic, netconsole)),
			(TotalTextCmdPerTic(tic) > j || netbuffer->u.textcmd[0] + (textcmd ? textcmd[0] : 0) > MAXTEXTCMD)
			&& tic < firstticstosend + BACKUPTICS)
			tic++;

		if (tic >= firstticstosend + BACKUPTICS)
		{
			DEBFILE(va("GetPacket: Textcmd too long (max %s, used %s, mak %d, "
			"tosend %u, node %u, player %d)\n", sizeu1(j), sizeu2(TotalTextCmdPerTic(maketic)),
					   maketic, firstticstosend, node, netconsole));
			Net_UnAcknowledgePacket(node);
			return;
		}

		// Make sure we have a buffer
		if (!textcmd) textcmd = D_GetTextcmd(tic, netconsole);

		DEBFILE(va("textcmd put in tic %u at position %d (player %d) ftts %u mk %u\n",
				   tic, textcmd[0]+1, netconsole, firstticstosend, maketic));

		memcpy(&textcmd[textcmd[0]+1], netbuffer->u.textcmd+1, netbuffer->u.textcmd[0]);
		textcmd[0] += (uint8_t)netbuffer->u.textcmd[0];
	}
}

static void PT_Login(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (client)
		return;

	I_Assert((uint8_t)node < sizeof(adminsalt) / sizeof(adminsalt[0]));

	// I_GetRandomBytes should get it's data from a CSPRNG, so it's safe to use it here.
	I_GetRandomBytes(adminsalt[node], sizeof(adminsalt[node]));
	adminsalt[node][8] = '\0'; // convenience
	netbuffer->packettype = PT_LOGINCHALLENGE;
	memcpy(netbuffer->u.salt, adminsalt[node], sizeof(adminsalt[node]));
	HSendPacket(node, true, 0, sizeof(adminsalt[node]));
}

static void PT_LoginAuth(int8_t node, int32_t netconsole)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	uint8_t finalsha256[32];/* Well, it's the cool thing to do? */
	uint32_t i;
	if (client)
		return;

	if ((size_t)doomcom->datalength < sizeof(netbuffer->u.sha256sum))/* ignore partial sends */
		return;

	if (adminsalt[node][0] == 0)
	{
		CONS_Printf(M_GetText("Password from %s failed (no login request).\n"), player_names[netconsole]);
		return;
	}
	if (adminpasscount == 0)
	{
		adminsalt[node][0] = 0;
		CONS_Printf(M_GetText("Password from %s failed (no password set).\n"), player_names[netconsole]);
		return;
	}

	for (i = 0; i < adminpasscount; i++)
	{
		// Do the final pass to compare with the sent md5
		D_SHA256PasswordPass((const uint8_t *)adminpass[i], strlen(adminpass[i]), adminsalt[node], finalsha256);

		if (!memcmp(netbuffer->u.sha256sum, finalsha256, 32))
		{
			adminsalt[node][0] = 0;
			CONS_Printf(M_GetText("%s passed authentication.\n"), player_names[netconsole]);
			COM_BufInsertText(va("promote %d\n", netconsole)); // do this immediately
			return;
		}
	}

	adminsalt[node][0] = 0;
	CONS_Printf(M_GetText("Password from %s failed.\n"), player_names[netconsole]);
}

static void PT_ClientQuit(int8_t node, int32_t netconsole)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (client)
		return;

	// nodeingame will be put false in the execution of kick command
	// this allow to send some packets to the quitting client to have their ack back
	nodewaiting[node] = 0;
	if (netconsole != -1 && playeringame[netconsole])
	{
		uint8_t kickmsg;

		if (netbuffer->packettype == PT_NODETIMEOUT)
			kickmsg = KICK_MSG_TIMEOUT;
		else
			kickmsg = KICK_MSG_PLAYER_QUIT;

		SendKick(netconsole, kickmsg);

		/*
		 n odetoplayer[node] = -1;*

		 if (nodetoplayer2[node] != -1 && nodetoplayer2[node] >= 0
			 && playeringame[(uint8_t)nodetoplayer2[node]])
			 {
			 SendKick(nodetoplayer2[node], kickmsg);
		 nodetoplayer2[node] = -1;
	}

	if (nodetoplayer3[node] != -1 && nodetoplayer3[node] >= 0
		&& playeringame[(uint8_t)nodetoplayer3[node]])
		{
		SendKick(nodetoplayer3[node], kickmsg);
	nodetoplayer3[node] = -1;
	}

	if (nodetoplayer4[node] != -1 && nodetoplayer4[node] >= 0
		&& playeringame[(uint8_t)nodetoplayer4[node]])
		{
		SendKick(nodetoplayer4[node], kickmsg);
	nodetoplayer4[node] = -1;
	}
	*/
	}
	Net_CloseConnection(node);
	nodeingame[node] = false;
	nodeneedsauth[node] = false;
}

static void PT_CanReceiveGamestate(int8_t node)
{
	if (client || sendingsavegame[node])
		return;

	CONS_Printf(M_GetText("Resending game state to %s...\n"), player_names[nodetoplayer[node]]);

	SV_SendSaveGame(node, true); // Resend a complete game state
	resendingsavegame[node] = true;
}

static void PT_AskLuaFile(int8_t node)
{
	if (server && luafiletransfers && luafiletransfers->nodestatus[node] == LFTNS_ASKED)
		AddLuaFileToSendQueue(node, luafiletransfers->realfilename);
}

static void PT_HasLuaFile(int8_t node)
{
	if (server && luafiletransfers && luafiletransfers->nodestatus[node] == LFTNS_SENDING)
		SV_HandleLuaFileSent(node);
}

static void PT_ReceivedGamestate(int8_t node)
{
	sendingsavegame[node] = false;
	resendingsavegame[node] = false;
	savegameresendcooldown[node] = I_GetTime() + 5 * TICRATE;
}

static void PT_ServerTics(int8_t node, int32_t netconsole)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	uint8_t *pak, *txtpak, numtxtpak;
	tic_t realend, realstart;

	if (!nodeingame[node])
	{
		// Do not remove my own server (we have just get a out of order packet)
		if (node != servernode)
		{
			DEBFILE(va("unknown packet received (%d) from unknown host\n",netbuffer->packettype));
			Net_CloseConnection(node);
		}
		return;
	}

	// Only accept PT_SERVERTICS from the server.
	if (node != servernode)
	{
		CONS_Alert(CONS_WARNING, M_GetText("%s received from non-host %d\n"), "PT_SERVERTICS", node);
		if (server)
			SendKick(netconsole, KICK_MSG_CON_FAIL);
		return;;
	}

	realstart = ExpandTics(netbuffer->u.serverpak.starttic, maketic);
	realend = realstart + netbuffer->u.serverpak.numtics;

	txtpak = (uint8_t *)&netbuffer->u.serverpak.cmds[netbuffer->u.serverpak.numslots
	* netbuffer->u.serverpak.numtics];

	if (realend > gametic + CLIENTBACKUPTICS)
		realend = gametic + CLIENTBACKUPTICS;
	cl_packetmissed = realstart > neededtic;

	if (realstart <= neededtic && realend > neededtic)
	{
		tic_t i, j;
		pak = (uint8_t *)&netbuffer->u.serverpak.cmds;

		for (i = realstart; i < realend; i++)
		{
			// clear first
			D_Clearticcmd(i);

			// copy the tics
			pak = G_ScpyTiccmd(netcmds[i%BACKUPTICS], pak,
							   netbuffer->u.serverpak.numslots*sizeof (ticcmd_t));

			// copy the textcmds
			numtxtpak = *txtpak++;
			for (j = 0; j < numtxtpak; j++)
			{
				int32_t playernum = *txtpak++; // playernum
				const size_t txtsize = txtpak[0]+1;

				if (playernum < 0 || playernum >= MAXPLAYERS)
				{
					CONS_Alert(CONS_WARNING, "Got bogus NetXCmd packet targetting player %d\n", playernum);
					return;
				}

				if (i >= gametic) // Don't copy old net commands
					memcpy(D_GetTextcmd(i, playernum), txtpak, txtsize);
				txtpak += txtsize;
			}
		}

		neededtic = realend;
	}
	else
	{
		DEBFILE(va("frame not in bound: %u\n", neededtic));
		/*if (realend < neededtic - 2 * TICRATE || neededtic + 2 * TICRATE < realstart)
		 *	I_Error("Received an out of order PT_SERVERTICS packet!\n"
		 *			"Got tics %d-%d, needed tic %d\n\n"
		 *			"Please report this crash on the Master Board,\n"
		 *			"IRC or Discord so it can be fixed.\n", (int32_t)realstart, (int32_t)realend, (int32_t)neededtic);*/
	}
}

static void PT_Ping(int8_t node, int32_t netconsole)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	// Only accept PT_PING from the server.
	if (node != servernode)
	{
		CONS_Alert(CONS_WARNING, M_GetText("%s received from non-host %d\n"), "PT_PING", node);
		if (server)
			SendKick(netconsole, KICK_MSG_CON_FAIL);
		return;
	}

	//Update client ping table from the server.
	if (client)
	{
		uint8_t i;
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i])
			{
				playerpingtable[i] = (tic_t)netbuffer->u.netinfo.pingtable[i];
				playerpacketlosstable[i] = netbuffer->u.netinfo.packetloss[i];
				playerdelaytable[i] = netbuffer->u.netinfo.delay[i];
			}
		}

		servermaxping = (tic_t)netbuffer->u.netinfo.pingtable[MAXPLAYERS];
	}
}

static void PT_WillDiagnoseRNG(int8_t node, int32_t netconsole)
{
	(void)netconsole;
	I_OutputMsg(M_GetText("Preparing RNG diagnosis...\n"));

	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (server)
		return;

	if (node != servernode)
		return; // Not the server, so we don't care.

	// Gather our seeds and send that to the server again.

	int32_t _seed = 0;
	for (_seed = 0; _seed < PRNUMCLASS; _seed++)
	{
		netbuffer->u.rngdiagnosis.client_seeds[_seed] = P_GetRandSeed(_seed);

		if (_seed & 1)
		{
			netbuffer->u.rngdiagnosis.checksums[1] -= netbuffer->u.rngdiagnosis.client_seeds[_seed];
		}
		else
		{
			netbuffer->u.rngdiagnosis.checksums[1] += netbuffer->u.rngdiagnosis.client_seeds[_seed];
		}
	}

	netbuffer->packettype = PT_RNGDIAGNOSIS;

	if (!HSendPacket(servernode, true, 0, sizeof(netbuffer->u.rngdiagnosis)))
	{
		I_OutputMsg(M_GetText("ERROR: Wasn't able to send our seeds to the server!\n"));
		return;
	}

	I_OutputMsg(M_GetText("Sent our seeds to the server.\n"));
}

// Blast-print the seeds; very much not feasible with dynamic seed allocation
#define PRINTSEEDSMISMATCH(seed)                                                               \
	if (netbuffer->u.rngdiagnosis.server_seeds[PR_##seed] !=                               \
	    netbuffer->u.rngdiagnosis.client_seeds[PR_##seed])                                 \
	{                                                                                      \
		CONS_Printf("Seed desynch for RNG class %d (" #seed "); expected 0x%08" PRIX32 \
			    ", got 0x%08" PRIX32 "\n",                                         \
			    PR_##seed,                                                         \
			    netbuffer->u.rngdiagnosis.server_seeds[PR_##seed],                 \
			    netbuffer->u.rngdiagnosis.client_seeds[PR_##seed]);                \
		numdesynchingseeds++;                                                          \
	};

static void PT_RNGDiagnosis(int8_t node)
{
	(void)node;

	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	uint16_t pid = netbuffer->u.rngdiagnosis.client_id;

	CONS_Printf(M_GetText("Diagnosing RNG for player %d (%s)...\n"), pid+1, player_names[pid]);

	numdesynchingseeds = 0;
	{RNGSEEDS(PRINTSEEDSMISMATCH)}

	if (numdesynchingseeds < 1)
	{
		if (netbuffer->u.rngdiagnosis.checksums[0] !=
		    netbuffer->u.rngdiagnosis.checksums[1])
		{
			CONS_Alert(CONS_ERROR, "Checksums mismatch, but"
				   " no RNG seed mismatches found!\n");
		}
		else
		{
			CONS_Printf("No RNG seed mismatches found.\n");
		}
	}
	CONS_Printf("------------\n");

	if (netbuffer->u.rngdiagnosis.nextpacket == PT_WILLRESENDGAMESTATE)
	{
		// Resend the gamestate to the player
		I_OutputMsg("Gamestate resend queued.\n");
		netbuffer->packettype = PT_WILLRESENDGAMESTATE;
		HSendPacket(node, true, 0, 0);

		resendingsavegame[node] = true;
	}
}

#undef PRINTSEEDSMISMATCH

static void PT_WillResendGamestate(int8_t node)
{
	(void)node;

	char tmpsave[264];
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (server || cl_redownloadinggamestate)
		return;

	// Don't let the server pull a fast one with everyone's identity!
	// Save the public keys we see, so if the server tries to swap one, we'll know.
	int i;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		memcpy(priorKeys[i], players[i].public_key, sizeof(priorKeys[i]));
	}

	// Send back a PT_CANRECEIVEGAMESTATE packet to the server
	// so they know they can start sending the game state
	netbuffer->packettype = PT_CANRECEIVEGAMESTATE;
	if (!HSendPacket(servernode, true, 0, 0))
		return;

	CONS_Printf(M_GetText("Reloading game state...\n"));

	snprintf(tmpsave, sizeof(tmpsave), "%s" PATHSEP TMPSAVENAME, srb2home);

	// Don't get a corrupt savegame error because tmpsave already exists
	if (FIL_FileExists(tmpsave) && unlink(tmpsave) == -1)
		I_Error("Can't delete %s\n", tmpsave);

	CL_PrepareDownloadSaveGame(tmpsave);

	cl_redownloadinggamestate = true;
}

static void PT_LoginChallenge(int8_t node)
{
	char salt[9];
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	if (node != servernode)
		return;

	if ((size_t)doomcom->datalength < sizeof(netbuffer->u.salt))/* ignore partial sends */
		return;

	if (reqpass == NULL)
		return; // got PT_LOGINCHALLENGE but we didn't request a login

	memcpy(salt, netbuffer->u.salt, sizeof(netbuffer->u.salt));
	D_SHA256PasswordPass((const uint8_t *)reqpass, strlen(reqpass), salt, netbuffer->u.sha256sum);
	Z_Free(reqpass);
	reqpass = NULL;

	netbuffer->packettype = PT_LOGINAUTH;
	HSendPacket(servernode, true, 0, sizeof(netbuffer->u.sha256sum));
}

static void PT_SendingLuaFile(int8_t node)
{
	(void)node;

	if (client)
		CL_PrepareDownloadLuaFile();
}

static void HandlePlayerInfo(int8_t node)
{
	(void)node;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	for (int32_t i = 0; i < MAXPLAYERINFO; i++)
	{
		playerinfo[i] = netbuffer->u.playerinfo[i];
	}
	receivedplayerinfo = true;
}

static void PT_ReqMapQueue(int node)
{
	if (client)
		return; // Only sent to servers, why are we receiving this?

	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	reqmapqueue_pak reqmapqueue = netbuffer->u.reqmapqueue;

	// Check for a spoofed source.
	if (reqmapqueue.source == serverplayer)
	{
		// Servers aren't guaranteed to have a playernode, dedis exist.
		if (node != servernode)
			return;
	}
	else
	{
		if (playernode[reqmapqueue.source] != node)
			return;

		if (!IsPlayerAdmin(reqmapqueue.source))
		{
			CONS_Debug(DBG_NETPLAY,"Received illegal request map queue cmd from Player %d (%s).\n", reqmapqueue.source+1, player_names[reqmapqueue.source]);
			SendKick(reqmapqueue.source, KICK_MSG_CON_FAIL);
			return;
		}
	}

	const dboolean doclear = (reqmapqueue.newgametype == ROUNDQUEUE_CMD_CLEAR);

	// The following prints will only appear when multiple clients
	// attempt to affect the round queue at similar time increments
	if (doclear == true)
	{
		if (roundqueue.size == 0)
		{
			// therefore this one doesn't really need a error print
			// because what both players wanted was done anyways
			//CONS_Alert(CONS_ERROR, "queuemap: Queue is already empty!\n");
			return;
		}
	}
	else if (reqmapqueue.newgametype == ROUNDQUEUE_CMD_SHOW)
	{
		char maprevealmsg[256];
		if (roundqueue.size == 0)
		{
			strlcpy(maprevealmsg, "There are no Rounds queued.", 256);
		}
		else if (roundqueue.position >= roundqueue.size)
		{
			strlcpy(maprevealmsg, "There are no more Rounds queued!", 256);
		}
		else
		{
			char *title = G_BuildMapTitle(roundqueue.entries[roundqueue.position].mapnum + 1);

			strlcpy(
				maprevealmsg,
				va("The next Round will be on \"%s\".", title),
				256
			);

			Z_Free(title);
		}
		DoSayCommand(maprevealmsg, 0, HU_SHOUT, servernode);

		return;
	}
	else if (roundqueue.size >= ROUNDQUEUE_MAX)
	{
		CONS_Alert(CONS_ERROR, "Recieved REQMAPQUEUE, but unable to add map beyond %u\n", roundqueue.size);

		// But this one does, because otherwise it's silent failure!
		char rejectmsg[256];
		strlcpy(rejectmsg, "The server couldn't queue your chosen map.", 256);
		SendServerNotice(reqmapqueue.source, rejectmsg);

		return;
	}

	if (reqmapqueue.newmapnum == NEXTMAP_VOTING)
	{
		/*uint8_t numPlayers = 0, i;
		for (i = 0; i < MAXPLAYERS; ++i)
		{
			if (!playeringame[i] || players[i].spectator)
			{
				continue;
			}

			extern consvar_t cv_forcebots; // debug

			if (!(gametypes[reqmapqueue.newgametype]->rules & GTR_BOTS) && players[i].bot && !cv_forcebots.value)
			{
				// Gametype doesn't support bots
				continue;
			}

			numPlayers++;
		}*/

		reqmapqueue.newmapnum = G_RandMap(G_TOLFlag(reqmapqueue.newgametype), UINT16_MAX, false, 0, false, NULL);
	}

	if (reqmapqueue.newmapnum >= nummapheaders)
	{
		CONS_Alert(CONS_ERROR, "Recieved REQMAPQUEUE, but unable to add map of invalid ID (%u)\n", reqmapqueue.newmapnum);

		char rejectmsg[256];
		strlcpy(rejectmsg, "The server couldn't queue your chosen map.", 256);
		SendServerNotice(reqmapqueue.source, rejectmsg);

		return;
	}

	G_AddMapToBuffer(reqmapqueue.newmapnum);

	uint8_t buf[1+2+1];
	uint8_t *buf_p = buf;

	WRITEUINT8(buf_p, reqmapqueue.flags);
	WRITEUINT16(buf_p, reqmapqueue.newgametype);

	WRITEUINT8(buf_p, roundqueue.size);

	// Match Got_MapQueuecmd, but with the addition of reqmapqueue.newmapnum available to us
	if (doclear == true)
	{
		memset(&roundqueue, 0, sizeof(struct roundqueue));
	}
	else
	{
		G_MapIntoRoundQueue(
			reqmapqueue.newmapnum,
			reqmapqueue.newgametype,
			((reqmapqueue.flags & 1) != 0),
							false
		);
	}

	SendNetXCmd(XD_MAPQUEUE, buf, buf_p - buf);
}


/** Handles a packet received from a node that isn't in game
  *
  * \param node The packet sender
  * \todo Choose a better name, as the packet can also come from the server apparently?
  * \sa HandlePacketFromPlayer
  * \sa GetPackets
  *
  */
static void HandlePacketFromAwayNode(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	if (node != servernode)
		DEBFILE(va("Received packet from unknown host %d\n", node));

	switch (netbuffer->packettype)
	{
		case PT_ASKINFOVIAMS   : PT_AskInfoViaMS   (node    );      break;
		case PT_TELLFILESNEEDED: PT_TellFilesNeeded(node    );      break;
		case PT_MOREFILESNEEDED: PT_MoreFilesNeeded(node    );      break;
		case PT_ASKINFO        : PT_AskInfo        (node    );      break;
		case PT_SERVERREFUSE   : PT_ServerRefuse   (node    );      break;
		case PT_SERVERCFG      : PT_ServerCFG      (node    );      break;
		case PT_FILEFRAGMENT   : PT_FileFragment   (node, -1);      break;
		case PT_FILEACK        : PT_FileAck        (node    );      break;
		case PT_FILERECEIVED   : PT_FileReceived   (node    );      break;
		case PT_REQUESTFILE    : PT_RequestFile    (node    );      break;
		case PT_NODETIMEOUT    : PT_ClientQuit     (node, -1);      break;
		case PT_CLIENTQUIT     : PT_ClientQuit     (node, -1);      break;
		case PT_SERVERTICS     : PT_ServerTics     (node, -1);      break;
		case PT_CLIENTCMD      :                                    break; // This is not an "unknown packet"
		case PT_PLAYERINFO     : HandlePlayerInfo  (node    );      break;
		case PT_CLIENTKEY      : PT_ClientKey      (node    );      break;
		case PT_SERVERCHALLENGE: PT_ServerChallenge(node,&cl_mode); break;

		default:
			DEBFILE(va("unknown packet received (%d) from unknown host\n",netbuffer->packettype));
			Net_CloseConnection(node);
	}
}

static char NodeToSplitPlayer(int node, int split)
{
	if (split == 0)
		return nodetoplayer[node];
	else if (split == 1)
		return nodetoplayer2[node];
	else if (split == 2)
		return nodetoplayer3[node];
	else if (split == 3)
		return nodetoplayer4[node];
	return -1;
}

static void PT_ChallengeAll(int8_t node)
{
	if (demo.playback || node != servernode) // SERVER should still respond to this to prove its own identity, just not from clients.
		return;

	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	int challengeplayers;

	memcpy(lastChallengeAll, netbuffer->u.challengeall.secret, sizeof(lastChallengeAll));

	shouldsign_t safe = ShouldSignChallenge(lastChallengeAll);
	if (safe != SIGN_OK)
	{
		if (safe == SIGN_BADIP)
			HandleSigfail("External server sent the wrong IP");
		else if (safe == SIGN_BADTIME)
			HandleSigfail("Bad timestamp - is your time set correctly?");
		else
			HandleSigfail("Unknown auth error - contact a developer");
		return;
	}

	netbuffer->packettype = PT_RESPONSEALL;

	#ifdef DEVELOP
	if (cv_noresponse.value)
	{
		CV_AddValue(&cv_noresponse, -1);
		CONS_Alert(CONS_WARNING, "cv_noresponse enabled, not sending PT_RESPONSEALL\n");
		return;
	}
	#endif

	// Don't leak uninitialized memory.
	memset(&netbuffer->u.responseall, 0, sizeof(netbuffer->u.responseall));

	for (challengeplayers = 0; challengeplayers <= splitscreen; challengeplayers++)
	{
		uint8_t signature[SIGNATURELENGTH];

		crypto_eddsa_sign(signature, g_secret_key[challengeplayers], lastChallengeAll, sizeof(lastChallengeAll));
		if (crypto_eddsa_check(signature, g_public_key[challengeplayers], lastChallengeAll, sizeof(lastChallengeAll)) != 0)
			I_Error("Couldn't self-verify key associated with player %d.\nKey may be corrupted.", challengeplayers);

		#ifdef DEVELOP
		if (cv_badresponse.value)
		{
			CV_AddValue(&cv_badresponse, -1);
			CONS_Alert(CONS_WARNING, "cv_badresponse enabled, scrubbing signature from PT_RESPONSEALL\n");
			memset(signature, 0, sizeof(signature));
		}
		#endif

		memcpy(netbuffer->u.responseall.signature[challengeplayers], signature, sizeof(signature));
	}

	HSendPacket(servernode, true, 0, sizeof(netbuffer->u.responseall));
}

static void PT_ResponseAll(int8_t node)
{
	if (demo.playback || client)
		return;

	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	int responseplayer;
	for (responseplayer = 0; responseplayer < MAXSPLITSCREENPLAYERS; responseplayer++)
	{
		int targetplayer = NodeToSplitPlayer(node, responseplayer);
		if (targetplayer == -1)
			continue;

		if (crypto_eddsa_check(netbuffer->u.responseall.signature[responseplayer], players[targetplayer].public_key, lastChallengeAll, sizeof(lastChallengeAll)))
		{
			// Something scary can happen when multiple kicks that resolve to the same node are processed in quick succession.
			// Sometimes, a kick will still be left to process after the player's been disposed, and that causes the kick to resolve on the server instead!
			// This sucks, so we check for a stale/misfiring kick beforehand.
			if (playernode[targetplayer] != 0)
				SendKick(targetplayer, KICK_MSG_SIGFAIL);
			return;
		}
		else
		{
			memcpy(lastReceivedSignature[targetplayer], netbuffer->u.responseall.signature[responseplayer], sizeof(lastReceivedSignature[targetplayer]));
		}
	}
}

static void PT_ResultsAll(int8_t node)
{
	if (demo.playback || server || node != servernode || !expectChallenge)
		return;

	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	int resultsplayer;
	uint8_t allZero[PUBKEYLENGTH];
	memset(allZero, 0, PUBKEYLENGTH);

	for (resultsplayer = 0; resultsplayer < MAXPLAYERS; resultsplayer++)
	{
		if (!playeringame[resultsplayer])
		{
			continue;
		}
		else if (memcmp(knownWhenChallenged[resultsplayer], allZero, PUBKEYLENGTH) == 0)
		{
			// Wasn't here for the challenge.
			continue;
		}
		else if (memcmp(knownWhenChallenged[resultsplayer], players[resultsplayer].public_key, sizeof(knownWhenChallenged[resultsplayer])) != 0)
		{
			// A player left after the challenge process started, and someone else took their place.
			// That means they haven't received a challenge either.
			continue;
		}
		else
		{
			if (crypto_eddsa_check(netbuffer->u.resultsall.signature[resultsplayer],
				knownWhenChallenged[resultsplayer], lastChallengeAll, sizeof(lastChallengeAll)))
			{
				CONS_Alert(CONS_WARNING, "PT_RESULTSALL had invalid signature %s for node %d player %d split %d, something doesn't add up!\n",
						GetPrettyRRID(netbuffer->u.resultsall.signature[resultsplayer], true), playernode[resultsplayer], resultsplayer, players[resultsplayer].splitscreenindex);
				HandleSigfail("Server sent invalid client signature.");
				return;
			}
		}
	}
	csprng(lastChallengeAll, sizeof(lastChallengeAll));
	expectChallenge = false;
}

/** Handles a packet received from a node that is in game
  *
  * \param node The packet sender
  * \todo Choose a better name
  * \sa HandlePacketFromAwayNode
  * \sa GetPackets
  *
  */
static void HandlePacketFromPlayer(int8_t node)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	int32_t netconsole;

	if (dedicated && node == 0)
		netconsole = 0;
	else
		netconsole = nodetoplayer[node];
#ifdef PARANOIA
	if (netconsole >= MAXPLAYERS)
		I_Error("bad table nodetoplayer: node %d player %d", doomcom->remotenode, netconsole);
#endif

#ifdef SIGNGAMETRAFFIC
	if (server)
	{
		int splitnodes;
		if (IsPacketSigned(netbuffer->packettype))
		{
			for (splitnodes = 0; splitnodes < MAXSPLITSCREENPLAYERS; splitnodes++)
			{
				int targetplayer = NodeToSplitPlayer(node, splitnodes);
				if (targetplayer == -1)
					continue;

				const void* message = &netbuffer->u;

				if (demo.playback)
				{
					//CONS_Printf("Throwing out a guest signature from node %d player %d\n", node, splitnodes);
				}
				else
				{
					if (crypto_eddsa_check(netbuffer->signature[splitnodes], players[targetplayer].public_key, message, doomcom->datalength - BASEPACKETSIZE))
					{
						CONS_Alert(CONS_ERROR, "SIGFAIL! Packet type %d from node %d player %d\nkey %s size %d netconsole %d\n",
							netbuffer->packettype, node, splitnodes,
							GetPrettyRRID(players[targetplayer].public_key, true), doomcom->datalength - BASEPACKETSIZE, netconsole);

						// Something scary can happen when multiple kicks that resolve to the same node are processed in quick succession.
						// Sometimes, a kick will still be left to process after the player's been disposed, and that causes the kick to resolve on the server instead!
						// This sucks, so we check for a stale/misfiring kick beforehand.
						if (netconsole != -1)
							SendKick(netconsole, KICK_MSG_SIGFAIL);
						Net_CloseConnection(node);
						nodeingame[node] = false;
						return;
					}
				}
			}
		}
	}
#endif

	switch (netbuffer->packettype)
	{
		// SERVER RECEIVE
		case PT_CLIENTCMD          : PT_ClientCmd          (node, netconsole); break;
		case PT_CLIENT2CMD         : PT_ClientCmd          (node, netconsole); break;
		case PT_CLIENT3CMD         : PT_ClientCmd          (node, netconsole); break;
		case PT_CLIENT4CMD         : PT_ClientCmd          (node, netconsole); break;
		case PT_CLIENTMIS          : PT_ClientCmd          (node, netconsole); break;
		case PT_CLIENT2MIS         : PT_ClientCmd          (node, netconsole); break;
		case PT_CLIENT3MIS         : PT_ClientCmd          (node, netconsole); break;
		case PT_CLIENT4MIS         : PT_ClientCmd          (node, netconsole); break;
		case PT_NODEKEEPALIVE      : PT_ClientCmd          (node, netconsole); break;
		case PT_NODEKEEPALIVEMIS   : PT_ClientCmd          (node, netconsole); break;
		case PT_BASICKEEPALIVE     : PT_BasicKeepAlive     (node, netconsole); break;
		case PT_TEXTCMD            : PT_TextCmd            (node, netconsole); break;
		case PT_TEXTCMD2           : PT_TextCmd            (node, netconsole); break;
		case PT_TEXTCMD3           : PT_TextCmd            (node, netconsole); break;
		case PT_TEXTCMD4           : PT_TextCmd            (node, netconsole); break;
		case PT_SAY                : PT_Say                (node            ); break;
		case PT_REQMAPQUEUE        : PT_ReqMapQueue        (node            ); break;
		case PT_LOGIN              : PT_Login              (node            ); break;
		case PT_LOGINAUTH          : PT_LoginAuth          (node, netconsole); break;
		case PT_NODETIMEOUT        : PT_ClientQuit         (node, netconsole); break;
		case PT_CLIENTQUIT         : PT_ClientQuit         (node, netconsole); break;
		case PT_CANRECEIVEGAMESTATE: PT_CanReceiveGamestate(node            ); break;
		case PT_ASKLUAFILE         : PT_AskLuaFile         (node            ); break;
		case PT_HASLUAFILE         : PT_HasLuaFile         (node            ); break;
		case PT_RECEIVEDGAMESTATE  : PT_ReceivedGamestate  (node            ); break;
		case PT_RNGDIAGNOSIS       : PT_RNGDiagnosis       (node            ); break;
		// CLIENT RECEIVE
		case PT_SERVERTICS         : PT_ServerTics         (node, netconsole); break;
		case PT_PING               : PT_Ping               (node, netconsole); break;
		case PT_FILEFRAGMENT       : PT_FileFragment       (node, netconsole); break;
		case PT_FILEACK            : PT_FileAck            (node            ); break;
		case PT_FILERECEIVED       : PT_FileReceived       (node            ); break;
		case PT_LOGINCHALLENGE     : PT_LoginChallenge     (node            ); break;
		case PT_SERVERINFOUPDATE   : PT_ServerInfoUpdate   (node            ); break;
		case PT_WILLRESENDGAMESTATE: PT_WillResendGamestate(node            ); break;
		case PT_WILLDIAGNOSERNG    : PT_WillDiagnoseRNG    (node, netconsole); break;
		case PT_SENDINGLUAFILE     : PT_SendingLuaFile     (node            ); break;
		case PT_SERVERCFG          :                                           break;
		// CHALLENGE
		case PT_CHALLENGEALL       : PT_ChallengeAll       (node            ); break;
		case PT_RESPONSEALL        : PT_ResponseAll        (node            ); break;
		case PT_RESULTSALL         : PT_ResultsAll         (node            ); break;
		default:
			DEBFILE(va("UNKNOWN PACKET TYPE RECEIVED %d from host %d\n",
				netbuffer->packettype, node));
	}
}

/**	Handles all received packets, if any
  *
  * \todo Add details to this description (lol)
  *
  */
static void GetPackets(void)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	int8_t node; // The packet sender

	player_joining = false;

	while (HGetPacket())
	{
		node = (int8_t)doomcom->remotenode;

		if (netbuffer->packettype == PT_CLIENTJOIN && server)
		{
			if (levelloading == false) // Otherwise just ignore
			{
				HandleConnect(node);
			}
			continue;
		}
		if (node == servernode && client && cl_mode != CL_SEARCHING)
		{
			if (netbuffer->packettype == PT_SERVERSHUTDOWN)
			{
				HandleShutdown(node);
				continue;
			}
			if (netbuffer->packettype == PT_NODETIMEOUT)
			{
				HandleTimeout(node);
				continue;
			}
		}

		if (netbuffer->packettype == PT_SERVERINFO)
		{
			HandleServerInfo(node);
			continue;
		}

		// Packet received from someone already playing
		if (nodeingame[node])
			HandlePacketFromPlayer(node);
		// Packet received from someone not playing
		else
			HandlePacketFromAwayNode(node);
	}
}

static uint32_t GenerateConsistancySum(consistancy_t *c)
{
	uint32_t sum = 0;

	// RNG seed checksum
	sum += c->seed_checksum;

	// Player data
	sum += c->playerpos[0];
	sum += c->playerpos[1];
	sum += c->itemsum;

	// Object data
	sum += c->position[0];
	sum += c->position[1];
	sum += c->position[2];

	sum += c->momentum[0];
	sum += c->momentum[1];
	sum += c->momentum[2];

	sum += c->mobjtype;
	sum += c->mobjangle;
	sum += c->mobjflags;
	sum += c->mobjflags2;
	sum += c->mobjeflags;
	sum += c->mobjstate;
	sum += c->mobjtics;
	sum += c->mobjsprite;
	sum += c->mobjframe;

	c->checksum = (int16_t)(sum & 0xFFFF);

	return sum;
}

static void ResetConsistancy(consistancy_t *c)
{
	c->checksum = 0;

	c->playerpos[0] = 0;
	c->playerpos[1] = 0;

	c->position[0] = 0;
	c->position[1] = 0;
	c->position[2] = 0;

	c->momentum[0] = 0;
	c->momentum[1] = 0;
	c->momentum[2] = 0;

	c->itemsum = 0; // Player item checksum

	c->seed_checksum = 0;

	// Object data checksum
	c->mobjtype = 0;
	c->mobjangle = 0;
	c->mobjflags = 0;
	c->mobjflags2 = 0;
	c->mobjeflags = 0;
	c->mobjstate = 0;
	c->mobjtics = 0;
	c->mobjsprite = 0;
	c->mobjframe = 0;
}

// Feeds a consistancy debug print to a string.
void SPrintConsistancy(char *out, consistancy_t *c)
{
	snprintf(out,
		 CONSISTANCYDBGSIZE,
		 "------------\nChecksum: %d\nRandomization Seed Checksum: 0x%" PRIX32 "\n------------\n[Player Position] X: %d, Y: %d\nItem Checksum: %d\n[Position] X: %d, Y: %d, Z: %d\n[Speed] X: %d, Y: %d, Z: %d\nObject Type: %d\nAngle: %d\n[Flags] Flags1: %d, Flags2: %d, EFlags: %d\n[Animation] State: %d, Tics %d, Sprite: %d, Frame: %d\n------------\n",
		 c->checksum,
		 c->seed_checksum,
		 c->playerpos[0], c->playerpos[1],
		 c->itemsum,
		 c->position[0], c->position[1], c->position[2],
		 c->momentum[0], c->momentum[1], c->momentum[2],
		 c->mobjtype,
		 c->mobjangle,
		 c->mobjflags, c->mobjflags2, c->mobjeflags,
		 c->mobjstate,
		 c->mobjtics,
		 c->mobjsprite,
		 c->mobjframe);
}

// I'm losing my sanity fast, just do this.
/*inline static void ConsistancyXOR(consistancy_t *c, int32_t value)
{
	c->playerpos[0] ^= value;
	c->playerpos[1] ^= value;

	c->itemsum ^= value; // Player item checksum

	c->position[0] ^= value;
	c->position[1] ^= value;
	c->position[2] ^= value;

	c->momentum[0] ^= value;
	c->momentum[1] ^= value;
	c->momentum[2] ^= value;

	c->rngseed ^= value;

	// Object data checksum
	c->mobjtype ^= value;
	c->mobjangle ^= value;
	c->mobjflags ^= value;
	c->mobjflags2 ^= value;
	c->mobjeflags ^= value;
	c->mobjstate ^= value;
	c->mobjtics ^= value;
	c->mobjsprite ^= value;
	c->mobjframe ^= value;
}*/

//
// NetUpdate
// Builds ticcmds for console player,
// sends out a packet
//
// no more use random generator, because at very first tic isn't yet synchronized
// Note: It is called consistAncy on purpose.
//
static int16_t Consistancy(consistancy_t *c, uint32_t idx)
{
	int32_t i = 0;
	uint32_t ret = 0;
#ifdef MOBJCONSISTANCY
	thinker_t *th = NULL;
	mobj_t *mo = NULL;
#endif

	DEBFILE(va("TIC %u ", gametic));

	if (!c)
	{
		// !?
		return -1;
	}

	ResetConsistancy(c);
	memset(serverseedarchive[idx], 0, sizeof(serverseedarchive[idx]));

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
		{
			c->playerpos[0] ^= 0xCCCC;
			c->playerpos[1] ^= 0xCCCC;
			c->itemsum ^= 0xCCCC;
		}
		else if (!players[i].mo || gamestate != GS_LEVEL);
		else
		{
			c->playerpos[0] += players[i].mo->x;
			c->playerpos[1] -= players[i].mo->y;
			c->itemsum += players[i].itemtype; // powers[pw_shield]


			c->playerpos[0] *= i+1;
			c->playerpos[1] *= i+1;
			c->itemsum *= i+1;
		}
	}
	// I give up
	// Coop desynching enemies is painful
	if (gamestate == GS_LEVEL)
	{
		// Write seeds before consistancy
		int32_t seed = 0;
		for (seed = 0; seed < PRNUMCLASS; seed++)
		{
			serverseedarchive[idx][seed] = P_GetRandSeed(seed);
		}

		// To make the checksum a bit more reliable, we're going to loop through
		// every seed, and do the "add-subtract" sampling as-per mobjs.
		for (i = 0; i < PRNUMCLASS; i++)
		{
			if (i & 1)
			{
				c->seed_checksum -= P_GetRandSeed(i);
			}
			else
			{
				c->seed_checksum += P_GetRandSeed(i);
			}
		}
	}

#ifdef MOBJCONSISTANCY
	if (gamestate == GS_LEVEL)
	{
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mo = (mobj_t *)th;

			if (!MobjIsNetSynced(mo))
				continue;

			if (mo->flags & (MF_SPECIAL | MF_SOLID | MF_PUSHABLE | MF_BOSS | MF_MISSILE | MF_SPRING | MF_MONITOR | MF_FIRE | MF_ENEMY | MF_PAIN | MF_STICKY))
			{
				c->mobjtype -= mo->type;
				c->position[0] += mo->x;
				c->position[1] -= mo->y;
				c->position[2] += mo->z;
				c->momentum[0] -= mo->momx;
				c->momentum[1] += mo->momy;
				c->momentum[2] -= mo->momz;
				c->mobjangle += mo->angle;
				c->mobjflags -= mo->flags;
				c->mobjflags2 += mo->flags2;
				c->mobjeflags -= mo->eflags;
				if (mo->target)
				{
					c->mobjtype += mo->target->type;
					c->position[0] -= mo->target->x;
					c->position[1] += mo->target->y;
					c->position[2] -= mo->target->z;
					c->momentum[0] += mo->target->momx;
					c->momentum[1] -= mo->target->momy;
					c->momentum[2] += mo->target->momz;
					c->mobjangle -= mo->target->angle;
					c->mobjflags += mo->target->flags;
					c->mobjflags2 -= mo->target->flags2;
					c->mobjeflags += mo->target->eflags;
					c->mobjstate -= mo->target->state - states;
					c->mobjtics += mo->target->tics;
					c->mobjsprite -= mo->target->sprite;
					//c->mobjframe += mo->target->frame;
				}
				else
					ConsistancyXOR(c, 0x3333);
				if (mo->tracer && MobjIsNetSynced(mo->tracer))
				{
					c->mobjtype += mo->tracer->type;
					c->position[0] -= mo->tracer->x;
					c->position[1] += mo->tracer->y;
					c->position[2] -= mo->tracer->z;
					c->momentum[0] += mo->tracer->momx;
					c->momentum[1] -= mo->tracer->momy;
					c->momentum[2] += mo->tracer->momz;
					c->mobjangle -= mo->tracer->angle;
					c->mobjflags += mo->tracer->flags;
					c->mobjflags2 -= mo->tracer->flags2;
					c->mobjeflags += mo->tracer->eflags;
					c->mobjstate -= mo->tracer->state - states;
					c->mobjtics += mo->tracer->tics;
					c->mobjsprite -= mo->tracer->sprite;
					//c->mobjframe += mo->tracer->frame;
				}
				else
					ConsistancyXOR(c, 0xAAAA);
				// SRB2Kart: We use hnext & hprev very extensively
				if (mo->hnext && MobjIsNetSynced(mo->hnext))
				{
					c->mobjtype += mo->hnext->type;
					c->position[0] -= mo->hnext->x;
					c->position[1] += mo->hnext->y;
					c->position[2] -= mo->hnext->z;
					c->momentum[0] += mo->hnext->momx;
					c->momentum[1] -= mo->hnext->momy;
					c->momentum[2] += mo->hnext->momz;
					c->mobjangle -= mo->hnext->angle;
					c->mobjflags += mo->hnext->flags;
					c->mobjflags2 -= mo->hnext->flags2;
					c->mobjeflags += mo->hnext->eflags;
					c->mobjstate -= mo->hnext->state - states;
					c->mobjtics += mo->hnext->tics;
					c->mobjsprite -= mo->hnext->sprite;
					//c->mobjframe += mo->hnext->frame;
				}
				else
					ConsistancyXOR(c, 0x5555);
				if (mo->hprev && MobjIsNetSynced(mo->hprev))
				{
					c->mobjtype += mo->hprev->type;
					c->position[0] -= mo->hprev->x;
					c->position[1] += mo->hprev->y;
					c->position[2] -= mo->hprev->z;
					c->momentum[0] += mo->hprev->momx;
					c->momentum[1] -= mo->hprev->momy;
					c->momentum[2] += mo->hprev->momz;
					c->mobjangle -= mo->hprev->angle;
					c->mobjflags += mo->hprev->flags;
					c->mobjflags2 -= mo->hprev->flags2;
					c->mobjeflags += mo->hprev->eflags;
					c->mobjstate -= mo->hprev->state - states;
					c->mobjtics += mo->hprev->tics;
					c->mobjsprite -= mo->hprev->sprite;
					//c->mobjframe += mo->hprev->frame;
				}
				else
					ConsistancyXOR(c, 0xCCCC);
				c->mobjstate -= mo->state - states;
				c->mobjtics += mo->tics;
				c->mobjsprite -= mo->sprite;
				//c->mobjframe += mo->frame;
			}
		}
	}
#endif

	ret = GenerateConsistancySum(c);

	DEBFILE(va("Consistancy = %u\n", (ret & 0xFFFF)));

	return (int16_t)(ret & 0xFFFF);
}

// confusing, but this DOESN'T send PT_NODEKEEPALIVE, it sends PT_BASICKEEPALIVE
// used during wipes to tell the server that a node is still connected
static void CL_SendClientKeepAlive(void)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	netbuffer->packettype = PT_BASICKEEPALIVE;

	HSendPacket(servernode, false, 0, 0);
}

static void SV_SendServerKeepAlive(void)
{
	int32_t n;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	for (n = 1; n < MAXNETNODES; n++)
	{
		if (!nodeingame[n])
			continue;

		netbuffer->packettype = PT_BASICKEEPALIVE;
		HSendPacket(n, false, 0, 0);
	}
}

// Consistancy A gets all of Consistancy B's values.
#define CloneConsistancy(ca, cb) \
{ \
	ca.checksum = cb.checksum; \
	ca.seed_checksum = cb.seed_checksum; \
	ca.itemsum = cb.itemsum; \
\
	ca.playerpos[0] = cb.playerpos[0]; \
	ca.playerpos[1] = cb.playerpos[1]; \
\
	ca.position[0] = cb.position[0]; \
	ca.position[1] = cb.position[1]; \
	ca.position[2] = cb.position[2]; \
	ca.momentum[0] = cb.momentum[0]; \
	ca.momentum[1] = cb.momentum[1]; \
	ca.momentum[2] = cb.momentum[2]; \
	ca.mobjtype = cb.mobjtype; \
	ca.mobjangle = cb.mobjangle; \
	ca.mobjflags = cb.mobjflags; \
	ca.mobjflags2 = cb.mobjflags2; \
	ca.mobjeflags = cb.mobjeflags; \
	ca.mobjstate = cb.mobjstate; \
	ca.mobjtics = cb.mobjtics; \
	ca.mobjsprite = cb.mobjsprite; \
	ca.mobjframe = cb.mobjframe; \
}

// send the client packet to the server
static void CL_SendClientCmd(void)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	size_t packetsize = 0;
	dboolean mis = false;

	netbuffer->packettype = PT_CLIENTCMD;

	if (cl_packetmissed)
	{
		netbuffer->packettype = PT_CLIENTMIS;
		mis = true;
	}

	netbuffer->u.clientpak.resendfrom = (uint8_t)(neededtic & UINT8_MAX);
	netbuffer->u.clientpak.client_tic = (uint8_t)(gametic & UINT8_MAX);

	if (gamestate == GS_WAITINGPLAYERS)
	{
		// Send PT_NODEKEEPALIVE packet
		netbuffer->packettype = (mis ? PT_NODEKEEPALIVEMIS : PT_NODEKEEPALIVE);
		packetsize = sizeof (clientcmd_pak) - sizeof (ticcmd_t) - sizeof (int16_t);
		HSendPacket(servernode, false, 0, packetsize);
	}
	else if (gamestate != GS_NULL && (addedtogame || dedicated))
	{
		uint8_t lagDelay = 0;

		if (lowest_lag > 0)
		{
			// Gentlemens' ping.
			lagDelay = min(lowest_lag, MAXGENTLEMENDELAY);
		}

		packetsize = sizeof (clientcmd_pak);
		G_MoveTiccmd(&netbuffer->u.clientpak.cmd, &localcmds[0][lagDelay], 1);

		CloneConsistancy(netbuffer->u.clientpak.consistancy,
				 consistancy[gametic % BACKUPTICS]);

		if (splitscreen) // Send a special packet with 2 cmd for splitscreen
		{
			netbuffer->packettype = (mis ? PT_CLIENT2MIS : PT_CLIENT2CMD);
			packetsize = sizeof (client2cmd_pak);
			G_MoveTiccmd(&netbuffer->u.client2pak.cmd2, &localcmds[1][lagDelay], 1);

			if (splitscreen > 1)
			{
				netbuffer->packettype = (mis ? PT_CLIENT3MIS : PT_CLIENT3CMD);
				packetsize = sizeof (client3cmd_pak);
				G_MoveTiccmd(&netbuffer->u.client3pak.cmd3, &localcmds[2][lagDelay], 1);

				if (splitscreen > 2)
				{
					netbuffer->packettype = (mis ? PT_CLIENT4MIS : PT_CLIENT4CMD);
					packetsize = sizeof (client4cmd_pak);
					G_MoveTiccmd(&netbuffer->u.client4pak.cmd4, &localcmds[3][lagDelay], 1);
				}
			}
		}

		HSendPacket(servernode, false, 0, packetsize);
	}

	if (cl_mode == CL_CONNECTED || dedicated)
	{
		uint8_t i;
		// Send extra data if needed
		for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
		{
			if (localtextcmd[i][0])
			{
				switch (i)
				{
					case 3:
						netbuffer->packettype = PT_TEXTCMD4;
						break;
					case 2:
						netbuffer->packettype = PT_TEXTCMD3;
						break;
					case 1:
						netbuffer->packettype = PT_TEXTCMD2;
						break;
					default:
						netbuffer->packettype = PT_TEXTCMD;
						break;
				}

				memcpy(netbuffer->u.textcmd, localtextcmd[i], localtextcmd[i][0]+1);
				// All extra data have been sent
				if (HSendPacket(servernode, true, 0, localtextcmd[i][0]+1)) // Send can fail...
				{
					localtextcmd[i][0] = 0;

					if (textcmdbuf[i] != NULL)
					{
						textcmdbuf_t *buf = textcmdbuf[i];
						memcpy(localtextcmd[i], textcmdbuf[i]->cmd, textcmdbuf[i]->cmd[0]+1);
						textcmdbuf[i] = textcmdbuf[i]->next;
						Z_Free(buf);
					}
				}
			}
		}
	}
}

// send the server packet
// send tic from firstticstosend to maketic-1
static void SV_SendTics(void)
{
	tic_t realfirsttic, lasttictosend, i;
	uint32_t n;
	int32_t j;
	size_t packsize;
	uint8_t *bufpos;
	uint8_t *ntextcmd;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	// send to all client but not to me
	// for each node create a packet with x tics and send it
	// x is computed using supposedtics[n], max packet size and maketic
	for (n = 1; n < MAXNETNODES; n++)
	{
		if (!nodeingame[n])
			continue;

		// assert supposedtics[n]>=nettics[n]
		realfirsttic = supposedtics[n];
		lasttictosend = min(maketic, nettics[n] + CLIENTBACKUPTICS);

		if (realfirsttic >= lasttictosend)
		{
			// well we have sent all tics we will so use extrabandwidth
			// to resent packet that are supposed lost (this is necessary since lost
			// packet detection work when we have received packet with firsttic > neededtic
			// (getpacket servertics case)
			DEBFILE(va("Nothing to send node %u mak=%u sup=%u net=%u \n",
					   n, lasttictosend, supposedtics[n], nettics[n]));
			realfirsttic = nettics[n];
			if (realfirsttic >= lasttictosend || (I_GetTime() + n)&3)
				// all tic are ok
				continue;
			DEBFILE(va("Sent %d anyway\n", realfirsttic));
		}

		realfirsttic = max(realfirsttic, firstticstosend);

		// compute the length of the packet and cut it if too large
		packsize = BASESERVERTICSSIZE;

		for (i = realfirsttic; i < lasttictosend; i++)
		{
			packsize += sizeof (ticcmd_t) * doomcom->numslots;
			packsize += TotalTextCmdPerTic(i);

			if (packsize > software_MAXPACKETLENGTH)
			{
				DEBFILE(va("packet too large (%s) at tic %d (should be from %d to %d)\n",
						   sizeu1(packsize), i, realfirsttic, lasttictosend));
				lasttictosend = i;

				// too bad: too much player have send extradata and there is too
				//          much data in one tic.
				// To avoid it put the data on the next tic. (see getpacket
				// textcmd case) but when numplayer changes the computation can be different
				if (lasttictosend == realfirsttic)
				{
					if (packsize > MAXPACKETLENGTH)
						I_Error("Too many players: can't send %s data for %d players to node %d\n"
						"Well sorry nobody is perfect....\n",
						sizeu1(packsize), doomcom->numslots, n);
					else
					{
						lasttictosend++; // send it anyway!
						DEBFILE("sending it anyway\n");
					}
				}
				break;
			}
		}

		// Send the tics
		netbuffer->packettype = PT_SERVERTICS;
		netbuffer->u.serverpak.starttic = (uint8_t)realfirsttic;
		netbuffer->u.serverpak.numtics = (uint8_t)(lasttictosend - realfirsttic);
		netbuffer->u.serverpak.numslots = (uint8_t)LSBF_SHORT(doomcom->numslots);
		bufpos = (uint8_t *)&netbuffer->u.serverpak.cmds;

		for (i = realfirsttic; i < lasttictosend; i++)
		{
			bufpos = G_DcpyTiccmd(bufpos, netcmds[i%BACKUPTICS], doomcom->numslots * sizeof (ticcmd_t));
		}

		// add textcmds
		for (i = realfirsttic; i < lasttictosend; i++)
		{
			ntextcmd = bufpos++;
			*ntextcmd = 0;
			for (j = 0; j < MAXPLAYERS; j++)
			{
				uint8_t *textcmd = D_GetExistingTextcmd(i, j);
				int32_t size = textcmd ? textcmd[0] : 0;

				if ((!j || playeringame[j]) && size)
				{
					(*ntextcmd)++;
					WRITEUINT8(bufpos, j);
					memcpy(bufpos, textcmd, size + 1);
					bufpos += size + 1;
				}
			}
		}
		packsize = bufpos - (uint8_t *)&(netbuffer->u);

		HSendPacket(n, false, 0, packsize);
		// when tic are too large, only one tic is sent so don't go backward!
		if (lasttictosend-doomcom->extratics > realfirsttic)
			supposedtics[n] = lasttictosend-doomcom->extratics;
		else
			supposedtics[n] = lasttictosend;

		supposedtics[n] = max(supposedtics[n], nettics[n]);
	}
	// node 0 is me!
	supposedtics[0] = maketic;
}

//
// TryRunTics
//
static void CreateNewLocalCMD(uint8_t p, int32_t realtics)
{
	int32_t i;

	for (i = MAXGENTLEMENDELAY-1; i > 0; i--)
	{
		G_MoveTiccmd(&localcmds[p][i], &localcmds[p][i-1], 1);
	}

	G_BuildTiccmd(&localcmds[p][0], realtics, p+1);
	localcmds[p][0].flags |= TICCMD_RECEIVED;
}

static void Local_Maketic(int32_t realtics)
{
	int32_t i;

	I_OsPolling(); // I_Getevent
	D_ProcessEvents(); // menu responder, cons responder,
	                   // game responder calls HU_Responder, AM_Responder,
	                   // and G_MapEventsToControls

	if (!dedicated) rendergametic = gametic;

	// translate inputs (keyboard/mouse/joystick) into game controls
	for (i = 0; i <= splitscreen; i++)
	{
		CreateNewLocalCMD(i, realtics);
	}
}

// create missed tic
static void SV_Maketic(void)
{
	int32_t i;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;
		if (K_PlayerUsesBotMovement(&players[i]))
		{
			K_BuildBotTiccmd(&players[i], &playercmds[i]);
			continue;
		}
		// We didn't receive this tic
		if ((netcmds[maketic % BACKUPTICS][i].flags & TICCMD_RECEIVED) == 0)
		{
			ticcmd_t *    ticcmd = &netcmds[(maketic    ) % BACKUPTICS][i];
			ticcmd_t *prevticcmd = &netcmds[(maketic - 1) % BACKUPTICS][i];
			// Copy the input from the previous tic
			*ticcmd = *prevticcmd;
			ticcmd->flags &= ~TICCMD_RECEIVED;
		}
	}
	// Moved here so bots and packetloss indication doesn't break....
	G_CopyTiccmd(netcmds[maketic % BACKUPTICS], playercmds, MAXPLAYERS);
	maketic++;
}

dboolean TryRunTics(tic_t realtics)
{
	dboolean ticking;

	// the machine has lagged but it is not so bad
	if (realtics > TICRATE/7) // FIXME: consistency failure!!
	{
		if (server)
			realtics = 1;
		else
			realtics = TICRATE/7;
	}

	if (singletics)
		realtics = 1;

	if (realtics >= 1)
	{
		COM_BufTicker();
		if (mapchangepending)
			D_MapChange(-1, 0, encoremode, false, 2, false, forcespecialstage); // finish the map change

		// fun fact: this used to be located in NetUpdate!
		// as it turns out, reloading the level in rendering code is a very bad idea!
		if (client)
		{
			// If the client just finished redownloading the game state, load it
			if (cl_redownloadinggamestate && fileneeded[0].status == FS_FOUND)
				CL_ReloadReceivedSavegame();

			hu_redownloadinggamestate = cl_redownloadinggamestate;
		}
	}

	NetUpdate();

	if (demo.playback)
	{
		neededtic = gametic + realtics;
		// start a game after a demo
		maketic += realtics;
		firstticstosend = maketic;
		tictoclear = firstticstosend;
	}

	GetPackets();

#ifdef DEBUGFILE
	if (debugfile && (realtics || neededtic > gametic))
	{
		//SoM: 3/30/2000: Need long int32_t in the format string for args 4 & 5.
		//Shut up stupid warning!
		fprintf(debugfile, "------------ Tryruntic: REAL:%d NEED:%d GAME:%d LOAD: %d\n",
			realtics, neededtic, gametic, debugload);
		debugload = 100000;
	}
#endif

	ticking = neededtic > gametic;

	if (ticking)
	{
		if (realtics)
			hu_stopped = false;
	}

	if (player_joining)
	{
		if (realtics)
			hu_stopped = true;
		return false;
	}

	if (ticking)
	{
		{

			// run the count * tics
			while (neededtic > gametic)
			{
				dboolean dontRun = false;

				dboolean update_stats = !(paused || P_AutoPause());

				DEBFILE(va("============ Running tic %d (local %d)\n", gametic, localgametic));

				if (update_stats)
				{
					ps_prevtictime.value.p = ps_tictime.value.p;
					PS_START_TIMING(ps_tictime);
				}


				dontRun = ExtraDataTicker();

				if (levelloading == false
					|| gametic > levelstarttic + 5) // Don't lock-up if a malicious client is sending tons of netxcmds
				{
					// During level load, we want to pause
					// execution until we've finished loading
					// all of the netxcmds in our buffer.
					dontRun = false;
				}

				if (dontRun == false)
				{
					if (levelloading == true)
					{
						P_PostLoadLevel();
					}

					G_Ticker((gametic % NEWTICRATERATIO) == 0);
				}
				if (Playing() && netgame && (gametic % TICRATE == 0))
				{
					Schedule_Run();
				}

				gametic++;
				Consistancy(&consistancy[gametic%BACKUPTICS], gametic%BACKUPTICS);

				if (update_stats)
				{
					PS_STOP_TIMING(ps_tictime);
					PS_UpdateTickStats();
				}

				// Leave a certain amount of tics present in the net buffer as long as we've ran at least one tic this frame.
				if (client && gamestate == GS_LEVEL && leveltime > 1 && neededtic <= gametic + cv_netticbuffer.value)
				{
					break;
				}
			}
		}
	}
	else
	{
		if (realtics)
			hu_stopped = true;
	}


	return ticking;
}


/*	Ping Update except better:
	We call this once per second and check for people's pings. If their ping happens to be too high, we increment some timer and kick them out.
	If they're not lagging, decrement the timer by 1. Of course, reset all of this if they leave.
*/

static int32_t pingtimeout[MAXPLAYERS];

static inline void PingUpdate(void)
{
	int32_t i;
	dboolean laggers[MAXPLAYERS];
	uint8_t numlaggers = 0;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	memset(laggers, 0, sizeof(dboolean) * MAXPLAYERS);

	netbuffer->packettype = PT_PING;

	//check for ping limit breakage.
	if (cv_maxping.value)
	{
		for (i = 1; i < MAXPLAYERS; i++)
		{
			if (playeringame[i]
			&& (realpingtable[i] / pingmeasurecount > (unsigned)cv_maxping.value))
			{
				if (players[i].jointime > 30 * TICRATE)
					laggers[i] = true;
				numlaggers++;
			}
			else
				pingtimeout[i] = 0;
		}

		//kick lagging players... unless everyone but the server's ping sucks.
		//in that case, it is probably the server's fault.
		if (numlaggers < D_NumPlayers() - 1)
		{
			for (i = 1; i < MAXPLAYERS; i++)
			{
				if (playeringame[i] && laggers[i])
				{
					pingtimeout[i]++;

					// ok your net has been bad for too long, you deserve to die.
					if (pingtimeout[i] > cv_pingtimeout.value)
					{
						pingtimeout[i] = 0;
						SendKick(i, KICK_MSG_PING_HIGH);
					}
				}
				/*
					you aren't lagging,
					but you aren't free yet.
					In case you'll keep spiking,
					we just make the timer go back down. (Very unstable net must still get kicked).
				*/
				else
					pingtimeout[i] = (pingtimeout[i] == 0 ? 0 : pingtimeout[i]-1);
			}
		}
	}

	//make the ping packet and clear server data for next one
	for (i = 0; i < MAXPLAYERS; i++)
	{
		//CONS_Printf("player %d - total pings: %d\n", i, realpingtable[i]);

		netbuffer->u.netinfo.pingtable[i] = realpingtable[i] / pingmeasurecount;
		//server takes a snapshot of the real ping for display.
		//otherwise, pings fluctuate a lot and would be odd to look at.
		playerpingtable[i] = realpingtable[i] / pingmeasurecount;
		realpingtable[i] = 0; //Reset each as we go.

		uint32_t node = playernode[i];
		if (node < MAXNETNODES)
		{
			playerpacketlosstable[i] = 0;
			uint32_t totalpackets = 0;
			for (int32_t j = 0; j < PACKETLOSSCYCLES; j++)
			{
				playerpacketlosstable[i] += lostpackets[j][node];
				totalpackets += sentpackets[j][node];
			}
			if (totalpackets == 0)
				playerpacketlosstable[i] = 0;
		}

		netbuffer->u.netinfo.packetloss[i] = playerpacketlosstable[i];
		netbuffer->u.netinfo.delay[i] = playerdelaytable[i];
	}

	if (++plcycle >= PACKETLOSSCYCLES)
		plcycle = 0;
	for (int32_t node = 0; node < MAXNETNODES; node++)
	{
		lostpackets[plcycle][node] = 0;
		sentpackets[plcycle][node] = 0;
	}

	// send the server's maxping as last element of our ping table. This is useful to let us know when we're about to get kicked.
	netbuffer->u.netinfo.pingtable[MAXPLAYERS] = cv_maxping.value;

	//send out our ping packets
	for (i = 0; i < MAXNETNODES; i++)
		if (nodeingame[i])
			HSendPacket(i, true, 0, sizeof(netinfo_pak));

	pingmeasurecount = 0; //Reset count
}

static tic_t gametime = 0;

static void UpdatePingTable(void)
{
	tic_t fastest;
	tic_t slowest;
	tic_t lag;

	int32_t i;

	if (server)
	{
		if (Playing() && !(gametime % 8)) // Value chosen based on _my vibes man_
			PingUpdate();

		fastest = 0;
		slowest = 0;

		// update node latency values so we can take an average later.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && playernode[i] > 0)
			{
				// TicsToMilliseconds can't handle pings over 1000ms lol
				realpingtable[i] += GetLag(playernode[i]);

				if (!players[i].spectator)
				{
					lag = playerpingtable[i];
					if (! fastest || lag < fastest)
						fastest = lag;

					if (! slowest || lag > slowest)
						slowest = lag;
				}
			}
		}

		if (server_lagless == 1)
			lowest_lag = 0;
		else if (server_lagless == 0)
			lowest_lag = fastest;
		else if (server_lagless == -1)
			lowest_lag = slowest;

		// Don't gentleman below your mindelay
		if (lowest_lag < (tic_t)cv_mindelay.value)
			lowest_lag = (tic_t)cv_mindelay.value;

		pingmeasurecount++;

		switch (playerpernode[0])
		{
			case 4:
				playerdelaytable[nodetoplayer4[0]] = lowest_lag;
				/*FALLTHRU*/
			case 3:
				playerdelaytable[nodetoplayer3[0]] = lowest_lag;
				/*FALLTHRU*/
			case 2:
				playerdelaytable[nodetoplayer2[0]] = lowest_lag;
				/*FALLTHRU*/
			case 1:
				playerdelaytable[nodetoplayer[0]] = lowest_lag;
		}
	}
	else // We're a client, handle mindelay on the way out.
	{
		tic_t mydelay = playerpingtable[consoleplayer];

		if (mydelay < (tic_t)cv_mindelay.value)
			lowest_lag = cv_mindelay.value - mydelay;
		else
			lowest_lag = 0;
	}
}

// It's that time again! Send everyone a safe message to sign, so we can show off their signature and prove we're playing fair.
#if 0
static void SendChallenges(void)
{
	int i;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	netbuffer->packettype = PT_CHALLENGEALL;

	#ifdef DEVELOP
	if (cv_nochallenge.value)
	{
		CV_AddValue(&cv_nochallenge, -1);
		CONS_Alert(CONS_WARNING, "cv_nochallenge enabled, not sending PT_CHALLENGEALL\n");
		return;
	}
	#endif

	memset(knownWhenChallenged, 0, sizeof(knownWhenChallenged));
	memset(lastReceivedSignature, 0, sizeof(lastReceivedSignature));

	GenerateChallenge(netbuffer->u.challengeall.secret);
	memcpy(lastChallengeAll, netbuffer->u.challengeall.secret, sizeof(lastChallengeAll));

	// Take note of everyone's current key, so that players who disconnect and are replaced aren't held to the old player's challenge.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
			memcpy(knownWhenChallenged[i], players[i].public_key, sizeof(knownWhenChallenged[i]));
	}

	for (i = 0; i < MAXNETNODES; i++)
	{
		if (nodeingame[i])
			HSendPacket(i, true, 0, sizeof(challengeall_pak));
	}
}

// Before we start sending out the results, we need to kick everyone who didn't respond.
// (If we try to do both at once, clients will still see players who failled in-game when the results arrive...)
static void KickUnverifiedPlayers(void)
{
	int i;
	uint8_t allZero[SIGNATURELENGTH];
	memset(allZero, 0, SIGNATURELENGTH);

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;
		if (memcmp(lastReceivedSignature[i], allZero, SIGNATURELENGTH) == 0) // We never got a response!
		{
			if (memcmp(&knownWhenChallenged[i], &players[i].public_key, sizeof(knownWhenChallenged[i])) == 0)
			{
				if (playernode[i] != servernode)
				{
					SendKick(i, KICK_MSG_SIGFAIL);
				}
			}
		}
	}
}

//
static void SendChallengeResults(void)
{
	int i;
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);
	netbuffer->packettype = PT_RESULTSALL;

	#ifdef DEVELOP
	if (cv_noresults.value)
	{
		CV_AddValue(&cv_noresults, -1);
		CONS_Alert(CONS_WARNING, "cv_noresults enabled, not sending PT_RESULTSALL\n");
		return;
	}
	#endif

	uint8_t allZero[SIGNATURELENGTH];
	memset(allZero, 0, sizeof(allZero));

	memset(&netbuffer->u.resultsall, 0, sizeof(netbuffer->u.resultsall));

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;

		// Don't try to transmit signatures for players who didn't get here in time to send one.
		// (Everyone who had their chance should have been kicked by KickUnverifiedPlayers by now.)
		if (memcmp(lastReceivedSignature[i], allZero, SIGNATURELENGTH) == 0)
			continue;

		memcpy(netbuffer->u.resultsall.signature[i], lastReceivedSignature[i], sizeof(netbuffer->u.resultsall.signature[i]));
		#ifdef DEVELOP
		if (cv_badresults.value)
		{
			CV_AddValue(&cv_badresults, -1);
			CONS_Alert(CONS_WARNING, "cv_badresults enabled, scrubbing signature from PT_RESULTSALL\n");
			memset(netbuffer->u.resultsall.signature[i], 0, sizeof(netbuffer->u.resultsall.signature[i]));
		}
		#endif
	}

	for (i = 0; i < MAXNETNODES; i++)
	{
		if (nodeingame[i])
			HSendPacket(i, true, 0, sizeof(resultsall_pak));
	}
}

// Who should we try to verify when results come in?
// Store a public key for every active slot, so if players shuffle during challenge leniency,
// we don't incorrectly try to verify someone who didn't even get a challenge, throw a tantrum, and bail.
static void CheckPresentPlayers(void)
{
	int i;
	memset(knownWhenChallenged, 0, sizeof(knownWhenChallenged));

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
		{
			continue;
		}
		else
		{
			memcpy(knownWhenChallenged[i], players[i].public_key, sizeof(knownWhenChallenged[i]));
		}
	}
}

// Handle "client-to-client" auth challenge flow.
static void UpdateChallenges(void)
{
	if (!(Playing() && netgame))
		return;

	if (server)
	{
		if (leveltime == CHALLENGEALL_START)
			SendChallenges();

		if (leveltime == CHALLENGEALL_KICKUNRESPONSIVE)
			KickUnverifiedPlayers();

		if (leveltime == CHALLENGEALL_SENDRESULTS)
			SendChallengeResults();

	}
	else // client
	{
		if (leveltime <= CHALLENGEALL_START)
			expectChallenge = true;

		if (leveltime == CHALLENGEALL_START)
			CheckPresentPlayers();

		if (leveltime > CHALLENGEALL_CLIENTCUTOFF && expectChallenge)
			HandleSigfail("Didn't receive client signatures.");
	}
}
#endif

static void RenewHolePunch(void)
{
	static time_t past;

	const time_t now = time(NULL);

	if ((now - past) > 20)
	{
		I_NetRegisterHolePunch();
		past = now;
	}
}

// Handle timeouts to prevent definitive freezes from happenning
static void HandleNodeTimeouts(void)
{
	int32_t i;

	if (server)
	{
		for (i = 1; i < MAXNETNODES; i++)
			if ((nodeingame[i] || nodeneedsauth[i]) && freezetimeout[i] < I_GetTime())
				Net_ConnectionTimeout(i);

		// In case the cvar value was lowered
		if (joindelay)
			joindelay = min(joindelay - 1, 3 * (tic_t)cv_joindelay.value * TICRATE);
	}
}

// Keep the network alive use with care!
void NetKeepAlive_NoTImeToWait(void)
{
	UpdatePingTable();

	GetPackets();

#ifdef MASTERSERVER
	MasterClient_Ticker();
#endif

	if (netgame && serverrunning)
	{
		RenewHolePunch();
	}

	if (client)
	{
		// send keep alive
		CL_SendClientKeepAlive();
		// No need to check for resynch because we aren't running any tics
	}
	else
	{
		SV_SendServerKeepAlive();
	}

	// No else because no tics are being run and we can't resynch during this

	Net_AckTicker();
	HandleNodeTimeouts();
	FileSendTicker();
}

// Keep the network alive while not advancing tics!
void NetKeepAlive(void)
{
	tic_t nowtime;
	int32_t realtics;

	nowtime = I_GetTime();
	realtics = nowtime - gametime;

	// return if there's no time passed since the last call
	if (realtics <= 0) // nothing new to update
		return;

	NetKeepAlive_NoTImeToWait();
}

// If a tree falls in the forest but nobody is around to hear it, does it make a tic?
#define DEDICATEDIDLETIME (10*TICRATE)

void NetUpdate(void)
{
	static tic_t resptime = 0;
	tic_t nowtime;
	int32_t i;
	int32_t realtics;

	nowtime = I_GetTime();
	realtics = nowtime - gametime;

	if (realtics <= 0) // nothing new to update
		return;

	if (realtics > 5)
	{
		if (server)
			realtics = 1;
		else
			realtics = 5;
	}

#ifdef DEDICATEDIDLETIME
	if (server && dedicated && gamestate == GS_LEVEL)
	{
		static tic_t dedicatedidle = 0;

		for (i = 1; i < MAXNETNODES; ++i)
			if (nodeingame[i])
			{
				if (dedicatedidle == DEDICATEDIDLETIME)
				{
					CONS_Printf("DEDICATED: Awakening from idle (Node %d detected...)\n", i);
					dedicatedidle = 0;
				}
				break;
			}

		if (i == MAXNETNODES)
		{
			if (leveltime == 2)
			{
				// On next tick...
				dedicatedidle = DEDICATEDIDLETIME-1;
			}
			else if (dedicatedidle == DEDICATEDIDLETIME)
			{
				if (D_GetExistingTextcmd(gametic, 0) || D_GetExistingTextcmd(gametic+1, 0))
				{
					CONS_Printf("DEDICATED: Awakening from idle (Netxcmd detected...)\n");
					dedicatedidle = 0;
				}
				else
				{
					realtics = 0;
				}
			}
			else if ((dedicatedidle += realtics) >= DEDICATEDIDLETIME)
			{
				const char *idlereason = "at round start";
				if (leveltime > 3)
					idlereason = va("for %d seconds", dedicatedidle/TICRATE);

				CONS_Printf("DEDICATED: No nodes %s, idling...\n", idlereason);
				realtics = 0;
				dedicatedidle = DEDICATEDIDLETIME;
			}
		}
	}
#endif

	gametime = nowtime;

	UpdatePingTable();

	//UpdateChallenges();

	if (client)
		maketic = neededtic;

	Local_Maketic(realtics); // make local tic, and call menu?

	if (server)
		CL_SendClientCmd(); // send it

	GetPackets(); // get packet from client or from server

	// client send the command after a receive of the server
	// the server send before because in single player is beter

#ifdef MASTERSERVER
	MasterClient_Ticker(); // Acking the Master Server
#endif

	if (netgame && serverrunning)
	{
		RenewHolePunch();
	}

	if (client)
	{
		CL_SendClientCmd(); // Send tic cmd
	}
	else
	{
		if (!demo.playback && realtics > 0)
		{
			int32_t counts;

			hu_redownloadinggamestate = false;

			// Don't erase tics not acknowledged
			counts = realtics;

			firstticstosend = gametic;
			for (i = 0; i < MAXNETNODES; i++)
			{
				if (!nodeingame[i])
					continue;
				if (nettics[i] < firstticstosend)
					firstticstosend = nettics[i];
				if (maketic + counts >= nettics[i] + (BACKUPTICS - TICRATE))
					Net_ConnectionTimeout(i);
			}

			if (maketic + counts >= firstticstosend + BACKUPTICS)
				counts = firstticstosend+BACKUPTICS-maketic-1;

			for (i = 0; i < counts; i++)
				SV_Maketic(); // Create missed tics and increment maketic

			for (; tictoclear < firstticstosend; tictoclear++) // Clear only when acknowledged
				D_Clearticcmd(tictoclear);                    // Clear the maketic the new tic

			SV_SendTics();

			neededtic = maketic; // The server is a client too
		}
	}

	if (server)
	{
		for(i = 0; i < MAXPLAYERS; i++)
		{
			if (stop_spamming[i] > 0)
				stop_spamming[i]--;
		}
	}

	Net_AckTicker();
	HandleNodeTimeouts();

	nowtime /= NEWTICRATERATIO;

	if (nowtime > resptime)
	{
		resptime = nowtime;
#ifdef HAVE_THREADS
		I_lock_mutex(&m_menu_mutex);
#endif
		M_Ticker();
#ifdef HAVE_THREADS
		I_unlock_mutex(m_menu_mutex);
#endif
		CON_Ticker();
	}

	FileSendTicker();
}

/** Returns the number of players playing.
  * \return Number of players. Can be zero if we're running a ::dedicated
  *         server.
  * \author Graue <graue@oceanbase.org>
  */
int32_t D_NumPlayers(void)
{
	int32_t num = 0, ix;

	for (ix = 0; ix < MAXPLAYERS; ix++)
	{
		if (playeringame[ix] && !players[ix].bot)
		{
			num++;
		}
	}

	return num;
}

/** Return whether a player is a real person (not a CPU) and not spectating.
  */
dboolean D_IsPlayerHumanAndGaming (int32_t player_number)
{
	player_t * player = &players[player_number];
	return (
			playeringame[player_number] &&
			! player->spectator &&
			! player->bot
	);
}

tic_t GetLag(int32_t node)
{
	// If the client has caught up to the server -- say, during a wipe -- lag is meaningless.
	if (nettics[node] > gametic)
		return 0;
	return gametic - nettics[node];
}

#define REWIND_POINT_INTERVAL 4*TICRATE + 16
rewind_t *netrewindhead;

void CL_ClearRewinds(void)
{
	rewind_t *head;
	while ((head = netrewindhead))
	{
		netrewindhead = netrewindhead->next;
		free(head);
	}
}

rewind_t *CL_SaveRewindPoint(size_t demopos)
{
	savebuffer_t save = {0};
	rewind_t *rewind;

	if (netrewindhead && netrewindhead->leveltime + REWIND_POINT_INTERVAL > leveltime)
		return NULL;

	rewind = (rewind_t *)malloc(sizeof (rewind_t));
	if (!rewind)
		return NULL;

	P_SaveBufferFromExisting(&save, rewind->savebuffer, NETSAVEGAMESIZE);
	P_SaveNetGame(&save, false);

	rewind->leveltime = leveltime;
	rewind->next = netrewindhead;
	rewind->demopos = demopos;
	netrewindhead = rewind;

	return rewind;
}

rewind_t *CL_RewindToTime(tic_t time)
{
	savebuffer_t save = {0};
	rewind_t *rewind;

	while (netrewindhead && netrewindhead->leveltime > time)
	{
		rewind = netrewindhead->next;
		free(netrewindhead);
		netrewindhead = rewind;
	}

	if (!netrewindhead)
		return NULL;

	P_SaveBufferFromExisting(&save, netrewindhead->savebuffer, NETSAVEGAMESIZE);
	P_LoadNetGame(&save, false);

	wipegamestate = gamestate; // No fading back in!
	timeinmap = leveltime;

	return netrewindhead;
}

#undef CloneConsistancy

void D_SHA256PasswordPass(const uint8_t *buffer, size_t len, const char *salt, uint8_t dest[static 32])
{
	uint8_t *tmpbuf;
	const size_t sl = strlen(salt);

	if (len > 256-sl)
		len = 256-sl;

	tmpbuf = Z_Malloc(sizeof(char) * (sl + len), PU_STATIC, NULL);
	memcpy(tmpbuf, buffer, len);
	memmove(&tmpbuf[len], salt, sl);

	lonesha256(dest, tmpbuf, len+sl);
	Z_Free(tmpbuf);
}

// Want to say something? XD_SAY is server only, gotta request that they send one on our behalf
void DoSayPacket(int8_t target, uint8_t flags, uint8_t source, char *message)
{
	doomdata_t *netbuffer = DOOMCOM_DATA(doomcom);

	say_pak *packet = (void*)&netbuffer->u.say;
	netbuffer->packettype = PT_SAY;

	memset(packet->message, 0, sizeof(packet->message));
	strcpy(packet->message, message);

	packet->source = source;
	packet->flags = flags;
	packet->target = target;

	HSendPacket(servernode, false, 0, sizeof(say_pak));
}

void DoSayPacketFromCommand(int8_t target, size_t usedargs, uint8_t flags)
{
	char msg[HU_MAXMSGLEN + 1];
	size_t numwords, ix;
	const size_t msgspace = sizeof msg;

	numwords = COM_Argc() - usedargs;
	I_Assert(numwords > 0);

	msg[0] = '\0';

	for (ix = 0; ix < numwords; ix++)
	{
		if (ix > 0)
			strlcat(msg, " ", msgspace);
		strlcat(msg, COM_Argv(ix + usedargs), msgspace);
	}

	DoSayPacket(target, flags, consoleplayer, msg);
}

// This is meant to be targeted at player indices, not whatever the hell XD_SAY is doing with 1-indexed players.
void SendServerNotice(int8_t target, char *message)
{
	if (client)
		return;
	DoSayCommand(message, target + 1, HU_PRIVNOTICE, servernode);
}
