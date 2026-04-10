// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2011-2016 by Matthew "Kaito Sinclaire" Walsh.
// Copyright (C) 1999-2025 by Sonic Team Junior.
// Copyright (C) 2026 by Team BlanKart.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  m_menu.c
/// \brief XMOD's extremely revamped menu system, revamped again for BlanKart

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
#include "v_video.h"
#include "m_argv.h"
#include "m_textinput.h"

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
#include "f_dscredits.hpp" // BlanKart: Secret credits

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

#include "d_net.h"
#include "i_net.h"
#include "mserv.h"
#include "m_misc.h"
#include "m_anigif.h"
#include "byteptr.h"
#include "st_stuff.h"
#include "i_sound.h"
#include "k_hud.h" // SRB2kart
#include "k_kart.h"
#include "k_items.h" // KartItemCVars
#include "k_pwrlv.h"
#include "k_stats.h" // SRB2kart
#include "d_player.h" // KITEM_ constants
#include "k_color.h"
#include "k_grandprix.h"
#include "k_follower.h"
#include "r_fps.h"
#include "m_easing.h"

#include "i_gamepad.h" // for joystick menu controls

// Condition Sets
#include "m_cond.h"

// And just some randomness for the exits.
#include "m_random.h"

#include "deh_tables.h" // menunames

#if defined(HAVE_SDL)
#include <SDL3/SDL.h>
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

static char menu_text_input_buf[MAXSTRINGLENGTH];
static textinput_t menuinput;

static levellistmode_e levellistmode = LLM_CREATESERVER;

static char joystickInfo[MAXGAMEPADS+1][29];

INT16 startmap; // Mario, NiGHTS, or just a plain old normal game?

menu_t menudefs[MAXMENUTYPES]; // array of all menudefs
UINT16 nummenutypes;
menutype_t menustack[NUMMENULEVELS]; // stack of active menus, [0] == current menu
menutype_t restoremenustack[NUMMENULEVELS]; // stack of active menus, [0] == current menu
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

// level select platter
#define MAPSPERROW 5 // map sperrow?
#define MAXMAPNAME 21+1+21+1+2+1 // lvlttl + SPACE + zonttl + SPACE + actnum + NUL

typedef struct
{
	mapnum_t mapnum;
	char mapname[MAXMAPNAME];
	UINT8 color;
	boolean available;
} levelselectmap_t;

typedef struct
{
	char header[MAXMAPNAME];
	boolean wide;
	UINT16 cupid;
	UINT8 numcolumns;
	levelselectmap_t maps[MAPSPERROW];
} levelselectrow_t;

static struct levelselect_t
{
	UINT8 row;
	UINT8 column;
	UINT8 highlight;
	INT32 namescroll;
	fixed_t offsetx, offsety;

	INT16 randomrow, randomcol;
	INT16 returnto;
	INT16 firstmenuitem, lastmenuitem;
	tic_t selectdelay;

	UINT8 numrows;
	levelselectrow_t *rows;
} levelselect = {0};

#define lsmapwidth 80
#define lsmapheight 50
#define lsmapnameheight 8
#define lshseperation (lsmapwidth - 25)
#define lshseperationcup (lsmapwidth/2 + 2)
#define lsbasevseperation (3*(lsmapheight + lsmapnameheight)/4 + 1)
#define lsheadingheight 28
#define getheadingoffset(row) (levelselect.rows[row].header[0] ? lsheadingheight : 0)
#define lsvseperation(row) (lsbasevseperation + getheadingoffset(row))

#define lsbasex (BASEVIDWIDTH/2 - lshseperation*(MAPSPERROW-1)/2)
#define lsbasey (BASEVIDHEIGHT/2 - 2)

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

// uhhhhhh hack?
static void M_ChangecontrolResponse(event_t *ev);

// Consvar onchange functions
static void Nextmap_OnChange(void);
static void Nextcup_OnChange(void);
static void Levelplatter_OnChange(void);
static void Dummymenuplayer_OnChange(void);
static void Dummystaff_OnChange(void);

// menus now use short names rather than hardcoded indices to identify menuitems
menuitem_t *M_CheckMenuItem(menutype_t type, const char *name)
{
	INT16 i;
	menu_t *menu = &menudefs[type];
	UINT32 hash = HASH32(name, strlen(name));

	for (i = 0; i < menu->numitems; i++) {
		if (hash != menu->menuitems[i].info.namehash)
			continue;
		if (fastcmp(name, strbuf_get(menunames, menu->menuitems[i].info.nameofs)))
			return &menu->menuitems[i];
	}
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

static boolean M_ItemSelectable(menuitem_t *item)
{
	return (item->status & IT_INTERACT) && !(item->status & (IT_HIDDEN|IT_GRAYEDOUT|IT_SECRET));
}

// TODO this ought to be controlled by the item argument...
static UINT32 M_StringCvarLength(consvar_t *cv)
{
	if (cv == &cv_dummyip)
		return 28;
	else if (cv == &cv_dummyname || cv == &cv_playername[0] || cv == &cv_playername[1] || cv == &cv_playername[2] || cv == &cv_playername[3])
		return MAXPLAYERNAME + 1;
	else
		return MAXSTRINGLENGTH;
}

// an array of macros for getting/setting menuitem properties
#define M_IsItemOn(t, n)                (itemOn == M_GetMenuIndex(t, n))
#define M_SetItemOn(t, n)               (M_RawSetItemOn(&menudefs[t], M_GetMenuIndex(t, n)))
#define M_SetItemRoutine(t, n, v)       (M_GetMenuItem(t, n)->routine = v)
#define M_SetItemCvar(t, n, v)          (M_GetMenuItem(t, n)->cvar = v)
#define M_SetItemArgument(t, n, v)      (M_GetMenuItem(t, n)->argument = v)
#define M_SetItemX(t, n, v)             (M_GetMenuItem(t, n)->x = v)
#define M_SetItemY(t, n, v)             (M_GetMenuItem(t, n)->y = v)
#define M_GetItemX(t, n)                (M_GetMenuItem(t, n)->x)
#define M_GetItemY(t, n)                (M_GetMenuItem(t, n)->y)

static void M_RawSetItemOn(menu_t *menu, INT16 index)
{
	INT16 i;
	itemOn = index;

	if (!menu->numitems)
		return;

	// in case of...
	if (itemOn >= menu->numitems)
		itemOn = menu->numitems - 1;

	// the curent item can be disabled,
	// this code go up until an enabled item found
	if (menu->numitems && !M_ItemSelectable(&menu->menuitems[itemOn]))
	{
		for (i = 0; i < menu->numitems; i++)
		{
			if (M_ItemSelectable(&menu->menuitems[i]))
			{
				itemOn = i;
				break;
			}
		}
	}

	// update text input for string cvars
	consvar_t *cv = menu->menuitems[itemOn].cvar;
	if (cv && !cv->PossibleValue)
	{
		M_TextInputInit(&menuinput, menu_text_input_buf, M_StringCvarLength(cv));
		M_TextInputSetString(&menuinput, cv->string);
		I_SetTextInput(true);
	}
	else
	{
		I_SetTextInput(false);
	}
}

static void M_ChangeItemStatus(menutype_t type, const char *name, menuitemflags_t flag, boolean cond)
{
	if (cond)
		M_GetMenuItem(type, name)->status |= flag;
	else
		M_GetMenuItem(type, name)->status &= ~flag;
}

static void M_SetItemText(menutype_t type, const char *name, const char *string)
{
	menuitem_t *item = M_GetMenuItem(type, name);
	if (item->text)
		Z_Free(item->text);
	item->text = Z_StrDup(string);
}

static void M_SetItemPatch(menutype_t type, const char *name, const char *string)
{
	menuitem_t *item = M_GetMenuItem(type, name);
	if (item->patch)
		Z_Free(item->patch);
	item->patch = Z_StrDup(string);
}

#define M_SetItemVisible(t, n, c)  M_ChangeItemStatus(t, n, IT_HIDDEN, !(c))
#define M_SetItemDisabled(t, n, c) M_ChangeItemStatus(t, n, IT_GRAYEDOUT, c)
#define M_SetItemSecret(t, n, c)   M_ChangeItemStatus(t, n, IT_SECRET, c)

// bruh...
static UINT32 M_ServersPerPage(void)
{
	INT32 spp = M_GetMenuIndex(MN_MP_CONNECT, "LINEMAX") - M_GetMenuIndex(MN_MP_CONNECT, "LINE1") + 1;
	if (spp < 1 || spp >= MAXSERVERLIST)
		I_Error("Broken server connection menu");
	return (UINT32)spp;
}

// TODO: port cup->realname
static const char *CupName(const cupheader_t *cup, boolean suffix)
{
	const char *name = strchr(cup->name, '_');
	if (name != NULL)
		name++;
	else
		name = cup->name;

	// extra cursed so you really want to remove this hack!
	char *s = va("%s", name);
	char *ret = s;
	while (*s++ != '\0')
		if (*s == '_')
			*s = ' ';
	return suffix ? strcat(ret, " Cup") : ret;
}

// ==========================================================================
// CONSOLE VARIABLES AND THEIR POSSIBLE VALUES GO HERE.
// ==========================================================================

consvar_t cv_showfocuslost = CVAR_INIT ("showfocuslost", "Yes", CV_SAVE, CV_YesNo, NULL);

consvar_t cv_nextmap = CVAR_INIT ("nextmap", "0", CV_HIDEN|CV_CALL|CV_NOINIT, CV_Unsigned, Nextmap_OnChange);
consvar_t cv_nextcup = CVAR_INIT ("nextcup", "0", CV_HIDEN|CV_CALL|CV_NOINIT, CV_Unsigned, Nextcup_OnChange);

static CV_PossibleValue_t skins_cons_t[] = {{0, "MIN"}, {MAXSKINS, "MAX"}, {0, NULL}};
consvar_t cv_chooseskin = CVAR_INIT ("chooseskin", "0", CV_HIDEN, skins_cons_t, NULL);

static CV_PossibleValue_t skinselectstyle_cons_t[] = {{0, "Bar"}, {1, "Grid"}, {0, NULL}};
consvar_t cv_skinselectstyle = CVAR_INIT ("skinselectstyle", "Grid", CV_SAVE|CV_CALL|CV_NOINIT, skinselectstyle_cons_t, Skinselectstyle_option_Onchange);

static CV_PossibleValue_t skinselectsort_cons_t[] = {
	{SKINMENUSORT_ID, 		"Order Added"},
	{SKINMENUSORT_NAME, 	"Name"},
	{SKINMENUSORT_SPEED, 	"Speed"},
	{SKINMENUSORT_WEIGHT, 	"Weight"},
	{SKINMENUSORT_ACCEL, 	"Accel"},
	{SKINMENUSORT_HANDLING, "Handling"},
	{SKINMENUSORT_PREFCOLOR, "Preferred Colour"},
	{0, NULL}
};
consvar_t cv_skinselectsort = CVAR_INIT ("skinselectsort", "Name", CV_SAVE|CV_CALL|CV_NOINIT, skinselectsort_cons_t, Skinsort_option_Onchange);

// This gametype list is integral for many different reasons.
// When you add gametypes here, don't forget to update them in dehacked.c and doomstat.h!
CV_PossibleValue_t gametype_cons_t[MAXGAMETYPES+1];

consvar_t cv_newgametype = CVAR_INIT ("newgametype", "Race", CV_HIDEN|CV_CALL|CV_NOINIT, gametype_cons_t, Levelplatter_OnChange);

enum {
	LEVELSORT_FILE,
	LEVELSORT_NAME,
	LEVELSORT_ID,
	LEVELSORT_CUP, // MUST BE LAST because of hack in MR_PrepareLevelPlatter
};
static CV_PossibleValue_t levelsort_cons_t[] = {
	{LEVELSORT_FILE, "Addon"},
	{LEVELSORT_NAME, "Name"},
	{LEVELSORT_ID,   "Order Added"},
	{LEVELSORT_CUP,  "Cup"},
	{0, NULL}
};
consvar_t cv_levelsort = CVAR_INIT ("levelsort", "Addon", CV_HIDEN|CV_CALL|CV_NOINIT, levelsort_cons_t, Levelplatter_OnChange);

static consvar_t cv_showallmaps = CVAR_INIT ("showallmaps", "No", CV_SAVE, CV_YesNo, NULL);

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
static CV_PossibleValue_t dummystaff_cons_t[] = {{0, "MIN"}, {100, "MAX"}, {0, NULL}};

consvar_t cv_dummymenuplayer = CVAR_INIT ("dummymenuplayer", "P1", CV_HIDEN|CV_CALL, dummymenuplayer_cons_t, Dummymenuplayer_OnChange);
consvar_t cv_dummyteam = CVAR_INIT ("dummyteam", "Spectator", CV_HIDEN, dummyteam_cons_t, NULL);
consvar_t cv_dummyspectate = CVAR_INIT ("dummyspectate", "Spectator", CV_HIDEN, dummyspectate_cons_t, NULL);
consvar_t cv_dummyscramble = CVAR_INIT ("dummyscramble", "Random", CV_HIDEN, dummyscramble_cons_t, NULL);
consvar_t cv_dummystaff = CVAR_INIT ("dummystaff", "0", CV_HIDEN|CV_CALL, dummystaff_cons_t, Dummystaff_OnChange);

consvar_t cv_dummygpdifficulty = CVAR_INIT ("dummygpdifficulty", "Normal", CV_HIDEN, gpdifficulty_cons_t, NULL);
consvar_t cv_dummygpencore = CVAR_INIT ("dummygpencore", "Off", CV_HIDEN, CV_OnOff, NULL);

static CV_PossibleValue_t dummymultiplayer_cons_t[] = {{0, "MIN"}, {3, "MAX"}, {0, NULL}};
static CV_PossibleValue_t dummyfollower_cons_t[] = {{-1, "MIN"}, {MAXFOLLOWERS, "MAX"}, {0, NULL}};
static CV_PossibleValue_t dummycolor_cons_t[] = {{0, "MIN"}, {MAXSKINCOLORS, "MAX"}, {0, NULL}};

consvar_t cv_dummymultiplayer = CVAR_INIT ("dummymultiplayer", "0", CV_HIDEN, dummymultiplayer_cons_t, NULL);
consvar_t cv_dummyip = CVAR_INIT ("dummyip", "", CV_HIDEN, NULL, NULL);
consvar_t cv_dummyname = CVAR_INIT ("dummyname", "", CV_HIDEN, NULL, NULL);
consvar_t cv_dummyfollower = CVAR_INIT ("dummyfollower", "-1", CV_HIDEN, dummyfollower_cons_t, NULL);

SINT8 dummycolorplayer = 0;
static void SetDummyColorPlayer(void)
{
	G_SetPlayerControllerIndicatorColor(dummycolorplayer, (UINT16)cv_dummycolor.value);
}

consvar_t cv_dummycolor = CVAR_INIT ("dummycolor", "0", CV_HIDEN|CV_CALL|CV_NOINIT, dummycolor_cons_t, SetDummyColorPlayer);

static CV_PossibleValue_t dummyserverpage_cons_t[] = {{0, "MIN"}, {0, "MAX"}, {0, NULL}};
consvar_t cv_dummyserverpage = CVAR_INIT ("dummyserverpage", "0", CV_HIDEN, dummyserverpage_cons_t, NULL);

consvar_t cv_menucaps = CVAR_INIT ("menucaps", "Off", CV_SAVE, CV_OnOff, NULL);

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
	INT16 gt, i;
	boolean usenewgametype = false;

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

	if (modeattacking)
	{
		highlightflags = V_ORANGEMAP;
		return;
	}

	if (currentMenu) for (i = 0; i < currentMenu->numitems; i++)
		if (currentMenu->menuitems[i].cvar == &cv_newgametype)
		{
			usenewgametype = true;
			break;
		}

	if (usenewgametype && levellistmode != LLM_CUPSELECT)
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
		highlightflags = gametypes[gt]->color;
		warningflags = V_ORANGEMAP;
		return;
	}
	if (gt == GT_RACE)
	{
		highlightflags = gametypes[gt]->color;
		return;
	}

	if (gametypes[gt]->color)
	{
		highlightflags = gametypes[gt]->color;
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

fixed_t M_GetMapThumbnail(mapnum_t mapnum, patch_t **out)
{
	patch_t *patch = NULL;
	if (mapnum == NEXTMAP_INVALID)
		patch = randomlvl;
	else if (/*mapnum >= 0 &&*/ mapnum < nummapheaders && mapheaderinfo[mapnum])
		patch = mapheaderinfo[mapnum]->thumbnailPic;

	if (!patch)
		patch = blanklvl;

	*out = patch;

	// check width instead of height because haha big winton
	return patch->width >= 320 ? FRACUNIT : FRACUNIT*2;
}

// Options
#ifdef HWRENDER
INT32 MR_OpenGLOptionsMenu(INT32 choice)
{
	(void)choice;

	if (rendermode != render_opengl)
	{
		M_StartMessage(M_GetText("You must be in OpenGL mode\nto access this menu.\n\n(Press a key)\n"), NULL, MM_NOTHING);
		return false;
	}

	return true;
}
#endif

void M_UpdateNumServerPages(void)
{
	dummyserverpage_cons_t[1].value = serverlistcount ? (serverlistcount - 1)/M_ServersPerPage() : 0;
}

// ==========================================================================
// CVAR ONCHANGE EVENTS GO HERE
// ==========================================================================
// (there's only a couple anyway)

// Prototypes
static void M_UpdateLevelPlatter(void);

#define ADD(cv, str) \
if (cv.value) \
{ \
  len += 3; \
  new_str = Z_Realloc(new_str, len, PU_STATIC, NULL); \
  strcat(new_str, str); \
}

#define ADDEQUALS(cv, str, num) \
if (cv.value == num) \
{ \
  len += 3; \
  new_str = Z_Realloc(new_str, len, PU_STATIC, NULL); \
  strcat(new_str, str); \
}

// ADD if Greater or Equal
#define ADDGE(cv, str, num) \
if (cv.value >= num) \
{ \
  len += 3; \
  new_str = Z_Realloc(new_str, len, PU_STATIC, NULL); \
  strcat(new_str, str); \
}

// Nextmap.  Used for Time Attack.
static void Nextmap_OnChange(void)
{
	// Update the string in the consvar.
	Z_Free(cv_nextmap.zstring);
	cv_nextmap.string = cv_nextmap.zstring = cv_nextmap.value ? G_BuildMapTitle(cv_nextmap.value) : Z_StrDup("Random");
}

static void Nextcup_OnChange(void)
{
	cupheader_t *cup;
	for (cup = kartcupheaders; cup != NULL; cup = cup->next)
		if (cup->id == cv_nextcup.value-1)
			break;

	// Update the string in the consvar.
	Z_Free(cv_nextcup.zstring);
	cv_nextcup.string = cv_nextcup.zstring = cv_nextcup.value ? Z_StrDup(CupName(cup, true)) : Z_StrDup("Random");
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

static void Levelplatter_OnChange(void)
{
	M_UpdateLevelPlatter();
}

void Screenshot_option_Onchange(void)
{
	M_SetItemVisible(MN_OP_SCREENSHOTS, "FOLDER", cv_screenshot_option.value == 3);
}

void Moviemode_mode_Onchange(void)
{
	M_SetItemVisible(MN_OP_SCREENSHOTS, "GIFOPTIMIZE", cv_moviemode.value == MM_GIF);
	M_SetItemVisible(MN_OP_SCREENSHOTS, "GIFDOWNSCALE", cv_moviemode.value == MM_GIF);
	M_SetItemVisible(MN_OP_SCREENSHOTS, "APNGMEMORY", cv_moviemode.value == MM_APNG);
	M_SetItemVisible(MN_OP_SCREENSHOTS, "APNGCOMPRESSION", cv_moviemode.value == MM_APNG);
	M_SetItemVisible(MN_OP_SCREENSHOTS, "APNGSTRATEGY", cv_moviemode.value == MM_APNG);
	M_SetItemVisible(MN_OP_SCREENSHOTS, "APNGWINDOW", cv_moviemode.value == MM_APNG);
}

void Addons_option_Onchange(void)
{
	M_SetItemVisible(MN_OP_ADDONS, "FOLDER", cv_addons_option.value == 3);
}

void Moviemode_option_Onchange(void)
{
	;
}


//width now 9 so we can make moe's "2D" grid just an option in the normal grid
#define SKINGRIDWIDTH 9
#define SKINGRIDHEIGHT 6

static INT32 gridcss_skinydrag;
static INT32 gridcss_skinmemory;
static INT32 gridcss_row;
static INT32 gridcss_column;

void Skinsort_option_Onchange(void)
{
	SortSkins();
	INT32 sortedIndex = FindSortedSkinIndex(cv_chooseskin.value);
	gridcss_row = sortedIndex % SKINGRIDWIDTH;
	gridcss_column = sortedIndex / SKINGRIDWIDTH;
	// I really need to macro this lol
	gridcss_skinydrag = CLAMP(gridcss_column - SKINGRIDHEIGHT + 1, 0, ((numskins - 1) / SKINGRIDWIDTH) - SKINGRIDHEIGHT + 1);
}

void Skinselectstyle_option_Onchange(void)
{
	boolean visible = cv_skinselectstyle.value == 0;
	M_SetItemVisible(MN_MP_PLAYERSETUP, "ACCELERATION", visible);
	M_SetItemVisible(MN_MP_PLAYERSETUP, "MAXSPEED", visible);
	M_SetItemVisible(MN_MP_PLAYERSETUP, "HANDLING", visible);
	M_SetItemVisible(MN_MP_PLAYERSETUP, "WEIGHT", visible);
	M_SetItemVisible(MN_MP_PLAYERSETUP, "ARROWLR", visible);
	M_SetItemVisible(MN_MP_PLAYERSETUP, "ARROWUD", visible);
	M_SetItemVisible(MN_MP_PLAYERSETUP, "STATBG", visible);

	M_SetItemVisible(MN_MP_PLAYERSETUP, "STATBAR", !visible);

	Skinsort_option_Onchange();
}

// ==========================================================================
// END ORGANIZATION STUFF.
// ==========================================================================

// =========================================================================
// MENU PRESENTATION PARAMETER HANDLING (BACKGROUNDS)
// =========================================================================

void M_InitMenuPresTables(void)
{
	INT32 i;

	// Called in d_main before SOC can get to the tables
	// Set menupres defaults
	for (i = 0; i < MAXMENUTYPES; i++)
	{
		// so-called "undefined"
		menudefs[i].fadestrength = -1;
		menudefs[i].hidetitlepics = -1; // inherits global hidetitlepics
		menudefs[i].ttmode = TTMODE_NONE;
		menudefs[i].ttscale = UINT8_MAX;
		menudefs[i].ttname[0] = 0;
		menudefs[i].ttx = INT16_MAX;
		menudefs[i].tty = INT16_MAX;
		menudefs[i].ttloop = INT16_MAX;
		menudefs[i].tttics = UINT16_MAX;
		menudefs[i].enterwipe = -1;
		menudefs[i].exitwipe = -1;
		menudefs[i].bgcolor = -1;
		menudefs[i].titlescrollxspeed = INT32_MAX;
		menudefs[i].titlescrollyspeed = INT32_MAX;
		menudefs[i].muscredity = 0;
		menudefs[i].muscreditanimtime = 2*TICRATE;
		menudefs[i].muscreditsnapflags = 0;
		menudefs[i].bghide = true;
		// default true
		menudefs[i].enterbubble = true;
		menudefs[i].exitbubble = true;
		menudefs[i].muslooping = true;
		menudefs[i].muscreditshow = true;
	}
}

// ====================================
// EFFECTS
// ====================================

void M_SetMenuCurBackground(void)
{
	for (UINT8 i = 0; i < NUMMENULEVELS; i++)
	{
		menutype_t menutype = menustack[i];
		if (!menutype)
			break;
		if (menudefs[menutype].bgcolor >= 0)
		{
			curbgcolor = menudefs[menutype].bgcolor;
			return;
		}
		else if (menudefs[menutype].bghide && titlemapinaction) // hide the background
		{
			curbghide = true;
			return;
		}
		else if (menudefs[menutype].bgname[0])
		{
			strncpy(curbgname, menudefs[menutype].bgname, 8);
			curbgxspeed = menudefs[menutype].titlescrollxspeed != INT32_MAX ? menudefs[menutype].titlescrollxspeed : titlescrollxspeed;
			curbgyspeed = menudefs[menutype].titlescrollyspeed != INT32_MAX ? menudefs[menutype].titlescrollyspeed : titlescrollyspeed;
			return;
		}
	}
	if (titlemapinaction) // hide the background by default in titlemap
		curbghide = true;
	else
	{
		strncpy(curbgname, "TITLESKY", 9);
		curbgxspeed = titlescrollxspeed;
		curbgyspeed = titlescrollyspeed;
	}
}

void M_ChangeMenuMusic(void)
{
	menutype_t target = MN_MAIN; // default if nothing is found

	if (GetClientMode() == CL_VIEWSERVER)
		return;

	for (UINT8 i = 0; i < NUMMENULEVELS; i++)
	{
		menutype_t menutype = menustack[i];
		if (!menutype)
			break;
		if (menudefs[menutype].musname[0])
		{
			target = menutype;
			break;
		}
		else if (menudefs[menutype].musstop)
		{
			S_StopMusic();
			S_ClearMusicCredit();
			return;
		}
		else if (menudefs[menutype].musignore)
			return;
	}
	if (target == MN_MAIN && gamestate == GS_TITLESCREEN && finalecount < 50)
	{
		S_StopMusic();
		S_ClearMusicCredit();
	}
	else
	{
		if (!fasticmp(menudefs[target].musname, S_MusicName()))
		{
			S_ChangeMusic(menudefs[target].musname, menudefs[target].mustrack, menudefs[target].muslooping);

			if (menudefs[target].muscreditshow)
			{
				S_ShowMusicCredit(menudefs[target].muscredity, menudefs[target].muscreditanimtime, menudefs[target].muscreditsnapflags);
			}
		}
		else
		{
			S_ChangeMusic(menudefs[target].musname, menudefs[target].mustrack, menudefs[target].muslooping);
		}
	}
}

void M_SetMenuCurFadeValue(void)
{
	for (UINT8 i = 0; i < NUMMENULEVELS; i++)
	{
		menutype_t menutype = menustack[i];
		if (!menutype)
			break;
		if (menudefs[menutype].fadestrength >= 0)
		{
			curfadevalue = menudefs[menutype].fadestrength;
			return;
		}
	}
	curfadevalue = 16; // default
}

void M_SetMenuCurTitlePics(void)
{
	for (UINT8 i = 0; i < NUMMENULEVELS; i++)
	{
		menutype_t menutype = menustack[i];
		if (!menutype)
			break;
		if (menudefs[menutype].hidetitlepics >= 0)
		{
			curhidepics = menudefs[menutype].hidetitlepics;
			return;
		}
		else if (menudefs[menutype].ttmode == TTMODE_USER)
		{
			if (menudefs[menutype].ttname[0])
			{
				curhidepics = menudefs[menutype].hidetitlepics;
				curttmode = menudefs[menutype].ttmode;
				curttscale = (menudefs[menutype].ttscale != UINT8_MAX ? menudefs[menutype].ttscale : ttscale);
				strncpy(curttname, menudefs[menutype].ttname, sizeof(curttname)-1);
				curttx = (menudefs[menutype].ttx != INT16_MAX ? menudefs[menutype].ttx : ttx);
				curtty = (menudefs[menutype].tty != INT16_MAX ? menudefs[menutype].tty : tty);
				curttloop = (menudefs[menutype].ttloop != INT16_MAX ? menudefs[menutype].ttloop : ttloop);
				curtttics = (menudefs[menutype].tttics != UINT16_MAX ? menudefs[menutype].tttics : tttics);
			}
			else
				curhidepics = menudefs[menutype].hidetitlepics;
			return;
		}
		else if (menudefs[menutype].ttmode != TTMODE_NONE)
		{
			curhidepics = menudefs[menutype].hidetitlepics;
			curttmode = menudefs[menutype].ttmode;
			curttscale = (menudefs[menutype].ttscale != UINT8_MAX ? menudefs[menutype].ttscale : ttscale);
			return;
		}
	}
	// nothing found, use default values
	curhidepics = hidetitlepics;
	curttmode = ttmode;
	curttscale = ttscale;
	strncpy(curttname, ttname, 9);
	curttx = ttx;
	curtty = tty;
	curttloop = ttloop;
	curtttics = tttics;
}

// ====================================
// MENU STATE
// ====================================

static void M_HandleMenuPresState(menutype_t newMenuType)
{
	INT32 i;
	menutype_t menutype, exittype = currentMenu ? currentMenu - menudefs : MN_NONE;
	boolean noancestor = menustack[0] == MN_NONE;
	INT16 exittag = 0, entertag = 0;
	INT16 exitwipe = -1, enterwipe = -1;
	boolean exitbubble = true, enterbubble = true;

	if (newMenuType == exittype) // same menu?
		return;

	F_InitMenuPresValues(false);

	// don't do the below during the in-game menus
	if (gamestate != GS_TITLESCREEN && gamestate != GS_MENU)
		return;

	// 0. Get the type and level of each menu, and level of common ancestor
	// 1. Get the wipes for both, then run the exit wipe
	// 2. Change music (so that execs can change it again later)
	// 3. Run each exit exec on the prevMenuId up to the common ancestor (UNLESS NoBubbleExecs)
	// 4. Run each entrance exec on the activeMenuId down from the common ancestor (UNLESS NoBubbleExecs)
	// 5. Run the entrance wipe

	// menustack changes happen before this function, currentMenu is changed afterwards
	if (exittype && menustack[1] != exittype)
	{
		exitwipe = menudefs[exittype].exitwipe;
		exitbubble = menudefs[exittype].exitbubble;
		exittag = menudefs[exittype].exittag;
	}

	if (newMenuType && menustack[1] == exittype)
	{
		enterwipe = menudefs[newMenuType].enterwipe;
		enterbubble = menudefs[newMenuType].enterbubble;
		entertag = menudefs[newMenuType].entertag;
	}

	// if no common ancestor (top menu), force a wipe. Look for a specified wipe first.
	// Don't force a wipe if you're actually going to/from the main menu
	if (noancestor && exitwipe < 0 && newMenuType != MN_MAIN && exittype != MN_MAIN)
	{
		for (i = 0; i < NUMMENULEVELS; i++)
		{
			menutype = menustack[i];

			if (menudefs[menutype].exitwipe >= 0)
			{
				exitwipe = menudefs[menutype].exitwipe;
				break;
			}
		}

		if (exitwipe < 0)
			exitwipe = menudefs[MN_MAIN].exitwipe;
	}

	// do the same for enter wipe
	if (noancestor && enterwipe < 0 && newMenuType != MN_MAIN && exittype != MN_MAIN)
	{
		for (i = 0; i < NUMMENULEVELS; i++)
		{
			menutype = menustack[i];

			if (menudefs[menutype].enterwipe >= 0)
			{
				exitwipe = menudefs[menutype].enterwipe;
				break;
			}
		}

		if (enterwipe < 0)
			enterwipe = menudefs[MN_MAIN].enterwipe;
	}

	// Change the music
	M_ChangeMenuMusic();

	// Run the linedef execs
	if (titlemapinaction)
	{
		// Run the exit tags
		if (exitbubble && noancestor)
		{
			for (i = 0; i < NUMMENULEVELS; i++) // don't run the common ancestor's exit tag
			{
				menutype = menustack[i];
				if (!menutype)
					break;
				if (menudefs[menutype].exittag)
					P_LinedefExecute(menudefs[menutype].exittag, players[displayplayers[0]].mo, NULL);
			}
		}
		if (exittag)
			P_LinedefExecute(exittag, players[displayplayers[0]].mo, NULL);

		// Run the enter tags
		(void)enterbubble;/*if (enterbubble)
		{
			for (i = anceslevel+1; i <= enterlevel; i++) // don't run the common ancestor's enter tag
			{
				menutype = menustack[i];
				if (menudefs[menutype].entertag)
					P_LinedefExecute(menudefs[menutype].entertag, players[displayplayers[0]].mo, NULL);
			}
		}
		else*/ if (entertag)
			P_LinedefExecute(entertag, players[displayplayers[0]].mo, NULL);
	}

	// Set the wipes for next frame
	if (exitwipe >= 0 || enterwipe >= 0 || noancestor)
	{
		// HACK: INT16_MAX signals to not wipe
		// because 0 is a valid index and -1 means default
		wipetypepre = (exitwipe || noancestor) ? exitwipe : INT16_MAX;
		wipetypepost = (enterwipe || noancestor) ? enterwipe : INT16_MAX;
		wipegamestate = FORCEWIPE;

		// If just one of the above is a force not-wipe,
		// mirror the other wipe.
		if (wipetypepre != INT16_MAX && wipetypepost == INT16_MAX)
			wipetypepost = wipetypepre;
		else if (wipetypepost != INT16_MAX && wipetypepre == INT16_MAX)
			wipetypepre = wipetypepost;

		// D_Display runs the next step of processing
	}
}

// =========================================================================
// BASIC MENU HANDLING
// =========================================================================

static UINT8      multi_spr2;
static void M_GetFollowerState(void);

static void M_ResetCvar(menuitem_t *item)
{
	consvar_t *cv = item->cvar;

	if (cv == &cv_playercolor[0] || cv == &cv_playercolor[1] || cv == &cv_playercolor[2] || cv == &cv_playercolor[3] || cv == &cv_dummycolor)
	{
		INT32 skinno = R_SkinAvailable(skins[cv_chooseskin.value].name);
		if (skinno != -1 && skincolors[skins[skinno].prefcolor].accessible)
			CV_SetValue(cv, skins[skinno].prefcolor);
	}
	else
		CV_Set(cv, cv->defaultvalue);
}

static void M_ChangeCvar(menuitem_t *item, SINT8 direction)
{
	consvar_t *cv = item->cvar;
	INT32 amount = item->argument ? item->argument : cv->flags & CV_FLOAT ? FRACUNIT/16 : 1;
	amount *= direction;

	if (cv->flags & CV_FLOAT)
	{
		char s[20];
		float n = FIXED_TO_FLOAT(cv->value + amount);
		sprintf(s, "%ld%s", (long)n, M_Ftrim(n));
		CV_Set(cv, s);
	}
	else
		CV_AddValue(cv, amount);

	if (cv == &cv_chooseskin)
		multi_spr2 = P_GetSkinSprite2(&skins[cv->value], SPR2_FSTN, NULL);
	else if (cv == &cv_dummyfollower)
		M_GetFollowerState();	// update follower state
}

// lock out further input in a tic when important buttons are pressed
// (in other words -- stop bullshit happening by mashing buttons in fades)
static INT32 noFurtherInput = 0;

static void Command_Manual_f(void)
{
	if (modeattacking)
		return;
	M_StartControlPanel();
	M_EnterMenu(MN_HELP, true, 0);
	M_SetItemOn(MN_HELP, "PAGE0");
}

// arbitrary keyboard shortcuts because fuck you
static boolean M_DemoBinds(INT32 ch)
{
	switch (ch)
	{
	case '\'':	// toggle freecam
		MR_PlaybackToggleFreecam(0);
		break;

	case ']':	// ffw / advance frame (depends on if paused or not)
		if (paused)
			MR_PlaybackAdvance(0);
		else
			MR_PlaybackFastForward(0);
		break;

	case '[':	// rewind /backupframe, uses the same function
		MR_PlaybackRewind(0);
		return false;

	case '\\':	// pause
		MR_PlaybackPause(0);
		break;

	// viewpoints, an annoyance (tm)
	case '-':	// viewpoint minus
		MR_PlaybackSetViews(-1); // yeah lol.
		break;

	case '=':	// viewpoint plus
		MR_PlaybackSetViews(1); // yeah lol.
		break;

	// switch viewpoints:
	case '1':	// viewpoint for p1 (also f12)
		// maximum laziness:
		if (!demo.freecam)
			G_AdjustView(1, 1, true);
		return false;
	case '2':	// viewpoint for p2
		if (!demo.freecam)
			G_AdjustView(2, 1, true);
		return false;
	case '3':	// viewpoint for p3
		if (!demo.freecam)
			G_AdjustView(3, 1, true);
		return false;
	case '4':	// viewpoint for p4
		if (!demo.freecam)
			G_AdjustView(4, 1, true);
		return false;

	default:
		return false;
	}
	return true;
}

// if the menu is being used during a wipe, running routines can be dangerous!
// buffer a single input, then wait for the wipe to end
static boolean M_WipeBuffer(INT32 ch, menufunc_f *routine)
{
	if (!WipeInAction)
		return false;

	// these routines are allowed to run during wipes
	// solely to make the menus less annoying to use
	if (routine == NULL
	 || (routine == MR_SelectableClearMenus && !currentMenu->quitroutine)
	 || routine == MR_Options
	 || routine == MR_SetupMultiPlayer
	 || routine == MR_SetupControlsMenu
	 || routine == MR_ChangeControl
	 || routine == MR_PrepareLevelPlatter
	 || routine == MR_HandleLevelPlatter
	)
		return false;

	noFurtherInput = ch;
	S_StartSound(NULL, sfx_kc50);
	return true;
}

// use this when routine being NULL isn't a free pass
static INT32 MR_Dummy(INT32 ch) { (void)ch; return true; }

static boolean M_GamestateCanOpenMenu(void)
{
	switch (gamestate)
	{
		case GS_INTRO:
		case GS_CUTSCENE:
		case GS_CREDITS:
		case GS_BLANCREDITS:
		case GS_SECRETCREDITS:
		case GS_EVALUATION:
			return false;

		default:
			return true;
	}
}

static INT32 RemapGamepadButton(event_t *ev)
{
	// remap to keyboard keys if needed
	if (ev->data1 < KEY_JOY1 || ev->data1 >= JOYINPUTEND)
		return ev->data1;

	static INT32 joyremap[][2] = {
		{ gc_accelerate,  KEY_ENTER      }, // these two first
		{ gc_brake,       KEY_ESCAPE     }, // in case of conflicts
		{ gc_fire,        KEY_BACKSPACE  },
		{ gc_drift,       KEY_SPACE      },
		{ gc_systemmenu,  KEY_ESCAPE     },
		{ gc_aimforward,  KEY_UPARROW    },
		{ gc_aimbackward, KEY_DOWNARROW  },
		{ gc_turnleft,    KEY_LEFTARROW  },
		{ gc_turnright,   KEY_RIGHTARROW },
		{ gc_lookback,	  KEY_TAB },
	};

	for (size_t r = 0; r < sizeof(joyremap)/sizeof(*joyremap); r++)
	{
		INT32 gc = joyremap[r][0];
		if (gc == gc_brake && !menustack[0])
			continue; // don't open the menu with brake!

		if (G_ControlBoundToKey(0, gc, ev->data1, true))
		{
			return joyremap[r][1];
		}
	}

	// remap the hats too
	switch (ev->data1)
	{
		case KEY_HAT1+0:
			return KEY_UPARROW;
		case KEY_HAT1+1:
			return KEY_DOWNARROW;
		case KEY_HAT1+2:
			return KEY_LEFTARROW;
		case KEY_HAT1+3:
			return KEY_RIGHTARROW;
	}
	return ev->data1;
}

//
// M_Responder
//
boolean M_Responder(event_t *ev)
{
	INT32 ch = -1;
	static tic_t mousewait = 0, joyxwait = 0, joyywait = 0;
	static INT32 pjoyx = 0, pjoyy = 0;
	static INT32 pmousex = 0, pmousey = 0;
	static INT32 lastx = 0, lasty = 0;
	INT32 deviceplayer = G_GetDevicePlayer(ev->device);

	if (dedicated
		|| (demo.playback && demo.title)
		|| M_GamestateCanOpenMenu() == false)
	{
		return false;
	}

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

		// added 5-2-98 remap virtual keys (mouse buttons)
		switch (ch)
		{
			case KEY_MOUSE1:
				ch = KEY_ENTER;
				break;
			case KEY_MOUSE1 + 1:
				ch = KEY_ESCAPE;
				break;
			default:
				ch = RemapGamepadButton(ev);
				break;
		}
	}
	else if (menustack[0])
	{
		if (ev->type == ev_joystick && deviceplayer == 0)
		{
			fixed_t rate = 0;
			UINT32 repeat = 0;

			if (ev->data1 == 1)
			{
				pjoyy = (abs(ev->data2) > JOYAXISRANGE/4) ? ev->data2 : 0;
			}
			else
			{
				pjoyx = (abs(ev->data2) > JOYAXISRANGE/4) ? ev->data2 : 0;
			}
			
			if (pjoyx == 0 && pjoyy == 0)
			{
				joyywait = 0;
			}
			else if (abs(pjoyy) > abs(pjoyx))
			{
				rate = FixedDiv(abs(pjoyy), JOYAXISRANGE);
				repeat = (FixedMul(2*FRACUNIT, rate) + FixedMul(FRACUNIT - rate, (NEWTICRATE/2)*FRACUNIT))/FRACUNIT;
			
				if (pjoyy > 0)
				{
					if (joyywait < I_GetTime())
					{
						ch = KEY_DOWNARROW;
						joyywait = I_GetTime() + repeat;
					}
				}
				else if (pjoyy < 0)
				{
					if (joyywait < I_GetTime())
					{
						ch = KEY_UPARROW;
						joyywait = I_GetTime() + repeat;
					}
				}
			}
			else
			{
				rate = FixedDiv(abs(pjoyx), JOYAXISRANGE);
				repeat = (FixedMul(2*FRACUNIT, rate) + FixedMul(FRACUNIT - rate, (NEWTICRATE/2)*FRACUNIT))/FRACUNIT;
				
				if (pjoyx > 0)
				{
					if (joyxwait < I_GetTime())
					{
						ch = KEY_RIGHTARROW;
						joyxwait = I_GetTime() + repeat;
					}
				}
				else if (pjoyx < 0)
				{
					if (joyxwait < I_GetTime())
					{
						ch = KEY_LEFTARROW;
						joyxwait = I_GetTime() + repeat;
					}
				}
			}
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

	if (ch == -1)
		return false;

	if (messagebox.active)
	{
		if (messagebox.messagetype != MM_EVENTHANDLER)
		{
			if (ch == ' ' || ch == 'n' || ch == 'y' || ch == KEY_ESCAPE || ch == KEY_ENTER)
			{
				if (M_WipeBuffer(ch, MR_Dummy))
					return true;
				if (messagebox.routine)
					messagebox.routine(ch);
				messagebox.active = false;
				noFurtherInput = -1;
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
		noFurtherInput = -1;

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
				if (modeattacking || WipeInAction)
					return true;
				M_StartControlPanel();
				M_EnterMenu(MN_OP_MAIN, true, 0);
				M_EnterMenu(MN_OP_SOUND, true, 0);
				M_SetItemOn(MN_OP_SOUND, "SOUND");
				return true;

			case KEY_F5: // Video Mode
				if (modeattacking || WipeInAction)
					return true;
				M_StartControlPanel();
				M_EnterMenu(MN_OP_MAIN, true, 0);
				M_EnterMenu(MN_OP_VIDEO, true, 0);
				M_EnterMenu(MN_OP_VIDEOMODE, true, 0);
				return true;

			case KEY_F6: // Empty
				return true;

			case KEY_F7: // Options
				if (modeattacking || WipeInAction)
					return true;
				M_StartControlPanel();
				M_EnterMenu(MN_OP_MAIN, true, 0);
				return true;

			// Screenshots on F8 now handled elsewhere
			// Same with Moviemode on F9

			case KEY_F10: // Quit SRB2
				MR_QuitSRB2(0);
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
		noFurtherInput = 0; // turns out we didn't care
		return false;
	}

	// Handle menuitems which need a specific key handling
	if (currentMenu->keyhandler)
	{
		if (M_WipeBuffer(ch, currentMenu->keyhandler))
			return true;
		if (shiftdown && ch >= 32 && ch <= 127)
			ch = shiftxform[ch];
		if (currentMenu->keyhandler(ch))
			return true;
	}

	// BP: one of the more big hack i have never made
	// G: not so hacky anymore...?
	// G: nevermind we have combi menuitems now
	menuitem_t *item = currentMenu->numitems ? &currentMenu->menuitems[itemOn] : NULL;

	// string cvars accept any keyboard input
	if (item && item->cvar && !item->cvar->PossibleValue)
	{
		if (item->cvar->flags & CV_CALL && M_WipeBuffer(ch, MR_Dummy))
			return true;

		if (M_TextInputHandle(&menuinput, ch))
		{
			S_StartSound(NULL, sfx_menu1); // Tails
			CV_Set(item->cvar, menuinput.buffer);
			return true;
		}
	}

	if (menustack[0] == MN_PLAYBACK && !con_destlines)
	{
		playback_last_menu_interaction_leveltime = leveltime;
		M_DemoBinds(ch);

		// Flip left/right with up/down for the playback menu, since it's a horizontal icon row.
		switch (ch)
		{
			case KEY_LEFTARROW: ch = KEY_UPARROW; break;
			case KEY_UPARROW: ch = KEY_RIGHTARROW; break;
			case KEY_RIGHTARROW: ch = KEY_DOWNARROW; break;
			case KEY_DOWNARROW: ch = KEY_LEFTARROW; break;

			default: break;
		}
	}

	INT16 oldItemOn = itemOn; // prevent infinite loop
	INT16 newItemOn = itemOn;

	// Keys usable within menu
	switch (ch)
	{
		case KEY_DOWNARROW:
			if (!item)
				return true;

			do if (++newItemOn >= currentMenu->numitems)
				newItemOn = 0;
			while (oldItemOn != newItemOn && !M_ItemSelectable(&currentMenu->menuitems[newItemOn]));

			M_RawSetItemOn(currentMenu, newItemOn);
			S_StartSound(NULL, sfx_menu1);
			return true;

		case KEY_UPARROW:
			if (!item)
				return true;

			do if (--newItemOn < 0)
				newItemOn = currentMenu->numitems - 1;
			while (oldItemOn != newItemOn && !M_ItemSelectable(&currentMenu->menuitems[newItemOn]));

			M_RawSetItemOn(currentMenu, newItemOn);
			S_StartSound(NULL, sfx_menu1);
			return true;

		case KEY_LEFTARROW:
		case KEY_RIGHTARROW:
			if (!item || !(item->cvar || item->status & IT_ARROWS) || item->cvar == &cv_nextmap || item->cvar == &cv_nextcup)
				return true;

			if (M_WipeBuffer(ch, item->cvar ? (item->cvar->flags & CV_CALL ? MR_Dummy : NULL) : item->routine))
				return true;

			if (menustack[0] != MN_OP_SOUND || itemOn > 3)
				S_StartSound(NULL, sfx_menu1);

			if (item->cvar)
				M_ChangeCvar(item, ch == KEY_RIGHTARROW ? 1 : -1);
			else
				item->routine(ch == KEY_RIGHTARROW ? 1 : 0);

			return true;

		case KEY_ENTER:
			if (!item)
				return true;

			noFurtherInput = -1;
			currentMenu->lastOn = itemOn;

			if (menustack[0] == MN_PLAYBACK)
			{
				boolean held = (boolean)playback_enterheld;
				if (held)
					return true;
				playback_enterheld = 3;
			}

			INT32 argument = item->argument;
			if (item->cvar && item->cvar != &cv_nextmap && item->cvar != &cv_nextcup)
				argument = item->cvar->value;

			if (item->submenu)
			{
				if (M_WipeBuffer(ch, menudefs[item->submenu].enterroutine))
					return true;
				S_StartSound(NULL, sfx_menu1);
				M_EnterMenu(item->submenu, true, argument);
			}
			else if (item->routine)
			{
				if (M_WipeBuffer(ch, item->routine))
					return true;
				S_StartSound(NULL, sfx_menu1);
				item->routine(argument);
			}
			else if (item->cvar && item->cvar->PossibleValue) // not for string cvars!
			{
				if (item->cvar == &cv_nextmap || item->cvar == &cv_nextcup)
					return true;
				if (item->cvar->flags & CV_CALL && M_WipeBuffer(ch, MR_Dummy))
					return true;
				S_StartSound(NULL, sfx_menu1);
				M_ChangeCvar(item, 1); // right arrow
			}
			return true;

		case KEY_ESCAPE:
			noFurtherInput = -1;
			currentMenu->lastOn = itemOn;

			if (M_WipeBuffer(ch, currentMenu->quitroutine))
				return true;

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
			// detach any keys associated with the game control
			if (item && item->routine == MR_ChangeControl)
			{
				G_ClearControlKeys(setupcontrols, item->argument);
				S_StartSound(NULL, sfx_shldls);
				return true;
			}

			if (!item || !item->cvar
			 || item->cvar == &cv_chooseskin
			 || item->cvar == &cv_dummystaff
			 || item->cvar == &cv_nextmap
			 || item->cvar == &cv_nextcup
			 || item->cvar == &cv_newgametype
			 || item->cvar == &cv_dummymultiplayer)
				return true;

			if (item->cvar->flags & CV_CALL && M_WipeBuffer(ch, MR_Dummy))
				return true;

			if (menustack[0] != MN_OP_SOUND || itemOn > 3)
				S_StartSound(NULL, sfx_menu1);
			M_ResetCvar(item);
			return true;

		default:
			CON_Responder(ev);
			return true;
	}
}

// special responder for demos
boolean M_DemoResponder(event_t *ev)
{
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
		// since this is ONLY for demos, there isn't MUCH for us to do.
		eatinput = M_DemoBinds(ev->data1);
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
		if (gamestate == GS_MENU)
		{
			if (curbgcolor >= 0)
				V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, curbgcolor);
			else if (!curbghide || !titlemapinaction)
				F_SkyScroll(curbgxspeed, curbgyspeed, curbgname);
			if (curfadevalue)
				V_DrawFadeScreen(0xFF00, curfadevalue);
		}
		else if (!WipeInAction && curfadevalue)
			V_DrawFadeScreen(0xFF00, curfadevalue);
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
	if (menustack[0] && gamestate != GS_NULL)
	{
		CON_ToggleOff(); // move away console
		return;
	}

	if (gamestate == GS_TITLESCREEN) // Set up menu state
	{
		// No instantly skipping the titlescreen.
		// (We can change this timer later when extra animation is added.)
		if (finalecount < 1)
			return;
	}

	if (demo.playback)
	{
		M_EnterMenu(MN_PLAYBACK, true, 0);
		playback_last_menu_interaction_leveltime = leveltime;
	}
	else if (!Playing())
	{
		if (gamestate != GS_MENU)
		{
			if (titlemapinaction)
			{
				// We clear a LITTLE bit of state, but not a full D_ClearState.
				// Just enough to guarantee SV_ResetServer is called before session start.
				SV_StopServer();
				SV_ResetServer();
			}

			G_SetGamestate(GS_MENU);
			gameaction = ga_nothing;
			paused = false;
			CON_ToggleOff();

			modeattacking = ATTACKING_NONE;
		}

		M_EnterMenu(MN_MAIN, true, 0);
		M_SetItemOn(MN_MAIN, "SINGLEPLAYER");
	}
	else if (modeattacking)
	{
		M_EnterMenu(MN_MAPAUSE, true, 0);
		M_SetItemOn(MN_MAPAUSE, "CONTINUE");
	}
	else if (grandprixinfo.gp) // Single Player
	{
		boolean retryallowed = false;
		INT32 i;
		for (i = 0; i <= splitscreen; i++)
		{
			if (G_GametypeUsesLives() && players[g_localplayers[i]].lives <= 1)
				continue;
			retryallowed = true;
			break;
		}

		if (gamestate != GS_LEVEL)
			retryallowed = false;

		// intermission, so gray out stuff.
		M_SetItemDisabled(MN_SPAUSE, "RETRY", !retryallowed /*|| ultimatemode*/);

		M_EnterMenu(MN_SPAUSE, true, 0);
		M_SetItemOn(MN_SPAUSE, "CONTINUE");
	}
	else // multiplayer
	{
#ifdef HAVE_DISCORDRPC
		menudefs[MN_MPAUSE].y = 72;
		M_RefreshPauseMenu();
		M_SetItemVisible(MN_MPAUSE, "DISCORDREQUESTS", true);
#else
		menudefs[MN_MPAUSE].y = 80;
		M_SetItemVisible(MN_MPAUSE, "DISCORDREQUESTS", false);
#endif
		Dummymenuplayer_OnChange();

		M_SetItemVisible(MN_MPAUSE, "MAPCHANGE", server || IsPlayerAdmin(consoleplayer));
		M_SetItemVisible(MN_MPAUSE, "ADDONS", server || IsPlayerAdmin(consoleplayer));
		M_SetItemVisible(MN_MPAUSE, "SCRAMBLE", (server || IsPlayerAdmin(consoleplayer)) && G_GametypeHasTeams());

		if (!(server || IsPlayerAdmin(consoleplayer)))
			menudefs[MN_MPAUSE].y += 24;

		M_SetItemVisible(MN_MPAUSE, "SETUP1", splitscreen > 0);
		M_SetItemVisible(MN_MPAUSE, "SETUP2", splitscreen > 0);
		M_SetItemVisible(MN_MPAUSE, "SETUP3", splitscreen > 1);
		M_SetItemVisible(MN_MPAUSE, "SETUP4", splitscreen > 2);
		M_SetItemVisible(MN_MPAUSE, "PLAYERSETUP", !splitscreen);
		M_SetItemVisible(MN_CHANGETEAM, "PLAYER", splitscreen);
		M_SetItemVisible(MN_CHANGESPECTATE, "PLAYER", splitscreen);

		// splitscreen hell! yay!
		M_SetItemVisible(MN_MPAUSE, "SPECTATE", false);
		M_SetItemDisabled(MN_MPAUSE, "SPECTATE", false);
		M_SetItemVisible(MN_MPAUSE, "ENTER", false);
		M_SetItemVisible(MN_MPAUSE, "CANCELJOIN", false);
		M_SetItemVisible(MN_MPAUSE, "TEAMCHANGE", false);
		M_SetItemVisible(MN_MPAUSE, "SPECTATECHANGE", false);

		if (splitscreen && netgame)
		{
			if (G_GametypeHasTeams())
				M_SetItemVisible(MN_MPAUSE, "TEAMCHANGE", true);
			else if (G_GametypeHasSpectators())
				M_SetItemVisible(MN_MPAUSE, "SPECTATECHANGE", true);
		}
		else if (!splitscreen)
		{
			if (G_GametypeHasTeams())
				M_SetItemVisible(MN_MPAUSE, "TEAMCHANGE", true);
			else if (G_GametypeHasSpectators())
				M_SetItemVisible(MN_MPAUSE, !players[consoleplayer].spectator ? "SPECTATE" :
				                            players[consoleplayer].pflags & PF_WANTSTOJOIN ? "CANCELJOIN" :
				                            "ENTER", true);
			else // in this odd case, we still want something to be on the menu even if it's useless
			{
				M_SetItemVisible(MN_MPAUSE, "SPECTATE", true);
				M_SetItemDisabled(MN_MPAUSE, "SPECTATE", true);
			}
		}

		M_EnterMenu(MN_MPAUSE, true, 0);
		M_SetItemOn(MN_MPAUSE, "CONTINUE");
	}

	CON_ToggleOff(); // move away console
}

//
// M_ClearMenusEx
//
void M_ClearMenusEx(boolean callexitmenufunc, boolean notitle)
{
	CON_ClearHUD();
	I_SetTextInput(false);

	if (currentMenu)
	{
		if (currentMenu->quitroutine && callexitmenufunc)
			currentMenu->quitroutine(0);

		// Save the config file. I'm sick of crashing the game later and losing all my changes!
		char buf[sizeof(configfile) + 50];
		sprintf(buf, "saveconfig \"%s\" -silent\n", configfile);
		COM_BufAddText(buf);

		if (currentMenu->exitwipe >= 0)
		{
			wipetypepre = currentMenu->exitwipe;
			wipegamestate = FORCEWIPE;
		}
	}

	memset(menustack, 0, sizeof(menustack));
	currentMenu = NULL;
	messagebox.active = false;
	hidetitlemap = false;

	if (!notitle)
	{
		// D_StartTitle does its own wipe, since GS_MENU is now a complete gamestate.
		if (gamestate == GS_MENU)
			D_StartTitle();
	}
}

// corrects the value of cv_nextmap if it can't show its current map
// returns false if there's no map to show
static boolean M_CheckNextMap(menutype_t menunum)
{
	INT32 i;
	menu_t *menudef = &menudefs[menunum];
	for (i = 0; i < menudef->numitems; i++)
		if (menudef->menuitems[i].cvar == &cv_nextmap)
			break;

	if (i == menudef->numitems)
		return true;

	levellistmode = menudef->menuitems[i].argument;

	if (!M_CanShowLevelInList(cv_nextmap.value - 1))
	{
		for (i = 0; !M_CanShowLevelInList(i); i++)
			if (i == nummapheaders)
				return false;
		CV_SetValue(&cv_nextmap, i + 1);
	}

	return true;
}

// ditto, but for cv_nextcup
static boolean M_CheckNextCup(menutype_t menunum)
{
	INT32 i;
	menu_t *menudef = &menudefs[menunum];
	for (i = 0; i < menudef->numitems; i++)
		if (menudef->menuitems[i].cvar == &cv_nextcup)
			break;

	if (i == menudef->numitems)
		return true;

	if (kartcupheaders == NULL)
		return false;

	if (cv_nextcup.value == 0)
		CV_SetValue(&cv_nextcup, kartcupheaders->id + 1);

	return true;
}

static void M_UpdateTimeAttackMenu(void)
{
	// see also p_setup.c's P_LoadRecordGhosts
	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(true, levellistmode == LLM_ITEMBREAKER);

	boolean visible;
	boolean showreplay = false, showguest = false;

	// best time
	visible = G_CheckRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, REPLAY_BESTTIME);
	M_SetItemVisible(MN_SP_REPLAY, "REPLAYTIME", visible);
	M_SetItemVisible(MN_SP_GUESTREPLAY, "SAVETIME", visible);
	if (visible)
		showreplay = showguest = true;

	// best lap
	visible = levellistmode != LLM_ITEMBREAKER && G_CheckRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, REPLAY_BESTLAP);
	M_SetItemVisible(MN_SP_REPLAY, "REPLAYLAP", visible);
	M_SetItemVisible(MN_SP_GUESTREPLAY, "SAVELAP", visible);
	if (visible)
		showreplay = showguest = true;

	// last
	visible = G_CheckRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, REPLAY_LAST);
	M_SetItemVisible(MN_SP_REPLAY, "REPLAYLAST", visible);
	M_SetItemVisible(MN_SP_GUESTREPLAY, "SAVELAST", visible);
	if (visible)
		showreplay = showguest = true;

	// guest
	visible = G_CheckRecordReplay(gpath, cv_nextmap.value, UINT16_MAX, REPLAY_GUEST);;
	M_SetItemVisible(MN_SP_REPLAY, "REPLAYGUEST", visible);
	M_SetItemVisible(MN_SP_GUESTREPLAY, "DELETE", visible);
	M_SetItemVisible(MN_SP_GHOST, "GUEST", visible);
	if (visible)
		showreplay = showguest = true;

	// staff
	CV_StealthSetValue(&cv_dummystaff, 0);
	CV_SetValue(&cv_dummystaff, 1);
	visible = cv_dummystaff.value != 0;
	M_SetItemVisible(MN_SP_REPLAY, "REPLAYSTAFF", visible);
	M_SetItemVisible(MN_SP_GHOST, "STAFF", visible);
	if (visible)
	{
		CV_StealthSetValue(&cv_dummystaff, 1);
		showreplay = true;
	}

	M_SetItemVisible(MN_SP_TIMEATTACK, "REPLAY", showreplay);
	M_SetItemVisible(MN_SP_TIMEATTACK, "GUEST", showguest);
}

//
// M_SetupNextMenu
//
static void M_SetupNextMenu(menutype_t menunum, boolean callexit)
{
	menu_t *menudef = &menudefs[menunum];

	// If you're going from a menu to itself, why are you running the quitroutine? You're not quitting it! -SH
	if (callexit && currentMenu && currentMenu != menudef && currentMenu->quitroutine)
		currentMenu->quitroutine(0);

	M_HandleMenuPresState(menunum);

	currentMenu = menudef;
	M_RawSetItemOn(currentMenu, currentMenu->lastOn);

	hidetitlemap = false;

	if (menunum == MN_SP_TIMEATTACK)
		M_UpdateTimeAttackMenu();

	M_CheckNextMap(menunum);
	M_CheckNextCup(menunum);
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

// push a new menu to the stack (if the menu's enterroutine allows it)
void M_EnterMenu(menutype_t menunum, boolean callexit, INT32 arg)
{
	size_t i;

	if (menustack[NUMMENULEVELS-1])
		CONS_Alert(CONS_WARNING, "Max menu depth (%d) exceeded!\n", NUMMENULEVELS);

	for (i = NUMMENULEVELS-1; i; i--)
		menustack[i] = menustack[i-1];
	menustack[0] = menunum;

	if (menudefs[menunum].enterroutine && !menudefs[menunum].enterroutine(arg))
	{
		// i guess we're not going there... restore the menu stack
		for (i = 0; i < NUMMENULEVELS-1; i++)
			menustack[i] = menustack[i+1];
		return;
	}

	if (menustack[0] == MN_NONE)
		menustack[0] = menunum; // M_ClearMenus got called, put it back on the stack

	M_SetupNextMenu(menunum, callexit);
}

// Use for menus you return to.
// Backs up the stack so you can place it back into place
void M_RestoreMenuStack(boolean backup)
{
	if (backup)
	{
		memcpy(restoremenustack, menustack, sizeof(menustack));
		return;
	}

	D_ResetGameState();
	M_ClearMenusEx(true, false);
	M_StartControlPanel();

	// Put the stack back into place and wipe the restore stack
	memcpy(menustack, restoremenustack, sizeof(restoremenustack));
	memset(restoremenustack, MN_NONE, sizeof(restoremenustack));

	if (menustack[0])
		M_SetupNextMenu(menustack[0], true);
	else
	{
		CONS_Alert(CONS_WARNING, "Could not restore menu stack, returning to main menu.\n");

		D_ResetGameState();
		M_StartControlPanel();
	}
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
	// send buffered input from wipe?
	if (noFurtherInput > 0 && !WipeInAction)
	{
		event_t fake;
		fake.device = 0;
		fake.type = ev_keydown;
		fake.data1 = noFurtherInput;
		D_PostEvent(&fake);
		noFurtherInput = 0;
	}
	else if (noFurtherInput == -1 || !WipeInAction)
		noFurtherInput = 0;

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

	// Hide some options based on the current render mode
#ifdef HWRENDER
	M_SetItemVisible(MN_OP_VIDEO, "OPENGL", rendermode == render_opengl);
#endif
	//M_SetItemVisible(MN_OP_VIDEO, "PARALLEL", rendermode == render_soft);

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

	levelselect.namescroll++;
	if (levelselect.selectdelay > 0 && --levelselect.selectdelay == 0)
	{
		if (menustack[1] == MN_MPAUSE)
			MR_ChangeLevel(0);
		else if (menustack[1] == MN_SP_GRANDPRIX)
			MR_StartGrandPrix(0);
		else
			M_ExitMenu();
	}

	CL_TimeoutServerList();
}

//
// M_Init
//
void M_Init(void)
{
	CV_RegisterVar(&cv_nextmap);
	CV_RegisterVar(&cv_nextcup);
	CV_RegisterVar(&cv_newgametype);
	CV_RegisterVar(&cv_levelsort);
	CV_RegisterVar(&cv_chooseskin);
	CV_RegisterVar(&cv_skinselectstyle);
	CV_RegisterVar(&cv_skinselectsort);
	CV_RegisterVar(&cv_autorecord);

	if (dedicated)
		return;

	COM_AddCommand("manual", Command_Manual_f);

	CV_RegisterVar(&cv_showallmaps);

	// Menu hacks
	CV_RegisterVar(&cv_dummymenuplayer);
	CV_RegisterVar(&cv_dummyteam);
	CV_RegisterVar(&cv_dummyspectate);
	CV_RegisterVar(&cv_dummyscramble);
	CV_RegisterVar(&cv_dummystaff);

	CV_RegisterVar(&cv_dummygpdifficulty);
	CV_RegisterVar(&cv_dummygpencore);

	CV_RegisterVar(&cv_dummymultiplayer);
	CV_RegisterVar(&cv_dummyip);
	CV_RegisterVar(&cv_dummyname);
	CV_RegisterVar(&cv_dummyfollower);
	CV_RegisterVar(&cv_dummycolor);
	CV_RegisterVar(&cv_dummyserverpage);

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
	CV_RegisterVar(&cv_menucaps);
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

// Thanks to hayaUnderscore for creating this code used in SRB2Kart Saturn
static void M_DrawSplitText(INT32 x, INT32 y, INT32 option,INT32 alpha)
{
	char* icopy = strdup(currentMenu->menuitems[itemOn].tooltip);
	char** clines = NULL;
	INT16 num_lines = 0;

	if (icopy == NULL) return;

	char* strtoken = strtok(icopy, "\n");

	while (strtoken != NULL)
	{
		char* line = strdup(strtoken);

		if (line == NULL) return;

		clines = (char**)realloc(clines, (num_lines + 1) * sizeof(char*));
		clines[num_lines] = line;
		num_lines++;

		strtoken = strtok(NULL, "\n");
	}

	free(icopy);

	INT16 yoffset;
	yoffset = (((5*10 - num_lines*10)));

	// Draw BG first,,,
	for (int i = 0; i < num_lines; i++)
	{
		V_DrawFill(0, (y + yoffset - 6)+5, vid.width, 11, 159|V_SNAPTOBOTTOM|V_SNAPTOLEFT);
		yoffset += 11;
	}

	yoffset = (((5*10 - num_lines*10)));

	// THEN the text
	for (int i = 0; i < num_lines; i++)
	{
        V_DrawCenteredThinString(x, y + yoffset, option, clines[i]);
		V_DrawCenteredThinString(x, y + yoffset, option|highlightflags|((9 - alpha) << V_ALPHASHIFT), clines[i]);
		yoffset += 10;
        // Remember to free the memory for each line when you're done with it.
        free((void*)clines[i]);
    }

	free(clines);
}

//
// M_DrawMenuTooltips
//
// Draw a banner across the bottom of the screen, with a description of the current option displayed
//
INT16 lastitemon = 0;
INT32 alphatimer = 0;
static void M_DrawMenuTooltips(void)
{
	if (currentMenu->menuitems[itemOn].tooltip != NULL)
	{
		if (lastitemon != itemOn)
		{
			alphatimer = 9;
			lastitemon = itemOn;
		}

		M_DrawSplitText(BASEVIDWIDTH / 2, BASEVIDHEIGHT-50, V_SNAPTOBOTTOM|V_6WIDTHSPACE|MENUCAPS, alphatimer);
		if (alphatimer > 0 && renderisnewtic)
			alphatimer--;
	}
}

//
//  Draw a textbox, like Quake does, because sometimes it's difficult
//  to read the text with all the stuff in the background...
//
void M_DrawTextBox(INT32 x, INT32 y, INT32 width, INT32 boxlines)
{
	// Solid color textbox.
	V_DrawFill(x+5, y+5, width*8+6, boxlines*8+6, 159);
}

// horizontally centered text
static void M_CentreText(INT32 y, const char *string)
{
	INT32 x;
	//added : 02-02-98 : centre on 320, because V_DrawString centers on vid.width...
	x = (BASEVIDWIDTH - V_StringWidth(string,  MENUCAPS|V_OLDSPACING))>>1;
	V_DrawString(x,y, MENUCAPS|V_OLDSPACING,string);
}

//
// M_DrawMapEmblems
//
// used by pause & statistics to draw a row of emblems for a map
//
static void M_DrawMapEmblems(mapnum_t mapnum, INT32 x, INT32 y)
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

#define SLIDER_RANGE (10*8)
static void M_DrawSliderCursor(INT16 x, INT16 y, INT32 vflags, consvar_t *cv, INT32 value)
{
	fixed_t range = FixedDiv(value - cv->PossibleValue[0].value, cv->PossibleValue[1].value - cv->PossibleValue[0].value);
	V_DrawFixedPatch((x - 4)*FRACUNIT + (SLIDER_RANGE+4)*range, y*FRACUNIT, FRACUNIT, vflags, W_CachePatchName("M_SLIDEC", PU_CACHE), NULL);
}

static void M_DrawRightString(menuitem_t *item, INT16 x, INT16 y, INT32 vflags, boolean selected)
{
	INT16 w;
	if (item->cvar)
	{
		consvar_t *cv = item->cvar;
		if (!cv->PossibleValue) // string cvar?
		{
			if (currentMenu->scrollheight && y + 12 > currentMenu->y + currentMenu->scrollheight)
				return;

			// see M_ItemExtraOffset
			boolean side = cv == &cv_dummyname || cv == &cv_playername[0];
			INT32 xofs = side ? 32 : 0;
			INT32 yofs = side ? -8 : 4;
			w = M_StringCvarLength(cv);

			M_DrawTextBox(x + xofs, y + yofs, w, 1);
			if (selected)
				M_DrawTextInput(x + xofs + 8, y + yofs + 8, &menuinput, vflags|V_ALLOWLOWERCASE);
			else
				V_DrawString(x + xofs + 8, y + yofs + 8, vflags|V_ALLOWLOWERCASE, cv->string);
		}
		else if (item->status & IT_SLIDER)
		{
			INT32 i;
			patch_t *p;

			vflags &= ~(V_FLIP|V_PARAMMASK);
			x = BASEVIDWIDTH - x - SLIDER_RANGE - 6;

			if (selected)
			{
				V_DrawCharacter(x - 16 - (skullAnimCounter/5), y,
					'\x1C' | highlightflags | vflags, false); // left arrow
				V_DrawCharacter(x + SLIDER_RANGE + 8 + (skullAnimCounter/5), y,
					'\x1D' | highlightflags | vflags, false); // right arrow
			}

			if (atoi(cv->defaultvalue) != cv->value)
				M_DrawSliderCursor(x, y, vflags, cv, atoi(cv->defaultvalue));

			p = W_CachePatchName("M_SLIDEL", PU_CACHE);
			V_DrawScaledPatch(x - 8, y, vflags, p);

			p = W_CachePatchName("M_SLIDEM", PU_CACHE);
			for (i = 0; i < SLIDER_RANGE; i += 8)
				V_DrawScaledPatch (x + i, y, vflags, p);

			p = W_CachePatchName("M_SLIDER", PU_CACHE);
			V_DrawScaledPatch(x + i, y, vflags, p);

			M_DrawSliderCursor(x, y, vflags, cv, cv->value);
		}
		else
		{
			const char *str = cv->string;
			INT32 soffset = 0;
			boolean warning = (cv->flags & CV_CHEAT) && !CV_IsSetToDefault(cv);

			// special cvar value offsets for time attack!
			// i don't feel like adding an extra flag for this...
			/*
			if (y < 78) for (w = 0; w < NUMMENULEVELS; w++)
			{
				if (menustack[w] == MN_SP_TIMEATTACK)
				{
					soffset = 40;
					break;
				}
			}
			*/

			if (cv == &cv_chooseskin)
				str = skins[cv_chooseskin.value].realname;
			else if (cv == &cv_dummyfollower)
				str = cv_dummyfollower.value == -1 ? "None" : followers[cv_dummyfollower.value].name;
			else if (cv == &cv_dummystaff && cv_dummystaff.value)
				str = dummystaffname;
			else if (cv == &cv_dummycolor)
				str = skincolors[cv_dummycolor.value].name;
			else if (cv == &cv_dummyserverpage)
				str = va("%d of %d", cv->value + 1, dummyserverpage_cons_t[1].value + 1);
			else if (cv == &cv_soundtest && cv_soundtest.value)
				V_DrawRightAlignedString(BASEVIDWIDTH - x - soffset, y + 8, highlightflags|vflags, S_sfx[cv_soundtest.value].name);
			else if (cv == &cv_gamesounds)
			{
				str = sound_disabled ? "Off" : "On";
				warning = sound_disabled;
			}
			else if (cv == &cv_gamedigimusic)
			{
				str = digital_disabled ? "Off" : "On";
				warning = digital_disabled;
			}
			else if (cv == &cv_dummymultiplayer)
				return;

			if (menustack[0] == MN_MP_PLAYERSETUP)
				vflags |= V_ALLOWLOWERCASE;

			w = V_StringWidth(str, vflags);
			V_DrawString(BASEVIDWIDTH - x - soffset - w, y, (warning ? warningflags : highlightflags)|vflags, str);

			if (selected && cv != &cv_nextmap && cv != &cv_nextcup)
			{
				vflags &= ~(V_FLIP|V_PARAMMASK);
				V_DrawCharacter(BASEVIDWIDTH - x - soffset - 10 - w - (skullAnimCounter/5), y,
					'\x1C' | highlightflags | vflags, false); // left arrow
				V_DrawCharacter(BASEVIDWIDTH - x - soffset + 2 + (skullAnimCounter/5), y,
					'\x1D' | highlightflags | vflags, false); // right arrow
			}
		}
	}
	else if (item->routine)
	{
		char tmp[32*MAXINPUTMAPPING]; // should be enough :^)
		INT32 key;
		if (item->routine != MR_ChangeControl)
			return;

		tmp[0] ='\0';
		for (w = 0; w < MAXINPUTMAPPING; w++)
		{
			key = setupcontrols[item->argument][w];
			if (key != KEY_NULL)
			{
				if (tmp[0] != '\0')
					strcat(tmp, ", ");
				strcat(tmp, G_KeynumToString(key));
			}
		}
		if (tmp[0] == '\0')
			strcpy(tmp, "---");

		w = V_StringWidth(tmp, vflags);
		(w > BASEVIDWIDTH/2 - 4 ? V_DrawRightAlignedThinString : V_DrawRightAlignedString)
			(BASEVIDWIDTH-currentMenu->x, y, vflags|highlightflags, tmp);
	}
	else
	{
		if (item->submenu == MN_OP_VIDEOMODE) // show current resolution
		{
			V_DrawRightAlignedString(BASEVIDWIDTH - x, y,
				(SCR_IsAspectCorrect(vid.width, vid.height) ? recommendedflags : highlightflags)|vflags,
				va("%dx%d", vid.width, vid.height));
		}
		else if (item->submenu == MN_OP_JOYSTICKSET) // gamepad select
		{
			INT32 stick = cv_usecontroller[setupcontrolplayer-1].value;
			const char *name = stick == 0 ? "None" : I_GetControllerName(stick);
			if (!name) name = "Unavailable";
			w = V_StringWidth(name, vflags);
			(w > BASEVIDWIDTH/2 - 4 ? V_DrawRightAlignedThinString : V_DrawRightAlignedString)
				(BASEVIDWIDTH - x, y, vflags|highlightflags, name);
		}
#ifdef HAVE_DISCORDRPC
		else if (item->submenu == MN_MISC_DISCORDREQUESTS)
		{
			vflags &= ~(V_FLIP|V_PARAMMASK);
			const tic_t freq = TICRATE/2;
			if ((leveltime % freq) >= freq/2)
				V_DrawScaledPatch(204, y - 1, vflags, W_CachePatchName("K_REQUE2", PU_CACHE));
		}
#endif
		else if (item->patch)
			V_DrawRightAlignedString(BASEVIDWIDTH - x, y, (selected || (item->status & ITH_MASK) == ITH_HIGHLIGHT ? highlightflags : 0)|vflags, item->patch);
	}
}

static void M_DrawSticker(INT32 x, INT32 y, INT32 width, INT32 flags, boolean isSmall);

// draws a menu item, returns cursor offset
static INT32 M_DrawMenuItem(menuitem_t *item, INT16 x, INT16 y, INT32 vflags, boolean selected)
{
	INT32 width = 0;
	const char *string = item->text ? item->text : "";
	fixed_t scale = item->status & IT_SMALL ? FRACUNIT/2 : FRACUNIT;
	int font;
	INT32 colorflag;
	INT32 (*widthfunc)(const char *string, INT32 length, INT32 option);

	if (item->status & IT_SECRET)
	{
		string = M_CreateSecretMenuOption(string);
		vflags |= V_TRANSLUCENT|V_OLDSPACING;
	}
	else if (item->status & IT_GRAYEDOUT)
		vflags |= V_TRANSLUCENT;

	switch (item->status & ITH_MASK)
	{
	case ITH_HIGHLIGHT:
		colorflag = highlightflags; break;
	case ITH_RECOMMEND:
		colorflag = recommendedflags; break;
	case ITH_WARNING:
		colorflag = warningflags; break;
	default:
		colorflag = 0; break;
	}

	// color a dinosaur?
	if ((item->status & ITF_MASK) == ITF_FILL)
	{
		UINT16 fillw, fillh;
		UINT8 fillc;
		INT16 ofsx, ofsy;
		// TODO: these can overflow, reimplement sscanf to fix i guess
		int results = sscanf(item->patch, "%hux%hu,%hhu,%hd,%hd", &fillw, &fillh, &fillc, &ofsx, &ofsy);
		if (results != 3 && results != 5)
			return 0;

		if (item->status & IT_CENTER)
			width = fillw/2;
		else if (item->status & IT_RIGHT)
			width = fillw;

		V_DrawFill(x - width + (results == 5 ? ofsx : 0), y + (results == 5 ? ofsy : 0), fillw, fillh, fillc);
		return 0;
	}

	// draw a patch or map thumbnail instead of a string?
	if ((item->status & ITF_MASK) >= ITF_IMAGETYPE)
	{
		patch_t *p = NULL;
		UINT8 *cmap = NULL;

		switch (item->status & ITF_MASK)
		{
		case ITF_THUMBNAIL:
		{
			mapnum_t mapnum = G_MapNumber(item->patch);
			scale = M_GetMapThumbnail(mapnum < nummapheaders ? mapnum : NEXTMAP_INVALID, &p)/4;
			break;
		}
		case ITF_PATCH:
		{
			if (!item->patch)
				return 0;

			p = W_CachePatchName(item->patch, PU_CACHE);
			break;
		}
		}

		if (item->status & IT_CENTER)
			width = SHORT(p->width)/2;
		else if (item->status & IT_RIGHT)
			width = SHORT(p->width);

		cmap = V_GetStringColormap(colorflag);

		V_DrawFixedPatch((x - width)<<FRACBITS, y<<FRACBITS, scale, vflags & ~V_FLIP, p, cmap);
		return width;
	}

	switch (item->status & ITF_MASK)
	{
	case ITF_THIN2:
		vflags |= V_6WIDTHSPACE; // FALLTHRU
	case ITF_THIN:
		font = TINY_FONT;
		widthfunc = V_ThinSubStringWidth;
		break;

	case ITF_HEADER:
		x -= 16; // FALLTHRU
	default:
		font = HU_FONT;
		widthfunc = V_SubStringWidth;
		break;
	}

	if (item->status & IT_CENTER)
		width = widthfunc(string, -1, vflags)/2;
	else if (item->status & IT_RIGHT)
		width = widthfunc(string, -1, vflags);

	if (item->status & IT_STICKER)
		M_DrawSticker(x -width, y + 2, widthfunc(string, -1, vflags), vflags & ~V_FLIP, true);

	V_DrawStringScaled((x - width)<<FRACBITS, y<<FRACBITS, scale, FRACUNIT, FRACUNIT, (selected ? highlightflags : colorflag)|vflags, font, string);

	if (!(item->status & (IT_SECRET|IT_GRAYEDOUT)))
		M_DrawRightString(item, x, y, vflags, selected);

	return width;
}

// returns 16 if this item draws a cvar string input. yep, that's it!
static INT16 M_ItemExtraOffset(menuitem_t *item)
{
	// see M_DrawRightString
	boolean side = item->cvar == &cv_dummyname || item->cvar == &cv_playername[0];
	return item->cvar && !item->cvar->PossibleValue && !side ? 16 : 0;
}

// gets the absolute Y coordinate where this menuitem should be drawn
// TODO: merge this with the actual drawing loop somehow
static INT16 M_GetItemAbsY(menu_t *menu, INT16 index)
{
	INT16 i, y = 0;
	for (i = 0; i < menu->numitems; i++)
	{
		menuitem_t *item = &menu->menuitems[i];

		if (item->status & (IT_OVERLAY|IT_HIDDEN))
			continue;

		if (item->y)
		{
			if (!(item->status & IT_OFSY))
				y = item->y;
			else if (!(item->status & IT_TEMPORARY))
				y = y + item->y;
		}

		if (i >= index)
			break;

		y += menu->lineheight + M_ItemExtraOffset(item);
	}

	return y;
}

void MD_DrawGenericMenu(void)
{
	INT16 scrollx = currentMenu->x, scrolly = currentMenu->y;
	INT16 scrollheight = currentMenu->scrollheight;
	if (!scrollheight)
		scrollheight = INT16_MAX;
	if (!currentMenu->numitems)
		return;

	boolean hidecursor = currentMenu->drawroutine == MD_DrawLevelPlatterMenu && levelselect.row > 0;

	INT16 topy = M_GetItemAbsY(currentMenu, 0);
	INT16 boty = M_GetItemAbsY(currentMenu, currentMenu->numitems-1);

	if (boty - topy > scrollheight)
	{
		scrolly = scrolly - M_GetItemAbsY(currentMenu, itemOn) + scrollheight/2;
		if (scrolly > currentMenu->y - topy)
			scrolly = currentMenu->y - topy;
		else if (scrolly < currentMenu->y - boty + scrollheight)
			scrolly = currentMenu->y - boty + scrollheight;
	}

	// draw title (or big pic)
	M_DrawMenuTitle();

	INT16 i, w, x = scrollx, y = scrolly, cursorx = 0, cursory = 0;
	boolean clipbot = false, cliptop = false;

	// levelselect no longer needs a special drawer!
	for (i = 0; i < currentMenu->numitems; i++)
		if (currentMenu->menuitems[i].cvar == &cv_nextmap)
		{
			patch_t *patch;
			fixed_t scale = M_GetMapThumbnail(cv_nextmap.value - 1, &patch);
			V_DrawSciencePatch((BASEVIDWIDTH - FixedMul(patch->width, scale/4) - currentMenu->x)*FRACUNIT,
			                  (scrolly + M_GetItemAbsY(currentMenu, i) + 35 - FixedMul(patch->height, scale/8))*FRACUNIT,
			                  0, patch, scale/4);
			break;
		}

	for (i = 0; i < currentMenu->numitems; i++)
	{
		INT16 dx, dy;
		menuitem_t *item = &currentMenu->menuitems[i];

		if (item->status & IT_HIDDEN)
			continue;

		if (item->status & IT_OVERLAY)
		{
			// draw at screen coordinates; no clipping needed
			dx = item->x;
			dy = item->y;
		}
		else
		{
			dx = x + item->x;
			if (item->x)
			{
				if (!(item->status & IT_OFSX))
					x = dx = item->x + scrollx;
				else if (!(item->status & IT_TEMPORARY))
					x = dx;
			}

			dy = y + item->y;
			if (item->y)
			{
				if (!(item->status & IT_OFSY))
					y = dy = item->y + scrolly;
				else if (!(item->status & IT_TEMPORARY))
					y = dy;
			}

			if (dy < currentMenu->y)
			{
				cliptop = true;
				goto nodraw;
			}
			else if (dy > currentMenu->y + scrollheight)
			{
				clipbot = true;
				goto nodraw;
			}
		}

		w = M_DrawMenuItem(item, dx, dy, MENUCAPS, !hidecursor && item->status & IT_INTERACT && i == itemOn);

		if (!hidecursor && i == itemOn)
		{
			cursory = y;
			cursorx = x - w - 24 + currentMenu->cursoroffset;
		}

	nodraw:
		if (!(item->status & IT_OVERLAY))
			y += currentMenu->lineheight + M_ItemExtraOffset(item);
	}

	// DRAW THE SKULL CURSOR
	if (!hidecursor && M_ItemSelectable(&currentMenu->menuitems[itemOn]))
		V_DrawScaledPatch(cursorx, cursory, (noFurtherInput > 0 ? V_TRANSLUCENT : 0), W_CachePatchName("M_CURSOR", PU_CACHE));
	if (noFurtherInput > 0)
		V_DrawSmallString(cursorx, cursory+3, 0, "WAIT");

	x = currentMenu->x - 20 + currentMenu->cursoroffset;
	if (cliptop)
		V_DrawCharacter(x, currentMenu->y - (skullAnimCounter/5), '\x1A' | highlightflags, false); // up arrow
	if (clipbot)
		V_DrawCharacter(x, currentMenu->y + scrollheight + (skullAnimCounter/5), '\x1B' | highlightflags, false); // down arrow

	if (menustack[0] == MN_MAIN)
	{
		INT32 texty = vid.height - 10*vid.dup;
#define addtext(f, str) {\
V_DrawThinString(vid.dup, texty, MENUCAPS|V_NOSCALESTART|f, str);\
texty -= 10*vid.dup;\
}
		if (customversionstring[0] != '\0')
		{
			addtext(V_ALLOWLOWERCASE, customversionstring);
			addtext(0, "Mod version:");
		}
		else
		{
// Development -- show revision / branch info
#if defined(DEVELOP) || defined(COMMITVERSION)
			addtext(V_ALLOWLOWERCASE|V_YELLOWMAP|V_TRANSLUCENT, D_GetFancyBranchName());
			V_DrawThinString(0, 0, V_ALLOWLOWERCASE|V_ORANGEMAP|V_TRANSLUCENT|V_SNAPTOTOP, va("%s", complast));
#else // Regular build
			addtext(V_ALLOWLOWERCASE|V_TRANSLUCENT, va("%s", VERSIONSTRING));
#endif
			if (compuncommitted)
				addtext(V_REDMAP|V_STRINGDANCE|V_TRANSLUCENT, "! UNCOMMITTED CHANGES !");
		}
#undef addtext
	}

	M_DrawMenuTooltips();
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

	skins_cons_t[1].value = numskins-1;
	dummyfollower_cons_t[1].value = numfollowers-1;
	dummycolor_cons_t[1].value = numskincolors-1;

	j = R_SkinAvailable(cv_skin[0].string);
	if (j == -1)
		j = 0;

	CV_SetValue(&cv_chooseskin, j); // This causes crash sometimes?!

	return;
}

static INT32 M_GetLevelListTOL(void)
{
	switch (levellistmode)
	{
	case LLM_TIMEATTACK:
		return TOL_RACE;
	case LLM_ITEMBREAKER:
		return TOL_BATTLE;
	case LLM_BOSS:
		return TOL_BOSS;
	default:
		return G_TOLFlag(cv_newgametype.value);
	}
}

static boolean M_LevelAvailableOnPlatter(mapnum_t mapnum)
{
	// don't care
	(void)mapnum;
	return true;
}

//
// M_CanShowLevelOnPlatter
//
// Determines whether to show a given map in the various level-select lists.
//
static boolean M_CanShowLevelOnPlatter(mapnum_t mapnum)
{
	// Does the map exist?
	if (/*mapnum < 0 || */mapnum >= nummapheaders || !mapheaderinfo[mapnum])
		return false;

	// Does the map have a name?
	if (!mapheaderinfo[mapnum]->lvlttl[0])
		return false;

	// Does the map have a LUMP?
	if (mapheaderinfo[mapnum]->lumpnum == LUMPERROR)
		return false;

	// Does the typeoflevel match?
	if (!(mapheaderinfo[mapnum]->typeoflevel & M_GetLevelListTOL()))
		return false;

	switch (levellistmode)
	{
		case LLM_CREATESERVER:
			// Should the map be hidden?
			if (mapheaderinfo[mapnum]->menuflags & LF2_HIDEINMENU && mapnum+1 != gamemap)
				return cv_showallmaps.value;

			if (M_MapLocked(mapnum+1))
				return cv_showallmaps.value; // not unlocked

			return true;

		case LLM_CUPSELECT:
			return true;

		case LLM_TIMEATTACK:
		case LLM_ITEMBREAKER:
			if (mapheaderinfo[mapnum]->menuflags & LF2_NOTIMEATTACK)
				return false;

			if (M_MapLocked(mapnum+1))
				return false; // not unlocked

			if (M_SecretUnlocked(SECRET_HELLATTACK))
				return true; // now you're in hell

			if (mapheaderinfo[mapnum]->menuflags & LF2_HIDEINMENU)
				return false; // map hell

			if (mapheaderinfo[mapnum]->menuflags & LF2_VISITNEEDED)
			{
				maprecord_t *record = G_GetMapRecord(G_BuildMapName(mapnum+1));
				if (record == NULL || !record->visited)
					return false;
			}

			return true;

		default:
			return false;
	}
}

static boolean M_CanShowCupOnPlatter(const cupheader_t *cup)
{
	(void)cup;
	return true;
}

typedef int (qsort_f)(const void *, const void *);

static int Levelsort_Name(const void *a, const void *b)
{
	const INT16 m1 = *(const INT16 *)a, m2 = *(const INT16 *)b;
	int c = strcmp(mapheaderinfo[m1]->lvlttl, mapheaderinfo[m2]->lvlttl);
	return c != 0 ? c : m1 - m2;
}

static int Levelsort_Cup(const void *a, const void *b)
{
	const INT16 m1 = *(const INT16 *)a, m2 = *(const INT16 *)b;

	if (mapheaderinfo[m1]->cup == mapheaderinfo[m2]->cup)
	{
		// if cupless, sort by map ID, otherwise sort by order within the cup
		cupheader_t *cup = mapheaderinfo[m1]->cup;
		if (cup == NULL)
			return m1 - m2;

		INT16 pos1 = -1, pos2 = -1;
		for (INT32 i = 0; i < CUPCACHE_MAX; i++)
		{
			if (cup->levellist[i] == NULL)
				continue;
			if (cup->cachedlevels[i] == m1)
				pos1 = i;
			if (cup->cachedlevels[i] == m2)
				pos2 = i;
		}
		return pos1 - pos2 ? pos1 - pos2 : m1 - m2;
	}
	else
	{
		// sort cupless maps to bottom, otherwise sort by cup ID
		return mapheaderinfo[m1]->cup == NULL ? 1
		     : mapheaderinfo[m2]->cup == NULL ? -1
		     : mapheaderinfo[m1]->cup->id - mapheaderinfo[m2]->cup->id;
	}
}

static int Levelsort_File(const void *a, const void *b)
{
	const INT16 m1 = *(const INT16 *)a, m2 = *(const INT16 *)b;
	return mapheaderinfo[m1]->lumpnum - mapheaderinfo[m2]->lumpnum;
}

static int Cupsort_Name(const void *a, const void *b)
{
	const cupheader_t *c1 = *(const cupheader_t **)a, *c2 = *(const cupheader_t **)b;
	CLEANUP(pfree) char *name1 = strdup(CupName(c1, false)), *name2 = strdup(CupName(c2, false));
	int c = strcmp(name1, name2);
	return c != 0 ? c : c1->id - c2->id;
}

static int Cupsort_File(const void *a, const void *b)
{
	const cupheader_t *c1 = *(const cupheader_t **)a, *c2 = *(const cupheader_t **)b;
	// can't get the full lump number from within dehacked.c, this will do...
	int w = c1->wadnum - c2->wadnum;
	return w != 0 ? w : c1->id - c2->id;
}

static qsort_f *levelsortfuncs[] = {
	[LEVELSORT_ID] = NULL,
	[LEVELSORT_NAME] = Levelsort_Name,
	[LEVELSORT_CUP] = Levelsort_Cup,
	[LEVELSORT_FILE] = Levelsort_File,
};

static qsort_f *cupsortfuncs[] = {
	[LEVELSORT_ID] = NULL,
	[LEVELSORT_NAME] = Cupsort_Name,
	[LEVELSORT_FILE] = Cupsort_File,
};

static boolean M_MakeLevelSelectHeader(levelselectrow_t *row, mapheader_t *prev, mapheader_t *iter)
{
	const char *header = NULL;

	switch (cv_levelsort.value)
	{
	case LEVELSORT_CUP:
		if (iter == NULL || prev->cup != iter->cup)
			header = prev->cup == NULL ? "Unsorted" : CupName(prev->cup, true);
		break;
	case LEVELSORT_FILE:
		if (iter == NULL || WADFILENUM(prev->lumpnum) != WADFILENUM(iter->lumpnum))
		{
			const char *wadname = wadfiles[WADFILENUM(prev->lumpnum)]->filename;
			header = strrchr(wadname, *PATHSEP);
			if (header != NULL)
				header++;
			else
				header = wadname;
		}
		break;
	default:
		break;
	}

	return header != NULL && strlcpy(row->header, header, sizeof(row->header));
}

// a nice cup of tea, perhaps?
static void M_UpdateLevelPlatterCups(void)
{
	INT32 i, j;
	cupheader_t *cup;
	levelselectrow_t *lsrow;
	cupheader_t *sortedcups[UINT16_MAX]; // cope!
	UINT16 numsortedcups = 0;

	for (cup = kartcupheaders; cup != NULL; cup = cup->next)
		if (M_CanShowCupOnPlatter(cup))
			sortedcups[numsortedcups++] = cup;

	if (cupsortfuncs[cv_levelsort.value] != NULL)
		qs22j(sortedcups, numsortedcups, sizeof(*sortedcups), cupsortfuncs[cv_levelsort.value]);

	for (i = 0; i < numsortedcups; i++)
	{
		cup = sortedcups[i];
		levelselect.rows = Z_Realloc(levelselect.rows, ++levelselect.numrows*sizeof(levelselectrow_t), PU_STATIC, NULL);
		lsrow = &levelselect.rows[levelselect.numrows-1];
		memset(lsrow, 0, sizeof(*lsrow));

		lsrow->cupid = cup->id;
		lsrow->wide = true;

		if (cv_levelsort.value == LEVELSORT_FILE && (i == 0 || cup->wadnum != sortedcups[i-1]->wadnum))
		{
			const char *wadname = wadfiles[cup->wadnum]->filename;
			const char *header = strrchr(wadname, *PATHSEP);
			if (header != NULL)
				header++;
			else
				header = wadname;
			strlcpy(lsrow->header, header, sizeof(lsrow->header));
		}

		for (j = 0; j < cup->numlevels; j++)
		{
			if (lsrow->numcolumns >= MAPSPERROW)
				break;

			levelselectmap_t *lsmap = &lsrow->maps[lsrow->numcolumns++];
			mapnum_t mapnum = cup->cachedlevels[j];

			lsmap->mapnum = mapnum; // putting the map on the platter
			lsmap->available = M_LevelAvailableOnPlatter(mapnum);
			lsmap->color = 159;

			// individual map name
			if (lsmap->available)
			{
				char* mapname = G_BuildMapTitle(mapnum+1);
				strcpy(lsmap->mapname, mapname);
				Z_Free(mapname);
			}
			else
			{
				strcpy(lsmap->mapname, "???");
			}
		}
	}

	if (levelselect.row >= levelselect.numrows)
		levelselect.row = levelselect.numrows - 1;

	// dumbass hack until menu frames
	levelselect.firstmenuitem = 0;
	for (i = 0; i < menudefs[MN_CHANGELEVEL].numitems; i++)
	{
		if (!M_ItemSelectable(&menudefs[MN_CHANGELEVEL].menuitems[i]))
			continue;
		levelselect.firstmenuitem = i;
		break;
	}
	levelselect.lastmenuitem = menudefs[MN_CHANGELEVEL].numitems-1;
	for (i = menudefs[MN_CHANGELEVEL].numitems - 1; i >= 0; i--)
	{
		if (!M_ItemSelectable(&menudefs[MN_CHANGELEVEL].menuitems[i]))
			continue;
		levelselect.lastmenuitem = i;
		break;
	}
}

// refresh the level list while the platter is active
static void M_UpdateLevelPlatter(void)
{
	INT32 i, row = 0, headerRow = -1;
	mapheader_t *previtermap = NULL; // prevmap AND lastmap are taken...!
	boolean forceNewRow = true;
	INT16 sortedmaps[INT16_MAX];
	INT16 numsortedmaps = 0;
	INT16 randommap = -3; // need at least 2 available maps to safely randomize

	levelselect.numrows = 1;
	levelselect.rows = Z_Realloc(levelselect.rows, levelselect.numrows*sizeof(levelselectrow_t), PU_STATIC, NULL);
	memset(&levelselect.rows[0], 0, sizeof(levelselectrow_t));
	strcpy(levelselect.rows[0].header, "Options");
	levelselect.rows[0].wide = true;

	if (levellistmode == LLM_CUPSELECT)
	{
		M_UpdateLevelPlatterCups();
		return;
	}

	for (i = 0; i < nummapheaders; i++)
	{
		if (M_CanShowLevelOnPlatter(i))
		{
			sortedmaps[numsortedmaps++] = i;
			if (M_LevelAvailableOnPlatter(i) && randommap < -1)
				randommap++;
		}
	}

	if (levelsortfuncs[cv_levelsort.value] != NULL)
		qs22j(sortedmaps, numsortedmaps, sizeof(*sortedmaps), levelsortfuncs[cv_levelsort.value]);

	if (randommap == -1)
		while (randommap == -1 || !M_LevelAvailableOnPlatter(randommap) || randommap == cv_nextmap.value - 1)
			randommap = sortedmaps[M_RandomKey(numsortedmaps)];

	M_SetItemDisabled(MN_CHANGELEVEL, "RANDOM", randommap < 0);

	for (i = 0; i < numsortedmaps; i++)
	{
		mapnum_t mapnum = sortedmaps[i];
		mapheader_t *itermap = mapheaderinfo[mapnum];
		boolean wide = false;//(headermap->menuflags & LF2_WIDEICON);

		if (headerRow != -1 && M_MakeLevelSelectHeader(&levelselect.rows[headerRow], previtermap, itermap))
		{
			forceNewRow = true;
			headerRow = -1;
		}

		// preparing next position to drop mapnum into
		if (forceNewRow || wide || levelselect.rows[row].numcolumns == MAPSPERROW)
		{
			if (++row >= levelselect.numrows)
			{
				levelselect.numrows = row+1;
				levelselect.rows = Z_Realloc(levelselect.rows, levelselect.numrows*sizeof(levelselectrow_t), PU_STATIC, NULL);
				memset(&levelselect.rows[row], 0, sizeof(levelselectrow_t));
				levelselect.rows[row].wide = wide;
			}
		}

		if (mapnum == randommap)
		{
			levelselect.randomrow = row;
			levelselect.randomcol = levelselect.rows[row].numcolumns;
		}

		levelselectmap_t *lsmap = &levelselect.rows[row].maps[levelselect.rows[row].numcolumns++];

		lsmap->mapnum = mapnum; // putting the map on the platter
		lsmap->available = M_LevelAvailableOnPlatter(mapnum);
		lsmap->color = itermap->unlockrequired < 0 ? 159 : 63;

		// individual map name
		if (lsmap->available)
		{
			char* mapname = G_BuildMapTitle(mapnum+1);
			strcpy(lsmap->mapname, mapname);
			Z_Free(mapname);
		}
		else
		{
			strcpy(lsmap->mapname, "???");
		}

		// Done adding this one
		previtermap = itermap;
		forceNewRow = wide;
		if (headerRow == -1)
			headerRow = row;
	}

	// check headers for the last map added, too!
	if (previtermap != NULL)
		M_MakeLevelSelectHeader(&levelselect.rows[headerRow], previtermap, NULL);

	if (levelselect.row >= levelselect.numrows)
		levelselect.row = levelselect.numrows - 1;

	// dumbass hack until menu frames
	levelselect.firstmenuitem = 0;
	for (i = 0; i < menudefs[MN_CHANGELEVEL].numitems; i++)
	{
		if (!M_ItemSelectable(&menudefs[MN_CHANGELEVEL].menuitems[i]))
			continue;
		levelselect.firstmenuitem = i;
		break;
	}
	levelselect.lastmenuitem = menudefs[MN_CHANGELEVEL].numitems-1;
	for (i = menudefs[MN_CHANGELEVEL].numitems - 1; i >= 0; i--)
	{
		if (!M_ItemSelectable(&menudefs[MN_CHANGELEVEL].menuitems[i]))
			continue;
		levelselect.lastmenuitem = i;
		break;
	}
}

// finds a map/cup on the platter
// returns row in low 16 bits, column in upper 16 bits
static INT32 M_FindOnPlatter(INT32 which)
{
	for (INT32 row = 0; row < levelselect.numrows; row++)
	{
		if (levellistmode == LLM_CUPSELECT)
		{
			if (row > 0 && levelselect.rows[row].cupid == which)
			{
				return row;
			}
		}
		else for (INT32 col = 0; col < levelselect.rows[row].numcolumns; col++)
		{
			levelselectmap_t *lsmap = &levelselect.rows[row].maps[col];
			if (lsmap->mapnum == which)
			{
				return row | (col << 16);
			}
		}
	}
	return -1;
}

//
// MR_PrepareLevelPlatter
//
// Prepares a tasty dish of zones and acts!
// Call before any attempt to access a level platter.
//
INT32 MR_PrepareLevelPlatter(INT32 choice)
{
	if (choice < 0 || choice >= LLM__MAX)
		return false;

	levellistmode = choice;

	if (gamestate == GS_LEVEL)
		CV_SetValue(&cv_newgametype, gametype);

	M_SetItemVisible(MN_CHANGELEVEL, "GAMETYPE", levellistmode == LLM_CREATESERVER);
	M_SetItemVisible(MN_CHANGELEVEL, "ENCORE", levellistmode == LLM_CREATESERVER && M_SecretUnlocked(SECRET_ENCORE));
	M_SetItemVisible(MN_CHANGELEVEL, "RANDOM", levellistmode != LLM_CUPSELECT);

	levelsort_cons_t[LEVELSORT_CUP].strvalue = levellistmode == LLM_CUPSELECT ? NULL : "Cup"; // bruh

	if (levelselect.rows != NULL)
		Z_Free(levelselect.rows);

	memset(&levelselect, 0, sizeof(levelselect));

	M_UpdateLevelPlatter();

	// start on nextmap for convenience
	INT32 found = M_FindOnPlatter(levellistmode == LLM_CUPSELECT ? cv_nextcup.value - 1 : cv_nextmap.value - 1);
	if (found != -1)
	{
		levelselect.row = found & 0xffff;
		levelselect.column = found >> 16;
	}

	return true;
}

// this ought to go in doomtype
#define mod(x, y) (((x) % (y) + (y)) % (y))

static void M_MoveLevelPlatter(INT32 vmove)
{
	vmove %= levelselect.numrows;
	if (abs(vmove) > levelselect.numrows/2)
		vmove -= levelselect.numrows * intsign(vmove);

	levelselect.offsety = 0;
	for (INT32 count = abs(vmove); count > 0; count--)
	{
		if (vmove < 0)
			levelselect.offsety -= lsvseperation(levelselect.row) * FRACUNIT;
		levelselect.row = mod(levelselect.row + intsign(vmove), levelselect.numrows);
		if (vmove > 0)
			levelselect.offsety += lsvseperation(levelselect.row) * FRACUNIT;
	}

	// update highlighted header
	INT32 iter = levelselect.row;
	while (levelselect.rows[iter].header[0] == '\0')
	{
		iter = (iter - 1) % levelselect.numrows;
		if (iter == levelselect.row)
			break;
	}
	levelselect.highlight = iter;

	levelselect.namescroll = 0;
}

static void M_LevelPlatterSelect(boolean delay)
{
	if (levellistmode == LLM_CUPSELECT)
	{
		CV_SetValue(&cv_nextcup, levelselect.rows[levelselect.row].cupid + 1);
		S_StartSound(NULL, sfx_s221);
		if (menustack[1] == MN_SP_GRANDPRIX)
		{
			levelselect.selectdelay = 3*TICRATE;
			S_StartSound(NULL, sfx_s25f);
			S_FadeMusicFromVolume(0, -1, 1000); // le epic fadeout
		}
		else
		{
			levelselect.selectdelay = TICRATE/3;
		}
		return;
	}

	const levelselectmap_t *lsmap = &levelselect.rows[levelselect.row].maps[levelselect.column];
	if (lsmap->available)
	{
		CV_SetValue(&cv_nextmap, lsmap->mapnum + 1);
		levelselect.selectdelay = delay ? 6*TICRATE/7 : TICRATE/3;
		S_StartSound(NULL, sfx_s221);
	}
	else if (levelselect.offsety == 0) // prevent sound spam
	{
		levelselect.offsety = -8 * FRACUNIT;
		S_StartSound(NULL, sfx_s3kb2);
	}
}

INT32 MR_LevelPlatterRandom(INT32 choice)
{
	(void)choice;
	M_MoveLevelPlatter(levelselect.randomrow - levelselect.row);
	levelselect.row = levelselect.randomrow;
	levelselect.column = levelselect.randomcol;
	S_StopSoundByNum(sfx_menu1); // oh boy
	M_LevelPlatterSelect(true);

	return true;
}

//
// M_HandleLevelPlatter
//
// Reacts to your key inputs. Basically a mini menu thinker.
//
INT32 MR_HandleLevelPlatter(INT32 choice)
{
	INT32 hmove = 0, vmove = 0;

	if (levelselect.selectdelay > 0)
		return choice != KEY_CONSOLE; // let me open the console at least lol

	switch (choice)
	{
	case KEY_UPARROW:
	case KEY_DOWNARROW:
		vmove = choice == KEY_UPARROW ? -1 : 1;
		break;

	case KEY_PGUP:
	case KEY_PGDN:
		vmove = choice == KEY_PGUP ? -3 : 3;
		break;

	case KEY_LEFTARROW:
	case KEY_RIGHTARROW:
		hmove = choice == KEY_LEFTARROW ? -1 : 1;
		break;

	case KEY_BACKSPACE: // return to home
	{
		INT32 dest;
		if (levelselect.row > 0)
		{
			levelselectrow_t *lsrow = &levelselect.rows[levelselect.row];
			if (levellistmode == LLM_CUPSELECT)
				levelselect.returnto = lsrow->cupid;
			else
				levelselect.returnto = lsrow->maps[levelselect.column].mapnum;
			dest = 0;
		}
		else
		{
			INT32 found = M_FindOnPlatter(levelselect.returnto);
			if (found == -1)
				return false;
			dest = found & 0xffff;
			levelselect.column = found >> 16;
		}

		if (abs(levelselect.row - dest) > levelselect.numrows/2)
			vmove = dest - levelselect.numrows - levelselect.row;
		else
			vmove = dest - levelselect.row;
		break;
	}

	case KEY_ENTER:
		if (levelselect.row == 0)
			return false;

		M_LevelPlatterSelect(false);
		return true;
	}

	// move out of the options list only if you're at the top/bottom, or if pressing pgup/pgdn
	if (levelselect.row == 0 && (choice == KEY_UPARROW || choice == KEY_DOWNARROW)
	 && (vmove > 0 ? itemOn < levelselect.lastmenuitem : itemOn > levelselect.firstmenuitem))
		vmove = 0;

	// ...or if there's no other rows to move to???
	if (levelselect.numrows <= 1)
		vmove = 0;

	if (vmove != 0)
	{
		M_MoveLevelPlatter(vmove);

		// reset itemOn when moving into the options list
		if (choice != KEY_BACKSPACE && levelselect.row == (vmove > 0 ? levelselect.numrows : 0) - vmove)
			itemOn = vmove > 0 ? levelselect.firstmenuitem : levelselect.lastmenuitem;
	}

	if (hmove != 0)
	{
		if (levelselect.rows[levelselect.row].wide)
			return levelselect.row > 0;
		levelselect.namescroll = 0;
		levelselect.column = mod(levelselect.column + hmove, MAPSPERROW);
		levelselect.offsetx = lshseperation * -hmove * FRACUNIT;
	}

	if (vmove != 0 || hmove != 0)
	{
		if (levelselect.row > 0)
			S_StartSoundAtVolume(NULL, sfx_s3kb7, 160);
		else
			S_StartSound(NULL, sfx_menu1);
		return true;
	}

	return false;
}

static void M_DrawLevelPlatterHeader(INT32 y, const char *header, boolean headerhighlight, boolean allowlowercase)
{
	y -= (lsmapheight + lsmapnameheight + 1)/2;
	y += lsheadingheight - 12;
	V_DrawString(19, y, (headerhighlight ? V_YELLOWMAP : 0)|(allowlowercase ? V_ALLOWLOWERCASE : 0), header);
	y += 9;
	V_DrawFill(19, y, 281, 1, (headerhighlight ? yellowmap[3] : 3));
	V_DrawFill(300, y, 1, 1, 26);
	y++;
	V_DrawFill(19, y, 282, 1, 26);
}

// draws a map CENTERED around the given coordinates
static void M_DrawLevelPlatterMap(const levelselectmap_t *lsmap, INT32 x, INT32 y, boolean highlight, boolean wide)
{
	fixed_t scale = highlight ? FRACUNIT : FRACUNIT/2;
	fixed_t width = wide ? 282*FRACUNIT : lsmapwidth*scale;
	fixed_t height = (lsmapheight + lsmapnameheight)*scale;

	// wide thumbnails? mmm... i dunno... maybe...
	patch_t *patch;
	boolean encore = cv_kartencore.value == 1 && levellistmode == LLM_CREATESERVER && cv_newgametype.value == GT_RACE;
	fixed_t thumbscale = M_GetMapThumbnail(lsmap->available ? lsmap->mapnum : NEXTMAP_INVALID, &patch);
	thumbscale = FixedMul(thumbscale/4, scale);

	V_DrawSciencePatch(x*FRACUNIT - patch->width*thumbscale/2 + (encore ? patch->width*thumbscale : 0),
	                   y*FRACUNIT - patch->height*thumbscale/2 - lsmapnameheight*scale/2,
	                   encore ? V_FLIP : 0, patch, thumbscale);
	//if (!lsmap->available)
		//M_DrawStaticBox(x, y, V_80TRANS, wide ? 282 : 80, 50);

	if (encore && highlight)
	{
		static angle_t rubyfloattime = 0;
		const fixed_t rubyheight = FSIN(rubyfloattime)*2;
		V_DrawSciencePatch(x*FRACUNIT, y*FRACUNIT - rubyheight, 0, W_CachePatchName("RUBYICON", PU_CACHE), FRACUNIT);
		rubyfloattime += FixedMul(ANGLE_MAX/NEWTICRATE, renderdeltatics);
	}

	x = x*FRACUNIT - width/2;
	y = y*FRACUNIT + height/2 - lsmapnameheight*scale;
	V_SetClipRect(x, y, width, (lsmapnameheight / (wide || highlight ? 1 : 2))*FRACUNIT, 0);

	V_DrawFill(-9999, -9999, 22222, 22222, V_NOSCALESTART|lsmap->color);
	if (wide || highlight)
	{
		INT32 scrollamt = V_ThinStringWidth(lsmap->mapname, MENUCAPS|V_6WIDTHSPACE)*FRACUNIT - width;
		if (scrollamt > 0)
		{
			// scroll the map's name!
			INT32 t = ((levelselect.namescroll % (scrollamt/FRACUNIT + TICRATE*2)) - TICRATE)*FRACUNIT;
			x -= max(0, min(t, scrollamt));
			if (t > 0 && t <= scrollamt)
				x = x + FRACUNIT - R_GetTimeFrac(RTF_MENU);
		}
		else
		{
			x -= scrollamt/2; // center it instead!
		}
		V_DrawThinStringAtFixed(x, y - FRACUNIT/2, MENUCAPS|V_6WIDTHSPACE|(highlight ? highlightflags : 0), lsmap->mapname);
	}
	else
	{
		INT32 scrollamt = V_SmallStringWidth(lsmap->mapname, MENUCAPS)*FRACUNIT - width;

		// screw adding more string drawing macros
		V__DrawOneScaleString(x - min(0, scrollamt/2), y, FRACUNIT/2, MENUCAPS, HU_FONT, lsmap->mapname);
	}

	V_ClearClipRect();
}

static void M_DrawLevelPlatterRow(UINT8 row, INT32 y)
{
	const boolean rowhighlight = (row == levelselect.row);
	const levelselectrow_t *lsrow = &levelselect.rows[row];

	if (lsrow->header[0])
	{
		M_DrawLevelPlatterHeader(y, lsrow->header, (rowhighlight || (row == levelselect.highlight)), false);
		y += lsheadingheight;
	}

	if (row == 0)
	{
		currentMenu->y = y - lsmapheight/2 + 2;
		V_SetClipRect(0, (y - lsmapheight/2)*FRACUNIT, 320*FRACUNIT, (currentMenu->scrollheight+12)*FRACUNIT, 0);
		V_DrawFill((BASEVIDWIDTH - 282)/2, -9999, 282, 22222, 27);
		MD_DrawGenericMenu();
		V_ClearClipRect();
		return;
	}

	if (levellistmode == LLM_CUPSELECT)
	{
		cupheader_t *cup;

		// relax, we're only doing this 7 times per frame at most...
		for (cup = kartcupheaders; cup != NULL; cup = cup->next)
			if (cup->id == lsrow->cupid)
				break;

		//V_DrawScaledPatch(lsbasex - 14, y - 16, 0, W_CachePatchName(cup->icon, PU_CACHE));
		V_DrawCenteredString(lsbasex + 2, y - 4, MENUCAPS|(rowhighlight ? V_YELLOWMAP : 0), CupName(cup, false));
	}

	for (UINT8 col = 0; col < lsrow->numcolumns; col++)
	{
		if (levellistmode == LLM_CUPSELECT)
			M_DrawLevelPlatterMap(&lsrow->maps[col], lsbasex + 70 + col*lshseperationcup, y, false, false);
		else if (!rowhighlight || col != levelselect.column)
			M_DrawLevelPlatterMap(&lsrow->maps[col], lsbasex + col*lshseperation, y, false, lsrow->wide);
	}
}

void MD_DrawLevelPlatterMenu(void)
{
	UINT8 row = levelselect.row;
	INT32 y = lsbasey + FixedInt(levelselect.offsety) - getheadingoffset(row);
	INT32 hiliy = INT32_MIN;
	INT32 minrows = (vid.scaledheight + lsbasevseperation/2)/lsbasevseperation;
	INT32 cursorx = levelselect.rows[row].wide ? 0 : levelselect.column * lshseperation;

	// finds row at top of the screen
	while (y >= -lsbasevseperation/2)
	{
		if (row == 0)
		{
			if (levelselect.numrows < minrows)
				break;
			row = levelselect.numrows;
		}
		row--;
		y -= lsvseperation(row);
	}

	// draw from top to bottom
	while (y <= (vid.scaledheight) + lsbasevseperation/2)
	{
		M_DrawLevelPlatterRow(row, y);
		if (row == levelselect.row)
			hiliy = y + getheadingoffset(row);
		y += lsvseperation(row);
		if (row == levelselect.numrows-1)
		{
			if (levelselect.numrows < minrows)
				break;
			row = UINT8_MAX;
		}
		row++;
	}

	// draw cursor box
	if (levelselect.row > 0 && hiliy != INT32_MIN)
	{
		levelselectrow_t *lsrow = &levelselect.rows[levelselect.row];
		if (!lsrow->wide && levelselect.column < lsrow->numcolumns)
			M_DrawLevelPlatterMap(&lsrow->maps[levelselect.column], lsbasex + cursorx, hiliy, true, lsrow->wide);

		fixed_t fx = (lsbasex + cursorx - lsmapwidth/2)*FRACUNIT + levelselect.offsetx;
		fixed_t fy = hiliy*FRACUNIT - (lsmapheight + (levellistmode == LLM_CUPSELECT ? 0 : lsmapnameheight))*FRACUNIT/2;
		fixed_t width = (lsrow->wide ? 300 : lsmapwidth)*FRACUNIT;
		fixed_t height = lsmapheight*FRACUNIT;
		INT32 trans = 0;
		if (levelselect.selectdelay > 0 ? skullAnimCounter & 1 : skullAnimCounter < 4)
			trans = V_GetStringColormap(highlightflags)[0];

		V_DrawFixedFill(fx, fy,                               width, FRACUNIT, trans);
		V_DrawFixedFill(fx, fy + height - FRACUNIT,           width, FRACUNIT, trans);
		V_DrawFixedFill(fx, fy + FRACUNIT,                    FRACUNIT, height - 2*FRACUNIT, trans);
		V_DrawFixedFill(fx + width - FRACUNIT, fy + FRACUNIT, FRACUNIT, height - 2*FRACUNIT, trans);
	}

	// handle movement of cursor box
	fixed_t cursormovefrac = FixedDiv(2, 3);
	if (abs(levelselect.offsety) > FRACUNIT/2)
	{
		fixed_t offs = levelselect.offsety;
		fixed_t newoffs = FixedMul(offs, cursormovefrac);
		fixed_t deltaoffs = newoffs - offs;
		newoffs = offs + FixedMul(deltaoffs, renderdeltatics);
		levelselect.offsety = newoffs;
	}
	else
		levelselect.offsety = 0;

	if (abs(levelselect.offsetx) > FRACUNIT/4)
	{
		fixed_t offs = levelselect.offsetx;
		fixed_t newoffs = FixedMul(offs, cursormovefrac);
		fixed_t deltaoffs = newoffs - offs;
		newoffs = offs + FixedMul(deltaoffs, renderdeltatics);
		levelselect.offsetx = newoffs;
	}
	else
		levelselect.offsetx = 0;

	M_DrawMenuTitle();
}

//
// M_CanShowLevelInList
//
// Determines whether to show a given map in the various level-select lists.
// Set gt = -1 to ignore gametype.
//
boolean M_CanShowLevelInList(mapnum_t mapnum)
{
	return M_CanShowLevelOnPlatter(mapnum) && M_LevelAvailableOnPlatter(mapnum);
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
void MD_DrawImageDef(void)
{
	for (INT16 i = 0; i < currentMenu->numitems; i++)
	{
		if (i == itemOn)
			currentMenu->menuitems[i].status &= ~IT_HIDDEN;
		else
			currentMenu->menuitems[i].status |= IT_HIDDEN;
	}

	MD_DrawGenericMenu();

	if (!currentMenu->menuitems[itemOn].argument)
	{
		V_DrawString(2,BASEVIDHEIGHT-10, MENUCAPS|V_YELLOWMAP, va("%d", (itemOn<<1)-1)); // intentionally not highlightflags, unlike below
		V_DrawRightAlignedString(BASEVIDWIDTH-2,BASEVIDHEIGHT-10, MENUCAPS|V_YELLOWMAP, va("%d", itemOn<<1)); // ditto
	}
	else
	{
		INT32 x = BASEVIDWIDTH>>1, y = (BASEVIDHEIGHT>>1) - 4;
		x += (itemOn ? 1 : -1)*((BASEVIDWIDTH>>2) + 10);
		V_DrawCenteredString(x, y-10, MENUCAPS|highlightflags, "Use arrow keys");
		V_DrawCharacter(x - 10 - (skullAnimCounter/5), y,
			'\x1C' | highlightflags, false); // left arrow
		V_DrawCharacter(x + 2 + (skullAnimCounter/5), y,
			'\x1D' | highlightflags, false); // right arrow
		V_DrawCenteredString(x, y+10, MENUCAPS|highlightflags, "to leaf through");
	}
}

// Handles the ImageDefs.  Just a specialized function that
// uses left and right movement.
INT32 MR_HandleImageDef(INT32 choice)
{
	switch (choice)
	{
		case KEY_RIGHTARROW:
			if (itemOn >= (INT16)(currentMenu->numitems-1))
				break;
			S_StartSound(NULL, sfx_menu1);
			M_RawSetItemOn(currentMenu, itemOn + 1);
			break;

		case KEY_LEFTARROW:
			if (!itemOn)
				break;
			S_StartSound(NULL, sfx_menu1);
			M_RawSetItemOn(currentMenu, itemOn - 1);
			break;

		default:
			return false;
	}

	return true;
}

// ======================
// MISC MAIN MENU OPTIONS
// ======================

INT32 MR_AddonsOptions(INT32 choice)
{
	(void)choice;
	Addons_option_Onchange();
	return true;
}

#define LOCATIONSTRING1 "Visit \x83SRB2.ORG/MODS\x80 to get & make addons!"
#define LOCATIONSTRING2 "Visit \x88SRB2.ORG/MODS\x80 to get & make addons!"

INT32 MR_Addons(INT32 choice)
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

	strlcpy(menupath, pathname, MAXFILEPATH);
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
		return false;
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

	return true;
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
	static char header[MAXFILEPATH];

	strlcpy(header, va("%s folder%s", cv_addons_option.string, menupath+menupathindex[menudepth-1]-1), MAXFILEPATH);
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

#define FILEADDSFX sfx_addfil
#define FILENOTADDSFX sfx_notadd
#define FILEERRORSFX sfx_adderr

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
			S_StartSound(NULL, FILENOTADDSFX);
			if (refreshdirmenu & REFRESHDIR_MAX)
				message = va("%c%s\x80\nMaximum number of addons reached.\nA file could not be loaded.\nIf you wish to play with this addon, restart the game to clear existing ones.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname);
			else
				message = va("%c%s\x80\nA file was not loaded.\nCheck the console log for more information.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname);
		}
		else if (refreshdirmenu & (REFRESHDIR_WARNING|REFRESHDIR_ERROR))
		{
			S_StartSound(NULL, FILEERRORSFX);
			message = va("%c%s\x80\nA file was loaded with %s.\nCheck the console log for more information.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname, ((refreshdirmenu & REFRESHDIR_ERROR) ? "errors" : "warnings"));
		}
		else if (majormods && !prevmajormods)
		{
			S_StartSound(NULL, FILEADDSFX);
			message = va("%c%s\x80\nYou've loaded a gameplay-modifying addon.\n\nRecord Attack data will be saved to seperate save.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname);
			prevmajormods = majormods;
		}

		if (message)
		{
			M_StartMessage(message, M_AddonsClearName, MM_EVENTHANDLER);
			return true;
		}

		S_StartSound(NULL, FILEADDSFX);
		CLEARNAME;
	}

	return false;
}

void MD_DrawAddons(void)
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
		V_DrawCenteredString(BASEVIDWIDTH/2, 5, MENUCAPS|warningflags, "Adding files mid-game may cause problems.");
	else
		V_DrawCenteredString(BASEVIDWIDTH/2, 5, MENUCAPS, (recommendedflags == V_SKYMAP ? LOCATIONSTRING2 : LOCATIONSTRING1));

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

	V_DrawString(x-21, (y - 16) + 4, MENUCAPS|highlightflags|V_ALLOWLOWERCASE, M_AddonsHeaderPath());
	V_DrawFill(x-21, (y - 16) + 13, MAXSTRINGLENGTH*8+6, 1, hilicol);
	V_DrawFill(x-21, (y - 16) + 14, MAXSTRINGLENGTH*8+6, 1, 30);

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
		V_DrawString(19, y+4 - (skullAnimCounter/5), MENUCAPS|highlightflags, "\x1A");

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
		V_DrawString(19, y-12 + (skullAnimCounter/5), MENUCAPS|highlightflags, "\x1B");

	y = BASEVIDHEIGHT - currentMenu->y + 1;

	M_DrawTextBox(x - (21 + 5), y, MAXSTRINGLENGTH, 1);

	if (menusearch.length)
		M_DrawTextInput(x - 18, y + 8, &menusearch, V_ALLOWLOWERCASE);
	else
		V_DrawString(x - 18, y + 8, V_ALLOWLOWERCASE|V_TRANSLUCENT, "Type to search...");

	x -= (21 + 5 + 16);
	V_DrawSmallScaledPatch(x, y + 4, (menusearch.length ? 0 : V_TRANSLUCENT), addonsp[NUM_EXT+3]);

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

INT32 MR_HandleAddons(INT32 choice)
{
	if (M_TextInputHandle(&menusearch, choice))
	{
		S_StartSound(NULL,sfx_menu1);

		char *tempname = NULL;
		if (dirmenu && dirmenu[dir_on[menudepthleft]])
			tempname = Z_StrDup(dirmenu[dir_on[menudepthleft]]+DIR_STRING); // don't need to I_Error if can't make - not important, just QoL
#if 0 // much slower
		if (!preparefilemenu(true, false))
		{
			UNEXIST;
			return true;
		}
#else // streamlined
		searchfilemenu(tempname);
#endif
		return true;
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
					S_StartSound(NULL, FILENOTADDSFX);
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
									S_StartSound(NULL, FILEERRORSFX);
									M_StartMessage(va("%c%s\x80\nThis folder is empty.\n\n(Press a key)\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), M_AddonsHeaderPath()),NULL,MM_NOTHING);
									menupath[menupathindex[++menudepthleft]] = 0;

									if (!preparefilemenu(true, false))
									{
										UNEXIST;
										return true;
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
								S_StartSound(NULL, FILENOTADDSFX);
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
								return true;
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
							S_StartSound(NULL, FILENOTADDSFX);
					}
				}
				if (refresh)
					refreshdirmenu |= REFRESHDIR_NORMAL;
			}
			break;
		default:
			return false;
	}
	return true;
}

INT32 MR_QuitAddons(INT32 choice)
{
	(void)choice;
	closefilemenu(true);
	return true;
}

// ---- REPLAY HUT -----
menudemo_t *demolist = NULL;

#define DF_ENCORE       0x40
static INT16 replayScrollTitle = 0;
static INT16 replayScrollTitleOld = 0;
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
			snprintf(demolist[i].filepath, MAXFILEPATH, "%s%s", menupath, dirmenu[i] + DIR_STRING);
			sprintf(demolist[i].title, ".....");
		}
	}
}

INT32 MR_ReplayHut(INT32 choice)
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
		return false;
	}
	else if (!demo.inreplayhut)
		dir_on[menudepthleft] = 0;
	demo.inreplayhut = true;

	replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;

	PrepReplayList();

	//M_ClearMenus(true);
	modeattacking = ATTACKING_MENU;
	titlemapinaction = false; // Nope don't give us HOMs please

	demo.rewinding = false;
	CL_ClearRewinds();

	return true;
}

INT32 MR_HandleReplayHutList(INT32 choice)
{
	switch (choice)
	{
	case KEY_UPARROW:
		if (dir_on[menudepthleft])
			dir_on[menudepthleft]--;
		else
			break;

		S_StartSound(NULL, sfx_menu1);
		replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;
		replayScrollTitleOld =0;
		break;

	case KEY_DOWNARROW:
		if (dir_on[menudepthleft] < sizedirmenu-1)
			dir_on[menudepthleft]++;
		else
			break;

		S_StartSound(NULL, sfx_menu1);
		replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;
		replayScrollTitleOld =0;
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
							M_ExitMenu();
							return true;
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
					M_ExitMenu();
					return true;
				}
				PrepReplayList();
				break;
			default:
				currentMenu->lastOn = itemOn;
				M_EnterMenu(MN_MISC_REPLAYSTART, false, 0); // ReplayDef's quit routine would boot us back to the title screen

				replayScrollTitle = 0; replayScrollDelay = TICRATE; replayScrollDir = 1;
				replayScrollTitleOld =0;

				switch (demolist[dir_on[menudepthleft]].addonstatus)
				{
				case DFILE_ERROR_CANNOTLOAD:
					// Only show "Watch Replay Without Addons"
					M_SetItemVisible(MN_MISC_REPLAYSTART, "LOADWATCH", false);
					M_SetItemVisible(MN_MISC_REPLAYSTART, "NOLOADWATCH", true);
					M_SetItemVisible(MN_MISC_REPLAYSTART, "WATCH", false);
					M_SetItemOn(MN_MISC_REPLAYSTART, "NOLOADWATCH");
					break;

				case DFILE_ERROR_NOTLOADED:
				case DFILE_ERROR_INCOMPLETEOUTOFORDER:
					// Show "Load Addons and Watch Replay" and "Watch Replay Without Addons"
					M_SetItemVisible(MN_MISC_REPLAYSTART, "LOADWATCH", true);
					M_SetItemVisible(MN_MISC_REPLAYSTART, "NOLOADWATCH", true);
					M_SetItemVisible(MN_MISC_REPLAYSTART, "WATCH", false);
					M_SetItemOn(MN_MISC_REPLAYSTART, "LOADWATCH");
					break;

				case DFILE_ERROR_EXTRAFILES:
				case DFILE_ERROR_OUTOFORDER:
				default:
					// Show "Watch Replay"
					M_SetItemVisible(MN_MISC_REPLAYSTART, "LOADWATCH", false);
					M_SetItemVisible(MN_MISC_REPLAYSTART, "NOLOADWATCH", false);
					M_SetItemVisible(MN_MISC_REPLAYSTART, "WATCH", true);
					M_SetItemOn(MN_MISC_REPLAYSTART, "WATCH");
					break;
				}
		}

		break;

	default:
		return false;
	}
	return true;
}

#define SCALEDVIEWWIDTH (vid.scaledwidth)
#define SCALEDVIEWHEIGHT (vid.scaledheight)
static void DrawReplayHutReplayInfo(void)
{
	patch_t *patch;
	UINT8 *colormap;
	INT32 x, y, w, h;

	switch (demolist[dir_on[menudepthleft]].type)
	{
	case MD_NOTLOADED:
		V_DrawCenteredString(160, 40, MENUCAPS|V_SNAPTOTOP, "Loading replay information...");
		break;

	case MD_INVALID:
		V_DrawCenteredString(160, 40, MENUCAPS|V_SNAPTOTOP|warningflags, "This replay cannot be played.");
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
		if (patch == blanklvl && demolist[dir_on[menudepthleft]].map == NEXTMAP_INVALID)
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
			V_DrawString(x, y, MENUCAPS|V_SNAPTOTOP, G_BuildMapTitle(demolist[dir_on[menudepthleft]].map+1));
		else
			V_DrawString(x, y, V_SNAPTOTOP|V_ALLOWLOWERCASE|V_TRANSLUCENT, "Level is not loaded.");

		if (demolist[dir_on[menudepthleft]].numlaps)
			V_DrawThinString(x, y+9, V_SNAPTOTOP|V_ALLOWLOWERCASE, va("(%d laps)", demolist[dir_on[menudepthleft]].numlaps));

		const char *gametypename = gametypes[demolist[dir_on[menudepthleft]].gametype] ? gametypes[demolist[dir_on[menudepthleft]].gametype]->name : "Unknown";
		const char *gamespeed = kartspeed_cons_t[(demolist[dir_on[menudepthleft]].kartspeed & ~DF_ENCORE) + 1].strvalue;
		const char *gamemodestring = va("%s (%s speed)", gametypename, gamespeed);

		V_DrawString(x, y+20, V_SNAPTOTOP|V_ALLOWLOWERCASE, gamemodestring);

		if (!demolist[dir_on[menudepthleft]].standings[0].ranking)
		{
			// No standings were loaded!
			V_DrawString(x, y+39, V_SNAPTOTOP|V_ALLOWLOWERCASE|V_TRANSLUCENT, "No standings available.");


			break;
		}

		V_DrawThinString(x, y+29, MENUCAPS|V_SNAPTOTOP|highlightflags, "Winner");
		V_DrawString(x+38, y+30, V_SNAPTOTOP|V_ALLOWLOWERCASE, demolist[dir_on[menudepthleft]].standings[0].name);

		if (demolist[dir_on[menudepthleft]].gametype == GT_RACE)
		{
			V_DrawThinString(x, y+39, MENUCAPS|V_SNAPTOTOP|highlightflags, "Time");
			V_DrawRightAlignedString(x+84, y+40, V_SNAPTOTOP, va("%d'%02d\"%02d",
											G_TicsToMinutes(demolist[dir_on[menudepthleft]].standings[0].timeorscore, true),
											G_TicsToSeconds(demolist[dir_on[menudepthleft]].standings[0].timeorscore),
											G_TicsToCentiseconds(demolist[dir_on[menudepthleft]].standings[0].timeorscore)
			));
		}
		else
		{
			V_DrawThinString(x, y+39, MENUCAPS|V_SNAPTOTOP|highlightflags, "Score");
			V_DrawString(x+32, y+40, V_SNAPTOTOP, va("%d", demolist[dir_on[menudepthleft]].standings[0].timeorscore));
		}

		// Character face!
		if (demolist[dir_on[menudepthleft]].standings[0].skin < numskins && W_CheckNumForName(skins[demolist[dir_on[menudepthleft]].standings[0].skin].facewant) != LUMPERROR)
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

static fixed_t finalscroll = 0;
static fixed_t oldfinalscroll = 0;

void MD_DrawReplayHut(void)
{
	INT32 x, y, cursory = 0;
	INT16 i;
	boolean processed_one_this_frame = false;

	static UINT16 replayhutmenuy = 0;

	if (cv_vhseffect.value)
		V_DrawVhsEffect(false);

	// Draw menu choices
	x = currentMenu->x;
	y = currentMenu->y;

	INT32 maxy;
	// Scroll menu items if needed
	cursory = y + dir_on[menudepthleft]*10;
	maxy = y + sizedirmenu*10;

	if (cursory > maxy - 20)
		cursory = maxy - 20;

	if (cursory - replayhutmenuy > SCALEDVIEWHEIGHT-50)
		replayhutmenuy += (cursory-SCALEDVIEWHEIGHT-replayhutmenuy + 51)/2;
	else if (cursory - replayhutmenuy < 110)
		replayhutmenuy += (max(0, cursory-110)-replayhutmenuy - 1)/2;

	y -= replayhutmenuy;

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

		if (i == (INT16)dir_on[menudepthleft])
		{
			cursory = localy;

			if (renderisnewtic)
			{
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
			}

			replayScrollTitleOld = replayScrollTitle;
			finalscroll = (localx - (replayScrollTitle>>1))*FRACUNIT;
			oldfinalscroll = (localx - (replayScrollTitleOld>>1))*FRACUNIT;
			V_DrawStringAtFixed(R_InterpolateFixed(oldfinalscroll, finalscroll), localy*FRACUNIT, V_SNAPTOTOP|V_SNAPTOLEFT|highlightflags|V_ALLOWLOWERCASE, demolist[i].title);
		}
		else
			V_DrawString(localx, localy, V_SNAPTOTOP|V_SNAPTOLEFT|V_ALLOWLOWERCASE, demolist[i].title);
	}

	// Draw scrollbar
	y = sizedirmenu*10 + 30;
	if (y > SCALEDVIEWHEIGHT-80)
	{
		V_DrawFill(BASEVIDWIDTH-4, 75, 4, SCALEDVIEWHEIGHT-80, V_SNAPTOTOP|V_SNAPTORIGHT|159);
		V_DrawFill(BASEVIDWIDTH-3, 76 + (SCALEDVIEWHEIGHT-80) * replayhutmenuy / y, 2, (((SCALEDVIEWHEIGHT-80) * (SCALEDVIEWHEIGHT-80))-1) / y - 1, V_SNAPTOTOP|V_SNAPTORIGHT|149);
	}

	// Draw the cursor
	V_DrawScaledPatch(currentMenu->x - 24, cursory, V_SNAPTOTOP|V_SNAPTOLEFT,
		W_CachePatchName("M_CURSOR", PU_CACHE));

	// Now draw some replay info!
	V_DrawFill(10, 10, 300, 60, V_SNAPTOTOP|159);

	DrawReplayHutReplayInfo();
}

void MD_DrawReplayStartMenu(void)
{
	const char *warning;
	UINT8 i;
	INT32 xoffs, yoffs;

	MD_DrawGenericMenu();

#define STARTY 62-(replayScrollTitle>>1)
	// Draw rankings beyond first
	for (i = 1; i < MAXPLAYERS && demolist[dir_on[menudepthleft]].standings[i].ranking; i++)
	{
		xoffs = yoffs = 0;
		patch_t *patch;
		UINT8 *colormap;

		V_DrawRightAlignedString(BASEVIDWIDTH-100, STARTY + i*20, MENUCAPS|V_SNAPTOTOP|highlightflags, va("%2d", demolist[dir_on[menudepthleft]].standings[i].ranking));
		V_DrawThinString(BASEVIDWIDTH-96, STARTY + i*20, V_SNAPTOTOP|V_ALLOWLOWERCASE, demolist[dir_on[menudepthleft]].standings[i].name);

		if (demolist[dir_on[menudepthleft]].standings[i].timeorscore == UINT32_MAX-1)
			V_DrawThinString(BASEVIDWIDTH-92, STARTY + i*20 + 9, MENUCAPS|V_SNAPTOTOP, "No Contest");
		else if (demolist[dir_on[menudepthleft]].gametype == GT_RACE)
			V_DrawRightAlignedString(BASEVIDWIDTH-40, STARTY + i*20 + 9, V_SNAPTOTOP, va("%d'%02d\"%02d",
											G_TicsToMinutes(demolist[dir_on[menudepthleft]].standings[i].timeorscore, true),
											G_TicsToSeconds(demolist[dir_on[menudepthleft]].standings[i].timeorscore),
											G_TicsToCentiseconds(demolist[dir_on[menudepthleft]].standings[i].timeorscore)
			));
		else
			V_DrawString(BASEVIDWIDTH-92, STARTY + i*20 + 9, V_SNAPTOTOP, va("%d", demolist[dir_on[menudepthleft]].standings[i].timeorscore));

		// Character face!
		if (demolist[dir_on[menudepthleft]].standings[i].skin < numskins && W_CheckNumForName(skins[demolist[dir_on[menudepthleft]].standings[i].skin].facewant) != LUMPERROR)
		{
			patch = faceprefix[demolist[dir_on[menudepthleft]].standings[i].skin][FACE_RANK];

			xoffs = SHORT(patch->leftoffset);
			yoffs = SHORT(patch->topoffset);

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

		V_DrawMappedPatch(BASEVIDWIDTH-5 - SHORT(patch->width) + xoffs, STARTY + i*20 + yoffs, V_SNAPTOTOP, patch, colormap);
	}
#undef STARTY

	// Handle scrolling rankings
	if (renderisnewtic)
	{
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

INT32 MR_QuitReplayHut(INT32 choice)
{
	(void)choice;

	if (demolist)
		Z_Free(demolist);
	demolist = NULL;

	demo.inreplayhut = false;
	if (!M_AttackOnStack())
	{
		modeattacking = ATTACKING_NONE;
	}
	return true;
}

INT32 MR_HutStartReplay(INT32 choice)
{
	(void)choice;

	M_RestoreMenuStack(true);
	COM_BufAddText(va("playdemo \"%s\" %s",
	               demolist[dir_on[menudepthleft]].filepath + strlen(srb2home) + 1, // dumb hack
	               M_IsItemOn(MN_MISC_REPLAYSTART, "LOADWATCH") ? "-addfiles" : "-force"));
	return true;
}

void M_SetPlaybackMenuPointer(void)
{
	M_SetItemOn(MN_PLAYBACK, "PAUSE");
}

void MD_DrawPlaybackMenu(void)
{
	INT16 i;
	patch_t *icon;
	UINT8 *activemap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_GOLD, GTC_MENUCACHE);
	UINT32 transmap = max(0, (INT32)(leveltime - playback_last_menu_interaction_leveltime - 4*TICRATE)) / 5;
	transmap = min(8, transmap) << V_ALPHASHIFT;

	if (leveltime - playback_last_menu_interaction_leveltime >= 6*TICRATE)
		playback_last_menu_interaction_leveltime = leveltime - 6*TICRATE;

	// Toggle items
	boolean showpause = paused && !demo.rewinding;

	M_SetItemVisible(MN_PLAYBACK, "PAUSE", !showpause);
	M_SetItemVisible(MN_PLAYBACK, "FASTFORWARD", !showpause);
	M_SetItemVisible(MN_PLAYBACK, "REWIND", !showpause);
	M_SetItemVisible(MN_PLAYBACK, "RESUME", showpause);
	M_SetItemVisible(MN_PLAYBACK, "ADVANCEFRAME", showpause);
	M_SetItemVisible(MN_PLAYBACK, "REWINDFRAME", showpause);

	if (showpause)
	{
		if (M_IsItemOn(MN_PLAYBACK, "REWIND"))
			M_SetItemOn(MN_PLAYBACK, "REWINDFRAME");
		else if (M_IsItemOn(MN_PLAYBACK, "PAUSE"))
			M_SetItemOn(MN_PLAYBACK, "RESUME");
		else if (M_IsItemOn(MN_PLAYBACK, "FASTFORWARD"))
			M_SetItemOn(MN_PLAYBACK, "ADVANCEFRAME");
	}
	else
	{
		if (M_IsItemOn(MN_PLAYBACK, "REWINDFRAME"))
			M_SetItemOn(MN_PLAYBACK, "REWIND");
		else if (M_IsItemOn(MN_PLAYBACK, "RESUME"))
			M_SetItemOn(MN_PLAYBACK, "PAUSE");
		else if (M_IsItemOn(MN_PLAYBACK, "ADVANCEFRAME"))
			M_SetItemOn(MN_PLAYBACK, "FASTFORWARD");
	}

	M_SetItemVisible(MN_PLAYBACK, "NUMVIEWS", !modeattacking);
	M_SetItemVisible(MN_PLAYBACK, "VIEW1", !modeattacking && r_splitscreen >= 0);
	M_SetItemVisible(MN_PLAYBACK, "VIEW2", !modeattacking && r_splitscreen >= 1);
	M_SetItemVisible(MN_PLAYBACK, "VIEW3", !modeattacking && r_splitscreen >= 2);
	M_SetItemVisible(MN_PLAYBACK, "VIEW4", !modeattacking && r_splitscreen >= 3);

	if (modeattacking)
	{
		M_SetItemX(MN_PLAYBACK, "FREECAM", 72);
		M_SetItemX(MN_PLAYBACK, "QUIT", 88);
		currentMenu->x = BASEVIDWIDTH/2 - 52;
	}
	else
	{
		M_SetItemX(MN_PLAYBACK, "FREECAM", 156);
		M_SetItemX(MN_PLAYBACK, "QUIT", 172);
		currentMenu->x = BASEVIDWIDTH/2 - 88;
	}

	// wip
	//M_DrawTextBox(currentMenu->x-68, currentMenu->y-7, 15, 15);
	//M_DrawCenteredMenu();

	INT32 xoffs, yoffs;

	for (i = 0; i < currentMenu->numitems; i++)
	{
		xoffs = yoffs = 0;
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

				xoffs = icon->leftoffset;
				yoffs = icon->topoffset;

				if (i != itemOn)
					inactivemap = R_GetTranslationColormap(players[ply].skin, players[ply].skincolor, GTC_MENUCACHE);
			}
			else if (currentMenu->menuitems[i].patch && W_CheckNumForName(currentMenu->menuitems[i].patch) != LUMPERROR)
				icon = W_CachePatchName(currentMenu->menuitems[i].patch, PU_CACHE);
			else
				icon = W_CachePatchName("PLAYRANK", PU_CACHE); // temp
		}
		else if (currentMenu->menuitems[i].status & IT_HIDDEN)
			continue;
		else if (currentMenu->menuitems[i].patch && W_CheckNumForName(currentMenu->menuitems[i].patch) != LUMPERROR)
			icon = W_CachePatchName(currentMenu->menuitems[i].patch, PU_CACHE);
		else
			icon = W_CachePatchName("PLAYRANK", PU_CACHE); // temp

		if ((i == M_GetMenuIndex(MN_PLAYBACK, "FASTFORWARD") && cv_playbackspeed.value > 1) || (i == M_GetMenuIndex(MN_PLAYBACK, "REWIND") && demo.rewinding))
			V_DrawMappedPatch(currentMenu->x + currentMenu->menuitems[i].x + xoffs, currentMenu->y + yoffs, transmap|V_SNAPTOTOP, icon, R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_JAWZ, GTC_MENUCACHE));
		else
			V_DrawMappedPatch(currentMenu->x + currentMenu->menuitems[i].x + xoffs, currentMenu->y + yoffs, transmap|V_SNAPTOTOP, icon, (i == itemOn) ? activemap : inactivemap);

		if (i == itemOn)
		{
			V_DrawCharacter(currentMenu->x + currentMenu->menuitems[i].x + 4, currentMenu->y + 14,
				'\x1A' | transmap|V_SNAPTOTOP|highlightflags, false);

			V_DrawCenteredString(BASEVIDWIDTH/2, currentMenu->y + 18, transmap|V_SNAPTOTOP|V_ALLOWLOWERCASE, currentMenu->menuitems[i].text);

			if (currentMenu->menuitems[i].status & IT_ARROWS)
			{
				char *str = NULL;

				if (!(i == M_GetMenuIndex(MN_PLAYBACK, "NUMVIEWS") && r_splitscreen == 3))
					V_DrawCharacter(BASEVIDWIDTH/2 - 4, currentMenu->y + 28 - (skullAnimCounter/5),
						'\x1A' | transmap|V_SNAPTOTOP|highlightflags, false); // up arrow

				if (!(i == M_GetMenuIndex(MN_PLAYBACK, "NUMVIEWS") && r_splitscreen == 0))
					V_DrawCharacter(BASEVIDWIDTH/2 - 4, currentMenu->y + 48 + (skullAnimCounter/5),
						'\x1B' | transmap|V_SNAPTOTOP|highlightflags, false); // down arrow

				if (i == M_GetMenuIndex(MN_PLAYBACK, "NUMVIEWS"))
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

INT32 MR_PlaybackRewind(INT32 choice)
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
	return true;
}

INT32 MR_PlaybackPause(INT32 choice)
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
	return true;
}

INT32 MR_PlaybackFastForward(INT32 choice)
{
	(void)choice;

	if (demo.rewinding)
	{
		G_ConfirmRewind(leveltime);
		paused = false;
		S_ResumeAudio();
	}
	CV_SetValue(&cv_playbackspeed, cv_playbackspeed.value == 1 ? 4 : 1);
	return true;
}

INT32 MR_PlaybackAdvance(INT32 choice)
{
	(void)choice;

	paused = false;
	TryRunTics(1);
	paused = true;
	return true;
}

INT32 MR_PlaybackSetViews(INT32 choice)
{
	if (demo.freecam)
		return false; // not here.

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
	return true;
}

INT32 MR_PlaybackAdjustView(INT32 choice)
{
	INT16 splitnum = M_IsItemOn(MN_PLAYBACK, "VIEW1") ? 1 :
	                 M_IsItemOn(MN_PLAYBACK, "VIEW2") ? 2 :
	                 M_IsItemOn(MN_PLAYBACK, "VIEW3") ? 3 :
	                 M_IsItemOn(MN_PLAYBACK, "VIEW4") ? 4 : -1;

	if (splitnum < 1 || splitnum > 4)
		return false;

	G_AdjustView(splitnum, (choice > 0) ? 1 : -1, true);
	return true;
}

// this one's rather tricky
INT32 MR_PlaybackToggleFreecam(INT32 choice)
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
	return true;
}

INT32 MR_PlaybackQuit(INT32 choice)
{
	(void)choice;
	UINT8 wasmodeattacking = modeattacking;
	G_StopDemo();

	if (demo.inreplayhut)
	{
		// Setup menu stack so we can back out of the menu as per usual.
		M_RestoreMenuStack(false);
		modeattacking = ATTACKING_MENU;
	}
	else if (wasmodeattacking)
		MR_ModeAttackEndGame(0);
	else
	{
		M_ClearMenus(true);
		D_ResetGameState();
		M_StartControlPanel();
	}
	return true;
}

INT32 MR_ChangeLevel(INT32 choice)
{
	(void)choice;
	M_ClearMenus(true);
	COM_BufAddText(va("map %d -gametype \"%s\"\n", cv_nextmap.value, cv_newgametype.string));
	return true;
}

INT32 MR_ConfirmSpectate(INT32 choice)
{
	(void)choice;
	// We allow switching to spectator even if team changing is not allowed
	M_ClearMenus(true);
	COM_ImmedExecute("changeteam spectator");
	return true;
}

INT32 MR_ConfirmEnterGame(INT32 choice)
{
	(void)choice;
	if (!cv_allowteamchange.value)
	{
		M_StartMessage(M_GetText("The server is not allowing\nteam changes at this time.\nPress a key.\n"), NULL, MM_NOTHING);
		return false;
	}
	M_ClearMenus(true);
	COM_ImmedExecute("changeteam playing");
	return true;
}

INT32 MR_ConfirmTeamScramble(INT32 choice)
{
	(void)choice;
	M_ClearMenus(true);

	COM_ImmedExecute(va("teamscramble %d", cv_dummyscramble.value+1));
	return true;
}

INT32 MR_ConfirmTeamChange(INT32 choice)
{
	(void)choice;

	if (cv_dummymenuplayer.value > splitscreen+1)
		return false;

	if (!cv_allowteamchange.value && cv_dummyteam.value)
	{
		M_StartMessage(M_GetText("The server is not allowing\nteam changes at this time.\nPress a key.\n"), NULL, MM_NOTHING);
		return false;
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

	return true;
}

INT32 MR_ConfirmSpectateChange(INT32 choice)
{
	(void)choice;

	if (cv_dummymenuplayer.value > splitscreen+1)
		return false;

	if (!cv_allowteamchange.value && cv_dummyspectate.value)
	{
		M_StartMessage(M_GetText("The server is not allowing\nteam changes at this time.\nPress a key.\n"), NULL, MM_NOTHING);
		return false;
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

	return true;
}

INT32 MR_Options(INT32 choice)
{
	(void)choice;

	boolean gamecheck = (Playing() || demo.playback);

	// if the player is not admin or server, disable gameplay & server options
	M_SetItemDisabled(MN_OP_MAIN, "GAMEPLAY", Playing() && !(server || IsPlayerAdmin(consoleplayer)));
	M_SetItemDisabled(MN_OP_MAIN, "SERVER", Playing() && !(server || IsPlayerAdmin(consoleplayer)));

	// no credits or data erasing in-game/during replays
	M_SetItemDisabled(MN_OP_MAIN, "KARTCREDITS", gamecheck);
	M_SetItemDisabled(MN_OP_MAIN, "EXTRAREDITS", gamecheck);
	//M_SetItemDisabled(MN_OP_MAIN, "BLANCREDITS", gamecheck);
	//M_SetItemDisabled(MN_OP_MAIN, "SECRETCREDS", gamecheck);

	M_SetItemDisabled(MN_OP_DATA, "ERASE", gamecheck);

	M_SetItemSecret(MN_OP_GAME, "ENCORE", !M_SecretUnlocked(SECRET_ENCORE));

	M_SetItemDisabled(MN_OP_MAIN, "CUSTOM", !menudefs[MN_OP_CUSTOM].numitems);

#ifdef HAVE_DISCORDRPC
	M_SetItemVisible(MN_OP_DATA, "DISCORD", true);
#else
	M_SetItemVisible(MN_OP_DATA, "DISCORD", false);
#endif

	return true;
}

static void M_RetryResponse(INT32 ch)
{
	if (ch != 'y' && ch != KEY_ENTER)
		return;

	if (netgame) // Should never happen!
		return;

	M_ClearMenus(true);
	G_SetRetryFlag();
}

INT32 MR_Retry(INT32 choice)
{
	(void)choice;
	M_StartMessage(va("Start this %s over?\n\n(Press 'Y' to confirm)\n", (gametypes[gametype]->rules & GTR_CIRCUIT) ? "race" : "battle"),M_RetryResponse,MM_YESNO);
	return true;
}

INT32 MR_SelectableClearMenus(INT32 choice)
{
	(void)choice;
	M_ClearMenus(true);
	return true;
}

INT32 MR_GoBack(INT32 choice)
{
	(void)choice;
	M_ExitMenu();
	return true;
}

void M_RefreshPauseMenu(void)
{
#ifdef HAVE_DISCORDRPC
	M_SetItemDisabled(MN_MPAUSE, "DISCORDREQUESTS", discordRequestList == NULL);
#endif
}

// ========
// SKY ROOM
// ========

UINT8 skyRoomMenuTranslations[MAXUNLOCKABLES] = {};

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
void MD_DrawChecklist(void)
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

		V_DrawString(8, (line*8), (unlockables[i].unlocked ? recommendedflags : warningflags)|MENUCAPS, (secret ? secretname : unlockables[i].name));

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

INT32 MR_PlaySound(INT32 arg)
{
	S_StopSounds();
	S_StartSound(NULL, arg);
	return true;
}

static musicdef_t *curplaying = NULL;
static INT32 st_sel = 0;
static tic_t st_time = 0;
static size_t st_namescroll = 0;
static size_t st_namescrollstate = 0;

INT32 MR_MusicTest(INT32 choice)
{
	//INT32 ul = skyRoomMenuTranslations[choice-1];
	//UINT8 i;
	//char buf[8];
	(void)choice;

	if (!S_PrepareSoundTest())
	{
		M_StartMessage(M_GetText("No selectable tracks found.\n"),NULL,MM_NOTHING);
		return false;
	}

	curplaying = NULL;
	st_time = 0;
	st_sel = 0;
	return true;
}

void MD_DrawMusicTest(void)
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

	y = (BASEVIDWIDTH-(vid.scaledwidth))/2;

	V_DrawFill(y-1, 20, vid.scaledwidth+1, 24, 159);
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
				V_DrawString(vid.dup, vid.height - 10*vid.dup, V_NOSCALESTART|V_ALLOWLOWERCASE, va("%.6s", &curplaying->name[0][0]));
			else {
				V_DrawSmallString(vid.dup, vid.height - 5*vid.dup, V_NOSCALESTART|V_ALLOWLOWERCASE, va("%.6s - %.255s\n", &curplaying->name[0][0], curplaying->usage));
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

INT32 MR_HandleMusicTest(INT32 choice)
{
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
			return false;
	}
	return true;
}

INT32 MR_QuitMusicTest(INT32 choice)
{
	(void)choice;
	Z_Free(soundtestdefs);
	soundtestdefs = NULL;
	st_namescroll = 0;
	st_namescrollstate = 0;
	return true;
}

// ==================
// NEW GAME FUNCTIONS
// ==================

INT32 MR_Credits(INT32 choice)
{
	(void)choice;
	cursaveslot = -2;
	M_ClearMenus(true);
	F_StartCredits();
	return true;
}

INT32 MR_BlanCredits(INT32 choice)
{
	(void)choice;
	cursaveslot = -2;
	M_ClearMenus(true);
	F_BlanStartCredits();
	return true;
}

INT32 MR_SecretCredits(INT32 choice)
{
	// Reset ticks here before anything else.
	dscreditsticks = 0;

	(void)choice;
	cursaveslot = -2;
	M_ClearMenus(true);
	F_StartSecretCredits();
	return true;
}

// ==================
// SINGLE PLAYER MENU
// ==================

INT32 MR_SinglePlayerMenu(INT32 choice)
{
	(void)choice;
	M_SetItemSecret(MN_SP_MAIN, "GRANDPRIX", false);
	M_SetItemSecret(MN_SP_MAIN, "TIMEATTACK", !M_SecretUnlocked(SECRET_TIMEATTACK));
	M_SetItemSecret(MN_SP_MAIN, "ITEMBREAKER", !M_SecretUnlocked(SECRET_ITEMBREAKER));
	return true;
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
	menudrawer_f *drawer;
} statpage_t;

static statpage_t statsPages[] = {
	{ "Play Time Statistics", M_DrawStatsPlaytime, },
	{ "Level Statistics", M_DrawStatsMaps, },
	{ "Extra Statistics", M_DrawStatsExtra, },
};

#define LENSTATSPAGES (sizeof(statsPages)/sizeof(statsPages[0]))
#define NUMSTATSPAGES (kartstats.vanilla ? 2 : LENSTATSPAGES)

INT32 MR_Statistics(INT32 choice)
{
	INT16 i, j = 0;

	(void)choice;

	if (!statsMapList || nummapheaders != statsMapListLen)
	{
		statsMapList = Z_Realloc(statsMapList, nummapheaders * sizeof(*statsMapList), PU_STATIC, NULL);
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

	return true;
}

static void M_DrawStatsMaps(void)
{
	char beststr[40];
	tic_t besttime = 0;
	INT32 mapsfinished = 0;

	int location = statsLocation;
	INT32 y = 62, i = -1;
	INT16 mnum;
	extraemblem_t *exemblem;
	boolean dotopname = true, dobottomarrow = (location < statsMax);

	const char *realname = G_GetRecordPresetName(currentrecordpreset);
	V_DrawThinString(0, 0, V_ALLOWLOWERCASE, realname ? realname : currentrecordpreset); // temporary
	V_DrawThinString(0, 8, MENUCAPS|V_GRAYMAP|V_6WIDTHSPACE, "(BKSP to change)");

	for (INT32 j = 0; j < nummapheaders; j++)
	{
		if (!mapheaderinfo[j] || (mapheaderinfo[j]->menuflags & LF2_NOTIMEATTACK))
			continue;

		maprecord_t *record = G_GetMapRecord(G_BuildMapName(j+1));
		if (record == NULL)
			continue;

		maprecordpreset_t *preset = G_GetMapRecordPreset(record, currentrecordpreset);
		if (preset == NULL)
			continue;

		besttime += preset->besttime;
		mapsfinished++;
	}

	V_DrawString(20, 42, MENUCAPS|highlightflags, "Combined time records:");

	sprintf(beststr, "%dh %dm %ds", G_TicsToHours(besttime), G_TicsToMinutes(besttime, false), G_TicsToSeconds(besttime));
	V_DrawRightAlignedString(BASEVIDWIDTH-16, 42, MENUCAPS|(mapsfinished != nummapheaders ? warningflags : 0), beststr);

	if (mapsfinished != nummapheaders)
		V_DrawRightAlignedString(BASEVIDWIDTH-16, 50, MENUCAPS|warningflags, va("(%d unfinished)", nummapheaders - mapsfinished));
	else
		V_DrawRightAlignedString(BASEVIDWIDTH-16, 50, MENUCAPS|recommendedflags, "(complete)");

	if (G_EmblemsEnabled())
	{
		V_DrawString(32, 50, 0, va("x %d/%d", M_CountEmblems(), numemblems+numextraemblems));
		V_DrawSmallScaledPatch(20, 50, 0, W_CachePatchName("GOTITA", PU_STATIC));
	}

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
			V_DrawString(20,  y, MENUCAPS|highlightflags, "Level Name");
			V_DrawString(160, y, MENUCAPS|highlightflags, "Best");
			V_DrawString(224, y, MENUCAPS|highlightflags, "Play");
			//V_DrawString(256, y, MENUCAPS|highlightflags, "Medals");
			y += 8;
			dotopname = false;
		}

		mnum = statsMapList[i];
		if (G_EmblemsEnabled())
			M_DrawMapEmblems(mnum, 295, y);

		maprecord_t *record = G_GetMapRecord(G_BuildMapName(mnum+1));
		maprecordpreset_t *preset = record ? G_GetMapRecordPreset(record, currentrecordpreset) : NULL;
		CLEANUP(Z_Pfree) char *maptitle = G_BuildMapTitle(mnum+1);

		V_DrawThinString(20, y, MENUCAPS, maptitle);
		V_DrawThinString(160, y, MENUCAPS, preset == NULL || preset->besttime == UINT32_MAX ? "--'--\"--" :
			va("%02d'%02d\"%02d",
			G_TicsToMinutes(preset->besttime, true),
			G_TicsToSeconds(preset->besttime),
			G_TicsToCentiseconds(preset->besttime)));
		V_DrawThinString(224, y, MENUCAPS, preset == NULL || preset->playtime == 0 ? "" :
			va("%d:%02d:%02d",
			G_TicsToHours(preset->playtime),
			G_TicsToMinutes(preset->playtime, false),
			G_TicsToSeconds(preset->playtime)));

		y += 8;

		if (y >= BASEVIDHEIGHT-8)
			goto bottomarrow;
	}
	if (dotopname && !location)
	{
		V_DrawString(20,  y, MENUCAPS|highlightflags, "Level Name");
		V_DrawString(256, y, MENUCAPS|highlightflags, "Medals");
		y += 8;
	}
	else if (location)
		--location;

	// Extra Emblems
	for (i = -2; i < numextraemblems; ++i)
	{
		if (i == -1)
		{
			V_DrawString(20, y, MENUCAPS|highlightflags, "Extra Medals");
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

			V_DrawString(20, y, MENUCAPS, va("%s", exemblem->description));
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
		V_DrawString(20, (y), MENUCAPS|highlightflags, title); \
		tic_t timeval = kartstats.field; \
		snprintf(timebuf, 80, "%02i:%02i:%02i", G_TicsToHours(timeval), G_TicsToMinutes(timeval, false), G_TicsToSeconds(timeval)); \
		V_DrawRightAlignedString(BASEVIDWIDTH-16, (y), MENUCAPS, timebuf); \
	}

#define DRAWAMOUNTSTAT(y, title, field) { \
		V_DrawString(20, (y), highlightflags, title); \
		unsigned amountval = kartstats.field; \
		V_DrawRightAlignedString(BASEVIDWIDTH-16, (y), MENUCAPS, va("%u", amountval)); \
	}

static void M_DrawStatsPlaytime(void)
{
	V_DrawString(20, 42, MENUCAPS|highlightflags, "Total Play Time:");
	V_DrawCenteredString(BASEVIDWIDTH/2, 52, MENUCAPS, va("%i hours, %i minutes, %i seconds",
	                         G_TicsToHours(kartstats.totalplaytime),
	                         G_TicsToMinutes(kartstats.totalplaytime, false),
	                         G_TicsToSeconds(kartstats.totalplaytime)));
	V_DrawString(20, 62, MENUCAPS|highlightflags, "Total Matches:");
	V_DrawRightAlignedString(BASEVIDWIDTH-16, 62, MENUCAPS, va("%i played", kartstats.matchesplayed));

	V_DrawString(20, 82, MENUCAPS|highlightflags, "Online Power Level:");
	V_DrawRightAlignedString(BASEVIDWIDTH-16, 92, MENUCAPS, va("Race: %i", vspowerlevel[PWRLV_RACE]));
	V_DrawRightAlignedString(BASEVIDWIDTH-16, 102, MENUCAPS, va("Battle: %i", vspowerlevel[PWRLV_BATTLE]));

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

void MD_DrawLevelStats(void)
{
	M_DrawMenuTitle();

	V_DrawCenteredString(BASEVIDWIDTH/2, 28, MENUCAPS|highlightflags, statsPages[statsCurrentPage].title);

	INT32 w = V_StringWidth(statsPages[statsCurrentPage].title, highlightflags);
	V_DrawCharacter(BASEVIDWIDTH/2 - w/2 - 10 - (skullAnimCounter/5), 28,
			'\x1C' | highlightflags, false); // left arrow
	V_DrawCharacter(BASEVIDWIDTH/2 + w/2 + 2 + (skullAnimCounter/5), 28,
			'\x1D' | highlightflags, false); // right arrow

	statsPages[statsCurrentPage].drawer();
}

// Handle statistics.
INT32 MR_HandleLevelStats(INT32 choice)
{
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

		case KEY_BACKSPACE:
			if (statsCurrentPage != 1)
				break;
			S_StartSound(NULL, sfx_menu1);
			recordpresetnum_e i, j;
			for (i = 0; i < numrecordpresets; i++)
				if (fastcmp(currentrecordpreset, recordpresets[i]->name))
					break;
			for (j = i; (i = (i + 1) % numrecordpresets) != j;)
				if (recordpresets[i]->numversions)
				{
					G_SetCurrentRecordPreset(recordpresets[i]);
					break;
				}
			break;

		default:
			return false;
	}
	return true;
}

INT32 MR_GrandPrixTemp(INT32 choice)
{
	(void)choice;
	if (!M_CheckNextCup(menustack[0]))
	{
		M_StartMessage(M_GetText("No cups found for Grand Prix.\n"),NULL,MM_NOTHING);
		return false;
	}
	M_PatchSkinNameTable();
	return true;
}

// Start Grand Prix!
INT32 MR_StartGrandPrix(INT32 choice)
{
	cupheader_t *gpcup;

	(void)choice;

	M_RestoreMenuStack(true);
	M_ClearMenusEx(true, true);

	memset(&grandprixinfo, 0, sizeof(struct grandprixinfo));

	switch (cv_dummygpdifficulty.value)
	{
		case KARTSPEED_EASY:
		case KARTSPEED_NORMAL:
		case KARTSPEED_HARD:
		case KARTSPEED_EXPERT:
			grandprixinfo.gamespeed = cv_dummygpdifficulty.value;
			break;
		case KARTGP_MASTER:
			grandprixinfo.gamespeed = KARTSPEED_HARD;
			grandprixinfo.masterbots = true;
			break;
		case KARTGP_NIGHTMARE:
			grandprixinfo.gamespeed = KARTSPEED_EXPERT;
			grandprixinfo.masterbots = true;
			break;
		case KARTGP_LUNATIC:
			grandprixinfo.gamespeed = KARTSPEED_HARD;
			grandprixinfo.lunaticmode = true;
			grandprixinfo.masterbots = true;
			break;
		case KARTGP_MANIAC:
			grandprixinfo.gamespeed = KARTSPEED_EXPERT;
			grandprixinfo.lunaticmode = true;
			grandprixinfo.masterbots = true;
			break;
		default:
			CONS_Alert(CONS_WARNING, "Invalid GP difficulty\n");
			grandprixinfo.gamespeed = KARTSPEED_DEFAULT;
			break;
	}

	for (gpcup = kartcupheaders; gpcup != NULL; gpcup = gpcup->next)
	{
		if (gpcup->id == cv_nextcup.value-1)
			break;
	}

	I_Assert(gpcup != NULL);

	grandprixinfo.gp = true;
	grandprixinfo.wonround = false;
	grandprixinfo.initalize = true;
	grandprixinfo.cup = gpcup;

	// Populate the roundqueue
	memset(&roundqueue, 0, sizeof(struct roundqueue));
	G_GPCupIntoRoundQueue(gpcup, GT_RACE/*levellist.newgametype*/, (boolean)cv_dummygpencore.value);
	roundqueue.position = roundqueue.roundnum = 1;
	roundqueue.netcommunicate = true; // relevant for future Online GP

	multiplayer = true;

	D_MapChange(
		roundqueue.entries[0].mapnum + 1,
		roundqueue.entries[0].gametype,
		roundqueue.entries[0].encore,
		true,
		1,
		false,
		roundqueue.entries[0].rankrestricted
	);

	return true;
}

// ===========
//  MAIN MENU
// ===========

// ===========
// MODE ATTACK
// ===========

boolean M_AttackOnStack(void)
{
	UINT16 i;

	for (i = 0; i < sizeof(menustack)/sizeof(menustack[0]); i++)
	{
		if (menustack[i] == MN_SP_TIMEATTACK)
			return true;
		// shhh this isnt time attack but it also uses modeattacking menu...
		if (menustack[i] == MN_MISC_REPLAYHUT)
			return true;
	}

	return false;
}

// Drawing function for Time Attack
void MD_DrawTimeAttackMenu(void)
{
	maprecordpreset_t *preset = NULL;
	maprecord_t *record = G_GetMapRecord(G_BuildMapName(cv_nextmap.value));
	if (record != NULL)
		preset = G_GetMapRecordPreset(record, currentrecordpreset);

	// Character face!
	{
		UINT8 *colormap = R_GetTranslationColormap(cv_chooseskin.value, cv_playercolor[0].value, GTC_MENUCACHE);
		V_DrawMappedPatch(BASEVIDWIDTH - menudefs[MN_SP_TIMEATTACK].x - SHORT(faceprefix[cv_chooseskin.value][FACE_WANTED]->width), menudefs[MN_SP_TIMEATTACK].y, 0, faceprefix[cv_chooseskin.value][FACE_WANTED], colormap);
	}

	MD_DrawGenericMenu();

	// Level record list
	if (cv_nextmap.value)
	{
		INT32 dupadjust = (vid.scaledwidth);
		V_DrawFill((BASEVIDWIDTH - dupadjust)>>1, 78, dupadjust, 36, 159);

		if (levellistmode != LLM_ITEMBREAKER)
		{
			V_DrawRightAlignedString(149, 80, MENUCAPS|highlightflags, "Best Lap:");
			K_drawKartTimestamp(preset ? preset->bestlap : UINT32_MAX, 19, 86, -1, 2);
		}
		V_DrawRightAlignedString(292, 80, MENUCAPS|highlightflags, "Best Time:");
		K_drawKartTimestamp(preset ? preset->besttime : UINT32_MAX, 162, 86, cv_nextmap.value-1, 1);
	}

	// Draw current RA preset mode.
	const char *realname = G_GetRecordPresetName(currentrecordpreset);
	V_DrawString(50, 104, MENUCAPS|V_6WIDTHSPACE, realname ? realname : "Unknown mode");

	// ALWAYS DRAW player name, level name, skin and color even when not on this menu!
	if (menustack[0] == MN_SP_TIMEATTACK)
		return;

	menu_t *menu = &menudefs[MN_SP_TIMEATTACK];
	for (INT32 i = 0; i < menu->numitems; i++)
	{
		INT32 x = menu->x + menu->menuitems[i].x;
		INT32 y = menu->y + M_GetItemAbsY(menu, i);
		if (y < 128)
			M_DrawMenuItem(&menu->menuitems[i], x, y, V_TRANSLUCENT|MENUCAPS, false);
	}
}

// Going to Time Attack menu...
INT32 MR_TimeAttack(INT32 arg)
{
	if (arg >= LLM__MAX)
		return false;

	if (arg >= 0)
	{
		M_SetItemArgument(menustack[0], "LEVEL", arg); // Don't be dependent on cv_newgametype

		if (!M_CheckNextMap(menustack[0]))
		{
			M_StartMessage(M_GetText(va("No levels found for %s.\n", arg == LLM_ITEMBREAKER ? "Item Breaker" : "Time Attack")), NULL, MM_NOTHING);
			return false;
		}
	}

	M_PatchSkinNameTable();
	//M_ClearMenus(true);

	modeattacking = ATTACKING_MENU;
	titlemapinaction = false; // Nope don't give us HOMs please

	M_SetItemOn(MN_SP_TIMEATTACK, "START"); // "Start" is selected.

	return true;
}

INT32 MR_QuitTimeAttackMenu(INT32 choice)
{
	(void)choice;

	// you know what? always putting these in the buffer won't hurt anything.
	COM_BufAddText(va("skin \"%s\"\n", skins[cv_chooseskin.value].name));
	if (!M_AttackOnStack())
	{
		modeattacking = ATTACKING_NONE;
	}
	return true;
}

// Player has selected the "START" from the time attack screen
INT32 MR_ChooseTimeAttack(INT32 choice)
{
	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(true, levellistmode == LLM_ITEMBREAKER);
	CLEANUP(pfree) char *demopath = G_GetRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, REPLAY_LAST);

	emeralds = 0;
	if (choice != 1)
		M_RestoreMenuStack(true);
	M_ClearMenusEx(true, true);
	G_SetPresetCvars(G_GetRecordPresetVersion(currentrecordpreset, currentrecordpresetversion));
	modeattacking = (levellistmode == LLM_ITEMBREAKER ? ATTACKING_ITEMBREAK : ATTACKING_TIME);
	M_MkdirEach(gpath, M_PathParts(gpath) - 4, 0755);

	if (!cv_autorecord.value)
		remove(demopath);
	else
		G_RecordDemo(demopath);

	G_DeferedInitNew(false, cv_nextmap.value, cv_chooseskin.value, 0, false);
	return true;
}

INT32 MR_ReplayStaff(INT32 choice)
{
	(void)choice;
	lumpnum_t l = W_CheckNumForName(va("%sS%02u",G_BuildMapName(cv_nextmap.value),cv_dummystaff.value));
	if (l == LUMPERROR)
		return false;

	M_RestoreMenuStack(true);
	COM_BufAddText(va("playdemo %sS%02u -force", G_BuildMapName(cv_nextmap.value), cv_dummystaff.value));
	return true;
}

INT32 MR_TimeAttackPreset(INT32 arg)
{
	if (arg < 0 || arg >= numrecordpresets)
		return false;

	recordpreset_t *new = recordpresets[arg];
	if (new->numversions == 0)
		return false;

	G_SetCurrentRecordPreset(new);
	return true;
}

// Player has selected the "REPLAY" from the time attack screen
INT32 MR_ReplayTimeAttack(INT32 arg)
{
	if (arg < 0 || arg >= REPLAY__MAX)
		return false;

	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(false, levellistmode == LLM_ITEMBREAKER);
	CLEANUP(pfree) char *demopath = G_GetRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, arg);

	M_RestoreMenuStack(true);
	COM_BufAddText(va("playdemo \"%s\" -force", demopath));
	return true;
}

static void M_EraseGuest(INT32 choice)
{
	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(true, levellistmode == LLM_ITEMBREAKER);
	CLEANUP(pfree) char *rguest = G_GetRecordReplay(gpath, cv_nextmap.value, UINT16_MAX, REPLAY_GUEST);
	(void)choice;
	if (FIL_FileExists(rguest))
		remove(rguest);
	M_ExitMenu();
	M_StartMessage(M_GetText("Guest replay data erased.\n"),NULL,MM_NOTHING);
}

static void M_OverwriteGuest(recordreplay_e which)
{
	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(true, levellistmode == LLM_ITEMBREAKER);
	CLEANUP(pfree) char *rguest = G_GetRecordReplay(gpath, cv_nextmap.value, UINT16_MAX, REPLAY_GUEST);
	CLEANUP(pfree) char *overwrite = G_GetRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, which);
	UINT8 *buf;
	size_t len = FIL_ReadFile(overwrite, &buf);
	if (!len) {
		return;
	}
	if (FIL_FileExists(rguest)) {
		messagebox.active = false;
		remove(rguest);
	}
	FIL_WriteFile(rguest, buf, len);
	M_ExitMenu();
	M_StartMessage(M_GetText("Guest replay data saved.\n"),NULL,MM_NOTHING);
}

static void M_OverwriteGuest_Time(INT32 choice)
{
	(void)choice;
	M_OverwriteGuest(REPLAY_BESTTIME);
}

static void M_OverwriteGuest_Lap(INT32 choice)
{
	(void)choice;
	M_OverwriteGuest(REPLAY_BESTLAP);
}

static void M_OverwriteGuest_Last(INT32 choice)
{
	(void)choice;
	M_OverwriteGuest(REPLAY_LAST);
}

INT32 MR_SetGuestReplay(INT32 arg)
{
	void (*which)(INT32);
	switch(arg)
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
		return true;
	}
	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(true, levellistmode == LLM_ITEMBREAKER);
	if (G_CheckRecordReplay(gpath, cv_nextmap.value, UINT16_MAX, REPLAY_GUEST))
		M_StartMessage(M_GetText("Are you sure you want to\noverwrite the guest replay data?\n\n(Press 'Y' to confirm)\n"), which, MM_YESNO);
	else
		which(0);
	return true;
}

INT32 MR_ModeAttackRetry(INT32 choice)
{
	(void)choice;
	G_CheckDemoStatus(); // Cancel recording
	if (!modeattacking)
		return false;

	MR_ChooseTimeAttack(1);
	return true;
}

INT32 MR_ModeAttackEndGame(INT32 choice)
{
	(void)choice;
	G_CheckDemoStatus(); // Cancel recording

	if (gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_VOTING)
		Command_ExitGame_f();

	// Setup menu stack so we can back out of the menu as per usual.
	M_RestoreMenuStack(false);
	modeattacking = ATTACKING_MENU;

	return true;
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

INT32 MR_EndGame(INT32 choice)
{
	(void)choice;
	if (demo.playback)
		return false;

	if (!Playing())
		return false;

	M_StartMessage(M_GetText("Are you sure you want to end the game?\n\n(Press 'Y' to confirm)\n"), M_ExitGameResponse, MM_YESNO);
	return true;
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

INT32 MR_Connect(INT32 arg)
{
	// do not call menuexitfunc
	M_ClearMenus(false);

	CV_Set(&cv_lastserver, I_GetNodeAddress(serverlist[arg + cv_dummyserverpage.value * M_ServersPerPage()].node));

	COM_BufAddText(va("connect node %d\n", serverlist[arg + cv_dummyserverpage.value * M_ServersPerPage()].node));
	return true;
}

INT32 MR_Refresh(INT32 choice)
{
	(void)choice;

	I_OsPolling();
	I_UpdateNoBlit();
	if (rendermode == render_soft)
		I_FinishUpdate(); // page flip or blit buffer

	// first page of servers
	CV_SetValue(&cv_dummyserverpage, 0);

	CL_UpdateServerList();

#ifdef MASTERSERVER
#ifdef HAVE_THREADS
	Spawn_masterserver_thread("fetch-servers", Fetch_servers_thread);
#else/*HAVE_THREADS*/
	Fetch_servers_thread(NULL);
#endif/*HAVE_THREADS*/
#endif/*MASTERSERVER*/

	return true;
}

static void MD_DrawServerCountAndHorizontalBar(void)
{
	const char *text;
	INT32 radius;
	INT32 center = BASEVIDWIDTH/2;

	switch (M_GetWaitingMode())
	{
		case M_WAITING_VERSION:
			text = "Checking for updates";
			break;

		case M_WAITING_SERVERS:
			text = "Loading server list";
			break;

		default:
			if (serverlistultimatecount > serverlistcount)
			{
				text = va("%d/%d servers found%.*s",
						serverlistcount,
						serverlistultimatecount,
						I_GetTime() / NEWTICRATE % 4, "...");
			}
			else if (serverlistcount > 0)
			{
				text = va("%d server%s found", serverlistcount, serverlistcount > 1 ? "s" : "");
			}
			else
			{
				text = "No servers found";
			}
	}

	radius = V_StringWidth(text, 0) / 2;

	V_DrawCenteredString(center, currentMenu->y+28, 0, text);

	// Horizontal line!
	V_DrawFill(1, currentMenu->y+32, center - radius - 2, 1, 0);
	V_DrawFill(center + radius + 2, currentMenu->y+32, BASEVIDWIDTH - 1, 1, 0);
}

static void M_DrawServerLines(INT32 x, INT32 page)
{
	UINT16 i;
	char servergametype[30];
	char pwr[13];
	INT16 firstserverline = M_GetMenuIndex(MN_MP_CONNECT, "LINE1");
	UINT32 serversperpage = M_ServersPerPage(); // server sperpage?
	menu_t *conmenu = &menudefs[MN_MP_CONNECT]; // meh, whatever

	for (i = 0; i < min(serverlistcount - page * serversperpage, serversperpage); i++)
	{
		INT32 slindex = i + page * serversperpage;
		UINT32 globalflags = ((serverlist[slindex].info.numberofplayer >= serverlist[slindex].info.maxplayer) ? V_TRANSLUCENT : 0)
		|((itemOn == firstserverline+i) ? highlightflags : 0)|V_ALLOWLOWERCASE;

		// min width is probably like 268px (sorry for shitty formatting)
		V_DrawFill(x - 3,
				   S_LINEY(i) - (i == 0 ? 3 : 0),
				   268 + 6, (i == 0 ? 15 : 12),
				   (itemOn == firstserverline+i) ? 153 : ((i & 1) ? 159 : 156)
		);

		V_DrawString(x, S_LINEY(i), globalflags, serverlist[slindex].info.servername);

		V_DrawSmallString(x, S_LINEY(i)+8, globalflags,
						  va("Ping: %u", (UINT32)LONG(serverlist[slindex].info.time)));

		V_DrawSmallString(x+44,S_LINEY(i)+8, globalflags,
						  va("Players: %02d/%02d", serverlist[slindex].info.numberofplayer, serverlist[slindex].info.maxplayer));

		sprintf(servergametype, "%s (%s)", serverlist[slindex].info.gametypename, kartspeed_cons_t[(serverlist[slindex].info.kartvars & SV_SPEEDMASK)+1].strvalue);
		V_DrawSmallString(x+108, S_LINEY(i)+8, globalflags, servergametype);

		if (serverlist[slindex].info.avgpwrlv == -1)
			sprintf(pwr, "PWR.LV: Off");
		else if (serverlist[slindex].info.avgpwrlv > 0)
			sprintf(pwr, "PWR.LV: %04d", serverlist[slindex].info.avgpwrlv);
		else
			sprintf(pwr, "PWR.LV: ----");
		V_DrawSmallString(x+181, S_LINEY(i)+8, globalflags, pwr);

		// Don't use color flags intentionally, the global yellow color will auto override the text color code
		if (serverlist[slindex].info.modifiedgame)
			V_DrawSmallString(x+228, S_LINEY(i)+8, globalflags, "\x85" "Mod");
		if (serverlist[slindex].info.cheatsenabled)
			V_DrawSmallString(x+244, S_LINEY(i)+8, globalflags, "\x83" "Cheats");

		conmenu->menuitems[i+firstserverline].status &= ~IT_HIDDEN;
	}
}

//static float serverlistslidex;
//INT32 oldserverlistpage;

void MD_DrawConnectMenu(void)
{
	UINT16 i;
	INT16 firstserverline = M_GetMenuIndex(MN_MP_CONNECT, "LINE1");
	UINT32 serversperpage = M_ServersPerPage(); // server sperpage?
	INT32 mservflags = V_ALLOWLOWERCASE;
	menu_t *conmenu = &menudefs[MN_MP_CONNECT]; // meh, whatever

	for (i = firstserverline; i < firstserverline+serversperpage; i++)
		conmenu->menuitems[i].status |= IT_HIDDEN;

	// Did you change the Server Browser address? Have a little reminder.
	if (CV_IsSetToDefault(&cv_masterserver))
		mservflags = mservflags|highlightflags|V_30TRANS;
	else
		mservflags = mservflags|warningflags;
	V_DrawRightAlignedSmallString(BASEVIDWIDTH - currentMenu->x, currentMenu->y+3 + M_GetMenuItem(MN_MP_CONNECT, "REFRESH")->y,
								  mservflags, va("MS: %s", cv_masterserver.string));

	// When switching pages, slide the old page and the
	// new page across the screen
	/*if (oldserverlistpage != cv_dummyserverpage.value)
	{
		const float ease = serverlistslidex / 2.f;
		const INT32 offx = serverlistslidex > 0 ? BASEVIDWIDTH : -(BASEVIDWIDTH);
		const INT32 x = (FLOAT_TO_FIXED(serverlistslidex) + ease * rendertimefrac) / FRACUNIT;

		M_DrawServerLines(currentMenu->x + x - offx, oldserverlistpage);
		M_DrawServerLines(currentMenu->x + x, cv_dummyserverpage.value);

		if (renderisnewtic)
		{
			serverlistslidex -= ease;

			if ((INT32)serverlistslidex == 0)
				oldserverlistpage = serverlistpage;
		}
	}
	else*/
	{
		M_DrawServerLines(currentMenu->x, cv_dummyserverpage.value);
	}

	MD_DrawServerCountAndHorizontalBar();

	MD_DrawGenericMenu();
}

INT32 MR_CancelConnect(INT32 choice)
{
	(void)choice;
	D_CloseConnection();
	return true;
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
	messagebox.active = false;
	// modified game check: no longer handled
	// we don't request a restart unless the filelist differs

	// first page of servers
	CV_SetValue(&cv_dummyserverpage, 0);

	CL_UpdateServerList();

	M_EnterMenu(MN_MP_CONNECT, true, 0);
	M_SetItemOn(MN_MP_CONNECT, "SORTING");

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
	MR_Refresh(0);
#endif/*defined (MASTERSERVER) && defined (HAVE_THREADS)*/
}

INT32 MR_ConnectMenuModChecks(INT32 choice)
{
	(void)choice;
	// okay never mind we want to COMMUNICATE to the player pre-emptively instead of letting them try and then get confused when it doesn't work

	if (modifiedgame)
	{
		M_StartMessage(M_GetText("You have addons loaded.\nYou won't be able to join netgames!\n\nTo play online, restart the game\nand don't load any addons.\nSRB2Kart will automatically add\neverything you need when you join.\n\n(Press a key)\n"), M_ConnectMenu, MM_EVENTHANDLER);
		return true;
	}

	M_ConnectMenu(-1);
	return true;
}

//===========================================================================
// Start Server Menu
//===========================================================================

INT32 MR_StartServer(INT32 choice)
{
	UINT8 ssplayers = cv_splitplayers.value-1;

	(void)choice;

	if (menustack[0] == MN_MP_SPLITSCREEN)
		netgame = false;
	else
		netgame = true;

	multiplayer = true;

	// Still need to reset devmode
	cht_debug = 0;

	if (demo.playback)
		G_StopDemo();
	if (metalrecording)
		G_StopMetalDemo();

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
		SV_StartSinglePlayerServer(cv_newgametype.value, false);
		D_MapChange(cv_nextmap.value, cv_newgametype.value, (cv_kartencore.value == 1), 1, 1, false, false);
	}
	else
	{
		D_MapChange(cv_nextmap.value, cv_newgametype.value, (cv_kartencore.value == 1), 1, 1, false, false);
		COM_BufAddText("dummyconsvar 1\n");
	}

	M_ClearMenus(true);
	return true;
}

// ==============
// CONNECT VIA IP
// ==============

// Draw the funky Connect IP menu. Tails 11-19-2002
// So much work for such a little thing!
void MD_DrawMPMainMenu(void)
{
	INT32 x = currentMenu->x;
	INT32 y = currentMenu->y;

	// use generic drawer for cursor, items and title
	MD_DrawGenericMenu();

	// character bar, ripped off the color bar :V
	{
#define iconwidth 32
#define spacingwidth 32
#define incrwidth (iconwidth + spacingwidth)
		UINT16 i = 0, pskin, pcol;
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

			if (M_IsItemOn(MN_MP_MAIN, "PLAYERSETUP") && i == cv_dummymultiplayer.value+1)
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
	if (cv_splitplayers.value <= cv_dummymultiplayer.value)
		CV_SetValue(&cv_dummymultiplayer, 0);

	// anything to avoid a keyhandler :^)
	dummymultiplayer_cons_t[1].value = cv_splitplayers.value - 1;
}



// Tails 11-19-2002
INT32 MR_ConnectIP(INT32 choice)
{
	(void)choice;

	if (*cv_dummyip.string == 0)
	{
		M_StartMessage("You must specify an IP address.\n", NULL, MM_NOTHING);
		return false;
	}

	M_ClearMenusEx(true, true);

	CV_Set(&cv_lastserver,cv_dummyip.string);
	COM_BufAddText(va("connect \"%s\"\n", cv_dummyip.string));

	// A little "please wait" message.
	M_DrawTextBox(56, BASEVIDHEIGHT/2-12, 24, 2);
	V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT/2, MENUCAPS, "Connecting to server...");
	I_OsPolling();
	I_UpdateNoBlit();
	if (rendermode == render_soft)
		I_FinishUpdate(); // page flip or blit buffer
	return true;
}


//Join Last server
INT32 MR_ConnectLastServer(INT32 choice)
{
	(void)choice;

	if (!*cv_lastserver.string)
	{
		M_StartMessage("You haven't previosuly joined a server.\n", NULL, MM_NOTHING);
		return false;
	}

	M_ClearMenus(true);
	COM_BufAddText(va("connect \"%s\"\n", cv_lastserver.string));
	return true;
}

// ========================
// MULTIPLAYER PLAYER SETUP
// ========================
// Tails 03-02-2002

// used for skin display on player setup menu
static fixed_t    multi_tics;
static state_t   *multi_state;

// used for follower display on player setup menu
static fixed_t follower_tics;
static UINT32 follower_frame;	// used for FF_ANIMATE garbo
static state_t *follower_state;

// this is set before entering the MultiPlayer setup menu,
// for either player 1 or 2
static UINT8 setupplayer;

#define charw 72

void MD_DrawSetupMultiPlayerMenu(void)
{
	// use generic drawer for cursor, items and title
	// bg, text, arrows handled by generic drawer
	MD_DrawGenericMenu();

	MD_DrawCssColourBar();
	MD_DrawCssCharacter();

	if (cv_skinselectstyle.value)
	{
		MD_DrawCssStatBars();
		MD_DrawGridCssSelector();
	}
	else
	{
		MD_DrawCssStatBacker();
		MD_DrawBarCssSelector();
	}
}

void MD_DrawCssStatBacker(void)
{
	INT32 mx, my;
	patch_t *statdot = W_CachePatchName("K_SDOT0", PU_CACHE);
	UINT8 speed;
	UINT8 weight;
	const UINT8 *flashcol = V_GetStringColormap(highlightflags);
	INT16 i;

	INT32 skintodisplay = cv_chooseskin.value;

	mx = menudefs[MN_MP_PLAYERSETUP].x;
	my = menudefs[MN_MP_PLAYERSETUP].y;

	// SRB2Kart: draw the stat backer
	for (i = 0; i < numskins; i++) // draw the stat dots
	{
		if (i != skintodisplay && R_SkinAvailable(skins[i].name) != -1)
		{
			speed = skins[i].kartspeed;
			weight = skins[i].kartweight;
			V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, NULL);
		}
	}

	speed = skins[skintodisplay].kartspeed;
	weight = skins[skintodisplay].kartweight;

	statdot = W_CachePatchName("K_SDOT1", PU_CACHE);
	if (skullAnimCounter < 4) // SRB2Kart: we draw this dot later so that it's not covered if there's multiple skins with the same stats
		V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, flashcol);
	else
		V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, NULL);

	statdot = W_CachePatchName("K_SDOT2", PU_CACHE); // coloured center
	V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, R_GetTranslationColormap(0, cv_dummycolor.value, GTC_MENUCACHE));
}

void MD_DrawCssStatBars(void)
{
	INT32 mx, my;

	const INT32 BITSPACING = 4;
	const INT32 BITSHIFTSUB = 1;

	const INT32 BITSTARTXMAIN = 17;
	const INT32 BITSTARTXSUB = 50;

	const INT32 BITSPEEDSTARTY = 2;
	const INT32 BITWEIGHTSTARTY = 18;

	const INT32 ICNXFIRST = 1;
	const INT32 ICNXSECOND = 57;
	const INT32 ICNYFIRST = 1;
	const INT32 ICNYSECOND = 17;

	menuitem_t *item = M_GetMenuItem(MN_MP_PLAYERSETUP, "STATBAR");

	patch_t *statbitsub = 	W_CachePatchName("STATBSPD", PU_CACHE);
	patch_t *statbitmain = 	W_CachePatchName("STATBWGT", PU_CACHE);

	patch_t *speedicn = 	W_CachePatchName("K_STSPD", PU_CACHE);
	patch_t *weighticn = 	W_CachePatchName("K_STWHT", PU_CACHE);

	UINT8 *speedcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_SAPPHIRE, GTC_MENUCACHE);
	UINT8 *weightcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_KETCHUP, GTC_MENUCACHE);
	UINT8 *speednumcolour = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_NIGHTFALL, GTC_MENUCACHE);
	UINT8 *weightnumcolour = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_AMBER, GTC_MENUCACHE);
	// UINT8 *accelcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_GOLD, GTC_MENUCACHE);
	// UINT8 *handlingcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_MINT, GTC_MENUCACHE);
	UINT8 *subcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_BLACK, GTC_MENUCACHE);

	INT32 skintodisplay = cv_chooseskin.value;
	UINT8 speed = skins[skintodisplay].kartspeed;
	UINT8 weight = skins[skintodisplay].kartweight;

	mx = item->x;
	my = item->y;

	// // Draw speed
	for (INT16 s = 0; s < speed; s++)
	{
		V_DrawFixedPatch((mx + BITSTARTXMAIN + (BITSPACING * s))<<FRACBITS, (my + BITSPEEDSTARTY)<<FRACBITS, FRACUNIT, 0, statbitmain, speedcolour);
	}
	// Draw accel
	for (INT16 a = 0; a < 9-speed; a++)
	{
		V_DrawFixedPatch((mx + BITSTARTXSUB - (BITSPACING * a))<<FRACBITS, (my + BITSPEEDSTARTY + BITSHIFTSUB)<<FRACBITS, FRACUNIT, 0, statbitsub, subcolour);
	}

	// Draw weight
	for (INT16 w = 0; w < weight; w++)
	{
		V_DrawFixedPatch((mx + BITSTARTXMAIN + (BITSPACING * w))<<FRACBITS, (my + BITWEIGHTSTARTY)<<FRACBITS, FRACUNIT, 0, statbitmain, weightcolour);
	}
	// Draw handling
	for (INT16 h = 0; h < 9-weight; h++)
	{
		V_DrawFixedPatch((mx + BITSTARTXSUB - (BITSPACING * h))<<FRACBITS, (my + BITWEIGHTSTARTY + BITSHIFTSUB)<<FRACBITS, FRACUNIT, 0, statbitsub, subcolour);
	}

	// Draw our icons
	V_DrawFixedPatch((mx + ICNXFIRST)<<FRACBITS, (my + ICNYFIRST)<<FRACBITS, FRACUNIT, 0, speedicn, NULL);
	V_DrawFixedPatch((mx + ICNXFIRST)<<FRACBITS, (my + ICNYSECOND)<<FRACBITS, FRACUNIT, 0, weighticn, NULL);

	// Draw stat numbers
	V_DrawPaddedTallColorNumAtFixed((mx + ICNXSECOND+11 + (speed == 7 ? 1 : 0))<<FRACBITS, (my + ICNYFIRST+2)<<FRACBITS, 0, speed, 1, speednumcolour);
	V_DrawPaddedTallColorNumAtFixed((mx + ICNXSECOND+11 + (weight == 7 ? 1 : 0))<<FRACBITS, (my + ICNYSECOND+2)<<FRACBITS, 0, weight, 1, weightnumcolour);
}

void MD_DrawCssColourBar(void)
{
	INT32 mx, my;
	INT16 i;

	mx = menudefs[MN_MP_PLAYERSETUP].x;
	my = menudefs[MN_MP_PLAYERSETUP].y;

	// 2.2 color bar backported with permission
#define indexwidth 8
	{
		const INT32 numcolors = (250-charw)/(2*indexwidth); // Number of colors per side
		INT32 x = mx;
		INT32 w = indexwidth; // Width of a singular color block
		menucolor_t *mc, *choosecolor = menucolorhead; // Last accessed color
		UINT8 h;

		while (choosecolor->color != cv_dummycolor.value && choosecolor != menucolortail)
			choosecolor = choosecolor->next;

		// Draw color in the middle
		x += numcolors*w;
		for (h = 0; h < 16; h++)
			V_DrawFill(x, my+162+h, charw, 1, skincolors[cv_dummycolor.value].ramp[h]);

		//Draw colors from middle to left
		mc = choosecolor->prev;
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
		mc = choosecolor->next;
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
}

void MD_DrawCssCharacter(void)
{
	INT32 mx, my, flags = 0;
	INT32 st;
	patch_t *patch;
	UINT8 frame;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;

	INT32 skintodisplay = cv_chooseskin.value;

	mx = menudefs[MN_MP_PLAYERSETUP].x;
	my = menudefs[MN_MP_PLAYERSETUP].y;

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
	sprdef = &skins[skintodisplay].sprites[multi_spr2];

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

	V_SetClipRect((mx + 43 - (charw/2)) * FRACUNIT, (my+65) * FRACUNIT, charw * FRACUNIT, 84 * FRACUNIT, 0);
	// draw player sprite
	UINT8 *colormap = R_GetTranslationColormap(skintodisplay, cv_dummycolor.value, GTC_MENUCACHE);
	V_DrawFixedPatch((mx+43)<<FRACBITS,
				(my+136)<<FRACBITS,
				skins[skintodisplay].highresscale,
				flags, patch, colormap);

	// draw their follower if there is one
	if (cv_dummyfollower.value > -1)
	{
		follower_t fl = followers[cv_dummyfollower.value]; // shortcut for our sanity

		tic_t bobspeed = fl.bobspeed;
		if (fl.mode == FOLLOWERMODE_GROUND)
			bobspeed = FixedDiv(bobspeed*FRACUNIT, fl.bobamp / 6); // rough approximation of bounce speed

		// smooth floating, totally not stolen from rocket sneakers.
		fixed_t sine = FixedMul(fl.bobamp, FINESINE(((FixedMul(4 * M_TAU_FIXED, bobspeed) * followertimer)>>ANGLETOFINESHIFT) & FINEMASK));

		follower_tics -= renderdeltatics;

		// restart the ground follower's animation when it lands on the "ground"
		if (fl.mode == FOLLOWERMODE_GROUND)
		{
			// this sucks
			fixed_t sine_next = FixedMul(fl.bobamp, FINESINE(((FixedMul(4 * M_TAU_FIXED, bobspeed) * (followertimer + 1))>>ANGLETOFINESHIFT) & FINEMASK));
			if ((sine > 0) != (sine_next > 0))
			{
				st = fl.followstate;
				if (st != S_NULL)
					follower_state = &states[st];
				follower_tics = follower_state->tics*FRACUNIT;
				follower_frame = follower_state->frame & FF_FRAMEMASK; // get spritedef
			}
		}

		// animate the follower
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
				follower_frame = follower_state->frame & FF_FRAMEMASK; // get spritedef
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
		{
			// Fake the follower's in game appearance by now also applying some of its variables! coolio, eh?
			UINT16 color = K_GetEffectiveFollowerColor(cv_followercolor[setupplayer].value, &fl, cv_dummycolor.value, &skins[skintodisplay]);
			colormap = R_GetTranslationColormap(TC_DEFAULT, color, 0); // why does GTC_MENUCACHE not work here...?

			INT32 x = (mx+65)*FRACUNIT;
			INT32 y = ((my+105)*FRACUNIT);
			if (fl.mode == FOLLOWERMODE_GROUND)
				y += 40*FRACUNIT - FixedMul(abs(sine), max(2*FRACUNIT, (fl.bobamp*2)/3)); // bounce animation
			else
				y += sine;

			V_DrawFixedPatch(x, y, fl.scale, flags, patch, colormap);
			Z_Free(colormap);
		}
	}
	V_ClearClipRect();
}
#undef charw

void MD_DrawBarCssSelector(void)
{
	INT32 my;

	// mx = menudefs[MN_MP_PLAYERSETUP].x;
	my = menudefs[MN_MP_PLAYERSETUP].y;

	// character bar, ripped off the color bar :V
#define iconwidth 32
	{
		const INT32 icons = 4;
		INT32 k = -icons;
		INT16 col = (cv_chooseskin.value - icons) % numskins;
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
				offx = 8 + face->leftoffset;
				offy = 8 + face->topoffset;
			}
			colmap = R_GetTranslationColormap(col, cv_dummycolor.value, GTC_MENUCACHE);
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
}

// minenice's grid style CSS from an unfinished moe mansion pull request
// originally based on callmore's skin select

#define SKINXSHIFT 33
#define SKINYSHIFT 28

void MD_DrawGridCssSelector(void)
{
	INT32 skinn;
	patch_t *face;
	UINT8 *colmap;

	patch_t *cursor;
	static fixed_t cursorframe = 0;

	INT32 gridx;
	INT32 gridy;
	INT32 cursorx;
	INT32 cursory;
	INT32 calcs;

	INT32 skintodisplay = cv_chooseskin.value;

	// gridcss_skinydrag = 0;
	// draws background and scroll bar
	{
		INT32 x = ((BASEVIDWIDTH / 2) - (18 * SKINGRIDWIDTH) - 8) + 100 + SKINXSHIFT - 1;
		INT32 y = ((BASEVIDHEIGHT / 2) - (18 * (SKINGRIDWIDTH/2))) + SKINYSHIFT - 1;

		INT32 dx = (SKINGRIDWIDTH * 18);
		INT32 dy = (SKINGRIDHEIGHT * 18);
		INT32 scrx = 4; //2 is added to make the final "thickness"

		INT32 columncount;
		INT32 barlen;
		INT32 barpos;

		//draw BG
		V_DrawFill(x, y, dx, dy, 159);

		columncount = ((numskins - 1) / SKINGRIDWIDTH) + 1;

		if (columncount > SKINGRIDHEIGHT)
		{
			//draw scroll bar "back"
			x += dx + 4;
			V_DrawFill(x, y, scrx+2, dy, 9);
			V_DrawFill(x, y, 1, dy, 10);
			V_DrawFill(x, y + dy - 1, scrx+2, 1, 10);
			x++;
			y++;
			dy -= 2;
			V_DrawFill(x, y, scrx, dy, 15);

			//draw scroll bar bar
			barlen = dy * SKINGRIDHEIGHT / columncount;
			barpos = dy * gridcss_skinydrag / columncount;

			if (gridcss_skinydrag >= columncount - SKINGRIDHEIGHT)
			{
				barpos = dy - barlen;
			}

			V_DrawFill(x, y + barpos, scrx, barlen, 5);
			V_DrawFill(x, y + barpos, 1, barlen, 7);
			V_DrawFill(x, y + barpos + barlen - 1, scrx, 1, 7);
		}
	}
	// end background and scroll bar

	for (INT32 gridslot = 0; gridslot < SKINGRIDWIDTH*SKINGRIDHEIGHT; gridslot++)
	{
		gridx = ((gridslot % SKINGRIDWIDTH) * 18) + ((BASEVIDWIDTH / 2) - (18 * SKINGRIDWIDTH) - 8) + 100 + SKINXSHIFT; //BASEVIDWIDTH / 2 - ((icons + 1) * 24) - 4;
		gridy = ((gridslot / SKINGRIDWIDTH) * 18) + ((BASEVIDHEIGHT / 2) - (18 * (SKINGRIDWIDTH/2))) + SKINYSHIFT; //BASEVIDWIDTH / 2 - ((icons + 1) * 24) - 4;
		calcs = gridslot + (gridcss_skinydrag * SKINGRIDWIDTH);

		if (calcs < numskins)
			skinn = skinsorted[calcs];
			// skinn = calcs;
		else if (gridslot % SKINGRIDWIDTH == 0)
			break; //really conveniant (sic) place to break out here
		else
		{
			// draw an empty slot
			V_DrawFill(gridx, gridy, 16, 16, 158);
			continue;
		}

		if (skinn == skintodisplay)
		{
			cursorx = gridx;
			cursory = gridy;
		}
		else
		{
			face = faceprefix[skinn][FACE_RANK];
			colmap = R_GetTranslationColormap(skinn, skins[skinn].prefcolor, GTC_MENUCACHE);
			V_DrawFixedPatch((gridx + face->leftoffset) * FRACUNIT, (gridy + face->topoffset) * FRACUNIT, FRACUNIT, 0, face, colmap);
		}
	}

	// draw wanted portrait and cursor
	if (M_IsItemOn(MN_MP_PLAYERSETUP, "SKIN"))
	{
		face = faceprefix[skintodisplay][FACE_WANTED];
		colmap = R_GetTranslationColormap(skintodisplay, cv_dummycolor.value, GTC_MENUCACHE);
		V_DrawFixedPatch((cursorx * FRACUNIT) - (face->width * FRACUNIT/4), (cursory * FRACUNIT) - (face->height * FRACUNIT/4), FRACUNIT, 0, face, colmap);

		cursorframe = (cursorframe + (renderdeltatics / 4)) % (7*FRACUNIT);
		cursor = W_CachePatchName(va("K_BHILI%d", (cursorframe >> FRACBITS) + 1), PU_CACHE);
		// cursor patch offsets are wrong so draw at same coordinate as portrait
		V_DrawFixedPatch((cursorx * FRACUNIT) - (face->width * FRACUNIT/4), (cursory * FRACUNIT) - (face->height * FRACUNIT/4), FRACUNIT, 0, cursor, colmap);
	}
	else
	{
		face = faceprefix[skintodisplay][FACE_RANK];
		colmap = R_GetTranslationColormap(skintodisplay, cv_dummycolor.value, GTC_MENUCACHE);
		V_DrawFixedPatch((cursorx + face->leftoffset) * FRACUNIT, (cursory + face->topoffset) * FRACUNIT, FRACUNIT, 0, face, colmap);
		
		if (skullAnimCounter < 4)
		{
			cursor = W_CachePatchName("M_FSEL", PU_CACHE);
			colmap = V_GetStringColormap(skincolors[cv_dummycolor.value].chatcolor);
			V_DrawFixedPatch(cursorx * FRACUNIT, cursory * FRACUNIT, FRACUNIT/2, 0, cursor, colmap);
		}
	}
}

//
// Maps a CSS grid selection cursor column (x) and row (y) to a skin selection index
//
static inline INT32 MapGridSelectToSkin(INT32 row, INT32 column)
{
	return ((row % SKINGRIDWIDTH) + (column * SKINGRIDWIDTH));
}

INT32 MR_HandleSetupMultiPlayerMenu(INT32 choice)
{
	INT32 sortedIndex = 0;
	// don't consume input if we're not interacting with the grid CSS
	if (!(M_IsItemOn(MN_MP_PLAYERSETUP, "SKIN") && cv_skinselectstyle.value))
	{
		return false;
	}

	switch (choice)
	{
		case KEY_DOWNARROW:
			if (gridcss_column == (numskins - 1) / SKINGRIDWIDTH)
			{
				M_SetItemOn(MN_MP_PLAYERSETUP, "FOLLOWER");
				CV_SetValue(&cv_chooseskin, gridcss_skinmemory);
				sortedIndex = FindSortedSkinIndex(cv_chooseskin.value);
				gridcss_row = sortedIndex % SKINGRIDWIDTH;
				gridcss_column = sortedIndex / SKINGRIDWIDTH;

				gridcss_skinydrag = CLAMP(gridcss_column - SKINGRIDHEIGHT + 1, 0, ((numskins - 1) / SKINGRIDWIDTH) - SKINGRIDHEIGHT + 1);
			}
			else
			{
				gridcss_column++;

				if (MapGridSelectToSkin(gridcss_row, gridcss_column) > (numskins - 1))
				{
					gridcss_row = (numskins - 1) % SKINGRIDWIDTH;
					gridcss_column = (numskins - 1) / SKINGRIDWIDTH;
				}

				if ((gridcss_column - gridcss_skinydrag) > SKINGRIDHEIGHT - 1)
					gridcss_skinydrag++;
			}
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_UPARROW:
			if (gridcss_column == 0)
			{
				M_SetItemOn(MN_MP_PLAYERSETUP, "NAME");
				CV_SetValue(&cv_chooseskin, gridcss_skinmemory);
				sortedIndex = FindSortedSkinIndex(cv_chooseskin.value);
				gridcss_row = sortedIndex % SKINGRIDWIDTH;
				gridcss_column = sortedIndex / SKINGRIDWIDTH;

				gridcss_skinydrag = CLAMP(gridcss_column - SKINGRIDHEIGHT + 1, 0, ((numskins - 1) / SKINGRIDWIDTH) - SKINGRIDHEIGHT + 1);
			}
			else
			{
				gridcss_column--;

				if (gridcss_column < gridcss_skinydrag)
					gridcss_skinydrag--;
			}
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_LEFTARROW:
			if (gridcss_row == 0)
			{
				if (MapGridSelectToSkin(SKINGRIDWIDTH - 1, gridcss_column) > (numskins-1))
					gridcss_row = numskins - (gridcss_column * SKINGRIDWIDTH) - 1;
				else
					gridcss_row = SKINGRIDWIDTH - 1;
				if (gridcss_column > 0)
				{
					gridcss_row = SKINGRIDWIDTH - 1;
					gridcss_column--;

					if (gridcss_column < gridcss_skinydrag)
						gridcss_skinydrag--;
				}
			}
			else
			{
				gridcss_row--;
			}
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_RIGHTARROW:
			if ((gridcss_row + 1 == SKINGRIDWIDTH) || (MapGridSelectToSkin(gridcss_row + 1, gridcss_column) > (numskins - 1)))
			{
				gridcss_row = 0;
				if (MapGridSelectToSkin(gridcss_row, gridcss_column + 1) <= (numskins - 1))
				{
					gridcss_column++;

					if ((gridcss_column - gridcss_skinydrag) > SKINGRIDHEIGHT - 1)
						gridcss_skinydrag++;
				}
			}
			else
			{
				gridcss_row++;
			}
			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_PGDN:
			if (gridcss_column <= (numskins - 1) / SKINGRIDWIDTH)
			{
				gridcss_column = min(gridcss_column + SKINGRIDHEIGHT - 1, (numskins - 1) / SKINGRIDWIDTH);

				if (MapGridSelectToSkin(gridcss_row, gridcss_column) > (numskins - 1))
				{
					gridcss_row = (numskins - 1) % SKINGRIDWIDTH;
					gridcss_column = (numskins - 1) / SKINGRIDWIDTH;
				}

				gridcss_skinydrag = CLAMP(gridcss_column - SKINGRIDHEIGHT + 1, 0, ((numskins - 1) / SKINGRIDWIDTH) - SKINGRIDHEIGHT + 1);

				S_StartSound(NULL, sfx_menu1);
			}
			break;
		case KEY_PGUP:
			if (gridcss_column > 0)
			{
				gridcss_column = max(gridcss_column - SKINGRIDHEIGHT + 1, 0);
				gridcss_skinydrag = max(gridcss_column, 0);
				S_StartSound(NULL, sfx_menu1);
			}
			break;
		case KEY_TAB:
			M_SetItemOn(MN_MP_PLAYERSETUP, "FOLLOWER");
			CV_SetValue(&cv_chooseskin, gridcss_skinmemory);
			sortedIndex = FindSortedSkinIndex(cv_chooseskin.value);
			gridcss_row = sortedIndex % SKINGRIDWIDTH;
			gridcss_column = sortedIndex / SKINGRIDWIDTH;

			gridcss_skinydrag = CLAMP(gridcss_column - SKINGRIDHEIGHT + 1, 0, ((numskins - 1) / SKINGRIDWIDTH) - SKINGRIDHEIGHT - 1);

			S_StartSound(NULL, sfx_menu1);
			break;
		case KEY_ENTER:
			if (cv_chooseskin.value < numskins)
			{
				gridcss_skinmemory = cv_chooseskin.value;
				S_StartSound(NULL, sfx_s221);
			}
			break;
		default:
			return false;
	}

	CV_SetValue(&cv_chooseskin, skinsorted[MapGridSelectToSkin(gridcss_row, gridcss_column)]);
	return true;
}

// follower state update. This is its own function so that it's at least somewhat clean
static void M_GetFollowerState(void)
{
	if (cv_dummyfollower.value <= -1) // yikes, there's none!
		return;
	// ^ we don't actually need to set anything since it won't be displayed anyway.

	//followertimer = 0;	// reset timer. not like it'll overflow anytime soon but whatever.

	// set follower state
	follower_state = &states[followers[cv_dummyfollower.value].followstate];

	if (follower_state->frame & FF_ANIMATE)
		follower_tics = follower_state->var2*FRACUNIT;	// support for FF_ANIMATE
	else
		follower_tics = follower_state->tics*FRACUNIT;

	follower_frame = follower_state->frame & FF_FRAMEMASK;
}

// start the multiplayer setup menu
INT32 MR_SetupMultiPlayer(INT32 arg)
{
	if (arg < 0 || arg >= MAXSPLITSCREENPLAYERS)
		return false;

	setupplayer = arg;
	M_PatchSkinNameTable();

	multi_state = &states[mobjinfo[MT_PLAYER].seestate];
	multi_tics = multi_state->tics * FRACUNIT;

	CV_Set(&cv_dummyname, cv_playername[arg].string);
	CV_SetValue(&cv_chooseskin, R_SkinAvailable(cv_skin[arg].string));
	gridcss_skinmemory = cv_chooseskin.value;
	CV_SetValue(&cv_dummyfollower, cv_follower[arg].value);
	CV_SetValue(&cv_dummycolor, cv_playercolor[arg].value);
	G_SetPlayerControllerIndicatorColor(arg, cv_playercolor[arg].value);
	dummycolorplayer = arg;

	Skinsort_option_Onchange();

	M_GetFollowerState();	// update follower state

	// disable skin changes if we can't actually change skins
	M_SetItemDisabled(MN_MP_PLAYERSETUP, "SKIN", splitscreen >= arg && !CanChangeSkin(arg == 0 ? consoleplayer : g_localplayers[arg]));
	Skinselectstyle_option_Onchange();

	return true;
}

INT32 MR_QuitMultiPlayerMenu(INT32 choice)
{
	(void)choice;

	if (cv_skinselectstyle.value == 1)
		CV_SetValue(&cv_chooseskin, gridcss_skinmemory);

	const char *followername = cv_dummyfollower.value == -1 ?
		"None" : followers[cv_dummyfollower.value].skinname;
	COM_BufInsertText(va(
		"%s \"%s\"; %s \"%s\"; %s \"%s\"; %s \"%s\"\n",
		cv_playername[setupplayer].name,  cv_dummyname.string,
		cv_skin[setupplayer].name,        skins[cv_chooseskin.value].name,
		cv_playercolor[setupplayer].name, skincolors[cv_dummycolor.value].name,
		cv_follower[setupplayer].name,    followername
	));
	return true;
}
#undef SKINXSHIFT
#undef SKINYSHIFT

#undef SKINGRIDWIDTH
#undef SKINGRIDHEIGH

void M_AddMenuColor(UINT16 color) {
	menucolor_t *c;

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

INT32 MR_EraseData(INT32 arg)
{
	const char *eschoice, *esstr = M_GetText("Are you sure you want to erase\n%s?\n\n(Press 'Y' to confirm)\n");

	erasecontext = (UINT8)arg;

	if (arg == 0)
		eschoice = M_GetText("Record Attack data");
	else if (arg == 1)
		eschoice = M_GetText("Secrets data");
	else
		eschoice = M_GetText("ALL game data");

	M_StartMessage(va(esstr, eschoice), M_EraseDataResponse, MM_YESNO);
	return true;
}

INT32 MR_ScreenshotOptions(INT32 choice)
{
	(void)choice;
	Screenshot_option_Onchange();
	Moviemode_mode_Onchange();
	return true;
}

// =============
// JOYSTICK MENU
// =============

// Start the controls menu, setting it up for either the console player,
// or the secondary splitscreen player

void MD_DrawJoystick(void)
{
	INT32 i;
	INT32 compareval;
#ifdef JOYSTICK_HOTPLUG
	const INT32 numcontrollers = I_NumControllers();
#endif
	MD_DrawGenericMenu();

	for (i = 0; i <= MAXGAMEPADS; i++)
	{
#ifdef JOYSTICK_HOTPLUG
		if (atoi(cv_usecontroller[setupcontrolplayer-1].string) > numcontrollers)
			compareval = atoi(cv_usecontroller[setupcontrolplayer-1].string);
		else
#endif
			compareval = cv_usecontroller[setupcontrolplayer-1].value;

		V_DrawString(menudefs[MN_OP_JOYSTICKSET].x, menudefs[MN_OP_JOYSTICKSET].y + 16*i, ((i == compareval) ? V_GREENMAP : 0)|MENUCAPS, joystickInfo[i]);
	}
}

INT32 MR_SetupJoystickMenu(INT32 arg)
{
	const char *joyNA = "Unavailable";
	const INT32 numcontrollers = I_NumControllers();

	INT32 i = 0;

	if (arg < 0 || arg >= MAXSPLITSCREENPLAYERS)
		return false;

	setupcontrolplayer = arg + 1;

	strcpy(joystickInfo[i], "None");

	for (i = 1; i <= MAXGAMEPADS; i++)
	{
		if (i <= numcontrollers && (I_GetControllerName(i)) != NULL)
			strncpy(joystickInfo[i], I_GetControllerName(i), 28);
		else
			strcpy(joystickInfo[i], joyNA);

#ifdef JOYSTICK_HOTPLUG
		// We use cv_usecontroller.string as the USER-SET var
		// and cv_usecontroller.value as the INTERNAL var
		//
		// In practice, if cv_usecontroller.string == 0, this overrides
		// cv_usecontroller.value and always disables
		//
		// Update cv_usecontroller.string here so that the user can
		// properly change this value.
		for (INT32 j = 0; j < MAXSPLITSCREENPLAYERS; j++)
		{
			if (i == cv_usecontroller[j].value)
				CV_SetValue(&cv_usecontroller[j], i);
		}
#endif
	}

	return true;
}

INT32 MR_AssignJoystick(INT32 arg)
{
	const UINT8 p = setupcontrolplayer-1;

#ifdef JOYSTICK_HOTPLUG
	INT32 oldchoice, oldstringchoice;
	const INT32 numcontrollers = I_NumControllers();

	oldchoice = oldstringchoice = atoi(cv_usecontroller[p].string) > numcontrollers ? atoi(cv_usecontroller[p].string) : cv_usecontroller[p].value;
	CV_SetValue(&cv_usecontroller[p], arg);

	// Just in case last-minute changes were made to cv_usecontroller.value,
	// update the string too
	// But don't do this if we're intentionally setting higher than numcontrollers
	if (arg <= numcontrollers)
	{
		CV_SetValue(&cv_usecontroller[p], cv_usecontroller[p].value);

		// reset this so the comparison is valid
		if (oldchoice > numcontrollers)
			oldchoice = cv_usecontroller[p].value;

		if (oldchoice != arg)
		{
			if (arg && oldstringchoice > numcontrollers) // if we did not select "None", we likely selected a used device
				CV_SetValue(&cv_usecontroller[p], (oldstringchoice > numcontrollers ? oldstringchoice : oldchoice));

			if (oldstringchoice ==
				(atoi(cv_usecontroller[p].string) > numcontrollers ? atoi(cv_usecontroller[p].string) : cv_usecontroller[p].value))
				M_StartMessage("This joystick is used by another\n"
							   "player. Reset the joystick\n"
							   "for that player first.\n\n"
							   "(Press a key)\n", NULL, MM_NOTHING);
		}
	}
#else
	CV_SetValue(&cv_usecontroller[p], arg);
#endif
	return true;
}

// =============
// CONTROLS MENU
// =============

INT32 MR_SetupControlsMenu(INT32 arg)
{
	boolean player1 = arg == 0;

	if (arg < 0 || arg >= MAXSPLITSCREENPLAYERS)
		return false;

	setupcontrolplayer = arg + 1;
	setupcontrols = gamecontrol[arg];        // was called from main Options (for console player, then)

	// Set proper gamepad options
	M_SetItemArgument(MN_OP_CHANGECONTROLS, "SETJOY", arg);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "XDEADZ", &cv_deadzonex[arg]);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "YDEADZ", &cv_deadzoney[arg]);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "TDEADZ", &cv_deadzonet[arg]);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "DEAZS", &cv_deadzonestyle[arg]);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "TURNSMOOTHING", &cv_turnsmooth[arg]);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "LITESTEER", &cv_litesteer[arg]);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "LEDCOLOR", &cv_controllerled[arg]);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "RUMBLE", &cv_controllerrumble[arg]);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "RUMBLESTR", &cv_rumblestrength[arg]);
	M_SetItemCvar(MN_OP_CHANGECONTROLS, "TILTCONTROL", &cv_tiltcontrol[arg]);

	M_SetItemVisible(MN_OP_CHANGECONTROLS, "TALK", player1); // Chat
	//M_SetItemVisible(MN_OP_CHANGECONTROLS, "TEAM", player1); // Team-chat
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "SCORES", player1); // Rankings
	//M_SetItemVisible(MN_OP_CHANGECONTROLS, "VIEWPOINT", player1); // Viewpoint
	// 19 is Reset Camera, 20 is Toggle Chasecam
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "SCREENSHOT", player1); // Screenshot
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "RECORDGIF", player1); // GIF
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "SYSTEMMENU", player1); // System Menu
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "CONSOLE", player1); // Console
	/*
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "SPECTATORBUTTONS", player1); // Spectator Controls header
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "SPECTATORBUTTONS_", player1); // Spectator Controls space
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "SPECTATE", player1); // Spectate
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "LOOKUP", player1); // Look Up
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "LOOKDOWN", player1); // Look Down
	M_SetItemVisible(MN_OP_CHANGECONTROLS, "CENTERVIEW", player1); // Center View
	*/

	return true;
}

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

INT32 MR_ChangeControl(INT32 arg)
{
	// This buffer assumes a 35-character message (per below) plus a max control name limit of 32 chars (per controltochangetext)
	// If you change the below message, then change the size of this buffer!
	static char tmp[68];

	controltochange = arg;
	sprintf(tmp, M_GetText("Hit the new key for\n%s\nESC for Cancel"),
		currentMenu->menuitems[itemOn].text);
	strlcpy(controltochangetext, currentMenu->menuitems[itemOn].text, 33);

	M_StartMessage(tmp, M_ChangecontrolResponse, MM_EVENTHANDLER);
	return true;
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
	CV_StealthSet(&cv_usecontroller[p], cv_usecontroller[p].defaultvalue);

	S_StartSound(NULL, sfx_s224);
}

INT32 MR_ResetControls(INT32 choice)
{
	(void)choice;
	M_StartMessage(va(M_GetText("Reset Player %d's controls to defaults?\n\n(Press 'Y' to confirm)\n"), setupcontrolplayer), M_ResetControlsResponse, MM_YESNO);
	return true;
}

INT32 MR_RestartAudio(INT32 choice)
{
	(void)choice;
	COM_ImmedExecute("restartaudio");
	return true;
}

// ===============
// VIDEO MODE MENU
// ===============

//added : 30-01-98:
#define MAXCOLUMNMODES   12     //max modes displayed in one column
#define MAXMODEDESCS     (MAXCOLUMNMODES*3)

static modedesc_t modedescs[MAXMODEDESCS];

INT32 MR_VideoModeMenu(INT32 choice)
{
	INT32 i, j, vdup, nummodes;
	UINT32 width, height;
	const char *desc;

	(void)choice;

	memset(modedescs, 0, sizeof(modedescs));

	VID_PrepareModeList(); // FIXME: hack

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
				if (fastcmp(modedescs[j].desc, desc))
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
	return true;
}

// Draw the video modes list, a-la-Quake
void MD_DrawVideoMode(void)
{
	INT32 i, j, row, col;

	// draw title
	M_DrawMenuTitle();

	V_DrawCenteredString(BASEVIDWIDTH/2, menudefs[MN_OP_VIDEOMODE].y,
		MENUCAPS|highlightflags, "Choose mode, reselect to change default");

	row = 41;
	col = menudefs[MN_OP_VIDEOMODE].y + 14;
	for (i = 0; i < vidm_nummodes; i++)
	{
		if (i == vidm_selected)
			V_DrawString(row, col, MENUCAPS|highlightflags, modedescs[i].desc);
		// Show multiples of 320x200 as green.
		else
			V_DrawString(row, col, MENUCAPS|((modedescs[i].goodratio) ? recommendedflags : 0), modedescs[i].desc);

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
			MENUCAPS|recommendedflags, "Marked modes are recommended.");
		V_DrawCenteredString(BASEVIDWIDTH/2, menudefs[MN_OP_VIDEOMODE].y + 146,
			MENUCAPS|highlightflags, "Other modes may have visual errors.");
		V_DrawCenteredString(BASEVIDWIDTH/2, menudefs[MN_OP_VIDEOMODE].y + 158,
			MENUCAPS|highlightflags, "Larger modes may have performance issues.");
	}

	// Draw the cursor for the VidMode menu
	i = 41 - 10 + ((vidm_selected / vidm_column_size)*7*13);
	j = menudefs[MN_OP_VIDEOMODE].y + 14 + ((vidm_selected % vidm_column_size)*8);

	V_DrawScaledPatch(i - 8, j, 0,
		W_CachePatchName("M_CURSOR", PU_CACHE));
}

// special menuitem key handler for video mode list
INT32 MR_HandleVideoMode(INT32 ch)
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

		default:
			return false;
	}
	return true;
}

INT32 MR_CameraSetup(INT32 arg)
{
	if (arg < 0 || arg >= MAXSPLITSCREENPLAYERS)
		return false;

	M_SetItemCvar(MN_OP_CAMERASETUP, "FLIPCAM", &cv_flipcam[arg]);
	M_SetItemCvar(MN_OP_CAMERASETUP, "CAMFOV", &cv_fov[arg]);
	M_SetItemCvar(MN_OP_CAMERASETUP, "CAMDISTANCE", &cv_cam_dist[arg]);
	M_SetItemCvar(MN_OP_CAMERASETUP, "CAMHEIGHT", &cv_cam_height[arg]);
	M_SetItemCvar(MN_OP_CAMERASETUP, "CAMSPEED", &cv_cam_speed[arg]);
	M_SetItemCvar(MN_OP_CAMERASETUP, "CHASECAM", &cv_chasecam[arg]);
	return true;
}

// ===============
// Monitor Toggles
// ===============

static tic_t shitsfree = 0;
static tic_t fliptic[MAXKARTITEMS] = {0};

#define FLIPTIC_TIME (TICRATE / 2)

INT32 MR_SetupMonitorToggles(INT32 choice)
{
	(void)choice;
	const INT32 height = 4;
	menu_t *menudef = &menudefs[MN_OP_MONITORTOGGLE];

	// in the interest of not rushing the implementation of menu frames...
	// leak the memory. every single time.
	// (i dont think this actually leaks anything lol)
	// (unless someone adds menuitems to this menu for some stupid reason)
	menudef->numitems = 0;

	for (UINT8 i = 0; i <= numkartresults; i++)
	{
		kartresult_t *result = i < 3 ? &kartresults[i] : i == 3 ? NULL : &kartresults[i - 1];
		if (result != NULL && result->isalt)
			continue;

		menudef->menuitems = Z_Realloc(menudef->menuitems, sizeof(menuitem_t)*(menudef->numitems+1), PU_STATIC, NULL);
		menuitem_t *item = menudef->menuitems + menudef->numitems++;

		if (result == NULL)
		{
			item->argument = 1;
			item->patch = "";
		}
		else
		{
			item->argument = 0;
			item->patch = result->cvar->name;
			if (kartitems[result->type].altcvar != NULL)
				item->tooltip = "Press BACKSPACE to swap variants.";
			else if (result->type == KITEM_SUPERRING && cv_kartrings.value == 0)
				item->tooltip = "Rings must be enabled in Gameplay Mods!";
		}
	}

	while (menudef->numitems % height != 0)
	{
		menudef->menuitems = Z_Realloc(menudef->menuitems, sizeof(menuitem_t)*(menudef->numitems+1), PU_STATIC, NULL);
		menuitem_t *item = menudef->menuitems + menudef->numitems++;

		item->argument = 2;
		item->patch = "";
	}

	menudef->x = 136 - FixedMul(max(0, min((menudef->numitems - 1) / height, 7)), TICRATE*FRACUNIT/2);
	return true;
}

void MD_DrawMonitorToggles(void)
{
	const INT32 edges = 4;
	const INT32 height = 4;
	const INT32 spacing = 35;
	const INT32 column = itemOn / height;
	// const INT32 row = itemOn%height;
	INT32 leftdraw, rightdraw, totaldraw;
	INT32 x = currentMenu->x, y = currentMenu->y + (spacing / 4);
	INT32 onx = 0, ony = 0;
	INT32 i, translucent;
	const char* displayname;

	M_DrawMenuTitle();

	// Find the available space around column
	leftdraw = rightdraw = column;
	totaldraw = 0;
	for (i = 0; (totaldraw < edges * 2 && i < edges * 4); i++)
	{
		if (rightdraw + 1 < (currentMenu->numitems / height) + 1)
		{
			rightdraw++;
			totaldraw++;
		}
		if (leftdraw - 1 >= 0)
		{
			leftdraw--;
			totaldraw++;
		}
	}

	for (i = leftdraw; i <= rightdraw; i++)
	{
		INT32 j;

		for (j = 0; j < height; j++, y += spacing)
		{
			const INT32 thisitem = (i * height) + j;

			if (thisitem >= currentMenu->numitems)
				continue;

			if (thisitem == itemOn)
			{
				onx = x;
				ony = y;
				continue;
			}

			switch (currentMenu->menuitems[thisitem].argument)
			{
			case 2:
				V_DrawScaledPatch(
				    x, y, V_TRANSLUCENT, W_CachePatchName("K_ISBG", PU_CACHE));
				continue;
			case 1:
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISBG", PU_CACHE));
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISTOGL", PU_CACHE));
				continue;
			}

			const kartresult_t* result =
			    K_GetKartResult(currentMenu->menuitems[thisitem].patch);

			boolean enabled = result->cvar->value == 1;

			if (result->type == KITEM_SUPERRING && cv_kartrings.value == 0)
				enabled = false;

			translucent = enabled ? 0 : V_TRANSLUCENT;

			if (enabled)
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISBG", PU_CACHE));
			else
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISBGD", PU_CACHE));

			if (result->amount >= K_GetItemNumberDisplayMin(result->type, true))
			{
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISMUL", PU_CACHE));
				V_DrawScaledPatch(
				    x,
				    y,
				    translucent,
				    K_GetCachedItemPatch(result->type, true, result->amount));
				V_DrawString(x + 24,
					     y + 31,
					     V_ALLOWLOWERCASE | translucent,
					     va("x%d", result->amount));
			}
			else
				V_DrawScaledPatch(
				    x,
				    y,
				    translucent,
				    K_GetCachedItemPatch(result->type, true, result->amount));

			if (kartitems[result->type].altcvar != NULL && kartitems[result->type].altcvar->value && K_ShowAltItemIcon(result->type, true))
				V_DrawScaledPatch(x, y, 0, K_getItemAltPatch(true, false));
		}

		x += spacing;
		y = currentMenu->y + (spacing / 4);
	}

	switch (currentMenu->menuitems[itemOn].argument)
	{
	case 2:
	{
		displayname = "---";

		V_DrawScaledPatch(
		    onx - 1, ony - 2, V_TRANSLUCENT, W_CachePatchName("K_ITBG", PU_CACHE));

		if (shitsfree)
		{
			INT32 trans = V_TRANSLUCENT;
			if (shitsfree - 1 > TICRATE - 5)
				trans = ((10 - TICRATE) + shitsfree - 1) << V_ALPHASHIFT;
			else if (shitsfree < 5)
				trans = (10 - shitsfree) << V_ALPHASHIFT;
			V_DrawScaledPatch(
			    onx - 1, ony - 2, trans, W_CachePatchName("K_ITFREE", PU_CACHE));
		}
		break;
	}
	case 1:
	{
		displayname = "Toggle All";
		V_DrawScaledPatch(onx - 1, ony - 2, 0, W_CachePatchName("K_ITBG", PU_CACHE));
		V_DrawScaledPatch(onx - 1, ony - 2, 0, W_CachePatchName("K_ITTOGL", PU_CACHE));
		break;
	}
	default:
	{
		const kartresult_t* result = K_GetKartResult(currentMenu->menuitems[itemOn].patch);
		const kartitem_t* item = &kartitems[result->type];
		boolean enabled = result->cvar->value == 1;

		fixed_t fancyflip = abs(FCOS(FixedAngle(Easing_InOutCubic(
		                        fliptic[result->type] * FRACUNIT / FLIPTIC_TIME, 0, 180 * FRACUNIT))));
		fixed_t flipoffset = 25 * (FRACUNIT - fancyflip);

		if (result->type == KITEM_SUPERRING && cv_kartrings.value == 0)
			enabled = false;

		displayname = result->displayname;
		translucent = enabled ? 0 : V_TRANSLUCENT;

		if (enabled)
			V_DrawScaledPatch(
			    onx - 1, ony - 2, 0, W_CachePatchName("K_ITBG", PU_CACHE));
		else
			V_DrawScaledPatch(
			    onx - 1, ony - 2, 0, W_CachePatchName("K_ITBGD", PU_CACHE));

		SINT8 showalt = item->altcvar != NULL && !K_ShowAltItemIcon(result->type, false) ? item->altcvar->value : -1;
		if (fliptic[result->type] > FLIPTIC_TIME/2)
			showalt = !showalt;

		if (result->amount >= K_GetItemNumberDisplayMin(result->type, false))
		{
			V_DrawScaledPatch(
			    onx - 1, ony - 2, 0, W_CachePatchName("K_ITMUL", PU_CACHE));

			V_DrawStretchyFixedPatch(
			    ((onx - 1) << FRACBITS) + flipoffset,
			    (ony - 2) << FRACBITS,
			    fancyflip,
			    FRACUNIT,
			    translucent,
			    K_GetCachedItemPatchEx(result->type, false, result->amount, showalt),
			    NULL);

			V_DrawScaledPatch(
			    onx + 27, ony + 39, translucent, W_CachePatchName("K_ITX", PU_CACHE));

			V_DrawKartString(onx + 37, ony + 34, translucent, va("%d", result->amount));
		}
		else
			V_DrawStretchyFixedPatch(
			    ((onx - 1) << FRACBITS) + flipoffset,
			    (ony - 2) << FRACBITS,
			    fancyflip,
			    FRACUNIT,
			    translucent,
			    K_GetCachedItemPatchEx(result->type, false, result->amount, showalt),
			    NULL);

		if (item->altcvar != NULL && K_ShowAltItemIcon(result->type, false))
		{
			translucent = item->altcvar->value ? 0 : V_TRANSLUCENT;
			V_DrawScaledPatch(
			    onx - 1, ony - 2, translucent, K_getItemAltPatch(false, false));
		}
		break;
	}
	}

	if (renderisnewtic)
	{
		if (shitsfree)
			shitsfree--;

		for (i = 0; i < numkartitems; i++)
		{
			if (fliptic[i])
				fliptic[i]--;
		}
	}

	V_DrawCenteredString(
	    BASEVIDWIDTH / 2, currentMenu->y, MENUCAPS | highlightflags, va("* %s *", displayname));

	M_DrawMenuTooltips();
}

INT32 MR_HandleMonitorToggles(INT32 choice)
{
	const INT32 height = 4;
	const INT32 width = currentMenu->numitems/height;
	INT32 column = itemOn/height, row = itemOn%height;
	INT16 next;
	kartresult_t *result = K_GetKartResult(currentMenu->menuitems[itemOn].patch);

	switch (choice)
	{
		case KEY_RIGHTARROW:
			S_StartSound(NULL, sfx_menu1);
			column++;
			if (((column*height)+row) >= currentMenu->numitems)
				column = 0;
			next = min(((column*height)+row), currentMenu->numitems-1);
			M_RawSetItemOn(currentMenu, next);
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
			M_RawSetItemOn(currentMenu, next);
			break;

		case KEY_DOWNARROW:
			S_StartSound(NULL, sfx_menu1);
			row = (row+1) % height;
			if (((column*height)+row) >= currentMenu->numitems)
				row = 0;
			next = min(((column*height)+row), currentMenu->numitems-1);
			M_RawSetItemOn(currentMenu, next);
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
			M_RawSetItemOn(currentMenu, next);
			break;

		case KEY_ENTER:
			if (currentMenu->menuitems[itemOn].argument == 2)
			{
				//S_StartSound(NULL, sfx_s26d);
				if (!shitsfree)
				{
					shitsfree = TICRATE;
					S_StartSound(NULL, sfx_itfree);
				}
			}
			else if (currentMenu->menuitems[itemOn].argument == 1)
			{
				INT32 v = K_ItemResultEnabled(K_GetKartResult("sneaker")) ? 1 : 0;
				S_StartSound(NULL, sfx_s1b4);
				for (UINT8 i = 0; i < numkartresults; i++)
				{
					if (kartresults[i].cvar->value == v)
						CV_AddValue(kartresults[i].cvar, 1);
				}
			}
			else if (result != NULL)
			{
				if (result->type == KITEM_SUPERRING && cv_kartrings.value == 0)
				{
					S_StartSound(NULL, sfx_s231);
				}
				else
				{
					S_StartSound(NULL, sfx_s1ba);
					CV_AddValue(result->cvar, 1);
				}
			}
			break;

		case KEY_BACKSPACE:
			if (result != NULL && kartitems[result->type].altcvar != NULL)
			{
				if (K_ShowAltItemIcon(result->type, false))
				{
					S_StartSound(NULL, sfx_s3kb7);
				}
				else
				{
					S_StartSound(NULL, sfx_mds0a);
					fliptic[result->type] = FLIPTIC_TIME;
				}

				CV_AddValue(kartitems[result->type].altcvar, 1);
			}
			break;

		default:
			return false;
	}
	return true;
}

void M_ReturnToConnectMenu(void)
{
	M_ClearMenus(false);
	D_ResetGameState();
	M_StartControlPanel();
	M_EnterMenu(MN_MP_MAIN, true, 0);
}

INT32 MR_EnterViewServer(INT32 choice)
{
	(void)choice;
	char *servermap = serverlist[joinnode].info.maptitle;
	char *servername = serverlist[joinnode].info.servername;
	const char *speedstring = "Auto";
	UINT32 ping = serverlist[joinnode].info.time;
	UINT8 kartvars = serverlist[joinnode].info.kartvars;

	switch (kartvars & SV_SPEEDMASK)
	{
		case KARTSPEED_EASY:
			speedstring = "Easy";
			break;
		case KARTSPEED_NORMAL:
			speedstring = "Normal";
			break;
		case KARTSPEED_HARD:
			speedstring = "Hard";
			break;
		case KARTSPEED_EXPERT:
			speedstring = "Expert";
			break;
	}

	const char *mapname = G_BuildMapName(G_LevelTitleToMapNum(serverlist[joinnode].info.maptitle)+1);
	M_SetItemPatch(MN_VIEWSERVER, "THUMBNAIL", mapname ? mapname : "");
	M_SetItemText(MN_VIEWSERVER, "SERVERNAME", servername);
	M_SetItemText(MN_VIEWSERVER, "SERVERPING", va("Ping: %c%ums", ping < 128 ? '\x83' : ping < 256 ? '\x82' : '\x85', ping));
	M_SetItemText(MN_VIEWSERVER, "SERVERMAP", servermap);
	M_SetItemText(MN_VIEWSERVER, "SERVERGAMETYPE", va("%s: %s", serverlist[joinnode].info.gametypename, speedstring));

	if (fileneedednum > 0)
	{
		M_SetItemText(MN_VIEWSERVER, "SERVERADDON", va("\x82%d Addon%s", fileneedednum, fileneedednum > 1 ? "s" : ""));
	}
	else
	{
		M_SetItemText(MN_VIEWSERVER, "SERVERADDON", "Vanilla");
	}

	if (serverlist[joinnode].info.cheatsenabled)
	{
		M_SetItemText(MN_VIEWSERVER, "SERVERCHEATS", "Cheats");
	}

	M_SetItemVisible(MN_VIEWSERVER, "SERVERCHEATS", serverlist[joinnode].info.cheatsenabled);

	if (kartvars & SV_DEDICATED)
	{
		M_SetItemText(MN_VIEWSERVER, "SERVERDEDICATED", "Dedicated Server");
	}
	else
	{
		M_SetItemText(MN_VIEWSERVER, "SERVERDEDICATED", "Listen Server");
	}

	return true;
}

INT32 MR_QuitViewServer(INT32 choice)
{
	(void)choice;

	ChangeClientMode(CL_ABORTED);
	return true;
}

static boolean viewserver_showaddons = false;
static INT32 viewserver_scroll = 0;
static INT16 viewserver_animcount = 8;

INT32 MR_HandleViewServer(INT32 choice)
{
	switch (choice)
	{
		case KEY_ENTER:
			S_StartSound(NULL, sfx_menu1);
			ChangeClientMode(CL_CHECKFILES);
			ChangeServMusic(SERVMUS_2, true,true);
			M_ClearMenusEx(false, true);
			return true;
			break;

		case KEY_SPACE:
			S_StartSound(NULL, sfx_menu1);
			viewserver_showaddons = !viewserver_showaddons;
			return true;
			break;

		case KEY_UPARROW:
		case KEY_DOWNARROW:
			if (viewserver_showaddons && fileneedednum > 22)
			{
				if (choice == KEY_UPARROW)
					viewserver_scroll -= 1;
				else if (choice == KEY_DOWNARROW)
					viewserver_scroll += 1;
				S_StartSound(NULL, sfx_menu1);
				if (viewserver_scroll < 0) viewserver_scroll = 0;
				if (viewserver_scroll >= fileneedednum - 22) viewserver_scroll = fileneedednum -22;
				return true;
			}
			break;

		default:
			break;
	}

	return false;
}

void MD_DrawViewServer(void)
{
	MD_DrawGenericMenu();

	if (!viewserver_showaddons)
	{
		INT32 i;
		INT32 count = 0;
		INT32 x = 14;
		INT32 y = 84;
		char player_name[MAXPLAYERNAME+1];
		UINT8 playeramount = serverlist[joinnode].info.numberofplayer;
		UINT8 maxplayer = serverlist[joinnode].info.maxplayer;

		V_DrawString(12, 74, V_ALLOWLOWERCASE|highlightflags, "Players");
		V_DrawRightAlignedString(BASEVIDWIDTH - 12, 74, V_ALLOWLOWERCASE|highlightflags, va("%i / %i", playeramount, maxplayer));

		if (playeramount > 0)
		{
			for (i = 0; i < MAXPLAYERINFO; i++)
			{
				if (playerinfo[i].num < 255)
				{
					if (playeramount <= 16)
					{
						UINT16 skinum = playerinfo[i].skin;
						INT32 flags = 0;

						if (R_SkinAvailable(skins[skinum].name) == -1)
						{
							INT32 statuscolor = 1;

							if (playerinfo[i].team == 0) { statuscolor = 112; } // playing
							if (playerinfo[i].team == 1) { statuscolor = 35; } // ctf red team
							if (playerinfo[i].team == 2) { statuscolor = 152; } // ctf blue team
							if (playerinfo[i].team == 255) { statuscolor = 16; flags |= V_40TRANS; } // spectator or non-team

							V_DrawFill(x, y+5, 16, 16, statuscolor);
						}
						else
						{
							patch_t *facerank = faceprefix[skinum][FACE_RANK];
							UINT8 *colormap = R_GetTranslationColormap(skinum, skins[skinum].prefcolor, GTC_MENUCACHE);

							if (playerinfo[i].team == 255)
							{
								colormap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_GREY, GTC_MENUCACHE);
								flags |= V_40TRANS;
							}


							V_DrawFixedPatch((x + facerank->leftoffset)<<FRACBITS, (y + 5 + facerank->topoffset)<<FRACBITS, FRACUNIT, flags, facerank, colormap);
						}

						strlcpy(player_name, playerinfo[i].name, 12);
						V_DrawThinString(x + 20, y + 3 + 5, flags|V_ALLOWLOWERCASE|V_6WIDTHSPACE, player_name);

						y += 22;
						count++;
						if ((count == 4) || (count == 8) || (count == 12))
						{
							x += 74;
							y = 84;
						}
					}
					else
					{
						INT32 statuscolor = 1;

						strlcpy(player_name, playerinfo[i].name, 12);
						V_DrawThinString(x + 10, y, V_ALLOWLOWERCASE|V_6WIDTHSPACE, player_name);

						if (playerinfo[i].team == 0) { statuscolor = 112; } // playing
						if (playerinfo[i].team == 1) { statuscolor = 35; } // ctf red team
						if (playerinfo[i].team == 2) { statuscolor = 152; } // ctf blue team
						if (playerinfo[i].team == 255) { statuscolor = 16; } // spectator or non-team

						V_DrawFill(x, y, 7, 7, 31);
						V_DrawFill(x, y, 6, 6, statuscolor);

						y += 9;
						count++;
						if ((count == 11) || (count == 22))
						{
							x += 104;
							y = 84;
						}
					}
				}
			}
		}
	}
	else
	{
		INT32 i;
		INT32 count = 0;
		INT32 x = 14;
		INT32 y = 84;
		boolean small_mode = fileneedednum <= 11;
		char file_name[MAX_WADPATH+1];

		V_DrawString(12, 74, V_ALLOWLOWERCASE|highlightflags, "Addons");

#define maxcharlen (20 + 3) // 3 for the 3 dots
#define charsonside 10

		for (i = viewserver_scroll; i < fileneedednum; i++)
		{
			if (i & 1)
				V_DrawFill(x,y-1,
					(small_mode) ? 292 : 146, 9,
					156
				);

			fileneeded_t addon_file = fileneeded[i];
			strncpy(file_name, addon_file.filename, MAX_WADPATH);
			if ((UINT8)(strlen(file_name)+1) > maxcharlen && !small_mode)
				V_DrawThinString(x, y, V_ALLOWLOWERCASE|V_6WIDTHSPACE,
					va("\x82[#%d]\x80: %.*s...%s",i+1, charsonside, file_name, file_name+strlen(file_name)-((charsonside+1)))
				);
			else
			{
				V_DrawThinString(x, y, V_ALLOWLOWERCASE|V_6WIDTHSPACE,
					va("\x82[#%d]\x80: %s",i+1, file_name)
				);

				// make sure we have the space
				if (small_mode)
				{
					float file_size = ((float)addon_file.totalsize);
					UINT8 size_mode = 0; // regular bytes
					//in megabytes
					if (file_size >= (1024.0f * 1024.0f))
					{
						size_mode = 1;
						file_size /= (1024.0f * 1024.0f);
					}
					// KB
					else if (file_size >= 1024.0f)
					{
						size_mode = 2;
						file_size /= 1024.0f;
					}

					V_DrawRightAlignedThinString(x + 292,
						y, highlightflags|V_ALLOWLOWERCASE,
						// "~" since its approx this size, we mightve lost some
						// accuracy from only having 4 bytes carry the size
						va("(~%.1f%s)", file_size, size_mode == 0 ? "b" : (size_mode == 2 ? "kb" : "mb"))
					);
				}
			}

			y += 9;
			count++;
			if (count == 11)
			{
				x = BASEVIDWIDTH/2;
				y = 84;
			}
			// Cannot draw any more
			if (count == 22)
				break;
		}

		// reiterate again!!! Yes!!! Yay!!!
		{
			UINT32 totalsize = 0;
			UINT8 size_mode = 0; // regular bytes

			for (INT32 j = 0; j < fileneedednum; j++)
				totalsize += fileneeded[j].totalsize;
			totalsize = (float)totalsize;

			//in megabytes
			if (totalsize >= (1024.0f * 1024.0f))
			{
				size_mode = 1;
				totalsize /= (1024.0f * 1024.0f);
			}
			// KB
			else if (totalsize >= 1024.0f)
			{
				size_mode = 2;
				totalsize /= 1024.0f;
			}

			V_DrawRightAlignedThinString(BASEVIDWIDTH - 22, 74,
				V_ALLOWLOWERCASE|highlightflags,
				va("~%.1f%s total", (float)totalsize, size_mode == 0 ? "b" : (size_mode == 2 ? "kb" : "mb"))
			);
		}

		// draw the little arrows
		if (fileneedednum >= 22)
		{
			// up arrow
			if (viewserver_scroll)
				V_DrawRightAlignedThinString(BASEVIDWIDTH - 12,
					74 - (viewserver_animcount/5), highlightflags,
					"\x1A"
				);

			if (viewserver_scroll != fileneedednum-22)
				V_DrawRightAlignedThinString(BASEVIDWIDTH - 12,
					y-9 + (viewserver_animcount/5), highlightflags,
					"\x1B"
				);
		}
	}

#undef maxcharlen
#undef charsonside

	// Buttons
	M_SetItemVisible(MN_VIEWSERVER, "TOGGLEBUTTON1", viewserver_showaddons);
	M_SetItemVisible(MN_VIEWSERVER, "TOGGLEBUTTON2", !viewserver_showaddons);
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
			I_UpdateSound();
			I_FinishUpdate(); // Update the screen with the image Tails 06-19-2001
			I_Sleep(cv_sleep.value);
			I_UpdateTime(cv_timescale.value);
		}
	}
	I_Quit();
}

INT32 MR_QuitSRB2(INT32 choice)
{
	// We pick index 0 which is language sensitive, or one at random,
	// between 1 and maximum number.
	(void)choice;
	M_StartMessage(quitmsg[M_RandomKey(NUM_QUITMESSAGES)], M_QuitResponse, MM_YESNO);
	return true;
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

#ifdef HAVE_DISCORDRPC
static const tic_t confirmLength = 3*TICRATE/4;
static tic_t confirmDelay = 0;
static boolean confirmAccept = false;

INT32 MR_HandleDiscordRequests(INT32 choice)
{
	switch (choice)
	{
		case KEY_UPARROW:
		case KEY_DOWNARROW:
			break;

		case KEY_ENTER:
			if (confirmDelay > 0)
				break;
			Discord_Respond(discordRequestList->userID, DISCORD_REPLY_YES);
			confirmAccept = true;
			confirmDelay = confirmLength;
			S_StartSound(NULL, sfx_s3k63);
			break;

		case KEY_ESCAPE:
			if (confirmDelay > 0)
				break;
			Discord_Respond(discordRequestList->userID, DISCORD_REPLY_NO);
			confirmAccept = false;
			confirmDelay = confirmLength;
			S_StartSound(NULL, sfx_s3kb2);
			break;

		default:
			return false;
	}

	return true;
}

static const char *M_GetDiscordName(discordRequest_t *r)
{
	if (r == NULL)
		return "";

	if (cv_discordstreamer.value)
		return DRPC_HideUsername(r->username);

	return r->username;
}

void MD_DrawDiscordRequests(void)
{
	discordRequest_t *curRequest = discordRequestList;
	UINT8 *colormap;
	patch_t *hand = NULL;
	boolean removeRequest = false;

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

	MD_DrawGenericMenu();

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
			M_SetItemDisabled(MN_MPAUSE, "DISCORDREQUESTS", true);

			M_ExitMenu();
			if (menustack[0] == MN_MPAUSE)
				M_SetItemOn(MN_MPAUSE, "CONTINUE");

			return;
		}
	}
}
#endif
