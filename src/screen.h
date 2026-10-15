// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  screen.h
/// \brief Handles multiple resolutions, 8bpp/16bpp(highcolor) modes

#ifndef __SCREEN_H__
#define __SCREEN_H__

#include "command.h"

#ifdef __cplusplus
extern "C" {
#endif

// quickhack for V_Init()... to be cleaned up
#ifdef NOPOSTPROCESSING
#define NUMSCREENS 2
#else
#define NUMSCREENS 5
#endif

// Size of statusbar.
#define ST_HEIGHT 32
#define ST_WIDTH 320

// used now as a maximum video mode size for extra vesa modes.

// we try to re-allocate a minimum of buffers for stability of the memory,
// so all the small-enough tables based on screen size, are allocated once
// and for all at the maximum size.
#define MAXVIDWIDTH 8192 // don't set this too high because actually
#define MAXVIDHEIGHT 8192 // lots of tables are allocated with the MAX size.
#define MINVIDWIDTH 640	// 320x200 is a bit small
#define MINVIDHEIGHT  400 // but changing the base breaks litereally everything so this will do.
#define BASEVIDWIDTH 320 // NEVER CHANGE THIS! This is the original
#define BASEVIDHEIGHT 200 // resolution of the graphics.

// global video state
struct viddef_t
{
	// Each screen is [vid.width*vid.height];
	uint8_t *screens[5];
	// screens[0] = main display window
	// screens[1] = back screen, alternative blitting
	// screens[2] = screenshot buffer, gif movie buffer
	// screens[3] = fade screen start
	// screens[4] = fade screen end, postimage tempoarary buffer

	size_t rowbytes; // bytes per scanline of the VIDEO mode
	int32_t width; // PIXELS per scanline
	int32_t height;
	int32_t modenum; // vidmode num indexes videomodes list
	dboolean recalc; // if true, recalc vid-based stuff

	int32_t dup; // scale 1, 2, 3 value for menus & overlays
	int32_t fdup; // same as dup, but exact value when aspect ratio isn't 320/200
	int32_t udup;        // same as dup but does not account for highreshudscale
	uint8_t smalldup; // factor for a little bit of scaling
	uint8_t meddup; // factor for moderate, but not full, scaling

	int32_t scaledwidth; // width / dup
	int32_t scaledheight; // height / dup
#ifdef HWRENDER
	int32_t glstate;
#endif
};

enum
{
	VID_GL_LIBRARY_NOTLOADED  = 0,
	VID_GL_LIBRARY_LOADED     = 1,
	VID_GL_LIBRARY_ERROR      = -1,
};

// ----------------
// screen variables
// ----------------
extern viddef_t vid;
extern int32_t setmodeneeded; // mode number to set if needed, or 0
extern uint8_t setrenderneeded;

extern double averageFPS;

void SCR_ChangeRenderer(void);

extern CV_PossibleValue_t cv_renderer_t[];

extern consvar_t cv_scr_width, cv_scr_height, cv_scr_depth, cv_renderview, cv_renderer, cv_renderhitbox, cv_fullscreen;
extern consvar_t cv_highreshudscale;
extern consvar_t cv_vhseffect, cv_shittyscreen, cv_votebgscaling;
extern consvar_t cv_parallelsoftware;
extern consvar_t cv_accuratefps;

// wait for page flipping to end or not
extern consvar_t cv_vidwait;
extern consvar_t cv_timescale;

// Initialize the screen
void SCR_Startup(void);

// Change video mode, only at the start of a refresh.
void SCR_SetMode(void);

// Set current column / span drawers
//void R_SetColumnContext(enum columncontext_e _columncontext); // declared in r_draw!
void R_SetColumnFunc(size_t id, dboolean brightmapped);
void R_SetSpanFunc(size_t id, dboolean npo2, dboolean brightmapped);
dboolean R_SetSpanFuncFlat(size_t id); // flat color

// Compare current column drawer
dboolean R_CheckColumnFunc(size_t id);

// Recalc screen size dependent stuff
void SCR_Recalc(void);

// Check parms once at startup
void SCR_CheckDefaultMode(void);

// Set the mode number which is saved in the config
void SCR_SetDefaultMode(void);

void SCR_CalculateFPS(void);

FUNCMATH dboolean SCR_IsAspectCorrect(int32_t width, int32_t height);

// move out to main code for consistency
void SCR_DisplayTicRate(void);
void SCR_ClosedCaptions(void);
void SCR_DisplayLocalPing(void);
void SCR_DisplayMarathonInfo(void);
#undef DNWH

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__SCREEN_H__
