// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  deh_soc.h
/// \brief Load SOC file and change tables and text

#ifndef __DEH_SOC_H__
#define __DEH_SOC_H__

#include "doomdef.h"
#include "g_game.h"
#include "sounds.h"
#include "info.h"
#include "d_think.h"
#include "m_argv.h"
#include "z_zone.h"
#include "w_wad.h"
#include "m_menu.h"
#include "m_misc.h"
#include "f_finale.h"
#include "st_stuff.h"
#include "i_system.h"
#include "p_setup.h"
#include "r_data.h"
#include "r_textures.h"
#include "r_draw.h"
#include "r_picformats.h"
#include "r_things.h" // R_Char2Frame
#include "r_sky.h"
#include "lua_script.h" // Reluctantly included for LUA_EvalMath
#include "d_clisrv.h"
#include "k_items.h"

#include "info.h"
#include "dehacked.h"
#include "doomdef.h" // HWRENDER

#ifdef __cplusplus
extern "C" {
#endif

// Crazy word-reading stuff
/// \todo Put these in a seperate file or something.
mobjtype_t get_mobjtype(const char *word);
statenum_t get_state(const char *word);
spritenum_t get_sprite(const char *word);
playersprite_t get_sprite2(const char *word);
sfxenum_t get_sfx(const char *word);
menutype_t get_menutype(const char *word);
menufunc_f *get_menuroutine(const char *word);
menudrawer_f *get_menudrawer(const char *word);
panelfunc_f *get_panelfunc(const char *word);
//int16_t get_gametype(const char *word);
//powertype_t get_power(const char *word);
skincolornum_t get_skincolor(const char *word);
kartitemtype_e get_kartitem(const char *word);
useoddsfunc_f *get_useoddsfunc(const char *word);

void readwipes(MYFILE *f);
void readmaincfg(MYFILE *f);
void readconditionset(MYFILE *f, uint8_t setnum);
void readunlockable(MYFILE *f, int32_t num);
void readextraemblemdata(MYFILE *f, int32_t num);
void reademblemdata(MYFILE *f, int32_t num);
void readsound(MYFILE *f, int32_t num);
void readframe(MYFILE *f, int32_t num);
void readhuditem(MYFILE *f, int32_t num);
void readmenu(MYFILE *f, int32_t num);
void readmenupanel(MYFILE *f, menupaneldef_t *pd);
void readmenuitemstyle(MYFILE *f, menuitemstyle_t *style);
void readtextprompt(MYFILE *f, int32_t num);
void readcutscene(MYFILE *f, int32_t num);
void readlevelheader(MYFILE *f, char * name);
void readgametype(MYFILE *f, char *gtname);
void readsprite2(MYFILE *f, int32_t num);
void readskincolor(MYFILE *f, int32_t num, dboolean mainfile);
void readthing(MYFILE *f, int32_t num);
void readfreeslots(MYFILE *f);
void clear_emblems(void);
void clear_unlockables(void);
void clear_levels(void);
void clear_conditionsets(void);

void readcupheader(MYFILE *f, cupheader_t *cup);
void readfollower(MYFILE *f);
preciptype_t get_precip(const char *word);
void readweather(MYFILE *f, int32_t num);
void readkartitem(MYFILE *f, kartitem_t *item);
void readkartresult(MYFILE *f, kartresult_t *result);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
