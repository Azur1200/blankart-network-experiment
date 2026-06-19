// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  deh_tables.h
/// \brief Define DeHackEd tables.

#ifndef __DEH_TABLES_H__
#define __DEH_TABLES_H__

#include "doomdef.h" // Constants
#include "d_think.h" // actionf_p1
#include "info.h" // Mobj, state, sprite, etc constants
#include "lua_script.h"
#include "strbuf.h"
#include "m_menu.h" // menutype_t
#include "k_items.h"

#ifdef __cplusplus
extern "C" {
#endif

// Free slot names
// The crazy word-reading stuff uses these.
extern strbuf_t *statenames;
extern strbuf_t *mobjnames;
extern strbuf_t *skincolornames;
extern strbuf_t *menunames;
extern strbuf_t *kartitemnames;

extern UINT8 used_spr[(NUMSPRITEFREESLOTS / 8) + 1]; // Bitwise flag for sprite freeslot in use! I would use ceil() here if I could, but it only saves 1 byte of memory anyway.

const char *DEH_MobjtypeName(mobjtype_t i);
const char *DEH_StateName(statenum_t i);
const char *DEH_SkincolorName(skincolornum_t i);
const char *DEH_MenutypeName(menutype_t i);
const char *DEH_MenuPanelName(const menupaneldef_t *pd);
const char *DEH_MenuItemStyleName(const menuitemstyle_t *style);
const char *DEH_KartItemName(kartitemtype_e i);

mobjtype_t DEH_FindMobjtype(const char *word);
statenum_t DEH_FindState(const char *word);
skincolornum_t DEH_FindSkincolor(const char *word);
menutype_t DEH_FindMenutype(const char *word);
menupaneldef_t *DEH_FindMenuPanel(const char *word);
menuitemstyle_t *DEH_FindMenuItemStyle(const char *word);
kartitemtype_e DEH_FindKartItem(const char *word);

struct flickytypes_s {
	const char *name;
	const mobjtype_t type;
};

#define MAXFLICKIES 64

/** Action pointer for reading actions from Dehacked lumps.
  */
struct actionpointer_t
{
	actionf_p1 action; ///< Function pointer corresponding to the actual action.
	const char *name; ///< Name of the action in ALL CAPS.
};

struct menu_routine_s {
	const char *name;
	menufunc_f *routine;
};

struct menu_drawer_s {
	const char *name;
	menudrawer_f *drawer;
};

struct panel_func_s {
	const char *name;
	panelfunc_f *func;
};

struct odds_func_s {
	const char *name;
	useoddsfunc_f *func;
};

struct int_const_s {
	const char *n;
	// has to be able to hold both fixed_t and angle_t, so drastic measure!!
	lua_Integer v;
};

extern struct flickytypes_s FLICKYTYPES[];
extern actionpointer_t actionpointers[]; // Array mapping action names to action functions.
extern struct int_const_s const STATE_ALIASES[];
extern struct int_const_s const MOBJ_ALIASES[];
extern struct int_const_s const GAMETYPE_ALIASES[];
extern const char *const MOBJFLAG_LIST[];
extern const char *const MOBJFLAG2_LIST[]; // \tMF2_(\S+).*// (.+) --> \t"\1", // \2
extern const char *const MOBJEFLAG_LIST[];
extern const char *const MAPTHINGFLAG_LIST[4];
extern const char *const PLAYERFLAG_LIST[];
extern struct int_const_s const PLAYERFLAG_ALIASES[];
extern const char *const AIRDROPFLAG_LIST[];
extern const char *const ITEMFLAG_LIST[];
extern const char *const GAMETYPERULE_LIST[];
extern const char *const ML_LIST[]; // Linedef flags
extern const char *const ML_LIST_KART[]; // Linedef flags for Kart
extern const char *const MSF_LIST[]; // Sector flags
extern const char *const SSF_LIST[]; // Sector special flags
extern const char *const SD_LIST[]; // Sector damagetype
extern const char *const TO_LIST[]; // Sector triggerer
extern const char *const POWERS_LIST[];
extern const char *const KARTSTUFF_LIST[];
extern const char *const KARTHUD_LIST[];
extern const char *const HUDITEMS_LIST[];
extern struct menu_routine_s const MENU_ROUTINES[];
extern struct menu_drawer_s const MENU_DRAWERS[];
extern struct panel_func_s const PANEL_FUNCS[];
extern struct odds_func_s const USEODDS_FUNCS[];

extern struct int_const_s const INT_CONST[];

#ifdef __cplusplus
} // extern "C"
#endif

#endif
