// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2004-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  y_inter.c
/// \brief Tally screens, or "Intermissions" as they were formally called in Doom

#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "f_finale.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "i_net.h"
#include "i_video.h"
#include "p_tick.h"
#include "r_defs.h"
#include "r_skins.h"
#include "s_sound.h"
#include "st_stuff.h"
#include "v_video.h"
#include "w_wad.h"
#include "y_inter.h"
#include "z_zone.h"
#include "m_menu.h"
#include "m_misc.h"
#include "i_system.h"
#include "p_setup.h"

#include "r_local.h"
#include "p_local.h"

#include "m_cond.h" // condition sets
#include "lua_hook.h" // IntermissionThinker hook

#include "lua_hud.h"
#include "lua_hudlib_drawlist.h"

#include "m_random.h" // M_RandomKey
#include "g_input.h" // G_PlayerInputDown
#include "k_battle.h"
#include "k_boss.h"
#include "k_pwrlv.h"
#include "console.h" // cons_menuhighlight
#include "k_grandprix.h"
#include "r_fps.h" // R_GetTimeFrac

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

typedef struct
{
	char patch[9];
	 INT32 points;
	UINT8 display;
} y_bonus_t;

typedef struct
{
	INT32 *character[MAXPLAYERS]; // Winner's character #
	UINT16 *color[MAXPLAYERS]; // Winner's color #
	SINT8 num[MAXPLAYERS]; // Winner's player #
	char *name[MAXPLAYERS]; // Winner's name

	UINT8 numplayers; // Number of players being displayed

	char levelstring[64]; // holds levelnames up to 64 characters

	// SRB2kart
	INT32 increase[MAXPLAYERS]; // how much did the score increase by?
	UINT8 jitter[MAXPLAYERS]; // wiggle

	UINT32 val[MAXPLAYERS]; // Gametype-specific value
	UINT8 pos[MAXPLAYERS]; // player positions. used for ties

	boolean rankingsmode; // rankings mode
	boolean encore; // encore mode
} y_data;

static y_data data;

// graphics
static patch_t *bgtile = NULL;      // SPECTILE/SRB2BACK
static patch_t *interpic = NULL;    // custom picture defined in map header

static INT32 timer;

static boolean useinterpic;

static INT32 powertype = PWRLV_DISABLED;
static UINT8 *y_screenbuffer;

static INT32 intertic;
static INT32 endtic = -1;
static INT32 sorttic = -1;

intertype_t intertype = int_none;
intertype_t intermissiontypes[NUMGAMETYPES];

static huddrawlist_h luahuddrawlist_intermission;
static huddrawlist_h luahuddrawlist_vote;

static void Y_FollowIntermission(void);

static void Y_UnloadData(void);

// SRB2Kart: voting stuff
// Level images
typedef struct
{
	char str[62];
	UINT8 gtc;
	const char *gts;
	boolean encore;
} y_votelvlinfo;

// Clientside & splitscreen player info.
typedef struct
{
	SINT8 selection;
	UINT8 delay;
} y_voteplayer;

typedef struct
{
	y_voteplayer playerinfo[4];
	UINT8 ranim;
	UINT8 rtics;
	UINT8 roffset;
	UINT8 rsynctime;
	UINT8 rendoff;
	boolean loaded;
} y_voteclient;

// votescreen stuff
votescreen_t VoteScreen = {0};

static y_votelvlinfo levelinfo[13];
static y_voteclient voteclient;
static INT32 votetic;
static INT32 lastvotetic;
static INT32 voteendtic = -1;
static SINT8 votemax = 3;
static INT32 voterowmem = 0;
static boolean rowchange = false;
static boolean votenotyetpicked;

static void Y_UnloadVoteData(void);

//
// SRB2Kart - Y_CalculateMatchData and ancillary functions
//
static void Y_CompareTime(INT32 i)
{
	UINT32 val = ((players[i].pflags & PF_NOCONTEST || players[i].realtime == UINT32_MAX)
		? (UINT32_MAX-1) : players[i].realtime);

	if (!(val < data.val[data.numplayers]))
		return;

	data.val[data.numplayers] = val;
	data.num[data.numplayers] = i;
}

static void Y_CompareScore(INT32 i)
{
	UINT32 val = ((players[i].pflags & PF_NOCONTEST)
			? (UINT32_MAX-1) : players[i].roundscore);

	if (!(data.val[data.numplayers] == UINT32_MAX
	|| (!(players[i].pflags & PF_NOCONTEST) && val > data.val[data.numplayers])))
		return;

	data.val[data.numplayers] = val;
	data.num[data.numplayers] = i;
}

static void Y_CompareRank(INT32 i)
{
	INT32 increase = ((data.increase[i] == INT32_MIN) ? 0 : data.increase[i]);
	UINT32 score = players[i].score;

	if (powertype != PWRLV_DISABLED)
	{
		score = clientpowerlevels[i][powertype];
	}

	if (!(data.val[data.numplayers] == UINT32_MAX || (score - increase) > data.val[data.numplayers]))
		return;

	data.val[data.numplayers] = (score - increase);
	data.num[data.numplayers] = i;
}

static void Y_CalculateMatchData(UINT8 rankingsmode, void (*comparison)(INT32))
{
	INT32 i, j;
	boolean completed[MAXPLAYERS];
	INT32 numplayersingame = 0;

	// Initialize variables
	if (rankingsmode > 1)
		;
	else if ((data.rankingsmode = (boolean)rankingsmode))
	{
		sprintf(data.levelstring, "\x82*\x80 Total Rankings \x82*\x80");
		data.encore = false;
	}
	else
	{
		// set up the levelstring
		if (bossinfo.boss == true && bossinfo.enemyname)
		{
			snprintf(data.levelstring,
				sizeof data.levelstring,
				"\x82*\x80 %s \x82*\x80",
				bossinfo.enemyname);
		}
		else if (mapheaderinfo[prevmap]->levelflags & LF_NOZONE)
		{
			if (mapheaderinfo[prevmap]->actnum[0])
				snprintf(data.levelstring,
					sizeof data.levelstring,
					"\x82*\x80 %s %s \x82*\x80",
					mapheaderinfo[prevmap]->lvlttl, mapheaderinfo[prevmap]->actnum);
			else
				snprintf(data.levelstring,
					sizeof data.levelstring,
					"\x82*\x80 %s \x82*\x80",
					mapheaderinfo[prevmap]->lvlttl);
		}
		else
		{
			const char *zonttl = (mapheaderinfo[prevmap]->zonttl[0] ? mapheaderinfo[prevmap]->zonttl : "ZONE");
			if (mapheaderinfo[prevmap]->actnum[0])
				snprintf(data.levelstring,
					sizeof data.levelstring,
					"\x82*\x80 %s %s %s \x82*\x80",
					mapheaderinfo[prevmap]->lvlttl, zonttl, mapheaderinfo[prevmap]->actnum);
			else
				snprintf(data.levelstring,
					sizeof data.levelstring,
					"\x82*\x80 %s %s \x82*\x80",
					mapheaderinfo[prevmap]->lvlttl, zonttl);
		}

		data.levelstring[sizeof data.levelstring - 1] = '\0';

		data.encore = encoremode;

		memset(data.jitter, 0, sizeof (data.jitter));
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		data.val[i] = UINT32_MAX;

		if (!playeringame[i] || players[i].spectator)
		{
			data.increase[i] = INT32_MIN;
			continue;
		}

		if (!rankingsmode)
			data.increase[i] = INT32_MIN;

		numplayersingame++;
	}

	memset(data.color, 0, sizeof (data.color));
	memset(data.character, 0, sizeof (data.character));
	memset(completed, 0, sizeof (completed));
	data.numplayers = 0;

	for (j = 0; j < numplayersingame; j++)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator || completed[i])
				continue;

			comparison(i);
		}

		i = data.num[data.numplayers];

		completed[i] = true;

		data.color[data.numplayers] = &players[i].skincolor;
		data.character[data.numplayers] = &players[i].skin;
		data.name[data.numplayers] = player_names[i];

		if (data.numplayers && (data.val[data.numplayers] == data.val[data.numplayers-1]))
		{
			data.pos[data.numplayers] = data.pos[data.numplayers-1];
		}
		else
		{
			data.pos[data.numplayers] = data.numplayers+1;
		}

		if (!rankingsmode)
		{
			if ((powertype == PWRLV_DISABLED)
				&& (!(players[i].pflags & PF_NOCONTEST) || players[i].interpoints)
				&& (data.pos[data.numplayers] < (numplayersingame + spectateGriefed)))
			{
				// Online rank is handled further below in this file.
				data.increase[i] = players[i].interpoints ? players[i].interpoints : K_CalculateGPRankPoints(data.pos[data.numplayers], numplayersingame + spectateGriefed);
				players[i].score += data.increase[i];
			}

			if (demo.recording)
			{
				G_WriteStanding(
					data.pos[data.numplayers],
					data.name[data.numplayers],
					*data.character[data.numplayers],
					*data.color[data.numplayers],
					data.val[data.numplayers]
				);
			}
		}

		data.numplayers++;
	}
}

//
// Y_ConsiderScreenBuffer
//
// Draw a frame, and save it for the intermission background.
//
void Y_ConsiderScreenBuffer(void)
{
	Y_CleanupScreenBuffer();

	if (!D_RenderLevel())
		return; // we need a frame to copy!

	if (rendermode == render_soft)
	{
		y_screenbuffer = Z_Malloc(vid.width * vid.height, PU_STATIC, NULL);
		VID_BlitLinearScreen(vid.screens[0], y_screenbuffer, vid.width, vid.height, vid.width, vid.rowbytes);
	}
#ifdef HWRENDER
	else if (rendermode == render_opengl)
	{
		y_screenbuffer = Z_Malloc(0, PU_STATIC, NULL);
		HWR_MakeIntermissionBG();
	}
#endif
}

//
// Y_CleanupScreenBuffer
//
// Free all related memory.
//
void Y_CleanupScreenBuffer(void)
{
	// Who knows?
	if (y_screenbuffer == NULL)
		return;

	Z_Free(y_screenbuffer);
	y_screenbuffer = NULL;
}

//
// Y_IntermissionDrawer
//
// Called by D_Display. Nothing is modified here; all it does is draw. (SRB2Kart: er, about that...)
// Neat concept, huh?
//
void Y_IntermissionDrawer(void)
{
	INT32 i, whiteplayer = MAXPLAYERS, x = 4, hilicol = V_YELLOWMAP; // fallback

	if (intertype == int_none || rendermode == render_none)
		return;

	if (!useinterpic && y_screenbuffer == NULL
#ifdef HWRENDER
	// TODO resolution changes breaks the screentexture capture, I have no clue why
	&& rendermode != render_opengl
#endif
	)
		Y_ConsiderScreenBuffer();

	if (useinterpic)
		V_DrawScaledPatch(0, 0, 0, interpic);
	else if (y_screenbuffer != NULL)
	{
		if (rendermode == render_soft)
			VID_BlitLinearScreen(y_screenbuffer, vid.screens[0], vid.width, vid.height, vid.width, vid.rowbytes);
#ifdef HWRENDER
		else if (rendermode == render_opengl)
			HWR_DrawIntermissionBG();
#endif
	}
	else if (VoteScreen.bgpatch)
	{
		fixed_t hs = vid.width  * FRACUNIT / BASEVIDWIDTH;
		fixed_t vs = vid.height * FRACUNIT / BASEVIDHEIGHT;
		V_DrawStretchyFixedPatch(0, 0, hs, vs, V_NOSCALEPATCH, VoteScreen.bgpatch, NULL);
	}
	else // in case nothing else works...
		V_DrawPatchFill(bgtile);

	if (renderisnewtic)
	{
		LUA_HUD_ClearDrawList(luahuddrawlist_intermission);
		LUA_HookHUD(luahuddrawlist_intermission, HUD_HOOK(intermission));
	}
	LUA_HUD_DrawList(luahuddrawlist_intermission);

	if (!LUA_HudEnabled(hud_intermissiontally))
		goto skiptallydrawer;

	// Fade everything out
	V_DrawFadeScreen(0xFF00, 22);

	if (!r_splitscreen)
		whiteplayer = demo.playback ? displayplayers[0] : consoleplayer;

	if (cons_menuhighlight.value)
		hilicol = cons_menuhighlight.value;
	else if (modeattacking)
		hilicol = V_ORANGEMAP;
	else
	{
		if (gametypecolor[gametype])
			hilicol = gametypecolor[gametype];
		else
			hilicol = V_YELLOWMAP;
	}

	if (sorttic != -1 && intertic > sorttic)
	{
		INT32 count = (intertic - sorttic);

		if (count < 8)
			x -= ((((count<<FRACBITS) + R_GetTimeFrac(RTF_INTER)) * vid.width)>>FRACBITS) / (8 * vid.dupx);
		else if (count == 8)
			goto skiptallydrawer;
		else if (count < 16)
			x += (((((16 - count)<<FRACBITS) - R_GetTimeFrac(RTF_INTER)) * vid.width)>>FRACBITS) / (8 * vid.dupx);
	}

	if (intertype == int_race || intertype == int_battle || intertype == int_battletime)
	{
#define NUMFORNEWCOLUMN 8
		INT32 y = 41, gutter = ((data.numplayers > NUMFORNEWCOLUMN) ? 0 : (BASEVIDWIDTH/2));
		INT32 dupadjust = (vid.width/vid.dupx), duptweak = (dupadjust - BASEVIDWIDTH)/2;
		const char *timeheader;
		
		boolean manyplayers16 = (data.numplayers > NUMFORNEWCOLUMN*2);
		boolean manyplayers8 = (data.numplayers > NUMFORNEWCOLUMN);

		int y2;

		if (data.rankingsmode)
		{
			if (powertype == PWRLV_DISABLED)
			{
				timeheader = "RANK";
			}
			else
			{
				timeheader = "PWR.LV";
			}
		}
		else
		{
			switch (intertype)
			{
				case int_battle:
					timeheader = "SCORE";
					break;
				default:
					timeheader = "TIME";
					break;
			}
		}

		// draw the level name
		V_DrawCenteredString(-4 + x + BASEVIDWIDTH/2, 12, 0, data.levelstring);
		V_DrawFill((x-3) - duptweak, 34, dupadjust-2, 1, 0);

		if (data.encore)
			V_DrawCenteredString(-4 + x + BASEVIDWIDTH/2, 12-8, hilicol, "ENCORE MODE");

		if (manyplayers16)
		{
			V_DrawFill(x+101, 24, 1, 158, 0);
			V_DrawFill(x+207, 24, 1, 158, 0);
			V_DrawFill((x-3) - duptweak, 182, dupadjust-2, 1, 0);

			V_DrawRightAlignedString(x+152, 24, hilicol, timeheader);

			y = 37;
		}
		else if (manyplayers8)
		{
			V_DrawFill(x+156, 24, 1, 158, 0);
			V_DrawFill((x-3) - duptweak, 182, dupadjust-2, 1, 0);

			V_DrawCenteredString(x+6+(BASEVIDWIDTH/2), 24, hilicol, "#");
			V_DrawString(x+36+(BASEVIDWIDTH/2), 24, hilicol, "NAME");

			V_DrawRightAlignedString(x+152, 24, hilicol, timeheader);
		}
		else
		{
			V_DrawCenteredString(x+6, 24, hilicol, "#");
			V_DrawString(x+36, 24, hilicol, "NAME");
	
			V_DrawRightAlignedString(x+(BASEVIDWIDTH/2)+152, 24, hilicol, timeheader);
		}

		for (i = 0; i < data.numplayers; i++)
		{
			boolean dojitter = data.jitter[data.num[i]];
			data.jitter[data.num[i]] = 0;

			if (data.num[i] != MAXPLAYERS && playeringame[data.num[i]] && !players[data.num[i]].spectator)
			{
				char strtime[MAXPLAYERNAME+1];

				if (dojitter)
					y--;

				if (manyplayers16)
					V_DrawPingNum(x+6, y+2, 0, data.pos[i], NULL);
				else
					V_DrawCenteredString(x+6, y, 0, va("%d", data.pos[i]));

				if (data.color[i])
				{
					UINT8 *colormap = R_GetTranslationColormap(*data.character[i], *data.color[i], GTC_CACHE);
					patch_t *facerank = faceprefix[*data.character[i]][FACE_RANK];

					if (manyplayers16)
					{
						// fixed_t scale = K_UseHighResPortraits() ? FRACUNIT/4 : FRACUNIT/2;
						fixed_t scale = FRACUNIT/2;
						V_DrawFixedPatch((x+8)<<FRACBITS, (y+1)<<FRACBITS, scale, 0, facerank, colormap);
					}
					else
					{
						V_DrawMappedPatch(x+16+facerank->leftoffset, y-4+facerank->topoffset, 0, facerank, colormap);
					}
				}

				if (data.num[i] == whiteplayer && data.numplayers <= NUMFORNEWCOLUMN*2)
				{
					UINT8 cursorframe = (intertic / 4) % 8;
					patch_t *highlight = W_CachePatchName(va("K_CHILI%d", cursorframe+1), PU_CACHE);
					V_DrawScaledPatch(x+16+highlight->leftoffset, y-4+highlight->topoffset, 0, highlight);
				}

				if ((players[data.num[i]].pflags & PF_NOCONTEST) && players[data.num[i]].bot)
				{
					// RETIRED!!
					patch_t *retire = W_CachePatchName("K_NOBLNS", PU_CACHE);
					if (manyplayers16)
					{
						fixed_t scale = FRACUNIT/2;
						V_DrawFixedPatch(x+6, y-3, scale, 0, retire, NULL);
					}
					else
					{
						V_DrawScaledPatch(x+12, y-7, 0, retire);
					}
				}

				STRBUFCPY(strtime, data.name[i]);

				y2 = y;

				if (manyplayers16)
					V_DrawThinString(x+18, y, hilicol|V_ALLOWLOWERCASE|V_6WIDTHSPACE, strtime);
				else if (manyplayers8)
					V_DrawThinString(x+36, y2-1, ((data.num[i] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, strtime);
				else
					V_DrawString(x+36, y2, ((data.num[i] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE, strtime);

				if (data.rankingsmode)
				{
					if (powertype != PWRLV_DISABLED && !clientpowerlevels[data.num[i]][powertype])
					{
						// No power level (splitscreen guests)
						STRBUFCPY(strtime, "----");
					}
					else
					{
						if (data.increase[data.num[i]] != INT32_MIN)
						{
							// Checking player.interpoints so when "negative increase" reaches 0, it keeps the -
							char sign = players[data.num[i]].interpoints < 0 ? '-' : '+';

							if (powertype == PWRLV_DISABLED)
								snprintf(strtime, sizeof strtime, "(%c%02d)", sign, abs(data.increase[data.num[i]]));
							else
								snprintf(strtime, sizeof strtime, "(%d)", data.increase[data.num[i]]);

							if (manyplayers16)
								V_DrawRightAlignedThinString(x+83+gutter, y, V_6WIDTHSPACE, strtime);
							else if (manyplayers8)
								V_DrawRightAlignedThinString(x+133+gutter, y-1, V_6WIDTHSPACE, strtime);
							else
								V_DrawRightAlignedString(x+118+gutter, y, 0, strtime);
						}

						snprintf(strtime, sizeof strtime, "%d", data.val[i]);
					}

					if (manyplayers16)
						V_DrawRightAlignedThinString(x+100+gutter, y, V_6WIDTHSPACE, strtime);
					else if (manyplayers8)
						V_DrawRightAlignedThinString(x+152+gutter, y-1, V_6WIDTHSPACE, strtime);
					else
						V_DrawRightAlignedString(x+152+gutter, y, 0, strtime);
				}
				else
				{
					if (data.val[i] == (UINT32_MAX-1))
						V_DrawRightAlignedThinString(x+(manyplayers16 ? 100 : 152)+gutter, y-1, (manyplayers8 ? V_6WIDTHSPACE : 0), "NO CONTEST.");
					else
					{
						if (intertype == int_race || intertype == int_battletime)
						{
							snprintf(strtime, sizeof strtime, "%i'%02i\"%02i", G_TicsToMinutes(data.val[i], true),
							G_TicsToSeconds(data.val[i]), G_TicsToCentiseconds(data.val[i]));
							strtime[sizeof strtime - 1] = '\0';

							if (manyplayers16)
								V_DrawRightAlignedThinString(x+100+gutter, y, V_6WIDTHSPACE, strtime);
							else if (manyplayers8)
								V_DrawRightAlignedThinString(x+152+gutter, y-1, V_6WIDTHSPACE, strtime);
							else
								V_DrawRightAlignedString(x+152+gutter, y, 0, strtime);
						}
						else
						{
							if (manyplayers16)
								V_DrawRightAlignedThinString(x+100+gutter, y, V_6WIDTHSPACE, va("%i", data.val[i]));
							else if (manyplayers8)
								V_DrawRightAlignedThinString(x+152+gutter, y-1, V_6WIDTHSPACE, va("%i", data.val[i]));
							else
								V_DrawRightAlignedString(x+152+gutter, y, 0, va("%i", data.val[i]));
						}
					}
				}

				if (dojitter)
					y++;
			}
			else
				data.num[i] = MAXPLAYERS; // this should be the only field setting in this function

			if (manyplayers16)
			{
				y += 10;

				if (i % 14 == 13)
				{
					y = 37;
					x += BASEVIDWIDTH/3;
				}
			}
			else
			{
				y += 18;

				if (i == NUMFORNEWCOLUMN-1)
				{
					y = 41;
					x += BASEVIDWIDTH/2;
				}
			}

#undef NUMFORNEWCOLUMN
		}
	}

skiptallydrawer:
	if (!LUA_HudEnabled(hud_intermissionmessages))
		return;

	if (timer && grandprixinfo.gp == false && bossinfo.boss == false)
	{
		char *string;
		INT32 tickdown = (timer+1)/TICRATE;

		if (multiplayer && demo.playback)
			string = va("Replay ends in %d", tickdown);
		else
			string = va("%s starts in %d", cv_advancemap.string, tickdown);

		V_DrawCenteredString(BASEVIDWIDTH/2, 188, hilicol,
			string);
	}

	if ((demo.recording || demo.savemode == DSM_SAVED) && !demo.playback)
		switch (demo.savemode)
		{
		case DSM_NOTSAVING:
			V_DrawRightAlignedThinString(BASEVIDWIDTH - 2, 2, V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|hilicol, "Look Backward: Save replay");
			break;

		case DSM_SAVED:
			V_DrawRightAlignedThinString(BASEVIDWIDTH - 2, 2, V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|hilicol, "Replay saved!");
			break;

		case DSM_TITLEENTRY:
			ST_DrawDemoTitleEntry();
			break;

		default: // Don't render any text here
			break;
		}

	if ((speedscramble != -1) && (speedscramble != gamespeed))
		V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24, hilicol|V_ALLOWLOWERCASE|V_SNAPTOBOTTOM,
			va(M_GetText("Next race will be %s Speed!"), kartspeed_cons_t[1+speedscramble].strvalue));
}

//
// Y_Ticker
//
// Manages fake score tally for single player end of act, and decides when intermission is over.
//
void Y_Ticker(void)
{
	if (intertype == int_none)
		return;

	if (demo.recording)
	{
		if (demo.savemode == DSM_NOTSAVING && G_PlayerInputDown(0, gc_lookback, false))
			demo.savemode = DSM_TITLEENTRY;

		if (demo.savemode == DSM_WILLSAVE || demo.savemode == DSM_WILLAUTOSAVE)
			G_SaveDemo();
	}

	// Check for pause or menu up in single player
	if (paused || P_AutoPause())
		return;

	LUA_HOOK(IntermissionThinker);

	intertic++;

	// Team scramble code for team match and CTF.
	// Don't do this if we're going to automatically scramble teams next round.
	/*if (G_GametypeHasTeams() && cv_teamscramble.value && !cv_scrambleonchange.value && server)
	{
		// If we run out of time in intermission, the beauty is that
		// the P_Ticker() team scramble code will pick it up.
		if ((intertic % (TICRATE/7)) == 0)
			P_DoTeamscrambling();
	}*/

	// multiplayer uses timer (based on cv_inttime)
	if (timer)
	{
		if (!--timer)
		{
			Y_EndIntermission();
			G_AfterIntermission();
			return;
		}
	}
	// single player is hardcoded to go away after awhile
	else if (intertic == endtic)
	{
		Y_EndIntermission();
		G_AfterIntermission();
		return;
	}

	if (intertic < TICRATE || intertic & 1 || endtic != -1)
		return;

	if (intertype == int_race || intertype == int_battle || intertype == int_battletime)
	{
		{
			if (!data.rankingsmode && sorttic != -1 && (intertic >= sorttic + 8))
			{
				// Anything with post-intermission consequences here should also occur in Y_EndIntermission.
				K_RetireBots();
				Y_CalculateMatchData(1, Y_CompareRank);
			}

			if (data.rankingsmode && intertic > sorttic+16+(2*TICRATE))
			{
				INT32 q=0,r=0;
				boolean kaching = true;

				for (q = 0; q < data.numplayers; q++)
				{
					if (data.num[q] == MAXPLAYERS
						|| !data.increase[data.num[q]]
						|| data.increase[data.num[q]] == INT32_MIN)
					{
						continue;
					}

					r++;
					data.jitter[data.num[q]] = 1;

					if (powertype != PWRLV_DISABLED)
					{
						// Power Levels
						if (abs(data.increase[data.num[q]]) < 10)
						{
							// Not a lot of point increase left, just set to 0 instantly
							data.increase[data.num[q]] = 0;
						}
						else
						{
							SINT8 remove = 0; // default (should not happen)

							if (data.increase[data.num[q]] < 0)
								remove = -10;
							else if (data.increase[data.num[q]] > 0)
								remove = 10;

							// Remove 10 points at a time
							data.increase[data.num[q]] -= remove;

							// Still not zero, no kaching yet
							if (data.increase[data.num[q]] != 0)
								kaching = false;
						}
					}
					else
					{
						// Basic bitch points
						if (data.increase[data.num[q]])
						{
							INT32 diff = 1;
							INT32 increase = data.increase[data.num[q]];
							if (increase > 25)
								diff = increase/10;
							// This is wordy... But allows negative "increase"
							if (increase < 0)
								increase += diff;
							else
								increase -= diff;

							if (increase)
								kaching = false;
							data.increase[data.num[q]] = increase;
						}
					}
				}

				if (r)
				{
					S_StartSound(NULL, (kaching ? sfx_chchng : sfx_ptally));
					Y_CalculateMatchData(2, Y_CompareRank);
				}
				else
					endtic = intertic + 3*TICRATE; // 3 second pause after end of tally
			}
		}
	}
}

//
// Y_DetermineIntermissionType
//
// Determines the intermission type from the current gametype.
//
void Y_DetermineIntermissionType(void)
{
	// set to int_none initially
	intertype = int_none;

	if (gametype == GT_RACE)
		intertype = int_race;
	else if (gametype == GT_BATTLE)
	{
		if (grandprixinfo.gp == true && bossinfo.boss == false)
			intertype = int_none;
		else
		{
			UINT8 i = 0, nump = 0;
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (!playeringame[i] || players[i].spectator)
					continue;
				nump++;
			}
			intertype = (nump < 2 ? int_battletime : int_battle);
		}
	}
	else //if (intermissiontypes[gametype] != int_none)
		intertype = intermissiontypes[gametype];
}

//
// Y_StartIntermission
//
// Called by G_DoCompleted. Sets up data for intermission drawer/ticker.
//
void Y_StartIntermission(void)
{
	UINT8 i = 0, nump = 0;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		nump++;
	}

	intertic = -1;

#ifdef PARANOIA
	if (endtic != -1)
		I_Error("endtic is dirty");
#endif

	// set player Power Level type
	powertype = K_UsingPowerLevels();

	// determine the tic the intermission ends
	if (!K_CanChangeRules())
	{
		timer = 10*TICRATE;
	}
	else
	{
		timer = cv_inttime.value*TICRATE;
	}

	// determine the tic everybody's scores/PWR starts getting sorted
	sorttic = -1;
	if (!timer)
	{
		// Prevent a weird bug
		timer = 1; 
	}
	else if (nump < 2 && !netgame)
	{
		// No PWR/global score, skip it
		timer /= 2;
	}
	else
	{
		// Minimum two seconds for match results, then two second slideover approx halfway through
		sorttic = max((timer/2) - 2*TICRATE, 2*TICRATE);
	}

	if (intermissiontypes[gametype] != int_none)
		intertype = intermissiontypes[gametype];

	// We couldn't display the intermission even if we wanted to.
	// But we still need to give the players their score bonuses, dummy.
	//if (dedicated) return;

	// This should always exist, but just in case...
	if (prevmap >= nummapheaders || !mapheaderinfo[prevmap])
		I_Error("Y_StartIntermission: Internal map ID %d not found (nummapheaders = %d)", prevmap, nummapheaders);

	S_ShowMusicCredit(-30*FRACUNIT, 5*TICRATE, 0);

	switch (intertype)
	{
		case int_battle:
		case int_battletime:
		{
			if (timer > 1)
			{
				S_ChangeMusicInternal("racent", true); // loop it
				S_ShowMusicCredit(-30*FRACUNIT, 5*TICRATE, 0);
			}

			// Calculate who won
			if (intertype == int_battle)
			{
				Y_CalculateMatchData(0, Y_CompareScore);
				break;
			}
		}
		// FALLTHRU
		case int_race:
		{
			// Calculate who won
			Y_CalculateMatchData(0, Y_CompareTime);
			break;
		}

		case int_none:
		default:
			break;
	}

	LUA_HUD_DestroyDrawList(luahuddrawlist_intermission);
	luahuddrawlist_intermission = LUA_HUD_CreateDrawList();

	if (powertype != PWRLV_DISABLED)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			// Kind of a hack to do this here,
			// but couldn't think of a better way.
			data.increase[i] = K_FinalPowerIncrement(
				&players[i],
				clientpowerlevels[i][powertype],
				clientPowerAdd[i]
			);
		}

		K_CashInPowerLevels();
	}

	bgtile = W_CachePatchName("SRB2BACK", PU_STATIC);
	useinterpic = false;
	Automate_Run(AEV_INTERMISSIONSTART);
}

// ======

//
// Y_EndIntermission
//
void Y_EndIntermission(void)
{
	if (!data.rankingsmode)
	{
		K_RetireBots();
	}

	if (!dedicated)
		Y_UnloadData();

	endtic = -1;
	sorttic = -1;
	intertype = int_none;
}

//
// Y_FollowIntermission
//
static void Y_FollowIntermission(void)
{
	// This handles whether to play a post-level cutscene, end the game,
	// or simply go to the next level.
	// No need to duplicate the code here!
	G_AfterIntermission();
}

#define UNLOAD(x) {if ((x) != NULL) {Patch_Free(x);} x = NULL;}

//
// Y_UnloadData
//
static void Y_UnloadData(void)
{
	// unload the background patches
	UNLOAD(VoteScreen.bgpatch);
	UNLOAD(VoteScreen.widebgpatch);
	UNLOAD(bgtile);
	UNLOAD(interpic);
}

// SRB2Kart: Voting!

//
// Y_VoteScreenCheck
//
static void Y_VoteScreenCheck(void)
{
	strcpy(VoteScreen.Prefix, "INTS");

	if (VoteScreen.luaPrefix[0] != 0)
		strlcpy(VoteScreen.Prefix, VoteScreen.luaPrefix, sizeof(VoteScreen.Prefix));
	else if (gametype == GT_BATTLE)
		strcpy(VoteScreen.Prefix, "BTLS");

	VoteScreen.foundLuaVoteFrames = VoteScreen.foundLuaVoteWideFrames = 0;
	VoteScreen.currentAnimFrame = 0;

	INT32 i = 1;

	// check for lua vote background replacements
	for (;;)
	{
		// Check if the lumps exist (checking for VEXTR(N|W)xx for race and VEXTRB(N|W)xx for battle)
		boolean normalLumpExists = W_LumpExists(va("%sC%d", VoteScreen.Prefix, i));
		boolean wideLumpExists = W_LumpExists(va("%sW%d", VoteScreen.Prefix, i));

		if (normalLumpExists || wideLumpExists)
		{
			if (normalLumpExists)
				VoteScreen.foundLuaVoteFrames++;

			if (wideLumpExists)
				VoteScreen.foundLuaVoteWideFrames++;
		}
		else // If we don't find at least frame 1 (e.g VEXTRN1), let's just stop looking
			break;

		i++;
	}

	// non lua vote background handling
	boolean prefbattletype = ((votelevels[0][1] & ~0x80) == GT_BATTLE);
	VoteScreen.widebgpatch = W_CachePatchName((prefbattletype ? "BATTLSCW" : "INTERSCW"), PU_PATCH);
	VoteScreen.bgpatch = W_CachePatchName((prefbattletype ? "BATTLSCR" : "INTERSCR"), PU_PATCH);
}

//
// Y_VoteBackgroundDrawer
//
// Determines which patch drawer to use for scaling
//
static void Y_VoteBackgroundDrawer(patch_t *patch)
{
	switch (cv_votebgscaling.value)
	{
		case 1: // adaptive
			V_DrawAdaptiveScaledFullScreenPatch(patch);
			break;
		case 2: // vertical-fill
			V_DrawVerticallyScaledFullScreenPatch(patch);
			break;
		case 3: // horizontal-fill
			V_DrawHorizontallyScaledFullScreenPatch(patch);
			break;
		case 0: // vanilla
		default:
			V_DrawScaledPatch(((vid.width/2) / vid.dupx) - (patch->width/2),
							  (vid.height / vid.dupy) - patch->height,
							  V_SNAPTOTOP|V_SNAPTOLEFT, patch);
			break;
	}
}

//
// Y_DrawLuaVoteScreenPatch
//
// Handles votebackgrounds set by "setVoteBackground"
// Aswell as animated patches
//
static void Y_DrawLuaVoteScreenPatch(boolean widePatch)
{
	INT32 nextframe = 0;
	patch_t *votebg = NULL;
	char tempPrefix[6];
	const INT32 tempfoundAnimLuaVoteFrames = ((widePatch ? VoteScreen.foundLuaVoteWideFrames : VoteScreen.foundLuaVoteFrames) - 1);

	strcpy(tempPrefix, va("%s%s", VoteScreen.Prefix, (widePatch ? "W" : "C")));

	// Draw non animated patch
	if (!tempfoundAnimLuaVoteFrames)
	{
		votebg = W_CachePatchName(va("%s1", tempPrefix), PU_PATCH);
		Y_VoteBackgroundDrawer(votebg);
		return;
	}

	// Draw animated patch based on frame counter on vote screen

	// Just in case someone provides LESS widescreen frames than normal frames or vice versa, reset the frame counter to 0
	if (VoteScreen.currentAnimFrame > tempfoundAnimLuaVoteFrames)
		VoteScreen.currentAnimFrame = 0;

	nextframe = (VoteScreen.currentAnimFrame + 1);

	votebg = W_CachePatchName(va("%s%d", tempPrefix, nextframe), PU_PATCH);

	Y_VoteBackgroundDrawer(votebg);

	if (renderisnewtic && (votetic % 2 == 0) && !paused)
		VoteScreen.currentAnimFrame = (nextframe > tempfoundAnimLuaVoteFrames) ? 0 : nextframe;
}

//
// Y_DrawVoteScreenPatch
//

static void Y_DrawVoteScreenPatch(void)
{
	patch_t *votebg = NULL;
	const boolean widescreen = (vid.width / vid.dupx > 320);

	if (VoteScreen.foundLuaVoteWideFrames || VoteScreen.foundLuaVoteFrames)
	{
		Y_DrawLuaVoteScreenPatch(((widescreen && VoteScreen.foundLuaVoteWideFrames) || !VoteScreen.foundLuaVoteFrames));
		return;
	}

	// non widescreen patch
	votebg = VoteScreen.bgpatch;

	UINT8 prefgametype = (votelevels[0][1] & ~0x80);
	const boolean widebgreplaced = (prefgametype == GT_BATTLE) ? VoteScreen.replaced.widebattle : VoteScreen.replaced.widerace;
	const boolean bgreplaced = (prefgametype == GT_BATTLE) ? VoteScreen.replaced.battle : VoteScreen.replaced.race;

	// we check a bunch of stuff to always have a "valid" fallback
	if ((widescreen && (widebgreplaced || !bgreplaced))
	|| (!widescreen && (widebgreplaced && !bgreplaced)))
	{
		votebg = VoteScreen.widebgpatch; // widescreen patch
	}

	Y_VoteBackgroundDrawer(votebg);
}

static fixed_t Y_CalculatePicScale(fixed_t picscale, INT32 hypoti)
{
	picscale *= 10;
	picscale /= (40-hypoti);

	if ( ((hypoti % 5) == 1) || ((hypoti % 5) == 4) || ((hypoti % 5) == 2) )
	{
		if ((hypoti % 5) == 2) // scale DOWN the image
			picscale -= (hypoti*16);
		else
			picscale += (hypoti*2); // scale UP the image
	}

	return picscale;
}

//
// Y_VoteDrawer
//
// Draws the voting screen!
//
void Y_VoteDrawer(void)
{
	INT32 rowval, i, px, lvls, x, picdiff, y = 0, height = 0;
	UINT8 selected[12];
	fixed_t rubyheight = 0;
	fixed_t scale;
	patch_t *pic;
	INT16 mapnum;
	fixed_t picwidth = 160;

	// CEP: scale by screen hypotenuse for extra voting rows
	INT32 vidx = ((vid.width) / vid.dupx);
	INT32 vidy = ((vid.height) / vid.dupy);
	fixed_t hypotf = 0;
	INT32 hypoti = 0;

	INT32 numplayers = 0;
	boolean highplayers = false;

	// get the hypotenuse
	hypoti = (vidx*vidx) + (vidy*vidy);
	hypotf = FixedSqrt(hypoti);

	// convert the fixed_t back into an integer
	hypoti = ((hypotf*10)/FRACUNIT);

	if ((voterowmem != cv_votemaxrows.value) && (votemax != cv_votemaxrows.value)) // voting rows were changed(?)
	{
		CONS_Printf(M_GetText("Max rows will be changed to %d on the next votescreen.\n"), cv_votemaxrows.value); // notify the players
		voterowmem = cv_votemaxrows.value;
	}

	// divisor for rescaling
	INT32 hypotdiv = max(10, (40-hypoti));

	// readjust the picscale
	picwidth *= Y_CalculatePicScale(FRACUNIT, hypoti);

	if (rendermode == render_none)
		return;

	if (votetic >= voteendtic && voteendtic != -1)
		return;

	if (!voteclient.loaded)
		return;

	{
		static angle_t rubyfloattime = 0;
		rubyheight = FINESINE(rubyfloattime>>ANGLETOFINESHIFT);
		rubyfloattime += FixedMul(ANGLE_MAX/NEWTICRATE, renderdeltatics);
	}

	V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);

	// decides which votebg to draw and draws it
	Y_DrawVoteScreenPatch();

	rowval = (votemax*3)+((votemax > 1) ? (votemax - 1) : 0);
	for (i = 0; i < (rowval+1); i++) // First, we need to figure out the height of this thing...
	{
		UINT8 j;
		selected[i] = 0; // Initialize

		for (j = 0; j <= splitscreen; j++)
		{
			if (voteclient.playerinfo[j].selection == i)
				selected[i]++;
		}

		if (selected[i])
			height += 25; // 50
		else
			height += 25;

		if (i < rowval)
			height += 5-splitscreen;
	}

	height /= votemax;

	y = (200-height)/2;
	picdiff = 80*( max(0, (votemax-1) ) ); // let's draw these in reverse order
	lvls = -1; // shitty cheat

	for (i = 0; i < (rowval+1); i++)
	{
		const char *str;
		UINT8 j, color;

		// CEP: hack hell
		INT32 scaledpicdiff = ((picdiff*10)/hypotdiv);
		INT32 fillscale = 800/hypotdiv;
		INT32 hypotmod = (hypoti % 5); // hypotenuse mod 5, rescale the bounding box
		INT32 hypotadd = ((hypotmod > 1) ? (hypotmod/4) : hypotmod); // how much do we add the bounding box by?

		// integer scaling makes me want to DIE
		if (hypotmod == 3)
			hypotadd += (1);

		scaledpicdiff *= 3;
		scaledpicdiff /= 2; // 1.5

		if (i == rowval)
		{
			str = "RANDOM";
			mapnum = -1;
		}
		else
		{
			str = levelinfo[i].str;
			mapnum = votelevels[i][0];
		}
		scale = M_GetMapThumbnail(mapnum, &pic)/2;

		if (selected[i])
		{
			UINT8 sizeadd = selected[i];

			for (j = 0; j <= splitscreen; j++) // another loop for drawing the selection backgrounds in the right order, grumble grumble..
			{
				INT32 handy = y;
				UINT8 p;
				UINT8 *colormap;
				patch_t *thiscurs;

				if (voteclient.playerinfo[j].selection != i)
					continue;

				if (!splitscreen)
				{
					thiscurs = VoteScreen.cursor[0];
					p = consoleplayer;
					color = levelinfo[i].gtc;
					colormap = NULL;
				}
				else
				{
					switch (j)
					{
						case 1:
							thiscurs = VoteScreen.cursor[2];
							p = g_localplayers[1];
							break;
						case 2:
							thiscurs = VoteScreen.cursor[3];
							p = g_localplayers[2];
							break;
						case 3:
							thiscurs = VoteScreen.cursor[4];
							p = g_localplayers[3];
							break;
						default:
							thiscurs = VoteScreen.cursor[1];
							p = g_localplayers[0];
							break;
					}

					color = skincolors[players[p].skincolor].ramp[7];
					colormap = R_GetTranslationColormap(TC_DEFAULT, players[p].skincolor, GTC_CACHE);
				}

				if (votes[p] != -1 || players[p].spectator)
					continue;

				handy += 3*(3-splitscreen) + (13*j);
				V_DrawMappedPatch(BASEVIDWIDTH-(1600/hypotdiv)-scaledpicdiff, handy, V_SNAPTORIGHT, thiscurs, colormap);

				if (votetic % 10 < 4)
					V_DrawFill(BASEVIDWIDTH-(1200/hypotdiv)-sizeadd-scaledpicdiff, y-sizeadd, (fillscale + (sizeadd*2) + hypotadd), ((500/hypotdiv)+(sizeadd*2)) + hypotadd, 0|V_SNAPTORIGHT);
				else
					V_DrawFill(BASEVIDWIDTH-(1200/hypotdiv)-sizeadd-scaledpicdiff, y-sizeadd, (fillscale + (sizeadd*2) + hypotadd), ((500/hypotdiv)+(sizeadd*2)) + hypotadd, color|V_SNAPTORIGHT);

				sizeadd--;
			}

			if (!levelinfo[i].encore)
				V_DrawFixedPatch((BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff)<<FRACBITS, y<<FRACBITS, Y_CalculatePicScale(scale,hypoti)/2, V_SNAPTORIGHT, pic, 0);
			else
			{
				V_DrawFixedPatch(((BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff)<<FRACBITS) + (picwidth/2), y<<FRACBITS, Y_CalculatePicScale(scale,hypoti)/2, V_FLIP|V_SNAPTORIGHT, pic, 0);
				V_DrawFixedPatch(((BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff)<<FRACBITS) + (picwidth/4), (y<<FRACBITS) + (25<<(FRACBITS-1)) - rubyheight, Y_CalculatePicScale(FRACUNIT,hypoti), V_SNAPTORIGHT, VoteScreen.rubyicon, NULL);
			}

			V_DrawRightAlignedThinString(BASEVIDWIDTH-(420/hypotdiv)-scaledpicdiff, (400/hypotdiv)+y, V_SNAPTORIGHT|V_6WIDTHSPACE, str);

			if (levelinfo[i].gts)
			{
				INT32 w = V_ThinStringWidth(levelinfo[i].gts, V_SNAPTORIGHT)+1;
				w *= 10;
				w /= (hypotdiv);

				if (hypoti >= 16)
				{
					V_DrawFill(BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff, y+10, w+1, 2, V_SNAPTORIGHT|31);
					V_DrawFill(BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff, y, w, 11, V_SNAPTORIGHT|levelinfo[i].gtc);
					V_DrawDiag(BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff+w+1, y, 12, V_SNAPTORIGHT|31);
					V_DrawDiag(BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff+w, y, 11, V_SNAPTORIGHT|levelinfo[i].gtc);
					V_DrawThinString(BASEVIDWIDTH-(1188/hypotdiv)-scaledpicdiff, y+1, V_SNAPTORIGHT, levelinfo[i].gts);
				}
				else // literally almost entirely covers the map icon, let's just mark it red or something
				{
					V_DrawDiag(BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff, y, 8, V_SNAPTORIGHT|31);
					V_DrawDiag(BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff, y, 6, V_SNAPTORIGHT|levelinfo[i].gtc);
				}
			}

			y += ((25*10) / (hypotdiv/2));
			lvls += 1;

			// screen height isn't doing us any favors
			if (lvls >= 6) // loop over if we have an extra row
			{
				lvls = -2;
				picdiff -= 90; // yes, this will overlap; no, I don't plan to change it
				y = (200-height)/2;
				y -= 5;
			}
		}
		else
		{
			if (!levelinfo[i].encore)
				V_DrawFixedPatch((BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff)<<FRACBITS, y<<FRACBITS, Y_CalculatePicScale(scale,hypoti)/2, V_SNAPTORIGHT, pic, 0);
			else
			{
				V_DrawFixedPatch(((BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff)<<FRACBITS) + (picwidth/2), y<<FRACBITS, Y_CalculatePicScale(scale,hypoti)/2, V_FLIP|V_SNAPTORIGHT, pic, 0);
				V_DrawFixedPatch(((BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff)<<FRACBITS) + (picwidth/4), (y<<FRACBITS) + (25<<(FRACBITS-1)) - rubyheight, Y_CalculatePicScale(FRACUNIT,hypoti), V_SNAPTORIGHT, VoteScreen.rubyicon, NULL);
			}

			if (levelinfo[i].gts)
			{
				V_DrawDiag(BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff, y, 8, V_SNAPTORIGHT|31);
				V_DrawDiag(BASEVIDWIDTH-(1200/hypotdiv)-scaledpicdiff, y, 6, V_SNAPTORIGHT|levelinfo[i].gtc);
			}

			y += ((25*10) / (hypotdiv/2));
			lvls += 1;

			if (lvls >= 6)
			{
				lvls = -2;
				picdiff -= 90;
				y = (200-height)/2;
				y -= 5;
			}
		}

		y += 5-splitscreen;
		lvls += 1;

		if (lvls >= 6)
		{
			lvls = -2;
			picdiff -= 90;
			y = (200-height)/2;
			y -= 5;
		}
	}

	x = 20;
	y = 10;

	for (px = 0; px < MAXPLAYERS; px++)
	{
		if (playeringame[px])
		{
			numplayers++;
		}
	}

	highplayers = numplayers > 16;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		INT32 bigaddx = 60; INT32 bigaddy = 30;
		INT32 smalladdx = 42; INT32 smalladdy = 18;
		INT32 smallfaceheight = 0; INT32 bigfaceheight = 9;
		INT32 smallrectheight = 18; INT32 bigrectheight = 27;
		INT32 smallrubyoffset = 4<<FRACBITS;
		INT32 smallpicoffset = 4<<FRACBITS;

		if (dedicated && i == 0) // While leaving blank spots for non-existent players is largely intentional, the first spot *always* being blank looks a tad silly :V
			continue;

		if ((playeringame[i] && !players[i].spectator) && votes[i] != -1)
		{
			if (votes[i] >= ((votemax*3)+((votemax > 1) ? (votemax - 1) : 0)) && (i != pickedvote || voteendtic == -1))
				mapnum = -1; // randomlvl
			else
				mapnum = votelevels[votes[i]][0];

			scale = M_GetMapThumbnail(mapnum, &pic)/8;

			if (!timer && i == voteclient.ranim)
			{
				V_DrawScaledPatch(x-18, y+(highplayers ? 2 : bigfaceheight), V_SNAPTOLEFT, VoteScreen.cursor[0]);
				if (voteendtic != -1 && !(votetic % 4))
					V_DrawFill(x-1, y-1, 42, (highplayers ? smallrectheight : bigrectheight), 0|V_SNAPTOLEFT);
				else
					V_DrawFill(x-1, y-1, 42, (highplayers ? smallrectheight : bigrectheight), levelinfo[votes[i]].gtc|V_SNAPTOLEFT);
			}

			if (highplayers)
			{
				V_SetClipRect(x<<FRACBITS, y<<FRACBITS, 48<<FRACBITS, 16<<FRACBITS, V_SNAPTOLEFT);
			}
			if (!levelinfo[votes[i]].encore)
				V_DrawFixedPatch(x<<FRACBITS, (y<<FRACBITS) - (highplayers ? smallpicoffset : 0), scale, V_SNAPTOLEFT, pic, NULL);
			else
			{
				V_DrawFixedPatch((x+FixedMul(pic->width, scale))<<FRACBITS, (y<<FRACBITS) - (highplayers ? smallpicoffset : 0), scale, V_SNAPTOLEFT|V_FLIP, pic, NULL);
				V_DrawFixedPatch((x+20)<<FRACBITS, (y<<FRACBITS) + (25<<(FRACBITS-1)) - rubyheight - (highplayers ? smallrubyoffset : 0), FRACUNIT/2, V_SNAPTOLEFT, VoteScreen.rubyicon, NULL);
			}
			if (highplayers)
			{
				V_ClearClipRect();
			}

			if (levelinfo[votes[i]].gts)
			{
				V_DrawDiag(x, y, 8, V_SNAPTOLEFT|31);
				V_DrawDiag(x, y, 6, V_SNAPTOLEFT|levelinfo[votes[i]].gtc);
			}

			if (players[i].skincolor)
			{
				UINT8 *colormap = R_GetTranslationColormap(players[i].skin, players[i].skincolor, GTC_CACHE);
				patch_t *facerank = faceprefix[players[i].skin][FACE_RANK];

				V_DrawMappedPatch(x+24+facerank->leftoffset, y+(highplayers ? smallfaceheight : bigfaceheight)+facerank->topoffset, V_SNAPTOLEFT, facerank, colormap);
			}

			if (!splitscreen && i == consoleplayer)
			{
				UINT8 cursorframe = (votetic / 4) % 8;
				patch_t *highlight = W_CachePatchName(va("K_CHILI%d", cursorframe+1), PU_CACHE);
				V_DrawScaledPatch(x+24+highlight->leftoffset, y+(highplayers ? smallfaceheight : bigfaceheight)+highlight->topoffset, V_SNAPTOLEFT, highlight);
			}
		}

		y += highplayers ? smalladdy : bigaddy;

		if (y > BASEVIDHEIGHT-40)
		{
			x += highplayers ? smalladdx : bigaddx;
			y = 10;
		}
	}

	if (timer)
	{
		INT32 hilicol, tickdown = (timer+1)/TICRATE;
		if (cons_menuhighlight.value)
			hilicol = cons_menuhighlight.value;
		else
		{
			if (gametypecolor[gametype])
				hilicol = gametypecolor[gametype];
			else
				hilicol = V_YELLOWMAP;
		}
		V_DrawCenteredString(BASEVIDWIDTH/2, 188, hilicol,
			va("Vote ends in %d", tickdown));
	}

	lastvotetic = votetic;

	if (renderisnewtic)
	{
		LUA_HUD_ClearDrawList(luahuddrawlist_vote);
		LUA_HookHUD(luahuddrawlist_vote, HUD_HOOK(vote));
	}
	LUA_HUD_DrawList(luahuddrawlist_vote);
}

//
// Y_VoteStop
//
// Vote screen's selection stops moving
//
SINT8 deferredlevel = 0;
static void Y_VoteStops(SINT8 pick, SINT8 level)
{
	nextmap = votelevels[level][0];

	//if (level == 4)
		//S_StartSound(NULL, sfx_noooo2); // gasp
	if (mapheaderinfo[nextmap] && (mapheaderinfo[nextmap]->menuflags & LF2_HIDEINMENU))
		S_StartSound(NULL, sfx_noooo1); // this is bad
	else if (netgame && P_IsLocalPlayer(&players[pick]))
		S_StartSound(NULL, sfx_yeeeah); // yeeeah!
	else
		S_StartSound(NULL, sfx_kc48); // just a cool sound

	if (gametype != votelevels[level][1])
	{
		INT16 lastgametype = gametype;
		G_SetGametype(votelevels[level][1]);
		D_GameTypeChanged(lastgametype);
		forceresetplayers = true;
	}

	deferencoremode = (levelinfo[level].encore);
}

//
// Y_VoteTicker
//
// Vote screen thinking :eggthinking:
//
void Y_VoteTicker(void)
{
	INT32 i;
	boolean everyone_voted;

	if (paused || P_AutoPause() || !voteclient.loaded)
		return;

	LUA_HOOK(VoteThinker);

	votetic++;

	if (votetic == voteendtic)
	{
		Y_EndVote();
		Y_FollowIntermission();
		return;
	}

	for (i = 0; i < MAXPLAYERS; i++) // Correct votes as early as possible, before they're processed by the game at all
	{
		if (!playeringame[i] || players[i].spectator)
			votes[i] = -1; // Spectators are the lower class, and have effectively no voice in the government. Democracy sucks.
		else if (pickedvote != -1 && votes[i] == -1)
			votes[i] = (votemax*3)+((votemax > 1) ? (votemax - 1) : 0); // Slow people get random
	}

	if (server && pickedvote != -1 && votes[pickedvote] == -1) // Uh oh! The person who got picked left! Recalculate, quick!
		D_PickVote();

	if (!votetic)
	{
		S_ChangeMusicInternal("vote", true);
		S_ShowMusicCredit(0, 5*TICRATE, 0);
	}

	if (timer)
		timer--;

	if (pickedvote != -1)
	{
		timer = 0;
		voteclient.rsynctime++;

		if (voteendtic == -1)
		{
			UINT8 tempvotes[MAXPLAYERS];
			UINT8 numvotes = 0;

			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (votes[i] == -1)
					continue;
				tempvotes[numvotes] = i;
				numvotes++;
			}

			if (numvotes < 1) // Whoops! Get outta here.
			{
				Y_EndVote();
				Y_FollowIntermission();
				return;
			}

			voteclient.rtics--;

			if (voteclient.rtics <= 0)
			{
				voteclient.roffset++;
				voteclient.rtics = min(20, (3*voteclient.roffset/4)+5);
				S_StartSound(NULL, sfx_kc39);
			}

			if (voteclient.rendoff == 0 || voteclient.roffset < voteclient.rendoff)
				voteclient.ranim = tempvotes[((pickedvote + voteclient.roffset) % numvotes)];

			if (voteclient.roffset >= 20)
			{
				if (voteclient.rendoff == 0)
				{
					if (voteclient.rsynctime % 51 == 0) // Song is 1.45 seconds long (sorry @ whoever wants to replace it in a music wad :V)
					{
						for (i = 5; i >= 3; i--) // Find a suitable place to stop
						{
							if (tempvotes[((pickedvote + voteclient.roffset + i) % numvotes)] == pickedvote)
							{
								voteclient.rendoff = voteclient.roffset+i;
								if (M_RandomChance(FRACUNIT/32)) // Let it cheat occasionally~
									voteclient.rendoff++;
								S_ChangeMusicInternal("voteeb", false);
								S_ShowMusicCredit(0, 5*TICRATE, 0);
								break;
							}
						}
					}
				}
				else if (voteclient.roffset >= voteclient.rendoff)
				{
					voteendtic = votetic + (3*TICRATE);
					Y_VoteStops(pickedvote, deferredlevel);
				}
			}
		}
		else
			voteclient.ranim = pickedvote;
	}
	else if (votenotyetpicked)
	{
		if (votetic < 3*(NEWTICRATE/7)) // give it some time before letting you control it :V
			return;

		/*
		The vote ended, but it will take at least a tic for that to reach us from
		the server. Don't let me change the vote now, it won't matter anyway!
		*/
		if (timer)
		{
			for (i = 0; i <= splitscreen; i++)
			{
				UINT8 p;
				boolean pressed = false;
				SINT8 votewrap = 0;

				if (votemax == 2)
					votewrap = 3;
				else if (votemax == 3)
					votewrap = 7;

				switch (i)
				{
					case 1:
						p = g_localplayers[1];
						break;
					case 2:
						p = g_localplayers[2];
						break;
					case 3:
						p = g_localplayers[3];
						break;
					default:
						p = consoleplayer;
						break;
				}

				if (voteclient.playerinfo[i].delay)
					voteclient.playerinfo[i].delay--;

				if ((playeringame[p] && !players[p].spectator)
						&& !voteclient.playerinfo[i].delay
						&& pickedvote == -1 && votes[p] == -1)
				{
					if (G_PlayerInputDown(i, gc_aimforward, false))
					{
						voteclient.playerinfo[i].selection--;
						pressed = true;
					}

					if (G_PlayerInputDown(i, gc_aimbackward, false) && pressed == false)
					{
						voteclient.playerinfo[i].selection++;
						pressed = true;
					}

					if (votemax > 1) // only allow side-movements for multi-row selections
					{
						// HORRIBLE hack, my GOD
						if (G_PlayerInputDown(i, gc_turnright, false) && !pressed) // move right
						{
							if (voteclient.playerinfo[i].selection <= votewrap)
								voteclient.playerinfo[i].selection += 4;
							else
								voteclient.playerinfo[i].selection -= ((votemax-1)*4);
							pressed = true;
						}

						if (G_PlayerInputDown(i, gc_turnleft, false) && !pressed) // move left
						{
							if (voteclient.playerinfo[i].selection > 3)
								voteclient.playerinfo[i].selection -= 4;
							else
								voteclient.playerinfo[i].selection += ((votemax-1)*4);
							pressed = true;
						}
					}

					if (voteclient.playerinfo[i].selection < 0)
						voteclient.playerinfo[i].selection = ((votemax*3)+((votemax > 1) ? (votemax-1) : 0) );
					if (voteclient.playerinfo[i].selection > ((votemax*3)+((votemax > 1) ? (votemax-1) : 0)) )
						voteclient.playerinfo[i].selection = 0;

					if (G_PlayerInputDown(i, gc_accelerate, false) && pressed == false)
					{
						D_ModifyClientVote(consoleplayer, voteclient.playerinfo[i].selection, i);
						pressed = true;
					}
				}

				if (pressed)
				{
					S_StartSound(NULL, sfx_kc4a);
					voteclient.playerinfo[i].delay = NEWTICRATE/7;
				}
			}
		}

		if (server)
		{
			everyone_voted = true;/* the default condition */

			if (timer == 0)
			{
				for (i = 0; i < MAXPLAYERS; i++)
				{
					if ((playeringame[i] && !players[i].spectator) && votes[i] == -1)
						votes[i] = (votemax*3)+((votemax > 1) ? (votemax-1) : 0);
				}
			}
			else
			{
				for (i = 0; i < MAXPLAYERS; i++)
				{
					if ((playeringame[i] && !players[i].spectator) && votes[i] == -1)
					{
						if (players[i].bot)
						{
							if (( M_RandomFixed() % 100 ) == 0)
								D_ModifyClientVote(i, M_RandomKey(4), 0);
						}

						if (votes[i] == -1)
							everyone_voted = false;
					}
				}
			}

			if (everyone_voted)
			{
				timer = 0;
				if (voteendtic == -1)
				{
					votenotyetpicked = false;/* don't pick vote twice */
					D_PickVote();
				}
			}
		}
	}
}

//
// Y_StartVote
//
// MK online style voting screen, appears after intermission
//

static void Y_InitVoteDrawing(void)
{
	if (dedicated)
		return;

	// setup the background patches
	Y_VoteScreenCheck();

	VoteScreen.cursor[0] = W_CachePatchName("M_CURSOR", PU_PATCH);
	VoteScreen.cursor[1] = W_CachePatchName("P1CURSOR", PU_PATCH);
	VoteScreen.cursor[2] = W_CachePatchName("P2CURSOR", PU_PATCH);
	VoteScreen.cursor[3] = W_CachePatchName("P3CURSOR", PU_PATCH);
	VoteScreen.cursor[4] = W_CachePatchName("P4CURSOR", PU_PATCH);

	VoteScreen.rubyicon  = W_CachePatchName("RUBYICON", PU_PATCH);
}

void Y_StartVote(void)
{
	INT32 i = 0;
	INT32 rowval = (cv_votemaxrows.value*3)+((cv_votemaxrows.value > 1) ? (cv_votemaxrows.value - 1) : 0);

	votemax = cv_votemaxrows.value; // can we please avoid SIGSEGVs
	voterowmem = cv_votemaxrows.value; // this is just for the notif system
	rowchange = false;

	votetic = -1;

#ifdef PARANOIA
	if (voteendtic != -1)
		I_Error("voteendtic is dirty");
#endif

	// cache vote patches
	Y_InitVoteDrawing();

	timer = cv_votetime.value*TICRATE;
	pickedvote = -1;

	votenotyetpicked = true;

	for (i = 0; i < 3; i++)
	{
		voteclient.playerinfo[i].selection = 0;
		voteclient.playerinfo[i].delay = 0;
	}

	voteclient.ranim = 0;
	voteclient.rtics = 1;
	voteclient.roffset = 0;
	voteclient.rsynctime = 0;
	voteclient.rendoff = 0;

	for (i = 0; i < MAXPLAYERS; i++)
		votes[i] = -1;

	for (i = 0; i < (rowval + 1); i++)
	{
		// set up the encore
		levelinfo[i].encore = (votelevels[i][1] & VOTEMODIFIER_ENCORE);
		votelevels[i][1] &= ~VOTEMODIFIER_ENCORE;

		// set up the levelstring
		if (mapheaderinfo[votelevels[i][0]]->levelflags & LF_NOZONE || !mapheaderinfo[votelevels[i][0]]->zonttl[0])
		{
			if (mapheaderinfo[votelevels[i][0]]->actnum[0])
				snprintf(levelinfo[i].str,
					sizeof levelinfo[i].str,
					"%s %s",
					mapheaderinfo[votelevels[i][0]]->lvlttl, mapheaderinfo[votelevels[i][0]]->actnum);
			else
				snprintf(levelinfo[i].str,
					sizeof levelinfo[i].str,
					"%s",
					mapheaderinfo[votelevels[i][0]]->lvlttl);
		}
		else
		{
			if (mapheaderinfo[votelevels[i][0]]->actnum[0])
				snprintf(levelinfo[i].str,
					sizeof levelinfo[i].str,
					"%s %s %s",
					mapheaderinfo[votelevels[i][0]]->lvlttl, mapheaderinfo[votelevels[i][0]]->zonttl, mapheaderinfo[votelevels[i][0]]->actnum);
			else
				snprintf(levelinfo[i].str,
					sizeof levelinfo[i].str,
					"%s %s",
					mapheaderinfo[votelevels[i][0]]->lvlttl, mapheaderinfo[votelevels[i][0]]->zonttl);
		}

		levelinfo[i].str[sizeof levelinfo[i].str - 1] = '\0';

		// set up the gtc and gts
		levelinfo[i].gtc = G_GetGametypeColor(votelevels[i][1]);
		if (i == 2 && votelevels[i][1] != votelevels[0][1])
			levelinfo[i].gts = gametype_cons_t[votelevels[i][1]].strvalue;
		else
			levelinfo[i].gts = NULL;
	}

	voteclient.loaded = true;
	Automate_Run(AEV_VOTESTART);

	LUA_HUD_DestroyDrawList(luahuddrawlist_vote);
	luahuddrawlist_vote = LUA_HUD_CreateDrawList();
}

//
// Y_EndVote
//
void Y_EndVote(void)
{
	Y_UnloadVoteData();
	voteendtic = -1;
}

//
// Y_UnloadVoteData
//
static void Y_UnloadVoteData(void)
{
	voteclient.loaded = false;

	if (dedicated)
		return;

	UNLOAD(VoteScreen.widebgpatch);
	UNLOAD(VoteScreen.bgpatch);
	UNLOAD(VoteScreen.cursor[0]);
	UNLOAD(VoteScreen.cursor[1]);
	UNLOAD(VoteScreen.cursor[2]);
	UNLOAD(VoteScreen.cursor[3]);
	UNLOAD(VoteScreen.cursor[4]);
	UNLOAD(VoteScreen.rubyicon);
}

//
// Y_SetupVoteFinish
//
void Y_SetupVoteFinish(SINT8 pick, SINT8 level)
{
	if (!voteclient.loaded)
		return;

	if (pick == -1) // No other votes? We gotta get out of here, then!
	{
		Y_EndVote();
		Y_FollowIntermission();
		return;
	}

	if (pickedvote == -1)
	{
		INT32 i;
		SINT8 votecompare = -1;
		INT32 endtype = 0;

		voteclient.rsynctime = 0;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if ((playeringame[i] && !players[i].spectator) && votes[i] == -1)
				votes[i] = (votemax*3)+((votemax > 1) ? (votemax - 1) : 0);

			if (votes[i] == -1 || endtype > 1) // Don't need to go on
				continue;

			if (endtype == 2)
				continue;

			if (votecompare == -1)
			{
				votecompare = votes[i];
				endtype = 1;
			}
			else if (votes[i] != votecompare)
				endtype = 2;
		}

		if (endtype == 1) // Only one unique vote, so just end it immediately.
		{
			voteendtic = votetic + (5*TICRATE);
			S_ChangeMusicInternal("voteeb", false);
			S_ShowMusicCredit(0, 5*TICRATE, 0);
			Y_VoteStops(pick, level);
		}
		else if (endtype == 0) // Might as well put this here, too.
		{
			Y_EndVote();
			Y_FollowIntermission();
			return;
		}
		else
		{
			S_ChangeMusicInternal("voteea", true);
			S_ShowMusicCredit(0, 5*TICRATE, 0);
		}
	}

	deferredlevel = level;
	pickedvote = pick;
	timer = 0;
}
