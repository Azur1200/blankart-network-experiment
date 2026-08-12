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
#include "k_hud.h"
#include "k_itemlist.hpp"
#include "k_vote.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

#define ITEMLIST_PLAYER_YOFFSET 9
#define ITEMLIST_SCROLLSPEED (3 * FRACUNIT / 4)
#define ITEMLIST_SCROLLDELAY (3 * TICRATE)
#define ITEMLIST_SCROLLREPEAT 1

typedef struct
{
	char patch[9];
	 int32_t points;
	uint8_t display;
} y_bonus_t;

typedef struct
{
	int32_t *character[MAXPLAYERS]; // Winner's character #
	uint16_t *color[MAXPLAYERS]; // Winner's color #
	int8_t num[MAXPLAYERS]; // Winner's player #
	char *name[MAXPLAYERS]; // Winner's name

	uint8_t numplayers; // Number of players being displayed

	char levelstring[64]; // holds levelnames up to 64 characters

	// SRB2kart
	int32_t increase[MAXPLAYERS]; // how much did the score increase by?
	uint8_t jitter[MAXPLAYERS]; // wiggle

	uint32_t val[MAXPLAYERS]; // Gametype-specific value
	uint8_t pos[MAXPLAYERS]; // player positions. used for ties

	dboolean rankingsmode; // rankings mode
	dboolean itemrolls;
	dboolean encore; // encore mode
} y_data;

static y_data data;

// graphics
static patch_t *bgtile = NULL;      // SPECTILE/SRB2BACK
static patch_t *interpic = NULL;    // custom picture defined in map header

static int32_t timer;

static dboolean useinterpic;

static int32_t powertype = PWRLV_DISABLED;
static uint8_t *y_screenbuffer;

static int32_t intertic;
static int32_t endtic = -1;
static int32_t sorttic = -1;
static int32_t rolltic = -1;

static fixed_t listscroll_length = 0;
static dboolean listscroll_reverse = false;
static int32_t listscroll_delay = 0;
static fixed_t xscroll = 0;

intertype_t intertype = int_none;

static huddrawlist_h luahuddrawlist_intermission;

static void Y_UnloadData(void);

//
// SRB2Kart - Y_CalculateMatchData and ancillary functions
//
static void Y_CompareTime(int32_t i)
{
	uint32_t val = ((players[i].pflags & PF_NOCONTEST || players[i].realtime == UINT32_MAX)
		? (UINT32_MAX-1) : players[i].realtime);

	if (!(val < data.val[data.numplayers]))
		return;

	data.val[data.numplayers] = val;
	data.num[data.numplayers] = i;
}

static void Y_CompareScore(int32_t i)
{
	uint32_t val = ((players[i].pflags & PF_NOCONTEST)
			? (UINT32_MAX-1) : players[i].roundscore);

	if (!(data.val[data.numplayers] == UINT32_MAX
	|| (!(players[i].pflags & PF_NOCONTEST) && val > data.val[data.numplayers])))
		return;

	data.val[data.numplayers] = val;
	data.num[data.numplayers] = i;
}

static void Y_CompareRank(int32_t i)
{
	int32_t increase = ((data.increase[i] == INT32_MIN) ? 0 : data.increase[i]);
	uint32_t score = players[i].score;

	if (powertype != PWRLV_DISABLED)
	{
		score = clientpowerlevels[i][powertype];
	}

	if (!(data.val[data.numplayers] == UINT32_MAX || (score - increase) > data.val[data.numplayers]))
		return;

	data.val[data.numplayers] = (score - increase);
	data.num[data.numplayers] = i;
}

static void Y_CalculateMatchData(uint8_t rankingsmode, void (*comparison)(int32_t))
{
	int32_t i, j;
	dboolean completed[MAXPLAYERS];
	int32_t numplayersingame = 0;

	// Initialize variables
	if (rankingsmode > 1)
		;
	else if ((data.rankingsmode = (dboolean)rankingsmode))
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
		data.itemrolls = Y_ItemListActive();

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

dboolean Y_ItemListActive(void)
{
	uint8_t i = 0, nump = 0;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		nump++;
	}

	return ((itemlistactive) && (nump > 1));
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
		HWR_DrawIntermissionBG();
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
	int32_t i, whiteplayer = MAXPLAYERS, x = 4, hilicol = V_YELLOWMAP; // fallback
	int32_t xx = x, x_base = x;

	if (intertype == int_none || rendermode == render_none)
		return;

	//int32_t w = vid.scaledwidth;
	//int32_t h = (vid.height / vid.dup);

	//const int32_t vidxdiff = (w - BASEVIDWIDTH) / 2;
	//const int32_t vidydiff = (h - BASEVIDHEIGHT) / 2;

	if (!useinterpic && y_screenbuffer == NULL
#ifdef HWRENDER
	// TODO resolution changes breaks the screentexture capture, I have no clue why
	&& rendermode != render_opengl
#endif
	)
		Y_ConsiderScreenBuffer();

	if (useinterpic)
		V_DrawScaledPatch(0, 0, 0, interpic);
#ifdef HWRENDER
	else if (y_screenbuffer != NULL && !(rendermode == render_opengl && cv_glscreentextures.value != 2))
#else
	else if (y_screenbuffer != NULL)
#endif
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
		hilicol = gametypes[gametype]->color;

	if (rolltic != -1 && intertic > (rolltic - 8) && (intertic < (rolltic + 8)))
	{
		int32_t count = (intertic - (rolltic - 8));

		if (count < 8)
			x -= ((((count<<FRACBITS) + R_GetTimeFrac(RTF_INTER)) * vid.width)>>FRACBITS) / (8 * vid.dup);
		else if (count == 8)
			goto skiptallydrawer;
		else if (count < 16)
			x += (((((16 - count)<<FRACBITS) - R_GetTimeFrac(RTF_INTER)) * vid.width)>>FRACBITS) / (8 * vid.dup);
	}
	else if (sorttic != -1 && intertic > sorttic)
	{
		int32_t count = (intertic - sorttic);

		if (count < 8)
			x -= ((((count<<FRACBITS) + R_GetTimeFrac(RTF_INTER)) * vid.width)>>FRACBITS) / (8 * vid.dup);
		else if (count == 8)
			goto skiptallydrawer;
		else if (count < 16)
			x += (((((16 - count)<<FRACBITS) - R_GetTimeFrac(RTF_INTER)) * vid.width)>>FRACBITS) / (8 * vid.dup);
	}

	if (intertype == int_race || intertype == int_battle || intertype == int_battletime)
	{
#define NUMFORNEWCOLUMN 8
		int32_t y = 41, gutter = ((data.numplayers > NUMFORNEWCOLUMN) ? 0 : (BASEVIDWIDTH/2));
		int32_t dupadjust = (vid.scaledwidth), duptweak = (dupadjust - BASEVIDWIDTH)/2;
		fixed_t newlist_xpush = (BASEVIDWIDTH/2) * FRACUNIT;
		const char *timeheader;

		dboolean manyplayers16 = (data.numplayers > NUMFORNEWCOLUMN*2);
		dboolean manyplayers8 = (data.numplayers > NUMFORNEWCOLUMN);
		dboolean displayitemrolls = (data.itemrolls && (intertic <= rolltic));

		if (displayitemrolls)
			manyplayers16 = manyplayers8 = false;

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

		int32_t xscroll_px = (xscroll / FRACUNIT);

		for (i = 0; i < data.numplayers; i++)
		{
			dboolean dojitter = data.jitter[data.num[i]];
			data.jitter[data.num[i]] = 0;

			if (data.num[i] != MAXPLAYERS && playeringame[data.num[i]] && !players[data.num[i]].spectator)
			{
				char strtime[MAXPLAYERNAME+1];

				if (dojitter)
					y--;

				if (displayitemrolls)
				{
					if (kp_facenum[data.pos[i]] == missingpat)
						V_DrawPingNum(x+2-xscroll_px, y+1, 0, data.pos[i], NULL);
					else
						V_DrawScaledPatch(x-5-xscroll_px, y+1, 0, kp_facenum[data.pos[i]]);
				}
				else if (manyplayers16)
				{
					V_DrawPingNum(x + 6, y + 2, 0, data.pos[i], NULL);
				}
				else
					V_DrawCenteredString(x+6, y, 0, va("%d", data.pos[i]));

				if (data.color[i])
				{
					uint8_t *colormap = R_GetTranslationColormap(*data.character[i], *data.color[i], GTC_CACHE);
					patch_t *facerank;

					dboolean hires = K_UseHighResPortraits() && !manyplayers16 && !displayitemrolls;
					facerank = faceprefix[*data.character[i]][hires ? FACE_WANTED : FACE_RANK];
					int8_t hiresoffsetx = hires ? 8: 0;
					int8_t hiresoffsety = hires ? 2: 0;

					fixed_t scale = hires ? FRACUNIT / 2 : FRACUNIT;

					if (manyplayers16)
					{
						scale = FRACUNIT / 2;

						V_DrawFixedPatch((x+8)<<FRACBITS, (y+1)<<FRACBITS, scale, 0, facerank, colormap);
					}
					else
					{
						int32_t xoffs, yoffs;
						int16_t leftoffset = hires ? facerank->leftoffset / 2 : facerank->leftoffset;
						int16_t topoffset = hires ? facerank->topoffset / 2 :facerank->topoffset;

						scale = (hires || displayitemrolls) ? FRACUNIT/2 : FRACUNIT;

						xoffs = FixedMul(16, scale);
						yoffs = FixedMul(4, scale);

						if (displayitemrolls)
						{
							V_DrawFixedPatch(
								((x+11-xscroll_px)*FRACUNIT) + ((leftoffset) * scale),
								((y+1)*FRACUNIT) + ((topoffset) * scale),
								scale,
								0,
								facerank,
								colormap
							);

							if ((x_base - x) == 0)
							{
								// Terrible hack to remedy offset woes: Set a "base" value for x to reference.
								x_base = (x+11) + 20;
							}

							xx = x + x_base - 4;
						}
						else
						{
							V_DrawFixedPatch((x+xoffs+hiresoffsetx)<<FRACBITS, ((y-yoffs- +hiresoffsety)<<FRACBITS), scale, 0, facerank, colormap);
						}
					}
				}

				if ((!displayitemrolls) && data.num[i] == whiteplayer && data.numplayers <= NUMFORNEWCOLUMN*2)
				{
					uint8_t cursorframe = (intertic / 4) % 8;
					patch_t *highlight = W_CachePatchName(va("K_CHILI%d", cursorframe+1), PU_CACHE);
					V_DrawScaledPatch(x+16+highlight->leftoffset, y-4+highlight->topoffset, 0, highlight);
				}

				if ((!displayitemrolls) && (players[data.num[i]].pflags & PF_NOCONTEST) && players[data.num[i]].bot)
				{
					// RETIRED!!
					patch_t *retire = W_CachePatchName("K_NOBLNS", PU_CACHE);
					if (manyplayers16)
						V_DrawSmallScaledPatch(x+6, y-1, 0, retire);
					else
						V_DrawScaledPatch(x+12, y-7, 0, retire);
				}

				STRBUFCPY(strtime, data.name[i]);

				y2 = y;

				int32_t slen = 0;
				int32_t slen_temp = slen;

				if (manyplayers16)
					V_DrawThinString(x+18, y, ((data.num[i] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, strtime);
				else if (displayitemrolls)
				{
					V_DrawThinString(x + 20 - xscroll_px, y, ((data.num[i] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, strtime);

					// Get the longest string size. This is... gross.
					int32_t ii;

					for (ii = 0; ii < data.numplayers; ii++)
					{
						if (data.num[ii] != MAXPLAYERS && playeringame[data.num[ii]] && !players[data.num[ii]].spectator)
						{
							slen_temp = V_ThinStringWidth(data.name[ii], ((data.num[ii] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE);
							slen = ((slen_temp > slen) ? slen_temp : slen);
						}
					}
				}
				else if (manyplayers8)
				{
					V_DrawThinString(x+36, y2 - 1, ((data.num[i] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, strtime);
					slen = V_ThinStringWidth(strtime, ((data.num[i] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE);
				}
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
				else if (data.itemrolls && (intertic <= rolltic))
				{
					const fixed_t itemlistlen = K_DrawItemList((int32_t)(data.num[i]), ((xx+2+slen-xscroll_px) * FRACUNIT), ((y+1) * FRACUNIT));
					newlist_xpush = max(newlist_xpush, ((x_base+9+slen) * FRACUNIT) + itemlistlen);
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
				y += (displayitemrolls) ? ITEMLIST_PLAYER_YOFFSET : 18;

				if ((i % 16) == (((displayitemrolls) ? 2 : 1) * NUMFORNEWCOLUMN) - 1)
				{
					y = 41;
					x += (newlist_xpush / FRACUNIT);

					newlist_xpush = (BASEVIDWIDTH / 2) * FRACUNIT;
				}
			}
#undef NUMFORNEWCOLUMN
		}

		listscroll_length = max(0, (x - 4) - BASEVIDWIDTH) * FRACUNIT;
	}

skiptallydrawer:
	if (!LUA_HudEnabled(hud_intermissionmessages))
		return;

	if (timer && grandprixinfo.gp == false && bossinfo.boss == false)
	{
		const char *string;
		int32_t tickdown = (timer+1)/TICRATE;

		if (multiplayer && demo.playback)
			string = va("Replay ends in %d", tickdown);
		else if (modeattacking != ATTACKING_NONE)
			string = va("Exiting in %d", tickdown);
		else
		{
			mapnum_t next = G_CheckNextMap(false);
			switch (next)
			{
			case NEXTMAP_VOTING:
				string = "Vote";
				break;
			case NEXTMAP_CEREMONY:
				string = "Ceremony";
				break;
			case NEXTMAP_RANDOM:
				string = "Random";
				break;
			default:
				string = next == prevmap ? "Same" : "Next";
				break;
			}
			string = va("%s starts in %d", string, tickdown);
		}

		V_DrawCenteredString(BASEVIDWIDTH/2, 188, hilicol,
			string);
	}

	if ((demo.recording || demo.savemode == DSM_SAVED) && !demo.playback)
		switch (demo.savemode)
		{
		case DSM_NOTSAVING:
		{
			char replaytext[60] = {0};
			int32_t flags = 0;
			const char *item1 = gamecontrol[0][gc_lookback][0] != 0 ? G_KeynumToString(gamecontrol[0][gc_lookback][0]) : NULL;
			const char *item2 = gamecontrol[0][gc_lookback][1] != 0 ? G_KeynumToString(gamecontrol[0][gc_lookback][1]) : NULL;
			const char *item3 = gamecontrol[0][gc_lookback][2] != 0 ? G_KeynumToString(gamecontrol[0][gc_lookback][2]) : NULL;
			const char *item4 = gamecontrol[0][gc_lookback][3] != 0 ? G_KeynumToString(gamecontrol[0][gc_lookback][3]) : NULL;

			if (item1 && item2 && item3 && item4)
			{
				snprintf(replaytext, 60, "%s/%s/%s/%s: Save Replay", item1,item2,item3,item4);
				flags = V_6WIDTHSPACE;
			}
			else if (item1 && item2 && item3)
			{
				snprintf(replaytext, 60, "%s/%s/%s: Save Replay", item1,item2,item3);
				flags = V_6WIDTHSPACE;
			}
			else if (item1 && item2)
			{
				snprintf(replaytext, 60, "%s/%s: Save Replay", item1,item2);
				flags = V_6WIDTHSPACE;
			}
			else
			{
				snprintf(replaytext, 60, "%s: Save Replay", item1 ? item1 : "Look Backwards");
			}

			V_DrawRightAlignedThinString(BASEVIDWIDTH - 2, 2, V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|hilicol|flags, replaytext);
			break;
		}

		case DSM_SAVED:
			V_DrawRightAlignedThinString(BASEVIDWIDTH - 2, 2, V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|hilicol, "Replay saved!");
			break;

		default: // Don't render any text here
			break;
		}

	if ((speedscramble != -1) && (speedscramble != gamespeed))
		V_DrawThinString(5, 188, hilicol|V_ALLOWLOWERCASE|V_SNAPTOLEFT|V_6WIDTHSPACE,
			va(M_GetText("Next race is %s Speed!"), kartspeed_cons_t[1+speedscramble].strvalue));

	if (renderisnewtic)
	{
		LUA_HUD_ClearDrawList(luahuddrawlist_intermission);
		LUA_HookHUD(luahuddrawlist_intermission, HUD_HOOK(intermission));
	}
	LUA_HUD_DrawList(luahuddrawlist_intermission);
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
		if (demo.savemode == DSM_NOTSAVING && G_PlayerInputDown(0, gc_lookback, false, DEADZONE_BUTTON) && !menustack[0] && !chat_on)
		{
			demo.savemode = DSM_TITLEENTRY;
			M_OpenVirtualKeyboard(&demo.titlenameinput, demo.titlename, &replaysave);
			M_StartControlPanel();
			M_EnterMenu(MN_TYPINGDUMMY, false, 0);
		}

		if (demo.savemode == DSM_WILLSAVE || demo.savemode == DSM_WILLAUTOSAVE)
		{
			G_SetAutoSaveName();
			G_SaveDemo();
		}

	}

	// Check for pause or menu up in single player
	if (paused || P_AutoPause())
		return;

	LUA_HOOK(IntermissionThinker);

	intertic++;

	if (listscroll_length && (intertic > (TICRATE * 2)) && (intertic <= rolltic))
	{

		if (!listscroll_reverse)
		{
			if ((xscroll >= listscroll_length))
			{
				if (!listscroll_delay)
					listscroll_delay = ITEMLIST_SCROLLDELAY;
			}
			else
				xscroll = min(listscroll_length, xscroll + ITEMLIST_SCROLLSPEED);
		}
		else
		{
			if ((xscroll <= 0))
			{
				if (!listscroll_delay)
					listscroll_delay = ITEMLIST_SCROLLDELAY;
			}
			else
				xscroll = max(0, xscroll - ITEMLIST_SCROLLSPEED);
		}

		if (listscroll_delay)
		{
			listscroll_delay--;

			if (!listscroll_delay)
				listscroll_reverse = (!listscroll_reverse);
		}
	}
	else
	{
		listscroll_length = 0;
		listscroll_delay = 0;
		listscroll_reverse = false;

		if (xscroll > 0)
		{
			if (intertic < rolltic)
				xscroll = max(0, xscroll - ITEMLIST_SCROLLSPEED);
			else
				xscroll = 0;
		}
	}

	// Team scramble code for team match and CTF.
	// Don't do this if we're going to automatically scramble teams next round.
	/*if (G_GametypeHasTeams() && cv_teamscramble.value && !cv_scrambleonchange.value && server)
	{
		// If we run out of time in intermission, the beauty is that
		// the P_Ticker() team scramble code will pick it up.
		if ((intertic % (TICRATE/7)) == 0)
			P_DoTeamscrambling();
	}*/

	if ((timer && !--timer)
		|| (intertic == endtic))
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
				int32_t q=0,r=0;
				dboolean kaching = true;

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
							int8_t remove = 0; // default (should not happen)

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
							int32_t diff = 1;
							int32_t increase = data.increase[data.num[q]];
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
				/*else -- This is how to define an endtic, but we currently use timer for both SP and MP.
					endtic = intertic + 3*TICRATE;*/
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
	// set initially
	intertype = gametypes[gametype]->intermission;

	// TODO: special cases
	if (gametype == GT_BATTLE)
	{
		if (grandprixinfo.gp == true && bossinfo.boss == false)
			intertype = int_none;
		else
		{
			uint8_t i = 0, nump = 0;
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (!playeringame[i] || players[i].spectator)
					continue;
				nump++;
			}
			intertype = (nump < 2 ? int_battletime : int_battle);
		}
	}
}

//
// Y_StartIntermission
//
// Called by G_DoCompleted. Sets up data for intermission drawer/ticker.
//
void Y_StartIntermission(void)
{
	uint8_t i = 0, nump = 0;
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
	// Technically cv_inttime is saved to demos... but this permits having extremely long timers for post-netgame chatting without stranding you on the intermission in netreplays.
	if (!K_CanChangeRules(false))
	{
		timer = 10*TICRATE;
	}
	else
	{
		timer = cv_inttime.value*TICRATE;
	}

	// determine the tic everybody's scores/PWR starts getting sorted
	sorttic = -1;
	rolltic = -1;
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

			if (data.itemrolls)
			{
				if (!K_CanChangeRules(false))
				{
					rolltic = 10*TICRATE;
				}
				else
				{
					rolltic = cv_rolltime.value*TICRATE;
				}

				sorttic += rolltic;
				timer += rolltic;
			}
			break;
		}

		case int_none:
		default:
			break;
	}

	if (rendermode != render_none)
	{
		LUA_HUD_DestroyDrawList(luahuddrawlist_intermission);
		luahuddrawlist_intermission = LUA_HUD_CreateDrawList();
	}

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

	if (roundqueue.size > 0 && roundqueue.position == roundqueue.size)
	{
		Automate_Run(AEV_QUEUEEND);
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
void Y_FollowIntermission(void)
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
	UNLOAD(bgtile);
	UNLOAD(interpic);
}

#undef ITEMLIST_PLAYER_YOFFSET
#undef ITEMLIST_SCROLLSPEED
#undef ITEMLIST_SCROLLDELAY
