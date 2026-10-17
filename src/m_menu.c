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

#include "command.h"
#include "d_clisrv.h"
#include "doomstat.h"
#include "g_demo.h"
#include "g_state.h"
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
#include "r_voicepreference.hpp"
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
#include "sdl/sdlmain.h" // JOYSTICK_HOTPLUG
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

#include "k_director.h"

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
static levellistmode_e levellistmode_ta = LLM__MAX;

menu_t menudefs[MAXMENUTYPES]; // array of all menudefs
uint16_t nummenutypes;
menupaneldef_t menupanels[MAXMENUPANELS];
size_t nummenupanels;
menuitemstyle_t menuitemstyles[MAXMENUITEMSTYLES];
size_t nummenuitemstyles;
menutype_t menustack[NUMMENULEVELS]; // stack of active menus, [0] == current menu
menutype_t restoremenustack[NUMMENULEVELS]; // stack of active menus, [0] == current menu
static menu_t *currentMenu; // current menudef
static size_t numcurpanels;
static menupanelinst_t *curpanels; // pointers to instantiated menu panels
static size_t panelOn;
static int16_t skullAnimCounter = 10; // skull animation counter
static tic_t menutimer = 0, cursortimer = 0; // tics since menu change/menu movement

int32_t menuKey = -1; // keyboard key pressed for menu
menucmd_t menucmd[MAXSPLITSCREENPLAYERS] = {0};

menutyping_t menutyping = {0};

// keyboard layouts
int16_t virtualKeyboard[5][NUMVIRTUALKEYSINROW] = {

	{'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', KEY_BACKSPACE, 1},
	{'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '-', '='},
	{'a', 's', 'd', 'f', 'g', 'h', 'i', 'j', 'k', 'l', '[', ']'},
	{'z', 'x', 'c', 'v', 'b', 'n', 'm', 0, ',', '.', ';', '\''},
	{KEY_RSHIFT, 1, 1, KEY_SPACE, 1, 1, 1, 1, '/', '\\', KEY_ENTER, 1}
};

int16_t shift_virtualKeyboard[5][NUMVIRTUALKEYSINROW] = {

	{'!', '@', '#', '$', '%', '^', '&', '*', '(', ')', KEY_BACKSPACE, 1},
	{'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '_', '+'},
	{'A', 'S', 'D', 'F', 'G', 'H', 'I', 'J', 'K', 'L', '{', '}'},
	{'Z', 'X', 'C', 'V', 'B', 'N', 'M', 0, '<', '>', ':', '\"'},
	{KEY_RSHIFT, 1, 1, KEY_SPACE, 1, 1, 1, 1, '?', '|', KEY_ENTER, 1}
};

messagebox_t messagebox = {0};
messagebox_t messageboxqueue[NUMMENULEVELS] = {0};
uint8_t messageboxqueuesize = 0;

static  uint8_t setupcontrolplayer;
static  int32_t   (*setupcontrols)[MAXINPUTMAPPING];  // pointer to the gamecontrols of the player being edited

// shhh... what am I doing... nooooo!
static int32_t vidm_testingmode = 0;
static int32_t vidm_previousmode;

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
	uint8_t color;
	dboolean available;
} levelselectmap_t;

typedef struct
{
	char header[MAXMAPNAME];
	dboolean wide;
	uint16_t cupid;
	uint8_t numcolumns;
	levelselectmap_t maps[MAPSPERROW];
} levelselectrow_t;

static struct levelselect_t
{
	uint8_t row;
	uint8_t column;
	uint8_t highlight;
	int32_t namescroll;
	fixed_t offsetx, offsety;

	int16_t randomrow, randomcol;
	int16_t returnto;
	tic_t selectdelay;

	uint8_t numrows;
	levelselectrow_t *rows;
} levelselect = {0};

#define lsmapwidth (80*FRACUNIT)
#define lsmapheight (50*FRACUNIT)
#define lsmapnameheight (8*FRACUNIT)
#define lshseperation (lsmapwidth - 25*FRACUNIT)
#define lshseperationcup (lsmapwidth/2 + 2*FRACUNIT)
#define lsbasevseperation (3*(lsmapheight + lsmapnameheight)/4 + FRACUNIT)
#define lsheadingheight (28*FRACUNIT)
#define getheadingoffset(row) (levelselect.rows[row].header[0] ? lsheadingheight : 0)
#define lsvseperation(row) (lsbasevseperation + getheadingoffset(row))

#define lsbasex (BASEVIDWIDTH*FRACUNIT/2 - lshseperation*(MAPSPERROW-1)/2)
#define lsbasey (BASEVIDHEIGHT*FRACUNIT/2 - 4*FRACUNIT)

// Multiplayer
static void M_ConnectMenu(int32_t choice);

static patch_t *addonsp[NUM_EXT+5];

#define numaddonsshown 4

// Replay hut
static uint8_t playback_enterheld = 0; // horrid hack to prevent holding the button from being extremely fucked

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

/*FNONULL*/ menupaneldef_t *M_AllocateMenuPanelDef(const char *name)
{
	menupaneldef_t *pd = &menupanels[nummenupanels++];
	DEH_Link(name, &pd->info, &menunames);
	pd->lineheight = 8;
	return pd;
}

/*RNONULL INONULL1(1)*/ menuitemstyle_t *M_AllocateMenuItemStyle(const char *name, const menuitemstyle_t *parent)
{
	menuitemstyle_t *style = &menuitemstyles[nummenuitemstyles++];
	DEH_Link(name, &style->info, &menunames);

	if (parent != NULL)
	{
		style->parent = parent - menuitemstyles;
		memcpy(style->parts, parent->parts, sizeof(style->parts));
		memcpy(style->partenabled, parent->partenabled, sizeof(style->partenabled));
		for (size_t i = 0; i < MAXMENUITEMPARTS; i++)
		{
			style->parts[i].fixedstring = style->parts[i].fixedstring2 = NULL; // not my memory!
			style->parts[i].source = style->parts[i].source2 = ITS_DERIVED;
		}
	}

	return style;
}

/*ANONULL*/ static menupanelinst_t *M_CheckPanel(const char *name)
{
	size_t i;
	for (i = 0; i < numcurpanels; i++)
		if (fastcmp(name, curpanels[i].name))
			return &curpanels[i];
	return NULL;
}

/*FNONULL*/ static menupanelinst_t *M_GetPanel(const char *name)
{
	menupanelinst_t *pi = M_CheckPanel(name);
	if (pi == NULL)
		I_Error("Panel instance %s not found", name);
	return pi;
}

/*FNONULL*/ static menupaneldef_t *M_GetDefFromInst(const menupanelinst_t *pi)
{
	return &menupanels[pi->defnum];
}

/*FNONULL*/ static menuitem_t *M_AppendMenuItem(menupanelinst_t *pi, fixed_t x, fixed_t y, int32_t status)
{
	pi->items = Z_Realloc(pi->items, sizeof(menuitem_t)*(pi->numitems+1), PU_STATIC, NULL);
	menuitem_t *item = &pi->items[pi->numitems++];
	item->x = x;
	item->y = y;
	item->status = status;
	return item;
}

/*ANONULL*/ static void M_ClearPanelItems(menupanelinst_t *pi)
{
	size_t numitems = pi->numitems;
	pi->numitems = pi->itemon = 0;

	if (pi->items == NULL)
		return;

	while (numitems-- > 0)
	{
		menuitem_t *item = &pi->items[numitems];
		Z_Free(item->text);
		Z_Free(item->patch);
		Z_Free(item->tooltip);
		Z_Free(item->extra);
	}

	Z_Free(pi->items);
	pi->items = NULL;
}

// menus now use short names rather than hardcoded indices to identify menuitems
/*ANONULL*/ menuitem_t *M_CheckMenuItemDef(const menupaneldef_t *pd, const char *name)
{
	uint16_t i;
	uint32_t hash = HASH32(name, strlen(name));

	for (i = 0; i < pd->numitems; i++) {
		if (hash != pd->items[i].info.namehash)
			continue;
		if (fastcmp(name, strbuf_get(menunames, pd->items[i].info.nameofs)))
			return &pd->items[i];
	}
	return NULL;
}

/*ANONULL*/ static menuitem_t *M_CheckMenuItem(const menupanelinst_t *pi, const char *name)
{
	uint16_t i;
	uint32_t hash = HASH32(name, strlen(name));

	for (i = 0; i < pi->numitems; i++) {
		if (hash != pi->items[i].info.namehash)
			continue;
		if (fastcmp(name, strbuf_get(menunames, pi->items[i].info.nameofs)))
			return &pi->items[i];
	}
	return NULL;
}

/*FNONULL*/ menuitem_t *M_GetMenuItemDef(const menupaneldef_t *pd, const char *name)
{
	menuitem_t *item = M_CheckMenuItemDef(pd, name);
	if (!item)
		I_Error("Menu panel %s has no item named '%s'", DEH_MenuPanelName(pd), name);
	return item;
}

/*FNONULL*/ static menuitem_t *M_GetMenuItem(const menupanelinst_t *pi, const char *name)
{
	menuitem_t *item = M_CheckMenuItem(pi, name);
	if (!item)
		I_Error("Menu panel %s (instance %s) has no item named '%s'", DEH_MenuPanelName(M_GetDefFromInst(pi)), pi->name, name);
	return item;
}

static uint16_t M_GetMenuIndexDef(const menupaneldef_t *pd, const char *name)
{
	return M_GetMenuItemDef(pd, name) - pd->items;
}

static uint16_t M_GetMenuIndex(const menupanelinst_t *pi, const char *name)
{
	return M_GetMenuItem(pi, name) - pi->items;
}

static fixed_t M_ItemWidth(const menupanelinst_t *pi, size_t index)
{
	// TODO every other style
	return V_StringWidth(pi->items[index].text, pi->vflags|MENUCAPS)*FRACUNIT;
}

static dboolean M_ItemSelectable(const menuitem_t *item)
{
	return (item->status & IT_INTERACT) && !(item->status & (IT_HIDDEN|IT_GRAYEDOUT|IT_SECRET));
}

// TODO this ought to be controlled by the item argument...
static uint32_t M_StringCvarLength(consvar_t *cv)
{
	if (cv == &cv_dummyip)
		return 28;
	else if (cv == &cv_dummyname || cv == &cv_playername[0] || cv == &cv_playername[1] || cv == &cv_playername[2] || cv == &cv_playername[3])
		return MAXPLAYERNAME + 1;
	else if (cv == &cv_dummyreplaysave)
		return 65;
	else if (cv == &cv_dummyreplaysearch)
		return MAXREPLAYQUERY;
	else
		return MAXSTRINGLENGTH;
}

// an array of macros for getting/setting menuitem properties
#define M_IsItemOn(f, n)              ((f)->itemon == M_GetMenuIndex(f, n))
#define M_SetItemOn(f, n)             (M_RawSetItemOn(f, M_GetMenuIndex(f, n)))
#define M_SetItemCvar(f, n, v)        (M_GetMenuItem(f, n)->cvar = v)
#define M_SetItemArgument(f, n, v)    (M_GetMenuItem(f, n)->argument = v)
#define M_SetItemX(f, n, v)           (M_GetMenuItem(f, n)->x = v)
#define M_GetItemX(f, n)              (M_GetMenuItem(f, n)->x)
#define M_GetItemY(f, n)              (M_GetMenuItem(f, n)->y)

static void M_RawSetItemOn(menupanelinst_t *pi, uint16_t index)
{
	size_t i;
	uint16_t old = pi->itemon;
	pi->itemon = max(0, min(index, pi->numitems - 1));

	if (pi->numitems == 0)
		return;

	if (old != pi->itemon)
		cursortimer = 0;

	// the curent item can be disabled,
	// this code go up until an enabled item found
	if (!M_ItemSelectable(&pi->items[pi->itemon]))
	{
		for (i = 0; i < pi->numitems; i++)
		{
			if (M_ItemSelectable(&pi->items[i]))
			{
				pi->itemon = i;
				break;
			}
		}
	}
}

static void M_ChangeItemStatus(menupanelinst_t *pi, const char *name, menuitemflags_t flag, dboolean cond)
{
	if (cond)
		M_GetMenuItem(pi, name)->status |= flag;
	else
		M_GetMenuItem(pi, name)->status &= ~flag;
}

static void M_SetItemTextRaw(menuitem_t *item, const char *string)
{
	if (item->text)
		Z_Free(item->text);
	item->text = Z_StrDup(string);
}

static void M_SetItemText(const menupanelinst_t *pi, const char *name, const char *string)
{
	menuitem_t *item = M_GetMenuItem(pi, name);
	M_SetItemTextRaw(item, string);
}

static void M_SetItemPatchRaw(menuitem_t *item, const char *string)
{
	if (item->patch)
		Z_Free(item->patch);
	item->patch = Z_StrDup(string);
}

static void M_SetItemPatch(const menupanelinst_t *pi, const char *name, const char *string)
{
	menuitem_t *item = M_GetMenuItem(pi, name);
	M_SetItemPatchRaw(item, string);
}

static void M_SetItemTooltipRaw(menuitem_t *item, const char *string)
{
	if (item->tooltip)
		Z_Free(item->tooltip);
	item->tooltip = Z_StrDup(string);
}

/* static void M_SetItemTooltip(const menupanelinst_t *pi, const char *name, const char *string)
{
	menuitem_t *item = M_GetMenuItem(pi, name);
	M_SetItemTooltipRaw(item, string);
} */

static void M_SetItemExtraRaw(menuitem_t *item, const char *string)
{
	if (item->extra)
		Z_Free(item->extra);
	item->extra = Z_StrDup(string);
}

static void M_SetItemExtra(const menupanelinst_t *pi, const char *name, const char *string)
{
	menuitem_t *item = M_GetMenuItem(pi, name);
	M_SetItemExtraRaw(item, string);
}

#define M_SetItemVisible(f, n, c)  M_ChangeItemStatus(f, n, IT_HIDDEN, !(c))
#define M_SetItemDisabled(f, n, c) M_ChangeItemStatus(f, n, IT_GRAYEDOUT, c)
#define M_SetItemSecret(f, n, c)   M_ChangeItemStatus(f, n, IT_SECRET, c)

// bruh...
static uint32_t M_ServersPerPage(void)
{
	const menupaneldef_t *pd = DEH_FindMenuPanel("MP_CONNECT");
	int32_t spp = M_GetMenuIndexDef(pd, "LINEMAX") - M_GetMenuIndexDef(pd, "LINE1") + 1;
	if (spp < 1 || spp >= MAXSERVERLIST)
		I_Error("Broken server connection menu");
	return (uint32_t)spp;
}

// TODO: port cup->realname
static const char *CupName(const cupheader_t *cup, dboolean suffix)
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

static CV_PossibleValue_t debugpanels_cons_t[] = {{0, "Off"}, {1, "No render"}, {2, "No drawroutines"}, {0, NULL}};
consvar_t cv_debugpanels = CVAR_INIT ("debugpanels", "Off", 0, debugpanels_cons_t, NULL);

// How much replays are checked per frame when using search
consvar_t cv_replaysearchrate = CVAR_INIT ("replaysearchrate", "1000", CV_SAVE, CV_Natural, NULL);

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

int8_t dummycolorplayer = 0;
static void SetDummyColorPlayer(void)
{
	G_SetPlayerControllerIndicatorColor(dummycolorplayer, (uint16_t)cv_dummycolor.value);
}

consvar_t cv_dummycolor = CVAR_INIT ("dummycolor", "0", CV_HIDEN|CV_CALL|CV_NOINIT, dummycolor_cons_t, SetDummyColorPlayer);

static CV_PossibleValue_t dummyserverpage_cons_t[] = {{0, "MIN"}, {0, "MAX"}, {0, NULL}};
consvar_t cv_dummyserverpage = CVAR_INIT ("dummyserverpage", "0", CV_HIDEN, dummyserverpage_cons_t, NULL);

consvar_t cv_menucaps = CVAR_INIT ("menucaps", "Off", CV_SAVE, CV_OnOff, NULL);

static tic_t playback_last_menu_interaction_leveltime = 0;

consvar_t cv_debugvirtualkeyboard = CVAR_INIT ("debugvirtualkeyboard", "Off", 0, CV_OnOff, NULL);

static void AddonSearch(void);

consvar_t cv_search_oldinput = CVAR_INIT ("search_oldinput", "Off", CV_SAVE, CV_OnOff, NULL);

consvar_t cv_dummyaddonsearch = CVAR_INIT ("dummyaddonsearch", "", CV_HIDEN|CV_CALL|CV_NOINIT, NULL, AddonSearch);

static void ReplaySearch(void);

consvar_t cv_dummyreplaysearch = CVAR_INIT ("dummyreplaysearch", "", CV_HIDEN|CV_CALL|CV_NOINIT, NULL, ReplaySearch);

// ==========================================================================
// ALL MENU DEFINITIONS GO HERE
// ==========================================================================

//
// M_GetGametypeColor
//
// Pretty and consistent ^u^
// See also G_GetGametypeColor.
//

static int32_t highlightflags, recommendedflags, warningflags;

inline static void M_GetGametypeColor(void)
{
	int16_t gt;
	size_t i, j;
	dboolean usenewgametype = false;

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

	for (i = 0; i < numcurpanels; i++)
		for (j = 0; j < curpanels[i].numitems; j++)
			if (curpanels[i].items[j].cvar == &cv_newgametype)
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
int32_t HU_GetHighlightColor(void)
{
	M_GetGametypeColor();	// update flag colour reguardless of the menu being opened or not.
	return highlightflags;
}

fixed_t M_GetMapThumbnail(mapnum_t mapnum, patch_t **out)
{
	patch_t *patch = NULL;
	if (mapnum == NEXTMAP_INVALID)
		patch = randomlvl;
	else if (mapnum == NEXTMAP_RESERVED)
		patch = nolvl;
	else if (/*mapnum >= 0 &&*/ mapnum < nummapheaders && mapheaderinfo[mapnum])
		patch = mapheaderinfo[mapnum]->thumbnailPic;

	if (!patch)
		patch = blanklvl;

	*out = patch;

	// check width instead of height because haha big winton
	return patch->width >= 320 ? FRACUNIT : FRACUNIT*2;
}

// Options

void MP_TickOptions(menupanelinst_t *pi)
{
	if (gamestate == GS_MENU)
		menudefs[MN_OP_MAIN].fadestrength = menudefs[MN_MAIN].fadestrength;
	else
		menudefs[MN_OP_MAIN].fadestrength = menudefs[MN_MPAUSE].fadestrength;;

	M_SetItemDisabled(pi, "CUSTOM", menudefs[MN_OP_CUSTOM].numpanels == 0);
}

#ifdef HWRENDER
int32_t MR_OpenGLOptionsMenu(int32_t choice)
{
	(void)choice;

	if (rendermode != render_opengl)
	{
		M_StartMessage2("Options", M_GetText("You must be in OpenGL mode\nto access this menu.\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
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
		uint8_t numstaff = 1;
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

		snprintf(temp, sizeof(dummystaffname), " - %d", cv_dummystaff.value);
	}
}

static void Levelplatter_OnChange(void)
{
	M_UpdateLevelPlatter();
}

static void Reinitialize(void)
{
	// FIXME this is ass
	if (numcurpanels == 0)
		return;
	menupanelinst_t *pi = &curpanels[0];
	menupaneldef_t *pd = M_GetDefFromInst(pi);
	if (pd->initializer != NULL)
		pd->initializer(pi);
}

void Screenshot_option_Onchange(void)
{
	Reinitialize();
}

void Moviemode_mode_Onchange(void)
{
	Reinitialize();
}

void Addons_option_Onchange(void)
{
	Reinitialize();
}

void Moviemode_option_Onchange(void)
{
	;
}

//width now 9 so we can make moe's "2D" grid just an option in the normal grid
#define SKINGRIDWIDTH 9
#define SKINGRIDHEIGHT 6

static dboolean gridcss_active;
static int32_t gridcss_skinydrag;
static int32_t gridcss_skinmemory;
static int32_t gridcss_row;
static int32_t gridcss_column;

void Skinsort_option_Onchange(void)
{
	SortSkins();
	int32_t sortedIndex = FindSortedSkinIndex(cv_chooseskin.value);
	gridcss_row = sortedIndex % SKINGRIDWIDTH;
	gridcss_column = sortedIndex / SKINGRIDWIDTH;
	// I really need to macro this lol
	gridcss_skinydrag = CLAMP(gridcss_column - SKINGRIDHEIGHT + 1, 0, ((numskins - 1) / SKINGRIDWIDTH) - SKINGRIDHEIGHT + 1);
}

void Skinselectstyle_option_Onchange(void)
{
	Reinitialize();
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
	int32_t i;

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
	for (uint8_t i = 0; i < NUMMENULEVELS; i++)
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

	for (uint8_t i = 0; i < NUMMENULEVELS; i++)
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
			S_StopMusicCredit();
			return;
		}
		else if (menudefs[menutype].musignore)
			return;
	}
	if (target == MN_MAIN && gamestate == GS_TITLESCREEN && finalecount < 50)
	{
		S_StopMusic();
		S_StopMusicCredit();
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
	for (uint8_t i = 0; i < NUMMENULEVELS; i++)
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
	for (uint8_t i = 0; i < NUMMENULEVELS; i++)
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
	int32_t i;
	menutype_t menutype, exittype = currentMenu ? currentMenu - menudefs : MN_NONE;
	dboolean noancestor = menustack[0] == MN_NONE;
	int16_t exittag = 0, entertag = 0;
	int16_t exitwipe = -1, enterwipe = -1;
	dboolean exitbubble = true, enterbubble = true;

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

static uint8_t      multi_spr2;
static void M_GetFollowerState(void);

static void M_ResetCvar(menuitem_t *item)
{
	consvar_t *cv = item->cvar;

	if (cv == &cv_playercolor[0] || cv == &cv_playercolor[1] || cv == &cv_playercolor[2] || cv == &cv_playercolor[3] || cv == &cv_dummycolor)
	{
		int32_t skinno = R_SkinAvailable(skins[cv_chooseskin.value]->name);
		if (skinno != -1 && skincolors[skins[skinno]->prefcolor].accessible)
			CV_SetValue(cv, skins[skinno]->prefcolor);
	}
	else
		CV_Set(cv, cv->defaultvalue);
}

static void M_ChangeCvar(menuitem_t *item, int8_t direction)
{
	consvar_t *cv = item->cvar;
	int32_t amount = item->argument ? item->argument : cv->flags & CV_FLOAT ? FRACUNIT/16 : 1;
	amount *= direction;

	if (cv->flags & CV_FLOAT)
	{
		char s[20];
		float n = FIXED_TO_FLOAT(cv->value + amount);
		snprintf(s, sizeof(s), "%ld%s", (long)n, M_Ftrim(n));
		CV_Set(cv, s);
	}
	else
		CV_AddValue(cv, amount);

	if (cv == &cv_chooseskin)
		multi_spr2 = P_GetSkinSprite2(skins[cv->value], SPR2_FSTN, NULL);
	else if (cv == &cv_dummyfollower)
		M_GetFollowerState();	// update follower state
}

// lock out further input in a tic when important buttons are pressed
// (in other words -- stop bullshit happening by mashing buttons in fades)
static int32_t noFurtherInput = 0;

static void Command_Manual_f(void)
{
	if (modeattacking)
		return;
	M_StartControlPanel();
	M_EnterMenu(MN_HELP, true, 0);
	M_SetItemOn(&curpanels[panelOn], "PAGE0");
}

// arbitrary keyboard shortcuts because fuck you
static dboolean M_DemoBinds(int32_t ch)
{
	switch (ch)
	{
	case ';':	// toggle director
		MR_PlaybackToggleDirector(0);
		break;
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
static dboolean M_WipeBuffer(int32_t ch, menufunc_f *routine)
{
	if (!WipeInAction)
		return false;

	// these routines are allowed to run during wipes
	// solely to make the menus less annoying to use
	if (routine == NULL
	 || (routine == MR_SelectableClearMenus && !currentMenu->quitroutine)
	 || routine == MR_SetupMultiPlayer
	 || routine == MR_HandleSetupMultiPlayerMenu
	 || routine == MR_SetupControlsMenu
	 || routine == MR_ChangeControl
	 || routine == MR_SetLevelListMode
	 || routine == MR_HandleLevelPlatter
	)
		return false;

	noFurtherInput = ch;
	menucmd[0].wipebuffer = true;
	S_StartSound(NULL, sfx_kc50);
	return true;
}

// use this when routine being NULL isn't a free pass
static int32_t MR_Dummy(int32_t ch) { (void)ch; return true; }

static fixed_t M_GetItemAbsY(const menupanelinst_t *pi, size_t index);
static int64_t M_GetItemAbsY_64(const menupanelinst_t *pi, size_t index);

static dboolean M_GamestateCanOpenMenu(void)
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

// Prevent go back+systemmenu on the same key from conflicting.
int8_t pausehack = 0;

//
// M_Responder
//
dboolean M_Responder(event_t *ev)
{
	if (dedicated
		|| (demo.playback && demo.title)
		|| M_GamestateCanOpenMenu() == false)
	{
		return false;
	}

	if (gamestate == GS_TITLESCREEN && finalecount < TICRATE)
		return false;

	if (noFurtherInput)
	{
		// Ignore input after enter/escape/other buttons
		// (but still allow shift keyup so caps doesn't get stuck)
		return false;
	}

	if (ev->type == ev_keydown && ev->data1 > 0 && ev->data1 < NUMKEYS)
	{
		// Record keyboard presses
		menuKey = ev->data1;
	}

	if (messagebox.active && messagebox.messagetype == MM_EVENTHANDLER)
	{
		if (ev->type == ev_mouse || ev->type == ev_gyroscope || ev->type == ev_accelerometer)
			return false;

		if (menucmd[0].delay > 0)
			return true;

		if (messagebox.callback.handler && ((ev->type != ev_joystick) || 
			(ev->type == ev_joystick && !G_AxisInDeadzone(setupcontrolplayer-1, ev))))
		{
			messagebox.callback.handler(ev);
		}

		M_SetMenuDelay(0);
		return true;
	}

	// Handle menu handling in-game.
	if (!menustack[0])
	{
		noFurtherInput = -1;

		// The Fx keys.
		switch (menuKey)
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
				M_SetItemOn(&curpanels[0], "SOUND");
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
		}

		if (G_PlayerInputDown(0, gc_systemmenu, true, DEADZONE_BUTTON) == true || menuKey == KEY_ESCAPE)
		{
			if (chat_on)
			{
				HU_clearChatChars();
				chat_on = false;
			}
			else if (pausehack == 0)
			{
				M_StartControlPanel();
			}
			return true;
		}

		noFurtherInput = 0; // turns out we didn't care
		return false;
	}

	// We're in the menu itself now.
	// M_Ticker will take care of the rest.
	return true;
}

// special responder for demos
dboolean M_DemoResponder(event_t *ev)
{
	dboolean eatinput = false;	// :omnom:

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

static void M_DrawMenuPanel(menupanelinst_t *pi, dboolean selected);
static void M_DrawSplitText(const char *text, int32_t x, int32_t y, int32_t option, int32_t alpha);

static int SortPanels(const void *a, const void *b)
{
	const menupanelinst_t *p1 = &curpanels[*(const uint16_t *)a], *p2 = &curpanels[*(const uint16_t *)b];
	int c = p1->layer - p2->layer;
	return c != 0 ? c : p1 - p2;
}

#define TYPEFADETIME 4
#define TYPEFADETIMEHALF TYPEFADETIME/2

// Draws the typing submenu
static void M_DrawMenuTyping(void)
{
	const uint8_t pid = 0;

	int32_t i, j;

	int32_t x, y;

	char buf[8];	// We write there to use drawstring for convenience.

	V_DrawFadeScreen(31, (menutyping.menutypingfade+11)/2);

	// Draw the string we're editing at the top.

	const int32_t boxwidth = (8*(MAXSTRINGLENGTH + 1)) + 7;
	x = (BASEVIDWIDTH - boxwidth)/2;
	y = 80;
	if (menutyping.menutypingfade < TYPEFADETIMEHALF)
		y += floor(pow(2, (double)(TYPEFADETIMEHALF - menutyping.menutypingfade)));
	else
		y += (TYPEFADETIMEHALF-menutyping.menutypingfade);

	menuitem_t *item = menutyping.item;

	dboolean scrolltextbox = false;
	int32_t length = M_StringCvarLength(item->cvar);

	if (length > MAXSTRINGLENGTH)
	{
		length = MAXSTRINGLENGTH;
		scrolltextbox = true;
	}

	// Draw the string we're editing at the top.
	uint16_t offset = (length + 1) * 8;
	M_DrawTextBox((BASEVIDWIDTH-offset)/2 - 8, y + 12 - 8, length, 1, 0);

	if (scrolltextbox)
		M_DrawTextInputScroll((BASEVIDWIDTH-offset)/2, y + 12, menutyping.textinput, V_ALLOWLOWERCASE, (MAXSTRINGLENGTH-1)*8);
	else
		M_DrawTextInput((BASEVIDWIDTH-offset)/2, y + 12, menutyping.textinput, V_ALLOWLOWERCASE);

	if (item->text)
	{
		V_DrawThinString((BASEVIDWIDTH-offset)/2 - 8, y + 12 - 8 - 10, highlightflags|V_6WIDTHSPACE|V_ALLOWLOWERCASE, item->text);
	}

	const int32_t buttonwidth = ((boxwidth + 1)/NUMVIRTUALKEYSINROW);
#define BUTTONHEIGHT (11)

	// Now the keyboard itself
	x += 5;
	int32_t returnx = x;

	if (menutyping.menutypingfade > TYPEFADETIMEHALF)
	{
		y += 26;

		if (menutyping.menutypingfade < TYPEFADETIME)
		{
			y += floor(pow(2, (double)(TYPEFADETIME - menutyping.menutypingfade))); // double yoffs for animation
		}

		int32_t tempkeyboardx = menutyping.keyboardx;

		while (virtualKeyboard[menutyping.keyboardy][tempkeyboardx] == 1
		&& tempkeyboardx > 0)
			tempkeyboardx--;

		for (i = 0; i < 5; i++)
		{
			j = 0;
			while (j < NUMVIRTUALKEYSINROW)
			{
				int32_t mflag = V_ALLOWLOWERCASE;
				int16_t c = virtualKeyboard[i][j];

				int32_t buttonspacing = 1;

				uint8_t col = 27;

				int32_t arrowoffset = 0;

				while (j + buttonspacing < NUMVIRTUALKEYSINROW
				&& virtualKeyboard[i][j + buttonspacing] == 1)
				{
					buttonspacing++;
				}

				if (menutyping.keyboardshift ^ menutyping.keyboardcapslock)
					c = shift_virtualKeyboard[i][j];

				if (i < 4 && j < NUMVIRTUALKEYSINROW-2)
				{
					col = 25;
				}

				dboolean canmodifycol = (menutyping.menutypingfade == TYPEFADETIME);

				if (c == KEY_BACKSPACE)
				{
					arrowoffset = 1;
					buf[0] = '\x1C'; // left arrow
					buf[1] = '\0';

					if (canmodifycol && M_MenuButtonHeld(pid, MBT_CANCEL))
					{
						col -= 4;
						canmodifycol = false;
					}
				}
				else if (c == KEY_RSHIFT)
				{
					arrowoffset = 2;
					buf[0] = '\x1A'; // up arrow
					buf[1] = '\0';

					if (menutyping.keyboardcapslock || menutyping.keyboardshift)
					{
						col = 22;
					}
				}
				else if (c == KEY_ENTER)
				{
					strcpy(buf, "OK");

					if (menutyping.menutypingclose)
					{
						col -= 4;
						canmodifycol = false;
					}
				}
				else if (c == KEY_SPACE)
				{
					strcpy(buf, "Space");
				}
				else
				{
					buf[0] = c;
					buf[1] = '\0';
				}

				int32_t width = (buttonwidth * buttonspacing) - 1;

				// highlight:
				/*if (menutyping.keyboardtyping)
				{
					mflag |= V_TRANSLUCENT;	// grey it out if we can't use it.
				}
				else*/
				{
					if (tempkeyboardx == j && menutyping.keyboardy == i)
					{
						if (canmodifycol && M_MenuButtonHeld(pid, MBT_CONFIRM))
						{
							col -= 4;
							canmodifycol = false;
						}

						V_DrawFill(x + 1, y + 1, width - 2, BUTTONHEIGHT - 2, col - 3);

						V_DrawFill(x, y,                    width, 1, 121);
						V_DrawFill(x, y + BUTTONHEIGHT - 1, width, 1, 121);

						V_DrawFill(x,             y + 1, 1, BUTTONHEIGHT - 2, 121);
						V_DrawFill(x + width - 1, y + 1, 1, BUTTONHEIGHT - 2, 121);

						mflag |= highlightflags;
					}
					else
					{
						V_DrawFill(x, y, width, BUTTONHEIGHT, col);
					}
				}

				if (arrowoffset != 0)
				{
					if (c == KEY_RSHIFT)
					{
						V_DrawFill(x + width - 5, y + 1, 4, 4, 31);

						if (menutyping.keyboardcapslock)
						{
							V_DrawFill(x + width - 4, y + 2, 2, 2, 121);
						}
					}

					V_DrawCenteredString(x + (width/2), y + 1 + arrowoffset, mflag, buf);
				}
				else
				{
					V_DrawCenteredThinString(x + (width/2), y + 1, mflag, buf);
				}

				x += width + 1;
				j += buttonspacing;
			}
			x = returnx;
			y += BUTTONHEIGHT + 1;
		}
	}

#undef BUTTONHEIGHT

	y = 187;

	if (menutyping.menutypingfade < TYPEFADETIMEHALF)
	{
		y += 3 * (TYPEFADETIMEHALF - menutyping.menutypingfade);
	}

	// Some contextual stuff
	if (menutyping.keyboardtyping)
	{
		V_DrawThinString(returnx, y-10, V_GRAYMAP|V_ALLOWLOWERCASE|V_6WIDTHSPACE,
			"Type using your keyboard. Press Enter to confirm & exit."
			"\nPress any button on your controller to use the Virtual Keyboard."
		);
	}
	else
	{
		V_DrawThinString(returnx, y-10, V_GRAYMAP|V_ALLOWLOWERCASE|V_6WIDTHSPACE,
			"Type using the Virtual Keyboard. Use the \'OK\' button to confirm & exit."
			"\nPress any keyboard key to type normally."
		);
	}

}


//
// M_Drawer
// Called after the view has been rendered,
// but before it has been blitted.
//
void M_Drawer(void)
{
	if (menustack[0])
	{
		// now that's more readable with a faded background (yeah like Quake...)
		if (gamestate == GS_MENU)
		{
			if (curbgcolor >= 0)
				V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, curbgcolor);
			else if (fastcmp(curbgname, "SRB2KTTL") && (!curbghide || !titlemapinaction))
				F_TitleBGScroll(5, menuanimtimer);
			else if ((!curbghide || !titlemapinaction))
				F_SkyScroll(curbgxspeed, curbgyspeed, curbgname);
			if (curfadevalue)
				V_DrawFadeScreen(0xFF00, curfadevalue);
		}
		else if (!WipeInAction && curfadevalue)
			V_DrawFadeScreen(0xFF00, curfadevalue);
	}

	if (GetClientMode() != CL_DOWNLOADFILES && GetClientMode() != CL_LOADFILES && GetClientMode() != CL_CHECKFILES
#ifdef HAVE_CURL
		&& GetClientMode() != CL_DOWNLOADHTTPFILES
#endif
		&& GetClientMode() != CL_CONFIRMCONNECT)
	{
		size_t i;
		uint16_t sortedpanels[UINT16_MAX];
		if (numcurpanels >= UINT16_MAX)
			I_Error("You have way too many panels, man!");
		for (i = 0; i < numcurpanels; i++)
			sortedpanels[i] = i;
		qs22j(sortedpanels, numcurpanels, sizeof(*sortedpanels), SortPanels);

		if (currentMenu && currentMenu->vhs && cv_vhseffect.value)
			V_DrawVhsEffect(false);

		M_GetGametypeColor();
		for (i = 0; i < numcurpanels; i++)
			M_DrawMenuPanel(&curpanels[sortedpanels[i]], panelOn == sortedpanels[i] && !curpanels[sortedpanels[i]].disabled);

		if (menustack[0] == MN_MAIN)
		{
			int32_t texty = vid.height - 10*vid.dup;
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

		// draw menuitem tooltip
		if (numcurpanels > 0)
		{
			const menupanelinst_t *pi = &curpanels[panelOn];
			if (pi->itemon < pi->numitems && pi->items[pi->itemon].tooltip != NULL)
			{
				M_DrawSplitText(pi->items[pi->itemon].tooltip,
								BASEVIDWIDTH / 2,
					BASEVIDHEIGHT-50,
					V_SNAPTOBOTTOM|V_6WIDTHSPACE|MENUCAPS,
					cursortimer <= 9 ? 9 - cursortimer : 0);
			}
		}
	}

	if (menutyping.active)
		M_DrawMenuTyping();

	if (messagebox.active)
	{
		V_DrawFadeScreen(0xFF00, 16);
		M_DrawMessageMenu();
	}

	dboolean mainpause_open = menustack[0]
		&& (menustack[0] == MN_MPAUSE ||
			menustack[0] == MN_MAPAUSE ||
			menustack[0] == MN_SPAUSE
		);

	if (netgame && Playing())
	{
		ST_DrawServerSplash(!mainpause_open);
	}

	if (mainpause_open && g_realsongcredit)
	{
		V_DrawThinString(6, 32+20, V_ALLOWLOWERCASE|V_6WIDTHSPACE|V_SNAPTOLEFT, g_realsongcredit);
	}

	// focus lost notification goes on top of everything, even the former everything
	if (window_notinfocus && cv_showfocuslost.value)
	{
		M_DrawTextBox((BASEVIDWIDTH/2) - (60), (BASEVIDHEIGHT/2) - (16), 13, 2, 0);
		if (gamestate == GS_LEVEL && (P_AutoPause() || paused))
			V_DrawCenteredString(BASEVIDWIDTH/2, (BASEVIDHEIGHT/2) - (4), highlightflags, "Game Paused");
		else
			V_DrawCenteredString(BASEVIDWIDTH/2, (BASEVIDHEIGHT/2) - (4), highlightflags, "Focus Lost");
	}
}

void MP_PlayerSplitscreen(menupanelinst_t *pi)
{
	M_SetItemVisible(pi, "PLAYER", splitscreen > 0);
}

void MP_MultiplayerPause(menupanelinst_t *pi)
{
#ifdef HAVE_DISCORDRPC
	pi->y = 72*FRACUNIT;
	M_RefreshPauseMenu();
	M_SetItemVisible(pi, "DISCORDREQUESTS", true);
#else
	pi->y = 80*FRACUNIT;
	M_SetItemVisible(pi, "DISCORDREQUESTS", false);
#endif
	Dummymenuplayer_OnChange();

	M_SetItemVisible(pi, "MAPCHANGE", server || IsPlayerAdmin(consoleplayer));
	M_SetItemVisible(pi, "ADDONS", server || IsPlayerAdmin(consoleplayer));
	M_SetItemVisible(pi, "SCRAMBLE", (server || IsPlayerAdmin(consoleplayer)) && G_GametypeHasTeams());

	if (!(server || IsPlayerAdmin(consoleplayer)))
		pi->y += 24*FRACUNIT;

	M_SetItemVisible(pi, "SETUP1", splitscreen > 0);
	M_SetItemVisible(pi, "SETUP2", splitscreen > 0);
	M_SetItemVisible(pi, "SETUP3", splitscreen > 1);
	M_SetItemVisible(pi, "SETUP4", splitscreen > 2);
	M_SetItemVisible(pi, "PLAYERSETUP", !splitscreen);

	// splitscreen hell! yay!
	M_SetItemVisible(pi, "SPECTATE", false);
	M_SetItemDisabled(pi, "SPECTATE", false);
	M_SetItemVisible(pi, "ENTER", false);
	M_SetItemVisible(pi, "CANCELJOIN", false);
	M_SetItemVisible(pi, "TEAMCHANGE", false);
	M_SetItemVisible(pi, "SPECTATECHANGE", false);

	if (splitscreen && netgame)
	{
		if (G_GametypeHasTeams())
			M_SetItemVisible(pi, "TEAMCHANGE", true);
		else if (G_GametypeHasSpectators())
			M_SetItemVisible(pi, "SPECTATECHANGE", true);
	}
	else if (!splitscreen)
	{
		if (G_GametypeHasTeams())
			M_SetItemVisible(pi, "TEAMCHANGE", true);
		else if (G_GametypeHasSpectators())
			M_SetItemVisible(pi, !players[consoleplayer].spectator ? "SPECTATE" :
			                            players[consoleplayer].pflags & PF_WANTSTOJOIN ? "CANCELJOIN" :
			                            "ENTER", true);
		else // in this odd case, we still want something to be on the menu even if it's useless
		{
			M_SetItemVisible(pi, "SPECTATE", true);
			M_SetItemDisabled(pi, "SPECTATE", true);
		}
	}
}

//
// M_StartControlPanel
//
void M_StartControlPanel(void)
{
	int32_t i;

	// intro might call this repeatedly
	if (menustack[0] && gamestate != GS_NULL)
	{
		CON_ToggleOff(); // move away console
		return;
	}

	menupanelinst_t *pi;

	if (gamestate == GS_TITLESCREEN) // Set up menu state
	{
		// No instantly skipping the titlescreen.
		// (We can change this timer later when extra animation is added.)
		if (finalecount < 1)
			return;
	}

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		menucmd[i].delay = MENUDELAYTIME;
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
		pi = M_GetPanel("MAIN");
		M_SetItemOn(pi, "SINGLEPLAYER");
	}
	else if (modeattacking)
	{
		M_EnterMenu(MN_MAPAUSE, true, 0);
		pi = M_GetPanel("MAIN");
		M_SetItemOn(pi, "CONTINUE");
	}
	else if (grandprixinfo.gp) // Single Player
	{
		dboolean retryallowed = false;
		for (i = 0; i <= splitscreen; i++)
		{
			if (G_GametypeUsesLives() && players[g_localplayers[i]].lives <= 1)
				continue;
			retryallowed = true;
			break;
		}

		if (gamestate != GS_LEVEL)
			retryallowed = false;

		M_EnterMenu(MN_SPAUSE, true, 0);
		pi = M_GetPanel("MAIN");
		M_SetItemOn(pi, "CONTINUE");

		// intermission, so gray out stuff.
		M_SetItemDisabled(pi, "RETRY", !retryallowed /*|| ultimatemode*/);
	}
	else // multiplayer
	{
		M_EnterMenu(MN_MPAUSE, true, 0);
		pi = M_GetPanel("MAIN");
		M_SetItemOn(pi, "CONTINUE");
	}

	CON_ToggleOff(); // move away console
}

static void M_ClearPanelInstances(void)
{
	while (numcurpanels > 0)
	{
		menupanelinst_t *pi = &curpanels[--numcurpanels];
		menupanelinst_t *src = &currentMenu->panels[numcurpanels];
		if (pi->defnum != src->defnum)
			I_Error("Mismatched panel defnum, you've got work to do");

		src->itemon = pi->itemon; // save last selected item

		M_ClearPanelItems(pi);
	}
	Z_Free(curpanels);
}

//
// M_ClearMenusEx
//
void M_ClearMenusEx(dboolean callexitmenufunc, dboolean notitle)
{
	CON_ClearHUD();
	I_SetTextInput(false);

	if (currentMenu)
	{
		if (currentMenu->quitroutine && callexitmenufunc)
			currentMenu->quitroutine(0);

		// Save the config file. I'm sick of crashing the game later and losing all my changes!
		char buf[sizeof(configfile) + 50];
		snprintf(buf, sizeof(buf), "saveconfig \"%s\" -silent\n", configfile);
		COM_BufAddText(buf);

		if (currentMenu->exitwipe >= 0)
		{
			wipetypepre = currentMenu->exitwipe;
			wipegamestate = FORCEWIPE;
		}
	}

	M_ClearPanelInstances();
	memset(menustack, 0, sizeof(menustack));
	currentMenu = NULL;
	M_MessageboxClose(true);
	hidetitlemap = false;
	M_AbortVirtualKeyboard();

	if (!notitle)
	{
		// D_StartTitle does its own wipe, since GS_MENU is now a complete gamestate.
		if (gamestate == GS_MENU)
			D_StartTitle();
	}
}

// corrects the value of cv_nextmap if it can't show its current map
// returns false if there's no map to show
static dboolean M_CheckNextMap(const menupanelinst_t *pi)
{
	size_t i;

	if (pi != NULL)
	{
		for (i = 0; i < pi->numitems; i++)
			if (pi->items[i].cvar == &cv_nextmap)
				break;
		if (i == pi->numitems)
			return true;

		if (levellistmode_ta != LLM__MAX)
			pi->items[i].argument = levellistmode_ta;

		levellistmode = pi->items[i].argument;
	}

	if (!M_CanShowLevelInList(cv_nextmap.value - 1))
	{
		for (i = 0; !M_CanShowLevelInList(i); i++)
			if ((ssize_t)i == nummapheaders)
				return false;
		CV_SetValue(&cv_nextmap, i + 1);
	}

	return true;
}

// ditto, but for cv_nextcup
static dboolean M_CheckNextCup(const menupanelinst_t *pi)
{
	(void)pi;

	if (kartcupheaders == NULL)
		return false;

	if (cv_nextcup.value == 0)
		CV_SetValue(&cv_nextcup, kartcupheaders->id + 1);

	return true;
}

static void M_MaybeSetItemVisible(menupanelinst_t *pi, const char *name, dboolean cond)
{
	menuitem_t *item = M_CheckMenuItem(pi, name);
	if (item == NULL)
		return;

	if (cond)
		item->status &= ~IT_HIDDEN;
	else
		item->status |= IT_HIDDEN;
}

//
// M_SetupNextMenu
//
static void M_SetupNextMenu(menutype_t menunum)
{
	size_t i, j;
	menu_t *menudef = &menudefs[menunum];

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		menucmd[i].delay = MENUDELAYTIME;
	}

	M_HandleMenuPresState(menunum);
	M_ClearPanelInstances();
	menutimer = cursortimer = 0;
	currentMenu = menudef;

	for (i = 0; i < menudef->numpanels; i++)
	{
		menupanelinst_t *src = &menudef->panels[i];
		const menupaneldef_t *pd = &menupanels[src->defnum];

		Z_Realloc(curpanels, sizeof(*curpanels)*(numcurpanels + 1), PU_STATIC, &curpanels);
		menupanelinst_t *pi = &curpanels[numcurpanels++];

		memset(pi, 0, sizeof(*pi));
		pi->visible = true;

		// copy instance stuff
		strlcpy(pi->name, src->name, sizeof(pi->name));
		pi->x = src->x * FRACUNIT;
		pi->y = src->y * FRACUNIT;
		pi->defnum = src->defnum;
		pi->itemon = src->itemon;
		pi->vflags = src->vflags;
		pi->disabled = src->disabled;
		pi->layer = src->layer;
		pi->cliprect = src->cliprect;
		pi->cliprect_relative = src->cliprect_relative;
		pi->cliprect_x = src->cliprect_x * FRACUNIT;
		pi->cliprect_y = src->cliprect_y * FRACUNIT;
		pi->cliprect_width = src->cliprect_width * FRACUNIT;
		pi->cliprect_height = src->cliprect_height * FRACUNIT;

		// copy items from the panel definition (TODO menuitem def/inst)
		if (pd->numitems > 0)
		{
			pi->items = Z_Malloc(sizeof(menuitem_t)*pd->numitems, PU_STATIC, NULL);
			memcpy(pi->items, pd->items, sizeof(menuitem_t)*pd->numitems);
			pi->numitems = pd->numitems;
			for (j = 0; j < pi->numitems; j++)
			{
				menuitem_t *item = &pi->items[j];
				if (item->text != NULL)
					item->text = Z_StrDup(item->text);
				if (item->patch != NULL)
					item->patch = Z_StrDup(item->patch);
				if (item->tooltip != NULL)
					item->tooltip = Z_StrDup(item->tooltip);
				if (item->extra != NULL)
					item->extra = Z_StrDup(item->extra);
				item->x *= FRACUNIT;
				item->y *= FRACUNIT;
			}
		}
		else
		{
			pi->items = NULL;
		}

		pi->scrollheight = pd->scrollheight*FRACUNIT;
		pi->cursoroffset = pd->cursoroffset*FRACUNIT;
		pi->navitemcount = pd->navitemcount;

		M_CheckNextMap(pi);
		M_CheckNextCup(pi);

		if (pd->initializer != NULL)
			pd->initializer(pi);

		M_RawSetItemOn(pi, pi->itemon);
	}

	// TODO navigation between panels
	panelOn = 0;

	hidetitlemap = false;
}

// pop the active menu from the stack
void M_ExitMenu(void)
{
	size_t i;

	if (currentMenu != NULL && currentMenu->quitroutine != NULL)
		currentMenu->quitroutine(0);

	for (i = 0; i < NUMMENULEVELS-1; i++)
		menustack[i] = menustack[i+1];
	menustack[NUMMENULEVELS-1] = MN_NONE;

	if (menustack[0])
		M_SetupNextMenu(menustack[0]);
	else
		M_ClearMenus(true);
}

// push a new menu to the stack (if the menu's enterroutine allows it)
void M_EnterMenu(menutype_t menunum, dboolean callexit, int32_t arg)
{
	(void)callexit; // quitroutines are only called when exiting menus now, not entering!
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
		menustack[NUMMENULEVELS-1] = MN_NONE;
		return;
	}

	if (menustack[0] == MN_NONE)
		menustack[0] = menunum; // M_ClearMenus got called, put it back on the stack

	M_SetupNextMenu(menunum);
}

// Use for menus you return to.
// Backs up the stack so you can place it back into place
void M_RestoreMenuStack(dboolean backup)
{
	if (backup)
	{
		memcpy(restoremenustack, menustack, sizeof(menustack));
		return;
	}

	D_ClearState();
	M_ClearMenusEx(true, false);
	M_StartControlPanel();

	// Put the stack back into place and wipe the restore stack
	memcpy(menustack, restoremenustack, sizeof(restoremenustack));
	memset(restoremenustack, MN_NONE, sizeof(restoremenustack));

	if (menustack[0])
		M_SetupNextMenu(menustack[0]);
	else
	{
		CONS_Alert(CONS_WARNING, "Could not restore menu stack, returning to main menu.\n");

		M_ClearMenusEx(false, true);
		D_ClearState();
		M_StartControlPanel();
	}
}

// Guess I'll put this here, idk
dboolean M_MouseNeeded(void)
{
	return false;
}

//
// M_Ticker
//

void M_SetMenuDelay(uint8_t i)
{
	menucmd[i].delayCount++;
	if (menucmd[i].delayCount < 1)
	{
		// Shouldn't happen, but for safety.
		menucmd[i].delayCount = 1;
	}

	menucmd[i].delay = (MENUDELAYTIME - min(MENUDELAYTIME - 1, menucmd[i].delayCount));
	if (menucmd[i].delay < MENUMINDELAY)
	{
		menucmd[i].delay = MENUMINDELAY;
	}
}

static void handle_dpad(event_t* ev, int32_t device, dboolean keyup)
{
	int8_t pid = G_GetDevicePlayer(device);

	if (pid == -1)
		return;

	switch (ev->data1)
	{
		case KEY_HAT1+0:
			menucmd[pid].ev_dpad_ud =  keyup ? 0 : -1;
			break;
		case KEY_HAT1+1:
			menucmd[pid].ev_dpad_ud = keyup ? 0 : 1;
			break;
		case KEY_HAT1+2:
			menucmd[pid].ev_dpad_lr = keyup ? 0 : -1;
			break;
		case KEY_HAT1+3:
			menucmd[pid].ev_dpad_lr = keyup ? 0 : 1;
			break;
		default:
			break;
	}
}

void M_HandleDpadEvent(event_t* ev)
{
	int32_t device = ev->device;

	if (ev->data1 < KEY_JOY1 || ev->data1 >= JOYINPUTEND)
		return;

	switch (ev->type)
	{
		case ev_keydown:
			handle_dpad(ev, device, false);
			break;

		case ev_keyup:
			handle_dpad(ev, device, true);
			break;

		default:
			break;
	}
}

void M_UpdateMenuCMD(uint8_t i, dboolean bailrequired)
{
	int32_t thisMenuKey = menuKey;

	if (menucmd[i].wipebuffer && WipeInAction)
		return;
	else if (menucmd[i].wipebuffer && !WipeInAction)
	{
		menucmd[i].wipebuffer = false;
		return; // Lets do our last action now.
	}

	menucmd[i].prev_dpad_ud = menucmd[i].dpad_ud;
	menucmd[i].prev_dpad_lr = menucmd[i].dpad_lr;

	menucmd[i].dpad_ud = 0;
	menucmd[i].dpad_lr = 0;

	menucmd[i].buttonsHeld = menucmd[i].buttons;
	menucmd[i].buttons = 0;

	if (bailrequired && i == 0)
	{
		if (gamekeydown[0][KEY_ESCAPE] > 0) { menucmd[i].buttons |= MBT_CANCEL; }
	}

	if (keyboardcontrolled && i == 0 && (menustack[0] || messagebox.active))
	{
		switch (thisMenuKey)
		{
			case KEY_UPARROW:
				menucmd[i].dpad_ud--;
				break;
			case KEY_DOWNARROW:
				menucmd[i].dpad_ud++;
				break;

			case KEY_LEFTARROW:
				menucmd[i].dpad_lr--;
				break;

			case KEY_RIGHTARROW:
				menucmd[i].dpad_lr++;
				break;

			case KEY_ENTER:
				menucmd[i].buttons |= MBT_CONFIRM;
				break;

			case KEY_ESCAPE:
				if (menustack[0])
					menucmd[i].buttons |= MBT_CANCEL;
				else
					menucmd[i].buttons |= MBT_HOME;
				break;

			case KEY_SPACE:
				menucmd[i].buttons |= MBT_INFO1;
				break;

			case KEY_BACKSPACE:
				menucmd[i].buttons |= MBT_INFO2;
				break;

			case KEY_EQUALS:
				menucmd[i].buttons |= MBT_INFO3;
				break;
		}
	}
	else
	{
		if (G_PlayerInputDown(i, gc_aimforward, false, DEADZONE_Y)) { menucmd[i].dpad_ud--; }
		else if (menustack[0] && menucmd[i].ev_dpad_ud < 0) { menucmd[i].dpad_ud--; }

		if (G_PlayerInputDown(i, gc_aimbackward, false, DEADZONE_Y)) { menucmd[i].dpad_ud++; }
		else if (menustack[0] && menucmd[i].ev_dpad_ud > 0) { menucmd[i].dpad_ud++; }

		if (G_PlayerInputDown(i, gc_turnleft, false, DEADZONE_X)) { menucmd[i].dpad_lr--; }
		else if (menustack[0] && menucmd[i].ev_dpad_lr < 0) { menucmd[i].dpad_lr--; }

		if (G_PlayerInputDown(i, gc_turnright, false, DEADZONE_X)) { menucmd[i].dpad_lr++; }
		else if (menustack[0] && menucmd[i].ev_dpad_lr > 0) { menucmd[i].dpad_lr++; }

		if (G_PlayerInputDown(i, gc_accelerate, false, DEADZONE_BUTTON)) { menucmd[i].buttons |= MBT_CONFIRM; }
		if (G_PlayerInputDown(i, gc_brake, false, DEADZONE_BUTTON)) { menucmd[i].buttons |= MBT_CANCEL; }
		if (G_PlayerInputDown(i, gc_fire, false, DEADZONE_BUTTON)) { menucmd[i].buttons |= MBT_INFO1; }
		if (G_PlayerInputDown(i, gc_drift, false, DEADZONE_BUTTON)) { menucmd[i].buttons |= MBT_INFO2; }
		if (G_PlayerInputDown(i, gc_pause, false, DEADZONE_BUTTON)) { menucmd[i].buttons |= MBT_INFO3; }
		if (G_PlayerInputDown(i, gc_systemmenu, false, DEADZONE_BUTTON)) { menucmd[i].buttons |= MBT_HOME; }
	}

	if (menucmd[i].dpad_ud == 0 && menucmd[i].dpad_lr == 0 && menucmd[i].buttons == 0)
	{
		// Reset delay count with no buttons.
		menucmd[i].delay = min(menucmd[i].delay, MENUMINDELAY);
		menucmd[i].delayCount = 0;
	}
}

dboolean M_MenuButtonPressed(uint8_t pid, uint32_t bt)
{
	if (menucmd[pid].buttonsHeld & bt)
	{
		return false;
	}

	return (menucmd[pid].buttons & bt);
}

dboolean M_MenuButtonHeld(uint8_t pid, uint32_t bt)
{
	return !!(menucmd[pid].buttons & bt);
}

static dboolean M_HandleGoBack(int32_t thisMenuKey)
{
	if (M_MenuButtonPressed(0, MBT_CANCEL) || M_MenuButtonPressed(0, MBT_HOME))
	{
		noFurtherInput = -1;

		if (M_WipeBuffer(thisMenuKey, currentMenu->quitroutine))
			return true;

		//If we entered the game search menu, but didn't enter a game,
		//make sure the game doesn't still think we're in a netgame.
		if (!Playing() && netgame && multiplayer)
		{
			netgame = false;
			multiplayer = false;
		}
		M_ExitMenu();

		pausehack = MENUDELAYTIME;
		return true;
	}

	return false;
}

sfxenum_t M_MenuTypingSounds(int32_t key)
{
	switch (key)
	{
		case KEY_BACKSPACE:
			return sfx_s1b8;
		case KEY_LSHIFT:
		case KEY_RSHIFT:
			return sfx_s3k52;
		case KEY_CAPSLOCK:
			return sfx_s1ba;
		case KEY_ENTER:
			return sfx_s3k63;
		case KEY_LEFTARROW:
		case KEY_RIGHTARROW:
			return sfx_s3k5b;
		case KEY_ESCAPE:
			return sfx_s3k63;
		default:
			return sfx_s1bb;
	}
}

void M_OpenVirtualKeyboard(textinput_t *textinput, char *buffer, menuitem_t *item)
{
	if (item == NULL)
	{
		M_StartMessage2("Failed to open Keyboard", "Failed to open Keyboard dialog.\nSupplied item does not exist.\n", MB_NONE, MM_NOTHING, "Press any key", NULL);
		return;
	}

	menutyping.active = true;
	menutyping.menutypingclose = false;

	menutyping.textinput = textinput;
	menutyping.item = item;

	// Start on enter for people who want to quickly exit typing menus.
	menutyping.keyboardx = 11;
	menutyping.keyboardy = 4;

	M_TextInputInit(menutyping.textinput, buffer, M_StringCvarLength(menutyping.item->cvar));
	M_TextInputSetString(menutyping.textinput, menutyping.item->cvar->string);
	I_SetTextInput(true);
}

void M_SwitchVirtualKeyboard(dboolean gamepad)
{
	menutyping.keyboardtyping = cv_debugvirtualkeyboard.value ? false : !gamepad;
}

static void M_CloseVirtualKeyboard(void)
{
	menutyping.menutypingclose = true;	// close menu.
}

void M_AbortVirtualKeyboard(void)
{
	if (!menutyping.active)
		return;

	menutyping.active = false;
	menutyping.menutypingfade = 0;

	menutyping.textinput = NULL;
	I_SetTextInput(false);

	menutyping.item = NULL;

	if (menustack[0] == MN_TYPINGDUMMY)
		M_ClearMenusEx(false, true);
}

static void M_ToggleVirtualShift(void)
{
	if (menutyping.keyboardcapslock == true)
	{
		menutyping.keyboardcapslock = false;
	}
	else
	{
		menutyping.keyboardshift ^= true;
		if (menutyping.keyboardshift == false)
		{
			menutyping.keyboardcapslock = true;
		}
	}
}

void M_MenuTypingInput(int32_t key)
{
	const uint8_t pid = 0;
	menuitem_t *item = menutyping.item;

	// Determine when to check for keyboard inputs or controller inputs using menuKey, which is the key passed here as argument.
	if (key > 0)
	{
		dboolean gamepad = (key >= NUMKEYS);
		M_SwitchVirtualKeyboard(gamepad);
		if (gamepad)
			return;
	}

	if (!menutyping.active)
		return;

	// Fade-in

	if (menutyping.menutypingclose)
	{
		// Closing
		menutyping.menutypingfade--;
		if (!menutyping.menutypingfade)
		{
			M_AbortVirtualKeyboard();
			M_DoItemRoutine(item, key);
		}

		return;	// prevent inputs while closing the menu.
	}
	else
	{
		// Opening
		const uint8_t destination = (menutyping.keyboardtyping ? TYPEFADETIMEHALF : TYPEFADETIME);

		if (menutyping.menutypingfade > destination)
		{
			menutyping.menutypingfade--;
		}
		else if (menutyping.menutypingfade < destination)
		{
			menutyping.menutypingfade++;
		}

		if (menutyping.menutypingfade >= TYPEFADETIMEHALF) // either is visible
		{
			if (key == KEY_ENTER || key == KEY_ESCAPE)
			{
				M_CloseVirtualKeyboard();
				CV_Set(item->cvar, menutyping.textinput->buffer);
				M_SetMenuDelay(pid);
				S_StartSound(NULL, M_MenuTypingSounds(key));
				return;
			}

			if (menutyping.keyboardtyping)
			{
				if (key > 0 && M_TextInputHandle(menutyping.textinput, key))
				{
					S_StartSound(NULL, M_MenuTypingSounds(key));
				}
				return;
			}
		}

		if (menutyping.menutypingfade != destination)
		{
			// Don't allow typing until it's fully opened.
			return;
		}
	}

	if (menucmd[pid].delay == 0 && !menutyping.keyboardtyping)	// We must check for this here because we bypass the normal delay check to allow for normal keyboard inputs
	{
		if (menucmd[pid].dpad_ud > 0)	// down
		{
			menutyping.keyboardy++;
			if (menutyping.keyboardy > 4)
				menutyping.keyboardy = 0;

			M_SetMenuDelay(pid);
			S_StartSound(NULL, sfx_s3k5b);
		}
		else if (menucmd[pid].dpad_ud < 0) // up
		{
			menutyping.keyboardy--;
			if (menutyping.keyboardy < 0)
				menutyping.keyboardy = 4;

			M_SetMenuDelay(pid);
			S_StartSound(NULL, sfx_s3k5b);
		}
		else if (menucmd[pid].dpad_lr > 0)	// right
		{
			do
			{
				menutyping.keyboardx++;
				if (menutyping.keyboardx > NUMVIRTUALKEYSINROW-1)
				{
					menutyping.keyboardx = 0;
					break;
				}
			}
			while (virtualKeyboard[menutyping.keyboardy][menutyping.keyboardx] == 1);

			M_SetMenuDelay(pid);
			S_StartSound(NULL, sfx_s3k5b);
		}
		else if (menucmd[pid].dpad_lr < 0)	// left
		{
			while (virtualKeyboard[menutyping.keyboardy][menutyping.keyboardx] == 1)
			{
				menutyping.keyboardx--;
				if (menutyping.keyboardx < 0)
				{
					menutyping.keyboardx = NUMVIRTUALKEYSINROW-1;
					break;
				}
			}

			menutyping.keyboardx--;
			if (menutyping.keyboardx < 0)
			{
				menutyping.keyboardx = NUMVIRTUALKEYSINROW-1;
			}

			M_SetMenuDelay(pid);
			S_StartSound(NULL, sfx_s3k5b);
		}
		else if (M_MenuButtonPressed(pid, MBT_INFO1))
		{
			M_TextInputHandle(menutyping.textinput, KEY_LEFTARROW);
			M_SetMenuDelay(pid);
			S_StartSound(NULL, sfx_s3k5b);
		}
		else if (M_MenuButtonPressed(pid, MBT_INFO2))
		{
			M_TextInputHandle(menutyping.textinput, KEY_RIGHTARROW);
			M_SetMenuDelay(pid);
			S_StartSound(NULL, sfx_s3k5b);
		}
		else if (M_MenuButtonPressed(pid, MBT_INFO3))
		{
			M_ToggleVirtualShift();
			M_SetMenuDelay(pid);
			S_StartSound(NULL, M_MenuTypingSounds(menutyping.keyboardcapslock ? KEY_CAPSLOCK : KEY_RSHIFT));
		}
		else if (M_MenuButtonPressed(pid, MBT_CANCEL))
		{
			M_TextInputHandle(menutyping.textinput, KEY_BACKSPACE);
			M_SetMenuDelay(pid);
			S_StartSound(NULL, M_MenuTypingSounds(KEY_BACKSPACE));
		}
		else if (M_MenuButtonPressed(pid, MBT_CONFIRM))
		{
			// Add the character. First though, check what we're pressing....
			int32_t tempkeyboardx = menutyping.keyboardx;
			int16_t c = 0;
			while ((c = virtualKeyboard[menutyping.keyboardy][tempkeyboardx]) == 1
				&& tempkeyboardx > 0)
				tempkeyboardx--;

			if (c > 1)
			{
				if (menutyping.keyboardshift ^ menutyping.keyboardcapslock)
					c = shift_virtualKeyboard[menutyping.keyboardy][tempkeyboardx];

				if (c == KEY_RSHIFT)
				{
					M_ToggleVirtualShift();
					M_SetMenuDelay(pid);
					S_StartSound(NULL, M_MenuTypingSounds(menutyping.keyboardcapslock ? KEY_CAPSLOCK : KEY_RSHIFT));
					return;
				}
				/*
				 *	else if (c == KEY_CAPSLOCK)
				 *		menutyping.keyboardcapslock = !menutyping.keyboardcapslock;
				 */
				else if (c == KEY_ENTER)
				{
					M_CloseVirtualKeyboard();
					CV_Set(item->cvar, menutyping.textinput->buffer);
					S_StartSound(NULL, M_MenuTypingSounds(c));
					return;
				}
				else
				{
					M_TextInputHandle(menutyping.textinput, (int32_t)c); // Write!
					menutyping.keyboardshift = false;		// undo shift if it had been pressed
				}

				M_SetMenuDelay(pid);
				S_StartSound(NULL, M_MenuTypingSounds(c));
			}
		}
		else if (M_MenuButtonPressed(pid, MBT_HOME))
		{
			M_CloseVirtualKeyboard();
			CV_Set(item->cvar, menutyping.textinput->buffer);
			S_StartSound(NULL, M_MenuTypingSounds(KEY_ENTER));
			return;
		}
	}
}

void M_DoItemRoutine(menuitem_t *item, int32_t thisMenuKey)
{
	if (item == NULL)
		return;

	uint8_t pid = 0;

	noFurtherInput = -1;

	if (menustack[0] == MN_PLAYBACK)
	{
		dboolean held = (dboolean)playback_enterheld;
		if (held)
			return;
		playback_enterheld = 3;
	}

	int32_t argument = item->argument;
	if (item->cvar && item->cvar != &cv_nextmap && item->cvar != &cv_nextcup)
		argument = item->cvar->value;

	if (item->submenu)
	{
		if (M_WipeBuffer(thisMenuKey, menudefs[item->submenu].enterroutine))
			return;
		S_StartSound(NULL, sfx_menu1);
		M_EnterMenu(item->submenu, true, argument);
	}
	else if (item->routine)
	{
		if (M_WipeBuffer(thisMenuKey, item->routine))
			return;
		S_StartSound(NULL, sfx_menu1);
		item->routine(argument);
	}
	else if (item->cvar && item->cvar->PossibleValue) // not for string cvars!
	{
		if (item->cvar == &cv_nextmap || item->cvar == &cv_nextcup)
			return;
		if (item->cvar->flags & CV_CALL && M_WipeBuffer(thisMenuKey, MR_Dummy))
			return;
		S_StartSound(NULL, sfx_menu1);
		M_ChangeCvar(item, 1); // right arrow
	}

	M_SetMenuDelay(pid);
}

menupanelinst_t *M_GetCurrentMenuPanelInst(void)
{
	return panelOn < numcurpanels ? &curpanels[panelOn] : NULL;
}

menupaneldef_t *M_GetCurrentMenuPanelDef(menupanelinst_t *curpi)
{
	return curpi != NULL ? M_GetDefFromInst(curpi) : NULL;
}

menuitem_t *M_GetCurrentMenuItem(menupanelinst_t *curpi)
{
	return curpi && curpi->numitems ? &curpi->items[curpi->itemon] : NULL;
}

void M_HandleMessageBox(int32_t thisMenuKey)
{
	const uint8_t pid = 0;

	if (messagebox.messagetype != MM_EVENTHANDLER)
	{
		if (menucmd[pid].buttons != 0 && menucmd[pid].buttonsHeld == 0)
		{
			if (M_WipeBuffer(thisMenuKey, MR_Dummy))
				return;
			if (messagebox.callback.routine)
			{
				if (messagebox.messagetype == MM_YESNO)
				{
					if (M_MenuButtonPressed(0, MBT_CONFIRM))
					{
						messagebox.callback.routine(MA_YES);
						S_StartSound(NULL, sfx_s3k63);
					}
					else if (M_MenuButtonPressed(0, MBT_CANCEL) || M_MenuButtonPressed(0, MBT_HOME))
					{
						messagebox.callback.routine(MA_NO);
						S_StartSound(NULL, sfx_s1b8);
					}
				}
				else
				{
					messagebox.callback.routine(thisMenuKey);
				}
			}

			M_MessageboxClose(false);
			noFurtherInput = -1;
			M_SetMenuDelay(pid);
			return;
		}
	}
}

static void M_HandleMenuInput(void)
{
	uint8_t pid = 0;
	int32_t thisMenuKey = menuKey;

	menuKey = -1;

	if (!menustack[0])
	{
		// We're not in the menu.
		return;
	}

	if (messagebox.active)
	{
		M_HandleMessageBox(thisMenuKey);
		return;
	}

	if (menucmd[pid].wipebuffer)
	{
		return;
	}

	// Typing for text input
	if (menutyping.active)
	{
		M_MenuTypingInput(-1);
		return;
	}

	if (menucmd[pid].delay > 0)
	{
		return;
	}

	// BP: one of the more big hack i have never made
	// G: not so hacky anymore...?
	// G: nevermind we have combi menuitems now
	// G: OH DEAR GOD
	menupanelinst_t *curpi = M_GetCurrentMenuPanelInst();
	menupaneldef_t *curpd = M_GetCurrentMenuPanelDef(curpi);
	menuitem_t *item = M_GetCurrentMenuItem(curpi);

	// Handle menuitems which need a specific key handling
	if (curpd != NULL && curpd->keyhandler != NULL)
	{
		if (M_WipeBuffer(thisMenuKey, curpd->keyhandler))
			return;
		if (curpd->keyhandler(thisMenuKey))
			return;
	}

	if (menustack[0] == MN_PLAYBACK && !con_destlines)
	{
		playback_last_menu_interaction_leveltime = leveltime;
		M_DemoBinds(thisMenuKey);
	}

	if (item == NULL)
	{
		M_HandleGoBack(thisMenuKey);
		return;
	}

	// string cvars accept any keyboard input
	if (item->cvar && !item->cvar->PossibleValue)
	{
		// If we're hovering over a IT_CV_STRING option, pressing A/X opens the typing submenu
		if (M_MenuButtonPressed(pid, MBT_CONFIRM))
		{
			if (item->cvar->flags & CV_CALL && M_WipeBuffer(thisMenuKey, MR_Dummy))
				return;

			menutyping.keyboardtyping = thisMenuKey != 0 ? true : false;	// If we entered this menu by pressing a menu Key, default to keyboard typing, otherwise use controller.
			M_OpenVirtualKeyboard(&menuinput, menu_text_input_buf, item);
			return;
		}
	}

	if (M_HandleGoBack(thisMenuKey))
	{
		return;
	}

	// HMMM WHERE WAS THIS COPY PASTED FROM, I WONDER
	int32_t hmove = 0, vmove = 0;

	vmove = menucmd[pid].dpad_ud;
	hmove = menucmd[pid].dpad_lr;

	// Flip left/right with up/down for the playback menu, since it's a horizontal icon row.
	if (curpd->horizontal)
	{
		int32_t tmp = hmove;
		hmove = vmove;
		vmove = tmp;
	}

	size_t oldPanelOn = panelOn;
	size_t newPanelOn = panelOn;
	uint16_t oldItemOn = curpi->itemon; // prevent infinite loop
	int32_t newItemOn = curpi->itemon;
	uint16_t altScrollOn = curpi->itemon;

	// Keys usable within menu
	if (vmove != 0)
	{
		int32_t dir = intsign(vmove);
		do
		{
			if (curpd->multinav)
			{
				if (newItemOn + dir >= curpi->numitems)
					newItemOn = curpi->numitems - (curpi->numitems % curpi->navitemcount);
				else
					newItemOn = newItemOn - (newItemOn % curpi->navitemcount) + intmod(newItemOn + dir, curpi->navitemcount);
				newItemOn = min(newItemOn, curpi->numitems - 1);
			}
			else
			{
				// i don't care anymore, get me out of this hell already
				// ...WOAH! i decided to use my brain for five seconds and look, scroll stops aren't broken now!
				if (curpi->altscroll)
				{
					altScrollOn = newItemOn;
					if (newItemOn + dir < 0 || (dir > 0 && M_GetItemAbsY(curpi, curpi->numitems-1) - M_GetItemAbsY(curpi, newItemOn) <= curpi->scrollheight))
						break;
				}
				newItemOn += dir;
			}

			newPanelOn = panelOn;
			if (curpi->disabled)
			{
				// can't change panels here or we'll get stuck in an infinite loop...
			}
			else if (newItemOn >= curpi->numitems)
			{
				do
					if (++panelOn == numcurpanels) panelOn = 0;
					while (panelOn != newPanelOn && curpanels[panelOn].disabled);
					newItemOn -= curpi->numitems == 0 ? newItemOn : curpi->numitems;
			}
			else if (newItemOn < 0)
			{
				do
					if (panelOn-- == 0) panelOn = numcurpanels - 1;
					while (panelOn != newPanelOn && curpanels[panelOn].disabled);
			}
			curpi = &curpanels[panelOn];
			curpd = M_GetDefFromInst(curpi);
			newItemOn = curpi->numitems == 0 ? 0 : intmod(newItemOn, curpi->numitems);
		}
		while (!(oldPanelOn == panelOn && oldItemOn == newItemOn)
			&& (curpi->numitems == 0
			|| (curpi->altscroll ? M_GetItemAbsY(curpi, altScrollOn) == M_GetItemAbsY(curpi, newItemOn) : !M_ItemSelectable(&curpi->items[newItemOn]))
			|| (vmove -= intsign(vmove)) != 0));

		M_RawSetItemOn(curpi, newItemOn);
		if (curpd->keyhandler == MR_HandleLevelPlatter)
		{
			curpd->keyhandler(thisMenuKey); // dumb hack until i figure this out
			M_SetMenuDelay(pid);
			return;
		}
		else if (oldPanelOn != panelOn || oldItemOn != newItemOn)
		{
			S_StartSound(NULL, sfx_menu1);
			M_SetMenuDelay(pid);
		}
		return;
	}
	else if (hmove != 0)
	{
		if (curpd->multinav)
		{
			int32_t dir = intsign(hmove) * curpi->navitemcount;
			do
			{
				newItemOn += dir;
				if (newItemOn >= curpi->numitems)
				{
					newItemOn %= curpi->navitemcount;
				}
				else if (newItemOn < 0)
				{
					newItemOn = curpi->numitems - (curpi->numitems % curpi->navitemcount) + curpi->navitemcount + newItemOn;
					if (newItemOn >= curpi->numitems) // wrap around properly if the last column isn't full
						newItemOn -= curpi->navitemcount;
				}
				newItemOn = CLAMP(newItemOn, 0, curpi->numitems - 1);
			}
			while (oldItemOn != newItemOn && !M_ItemSelectable(&curpi->items[newItemOn]));

			M_RawSetItemOn(curpi, newItemOn);
			S_StartSound(NULL, sfx_menu1);
			M_SetMenuDelay(pid);
			return;
		}
		else
		{
			if (!(item->cvar || item->status & IT_ARROWS) || item->cvar == &cv_nextmap || item->cvar == &cv_nextcup)
				return;

			if (item->cvar && !item->cvar->PossibleValue)
				return;

			if (M_WipeBuffer(thisMenuKey, item->cvar ? (item->cvar->flags & CV_CALL ? MR_Dummy : NULL) : item->routine))
				return;

			if (menustack[0] != MN_OP_SOUND || curpi->itemon > 3)
				S_StartSound(NULL, sfx_menu1);

			if (item->cvar)
				M_ChangeCvar(item, hmove);
			else
				item->routine(hmove > 0 ? 1 : 0);

			M_SetMenuDelay(pid);
			return;
		}
	}
	else
	{
		if (M_MenuButtonPressed(0, MBT_CONFIRM))
		{
			M_DoItemRoutine(item, thisMenuKey);
			return;
		}
		else if (M_MenuButtonPressed(0, MBT_INFO2))
		{
			if (item->routine == MR_ChangeControl)
			{
				G_ClearControlKeys(setupcontrols, item->argument);
				S_StartSound(NULL, sfx_shldls);
				M_SetMenuDelay(pid);
				return;
			}

			if (item->cvar == NULL
				|| item->cvar == &cv_chooseskin
				|| item->cvar == &cv_dummystaff
				|| item->cvar == &cv_nextmap
				|| item->cvar == &cv_nextcup
				|| item->cvar == &cv_newgametype
				|| item->cvar == &cv_dummymultiplayer)
				return;

			if (item->cvar->flags & CV_CALL && M_WipeBuffer(thisMenuKey, MR_Dummy))
				return;

			if (menustack[0] != MN_OP_SOUND || curpi->itemon > 3)
				S_StartSound(NULL, sfx_menu1);
			M_ResetCvar(item);

			M_SetMenuDelay(pid);
			return;
		}
	}
}

static void M_HutCheckReplays(size_t maxnum);

void M_Ticker(void)
{
	size_t i;
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

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		if (menucmd[i].delay > 0)
		{
			menucmd[i].delay--;
		}
	}

	if (pausehack > 0)
	{
		pausehack--;
	}

	if (noFurtherInput <= 0)
	{
		M_HandleMenuInput();
	}

	if (menustack[0])
	{
		for (i = 0; i < numcurpanels; i++)
		{
			menupaneldef_t *pd = M_GetDefFromInst(&curpanels[i]);

			if (pd == NULL)
				continue;

			if (pd->tickroutine == NULL)
				continue;

			pd->tickroutine(&curpanels[i]);
		}
	}

	if (currentMenu != NULL && currentMenu->tickroutine != NULL)
	{
		currentMenu->tickroutine(0);
	}

	if (dedicated)
	{
		return;
	}

	if (--skullAnimCounter <= 0)
		skullAnimCounter = 8;

	menutimer++;
	cursortimer++;

	if (menustack[0] == MN_PLAYBACK)
	{
		if (playback_enterheld > 0)
			playback_enterheld--;
	}
	else
		playback_enterheld = 0;

	// Hide some options based on the current render mode
#if 0
#ifdef HWRENDER
	M_SetItemVisible(MN_OP_VIDEO, "OPENGL", rendermode == render_opengl);
#endif
	//M_SetItemVisible(MN_OP_VIDEO, "PARALLEL", rendermode == render_soft);
#endif

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

	if (gamestate == GS_MENU)
	{
		if (M_AttackOnStack())
			modeattacking = ATTACKING_MENU;
		else
			modeattacking = ATTACKING_NONE;
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

	CV_RegisterVar(&cv_search_oldinput);

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
	CV_RegisterVar(&cv_dummyaddonsearch);
	CV_RegisterVar(&cv_dummyreplaysearch);
	CV_RegisterVar(&cv_dummyreplaysave);

	quitmsg[QUITMSG] = M_GetText("Eggman's tied explosives\nto your girlfriend, and\nwill activate them if\nyou press the 'Y' key!\nPress 'N' to save her!\n");
	quitmsg[QUITMSG1] = M_GetText("What would Tails say if\nhe saw you quitting the game?\n");
	quitmsg[QUITMSG2] = M_GetText("Hey!\nWhere do ya think you're goin'?\n");
	quitmsg[QUITMSG3] = M_GetText("Forget your studies!\nPlay some more!\n");
	quitmsg[QUITMSG4] = M_GetText("You're trying to say you\nlike Sonic R better than\nthis, aren't you?\n");
	quitmsg[QUITMSG5] = M_GetText("Don't leave yet -- there's a\nsuper emerald around that corner!\n");
	quitmsg[QUITMSG6] = M_GetText("You'd rather work than play?\n");
	quitmsg[QUITMSG7] = M_GetText("Go ahead and leave. See if I care...\n*sniffle*\n");

	quitmsg[QUIT2MSG] = M_GetText("If you leave now,\nEggman will take over the world!\n");
	quitmsg[QUIT2MSG1] = M_GetText("On your mark,\nget set,\nhit the 'N' key!\n");
	quitmsg[QUIT2MSG2] = M_GetText("Aw c'mon, just\na few more laps!\n");
	quitmsg[QUIT2MSG3] = M_GetText("Did you get all those Chaos Emeralds?\n");
	quitmsg[QUIT2MSG4] = M_GetText("If you leave, I'll use\nmy Jawz on you!\n");
	quitmsg[QUIT2MSG5] = M_GetText("Don't go!\nYou might find the hidden\nlevels!\n");
	quitmsg[QUIT2MSG6] = M_GetText("Hit the 'N' key, Sonic!\nThe 'N' key!\n");

	quitmsg[QUIT3MSG] = M_GetText("Are you really going to give up?\nWe certainly would never give you up.\n");
	quitmsg[QUIT3MSG1] = M_GetText("Come on, just ONE more netgame!\n");
	quitmsg[QUIT3MSG2] = M_GetText("Press 'N' to unlock\nthe Golden Kart!\n");
	quitmsg[QUIT3MSG3] = M_GetText("Couldn't handle\nthe banana meta?\n");
	quitmsg[QUIT3MSG4] = M_GetText("Every time you press 'Y', an\nSRB2Kart Developer cries...\n");
	quitmsg[QUIT3MSG5] = M_GetText("You'll be back to play soon, though...\n...right?\n");
	quitmsg[QUIT3MSG6] = M_GetText("Aww, is Eggman's Nightclub too\ndifficult for you?\n");

	CV_RegisterVar(&cv_serversort);
	CV_RegisterVar(&cv_menucaps);
	CV_RegisterVar(&cv_replaysearchrate);

	CV_RegisterVar(&cv_debugpanels);

	CV_RegisterVar(&cv_debugvirtualkeyboard);
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
static void M_DrawSplitText(const char *text, int32_t x, int32_t y, int32_t option, int32_t alpha)
{
	char* icopy = strdup(text);
	char** clines = NULL;
	int16_t num_lines = 0;

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

	int16_t yoffset;
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
//  Draw a textbox, like Quake does, because sometimes it's difficult
//  to read the text with all the stuff in the background...
//
void M_DrawTextBox(int32_t x, int32_t y, int32_t width, int32_t boxlines, int32_t flags)
{
	// Solid color textbox.
	V_DrawFill(x+5, y+5, width*8+6, boxlines*8+6, 159|flags);
}

static void M_DrawMenuTitle(void)
{
	if (currentMenu->headerpic)
	{
		patch_t *p = W_CachePatchName(currentMenu->headerpic, PU_CACHE);

		if (p->height > 24) // title is larger than normal
		{
			int32_t xtitle = (BASEVIDWIDTH - (LSBF_SHORT(p->width)/2))/2;
			int32_t ytitle = (30 - (LSBF_SHORT(p->height)/2))/2;

			if (xtitle < 0)
				xtitle = 0;
			if (ytitle < 0)
				ytitle = 0;

			V_DrawSmallScaledPatch(xtitle, ytitle, 0, p);
		}
		else
		{
			int32_t xtitle = (BASEVIDWIDTH - LSBF_SHORT(p->width))/2;
			int32_t ytitle = (30 - LSBF_SHORT(p->height))/2;

			if (xtitle < 0)
				xtitle = 0;
			if (ytitle < 0)
				ytitle = 0;

			V_DrawScaledPatch(xtitle, ytitle, 0, p);
		}
	}
}

#define SLIDER_RANGE (10*8)
static void M_DrawSliderCursor(fixed_t x, fixed_t y, int32_t vflags, consvar_t *cv, int32_t value)
{
	fixed_t range = FixedDiv(value - cv->PossibleValue[0].value, cv->PossibleValue[1].value - cv->PossibleValue[0].value);
	V_DrawSciencePatch(x - 4*FRACUNIT + (SLIDER_RANGE+4)*range, y, vflags, W_CachePatchName("M_SLIDEC", PU_CACHE), FRACUNIT);
}

static fixed_t M_GetAlignment(const menuitempart_t *part, fixed_t x, fixed_t width)
{
	switch (part->align)
	{
	case ITA_LEFT:
		return 0;
	case ITA_CENTER:
		return width/2;
	case ITA_RIGHT:
		return width;
	case ITA_EDGE:
		return width - (BASEVIDWIDTH*FRACUNIT - x*2);
	}

	return INT32_MAX;
}

static const char *M_GetPartString(const menuitempart_t *part, const menuitem_t *item, itempartsource_e source)
{
	switch (source)
	{
	case ITS_TEXT:
		return item->text;
	case ITS_PATCH:
		return item->patch;
	case ITS_EXTRA:
		return item->extra;
	case ITS_FIXED:
		return part->fixedstring;
	case ITS_FIXED2:
		return part->fixedstring2;
	case ITS_DERIVED:
		I_Error("ITS_DERIVED is the caller's problem, silly");
	}

	return NULL;
}

static const menuitemstyle_t defaultstyle = { .partenabled = { true } };

// draws a menu item, returns cursor offset
static fixed_t M_DrawMenuItem(const menupanelinst_t *pi, menuitem_t *item, fixed_t x, fixed_t y, int32_t vflags, dboolean selected)
{
	fixed_t width = 0;
	fixed_t retwidth = 0;
	const menuitemstyle_t *style = item->style;
	if (style == NULL)
		style = &defaultstyle;

	for (size_t i = 0; i < MAXMENUITEMPARTS; i++)
	{
		if (style->partenabled[i] == false)
			continue;

		const menuitempart_t *part = &style->parts[i];
		const menuitempart_t *parent = &menuitemstyles[style->parent].parts[i];
		const char *string, *string2;

		{
			const menuitempart_t *which;
			which = part->source == ITS_DERIVED ? parent : part;
			string = M_GetPartString(which, item, which->source);
			which = part->source2 == ITS_DERIVED ? parent : part;
			string2 = M_GetPartString(which, item, which->source2);
		}

		if (string == NULL)
			continue;

		fixed_t scale = part->small ? FRACUNIT/2 : FRACUNIT;
		int32_t colorflag;

		if (item->status & IT_SECRET)
		{
			string = M_CreateSecretMenuOption(string);
			vflags |= V_TRANSLUCENT|V_OLDSPACING;
		}
		else if (item->status & IT_GRAYEDOUT || part->translucent)
			vflags |= V_TRANSLUCENT;

		if (selected && part->type == ITT_STRING && !part->nohighlight)
		{
			colorflag = highlightflags;
		}
		else switch (part->highlight)
		{
		case ITH_NONE:
			colorflag = 0; break;
		case ITH_HIGHLIGHT:
			colorflag = highlightflags; break;
		case ITH_RECOMMENDED:
			colorflag = recommendedflags; break;
		case ITH_WARNING:
			colorflag = warningflags; break;
		}

		patch_t *p = NULL;
		uint8_t *cmap = NULL;
		int32_t num;
		fixed_t stringwidth = 0;
		fixed_t stringscale = 0;

		switch (part->type)
		{
		case ITT_STRING:
			switch (part->font)
			{
			case HU_FONT:
			case TINY_FONT:
			case CRED_FONT:
				stringscale = FRACUNIT;
				break;
			default:
				I_Error("unknown font %d", part->font);
				break;
			}

			if (part->fontalt)
				vflags |= V_6WIDTHSPACE;

			stringwidth = V_StringScaledWidth(stringscale, stringscale, stringscale, vflags, part->font, string);

			width = M_GetAlignment(part, x, stringwidth);

			V_DrawStringScaled(x - width + part->xofs, y + part->yofs, scale, FRACUNIT, FRACUNIT, colorflag|vflags, part->font, string);
			retwidth = max(retwidth, width);
			break;

		case ITT_FILL:
			;
			fixed_t fillw, fillh;
			uint8_t fillc;
			// TODO: these can overflow, reimplement sscanf to fix i guess
			int results = sscanf(string, "%dx%d,%hhu", &fillw, &fillh, &fillc);
			if (results != 3)
				break;

			fillw *= FRACUNIT;
			fillh *= FRACUNIT;

			if (colorflag != 0)
				fillc = V_GetStringColormap(colorflag)[fillc];

			width = M_GetAlignment(part, x, fillw);
			V_DrawFixedFill(x - width + part->xofs, y + part->yofs, fillw, fillh, fillc|vflags);
			retwidth = max(retwidth, width);
			break;

		case ITT_PATCH:
			p = W_CachePatchName(string, PU_CACHE);
			cmap = V_GetStringColormap(colorflag);
		drawpatch:
			width = M_GetAlignment(part, x, p->width*FRACUNIT);
			V_DrawFixedPatch(x - width + part->xofs, y + part->yofs, scale, vflags & ~V_FLIP, p, cmap);
			retwidth = max(retwidth, width);
			if (part->type == ITT_SKINICON && part->highlight == ITH_HIGHLIGHT)
			{
				p = W_CachePatchName(va("K_%cHILI%d", part->small ? 'C' : 'B', (menutimer/4 % 8) + 1), PU_CACHE);
				V_DrawSciencePatch(x - width + part->xofs, y + part->yofs, vflags & ~V_FLIP, p, FRACUNIT);
			}
			break;

		case ITT_THUMBNAIL:
			if (fastcmp(string, "$"))
				num = NEXTMAP_INVALID; // random
			else if (fastcmp(string, "?"))
				num = NEXTMAP_RESERVED; // unknown
			else
				num = G_MapNumber(string);
			scale = M_GetMapThumbnail(num, &p)/4;
			goto drawpatch;

		case ITT_SKINICON:
			num = R_SkinAvailable(string);
			scale = FRACUNIT;
			if (num < 0)
				p = W_CachePatchName(part->small ? "M_NORANK" : "M_NOWANT", PU_CACHE);
			else
				p = faceprefix[num][part->small ? FACE_RANK : FACE_WANTED];

			if (item->status & IT_GRAYEDOUT)
			{
				// viewserver...
				cmap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_GREY, GTC_MENUCACHE);
			}
			else
			{
				// replay hut...
				skincolornum_t color = string2 != NULL ? DEH_FindSkincolor(string2) : MAXSKINCOLORS;
				if (color == MAXSKINCOLORS)
					color = 0;
				cmap = R_GetTranslationColormap(num < 0 ? TC_RAINBOW : num, color, GTC_MENUCACHE);
			}

			goto drawpatch;
		}
	}

	// TODO: this really should be part of the style... somehow?
	if (item->cvar != NULL && !(item->status & (IT_SECRET|IT_GRAYEDOUT)))
	{
		consvar_t *cv = item->cvar;
		if (!cv->PossibleValue) // string cvar?
		{
			if (pi->scrollheight && y + 12*FRACUNIT > pi->y + pi->scrollheight)
				return retwidth;

			// see M_ItemExtraOffset
			dboolean side = cv == &cv_dummyname || cv == &cv_playername[0];
			int32_t xofs = side ? 32*FRACUNIT : 0;
			int32_t yofs = side ? -8*FRACUNIT : 4*FRACUNIT;

			M_DrawTextBox((x + xofs)/FRACUNIT, (y + yofs)/FRACUNIT, M_StringCvarLength(cv), 1, 0);
			V_DrawStringAtFixed(x + xofs + 8*FRACUNIT, y + yofs + 8*FRACUNIT, vflags|V_ALLOWLOWERCASE, cv->string);
		}
		else if (item->status & IT_SLIDER)
		{
			int32_t i;
			patch_t *p;

			vflags &= ~(V_FLIP|V_PARAMMASK);
			x = BASEVIDWIDTH*FRACUNIT - x - SLIDER_RANGE*FRACUNIT - 6*FRACUNIT;

			p = W_CachePatchName("M_SLIDEL", PU_CACHE);
			V_DrawSciencePatch(x - 8*FRACUNIT, y, vflags, p, FRACUNIT);

			p = W_CachePatchName("M_SLIDEM", PU_CACHE);
			for (i = 0; i < SLIDER_RANGE; i += 8)
				V_DrawSciencePatch(x + i*FRACUNIT, y, vflags, p, FRACUNIT);

			p = W_CachePatchName("M_SLIDER", PU_CACHE);
			V_DrawSciencePatch(x + i*FRACUNIT, y, vflags, p, FRACUNIT);

			if (atoi(cv->defaultvalue) != cv->value)
				M_DrawSliderCursor(x, y, vflags|V_TRANSLUCENT, cv, atoi(cv->defaultvalue));

			M_DrawSliderCursor(x, y, vflags, cv, cv->value);

			if (selected)
			{
				V_DrawFixedCharacter(x - 16*FRACUNIT - (skullAnimCounter/5)*FRACUNIT, y,
									 '\x1C' | highlightflags | vflags, false); // left arrow
				V_DrawFixedCharacter(x + SLIDER_RANGE*FRACUNIT + 8*FRACUNIT + (skullAnimCounter/5)*FRACUNIT, y,
									 '\x1D' | highlightflags | vflags, false); // right arrow

				fixed_t range = FixedDiv(atoi(cv->defaultvalue) - cv->PossibleValue[0].value, cv->PossibleValue[1].value - cv->PossibleValue[0].value);
				V_DrawCenteredStringAtFixed(x - 4*FRACUNIT + (SLIDER_RANGE+4)*range, y, V_30TRANS,
									 (cv->flags & CV_FLOAT) ? va("%.2f", FIXED_TO_FLOAT(cv->value)) : va("%d", cv->value));
			}
		}
		else
		{
			const char *str = cv->string;
			dboolean warning = (cv->flags & CV_CHEAT) && !CV_IsSetToDefault(cv);

			if (cv == &cv_chooseskin)
				str = skins[cv_chooseskin.value]->realname;
			else if (cv == &cv_dummyfollower)
				str = cv_dummyfollower.value == -1 ? "None" : followers[cv_dummyfollower.value].name;
			else if (cv == &cv_dummystaff && cv_dummystaff.value)
				str = dummystaffname;
			else if (cv == &cv_dummycolor)
				str = skincolors[cv_dummycolor.value].name;
			else if (cv == &cv_dummyserverpage)
				str = va("%d of %d", cv->value + 1, dummyserverpage_cons_t[1].value + 1);
			else if (cv == &cv_soundtest && cv_soundtest.value)
				V_DrawRightAlignedStringAtFixed(BASEVIDWIDTH*FRACUNIT - x, y + 8*FRACUNIT, highlightflags|vflags, S_sfx[cv_soundtest.value].name);
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
				return retwidth;

			if (menustack[0] == MN_MP_PLAYERSETUP)
				vflags |= V_ALLOWLOWERCASE;

			fixed_t w = V_StringWidth(str, vflags)*FRACUNIT;
			V_DrawStringAtFixed(BASEVIDWIDTH*FRACUNIT - x - w, y, (warning ? warningflags : highlightflags)|vflags, str);

			if (selected && cv != &cv_nextmap && cv != &cv_nextcup)
			{
				vflags &= ~(V_FLIP|V_PARAMMASK);
				V_DrawFixedCharacter(BASEVIDWIDTH*FRACUNIT - x - 10*FRACUNIT - w - (skullAnimCounter/5)*FRACUNIT, y,
					'\x1C' | highlightflags | vflags, false); // left arrow
				V_DrawFixedCharacter(BASEVIDWIDTH*FRACUNIT - x + 2*FRACUNIT + (skullAnimCounter/5)*FRACUNIT, y,
					'\x1D' | highlightflags | vflags, false); // right arrow
			}
		}
	}

	return retwidth;
}

// returns 16 if this item draws a cvar string input. yep, that's it!
static fixed_t M_ItemExtraOffset(const menuitem_t *item)
{
	// see M_DrawRightString
	dboolean side = item->cvar == &cv_dummyname || item->cvar == &cv_playername[0];
	return item->cvar && !item->cvar->PossibleValue && !side ? 16*FRACUNIT : 0;
}

// gets the absolute Y coordinate where this menuitem should be drawn
// TODO: merge this with the actual drawing loop somehow
static fixed_t M_GetItemAbsY(const menupanelinst_t *pi, size_t index)
{
	size_t i;
	fixed_t y = 0;
	menupaneldef_t *pd = M_GetDefFromInst(pi);
	for (i = 0; i < pi->numitems; i++)
	{
		const menuitem_t *item = &pi->items[i];

		if (item->status & IT_HIDDEN)
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

		y += pd->lineheight*FRACUNIT + M_ItemExtraOffset(item);
	}

	return y;
}

static int64_t M_GetItemAbsY_64(const menupanelinst_t *pi, size_t index)
{
	size_t i;
	int64_t y = 0;
	menupaneldef_t *pd = M_GetDefFromInst(pi);
	for (i = 0; i < pi->numitems; i++)
	{
		const menuitem_t *item = &pi->items[i];

		if (item->status & IT_HIDDEN)
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

		y += pd->lineheight*FRACUNIT + M_ItemExtraOffset(item);
	}

	return y;
}

static void M_UpdateStyleFixedStrings(void)
{
	menuitemstyle_t *style;
	menupanelinst_t *curpi = &curpanels[panelOn];

	style = DEH_FindMenuItemStyle("VIDEOMODE");
	if (style != NULL)
	{
		Z_Free(style->parts[1].fixedstring);
		style->parts[1].fixedstring = Z_StrDup(va("%dx%d", vid.width, vid.height));
		style->parts[1].highlight = SCR_IsAspectCorrect(vid.width, vid.height) ? ITH_RECOMMENDED : ITH_HIGHLIGHT;
	}

	style = DEH_FindMenuItemStyle("JOYSTICKNAME");
	if (style != NULL && setupcontrolplayer > 0)
	{
		int32_t stick = cv_usecontroller[setupcontrolplayer-1].value;
		const char *name = stick == 0 ? "None" : I_GetControllerName(stick);
		if (!name) name = "Unavailable";
		Z_Free(style->parts[1].fixedstring);
		style->parts[1].fixedstring = Z_StrDup(name);
		style->parts[1].font = V_StringWidth(name, 0) > BASEVIDWIDTH/2 - 4 ? TINY_FONT : HU_FONT;
	}

#ifdef HAVE_DISCORDRPC
	style = DEH_FindMenuItemStyle("DISCORDREQUEST");
	if (style != NULL)
	{
		const tic_t freq = TICRATE/2;
		style->partenabled[1] = discordRequestList != NULL && (menutimer % freq) >= freq/2;
	}
#endif

	for (int32_t i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		style = DEH_FindMenuItemStyle(va("P%cWANTED", i + '1'));
		if (style == NULL)
			continue;

		Z_Free(style->parts[0].fixedstring);
		style->parts[0].fixedstring = Z_StrDup(R_SkinAvailable(cv_skin[i].string) != -1 ? cv_skin[i].string : "sonic");
		Z_Free(style->parts[0].fixedstring2);
		style->parts[0].fixedstring2 = Z_StrDup(DEH_SkincolorName(cv_playercolor[i].value));

		style = DEH_FindMenuItemStyle(va("P%cMULTIPLAYER", i + '1'));
		if (style == NULL)
			continue;

		menuitem_t *psetup = M_CheckMenuItem(curpi, "PLAYERSETUP");
		style->parts[0].translucent = i >= cv_splitplayers.value;
		style->parts[0].highlight = psetup != NULL && curpi->itemon == psetup - curpi->items && i == cv_dummymultiplayer.value ? ITH_HIGHLIGHT : ITH_NONE;
	}
}

static void M_DrawMenuPanel(menupanelinst_t *pi, dboolean selected)
{
	const menupaneldef_t *pd = M_GetDefFromInst(pi);
	int32_t pflags = pi->vflags & ~V_FLIP;

	if (pi->visible == false || cv_debugpanels.value == 1)
		goto justdrawroutine;

	if (pi->cliprect)
	{
		fixed_t x_clip = pi->cliprect_relative ? pi->x : pi->cliprect_x;
		fixed_t y_clip = pi->cliprect_relative ? pi->y : pi->cliprect_y;

		V_SetClipRect(x_clip, y_clip, pi->cliprect_width, pi->cliprect_height, pflags);
	}

	fixed_t scrollx = pi->x;
	int64_t scrolly = pi->y;
	fixed_t scrollheight = pi->scrollheight;
	if (!scrollheight)
		scrollheight = INT32_MAX;

	dboolean hidecursor = !selected || pi->numitems == 0 || pd->drawroutine == MD_DrawMonitorToggles || pd->drawroutine == MD_DrawLevelPlatterMenu;

	int64_t topy = M_GetItemAbsY(pi, 0);
	int64_t boty = M_GetItemAbsY_64(pi, pi->numitems-1);

	if (pi->altscroll || (selected && boty - topy > scrollheight))
	{
		scrolly -= M_GetItemAbsY_64(pi, pi->itemon) - (pi->altscroll ? 0 : scrollheight/2);

		const int64_t topbound = ((int64_t)(pi->y) - topy);
		const int64_t bottombound = ((int64_t)(pi->y) - boty + scrollheight);

		if (pi->altscroll)
			;
		else if (scrolly > topbound)
			scrolly = topbound;
		else if (scrolly < bottombound)
			scrolly = bottombound;
	}

	fixed_t scrolly_fixed = (fixed_t)(scrolly);

	// draw title (or big pic)
	M_DrawMenuTitle();

	size_t i;
	fixed_t w, x = scrollx;
	int64_t y = scrolly;
	fixed_t cursorx = 0, cursory = 0;
	dboolean clipbot = false, cliptop = false;

	// levelselect no longer needs a special drawer!
	for (i = 0; i < pi->numitems; i++)
		if (pi->items[i].cvar == &cv_nextmap)
		{
			patch_t *patch;
			fixed_t scale = M_GetMapThumbnail(cv_nextmap.value - 1, &patch);
			V_DrawSciencePatch(BASEVIDWIDTH*FRACUNIT - patch->width*scale/4 - pi->x,
			                  scrolly_fixed + M_GetItemAbsY(pi, i) + 35*FRACUNIT - patch->height*scale/8,
			                  pflags, patch, scale/4);
			break;
		}

	// FIXME: yep, control update is shoved here
	// binds can change at any time, and the controls menu doesn't have a drawroutine, so...
	for (i = 0; i < pi->numitems; i++)
	{
		menuitem_t *item = &pi->items[i];
		if (item->routine != MR_ChangeControl)
			continue;

		char tmp[32*MAXINPUTMAPPING]; // should be enough :^)
		int32_t key;

		tmp[0] ='\0';
		for (int32_t j = 0; j < MAXINPUTMAPPING; j++)
		{
			key = setupcontrols[item->argument][j];
			if (key != KEY_NULL)
			{
				if (tmp[0] != '\0')
					strcat(tmp, ", ");
				strcat(tmp, G_KeynumToString(key));
			}
		}
		if (tmp[0] == '\0')
			strcpy(tmp, "---");

		item->style = DEH_FindMenuItemStyle(V_StringWidth(tmp, 0) > BASEVIDWIDTH/2 - 4 ? "CHANGECONTROLNAMELONG" : "CHANGECONTROLNAME");
		M_SetItemPatchRaw(item, tmp); // the fragmentation is INSANE, look at the reserved bytes :face_holding_back_tears:
	}

	M_UpdateStyleFixedStrings();

	for (i = 0; i < pi->numitems; i++)
	{
		fixed_t dx;
		int64_t dy;
		menuitem_t *item = &pi->items[i];

		if (item->status & IT_HIDDEN)
			continue;

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

		if (scrollheight != INT32_MAX && dy < pi->y)
		{
			cliptop = true;
			goto nodraw;
		}
		else if (dy > (int64_t)pi->y + scrollheight)
		{
			clipbot = true;
			goto nodraw;
		}

		w = M_DrawMenuItem(pi, item, dx, (fixed_t)(dy), pi->vflags | MENUCAPS, !hidecursor && item->status & IT_INTERACT && i == pi->itemon);

		if (!hidecursor && i == pi->itemon)
		{
			cursory = (fixed_t)(y);
			cursorx = x - w - 24*FRACUNIT + pi->cursoroffset;
		}

	nodraw:
		y += pd->lineheight*FRACUNIT + M_ItemExtraOffset(item);
	}

	fixed_t yfx = (fixed_t)(y);

	dboolean wipebuffer = (noFurtherInput > 0 || menucmd[0].wipebuffer);

	// DRAW THE SKULL CURSOR
	if (!hidecursor && M_ItemSelectable(&pi->items[pi->itemon]))
	{
		const char *pname = "M_CURSOR";
		uint8_t *cmap = NULL;
		fixed_t scale = FRACUNIT;

		if (pi->items[pi->itemon].cvar && !pi->items[pi->itemon].cvar->PossibleValue)
		{
			if (wipebuffer)
				cursorx += (5*FRACUNIT);
			else
				cursorx += (5*FRACUNIT) + (skullAnimCounter/5)*FRACUNIT;

			V_DrawFixedCharacter(cursorx, cursory,
							'\x1D' | highlightflags, false); // right arrow
		}
		else
		{
			if (pd->drawroutine == MD_DrawAddons)
			{
				pname = "M_FSEL";
				cursorx += 15*FRACUNIT;
				scale /= 2;
				if (skullAnimCounter < 4)
					cmap = V_GetStringColormap(highlightflags);
			}

			V_DrawFixedPatch(cursorx, cursory, scale, (wipebuffer ? V_TRANSLUCENT : 0) | pflags, W_CachePatchName(pname, PU_CACHE), cmap);
		}
	}
	if (wipebuffer)
		V__DrawOneScaleString(cursorx, cursory + 3*FRACUNIT, FRACUNIT/2, pflags, HU_FONT, "WAIT");

	// Draw scrollbar
	if (pd->scrollbar == true)
	{
		int64_t bh = (vid.scaledheight - 80)*FRACUNIT;
		x = (BASEVIDWIDTH - 4)*FRACUNIT;
		yfx = 75*FRACUNIT;
		int64_t barheight = max(FRACUNIT,FixedDiv(FixedMul(bh, bh) - 1, boty) - FRACUNIT);
		if (boty > bh)
		{
			boty += scrollheight + 10*FRACUNIT;
			V_DrawFixedFill(x, yfx, 4*FRACUNIT, bh, V_SNAPTOTOP|V_SNAPTORIGHT|159);
			x += FRACUNIT, yfx += FRACUNIT;
			V_DrawFixedFill(x, yfx + FixedMul(bh, FixedDiv(M_GetItemAbsY(pi, pi->itemon), boty)), 2*FRACUNIT, barheight, V_SNAPTOTOP|V_SNAPTORIGHT|149);
		}
	}
	else
	{
		pflags |= highlightflags;
		x = pi->x - 20*FRACUNIT + pi->cursoroffset;
		if (cliptop)
			V_DrawCharacter(x/FRACUNIT, pi->y/FRACUNIT - (skullAnimCounter/5), '\x1A' | pflags, false); // up arrow
		if (clipbot)
			V_DrawCharacter(x/FRACUNIT, pi->y/FRACUNIT + scrollheight/FRACUNIT + (skullAnimCounter/5), '\x1B' | pflags, false); // down arrow

		// i hate custom scrollbars
		if (pd->drawroutine == MD_DrawAddons)
		{
			int64_t m = BASEVIDHEIGHT*FRACUNIT - (pi->y - FRACUNIT)*2;
			if (pi->numitems <= (2*numaddonsshown + 1))
				i = 0;
			else
			{
				int64_t q = m;
				m = ((2*numaddonsshown + 1) * m)/pi->numitems;
				if (pi->itemon <= numaddonsshown) // all the way up
					i = 0;
				else if (pi->numitems <= (pi->itemon + numaddonsshown + 1)) // all the way down
					i = q-m;
				else
					i = ((pi->itemon - numaddonsshown) * (q-m))/(pi->numitems - (2*numaddonsshown + 1));
			}

			V_DrawFixedFill(pi->x + (MAXSTRINGLENGTH*8+5 - 21)*FRACUNIT, (pi->y - FRACUNIT) + i, FRACUNIT, m, V_GetStringColormap(highlightflags)[0]);
		}
	}

justdrawroutine:
	if (cv_debugpanels.value != 2 && pd->drawroutine)
		pd->drawroutine(pi);

	if (pi->cliprect)
		V_ClearClipRect();
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
	int32_t j;

	skins_cons_t[1].value = numskins-1;
	dummyfollower_cons_t[1].value = numfollowers-1;
	dummycolor_cons_t[1].value = numskincolors-1;

	j = R_SkinAvailable(cv_skin[0].string);
	if (j == -1)
		j = 0;

	CV_SetValue(&cv_chooseskin, j); // This causes crash sometimes?!

	return;
}

static int32_t M_GetLevelListTOL(void)
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

static dboolean M_LevelAvailableOnPlatter(mapnum_t mapnum)
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
static dboolean M_CanShowLevelOnPlatter(mapnum_t mapnum)
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

static dboolean M_CanShowCupOnPlatter(const cupheader_t *cup)
{
	(void)cup;
	return true;
}

typedef int (qsort_f)(const void *, const void *);

static int Levelsort_Name(const void *a, const void *b)
{
	const int16_t m1 = *(const int16_t *)a, m2 = *(const int16_t *)b;
	int c = strcmp(mapheaderinfo[m1]->lvlttl, mapheaderinfo[m2]->lvlttl);
	return c != 0 ? c : m1 - m2;
}

static int Levelsort_Cup(const void *a, const void *b)
{
	const int16_t m1 = *(const int16_t *)a, m2 = *(const int16_t *)b;

	if (mapheaderinfo[m1]->cup == mapheaderinfo[m2]->cup)
	{
		// if cupless, sort by map ID, otherwise sort by order within the cup
		cupheader_t *cup = mapheaderinfo[m1]->cup;
		if (cup == NULL)
			return m1 - m2;

		int16_t pos1 = -1, pos2 = -1;
		for (int32_t i = 0; i < CUPCACHE_MAX; i++)
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
	const int16_t m1 = *(const int16_t *)a, m2 = *(const int16_t *)b;
	return mapheaderinfo[m1]->lumpnum - mapheaderinfo[m2]->lumpnum;
}

static int Cupsort_Name(const void *a, const void *b)
{
	const cupheader_t *c1 = *(const cupheader_t **)a, *c2 = *(const cupheader_t **)b;
	CLEANUP(pfree) char *name1 = strdup(CupName(c1, false)), *name2 = strdup(CupName(c2, false));

	if (name1 == NULL || name2 == NULL)
	{
		I_Error("Cupsort_Name: Out of memory!\n");
	}

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

static dboolean M_MakeLevelSelectHeader(levelselectrow_t *row, mapheader_t *prev, mapheader_t *iter)
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
	int32_t i, j;
	cupheader_t *cup;
	levelselectrow_t *lsrow;
	cupheader_t *sortedcups[UINT16_MAX]; // cope!
	uint16_t numsortedcups = 0;

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
}

// refresh the level list while the platter is active
static void M_UpdateLevelPlatter(void)
{
	int32_t i, row = 0, headerRow = -1;
	mapheader_t *previtermap = NULL; // prevmap AND lastmap are taken...!
	dboolean forceNewRow = true;
	int16_t sortedmaps[INT16_MAX];
	int16_t numsortedmaps = 0;
	int16_t randommap = -3; // need at least 2 available maps to safely randomize

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

	menupanelinst_t *pi = M_CheckPanel("OPTIONS");
	if (pi != NULL)
		M_SetItemDisabled(pi, "RANDOM", randommap < 0);

	for (i = 0; i < numsortedmaps; i++)
	{
		mapnum_t mapnum = sortedmaps[i];
		mapheader_t *itermap = mapheaderinfo[mapnum];
		dboolean wide = false;//(headermap->menuflags & LF2_WIDEICON);

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
}

// finds a map/cup on the platter
// returns row in low 16 bits, column in upper 16 bits
static int32_t M_FindOnPlatter(int32_t which)
{
	for (int32_t row = 0; row < levelselect.numrows; row++)
	{
		if (levellistmode == LLM_CUPSELECT)
		{
			if (row > 0 && levelselect.rows[row].cupid == which)
			{
				return row;
			}
		}
		else for (int32_t col = 0; col < levelselect.rows[row].numcolumns; col++)
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

int32_t MR_SetLevelListMode(int32_t choice)
{
	if (choice < 0 || choice >= LLM__MAX)
		return false;

	levellistmode = choice;
	return true;
}

void MP_LevelPlatterOptions(menupanelinst_t *pi)
{
	M_SetItemVisible(pi, "GAMETYPE", levellistmode == LLM_CREATESERVER);
	M_SetItemVisible(pi, "ENCORE", levellistmode == LLM_CREATESERVER && M_SecretUnlocked(SECRET_ENCORE));
	M_SetItemVisible(pi, "RANDOM", levellistmode != LLM_CUPSELECT);
}

//
// MR_PrepareLevelPlatter
//
// Prepares a tasty dish of zones and acts!
// Call before any attempt to access a level platter.
//
void MP_PrepareLevelPlatter(menupanelinst_t *pi)
{
	(void)pi;
	if (gamestate == GS_LEVEL)
		CV_SetValue(&cv_newgametype, gametype);

	levelsort_cons_t[LEVELSORT_CUP].strvalue = levellistmode == LLM_CUPSELECT ? NULL : "Cup"; // bruh

	if (levelselect.rows != NULL)
		Z_Free(levelselect.rows);

	memset(&levelselect, 0, sizeof(levelselect));

	M_UpdateLevelPlatter();

	// start on current map for convenience if found and inside level otherwise
	int32_t found = M_FindOnPlatter(gamemap-1);

	// start on nextmap for convenience
	if (gamestate != GS_LEVEL || found == -1)
		found = M_FindOnPlatter(levellistmode == LLM_CUPSELECT ? cv_nextcup.value - 1 : cv_nextmap.value - 1);

	if (found != -1)
	{
		levelselect.row = found & 0xffff;
		levelselect.column = found >> 16;
	}
}

static void M_MoveLevelPlatter(int32_t vmove)
{
	vmove %= levelselect.numrows;
	if (abs(vmove) > levelselect.numrows/2)
		vmove -= levelselect.numrows * intsign(vmove);

	levelselect.offsety = 0;
	for (int32_t count = abs(vmove); count > 0; count--)
	{
		if (vmove < 0)
			levelselect.offsety -= lsvseperation(levelselect.row);
		levelselect.row = intmod(levelselect.row + intsign(vmove), levelselect.numrows);
		if (vmove > 0)
			levelselect.offsety += lsvseperation(levelselect.row);
	}

	// update highlighted header
	int32_t iter = levelselect.row;
	while (levelselect.rows[iter].header[0] == '\0')
	{
		iter = (iter - 1) % levelselect.numrows;
		if (iter == levelselect.row)
			break;
	}
	levelselect.highlight = iter;

	levelselect.namescroll = 0;
}

static void M_LevelPlatterSelect(dboolean delay)
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

int32_t MR_LevelPlatterRandom(int32_t choice)
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
int32_t MR_HandleLevelPlatter(int32_t choice)
{
	int32_t hmove = 0, vmove = 0;

	if (levelselect.selectdelay > 0)
		return choice != KEY_CONSOLE; // let me open the console at least lol

	vmove = menucmd[0].dpad_ud;
	hmove = menucmd[0].dpad_lr;

	if (M_MenuButtonPressed(0, MBT_INFO2))
	{
		int32_t dest;
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
			int32_t found = M_FindOnPlatter(levelselect.returnto);
			if (found == -1)
				return false;
			dest = found & 0xffff;
			levelselect.column = found >> 16;
		}

		if (abs(levelselect.row - dest) > levelselect.numrows/2)
			vmove = dest - levelselect.numrows - levelselect.row;
		else
			vmove = dest - levelselect.row;

		panelOn = M_GetPanel("OPTIONS") - curpanels;

		M_SetMenuDelay(0);
	}
	else if (M_MenuButtonPressed(0, MBT_CONFIRM))
	{
		if (levelselect.row == 0)
			return false;

		M_LevelPlatterSelect(false);

		M_SetMenuDelay(0);
		return true;
	}

	if (levelselect.numrows <= 1)
		vmove = 0;

	if (vmove != 0)
	{
		M_SetMenuDelay(0);
		M_MoveLevelPlatter(vmove);
	}

	if (hmove != 0)
	{
		M_SetMenuDelay(0);

		if (levelselect.rows[levelselect.row].wide)
			return levelselect.row > 0;
		levelselect.namescroll = 0;
		levelselect.column = intmod(levelselect.column + hmove, MAPSPERROW);
		levelselect.offsetx = lshseperation * -hmove;
	}

	if (levelselect.row > 0 && (vmove != 0 || hmove != 0))
	{
		S_StartSoundAtVolume(NULL, sfx_s3kb7, 160);
		return true;
	}

	return false;
}

static void M_DrawLevelPlatterHeader(fixed_t y, const char *header, dboolean headerhighlight, dboolean allowlowercase)
{
	y -= (lsmapheight + lsmapnameheight + FRACUNIT)/2;
	y += lsheadingheight - 12*FRACUNIT;
	V_DrawStringAtFixed(19*FRACUNIT, y, (headerhighlight ? V_YELLOWMAP : 0)|(allowlowercase ? V_ALLOWLOWERCASE : 0), header);
	y += 9*FRACUNIT;
	V_DrawFixedFill(19*FRACUNIT, y, 281*FRACUNIT, FRACUNIT, (headerhighlight ? yellowmap[3] : 3));
	V_DrawFixedFill(300*FRACUNIT, y, FRACUNIT, FRACUNIT, 26);
	y += FRACUNIT;
	V_DrawFixedFill(19*FRACUNIT, y, 282*FRACUNIT, FRACUNIT, 26);
}

// draws a map CENTERED around the given coordinates
static void M_DrawLevelPlatterMap(const levelselectmap_t *lsmap, fixed_t x, fixed_t y, dboolean highlight, dboolean wide)
{
	fixed_t scale = highlight ? FRACUNIT : FRACUNIT/2;
	fixed_t width = wide ? 282*FRACUNIT : FixedMul(lsmapwidth, scale);
	fixed_t height = FixedMul(lsmapheight + lsmapnameheight, scale);

	// wide thumbnails? mmm... i dunno... maybe...
	patch_t *patch;
	dboolean encore = cv_kartencore.value == 1 && levellistmode == LLM_CREATESERVER && cv_newgametype.value == GT_RACE;
	fixed_t thumbscale = M_GetMapThumbnail(lsmap->available ? lsmap->mapnum : NEXTMAP_INVALID, &patch);
	thumbscale = FixedMul(thumbscale/4, scale);

	V_DrawSciencePatch(x - patch->width*thumbscale/2 + (encore ? patch->width*thumbscale : 0),
	                   y - patch->height*thumbscale/2 - FixedMul(lsmapnameheight, scale/2),
	                   encore ? V_FLIP : 0, patch, thumbscale);
	//if (!lsmap->available)
		//M_DrawStaticBox(x, y, V_80TRANS, wide ? 282 : 80, 50);

	if (encore && highlight)
	{
		static angle_t rubyfloattime = 0;
		const fixed_t rubyheight = FSIN(rubyfloattime)*2;
		V_DrawSciencePatch(x, y - lsmapnameheight/2 - rubyheight, 0, W_CachePatchName("RUBYICON", PU_CACHE), FRACUNIT);
		rubyfloattime += FixedMul(ANGLE_MAX/NEWTICRATE, renderdeltatics);
	}

	x -= width/2;
	y += height/2 - FixedMul(lsmapnameheight, scale);
	V_SetClipRect(x, y, width, lsmapnameheight / (wide || highlight ? 1 : 2), 0);

	V_DrawFill(-9999, -9999, 22222, 22222, V_NOSCALESTART|lsmap->color);
	if (wide || highlight)
	{
		int32_t scrollamt = V_ThinStringWidth(lsmap->mapname, MENUCAPS|V_6WIDTHSPACE)*FRACUNIT - width;
		if (scrollamt > 0)
		{
			// scroll the map's name!
			int32_t t = ((levelselect.namescroll % (scrollamt/FRACUNIT + TICRATE*2)) - TICRATE)*FRACUNIT;
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
		int32_t scrollamt = V_SmallStringWidth(lsmap->mapname, MENUCAPS)*FRACUNIT - width;

		// screw adding more string drawing macros
		V__DrawOneScaleString(x - min(0, scrollamt/2), y, FRACUNIT/2, MENUCAPS, HU_FONT, lsmap->mapname);
	}

	V_ClearClipRect();
}

static void M_DrawLevelPlatterRow(uint8_t row, fixed_t y)
{
	const dboolean rowhighlight = (row == levelselect.row);
	const levelselectrow_t *lsrow = &levelselect.rows[row];

	if (lsrow->header[0])
	{
		M_DrawLevelPlatterHeader(y, lsrow->header, (rowhighlight || (row == levelselect.highlight)), false);
		y += lsheadingheight;
	}

	if (row == 0)
	{
		menupanelinst_t *options = M_GetPanel("OPTIONS");
		options->visible = true;
		options->y = y + 3*FRACUNIT - lsmapheight/2;
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
		V_DrawCenteredStringAtFixed(lsbasex + 2*FRACUNIT, y - 4*FRACUNIT, MENUCAPS|(rowhighlight ? V_YELLOWMAP : 0), CupName(cup, false));
	}

	for (uint8_t col = 0; col < lsrow->numcolumns; col++)
	{
		if (levellistmode == LLM_CUPSELECT)
			M_DrawLevelPlatterMap(&lsrow->maps[col], lsbasex + 70*FRACUNIT + col*lshseperationcup, y, false, false);
		else if (!rowhighlight || col != levelselect.column)
			M_DrawLevelPlatterMap(&lsrow->maps[col], lsbasex + col*lshseperation, y, false, lsrow->wide);
	}
}

void MD_DrawLevelPlatterMenu(menupanelinst_t *pi)
{
	(void)pi;

	uint8_t row = levelselect.row;
	int32_t y = lsbasey + levelselect.offsety - getheadingoffset(row);
	int32_t hiliy = INT32_MIN;
	int32_t minrows = (vid.scaledheight*FRACUNIT + lsbasevseperation)/lsbasevseperation;
	int32_t cursorx = levelselect.rows[row].wide ? 0 : levelselect.column * lshseperation;

	M_GetPanel("OPTIONS")->visible = false;

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
	while (y <= vid.scaledheight*FRACUNIT + lsbasevseperation/2)
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

		fixed_t fx = lsbasex + cursorx - lsmapwidth/2 + levelselect.offsetx;
		fixed_t fy = hiliy - (lsmapheight + (levellistmode == LLM_CUPSELECT ? 0 : lsmapnameheight))/2;
		fixed_t width = lsrow->wide ? 300*FRACUNIT : lsmapwidth;
		fixed_t height = lsmapheight;
		int32_t trans = 0;
		if (levelselect.selectdelay > 0 ? skullAnimCounter & 1 : skullAnimCounter < 4)
			trans = V_GetStringColormap(highlightflags)[0];

		V_DrawFixedFill(fx, fy,                               width, FRACUNIT, trans);
		V_DrawFixedFill(fx, fy + height - FRACUNIT,           width, FRACUNIT, trans);
		V_DrawFixedFill(fx, fy + FRACUNIT,                    FRACUNIT, height - 2*FRACUNIT, trans);
		V_DrawFixedFill(fx + width - FRACUNIT, fy + FRACUNIT, FRACUNIT, height - 2*FRACUNIT, trans);
	}

	// handle movement of cursor box
	fixed_t cursormovefrac = FixedDiv(2, 3);
	if (abs(levelselect.offsety) > FRACUNIT/4)
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
dboolean M_CanShowLevelInList(mapnum_t mapnum)
{
	return M_CanShowLevelOnPlatter(mapnum) && M_LevelAvailableOnPlatter(mapnum);
}

const char *M_ButtonName(uint8_t p, gamecontrols_e gc, dboolean forcekeyboard)
{
	if (forcekeyboard || (p == 0 && keyboardcontrolled && (messagebox.active || menustack[0])))
	{
		switch (gc)
		{
			case gc_accelerate:
				return "Enter";
			case gc_systemmenu:
			case gc_brake:
				return "ESC";
			case gc_fire:
				return "Space";
			case gc_drift:
				return "Backspace";
			case gc_pause:
				return "=";
			default:
				goto fallback;
		}
	}
	else
fallback:
	{
		int32_t keynum = G_KeynumFromGamecontrol(gc);

		return G_KeynumToString(keynum);
	}
}

// Can't use va twice in the same line.
char ConfirmString[25];
char DeclineString[25];

char *M_MessageConfirmText(dboolean gamequit, dboolean forcekeyboard)
{
	const char *item1 = M_ButtonName(0, gc_accelerate, forcekeyboard);

	memset(&ConfirmString, 0, sizeof(ConfirmString));

	if (gamequit)
		snprintf(ConfirmString, 25, "%s to quit", item1);
	else
		snprintf(ConfirmString, 25, "%s to confirm", item1);

	return ConfirmString;
}

char *M_MessageCancelText(dboolean gamequit, dboolean forcekeyboard)
{
	const char *item1 = M_ButtonName(0, gc_brake, forcekeyboard);

	memset(&DeclineString, 0, sizeof(DeclineString));

	if (gamequit)
		snprintf(DeclineString, 25, "%s to return", item1);
	else
		snprintf(DeclineString, 25, "%s to cancel", item1);

	return DeclineString;
}

void M_MessageboxClose(dboolean clearstack)
{
	messagebox.active = false;

	Z_Free(messagebox.headertext);
	Z_Free(messagebox.text);
	Z_Free(messagebox.confirmtext);
	Z_Free(messagebox.declinetext);

	memset(&messagebox, 0, sizeof(messagebox_t));

	if (messageboxqueuesize == 0)
	{
		return;
	}

	if (messageboxqueue[messageboxqueuesize-1].active || clearstack)
	{
		if (!clearstack)
			memcpy(&messagebox, &messageboxqueue[messageboxqueuesize-1], sizeof (messagebox_t));

		if (!clearstack && messageboxqueue[messageboxqueuesize-1].headertext && messageboxqueue[messageboxqueuesize-1].headertext[0] != 0)
			messagebox.headertext = Z_StrDup(messageboxqueue[messageboxqueuesize-1].headertext);
		else
			messagebox.headertext = NULL;

		if (!clearstack && messageboxqueue[messageboxqueuesize-1].text && messageboxqueue[messageboxqueuesize-1].text[0] != 0)
			messagebox.text = Z_StrDup(messageboxqueue[messageboxqueuesize-1].text);
		else
			messagebox.text = NULL;

		if (!clearstack && messageboxqueue[messageboxqueuesize-1].confirmtext && messageboxqueue[messageboxqueuesize-1].confirmtext[0] != 0)
			messagebox.confirmtext = Z_StrDup(messageboxqueue[messageboxqueuesize-1].confirmtext);
		else
			messagebox.confirmtext = NULL;

		if (!clearstack && messageboxqueue[messageboxqueuesize-1].declinetext && messageboxqueue[messageboxqueuesize-1].declinetext[0] != 0)
			messagebox.declinetext = Z_StrDup(messageboxqueue[messageboxqueuesize-1].declinetext);
		else
			messagebox.declinetext = NULL;

		if (clearstack)
		{
			int32_t i;

			for (i = 0; i < NUMMENULEVELS; i++)
			{
				Z_Free(messageboxqueue[i].headertext);
				Z_Free(messageboxqueue[i].text);
				Z_Free(messageboxqueue[i].confirmtext);
				Z_Free(messageboxqueue[i].declinetext);
			}
		}
		else
		{
			Z_Free(messageboxqueue[messageboxqueuesize-1].headertext);
			Z_Free(messageboxqueue[messageboxqueuesize-1].text);
			Z_Free(messageboxqueue[messageboxqueuesize-1].confirmtext);
			Z_Free(messageboxqueue[messageboxqueuesize-1].declinetext);
		}

		if (clearstack)
			memset(&messageboxqueue, 0, sizeof(messageboxqueue));
		else
			memset(&messageboxqueue[messageboxqueuesize-1], 0, sizeof(messagebox_t));

		if (clearstack)
			messageboxqueuesize = 0;
		else
			messageboxqueuesize--;
	}

}

// ==================================================
// MESSAGE BOX (aka: a hacked, cobbled together menu)
// ==================================================
void M_StartMessage2(const char *headerstr, const char *str, messagebox_callback callback,
	menumessagetype_t itemtype, const char *confirmstr, const char *declinestr)
{
	size_t max = 0, start = 0, i = 0, strlines = 0;

	static char *message = NULL;

	messagebox_t *messageboxset = messagebox.active ? &messageboxqueue[messageboxqueuesize] : &messagebox;

	if (messagebox.active)
		messageboxqueuesize++;

	if (messageboxqueuesize >= NUMMENULEVELS)
	{
		CONS_Alert(CONS_WARNING, "Too many messageboxes queued, dropping newest added.\n");
		messageboxqueuesize--;
		return;
	}

	if (str && str[0] != 0)
	{
		message = Z_StrDup(str);
		DEBFILE(message);
	}

	if (message)
	{
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
	}

	M_StartControlPanel(); // can't put menuactive to true

	if (headerstr && headerstr[0] != 0)
		messageboxset->headertext = Z_StrDup(headerstr);
	else
		messageboxset->headertext = NULL;

	if (message)
		messageboxset->text = Z_StrDup(message);
	else
		messageboxset->text = NULL;

	if (confirmstr && confirmstr[0] != 0)
		messageboxset->confirmtext = Z_StrDup(confirmstr);
	else
		messageboxset->confirmtext = NULL;

	if (declinestr && declinestr[0] != 0)
		messageboxset->declinetext = Z_StrDup(declinestr);
	else
		messageboxset->declinetext = NULL;

	messageboxset->messagetype = itemtype;

	messageboxset->callback.handler = NULL;
	messageboxset->callback.routine = NULL;

	if (itemtype == MM_EVENTHANDLER)
	{
		if (callback.handler)
		{
			messageboxset->callback.handler = callback.handler;
		}
	}
	else
	{
		if (callback.routine)
		{
			messageboxset->callback.routine = callback.routine;
		}
	}

	if (message)
	{
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
	}

	if (headerstr && headerstr[0] != 0)
		strlines++;

	if ((confirmstr && confirmstr[0] != 0) || (declinestr && declinestr[0] != 0))
		strlines++;

	messageboxset->x = (BASEVIDWIDTH  - 8*max-16)/2;
	messageboxset->y = (BASEVIDHEIGHT - (message ? M_StringHeight(message) : 0))/2;

	messageboxset->numlines = strlines;

	messageboxset->maxlength = max;

	messageboxset->active = true;

	Z_Free(message);
}

#define MAXMSGLINELEN 256

static void M_DrawMessageMenu(void)
{
	int32_t y = messagebox.y;
	size_t i, start = 0;
	char string[MAXMSGLINELEN];
	const char *msg = messagebox.text;
	int16_t numboxlines = messagebox.numlines;
	int32_t boxwidth = messagebox.maxlength*8+6;
	int32_t boxheight = messagebox.numlines*8+8;
	const int32_t thinflags = V_6WIDTHSPACE|V_ALLOWLOWERCASE;

	dboolean messagevalid = !!(messagebox.text);
	dboolean headervalid = !!(messagebox.headertext);
	dboolean confirmvalid = !!(messagebox.confirmtext);
	dboolean declinevalid = !!(messagebox.declinetext);

	if (confirmvalid || declinevalid)
		boxheight += 4;

	V_DrawFill(messagebox.x+5, (y+5) - 8, boxwidth, boxheight, 159);

	if (headervalid)
	{
		numboxlines--;
		V_DrawCenteredThinString(messagebox.x + boxwidth/2, y, thinflags, messagebox.headertext);
		y += 11;
	}

	if ((confirmvalid && !declinevalid) || (declinevalid && !confirmvalid))
	{
		const char *textstring = NULL;

		numboxlines--;

		int32_t textx = messagebox.x + boxwidth/2;
		int32_t texty = y+(numboxlines*8) + 2;

		if (confirmvalid && !declinevalid)
			textstring = messagebox.confirmtext;
		else
			textstring = messagebox.declinetext;

		V_DrawCenteredThinString(textx, texty, thinflags, textstring);
	}
	else if (confirmvalid && declinevalid)
	{
		numboxlines--;

		const int32_t spacing = 10;

		int32_t offset = spacing + V_ThinStringWidth(messagebox.confirmtext, thinflags) + V_ThinStringWidth(messagebox.declinetext, thinflags);
		offset = offset / 4;

		int32_t textx = (messagebox.x + boxwidth/2) - offset;
		int32_t text2x = (messagebox.x + boxwidth/2) + offset;
		int32_t texty = y+(numboxlines*8) + 2;

		V_DrawCenteredThinString(textx, texty, thinflags, messagebox.confirmtext);

		V_DrawCenteredThinString(text2x, texty, thinflags, messagebox.declinetext);
	}


	if (messagevalid)
	{
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

			V_DrawString((BASEVIDWIDTH - V_StringWidth(string, V_ALLOWLOWERCASE|V_6WIDTHSPACE))/2,y,V_ALLOWLOWERCASE|V_6WIDTHSPACE,string);
			y += 8; //LSBF_SHORT(hu_font[0]->height);
		}
	}
}

// =========
// IMAGEDEFS
// =========

// Draw an Image Def.  Aka, Help images.
// Defines what image is used in (menuitem_t)->text.
// You can even put multiple images in one menu!

void MP_TickImageDef(menupanelinst_t *pi)
{
	for (size_t i = 0; i < pi->numitems; i++)
	{
		if (i == pi->itemon)
			pi->items[i].status &= ~IT_HIDDEN;
		else
			pi->items[i].status |= IT_HIDDEN;
	}
}

void MD_DrawImageDef(menupanelinst_t *pi)
{
	if (!pi->items[pi->itemon].argument)
	{
		V_DrawString(2,BASEVIDHEIGHT-10, MENUCAPS|V_YELLOWMAP, va("%d", (pi->itemon<<1)-1)); // intentionally not highlightflags, unlike below
		V_DrawRightAlignedString(BASEVIDWIDTH-2,BASEVIDHEIGHT-10, MENUCAPS|V_YELLOWMAP, va("%d", pi->itemon<<1)); // ditto
	}
	else
	{
		int32_t x = BASEVIDWIDTH>>1, y = (BASEVIDHEIGHT>>1) - 4;
		x += (pi->itemon ? 1 : -1)*((BASEVIDWIDTH>>2) + 10);
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
int32_t MR_HandleImageDef(int32_t choice)
{
	(void)choice;

	menupanelinst_t *pi = &curpanels[panelOn];

	if (menucmd[0].dpad_lr > 0)
	{
		if (pi->itemon >= pi->numitems-1)
			return true;
		S_StartSound(NULL, sfx_menu1);
		M_RawSetItemOn(pi, pi->itemon + 1);
		M_SetMenuDelay(0);
	}
	else if (menucmd[0].dpad_lr < 0)
	{
		if (!pi->itemon)
			return true;
		S_StartSound(NULL, sfx_menu1);
		M_RawSetItemOn(pi, pi->itemon - 1);
		M_SetMenuDelay(0);
	}
	else
		return false;

	return true;
}

// ======================
// MISC MAIN MENU OPTIONS
// ======================

void MP_AddonsOptions(menupanelinst_t *pi)
{
	M_SetItemVisible(pi, "FOLDER", cv_addons_option.value == 3);
}

#define LOCATIONSTRING1 "Visit \x83SRB2.ORG/MODS\x80 to get & make addons!"
#define LOCATIONSTRING2 "Visit \x88SRB2.ORG/MODS\x80 to get & make addons!"

int32_t MR_Addons(int32_t choice)
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
		M_StartMessage2("Addons", va("No files/folders found.\n\n%s\n", (recommendedflags == V_SKYMAP ? LOCATIONSTRING2 : LOCATIONSTRING1)),MB_NONE,MM_NOTHING, "Press any key", NULL);
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
static void M_DrawTemperature(int32_t x, fixed_t t)
{
	int32_t y;

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
			uint8_t colours[NUMCOLOURS] = {135, 133, 92, 77, 114, 178, 161, 162};
			uint8_t c;
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
	uint32_t len;
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
		M_StartMessage2("Addons", va("\x82%s\x80\nThis folder no longer exists!\nAborting to main menu.\n", M_AddonsHeaderPath()),MB_NONE,MM_NOTHING, "Press any key", NULL)

#define CLEARNAME Z_Free(refreshdirname);\
					refreshdirname = NULL

static dboolean prevmajormods = false;

static void M_AddonsClearName(int32_t choice)
{
	(void)choice;
	if (!majormods || prevmajormods)
	{
		CLEARNAME;
	}
	M_MessageboxClose(false);
}

#define FILEADDSFX sfx_addfil
#define FILENOTADDSFX sfx_notadd
#define FILEERRORSFX sfx_adderr

// returns whether to do message draw
static dboolean M_AddonsRefresh(void)
{
	if (refreshdirmenu & REFRESHDIR_NORMAL)
	{
		if (preparefilemenu(true, false))
		{
			Reinitialize();
		}
		else
		{
			UNEXIST;
			if (refreshdirname)
			{
				CLEARNAME;
			}
			return true;
		}
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
				message = va("%c%s\x80\nMaximum number of addons reached.\nA file could not be loaded.\nIf you wish to play with this addon, restart the game to clear existing ones.", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname);
			else
				message = va("%c%s\x80\nA file was not loaded.\nCheck the console log for more information.", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname);
		}
		else if (refreshdirmenu & (REFRESHDIR_WARNING|REFRESHDIR_ERROR))
		{
			S_StartSound(NULL, FILEERRORSFX);
			message = va("%c%s\x80\nA file was loaded with %s.\nCheck the console log for more information.", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname, ((refreshdirmenu & REFRESHDIR_ERROR) ? "errors" : "warnings"));
		}
		else if (majormods && !prevmajormods)
		{
			S_StartSound(NULL, FILEADDSFX);
			message = va("%c%s\x80\nYou've loaded a gameplay-modifying addon.\n\nRecord Attack data will be saved to seperate save.\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), refreshdirname);
			prevmajormods = majormods;
		}

		if (message)
		{
			M_StartMessage2("Warning", message, MB_ROUTINE(M_AddonsClearName), MM_NOTHING, "Press any key", NULL);
			return true;
		}

		S_StartSound(NULL, FILEADDSFX);
		CLEARNAME;
	}

	return false;
}

void MP_TickAddonsURL(menupanelinst_t *pi)
{
	M_SetItemVisible(pi, "LOCATION1", !Playing());
	M_SetItemVisible(pi, "LOCATION2", !Playing());
	M_SetItemVisible(pi, "MIDGAME", Playing());
}

void MP_TickAddonsBackground(menupanelinst_t *pi)
{
	M_SetItemText(pi, "HEADERPATH", M_AddonsHeaderPath());
	M_SetItemDisabled(pi, "DISKICON", majormods);
	M_SetItemVisible(pi, "ADDONSLOADED", modifiedgame);

	if (cv_search_oldinput.value)
	{
		M_SetItemDisabled(pi, "SEARCHICON", menusearch.length == 0);
		M_SetItemDisabled(pi, "SEARCHTEXT", menusearch.length == 0);
		if (menusearch.length == 0)
		{
			M_SetItemText(pi, "SEARCHTEXT", "Type to search...");
		}
		else
		{
			M_SetItemText(pi, "SEARCHTEXT", menusearch.buffer);
		}
	}
	else
	{
		M_SetItemDisabled(pi, "SEARCHICON", *cv_dummyaddonsearch.string == 0);
		M_SetItemDisabled(pi, "SEARCHTEXT", *cv_dummyaddonsearch.string == 0);
		if (*cv_dummyaddonsearch.string == 0)
		{
			const char *button = M_ButtonName(0, gc_drift, false);
			M_SetItemText(pi, "SEARCHTEXT", va("Press %s to Search", button));
		}
		else
		{
			M_SetItemText(pi, "SEARCHTEXT", cv_dummyaddonsearch.string);
		}
	}
}

void MD_DrawAddons(menupanelinst_t *pi)
{
	(void)pi;

	int32_t y;

	// hack - need to refresh at end of frame to handle addfile...
	if (refreshdirmenu && M_AddonsRefresh())
	{
		return;
	}

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
}

static void M_AddonExec(int32_t ch)
{
	if (ch != MA_YES)
		return;

	S_StartSound(NULL, sfx_zoom);
	COM_BufAddText(va("exec \"%s%s\"", menupath, curpanels[panelOn].items[curpanels[panelOn].itemon].extra));
}

static const char *addonpatches[] = {
	[EXT_FOLDER] = "M_FFLDR",
	[EXT_UP] = "M_FBACK",
	[EXT_NORESULTS] = "M_FNOPE",
	[EXT_TXT] = "M_FTXT",
	[EXT_CFG] = "M_FCFG",
	[EXT_WAD] = "M_FWAD",
#ifdef USE_KART
	[EXT_KART] = "M_FKART",
#endif
	[EXT_PK3] = "M_FPK3",
	[EXT_SOC] = "M_FSOC",
	[EXT_LUA] = "M_FLUA",
	[NUM_EXT] = "M_FUNKN",
	[NUM_EXT+1] = "M_FSEL",
	[NUM_EXT+2] = "M_FLOAD",
	[NUM_EXT+3] = "M_FSRCH",
	[NUM_EXT+4] = "M_FSAVE",
};

void MP_PrepAddonList(menupanelinst_t *pi)
{
	size_t i;

	M_ClearPanelItems(pi);

	for (i = 0; i < sizedirmenu; i++)
	{
		menuitem_t *item = M_AppendMenuItem(pi, 0, 0, IT_INTERACT);
		uint8_t type = dirmenu[i][DIR_TYPE];
		item->style = DEH_FindMenuItemStyle(type & EXT_LOADED ? "ADDONLOADED" : "ADDON");
		M_SetItemPatchRaw(item, addonpatches[type & ~EXT_LOADED]);

#define charsonside 14
		if (dirmenu[i][DIR_LEN] > (charsonside*2 + 3))
			M_SetItemTextRaw(item, va("%." TOSTR2(charsonside) "s...%s", dirmenu[i]+DIR_STRING, dirmenu[i]+DIR_STRING+dirmenu[i][DIR_LEN]-(charsonside+1)));
#undef charsonside
		else
			M_SetItemTextRaw(item, dirmenu[i]+DIR_STRING);

		M_SetItemExtraRaw(item, dirmenu[i]+DIR_STRING);
	}

	M_RawSetItemOn(pi, dir_on[menudepthleft]);
}

static void AddonSearch(void)
{
	const char *filename = curpanels[panelOn].items[curpanels[panelOn].itemon].extra;
	char *tempname = Z_StrDup(filename);
#if 0 // much slower
	if (!preparefilemenu(true, false))
	{
		UNEXIST;
		return true;
	}
#else // streamlined
	searchfilemenu(tempname);
#endif
	Reinitialize();
}

// lolololol
static char addonssearchtext[] = "Search for files and folders";
static menuitem_t addonsearch = {.text = addonssearchtext, .cvar = &cv_dummyaddonsearch};

int32_t MR_HandleAddons(int32_t choice)
{
	if (menucmd[0].delay > 0)
		return false;

	const char *filename = curpanels[panelOn].items[curpanels[panelOn].itemon].extra;

	if (cv_search_oldinput.value && M_TextInputHandle(&menusearch, choice))
	{
		S_StartSound(NULL,sfx_menu1);

		char *tempname = Z_StrDup(filename);
#if 0 // much slower
		if (!preparefilemenu(true, false))
		{
			UNEXIST;
			return true;
		}
#else // streamlined
		searchfilemenu(tempname);
#endif
		Reinitialize();
		return true;
	}

	if (menucmd[0].dpad_ud > 0)
	{
		if (dir_on[menudepthleft] < sizedirmenu-1)
		{
			dir_on[menudepthleft]++;
			M_SetMenuDelay(0);
			return false;
		}
	}
	else if (menucmd[0].dpad_ud < 0)
	{
		if (dir_on[menudepthleft])
		{
			dir_on[menudepthleft]--;
			M_SetMenuDelay(0);
			return false;
		}
	}
	else if (M_MenuButtonPressed(0, MBT_CONFIRM))
	{
		M_SetMenuDelay(0);

		if (!dirmenu[dir_on[menudepthleft]])
		{
			refreshdirmenu |= REFRESHDIR_NORMAL;
			S_StartSound(NULL, FILENOTADDSFX);
			M_SetMenuDelay(0);
			return true;
		}

		switch (dirmenu[dir_on[menudepthleft]][DIR_TYPE])
		{
			case EXT_FOLDER:
				strcpy(&menupath[menupathindex[menudepthleft]],filename);
				if (menudepthleft)
				{
					menupathindex[--menudepthleft] = strlen(menupath);
					menupath[menupathindex[menudepthleft]] = 0;

					if (!preparefilemenu(false, false))
					{
						S_StartSound(NULL, FILEERRORSFX);
						M_StartMessage2("Addons", va("%c%s\x80\nThis folder is empty.\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), M_AddonsHeaderPath()),MB_NONE,MM_NOTHING, "Press any key", NULL);
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

					CV_StealthSet(&cv_dummyaddonsearch, "");
					Reinitialize();
				}
				else
				{
					S_StartSound(NULL, FILENOTADDSFX);
					M_StartMessage2("Addons", va("%c%s\x80\nThis folder is too deep to navigate to!\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), M_AddonsHeaderPath()),MB_NONE,MM_NOTHING, "Press any key", NULL);
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
				CV_StealthSet(&cv_dummyaddonsearch, "");
				Reinitialize();
				break;
			case EXT_TXT:
				M_StartMessage2("Addons", va("%c%s\x80\nThis file may not be a console script.\nAttempt to run anyways?\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), filename), MB_ROUTINE(M_AddonExec) ,MM_YESNO, M_MessageConfirmText(false, false), M_MessageCancelText(false, false));
				break;
			case EXT_CFG:
				M_AddonExec(MA_YES);
				break;
			case EXT_LUA:
			case EXT_SOC:
			case EXT_WAD:
#ifdef USE_KART
			case EXT_KART:
#endif
			case EXT_PK3:
				COM_BufAddText(va("addfile \"%s%s\"", menupath, filename));
				break;
			default:
				S_StartSound(NULL, FILENOTADDSFX);
		}
	}
	else if (M_MenuButtonPressed(0, MBT_INFO2))
	{
		M_OpenVirtualKeyboard(&menusearch, menusearchbuf, &addonsearch);
	}
	else
	{
		switch (choice)
		{
			default:
				return false;
		}
	}

	return true;
}

int32_t MR_QuitAddons(int32_t choice)
{
	(void)choice;
	CV_StealthSet(&cv_dummyaddonsearch, "");
	closefilemenu(true);
	return true;
}

// ---- REPLAY HUT -----
menudemo_t *demolist = NULL;

// Locked behind Lock_search_state
menudemo_t *demolist_all; // All replays
dboolean replaynamesloaded = false;

#ifdef HAVE_THREADS
I_mutex replayquerymutex;

// I_In_Exiting_Signal_Handler is an evil hack
// to avoid infinite SIGABRT recursion in the signal handler
// due to poisoned locks or mach-o kernel not supporting locks in signals
// or something like that. idk
#  define Lock_search_state()    if (!I_In_Exiting_Signal_Handler()) { I_lock_mutex(&replayquerymutex); }
#  define Unlock_search_state()  if (!I_In_Exiting_Signal_Handler()) { I_unlock_mutex(replayquerymutex); }
#else/*HAVE_THREADS*/
#  define Lock_search_state()
#  define Unlock_search_state()
#endif/*HAVE_THREADS*/

static char replayqueryinput_buffer[MAXREPLAYQUERY+1]; // The input typed
static textinput_t replayqueryinput;

static size_t replayqueryfound = 0; // Number of checked replay entries
static size_t replayquerycheck = 0; // Index of next replay entry to check in demolist_all
static dboolean replaylistgenerated = false; // if the list is ready to use?

#define DF_ENCORE       0x40

static void CreateReplayItems(menupanelinst_t *pi)
{
	if (replaylistgenerated)
		return;

	M_ClearPanelItems(pi);

	size_t i;
	for (i = 0; i < replayqueryfound; i++)
	{
		menuitem_t *item = M_AppendMenuItem(pi, 0, 0, IT_INTERACT);
		M_SetItemTextRaw(item, demolist[i].title);
		M_SetItemExtraRaw(item, dirmenu[i] + DIR_STRING);
		item->argument = dirmenu[i][DIR_TYPE];
		if (item->argument != EXT_FOLDER && item->argument != EXT_UP)
		{
			item->submenu = MN_MISC_REPLAYSTART;
		}

		if (demolist[i].type == MD_SUBDIR)
		{
			item->style = DEH_FindMenuItemStyle("REPLAYSUBDIR");
			M_SetItemPatchRaw(item, item->argument == EXT_UP ? "M_RBACK" : "M_RFLDR");
		}
	}

	replaylistgenerated = true;
	M_RawSetItemOn(pi, dir_on[menudepthleft]);
}

static void ReplayNamesLoadThread(void* userdata)
{
	Lock_search_state();

	size_t demolist_all_size = sizedirmenu;
	menudemo_t *demolist_all_local = (menudemo_t*)malloc(sizeof(menudemo_t)*sizedirmenu);
	memcpy(demolist_all_local, demolist_all, sizeof(menudemo_t)*sizedirmenu);
	char *replaydirpath = (char*)userdata;

	Unlock_search_state();

	for (size_t i = 0; i < demolist_all_size; ++i)
	{
		if (demolist_all_local[i].type != MD_SUBDIR)
		{
			G_LoadDemoInfo(&demolist_all_local[i], true);

			// this is fucking horrid, but i wanna be able to search for dates
			/*if (demolist_all_local[i].date[0] != '\0')
			{
				snprintf(demolist_all_local[i].title, sizeof(demolist_all_local[i].title), "%s %s", demolist_all_local[i].title, demolist_all_local[i].date);
			}*/
		}
	}

	Lock_search_state();

	if (strcmp(menupath, replaydirpath) == 0)
	{
		memcpy(demolist_all, demolist_all_local, sizeof(menudemo_t)*sizedirmenu);
		replaynamesloaded = true;
	}

	Unlock_search_state();

	// Was allocated before thread start, we need to free it
	free(replaydirpath);
	free(demolist_all_local);
}

static void LoadReplayNames(void)
{
	char *replaydirpath = strdup(menupath);

	Lock_search_state();
	replaynamesloaded = false;
	Unlock_search_state();

#ifdef HAVE_THREADS
	I_spawn_thread("replay-names-load", ReplayNamesLoadThread, replaydirpath);
#else
	ReplayNamesLoadThread(replaydirpath);
#endif
}

static void ResetReplayQuery(void)
{
	memset(replayqueryinput_buffer, 0, MAXREPLAYQUERY);
	CV_StealthSet(&cv_dummyreplaysearch, "");
	M_TextInputInit(&replayqueryinput, replayqueryinput_buffer, MAXREPLAYQUERY);

	replayqueryfound = 0;
	replayquerycheck = 0;
}

static void AddCheckedReplay(void)
{
	memcpy(&demolist[replayqueryfound], &demolist_all[replayquerycheck], sizeof(menudemo_t));
	++replayqueryfound;
	++replayquerycheck;
}

// Check up to maxnum replays if they match with query
static void M_HutCheckReplays(size_t maxnum)
{
	if (!replaynamesloaded)
		return;

	// Already checked everything
	if (replayquerycheck == sizedirmenu)
		return;

	replaylistgenerated = false;

	// If we don't have any query, just copy all demos and consider them checked
	if (replayqueryinput.length == 0)
	{
		// Mark as done
		replayqueryfound = replayquerycheck = sizedirmenu;

		memcpy(demolist, demolist_all, sizeof(menudemo_t) * sizedirmenu);

		return;
	}

	while (maxnum-- && replayquerycheck < sizedirmenu)
	{
		switch (demolist_all[replayquerycheck].type)
		{
			case MD_SUBDIR:
				AddCheckedReplay(); // Just add subdirs
				// Will also be decremented on next iteration. Basically, we just don't want to
				// spend our number of tries on subdirs, since we don't check anything for them
				++maxnum;
				break;

			case MD_NOTLOADED:
			case MD_OUTDATED:
			case MD_LOADED:
				if (demolist_all[replayquerycheck].title[0] && strcasestr(demolist_all[replayquerycheck].title, replayqueryinput.buffer) != NULL)
					AddCheckedReplay(); // It matches, add it!
				else
					replayquerycheck++; // Doesn't match, moving on...

				break;

			// Don't show invalid replays
			case MD_INVALID:
				replayquerycheck++;
		}
	}
}

static int ReplayListSortComparator(const void *entry1, const void *entry2)
{
	const menudemo_t *demo1 = (const menudemo_t*)entry1;
	const menudemo_t *demo2 = (const menudemo_t*)entry2;

	char filepath1[sizeof(demo1->filepath)];
	char filepath2[sizeof(demo2->filepath)];

	// First check for directories, they always should be at the top
	if (demo1->type == MD_SUBDIR && demo2->type != MD_SUBDIR)
		return -1;
	else if (demo2->type == MD_SUBDIR && demo1->type != MD_SUBDIR)
		return 1;
	else if (demo1->type == MD_SUBDIR && demo2->type == MD_SUBDIR)
		return 0;

	memcpy(filepath1, demo1->filepath, sizeof(filepath1));
	memcpy(filepath2, demo2->filepath, sizeof(filepath2));

	nameonly(filepath1);
	nameonly(filepath2);

	// Comparing in opposite order to move new replays to the top
	return strncmp(filepath2, filepath1, sizeof(filepath1));
}

static void PrepReplayList(dboolean reset)
{
	size_t i;

	replayquerycheck = replayqueryfound = 0;

	Z_Free(demolist);
	demolist = Z_Calloc(sizeof(menudemo_t) * sizedirmenu, PU_STATIC, NULL);

	// If directory didn't change, keep demolist_all
	if (!reset)
		return;

	Lock_search_state();

	if (demolist_all)
		Z_Free(demolist_all);

	demolist_all = Z_Calloc(sizeof(menudemo_t) * sizedirmenu, PU_STATIC, NULL);

	for (i = 0; i < sizedirmenu; i++)
	{
		if (dirmenu[i][DIR_TYPE] == EXT_UP)
		{
			demolist_all[i].type = MD_SUBDIR;
			snprintf(demolist_all[i].title, sizeof(demolist_all[i].title), "UP");
		}
		else if (dirmenu[i][DIR_TYPE] == EXT_FOLDER)
		{
			demolist_all[i].type = MD_SUBDIR;
			strncpy(demolist_all[i].title, dirmenu[i] + DIR_STRING, 64);
		}
		else
		{
			demolist_all[i].type = MD_NOTLOADED;
			snprintf(demolist_all[i].filepath, MAXFILEPATH, "%s%s", menupath, dirmenu[i] + DIR_STRING);
			snprintf(demolist_all[i].title, sizeof(demolist_all[i].title), ".....");
		}
	}

	qs22j(demolist_all, sizedirmenu, sizeof(menudemo_t), ReplayListSortComparator);

	Unlock_search_state();

	LoadReplayNames();
}

void MP_PrepReplayList(menupanelinst_t *pi)
{
	(void)pi;
	PrepReplayList(true);
}

int32_t MR_ReplayHut(int32_t choice)
{
	(void)choice;

	if (!demo.inreplayhut)
	{
		snprintf(menupath, 1024, "%s"PATHSEP"media"PATHSEP"replay"PATHSEP"online"PATHSEP, srb2home);
		menupathindex[(menudepthleft = menudepth-1)] = strlen(menupath);
		ResetReplayQuery();
	}
	if (!preparefilemenu(false, true))
	{
		M_StartMessage2("Replay Hut", "No replays found.\n", MB_NONE, MM_NOTHING, "Press any key", NULL);
		return false;
	}
	else if (!demo.inreplayhut)
		dir_on[menudepthleft] = 0;
	demo.inreplayhut = true;

	//M_ClearMenus(true);
	modeattacking = ATTACKING_MENU;
	titlemapinaction = TITLEMAP_OFF; // Nope don't give us HOMs please

	demo.rewinding = false;
	CL_ClearRewinds();

	return true;
}

void MP_ReplayHutStart(menupanelinst_t *pi)
{
	switch (demolist[dir_on[menudepthleft]].addonstatus)
	{
	case DFILE_ERROR_CANNOTLOAD:
		// Only show "Watch Replay Without Addons"
		M_SetItemVisible(pi, "LOADWATCH", false);
		M_SetItemVisible(pi, "NOLOADWATCH", true);
		M_SetItemVisible(pi, "WATCH", false);
		M_SetItemOn(pi, "NOLOADWATCH");
		break;

	case DFILE_ERROR_NOTLOADED:
	case DFILE_ERROR_INCOMPLETEOUTOFORDER:
		// Show "Load Addons and Watch Replay" and "Watch Replay Without Addons"
		M_SetItemVisible(pi, "LOADWATCH", true);
		M_SetItemVisible(pi, "NOLOADWATCH", true);
		M_SetItemVisible(pi, "WATCH", false);
		M_SetItemOn(pi, "LOADWATCH");
		break;

	case DFILE_ERROR_EXTRAFILES:
	case DFILE_ERROR_OUTOFORDER:
	default:
		// Show "Watch Replay"
		M_SetItemVisible(pi, "LOADWATCH", false);
		M_SetItemVisible(pi, "NOLOADWATCH", false);
		M_SetItemVisible(pi, "WATCH", true);
		M_SetItemOn(pi, "WATCH");
		break;
	}
}

static dboolean M_HandleReplayHutQuery(int32_t choice)
{
	// Yea gonna copy buffer and check if it changed, thats better than checking for specific keys i think
	char tmp[MAXREPLAYQUERY+1];
	memcpy(tmp, replayqueryinput_buffer, MAXREPLAYQUERY+1);

	if (M_TextInputHandle(&replayqueryinput, choice))
	{
		S_StartSound(NULL,sfx_menu1);

		// Restart search only if we actually modified input and not just moved in it
		if (memcmp(tmp, replayqueryinput_buffer, MAXREPLAYQUERY+1))
		{
			preparefilemenu(false, true);
			dir_on[menudepthleft] = 0;

			menupanelinst_t *pi = &curpanels[panelOn];
			MP_PrepReplayList(pi);
		}

		return true;
	}

	return false;
}

static void ReplaySearch(void)
{
	preparefilemenu(false, true);
	dir_on[menudepthleft] = 0;

	menupanelinst_t *pi = &curpanels[panelOn];
	MP_PrepReplayList(pi);
}

// lolololol 2
static char replayssearchtext[] = "Search for replays";
static menuitem_t replaysearch = {.text = replayssearchtext, .cvar = &cv_dummyreplaysearch};

int32_t MR_HandleReplayHutList(int32_t choice)
{
	if (cv_search_oldinput.value && M_HandleReplayHutQuery(choice))
		return false;
	else if (M_MenuButtonPressed(0, MBT_INFO2))
	{
		M_OpenVirtualKeyboard(&replayqueryinput, replayqueryinput_buffer, &replaysearch);
	}

	if (replayquerycheck != sizedirmenu)
		return true;

	if (!replaylistgenerated)
		return true;

	if (menucmd[0].delay > 0)
		return false;

	menupanelinst_t *pi = &curpanels[panelOn];
	menuitem_t *item = &pi->items[pi->itemon];
	panelfunc_f *initializer = M_GetDefFromInst(pi)->initializer;

	if (menucmd[0].dpad_ud > 0)
	{
		if (dir_on[menudepthleft] < replayqueryfound-1)
		{
			dir_on[menudepthleft]++;
			M_SetMenuDelay(0);
			return false;
		}
	}
	else if (menucmd[0].dpad_ud < 0)
	{
		if (dir_on[menudepthleft])
		{
			dir_on[menudepthleft]--;
			M_SetMenuDelay(0);
			return false;
		}
	}
	else if (M_MenuButtonPressed(0, MBT_CONFIRM))
	{
		M_SetMenuDelay(0);

		switch (item->argument)
		{
			case EXT_FOLDER:
				strcpy(&menupath[menupathindex[menudepthleft]], item->extra);
				if (menudepthleft)
				{
					menupathindex[--menudepthleft] = strlen(menupath);
					menupath[menupathindex[menudepthleft]] = 0;
					ResetReplayQuery();

					if (!preparefilemenu(false, true))
					{
						S_StartSound(NULL, sfx_s224);
						M_StartMessage2("Replay Hut", va("%c%s\x80\nThis folder is empty.\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), M_AddonsHeaderPath()),MB_NONE,MM_NOTHING, "Press any key", NULL);
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
						initializer(pi);
					}
				}
				else
				{
					S_StartSound(NULL, sfx_s26d);
					M_StartMessage2("Replay Hut", va("%c%s\x80\nThis folder is too deep to navigate to!\n", ('\x80' + (highlightflags>>V_CHARCOLORSHIFT)), M_AddonsHeaderPath()),MB_NONE,MM_NOTHING, "Press any key", NULL);
					menupath[menupathindex[menudepthleft]] = 0;
					ResetReplayQuery();
				}
				break;
			case EXT_UP:
				S_StartSound(NULL, sfx_menu1);
				menupath[menupathindex[++menudepthleft]] = 0;
				ResetReplayQuery();
				if (!preparefilemenu(false, true))
				{
					M_ExitMenu();
					return true;
				}
				initializer(pi);
				break;
			default:
				return false;
		}
	}
	else
	{
		switch (choice)
		{
			default:
				return false;
		}
	}

	return true;
}

void MP_TickReplayHutInfoForeground(menupanelinst_t *pi)
{
	pi->visible = false;
	M_SetItemVisible(pi, "OUTDATED", false);

	menudemo_t *mdemo = &demolist[dir_on[menudepthleft]];

	switch (mdemo->type)
	{
		case MD_NOTLOADED:
			break;

		case MD_INVALID:
			break;

		case MD_SUBDIR:
			break; // Can't think of anything to draw here right now

		case MD_OUTDATED:
			M_SetItemVisible(pi, "OUTDATED", true);
			/* FALLTHRU */
			default:
				pi->visible = true;

				int32_t mapnum = mdemo->map + 1;
				char *mapname;
				if (mapnum <= nummapheaders)
				{
					M_SetItemPatch(pi, "THUMBNAIL", G_BuildMapName(mapnum));
					mapname = G_BuildMapTitle(mapnum);
					M_SetItemDisabled(pi, "MAPNAME", false);
				}
				else
				{
					M_SetItemPatch(pi, "THUMBNAIL", "?");
					mapname = Z_StrDup("Level is not loaded.");
					M_SetItemDisabled(pi, "MAPNAME", true);
				}
				M_SetItemText(pi, "MAPNAME", mapname);
				Z_Free(mapname);

				int32_t laps = mdemo->numlaps;
				M_SetItemText(pi, "NUMLAPS", laps ? va("(%d laps)", laps) : "");

				M_SetItemText(pi, "GAMETYPE", mdemo->gametype == GT_RACE
				? va("Race (%s speed)", kartspeed_cons_t[(mdemo->kartspeed & ~DF_ENCORE) + 1].strvalue)
				: "Battle Mode");

				dboolean hidescore = !mdemo->standings[0].ranking;
				M_SetItemVisible(pi, "NOSTANDINGS", hidescore);
				M_SetItemVisible(pi, "WINNER", !hidescore);
				M_SetItemVisible(pi, "WINNERNAME", !hidescore);
				M_SetItemVisible(pi, "SCORE", !hidescore);
				M_SetItemVisible(pi, "SCORENAME", !hidescore);
				M_SetItemVisible(pi, "FACE", !hidescore);

				// No standings were loaded!
				if (hidescore)
					break;

		M_SetItemText(pi, "WINNERNAME", mdemo->standings[0].name);

		uint32_t score = mdemo->standings[0].timeorscore;
		M_SetItemText(pi, "SCORENAME", mdemo->gametype == GT_RACE ? "Time" : "Score");

		M_GetMenuItem(pi, "SCORE")->style = NULL;
		if (score == UINT32_MAX-1)
		{
			M_SetItemText(pi, "SCORE", "No Contest");
			M_GetMenuItem(pi, "SCORE")->style = DEH_FindMenuItemStyle("THIN");
		}
		else if (mdemo->gametype == GT_RACE)
		{
			M_SetItemText(pi, "SCORE", va("%d'%02d\"%02d",
										  G_TicsToMinutes(score, true),
										  G_TicsToSeconds(score),
										  G_TicsToCentiseconds(score)));
		}
		else
		{
			M_SetItemText(pi, "SCORE", va("%d", score));
		}

		// Character face!
		int32_t skin = mdemo->standings[0].skin;
		skincolornum_t color = mdemo->standings[0].color;
		M_SetItemPatch(pi, "FACE", skin >= 0 && skin < numskins ? skins[skin]->name : "");
		M_SetItemExtra(pi, "FACE", color >= 0 && color < numskincolors ? DEH_SkincolorName(color) : "");
		break;
	}
}

void MP_TickReplayHutInfoBackground(menupanelinst_t *pi)
{
	M_SetItemVisible(pi, "CENTERTEXT", false);

	menudemo_t *mdemo = &demolist[dir_on[menudepthleft]];

	if (menustack[0] == MN_MISC_REPLAYSTART)
	{
		M_SetItemVisible(pi, "SEARCHICON", false);
		M_SetItemVisible(pi, "SEARCHTEXT", false);
		M_SetItemVisible(pi, "SEARCHBOX", false);
	}
	else
	{

		M_SetItemVisible(pi, "SEARCHICON", true);
		M_SetItemVisible(pi, "SEARCHTEXT", true);
		M_SetItemVisible(pi, "SEARCHBOX", true);
		if (cv_search_oldinput.value)
		{
			M_SetItemDisabled(pi, "SEARCHICON", replayqueryinput.length == 0);
			M_SetItemDisabled(pi, "SEARCHTEXT", replayqueryinput.length == 0);
			if (replayqueryinput.length == 0)
			{
				M_SetItemText(pi, "SEARCHTEXT", "Type to search...");
			}
			else
			{
				M_SetItemText(pi, "SEARCHTEXT", replayqueryinput.buffer);
			}
		}
		else
		{
			M_SetItemDisabled(pi, "SEARCHICON", *cv_dummyreplaysearch.string == 0);
			M_SetItemDisabled(pi, "SEARCHTEXT", *cv_dummyreplaysearch.string == 0);
			if (*cv_dummyreplaysearch.string == 0)
			{
				const char *button = M_ButtonName(0, gc_drift, false);
				M_SetItemText(pi, "SEARCHTEXT", va("Press %s to Search", button));
			}
			else
			{
				M_SetItemText(pi, "SEARCHTEXT", cv_dummyreplaysearch.string);
			}
		}
	}

	if (replayquerycheck != sizedirmenu || !replaylistgenerated)
	{
		M_SetItemVisible(pi, "CENTERTEXT", true);
		M_SetItemText(pi, "CENTERTEXT", "Loading replays...");
		M_GetMenuItem(pi, "CENTERTEXT")->status &= ~ITH_WARNING;
		return;
	}

	switch (mdemo->type)
	{
		case MD_NOTLOADED:
			M_SetItemVisible(pi, "CENTERTEXT", true);
			M_SetItemText(pi, "CENTERTEXT", "Loading replay information...");
			M_GetMenuItem(pi, "CENTERTEXT")->status &= ~ITH_WARNING;
			break;

		case MD_INVALID:
			M_SetItemVisible(pi, "CENTERTEXT", true);
			M_SetItemText(pi, "CENTERTEXT", "This replay cannot be played.");
			M_GetMenuItem(pi, "CENTERTEXT")->status |= ITH_WARNING;
			break;

		case MD_SUBDIR:
			break; // Can't think of anything to draw here right now

		case MD_OUTDATED:
			break;

		default:
			break;
	}
}

// TODO: generalize, i can't do math ugh
static fixed_t M_ReplayHutScroll(fixed_t scrollamt, tic_t timer)
{
	fixed_t scrollx = 0;
	scrollamt *= 2;
	if (scrollamt > 0)
	{
		// scroll the map's name! (hmmm where was this copy pasted from, i wonder)
		int32_t scrollmax = (scrollamt/FRACUNIT + TICRATE)*2;
		fixed_t t = ((timer % scrollmax) - TICRATE)*FRACUNIT;
		if (t < scrollmax*FRACUNIT/2)
		{
			scrollx = max(0, min(t, scrollamt));
			if (t >= 0 && t < scrollamt)
				scrollx += R_GetTimeFrac(RTF_MENU);
		}
		else
		{
			t -= scrollmax*FRACUNIT/2;
			scrollx = scrollamt - max(0, min(t, scrollamt));
			if (t > 0 && t <= scrollamt)
				scrollx += FRACUNIT - R_GetTimeFrac(RTF_MENU); // FIXME: hiccup on last tic (1.5 -> 0)
		}
	}
	return scrollx / 2;
}

static dboolean M_CurrentReplayRankingCount(void)
{
	menudemo_t *mdemo = &demolist[dir_on[menudepthleft]];
	uint8_t i;
	uint8_t rankingcount = 0;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!mdemo->standings[i].ranking)
			continue;

		rankingcount++;
	}

	return rankingcount;
}

void MP_TickReplayHutMain(menupanelinst_t *pi)
{
	M_SetItemVisible(pi, "RANKINGBG", (M_CurrentReplayRankingCount() > 1));
}

void MP_TickReplayHutDemoList(menupanelinst_t *pi)
{
	size_t i;

	M_HutCheckReplays(cv_replaysearchrate.value);

	pi->visible = false;

	if (replayquerycheck != sizedirmenu)
		return;

	pi->visible = true;

	CreateReplayItems(pi);

	for (i = 0; i < 50; i++)
	{
		size_t demoi = pi->itemon + (i/2 * (i & 1 ? -1 : 1));
		if (demoi < replayqueryfound && demolist[demoi].type == MD_NOTLOADED)
		{
			G_LoadDemoInfo(&demolist[demoi], false);
			break;
		}
	}

	menuitemstyle_t *style = DEH_FindMenuItemStyle("REPLAYSELECT");
	if (style == NULL)
		return;

	style->parts[0].xofs = 0;
	for (i = 0; i < pi->numitems; i++)
	{
		if (pi->items[i].style == style)
			pi->items[i].style = NULL;

		if (i != pi->itemon || demolist[i].type == MD_SUBDIR)
			continue;

		pi->items[i].style = style;
		style->parts[0].xofs = -M_ReplayHutScroll(M_ItemWidth(pi, i) - vid.scaledwidth*FRACUNIT + pi->x*2, cursortimer);
	}
}

void MP_TickReplayHutRankings(menupanelinst_t *pi)
{
	size_t i;
	menudemo_t *mdemo = &demolist[dir_on[menudepthleft]];

	// Draw rankings beyond first
	M_ClearPanelItems(pi);

	for (i = 1; i < MAXPLAYERS && mdemo->standings[i].ranking; i++)
	{
		menuitem_t *item;
		int32_t y = i*20*FRACUNIT;
		uint32_t score = mdemo->standings[i].timeorscore;

		item = M_AppendMenuItem(pi, 1, y, 0);
		item->style = DEH_FindMenuItemStyle(score == UINT32_MAX-1 ? "REPLAYRANKINGTHIN" : "REPLAYRANKING");
		M_SetItemTextRaw(item, va("%2d", mdemo->standings[i].ranking));
		M_SetItemPatchRaw(item, mdemo->standings[i].name);

		if (score == UINT32_MAX-1)
			M_SetItemExtraRaw(item, "No Contest");
		else if (mdemo->gametype == GT_RACE)
			M_SetItemExtraRaw(item, va("%d'%02d\"%02d",
				G_TicsToMinutes(score, true),
				G_TicsToSeconds(score),
				G_TicsToCentiseconds(score)));
		else
			M_SetItemExtraRaw(item, va("%d", score));

		// Character face!
		item = M_AppendMenuItem(pi, 95*FRACUNIT, y, 0);
		item->style = DEH_FindMenuItemStyle("REPLAYRANKINGICON");
		int32_t skin = mdemo->standings[i].skin;
		skincolornum_t color = mdemo->standings[i].color;
		M_SetItemPatchRaw(item, skin >= 0 && skin < numskins ? skins[skin]->name : "");
		M_SetItemExtraRaw(item, color >= 0 && color < numskincolors ? DEH_SkincolorName(color) : "");
	}

	pi->y = 52*FRACUNIT - M_ReplayHutScroll((100 + i*20 - vid.scaledheight)*FRACUNIT, menutimer);
}

void MP_TickReplayHutDemoTitle(menupanelinst_t *pi)
{
	menudemo_t *mdemo = &demolist[dir_on[menudepthleft]];

	M_SetItemText(pi, "DEMOTITLE", mdemo->title);

	menuitem_t *item = M_GetMenuItem(pi, "DEMOTITLE");

	if (M_CurrentReplayRankingCount() > 1)
	{
		pi->cliprect = true;
	}
	else
	{
		pi->cliprect = false;
	}

	pi->x = 10*FRACUNIT - M_ReplayHutScroll((V_StringWidth(item->text, 0) - vid.scaledheight)*FRACUNIT, menutimer);
}

int32_t MR_QuitReplayHut(int32_t choice)
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

	S_StopMusicCredit();
	return true;
}

static void M_LoadAddonsResponse(int32_t ch)
{
	if (ch != MA_YES)
		return;

	M_RestoreMenuStack(true);

	COM_BufAddText(va("playdemo \"%s\" %s",
	demolist[dir_on[menudepthleft]].filepath + strlen(srb2home) + 1, // dumb hack
	M_IsItemOn(&curpanels[panelOn], "LOADWATCH") ? "-addfiles" : "-force"));
	M_ClearMenusEx(true, true);
}

int32_t MR_HutStartReplay(int32_t choice)
{
	(void)choice;

	const char *loadheader = M_IsItemOn(&curpanels[panelOn], "LOADWATCH" )  ? "Load Addons and Watch" : "Watch Without Addons";

	menudemo_t *mdemo = &demolist[dir_on[menudepthleft]];

	// Draw a warning prompt if needed
	const char *warning = "";
	dboolean warningpopup = true;
	switch (mdemo->addonstatus)
	{
		case DFILE_ERROR_CANNOTLOAD:
			if (M_IsItemOn(&curpanels[panelOn], "LOADWATCH" ))
			{
				warning = "Some addons in this replay cannot be loaded.\nYou can watch anyway, but desyncs may occur.";
			}
			else if (M_IsItemOn(&curpanels[panelOn], "NOLOADWATCH" ))
			{
				warning = "You can watch without loading addons, but desyncs may occur.";
			}
			else
			{
				warningpopup = false;
			}
			break;

		case DFILE_ERROR_NOTLOADED:
		case DFILE_ERROR_INCOMPLETEOUTOFORDER:
			if (M_IsItemOn(&curpanels[panelOn], "LOADWATCH" ))
			{
				warning = "Loading addons will mark your game as modified, and Record Attack may be unavailable.";
			}
			else if (M_IsItemOn(&curpanels[panelOn], "NOLOADWATCH" ))
			{
				warning = "You can watch without loading addons, but desyncs may occur.";
			}
			break;

		case DFILE_ERROR_EXTRAFILES:
			if (M_IsItemOn(&curpanels[panelOn], "LOADWATCH" ))
			{
				warning = "You have addons loaded that were not present in this replay.\nYou can watch anyway, but desyncs may occur.";
			}
			else if (M_IsItemOn(&curpanels[panelOn], "NOLOADWATCH" ))
			{
				warning = "You can watch without loading addons, but desyncs may occur.";
			}
			else
			{
				warningpopup = false;
			}
			break;

		case DFILE_ERROR_OUTOFORDER:
			if (M_IsItemOn(&curpanels[panelOn], "LOADWATCH" ))
			{
				warning = "You have this replay's addons loaded, but they are out of order.\nYou can watch anyway, but desyncs may occur.";
			}
			else if (M_IsItemOn(&curpanels[panelOn], "NOLOADWATCH" ))
			{
				warning = "You can watch without loading addons, but desyncs may occur.";
			}
			else
			{
				warningpopup = false;
			}
			break;

		default:
			warningpopup = false;
			break;
	}

	if (warningpopup)
		M_StartMessage2(loadheader, warning, MB_ROUTINE(M_LoadAddonsResponse), MM_YESNO, M_MessageConfirmText(false, false), M_MessageCancelText(false, false));
	else
		M_LoadAddonsResponse(MA_YES);
	return true;
}

void M_SetPlaybackMenuPointer(void)
{
	// FIXME
	//M_SetItemOn(&curpanels[panelOn], "PAUSE");
}

void MP_TickPlayBackMenu(menupanelinst_t *pi)
{
	pi->visible = false;

	if (leveltime - playback_last_menu_interaction_leveltime >= 6*TICRATE)
		playback_last_menu_interaction_leveltime = leveltime - 6*TICRATE;

	// Toggle items
	dboolean showpause = paused && !demo.rewinding;

	M_SetItemVisible(pi, "PAUSE", !showpause);
	M_SetItemVisible(pi, "FASTFORWARD", !showpause);
	M_SetItemVisible(pi, "REWIND", !showpause);
	M_SetItemVisible(pi, "RESUME", showpause);
	M_SetItemVisible(pi, "ADVANCEFRAME", showpause);
	M_SetItemVisible(pi, "REWINDFRAME", showpause);

	if (showpause)
	{
		if (M_IsItemOn(pi, "REWIND"))
			M_SetItemOn(pi, "REWINDFRAME");
		else if (M_IsItemOn(pi, "PAUSE"))
			M_SetItemOn(pi, "RESUME");
		else if (M_IsItemOn(pi, "FASTFORWARD"))
			M_SetItemOn(pi, "ADVANCEFRAME");
	}
	else
	{
		if (M_IsItemOn(pi, "REWINDFRAME"))
			M_SetItemOn(pi, "REWIND");
		else if (M_IsItemOn(pi, "RESUME"))
			M_SetItemOn(pi, "PAUSE");
		else if (M_IsItemOn(pi, "ADVANCEFRAME"))
			M_SetItemOn(pi, "FASTFORWARD");
	}

	M_SetItemVisible(pi, "NUMVIEWS", !modeattacking);
	M_SetItemVisible(pi, "VIEW1", !modeattacking && r_splitscreen >= 0);
	M_SetItemVisible(pi, "VIEW2", !modeattacking && r_splitscreen >= 1);
	M_SetItemVisible(pi, "VIEW3", !modeattacking && r_splitscreen >= 2);
	M_SetItemVisible(pi, "VIEW4", !modeattacking && r_splitscreen >= 3);

	M_SetItemVisible(pi, "DIRECTOR", !modeattacking);

	if (modeattacking)
	{
		M_SetItemX(pi, "FREECAM", 72*FRACUNIT);
		M_SetItemX(pi, "QUIT", 88*FRACUNIT);
		pi->x = (BASEVIDWIDTH/2 - 52)*FRACUNIT;
	}
	else
	{
		M_SetItemX(pi, "DIRECTOR", 156*FRACUNIT);
		M_SetItemX(pi, "FREECAM", 172*FRACUNIT);
		M_SetItemX(pi, "QUIT", 188*FRACUNIT);
		pi->x = (BASEVIDWIDTH/2 - 98)*FRACUNIT;
	}
}

void MD_DrawPlaybackMenu(menupanelinst_t *pi)
{
	int16_t i;
	patch_t *icon;
	uint8_t *activemap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_GOLD, GTC_MENUCACHE);
	uint32_t transmap = max(0, (int32_t)(leveltime - playback_last_menu_interaction_leveltime - 4*TICRATE)) / 5;
	transmap = min(8, transmap) << V_ALPHASHIFT;

	// wip
	//M_DrawTextBox(currentMenu->x-68, currentMenu->y-7, 15, 15, 0);
	//M_DrawCenteredMenu();

	for (i = 0; i < pi->numitems; i++)
	{
		fixed_t xoffs = 0, yoffs = 0;
		uint8_t *inactivemap = NULL;

		int16_t splitnum = i == M_GetMenuIndex(pi, "VIEW1") ? 0 :
		                 i == M_GetMenuIndex(pi, "VIEW2") ? 1 :
		                 i == M_GetMenuIndex(pi, "VIEW3") ? 2 :
		                 i == M_GetMenuIndex(pi, "VIEW4") ? 3 : -1;

		if (splitnum >= 0 && splitnum < 4)
		{
			if (modeattacking) continue;

			if (r_splitscreen >= splitnum)
			{
				int32_t ply = displayplayers[splitnum];

				icon = faceprefix[players[ply].skin][FACE_RANK];

				xoffs = icon->leftoffset*FRACUNIT;
				yoffs = icon->topoffset*FRACUNIT;

				if (i != pi->itemon)
					inactivemap = R_GetTranslationColormap(players[ply].skin, players[ply].skincolor, GTC_MENUCACHE);
			}
			else if (pi->items[i].patch && W_CheckNumForName(pi->items[i].patch) != LUMPERROR)
				icon = W_CachePatchName(pi->items[i].patch, PU_CACHE);
			else
				icon = W_CachePatchName("PLAYRANK", PU_CACHE); // temp
		}
		else if (pi->items[i].status & IT_HIDDEN)
			continue;
		else if (pi->items[i].patch && W_CheckNumForName(pi->items[i].patch) != LUMPERROR)
			icon = W_CachePatchName(pi->items[i].patch, PU_CACHE);
		else
			icon = W_CachePatchName("PLAYRANK", PU_CACHE); // temp

		if ((i == M_GetMenuIndex(pi, "FASTFORWARD") && cv_playbackspeed.value > 1) || (i == M_GetMenuIndex(pi, "REWIND") && demo.rewinding))
			V_DrawFixedPatch(pi->x + pi->items[i].x + xoffs, pi->y + yoffs, FRACUNIT, transmap|V_SNAPTOTOP, icon, R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_JAWZ, GTC_MENUCACHE));
		else
			V_DrawFixedPatch(pi->x + pi->items[i].x + xoffs, pi->y + yoffs, FRACUNIT, transmap|V_SNAPTOTOP, icon, (i == pi->itemon) ? activemap : inactivemap);

		if (i == pi->itemon)
		{
			V_DrawCharacter((pi->x + pi->items[i].x)/FRACUNIT + 4, pi->y/FRACUNIT + 14,
				'\x1A' | transmap|V_SNAPTOTOP|highlightflags, false);

			V_DrawCenteredStringAtFixed(BASEVIDWIDTH*FRACUNIT/2, pi->y + 18*FRACUNIT, transmap|V_SNAPTOTOP|V_ALLOWLOWERCASE, pi->items[i].text);

			if (pi->items[i].status & IT_ARROWS)
			{
				char *str = NULL;

				if (!(i == M_GetMenuIndex(pi, "NUMVIEWS") && r_splitscreen == 3))
					V_DrawCharacter(BASEVIDWIDTH/2 - 4, pi->y/FRACUNIT + 28 - (skullAnimCounter/5),
						'\x1A' | transmap|V_SNAPTOTOP|highlightflags, false); // up arrow

				if (!(i == M_GetMenuIndex(pi, "NUMVIEWS") && r_splitscreen == 0))
					V_DrawCharacter(BASEVIDWIDTH/2 - 4, pi->y/FRACUNIT + 48 + (skullAnimCounter/5),
						'\x1B' | transmap|V_SNAPTOTOP|highlightflags, false); // down arrow

				if (i == M_GetMenuIndex(pi, "NUMVIEWS"))
					str = va("%d", r_splitscreen+1);
				else if (splitnum >= 0 && splitnum < 4)
					str = player_names[displayplayers[splitnum]]; // 0 to 3
				else
					I_Error("bruh");

				V_DrawCenteredStringAtFixed(BASEVIDWIDTH*FRACUNIT/2, pi->y + 38*FRACUNIT, transmap|V_SNAPTOTOP|V_ALLOWLOWERCASE|highlightflags, str);
			}
		}
	}
}

int32_t MR_PlaybackRewind(int32_t choice)
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

int32_t MR_PlaybackPause(int32_t choice)
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

int32_t MR_PlaybackFastForward(int32_t choice)
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

int32_t MR_PlaybackAdvance(int32_t choice)
{
	(void)choice;

	paused = false;
	TryRunTics(1);
	paused = true;
	return true;
}

int32_t MR_PlaybackSetViews(int32_t choice)
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

int32_t MR_PlaybackAdjustView(int32_t choice)
{
	int16_t splitnum = M_IsItemOn(&curpanels[panelOn], "VIEW1") ? 1 :
	                 M_IsItemOn(&curpanels[panelOn], "VIEW2") ? 2 :
	                 M_IsItemOn(&curpanels[panelOn], "VIEW3") ? 3 :
	                 M_IsItemOn(&curpanels[panelOn], "VIEW4") ? 4 : -1;

	if (splitnum < 1 || splitnum > 4)
		return false;

	G_AdjustView(splitnum, (choice > 0) ? 1 : -1, true);
	return true;
}

// this one's rather tricky
int32_t MR_PlaybackToggleFreecam(int32_t choice)
{
	(void)choice;
	M_ClearMenus(true);

	// remove splitscreen:
	splitscreen = 0;
	R_ExecuteSetViewSize();

	G_SyncDemoParty(consoleplayer, 0);
	uint8_t i;
	for (i = 0; i <= r_splitscreen; ++i)
	{
		P_ToggleDemoCamera(i);
	}
	return true;
}

int32_t MR_PlaybackToggleDirector(int32_t choice)
{
	(void)choice;
	M_ClearMenus(true);

	// remove splitscreen:
	splitscreen = 0;
	R_ExecuteSetViewSize();

	G_SyncDemoParty(consoleplayer, 0);
	K_ToggleDirector();
	return true;
}

int32_t MR_PlaybackQuit(int32_t choice)
{
	(void)choice;
	uint8_t wasmodeattacking = modeattacking;
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
		D_ClearState();
		M_StartControlPanel();
	}
	return true;
}

int32_t MR_ChangeLevel(int32_t choice)
{
	(void)choice;
	M_ClearMenus(true);
	COM_BufAddText(va("map %d -gametype \"%s\"\n", cv_nextmap.value, cv_newgametype.string));
	return true;
}

int32_t MR_ConfirmSpectate(int32_t choice)
{
	(void)choice;
	// We allow switching to spectator even if team changing is not allowed
	M_ClearMenus(true);
	COM_ImmedExecute("changeteam spectator");
	return true;
}

int32_t MR_ConfirmEnterGame(int32_t choice)
{
	(void)choice;
	if (!cv_allowteamchange.value)
	{
		M_StartMessage2("Team Change", M_GetText("The server is not allowing\nteam changes at this time.\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
		return false;
	}
	M_ClearMenus(true);
	COM_ImmedExecute("changeteam playing");
	return true;
}

int32_t MR_ConfirmTeamScramble(int32_t choice)
{
	(void)choice;
	M_ClearMenus(true);

	COM_ImmedExecute(va("teamscramble %d", cv_dummyscramble.value+1));
	return true;
}

int32_t MR_ConfirmTeamChange(int32_t choice)
{
	(void)choice;

	if (cv_dummymenuplayer.value > splitscreen+1)
		return false;

	if (!cv_allowteamchange.value && cv_dummyteam.value)
	{
		M_StartMessage2("Team Change", M_GetText("The server is not allowing\nteam changes at this time.\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
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

int32_t MR_ConfirmSpectateChange(int32_t choice)
{
	(void)choice;

	if (cv_dummymenuplayer.value > splitscreen+1)
		return false;

	if (!cv_allowteamchange.value && cv_dummyspectate.value)
	{
		M_StartMessage2("Team Change", M_GetText("The server is not allowing\nteam changes at this time.\n"), MB_NONE, MM_NOTHING, "Press any key", NULL);
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

void MP_Options(menupanelinst_t *pi)
{
	dboolean gamecheck = (Playing() || demo.playback);

	// if the player is not admin or server, disable gameplay & server options
	M_SetItemDisabled(pi, "GAMEPLAY", Playing() && !(server || IsPlayerAdmin(consoleplayer)));
	M_SetItemDisabled(pi, "SERVER", Playing() && !(server || IsPlayerAdmin(consoleplayer)));

	// no credits or data erasing in-game/during replays
	M_SetItemDisabled(pi, "KARTCREDITS", gamecheck);
	M_SetItemDisabled(pi, "EXTRAREDITS", gamecheck);
	//M_SetItemDisabled(pi, "BLANCREDITS", gamecheck);
	//M_SetItemDisabled(pi, "SECRETCREDS", gamecheck);

	// FIXME M_SetItemDisabled(MN_OP_DATA, "ERASE", gamecheck);

	// FIXME M_SetItemDisabled(pi, "CUSTOM", !menudefs[MN_OP_CUSTOM].numitems);

#ifdef HAVE_DISCORDRPC
	// FIXME M_SetItemVisible(MN_OP_DATA, "DISCORD", true);
#else
	// FIXME M_SetItemVisible(MN_OP_DATA, "DISCORD", false);
#endif
}

void MP_GameOptions(menupanelinst_t *pi)
{
	M_SetItemSecret(pi, "ENCORE", !M_SecretUnlocked(SECRET_ENCORE));
}

static void M_RetryResponse(int32_t ch)
{
	if (ch != MA_YES)
		return;

	if (netgame) // Should never happen!
		return;

	M_ClearMenus(true);
	G_SetRetryFlag();
}

int32_t MR_Retry(int32_t choice)
{
	(void)choice;
	M_StartMessage2("Retry?", va("Start this %s over?\n", (gametypes[gametype]->rules & GTR_CIRCUIT) ? "race" : "battle"), MB_ROUTINE(M_RetryResponse),MM_YESNO, M_MessageConfirmText(false, false), M_MessageCancelText(false, false));
	return true;
}

int32_t MR_SelectableClearMenus(int32_t choice)
{
	(void)choice;
	M_ClearMenus(true);
	return true;
}

int32_t MR_GoBack(int32_t choice)
{
	(void)choice;
	M_ExitMenu();
	return true;
}

void M_RefreshPauseMenu(void)
{
#ifdef HAVE_DISCORDRPC
	if (menustack[0] == MN_MPAUSE)
		M_SetItemDisabled(&curpanels[0], "DISCORDREQUESTS", discordRequestList == NULL);
#endif
}

// ========
// SKY ROOM
// ========

uint8_t skyRoomMenuTranslations[MAXUNLOCKABLES] = {};

static char *M_GetConditionString(const condition_t *cond)
{
	switch (cond->type)
	{
		case UC_PLAYTIME:
			return va("Play for %i:%02i:%02i",
			          G_TicsToHours(cond->requirement),
			          G_TicsToMinutes(cond->requirement, false),
			          G_TicsToSeconds(cond->requirement));
		case UC_MATCHESPLAYED:
			return va("Play %d matches", cond->requirement);
		case UC_POWERLEVEL:
			return va("Reach power level %d in %s", cond->requirement, (cond->extrainfo1 == PWRLV_BATTLE ? "Battle" : "Race"));
		case UC_GAMECLEAR:
			if (cond->requirement > 1)
				return va("Beat game %d times", cond->requirement);
			else
				return va("Beat the game");
		case UC_OVERALLTIME:
			return va("Get overall Time Attack of %i:%02i:%02i",
			          G_TicsToHours(cond->requirement),
			          G_TicsToMinutes(cond->requirement, false),
			          G_TicsToSeconds(cond->requirement));
		case UC_MAPVISITED:
			return va("Visit %s", G_BuildMapTitle(cond->requirement-1));
		case UC_MAPBEATEN:
			return va("Beat %s", G_BuildMapTitle(cond->requirement-1));
		case UC_MAPENCORE:
			return va("Beat %s in Encore Mode", G_BuildMapTitle(cond->requirement-1));
		case UC_MAPTIME:
			return va("Beat %s in %i:%02i.%02i", G_BuildMapTitle(cond->extrainfo1-1),
			          G_TicsToMinutes(cond->requirement, true),
			          G_TicsToSeconds(cond->requirement),
			          G_TicsToCentiseconds(cond->requirement));
		case UC_TOTALEMBLEMS:
			return va("Get %d medals", cond->requirement);
		case UC_EXTRAEMBLEM:
			return va("Get \"%s\" medal", extraemblems[cond->requirement-1].name);
		default:
			return NULL;
	}
}

void MP_UnlockablesChecklist(menupanelinst_t *pi)
{
	M_ClearPanelItems(pi);
	pi->altscroll = true;

	for (uint32_t i = 0; i < MAXUNLOCKABLES; i++)
	{
		dboolean secret = !M_Achieved(unlockables[i].showconditionset - 1) && !unlockables[i].unlocked;

		if (unlockables[i].name[0] == 0 || unlockables[i].nochecklist
		|| !unlockables[i].conditionset || unlockables[i].conditionset > MAXCONDITIONSETS
		|| (unlockables[i].type == SECRET_HELLATTACK && secret)) // TODO: turn this into an unlockable setting instead of tying it to Hell Attack
			continue;

		menuitem_t *item = M_AppendMenuItem(pi, 1, 8*FRACUNIT, IT_OFSY); // FIXME autolayout
		M_SetItemTextRaw(item, secret ? M_CreateSecretMenuOption(unlockables[i].name) : unlockables[i].name);
		item->style = DEH_FindMenuItemStyle(unlockables[i].unlocked ? "RECOMMENDEDHIGHLIGHT" : "WARNINGHIGHLIGHT");

		uint32_t lastid = UINT32_MAX;
		for (uint32_t c = 0; c < conditionSets[unlockables[i].conditionset - 1].numconditions; c++)
		{
			const condition_t *cond = &conditionSets[unlockables[i].conditionset - 1].condition[c];
			CLEANUP(pfree) char *str = strdup(M_GetConditionString(cond)); // va moment

			if (str == NULL)
				continue;

			item = M_AppendMenuItem(pi, 8*FRACUNIT, 0, 0);
			const char *bullet = "*";
			if (lastid != UINT32_MAX && cond->id == lastid)
				bullet = secret ? "   ?" : "   &";

			M_SetItemTextRaw(item, va("%s %s", bullet, secret ? M_CreateSecretMenuOption(str) : str));
			if (M_CheckCondition(cond))
				item->style = DEH_FindMenuItemStyle("HIGHLIGHT");

			lastid = cond->id;
		}
	}
}

int32_t MR_PlaySound(int32_t arg)
{
	S_StopSounds();
	S_StartSound(NULL, arg);
	return true;
}

static musicdef_t *curplaying = NULL;
static int32_t st_sel = 0;
static tic_t st_time = 0;
static size_t st_namescroll = 0;
static size_t st_namescrollstate = 0;

int32_t MR_MusicTest(int32_t choice)
{
	//int32_t ul = skyRoomMenuTranslations[choice-1];
	//uint8_t i;
	//char buf[8];
	(void)choice;

	if (!S_PrepareSoundTest())
	{
		M_StartMessage2("Music Test", M_GetText("No selectable tracks found.\n"),MB_NONE,MM_NOTHING, "Press any Key", NULL);
		return false;
	}

	curplaying = NULL;
	st_time = 0;
	st_sel = 0;
	return true;
}

void MD_DrawMusicTest(menupanelinst_t *pi)
{
	(void)pi;

	int32_t x, y, i;

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
		int32_t t, b, q, m = 128;

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

int32_t MR_HandleMusicTest(int32_t choice)
{
	if (menucmd[0].dpad_ud > 0)
	{
		if (st_sel++ >= numsoundtestdefs-1)
			st_sel = 0;
		{
			S_StartSound(NULL, sfx_menu1);
		}
		st_namescroll = 0;
		st_namescrollstate = 0;
		M_SetMenuDelay(0);
	}
	else if (menucmd[0].dpad_ud < 0)
	{
		if (!st_sel--)
			st_sel = numsoundtestdefs-1;
		{
			S_StartSound(NULL, sfx_menu1);
		}
		st_namescroll = 0;
		st_namescrollstate = 0;
		M_SetMenuDelay(0);
	}
	else if (M_MenuButtonPressed(0, MBT_INFO2))
	{
		if (curplaying)
		{
			S_StopSounds();
			S_StopMusic();
			curplaying = NULL;
			st_time = 0;
			S_StartSound(NULL, sfx_skid);
			M_SetMenuDelay(0);
		}
	}
	else if (menucmd[0].dpad_lr != 0 || M_MenuButtonPressed(0, MBT_CONFIRM))
	{
		S_StopSounds();
		S_StopMusic();
		st_time = 0;
		curplaying = soundtestdefs[st_sel];
		S_ChangeMusicInternal(&curplaying->name[0][0], true);
		M_SetMenuDelay(0);
	}
	else
	{
		switch (choice)
		{
			default:
				return false;
		}
	}

	return true;
}

int32_t MR_QuitMusicTest(int32_t choice)
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

int32_t MR_Credits(int32_t choice)
{
	(void)choice;
	cursaveslot = -2;
	M_ClearMenus(true);
	F_StartCredits();
	return true;
}

int32_t MR_BlanCredits(int32_t choice)
{
	(void)choice;
	cursaveslot = -2;
	M_ClearMenus(true);
	F_BlanStartCredits();
	return true;
}

int32_t MR_SecretCredits(int32_t choice)
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

void MP_SinglePlayerMain(menupanelinst_t *pi)
{
	M_SetItemSecret(pi, "GRANDPRIX", false);
	M_SetItemSecret(pi, "TIMEATTACK", !M_SecretUnlocked(SECRET_TIMEATTACK));
	M_SetItemSecret(pi, "ITEMBREAKER", !M_SecretUnlocked(SECRET_ITEMBREAKER));
}

// ===============
// STATISTICS MENU
// ===============

void MP_StatsRecords(menupanelinst_t *pi)
{
	menuitem_t *item;
	fixed_t y = 0;
	int32_t i;
	dboolean showemblems = G_EmblemsEnabled();

	CLEANUP(pfree) int16_t *statsMapList = malloc(nummapheaders * sizeof(*statsMapList));
	int16_t statsMapListLen = 0;

	for (i = 0; i < nummapheaders; i++)
	{
		if (!mapheaderinfo[i] || mapheaderinfo[i]->lvlttl[0] == '\0')
			continue;

		if (mapheaderinfo[i]->menuflags & (LF2_HIDEINSTATS|LF2_HIDEINMENU|LF2_HIDEINVOTE))
			continue;

		if (M_MapLocked(i+1)) // !mapvisited[i]
			continue;

		statsMapList[statsMapListLen++] = i;
	}

	M_ClearPanelItems(pi);
	pi->altscroll = true;

	for (i = -1; i < statsMapListLen; i++, y += 8*FRACUNIT)
	{
		item = M_AppendMenuItem(pi, 1, y, 0); // FIXME autolayout

		if (i == -1)
		{
			item->style = DEH_FindMenuItemStyle("STATSMAPHEADING");
			M_SetItemTextRaw(item, "Level Name");
			M_SetItemPatchRaw(item, "Best");
			M_SetItemExtraRaw(item, "Play");
			//V_DrawString(256, y, MENUCAPS|highlightflags, "Medals");
			continue;
		}

		item->style = DEH_FindMenuItemStyle("STATSMAP");

		int16_t mnum = statsMapList[i];
		maprecord_t *record = G_GetMapRecord(G_BuildMapName(mnum+1));
		maprecordpreset_t *preset = record ? G_GetMapRecordPreset(record, currentrecordpreset) : NULL;
		CLEANUP(Z_Pfree) char *maptitle = G_BuildMapTitle(mnum+1);

		M_SetItemTextRaw(item, maptitle);
		M_SetItemPatchRaw(item, preset == NULL || preset->besttime == UINT32_MAX ? "--'--\"--" :
			va("%02d'%02d\"%02d",
			G_TicsToMinutes(preset->besttime, true),
			G_TicsToSeconds(preset->besttime),
			G_TicsToCentiseconds(preset->besttime)));
		M_SetItemExtraRaw(item, preset == NULL || preset->playtime == 0 ? "-:--:--" :
			va("%d:%02d:%02d",
			G_TicsToHours(preset->playtime),
			G_TicsToMinutes(preset->playtime, false),
			G_TicsToSeconds(preset->playtime)));

		if (!showemblems)
			continue;

		fixed_t x = 275*FRACUNIT;
		uint8_t lasttype = UINT8_MAX, curtype;
		emblem_t *emblem = M_GetLevelEmblems(mnum);
		for (; emblem != NULL; emblem = M_GetLevelEmblems(NEXTMAP_INVALID), x -= 8*FRACUNIT)
		{
			menuitem_t *itemblem = M_AppendMenuItem(pi, x, y, 0);
			itemblem->style = DEH_FindMenuItemStyle("PATCH-SMALL");

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
				x -= 4*FRACUNIT;
			lasttype = curtype;

			if (emblem->collected)
				M_SetItemPatchRaw(itemblem, M_GetEmblemPatch(emblem, false));
				// FIXME R_GetTranslationColormap(TC_DEFAULT, M_GetEmblemColor(emblem), GTC_MENUCACHE));
			else
				M_SetItemPatchRaw(itemblem, "NEEDIT");
		}
	}

	// Extra Emblems
	for (i = -2; i < numextraemblems; i++, y += 8*FRACUNIT)
	{
		item = M_AppendMenuItem(pi, 1, y, 0); // FIXME autolayout
		if (i < 0)
		{
			if (i == -1)
			{
				item->style = DEH_FindMenuItemStyle("STATSMAPHEADING");
				M_SetItemTextRaw(item, "Extra Medals");
			}
			continue;
		}

		extraemblem_t *exemblem = &extraemblems[i];

		item->style = DEH_FindMenuItemStyle("STATSMAP");
		M_SetItemTextRaw(item, va("%s", exemblem->description));

		menuitem_t *itemblem = M_AppendMenuItem(pi, 275*FRACUNIT, y, 0);
		itemblem->style = DEH_FindMenuItemStyle("PATCH-SMALL");
		if (exemblem->collected)
			M_SetItemPatchRaw(itemblem, M_GetExtraEmblemPatch(exemblem, false));
			// FIXME R_GetTranslationColormap(TC_DEFAULT, M_GetExtraEmblemColor(exemblem), GTC_MENUCACHE));
		else
			M_SetItemPatchRaw(itemblem, "NEEDIT");
	}
}

void MP_StatsRecordsBG(menupanelinst_t *pi)
{
	tic_t besttime = 0;
	int32_t mapsfinished = 0;
	for (int32_t i = 0; i < nummapheaders; i++)
	{
		if (!mapheaderinfo[i] || (mapheaderinfo[i]->menuflags & LF2_NOTIMEATTACK))
			continue;

		maprecord_t *record = G_GetMapRecord(G_BuildMapName(i+1));
		if (record == NULL)
			continue;

		maprecordpreset_t *preset = G_GetMapRecordPreset(record, currentrecordpreset);
		if (preset == NULL)
			continue;

		besttime += preset->besttime;
		mapsfinished++;
	}

	char *beststr = va("%dh %dm %ds", G_TicsToHours(besttime), G_TicsToMinutes(besttime, false), G_TicsToSeconds(besttime));
	M_SetItemText(pi, "TOTALTIME", beststr);
	M_GetMenuItem(pi, "TOTALTIME")->style = DEH_FindMenuItemStyle(mapsfinished != nummapheaders ? "EDGE-WARNINGHIGHLIGHT" : "EDGE");
	if (mapsfinished != nummapheaders)
	{
		M_SetItemText(pi, "MAPSFINISHED", va("(%d unfinished)", nummapheaders - mapsfinished));
		M_GetMenuItem(pi, "MAPSFINISHED")->style = DEH_FindMenuItemStyle("EDGE-WARNINGHIGHLIGHT");
	}
	else
	{
		M_SetItemText(pi, "MAPSFINISHED", "(complete)");
		M_GetMenuItem(pi, "MAPSFINISHED")->style = DEH_FindMenuItemStyle("EDGE-RECOMMENDEDHIGHLIGHT");
	}

	dboolean showemblems = G_EmblemsEnabled();
	M_SetItemText(pi, "EMBLEMCOUNT", va("x %d/%d", M_CountEmblems(), numemblems+numextraemblems));
	M_SetItemVisible(pi, "EMBLEMCOUNT", showemblems);
	M_SetItemVisible(pi, "GOTIT", showemblems);

	const char *realname = G_GetRecordPresetName(currentrecordpreset);
	M_SetItemText(pi, "MODE", realname ? realname : currentrecordpreset);
}

#define TIMESTAT(title, field) { \
		tic_t t = kartstats.field; \
		M_SetItemPatch(pi, title, va("%02i:%02i:%02i", G_TicsToHours(t), G_TicsToMinutes(t, false), G_TicsToSeconds(t))); \
	}

#define AMOUNTSTAT(title, field) { \
		unsigned t = kartstats.field; \
		M_SetItemPatch(pi, title, va("%u", t)); \
	}

void MP_StatsPlaytime(menupanelinst_t *pi)
{
	M_SetItemText(pi, "PLAYTIME", va("%i hours, %i minutes, %i seconds",
	                         G_TicsToHours(kartstats.totalplaytime),
	                         G_TicsToMinutes(kartstats.totalplaytime, false),
	                         G_TicsToSeconds(kartstats.totalplaytime)));
	M_SetItemText(pi, "MATCHES", va("%i played", kartstats.matchesplayed));

	M_SetItemText(pi, "RACEPWRLV", va("Race: %i", vspowerlevel[PWRLV_RACE]));
	M_SetItemText(pi, "BATTLEPWRLV", va("Battle: %i", vspowerlevel[PWRLV_BATTLE]));

	TIMESTAT("RAPLAYTIME", raplaytime);
	TIMESTAT("ONLINEPLAYTIME", onlineplaytime);
	TIMESTAT("RACEPLAYTIME", raceplaytime);
	TIMESTAT("BATTLEPLAYTIME", battleplaytime);

	M_SetItemVisible(pi, "RAPLAYTIME", !kartstats.vanilla);
	M_SetItemVisible(pi, "ONLINEPLAYTIME", !kartstats.vanilla);
	M_SetItemVisible(pi, "RACEPLAYTIME", !kartstats.vanilla);
	M_SetItemVisible(pi, "BATTLEPLAYTIME", !kartstats.vanilla);
}

// Note: only available with non-vanilla stats loaded, so it doesn't check for that
void MP_StatsExtra(menupanelinst_t *pi)
{
	TIMESTAT("SPBTARGETTIME", spbtargettime);
	TIMESTAT("SPINOUTTIME", spinouttime);
	AMOUNTSTAT("TOTALWINS", totalwins);
	AMOUNTSTAT("TOTALPODIUM", totalpodium);
	AMOUNTSTAT("HITS", hits);
	AMOUNTSTAT("SELFHITS", selfhits);
	AMOUNTSTAT("SINKS", sinks);
	AMOUNTSTAT("SINKED", sinked);
	AMOUNTSTAT("RESPAWNS", respawns);
	AMOUNTSTAT("PERFECTMEDALS", perfectmedals);
}

void MD_DrawLevelStats(menupanelinst_t *pi)
{
	(void)pi;

	int32_t w = V_StringWidth(curpanels[panelOn].name, highlightflags);
	V_DrawCharacter(BASEVIDWIDTH/2 - w/2 - 10 - (skullAnimCounter/5), 28,
			'\x1C' | highlightflags, false); // left arrow
	V_DrawCharacter(BASEVIDWIDTH/2 + w/2 + 2 + (skullAnimCounter/5), 28,
			'\x1D' | highlightflags, false); // right arrow
}

int32_t MR_TickStats(int32_t choice)
{
	(void)choice;

	menupanelinst_t *pi = M_GetPanel("MAIN");

	// cursoroffset != 0 means the panel can be navigated to
	// yup!
	M_GetPanel("EXTRA")->cursoroffset = kartstats.vanilla ? 0 : 3*FRACUNIT;

	for (size_t i = 0; i < numcurpanels; i++)
		if (i != (size_t)(pi - curpanels))
			curpanels[i].visible = i == panelOn;

	if (M_CheckPanel("RECORDS") == &curpanels[panelOn])
		M_GetPanel("RECORDSBG")->visible = true; // ugh

	// FIXME
	//title = "Play Time Statistics"; break;
	//title = "Level Statistics"; break;
	//title = "Extra Statistics"; break;
	M_SetItemText(pi, "HEADING", curpanels[panelOn].name);

	return true;
}

// Handle statistics.
int32_t MR_HandleLevelStats(int32_t choice)
{
	(void)choice;

	if (menucmd[0].dpad_lr > 0)
	{
		S_StartSound(NULL, sfx_menu1);
		M_SetMenuDelay(0);
		do
			panelOn = intmod(panelOn + 1, numcurpanels);
		while (curpanels[panelOn].cursoroffset == 0);
	}
	else if (menucmd[0].dpad_lr < 0)
	{
		S_StartSound(NULL, sfx_menu1);
		M_SetMenuDelay(0);
		do
			// sadly, panelOn is size_t, which means integer promotion does not
			// convert it from unsigned to signed, which means this line doesn't work:
			//panelOn = intmod(panelOn - 1, numcurpanels);
			// fuck this chud ass language
			if (panelOn-- == 0) panelOn = numcurpanels-1;
		while (curpanels[panelOn].cursoroffset == 0);
	}
	else if (M_MenuButtonPressed(0, MBT_INFO2))
	{
		menupanelinst_t *maps = M_CheckPanel("RECORDS");
		if (&curpanels[panelOn] != maps)
			return true;
		S_StartSound(NULL, sfx_menu1);
		M_SetMenuDelay(0);
		recordpresetnum_e i, j;
		for (i = 0; i < numrecordpresets; i++)
		{
			if (fastcmp(currentrecordpreset, recordpresets[i]->name))
				break;
		}

		for (j = i; (i = (i + 1) % numrecordpresets) != j;)
		{
			if (recordpresets[i]->numversions)
			{
				G_SetCurrentRecordPreset(recordpresets[i]);
				break;
			}
		}

		M_GetDefFromInst(maps)->initializer(maps);
		M_GetDefFromInst(M_GetPanel("RECORDSBG"))->initializer(M_GetPanel("RECORDSBG"));
	}
	else
		return false;

	return true;
}

int32_t MR_GrandPrixTemp(int32_t choice)
{
	(void)choice;
	if (!M_CheckNextCup(NULL))
	{
		M_StartMessage2("GrandPrix", M_GetText("No cups found for Grand Prix.\n"),MB_NONE,MM_NOTHING, "Press any key", NULL);
		return false;
	}
	M_PatchSkinNameTable();
	return true;
}

// Start Grand Prix!
int32_t MR_StartGrandPrix(int32_t choice)
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
	grandprixinfo.ranked = (dboolean)(cv_kartraceranks.value);
	grandprixinfo.wonround = false;
	grandprixinfo.initalize = true;
	grandprixinfo.cup = gpcup;

	// Populate the roundqueue
	memset(&roundqueue, 0, sizeof(struct roundqueue));
	G_GPCupIntoRoundQueue(gpcup, GT_RACE/*levellist.newgametype*/, (dboolean)cv_dummygpencore.value);
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

void MP_TimeAttackMenu(menupanelinst_t *pi)
{
	// see also p_setup.c's P_LoadRecordGhosts
	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(true, levellistmode == LLM_ITEMBREAKER);

	dboolean visible;
	dboolean showreplay = false, showguest = false;

	// best time
	visible = G_CheckRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, REPLAY_BESTTIME);
	M_MaybeSetItemVisible(pi, "REPLAYTIME", visible);
	if (visible)
		showreplay = showguest = true;

	// best lap
	visible = levellistmode != LLM_ITEMBREAKER && G_CheckRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, REPLAY_BESTLAP);
	M_MaybeSetItemVisible(pi, "REPLAYLAP", visible);
	if (visible)
		showreplay = showguest = true;

	// last
	visible = G_CheckRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, REPLAY_LAST);
	M_MaybeSetItemVisible(pi, "REPLAYLAST", visible);
	if (visible)
		showreplay = showguest = true;

	// guest
	visible = G_CheckRecordReplay(gpath, cv_nextmap.value, UINT16_MAX, REPLAY_GUEST);
	M_MaybeSetItemVisible(pi, "REPLAYGUEST", visible);
	if (visible)
		showreplay = showguest = true;

	// staff
	CV_StealthSetValue(&cv_dummystaff, 0);
	CV_SetValue(&cv_dummystaff, 1);
	visible = cv_dummystaff.value != 0;
	M_MaybeSetItemVisible(pi, "REPLAYSTAFF", visible);
	if (visible)
	{
		CV_StealthSetValue(&cv_dummystaff, 1);
		showreplay = true;
	}

	// show the actual replay/guest options, for the main TA menu
	M_MaybeSetItemVisible(pi, "SHOWREPLAY", showreplay);
	M_MaybeSetItemVisible(pi, "SHOWGUEST", showguest);
}

void MP_TimeAttackPresets(menupanelinst_t *pi)
{
	M_ClearPanelItems(pi);

	menuitem_t *item;
	for (size_t i = 0; i < numrecordpresets; i++)
	{
		item = M_AppendMenuItem(pi, 0, 0, IT_INTERACT);
		item->routine = MR_TimeAttackPreset;
		item->argument = i;
		item->style = DEH_FindMenuItemStyle("HIGHLIGHT");
		M_SetItemTextRaw(item, recordpresets[i]->realname);
	}
	item = M_AppendMenuItem(pi, 0, 6*FRACUNIT, IT_INTERACT|IT_OFSY);
	item->routine = MR_GoBack;
	item->style = DEH_FindMenuItemStyle("HIGHLIGHT");
	M_SetItemTextRaw(item, "Back");
}

dboolean M_AttackOnStack(void)
{
	uint16_t i;

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

void MP_TickTimeAttackMenu(menupanelinst_t *pi)
{
	// Character face!
	M_SetItemPatch(pi, "PLAYERICON", skins[cv_chooseskin.value]->name);
	M_SetItemExtra(pi, "PLAYERICON", DEH_SkincolorName(cv_playercolor[0].value));
}

// Drawing function for Time Attack
void MD_DrawTimeAttackMenu(menupanelinst_t *pi)
{
	(void)pi;

	maprecordpreset_t *preset = NULL;
	maprecord_t *record = G_GetMapRecord(G_BuildMapName(cv_nextmap.value));
	if (record != NULL)
		preset = G_GetMapRecordPreset(record, currentrecordpreset);

	// Level record list
	if (cv_nextmap.value)
	{
		int32_t dupadjust = (vid.scaledwidth);
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
}

// Going to Time Attack menu...
int32_t MR_TimeAttack(int32_t arg)
{
	if (arg >= LLM__MAX)
		return false;

	if (arg >= 0)
	{
		levellistmode_ta = arg;
		if (!M_CheckNextMap(NULL))
		{
			const char * string = arg == LLM_ITEMBREAKER ? "Item Breaker" : "Time Attack";

			M_StartMessage2(string, M_GetText(va("No levels found for %s.\n", string)), MB_NONE, MM_NOTHING, "Press any key", NULL);
			return false;
		}
	}

	M_PatchSkinNameTable();
	//M_ClearMenus(true);

	modeattacking = ATTACKING_MENU;
	titlemapinaction = false; // Nope don't give us HOMs please

	// FIXME
	// M_SetItemOn(&curpanels[panelOn], "START"); // "Start" is selected.

	return true;
}

int32_t MR_QuitTimeAttackMenu(int32_t choice)
{
	(void)choice;

	// you know what? always putting these in the buffer won't hurt anything.
	COM_BufAddText(va("skin \"%s\"\n", skins[cv_chooseskin.value]->name));

	if (!M_AttackOnStack())
	{
		modeattacking = ATTACKING_NONE;
	}

	if (modeattacking == ATTACKING_NONE)
		levellistmode_ta = LLM__MAX;

	S_StopMusicCredit();
	return true;
}

// Player has selected the "START" from the time attack screen
int32_t MR_ChooseTimeAttack(int32_t choice)
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

int32_t MR_ReplayStaff(int32_t choice)
{
	(void)choice;
	lumpnum_t l = W_CheckNumForName(va("%sS%02u",G_BuildMapName(cv_nextmap.value),cv_dummystaff.value));
	if (l == LUMPERROR)
		return false;

	M_RestoreMenuStack(true);
	M_ClearMenusEx(true, true);
	COM_BufAddText(va("playdemo %sS%02u -force", G_BuildMapName(cv_nextmap.value), cv_dummystaff.value));
	return true;
}

int32_t MR_TimeAttackPreset(int32_t arg)
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
int32_t MR_ReplayTimeAttack(int32_t arg)
{
	if (arg < 0 || arg >= REPLAY__MAX)
		return false;

	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(false, levellistmode == LLM_ITEMBREAKER);
	CLEANUP(pfree) char *demopath = G_GetRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, arg);

	M_RestoreMenuStack(true);
	M_ClearMenusEx(true, true);
	COM_BufAddText(va("playdemo \"%s\" -force", demopath));
	return true;
}

static void M_EraseGuest(int32_t choice)
{
	if (choice != MA_YES)
		return;

	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(true, levellistmode == LLM_ITEMBREAKER);
	CLEANUP(pfree) char *rguest = G_GetRecordReplay(gpath, cv_nextmap.value, UINT16_MAX, REPLAY_GUEST);

	if (FIL_FileExists(rguest))
		remove(rguest);
	M_ExitMenu();
	M_StartMessage2("Guest", M_GetText("Guest replay data erased.\n"),MB_NONE,MM_NOTHING, "Press any key", NULL);
}

static void M_OverwriteGuest(recordreplay_e which)
{
	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(true, levellistmode == LLM_ITEMBREAKER);
	CLEANUP(pfree) char *rguest = G_GetRecordReplay(gpath, cv_nextmap.value, UINT16_MAX, REPLAY_GUEST);
	CLEANUP(pfree) char *overwrite = G_GetRecordReplay(gpath, cv_nextmap.value, cv_chooseskin.value, which);
	uint8_t *buf;
	size_t len = FIL_ReadFile(overwrite, &buf);
	if (!len) {
		return;
	}
	if (FIL_FileExists(rguest)) {
		M_MessageboxClose(false);
		remove(rguest);
	}
	FIL_WriteFile(rguest, buf, len);
	M_ExitMenu();
	M_StartMessage2("Guest", M_GetText("Guest replay data saved.\n"),MB_NONE,MM_NOTHING, "Press any key", NULL);
}

static void M_OverwriteGuest_Time(int32_t choice)
{
	if (choice != MA_YES)
		return;

	M_OverwriteGuest(REPLAY_BESTTIME);
}

static void M_OverwriteGuest_Lap(int32_t choice)
{
	if (choice != MA_YES)
		return;

	M_OverwriteGuest(REPLAY_BESTLAP);
}

static void M_OverwriteGuest_Last(int32_t choice)
{
	if (choice != MA_YES)
		return;

	M_OverwriteGuest(REPLAY_LAST);
}

int32_t MR_SetGuestReplay(int32_t arg)
{
	void (*which)(int32_t);
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
		M_StartMessage2("Guest", M_GetText("Are you sure you want to\ndelete the guest replay data?\n"), MB_ROUTINE(M_EraseGuest), MM_YESNO, M_MessageConfirmText(false, false), M_MessageCancelText(false, false));
		return true;
	}
	CLEANUP(pfree) char *gpath = G_GetRecordReplayFolder(true, levellistmode == LLM_ITEMBREAKER);
	if (G_CheckRecordReplay(gpath, cv_nextmap.value, UINT16_MAX, REPLAY_GUEST))
		M_StartMessage2("Guest", M_GetText("Are you sure you want to\noverwrite the guest replay data?\n"), MB_ROUTINE(which), MM_YESNO, M_MessageConfirmText(false, false), M_MessageCancelText(false, false));
	else
		which(MA_YES);
	return true;
}

int32_t MR_ModeAttackRetry(int32_t choice)
{
	(void)choice;
	G_CheckDemoStatus(); // Cancel recording
	if (!modeattacking)
		return false;

	MR_ChooseTimeAttack(1);
	return true;
}

int32_t MR_ModeAttackEndGame(int32_t choice)
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

static void M_ExitGameResponse(int32_t ch)
{
	if (ch != MA_YES)
		return;

	//Command_ExitGame_f();
	G_SetExitGameFlag();
	M_ClearMenus(true);
}

int32_t MR_EndGame(int32_t choice)
{
	(void)choice;
	if (demo.playback)
		return false;

	if (!Playing())
		return false;

	M_StartMessage2("End Game", M_GetText("Are you sure you want to end the game?\n"), MB_ROUTINE(M_ExitGameResponse), MM_YESNO, M_MessageConfirmText(false, false), M_MessageCancelText(false, false));
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

#define S_LINEY(n) ((pi->y/FRACUNIT) + SERVERHEADERHEIGHT + (n * SERVERLINEHEIGHT))

int32_t MR_Connect(int32_t arg)
{
	// do not call menuexitfunc
	M_ClearMenusEx(false, true);

	CV_Set(&cv_lastserver, I_GetNodeAddress(serverlist[arg + cv_dummyserverpage.value * M_ServersPerPage()].node));

	COM_BufAddText(va("connect node %d\n", serverlist[arg + cv_dummyserverpage.value * M_ServersPerPage()].node));
	return true;
}

int32_t MR_Refresh(int32_t choice)
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

static void MD_DrawServerCountAndHorizontalBar(const menupanelinst_t *pi)
{
	const char *text;
	int32_t radius;
	int32_t center = BASEVIDWIDTH*FRACUNIT/2;

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

	radius = V_StringWidth(text, 0) * FRACUNIT/2;

	V_DrawCenteredStringAtFixed(center, pi->y + 28*FRACUNIT, 0, text);

	// Horizontal line!
	V_DrawFixedFill(FRACUNIT, pi->y + 32*FRACUNIT, center - radius - 2, FRACUNIT, 0);
	V_DrawFixedFill(center + radius + 2*FRACUNIT, pi->y + 32*FRACUNIT, (BASEVIDWIDTH - 1)*FRACUNIT, FRACUNIT, 0);
}

static void M_DrawServerLines(const menupanelinst_t *pi, int32_t x, int32_t page)
{
	uint16_t i;
	char servergametype[30];
	char pwr[16];
	uint16_t firstserverline = M_GetMenuIndex(pi, "LINE1");
	uint32_t serversperpage = M_ServersPerPage(); // server sperpage?

	for (i = 0; i < min(serverlistcount - page * serversperpage, serversperpage); i++)
	{
		int32_t slindex = i + page * serversperpage;
		uint32_t globalflags = ((serverlist[slindex].info.numberofplayer >= serverlist[slindex].info.maxplayer) ? V_TRANSLUCENT : 0)
		|((pi->itemon == firstserverline+i) ? highlightflags : 0)|V_ALLOWLOWERCASE;

		// min width is probably like 268px (sorry for shitty formatting)
		V_DrawFill(x - 3,
				   S_LINEY(i) - (i == 0 ? 3 : 0),
				   268 + 6, (i == 0 ? 15 : 12),
				   (pi->itemon == firstserverline+i) ? 153 : ((i & 1) ? 159 : 156)
		);

		V_DrawString(x, S_LINEY(i), globalflags, serverlist[slindex].info.servername);

		V_DrawSmallString(x, S_LINEY(i)+8, globalflags,
						  va("Ping: %u", (uint32_t)LSBF_LONG(serverlist[slindex].info.time)));

		V_DrawSmallString(x+44,S_LINEY(i)+8, globalflags,
						  va("Players: %02d/%02d", serverlist[slindex].info.numberofplayer, serverlist[slindex].info.maxplayer));

		snprintf(servergametype, sizeof(servergametype), "%s (%s)", serverlist[slindex].info.gametypename, kartspeed_cons_t[(serverlist[slindex].info.kartvars & SV_SPEEDMASK)+1].strvalue);
		V_DrawSmallString(x+108, S_LINEY(i)+8, globalflags, servergametype);

		if (serverlist[slindex].info.avgpwrlv == -1)
			snprintf(pwr, sizeof(pwr), "PWR.LV: Off");
		else if (serverlist[slindex].info.avgpwrlv > 0)
			snprintf(pwr, sizeof(pwr), "PWR.LV: %04d", serverlist[slindex].info.avgpwrlv);
		else
			snprintf(pwr, sizeof(pwr), "PWR.LV: ----");
		V_DrawSmallString(x+181, S_LINEY(i)+8, globalflags, pwr);

		// Don't use color flags intentionally, the global yellow color will auto override the text color code
		if (serverlist[slindex].info.modifiedgame)
			V_DrawSmallString(x+228, S_LINEY(i)+8, globalflags, "\x85" "Mod");
		if (serverlist[slindex].info.cheatsenabled)
			V_DrawSmallString(x+244, S_LINEY(i)+8, globalflags, "\x83" "Cheats");

		pi->items[i+firstserverline].status &= ~IT_HIDDEN;
	}
}

//static float serverlistslidex;
//int32_t oldserverlistpage;

void MD_DrawConnectMenu(menupanelinst_t *pi)
{
	uint16_t i;
	uint16_t firstserverline = M_GetMenuIndex(pi, "LINE1");
	uint32_t serversperpage = M_ServersPerPage(); // server sperpage?
	int32_t mservflags = V_ALLOWLOWERCASE;

	for (i = firstserverline; i < firstserverline+serversperpage; i++)
		pi->items[i].status |= IT_HIDDEN;

	// Did you change the Server Browser address? Have a little reminder.
	if (CV_IsSetToDefault(&cv_masterserver))
		mservflags = mservflags|highlightflags|V_30TRANS;
	else
		mservflags = mservflags|warningflags;
	V_DrawRightAlignedSmallString(BASEVIDWIDTH - pi->x/FRACUNIT, pi->y/FRACUNIT + 3 + M_GetMenuItem(pi, "REFRESH")->y,
								  mservflags, va("MS: %s", cv_masterserver.string));

	// When switching pages, slide the old page and the
	// new page across the screen
	/*if (oldserverlistpage != cv_dummyserverpage.value)
	{
		const float ease = serverlistslidex / 2.f;
		const int32_t offx = serverlistslidex > 0 ? BASEVIDWIDTH : -(BASEVIDWIDTH);
		const int32_t x = (FLOAT_TO_FIXED(serverlistslidex) + ease * rendertimefrac) / FRACUNIT;

		M_DrawServerLines(currentMenu->x + x - offx, oldserverlistpage);
		M_DrawServerLines(currentMenu->x + x, cv_dummyserverpage.value);

		if (renderisnewtic)
		{
			serverlistslidex -= ease;

			if ((int32_t)serverlistslidex == 0)
				oldserverlistpage = serverlistpage;
		}
	}
	else*/
	{
		M_DrawServerLines(pi, pi->x/FRACUNIT, cv_dummyserverpage.value);
	}

	MD_DrawServerCountAndHorizontalBar(pi);

	//MD_DrawGenericMenu();
}

int32_t MR_CancelConnect(int32_t choice)
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
		snprintf(updatestring, sizeof(updatestring), UPDATE_ALERT_STRING, VERSIONSTRING, updatecheck);
#ifdef HAVE_THREADS
		I_lock_mutex(&m_menu_mutex);
#endif
		M_StartMessage2("Master Server", updatestring, MB_NONE, MM_NOTHING, "Press any key", NULL);
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

static void M_ConnectMenu(int32_t choice)
{
	(void)choice;
	M_MessageboxClose(false);
	// modified game check: no longer handled
	// we don't request a restart unless the filelist differs

	// first page of servers
	CV_SetValue(&cv_dummyserverpage, 0);

	CL_UpdateServerList();

	M_EnterMenu(MN_MP_CONNECT, true, 0);
	M_SetItemOn(&curpanels[panelOn], "SORTING");

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

int32_t MR_ConnectMenuModChecks(int32_t choice)
{
	(void)choice;
	// okay never mind we want to COMMUNICATE to the player pre-emptively instead of letting them try and then get confused when it doesn't work

	if (modifiedgame)
	{
		M_StartMessage2("Notice", M_GetText("You have addons loaded.\nYou won't be able to join netgames!\n\nTo play online, restart the game\nand don't load any addons.\nSRB2Kart will automatically add\neverything you need when you join.\n"), MB_ROUTINE(M_ConnectMenu), MM_NOTHING, "Press any key", NULL);
		return true;
	}

	M_ConnectMenu(-1);
	return true;
}

//===========================================================================
// Start Server Menu
//===========================================================================

int32_t MR_StartServer(int32_t choice)
{
	uint8_t ssplayers = cv_splitplayers.value-1;

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

	M_ClearMenusEx(true, true);
	return true;
}

// ==============
// CONNECT VIA IP
// ==============

static void Splitplayers_OnChange(void)
{
	if (cv_splitplayers.value <= cv_dummymultiplayer.value)
		CV_SetValue(&cv_dummymultiplayer, 0);

	// anything to avoid a keyhandler :^)
	dummymultiplayer_cons_t[1].value = cv_splitplayers.value - 1;
}

// Tails 11-19-2002
int32_t MR_ConnectIP(int32_t choice)
{
	(void)choice;

	if (*cv_dummyip.string == 0)
	{
		M_StartMessage2("Connect", "You must specify an IP address.\n", MB_NONE, MM_NOTHING, "Press any key", NULL);
		return false;
	}

	M_ClearMenusEx(true, true);

	CV_Set(&cv_lastserver,cv_dummyip.string);
	COM_BufAddText(va("connect \"%s\"\n", cv_dummyip.string));

	// A little "please wait" message.
	M_DrawTextBox(56, BASEVIDHEIGHT/2-12, 24, 2, 0);
	V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT/2, MENUCAPS, "Connecting to server...");
	I_OsPolling();
	I_UpdateNoBlit();
	if (rendermode == render_soft)
		I_FinishUpdate(); // page flip or blit buffer
	return true;
}

//Join Last server
int32_t MR_ConnectLastServer(int32_t choice)
{
	(void)choice;

	if (!*cv_lastserver.string)
	{
		M_StartMessage2("Connect", "You haven't previosuly joined a server.\n", MB_NONE, MM_NOTHING, "Press any key", NULL);
		return false;
	}

	M_ClearMenusEx(true, true);
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
static uint32_t follower_frame;	// used for FF_ANIMATE garbo
static state_t *follower_state;

// this is set before entering the MultiPlayer setup menu,
// for either player 1 or 2
static uint8_t setupplayer;

#define charw 72

static void MD_DrawCssColourBar(menupanelinst_t *pi);
static void MD_DrawCssCharacter(menupanelinst_t *pi);
static void MD_DrawCssStatBars(menupanelinst_t *pi);
static void MD_DrawGridCssSelector(menupanelinst_t *pi);
static void MD_DrawCssStatBacker(menupanelinst_t *pi);
static void MD_DrawBarCssSelector(menupanelinst_t *pi);

void MD_DrawSetupMultiPlayerMenu(menupanelinst_t *pi)
{
	// use generic drawer for cursor, items and title
	// bg, text, arrows handled by generic drawer
	//MD_DrawGenericMenu();

	MD_DrawCssColourBar(pi);
	MD_DrawCssCharacter(pi);

	if (cv_skinselectstyle.value)
	{
		MD_DrawCssStatBars(pi);
		MD_DrawGridCssSelector(pi);
	}
	else
	{
		MD_DrawCssStatBacker(pi);
		MD_DrawBarCssSelector(pi);
	}
}

static void MD_DrawCssStatBacker(menupanelinst_t *pi)
{
	int32_t mx, my;
	patch_t *statdot = W_CachePatchName("K_SDOT0", PU_CACHE);
	uint8_t speed;
	uint8_t weight;
	const uint8_t *flashcol = V_GetStringColormap(highlightflags);
	int16_t i;

	int32_t skintodisplay = cv_chooseskin.value;

	mx = pi->x/FRACUNIT;
	my = pi->y/FRACUNIT;

	// SRB2Kart: draw the stat backer
	for (i = 0; i < numskins; i++) // draw the stat dots
	{
		if (i != skintodisplay && R_SkinAvailable(skins[i]->name) != -1)
		{
			speed = skins[i]->kartspeed;
			weight = skins[i]->kartweight;
			V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, NULL);
		}
	}

	speed = skins[skintodisplay]->kartspeed;
	weight = skins[skintodisplay]->kartweight;

	statdot = W_CachePatchName("K_SDOT1", PU_CACHE);
	if (skullAnimCounter < 4) // SRB2Kart: we draw this dot later so that it's not covered if there's multiple skins with the same stats
		V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, flashcol);
	else
		V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, NULL);

	statdot = W_CachePatchName("K_SDOT2", PU_CACHE); // coloured center
	V_DrawFixedPatch(((BASEVIDWIDTH - mx - 80) + ((speed-1)*8))<<FRACBITS, ((my+76) + ((weight-1)*8))<<FRACBITS, FRACUNIT, 0, statdot, R_GetTranslationColormap(0, cv_dummycolor.value, GTC_MENUCACHE));
}

static void MD_DrawCssStatBars(menupanelinst_t *pi)
{
	int32_t mx, my;

	const int32_t BITSPACING = 4;
	const int32_t BITSHIFTSUB = 1;

	const int32_t BITSTARTXMAIN = 17;
	const int32_t BITSTARTXSUB = 50;

	const int32_t BITSPEEDSTARTY = 2;
	const int32_t BITWEIGHTSTARTY = 18;

	const int32_t ICNXFIRST = 1;
	const int32_t ICNXSECOND = 57;
	const int32_t ICNYFIRST = 1;
	const int32_t ICNYSECOND = 17;

	menuitem_t *item = M_GetMenuItem(pi, "STATBAR");

	patch_t *statbitsub = 	W_CachePatchName("STATBSPD", PU_CACHE);
	patch_t *statbitmain = 	W_CachePatchName("STATBWGT", PU_CACHE);

	patch_t *speedicn = 	W_CachePatchName("K_STSPD", PU_CACHE);
	patch_t *weighticn = 	W_CachePatchName("K_STWHT", PU_CACHE);

	uint8_t *speedcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_SAPPHIRE, GTC_MENUCACHE);
	uint8_t *weightcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_KETCHUP, GTC_MENUCACHE);
	uint8_t *speednumcolour = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_NIGHTFALL, GTC_MENUCACHE);
	uint8_t *weightnumcolour = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_AMBER, GTC_MENUCACHE);
	// uint8_t *accelcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_GOLD, GTC_MENUCACHE);
	// uint8_t *handlingcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_MINT, GTC_MENUCACHE);
	uint8_t *subcolour = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_BLACK, GTC_MENUCACHE);

	int32_t skintodisplay = cv_chooseskin.value;
	uint8_t speed = skins[skintodisplay]->kartspeed;
	uint8_t weight = skins[skintodisplay]->kartweight;

	mx = item->x;
	my = item->y;

	mx = (pi->x + M_GetItemX(pi, "STATBAR"))/FRACUNIT;
	my = (pi->y + M_GetItemY(pi, "STATBAR"))/FRACUNIT;

	// // Draw speed
	for (int16_t s = 0; s < speed; s++)
	{
		V_DrawFixedPatch((mx + BITSTARTXMAIN + (BITSPACING * s))<<FRACBITS, (my + BITSPEEDSTARTY)<<FRACBITS, FRACUNIT, 0, statbitmain, speedcolour);
	}
	// Draw accel
	for (int16_t a = 0; a < 9-speed; a++)
	{
		V_DrawFixedPatch((mx + BITSTARTXSUB - (BITSPACING * a))<<FRACBITS, (my + BITSPEEDSTARTY + BITSHIFTSUB)<<FRACBITS, FRACUNIT, 0, statbitsub, subcolour);
	}

	// Draw weight
	for (int16_t w = 0; w < weight; w++)
	{
		V_DrawFixedPatch((mx + BITSTARTXMAIN + (BITSPACING * w))<<FRACBITS, (my + BITWEIGHTSTARTY)<<FRACBITS, FRACUNIT, 0, statbitmain, weightcolour);
	}
	// Draw handling
	for (int16_t h = 0; h < 9-weight; h++)
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

static void MD_DrawCssColourBar(menupanelinst_t *pi)
{
	int32_t mx, my;
	int16_t i;

	mx = pi->x/FRACUNIT;
	my = pi->y/FRACUNIT;

	// 2.2 color bar backported with permission
#define indexwidth 8
	{
		const int32_t numcolors = (250-charw)/(2*indexwidth); // Number of colors per side
		int32_t x = mx;
		int32_t w = indexwidth; // Width of a singular color block
		menucolor_t *mc, *choosecolor = menucolorhead; // Last accessed color
		uint8_t h;

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

static void MD_DrawCssCharacter(menupanelinst_t *pi)
{
	int32_t mx, my, flags = 0;
	int32_t st;
	patch_t *patch;
	uint8_t frame;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;

	int32_t skintodisplay = cv_chooseskin.value;

	mx = pi->x/FRACUNIT;
	my = pi->y/FRACUNIT;

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
	sprdef = &skins[skintodisplay]->sprites[multi_spr2];

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
	uint8_t *colormap = R_GetTranslationColormap(skintodisplay, cv_dummycolor.value, GTC_MENUCACHE);
	V_DrawFixedPatch((mx+43)<<FRACBITS,
				(my+136)<<FRACBITS,
				skins[skintodisplay]->highresscale,
				flags, patch, colormap);

	// draw their follower if there is one
	if (cv_dummyfollower.value > -1)
	{
		follower_t fl = followers[cv_dummyfollower.value]; // shortcut for our sanity

		tic_t bobspeed = fl.bobspeed;
		if (fl.mode == FOLLOWERMODE_GROUND)
			bobspeed = FixedDiv(bobspeed*FRACUNIT, fl.bobamp / 6); // rough approximation of bounce speed

		// smooth floating, totally not stolen from rocket sneakers.
		fixed_t sine = FixedMul(fl.bobamp, FINESINE(((FixedMul(4 * M_TAU_FIXED, bobspeed) * menutimer)>>ANGLETOFINESHIFT) & FINEMASK));

		follower_tics -= renderdeltatics;

		// restart the ground follower's animation when it lands on the "ground"
		if (fl.mode == FOLLOWERMODE_GROUND)
		{
			// this sucks
			fixed_t sine_next = FixedMul(fl.bobamp, FINESINE(((FixedMul(4 * M_TAU_FIXED, bobspeed) * (menutimer + 1))>>ANGLETOFINESHIFT) & FINEMASK));
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
			uint16_t color = K_GetEffectiveFollowerColor(cv_followercolor[setupplayer].value, &fl, cv_dummycolor.value, skins[skintodisplay]);
			colormap = R_GetTranslationColormap(TC_DEFAULT, color, 0); // why does GTC_MENUCACHE not work here...?

			int32_t x = (mx+65)*FRACUNIT;
			int32_t y = ((my+105)*FRACUNIT);
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

static void MD_DrawBarCssSelector(menupanelinst_t *pi)
{
	int32_t my;

	// mx = pi->x/FRACUNIT;
	my = pi->y/FRACUNIT;

	// character bar, ripped off the color bar :V
#define iconwidth 32
	{
		const int32_t icons = 4;
		int32_t k = -icons;
		int16_t col = (cv_chooseskin.value - icons) % numskins;
		int32_t x = BASEVIDWIDTH/2 - ((icons+1)*24) - 4;
		fixed_t scale = FRACUNIT/2;
		int32_t offx = 8, offy = 8;
		patch_t *cursor;
		static fixed_t cursorframe = 0;
		patch_t *face;
		uint8_t *colmap;

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

static void MD_DrawGridCssSelector(menupanelinst_t *pi)
{
	int32_t skinn;
	patch_t *face;
	uint8_t *colmap;

	patch_t *cursor;
	static fixed_t cursorframe = 0;

	int32_t gridx;
	int32_t gridy;
	int32_t cursorx;
	int32_t cursory;
	int32_t calcs;

	int32_t skintodisplay = cv_chooseskin.value;

	// gridcss_skinydrag = 0;
	// draws background and scroll bar
	{
		int32_t x = ((BASEVIDWIDTH / 2) - (18 * SKINGRIDWIDTH) - 8) + 100 + SKINXSHIFT - 1;
		int32_t y = ((BASEVIDHEIGHT / 2) - (18 * (SKINGRIDWIDTH/2))) + SKINYSHIFT - 1;

		int32_t dx = (SKINGRIDWIDTH * 18);
		int32_t dy = (SKINGRIDHEIGHT * 18);
		int32_t scrx = 4; //2 is added to make the final "thickness"

		int32_t columncount;
		int32_t barlen;
		int32_t barpos;

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

	for (int32_t gridslot = 0; gridslot < SKINGRIDWIDTH*SKINGRIDHEIGHT; gridslot++)
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
			colmap = R_GetTranslationColormap(skinn, skins[skinn]->prefcolor, GTC_MENUCACHE);
			V_DrawFixedPatch((gridx + face->leftoffset) * FRACUNIT, (gridy + face->topoffset) * FRACUNIT, FRACUNIT, 0, face, colmap);
		}
	}

	// draw wanted portrait and cursor
	if (M_IsItemOn(pi, "SKIN") && gridcss_active)
	{
		face = faceprefix[skintodisplay][FACE_WANTED];
		colmap = R_GetTranslationColormap(skintodisplay, cv_dummycolor.value, GTC_MENUCACHE);
		V_DrawFixedPatch((cursorx * FRACUNIT) - (face->width * FRACUNIT/4), (cursory * FRACUNIT) - (face->height * FRACUNIT/4), FRACUNIT, 0, face, colmap);

		cursorframe += renderdeltatics / 4;
		for (; cursorframe > 7 * FRACUNIT; cursorframe -= 7 * FRACUNIT) {}
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
static inline int32_t MapGridSelectToSkin(int32_t row, int32_t column)
{
	return ((row % SKINGRIDWIDTH) + (column * SKINGRIDWIDTH));
}

int32_t MR_HandleSetupMultiPlayerMenu(int32_t choice)
{
	int32_t sortedIndex = 0;

	if (M_IsItemOn(&curpanels[panelOn], "FOLLOWER")
		&& (M_MenuButtonPressed(0, MBT_INFO2) || M_MenuButtonPressed(0, MBT_CONFIRM))
	)
	{
		if (cv_dummyfollower.value > -1)
		{
			follower_t fl = followers[cv_dummyfollower.value];
			S_StopSounds();
			S_StartSound(NULL, fl.hornsound);
		}
		return true;
	}

	// don't consume input if we're not interacting with the grid CSS
	if (cv_skinselectstyle.value != 1) return false;
	if (!M_IsItemOn(&curpanels[panelOn], "SKIN")) return false;

	if (!gridcss_active)
	{
		if (M_MenuButtonPressed(0, MBT_CONFIRM))
		{
			gridcss_active = true;
			S_StartSound(NULL, sfx_menu1);
			return true;
		}
		if (menucmd[0].dpad_lr != 0)
		{
			gridcss_active = true;
			// fall-thru into navigation
		}
		else
		{
			return false;
		}
	}

	if (menucmd[0].dpad_ud > 0)
	{
		if (gridcss_column == (numskins - 1) / SKINGRIDWIDTH)
		{
			M_SetItemOn(&curpanels[panelOn], "FOLLOWER");
			CV_SetValue(&cv_chooseskin, gridcss_skinmemory);
			sortedIndex = FindSortedSkinIndex(cv_chooseskin.value);
			gridcss_row = sortedIndex % SKINGRIDWIDTH;
			gridcss_column = sortedIndex / SKINGRIDWIDTH;

			gridcss_active = false;

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
		M_SetMenuDelay(0);
	}
	else if (menucmd[0].dpad_ud < 0)
	{
		if (gridcss_column == 0)
		{
			M_SetItemOn(&curpanels[panelOn], "NAME");
			CV_SetValue(&cv_chooseskin, gridcss_skinmemory);
			sortedIndex = FindSortedSkinIndex(cv_chooseskin.value);
			gridcss_row = sortedIndex % SKINGRIDWIDTH;
			gridcss_column = sortedIndex / SKINGRIDWIDTH;

			gridcss_active = false;

			gridcss_skinydrag = CLAMP(gridcss_column - SKINGRIDHEIGHT + 1, 0, ((numskins - 1) / SKINGRIDWIDTH) - SKINGRIDHEIGHT + 1);
		}
		else
		{
			gridcss_column--;

			if (gridcss_column < gridcss_skinydrag)
				gridcss_skinydrag--;
		}

		S_StartSound(NULL, sfx_menu1);
		M_SetMenuDelay(0);
	}
	else if (menucmd[0].dpad_lr < 0)
	{
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
		M_SetMenuDelay(0);
	}
	else if (menucmd[0].dpad_lr > 0)
	{
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
		M_SetMenuDelay(0);
	}
	else if (M_MenuButtonPressed(0, MBT_CONFIRM))
	{
		if (cv_chooseskin.value < numskins)
		{
			skin_t *skin = skins[cv_chooseskin.value];
			int32_t voxid = R_GetLocalPreferredVoiceForSkin(setupplayer, skin->name);
			kartvoice_t *voice = NULL;
			gridcss_skinmemory = cv_chooseskin.value;
			gridcss_active = false;

			if (voxid == MAXSKINVOICES)
			{
				// Couldn't find a voice.
				// try current setting
				voxid = R_FindIDForVoice(skin, localvoicedata[setupplayer].name);
				// sorry nothing
				if (voxid == MAXSKINVOICES) voxid = 0;
			}
			voice = &skin->voices[voxid];
			S_StopSounds();
			S_StartSound(NULL, sfx_s221);
			if (voice != NULL)
				S_StartSound(NULL, R_GetLegacySkinSound(voice, sfx_kslow));
			M_SetMenuDelay(0);
		}
	}
	else if (M_MenuButtonPressed(0, MBT_CANCEL))
	{
		CV_SetValue(&cv_chooseskin, gridcss_skinmemory);
		sortedIndex = FindSortedSkinIndex(cv_chooseskin.value);
		gridcss_row = sortedIndex % SKINGRIDWIDTH;
		gridcss_column = sortedIndex / SKINGRIDWIDTH;

		gridcss_skinydrag = CLAMP(gridcss_column - SKINGRIDHEIGHT + 1, 0, ((numskins - 1) / SKINGRIDWIDTH) - SKINGRIDHEIGHT + 1);

		gridcss_active = false;

		S_StartSound(NULL, sfx_menu1);
		M_SetMenuDelay(0);
	}
	else
	{
		switch (choice)
		{
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
					M_SetMenuDelay(0);
				}
				break;
			case KEY_PGUP:
				if (gridcss_column > 0)
				{
					gridcss_column = max(gridcss_column - SKINGRIDHEIGHT + 1, 0);
					gridcss_skinydrag = max(gridcss_column, 0);
					S_StartSound(NULL, sfx_menu1);
					M_SetMenuDelay(0);
				}
				break;
			default:
				return false;
		}
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

	// set follower state
	follower_state = &states[followers[cv_dummyfollower.value].followstate];

	if (follower_state->frame & FF_ANIMATE)
		follower_tics = follower_state->var2*FRACUNIT;	// support for FF_ANIMATE
	else
		follower_tics = follower_state->tics*FRACUNIT;

	follower_frame = follower_state->frame & FF_FRAMEMASK;
}

// start the multiplayer setup menu
int32_t MR_SetupMultiPlayer(int32_t arg)
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
	gridcss_active = false;

	Skinsort_option_Onchange();

	M_GetFollowerState();	// update follower state

	Skinselectstyle_option_Onchange();

	return true;
}

void MP_TickMultiPlayerMenu(menupanelinst_t *pi)
{
	// disable skin changes if we can't actually change skins
	int32_t pnum = setupplayer;
	M_SetItemDisabled(pi, "SKIN", splitscreen >= pnum && !CanChangeSkin(pnum == 0 ? consoleplayer : g_localplayers[pnum]));

	dboolean visible = cv_skinselectstyle.value == 0;
	M_SetItemVisible(pi, "ACCELERATION", visible);
	M_SetItemVisible(pi, "MAXSPEED", visible);
	M_SetItemVisible(pi, "HANDLING", visible);
	M_SetItemVisible(pi, "WEIGHT", visible);
	M_SetItemVisible(pi, "ARROWLR", visible);
	M_SetItemVisible(pi, "ARROWUD", visible);
	M_SetItemVisible(pi, "STATBG", visible);

	M_SetItemVisible(pi, "STATBAR", !visible);
}

int32_t MR_QuitMultiPlayerMenu(int32_t choice)
{
	(void)choice;

	if (cv_skinselectstyle.value == 1)
		CV_SetValue(&cv_chooseskin, gridcss_skinmemory);

	const char *followername = cv_dummyfollower.value == -1 ?
		"None" : followers[cv_dummyfollower.value].skinname;
	COM_BufInsertText(va(
		"%s \"%s\"; %s \"%s\"; %s \"%s\"; %s \"%s\"\n",
		cv_playername[setupplayer].name,  cv_dummyname.string,
		cv_skin[setupplayer].name,        skins[cv_chooseskin.value]->name,
		cv_playercolor[setupplayer].name, skincolors[cv_dummycolor.value].name,
		cv_follower[setupplayer].name,    followername
	));
	return true;
}
#undef SKINXSHIFT
#undef SKINYSHIFT

#undef SKINGRIDWIDTH
#undef SKINGRIDHEIGH

void M_AddMenuColor(uint16_t color) {
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

void M_MoveColorBefore(uint16_t color, uint16_t targ) {
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

void M_MoveColorAfter(uint16_t color, uint16_t targ) {
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

uint16_t M_GetColorBefore(uint16_t color) {
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

uint16_t M_GetColorAfter(uint16_t color) {
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
	uint16_t i;
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
static uint8_t erasecontext = 0;

static void M_EraseDataResponse(int32_t ch)
{
	if (ch != MA_YES)
		return;

	S_StartSound(NULL, sfx_itrole); // bweh heh heh

	// Delete the data
	if (erasecontext == 2)
	{
		uint8_t i;
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

int32_t MR_EraseData(int32_t arg)
{
	const char *eschoice, *esstr = M_GetText("Are you sure you want to erase\n%s?\n");

	erasecontext = (uint8_t)arg;

	if (arg == 0)
		eschoice = M_GetText("Record Attack data");
	else if (arg == 1)
		eschoice = M_GetText("Secrets data");
	else
		eschoice = M_GetText("ALL game data");

	M_StartMessage2("Delete Data?", va(esstr, eschoice), MB_ROUTINE(M_EraseDataResponse), MM_YESNO, M_MessageConfirmText(false, false), M_MessageCancelText(false, false));
	return true;
}

void MP_ScreenshotOptions(menupanelinst_t *pi)
{
	M_SetItemVisible(pi, "FOLDER", cv_screenshot_option.value == 3);
	M_SetItemVisible(pi, "GIFOPTIMIZE", cv_moviemode.value == MM_GIF);
	M_SetItemVisible(pi, "GIFDOWNSCALE", cv_moviemode.value == MM_GIF);
	M_SetItemVisible(pi, "APNGMEMORY", cv_moviemode.value == MM_APNG);
	M_SetItemVisible(pi, "APNGCOMPRESSION", cv_moviemode.value == MM_APNG);
	M_SetItemVisible(pi, "APNGSTRATEGY", cv_moviemode.value == MM_APNG);
	M_SetItemVisible(pi, "APNGWINDOW", cv_moviemode.value == MM_APNG);
}

// =============
// JOYSTICK MENU
// =============

// Start the controls menu, setting it up for either the console player,
// or the secondary splitscreen player

void MD_DrawJoystick(menupanelinst_t *pi)
{
	int32_t i;
	int32_t compareval;
#ifdef JOYSTICK_HOTPLUG
	const int32_t numcontrollers = I_NumControllers();
#endif
	//MD_DrawGenericMenu();

	for (i = 0; i <= MAXGAMEPADS; i++)
	{
#ifdef JOYSTICK_HOTPLUG
		if (atoi(cv_usecontroller[setupcontrolplayer-1].string) > numcontrollers)
			compareval = atoi(cv_usecontroller[setupcontrolplayer-1].string);
		else
#endif
			compareval = cv_usecontroller[setupcontrolplayer-1].value;

		pi->items[i].style = DEH_FindMenuItemStyle(i == compareval ? "JOYSTICKLISTSET" : "JOYSTICKLIST");
	}
}

void MP_SetupJoystick(menupanelinst_t *pi)
{
	const int32_t numcontrollers = I_NumControllers();

	int32_t i = 0;

	M_SetItemText(pi, "JOY1", "None");

	for (i = 1; i <= MAXGAMEPADS && i < pi->numitems; i++)
	{
		if (i <= numcontrollers && (I_GetControllerName(i)) != NULL)
			M_SetItemTextRaw(&pi->items[i], I_GetControllerName(i));
		else
			M_SetItemTextRaw(&pi->items[i], "Unavailable");

#ifdef JOYSTICK_HOTPLUG
		// We use cv_usecontroller.string as the USER-SET var
		// and cv_usecontroller.value as the INTERNAL var
		//
		// In practice, if cv_usecontroller.string == 0, this overrides
		// cv_usecontroller.value and always disables
		//
		// Update cv_usecontroller.string here so that the user can
		// properly change this value.
		for (int32_t j = 0; j < MAXSPLITSCREENPLAYERS; j++)
		{
			if (i == cv_usecontroller[j].value)
				CV_SetValue(&cv_usecontroller[j], i);
		}
#endif
	}
}

void M_UpdateJoystickMenu(void)
{
	for (size_t i = 0; i < numcurpanels; i++)
		if (M_GetDefFromInst(&curpanels[i])->initializer == MP_SetupJoystick)
			MP_SetupJoystick(&curpanels[i]);
}

int32_t MR_AssignJoystick(int32_t arg)
{
	const uint8_t p = setupcontrolplayer-1;

#ifdef JOYSTICK_HOTPLUG
	int32_t oldchoice, oldstringchoice;
	const int32_t numcontrollers = I_NumControllers();

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
				M_StartMessage2("Controls", "This joystick is used by another\n"
							   "player. Reset the joystick\n"
							   "for that player first.\n", MB_NONE, MM_NOTHING, "Press any key", NULL);
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

int32_t MR_SetupControlsMenu(int32_t arg)
{
	if (arg < 0 || arg >= MAXSPLITSCREENPLAYERS)
		return false;

	setupcontrolplayer = arg + 1;
	setupcontrols = gamecontrol[arg];        // was called from main Options (for console player, then)
	return true;
}

void MP_SetupControls(menupanelinst_t *pi)
{
	int32_t pnum = setupcontrolplayer - 1;
	dboolean player1 = pnum == 0;

	// Set proper gamepad options
	M_SetItemArgument(pi, "SETJOY", pnum);
	M_SetItemCvar(pi, "XDEADZ", &cv_deadzonex[pnum]);
	M_SetItemCvar(pi, "YDEADZ", &cv_deadzoney[pnum]);
	M_SetItemCvar(pi, "TDEADZ", &cv_deadzonet[pnum]);
	M_SetItemCvar(pi, "DEAZS", &cv_deadzonestyle[pnum]);
	M_SetItemCvar(pi, "TURNSMOOTHING", &cv_turnsmooth[pnum]);
	M_SetItemCvar(pi, "LITESTEER", &cv_litesteer[pnum]);
	M_SetItemCvar(pi, "LEDCOLOR", &cv_controllerled[pnum]);
	M_SetItemCvar(pi, "RUMBLE", &cv_controllerrumble[pnum]);
	M_SetItemCvar(pi, "RUMBLESTR", &cv_rumblestrength[pnum]);
	M_SetItemCvar(pi, "TILTCONTROL", &cv_tiltcontrol[pnum]);

	M_SetItemVisible(pi, "TALK", player1); // Chat
	//M_SetItemVisible(pi, "TEAM", player1); // Team-chat
	M_SetItemVisible(pi, "SCORES", player1); // Rankings
	//M_SetItemVisible(pi, "VIEWPOINT", player1); // Viewpoint
	// 19 is Reset Camera, 20 is Toggle Chasecam
	M_SetItemVisible(pi, "SCREENSHOT", player1); // Screenshot
	M_SetItemVisible(pi, "RECORDGIF", player1); // GIF
	M_SetItemVisible(pi, "SYSTEMMENU", player1); // System Menu
	M_SetItemVisible(pi, "CONSOLE", player1); // Console
	/*
	M_SetItemVisible(pi, "SPECTATORBUTTONS", player1); // Spectator Controls header
	M_SetItemVisible(pi, "SPECTATORBUTTONS_", player1); // Spectator Controls space
	M_SetItemVisible(pi, "SPECTATE", player1); // Spectate
	M_SetItemVisible(pi, "LOOKUP", player1); // Look Up
	M_SetItemVisible(pi, "LOOKDOWN", player1); // Look Down
	M_SetItemVisible(pi, "CENTERVIEW", player1); // Center View
	*/
}

static int32_t controltochange;
static char controltochangetext[33];

static void M_ChangecontrolResponse(event_t *ev)
{
	int32_t        control;
	int32_t        found;
	int32_t        ch = ev->data1;

	if (ev->device > 0 && G_GetDevicePlayer(ev->device) != setupcontrolplayer-1)
		return;

	if (ev->type == ev_mouse || ev->type == ev_gyroscope || ev->type == ev_accelerometer || ev->type == ev_keyup)
		return;

	if (ev->type == ev_joystick)
	{
		if (!G_AxisInDeadzone(setupcontrolplayer-1, ev))
			ch = G_AxisToKey(ev);
		else
			return;
	}

	// ESCAPE cancels; dummy out PAUSE
	if (ch != KEY_ESCAPE)
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
	// else if (ch == KEY_PAUSE)
	// {
	// 	// This buffer assumes a 125-character message plus a 32-character control name (per controltochangetext buffer size)
	// 	static char tmp[158];

	// 	if (controltochange == gc_pause)
	// 		snprintf(tmp, sizeof(tmp), M_GetText("The \x82Pause Key \x80is enabled, but \nyou may select another key. \n\nHit another key for\n%s\n"),
	// 			controltochangetext);
	// 	else
	// 		snprintf(tmp, sizeof(tmp), M_GetText("The \x82Pause Key \x80is enabled, but \nit is not configurable. \n\nHit another key for\n%s\n"),
	// 			controltochangetext);

	// 	M_StartMessage2("Rebinding Control", tmp, ROUTINECAST(M_ChangecontrolResponse), MM_EVENTHANDLER, NULL, "ESC to Cancel");

	// 	S_StartSound(NULL, sfx_s3k42);
	// 	return;
	// }
	else
		S_StartSound(NULL, sfx_s224);

	M_MessageboxClose(false);
}

int32_t MR_ChangeControl(int32_t arg)
{
	const menupanelinst_t *pi = &curpanels[panelOn];
	// This buffer assumes a 35-character message (per below) plus a max control name limit of 32 chars (per controltochangetext)
	// If you change the below message, then change the size of this buffer!
	static char tmp[68];

	controltochange = arg;
	snprintf(tmp, sizeof(tmp), M_GetText("Hit the new key for\n%s\n"),
		pi->items[pi->itemon].text);
	strlcpy(controltochangetext, pi->items[pi->itemon].text, 33);

	M_StartMessage2("Rebinding Control", tmp, MB_HANDLER(M_ChangecontrolResponse), MM_EVENTHANDLER, NULL, "ESC to Cancel");
	return true;
}

static void M_ResetControlsResponse(int32_t ch)
{
	if (ch != MA_YES)
		return;

	const uint8_t p = setupcontrolplayer-1;
	int32_t i;

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

int32_t MR_ResetControls(int32_t choice)
{
	(void)choice;
	M_StartMessage2("Cnntrols", va(M_GetText("Reset Player %d's controls to defaults?\n"), setupcontrolplayer), MB_ROUTINE(M_ResetControlsResponse), MM_YESNO, M_MessageConfirmText(false, false), M_MessageCancelText(false, false));
	return true;
}

int32_t MR_RestartAudio(int32_t choice)
{
	(void)choice;
	COM_ImmedExecute("restartaudio");
	return true;
}

// ===============
// VIDEO MODE MENU
// ===============

void MP_VideoModeList(menupanelinst_t *pi)
{
	int32_t i, j, nummodes;
	uint32_t width, height;

	VID_PrepareModeList(); // FIXME: hack
	M_ClearPanelItems(pi);
	nummodes = VID_NumModes();

	// DOS does not skip mode 0, because mode 0 is ALWAYS present
	for (i = 0; i < nummodes; i++)
	{
		const char *desc = VID_GetModeName(i);
		if (desc == NULL)
			continue;

		// when a resolution exists both under VGA and VESA, keep the
		// VESA mode, which is always a higher modenum
		for (j = 0; j < pi->numitems; j++)
		{
			if (fastcmp(pi->items[j].text, desc))
			{
				// mode(0): 320x200 is always standard VGA, not vesa
				if (pi->items[j].argument != 0)
				{
					pi->items[j].argument = i;
					if (i == vid.modenum)
						M_RawSetItemOn(pi, j);
				}
				goto duped;
			}
		}

		menuitem_t *item = M_AppendMenuItem(pi, 0, 0, IT_INTERACT);
		item->argument = i;
		item->routine = MR_SetVideoMode;
		M_SetItemTextRaw(item, desc);

		if (i == vid.modenum)
			M_RawSetItemOn(pi, pi->numitems - 1);

		// Pull out the width and height
		sscanf(desc, "%u%*c%u", &width, &height);

		// Show multiples of 320x200 as green.
		if (SCR_IsAspectCorrect(width, height))
			item->style = DEH_FindMenuItemStyle("RECOMMENDEDHIGHLIGHT");

	duped:
		continue;
	}

	pi->navitemcount = (pi->numitems+2) / 3;
	for (i = 0; i < pi->numitems; i++)
	{
		pi->items[i].x = (i / pi->navitemcount) * 91*FRACUNIT;
		pi->items[i].x++; // AAAACK AUTOLAYOUUUUT
		pi->items[i].y = (i % pi->navitemcount) * 8*FRACUNIT;
		pi->items[i].y++; // AAAACK AUTOLAYOUUUUT
	}
}

// Draw the video modes list, a-la-Quake
void MD_DrawVideoMode(menupanelinst_t *pi)
{
	dboolean testing = vidm_testingmode > 0;
	menupanelinst_t *ptesting = M_GetPanel("TESTING");
	menupanelinst_t *pinfo = M_GetPanel("INFO");
	if (testing)
	{
		ptesting->visible = true;
		pinfo->visible = false;
		pi = ptesting;
	}
	else
	{
		ptesting->visible = false;
		pinfo->visible = true;
		pi = pinfo;
	}

	M_SetItemText(pi, "CURRENTMODE",
		va(testing ? "Previewing mode %c%dx%d" : "Current mode is %c%dx%d",
		   0x80 | ((SCR_IsAspectCorrect(vid.width, vid.height) ? recommendedflags : 0) >> V_CHARCOLORSHIFT),
		   vid.width, vid.height)
	);

	if (testing)
	{
		int32_t testtime = (vidm_testingmode/TICRATE) + 1;
		M_SetItemText(pi, "WAITTIME", va("Wait %d second%s", testtime, (testtime > 1) ? "s" : ""));
	}
	else
	{
		M_SetItemText(pi, "DEFAULTMODE",
			va("Default mode is %c%dx%d",
			   0x80 | ((SCR_IsAspectCorrect(cv_scr_width.value, cv_scr_height.value) ? recommendedflags : 0) >> V_CHARCOLORSHIFT),
			   cv_scr_width.value, cv_scr_height.value)
		);
	}
}

int32_t MR_SetVideoMode(int32_t choice)
{
	if (vid.modenum == choice)
	{
		SCR_SetDefaultMode();
	}
	else
	{
		vidm_testingmode = 15*TICRATE;
		vidm_previousmode = vid.modenum;
		if (!setmodeneeded) // in case the previous setmode was not finished
			setmodeneeded = choice + 1;
	}
	return true;
}

// special menuitem key handler for video mode list
int32_t MR_HandleVideoMode(int32_t ch)
{
	(void)ch;

	if (vidm_testingmode <= 0)
		return false;

	if (M_MenuButtonPressed(0, MBT_CANCEL))
	{
		setmodeneeded = vidm_previousmode + 1;
		vidm_testingmode = 0;
	}
	else if (M_MenuButtonPressed(0, MBT_CONFIRM))
	{
		S_StartSound(NULL, sfx_menu1);
		vidm_testingmode = 0; // stop testing
	}

	return true;
}

void MP_CameraSetup(menupanelinst_t *pi)
{
	int32_t pnum = setupcontrolplayer-1;
	M_SetItemCvar(pi, "FLIPCAM", &cv_flipcam[pnum]);
	M_SetItemCvar(pi, "CAMFOV", &cv_fov[pnum]);
	M_SetItemCvar(pi, "CAMDISTANCE", &cv_cam_dist[pnum]);
	M_SetItemCvar(pi, "CAMHEIGHT", &cv_cam_height[pnum]);
	M_SetItemCvar(pi, "CAMSPEED", &cv_cam_speed[pnum]);
	M_SetItemCvar(pi, "CHASECAM", &cv_chasecam[pnum]);
}

// ===============
// Monitor Toggles
// ===============

static tic_t shitsfree = 0;
static tic_t fliptic[MAXKARTITEMS] = {0};

#define FLIPTIC_TIME (TICRATE / 2)

void MP_SetupMonitorToggles(menupanelinst_t *pi)
{
	menuitem_t *item;
	M_ClearPanelItems(pi);

	for (uint8_t i = 0; i <= numkartresults; i++)
	{
		kartresult_t *result = i < pi->navitemcount - 1 ? &kartresults[i] : i == pi->navitemcount - 1 ? NULL : &kartresults[i - 1];
		if (result != NULL && result->isalt)
			continue;

		item = M_AppendMenuItem(pi, 0, 0, IT_INTERACT);

		if (result == NULL)
		{
			item->argument = 1;
			M_SetItemPatchRaw(item, "");
		}
		else
		{
			const char *button = M_ButtonName(0, gc_drift,false);

			item->argument = 0;
			M_SetItemPatchRaw(item, result->cvar->name);
			if (kartitems[result->type].altcvar != NULL)
				M_SetItemTooltipRaw(item, va("Press %s to swap variants.", button));
			else if (result->type == KITEM_SUPERRING && cv_kartrings.value == 0)
				M_SetItemTooltipRaw(item, "Rings must be enabled in Gameplay Mods!");
		}
	}

	while (pi->numitems % pi->navitemcount != 0)
	{
		item = M_AppendMenuItem(pi, 0, 0, IT_INTERACT);
		item->argument = 2;
		M_SetItemPatchRaw(item, "");
	}

	pi->x = 136*FRACUNIT - max(0, min((pi->numitems - 1) / pi->navitemcount, 7))*TICRATE*FRACUNIT/2;
}

void MD_DrawMonitorToggles(menupanelinst_t *pi)
{
	const int32_t edges = 4;
	const int32_t height = pi->navitemcount;
	const int32_t spacing = 35;
	const int32_t column = pi->itemon / height;
	// const int32_t row = itemOn%height;
	int32_t leftdraw, rightdraw, totaldraw;
	int32_t x = pi->x/FRACUNIT, y = pi->y/FRACUNIT + (spacing / 4);
	int32_t onx = 0, ony = 0;
	int32_t i, translucent;
	const char* displayname;

	M_DrawMenuTitle();

	// Find the available space around column
	leftdraw = rightdraw = column;
	totaldraw = 0;
	for (i = 0; (totaldraw < edges * 2 && i < edges * 4); i++)
	{
		if (rightdraw + 1 < (pi->numitems / height) + 1)
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
		int32_t j;

		for (j = 0; j < height; j++, y += spacing)
		{
			const int32_t thisitem = (i * height) + j;

			if (thisitem >= pi->numitems)
				continue;

			if (thisitem == pi->itemon)
			{
				onx = x;
				ony = y;
				continue;
			}

			switch (pi->items[thisitem].argument)
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

			const kartresult_t* result = K_GetKartResult(pi->items[thisitem].patch);
			const kartitem_t* item = &kartitems[result->type];

			dboolean enabled = result->cvar->value == 1;

			if (result->type == KITEM_SUPERRING && cv_kartrings.value == 0)
				enabled = false;

			translucent = enabled ? 0 : V_TRANSLUCENT;

			if (enabled)
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISBG", PU_CACHE));
			else
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISBGD", PU_CACHE));

			dboolean shownum = result->amount >= K_GetItemNumberDisplayMin(result->type, true);
			int8_t showalt = item->altcvar != NULL && !K_ShowAltItemIcon(result->type, false) ? item->altcvar->value : -1;
			if (shownum)
				V_DrawScaledPatch(x, y, 0, W_CachePatchName("K_ISMUL", PU_CACHE));
			V_DrawScaledPatch(x, y, translucent, K_GetCachedItemPatchEx(result->type, true, result->amount, showalt));
			if (shownum)
				V_DrawString(x + 24, y + 31, V_ALLOWLOWERCASE | translucent, va("x%d", result->amount));

			if (kartitems[result->type].altcvar != NULL && kartitems[result->type].altcvar->value && K_ShowAltItemIcon(result->type, true))
				V_DrawScaledPatch(x, y, 0, K_getItemAltPatch(true, false));
		}

		x += spacing;
		y = pi->y/FRACUNIT + (spacing / 4);
	}

	switch (pi->items[pi->itemon].argument)
	{
	case 2:
	{
		displayname = "---";

		V_DrawScaledPatch(
		    onx - 1, ony - 2, V_TRANSLUCENT, W_CachePatchName("K_ITBG", PU_CACHE));

		if (shitsfree)
		{
			int32_t trans = V_TRANSLUCENT;
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
		const kartresult_t* result = K_GetKartResult(pi->items[pi->itemon].patch);
		const kartitem_t* item = &kartitems[result->type];
		dboolean enabled = result->cvar->value == 1;

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

		dboolean shownum = result->amount >= K_GetItemNumberDisplayMin(result->type, true);
		int8_t showalt = item->altcvar != NULL && !K_ShowAltItemIcon(result->type, false) ? item->altcvar->value : -1;
		if (fliptic[result->type] > FLIPTIC_TIME/2)
			showalt = !showalt;

		if (shownum)
			V_DrawScaledPatch(onx - 1, ony - 2, 0, W_CachePatchName("K_ITMUL", PU_CACHE));

		V_DrawStretchyFixedPatch(
			((onx - 1) << FRACBITS) + flipoffset,
			(ony - 2) << FRACBITS,
			fancyflip,
			FRACUNIT,
			translucent,
			K_GetCachedItemPatchEx(result->type, false, result->amount, showalt),
			NULL
		);

		if (shownum)
			V_DrawScaledPatch(onx + 27, ony + 39, translucent, W_CachePatchName("K_ITX", PU_CACHE));
		if (shownum)
			V_DrawKartString(onx + 37, ony + 34, translucent, va("%d", result->amount));

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

	V_DrawCenteredStringAtFixed(
	    BASEVIDWIDTH*FRACUNIT/2, pi->y, MENUCAPS | highlightflags, va("* %s *", displayname));
}

int32_t MR_HandleMonitorToggles(int32_t choice)
{
	(void)choice;

	menupanelinst_t *pi = &curpanels[panelOn];
	kartresult_t *result = K_GetKartResult(pi->items[pi->itemon].patch);

	if (M_MenuButtonPressed(0, MBT_CONFIRM))
	{
		if (pi->items[pi->itemon].argument == 2)
		{
			//S_StartSound(NULL, sfx_s26d);
			if (!shitsfree)
			{
				shitsfree = TICRATE;
				S_StartSound(NULL, sfx_itfree);
			}

			M_SetMenuDelay(0);
		}
		else if (pi->items[pi->itemon].argument == 1)
		{
			int32_t v = K_ItemResultEnabled(K_GetKartResult("sneaker")) ? 1 : 0;
			S_StartSound(NULL, sfx_s1b4);
			M_SetMenuDelay(0);
			for (uint8_t i = 0; i < numkartresults; i++)
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

			M_SetMenuDelay(0);
		}
	}
	else if (M_MenuButtonPressed(0, MBT_INFO2))
	{
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

			M_SetMenuDelay(0);
		}
	}
	else
		return false;

	return true;
}

void M_ReturnToConnectMenu(void)
{
	M_ClearMenus(false);
	D_ClearState();
	M_StartControlPanel();
	M_EnterMenu(MN_MP_MAIN, true, 0);
}

static dboolean viewserver_showaddons = false;

void MP_SetupViewServer(menupanelinst_t *pi)
{
	char *servermap = serverlist[joinnode].info.maptitle;
	char *servername = serverlist[joinnode].info.servername;
	const char *speedstring = "Auto";
	uint32_t ping = serverlist[joinnode].info.time;
	uint8_t kartvars = serverlist[joinnode].info.kartvars;

	M_SetItemVisible(pi, "TOGGLEBUTTON1", viewserver_showaddons);
	M_SetItemVisible(pi, "TOGGLEBUTTON2", !viewserver_showaddons);

	M_SetItemVisible(pi, "PLAYERSHEADING", !viewserver_showaddons);
	M_SetItemVisible(pi, "PLAYERSCOUNT", !viewserver_showaddons);
	M_SetItemVisible(pi, "ADDONSHEADING", viewserver_showaddons);
	M_SetItemVisible(pi, "ADDONSCOUNT", viewserver_showaddons);

	const char *item1 = M_ButtonName(0, gc_accelerate, keyboardcontrolled);
	const char *item2 = M_ButtonName(0, gc_brake, keyboardcontrolled);
	const char *item3 = M_ButtonName(0, gc_fire, keyboardcontrolled);

	M_SetItemText(pi, "TOGGLEBUTTON1", va("[%s] = Players", item3));
	M_SetItemText(pi, "TOGGLEBUTTON2", va("[%s] = Addons", item3));

	M_SetItemText(pi, "EXITBUTTON", va("[%s] = Return", item2));

	M_SetItemText(pi, "JOINBUTTON", va("[%s] = Join", item1));

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
	M_SetItemPatch(pi, "THUMBNAIL", mapname ? mapname : "");
	M_SetItemText(pi, "SERVERNAME", servername);
	M_SetItemText(pi, "SERVERPING", va("Ping: %c%ums", ping < 128 ? '\x83' : ping < 256 ? '\x82' : '\x85', ping));
	M_SetItemText(pi, "SERVERMAP", servermap);
	M_SetItemText(pi, "SERVERGAMETYPE", va("%s: %s", serverlist[joinnode].info.gametypename, speedstring));

	if (fileneedednum > 0)
	{
		M_SetItemText(pi, "SERVERADDON", va("\x82%d Addon%s", fileneedednum, fileneedednum > 1 ? "s" : ""));
	}
	else
	{
		M_SetItemText(pi, "SERVERADDON", "Vanilla");
	}

	if (serverlist[joinnode].info.cheatsenabled)
	{
		M_SetItemText(pi, "SERVERCHEATS", "Cheats");
	}

	M_SetItemVisible(pi, "SERVERCHEATS", serverlist[joinnode].info.cheatsenabled);

	if (kartvars & SV_DEDICATED)
	{
		M_SetItemText(pi, "SERVERDEDICATED", "Dedicated Server");
	}
	else
	{
		M_SetItemText(pi, "SERVERDEDICATED", "Listen Server");
	}

	M_SetItemText(pi, "PLAYERSCOUNT", va("%i / %i",
	                                     serverlist[joinnode].info.numberofplayer,
	                                     serverlist[joinnode].info.maxplayer));

	{
		double totalsize = 0.0;
		const char *unit = "b";

		for (int32_t j = 0; j < fileneedednum; j++)
			totalsize += fileneeded[j].totalsize;

		//in megabytes
		if (totalsize >= 1024.0 * 1024.0)
		{
			unit = "mb";
			totalsize /= 1024.0 * 1024.0;
		}
		// KB
		else if (totalsize >= 1024.0)
		{
			unit = "kb";
			totalsize /= 1024.0;
		}

		M_SetItemText(pi, "ADDONSCOUNT", va("~%.1f%s total", totalsize, unit));
	}

}

int32_t MR_QuitViewServer(int32_t choice)
{
	(void)choice;

	ChangeClientMode(CL_ABORTED);
	return true;
}

int32_t MR_HandleViewServer(int32_t choice)
{
	(void)choice;

	if (M_MenuButtonPressed(0, MBT_CONFIRM))
	{
		S_StartSound(NULL, sfx_menu1);
		ChangeClientMode(CL_CHECKFILES);
		ChangeServMusic(SERVMUS_2, true,true);
		M_ClearMenusEx(false, true);
		M_SetMenuDelay(0);
	}
	else if (M_MenuButtonPressed(0, MBT_INFO1))
	{
		S_StartSound(NULL, sfx_menu1);
		viewserver_showaddons = !viewserver_showaddons;
	}
	else
		return false;

	return true;
}

void MP_ViewServerPlayers(menupanelinst_t *pi)
{
	int32_t i;
	int32_t count = 0;
	fixed_t x = 1, y = 1; // FIXME auto

	M_ClearPanelItems(pi);

	pi->visible = !viewserver_showaddons;
	pi->disabled = true;

	for (i = 0; i < MAXPLAYERINFO; i++)
	{
		if (playerinfo[i].num == 255)
			continue;

		menuitem_t *item = M_AppendMenuItem(pi, x, y, 0);
		M_SetItemTextRaw(item, va("%.11s", playerinfo[i].name));
		uint8_t statuscolor = 1;
		switch (playerinfo[i].team)
		{
			case 0: statuscolor = 112; break; // playing
			case 1: statuscolor = 35; break; // ctf red team
			case 2: statuscolor = 152; break; // ctf blue team
			case 255: statuscolor = 16; break; // spectator or non-team
		}

		if (serverlist[joinnode].info.numberofplayer <= 16)
		{
			uint16_t skinum = playerinfo[i].skin;
			dboolean validskin = skinum < numskins;


			if (playerinfo[i].team == 255)
				item->status |= IT_GRAYEDOUT;

			if (!validskin || R_SkinAvailable(skins[skinum]->name) == -1)
			{
				item->style = DEH_FindMenuItemStyle("VIEWSERVER_PLAYER_MISSING");
				M_SetItemPatchRaw(item, va("16x16,%u", statuscolor));
			}
			else
			{
				item->style = DEH_FindMenuItemStyle("VIEWSERVER_PLAYER");
				M_SetItemPatchRaw(item, skins[skinum]->name);
			}

			if (validskin)
				M_SetItemExtraRaw(item, DEH_SkincolorName(skins[skinum]->prefcolor));

			y += 22*FRACUNIT;
			if (++count % 4 == 0)
			{
				x += 74*FRACUNIT;
				y = 1;
			}
		}
		else
		{
			item->style = DEH_FindMenuItemStyle("VIEWSERVER_PLAYER_SMALL");
			M_SetItemPatchRaw(item, va("6x6,%u", statuscolor));

			y += 9*FRACUNIT;
			if (++count % 11 == 0)
			{
				x += 104*FRACUNIT;
				y = 1;
			}
		}
	}
}

void MP_ViewServerAddons(menupanelinst_t *pi)
{
	int32_t i;
	int32_t count = 0;
	fixed_t x = 1, y = 1; // FIXME auto
	dboolean small_mode = fileneedednum <= 11;

	pi->visible = viewserver_showaddons;
	pi->disabled = true;
	pi->altscroll = true;

	M_ClearPanelItems(pi);

#define maxcharlen (20 + 3) // 3 for the 3 dots
#define charsonside 10

	for (i = 0; i < fileneedednum; i++)
	{
		menuitem_t *item = M_AppendMenuItem(pi, x, y, 0);
		item->style = DEH_FindMenuItemStyle(va("VIEWSERVER_ADDON%s%s",
		                                       small_mode ? "_SMALL" : "",
		                                       (small_mode ? i & 1 : !(i & 1) != !(i & 2)) ? "_BACK" : ""));

		fileneeded_t addon_file = fileneeded[i];
		size_t namelen = strlen(addon_file.filename);
		if (namelen >= maxcharlen && !small_mode)
			M_SetItemTextRaw(item, va("\x82[#%d]\x80: %." TOSTR2(charsonside) "s...%s",
			                          i+1,
			                          addon_file.filename,
			                          addon_file.filename + namelen - (charsonside + 1)));
		else
			M_SetItemTextRaw(item, va("\x82[#%d]\x80: %s", i+1, addon_file.filename));

		double file_size = addon_file.totalsize;
		const char *unit = "b";
		//in megabytes
		if (file_size >= 1024.0 * 1024.0)
		{
			unit = "mb";
			file_size /= 1024.0 * 1024.0;
		}
		// KB
		else if (file_size >= 1024.0)
		{
			unit = "kb";
			file_size /= 1024.0;
		}

		// "~" since its approx this size, we mightve lost some
		// accuracy from only having 4 bytes carry the size
		M_SetItemExtraRaw(item, va("(~%.1f%s)", file_size, unit));

		count++;
		if (small_mode)
		{
			y += 9*FRACUNIT;
		}
		else
		{
			if (count & 1)
			{
				x += 146*FRACUNIT;
			}
			else
			{
				x -= 146*FRACUNIT;
				y += 9*FRACUNIT;
			}
		}
	}

#undef maxcharlen
#undef charsonside
}

int32_t MR_TickViewServer(int32_t choice)
{
	(void)choice;

	menupanelinst_t *pi = M_GetPanel("MAIN");;

	// Buttons
	M_SetItemVisible(pi, "TOGGLEBUTTON1", viewserver_showaddons);
	M_SetItemVisible(pi, "TOGGLEBUTTON2", !viewserver_showaddons);

	M_SetItemVisible(pi, "PLAYERSHEADING", !viewserver_showaddons);
	M_SetItemVisible(pi, "PLAYERSCOUNT", !viewserver_showaddons);
	M_SetItemVisible(pi, "ADDONSHEADING", viewserver_showaddons);
	M_SetItemVisible(pi, "ADDONSCOUNT", viewserver_showaddons);

	const char *item1 = M_ButtonName(0, gc_accelerate, keyboardcontrolled);
	const char *item2 = M_ButtonName(0, gc_brake,  keyboardcontrolled);
	const char *item3 = M_ButtonName(0, gc_fire, keyboardcontrolled);

	M_SetItemText(pi, "TOGGLEBUTTON1", va("[%s] = Players", item3));
	M_SetItemText(pi, "TOGGLEBUTTON2", va("[%s] = Addons", item3));

	M_SetItemText(pi, "EXITBUTTON", va("[%s] = Return", item2));

	M_SetItemText(pi, "JOINBUTTON", va("[%s] = Join", item1));

	menupanelinst_t *pplayers = M_GetPanel("PLAYERS");
	menupanelinst_t *paddons = M_GetPanel("ADDONS");
	pplayers->visible = !viewserver_showaddons;
	pplayers->disabled = true;
	paddons->visible = viewserver_showaddons;
	paddons->disabled = true;
	paddons->altscroll = true;

	panelOn = (viewserver_showaddons ? paddons : pplayers) - curpanels;

	return true;
}

// =========
// Quit Game
// =========
static int32_t quitsounds[] =
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

void M_QuitResponse(int32_t ch)
{
	if (ch != MA_YES)
		return;

	tic_t ptime;
	int32_t mrand;

	if (!(netgame || cht_debug))
	{
		mrand = M_RandomKey(sizeof(quitsounds)/sizeof(int32_t));
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

int32_t MR_QuitSRB2(int32_t choice)
{
	// We pick index 0 which is language sensitive, or one at random,
	// between 1 and maximum number.
	(void)choice;
	M_StartMessage2("Leave Game?", quitmsg[M_RandomKey(NUM_QUITMESSAGES)], MB_ROUTINE(M_QuitResponse), MM_YESNO, M_MessageConfirmText(true, false), M_MessageCancelText(true, false));
	return true;
}

#ifdef HAVE_DISCORDRPC
static const tic_t confirmLength = 3*TICRATE/4;
static tic_t confirmDelay = 0;
static dboolean confirmAccept = false;
dboolean removeRequest = false;

int32_t MR_HandleDiscordRequests(int32_t choice)
{
	(void)choice;

	if (menucmd[0].dpad_lr || menucmd[0].dpad_ud)
	{
		// Do nothing.
		;
	}
	else if (M_MenuButtonPressed(0, MBT_CONFIRM))
	{
		if (confirmDelay > 0)
			return true;
		Discord_Respond(discordRequestList->userID, DISCORD_REPLY_YES);
		confirmAccept = true;
		confirmDelay = confirmLength;
		S_StartSound(NULL, sfx_s3k63);
		M_SetMenuDelay(0);
	}
	else if (M_MenuButtonPressed(0, MBT_CANCEL))
	{
		if (confirmDelay > 0)
			return true;
		Discord_Respond(discordRequestList->userID, DISCORD_REPLY_NO);
		confirmAccept = false;
		confirmDelay = confirmLength;
		S_StartSound(NULL, sfx_s3kb2);
		M_SetMenuDelay(0);
	}
	else
		return false;

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

int32_t MR_TickDiscordRequests(int32_t choice)
{
	(void)choice;

	if (confirmDelay > 0)
	{
		confirmDelay--;

		if (confirmDelay == 0)
			removeRequest = true;
	}

	if (removeRequest == true)
	{
		DRPC_RemoveRequest(discordRequestList);

		removeRequest = false;

		if (discordRequestList == NULL)
		{
			// No other requests
			// TODO:
			M_ExitMenu(); // <------ the problem in question
			if (menustack[0] == MN_MPAUSE)
			{
				menupanelinst_t *pi = M_GetPanel("MAIN");
				M_SetItemOn(pi, "CONTINUE");
			};
		}
	}

	return true;
}

void MD_DrawDiscordRequests(menupanelinst_t *pi)
{
	(void)pi;

	discordRequest_t *curRequest = discordRequestList;
	uint8_t *colormap;
	patch_t *hand = NULL;

	int32_t x = 100;
	int32_t y = 133;

	int32_t slide = 0;
	int32_t maxYSlide = 18;

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

	const char *name = M_GetDiscordName(curRequest);
	const int32_t flags = V_ALLOWLOWERCASE|V_6WIDTHSPACE;

	K_DrawSticker(x + (slide * 32), y - 1, V_ThinStringWidth(name, flags), 0, false);
	V_DrawThinString(x + (slide * 32), y, flags|V_YELLOWMAP, name);

	y -= 18;

	while (curRequest->next != NULL)
	{
		int32_t ySlide = min(slide * 4, maxYSlide);

		curRequest = curRequest->next;

		name = M_GetDiscordName(curRequest);

		K_DrawSticker(x, y - 1 + ySlide, V_ThinStringWidth(name, flags), 0, false);
		V_DrawThinString(x, y + ySlide, flags, name);

		y -= 12;
		maxYSlide = 12;
	}
}
#endif
