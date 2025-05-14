// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2011-2016 by Matthew "Inuyasha" Walsh.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  m_menu.c
/// \brief XMOD's extremely revamped menu system.

#include "doomstat.h"
#include "screen.h"
#ifdef __GNUC__
#include <unistd.h>
#endif

#include "m_menu.h"

#include "doomdef.h"
#include "d_main.h"
#include "d_netcmd.h"
#include "console.h"
#include "r_fps.h"
#include "r_local.h"
#include "hu_stuff.h"
#include "g_game.h"
#include "g_input.h"
#include "m_argv.h"

// Data.
#include "sounds.h"
#include "s_sound.h"
#include "i_time.h"
#include "i_system.h"
#include "i_threads.h"

// Addfile
#include "filesrch.h"

#include "v_video.h"
#include "i_video.h"
#include "keys.h"
#include "z_zone.h"
#include "w_wad.h"
#include "p_local.h"
#include "p_setup.h"
#include "f_finale.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

#include "d_net.h"
#include "mserv.h"
#include "m_misc.h"
#include "m_anigif.h"
#include "byteptr.h"
#include "st_stuff.h"
#include "i_sound.h"
#include "k_hud.h" // SRB2kart
#include "k_kart.h" // KartItemCVars
#include "k_pwrlv.h"
#include "k_stats.h" // SRB2kart
#include "d_player.h" // KITEM_ constants
#include "k_color.h"
#include "k_grandprix.h"
#include "k_follower.h"
#include "r_fps.h"

#include "i_joy.h" // for joystick menu controls

// Condition Sets
#include "m_cond.h"

// And just some randomness for the exits.
#include "m_random.h"

#if defined(HAVE_SDL)
#include <SDL.h>
#if SDL_VERSION_ATLEAST(2,0,0)
#include "sdl/sdlmain.h" // JOYSTICK_HOTPLUG
#endif
#endif

#ifdef PC_DOS
#include <stdio.h> // for snprintf
int	snprintf(char *str, size_t n, const char *fmt, ...);
//int	vsnprintf(char *str, size_t n, const char *fmt, va_list ap);
#endif

#ifdef HAVE_DISCORDRPC
//#include "discord_rpc.h"
#include "discord.h"
#endif

#include "qs22j.h"

#define SKULLXOFF -32
#define LINEHEIGHT 16
#define STRINGHEIGHT 8
#define FONTBHEIGHT 20
#define SMALLLINEHEIGHT 8
#define SLIDER_RANGE 10
#define SLIDER_WIDTH (8*SLIDER_RANGE+6)

typedef enum
{
	QUITMSG = 0,
	QUITMSG1,
	QUITMSG2,
	QUITMSG3,
	QUITMSG4,
	QUITMSG5,
	QUITMSG6,
	QUITMSG7,

	QUIT2MSG,
	QUIT2MSG1,
	QUIT2MSG2,
	QUIT2MSG3,
	QUIT2MSG4,
	QUIT2MSG5,
	QUIT2MSG6,

	QUIT3MSG,
	QUIT3MSG1,
	QUIT3MSG2,
	QUIT3MSG3,
	QUIT3MSG4,
	QUIT3MSG5,
	QUIT3MSG6,
	NUM_QUITMESSAGES
} text_enum;

#ifdef HAVE_THREADS
I_mutex m_menu_mutex;
#endif

M_waiting_mode_t m_waiting_mode = M_NOT_WAITING;

const char *quitmsg[NUM_QUITMESSAGES];

// Stuff for customizing the player select screen Tails 09-22-2003
description_t description[MAXSKINS];

boolean fromlevelselect = false;

typedef enum
{
	LLM_CREATESERVER,
	LLM_LEVELSELECT,
	LLM_TIMEATTACK,
	LLM_ITEMBREAKER,
	LLM_BOSS
} levellist_mode_t;

levellist_mode_t levellistmode = LLM_CREATESERVER;
UINT8 maplistoption = 0;

static char joystickInfo[MAXGAMEPADS][29];
static UINT32 serverlistpage;

INT16 startmap; // Mario, NiGHTS, or just a plain old normal game?

menu_t menudefs[NUMMENUTYPES]; // array of all menudefs
menutype_t menustack[NUMMENULEVELS]; // stack of active menus, [0] == current menu
static menu_t *currentMenu; // current menudef
static INT16 itemOn = 1; // menu item skull is on, Hack by Tails 09-18-2002
static INT16 skullAnimCounter = 10; // skull animation counter
static tic_t followertimer = 0;		// Used for smooth follower floating

static struct
{
	boolean active;
	const char *text;
	union
	{
		void (*routine)(INT32 choice); // MM_YESNO
		void (*handler)(event_t *ev);  // MM_EVENTHANDLER
	};
	menumessagetype_t messagetype;
	INT16 x, y;
	INT16 numlines;
	INT16 maxlength;
} messagebox;

static  UINT8 setupcontrolplayer;
static  INT32   (*setupcontrols)[MAXINPUTMAPPING];  // pointer to the gamecontrols of the player being edited

// shhh... what am I doing... nooooo!
static INT32 vidm_testingmode = 0;
static INT32 vidm_previousmode;
static INT32 vidm_selected = 0;
static INT32 vidm_nummodes;
static INT32 vidm_column_size;

// Prototyping is fun, innit?
// ==========================================================================
// NEEDED FUNCTION PROTOTYPES GO HERE
// ==========================================================================

void M_SetWaitingMode(int mode);
int  M_GetWaitingMode(void);

#define lsheadingheight 16

// Sky Room
static char *M_GetConditionString(condition_t cond);

// Multiplayer
static void M_ConnectMenu(INT32 choice);

static patch_t *addonsp[NUM_EXT+5];

#define numaddonsshown 4

// Replay hut
static UINT8 playback_enterheld = 0; // horrid hack to prevent holding the button from being extremely fucked

// Drawing functions
static void M_DrawMessageMenu(void);
static void M_DrawLevelSelectOnly(boolean leftfade, boolean rightfade);

// uhhhhhh hack?
static void M_ChangecontrolResponse(event_t *ev);

// Consvar onchange functions
static void Newgametype_OnChange(void);
static void Dummymenuplayer_OnChange(void);
static void Dummystaff_OnChange(void);

// menus now use short names rather than hardcoded indices to identify menuitems
menuitem_t *M_CheckMenuItem(menutype_t type, const char *name)
{
	INT16 i;
	menu_t *menu = &menudefs[type];

	for (i = 0; i < menu->numitems; i++)
		if (!strncmp(menu->menuitems[i].itemname, name, ITEMNAMELEN))
			return &menu->menuitems[i];
	return NULL;
}

menuitem_t *M_GetMenuItem(menutype_t type, const char *name)
{
	menuitem_t *item = M_CheckMenuItem(type, name);
	if (!item)
		I_Error("Menu %d has no item %s", type, name);
	return item;
}

static INT16 M_GetMenuIndex(menutype_t type, const char *name)
{
	return M_GetMenuItem(type, name) - menudefs[type].menuitems;
}

// an array of macros for getting/setting menuitem properties
#define M_IsItemOn(t, n)                (itemOn == M_GetMenuIndex(t, n))
#define M_SetItemOn(t, n)               (itemOn = M_GetMenuIndex(t, n))
#define M_SetItemStatus(t, n, v)        (M_GetMenuItem(t, n)->status = v)
#define M_GetItemStatus(t, n)           (M_GetMenuItem(t, n)->status)
#define M_SetItemRoutine(t, n, v)       (M_GetMenuItem(t, n)->itemaction.routine = v)
#define M_SetItemCvar(t, n, v)          (M_GetMenuItem(t, n)->itemaction.cvar = v)
#define M_SetItemKey(t, n, v)           (M_GetMenuItem(t, n)->alphaKey = v)
#define M_SetItemY M_SetItemKey
#define M_SetItemX M_SetItemKey // 2D menus wen
#define M_GetItemKey(t, n)              (M_GetMenuItem(t, n)->alphaKey)
#define M_GetItemY M_GetItemKey
#define M_AdjustItemY(t, n, v)          (M_GetMenuItem(t, n)->alphaKey += v)

// bruh...
static UINT32 M_ServersPerPage(void)
{
	INT32 spp = M_GetMenuIndex(MN_MP_CONNECT, "LASLIN") - M_GetMenuIndex(MN_MP_CONNECT, "FIRLIN") + 1;
	if (spp < 1 || spp >= MAXSERVERLIST)
		I_Error("Broken server connection menu");
	return (UINT32)spp;
}

// ==========================================================================
// CONSOLE VARIABLES AND THEIR POSSIBLE VALUES GO HERE.
// ==========================================================================

consvar_t cv_showfocuslost = CVAR_INIT ("showfocuslost", "Yes", CV_SAVE, CV_YesNo, NULL);

static CV_PossibleValue_t map_cons_t[] = {
	{-1,"MIN"},
	{NEXTMAP_SPECIAL, "MAX"}, // TODO: kill nextmap (can't do that i'm afraid!)
	{0, NULL}
};
consvar_t cv_nextmap = CVAR_INIT ("nextmap", "1", CV_HIDEN|CV_CALL|CV_NOINIT, map_cons_t, Nextmap_OnChange);

static INT16 lastnextmap = 1;

static CV_PossibleValue_t skins_cons_t[MAXSKINS+1] = {{1, DEFAULTSKIN}};
consvar_t cv_chooseskin = CVAR_INIT ("chooseskin", DEFAULTSKIN, CV_HIDEN|CV_CALL|CV_NOINIT, skins_cons_t, Nextmap_OnChange);

// This gametype list is integral for many different reasons.
// When you add gametypes here, don't forget to update them in dehacked.c and doomstat.h!
CV_PossibleValue_t gametype_cons_t[NUMGAMETYPES+1];

consvar_t cv_newgametype = CVAR_INIT ("newgametype", "Race", CV_HIDEN|CV_CALL, gametype_cons_t, Newgametype_OnChange);

static CV_PossibleValue_t serversort_cons_t[] = {
	{0,"Ping"},
	{1,"Modified State"},
	{2,"Most Players"},
	{3,"Least Players"},
	{4,"Max Player Slots"},
	{5,"Gametype"},
	{0,NULL}
};
consvar_t cv_serversort = CVAR_INIT ("serversort", "Ping", CV_CALL, serversort_cons_t, M_SortServerList);

// first time memory
consvar_t cv_tutorialprompt = CVAR_INIT ("tutorialprompt", "On", CV_SAVE, CV_OnOff, NULL);

// autorecord demos for time attack
static consvar_t cv_autorecord = CVAR_INIT ("autorecord", "Yes", 0, CV_YesNo, NULL);

CV_PossibleValue_t ghost_cons_t[] = {{0, "Hide"}, {1, "Show Character"}, {2, "Show All"}, {0, NULL}};
CV_PossibleValue_t ghost2_cons_t[] = {{0, "Hide"}, {1, "Show"}, {0, NULL}};

consvar_t cv_ghost_besttime  = CVAR_INIT ("ghost_besttime",  "Show All", CV_SAVE, ghost_cons_t, NULL);
consvar_t cv_ghost_bestlap   = CVAR_INIT ("ghost_bestlap",   "Show All", CV_SAVE, ghost_cons_t, NULL);
consvar_t cv_ghost_last      = CVAR_INIT ("ghost_last",      "Show All", CV_SAVE, ghost_cons_t, NULL);
consvar_t cv_ghost_guest     = CVAR_INIT ("ghost_guest",     "Show", CV_SAVE, ghost2_cons_t, NULL);
consvar_t cv_ghost_staff     = CVAR_INIT ("ghost_staff",     "Show", CV_SAVE, ghost2_cons_t, NULL);

//Console variables used solely in the menu system.
//todo: add a way to use non-console variables in the menu
//      or make these consvars legitimate like color or skin.
static void Splitplayers_OnChange(void);
CV_PossibleValue_t splitplayers_cons_t[] = {{1, "One"}, {2, "Two"}, {3, "Three"}, {4, "Four"}, {0, NULL}};
consvar_t cv_splitplayers = CVAR_INIT ("splitplayers", "One", CV_CALL, splitplayers_cons_t, Splitplayers_OnChange);

static CV_PossibleValue_t dummymenuplayer_cons_t[] = {{0, "NOPE"}, {1, "P1"}, {2, "P2"}, {3, "P3"}, {4, "P4"}, {0, NULL}};
static CV_PossibleValue_t dummyteam_cons_t[] = {{0, "Spectator"}, {1, "Red"}, {2, "Blue"}, {0, NULL}};
static CV_PossibleValue_t dummyspectate_cons_t[] = {{0, "Spectator"}, {1, "Playing"}, {0, NULL}};
static CV_PossibleValue_t dummyscramble_cons_t[] = {{0, "Random"}, {1, "Points"}, {0, NULL}};
static CV_PossibleValue_t ringlimit_cons_t[] = {{-20, "MIN"}, {20, "MAX"}, {0, NULL}};
static CV_PossibleValue_t liveslimit_cons_t[] = {{-1, "MIN"}, {9, "MAX"}, {0, NULL}};
static CV_PossibleValue_t dummystaff_cons_t[] = {{0, "MIN"}, {100, "MAX"}, {0, NULL}};

consvar_t cv_dummymenuplayer = CVAR_INIT ("dummymenuplayer", "P1", CV_HIDEN|CV_CALL, dummymenuplayer_cons_t, Dummymenuplayer_OnChange);
consvar_t cv_dummyteam = CVAR_INIT ("dummyteam", "Spectator", CV_HIDEN, dummyteam_cons_t, NULL);
consvar_t cv_dummyspectate = CVAR_INIT ("dummyspectate", "Spectator", CV_HIDEN, dummyspectate_cons_t, NULL);
consvar_t cv_dummyscramble = CVAR_INIT ("dummyscramble", "Random", CV_HIDEN, dummyscramble_cons_t, NULL);
consvar_t cv_dummyrings = CVAR_INIT ("dummyrings", "0", CV_HIDEN, ringlimit_cons_t, NULL);
consvar_t cv_dummylives = CVAR_INIT ("dummylives", "0", CV_HIDEN, liveslimit_cons_t, NULL);
static consvar_t cv_dummystaff = CVAR_INIT ("dummystaff", "0", CV_HIDEN|CV_CALL, dummystaff_cons_t, Dummystaff_OnChange);

consvar_t cv_dummyattackingrings = CVAR_INIT ("dummyattackingrings", "Off", CV_HIDEN|CV_CALL|CV_NOINIT, CV_OnOff, Nextmap_OnChange);
consvar_t cv_dummyattackingstacking = CVAR_INIT ("dummyattackingstacking", "Off", CV_HIDEN|CV_CALL|CV_NOINIT, CV_OnOff, Nextmap_OnChange);
consvar_t cv_dummyattackingchaining = CVAR_INIT ("dummyattackingchaining", "Off", CV_HIDEN|CV_CALL|CV_NOINIT, CV_OnOff, Nextmap_OnChange);
consvar_t cv_dummyattackingslipdash = CVAR_INIT ("dummyattackingslipdash", "Off", CV_HIDEN|CV_CALL|CV_NOINIT, CV_OnOff, Nextmap_OnChange);
consvar_t cv_dummyattackingpurpledrift = CVAR_INIT ("dummyattackingpurpledrift", "Off", CV_HIDEN|CV_CALL|CV_NOINIT, CV_OnOff, Nextmap_OnChange);

static CV_PossibleValue_t dummygpdifficulty_cons_t[] = {{0, "Easy"}, {1, "Normal"}, {2, "Hard"}, {3, "Master"}, {0, NULL}};
static CV_PossibleValue_t dummygpcup_cons_t[50] = {{1, "TEMP"}}; // A REALLY BIG NUMBER, SINCE THIS IS TEMP UNTIL NEW MENUS

consvar_t cv_dummygpdifficulty = CVAR_INIT ("dummygpdifficulty", "Normal", CV_HIDEN, dummygpdifficulty_cons_t, NULL);
consvar_t cv_dummygpencore = CVAR_INIT ("dummygpencore", "Off", CV_HIDEN, CV_OnOff, NULL);
consvar_t cv_dummygpcup = CVAR_INIT ("dummygpcup", "TEMP", CV_HIDEN, dummygpcup_cons_t, NULL);

static tic_t playback_last_menu_interaction_leveltime = 0;

// ==========================================================================
// ALL MENU DEFINITIONS GO HERE
// ==========================================================================

//
// M_GetGametypeColor
//
// Pretty and consistent ^u^
// See also G_GetGametypeColor.
//

static INT32 highlightflags, recommendedflags, warningflags;

inline static void M_GetGametypeColor(void)
{
	INT16 gt;

	warningflags = V_REDMAP;
	recommendedflags = V_GREENMAP;

	if (cons_menuhighlight.value)
	{
		highlightflags = cons_menuhighlight.value;
		if (highlightflags == V_REDMAP)
		{
			warningflags = V_ORANGEMAP;
			return;
		}
		if (highlightflags == V_GREENMAP)
		{
			recommendedflags = V_SKYMAP;
			return;
		}
		return;
	}

	warningflags = V_REDMAP;
	recommendedflags = V_GREENMAP;

	if (modeattacking // == ATTACKING_TIME
		|| gamestate == GS_TIMEATTACK)
	{
		highlightflags = V_ORANGEMAP;
		return;
	}

	if (currentMenu && currentMenu->drawroutine == M_DrawServerMenu)
		gt = cv_newgametype.value;
	else if (!Playing())
	{
		highlightflags = V_YELLOWMAP;
		return;
	}
	else
		gt = gametype;

	if (gt == GT_BATTLE || levellistmode == LLM_BOSS)
	{
		highlightflags = gametypecolor[gt];
		warningflags = V_ORANGEMAP;
		return;
	}
	if (gt == GT_RACE)
	{
		highlightflags = gametypecolor[gt];
		return;
	}

	if (gametypecolor[gt])
	{
		highlightflags = gametypecolor[gt];
		return;
	}

	highlightflags = V_YELLOWMAP; // FALLBACK
}

// excuse me but I'm extremely lazy:
INT32 HU_GetHighlightColor(void)
{
	M_GetGametypeColor();	// update flag colour reguardless of the menu being opened or not.
	return highlightflags;
}

fixed_t M_GetMapThumbnail(INT16 mapnum, patch_t **out)
{
	patch_t *patch = NULL;
	if (mapnum == -1)
		patch = randomlvl;
	else if (mapnum >= 0 && mapnum < nummapheaders && mapheaderinfo[mapnum])
		patch = mapheaderinfo[mapnum]->thumbnailPic;

	if (!patch)
		patch = blanklvl;

	*out = patch;

	// check width instead of height because haha big winton
	return patch->width >= 320 ? FRACUNIT : FRACUNIT*2;
}

// Options
#ifdef HWRENDER
void M_OpenGLOptionsMenu(INT32 choice)
{
	(void)choice;

	if (rendermode == render_opengl)
		M_EnterMenu(MN_OP_OPENGL, true);
	else
		M_StartMessage(M_GetText("You must be in OpenGL mode\nto access this menu.\n\n(Press a key)\n"), NULL, MM_NOTHING);
}
#endif

// ==========================================================================
// CVAR ONCHANGE EVENTS GO HERE
// ==========================================================================
// (there's only a couple anyway)

// Prototypes
static INT32 M_FindFirstMap(INT32 gtype);
static INT32 M_GetFirstLevelInList(void);

#define ADD(cv, str) \
if (cv.value) \
{ \
  len += 3; \
  new_str = Z_Realloc(new_str, len, PU_STATIC, NULL); \
  strcat(new_str, str); \
}

char *M_AppendGametypeAndModName(void)
{
	UINT8 len = 4;
	char *new_str;

	new_str = Z_Malloc(4, PU_STATIC, NULL);

	if ((!Playing() && (levellistmode == LLM_ITEMBREAKER)) || (modeattacking & ATTACKING_ITEMBREAK))
	{
		strcpy(new_str, "IB-");
	}
	else
	{
		strcpy(new_str, "RA-");
	}

	ADD(cv_dummyattackingrings, "RN-")
	ADD(cv_dummyattackingstacking, "ST-")
	ADD(cv_dummyattackingchaining, "CH-")
	ADD(cv_dummyattackingslipdash, "SD-")
	ADD(cv_dummyattackingpurpledrift, "PD-")

	new_str[len-1] = '\0';

	return new_str;
}

// Nextmap.  Used for Time Attack.
void Nextmap_OnChange(void)
{
	char *leveltitle;

	// welp, we're stuck with nextmap for the time being. so just make the damn thing work
	if (cv_nextmap.value != lastnextmap)
	{
		boolean increment = cv_nextmap.value > lastnextmap;
		INT16 oldvalue = cv_nextmap.value - 1;
		INT16 newvalue = oldvalue;
		INT32 gt = cv_newgametype.value;
		while (!M_CanShowLevelInList(newvalue, gt))
		{
			if (increment) // Going up!
			{
				if (++newvalue == nummapheaders)
					newvalue = -1;
			}
			else // Going down!
			{
				if (--newvalue == -2)
					newvalue = nummapheaders-1;
			}

			if (newvalue == oldvalue)
				break; // don't loop forever if there's none of a certain gametype
		}
		cv_nextmap.value = lastnextmap = newvalue + 1;
	}

	// Update the string in the consvar.
	Z_Free(cv_nextmap.zstring);
	leveltitle = cv_nextmap.value ? G_BuildMapTitle(cv_nextmap.value) : Z_StrDup("Random");
	cv_nextmap.string = cv_nextmap.zstring = leveltitle;

	if (menustack[0] == MN_SP_TIMEATTACK || menustack[0] == MN_SP_MODS || menustack[0] == MN_SP_PRES)
	{
		// see also p_setup.c's P_LoadRecordGhosts
		char *gamemode = M_AppendGametypeAndModName();
		char *gpath = xva("%s"PATHSEP"media"PATHSEP"replay"PATHSEP"%s"PATHSEP"%s", srb2home, timeattackfolder, G_BuildMapName(cv_nextmap.value));
		UINT8 active = 0;

		CV_StealthSetValue(&cv_dummystaff, 0);

		active = 0;
		M_SetItemStatus(MN_SP_TIMEATTACK, "GUEST", IT_DISABLED);
		M_SetItemStatus(MN_SP_TIMEATTACK, "REPLAY", IT_DISABLED);

		// Check if file exists, if not, disable REPLAY option
		M_SetItemStatus(MN_SP_REPLAY, "RPTIME", IT_DISABLED);
		M_SetItemStatus(MN_SP_REPLAY, "RPLAP", IT_DISABLED);
		M_SetItemStatus(MN_SP_REPLAY, "RPLAST", IT_DISABLED);
		M_SetItemStatus(MN_SP_REPLAY, "RPGUES", IT_DISABLED);
		M_SetItemStatus(MN_SP_REPLAY, "RPSTAF", IT_DISABLED);
		M_SetItemStatus(MN_SP_GUESTREPLAY, "STIME", IT_DISABLED);
		M_SetItemStatus(MN_SP_GUESTREPLAY, "SLAP", IT_DISABLED);
		M_SetItemStatus(MN_SP_GUESTREPLAY, "SLAST", IT_DISABLED);
		M_SetItemStatus(MN_SP_GUESTREPLAY, "DELETE", IT_DISABLED);

		M_SetItemStatus(MN_SP_GHOST, "GUEST", IT_DISABLED);
		M_SetItemStatus(MN_SP_GHOST, "STAFF", IT_DISABLED);

		if (FIL_FileExists(va("%s-%s-%s-time-best.lmp", gpath, cv_chooseskin.string, gamemode))) {
			M_SetItemStatus(MN_SP_REPLAY, "RPTIME", IT_WHITESTRING|IT_CALL);
			M_SetItemStatus(MN_SP_GUESTREPLAY, "STIME", IT_WHITESTRING|IT_CALL);
			active |= 3;
		}

		if (levellistmode != LLM_ITEMBREAKER) {
			if (FIL_FileExists(va("%s-%s-%s-lap-best.lmp", gpath, cv_chooseskin.string, gamemode))) {
				M_SetItemStatus(MN_SP_REPLAY, "RPLAP", IT_WHITESTRING|IT_CALL);
				M_SetItemStatus(MN_SP_GUESTREPLAY, "SLAP", IT_WHITESTRING|IT_CALL);
				active |= 3;
			}
		}

		if (FIL_FileExists(va("%s-%s-%s-last.lmp", gpath, cv_chooseskin.string, gamemode))) {
			M_SetItemStatus(MN_SP_REPLAY, "RPLAST", IT_WHITESTRING|IT_CALL);
			M_SetItemStatus(MN_SP_GUESTREPLAY, "SLAST", IT_WHITESTRING|IT_CALL);
			active |= 3;
		}

		if (FIL_FileExists(va("%s-%s-guest.lmp", gpath, gamemode)))
		{
			M_SetItemStatus(MN_SP_REPLAY, "RPGUES", IT_WHITESTRING|IT_CALL);
			M_SetItemStatus(MN_SP_GUESTREPLAY, "DELETE", IT_WHITESTRING|IT_CALL);
			M_SetItemStatus(MN_SP_GHOST, "GUEST", IT_STRING|IT_CVAR);
			active |= 3;
		}

		CV_SetValue(&cv_dummystaff, 1);
		if (cv_dummystaff.value)
		{
			M_SetItemStatus(MN_SP_REPLAY, "RPSTAF", IT_WHITESTRING|IT_KEYHANDLER);
			M_SetItemStatus(MN_SP_GHOST, "STAFF", IT_STRING|IT_CVAR);
			CV_StealthSetValue(&cv_dummystaff, 1);
			active |= 1;
		}

		if (active) {
			if (active & 1)
				M_SetItemStatus(MN_SP_TIMEATTACK, "REPLAY", IT_WHITESTRING|IT_SUBMENU);
			if (active & 2)
				M_SetItemStatus(MN_SP_TIMEATTACK, "GUEST", IT_WHITESTRING|IT_SUBMENU);
		}
		else if (M_IsItemOn(MN_SP_TIMEATTACK, "REPLAY")) // Reset lastOn so replay isn't still selected when not available.
		{
			currentMenu->lastOn = itemOn;
			M_SetItemOn(MN_SP_TIMEATTACK, "START");
		}

		free(gpath);
		Z_Free(gamemode);
	}
}

static void Dummymenuplayer_OnChange(void)
{
	if (cv_dummymenuplayer.value < 1)
		CV_StealthSetValue(&cv_dummymenuplayer, splitscreen+1);
	else if (cv_dummymenuplayer.value > splitscreen+1)
		CV_StealthSetValue(&cv_dummymenuplayer, 1);
}

char dummystaffname[22];

static void Dummystaff_OnChange(void)
{
	lumpnum_t l;

	dummystaffname[0] = '\0';

	// TODO: Use map header to determine lump name
	if ((l = W_CheckNumForLongName(va("%sS01",G_BuildMapName(cv_nextmap.value)))) == LUMPERROR)
	{
		CV_StealthSetValue(&cv_dummystaff, 0);
		return;
	}
	else
	{
		char *temp = dummystaffname;
		UINT8 numstaff = 1;
		while (numstaff < 99 && (l = W_CheckNumForLongName(va("%sS%02u",G_BuildMapName(cv_nextmap.value),numstaff+1))) != LUMPERROR)
			numstaff++;

		if (cv_dummystaff.value < 1)
			CV_StealthSetValue(&cv_dummystaff, numstaff);
		else if (cv_dummystaff.value > numstaff)
			CV_StealthSetValue(&cv_dummystaff, 1);

		if ((l = W_CheckNumForLongName(va("%sS%02u",G_BuildMapName(cv_nextmap.value), cv_dummystaff.value))) == LUMPERROR)
			return; // shouldn't happen but might as well check...

		G_UpdateStaffGhostName(l);

		while (*temp)
			temp++;

		sprintf(temp, " - %d", cv_dummystaff.value);
	}
}

// Newgametype.  Used for gametype changes.
static void Newgametype_OnChange(void)
{
	if (menustack[0] && cv_nextmap.value)
	{
		INT32 gt = cv_newgametype.value;

		if (!mapheaderinfo[cv_nextmap.value-1])
			P_AllocMapHeader((INT16)(cv_nextmap.value-1));

		if (gt >= 0 && gt < gametypecount && !(mapheaderinfo[cv_nextmap.value-1]->typeoflevel & gametypetol[gt]))
			CV_SetValue(&cv_nextmap, M_FindFirstMap(gt));
	}
}

void Screenshot_option_Onchange(void)
{
	M_SetItemStatus(MN_OP_SCREENSHOTS, "FOLDER", cv_screenshot_option.value == 3 ? IT_CVAR|IT_STRING|IT_CV_STRING : IT_DISABLED);
}

void Moviemode_mode_Onchange(void)
{
	M_SetItemStatus(MN_OP_SCREENSHOTS, "GIFOPT", IT_DISABLED);
	M_SetItemStatus(MN_OP_SCREENSHOTS, "GIFDWN", IT_DISABLED);
	M_SetItemStatus(MN_OP_SCREENSHOTS, "APNMEM", IT_DISABLED);
	M_SetItemStatus(MN_OP_SCREENSHOTS, "APNCMP", IT_DISABLED);
	M_SetItemStatus(MN_OP_SCREENSHOTS, "APNSTR", IT_DISABLED);
	M_SetItemStatus(MN_OP_SCREENSHOTS, "APNWIN", IT_DISABLED);

	switch (cv_moviemode.value)
	{
	case MM_GIF:
		M_SetItemStatus(MN_OP_SCREENSHOTS, "GIFOPT", IT_STRING|IT_CVAR);
		M_SetItemStatus(MN_OP_SCREENSHOTS, "GIFDWN", IT_STRING|IT_CVAR);
		break;
	case MM_APNG:
		M_SetItemStatus(MN_OP_SCREENSHOTS, "APNMEM", IT_STRING|IT_CVAR);
		M_SetItemStatus(MN_OP_SCREENSHOTS, "APNCMP", IT_STRING|IT_CVAR);
		M_SetItemStatus(MN_OP_SCREENSHOTS, "APNSTR", IT_STRING|IT_CVAR);
		M_SetItemStatus(MN_OP_SCREENSHOTS, "APNWIN", IT_STRING|IT_CVAR);
		break;
	default:
		break;
	}
}

void Addons_option_Onchange(void)
{
	M_SetItemStatus(MN_OP_ADDONS, "FOLDER", cv_addons_option.value == 3 ? IT_CVAR|IT_STRING|IT_CV_STRING : IT_DISABLED);
}

void Moviemode_option_Onchange(void)
{
	;
}

// ==========================================================================
// END ORGANIZATION STUFF.
// ==========================================================================

// =========================================================================
// MENU PRESENTATION PARAMETER HANDLING (BACKGROUNDS)
// =========================================================================

menupres_t menupres[NUMMENUTYPES];

void M_InitMenuPresTables(void)
{
	INT32 i;

	// Called in d_main before SOC can get to the tables
	// Set menupres defaults
	for (i = 0; i < NUMMENUTYPES; i++)
	{
		// so-called "undefined"
		menupres[i].fadestrength = -1;
		menupres[i].hidetitlepics = -1; // inherits global hidetitlepics
		menupres[i].ttmode = TTMODE_NONE;
		menupres[i].ttscale = UINT8_MAX;
		menupres[i].ttname[0] = 0;
		menupres[i].ttx = INT16_MAX;
		menupres[i].tty = INT16_MAX;
		menupres[i].ttloop = INT16_MAX;
		menupres[i].tttics = UINT16_MAX;
		menupres[i].enterwipe = -1;
		menupres[i].exitwipe = -1;
		menupres[i].bgcolor = -1;
		menupres[i].titlescrollxspeed = INT32_MAX;
		menupres[i].titlescrollyspeed = INT32_MAX;
		menupres[i].bghide = true;
		// default true
		menupres[i].enterbubble = true;
		menupres[i].exitbubble = true;

		if (i != MN_MAIN)
		{
			menupres[i].muslooping = true;
		}
		if (i == MN_SP_TIMEATTACK)
			strncpy(menupres[i].musname, "_recat", 7);
		else if (i == MN_SR_SOUNDTEST)
		{
			*menupres[i].musname = '\0';
			menupres[i].musstop = true;
		}
	}
}

// =========================================================================
// BASIC MENU HANDLING
// =========================================================================

static void M_ChangeCvar(INT32 choice)
{
	consvar_t *cv = currentMenu->menuitems[itemOn].itemaction.cvar;

	if (choice == -1)
	{
		if (cv == &cv_playercolor[0])
		{
			SINT8 skinno = R_SkinAvailable(cv_chooseskin.string);
			if (skinno != -1)
				CV_SetValue(cv,skins[skinno].prefcolor);
			return;
		}
		CV_Set(cv,cv->defaultvalue);
		return;
	}

	choice = (choice<<1) - 1;

	if (((currentMenu->menuitems[itemOn].status & IT_CVARTYPE) == IT_CV_SLIDER)
	    ||((currentMenu->menuitems[itemOn].status & IT_CVARTYPE) == IT_CV_INVISSLIDER)
	    ||((currentMenu->menuitems[itemOn].status & IT_CVARTYPE) == IT_CV_NOMOD))
	{
		CV_SetValue(cv,cv->value+choice);
	}
	else if (cv->flags & CV_FLOAT)
	{
		if (!(currentMenu->menuitems[itemOn].status & IT_CV_INTEGERSTEP))
		{
			char s[20];
			float n = FIXED_TO_FLOAT(cv->value)+(choice)*(1.0f/16.0f);
			sprintf(s,"%ld%s",(long)n,M_Ftrim(n));
			CV_Set(cv,s);
		}
		else
			CV_SetValue(cv,FIXED_TO_FLOAT(cv->value)+(choice));
	}
	else
		CV_AddValue(cv,choice);
}

static boolean M_ChangeStringCvar(INT32 choice)
{
	consvar_t *cv = currentMenu->menuitems[itemOn].itemaction.cvar;
	char buf[MAXSTRINGLENGTH];
	size_t len;

	if (shiftdown && choice >= 32 && choice <= 127)
		choice = shiftxform[choice];

	switch (choice)
	{
		case KEY_BACKSPACE:
			len = strlen(cv->string);
			if (len > 0)
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				M_Memcpy(buf, cv->string, len);
				buf[len-1] = 0;
				CV_Set(cv, buf);
			}
			return true;
		case KEY_DEL:
			if (cv->string[0])
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				CV_Set(cv, "");
			}
			return true;
		default:
			if (choice >= 32 && choice <= 127)
			{
				len = strlen(cv->string);
				if (len < MAXSTRINGLENGTH - 1)
				{
					S_StartSound(NULL,sfx_menu1); // Tails
					M_Memcpy(buf, cv->string, len);
					buf[len++] = (char)choice;
					buf[len] = 0;
					CV_Set(cv, buf);
				}
				return true;
			}
			break;
	}
	return false;
}

static void M_NextOpt(void)
{
	INT16 oldItemOn = itemOn; // prevent infinite loop

	do
	{
		if (itemOn + 1 > currentMenu->numitems - 1)
			itemOn = 0;
		else
			itemOn++;
	} while (oldItemOn != itemOn && (currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_SPACE);
}

static void M_PrevOpt(void)
{
	INT16 oldItemOn = itemOn; // prevent infinite loop

	do
	{
		if (!itemOn)
			itemOn = currentMenu->numitems - 1;
		else
			itemOn--;
	} while (oldItemOn != itemOn && (currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_SPACE);
}

// lock out further input in a tic when important buttons are pressed
// (in other words -- stop bullshit happening by mashing buttons in fades)
static boolean noFurtherInput = false;

static void Command_Manual_f(void)
{
	if (modeattacking)
		return;
	M_StartControlPanel();
	M_Manual(INT32_MAX);
	itemOn = 0;
}

//
// M_Responder
//
boolean M_Responder(event_t *ev)
{
	INT32 ch = -1;
//	INT32 i;
	static tic_t joywait = 0, mousewait = 0;
	static INT32 pmousex = 0, pmousey = 0;
	static INT32 lastx = 0, lasty = 0;
	void (*routine)(INT32 choice); // for some casting problem
	INT32 deviceplayer = G_GetDevicePlayer(ev->device);

	if (dedicated || (demo.playback && demo.title)
	|| gamestate == GS_INTRO || gamestate == GS_CUTSCENE || gamestate == GS_GAMEEND
	|| gamestate == GS_CREDITS || gamestate == GS_EVALUATION
	|| gamestate == GS_BLANCREDITS
	)
		return false;
		
	if (CON_Ready() && gamestate != GS_WAITINGPLAYERS)
		return false;


	if (noFurtherInput)
	{
		// Ignore input after enter/escape/other buttons
		// (but still allow shift keyup so caps doesn't get stuck)
		return false;
	}
	else if (ev->type == ev_keydown)
	{
		ch = ev->data1;

		// added 5-2-98 remap virtual keys (mouse & joystick buttons)
		switch (ch)
		{
			case KEY_MOUSE1:
				ch = KEY_ENTER;
				break;
			case KEY_MOUSE1 + 1:
				ch = KEY_BACKSPACE;
				break;
			case KEY_HAT1:
				if (deviceplayer == 0)
					ch = KEY_UPARROW;
				break;
			case KEY_HAT1 + 1:
				if (deviceplayer == 0)
					ch = KEY_DOWNARROW;
				break;
			case KEY_HAT1 + 2:
				if (deviceplayer == 0)
					ch = KEY_LEFTARROW;
				break;
			case KEY_HAT1 + 3:
				if (deviceplayer == 0)
					ch = KEY_RIGHTARROW;
				break;
			default:
				break;
		}
	}
	else if (menustack[0])
	{
		if (ev->type == ev_joystick && deviceplayer == 0)
		{
			static INT32 lastjoy[JOYAXISES] = {0};

			if (G_AxisInDeadzone(deviceplayer, ev))
			{
				lastjoy[ev->data1] = 0;
				return false;
			}

			tic_t thistime = I_GetTime();

			// no previous direction OR change direction
			if (joywait < thistime && (lastjoy[ev->data1] == 0 || (ev->data2 < 0) != (lastjoy[ev->data1] < 0)))
			{
				ch = G_AxisToKey(ev);
				joywait = thistime + NEWTICRATE/7;
			}
			else
				ch = 0;

			lastjoy[ev->data1] = ev->data2;
		}
		// pass through other joysticks
		else if (ev->type == ev_joystick)
		{
			if (!G_AxisInDeadzone(deviceplayer, ev))
				ch = 0;
			else
				return false;
		}
		else if (ev->type == ev_mouse && mousewait < I_GetTime())
		{
			pmousey += ev->data3;
			if (pmousey < lasty-30)
			{
				ch = KEY_DOWNARROW;
				mousewait = I_GetTime() + NEWTICRATE/7;
				pmousey = lasty -= 30;
			}
			else if (pmousey > lasty + 30)
			{
				ch = KEY_UPARROW;
				mousewait = I_GetTime() + NEWTICRATE/7;
				pmousey = lasty += 30;
			}

			pmousex += ev->data2;
			if (pmousex < lastx - 30)
			{
				ch = KEY_LEFTARROW;
				mousewait = I_GetTime() + NEWTICRATE/7;
				pmousex = lastx -= 30;
			}
			else if (pmousex > lastx+30)
			{
				ch = KEY_RIGHTARROW;
				mousewait = I_GetTime() + NEWTICRATE/7;
				pmousex = lastx += 30;
			}
		}
	}

	// remap to keyboard keys if needed
	if (deviceplayer == 0 && ch >= NUMKEYS)
	{
		static INT32 joyremap[][2] = {
			{ gc_accelerate,  KEY_ENTER      }, // these two first
			{ gc_brake,       KEY_ESCAPE     }, // in case of conflicts
			{ gc_fire,        KEY_BACKSPACE  },
			{ gc_systemmenu,  KEY_ESCAPE     },
			{ gc_aimforward,  KEY_UPARROW    },
			{ gc_aimbackward, KEY_DOWNARROW  },
			{ gc_turnleft,    KEY_LEFTARROW  },
			{ gc_turnright,   KEY_RIGHTARROW },
		};

		for (size_t r = 0; r < sizeof(joyremap)/sizeof(*joyremap); r++)
		{
			INT32 gc = joyremap[r][0];
			if (gc == gc_brake && !menustack[0])
				continue; // don't open the menu with brake!

			if (G_ControlBoundToKey(0, gc, ch, true))
			{
				ch = joyremap[r][1];
				break;
			}
		}
	}

	if (ch == -1)
		return false;

	if (messagebox.active)
	{
		if (messagebox.messagetype != MM_EVENTHANDLER)
		{
			if (ch == ' ' || ch == 'n' || ch == 'y' || ch == KEY_ESCAPE || ch == KEY_ENTER)
			{
				if (messagebox.routine)
					messagebox.routine(ch);
				messagebox.active = false;
				noFurtherInput = true;
			}
		}
		else
		{
			// dirty hack: for customising controls, I want only buttons/keys/axes, not mouse
			if (messagebox.handler && !(ev->type == ev_mouse || (ev->type == ev_joystick && messagebox.handler != M_ChangecontrolResponse)))
				messagebox.handler(ev);
		}
		return true;
	}

	// F-Keys
	if (!menustack[0])
	{
		noFurtherInput = true;

		switch (ch)
		{
			case KEY_F1: // Help key
				Command_Manual_f();
				return true;

			case KEY_F2: // Empty
				return true;

			case KEY_F3: // Toggle HUD
				CV_SetValue(&cv_showhud, !cv_showhud.value);
				return true;

			case KEY_F4: // Sound Volume
				if (modeattacking)
					return true;
				M_StartControlPanel();
				M_Options(0);
				M_EnterMenu(MN_OP_SOUND, true);
				itemOn = 0;
				return true;

#ifndef DC
			case KEY_F5: // Video Mode
				if (modeattacking)
					return true;
				M_StartControlPanel();
				M_Options(0);
				M_EnterMenu(MN_OP_VIDEO, true);
				M_VideoModeMenu(0);
				return true;
#endif

			case KEY_F6: // Empty
				return true;

			case KEY_F7: // Options
				if (modeattacking)
					return true;
				M_StartControlPanel();
				M_Options(0);
				return true;

			// Screenshots on F8 now handled elsewhere
			// Same with Moviemode on F9

			case KEY_F10: // Quit SRB2
				M_QuitSRB2(0);
				return true;

			case KEY_F11: // Fullscreen
				CV_AddValue(&cv_fullscreen, 1);
				return true;

			// Spymode on F12 handled in game logic

			case KEY_ESCAPE: // Pop up menu
				if (chat_on)
				{
					HU_clearChatChars();
					chat_on = false;
				}
				else
					M_StartControlPanel();
				return true;
		}
		noFurtherInput = false; // turns out we didn't care
		return false;
	}

	routine = currentMenu->menuitems[itemOn].itemaction.routine;

	// Handle menuitems which need a specific key handling
	if (routine && (currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_KEYHANDLER)
	{
		if (shiftdown && ch >= 32 && ch <= 127)
			ch = shiftxform[ch];
		routine(ch);
		return true;
	}

	// BP: one of the more big hack i have never made
	if (routine && (currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_CVAR)
	{
		if ((currentMenu->menuitems[itemOn].status & IT_CVARTYPE) == IT_CV_STRING)
		{

			if (shiftdown && ch >= 32 && ch <= 127)
				ch = shiftxform[ch];
			if (M_ChangeStringCvar(ch))
				return true;
			else
				routine = NULL;
		}
		else
			routine = M_ChangeCvar;
	}

	if (menustack[0] == MN_PLAYBACK && !con_destlines)
	{
		playback_last_menu_interaction_leveltime = leveltime;
		// Flip left/right with up/down for the playback menu, since it's a horizontal icon row.
		switch (ch)
		{
			case KEY_LEFTARROW: ch = KEY_UPARROW; break;
			case KEY_UPARROW: ch = KEY_RIGHTARROW; break;
			case KEY_RIGHTARROW: ch = KEY_DOWNARROW; break;
			case KEY_DOWNARROW: ch = KEY_LEFTARROW; break;

			// arbitrary keyboard shortcuts because fuck you

			case '\'':	// toggle freecam
				M_PlaybackToggleFreecam(0);
				break;

			case ']':	// ffw / advance frame (depends on if paused or not)
				if (paused)
					M_PlaybackAdvance(0);
				else
					M_PlaybackFastForward(0);
				break;

			case '[':	// rewind /backupframe, uses the same function
				M_PlaybackRewind(0);
				break;

			case '\\':	// pause
				M_PlaybackPause(0);
				break;

			// viewpoints, an annoyance (tm)
			case '-':	// viewpoint minus
				M_PlaybackSetViews(-1);	// yeah lol.
				break;

			case '=':	// viewpoint plus
				M_PlaybackSetViews(1);	// yeah lol.
				break;

			// switch viewpoints:
			case '1':	// viewpoint for p1 (also f12)
				// maximum laziness:
				if (!demo.freecam)
					G_AdjustView(1, 1, true);
				break;
			case '2':	// viewpoint for p2
				if (!demo.freecam)
					G_AdjustView(2, 1, true);
				break;
			case '3':	// viewpoint for p3
				if (!demo.freecam)
					G_AdjustView(3, 1, true);
				break;
			case '4':	// viewpoint for p4
				if (!demo.freecam)
					G_AdjustView(4, 1, true);
				break;

			default: break;
		}
	}

	// Keys usable within menu
	switch (ch)
	{
		case KEY_DOWNARROW:
			M_NextOpt();
			S_StartSound(NULL, sfx_menu1);
			return true;

		case KEY_UPARROW:
			M_PrevOpt();
			S_StartSound(NULL, sfx_menu1);
			return true;

		case KEY_LEFTARROW:
			if (routine && ((currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_ARROWS
				|| (currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_CVAR))
			{
				if (menustack[0] != MN_OP_SOUND || itemOn > 3)
					S_StartSound(NULL, sfx_menu1);
				routine(0);
			}
			return true;

		case KEY_RIGHTARROW:
			if (routine && ((currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_ARROWS
				|| (currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_CVAR))
			{
				if (menustack[0] != MN_OP_SOUND || itemOn > 3)
					S_StartSound(NULL, sfx_menu1);
				routine(1);
			}
			return true;

		case KEY_ENTER:
			noFurtherInput = true;
			currentMenu->lastOn = itemOn;

			if (menustack[0] == MN_PLAYBACK)
			{
				boolean held = (boolean)playback_enterheld;
				if (held)
					return true;
				playback_enterheld = 3;
			}

			if (routine)
			{
				S_StartSound(NULL, sfx_menu1);
				switch (currentMenu->menuitems[itemOn].status & IT_TYPE)
				{
					case IT_CVAR:
					case IT_ARROWS:
						routine(1); // right arrow
						break;
					case IT_CALL:
						routine(itemOn);
						break;
					case IT_SUBMENU:
						currentMenu->lastOn = itemOn;
						M_EnterMenu(currentMenu->menuitems[itemOn].itemaction.submenu, true);
						break;
				}
			}
			return true;

		case KEY_ESCAPE:
		//case KEY_JOY1 + 2:
			noFurtherInput = true;
			currentMenu->lastOn = itemOn;

			//If we entered the game search menu, but didn't enter a game,
			//make sure the game doesn't still think we're in a netgame.
			if (!Playing() && netgame && multiplayer)
			{
				netgame = false;
				multiplayer = false;
			}
			M_ExitMenu();

			return true;

		case KEY_BACKSPACE:
			if ((currentMenu->menuitems[itemOn].status) == IT_CONTROL)
			{
				// detach any keys associated with the game control
				G_ClearControlKeys(setupcontrols, currentMenu->menuitems[itemOn].alphaKey);
				S_StartSound(NULL, sfx_shldls);
				return true;
			}

			if (routine && ((currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_ARROWS
				|| (currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_CVAR))
			{
				consvar_t *cv = currentMenu->menuitems[itemOn].itemaction.cvar;

				if (cv == &cv_chooseskin
					|| cv == &cv_dummystaff
					|| cv == &cv_nextmap
					|| cv == &cv_newgametype)
					return true;

				if (menustack[0] != MN_OP_SOUND || itemOn > 3)
					S_StartSound(NULL, sfx_menu1);
				routine(-1);
				return true;
			}

			// Why _does_ backspace go back anyway?
			//currentMenu->lastOn = itemOn;
			//if (currentMenu->prevMenu)
			//	M_SetupNextMenu(currentMenu->prevMenu);
			return false;

		default:
			CON_Responder(ev);
			break;
	}

	return true;
}

// special responder for demos
boolean M_DemoResponder(event_t *ev)
{

	INT32 ch = -1;	// cur event data
	boolean eatinput = false;	// :omnom:

	//should be accounted for beforehand but just to be safe...
	if (!demo.playback || demo.title)
		return false;

	if (noFurtherInput)
	{
		// Ignore input after enter/escape/other buttons
		// (but still allow shift keyup so caps doesn't get stuck)
		return false;
	}
	else if (ev->type == ev_keydown && !con_destlines)	// not while the console is on please
	{
		ch = ev->data1;
		// since this is ONLY for demos, there isn't MUCH for us to do.
		// mirrored from m_responder

		switch (ch)
		{
			// arbitrary keyboard shortcuts because fuck you

			case '\'':	// toggle freecam
				M_PlaybackToggleFreecam(0);
				eatinput = true;
				break;

			case ']':	// ffw / advance frame (depends on if paused or not)
				if (paused)
					M_PlaybackAdvance(0);
				else
					M_PlaybackFastForward(0);
				eatinput = true;
				break;

			case '[':	// rewind /backupframe, uses the same function
				M_PlaybackRewind(0);
				break;

			case '\\':	// pause
				M_PlaybackPause(0);
				eatinput = true;
				break;

			// viewpoints, an annoyance (tm)
			case '-':	// viewpoint minus
				M_PlaybackSetViews(-1);	// yeah lol.
				eatinput = true;
				break;

			case '=':	// viewpoint plus
				M_PlaybackSetViews(1);	// yeah lol.
				eatinput = true;
				break;

			// switch viewpoints:
			case '1':	// viewpoint for p1 (also f12)
				// maximum laziness:
				if (!demo.freecam)
					G_AdjustView(1, 1, true);
				break;
			case '2':	// viewpoint for p2
				if (!demo.freecam)
					G_AdjustView(2, 1, true);
				break;
			case '3':	// viewpoint for p3
				if (!demo.freecam)
					G_AdjustView(3, 1, true);
				break;
			case '4':	// viewpoint for p4
				if (!demo.freecam)
					G_AdjustView(4, 1, true);
				break;

			default: break;
		}

	}
	return eatinput;
}


//
// M_Drawer
// Called after the view has been rendered,
// but before it has been blitted.
//
void M_Drawer(void)
{
	if (menustack[0] || messagebox.active)
	{
		// now that's more readable with a faded background (yeah like Quake...)
		if (gamestate == GS_TIMEATTACK)
			V_DrawPatchFill(W_CachePatchName("SRB2BACK", PU_CACHE));
		else if (!WipeInAction && menustack[0] != MN_PLAYBACK) // Replay playback has its own background
			V_DrawFadeScreen(0xFF00, 16);
	}

	if (messagebox.active)
	{
		M_DrawMessageMenu();
	}
	else if (currentMenu && currentMenu->drawroutine)
	{
		M_GetGametypeColor();
		currentMenu->drawroutine(); // call current menu Draw routine
	}

	// focus lost notification goes on top of everything, even the former everything
	if (window_notinfocus && cv_showfocuslost.value)
	{
		M_DrawTextBox((BASEVIDWIDTH/2) - (60), (BASEVIDHEIGHT/2) - (16), 13, 2);
		if (gamestate == GS_LEVEL && (P_AutoPause() || paused))
			V_DrawCenteredString(BASEVIDWIDTH/2, (BASEVIDHEIGHT/2) - (4), highlightflags, "Game Paused");
		else
			V_DrawCenteredString(BASEVIDWIDTH/2, (BASEVIDHEIGHT/2) - (4), highlightflags, "Focus Lost");
	}
}

//
// M_StartControlPanel
//
void M_StartControlPanel(void)
{
	// intro might call this repeatedly
	if (menustack[0])
	{
		CON_ToggleOff(); // move away console
		return;
	}

	if (demo.playback)
	{
		M_EnterMenu(MN_PLAYBACK, true);
		playback_last_menu_interaction_leveltime = leveltime;
	}
	else if (!Playing())
	{
		M_EnterMenu(MN_MAIN, true);
		M_SetItemOn(MN_MAIN, "SINGLE");
	}
	else if (modeattacking)
	{
		M_EnterMenu(MN_MAPAUSE, true);
		M_SetItemOn(MN_MAPAUSE, "CONTIN");
	}
	else if (!(netgame || multiplayer)) // Single Player
	{
		if (gamestate != GS_LEVEL /*|| ultimatemode*/) // intermission, so gray out stuff.
		{
			M_SetItemStatus(MN_SPAUSE, "PANDOR", M_SecretUnlocked(SECRET_PANDORA) ? IT_GRAYEDOUT : IT_DISABLED);
			M_SetItemStatus(MN_SPAUSE, "RETRY", IT_GRAYEDOUT);
		}
		else
		{
			M_SetItemStatus(MN_SPAUSE, "PANDOR", M_SecretUnlocked(SECRET_PANDORA) ? IT_STRING|IT_CALL : IT_DISABLED);
			M_SetItemStatus(MN_SPAUSE, "RETRY", IT_STRING|IT_CALL);
		}

		M_SetItemStatus(MN_SPAUSE, "EMBLEM", M_SecretUnlocked(SECRET_EMBLEMHINTS) ? IT_STRING|IT_CALL : IT_DISABLED);

		M_EnterMenu(MN_SPAUSE, true);
		M_SetItemOn(MN_SPAUSE, "CONTIN");
	}
	else // multiplayer
	{
		M_SetItemStatus(MN_MPAUSE, "MAPCHG", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "ADDONS", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "SCRMBL", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "SETUP1", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "SETUP2", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "SETUP3", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "SETUP4", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "SPECTA", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "ENTER", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "CANCJO", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "TEAMCH", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "SPECCH", IT_DISABLED);
		M_SetItemStatus(MN_MPAUSE, "PSETUP", IT_DISABLED);
		M_SetItemStatus(MN_CHANGETEAM, "PLAYER", IT_DISABLED);
		M_SetItemStatus(MN_CHANGESPECTATE, "PLAYER", IT_DISABLED);

		// Reset these in case splitscreen messes things up
		M_SetItemY(MN_MPAUSE, "ADDONS", 8);
		M_SetItemY(MN_MPAUSE, "SCRMBL", 8);
		M_SetItemY(MN_MPAUSE, "MAPCHG", 24);

		M_SetItemY(MN_MPAUSE, "TEAMCH", 48);
		M_SetItemY(MN_MPAUSE, "SPECCH", 48);
		M_SetItemY(MN_MPAUSE, "OPTION", 64);
		M_SetItemY(MN_MPAUSE, "TITLE", 80);
		M_SetItemY(MN_MPAUSE, "QUIT", 88);

		Dummymenuplayer_OnChange();

		if ((server || IsPlayerAdmin(consoleplayer)))
		{
			M_SetItemStatus(MN_MPAUSE, "MAPCHG", IT_STRING | IT_CALL);
			M_SetItemStatus(MN_MPAUSE, "ADDONS", IT_STRING | IT_CALL);
			if (G_GametypeHasTeams())
				M_SetItemStatus(MN_MPAUSE, "SCRMBL", IT_STRING | IT_SUBMENU);
		}

		if (splitscreen)
		{
			M_SetItemStatus(MN_MPAUSE, "SETUP1", IT_STRING | IT_CALL);
			M_SetItemStatus(MN_MPAUSE, "SETUP2", IT_STRING | IT_CALL);
			M_SetItemStatus(MN_CHANGETEAM, "PLAYER", IT_STRING|IT_CVAR);
			M_SetItemStatus(MN_CHANGESPECTATE, "PLAYER", IT_STRING|IT_CVAR);

			if (netgame)
			{
				if (G_GametypeHasTeams())
				{
					M_SetItemStatus(MN_MPAUSE, "TEAMCH", IT_STRING | IT_SUBMENU);
					M_AdjustItemY(MN_MPAUSE, "TEAMCH", (splitscreen+1) * 8);
					M_AdjustItemY(MN_MPAUSE, "OPTION", 8);
					M_AdjustItemY(MN_MPAUSE, "TITLE", 8);
					M_AdjustItemY(MN_MPAUSE, "QUIT", 8);
				}
				else if (G_GametypeHasSpectators())
				{
					M_SetItemStatus(MN_MPAUSE, "SPECCH", IT_STRING | IT_SUBMENU);
					M_AdjustItemY(MN_MPAUSE, "SPECCH", (splitscreen+1) * 8);
					M_AdjustItemY(MN_MPAUSE, "OPTION", 8);
					M_AdjustItemY(MN_MPAUSE, "TITLE", 8);
					M_AdjustItemY(MN_MPAUSE, "QUIT", 8);
				}
			}

			if (splitscreen > 1)
			{
				M_SetItemStatus(MN_MPAUSE, "SETUP3", IT_STRING | IT_CALL);

				M_AdjustItemY(MN_MPAUSE, "OPTION", 8);
				M_AdjustItemY(MN_MPAUSE, "TITLE", 8);
				M_AdjustItemY(MN_MPAUSE, "QUIT", 8);

				if (splitscreen > 2)
				{
					M_SetItemStatus(MN_MPAUSE, "SETUP4", IT_STRING | IT_CALL);
					M_AdjustItemY(MN_MPAUSE, "OPTION", 8);
					M_AdjustItemY(MN_MPAUSE, "TITLE", 8);
					M_AdjustItemY(MN_MPAUSE, "QUIT", 8);
				}
			}
		}
		else
		{
			M_SetItemStatus(MN_MPAUSE, "PSETUP", IT_STRING | IT_CALL);

			if (G_GametypeHasTeams())
				M_SetItemStatus(MN_MPAUSE, "TEAMCH", IT_STRING | IT_SUBMENU);
			else if (G_GametypeHasSpectators())
			{
				if (!players[consoleplayer].spectator)
					M_SetItemStatus(MN_MPAUSE, "SPECTA", IT_STRING | IT_CALL);
				else if (players[consoleplayer].pflags & PF_WANTSTOJOIN)
					M_SetItemStatus(MN_MPAUSE, "CANCJO", IT_STRING | IT_CALL);
				else
					M_SetItemStatus(MN_MPAUSE, "ENTER", IT_STRING | IT_CALL);
			}
			else // in this odd case, we still want something to be on the menu even if it's useless
				M_SetItemStatus(MN_MPAUSE, "SPECTA", IT_GRAYEDOUT);
		}

#ifdef HAVE_DISCORDRPC
		{
			M_AdjustItemY(MN_MPAUSE, "ADDONS", -8);
			M_AdjustItemY(MN_MPAUSE, "SCRMBL", -8);
			M_AdjustItemY(MN_MPAUSE, "MAPCHG", -8);
			M_RefreshPauseMenu();
		}
#endif

		M_EnterMenu(MN_MPAUSE, true);
		M_SetItemOn(MN_MPAUSE, "CONTIN");
	}

	CON_ToggleOff(); // move away console
}

//
// M_ClearMenus
//
void M_ClearMenus(boolean callexitmenufunc)
{
	if (currentMenu && currentMenu->quitroutine && callexitmenufunc)
		currentMenu->quitroutine(0);

#ifndef DC // Save the config file. I'm sick of crashing the game later and losing all my changes!
	if (menustack[0])
		COM_BufAddText(va("saveconfig \"%s\" -silent\n", configfile));
#endif //Alam: But not on the Dreamcast's VMUs

	memset(menustack, 0, sizeof(menustack));
	currentMenu = NULL;
	messagebox.active = false;
	hidetitlemap = false;

	// D_StartTitle does its own wipe, since GS_TIMEATTACK is now a complete gamestate.
	if (gamestate == GS_TIMEATTACK)
		D_StartTitle();
}

//
// M_SetupNextMenu
//
static void M_SetupNextMenu(menutype_t menunum, boolean callexit)
{
	INT16 i;
	menu_t *menudef = &menudefs[menunum];

	// If you're going from a menu to itself, why are you running the quitroutine? You're not quitting it! -SH
	if (callexit && currentMenu && currentMenu != menudef && currentMenu->quitroutine)
		currentMenu->quitroutine(0);

	currentMenu = menudef;
	itemOn = currentMenu->lastOn;

	// in case of...
	if (itemOn >= currentMenu->numitems)
		itemOn = currentMenu->numitems - 1;

	// the curent item can be disabled,
	// this code go up until an enabled item found
	if ((currentMenu->menuitems[itemOn].status & IT_TYPE) == IT_SPACE)
	{
		for (i = 0; i < currentMenu->numitems; i++)
		{
			if ((currentMenu->menuitems[i].status & IT_TYPE) != IT_SPACE)
			{
				itemOn = i;
				break;
			}
		}
	}

	hidetitlemap = false;
}

// pop the active menu from the stack
void M_ExitMenu(void)
{
	size_t i;

	for (i = 0; i < NUMMENULEVELS-1; i++)
		menustack[i] = menustack[i+1];
	menustack[NUMMENULEVELS-1] = MN_NONE;

	if (menustack[0])
		M_SetupNextMenu(menustack[0], true);
	else
		M_ClearMenus(true);
}

// push a new menu to the stack
void M_EnterMenu(menutype_t menunum, boolean callexit)
{
	size_t i = NUMMENULEVELS-1;
	if (menustack[i])
		CONS_Alert(CONS_WARNING, "Max menu depth (%d) exceeded!\n", NUMMENULEVELS);

	do menustack[i] = menustack[i-1]; while (--i); // one-line do-while loop... that's something :^)
	menustack[0] = menunum;

	M_SetupNextMenu(menunum, callexit);
}

// Guess I'll put this here, idk
boolean M_MouseNeeded(void)
{
	return false;
}

//
// M_Ticker
//
void M_Ticker(void)
{
	// reset input trigger
	noFurtherInput = false;

	if (dedicated)
		return;

	if (--skullAnimCounter <= 0)
		skullAnimCounter = 8;

	followertimer++;

	if (menustack[0] == MN_PLAYBACK)
	{
		if (playback_enterheld > 0)
			playback_enterheld--;
	}
	else
		playback_enterheld = 0;

	//added : 30-01-98 : test mode for five seconds
	if (vidm_testingmode > 0)
	{
		// restore the previous video mode
		if (--vidm_testingmode == 0)
			setmodeneeded = vidm_previousmode + 1;
	}

#if defined (MASTERSERVER) && defined (HAVE_THREADS)
	I_lock_mutex(&ms_ServerList_mutex);
	{
		if (ms_ServerList)
		{
			CL_QueryServerList(ms_ServerList);
			free(ms_ServerList);
			ms_ServerList = NULL;
		}
	}
	I_unlock_mutex(ms_ServerList_mutex);
#endif
}

//
// M_Init
//
void M_Init(void)
{
	CV_RegisterVar(&cv_nextmap);
	CV_RegisterVar(&cv_newgametype);
	CV_RegisterVar(&cv_chooseskin);
	CV_RegisterVar(&cv_autorecord);

	if (dedicated)
		return;

	COM_AddCommand("manual", Command_Manual_f);

	// Menu hacks
	CV_RegisterVar(&cv_dummymenuplayer);
	CV_RegisterVar(&cv_dummyteam);
	CV_RegisterVar(&cv_dummyspectate);
	CV_RegisterVar(&cv_dummyscramble);
	CV_RegisterVar(&cv_dummyrings);
	CV_RegisterVar(&cv_dummylives);
	CV_RegisterVar(&cv_dummystaff);

	CV_RegisterVar(&cv_dummyattackingrings);
	CV_RegisterVar(&cv_dummyattackingstacking);
	CV_RegisterVar(&cv_dummyattackingchaining);
	CV_RegisterVar(&cv_dummyattackingslipdash);
	CV_RegisterVar(&cv_dummyattackingpurpledrift);

	CV_RegisterVar(&cv_dummygpdifficulty);
	CV_RegisterVar(&cv_dummygpencore);
	CV_RegisterVar(&cv_dummygpcup);

	quitmsg[QUITMSG] = M_GetText("Eggman's tied explosives\nto your girlfriend, and\nwill activate them if\nyou press the 'Y' key!\nPress 'N' to save her!\n\n(Press 'Y' to quit)");
	quitmsg[QUITMSG1] = M_GetText("What would Tails say if\nhe saw you quitting the game?\n\n(Press 'Y' to quit)");
	quitmsg[QUITMSG2] = M_GetText("Hey!\nWhere do ya think you're goin'?\n\n(Press 'Y' to quit)");
	quitmsg[QUITMSG3] = M_GetText("Forget your studies!\nPlay some more!\n\n(Press 'Y' to quit)");
	quitmsg[QUITMSG4] = M_GetText("You're trying to say you\nlike Sonic R better than\nthis, aren't you?\n\n(Press 'Y' to quit)");
	quitmsg[QUITMSG5] = M_GetText("Don't leave yet -- there's a\nsuper emerald around that corner!\n\n(Press 'Y' to quit)");
	quitmsg[QUITMSG6] = M_GetText("You'd rather work than play?\n\n(Press 'Y' to quit)");
	quitmsg[QUITMSG7] = M_GetText("Go ahead and leave. See if I care...\n*sniffle*\n\n(Press 'Y' to quit)");

	quitmsg[QUIT2MSG] = M_GetText("If you leave now,\nEggman will take over the world!\n\n(Press 'Y' to quit)");
	quitmsg[QUIT2MSG1] = M_GetText("On your mark,\nget set,\nhit the 'N' key!\n\n(Press 'Y' to quit)");
	quitmsg[QUIT2MSG2] = M_GetText("Aw c'mon, just\na few more laps!\n\n(Press 'Y' to quit)");
	quitmsg[QUIT2MSG3] = M_GetText("Did you get all those Chaos Emeralds?\n\n(Press 'Y' to quit)");
	quitmsg[QUIT2MSG4] = M_GetText("If you leave, I'll use\nmy Jawz on you!\n\n(Press 'Y' to quit)");
	quitmsg[QUIT2MSG5] = M_GetText("Don't go!\nYou might find the hidden\nlevels!\n\n(Press 'Y' to quit)");
	quitmsg[QUIT2MSG6] = M_GetText("Hit the 'N' key, Sonic!\nThe 'N' key!\n\n(Press 'Y' to quit)");

	quitmsg[QUIT3MSG] = M_GetText("Are you really going to give up?\nWe certainly would never give you up.\n\n(Press 'Y' to quit)");
	quitmsg[QUIT3MSG1] = M_GetText("Come on, just ONE more netgame!\n\n(Press 'Y' to quit)");
	quitmsg[QUIT3MSG2] = M_GetText("Press 'N' to unlock\nthe Golden Kart!\n\n(Press 'Y' to quit)");
	quitmsg[QUIT3MSG3] = M_GetText("Couldn't handle\nthe banana meta?\n\n(Press 'Y' to quit)");
	quitmsg[QUIT3MSG4] = M_GetText("Every time you press 'Y', an\nSRB2Kart Developer cries...\n\n(Press 'Y' to quit)");
	quitmsg[QUIT3MSG5] = M_GetText("You'll be back to play soon, though...\n...right?\n\n(Press 'Y' to quit)");
	quitmsg[QUIT3MSG6] = M_GetText("Aww, is Eggman's Nightclub too\ndifficult for you?\n\n(Press 'Y' to quit)");

	CV_RegisterVar(&cv_serversort);
}

void M_InitCharacterTables(void)
{
	UINT8 i;

	// Setup description table
	for (i = 0; i < MAXSKINS; i++)
	{
		if (i == 0)
		{
			strcpy(description[i].notes, "\x82Sonic\x80 is the fastest of the three, but also the hardest to control. Beginners beware, but experts will find Sonic very powerful.\n\n\x82""Ability:\x80 Speed Thok\nDouble jump to zoom forward with a huge burst of speed.\n\n\x82Tip:\x80 Simply letting go of forward does not slow down in SRB2. To slow down, hold the opposite direction.");
			strcpy(description[i].picname, "");
			strcpy(description[i].skinname, "sonic");
		}
		else if (i == 1)
		{
			strcpy(description[i].notes, "\x82Tails\x80 is the most mobile of the three, but has the slowest speed. Because of his mobility, he's well-\nsuited to beginners.\n\n\x82""Ability:\x80 Fly\nDouble jump to start flying for a limited time. Repetitively hit the jump button to ascend.\n\n\x82Tip:\x80 To quickly descend while flying, hit the spin button.");
			strcpy(description[i].picname, "");
			strcpy(description[i].skinname, "tails");
		}
		else if (i == 2)
		{
			strcpy(description[i].notes, "\x82Knuckles\x80 is well-\nrounded and can destroy breakable walls simply by touching them, but he can't jump as high as the other two.\n\n\x82""Ability:\x80 Glide & Climb\nDouble jump to glide in the air as long as jump is held. Glide into a wall to climb it.\n\n\x82Tip:\x80 Press spin while climbing to jump off the wall; press jump instead to jump off\nand face away from\nthe wall.");
			strcpy(description[i].picname, "");
			strcpy(description[i].skinname, "knuckles");
		}
		else if (i == 3)
		{
			strcpy(description[i].notes, "\x82Sonic & Tails\x80 team up to take on Dr. Eggman!\nControl Sonic while Tails desperately struggles to keep up.\n\nPlayer 2 can control Tails directly by setting the controls in the options menu.\nTails's directional controls are relative to Player 1's camera.\n\nTails can pick up Sonic while flying and carry him around.");
			strcpy(description[i].picname, "CHRS&T");
			strcpy(description[i].skinname, "sonic&tails");
		}
		else
		{
			strcpy(description[i].notes, "???");
			strcpy(description[i].picname, "");
			strcpy(description[i].skinname, "");
		}
	}
}

// ==========================================================================
// SPECIAL MENU OPTION DRAW ROUTINES GO HERE
// ==========================================================================

// Converts a string into question marks.
// Used for the secrets menu, to hide yet-to-be-unlocked stuff.
static const char *M_CreateSecretMenuOption(const char *str)
{
	static char qbuf[32];
	int i;

	for (i = 0; i < 31; ++i)
	{
		if (!str[i])
		{
			qbuf[i] = '\0';
			return qbuf;
		}
		else if (str[i] != ' ')
			qbuf[i] = '?';
		else
			qbuf[i] = ' ';
	}

	qbuf[31] = '\0';
	return qbuf;
}

static void M_DrawThermo(INT32 x, INT32 y, consvar_t *cv)
{
	INT32 xx = x, i;
	lumpnum_t leftlump, rightlump, centerlump[2], cursorlump;
	patch_t *p;

	leftlump = W_GetNumForName("M_THERML");
	rightlump = W_GetNumForName("M_THERMR");
	centerlump[0] = W_GetNumForName("M_THERMM");
	centerlump[1] = W_GetNumForName("M_THERMM");
	cursorlump = W_GetNumForName("M_THERMO");

	V_DrawScaledPatch(xx, y, 0, p = W_CachePatchNum(leftlump,PU_CACHE));
	xx += SHORT(p->width) - SHORT(p->leftoffset);
	for (i = 0; i < 16; i++)
	{
		V_DrawScaledPatch(xx, y, 0, W_CachePatchNum(centerlump[i & 1], PU_CACHE));
		xx += 8;
	}
	V_DrawScaledPatch(xx, y, 0, W_CachePatchNum(rightlump, PU_CACHE));

	xx = (cv->value - cv->PossibleValue[0].value) * (15*8) /
		(cv->PossibleValue[1].value - cv->PossibleValue[0].value);

	V_DrawScaledPatch((x + 8) + xx, y, 0, W_CachePatchNum(cursorlump, PU_CACHE));
}

//  A smaller 'Thermo', with range given as percents (0-100)
static void M_DrawSlider(INT32 x, INT32 y, const consvar_t *cv, boolean ontop)
{
	INT32 i;
	INT32 range;
	patch_t *p;

	for (i = 0; cv->PossibleValue[i+1].strvalue; i++);

	x = BASEVIDWIDTH - x - SLIDER_WIDTH;

	if (ontop)
	{
		V_DrawCharacter(x - 16 - (skullAnimCounter/5), y,
			'\x1C' | highlightflags, false); // left arrow
		V_DrawCharacter(x+(SLIDER_RANGE*8) + 8 + (skullAnimCounter/5), y,
			'\x1D' | highlightflags, false); // right arrow
	}

	if ((range = atoi(cv->defaultvalue)) != cv->value)
	{
		range = ((range - cv->PossibleValue[0].value) * 100 /
		(cv->PossibleValue[1].value - cv->PossibleValue[0].value));

		if (range < 0)
			range = 0;
		if (range > 100)
			range = 100;

		// draw the default
		p = W_CachePatchName("M_SLIDEC", PU_CACHE);
		V_DrawScaledPatch(x - 4 + (((SLIDER_RANGE)*8 + 4)*range)/100, y, 0, p);
	}

	V_DrawScaledPatch(x - 8, y, 0, W_CachePatchName("M_SLIDEL", PU_CACHE));

	p =  W_CachePatchName("M_SLIDEM", PU_CACHE);
	for (i = 0; i < SLIDER_RANGE; i++)
		V_DrawScaledPatch (x+i*8, y, 0,p);

	p = W_CachePatchName("M_SLIDER", PU_CACHE);
	V_DrawScaledPatch(x+SLIDER_RANGE*8, y, 0, p);

	range = ((cv->value - cv->PossibleValue[0].value) * 100 /
	 (cv->PossibleValue[1].value - cv->PossibleValue[0].value));

	if (range < 0)
		range = 0;
	if (range > 100)
		range = 100;

	// draw the slider cursor
	p = W_CachePatchName("M_SLIDEC", PU_CACHE);
	V_DrawScaledPatch(x - 4 + (((SLIDER_RANGE)*8 + 4)*range)/100, y, 0, p);
}

//
//  Draw a textbox, like Quake does, because sometimes it's difficult
//  to read the text with all the stuff in the background...
//
void M_DrawTextBox(INT32 x, INT32 y, INT32 width, INT32 boxlines)
{
	// Solid color textbox.
	V_DrawFill(x+5, y+5, width*8+6, boxlines*8+6, 159);
	//V_DrawFill(x+8, y+8, width*8, boxlines*8, 31);
}

// horizontally centered text
static void M_CentreText(INT32 y, const char *string)
{
	INT32 x;
	//added : 02-02-98 : centre on 320, because V_DrawString centers on vid.width...
	x = (BASEVIDWIDTH - V_StringWidth(string, V_OLDSPACING))>>1;
	V_DrawString(x,y,V_OLDSPACING,string);
}

//
// M_DrawMapEmblems
//
// used by pause & statistics to draw a row of emblems for a map
//
static void M_DrawMapEmblems(INT32 mapnum, INT32 x, INT32 y)
{
	UINT8 lasttype = UINT8_MAX, curtype;
	emblem_t *emblem = M_GetLevelEmblems(mapnum);

	while (emblem)
	{
		switch (emblem->type)
		{
			case ET_TIME: //case ET_SCORE: case ET_RINGS:
				curtype = 1; break;
			/*case ET_NGRADE: case ET_NTIME:
				curtype = 2; break;*/
			default:
				curtype = 0; break;
		}

		// Shift over if emblem is of a different discipline
		if (lasttype != UINT8_MAX && lasttype != curtype)
			x -= 4;
		lasttype = curtype;

		if (emblem->collected)
			V_DrawSmallMappedPatch(x, y, 0, W_CachePatchName(M_GetEmblemPatch(emblem, false), PU_CACHE),
			                       R_GetTranslationColormap(TC_DEFAULT, M_GetEmblemColor(emblem), GTC_MENUCACHE));
		else
			V_DrawSmallScaledPatch(x, y, 0, W_CachePatchName("NEEDIT", PU_CACHE));

		emblem = M_GetLevelEmblems(-1);
		x -= 8;
	}
}

static void M_DrawMenuTitle(void)
{
	if (currentMenu->headerpic)
	{
		patch_t *p = W_CachePatchName(currentMenu->headerpic, PU_CACHE);

		if (p->height > 24) // title is larger than normal
		{
			INT32 xtitle = (BASEVIDWIDTH - (SHORT(p->width)/2))/2;
			INT32 ytitle = (30 - (SHORT(p->height)/2))/2;

			if (xtitle < 0)
				xtitle = 0;
			if (ytitle < 0)
				ytitle = 0;

			V_DrawSmallScaledPatch(xtitle, ytitle, 0, p);
		}
		else
		{
			INT32 xtitle = (BASEVIDWIDTH - SHORT(p->width))/2;
			INT32 ytitle = (30 - SHORT(p->height))/2;

			if (xtitle < 0)
				xtitle = 0;
			if (ytitle < 0)
				ytitle = 0;

			V_DrawScaledPatch(xtitle, ytitle, 0, p);
		}
	}
}

void M_DrawGenericMenu(void)
{
	INT32 x, y, w, i, cursory = 0;

	// DRAW MENU
	x = currentMenu->x;
	y = currentMenu->y;

	// draw title (or big pic)
	M_DrawMenuTitle();

	for (i = 0; i < currentMenu->numitems; i++)
	{
		if (i == itemOn)
			cursory = y;
		switch (currentMenu->menuitems[i].status & IT_DISPLAY)
		{
			case IT_PATCH:
				if (currentMenu->menuitems[i].patch && currentMenu->menuitems[i].patch[0])
				{
					if (currentMenu->menuitems[i].status & IT_CENTER)
					{
						patch_t *p;
						p = W_CachePatchName(currentMenu->menuitems[i].patch, PU_CACHE);
						V_DrawScaledPatch((BASEVIDWIDTH - SHORT(p->width))/2, y, 0, p);
					}
					else
					{
						V_DrawScaledPatch(x, y, 0,
							W_CachePatchName(currentMenu->menuitems[i].patch, PU_CACHE));
					}
				}
				/* FALLTHRU */
			case IT_NOTHING:
			case IT_DYBIGSPACE:
				y = currentMenu->y+currentMenu->menuitems[i].alphaKey;//+= LINEHEIGHT;
				break;
			case IT_BIGSLIDER:
				M_DrawThermo(x, y, currentMenu->menuitems[i].itemaction.cvar);
				y += LINEHEIGHT;
				break;
			case IT_STRING:
			case IT_WHITESTRING:
				if (currentMenu->menuitems[i].alphaKey)
					y = currentMenu->y+currentMenu->menuitems[i].alphaKey;
				if (i == itemOn)
					cursory = y;

				if ((currentMenu->menuitems[i].status & IT_DISPLAY)==IT_STRING)
					V_DrawString(x, y, 0, currentMenu->menuitems[i].text);
				else
					V_DrawString(x, y, highlightflags, currentMenu->menuitems[i].text);

				// Cvar specific handling
				switch (currentMenu->menuitems[i].status & IT_TYPE)
					case IT_CVAR:
					{
						consvar_t *cv = currentMenu->menuitems[i].itemaction.cvar;
						switch (currentMenu->menuitems[i].status & IT_CVARTYPE)
						{
							case IT_CV_SLIDER:
								M_DrawSlider(x, y, cv, (i == itemOn));
							case IT_CV_NOPRINT: // color use this
							case IT_CV_INVISSLIDER: // monitor toggles use this
								break;
							case IT_CV_STRING:
								M_DrawTextBox(x, y + 4, MAXSTRINGLENGTH, 1);
								V_DrawString(x + 8, y + 12, V_ALLOWLOWERCASE, cv->string);
								if (skullAnimCounter < 4 && i == itemOn)
									V_DrawCharacter(x + 8 + V_StringWidth(cv->string, 0), y + 12,
										'_' | 0x80, false);
								y += 16;
								break;
							default:
								w = V_StringWidth(cv->string, 0);
								V_DrawString(BASEVIDWIDTH - x - w, y,
									((cv->flags & CV_CHEAT) && !CV_IsSetToDefault(cv) ? warningflags : highlightflags), cv->string);
								if (i == itemOn)
								{
									V_DrawCharacter(BASEVIDWIDTH - x - 10 - w - (skullAnimCounter/5), y,
											'\x1C' | highlightflags, false); // left arrow
									V_DrawCharacter(BASEVIDWIDTH - x + 2 + (skullAnimCounter/5), y,
											'\x1D' | highlightflags, false); // right arrow
								}
								break;
						}
						break;
					}
					y += STRINGHEIGHT;
					break;
			case IT_STRING2:
				V_DrawString(x, y, 0, currentMenu->menuitems[i].text);
				/* FALLTHRU */
			case IT_DYLITLSPACE:
				y += SMALLLINEHEIGHT;
				break;
			case IT_GRAYPATCH:
				if (currentMenu->menuitems[i].patch && currentMenu->menuitems[i].patch[0])
					V_DrawMappedPatch(x, y, 0,
						W_CachePatchName(currentMenu->menuitems[i].patch,PU_CACHE), graymap);
				y += LINEHEIGHT;
				break;
			case IT_TRANSTEXT:
				if (currentMenu->menuitems[i].alphaKey)
					y = currentMenu->y+currentMenu->menuitems[i].alphaKey;
				/* FALLTHRU */
			case IT_TRANSTEXT2:
				V_DrawString(x, y, V_TRANSLUCENT, currentMenu->menuitems[i].text);
				y += SMALLLINEHEIGHT;
				break;
			case IT_QUESTIONMARKS:
				if (currentMenu->menuitems[i].alphaKey)
					y = currentMenu->y+currentMenu->menuitems[i].alphaKey;

				V_DrawString(x, y, V_TRANSLUCENT|V_OLDSPACING, M_CreateSecretMenuOption(currentMenu->menuitems[i].text));
				y += SMALLLINEHEIGHT;
				break;
			case IT_HEADERTEXT: // draws 16 pixels to the left, in yellow text
				if (currentMenu->menuitems[i].alphaKey)
					y = currentMenu->y+currentMenu->menuitems[i].alphaKey;

				V_DrawString(x-16, y, highlightflags, currentMenu->menuitems[i].text);
				y += SMALLLINEHEIGHT;
				break;
		}
	}

	// DRAW THE SKULL CURSOR
	if (((currentMenu->menuitems[itemOn].status & IT_DISPLAY) == IT_PATCH)
		|| ((currentMenu->menuitems[itemOn].status & IT_DISPLAY) == IT_NOTHING))
	{
		V_DrawScaledPatch(currentMenu->x + SKULLXOFF, cursory - 5, 0,
			W_CachePatchName("M_CURSOR", PU_CACHE));
	}
	else
	{
		V_DrawScaledPatch(currentMenu->x - 24, cursory, 0,
			W_CachePatchName("M_CURSOR", PU_CACHE));
		V_DrawString(currentMenu->x, cursory, highlightflags, currentMenu->menuitems[itemOn].text);
	}
}

void M_DrawPauseMenu(void)
{
#ifdef HAVE_DISCORDRPC
	// kind of hackily baked in here
	if (menustack[0] == MN_MPAUSE && discordRequestList != NULL)
	{
		const tic_t freq = TICRATE/2;

		if ((leveltime % freq) >= freq/2)
		{
			V_DrawFixedPatch(204 * FRACUNIT,
				(currentMenu->y + M_GetItemY(MN_MPAUSE, "DISCRQ") - 1) * FRACUNIT,
				FRACUNIT,
				0,
				W_CachePatchName("K_REQUE2", PU_CACHE),
				NULL
			);
		}
	}
#endif

	M_DrawGenericMenu();
}

void M_DrawCenteredMenu(void)
{
	INT32 x, y, i, cursory = 0;

	// DRAW MENU
	x = currentMenu->x;
	y = currentMenu->y;

	// draw title (or big pic)
	M_DrawMenuTitle();

	for (i = 0; i < currentMenu->numitems; i++)
	{
		if (i == itemOn)
			cursory = y;
		switch (currentMenu->menuitems[i].status & IT_DISPLAY)
		{
			case IT_PATCH:
				if (currentMenu->menuitems[i].patch && currentMenu->menuitems[i].patch[0])
				{
					if (currentMenu->menuitems[i].status & IT_CENTER)
					{
						patch_t *p;
						p = W_CachePatchName(currentMenu->menuitems[i].patch, PU_CACHE);
						V_DrawScaledPatch((BASEVIDWIDTH - SHORT(p->width))/2, y, 0, p);
					}
					else
					{
						V_DrawScaledPatch(x, y, 0,
							W_CachePatchName(currentMenu->menuitems[i].patch, PU_CACHE));
					}
				}
				/* FALLTHRU */
			case IT_NOTHING:
			case IT_DYBIGSPACE:
				y += LINEHEIGHT;
				break;
			case IT_BIGSLIDER:
				M_DrawThermo(x, y, currentMenu->menuitems[i].itemaction.cvar);
				y += LINEHEIGHT;
				break;
			case IT_STRING:
			case IT_WHITESTRING:
				if (currentMenu->menuitems[i].alphaKey)
					y = currentMenu->y+currentMenu->menuitems[i].alphaKey;
				if (i == itemOn)
					cursory = y;

				if ((currentMenu->menuitems[i].status & IT_DISPLAY)==IT_STRING)
					V_DrawCenteredString(x, y, 0, currentMenu->menuitems[i].text);
				else
					V_DrawCenteredString(x, y, highlightflags, currentMenu->menuitems[i].text);

				// Cvar specific handling
				switch(currentMenu->menuitems[i].status & IT_TYPE)
					case IT_CVAR:
					{
						consvar_t *cv = currentMenu->menuitems[i].itemaction.cvar;
						switch(currentMenu->menuitems[i].status & IT_CVARTYPE)
						{
							case IT_CV_SLIDER:
								M_DrawSlider(x, y, cv, (i == itemOn));
							case IT_CV_NOPRINT: // color use this
								break;
							case IT_CV_STRING:
								M_DrawTextBox(x, y + 4, MAXSTRINGLENGTH, 1);
								V_DrawString(x + 8, y + 12, V_ALLOWLOWERCASE, cv->string);
								if (skullAnimCounter < 4 && i == itemOn)
									V_DrawCharacter(x + 8 + V_StringWidth(cv->string, 0), y + 12,
										'_' | 0x80, false);
								y += 16;
								break;
							default:
								V_DrawString(BASEVIDWIDTH - x - V_StringWidth(cv->string, 0), y,
									((cv->flags & CV_CHEAT) && !CV_IsSetToDefault(cv) ? warningflags : highlightflags), cv->string);
								break;
						}
						break;
					}
					y += STRINGHEIGHT;
					break;
			case IT_STRING2:
				V_DrawCenteredString(x, y, 0, currentMenu->menuitems[i].text);
				/* FALLTHRU */
			case IT_DYLITLSPACE:
				y += SMALLLINEHEIGHT;
				break;
			case IT_QUESTIONMARKS:
				if (currentMenu->menuitems[i].alphaKey)
					y = currentMenu->y+currentMenu->menuitems[i].alphaKey;

				V_DrawCenteredString(x, y, V_TRANSLUCENT|V_OLDSPACING, M_CreateSecretMenuOption(currentMenu->menuitems[i].text));
				y += SMALLLINEHEIGHT;
				break;
			case IT_GRAYPATCH:
				if (currentMenu->menuitems[i].patch && currentMenu->menuitems[i].patch[0])
					V_DrawMappedPatch(x, y, 0,
						W_CachePatchName(currentMenu->menuitems[i].patch,PU_CACHE), graymap);
				y += LINEHEIGHT;
				break;
			case IT_TRANSTEXT:
				if (currentMenu->menuitems[i].alphaKey)
					y = currentMenu->y+currentMenu->menuitems[i].alphaKey;
				/* FALLTHRU */
			case IT_TRANSTEXT2:
				V_DrawCenteredString(x, y, V_TRANSLUCENT, currentMenu->menuitems[i].text);
				y += SMALLLINEHEIGHT;
				break;
		}
	}

	// DRAW THE SKULL CURSOR
	if (((currentMenu->menuitems[itemOn].status & IT_DISPLAY) == IT_PATCH)
		|| ((currentMenu->menuitems[itemOn].status & IT_DISPLAY) == IT_NOTHING))
	{
		V_DrawScaledPatch(x + SKULLXOFF, cursory - 5, 0,
			W_CachePatchName("M_CURSOR", PU_CACHE));
	}
	else
	{
		V_DrawScaledPatch(x - V_StringWidth(currentMenu->menuitems[itemOn].text, 0)/2 - 24, cursory, 0,
			W_CachePatchName("M_CURSOR", PU_CACHE));
		V_DrawCenteredString(x, cursory, highlightflags, currentMenu->menuitems[itemOn].text);
	}

	if (menustack[0] == MN_MAIN)
	{
		INT32 texty = vid.height - 10*vid.dupy;
#define addtext(f, str) {\
V_DrawThinString(vid.dupx, texty, V_NOSCALESTART|f, str);\
texty -= 10*vid.dupy;\
}
		if (customversionstring[0] != '\0')
		{
			addtext(V_ALLOWLOWERCASE, customversionstring);
			addtext(0, "Mod version:");
		}
		else
		{
// Development -- show revision / branch info
#if defined(DEVELOP)
			addtext(V_ALLOWLOWERCASE|V_GREENMAP|V_TRANSLUCENT, comprevision);
			addtext(V_ALLOWLOWERCASE|V_YELLOWMAP|V_TRANSLUCENT, compbranch);
			V_DrawThinString(0, 0, V_ALLOWLOWERCASE|V_ORANGEMAP|V_TRANSLUCENT|V_SNAPTOTOP, va("%s", complast));
#else // Regular build
			addtext(V_ALLOWLOWERCASE|V_TRANSLUCENT, va("%s", VERSIONSTRING));
#endif
			if (compuncommitted)
				addtext(V_REDMAP|V_STRINGDANCE|V_TRANSLUCENT, "! UNCOMMITTED CHANGES !");
		}
#undef addtext
	}
}

//
// M_StringHeight
//
// Find string height from hu_font chars
//
static inline size_t M_StringHeight(const char *string)
{
	size_t h = 8, i;

	for (i = 0; i < strlen(string); i++)
		if (string[i] == '\n')
			h += 8;

	return h;
}

// ==========================================================================
// Extraneous menu patching functions
// ==========================================================================

//
// M_PatchSkinNameTable
//
// Like M_PatchLevelNameTable, but for cv_chooseskin
//
static void M_PatchSkinNameTable(void)
{
	INT32 j;

	memset(skins_cons_t, 0, sizeof (skins_cons_t));

	for (j = 0; j < MAXSKINS; j++)
	{
		if (skins[j].name[0] != '\0')
		{
			skins_cons_t[j].strvalue = skins[j].name;
			skins_cons_t[j].value = j+1;
		}
		else
		{
			skins_cons_t[j].strvalue = NULL;
			skins_cons_t[j].value = 0;
			break;
		}
	}

	j = R_SkinAvailable(cv_skin[0].string);
	if (j == -1)
		j = 0;

	CV_SetValue(&cv_chooseskin, j+1); // This causes crash sometimes?!

	return;
}

//
// M_PrepareCupList
//
static boolean M_PrepareCupList(void)
{
	cupheader_t *cup = kartcupheaders;
	INT32 i = 0;

	memset(dummygpcup_cons_t, 0, sizeof (dummygpcup_cons_t));

	if (cup == NULL)
		return false;

	while (cup != NULL)
	{
		dummygpcup_cons_t[i].strvalue = cup->name;
		dummygpcup_cons_t[i].value = i+1;
		// this will probably crash or do something stupid at over 50 cups,
		// but this is all behavior that gets completely overwritten in new-menus, so I'm not worried
		i++;
		cup = cup->next;
	}

	for (; i < 50; i++)
	{
		dummygpcup_cons_t[i].strvalue = NULL;
		dummygpcup_cons_t[i].value = 0;
	}

	CV_SetValue(&cv_dummygpcup, 1); // This causes crash sometimes?!

	return true;
}

static boolean nextmapinit = false;

// Call before showing any level-select menus
static void M_PrepareLevelSelect(void)
{
	if (!nextmapinit)
	{
		// nextmap needs CV_NOINIT because it's registered before IWADs
		// we have to init here, or else you'll see "1" on the level select...
		Nextmap_OnChange();
		nextmapinit = true;
	}
	if (levellistmode != LLM_CREATESERVER)
		CV_SetValue(&cv_nextmap, M_GetFirstLevelInList());
	else
		Newgametype_OnChange(); // Make sure to start on an appropriate map if wads have been added
}

//
// M_CanShowLevelInList
//
// Determines whether to show a given map in the various level-select lists.
// Set gt = -1 to ignore gametype.
//
boolean M_CanShowLevelInList(INT32 mapnum, INT32 gt)
{
	UINT32 tolflag = G_TOLFlag(gt);

	// Random map!
	if (mapnum == -1)
		return (levellistmode == LLM_CREATESERVER);

	// Does the map exist?
	if (mapnum < 0 || mapnum >= nummapheaders || !mapheaderinfo[mapnum])
		return false;

	// Does the map have a name?
	if (!mapheaderinfo[mapnum]->lvlttl[0])
		return false;

	// Does the map have a LUMP?
	if (mapheaderinfo[mapnum]->lumpnum == LUMPERROR)
		return false;

	switch (levellistmode)
	{
		case LLM_CREATESERVER:
			// Should the map be hidden?
			if (mapheaderinfo[mapnum]->menuflags & LF2_HIDEINMENU && mapnum+1 != gamemap)
				return false;

			if (M_MapLocked(mapnum+1))
				return false; // not unlocked

			// Check for TOL
			if (!(mapheaderinfo[mapnum]->typeoflevel & tolflag))
				return false;

			return true;

		/*case LLM_LEVELSELECT:
			if (mapheaderinfo[mapnum]->levelselect != maplistoption)
				return false;

			if (M_MapLocked(mapnum+1))
				return false; // not unlocked

			return true;*/
		case LLM_TIMEATTACK:
		case LLM_ITEMBREAKER:
			if (mapheaderinfo[mapnum]->menuflags & LF2_NOTIMEATTACK)
				return false;

			if ((levellistmode == LLM_TIMEATTACK && !(mapheaderinfo[mapnum]->typeoflevel & TOL_RACE))
			|| (levellistmode == LLM_ITEMBREAKER && !(mapheaderinfo[mapnum]->typeoflevel & TOL_BATTLE)))
				return false;

			if (M_MapLocked(mapnum+1))
				return false; // not unlocked

			if (M_SecretUnlocked(SECRET_HELLATTACK))
				return true; // now you're in hell

			if (mapheaderinfo[mapnum]->menuflags & LF2_HIDEINMENU)
				return false; // map hell

			if ((mapheaderinfo[mapnum]->menuflags & LF2_VISITNEEDED) && !mapheaderinfo[mapnum]->mapvisited)
				return false;

			return true;

		case LLM_BOSS:
			if (!(mapheaderinfo[mapnum]->typeoflevel & TOL_BOSS))
				return false;

			return true;

		default:
			return false;
	}

	// Hmm? Couldn't decide?
	return false;
}

static INT32 M_CountLevelsToShowInList(void)
{
	INT32 mapnum, count = 0;

	for (mapnum = 0; mapnum < nummapheaders; mapnum++)
		if (M_CanShowLevelInList(mapnum, -1))
			count++;

	return count;
}

static INT32 M_GetFirstLevelInList(void)
{
	INT32 mapnum;

	for (mapnum = 0; mapnum < nummapheaders; mapnum++)
		if (M_CanShowLevelInList(mapnum, -1))
			return mapnum + 1;

	return 1;
}

// ==================================================
// MESSAGE BOX (aka: a hacked, cobbled together menu)
// ==================================================
void M_StartMessage2(const char *string, void (*routine)(void),
	menumessagetype_t itemtype)
{
	size_t max = 0, start = 0, i, strlines;
	static char *message = NULL;
	Z_Free(message);
	message = Z_StrDup(string);
	DEBFILE(message);

	// Rudementary word wrapping.
	// Simple and effective. Does not handle nonuniform letter sizes, colors, etc. but who cares.
	strlines = 0;
	for (i = 0; message[i]; i++)
	{
		if (message[i] == ' ')
		{
			start = i;
			max += 4;
		}
		else if (message[i] == '\n')
		{
			strlines = i;
			start = 0;
			max = 0;
			continue;
		}
		else
			max += 8;

		// Start trying to wrap if presumed length exceeds the screen width.
		if (max >= BASEVIDWIDTH && start > 0)
		{
			message[start] = '\n';
			max -= (start-strlines)*8;
			strlines = start;
			start = 0;
		}
	}

	start = 0;
	max = 0;

	M_StartControlPanel(); // can't put menuactive to true

	messagebox.text = message;
	messagebox.messagetype = itemtype;
	if (itemtype == MM_EVENTHANDLER)
		messagebox.handler = (void (*)(event_t *))routine;
	else
		messagebox.routine = (void (*)(INT32))routine;

	//added : 06-02-98: now draw a textbox around the message
	// compute lenght max and the numbers of lines
	for (strlines = 0; *(message+start); strlines++)
	{
		for (i = 0;i < strlen(message+start);i++)
		{
			if (*(message+start+i) == '\n')
			{
				if (i > max)
					max = i;
				start += i;
				i = (size_t)-1; //added : 07-02-98 : damned!
				start++;
				break;
			}
		}

		if (i == strlen(message+start))
			start += i;
	}

	messagebox.x = (BASEVIDWIDTH  - 8*max-16)/2;
	messagebox.y = (BASEVIDHEIGHT - M_StringHeight(message))/2;

	messagebox.numlines = strlines;
	messagebox.maxlength = max;

	messagebox.active = true;
}

#define MAXMSGLINELEN 256

static void M_DrawMessageMenu(void)
{
	INT32 y = messagebox.y;
	size_t i, start = 0;
	char string[MAXMSGLINELEN];
	const char *msg = messagebox.text;

	M_DrawTextBox(messagebox.x, y - 8, messagebox.maxlength, messagebox.numlines);

	while (*(msg+start))
	{
		size_t len = strlen(msg+start);

		for (i = 0; i < len; i++)
		{
			if (*(msg+start+i) == '\n')
			{
				memset(string, 0, MAXMSGLINELEN);
				if (i >= MAXMSGLINELEN)
				{
					CONS_Printf("M_DrawMessageMenu: too long segment in %s\n", msg);
					return;
				}
				else
				{
					strncpy(string,msg+start, i);
					string[i] = '\0';
					start += i;
					i = (size_t)-1; //added : 07-02-98 : damned!
					start++;
				}
				break;
			}
		}

		if (i == strlen(msg+start))
		{
			if (i >= MAXMSGLINELEN)
			{
				CONS_Printf("M_DrawMessageMenu: too long segment in %s\n", msg);
				return;
			}
			else
			{
				strcpy(string, msg + start);
				start += i;
			}
		}

		V_DrawString((BASEVIDWIDTH - V_StringWidth(string, 0))/2,y,V_ALLOWLOWERCASE,string);
		y += 8; //SHORT(hu_font[0]->height);
	}
}

// =========
// IMAGEDEFS
// =========

// Draw an Image Def.  Aka, Help images.
// Defines what image is used in (menuitem_t)->text.
// You can even put multiple images in one menu!
void M_DrawImageDef(void)
{
	patch_t *patch = W_CachePatchName(currentMenu->menuitems[itemOn].text,PU_CACHE);
	if (patch->width <= BASEVIDWIDTH)
		V_DrawScaledPatch(0,0,0,patch);
	else
		V_DrawSmallScaledPatch(0,0,0,patch);

	if (currentMenu->menuitems[itemOn].alphaKey)
	{
		V_DrawString(2,BASEVIDHEIGHT-10, V_YELLOWMAP, va("%d", (itemOn<<1)-1)); // intentionally not highlightflags, unlike below
		V_DrawRightAlignedString(BASEVIDWIDTH-2,BASEVIDHEIGHT-10, V_YELLOWMAP, va("%d", itemOn<<1)); // ditto
	}
	else
	{
		INT32 x = BASEVIDWIDTH>>1, y = (BASEVIDHEIGHT>>1) - 4;
		x += (itemOn ? 1 : -1)*((BASEVIDWIDTH>>2) + 10);
		V_DrawCenteredString(x, y-10, highlightflags, "USE ARROW KEYS");
		V_DrawCharacter(x - 10 - (skullAnimCounter/5), y,
			'\x1C' | highlightflags, false); // left arrow
		V_DrawCharacter(x + 2 + (skullAnimCounter/5), y,
			'\x1D' | highlightflags, false); // right arrow
		V_DrawCenteredString(x, y+10, highlightflags, "TO LEAF THROUGH");
	}
}

// Handles the ImageDefs.  Just a specialized function that
// uses left and right movement.
void M_HandleImageDef(INT32 choice)
{
	boolean exitmenu = false;

	switch (choice)
	{
		case KEY_RIGHTARROW:
			if (itemOn >= (INT16)(currentMenu->numitems-1))
				break;
			S_StartSound(NULL, sfx_menu1);
			itemOn++;
			break;

		case KEY_LEFTARROW:
			if (!itemOn)
				break;

			S_StartSound(NULL, sfx_menu1);
			itemOn--;
			break;

		case KEY_ESCAPE:
		case KEY_ENTER:
			exitmenu = true;
			break;
	}

	if (exitmenu)
		M_ExitMenu();
}

// ======================
// MISC MAIN MENU OPTIONS
// ======================

void M_AddonsOptions(INT32 choice)
{
	(void)choice;
	Addons_option_Onchange();

	M_EnterMenu(MN_OP_ADDONS, true);
}

#define LOCATIONSTRING1 "Visit \x83SRB2.ORG/MODS\x80 to get & make addons!"
#define LOCATIONSTRING2 "Visit \x88SRB2.ORG/MODS\x80 to get & make addons!"

void M_Addons(INT32 choice)
{
	const char *pathname = ".";

	(void)choice;

#if 1
	if (cv_addons_option.value == 0)
		pathname = usehome ? srb2home : srb2path;
	else if (cv_addons_option.value == 1)
		pathname = srb2home;
	else if (cv_addons_option.value == 2)
		pathname = srb2path;
	else
#endif
	if (cv_addons_option.value == 3 && *cv_addons_folder.string != '\0')
		pathname = cv_addons_folder.string;

	strlcpy(menupath, pathname, 1024);
	menupathindex[(menudepthleft = menudepth-1)] = strlen(menupath) + 1;

	if (menupath[menupathindex[menudepthleft]-2] != PATHSEP[0])
	{
		menupath[menupathindex[menudepthleft]-1] = PATHSEP[0];
		menupath[menupathindex[menudepthleft]] = 0;
	}
	else
		--menupathindex[menudepthleft];

	if (!preparefilemenu(false, false))
	{
		M_StartMessage(va("No files/folders found.\n\n%s\n\n(Press a key)\n", (recommendedflags == V_SKYMAP ? LOCATIONSTRING2 : LOCATIONSTRING1)),NULL,MM_NOTHING);
		return;
	}
	else
		dir_on[menudepthleft] = 0;

	if (addonsp[0]) // never going to have some provided but not all, saves individually checking
	{
		size_t i;
		for (i = 0; i < NUM_EXT+5; i++)
			W_UnlockCachedPatch(addonsp[i]);
	}

	addonsp[EXT_FOLDER] = W_CachePatchName("M_FFLDR", PU_STATIC);
	addonsp[EXT_UP] = W_CachePatchName("M_FBACK", PU_STATIC);
	addonsp[EXT_NORESULTS] = W_CachePatchName("M_FNOPE", PU_STATIC);
	addonsp[EXT_TXT] = W_CachePatchName("M_FTXT", PU_STATIC);
	addonsp[EXT_CFG] = W_CachePatchName("M_FCFG", PU_STATIC);
	addonsp[EXT_WAD] = W_CachePatchName("M_FWAD", PU_STATIC);
#ifdef USE_KART
	addonsp[EXT_KART] = W_CachePatchName("M_FKART", PU_STATIC);
#endif
	addonsp[EXT_PK3] = W_CachePatchName("M_FPK3", PU_STATIC);
	addonsp[EXT_SOC] = W_CachePatchName("M_FSOC", PU_STATIC);
	addonsp[EXT_LUA] = W_CachePatchName("M_FLUA", PU_STATIC);
	addonsp[NUM_EXT] = W_CachePatchName("M_FUNKN", PU_STATIC);
	addonsp[NUM_EXT+1] = W_CachePatchName("M_FSEL", PU_STATIC);
	addonsp[NUM_EXT+2] = W_CachePatchName("M_FLOAD", PU_STATIC);
	addonsp[NUM_EXT+3] = W_CachePatchName("M_FSRCH", PU_STATIC);
	addonsp[NUM_EXT+4] = W_CachePatchName("M_FSAVE", PU_STATIC);

	M_EnterMenu(MN_AD_MAIN, true);
}

#define width 4
#define vpadding 27
#define h (BASEVIDHEIGHT-(2*vpadding))
#define NUMCOLOURS 8 // when toast's coding it's british english hacker fucker
static void M_DrawTemperature(INT32 x, fixed_t t)
{
	INT32 y;

	// bounds check
	if (t > FRACUNIT)
		t = FRACUNIT;
	/*else if (t < 0) -- not needed
		t = 0;*/

	// scale
	if (t > 1)
		t = (FixedMul(h<<FRACBITS, t)>>FRACBITS);

	// border
	V_DrawFill(x - 1, vpadding, 1, h, 0);
	V_DrawFill(x + width, vpadding, 1, h, 0);
	V_DrawFill(x - 1, vpadding-1, width+2, 1, 0);
	V_DrawFill(x - 1, vpadding+h, width+2, 1, 0);

	// bar itself
	y = h;
	if (t)
		for (t = h - t; y > 0; y--)
		{
			UINT8 colours[NUMCOLOURS] = {135, 133, 92, 77, 114, 178, 161, 162};
			UINT8 c;
			if (y <= t) break;
			if (y+vpadding >= BASEVIDHEIGHT/2)
				c = 185;
			else
				c = colours[(NUMCOLOURS*(y-1))/(h/2)];
			V_DrawFill(x, y-1 + vpadding, width, 1, c);
		}

	// fill the rest of the backing
	if (y)
		V_DrawFill(x, vpadding, width, y, 30);
}
#undef width
#undef vpadding
#undef h
#undef NUMCOLOURS

static char *M_AddonsHeaderPath(void)
{
	UINT32 len;
	static char header[1024];

	strlcpy(header, va("%s folder%s", cv_addons_option.string, menupath+menupathindex[menudepth-1]-1), 1024);
	len = strlen(header);
	if (len > 34)
	{
		len = len-34;
		header[len] = header[len+1] = header[len+2] = '.';
	}
	else
		len = 0;

	return header+len;
}

#define UNEXIST S_StartSound(NULL, sfx_s26d);\
		M_ExitMenu();\
		M_StartMessage(va("\x82%s\x80\nThis folder no longer exists!\nAborting to main menu.\n\n(Press a key)\n", M_AddonsHeaderPath()),NULL,MM_NOTHING)

#define CLEARNAME Z_Free(refreshdirname);\
					refreshdirname = NULL

static boolean prevmajormods = false;

static void M_AddonsClearName(INT32 choice)
{
	(void)choice;
	if (!majormods || prevmajormods)
	{
		CLEARNAME;
	}
	messagebox.active = false;
}

// returns whether to do message draw
static boolean M_AddonsRefresh(void)
{
	if ((refreshdirmenu & REFRESHDIR_NORMAL) && !preparefilemenu(true, false))
	{
		UNEXIST;
		if (refreshdirname)
		{
			CLEARNAME;
		}
		return true;
	}

#ifdef DEVELOP
	prevmajormods = majormods;
#else
	if (!majormods && prevmajormods)
		prevmajormods = false;
#endif

	if ((refreshdirmenu & REFRESHDIR_ADDFILE) || (majormods && !prevmajormods))
	{
		char *message = NULL;

		if (refreshdirmenu & REFRESHDIR_NOTLOADED)
		{
			S_StartSound(NULL, sfx_s26d);
			if (refreshdirmenu & REFRESHDIR_MAX)
				message = va("%c%s\x80\nMaximum number of addons reached.\nA file could not be loaded.\nIf you wish to play with this addon, restart the game to clear existing ones.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname);
			else
				message = va("%c%s\x80\nA file was not loaded.\nCheck the console log for more information.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname);
		}
		else if (refreshdirmenu & (REFRESHDIR_WARNING|REFRESHDIR_ERROR))
		{
			S_StartSound(NULL, sfx_s224);
			message = va("%c%s\x80\nA file was loaded with %s.\nCheck the console log for more information.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname, ((refreshdirmenu & REFRESHDIR_ERROR) ? "errors" : "warnings"));
		}
		else if (majormods && !prevmajormods)
		{
			S_StartSound(NULL, sfx_s221);
			message = va("%c%s\x80\nYou've loaded a gameplay-modifying addon.\n\nRecord Attack data will be saved to seperate save.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname);
			prevmajormods = majormods;
		}

		if (message)
		{
			M_StartMessage(message, M_AddonsClearName, MM_EVENTHANDLER);
			return true;
		}

		S_StartSound(NULL, sfx_s221);
		CLEARNAME;
	}

	return false;
}

void M_DrawAddons(void)
{
	INT32 x, y;
	ssize_t i, m;
	const UINT8 *flashcol = NULL;
	UINT8 hilicol;

	// hack - need to refresh at end of frame to handle addfile...
	if (refreshdirmenu & M_AddonsRefresh())
	{
		M_DrawMessageMenu();
		return;
	}

	if (Playing())
		V_DrawCenteredString(BASEVIDWIDTH/2, 5, warningflags, "Adding files mid-game may cause problems.");
	else
		V_DrawCenteredString(BASEVIDWIDTH/2, 5, 0, (recommendedflags == V_SKYMAP ? LOCATIONSTRING2 : LOCATIONSTRING1));

	if (numwadfiles <= NUMMAINWADS)
		y = 0;
	else if (numwadfiles >= MAX_WADFILES)
		y = FRACUNIT;
	else
	{
		y = FixedDiv(((ssize_t)(numwadfiles) - (ssize_t)(NUMMAINWADS))<<FRACBITS, ((ssize_t)MAX_WADFILES - (ssize_t)(NUMMAINWADS))<<FRACBITS);
		if (y > FRACUNIT) // happens because of how we're shrinkin' it a little
			y = FRACUNIT;
	}

	M_DrawTemperature(BASEVIDWIDTH - 19 - 5, y);

	// DRAW MENU
	x = currentMenu->x;
	y = currentMenu->y + 1;

	hilicol = V_GetStringColormap(highlightflags)[0];

	V_DrawString(x-21, (y - 16) + (lsheadingheight - 12), highlightflags|V_ALLOWLOWERCASE, M_AddonsHeaderPath());
	V_DrawFill(x-21, (y - 16) + (lsheadingheight - 3), MAXSTRINGLENGTH*8+6, 1, hilicol);
	V_DrawFill(x-21, (y - 16) + (lsheadingheight - 2), MAXSTRINGLENGTH*8+6, 1, 30);

	m = (BASEVIDHEIGHT - currentMenu->y + 2) - (y - 1);
	V_DrawFill(x - 21, y - 1, MAXSTRINGLENGTH*8+6, m, 159);

	// scrollbar!
	if (sizedirmenu <= (2*numaddonsshown + 1))
		i = 0;
	else
	{
		ssize_t q = m;
		m = ((2*numaddonsshown + 1) * m)/sizedirmenu;
		if (dir_on[menudepthleft] <= numaddonsshown) // all the way up
			i = 0;
		else if (sizedirmenu <= (dir_on[menudepthleft] + numaddonsshown + 1)) // all the way down
			i = q-m;
		else
			i = ((dir_on[menudepthleft] - numaddonsshown) * (q-m))/(sizedirmenu - (2*numaddonsshown + 1));
	}

	V_DrawFill(x + MAXSTRINGLENGTH*8+5 - 21, (y - 1) + i, 1, m, hilicol);

	// get bottom...
	m = dir_on[menudepthleft] + numaddonsshown + 1;
	if (m > (ssize_t)sizedirmenu)
		m = sizedirmenu;

	// then compute top and adjust bottom if needed!
	if (m < (2*numaddonsshown + 1))
	{
		m = min(sizedirmenu, 2*numaddonsshown + 1);
		i = 0;
	}
	else
		i = m - (2*numaddonsshown + 1);

	if (i != 0)
		V_DrawString(19, y+4 - (skullAnimCounter/5), highlightflags, "\x1A");

	if (skullAnimCounter < 4)
		flashcol = V_GetStringColormap(highlightflags);

	for (; i < m; i++)
	{
		UINT32 flags = V_ALLOWLOWERCASE;
		if (y > BASEVIDHEIGHT) break;
		if (dirmenu[i])
#define type (UINT8)(dirmenu[i][DIR_TYPE])
		{
			if (type & EXT_LOADED)
			{
				flags |= V_TRANSLUCENT;
				V_DrawSmallScaledPatch(x-(16+4), y, V_TRANSLUCENT, addonsp[(type & ~EXT_LOADED)]);
				V_DrawSmallScaledPatch(x-(16+4), y, 0, addonsp[NUM_EXT+2]);
			}
			else
				V_DrawSmallScaledPatch(x-(16+4), y, 0, addonsp[(type & ~EXT_LOADED)]);

			if ((size_t)i == dir_on[menudepthleft])
			{
				V_DrawFixedPatch((x-(16+4))<<FRACBITS, (y)<<FRACBITS, FRACUNIT/2, 0, addonsp[NUM_EXT+1], flashcol);
				flags = V_ALLOWLOWERCASE|highlightflags;
			}

#define charsonside 14
			if (dirmenu[i][DIR_LEN] > (charsonside*2 + 3))
				V_DrawString(x, y+4, flags, va("%.*s...%s", charsonside, dirmenu[i]+DIR_STRING, dirmenu[i]+DIR_STRING+dirmenu[i][DIR_LEN]-(charsonside+1)));
#undef charsonside
			else
				V_DrawString(x, y+4, flags, dirmenu[i]+DIR_STRING);
		}
#undef type
		y += 16;
	}

	if (m != (ssize_t)sizedirmenu)
		V_DrawString(19, y-12 + (skullAnimCounter/5), highlightflags, "\x1B");

	y = BASEVIDHEIGHT - currentMenu->y + 1;

	M_DrawTextBox(x - (21 + 5), y, MAXSTRINGLENGTH, 1);
	if (menusearch[0])
		V_DrawString(x - 18, y + 8, V_ALLOWLOWERCASE, menusearch+1);
	else
		V_DrawString(x - 18, y + 8, V_ALLOWLOWERCASE|V_TRANSLUCENT, "Type to search...");
	if (skullAnimCounter < 4)
		V_DrawCharacter(x - 18 + V_StringWidth(menusearch+1, 0), y + 8,
			'_' | 0x80, false);

	x -= (21 + 5 + 16);
	V_DrawSmallScaledPatch(x, y + 4, (menusearch[0] ? 0 : V_TRANSLUCENT), addonsp[NUM_EXT+3]);

	x = BASEVIDWIDTH - x - 16;
	V_DrawSmallScaledPatch(x, y + 4, ((!majormods) ? 0 : V_TRANSLUCENT), addonsp[NUM_EXT+4]);

	if (modifiedgame)
		V_DrawSmallScaledPatch(x, y + 4, 0, addonsp[NUM_EXT+2]);
}

static void M_AddonExec(INT32 ch)
{
	if (ch != 'y' && ch != KEY_ENTER)
		return;

	S_StartSound(NULL, sfx_zoom);
	COM_BufAddText(va("exec \"%s%s\"", menupath, dirmenu[dir_on[menudepthleft]]+DIR_STRING));
}

#define len menusearch[0]
static boolean M_ChangeStringAddons(INT32 choice)
{
	if (shiftdown && choice >= 32 && choice <= 127)
		choice = shiftxform[choice];

	switch (choice)
	{
		case KEY_DEL:
			if (len)
			{
				len = menusearch[1] = 0;
				return true;
			}
			break;
		case KEY_BACKSPACE:
			if (len)
			{
				menusearch[1+--len] = 0;
				return true;
			}
			break;
		default:
			if (choice >= 32 && choice <= 127)
			{
				if (len < MAXSTRINGLENGTH - 1)
				{
					menusearch[1+len++] = (char)choice;
					menusearch[1+len] = 0;
					return true;
				}
			}
			break;
	}
	return false;
}
#undef len

void M_HandleAddons(INT32 choice)
{
	boolean exitmenu = false; // exit to previous menu

	if (M_ChangeStringAddons(choice))
	{
		char *tempname = NULL;
		if (dirmenu && dirmenu[dir_on[menudepthleft]])
			tempname = Z_StrDup(dirmenu[dir_on[menudepthleft]]+DIR_STRING); // don't need to I_Error if can't make - not important, just QoL
#if 0 // much slower
		if (!preparefilemenu(true, false))
		{
			UNEXIST;
			return;
		}
#else // streamlined
		searchfilemenu(tempname);
#endif
	}

	switch (choice)
	{
		case KEY_DOWNARROW:
			if (dir_on[menudepthleft] < sizedirmenu-1)
				dir_on[menudepthleft]++;
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_UPARROW:
			if (dir_on[menudepthleft])
				dir_on[menudepthleft]--;
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_PGDN:
			{
				UINT8 i;
				for (i = numaddonsshown; i && (dir_on[menudepthleft] < sizedirmenu-1); i--)
					dir_on[menudepthleft]++;
			}
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_PGUP:
			{
				UINT8 i;
				for (i = numaddonsshown; i && (dir_on[menudepthleft]); i--)
					dir_on[menudepthleft]--;
			}
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_ENTER:
			{
				boolean refresh = true;
				if (!dirmenu[dir_on[menudepthleft]])
					S_StartSound(NULL, sfx_s26d);
				else
				{
					switch (dirmenu[dir_on[menudepthleft]][DIR_TYPE])
					{
						case EXT_FOLDER:
							strcpy(&menupath[menupathindex[menudepthleft]],dirmenu[dir_on[menudepthleft]]+DIR_STRING);
							if (menudepthleft)
							{
								menupathindex[--menudepthleft] = strlen(menupath);
								menupath[menupathindex[menudepthleft]] = 0;

								if (!preparefilemenu(false, false))
								{
									S_StartSound(NULL, sfx_s224);
									M_StartMessage(va("%c%s\x80\nThis folder is empty.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), M_AddonsHeaderPath()),NULL,MM_NOTHING);
									menupath[menupathindex[++menudepthleft]] = 0;

									if (!preparefilemenu(true, false))
									{
										UNEXIST;
										return;
									}
								}
								else
								{
									S_StartSound(NULL, sfx_menu1);
									dir_on[menudepthleft] = 1;
								}
								refresh = false;
							}
							else
							{
								S_StartSound(NULL, sfx_s26d);
								M_StartMessage(va("%c%s\x80\nThis folder is too deep to navigate to!\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), M_AddonsHeaderPath()),NULL,MM_NOTHING);
								menupath[menupathindex[menudepthleft]] = 0;
							}
							break;
						case EXT_UP:
							S_StartSound(NULL, sfx_menu1);
							menupath[menupathindex[++menudepthleft]] = 0;
							if (!preparefilemenu(false, false))
							{
								UNEXIST;
								return;
							}
							break;
						case EXT_TXT:
							M_StartMessage(va("%c%s\x80\nThis file may not be a console script.\nAttempt to run anyways? \n\n(Press 'Y' to confirm)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), dirmenu[dir_on[menudepthleft]]+DIR_STRING), M_AddonExec ,MM_YESNO);
							break;
						case EXT_CFG:
							M_AddonExec(KEY_ENTER);
							break;
						case EXT_LUA:
						case EXT_SOC:
						case EXT_WAD:
#ifdef USE_KART
						case EXT_KART:
#endif
						case EXT_PK3:
							COM_BufAddText(va("addfile \"%s%s\"", menupath, dirmenu[dir_on[menudepthleft]]+DIR_STRING));
							break;
						default:
							S_StartSound(NULL, sfx_s26d);
					}
				}
				if (refresh)
					refreshdirmenu |= REFRESHDIR_NORMAL;
			}
			break;

		case KEY_ESCAPE:
			exitmenu = true;
			break;

		default:
			break;
	}
	if (exitmenu)
	{
		closefilemenu(true);

		// Secret menu!
		//MainMenu[secrets].status = (M_AnySecretUnlocked()) ? (IT_STRING | IT_CALL) : (IT_DISABLED);

		M_ExitMenu();
	}
}

// ---- REPLAY HUT -----
menudemo_t *demolist;

#define DF_ENCORE       0x40
static INT16 replayScrollTitle = 0;
static SINT8 replayScrollDelay = TICRATE, replayScrollDir = 1;

static void PrepReplayList(void)
{
	size_t i;

	if (demolist)
		Z_Free(demolist);

	demolist = Z_Calloc(sizeof(menudemo_t) * sizedirmenu, PU_STATIC, NULL);

	for (i = 0; i < sizedirmenu; i++)
	{
		if (dirmenu[i][DIR_TYPE] == EXT_UP)
		{
			demolist[i].type = MD_SUBDIR;
			sprintf(demolist[i].title, "UP");
		}
		else if (dirmenu[i][DIR_TYPE] == EXT_FOLDER)
		{
			demolist[i].type = MD_SUBDIR;
			strncpy(demolist[i].title, dirmenu[i] + DIR_STRING, 64);
		}
		else
		{
			demolist[i].type = MD_NOTLOADED;
			snprintf(demolist[i].filepath, 1024, "%s%s", menupath, dirmenu[i] + DIR_STRING);
			sprintf(demolist[i].title, ".....");
		}
	}
}

void M_ReplayHut(INT32 choice)
{
	(void)choice;

	if (!demo.inreplayhut)
	{
		snprintf(menupath, 1024, "%s"PATHSEP"media"PATHSEP"replay"PATHSEP"online"PATHSEP, srb2home);
		menupathindex[(menudepthleft = menudepth-1)] = strlen(menupath);
	}
	if (!preparefilemenu(false, true))
	{
		M_StartMessage("No replays found.\n\n(Press a key)\n", NULL, MM_NOTHING);
		return;
	}
	else if (!demo.inreplayhut)
		dir_on[menudepthleft] = 0;
	demo.inreplayhut = true;

	replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;

	PrepReplayList();

	M_ClearMenus(true);
	M_EnterMenu(MN_MISC_REPLAYHUT, true);
	G_SetGamestate(GS_TIMEATTACK);
	titlemapinaction = TITLEMAP_OFF; // Nope don't give us HOMs please

	demo.rewinding = false;
	CL_ClearRewinds();

	S_ChangeMusicInternal("replst", true);
}

void M_HandleReplayHutList(INT32 choice)
{
	switch (choice)
	{
	case KEY_UPARROW:
		if (dir_on[menudepthleft])
			dir_on[menudepthleft]--;
		else
			return;
			//M_PrevOpt();

		S_StartSound(NULL, sfx_menu1);
		replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;
		break;

	case KEY_DOWNARROW:
		if (dir_on[menudepthleft] < sizedirmenu-1)
			dir_on[menudepthleft]++;
		else
			return;
			//itemOn = 0; // Not M_NextOpt because that would take us to the extra dummy item

		S_StartSound(NULL, sfx_menu1);
		replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;
		break;

	case KEY_ESCAPE:
		M_QuitReplayHut(0);
		break;

	case KEY_ENTER:
		switch (dirmenu[dir_on[menudepthleft]][DIR_TYPE])
		{
			case EXT_FOLDER:
				strcpy(&menupath[menupathindex[menudepthleft]],dirmenu[dir_on[menudepthleft]]+DIR_STRING);
				if (menudepthleft)
				{
					menupathindex[--menudepthleft] = strlen(menupath);
					menupath[menupathindex[menudepthleft]] = 0;

					if (!preparefilemenu(false, true))
					{
						S_StartSound(NULL, sfx_s224);
						M_StartMessage(va("%c%s\x80\nThis folder is empty.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), M_AddonsHeaderPath()),NULL,MM_NOTHING);
						menupath[menupathindex[++menudepthleft]] = 0;

						if (!preparefilemenu(true, true))
						{
							M_QuitReplayHut(0);
							return;
						}
					}
					else
					{
						S_StartSound(NULL, sfx_menu1);
						dir_on[menudepthleft] = 1;
						PrepReplayList();
					}
				}
				else
				{
					S_StartSound(NULL, sfx_s26d);
					M_StartMessage(va("%c%s\x80\nThis folder is too deep to navigate to!\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), M_AddonsHeaderPath()),NULL,MM_NOTHING);
					menupath[menupathindex[menudepthleft]] = 0;
				}
				break;
			case EXT_UP:
				S_StartSound(NULL, sfx_menu1);
				menupath[menupathindex[++menudepthleft]] = 0;
				if (!preparefilemenu(false, true))
				{
					M_QuitReplayHut(0);
					return;
				}
				PrepReplayList();
				break;
			default:
				currentMenu->lastOn = itemOn;
				M_EnterMenu(MN_MISC_REPLAYSTART, false); // ReplayDef's quit routine would boot us back to the title screen

				replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;

				switch (demolist[dir_on[menudepthleft]].addonstatus)
				{
				case DFILE_ERROR_CANNOTLOAD:
					// Only show "Watch Replay Without Addons"
					M_SetItemStatus(MN_MISC_REPLAYSTART, "LOADWA", IT_DISABLED);
					M_SetItemStatus(MN_MISC_REPLAYSTART, "NOLOAD", IT_CALL|IT_STRING);
					//M_SetItemY(MN_MISC_REPLAYSTART, "NOLOAD", 0);
					M_SetItemStatus(MN_MISC_REPLAYSTART, "WATCH", IT_DISABLED);
					M_SetItemOn(MN_MISC_REPLAYSTART, "NOLOAD");
					break;

				case DFILE_ERROR_NOTLOADED:
				case DFILE_ERROR_INCOMPLETEOUTOFORDER:
					// Show "Load Addons and Watch Replay" and "Watch Replay Without Addons"
					M_SetItemStatus(MN_MISC_REPLAYSTART, "LOADWA", IT_CALL|IT_STRING);
					M_SetItemStatus(MN_MISC_REPLAYSTART, "NOLOAD", IT_CALL|IT_STRING);
					//M_SetItemY(MN_MISC_REPLAYSTART, "NOLOAD", 10);
					M_SetItemStatus(MN_MISC_REPLAYSTART, "WATCH", IT_DISABLED);
					M_SetItemOn(MN_MISC_REPLAYSTART, "LOADWA");
					break;

				case DFILE_ERROR_EXTRAFILES:
				case DFILE_ERROR_OUTOFORDER:
				default:
					// Show "Watch Replay"
					M_SetItemStatus(MN_MISC_REPLAYSTART, "LOADWA", IT_DISABLED);
					M_SetItemStatus(MN_MISC_REPLAYSTART, "NOLOAD", IT_DISABLED);
					M_SetItemStatus(MN_MISC_REPLAYSTART, "WATCH", IT_CALL|IT_STRING);
					//M_SetItemY(MN_MISC_REPLAYSTART, "WATCH", 0);
					M_SetItemOn(MN_MISC_REPLAYSTART, "WATCH");
					break;
				}
		}

		break;
	}
}

#define SCALEDVIEWWIDTH (vid.width/vid.dupx)
#define SCALEDVIEWHEIGHT (vid.height/vid.dupy)
static void DrawReplayHutReplayInfo(void)
{
	patch_t *patch;
	UINT8 *colormap;
	INT32 x, y, w, h;

	switch (demolist[dir_on[menudepthleft]].type)
	{
	case MD_NOTLOADED:
		V_DrawCenteredString(160, 40, V_SNAPTOTOP, "Loading replay information...");
		break;

	case MD_INVALID:
		V_DrawCenteredString(160, 40, V_SNAPTOTOP|warningflags, "This replay cannot be played.");
		break;

	case MD_SUBDIR:
		break; // Can't think of anything to draw here right now

	case MD_OUTDATED:
		V_DrawThinString(17, 64, V_SNAPTOTOP|V_ALLOWLOWERCASE|V_TRANSLUCENT|highlightflags, "Recorded on an outdated version.");
		/* FALLTHRU */
	default:
		// Draw level stuff
		x = 15; y = 15;

		//CONS_Printf("%d %s\n", demolist[dir_on[menudepthleft]].map, G_BuildMapName(demolist[dir_on[menudepthleft]].map));
		fixed_t scale = M_GetMapThumbnail(demolist[dir_on[menudepthleft]].map, &patch)/4;
		if (patch == blanklvl && !mapheaderinfo[demolist[dir_on[menudepthleft]].map])
			patch = nolvl;

		if (!(demolist[dir_on[menudepthleft]].kartspeed & DF_ENCORE))
			V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, scale, V_SNAPTOTOP, patch, NULL);
		else
		{
			w = FixedMul(SHORT(patch->width), scale);
			h = FixedMul(SHORT(patch->height), scale);
			V_DrawFixedPatch((x+(w>>1))<<FRACBITS, y<<FRACBITS, scale, V_SNAPTOTOP|V_FLIP, patch, NULL);

			{
				static angle_t rubyfloattime = 0;
				const fixed_t rubyheight = FINESINE(rubyfloattime>>ANGLETOFINESHIFT);
				V_DrawFixedPatch((x+(w>>2))<<FRACBITS, ((y+(h>>2))<<FRACBITS) - (rubyheight<<1), FRACUNIT, V_SNAPTOTOP, W_CachePatchName("RUBYICON", PU_CACHE), NULL);
				rubyfloattime += FixedMul(ANGLE_MAX/NEWTICRATE, renderdeltatics);
			}
		}

		x += 85;

		if (demolist[dir_on[menudepthleft]].map != NEXTMAP_INVALID)
			V_DrawString(x, y, V_SNAPTOTOP, G_BuildMapTitle(demolist[dir_on[menudepthleft]].map+1));
		else
			V_DrawString(x, y, V_SNAPTOTOP|V_ALLOWLOWERCASE|V_TRANSLUCENT, "Level is not loaded.");

		if (demolist[dir_on[menudepthleft]].numlaps)
			V_DrawThinString(x, y+9, V_SNAPTOTOP|V_ALLOWLOWERCASE, va("(%d laps)", demolist[dir_on[menudepthleft]].numlaps));

		V_DrawString(x, y+20, V_SNAPTOTOP|V_ALLOWLOWERCASE, demolist[dir_on[menudepthleft]].gametype == GT_RACE ?
			va("Race (%s speed)", kartspeed_cons_t[(demolist[dir_on[menudepthleft]].kartspeed & ~DF_ENCORE) + 1].strvalue) :
			"Battle Mode");

		if (!demolist[dir_on[menudepthleft]].standings[0].ranking)
		{
			// No standings were loaded!
			V_DrawString(x, y+39, V_SNAPTOTOP|V_ALLOWLOWERCASE|V_TRANSLUCENT, "No standings available.");


			break;
		}

		V_DrawThinString(x, y+29, V_SNAPTOTOP|highlightflags, "WINNER");
		V_DrawString(x+38, y+30, V_SNAPTOTOP|V_ALLOWLOWERCASE, demolist[dir_on[menudepthleft]].standings[0].name);

		if (demolist[dir_on[menudepthleft]].gametype == GT_RACE)
		{
			V_DrawThinString(x, y+39, V_SNAPTOTOP|highlightflags, "TIME");
			V_DrawRightAlignedString(x+84, y+40, V_SNAPTOTOP, va("%d'%02d\"%02d",
											G_TicsToMinutes(demolist[dir_on[menudepthleft]].standings[0].timeorscore, true),
											G_TicsToSeconds(demolist[dir_on[menudepthleft]].standings[0].timeorscore),
											G_TicsToCentiseconds(demolist[dir_on[menudepthleft]].standings[0].timeorscore)
			));
		}
		else
		{
			V_DrawThinString(x, y+39, V_SNAPTOTOP|highlightflags, "SCORE");
			V_DrawString(x+32, y+40, V_SNAPTOTOP, va("%d", demolist[dir_on[menudepthleft]].standings[0].timeorscore));
		}

		// Character face!

		// Lat: 08/06/2020: For some reason missing skins have their value set to 255 (don't even ask me why I didn't write this)
		// and for an even STRANGER reason this passes the first check below, so we're going to make sure that the skin here ISN'T 255 before we do anything stupid.

		if (demolist[dir_on[menudepthleft]].standings[0].skin != 0xFF)
		{
			patch = faceprefix[demolist[dir_on[menudepthleft]].standings[0].skin][FACE_WANTED];
			colormap = R_GetTranslationColormap(
				demolist[dir_on[menudepthleft]].standings[0].skin,
				demolist[dir_on[menudepthleft]].standings[0].color,
				GTC_MENUCACHE);
		}
		else
		{
			patch = W_CachePatchName("M_NOWANT", PU_CACHE);
			colormap = R_GetTranslationColormap(
				TC_RAINBOW,
				demolist[dir_on[menudepthleft]].standings[0].color,
				GTC_MENUCACHE);
		}

		V_DrawMappedPatch(BASEVIDWIDTH-15 - SHORT(patch->width), y+20, V_SNAPTOTOP, patch, colormap);

		break;
	}
}

void M_DrawReplayHut(void)
{
	INT32 x, y, cursory = 0;
	INT16 i;
	INT16 replaylistitem = currentMenu->numitems-2;
	boolean processed_one_this_frame = false;

	static UINT16 replayhutmenuy = 0;

	if (cv_vhseffect.value)
		V_DrawVhsEffect(false);

	// Draw menu choices
	x = currentMenu->x;
	y = currentMenu->y;

	if (itemOn > replaylistitem)
	{
		itemOn = replaylistitem;
		dir_on[menudepthleft] = sizedirmenu-1;
		replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;
	}
	else if (itemOn < replaylistitem)
	{
		dir_on[menudepthleft] = 0;
		replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;
	}

	if (itemOn == replaylistitem)
	{
		INT32 maxy;
		// Scroll menu items if needed
		cursory = y + currentMenu->menuitems[replaylistitem].alphaKey + dir_on[menudepthleft]*10;
		maxy = y + currentMenu->menuitems[replaylistitem].alphaKey + sizedirmenu*10;

		if (cursory > maxy - 20)
			cursory = maxy - 20;

		if (cursory - replayhutmenuy > SCALEDVIEWHEIGHT-50)
			replayhutmenuy += (cursory-SCALEDVIEWHEIGHT-replayhutmenuy + 51)/2;
		else if (cursory - replayhutmenuy < 110)
			replayhutmenuy += (max(0, cursory-110)-replayhutmenuy - 1)/2;
	}
	else
		replayhutmenuy /= 2;

	y -= replayhutmenuy;

	// Draw static menu items
	for (i = 0; i < replaylistitem; i++)
	{
		INT32 localy = y + currentMenu->menuitems[i].alphaKey;

		if (localy < 65)
			continue;

		if (i == itemOn)
			cursory = localy;

		if ((currentMenu->menuitems[i].status & IT_DISPLAY)==IT_STRING)
			V_DrawString(x, localy, V_SNAPTOTOP|V_SNAPTOLEFT, currentMenu->menuitems[i].text);
		else
			V_DrawString(x, localy, V_SNAPTOTOP|V_SNAPTOLEFT|highlightflags, currentMenu->menuitems[i].text);
	}

	y += currentMenu->menuitems[replaylistitem].alphaKey;

	for (i = 0; i < (INT16)sizedirmenu; i++)
	{
		INT32 localy = y+i*10;
		INT32 localx = x;

		if (localy < 65)
			continue;
		if (localy >= SCALEDVIEWHEIGHT)
			break;

		if (demolist[i].type == MD_NOTLOADED && !processed_one_this_frame)
		{
			processed_one_this_frame = true;
			G_LoadDemoInfo(&demolist[i]);
		}

		if (demolist[i].type == MD_SUBDIR)
		{
			localx += 8;
			V_DrawScaledPatch(x - 4, localy, V_SNAPTOTOP|V_SNAPTOLEFT, W_CachePatchName(dirmenu[i][DIR_TYPE] == EXT_UP ? "M_RBACK" : "M_RFLDR", PU_CACHE));
		}

		if (itemOn == replaylistitem && i == (INT16)dir_on[menudepthleft])
		{
			cursory = localy;

			if (replayScrollDelay)
				replayScrollDelay--;
			else if (replayScrollDir > 0)
			{
				if (replayScrollTitle < (V_StringWidth(demolist[i].title, 0) - (SCALEDVIEWWIDTH - (x<<1)))<<1)
					replayScrollTitle++;
				else
				{
					replayScrollDelay = TICRATE;
					replayScrollDir = -1;
				}
			}
			else
			{
				if (replayScrollTitle > 0)
					replayScrollTitle--;
				else
				{
					replayScrollDelay = TICRATE;
					replayScrollDir = 1;
				}
			}

			V_DrawString(localx - (replayScrollTitle>>1), localy, V_SNAPTOTOP|V_SNAPTOLEFT|highlightflags|V_ALLOWLOWERCASE, demolist[i].title);
		}
		else
			V_DrawString(localx, localy, V_SNAPTOTOP|V_SNAPTOLEFT|V_ALLOWLOWERCASE, demolist[i].title);
	}

	// Draw scrollbar
	y = sizedirmenu*10 + currentMenu->menuitems[replaylistitem].alphaKey + 30;
	if (y > SCALEDVIEWHEIGHT-80)
	{
		V_DrawFill(BASEVIDWIDTH-4, 75, 4, SCALEDVIEWHEIGHT-80, V_SNAPTOTOP|V_SNAPTORIGHT|159);
		V_DrawFill(BASEVIDWIDTH-3, 76 + (SCALEDVIEWHEIGHT-80) * replayhutmenuy / y, 2, (((SCALEDVIEWHEIGHT-80) * (SCALEDVIEWHEIGHT-80))-1) / y - 1, V_SNAPTOTOP|V_SNAPTORIGHT|149);
	}

	// Draw the cursor
	V_DrawScaledPatch(currentMenu->x - 24, cursory, V_SNAPTOTOP|V_SNAPTOLEFT,
		W_CachePatchName("M_CURSOR", PU_CACHE));
	V_DrawString(currentMenu->x, cursory, V_SNAPTOTOP|V_SNAPTOLEFT|highlightflags, currentMenu->menuitems[itemOn].text);

	// Now draw some replay info!
	V_DrawFill(10, 10, 300, 60, V_SNAPTOTOP|159);

	if (itemOn == replaylistitem)
	{
		DrawReplayHutReplayInfo();
	}
}

void M_DrawReplayStartMenu(void)
{
	const char *warning;
	UINT8 i;

	M_DrawGenericMenu();

#define STARTY 62-(replayScrollTitle>>1)
	// Draw rankings beyond first
	for (i = 1; i < MAXPLAYERS && demolist[dir_on[menudepthleft]].standings[i].ranking; i++)
	{
		patch_t *patch;
		UINT8 *colormap;

		V_DrawRightAlignedString(BASEVIDWIDTH-100, STARTY + i*20, V_SNAPTOTOP|highlightflags, va("%2d", demolist[dir_on[menudepthleft]].standings[i].ranking));
		V_DrawThinString(BASEVIDWIDTH-96, STARTY + i*20, V_SNAPTOTOP|V_ALLOWLOWERCASE, demolist[dir_on[menudepthleft]].standings[i].name);

		if (demolist[dir_on[menudepthleft]].standings[i].timeorscore == UINT32_MAX-1)
			V_DrawThinString(BASEVIDWIDTH-92, STARTY + i*20 + 9, V_SNAPTOTOP, "NO CONTEST");
		else if (demolist[dir_on[menudepthleft]].gametype == GT_RACE)
			V_DrawRightAlignedString(BASEVIDWIDTH-40, STARTY + i*20 + 9, V_SNAPTOTOP, va("%d'%02d\"%02d",
											G_TicsToMinutes(demolist[dir_on[menudepthleft]].standings[i].timeorscore, true),
											G_TicsToSeconds(demolist[dir_on[menudepthleft]].standings[i].timeorscore),
											G_TicsToCentiseconds(demolist[dir_on[menudepthleft]].standings[i].timeorscore)
			));
		else
			V_DrawString(BASEVIDWIDTH-92, STARTY + i*20 + 9, V_SNAPTOTOP, va("%d", demolist[dir_on[menudepthleft]].standings[i].timeorscore));

		// Character face!

		// Lat: 08/06/2020: For some reason missing skins have their value set to 255 (don't even ask me why I didn't write this)
		// and for an even STRANGER reason this passes the first check below, so we're going to make sure that the skin here ISN'T 255 before we do anything stupid.

		if (demolist[dir_on[menudepthleft]].standings[i].skin != 0xFF)
		{
			patch = faceprefix[demolist[dir_on[menudepthleft]].standings[i].skin][FACE_RANK];
			colormap = R_GetTranslationColormap(
				demolist[dir_on[menudepthleft]].standings[i].skin,
				demolist[dir_on[menudepthleft]].standings[i].color,
				GTC_MENUCACHE);
		}
		else
		{
			patch = W_CachePatchName("M_NORANK", PU_CACHE);
			colormap = R_GetTranslationColormap(
				TC_RAINBOW,
				demolist[dir_on[menudepthleft]].standings[i].color,
				GTC_MENUCACHE);
		}

		V_DrawMappedPatch(BASEVIDWIDTH-5 - SHORT(patch->width), STARTY + i*20, V_SNAPTOTOP, patch, colormap);
	}
#undef STARTY

	// Handle scrolling rankings
	if (replayScrollDelay)
		replayScrollDelay--;
	else if (replayScrollDir > 0)
	{
		if (replayScrollTitle < (i*20 - SCALEDVIEWHEIGHT + 100)<<1)
			replayScrollTitle++;
		else
		{
			replayScrollDelay = TICRATE;
			replayScrollDir = -1;
		}
	}
	else
	{
		if (replayScrollTitle > 0)
			replayScrollTitle--;
		else
		{
			replayScrollDelay = TICRATE;
			replayScrollDir = 1;
		}
	}

	V_DrawFill(10, 10, 300, 60, V_SNAPTOTOP|159);
	DrawReplayHutReplayInfo();

	V_DrawString(10, 72, V_SNAPTOTOP|highlightflags|V_ALLOWLOWERCASE, demolist[dir_on[menudepthleft]].title);

	// Draw a warning prompt if needed
	switch (demolist[dir_on[menudepthleft]].addonstatus)
	{
	case DFILE_ERROR_CANNOTLOAD:
		warning = "Some addons in this replay cannot be loaded.\nYou can watch anyway, but desyncs may occur.";
		break;

	case DFILE_ERROR_NOTLOADED:
	case DFILE_ERROR_INCOMPLETEOUTOFORDER:
		warning = "Loading addons will mark your game as modified, and Record Attack may be unavailable.\nYou can watch without loading addons, but desyncs may occur.";
		break;

	case DFILE_ERROR_EXTRAFILES:
		warning = "You have addons loaded that were not present in this replay.\nYou can watch anyway, but desyncs may occur.";
		break;

	case DFILE_ERROR_OUTOFORDER:
		warning = "You have this replay's addons loaded, but they are out of order.\nYou can watch anyway, but desyncs may occur.";
		break;

	default:
		return;
	}

	V_DrawSmallString(4, BASEVIDHEIGHT-14, V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_ALLOWLOWERCASE, warning);
}

void M_QuitReplayHut(INT32 choice)
{
	(void)choice;

	M_ClearMenus(false);

	if (demolist)
		Z_Free(demolist);
	demolist = NULL;

	demo.inreplayhut = false;
}

void M_HutStartReplay(INT32 choice)
{
	(void)choice;

	M_ClearMenus(false);
	demo.loadfiles = M_IsItemOn(MN_MISC_REPLAYSTART, "LOADWA");
	demo.ignorefiles = !M_IsItemOn(MN_MISC_REPLAYSTART, "LOADWA");

	G_DoPlayDemo(demolist[dir_on[menudepthleft]].filepath);
}

void M_SetPlaybackMenuPointer(void)
{
	M_SetItemOn(MN_PLAYBACK, "PAUSE");
}

void M_DrawPlaybackMenu(void)
{
	INT16 i;
	patch_t *icon;
	UINT8 *activemap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_GOLD, GTC_MENUCACHE);
	UINT32 transmap = max(0, (INT32)(leveltime - playback_last_menu_interaction_leveltime - 4*TICRATE)) / 5;
	transmap = min(8, transmap) << V_ALPHASHIFT;

	if (leveltime - playback_last_menu_interaction_leveltime >= 6*TICRATE)
		playback_last_menu_interaction_leveltime = leveltime - 6*TICRATE;

	// Toggle items
	if (paused && !demo.rewinding)
	{
		M_SetItemStatus(MN_PLAYBACK, "PAUSE", IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "FASTFW", IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "REWIND", IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "RESUME", IT_CALL|IT_STRING);
		M_SetItemStatus(MN_PLAYBACK, "ADVFRA", IT_CALL|IT_STRING);
		M_SetItemStatus(MN_PLAYBACK, "REWFRA", IT_CALL|IT_STRING);

		if (M_IsItemOn(MN_PLAYBACK, "REWIND"))
			M_SetItemOn(MN_PLAYBACK, "REWFRA");
		else if (M_IsItemOn(MN_PLAYBACK, "PAUSE"))
			M_SetItemOn(MN_PLAYBACK, "RESUME");
		else if (M_IsItemOn(MN_PLAYBACK, "FASTFW"))
			M_SetItemOn(MN_PLAYBACK, "ADVFRA");
	}
	else
	{
		M_SetItemStatus(MN_PLAYBACK, "PAUSE", IT_CALL|IT_STRING);
		M_SetItemStatus(MN_PLAYBACK, "FASTFW", IT_CALL|IT_STRING);
		M_SetItemStatus(MN_PLAYBACK, "REWIND", IT_CALL|IT_STRING);
		M_SetItemStatus(MN_PLAYBACK, "RESUME", IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "ADVFRA", IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "REWFRA", IT_DISABLED);

		if (M_IsItemOn(MN_PLAYBACK, "REWFRA"))
			M_SetItemOn(MN_PLAYBACK, "REWIND");
		else if (M_IsItemOn(MN_PLAYBACK, "RESUME"))
			M_SetItemOn(MN_PLAYBACK, "PAUSE");
		else if (M_IsItemOn(MN_PLAYBACK, "ADVFRA"))
			M_SetItemOn(MN_PLAYBACK, "FASTFW");
	}

	if (modeattacking)
	{
		M_SetItemStatus(MN_PLAYBACK, "NUMVIE", IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "VIEW1", IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "VIEW2", IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "VIEW3", IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "VIEW4", IT_DISABLED);
		M_SetItemX(MN_PLAYBACK, "FRECAM", 72);
		M_SetItemX(MN_PLAYBACK, "QUIT", 88);

		currentMenu->x = BASEVIDWIDTH/2 - 52;
	}
	else
	{
		M_SetItemStatus(MN_PLAYBACK, "NUMVIE", IT_ARROWS|IT_STRING);
		M_SetItemStatus(MN_PLAYBACK, "VIEW1", r_splitscreen >= 0 ? IT_ARROWS|IT_STRING : IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "VIEW2", r_splitscreen >= 1 ? IT_ARROWS|IT_STRING : IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "VIEW3", r_splitscreen >= 2 ? IT_ARROWS|IT_STRING : IT_DISABLED);
		M_SetItemStatus(MN_PLAYBACK, "VIEW4", r_splitscreen >= 3 ? IT_ARROWS|IT_STRING : IT_DISABLED);

		M_SetItemX(MN_PLAYBACK, "FRECAM", 156);
		M_SetItemX(MN_PLAYBACK, "QUIT", 172);
		currentMenu->x = BASEVIDWIDTH/2 - 88;
	}

	// wip
	//M_DrawTextBox(currentMenu->x-68, currentMenu->y-7, 15, 15);
	//M_DrawCenteredMenu();

	for (i = 0; i < currentMenu->numitems; i++)
	{
		UINT8 *inactivemap = NULL;

		INT16 splitnum = i == M_GetMenuIndex(MN_PLAYBACK, "VIEW1") ? 0 :
		                 i == M_GetMenuIndex(MN_PLAYBACK, "VIEW2") ? 1 :
		                 i == M_GetMenuIndex(MN_PLAYBACK, "VIEW3") ? 2 :
		                 i == M_GetMenuIndex(MN_PLAYBACK, "VIEW4") ? 3 : -1;

		if (splitnum >= 0 && splitnum < 4)
		{
			if (modeattacking) continue;

			if (r_splitscreen >= splitnum)
			{
				INT32 ply = displayplayers[splitnum];

				icon = faceprefix[players[ply].skin][FACE_RANK];
				if (i != itemOn)
					inactivemap = R_GetTranslationColormap(players[ply].skin, players[ply].skincolor, GTC_MENUCACHE);
			}
			else if (currentMenu->menuitems[i].patch && W_CheckNumForName(currentMenu->menuitems[i].patch) != LUMPERROR)
				icon = W_CachePatchName(currentMenu->menuitems[i].patch, PU_CACHE);
			else
				icon = W_CachePatchName("PLAYRANK", PU_CACHE); // temp
		}
		else if (currentMenu->menuitems[i].status == IT_DISABLED)
			continue;
		else if (currentMenu->menuitems[i].patch && W_CheckNumForName(currentMenu->menuitems[i].patch) != LUMPERROR)
			icon = W_CachePatchName(currentMenu->menuitems[i].patch, PU_CACHE);
		else
			icon = W_CachePatchName("PLAYRANK", PU_CACHE); // temp

		if ((i == M_GetMenuIndex(MN_PLAYBACK, "FASTFW") && cv_playbackspeed.value > 1) || (i == M_GetMenuIndex(MN_PLAYBACK, "REWIND") && demo.rewinding))
			V_DrawMappedPatch(currentMenu->x + currentMenu->menuitems[i].alphaKey, currentMenu->y, transmap|V_SNAPTOTOP, icon, R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_JAWZ, GTC_MENUCACHE));
		else
			V_DrawMappedPatch(currentMenu->x + currentMenu->menuitems[i].alphaKey, currentMenu->y, transmap|V_SNAPTOTOP, icon, (i == itemOn) ? activemap : inactivemap);

		if (i == itemOn)
		{
			V_DrawCharacter(currentMenu->x + currentMenu->menuitems[i].alphaKey + 4, currentMenu->y + 14,
				'\x1A' | transmap|V_SNAPTOTOP|highlightflags, false);

			V_DrawCenteredString(BASEVIDWIDTH/2, currentMenu->y + 18, transmap|V_SNAPTOTOP|V_ALLOWLOWERCASE, currentMenu->menuitems[i].text);

			if ((currentMenu->menuitems[i].status & IT_TYPE) == IT_ARROWS)
			{
				char *str = NULL;

				if (!(i == M_GetMenuIndex(MN_PLAYBACK, "NUMVIE") && r_splitscreen == 3))
					V_DrawCharacter(BASEVIDWIDTH/2 - 4, currentMenu->y + 28 - (skullAnimCounter/5),
						'\x1A' | transmap|V_SNAPTOTOP|highlightflags, false); // up arrow

				if (!(i == M_GetMenuIndex(MN_PLAYBACK, "NUMVIE") && r_splitscreen == 0))
					V_DrawCharacter(BASEVIDWIDTH/2 - 4, currentMenu->y + 48 + (skullAnimCounter/5),
						'\x1B' | transmap|V_SNAPTOTOP|highlightflags, false); // down arrow

				if (i == M_GetMenuIndex(MN_PLAYBACK, "NUMVIE"))
					str = va("%d", r_splitscreen+1);
				else if (splitnum >= 0 && splitnum < 4)
					str = player_names[displayplayers[splitnum]]; // 0 to 3
				else
					I_Error("bruh");

				V_DrawCenteredString(BASEVIDWIDTH/2, currentMenu->y + 38, transmap|V_SNAPTOTOP|V_ALLOWLOWERCASE|highlightflags, str);
			}
		}
	}
}

void M_PlaybackRewind(INT32 choice)
{
	static tic_t lastconfirmtime;

	(void)choice;

	if (!demo.rewinding)
	{
		if (paused)
		{
			G_ConfirmRewind(leveltime-1);
			paused = true;
			S_PauseAudio();
		}
		else
			demo.rewinding = paused = true;
	}
	else if (lastconfirmtime + TICRATE/2 < I_GetTime())
	{
		lastconfirmtime = I_GetTime();
		G_ConfirmRewind(leveltime);
	}

	CV_SetValue(&cv_playbackspeed, 1);
}

void M_PlaybackPause(INT32 choice)
{
	(void)choice;

	paused = !paused;

	if (demo.rewinding)
	{
		G_ConfirmRewind(leveltime);
		paused = true;
		S_PauseAudio();
	}
	else if (paused)
		S_PauseAudio();
	else
		S_ResumeAudio();

	CV_SetValue(&cv_playbackspeed, 1);
}

void M_PlaybackFastForward(INT32 choice)
{
	(void)choice;

	if (demo.rewinding)
	{
		G_ConfirmRewind(leveltime);
		paused = false;
		S_ResumeAudio();
	}
	CV_SetValue(&cv_playbackspeed, cv_playbackspeed.value == 1 ? 4 : 1);
}

void M_PlaybackAdvance(INT32 choice)
{
	(void)choice;

	paused = false;
	TryRunTics(1);
	paused = true;
}

void M_PlaybackSetViews(INT32 choice)
{
	if (demo.freecam)
		return; // not here.

	if (choice > 0)
	{
		if (r_splitscreen < 3)
			G_AdjustView(r_splitscreen + 2, 0, true);
	}
	else if (r_splitscreen)
	{
		if (choice == 0)
		{
			G_SyncDemoParty(displayplayers[r_splitscreen], r_splitscreen - 1);
		}
		else
		{
			G_SyncDemoParty(consoleplayer, 0);
		}
	}
}

void M_PlaybackAdjustView(INT32 choice)
{
	INT16 splitnum = M_IsItemOn(MN_PLAYBACK, "VIEW1") ? 1 :
	                 M_IsItemOn(MN_PLAYBACK, "VIEW2") ? 2 :
	                 M_IsItemOn(MN_PLAYBACK, "VIEW3") ? 3 :
	                 M_IsItemOn(MN_PLAYBACK, "VIEW4") ? 4 : -1;

	if (splitnum >= 1 && splitnum <= 4)
		G_AdjustView(splitnum, (choice > 0) ? 1 : -1, true);
}

// this one's rather tricky
void M_PlaybackToggleFreecam(INT32 choice)
{
	(void)choice;
	M_ClearMenus(true);

	// remove splitscreen:
	splitscreen = 0;
	R_ExecuteSetViewSize();

	UINT8 i;
	for (i = 0; i <= r_splitscreen; ++i)
	{
		P_ToggleDemoCamera(i);
	}
}

void M_PlaybackQuit(INT32 choice)
{
	(void)choice;
	G_StopDemo();

	if (demo.inreplayhut)
		M_ReplayHut(choice);
	else if (modeattacking)
	{
		M_ModeAttackEndGame(0);
		S_ChangeMusicInternal("racent", true);
	}
	else
	{
		M_ClearMenus(true);
		D_StartTitle();
	}
}

void M_PandorasBox(INT32 choice)
{
	(void)choice;
	CV_StealthSetValue(&cv_dummyrings, players[consoleplayer].rings);
	CV_StealthSetValue(&cv_dummylives, players[consoleplayer].lives);
	M_EnterMenu(MN_SR_PANDORA, true);
}

void M_ExitPandorasBox(INT32 choice)
{
	(void)choice;
	if (cv_dummyrings.value != players[consoleplayer].rings)
		COM_ImmedExecute(va("setrings %d", cv_dummyrings.value));
	if (cv_dummylives.value != players[consoleplayer].lives)
		COM_ImmedExecute(va("setlives %d", cv_dummylives.value));
}

void M_ChangeLevel(INT32 choice)
{
	(void)choice;
	INT16 map = cv_nextmap.value ? cv_nextmap.value : G_RandMap(G_TOLFlag(cv_newgametype.value), gamestate == GS_LEVEL ? gamemap-1 : prevmap, 0, 0, false, NULL);
	M_ClearMenus(true);
	COM_BufAddText(va("map %d -gametype \"%s\"\n", map, cv_newgametype.string));
}

void M_ConfirmSpectate(INT32 choice)
{
	(void)choice;
	// We allow switching to spectator even if team changing is not allowed
	M_ClearMenus(true);
	COM_ImmedExecute("changeteam spectator");
}

void M_ConfirmEnterGame(INT32 choice)
{
	(void)choice;
	if (!cv_allowteamchange.value)
	{
		M_StartMessage(M_GetText("The server is not allowing\nteam changes at this time.\nPress a key.\n"), NULL, MM_NOTHING);
		return;
	}
	M_ClearMenus(true);
	COM_ImmedExecute("changeteam playing");
}

void M_ConfirmTeamScramble(INT32 choice)
{
	(void)choice;
	M_ClearMenus(true);

	COM_ImmedExecute(va("teamscramble %d", cv_dummyscramble.value+1));
}

void M_ConfirmTeamChange(INT32 choice)
{
	(void)choice;

	if (cv_dummymenuplayer.value > splitscreen+1)
		return;

	if (!cv_allowteamchange.value && cv_dummyteam.value)
	{
		M_StartMessage(M_GetText("The server is not allowing\nteam changes at this time.\nPress a key.\n"), NULL, MM_NOTHING);
		return;
	}

	M_ClearMenus(true);

	switch (cv_dummymenuplayer.value)
	{
		case 1:
		default:
			COM_ImmedExecute(va("changeteam %s", cv_dummyteam.string));
			break;
		case 2:
			COM_ImmedExecute(va("changeteam2 %s", cv_dummyteam.string));
			break;
		case 3:
			COM_ImmedExecute(va("changeteam3 %s", cv_dummyteam.string));
			break;
		case 4:
			COM_ImmedExecute(va("changeteam4 %s", cv_dummyteam.string));
			break;
	}
}

void M_ConfirmSpectateChange(INT32 choice)
{
	(void)choice;

	if (cv_dummymenuplayer.value > splitscreen+1)
		return;

	if (!cv_allowteamchange.value && cv_dummyspectate.value)
	{
		M_StartMessage(M_GetText("The server is not allowing\nteam changes at this time.\nPress a key.\n"), NULL, MM_NOTHING);
		return;
	}

	M_ClearMenus(true);

	switch (cv_dummymenuplayer.value)
	{
		case 1:
		default:
			COM_ImmedExecute(va("changeteam %s", cv_dummyspectate.string));
			break;
		case 2:
			COM_ImmedExecute(va("changeteam2 %s", cv_dummyspectate.string));
			break;
		case 3:
			COM_ImmedExecute(va("changeteam3 %s", cv_dummyspectate.string));
			break;
		case 4:
			COM_ImmedExecute(va("changeteam4 %s", cv_dummyspectate.string));
			break;
	}
}

void M_Options(INT32 choice)
{
	(void)choice;

	// if the player is not admin or server, disable gameplay & server options
	M_SetItemStatus(MN_OP_MAIN, "GAMEPL", Playing() && !(server || IsPlayerAdmin(consoleplayer)) ? IT_GRAYEDOUT : IT_STRING|IT_SUBMENU);
	M_SetItemStatus(MN_OP_MAIN, "SERVER", Playing() && !(server || IsPlayerAdmin(consoleplayer)) ? IT_GRAYEDOUT : IT_STRING|IT_SUBMENU);

	// no credits or data erasing in-game
	M_SetItemStatus(MN_OP_MAIN, "KCRED", Playing() ? IT_GRAYEDOUT : IT_STRING|IT_CALL);
	M_SetItemStatus(MN_OP_MAIN, "BCRED", Playing() ? IT_GRAYEDOUT : IT_STRING|IT_CALL);
	M_SetItemStatus(MN_OP_DATA, "ERASE", Playing() ? IT_GRAYEDOUT : IT_STRING|IT_SUBMENU);

	M_SetItemStatus(MN_OP_GAME, "ENCORE", M_SecretUnlocked(SECRET_ENCORE) ? IT_CVAR|IT_STRING : IT_SECRET);

	M_SetItemStatus(MN_OP_MAIN, "CUSTOM", !menudefs[MN_OP_CUSTOM].numitems ? IT_GRAYEDOUT : IT_STRING|IT_SUBMENU);

	M_EnterMenu(MN_OP_MAIN, true);
}

void M_Manual(INT32 choice)
{
	(void)choice;
	M_EnterMenu(MN_HELP, true);
}

static void M_RetryResponse(INT32 ch)
{
	if (ch != 'y' && ch != KEY_ENTER)
		return;

	if (netgame || multiplayer) // Should never happen!
		return;

	M_ClearMenus(true);
	G_SetRetryFlag();
}

void M_Retry(INT32 choice)
{
	(void)choice;
	M_StartMessage(va("Start this %s over?\n\n(Press 'Y' to confirm)\n", (gametyperules & GTR_CIRCUIT) ? "race" : "battle"),M_RetryResponse,MM_YESNO);
}

void M_SelectableClearMenus(INT32 choice)
{
	(void)choice;
	M_ClearMenus(true);
}

void M_RefreshPauseMenu(void)
{
#ifdef HAVE_DISCORDRPC
	if (discordRequestList != NULL)
	{
		M_SetItemStatus(MN_MPAUSE, "DISCRQ", IT_STRING | IT_SUBMENU);
	}
	else
	{
		M_SetItemStatus(MN_MPAUSE, "DISCRQ", IT_GRAYEDOUT);
	}
#endif
}

// ======
// CHEATS
// ======

ATTRNORETURN void FUNCNORETURN M_UltimateCheat(INT32 choice)
{
	(void)choice;
	I_Quit();
}

void M_GetAllEmeralds(INT32 choice)
{
	(void)choice;

	emeralds = ((EMERALD7)*2)-1;
	M_StartMessage(M_GetText("You now have all 7 emeralds.\nUse them wisely.\nWith great power comes great ring drain.\n"),NULL,MM_NOTHING);

	G_SetGameModified(multiplayer, true);
}

static void M_DestroyRobotsResponse(INT32 ch)
{
	if (ch != 'y' && ch != KEY_ENTER)
		return;

	// Destroy all robots
	P_DestroyRobots();

	G_SetGameModified(multiplayer, true);
}

void M_DestroyRobots(INT32 choice)
{
	(void)choice;

	M_StartMessage(M_GetText("Do you want to destroy all\nrobots in the current level?\n\n(Press 'Y' to confirm)\n"), M_DestroyRobotsResponse, MM_YESNO);
}

// ========
// SKY ROOM
// ========

UINT8 skyRoomMenuTranslations[MAXUNLOCKABLES];

static char *M_GetConditionString(condition_t cond)
{
	switch(cond.type)
	{
		case UC_PLAYTIME:
			return va("Play for %i:%02i:%02i",
				G_TicsToHours(cond.requirement),
				G_TicsToMinutes(cond.requirement, false),
				G_TicsToSeconds(cond.requirement));
		case UC_MATCHESPLAYED:
			return va("Play %d matches", cond.requirement);
		case UC_POWERLEVEL:
			return va("Reach power level %d in %s", cond.requirement, (cond.extrainfo1 == PWRLV_BATTLE ? "Battle" : "Race"));
		case UC_GAMECLEAR:
			if (cond.requirement > 1)
				return va("Beat game %d times", cond.requirement);
			else
				return va("Beat the game");
		case UC_OVERALLTIME:
			return va("Get overall Time Attack of %i:%02i:%02i",
				G_TicsToHours(cond.requirement),
				G_TicsToMinutes(cond.requirement, false),
				G_TicsToSeconds(cond.requirement));
		case UC_MAPVISITED:
			return va("Visit %s", G_BuildMapTitle(cond.requirement-1));
		case UC_MAPBEATEN:
			return va("Beat %s", G_BuildMapTitle(cond.requirement-1));
		case UC_MAPENCORE:
			return va("Beat %s in Encore Mode", G_BuildMapTitle(cond.requirement-1));
		case UC_MAPTIME:
			return va("Beat %s in %i:%02i.%02i", G_BuildMapTitle(cond.extrainfo1-1),
				G_TicsToMinutes(cond.requirement, true),
				G_TicsToSeconds(cond.requirement),
				G_TicsToCentiseconds(cond.requirement));
		case UC_TOTALEMBLEMS:
			return va("Get %d medals", cond.requirement);
		case UC_EXTRAEMBLEM:
			return va("Get \"%s\" medal", extraemblems[cond.requirement-1].name);
		default:
			return NULL;
	}
}

#define NUMCHECKLIST 23
void M_DrawChecklist(void)
{
	UINT32 i, line = 0, c;
	INT32 lastid;
	boolean secret = false;

	for (i = 0; i < MAXUNLOCKABLES; i++)
	{
		const char *secretname;

		secret = (!M_Achieved(unlockables[i].showconditionset - 1) && !unlockables[i].unlocked);

		if (unlockables[i].name[0] == 0 || unlockables[i].nochecklist
		|| !unlockables[i].conditionset || unlockables[i].conditionset > MAXCONDITIONSETS
		|| (unlockables[i].type == SECRET_HELLATTACK && secret)) // TODO: turn this into an unlockable setting instead of tying it to Hell Attack
			continue;

		++line;
		secretname = M_CreateSecretMenuOption(unlockables[i].name);

		V_DrawString(8, (line*8), (unlockables[i].unlocked ? recommendedflags : warningflags), (secret ? secretname : unlockables[i].name));

		if (conditionSets[unlockables[i].conditionset - 1].numconditions)
		{
			c = 0;
			lastid = -1;

			for (c = 0; c < conditionSets[unlockables[i].conditionset - 1].numconditions; c++)
			{
				condition_t cond = conditionSets[unlockables[i].conditionset - 1].condition[c];
				UINT8 achieved = M_CheckCondition(&cond);
				char *str = M_GetConditionString(cond);
				const char *secretstr = M_CreateSecretMenuOption(str);

				if (!str)
					continue;

				++line;

				if (lastid == -1 || cond.id != (UINT32)lastid)
				{
					V_DrawString(16, (line*8), V_MONOSPACE|V_ALLOWLOWERCASE|(achieved ? highlightflags : 0), "*");
					V_DrawString(32, (line*8), V_MONOSPACE|V_ALLOWLOWERCASE|(achieved ? highlightflags : 0), (secret ? secretstr : str));
				}
				else
				{
					V_DrawString(32, (line*8), V_MONOSPACE|V_ALLOWLOWERCASE|(achieved ? highlightflags : 0), (secret ? "?" : "&"));
					V_DrawString(48, (line*8), V_MONOSPACE|V_ALLOWLOWERCASE|(achieved ? highlightflags : 0), (secret ? secretstr : str));
				}

				lastid = cond.id;
			}
		}

		++line;

		if (line >= NUMCHECKLIST)
			break;
	}
}
#undef NUMCHECKLIST

#define NUMHINTS 5
void M_EmblemHints(INT32 choice)
{
	(void)choice;
	M_SetItemStatus(MN_SR_EMBLEMHINT, "RADAR", M_SecretUnlocked(SECRET_ITEMFINDER) ? IT_CVAR|IT_STRING : IT_SECRET);
	M_EnterMenu(MN_SR_EMBLEMHINT, true);
	M_SetItemOn(MN_SR_EMBLEMHINT, "BACK"); // always start on back.
}

void M_DrawEmblemHints(void)
{
	INT32 i, j = 0;
	UINT32 collected = 0;
	emblem_t *emblem;
	const char *hint;

	for (i = 0; i < numemblems; i++)
	{
		INT32 checkLevel;

		emblem = &emblemlocations[i];
		if (emblem->type != ET_GLOBAL)
			continue;

		checkLevel = G_MapNumber(emblem->level);

		if (!mapheaderinfo[checkLevel] || gamemap != checkLevel)
			continue;

		if (emblem->collected)
		{
			collected = recommendedflags;
			V_DrawMappedPatch(12, 12+(28*j), 0, W_CachePatchName(M_GetEmblemPatch(emblem, false), PU_CACHE),
				R_GetTranslationColormap(TC_DEFAULT, M_GetEmblemColor(emblem), GTC_MENUCACHE));
		}
		else
		{
			collected = 0;
			V_DrawScaledPatch(12, 12+(28*j), 0, W_CachePatchName("NEEDIT", PU_CACHE));
		}

		if (emblem->hint[0])
			hint = emblem->hint;
		else
			hint = M_GetText("No hints available.");
		hint = V_WordWrap(40, BASEVIDWIDTH-12, 0, hint);
		V_DrawString(40, 8+(28*j), V_ALLOWLOWERCASE|collected, hint);

		if (++j >= NUMHINTS)
			break;
	}
	if (!j)
		V_DrawCenteredString(160, 48, highlightflags, "No hidden medals on this map.");

	M_DrawGenericMenu();
}

void M_DrawSkyRoom(void)
{
	INT32 i, y = 0;
	INT32 lengthstring = 0;

	M_DrawGenericMenu();

	if (menustack[0] == MN_OP_SOUND)
	{
		V_DrawRightAlignedString(BASEVIDWIDTH - currentMenu->x,
			currentMenu->y+M_GetItemY(MN_OP_SOUND, "SNDENA"),
			(sound_disabled ? warningflags : highlightflags),
			(sound_disabled ? "OFF" : "ON"));

		V_DrawRightAlignedString(BASEVIDWIDTH - currentMenu->x,
			currentMenu->y+M_GetItemY(MN_OP_SOUND, "MUSENA"),
			(digital_disabled ? warningflags : highlightflags),
			(digital_disabled ? "OFF" : "ON"));

		if (M_IsItemOn(MN_OP_SOUND, "SNDENA"))
			lengthstring = 8*(sound_disabled ? 3 : 2);
		else if (M_IsItemOn(MN_OP_SOUND, "MUSENA"))
			lengthstring = 8*(digital_disabled ? 3 : 2);
	}

	for (i = 0; i < currentMenu->numitems; ++i)
	{
		if (currentMenu->menuitems[i].itemaction.routine == M_HandleSoundTest)
		{
			y = currentMenu->menuitems[i].alphaKey;
			break;
		}
	}

	if (y)
	{
		y += currentMenu->y;

		V_DrawRightAlignedString(BASEVIDWIDTH - currentMenu->x, y, highlightflags, cv_soundtest.string);
		if (cv_soundtest.value)
			V_DrawRightAlignedString(BASEVIDWIDTH - currentMenu->x, y + 8, highlightflags, S_sfx[cv_soundtest.value].name);

		if (i == itemOn)
			lengthstring = V_StringWidth(cv_soundtest.string, 0);
	}

	if (lengthstring)
	{
		V_DrawCharacter(BASEVIDWIDTH - currentMenu->x - 10 - lengthstring - (skullAnimCounter/5), currentMenu->y+currentMenu->menuitems[itemOn].alphaKey,
			'\x1C' | highlightflags, false); // left arrow
		V_DrawCharacter(BASEVIDWIDTH - currentMenu->x + 2 + (skullAnimCounter/5), currentMenu->y+currentMenu->menuitems[itemOn].alphaKey,
			'\x1D' | highlightflags, false); // right arrow
	}
}

void M_HandleSoundTest(INT32 choice)
{
	boolean exitmenu = false; // exit to previous menu

	switch (choice)
	{
		case KEY_DOWNARROW:
			M_NextOpt();
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_UPARROW:
			M_PrevOpt();
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_BACKSPACE:
		case KEY_ESCAPE:
			exitmenu = true;
			break;

		case KEY_RIGHTARROW:
			CV_AddValue(&cv_soundtest, 1);
			break;
		case KEY_LEFTARROW:
			CV_AddValue(&cv_soundtest, -1);
			break;
		case KEY_ENTER:
			S_StopSounds();
			S_StartSound(NULL, cv_soundtest.value);
			break;

		default:
			break;
	}
	if (exitmenu)
		M_ExitMenu();
}

static musicdef_t *curplaying = NULL;
static INT32 st_sel = 0;
static tic_t st_time = 0;
static size_t st_namescroll = 0;
static size_t st_namescrollstate = 0;

void M_MusicTest(INT32 choice)
{
	//INT32 ul = skyRoomMenuTranslations[choice-1];
	//UINT8 i;
	//char buf[8];
	(void)choice;

	if (!S_PrepareSoundTest())
	{
		M_StartMessage(M_GetText("No selectable tracks found.\n"),NULL,MM_NOTHING);
		return;
	}

	curplaying = NULL;
	st_time = 0;

	st_sel = 0;

	M_EnterMenu(MN_SR_SOUNDTEST, true);
}

void M_DrawMusicTest(void)
{
	INT32 x, y, i;

	// let's handle the ticker first. ideally we'd tick this somewhere else, BUT...
	if (curplaying)
	{
		{
			fixed_t work;

			work = st_time;

			if (st_time >= (FRACUNIT << (FRACBITS - 2))) // prevent overflow jump - takes about 15 minutes of loop on the same song to reach
					st_time = work;

			st_time += renderdeltatics;
		}
	}

	x = 90<<FRACBITS;
	y = (BASEVIDHEIGHT-32)<<FRACBITS;

	y = (BASEVIDWIDTH-(vid.width/vid.dupx))/2;

	V_DrawFill(y-1, 20, vid.width/vid.dupx+1, 24, 159);
	{
		static fixed_t st_scroll = -FRACUNIT;
		const char* titl;

		x = 16;
		V_DrawString(x, 10, 0, "NOW PLAYING:");
		if (curplaying)
		{
			if (curplaying->source && curplaying->source[0] && curplaying->title && curplaying->title[0])
				titl = va("%s - %s - ", curplaying->title, curplaying->source);
			else if (curplaying->title && curplaying->title[0])
				titl = va("%s - ", curplaying->title);
			else
				titl = va("%s - ", "No Title - ");
		}
		else
			titl = "NONE - ";

		i = V_LevelNameWidth(titl);

		st_scroll += renderdeltatics;

		while (st_scroll >= (i << FRACBITS))
			st_scroll -= i << FRACBITS;

		x -= st_scroll >> FRACBITS;

		while (x < BASEVIDWIDTH-y)
			x += i;
		while (x > y)
		{
			x -= i;
			V_DrawLevelTitle(x, 24, 0, titl);
		}

		if (curplaying && curplaying->author && curplaying->composers && curplaying->composers[0] && curplaying->author[0])
			V_DrawRightAlignedThinString(BASEVIDWIDTH-16, 46, V_ALLOWLOWERCASE, va ("%s, %s",curplaying->author, curplaying->composers));
		else if (curplaying && curplaying->author && curplaying->author[0])
			V_DrawRightAlignedThinString(BASEVIDWIDTH-16, 46, V_ALLOWLOWERCASE, curplaying->author);

		if (curplaying)
		{
			if (!curplaying->usage || !curplaying->usage[0])
				V_DrawString(vid.dupx, vid.height - 10*vid.dupy, V_NOSCALESTART|V_ALLOWLOWERCASE, va("%.6s", &curplaying->name[0][0]));
			else {
				V_DrawSmallString(vid.dupx, vid.height - 5*vid.dupy, V_NOSCALESTART|V_ALLOWLOWERCASE, va("%.6s - %.255s\n", &curplaying->name[0][0], curplaying->usage));
			}
		}
	}

	V_DrawFill(20, 60, 280, 128, 159);

	{
		INT32 t, b, q, m = 128;

		if (numsoundtestdefs <= 8)
		{
			t = 0;
			b = numsoundtestdefs - 1;
			i = 0;
		}
		else
		{
			q = m;
			m = (5*m)/numsoundtestdefs;
			if (st_sel < 3)
			{
				t = 0;
				b = 7;
				i = 0;
			}
			else if (st_sel >= numsoundtestdefs-4)
			{
				t = numsoundtestdefs - 8;
				b = numsoundtestdefs - 1;
				i = q-m;
			}
			else
			{
				t = st_sel - 3;
				b = st_sel + 4;
				i = (t * (q-m))/(numsoundtestdefs - 8);
			}
		}

		V_DrawFill(20+280-1, 60 + i, 1, m, 0);

		if (t != 0)
			V_DrawString(20+280+4, 60+4 - (skullAnimCounter/5), V_YELLOWMAP, "\x1A");

		if (b != numsoundtestdefs - 1)
			V_DrawString(20+280+4, 60+128-12 + (skullAnimCounter/5), V_YELLOWMAP, "\x1B");

		x = 24;
		y = 64;

		if (renderisnewtic) ++st_namescroll;

		while (t <= b)
		{
			if (t == st_sel)
				V_DrawFill(20, y-4, 280-1, 16, 157);

			{
				const size_t MAXLENGTH = 34;
				const tic_t SCROLLSPEED = TICRATE/5; // Number of tics for name being scrolled by 1 letter
				size_t nameoffset = 0;
				size_t namelength = soundtestdefs[t]->title ? strlen(soundtestdefs[t]->title) : 8; // "No Title"
				if (soundtestdefs[t]->source && (soundtestdefs[t]->source[0]))
					namelength = strlen(va("%s - %s ", soundtestdefs[t]->title, soundtestdefs[t]->source));

				char buf[MAXLENGTH+1];

				if (t == st_sel && namelength > MAXLENGTH)
				{
					switch (st_namescrollstate)
					{
						case 0:
						{
							// Scroll forward
							nameoffset = (st_namescroll/SCROLLSPEED) % (namelength - MAXLENGTH + 1);

							if (nameoffset == namelength - MAXLENGTH)
							{
								st_namescroll = 0;
								st_namescrollstate++;
							}
						}
						break;

						case 1:
						{
							nameoffset = namelength - MAXLENGTH;

							// Show name end for 1 second, then start scrolling back
							if (st_namescroll == TICRATE)
							{
								st_namescroll = 0;
								st_namescrollstate++;
							}
						}
						break;

						case 2:
						{
							// Scroll back
							nameoffset = (namelength - MAXLENGTH - 1) - (st_namescroll/SCROLLSPEED) % (namelength - MAXLENGTH);

							if (nameoffset == 0)
							{
								st_namescroll = 0;
								st_namescrollstate++;
							}
						}
						break;

						case 3:
						{
							nameoffset = 0;

							// Show name beginning for 1 second, then start scrolling forward again
							if (st_namescroll == TICRATE)
							{
								st_namescroll = 0;
								st_namescrollstate = 0;
							}
						}
						break;
					}
				}

				if (soundtestdefs[t]->title && soundtestdefs[t]->title[0]
					&& soundtestdefs[t]->source && soundtestdefs[t]->source[0])
					strncpy(buf, va("%s - %s",soundtestdefs[t]->title, soundtestdefs[t]->source) + nameoffset, MAXLENGTH);
				else if (soundtestdefs[t]->title && soundtestdefs[t]->title[0])
					strncpy(buf, soundtestdefs[t]->title + nameoffset, MAXLENGTH);
				else
					strncpy(buf, "No Title or Source", MAXLENGTH);
				buf[MAXLENGTH] = 0;

				V_DrawString(x, y, (t == st_sel ? V_YELLOWMAP : 0)|V_ALLOWLOWERCASE|V_MONOSPACE, buf);
				if (curplaying == soundtestdefs[t])
				{
					V_DrawFill(20+280-9, y-4, 8, 16, 150);
				}
			}
			t++;
			y += 16;
		}
	}
}

void M_HandleMusicTest(INT32 choice)
{
	boolean exitmenu = false; // exit to previous menu

	switch (choice)
	{
		case KEY_DOWNARROW:
			if (st_sel++ >= numsoundtestdefs-1)
				st_sel = 0;
			{
				S_StartSound(NULL, sfx_menu1);
			}
			st_namescroll = 0;
			st_namescrollstate = 0;
			break;
		case KEY_UPARROW:
			if (!st_sel--)
				st_sel = numsoundtestdefs-1;
			{
				S_StartSound(NULL, sfx_menu1);
			}
			st_namescroll = 0;
			st_namescrollstate = 0;
			break;
		case KEY_PGDN:
			if (st_sel < numsoundtestdefs-1)
			{
				st_sel += 3;
				if (st_sel >= numsoundtestdefs-1)
					st_sel = numsoundtestdefs-1;
				S_StartSound(NULL, sfx_menu1);
			}
			st_namescroll = 0;
			st_namescrollstate = 0;
			break;
		case KEY_PGUP:
			if (st_sel)
			{
				st_sel -= 3;
				if (st_sel < 0)
					st_sel = 0;
				S_StartSound(NULL, sfx_menu1);
			}
			st_namescroll = 0;
			st_namescrollstate = 0;
			break;
		case KEY_BACKSPACE:
			if (curplaying)
			{
				S_StopSounds();
				S_StopMusic();
				curplaying = NULL;
				st_time = 0;
				S_StartSound(NULL, sfx_skid);
			}
			break;
		case KEY_ESCAPE:
			exitmenu = true;
			st_namescroll = 0;
			st_namescrollstate = 0;
			break;

		case KEY_RIGHTARROW:
		case KEY_LEFTARROW:
		case KEY_ENTER:
			S_StopSounds();
			S_StopMusic();
			st_time = 0;
			curplaying = soundtestdefs[st_sel];
			S_ChangeMusicInternal(&curplaying->name[0][0], true);
			break;

		default:
			break;
	}
	if (exitmenu)
	{
		Z_Free(soundtestdefs);
		soundtestdefs = NULL;

		M_ExitMenu();
	}
}

// ==================
// NEW GAME FUNCTIONS
// ==================

void M_Credits(INT32 choice)
{
	(void)choice;
	cursaveslot = -2;
	M_ClearMenus(true);
	F_StartCredits();
}

void M_BlanCredits(INT32 choice)
{
	(void)choice;
	cursaveslot = -2;
	M_ClearMenus(true);
	F_BlanStartCredits();
}

// ==================
// SINGLE PLAYER MENU
// ==================

void M_SinglePlayerMenu(INT32 choice)
{
	(void)choice;
	M_SetItemStatus(MN_SP_MAIN, "GP", IT_CALL|IT_STRING);
	M_SetItemStatus(MN_SP_MAIN, "TA", M_SecretUnlocked(SECRET_TIMEATTACK) ? IT_CALL|IT_STRING : IT_SECRET);
	M_SetItemStatus(MN_SP_MAIN, "IT", M_SecretUnlocked(SECRET_ITEMBREAKER) ? IT_CALL|IT_STRING : IT_SECRET);
	M_EnterMenu(MN_SP_MAIN, true);
}

// ===============
// STATISTICS MENU
// ===============

static void M_DrawStatsMaps(void);
static void M_DrawStatsPlaytime(void);
static void M_DrawStatsExtra(void); // dunno how to name this one

static INT32 statsLocation;
static INT32 statsMax;
static INT16 *statsMapList = NULL;
static INT16 statsMapListLen;
static UINT8 statsCurrentPage = 0;

typedef struct statpage_s {
	const char *title;
	void (*drawer)(void);
} statpage_t;

static statpage_t statsPages[] = {
	{ "Play Time Statistics", M_DrawStatsPlaytime, },
	{ "Level Statistics", M_DrawStatsMaps, },
	{ "Extra Statistics", M_DrawStatsExtra, },
};

#define LENSTATSPAGES (sizeof(statsPages)/sizeof(statsPages[0]))
#define NUMSTATSPAGES (kartstats.vanilla ? 2 : LENSTATSPAGES)

void M_Statistics(INT32 choice)
{
	INT16 i, j = 0;

	(void)choice;

	if (!statsMapList || nummapheaders != statsMapListLen)
	{
		statsMapList = Z_Realloc(statsMapList, nummapheaders * sizeof(*statsMapList), PU_LEVEL, NULL);
		statsMapListLen = nummapheaders;
	}

	for (i = 0; i < nummapheaders; i++)
	{
		if (!mapheaderinfo[i] || mapheaderinfo[i]->lvlttl[0] == '\0')
			continue;

		if (mapheaderinfo[i]->menuflags & (LF2_HIDEINSTATS|LF2_HIDEINMENU))
			continue;

		if (M_MapLocked(i+1)) // !mapvisited[i]
			continue;

		statsMapList[j++] = i;
	}
	statsMapList[j] = -1;
	statsMax = j - 11 + numextraemblems;
	statsLocation = 0;
	statsCurrentPage = 0;

	if (statsMax < 0)
		statsMax = 0;

	M_EnterMenu(MN_SP_LEVELSTATS, true);
}

static void M_DrawStatsMaps(void)
{
	char beststr[40];
	tic_t besttime = 0;
	INT32 mapsunfinished = 0;

	int location = statsLocation;
	INT32 y = 62, i = -1, j;
	INT16 mnum;
	extraemblem_t *exemblem;
	boolean dotopname = true, dobottomarrow = (location < statsMax);
	SINT8 preset = G_RecordPresetIndex();

	for (j = 0; j < nummapheaders; j++)
	{
		if (!mapheaderinfo[j] || (mapheaderinfo[j]->menuflags & LF2_NOTIMEATTACK))
			continue;

		if (!mapheaderinfo[j]->mainrecord[preset] || mapheaderinfo[j]->mainrecord[preset]->time <= 0)
		{
			mapsunfinished++;
			continue;
		}

		besttime += mapheaderinfo[j]->mainrecord[preset]->time;
	}

	V_DrawString(20, 42, highlightflags, "Combined time records:");

	sprintf(beststr, "%i:%02i:%02i.%02i", G_TicsToHours(besttime), G_TicsToMinutes(besttime, false), G_TicsToSeconds(besttime), G_TicsToCentiseconds(besttime));
	V_DrawRightAlignedString(BASEVIDWIDTH-16, 42, (mapsunfinished ? warningflags : 0), beststr);

	if (mapsunfinished)
		V_DrawRightAlignedString(BASEVIDWIDTH-16, 50, warningflags, va("(%d unfinished)", mapsunfinished));
	else
		V_DrawRightAlignedString(BASEVIDWIDTH-16, 50, recommendedflags, "(complete)");

	V_DrawString(32, 50, 0, va("x %d/%d", M_CountEmblems(), numemblems+numextraemblems));
	V_DrawSmallScaledPatch(20, 50, 0, W_CachePatchName("GOTITA", PU_STATIC));

	if (location)
		V_DrawCharacter(10, y-(skullAnimCounter/5),
			'\x1A' | highlightflags, false); // up arrow

	while (statsMapList[++i] != -1)
	{
		if (location)
		{
			--location;
			continue;
		}
		else if (dotopname)
		{
			V_DrawString(20,  y, highlightflags, "LEVEL NAME");
			V_DrawString(256, y, highlightflags, "MEDALS");
			y += 8;
			dotopname = false;
		}

		mnum = statsMapList[i];
		M_DrawMapEmblems(mnum+1, 295, y);

		if (mapheaderinfo[mnum]->levelflags & LF_NOZONE)
			V_DrawString(20, y, 0, va("%s %s",
				mapheaderinfo[mnum]->lvlttl,
				mapheaderinfo[mnum]->actnum));
		else
			V_DrawString(20, y, 0, va("%s %s %s",
				mapheaderinfo[mnum]->lvlttl,
				(mapheaderinfo[mnum]->zonttl[0] ? mapheaderinfo[mnum]->zonttl : "Zone"),
				mapheaderinfo[mnum]->actnum));

		y += 8;

		if (y >= BASEVIDHEIGHT-8)
			goto bottomarrow;
	}
	if (dotopname && !location)
	{
		V_DrawString(20,  y, highlightflags, "LEVEL NAME");
		V_DrawString(256, y, highlightflags, "MEDALS");
		y += 8;
	}
	else if (location)
		--location;

	// Extra Emblems
	for (i = -2; i < numextraemblems; ++i)
	{
		if (i == -1)
		{
			V_DrawString(20, y, highlightflags, "EXTRA MEDALS");
			if (location)
			{
				y += 8;
				location++;
			}
		}
		if (location)
		{
			--location;
			continue;
		}

		if (i >= 0)
		{
			exemblem = &extraemblems[i];

			if (exemblem->collected)
				V_DrawSmallMappedPatch(295, y, 0, W_CachePatchName(M_GetExtraEmblemPatch(exemblem, false), PU_CACHE),
				                       R_GetTranslationColormap(TC_DEFAULT, M_GetExtraEmblemColor(exemblem), GTC_MENUCACHE));
			else
				V_DrawSmallScaledPatch(295, y, 0, W_CachePatchName("NEEDIT", PU_CACHE));

			V_DrawString(20, y, 0, va("%s", exemblem->description));
		}

		y += 8;

		if (y >= BASEVIDHEIGHT-8)
			goto bottomarrow;
	}
bottomarrow:
	if (dobottomarrow)
		V_DrawCharacter(10, y-8 + (skullAnimCounter/5),
			'\x1B' | highlightflags, false); // down arrow
}

#define DRAWTIMESTAT(y, title, field) { \
		char timebuf[80]; \
		V_DrawString(20, (y), highlightflags, title); \
		tic_t timeval = kartstats.field; \
		snprintf(timebuf, 80, "%02i:%02i:%02i", G_TicsToHours(timeval), G_TicsToMinutes(timeval, false), G_TicsToSeconds(timeval)); \
		V_DrawRightAlignedString(BASEVIDWIDTH-16, (y), 0, timebuf); \
	}

#define DRAWAMOUNTSTAT(y, title, field) { \
		V_DrawString(20, (y), highlightflags, title); \
		unsigned amountval = kartstats.field; \
		V_DrawRightAlignedString(BASEVIDWIDTH-16, (y), 0, va("%u", amountval)); \
	}

static void M_DrawStatsPlaytime(void)
{
	V_DrawString(20, 42, highlightflags, "Total Play Time:");
	V_DrawCenteredString(BASEVIDWIDTH/2, 52, 0, va("%i hours, %i minutes, %i seconds",
	                         G_TicsToHours(kartstats.totalplaytime),
	                         G_TicsToMinutes(kartstats.totalplaytime, false),
	                         G_TicsToSeconds(kartstats.totalplaytime)));
	V_DrawString(20, 62, highlightflags, "Total Matches:");
	V_DrawRightAlignedString(BASEVIDWIDTH-16, 62, 0, va("%i played", kartstats.matchesplayed));

	V_DrawString(20, 82, highlightflags, "Online Power Level:");
	V_DrawRightAlignedString(BASEVIDWIDTH-16, 92, 0, va("Race: %i", vspowerlevel[PWRLV_RACE]));
	V_DrawRightAlignedString(BASEVIDWIDTH-16, 102, 0, va("Battle: %i", vspowerlevel[PWRLV_BATTLE]));

	// Nothing else to draw
	if (kartstats.vanilla)
		return;

	DRAWTIMESTAT(122, "RA Play Time:", raplaytime);
	DRAWTIMESTAT(132, "Online Play Time:", onlineplaytime);
	DRAWTIMESTAT(142, "Race Play Time:", raceplaytime);
	DRAWTIMESTAT(152, "Battle Play Time:", battleplaytime);
}

// Note: only available with non-vanilla stats loaded, so it doesn't check for that
static void M_DrawStatsExtra(void)
{
	DRAWTIMESTAT(42, "Time being SPB target:", spbtargettime);
	DRAWTIMESTAT(52, "Time spent in spinout:", spinouttime);

	DRAWAMOUNTSTAT(72, "Total wins:", totalwins);
	DRAWAMOUNTSTAT(82, "Total podium (2nd/3rd place):", totalpodium);

	DRAWAMOUNTSTAT(102, "Hits landed:", hits);
	DRAWAMOUNTSTAT(112, "Self-hits landed:", selfhits);

	DRAWAMOUNTSTAT(132, "Sinks landed:", sinks);
	DRAWAMOUNTSTAT(142, "Times hit by sink:", sinked);

	DRAWAMOUNTSTAT(162, "Total respawns:", respawns);
}

#undef DRAWAMOUNTSTAT
#undef DRAWTIMESTAT

void M_DrawLevelStats(void)
{
	M_DrawMenuTitle();

	V_DrawCenteredString(BASEVIDWIDTH/2, 28, highlightflags, statsPages[statsCurrentPage].title);

	INT32 w = V_StringWidth(statsPages[statsCurrentPage].title, highlightflags);
	V_DrawCharacter(BASEVIDWIDTH/2 - w/2 - 10 - (skullAnimCounter/5), 28,
			'\x1C' | highlightflags, false); // left arrow
	V_DrawCharacter(BASEVIDWIDTH/2 + w/2 + 2 + (skullAnimCounter/5), 28,
			'\x1D' | highlightflags, false); // right arrow

	statsPages[statsCurrentPage].drawer();
}

// Handle statistics.
void M_HandleLevelStats(INT32 choice)
{
	boolean exitmenu = false; // exit to previous menu

	switch (choice)
	{
		case KEY_DOWNARROW:
			if (statsCurrentPage != 1) // Must be on level stats page
				break;
			S_StartSound(NULL, sfx_menu1);
			if (statsLocation < statsMax)
				++statsLocation;
			break;

		case KEY_UPARROW:
			if (statsCurrentPage != 1) // Must be on level stats page
				break;
			S_StartSound(NULL, sfx_menu1);
			if (statsLocation)
				--statsLocation;
			break;

		case KEY_RIGHTARROW:
			S_StartSound(NULL, sfx_menu1);
			statsCurrentPage++;
			if (statsCurrentPage >= NUMSTATSPAGES)
				statsCurrentPage = 0;
			break;

		case KEY_LEFTARROW:
			S_StartSound(NULL, sfx_menu1);
			if (statsCurrentPage == 0)
				statsCurrentPage = NUMSTATSPAGES-1;
			else
				--statsCurrentPage;
			break;

		case KEY_PGDN:
			if (statsCurrentPage != 1) // Must be on level stats page
				break;
			S_StartSound(NULL, sfx_menu1);
			statsLocation += (statsLocation+13 >= statsMax) ? statsMax-statsLocation : 13;
			break;

		case KEY_PGUP:
			if (statsCurrentPage != 1) // Must be on level stats page
				break;
			S_StartSound(NULL, sfx_menu1);
			statsLocation -= (statsLocation < 13) ? statsLocation : 13;
			break;

		case KEY_ESCAPE:
			exitmenu = true;
			break;
	}
	if (exitmenu)
		M_ExitMenu();
}

void M_GrandPrixTemp(INT32 choice)
{
	(void)choice;
	if (!M_PrepareCupList())
	{
		M_StartMessage(M_GetText("No cups found for Grand Prix.\n"),NULL,MM_NOTHING);
		return;
	}
	M_PatchSkinNameTable();
	M_EnterMenu(MN_SP_GRANDPRIX, true);
}

// Start Grand Prix!
void M_StartGrandPrix(INT32 choice)
{
	cupheader_t *gpcup = kartcupheaders;
	INT32 levelNum;

	(void)choice;

	if (gpcup == NULL)
	{
		// welp
		I_Error("No cup definitions for GP\n");
		return;
	}

	M_ClearMenus(true);

	memset(&grandprixinfo, 0, sizeof(struct grandprixinfo));

	switch (cv_dummygpdifficulty.value)
	{
		case KARTSPEED_EASY:
		case KARTSPEED_NORMAL:
		case KARTSPEED_HARD:
			grandprixinfo.gamespeed = cv_dummygpdifficulty.value;
			break;
		case KARTGP_MASTER:
			grandprixinfo.gamespeed = KARTSPEED_HARD;
			grandprixinfo.masterbots = true;
			break;
		default:
			CONS_Alert(CONS_WARNING, "Invalid GP difficulty\n");
			grandprixinfo.gamespeed = KARTSPEED_NORMAL;
			break;
	}

	grandprixinfo.encore = (boolean)(cv_dummygpencore.value);

	while (gpcup != NULL && gpcup->id != cv_dummygpcup.value-1)
	{
		gpcup = gpcup->next;
	}

	if (gpcup == NULL)
	{
		gpcup = kartcupheaders;
	}

	grandprixinfo.cup = gpcup;

	grandprixinfo.gp = true;
	grandprixinfo.roundnum = 1;
	grandprixinfo.wonround = false;

	grandprixinfo.initalize = true;

	levelNum = G_MapNumber(grandprixinfo.cup->levellist[0]);

	G_DeferedInitNew(
		false,
		levelNum + 1,
		(UINT8)(cv_chooseskin.value - 1),
		(UINT8)(cv_splitplayers.value - 1),
		false
	);
}

// ===========
// MODE ATTACK
// ===========

// draw stuff "in the background" for time attack
static void M_DrawTimeAttackBackground(menuitem_t *item)
{
	INT16 x = currentMenu->x;
	INT16 y = currentMenu->y+item->alphaKey;
	consvar_t *ncv = item->itemaction.cvar;
	V_DrawString(x, y, V_TRANSLUCENT, item->text);
	if (item->status & IT_CV_STRING)
	{
		M_DrawTextBox(x + 32, y - 8, MAXPLAYERNAME, 1);
		V_DrawString(x + 40, y, V_TRANSLUCENT|V_ALLOWLOWERCASE, ncv->string);
	}
	else
	{
		const char *str = ((ncv == &cv_chooseskin) ? skins[cv_chooseskin.value-1].realname : ncv->string);
		INT32 soffset = 40, strw = V_StringWidth(str, 0);

		// hack to keep the menu from overlapping the level icon
		if (ncv == &cv_nextmap)
			soffset = 0;

		// Should see nothing but strings
		V_DrawString(BASEVIDWIDTH - x - soffset - strw, y, highlightflags|V_TRANSLUCENT, str);
	}
}

// Drawing function for Time Attack
void M_DrawTimeAttackMenu(void)
{
	INT32 i, x, y, cursory = 0;
	UINT16 dispstatus;
	SINT8 preset = G_RecordPresetIndex();

	//S_ChangeMusicInternal("racent", true); // Eww, but needed for when user hits escape during demo playback

	M_DrawMenuTitle();
	if (menustack[0] == MN_SP_TIMEATTACK)
		M_DrawLevelSelectOnly(true, false);

	// draw menu (everything else goes on top of it)
	// Sadly we can't just use generic mode menus because we need some extra hacks
	x = currentMenu->x;
	y = currentMenu->y;

	// Character face!
	{
		UINT8 *colormap = R_GetTranslationColormap(cv_chooseskin.value-1, cv_playercolor[0].value, GTC_MENUCACHE);
		V_DrawMappedPatch(BASEVIDWIDTH-x - SHORT(faceprefix[cv_chooseskin.value-1][FACE_WANTED]->width), y, 0, faceprefix[cv_chooseskin.value-1][FACE_WANTED], colormap);
	}

	for (i = 0; i < currentMenu->numitems; ++i)
	{
		dispstatus = (currentMenu->menuitems[i].status & IT_DISPLAY);
		if (dispstatus != IT_STRING && dispstatus != IT_WHITESTRING)
			continue;

		y = currentMenu->y+currentMenu->menuitems[i].alphaKey;
		if (i == itemOn)
			cursory = y;

		V_DrawString(x, y, (dispstatus == IT_WHITESTRING) ? highlightflags : 0 , currentMenu->menuitems[i].text);

		// Cvar specific handling
		if ((currentMenu->menuitems[i].status & IT_TYPE) == IT_CVAR)
		{
			consvar_t *cv = currentMenu->menuitems[i].itemaction.cvar;
			if (currentMenu->menuitems[i].status & IT_CV_STRING)
			{
				M_DrawTextBox(x + 32, y - 8, MAXPLAYERNAME, 1);
				V_DrawString(x + 40, y, V_ALLOWLOWERCASE, cv->string);
				if (itemOn == i && skullAnimCounter < 4) // blink cursor
					V_DrawCharacter(x + 40 + V_StringWidth(cv->string, V_ALLOWLOWERCASE), y, '_',false);
			}
			else
			{
				const char *str = ((cv == &cv_chooseskin) ? skins[cv_chooseskin.value-1].realname : cv->string);
				INT32 soffset = 40, strw = V_StringWidth(str, 0);

				// hack to keep the menu from overlapping the level icon
				if (menustack[0] != MN_SP_TIMEATTACK || cv == &cv_nextmap)
					soffset = 0;

				// Should see nothing but strings
				V_DrawString(BASEVIDWIDTH - x - soffset - strw, y, highlightflags, str);

				if (i == itemOn)
				{
					V_DrawCharacter(BASEVIDWIDTH - x - soffset - 10 - strw - (skullAnimCounter/5), y,
						'\x1C' | highlightflags, false); // left arrow
					V_DrawCharacter(BASEVIDWIDTH - x - soffset + 2 + (skullAnimCounter/5), y,
						'\x1D' | highlightflags, false); // right arrow
				}
			}
		}
		else if ((currentMenu->menuitems[i].status & IT_TYPE) == IT_KEYHANDLER && cv_dummystaff.value) // bad hacky assumption: IT_KEYHANDLER is assumed to be staff ghost selector
		{
			INT32 strw = V_StringWidth(dummystaffname, V_ALLOWLOWERCASE);
			V_DrawString(BASEVIDWIDTH - x - strw, y, highlightflags|V_ALLOWLOWERCASE, dummystaffname);
			if (i == itemOn)
			{
				V_DrawCharacter(BASEVIDWIDTH - x - 10 - strw - (skullAnimCounter/5), y,
					'\x1C' | highlightflags, false); // left arrow
				V_DrawCharacter(BASEVIDWIDTH - x + 2 + (skullAnimCounter/5), y,
					'\x1D' | highlightflags, false); // right arrow
			}
		}
	}

	x = currentMenu->x;
	y = currentMenu->y;

	// DRAW THE SKULL CURSOR
	V_DrawScaledPatch(x - 24, cursory, 0, W_CachePatchName("M_CURSOR", PU_CACHE));
	V_DrawString(x, cursory, highlightflags, currentMenu->menuitems[itemOn].text);

	// Level record list
	if (cv_nextmap.value)
	{
		INT32 dupadjust = (vid.width/vid.dupx);
		tic_t lap = 0, time = 0;
		if (mapheaderinfo[cv_nextmap.value-1]->mainrecord[preset])
		{
			lap = mapheaderinfo[cv_nextmap.value-1]->mainrecord[preset]->lap;
			time = mapheaderinfo[cv_nextmap.value-1]->mainrecord[preset]->time;
		}

		V_DrawFill((BASEVIDWIDTH - dupadjust)>>1, 78, dupadjust, 36, 159);

		if (levellistmode != LLM_ITEMBREAKER)
		{
			V_DrawRightAlignedString(149, 80, highlightflags, "BEST LAP:");
			K_drawKartTimestamp(lap, 19, 86, -1, 2);
		}

		V_DrawRightAlignedString(292, 80, highlightflags, "BEST TIME:");
		K_drawKartTimestamp(time, 162, 86, cv_nextmap.value-1, 1);
	}

	// Draw current RA preset mode.
	{
		const char *mode = "Unkown mode";
		if (preset == 0)
		{
			mode = "SRB2Kart Mode";
		}
		else if (preset == 1)
		{
			mode = "Tech Mode";
		}
		else if (preset == 2)
		{
			mode = "BlanKart Mode";
		}
		else if (preset == 3)
		{
			mode = "Custom Mode";
		}

		V_DrawString(50, 104, V_6WIDTHSPACE, mode);
	}

	// ALWAYS DRAW player name, level name, skin and color even when not on this menu!
	if (menustack[0] != MN_SP_TIMEATTACK)
	{
		M_DrawTimeAttackBackground(M_GetMenuItem(MN_SP_TIMEATTACK, "NAME"));
		M_DrawTimeAttackBackground(M_GetMenuItem(MN_SP_TIMEATTACK, "SKIN"));
		M_DrawTimeAttackBackground(M_GetMenuItem(MN_SP_TIMEATTACK, "COLOR"));
		M_DrawTimeAttackBackground(M_GetMenuItem(MN_SP_TIMEATTACK, "LEVEL"));
	}
}

// Going to Time Attack menu...
void M_TimeAttack(INT32 choice)
{
	(void)choice;

	memset(skins_cons_t, 0, sizeof (skins_cons_t));

	levellistmode = LLM_TIMEATTACK; // Don't be dependent on cv_newgametype

	if (M_CountLevelsToShowInList() == 0)
	{
		M_StartMessage(M_GetText("No levels found for Time Attack.\n"),NULL,MM_NOTHING);
		return;
	}

	M_PatchSkinNameTable();

	M_PrepareLevelSelect();
	M_ClearMenus(true);
	M_EnterMenu(MN_SP_TIMEATTACK, true);

	G_SetGamestate(GS_TIMEATTACK);
	titlemapinaction = TITLEMAP_OFF; // Nope don't give us HOMs please

	if (cv_nextmap.value)
		Nextmap_OnChange();
	else
		CV_AddValue(&cv_nextmap, 1);

	M_SetItemOn(MN_SP_TIMEATTACK, "START"); // "Start" is selected.

	S_ChangeMusicInternal("racent", true);
}

// Same as above, but sets a different levellistmode. Should probably be merged...
void M_ItemBreaker(INT32 choice)
{
	(void)choice;

	memset(skins_cons_t, 0, sizeof (skins_cons_t));

	levellistmode = LLM_ITEMBREAKER; // Don't be dependent on cv_newgametype

	if (M_CountLevelsToShowInList() == 0)
	{
		M_StartMessage(M_GetText("No levels found for Item Breaker.\n"),NULL,MM_NOTHING);
		return;
	}

	M_PatchSkinNameTable();

	M_PrepareLevelSelect();
	M_ClearMenus(true);
	M_EnterMenu(MN_SP_TIMEATTACK, true);

	G_SetGamestate(GS_TIMEATTACK);
	titlemapinaction = TITLEMAP_OFF; // Nope don't give us HOMs please

	if (cv_nextmap.value)
		Nextmap_OnChange();
	else
		CV_AddValue(&cv_nextmap, 1);

	M_SetItemOn(MN_SP_TIMEATTACK, "START"); // "Start" is selected.

	S_ChangeMusicInternal("racent", true);
}

void M_QuitTimeAttackMenu(INT32 choice)
{
	(void)choice;

	// you know what? always putting these in the buffer won't hurt anything.
	COM_BufAddText(va("skin \"%s\"\n", cv_chooseskin.string));
}

// Player has selected the "START" from the time attack screen
void M_ChooseTimeAttack(INT32 choice)
{
	char *gpath;
	char *gamemode = M_AppendGametypeAndModName();
	char nameofdemo[256];
	(void)choice;
	emeralds = 0;
	M_ClearMenus(true);
	modeattacking = (levellistmode == LLM_ITEMBREAKER ? ATTACKING_ITEMBREAK : ATTACKING_TIME);

	gpath = va("%s"PATHSEP"media"PATHSEP"replay"PATHSEP"%s",
			   srb2home, timeattackfolder);
	M_MkdirEach(gpath, M_PathParts(gpath) - 3, 0755);

	strcat(gpath, PATHSEP);
	strcat(gpath, G_BuildMapName(cv_nextmap.value));

	snprintf(nameofdemo, sizeof nameofdemo, "%s-%s-%s-last", gpath, cv_skin[0].string, gamemode);

	Z_Free(gamemode);

	if (!cv_autorecord.value)
		remove(va("%s"PATHSEP"%s.lmp", srb2home, nameofdemo));
	else
		G_RecordDemo(nameofdemo);

	G_DeferedInitNew(false, cv_nextmap.value, (UINT8)(cv_chooseskin.value-1), 0, false);
}

void M_HandleStaffReplay(INT32 choice)
{
	boolean exitmenu = false; // exit to previous menu
	lumpnum_t l = W_CheckNumForName(va("%sS%02u",G_BuildMapName(cv_nextmap.value),cv_dummystaff.value));

	switch (choice)
	{
		case KEY_DOWNARROW:
			M_NextOpt();
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_UPARROW:
			M_PrevOpt();
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_BACKSPACE:
		case KEY_ESCAPE:
			exitmenu = true;
			break;
		case KEY_RIGHTARROW:
			CV_AddValue(&cv_dummystaff, 1);
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_LEFTARROW:
			CV_AddValue(&cv_dummystaff, -1);
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_ENTER:
			if (l == LUMPERROR)
				break;
			M_ClearMenus(true);
			demo.loadfiles = false; demo.ignorefiles = true; // Just assume that record attack replays have the files needed
			G_DoPlayDemo(va("%sS%02u",G_BuildMapName(cv_nextmap.value),cv_dummystaff.value));
			break;
		default:
			break;
	}
	if (exitmenu)
		M_ExitMenu();
}

void M_SRB2KartPreset(INT32 choice)
{
	(void)choice;

	CV_Set(&cv_dummyattackingrings, "Off");
	CV_Set(&cv_dummyattackingstacking, "Off");
	CV_Set(&cv_dummyattackingchaining, "Off");
	CV_Set(&cv_dummyattackingslipdash, "Off");
	CV_Set(&cv_dummyattackingpurpledrift, "Off");
}

void M_TechPreset(INT32 choice)
{
	(void)choice;

	CV_Set(&cv_dummyattackingrings, "Off");
	CV_Set(&cv_dummyattackingstacking, "On");
	CV_Set(&cv_dummyattackingchaining, "On");
	CV_Set(&cv_dummyattackingslipdash,"Off");
	CV_Set(&cv_dummyattackingpurpledrift, "Off");
}

void M_BlanKartPreset(INT32 choice)
{
	(void)choice;

	CV_Set(&cv_dummyattackingrings, "On");
	CV_Set(&cv_dummyattackingstacking, "On");
	CV_Set(&cv_dummyattackingchaining, "On");
	CV_Set(&cv_dummyattackingslipdash, "On");
	CV_Set(&cv_dummyattackingpurpledrift, "On");
}

// Player has selected the "REPLAY" from the time attack screen
void M_ReplayTimeAttack(INT32 choice)
{
	const char *which;
	char *gamemode = M_AppendGametypeAndModName();
	M_ClearMenus(true);
	demo.loadfiles = false; demo.ignorefiles = true; // Just assume that record attack replays have the files needed

	switch(choice) {
	default:
	case 0: // best time
		which = "time-best";
		break;
	case 1: // best lap
		which = "lap-best";
		break;
	case 2: // last
		which = "last";
		break;
	case 3: // guest
		// srb2/replay/main/map01-guest.lmp
		G_DoPlayDemo(va("%s"PATHSEP"media"PATHSEP"replay"PATHSEP"%s"PATHSEP"%s-%s-guest.lmp", srb2home, timeattackfolder, G_BuildMapName(cv_nextmap.value), gamemode));
		return;
	}
	// srb2/replay/main/map01-sonic-time-best.lmp
	G_DoPlayDemo(va("%s"PATHSEP"media"PATHSEP"replay"PATHSEP"%s"PATHSEP"%s-%s-%s-%s.lmp", srb2home, timeattackfolder, G_BuildMapName(cv_nextmap.value), cv_chooseskin.string, gamemode, which));
	Z_Free(gamemode);
}

static void M_EraseGuest(INT32 choice)
{
	char *gamemode = M_AppendGametypeAndModName();
	const char *rguest = va("%s"PATHSEP"media"PATHSEP"replay"PATHSEP"%s"PATHSEP"%s-%s-guest.lmp", srb2home, timeattackfolder, G_BuildMapName(cv_nextmap.value), gamemode);
	(void)choice;
	if (FIL_FileExists(rguest))
		remove(rguest);
	M_ExitMenu();
	CV_AddValue(&cv_nextmap, -1);
	CV_AddValue(&cv_nextmap, 1);
	M_StartMessage(M_GetText("Guest replay data erased.\n"),NULL,MM_NOTHING);
	Z_Free(gamemode);
}

static void M_OverwriteGuest(const char *which)
{
	char *gamemode = M_AppendGametypeAndModName();
	char *rguest = Z_StrDup(va("%s"PATHSEP"media"PATHSEP"replay"PATHSEP"%s"PATHSEP"%s-%s-guest.lmp", srb2home, timeattackfolder, G_BuildMapName(cv_nextmap.value), gamemode));
	UINT8 *buf;
	size_t len;
	len = FIL_ReadFile(va("%s"PATHSEP"media"PATHSEP"replay"PATHSEP"%s"PATHSEP"%s-%s-%s-%s.lmp", srb2home, timeattackfolder, G_BuildMapName(cv_nextmap.value), cv_chooseskin.string, gamemode,which), &buf);
	if (!len) {
		return;
	}
	if (FIL_FileExists(rguest)) {
		messagebox.active = false;
		remove(rguest);
	}
	FIL_WriteFile(rguest, buf, len);
	Z_Free(rguest);
	M_ExitMenu();
	CV_AddValue(&cv_nextmap, -1);
	CV_AddValue(&cv_nextmap, 1);
	M_StartMessage(M_GetText("Guest replay data saved.\n"),NULL,MM_NOTHING);
	Z_Free(gamemode);
}

static void M_OverwriteGuest_Time(INT32 choice)
{
	(void)choice;
	M_OverwriteGuest("time-best");
}

static void M_OverwriteGuest_Lap(INT32 choice)
{
	(void)choice;
	M_OverwriteGuest("lap-best");
}

static void M_OverwriteGuest_Last(INT32 choice)
{
	(void)choice;
	M_OverwriteGuest("last");
}

void M_SetGuestReplay(INT32 choice)
{
	void (*which)(INT32);
	switch(choice)
	{
	case 0: // best time
		which = M_OverwriteGuest_Time;
		break;
	case 1: // best lap
		which = M_OverwriteGuest_Lap;
		break;
	case 2: // last
		which = M_OverwriteGuest_Last;
		break;
	case 3: // guest
	default:
		M_StartMessage(M_GetText("Are you sure you want to\ndelete the guest replay data?\n\n(Press 'Y' to confirm)\n"), M_EraseGuest, MM_YESNO);
		return;
	}
	if (FIL_FileExists(va("%s"PATHSEP"media"PATHSEP"replay"PATHSEP"%s"PATHSEP"%s-guest.lmp", srb2home, timeattackfolder, G_BuildMapName(cv_nextmap.value))))
		M_StartMessage(M_GetText("Are you sure you want to\noverwrite the guest replay data?\n\n(Press 'Y' to confirm)\n"), which, MM_YESNO);
	else
		which(0);
}

void M_ModeAttackRetry(INT32 choice)
{
	(void)choice;
	G_CheckDemoStatus(); // Cancel recording
	if (modeattacking)
		M_ChooseTimeAttack(0);
}

void M_ModeAttackEndGame(INT32 choice)
{
	(void)choice;
	G_CheckDemoStatus(); // Cancel recording

	if (gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_VOTING)
		Command_ExitGame_f();

	M_ClearMenus(true);
	M_EnterMenu(MN_SP_TIMEATTACK, true);

	G_SetGamestate(GS_TIMEATTACK);
	modeattacking = ATTACKING_NONE;
	S_ChangeMusicInternal("racent", true);
	// Update replay availability.
	CV_AddValue(&cv_nextmap, 1);
	CV_AddValue(&cv_nextmap, -1);
}

// ========
// END GAME
// ========

static void M_ExitGameResponse(INT32 ch)
{
	if (ch != 'y' && ch != KEY_ENTER)
		return;

	//Command_ExitGame_f();
	G_SetExitGameFlag();
	M_ClearMenus(true);
}

void M_EndGame(INT32 choice)
{
	(void)choice;
	if (demo.playback)
		return;

	if (!Playing())
		return;

	M_StartMessage(M_GetText("Are you sure you want to end the game?\n\n(Press 'Y' to confirm)\n"), M_ExitGameResponse, MM_YESNO);
}

//===========================================================================
// Connect Menu
//===========================================================================

void
M_SetWaitingMode (int mode)
{
#ifdef HAVE_THREADS
	I_lock_mutex(&m_menu_mutex);
#endif
	{
		m_waiting_mode = mode;
	}
#ifdef HAVE_THREADS
	I_unlock_mutex(m_menu_mutex);
#endif
}

int
M_GetWaitingMode (void)
{
	int mode;

#ifdef HAVE_THREADS
	I_lock_mutex(&m_menu_mutex);
#endif
	{
		mode = m_waiting_mode;
	}
#ifdef HAVE_THREADS
	I_unlock_mutex(m_menu_mutex);
#endif

	return mode;
}

#ifdef MASTERSERVER
#ifdef HAVE_THREADS
static void
Spawn_masterserver_thread (const char *name, void (*thread)(int*))
{
	int *id = malloc(sizeof *id);

	I_lock_mutex(&ms_QueryId_mutex);
	{
		*id = ms_QueryId;
	}
	I_unlock_mutex(ms_QueryId_mutex);

	I_spawn_thread(name, (I_thread_fn)thread, id);
}

static int
Same_instance (int id)
{
	int okay;

	I_lock_mutex(&ms_QueryId_mutex);
	{
		okay = ( id == ms_QueryId );
	}
	I_unlock_mutex(ms_QueryId_mutex);

	return okay;
}
#endif/*HAVE_THREADS*/

static void
Fetch_servers_thread (int *id)
{
	msg_server_t * server_list;

	(void)id;

	M_SetWaitingMode(M_WAITING_SERVERS);

#ifdef HAVE_THREADS
	server_list = GetShortServersList(*id);
#else
	server_list = GetShortServersList(0);
#endif

	if (server_list)
	{
#ifdef HAVE_THREADS
		if (Same_instance(*id))
#endif
		{
			M_SetWaitingMode(M_NOT_WAITING);

#ifdef HAVE_THREADS
			I_lock_mutex(&ms_ServerList_mutex);
			{
				ms_ServerList = server_list;
			}
			I_unlock_mutex(ms_ServerList_mutex);
#else
			CL_QueryServerList(server_list);
			free(server_list);
#endif
		}
#ifdef HAVE_THREADS
		else
		{
			free(server_list);
		}
#endif
	}

#ifdef HAVE_THREADS
	free(id);
#endif
}
#endif/*MASTERSERVER*/

#define SERVERHEADERHEIGHT 36
#define SERVERLINEHEIGHT 12

#define S_LINEY(n) currentMenu->y + SERVERHEADERHEIGHT + (n * SERVERLINEHEIGHT)

void M_HandleServerPage(INT32 choice)
{
	boolean exitmenu = false; // exit to previous menu

	switch (choice)
	{
		case KEY_DOWNARROW:
			M_NextOpt();
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_UPARROW:
			M_PrevOpt();
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_BACKSPACE:
		case KEY_ESCAPE:
			exitmenu = true;
			break;

		case KEY_ENTER:
		case KEY_RIGHTARROW:
			S_StartSound(NULL, sfx_menu1);
			if ((serverlistpage + 1) * M_ServersPerPage() < serverlistcount)
				serverlistpage++;
			break;
		case KEY_LEFTARROW:
			S_StartSound(NULL, sfx_menu1);
			if (serverlistpage > 0)
				serverlistpage--;
			break;

		default:
			break;
	}
	if (exitmenu)
		M_ExitMenu();
}

void M_Connect(INT32 choice)
{
	// do not call menuexitfunc
	M_ClearMenus(false);

	INT16 firstserverline = M_GetMenuIndex(MN_MP_CONNECT, "FIRLIN");
	COM_BufAddText(va("connect node %d\n", serverlist[choice-firstserverline + serverlistpage * M_ServersPerPage()].node));
}

void M_Refresh(INT32 choice)
{
	(void)choice;

	// Display a little "please wait" message.
	M_DrawTextBox(52, BASEVIDHEIGHT/2-10, 25, 3);
	V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT/2, 0, "Searching for servers...");
	V_DrawCenteredString(BASEVIDWIDTH/2, (BASEVIDHEIGHT/2)+12, 0, "Please wait.");
	I_OsPolling();
	I_UpdateNoBlit();
	if (rendermode == render_soft)
		I_FinishUpdate(); // page flip or blit buffer

	// first page of servers
	serverlistpage = 0;

#ifdef MASTERSERVER
#ifdef HAVE_THREADS
	Spawn_masterserver_thread("fetch-servers", Fetch_servers_thread);
#else/*HAVE_THREADS*/
	Fetch_servers_thread(NULL);
#endif/*HAVE_THREADS*/
#else/*MASTERSERVER*/
	CL_UpdateServerList();
#endif/*MASTERSERVER*/
}

void M_DrawConnectMenu(void)
{
	UINT16 i;
	//const char *gt = "Unknown";
	//const char *spd = "";
	const char *pwr = "----";
	INT16 firstserverline = M_GetMenuIndex(MN_MP_CONNECT, "FIRLIN");
	UINT32 serversperpage = M_ServersPerPage(); // server sperpage?
	INT32 numPages = (serverlistcount+(serversperpage-1))/serversperpage;
	int waiting;
	menu_t *conmenu = &menudefs[MN_MP_CONNECT]; // meh, whatever

	for (i = firstserverline; i < firstserverline+serversperpage; i++)
		conmenu->menuitems[i].status = IT_STRING | IT_SPACE;

	if (!numPages)
		numPages = 1;

	// Page num
	V_DrawRightAlignedString(BASEVIDWIDTH - currentMenu->x, currentMenu->y + M_GetItemY(MN_MP_CONNECT, "PAGE"),
	                         highlightflags, va("%u of %d", serverlistpage+1, numPages));

	// Horizontal line!
	V_DrawFill(1, currentMenu->y+32, 318, 1, 0);

	if (serverlistcount <= 0)
		V_DrawString(currentMenu->x,currentMenu->y+SERVERHEADERHEIGHT, 0, "No servers found");
	else
	for (i = 0; i < min(serverlistcount - serverlistpage * serversperpage, serversperpage); i++)
	{
		INT32 slindex = i + serverlistpage * serversperpage;
		UINT32 globalflags = ((serverlist[slindex].info.numberofplayer >= serverlist[slindex].info.maxplayer) ? V_TRANSLUCENT : 0)
			|((itemOn == firstserverline+i) ? highlightflags : 0)|V_ALLOWLOWERCASE;

		V_DrawString(currentMenu->x, S_LINEY(i), globalflags, serverlist[slindex].info.servername);

		V_DrawSmallString(currentMenu->x, S_LINEY(i)+8, globalflags,
		                     va("Ping: %u", (UINT32)LONG(serverlist[slindex].info.time)));

		V_DrawSmallString(currentMenu->x+44,S_LINEY(i)+8, globalflags,
		                         va("Players: %02d/%02d", serverlist[slindex].info.numberofplayer, serverlist[slindex].info.maxplayer));

		V_DrawSmallString(currentMenu->x+108, S_LINEY(i)+8, globalflags, serverlist[slindex].info.gametypename);

		// display game speed for race gametypes
		/* todo: send if the gametype is GTR_CIRCUIT, and uses game speed
		if (serverlist[slindex].info.gametype == GT_RACE)
		{
			spd = kartspeed_cons_t[(serverlist[slindex].info.kartvars & SV_SPEEDMASK)+1].strvalue;
			V_DrawSmallString(currentMenu->x+128, S_LINEY(i)+8, globalflags, va("(%s)", spd));
		}
		*/

		pwr = "----";
		if (serverlist[slindex].info.avgpwrlv == -1)
			pwr = "Off";
		else if (serverlist[slindex].info.avgpwrlv > 0)
			pwr = va("%04d", serverlist[slindex].info.avgpwrlv);
		V_DrawSmallString(currentMenu->x+171, S_LINEY(i)+8, globalflags, va("Power Level: %s", pwr));

		// Don't use color flags intentionally, the global yellow color will auto override the text color code
		if (serverlist[slindex].info.modifiedgame)
			V_DrawSmallString(currentMenu->x+245, S_LINEY(i)+8, globalflags, "\x85" "Mod");
		if (serverlist[slindex].info.cheatsenabled)
			V_DrawSmallString(currentMenu->x+265, S_LINEY(i)+8, globalflags, "\x83" "Cheats");

		conmenu->menuitems[i+firstserverline].status = IT_STRING | IT_CALL;
	}

	M_DrawGenericMenu();

	waiting = M_GetWaitingMode();

	if (waiting)
	{
		const char *message;

		if (waiting == M_WAITING_VERSION)
			message = "Checking for updates...";
		else
			message = "Searching for servers...";

		// Display a little "please wait" message.
		M_DrawTextBox(52, BASEVIDHEIGHT/2-10, 25, 3);
		V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT/2, 0, message);
		V_DrawCenteredString(BASEVIDWIDTH/2, (BASEVIDHEIGHT/2)+12, 0, "Please wait.");
	}
}

void M_CancelConnect(INT32 choice)
{
	(void)choice;
	D_CloseConnection();
}

// Ascending order, not descending.
// The casts are safe as long as the caller doesn't do anything stupid.
#define SERVER_LIST_ENTRY_COMPARATOR(key) \
static int ServerListEntryComparator_##key(const void *entry1, const void *entry2) \
{ \
	const serverelem_t *sa = (const serverelem_t*)entry1, *sb = (const serverelem_t*)entry2; \
	if (sa->info.key != sb->info.key) \
		return sa->info.key - sb->info.key; \
	return strcmp(sa->info.servername, sb->info.servername); \
}

// This does descending instead of ascending.
#define SERVER_LIST_ENTRY_COMPARATOR_REVERSE(key) \
static int ServerListEntryComparator_##key##_reverse(const void *entry1, const void *entry2) \
{ \
	const serverelem_t *sa = (const serverelem_t*)entry1, *sb = (const serverelem_t*)entry2; \
	if (sb->info.key != sa->info.key) \
		return sb->info.key - sa->info.key; \
	return strcmp(sb->info.servername, sa->info.servername); \
}

SERVER_LIST_ENTRY_COMPARATOR(time)
SERVER_LIST_ENTRY_COMPARATOR(numberofplayer)
SERVER_LIST_ENTRY_COMPARATOR_REVERSE(numberofplayer)
SERVER_LIST_ENTRY_COMPARATOR_REVERSE(maxplayer)

static int ServerListEntryComparator_gametypename(const void *entry1, const void *entry2)
{
	const serverelem_t *sa = (const serverelem_t*)entry1, *sb = (const serverelem_t*)entry2;
	int c;
	if (( c = strcasecmp(sa->info.gametypename, sb->info.gametypename) ))
		return c;
	return strcmp(sa->info.servername, sb->info.servername); \
}

// Special one for modified state.
static int ServerListEntryComparator_modified(const void *entry1, const void *entry2)
{
	const serverelem_t *sa = (const serverelem_t*)entry1, *sb = (const serverelem_t*)entry2;

	// Modified acts as 2 points, cheats act as one point.
	int modstate_a = (sa->info.cheatsenabled ? 1 : 0) | (sa->info.modifiedgame ? 2 : 0);
	int modstate_b = (sb->info.cheatsenabled ? 1 : 0) | (sb->info.modifiedgame ? 2 : 0);

	if (modstate_a != modstate_b)
		return modstate_a - modstate_b;

	// Default to strcmp.
	return strcmp(sa->info.servername, sb->info.servername);
}

void M_SortServerList(void)
{
	switch(cv_serversort.value)
	{
	case 0:		// Ping.
		qs22j(serverlist, serverlistcount, sizeof(serverelem_t), ServerListEntryComparator_time);
		break;
	case 1:		// Modified state.
		qs22j(serverlist, serverlistcount, sizeof(serverelem_t), ServerListEntryComparator_modified);
		break;
	case 2:		// Most players.
		qs22j(serverlist, serverlistcount, sizeof(serverelem_t), ServerListEntryComparator_numberofplayer_reverse);
		break;
	case 3:		// Least players.
		qs22j(serverlist, serverlistcount, sizeof(serverelem_t), ServerListEntryComparator_numberofplayer);
		break;
	case 4:		// Max players.
		qs22j(serverlist, serverlistcount, sizeof(serverelem_t), ServerListEntryComparator_maxplayer_reverse);
		break;
	case 5:		// Gametype.
		qs22j(serverlist, serverlistcount, sizeof(serverelem_t), ServerListEntryComparator_gametypename);
		break;
	}
}

#ifdef UPDATE_ALERT
static void M_CheckMODVersion(int id)
{
	char updatestring[500];
	const char *updatecheck = GetMODVersion(id);
	if(updatecheck)
	{
		sprintf(updatestring, UPDATE_ALERT_STRING, VERSIONSTRING, updatecheck);
#ifdef HAVE_THREADS
		I_lock_mutex(&m_menu_mutex);
#endif
		M_StartMessage(updatestring, NULL, MM_NOTHING);
#ifdef HAVE_THREADS
		I_unlock_mutex(m_menu_mutex);
#endif
	}
}
#endif/*UPDATE_ALERT*/

#if defined (UPDATE_ALERT) && defined (HAVE_THREADS)
static void
Check_new_version_thread (int *id)
{
	M_SetWaitingMode(M_WAITING_VERSION);

	M_CheckMODVersion(*id);

	if (Same_instance(*id))
	{
		Fetch_servers_thread(id);
	}
	else
	{
		free(id);
	}
}
#endif/*defined (UPDATE_ALERT) && defined (HAVE_THREADS)*/

static void M_ConnectMenu(INT32 choice)
{
	(void)choice;
	// modified game check: no longer handled
	// we don't request a restart unless the filelist differs

	// first page of servers
	serverlistpage = 0;
	M_EnterMenu(MN_MP_CONNECT, true);
	itemOn = 0;

#if defined (MASTERSERVER) && defined (HAVE_THREADS)
	I_lock_mutex(&ms_QueryId_mutex);
	{
		ms_QueryId++;
	}
	I_unlock_mutex(ms_QueryId_mutex);

	I_lock_mutex(&ms_ServerList_mutex);
	{
		if (ms_ServerList)
		{
			free(ms_ServerList);
			ms_ServerList = NULL;
		}
	}
	I_unlock_mutex(ms_ServerList_mutex);

#ifdef UPDATE_ALERT
	Spawn_masterserver_thread("check-new-version", Check_new_version_thread);
#else/*UPDATE_ALERT*/
	Spawn_masterserver_thread("fetch-servers", Fetch_servers_thread);
#endif/*UPDATE_ALERT*/
#else/*defined (MASTERSERVER) && defined (HAVE_THREADS)*/
#ifdef UPDATE_ALERT
	M_CheckMODVersion(0);
#endif/*UPDATE_ALERT*/
	M_Refresh(0);
#endif/*defined (MASTERSERVER) && defined (HAVE_THREADS)*/
}

void M_ConnectMenuModChecks(INT32 choice)
{
	(void)choice;
	// okay never mind we want to COMMUNICATE to the player pre-emptively instead of letting them try and then get confused when it doesn't work

	if (modifiedgame)
	{
		M_StartMessage(M_GetText("You have addons loaded.\nYou won't be able to join netgames!\n\nTo play online, restart the game\nand don't load any addons.\nSRB2Kart will automatically add\neverything you need when you join.\n\n(Press a key)\n"), M_ConnectMenu, MM_EVENTHANDLER);
		return;
	}

	M_ConnectMenu(-1);
}

//===========================================================================
// Start Server Menu
//===========================================================================

//
// FindFirstMap
//
// Finds the first map of a particular gametype (or returns the current map)
// Defaults to 1 if nothing found.
//
static INT32 M_FindFirstMap(INT32 gtype)
{
	INT32 i;

	if (mapheaderinfo[gamemap] && (mapheaderinfo[gamemap]->typeoflevel & gametypetol[gtype]))
		return gamemap;

	for (i = 0; i < nummapheaders; i++)
	{
		if (!mapheaderinfo[i])
			continue;
		if (!(mapheaderinfo[i]->typeoflevel & gametypetol[gtype]))
			continue;
		return i + 1;
	}

	return 1;
}

void M_StartServer(INT32 choice)
{
	UINT8 ssplayers = cv_splitplayers.value-1;

	(void)choice;

	if (menustack[0] == MN_MP_SPLITSCREEN)
		netgame = false;
	else
		netgame = true;

	multiplayer = true;

	strncpy(connectedservername, cv_servername.string, MAXSERVERNAME);

	// Still need to reset devmode
	cht_debug = 0;

	if (demo.playback)
		G_StopDemo();
	if (metalrecording)
		G_StopMetalDemo();

	if (!cv_nextmap.value)
		CV_SetValue(&cv_nextmap, G_RandMap(G_TOLFlag(cv_newgametype.value), -1, 0, 0, false, NULL)+1);

	if (cv_maxplayers.value < ssplayers+1)
		CV_SetValue(&cv_maxplayers, ssplayers+1);

	if (splitscreen != ssplayers)
	{
		splitscreen = ssplayers;
		SplitScreen_OnChange();
	}

	if (menustack[0] == MN_MP_SPLITSCREEN) // offline server
	{
		paused = false;
		SV_StartSinglePlayerServer();
		multiplayer = true; // yeah, SV_StartSinglePlayerServer clobbers this...
		D_MapChange(cv_nextmap.value, cv_newgametype.value, (cv_kartencore.value == 1), 1, 1, false, false);
	}
	else
	{
		D_MapChange(cv_nextmap.value, cv_newgametype.value, (cv_kartencore.value == 1), 1, 1, false, false);
		COM_BufAddText("dummyconsvar 1\n");
	}

	M_ClearMenus(true);
}

static void M_DrawLevelSelectOnly(boolean leftfade, boolean rightfade)
{
	patch_t *PictureOfLevel;
	INT32 x, y, w, i, oldval, trans, dupadjust = ((vid.width/vid.dupx) - BASEVIDWIDTH)>>1;
	fixed_t scale = M_GetMapThumbnail(cv_nextmap.value-1, &PictureOfLevel)/4;

	w = FixedMul(SHORT(PictureOfLevel->width), scale);
	i = FixedMul(SHORT(PictureOfLevel->height), scale);
	x = BASEVIDWIDTH/2 - w/2;
	y = currentMenu->y + 130 + 8 - i;

	if (currentMenu->menuitems[itemOn].itemaction.cvar == &cv_nextmap && skullAnimCounter < 4)
		trans = 0;
	else
		trans = G_GetGametypeColor(cv_newgametype.value);

	V_DrawFill(x-1, y-1, w+2, i+2, trans); // variable reuse...

	if ((cv_kartencore.value != 1) || gamestate == GS_TIMEATTACK || cv_newgametype.value != GT_RACE)
		V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, scale, 0, PictureOfLevel, NULL);
	else
	{
		/*UINT8 *mappingforencore = NULL;
		if ((lumpnum = W_CheckNumForName(va("%sE", mapname))) != LUMPERROR)
			mappingforencore = W_CachePatchNum(lumpnum, PU_CACHE);*/

		V_DrawFixedPatch((x+w)<<FRACBITS, y<<FRACBITS, scale, V_FLIP, PictureOfLevel, NULL);

		{
			static angle_t rubyfloattime = 0;
			const fixed_t rubyheight = FINESINE(rubyfloattime>>ANGLETOFINESHIFT);
			V_DrawFixedPatch((x+w/2)<<FRACBITS, ((y+i/2)<<FRACBITS) - (rubyheight<<1), FRACUNIT, 0, W_CachePatchName("RUBYICON", PU_CACHE), NULL);
			rubyfloattime += FixedMul(ANGLE_MAX/NEWTICRATE, renderdeltatics);
		}
	}
	/*V_DrawDiag(x, y, 12, 31);
	V_DrawDiag(x, y, 10, G_GetGametypeColor(cv_newgametype.value));*/

	y += i/4;
	i = cv_nextmap.value - 1;
	trans = (leftfade ? V_TRANSLUCENT : 0);

#define horizspac 2
	do
	{
		oldval = i;
		do
		{
			i--;
			if (i == -2)
				i = nummapheaders-1;

			if (i == oldval)
				return;

			if(!mapheaderinfo[i])
				continue; // Don't allocate the header.  That just makes memory usage skyrocket.

		} while (!M_CanShowLevelInList(i, cv_newgametype.value));

		scale = M_GetMapThumbnail(i, &PictureOfLevel)/8;

		x -= horizspac + w/2;

		V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, scale, trans, PictureOfLevel, NULL);
	} while (x > horizspac-dupadjust);

	x = (BASEVIDWIDTH + w)/2 + horizspac;
	i = cv_nextmap.value - 1;
	trans = (rightfade ? V_TRANSLUCENT : 0);

	while (x < BASEVIDWIDTH+dupadjust-horizspac)
	{
		oldval = i;
		do
		{
			i++;
			if (i == nummapheaders)
				i = -1;

			if (i == oldval)
				return;

			if(!mapheaderinfo[i])
				continue; // Don't allocate the header.  That just makes memory usage skyrocket.

		} while (!M_CanShowLevelInList(i, cv_newgametype.value));

		scale = M_GetMapThumbnail(i, &PictureOfLevel)/8;

		V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, scale, trans, PictureOfLevel, NULL);

		x += horizspac + w/2;
	}
#undef horizspac
}

void M_DrawServerMenu(void)
{
	M_DrawLevelSelectOnly(false, false);
	M_DrawGenericMenu();
}

void M_MapChange(INT32 choice)
{
	(void)choice;

	levellistmode = LLM_CREATESERVER;

	CV_SetValue(&cv_newgametype, gametype);
	CV_SetValue(&cv_nextmap, gamemap);

	M_PrepareLevelSelect();
	M_EnterMenu(MN_CHANGELEVEL, true);
}

void M_StartOfflineServerMenu(INT32 choice)
{
	(void)choice;
	levellistmode = LLM_CREATESERVER;
	M_PrepareLevelSelect();
	M_EnterMenu(MN_MP_SPLITSCREEN, true);
}

void M_StartServerMenu(INT32 choice)
{
	(void)choice;
	levellistmode = LLM_CREATESERVER;
	M_PrepareLevelSelect();
	M_EnterMenu(MN_MP_SERVER, true);
}

// ==============
// CONNECT VIA IP
// ==============

static char setupm_ip[28];
static UINT8 setupm_pselect = 1;

// Draw the funky Connect IP menu. Tails 11-19-2002
// So much work for such a little thing!
void M_DrawMPMainMenu(void)
{
	INT32 x = currentMenu->x;
	INT32 y = currentMenu->y;

	// use generic drawer for cursor, items and title
	M_DrawGenericMenu();

#if MAXPLAYERS != 16
Update the maxplayers label...
#endif
	V_DrawRightAlignedString(BASEVIDWIDTH-x, y+M_GetItemY(MN_MP_MAIN, "STASRV"),
		(M_IsItemOn(MN_MP_MAIN, "STASRV") ? highlightflags : 0), "(2-16 players)");

	V_DrawRightAlignedString(BASEVIDWIDTH-x, y+M_GetItemY(MN_MP_MAIN, "OFLSRV"),
		(M_IsItemOn(MN_MP_MAIN, "OFLSRV") ? highlightflags : 0),
		"(2-4 players)"
		);

	y += M_GetItemY(MN_MP_MAIN, "CONIP");

	V_DrawFill(x+5, y+4+5, /*16*8 + 6,*/ BASEVIDWIDTH - 2*(x+5), 8+6, 159);

	// draw name string
	V_DrawString(x+8,y+12, V_ALLOWLOWERCASE, setupm_ip);

	// draw text cursor for name
	if (M_IsItemOn(MN_MP_MAIN, "CONIP")
	    && skullAnimCounter < 4)   //blink cursor
		V_DrawCharacter(x+8+V_StringWidth(setupm_ip, V_ALLOWLOWERCASE),y+12,'_',false);

	// character bar, ripped off the color bar :V
	{
#define iconwidth 32
#define spacingwidth 32
#define incrwidth (iconwidth + spacingwidth)
		UINT8 i = 0, pskin, pcol;
		// player arrangement width, but there's also a chance i'm a furry, shhhhhh
		const INT32 paw = iconwidth + 3*incrwidth;
		INT32 trans = 0;
		UINT8 *colmap;
		x = BASEVIDWIDTH/2 - paw/2;
		y = currentMenu->y + 32;

		while (++i <= 4)
		{
			pskin = R_SkinAvailable(cv_skin[i-1].string);
			pcol = cv_playercolor[i-1].value;

			if (pskin >= MAXSKINS)
				pskin = 0;

			if (!trans && i > cv_splitplayers.value)
				trans = V_TRANSLUCENT;

			colmap = R_GetTranslationColormap(pskin, pcol, GTC_MENUCACHE);

			V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, FRACUNIT, trans, faceprefix[pskin][FACE_WANTED], colmap);

			if (M_IsItemOn(MN_MP_MAIN, "PSETUP") && i == setupm_pselect)
			{
				static fixed_t cursorframe = 0;
				
				cursorframe += renderdeltatics / 4;
				for (; cursorframe > 7 * FRACUNIT; cursorframe -= 7 * FRACUNIT) {}

				V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, FRACUNIT, 0, W_CachePatchName(va("K_BHILI%d", (cursorframe >> FRACBITS) + 1), PU_CACHE), NULL);
			}

			x += incrwidth;
		}
#undef incrwidth
#undef spacingwidth
#undef iconwidth
	}
}

static void Splitplayers_OnChange(void)
{
	if (cv_splitplayers.value < setupm_pselect)
		setupm_pselect = 1;
}

void M_SetupMultiHandler(INT32 choice)
{
	boolean exitmenu = false;  // exit to previous menu and send name change

	switch (choice)
	{
		case KEY_LEFTARROW:
			if (cv_splitplayers.value > 1)
			{
				if (--setupm_pselect < 1)
					setupm_pselect = cv_splitplayers.value;
				S_StartSound(NULL,sfx_menu1); // Tails
			}
			break;

		case KEY_RIGHTARROW:
			if (cv_splitplayers.value > 1)
			{
				if (++setupm_pselect > cv_splitplayers.value)
					setupm_pselect = 1;
				S_StartSound(NULL,sfx_menu1); // Tails
			}
			break;

		case KEY_DOWNARROW:
			M_NextOpt();
			S_StartSound(NULL,sfx_menu1); // Tails
			break;

		case KEY_UPARROW:
			M_PrevOpt();
			S_StartSound(NULL,sfx_menu1); // Tails
			break;

		case KEY_ENTER:
		{
			S_StartSound(NULL,sfx_menu1); // Tails
			currentMenu->lastOn = itemOn;
			switch (setupm_pselect)
			{
				case 2:
					M_SetupMultiPlayer2(0);
					return;
				case 3:
					M_SetupMultiPlayer3(0);
					return;
				case 4:
					M_SetupMultiPlayer4(0);
					return;
				default:
					M_SetupMultiPlayer(0);
					return;
			}
			break;
		}

		case KEY_ESCAPE:
			exitmenu = true;
			break;
	}

	if (exitmenu)
		M_ExitMenu();
}

// Tails 11-19-2002
static void M_ConnectIP(INT32 choice)
{
	(void)choice;

	if (*setupm_ip == 0)
	{
		M_StartMessage("You must specify an IP address.\n", NULL, MM_NOTHING);
		return;
	}

	M_ClearMenus(true);

	COM_BufAddText(va("connect \"%s\"\n", setupm_ip));

	// A little "please wait" message.
	M_DrawTextBox(56, BASEVIDHEIGHT/2-12, 24, 2);
	V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT/2, 0, "Connecting to server...");
	I_OsPolling();
	I_UpdateNoBlit();
	if (rendermode == render_soft)
		I_FinishUpdate(); // page flip or blit buffer
}

// Tails 11-19-2002
void M_HandleConnectIP(INT32 choice)
{
	size_t l;
	boolean exitmenu = false;  // exit to previous menu and send name change

	switch (choice)
	{
		case KEY_DOWNARROW:
			M_NextOpt();
			S_StartSound(NULL,sfx_menu1); // Tails
			break;

		case KEY_UPARROW:
			M_PrevOpt();
			S_StartSound(NULL,sfx_menu1); // Tails
			break;

		case KEY_ENTER:
			S_StartSound(NULL,sfx_menu1); // Tails
			currentMenu->lastOn = itemOn;
			M_ConnectIP(1);
			break;

		case KEY_ESCAPE:
			exitmenu = true;
			break;

		case KEY_BACKSPACE:
			if ((l = strlen(setupm_ip)) != 0)
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				setupm_ip[l-1] = 0;
			}
			break;

		case KEY_DEL:
			if (setupm_ip[0])
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				setupm_ip[0] = 0;
			}
			break;

		default:
			l = strlen(setupm_ip);
			if (l >= 28-1)
				break;

			// Rudimentary number and period enforcing - also allows letters so hostnames can be used instead
			if ((choice >= '-' && choice <= ':') || (choice >= 'A' && choice <= 'Z') || (choice >= 'a' && choice <= 'z'))
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				setupm_ip[l] = (char)choice;
				setupm_ip[l+1] = 0;
			}
			else if (choice >= 199 && choice <= 211 && choice != 202 && choice != 206) //numpad too!
			{
				char keypad_translation[] = {'7','8','9','-','4','5','6','+','1','2','3','0','.'};
				choice = keypad_translation[choice - 199];
				S_StartSound(NULL,sfx_menu1); // Tails
				setupm_ip[l] = (char)choice;
				setupm_ip[l+1] = 0;
			}
			break;
	}

	if (exitmenu)
		M_ExitMenu();
}

// ========================
// MULTIPLAYER PLAYER SETUP
// ========================
// Tails 03-02-2002

// used for skin display on player setup menu
static fixed_t    multi_tics;
static state_t   *multi_state;
static UINT8      multi_spr2;

// used for follower display on player setup menu
static fixed_t follower_tics;
static UINT32 follower_frame;	// used for FF_ANIMATE garbo
static state_t *follower_state;

// this is set before entering the MultiPlayer setup menu,
// for either player 1 or 2
static char       setupm_name[MAXPLAYERNAME+1];
static player_t  *setupm_player;
static consvar_t *setupm_cvskin;
static consvar_t *setupm_cvcolor;
static consvar_t *setupm_cvname;
static consvar_t *setupm_cvfollower;
static consvar_t *setupm_cvfollowercolor;
static INT32      setupm_fakeskin;
static menucolor_t *setupm_fakecolor;
static INT32	  setupm_fakefollower;	// -1 is for none, our followers start at 0

void M_DrawSetupMultiPlayerMenu(void)
{
	INT32 mx, my, st, flags = 0;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	patch_t *statbg = W_CachePatchName("K_STATBG", PU_CACHE);
	patch_t *statlr = W_CachePatchName("K_STATLR", PU_CACHE);
	patch_t *statud = W_CachePatchName("K_STATUD", PU_CACHE);
	patch_t *statdot = W_CachePatchName("K_SDOT0", PU_CACHE);
	patch_t *patch;
	UINT8 frame;
	UINT8 speed;
	UINT8 weight;
	const UINT8 *flashcol = V_GetStringColormap(highlightflags);
	INT32 statx, staty;
	char *fname;
	INT16 i;

	mx = menudefs[MN_MP_PLAYERSETUP].x;
	my = menudefs[MN_MP_PLAYERSETUP].y;

	statx = (BASEVIDWIDTH - mx - 118);
	staty = (my+62);

	// use generic drawer for cursor, items and title
	M_DrawGenericMenu();

	// draw name string
	M_DrawTextBox(mx + 32, my - 8, MAXPLAYERNAME, 1);
	V_DrawString(mx + 40, my, V_ALLOWLOWERCASE, setupm_name);

	// draw text cursor for name
	if (M_IsItemOn(MN_MP_PLAYERSETUP, "NAME") && skullAnimCounter < 4) // blink cursor
		V_DrawCharacter(mx + 40 + V_StringWidth(setupm_name, V_ALLOWLOWERCASE), my, '_',false);

	// draw skin string
	st = V_StringWidth(skins[setupm_fakeskin].realname, 0);
	V_DrawString(BASEVIDWIDTH - mx - st, my + 16,
	             ((M_GetItemStatus(MN_MP_PLAYERSETUP, "SKIN") & IT_TYPE) == IT_SPACE ? V_TRANSLUCENT : 0)|highlightflags|V_ALLOWLOWERCASE,
	             skins[setupm_fakeskin].realname);
	if (M_IsItemOn(MN_MP_PLAYERSETUP, "SKIN"))
	{
		V_DrawCharacter(BASEVIDWIDTH - mx - 10 - st - (skullAnimCounter/5), my + 16,
			'\x1C' | highlightflags, false); // left arrow
		V_DrawCharacter(BASEVIDWIDTH - mx + 2 + (skullAnimCounter/5), my + 16,
			'\x1D' | highlightflags, false); // right arrow
	}

	// draw follower string
	fname = malloc(SKINNAMESIZE+1);

	if (setupm_fakefollower == -1)
		strcpy(fname, "None");
	else
		strcpy(fname, followers[setupm_fakefollower].name);

	st = V_StringWidth(fname, 0);
	V_DrawString(BASEVIDWIDTH - mx - st, my + 26,
	             ((M_GetItemStatus(MN_MP_PLAYERSETUP, "FOLLOW") & IT_TYPE) == IT_SPACE ? V_TRANSLUCENT : 0)|highlightflags|V_ALLOWLOWERCASE,
	             fname);
	if (M_IsItemOn(MN_MP_PLAYERSETUP, "FOLLOW"))
	{
		V_DrawCharacter(BASEVIDWIDTH - mx - 10 - st - (skullAnimCounter/5), my + 26,
			'\x1C' | highlightflags, false); // left arrow
		V_DrawCharacter(BASEVIDWIDTH - mx + 2 + (skullAnimCounter/5), my + 26,
			'\x1D' | highlightflags, false); // right arrow
	}

	// draw the name of the color you have chosen
	// Just so people don't go thinking that "Default" is Green.
	st = V_StringWidth(skincolors[setupm_fakecolor->color].name, 0);
	V_DrawString(BASEVIDWIDTH - mx - st, my + 152, highlightflags|V_ALLOWLOWERCASE, skincolors[setupm_fakecolor->color].name);	// SRB2kart
	if (M_IsItemOn(MN_MP_PLAYERSETUP, "COLOR"))
	{
		V_DrawCharacter(BASEVIDWIDTH - mx - 10 - st - (skullAnimCounter/5), my + 152,
			'\x1C' | highlightflags, false); // left arrow
		V_DrawCharacter(BASEVIDWIDTH - mx + 2 + (skullAnimCounter/5), my + 152,
			'\x1D' | highlightflags, false); // right arrow
	}

	// SRB2Kart: draw the stat backer
	// labels
	V_DrawThinString(statx+16, staty, V_6WIDTHSPACE|highlightflags, "Acceleration");
	V_DrawThinString(statx+91, staty, V_6WIDTHSPACE|highlightflags, "Max Speed");
	V_DrawThinString(statx, staty+12, V_6WIDTHSPACE|highlightflags, "Handling");
	V_DrawThinString(statx+7, staty+77, V_6WIDTHSPACE|highlightflags, "Weight");
	// label arrows
	V_DrawFixedPatch((statx+64)<<FRACBITS, staty<<FRACBITS, FRACUNIT, 0, statlr, flashcol);
	V_DrawFixedPatch((statx+24)<<FRACBITS, (staty+22)<<FRACBITS, FRACUNIT, 0, statud, flashcol);
	// bg
	V_DrawFixedPatch((statx+34)<<FRACBITS, (staty+10)<<FRACBITS, FRACUNIT, 0, statbg, 0);

	for (i = 0; i < numskins; i++) // draw the stat dots
	{
		if (i != setupm_fakeskin && R_SkinAvailable(skins[i].name) != -1)
		{
			speed = skins[i].kartspeed;
			weight = skins[i].kartweight;
			V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, NULL);
		}
	}

	speed = skins[setupm_fakeskin].kartspeed;
	weight = skins[setupm_fakeskin].kartweight;

	statdot = W_CachePatchName("K_SDOT1", PU_CACHE);
	if (skullAnimCounter < 4) // SRB2Kart: we draw this dot later so that it's not covered if there's multiple skins with the same stats
		V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, flashcol);
	else
		V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, NULL);

	statdot = W_CachePatchName("K_SDOT2", PU_CACHE); // coloured center
	if (setupm_fakecolor->color)
		V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, R_GetTranslationColormap(0, setupm_fakecolor->color, GTC_MENUCACHE));

	// 2.2 color bar backported with permission
#define charw 72
#define indexwidth 8
	{
		const INT32 numcolors = (250-charw)/(2*indexwidth); // Number of colors per side
		INT32 x = mx;
		INT32 w = indexwidth; // Width of a singular color block
		menucolor_t *mc = setupm_fakecolor->prev; // Last accessed color
		UINT8 h;

		// Draw color in the middle
		x += numcolors*w;
		for (h = 0; h < 16; h++)
			V_DrawFill(x, my+162+h, charw, 1, skincolors[setupm_fakecolor->color].ramp[h]);

		//Draw colors from middle to left
		for (i=0; i<numcolors; i++) {
			x -= w;
			// Find accessible color before this one
			while (!skincolors[mc->color].accessible)
				mc = mc->prev;
			for (h = 0; h < 16; h++)
				V_DrawFill(x, my+162+h, w, 1, skincolors[mc->color].ramp[h]);
			mc = mc->prev;
		}

		// Draw colors from middle to right
		mc = setupm_fakecolor->next;
		x += numcolors*w + charw;
		for (i=0; i<numcolors; i++) {
			// Find accessible color after this one
			while (!skincolors[mc->color].accessible)
				mc = mc->next;
			for (h = 0; h < 16; h++)
				V_DrawFill(x, my+162+h, w, 1, skincolors[mc->color].ramp[h]);
			x += w;
			mc = mc->next;
		}
	}
#undef indexwidth

	// character bar, ripped off the color bar :V
	if (setupm_fakecolor->color) // inverse should never happen
#define iconwidth 32
	{
		const INT32 icons = 4;
		INT32 k = -icons;
		INT16 col = (setupm_fakeskin - icons) % numskins;
		INT32 x = BASEVIDWIDTH/2 - ((icons+1)*24) - 4;
		fixed_t scale = FRACUNIT/2;
		INT32 offx = 8, offy = 8;
		patch_t *cursor;
		static fixed_t cursorframe = 0;
		patch_t *face;
		UINT8 *colmap;

		cursorframe += renderdeltatics / 4;
		for (; cursorframe > 7 * FRACUNIT; cursorframe -= 7 * FRACUNIT) {}

		cursor = W_CachePatchName(va("K_BHILI%d", (cursorframe >> FRACBITS) + 1), PU_CACHE);

		if (col < 0)
			col += numskins;
		while (k <= icons)
		{
			if (!(k++))
			{
				scale = FRACUNIT;
				face = faceprefix[col][FACE_WANTED];
				offx = 12;
				offy = 0;
			}
			else
			{
				scale = FRACUNIT/2;
				face = faceprefix[col][FACE_RANK];
				offx = 8;
				offy = 8;
			}
			colmap =  R_GetTranslationColormap(col, setupm_fakecolor->color, GTC_MENUCACHE);
			if (face)
				V_DrawFixedPatch((x+offx)<<FRACBITS, (my+28+offy)<<FRACBITS, FRACUNIT, 0, face, colmap);
			if (scale == FRACUNIT) // bit of a hack
				V_DrawFixedPatch((x+offx)<<FRACBITS, (my+28+offy)<<FRACBITS, FRACUNIT, 0, cursor, colmap);
			if (++col >= numskins)
				col -= numskins;
			x += FixedMul(iconwidth<<FRACBITS, 3*scale/2)/FRACUNIT;
		}
	}
#undef iconwidth

	// anim the player in the box
	multi_tics -= renderdeltatics;
	if (multi_tics <= 0)
	{
		st = multi_state->nextstate;
		if (st != S_NULL)
			multi_state = &states[st];

		if (multi_state->tics <= -1)
			multi_tics += 15*FRACUNIT;
		else
			multi_tics += multi_state->tics * FRACUNIT;
	}

	// skin 0 is default player sprite
	sprdef = &skins[setupm_fakeskin].sprites[multi_spr2];

	if (!sprdef->numframes) // No frames ??
		return; // Can't render!

	frame = multi_state->frame & FF_FRAMEMASK;
	if (frame >= sprdef->numframes) // Walking animation missing
		frame = 0; // Try to use standing frame

	sprframe = &sprdef->spriteframes[frame];
	patch = W_CachePatchNum(sprframe->lumppat[1], PU_CACHE);
	if (sprframe->flip & 2) // Only for first sprite
		flags |= V_FLIP; // This sprite is left/right flipped!

	// draw box around guy
	V_DrawFill(mx + 43 - (charw/2), my+65, charw, 84, 159);

	// draw player sprite
	if (setupm_fakecolor->color) // inverse should never happen
	{
		UINT8 *colormap = R_GetTranslationColormap(setupm_fakeskin, setupm_fakecolor->color, GTC_MENUCACHE);

		if (skins[setupm_fakeskin].flags & SF_HIRES)
		{
			V_DrawFixedPatch((mx+43)<<FRACBITS,
						(my+131)<<FRACBITS,
						skins[setupm_fakeskin].highresscale,
						flags, patch, colormap);
		}
		else
			V_DrawMappedPatch(mx+43, my+131, flags, patch, colormap);
	}

	// draw their follower if there is one
	if (setupm_fakefollower > -1 && setupm_fakefollower < numfollowers)
	{
		// animate the follower

		follower_tics -= renderdeltatics;
		if (follower_tics <= 0)
		{

			// FF_ANIMATE; cycle through FRAMES and get back afterwards. This will be prominent amongst followers hence why it's being supported here.
			if (follower_state->frame & FF_ANIMATE)
			{
				follower_frame++;
				follower_tics = follower_state->var2*FRACUNIT;
				if (follower_frame > (follower_state->frame & FF_FRAMEMASK) + follower_state->var1)	// that's how it works, right?
					follower_frame = follower_state->frame & FF_FRAMEMASK;
			}
			else
			{
				st = follower_state->nextstate;
				if (st != S_NULL)
					follower_state = &states[st];
				follower_tics = follower_state->tics*FRACUNIT;
						// get spritedef:
				follower_frame = follower_state->frame & FF_FRAMEMASK;
			}
		}
		sprdef = &sprites[follower_state->sprite];

		// draw the follower

		if (follower_frame >= sprdef->numframes)
			follower_frame = 0;	// frame doesn't exist, we went beyond it... what?
		sprframe = &sprdef->spriteframes[follower_frame];
		patch = W_CachePatchNum(sprframe->lumppat[1], PU_CACHE);
		if (sprframe->flip & 2) // Only for first sprite
			flags |= V_FLIP; // This sprite is left/right flipped!

		// draw follower sprite
		if (setupm_fakecolor->color) // inverse should never happen
		{
			// Fake the follower's in game appearance by now also applying some of its variables! coolio, eh?
			follower_t fl = followers[setupm_fakefollower];	// shortcut for our sanity

			tic_t bobspeed = fl.bobspeed;
			if (fl.mode == FOLLOWERMODE_GROUND)
				bobspeed = FixedDiv(bobspeed, fl.bobamp / 6); // Rough approximation of bounce speed

			// smooth floating, totally not stolen from rocket sneakers.
			fixed_t sine = FixedMul(fl.bobamp, FINESINE(((FixedMul(4 * M_TAU_FIXED, bobspeed) * followertimer)>>ANGLETOFINESHIFT) & FINEMASK));

			UINT16 color = K_GetEffectiveFollowerColor(setupm_cvfollowercolor->value, &fl, setupm_fakecolor->color, &skins[setupm_fakeskin]);
			UINT8 *colormap = R_GetTranslationColormap(TC_DEFAULT, color, 0); // why does GTC_MENUCACHE not work here...?

			INT32 x = (mx+65)*FRACUNIT;
			INT32 y = ((my+100)*FRACUNIT);
			if (fl.mode == FOLLOWERMODE_GROUND)
				y += 40*FRACUNIT - abs(sine) * 2; // Bounce animation
			else
				y += sine;

			V_DrawFixedPatch(x, y, fl.scale, flags, patch, colormap);
			Z_Free(colormap);
		}
	}

#undef charw
}

// follower state update. This is its own function so that it's at least somewhat clean
static void M_GetFollowerState(void)
{

	if (setupm_fakefollower <= -1 || setupm_fakefollower >= numfollowers) // yikes, there's none!
		return;
	// ^ we don't actually need to set anything since it won't be displayed anyway.

	//followertimer = 0;	// reset timer. not like it'll overflow anytime soon but whatever.

	// set follower state
	follower_state = &states[followers[setupm_fakefollower].followstate];

	if (follower_state->frame & FF_ANIMATE)
		follower_tics = follower_state->var2*FRACUNIT;	// support for FF_ANIMATE
	else
		follower_tics = follower_state->tics*FRACUNIT;

	follower_frame = follower_state->frame & FF_FRAMEMASK;
}

// Handle 1P/2P MP Setup
void M_HandleSetupMultiPlayer(INT32 choice)
{
	size_t   l;
	INT32 prev_setupm_fakeskin;
	boolean  exitmenu = false;  // exit to previous menu and send name change

	switch (choice)
	{
		case KEY_DOWNARROW:
			M_NextOpt();
			S_StartSound(NULL,sfx_menu1); // Tails
			break;

		case KEY_UPARROW:
			M_PrevOpt();
			S_StartSound(NULL,sfx_menu1); // Tails
			break;

		case KEY_LEFTARROW:
			if (M_IsItemOn(MN_MP_PLAYERSETUP, "SKIN"))       //player skin
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				prev_setupm_fakeskin = setupm_fakeskin;
				do
				{
					setupm_fakeskin--;
					if (setupm_fakeskin < 0)
						setupm_fakeskin = numskins-1;
				}
				while ((prev_setupm_fakeskin != setupm_fakeskin) && !(R_SkinUsable(-1, setupm_fakeskin)));
				multi_spr2 = P_GetSkinSprite2(&skins[setupm_fakeskin], SPR2_FSTN, NULL);
			}
			else if (M_IsItemOn(MN_MP_PLAYERSETUP, "FOLLOW")) // follower
			{
				S_StartSound(NULL,sfx_menu1);
				setupm_fakefollower--;
				M_GetFollowerState();	// update follower state
			}
			else if (M_IsItemOn(MN_MP_PLAYERSETUP, "COLOR")) // player color
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				setupm_fakecolor = setupm_fakecolor->prev;
			}
			break;

		case KEY_RIGHTARROW:
			if (M_IsItemOn(MN_MP_PLAYERSETUP, "SKIN"))       //player skin
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				prev_setupm_fakeskin = setupm_fakeskin;
				do
				{
					setupm_fakeskin++;
					if (setupm_fakeskin > numskins-1)
						setupm_fakeskin = 0;
				}
				while ((prev_setupm_fakeskin != setupm_fakeskin) && !(R_SkinUsable(-1, setupm_fakeskin)));
				multi_spr2 = P_GetSkinSprite2(&skins[setupm_fakeskin], SPR2_FSTN, NULL);
			}
			else if (M_IsItemOn(MN_MP_PLAYERSETUP, "FOLLOW")) // follower
			{
				S_StartSound(NULL,sfx_menu1);
				setupm_fakefollower++;
				M_GetFollowerState();
			}
			else if (M_IsItemOn(MN_MP_PLAYERSETUP, "COLOR")) // player color
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				setupm_fakecolor = setupm_fakecolor->next;
			}
			break;

		case KEY_ESCAPE:
			exitmenu = true;
			break;

		case KEY_BACKSPACE:
			if (M_IsItemOn(MN_MP_PLAYERSETUP, "NAME"))
			{
				if ((l = strlen(setupm_name))!=0)
				{
					S_StartSound(NULL,sfx_menu1); // Tails
					setupm_name[l-1] =0;
				}
			}
			else if (M_IsItemOn(MN_MP_PLAYERSETUP, "FOLLOW")) // follower
			{
				S_StartSound(NULL,sfx_menu1);
				setupm_fakefollower = -1;
			}
			else if (M_IsItemOn(MN_MP_PLAYERSETUP, "COLOR"))
			{
				UINT16 col = skins[setupm_fakeskin].prefcolor;
				if ((setupm_fakecolor->color != col) && skincolors[col].accessible)
				{
					S_StartSound(NULL,sfx_menu1); // Tails
					for (setupm_fakecolor=menucolorhead;;setupm_fakecolor=setupm_fakecolor->next)
						if (setupm_fakecolor->color == col || setupm_fakecolor == menucolortail)
							break;
				}
			}
			break;

		case KEY_DEL:
			if (M_IsItemOn(MN_MP_PLAYERSETUP, "NAME") && (l = strlen(setupm_name))!=0)
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				setupm_name[0] = 0;
			}
			break;

		default:
			if (choice < 32 || choice > 127 || !M_IsItemOn(MN_MP_PLAYERSETUP, "NAME"))
				break;
			l = strlen(setupm_name);
			if (l < MAXPLAYERNAME)
			{
				S_StartSound(NULL,sfx_menu1); // Tails
				setupm_name[l] =(char)choice;
				setupm_name[l+1] =0;
			}
			break;
	}

	// check followers:
	if (setupm_fakefollower < -1)
	{
		setupm_fakefollower = numfollowers;
		M_GetFollowerState(); // update follower state
	}
	if (setupm_fakefollower >= numfollowers)
	{
		setupm_fakefollower = -1;
		M_GetFollowerState(); // update follower state
	}

	// check color
	if (M_IsItemOn(MN_MP_PLAYERSETUP, "COLOR") && !skincolors[setupm_fakecolor->color].accessible) {
		if (choice == KEY_LEFTARROW)
			while (!skincolors[setupm_fakecolor->color].accessible)
				setupm_fakecolor = setupm_fakecolor->prev;
		else if (choice == KEY_RIGHTARROW || choice == KEY_ENTER)
			while (!skincolors[setupm_fakecolor->color].accessible)
				setupm_fakecolor = setupm_fakecolor->next;
	}

	if (exitmenu)
		M_ExitMenu();
}

// start the multiplayer setup menu
void M_SetupMultiPlayer(INT32 choice)
{
	(void)choice;

	multi_state = &states[mobjinfo[MT_PLAYER].seestate];
	multi_tics = multi_state->tics * FRACUNIT;

	strcpy(setupm_name, cv_playername[0].string);

	// set for player 1
	setupm_player = &players[consoleplayer];
	setupm_cvskin = &cv_skin[0];
	setupm_cvcolor = &cv_playercolor[0];
	setupm_cvname = &cv_playername[0];
	setupm_cvfollower = &cv_follower[0];
	setupm_cvfollowercolor = &cv_followercolor[0];

	setupm_fakefollower = setupm_cvfollower->value;

	// yikes, we don't want none of that...
	if (setupm_fakefollower >= numfollowers)
		setupm_fakefollower = -1;

	M_GetFollowerState();	// update follower state

	// For whatever reason this doesn't work right if you just use ->value
	setupm_fakeskin = R_SkinAvailable(setupm_cvskin->string);
	if (setupm_fakeskin == -1)
		setupm_fakeskin = 0;

	for (setupm_fakecolor=menucolorhead;;setupm_fakecolor=setupm_fakecolor->next)
		if (setupm_fakecolor->color == setupm_cvcolor->value || setupm_fakecolor == menucolortail)
			break;

	// disable skin changes if we can't actually change skins
	if (!CanChangeSkin(consoleplayer))
		M_SetItemStatus(MN_MP_PLAYERSETUP, "SKIN", IT_GRAYEDOUT);
	else
		M_SetItemStatus(MN_MP_PLAYERSETUP, "SKIN", IT_KEYHANDLER|IT_STRING);

	M_EnterMenu(MN_MP_PLAYERSETUP, true);
}

// start the multiplayer setup menu, for secondary player (splitscreen mode)
void M_SetupMultiPlayer2(INT32 choice)
{
	(void)choice;

	multi_state = &states[mobjinfo[MT_PLAYER].seestate];
	multi_tics = multi_state->tics * FRACUNIT;
	strcpy (setupm_name, cv_playername[1].string);

	// set for splitscreen secondary player
	setupm_player = &players[g_localplayers[1]];
	setupm_cvskin = &cv_skin[1];
	setupm_cvcolor = &cv_playercolor[1];
	setupm_cvname = &cv_playername[1];
	setupm_cvfollower = &cv_follower[1];
	setupm_cvfollowercolor = &cv_followercolor[1];

	setupm_fakefollower = setupm_cvfollower->value;

	// yikes, we don't want none of that...
	if (setupm_fakefollower >= numfollowers)
		setupm_fakefollower = -1;

	M_GetFollowerState();	// update follower state

	// For whatever reason this doesn't work right if you just use ->value
	setupm_fakeskin = R_SkinAvailable(setupm_cvskin->string);
	if (setupm_fakeskin == -1)
		setupm_fakeskin = 0;

	for (setupm_fakecolor=menucolorhead;;setupm_fakecolor=setupm_fakecolor->next)
		if (setupm_fakecolor->color == setupm_cvcolor->value || setupm_fakecolor == menucolortail)
			break;

	// disable skin changes if we can't actually change skins
	if (splitscreen && !CanChangeSkin(g_localplayers[1]))
		M_SetItemStatus(MN_MP_PLAYERSETUP, "SKIN", IT_GRAYEDOUT);
	else
		M_SetItemStatus(MN_MP_PLAYERSETUP, "SKIN", IT_KEYHANDLER | IT_STRING);

	M_EnterMenu(MN_MP_PLAYERSETUP, true);
}

// start the multiplayer setup menu, for third player (splitscreen mode)
void M_SetupMultiPlayer3(INT32 choice)
{
	(void)choice;

	multi_state = &states[mobjinfo[MT_PLAYER].seestate];
	multi_tics = multi_state->tics;
	strcpy(setupm_name, cv_playername[2].string);

	// set for splitscreen third player
	setupm_player = &players[g_localplayers[2]];
	setupm_cvskin = &cv_skin[2];
	setupm_cvcolor = &cv_playercolor[2];
	setupm_cvname = &cv_playername[2];
	setupm_cvfollower = &cv_follower[2];
	setupm_cvfollowercolor = &cv_followercolor[2];

	setupm_fakefollower = setupm_cvfollower->value;

	// yikes, we don't want none of that...
	if (setupm_fakefollower >= numfollowers)
		setupm_fakefollower = -1;

	M_GetFollowerState();	// update follower state

	// For whatever reason this doesn't work right if you just use ->value
	setupm_fakeskin = R_SkinAvailable(setupm_cvskin->string);
	if (setupm_fakeskin == -1)
		setupm_fakeskin = 0;

	for (setupm_fakecolor=menucolorhead;;setupm_fakecolor=setupm_fakecolor->next)
		if (setupm_fakecolor->color == setupm_cvcolor->value || setupm_fakecolor == menucolortail)
			break;

	// disable skin changes if we can't actually change skins
	if (splitscreen > 1 && !CanChangeSkin(g_localplayers[2]))
		M_SetItemStatus(MN_MP_PLAYERSETUP, "SKIN", IT_GRAYEDOUT);
	else
		M_SetItemStatus(MN_MP_PLAYERSETUP, "SKIN", IT_KEYHANDLER | IT_STRING);

	M_EnterMenu(MN_MP_PLAYERSETUP, true);
}

// start the multiplayer setup menu, for third player (splitscreen mode)
void M_SetupMultiPlayer4(INT32 choice)
{
	(void)choice;

	multi_state = &states[mobjinfo[MT_PLAYER].seestate];
	multi_tics = multi_state->tics;
	strcpy(setupm_name, cv_playername[3].string);

	// set for splitscreen fourth player
	setupm_player = &players[g_localplayers[3]];
	setupm_cvskin = &cv_skin[3];
	setupm_cvcolor = &cv_playercolor[3];
	setupm_cvname = &cv_playername[3];
	setupm_cvfollower = &cv_follower[3];
	setupm_cvfollowercolor = &cv_followercolor[3];

	setupm_fakefollower = setupm_cvfollower->value;

	// yikes, we don't want none of that...
	if (setupm_fakefollower >= numfollowers)
		setupm_fakefollower = -1;

	M_GetFollowerState();	// update follower state

	// For whatever reason this doesn't work right if you just use ->value
	setupm_fakeskin = R_SkinAvailable(setupm_cvskin->string);
	if (setupm_fakeskin == -1)
		setupm_fakeskin = 0;

	for (setupm_fakecolor=menucolorhead;;setupm_fakecolor=setupm_fakecolor->next)
		if (setupm_fakecolor->color == setupm_cvcolor->value || setupm_fakecolor == menucolortail)
			break;

	// disable skin changes if we can't actually change skins
	if (splitscreen > 2 && !CanChangeSkin(g_localplayers[3]))
		M_SetItemStatus(MN_MP_PLAYERSETUP, "SKIN", IT_GRAYEDOUT);
	else
		M_SetItemStatus(MN_MP_PLAYERSETUP, "SKIN", IT_KEYHANDLER | IT_STRING);

	M_EnterMenu(MN_MP_PLAYERSETUP, true);
}

void M_QuitMultiPlayerMenu(INT32 choice)
{
	(void)choice;
	size_t l;
	const char *followername = setupm_fakefollower == -1 ?
		"None" : followers[setupm_fakefollower].skinname;
	// send name if changed
	if (strcmp(setupm_name, setupm_cvname->string))
	{
		// remove trailing whitespaces
		for (l= strlen(setupm_name)-1;
		    (signed)l >= 0 && setupm_name[l] ==' '; l--)
			setupm_name[l] =0;
		COM_BufAddText (va("%s \"%s\"\n",setupm_cvname->name,setupm_name));
	}
	// you know what? always putting these in the buffer won't hurt anything.
	COM_BufAddText (va("%s \"%s\"\n",setupm_cvskin->name,skins[setupm_fakeskin].name));
	COM_BufAddText (va("%s %d\n",setupm_cvcolor->name,setupm_fakecolor->color));
	COM_BufAddText (va("%s %s\n",setupm_cvfollower->name,followername));
}

void M_AddMenuColor(UINT16 color) {
	menucolor_t *c;

	// SRB2Kart: I do not understand vanilla doesn't need this but WE do???!?!??!
	if (!skincolors[color].accessible) {
		return;
	}

	if (color >= numskincolors) {
		CONS_Printf("M_AddMenuColor: color %d does not exist.",color);
		return;
	}

	c = (menucolor_t *)malloc(sizeof(menucolor_t));
	c->color = color;
	if (menucolorhead == NULL) {
		c->next = c;
		c->prev = c;
		menucolorhead = c;
		menucolortail = c;
	} else {
		c->next = menucolorhead;
		c->prev = menucolortail;
		menucolortail->next = c;
		menucolorhead->prev = c;
		menucolortail = c;
	}
}

void M_MoveColorBefore(UINT16 color, UINT16 targ) {
	menucolor_t *look, *c = NULL, *t = NULL;

	if (color == targ)
		return;
	if (color >= numskincolors) {
		CONS_Printf("M_MoveColorBefore: color %d does not exist.",color);
		return;
	}
	if (targ >= numskincolors) {
		CONS_Printf("M_MoveColorBefore: target color %d does not exist.",targ);
		return;
	}

	for (look=menucolorhead;;look=look->next) {
		if (look->color == color)
			c = look;
		else if (look->color == targ)
			t = look;
		if (c != NULL && t != NULL)
			break;
		if (look==menucolortail)
			return;
	}

	if (c == t->prev)
		return;

	if (t==menucolorhead)
		menucolorhead = c;
	if (c==menucolortail)
		menucolortail = c->prev;

	c->prev->next = c->next;
	c->next->prev = c->prev;

	c->prev = t->prev;
	c->next = t;
	t->prev->next = c;
	t->prev = c;
}

void M_MoveColorAfter(UINT16 color, UINT16 targ) {
	menucolor_t *look, *c = NULL, *t = NULL;

	if (color == targ)
		return;
	if (color >= numskincolors) {
		CONS_Printf("M_MoveColorAfter: color %d does not exist.\n",color);
		return;
	}
	if (targ >= numskincolors) {
		CONS_Printf("M_MoveColorAfter: target color %d does not exist.\n",targ);
		return;
	}

	for (look=menucolorhead;;look=look->next) {
		if (look->color == color)
			c = look;
		else if (look->color == targ)
			t = look;
		if (c != NULL && t != NULL)
			break;
		if (look==menucolortail)
			return;
	}

	if (t == c->prev)
		return;

	if (t==menucolortail)
		menucolortail = c;
	else if (c==menucolortail)
		menucolortail = c->prev;

	c->prev->next = c->next;
	c->next->prev = c->prev;

	c->next = t->next;
	c->prev = t;
	t->next->prev = c;
	t->next = c;
}

UINT16 M_GetColorBefore(UINT16 color) {
	menucolor_t *look;

	if (color >= numskincolors) {
		CONS_Printf("M_GetColorBefore: color %d does not exist.\n",color);
		return 0;
	}

	for (look=menucolorhead;;look=look->next) {
		if (look->color == color)
			return look->prev->color;
		if (look==menucolortail)
			return 0;
	}
}

UINT16 M_GetColorAfter(UINT16 color) {
	menucolor_t *look;

	if (color >= numskincolors) {
		CONS_Printf("M_GetColorAfter: color %d does not exist.\n",color);
		return 0;
	}

	for (look=menucolorhead;;look=look->next) {
		if (look->color == color)
			return look->next->color;
		if (look==menucolortail)
			return 0;
	}
}

void M_InitPlayerSetupColors(void) {
	UINT16 i;
	numskincolors = SKINCOLOR_FIRSTFREESLOT;
	menucolorhead = menucolortail = NULL;
	for (i=0; i<numskincolors; i++)
		M_AddMenuColor(i);
}

void M_FreePlayerSetupColors(void) {
	menucolor_t *look = menucolorhead, *tmp;

	if (menucolorhead==NULL)
		return;

	while (true) {
		if (look != menucolortail) {
			tmp = look;
			look = look->next;
			free(tmp);
		} else {
			free(look);
			return;
		}
	}

	menucolorhead = menucolortail = NULL;
}

// =================
// DATA OPTIONS MENU
// =================
static UINT8 erasecontext = 0;

static void M_EraseDataResponse(INT32 ch)
{
	UINT8 i;

	if (ch != 'y' && ch != KEY_ENTER)
		return;

	S_StartSound(NULL, sfx_itrole); // bweh heh heh

	// Delete the data
	if (erasecontext == 2)
	{
		// SRB2Kart: This actually needs to be done FIRST, so that you don't immediately regain playtime/matches secrets
		K_EraseStats();
		for (i = 0; i < PWRLV_NUMTYPES; i++)
			vspowerlevel[i] = PWRLVRECORD_START;
		F_StartIntro();
	}
	if (erasecontext != 1)
		G_ClearRecords();
	if (erasecontext != 0)
		M_ClearSecrets();
	M_ClearMenus(true);
}

void M_EraseData(INT32 choice)
{
	const char *eschoice, *esstr = M_GetText("Are you sure you want to erase\n%s?\n\n(Press 'Y' to confirm)\n");

	erasecontext = (UINT8)choice;

	if (choice == 0)
		eschoice = M_GetText("Record Attack data");
	else if (choice == 1)
		eschoice = M_GetText("Secrets data");
	else
		eschoice = M_GetText("ALL game data");

	M_StartMessage(va(esstr, eschoice), M_EraseDataResponse, MM_YESNO);
}

void M_ScreenshotOptions(INT32 choice)
{
	(void)choice;
	Screenshot_option_Onchange();
	Moviemode_mode_Onchange();

	M_EnterMenu(MN_OP_SCREENSHOTS, true);
}

// =============
// JOYSTICK MENU
// =============

// Start the controls menu, setting it up for either the console player,
// or the secondary splitscreen player

void M_DrawJoystick(void)
{
	INT32 i;
	INT32 compareval;

	M_DrawGenericMenu();

	for (i = 0; i <= MAXGAMEPADS; i++)
	{
		M_DrawTextBox(menudefs[MN_OP_JOYSTICKSET].x-8, menudefs[MN_OP_JOYSTICKSET].y+LINEHEIGHT*i-12, 28, 1);

#ifdef JOYSTICK_HOTPLUG
		if (atoi(cv_usejoystick[setupcontrolplayer-1].string) > I_NumJoys())
			compareval = atoi(cv_usejoystick[setupcontrolplayer-1].string);
		else
#endif
			compareval = cv_usejoystick[setupcontrolplayer-1].value;

		V_DrawString(menudefs[MN_OP_JOYSTICKSET].x, menudefs[MN_OP_JOYSTICKSET].y+LINEHEIGHT*i-4, (i == compareval) ? V_GREENMAP : 0, joystickInfo[i]);
	}
}

void M_SetupJoystickMenu(INT32 choice)
{
	const char *joyNA = "Unavailable";
	const INT32 n = I_NumJoys();

	INT32 i = 0;
	INT32 j;

	(void)choice;

	strcpy(joystickInfo[i], "None");

	for (i = 1; i <= MAXGAMEPADS; i++)
	{
		if (i <= n && (I_GetJoyName(i)) != NULL)
			strncpy(joystickInfo[i], I_GetJoyName(i), 28);
		else
			strcpy(joystickInfo[i], joyNA);

#ifdef JOYSTICK_HOTPLUG
		// We use cv_usejoystick.string as the USER-SET var
		// and cv_usejoystick.value as the INTERNAL var
		//
		// In practice, if cv_usejoystick.string == 0, this overrides
		// cv_usejoystick.value and always disables
		//
		// Update cv_usejoystick.string here so that the user can
		// properly change this value.
		for (j = 0; j < MAXSPLITSCREENPLAYERS; j++)
		{
			if (i == cv_usejoystick[j].value)
				CV_SetValue(&cv_usejoystick[j], i);
		}
#endif
	}

	M_EnterMenu(MN_OP_JOYSTICKSET, true);
}

void M_Setup1PJoystickMenu(INT32 choice)
{
	setupcontrolplayer = 1;
	M_SetupJoystickMenu(choice);
}

void M_Setup2PJoystickMenu(INT32 choice)
{
	setupcontrolplayer = 2;
	M_SetupJoystickMenu(choice);
}

void M_Setup3PJoystickMenu(INT32 choice)
{
	setupcontrolplayer = 3;
	M_SetupJoystickMenu(choice);
}

void M_Setup4PJoystickMenu(INT32 choice)
{
	setupcontrolplayer = 4;
	M_SetupJoystickMenu(choice);
}

void M_AssignJoystick(INT32 choice)
{
	const UINT8 p = setupcontrolplayer-1;

#ifdef JOYSTICK_HOTPLUG
	INT32 oldchoice, oldstringchoice;
	INT32 numjoys = I_NumJoys();

	oldchoice = oldstringchoice = atoi(cv_usejoystick[p].string) > numjoys ? atoi(cv_usejoystick[p].string) : cv_usejoystick[p].value;
	CV_SetValue(&cv_usejoystick[p], choice);

	// Just in case last-minute changes were made to cv_usejoystick.value,
	// update the string too
	// But don't do this if we're intentionally setting higher than numjoys
	if (choice <= numjoys)
	{
		CV_SetValue(&cv_usejoystick[p], cv_usejoystick[p].value);

		// reset this so the comparison is valid
		if (oldchoice > numjoys)
			oldchoice = cv_usejoystick[p].value;

		if (oldchoice != choice)
		{
			if (choice && oldstringchoice > numjoys) // if we did not select "None", we likely selected a used device
				CV_SetValue(&cv_usejoystick[p], (oldstringchoice > numjoys ? oldstringchoice : oldchoice));

			if (oldstringchoice ==
				(atoi(cv_usejoystick[p].string) > numjoys ? atoi(cv_usejoystick[p].string) : cv_usejoystick[p].value))
				M_StartMessage("This joystick is used by another\n"
							   "player. Reset the joystick\n"
							   "for that player first.\n\n"
							   "(Press a key)\n", NULL, MM_NOTHING);
		}
	}
#else
	CV_SetValue(&cv_usejoystick[p], choice);
#endif
}

// =============
// CONTROLS MENU
// =============

void M_Setup1PControlsMenu(INT32 choice)
{
	(void)choice;
	setupcontrolplayer = 1;
	setupcontrols = gamecontrol[0];        // was called from main Options (for console player, then)
	currentMenu->lastOn = itemOn;

	// Set proper gamepad options
	M_SetItemRoutine(MN_OP_CHANGECONTROLS, "SETJOY", M_Setup1PJoystickMenu);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "DEADZ", &cv_deadzone[0]);

	// Unhide P1-only controls
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "TALK", IT_CONTROL); // Chat
	//M_SetItemStatus(MN_OP_CHANGECONTROLS, "TEAM", IT_CONTROL); // Team-chat
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SCORES", IT_CONTROL); // Rankings
	//M_SetItemStatus(MN_OP_CHANGECONTROLS, "VIEWPT", IT_CONTROL); // Viewpoint
	// 19 is Reset Camera, 20 is Toggle Chasecam
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "PAUSE", IT_CONTROL); // Pause
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SCRSHT", IT_CONTROL); // Screenshot
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "RECGIF", IT_CONTROL); // GIF
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SYSMEN", IT_CONTROL); // System Menu
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "CONSOL", IT_CONTROL); // Console
	/*M_SetItemStatus(MN_OP_CHANGECONTROLS, 26, IT_HEADER); // Spectator Controls header
	M_SetItemStatus(MN_OP_CHANGECONTROLS, 27, IT_SPACE); // Spectator Controls space
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SPECTA", IT_CONTROL); // Spectate
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "LOOKUP", IT_CONTROL); // Look Up
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "LOOKDW", IT_CONTROL); // Look Down
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "CENTER", IT_CONTROL); // Center View
	*/

	M_EnterMenu(MN_OP_CHANGECONTROLS, true);
}

void M_Setup2PControlsMenu(INT32 choice)
{
	(void)choice;
	setupcontrolplayer = 2;
	setupcontrols = gamecontrol[1];
	currentMenu->lastOn = itemOn;

	// Set proper gamepad options
	M_SetItemRoutine(MN_OP_CHANGECONTROLS, "SETJOY", M_Setup2PJoystickMenu);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "DEADZ", &cv_deadzone[1]);

	// Hide P1-only controls
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "TALK", IT_GRAYEDOUT2); // Chat
	//M_SetItemStatus(MN_OP_CHANGECONTROLS, "TEAM", IT_GRAYEDOUT2); // Team-chat
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SCORES", IT_GRAYEDOUT2); // Rankings
	//M_SetItemStatus(MN_OP_CHANGECONTROLS, "VIEWPT", IT_GRAYEDOUT2); // Viewpoint
	// 19 is Reset Camera, 20 is Toggle Chasecam
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "PAUSE", IT_GRAYEDOUT2); // Pause
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SCRSHT", IT_GRAYEDOUT2); // Screenshot
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "RECGIF", IT_GRAYEDOUT2); // GIF
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SYSMEN", IT_GRAYEDOUT2); // System Menu
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "CONSOL", IT_GRAYEDOUT2); // Console
	/*M_SetItemStatus(MN_OP_CHANGECONTROLS, 26, IT_GRAYEDOUT2); // Spectator Controls header
	M_SetItemStatus(MN_OP_CHANGECONTROLS, 27, IT_GRAYEDOUT2); // Spectator Controls space
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SPECTA", IT_GRAYEDOUT2); // Spectate
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "LOOKUP", IT_GRAYEDOUT2); // Look Up
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "LOOKDW", IT_GRAYEDOUT2); // Look Down
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "CENTER", IT_GRAYEDOUT2); // Center View
	*/

	M_EnterMenu(MN_OP_CHANGECONTROLS, true);
}

void M_Setup3PControlsMenu(INT32 choice)
{
	(void)choice;
	setupcontrolplayer = 3;
	setupcontrols = gamecontrol[2];
	currentMenu->lastOn = itemOn;

	// Set proper gamepad options
	M_SetItemRoutine(MN_OP_CHANGECONTROLS, "SETJOY", M_Setup3PJoystickMenu);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "DEADZ", &cv_deadzone[2]);

	// Hide P1-only controls
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "TALK", IT_GRAYEDOUT2); // Chat
	//M_SetItemStatus(MN_OP_CHANGECONTROLS, "TEAM", IT_GRAYEDOUT2); // Team-chat
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SCORES", IT_GRAYEDOUT2); // Rankings
	//M_SetItemStatus(MN_OP_CHANGECONTROLS, "VIEWPT", IT_GRAYEDOUT2); // Viewpoint
	// 19 is Reset Camera, 20 is Toggle Chasecam
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "PAUSE", IT_GRAYEDOUT2); // Pause
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SCRSHT", IT_GRAYEDOUT2); // Screenshot
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "RECGIF", IT_GRAYEDOUT2); // GIF
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SYSMEN", IT_GRAYEDOUT2); // System Menu
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "CONSOL", IT_GRAYEDOUT2); // Console
	/*M_SetItemStatus(MN_OP_CHANGECONTROLS, 26, IT_GRAYEDOUT2); // Spectator Controls header
	M_SetItemStatus(MN_OP_CHANGECONTROLS, 27, IT_GRAYEDOUT2); // Spectator Controls space
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SPECTA", IT_GRAYEDOUT2); // Spectate
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "LOOKUP", IT_GRAYEDOUT2); // Look Up
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "LOOKDW", IT_GRAYEDOUT2); // Look Down
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "CENTER", IT_GRAYEDOUT2); // Center View
	*/

	M_EnterMenu(MN_OP_CHANGECONTROLS, true);
}

void M_Setup4PControlsMenu(INT32 choice)
{
	(void)choice;
	setupcontrolplayer = 4;
	setupcontrols = gamecontrol[3];
	currentMenu->lastOn = itemOn;

	// Set proper gamepad options
	M_SetItemRoutine(MN_OP_CHANGECONTROLS, "SETJOY", M_Setup4PJoystickMenu);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "DEADZ", &cv_deadzone[3]);

	// Hide P1-only controls
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "TALK", IT_GRAYEDOUT2); // Chat
	//M_SetItemStatus(MN_OP_CHANGECONTROLS, "TEAM", IT_GRAYEDOUT2); // Team-chat
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SCORES", IT_GRAYEDOUT2); // Rankings
	//M_SetItemStatus(MN_OP_CHANGECONTROLS, "VIEWPT", IT_GRAYEDOUT2); // Viewpoint
	// 19 is Reset Camera, 20 is Toggle Chasecam
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "PAUSE", IT_GRAYEDOUT2); // Pause
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SCRSHT", IT_GRAYEDOUT2); // Screenshot
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "RECGIF", IT_GRAYEDOUT2); // GIF
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SYSMEN", IT_GRAYEDOUT2); // System Menu
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "CONSOL", IT_GRAYEDOUT2); // Console
	/*M_SetItemStatus(MN_OP_CHANGECONTROLS, 26, IT_GRAYEDOUT2); // Spectator Controls header
	M_SetItemStatus(MN_OP_CHANGECONTROLS, 27, IT_GRAYEDOUT2); // Spectator Controls space
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "SPECTA", IT_GRAYEDOUT2); // Spectate
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "LOOKUP", IT_GRAYEDOUT2); // Look Up
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "LOOKDW", IT_GRAYEDOUT2); // Look Down
	M_SetItemStatus(MN_OP_CHANGECONTROLS, "CENTER", IT_GRAYEDOUT2); // Center View
	*/

	M_EnterMenu(MN_OP_CHANGECONTROLS, true);
}

#define controlheight 18

// Draws the Customise Controls menu
void M_DrawControl(void)
{
	char tmp[32*MAXINPUTMAPPING]; // should be enough :^)
	INT32 x, y, w, i, max, cursory = 0, iter;
	INT32 key;

	x = currentMenu->x;
	y = currentMenu->y;

	/*i = itemOn - (controlheight/2);
	if (i < 0)
		i = 0;
	*/

	iter = (controlheight/2);
	for (i = itemOn; ((iter || currentMenu->menuitems[i].status == IT_GRAYEDOUT2) && i > 0); i--)
	{
		if (currentMenu->menuitems[i].status != IT_GRAYEDOUT2)
			iter--;
	}
	if (currentMenu->menuitems[i].status == IT_GRAYEDOUT2)
		i--;

	iter += (controlheight/2);
	for (max = itemOn; (iter && max < currentMenu->numitems); max++)
	{
		if (currentMenu->menuitems[max].status != IT_GRAYEDOUT2)
			iter--;
	}

	if (iter)
	{
		iter += (controlheight/2);
		for (i = itemOn; ((iter || currentMenu->menuitems[i].status == IT_GRAYEDOUT2) && i > 0); i--)
		{
			if (currentMenu->menuitems[i].status != IT_GRAYEDOUT2)
				iter--;
		}
	}

	/*max = i + controlheight;
	if (max > currentMenu->numitems)
	{
		max = currentMenu->numitems;
		if (max < controlheight)
			i = 0;
		else
			i = max - controlheight;
	}*/

	// draw title (or big pic)
	M_DrawMenuTitle();

	M_CentreText(28,
		(setupcontrolplayer > 1 ? va("\x86""Set controls for ""\x82""Player %d", setupcontrolplayer) :
		                          "\x86""Press ""\x82""ENTER""\x86"" to change, ""\x82""BACKSPACE""\x86"" to clear"));

	if (i)
		V_DrawCharacter(currentMenu->x - 16, y-(skullAnimCounter/5),
			'\x1A' | highlightflags, false); // up arrow
	if (max != currentMenu->numitems)
		V_DrawCharacter(currentMenu->x - 16, y+(SMALLLINEHEIGHT*(controlheight-1))+(skullAnimCounter/5) + (skullAnimCounter/5),
			'\x1B' | highlightflags, false); // down arrow

	for (; i < max; i++)
	{
		if (currentMenu->menuitems[i].status == IT_GRAYEDOUT2)
			continue;

		if (i == itemOn)
			cursory = y;

		if (currentMenu->menuitems[i].status == IT_CONTROL)
		{
			V_DrawString(x, y, ((i == itemOn) ? highlightflags : 0), currentMenu->menuitems[i].text);

			tmp[0] ='\0';
			for (iter = 0; iter < MAXINPUTMAPPING; iter++)
			{
				key = setupcontrols[currentMenu->menuitems[i].alphaKey][iter];
				if (key != KEY_NULL)
				{
					if (tmp[0] != '\0')
						strcat(tmp, ", ");
					strcat(tmp, G_KeynumToString(key));
				}
			}
			if (tmp[0] == '\0')
				strcpy(tmp, "---");

			w = V_StringWidth(tmp, highlightflags);
			(w > BASEVIDWIDTH/2 - 4 ? V_DrawRightAlignedThinString : V_DrawRightAlignedString)
				(BASEVIDWIDTH-currentMenu->x, y, highlightflags, tmp);
		}
		/*else if (currentMenu->menuitems[i].status == IT_GRAYEDOUT2)
			V_DrawString(x, y, V_TRANSLUCENT, currentMenu->menuitems[i].text);*/
		else if ((currentMenu->menuitems[i].status == IT_HEADER) && (i != max-1))
			V_DrawString(13, y+6, highlightflags, currentMenu->menuitems[i].text);
		else if (currentMenu->menuitems[i].status & IT_STRING)
		{
			V_DrawString(x, y, ((i == itemOn) ? highlightflags : 0), currentMenu->menuitems[i].text);
			if (currentMenu->menuitems[i].status & IT_CVAR)
			{
				V_DrawRightAlignedString(BASEVIDWIDTH-currentMenu->x, y, highlightflags, currentMenu->menuitems[i].itemaction.cvar->string);
				if (i == itemOn)
				{
					w = V_StringWidth(currentMenu->menuitems[i].itemaction.cvar->string, highlightflags);
					V_DrawCharacter(BASEVIDWIDTH - currentMenu->x - 10 - w - (skullAnimCounter/5), y,
						'\x1C' | highlightflags, false); // left arrow
					V_DrawCharacter(BASEVIDWIDTH - currentMenu->x + 2 + (skullAnimCounter/5), y,
						'\x1D' | highlightflags, false); // right arrow
				}
			}
			else if (i == 0) // gamepad select
			{
				INT32 stick = cv_usejoystick[setupcontrolplayer-1].value;
				const char *name = stick == 0 ? "None" : I_GetJoyName(stick);
				if (!name) name = "?";
				w = V_StringWidth(name, highlightflags);
				(w > BASEVIDWIDTH/2 - 4 ? V_DrawRightAlignedThinString : V_DrawRightAlignedString)
					(BASEVIDWIDTH-currentMenu->x, y, highlightflags, name);
			}
		}

		y += SMALLLINEHEIGHT;
	}

	V_DrawScaledPatch(currentMenu->x - 18, cursory, 0,
		W_CachePatchName("M_CURSOR", PU_CACHE));
}

#undef controlheight

static INT32 controltochange;
static char controltochangetext[33];

static void M_ChangecontrolResponse(event_t *ev)
{
	INT32        control;
	INT32        found;
	INT32        ch = ev->data1;

	if (ev->device > 0 && G_GetDevicePlayer(ev->device) != setupcontrolplayer-1)
		return;

	if (ev->type == ev_joystick)
	{
		if (!G_AxisInDeadzone(setupcontrolplayer-1, ev))
			ch = G_AxisToKey(ev);
		else
			return;
	}

	// ESCAPE cancels; dummy out PAUSE
	if (ch != KEY_ESCAPE && ch != KEY_PAUSE)
	{
		// keypad arrows are converted for the menu in cursor arrows
		// so use the event instead of ch
		if (ev->type == ev_keydown)
			ch = ev->data1;

		control = controltochange;

		// check if we already entered this key
		for (found = MAXINPUTMAPPING-1; found >= 0; found--)
			if (setupcontrols[control][found] == ch)
				break;

		if (found >= 0)
		{
			// replace mouse and joy clicks by double clicks
			/*
			if (ch >= KEY_MOUSE1 && ch <= KEY_MOUSE1+MOUSEBUTTONS)
				setupcontrols[control][found] = ch-KEY_MOUSE1+KEY_DBLMOUSE1;
			else if (ch >= KEY_JOY1 && ch <= KEY_JOY1+JOYBUTTONS)
				setupcontrols[control][found] = ch-KEY_JOY1+KEY_DBLJOY1;
			*/
		}
		else
		{
			// find an empty slot for this key
			for (found = 0; found < MAXINPUTMAPPING; found++)
				if (setupcontrols[control][found] == KEY_NULL)
					break;

			// no slots? shift down the other keys to make room (last one out)
			if (found == MAXINPUTMAPPING)
				for (found = 0; found < MAXINPUTMAPPING-1; found++)
					setupcontrols[control][found] = setupcontrols[control][found+1];

			(void)G_CheckDoubleUsage(ch, setupcontrolplayer-1, true);
			setupcontrols[control][found] = ch;
		}
		S_StartSound(NULL, sfx_s221);
	}
	else if (ch == KEY_PAUSE)
	{
		// This buffer assumes a 125-character message plus a 32-character control name (per controltochangetext buffer size)
		static char tmp[158];

		if (controltochange == gc_pause)
			sprintf(tmp, M_GetText("The \x82Pause Key \x80is enabled, but \nyou may select another key. \n\nHit another key for\n%s\nESC for Cancel"),
				controltochangetext);
		else
			sprintf(tmp, M_GetText("The \x82Pause Key \x80is enabled, but \nit is not configurable. \n\nHit another key for\n%s\nESC for Cancel"),
				controltochangetext);

		M_StartMessage(tmp, M_ChangecontrolResponse, MM_EVENTHANDLER);

		S_StartSound(NULL, sfx_s3k42);
		return;
	}
	else
		S_StartSound(NULL, sfx_s224);

	messagebox.active = false;
}

void M_ChangeControl(INT32 choice)
{
	// This buffer assumes a 35-character message (per below) plus a max control name limit of 32 chars (per controltochangetext)
	// If you change the below message, then change the size of this buffer!
	static char tmp[68];

	controltochange = currentMenu->menuitems[choice].alphaKey;
	sprintf(tmp, M_GetText("Hit the new key for\n%s\nESC for Cancel"),
		currentMenu->menuitems[choice].text);
	strlcpy(controltochangetext, currentMenu->menuitems[choice].text, 33);

	M_StartMessage(tmp, M_ChangecontrolResponse, MM_EVENTHANDLER);
}

static void M_ResetControlsResponse(INT32 ch)
{
	const UINT8 p = setupcontrolplayer-1;
	INT32 i;

	if (ch != 'y' && ch != KEY_ENTER)
		return;

	// clear all controls
	for (i = 0; i < num_gamecontrols; i++)
	{
		G_ClearControlKeys(gamecontrol[p], i);
	}

	// Setup original defaults
	G_ResetControls(p);

	// Setup gamepad option defaults (yucky)
	CV_StealthSet(&cv_usejoystick[p], cv_usejoystick[p].defaultvalue);

	S_StartSound(NULL, sfx_s224);
}

void M_ResetControls(INT32 choice)
{
	(void)choice;
	M_StartMessage(va(M_GetText("Reset Player %d's controls to defaults?\n\n(Press 'Y' to confirm)\n"), setupcontrolplayer), M_ResetControlsResponse, MM_YESNO);
}

// ===============
// VIDEO MODE MENU
// ===============

//added : 30-01-98:
#define MAXCOLUMNMODES   12     //max modes displayed in one column
#define MAXMODEDESCS     (MAXCOLUMNMODES*3)

static modedesc_t modedescs[MAXMODEDESCS];

void M_VideoModeMenu(INT32 choice)
{
	INT32 i, j, vdup, nummodes;
	UINT32 width, height;
	const char *desc;

	(void)choice;

	memset(modedescs, 0, sizeof(modedescs));

#if (defined (__unix__) && !defined (MSDOS)) || defined (UNIXCOMMON) || defined (HAVE_SDL)
	VID_PrepareModeList(); // FIXME: hack
#endif
	vidm_nummodes = 0;
	vidm_selected = 0;
	nummodes = VID_NumModes();

	// DOS does not skip mode 0, because mode 0 is ALWAYS present
	i = 0;
	for (; i < nummodes && vidm_nummodes < MAXMODEDESCS; i++)
	{
		desc = VID_GetModeName(i);
		if (desc)
		{
			vdup = 0;

			// when a resolution exists both under VGA and VESA, keep the
			// VESA mode, which is always a higher modenum
			for (j = 0; j < vidm_nummodes; j++)
			{
				if (!strcmp(modedescs[j].desc, desc))
				{
					// mode(0): 320x200 is always standard VGA, not vesa
					if (modedescs[j].modenum)
					{
						modedescs[j].modenum = i;
						vdup = 1;

						if (i == vid.modenum)
							vidm_selected = j;
					}
					else
						vdup = 1;

					break;
				}
			}

			if (!vdup)
			{
				modedescs[vidm_nummodes].modenum = i;
				modedescs[vidm_nummodes].desc = desc;

				if (i == vid.modenum)
					vidm_selected = vidm_nummodes;

				// Pull out the width and height
				sscanf(desc, "%u%*c%u", &width, &height);

				// Show multiples of 320x200 as green.
				if (SCR_IsAspectCorrect(width, height))
					modedescs[vidm_nummodes].goodratio = 1;

				vidm_nummodes++;
			}
		}
	}

	vidm_column_size = (vidm_nummodes+2) / 3;

	M_EnterMenu(MN_OP_VIDEOMODE, true);
}

void M_DrawVideoMenu(void)
{
	M_DrawGenericMenu();

	V_DrawRightAlignedString(BASEVIDWIDTH - currentMenu->x, currentMenu->y + M_GetItemY(MN_OP_VIDEO, "SETMOD"),
		(SCR_IsAspectCorrect(vid.width, vid.height) ? recommendedflags : highlightflags),
			va("%dx%d", vid.width, vid.height));
	
#ifdef HWRENDER
	// Hide some options based on the current render mode
	if (rendermode == render_opengl)
	{
		M_SetItemStatus(MN_OP_VIDEO, "OPENGL", IT_CALL | IT_STRING);
		//M_SetItemStatus(MN_OP_VIDEO, "PARSOF", IT_DISABLED);
	}
	else
#endif
	{
		//M_SetItemStatus(MN_OP_VIDEO, "PARSOF", IT_CALL | IT_CVAR);
#ifdef HWRENDER
		M_SetItemStatus(MN_OP_VIDEO, "OPENGL", IT_DISABLED);
#endif
	}
}

void M_DrawHUDOptions(void)
{
	const char *str0 = ")";
	const char *str1 = " Warning highlight";
	const char *str2 = ",";
	const char *str3 = "Good highlight";
	INT32 x = BASEVIDWIDTH - currentMenu->x + 2, y = currentMenu->y + 90;
	INT32 w0 = V_StringWidth(str0, 0), w1 = V_StringWidth(str1, 0), w2 = V_StringWidth(str2, 0), w3 = V_StringWidth(str3, 0);

	M_DrawGenericMenu();

	x -= w0;
	V_DrawString(x, y, highlightflags, str0);
	x -= w1;
	V_DrawString(x, y, warningflags, str1);
	x -= w2;
	V_DrawString(x, y, highlightflags, str2);
	x -= w3;
	V_DrawString(x, y, recommendedflags, str3);
	V_DrawRightAlignedString(x, y, highlightflags, "(");
}

// Draw the video modes list, a-la-Quake
void M_DrawVideoMode(void)
{
	INT32 i, j, row, col;

	// draw title
	M_DrawMenuTitle();

	V_DrawCenteredString(BASEVIDWIDTH/2, menudefs[MN_OP_VIDEOMODE].y,
		highlightflags, "Choose mode, reselect to change default");

	row = 41;
	col = menudefs[MN_OP_VIDEOMODE].y + 14;
	for (i = 0; i < vidm_nummodes; i++)
	{
		if (i == vidm_selected)
			V_DrawString(row, col, highlightflags, modedescs[i].desc);
		// Show multiples of 320x200 as green.
		else
			V_DrawString(row, col, (modedescs[i].goodratio) ? recommendedflags : 0, modedescs[i].desc);

		col += 8;
		if ((i % vidm_column_size) == (vidm_column_size-1))
		{
			row += 7*13;
			col = menudefs[MN_OP_VIDEOMODE].y + 14;
		}
	}

	if (vidm_testingmode > 0)
	{
		INT32 testtime = (vidm_testingmode/TICRATE) + 1;

		M_CentreText(menudefs[MN_OP_VIDEOMODE].y + 116,
			va("Previewing mode %c%dx%d",
				(SCR_IsAspectCorrect(vid.width, vid.height)) ? 0x83 : 0x80,
				vid.width, vid.height));
		M_CentreText(menudefs[MN_OP_VIDEOMODE].y + 138,
			"Press ENTER again to keep this mode");
		M_CentreText(menudefs[MN_OP_VIDEOMODE].y + 150,
			va("Wait %d second%s", testtime, (testtime > 1) ? "s" : ""));
		M_CentreText(menudefs[MN_OP_VIDEOMODE].y + 158,
			"or press ESC to return");

	}
	else
	{
		M_CentreText(menudefs[MN_OP_VIDEOMODE].y + 116,
			va("Current mode is %c%dx%d",
				(SCR_IsAspectCorrect(vid.width, vid.height)) ? 0x83 : 0x80,
				vid.width, vid.height));
		M_CentreText(menudefs[MN_OP_VIDEOMODE].y + 124,
			va("Default mode is %c%dx%d",
				(SCR_IsAspectCorrect(cv_scr_width.value, cv_scr_height.value)) ? 0x83 : 0x80,
				cv_scr_width.value, cv_scr_height.value));

		V_DrawCenteredString(BASEVIDWIDTH/2, menudefs[MN_OP_VIDEOMODE].y + 138,
			recommendedflags, "Marked modes are recommended.");
		V_DrawCenteredString(BASEVIDWIDTH/2, menudefs[MN_OP_VIDEOMODE].y + 146,
			highlightflags, "Other modes may have visual errors.");
		V_DrawCenteredString(BASEVIDWIDTH/2, menudefs[MN_OP_VIDEOMODE].y + 158,
			highlightflags, "Larger modes may have performance issues.");
	}

	// Draw the cursor for the VidMode menu
	i = 41 - 10 + ((vidm_selected / vidm_column_size)*7*13);
	j = menudefs[MN_OP_VIDEOMODE].y + 14 + ((vidm_selected % vidm_column_size)*8);

	V_DrawScaledPatch(i - 8, j, 0,
		W_CachePatchName("M_CURSOR", PU_CACHE));
}

// special menuitem key handler for video mode list
void M_HandleVideoMode(INT32 ch)
{
	if (vidm_testingmode > 0) switch (ch)
	{
		// change back to the previous mode quickly
		case KEY_ESCAPE:
			setmodeneeded = vidm_previousmode + 1;
			vidm_testingmode = 0;
			break;

		case KEY_ENTER:
			S_StartSound(NULL, sfx_menu1);
			vidm_testingmode = 0; // stop testing
	}

	else switch (ch)
	{
		case KEY_DOWNARROW:
			S_StartSound(NULL, sfx_menu1);
			if (++vidm_selected >= vidm_nummodes)
				vidm_selected = 0;
			break;

		case KEY_UPARROW:
			S_StartSound(NULL, sfx_menu1);
			if (--vidm_selected < 0)
				vidm_selected = vidm_nummodes - 1;
			break;

		case KEY_LEFTARROW:
			S_StartSound(NULL, sfx_menu1);
			vidm_selected -= vidm_column_size;
			if (vidm_selected < 0)
				vidm_selected = (vidm_column_size*3) + vidm_selected;
			if (vidm_selected >= vidm_nummodes)
				vidm_selected = vidm_nummodes - 1;
			break;

		case KEY_RIGHTARROW:
			S_StartSound(NULL, sfx_menu1);
			vidm_selected += vidm_column_size;
			if (vidm_selected >= (vidm_column_size*3))
				vidm_selected %= vidm_column_size;
			if (vidm_selected >= vidm_nummodes)
				vidm_selected = vidm_nummodes - 1;
			break;

		case KEY_ENTER:
			S_StartSound(NULL, sfx_menu1);
			if (vid.modenum == modedescs[vidm_selected].modenum)
				SCR_SetDefaultMode();
			else
			{
				vidm_testingmode = 15*TICRATE;
				vidm_previousmode = vid.modenum;
				if (!setmodeneeded) // in case the previous setmode was not finished
					setmodeneeded = modedescs[vidm_selected].modenum + 1;
			}
			break;

		case KEY_ESCAPE: // this one same as M_Responder
			M_ExitMenu();
			break;

		default:
			break;
	}
}

// ===============
// Monitor Toggles
// ===============

static tic_t shitsfree = 0;

void M_DrawMonitorToggles(void)
{
	const INT32 edges = 4;
	const INT32 height = 4;
	const INT32 spacing = 35;
	const INT32 column = itemOn/height;
	//const INT32 row = itemOn%height;
	INT32 leftdraw, rightdraw, totaldraw;
	INT32 x = currentMenu->x, y = currentMenu->y+(spacing/4);
	INT32 onx = 0, ony = 0;
	consvar_t *cv;
	INT32 i, translucent, drawnum;

	M_DrawMenuTitle();

	// Find the available space around column
	leftdraw = rightdraw = column;
	totaldraw = 0;
	for (i = 0; (totaldraw < edges*2 && i < edges*4); i++)
	{
		if (rightdraw+1 < (currentMenu->numitems/height)+1)
		{
			rightdraw++;
			totaldraw++;
		}
		if (leftdraw-1 >= 0)
		{
			leftdraw--;
			totaldraw++;
		}
	}

	for (i = leftdraw; i <= rightdraw; i++)
	{
		INT32 j;

		for (j = 0; j < height; j++)
		{
			const INT32 thisitem = (i*height)+j;

			if (thisitem >= currentMenu->numitems)
				continue;

			if (thisitem == itemOn)
			{
				onx = x;
				ony = y;
				y += spacing;
				continue;
			}

#ifdef ITEMTOGGLEBOTTOMRIGHT
			if (currentMenu->menuitems[thisitem].alphaKey == 255)
			{
				V_DrawScaledPatch(x, y, V_TRANSLUCENT, W_CachePatchName("K_ISBG", PU_CACHE));
				continue;
			}
#endif
			if (currentMenu->menuitems[thisitem].alphaKey == 0)
			{
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISBG", PU_CACHE));
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISTOGL", PU_CACHE));
				continue;
			}

			cv = KartItemCVars[currentMenu->menuitems[thisitem].alphaKey-1];
			translucent = (cv->value ? 0 : V_TRANSLUCENT);

			switch (currentMenu->menuitems[thisitem].alphaKey)
			{
				case KRITEM_DUALSNEAKER:
				case KRITEM_DUALJAWZ:
					drawnum = 2;
					break;
				case KRITEM_TRIPLESNEAKER:
				case KRITEM_TRIPLEBANANA:
				case KRITEM_TRIPLEORBINAUT:
					drawnum = 3;
					break;
				case KRITEM_QUADORBINAUT:
					drawnum = 4;
					break;
				case KRITEM_TENFOLDBANANA:
					drawnum = 10;
					break;
				default:
					drawnum = 0;
					break;
			}

			if (cv->value)
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISBG", PU_CACHE));
			else
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISBGD", PU_CACHE));

			if (drawnum != 0)
			{
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISMUL", PU_CACHE));
				V_DrawScaledPatch(x, y, translucent, W_CachePatchName(K_GetItemPatch(currentMenu->menuitems[thisitem].alphaKey, true), PU_CACHE));
				V_DrawString(x+24, y+31, V_ALLOWLOWERCASE|translucent, va("x%d", drawnum));
			}
			else
				V_DrawScaledPatch(x, y, translucent, W_CachePatchName(K_GetItemPatch(currentMenu->menuitems[thisitem].alphaKey, true), PU_CACHE));

			y += spacing;
		}

		x += spacing;
		y = currentMenu->y+(spacing/4);
	}

	{
#ifdef ITEMTOGGLEBOTTOMRIGHT
		if (currentMenu->menuitems[itemOn].alphaKey == 255)
		{
			V_DrawScaledPatch(onx-1, ony-2, V_TRANSLUCENT, W_CachePatchName("K_ITBG", PU_CACHE));
			if (shitsfree)
			{
				INT32 trans = V_TRANSLUCENT;
				if (shitsfree-1 > TICRATE-5)
					trans = ((10-TICRATE)+shitsfree-1)<<V_ALPHASHIFT;
				else if (shitsfree < 5)
					trans = (10-shitsfree)<<V_ALPHASHIFT;
				V_DrawScaledPatch(onx-1, ony-2, trans, W_CachePatchName("K_ITFREE", PU_CACHE));
			}
		}
		else
#endif
		if (currentMenu->menuitems[itemOn].alphaKey == 0)
		{
			V_DrawScaledPatch(onx-1, ony-2, 0, W_CachePatchName("K_ITBG", PU_CACHE));
			V_DrawScaledPatch(onx-1, ony-2, 0, W_CachePatchName("K_ITTOGL", PU_CACHE));
		}
		else
		{
			cv = KartItemCVars[currentMenu->menuitems[itemOn].alphaKey-1];
			translucent = (cv->value ? 0 : V_TRANSLUCENT);

			switch (currentMenu->menuitems[itemOn].alphaKey)
			{
				case KRITEM_TENFOLDBANANA:
					drawnum = 10;
					break;
				default:
					drawnum = 0;
					break;
			}

			if (cv->value)
				V_DrawScaledPatch(onx-1, ony-2, 0, W_CachePatchName("K_ITBG", PU_CACHE));
			else
				V_DrawScaledPatch(onx-1, ony-2, 0, W_CachePatchName("K_ITBGD", PU_CACHE));

			if (drawnum != 0)
			{
				V_DrawScaledPatch(onx-1, ony-2, 0, W_CachePatchName("K_ITMUL", PU_CACHE));
				V_DrawScaledPatch(onx-1, ony-2, translucent, W_CachePatchName(K_GetItemPatch(currentMenu->menuitems[itemOn].alphaKey, false), PU_CACHE));
				V_DrawScaledPatch(onx+27, ony+39, translucent, W_CachePatchName("K_ITX", PU_CACHE));
				V_DrawKartString(onx+37, ony+34, translucent, va("%d", drawnum));
			}
			else
				V_DrawScaledPatch(onx-1, ony-2, translucent, W_CachePatchName(K_GetItemPatch(currentMenu->menuitems[itemOn].alphaKey, false), PU_CACHE));
		}
	}

	if (shitsfree)
		shitsfree--;

	V_DrawCenteredString(BASEVIDWIDTH/2, currentMenu->y, highlightflags, va("* %s *", currentMenu->menuitems[itemOn].text));
}

void M_HandleMonitorToggles(INT32 choice)
{
	const INT32 width = 6, height = 4;
	INT32 column = itemOn/height, row = itemOn%height;
	INT16 next;
	UINT8 i;
	boolean exitmenu = false;

	switch (choice)
	{
		case KEY_RIGHTARROW:
			S_StartSound(NULL, sfx_menu1);
			column++;
			if (((column*height)+row) >= currentMenu->numitems)
				column = 0;
			next = min(((column*height)+row), currentMenu->numitems-1);
			itemOn = next;
			break;

		case KEY_LEFTARROW:
			S_StartSound(NULL, sfx_menu1);
			column--;
			if (column < 0)
				column = width;
			if (((column*height)+row) >= currentMenu->numitems)
				column--;
			next = max(((column*height)+row), 0);
			if (next >= currentMenu->numitems)
				next = currentMenu->numitems-1;
			itemOn = next;
			break;

		case KEY_DOWNARROW:
			S_StartSound(NULL, sfx_menu1);
			row = (row+1) % height;
			if (((column*height)+row) >= currentMenu->numitems)
				row = 0;
			next = min(((column*height)+row), currentMenu->numitems-1);
			itemOn = next;
			break;

		case KEY_UPARROW:
			S_StartSound(NULL, sfx_menu1);
			row = (row-1) % height;
			if (row < 0)
				row = height-1;
			if (((column*height)+row) >= currentMenu->numitems)
				row--;
			next = max(((column*height)+row), 0);
			if (next >= currentMenu->numitems)
				next = currentMenu->numitems-1;
			itemOn = next;
			break;

		case KEY_ENTER:
#ifdef ITEMTOGGLEBOTTOMRIGHT
			if (currentMenu->menuitems[itemOn].alphaKey == 255)
			{
				//S_StartSound(NULL, sfx_s26d);
				if (!shitsfree)
				{
					shitsfree = TICRATE;
					S_StartSound(NULL, sfx_itfree);
				}
			}
			else
#endif
			if (currentMenu->menuitems[itemOn].alphaKey == 0)
			{
				INT32 v = cv_sneaker.value;
				S_StartSound(NULL, sfx_s1b4);
				for (i = 0; i < NUMKARTRESULTS-1; i++)
				{
					if (KartItemCVars[i]->value == v)
						CV_AddValue(KartItemCVars[i], 1);
				}
			}
			else
			{
				S_StartSound(NULL, sfx_s1ba);
				CV_AddValue(KartItemCVars[currentMenu->menuitems[itemOn].alphaKey-1], 1);
			}
			break;

		case KEY_ESCAPE:
			exitmenu = true;
			break;
	}

	if (exitmenu)
		M_ExitMenu();
}

// =========
// Quit Game
// =========
static INT32 quitsounds[] =
{
	// holy shit we're changing things up!
	// srb2kart: you ain't seen nothing yet
	sfx_kc2e,
	sfx_kc2f,
	sfx_cdfm01,
	sfx_ddash,
	sfx_s3ka2,
	sfx_s3k49,
	sfx_slip,
	sfx_tossed,
	sfx_s3k7b,
	sfx_itrolf,
	sfx_itrole,
	sfx_cdpcm9,
	sfx_s3k4e,
	sfx_s259,
	sfx_3db06,
	sfx_s3k3a,
	sfx_peel,
	sfx_cdfm28,
	sfx_s3k96,
	sfx_s3kc0s,
	sfx_cdfm39,
	sfx_hogbom,
	sfx_kc5a,
	sfx_kc46,
	sfx_s3k92,
	sfx_s3k42,
	sfx_kpogos,
	sfx_screec
};

void M_QuitResponse(INT32 ch)
{
	tic_t ptime;
	INT32 mrand;

	if (ch != 'y' && ch != KEY_ENTER)
		return;
	if (!(netgame || cht_debug))
	{
		mrand = M_RandomKey(sizeof(quitsounds)/sizeof(INT32));
		if (quitsounds[mrand]) S_StartSound(NULL, quitsounds[mrand]);

		//added : 12-02-98: do that instead of I_WaitVbl which does not work
		ptime = I_GetTime() + NEWTICRATE*2; // Shortened the quit time, used to be 2 seconds Tails 03-26-2001
		while (ptime > I_GetTime())
		{
			V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);
			V_DrawSmallScaledPatch(0, 0, 0, W_CachePatchName("GAMEQUIT", PU_CACHE)); // Demo 3 Quit Screen Tails 06-16-2001
			I_FinishUpdate(); // Update the screen with the image Tails 06-19-2001
			I_Sleep(cv_sleep.value);
			I_UpdateTime(cv_timescale.value);
		}
	}
	I_Quit();
}

void M_QuitSRB2(INT32 choice)
{
	// We pick index 0 which is language sensitive, or one at random,
	// between 1 and maximum number.
	(void)choice;
	M_StartMessage(quitmsg[M_RandomKey(NUM_QUITMESSAGES)], M_QuitResponse, MM_YESNO);
}

#ifdef HAVE_DISCORDRPC
static const tic_t confirmLength = 3*TICRATE/4;
static tic_t confirmDelay = 0;
static boolean confirmAccept = false;

void M_HandleDiscordRequests(INT32 choice)
{
	if (confirmDelay > 0)
		return;

	switch (choice)
	{
		case KEY_ENTER:
			Discord_Respond(discordRequestList->userID, DISCORD_REPLY_YES);
			confirmAccept = true;
			confirmDelay = confirmLength;
			S_StartSound(NULL, sfx_s3k63);
			break;

		case KEY_ESCAPE:
			Discord_Respond(discordRequestList->userID, DISCORD_REPLY_NO);
			confirmAccept = false;
			confirmDelay = confirmLength;
			S_StartSound(NULL, sfx_s3kb2);
			break;
	}
}

static const char *M_GetDiscordName(discordRequest_t *r)
{
	if (r == NULL)
		return "";

	if (cv_discordstreamer.value)
		return r->username;

	return va("%s#%s", r->username, r->discriminator);
}

// (this goes in k_hud.c when merged into v2)
static void M_DrawSticker(INT32 x, INT32 y, INT32 width, INT32 flags, boolean isSmall)
{
	patch_t *stickerEnd;
	INT32 height;

	if (isSmall == true)
	{
		stickerEnd = W_CachePatchName("K_STIKE2", PU_CACHE);
		height = 6;
	}
	else
	{
		stickerEnd = W_CachePatchName("K_STIKEN", PU_CACHE);
		height = 11;
	}

	V_DrawFixedPatch(x*FRACUNIT, y*FRACUNIT, FRACUNIT, flags, stickerEnd, NULL);
	V_DrawFill(x, y, width, height, 24|flags);
	V_DrawFixedPatch((x + width)*FRACUNIT, y*FRACUNIT, FRACUNIT, flags|V_FLIP, stickerEnd, NULL);
}

void M_DrawDiscordRequests(void)
{
	discordRequest_t *curRequest = discordRequestList;
	UINT8 *colormap;
	patch_t *hand = NULL;
	boolean removeRequest = false;

	const char *wantText = "...would like to join!";
	const char *controlText = "\x82" "ENTER" "\x80" " - Accept    " "\x82" "ESC" "\x80" " - Decline";

	INT32 x = 100;
	INT32 y = 133;

	INT32 slide = 0;
	INT32 maxYSlide = 18;

	if (confirmDelay > 0)
	{
		if (confirmAccept == true)
		{
			colormap = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_GREEN, GTC_MENUCACHE);
			hand = W_CachePatchName("K_LAPH02", PU_CACHE);
		}
		else
		{
			colormap = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_RED, GTC_MENUCACHE);
			hand = W_CachePatchName("K_LAPH03", PU_CACHE);
		}

		slide = confirmLength - confirmDelay;

		confirmDelay--;

		if (confirmDelay == 0)
			removeRequest = true;
	}
	else
	{
		colormap = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_GREY, GTC_MENUCACHE);
	}

	V_DrawFixedPatch(56*FRACUNIT, 150*FRACUNIT, FRACUNIT, 0, W_CachePatchName("K_LAPE01", PU_CACHE), colormap);

	if (hand != NULL)
	{
		fixed_t handoffset = (4 - abs((signed)(skullAnimCounter - 4))) * FRACUNIT;
		V_DrawFixedPatch(56*FRACUNIT, 150*FRACUNIT + handoffset, FRACUNIT, 0, hand, NULL);
	}

	M_DrawSticker(x + (slide * 32), y - 1, V_ThinStringWidth(M_GetDiscordName(curRequest), V_ALLOWLOWERCASE|V_6WIDTHSPACE), 0, false);
	V_DrawThinString(x + (slide * 32), y, V_ALLOWLOWERCASE|V_6WIDTHSPACE|V_YELLOWMAP, M_GetDiscordName(curRequest));

	M_DrawSticker(x, y + 12, V_ThinStringWidth(wantText, V_ALLOWLOWERCASE|V_6WIDTHSPACE), 0, true);
	V_DrawThinString(x, y + 10, V_ALLOWLOWERCASE|V_6WIDTHSPACE, wantText);

	M_DrawSticker(x, y + 26, V_ThinStringWidth(controlText, V_ALLOWLOWERCASE|V_6WIDTHSPACE), 0, true);
	V_DrawThinString(x, y + 24, V_ALLOWLOWERCASE|V_6WIDTHSPACE, controlText);

	y -= 18;

	while (curRequest->next != NULL)
	{
		INT32 ySlide = min(slide * 4, maxYSlide);

		curRequest = curRequest->next;

		M_DrawSticker(x, y - 1 + ySlide, V_ThinStringWidth(M_GetDiscordName(curRequest), V_ALLOWLOWERCASE|V_6WIDTHSPACE), 0, false);
		V_DrawThinString(x, y + ySlide, V_ALLOWLOWERCASE|V_6WIDTHSPACE, M_GetDiscordName(curRequest));

		y -= 12;
		maxYSlide = 12;
	}

	if (removeRequest == true)
	{
		DRPC_RemoveRequest(discordRequestList);

		if (discordRequestList == NULL)
		{
			// No other requests
			M_SetItemStatus(MN_MPAUSE, "DISCRQ", IT_GRAYEDOUT);

			M_ExitMenu();
			if (menustack[0] == MN_MPAUSE)
				M_SetItemOn(MN_MPAUSE, "CONTIN");

			return;
		}
	}
}
#endif
