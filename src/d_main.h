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
/// \file  d_main.h
/// \brief game startup, and main loop code, system specific interface stuff.

#ifndef __D_MAIN__
#define __D_MAIN__

#include "d_event.h"
#include "w_wad.h"   // for MAX_WADFILES

#ifdef __cplusplus
extern "C" {
#endif

// Store file nameres here for future reference else where.
#define SRB2NAME "srb2.srb"
#define GRAPHICSNAME "gfx.kart"
#define TEXTURESNAME "textures.kart"
#define CHARSNAME "chars.kart"
#define MAPSNAME "maps.kart"
#define MAINNAME "main.pk3"
#define BLANMUSICNAME "blanmusic.pk3"
#define BONUSCHARSNAME "bonuschars.kart"
#define PATCHNAME "patch.pk3"
#define MUSICNAME "music.kart"
#define SOUNDSNAME "sounds.kart"

typedef enum
{
	MAINWAD_SRB2,
	MAINWAD_GFX,
	MAINWAD_TEXTURES,
	MAINWAD_CHARS,
	MAINWAD_MAPS,
	MAINWAD_MAIN,
	MAINWAD_BONUSCHARS,
#ifdef USE_PATCH_FILE
	MAINWAD_PATCH,
#endif
	NUMMAINWADS
} mainwad_t;

// make sure not to write back the config until it's been correctly loaded
extern boolean gameconfig_loaded;
extern tic_t rendergametic;

extern boolean loaded_config;

extern char srb2home[256]; //Alam: My Home
extern boolean usehome; //Alam: which path?
extern const char *pandf; //Alam: how to path?
extern char srb2path[256]; //Alam: SRB2's Home

const char *D_GetFancyBranchName(void);

extern uint8_t	g_public_key[MAXSPLITSCREENPLAYERS][PUBKEYLENGTH];
extern uint8_t	g_secret_key[MAXSPLITSCREENPLAYERS][PRIVKEYLENGTH];

// the infinite loop of D_SRB2Loop() called from win_main for windows version
void D_SRB2Loop(void) FUNCNORETURN;

boolean D_RenderLevel(void);

typedef enum
{
	WIPELOOP_RUNWIPE,
	WIPELOOP_TITLECARD,
	WIPELOOP_ENCORE,
	WIPELOOP_TITLEBLACK,
} wipelooptype_t;

void D_WipeLoop(wipelooptype_t type, UINT8 wipetype, boolean drawMenu);

//
// D_SRB2Main()
// Not a globally visible function, just included for source reference,
// calls all startup code, parses command line options.
//
void D_SRB2Main(void);

// Called by IO functions when input is detected.
void D_PostEvent(const event_t *ev);
#if defined (PC_DOS) && !defined (DOXYGEN)
void D_PostEvent_end(void);    // delimiter for locking memory
#endif

void D_ProcessEvents(void);

const char *D_Home(void);

//
// BASE LEVEL
//
void D_StartTitle(void);
void D_ClearState(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__D_MAIN__
