// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  console.c
/// \brief Console drawing and input

#ifdef __GNUC__
#include <unistd.h>
#endif

#include "doomdef.h"
#include "console.h"
#include "g_game.h"
#include "g_input.h"
#include "hu_stuff.h"
#include "keys.h"
#include "r_main.h"
#include "r_defs.h"
#include "sounds.h"
#include "st_stuff.h"
#include "s_sound.h"
#include "v_video.h"
#include "i_video.h"
#include "z_zone.h"
#include "i_system.h"
#include "i_threads.h"
#include "d_main.h"
#include "m_menu.h"
#include "m_textinput.h"
#include "m_emotes.h"
#include "i_time.h"
#include "filesrch.h"
#include "m_misc.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

#define MAXHUDLINES 20

#ifdef HAVE_THREADS
I_mutex con_mutex;

// I_In_Exiting_Signal_Handler is an evil hack
// to avoid infinite SIGABRT recursion in the signal handler
// due to poisoned locks or mach-o kernel not supporting locks in signals
// or something like that. idk
#  define Lock_state()    if (!I_In_Exiting_Signal_Handler()) { I_lock_mutex(&con_mutex); }
#  define Unlock_state()  if (!I_In_Exiting_Signal_Handler()) { I_unlock_mutex(con_mutex); }
#else/*HAVE_THREADS*/
#  define Lock_state()
#  define Unlock_state()
#endif/*HAVE_THREADS*/

static dboolean con_started = false; // console has been initialised
       dboolean con_startup = false; // true at game startup

       con_loadprogress_t con_startup_loadprogress = 0; // Progress for startup load bar

static dboolean con_forcepic = true; // at startup toggle console translucency when first off
       dboolean con_recalc;          // set true when screen size has changed

static tic_t con_tick; // console ticker for blinking prompt cursor
                        // con_scrollup should use time (currenttime - lasttime)..

static dboolean consoletoggle; // true when console key pushed, ticker will handle
static dboolean consoleready;  // console prompt is ready

       int32_t con_destlines; // vid lines used by console at final position
static int32_t con_curlines;  // vid lines currently used by console

static int32_t con_hudlines;        // number of console heads up message lines
static int32_t con_hudtime[MAXHUDLINES];      // remaining time of display for hud msg lines

// console text output
static char *con_line;          // console text output current line
static size_t con_cx;           // cursor position in current line
static size_t con_cy;           // cursor line number in con_buffer, is always
                                // increasing, and wrapped around in the text
                                // buffer using modulo.

static size_t con_totallines;      // lines of console text into the console buffer
static size_t con_width;           // columns of chars, depend on vid mode width

static size_t con_scrollup;        // how many rows of text to scroll up (pgup/pgdn)
uint32_t con_scalefactor;            // text size scale factor

// hold 32 last lines of input for history
#define CON_MAXPROMPTCHARS 256
#define CON_PROMPTCHAR '$'

static char inputlines[32][CON_MAXPROMPTCHARS]; // hold last 32 prompt lines

static int32_t inputline;    // current input line number
static int32_t inputhist;    // line number of history input line to restore
static textinput_t input;
// notice: input does NOT include the "$" at the start of the line. - 11/3/16

// protos.
static void CON_InputInit(void);
static void CON_RecalcSize(void);
static void CON_ChangeHeight(void);

static void CON_DrawBackpic(void);
static void CONS_height_Change(void);
static void CONS_hudlines_Change(void);
static void CONS_backcolor_Change(void);

//======================================================================
//                   CONSOLE VARS AND COMMANDS
//======================================================================
#ifdef macintosh
#define CON_BUFFERSIZE 4096 // my compiler can't handle local vars >32k
#else
#define CON_BUFFERSIZE 16384
#endif

static char con_buffer[CON_BUFFERSIZE];

// how many seconds the hud messages lasts on the screen
static consvar_t cons_msgtimeout = CVAR_INIT ("con_hudtime", "5", CV_SAVE, CV_Unsigned, NULL);

// number of lines displayed on the HUD
static consvar_t cons_hudlines = CVAR_INIT ("con_hudlines", "5", CV_CALL|CV_SAVE, CV_Unsigned, CONS_hudlines_Change);

// number of lines console move per frame
// (con_speed needs a limit, apparently)
static CV_PossibleValue_t speed_cons_t[] = {{0, "MIN"}, {64, "MAX"}, {0, NULL}};
static consvar_t cons_speed = CVAR_INIT ("con_speed", "8", CV_SAVE, speed_cons_t, NULL);

// percentage of screen height to use for console
static consvar_t cons_height = CVAR_INIT ("con_height", "50", CV_CALL|CV_SAVE, CV_Unsigned, CONS_height_Change);

static CV_PossibleValue_t backpic_cons_t[] = {{0, "translucent"}, {1, "picture"}, {0, NULL}};
// whether to use console background picture, or translucent mode
static consvar_t cons_backpic = CVAR_INIT ("con_backpic", "translucent", CV_SAVE, backpic_cons_t, NULL);

static CV_PossibleValue_t backcolor_cons_t[] = {{0, "White"}, 		{1, "Black"},		{2, "Sepia"},
												{3, "Brown"},		{4, "Pink"},		{5, "Raspberry"},
												{6, "Red"},			{7, "Creamsicle"},	{8, "Orange"},
												{9, "Gold"},		{10,"Yellow"},		{11,"Emerald"},
												{12,"Green"},		{13,"Cyan"},		{14,"Steel"},
												{15,"Periwinkle"},	{16,"Blue"},		{17,"Purple"},
												{18,"Lavender"},
												{0, NULL}};
consvar_t cons_backcolor = CVAR_INIT ("con_backcolor", "Black", CV_CALL|CV_NOINIT|CV_SAVE, backcolor_cons_t, CONS_backcolor_Change);

static CV_PossibleValue_t menuhighlight_cons_t[] =
{
	{0, "Game type"},
	{V_YELLOWMAP, "Always yellow"},
	{V_PURPLEMAP, "Always purple"},
	{V_GREENMAP, "Always green"},
	{V_BLUEMAP, "Always blue"},
	{V_REDMAP, "Always red"},
	{V_GRAYMAP, "Always gray"},
	{V_ORANGEMAP, "Always orange"},
	{V_SKYMAP, "Always sky-blue"},
	{V_GOLDMAP, "Always gold"},
	{V_LAVENDERMAP, "Always lavender"},
	{V_AQUAMAP, "Always aqua-green"},
	{V_MAGENTAMAP, "Always magenta"},
	{V_PINKMAP, "Always pink"},
	{V_BROWNMAP, "Always brown"},
	{V_TANMAP, "Always tan"},
	{0, NULL}
};
consvar_t cons_menuhighlight = CVAR_INIT ("menuhighlight", "Game type", CV_SAVE, menuhighlight_cons_t, NULL);

static void CON_Print(char *msg);

// Change the console height on demand
//
static void CONS_height_Change(void)
{
	Lock_state();

	if (con_destlines > 0 && !con_startup) // If the console is open (as in, not using "bind")...
		CON_ChangeHeight(); // ...update its height now, not only when it's closed and re-opened

	Unlock_state();
}

//
//
static void CONS_hudlines_Change(void)
{
	int32_t i;

	Lock_state();

	// Clear the currently displayed lines
	for (i = 0; i < con_hudlines; i++)
		con_hudtime[i] = 0;

	if (cons_hudlines.value < 1)
		cons_hudlines.value = 1;
	else if (cons_hudlines.value > MAXHUDLINES)
		cons_hudlines.value = MAXHUDLINES;

	con_hudlines = cons_hudlines.value;

	Unlock_state();

	CONS_Printf(M_GetText("Number of console HUD lines is now %d\n"), con_hudlines);
}

// Clear console text buffer
//
static void CONS_Clear_f(void)
{
	Lock_state();

	memset(con_buffer, 0, CON_BUFFERSIZE);

	con_cx = 0;
	con_cy = con_totallines-1;
	con_line = &con_buffer[con_cy*con_width];
	con_scrollup = 0;

	Unlock_state();
}

static char *bindtable[NUMINPUTS];

static void CONS_Bind_f(void)
{
	size_t na;
	int32_t key;

	na = COM_Argc();

	if (na != 2 && na != 3)
	{
		CONS_Printf(M_GetText("bind <keyname> [<command>]: create shortcut keys to command(s)\n"));
		CONS_Printf("\x82%s", M_GetText("Bind table :\n"));
		na = 0;
		for (key = 0; key < NUMINPUTS; key++)
			if (!menustack[0] && bindtable[key])
			{
				CONS_Printf("%s : \"%s\"\n", G_KeynumToString(key), bindtable[key]);
				na = 1;
			}
		if (!na)
			CONS_Printf(M_GetText("(empty)\n"));
		return;
	}

	key = G_KeyStringtoNum(COM_Argv(1));
	if (key <= 0 || key >= NUMINPUTS)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Invalid key name\n"));
		return;
	}

	Z_Free(bindtable[key]);
	bindtable[key] = NULL;

	if (na == 3)
		bindtable[key] = Z_StrDup(COM_Argv(2));
}

//======================================================================
//                          CONSOLE SETUP
//======================================================================

// Font colormap colors
// TODO: This could probably be improved somehow...
// These colormaps are 99% identical, with just a few changed bytes
// This could EASILY be handled by modifying a centralised colormap
// for software depending on the prior state - but yknow, OpenGL...
uint8_t *yellowmap, *purplemap, *greenmap, *bluemap, *graymap, *redmap, *orangemap,\
 *skymap, *goldmap, *lavendermap, *aquamap, *magentamap, *pinkmap, *brownmap, *tanmap;

// Console BG color
uint8_t *consolebgmap = NULL;
uint8_t *promptbgmap = NULL;
static uint8_t promptbgcolor = UINT8_MAX;

void CON_SetupBackColormapEx(int32_t color, dboolean prompt)
{
	uint16_t i, palsum;
	uint8_t j, palindex;
	uint8_t *pal = W_CacheLumpName(GetPalette(), PU_CACHE);
	int32_t shift = 6;

	if (color == INT32_MAX)
		color = cons_backcolor.value;

	shift = 6; // 12 colors -- shift of 7 means 6 colors

	switch (color)
	{
		case 0:		palindex = 15; 	break; 	// White
		case 1:		palindex = 31;	break; 	// Black
		case 2:		palindex = 251;	break;	// Sepia
		case 3:		palindex = 239;	break; 	// Brown
		case 4:		palindex = 215; shift = 7; 	break; 	// Pink
		case 5:		palindex = 37; shift = 7;	break; 	// Raspberry
		case 6:		palindex = 47; shift = 7;	break; 	// Red
		case 7:		palindex = 53;	shift = 7;	break;	// Creamsicle
		case 8:		palindex = 63;	break; 	// Orange
		case 9:		palindex = 56; shift = 7;	break; 	// Gold
		case 10:	palindex = 79; shift = 7;	break; 	// Yellow
		case 11:	palindex = 119; shift = 7; 	break; 	// Emerald
		case 12:	palindex = 111;	break; 	// Green
		case 13:	palindex = 136;	shift = 7; break; 	// Cyan
		case 14:	palindex = 175; shift = 7;	break; 	// Steel
		case 15:	palindex = 166;	shift = 7; 	break; 	// Periwinkle
		case 16:	palindex = 159;	break; 	// Blue
		case 17:	palindex = 187; shift = 7; 	break; 	// Purple
		case 18:	palindex = 199; shift = 7; 	break; 	// Lavender
		// Default green
		default:	palindex = 111; break;
	}

	if (prompt)
	{
		if (!promptbgmap)
			promptbgmap = (uint8_t *)Z_Malloc(256, PU_STATIC, NULL);

		if (color == promptbgcolor)
			return;
		else
			promptbgcolor = color;
	}
	else if (!consolebgmap)
		consolebgmap = (uint8_t *)Z_Malloc(256, PU_STATIC, NULL);

	// setup background colormap
	for (i = 0, j = 0; i < 768; i += 3, j++)
	{
		palsum = (pal[i] + pal[i+1] + pal[i+2]) >> shift;
		if (prompt)
			promptbgmap[j] = (uint8_t)(palindex - palsum);
		else
			consolebgmap[j] = (uint8_t)(palindex - palsum);
	}
}

void CON_SetupBackColormap(void)
{
	CON_SetupBackColormapEx(cons_backcolor.value, false);
	CON_SetupBackColormapEx(1, true); // default to gray
}

static void CONS_backcolor_Change(void)
{
	CON_SetupBackColormapEx(cons_backcolor.value, false);
}

static void CON_SetupColormaps(void)
{
	int32_t i;
	uint8_t *memorysrc = (uint8_t *)Z_Malloc((256*15), PU_STATIC, NULL);

	purplemap   = memorysrc;
	yellowmap   = (purplemap+256);
	greenmap    = (yellowmap+256);
	bluemap     = (greenmap+256);
	redmap      = (bluemap+256);
	graymap     = (redmap+256);
	orangemap   = (graymap+256);
	skymap      = (orangemap+256);
	lavendermap = (skymap+256);
	goldmap     = (lavendermap+256);
	aquamap     = (goldmap+256);
	magentamap  = (aquamap+256);
	pinkmap     = (magentamap+256);
	brownmap    = (pinkmap+256);
	tanmap      = (brownmap+256);

	// setup the other colormaps, for console text

	// these don't need to be aligned, unless you convert the
	// V_DrawMappedPatch() into optimised asm.

	for (i = 0; i < (256*15); i++, ++memorysrc)
		*memorysrc = (uint8_t)(i & 0xFF); // remap each color to itself...

	purplemap[0]   = (uint8_t)163;
	yellowmap[0]   = (uint8_t)73;
	greenmap[0]    = (uint8_t)98;
	bluemap[0]     = (uint8_t)148;
	redmap[0]      = (uint8_t)34; // battle
	graymap[0]     = (uint8_t)10;
	orangemap[0]   = (uint8_t)52; // record attack
	skymap[0]      = (uint8_t)132; // race
	lavendermap[0] = (uint8_t)192;
	goldmap[0]     = (uint8_t)65;
	aquamap[0]     = (uint8_t)121;
	magentamap[0]  = (uint8_t)182;
	pinkmap[0]     = (uint8_t)210;
	brownmap[0]    = (uint8_t)224;
	tanmap[0]      = (uint8_t)217; // no longer nice :(

	// Init back colormap
	CON_SetupBackColormap();
}

// Setup the console text buffer
//
void CON_Init(void)
{
	int32_t i;

	for (i = 0; i < NUMINPUTS; i++)
		bindtable[i] = NULL;

	Lock_state();

	// clear all lines
	memset(con_buffer, 0, CON_BUFFERSIZE);

	// make sure it is ready for the loading screen
	con_width = 0;

	Unlock_state();

	CON_RecalcSize();

	CON_SetupColormaps();

	Lock_state();

	con_hudlines = atoi(cons_hudlines.defaultvalue);

	Unlock_state();

	// setup console input filtering
	CON_InputInit();

	// register our commands
	//
	COM_AddCommand("cls", CONS_Clear_f);
	//COM_AddCommand("english", CONS_English_f);
	// set console full screen for game startup MAKE SURE VID_Init() done !!!
	Lock_state();

	con_destlines = vid.height;
	con_curlines = vid.height;

	if (!dedicated)
	{

		con_started = true;
		con_startup = true;
		consoletoggle = false;
	}
	else
	{
		con_started = true;
		con_startup = false;
		consoletoggle = true;
	}

	Unlock_state();
}

void CON_Register(void)
{
	if (dedicated)
		return;

	CV_RegisterVar(&cons_msgtimeout);
	CV_RegisterVar(&cons_hudlines);
	CV_RegisterVar(&cons_speed);
	CV_RegisterVar(&cons_height);
	CV_RegisterVar(&cons_backpic);
	CV_RegisterVar(&cons_backcolor);
	CV_RegisterVar(&cons_menuhighlight);
	COM_AddCommand("bind", CONS_Bind_f);
}

// Console input initialization
//
static void CON_InputInit(void)
{
	Lock_state();

	// prepare the first prompt line
	memset(inputlines, 0, sizeof (inputlines));
	inputline = 0;

	M_TextInputInit(&input, inputlines[inputline], CON_MAXPROMPTCHARS);

	Unlock_state();
}

//======================================================================
//                        CONSOLE EXECUTION
//======================================================================

// Called at screen size change to set the rows and line size of the
// console text buffer.
//
static void CON_RecalcSize(void)
{
	size_t conw, oldcon_width, oldnumlines, i, oldcon_cy;
	char *tmp_buffer;
	char *string;

	Lock_state();

	switch (cv_constextsize.value)
	{
		case V_NOSCALEPATCH:
			con_scalefactor = 1;
			break;
		case V_SMALLSCALEPATCH:
			con_scalefactor = vid.smalldup;
			break;
		case V_MEDSCALEPATCH:
			con_scalefactor = vid.meddup;
			break;
		default:	// Full scaling
			con_scalefactor = vid.dup;
			break;
	}

	con_recalc = false;

	if (dedicated)
		conw = 1;
	else
		conw = (vid.width>>3) / con_scalefactor - 2;

	if (con_curlines == vid.height) // first init
	{
		con_curlines = vid.height;
		con_destlines = vid.height;
	}

	if (con_destlines > 0) // Resize console if already open
	{
		CON_ChangeHeight();
		con_curlines = con_destlines;
	}

	// check for change of video width
	if (conw == con_width)
	{
		Unlock_state();
		return; // didn't change
	}

	Unlock_state();

	tmp_buffer = Z_Malloc(CON_BUFFERSIZE, PU_STATIC, NULL);
	string = Z_Malloc(CON_BUFFERSIZE, PU_STATIC, NULL); // BP: it is a line but who know

	Lock_state();

	oldcon_width = con_width;
	oldnumlines = con_totallines;
	oldcon_cy = con_cy;
	memcpy(tmp_buffer, con_buffer, CON_BUFFERSIZE);

	if (conw < 1)
		con_width = (BASEVIDWIDTH>>3) - 2;
	else
		con_width = conw;

	con_width += 11; // Graue 06-19-2004 up to 11 control chars per line

	con_totallines = CON_BUFFERSIZE / con_width;
	memset(con_buffer, ' ', CON_BUFFERSIZE);

	con_cx = 0;
	con_cy = con_totallines-1;
	con_line = &con_buffer[con_cy*con_width];
	con_scrollup = 0;

	Unlock_state();

	// re-arrange console text buffer to keep text
	if (oldcon_width) // not the first time
	{
		for (i = oldcon_cy + 1; i < oldcon_cy + oldnumlines; i++)
		{
			if (tmp_buffer[(i%oldnumlines)*oldcon_width])
			{
				memcpy(string, &tmp_buffer[(i%oldnumlines)*oldcon_width], oldcon_width);
				conw = oldcon_width - 1;
				while (string[conw] == ' ' && conw)
					conw--;
				string[conw+1] = '\n';
				string[conw+2] = '\0';
				CON_Print(string);
			}
		}
	}

	Z_Free(string);
	Z_Free(tmp_buffer);
}

static void CON_ChangeHeight(void)
{
	int32_t minheight;

	Lock_state();

	minheight = 20 * con_scalefactor;	// 20 = 8+8+4

	// toggle console in
	con_destlines = (cons_height.value*vid.height)/100;
	if (con_destlines < minheight)
		con_destlines = minheight;
	else if (con_destlines > vid.height)
		con_destlines = vid.height;

	con_destlines &= ~0x3; // multiple of text row height

	Unlock_state();
}

// Handles Console moves in/out of screen (per frame)
//
void CON_MoveConsole(void)
{
	static fixed_t fracmovement = 0;

	Lock_state();

	if (con_curlines == con_destlines)
	{
		Unlock_state();
		return;
	}

	// instant
	if (!cons_speed.value)
	{
		con_curlines = con_destlines;
		Unlock_state();
		return;
	}

	// Not instant - Increment fracmovement fractionally
	fracmovement += FixedMul(cons_speed.value*vid.fdup, renderdeltatics);

	if (con_curlines < con_destlines) // Move the console downwards
	{
		con_curlines += FixedInt(fracmovement); // Move by fracmovement's integer value
		if (con_curlines > con_destlines) // If we surpassed the destination...
			con_curlines = con_destlines; // ...clamp to it!
	}
	else // Move the console upwards
	{
		con_curlines -= FixedInt(fracmovement);
		if (con_curlines < con_destlines)
			con_curlines = con_destlines;

		if (con_destlines == 0) // If the console is being closed, not just moved up...
			con_tick = 0; // ...don't show the blinking cursor
	}

	fracmovement %= FRACUNIT; // Reset fracmovement's integer value, but keep the fraction

	Unlock_state();
}

static char shiftxform[] =
{
	0,
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
	11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
	31,
	' ', '!', '"', '#', '$', '%', '&',
	'"', // shift-'
	'(', ')', '*', '+',
	'<', // shift-,
	'_', // shift--
	'>', // shift-.
	'?', // shift-/
	')', // shift-0
	'!', // shift-1
	'@', // shift-2
	'#', // shift-3
	'$', // shift-4
	'%', // shift-5
	'^', // shift-6
	'&', // shift-7
	'*', // shift-8
	'(', // shift-9
	':',
	':', // shift-;
	'<',
	'+', // shift-=
	'>', '?', '@',
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
	'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
	'{', // shift-[
	'|', // shift-backslash - OH MY GOD DOES WATCOM SUCK
	'}', // shift-]
	'"', '_',
	'~', // shift-`
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
	'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
	'{', '|', '}', '~', 127
};

int32_t CON_ShiftChar(int32_t ch)
{
	if (I_UseNativeKeyboard() || ch >= (int32_t)sizeof(shiftxform))
		return ch;

	// warning: shiftdown is NOT a dboolean, it's 1 or 2 for lshift/rshift
	if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
		return !!shiftdown != capslock ? shiftxform[ch] : ch;
	else	// if we're holding shift we should still shift non letter symbols
		return shiftdown ? shiftxform[ch] : ch;
}

// Clear time of console heads up messages
//
void CON_ClearHUD(void)
{
	int32_t i;

	Lock_state();

	for (i = 0; i < con_hudlines; i++)
		con_hudtime[i] = 0;

	Unlock_state();
}

// Force console to move out immediately
// note: con_ticker will set consoleready false
void CON_ToggleOff(void)
{
	Lock_state();

	if (!con_destlines)
	{
		Unlock_state();
		return;
	}

	con_destlines = 0;
	con_curlines = 0;
	CON_ClearHUD();
	con_forcepic = 0;

	I_UpdateMouseGrab();

	I_SetTextInput(false);

	Unlock_state();
}

dboolean CON_Ready(void)
{
	dboolean ready;
	Lock_state();
	{
		ready = consoleready;
	}
	Unlock_state();
	return ready;
}

// Console ticker: handles console move in/out, cursor blinking
//
void CON_Ticker(void)
{
	int32_t i;
	int32_t minheight;

	Lock_state();

	minheight = 20 * con_scalefactor;	// 20 = 8+8+4

	// cursor blinking
	con_tick++;
	con_tick &= 7;

	// console key was pushed
	if (consoletoggle)
	{
		consoletoggle = false;

		// toggle off console
		if (con_destlines > 0)
		{
			con_destlines = 0;
			CON_ClearHUD();
			I_UpdateMouseGrab();
			I_SetTextInput(false);
		}
		else
		{
			CON_ChangeHeight();
			I_SetTextInput(true);
		}
	}

	// check if console ready for prompt
	if (con_destlines >= minheight)
	{
		consoleready = true;
	}
	else
	{
		consoleready = false;
	}
	// make overlay messages disappear after a while
	for (i = 0; i < con_hudlines; i++)
	{
		con_hudtime[i]--;
		if (con_hudtime[i] < 0)
			con_hudtime[i] = 0;
	}

	Unlock_state();
}

//
// ----
//

static void AdjustTextSize(int32_t n)
{
	int32_t range = vid.dup;
	if (range < 3) // make bigger jumps if the resolution is small
		n *= 4 - range;

	// 1-3 ascending is larger
	// but 0 is special and is the largest
	int32_t cur = (cv_constextsize.value & V_SCALEPATCHMASK) >> V_SCALEPATCHSHIFT;
	if (!cur) // reverse direction at 0
	{
		if (n < 0)
			cur = 4 + n;
		else
			return;
	}
	else
		cur += n;
	if (cur < 1) // min: V_NOSCALEPATCH
		cur = 1;
	if (cur > 4) // max: 4 & 3 = 0
		cur = 4;
	CV_SetValue(&cv_constextsize, (cur << V_SCALEPATCHSHIFT) & V_SCALEPATCHMASK);
}

// Handles console key input
//
dboolean CON_Responder(event_t *ev)
{
	static uint8_t consdown = false; // console is treated differently due to rare usage

	// sequential completions a la 4dos
	static char completioncmd[80 + sizeof("find ")] = "find ";
	static char *completion = &completioncmd[sizeof("find ")-1];

	static int32_t skips;

	static int32_t   com_skips;
	static int32_t   var_skips;
	static int32_t alias_skips;

	const char *cmd = NULL;
	int32_t key;

	if (chat_on)
		return false;

	// let go keyup events, don't eat them
	if (ev->type != ev_keydown && ev->type != ev_console)
	{
		if (G_ControlBoundToKey(0, gc_console, ev->data1, false))
			consdown = false;
		return false;
	}

	key = ev->data1;

	// check for console toggle key
	if (ev->type != ev_console)
	{
		if ((modeattacking != ATTACKING_NONE && modeattacking != ATTACKING_MENU && !demo.playback) || metalrecording || marathonmode)
			return false;

		if (ev->data1 >= NUMKEYS) // See also: HUD_Responder
		{
			int32_t i;
			for (i = 0; i < num_gamecontrols; i++)
			{
				if (G_ControlBoundToKey(0, i, ev->data1, false))
					break;
			}

			if (i == num_gamecontrols)
				return false;
		}

		if (G_ControlBoundToKey(0, gc_console, key, false))
		{
			if (consdown) // ignore repeat
				return true;
			consoletoggle = true;
			consdown = true;
			return true;
		}

		// check other keys only if console prompt is active
		if (!consoleready && key < NUMINPUTS) // metzgermeister: boundary check!!
		{
			if (!menustack[0] && bindtable[key])
			{
				COM_BufAddText(bindtable[key]);
				COM_BufAddText("\n");
				return true;
			}
			return false;
		}

		// escape key toggle off console
		if (key == KEY_ESCAPE)
		{
			consoletoggle = true;
			return true;
		}
	}

	Lock_state();
	M_TextInputHandle(&input, key);
	Unlock_state();

	// Always eat ctrl/shift/alt if console open, so the menu doesn't get ideas
	if (key == KEY_LSHIFT || key == KEY_RSHIFT
	 || key == KEY_LCTRL || key == KEY_RCTRL
	 || key == KEY_LALT || key == KEY_RALT)
		return true;

	// ctrl modifier -- changes behavior, adds shortcuts
	if (ctrldown)
	{
		// show all cvars/commands that match what we have inputted
		if (key == KEY_TAB)
		{
			if (!completion[0])
			{
				if (!input.length || input.length >= 40 || strchr(input.buffer, ' '))
					return true;
				strcpy(completion, input.buffer);
			}
			COM_BufInsertText(completioncmd);
			completion[0] = 0;
			return true;
		}
		// ---

		if (key == KEY_HOME) // oldest text in buffer
		{
			con_scrollup = (con_totallines-((con_curlines-16)>>3));
			return true;
		}
		else if (key == KEY_END) // most recent text in buffer
		{
			con_scrollup = 0;
			return true;
		}

		// Those are already handled in M_TextInputHandle, but they do extra logic... Maybe textinput_t should have some sort of callbacks?
		if (key == 'x' || key == 'X' || key == 'v' || key == 'V')
			completion[0] = 0;

		// Zoom
		if (key == '=')
		{
			AdjustTextSize(1);
			return true;
		}
		else if (key == '-')
		{
			AdjustTextSize(-1);
			return true;
		}

		// ...why shouldn't it eat the key? if it doesn't, it just means you
		// can control Sonic from the console, which is silly
		return true;//return false;
	}

	// command completion forward (tab) and backward (shift-tab)
	if (key == KEY_TAB)
	{
		// sequential command completion forward and backward

		// remember typing for several completions (a-la-4dos)
		if (!completion[0])
		{
			if (!input.length || input.length >= 40 || strchr(input.buffer, ' '))
				return true;
			strcpy(completion, input.buffer);
			skips       = 0;
			com_skips   = 0;
			var_skips   = 0;
			alias_skips = 0;
		}
		else
		{
			if (shiftdown)
			{
				if (skips > 0)
					skips--;
			}
			else
			{
				skips++;
			}
		}

		if (skips <= com_skips)
		{
			cmd = COM_CompleteCommand(completion, skips);

			if (cmd && skips == com_skips)
			{
				com_skips  ++;
				var_skips  ++;
				alias_skips++;
			}
		}

		if (!cmd && skips <= var_skips)
		{
			cmd = CV_CompleteVar(completion, skips - com_skips);

			if (cmd && skips == var_skips)
			{
				var_skips  ++;
				alias_skips++;
			}
		}

		if (!cmd && skips <= alias_skips)
		{
			cmd = COM_CompleteAlias(completion, skips - var_skips);

			if (cmd && skips == alias_skips)
			{
				alias_skips++;
			}
		}

		if (cmd)
		{
			Lock_state();
			M_TextInputSetString(&input, va("%s ", cmd));
			Unlock_state();
		}
		else
		{
			skips--;
		}

		return true;
	}

	// move up (backward) in console textbuffer
	if (key == KEY_PGUP)
	{
		if (con_scrollup < (con_totallines-((con_curlines-16)>>3)))
			con_scrollup++;
		return true;
	}
	else if (key == KEY_PGDN)
	{
		if (con_scrollup > 0)
			con_scrollup--;
		return true;
	}

	// At this point we're messing with input
	// Clear completion
	completion[0] = 0;

	// command enter
	if (key == KEY_ENTER)
	{
		if (!input.length)
			return true;

		// push the command
		COM_BufAddText(input.buffer);
		COM_BufAddText("\n");

		CONS_Printf("\x86""%c""\x80""%s\n", CON_PROMPTCHAR, inputlines[inputline]);

		Lock_state();

		// Only add command to history if it differs from previous one
		if (!fastcmp(input.buffer, inputlines[(inputline-1) & 31]))
		{
			inputline = (inputline+1) & 31;
			M_TextInputInit(&input, inputlines[inputline], CON_MAXPROMPTCHARS);
		}

		inputhist = inputline;
		M_TextInputClear(&input);

		Unlock_state();

		return true;
	}

	// move back in input history
	if (key == KEY_UPARROW)
	{
		// copy one of the previous inputlines to the current
		do
			inputhist = (inputhist - 1) & 31; // cycle back
		while (inputhist != inputline && !inputlines[inputhist][0]);

		// stop at the last history input line, which is the
		// current line + 1 because we cycle through the 32 input lines
		if (inputhist == inputline)
			inputhist = (inputline + 1) & 31;

		Lock_state();
		M_TextInputSetString(&input, inputlines[inputhist]);
		Unlock_state();
		return true;
	}

	// move forward in input history
	if (key == KEY_DOWNARROW)
	{
		if (inputhist == inputline)
			return true;
		do
			inputhist = (inputhist + 1) & 31;
		while (inputhist != inputline && !inputlines[inputhist][0]);

		// back to currentline
		Lock_state();
		if (inputhist == inputline)
			M_TextInputClear(&input);
		else
			M_TextInputSetString(&input, inputlines[inputhist]);
		Unlock_state();

		return true;
	}

	return true;
}

// Insert a new line in the console text buffer
//
static void CON_Linefeed(void)
{
	// set time for heads up messages
	con_hudtime[con_cy%con_hudlines] = cons_msgtimeout.value*TICRATE;

	con_cy++;
	con_cx = 0;

	con_line = &con_buffer[(con_cy%con_totallines)*con_width];
	memset(con_line, ' ', con_width);
}

// Outputs text into the console text buffer
static void CON_Print(char *msg)
{
	size_t l;
	int32_t controlchars = 0; // for color changing
	char color = '\x80';  // keep color across lines

	if (msg == NULL)
		return;

	if (*msg == '\3') // chat text, makes ding sound
		S_StartSound(NULL, sfx_radio);
	else if (*msg == '\4') // chat action, dings and is in yellow
	{
		*msg = '\x82'; // yellow
		S_StartSound(NULL, sfx_radio);
	}

	Lock_state();

	if (!(*msg & 0x80))
	{
		con_line[con_cx++] = '\x80';
		controlchars = 1;
	}

	while (*msg)
	{
		// skip non-printable characters and white spaces
		while (*msg && *msg <= ' ')
		{
			if (*msg & 0x80)
			{
				color = con_line[con_cx++] = *(msg++);
				controlchars++;
				continue;
			}
			else if (*msg == '\r') // carriage return
			{
				con_cy--;
				CON_Linefeed();
				color = '\x80';
				controlchars = 0;
			}
			else if (*msg == '\n') // linefeed
			{
				CON_Linefeed();
				con_line[con_cx++] = color;
				controlchars = 1;
			}
			else if (*msg == ' ') // space
			{
				con_line[con_cx++] = ' ';
				if (con_cx - controlchars >= con_width-11)
				{
					CON_Linefeed();
					con_line[con_cx++] = color;
					controlchars = 1;
				}
			}
			else if (*msg == '\t')
			{
				// adds tab spaces for nice layout in console

				do
				{
					con_line[con_cx++] = ' ';
				} while ((con_cx - controlchars) % 4 != 0);

				if (con_cx - controlchars >= con_width-11)
				{
					CON_Linefeed();
					con_line[con_cx++] = color;
					controlchars = 1;
				}
			}
			msg++;
		}

		if (*msg == '\0')
		{
			Unlock_state();
			return;
		}

		// printable character
		for (l = 0; l < (con_width-11) && msg[l] > ' '; l++)
			;

		// word wrap
		if ((con_cx - controlchars) + l > con_width-11)
		{
			CON_Linefeed();
			con_line[con_cx++] = color;
			controlchars = 1;
		}

		// a word at a time
		for (; l > 0; l--)
			con_line[con_cx++] = *(msg++);
	}

	Unlock_state();
}

void CON_LogMessage(const char *msg)
{
	char txt[8192], *t;
	const char *p = msg, *e = txt+sizeof (txt)-2;

	for (t = txt; *p != '\0'; p++)
	{
		if (*p == '\n' || *p >= ' ') // don't log or console print CON_Print's control characters
			*t++ = *p;

		if (t >= e)
		{
			*t = '\0'; //end of string
			I_OutputMsg("%s", txt); //print string
			t = txt; //reset t pointer
			memset(txt,'\0', sizeof (txt)); //reset txt
		}
	}
	*t = '\0'; //end of string
	I_OutputMsg("%s", txt);
}

// Console print! Wahooo! Lots o fun!
//

void CONS_Printf(const char *fmt, ...)
{
	va_list argptr;
	static char *txt = NULL;

	if (txt == NULL)
		txt = malloc(8192);

	va_start(argptr, fmt);
	vsnprintf(txt, 8192, fmt, argptr);
	va_end(argptr);

	Lock_state();

	// echo console prints to log file
	DEBFILE(txt);

	// write message in con text buffer
	if (con_started)
		CON_Print(txt);

	CON_LogMessage(txt);

	// make sure new text is visible
	con_scrollup = 0;

	Unlock_state();
}

void CONS_Alert(alerttype_t level, const char *fmt, ...)
{
	va_list argptr;
	static char *txt = NULL;

	if (txt == NULL)
		txt = malloc(8192);

	va_start(argptr, fmt);
	vsnprintf(txt, 8192, fmt, argptr);
	va_end(argptr);

	switch (level)
	{
		case CONS_NOTICE:
			// no notice for notices, hehe
			CONS_Printf("\x83" "%s" "\x80 ", M_GetText("NOTICE:"));
			break;
		case CONS_WARNING:
			refreshdirmenu |= REFRESHDIR_WARNING;
			CONS_Printf("\x82" "%s" "\x80 ", M_GetText("WARNING:"));
			break;
		case CONS_ERROR:
			refreshdirmenu |= REFRESHDIR_ERROR;
			CONS_Printf("\x85" "%s" "\x80 ", M_GetText("ERROR:"));
			break;
	}

	// I am lazy and I feel like just letting CONS_Printf take care of things.
	// Is that okay?
	CONS_Printf("%s", txt);
}

void CONS_Debug(uint32_t debugflags, const char *fmt, ...)
{
	va_list argptr;
	static char *txt = NULL;

	if ((cht_debug & debugflags) != debugflags)
		return;

	if (txt == NULL)
		txt = malloc(8192);

	va_start(argptr, fmt);
	vsnprintf(txt, 8192, fmt, argptr);
	va_end(argptr);

	// Again I am lazy, oh well
	CONS_Printf("%s", txt);
}


// Print an error message, and wait for ENTER key to continue.
// To make sure the user has seen the message
//
void CONS_Error(const char *msg)
{
	CONS_Printf("\x82%s", msg); // write error msg in different colour
	CONS_Printf(M_GetText("Press ENTER to continue\n"));

	// dirty quick hack, but for the good cause
	while (I_GetKey() != KEY_ENTER)
		I_OsPolling();
}

//======================================================================
//                          CONSOLE DRAW
//======================================================================

// draw console prompt line
//
static void CON_DrawInput(void)
{
	int32_t charwidth = (int32_t)con_scalefactor << 3;
	const char *p = inputlines[inputline];
	size_t c, clen, cend;
	uint8_t lellip = 0, rellip = 0;
	int32_t x, y, i;

	y = con_curlines - 12 * con_scalefactor;
	x = charwidth*2;

	clen = con_width-13;

	if (input.length <= clen)
	{
		c = 0;
		clen = input.length;
	}
	else // input line scrolls left if it gets too long
	{
		clen -= 2; // There will always be some extra truncation -- but where is what we'll find out

		if (input.cursor <= clen/2)
		{
			// Close enough to right edge to show all
			c = 0;
			// Always will truncate right side from this position, so always draw right ellipsis
			rellip = 1;
		}
		else
		{
			// Cursor in the middle (or right side) of input
			// Move over for the ellipsis
			c = input.cursor - (clen/2) + 2;
			x += charwidth*2;
			lellip = 1;

			if (c + clen >= input.length)
			{
				// Cursor in the right side of input
				// We were too far over, so move back
				c = input.length - clen;
			}
			else
			{
				// Cursor in the middle -- ellipses on both sides
				clen -= 2;
				rellip = 1;
			}
		}
	}

	if (lellip)
	{
		x -= charwidth*3;
		if (input.select < c)
			V_DrawFill(x, y, charwidth*3, (10 * con_scalefactor), 107 | V_NOSCALESTART);
		for (i = 0; i < 3; ++i, x += charwidth)
			V_DrawCharacter(x, y, '.' | cv_constextsize.value | V_GRAYMAP | V_NOSCALESTART, true);
	}
	else
		V_DrawCharacter(x-charwidth, y, CON_PROMPTCHAR | cv_constextsize.value | V_GRAYMAP | V_NOSCALESTART, true);

	for (cend = c + clen; c < cend; ++c, x += charwidth)
	{
		if ((input.select > c && input.cursor <= c) || (input.select <= c && input.cursor > c))
		{
			V_DrawFill(x, y, charwidth, (10 * con_scalefactor), 77 | V_NOSCALESTART);
			V_DrawCharacter(x, y, p[c] | cv_constextsize.value | V_YELLOWMAP | V_NOSCALESTART, true);
		}
		else
			V_DrawCharacter(x, y, p[c] | cv_constextsize.value | V_NOSCALESTART, true);

		if (c == input.cursor && con_tick >= 4)
			V_DrawCharacter(x, y + (con_scalefactor*2), '_' | cv_constextsize.value | V_NOSCALESTART, true);
	}
	if (cend == input.cursor && con_tick >= 4)
		V_DrawCharacter(x, y + (con_scalefactor*2), '_' | cv_constextsize.value | V_NOSCALESTART, true);
	if (rellip)
	{
		if (input.select > cend)
			V_DrawFill(x, y, charwidth*3, (10 * con_scalefactor), 107 | V_NOSCALESTART);
		for (i = 0; i < 3; ++i, x += charwidth)
			V_DrawCharacter(x, y, '.' | cv_constextsize.value | V_GRAYMAP | V_NOSCALESTART, true);
	}
}

// draw the last lines of console text to the top of the screen
static void CON_DrawHudlines(void)
{
	uint8_t *p;
	size_t i;
	int32_t y;
	int32_t charflags = 0;
	int32_t charwidth = 8 * con_scalefactor;
	int32_t charheight = 8 * con_scalefactor;

	if (con_hudlines <= 0)
		return;

	if (chat_on && OLDCHAT)
		y = charheight; // leave place for chat input in the first row of text (only do it if consolechat is on.)
	else
		y = 0;

	for (i = con_cy - con_hudlines+1; i <= con_cy; i++)
	{
		size_t c;
		int32_t x;

		if ((signed)i < 0)
			continue;
		if (con_hudtime[i%con_hudlines] == 0)
			continue;

		p = (uint8_t *)&con_buffer[(i%con_totallines)*con_width];

		for (c = 0, x = 0; c < con_width; c++, x += charwidth, p++)
		{
			while (*p & 0x80) // Graue 06-19-2004
			{
				charflags = (*p & 0x7f) << V_CHARCOLORSHIFT;
				p++;
				c++;
			}
			if (c >= con_width)
				break;

			int emotelen;
			emote_t *emote;

			if ((emote = M_VerifyEmote((const char *)p, &emotelen)))
			{
				M_DrawScaledEmote(x<<FRACBITS, (y+2*con_scalefactor)<<FRACBITS, charwidth*FRACUNIT/EMOTEWIDTH, emote, V_NOSCALESTART|V_NOSCALEPATCH);
				p += emotelen-1;
				c += emotelen-1;
				continue;
			}
			else if (*p < HU_FONTSTART)
				;//charwidth = 4 * con_scalefactor;
			else
			{
				//charwidth = (hu_font['A'-HU_FONTSTART]->width) * con_scalefactor;
				V_DrawCharacter(x, y, (int32_t)(*p) | charflags | cv_constextsize.value | V_NOSCALESTART, true);
			}
		}

		//V_DrawCharacter(x, y, (p[c]&0xff) | cv_constextsize.value | V_NOSCALESTART, true);
		y += charheight;
	}
}

// Lactozilla: Draws the console's background picture.
static void CON_DrawBackpic(void)
{
	patch_t *con_backpic;
	lumpnum_t piclump;
	int x, w, h;

	// Get the lumpnum for CONSBACK, STARTUP (Only during game startup) or fallback into MISSING.
	if (con_startup)
		piclump = W_CheckNumForName("STARTUP");
	else
		piclump = W_CheckNumForName("KARTKREW");

	if (piclump == LUMPERROR)
		piclump = W_GetNumForName("MISSING");

	// Cache the patch.
	con_backpic = W_CachePatchNum(piclump, PU_PATCH);

	// Center the backpic, and draw a vertically cropped patch.
	w = (BASEVIDWIDTH * vid.dup);
	x = (vid.width / 2) - (w / 2);
	h = con_curlines/vid.dup;

	// If the patch doesn't fill the entire screen,
	// then fill the sides with a solid color.
	if (x > 0)
	{
		column_t *column = (column_t *)((uint8_t *)(con_backpic->columns) + (con_backpic->columnofs[0]));
		if (!column->topdelta)
		{
			uint8_t *source = (uint8_t *)(column) + 3;
			int32_t color = (source[0] | V_NOSCALESTART);
			// left side
			V_DrawFill(0, 0, x, con_curlines, color);
			// right side
			V_DrawFill((x + w), 0, (vid.width - w), con_curlines, color);
		}
	}

	// Draw the patch.
	V_DrawCroppedPatch(x << FRACBITS, 0, FRACUNIT, V_NOSCALESTART, con_backpic,
			0, ( BASEVIDHEIGHT - h ), BASEVIDWIDTH, h);

	// Unlock the cached patch.
	W_UnlockCachedPatch(con_backpic);
}

// draw the console background, text, and prompt if enough place
//
static void CON_DrawConsole(void)
{
	uint8_t *p;
	size_t i;
	int32_t y;
	int32_t charflags = 0;
	int32_t charwidth = (int32_t)con_scalefactor << 3;
	int32_t charheight = charwidth;
	int32_t minheight = 20 * con_scalefactor;	// 20 = 8+8+4

	if (con_curlines <= 0)
		return;

	// draw console background
	if (cons_backpic.value || con_forcepic)
		CON_DrawBackpic();
	else
	{
		// inu: no more width (was always 0 and vid.width)
		if (rendermode != render_none)
			V_DrawFadeConsBack(con_curlines); // translucent background
	}

	// draw console text lines from top to bottom
	if (con_curlines >= minheight)
	{
		i = con_cy - con_scrollup;

		// skip the last empty line due to the cursor being at the start of a new line
		i--;

		i -= (con_curlines - minheight) / charheight;

		if (rendermode == render_none) return;

		for (y = (con_curlines-minheight) % charheight; y <= con_curlines-minheight; y += charheight, i++)
		{
			int32_t x;
			size_t c;

			p = (uint8_t *)&con_buffer[((i > 0 ? i : 0)%con_totallines)*con_width];

			for (c = 0, x = charwidth; c < con_width; c++, x += charwidth, p++)
			{
				while (*p & 0x80)
				{
					charflags = (*p & 0x7f) << V_CHARCOLORSHIFT;
					p++;
					c++;
				}
				if (c >= con_width)
					break;

				int emotelen;
				emote_t *emote;

				if ((emote = M_VerifyEmote((const char *)p, &emotelen)))
				{
					M_DrawScaledEmote(x<<FRACBITS, (y+2*con_scalefactor)<<FRACBITS, charwidth*FRACUNIT/EMOTEWIDTH, emote, V_NOSCALESTART|V_NOSCALEPATCH);
					p += emotelen-1;
					c += emotelen-1;
					continue;
				}
				V_DrawCharacter(x, y, (int32_t)(*p) | charflags | cv_constextsize.value | V_NOSCALESTART, true);
			}
		}
	}

	// draw prompt if enough place (not while game startup)
	if ((con_curlines >= (minheight-charheight)) && !con_startup)
		CON_DrawInput();
}

static dboolean CON_GamestateDrawHudLines(void)
{
	switch (gamestate)
	{
		case GS_LEVEL:
		case GS_INTERMISSION:
		case GS_VOTING:
		case GS_CUTSCENE:
		case GS_CREDITS:
		case GS_BLANCREDITS:
		case GS_SECRETCREDITS:
		case GS_EVALUATION:
		case GS_WAITINGPLAYERS:
			return true;

		default:
			return false;
	}
}

// Console refresh drawer, call each frame
//
void CON_Drawer(void)
{
	Lock_state();

	if (!con_started || !graphics_started)
	{
		Unlock_state();
		return;
	}

	if (con_recalc)
	{
		CON_RecalcSize();
		if (con_curlines <= 0)
			CON_ClearHUD();
	}

	// console movement
	CON_MoveConsole();

	if (con_curlines > 0)
		CON_DrawConsole();
	else if (CON_GamestateDrawHudLines() == true)
		CON_DrawHudlines();

	Unlock_state();
}

static const char *CON_LoadingStrings[LOADED_ALLDONE+1] =
{
	"Init zone memory...", //LOADED_ZINIT
	"Init game timing...", //LOADED_ISTARTUPTIMER
	"Loading main assets...", //LOADED_IWAD
	"Init graphics subsystem...", //LOADED_ISTARTUPGRAPHICS
	"Cache fonts...", //LOADED_HUINIT
	"Loading add-ons...", //LOADED_PWAD
	"Load settings...", //LOADED_CONFIG
	"Cache textures...", //LOADED_INITTEXTUREDATA
	"Cache sprites...", //LOADED_INITSPIRTES
	"Load characters...", //LOADED_INITSKINS
	"Init rendering daemon...", //LOADED_RINIT
	"Init audio subsystem...", //LOADED_SINITSFXCHANNELS
	"Cache HUD...", //LOADED_STINIT
	"Init ACSVM...", //LOADED_ACSINIT
	"Check game status...", //LOADED_DCHECKNETGAME
	"Now starting..."
}; // see also con_loadprogress_t in console.h

//
// Error handling for the loading bar, to ensure it doesn't skip any steps.
//
void CON_SetLoadingProgress(con_loadprogress_t newStep)
{
	const con_loadprogress_t expectedStep = con_startup_loadprogress + 1;

	if (newStep != expectedStep)
	{
		I_Error("Something is wrong with the loading bar! (got %d, expected %d)\n", newStep, expectedStep);
		return;
	}

	con_startup_loadprogress = newStep;

	if (con_startup_loadprogress <= LOADED_ALLDONE)
		CONS_Printf("LOADING UPDATE - %s\n", CON_LoadingStrings[con_startup_loadprogress]);

	if (con_startup_loadprogress < LOADED_ISTARTUPGRAPHICS) // rendering not possible?
		return;
	CON_DrawLoadBar(); // here we display the console text
	I_OsPolling();
	I_UpdateNoBlit();
	I_FinishUpdate(); // page flip or blit buffer
}

//
// Draws a simple white fill at the bottom of startup for load progress
//
void CON_DrawLoadBar(void)
{
	const int16_t barheight = 2;
	int16_t barwidth = 0;

	Lock_state();

	if (!con_started || !graphics_started)
	{
		Unlock_state();
		return;
	}

	CON_DrawBackpic();

	barwidth = (BASEVIDWIDTH * con_startup_loadprogress) / LOADED_ALLDONE;
	V_DrawFill(0, BASEVIDHEIGHT - barheight, barwidth, barheight, 0);
	if (con_startup_loadprogress <= LOADED_ALLDONE)
		V_DrawString(4, BASEVIDHEIGHT - (barheight + 8 + 4), 0, CON_LoadingStrings[con_startup_loadprogress]);

	Unlock_state();
}
