// SONIC ROBO BLAST 2 KART
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
#include "info.h"
#include "k_kart.h"
#include "k_battle.h"
#include "k_grandprix.h"
#include "k_boss.h"
#include "k_color.h"
#include "k_director.h"
#include "p_mobj.h"
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
#include "p_local.h"
#include "z_zone.h"
#include "m_cond.h"
#include "r_main.h"
#include "s_sound.h"
#include "r_things.h"
#include "r_fps.h"
#include "m_random.h"
#include "g_party.h"

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
IMPL_HUD_OFFSET(posi); // Position in race
IMPL_HUD_OFFSET(face); // Mini rankings
IMPL_HUD_OFFSET(stcd); // Starting countdown
IMPL_HUD_OFFSET_Y(chek); // Check gfx
IMPL_HUD_OFFSET(mini); // Minimap
IMPL_HUD_OFFSET(want); // Wanted
//IMPL_HUD_OFFSET(stat); // Stats

#undef IMPL_HUD_OFFSET
#undef IMPL_HUD_OFFSET_X
#undef IMPL_HUD_OFFSET_Y

static CV_PossibleValue_t speedo_cons_t[]= {
	{0, "Default"},
	{1, "Small"},
	//{2, "P-Meter"},  Readded later need to draw smaller sprites.
	{0, NULL}};
consvar_t cv_newspeedometer = CVAR_INIT ("newspeedometer", "Small", CV_SAVE, speedo_cons_t, NULL);

static CV_PossibleValue_t inputdisplay_cons_t[] = {{0, "Off"}, {1, "Wheel"}, {2, "Stick"}, {3, "SRB2"}, {0, NULL}};

consvar_t cv_showinput = CVAR_INIT ("showinput", "Off", CV_SAVE, inputdisplay_cons_t, NULL);

consvar_t cv_colorizedhud = CVAR_INIT ("colorizedhud", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_colorizeditembox = CVAR_INIT ("colorizeditembox", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_darkitembox = CVAR_INIT ("darkitembox", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_showfinishedplayers = CVAR_INIT ("showfinishedplayers", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_fancyroulette = CVAR_INIT ("fancyroulette", "Off", CV_SAVE, CV_OnOff, NULL);

static CV_PossibleValue_t HudColor_cons_t[MAXSKINCOLORS+1];
consvar_t cv_colorizedhudcolor = CVAR_INIT ("colorizedhudcolor", "Skin Color", CV_SAVE, HudColor_cons_t, NULL);

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

static patch_t *kp_splitlapflag;
static patch_t *kp_splitkarmabomb;

static patch_t *kp_startcountdown[20];
static patch_t *kp_racefinish[6];

static patch_t *kp_positionnum[NUMPOSNUMS][NUMPOSFRAMES];
static patch_t *kp_winnernum[NUMPOSFRAMES];

static patch_t *kp_facenum[MAXPLAYERS+1];
static patch_t *kp_facehighlight[8];

static patch_t *kp_nocontestminimap;
static patch_t *kp_spbminimap;
static patch_t *kp_itemboxminimap;

static patch_t *kp_ringsticker[5];
static patch_t *kp_ringsplitscreen;
static patch_t *kp_ringdebtminus;
static patch_t *kp_ringdebtminussmall;

static patch_t *kp_speedometersticker[2];
static patch_t *kp_speedometerlabel[4];
static patch_t *kp_kartzspeedo[25];

static patch_t *kp_rankbumper;
static patch_t *kp_tinybumper[2];
static patch_t *kp_ranknobumpers;
static patch_t *kp_rankcapsule;

static patch_t *kp_battlewin;
static patch_t *kp_battlecool;
static patch_t *kp_battlelose;
static patch_t *kp_battlewait;
static patch_t *kp_battleinfo;
static patch_t *kp_wanted;
static patch_t *kp_wantedsplit;
static patch_t *kp_wantedreticle;
static patch_t *kp_minimapdot;

static patch_t *kp_itembg[8];
static patch_t *kp_itemtimer[2];
static patch_t *kp_itemmulsticker[4];
static patch_t *kp_itemx;

static patch_t *kp_sadface[2];
static patch_t *kp_sneaker[4];
static patch_t *kp_rocketsneaker[2];
static patch_t *kp_invincibility[13];
static patch_t *kp_banana[5];
static patch_t *kp_eggman[2];
static patch_t *kp_orbinaut[5];
static patch_t *kp_jawz[3];
static patch_t *kp_mine[2];
static patch_t *kp_ballhog[2];
static patch_t *kp_selfpropelledbomb[2];
static patch_t *kp_grow[2];
static patch_t *kp_shrink[2];
static patch_t *kp_thundershield[2];
static patch_t *kp_hyudoro[2];
static patch_t *kp_pogospring[2];
static patch_t *kp_kitchensink[2];
static patch_t *kp_superring[2];
static patch_t *kp_landmine[2];
static patch_t *kp_bubbleshield[2];
static patch_t *kp_flameshield[2];

static patch_t *kp_check[6];

static patch_t *kp_eggnum[4];

static patch_t *kp_fpview[3];
static patch_t *kp_inputwheel[5];

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
	REG_HUD_OFFSET(posi); // Position in race
	REG_HUD_OFFSET(face); // Mini rankings
	REG_HUD_OFFSET(stcd); // Starting countdown
	REG_HUD_OFFSET_Y(chek); // Check gfx
	REG_HUD_OFFSET(mini); // Minimap
	REG_HUD_OFFSET(want); // Wanted
	//REG_HUD_OFFSET(stat); // Stats

#undef REG_HUD_OFFSET
#undef REG_HUD_OFFSET_X
#undef REG_HUD_OFFSET_Y

	CV_RegisterVar(&cv_newspeedometer);
	CV_RegisterVar(&cv_showinput);
	CV_RegisterVar(&cv_colorizedhud);
	CV_RegisterVar(&cv_colorizedhudcolor);
	CV_RegisterVar(&cv_colorizeditembox);
	CV_RegisterVar(&cv_darkitembox);
	CV_RegisterVar(&cv_showfinishedplayers);
	CV_RegisterVar(&cv_fancyroulette);
}

void K_LoadKartHUDGraphics(void)
{
	INT32 i, j;
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
	sprintf(buffer, "K_POSNxx");
	for (i = 0; i < NUMPOSNUMS; i++)
	{
		buffer[6] = '0'+i;
		for (j = 0; j < NUMPOSFRAMES; j++)
		{
			//sprintf(buffer, "K_POSN%d%d", i, j);
			buffer[7] = '0'+j;
			HU_UpdatePatch(&kp_positionnum[i][j], "%s", buffer);
		}
	}

	sprintf(buffer, "K_POSNWx");
	for (i = 0; i < NUMWINFRAMES; i++)
	{
		buffer[7] = '0'+i;
		HU_UpdatePatch(&kp_winnernum[i], "%s", buffer);
	}

	sprintf(buffer, "OPPRNKxx");
	for (i = 0; i <= MAXPLAYERS; i++)
	{
		buffer[6] = '0'+(i/10);
		buffer[7] = '0'+(i%10);
		HU_UpdatePatch(&kp_facenum[i], "%s", buffer);
	}

	sprintf(buffer, "K_CHILIx");
	for (i = 0; i < 8; i++)
	{
		buffer[7] = '0'+(i+1);
		HU_UpdatePatch(&kp_facehighlight[i], "%s", buffer);
	}

	// Special minimap icons
	HU_UpdatePatch(&kp_nocontestminimap, "MINIDEAD");
	HU_UpdatePatch(&kp_spbminimap, "K_SPTKRM");
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
			kp_kartzspeedo[m] = W_CachePatchLongName(patchNames[m], PU_HUDGFX);
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
	HU_UpdatePatch(&kp_itembg[4], "K_ITBC");
	HU_UpdatePatch(&kp_itembg[5], "K_ITBCD");
	HU_UpdatePatch(&kp_itemtimer[0], "K_ITIMER");
	HU_UpdatePatch(&kp_itemmulsticker[0], "K_ITMUL");
	HU_UpdatePatch(&kp_itemmulsticker[2], "K_ITMULC");
	HU_UpdatePatch(&kp_itemx, "K_ITX");

	HU_UpdatePatch(&kp_sadface[0], "K_ITSAD");
	HU_UpdatePatch(&kp_sneaker[0], "K_ITSHOE");
	HU_UpdatePatch(&kp_sneaker[1], "K_ITSHO2");
	HU_UpdatePatch(&kp_sneaker[2], "K_ITSHO3");
	HU_UpdatePatch(&kp_rocketsneaker[0], "K_ITRSHE");

	sprintf(buffer, "K_ITINVx");
	for (i = 0; i < 7; i++)
	{
		buffer[7] = '1'+i;
		HU_UpdatePatch(&kp_invincibility[i], "%s", buffer);
	}
	HU_UpdatePatch(&kp_banana[0], "K_ITBANA");
	HU_UpdatePatch(&kp_banana[1], "K_ITBAN2");
	HU_UpdatePatch(&kp_banana[2], "K_ITBAN3");
	HU_UpdatePatch(&kp_banana[3], "K_ITBAN4");
	HU_UpdatePatch(&kp_eggman[0], "K_ITEGGM");
	sprintf(buffer, "K_ITORBx");
	for (i = 0; i < 4; i++)
	{
		buffer[7] = '1'+i;
		HU_UpdatePatch(&kp_orbinaut[i], "%s", buffer);
	}
	HU_UpdatePatch(&kp_jawz[0], "K_ITJAWZ");
	HU_UpdatePatch(&kp_jawz[1], "K_ITJAW2");
	HU_UpdatePatch(&kp_mine[0], "K_ITMINE");
	HU_UpdatePatch(&kp_ballhog[0], "K_ITBHOG");
	HU_UpdatePatch(&kp_selfpropelledbomb[0], "K_ITSPB");
	HU_UpdatePatch(&kp_grow[0], "K_ITGROW");
	HU_UpdatePatch(&kp_shrink[0], "K_ITSHRK");
	HU_UpdatePatch(&kp_thundershield[0], "K_ITTHNS");
	HU_UpdatePatch(&kp_hyudoro[0], "K_ITHYUD");
	HU_UpdatePatch(&kp_pogospring[0], "K_ITPOGO");
	HU_UpdatePatch(&kp_kitchensink[0], "K_ITSINK");
	HU_UpdatePatch(&kp_superring[0], "K_ITRING");
	HU_UpdatePatch(&kp_landmine[0], "K_ITLNDM");
	HU_UpdatePatch(&kp_bubbleshield[0], "K_ITBUBS");
	HU_UpdatePatch(&kp_flameshield[0], "K_ITFLMS");

	// Splitscreen
	HU_UpdatePatch(&kp_itembg[2], "K_ISBG");
	HU_UpdatePatch(&kp_itembg[3], "K_ISBGD");
	HU_UpdatePatch(&kp_itembg[6], "K_ISBC");
	HU_UpdatePatch(&kp_itembg[7], "K_ISBCD");
	HU_UpdatePatch(&kp_itemtimer[1], "K_ISIMER");
	HU_UpdatePatch(&kp_itemmulsticker[1], "K_ISMUL");
	HU_UpdatePatch(&kp_itemmulsticker[3], "K_ISMULC");

	HU_UpdatePatch(&kp_sadface[1], "K_ISSAD");
	HU_UpdatePatch(&kp_sneaker[3], "K_ISSHOE");
	HU_UpdatePatch(&kp_rocketsneaker[1], "K_ISRSHE");
	sprintf(buffer, "K_ISINVx");
	for (i = 0; i < 6; i++)
	{
		buffer[7] = '1'+i;
		HU_UpdatePatch(&kp_invincibility[i+7], "%s", buffer);
	}
	HU_UpdatePatch(&kp_banana[4], "K_ISBANA");
	HU_UpdatePatch(&kp_eggman[1], "K_ISEGGM");
	HU_UpdatePatch(&kp_orbinaut[4], "K_ISORBN");
	HU_UpdatePatch(&kp_jawz[2], "K_ISJAWZ");
	HU_UpdatePatch(&kp_mine[1], "K_ISMINE");
	HU_UpdatePatch(&kp_ballhog[1], "K_ISBHOG");
	HU_UpdatePatch(&kp_selfpropelledbomb[1], "K_ISSPB");
	HU_UpdatePatch(&kp_grow[1], "K_ISGROW");
	HU_UpdatePatch(&kp_shrink[1], "K_ISSHRK");
	HU_UpdatePatch(&kp_thundershield[1], "K_ISTHNS");
	HU_UpdatePatch(&kp_hyudoro[1], "K_ISHYUD");
	HU_UpdatePatch(&kp_pogospring[1], "K_ISPOGO");
	HU_UpdatePatch(&kp_kitchensink[1], "K_ISSINK");
	HU_UpdatePatch(&kp_superring[1], "K_ISRING");
	HU_UpdatePatch(&kp_landmine[1], "K_ISLNDM");
	HU_UpdatePatch(&kp_bubbleshield[1], "K_ISBUBS");
	HU_UpdatePatch(&kp_flameshield[1], "K_ISFLMS");

	// CHECK indicators
	sprintf(buffer, "K_CHECKx");
	for (i = 0; i < 6; i++)
	{
		buffer[7] = '1'+i;
		HU_UpdatePatch(&kp_check[i], "%s", buffer);
	}

	// Eggman warning numbers
	sprintf(buffer, "K_EGGNx");
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
	sprintf(buffer, "K_WHEELx");
	for (i = 0; i < 5; i++)
	{
		buffer[7] = '0'+i;
		HU_UpdatePatch(&kp_inputwheel[i], "%s", buffer);
	}

	// HERE COMES A NEW CHALLENGER
	sprintf(buffer, "K_CHALxx");
	for (i = 0; i < 25; i++)
	{
		buffer[6] = '0'+((i+1)/10);
		buffer[7] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_challenger[i], "%s", buffer);
	}

	// Lap start animation
	sprintf(buffer, "K_LAP0x");
	for (i = 0; i < 7; i++)
	{
		buffer[6] = '0'+(i+1);
		HU_UpdatePatch(&kp_lapanim_lap[i], "%s", buffer);
	}

	sprintf(buffer, "K_LAPFxx");
	for (i = 0; i < 11; i++)
	{
		buffer[6] = '0'+((i+1)/10);
		buffer[7] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_lapanim_final[i], "%s", buffer);
	}

	sprintf(buffer, "K_LAPNxx");
	for (i = 0; i < 10; i++)
	{
		buffer[6] = '0'+i;
		for (j = 0; j < 3; j++)
		{
			buffer[7] = '0'+(j+1);
			HU_UpdatePatch(&kp_lapanim_number[i][j], "%s", buffer);
		}
	}

	sprintf(buffer, "K_LAPE0x");
	for (i = 0; i < 2; i++)
	{
		buffer[7] = '0'+(i+1);
		HU_UpdatePatch(&kp_lapanim_emblem[i], "%s", buffer);
	}

	sprintf(buffer, "K_LAPH0x");
	for (i = 0; i < 3; i++)
	{
		buffer[7] = '0'+(i+1);
		HU_UpdatePatch(&kp_lapanim_hand[i], "%s", buffer);
	}

	HU_UpdatePatch(&kp_yougotem, "YOUGOTEM");

	sprintf(buffer, "K_BOSB0x");
	for (i = 0; i < 8; i++)
	{
		buffer[7] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_bossbar[i], "%s", buffer);
	}

	sprintf(buffer, "K_BOSR0x");
	for (i = 0; i < 4; i++)
	{
		buffer[7] = '0'+((i+1)%10);
		HU_UpdatePatch(&kp_bossret[i], "%s", buffer);
	}
}

// For the item toggle menu
const char *K_GetItemPatch(UINT8 item, boolean tiny)
{
	switch (item)
	{
		case KITEM_SNEAKER:
			return (tiny ? "K_ISSHOE" : "K_ITSHOE");
		case KITEM_ROCKETSNEAKER:
			return (tiny ? "K_ISRSHE" : "K_ITRSHE");
		case KITEM_INVINCIBILITY:
			return (tiny ? "K_ISINV1" : "K_ITINV1");
		case KITEM_BANANA:
			return (tiny ? "K_ISBANA" : "K_ITBANA");
		case KITEM_EGGMAN:
			return (tiny ? "K_ISEGGM" : "K_ITEGGM");
		case KITEM_ORBINAUT:
			return (tiny ? "K_ISORBN" : "K_ITORB1");
		case KITEM_JAWZ:
			return (tiny ? "K_ISJAWZ" : "K_ITJAWZ");
		case KITEM_MINE:
			return (tiny ? "K_ISMINE" : "K_ITMINE");
		case KITEM_BALLHOG:
			return (tiny ? "K_ISBHOG" : "K_ITBHOG");
		case KITEM_SPB:
			return (tiny ? "K_ISSPB" : "K_ITSPB");
		case KITEM_GROW:
			return (tiny ? "K_ISGROW" : "K_ITGROW");
		case KITEM_SHRINK:
			return (tiny ? "K_ISSHRK" : "K_ITSHRK");
		case KITEM_THUNDERSHIELD:
			return (tiny ? "K_ISTHNS" : "K_ITTHNS");
		case KITEM_HYUDORO:
			return (tiny ? "K_ISHYUD" : "K_ITHYUD");
		case KITEM_POGOSPRING:
			return (tiny ? "K_ISPOGO" : "K_ITPOGO");
		case KITEM_KITCHENSINK:
			return (tiny ? "K_ISSINK" : "K_ITSINK");
		case KITEM_SUPERRING:
			return (tiny ? "K_ISRING" : "K_ITRING");
		case KITEM_LANDMINE:
			return (tiny ? "K_ISLNDM" : "K_ITLNDM");
		case KITEM_BUBBLESHIELD:
			return (tiny ? "K_ISBUBS" : "K_ITBUBS");
		case KITEM_FLAMESHIELD:
			return (tiny ? "K_ISFLMS" : "K_ITFLMS");
		case KRITEM_DUALSNEAKER:
			return (tiny ? "K_ISSHOE" : "K_ITSHO2");
		case KRITEM_TRIPLESNEAKER:
			return (tiny ? "K_ISSHOE" : "K_ITSHO3");
		case KRITEM_TRIPLEORBINAUT:
			return (tiny ? "K_ISORBN" : "K_ITORB3");
		case KRITEM_DUALJAWZ:
			return (tiny ? "K_ISJAWZ" : "K_ITJAW2");
		case KRITEM_TRIPLEBANANA:
			return (tiny ? "K_ISBANA" : "K_ITBAN3");
		case KRITEM_TENFOLDBANANA:
			return (tiny ? "K_ISBANA" : "K_ITBAN4");
		case KRITEM_QUADORBINAUT:
			return (tiny ? "K_ISORBN" : "K_ITORB4");
		default:
			return (tiny ? "K_ISSAD" : "K_ITSAD");
	}
}

static patch_t *K_GetCachedItemPatch(INT32 item, UINT8 offset)
{
	patch_t **kp[1 + NUMKARTITEMS] = {
		kp_sadface,
		NULL,
		kp_sneaker,
		kp_rocketsneaker,
		kp_invincibility,
		kp_banana,
		kp_eggman,
		kp_orbinaut,
		kp_jawz,
		kp_mine,
		kp_ballhog,
		kp_selfpropelledbomb,
		kp_grow,
		kp_shrink,
		kp_thundershield,
		kp_hyudoro,
		kp_pogospring,
		kp_kitchensink,
		kp_superring,
		kp_landmine,
		kp_bubbleshield,
		kp_flameshield,
	};

	if (item == KITEM_SAD || (item > KITEM_NONE && item < NUMKARTITEMS))
		return kp[item - KITEM_SAD][offset];
	else
		return NULL;
}

//}

INT32 ITEM_X, ITEM_Y;	// Item Window
INT32 TIME_X, TIME_Y;	// Time Sticker
INT32 LAPS_X, LAPS_Y;	// Lap Sticker
INT32 RING_X, RING_Y;	// Player Rings
INT32 SPDM_X, SPDM_Y;	// Speedometer
INT32 ACCE_X, ACCE_Y;	// Accessibility
INT32 POSI_X, POSI_Y;	// Position Number
INT32 FACE_X, FACE_Y;	// Top-four Faces
INT32 STCD_X, STCD_Y;	// Starting countdown
INT32 CHEK_Y;			// CHECK graphic
INT32 MINI_X, MINI_Y;	// Minimap
INT32 WANT_X, WANT_Y;	// Battle WANTED poster

// This is for the P2 and P4 side of splitscreen. Then we'll flip P1's and P2's to the bottom with V_SPLITSCREEN.
INT32 ITEM2_X, ITEM2_Y;
INT32 LAPS2_X, LAPS2_Y;
INT32 RING2_X, RING2_Y;
INT32 POSI2_X, POSI2_Y;

void K_ReloadHUDColorCvar(void)
{
	HudColor_cons_t[0].value = 0;
	HudColor_cons_t[0].strvalue = "Skin Color";

	for (INT32 i = 1; i < numskincolors; i++)
	{
		HudColor_cons_t[i].value = i;
		HudColor_cons_t[i].strvalue = skincolors[i].name;				// SRB2kart
	}

	HudColor_cons_t[MAXSKINCOLORS].value = 0;
	HudColor_cons_t[MAXSKINCOLORS].strvalue = NULL;
}


boolean K_UseColorHud(void)
{
	return cv_colorizedhud.value;
}

UINT8 K_GetHudColor(void)
{
	if (cv_colorizedhud.value && cv_colorizedhudcolor.value) return cv_colorizedhudcolor.value;

	return ((stplyr && gamestate == GS_LEVEL) ? stplyr->skincolor : cv_playercolor[0].value);
}

static boolean K_BigLapSticker(void)
{
	return ((cv_numlaps.value > 9) && (!stplyr->exiting));
}

patch_t *K_getItemBoxPatch(boolean small, boolean dark)
{
	UINT8 ofs = (cv_darkitembox.value && dark ? 1 : 0) + (small ? 2 : 0);
	return (cv_colorizeditembox.value && K_UseColorHud()) ? kp_itembg[4+ofs] : kp_itembg[ofs];
}

patch_t *K_getItemMulPatch(boolean small)
{
	UINT8 ofs = small ? 1 : 0;
	return K_UseColorHud() ? kp_itemmulsticker[2+ofs] : kp_itemmulsticker[ofs];
}

// This version of the function was prototyped in Lua by Nev3r ... a HUGE thank you goes out to them!
void K_ObjectTracking(trackingResult_t *result, vector3_t *point, boolean reverse)
{
#define NEWTAN(x) FINETANGENT(((x + ANGLE_90) >> ANGLETOFINESHIFT) & 4095) // tan function used by Lua
#define NEWCOS(x) FINECOSINE((x >> ANGLETOFINESHIFT) & FINEMASK)

	angle_t viewpointAngle, viewpointAiming, viewpointRoll;

	INT32 screenWidth, screenHeight;
	fixed_t screenHalfW, screenHalfH;

	const fixed_t baseFov = 90*FRACUNIT;
	fixed_t fovDiff, fov, fovTangent, fg;

	fixed_t h;
	INT32 da;

	UINT8 cameraNum = R_GetViewNumber();

	I_Assert(result != NULL);
	I_Assert(point != NULL);

	// Initialize defaults
	result->x = result->y = 0;
	result->scale = FRACUNIT;
	result->onScreen = false;

	// Take the view's properties as necessary.
	if (reverse)
	{
		viewpointAngle = (INT32)(viewangle + ANGLE_180);
		viewpointAiming = (INT32)InvAngle(aimingangle);
		viewpointRoll = (INT32)viewroll;
	}
	else
	{
		viewpointAngle = (INT32)viewangle;
		viewpointAiming = (INT32)aimingangle;
		viewpointRoll = (INT32)InvAngle(viewroll);
	}

	// Calculate screen size adjustments.
	screenWidth = vid.width/vid.dupx;
	screenHeight = vid.height/vid.dupy;

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
	fov = ((baseFov - fovDiff) / 2) - (stplyr->fovadd / 2);
	fovTangent = NEWTAN(FixedAngle(fov));

	if (r_splitscreen == 1)
	{
		// Splitscreen FOV is adjusted to maintain expected vertical view
		fovTangent = 10*fovTangent/17;
	}

	fg = (screenWidth >> 1) * fovTangent;

	// Determine viewpoint factors.
	h = R_PointToDist2(point->x, point->y, viewx, viewy);
	da = AngleDeltaSigned(viewpointAngle, R_PointToAngle2(point->x, point->y, viewx, viewy));

	// Set results relative to top left!
	result->x = FixedMul(NEWTAN(da), fg);
	result->y = FixedMul((NEWTAN(viewpointAiming) - FixedDiv((viewz - point->z), 1 + FixedMul(NEWCOS(da), h))), fg);

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

	// Center results.
	result->x += screenHalfW;
	result->y += screenHalfH;

	result->scale = FixedDiv(screenHalfW, h+1);

	result->onScreen = ((abs(da) > ANG60) || (abs(AngleDeltaSigned(viewpointAiming, R_PointToAngle2(0, 0, h, (viewz - point->z)))) > ANGLE_45));

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
	result->x -= ((vid.width/vid.dupx) - BASEVIDWIDTH)<<(FRACBITS-((r_splitscreen >= 2) ? 2 : 1));
	result->y -= ((vid.height/vid.dupy) - BASEVIDHEIGHT)<<(FRACBITS-((r_splitscreen >= 1) ? 2 : 1));

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

	if (r_splitscreen)	// Splitscreen
	{
		ITEM_X = 5;
		ITEM_Y = 3;

		LAPS_Y = (BASEVIDHEIGHT/2)-24;

		RING_Y = (BASEVIDHEIGHT/2)-24;

		SPDM_Y = (BASEVIDHEIGHT/2)-24;

		POSI_Y = (BASEVIDHEIGHT/2)- 2;

		STCD_Y = BASEVIDHEIGHT/4;

		MINI_Y = (BASEVIDHEIGHT/2);

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

			// 2P (top right)
			ITEM2_X = (BASEVIDWIDTH/2)-39;
			ITEM2_Y = -8;

			LAPS2_X = (BASEVIDWIDTH/2)-43;
			LAPS2_Y = (BASEVIDHEIGHT/2)-12;

			RING2_X = (BASEVIDWIDTH/2)-43;
			RING2_Y = (BASEVIDHEIGHT/2)-12;

			POSI2_X = (BASEVIDWIDTH/2)-4;
			POSI2_Y = (BASEVIDHEIGHT/2)-26;

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

void K_getItemBoxDrawinfo(drawinfo_t *out)
{
	INT32 fx, fy, fflags;
	boolean flipamount = false;

	// pain and suffering defined below
	if (r_splitscreen < 2) // don't change shit for THIS splitscreen.
	{
		fx = ITEM_X;
		fy = ITEM_Y;
		fflags = V_SNAPTOTOP|V_SNAPTOLEFT|V_SPLITSCREEN;
	}
	else // now we're having a fun game.
	{
		if (stplyr == &players[displayplayers[0]] || stplyr == &players[displayplayers[2]]) // If we are P1 or P3...
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
}

void K_getLapsDrawinfo(drawinfo_t *out)
{
	INT32 fx, fy, splitflags = 0;

	// pain and suffering defined below
	if (r_splitscreen < 2)	// don't change shit for THIS splitscreen.
	{
		fx = LAPS_X;
		fy = LAPS_Y;
		splitflags = V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANS|V_SPLITSCREEN;
	}
	else
	{
		if (stplyr == &players[displayplayers[0]] || stplyr == &players[displayplayers[2]])	// If we are P1 or P3...
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
	INT32 fx, fy, splitflags = 0;

	// pain and suffering defined below
	if (r_splitscreen < 2)	// don't change shit for THIS splitscreen.
	{
		fx = RING_X;
		fy = RING_Y;
		splitflags = V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANS|V_SPLITSCREEN;
	}
	else
	{
		if (stplyr == &players[displayplayers[0]] || stplyr == &players[displayplayers[2]])	// If we are P1 or P3...
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
	INT32 fx = MINI_X, fy = MINI_Y, fflags = (r_splitscreen < 2 ? V_SNAPTORIGHT : 0);	// flags should only be 0 when it's centered (4p split)

	fx -= SHORT(minimapinfo.minimap_pic->width)/2;
	fy -= SHORT(minimapinfo.minimap_pic->height)/2;

	out->x = fx;
	out->y = fy;
	out->flags = fflags;
}

// see also MT_PLAYERARROW mobjthinker in p_mobj.c
static void K_drawKartItem(void)
{
	// ITEM_X = BASEVIDWIDTH-50;	// 270
	// ITEM_Y = 24;					//  24

	// Why write V_DrawScaledPatch calls over and over when they're all the same?
	// Set to 'no item' just in case.
	const UINT8 offset = ((r_splitscreen > 1) ? 1 : 0);
	patch_t *localpatch = kp_nodraw;
	patch_t *localbg;
	patch_t *localinv = ((offset) ? kp_invincibility[((leveltime % (6*3)) / 3) + 7] : kp_invincibility[(leveltime % (7*3)) / 3]);
	boolean dark = false;
	INT32 fx = 0, fy = 0, fflags = 0;	// final coords for hud and flags...
	INT32 numberdisplaymin = 2;
	INT32 itembar = 0;
	INT32 maxl = 0; // itembar's normal highest value
	INT32 flamebar = 0;
	INT32 flamemaxl = 0; // flamebar's normal highest value
	const INT32 barlength = (r_splitscreen > 1 ? 12 : 26);
	UINT16 localcolor = SKINCOLOR_NONE;
	SINT8 colormode = TC_RAINBOW;
	UINT8 *colmap = NULL;
	UINT8 *colormap = NULL;
	boolean flipamount = false;	// Used for 3P/4P splitscreen to flip item amount stuff

	if (stplyr->itemroulette)
	{
		const INT32 item = K_GetRollingRouletteItem(stplyr);

		if (K_GetHudColor())
			localcolor = K_GetHudColor();

		switch (item)
		{
			case KITEM_SNEAKER:
				localpatch = kp_sneaker[offset ? 3: 0];
				break;
			case KITEM_BANANA:
				localpatch = kp_banana[offset ? 4: 0];
				break;
			case KITEM_ORBINAUT:
				localpatch = kp_orbinaut[3+offset];
				break;
			case KITEM_JAWZ:
				localpatch = kp_jawz[offset ? 2: 0];
				break;
			case KITEM_INVINCIBILITY:
				localpatch = localinv;
				break;
			default:
				localpatch = K_GetCachedItemPatch(item, offset);
		}
	}
	else
	{
		// I'm doing this a little weird and drawing mostly in reverse order
		// The only actual reason is to make sneakers line up this way in the code below
		// This shouldn't have any actual baring over how it functions
		// Hyudoro is first, because we're drawing it on top of the player's current item
		if (stplyr->stolentimer > 0)
		{
			if (leveltime & 2)
				localpatch = kp_hyudoro[offset];
			else
				localpatch = kp_nodraw;
		}
		else if ((stplyr->stealingtimer > 0) && (leveltime & 2))
		{
			localpatch = kp_hyudoro[offset];
		}
		else if (stplyr->eggmanexplode > 1)
		{
			if (leveltime & 1)
				localpatch = kp_eggman[offset];
			else
				localpatch = kp_nodraw;
		}
		else if (stplyr->rocketsneakertimer > 1)
		{
			itembar = stplyr->rocketsneakertimer;
			maxl = (itemtime*3) - barlength;

			if (leveltime & 1)
				localpatch = kp_rocketsneaker[offset];
			else
				localpatch = kp_nodraw;
		}
		else if (stplyr->flametimer > 1)
		{
			itembar = stplyr->flametimer;
			maxl = (itemtime*3) - barlength;
			flamebar = stplyr->flamestore;
			flamemaxl = FLAMESTOREMAX;
			localbg = kp_itembg[offset+1];

			if ((stplyr->flamestore >= FLAMESTOREMAX-1) && (leveltime & 1))
			{
				colormode = TC_BLINK;
				localcolor = SKINCOLOR_WHITE;
			}

			if (leveltime & 1)
				localpatch = kp_flameshield[offset];
			else
				localpatch = kp_nodraw;
		}
		else if (stplyr->growshrinktimer > 0)
		{
			if (stplyr->growcancel > 0)
			{
				itembar = stplyr->growcancel;
				maxl = 26;
			}

			if (leveltime & 1)
				localpatch = kp_grow[offset];
			else
				localpatch = kp_nodraw;
		}
		else if (K_GetShieldFromPlayer(stplyr) == KSHIELD_BUBBLE)
		{
			localpatch = kp_bubbleshield[offset];

			if ((stplyr->bubbleblowup > bubbletime) && (leveltime & 1))
			{
				colormode = TC_BLINK;
				localcolor = SKINCOLOR_WHITE;
			}

			itembar = 2 - stplyr->bubblepop;
			maxl = 2;
		}
		else if (stplyr->sadtimer > 0)
		{
			if (leveltime & 2)
				localpatch = kp_sadface[offset];
			else
				localpatch = kp_nodraw;
		}
		else
		{
			if (stplyr->itemamount <= 0)
				return;

			switch(stplyr->itemtype)
			{
				case KITEM_SNEAKER:
					localpatch = kp_sneaker[(offset ? 3 : min(stplyr->itemamount-1, 2))];
					numberdisplaymin = 4;
					numberdisplaymin = offset ? 2 : 4;
					break;
				case KITEM_INVINCIBILITY:
					localpatch = localinv;
					localbg = kp_itembg[offset+1];
					break;
				case KITEM_BANANA:
					localpatch = kp_banana[(offset ? 4 : min(stplyr->itemamount-1, 3))];
					numberdisplaymin = offset ? 2 : 5;
					break;
				case KITEM_ORBINAUT:
					localpatch = kp_orbinaut[(offset ? 4 : min(stplyr->itemamount-1, 3))];
					numberdisplaymin = offset ? 2 : 5;
					break;
				case KITEM_JAWZ:
					localpatch = kp_jawz[(offset ? 2 : min(stplyr->itemamount-1, 1))];
					numberdisplaymin = 3;
					numberdisplaymin = offset ? 2 : 3;
					break;
				case KITEM_SPB:
				case KITEM_THUNDERSHIELD:
				case KITEM_BUBBLESHIELD:
				case KITEM_FLAMESHIELD:
					dark = true;
					/*FALLTHRU*/

				default:
					localpatch = K_GetCachedItemPatch(stplyr->itemtype, offset);

					if (localpatch == NULL)
						localpatch = kp_nodraw; // diagnose underflows
					break;
			}

			if ((stplyr->itemflags & IF_ITEMOUT) && !(leveltime & 1))
				localpatch = kp_nodraw;
		}

		if (stplyr->itemblink && (leveltime & 1))
		{
			colormode = TC_BLINK;

			switch (stplyr->itemblinkmode)
			{
				case 2:
					localcolor = K_RainbowColor(leveltime);
					break;
				case 1:
					localcolor = SKINCOLOR_RED;
					break;
				default:
					localcolor = SKINCOLOR_WHITE;
					break;
			}
		}
	}

	localbg = K_getItemBoxPatch((boolean)offset, dark);
	drawinfo_t info;
	K_getItemBoxDrawinfo(&info);
	fx = info.x;
	fy = info.y;
	fflags = info.flags;
	flipamount = info.flipamount;
	if (localcolor != SKINCOLOR_NONE)
		colmap = R_GetTranslationColormap(colormode, localcolor, GTC_CACHE);
	if (K_UseColorHud())
		colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);

	V_DrawMappedPatch(fx, fy, V_HUDTRANS|fflags, localbg, colormap);

	//V_SetClipRect((fx + 10) << FRACBITS, (fy + 10) << FRACBITS, 30 << FRACBITS, 30 << FRACBITS, V_HUDTRANS|V_SLIDEIN|fflags);

	fixed_t rfy = fy<<FRACBITS;
	INT32 fancyflags = V_HUDTRANS|fflags;

	if (cv_fancyroulette.value && stplyr->itemroulette && !stplyr->deadtimer)
	{
		fixed_t frac = R_GetHudUncap();
		UINT8   fancystep = (offset ? 6 : 10);
		fixed_t fancyoffset = (stplyr->itemroulette % 3)-1;

		if (fancyoffset != 0)
		{
			fancyflags &= ~V_HUDTRANS;
			fancyflags |=V_HUDTRANSHALF;
		}

		rfy += (fancystep * fancyoffset * FRACUNIT) + FixedMul(fancystep*FRACUNIT, frac) - fancystep/2*FRACUNIT;
	}

	// Then, the numbers:
	if (stplyr->itemamount >= numberdisplaymin && !stplyr->itemroulette)
	{
		localbg = K_getItemMulPatch((boolean)offset);
		V_DrawMappedPatch(fx + (flipamount ? 48 : 0), fy, V_HUDTRANS|fflags|(flipamount ? V_FLIP : 0), localbg, colormap); // flip this graphic for p2 and p4 in split and shift it.
		V_DrawFixedPatch(fx<<FRACBITS, fy<<FRACBITS, FRACUNIT, V_HUDTRANS|fflags, localpatch, colmap);
		if (offset)
			if (flipamount) // reminder that this is for 3/4p's right end of the screen.
				V_DrawString(fx+2, fy+31, V_ALLOWLOWERCASE|V_HUDTRANS|fflags, va("x%d", stplyr->itemamount));
			else
				V_DrawString(fx+24, fy+31, V_ALLOWLOWERCASE|V_HUDTRANS|fflags, va("x%d", stplyr->itemamount));
		else
		{
			V_DrawScaledPatch(fy+28, fy+41, V_HUDTRANS|fflags, kp_itemx);
			V_DrawKartString(fx+38, fy+36, V_HUDTRANS|fflags, va("%d", stplyr->itemamount));
		}
	}
	else
		V_DrawFixedPatch(fx<<FRACBITS, rfy, FRACUNIT, fancyflags, localpatch, colmap);

	//V_ClearClipRect();

	// Extensible meter, currently used by Grow, Rocket Sneakers and Flame Shield
	if (itembar)
	{
		const INT32 fill = ((itembar*barlength)/maxl);
		const INT32 length = min(barlength, fill);
		const INT32 height = (offset ? 1 : 2);
		const INT32 x = (offset ? 17 : 11), y = (offset ? 27 : 35);

		V_DrawScaledPatch(fx+x, fy+y, V_HUDTRANS|fflags, kp_itemtimer[offset]);
		// The left dark "AA" edge
		V_DrawFill(fx+x+1, fy+y+1, (length == 2 ? 2 : 1), height, 12|fflags);
		// The bar itself
		if (length > 2)
		{
			V_DrawFill(fx+x+length, fy+y+1, 1, height, 12|fflags); // the right one
			if (height == 2)
				V_DrawFill(fx+x+2, fy+y+2, length-2, 1, 8|fflags); // the dulled underside
			V_DrawFill(fx+x+2, fy+y+1, length-2, 1, 0|fflags); // the shine
		}
	}

	if (flamebar)
	{
		const INT32 fill = ((flamebar*barlength)/flamemaxl);
		const INT32 length = min(barlength, fill);
		const INT32 height = (offset ? 1 : 2);
		const INT32 x = (offset ? 17 : 11), y = (offset ? 27 : 35);

		V_DrawScaledPatch(fx+x, fy+y-8, V_HUDTRANS|fflags, kp_itemtimer[offset]);
		// The left dark "AA" edge
		V_DrawFill(fx+x+1, fy+y+1-8, (length == 2 ? 2 : 1), height, 55|fflags);
		// The bar itself
		if (length > 2)
		{
			V_DrawFill(fx+x+length, fy+y+1-8, 1, height, 55|fflags); // the right one
			if (height == 2)
				V_DrawFill(fx+x+2, fy+y+2-8, length-2, 1, 36|fflags); // the dulled underside
			V_DrawFill(fx+x+2, fy+y+1-8, length-2, 1, 51|fflags); // the shine
		}
	}

	// Quick Eggman numbers
	if (stplyr->eggmanexplode > 1)
		V_DrawScaledPatch(fx+17, fy+13-offset, V_HUDTRANS|fflags, kp_eggnum[min(3, G_TicsToSeconds(stplyr->eggmanexplode))]);
}

void K_drawKartTimestamp(tic_t drawtime, INT32 TX, INT32 TY, INT16 emblemmap, UINT8 mode)
{
	// TIME_X = BASEVIDWIDTH-124;	// 196
	// TIME_Y = 6;					//   6

	tic_t worktime;
	boolean dontdraw = false;
	boolean emblemenabled = K_EmblemsEnabled();

	INT32 splitflags = 0;
	if (!mode)
	{
		splitflags = V_HUDTRANS|V_SNAPTOTOP|V_SNAPTORIGHT|V_SPLITSCREEN;

#ifndef TESTOVERTIMEINFREEPLAY
		if (itembreaker) // capsules override any time limit settings
			;
		else
#endif
		if (bossinfo.boss == true)
			;
		else if (timelimitintics > 0 && (gametyperules & GTR_TIMELIMIT)) // TODO
		{
			if (drawtime >= timelimitintics)
			{
				if (((drawtime-timelimitintics)/TICRATE) & 1)
				{
					dontdraw = true;
				}
				drawtime = 0;
			}
			else
			{
				drawtime = timelimitintics - drawtime;
			}
		}
	}

	if (!K_UseColorHud())
		V_DrawScaledPatch(TX, TY, splitflags, ((mode == 2) ? kp_lapstickerwide[0] : kp_timestickerwide[0]));
	else //Colourized hud
	{
		UINT8 *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
		V_DrawMappedPatch(TX, TY, splitflags, ((mode == 2) ? kp_lapstickerwide[1] : kp_timestickerwide[1]), colormap);
	}

	TX += 33;

	worktime = drawtime/(60*TICRATE);

	if (mode && !drawtime)
		V_DrawKartString(TX, TY+3, splitflags, va("--'--\"--"));
	else if (dontdraw) // overtime flash
		;
	else if (worktime < 100) // 99:99:99 only
	{
		// zero minute
		if (worktime < 10)
		{
			V_DrawKartString(TX, TY+3, splitflags, va("0"));
			// minutes time       0 __ __
			V_DrawKartString(TX+12, TY+3, splitflags, va("%d", worktime));
		}
		// minutes time       0 __ __
		else
			V_DrawKartString(TX, TY+3, splitflags, va("%d", worktime));

		// apostrophe location     _'__ __
		V_DrawKartString(TX+24, TY+3, splitflags, va("'"));

		worktime = (drawtime/TICRATE % 60);

		// zero second        _ 0_ __
		if (worktime < 10)
		{
			V_DrawKartString(TX+36, TY+3, splitflags, va("0"));
		// seconds time       _ _0 __
			V_DrawKartString(TX+48, TY+3, splitflags, va("%d", worktime));
		}
		// zero second        _ 00 __
		else
			V_DrawKartString(TX+36, TY+3, splitflags, va("%d", worktime));

		// quotation mark location    _ __"__
		V_DrawKartString(TX+60, TY+3, splitflags, va("\""));

		worktime = G_TicsToCentiseconds(drawtime);

		// zero tick          _ __ 0_
		if (worktime < 10)
		{
			V_DrawKartString(TX+72, TY+3, splitflags, va("0"));
		// tics               _ __ _0
			V_DrawKartString(TX+84, TY+3, splitflags, va("%d", worktime));
		}
		// zero tick          _ __ 00
		else
			V_DrawKartString(TX+72, TY+3, splitflags, va("%d", worktime));
	}
	else if ((drawtime/TICRATE) & 1)
		V_DrawKartString(TX, TY+3, splitflags, va("99'59\"99"));

	if ((modeattacking || (mode == 1)) && emblemenabled && !demo.playback) // emblem time!
	{
		INT32 workx = TX + 96, worky = TY+18;
		SINT8 curemb = 0;
		patch_t *emblempic[3] = {NULL, NULL, NULL};
		UINT8 *emblemcol[3] = {NULL, NULL, NULL};

		emblem_t *emblem = M_GetLevelEmblems(emblemmap);
		while (emblem)
		{
			char targettext[9];

			switch (emblem->type)
			{
				case ET_TIME:
					{
						static boolean canplaysound = true;
						tic_t timetoreach = emblem->var;

						if (emblem->collected)
						{
							emblempic[curemb] = W_CachePatchLongName(M_GetEmblemPatch(emblem, false), PU_CACHE);
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
			V_DrawSmallScaledPatch(workx + 4, worky, splitflags, W_CachePatchLongName("NEEDIT", PU_CACHE));

			break;

bademblem:
			emblem = M_GetLevelEmblems(-1);
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

static void K_DrawKartPositionNum(INT32 num)
{
	// POSI_X = BASEVIDWIDTH - 51;	// 269
	// POSI_Y = BASEVIDHEIGHT- 64;	// 136

	boolean win = (stplyr->exiting && num == 1);
	//INT32 X = POSI_X;
	INT32 W = SHORT(kp_positionnum[0][0]->width);
	fixed_t scale = FRACUNIT;
	patch_t *localpatch = kp_positionnum[0][0];
	INT32 fx = 0, fy = 0, fflags = 0;
	INT32 xoffs = (cv_showinput.value > 0) ? -48 : 0;
	INT32 addOrSub = V_ADD;
	boolean flipdraw = false;	// flip the order we draw it in for MORE splitscreen bs. fun.
	boolean flipvdraw = false;	// used only for 2p splitscreen so overtaking doesn't make 1P's position fly off the screen.
	boolean overtake = false;

	if ((mapheaderinfo[gamemap - 1]->levelflags & LF_SUBTRACTNUM) == LF_SUBTRACTNUM)
	{
		addOrSub = V_SUBTRACT;
	}

	if (stplyr->positiondelay || stplyr->exiting)
	{
		scale *= 2;
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
		if (stplyr == &players[displayplayers[0]])	// for player 1: display this at the top right, above the minimap.
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
		if (stplyr == &players[displayplayers[0]] || stplyr == &players[displayplayers[2]])	// If we are P1 or P3...
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
			boolean useRedNums = K_IsPlayerLosing(stplyr);

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
			(fx<<FRACBITS) + ((overtake && flipdraw) ? (SHORT(localpatch->width)*scale/2) : 0),
			(fy<<FRACBITS) + ((overtake && flipvdraw) ? (SHORT(localpatch->height)*scale/2) : 0),
			scale, addOrSub|V_HUDTRANSHALF|fflags, localpatch, NULL
		);
		// ^ if we overtake as p1 or p3 in splitscren, we shift it so that it doesn't go off screen.
		// ^ if we overtake as p1 in 2p splits, shift vertically so that this doesn't happen either.

		fx -= W;
		num /= 10;
	}
}

static UINT32 K_InvincibilityHUDVisibility(UINT16 t)
{
	UINT32 alphalevel = st_translucency;

	alphalevel = min(10, FixedMul(alphalevel, K_InvincibilityGradient(t)));

	return min(9, 10 - alphalevel)<<V_ALPHASHIFT;
}

static boolean K_drawKartPositionFaces(void)
{
	// FACE_X = 15;				//  15
	// FACE_Y = 72;				//  72

	INT32 Y = FACE_Y-9; // -9 to offset where it's being drawn if there are more than one
	INT32 i, j, ranklines, strank = -1;
	boolean completed[MAXPLAYERS];
	INT32 rankplayer[MAXPLAYERS];
	INT32 bumperx, numplayersingame = 0;
	UINT8 *colormap;
	UINT32 invinchudtrans;

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
		UINT8 lowestposition = MAXPLAYERS+1;
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

			V_DrawMappedPatch(FACE_X, Y, V_HUDTRANS|V_SNAPTOLEFT, faceprefix[players[rankplayer[i]].skin][FACE_RANK], colormap);

			if (players[rankplayer[i]].invincibilitytimer)
			{
				colormap = R_GetTranslationColormap(TC_RAINBOW, K_RainbowColor(leveltime / 2), GTC_CACHE);
				invinchudtrans = K_InvincibilityHUDVisibility(players[rankplayer[i]].invincibilitytimer);

				V_DrawMappedPatch(FACE_X, Y, invinchudtrans|V_SNAPTOLEFT|V_ADD, faceprefix[players[rankplayer[i]].skin][FACE_RANK], colormap);
			}

			if (LUA_HudEnabled(hud_battlebumpers))
			{
				if ((gametyperules & GTR_BUMPERS) && players[rankplayer[i]].bumper > 0)
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
			V_DrawScaledPatch(FACE_X, Y, V_HUDTRANS|V_SNAPTOLEFT, kp_facehighlight[(leveltime / 4) % 8]);

		if (gametype == GT_BATTLE && players[rankplayer[i]].bumper <= 0)
			V_DrawScaledPatch(FACE_X-4, Y-3, V_HUDTRANS|V_SNAPTOLEFT, kp_ranknobumpers);
		else
		{
			INT32 pos = players[rankplayer[i]].position;
			if (pos < 0 || pos > MAXPLAYERS)
				pos = 0;
			// Draws the little number over the face
			V_DrawScaledPatch(FACE_X-5, Y+10, V_HUDTRANS|V_SNAPTOLEFT, kp_facenum[pos]);
		}

		Y -= 18;
	}

	return false;
}

static void K_drawBossHealthBar(void)
{
	UINT8 i = 0, barstatus = 1, randlen = 0, darken = 0;
	const INT32 startx = BASEVIDWIDTH - 23;
	INT32 starty = BASEVIDHEIGHT - 25;
	INT32 rolrand = 0;
	boolean randsign = false;

	if (bossinfo.barlen <= 1)
		return;

	// Entire bar juddering!
	if (lt_exitticker < (TICRATE/2))
		;
	else if (bossinfo.visualbarimpact)
	{
		INT32 mag = min((bossinfo.visualbarimpact/4) + 1, 8);
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

	randlen = M_RandomKey(bossinfo.visualbar-(bossinfo.visualdiv/(2*FRACUNIT)))+1;
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
				randlen = M_RandomKey(bossinfo.visualbar-(bossinfo.visualdiv/(2*FRACUNIT)))+1;
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
			else if ((bossinfo.visualbar - i) < (INT32)(bossinfo.visualbarimpact/8))
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
// HU_DrawTabRankings -- moved here to take advantage of kart stuff!
//
void K_DrawTabRankings(INT32 x, INT32 y, playersort_t *tab, INT32 scorelines, INT32 whiteplayer, INT32 hilicol)
{
	INT32 i, rightoffset = 240;
	const UINT8 *colormap;
	INT32 dupadjust = (vid.width/vid.dupx), duptweak = (dupadjust - BASEVIDWIDTH)/2;
	int basey = y, basex = x, y2;

	//this function is designed for 9 or less score lines only
	//I_Assert(scorelines <= 9); -- not today bitch, kart fixed it up

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
		char strtime[MAXPLAYERNAME+1];

		if (players[tab[i].num].spectator || !players[tab[i].num].mo)
			continue; //ignore them.

		if (netgame) // don't draw ping offline
		{
			if (players[tab[i].num].bot)
			{
				V_DrawString(x + ((i < 8) ? -25 : rightoffset + 3), y-2, V_SNAPTOLEFT, "CPU");
			}
			else if (tab[i].num != serverplayer || !server_lagless)
			{
				HU_drawPing(x + ((i < 8) ? -17 : rightoffset + 11), y-4, playerpingtable[tab[i].num], 0, false);
			}
		}

		STRBUFCPY(strtime, tab[i].name);

		y2 = y;

		if (scorelines >= 8)
			V_DrawThinString(x + 20, y2, ((tab[i].num == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, strtime);
		else
			V_DrawString(x + 20, y2, ((tab[i].num == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE, strtime);

		if (players[tab[i].num].mo->color)
		{
			colormap = R_GetTranslationColormap(players[tab[i].num].skin, players[tab[i].num].mo->color, GTC_CACHE);
			if (players[tab[i].num].mo->colorized)
				colormap = R_GetTranslationColormap(TC_RAINBOW, players[tab[i].num].mo->color, GTC_CACHE);
			else
				colormap = R_GetTranslationColormap(players[tab[i].num].skin, players[tab[i].num].mo->color, GTC_CACHE);

			V_DrawMappedPatch(x, y-4, 0, faceprefix[players[tab[i].num].skin][FACE_RANK], colormap);
			/*if (gametype == GT_BATTLE && players[tab[i].num].bumper > 0) -- not enough space for this
			{
				INT32 bumperx = x+19;
				V_DrawMappedPatch(bumperx-2, y-4, 0, kp_tinybumper[0], colormap);
				for (j = 1; j < players[tab[i].num].bumper; j++)
				{
					bumperx += 5;
					V_DrawMappedPatch(bumperx, y-4, 0, kp_tinybumper[1], colormap);
				}
			}*/
		}

		if (tab[i].num == whiteplayer)
			V_DrawScaledPatch(x, y-4, 0, kp_facehighlight[(leveltime / 4) % 8]);

		if (gametype == GT_BATTLE && players[tab[i].num].bumper <= 0)
			V_DrawScaledPatch(x-4, y-7, 0, kp_ranknobumpers);
		else
		{
			INT32 pos = players[tab[i].num].position;
			if (pos < 0 || pos > MAXPLAYERS)
				pos = 0;
			// Draws the little number over the face
			V_DrawScaledPatch(x-5, y+6, 0, kp_facenum[pos]);
		}

		if (gametype == GT_RACE)
		{
#define timestring(time) va("%i'%02i\"%02i", G_TicsToMinutes(time, true), G_TicsToSeconds(time), G_TicsToCentiseconds(time))
			if (scorelines >= 8)
			{
				if (players[tab[i].num].exiting)
					V_DrawRightAlignedThinString(x+rightoffset, y-1, hilicol|V_6WIDTHSPACE, timestring(players[tab[i].num].realtime));
				else if (players[tab[i].num].pflags & PF_NOCONTEST)
					V_DrawRightAlignedThinString(x+rightoffset, y-1, V_6WIDTHSPACE, "NO CONTEST.");
				else if (circuitmap)
					V_DrawRightAlignedThinString(x+rightoffset, y-1, V_6WIDTHSPACE, va("Lap %d", tab[i].count));
			}
			else
			{
				if (players[tab[i].num].exiting)
					V_DrawRightAlignedString(x+rightoffset, y, hilicol, timestring(players[tab[i].num].realtime));
				else if (players[tab[i].num].pflags & PF_NOCONTEST)
					V_DrawRightAlignedThinString(x+rightoffset, y-1, 0, "NO CONTEST.");
				else if (circuitmap)
					V_DrawRightAlignedString(x+rightoffset, y, 0, va("Lap %d", tab[i].count));
			}
#undef timestring
		}
		else
			V_DrawRightAlignedString(x+rightoffset, y, 0, va("%u", tab[i].count));

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
	const boolean uselives = G_GametypeUsesLives();
	INT32 fx = 0, fy = 0, splitflags = 0;	// stuff for 3p / 4p splitscreen.

	drawinfo_t info;
	K_getLapsDrawinfo(&info);
	fx = info.x;
	fy = info.y;
	splitflags = info.flags;

	if (r_splitscreen > 1)
	{
		INT32 fr = 0;

		fr = fx;

		// Laps
		V_DrawScaledPatch(fx, fy, V_HUDTRANS|splitflags, kp_splitlapflag);
		V_DrawScaledPatch(fx+22, fy, V_HUDTRANS|splitflags, frameslash);

		if (numlaps >= 10)
		{
			UINT8 ln[2];
			ln[0] = ((stplyr->laps+1 / 10) % 10);
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

		// Lives
		if (LUA_HudEnabled(hud_lives) && uselives)
		{
			UINT8 *colormap = R_GetTranslationColormap(stplyr->skin, K_GetHudColor(), GTC_CACHE);
			V_DrawMappedPatch(fr+21, fy-13, V_HUDTRANS|splitflags, faceprefix[stplyr->skin][FACE_MINIMAP], colormap);
			if (stplyr->lives >= 0)
				V_DrawScaledPatch(fr+34, fy-10, V_HUDTRANS|splitflags, fontv[PINGNUM_FONT].font[(stplyr->lives % 10)]); // make sure this doesn't overflow OR underflow
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
			UINT8 *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
			if (K_BigLapSticker())
				V_DrawMappedPatch(fx, fy, V_HUDTRANS|splitflags, ((stplyr->laps > 9) ? kp_lapstickerbig2[1] : kp_lapstickerbig[1]), colormap);
			else
				V_DrawMappedPatch(fx, fy, V_HUDTRANS|splitflags, kp_lapsticker[1], colormap);
		}

		if (stplyr->exiting)
			V_DrawKartString(fx+33, fy+3, V_HUDTRANS|V_SLIDEIN|splitflags, "FIN");
		else
			V_DrawKartString(fx+33, fy+3, V_HUDTRANS|splitflags, va("%d/%d", min(stplyr->laps, numlaps), numlaps));

		// Lives
		if (LUA_HudEnabled(hud_lives) && uselives)
		{
			INT32 offsetx = 0;
			INT32 offsety = 0;

			if (cv_newspeedometer.value == 0 && !K_RingsActive())
			{
				offsetx = 25;
				offsety = 15;
			}
			else if (K_RingsActive())
			{
				offsetx = 4;
			}

			UINT8 *colormap = R_GetTranslationColormap(stplyr->skin, K_GetHudColor(), GTC_CACHE);
			V_DrawMappedPatch(fx+59+offsetx, fy-16+offsety, V_HUDTRANS|splitflags, faceprefix[stplyr->skin][FACE_RANK], colormap);
			if (stplyr->lives >= 0)
				V_DrawScaledPatch(fx+77+offsetx, fy-11+offsety, V_HUDTRANS|splitflags, kp_facenum[(stplyr->lives % 10)]); // make sure this doesn't overflow OR underflow
		}
	}
}

static void K_drawKartAccessibilityIcons(INT32 fx)
{
	INT32 fy = ACCE_Y-25;
	INT32 splitflags = V_SNAPTOLEFT|V_SNAPTOBOTTOM|V_HUDTRANS|V_SPLITSCREEN;
	//INT32 step = 1; -- if there's ever more than one accessibility icon

	fx += ACCE_X;

	if (r_splitscreen < 2) // adjust to speedometer height
	{
		if (cv_acce_xoffset.value == 0 && cv_acce_yoffset.value == 0)
		{

			if (cv_newspeedometer.value == 0)
			{
				fy += 44;
			}
			else
			{
				if (cv_kartspeedometer.value == 0 && !K_RingsActive())
					fy += 18;

				if (gametype == GT_BATTLE)
					fy -= 4;
			}
		}
	}
	else
	{
		fx = ACCE_X+43;
		fy = ACCE_Y;
		if (!(stplyr == &players[displayplayers[0]] || stplyr == &players[displayplayers[2]]))	// If we are not P1 or P3...
		{
			splitflags ^= (V_SNAPTOLEFT|V_SNAPTORIGHT);
			fx = (BASEVIDWIDTH/2) - (fx + 10);
			//step = -step;
		}
	}

	if (stplyr->pflags & PF_KICKSTARTACCEL) // just KICKSTARTACCEL right now, maybe more later
	{
		SINT8 col = 0, wid, fil, ofs;
		UINT8 i = 7;
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

static void K_drawKartSpeedometer(void)
{
	static fixed_t convSpeed;
	UINT8 labeln = 0;
	UINT8 numbers[3];
	INT32 splitflags = V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_SPLITSCREEN;
	INT32 battleoffset = 0;
	INT32 ringoffset = 0;
	INT32 oldringoffset = 0;

	switch (cv_kartspeedometer.value)
	{
		case 1: // Kilometers
			convSpeed = FixedDiv(FixedMul(stplyr->speed, 142371), mapobjectscale) / FRACUNIT; // 2.172409058
			labeln = 0;
			break;
		case 2: // Miles
			convSpeed = FixedDiv(FixedMul(stplyr->speed, 88465), mapobjectscale) / FRACUNIT; // 1.349868774
			labeln = 1;
			break;
		case 3: // Fracunits
			convSpeed = FixedDiv(stplyr->speed, mapobjectscale) / FRACUNIT; // 1.0. duh.
			labeln = 2;
			break;
		case 4: // Sonic Drift 2 style percentage
			if (stplyr->mo)
				convSpeed = (FixedDiv(stplyr->speed, FixedMul(K_GetKartSpeed(stplyr, false, false), K_PlayerBaseFriction(stplyr, ORIG_FRICTION)))*100)>>FRACBITS;
			labeln = 3;
			break;
		default:
			break;
	}

	// Don't overflow
	// (negative speed IS really high speed :V)
	if (convSpeed > 999 || convSpeed < 0)
		convSpeed = 999;
	
	if (cv_speed_xoffset.value == 0 && cv_speed_yoffset.value == 0)
	{
		if (gametype == GT_BATTLE)
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
				V_DrawKartString(SPDM_X + oldringoffset, SPDM_Y-18 + battleoffset + ringoffset, V_HUDTRANS|splitflags, va("%3d km/h", convSpeed));
				break;
			case 2:
				V_DrawKartString(SPDM_X + oldringoffset, SPDM_Y-18 + battleoffset + ringoffset, V_HUDTRANS|splitflags, va("%3d mph", convSpeed));
				break;
			case 3:
				V_DrawKartString(SPDM_X + oldringoffset, SPDM_Y-18 + battleoffset + ringoffset, V_HUDTRANS|splitflags, va("%3d fu/t", convSpeed));
				break;
			case 4:
				V_DrawKartString(SPDM_X + oldringoffset, SPDM_Y-18 + battleoffset + ringoffset, V_HUDTRANS|splitflags, va("%4d %%", convSpeed));
				break;
			default:
				break;
		}
	}
	else if (cv_newspeedometer.value == 1)
	{
		numbers[0] = ((convSpeed / 100) % 10);
		numbers[1] = ((convSpeed / 10) % 10);
		numbers[2] = (convSpeed % 10);

		if (!K_UseColorHud())
		{

			V_DrawScaledPatch(SPDM_X, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_speedometersticker[0]);
		}
		else
		{
			UINT8 *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
			V_DrawMappedPatch(SPDM_X, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_speedometersticker[1], colormap);
		}

		V_DrawScaledPatch(SPDM_X+7, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_facenum[numbers[0]]);
		V_DrawScaledPatch(SPDM_X+13, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_facenum[numbers[1]]);
		V_DrawScaledPatch(SPDM_X+19, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_facenum[numbers[2]]);
		V_DrawScaledPatch(SPDM_X+29, SPDM_Y-9 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_speedometerlabel[labeln]);
	}
	// Readded later need to draw smaller graphics.
	/*else if (cv_newspeedometer.value == 2)
	{
		fixed_t fuspeed =  FixedDiv(stplyr->speed, mapobjectscale)/FRACUNIT;
		INT32 spdpatch = 0;

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

		V_DrawScaledPatch(SPDM_X, SPDM_Y-18 + battleoffset + ringoffset, V_HUDTRANS|splitflags, kp_kartzspeedo[spdpatch]);
	}*/

	K_drawKartAccessibilityIcons((cv_newspeedometer.value == 0) ? 50 : 56);
}

static void K_drawRingMeter(void)
{
	UINT8 rn[2];
	UINT8 *ringmap = NULL;
	boolean colorring = false;
	SINT8 ringcount = stplyr->rings;
	INT32 fx = 0, fy = 0, splitflags = 0;	// stuff for 3p / 4p splitscreen.

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
	else if (ringcount >= 20) // Maxed out
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
		SINT8 i;
		SINT8 ringoffsety = 0;
		UINT8 *colormap = NULL;
		SINT8 coloroffset = 0;

		if (cv_speed_xoffset.value == 0 && cv_speed_yoffset.value == 0)
		{
			if (gametype == GT_BATTLE)
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

		if (stplyr->rings < 0)
		{
			// Invert the ring count
			ringcount = -ringcount;
		}

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
			UINT8 barcolors[5] = {66,72,2,68};
			boolean indebt = false;

			if (stplyr->rings < 0)
			{
				barcolors[0] = 38;
				barcolors[1] = 36;
				barcolors[2] = 32;
				barcolors[3] = 40;
				indebt = true;
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
	UINT8 *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
	INT32 fx = 0, fy = 0, splitflags = 0;	// stuff for 3p / 4p splitscreen.

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
			V_DrawMappedPatch(fx+1, fy-2, V_HUDTRANS|splitflags, kp_rankcapsule, NULL);

			if (numtargets > 9 || nummapboxes > 9)
			{
				UINT8 ln[2];
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
			if (stplyr->bumper <= 0)
			{
				V_DrawMappedPatch(fx, fy-1, V_HUDTRANS|splitflags, kp_splitkarmabomb, colormap);
				V_DrawString(fx+13, fy+1, V_HUDTRANS|splitflags, va("%d/2", stplyr->karmapoints));
			}
			else
			{
				INT32 maxbumper = K_StartingBumperCount();
				V_DrawMappedPatch(fx+1, fy-2, V_HUDTRANS|splitflags, kp_rankbumper, colormap);

				if (stplyr->bumper > 9 || maxbumper > 9)
				{
					UINT8 ln[2];
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
			patch_t *item = W_CachePatchLongName("RNDMA0", PU_PATCH);
			UINT8 *itemcolormap = R_GetTranslationColormap(TC_BLINK, SKINCOLOR_BLACK, GTC_CACHE);

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
				INT32 maxbumper = K_StartingBumperCount();

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
	UINT8 i, numwanted = 0;
	UINT8 *colormap = NULL;
	INT32 basex = 0, basey = 0;

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
		}
	}
	else if (r_splitscreen == 3) // 4P splitscreen...
	{
		basex = BASEVIDWIDTH/2 - (SHORT(kp_wantedsplit->width)/2);	// center on screen
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
		INT32 x = basex+(r_splitscreen > 1 ? 13 : 8), y = basey+(r_splitscreen > 1 ? 16 : 21);
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
			V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, FRACUNIT, V_HUDTRANS|(r_splitscreen < 3 ? V_SNAPTORIGHT : 0)|V_SNAPTOBOTTOM, (scale == FRACUNIT ? faceprefix[p->skin][FACE_WANTED] : faceprefix[p->skin][FACE_RANK]), colormap);
			/*if (basey2)	// again with 4p stuff
				V_DrawFixedPatch(x<<FRACBITS, (y - (basey-basey2))<<FRACBITS, FRACUNIT, V_HUDTRANS|V_SNAPTOTOP, (scale == FRACUNIT ? faceprefix[p->skin][FACE_WANTED] : faceprefix[p->skin][FACE_RANK]), colormap);*/
		}
	}
}

static void K_drawKartPlayerCheck(void)
{
	const fixed_t maxdistance = FixedMul(1280 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	UINT8 i;
	INT32 splitflags = V_SNAPTOBOTTOM|V_SPLITSCREEN;
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
		UINT8 *colormap = NULL;
		UINT8 pnum = 0;
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

		if ((checkplayer->invincibilitytimer <= 0) && (leveltime & 2))
		{
			pnum++; // white frames
		}

		if (checkplayer->itemtype == KITEM_GROW || checkplayer->growshrinktimer > 0)
		{
			pnum += 4;
		}
		else if (checkplayer->itemtype == KITEM_INVINCIBILITY || checkplayer->invincibilitytimer)
		{
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

static boolean K_ShowPlayerNametag(player_t *p)
{
	if (cv_seenames.value == 0)
	{
		return false;
	}

	if (demo.playback == true && camera[R_GetViewNumber()].freecam == true)
	{
		return true;
	}

	if (stplyr == p)
	{
		return false;
	}

	if (gametyperules & GTR_CIRCUIT)
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

static void K_DrawLocalTagForPlayer(fixed_t x, fixed_t y, player_t *p, UINT8 id)
{
	UINT16 chatcolor = skincolors[p->skincolor].chatcolor;
	char letters[4] = {'A', 'B', 'C', 'D'};

	V_DrawCenteredSmallStringAtFixed(x, y, V_HUDTRANS|V_ALLOWLOWERCASE|V_SPLITSCREEN|chatcolor, va("%c\nv", letters[id]));
}

static void K_DrawRivalTagForPlayer(fixed_t x, fixed_t y)
{
	UINT16 chatcolor = skincolors[SKINCOLOR_ORANGE].chatcolor;

	V_DrawCenteredSmallStringAtFixed(x, y, V_HUDTRANS|V_ALLOWLOWERCASE|V_SPLITSCREEN|chatcolor, "Rival\nv");
}

static const char *K_StringTypingDot(player_t *p)
{
	static const char *dots = "...";

	return dots + CLAMP(3 - p->typing_duration/16, 0, 3);
}

static void K_DrawTypingNotifier(fixed_t x, fixed_t y, player_t *p)
{
	if (p->cmd.flags & TICCMD_TYPING)
	{
		V_DrawCenteredSmallStringAtFixed(x, y, V_SPLITSCREEN, va("Typing%s",K_StringTypingDot(p)));
	}
}

static void K_DrawNameTagForPlayer(fixed_t x, fixed_t y, player_t *p)
{
	const INT32 clr = skincolors[p->skincolor].chatcolor;
	const INT32 namelen = V_ThinStringWidth(player_names[p - players], V_6WIDTHSPACE|V_ALLOWLOWERCASE);

	UINT8 *colormap = V_GetStringColormap(clr);
	INT32 barx = 0, bary = 0, barw = 0;

	UINT8 cnum = R_GetViewNumber();

	// Since there's no "V_DrawFixedFill", and I don't feel like making it,
	// fuck it, we're gonna just V_NOSCALESTART hack it
	if (cnum & 1)
	{
		x += (BASEVIDWIDTH/2) * FRACUNIT;
	}

	if ((r_splitscreen == 1 && cnum == 1)
	|| (r_splitscreen > 1 && cnum > 1))
	{
		y += (BASEVIDHEIGHT/2) * FRACUNIT;
	}

	barw = (namelen * vid.dupx);

	barx = (x * vid.dupx) / FRACUNIT;
	bary = (y * vid.dupy) / FRACUNIT;

	barx += (6 * vid.dupx);
	bary -= (16 * vid.dupx);

	// Center it if necessary
	if (vid.width != BASEVIDWIDTH * vid.dupx)
	{
		barx += (vid.width - (BASEVIDWIDTH * vid.dupx)) / 2;
	}

	if (vid.height != BASEVIDHEIGHT * vid.dupy)
	{
		bary += (vid.height - (BASEVIDHEIGHT * vid.dupy)) / 2;
	}

	// Lat: 10/06/2020: colormap can be NULL on the frame you join a game, just arbitrarily use palette indexes 31 and 0 instead of whatever the colormap would give us instead to avoid crashes.


	// Draw the stem
	{
		fixed_t stemx;
		fixed_t stemy;
		int j;
		/*boolean flipcam = (p->pflags & PF_FLIPCAM) && (p->mo->eflags & MFE_VERTICALFLIP);
		boolean flipped;

		if (flipcam)
			flipped = (p->mo->eflags & MFE_VERTICALFLIP) != (stplyr->mo->eflags & MFE_VERTICALFLIP);
		else
			flipped = p->mo->eflags & MFE_VERTICALFLIP;*/
		stemx = (x * vid.dupx) / FRACUNIT;
		stemy = (y * vid.dupy) / FRACUNIT;

		// Center it if necessary
		if (vid.width != BASEVIDWIDTH * vid.dupx)
		{
			stemx += (vid.width - (BASEVIDWIDTH * vid.dupx)) / 2;
		}

		if (vid.height != BASEVIDHEIGHT * vid.dupy)
		{
			stemy += (vid.height - (BASEVIDHEIGHT * vid.dupy)) / 2;
		}

		for	(j = 0; j < 4; j++)
		{
			stemy -= vid.dupy*4;
			V_DrawFill(stemx, stemy, vid.dupy*3, vid.dupy*4, (colormap ? colormap[31] : 31)|V_NOSCALESTART);
			V_DrawFill(stemx+vid.dupx, stemy + vid.dupy, vid.dupy, vid.dupy*4, (colormap ? colormap[0] : 0)|V_NOSCALESTART);
			stemx += vid.dupx;
		}

		V_DrawFill(barx, bary, barw, (3 * vid.dupy), (colormap ? colormap[31] : 31)|V_NOSCALESTART);
		V_DrawFill(barx, bary + vid.dupy, barw, vid.dupy, (colormap ? colormap[0] : 0)|V_NOSCALESTART);
		V_DrawFill(stemx+vid.dupx, stemy+vid.dupy, barw - vid.dupx*3, vid.dupy, (colormap ? colormap[0] : 0)|V_NOSCALESTART);
	}
	// END DRAWFILL DUMBNESS



	// Draw the name itself
	V_DrawThinStringAtFixed(x + (5*FRACUNIT), y - (26*FRACUNIT), V_6WIDTHSPACE|V_ALLOWLOWERCASE|clr, player_names[p - players]);
}

typedef struct weakspotdraw_t
{
	UINT8 i;
	INT32 x;
	INT32 y;
	boolean candrawtag;
} weakspotdraw_t;

static void K_DrawWeakSpot(weakspotdraw_t *ws)
{
	UINT8 *colormap;
	UINT8 j = (bossinfo.weakspots[ws->i].type == SPOT_BUMP) ? 1 : 0;
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
	const fixed_t maxdistance = 8192*mapobjectscale;
	vector3_t c;
	UINT8 cnum = R_GetViewNumber();
	UINT8 tobesorted[MAXPLAYERS];
	fixed_t sortdist[MAXPLAYERS];
	UINT8 sortlen = 0;
	UINT8 i, j;

	if (stplyr == NULL || stplyr->mo == NULL || P_MobjWasRemoved(stplyr->mo))
	{
		return;
	}

	if (stplyr->awayviewtics)
	{
		return;
	}

	// Crop within splitscreen bounds
	switch (r_splitscreen)
	{
		case 1:
			V_SetClipRect(
				0,
				cnum == 1 ? (BASEVIDHEIGHT / 2) * FRACUNIT : 0,
				BASEVIDWIDTH * FRACUNIT,
				(BASEVIDHEIGHT / 2) * FRACUNIT,
				0
			);
			break;

		case 2:
		case 3:
			V_SetClipRect(
				cnum & 1 ? (BASEVIDWIDTH / 2) * FRACUNIT : 0,
				cnum > 1 ? (BASEVIDHEIGHT / 2) * FRACUNIT : 0,
				(BASEVIDWIDTH / 2) * FRACUNIT,
				(BASEVIDHEIGHT / 2) * FRACUNIT,
				0
			);
			break;
	}

	c.x = viewx;
	c.y = viewy;
	c.z = viewz;

	// Maybe shouldn't be handling this here... but the camera info is too good.
	if (bossinfo.boss)
	{
		weakspotdraw_t weakspotdraw[NUMWEAKSPOTS];
		UINT8 numdraw = 0;
		boolean onleft = false;

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

	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *ntplayer = &players[i];
		fixed_t distance = maxdistance+1;
		vector3_t v;

		if (!playeringame[i] || ntplayer->spectator)
		{
			// Not in-game
			continue;
		}

		if (ntplayer->mo == NULL || P_MobjWasRemoved(ntplayer->mo))
		{
			// No object
			continue;
		}

		if (ntplayer->mo->renderflags & K_GetPlayerDontDrawFlag(stplyr))
		{
			// Invisible on this screen
			continue;
		}

		if ((gametyperules & GTR_BUMPERS) && (ntplayer->bumper <= 0))
		{
			// Dead in Battle
			continue;
		}

		if (!P_CheckSight(stplyr->mo, ntplayer->mo))
		{
			// Can't see
			continue;
		}

		v.x = R_InterpolateFixed(ntplayer->mo->old_x, ntplayer->mo->x);
		v.y = R_InterpolateFixed(ntplayer->mo->old_y, ntplayer->mo->y);
		v.z = R_InterpolateFixed(ntplayer->mo->old_z, ntplayer->mo->z);

		if (!(ntplayer->mo->eflags & MFE_VERTICALFLIP))
		{
			v.z += ntplayer->mo->height;
		}

		distance = R_PointToDist2(c.x, c.y, v.x, v.y);

		if (distance > maxdistance)
		{
			// Too far away
			continue;
		}

		tobesorted[sortlen] = ntplayer - players;
		sortdist[sortlen] = distance;
		sortlen++;
	}

	if (sortlen > 0)
	{
		UINT8 sortedplayers[sortlen];

		for (i = 0; i < sortlen; i++)
		{
			UINT8 pos = 0;

			for (j = 0; j < sortlen; j++)
			{
				if (j == i)
				{
					continue;
				}

				if (sortdist[i] < sortdist[j]
				|| (sortdist[i] == sortdist[j] && i > j))
				{
					pos++;
				}
			}

			sortedplayers[pos] = tobesorted[i];
		}

		for (i = 0; i < sortlen; i++)
		{
			trackingResult_t result;
			player_t *ntplayer = &players[sortedplayers[i]];

			fixed_t headOffset = 36*ntplayer->mo->scale;

			SINT8 localindicator = -1;
			vector3_t v;

			v.x = R_InterpolateFixed(ntplayer->mo->old_x, ntplayer->mo->x);
			v.y = R_InterpolateFixed(ntplayer->mo->old_y, ntplayer->mo->y);
			v.z = R_InterpolateFixed(ntplayer->mo->old_z, ntplayer->mo->z);

			v.z += (ntplayer->mo->height / 2);

			if (stplyr->mo->eflags & MFE_VERTICALFLIP)
			{
				v.z -= headOffset;
			}
			else
			{
				v.z += headOffset;
			}

			K_ObjectTracking(&result, &v, false);

			if (result.onScreen == true)
			{
				if (!(demo.playback == true && camera[cnum].freecam == true))
				{
					for (j = 0; j <= r_splitscreen; j++)
					{
						if (ntplayer == &players[displayplayers[j]])
						{
							break;
						}
					}

					if (j <= r_splitscreen && j != cnum)
					{
						localindicator = j;
					}
				}

				if (localindicator >= 0)
				{
					K_DrawLocalTagForPlayer(result.x, result.y, ntplayer, localindicator);
				}
				else if (ntplayer->bot)
				{
					if (ntplayer->botvars.rival == true)
					{
						K_DrawRivalTagForPlayer(result.x, result.y);
					}
				}
				else if (netgame || demo.playback)
				{
					if (K_ShowPlayerNametag(ntplayer) == true)
					{
						K_DrawNameTagForPlayer(result.x, result.y, ntplayer);
						K_DrawTypingNotifier(result.x, result.y, ntplayer);
					}
				}
			}
		}
	}

	V_ClearClipRect();
}

static void K_drawKartMinimapIcon(fixed_t objx, fixed_t objy, INT32 hudx, INT32 hudy, INT32 flags, patch_t *icon, UINT8 *colormap)
{
	// amnum xpos & ypos are the icon's speed around the HUD.
	// The number being divided by is for how fast it moves.
	// The higher the number, the slower it moves.

	// am xpos & ypos are the icon's starting position. Withouht
	// it, they wouldn't 'spawn' on the top-right side of the HUD.

	fixed_t amnumxpos, amnumypos;
	INT32 amxpos, amypos;
	fixed_t scale = FRACUNIT;

	if (!cv_showminimapangle.value && (icon == kp_minimapdot))
		return;

	if (cv_minihead.value)
		scale = FRACUNIT / 2;

	amnumxpos = (FixedMul(objx, minimapinfo.zoom) - minimapinfo.offs_x);
	amnumypos = -(FixedMul(objy, minimapinfo.zoom) - minimapinfo.offs_y);

	if (encoremode)
		amnumxpos = -amnumxpos;

	amxpos = amnumxpos + ((hudx + (SHORT(minimapinfo.minimap_pic->width)-SHORT(icon->width))/2)<<FRACBITS);
	amypos = amnumypos + ((hudy + (SHORT(minimapinfo.minimap_pic->height)-SHORT(icon->height))/2)<<FRACBITS);

	if (cv_minihead.value && !(icon == kp_minimapdot))
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

static void K_drawKartMinimapNametag(fixed_t objx, fixed_t objy, INT32 hudx, INT32 hudy, INT32 flags, player_t *player)
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
	INT32 amxpos, amypos;
	UINT16 skin = 0;
	UINT16 chatcolor = skincolors[player->mo->color].chatcolor;

	amnumxpos = (FixedMul(objx, minimapinfo.zoom) - minimapinfo.offs_x);
	amnumypos = -(FixedMul(objy, minimapinfo.zoom) - minimapinfo.offs_y);

	if (encoremode)
		amnumxpos = -amnumxpos;

	skin = ((skin_t*)player->mo->skin)-skins;

	amxpos = amnumxpos + ((hudx + (SHORT(minimapinfo.minimap_pic->width)-SHORT(faceprefix[skin][FACE_MINIMAP]->width))/2)<<FRACBITS);
	amypos = amnumypos + ((hudy + (SHORT(minimapinfo.minimap_pic->height)-SHORT(faceprefix[skin][FACE_MINIMAP]->height))/2)<<FRACBITS);

	const char *player_name = va("%s",player_names[player - players]);
	V_DrawCenteredSmallStringAtFixed(amxpos + (4*FRACUNIT), amypos - (3*FRACUNIT), V_ALLOWLOWERCASE|flags|chatcolor, player_name);
}

static void K_drawKartMinimapDot(fixed_t objx, fixed_t objy, INT32 hudx, INT32 hudy, INT32 flags, UINT8 color, UINT8 size)
{
	fixed_t amnumxpos, amnumypos;
	INT32 amxpos, amypos;

	amnumxpos = (FixedMul(objx, minimapinfo.zoom) - minimapinfo.offs_x);
	amnumypos = -(FixedMul(objy, minimapinfo.zoom) - minimapinfo.offs_y);

	if (encoremode)
		amnumxpos = -amnumxpos;

	amxpos = (amnumxpos / FRACUNIT) + (SHORT(minimapinfo.minimap_pic->width) / 2);
	amypos = (amnumypos / FRACUNIT) + (SHORT(minimapinfo.minimap_pic->height) / 2);

	if (flags & V_NOSCALESTART)
	{
		amxpos *= vid.dupx;
		amypos *= vid.dupy;
	}

	V_DrawFill((amxpos + hudx) - (size / 2), (amypos + hudy) - (size / 2), size, size, flags | color);
}

static void K_drawKartMinimapWaypoint(waypoint_t *wp, INT32 hudx, INT32 hudy, INT32 flags)
{
	UINT8 pal = 0x95; // blue
	UINT8 size = 3;

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

	if (!(flags & V_NOSCALESTART))
	{
		hudx *= vid.dupx;
		hudy *= vid.dupy;
	}

	K_drawKartMinimapDot(wp->mobj->x, wp->mobj->y, hudx, hudy, flags | V_NOSCALESTART, pal, size);
}

static void K_drawKartMinimapCluster(INT32 hudx, INT32 hudy, INT32 flags)
{
	UINT8 pal = 180; // Strong pink color.
	UINT8 size = 6;

	if (!(flags & V_NOSCALESTART))
	{
		hudx *= vid.dupx;
		hudy *= vid.dupy;
	}

	K_drawKartMinimapDot(clusterpoint.x, clusterpoint.y, hudx, hudy, flags | V_NOSCALESTART, pal, size);
}

#define ICON_DOT_RADIUS (cv_minihead.value && !cv_showminimapnames.value) ? 8 : 10

typedef struct
{
	boolean draw;
	boolean red;
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

static void K_drawKartMinimap(void)
{
	patch_t *workingPic;
	INT32 i = 0;
	INT32 x, y;
	INT32 minimaptrans = cv_kartminimap.value;
	INT32 splitflags = 0;
	UINT16 skin = 0;
	UINT8 *colormap = NULL;
	SINT8 localplayers[MAXSPLITSCREENPLAYERS];
	SINT8 numlocalplayers = 0;
	mobj_t *mobj, *next;	// for SPB drawing (or any other item(s) we may wanna draw, I dunno!)
	fixed_t interpx, interpy;
	spbdraw_t spb;
	UINT16 usecolor;
	boolean colorizeplayer;

	// Draw the HUD only when playing in a level.
	// hu_stuff needs this, unlike st_stuff.
	if (gamestate != GS_LEVEL)
		return;

	// Only draw for the first player
	// Maybe move this somewhere else where this won't be a concern?
	if (stplyr != &players[displayplayers[0]])
		return;

	if (minimapinfo.minimap_pic == NULL)
	{
		return; // no pic, just get outta here
	}

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
			minimaptrans = (((INT32)lt_exitticker)*minimaptrans)/((INT32)length);
		
	}
	else if (r_splitscreen == 3) // 4P centered
	{
		const tic_t length = TICRATE/2;

		if (!lt_exitticker)
			return;
		if (lt_exitticker < length)
			minimaptrans = (((INT32)lt_exitticker)*minimaptrans)/((INT32)length);
	}
	// 3P lives in the middle of the bottom right player and shouldn't fade in OR slide

	if (!minimaptrans)
		return;

	colorizeplayer = false;

	minimaptrans = ((10-minimaptrans)<<FF_TRANSSHIFT);

	if (encoremode)
		V_DrawScaledPatch(x+SHORT(minimapinfo.minimap_pic->width), y, splitflags|minimaptrans|V_FLIP, minimapinfo.minimap_pic);
	else
		V_DrawScaledPatch(x, y, splitflags|minimaptrans, minimapinfo.minimap_pic);

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
				skin = ((skin_t*)g->mo->skin)-skins;
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

			K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, faceprefix[skin][FACE_MINIMAP], colormap);
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
			if (!players[i].mo || players[i].spectator || !players[i].mo->skin || (!cv_showfinishedplayers.value && stplyr->exiting))
				continue;

			if (i == displayplayers[0] || i == displayplayers[1] || i == displayplayers[2] || i == displayplayers[3])
			{
				// Draw display players on top of everything else
				localplayers[numlocalplayers++] = i;
				continue;
			}

			// Now we know it's not a display player, handle non-local player exceptions.
			if ((gametyperules & GTR_BUMPERS) && players[i].bumper <= 0)
				continue;

			if (players[i].hyudorotimer > 0)
			{
				if (!((players[i].hyudorotimer < TICRATE/2
					|| players[i].hyudorotimer > hyudorotime-(TICRATE/2))
					&& !(leveltime & 1)))
					continue;
			}

			mobj = players[i].mo;

			if (players[i].mo->health <= 0 && players[i].pflags & PF_NOCONTEST)
			{
				workingPic = kp_nocontestminimap;
				colormap = R_GetTranslationColormap(TC_DEFAULT, mobj->color, GTC_CACHE);
			}
			else
			{
				skin = ((skin_t*)players[i].mo->skin)-skins;

				workingPic = faceprefix[skin][FACE_MINIMAP];

				colorizeplayer = players[i].mo->colorized;

				if ((players[i].invincibilitytimer) && (K_InvincibilityGradient(players[i].invincibilitytimer) > (FRACUNIT/2)))
				{
					usecolor = (K_RainbowColor(leveltime / 2));
					colorizeplayer = true;
				}
				else
				{
					usecolor = players[i].mo->color;
				}

				if (players[i].mo->color)
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
				interpx = R_InterpolateFixed(mobj->old_x, mobj->x);
				interpy = R_InterpolateFixed(mobj->old_y, mobj->y);

				K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, workingPic, colormap);

				if (mobj->player)
				{
					// Draw the Nametag
					K_drawKartMinimapNametag(interpx, interpy, x, y, splitflags, &players[i]);
				}

				// Target reticule
				if ((gametype == GT_RACE && players[i].position == spbplace)
					|| (gametype == GT_BATTLE && K_IsPlayerWanted(&players[i])))
				{
					K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, kp_wantedreticle, NULL);
				}
			}
		}
	}

	// draw minimap-pertinent objects
	for (mobj = kitemcap; mobj; mobj = next)
	{
		next = mobj->itnext;

		workingPic = NULL;
		colormap = NULL;

		if (mobj->health <= 0)
			continue;

		switch (mobj->type)
		{
			case MT_SPB:
				spb = K_ShouldDrawSPB(mobj);

				if (spb.draw)
				{
					INT32 tc = spb.red ? TC_BLINK : TC_DEFAULT;
					skincolornum_t color = SKINCOLOR_RED;

					// If not flashing red then...
					if (!spb.red && mobj->color != SKINCOLOR_NONE)
					{
						// Use SPB's current color.
						color = mobj->color;
					}

					workingPic = kp_spbminimap;
					colormap = R_GetTranslationColormap(tc, color, GTC_CACHE);
				}
				else
				{
					workingPic = NULL;
				}
				break;
			case MT_RANDOMITEM:
				if (itembreaker && (mobj->flags2 & MF2_BOSSNOTRAP) && !(mobj->flags2 & MF2_BOSSFLEE))
				{
					workingPic = kp_itemboxminimap;
					colormap = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_YELLOW, GTC_CACHE);
				}
				else
					workingPic = NULL;
				break;
			default:
				break;
		}

		if (!workingPic)
			continue;

		interpx = R_InterpolateFixed(mobj->old_x, mobj->x);
		interpy = R_InterpolateFixed(mobj->old_y, mobj->y);

		K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, workingPic, colormap);
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
			K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, kp_wantedreticle, colormap);
		}
	}

	for (i = 0; i < numlocalplayers; i++)
	{
		boolean nocontest = false;

		if (localplayers[i] == -1)
			continue; // this doesn't interest us

		if ((players[localplayers[i]].hyudorotimer > 0) && (leveltime & 1))
			continue;

		mobj = players[localplayers[i]].mo;

		if (players[localplayers[i]].mo->health <= 0 && players[localplayers[i]].pflags & PF_NOCONTEST)
		{
			workingPic = kp_nocontestminimap;
			colormap = R_GetTranslationColormap(TC_DEFAULT, mobj->color, GTC_CACHE);

			nocontest = true;
		}
		else
		{
			skin = ((skin_t*)players[localplayers[i]].mo->skin)-skins;

			workingPic = faceprefix[skin][FACE_MINIMAP];

			colorizeplayer = players[localplayers[i]].mo->colorized;

			if ((players[localplayers[i]].invincibilitytimer) && (K_InvincibilityGradient(players[localplayers[i]].invincibilitytimer) > (FRACUNIT/2)))
			{
				usecolor = (K_RainbowColor(leveltime / 2));
				colorizeplayer = true;
			}
			else
			{
				usecolor = players[localplayers[i]].mo->color;
			}

			if (players[localplayers[i]].mo->color)
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

			if (skin && mobj->color && !mobj->colorized // relevant to redo
				&& skins[skin].starttranscolor != skins[0].starttranscolor) // redoing would have an affect
			{
				colormap = R_GetTranslationColormap(TC_DEFAULT, mobj->color, GTC_CACHE);
			}

			interpx = R_InterpolateFixed(mobj->old_x, mobj->x);
			interpy = R_InterpolateFixed(mobj->old_y, mobj->y);

			K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, workingPic, colormap);

			// Target reticule
			if ((gametype == GT_RACE && players[localplayers[i]].position == spbplace)
				|| (gametype == GT_BATTLE && K_IsPlayerWanted(&players[localplayers[i]])))
			{
				K_drawKartMinimapIcon(interpx, interpy, x, y, splitflags, kp_wantedreticle, NULL);
			}

			if (!nocontest)
			{
				K_drawKartMinimapIcon(
						interpx,
						interpy,
						x + FixedMul(FCOS(ang), ICON_DOT_RADIUS),
						y - FixedMul(FSIN(ang), ICON_DOT_RADIUS),
						splitflags,
						kp_minimapdot,
						colormap
				);
			}

			K_drawKartMinimapNametag(interpx, interpy, x, y, splitflags, mobj->player);
		}
	}

	if (cv_kartdebugwaypoints.value != 0)
	{
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

	if (cv_kartdebugcluster.value != 0)
	{
		K_drawKartMinimapCluster(x, y, splitflags);
	}
}


static void K_drawKartStartCountdown(void)
{
	INT32 pnum = 0;

	if (leveltime > starttime-(3*TICRATE))
	{

		if (leveltime >= starttime-(2*TICRATE)) // 2
			pnum++;
		if (leveltime >= starttime-TICRATE) // 1
			pnum++;

		if (leveltime >= starttime) // GO!
		{
			UINT8 i;
			UINT8 numplayers = 0;

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

		V_DrawScaledPatch(STCD_X - (SHORT(kp_startcountdown[pnum]->width)/2), STCD_Y - (SHORT(kp_startcountdown[pnum]->height)/2), V_SPLITSCREEN, kp_startcountdown[pnum]);
	}
}

static void K_drawKartFinish(void)
{
	INT32 pnum = 0, splitflags = V_SPLITSCREEN;

	if (!stplyr->karthud[khud_cardanimation] || stplyr->karthud[khud_cardanimation] >= 2*TICRATE)
		return;

	if ((stplyr->karthud[khud_cardanimation] % (2*5)) / 5) // blink
		pnum = 1;

	if (r_splitscreen > 1) // 3/4p, stationary FIN
	{
		pnum += 2;
		V_DrawScaledPatch(STCD_X - (SHORT(kp_racefinish[pnum]->width)/2), STCD_Y - (SHORT(kp_racefinish[pnum]->height)/2), splitflags, kp_racefinish[pnum]);
		return;
	}

	//else -- 1/2p, scrolling FINISH
	{
		INT32 x, xval, ox, interpx, pwidth;

		if (r_splitscreen) // wide splitscreen
			pnum += 4;

		x = ((vid.width<<FRACBITS)/vid.dupx);
		xval = (SHORT(kp_racefinish[pnum]->width)<<FRACBITS);

		pwidth = max(xval, x);

		x = ((TICRATE - stplyr->karthud[khud_cardanimation]) * pwidth) / TICRATE;
		ox = ((TICRATE - (stplyr->karthud[khud_cardanimation] - 1)) * pwidth) / TICRATE;


		interpx = R_InterpolateFixed(ox, x);

		if (r_splitscreen && stplyr == &players[displayplayers[1]])
			interpx = -interpx;

		V_DrawFixedPatch(interpx + (STCD_X<<FRACBITS) - (pwidth / 2),
			(STCD_Y<<FRACBITS) - (SHORT(kp_racefinish[pnum]->height)<<(FRACBITS-1)),
			FRACUNIT,
			splitflags, kp_racefinish[pnum], NULL);
	}
}

static void K_drawBattleFullscreen(void)
{

	INT32 cardanim = stplyr->karthud[khud_cardanimation] << FRACBITS;

	// fill in the fractional bits
	if (cardanim && cardanim != 164*FRACUNIT)
	{
		INT32 frac = R_GetHudUncap() * ((164 - stplyr->karthud[khud_cardanimation])/8 + 1);
		if (stplyr->exiting)
			cardanim += frac;
		else
			cardanim += stplyr->karmadelay < 6*TICRATE ? -frac : frac;
	}

	INT32 x = BASEVIDWIDTH/2;
	INT32 y = (-64*FRACUNIT) + cardanim; // card animation goes from 0 to 164, 164 is the middle of the screen the screen
	INT32 splitflags = V_SNAPTOTOP; // I don't feel like properly supporting non-green resolutions, so you can have a misuse of SNAPTO instead
	fixed_t scale = FRACUNIT;
	boolean drawcomebacktimer = true;	// lazy hack because it's cleaner in the long run.

	if (!LUA_HudEnabled(hud_battlecomebacktimer))
		drawcomebacktimer = false;

	if (r_splitscreen)
	{
		if ((splitscreen == 1 && stplyrnum == 1) || (splitscreen > 1 && stplyrnum & 2))
		{
			y = 232-(cardanim/2);
			splitflags = V_SNAPTOBOTTOM;
		}
		else
			y = -32+(cardanim/2);

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
		if (stplyr->exiting < 6*TICRATE && !stplyr->spectator)
		{
			patch_t *p = kp_battlecool;

			if (K_IsPlayerLosing(stplyr))
				p = kp_battlelose;
			else if (stplyr->position == 1 && (!itembreaker || numtargets >= nummapboxes))
				p = kp_battlewin;

			V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, scale, splitflags, p, NULL);
		}
		else
			K_drawKartFinish();
	}
	else if (stplyr->bumper <= 0 && stplyr->karmadelay && comeback && !stplyr->spectator && drawcomebacktimer)
	{
		UINT16 t = stplyr->karmadelay/(10*TICRATE);
		INT32 txoff, adjust = (r_splitscreen > 1) ? 4 : 6; // normal string is 8, kart string is 12, half of that for ease
		INT32 ty = (BASEVIDHEIGHT/2)+66;

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
			V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, scale, splitflags, kp_battleinfo, NULL);
		else
			V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, scale, splitflags, kp_battlewait, NULL);

		if (r_splitscreen > 1)
			V_DrawString(x-txoff, ty, 0, va("%d", stplyr->karmadelay/TICRATE));
		else
		{
			if (!K_UseColorHud())
				V_DrawFixedPatch(x<<FRACBITS, ty<<FRACBITS, scale, 0, kp_timeoutsticker[0], NULL);
			else //Colourized hud
			{
				UINT8 *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
				V_DrawFixedPatch(x<<FRACBITS, ty<<FRACBITS, scale, 0, kp_timeoutsticker[1], colormap);
			}
			V_DrawKartString(x-txoff, ty, 0, va("%d", stplyr->karmadelay/TICRATE));
		}
	}

	// FREE PLAY?
	{
		UINT8 i;

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
	static INT32 pnum[4], turn[4], drift[4];
	const INT16 steerThreshold = KART_FULLTURN / 2;
	INT32 pn = 0, tn = 0, dr = 0;
	INT32 target = 0, splitflags = V_SNAPTOBOTTOM|V_SPLITSCREEN;
	INT32 x = BASEVIDWIDTH/2, y = BASEVIDHEIGHT;
	fixed_t scale;
	UINT8 *colmap = NULL;

	if (stplyr->spectator || !stplyr->mo || (stplyr->mo->renderflags & RF_DONTDRAW))
		return;

	if (stplyr == &players[displayplayers[1]] && r_splitscreen)
		{ pn = pnum[1]; tn = turn[1]; dr = drift[1]; }
	else if (stplyr == &players[displayplayers[2]] && r_splitscreen > 1)
		{ pn = pnum[2]; tn = turn[2]; dr = drift[2]; }
	else if (stplyr == &players[displayplayers[3]] && r_splitscreen > 2)
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
		y += FRACUNIT/(vid.dupx < vid.dupy ? vid.dupx : vid.dupy); // correct a one-pixel gap on the screen view (not the basevid view)
	}
	else if (r_splitscreen)
		scale = FRACUNIT/2;
	else
		scale = FRACUNIT;

	if (stplyr->mo)
	{
		UINT8 driftcolor = K_DriftSparkColor(stplyr, stplyr->driftcharge);
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

	if (stplyr == &players[displayplayers[1]] && r_splitscreen)
		{ pnum[1] = pn; turn[1] = tn; drift[1] = dr; }
	else if (stplyr == &players[displayplayers[2]] && r_splitscreen > 1)
		{ pnum[2] = pn; turn[2] = tn; drift[2] = dr; }
	else if (stplyr == &players[displayplayers[3]] && r_splitscreen > 2)
		{ pnum[3] = pn; turn[3] = tn; drift[3] = dr; }
	else
		{ pnum[0] = pn; turn[0] = tn; drift[0] = dr; }
}

// doesn't need to ever support 4p
static void K_drawInput(void)
{
	static INT32 pn = 0;
	INT32 target = 0, splitflags = (V_SNAPTOBOTTOM|V_SNAPTORIGHT|V_SLIDEIN);
	INT32 x = (BASEVIDWIDTH - 32)*FRACUNIT, y = (BASEVIDHEIGHT - 24)*FRACUNIT;
	UINT8 *shadowcolormap = NULL;
	INT32 offs, col;
	const UINT8 hudcolor = K_GetHudColor();
	const INT32 accent1 = splitflags | skincolors[hudcolor].ramp[5];
	const INT32 accent2 = splitflags | skincolors[hudcolor].ramp[9];
	const UINT8 *hudcolormap = R_GetTranslationColormap(0, hudcolor, GTC_CACHE);

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
		INT32 joyx, joyxoffs, joyy, joyyoffs;
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
			INT16 turning = encoremode ? -stplyr->cmd.turning : stplyr->cmd.turning;
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
		if (!stplyr->cmd.turning) // no turn
			target = 0;
		else // turning of multiple strengths!
		{
			target = ((abs(stplyr->cmd.turning) - 1)/125)+1;
			if (target > 4)
				target = 4;
			if (stplyr->cmd.turning < 0)
				target = -target;
		}

		if (pn != target)
		{
			if (abs(pn - target) == 1)
				pn = target;
			else if (pn < target)
				pn += 2;
			else //if (pn > target)
				pn -= 2;
		}

		if (pn < 0)
		{
			splitflags |= V_FLIP; // right turn
			x -= FRACUNIT;
		}

		target = abs(pn);
		if (target > 4)
			target = 4;

		if (!K_GetHudColor())
			V_DrawFixedPatch(x, y, FRACUNIT, splitflags, kp_inputwheel[target], NULL);
		else
		{
			UINT8 *colormap;
			colormap = R_GetTranslationColormap(0, K_GetHudColor(), GTC_CACHE);
			V_DrawFixedPatch(x, y, FRACUNIT, splitflags, kp_inputwheel[target], colormap);
		}
	}
}

static void K_drawChallengerScreen(void)
{
	// This is an insanely complicated animation.
	static UINT8 anim[52] = {
		0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13, // frame 1-14, 2 tics: HERE COMES A NEW slides in
		14,14,14,14,14,14, // frame 15, 6 tics: pause on the W
		15,16,17,18, // frame 16-19, 1 tic: CHALLENGER approaches screen
		19,20,19,20,19,20,19,20,19,20, // frame 20-21, 1 tic, 5 alternating: all text vibrates from impact
		21,22,23,24 // frame 22-25, 1 tic: CHALLENGER turns gold
	};
	const UINT8 offset = min(52-1, (3*TICRATE)-mapreset);

	V_DrawFadeScreen(0xFF00, 16); // Fade out
	V_DrawScaledPatch(0, 0, 0, kp_challenger[anim[offset]]);
}

static void K_drawLapStartAnim(void)
{
	if (!cv_showlapemblem.value)
		return;

	// This is an EVEN MORE insanely complicated animation.
	const UINT8 t = stplyr->karthud[khud_lapanimation];
	const UINT8 progress = 80 - t;

	const UINT8 tOld = t + 1;
	const UINT8 progressOld = 80 - tOld;

	const tic_t leveltimeOld = leveltime - 1;

	UINT8 *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);

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

	if (stplyr->laps == (UINT8)(numlaps))
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
				kp_lapanim_number[(((UINT32)stplyr->laps) / 10)][min(progress/2-8, 2)], NULL);

			if (progress/2-10 >= 0)
			{
				newval = (208 + (32 * max(0, progress - 76))) * FRACUNIT;
				oldval = (208 + (32 * max(0, progressOld - 76))) * FRACUNIT;
				interpx = R_InterpolateFixed(oldval, newval);

				V_DrawFixedPatch(
					interpx, // 221
					30*FRACUNIT, // 24
					FRACUNIT, V_SNAPTOTOP|V_HUDTRANS,
					kp_lapanim_number[(((UINT32)stplyr->laps) % 10)][min(progress/2-10, 2)], NULL);
			}
		}
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
Draw_party_ping (int ss, INT32 snap)
{
	HU_drawMiniPing(0, 0, playerpingtable[displayplayers[ss]], V_HUDTRANS|V_SPLITSCREEN|V_SNAPTOTOP|snap);
}

static void
K_drawMiniPing (void)
{
	UINT32 f = V_SNAPTORIGHT;
	UINT8 i;

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

static patch_t *K_GetSmallStaticCachedItemPatch(kartitems_t item)
{
	UINT8 offset;

	item = K_ItemResultToType(item);

	switch (item)
	{
		case KITEM_INVINCIBILITY:
			offset = 7;
			break;

		case KITEM_BANANA:
			offset = 4;
			break;

		case KITEM_ORBINAUT:
			offset = 4;
			break;

		case KITEM_JAWZ:
			offset = 2;
			break;

		case KITEM_SNEAKER:
			offset = 3;
			break;

		default:
			offset = 1;
	}

	return K_GetCachedItemPatch(item, offset);
}

static void K_drawDistributionDebugger(void)
{
	UINT8 useodds = 0;
	UINT8 pingame = 0, bestbumper = 0;
	UINT32 pdis = 0;
	INT32 i;
	INT32 item;
	INT32 x = -9, y = -9;
	boolean dontforcespb = false;
	boolean spbrush = false;

	//if (stplyr != &players[displayplayers[0]]) // only for p1
		//return;

	// The only code duplication from the Kart, just to avoid the actual item function from calculating pingame twice
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		pingame++;
		if (players[i].exiting)
			dontforcespb = true;
		if (players[i].bumper > bestbumper)
			bestbumper = players[i].bumper;
	}

	if (!K_UsingLegacyCheckpoints())
	{
		// lovely double loop......
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && !players[i].spectator
				&& players[i].position == 1)
			{
				// This player is first! Yay!
				pdis = stplyr->distancetofinish - players[i].distancetofinish;
				break;
			}
		}
	}
	else
	{
		// Pain and fucking suffering.
		SINT8 sortedPlayers[MAXPLAYERS];
		UINT8 sortLength = 0;

		memset(sortedPlayers, -1, sizeof(sortedPlayers));

		if (stplyr->mo != NULL && P_MobjWasRemoved(stplyr->mo) == false)
		{
			// Sort all of the players ahead of you.
			// Then tally up their distances in a conga line.

			// This will create a much more consistent item
			// distance algorithm than the "spider web" thing
			// that it was doing before.

			// Add yourself to the list.
			// You'll always be the end of the list,
			// so we can also calculate the length here.
			sortedPlayers[ stplyr->position - 1 ] = stplyr - players;
			sortLength = stplyr->position;

			// Will only need to do this if there's goint to be
			// more than yourself in the list.
			if (sortLength > 1)
			{
				SINT8 firstIndex = -1;
				SINT8 secondIndex = -1;
				INT32 startFrom = INT32_MAX;

				// Add all of the other players.
				for (i = 0; i < MAXPLAYERS; i++)
				{
					INT32 pos = INT32_MAX;

					if (!playeringame[i] || players[i].spectator)
					{
						continue;
					}

					if (players[i].mo == NULL || P_MobjWasRemoved(players[i].mo) == true)
					{
						continue;
					}

					pos = players[i].position;

					if (pos <= 0 || pos > MAXPLAYERS)
					{
						// Invalid position.
						continue;
					}

					if (pos >= stplyr->position)
					{
						// Tied / behind us.
						// Also handles ourselves, obviously.
						continue;
					}

					// Ties are done with port priority, if there are any.
					if (sortedPlayers[ pos - 1 ] == -1)
					{
						sortedPlayers[ pos - 1 ] = i;
					}
				}

				// The chance of this list having gaps is improbable,
				// but not impossible. So we need to spend some extra time
				// to prevent the gaps from mattering.
				for (i = 0; i < sortLength-1; i++)
				{
					if (sortedPlayers[i] >= 0 && sortedPlayers[i] < MAXPLAYERS)
					{
						// First valid index in the list found.
						firstIndex = sortedPlayers[i];

						// Start the next loop after this player.
						startFrom = i + 1;
						break;
					}
				}

				if (firstIndex >= 0 && firstIndex < MAXPLAYERS
					&& startFrom < sortLength)
				{
					// First index is valid, so we can
					// start comparing the players.

					player_t *firstPlayer = NULL;
					player_t *secondPlayer = NULL;

					for (i = startFrom; i < sortLength; i++)
					{
						if (sortedPlayers[i] >= 0 && sortedPlayers[i] < MAXPLAYERS)
						{
							secondIndex = sortedPlayers[i];

							firstPlayer = &players[firstIndex];
							secondPlayer = &players[secondIndex];

							// Add the distance to the player behind you.
							pdis += P_AproxDistance(P_AproxDistance(
										firstPlayer->mo->x - secondPlayer->mo->x,
										firstPlayer->mo->y - secondPlayer->mo->y),
										firstPlayer->mo->z - secondPlayer->mo->z) / FRACUNIT;

							// Advance to next index.
							firstIndex = secondIndex;
						}
					}
				}
			}
		}

	}

	if (spbplace != -1 && stplyr->position == spbplace+1)
	{
		// SPB Rush Mode: It's 2nd place's job to catch-up items and make 1st place's job hell
		if (!(K_UsingLegacyCheckpoints()))
			pdis = (3 * pdis) / 2;
		spbrush = true;
	}

	pdis = K_ScaleItemDistance(pdis, pingame, spbrush);

	if (stplyr->bot && stplyr->botvars.rival)
	{
		// Rival has better odds :)
		pdis = (15 * pdis) / 14;
	}

	if (K_UsingLegacyCheckpoints())
		useodds = K_FindLegacyUseodds(stplyr, 0, pingame, bestbumper, spbrush, dontforcespb);
	else
		useodds = K_FindUseodds(stplyr, 0, pdis, bestbumper, spbrush);

	if (pingame == 1)
	{
		if (stplyr->itemroulette && (stplyr->cmd.buttons & BT_ATTACK) && cv_superring.value && (K_RingsActive() == true))
			V_DrawScaledPatch(x, y, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, K_GetSmallStaticCachedItemPatch(KITEM_SUPERRING));
		else
			V_DrawScaledPatch(x, y, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, K_GetSmallStaticCachedItemPatch(KITEM_SNEAKER));
		V_DrawThinString(x+11, y+31, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, va("%d", 200));
	}
	else if (useodds == 69)
	{
		V_DrawScaledPatch(x, y, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, K_GetSmallStaticCachedItemPatch(KITEM_SPB));
		V_DrawThinString(x+11, y+31, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, va("%d", 200));

	}
	else
	{

		for (item = 1; item < NUMKARTRESULTS; item++)
		{
			INT32 itemodds;
			INT32 amount;

			if (K_UsingLegacyCheckpoints())
				itemodds = K_KartGetLegacyItemOdds(useodds, item, 0, spbrush);
			else
				itemodds = K_KartGetItemOdds(
					useodds, item,
					stplyr->distancetofinish,
					0,
					spbrush, stplyr->bot, (stplyr->bot && stplyr->botvars.rival)
				);

			if (itemodds <= 0)
				continue;

			V_DrawScaledPatch(x, y, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, K_GetSmallStaticCachedItemPatch(item));
			V_DrawThinString(x+11, y+31, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, va("%d", itemodds));

			// Display amount for multi-items
			amount = K_ItemResultToAmount(item);
			if (amount > 1)
			{
				V_DrawString(x+24, y+31, V_SPLITSCREEN|V_ALLOWLOWERCASE|V_HUDTRANS|V_SNAPTOTOP, va("x%d", amount));
			}


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
	else if (useodds == 69)
		V_DrawString(0, 0, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, "FORCED SPB");
	else
		V_DrawString(0, 0, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, va("USEODDS %d", useodds));

	V_DrawString(150, 0, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, va("PDIS %d", pdis));

	if (K_UsingLegacyCheckpoints())
		V_DrawSmallString(70, 0, V_SPLITSCREEN|V_HUDTRANS|V_SNAPTOTOP, "Legacy Distance Mode");

}

static void K_drawCheckpointDebugger(void)
{
	if (stplyr != &players[displayplayers[0]]) // only for p1
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

	if (stplyr != &players[displayplayers[0]]) // only for p1
		return;

	if (stplyr->bigwaypointgap)
	{
		V_DrawString(8, 146, 0, va("Auto Respawn Timer: %d", stplyr->bigwaypointgap));
	}

	V_DrawString(8, 156, 0, va("Current Waypoint ID: %d", K_GetWaypointID(stplyr->currentwaypoint)));
	V_DrawString(8, 166, 0, va("Next Waypoint ID: %d", K_GetWaypointID(stplyr->nextwaypoint)));
	V_DrawString(8, 176, 0, va("Finishline Distance: %d", stplyr->distancetofinish));
}

static void K_DrawClusterDebugger(void)
{
	if (cv_kartdebugcluster.value == 0)
		return;

	if (stplyr != &players[displayplayers[0]]) // only for p1
		return;
	
	INT32 vflags = V_6WIDTHSPACE|V_ALLOWLOWERCASE;

	if (K_UsingLegacyCheckpoints() && !(gametype == GT_BATTLE))
	{
		V_DrawThinString(8, 136, vflags, va("Cluster player: %s", player_names[clusterid]));
		V_DrawThinString(8, 146, vflags, va("X: %f, Y: %f, Z: %f, Dist. from cluster: %d", FIXED_TO_FLOAT(clusterpoint.x), FIXED_TO_FLOAT(clusterpoint.y), FIXED_TO_FLOAT(clusterpoint.z), stplyr->distancefromcluster));
	}
	else
	{
		V_DrawThinString(8, 136, vflags, va("Cluster DtF: %d, Your DtF: %d", clusterdtf.x, stplyr->distancetofinish));
		V_DrawThinString(8, 146, vflags, va("Distance from Cluster: %d", stplyr->distancefromcluster));
	}
}

static void K_DrawBotDebugger(void)
{
	if (!cv_kartdebugbot.value || !stplyr->bot)
		return;

	if (stplyrnum != 0) // only for p1
		return;

	INT32 vflags = V_6WIDTHSPACE|V_ALLOWLOWERCASE;

	static const char *driftstates[] = {
		[DRIFTSTATE_AUTO] = "Auto",
		[DRIFTSTATE_STARTING] = "Starting",
		[DRIFTSTATE_ACTIVE] = "Active",
		[DRIFTSTATE_ENDING] = "Ending",
	};

	V_DrawThinString(24, 100, vflags, va("Drift state: %s", driftstates[stplyr->botvars.driftstate]));
	V_DrawThinString(24, 108, vflags|(stplyr->botvars.driftlockout ? V_ORANGEMAP : 0), va("Drift lockout: %d", stplyr->botvars.driftlockout));
	V_DrawThinString(24, 116, vflags, va("Drift turn: %d", stplyr->botvars.driftturn));
	V_DrawThinString(24, 124, vflags, va("Drift timer: %d", stplyr->botvars.drifttime));
}

void K_drawKartHUD(void)
{
	boolean islonesome = false;
	boolean battlefullscreen = false;
	UINT8 viewnum = R_GetViewNumber();
	boolean freecam = camera[viewnum].freecam;	//disable some hud elements w/ freecam
	UINT8 i;

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
	if (mapreset && stplyr == &players[displayplayers[0]])
	{
		K_drawChallengerScreen();
		return;
	}

	battlefullscreen = ((gametyperules & (GTR_BUMPERS|GTR_KARMA)) == (GTR_BUMPERS|GTR_KARMA)
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
		if (gametype == GT_BATTLE)
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
		if (LUA_HudEnabled(hud_time))
			K_drawKartTimestamp(stplyr->realtime, TIME_X, TIME_Y, gamemap, 0);

		islonesome = K_drawKartPositionFaces();
	}

	if (!stplyr->spectator && !freecam) // Bottom of the screen elements, don't need in spectate mode
	{
		if (demo.title) // Draw title logo instead in demo.titles
		{
			INT32 x = (BASEVIDWIDTH - 32), y = 128, snapflags = V_SNAPTOBOTTOM|V_SNAPTORIGHT;

			if (r_splitscreen == 3)
			{
				x = BASEVIDWIDTH/2 + 10;
				y = BASEVIDHEIGHT/2 - 30;
				snapflags = 0;
			}

			V_DrawTinyScaledPatch(x-54, y, snapflags|V_SLIDEIN, W_CachePatchLongName("TTKBANNR", PU_CACHE));
			V_DrawTinyScaledPatch(x-54, y+25, snapflags|V_SLIDEIN, W_CachePatchLongName("TTKART", PU_CACHE));
		}
		else
		{
			if (LUA_HudEnabled(hud_position))
			{
				if (bossinfo.boss)
				{
					K_drawBossHealthBar();
				}
				else if (gametype == GT_RACE) // Race-only elements (not currently gametyperuleable)
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
				if (gametyperules & GTR_CIRCUIT)
				{
					K_drawKartLaps();
				}
				else if (gametyperules & GTR_BUMPERS)
				{
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
				K_drawKartAccessibilityIcons((cv_newspeedometer.value == 0) ? 50 : 0);
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
	if (!(gametyperules & GTR_FREEROAM) && starttime != introtime
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
			INT32 karlen = strlen(countstr)*6; // half of 12
			V_DrawKartString((BASEVIDWIDTH/2)-karlen, LAPS_Y+3, V_SPLITSCREEN, countstr);
		}
	}

	// Race overlays
	if ((gametyperules & GTR_CIRCUIT) && !freecam)
	{
		if (stplyr->exiting)
			K_drawKartFinish();
		else if (stplyr->karthud[khud_lapanimation] && !r_splitscreen)
			K_drawLapStartAnim();
	}

	if (modeattacking || freecam) // everything after here is MP and debug only
		return;

	if (gametype == GT_BATTLE && !r_splitscreen && (stplyr->karthud[khud_yougotem] % 2)) // * YOU GOT EM *
		V_DrawScaledPatch(BASEVIDWIDTH/2 - (SHORT(kp_yougotem->width)/2), 32, V_HUDTRANS, kp_yougotem);

	// Draw FREE PLAY.
	if (islonesome)
		K_drawKartFreePlay();

	if (r_splitscreen == 0 && (stplyr->pflags & PF_WRONGWAY) && ((leveltime / 8) & 1) && !stplyr->exiting)
	{
		V_DrawCenteredString(BASEVIDWIDTH>>1, 176, V_REDMAP|V_SNAPTOBOTTOM, "WRONG WAY");
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
		UINT8 p;
		for (p = 0; p < MAXPLAYERS; p++)
			V_DrawString(8, 64+(8*p), V_YELLOWMAP, va("%d - %d (%dl)", p, playernode[p], players[p].cmd.latency));
	}

	if (cv_kartdebugcolorize.value && stplyr->mo && stplyr->mo->skin)
	{
		INT32 x = 0, y = 0;
		UINT16 c;

		for (c = 0; c < numskincolors; c++)
		{
			if (skincolors[c].accessible)
			{
				UINT8 *cm = R_GetTranslationColormap(TC_RAINBOW, c, GTC_CACHE);
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
	K_DrawBotDebugger();
	K_DrawClusterDebugger();
	K_DrawDirectorDebugger();
}
