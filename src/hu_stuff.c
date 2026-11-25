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
/// \file  hu_stuff.c
/// \brief Heads up display

#include "command.h"
#include "doomdef.h"
#include "byteptr.h"
#include "hu_stuff.h"
#include "font.h"

#include "k_grandprix.h"
#include "m_menu.h" // gametype_cons_t
#include "m_cond.h" // emblems
#include "m_misc.h" // word jumping
#include "m_emotes.h"

#include "d_clisrv.h"

#include "g_game.h"
#include "g_input.h"

#include "i_video.h"
#include "i_system.h"

#include "st_stuff.h" // ST_HEIGHT
#include "r_local.h"

#include "keys.h"
#include "v_video.h"

#include "w_wad.h"
#include "z_zone.h"

#include "console.h"
#include "am_map.h"
#include "d_main.h"

#include "p_local.h" // camera[]
#include "p_tick.h"

#include "m_textinput.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

#include "lua_hud.h"
#include "lua_hudlib_drawlist.h"
#include "lua_hook.h"

// SRB2Kart
#include "s_sound.h" // song credits
#include "k_kart.h"
#include "k_boss.h"
#include "k_color.h"
#include "k_hud.h"
#include "r_fps.h"
#include "k_hud_message.h"

// coords are scaled
#define HU_INPUTX 0
#define HU_INPUTY 0

//-------------------------------------------
//              heads up font
//-------------------------------------------

// ping font
// Note: I'd like to adress that at this point we might *REALLY* want to work towards a common drawString function that can take any font we want because this is really turning into a MESS. :V -Lat'
patch_t *pinggfx[5];	// small ping graphic
patch_t *mping[5]; // smaller ping graphic
patch_t *pingmeasure[2]; // ping measurement graphic
patch_t *pinglocal[2]; // mindelay indecator

patch_t *framecounter;
patch_t *frameslash;	// framerate stuff. Used in screen.c

patch_t *srb2back = NULL;

static player_t *plr;
dboolean hu_keystrokes; // :)
dboolean chat_on; // entering a chat message?
dboolean wants_demo_save; // Wants to save a demo?
dboolean holding_demo_save;
static char w_chat_buf[HU_MAXMSGLEN + 1];
static textinput_t w_chat;
static dboolean headsupactive = false;
dboolean hu_showscores; // draw rankings
static char hu_tick;
static tic_t hu_emoteanim = 0;
emote_autocomplete_t emote_autocomplete = {0};

//-------------------------------------------
//              misc vars
//-------------------------------------------

patch_t *missingpat;
patch_t *blanklvl;
patch_t *randomlvl;
patch_t *nolvl;

// song credits
static patch_t *songcreditbg;

// -------
// protos.
// -------
static void HU_DrawRankings(void);

// stuff from SRB2-Banpyura thx alot! :3
consvar_t cv_chat_xoffset = CVAR_INIT ("chat_xoffset", "0", CV_SAVE, CV_Signed, NULL);
consvar_t cv_chat_yoffset = CVAR_INIT ("chat_yoffset", "0", CV_SAVE, CV_Signed, NULL);
consvar_t cv_chat_showlimit = CVAR_INIT ("chat_showlimit", "Yes", CV_SAVE, CV_YesNo, NULL);

//======================================================================
//                 KEYBOARD LAYOUTS FOR ENTERING TEXT
//======================================================================

static char cechotext[1024];
static tic_t cechotimer = 0;
static tic_t cechoduration = 5*TICRATE;
static int32_t cechoflags = 0;

static tic_t resynch_ticker = 0;

static huddrawlist_h luahuddrawlist_scores = NULL;

//======================================================================
//                          HEADS UP INIT
//======================================================================

// just after
static void Command_Say_f(void);
static void Command_Sayto_f(void);
static void Command_Sayteam_f(void);
static void Command_CSay_f(void);
static void Command_Shout(void);
static void Got_Saycmd(uint8_t **p, int32_t playernum);

void HU_LoadGraphics(void)
{
	int32_t i;

	if (dedicated)
		return;

	Font_Load();

	HU_UpdatePatch(&blanklvl, "BLANKLVL");
	HU_UpdatePatch(&randomlvl, "RANDOMLV");
	HU_UpdatePatch(&nolvl, "M_NOLVL");

	HU_UpdatePatch(&songcreditbg, "K_SONGCR");

	// cache ping gfx:
	for (i = 0; i < 5; i++)
	{
		HU_UpdatePatch(&pinggfx[i], "PINGGFX%d", i+1);
		HU_UpdatePatch(&mping[i], "MPING%d", i+1);
	}

	HU_UpdatePatch(&pingmeasure[0], "PINGD");
	HU_UpdatePatch(&pingmeasure[1], "PINGMS");

	HU_UpdatePatch(&pinglocal[0], "PINGGFXL");
	HU_UpdatePatch(&pinglocal[1], "MPINGL");

	// fps stuff
	HU_UpdatePatch(&framecounter, "FRAMER");
	HU_UpdatePatch(&frameslash, "FRAMESL");

	HU_UpdatePatch(&srb2back, "SRB2BACK");
}

// Initialise Heads up
// once at game startup.
//
void HU_Init(void)
{
	font_t font;

	COM_AddCommand("say", Command_Say_f);
	COM_AddCommand("sayto", Command_Sayto_f);
	COM_AddCommand("sayteam", Command_Sayteam_f);
	COM_AddCommand("csay", Command_CSay_f);
	COM_AddCommand("shout", Command_Shout);
	RegisterNetXCmd(XD_SAY, Got_Saycmd);

	// only allocate if not present, to save us a lot of headache
	if (missingpat == NULL)
	{
		lumpnum_t missingnum = W_GetNumForName("MISSING");
		if (missingnum == LUMPERROR)
			I_Error("HU_LoadGraphics: \"MISSING\" patch not present in resource files.");

		missingpat = W_CachePatchNum(missingnum, PU_STATIC);
	}

	/*
	Setup fonts
	*/

	if (!dedicated)
	{
#define  DIM( s, n ) ( font.start = s, font.size = n )
#define ADIM( name )        DIM (name ## _FONTSTART, name ## _FONTSIZE)
#define   PR( s )           strcpy(font.prefix, s)
#define  DIG( n )           ( font.digits = n )
#define  REG                Font_DumbRegister(&font)

		DIG  (3);

		ADIM (HU);

		PR   ("STCFN");
		REG;

		PR   ("TNYFN");
		REG;

		ADIM (KART);
		PR   ("MKFNT");
		REG;

		ADIM (LT);
		PR   ("LTFNT");
		REG;

		ADIM (CRED);
		PR   ("CRFNT");
		REG;

		DIG  (3);

		ADIM (LT);

		PR   ("GTOL");
		REG;

		PR   ("GTFN");
		REG;

		DIG  (1);

		DIM  (0, 10);

		PR   ("STTNUM");
		REG;

		PR   ("NGTNUM");
		REG;

		PR   ("PINGN");
		REG;

#undef  REG
#undef  DIG
#undef  PR
#undef  ADMIN
#undef  DIM
	}

	if (dedicated || rendermode == render_none)
		return;

	HU_LoadGraphics();

	luahuddrawlist_scores = LUA_HUD_CreateDrawList();
}

patch_t *HU_UpdateOrBlankPatch(patch_t **user, dboolean required, const char *format, ...)
{
	va_list ap;
	char buffer[9];

	lumpnum_t lump = INT16_MAX;
	patch_t *patch;

	va_start (ap, format);
	vsnprintf(buffer, sizeof buffer, format, ap);
	va_end   (ap);

	if (user && partadd_earliestfile != UINT16_MAX)
	{
		uint16_t fileref = numwadfiles;
		lump = INT16_MAX;

		while ((lump == INT16_MAX) && ((--fileref) >= partadd_earliestfile))
		{
			lump = W_CheckNumForNamePwad(buffer, fileref, 0);
		}

		/* no update in this wad */
		if (fileref < partadd_earliestfile)
			return *user;

		CONS_Printf("pe = %d, fr = %d\n", partadd_earliestfile, fileref);

		lump |= (fileref << 16);
	}
	else
	{
		lump = W_CheckNumForName(buffer);

		if (lump == LUMPERROR)
		{
			if (required == true)
				*user = missingpat;

			return *user;
		}
	}

	patch = W_CachePatchNum(lump, PU_HUDGFX);

	if (user)
	{
		if (*user)
			Patch_Free(*user);

		*user = patch;
	}

	return patch;
}

static inline void HU_Stop(void)
{
	headsupactive = false;
}

//
// Reset Heads up when consoleplayer spawns
//
void HU_Start(void)
{
	if (headsupactive)
		HU_Stop();

	plr = &players[consoleplayer];

	headsupactive = true;
}

//======================================================================
//                            EXECUTION
//======================================================================


// EVERY CHANGE IN THIS SCRIPT IS LOL XD! BY VINCYTM

static uint32_t chat_nummsg_log = 0;
static uint32_t chat_nummsg_min = 0;
static uint32_t chat_scroll = 0;
static tic_t chat_scrolltime = 0;

static uint32_t chat_maxscroll = 0; // how far can we scroll?

//static chatmsg_t chat_mini[CHAT_BUFSIZE]; // Display the last few messages sent.
//static chatmsg_t chat_log[CHAT_BUFSIZE]; // Keep every message sent to us in memory so we can scroll n shit, it's cool.

static char chat_log[CHAT_BUFSIZE][255]; // hold the last 48 or so messages in that log.
static char chat_mini[8][255]; // display up to 8 messages that will fade away / get overwritten
static tic_t chat_timers[8];

static dboolean chat_scrollmedown = false; // force instant scroll down on the chat log. Happens when you open it / send a message.

// remove text from minichat table

static int16_t addy = 0; // use this to make the messages scroll smoothly when one fades away

static void HU_removeChatText_Mini(void)
{
	// MPC: Don't create new arrays, just iterate through an existing one
	size_t i;
	for(i=0;i<chat_nummsg_min-1;i++) {
		strcpy(chat_mini[i], chat_mini[i+1]);
		chat_timers[i] = chat_timers[i+1];
	}
	chat_nummsg_min--; // lost 1 msg.

	// use addy and make shit slide smoothly af.
	addy += (vid.width < 640) ? 8 : 6;

}

// same but w the log. TODO: optimize this and maybe merge in a single func? im bad at C.
static void HU_removeChatText_Log(void)
{
	// MPC: Don't create new arrays, just iterate through an existing one
	size_t i;
	for(i=0;i<chat_nummsg_log-1;i++) {
		strcpy(chat_log[i], chat_log[i+1]);
	}

	chat_nummsg_log--; // lost 1 msg.
}

void HU_AddChatText(const char *text, dboolean playsound)
{
	if (playsound && cv_consolechat.value != 2)	// Don't play the sound if we're using hidden chat.
		S_StartSound(NULL, sfx_radio);
	// reguardless of our preferences, put all of this in the chat buffer in case we decide to change from oldchat mid-game.

	if (chat_nummsg_log >= CHAT_BUFSIZE) // too many messages!
		HU_removeChatText_Log();

	strcpy(chat_log[chat_nummsg_log], text);
	chat_nummsg_log++;

	if (chat_nummsg_min >= 8)
		HU_removeChatText_Mini();

	strcpy(chat_mini[chat_nummsg_min], text);
	chat_timers[chat_nummsg_min] = TICRATE*cv_chattime.value;
	chat_nummsg_min++;

	if (OLDCHAT) // if we're using oldchat, print directly in console
		CONS_Printf("%s\n", text);
	else			// if we aren't, still save the message to log.txt
	{
		CON_LogMessage(text);
		CON_LogMessage("\n"); // Add newline. Don't use va for that, since `text` might be refering to va's buffer itself
	}
}


/** Runs a say command, sending an ::XD_SAY message.
  * A say command consists of a signed 8-bit integer for the target, an
  * unsigned 8-bit flag variable, and then the message itself.
  *
  * The target is 0 to say to everyone, 1 to 32 to say to that player, or -1
  * to -32 to say to everyone on that player's team. Note: This means you
  * have to add 1 to the player number, since they are 0 to 31 internally.
  *
  * The flag HU_SHOUT will be set if it is the dedicated server speaking.
  *
  * This function obtains the message using COM_Argc() and COM_Argv().
  *
  * \param target    Target to send message to.
  * \param usedargs  Number of arguments to ignore.
  * \param flags     Set HU_CSAY for server/admin to CECHO everyone.
  * \sa Command_Say_f, Command_Sayteam_f, Command_Sayto_f, Got_Saycmd
  * \author Graue <graue@oceanbase.org>
  */

void DoSayCommand(char *message, int8_t target, uint8_t flags, uint8_t source)
{
	char buf[3 + HU_MAXMSGLEN];
	char *p = buf;

	// Enforce shout for the dedicated server.
	if (dedicated && source == serverplayer && !(flags & HU_CSAY))
	{
		flags |= HU_SHOUT;
	}

	WRITEINT8(p, target);
	WRITEUINT8(p, flags);
	WRITEUINT8(p, source);
	WRITESTRINGN(p, message, HU_MAXMSGLEN);

	SendNetXCmd(XD_SAY, buf, p - buf);
}

/** Send a message to everyone.
  * \sa DoSayPacket, Command_Sayteam_f, Command_Sayto_f
  * \author Graue <graue@oceanbase.org>
  */
static void Command_Say_f(void)
{
	if (COM_Argc() < 2)
	{
		CONS_Printf(M_GetText("say <message>: send a message\n"));
		return;
	}

	// Autoshout is handled by HU_queueChatChar.
	// If you're using the say command, you can use the shout command, lol.
	DoSayPacketFromCommand(0, 1, 0);
}

/** Send a message to a particular person.
  * \sa DoSayPacket, Command_Sayteam_f, Command_Say_f
  * \author Graue <graue@oceanbase.org>
  */
static void Command_Sayto_f(void)
{
	int32_t target;

	if (COM_Argc() < 3)
	{
		CONS_Printf(M_GetText("sayto <playername|playernum> <message>: send a message to a player\n"));
		return;
	}

	target = D_LookupPlayer(COM_Argv(1));
	if (target == -1)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("No player with that name!\n"));
		return;
	}
	target++; // Internally we use 0 to 31, but say command uses 1 to 32.

	DoSayPacketFromCommand((int8_t)target, 2, 0);
}

/** Send a message to members of the player's team.
  * \sa DoSayPacket, Command_Say_f, Command_Sayto_f
  * \author Graue <graue@oceanbase.org>
  */
static void Command_Sayteam_f(void)
{
	if (COM_Argc() < 2)
	{
		CONS_Printf(M_GetText("sayteam <message>: send a message to your team\n"));
		return;
	}

	if (dedicated)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Dedicated servers can't send team messages. Use \"say\".\n"));
		return;
	}

	if (G_GametypeHasTeams())	// revert to normal say if we don't have teams in this gametype.
		DoSayPacketFromCommand(-1, 1, 0);
	else
		DoSayPacketFromCommand(0, 1, 0);
}

/** Send a message to everyone, to be displayed by CECHO. Only
  * permitted to servers and admins.
  */
static void Command_CSay_f(void)
{
	if (COM_Argc() < 2)
	{
		CONS_Printf(M_GetText("csay <message>: send a message to be shown in the middle of the screen\n"));
		return;
	}

	if (!server && !IsPlayerAdmin(consoleplayer))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Only servers and admins can use csay.\n"));
		return;
	}

	DoSayPacketFromCommand(0, 1, HU_CSAY);
}

static void Command_Shout(void)
{
	if (COM_Argc() < 2)
	{
		CONS_Printf(M_GetText("shout <message>: send a message with special alert sound, name, and color\n"));
		return;
	}

	if (!server && !IsPlayerAdmin(consoleplayer))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Only servers and admins can use shout.\n"));
		return;
	}

	DoSayPacketFromCommand(0, 1, HU_SHOUT);
}

/** Receives a message, processing an ::XD_SAY command.
  * \sa DoSayPacket
  * \author Graue <graue@oceanbase.org>
  */
static void Got_Saycmd(uint8_t **p, int32_t playernum)
{
	int8_t target;
	uint8_t flags;
	const char *dispname;
	char *msg;
	dboolean action = false;
	char *ptr;

	CONS_Debug(DBG_NETPLAY,"Received SAY cmd from Player %d (%s)\n", playernum+1, player_names[playernum]);

	// Only server can ever legitimately send this
	if (playernum != serverplayer)
		return;

	target = READINT8(*p);
	flags = READUINT8(*p);
	playernum = READUINT8(*p);
	msg = (char *)*p;
	SKIPSTRINGL(*p, HU_MAXMSGLEN + 1);

	//check for invalid characters (0x80 or above)
	{
		size_t i;
		const size_t j = strlen(msg);
		for (i = 0; i < j; i++)
		{
			if (msg[i] & 0x80)
			{
				CONS_Alert(CONS_WARNING, M_GetText("Illegal say command received from %s containing invalid characters\n"), player_names[playernum]);
				if (server)
					SendKick(playernum, KICK_MSG_CON_FAIL);
				return;
			}
		}
	}
	
	if (LUA_HookPlayerMsg(playernum, target, flags, msg))
		return;

	// If it's a CSAY, just CECHO and be done with it.
	if (flags & HU_CSAY)
	{
		HU_SetCEchoDuration(5);
		I_OutputMsg("Server message: %s", msg);
		//HU_DoCEcho(msg);
		K_AddCEchoMessage(msg);
		return;
	}

	// Handle "/me" actions, but only in messages to everyone.
	if (target == 0 && strlen(msg) > 4 && strnicmp(msg, "/me ", 4) == 0)
	{
		msg += 4;
		action = true;
	}

	if (flags & HU_SHOUT)
		dispname = cv_shoutname.zstring;
	else
		dispname = player_names[playernum];

	// Clean up message a bit
	// If you use a \r character, you can remove your name
	// from before the text and then pretend to be someone else!
	ptr = msg;
	while (*ptr != '\0')
	{
		if (*ptr == '\r')
			*ptr = ' ';

		ptr++;
	}

	// Show messages sent by you, to you, to your team, or to everyone:
	if (playernum == consoleplayer // By you
	|| (target == -1 && ST_SameTeam(&players[consoleplayer], &players[playernum])) // To your team
	|| target == 0 // To everyone
	|| consoleplayer == target-1) // To you
	{
		const char *prefix, *cstart, *adminchar = "\x82~\x83", *remotechar = "\x82@\x83", *fmt2, *textcolor = "\x80";
		char *tempchar = NULL;
		char color_prefix[2];

		if (flags & HU_SHOUT)
		{
			if (cv_shoutcolor.value == -1)
			{
				uint16_t chatcolor = skincolors[players[playernum].skincolor].chatcolor;

				if (chatcolor > V_TANMAP)
				{
					snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80');
				}
				else
				{
					snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80' + (chatcolor >> V_CHARCOLORSHIFT));
				}
			}
			else
			{
				snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80' + cv_shoutcolor.value);
			}

			// Colorize full text
			cstart = textcolor = color_prefix;
		}
		else if (players[playernum].spectator)
		{
			// grey text
			cstart = textcolor = "\x86";
		}
		else if (target == -1) // say team
		{
			if (players[playernum].ctfteam == 1)
			{
				// red text
				cstart = textcolor = "\x85";
			}
			else
			{
				// blue text
				cstart = textcolor = "\x84";
			}
		}
		else
		{
			uint16_t chatcolor = skincolors[players[playernum].skincolor].chatcolor;

			if (chatcolor > V_TANMAP)
			{
				snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80');
			}
			else
			{
				snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80' + (chatcolor >> V_CHARCOLORSHIFT));
			}

			cstart = color_prefix;
		}
		prefix = cstart;
		
		if (strnicmp(msg, ">", 1) == 0) // >imagine not having this hardcoded already
		{
			textcolor = "\x83";
		}
		
		if (strnicmp(msg, "\\", 1) == 0) // I love yen texting :)
		{
			textcolor = "\x88";
		}

		// Give admins and remote admins their symbols.
		if (playernum == serverplayer)
			tempchar = (char *)Z_Calloc(strlen(cstart) + strlen(adminchar) + 1, PU_STATIC, NULL);
		else if (IsPlayerAdmin(playernum))
			tempchar = (char *)Z_Calloc(strlen(cstart) + strlen(remotechar) + 1, PU_STATIC, NULL);

		if (tempchar)
		{
			if (playernum == serverplayer)
				strcat(tempchar, adminchar);
			else
				strcat(tempchar, remotechar);
			strcat(tempchar, cstart);
			cstart = tempchar;
		}

		// Choose the proper format string for display.
		// Each format includes four strings: color start, display
		// name, color end, and the message itself.
		// '\4' makes the message yellow and beeps; '\3' just beeps.
		if (action)
			fmt2 = "* %s%s%s %s%s";
		else if (target-1 == consoleplayer) // To you
		{
			prefix = "\x82[PM]";
			cstart = "\x82";
			textcolor = "\x82";
			fmt2 = "%s<%s%s> %s%s";

			if (flags & HU_PRIVNOTICE)
			{
				dispname = "SERVER";
				prefix = "\x82";
				fmt2 = "%s[%s%s]%s %s%s";
			}
		}
		else if (target > 0) // By you, to another player
		{
			// Use target's name.
			dispname = player_names[target-1];
			prefix = "\x82[TO]";
			cstart = "\x82";
			fmt2 = "%s<%s%s> %s%s";

			if (flags & HU_PRIVNOTICE)
			{
				if (tempchar)
					Z_Free(tempchar);
				return; // I pretend I do not see it
			}
		}
		else // To everyone or sayteam, it doesn't change anything.
			fmt2 = "%s<%s%s> %s%s";
		/*else // To your team
		{
			if (players[playernum].ctfteam == 1) // red
				prefix = "\x85[TEAM]";
			else if (players[playernum].ctfteam == 2) // blue
				prefix = "\x84[TEAM]";
			else
				prefix = "\x83"; // makes sure this doesn't implode if you sayteam on non-team gamemodes

			fmt2 = "%s<%s%s>\x80%s %s%s";
		}*/

		HU_AddChatText(va(fmt2, prefix, cstart, dispname, textcolor, msg), (cv_chatnotifications.value) && !(flags & HU_SHOUT)); // add to chat

		if ((cv_chatnotifications.value) && (flags & HU_SHOUT))
			S_StartSound(NULL, sfx_sysmsg);

		Z_Free(tempchar);
	}
#ifdef _DEBUG
	// I just want to point out while I'm here that because the data is still
	// sent to all players, techincally anyone can see your chat if they really
	// wanted to, even if you used sayto or sayteam.
	// You should never send any sensitive info through sayto for that reason.
	else
		CONS_Printf("Dropped chat: %d %d %s\n", playernum, target, msg);
#endif
}

//
//
void HU_TickSongCredits(void)
{
	if (cursongcredit.text == NULL) // No def
	{
		if (g_realsongcredit == NULL)
			S_StopMusicCredit();
		return;
	}

	cursongcredit.old_x = cursongcredit.x;

	if (cursongcredit.anim > 0)
	{
		int32_t len = V_ThinStringWidth(cursongcredit.text, V_ALLOWLOWERCASE|V_6WIDTHSPACE);
		fixed_t destx = (len+7) * FRACUNIT;

		if (cursongcredit.trans > 0)
		{
			cursongcredit.trans--;
		}

		if (cursongcredit.x < destx)
		{
			cursongcredit.x += (destx - cursongcredit.x) / 2;
		}

		if (cursongcredit.x > destx)
		{
			cursongcredit.x = destx;
		}

		cursongcredit.anim--;
	}
	else
	{
		if (cursongcredit.trans < NUMTRANSMAPS)
		{
			cursongcredit.trans++;
		}

		if (cursongcredit.x > 0)
		{
			cursongcredit.x /= 2;
		}

		if (cursongcredit.x < 0)
		{
			cursongcredit.x = 0;
		}
	}
}

void HU_Ticker(void)
{
	if (dedicated)
		return;

	hu_tick++;
	hu_emoteanim++;
	hu_tick &= 7; // currently only to blink chat input cursor

	if (G_PlayerInputDown(0, gc_scores, false, DEADZONE_BUTTON))
		hu_showscores = !menustack[0] && !chat_on;
	else
		hu_showscores = false;

	hu_keystrokes = false;

	if (chat_on)
	{
		// count down the scroll timer.
		if (chat_scrolltime > 0)
			chat_scrolltime--;
	}

	if (netgame)
	{
		size_t i = 0;

		// handle chat timers
		for (i=0; (i<chat_nummsg_min); i++)
		{
			if (chat_timers[i] > 0)
				chat_timers[i]--;
			else
				HU_removeChatText_Mini();
		}
	}

	if (WipeInAction)
		return;

	if (cechotimer)
		cechotimer--;

	if (gamestate != GS_LEVEL)
	{
		return;
	}

	resynch_ticker++;
}

static dboolean teamtalk = false;
static dboolean justscrolleddown;
static dboolean justscrolledup;
static int16_t typelines = 1; // number of drawfill lines we need when drawing the chat. it's some weird hack and might be one frame off but I'm lazy to make another loop.
// It's up here since it has to be reset when we open the chat.

static dboolean HU_chatboxContainsOnlySpaces(void)
{
	size_t i;

	for (i = 0; w_chat_buf[i]; i++)
		if (w_chat_buf[i] != ' ')
			return false;

	return true;
}

static void HU_sendChatMessage(void)
{
	char buf[2 + HU_MAXMSGLEN + 1];
	char *msg = &buf[2];
	size_t ci;
	int32_t target = 0;

	// if our message was nothing but spaces, don't send it.
	if (HU_chatboxContainsOnlySpaces())
		return;

	// copy printable characters and terminating '\0' only.
	for (ci = 2; w_chat_buf[ci-2]; ci++)
	{
		char c = w_chat_buf[ci-2];
		if (c >= ' ' && !(c & 0x80))
			buf[ci] = c;
	};
	buf[ci] = '\0';

	M_TextInputClear(&w_chat);

	if (strlen(msg) > 4 && strnicmp(msg, "/pm", 3) == 0) // used /pm
	{
		int32_t spc = 1; // used if playernum[1] is a space.
		char playernum[3];
		const char *newmsg;

		strncpy(playernum, msg+3, 3);
		// check for undesirable characters in our "number"
		if (!(isdigit(playernum[0]) && isdigit(playernum[1])))
		{
			// check if playernum[1] is a space
			if (playernum[1] == ' ')
				spc = 0;
				// let it slide
			else
			{
				HU_AddChatText("\x82NOTICE: \x80Invalid command format. Correct format is \'/pm<player num> \'.", false);
				return;
			}
		}
		// I'm very bad at C, I swear I am, additional checks eww!
		if (spc != 0 && msg[5] != ' ')
		{
			HU_AddChatText("\x82NOTICE: \x80Invalid command format. Correct format is \'/pm<player num> \'.", false);
			return;
		}

		target = atoi(playernum); // turn that into a number

		// check for target player, if it doesn't exist then we can't send the message!
		if (target < MAXPLAYERS && playeringame[target]) // player exists
			target++; // even though playernums are from 0 to 31, target is 1 to 32, so up that by 1 to have it work!
		else
		{
			HU_AddChatText(va("\x82NOTICE: \x80Player %d does not exist.", target), false); // same
			return;
		}

		// we need to get rid of the /pm<player num>
		newmsg = msg+5+spc;
		strlcpy(msg, newmsg, HU_MAXMSGLEN + 1);
	}
	if (ci > 2) // don't send target+flags+empty message.
	{
		if (teamtalk)
			buf[0] = -1; // target
		else
			buf[0] = target;

		buf[1] = ((server || IsPlayerAdmin(consoleplayer)) && cv_autoshout.value) ? HU_SHOUT : 0; // flags
		DoSayPacket(target, buf[1], consoleplayer, msg);
	}
}

void HU_clearChatChars(void)
{
	M_TextInputClear(&w_chat);
	chat_on = false;

	I_UpdateMouseGrab();
	I_SetTextInput(false);
}

//
// Returns true if key eaten
//
dboolean HU_Responder(event_t *ev)
{
	if (ev->type != ev_keydown)
		return false;

	// only KeyDown events now...

	// Shoot, to prevent P1 chatting from ruining the game for everyone else, it's either:
	// A. completely disallow opening chat entirely in online splitscreen
	// or B. iterate through all controls to make sure it's bound to player 1 before eating
	// You can see which one I chose.
	// (Unless if you're sharing a keyboard, since you probably establish when you start chatting that you have dibs on it...)
	// (Ahhh, the good ol days when I was a kid who couldn't afford an extra USB controller...)

	if (ev->data1 >= NUMKEYS)
	{
		int32_t i;
		for (i = 0; i < num_gamecontrols; i++)
		{
			if (G_ControlBoundToKey(0, i, ev->data1, false))
				break;
		}

		if (i == num_gamecontrols)
			return false;
	}

	if (!chat_on)
	{
		// enter chat mode
		if (G_ControlBoundToKey(0, gc_talkkey, ev->data1, false)
			&& netgame && !OLD_MUTE) // check for old chat mute, still let the players open the chat incase they want to scroll otherwise.
		{
			chat_on = true;
			w_chat_buf[0] = 0;
			M_TextInputInit(&w_chat, w_chat_buf, sizeof w_chat_buf);
			memset(&emote_autocomplete, 0, sizeof(emote_autocomplete));
			teamtalk = false;
			chat_scrollmedown = true;
			typelines = 1;
			I_SetTextInput(true);
			return true;
		}
		if (G_ControlBoundToKey(0, gc_teamkey, ev->data1, false)
			&& netgame && !OLD_MUTE)
		{
			chat_on = true;
			w_chat_buf[0] = 0;
			M_TextInputInit(&w_chat, w_chat_buf, sizeof w_chat_buf);
			memset(&emote_autocomplete, 0, sizeof(emote_autocomplete));
			teamtalk = G_GametypeHasTeams();	// Don't teamtalk if we don't have teams.
			chat_scrollmedown = true;
			typelines = 1;
			I_SetTextInput(true);
			return true;
		}
	}
	else // if chat_on
	{
		int32_t c = (int32_t)ev->data1;

		// Ignore modifier keys
		// Note that we do this here so users can still set
		// their chat keys to one of these, if they so desire.
		if (ev->data1 == KEY_LSHIFT || ev->data1 == KEY_RSHIFT
		 || ev->data1 == KEY_LCTRL || ev->data1 == KEY_RCTRL
		 || ev->data1 == KEY_LALT || ev->data1 == KEY_RALT)
			return true;

		// Ignore non-keyboard keys, except when the talk key is bound
		if (ev->data1 >= NUMKEYS
		&& !G_ControlBoundToKey(0, gc_talkkey, ev->data1, false))
			return false;

		if (M_TextInputHandleEmotes(&w_chat, c, &emote_autocomplete))
			; // Do nothing
		else if (c == KEY_ENTER)
		{
			if (!CHAT_MUTE)
				HU_sendChatMessage();

			chat_on = false;
			chat_scrollmedown = true; // you hit enter, so you might wanna autoscroll to see what you just sent. :)
			I_SetTextInput(false);
			I_UpdateMouseGrab();
		}
		else if (c == KEY_ESCAPE
			|| ((G_ControlBoundToKey(0, gc_talkkey, c, false)
			|| G_ControlBoundToKey(0, gc_teamkey, c, false))
			&& c >= NUMKEYS)) // If it's not a keyboard key, then the chat button is used as a toggle.
		{
			chat_on = false;
			I_SetTextInput(false);
			I_UpdateMouseGrab();
		}
		else if ((c == KEY_UPARROW || c == KEY_MOUSEWHEELUP) && chat_scroll > 0 && !OLDCHAT) // CHAT SCROLLING YAYS!
		{
			chat_scroll--;
			justscrolledup = true;
			chat_scrolltime = 4;
		}
		else if ((c == KEY_DOWNARROW || c == KEY_MOUSEWHEELDOWN) && chat_scroll < chat_maxscroll && chat_maxscroll > 0 && !OLDCHAT)
		{
			chat_scroll++;
			justscrolleddown = true;
			chat_scrolltime = 4;
		}

		return true;
	}

	return false;
}

//======================================================================
//                         HEADS UP DRAWING
//======================================================================

// Precompile a wordwrapped string to any given width.
// This is a muuuch better method than V_WORDWRAP.
// again stolen and modified a bit from video.c, don't mind me, will need to rearrange this one day.
// this one is simplified for the chat drawer.
static char *CHAT_WordWrap(int32_t x, int32_t w, int32_t option, const char *string)
{
	int32_t c;
	size_t chw, i, lastusablespace = 0;
	size_t slen;
	char *newstring = Z_StrDup(string);
	int32_t spacewidth = (vid.width < 640) ? 8 : 4, charwidth = (vid.width < 640) ? 8 : 4;
	emote_t *emote = NULL;
	int emotelen = 0;

	slen = strlen(string);
	x = 0;

	for (i = 0; i < slen; ++i)
	{
		c = newstring[i];
		if ((uint8_t)c >= 0x80 && (uint8_t)c <= 0x8F) //color parsing! -Inuyasha 2.16.09
			continue;

		if (c == '\n')
		{
			x = 0;
			lastusablespace = 0;
			continue;
		}

		if (!(option & V_ALLOWLOWERCASE))
			c = toupper(c);
		c -= HU_FONTSTART;

		if ((emote = M_VerifyEmote(string+i, &emotelen)))
		{
			chw = EMOTEWIDTH;
			i += emotelen-1; // Will be incremented on next loop iteration, hence -1
		}
		else if (c < 0 || c >= HU_FONTSIZE || !fontv[HU_FONT].font[c])
		{
			chw = spacewidth;
			lastusablespace = i;
		}
		else
			chw = charwidth;

		x += chw;

		if (lastusablespace != 0 && x > w)
		{
			//CONS_Printf("Wrap at index %d\n", i);
			newstring[lastusablespace] = '\n';
			i = lastusablespace+1;
			lastusablespace = 0;
			x = 0;
		}
	}

	return newstring;
}

// 30/7/18: chaty is now the distance at which the lowest point of the chat will be drawn if that makes any sense.

// let's use this as our coordinates
// :chao: guess this just works lol
#define chatx ((int16_t)(13 + (cv_chat_xoffset.value)))
#define chaty ((int16_t)(169 + (cv_chat_yoffset.value)))

#define CHATSNAP (V_SNAPTOBOTTOM|V_SNAPTOLEFT)

// chat stuff by VincyTM LOL XD!

// HU_DrawMiniChat

static void HU_drawMiniChat(void)
{
	int32_t x = chatx+2;
	int32_t charwidth = 4, charheight = 6;
	int32_t boxw = cv_chatwidth.value;
	int32_t dx = 0, dy = 0;
	size_t i = chat_nummsg_min;
	dboolean prev_linereturn = false; // a hack to prevent double \n while I have no idea why they happen in the first place.

	int32_t msglines = 0;
	// process all messages once without rendering anything or doing anything fancy so that we know how many lines each message has...
	int32_t y;

	if (!chat_nummsg_min)
		return; // needless to say it's useless to do anything if we don't have anything to draw.

	if (r_splitscreen > 1)
		boxw = max(64, boxw/2);

	for (; i>0; i--)
	{
		char *msg = CHAT_WordWrap(x+2, boxw-(charwidth*2), CHATSNAP|V_ALLOWLOWERCASE, chat_mini[i-1]);
		size_t j = 0;
		int32_t linescount = 0;
		emote_t *emote = NULL;
		int emotelen = 0;

		while (msg[j]) // iterate through msg
		{
			if (msg[j] < HU_FONTSTART) // don't draw
			{
				if (msg[j] == '\n') // get back down.
				{
					++j;
					if (!prev_linereturn)
					{
						linescount += 1;
						dx = 0;
					}
					prev_linereturn = true;
					continue;
				}
				else if (msg[j] & 0x80) // stolen from video.c, nice.
				{
					++j;
					continue;
				}

				++j;
			}
			else if ((emote = M_VerifyEmote(msg+j, &emotelen)))
			{
				dx += EMOTEWIDTH - charwidth;
				j += emotelen;
			}
			else
			{
				j++;
			}

			prev_linereturn = false;
			dx += charwidth;

			if (dx >= boxw)
			{
				dx = 0;
				linescount += 1;
			}
		}

		dy = 0;
		dx = 0;
		msglines += linescount+1;
		Z_Free(msg);
	}

	y = chaty - charheight*(msglines+1);

#ifdef NETSPLITSCREEN
	if (r_splitscreen)
	{
		y -= BASEVIDHEIGHT/2;
		if (r_splitscreen > 1)
			y += 16;
	}
	else
#endif
	{
		y -= (cv_kartspeedometer.value ? 16 : 0);
	}

	dx = 0;
	dy = 0;
	i = 0;
	prev_linereturn = false;

	for (; i<=(chat_nummsg_min-1); i++) // iterate through our hot messages
	{
		int32_t clrflag = 0;
		int32_t timer = ((cv_chattime.value*TICRATE)-chat_timers[i]) - cv_chattime.value*TICRATE+9; // see below...
		int32_t transflag = (timer >= 0 && timer <= 9) ? (timer*V_10TRANS) : 0; // you can make bad jokes out of this one.
		size_t j = 0;
		char *msg = CHAT_WordWrap(x+2, boxw-(charwidth*2), CHATSNAP|V_ALLOWLOWERCASE, chat_mini[i]); // get the current message, and word wrap it.
		uint8_t *colormap = NULL;
		emote_t *emote = NULL;
		int emotelen = 0;

		while (msg[j]) // iterate through msg
		{
			if (msg[j] < HU_FONTSTART) // don't draw
			{
				if (msg[j] == '\n') // get back down.
				{
					++j;
					if (!prev_linereturn)
					{
						dy += charheight;
						dx = 0;
					}
					prev_linereturn = true;
					continue;
				}
				else if (msg[j] & 0x80) // stolen from video.c, nice.
				{
					clrflag = ((msg[j] & 0x7f) << V_CHARCOLORSHIFT) & V_CHARCOLORMASK;
					colormap = V_GetStringColormap(clrflag);
					++j;
					continue;
				}

				++j;
			}
			else if ((emote = M_VerifyEmote(msg+j, &emotelen)))
			{
				if (cv_chatbacktint.value) // on request of wolfy
					V_DrawFillConsoleMap(x + dx + 2, y+dy, EMOTEWIDTH, charheight, 239|CHATSNAP);

				M_DrawEmote(x+dx+2, y+dy, emote, CHATSNAP|transflag);
				dx += EMOTEWIDTH - charwidth;

				j += emotelen;
			}
			else
			{
				if (cv_chatbacktint.value) // on request of wolfy
					V_DrawFillConsoleMap(x + dx + 2, y+dy, charwidth, charheight, 159|CHATSNAP);

				V_DrawChatCharacter(x + dx + 2, y+dy+1, msg[j++] |CHATSNAP|transflag, true, colormap);
			}

			dx += charwidth;
			prev_linereturn = false;

			if (dx >= boxw)
			{
				dx = 0;
				dy += charheight;
			}
		}

		dy += charheight;
		dx = 0;
		Z_Free(msg);
	}

	// decrement addy and make that shit smooth:
	addy /= 2;

}

// HU_DrawChatLog

static void HU_drawChatLog(int32_t offset)
{
	int32_t charwidth = 4, charheight = 6;
	int32_t boxw = cv_chatwidth.value, boxh = cv_chatheight.value;
	int32_t x = chatx+2, y, dx = 0, dy = 0;
	uint32_t i = 0;
	int32_t chat_topy, chat_bottomy;
	int32_t highlight = HU_GetHighlightColor();
	dboolean atbottom = false;

	// make sure that our scroll position isn't "illegal";
	if (chat_scroll > chat_maxscroll)
		chat_scroll = chat_maxscroll;

#ifdef NETSPLITSCREEN
	if (r_splitscreen)
	{
		boxh = max(6, boxh/2);
		if (r_splitscreen > 1)
			boxw = max(64, boxw/2);
	}
#endif

	y = chaty - offset*charheight - (chat_scroll*charheight) - boxh*charheight - 12;

#ifdef NETSPLITSCREEN
	if (r_splitscreen)
	{
		y -= BASEVIDHEIGHT/2;

		if (r_splitscreen > 1)
			y += 16;
	}
	else
#endif
	{
		y -= (cv_kartspeedometer.value ? 16 : 0);
	}

	chat_topy = y + chat_scroll*charheight;
	chat_bottomy = chat_topy + boxh*charheight;

	V_DrawFillConsoleMap(chatx, chat_topy, boxw, boxh*charheight +2, 159|CHATSNAP); // log box

	for (i = 0; i < chat_nummsg_log; i++) // iterate through our chatlog
	{
		int32_t clrflag = 0;
		int32_t j = 0;
		char *msg = CHAT_WordWrap(x+2, boxw-(charwidth*2), CHATSNAP|V_ALLOWLOWERCASE, chat_log[i]); // get the current message, and word wrap it.
		uint8_t *colormap = NULL;
		emote_t *emote = NULL;
		int emotelen = 0;

		while (msg[j]) // iterate through msg
		{
			if (msg[j] < HU_FONTSTART) // don't draw
			{
				if (msg[j] == '\n') // get back down.
				{
					++j;
					dy += charheight;
					dx = 0;
					continue;
				}
				else if (msg[j] & 0x80) // stolen from video.c, nice.
				{
					clrflag = ((msg[j] & 0x7f) << V_CHARCOLORSHIFT) & V_CHARCOLORMASK;
					colormap = V_GetStringColormap(clrflag);
					++j;
					continue;
				}

				++j;
			}
			else if ((emote = M_VerifyEmote(msg+j, &emotelen)))
			{
				if ((y+dy+2 >= chat_topy) && (y+dy < (chat_bottomy)))
				{
					M_DrawEmote(x+dx+2, y+dy+1, emote, CHATSNAP);
					dx += EMOTEWIDTH - charwidth;
				}
				j += emotelen;
			}
			else
			{
				if ((y+dy+2 >= chat_topy) && (y+dy < (chat_bottomy)))
					V_DrawChatCharacter(x + dx + 2, y+dy+2, msg[j++] |CHATSNAP, true, colormap);
				else
					j++; // don't forget to increment this or we'll get stuck in the limbo.
			}

			dx += charwidth;
			if (dx >= boxw-charwidth-2 && i<chat_nummsg_log && msg[j] >= HU_FONTSTART) // end of message shouldn't count, nor should invisible characters!!!!
			{
				dx = 0;
				dy += charheight;
			}
		}

		dy += charheight;
		dx = 0;
		Z_Free(msg);
	}


	if (((chat_scroll >= chat_maxscroll) || (chat_scrollmedown)) && !(justscrolleddown || justscrolledup || chat_scrolltime)) // was already at the bottom of the page before new maxscroll calculation and was NOT scrolling.
	{
		atbottom = true; // we should scroll
	}
	chat_scrollmedown = false;

	// getmaxscroll through a lazy hack. We do all these loops,
	// so let's not do more loops that are gonna lag the game more. :P
	chat_maxscroll = max(dy / charheight - cv_chatheight.value, 0);

	// if we're not bound by the time, autoscroll for next frame:
	if (atbottom)
		chat_scroll = chat_maxscroll;

	// draw arrows to indicate that we can (or not) scroll.
	// account for Y = -1 offset in tinyfont
	if (chat_scroll > 0)
		V_DrawCharacter(chatx-9, ((justscrolledup) ? (chat_topy-1) : (chat_topy)), CHATSNAP | highlight | '\x1A', false); // up arrow
	if (chat_scroll < chat_maxscroll)
		V_DrawCharacter(chatx-9, chat_bottomy-((justscrolleddown) ? 5 : 6), CHATSNAP | highlight | '\x1B', false); // down arrow

	justscrolleddown = false;
	justscrolledup = false;
}

//
// HU_DrawChat
//
// Draw chat input
//

static void HU_DrawChat(void)
{
	int32_t charwidth = 4, charheight = 6;
	int32_t boxw = cv_chatwidth.value;
	int32_t t = 0, c = 0, y = chaty - (typelines*charheight);
	uint32_t i = 0, saylen = strlen(w_chat_buf); // You learn new things everyday!
	uint32_t charcount = 0;
	int32_t cflag = 0;\
	size_t select_start = 0, select_end = 0;
	const char *ntalk = "Say: ", *ttalk = "Team: ";
	const char *talk = ntalk;
	const char *mute = "Chat has been muted.";

#ifdef NETSPLITSCREEN
	if (r_splitscreen)
	{
		y -= BASEVIDHEIGHT/2;
		if (r_splitscreen > 1)
		{
			y += 16;
			boxw = max(64, boxw/2);
		}
	}
	else
#endif
	{
		y -= (cv_kartspeedometer.value ? 16 : 0);
	}

	if (teamtalk)
	{
		talk = ttalk;
#if 0
		if (players[consoleplayer].ctfteam == 1)
			t = 0x500;  // Red
		else if (players[consoleplayer].ctfteam == 2)
			t = 0x400; // Blue
#endif
	}

	if (CHAT_MUTE)
	{
		talk = mute;
		typelines = 1;
		cflag = V_GRAYMAP; // set text in gray if chat is muted.
	}

	V_DrawFillConsoleMap(chatx, y-1, boxw, (typelines*charheight), 159 | CHATSNAP);

	while (talk[i])
	{
		if (talk[i] < HU_FONTSTART)
			++i;
		else
		{
			V_DrawChatCharacter(chatx + c + 2, y, talk[i] |CHATSNAP|cflag, true, V_GetStringColormap(talk[i]|cflag));
			i++;
		}

		c += charwidth;
	}

	// if chat is muted, just draw the log and get it over with, no need to draw anything else.
	if (CHAT_MUTE)
	{
		HU_drawChatLog(0);
		return;
	}

	i = 0;
	typelines = 1;

	if ((w_chat.cursor == 0 || w_chat.length == 0) && hu_tick < 4)
		V_DrawChatCharacter(chatx+2+c, y+1, '_'|CHATSNAP|t, true, NULL);

	if (w_chat.select != w_chat.cursor)
	{
		select_start = min(w_chat.select, w_chat.cursor);
		select_end =   max(w_chat.select, w_chat.cursor);
	}

	while (w_chat_buf[i])
	{
		dboolean skippedline = false;
		emote_t *emote = NULL;
		int emotelen = 0;
		int drawwidth = charwidth; // if we've drawn emote, selection needs to account for that

		if ((emote = M_VerifyEmote(w_chat_buf+i, &emotelen)))
		{
			M_DrawEmote(chatx + c + 2, y-1, emote, CHATSNAP|t);
			c += EMOTEWIDTH - charwidth;
			i += emotelen-1;
			drawwidth = EMOTEWIDTH;
			if (cv_chat_showlimit.value)
				charcount += emotelen;
		}
		else if (w_chat_buf[i] >= HU_FONTSTART) //Hurdler: isn't it better like that?
		{
			V_DrawChatCharacter(chatx + c + 2, y, w_chat_buf[i] | CHATSNAP | t, true, NULL);
			if (cv_chat_showlimit.value)
				charcount++;
		}

		// Draw selection
		if (i >= select_start && i < select_end)
			V_DrawFill(chatx + c + 2 - (drawwidth-charwidth), y-1, drawwidth, charheight, 103|V_TRANSLUCENT|CHATSNAP|t);

		++i;

		if (w_chat.cursor == i)
		{
			int32_t cursorx = (c+charwidth < boxw-charwidth) ? (chatx + 2 + c+charwidth) : (chatx+1); // we may have to go down.
			int32_t cursory = (cursorx != chatx+1) ? (y) : (y+charheight);
			if (hu_tick < 4)
				V_DrawChatCharacter(cursorx, cursory+1, '_' |CHATSNAP|t, true, NULL);

			if (cursorx == chatx+1 && saylen == i-1) // a weirdo hack
			{
				typelines += 1;
				skippedline = true;
			}
		}

		c += charwidth;
		if (c > boxw-(charwidth*2) && !skippedline)
		{
			c = 0;
			y += charheight;
			typelines += 1;
		}
	}

	if (emote_autocomplete.emotestart != -1 && (emote_autocomplete.complete[0] || (w_chat.cursor - emote_autocomplete.emotestart) > 1))
	{
		emote_t *suggest;
		int skip = 0;

		const char *complete;
		int complete_len;

		if (emote_autocomplete.complete[0])
		{
			complete = emote_autocomplete.complete;
			complete_len = strlen(complete);
		}
		else
		{
			complete = &w_chat_buf[emote_autocomplete.emotestart];
			complete_len = w_chat.cursor - emote_autocomplete.emotestart;
		}

		// A bit of copy-paste from /pm code :p
		int32_t suggesty = chaty - charheight - 1;
		size_t longest_suggestion_length = 0;

		while ((suggest = M_FindEmote(complete, complete_len, skip)) != NULL)
		{
			longest_suggestion_length = max(longest_suggestion_length, strlen(suggest->name));
			++skip;
		}

		skip = 0;

#ifdef NETSPLITSCREEN
		if (splitscreen)
		{
			suggesty -= BASEVIDHEIGHT/2;
			if (splitscreen > 1)
				suggesty += 16;
		}
		else
#endif
			suggesty -= (cv_kartspeedometer.value ? 16 : 0);

		while ((suggest = M_FindEmote(complete, complete_len, skip)) != NULL)
		{
			V_DrawFillConsoleMap(chatx + boxw + 2, suggesty - (7*skip), (longest_suggestion_length+2)*4 + EMOTEWIDTH, 6, CHATSNAP);

			// Highlight currently suggested emote
			int hlflag = 0;
			if (skip == emote_autocomplete.skip && emote_autocomplete.complete[0])
				hlflag = V_YELLOWMAP;

			V_DrawSmallString(chatx + boxw + 4 + EMOTEWIDTH, suggesty - (7*skip), hlflag|CHATSNAP|V_ALLOWLOWERCASE, va(":%s:", suggest->name));
			M_DrawEmote(chatx + boxw + 2, suggesty - skip*7, suggest, CHATSNAP);

			++skip;
		}
	}

	if (cv_chat_showlimit.value)
	{
		// Limit
		const int32_t clr = ((HU_MAXMSGLEN - charcount) > 64 ? V_TRANSLUCENT : (charcount == HU_MAXMSGLEN ? V_REDMAP : V_YELLOWMAP));
		V_DrawSmallString(chatx, y+7, CHATSNAP|clr, va("%d/%d", charcount, HU_MAXMSGLEN));
	}

	// handle /pm list. It's messy, horrible and I don't care.
	if (strnicmp(w_chat_buf, "/pm", 3) == 0 && vid.width >= 400 && !teamtalk) // 320x200 unsupported kthxbai
	{
		int32_t count = 0;
		int32_t p_dispy = chaty - charheight -1;
#ifdef NETSPLITSCREEN
		if (r_splitscreen)
		{
			p_dispy -= BASEVIDHEIGHT/2;
			if (r_splitscreen > 1)
				p_dispy += 16;
		}
		else
#endif
		{
			p_dispy -= (cv_kartspeedometer.value ? 16 : 0);
		}

		i = 0;
		for(i=0; (i<MAXPLAYERS); i++)
		{
			// filter: (code needs optimization pls help I'm bad with C)
			if (w_chat_buf[3])
			{
				char playernum[3];
				uint32_t n;
				// right, that's half important: (w_chat_buf[4] may be a space since /pm0 msg is perfectly acceptable!)
				if ( ( ((w_chat_buf[3] != 0) && ((w_chat_buf[3] < '0') || (w_chat_buf[3] > '9'))) || ((w_chat_buf[4] != 0) && (((w_chat_buf[4] < '0') || (w_chat_buf[4] > '9'))))) && (w_chat_buf[4] != ' '))
					break;

				strncpy(playernum, w_chat_buf+3, 3);
				n = atoi(playernum); // turn that into a number
				// special cases:

				if ((n == 0) && !(w_chat_buf[4] == '0'))
				{
					if (!(i<10))
						continue;
				}
				else if ((n == 1) && !(w_chat_buf[3] == '0'))
				{
					if (!((i == 1) || ((i >= 10) && (i <= 19))))
						continue;
				}
				else if ((n == 2) && !(w_chat_buf[3] == '0'))
				{
					if (!((i == 2) || ((i >= 20) && (i <= 29))))
						continue;
				}
				else if ((n == 3) && !(w_chat_buf[3] == '0'))
				{
					if (!((i == 3) || ((i >= 30) && (i <= 31))))
						continue;
				}
				else	// general case.
				{
					if (i != n)
						continue;
				}
			}

			if (playeringame[i])
			{
				char name[MAXPLAYERNAME+1];
				strlcpy(name, player_names[i], 7); // shorten name to 7 characters.
				V_DrawFillConsoleMap(chatx+ boxw + 2, p_dispy- (6*count), 48, 6, 159 | CHATSNAP); // fill it like the chat so the text doesn't become hard to read because of the hud.
				V_DrawSmallString(chatx+ boxw + 4, p_dispy- (6*count), CHATSNAP|V_ALLOWLOWERCASE, va("\x82%d\x80 - %s", i, name));
				count++;
			}
		}
		if (count == 0) // no results.
		{
			V_DrawFillConsoleMap(chatx+boxw+2, p_dispy- (6*count), 48, 6, 159 | CHATSNAP); // fill it like the chat so the text doesn't become hard to read because of the hud.
			V_DrawSmallString(chatx+boxw+4, p_dispy- (6*count), CHATSNAP|V_ALLOWLOWERCASE, "NO RESULT.");
		}
	}

	HU_drawChatLog(typelines-1); // typelines is the # of lines we're typing. If there's more than 1 then the log should scroll up to give us more space.
}


// For anyone who, for some godforsaken reason, likes oldchat.


static void HU_DrawChat_Old(void)
{
	int32_t t = 0, c = 0, y = HU_INPUTY;
	uint32_t charcount = 0;
	size_t i = 0;
	const char *ntalk = "Say: ", *ttalk = "Say-Team: ";
	const char *talk = ntalk;
	size_t select_start = 0, select_end = 0;
	int32_t charwidth = 8 * con_scalefactor; //(fontv[HU_FONT].font['A'-HU_FONTSTART]->width) * con_scalefactor;
	int32_t charheight = 8 * con_scalefactor; //(fontv[HU_FONT].font['A'-HU_FONTSTART]->height) * con_scalefactor;
	if (teamtalk)
	{
		talk = ttalk;
#if 0
		if (players[consoleplayer].ctfteam == 1)
			t = 0x500;  // Red
		else if (players[consoleplayer].ctfteam == 2)
			t = 0x400; // Blue
#endif
	}

	while (talk[i])
	{
		if (talk[i] < HU_FONTSTART)
		{
			++i;
			//charwidth = 4 * con_scalefactor;
		}
		else
		{
			//charwidth = (fontv[HU_FONT].font[talk[i]-HU_FONTSTART]->width) * con_scalefactor;
			V_DrawCharacter(HU_INPUTX + c, y, talk[i++] | cv_constextsize.value | V_NOSCALESTART, true);
		}
		c += charwidth;
	}

	if ((w_chat.cursor == 0 || w_chat.length == 0) && hu_tick < 4)
		V_DrawCharacter(HU_INPUTX+c+charwidth*w_chat.cursor, y+2*con_scalefactor, '_'|cv_constextsize.value|V_NOSCALESTART|t, true);

	if (w_chat.select != w_chat.cursor)
	{
		select_start = min(w_chat.select, w_chat.cursor);
		select_end =   max(w_chat.select, w_chat.cursor);
	}

	i = 0;
	while (w_chat_buf[i])
	{
		int emotelen;
		emote_t *emote;

		if ((emote = M_VerifyEmote(w_chat_buf+i, &emotelen)))
		{
			M_DrawScaledEmote((HU_INPUTX + c)<<FRACBITS, (y+con_scalefactor*2)<<FRACBITS, charwidth*FRACUNIT/EMOTEWIDTH, emote, V_NOSCALESTART | V_NOSCALEPATCH | t);
			i += emotelen-1;

			if (cv_chat_showlimit.value)
				charcount += emotelen;
		}
		else if (w_chat_buf[i] >= HU_FONTSTART) //Hurdler: isn't it better like that?
		{
			//charwidth = (fontv[HU_FONT].font[w_chat[i]-HU_FONTSTART]->width) * con_scalefactor;
			V_DrawCharacter(HU_INPUTX + c, y, w_chat_buf[i] | cv_constextsize.value | V_NOSCALESTART | t, true);

			if (cv_chat_showlimit.value)
				charcount++;
		}

		// Draw selection
		if (i >= select_start && i < select_end)
			V_DrawFill(HU_INPUTX + c, y, charwidth, charheight, 103|V_TRANSLUCENT|cv_constextsize.value|V_NOSCALESTART|t);

		++i;

		if (w_chat.cursor == i && hu_tick < 4)
		{
			int32_t cursorx = (HU_INPUTX+c+charwidth < vid.width) ? (HU_INPUTX + c + charwidth) : (HU_INPUTX); // we may have to go down.
			int32_t cursory = (cursorx != HU_INPUTX) ? (y) : (y+charheight);
			V_DrawCharacter(cursorx, cursory+2*con_scalefactor, '_' |cv_constextsize.value | V_NOSCALESTART|t, true);
		}

		c += charwidth;
		if (c >= vid.width)
		{
			c = 0;
			y += charheight;
		}
	}

	// console chat users are FINALLY being fed!
	if (cv_chat_showlimit.value)
	{
		const char *lim = va(" (%i/%i)", charcount, HU_MAXMSGLEN);
		for (i = 0; lim[i]; i++)
		{
			V_DrawCharacter(HU_INPUTX + c, y, lim[i] | cv_constextsize.value | V_NOSCALESTART | t, true);

			c += charwidth;
			if (c >= vid.width) // text wrapping
			{
				c = 0;
				y += charheight;
			}
		}
	}
}

static void HU_DrawCEcho(void)
{
	int32_t i = 0;
	int32_t y = (BASEVIDHEIGHT/2)-4;
	int32_t pnumlines = 0;

	uint32_t realflags = cechoflags|V_SPLITSCREEN; // requested as part of splitscreen's stuff

	char *line;
	char *echoptr;
	char temp[1024];

	for (i = 0; cechotext[i] != '\0'; ++i)
		if (cechotext[i] == '\\')
			pnumlines++;

	y -= (pnumlines-1)*6;

	// Prevent crashing because I'm sick of this
	if (y < 0)
	{
		CONS_Alert(CONS_WARNING, "CEcho contained too many lines, not displaying\n");
		cechotimer = 0;
		return;
	}

	strcpy(temp, cechotext);
	echoptr = &temp[0];

	while (*echoptr != '\0')
	{
		line = strchr(echoptr, '\\');

		if (line == NULL)
			break;

		*line = '\0';

		V_DrawCenteredString(BASEVIDWIDTH/2, y, realflags, echoptr);
		y += 12;

		echoptr = line;
		echoptr++;
	}
}

//
// demo info stuff
//
uint32_t hu_demotime;
uint32_t hu_demolap;

static void HU_DrawDemoInfo(void)
{
	if (!multiplayer)/* netreplay */
	{
		V_DrawCenteredString((BASEVIDWIDTH/2), BASEVIDHEIGHT-40, 0, M_GetText("Replay:"));
		V_DrawCenteredString((BASEVIDWIDTH/2), BASEVIDHEIGHT-32, V_ALLOWLOWERCASE, player_names[0]);
	}
	else
	{
		V_DrawRightAlignedThinString(BASEVIDWIDTH-2, BASEVIDHEIGHT-10, V_ALLOWLOWERCASE, demo.titlename);
	}

	if (modeattacking)
	{
		V_DrawRightAlignedString((BASEVIDWIDTH/2)-4, BASEVIDHEIGHT-24, V_YELLOWMAP|V_MONOSPACE, "BEST TIME:");
		if (hu_demotime != UINT32_MAX)
			V_DrawString((BASEVIDWIDTH/2)+4, BASEVIDHEIGHT-24, V_MONOSPACE, va("%i'%02i\"%02i",
				G_TicsToMinutes(hu_demotime,true),
				G_TicsToSeconds(hu_demotime),
				G_TicsToCentiseconds(hu_demotime)));
		else
			V_DrawString((BASEVIDWIDTH/2)+4, BASEVIDHEIGHT-24, V_MONOSPACE, "--'--\"--");

		V_DrawRightAlignedString((BASEVIDWIDTH/2)-4, BASEVIDHEIGHT-16, V_YELLOWMAP|V_MONOSPACE, "BEST LAP:");
		if (hu_demolap != UINT32_MAX)
			V_DrawString((BASEVIDWIDTH/2)+4, BASEVIDHEIGHT-16, V_MONOSPACE, va("%i'%02i\"%02i",
				G_TicsToMinutes(hu_demolap,true),
				G_TicsToSeconds(hu_demolap),
				G_TicsToCentiseconds(hu_demolap)));
		else
			V_DrawString((BASEVIDWIDTH/2)+4, BASEVIDHEIGHT-16, V_MONOSPACE, "--'--\"--");
	}
}


//
// Song credits
//
void HU_DrawSongCredits(void)
{
	fixed_t x;
	fixed_t y = ((r_splitscreen && G_GamestateUsesLevel()) ? (BASEVIDHEIGHT/2)-4 : 32) * FRACUNIT;
	int32_t snapflags = cursongcredit.snapflags;
	int32_t bgt;

	if (!cursongcredit.text || cursongcredit.trans >= NUMTRANSMAPS) // No def
	{
		return;
	}

	if (menustack[0] && (menustack[0] == MN_MPAUSE ||
		menustack[0] == MN_MAPAUSE ||
		menustack[0] == MN_SPAUSE)
	)
	{
		// We already display this here. No need to do it twice!
		return;
	}

	bgt = (NUMTRANSMAPS/2) + (cursongcredit.trans / 2);
	x = R_InterpolateFixed(cursongcredit.old_x, cursongcredit.x);
	y +=  cursongcredit.y;

	if (bgt < NUMTRANSMAPS)
	{
		V_DrawFixedPatch(x, y - (2 * FRACUNIT),
			FRACUNIT, V_SNAPTOLEFT|snapflags|(bgt<<V_ALPHASHIFT),
			songcreditbg, NULL);
	}

	V_DrawRightAlignedThinStringAtFixed(x, y,
		V_ALLOWLOWERCASE|V_6WIDTHSPACE|V_SNAPTOLEFT|snapflags|(cursongcredit.trans<<V_ALPHASHIFT),
		cursongcredit.text);
}


// Heads up displays drawer, call each frame
//
void HU_Drawer(void)
{
	if (cv_vhseffect.value && (paused || (demo.playback && cv_playbackspeed.value > 1)))
		V_DrawVhsEffect(demo.rewinding);

	// draw chat string plus cursor
	if (chat_on)
	{
		if (!OLDCHAT)
			HU_DrawChat();
		else
			HU_DrawChat_Old();
	}
	else
	{
		typelines = 1;
		chat_scrolltime = 0;

		if (!OLDCHAT && cv_consolechat.value < 2 && netgame) // Don't display minimized chat if you set the mode to Window (Hidden)
			HU_drawMiniChat(); // draw messages in a cool fashion.
	}

	if (WipeInAction)
		return;

	if (cechotimer)
		HU_DrawCEcho();

	if (!( Playing() || demo.playback )
	 || gamestate == GS_INTERMISSION || gamestate == GS_CUTSCENE
	 || gamestate == GS_CREDITS      || gamestate == GS_EVALUATION
	 || gamestate == GS_VOTING || gamestate == GS_WAITINGPLAYERS
	 || gamestate == GS_BLANCREDITS || gamestate == GS_SECRETCREDITS
	) // SRB2kart
		return;

	// draw multiplayer rankings
	if (hu_showscores)
	{
		if (netgame || multiplayer)
		{
			if (LUA_HudEnabled(hud_rankings))
				HU_DrawRankings();
			if (renderisnewtic)
			{
				LUA_HUD_ClearDrawList(luahuddrawlist_scores);
				LUA_HookHUD(luahuddrawlist_scores, HUD_HOOK(scores));
			}
			LUA_HUD_DrawList(luahuddrawlist_scores);
		}

		if (demo.playback)
		{
			HU_DrawDemoInfo();
		}
	}

	if (gamestate != GS_LEVEL)
		return;

	// draw desynch text
	if (hu_redownloadinggamestate)
	{
		char resynch_text[14];
		uint32_t i;

		// Animate the dots
		strcpy(resynch_text, "Resynching");
		for (i = 0; i < (resynch_ticker / 16) % 4; i++)
			strcat(resynch_text, ".");

		V_DrawCenteredString(BASEVIDWIDTH/2, 180, V_YELLOWMAP | V_ALLOWLOWERCASE, resynch_text);
	}

	if (modeattacking && retrytime > 0)
	{
		int32_t strength = ((retrytime - 1 - NEWTICRATE/2)*10)/(NEWTICRATE/3);
		int32_t x = BASEVIDWIDTH/2, y = BASEVIDHEIGHT/2; // obviously incorrect values while we scrap hudinfo

		V_DrawThinString(x, y,
			((leveltime & 4) ? V_SKYMAP : V_BLUEMAP),
			"HOLD TO RETRY...");

		if (strength > 9)
			V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 0);
		else if (strength > 0)
			V_DrawFadeScreen(0, strength);
	}
}

//======================================================================
//                   IN-LEVEL MULTIPLAYER RANKINGS
//======================================================================

static int
Ping_gfx_num (int lag)
{
	if (lag < 2)
		return 0;
	else if (lag < 4)
		return 1;
	else if (lag < 7)
		return 2;
	else if (lag < 10)
		return 3;
	else
		return 4;
}

static int
Ping_gfx_color (int lag)
{
	if (lag < 2)
		return SKINCOLOR_JAWZ;
	else if (lag < 4)
		return SKINCOLOR_MINT;
	else if (lag < 7)
		return SKINCOLOR_GOLD;
	else if (lag < 10)
		return SKINCOLOR_RASPBERRY;
	else
		return SKINCOLOR_MAGENTA;
}

static int
PL_gfx_color (int pl, dboolean mode)
{
	if (mode)
	{
		if (pl < 2)
			return SKINCOLOR_GOLD;
		else if (pl < 4)
			return SKINCOLOR_ORANGE;
		else if (pl < 6)
			return SKINCOLOR_CRIMSON;
		else
			return SKINCOLOR_MAGENTA;
	}
	else
	{
		if (pl < 2)
			return 72;
		else if (pl < 4)
			return 54;
		else if (pl < 6)
			return 35;
		else
			return 181;
	}
}

static const uint8_t *
Ping_gfx_colormap (uint32_t ping, uint32_t lag)
{
	const uint8_t *colormap = R_GetTranslationColormap(TC_RAINBOW, Ping_gfx_color(lag), GTC_CACHE);

	if (servermaxping && ping > servermaxping && hu_tick < 4)
	{
		// flash ping red if too high
		colormap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_WHITE, GTC_CACHE);
	}

	return colormap;
}

static uint32_t
Ping_to_ms (uint32_t lag)
{
	return (uint32_t)(lag * (1000.00f / TICRATE));
}

static int32_t
HU_PingRowStep (void)
{
	int32_t h0 = pingmeasure[0] ? pingmeasure[0]->height : 7;
	int32_t h1 = pingmeasure[1] ? pingmeasure[1]->height : 7;
	return max(h0, h1) + 1;
}

int32_t HU_GetLocalPingExtraHeight(void)
{
	if (cv_pingmeasurement.value != 2)
		return 0;

	if (cv_pingstyle.value)
		return 7;

	return HU_PingRowStep();
}

static void HU_drawPingLine(int32_t x, int32_t y, uint32_t ping, uint32_t lag, uint8_t *colormap, int32_t flags, int32_t measureid)
{
	if (measureid == 1)
	{
		V_DrawMappedPatch(x+11 - pingmeasure[1]->width, y, flags, pingmeasure[1], colormap);
		V_DrawPingNum(x+11 - pingmeasure[1]->width, y, flags, Ping_to_ms(lag), Ping_gfx_colormap(ping, lag));
	}
	else
	{
		int32_t nx = V_DrawPingNum(x + 10, y, flags, lag, Ping_gfx_colormap(ping, lag));
		V_DrawMappedPatch(nx+1 - pingmeasure[0]->width, y, flags, pingmeasure[0], colormap);
	}
}

//
// HU_drawPingEx
//
void HU_drawPingEx(int32_t x, int32_t y, uint32_t ping, uint32_t mindelay, uint32_t pl, int32_t flags, dboolean icon, dboolean allow_both)
{
	int32_t gfxnum; // gfx to draw
	uint32_t lag = max(ping, mindelay);
	uint8_t *colormap = NULL;

	gfxnum = Ping_gfx_num(ping);

	if (pl)
	{
		if (icon)
		{
			V_DrawFill(
				-pinggfx[gfxnum]->leftoffset + x + 2 - 1,
			  -pinggfx[gfxnum]->topoffset + y - 1,
			  pinggfx[gfxnum]->width + 2,
			  pinggfx[gfxnum]->height + 2,
			  PL_gfx_color(pl, false) | flags
			);
		}
		colormap = R_GetTranslationColormap(TC_RAINBOW, PL_gfx_color(pl, true), GTC_CACHE);
	}

	if (icon)
	{
		V_DrawScaledPatch(x+2, y, flags, pinggfx[gfxnum]);
	}

	if (cv_pingmeasurement.value == 2 && allow_both)
	{
		HU_drawPingLine(x, y+11, ping, lag, colormap, flags, 0);
		HU_drawPingLine(x, y+11 + HU_PingRowStep(), ping, lag, colormap, flags, 1);
	}
	else
	{
		int32_t measureid = (cv_pingmeasurement.value == 1) ? 1 : 0;
		HU_drawPingLine(x, y+11, ping, lag, colormap, flags, measureid);
	}
}

//
// HU_drawPing
//
void HU_drawPing(int32_t x, int32_t y, uint32_t ping, uint32_t mindelay, uint32_t pl, int32_t flags, dboolean icon)
{
	HU_drawPingEx(x, y, ping, mindelay, pl, flags, icon, false);
}

//
// HU_drawOldPing
//
void HU_drawOldPing(int32_t x, int32_t y, uint32_t ping, uint32_t mindelay, uint32_t pl, int32_t flags)
{
	int32_t measure = cv_pingmeasurement.value;
	uint32_t lag = max(ping, mindelay);

	//SRB2/Kart v1.0 style
	uint8_t numbars = 0; // how many ping bars do we draw?
	uint8_t barcolor = 31; // color we use for the bars (green, yellow, red or black)
	int8_t i = 0;
	int8_t yoffset = 6;

	if (vid.width >= 640)	// how sad, we're using a shit resolution.
	{
		if (measure == 2)
		{
			V_DrawRightAlignedSmallString(x+12, y+13, flags, va("d%d", lag));
			V_DrawRightAlignedSmallString(x+12, y+20, V_ALLOWLOWERCASE|flags, va("%dms", Ping_to_ms(lag)));
		}
		else if (measure == 1)
		{
			V_DrawRightAlignedSmallString(x+12, y+13, V_ALLOWLOWERCASE|flags, va("%dms", Ping_to_ms(lag)));
		}
		else
		{
			V_DrawRightAlignedSmallString(x+12, y+13, flags, va("d%d", lag));
		}
	}

	if (cv_pingicon.value)
	{
		switch (lag)
		{
			case 0 ... 1:
				numbars = 3;
				barcolor = 215; // Blue
				break;
			case 2 ... 3:
				numbars = 3;
				barcolor = 184; // Green
				break;
			case 4 ... 6:
				numbars = 2;    // Apparently ternaries w/ multiple statements don't look good in C so I decided against it.
				barcolor = 103; // Yellow
				break;
			case 7 ... 9:
				numbars = 1;
				barcolor = 155; // Red
				break;
			default:            // Brazil
				numbars = 0;
				barcolor = 31;  // black
				break;
		}

		if (pl)
		{
			barcolor = 194; // make it purplish
			// bars get indirectly set earlier
		}

		for (i = 0; (i < 3); i++) // Draw the ping bar
		{
			V_DrawFill(x+2 *(i-1)+7, y+8+yoffset-4, 2, 8-yoffset, 31|flags);
			if (i < numbars)
				V_DrawFill(x+2 *(i-1)+7, y+8+yoffset-3, 1, 8-yoffset-1, barcolor|flags);

			yoffset -= 2;
		}
	}
}

void
HU_drawMiniPing (int32_t x, int32_t y, uint32_t ping, uint32_t lag, int32_t flags)
{
	patch_t *patch;
	int32_t w = BASEVIDWIDTH;

	// draw new graphics later.
	return;

	if (r_splitscreen > 1)
	{
		w /= 2;
	}

	if (ping <= lag)
		patch = pinglocal[1]; // stone shoe
	else
		patch = mping[Ping_gfx_num(ping)];

	if (( flags & V_SNAPTORIGHT ))
		x += ( w - LSBF_SHORT (patch->width) );

	V_DrawScaledPatch(x, y, flags, patch);
}

//
// HU_DrawSpectatorTicker
//
static inline void HU_DrawSpectatorTicker(void)
{
	int32_t i;
	int32_t length = 0, height = 174;
	int32_t totallength = 0, templength = -8;
	int32_t dupadjust = vid.scaledwidth, duptweak = (dupadjust - BASEVIDWIDTH)/2;

	for (i = 0; i < MAXPLAYERS; i++)
		if (playeringame[i] && players[i].spectator)
			totallength += (signed)strlen(player_names[i]) * 8 + 16;

	length -= (leveltime % (totallength + dupadjust+8));
	length += dupadjust;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].spectator)
		{
			char *pos;
			char initial[MAXPLAYERNAME+1];
			char current[MAXPLAYERNAME+1];
			int32_t len;

			len = ((signed)strlen(player_names[i]) * 8 + 16);

			strcpy(initial, player_names[i]);
			pos = initial;

			if (length >= -len)
			{
				if (length < -8)
				{
					uint8_t eatenchars = (uint8_t)(abs(length) / 8);

					if (eatenchars <= strlen(initial))
					{
						// Eat one letter off the left side,
						// then compensate the drawing position.
						pos += eatenchars;
						strcpy(current, pos);
						templength = ((length + 8) % 8);
					}
					else
					{
						strcpy(current, " ");
						templength = length;
					}
				}
				else
				{
					strcpy(current, initial);
					templength = length;
				}

				V_DrawString(templength - duptweak, height, V_TRANSLUCENT|V_ALLOWLOWERCASE, current);
			}

			if ((length += len) >= dupadjust+8)
				break;
		}
	}
}

//
// HU_DrawRankings
//
static void HU_DrawRankings(void)
{
	playersort_t tab[MAXPLAYERS];
	int32_t i, j, scorelines, hilicol, numplayersingame = 0;
	dboolean completed[MAXPLAYERS];
	uint32_t whiteplayer = MAXPLAYERS;

	V_DrawFadeScreen(0xFF00, 16); // A little more readable, and prevents cheating the fades under other circumstances.

	if (cons_menuhighlight.value)
		hilicol = cons_menuhighlight.value;
	else if (modeattacking)
		hilicol = V_ORANGEMAP;
	else
		hilicol = gametypes[gametype]->color;

	if (!demo.playback)
	{
		// draw the current map in the lower left
		char *maptitle = G_BuildMapTitle(gamemap);

		if (maptitle)
		{
			V_DrawString(4, 188, hilicol|V_SNAPTOBOTTOM|V_SNAPTOLEFT, maptitle);
			Z_Free(maptitle);
		}
		else
		{
			V_DrawString(4, 188, hilicol|V_SNAPTOBOTTOM|V_SNAPTOLEFT, "UNKNOWN");
		}
	}
	else
	{
		// draw the current gametype in the lower left
		V_DrawString(4, 188, hilicol|V_SNAPTOBOTTOM|V_SNAPTOLEFT, (modeattacking) ? "Record Attack" : gametypes[gametype]->name);
	}

	if ((gametypes[gametype]->rules & (GTR_TIMELIMIT|GTR_POINTLIMIT)) && !bossinfo.boss)
	{
		if ((gametypes[gametype]->rules & GTR_TIMELIMIT) && timelimitintics > 0)
		{
			uint32_t timeval = (timelimitintics + starttime + 1 - leveltime);
			if (timeval > timelimitintics+1)
				timeval = timelimitintics+1;
			timeval /= TICRATE;

			if (leveltime <= (timelimitintics + starttime))
			{
				V_DrawCenteredString(64, 8, 0, "TIME LEFT");
				V_DrawCenteredString(64, 16, hilicol, va("%u", timeval));
			}

			// overtime
			if (!players[consoleplayer].exiting && (leveltime > (timelimitintics + starttime + TICRATE/2)) && cv_overtime.value)
			{
				V_DrawCenteredString(64, 8, 0, "TIME LEFT");
				V_DrawCenteredString(64, 16, hilicol, "OVERTIME");
			}
		}

		if ((gametypes[gametype]->rules & GTR_POINTLIMIT) && cv_pointlimit.value > 0)
		{
			V_DrawCenteredString(256, 8, 0, "POINT LIMIT");
			V_DrawCenteredString(256, 16, hilicol, va("%d", cv_pointlimit.value));
		}
	}
	else
	{
		if (circuitmap)
		{
			V_DrawCenteredString(64, 8, 0, "LAP COUNT");
			V_DrawCenteredString(64, 16, hilicol, va("%d", numlaps));
		}
	}

	V_DrawCenteredString(256, 8, 0, "GAME SPEED");
	V_DrawCenteredString(256, 16, hilicol, kartspeed_cons_t[1+gamespeed].strvalue);

	// When you play, you quickly see your score because your name is displayed in white.
	// When playing back a demo, you quickly see who's the view.
	if (!r_splitscreen)
		whiteplayer = demo.playback ? displayplayers[0] : consoleplayer;

	scorelines = 0;
	memset(completed, 0, sizeof (completed));
	memset(tab, 0, sizeof (playersort_t)*MAXPLAYERS);

	for (i = 0; i < MAXPLAYERS; i++)
	{
		tab[i].num = -1;
		tab[i].name = NULL;
		tab[i].string[0] = '\0';

		if (!playeringame[i] || players[i].spectator || !players[i].mo)
			continue;

		numplayersingame++;
	}

	for (j = 0; j < numplayersingame; j++)
	{
		uint8_t lowestposition = MAXPLAYERS+1;
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (completed[i] || !playeringame[i] || players[i].spectator || !players[i].mo)
				continue;

			if (players[i].position >= lowestposition)
				continue;

			tab[scorelines].num = i;
			lowestposition = players[i].position;
		}

		i = tab[scorelines].num;

		completed[i] = true;

		tab[scorelines].name = player_names[i];
		strncpy(tab[scorelines].clan, players[i].clan, MAXCLANTAG);

		if (gametypes[gametype]->rules & GTR_CIRCUIT)
		{
			if (!players[i].exiting)
				snprintf(tab[scorelines].string, sizeof(tab[scorelines].string), "Lap %d", players[i].laps);
			else
			{
				int32_t min = G_TicsToMinutes(players[i].realtime, true);
				int32_t sec = G_TicsToSeconds(players[i].realtime);
				int32_t cen = G_TicsToCentiseconds(players[i].realtime);

				snprintf(tab[scorelines].string, sizeof(tab[scorelines].string), "%i'%02i\"%02i", min, sec, cen);
			}
		}
		else if (gametypes[gametype]->rules & GTR_POINTS)
			snprintf(tab[scorelines].string, sizeof(tab[scorelines].string), "%d", players[i].roundscore);

		scorelines++;

#if MAXPLAYERS > 16
	if (scorelines > 16)
		break; //dont draw past bottom of screen, show the best only
#endif
	}

	if (!grandprixinfo.gp && cv_newtabranking.value)
	{
		int32_t xoffset = K_DrawNeoTabRankings(0, 33, tab, scorelines, whiteplayer, hilicol, true);
		K_DrawServerDescrption(xoffset+10, 33);
	}
	else
	{
		K_DrawTabRankings(((scorelines > 8) ? 32 : 40), 33, tab, scorelines, whiteplayer, hilicol);
	}

	// draw spectators in a ticker across the bottom
	if (netgame && G_GametypeHasSpectators())
		HU_DrawSpectatorTicker();
}

// Interface to CECHO settings for the outside world, avoiding the
// expense (and security problems) of going via the console buffer.
void HU_ClearCEcho(void)
{
	cechotimer = 0;
}

void HU_SetCEchoDuration(int32_t seconds)
{
	cechoduration = seconds * TICRATE;
}

void HU_SetCEchoFlags(int32_t flags)
{
	// Don't allow cechoflags to contain any bits in V_PARAMMASK
	cechoflags = (flags & ~V_PARAMMASK);
}

void HU_DoCEcho(const char *msg)
{
	I_OutputMsg("%s\n", msg); // print to log

	strncpy(cechotext, msg, sizeof(cechotext));
	strncat(cechotext, "\\", sizeof(cechotext) - strlen(cechotext) - 1);
	cechotext[sizeof(cechotext) - 1] = '\0';
	cechotimer = cechoduration;
}
