// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2020 by Sally "TehRealSalt" Cochenour.
// Copyright (C) 2018-2020 by Kart Krew.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  discord.h
/// \brief Discord Rich Presence handling

#ifdef HAVE_DISCORDRPC

#include <time.h>

#include "i_system.h"
#include "d_clisrv.h"
#include "d_netcmd.h"
#include "i_net.h"
#include "g_game.h"
#include "p_tick.h"
#include "m_menu.h" // M_ClearMenus
#include "r_things.h" // skins
#include "mserv.h" // cv_advertise
#include "z_zone.h"
#include "byteptr.h"
#include "stun.h"
#include "i_tcp.h" // current_port
#include "k_grandprix.h"

#include "discord.h"
#include "doomdef.h"

// Feel free to provide your own, if you care enough to create another Discord app for this :P
#define DISCORD_APPID "1364963317784776714"

// length of IP strings
#define IP_SIZE 21

static void Discordrp_OnChange(void);

consvar_t cv_discordrp = CVAR_INIT ("discordrp", "On", CV_SAVE|CV_CALL, CV_OnOff, Discordrp_OnChange);
consvar_t cv_discordstreamer = CVAR_INIT ("discordstreamer", "Off", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_discordasks = CVAR_INIT ("discordasks", "Yes", CV_SAVE|CV_CALL, CV_YesNo, DRPC_UpdatePresence);

struct discordInfo_s discordInfo;

discordRequest_t *discordRequestList = NULL;

size_t g_discord_skins = 0;

static char self_ip[IP_SIZE];

/*--------------------------------------------------
 *	const char *DRPC_HideUsername(const char *input)
 *
 *		See header file for description.
 * --------------------------------------------------*/
const char *DRPC_HideUsername(const char *input)
{
	static char buffer[5];
	int i;

	buffer[0] = input[0];

	for (i = 1; i < 4; ++i)
	{
		buffer[i] = '.';
	}

	buffer[4] = '\0';
	return buffer;
}

dboolean drpc_init = false;

/*--------------------------------------------------
	static char *DRPC_XORIPString(const char *input)

		Simple XOR encryption/decryption. Not complex or
		very secretive because we aren't sending anything
		that isn't easily accessible via our Master Server anyway.
--------------------------------------------------*/
static char *DRPC_XORIPString(const char *input)
{
	const uint8_t xor[IP_SIZE] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
	char *output = malloc(sizeof(char) * (IP_SIZE+1));
	uint8_t i;

	for (i = 0; i < IP_SIZE; i++)
	{
		char xorinput;

		if (!input[i])
			break;

		xorinput = input[i] ^ xor[i];

		if (xorinput < 32 || xorinput > 126)
		{
			xorinput = input[i];
		}

		output[i] = xorinput;
	}

	output[i] = '\0';

	return output;
}

/*--------------------------------------------------
	static void DRPC_HandleReady(const DiscordUser *user)

		Callback function, ran when the game connects to Discord.

	Input Arguments:-
		user - Struct containing Discord user info.

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleReady(const DiscordUser *user)
{
	if (cv_discordstreamer.value)
	{
		CONS_Printf("Discord: connected to %s\n", DRPC_HideUsername(user->username));
	}
	else
	{
		CONS_Printf("Discord: connected to %s (%s)\n", user->username, user->userId);
	}
}

/*--------------------------------------------------
	static void DRPC_HandleDisconnect(int err, const char *msg)

		Callback function, ran when disconnecting from Discord.

	Input Arguments:-
		err - Error type
		msg - Error message

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleDisconnect(int err, const char *msg)
{
	CONS_Printf("Discord: disconnected (%d: %s)\n", err, msg);
}

/*--------------------------------------------------
	static void DRPC_HandleError(int err, const char *msg)

		Callback function, ran when Discord outputs an error.

	Input Arguments:-
		err - Error type
		msg - Error message

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleError(int err, const char *msg)
{
	CONS_Alert(CONS_WARNING, "Discord error (%d: %s)\n", err, msg);
}

/*--------------------------------------------------
	static void DRPC_HandleJoin(const char *secret)

		Callback function, ran when Discord wants to
		connect a player to the game via a channel invite
		or a join request.

	Input Arguments:-
		secret - Value that links you to the server.

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleJoin(const char *secret)
{
	char *ip = DRPC_XORIPString(secret);
	CONS_Printf("Connecting to %s via Discord\n", ip);
	M_ClearMenus(true); //Don't have menus open during connection screen
	if (demo.playback && demo.title)
		G_CheckDemoStatus(); //Stop the title demo, so that the connect command doesn't error if a demo is playing
	COM_BufAddText(va("connect \"%s\"\n", ip));
	free(ip);
}

/*--------------------------------------------------
	static dboolean DRPC_InvitesAreAllowed(void)

		Determines whenever or not invites or
		ask to join requests are allowed.

	Input Arguments:-
		None

	Return:-
		true if invites are allowed, false otherwise.
--------------------------------------------------*/
static dboolean DRPC_InvitesAreAllowed(void)
{
	if (!Playing())
	{
		// We're not playing, so we should not be getting invites.
		return false;
	}

	if (cv_discordasks.value == 0)
	{
		// Client has the CVar set to off, so never allow invites from this client.
		return false;
	}

	if (discordInfo.joinsAllowed == true)
	{
		if (discordInfo.everyoneCanInvite == true)
		{
			// Everyone's allowed!
			return true;
		}
		else if (consoleplayer == serverplayer || IsPlayerAdmin(consoleplayer))
		{
			// Only admins are allowed!
			return true;
		}
	}

	// Did not pass any of the checks
	return false;
}

/*--------------------------------------------------
	static void DRPC_HandleJoinRequest(const DiscordUser *requestUser)

		Callback function, ran when Discord wants to
		ask the player if another Discord user can join
		or not.

	Input Arguments:-
		requestUser - DiscordUser struct for the user trying to connect.

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleJoinRequest(const DiscordUser *requestUser)
{
	discordRequest_t *append = discordRequestList;
	discordRequest_t *newRequest;

	if (DRPC_InvitesAreAllowed() == false)
	{
		// Something weird happened if this occurred...
		Discord_Respond(requestUser->userId, DISCORD_REPLY_IGNORE);
		return;
	}

	newRequest = Z_Calloc(sizeof(discordRequest_t), PU_STATIC, NULL);

	newRequest->username = Z_Calloc(344, PU_STATIC, NULL);
	snprintf(newRequest->username, 344, "%s", requestUser->username);

#if 0
	newRequest->discriminator = Z_Calloc(8, PU_STATIC, NULL);
	snprintf(newRequest->discriminator, 8, "%s", requestUser->discriminator);
#endif

	newRequest->userID = Z_Calloc(32, PU_STATIC, NULL);
	snprintf(newRequest->userID, 32, "%s", requestUser->userId);

	if (append != NULL)
	{
		discordRequest_t *prev = NULL;

		while (append != NULL)
		{
			// CHECK FOR DUPES!! Ignore any that already exist from the same user.
			if (fastcmp(newRequest->userID, append->userID))
			{
				Discord_Respond(newRequest->userID, DISCORD_REPLY_IGNORE);
				DRPC_RemoveRequest(newRequest);
				return;
			}

			prev = append;
			append = append->next;
		}

		newRequest->prev = prev;
		prev->next = newRequest;
	}
	else
	{
		discordRequestList = newRequest;
		M_RefreshPauseMenu();
	}

	// Made it to the end, request was valid, so play the request sound :)
	S_StartSound(NULL, sfx_requst);
}

/*--------------------------------------------------
	void DRPC_RemoveRequest(discordRequest_t *removeRequest)

		See header file for description.
--------------------------------------------------*/
void DRPC_RemoveRequest(discordRequest_t *removeRequest)
{
	if (removeRequest->prev != NULL)
	{
		removeRequest->prev->next = removeRequest->next;
	}

	if (removeRequest->next != NULL)
	{
		removeRequest->next->prev = removeRequest->prev;

		if (removeRequest == discordRequestList)
		{
			discordRequestList = removeRequest->next;
		}
	}
	else
	{
		if (removeRequest == discordRequestList)
		{
			discordRequestList = NULL;
		}
	}

	Z_Free(removeRequest->username);
#if 0
	Z_Free(removeRequest->discriminator);
#endif
	Z_Free(removeRequest->userID);
	Z_Free(removeRequest);
}

#ifdef _DEBUG
static dboolean comregistered = false;
static void COM_DiscordTest_f(void)
{
	DiscordUser test = {
		.username = "Jeffma Balls",
		.discriminator = "6942",
		.userId = "69420694206942069",
		.avatar = NULL, // doesn't matter
	};
	DRPC_HandleJoinRequest(&test);
}
#endif

/*--------------------------------------------------
	void DRPC_Init(void)

		See header file for description.
--------------------------------------------------*/
void DRPC_Init(void)
{
	if (drpc_init || !cv_discordrp.value) return;

	drpc_init = true;
	DiscordEventHandlers handlers;
	memset(&handlers, 0, sizeof(handlers));

	handlers.ready = DRPC_HandleReady;
	handlers.disconnected = DRPC_HandleDisconnect;
	handlers.errored = DRPC_HandleError;
	handlers.joinGame = DRPC_HandleJoin;
	handlers.joinRequest = DRPC_HandleJoinRequest;

	Discord_Initialize(DISCORD_APPID, &handlers, 1, NULL);
	I_AddExitFunc(DRPC_Shutdown);
	DRPC_UpdatePresence();

#ifdef _DEBUG
	if (!comregistered)
	{
		COM_AddCommand("discordtest", COM_DiscordTest_f);
		comregistered = true;
	}
#endif
}

void DRPC_Shutdown(void)
{
	if (!drpc_init)
	{
		CONS_Printf("DiscordRPC never started\n");
		return;
	}

	CONS_Printf("Shutting down DiscordRPC\n");

	Discord_Shutdown();
	drpc_init = false;
}

static void Discordrp_OnChange(void)
{
	if (cv_discordrp.value)
	{
		DRPC_UpdatePresence();
	}
	else
	{
		DRPC_Shutdown();
	}
}

/*--------------------------------------------------
	static void DRPC_GotServerIP(uint32_t address)

		Callback triggered by successful STUN response.

	Input Arguments:-
		address - IPv4 address of this machine, in network byte order.

	Return:-
		None
--------------------------------------------------*/
static void DRPC_GotServerIP(uint32_t address)
{
	const unsigned char * p = (const unsigned char *)&address;
	snprintf(self_ip, IP_SIZE, "%u.%u.%u.%u:%u", p[0], p[1], p[2], p[3], current_port);
	DRPC_UpdatePresence();
}

/*--------------------------------------------------
	static const char *DRPC_GetServerIP(void)

		Retrieves the IP address of the server that you're
		connected to. Will attempt to use curl for getting your
		own IP address, if it's not yours.
--------------------------------------------------*/
static const char *DRPC_GetServerIP(void)
{
	const char *address;

	// If you're connected
	if (I_GetNodeAddress && (address = I_GetNodeAddress(servernode)) != NULL)
	{
		if (!fastcmp(address, "self"))
		{
			// We're not the server, so we could successfully get the IP!
			// No need to do anything else :)
			return address;
		}
	}

	if (self_ip[0])
	{
		return self_ip;
	}
	else
	{
		// There happens to be a good way to get it after all! :D
		STUN_bind(DRPC_GotServerIP);
		return NULL;
	}
}

/*--------------------------------------------------
	void DRPC_EmptyRequests(void)

		Empties the request list. Any existing requests
		will get an ignore reply.
--------------------------------------------------*/
static void DRPC_EmptyRequests(void)
{
	while (discordRequestList != NULL)
	{
		Discord_Respond(discordRequestList->userID, DISCORD_REPLY_IGNORE);
		DRPC_RemoveRequest(discordRequestList);
	}
}

/*--------------------------------------------------
	void DRPC_UpdatePresence(void)

		See header file for description.
--------------------------------------------------*/
void DRPC_UpdatePresence(void)
{
	if (!cv_discordrp.value) return;

	if (!drpc_init) DRPC_Init();

	char detailstr[48+1];

	char mapname[5+21+21+2+1];
	char mapimg[MAXMAPLUMPNAME+5];

	char charimg[4+SKINNAMESIZE+2];
	char charname[11+SKINNAMESIZE+1];

	dboolean joinSecretSet = false;

	DiscordRichPresence discordPresence;
	memset(&discordPresence, 0, sizeof(discordPresence));

	if (dedicated)
	{
		return;
	}

	if (!cv_discordrp.value)
	{
		// User doesn't want to show their game information, so update with empty presence.
		// This just shows that they're playing SRB2Kart. (If that's too much, then they should disable game activity :V)
		DRPC_EmptyRequests();
		Discord_UpdatePresence(&discordPresence);
		return;
	}

#ifdef DEVELOP
	// This way, we can use the invite feature in-dev, but not have snoopers seeing any potential secrets! :P
	discordPresence.largeImageKey = "miscdevelop";
	discordPresence.largeImageText = "No peeking!";
	discordPresence.state = "Development EXE";

	DRPC_EmptyRequests();
	Discord_UpdatePresence(&discordPresence);
	return;
#endif // DEVELOP

	// Server info
	if (netgame)
	{
		if (DRPC_InvitesAreAllowed() == true)
		{
			const char *join;

			// Grab the host's IP for joining.
			if ((join = DRPC_GetServerIP()) != NULL)
			{
				discordPresence.joinSecret = DRPC_XORIPString(join);
				joinSecretSet = true;
			}
			else
			{
				return;
			}
		}

		if (cv_advertise.value)
		{
			discordPresence.state = "Public";
		}
		else
		{
			discordPresence.state = "Private";
		}

		discordPresence.partyId = server_context; // Thanks, whoever gave us Mumble support, for implementing the EXACT thing Discord wanted for this field!
		discordPresence.partySize = D_NumPlayers(); // Players in server
		discordPresence.partyMax = discordInfo.maxPlayers; // Max players
	}
	else
	{
		// Reset discord info if you're not in a place that uses it!
		// Important for if you join a server that compiled without HAVE_DISCORDRPC,
		// so that you don't ever end up using bad information from another server.
		memset(&discordInfo, 0, sizeof(discordInfo));

		// Offline info
		if (Playing())
		{
			if (grandprixinfo.gp == true)
			{
				discordPresence.state = "Grand Prix";
			}
			else
			{
				discordPresence.state = "Offline";
			}
		}
		else if (demo.playback && !demo.title)
			discordPresence.state = "Watching Replay";
		else
			discordPresence.state = "Menu";
	}

	// Gametype info
	if ((gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_VOTING) && Playing())
	{
		if (modeattacking)
		{
			const char *realname = G_GetRecordPresetName(currentrecordpreset);
			discordPresence.details = va("Time Attack: %s", realname ? realname : "Unknown Mode");
		}
		else
		{
			if (grandprixinfo.gp == true)
			{
				snprintf(detailstr, 48, "%s%s%s",
					gametypes[gametype]->name,
					va(" | %s", cv_dummygpdifficulty.string),
					(encoremode == true) ? " | Encore" : ""
				);
			}
			else
			{
				snprintf(detailstr, 48, "%s%s%s",
					gametypes[gametype]->name,
					va(" | %s", kartspeed_cons_t[gamespeed+1].strvalue),
					(encoremode == true) ? " | Encore" : ""
				);
			}
			discordPresence.details = detailstr;
		}
	}

	if ((gamestate == GS_LEVEL || gamestate == GS_INTERMISSION) // Map info
		&& !(demo.playback && demo.title))
	{
		if (mapheaderinfo[gamemap-1]->menuflags & LF2_HIDEINMENU)
		{
			// Hell map, use the method that got you here :P
			discordPresence.largeImageKey = "miscdice";
		}
		else
		{
			// Supported map names
			// I hate that this is easiest but oh well.
			static const char *supportedMaps[] = {
				// base game
				"MAP01",
				"MAP02",
				"MAP03",
				"MAP04",
				"MAP05",
				"MAP06",
				"MAP07",
				"MAP08",
				"MAP09",
				"MAP10",
				"MAP11",
				"MAP12",
				"MAP13",
				"MAP14",
				"MAP15",
				"MAP16",
				"MAP17",
				"MAP18",
				"MAP19",
				"MAP20",
				"MAP21",
				"MAP22",
				"MAP23",
				"MAP24",
				"MAP25",
				"MAP26",
				"MAP27",
				"MAP28",
				"MAP29",
				"MAP30",
				"MAP31",
				"MAP32",
				"MAP33",
				"MAP34",
				"MAP35",
				"MAP36",
				"MAP37",
				"MAP38",
				"MAP39",
				"MAP40",
				"MAP41",
				"MAP42",
				"MAP43",
				"MAP44",
				"MAP45",
				"MAP46",
				"MAP47",
				"MAP48",
				"MAP49",
				"MAP50",
				"MAP51",
				"MAP52",
				"MAP53",
				"MAP54",
				"MAP55",
				"MAP56",
				"MAP57",
				"MAP58",
				"MAP59",
				"MAP60",
				"MAPB0",
				"MAPB1",
				"MAPB2",
				"MAPB3",
				"MAPB4",
				"MAPB5",
				"MAPB6",
				"MAPB7",
				"MAPB8",
				"MAPB9",
				"MAPBA",
				"MAPBB",
				"MAPBC",
				"MAPBD",
				"MAPBE",
				"MAPBF",
				"MAPBG",
				"MAPBH",
				"MAPBI",
				"MAPBJ",
				"MAPBK",
				"MAPBL",
				"MAPBM",
				"MAPBN",
				"MAPBO",
				"MAPBP",
				"MAPBQ",
				"MAPBR",
				"MAPBS",
				NULL
			};

			dboolean customMap = true;
			uint16_t checkMap = 0;

			// Map image
			while (supportedMaps[checkMap] != NULL)
			{
				if (fastcmp(mapheaderinfo[gamemap-1]->lumpname, supportedMaps[checkMap]))
				{
					snprintf(mapimg, MAXMAPLUMPNAME+4, "map_%s", mapheaderinfo[gamemap-1]->lumpname);
					strlwr(mapimg);
					discordPresence.largeImageKey = mapimg; // Map image
					customMap = false;
					break;
				}

				checkMap++;
			}

			if (customMap == true)
			{
				// This is probably a custom map!
				discordPresence.largeImageKey = "mapcustom";
			}
		}

		if (mapheaderinfo[gamemap-1]->menuflags & LF2_HIDEINMENU)
		{
			// Hell map, hide the name
			discordPresence.largeImageText = "Map: ???";
		}
		else
		{
			// Map name on tool tip
			char *title = G_BuildMapTitle(gamemap);
			snprintf(mapname, 48, "Map: %s", title);
			Z_Free(title);
			discordPresence.largeImageText = mapname;
		}

		if (gamestate == GS_LEVEL && Playing())
		{
			const time_t currentTime = time(NULL);
			const time_t mapTimeStart = currentTime - ((leveltime + (modeattacking ? starttime : 0)) / TICRATE);

			discordPresence.startTimestamp = mapTimeStart;

			if (timelimitintics > 0)
			{
				const time_t mapTimeEnd = mapTimeStart + ((timelimitintics + starttime + 1) / TICRATE);
				discordPresence.endTimestamp = mapTimeEnd;
			}
		}
	}
	else if (gamestate == GS_VOTING)
	{
		discordPresence.largeImageKey = ((gametype == GT_BATTLE) ? "miscredplanet" : "miscblueplanet");
		discordPresence.largeImageText = "Voting";
	}
	else
	{
		discordPresence.largeImageKey = "misctitle";
		discordPresence.largeImageText = "Title Screen";
	}

	// Character info
	if (Playing() && playeringame[consoleplayer] && !players[consoleplayer].spectator)
	{
		// Character image
		if ((unsigned)players[consoleplayer].skin < g_discord_skins) // Supported skins
		{
			snprintf(charimg, 22, "char_%s", skins[ players[consoleplayer].skin ]->name);
			discordPresence.smallImageKey = charimg;
		}
		else
		{
			// Use the custom character icon!
			discordPresence.smallImageKey = "charcustom";
		}

		snprintf(charname, 28, "Character: %s", skins[players[consoleplayer].skin]->realname);
		discordPresence.smallImageText = charname; // Character name
	}

	if (joinSecretSet == false)
	{
		// Not able to join? Flush the request list, if it exists.
		DRPC_EmptyRequests();
	}

	Discord_UpdatePresence(&discordPresence);
}

#endif // HAVE_DISCORDRPC
