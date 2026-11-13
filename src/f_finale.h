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
/// \file  f_finale.h
/// \brief Title screen, intro, game evaluation, and credits.
///        Also includes protos for screen wipe functions.

#ifndef __F_FINALE__
#define __F_FINALE__

#include "doomtype.h"
#include "d_event.h"
#include "p_mobj.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// FINALE
//

// Called by main loop.
dboolean F_IntroResponder(event_t *ev);
dboolean F_CutsceneResponder(event_t *ev);
dboolean F_CreditResponder(event_t *ev);

// Called by main loop.
void F_IntroTicker(void);
void F_TitleScreenTicker(dboolean run);
void F_CutsceneTicker(void);
void F_TitleDemoTicker(void);
void F_TextPromptTicker(void);

// Called by main loop.
void F_IntroDrawer(void);
void F_TitleScreenDrawer(void);
void F_SkyScroll(int32_t scrollxspeed, int32_t scrollyspeed, const char *patchname);

void F_StartWaitingPlayers(void);
void F_WaitingPlayersTicker(void);
void F_WaitingPlayersDrawer(void);

void F_GameEvaluationDrawer(void);
void F_StartGameEvaluation(void);
void F_GameEvaluationTicker(void);

void F_EndingTicker(void);

void F_CreditTicker(void);
void F_CreditDrawer(void);

void F_BlanCreditTicker(void);
void F_BlanCreditDrawer(void);

void F_StartCustomCutscene(int32_t cutscenenum, dboolean precutscene, dboolean resetplayer);
void F_CutsceneDrawer(void);
void F_EndCutScene(void);

void F_StartTextPrompt(int32_t promptnum, int32_t pagenum, mobj_t *mo, uint16_t postexectag, dboolean blockcontrols, dboolean freezerealtime);
void F_GetPromptPageByNamedTag(const char *tag, int32_t *promptnum, int32_t *pagenum);
void F_TextPromptDrawer(void);
void F_EndTextPrompt(dboolean forceexec, dboolean noexec);
dboolean F_GetPromptHideHudAll(void);
dboolean F_GetPromptHideHud(fixed_t y);

void F_StartIntro(void);
void F_StartTitleScreen(void);
void F_CacheTitleScreen(void);
void F_StartCredits(void);
void F_BlanStartCredits(void);
void F_StartSecretCredits(void);

void F_DrawCompositeGameLogo(tic_t time, int32_t yoffset);

void F_TitleBGScroll(int32_t scrollspeed, int32_t ticker);
void F_BlanTitleBG(int32_t ticker);

extern int32_t finalecount;
extern int32_t menuanimtimer;
extern int32_t titlescrollxspeed;
extern int32_t titlescrollyspeed;

typedef enum
{
	TTMODE_NONE = 0,
	TTMODE_KART,
	TTMODE_USER,
	TTMODE_BLAN
} ttmode_enum;

#define TTMAX_ALACROIX 30 // max frames for SONIC typeface, plus one for NULL terminating entry
#define TTMAX_USER 100

extern ttmode_enum ttmode;
extern uint8_t ttscale;
// ttmode user vars
extern char ttname[9];
extern int16_t ttx;
extern int16_t tty;
extern int16_t ttloop;
extern uint16_t tttics;
extern dboolean ttavailable[6];
extern const char *credits[];
extern const char *blancredits[];

typedef enum
{
	TITLEMAP_OFF = 0,
	TITLEMAP_LOADING,
	TITLEMAP_RUNNING
} titlemap_enum;

// Current menu parameters

extern mobj_t *titlemapcameraref;
extern char curbgname[9];
extern int8_t curfadevalue;
extern int32_t curbgcolor;
extern int32_t curbgxspeed;
extern int32_t curbgyspeed;
extern dboolean curbghide;
extern dboolean hidetitlemap;

extern dboolean curhidepics;
extern ttmode_enum curttmode;
extern uint8_t curttscale;
// ttmode user vars
extern char curttname[9];
extern int16_t curttx;
extern int16_t curtty;
extern int16_t curttloop;
extern uint16_t curtttics;

#define TITLEBACKGROUNDACTIVE (curfadevalue >= 0 || curbgname[0])

void F_InitMenuPresValues(dboolean title);
void F_MenuPresTicker(dboolean run);

//
// WIPE
//

extern uint8_t WipeInAction;
extern dboolean WipeStageTitle;

// Don't know where else to place this constant
// But this file seems appropriate
#define PRELEVELTIME TICRATE // frames in tics

fademask_t *F_GetFadeMask(uint8_t masknum, uint8_t scrnnum);
void F_DoWipe(fademask_t *fademask);
void F_WipeStartScreen(void);
void F_WipeEndScreen(void);
void F_RunWipe(uint8_t wipetype, dboolean drawMenu);
tic_t F_GetWipeLength(uint8_t wipetype);
dboolean F_WipeExists(uint8_t wipetype);
void F_WipeStageTitle(void);
#define F_WipeColorFill(c) V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, c)

enum
{
	wipe_credits_intermediate, // makes a good 0 I guess.

	// Gamestate wipes
	wipe_level_toblack,
	wipe_intermission_toblack,
	wipe_voting_toblack,
	wipe_titlescreen_toblack,
	wipe_menu_toblack,
	wipe_credits_toblack,
	wipe_blancredits_toblack,
	wipe_fullcredits_toblack,
	wipe_evaluation_toblack,
	wipe_intro_toblack,
	wipe_cutscene_toblack,

	// Specialized wipes
	wipe_encore_toinvert,
	wipe_encore_towhite,

	// "From black" wipes
	wipe_level_final,
	wipe_intermission_final,
	wipe_voting_final,
	wipe_titlescreen_final,
	wipe_menu_final,
	wipe_credits_final,
	wipe_blancredits_final,
	wipe_fullcredits_final,
	wipe_evaluation_final,
	wipe_intro_final,
	wipe_cutscene_final,

	NUMWIPEDEFS,
	WIPEFINALSHIFT = (wipe_level_final-wipe_level_toblack)
};

extern uint8_t wipedefs[NUMWIPEDEFS];

#ifdef __cplusplus
} // extern "C"
#endif

#endif
