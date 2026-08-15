// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2020 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_hud.c
/// \brief HUD drawing functions exclusive to Kart

#include "k_hud.h"
#include "command.h"
#include "g_mapnum.h"
#include "info.h"
#include "k_kart.h"
#include "k_battle.h"
#include "k_grandprix.h"
#include "k_boss.h"
#include "k_color.h"
#include "k_director.h"
#include "k_items.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "screen.h"
#include "doomtype.h"
#include "doomdef.h"
#include "hu_stuff.h"
#include "d_netcmd.h"
#include "v_video.h"
#include "r_draw.h"
#include "st_stuff.h"
#include "lua_hud.h"
#include "doomstat.h"
#include "d_clisrv.h"
#include "g_game.h"
#include "g_input.h"
#include "p_local.h"
#include "z_zone.h"
#include "m_cond.h"
#include "r_main.h"
#include "s_sound.h"
#include "r_things.h"
#include "r_fps.h"
#include "m_random.h"
#include "g_party.h"
#include "h_timers.h"
#include "k_bot.h" // K_DrawBotDebugger
#include "k_waypoint.h"

#include "k_cluster.hpp"

#include "radio_finish.h"

#define NUMPOSNUMS 10
#define NUMPOSFRAMES 7 // White, three blues, three reds
#define NUMWINFRAMES 6 // Red, yellow, green, cyan, blue, purple

// Hud offset cvars
#define IMPL_HUD_OFFSET_X(name)\
consvar_t cv_##name##_xoffset = CVAR_INIT ("hud_" #name "_xoffset", "0", CV_SAVE, CV_Signed, NULL);

#define IMPL_HUD_OFFSET_Y(name)\
consvar_t cv_##name##_yoffset = CVAR_INIT ("hud_" #name "_yoffset", "0", CV_SAVE, CV_Signed, NULL);

#define IMPL_HUD_OFFSET(name)\
IMPL_HUD_OFFSET_X(name)\
IMPL_HUD_OFFSET_Y(name)

IMPL_HUD_OFFSET(item); // Item box
IMPL_HUD_OFFSET(time); // Time
IMPL_HUD_OFFSET(laps); // Number of laps
IMPL_HUD_OFFSET(rings); // Number of rings
IMPL_HUD_OFFSET(dnft); // Countdown (did not finish timer)
IMPL_HUD_OFFSET(speed); // Speedometer
IMPL_HUD_OFFSET(acce); // Accessibility
IMPL_HUD_OFFSET(timers); // Timers
IMPL_HUD_OFFSET(draft); // Drafting
IMPL_HUD_OFFSET(lives); // Lives and Stats
IMPL_HUD_OFFSET(posi); // Position in race
IMPL_HUD_OFFSET(face); // Mini rankings
IMPL_HUD_OFFSET(stcd); // Starting countdown
IMPL_HUD_OFFSET_Y(chek); // Check gfx
IMPL_HUD_OFFSET(mini); // Minimap
IMPL_HUD_OFFSET(want); // Wanted
IMPL_HUD_OFFSET(lapem); // Lap emblem
IMPL_HUD_OFFSET(rank); // Rank score
IMPL_HUD_OFFSET(dialrank); // Rank score (Dial speedometer)
IMPL_HUD_OFFSET(bigrank); // Rank score (Big speedometer)
//IMPL_HUD_OFFSET(stat); // Stats

#undef IMPL_HUD_OFFSET
#undef IMPL_HUD_OFFSET_X
#undef IMPL_HUD_OFFSET_Y

static CV_PossibleValue_t speedo_cons_t[]= {
	{0, "Default"},
	{1, "Small"},
	{2, "P-Meter"},
	{3, "Dial"},
	{0, NULL}
};

consvar_t cv_newspeedometer = CVAR_INIT ("newspeedometer", "Small", CV_SAVE, speedo_cons_t, NULL);

static CV_PossibleValue_t inputdisplay_cons_t[] = {{0, "Off"}, {1, "Wheel"}, {2, "Stick"}, {3, "SRB2"}, {0, NULL}};

consvar_t cv_showinput = CVAR_INIT ("showinput", "Off", CV_SAVE, inputdisplay_cons_t, NULL);

consvar_t cv_colorizedhud = CVAR_INIT ("colorizedhud", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_colorizeditembox = CVAR_INIT ("colorizeditembox", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_darkitembox = CVAR_INIT ("darkitembox", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_showfinishedplayers = CVAR_INIT ("showfinishedplayers", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_smoothposition = CVAR_INIT ("smoothposition", "On", CV_SAVE, CV_OnOff, NULL);
static CV_PossibleValue_t driftgauge_cons_t[] = {{0, "Off"}, {1, "Spee"}, {2, "Achii"}, {3, "Wifi"}, {4, "Chaotix"}, {5, "Numbers"}, {6, "Legacy"}, {7, "Legacy Small"}, {8, "Legacy Large Numbers"}, {9, "Large Num"}, {0, NULL}};
consvar_t cv_driftgauge = CVAR_INIT ("kartdriftgauge", "Off", CV_SAVE, driftgauge_cons_t, NULL);
consvar_t cv_driftgaugeoffset = CVAR_INIT ("kartdriftgaugeoffset", "-10", CV_SAVE|CV_FLOAT, CV_Signed, NULL);

static CV_PossibleValue_t HudColor_cons_t[MAXSKINCOLORS+1];
consvar_t cv_colorizedhudcolor = CVAR_INIT ("colorizedhudcolor", "Skin Color", CV_SAVE, HudColor_cons_t, NULL);

consvar_t cv_newtabranking = CVAR_INIT ("newtabranking", "On", CV_SAVE, CV_OnOff, NULL);

consvar_t cv_draftindicator = CVAR_INIT ("draftindicator", "On", CV_SAVE, CV_OnOff, NULL);

consvar_t cv_showstats = CVAR_INIT ("showstats", "On", CV_SAVE, CV_OnOff, NULL);

// make char potraits use their high-res version instead
consvar_t cv_highresportrait = CVAR_INIT ("highresportrait", "Off", CV_SAVE, CV_OnOff, NULL);

typedef enum
{
	NT_OFF = 0,
	NT_DEFAULT,
	NT_SMALL,
	NT_TEXT,
} nametag_e;

static CV_PossibleValue_t nametag_cons_t[]= {
	{NT_OFF, "Off"},
	{NT_DEFAULT, "Default"},
	{NT_SMALL, "Small"},
	{NT_TEXT, "Text-Only"},
	{0, NULL}
};

consvar_t cv_seenames = CVAR_INIT ("seenames", "Default", CV_SAVE, nametag_cons_t, NULL);
consvar_t cv_seenamerestat = CVAR_INIT ("seenamerestat", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_seeownname = CVAR_INIT ("seeownname", "Off", CV_SAVE, CV_OnOff, NULL);

//{ 	Patch Definitions
static patch_t *kp_nodraw;

// Stickers
static patch_t *kp_timesticker[2];
static patch_t *kp_timestickerwide[2];
static patch_t *kp_lapsticker[2];
static patch_t *kp_lapstickerbig[2];
static patch_t *kp_lapstickerbig2[2];
static patch_t *kp_lapstickerwide[2];
static patch_t *kp_lapstickernarrow[2];
static patch_t *kp_bumpersticker[2];
static patch_t *kp_bumperstickerwide[2];
static patch_t *kp_karmasticker[2];
static patch_t *kp_timeoutsticker[2];

patch_t *kp_splitlapflag;
static patch_t *kp_splitkarmabomb;

static patch_t *kp_startcountdown[20];
static patch_t *kp_racefinish[6];

static patch_t *kp_positionnum[NUMPOSNUMS][NUMPOSFRAMES];
static patch_t *kp_winnernum[NUMPOSFRAMES];

patch_t *kp_facenum[MAXPLAYERS+1] = {};
static patch_t *kp_facehighlight[8];

static patch_t *kp_nocontestminimap;
patch_t *kp_itemboxminimap;

static patch_t *kp_ringsticker[5];
static patch_t *kp_ringsplitscreen;
static patch_t *kp_ringdebtminus;
static patch_t *kp_ringdebtminussmall;

static patch_t *kp_speedometersticker[2];
static patch_t *kp_speedometerlabel[4];
static patch_t *kp_kartzspeedo[25];
static patch_t *kp_kartzspeedo_smol[25];

static patch_t *kp_driftgauge[14];
static patch_t *kp_driftgaugeparts[5];

static patch_t *kp_flamometer[6];
static patch_t *kp_flamefire[18];

// Frames of animation for the fire
#define MAXFLAMOFIRETICS 8

// Rotating S-Monitor sparkles
static patch_t *kp_smonitorsparkle;

static patch_t *kp_rankbumper;
static patch_t *kp_tinybumper[2];
static patch_t *kp_ranknobumpers;

static patch_t *kp_battlewin;
static patch_t *kp_battlecool;
static patch_t *kp_battlelose;
static patch_t *kp_battlewait;
static patch_t *kp_battleinfo;
static patch_t *kp_wanted;
static patch_t *kp_wantedsplit;
static patch_t *kp_wantedreticle;
patch_t *kp_minimapdot;

static patch_t *kp_itembg[8];
static patch_t *kp_itemalt[4];
static patch_t *kp_itemtimer[2];
static patch_t *kp_itemmulsticker[4];
static patch_t *kp_itemx;

static patch_t *kp_check[11];

static patch_t *kp_eggnum[4];

static patch_t *kp_fpview[3];
static patch_t *kp_inputwheel;
static patch_t *kp_inputwheel_shadow;

static patch_t *kp_challenger[25];

static patch_t *kp_lapanim_lap[7];
static patch_t *kp_lapanim_final[11];
static patch_t *kp_lapanim_number[10][3];
static patch_t *kp_lapanim_emblem[2];
static patch_t *kp_lapanim_hand[3];

static patch_t *kp_yougotem;

static patch_t *kp_bossbar[8];
static patch_t *kp_bossret[4];

static patch_t *joybacking;
static patch_t *joyknob;
static patch_t *joyshadow;

patch_t *kp_itemtarget_arrow[2][2];
patch_t *kp_itemtarget_icon[2];
patch_t *kp_itemtarget_far[2][2];
patch_t *kp_itemtarget_far_text[2];
patch_t *kp_itemtarget_near[2][8];

// dial speedometer
static patch_t *kp_dial = NULL;
static patch_t *kp_dialfinish =  NULL;
static patch_t *kp_dialbase[4] = {NULL};
static patch_t *kp_speedpatchesdial[8] = {NULL};
static patch_t *kp_dialnum[20] = {NULL};

patch_t *kp_ranktier[NUMRANKTIERS] = {NULL};
patch_t *kp_ranktier4p[NUMRANKTIERS] = {NULL};

void K_RegisterKartHUDStuff(void)
{
	K_ReloadHUDColorCvar();

#define REG_HUD_OFFSET_X(name)\
	CV_RegisterVar(&cv_##name##_xoffset);

#define REG_HUD_OFFSET_Y(name)\
	CV_RegisterVar(&cv_##name##_yoffset);

#define REG_HUD_OFFSET(name)\
	REG_HUD_OFFSET_X(name)\
	REG_HUD_OFFSET_Y(name)

	REG_HUD_OFFSET(item); // Item box
	REG_HUD_OFFSET(time); // Time
	REG_HUD_OFFSET(laps); // Number of laps
	REG_HUD_OFFSET(rings); // Number of laps
	REG_HUD_OFFSET(dnft); // Countdown (did not finish timer)
	REG_HUD_OFFSET(speed); // Speedometer
	REG_HUD_OFFSET(acce); // Accessibility
	REG_HUD_OFFSET(timers); // Item Timers
	REG_HUD_OFFSET(draft); // Drafting
	REG_HUD_OFFSET(lives); // Lives and Stats
	REG_HUD_OFFSET(posi); // Position in race
	REG_HUD_OFFSET(face); // Mini rankings
	REG_HUD_OFFSET(stcd); // Starting countdown
	REG_HUD_OFFSET_Y(chek); // Check gfx
	REG_HUD_OFFSET(mini); // Minimap
	REG_HUD_OFFSET(want); // Wanted
	REG_HUD_OFFSET(lapem); // Lap emblem
	REG_HUD_OFFSET(rank); // Rank score
	REG_HUD_OFFSET(dialrank); // Rank score (Dial speedometer)
	REG_HUD_OFFSET(bigrank); // Rank score (Big speedometer)
	//REG_HUD_OFFSET(stat); // Stats

#undef REG_HUD_OFFSET
#undef REG_HUD_OFFSET_X
#undef REG_HUD_OFFSET_Y

	CV_RegisterVar(&cv_seenames);
	CV_RegisterVar(&cv_seenamerestat);
	CV_RegisterVar(&cv_seeownname);
	CV_RegisterVar(&cv_newspeedometer);
	CV_RegisterVar(&cv_showinput);
	CV_RegisterVar(&cv_colorizedhud);
	CV_RegisterVar(&cv_colorizedhudcolor);
	CV_RegisterVar(&cv_colorizeditembox);
	CV_RegisterVar(&cv_darkitembox);
	CV_RegisterVar(&cv_showfinishedplayers);
	CV_RegisterVar(&cv_smoothposition);
	CV_RegisterVar(&cv_driftgauge);
	CV_RegisterVar(&cv_driftgaugeoffset);
	CV_RegisterVar(&cv_newtabranking);
	CV_RegisterVar(&cv_draftindicator);
	CV_RegisterVar(&cv_showstats);
	CV_RegisterVar(&cv_highresportrait);

	// h_timers.cpp
	CV_RegisterVar(&cv_itemtimers);
	CV_RegisterVar(&cv_itemtimers_mode);

	// k_items.c
	CV_RegisterVar(&cv_fancyroulette);
	CV_RegisterVar(&cv_fancyroulettespeed);
	CV_RegisterVar(&cv_huditemamount);

	// k_hud_track.cpp
	CV_RegisterVar(&cv_kartdebughudtracker);

	// radio_finish.cpp
	CV_RegisterVar(&cv_show_riders_finish_ticker);
}

void K_LoadKartHUDGraphics(void)
{
	int32_t i, j;
	char buffer[9];

	// Null Stuff
	HU_UpdatePatch(&kp_nodraw, "K_TRNULL");

	// Stickers
	HU_UpdatePatch(&kp_timesticker[0], "K_STTIME");
	HU_UpdatePatch(&kp_timestickerwide[0], "K_STTIMW");
	HU_UpdatePatch(&kp_lapsticker[0], "K_STLAPS");
	HU_UpdatePatch(&kp_lapstickerbig[0], "K_STLAPB");
	HU_UpdatePatch(&kp_lapstickerbig2[0], "K_STLA2B");
	HU_UpdatePatch(&kp_lapstickerwide[0], "K_STLAPW");
	HU_UpdatePatch(&kp_lapstickernarrow[0], "K_STLAPN");
	HU_UpdatePatch(&kp_bumpersticker[0], "K_STBALN");
	HU_UpdatePatch(&kp_bumperstickerwide[0], "K_STBALW");
	HU_UpdatePatch(&kp_karmasticker[0], "K_STKARM");
	HU_UpdatePatch(&kp_timeoutsticker[0], "K_STTOUT");

	// Colored Stickers
	HU_UpdatePatch(&kp_timesticker[1], "K_SCTIME");
	HU_UpdatePatch(&kp_timestickerwide[1], "K_SCTIMW");
	HU_UpdatePatch(&kp_lapsticker[1], "K_SCLAPS");
	HU_UpdatePatch(&kp_lapstickerbig[1], "K_SCLAPB");
	HU_UpdatePatch(&kp_lapstickerbig2[1], "K_SCLA2B");
	HU_UpdatePatch(&kp_lapstickerwide[1], "K_SCLAPW");
	HU_UpdatePatch(&kp_lapstickernarrow[1], "K_SCLAPN");
	HU_UpdatePatch(&kp_bumpersticker[1], "K_SCBALN");
	HU_UpdatePatch(&kp_bumperstickerwide[1], "K_SCBALW");
	HU_UpdatePatch(&kp_karmasticker[1], "K_SCKARM");
	HU_UpdatePatch(&kp_timeoutsticker[1], "K_SCTOUT");

	// Splitscreen
	HU_UpdatePatch(&kp_splitlapflag, "K_SPTLAP");
	HU_UpdatePatch(&kp_splitkarmabomb, "K_SPTKRM");

	// Starting countdown
	HU_UpdatePatch(&kp_startcountdown[0], "K_CNT3A");
	HU_UpdatePatch(&kp_startcountdown[1], "K_CNT2A");
	HU_UpdatePatch(&kp_startcountdown[2], "K_CNT1A");
	HU_UpdatePatch(&kp_startcountdown[3], "K_CNTGOA");
	HU_UpdatePatch(&kp_startcountdown[4], "K_DUEL1");
	HU_UpdatePatch(&kp_startcountdown[5], "K_CNT3B");
	HU_UpdatePatch(&kp_startcountdown[6], "K_CNT2B");
	HU_UpdatePatch(&kp_startcountdown[7], "K_CNT1B");
	HU_UpdatePatch(&kp_startcountdown[8], "K_CNTGOB");
	HU_UpdatePatch(&kp_startcountdown[9], "K_DUEL2");

	// Splitscreen
	HU_UpdatePatch(&kp_startcountdown[10], "K_SMC3A");
	HU_UpdatePatch(&kp_startcountdown[11], "K_SMC2A");
	HU_UpdatePatch(&kp_startcountdown[12], "K_SMC1A");
	HU_UpdatePatch(&kp_startcountdown[13], "K_SMCGOA");
	HU_UpdatePatch(&kp_startcountdown[14], "K_SDUEL1");
	HU_UpdatePatch(&kp_startcountdown[15], "K_SMC3B");
	HU_UpdatePatch(&kp_startcountdown[16], "K_SMC2B");
	HU_UpdatePatch(&kp_startcountdown[17], "K_SMC1B");
	HU_UpdatePatch(&kp_startcountdown[18], "K_SMCGOB");
	HU_UpdatePatch(&kp_startcountdown[19], "K_SDUEL2");

	// Finish
	HU_UpdatePatch(&kp_racefinish[0], "K_FINA");
	HU_UpdatePatch(&kp_racefinish[1], "K_FINB");
	// Splitscreen
	HU_UpdatePatch(&kp_racefinish[2], "K_SMFINA");
	HU_UpdatePatch(&kp_racefinish[3], "K_SMFINB");
	// 2P splitscreen
	HU_UpdatePatch(&kp_racefinish[4], "K_2PFINA");
	HU_UpdatePatch(&kp_racefinish[5], "K_2PFINB");

	// Position numbers
	snprintf(buffer, sizeof(buffer), "K_POSNxx");
	for (i = 0; i < NUMPOSNUMS; i++)
	{
		buffer[6] = '0'+i;
		for (j = 0; j < NUMPOSFRAMES; j++)
		{
			//snprintf(buffer, sizeof(buffer), "K_POSN%d%d", i, j);
			buffer[7] = '0'+j;
			HU_UpdatePatch(&kp_positionnum[i][j], "%s", buffer);
		}
	}

	snprintf(buffer, sizeof(buffer), "K_POSNWx");
	for (i = 0; i < NUMWINFRAMES; i++)
	{
		buffer[7] = '0'+i;
		HU_UpdatePatch(&kp_winnernum[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "OPPRNKxx");
	for (i = 0; i <= MAXPLAYERS; i++)
	{
		buffer[6] = '0'+(i/10);
		buffer[7] = '0'+(i%10);
		HU_UpdatePatch(&kp_facenum[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "K_CHILIx");
	for (i = 0; i < 8; i++)
	{
		buffer[7] = '0'+(i+1);
		HU_UpdatePatch(&kp_facehighlight[i], "%s", buffer);

		kp_facehighlight[i]->pivot.x = kp_facehighlight[i]->width / 2;
		kp_facehighlight[i]->pivot.y = kp_facehighlight[i]->height / 2;
		kp_facehighlight[i]->alignflags |= PATCHALIGN_USEPIVOTS;
	}

	// Special minimap icons
	HU_UpdatePatch(&kp_nocontestminimap, "MINIDEAD");
	HU_UpdatePatch(&kp_itemboxminimap, "K_ITMMM");

	// Rings & Lives
	HU_UpdatePatch(&kp_ringsticker[0], "K_RNGHD");
	HU_UpdatePatch(&kp_ringsticker[1], "K_RNGHL");
	HU_UpdatePatch(&kp_ringsticker[2], "K_RNGHDC");
	HU_UpdatePatch(&kp_ringsticker[3], "K_RNGHLC");
	HU_UpdatePatch(&kp_ringsplitscreen, "K_RNGSS");
	HU_UpdatePatch(&kp_ringdebtminus, "K_RNGDM");
	HU_UpdatePatch(&kp_ringdebtminussmall, "K_RNGSM");

	// Speedometer
	HU_UpdatePatch(&kp_speedometersticker[0], "SP_SMSTC");

	// Speedometer Sticker Color
	HU_UpdatePatch(&kp_speedometersticker[1], "SC_SMSTC");

	// Dial speedometer
	HU_UpdatePatch(&kp_dial, "K_DSDIAL");

	kp_dial->pivot.x = kp_dial->width / 2;
	kp_dial->pivot.y = kp_dial->height / 2;
	kp_dial->alignflags |= PATCHALIGN_USEPIVOTS;

	HU_UpdatePatch(&kp_dialfinish, "K_DILFIN");
	HU_UpdatePatch(&kp_dialbase[0], "K_DSPBS1");
	HU_UpdatePatch(&kp_dialbase[1], "K_DSPBS2");
	HU_UpdatePatch(&kp_dialbase[2], "K_DSPBC1");
	HU_UpdatePatch(&kp_dialbase[3], "K_DSPBC2");
	HU_UpdatePatch(&kp_speedpatchesdial[0], "SP_DKMH");
	HU_UpdatePatch(&kp_speedpatchesdial[1], "SP_DMPH");
	HU_UpdatePatch(&kp_speedpatchesdial[2], "SP_DFRAC");
	HU_UpdatePatch(&kp_speedpatchesdial[3], "SP_DPERC");
	HU_UpdatePatch(&kp_speedpatchesdial[4], "SC_DKMH");
	HU_UpdatePatch(&kp_speedpatchesdial[5], "SC_DMPH");
	HU_UpdatePatch(&kp_speedpatchesdial[6], "SC_DFRAC");
	HU_UpdatePatch(&kp_speedpatchesdial[7], "SC_DPERC");

	snprintf(buffer, sizeof(buffer), "K_DSPNMx");
	for (i = 0; i < 10; i++)
	{
		buffer[7] = '0'+(i%10);
		HU_UpdatePatch(&kp_dialnum[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "K_DSPNCx");
	for (i = 0; i < 10; i++)
	{
		buffer[7] = '0'+(i%10);
		HU_UpdatePatch(&kp_dialnum[i+10], "%s", buffer);
	}

	// Driftgauge Stickers
	HU_UpdatePatch(&kp_driftgauge[0], "K_DGAU0"); // Spee
	HU_UpdatePatch(&kp_driftgauge[1], "K_DGAU1"); // Achii
	HU_UpdatePatch(&kp_driftgauge[2], "K_WDGSG"); // Wifi
	HU_UpdatePatch(&kp_driftgauge[3], "K_WDGBG"); // Anywhere you go
	HU_UpdatePatch(&kp_driftgauge[4], "K_DGAU3"); // Chaotix
	HU_UpdatePatch(&kp_driftgauge[5], "K_DGAU"); // Legacy
	HU_UpdatePatch(&kp_driftgauge[6], "K_DGSU"); // Legacy Small

	HU_UpdatePatch(&kp_driftgauge[7], "K_DGAUC0");
	HU_UpdatePatch(&kp_driftgauge[8], "K_DGAUC1");
	HU_UpdatePatch(&kp_driftgauge[9], "K_WDGSGC");
	HU_UpdatePatch(&kp_driftgauge[10], "K_WDGBGC");
	HU_UpdatePatch(&kp_driftgauge[11], "K_DGAUC3");
	HU_UpdatePatch(&kp_driftgauge[12], "K_DCAU");
	HU_UpdatePatch(&kp_driftgauge[13], "K_DCSU");


	// Driftgauge Parts
	HU_UpdatePatch(&kp_driftgaugeparts[0], "K_WDGM1");
	HU_UpdatePatch(&kp_driftgaugeparts[1], "K_WDGM2");
	HU_UpdatePatch(&kp_driftgaugeparts[2], "K_WDGM3");
	HU_UpdatePatch(&kp_driftgaugeparts[3], "K_WDGM4");
	HU_UpdatePatch(&kp_driftgaugeparts[4], "K_DGAU3M");

	// S-Monitor Sparkles
	HU_UpdatePatch(&kp_smonitorsparkle, "ALTISPRK");

	kp_smonitorsparkle->pivot.x = kp_smonitorsparkle->width / 2;
	kp_smonitorsparkle->pivot.y = kp_smonitorsparkle->height / 2;
	kp_smonitorsparkle->alignflags |= PATCHALIGN_USEPIVOTS;
	
	// Flamometer UI Elements
	HU_UpdatePatch(&kp_flamometer[0], "THERMOBACK");
	HU_UpdatePatch(&kp_flamometer[1], "THERMOFUEL");
	HU_UpdatePatch(&kp_flamometer[2], "THERMOTEMP");
	HU_UpdatePatch(&kp_flamometer[3], "THERMOMETRE");
	HU_UpdatePatch(&kp_flamometer[4], "THERMCBACK");
	HU_UpdatePatch(&kp_flamometer[5], "THERMCMETRE");

	//Flamometer Fire
	{
		const char* patchNames[] = {
			"THFIREX",
			"THFIRE1",
			"THFIRE2",
			"THFIRE3",
			"THFIRE4",
			"THFIRE5",
			"THFIRE6",
			"THFIRE7",
			"THFIRE8",
			"THFIRCX",
			"THFIRC1",
			"THFIRC2",
			"THFIRC3",
			"THFIRC4",
			"THFIRC5",
			"THFIRC6",
			"THFIRC7",
			"THFIRC8"
		};

		for (size_t m = 0; m < sizeof(patchNames) / sizeof(patchNames[0]); ++m)
		{
			kp_flamefire[m] = W_CachePatchName(patchNames[m], PU_HUDGFX);
		}
	}

	// Speedometer labels
	HU_UpdatePatch(&kp_speedometerlabel[0], "SP_MKMH");
	HU_UpdatePatch(&kp_speedometerlabel[1], "SP_MMPH");
	HU_UpdatePatch(&kp_speedometerlabel[2], "SP_MFRAC");
	HU_UpdatePatch(&kp_speedometerlabel[3], "SP_MPERC");

	{
		const char* patchNames[] = {
			"K_KZSP1", "K_KZSP2", "K_KZSP3", "K_KZSP4", "K_KZSP5",
			"K_KZSP6", "K_KZSP7", "K_KZSP8", "K_KZSP9", "K_KZSP10",
			"K_KZSP11", "K_KZSP12", "K_KZSP13", "K_KZSP14", "K_KZSP15",
			"K_KZSP16", "K_KZSP17", "K_KZSP18", "K_KZSP19", "K_KZSP20",
			"K_KZSP21", "K_KZSP22", "K_KZSP23", "K_KZSP24", "K_KZSP25"
		};

		for (size_t m = 0; m < sizeof(patchNames) / sizeof(patchNames[0]); ++m)
		{
			kp_kartzspeedo[m] = W_CachePatchName(patchNames[m], PU_HUDGFX);
		}
	}

	{
		const char* patchNames[] = {
			"K_KZSS1", "K_KZSS2", "K_KZSS3", "K_KZSS4", "K_KZSS5",
			"K_KZSS6", "K_KZSS7", "K_KZSS8", "K_KZSS9", "K_KZSS10",
			"K_KZSS11", "K_KZSS12", "K_KZSS13", "K_KZSS14", "K_KZSS15",
			"K_KZSS16", "K_KZSS17", "K_KZSS18", "K_KZSS19", "K_KZSS20",
			"K_KZSS21", "K_KZSS22", "K_KZSS23", "K_KZSS24", "K_KZSS25"
		};

		for (size_t m = 0; m < sizeof(patchNames) / sizeof(patchNames[0]); ++m)
		{
			kp_kartzspeedo_smol[m] = W_CachePatchName(patchNames[m], PU_HUDGFX);
		}
	}


	// Extra ranking icons
	HU_UpdatePatch(&kp_rankbumper, "K_BLNICO");
	HU_UpdatePatch(&kp_tinybumper[0], "K_BLNA");
	HU_UpdatePatch(&kp_tinybumper[1], "K_BLNB");
	HU_UpdatePatch(&kp_ranknobumpers, "K_NOBLNS");

	// Battle graphics
	HU_UpdatePatch(&kp_battlewin, "K_BWIN");
	HU_UpdatePatch(&kp_battlecool, "K_BCOOL");
	HU_UpdatePatch(&kp_battlelose, "K_BLOSE");
	HU_UpdatePatch(&kp_battlewait, "K_BWAIT");
	HU_UpdatePatch(&kp_battleinfo, "K_BINFO");
	HU_UpdatePatch(&kp_wanted, "K_WANTED");
	HU_UpdatePatch(&kp_wantedsplit, "4PWANTED");
	HU_UpdatePatch(&kp_wantedreticle, "MMAPWANT");
	HU_UpdatePatch(&kp_minimapdot, "MMAPDOT");

	// Kart Item Windows
	HU_UpdatePatch(&kp_itembg[0], "K_ITBG");
	HU_UpdatePatch(&kp_itembg[1], "K_ITBGD");
	HU_UpdatePatch(&kp_itembg[2], "K_ISBG");
	HU_UpdatePatch(&kp_itembg[3], "K_ISBGD");
	HU_UpdatePatch(&kp_itembg[4], "K_ITBC");
	HU_UpdatePatch(&kp_itembg[5], "K_ITBCD");
	HU_UpdatePatch(&kp_itembg[6], "K_ISBC");
	HU_UpdatePatch(&kp_itembg[7], "K_ISBCD");
	HU_UpdatePatch(&kp_itemalt[0], "K_ALTITM");
	HU_UpdatePatch(&kp_itemalt[1], "K_ALTITS");
	HU_UpdatePatch(&kp_itemalt[2], "K_ALTIMM");
	HU_UpdatePatch(&kp_itemalt[3], "K_ALTISM");
	HU_UpdatePatch(&kp_itemtimer[0], "K_ITIMER");
	HU_UpdatePatch(&kp_itemtimer[1], "K_ISIMER");
	HU_UpdatePatch(&kp_itemmulsticker[0], "K_ITMUL");
	HU_UpdatePatch(&kp_itemmulsticker[1], "K_ISMUL");
	HU_UpdatePatch(&kp_itemmulsticker[2], "K_ITMULC");
	HU_UpdatePatch(&kp_itemmulsticker[3], "K_ISMULC");
	HU_UpdatePatch(&kp_itemx, "K_ITX");

	for (i = 0; i < numkartitems; i++)
	{
		kartitem_t *item = &kartitems[i];

		int32_t n = sizeof(item->graphics)/sizeof(*item->graphics);
		for (j = 0; j < n; j++)
		{
			kartitemgraphics_t *graphics = &item->graphics[j];
			for (int32_t k = 0; k < graphics->numpatches; k++)
			{
				// have to manually do this because HU_UpdatePatch only checks graphics from added WADs during partadd
				// UUUUUGGGGGGGHHHHHHHHHHHHHHHHHHh
				lumpnum_t lump = W_CheckNumForName(graphics->patchnames[k]);
				graphics->patches[k] = lump == LUMPERROR ? missingpat : W_CachePatchNum(lump, PU_HUDGFX);
			}
		}
		R_AddKartItemSprites(item);
	}

	// CHECK indicators
	snprintf(buffer, sizeof(buffer), "K_CHECKx");
	for (i = 0; i < 10; i++)
	{
		buffer[7] = '1'+i;
		HU_UpdatePatch(&kp_check[i], "%s", buffer);
	}
	HU_UpdatePatch(&kp_check[10], "K_CHECKA");

	// Eggman warning numbers
	snprintf(buffer, sizeof(buffer), "K_EGGNx");
	for (i = 0; i < 4; i++)
	{
		buffer[6] = '0'+i;
		HU_UpdatePatch(&kp_eggnum[i], "%s", buffer);
	}

	// First person mode
	HU_UpdatePatch(&kp_fpview[0], "VIEWA0");
	HU_UpdatePatch(&kp_fpview[1], "VIEWB0D0");
	HU_UpdatePatch(&kp_fpview[2], "VIEWC0E0");

	// Input UI Stick
	HU_UpdatePatch(&joybacking, "JOYBCK");
	HU_UpdatePatch(&joyknob, "JOYKNB");
	HU_UpdatePatch(&joyshadow, "JOYSHD");

	// Input UI Wheel
	HU_UpdatePatch(&kp_inputwheel, "B_WHEEL0");
	kp_inputwheel->pivot.x = 
		(kp_inputwheel->width / 2) + kp_inputwheel->leftoffset;
	kp_inputwheel->pivot.y = 
		(kp_inputwheel->height / 2) + kp_inputwheel->topoffset;
	kp_inputwheel->alignflags |=
		(PATCHALIGN_AUTOCENTER | PATCHALIGN_USEPIVOTS);

	HU_UpdatePatch(&kp_inputwheel_shadow, "B_WHEEL1");
	kp_inputwheel_shadow->pivot.x = 
		(kp_inputwheel_shadow->width / 2) + kp_inputwheel_shadow->leftoffset;
	kp_inputwheel_shadow->pivot.y = 
		(kp_inputwheel_shadow->height / 2) + kp_inputwheel_shadow->topoffset;
	kp_inputwheel_shadow->alignflags |=
		(PATCHALIGN_AUTOCENTER | PATCHALIGN_USEPIVOTS);

	// HERE COMES A NEW CHALLENGER
	snprintf(buffer, sizeof(buffer), "K_CHALxx");
	for (i = 0; i < 25; i++)
	{
		buffer[6] = '0'+((i+1)/10);
		buffer[7] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_challenger[i], "%s", buffer);
	}

	// Lap start animation
	snprintf(buffer, sizeof(buffer), "K_LAP0x");
	for (i = 0; i < 7; i++)
	{
		buffer[6] = '0'+(i+1);
		HU_UpdatePatch(&kp_lapanim_lap[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "K_LAPFxx");
	for (i = 0; i < 11; i++)
	{
		buffer[6] = '0'+((i+1)/10);
		buffer[7] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_lapanim_final[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "K_LAPNxx");
	for (i = 0; i < 10; i++)
	{
		buffer[6] = '0'+i;
		for (j = 0; j < 3; j++)
		{
			buffer[7] = '0'+(j+1);
			HU_UpdatePatch(&kp_lapanim_number[i][j], "%s", buffer);
		}
	}

	snprintf(buffer, sizeof(buffer), "K_LAPE0x");
	for (i = 0; i < 2; i++)
	{
		buffer[7] = '0'+(i+1);
		HU_UpdatePatch(&kp_lapanim_emblem[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "K_LAPH0x");
	for (i = 0; i < 3; i++)
	{
		buffer[7] = '0'+(i+1);
		HU_UpdatePatch(&kp_lapanim_hand[i], "%s", buffer);
	}

	HU_UpdatePatch(&kp_yougotem, "YOUGOTEM");

	snprintf(buffer, sizeof(buffer), "K_BOSB0x");
	for (i = 0; i < 8; i++)
	{
		buffer[7] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_bossbar[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "K_BOSR0x");
	for (i = 0; i < 4; i++)
	{
		buffer[7] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_bossret[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "HITEARxx");
	for (i = 0; i < 2; i++)
	{
		buffer[6] = 'A'+i;

		for (j = 0; j < 2; j++)
		{
			buffer[7] = '0'+j;
			HU_UpdatePatch(&kp_itemtarget_arrow[i][j], "%s", buffer);
		}
	}

	snprintf(buffer, sizeof(buffer), "HUDITEDx");
	for (i = 0; i < 2; i++)
	{
		buffer[7] = '0'+i;
		HU_UpdatePatch(&kp_itemtarget_far_text[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "HUDITECx");
	for (i = 0; i < 2; i++)
	{
		buffer[7] = '0'+i;
		HU_UpdatePatch(&kp_itemtarget_icon[i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "HUDITEBx");
	for (i = 0; i < 2; i++)
	{
		buffer[7] = '0'+i;
		HU_UpdatePatch(&kp_itemtarget_far[0][i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "HUDI4PBx");
	for (i = 0; i < 2; i++)
	{
		buffer[7] = '0'+i;
		HU_UpdatePatch(&kp_itemtarget_far[1][i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "HUDITEAx");
	for (i = 0; i < 8; i++)
	{
		buffer[7] = '0'+i;
		HU_UpdatePatch(&kp_itemtarget_near[0][i], "%s", buffer);
	}

	snprintf(buffer, sizeof(buffer), "HUDI4PAx");
	for (i = 0; i < 8; i++)
	{
		buffer[7] = '0'+i;
		HU_UpdatePatch(&kp_itemtarget_near[1][i], "%s", buffer);
	}

	// Rank Tiers
	sprintf(buffer, "BKRNKxx");
	for (i = 0; i < NUMRANKTIERS; i++)
	{
		buffer[5] = '0'+((i+1)/10);
		buffer[6] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_ranktier[i], "%s", buffer);
	}

	sprintf(buffer, "BKRNSxx");
	for (i = 0; i < NUMRANKTIERS; i++)
	{
		buffer[5] = '0'+((i+1)/10);
		buffer[6] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_ranktier4p[i], "%s", buffer);
	}
}

//}

int32_t ITEM_X, ITEM_Y;	// Item Window
int32_t TIME_X, TIME_Y;	// Time Sticker
int32_t LAPS_X, LAPS_Y;	// Lap Sticker
int32_t RING_X, RING_Y;	// Player Rings
int32_t SPDM_X, SPDM_Y;	// Speedometer
int32_t ACCE_X, ACCE_Y;	// Accessibility
int32_t DRAT_X, DRAT_Y;	// Drafting
int32_t LIVE_X, LIVE_Y;	// Stats and Lives
int32_t POSI_X, POSI_Y;	// Position Number
int32_t FACE_X, FACE_Y;	// Top-four Faces
int32_t STCD_X, STCD_Y;	// Starting countdown
int32_t CHEK_Y;			// CHECK graphic
int32_t MINI_X, MINI_Y;	// Minimap
int32_t WANT_X, WANT_Y;	// Battle WANTED poster
int32_t RANK_X, RANK_Y;	// Race ranks

// This is for the P2 and P4 side of splitscreen. Then we'll flip P1's and P2's to the bottom with V_SPLITSCREEN.
int32_t ITEM2_X, ITEM2_Y;
int32_t LAPS2_X, LAPS2_Y;
int32_t RING2_X, RING2_Y;
int32_t POSI2_X, POSI2_Y;
int32_t DRAT2_X, DRAT2_Y;
int32_t LIVE2_X, LIVE2_Y;
int32_t RANK2_X, RANK2_Y;

void K_ReloadHUDColorCvar(void)
{
	HudColor_cons_t[0].value = 0;
	HudColor_cons_t[0].strvalue = "Skin Color";

	for (int32_t i = 1; i < numskincolors; i++)
	{
		HudColor_cons_t[i].value = i;
		HudColor_cons_t[i].strvalue = skincolors[i].name;				// SRB2kart
	}

	HudColor_cons_t[MAXSKINCOLORS].value = 0;
	HudColor_cons_t[MAXSKINCOLORS].strvalue = NULL;
}


dboolean K_UseColorHud(void)
{
	return cv_colorizedhud.value;
}

skincolornum_t K_GetHudColor(void)
{
	if (cv_colorizedhud.value && cv_colorizedhudcolor.value) return cv_colorizedhudcolor.value;

	return ((stplyr && gamestate == GS_LEVEL) ? stplyr->skincolor : cv_playercolor[0].value);
}

static dboolean K_IsHighResolution(void)
{
	return (vid.width >= 640 && vid.height >= 400);
}

dboolean K_UseHighResPortraits(void)
{
	return (cv_highresportrait.value && K_IsHighResolution());
}

static dboolean K_BigLapSticker(void)
{
	return ((cv_numlaps.value > 9) && (!stplyr->exiting));
}

patch_t *K_getItemBoxPatch(dboolean small, dboolean dark)
{
	uint8_t ofs = (cv_darkitembox.value && dark ? 1 : 0) + (small ? 2 : 0);
	return (cv_colorizeditembox.value && K_UseColorHud()) ? kp_itembg[4+ofs] : kp_itembg[ofs];
}

patch_t *K_getItemMulPatch(dboolean small)
{
	uint8_t ofs = small ? 1 : 0;
	return K_UseColorHud() ? kp_itemmulsticker[2+ofs] : kp_itemmulsticker[ofs];
}

patch_t *K_getItemAltPatch(dboolean small, dboolean multimode)
{
	uint8_t ofs = (small ? 1 : 0) + (multimode ? 2 : 0);
	return kp_itemalt[ofs];
}

dboolean K_ShowAltItemIcon(kartitemtype_e type, dboolean small)
{
	return K_GetCachedItemPatchEx(type, small, 0, (int8_t)true) == missingpat;
}

// This version of the function was prototyped in Lua by Nev3r ... a HUGE thank you goes out to them!
void K_ObjectTracking(trackingResult_t *result, const vector3_t *point, dboolean reverse)
{
#define NEWTAN(x) FINETANGENT(((x + ANGLE_90) >> ANGLETOFINESHIFT) & 4095) // tan function used by Lua
#define NEWCOS(x) FINECOSINE((x >> ANGLETOFINESHIFT) & FINEMASK)

	angle_t viewpointAngle, viewpointAiming, viewpointRoll;

	int32_t screenWidth, screenHeight;
	fixed_t screenHalfW, screenHalfH;

	const fixed_t baseFov = 90*FRACUNIT;
	fixed_t fovDiff, fov, fovTangent, fg;

	fixed_t h;
	int32_t da, dp;

	uint8_t cameraNum = R_GetViewNumber();

	I_Assert(result != NULL);
	I_Assert(point != NULL);

	// Initialize defaults
	result->x = result->y = 0;
	result->scale = FRACUNIT;
	result->onScreen = false;

	// Take the view's properties as necessary.
	if (reverse)
	{
		viewpointAngle = (int32_t)(viewangle + ANGLE_180);
		viewpointAiming = (int32_t)InvAngle(aimingangle);
		viewpointRoll = (int32_t)viewroll;
	}
	else
	{
		viewpointAngle = (int32_t)viewangle;
		viewpointAiming = (int32_t)aimingangle;
		viewpointRoll = (int32_t)InvAngle(viewroll);
	}

	// Calculate screen size adjustments.
	screenWidth = vid.scaledwidth;
	screenHeight = vid.scaledheight;

	if (r_splitscreen >= 2)
	{
		// Half-wide screens
		screenWidth >>= 1;
	}

	if (r_splitscreen >= 1)
	{
		// Half-tall screens
		screenHeight >>= 1;
	}

	screenHalfW = (screenWidth >> 1) << FRACBITS;
	screenHalfH = (screenHeight >> 1) << FRACBITS;

	// Calculate FOV adjustments.
	fovDiff = cv_fov[cameraNum].value - baseFov;
#ifdef HWRENDER
	fov = ((baseFov - fovDiff) / 2) - (stplyr->fovadd / 2);
#else
	fov = ((baseFov - fovDiff) / 2);
#endif
	fovTangent = NEWTAN(FixedAngle(fov));

	if (r_splitscreen == 1)
	{
		// Splitscreen FOV is adjusted to maintain expected vertical view
		fovTangent = 10*fovTangent/17;
	}

	fg = (screenWidth >> 1) * fovTangent;

	// Determine viewpoint factors.
	h = R_PointToDist2(point->x, point->y, viewx, viewy);
	da = AngleDeltaSigned(viewpointAngle, R_PointToAngle2(viewx, viewy, point->x, point->y));
	dp = AngleDeltaSigned(viewpointAiming, R_PointToAngle2(0, 0, h, viewz));

	if (reverse)
	{
		da = -(da);
	}

	// Set results relative to top left!
	result->x = FixedMul(NEWTAN(da), fg);
	result->y = FixedMul((NEWTAN(viewpointAiming) - FixedDiv((point->z - viewz), 1 + FixedMul(NEWCOS(da), h))), fg);

	result->angle = da;
	result->pitch = dp;
	result->fov = fg;

	// Rotate for screen roll...
	if (viewpointRoll)
	{
		fixed_t tempx = result->x;
		viewpointRoll >>= ANGLETOFINESHIFT;
		result->x = FixedMul(FINECOSINE(viewpointRoll), tempx) - FixedMul(FINESINE(viewpointRoll), result->y);
		result->y = FixedMul(FINESINE(viewpointRoll), tempx) + FixedMul(FINECOSINE(viewpointRoll), result->y);
	}

	// Flipped screen?
	if (encoremode)
	{
		result->x = -result->x;
	}

	if (camera[cameraNum].postimgflags & POSTIMG_FLIP)
	{
		result->y = -result->y;
	}

	// Center results.
	result->x += screenHalfW;
	result->y += screenHalfH;

	result->scale = FixedDiv(screenHalfW, h+1);

	result->onScreen = !((abs(da) > ANG60) || (abs(AngleDeltaSigned(viewpointAiming, R_PointToAngle2(0, 0, h, (viewz - point->z)))) > ANGLE_45));

	// Cheap dirty hacks for some split-screen related cases
	if (result->x < 0 || result->x > (screenWidth << FRACBITS))
	{
		result->onScreen = false;
	}

	if (result->y < 0 || result->y > (screenHeight << FRACBITS))
	{
		result->onScreen = false;
	}

	// adjust to non-green-resolution screen coordinates
	result->x -= ((vid.scaledwidth) - BASEVIDWIDTH)<<(FRACBITS-((r_splitscreen >= 2) ? 2 : 1));
	result->y -= ((vid.scaledheight) - BASEVIDHEIGHT)<<(FRACBITS-((r_splitscreen >= 1) ? 2 : 1));

	return;

#undef NEWTAN
#undef NEWCOS
}

static void K_initKartHUD(void)
{
	/*
		BASEVIDWIDTH  = 320
		BASEVIDHEIGHT = 200

		Item window graphic is 41 x 33

		Time Sticker graphic is 116 x 11
		Time Font is a solid block of (8 x [12) x 14], equal to 96 x 14
		Therefore, timestamp is 116 x 14 altogether

		Lap Sticker is 80 x 11
		Lap flag is 22 x 20
		Lap Font is a solid block of (3 x [12) x 14], equal to 36 x 14
		Therefore, lapstamp is 80 x 20 altogether

		Position numbers are 43 x 53

		Faces are 32 x 32
		Faces draw downscaled at 16 x 16
		Therefore, the allocated space for them is 16 x 67 altogether

		----

		ORIGINAL CZ64 SPLITSCREEN:

		Item window:
		if (!splitscreen) 	{ ICONX = 139; 				ICONY = 20; }
		else 				{ ICONX = BASEVIDWIDTH-315; ICONY = 60; }

		Time: 			   236, STRINGY(			   12)
		Lap:  BASEVIDWIDTH-304, STRINGY(BASEVIDHEIGHT-189)

	*/

	// Single Screen (defaults)
	// Item Window
	ITEM_X = 5 + cv_item_xoffset.value;							//   5
	ITEM_Y = 5 + cv_item_yoffset.value;							//   5
	// Level Timer
	TIME_X = BASEVIDWIDTH - 148 + cv_time_xoffset.value;		// 172
	TIME_Y = 9 + cv_time_yoffset.value;							//   9
	// Level Laps
	LAPS_X = 9 + cv_laps_xoffset.value;							//   9
	LAPS_Y = BASEVIDHEIGHT - 29 + cv_laps_yoffset.value;		// 171
	// Player Rings
	RING_X = 9 + cv_rings_xoffset.value;						//   9
	RING_Y = BASEVIDHEIGHT - 29 + cv_rings_yoffset.value;		// 171
	// Speedometer
	SPDM_X = 9 + cv_speed_xoffset.value;						//   9
	SPDM_Y = BASEVIDHEIGHT - 29 + cv_speed_yoffset.value;		// 171
	// Accessibility
	ACCE_X = 9 + cv_speed_xoffset.value;						//   9
	ACCE_Y = BASEVIDHEIGHT - 29 + cv_speed_yoffset.value;		// 171
	// Drafting
	DRAT_X = 172 + cv_draft_xoffset.value;						// 172
	DRAT_Y = BASEVIDHEIGHT - 41 + cv_draft_yoffset.value;		// 159
	// Lives and Stats
	LIVE_X = 9 + cv_lives_xoffset.value;							//   9
	LIVE_Y = BASEVIDHEIGHT - 29 + cv_lives_yoffset.value;		// 171
	// Position Number
	POSI_X = BASEVIDWIDTH  - 9 + cv_posi_xoffset.value;			// 268
	POSI_Y = BASEVIDHEIGHT - 9 + cv_posi_yoffset.value;			// 138
	// Top-Four Faces
	FACE_X = 9 + cv_face_xoffset.value;							//   9
	FACE_Y = 92 + cv_face_yoffset.value;						//  92
	// Starting countdown
	STCD_X = BASEVIDWIDTH/2 + cv_stcd_xoffset.value;			//   9
	STCD_Y = BASEVIDHEIGHT/2 + cv_stcd_yoffset.value;			//  92
	// CHECK graphic
	CHEK_Y = BASEVIDHEIGHT + cv_chek_yoffset.value;				// 200
	// Minimap
	MINI_X = BASEVIDWIDTH - 50 + cv_mini_xoffset.value;			// 270
	MINI_Y = (BASEVIDHEIGHT/2)-16 + cv_mini_yoffset.value; 		//  84
	// Battle WANTED poster
	WANT_X = BASEVIDWIDTH - 55 + cv_want_xoffset.value;			// 270
	WANT_Y = BASEVIDHEIGHT- 71 + cv_want_yoffset.value;			// 176
	// Rank Score
	RANK_X = 96 + cv_rank_xoffset.value;						//  96
	RANK_Y = BASEVIDHEIGHT - 50 + cv_rank_yoffset.value;		// 150

	const dboolean dialspeedometer = (cv_newspeedometer.value == 3 && r_splitscreen < 1);
	const dboolean bigspeedometer = (cv_newspeedometer.value == 0 || cv_newspeedometer.value == 2);

	if (dialspeedometer)  // Dial Speedometer
	{
		// This sits on top of the speedometer, so we need to adjust FURTHER for rings
		// Also: dial-unique offsets! What joy...

		if (K_RingsActive() == true)
		{
			RANK_X = 40 + cv_dialrank_xoffset.value;
			RANK_Y = (BASEVIDHEIGHT) - 53 + cv_dialrank_yoffset.value;
		}
		else
		{
			RANK_X = 41 + cv_dialrank_xoffset.value;
			RANK_Y = (BASEVIDHEIGHT) - 42 + cv_dialrank_yoffset.value;
		}
	}
	else if (bigspeedometer)  // Classic/Default or P-Meter Speedometer
	{
		if (K_RingsActive() == false)
		{
			// THREE OFFSET VALUES FOR ONE HUD ELEMENT?!
			RANK_X = 61 + cv_bigrank_xoffset.value;
			RANK_Y = (BASEVIDHEIGHT) - 72 + cv_bigrank_yoffset.value;
		}
	}

	if (r_splitscreen)	// Splitscreen
	{
		// Lock the positions in.
		ITEM_X = 5;
		ITEM_Y = 3;

		LAPS_X = 9;
		LAPS_Y = (BASEVIDHEIGHT/2)-24;

		RING_X = 9;
		RING_Y = (BASEVIDHEIGHT/2)-24;

		SPDM_X = 9;
		SPDM_Y = (BASEVIDHEIGHT/2)-24;

		POSI_X = BASEVIDWIDTH;
		POSI_Y = (BASEVIDHEIGHT/2)-2;

		DRAT_X = 172;
		DRAT_Y = (BASEVIDHEIGHT/2)-34;

		LIVE_X = 9;
		LIVE_Y = (BASEVIDHEIGHT/2)-24;

		STCD_X = BASEVIDWIDTH/2;
		STCD_Y = BASEVIDHEIGHT/4;

		MINI_X = BASEVIDWIDTH - 50;
		MINI_Y = (BASEVIDHEIGHT/2);

		RANK_X = 91;
		RANK_Y = (BASEVIDHEIGHT/2)-32;

		if (r_splitscreen > 1)	// 3P/4P Small Splitscreen
		{
			// 1P (top left)
			ITEM_X = -9;
			ITEM_Y = -8;

			LAPS_X = 3;
			LAPS_Y = (BASEVIDHEIGHT/2)-12;

			RING_X = 3;
			RING_Y = (BASEVIDHEIGHT/2)-12;

			POSI_X = 24;
			POSI_Y = (BASEVIDHEIGHT/2)-26;

			DRAT_X = 95;
			DRAT_Y = (BASEVIDHEIGHT/2)-32;

			LIVE_X = 3;
			LIVE_Y = (BASEVIDHEIGHT/2)-12;

			RANK_X = (BASEVIDWIDTH/2)-37;
			RANK_Y = 4;

			// 2P (top right)
			ITEM2_X = (BASEVIDWIDTH/2)-39;
			ITEM2_Y = -8;

			LAPS2_X = (BASEVIDWIDTH/2)-43;
			LAPS2_Y = (BASEVIDHEIGHT/2)-12;

			RING2_X = (BASEVIDWIDTH/2)-43;
			RING2_Y = (BASEVIDHEIGHT/2)-12;

			POSI2_X = (BASEVIDWIDTH/2)-4;
			POSI2_Y = (BASEVIDHEIGHT/2)-26;

			DRAT2_X = 95;
			DRAT2_Y = (BASEVIDHEIGHT/2)-32;

			LIVE2_X = (BASEVIDWIDTH/2)-43;
			LIVE2_Y = (BASEVIDHEIGHT/2)-12;

			RANK2_X = (BASEVIDWIDTH/2)+4;
			RANK2_Y = (BASEVIDHEIGHT)-12; // Bottom of the screen, for this

			// Reminder that 3P and 4P are just 1P and 2P splitscreen'd to the bottom.

			STCD_X = BASEVIDWIDTH/4;

			MINI_X = (3*BASEVIDWIDTH/4);
			MINI_Y = (3*BASEVIDHEIGHT/4);

			if (r_splitscreen > 2) // 4P-only
			{
				MINI_X = (BASEVIDWIDTH/2);
				MINI_Y = (BASEVIDHEIGHT/2);
			}
		}
	}
}

// RadioRacers
static trackingResult_t K_getRoulettePositionForTrackingPlayer(void)
{
	trackingResult_t result = {0};

	// No player object? Not bothering.
	const dboolean doesPlayerHaveMo = !((stplyr->mo == NULL || P_MobjWasRemoved(stplyr->mo)));
	if (!doesPlayerHaveMo)
		return result;

	vector3_t v = {
		R_InterpolateFixed(stplyr->mo->old_x, stplyr->mo->x) + stplyr->mo->sprxoff,
		R_InterpolateFixed(stplyr->mo->old_y, stplyr->mo->y) + stplyr->mo->spryoff,
		R_InterpolateFixed(stplyr->mo->old_z, stplyr->mo->z) + stplyr->mo->sprzoff + (stplyr->mo->height >> 1),
	};

	vector3_t v2 = {
		0,
		0,
		64 * stplyr->mo->scale * P_MobjFlip(stplyr->mo)
	};

	FV3_Add(&v,&v2);

	K_ObjectTracking(&result, &v, false);

	return result;
}

void K_getItemBoxDrawinfo(drawinfo_t *out, rouletteinfo_t *rinfo)
{
	int32_t fx, fy, fflags;
	dboolean flipamount = false;
	fixed_t baseHudScale = FRACUNIT;
	float baseHudScaleFloat = (float)((float)(baseHudScale) / (FRACUNIT));

	fflags = V_HUDTRANS;

	// pain and suffering defined below
	if (r_splitscreen < 2) // don't change shit for THIS splitscreen.
	{
		fx = ITEM_X;
		fy = ITEM_Y;

		// We are NOT supporting this for splitscreen, the vanilla layout is easier to read.
		if (cv_rouletteonplayer.value && r_splitscreen == 0)
		{
			trackingResult_t result = K_getRoulettePositionForTrackingPlayer();

			if(result.x != 0 && result.y != 0)
			{
				fflags = 0;
				rinfo->flags |= (RINFO_DRAWONPLAYER|RINFO_USECROP);
				/**
				 * This solution WILL obscure the player's view.
				 * The item roulette background is transparent .. but some items are pretty loud visually (e.g. flame shield)
				 * Making it scale any smaller than 75% is unreasonable (it spins pretty fast towards the end of a race)..
				 * so we'll just make it a bit translucent.
				 */
				baseHudScale = (3*FRACUNIT)/5; // 60%
				baseHudScaleFloat = (float)((float)(baseHudScale) / (FRACUNIT));

				rinfo->crop.x = rinfo->crop.y = (int)(8 * baseHudScaleFloat);

				// Upside down?
				const dboolean isupsidedown = (stplyr->mo->eflags & MFE_VERTICALFLIP);

				/**
				 * Offset it horizontally so it's closer to the center of the player.
				 * Offset it vertically so it's floating above the player.
				 */
				int32_t base_x = 22;
				int32_t base_y = 18;
				int32_t baseUpsideDown_y = 18;

				base_x = (int) (22 * baseHudScaleFloat);
				base_y = (int) ((35 * baseHudScaleFloat) + 2);
				baseUpsideDown_y = (int) ((18 * baseHudScaleFloat) + 2);

				fx = (result.x / FRACUNIT) - base_x; // 18 (+2)
				fy = (result.y / FRACUNIT) - (isupsidedown ? baseUpsideDown_y : base_y); // 15, 28

				// In case I forget the math..
				// ROULETTE_SPACING (36) * baseHudScale
				// If we're drawing the item box at 75% scale, then it's 36 * 75%;
				rinfo->intSpacing = (int32_t)((((float)(ROULETTE_SPACING)) * baseHudScaleFloat));
				rinfo->spacing = (rinfo->intSpacing << FRACBITS);
				rinfo->offset = FixedMul(rinfo->offset, FixedDiv(rinfo->spacing, ROULETTE_SPACING_FIXED));

				if (stplyr->exiting)
					fflags = V_HUDTRANS;
			}
		} else {
			fflags = V_SNAPTOTOP|V_SNAPTOLEFT|V_SPLITSCREEN;
		}
	}
	else // now we're having a fun game.
	{
		if (stplyrnum == 0 || stplyrnum == 2) // If we are P1 or P3...
		{
			fx = ITEM_X;
			fy = ITEM_Y;
			fflags = V_SNAPTOLEFT|V_SNAPTOTOP|V_SPLITSCREEN;
		}
		else // else, that means we're P2 or P4.
		{
			fx = ITEM2_X;
			fy = ITEM2_Y;
			fflags = V_SNAPTORIGHT|V_SNAPTOTOP|V_SPLITSCREEN;
			flipamount = true;
		}
	}

	out->x = fx;
	out->y = fy;
	out->flags = fflags;
	out->flipamount = flipamount;
	out->hudScale = baseHudScale;
	out->hudScaleFloat = baseHudScaleFloat;
}

void K_getLapsDrawinfo(drawinfo_t *out)
{
	int32_t fx, fy, splitflags = 0;

	// pain and suffering defined below
	if (r_splitscreen < 2)	// don't change shit for THIS splitscreen.
	{
		fx = LAPS_X;
		fy = LAPS_Y;
		splitflags = V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANS|V_SPLITSCREEN;
	}
	else
	{
		if (stplyrnum == 0 || stplyrnum == 2)	// If we are P1 or P3...
		{
			fx = LAPS_X;
			fy = LAPS_Y;
			splitflags = V_SNAPTOLEFT|V_SNAPTOBOTTOM|V_SPLITSCREEN;
		}
		else // else, that means we're P2 or P4.
		{
			fx = LAPS2_X;
			fy = LAPS2_Y;
			splitflags = V_SNAPTORIGHT|V_SNAPTOBOTTOM|V_SPLITSCREEN;
		}
	}

	out->x = fx;
	out->y = fy;
	out->flags = splitflags;
}

void K_getRingsDrawinfo(drawinfo_t *out)
{
	int32_t fx, fy, splitflags = 0;

	dboolean dialspeedometer = (cv_newspeedometer.value == 3 && r_splitscreen < 1);

	// pain and suffering defined below
	if (r_splitscreen < 2)	// don't change shit for THIS splitscreen.
	{
		fx = RING_X + ((dialspeedometer) ? 31 : 0);
		fy = RING_Y + ((dialspeedometer) ? 14 : 0);
		splitflags = V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANS|V_SPLITSCREEN;
	}
	else
	{
		if (stplyrnum == 0 || stplyrnum == 2)	// If we are P1 or P3...
		{
			fx = RING_X;
			fy = RING_Y;
			splitflags = V_SNAPTOLEFT|V_SNAPTOBOTTOM|V_SPLITSCREEN;
		}
		else // else, that means we're P2 or P4.
		{
			fx = RING2_X;
			fy = RING2_Y;
			splitflags = V_SNAPTORIGHT|V_SNAPTOBOTTOM|V_SPLITSCREEN;
		}
	}

	out->x = fx;
	out->y = fy;
	out->flags = splitflags;
}

void K_getMinimapDrawinfo(drawinfo_t *out)
{
	int32_t fx = MINI_X, fy = MINI_Y, fflags = (r_splitscreen < 2 ? V_SNAPTORIGHT : 0);	// flags should only be 0 when it's centered (4p split)

	fx -=minimapinfo.minimap_pic->width/2;
	fy -=minimapinfo.minimap_pic->height/2;

	out->x = fx;
	out->y = fy;
	out->flags = fflags;
}

static void K_DrawItemBar(int32_t fx, int32_t fy, fixed_t scale, int32_t fflags, fixed_t itembar, uint8_t colors[static 3])
{
	const dboolean fourp = r_splitscreen > 1;
	const int32_t barlength = (fourp ? 12 : 26)*scale;
	const int32_t length = min(barlength, FixedMul(itembar, barlength));
	const int32_t height = (fourp ? 1 : 2)*scale;
	const int32_t x = ((fx * FRACUNIT) + ((fourp ? 17 : 11) * scale)), y = ((fy  * FRACUNIT) + ((fourp ? 27 : 35) * scale));

	V_DrawSciencePatch(x, y, V_HUDTRANS|fflags, kp_itemtimer[fourp ? 1 : 0], scale);
	V_DrawFixedFill(x+scale, y+scale, min(scale, length), height, colors[2]|fflags); // the left edge
	V_DrawFixedFill(x+max(scale, length), y+scale, min(FRACUNIT, length), height, colors[2]|fflags); // the right edge
	if (!fourp)
		V_DrawFixedFill(x+2*scale, y+2*scale, max(0, length - 2*scale), scale, colors[1]|fflags); // the dulled underside
	V_DrawFixedFill(x+2*scale, y+scale, max(0, length - 2*scale), scale, colors[0]|fflags); // the shine
}

// TODO: use an actual patch overlay and clip it instead of using a rect, now that an actual patch can be added for this
static void K_DrawItemCooldown(int32_t fx, int32_t fy, int32_t fflags, tic_t timer, tic_t maxtimer, fixed_t scale)
{
	const dboolean fourp = r_splitscreen > 1;
	const int32_t rectTopFour = 13*scale;
	int32_t rectLeft = 5*scale;
	int32_t rectTop = 6*scale;
	int32_t rectSize = 40*scale;
	fixed_t prog = FixedDiv(timer, maxtimer);
	int32_t length = min(rectSize, FixedMul(rectSize, prog));
	if (timer > 0 && maxtimer > 0)
	{
		if (fourp)
		{
			rectLeft = 14*scale;
			rectTop = 14*scale;

			rectSize = 20*scale;
			length = min(rectSize, FixedMul(rectSize, prog));

			V_DrawFixedFill((fx << FRACBITS) + rectLeft, (fy << FRACBITS) + rectTopFour + (rectSize - length), rectSize, length, 2|fflags);
		}
		else
		{
			V_DrawFixedFill((fx << FRACBITS) + rectLeft, (fy << FRACBITS) + rectTop + (rectSize - length), rectSize, length, 2|fflags);
		}
	}
}

// see also MT_PLAYERARROW mobjthinker in p_mobj.c
static void K_drawKartItem(void)
{
	// ITEM_X = BASEVIDWIDTH-50;	// 270
	// ITEM_Y = 24;					//  24

	// Why write V_DrawScaledPatch calls over and over when they're all the same?
	// Set to 'no item' just in case.
	const dboolean tiny = r_splitscreen > 1;
	patch_t *localpatch = kp_nodraw;
	patch_t *localbg;
	dboolean dark = false;
	int32_t fx = 0, fy = 0, fflags = 0;	// final coords for hud and flags...
	fixed_t itembar = -1, flamebar = -1;
	uint16_t localcolor = SKINCOLOR_NONE;
	int8_t colormode = TC_RAINBOW;
	uint8_t *colmap = NULL;
	uint8_t *colormap = NULL;
	dboolean flipamount = false;	// Used for 3P/4P splitscreen to flip item amount stuff
	dboolean isalt = false;
	dboolean fade = false;

	// RadioRacers
	dboolean shouldDrawOnPlayer = false;

	if (stplyr->itemroulette)
	{
		const int32_t item = K_GetRollingRouletteItem(stplyr);

		if (K_GetHudColor())
			localcolor = K_GetHudColor();

		localpatch = K_GetCachedItemPatch(item, tiny, 0);
	}
	else
	{
		// S-Monitor nonsense necessitates we do this ahead of time
		dboolean candrawitemstatus = true;
		dboolean notincooldown = stplyr->itemusecooldown <= 0;

		if (stplyr->itemamount <= 0 && notincooldown)
			candrawitemstatus = false;

		if (stplyr->itemtype == 0 && notincooldown)
			candrawitemstatus = false;

		if (stplyr->itemtype >= numkartitems && stplyr->itemtype != MAXKARTITEMS)
			candrawitemstatus = false;

		// I'm doing this a little weird and drawing mostly in reverse order
		// The only actual reason is to make sneakers line up this way in the code below
		// This shouldn't have any actual baring over how it functions
		// Hyudoro is first, because we're drawing it on top of the player's current item
		if (stplyr->stolentimer > 0)
		{
			if (leveltime & 2)
				localpatch = K_GetCachedItemPatch(KITEM_HYUDORO, tiny, 0);
			else
				localpatch = kp_nodraw;
		}
		else if ((stplyr->stealingtimer > 0) && (leveltime & 2))
		{
			localpatch = K_GetCachedItemPatch(KITEM_HYUDORO, tiny, 0);
		}
		else if (stplyr->eggmanexplode > 1)
		{
			if (leveltime & 1)
				localpatch = K_GetCachedItemPatch(KITEM_EGGMAN, tiny, 0);
			else
				localpatch = kp_nodraw;
		}
		else if (stplyr->rocketsneakertimer > 1)
		{
			itembar = FixedDiv(stplyr->rocketsneakertimer, itemtime*3);

			if (leveltime & 1)
				localpatch = K_GetCachedItemPatch(KITEM_ROCKETSNEAKER, tiny, 0);
			else
				localpatch = kp_nodraw;
		}
		else if (stplyr->flametimer > 1)
		{
			itembar = FixedDiv(stplyr->flametimer, itemtime*3);
			flamebar = FixedDiv(stplyr->flamestore, FLAMESTOREMAX);
			dark = true;

			if ((stplyr->flamestore >= FLAMESTOREMAX-1) && (leveltime & 1))
			{
				colormode = TC_BLINK;
				localcolor = SKINCOLOR_WHITE;
			}

			if (leveltime & 1)
				localpatch = K_GetCachedItemPatch(KITEM_FLAMESHIELD, tiny, 0);
			else
				localpatch = kp_nodraw;
		}
		else if (stplyr->growshrinktimer > 0)
		{
			if (stplyr->growcancel > 0)
				itembar = FixedDiv(stplyr->growcancel, 26);

			if (leveltime & 1)
				localpatch = K_GetCachedItemPatch(KITEM_GROW, tiny, 0);
			else
				localpatch = kp_nodraw;
		}
		else if (stplyr->smonitortimer && (!candrawitemstatus))
		{
			if (stplyr->smonitortimer < SMONITORTIME)
				itembar = FixedDiv(stplyr->smonitortimer, max(1, stplyr->maxsmonitortime));

			if (stplyr->smonitorcancel > 0)
				flamebar = FixedDiv(stplyr->smonitorcancel, 26);

			if (leveltime & 1)
			{
				localpatch = K_GetCachedItemPatchEx(KITEM_INVINCIBILITY, tiny, 0, 1);
			}
			else
				localpatch = kp_nodraw;

			if (!K_SMonitorSlotHogging(stplyr))
			{
				fade = true;
			}
		}
		else if (K_GetShieldFromPlayer(stplyr) == KSHIELD_BUBBLE)
		{
			localpatch = K_GetCachedItemPatch(KITEM_BUBBLESHIELD, tiny, 0);
			dark = true;

			if (stplyr->bubbleblowup > 0 && leveltime & 1)
			{
				colormode = TC_BLINK;
				localcolor = SKINCOLOR_WHITE;
			}

			itembar = FixedDiv(stplyr->bubblehealth, MAXBUBBLEHEALTH);
		}
		else if (stplyr->attractioncharge > 0)
		{
			if (leveltime & 2)
			{
				if (stplyr->attractioncharge >= ATTRACTIONCHARGETIME)
				{
					colormode = TC_BLINK;
					localcolor = SKINCOLOR_WHITE;
				}
				localpatch = K_GetCachedItemPatch(KITEM_THUNDERSHIELD, tiny, 0);
			}
			else
			{
				localpatch = kp_nodraw;
			}

			itembar = FixedDiv(stplyr->attractioncharge, ATTRACTIONCHARGETIME);
		}
		else if (stplyr->bricktimer > 0)
		{
			if (leveltime & 2)
				localpatch = K_GetCachedItemPatch(KITEM_EGGBRICK, tiny, 0);
			else
				localpatch = kp_nodraw;
		}
		else if (stplyr->sadtimer > 0)
		{
			if (leveltime & 2)
				localpatch = K_GetCachedItemPatch(MAXKARTITEMS, tiny, 0);
			else
				localpatch = kp_nodraw;
		}
		else
		{
			if (!candrawitemstatus)
				return;

			if (stplyr->itemtype > 0 && stplyr->itemamount > 0)
				localpatch = K_GetCachedItemPatch(stplyr->itemtype, tiny, stplyr->itemamount);

			if (localpatch == NULL)
				localpatch = kp_nodraw; // diagnose underflows
			else if (K_IsKartItemAlternate(stplyr->itemtype) && K_ShowAltItemIcon(stplyr->itemtype, tiny))
				isalt = true;

			dark = K_GetItemFlags(stplyr->itemtype) & KIF_DARKBG;

			if ((stplyr->itemflags & IF_ITEMOUT) && !(leveltime & 1))
				localpatch = kp_nodraw;
		}

		if (stplyr->itemblink && (leveltime & 1))
		{
			colormode = TC_BLINK;

			switch (stplyr->itemblinkmode)
			{
				case KITEMBLINK_DEBUG:
				case KITEMBLINK_KARMA:
					localcolor = K_RainbowColor(leveltime);
					break;
				case KITEMBLINK_MASHED:
					localcolor = SKINCOLOR_RED;
					break;
				default:
					localcolor = SKINCOLOR_WHITE;
					break;
			}
		}
	}

	localbg = K_getItemBoxPatch(tiny, dark);
	drawinfo_t info;
	rouletteinfo_t rinfo;

	info.hudScale = FRACUNIT;
	info.hudScaleFloat = 1.0f;

	rinfo.crop.x = 8;
	rinfo.crop.y = 8;

	rinfo.offset = 0;
	rinfo.flags = 0;
	rinfo.offset = 0;
	rinfo.spacing = ROULETTE_SPACING_FIXED;
	rinfo.intSpacing = ROULETTE_SPACING;

	K_getItemBoxDrawinfo(&info, &rinfo);
	shouldDrawOnPlayer = ((rinfo.flags & RINFO_DRAWONPLAYER) == RINFO_DRAWONPLAYER);

	if ((shouldDrawOnPlayer) && (stplyr->flametimer > 1))
	{
		// Flamometer exists. You REALLY do not need this.
		itembar = -1;
		flamebar = -1;
	}

	int32_t hudtrans = V_GetHudTrans();
	int32_t transflag = (fade) ? V_HUDTRANSHALF : V_HUDTRANS;
	int32_t boxtransflag = V_HUDTRANS;
	int32_t transmul = FRACUNIT - (hudtrans * FRACUNIT / 10);

	if (hudtrans > 9)
	{
		// Vamoose
		return;
	}

	// Let the player pick and choose their visibility level.
	int32_t roulettetrans = cv_rouletteplayertrans.value;
	int32_t boxtrans = cv_rouletteplayerboxtrans.value;

	// RadioRacers
	if (shouldDrawOnPlayer)
	{
		dboolean rocketsmonitorbar = ((stplyr->rocketsneakertimer > 1) || (stplyr->smonitortimer));

		/**
		 * RadioRacers
		 *
		 * Flame Shield and Eggman Fakes get unique exceptions.
		 * This is because there are other visual elements that are more important than just the item icon.
		 *
		 * For Eggman Fakes, it's the impending timer.
		 * For Flame shields, it's the flame meter (more on that below).
		 * For rocket sneakers, it's the duration bar.
		 */

		// For Blan, the S-Monitor also needs duration visibility, so we dim the icon there, too.
		// Nice-to-have: some way for custom items to have this feature? Not sure yet.

		if (stplyr->itemtype == KITEM_FLAMESHIELD || stplyr->eggmanexplode > 1 || (rocketsmonitorbar)) {
			roulettetrans = (int32_t)((float)(roulettetrans) * (0.57143f * ((fade) ? 0.5f : 1.0f)));
		}

		if (stplyr->exiting)
		{
			roulettetrans = -1;
			boxtrans = -1;
		}

		if (roulettetrans > -1)
		{
			transflag = max(0, min(9, 10 - FixedMul(roulettetrans, transmul))) << V_ALPHASHIFT;
		}
		else
		{
			transflag = (fade) ? V_HUDTRANSHALF : V_HUDTRANS;
		}

		if (boxtrans > -1)
		{
			boxtransflag = max(0, min(9, 10 - FixedMul(boxtrans, transmul))) << V_ALPHASHIFT;
		}
		else
		{
			boxtransflag = V_HUDTRANS;
		}
	}

	fx = info.x;
	fy = info.y;
	fflags = info.flags;
	flipamount = info.flipamount;

	int32_t localpatchflags = (transflag|fflags);
	int32_t boxpatchflags = (boxtransflag|fflags);

	if (localcolor != SKINCOLOR_NONE)
	{
		colmap = R_GetTranslationColormap(colormode, localcolor, GTC_CACHE);
	}
	else if (K_GetItemFlags(stplyr->itemtype) & KIF_COLPATCH2PLAYER)
	{
		colmap = R_GetTranslationColormap(TC_DEFAULT, stplyr->skincolor, GTC_CACHE);
	}

	if (K_UseColorHud())
		colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);

	V_DrawFixedPatch(fx << FRACBITS, fy << FRACBITS, info.hudScale, boxpatchflags, localbg, colormap);

	// Then, the numbers:
	if (stplyr->itemamount >= K_GetItemNumberDisplayMin(stplyr->itemtype, tiny) && !stplyr->itemroulette)
	{
		localbg = K_getItemMulPatch(tiny);
		V_DrawFixedPatch((fx + (flipamount ? 48 : 0))<<FRACBITS, (fy<<FRACBITS), info.hudScale, V_HUDTRANS|fflags|(flipamount ? V_FLIP : 0), localbg, colormap); // flip this graphic for p2 and p4 in split and shift it.
		V_DrawFixedPatch(fx<<FRACBITS, fy<<FRACBITS, info.hudScale, localpatchflags, localpatch, colmap);

		// draw minecraft-style cooldown
		K_DrawItemCooldown(fx, fy, info.hudScale, V_HUDTRANSHALF|fflags, stplyr->itemusecooldown, stplyr->itemusecooldownmax);

		if (isalt)
			V_DrawFixedPatch(fx<<FRACBITS, fy<<FRACBITS, info.hudScale, localpatchflags, K_getItemAltPatch(tiny, true), colmap);

		if (tiny)
		{
			if (flipamount) // reminder that this is for 3/4p's right end of the screen.
				V_DrawString(fx+2, fy+31, V_ALLOWLOWERCASE|V_HUDTRANS|fflags, va("x%d", stplyr->itemamount));
			else
				V_DrawString(fx+24, fy+31, V_ALLOWLOWERCASE|V_HUDTRANS|fflags, va("x%d", stplyr->itemamount));
		}
		else
		{
			int32_t itemAmountFlags = V_HUDTRANS;

			if (shouldDrawOnPlayer)
				itemAmountFlags = (stplyr->exiting) ? V_HUDTRANS : V_30TRANS;

			V_DrawSciencePatch((fx << FRACBITS) + (28 * info.hudScale), (fy << FRACBITS) + (41 * info.hudScale), itemAmountFlags|fflags, kp_itemx, info.hudScale);
			V_DrawScalingKartStringAtFixed((fx << FRACBITS) + (38 * info.hudScale), (fy << FRACBITS) + (36 * info.hudScale), info.hudScale, itemAmountFlags|fflags, va("%d", stplyr->itemamount));
		}
	}
	else if (cv_fancyroulette.value && stplyr->itemroulette)// xitem-next styled animated roulette
	{
		fixed_t rfy = fy<<FRACBITS;
		int32_t fancyflags = V_HUDTRANS|fflags;
		int32_t cvarhudtrans = cv_translucenthud.value;
		int32_t animlength = cv_fancyroulettespeed.value;
		int32_t alpha = 0;
		int32_t shift = tiny ? 16 : 32;
		fixed_t shiftprog;
		fixed_t timefracs = (stplyr->playerstate == PST_DEAD) ? 0 : R_GetTimeFrac(RTF_LEVEL);

		int32_t baseitem;

		if (tiny)
		{
			V_SetClipRect((fx + 15) << FRACBITS, (fy + 14) << FRACBITS, 18 << FRACBITS, 18 << FRACBITS, V_HUDTRANS|fflags);
		}
		else
		{
			V_SetClipRect((fx + rinfo.crop.x) << FRACBITS, (fy + rinfo.crop.y) << FRACBITS, rinfo.spacing, rinfo.spacing, V_HUDTRANS|fflags);
		}
		for (int rouletteshift = -1; rouletteshift <= 2; rouletteshift++)
		{
			shiftprog = (FRACUNIT * -rouletteshift) + (FixedDiv((stplyr->itemroulette<<FRACBITS)|timefracs, animlength<<FRACBITS) % FRACUNIT);
			alpha = (FixedMul((abs(shiftprog) * 10), FixedDiv(cvarhudtrans << FRACBITS, 10 << FRACBITS)) >> FRACBITS)+(10-cvarhudtrans);
			rfy = (fy<<FRACBITS) + FixedMul(shift*shiftprog, info.hudScale);
			if (0 > alpha) alpha = 0;
			if (alpha >= 10) alpha = 10;
			fancyflags = (alpha<<V_ALPHASHIFT)|fflags;

			baseitem = K_GetRollingRouletteItemOffset(stplyr, rouletteshift, animlength);
			localpatch = K_GetCachedItemPatch(baseitem, tiny, 0);

			V_DrawFixedPatch(fx<<FRACBITS, rfy, info.hudScale, fancyflags, localpatch, colmap);
		}
		V_ClearClipRect();

		// draw minecraft-style cooldown
		K_DrawItemCooldown(fx, fy, V_HUDTRANSHALF|fflags, stplyr->itemusecooldown, stplyr->itemusecooldownmax, info.hudScale);
	}
	else
	{
		V_DrawFixedPatch(fx<<FRACBITS, fy<<FRACBITS, info.hudScale, localpatchflags, localpatch, colmap);
		
		// draw minecraft-style cooldown
		K_DrawItemCooldown(fx, fy, V_HUDTRANSHALF|fflags, stplyr->itemusecooldown, stplyr->itemusecooldownmax, info.hudScale);

		if (isalt)
			V_DrawFixedPatch(fx<<FRACBITS, fy<<FRACBITS, info.hudScale, localpatchflags, K_getItemAltPatch(tiny, false), colmap);
	}

	// Extensible meter, currently used by Grow, Rocket Sneakers, Flame Shield, and the S-Monitor
	// ...aren't you forgetting something?
	if (itembar != -1)
		K_DrawItemBar(fx, fy, info.hudScale, fflags|V_HUDTRANS, itembar, (uint8_t []){0, 8, 12});

	if (flamebar != -1)
		K_DrawItemBar(fx, fy - FixedMul(8, info.hudScale), info.hudScale, fflags|V_HUDTRANS, flamebar, (uint8_t []){51, 36, 55});

	// Quick Eggman numbers
	if (stplyr->eggmanexplode > 1)
	{
		V_DrawSciencePatch((fx<<FRACBITS) + (17 * info.hudScale),
				   (fy<<FRACBITS) + (13 * info.hudScale) - (tiny ? FRACUNIT : 0),
				   V_HUDTRANS|fflags, kp_eggnum[min(3, G_TicsToSeconds(stplyr->eggmanexplode))],
				   info.hudScale);
	}

	// draw the item reserve
	if (stplyr->itemreservetype && stplyr->itemreserveamount > 0)
	{
		localpatch = K_GetCachedItemPatch(stplyr->itemreservetype, true, stplyr->itemreserveamount);

		if (localpatch == NULL)
			localpatch = kp_nodraw; // diagnose underflows
		
		if (K_GetItemFlags(stplyr->itemreservetype) & KIF_COLPATCH2PLAYER)
		{
			colmap = R_GetTranslationColormap(TC_DEFAULT, stplyr->skincolor, GTC_CACHE);
		}
		else
		{
			colmap = NULL;
		}
		
		fx = (fx<<FRACBITS) + ((tiny ? 16 : 34) * (flipamount ? -info.hudScale : info.hudScale));
		fy = (fy<<FRACBITS) + ((tiny ? -10 : -7) * info.hudScale);
		if (!tiny)
		{
			dark = K_GetItemFlags(stplyr->itemreservetype) & KIF_DARKBG;
			localbg = K_getItemBoxPatch(true, dark);
			
			V_DrawFixedPatch(fx, fy, info.hudScale, boxpatchflags, localbg, colormap);
		}
		
		V_DrawFixedPatch(fx, fy, info.hudScale, localpatchflags, localpatch, colmap);

		// numbers
		if (stplyr->itemreserveamount >= K_GetItemNumberDisplayMin(stplyr->itemreservetype, true))
		{
			V_DrawPingNumScaled(fx + (26*(flipamount ? -info.hudScale : info.hudScale)), fy + (26*info.hudScale), info.hudScale, V_ALLOWLOWERCASE|V_HUDTRANS|fflags, stplyr->itemreserveamount, colmap);
		}
	}
}

#define BASEPADDING (4)

static void k_drawKartRaceRank(player_t* p, int32_t x, int32_t y, uint32_t flags, uint8_t* colormap)
{
	if (dedicated)
		return;  // Not for dedis, stupid!

	if (cannotrank)
		return;  // No point in displaying something irrelevant!

	if (!p)
		return;  // ...of course.

	const dboolean drawcompact = (r_splitscreen >= 1) ? true : false;

	patch_t** rankpicarray = (drawcompact) ? &kp_ranktier4p[0] : &kp_ranktier[0];

	patch_t* rankpic = rankpicarray[p->ranktier];

	if (!rankpic)
	{
		// Yeah.
		return;
	}

	const dboolean is_negative = (p->rankscore < 0);
	int32_t num = abs(p->rankscore);
	int32_t xx = 0;

	if (drawcompact)
	{
		// Draw the rank patch first
		V_DrawMappedPatch(x, y, flags, rankpic, colormap);

		uint16_t align = BASEPADDING;
		uint16_t numdiv = num;

		// Padding = 10 ^ (BASEPADDING - 1)
		const double maxpad_d =
		    min((double)(INT32_MAX), pow(10.0, (double)(max(0, BASEPADDING))));
		int32_t maxpadding = (int32_t)(maxpad_d);

		// If we're over the base padding, increase our padding
		while (numdiv > maxpadding)
		{
			align++;
			numdiv /= 10;
		}

		if (is_negative)
		{
			// If this is a negative number, draw the minus right before the actual
			// numbers
			V_DrawScaledPatch(x + 16, y + 2, flags, kp_ringdebtminussmall);
			xx = 4;

			// Reduce the padding a little so we're not clipping into the other player's
			// screen
			align--;
		}

		const int32_t pingwidth = LSBF_SHORT(fontv[PINGNUM_FONT].font[0]->width);

		// Pingnums are only ever right-aligned, so we have to force it to be left-aligned
		xx += (pingwidth - 1) * align;

		V_DrawPingNumPadded(x + 16 + xx, y, flags, num, align, NULL);

		return;
	}

	const int16_t basewidth = kp_facenum[0]->width;
	const int16_t rnknumwidth = basewidth - 1;
	int16_t rnkstringwidth = 0;

	// First loop: get the "width" of the numbers
	do
	{
		rnkstringwidth += (rnknumwidth - 1);
		num /= 10;
	} while (num);

	V_DrawMappedPatch(x + 7, y, flags, rankpic, colormap);

	// Add the minus if we're drawing a negative number
	if (is_negative)
		rnkstringwidth += 5;

	const int32_t centering = 15 + (rnkstringwidth / 2) - basewidth;

	// If this is a negative number, draw the minus right before the actual numbers
	if (is_negative)
		V_DrawScaledPatch(
		    x + 15 - ((rnkstringwidth / 2) + 5), y + 16, flags, kp_ringdebtminus);

	num = abs(p->rankscore);

	// Second loop: actually fucking draw the numbers
	do
	{
		V_DrawScaledPatch(x + centering - xx, y + 16, flags, kp_facenum[num % 10]);
		xx += (rnknumwidth - 1);
		num /= 10;
	} while (num);
}

#undef BASEPADDING

void K_drawKartTimestamp(tic_t drawtime, int32_t TX, int32_t TY, mapnum_t emblemmap, uint8_t mode)
{
	// TIME_X = BASEVIDWIDTH-124;	// 196
	// TIME_Y = 6;					//   6

	tic_t worktime;
	fixed_t jitter = 0;
	dboolean overtime = false, fastjitter = false, countdown = false;
	uint8_t *textcolor = NULL;

	int32_t splitflags = 0;
	if (!mode)
	{
		splitflags = V_HUDTRANS|V_SNAPTOTOP|V_SNAPTORIGHT|V_SPLITSCREEN;

		if (timelimitintics > 0)
		{
			if (drawtime >= timelimitintics)
			{
				overtime = true;
				jitter = 1;
			}
			else
			{
				drawtime = timelimitintics - drawtime;
				countdown = true;
				if (secretextratime)
					;
				else if (extratimeintics)
				{
					jitter = 2;
					fastjitter = true;
				}
				else if (drawtime <= 5*TICRATE)
				{
					jitter = ((drawtime <= 3*TICRATE) && (((drawtime-1) % TICRATE) >= TICRATE-2))
						? 3 : 1;
				}
			}
		}
	}

	if (!K_UseColorHud())
		V_DrawScaledPatch(TX, TY, splitflags, ((mode == 2) ? kp_lapstickerwide[0] : kp_timestickerwide[0]));
	else //Colourized hud
	{
		uint8_t *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
		V_DrawMappedPatch(TX, TY, splitflags, ((mode == 2) ? kp_lapstickerwide[1] : kp_timestickerwide[1]), colormap);
	}

	TX += 33;

	worktime = drawtime/(60*TICRATE);

	if (drawtime != UINT32_MAX && worktime >= 100)
	{
		jitter = 1;
		fastjitter = true;
		worktime = 99;
		drawtime = (100*(60*TICRATE))-1;
	}

	if (fastjitter)
	{
		jitter *= R_GetTimeFrac(RTF_LEVEL) * (leveltime & 1 ? 1 : -1);
	}
	else
	{
		if (drawtime & 2) jitter = -jitter;
		jitter *= !!(drawtime & 1) != countdown ? FRACUNIT - R_GetTimeFrac(RTF_LEVEL) : R_GetTimeFrac(RTF_LEVEL);
	}

	if (mode && drawtime == UINT32_MAX)
		V_DrawKartString(TX, TY+3, splitflags, va("--'--\"--"));
	else if (overtime)
	{
		if (((drawtime*2)/TICRATE) % 2 == 0)
		{
			textcolor = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_YELLOW, GTC_CACHE);
		}
		else
		{
			textcolor = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_ORANGE, GTC_CACHE);
		}

		const char *overtimestr = "OVERTIME";
		for (uint8_t i = 0; i < strlen(overtimestr); i++)
		{
			V_DrawStringScaledEx(
				(TIME_X + 33 + 12*i + (i == 6 ? -4 : i == 7 ? -1 : 0)) << FRACBITS,
				((TIME_Y + 3) << FRACBITS) + (i & 1 ? jitter : -jitter),
				FRACUNIT,
				FRACUNIT,
				FRACUNIT,
				FRACUNIT,
				splitflags,
				textcolor,
				KART_FONT,
				va("%c", overtimestr[i])
			);
		}
	}
	else
	{
		// minutes time      00 __ __
		V_DrawKartStringAtFixed(TX<<FRACBITS,      ((TY+3)<<FRACBITS)+jitter, splitflags, va("%d", worktime/10));
		V_DrawKartStringAtFixed((TX+12)<<FRACBITS, ((TY+3)<<FRACBITS)-jitter, splitflags, va("%d", worktime%10));

		// apostrophe location     _'__ __
		V_DrawKartString(TX+24, TY+3, splitflags, va("'"));

		worktime = (drawtime/TICRATE % 60);

		// seconds time       _ 00 __
		V_DrawKartStringAtFixed((TX+36)<<FRACBITS, ((TY+3)<<FRACBITS)+jitter, splitflags, va("%d", worktime/10));
		V_DrawKartStringAtFixed((TX+48)<<FRACBITS, ((TY+3)<<FRACBITS)-jitter, splitflags, va("%d", worktime%10));

		// quotation mark location    _ __"__
		V_DrawKartString(TX+60, TY+3, splitflags, va("\""));

		worktime = G_TicsToCentiseconds(drawtime);

		// tics               _ __ 00
		V_DrawKartStringAtFixed((TX+72)<<FRACBITS, ((TY+3)<<FRACBITS)+jitter, splitflags, va("%d", worktime/10));
		V_DrawKartStringAtFixed((TX+84)<<FRACBITS, ((TY+3)<<FRACBITS)-jitter, splitflags, va("%d", worktime%10));
	}

	if (mode != 2 && (modeattacking || (mode == 1)) && G_EmblemsEnabled()) // emblem time!
	{
		int32_t workx = TX + 96, worky = TY+18;
		int8_t curemb = 0;
		patch_t *emblempic[3] = {NULL, NULL, NULL};
		uint8_t *emblemcol[3] = {NULL, NULL, NULL};

		emblem_t *emblem = M_GetLevelEmblems(emblemmap);
		while (emblem)
		{
			char targettext[9];

			switch (emblem->type)
			{
				case ET_TIME:
					{
						static dboolean canplaysound = true;
						tic_t timetoreach = emblem->var;

						if (emblem->collected)
						{
							emblempic[curemb] = W_CachePatchName(M_GetEmblemPatch(emblem, false), PU_CACHE);
							emblemcol[curemb] = R_GetTranslationColormap(TC_DEFAULT, M_GetEmblemColor(emblem), GTC_CACHE);
							if (++curemb == 3)
								break;
							goto bademblem;
						}

						snprintf(targettext, 9, "%i'%02i\"%02i",
							G_TicsToMinutes(timetoreach, false),
							G_TicsToSeconds(timetoreach),
							G_TicsToCentiseconds(timetoreach));

						if (!mode)
						{
							if (stplyr->realtime > timetoreach)
							{
								splitflags = (splitflags &~ V_HUDTRANS)|V_HUDTRANSHALF;
								if (canplaysound)
								{
									S_StartSound(NULL, sfx_s3k72); //sfx_s26d); -- you STOLE fizzy lifting drinks
									canplaysound = false;
								}
							}
							else if (!canplaysound)
								canplaysound = true;
						}

						targettext[8] = 0;
					}
					break;
				default:
					goto bademblem;
			}

			V_DrawRightAlignedString(workx, worky, splitflags|V_6WIDTHSPACE, targettext);
			workx -= 67;
			V_DrawSmallScaledPatch(workx + 4, worky, splitflags, W_CachePatchName("NEEDIT", PU_CACHE));

			break;

bademblem:
			emblem = M_GetLevelEmblems(NEXTMAP_INVALID);
		}

		if (!mode)
			splitflags = (splitflags &~ V_HUDTRANSHALF)|V_HUDTRANS;
		while (curemb--)
		{
			workx -= 12;
			V_DrawSmallMappedPatch(workx + 4, worky, splitflags, emblempic[curemb], emblemcol[curemb]);
		}
	}
}

#define POS_DELAY_TIME 10

static void K_DrawKartPositionNum(int32_t num)
{
	// POSI_X = BASEVIDWIDTH - 51;	// 269
	// POSI_Y = BASEVIDHEIGHT- 64;	// 136

	dboolean win = (stplyr->exiting && num == 1);
	//int32_t X = POSI_X;
	int32_t W = kp_positionnum[0][0]->width;
	fixed_t scale = FRACUNIT;
	patch_t *localpatch = kp_positionnum[0][0];
	int32_t fx = 0, fy = 0, fflags = 0;
	int32_t xoffs = (cv_showinput.value > 0) ? -48 : 0;
	int32_t addOrSub = 0;
	dboolean flipdraw = false;	// flip the order we draw it in for MORE splitscreen bs. fun.
	dboolean flipvdraw = false;	// used only for 2p splitscreen so overtaking doesn't make 1P's position fly off the screen.
	dboolean overtake = false;

	if ((mapheaderinfo[gamemap - 1]->levelflags & LF_SUBTRACTNUM) == LF_SUBTRACTNUM)
	{
		addOrSub = V_SUBTRACT;
	}

	if (stplyr->positiondelay || stplyr->exiting)
	{
		const uint8_t delay = (stplyr->exiting) ? POS_DELAY_TIME : stplyr->positiondelay;
		const fixed_t add = (scale * 3) >> ((r_splitscreen == 1) ? 1 : 2);
		scale = cv_smoothposition.value ? scale + min((add * (delay * delay)) / (POS_DELAY_TIME * POS_DELAY_TIME), add) : scale*2;
		overtake = true;	// this is used for splitscreen stuff in conjunction with flipdraw.
	}

	if (r_splitscreen || ((cv_showinput.value > 0) && !r_splitscreen))
		scale /= 2;

	W = FixedMul(W<<FRACBITS, scale)>>FRACBITS;

	// pain and suffering defined below
	if (!r_splitscreen)
	{
		fx = POSI_X + xoffs;
		fy = BASEVIDHEIGHT - 8;
		fflags = V_SNAPTOBOTTOM|V_SNAPTORIGHT|V_SPLITSCREEN;
	}
	else if (r_splitscreen == 1)	// for this splitscreen, we'll use case by case because it's a bit different.
	{
		fx = POSI_X;
		if (stplyrnum == 0)	// for player 1: display this at the top right, above the minimap.
		{
			fy = 30;
			fflags = V_SNAPTOTOP|V_SNAPTORIGHT|V_SPLITSCREEN;
			if (overtake)
				flipvdraw = true;	// make sure overtaking doesn't explode us
		}
		else	// if we're not p1, that means we're p2. display this at the bottom right, below the minimap.
		{
			fy = (BASEVIDHEIGHT/2) - 8;
			fflags = V_SNAPTOBOTTOM|V_SNAPTORIGHT|V_SPLITSCREEN;
		}
	}
	else
	{
		if (stplyrnum == 0 || stplyrnum == 2)	// If we are P1 or P3...
		{
			fx = POSI_X;
			fy = POSI_Y;
			fflags = V_SNAPTOLEFT|V_SNAPTOBOTTOM|V_SPLITSCREEN;
			flipdraw = true;
			if (num && num >= 10)
				fx += W;	// this seems dumb, but we need to do this in order for positions above 10 going off screen.
		}
		else // else, that means we're P2 or P4.
		{
			fx = POSI2_X;
			fy = POSI2_Y;
			fflags = V_SNAPTORIGHT|V_SNAPTOBOTTOM|V_SPLITSCREEN;
		}
	}

	// Special case for 0
	if (num <= 0)
	{
		V_DrawFixedPatch(fx<<FRACBITS, fy<<FRACBITS, scale, addOrSub|fflags, kp_positionnum[0][0], NULL);
		return;
	}

	// Draw the number
	while (num)
	{
		if (win) // 1st place winner? You get rainbows!!
		{
			localpatch = kp_winnernum[(leveltime % (NUMWINFRAMES*3)) / 3];
		}
		else if (stplyr->laps >= numlaps || stplyr->exiting) // Check for the final lap, or won
		{
			dboolean useRedNums = K_IsPlayerLosing(stplyr);

			if (addOrSub == V_SUBTRACT)
			{
				// Subtracting RED will look BLUE, and vice versa.
				useRedNums = !useRedNums;
			}

			// Alternate frame every three frames
			switch ((leveltime % 9) / 3)
			{
				case 0:
					if (useRedNums == true)
						localpatch = kp_positionnum[num % 10][4];
					else
						localpatch = kp_positionnum[num % 10][1];
					break;
				case 1:
					if (useRedNums == true)
						localpatch = kp_positionnum[num % 10][5];
					else
						localpatch = kp_positionnum[num % 10][2];
					break;
				case 2:
					if (useRedNums == true)
						localpatch = kp_positionnum[num % 10][6];
					else
						localpatch = kp_positionnum[num % 10][3];
					break;
				default:
					localpatch = kp_positionnum[num % 10][0];
					break;
			}
		}
		else
		{
			localpatch = kp_positionnum[num % 10][0];
		}

		V_DrawFixedPatch(
			(fx<<FRACBITS) + ((overtake && flipdraw) ? (localpatch->width)*scale/2 : 0),
			(fy<<FRACBITS) + ((overtake && flipvdraw) ? (localpatch->height)*scale/2 : 0),
			scale, addOrSub|V_HUDTRANSHALF|fflags, localpatch, NULL
		);
		// ^ if we overtake as p1 or p3 in splitscren, we shift it so that it doesn't go off screen.
		// ^ if we overtake as p1 in 2p splits, shift vertically so that this doesn't happen either.

		fx -= W;
		num /= 10;
	}
}

static uint32_t K_SMonitorHUDVisibility(uint16_t t)
{
	uint32_t alphalevel = st_translucency;

	alphalevel = min(10, FixedMul(alphalevel, K_SMonitorGradient(t)));

	return min(9, 10 - alphalevel)<<V_ALPHASHIFT;
}

static dboolean K_RankIconCanSpinout(void)
{
	return ((cv_spinoutroll.value) & 2);
}

static dboolean K_MinimapIconCanSpinout(void)
{
	 return ((cv_spinoutroll.value) & 1);
}

static dboolean K_drawKartPositionFaces(void)
{
	// FACE_X = 15;				//  15
	// FACE_Y = 72;				//  72

	int32_t Y = FACE_Y-9; // -9 to offset where it's being drawn if there are more than one
	int32_t i, j, ranklines, strank = -1;
	dboolean completed[MAXPLAYERS];
	int32_t rankplayer[MAXPLAYERS];
	int32_t bumperx, numplayersingame = 0;
	uint8_t *colormap;
	uint32_t smntrhudtrans;
	vector2_t offsets;

#ifdef ROTSPRITE
	angle_t rollangle = 0;
	int32_t rot = 0;
#endif

	offsets.x = offsets.y = 0;

	ranklines = 0;
	memset(completed, 0, sizeof (completed));
	memset(rankplayer, 0, sizeof (rankplayer));

	for (i = 0; i < MAXPLAYERS; i++)
	{
		rankplayer[i] = -1;

		if (!playeringame[i] || players[i].spectator || !players[i].mo)
			continue;

		numplayersingame++;
	}

	if (numplayersingame <= 1)
		return true;

	if (!LUA_HudEnabled(hud_minirankings))
		return false;	// Don't proceed but still return true for free play above if HUD is disabled.

	for (j = 0; j < numplayersingame; j++)
	{
		uint8_t lowestposition = MAXPLAYERS+1;
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (completed[i] || !playeringame[i] || players[i].spectator || !players[i].mo)
				continue;

			if (players[i].position >= lowestposition)
				continue;

			rankplayer[ranklines] = i;
			lowestposition = players[i].position;
		}

		i = rankplayer[ranklines];

		completed[i] = true;

		if (players+i == stplyr)
			strank = ranklines;

		//if (ranklines == 5)
			//break; // Only draw the top 5 players -- we do this a different way now...

		ranklines++;
	}

	if (ranklines < 5)
		Y += (9*ranklines);
	else
		Y += (9*5);

	ranklines--;
	i = ranklines;

	if (gametype == GT_BATTLE || strank <= 2) // too close to the top, or playing battle, or a spectator? would have had (strank == -1) called out, but already caught by (strank <= 2)
	{
		if (i > 4) // could be both...
			i = 4;
		ranklines = 0;
	}
	else if (strank+2 >= ranklines) // too close to the bottom?
	{
		ranklines -= 4;
		if (ranklines < 0)
			ranklines = 0;
	}
	else
	{
		i = strank+2;
		ranklines = strank-2;
	}

	for (; i >= ranklines; i--)
	{
		if (!playeringame[rankplayer[i]]) continue;
		if (players[rankplayer[i]].spectator) continue;
		if (!players[rankplayer[i]].mo) continue;

		bumperx = FACE_X+19;

		if (players[rankplayer[i]].mo->color)
		{
			colormap = R_GetTranslationColormap(players[rankplayer[i]].skin, players[rankplayer[i]].mo->color, GTC_CACHE);

			if (players[rankplayer[i]].mo->colorized)
				colormap = R_GetTranslationColormap(TC_RAINBOW, players[rankplayer[i]].mo->color, GTC_CACHE);
			else
				colormap = R_GetTranslationColormap(players[rankplayer[i]].skin, players[rankplayer[i]].mo->color, GTC_CACHE);

			dboolean hires = K_UseHighResPortraits();

			patch_t *facerank = faceprefix[players[rankplayer[i]].skin][hires ? FACE_WANTED : FACE_RANK];

			offsets.x = facerank->leftoffset;
			offsets.y = facerank->topoffset;

#ifdef ROTSPRITE
			if ((K_RankIconCanSpinout()) && (players[rankplayer[i]].spinoutrot))
			{
				// Rotate counterclockwise.
				rollangle = FixedAngle(players[rankplayer[i]].spinoutrot * -1);
				rot = R_GetRollAngle(rollangle);

				if (rot)
				{
					facerank = Patch_GetRotated(facerank, rot, false);
				}
			}
#endif

			V_DrawFixedPatch((FACE_X + offsets.x)<<FRACBITS, (Y + offsets.y)<<FRACBITS, hires ? FRACUNIT / 2 : FRACUNIT, V_HUDTRANS|V_SNAPTOLEFT, facerank, colormap);

			if (players[rankplayer[i]].smonitortimer)
			{
				colormap = R_GetTranslationColormap(TC_BLINK, K_SMonitorColor(leveltime / 2), GTC_CACHE);
				smntrhudtrans = K_SMonitorHUDVisibility(players[rankplayer[i]].smonitortimer);

				V_DrawFixedPatch((FACE_X + offsets.x)<<FRACBITS, (Y + offsets.y)<<FRACBITS, hires ? FRACUNIT / 2 : FRACUNIT, smntrhudtrans|V_SNAPTOLEFT|V_ADD, facerank, colormap);
			}

			if (LUA_HudEnabled(hud_battlebumpers))
			{
				if ((gametypes[gametype]->rules & GTR_BUMPERS) && players[rankplayer[i]].bumper > 0)
				{
					V_DrawMappedPatch(bumperx-2, Y, V_HUDTRANS|V_SNAPTOLEFT, kp_tinybumper[0], colormap);
					for (j = 1; j < players[rankplayer[i]].bumper; j++)
					{
						bumperx += 5;
						V_DrawMappedPatch(bumperx, Y, V_HUDTRANS|V_SNAPTOLEFT, kp_tinybumper[1], colormap);
					}
				}
			}
		}

		if (i == strank)
		{
			patch_t *highlight = kp_facehighlight[(leveltime / 4) % 8];

			int32_t left, top;

			left = highlight->leftoffset;
			top = highlight->topoffset;

#ifdef ROTSPRITE
			if ((K_RankIconCanSpinout()) && (players[rankplayer[i]].spinoutrot))
			{
				// Rotate counterclockwise.
				rollangle = FixedAngle(players[rankplayer[i]].spinoutrot * -1);
				rot = R_GetRollAngle(rollangle);

				if (rot)
				{
					highlight = Patch_GetRotated(highlight, rot, false);
				}
			}
#endif

			V_DrawScaledPatch(FACE_X+left, Y+top, V_HUDTRANS|V_SNAPTOLEFT, highlight);
		}

		if ((gametypes[gametype]->rules & GTR_BUMPERS) && players[rankplayer[i]].bumper <= 0)
			V_DrawScaledPatch(FACE_X-4, Y-3, V_HUDTRANS|V_SNAPTOLEFT, kp_ranknobumpers);
		else
		{
			int32_t pos = players[rankplayer[i]].position;

			// Draws the little number over the face
			if (pos < 0 || pos > 16)
				V_DrawPingNum(FACE_X+2, Y+10, V_HUDTRANS|V_SNAPTOLEFT, pos, NULL);
			else
				V_DrawScaledPatch(FACE_X-5, Y+10, V_HUDTRANS|V_SNAPTOLEFT, kp_facenum[pos]);
		}

		Y -= 18;
	}

	return false;
}

static void K_drawBossHealthBar(void)
{
	uint8_t i = 0, barstatus = 1, randlen = 0, darken = 0;
	const int32_t startx = BASEVIDWIDTH - 23;
	int32_t starty = BASEVIDHEIGHT - 25;
	int32_t rolrand = 0, randtemp = 0;
	dboolean randsign = false;

	if (bossinfo.barlen <= 1)
		return;

	// Entire bar juddering!
	if (lt_exitticker < (TICRATE/2))
		;
	else if (bossinfo.visualbarimpact)
	{
		int32_t mag = min((bossinfo.visualbarimpact/4) + 1, 8);
		if (bossinfo.visualbarimpact & 1)
			starty -= mag;
		else
			starty += mag;
	}

	if ((lt_ticker >= lt_endtime) && bossinfo.enemyname)
	{
		if (lt_exitticker == 0)
		{
			rolrand = 5;
		}
		else if (lt_exitticker == 1)
		{
			rolrand = 7;
		}
		else
		{
			rolrand = 10;
		}
		V_DrawRightAlignedThinString(startx, starty-rolrand, V_HUDTRANS|V_SNAPTOBOTTOM|V_SNAPTORIGHT|V_6WIDTHSPACE, bossinfo.enemyname);
		rolrand = 0;
	}

	// Used for colour and randomisation.
	if (bossinfo.healthbar <= (bossinfo.visualdiv/FRACUNIT))
	{
		barstatus = 3;
	}
	else if (bossinfo.healthbar <= bossinfo.healthbarpinch)
	{
		barstatus = 2;
	}

	randtemp = bossinfo.visualbar-(bossinfo.visualdiv/(2*FRACUNIT));
	if (randtemp > 0)
		randlen = M_RandomKey(randtemp)+1;
	randsign = M_RandomChance(FRACUNIT/2);

	// Right wing.
	V_DrawScaledPatch(startx-1, starty, V_HUDTRANS|V_SNAPTOBOTTOM|V_SNAPTORIGHT|V_FLIP, kp_bossbar[0]);

	// Draw the bar itself...
	while (i < bossinfo.barlen)
	{
		V_DrawScaledPatch(startx-i, starty, V_HUDTRANS|V_SNAPTOBOTTOM|V_SNAPTORIGHT, kp_bossbar[1]);
		if (i < bossinfo.visualbar)
		{
			randlen--;
			if (!randlen)
			{
				randtemp = bossinfo.visualbar-(bossinfo.visualdiv/(2*FRACUNIT));
				if (randtemp > 0)
					randlen = M_RandomKey(randtemp)+1;
				if (barstatus > 1)
				{
					rolrand = M_RandomKey(barstatus)+1;
				}
				else
				{
					rolrand = 1;
				}
				if (randsign)
				{
					rolrand = -rolrand;
				}
				randsign = !randsign;
			}
			else
			{
				rolrand = 0;
			}
			if (lt_exitticker < (TICRATE/2))
				;
			else if ((bossinfo.visualbar - i) < (int32_t)(bossinfo.visualbarimpact/8))
			{
				if (bossinfo.visualbarimpact & 1)
					rolrand += (bossinfo.visualbar - i);
				else
					rolrand -= (bossinfo.visualbar - i);
			}
			if (bossinfo.visualdiv)
			{
				fixed_t work = 0;
				if ((i+1) == bossinfo.visualbar)
					darken = 1;
				else
				{
					darken = 0;
					// a hybrid fixed-int modulo...
					while ((work/FRACUNIT) < bossinfo.visualbar)
					{
						if (work/FRACUNIT != i)
						{
							work += bossinfo.visualdiv;
							continue;
						}
						darken = 1;
						break;
					}
				}
			}
			V_DrawScaledPatch(startx-i, starty+rolrand, V_HUDTRANS|V_SNAPTOBOTTOM|V_SNAPTORIGHT, kp_bossbar[(2*barstatus)+darken]);
		}
		i++;
	}

	// Left wing.
	V_DrawScaledPatch(startx-i, starty, V_HUDTRANS|V_SNAPTOBOTTOM|V_SNAPTORIGHT, kp_bossbar[0]);
}

//
// K_DrawNeoTabRankings -- A new way to view ingame info!
//	returns starting x of right offset for drawing your own info in there!
//
int32_t K_DrawNeoTabRankings(int32_t x, int32_t y, playersort_t *tab, int32_t scorelines, int32_t whiteplayer, int32_t hilicol, dboolean split)
{
	int32_t i, rightoffset = split ? 160 - 6: BASEVIDWIDTH - 6;
	const uint8_t *colormap = NULL;
	uint16_t hightlightcolor = 0;
	int32_t dupadjust = (vid.scaledwidth), duptweak = (dupadjust - BASEVIDWIDTH)/2;
	int32_t y2, x2;
	// int32_t basey = y, basex = x;

	(void)hilicol;

	scorelines--;

	x += 4;
	y += 9*scorelines;

	V_DrawFill(1-duptweak, 26, dupadjust-2, 1, 0); // Draw a horizontal line because it looks nice!

	if (split)
	{
		V_DrawFill(x + rightoffset + 6, 26, 1, 162, 0); // Draw a vertical line because it looks nice!
	}

	for (i = scorelines; i >= 0; i--)
	{
		int32_t pos = CLAMP(players[tab[i].num].position, 0 , MAXPLAYERS);
		char playername[MAXPLAYERNAME+1];
		char clanname[MAXCLANTAG];

		if (players[tab[i].num].spectator || !players[tab[i].num].mo)
			continue; //ignore them.

		if (netgame) // don't draw ping offline
		{
			if (players[tab[i].num].bot)
			{
				V_DrawThinString(x - 18 + 25, y, 0, "CPU");
			}
			else if (tab[i].num != serverplayer || (server_lagless == 0 || server_lagless == -1))
			{
				int32_t xoff = cv_pingmeasurement.value == 1 ? 33 : 26;

				HU_drawPing(x - 18 + xoff , y-10, playerpingtable[tab[i].num], playerdelaytable[tab[i].num], playerpacketlosstable[tab[i].num], 0, false);
			}
			else if (tab[i].num == serverplayer)
			{
				V_DrawThinString(x - 18 + 25, y, 0, "SRV");
			}
		}

		STRBUFCPY(playername, tab[i].name);
		strncpy(clanname, tab[i].clan, MAXCLANTAG);

		if (players[tab[i].num].mo->color)
		{
			colormap = R_GetTranslationColormap(players[tab[i].num].skin, players[tab[i].num].mo->color, GTC_CACHE);
			if (players[tab[i].num].mo->colorized)
				colormap = R_GetTranslationColormap(TC_RAINBOW, players[tab[i].num].mo->color, GTC_CACHE);
			else
				colormap = R_GetTranslationColormap(players[tab[i].num].skin, players[tab[i].num].mo->color, GTC_CACHE);

			hightlightcolor = skincolors[players[tab[i].num].mo->color].chatcolor;
		}

		if (pos < 0 || pos > 16)
			V_DrawPingNum(x+2, y+1, 0, pos, NULL);
		else
			V_DrawScaledPatch(x-5, y+1, 0, kp_facenum[pos]);

		x2 = netgame ? x + (BASEVIDWIDTH/20) : x;
		y2 = y;

		if (clanname[0] != 0)
		{
			uint8_t length = V_ThinStringWidth(clanname, V_ALLOWLOWERCASE|V_6WIDTHSPACE);
			V_DrawThinString(x2 + 20, y2, V_SKYMAP|V_ALLOWLOWERCASE|V_6WIDTHSPACE, clanname);
			V_DrawThinString(x2 + 20 + length + 2, y2, ((tab[i].num == whiteplayer) ? hightlightcolor : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, playername);

		}
		else
			V_DrawThinString(x2 + 20, y2, ((tab[i].num == whiteplayer) ? hightlightcolor : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, playername);

		patch_t *facerank = faceprefix[players[tab[i].num].skin][FACE_RANK];
		fixed_t scale = FRACUNIT/2;

		V_DrawFixedPatch(
			((x2+11)*FRACUNIT) + ((facerank->leftoffset) * scale),
			((y+1)*FRACUNIT) + ((facerank->topoffset) * scale),
			scale,
			0,
			facerank,
			colormap
		);

		/*if ((gametypes[gametype]->rules & GTR_BUMPERS) && players[tab[i].num].bumper > 0) -- not enough space for this
		{
			int32_t bumperx = x+19;
			V_DrawMappedPatch(bumperx-2, y-4, 0, kp_tinybumper[0], colormap);
			for (j = 1; j < players[tab[i].num].bumper; j++)
			{
				bumperx += 5;
				V_DrawMappedPatch(bumperx, y-4, 0, kp_tinybumper[1], colormap);
			}
		}*/

		patch_t *highlight = kp_facehighlight[(leveltime / 4) % 8];

		if (tab[i].num == whiteplayer)
			V_DrawFixedPatch(
				((x2+11)*FRACUNIT)+(highlight->leftoffset * scale),
				((y+1)*FRACUNIT)+(highlight->topoffset * scale),
				scale,
				0,
				highlight,
				NULL
			);

		if ((gametypes[gametype]->rules & GTR_BUMPERS) && players[tab[i].num].bumper <= 0)
			V_DrawScaledPatch(x2-4, y-7, 0, kp_ranknobumpers);

		if (tab[i].string[0] != '\0')
			V_DrawRightAlignedThinString(x+rightoffset+2, y, V_6WIDTHSPACE, tab[i].string);

		y -= 9;
	}
	return x+rightoffset;
}

servermods_t customservermods[MAXSERVERMODS];
uint8_t numcustomservermods = 0;

// Adds a new mod to the scoreboard display.
void K_AddNewScoreboardMod(const char *name, const consvar_t *cvar, int8_t active)
{
	uint32_t hashcompare = HASH32(name, MAXSERVERMODNAME);
	uint8_t i;

	for (i = 0; i < MAXSERVERMODS; i++)
	{
		if (customservermods[i].hash == hashcompare)
		{
			CONS_Alert(CONS_WARNING, "Scoreboard mod '%s' has already been added to the scoreboard.\n", name);
			return;
		}
	}

	if (numcustomservermods+1 == MAXSERVERMODS)
	{
		CONS_Alert(CONS_ERROR, "Maximum Amount of scoreboard mods has been reached.\n");
		return;
	}

	strncpy(customservermods[numcustomservermods].modname, name, MAXSERVERMODNAME);
	customservermods[numcustomservermods].hash = hashcompare;
	customservermods[numcustomservermods].cvar = cvar;
	customservermods[numcustomservermods].active = CLAMP(active, SCOREBOARDMOD_NOTUSED, SCOREBOARDMOD_ACTIVE);
	customservermods[numcustomservermods].valid = true;
	numcustomservermods++;
}

// Change the status of static scoreboard displays.
void K_SetScoreboardModStatus(const char *name, int8_t active)
{
	uint32_t hashcompare = HASH32(name, MAXSERVERMODNAME);
	uint8_t i;

	for (i = 0; i < MAXSERVERMODS; i++)
	{
		if (customservermods[i].hash == hashcompare)
		{
			customservermods[i].active = CLAMP(active, SCOREBOARDMOD_NOTUSED, SCOREBOARDMOD_ACTIVE);
			return;
		}
	}

	CONS_Alert(CONS_WARNING, "Server mod '%s' does not exist so status cannot be changed.\n", name);
}

static void K_DrawServerMods(int32_t x, int32_t y)
{
	uint8_t i, j;
	int32_t xoff = 0, yoff = 10;
	uint8_t numdrawn = 0;

	servermods_t basemods[] =
	{
		{"Restat", 0, &cv_allowrestat, -1, true},
		{"Item Litter", 0, NULL, K_ItemLitterActive() > 0, true},
		{"Rings", 0, NULL, K_RingsActive() > 0, true},
		{"4-Tier Drift", 0, NULL, K_PurpleDriftActive() > 0, true},
		{"Slipdash", 0, NULL, K_SlipdashActive() > 0, true},
		{"Stacking", 0, NULL, K_StackingActive() > 0, true},
		{"Chaining", 0, NULL, K_ChainingActive() > 0, true},
		{"Chain Offroad", 0, &cv_kartchainingoffroad, -1, true},
		{"Slope Boost", 0, NULL, K_PurpleDriftActive() > 0, true},
		{"Drafting", 0, NULL, K_DraftingActive() > 0, true},
		{"Light AirDrop", 0, NULL, K_AirDropActive() == AIRDROP_LIGHT, true},
		{"Heavy AirDrop", 0, NULL, K_AirDropActive() == AIRDROP_HEAVY, true},
		{"Fus. AirDrop", 0, NULL, K_AirDropActive() == AIRDROP_FUSION, true},
		{"Burst Thrust", 0, NULL, K_AirThrustActive() == AIRTHRUST_BURST, true},
		{"Flash Thrust", 0, NULL, K_AirThrustActive() == AIRTHRUST_FLASH, true},
		{"Recovery Dash", 0, NULL, K_RecoveryDashActive() > 0, true},
		{"Bump Spark", 0, &cv_kartbumpspark, -1, true},
		{"Bump Drift", 0, NULL, K_GetBumpSpark() > 0, true},
		{"Bump Spark", 0, NULL, K_GetBumpSpark() > BUMPSPARK_NOCHARGE, true},
		{"Bump Spring", 0, &cv_kartbumpspring, -1, true},
		{"Keep Stuff", 0, NULL, K_KeepStuffActive() > 0, true},
		{"No BananaDrag", 0, NULL, K_TrailSlowActive() < 1, true},
		{"No Air Bumps", 0, NULL, K_GetPlayerBump() == PBUMP_GNDONLY, true},
		{"No PvP Bumps", 0, NULL, K_GetPlayerBump() == PBUMP_NONE, true},
		//TODO: separate drawer that enumerates item changes?
	};

	for (j = 0; j < 2; j++)
	{
		uint8_t modcount = j == 0 ? sizeof(basemods)/sizeof(*basemods) : numcustomservermods;
		servermods_t *modslist = j == 0 ? basemods : customservermods;

		// Draw the the modlist.
		for (i = 0; i < modcount; i++)
		{
			dboolean drawdis = false;

			if (modslist[i].valid)
			{
				if (modslist[i].cvar && modslist[i].cvar->value)
				{
					drawdis = true;
				}
				else if (modslist[i].active == SCOREBOARDMOD_ACTIVE)
				{
					drawdis = true;
				}

				if (drawdis && modslist[i].modname[0] != '\0')
				{
					V_DrawSmallString(x+xoff, y+yoff, V_6WIDTHSPACE|V_ALLOWLOWERCASE, modslist[i].modname);
					numdrawn++;

					if ((numdrawn % 2) == 0)
					{
						xoff -= 50;
						yoff += 5;
					}
					else if ((numdrawn % 1) == 0)
					{
						xoff += 50;
					}
				}
			}
		}
	}

	if (numdrawn > 0)
		V_DrawThinString(x, y, V_6WIDTHSPACE|V_ALLOWLOWERCASE|V_GRAYMAP, "Gameplay / Balance Changes:");

}

void K_DrawServerDescrption(int32_t x, int32_t y)
{
	int32_t i, newlinecount = 0;
	if (connectedservername[0] != '\0')
		V_DrawThinString(x, y, V_6WIDTHSPACE|V_ALLOWLOWERCASE, connectedservername);

	V_DrawSmallString(x, y+10, V_6WIDTHSPACE|V_ALLOWLOWERCASE|V_GRAYMAP, va("Contact: %s", (connectedservercontact[0] != '\0') ? connectedservercontact : ""));

	if (connectedserverdescription[0] != '\0')
	{
		V_DrawSmallString(x, y+20, V_6WIDTHSPACE|V_ALLOWLOWERCASE, connectedserverdescription);

		for (i = 0; connectedserverdescription[i]; i++)
			newlinecount += (connectedserverdescription[i] == '\n');
	}

	K_DrawServerMods(x, y + 25 + newlinecount*6);
}

// The old school one....
void K_DrawTabRankings(int32_t x, int32_t y, playersort_t *tab, int32_t scorelines, int32_t whiteplayer, int32_t hilicol)
{
	int32_t i, rightoffset = 240;
	uint8_t *colormap = NULL;
	uint16_t hightlightcolor = 0;
	int32_t dupadjust = (vid.scaledwidth), duptweak = (dupadjust - BASEVIDWIDTH)/2;
	int basey = y, basex = x, y2;
	(void)hilicol;

	V_DrawFill(1-duptweak, 26, dupadjust-2, 1, 0); // Draw a horizontal line because it looks nice!

	scorelines--;
	if (scorelines >= 8)
	{
		V_DrawFill(160, 26, 1, 147, 0); // Draw a vertical line to separate the two sides.
		V_DrawFill(1-duptweak, 173, dupadjust-2, 1, 0); // And a horizontal line near the bottom.
		rightoffset = (BASEVIDWIDTH/2) - 4 - x;
		x = (BASEVIDWIDTH/2) + 4;
		y += 18*(scorelines-8);
	}
	else
	{
		y += 18*scorelines;
	}

	for (i = scorelines; i >= 0; i--)
	{
		char playername[MAXPLAYERNAME+1];

		if (players[tab[i].num].spectator || !players[tab[i].num].mo)
			continue; //ignore them.

		if (netgame) // don't draw ping offline
		{
			if (players[tab[i].num].bot)
			{
				V_DrawString(x + ((i < 8) ? -25 : rightoffset + 3), y-2, V_SNAPTOLEFT, "CPU");
			}
			else if (tab[i].num != serverplayer || (server_lagless == 0 || server_lagless == -1))
			{
				HU_drawPing(x + ((i < 8) ? -17 : rightoffset + 11), y-4, playerpingtable[tab[i].num], playerdelaytable[tab[i].num], playerpacketlosstable[tab[i].num], 0, true);
			}
			else if (tab[i].num == serverplayer)
			{
				V_DrawString(x + ((i < 8) ? -25 : rightoffset + 3), y-2, V_SNAPTOLEFT, "SRV");
			}
		}

		STRBUFCPY(playername, tab[i].name);

		y2 = y;

		if (players[tab[i].num].mo->color)
		{
			colormap = R_GetTranslationColormap(players[tab[i].num].skin, players[tab[i].num].mo->color, GTC_CACHE);
			if (players[tab[i].num].mo->colorized)
				colormap = R_GetTranslationColormap(TC_RAINBOW, players[tab[i].num].mo->color, GTC_CACHE);
			else
				colormap = R_GetTranslationColormap(players[tab[i].num].skin, players[tab[i].num].mo->color, GTC_CACHE);

			hightlightcolor = skincolors[players[tab[i].num].mo->color].chatcolor;
		}

		if (scorelines >= 8)
			V_DrawThinString(x + 20, y2, ((tab[i].num == whiteplayer) ? hightlightcolor : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, playername);
		else
			V_DrawString(x + 20, y2, ((tab[i].num == whiteplayer) ? hightlightcolor : 0)|V_ALLOWLOWERCASE, playername);

		dboolean hires = K_UseHighResPortraits();
		patch_t *facerank = faceprefix[players[tab[i].num].skin][hires ? FACE_WANTED : FACE_RANK];
		int16_t leftoffset = facerank->leftoffset;
		int16_t topoffset = facerank->topoffset;

		V_DrawFixedPatch((x+leftoffset)<<FRACBITS, (y-4+topoffset)<<FRACBITS, hires ? FRACUNIT/2 : FRACUNIT, 0, facerank, colormap);

		/*if ((gametypes[gametype]->rules & GTR_BUMPERS) && players[tab[i].num].bumper > 0) -- not enough space for this
		{
			int32_t bumperx = x+19;
			V_DrawMappedPatch(bumperx-2, y-4, 0, kp_tinybumper[0], colormap);
			for (j = 1; j < players[tab[i].num].bumper; j++)
			{
				bumperx += 5;
				V_DrawMappedPatch(bumperx, y-4, 0, kp_tinybumper[1], colormap);
			}
		}*/

		if (tab[i].num == whiteplayer)
		{
			patch_t *highlight = kp_facehighlight[(leveltime / 4) % 8];
			V_DrawScaledPatch(x+highlight->leftoffset, y-4+highlight->topoffset, 0, highlight);
		}

		if ((gametypes[gametype]->rules & GTR_BUMPERS) && players[tab[i].num].bumper <= 0)
			V_DrawScaledPatch(x-4, y-7, 0, kp_ranknobumpers);
		else
		{
			int32_t pos = players[tab[i].num].position;
			if (pos < 0 || pos > MAXPLAYERS)
				pos = 0;
			// Draws the little number over the face
			V_DrawScaledPatch(x-5, y+6, 0, kp_facenum[pos]);
		}

		if (tab[i].string[0] != '\0')
			V_DrawRightAlignedThinString(x+rightoffset, y-1, V_6WIDTHSPACE, tab[i].string);

		y -= 18;
		if (i == 8)
		{
			y = basey + 7*18;
			x = basex;
		}
	}
}

static void K_drawKartLaps(void)
{
	int32_t fx = 0, fy = 0, splitflags = 0;	// stuff for 3p / 4p splitscreen.

	drawinfo_t info;
	K_getLapsDrawinfo(&info);
	fx = info.x;
	fy = info.y;
	splitflags = info.flags;

	if (r_splitscreen > 1)
	{
		// Laps
		V_DrawScaledPatch(fx, fy, V_HUDTRANS|splitflags, kp_splitlapflag);
		V_DrawScaledPatch(fx+22, fy, V_HUDTRANS|splitflags, frameslash);

		if (numlaps >= 10)
		{
			uint8_t ln[2];
			ln[0] = ((stplyr->laps / 10) % 10);
			ln[1] = (stplyr->laps % 10);

			V_DrawScaledPatch(fx+13, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[0]]);
			V_DrawScaledPatch(fx+17, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[1]]);

			ln[0] = ((numlaps / 10) % 10);
			ln[1] = (numlaps % 10);

			V_DrawScaledPatch(fx+27, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[0]]);
			V_DrawScaledPatch(fx+31, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[1]]);
		}
		else
		{
			V_DrawScaledPatch(fx+13, fy, V_HUDTRANS|splitflags, kp_facenum[(stplyr->laps) % 10]);
			V_DrawScaledPatch(fx+27, fy, V_HUDTRANS|splitflags, kp_facenum[(numlaps) % 10]);
		}
	}
	else
	{
		if (!K_UseColorHud())
		{
			if (K_BigLapSticker())
				V_DrawScaledPatch(fx, fy, V_HUDTRANS|splitflags, ((stplyr->laps > 9) ? kp_lapstickerbig2[0] : kp_lapstickerbig[0]));
			else
				V_DrawScaledPatch(fx, fy, V_HUDTRANS|splitflags, kp_lapsticker[0]);
		}
		else
		{
			uint8_t *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
			if (K_BigLapSticker())
				V_DrawMappedPatch(fx, fy, V_HUDTRANS|splitflags, ((stplyr->laps > 9) ? kp_lapstickerbig2[1] : kp_lapstickerbig[1]), colormap);
			else
				V_DrawMappedPatch(fx, fy, V_HUDTRANS|splitflags, kp_lapsticker[1], colormap);
		}

		if (stplyr->exiting)
			V_DrawKartString(fx+33, fy+3, V_HUDTRANS|V_SLIDEIN|splitflags, "FIN");
		else
			V_DrawKartString(fx+33, fy+3, V_HUDTRANS|splitflags, va("%d/%d", min(stplyr->laps, numlaps), numlaps));
	}
}

void K_getLivesnStatsDrawinfo(drawinfo_t *out)
{
	int32_t fx, fy, splitflags = 0;

	// pain and suffering defined below
	if (r_splitscreen < 2)	// don't change shit for THIS splitscreen.
	{
		fx = LIVE_X;
		fy = LIVE_Y;
		splitflags = V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANS|V_SPLITSCREEN;
	}
	else
	{
		if (stplyrnum == 0 || stplyrnum == 2)	// If we are P1 or P3...
		{
			fx = LIVE_X;
			fy = LIVE_Y;
			splitflags = V_SNAPTOLEFT|V_SNAPTOBOTTOM|V_SPLITSCREEN;
		}
		else // else, that means we're P2 or P4.
		{
			fx = LIVE2_X;
			fy = LIVE2_Y;
			splitflags = V_SNAPTORIGHT|V_SNAPTOBOTTOM|V_SPLITSCREEN;
		}
	}

	out->x = fx;
	out->y = fy;
	out->flags = splitflags;
}

static void K_drawKartStatsnLives(void)
{
	const dboolean uselives = G_GametypeUsesLives();
	const dboolean stats = cv_showstats.value;

	int32_t fx = 0, fy = 0, splitflags = 0;	// stuff for 3p / 4p splitscreen.

	drawinfo_t info;
	K_getLivesnStatsDrawinfo(&info);
	fx = info.x;
	fy = info.y;
	splitflags = info.flags;

	if (r_splitscreen > 1)
	{
		// Lives
		if (LUA_HudEnabled(hud_lives) && uselives)
		{
			// We specify stplyr->skincolor since we want it to match the player and not the hud color.
			uint8_t *colormap = R_GetTranslationColormap(stplyr->skin, stplyr->skincolor, GTC_CACHE);
			patch_t *mmappatch = faceprefix[stplyr->skin][FACE_MINIMAP];

			V_DrawMappedPatch(fx+21+mmappatch->leftoffset, fy-13+mmappatch->topoffset, V_HUDTRANS|splitflags, mmappatch, colormap);
			if (stplyr->lives >= 0 && uselives)
				V_DrawScaledPatch(fx+34, fy-10, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[(stplyr->lives % 10)]); // make sure this doesn't overflow OR underflow
		}
	}
	else
	{
		// Stats and Lives
		if (LUA_HudEnabled(hud_lives) && (uselives || stats))
		{
			int32_t offsetx = 0;
			int32_t offsety = 0;
			dboolean split = r_splitscreen == 1;

			if ((cv_lives_xoffset.value == 0 && cv_lives_yoffset.value == 0) || split)
			{
				if ((cv_newspeedometer.value == 0 || cv_newspeedometer.value == 2 || cv_newspeedometer.value == 3) && !K_RingsActive() && !split)
				{
					offsetx = 25;
					offsety = 15;
				}
				else if (K_RingsActive())
				{
					offsetx = 4;
				}
			}

			// We specify stplyr->skincolor since we want it to match the player and not the hud color.
			uint8_t *colormap = R_GetTranslationColormap(stplyr->skin, stplyr->skincolor, GTC_CACHE);
			dboolean hires = K_UseHighResPortraits();
			patch_t *facerank = faceprefix[stplyr->skin][hires ? FACE_WANTED : FACE_RANK];
			int16_t topoffset = hires ? facerank->topoffset / 2 : facerank->topoffset;
			int16_t leftoffset = hires ? facerank->leftoffset / 2 : facerank->leftoffset;
			V_DrawFixedPatch((fx+59+offsetx+leftoffset)<<FRACBITS, (fy-16+offsety+topoffset)<<FRACBITS, hires ? FRACUNIT/2 : FRACUNIT, V_HUDTRANS|splitflags, facerank, colormap);
			if (stplyr->lives >= 0 && uselives)
				V_DrawScaledPatch(fx+77+offsetx, fy-11+offsety, V_HUDTRANS|splitflags, kp_facenum[(stplyr->lives % 10)]); // make sure this doesn't overflow OR underflow

			if (stats)
			{
				// Draw stats
				uint8_t *colormapstat = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_BLUE, GTC_CACHE);
				uint8_t *colormapstat2 = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_ORANGE, GTC_CACHE);

				if (stplyr->kartspeed != skins[stplyr->skin]->kartspeed
					|| stplyr->kartweight != skins[stplyr->skin]->kartweight)
				{
					colormapstat = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_YELLOW, GTC_CACHE);
					colormapstat2 = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_MUSTARD, GTC_CACHE);
				}

				V_DrawFixedPatch((fx+56+offsetx+leftoffset)<< FRACBITS, (fy-19+offsety+topoffset)<< FRACBITS, FRACUNIT, V_HUDTRANS|splitflags, kp_facenum[(stplyr->kartspeed % 10)], colormapstat);
				V_DrawFixedPatch((fx+69+offsetx+leftoffset)<< FRACBITS, (fy-4+offsety+topoffset)<< FRACBITS, FRACUNIT, V_HUDTRANS|splitflags, kp_facenum[(stplyr->kartweight % 10)], colormapstat2);
			}
		}
	}
}

static void K_drawKartAccessibilityIcons(int32_t fx)
{
	int32_t fy = ACCE_Y-25;
	int32_t splitflags = V_SNAPTOLEFT|V_SNAPTOBOTTOM|V_HUDTRANS|V_SPLITSCREEN;
	//int32_t step = 1; -- if there's ever more than one accessibility icon

	fx += ACCE_X;

	if (r_splitscreen < 2) // adjust to speedometer height
	{
		if (cv_acce_xoffset.value == 0 && cv_acce_yoffset.value == 0)
		{

			if (cv_newspeedometer.value == 0 || (cv_newspeedometer.value == 2 && !K_RingsActive()))
			{
				fy += 44;
			}
			else
			{
				if ((cv_newspeedometer.value == 0 || cv_newspeedometer.value == 2) && !K_RingsActive())
					fy += 18;

				if ((gametypes[gametype]->rules & GTR_BUMPERS) && !(gametypes[gametype]->rules & GTR_CIRCUIT))
					fy -= 4;
			}
		}
	}
	else
	{
		fx = ACCE_X+43;
		fy = ACCE_Y;
		if (!(stplyrnum == 0 || stplyrnum == 2))	// If we are not P1 or P3...
		{
			splitflags ^= (V_SNAPTOLEFT|V_SNAPTORIGHT);
			fx = (BASEVIDWIDTH/2) - (fx + 10);
			//step = -step;
		}
	}

	if (stplyr->pflags & PF_KICKSTARTACCEL) // just KICKSTARTACCEL right now, maybe more later
	{
		int8_t col = 0, wid, fil, ofs;
		uint8_t i = 7;
		ofs = (stplyr->kickstartaccel == ACCEL_KICKSTART) ? 1 : 0;
		fil = i-(stplyr->kickstartaccel*i)/ACCEL_KICKSTART;

		V_DrawFill(fx+4, fy+ofs-1, 2, 1, 31|splitflags);
		V_DrawFill(fx, (fy+ofs-1)+8, 10, 1, 31|splitflags);

		while (i--)
		{
			wid = (i/2)+1;
			V_DrawFill(fx+4-wid, fy+ofs+i, 2+(wid*2), 1, 31|splitflags);
			if (fil > 0)
			{
				if (i < fil)
					col = 23;
				else if (i == fil)
					col = 3;
				else
					col = 5 + (i-fil)*2;
			}
			else if ((leveltime % 7) == i)
				col = 0;
			else
				col = 3;
			V_DrawFill(fx+5-wid, fy+ofs+i, (wid*2), 1, col|splitflags);
		}

		//fx += step*12;
	}
}

#ifdef ROTSPRITE

#define DIALSPDDIV 97090 // 1.48148; converts 200 to 135
#define MPHDIV 68283 // 1.04192; converts 141 to 135

static void K_DrawDialNum(int32_t x, int32_t y, dboolean colorized, int32_t flags, int32_t num, int32_t digits, const uint8_t *colormap)
{
	int32_t w = 0;
	w = kp_dialnum[0]->width;

	if (flags & V_NOSCALESTART)
		w *= vid.dup;

	if (num < 0)
		num = -num;

	// draw the number
	do
	{
		x -= (w);

		if (colorized)
			V_DrawFixedPatch(x << FRACBITS, y << FRACBITS, FRACUNIT, flags, kp_dialnum[(num % 10) + 10], colormap);
		else
			V_DrawFixedPatch(x << FRACBITS, y << FRACBITS, FRACUNIT, flags, kp_dialnum[num % 10], colormap);

		num /= 10;
	} while (--digits);
}

static void K_DrawDialLaps(int32_t x, int32_t y, int32_t num, int32_t total, int32_t flags)
{
	int32_t fx;
	fx = x + ((num < 10) ? 0 : 6);
	V_DrawRankNum(fx, y, flags, num, (num < 10) ? 1 : 2, NULL);
	V_DrawScaledPatch(fx + 2, y, flags, frameslash);
	V_DrawRankNum(fx + 13 + ((total < 10) ? 0 : 6), y, flags, total, (total < 10) ? 1 : 2, NULL);
}

static void K_DrawDialSpeedometer(fixed_t speed,
								  fixed_t divisor,
								  uint16_t labeln,
								  int32_t splitflags,
								  dboolean battlemode,
								  dboolean infoactive,
								  dboolean colorized)
{
	const uint8_t* colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);

	const uint8_t infoidx = (infoactive) ? 1 : 0;

	const fixed_t spd = FixedDiv(speed, divisor);
	const angle_t speedangle =
		FixedAngle(((min(135 * FRACUNIT, spd) - (45 * FRACUNIT))));

	if (colorized)  // Colourized hud
	{
		V_DrawMappedPatch(
			SPDM_X, SPDM_Y - 25, (V_HUDTRANS | splitflags), kp_dialbase[infoidx + 2], colormap);
		V_DrawMappedPatch(SPDM_X,
						  SPDM_Y + 14,
						  V_HUDTRANS | splitflags,
						  kp_speedpatchesdial[labeln + 4],
						  colormap);
	}
	else
	{
		V_DrawScaledPatch(SPDM_X, SPDM_Y - 25, (V_HUDTRANS | splitflags), kp_dialbase[infoidx]);
		V_DrawScaledPatch(SPDM_X,
						  SPDM_Y + 14,
						  V_HUDTRANS | splitflags,
						  kp_speedpatchesdial[labeln]);
	}

	K_DrawDialNum(SPDM_X + 10,
				  SPDM_Y + 9,
				  colorized,
				  V_HUDTRANS | splitflags,
				  speed / FRACUNIT,
				  3,
				  colormap);

	// gotta center the dial manually
	V_DrawRotatedPatch((SPDM_X - 19) << FRACBITS,
			   (SPDM_Y - 1) << FRACBITS,
			   speedangle,
			   FRACUNIT,
			   FRACUNIT,
			   (V_HUDTRANS | splitflags),
			   kp_dial,
			   colormap);

	if (!infoactive)
	{
		// no need to draw info if we're not supposed to
		return;
	}

	// draw the info
	if (battlemode)
	{
		if (itembreaker)
		{
			V_DrawMappedPatch(SPDM_X + 28, SPDM_Y + 12, V_HUDTRANS | splitflags, kp_itemboxminimap, NULL);
			K_DrawDialLaps(SPDM_X + 46,
				       SPDM_Y + 13,
				       numtargets,
				       nummapboxes,
				       V_HUDTRANS | splitflags);
		}
		else
		{
			if (stplyr->bumper <= 0 && (gametypes[gametype]->rules & GTR_KARMA) && comeback)
			{
				V_DrawMappedPatch(
					SPDM_X + 28, SPDM_Y + 12, V_HUDTRANS | splitflags, kp_splitkarmabomb, colormap);
				K_DrawDialLaps(SPDM_X + 46,
							SPDM_Y + 13,
							stplyr->karmapoints,
							2,
							V_HUDTRANS | splitflags);
			}
			else  // the above doesn't need to account for weird stuff since the max amount of karma
				// necessary is always 2 ^^^^
			{
				int32_t maxbumper = K_StartingBumperCount();
				V_DrawMappedPatch(
					SPDM_X + 28, SPDM_Y + 12, V_HUDTRANS | splitflags, kp_rankbumper, colormap);
				K_DrawDialLaps(SPDM_X + 46,
							SPDM_Y + 13,
							stplyr->bumper,
							maxbumper,
							V_HUDTRANS | splitflags);
			}
		}
	}
	else
	{
		V_DrawScaledPatch(SPDM_X + 28, SPDM_Y + 12, V_HUDTRANS | splitflags, kp_splitlapflag);

		if (stplyr->exiting)
			V_DrawScaledPatch(SPDM_X + 39, SPDM_Y + 13, V_HUDTRANS | splitflags, kp_dialfinish);
		else
			K_DrawDialLaps(SPDM_X + 46,
						   SPDM_Y + 13,
						   stplyr->laps,
						   numlaps,
						   V_HUDTRANS | splitflags);
	}
}

#endif

static void K_drawKartSpeedometer(void)
{
	static fixed_t convSpeed[2] = {0};
	uint8_t labeln = 0;
	uint8_t numbers[3];
	int32_t splitflags = V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_SPLITSCREEN;
	int32_t battleoffset = 0;
	int32_t ringoffset = 0;
	int32_t oldringoffset = 0;
#ifdef ROTSPRITE
	fixed_t dial_divisor = DIALSPDDIV;
#endif

	switch (cv_kartspeedometer.value)
	{
		case 1: // Kilometers
			convSpeed[0] = FixedDiv(FixedMul(stplyr->speed, 142371), mapobjectscale); // 2.172409058
			convSpeed[1] = convSpeed[0] / FRACUNIT;
			labeln = 0;
			break;
		case 2: // Miles
			convSpeed[0] = FixedDiv(FixedMul(stplyr->speed, 88465), mapobjectscale); // 1.349868774
			convSpeed[1] = convSpeed[0] / FRACUNIT;
			labeln = 1;
			break;
		case 3: // Fracunits
			convSpeed[0] = FixedDiv(stplyr->speed, mapobjectscale); // 1.0. duh.
			convSpeed[1] = convSpeed[0] / FRACUNIT;
			labeln = 2;
			break;
		case 4: // Sonic Drift 2 style percentage
			if (stplyr->mo)
			{
				convSpeed[0] = (FixedDiv(stplyr->speed, FixedMul(K_GetKartSpeed(stplyr, false, false), K_PlayerBaseFriction(stplyr, ORIG_FRICTION)))*100);
				convSpeed[1] = convSpeed[0] >> FRACBITS;
			}
			labeln = 3;
			break;
		default:
			break;
	}

	// Don't overflow
	// (negative speed IS really high speed :V)
	if (convSpeed[1] > 999 || convSpeed[1] < 0)
	{
		convSpeed[1] = 999;
		convSpeed[0] = convSpeed[1] * FRACUNIT;
	}

	if (cv_speed_xoffset.value == 0 && cv_speed_yoffset.value == 0)
	{
		if ((gametypes[gametype]->rules & GTR_BUMPERS) && !(gametypes[gametype]->rules & GTR_CIRCUIT))
			battleoffset = -4;

		if (K_RingsActive() == true)
		{
			ringoffset = -16;
			oldringoffset = 6;
		}
	}

	if (cv_newspeedometer.value == 0)
	{
		switch (cv_kartspeedometer.value) {
			case 1:
				V_DrawKartString(SPDM_X + oldringoffset, SPDM_Y-18 + battleoffset + ringoffset, V_HUDTRANS|splitflags, va("%3d km/h", convSpeed[1]));
				break;
			case 2:
				V_DrawKartString(SPDM_X + oldringoffset, SPDM_Y-18 + battleoffset + ringoffset, V_HUDTRANS|splitflags, va("%3d mph", convSpeed[1]));
				break;
			case 3:
				V_DrawKartString(SPDM_X + oldringoffset, SPDM_Y-18 + battleoffset + ringoffset, V_HUDTRANS|splitflags, va("%3d fu/t", convSpeed[1]));
				break;
			case 4:
				V_DrawKartString(SPDM_X + oldringoffset, SPDM_Y-18 + battleoffset + ringoffset, V_HUDTRANS|splitflags, va("%4d %%", convSpeed[1]));
				break;
			default:
				break;
		}
	}
	else if (cv_newspeedometer.value == 1)
	{
		numbers[0] = ((convSpeed[1] / 100) % 10);
		numbers[1] = ((convSpeed[1] / 10) % 10);
		numbers[2] = (convSpeed[1] % 10);

		if (!K_UseColorHud())
		{

			V_DrawScaledPatch(SPDM_X, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_speedometersticker[0]);
		}
		else
		{
			uint8_t *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
			V_DrawMappedPatch(SPDM_X, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_speedometersticker[1], colormap);
		}

		V_DrawScaledPatch(SPDM_X+7, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_facenum[numbers[0]]);
		V_DrawScaledPatch(SPDM_X+13, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_facenum[numbers[1]]);
		V_DrawScaledPatch(SPDM_X+19, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_facenum[numbers[2]]);
		V_DrawScaledPatch(SPDM_X+29, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_speedometerlabel[labeln]);
	}
	else if (cv_newspeedometer.value == 2)
	{
		fixed_t fuspeed =  FixedDiv(stplyr->speed, mapobjectscale)/FRACUNIT;
		int32_t spdpatch = 0;
		int8_t yoffset = K_RingsActive() ? 12 : 18;

#define NUM_INTERVALS 22
		const int speedIntervals[NUM_INTERVALS] = {2, 5, 7, 10, 12, 15, 17, 20, 22, 25, 27, 30, 32, 35, 37, 40, 42, 45, 47, 50, 52, 55};

		for (int i = 0; i < NUM_INTERVALS; ++i)
		{
			if (fuspeed < speedIntervals[i])
			{
				spdpatch = i;
				break;
			}
		}
#undef NUM_INTERVALS

		if (((fuspeed < 57 && fuspeed > 54) || (fuspeed < 60 && fuspeed > 56) || (fuspeed > 59)) && (leveltime & 4))
			spdpatch = 24;
		else if (((fuspeed < 57 && fuspeed > 54) || (fuspeed < 60 && fuspeed > 56) || (fuspeed > 59)) && !(leveltime & 4))
			spdpatch = 23;

		V_DrawScaledPatch(SPDM_X, SPDM_Y-yoffset + battleoffset + ringoffset, V_HUDTRANS|splitflags, (!K_RingsActive() ? kp_kartzspeedo : kp_kartzspeedo_smol)[spdpatch]);
	}
#ifdef ROTSPRITE
	else if (cv_newspeedometer.value == 3)
	{
		K_DrawDialSpeedometer(convSpeed[0],
							dial_divisor,
							labeln,
							splitflags,
							(dboolean)((gametypes[gametype]->rules & GTR_BUMPERS) == GTR_BUMPERS),
							(LUA_HudEnabled(hud_gametypeinfo)),
							(K_UseColorHud()));
	}
#endif

	K_drawKartAccessibilityIcons((cv_newspeedometer.value == 0 || cv_newspeedometer.value == 2) ? 50 : 56);
}

#ifdef ROTSPRITE
#undef DIALSPDDIV
#undef MPHDIV
#endif

static void K_drawRingMeter(void)
{
	uint8_t rn[2];
	uint8_t *ringmap = NULL;
	dboolean colorring = false;
	int8_t ringcount = stplyr->rings;
	// int8_t overring = stplyr->rings % 20;
	int8_t ringmax = stplyr->ringmax;
	int32_t fx = 0, fy = 0, splitflags = 0;	// stuff for 3p / 4p splitscreen.

	rn[0] = ((abs(ringcount) / 10) % 10);
	rn[1] = (abs(ringcount) % 10);

	drawinfo_t info;
	K_getRingsDrawinfo(&info);
	fx = info.x;
	fy = info.y;
	splitflags = info.flags;

	if (ringcount <= 0 && (leveltime/5 & 1)) // In debt
	{
		ringmap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_CRIMSON, GTC_CACHE);
		colorring = true;
	}
	else if (ringcount > 20) // (placeholder) over-ring
		ringmap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_BLUE, GTC_CACHE);
	else if (ringcount >= ringmax) // Maxed out
		ringmap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_YELLOW, GTC_CACHE);

	if (r_splitscreen > 1)
	{
		V_DrawMappedPatch(fx, fy-10, V_HUDTRANS|splitflags, kp_ringsplitscreen, (colorring ? ringmap : NULL));

		if (ringcount < 0) // Draw the minus for ring debt
			V_DrawMappedPatch(fx+7, fy-8, V_HUDTRANS|splitflags, kp_ringdebtminussmall, ringmap);

		V_DrawMappedPatch(fx+11, fy-10, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[rn[0]], ringmap);
		V_DrawMappedPatch(fx+15, fy-10, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[rn[1]], ringmap);
	}
	else
	{
		int8_t i;
		int8_t ringoffsety = 0;
		uint8_t *colormap = NULL;
		int8_t coloroffset = 0;

		if (cv_speed_xoffset.value == 0 && cv_speed_yoffset.value == 0)
		{
			if ((gametypes[gametype]->rules & GTR_BUMPERS) && !(gametypes[gametype]->rules & GTR_CIRCUIT))
				ringoffsety -= 4;

			if (itembreaker)
				ringoffsety -= 2;
		}

		if (K_UseColorHud())
			coloroffset = 2;

		colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);

		V_DrawMappedPatch(fx, fy-14 + ringoffsety, V_HUDTRANS|splitflags, kp_ringsticker[((stplyr->pflags & PF_RINGLOCK) ? 1 : 0) + coloroffset], colormap);

		if (stplyr->rings < 0) // Draw the minus for ring debt
		{
			V_DrawMappedPatch(fx-5, fy-11 + ringoffsety, V_HUDTRANS|splitflags, kp_ringdebtminus, ringmap);
		}
#if 0
		if (stplyr->rings < 0)
		{
			// Invert the ring count
			ringcount = -ringcount;
		}
#endif
		// clamp and invert when needed for the bar
		ringcount = CLAMP(abs(ringcount), -20, 20);

		if (rn[1] == 1 && ringcount == 11)
			V_DrawMappedPatch(fx+2, fy-11 + ringoffsety, V_HUDTRANS|splitflags, kp_facenum[rn[0]], ringmap);
		else
			V_DrawMappedPatch(fx+2, fy-11 + ringoffsety, V_HUDTRANS|splitflags, kp_facenum[rn[0]], ringmap);

		if (rn[1] == 1 && ringcount == 11)
			V_DrawMappedPatch(fx+7, fy-11 + ringoffsety, V_HUDTRANS|splitflags, kp_facenum[rn[1]], ringmap);
		else
			V_DrawMappedPatch(fx+8, fy-11 + ringoffsety, V_HUDTRANS|splitflags, kp_facenum[rn[1]], ringmap);

		// Draw the fillbars
		if (stplyr->rings)
		{
			uint8_t barcolors[5] = {66,72,2,68};
			dboolean indebt = false;

			if (stplyr->rings < 0)
			{
				barcolors[0] = 38;
				barcolors[1] = 36;
				barcolors[2] = 32;
				barcolors[3] = 40;
				indebt = true;
			}
			else if (stplyr->rings > 20)
			{
				barcolors[0] = 132;
				barcolors[1] = 131;
				barcolors[2] = 128;
				barcolors[3] = 154;
			}

			if (!indebt || (indebt && (leveltime/5 & 1)))
			{
				for (i = 0; i != ringcount; i++)
				{

					V_DrawFill(fx+17+(2*i), fy-10 + ringoffsety, 1, 1, barcolors[0]|splitflags);
					V_DrawFill(fx+17+(2*i), fy-9 + ringoffsety, 1, 4, barcolors[1]|splitflags);
					V_DrawFill(fx+17+(2*i), fy-8 + ringoffsety, 1, 1, barcolors[2]|splitflags);
					V_DrawFill(fx+17+(2*i), fy-7 + ringoffsety, 1, 1, barcolors[3]|splitflags);
				}
			}
		}
	}
}

static void K_drawKartBumpersOrKarma(void)
{
	uint8_t *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
	int32_t fx = 0, fy = 0, splitflags = 0;	// stuff for 3p / 4p splitscreen.

	drawinfo_t info;
	K_getLapsDrawinfo(&info);
	fx = info.x;
	fy = info.y;
	splitflags = info.flags;

	if (r_splitscreen > 1)
	{

		V_DrawScaledPatch(fx+22, fy, V_HUDTRANS|splitflags, frameslash);

		if (itembreaker)
		{
			V_DrawMappedPatch(fx+1, fy-2, V_HUDTRANS|splitflags, kp_itemboxminimap, NULL);

			if (numtargets > 9 || nummapboxes > 9)
			{
				uint8_t ln[2];
				ln[0] = ((numtargets / 10) % 10);
				ln[1] = (numtargets % 10);

				V_DrawScaledPatch(fx+13, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[0]]);
				V_DrawScaledPatch(fx+17, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[1]]);

				ln[0] = ((nummapboxes / 10) % 10);
				ln[1] = (nummapboxes % 10);

				V_DrawScaledPatch(fx+27, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[0]]);
				V_DrawScaledPatch(fx+31, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[1]]);
			}
			else
			{
				V_DrawScaledPatch(fx+13, fy, V_HUDTRANS|splitflags, kp_facenum[numtargets % 10]);
				V_DrawScaledPatch(fx+27, fy, V_HUDTRANS|splitflags, kp_facenum[nummapboxes % 10]);
			}
		}
		else
		{
			if (stplyr->bumper <= 0 && (gametypes[gametype]->rules & GTR_KARMA) && comeback)
			{
				V_DrawMappedPatch(fx, fy-1, V_HUDTRANS|splitflags, kp_splitkarmabomb, colormap);
				V_DrawScaledPatch(fx+13, fy, V_HUDTRANS|splitflags, kp_facenum[stplyr->karmapoints % 10]);
				V_DrawScaledPatch(fx+27, fy, V_HUDTRANS|splitflags, kp_facenum[2 % 10]);
			}
			else
			{
				int32_t maxbumper = K_StartingBumperCount();
				V_DrawMappedPatch(fx+1, fy-2, V_HUDTRANS|splitflags, kp_rankbumper, colormap);

				if (stplyr->bumper > 9 || maxbumper > 9)
				{
					uint8_t ln[2];
					ln[0] = (stplyr->bumper / 10 % 10);
					ln[1] = (stplyr->bumper % 10);

					V_DrawScaledPatch(fx+13, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[0]]);
					V_DrawScaledPatch(fx+17, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[1]]);

					ln[0] = ((abs(maxbumper) / 10) % 10);
					ln[1] = (abs(maxbumper) % 10);

					V_DrawScaledPatch(fx+27, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[0]]);
					V_DrawScaledPatch(fx+31, fy, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[ln[1]]);
				}
				else
				{
					V_DrawScaledPatch(fx+13, fy, V_HUDTRANS|splitflags, kp_facenum[(stplyr->bumper) % 10]);
					V_DrawScaledPatch(fx+27, fy, V_HUDTRANS|splitflags, kp_facenum[(maxbumper) % 10]);
				}
			}
		}
	}
	else
	{
		if (itembreaker)
		{
			patch_t *item = W_CachePatchName("RNDMA0", PU_PATCH);
			uint8_t *itemcolormap = R_GetTranslationColormap(TC_BLINK, SKINCOLOR_BLACK, GTC_CACHE);

			if (!K_UseColorHud())
			{
				V_DrawScaledPatch(fx, fy, V_HUDTRANS|splitflags, kp_timesticker[0]);
			}
			else //Colourized hud
			{
				V_DrawMappedPatch(fx, fy, V_HUDTRANS|splitflags, kp_timesticker[1], colormap);
			}

			V_DrawStretchyFixedPatch((29 + item->width/2/4)*FRACUNIT, 193*FRACUNIT, FRACUNIT/2, FRACUNIT/2, V_HUDTRANS|splitflags, item, itemcolormap);
			V_DrawStretchyFixedPatch((29 + item->width/2/4)*FRACUNIT, 192*FRACUNIT, FRACUNIT/2, FRACUNIT/2, V_HUDTRANS|splitflags, item, NULL);
			V_DrawKartString(fx+47, fy+3, V_HUDTRANS|splitflags, va("%d/%d", numtargets, nummapboxes));
		}
		else
		{
			if (stplyr->bumper <= 0)
			{
				V_DrawMappedPatch(fx, fy, V_HUDTRANS|splitflags, (K_UseColorHud() ? kp_karmasticker[1] : kp_karmasticker[0]), colormap);
				V_DrawKartString(fx+47, fy+3, V_HUDTRANS|splitflags, va("%d/2", stplyr->karmapoints));
			}
			else
			{
				int32_t maxbumper = K_StartingBumperCount();

				if (stplyr->bumper > 9 && maxbumper > 9)
					V_DrawMappedPatch(fx, fy, V_HUDTRANS|splitflags, (K_UseColorHud() ? kp_bumperstickerwide[1] : kp_bumperstickerwide[0]), colormap);
				else
					V_DrawMappedPatch(fx, fy, V_HUDTRANS|splitflags, (K_UseColorHud() ? kp_bumpersticker[1] : kp_bumpersticker[0]), colormap);

				V_DrawKartString(fx+47, fy+3, V_HUDTRANS|splitflags, va("%d/%d", stplyr->bumper, maxbumper));
			}
		}
	}
}

static void K_drawKartWanted(void)
{
	uint8_t i, numwanted = 0;
	uint8_t *colormap = NULL;
	int32_t basex = 0, basey = 0;

	for (i = 0; i < 4; i++)
	{
		if (battlewanted[i] == -1)
			break;
		numwanted++;
	}

	if (numwanted <= 0)
		return;

	// set X/Y coords depending on splitscreen.
	if (r_splitscreen < 3) // 1P and 2P use the same code.
	{
		basex = WANT_X;
		basey = WANT_Y;
		if (r_splitscreen == 2)
		{
			basey += 16; // slight adjust for 3P
			basex -= 6;
		}
		else if (!r_splitscreen)
		{
			basex -= 48;

			// Position Number offset....
			if (gametypes[gametype]->rules & GTR_CIRCUIT)
				basex -= 30;
		}
	}
	else if (r_splitscreen == 3) // 4P splitscreen...
	{
		basex = BASEVIDWIDTH/2 - (kp_wantedsplit->width/2);	// center on screen
		basey = BASEVIDHEIGHT - 55;
		//basey2 = 4;
	}

	if (battlewanted[0] != -1)
		colormap = R_GetTranslationColormap(0, players[battlewanted[0]].skincolor, GTC_CACHE);
	V_DrawFixedPatch(basex<<FRACBITS, basey<<FRACBITS, FRACUNIT, V_HUDTRANS|(r_splitscreen < 3 ? V_SNAPTORIGHT : 0)|V_SNAPTOBOTTOM, (r_splitscreen > 1 ? kp_wantedsplit : kp_wanted), colormap);
	/*if (basey2)
		V_DrawFixedPatch(basex<<FRACBITS, basey2<<FRACBITS, FRACUNIT, V_HUDTRANS|V_SNAPTOTOP, (splitscreen == 3 ? kp_wantedsplit : kp_wanted), colormap);	// < used for 4p splits.*/

	for (i = 0; i < numwanted; i++)
	{
		int32_t x = basex+(r_splitscreen > 1 ? 13 : 8), y = basey+(r_splitscreen > 1 ? 16 : 21);
		fixed_t scale = FRACUNIT/2;
		player_t *p = &players[battlewanted[i]];

		if (battlewanted[i] == -1)
			break;

		if (numwanted == 1)
			scale = FRACUNIT;
		else
		{
			if (i & 1)
				x += 16;
			if (i > 1)
				y += 16;
		}

		if (players[battlewanted[i]].skincolor)
		{
			colormap = R_GetTranslationColormap(TC_RAINBOW, p->skincolor, GTC_CACHE);
			patch_t *wantedicon = (scale == FRACUNIT ? faceprefix[p->skin][FACE_WANTED] : faceprefix[p->skin][FACE_RANK]);
			V_DrawFixedPatch((x+wantedicon->leftoffset)<<FRACBITS, (y+wantedicon->topoffset)<<FRACBITS, FRACUNIT, V_HUDTRANS|(r_splitscreen < 3 ? V_SNAPTORIGHT : 0)|V_SNAPTOBOTTOM, wantedicon, colormap);
			/*if (basey2)	// again with 4p stuff
				V_DrawFixedPatch(x<<FRACBITS, (y - (basey-basey2))<<FRACBITS, FRACUNIT, V_HUDTRANS|V_SNAPTOTOP, (scale == FRACUNIT ? faceprefix[p->skin][FACE_WANTED] : faceprefix[p->skin][FACE_RANK]), colormap);*/
		}
	}
}

static void K_drawKartPlayerCheck(void)
{
	const fixed_t maxdistance = FixedMul(1280 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	uint8_t i;
	int32_t splitflags = V_SNAPTOBOTTOM|V_SPLITSCREEN;
	fixed_t y = CHEK_Y * FRACUNIT;

	if (stplyr == NULL || stplyr->mo == NULL || P_MobjWasRemoved(stplyr->mo))
	{
		return;
	}

	if (stplyr->spectator || stplyr->awayviewtics)
	{
		return;
	}

	if (stplyr->cmd.buttons & BT_LOOKBACK)
	{
		return;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *checkplayer = &players[i];
		fixed_t distance = maxdistance+1;
		uint8_t *colormap = NULL;
		uint8_t pnum = 0;
		vector3_t v;
		vector3_t pPos;
		trackingResult_t result;

		if (!playeringame[i] || checkplayer->spectator)
		{
			// Not in-game
			continue;
		}

		if (checkplayer->mo == NULL || P_MobjWasRemoved(checkplayer->mo))
		{
			// No object
			continue;
		}

		if (checkplayer == stplyr)
		{
			// This is you!
			continue;
		}

		v.x = R_InterpolateFixed(checkplayer->mo->old_x, checkplayer->mo->x);
		v.y = R_InterpolateFixed(checkplayer->mo->old_y, checkplayer->mo->y);
		v.z = R_InterpolateFixed(checkplayer->mo->old_z, checkplayer->mo->z);

		pPos.x = R_InterpolateFixed(stplyr->mo->old_x, stplyr->mo->x);
		pPos.y = R_InterpolateFixed(stplyr->mo->old_y, stplyr->mo->y);
		pPos.z = R_InterpolateFixed(stplyr->mo->old_z, stplyr->mo->z);

		distance = R_PointToDist2(pPos.x, pPos.y, v.x, v.y);

		if (distance > maxdistance)
		{
			// Too far away
			continue;
		}

		if ((checkplayer->invincibilitytimer <= 0) && (checkplayer->smonitortimer <= 0) && (leveltime & 2))
		{
			pnum++; // white frames
		}

		if (checkplayer->itemtype == KITEM_FLAMESHIELD || checkplayer->flametimer > 0)
		{
			pnum += 8;
		}
		else if (K_IsAltShrunk(checkplayer) && (checkplayer->itemtype == KITEM_SHRINK || checkplayer->growshrinktimer < 0))
		{
			pnum += 6;
		}
		else if (checkplayer->itemtype == KITEM_GROW || checkplayer->growshrinktimer > 0)
		{
			pnum += 4;
		}
		else if ((!K_IsKartItemAlternate(KITEM_INVINCIBILITY) && checkplayer->itemtype == KITEM_INVINCIBILITY) || checkplayer->invincibilitytimer)
		{
			pnum += 2;
		}
		else if ((K_IsKartItemAlternate(KITEM_INVINCIBILITY) && checkplayer->itemtype == KITEM_INVINCIBILITY) || checkplayer->smonitortimer)
		{
			// FIXME: Separate "CHECK" icon(?)
			pnum += 2;
		}

		K_ObjectTracking(&result, &v, true);

		if (result.onScreen == true)
		{
			colormap = R_GetTranslationColormap(TC_DEFAULT, checkplayer->mo->color, GTC_CACHE);
			V_DrawFixedPatch(result.x, y, FRACUNIT, V_HUDTRANS|V_SPLITSCREEN|splitflags, kp_check[pnum], colormap);
		}
	}
}

static dboolean K_ShowPlayerNametag(player_t *p)
{
	if (cv_seenames.value == NT_OFF)
	{
		return false;
	}

	if (demo.playback == true && camera[R_GetViewNumber()].freecam == true)
	{
		return true;
	}

	if (stplyr == p)
	{
		if (cv_seeownname.value)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	if (gametypes[gametype]->rules & GTR_CIRCUIT)
	{
		if ((p->position == 0)
		|| (stplyr->position == 0)
		|| (p->position < stplyr->position-2)
		|| (p->position > stplyr->position+2))
		{
			return false;
		}
	}

	return true;
}

static void K_DrawLocalTagForPlayer(fixed_t x, fixed_t y, player_t *p, uint8_t id, uint8_t flags)
{
	uint16_t chatcolor = skincolors[p->skincolor].chatcolor;

	V_DrawCenteredSmallStringAtFixed(x, y, V_HUDTRANS|chatcolor|flags, va("P%d", id+1));
}

static void K_DrawRivalTagForPlayer(fixed_t x, fixed_t y, uint8_t flags)
{
	uint8_t blinkstates[4] = {SKINCOLOR_RED, SKINCOLOR_ORANGE, SKINCOLOR_YELLOW, SKINCOLOR_ORANGE};
	uint16_t chatcolor = skincolors[blinkstates[(leveltime / 3) % 4]].chatcolor;

	V_DrawCenteredSmallStringAtFixed(x, y, V_HUDTRANS|chatcolor|flags, "RIVAL");
}

static const char *K_StringTypingDot(uint8_t duration)
{
	static const char *dots = "...";

	return dots + CLAMP(3 - duration/16, 0, 3);
}

static void K_DrawTypingNotifier(fixed_t x, fixed_t y, player_t *p, uint8_t flags)
{
	if (p->cmd.flags & TICCMD_TYPING)
	{
		V_DrawCenteredSmallStringAtFixed(x, y, V_SPLITSCREEN|flags, va("Typing%s",K_StringTypingDot(p->typing_duration)));
	}
}

static void K_DrawNameTagForPlayer(fixed_t x, fixed_t y, player_t *p, uint8_t flags)
{
	const int32_t clr = skincolors[p->skincolor].chatcolor;
	int32_t namelen = V_ThinStringWidth(player_names[p - players], V_6WIDTHSPACE|V_ALLOWLOWERCASE);

	if (p->clan[0] != 0)
		namelen = V_ThinStringWidth(va("%s%s", p->clan, player_names[p - players]), V_6WIDTHSPACE|V_ALLOWLOWERCASE)+2;

	uint8_t *colormap = V_GetStringColormap(clr);
	int32_t barx = x + 6*FRACUNIT, bary = y - 16*FRACUNIT, barw = namelen*FRACUNIT;
	uint8_t backcolor = colormap ? colormap[31] : 31, frontcolor = colormap ? colormap[0] : 0;
	int32_t vflags = V_SPLITSCREEN|flags;
	fixed_t textxoffset = 0;
	fixed_t textyoffset = 0;
	fixed_t barxoffset = 0;
	fixed_t baryoffset = 0;

	// Lat: 10/06/2020: colormap can be NULL on the frame you join a game, just arbitrarily use palette indexes 31 and 0 instead of whatever the colormap would give us instead to avoid crashes.

	// Calculate offsets and draw the stem
	{
		fixed_t stemx;
		fixed_t stemy;
		int8_t stemcount = 4;
		int j;
		dboolean flipcam = (p->pflags & PF_FLIPCAM) && (p->mo->eflags & MFE_VERTICALFLIP);
		dboolean flipped;

		if (flipcam)
			flipped = (p->mo->eflags & MFE_VERTICALFLIP) != (stplyr->mo->eflags & MFE_VERTICALFLIP);
		else
			flipped = p->mo->eflags & MFE_VERTICALFLIP;

		stemx = x;
		stemy = y;

		switch(cv_seenames.value)
		{
			case NT_DEFAULT:
				break;
			case NT_SMALL:
				stemcount = 2;
				textxoffset = -2*FRACUNIT;
				textyoffset = flipped ? -2*FRACUNIT : 12*FRACUNIT;
				barxoffset = -3*FRACUNIT;
				baryoffset = flipped ? -7*FRACUNIT : 7*FRACUNIT;
				break;
			case NT_TEXT:
				textxoffset = -9*FRACUNIT;
				textyoffset = 21*FRACUNIT;
				stemcount = 0;
				break;
		}

		if (stemcount > 0)
		{
			if (flipped)
			{
				for	(j = 0; j < stemcount; j++)
				{
					fixed_t last = j == 3 ? FRACUNIT : 0;
					stemy += FRACUNIT*4;
					V_DrawFixedFill(stemx, stemy, 3*FRACUNIT, 4*FRACUNIT, vflags|backcolor);
					V_DrawFixedFill(stemx + FRACUNIT, stemy, FRACUNIT, 4*FRACUNIT - last, vflags|frontcolor);
					stemx += FRACUNIT;
				}

				bary += FRACUNIT*33;

				x += FRACUNIT;
				y += FRACUNIT*33;
			}
			else
			{
				for	(j = 0; j < stemcount; j++)
				{
					fixed_t last = j == 3 ? FRACUNIT : 0;
					stemy -= FRACUNIT*4;
					V_DrawFixedFill(stemx, stemy, 3*FRACUNIT, 4*FRACUNIT, vflags|backcolor);
					V_DrawFixedFill(stemx + FRACUNIT, stemy + last, FRACUNIT, 4*FRACUNIT - last, vflags|frontcolor);
					stemx += FRACUNIT;
				}
			}


			if (cv_seenames.value == NT_SMALL)
			{
				fixed_t flipoffset = flipped ? 2*FRACUNIT : 0;
				V_DrawFixedFill(barx + barxoffset - 2*FRACUNIT, bary + baryoffset + flipoffset, 3*FRACUNIT, 1*FRACUNIT, vflags|backcolor);
			}

			V_DrawFixedFill(barx + barxoffset, bary + baryoffset, barw, 3*FRACUNIT, vflags|backcolor);
			V_DrawFixedFill(barx - FRACUNIT + barxoffset, bary + FRACUNIT + baryoffset, barw, FRACUNIT, vflags|frontcolor);
		}
		// END DRAWFILL DUMBNESS
	}

	// Draw the name itself
	if (cv_seenames.value == NT_DEFAULT)
	{
		if (p->clan[0] != 0)
		{
			uint8_t length = V_ThinStringWidth(p->clan, vflags|V_6WIDTHSPACE|V_ALLOWLOWERCASE)+2;
			V_DrawThinStringAtFixed(x + (5*FRACUNIT) + textxoffset, y - (26*FRACUNIT) + textyoffset, vflags|V_6WIDTHSPACE|V_ALLOWLOWERCASE|V_SKYMAP, p->clan);
			V_DrawThinStringAtFixed(x + (5*FRACUNIT) + textxoffset + (length*FRACUNIT), y - (26*FRACUNIT) + textyoffset, vflags|V_6WIDTHSPACE|V_ALLOWLOWERCASE|clr, player_names[p - players]);
		}
		else
		{
			V_DrawThinStringAtFixed(x + (5*FRACUNIT) + textxoffset, y - (26*FRACUNIT) + textyoffset, vflags|V_6WIDTHSPACE|V_ALLOWLOWERCASE|clr, player_names[p - players]);
		}
	}
	else
	{
		if (p->clan[0] != 0)
		{
			uint8_t length = V_SmallStringWidth(p->clan, vflags|V_6WIDTHSPACE|V_ALLOWLOWERCASE)+2;
			V_DrawSmallStringAtFixed(x + (5*FRACUNIT) + textxoffset, y - (26*FRACUNIT) + textyoffset, vflags|V_6WIDTHSPACE|V_ALLOWLOWERCASE|V_SKYMAP, p->clan);
			V_DrawSmallStringAtFixed(x + (5*FRACUNIT) + textxoffset + (length*FRACUNIT), y - (26*FRACUNIT) + textyoffset, vflags|V_6WIDTHSPACE|V_ALLOWLOWERCASE|clr, player_names[p - players]);
		}
		else
		{
			V_DrawSmallStringAtFixed(x + (5*FRACUNIT) + textxoffset, y - (26*FRACUNIT) + textyoffset, vflags|V_6WIDTHSPACE|V_ALLOWLOWERCASE|clr, player_names[p - players]);
		}

	}

	// Also draw stats of restated players.
	if (cv_seenamerestat.value && (p->kartspeed != skins[p->skin]->kartspeed
		|| p->kartweight != skins[p->skin]->kartweight))
	{
		V_DrawSmallStringAtFixed(x + (5*FRACUNIT) + textxoffset, y - (31*FRACUNIT) + textyoffset, vflags, va("\x84S%d ", p->kartspeed));
		V_DrawSmallStringAtFixed(x + (15*FRACUNIT) + textxoffset, y - (31*FRACUNIT) + textyoffset, vflags, va("\x87W%d ", p->kartweight));
	}
}

playertagtype_t K_WhichPlayerTag(player_t *p)
{
	const uint8_t cnum = R_GetViewNumber();

	if (!(demo.playback == true && camera[cnum].freecam == true) && P_IsDisplayPlayer(p) &&
		p != &players[displayplayers[cnum]])
	{
		return PLAYERTAG_LOCAL;
	}
	else if (p->bot)
	{
		if ((p->botvars.rival == true || cv_forcebots.value))
		{
			return PLAYERTAG_RIVAL;
		}
		/*else if (K_ShowPlayerNametag(p) == true)
		{
			return PLAYERTAG_CPU;
		}*/
	}
	else if (netgame || demo.playback || cv_seeownname.value)
	{
		if (K_ShowPlayerNametag(p) == true)
		{
			return PLAYERTAG_NAME;
		}
	}

	return PLAYERTAG_NONE;
}

void K_DrawPlayerTag(fixed_t x, fixed_t y, player_t *p, playertagtype_t type, dboolean foreground)
{
	int32_t flags = 0;

	switch (type)
	{
		case PLAYERTAG_LOCAL:
			flags |= V_SPLITSCREEN;
			K_DrawLocalTagForPlayer(x, y, p, G_PartyPosition(p - players), flags);
			break;

		case PLAYERTAG_RIVAL:
			flags |= V_SPLITSCREEN;
			flags |= foreground ? 0 : V_60TRANS;
			K_DrawRivalTagForPlayer(x, y, flags);
			break;

			// FALLTHRU
		/*case PLAYERTAG_CPU:
			flags |= V_SPLITSCREEN;
			flags |= foreground ? 0 : V_60TRANS;
			K_DrawCPUTagForPlayer(x, y, p, flags);
			break;*/

		case PLAYERTAG_NAME:
			flags |= foreground ? 0 : V_60TRANS;
			K_DrawNameTagForPlayer(x, y, p, flags);
			break;

		default:
			break;
	}

	K_DrawTypingNotifier(x, y, p, flags);
}

typedef struct weakspotdraw_t
{
	uint8_t i;
	int32_t x;
	int32_t y;
	dboolean candrawtag;
} weakspotdraw_t;

static void K_DrawWeakSpot(weakspotdraw_t *ws)
{
	uint8_t *colormap;
	uint8_t j = (bossinfo.weakspots[ws->i].type == SPOT_BUMP) ? 1 : 0;
	tic_t flashtime = ~1; // arbitrary high even number

	if (bossinfo.weakspots[ws->i].time < TICRATE)
	{
		if (bossinfo.weakspots[ws->i].time & 1)
			return;

		flashtime = bossinfo.weakspots[ws->i].time;
	}
	else if (bossinfo.weakspots[ws->i].time > (WEAKSPOTANIMTIME - TICRATE))
		flashtime = WEAKSPOTANIMTIME - bossinfo.weakspots[ws->i].time;

	if (flashtime & 1)
		colormap = R_GetTranslationColormap(TC_ALLWHITE, 0, GTC_CACHE);
	else
		colormap = R_GetTranslationColormap(TC_RAINBOW, bossinfo.weakspots[ws->i].color, GTC_CACHE);

	V_DrawFixedPatch(ws->x, ws->y, FRACUNIT, 0, kp_bossret[j], colormap);

	if (!ws->candrawtag || flashtime & 1 || flashtime < TICRATE/2)
		return;

	V_DrawFixedPatch(ws->x, ws->y, FRACUNIT, 0, kp_bossret[j+1], colormap);
}

static void K_drawKartNameTags(void)
{
	vector3_t c;
	const uint8_t cnum = R_GetViewNumber();
	size_t i, j;

	if (stplyr == NULL || stplyr->mo == NULL || P_MobjWasRemoved(stplyr->mo))
	{
		return;
	}

	if (stplyr->awayviewtics)
	{
		return;
	}

	// Crop within splitscreen bounds
	V_SetClipRectForPlayer(cnum);

	c.x = viewx;
	c.y = viewy;
	c.z = viewz;

	// Maybe shouldn't be handling this here... but the camera info is too good.
	if (bossinfo.boss)
	{
		weakspotdraw_t weakspotdraw[NUMWEAKSPOTS];
		uint8_t numdraw = 0;
		dboolean onleft = false;

		for (i = 0; i < NUMWEAKSPOTS; i++)
		{
			trackingResult_t result;
			vector3_t v;

			if (bossinfo.weakspots[i].spot == NULL || P_MobjWasRemoved(bossinfo.weakspots[i].spot))
			{
				// No object
				continue;
			}

			if (bossinfo.weakspots[i].time == 0 || bossinfo.weakspots[i].type == SPOT_NONE)
			{
				// not visible
				continue;
			}

			v.x = R_InterpolateFixed(bossinfo.weakspots[i].spot->old_x, bossinfo.weakspots[i].spot->x);
			v.y = R_InterpolateFixed(bossinfo.weakspots[i].spot->old_y, bossinfo.weakspots[i].spot->y);
			v.z = R_InterpolateFixed(bossinfo.weakspots[i].spot->old_z, bossinfo.weakspots[i].spot->z);

			v.z += (bossinfo.weakspots[i].spot->height / 2);

			K_ObjectTracking(&result, &v, false);
			if (result.onScreen == false)
			{
				continue;
			}

			weakspotdraw[numdraw].i = i;
			weakspotdraw[numdraw].x = result.x;
			weakspotdraw[numdraw].y = result.y;
			weakspotdraw[numdraw].candrawtag = true;

			for (j = 0; j < numdraw; j++)
			{
				if (abs(weakspotdraw[j].x - weakspotdraw[numdraw].x) > 50*FRACUNIT)
				{
					continue;
				}

				onleft = (weakspotdraw[j].x < weakspotdraw[numdraw].x);

				if (abs((onleft ? -5 : 5)
					+ weakspotdraw[j].y - weakspotdraw[numdraw].y) > 18*FRACUNIT)
				{
					continue;
				}

				if (weakspotdraw[j].x < weakspotdraw[numdraw].x)
				{
					weakspotdraw[j].candrawtag = false;
					break;
				}

				weakspotdraw[numdraw].candrawtag = false;
				break;
			}

			numdraw++;
		}

		for (i = 0; i < numdraw; i++)
		{
			K_DrawWeakSpot(&weakspotdraw[i]);
		}
	}

	K_drawTargetHUD(&c, stplyr);

	V_ClearClipRect();
}

#define MINIHEADSCALE (FRACUNIT / 2)

static inline void K_drawKartMinimapIcon(fixed_t objx, fixed_t objy, int32_t hudx, int32_t hudy, int32_t flags, patch_t *icon, uint8_t *colormap, vector2_t *origin)
{
	// amnum xpos & ypos are the icon's speed around the HUD.
	// The number being divided by is for how fast it moves.
	// The higher the number, the slower it moves.

	// am xpos & ypos are the icon's starting position. Withouht
	// it, they wouldn't 'spawn' on the top-right side of the HUD.

	fixed_t amnumxpos, amnumypos;
	int32_t amxpos, amypos;
	fixed_t scale = FRACUNIT;

	int16_t w, h;

	if (origin)
	{
		w = origin->x * 2;
		h = origin->y * 2;
	}
	else
	{
		w = icon->width;
		h = icon->height;
	}


	if (!cv_showminimapangle.value && (icon == kp_minimapdot))
		return;

	if (cv_minihead.value)
		scale = MINIHEADSCALE;

	amnumxpos = (FixedMul(objx, minimapinfo.zoom) - minimapinfo.offs_x);
	amnumypos = -(FixedMul(objy, minimapinfo.zoom) - minimapinfo.offs_y);

	if (encoremode)
		amnumxpos = -amnumxpos;

	amxpos = amnumxpos + ((hudx + (minimapinfo.minimap_pic->width-w)/2)<<FRACBITS);
	amypos = amnumypos + ((hudy + (minimapinfo.minimap_pic->height-h)/2)<<FRACBITS);

	if (cv_minihead.value && (!(icon == kp_minimapdot)) && (!origin))
	{
		amxpos += 2 * FRACUNIT;
		amypos += 2 * FRACUNIT;
	}

	if (cv_minihead.value && (icon == kp_wantedreticle))
	{
		amxpos += 2 * FRACUNIT;
		amypos += 2 * FRACUNIT;
	}

	V_DrawFixedPatch(amxpos, amypos, scale, flags, icon, colormap);
}

static void K_drawKartMinimapHeadlight(fixed_t objx, fixed_t objy, int32_t hudx, int32_t hudy, int32_t flags, angle_t ang, uint8_t *colormap)
{
	// amnum xpos & ypos are the icon's speed around the HUD.
	// The number being divided by is for how fast it moves.
	// The higher the number, the slower it moves.

	// am xpos & ypos are the icon's starting position. Withouht
	// it, they wouldn't 'spawn' on the top-right side of the HUD.

	fixed_t amnumxpos, amnumypos;
	int32_t amxpos, amypos;
	fixed_t scale = FRACUNIT;
	patch_t *icon = W_CachePatchNameRotated("MMAPHDLT", R_GetRollAngle(ang), PU_PATCH);

	amnumxpos = (FixedMul(objx, minimapinfo.zoom) - minimapinfo.offs_x);
	amnumypos = -(FixedMul(objy, minimapinfo.zoom) - minimapinfo.offs_y);

	if (encoremode)
		amnumxpos = -amnumxpos;

	amxpos = amnumxpos + ((hudx + (minimapinfo.minimap_pic->width-48)/2)<<FRACBITS);
	amypos = amnumypos + ((hudy + (minimapinfo.minimap_pic->height-24)/2)<<FRACBITS);

	V_DrawFixedPatch(amxpos, amypos, scale, flags|V_ADD, icon, colormap);
}

static void K_drawKartMinimapNametag(fixed_t objx, fixed_t objy, int32_t hudx, int32_t hudy, int32_t flags, player_t *player)
{
	// amnum xpos & ypos are the icon's speed around the HUD.
	// The number being divided by is for how fast it moves.
	// The higher the number, the slower it moves.

	// am xpos & ypos are the icon's starting position. Withouht
	// it, they wouldn't 'spawn' on the top-right side of the HUD.

	if (!cv_showminimapnames.value)
	{
		return;
	}

	if (!player)
	{
		return;
	}

	if (!player->mo)
	{
		return;
	}

	fixed_t amnumxpos, amnumypos;
	int32_t amxpos, amypos;
	uint16_t skin = 0;
	uint16_t chatcolor = skincolors[player->mo->color].chatcolor;

	amnumxpos = (FixedMul(objx, minimapinfo.zoom) - minimapinfo.offs_x);
	amnumypos = -(FixedMul(objy, minimapinfo.zoom) - minimapinfo.offs_y);

	if (encoremode)
		amnumxpos = -amnumxpos;

	skin = ((skin_t*)player->mo->skin)->skinnum;

	amxpos = amnumxpos + ((hudx + (minimapinfo.minimap_pic->width-faceprefix[skin][FACE_MINIMAP]->width)/2)<<FRACBITS);
	amypos = amnumypos + ((hudy + (minimapinfo.minimap_pic->height-faceprefix[skin][FACE_MINIMAP]->height)/2)<<FRACBITS);

	const char *player_name = va("%s",player_names[player - players]);
	V_DrawCenteredSmallStringAtFixed(amxpos + (6*FRACUNIT), amypos - (5*FRACUNIT), V_ALLOWLOWERCASE|flags|chatcolor, player_name);
}

static inline void K_drawKartMinimapDot(fixed_t objx, fixed_t objy, int32_t hudx, int32_t hudy, int32_t flags, uint8_t color, uint8_t intsize)
{
	fixed_t amnumxpos, amnumypos;
	fixed_t size = intsize * FRACUNIT;

	amnumxpos = (FixedMul(objx, minimapinfo.zoom) - minimapinfo.offs_x);
	amnumypos = -(FixedMul(objy, minimapinfo.zoom) - minimapinfo.offs_y);

	if (encoremode)
		amnumxpos = -amnumxpos;

	amnumxpos += minimapinfo.minimap_pic->width*FRACUNIT/2;
	amnumypos += minimapinfo.minimap_pic->height*FRACUNIT/2;

	V_DrawFixedFill((amnumxpos + hudx*FRACUNIT) - (size / 2), (amnumypos + hudy*FRACUNIT) - (size / 2), size, size, flags | color);
}

static void K_drawKartMinimapWaypoint(waypoint_t *wp, int32_t hudx, int32_t hudy, int32_t flags)
{
	uint8_t pal = 0x95; // blue
	uint8_t size = 3;

	if (wp == stplyr->nextwaypoint)
	{
		pal = 0x70; // green
		size = 6;
	}
	else if (K_GetWaypointIsShortcut(wp)) // shortcut
	{
		pal = 0x20; // pink
	}
	else if (!K_GetWaypointIsEnabled(wp)) // disabled
	{
		pal = 0x10; // gray
	}
	else if (wp->numnextwaypoints == 0 || wp->numprevwaypoints == 0)
	{
		pal = 0x40; // yellow
	}

	K_drawKartMinimapDot(wp->mobj->x, wp->mobj->y, hudx, hudy, flags, pal, size);
}

static void K_drawKartMinimapRings(mobj_t *mobj, int32_t hudx, int32_t hudy, int32_t flags)
{
	uint8_t pal = 0x40; // yellow
	uint8_t size = 1;

	K_drawKartMinimapDot(mobj->x, mobj->y, hudx, hudy, flags, pal, size);
}

static void K_drawKartMinimapCluster(int32_t hudx, int32_t hudy, int32_t flags)
{
	uint8_t pal = 180; // Strong pink color.
	uint8_t pal2 = 0x95; // Miscellaneous clusters get a blue color (FIXME: rainbow-color by ID?)
	uint8_t size = 6;
	uint8_t size2 = 3;

	fixed_t clusterx, clustery;

	clusterx = clustery = 0;

	if (num_player_clusters)
	{
		// More than just the main cluster is in this race!

		int32_t i = 0;
		for (i = 0; i < num_player_clusters; i++)
		{
			K_drawKartMinimapDot(player_clusters[i].nucleus.x, player_clusters[i].nucleus.y, hudx, hudy, flags, pal2, size2);
		}
	}

	if ((clusterid != UINT32_MAX) && (players[clusterid].mo) && (!P_MobjWasRemoved(players[clusterid].mo)))
	{
		clusterx = players[clusterid].mo->x;
		clustery = players[clusterid].mo->y;
	}
	else
	{
		clusterx = clusterpoint.x;
		clustery = clusterpoint.y;
	}

	K_drawKartMinimapDot(clusterx, clustery, hudx, hudy, flags, pal, size);
}

#define ICON_DOT_RADIUS (cv_minihead.value && !cv_showminimapnames.value) ? 8 : 10

typedef struct
{
	dboolean draw;
	dboolean red;
} spbdraw_t;

static spbdraw_t K_ShouldDrawSPB(mobj_t *mobj)
{
	fixed_t dist;
	spbdraw_t spbdraw;

	spbdraw.draw = false;
	spbdraw.red = false;

	if (!mobj->tracer)
	{
		spbdraw.draw = true;
		return spbdraw;
	}

	dist = FixedHypot(mobj->x-mobj->tracer->x, mobj->y-mobj->tracer->y)/mobj->tracer->scale;

	if (dist < 3072)
	{
		spbdraw.draw = (dist > 1024);
		spbdraw.red  = (leveltime/2 % 2 == 0);
		return spbdraw;
	}

	spbdraw.draw = true;
	return spbdraw;
}

#define TICTOANGLE(t) (FixedAngle(((360 * FRACUNIT / TICRATE) * t) % (360 * FRACUNIT)))

static void K_drawKartMinimap(void)
{
	patch_t *workingPic;
	int32_t i = 0;
	int32_t x, y;
	int32_t minimaptrans = cv_kartminimap.value;
	int32_t splitflags = 0;
	uint16_t skin = 0;
	uint8_t *colormap = NULL;
	skincolornum_t minimapcolor;
	int8_t localplayers[MAXSPLITSCREENPLAYERS];
	int8_t numlocalplayers = 0;
	mobj_t *mobj, *next;	// for SPB drawing (or any other item(s) we may wanna draw, I dunno!)
	fixed_t interpx, interpy;
	spbdraw_t spb;
	uint16_t usecolor;
	dboolean colorizeplayer;
	fixed_t smntrgradient = 0;

#ifdef ROTSPRITE
	angle_t rollangle = 0;
	int32_t rot = 0;
	int32_t sparkleflags;
	patch_t *rotsparkle;
	dboolean halftrans = false;
	fixed_t transmul = 0;
	uint32_t smonitortrans = 0;
#endif

	vector2_t iconoffsets;
	int32_t widthhalf, heighthalf;
	int32_t adjustx, adjusty;

	// Draw the HUD only when playing in a level.
	// hu_stuff needs this, unlike st_stuff.
	if (gamestate != GS_LEVEL)
		return;

	// Only draw for the first player
	// Maybe move this somewhere else where this won't be a concern?
	if (stplyrnum != 0)
		return;

	if (minimapinfo.minimap_pic == NULL)
	{
		return; // no pic, just get outta here
	}

	iconoffsets.x = 0;
	iconoffsets.y = 0;

	adjustx = adjusty = 0;
	widthhalf = heighthalf = 0;

	drawinfo_t info;
	K_getMinimapDrawinfo(&info);
	x = info.x;
	y = info.y;
	splitflags = info.flags;

	if (r_splitscreen < 2) // 1/2P right aligned
	{
		const tic_t length = TICRATE/2;

		if (!lt_exitticker)
			return;
		if (lt_exitticker < length)
			minimaptrans = (((int32_t)lt_exitticker)*minimaptrans)/((int32_t)length);

	}
	else if (r_splitscreen == 3) // 4P centered
	{
		const tic_t length = TICRATE/2;

		if (!lt_exitticker)
			return;
		if (lt_exitticker < length)
			minimaptrans = (((int32_t)lt_exitticker)*minimaptrans)/((int32_t)length);
	}
	// 3P lives in the middle of the bottom right player and shouldn't fade in OR slide

	if (!minimaptrans)
		return;

	colorizeplayer = false;

	minimaptrans = ((10-minimaptrans)<<FF_TRANSSHIFT);


	uint8_t bestplayer = MAXPLAYERS;
	if (mapnamespace != MNS_SRB2KART)
	{
		// Really looking forward to never writing this loop again
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;
			if (players[i].spectator)
				continue;
			if (players[i].position == 1)
				bestplayer = i;
		}
	}

	if (bestplayer == MAXPLAYERS || leveltime < starttime) // Countdown / no players
		minimapcolor = SKINCOLOR_WHITE;
	else if (players[bestplayer].laps >= numlaps) // Final lap
		minimapcolor = K_RainbowColor(leveltime);
	else // Standard: color to leader
		minimapcolor =  players[bestplayer].skincolor;

	if (encoremode)
		V_DrawFixedPatch((x+minimapinfo.minimap_pic->width) * FRACUNIT,
			(y)* FRACUNIT,
			FRACUNIT,
			splitflags|minimaptrans|V_FLIP,
			minimapinfo.minimap_pic,
			mapnamespace == MNS_SRB2KART ? NULL : R_GetTranslationColormap(TC_DEFAULT, minimapcolor, GTC_CACHE));
	else
		V_DrawFixedPatch((x) * FRACUNIT,
			(y) * FRACUNIT,
			FRACUNIT,
			splitflags|minimaptrans,
			minimapinfo.minimap_pic,
			mapnamespace == MNS_SRB2KART ? NULL : R_GetTranslationColormap(TC_DEFAULT, minimapcolor, GTC_CACHE));

	// let offsets transfer to the heads, too!
	if (encoremode)
		x += LSBF_SHORT(minimapinfo.minimap_pic->leftoffset);
	else
		x -= LSBF_SHORT(minimapinfo.minimap_pic->leftoffset);
	y -= LSBF_SHORT(minimapinfo.minimap_pic->topoffset);

	// initialize
	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
		localplayers[i] = -1;

	// Player's tiny icons on the Automap. (drawn opposite direction so player 1 is drawn last in splitscreen)
	if (ghosts)
	{
		demoghost *g = ghosts;
		while (g)
		{
			if (g->mo->skin)
				skin = ((skin_t*)g->mo->skin)->skinnum;
			else
				skin = 0;
			if (g->mo->color)
			{
				if (g->mo->colorized)
					colormap = R_GetTranslationColormap(TC_RAINBOW, g->mo->color, GTC_CACHE);
				else
					colormap = R_GetTranslationColormap(skin, g->mo->color, GTC_CACHE);
			}
			else
				colormap = NULL;

			interpx = R_InterpolateFixed(g->mo->old_x, g->mo->x);
			interpy = R_InterpolateFixed(g->mo->old_y, g->mo->y);

			patch_t *ghostPic = faceprefix[skin][FACE_MINIMAP];

			widthhalf = ((ghostPic->width) / 2);
			heighthalf = ((ghostPic->height) / 2);

			if (cv_minihead.value)
			{
				adjustx = FixedMul(4, (widthhalf - ghostPic->leftoffset) * FRACUNIT / widthhalf);
				adjusty = FixedMul(4, (heighthalf - ghostPic->topoffset) *
						  FRACUNIT / heighthalf);
			}

			iconoffsets.x = widthhalf - ghostPic->leftoffset - adjustx;
			iconoffsets.y = heighthalf - ghostPic->topoffset - adjusty;

			K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, ghostPic, colormap, &iconoffsets);
			g = g->next;
		}

		if (!stplyr->mo || stplyr->spectator || (!cv_showfinishedplayers.value && stplyr->exiting))
			return;

		localplayers[numlocalplayers++] = stplyr-players;
	}
	else
	{
		for (i = MAXPLAYERS-1; i >= 0; i--)
		{
			if (!playeringame[i])
				continue;
			if (!players[i].mo || players[i].spectator || !players[i].mo->skin || (!cv_showfinishedplayers.value && players[i].exiting))
				continue;

			if (i == displayplayers[0] || i == displayplayers[1] || i == displayplayers[2] || i == displayplayers[3])
			{
				// Draw display players on top of everything else
				localplayers[numlocalplayers++] = i;
				continue;
			}

			// Now we know it's not a display player, handle non-local player exceptions.
			if ((gametypes[gametype]->rules & GTR_BUMPERS) && players[i].bumper <= 0)
				continue;

			if (players[i].hyudorotimer > 0)
			{
				if (!((players[i].hyudorotimer < TICRATE/2
					|| players[i].hyudorotimer > hyudorotime-(TICRATE/2))
					&& !(leveltime & 1)))
					continue;
			}

			mobj = players[i].mo;

			if (mobj->health <= 0 && (players[i].pflags & PF_NOCONTEST))
			{
				if (P_MobjWasRemoved(mobj->tracer))
				{
					continue;
				}

				if (mobj->tracer->renderflags & RF_DONTDRAW)
				{
					continue;
				}

				workingPic = kp_nocontestminimap;

				widthhalf = ((workingPic->width) / 2);
				heighthalf = ((workingPic->height) / 2);

				if (cv_minihead.value)
				{
					adjustx = FixedMul(4, (widthhalf - workingPic->leftoffset) * FRACUNIT / widthhalf);
					adjusty = FixedMul(4, (heighthalf - workingPic->topoffset) *
							FRACUNIT / heighthalf);
				}

				iconoffsets.x = widthhalf - workingPic->leftoffset - adjustx;
				iconoffsets.y = heighthalf - workingPic->topoffset - adjusty;

				colormap = R_GetTranslationColormap(TC_DEFAULT, mobj->color, GTC_CACHE);

				if (mobj->tracer && !P_MobjWasRemoved(mobj->tracer))
					mobj = mobj->tracer;
			}
			else
			{
				skin = ((skin_t*)mobj->skin)->skinnum;

				workingPic = faceprefix[skin][FACE_MINIMAP];

				widthhalf = ((workingPic->width) / 2);
				heighthalf = ((workingPic->height) / 2);

				if (cv_minihead.value)
				{
					adjustx = FixedMul(4, (widthhalf - workingPic->leftoffset) *
							  FRACUNIT / widthhalf);
					adjusty = FixedMul(4, (heighthalf - workingPic->topoffset) *
							  FRACUNIT / heighthalf);
				}

				iconoffsets.x = widthhalf - workingPic->leftoffset - adjustx;
				iconoffsets.y = heighthalf - workingPic->topoffset - adjusty;

#ifdef ROTSPRITE
				if ((K_MinimapIconCanSpinout()) && (players[i].spinoutrot))
				{
					// Rotate counterclockwise.
					rollangle = FixedAngle(players[i].spinoutrot * -1);
					rot = R_GetRollAngle(rollangle);

					if (rot)
					{
						workingPic = Patch_GetRotated(workingPic, rot, false);
					}
				}
#endif

				colorizeplayer = mobj->colorized;

				smntrgradient = K_SMonitorGradient(players[i].smonitortimer);

				if (players[i].invincibilitytimer)
				{
					usecolor = (K_RainbowColor(leveltime / 2));
					colorizeplayer = true;
				}
				else if ((players[i].smonitortimer) && (smntrgradient > (FRACUNIT/2)))
				{
					usecolor = (K_SMonitorColor(leveltime / 2));
					colorizeplayer = true;
				}
				else
				{
					usecolor = mobj->color;
				}

				if (usecolor)
				{
					if (colorizeplayer)
						colormap = R_GetTranslationColormap(TC_RAINBOW, usecolor, GTC_CACHE);
					else
						colormap = R_GetTranslationColormap(skin, usecolor, GTC_CACHE);
				}
				else
					colormap = NULL;
			}

			//if (doprogressionbar == false)
			{
				// draw external players transparent
				if (mobj->player)
				{
					splitflags &= ~V_HUDTRANS;
					splitflags |= V_HUDTRANSHALF;
				}
				interpx = R_InterpolateFixed(mobj->old_x, mobj->x);
				interpy = R_InterpolateFixed(mobj->old_y, mobj->y);

				K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, workingPic, colormap, &iconoffsets);

#ifdef ROTSPRITE
				if ((players[i].smonitortimer) && (smntrgradient))
				{
					// Draw S-Monitor sparkles
					halftrans =
					    ((splitflags & V_HUDTRANSHALF) == V_HUDTRANSHALF);
					transmul = 0;
					smonitortrans = 0;

					sparkleflags =
					    splitflags & (~(V_HUDTRANS | V_HUDTRANSHALF));

					if (halftrans)
					{
						transmul = FRACUNIT -
							   (V_GetHudTransHalf() * FRACUNIT / 10);
					}
					else
					{
						transmul =
						    FRACUNIT - (V_GetHudTrans() * FRACUNIT / 10);
					}

					smonitortrans =
					    max(0,
						min(9,
						    10 - FixedMul(FixedMul(10, smntrgradient),
								  transmul)))
					    << V_ALPHASHIFT;
					sparkleflags |= smonitortrans;

					if (kp_smonitorsparkle)
					{
						rot = R_GetRollAngle(-TICTOANGLE(leveltime));

						if (rot)
						{
							rotsparkle = Patch_GetRotated(
							    kp_smonitorsparkle, rot, false);
						}
						else
						{
							rotsparkle = kp_smonitorsparkle;
						}

						if (rotsparkle)
						{
							widthhalf = ((kp_smonitorsparkle->width) / 2);
							heighthalf = ((kp_smonitorsparkle->height) / 2);

							if (cv_minihead.value)
							{
								adjustx = FixedMul(
								    4,
								    (widthhalf -
								     kp_smonitorsparkle->leftoffset) *
									FRACUNIT / widthhalf);
								adjusty = FixedMul(
								    4,
								    (heighthalf -
								     kp_smonitorsparkle->topoffset) *
									FRACUNIT / heighthalf);
							}

							iconoffsets.x = widthhalf -
									kp_smonitorsparkle->leftoffset -
									adjustx;
							iconoffsets.y = heighthalf -
									kp_smonitorsparkle->topoffset -
									adjusty;

							K_drawKartMinimapIcon(interpx,
									      interpy,
									      x,
									      y,
									      sparkleflags | V_ADD,
									      rotsparkle,
									      colormap,
									      &iconoffsets);
						}
					}
				}
#endif

				if (mobj->player)
				{
					// Draw the Nametag
					K_drawKartMinimapNametag(interpx, interpy, x, y, splitflags, &players[i]);
				}

				// Target reticule
				if ((gametype == GT_RACE && players[i].position == spbplace)
					|| ((gametypes[gametype]->rules & GTR_WANTED) && K_IsPlayerWanted(&players[i])))
				{
					K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, kp_wantedreticle, NULL, NULL);
				}
			}
		}
	}

	// draw minimap-pertinent objects
	for (mobj = kitemcap; mobj; mobj = next)
	{
		workingPic = NULL;
		colormap = NULL;
		next = mobj->itnext;

		if (mobj->health <= 0)
			continue;

		switch (mobj->type)
		{
			case MT_SPB:
				spb = K_ShouldDrawSPB(mobj);

				if (spb.draw)
				{
					int32_t tc = spb.red ? TC_BLINK : TC_DEFAULT;
					skincolornum_t color = SKINCOLOR_RED;

					// If not flashing red then...
					if (!spb.red && mobj->color != SKINCOLOR_NONE)
					{
						// Use SPB's current color.
						color = mobj->color;
					}

					workingPic = kp_splitkarmabomb;
					colormap = R_GetTranslationColormap(tc, color, GTC_CACHE);
				}
				else
				{
					workingPic = NULL;
				}
				break;
			default:
				break;
		}

		if (!workingPic)
			continue;

		interpx = R_InterpolateFixed(mobj->old_x, mobj->x);
		interpy = R_InterpolateFixed(mobj->old_y, mobj->y);

		K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, workingPic, colormap, NULL);
	}

	for (mobj = misccap; mobj; mobj = next)
	{
		workingPic = NULL;
		colormap = NULL;
		next = mobj->itnext;

		if (mobj->health <= 0)
			continue;

		switch (mobj->type)
		{
			case MT_RANDOMITEM:
				if (itembreaker && (mobj->flags2 & MF2_BOSSNOTRAP) && !(mobj->flags2 & MF2_BOSSFLEE))
				{
					workingPic = kp_itemboxminimap;
					colormap = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_YELLOW, GTC_CACHE);
				}
				else
				{
					workingPic = NULL;
				}
				break;
			default:
				break;
		}

		if (!workingPic)
			continue;

		interpx = R_InterpolateFixed(mobj->old_x, mobj->x);
		interpy = R_InterpolateFixed(mobj->old_y, mobj->y);

		K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, workingPic, colormap, NULL);
	}

	// draw our local players here, opaque.
	{
		splitflags &= ~V_HUDTRANSHALF;
		splitflags |= V_HUDTRANS;
	}

	// ...but first, any boss targets.
	if (bossinfo.boss)
	{
		for (i = 0; i < NUMWEAKSPOTS; i++)
		{
			// exists at all?
			if (bossinfo.weakspots[i].spot == NULL || P_MobjWasRemoved(bossinfo.weakspots[i].spot))
				continue;
			// shows on the minimap?
			if (bossinfo.weakspots[i].minimap == false)
				continue;
			// in the flashing period?
			if ((bossinfo.weakspots[i].time > (WEAKSPOTANIMTIME-(TICRATE/2))) && (bossinfo.weakspots[i].time & 1))
				continue;

			colormap = NULL;

			if (bossinfo.weakspots[i].color)
				colormap = R_GetTranslationColormap(TC_RAINBOW, bossinfo.weakspots[i].color, GTC_CACHE);

			interpx = R_InterpolateFixed(bossinfo.weakspots[i].spot->old_x, bossinfo.weakspots[i].spot->x);
			interpy = R_InterpolateFixed(bossinfo.weakspots[i].spot->old_y, bossinfo.weakspots[i].spot->y);

			// temporary graphic?
			K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, kp_wantedreticle, colormap, NULL);
		}
	}

	for (i = 0; i < numlocalplayers; i++)
	{
		dboolean nocontest = false;

		if (localplayers[i] == -1)
			continue; // this doesn't interest us

		if ((players[localplayers[i]].hyudorotimer > 0) && (leveltime & 1))
			continue;

		mobj = players[localplayers[i]].mo;

		if (mobj->health <= 0 && (players[localplayers[i]].pflags & PF_NOCONTEST))
		{
			if (P_MobjWasRemoved(mobj->tracer))
			{
				continue;
			}

			if (mobj->tracer->renderflags & RF_DONTDRAW)
			{
				continue;
			}

			workingPic = kp_nocontestminimap;

			widthhalf = ((workingPic->width) / 2);
			heighthalf = ((workingPic->height) / 2);

			if (cv_minihead.value)
			{
				adjustx = FixedMul(4, (widthhalf - workingPic->leftoffset) * FRACUNIT / widthhalf);
				adjusty = FixedMul(4, (heighthalf - workingPic->topoffset) *
						  FRACUNIT / heighthalf);
			}

			iconoffsets.x = widthhalf - workingPic->leftoffset - adjustx;
			iconoffsets.y = heighthalf - workingPic->topoffset - adjusty;

			colormap = R_GetTranslationColormap(TC_DEFAULT, mobj->color, GTC_CACHE);

			nocontest = true;

			if (mobj->tracer && !P_MobjWasRemoved(mobj->tracer))
				mobj = mobj->tracer;
		}
		else
		{
			skin = ((skin_t*)mobj->skin)->skinnum;

			workingPic = faceprefix[skin][FACE_MINIMAP];

			widthhalf = ((workingPic->width) / 2);
			heighthalf = ((workingPic->height) / 2);

			if (cv_minihead.value)
			{
				adjustx = FixedMul(4, (widthhalf - workingPic->leftoffset) * FRACUNIT / widthhalf);
				adjusty = FixedMul(4, (heighthalf - workingPic->topoffset) *
						  FRACUNIT / heighthalf);
			}

			iconoffsets.x = widthhalf - workingPic->leftoffset - adjustx;
			iconoffsets.y = heighthalf - workingPic->topoffset - adjusty;

#ifdef ROTSPRITE
			if ((K_MinimapIconCanSpinout()) && (players[localplayers[i]].spinoutrot))
			{
				// Rotate counterclockwise.
				rollangle = FixedAngle(players[localplayers[i]].spinoutrot * -1);
				rot = R_GetRollAngle(rollangle);

				if (rot)
				{
					workingPic = Patch_GetRotated(workingPic, rot, false);
				}
			}
#endif

			colorizeplayer = mobj->colorized;
			smntrgradient =
				K_SMonitorGradient(
					players[localplayers[i]].smonitortimer);

			if (players[localplayers[i]].invincibilitytimer)
			{
				usecolor = (K_RainbowColor(leveltime / 2));
				colorizeplayer = true;
			}
			else if ((players[localplayers[i]].smonitortimer)
				&& ((smntrgradient > (FRACUNIT / 2))))
			{
				usecolor = (K_SMonitorColor(leveltime / 2));
				colorizeplayer = true;
			}
			else
			{
				usecolor = mobj->color;
			}

			if (usecolor)
			{
				if (colorizeplayer)
					colormap = R_GetTranslationColormap(TC_RAINBOW, usecolor, GTC_CACHE);
				else
					colormap = R_GetTranslationColormap(skin, usecolor, GTC_CACHE);
			}
			else
				colormap = NULL;
		}

		//if (doprogressionbar == false)
		{
			angle_t ang = R_InterpolateAngle(mobj->old_angle, mobj->angle);

			if (encoremode)
				ang = ANGLE_180 - ang;

			interpx = R_InterpolateFixed(mobj->old_x, mobj->x);
			interpy = R_InterpolateFixed(mobj->old_y, mobj->y);

			if (!nocontest)
			{
				uint8_t *misccolormap = R_GetTranslationColormap(TC_DEFAULT, mobj->color, GTC_CACHE);

				if (cv_showminimapangle.value == 1)
				{
					K_drawKartMinimapIcon(
							interpx,
							interpy,
							x + FixedMul(FCOS(ang), ICON_DOT_RADIUS),
							y - FixedMul(FSIN(ang), ICON_DOT_RADIUS),
							splitflags,
							kp_minimapdot,
							misccolormap,
							NULL
					);
				}
				else if (cv_showminimapangle.value == 2)
				{
					K_drawKartMinimapHeadlight(
						interpx,
						interpy,
						x,
						y,
						splitflags,
						ang,
						misccolormap
					);
				}
			}

			K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, workingPic, colormap, &iconoffsets);

#ifdef ROTSPRITE
			if ((!nocontest) && (players[localplayers[i]].smonitortimer) && (smntrgradient))
			{
				// Draw S-Monitor sparkles
				halftrans = ((splitflags & V_HUDTRANSHALF) == V_HUDTRANSHALF);
				transmul = 0;
				smonitortrans = 0;

				sparkleflags = splitflags & (~(V_HUDTRANS | V_HUDTRANSHALF));

				if (halftrans)
				{
					transmul = FRACUNIT - (V_GetHudTransHalf() * FRACUNIT / 10);
				}
				else
				{
					transmul = FRACUNIT - (V_GetHudTrans() * FRACUNIT / 10);
				}

				transmul *= 2;

				smonitortrans =
				    max(0,
					min(9,
					    10 - FixedMul(FixedMul(10, smntrgradient), transmul)))
				    << V_ALPHASHIFT;
				sparkleflags |= smonitortrans;

				if (kp_smonitorsparkle)
				{
					rot = R_GetRollAngle(-TICTOANGLE(leveltime));

					if (rot)
					{
						rotsparkle = Patch_GetRotated(
						    kp_smonitorsparkle, rot, false);
					}
					else
					{
						rotsparkle = kp_smonitorsparkle;
					}

					if (rotsparkle)
					{
						widthhalf = ((kp_smonitorsparkle->width) / 2);
						heighthalf = ((kp_smonitorsparkle->height) / 2);

						if (cv_minihead.value)
						{
							adjustx = FixedMul(
							    4,
							    (widthhalf - kp_smonitorsparkle->leftoffset) *
								FRACUNIT / widthhalf);
							adjusty = FixedMul(
							    4,
							    (heighthalf - kp_smonitorsparkle->topoffset) *
								FRACUNIT / heighthalf);
						}

						iconoffsets.x =
						    widthhalf - kp_smonitorsparkle->leftoffset - adjustx;
						iconoffsets.y =
						    heighthalf - kp_smonitorsparkle->topoffset - adjusty;

						K_drawKartMinimapIcon(interpx,
								      interpy,
								      x,
								      y,
								      sparkleflags | V_ADD,
								      rotsparkle,
								      colormap,
								      &iconoffsets);
					}
				}
			}
#endif

			// Target reticule
			if ((gametype == GT_RACE && players[localplayers[i]].position == spbplace)
				|| ((gametypes[gametype]->rules & GTR_WANTED) && K_IsPlayerWanted(&players[localplayers[i]])))
			{
				K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, kp_wantedreticle, NULL, NULL);
			}

			K_drawKartMinimapNametag(interpx, interpy, x, y, splitflags, mobj->player);
		}
	}

	if (cv_kartdebugwaypoints.value != 0)
	{
		if (!(gametypes[gametype]->rules & GTR_CIRCUIT))
			return;

		if (waypointcap == NULL)
			return;

		size_t idx;

		for (idx = 0; idx < K_GetNumWaypoints(); ++idx)
		{
			waypoint_t *wp = K_GetWaypointFromIndex(idx);

			I_Assert(wp != NULL);

			K_drawKartMinimapWaypoint(wp, x, y, splitflags);
		}

		if (stplyr->nextwaypoint != NULL)
		{
			// should be drawn on top of the others
			K_drawKartMinimapWaypoint(stplyr->nextwaypoint, x, y, splitflags);
		}
	}

	if (cv_kartdebugrings.value != 0)
	{
		for (mobj = misccap; mobj; mobj = next)
		{
			next = mobj->itnext;

			if (mobj->type != MT_RING && mobj->type != MT_FLINGRING)
				continue;

			K_drawKartMinimapRings(mobj, x, y, splitflags);
		}
	}

	if (cv_kartdebugcluster.value != 0)
	{
		K_drawKartMinimapCluster(x, y, splitflags);
	}
}

static void K_drawKartFinish(void)
{
	int32_t timer, minsplitstationary, pnum = 0, splitflags = V_SPLITSCREEN;
	patch_t **kptodraw;

	{
		timer = stplyr->karthud[khud_finish];
		kptodraw = kp_racefinish;
		minsplitstationary = 2;
	}

	if (!timer || timer > 2*TICRATE)
		return;

	if ((timer % (2*5)) / 5) // blink
		pnum = 1;

	if (r_splitscreen > 0)
		pnum += (r_splitscreen > 1) ? 2 : 4;

	if (r_splitscreen >= minsplitstationary) // 3/4p, stationary FIN
	{
		V_DrawScaledPatch(STCD_X - (LSBF_SHORT(kptodraw[pnum]->width)/2), STCD_Y - (LSBF_SHORT(kptodraw[pnum]->height)/2), splitflags, kptodraw[pnum]);
		return;
	}

	//else -- 1/2p, scrolling FINISH
	{
		int32_t x, xval, ox, interpx, pwidth;

		x = ((vid.width<<FRACBITS)/vid.dup);
		xval = (LSBF_SHORT(kptodraw[pnum]->width)<<FRACBITS);

		pwidth = max(xval, x);

		x = ((TICRATE - timer) * pwidth) / TICRATE;
		ox = ((TICRATE - (timer - 1)) * pwidth) / TICRATE;

		interpx = R_InterpolateFixed(ox, x);

		if (r_splitscreen && stplyrnum == 1)
			interpx = -interpx;

		V_DrawFixedPatch(interpx + (STCD_X<<FRACBITS) - (pwidth / 2),
			(STCD_Y<<FRACBITS) - (LSBF_SHORT(kptodraw[pnum]->height)<<(FRACBITS-1)),
			FRACUNIT,
			splitflags, kptodraw[pnum], NULL);
	}
}

static void K_drawKartStartCountdown(void)
{
	int32_t pnum = 0;

	if (leveltime > starttime-(3*TICRATE))
	{

		if (leveltime >= starttime-(2*TICRATE)) // 2
			pnum++;
		if (leveltime >= starttime-TICRATE) // 1
			pnum++;

		if (leveltime >= starttime) // GO!
		{
			uint8_t i;
			uint8_t numplayers = 0;

			pnum++;

			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (playeringame[i] && !players[i].spectator)
					numplayers++;

				if (numplayers > 2)
					break;
			}

			// Re-enable when graphics are drawn up for this.
			/*if (numplayers == 2)
			{
				pnum++; // DUEL
			}*/
		}

		if ((leveltime % (2*5)) / 5) // blink
			pnum += 5;
		if (r_splitscreen) // splitscreen
			pnum += 10;

		V_DrawScaledPatch(STCD_X - (kp_startcountdown[pnum]->width/2), STCD_Y - (kp_startcountdown[pnum]->height/2), V_SPLITSCREEN, kp_startcountdown[pnum]);
	}
}

static void K_drawBattleFullscreen(void)
{
	int32_t cardanim = stplyr->karthud[khud_cardanimation] << FRACBITS;

	// fill in the fractional bits
	if (cardanim && cardanim != 164*FRACUNIT)
	{
		int32_t frac = R_GetTimeFrac(RTF_LEVEL) * ((164 - stplyr->karthud[khud_cardanimation])/8 + 1);
		if (stplyr->exiting)
			cardanim += frac;
		else
			cardanim += stplyr->karmadelay < 6*TICRATE ? -frac : frac;
	}

	int32_t x = BASEVIDWIDTH/2;
	int32_t y = (-64*FRACUNIT) + cardanim; // card animation goes from 0 to 164, 164 is the middle of the screen the screen
	int32_t splitflags = V_SNAPTOTOP; // I don't feel like properly supporting non-green resolutions, so you can have a misuse of SNAPTO instead
	fixed_t scale = FRACUNIT;
	dboolean drawcomebacktimer = true;	// lazy hack because it's cleaner in the long run.

	if (!LUA_HudEnabled(hud_battlecomebacktimer))
		drawcomebacktimer = false;

	if (r_splitscreen)
	{
		if ((splitscreen == 1 && stplyrnum == 1) || (splitscreen > 1 && stplyrnum & 2))
		{
			y = (232*FRACUNIT) - (cardanim/2);
			splitflags = V_SNAPTOBOTTOM;
		}
		else
			y = (-32*FRACUNIT) + (cardanim/2);

		if (r_splitscreen > 1)
		{
			scale /= 2;

			if (stplyrnum & 1)
				x = 3*BASEVIDWIDTH/4;
			else
				x = BASEVIDWIDTH/4;
		}
		else
		{
			if (stplyr->exiting)
			{
				if (stplyrnum & 1)
					x = BASEVIDWIDTH-96;
				else
					x = 96;
			}
			else
				scale /= 2;
		}
	}

	if (stplyr->exiting)
	{
		if (stplyrnum == 0)
			V_DrawFadeScreen(0xFF00, 16);
		if (stplyr->exiting <= 6*TICRATE && !stplyr->spectator)
		{
			patch_t *p = kp_battlecool;

			if (K_IsPlayerLosing(stplyr))
				p = kp_battlelose;
			else if (stplyr->position == 1 && (!itembreaker || numtargets >= nummapboxes))
				p = kp_battlewin;

			V_DrawFixedPatch(x<<FRACBITS, y, scale, splitflags, p, NULL);
		}

		K_drawKartFinish();
	}
	else if (stplyr->bumper <= 0 && stplyr->karmadelay && comeback && !stplyr->spectator && drawcomebacktimer)
	{
		uint16_t t = stplyr->karmadelay/(10*TICRATE);
		int32_t txoff, adjust = (r_splitscreen > 1) ? 4 : 6; // normal string is 8, kart string is 12, half of that for ease
		int32_t ty = (BASEVIDHEIGHT/2)+66;

		txoff = adjust;

		while (t)
		{
			txoff += adjust;
			t /= 10;
		}

		if (r_splitscreen)
		{
			if (r_splitscreen > 1)
				ty = (BASEVIDHEIGHT/4)+33;
			if ((splitscreen == 1 && stplyrnum == 1) || (splitscreen > 1 && stplyrnum & 2))
				ty += (BASEVIDHEIGHT/2);
		}
		else
			V_DrawFadeScreen(0xFF00, 16);

		if (!comebackshowninfo)
			V_DrawFixedPatch(x<<FRACBITS, y, scale, splitflags, kp_battleinfo, NULL);
		else
			V_DrawFixedPatch(x<<FRACBITS, y, scale, splitflags, kp_battlewait, NULL);

		if (r_splitscreen > 1)
			V_DrawString(x-txoff, ty, 0, va("%d", stplyr->karmadelay/TICRATE));
		else
		{
			if (!K_UseColorHud())
				V_DrawFixedPatch(x<<FRACBITS, ty<<FRACBITS, scale, 0, kp_timeoutsticker[0], NULL);
			else //Colourized hud
			{
				uint8_t *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
				V_DrawFixedPatch(x<<FRACBITS, ty<<FRACBITS, scale, 0, kp_timeoutsticker[1], colormap);
			}
			V_DrawKartString(x-txoff, ty, 0, va("%d", stplyr->karmadelay/TICRATE));
		}
	}

	// FREE PLAY?
	{
		uint8_t i;

		// check to see if there's anyone else at all
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (i == displayplayers[0])
				continue;
			if (playeringame[i] && !players[i].spectator)
				break;
		}

		if (i != MAXPLAYERS)
			K_drawKartFreePlay();
	}
}

static void K_drawKartFirstPerson(void)
{
	static int32_t pnum[4], turn[4], drift[4];
	const int16_t steerThreshold = KART_FULLTURN / 2;
	int32_t pn = 0, tn = 0, dr = 0;
	int32_t target = 0, splitflags = V_SNAPTOBOTTOM|V_SPLITSCREEN;
	int32_t x = BASEVIDWIDTH/2, y = BASEVIDHEIGHT;
	fixed_t scale;
	uint8_t *colmap = NULL;

	if (stplyr->spectator || !stplyr->mo || (stplyr->mo->renderflags & RF_DONTDRAW))
		return;

	if (stplyrnum == 1 && r_splitscreen)
		{ pn = pnum[1]; tn = turn[1]; dr = drift[1]; }
	else if (stplyrnum == 2 && r_splitscreen > 1)
		{ pn = pnum[2]; tn = turn[2]; dr = drift[2]; }
	else if (stplyrnum == 3 && r_splitscreen > 2)
		{ pn = pnum[3]; tn = turn[3]; dr = drift[3]; }
	else
		{ pn = pnum[0]; tn = turn[0]; dr = drift[0]; }

	if (r_splitscreen)
	{
		y >>= 1;
		if (r_splitscreen > 1)
			x >>= 1;
	}

	{
		if (stplyr->speed < (20*stplyr->mo->scale) && (leveltime & 1) && !r_splitscreen)
			y++;

		if (stplyr->mo->renderflags & RF_TRANSMASK)
			splitflags |= ((stplyr->mo->renderflags & RF_TRANSMASK) >> RF_TRANSSHIFT) << FF_TRANSSHIFT;
		else if (stplyr->mo->frame & FF_TRANSMASK)
			splitflags |= (stplyr->mo->frame & FF_TRANSMASK);
	}

	if (stplyr->cmd.turning > steerThreshold) // strong left turn
		target = 2;
	else if (stplyr->cmd.turning < -steerThreshold) // strong right turn
		target = -2;
	else if (stplyr->cmd.turning > 0) // weak left turn
		target = 1;
	else if (stplyr->cmd.turning < 0) // weak right turn
		target = -1;
	else // forward
		target = 0;

	if (encoremode)
		target = -target;

	if (pn < target)
		pn++;
	else if (pn > target)
		pn--;

	if (pn < 0)
		splitflags |= V_FLIP; // right turn

	target = abs(pn);
	if (target > 2)
		target = 2;

	x <<= FRACBITS;
	y <<= FRACBITS;

	if (tn != stplyr->cmd.turning/50)
		tn -= (tn - (stplyr->cmd.turning/50))/8;

	if (dr != stplyr->drift*16)
		dr -= (dr - (stplyr->drift*16))/8;

	if (r_splitscreen == 1)
	{
		scale = (2*FRACUNIT)/3;
		y += FRACUNIT/vid.dup; // correct a one-pixel gap on the screen view (not the basevid view)
	}
	else if (r_splitscreen)
		scale = FRACUNIT/2;
	else
		scale = FRACUNIT;

	if (stplyr->mo)
	{
		uint8_t driftcolor = K_DriftSparkColor(stplyr, stplyr->driftcharge);
		const angle_t ang = R_PointToAngle2(0, 0, stplyr->rmomx, stplyr->rmomy) - stplyr->drawangle;
		// yes, the following is correct. no, you do not need to swap the x and y.
		fixed_t xoffs = -P_ReturnThrustY(stplyr->mo, ang, (BASEVIDWIDTH<<(FRACBITS-2))/2);
		fixed_t yoffs = -P_ReturnThrustX(stplyr->mo, ang, 4*FRACUNIT);

		if ((yoffs += 4*FRACUNIT) < 0)
			yoffs = 0;

		if (r_splitscreen)
			xoffs = FixedMul(xoffs, scale);

		xoffs -= (tn)*scale;
		xoffs -= (dr)*scale;

		if (stplyr->drawangle == stplyr->mo->angle)
		{
			const fixed_t mag = FixedDiv(stplyr->speed, 10*stplyr->mo->scale);

			if (mag < FRACUNIT)
			{
				xoffs = FixedMul(xoffs, mag);
				if (!r_splitscreen)
					yoffs = FixedMul(yoffs, mag);
			}
		}

		if (stplyr->mo->momz > 0) // TO-DO: Draw more of the kart so we can remove this if!
			yoffs += stplyr->mo->momz/3;

		if (encoremode)
			x -= xoffs;
		else
			x += xoffs;
		if (!r_splitscreen)
			y += yoffs;


		if ((leveltime & 1) && (driftcolor != SKINCOLOR_NONE)) // drift sparks!
			colmap = R_GetTranslationColormap(TC_RAINBOW, driftcolor, GTC_CACHE);
		else if (stplyr->mo->colorized && stplyr->mo->color) // invincibility/grow/shrink!
			colmap = R_GetTranslationColormap(TC_RAINBOW, stplyr->mo->color, GTC_CACHE);
	}

	V_DrawFixedPatch(x, y, scale, splitflags, kp_fpview[target], colmap);

	if (stplyrnum == 1 && r_splitscreen)
		{ pnum[1] = pn; turn[1] = tn; drift[1] = dr; }
	else if (stplyrnum == 2 && r_splitscreen > 1)
		{ pnum[2] = pn; turn[2] = tn; drift[2] = dr; }
	else if (stplyrnum == 3 && r_splitscreen > 2)
		{ pnum[3] = pn; turn[3] = tn; drift[3] = dr; }
	else
		{ pnum[0] = pn; turn[0] = tn; drift[0] = dr; }
}

// doesn't need to ever support 4p
static void K_drawInput(void)
{
	int32_t splitflags = (V_SNAPTOBOTTOM|V_SNAPTORIGHT|V_SLIDEIN);
	int32_t x = (BASEVIDWIDTH - 32)*FRACUNIT, y = (BASEVIDHEIGHT - 24)*FRACUNIT;
	uint8_t *shadowcolormap = NULL;
	int32_t offs, col;
	const skincolornum_t hudcolor = K_GetHudColor();
	const int32_t accent1 = splitflags | skincolors[hudcolor].ramp[5];
	const int32_t accent2 = splitflags | skincolors[hudcolor].ramp[9];
	const uint8_t *hudcolormap = R_GetTranslationColormap(0, hudcolor, GTC_CACHE);

	if (r_splitscreen)
		return;

#define BUTTW 8
#define BUTTH 11

#define drawbutt(xoffs, butt, symb)\
	if ((K_GetKartButtons(stplyr) & butt))\
	{\
		offs = 2*FRACUNIT;\
		col = accent1;\
	}\
	else\
	{\
		offs = 0;\
		col = accent2;\
		V_DrawFill((x + xoffs*FRACUNIT)>>FRACBITS, (y + BUTTH*FRACUNIT)>>FRACBITS, BUTTW-1, 2, splitflags|31);\
	}\
	V_DrawFill((x + xoffs*FRACUNIT)>>FRACBITS, (y+offs)>>FRACBITS, BUTTW-1, BUTTH, col);\
	V_DrawFixedPatch(x + FRACUNIT + xoffs*FRACUNIT, y + offs + FRACUNIT, FRACUNIT, splitflags, fontv[TINY_FONT].font[symb-HU_FONTSTART], NULL)

	drawbutt(-2*BUTTW, BT_ACCELERATE, 'A');
	drawbutt(  -BUTTW, BT_BRAKE,      'B');
	drawbutt(       0, BT_DRIFT,      'D');
	drawbutt(   BUTTW, BT_ATTACK,     'I');

#undef drawbutt

#undef BUTTW
#undef BUTTH

	y -= FRACUNIT;

	if (cv_showinput.value > 1)
	{
		int32_t joyx, joyxoffs, joyy, joyyoffs;
		joyxoffs = -8, joyyoffs = -24;
		joyx = x>>FRACBITS, joyy = y>>FRACBITS;

		// O backing
		if (cv_showinput.value == 2)
		{
			shadowcolormap = R_GetTranslationColormap(0, SKINCOLOR_BLACK, GTC_CACHE);
			V_DrawFixedPatch((joyx+joyxoffs)<<FRACBITS, (joyy+joyyoffs-1)<<FRACBITS, FRACUNIT, splitflags, joybacking, hudcolormap);
		}
		else
		{
			V_DrawFill(joyx+joyxoffs, joyy+joyyoffs-1, 16, 16, splitflags|accent2);
			V_DrawFill(joyx+joyxoffs, joyy+joyyoffs+15, 16, 1, splitflags|31);
		}

		if (stplyr->cmd.turning || stplyr->cmd.throwdir)
		{
			int16_t turning = encoremode ? -stplyr->cmd.turning : stplyr->cmd.turning;
			if (cv_showinput.value == 2)
			{
				V_DrawFixedPatch((joyx+joyxoffs+3-turning/80)<<FRACBITS, (joyy+joyyoffs+2-stplyr->cmd.throwdir/80)<<FRACBITS, FRACUNIT, splitflags, joyknob, shadowcolormap);
				V_DrawFixedPatch((joyx+joyxoffs+3-turning/64)<<FRACBITS, (joyy+joyyoffs+1-stplyr->cmd.throwdir/64)<<FRACBITS, FRACUNIT, splitflags, joyknob, hudcolormap);
			}
			else
			{
				// joystick hole
				V_DrawFill(joyx+joyxoffs+5, joyy+joyyoffs+4, 6, 6, splitflags|accent1);
				// joystick top and back
				V_DrawFill(joyx+joyxoffs+3-turning/80,
					joyy+joyyoffs+2-stplyr->cmd.throwdir/80,
					10, 10, splitflags|31);
				V_DrawFill(joyx+joyxoffs+3-turning/64,
					joyy+joyyoffs+1-stplyr->cmd.throwdir/64,
					10, 10, splitflags|accent1);
			}
		}
		else
		{
			if (cv_showinput.value == 2)
			{
				V_DrawFixedPatch((joyx+joyxoffs+3)<<FRACBITS, (joyy+joyyoffs+8)<<FRACBITS, FRACUNIT, splitflags, joyshadow, shadowcolormap);
				V_DrawFixedPatch((joyx+joyxoffs+3)<<FRACBITS, (joyy+joyyoffs+1)<<FRACBITS, FRACUNIT, splitflags, joyknob, hudcolormap);
			}
			else
			{
				V_DrawFill(joyx+joyxoffs+3, joyy+joyyoffs+11, 10, 1, splitflags|accent2);
				V_DrawFill(joyx+joyxoffs+3,
					joyy+joyyoffs+1,
					10, 10,splitflags|accent1);
			}
		}
	}
	else if (cv_showinput.value == 1)
	{
		patch_t *workingPic = kp_inputwheel;
		patch_t *shadowPic = kp_inputwheel_shadow;

		int16_t turning = encoremode ? -stplyr->cmd.turning : stplyr->cmd.turning;
		angle_t rotate = FixedAngle(60 * FixedDiv(abs(turning), 1024) * intsign(turning));
		uint8_t *colormap;

		if (!K_GetHudColor())
		{
			V_DrawRotatedPatch(x-(17*FRACUNIT), y-(38*FRACUNIT)+(FRACUNIT*2), rotate, FRACUNIT, FRACUNIT, splitflags|V_SUBTRACT|V_10TRANS, shadowPic, NULL);
			V_DrawRotatedPatch(x-(17*FRACUNIT), y-(38*FRACUNIT), rotate, FRACUNIT, FRACUNIT, splitflags, workingPic, NULL);
		}
		else
		{
			colormap = R_GetTranslationColormap(0, K_GetHudColor(), GTC_CACHE);
			V_DrawRotatedPatch(x-(17*FRACUNIT), y-(38*FRACUNIT)+(FRACUNIT*2), rotate, FRACUNIT, FRACUNIT, splitflags|V_SUBTRACT|V_10TRANS, shadowPic, colormap);
			V_DrawRotatedPatch(x-(17*FRACUNIT), y-(38*FRACUNIT), rotate, FRACUNIT, FRACUNIT, splitflags, workingPic, colormap);
		}

		// a la GT7
		if (stplyr->cmd.flags & TICCMD_USINGTILT)
		{
			fixed_t tilt = G_GetGamepadGravity(stplyrnum).x;
			if (demo.playback || !P_IsMachineLocalPlayer(stplyr))
				tilt = -1;
			else
				rotate = FixedAngle(120 * abs(tilt));
			x += 17*FSIN(rotate) * intsign(tilt);
			y -= 17*FCOS(rotate);
			splitflags |= (((leveltime % 3 == 0) && (stplyr->cmd.flags & TICCMD_EXCESSTILT)) ? V_ADD|V_20TRANS : 0);

			if (!K_GetHudColor())
			{
				V_DrawFixedPatch(x-(2*FRACUNIT)-(FRACUNIT/2), y-(23*FRACUNIT), FRACUNIT, splitflags, kp_minimapdot, NULL);
			}
			else
			{
				colormap = R_GetTranslationColormap(0, K_GetHudColor(), GTC_CACHE);
				V_DrawFixedPatch(x-(2*FRACUNIT)-(FRACUNIT/2), y-(23*FRACUNIT), FRACUNIT, splitflags, kp_minimapdot, colormap);
			}
		}
	}
}

static void K_drawChallengerScreen(void)
{
	// This is an insanely complicated animation.
	static uint8_t anim[52] = {
		0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13, // frame 1-14, 2 tics: HERE COMES A NEW slides in
		14,14,14,14,14,14, // frame 15, 6 tics: pause on the W
		15,16,17,18, // frame 16-19, 1 tic: CHALLENGER approaches screen
		19,20,19,20,19,20,19,20,19,20, // frame 20-21, 1 tic, 5 alternating: all text vibrates from impact
		21,22,23,24 // frame 22-25, 1 tic: CHALLENGER turns gold
	};
	const uint8_t offset = min(52-1, (3*TICRATE)-mapreset);

	V_DrawFadeScreen(0xFF00, 16); // Fade out
	V_DrawScaledPatch(0, 0, 0, kp_challenger[anim[offset]]);
}

static dboolean K_DisplayingLapEmblem(void)
{
	return ((cv_showlapemblem.value & 1) && (stplyr->karthud[khud_lapanimation]));
}

static void K_drawLapStartAnim(void)
{
	if (!K_DisplayingLapEmblem())
		return;

	// This is an EVEN MORE insanely complicated animation.
	const uint8_t t = stplyr->karthud[khud_lapanimation];
	const uint8_t progress = 80 - t;

	const uint8_t tOld = t + 1;
	const uint8_t progressOld = 80 - tOld;

	const tic_t leveltimeOld = leveltime - 1;

	uint8_t *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);

	fixed_t interpx, interpy, newval, oldval;

	newval = (BASEVIDWIDTH/2 + (32 * max(0, t - 76))) * FRACUNIT;
	oldval = (BASEVIDWIDTH/2 + (32 * max(0, tOld - 76))) * FRACUNIT;
	interpx = R_InterpolateFixed(oldval, newval);

	newval = (48 - (32 * max(0, progress - 76))) * FRACUNIT;
	oldval = (48 - (32 * max(0, progressOld - 76))) * FRACUNIT;
	interpy = R_InterpolateFixed(oldval, newval);

	V_DrawFixedPatch(
		interpx, interpy,
		FRACUNIT, V_SNAPTOTOP|V_HUDTRANS,
		(modeattacking ? kp_lapanim_emblem[1] : kp_lapanim_emblem[0]), colormap);

	if (stplyr->karthud[khud_laphand] >= 1 && stplyr->karthud[khud_laphand] <= 3)
	{
		newval = (4 - abs((signed)((leveltime % 8) - 4))) * FRACUNIT;
		oldval = (4 - abs((signed)((leveltimeOld % 8) - 4))) * FRACUNIT;
		interpy += R_InterpolateFixed(oldval, newval);

		V_DrawFixedPatch(
			interpx, interpy,
			FRACUNIT, V_SNAPTOTOP|V_HUDTRANS,
			kp_lapanim_hand[stplyr->karthud[khud_laphand]-1], NULL);
	}

	if (stplyr->laps == (uint8_t)(numlaps))
	{
		newval = (62 - (32 * max(0, progress - 76))) * FRACUNIT;
		oldval = (62 - (32 * max(0, progressOld - 76))) * FRACUNIT;
		interpx = R_InterpolateFixed(oldval, newval);

		V_DrawFixedPatch(
			interpx, // 27
			30*FRACUNIT, // 24
			FRACUNIT, V_SNAPTOTOP|V_HUDTRANS,
			kp_lapanim_final[min(progress/2, 10)], NULL);

		if (progress/2-12 >= 0)
		{
			newval = (188 + (32 * max(0, progress - 76))) * FRACUNIT;
			oldval = (188 + (32 * max(0, progressOld - 76))) * FRACUNIT;
			interpx = R_InterpolateFixed(oldval, newval);

			V_DrawFixedPatch(
				interpx, // 194
				30*FRACUNIT, // 24
				FRACUNIT, V_SNAPTOTOP|V_HUDTRANS,
				kp_lapanim_lap[min(progress/2-12, 6)], NULL);
		}
	}
	else
	{
		newval = (82 - (32 * max(0, progress - 76))) * FRACUNIT;
		oldval = (82 - (32 * max(0, progressOld - 76))) * FRACUNIT;
		interpx = R_InterpolateFixed(oldval, newval);

		V_DrawFixedPatch(
			interpx, // 61
			30*FRACUNIT, // 24
			FRACUNIT, V_SNAPTOTOP|V_HUDTRANS,
			kp_lapanim_lap[min(progress/2, 6)], NULL);

		if (progress/2-8 >= 0)
		{
			newval = (188 + (32 * max(0, progress - 76))) * FRACUNIT;
			oldval = (188 + (32 * max(0, progressOld - 76))) * FRACUNIT;
			interpx = R_InterpolateFixed(oldval, newval);

			V_DrawFixedPatch(
				interpx, // 194
				30*FRACUNIT, // 24
				FRACUNIT, V_SNAPTOTOP|V_HUDTRANS,
				kp_lapanim_number[(((uint32_t)stplyr->laps) / 10)][min(progress/2-8, 2)], NULL);

			if (progress/2-10 >= 0)
			{
				newval = (208 + (32 * max(0, progress - 76))) * FRACUNIT;
				oldval = (208 + (32 * max(0, progressOld - 76))) * FRACUNIT;
				interpx = R_InterpolateFixed(oldval, newval);

				V_DrawFixedPatch(
					interpx, // 221
					30*FRACUNIT, // 24
					FRACUNIT, V_SNAPTOTOP|V_HUDTRANS,
					kp_lapanim_number[(((uint32_t)stplyr->laps) % 10)][min(progress/2-10, 2)], NULL);
			}
		}
	}
}

// Redundant copy from k_kart.
static int32_t K_GetRaceSplitsToggle(void)
{
	if (cv_showlapemblem.value < 2)
	{
		// Splits aren't on.
		return 0;
	}

	// Not taking any chances with this stupid goddamn engine.
	return max((int32_t)(cv_racesplits.value), 1);
}

static dboolean K_doDrawSplits(void)
{
	return (!K_DisplayingLapEmblem()) &&
			stplyr->karthud[khud_splittimer] &&
			(stplyr->karthud[khud_splittimer] > TICRATE/3 || stplyr->karthud[khud_splittimer]%2);
}

static void K_drawLapSplitTimestamp(void)
{
	if (!K_GetRaceSplitsToggle())
	{
		return;
	}

	// Draw the timestamp
	dboolean debug_alwaysdrawsplits = false;
	if (K_doDrawSplits() || debug_alwaysdrawsplits)
	{
		int32_t split = stplyr->karthud[khud_splittime];
		int32_t skin = stplyr->karthud[khud_splitskin];
		int32_t color = stplyr->karthud[khud_splitcolor];
		int32_t ahead = stplyr->karthud[khud_splitwin];
		// int32_t pos = stplyr->karthud[khud_splitposition];

		int32_t splitflags = V_HUDTRANS|V_SNAPTOTOP|V_SNAPTORIGHT|V_SPLITSCREEN;

		// debug
		if (!stplyr->karthud[khud_splittimer])
		{
			ahead = ((leveltime/17)%5) - 2;
			split = leveltime;
			skin = stplyr->skin;
			color = stplyr->skincolor;
		}

		split = abs(split);

		uint8_t *skincolor = R_GetTranslationColormap(skin, color, GTC_CACHE);

		uint8_t *textcolor = 0;
		switch (ahead)
		{
			case 2:
				textcolor = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_BLUE, GTC_CACHE); // leading and gaining
				break;
			case 1:
				textcolor = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_SKY, GTC_CACHE); // leading and losing
				break;
			case -1:
				textcolor = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_ORANGE, GTC_CACHE); // trailing and gaining
				break;
			case -2:
				textcolor = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_RASPBERRY, GTC_CACHE); // trailing and losing
				break;
		}

		if (!K_UseColorHud())
			V_DrawScaledPatch(TIME_X, TIME_Y, splitflags, ((gamemap == 2) ? kp_lapstickerwide[0] : kp_timestickerwide[0]));
		else //Colourized hud
		{
			uint8_t *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
			V_DrawMappedPatch(TIME_X, TIME_Y, splitflags, ((gamemap == 2) ? kp_lapstickerwide[1] : kp_timestickerwide[1]), colormap);
		}

		char buffer[32];
		snprintf(buffer, 32, "%02i'%02i\"%02i",
			G_TicsToMinutes(split, true),
			G_TicsToSeconds(split),
			G_TicsToCentiseconds(split)
		);

		V_DrawStringScaledEx(
			(TIME_X + 33) << FRACBITS,
			(TIME_Y + 3) << FRACBITS,
			FRACUNIT,
			FRACUNIT,
			FRACUNIT,
			FRACUNIT,
			splitflags,
			textcolor,
			KART_FONT,
			buffer
		);

		V_DrawStringScaledEx(
			(TIME_X + 15) << FRACBITS,
			(TIME_Y + 3) << FRACBITS,
			FRACUNIT,
			FRACUNIT,
			FRACUNIT,
			FRACUNIT,
			splitflags,
			textcolor,
			KART_FONT,
			ahead >= 0 ? "-" : "+"
		);

		V_DrawMappedPatch(TIME_X - 8, TIME_Y + 3, splitflags, faceprefix[skin][FACE_MINIMAP], skincolor);
	}
}

static void K_drawLapSplitComparison(void)
{
	if (!K_GetRaceSplitsToggle())
	{
		return;
	}

	// Draw the timestamp
	dboolean debug_alwaysdrawsplits = false;
	if (K_doDrawSplits() || debug_alwaysdrawsplits)
	{
		int32_t split = stplyr->karthud[khud_splittime];
		int32_t skin = stplyr->karthud[khud_splitskin];
		int32_t color = stplyr->karthud[khud_splitcolor];
		int32_t ahead = stplyr->karthud[khud_splitwin];
		int32_t pos = stplyr->karthud[khud_splitposition];

		// debug
		if (!stplyr->karthud[khud_splittimer])
		{
			ahead = ((leveltime/17)%5) - 2;
			split = leveltime;
			skin = stplyr->skin;
			color = stplyr->skincolor;
		}

		split = abs(split);

		uint8_t *skincolor = R_GetTranslationColormap(skin, color, GTC_CACHE);

		uint32_t textcolor = 0;
		switch (ahead)
		{
			case 2:
				textcolor = V_BLUEMAP; // leading and gaining
				break;
			case 1:
				textcolor = V_SKYMAP; // leading and losing
				break;
			case -1:
				textcolor = V_ORANGEMAP; // trailing and gaining
				break;
			case -2:
				textcolor = V_REDMAP; // trailing and losing
				break;
		}

		fixed_t row_position[2] = {
			BASEVIDWIDTH/2 + cv_lapem_xoffset.value,
			BASEVIDHEIGHT/4 + cv_lapem_yoffset.value
		};
		uint32_t splitflags = V_30TRANS;

		const char *arrow = (ahead == 1 || ahead == -2) ? "\x1B" : "\x1A";
		char buffer[256];
		snprintf(buffer, 256, "%s%02i'%02i\"%02i%s",
			ahead >= 0 ? "-" : "+",
			G_TicsToMinutes(split, true),
			G_TicsToSeconds(split),
			G_TicsToCentiseconds(split),
			arrow
		);

		int32_t stwidth = V_StringWidth(buffer, splitflags) / 2;

		// vibes offset
		V_DrawMappedPatch(row_position[0] - stwidth - 35, row_position[1], splitflags, faceprefix[skin][FACE_MINIMAP], skincolor);

		if (pos > 1)
		{
			V_DrawPingNum(row_position[0] - stwidth - 35, row_position[1], splitflags, pos, NULL);
		}

		// vibes offset TWO
		row_position[0] += 15;
		V_DrawCenteredString(row_position[0], row_position[1], splitflags|textcolor, buffer);
	}
}

void K_drawKartFreePlay(void)
{
	// Doesn't support splitscreens higher than 2 for real estate reasons.

	if (!LUA_HudEnabled(hud_freeplay))
		return;

	if (modeattacking || grandprixinfo.gp || bossinfo.boss || stplyr->spectator)
		return;

	if (!cv_showfreeplay.value)
		return;

	if (lt_exitticker < TICRATE/2)
		return;

	if (((leveltime-lt_endtime) % TICRATE) < TICRATE/2)
		return;

	V_DrawKartString((BASEVIDWIDTH - (LAPS_X+1)) - (12*9), // mirror the laps thingy
		LAPS_Y+3, V_HUDTRANS|V_SNAPTOBOTTOM|V_SNAPTORIGHT|V_SPLITSCREEN, "FREE PLAY");
}

static void
Draw_party_ping (int ss, int32_t snap)
{
	uint32_t ping = playerpingtable[displayplayers[ss]];
	uint32_t mindelay = playerdelaytable[displayplayers[ss]];
	HU_drawMiniPing(0, 0, ping, max(ping, mindelay), V_HUDTRANS|V_SPLITSCREEN|V_SNAPTOTOP|snap);
}

static void
K_drawMiniPing (void)
{
	uint32_t f = V_SNAPTORIGHT;
	uint8_t i;

	if (!cv_showping.value)
	{
		return;
	}

	for (i = 0; i <= r_splitscreen; i++)
	{
		if (stplyr == &players[displayplayers[i]])
		{
			if (r_splitscreen > 1 && !(i & 1))
			{
				f = V_SNAPTOLEFT;
			}

			Draw_party_ping(i, f);
			break;
		}
	}
}

static void K_drawDistributionDebugger(void)
{
	uint8_t useodds = 0;
	uint8_t pingame = 0, bestbumper = 0;
	uint32_t pdis = 0;
	int32_t i;
	int32_t x = -9, y = -9;
	//dboolean dontforcespb = false;
	dboolean spbrush = false;

	//if (stplyrnum != 0) // only for p1
		//return;

	// The only code duplication from the Kart, just to avoid the actual item function from calculating pingame twice
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		pingame++;
		//if (players[i].exiting)
			//dontforcespb = true;
		if (players[i].bumper > bestbumper)
			bestbumper = players[i].bumper;
	}

	pdis = K_CalculatePDIS(stplyr, pingame, &spbrush);

	useodds = K_FindUseodds(stplyr, 0, pdis, bestbumper, spbrush);

	if (pingame == 1)
	{
		if (stplyr->itemroulette && stplyr->cmd.buttons & BT_ATTACK && K_ItemResultEnabled(K_GetKartResult("superring")))
			V_DrawScaledPatch(x, y, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, K_GetCachedItemPatch(KITEM_SUPERRING, true, 0));
		else
			V_DrawScaledPatch(x, y, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, K_GetCachedItemPatch(KITEM_SNEAKER, true, 0));
		V_DrawThinString(x+11, y+31, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, va("%d", 200));
	}
	else
	{
		int32_t itemodds[MAXKARTRESULTS];
		kartroulette_t roulette = {
			.pdis = pdis,
			.playerpos = stplyr->position,
			.pos = useodds,
			.ourDist = stplyr->distancetofinish,
			.clusterDist = stplyr->distancefromcluster,
			.mashed = 0,
			.spbrush = spbrush,
			.bot = stplyr->bot,
			.rival = stplyr->bot && stplyr->botvars.rival,
			.inBottom = K_IsPlayerLosing(stplyr),
		};

		K_KartGetItemOdds(&roulette, itemodds);

		for (i = 0; i < numkartresults; i++)
		{
			kartresult_t *result = &kartresults[i];

			if (itemodds[i] <= 0 && roulette.forceme[i] == 0) // At the very least display forced items; that info's also important.
				continue;

			V_DrawScaledPatch(x, y, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, K_GetCachedItemPatch(result->type, true, 0));
			if (result->isalt)
				V_DrawScaledPatch(x+2, y, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, K_getItemAltPatch(true, false));
			V_DrawThinString(x+11, y+31, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, roulette.forceme[i] ? va("\x85" "FRC(%d)" "\x80 ", roulette.forceme[i]) : va("%d", itemodds[i]));
			if (result->amount > 1)
				V_DrawString(x+24, y+31, V_SPLITSCREEN|V_ALLOWLOWERCASE|V_HUDTRANS|V_SNAPTOTOP, va("x%d", result->amount));

			x += 32;
			if (x >= 297)
			{
				x = -9;
				y += 32;
			}
		}
	}

	if (pingame == 1)
		V_DrawString(0, 0, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, "TA MODE");
	else
		V_DrawString(0, 0, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, va("USEODDS %d", useodds));

	V_DrawString(150, 0, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, va("PDIS %u", pdis));

	if (K_LegacyOddsMode())
		V_DrawSmallString(70, 0, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, "Legacy Distance Mode");

	// cooldown timer debugging
	x = 240;
	y = 160;
	for (i = 0; i < numkartresults; i++)
	{
		kartresult_t *result = &kartresults[i];
		if (result->cooldown > 0 && result->isalt == K_IsKartItemAlternate(result->type))
		{
			int32_t color = result->flags & KRF_INDIRECTITEM ? V_ORANGEMAP : K_GetItemFlags(result->type) & KIF_UNIQUE ? V_YELLOWMAP : 0;
			V_DrawScaledPatch(x, y, V_HUDTRANS|V_SNAPTOBOTTOM, K_GetCachedItemPatch(result->type, true, 0));
			if (result->amount > 1)
				V_DrawThinString(x+30, y+30, V_ALLOWLOWERCASE|V_HUDTRANS|V_SNAPTOBOTTOM|V_6WIDTHSPACE, va("x%d", result->amount));
			V_DrawString(x+11, y+31, V_HUDTRANS|V_SNAPTOBOTTOM|color, va("%u", result->cooldown/TICRATE));
			x -= 32;
		}
	}
}

static void K_drawCheckpointDebugger(void)
{
	if (stplyrnum != 0) // only for p1
		return;

	if (stplyr->starpostnum >= K_CheckpointThreshold(true))
		V_DrawString(8, 184, 0, va("Checkpoint: %d / %d (Can finish)", stplyr->starpostnum, numstarposts));
	else if (K_CheckpointThreshold(false) != numstarposts)
		V_DrawString(8, 184, 0, va("Checkpoint: %d / %d (Skip: %d)", stplyr->starpostnum, numstarposts, K_CheckpointThreshold(false) + stplyr->starpostnum));
	else
		V_DrawString(8, 184, 0, va("Checkpoint: %d / %d", stplyr->starpostnum, numstarposts));

	if (K_UsingLegacyCheckpoints())
		V_DrawString(8, 192, 0, va("Waypoint dist: Prev %d, Next %d", stplyr->prevcheck, stplyr->nextcheck));
}

static void K_DrawWaypointDebugger(void)
{
	if (cv_kartdebugwaypoints.value == 0)
		return;

	if (stplyrnum != 0) // only for p1
		return;

	if (stplyr->bigwaypointgap)
		V_DrawString(8, 140, 0, va("Auto Respawn Timer: %d", stplyr->bigwaypointgap));

	if (stplyr->currentwaypoint &&
	    K_SafeRespawnWaypoint(stplyr->currentwaypoint) &&
	    (stplyr->currentwaypoint->mobj != NULL) &&
	    K_SafeRespawnPosition(stplyr->currentwaypoint->mobj))
	{
		V_DrawString(8, 120, 0, va("Waypoint %d (Current) Safe: True", K_GetWaypointID(stplyr->currentwaypoint)));
	}
	else
	{
		V_DrawString(8, 120, 0, va("Waypoint %d (Current) Safe: False", K_GetWaypointID(stplyr->currentwaypoint)));	
	}

	if (stplyr->nextwaypoint &&
	    K_SafeRespawnWaypoint(stplyr->nextwaypoint) &&
	    (stplyr->nextwaypoint->mobj != NULL) &&
	    K_SafeRespawnPosition(stplyr->nextwaypoint->mobj))
	{
		V_DrawString(8, 130, 0, va("Waypoint %d (Next) Safe: True", K_GetWaypointID(stplyr->nextwaypoint)));
	}
	else
	{
		V_DrawString(8, 130, 0, va("Waypoint %d (Next) Safe: False", K_GetWaypointID(stplyr->nextwaypoint)));
	}

	V_DrawString(8, 150, 0, va("Current Waypoint ID: %d", K_GetWaypointID(stplyr->currentwaypoint)));
	V_DrawString(8, 160, 0, va("Next Waypoint ID: %d", K_GetWaypointID(stplyr->nextwaypoint)));
	V_DrawString(8, 170, 0, va("Finishline Distance: %u (WP %u)", stplyr->distancetofinish,
	                        stplyr->currentwaypoint ? stplyr->currentwaypoint->distancetofinish : 0));
}

static void K_DrawClusterDebugger(void)
{
	if (cv_kartdebugcluster.value == 0)
		return;

	if (stplyrnum != 0) // only for p1
		return;

	int32_t vflags = V_6WIDTHSPACE|V_ALLOWLOWERCASE;

	int32_t y = 146;

	if (K_LegacyOddsMode())
	{
		if (clusterid != UINT32_MAX)
		{
			V_DrawThinString(8, y, vflags, va("X: %f, Y: %f, Z: %f, Dist. from cluster: %d", FIXED_TO_FLOAT(clusterpoint.x), FIXED_TO_FLOAT(clusterpoint.y), FIXED_TO_FLOAT(clusterpoint.z), stplyr->distancefromcluster));
			y -= 10;

			V_DrawThinString(8, y, vflags, va("Dist. from best-fit: %d", stplyr->distancefrombestfit));
			y -= 10;
		}

		if (num_player_clusters)
		{
			V_DrawThinString(8, y, vflags, va("Clusters active: %d", num_player_clusters));
			y -= 10;

			const player_t * bestfitplayer = player_clusters[0].nucleus.player;

			if (bestfitplayer)
			{
				V_DrawThinString(8, y, vflags, va("Best-fit player: %s", player_names[bestfitplayer - players]));
				y -= 10;
			}
		}

		if (clusterid != UINT32_MAX)
			V_DrawThinString(8, y, vflags, va("Cluster player: %s", player_names[clusterid]));
	}
	else
	{
		V_DrawThinString(8, y, vflags, va("Dist. from cluster: %d", stplyr->distancefromcluster));
		y -= 10;

		V_DrawThinString(8, y, vflags, va("Dist. from best-fit: %d", stplyr->distancefrombestfit));
		y -= 10;

		if (clusterid != UINT32_MAX)
		{
			V_DrawThinString(8, y, vflags, va("Cluster DtF: %d, Your DtF: %d", (uint32_t)(clusterdtf.x), stplyr->distancetofinish));
			y -= 10;
		}

		if (num_player_clusters)
		{
			V_DrawThinString(8, y, vflags, va("Clusters active: %d", num_player_clusters));
			y -= 10;

			const player_t * bestfitplayer = player_clusters[0].nucleus.player;

			if (bestfitplayer)
			{
				V_DrawThinString(8, y, vflags, va("Best-fit player: %s", player_names[bestfitplayer - players]));
				y -= 10;
			}
		}

		if (clusterid != UINT32_MAX)
			V_DrawThinString(8, y, vflags, va("Cluster player: %s", player_names[clusterid]));
	}
}

static void K_DrawProgressDebugger(void)
{
	if (cv_kartdebugprogress.value == 0)
		return;

	if (stplyrnum != 0) // only for p1
		return;

	int32_t vflags = V_6WIDTHSPACE|V_ALLOWLOWERCASE;

	int32_t y = 146;

	const int32_t leaderscore = stplyr->ranklog.leaderscore;
	const uint32_t progress = stplyr->progressthrumap;
	const double prog_d = FixedToDouble(progress >> BIGTOSTDBITS);

	V_DrawThinString(8, y, vflags, va("Leader score: %f, raw: %d", FixedToDouble((leaderscore / 32) >> BIGTOSTDBITS) * 100.0, leaderscore));
	y -= 10;

	V_DrawThinString(8, y, vflags, va("Progress percentage: %f, fixed: %d, raw: %d", (prog_d * 100.0), (progress >> BIGTOSTDBITS), progress));
	y -= 10;
}

void K_getSlipstreamDrawinfo(drawinfo_t *out)
{
	int32_t fx, fy, splitflags = 0;

	if (r_splitscreen < 2) // don't change shit for THIS splitscreen.
	{
		fx = DRAT_X;
		fy = DRAT_Y;
		splitflags = V_SPLITSCREEN;

		if (r_splitscreen > 0)
			splitflags |= V_30TRANS;
        else
            splitflags |= V_SNAPTOBOTTOM;
	}
	else // now we're having a fun game.
	{
		if (stplyrnum == 0 || stplyrnum == 2) // If we are P1 or P3...
		{
			fx = DRAT_X;
			fy = DRAT_Y;
			splitflags = V_30TRANS|V_SPLITSCREEN;
		}
		else // else, that means we're P2 or P4.
		{
			fx = DRAT2_X;
			fy = DRAT2_Y;
			splitflags = V_30TRANS|V_SPLITSCREEN;
		}
	}

	out->x = fx;
	out->y = fy;
	out->flags = splitflags;
}

static void K_SlipstreamIndicator(dboolean tiny)
{
	 if (!K_DraftingActive())
		return;

	if (!cv_draftindicator.value)
		return;

    const char *fullstr = "DRAFTING";
	char str[256] = {0};
	int8_t stringlen = strlen(fullstr);
    int8_t len = min(stplyr->draftpower / (FRACUNIT / stringlen), stringlen);
	int32_t flags;
	int32_t fx, fy;

	drawinfo_t info;
	K_getSlipstreamDrawinfo(&info);

	fx = info.x;
	fy = info.y;
	flags = info.flags;

    if (!len)
		return;

    strncpy(str, fullstr, len);

    if (stplyr->draftpower >= FRACUNIT)
	{
		const int32_t clr = skincolors[stplyr->skincolor].chatcolor;
		flags |= clr;
	}

	if (tiny)
		V_DrawSmallString(fx - V_StringWidth(str, flags)/2, fy, flags, str);
	else
		V_DrawThinString(fx - V_StringWidth(str, flags)/2, fy, flags, str);
}

// determines if gametype info (laps/bumpers) should be hidden
static dboolean K_DisableGametypeInfo(void)
{
	if (!LUA_HudEnabled(hud_gametypeinfo))
	{
		return true;
	}

#ifdef ROTSPRITE
	if (r_splitscreen || (!cv_kartspeedometer.value))
	{
		// don't need to run checks if we're in splitscreen, or not using the speedometer
		return false;
	}
	
	if (cv_newspeedometer.value == 3)
		return true;
#endif

	return false;
}

void K_drawKartHUD(void)
{
	dboolean islonesome = false;
	dboolean battlefullscreen = false;
	dboolean gameinfovisible = false;
	uint8_t viewnum = R_GetViewNumber();
	dboolean freecam = camera[viewnum].freecam;	//disable some hud elements w/ freecam
	uint8_t i;

	// Define the X and Y for each drawn object
	// This is handled by console/menu values
	K_initKartHUD();

	// Draw that fun first person HUD! Drawn ASAP so it looks more "real".
	for (i = 0; i <= r_splitscreen; i++)
	{
		if (stplyr == &players[displayplayers[i]] && !camera[i].chase && !freecam)
			K_drawKartFirstPerson();
	}

	// Draw full screen stuff that turns off the rest of the HUD
	if (mapreset && stplyrnum == 0)
	{
		K_drawChallengerScreen();
		return;
	}

	battlefullscreen = ((gametypes[gametype]->rules & (GTR_BUMPERS|GTR_KARMA)) == (GTR_BUMPERS|GTR_KARMA)
		&& (stplyr->exiting
		|| (stplyr->bumper <= 0
		&& stplyr->karmadelay > 0
		&& !(stplyr->pflags & PF_ELIMINATED)
		&& comeback == true
		&& stplyr->playerstate == PST_LIVE)));

	if (!demo.title && (!battlefullscreen || r_splitscreen))
	{
		// Draw the CHECK indicator before the other items, so it's overlapped by everything else
		if (LUA_HudEnabled(hud_check))	// delete lua when?
			if (cv_kartcheck.value && !splitscreen && !players[displayplayers[0]].exiting && !freecam)
				K_drawKartPlayerCheck();

		// nametags
		if (LUA_HudEnabled(hud_names))
			K_drawKartNameTags();

		// Draw WANTED status
		if (gametypes[gametype]->rules & GTR_WANTED)
		{
			if (LUA_HudEnabled(hud_wanted))
				K_drawKartWanted();
		}

		if (cv_kartminimap.value)
		{
			if (LUA_HudEnabled(hud_minimap))
				K_drawKartMinimap();
		}
	}

	// Drift gauge should ideally be drawn behind other hud stuff, right?
	// right?
	K_DrawDriftGauge();
	// new flame shield bars (player-space)
	K_DrawFlamometer();

	if (battlefullscreen && !freecam)
	{
		if (LUA_HudEnabled(hud_battlefullscreen))
			K_drawBattleFullscreen();
		return;
	}

	// Draw the item window
	if (LUA_HudEnabled(hud_item) && !freecam)
		K_drawKartItem();

	// If not splitscreen, draw...
	if (!r_splitscreen && !demo.title)
	{
		// Draw the timestamp
		// this branching code sucks now gotta fix later :,)
		if (LUA_HudEnabled(hud_time))
		{
			if (!(K_GetRaceSplitsToggle() || K_doDrawSplits()))
			{
				K_drawKartTimestamp(stplyr->realtime, TIME_X, TIME_Y, gamemap-1, 0);
			}
			else
			{
				// Draw splits
				if (cv_lapemblemmode.value)
				{
					if (K_doDrawSplits())
						K_drawLapSplitTimestamp();
					else
						K_drawKartTimestamp(stplyr->realtime, TIME_X, TIME_Y, gamemap-1, 0);
				}
				else
				{
					K_drawKartTimestamp(stplyr->realtime, TIME_X, TIME_Y, gamemap-1, 0);
					// mine: moved this here to avoid redundant comparisions
					K_drawLapSplitComparison();
				}
			}
		}

		islonesome = K_drawKartPositionFaces();

		RR_drawRidersFinishTicker();
	}

	if (!stplyr->spectator && !freecam) // Bottom of the screen elements, don't need in spectate mode
	{
		// get gametype info visibility ahead of time
		gameinfovisible = (!K_DisableGametypeInfo());

		if (demo.title) // Draw title logo instead in demo.titles
		{
			int32_t x = (BASEVIDWIDTH - 32), y = 128, snapflags = V_SNAPTOBOTTOM|V_SNAPTORIGHT;

			if (r_splitscreen == 3)
			{
				x = BASEVIDWIDTH/2 + 10;
				y = BASEVIDHEIGHT/2 - 30;
				snapflags = 0;
			}

			V_DrawTinyScaledPatch(x-54, y, snapflags|V_SLIDEIN, W_CachePatchName("TTKBANNR", PU_CACHE));
			V_DrawTinyScaledPatch(x-54, y+25, snapflags|V_SLIDEIN, W_CachePatchName("TTKART", PU_CACHE));
		}
		else
		{
			if (LUA_HudEnabled(hud_position))
			{
				if (bossinfo.boss)
				{
					K_drawBossHealthBar();
				}
				else if (gametypes[gametype]->rules & GTR_CIRCUIT) // Race-only elements
				{
					if (!islonesome)
					{
						// Draw the numerical position
						K_DrawKartPositionNum(stplyr->position);
					}
				}
			}

			if (LUA_HudEnabled(hud_gametypeinfo))
			{
				if (gametypes[gametype]->rules & GTR_CIRCUIT)
				{
					if (gameinfovisible)
						K_drawKartLaps();

					K_drawKartStatsnLives();
				}
				else if (gametypes[gametype]->rules & GTR_BUMPERS)
				{
					if (gameinfovisible)
						K_drawKartBumpersOrKarma();
				}
			}

			// Draw the speedometer and/or accessibility icons
			if (cv_kartspeedometer.value && !r_splitscreen && (LUA_HudEnabled(hud_speedometer)))
			{
				K_drawKartSpeedometer();
			}
			else
			{
				K_drawKartAccessibilityIcons((cv_newspeedometer.value == 0 || cv_newspeedometer.value == 2) ? 50 : 0);
			}

			// Draw the player's rank score
			if (!(dedicated || cannotrank))
			{
				const skincolornum_t ranktier_color =
				    (stplyr->ranktier >= RTIER_ONESTAR) ? SKINCOLOR_GOLD
									: SKINCOLOR_SILVER;
				uint8_t* cmap =
				    (stplyr->ranklog.prankeligible && stplyr->ranktier == RTIER_P)
					? R_GetTranslationColormap(
					      TC_RAINBOW, K_SMonitorColor(leveltime / 2), GTC_CACHE)
					: R_GetTranslationColormap(
					      TC_RAINBOW, ranktier_color, GTC_CACHE);

				if (r_splitscreen < 1)
				{
					k_drawKartRaceRank(
					    stplyr,
					    RANK_X,
					    RANK_Y,
					    V_HUDTRANS | V_SNAPTOBOTTOM | V_SNAPTOLEFT,
					    cmap);
				}
				else if (r_splitscreen == 1)
				{
					k_drawKartRaceRank(stplyr,
							   RANK_X,
							   RANK_Y,
							   V_SNAPTOBOTTOM | V_SNAPTOLEFT |
							       V_HUDTRANS | V_SPLITSCREEN,
							   cmap);
				}
				else if (r_splitscreen >= 2)
				{
					const dboolean oppositeside = ((stplyrnum & 1) == 1);
					const dboolean onbottom = (stplyrnum >= 2);
					const uint32_t snapflags =
					    (onbottom) ? V_SNAPTOBOTTOM : V_SNAPTOTOP;

					k_drawKartRaceRank(stplyr,
							   (oppositeside) ? RANK2_X : RANK_X,
							   (onbottom) ? RANK2_Y : RANK_Y,
							   V_HUDTRANS | snapflags,
							   cmap);
				}
			}

			if (LUA_HudEnabled(hud_rings) && K_RingsActive() == true)
			{
				K_drawRingMeter();
			}

			if (cv_showinput.value > 0)
			{
				// Draw the input UI
				if (LUA_HudEnabled(hud_position))
					K_drawInput();
			}
		}
	}

	// Draw the countdowns after everything else.
	if (!(gametypes[gametype]->rules & GTR_NOCOUNTDOWN) && starttime != introtime
	&& leveltime >= introtime
	&& leveltime < starttime+TICRATE)
	{
		K_drawKartStartCountdown();
	}
	else if (racecountdown && (!r_splitscreen || !stplyr->exiting))
	{
		char *countstr = va("%d", racecountdown/TICRATE);

		if (r_splitscreen > 1)
			V_DrawCenteredString(BASEVIDWIDTH/4, LAPS_Y+1, V_SPLITSCREEN, countstr);
		else
		{
			int32_t karlen = strlen(countstr)*6; // half of 12
			V_DrawKartString((BASEVIDWIDTH/2)-karlen, LAPS_Y+3, V_SNAPTOBOTTOM, countstr);
		}
	}

	// Race overlays
	if ((gametypes[gametype]->rules & GTR_CIRCUIT) && !freecam)
	{
		if (stplyr->exiting)
			K_drawKartFinish();
		else if (stplyr->karthud[khud_lapanimation] && !r_splitscreen)
			K_drawLapStartAnim();
	}

	K_DisplayItemTimers();
	K_SlipstreamIndicator(r_splitscreen > 1);

	if (modeattacking || freecam) // everything after here is MP and debug only
		return;

	// TODO: Make this slide like the other titlecards
	if ((gametypes[gametype]->rules & GTR_KARMA) && !r_splitscreen && stplyr->karthud[khud_yougotem]) // * YOU GOT EM *
		V_DrawScaledPatch(BASEVIDWIDTH/2 - (kp_yougotem->width/2), 32, V_HUDTRANS, kp_yougotem);

	// Draw FREE PLAY.
	if (islonesome)
		K_drawKartFreePlay();

	if (r_splitscreen == 0 && (stplyr->pflags & PF_WRONGWAY) && ((leveltime / 8) & 1) && !stplyr->exiting)
	{
		V_DrawCenteredString(BASEVIDWIDTH>>1, cv_itemtimers.value && !modeattacking ? 191 : 179, V_REDMAP|V_SNAPTOBOTTOM, "WRONG WAY");
	}

	if ((netgame || cv_mindelay.value) && r_splitscreen && Playing())
	{
		K_drawMiniPing();
	}

	if (cv_kartdebugdistribution.value)
		K_drawDistributionDebugger();

	if (cv_kartdebugcheckpoint.value)
		K_drawCheckpointDebugger();

	if (cv_kartdebugnodes.value)
	{
		uint8_t p;
		for (p = 0; p < MAXPLAYERS; p++)
			V_DrawString(8, 64+(8*p), V_YELLOWMAP, va("%d - %d (%dl)", p, playernode[p], players[p].cmd.latency));
	}

	if (cv_kartdebugcolorize.value && stplyr->mo && stplyr->mo->skin)
	{
		int32_t x = 0, y = 0;
		uint16_t c;

		for (c = 0; c < numskincolors; c++)
		{
			if (skincolors[c].accessible)
			{
				uint8_t *cm = R_GetTranslationColormap(TC_RAINBOW, c, GTC_CACHE);
				V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, FRACUNIT>>1, 0, faceprefix[stplyr->skin][FACE_WANTED], cm);

				x += 16;
				if (x > BASEVIDWIDTH-16)
				{
					x = 0;
					y += 16;
				}
			}
		}
	}

	K_DrawWaypointDebugger();
	K_DrawClusterDebugger();
	K_DrawProgressDebugger();
	K_DrawDirectorDebugger();

	if (stplyrnum == 0)
		K_DrawBotDebugger(stplyr);
}

// Thank you Haya....

// DRIFT GAUGE //

static int32_t driftskins[] =
{
	SKINCOLOR_BLACK,
	SKINCOLOR_SILVER,
	SKINCOLOR_BLUEBELL,
	SKINCOLOR_RASPBERRY,
	SKINCOLOR_VIOLET,
};

enum driftgauge_e
{
	DGAUGE_NONE = 0,
	DGAUGE_SPEE,
	DGAUGE_ACHII,
	DGAUGE_WIFI,
	DGAUGE_CHAOTIX,
	DGAUGE_NUMBERS,
	DGAUGE_LEGACY,
	DGAUGE_LEGACYSMALL,
	DGAUGE_LEGACYLARGE,
	DGAUGE_LEGACYNUMBERS,
};

typedef struct
{
	uint8_t patchnum;
	int16_t barx, bary;
	int16_t textx, texty;
	int16_t barwidth;
	int32_t meterfont;
	dboolean legacy;
} driftgauge_t;

static uint8_t legacydriftcolors[5][4] = {
	{  23,  23,  23,  23}, // back
	{   1,   1,  10,  16}, // white
	{ 133, 133, 172, 197}, // blue
	{  33,  33, 215,  71}, // red
	{ 161, 161, 196, 198}, // purple
};

static uint8_t driftcolors[5][4] = {
	{  24,  20,  24}, // back (extracted directly from generated TC_RAINBOW + SKINCOLOR_BLACK colormap)
	{   9,   2,   9}, // white
	{ 135, 130, 135}, // blue
	{  34,  32,  34}, // red
	{ 163, 161, 163}, // purple
};

static driftgauge_t driftgauges[] =
{
	[DGAUGE_SPEE] = {
		.patchnum = 0,
		.barx = -22, .bary = 2,
		.textx = 3, .texty = 6,
		.barwidth = 46,
		.meterfont = OPPRNK_FONT,
	},
	[DGAUGE_ACHII] = {
		.patchnum = 1,
		.barx = -22, .bary = 2,
		.textx = 3, .texty = 6,
		.barwidth = 46,
		.meterfont = OPPRNK_FONT,
	},
	[DGAUGE_WIFI] = {
		.patchnum = 2,
		.barx = 6 + 2, .bary = -8 + 22,
		.textx = 30, .texty = 10,
		.barwidth = 1, // special-cased
		.meterfont = PINGNUM_FONT,
	},
	[DGAUGE_CHAOTIX] = {
		.patchnum = 4,
		.barx = -23, .bary = -7,
		.textx = -18, .texty = -8,
		.barwidth = 34,
		.meterfont = OPPRNK_FONT,
	},
	[DGAUGE_NUMBERS] = {
		.textx = 0, .texty = 0,
		.meterfont = OPPRNK_FONT,
	},
	[DGAUGE_LEGACY] = {
		.patchnum = 5,
		.barx = -23, .bary = -1,
		.textx = 30, .texty = 0,
		.barwidth = 46,
		.meterfont = PINGNUM_FONT,
		.legacy = true,
	},
	[DGAUGE_LEGACYSMALL] = {
		.patchnum = 6,
		.barx = -23, .bary = -1,
		.textx = 20, .texty = 0,
		.barwidth = 23,
		.meterfont = PINGNUM_FONT,
		.legacy = true,
	},
	[DGAUGE_LEGACYLARGE] = {
		.patchnum = 5,
		.barx = -23, .bary = -1,
		.textx = 30, .texty = 0,
		.barwidth = 46,
		.meterfont = TALLNUM_FONT,
		.legacy = true,
	},
	[DGAUGE_LEGACYNUMBERS] = {
		.textx = 10, .texty = 0,
		.meterfont = TALLNUM_FONT,
	},
};

static uint8_t driftrainbow[] = {
	0, 32, 48, 64, 72, 80, 88, 96, 112, 120, 128, 144, 160, 176, 192, 200, 208, 216, 224, 240
};

static uint8_t wifibars[] = { 6, 11, 16, 21 };

// Based off https://github.com/GenericHeroGuy/ringracers-scripts/blob/master/src/sglua/Lua/HUD/driftgauge.lua
// Original script by GenericHeroGuy, graphics by Spee
void K_DrawDriftGauge(void)
{
	// Actually have it enabled?
	if (!cv_driftgauge.value)
		return;
	// Not in RA
	if (modeattacking != ATTACKING_NONE)
		return;
	// Make sure we actually have one, lmao
	if (stplyr->mo == NULL || P_MobjWasRemoved(stplyr->mo))
		return;
	// Check for chasecam
	// TODO: Check for this better ffs
	if (!cv_chasecam[R_GetViewNumber()].value)
		return;
	// I WANT TO LIVE
	if (stplyr->playerstate != PST_LIVE)
		return;

	mobj_t *mo = stplyr->mo;
	fixed_t gaugeofs = FixedMul(cv_driftgaugeoffset.value, ((cv_driftgaugeoffset.value > 0) ? mo->scale : mapobjectscale));
	vector3_t pos = {
		R_InterpolateFixed(mo->old_x, mo->x) + mo->sprxoff,
		R_InterpolateFixed(mo->old_y, mo->y) + mo->spryoff,
		R_InterpolateFixed(mo->old_z, mo->z) + mo->sprzoff + (mo->eflags & MFE_VERTICALFLIP ? mo->height - gaugeofs : gaugeofs),
	};
	trackingResult_t res;
	int32_t basex, basey, i = 0;
	int32_t flags = V_SPLITSCREEN; // V_HUDTRANS does not like other transparent effects...

	K_ObjectTracking(&res, &pos, false);
	basex = res.x;
	basey = res.y;

	driftgauge_t *gauge = &driftgauges[cv_driftgauge.value];
	// Deal with Wifi-Style when no purps
	int8_t holdupoffset = ((cv_driftgauge.value == DGAUGE_WIFI) && K_PurpleDriftActive()) + (K_UseColorHud() ? 1 : 0);
	patch_t *basepatch = gauge->barwidth ? kp_driftgauge[gauge->patchnum + (!!K_UseColorHud() * 6) + holdupoffset] : NULL;
	fixed_t textx = basex + gauge->textx*FRACUNIT;
	fixed_t texty = basey + gauge->texty*FRACUNIT;

	// TODO: fix the patch offsets
	if (cv_driftgauge.value == DGAUGE_LEGACYSMALL)
		basex += 10*FRACUNIT;

	// TODO: fix wifi offsets
	if (cv_driftgauge.value == DGAUGE_WIFI)
		basex -= 10*FRACUNIT;

	int32_t driftval = K_GetKartDriftSparkValue(stplyr);
	int32_t driftcharge = min(driftval*4, stplyr->driftcharge);
	dboolean rainbow = driftcharge >= driftval*4;
	const uint8_t level = K_GetKartDriftSparkStageForValue(stplyr, driftcharge) + 1;
	const uint8_t backlevel = rainbow ? 4 : max(0, level - 1);
	uint8_t *cmap = R_GetTranslationColormap(TC_RAINBOW, rainbow ? (int32_t)(1 + (leveltime % FIRSTSUPERCOLOR)) : driftskins[level], GTC_CACHE);

	int32_t afterimage = stplyr->karthud[khud_afterimagetime];
	if (!stplyr->drift && !stplyr->airthrustdir)
	{
		if (afterimage)
			goto doafterimage;
		else
			return;
	}

	if (basepatch)
	{
		uint8_t *backcmap, *basecmap = K_UseColorHud() ? R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE) : NULL;
		uint8_t frontcolors[4], backcolors[4];

		// the base graphic
		V_DrawFixedPatch(basex, basey, FRACUNIT, flags|V_HUDTRANS, basepatch, basecmap);

		if (rainbow)
		{
			int32_t backsize = sizeof(backcolors);
			backcmap = cmap;
			for (i = 0; i < backsize; i++)
				backcolors[i] = driftrainbow[leveltime % sizeof(driftrainbow)] + i;

			// HOT HOT HOT HOT HOOOOOOOT AAAAIIIIIIIIEEEEEEEEEEEEEEEEE
			uint8_t trans = abs(FINESINE((leveltime*ANGLE_22h)>>ANGLETOFINESHIFT)/(4*FRACUNIT/10));
			V_DrawFixedPatch(basex, basey, FRACUNIT, flags|(V_90TRANS - V_10TRANS*trans), basepatch, R_GetTranslationColormap(TC_BLINK, SKINCOLOR_RED, GTC_CACHE));
		}
		else
		{
			uint8_t (*barcolors)[4] = gauge->legacy ? legacydriftcolors : driftcolors;
			memcpy(frontcolors, barcolors[level], sizeof(frontcolors));

			if (backlevel == 0 && K_UseColorHud())
			{
				backcmap = R_GetTranslationColormap(TC_RAINBOW, stplyr->skincolor, GTC_CACHE);
				if (gauge->legacy)
					memset(backcolors, skincolors[K_GetHudColor()].ramp[7], sizeof(backcolors));
				else for (i = 0; i < 3; i++)
					backcolors[i] = skincolors[K_GetHudColor()].ramp[i == 1 ? 9 : 12];
			}
			else
			{
				memcpy(backcolors, barcolors[backlevel], sizeof(backcolors));
				backcmap = R_GetTranslationColormap(TC_RAINBOW, driftskins[backlevel], GTC_CACHE);
			}
		}

		const fixed_t barx = basex + gauge->barx*FRACUNIT;
		const fixed_t bary = basey + gauge->bary*FRACUNIT;
		const fixed_t barwidth = (cv_driftgauge.value == DGAUGE_WIFI ? wifibars[backlevel] : gauge->barwidth)*FRACUNIT;
		const int32_t chargewidth = FixedMul(barwidth, FixedDiv(
			driftcharge - K_GetKartDriftSparkValueForStage(stplyr, backlevel), // charge remaining
			K_GetKartDriftSparkValueForStage(stplyr, level) - K_GetKartDriftSparkValueForStage(stplyr, backlevel) // stage total
		));

		if (cv_driftgauge.value == DGAUGE_WIFI)
		{
			int8_t levels = K_PurpleDriftActive() ? 4 : 3;
			for (i = 0; i < levels; i++)
			{
				fixed_t h = backlevel < i ? 0 : backlevel > i ? wifibars[i]*FRACUNIT : chargewidth;
				V_SetClipRect(barx + (i*5)*FRACUNIT, bary-h, 4*FRACUNIT, wifibars[i]*FRACUNIT, flags);
				V_DrawFixedPatch(basex, basey, FRACUNIT, flags, kp_driftgaugeparts[i], cmap);
			}
		}
		else if (cv_driftgauge.value == DGAUGE_CHAOTIX)
		{
			// back
			if (backlevel)
				V_DrawFixedPatch(basex, basey, FRACUNIT, flags, kp_driftgaugeparts[4], backcmap);

			// front
			V_SetClipRect(barx, bary, chargewidth, 18*FRACUNIT, flags);
			V_DrawFixedPatch(basex, basey, FRACUNIT, flags, kp_driftgaugeparts[4], cmap);
		}
		else // Meter style
		{
			for (i = 0; i < (gauge->legacy ? 4 : 3); i++)
			{
				fixed_t ofs = gauge->legacy ? 0 : (i & 1)*FRACUNIT;
				fixed_t x = barx;
				fixed_t y = bary + i*FRACUNIT;
				fixed_t w = chargewidth;
				fixed_t h = FRACUNIT;

				// seamlessly clipped bars!
				V_SetClipRect(x + max(ofs, w), y, (barwidth - max(ofs, w)) - ofs, h, flags);
				V_DrawFixedFill(x, y, barwidth, h, flags|V_HUDTRANS|backcolors[i]);
				V_SetClipRect(x + ofs, y, min(w - ofs, barwidth - ofs*2), h, flags);
				V_DrawFixedFill(x, y, barwidth, h, flags|V_HUDTRANS|frontcolors[i]);
			}
		}

		V_ClearClipRect();
	}

doafterimage:;
	// right, also draw a cool number
	int32_t charge = (afterimage ? stplyr->karthud[khud_afterimagevalue] : driftcharge)*100 / driftval;
	if (afterimage)
	{
		flags |= V_TRANSLUCENT*2 - (V_10TRANS * afterimage);
		cmap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_GREY, GTC_CACHE);
	}

	if (gauge->meterfont == OPPRNK_FONT)
	{
		uint8_t numbers[3];
		numbers[0] = ((charge / 100) % 10);
		numbers[1] = ((charge / 10) % 10);
		numbers[2] = (charge % 10);
		V_DrawFixedPatch(textx, texty, FRACUNIT, flags, kp_facenum[numbers[0]], cmap);
		V_DrawFixedPatch(textx+(6*FRACUNIT), texty, FRACUNIT, flags, kp_facenum[numbers[1]], cmap);
		V_DrawFixedPatch(textx+(12*FRACUNIT), texty, FRACUNIT, flags, kp_facenum[numbers[2]], cmap);
	}
	else if (gauge->meterfont == PINGNUM_FONT)
	{
		// TODO pad with zeroes(?)
		V_DrawPingNumAtFixed(textx, texty, flags, charge, cmap);
	}
	else if (gauge->meterfont == TALLNUM_FONT)
	{
		V_DrawPaddedTallColorNumAtFixed(textx, texty, flags, charge, 3, cmap);
	}

	/*V_DrawStringScaledEx(
		textx, texty,
		FRACUNIT, FRACUNIT, FRACUNIT, FRACUNIT,
		flags|V_MONOSPACE, cmap,
		meterfont,
		va("%03d", charge)
	);*/
}

void K_DrawFlamometer(void)
{
	// Make sure we actually have one, lmao
	if (stplyr->mo == NULL || P_MobjWasRemoved(stplyr->mo))
		return;
	// Check for chasecam
	// TODO: Check for this better ffs
	if (!cv_chasecam[R_GetViewNumber()].value)
		return;
	// I WANT TO LIVE
	if (stplyr->playerstate != PST_LIVE)
		return;

	if (stplyr->exiting)
		return;

	if (stplyr->flametimer <= 0)
		return;

	mobj_t *mo = stplyr->mo;
	vector3_t pos = {
		R_InterpolateFixed(mo->old_x, mo->x) + mo->sprxoff,
		R_InterpolateFixed(mo->old_y, mo->y) + mo->spryoff,
		R_InterpolateFixed(mo->old_z, mo->z) + mo->sprzoff + (mo->eflags & MFE_VERTICALFLIP ? mo->height : 0),
	};
	trackingResult_t res;
	int32_t basex, basey, barextraflags = 0;
	int32_t flags = V_SPLITSCREEN|V_HUDTRANS;
	int32_t fuelbarheight = 41;
	int32_t tempbarheight = 49;
	uint8_t *colormap = NULL;

	K_ObjectTracking(&res, &pos, false);
	basex = res.x + (16<<FRACBITS);
	basey = res.y - (52<<FRACBITS);

	if (K_UseColorHud())
	{
		colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
	}
	
	// fire animation
	if (stplyr->flamestore >= FLAMESTOREMAX-1)
	{
		uint8_t flamofiretic = ((leveltime / 3) % MAXFLAMOFIRETICS) + 1;

		if (stplyr->flameoverheat < 3)
		{
			// Fancy "explode" VFX
			flamofiretic = 0;
		}

		if (K_UseColorHud())
		{
			flamofiretic += 9;
		}

		V_DrawFixedPatch(basex, basey, FRACUNIT, flags|V_ADD, kp_flamefire[flamofiretic], colormap);

		if (leveltime % 3 != 0)
		{
			barextraflags = V_ADD;
		}
	}

	// back
	V_DrawFixedPatch(basex, basey, FRACUNIT, flags, kp_flamometer[K_UseColorHud() ? 4 : 0], colormap);
	
	// bars
	// fuel
	fuelbarheight *= FixedDiv(stplyr->flametimer, itemtime*3);
	V_SetClipRect(basex + 7*FRACUNIT, basey + 58*FRACUNIT - fuelbarheight, 4*FRACUNIT, fuelbarheight, flags);
	V_DrawFixedPatch(basex, basey, FRACUNIT, flags, kp_flamometer[1], NULL);
	
	V_ClearClipRect();
	
	// temperature
	tempbarheight *= FixedDiv(stplyr->flamestore, FLAMESTOREMAX);
	V_SetClipRect(basex + 10*FRACUNIT, basey + 62*FRACUNIT - tempbarheight, 32*FRACUNIT, tempbarheight, flags);
	V_DrawFixedPatch(basex, basey, FRACUNIT, flags|barextraflags, kp_flamometer[2], NULL);

	V_ClearClipRect();

	// front
	V_DrawFixedPatch(basex, basey, FRACUNIT, flags, kp_flamometer[K_UseColorHud() ? 5 : 3], colormap);
}
