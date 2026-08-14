// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2020 by Kart Krew.
// Copyright (C) 2026 by Team BlanKart.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_vote.c
/// \brief MK online style voting screen

#include "doomdef.h"
#include "doomstat.h"
#include "g_game.h"
#include "i_video.h"
#include "r_skins.h"
#include "s_sound.h"
#include "st_stuff.h"
#include "v_video.h"
#include "w_wad.h"
#include "y_inter.h"
#include "z_zone.h"
#include "m_menu.h"
#include "r_draw.h"

#include "p_local.h"

#include "lua_hook.h" // IntermissionThinker hook

#include "lua_hud.h"
#include "lua_hudlib_drawlist.h"

#include "m_random.h" // M_RandomKey
#include "g_input.h" // G_PlayerInputDown
#include "k_bot.h"
#include "console.h" // cons_menuhighlight
#include "k_itemlist.hpp"
#include "k_vote.h"
#include "k_hud.h"

static int32_t votetimer;

static huddrawlist_h luahuddrawlist_vote;

// SRB2Kart: voting stuff
// Level images
typedef struct
{
    char str[62];
    uint8_t gtc;
    const char *gts;
    dboolean encore;
} y_votelvlinfo;

// Clientside & splitscreen player info.
typedef struct
{
    int8_t selection;
} y_voteplayer;

typedef struct
{
    y_voteplayer playerinfo[4];
    uint8_t ranim;
    uint8_t rtics;
    uint8_t roffset;
    uint8_t rsynctime;
    uint8_t rendoff;
    dboolean loaded;
} y_voteclient;

// votescreen stuff
votescreen_t VoteScreen = {0};

static y_votelvlinfo levelinfo[13];
static y_voteclient voteclient;
static int32_t votetic;
static int32_t lastvotetic;
static int32_t voteendtic = -1;
static int8_t votemax = 3;
static int32_t voterowmem = 0;
static dboolean rowchange = false;
static dboolean votenotyetpicked;

// SRB2Kart: Voting!

//
// Y_VoteScreenCheck
//
void Y_VoteScreenCheck(void)
{
	strcpy(VoteScreen.Prefix, "INTS");

	if (VoteScreen.luaPrefix[0] != 0)
		strlcpy(VoteScreen.Prefix, VoteScreen.luaPrefix, sizeof(VoteScreen.Prefix));
	else if (gametype == GT_BATTLE)
		strcpy(VoteScreen.Prefix, "BTLS");

	VoteScreen.foundLuaVoteFrames = VoteScreen.foundLuaVoteWideFrames = 0;
	VoteScreen.currentAnimFrame = 0;

	if (VoteScreen.timePerAnimFrame == 0)
		VoteScreen.timePerAnimFrame = 2;

	int32_t i = 1;

	// check for lua vote background replacements
	for (;;)
	{
		// Check if the lumps exist (checking for VEXTR(N|W)xx for race and VEXTRB(N|W)xx for battle)
		dboolean normalLumpExists = W_LumpExists(va("%sC%d", VoteScreen.Prefix, i));
		dboolean wideLumpExists = W_LumpExists(va("%sW%d", VoteScreen.Prefix, i));

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
	dboolean prefbattletype = ((g_voteLevels[0][1] & ~VOTEMODIFIER_ENCORE) == GT_BATTLE);
	VoteScreen.widebgpatch = W_CachePatchName((prefbattletype ? "BATTLSCW" : "INTERSCW"), PU_PATCH_LOWPRIORITY);
	VoteScreen.bgpatch = W_CachePatchName((prefbattletype ? "BATTLSCR" : "INTERSCR"), PU_PATCH_LOWPRIORITY);
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
			V_DrawScaledPatch(((vid.width/2) / vid.dup) - (patch->width/2),
							  (vid.height / vid.dup) - patch->height,
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
static void Y_DrawLuaVoteScreenPatch(dboolean widePatch)
{
	int32_t nextframe = 0;
	patch_t *votebg = NULL;
	char tempPrefix[6];
	const int32_t tempfoundAnimLuaVoteFrames = ((widePatch ? VoteScreen.foundLuaVoteWideFrames : VoteScreen.foundLuaVoteFrames) - 1);

	strcpy(tempPrefix, va("%s%s", VoteScreen.Prefix, (widePatch ? "W" : "C")));

	// Draw non animated patch
	if (!tempfoundAnimLuaVoteFrames)
	{
		votebg = W_CachePatchName(va("%s1", tempPrefix), PU_PATCH_LOWPRIORITY);
		Y_VoteBackgroundDrawer(votebg);
		return;
	}

	// Draw animated patch based on frame counter on vote screen

	// Just in case someone provides LESS widescreen frames than normal frames or vice versa, reset the frame counter to 0
	if (VoteScreen.currentAnimFrame > tempfoundAnimLuaVoteFrames)
		VoteScreen.currentAnimFrame = 0;

	nextframe = (VoteScreen.currentAnimFrame + 1);

	votebg = W_CachePatchName(va("%s%d", tempPrefix, nextframe), PU_PATCH_LOWPRIORITY);

	Y_VoteBackgroundDrawer(votebg);

	if (renderisnewtic && (votetic % VoteScreen.timePerAnimFrame == 0) && !paused)
		VoteScreen.currentAnimFrame = (nextframe > tempfoundAnimLuaVoteFrames) ? 0 : nextframe;
}

//
// Y_DrawVoteScreenPatch
//

static void Y_DrawVoteScreenPatch(void)
{
	patch_t *votebg = NULL;
	const dboolean widescreen = (vid.scaledwidth > 320);

	if (VoteScreen.foundLuaVoteWideFrames || VoteScreen.foundLuaVoteFrames)
	{
		Y_DrawLuaVoteScreenPatch(((widescreen && VoteScreen.foundLuaVoteWideFrames) || !VoteScreen.foundLuaVoteFrames));
		return;
	}

	// non widescreen patch
	votebg = VoteScreen.bgpatch;

	uint8_t prefgametype = (g_voteLevels[0][1] & ~VOTEMODIFIER_ENCORE);
	const dboolean widebgreplaced = (prefgametype == GT_BATTLE) ? VoteScreen.replaced.widebattle : VoteScreen.replaced.widerace;
	const dboolean bgreplaced = (prefgametype == GT_BATTLE) ? VoteScreen.replaced.battle : VoteScreen.replaced.race;

	// we check a bunch of stuff to always have a "valid" fallback
	if ((widescreen && (widebgreplaced || !bgreplaced))
	|| (!widescreen && (widebgreplaced && !bgreplaced)))
	{
		votebg = VoteScreen.widebgpatch; // widescreen patch
	}

	Y_VoteBackgroundDrawer(votebg);
}

static fixed_t Y_CalculatePicScale(fixed_t picscale, int32_t hypoti)
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

dboolean Y_PlayerIDCanVote(const uint8_t id)
{
	if (id >= MAXPLAYERS)
	{
		return false;
	}

	if (playeringame[id] == false || players[id].spectator == true)
	{
		return false;
	}

	if (players[id].bot && !cv_botcanvote.value)
	{
		// Bots may only vote if the server allows it
		return false;
	}

	return true;
}

//
// Y_VoteDrawer
//
// Draws the voting screen!
//
void Y_VoteDrawer(void)
{
	int32_t rowval, i, px, lvls, x, picdiff, y = 0, height = 0;
	uint8_t selected[12];
	fixed_t rubyheight = 0;
	fixed_t scale;
	patch_t *pic;
	mapnum_t mapnum;
	fixed_t picwidth = 160;

	// CEP: scale by screen hypotenuse for extra voting rows
	int32_t vidx = vid.scaledwidth;
	int32_t vidy = ((vid.height) / vid.dup);
	fixed_t hypotf = 0;
	int32_t hypoti = 0;

	int32_t numplayers = 0;
	dboolean highplayers = false;

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
	int32_t hypotdiv = max(10, (40-hypoti));

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
		uint8_t j;
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
		uint8_t j, color;

		// CEP: hack hell
		int32_t scaledpicdiff = ((picdiff*10)/hypotdiv);
		int32_t fillscale = 800/hypotdiv;
		int32_t hypotmod = (hypoti % 5); // hypotenuse mod 5, rescale the bounding box
		int32_t hypotadd = ((hypotmod > 1) ? (hypotmod/4) : hypotmod); // how much do we add the bounding box by?

		// integer scaling makes me want to DIE
		if (hypotmod == 3)
			hypotadd += (1);

		scaledpicdiff *= 3;
		scaledpicdiff /= 2; // 1.5

		if (i == rowval)
		{
			str = "RANDOM";
			mapnum = NEXTMAP_INVALID;
		}
		else
		{
			str = levelinfo[i].str;
			mapnum = g_voteLevels[i][0];
		}
		scale = M_GetMapThumbnail(mapnum, &pic)/2;

		if (selected[i])
		{
			uint8_t sizeadd = selected[i];

			for (j = 0; j <= splitscreen; j++) // another loop for drawing the selection backgrounds in the right order, grumble grumble..
			{
				int32_t handy = y;
				uint8_t p;
				uint8_t *colormap;
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

				if (g_votes[p] != -1 || !Y_PlayerIDCanVote(p))
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
				int32_t w = V_ThinStringWidth(levelinfo[i].gts, V_SNAPTORIGHT)+1;
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
		int32_t bigaddx = 60; int32_t bigaddy = 30;
		int32_t smalladdx = 42; int32_t smalladdy = 18;
		int32_t smallfaceheight = 0; int32_t bigfaceheight = 9;
		int32_t smallrectheight = 18; int32_t bigrectheight = 27;
		int32_t smallrubyoffset = 4<<FRACBITS;
		int32_t smallpicoffset = 4<<FRACBITS;

		if (dedicated && i == 0) // While leaving blank spots for non-existent players is largely intentional, the first spot *always* being blank looks a tad silly :V
			continue;

		if (Y_PlayerIDCanVote(i) && g_votes[i] != VOTE_NOT_PICKED)
		{
			if (g_votes[i] >= ((votemax*3)+((votemax > 1) ? (votemax - 1) : 0)) && (i != g_pickedVote || voteendtic == -1))
				mapnum = NEXTMAP_INVALID; // randomlvl
			else
				mapnum = g_voteLevels[g_votes[i]][0];

			scale = M_GetMapThumbnail(mapnum, &pic)/8;

			if (!votetimer && i == voteclient.ranim)
			{
				V_DrawScaledPatch(x-18, y+(highplayers ? 2 : bigfaceheight), V_SNAPTOLEFT, VoteScreen.cursor[0]);
				if (voteendtic != -1 && !(votetic % 4))
					V_DrawFill(x-1, y-1, 42, (highplayers ? smallrectheight : bigrectheight), 0|V_SNAPTOLEFT);
				else
					V_DrawFill(x-1, y-1, 42, (highplayers ? smallrectheight : bigrectheight), levelinfo[g_votes[i]].gtc|V_SNAPTOLEFT);
			}

			if (highplayers)
			{
				V_SetClipRect(x<<FRACBITS, y<<FRACBITS, 48<<FRACBITS, 16<<FRACBITS, V_SNAPTOLEFT);
			}
			if (!levelinfo[g_votes[i]].encore)
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

			if (levelinfo[g_votes[i]].gts)
			{
				V_DrawDiag(x, y, 8, V_SNAPTOLEFT|31);
				V_DrawDiag(x, y, 6, V_SNAPTOLEFT|levelinfo[g_votes[i]].gtc);
			}

			if (players[i].skincolor)
			{
				uint8_t *colormap = R_GetTranslationColormap(players[i].skin, players[i].skincolor, GTC_CACHE);
				dboolean hires = K_UseHighResPortraits();
				patch_t *facerank = faceprefix[players[i].skin][hires ? FACE_WANTED : FACE_RANK];
				fixed_t hiresscale = hires ? FRACUNIT/2 : FRACUNIT;

				V_DrawFixedPatch((x+24+facerank->leftoffset)<<FRACBITS, (y+(highplayers ? smallfaceheight : bigfaceheight)+facerank->topoffset)<<FRACBITS, hiresscale, V_SNAPTOLEFT, facerank, colormap);
			}

			if (!splitscreen && i == consoleplayer)
			{
				uint8_t cursorframe = (votetic / 4) % 8;
				patch_t *highlight = W_CachePatchName(va("K_CHILI%d", cursorframe+1), PU_PATCH_LOWPRIORITY);
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

	if (votetimer)
	{
		int32_t hilicol, tickdown = (votetimer+1)/TICRATE;

		if (cons_menuhighlight.value)
			hilicol = cons_menuhighlight.value;
		else
			hilicol = gametypes[gametype]->color;

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
static void Y_FinalizeVote(const int8_t level)
{
	nextmap = g_voteLevels[level][0];
	deferencoremode = (levelinfo[level].encore);

	if (gametype != g_voteLevels[level][1])
	{
		int16_t lastgametype = gametype;
		G_SetGametype(g_voteLevels[level][1]);
		D_GameTypeChanged(lastgametype);
		forceresetplayers = true;
	}
}

int8_t deferredlevel = 0;
static void Y_VoteStops(const int8_t pick, const int8_t level)
{
	Y_FinalizeVote(level);

	/*if (level == 4)
	{
		S_StartSound(NULL, sfx_noooo2); // gasp
	}
	else */
	if (mapheaderinfo[nextmap] && (mapheaderinfo[nextmap]->menuflags & LF2_HIDEINMENU))
	{
		S_StartSound(NULL, sfx_noooo1); // this is bad
	}
	else if (netgame && P_IsLocalPlayer(&players[pick]))
	{
		S_StartSound(NULL, sfx_yeeeah); // yeeeah!
	}
	else
	{
		S_StartSound(NULL, sfx_kc48); // just a cool sound
	}
}

//
// Y_VoteTicker
//
// Vote screen thinking :eggthinking:
//
void Y_VoteTicker(void)
{
	int32_t i, j;
	dboolean everyone_voted;

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
		if (!Y_PlayerIDCanVote(i))
			g_votes[i] = VOTE_NOT_PICKED; // Spectators are the lower class, and have effectively no voice in the government. Democracy sucks.
		else if (g_pickedVote != VOTE_NOT_PICKED && g_votes[i] == VOTE_NOT_PICKED)
			g_votes[i] = (votemax*3)+((votemax > 1) ? (votemax - 1) : 0); // Slow people get random
	}

	if (server && g_pickedVote != VOTE_NOT_PICKED && g_votes[g_pickedVote] == VOTE_NOT_PICKED) // Uh oh! The person who got picked left! Recalculate, quick!
		D_PickVote();

	if (!votetic)
	{
		S_ChangeMusicInternal("vote", true);
		S_ShowMusicCredit(0, 5*TICRATE, 0);
	}

	if (votetimer)
		votetimer--;

	if (g_pickedVote != VOTE_NOT_PICKED)
	{
		votetimer = 0;
		voteclient.rsynctime++;

		if (voteendtic == -1)
		{
			uint8_t tempvotes[MAXPLAYERS];
			uint8_t numvotes = 0;

			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (g_votes[i] == VOTE_NOT_PICKED)
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
				voteclient.ranim = tempvotes[((g_pickedVote + voteclient.roffset) % numvotes)];

			if (voteclient.roffset >= 20)
			{
				if (voteclient.rendoff == 0)
				{
					if (voteclient.rsynctime % 51 == 0) // Song is 1.45 seconds long (sorry @ whoever wants to replace it in a music wad :V)
					{
						for (i = 5; i >= 3; i--) // Find a suitable place to stop
						{
							if (tempvotes[((g_pickedVote + voteclient.roffset + i) % numvotes)] == g_pickedVote)
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
					Y_VoteStops(g_pickedVote, deferredlevel);
				}
			}
		}
		else
			voteclient.ranim = g_pickedVote;
	}
	else if (votenotyetpicked)
	{
		if (votetic < 3*(NEWTICRATE/7)) // give it some time before letting you control it :V
			return;

		if (menustack[0] || chat_on) // You can't control voting with menu or chat open.
			return;

		/*
		The vote ended, but it will take at least a tic for that to reach us from
		the server. Don't let me change the vote now, it won't matter anyway!
		*/
		if (votetimer)
		{
			for (i = 0; i <= splitscreen; i++)
			{
				uint8_t p;
				dboolean pressed = false;
				int8_t votewrap = 0;

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

				if (Y_PlayerIDCanVote(p)
						&& !menucmd[i].delay
						&& g_pickedVote == VOTE_NOT_PICKED && g_votes[p] == VOTE_NOT_PICKED)
				{
					if (menucmd[i].dpad_ud < 0)
					{
						voteclient.playerinfo[i].selection--;
						pressed = true;
					}
					else if (menucmd[i].dpad_ud > 0)
					{
						voteclient.playerinfo[i].selection++;
						pressed = true;
					}

					if (votemax > 1) // only allow side-movements for multi-row selections
					{
						// HORRIBLE hack, my GOD
						if (menucmd[i].dpad_lr > 0) // move right
						{
							if (voteclient.playerinfo[i].selection <= votewrap)
								voteclient.playerinfo[i].selection += 4;
							else
								voteclient.playerinfo[i].selection -= ((votemax-1)*4);
							pressed = true;
						}
						else if (menucmd[i].dpad_lr < 0) // move left
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

					if (M_MenuButtonPressed(i, MBT_CONFIRM))
					{
						D_ModifyClientVote(g_localplayers[i], voteclient.playerinfo[i].selection);
						pressed = true;
					}
				}

				if (pressed)
				{
					S_StartSound(NULL, sfx_kc4a);
					M_SetMenuDelay(i);
				}
			}
		}

		if (server)
		{
			everyone_voted = true;/* the default condition */

			if (votetimer == 0)
			{
				for (i = 0; i < MAXPLAYERS; i++)
				{
					if (Y_PlayerIDCanVote(i) && g_votes[i] == VOTE_NOT_PICKED)
						g_votes[i] = (votemax*3)+((votemax > 1) ? (votemax-1) : 0);
				}
			}
			else
			{
				for (i = 0; i < MAXPLAYERS; i++)
				{
					if (!Y_PlayerIDCanVote(i))
					{
						continue;
					}

					if (server && players[i].bot && g_votes[i] == VOTE_NOT_PICKED)
					{
						if (( M_RandomFixed() % 100 ) == 0)
						{
#define VOTEROWSADDSONE ((cv_votemaxrows.value*3) + 1 + ((cv_votemaxrows.value > 1) ? (cv_votemaxrows.value - 1) : 0))
							// bots vote randomly
							int32_t rng = M_RandomKey(VOTEROWSADDSONE);
							for (j = 0; j < VOTEROWSADDSONE; j++)
							{
								rng++;

								if (rng >= VOTEROWSADDSONE || rng < 0)
								{
									rng = 0;
								}
							}
#undef VOTEROWSADDSONE

							D_ModifyClientVote(i, rng);
						}
					}

					if (g_votes[i] == VOTE_NOT_PICKED)
					{
						everyone_voted = false;
					}
				}
			}

			if (everyone_voted)
			{
				votetimer = 0;
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

	VoteScreen.cursor[0] = W_CachePatchName("M_CURSOR", PU_PATCH_LOWPRIORITY);
	VoteScreen.cursor[1] = W_CachePatchName("P1CURSOR", PU_PATCH_LOWPRIORITY);
	VoteScreen.cursor[2] = W_CachePatchName("P2CURSOR", PU_PATCH_LOWPRIORITY);
	VoteScreen.cursor[3] = W_CachePatchName("P3CURSOR", PU_PATCH_LOWPRIORITY);
	VoteScreen.cursor[4] = W_CachePatchName("P4CURSOR", PU_PATCH_LOWPRIORITY);

	VoteScreen.rubyicon  = W_CachePatchName("RUBYICON", PU_PATCH_LOWPRIORITY);
}

void Y_StartVote(void)
{
	int32_t i = 0;
	int32_t rowval = (cv_votemaxrows.value*3)+((cv_votemaxrows.value > 1) ? (cv_votemaxrows.value - 1) : 0);

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

	votetimer = cv_votetime.value*TICRATE;
	g_pickedVote = VOTE_NOT_PICKED;

	votenotyetpicked = true;

	for (i = 0; i < 3; i++)
	{
		voteclient.playerinfo[i].selection = 0;
	}

	voteclient.ranim = 0;
	voteclient.rtics = 1;
	voteclient.roffset = 0;
	voteclient.rsynctime = 0;
	voteclient.rendoff = 0;

	for (i = 0; i < MAXPLAYERS; i++)
		g_votes[i] = VOTE_NOT_PICKED;

	for (i = 0; i < (rowval + 1); i++)
	{
		// set up the encore
		levelinfo[i].encore = (g_voteLevels[i][1] & VOTEMODIFIER_ENCORE) ? true : false;
		g_voteLevels[i][1] &= ~VOTEMODIFIER_ENCORE;

		// set up the levelstring
		if (mapheaderinfo[g_voteLevels[i][0]]->levelflags & LF_NOZONE || !mapheaderinfo[g_voteLevels[i][0]]->zonttl[0])
		{
			if (mapheaderinfo[g_voteLevels[i][0]]->actnum[0])
				snprintf(levelinfo[i].str,
					sizeof levelinfo[i].str,
					"%s %s",
					mapheaderinfo[g_voteLevels[i][0]]->lvlttl, mapheaderinfo[g_voteLevels[i][0]]->actnum);
			else
				snprintf(levelinfo[i].str,
					sizeof levelinfo[i].str,
					"%s",
					mapheaderinfo[g_voteLevels[i][0]]->lvlttl);
		}
		else
		{
			if (mapheaderinfo[g_voteLevels[i][0]]->actnum[0])
				snprintf(levelinfo[i].str,
					sizeof levelinfo[i].str,
					"%s %s %s",
					mapheaderinfo[g_voteLevels[i][0]]->lvlttl, mapheaderinfo[g_voteLevels[i][0]]->zonttl, mapheaderinfo[g_voteLevels[i][0]]->actnum);
			else
				snprintf(levelinfo[i].str,
					sizeof levelinfo[i].str,
					"%s %s",
					mapheaderinfo[g_voteLevels[i][0]]->lvlttl, mapheaderinfo[g_voteLevels[i][0]]->zonttl);
		}

		levelinfo[i].str[sizeof levelinfo[i].str - 1] = '\0';

		// set up the gtc and gts
		levelinfo[i].gtc = G_GetGametypeColor(g_voteLevels[i][1]);
		if (i == 2 && g_voteLevels[i][1] != g_voteLevels[0][1])
			levelinfo[i].gts = gametypes[g_voteLevels[i][1]]->name;
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
	if (nextmap >= NEXTMAP_SPECIAL)
	{
		// Don't leave nextmap unset if the vote is ended through
		// weird means! (such as a dedicated server becoming empty)
		// If nextmap was left at NEXTMAP_VOTING, we'd crash!
		Y_FinalizeVote(0);
	}

	voteclient.loaded = false;
	voteendtic = -1;
}

//
// Y_VoteClear
// Resets patches to prevent dangling pointers
//
void Y_VoteClear(void)
{
	VoteScreen.bgpatch = NULL;
	VoteScreen.widebgpatch = NULL;

	for (size_t i = 0; i < sizeof(VoteScreen.cursor)/sizeof(VoteScreen.cursor[0]); ++i)
		VoteScreen.cursor[i] = NULL;

	VoteScreen.rubyicon = NULL;
}

//
// Y_SetupVoteFinish
//
void Y_SetupVoteFinish(int8_t pick, int8_t level)
{
	if (!voteclient.loaded)
		return;

	if (pick == VOTE_NOT_PICKED) // No other votes? We gotta get out of here, then!
	{
		Y_EndVote();
		Y_FollowIntermission();
		return;
	}

	if (g_pickedVote == VOTE_NOT_PICKED)
	{
		int32_t i;
		int8_t votecompare = VOTE_NOT_PICKED;
		int32_t endtype = 0;

		voteclient.rsynctime = 0;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (Y_PlayerIDCanVote(i) && g_votes[i] == VOTE_NOT_PICKED)
				g_votes[i] = (votemax*3)+((votemax > 1) ? (votemax - 1) : 0);

			if (g_votes[i] == VOTE_NOT_PICKED || endtype > 1) // Don't need to go on
				continue;

			if (endtype == 2)
				continue;

			if (votecompare == VOTE_NOT_PICKED)
			{
				votecompare = g_votes[i];
				endtype = 1;
			}
			else if (g_votes[i] != votecompare)
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
	g_pickedVote = pick;
	votetimer = 0;
}
