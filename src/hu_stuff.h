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
/// \file  hu_stuff.h
/// \brief Heads up display

#ifndef __HU_STUFF_H__
#define __HU_STUFF_H__

#include "d_event.h"
#include "doomdef.h"
#include "w_wad.h"
#include "r_defs.h"
#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------
//           heads up font
//------------------------------------
#define HU_FONTSTART '\x16' // the first font character
#define HU_FONTEND '~'

#define HU_FONTSIZE (HU_FONTEND - HU_FONTSTART + 1)

// SRB2kart
#define KART_FONTSTART '\"' // the first font character
#define KART_FONTEND 'Z'

#define KART_FONTSIZE (KART_FONTEND - KART_FONTSTART + 1)
//

// Level title font
#define LT_FONTSTART '!' // the first font characters
#define LT_FONTEND 'z' // the last font characters
#define LT_FONTSIZE (LT_FONTEND - LT_FONTSTART + 1)

#define CRED_FONTSTART '!' // the first font character
#define CRED_FONTEND 'Z' // the last font character
#define CRED_FONTSIZE (CRED_FONTEND - CRED_FONTSTART + 1)

#define X( name ) name ## _FONT
/* fonts */
enum
{
	X        (HU),
	X      (TINY),
	X      (KART),

	X        (LT),
	X      (CRED),

	X      (GTOL),
	X      (GTFN),

	X   (TALLNUM),
	X (NIGHTSNUM),
	X   (PINGNUM),
	X   (OPPRNK),
};
#undef  X

//------------------------------------
//        sorted player lines
//------------------------------------

struct playersort_t
{
	char string[48];
	int32_t num;
	const char *name;
	char clan[MAXCLANTAG];
};

//------------------------------------
//           chat stuff
//------------------------------------
#define CHAT_BUFSIZE 64		// that's enough messages, right? We'll delete the older ones when that gets out of hand.
#define NETSPLITSCREEN // why the hell WOULDN'T we want this?
#ifdef NETSPLITSCREEN
#define OLDCHAT (cv_consolechat.value == 1 || dedicated || vid.width < 640)
#else
#define OLDCHAT (cv_consolechat.value == 1 || dedicated || vid.width < 640)
#endif
#define CHAT_MUTE (cv_mute.value && !(server || IsPlayerAdmin(consoleplayer))) // this still allows to open the chat but not to type. That's used for scrolling and whatnot.
#define OLD_MUTE (OLDCHAT && cv_mute.value && !(server || IsPlayerAdmin(consoleplayer))) // this is used to prevent oldchat from opening when muted.

typedef enum
{
	HU_SHOUT		= 1,		// Shout message
	HU_CSAY			= 1<<1,		// Middle-of-screen server message
	HU_PRIVNOTICE	= 1<<2,		// Special server sayto, we don't want to see it as the sender.
} sayflags_t;

// some functions
void HU_AddChatText(const char *text, dboolean playsound);

// set true when entering a chat message
extern dboolean chat_on;

// keystrokes in the console or chat window
extern dboolean hu_keystrokes;

extern patch_t *pinggfx[5];
extern patch_t *framecounter;
extern patch_t *frameslash;
extern patch_t *srb2back;

// set true whenever the tab rankings are being shown for any reason
extern dboolean hu_showscores;

extern consvar_t cv_chat_xoffset;
extern consvar_t cv_chat_yoffset;
extern consvar_t cv_chat_showlimit;

// init heads up data at game startup.
void HU_Init(void);

void HU_LoadGraphics(void);

// Load a HUDGFX patch or NULL/missingpat (dependent on required dboolean).
patch_t *HU_UpdateOrBlankPatch(patch_t **user, dboolean required, const char *format, ...);
//#define HU_CachePatch(...) HU_UpdateOrBlankPatch(NULL, false, __VA_ARGS__) -- not sure how to default the missingpat here plus not currently used
#define HU_UpdatePatch(user, ...) HU_UpdateOrBlankPatch(user, true, __VA_ARGS__)

// reset heads up when consoleplayer respawns.
void HU_Start(void);

dboolean HU_Responder(event_t *ev);
void HU_Ticker(void);
void HU_TickSongCredits(void);
void HU_DrawSongCredits(void);
void HU_Drawer(void);
char HU_dequeueChatChar(void);
void HU_clearChatChars(void);
void HU_drawPing(int32_t x, int32_t y, uint32_t ping, uint32_t mindelay, uint32_t pl, int32_t flags, dboolean icon);
void HU_drawOldPing(int32_t x, int32_t y, uint32_t ping, uint32_t mindelay, uint32_t pl, int32_t flags);
void HU_drawMiniPing (int32_t x, int32_t y, uint32_t ping, uint32_t lag, int32_t flags);

int32_t HU_CreateTeamScoresTbl(playersort_t *tab, uint32_t dmtotals[]);

// CECHO interface.
void HU_ClearCEcho(void);
void HU_SetCEchoDuration(int32_t seconds);
void HU_SetCEchoFlags(int32_t flags);
void HU_DoCEcho(const char *msg);

void DoSayCommand(char *message, int8_t target, uint8_t flags, uint8_t source);

// Demo playback info
extern uint32_t hu_demotime;
extern uint32_t hu_demolap;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
