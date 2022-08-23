// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  g_input.c
/// \brief handle mouse/keyboard/joystick inputs,
///        maps inputs to game controls (forward, spin, jump...)

#include "doomdef.h"
#include "doomstat.h"
#include "g_input.h"
#include "keys.h"
#include "hu_stuff.h" // need HUFONT start & end
#include "d_net.h"
#include "console.h"
#include "i_joy.h" // JOYAXISRANGE

#define MAXMOUSESENSITIVITY 100 // sensitivity steps

static CV_PossibleValue_t mousesens_cons_t[] = {{1, "MIN"}, {MAXMOUSESENSITIVITY, "MAX"}, {0, NULL}};
static CV_PossibleValue_t onecontrolperkey_cons_t[] = {{1, "One"}, {2, "Several"}, {0, NULL}};
static CV_PossibleValue_t turnsmooth_cons_t[] = {{2, "Slow"}, {1, "Fast"}, {0, "Off"}, {0, NULL}};

// mouse values are used once
consvar_t cv_mousesens = CVAR_INIT ("mousesens", "20", CV_SAVE, mousesens_cons_t, NULL);
consvar_t cv_mousesens2 = CVAR_INIT ("mousesens2", "20", CV_SAVE, mousesens_cons_t, NULL);
consvar_t cv_mouseysens = CVAR_INIT ("mouseysens", "20", CV_SAVE, mousesens_cons_t, NULL);
consvar_t cv_mouseysens2 = CVAR_INIT ("mouseysens2", "20", CV_SAVE, mousesens_cons_t, NULL);
consvar_t cv_controlperkey = CVAR_INIT ("controlperkey", "One", CV_SAVE, onecontrolperkey_cons_t, NULL);
consvar_t cv_turnsmooth = CVAR_INIT ("turnsmoothing", "Slow", CV_SAVE, turnsmooth_cons_t, NULL);

// current state of the keys
// JOYAXISRANGE for fully pressed, 0 for not pressed
INT32 gamekeydown[MAXDEVICES][NUMINPUTS];
boolean deviceResponding[MAXDEVICES]; 

// two key codes (or virtual key) per game control
INT32 gamecontrol[MAXSPLITSCREENPLAYERS][num_gamecontrols][MAXINPUTMAPPING];
INT32 gamecontroldefault[num_gamecontrols][MAXINPUTMAPPING]; // default control storage

// lists of GC codes for selective operation
/*
const INT32 gcl_accelerate[num_gcl_accelerate] = { gc_accelerate };

const INT32 gcl_brake[num_gcl_brake] = { gc_brake };

const INT32 gcl_drift[num_gcl_drift] = { gc_drift };

const INT32 gcl_movement[num_gcl_movement] = {
	gc_accelerate, gc_drift, gc_brake, gc_turnleft, gc_turnright
};

const INT32 gcl_item[num_gcl_item] = {
	gc_fire, gc_aimforward, gc_aimbackward
};

const INT32 gcl_full[num_gcl_full] = {
	gc_accelerate, gc_drift, gc_brake, gc_turnleft, gc_turnright,
	gc_fire, gc_aimforward, gc_aimbackward,
	gc_lookback
};
*/

INT32 G_GetDevicePlayer(INT32 deviceID)
{
	INT32 i;

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		if (deviceID == cv_usejoystick[i].value)
		{
			return i;
		}
	}

	return -1;
}

// converts ev_joystick axis to its corresponding key
INT32 G_AxisToKey(event_t *ev)
{
	return KEY_AXIS1 + (ev->data1 >= JOYANALOGS*2
	                    ? ev->data1 + JOYANALOGS*2
	                    : ev->data1*2 + (ev->data2 >= 0));
}

//
// Remaps the inputs to game controls.
//
// A game control can be triggered by one or more keys/buttons.
//
// Each key/mousebutton/joybutton triggers ONLY ONE game control.
//
void G_MapEventsToControls(event_t *ev)
{
	INT32 i;

	if (ev->device >= 0 && ev->device < MAXDEVICES)
	{
		switch (ev->type)
		{
			case ev_keydown:
			//case ev_keyup:
			//case ev_mouse:
			//case ev_joystick:
				deviceResponding[ev->device] = true;
				break;

			default:
				break;
		}
	}
	else
	{
		return;
	}

	switch (ev->type)
	{
		case ev_keydown:
			if (ev->data1 < NUMINPUTS)
			{
				gamekeydown[ev->device][ev->data1] = JOYAXISRANGE;
			}
#ifdef PARANOIA
			else
			{
				CONS_Debug(DBG_GAMELOGIC, "Bad downkey input %d\n", ev->data1);
			}
#endif
			break;

		case ev_keyup:
			if (ev->data1 < NUMINPUTS)
			{
				gamekeydown[ev->device][ev->data1] = 0;
			}
#ifdef PARANOIA
			else
			{
				CONS_Debug(DBG_GAMELOGIC, "Bad upkey input %d\n", ev->data1);
			}
#endif
			break;

		case ev_mouse: // buttons are virtual keys
			if (menuactive)
			{
				break;
			}

			// X axis
			if (ev->data2 < 0)
			{
				// Left
				gamekeydown[ev->device][KEY_MOUSEMOVE + 2] = abs(ev->data2);
				gamekeydown[ev->device][KEY_MOUSEMOVE + 3] = 0;
			}
			else
			{
				// Right
				gamekeydown[ev->device][KEY_MOUSEMOVE + 2] = 0;
				gamekeydown[ev->device][KEY_MOUSEMOVE + 3] = abs(ev->data2);
			}

			// Y axis
			if (ev->data3 < 0)
			{
				// Up
				gamekeydown[ev->device][KEY_MOUSEMOVE] = abs(ev->data3);
				gamekeydown[ev->device][KEY_MOUSEMOVE + 1] = 0;
			}
			else
			{
				// Down
				gamekeydown[ev->device][KEY_MOUSEMOVE] = 0;
				gamekeydown[ev->device][KEY_MOUSEMOVE + 1] = abs(ev->data3);
			}
			break;

		case ev_joystick: // buttons are virtual keys
			if (menuactive)
			{
				break;
			}

			if (ev->data1 >= JOYAXISSETS*2)
			{
#ifdef PARANOIA
				CONS_Debug(DBG_GAMELOGIC, "Bad joystick axis event %d\n", ev->data1);
#endif
				break;
			}

			i = ev->data1;

			if (G_GetDevicePlayer(ev->device) == 0)
			{
				if (CON_Ready() || chat_on)
					break;
			}

			if (i >= 2 * JOYANALOGS)
			{
				// The trigger axes are handled specially.
				i -= 2 * JOYANALOGS;
				gamekeydown[ev->device][KEY_AXIS1 + (JOYANALOGS * 4) + i] = max(0, ev->data2);
			}
			else
			{
				i *= 2;
				if (ev->data2 < 0)
				{
					// Left
					gamekeydown[ev->device][KEY_AXIS1 + i] = abs(ev->data2);
					gamekeydown[ev->device][KEY_AXIS1 + i + 1] = 0;
				}
				else
				{
					// Right
					gamekeydown[ev->device][KEY_AXIS1 + i] = 0;
					gamekeydown[ev->device][KEY_AXIS1 + i + 1] = abs(ev->data2);
				}
			}

			break;

		default:
			break;
	}
}

typedef struct
{
	INT32 keynum;
	const char *name;
} keyname_t;

static keyname_t keynames[] =
{
	{KEY_SPACE, "SPACE"},
	{KEY_CAPSLOCK, "CAPS LOCK"},
	{KEY_ENTER, "ENTER"},
	{KEY_TAB, "TAB"},
	{KEY_ESCAPE, "ESCAPE"},
	{KEY_BACKSPACE, "BACKSPACE"},

	{KEY_NUMLOCK, "NUM LOCK"},
	{KEY_SCROLLLOCK, "SCROLL LOCK"},

	// bill gates keys
	{KEY_LEFTWIN, "LWINDOWS"},
	{KEY_RIGHTWIN, "RWINDOWS"},
	{KEY_MENU, "MENU"},

	{KEY_LSHIFT, "LSHIFT"},
	{KEY_RSHIFT, "RSHIFT"},
	{KEY_LSHIFT, "SHIFT"},
	{KEY_LCTRL, "LCTRL"},
	{KEY_RCTRL, "RCTRL"},
	{KEY_LCTRL, "CTRL"},
	{KEY_LALT, "LALT"},
	{KEY_RALT, "RALT"},
	{KEY_LALT, "ALT"},

	// keypad keys
	{KEY_KPADSLASH, "KEYPAD /"},
	{KEY_KEYPAD7, "KEYPAD 7"},
	{KEY_KEYPAD8, "KEYPAD 8"},
	{KEY_KEYPAD9, "KEYPAD 9"},
	{KEY_MINUSPAD, "KEYPAD -"},
	{KEY_KEYPAD4, "KEYPAD 4"},
	{KEY_KEYPAD5, "KEYPAD 5"},
	{KEY_KEYPAD6, "KEYPAD 6"},
	{KEY_PLUSPAD, "KEYPAD +"},
	{KEY_KEYPAD1, "KEYPAD 1"},
	{KEY_KEYPAD2, "KEYPAD 2"},
	{KEY_KEYPAD3, "KEYPAD 3"},
	{KEY_KEYPAD0, "KEYPAD 0"},
	{KEY_KPADDEL, "KEYPAD ."},

	// extended keys (not keypad)
	{KEY_HOME, "HOME"},
	{KEY_UPARROW, "UP ARROW"},
	{KEY_PGUP, "PAGE UP"},
	{KEY_LEFTARROW, "LEFT ARROW"},
	{KEY_RIGHTARROW, "RIGHT ARROW"},
	{KEY_END, "END"},
	{KEY_DOWNARROW, "DOWN ARROW"},
	{KEY_PGDN, "PAGE DOWN"},
	{KEY_INS, "INSERT"},
	{KEY_DEL, "DELETE"},

	// other keys
	{KEY_F1, "F1"},
	{KEY_F2, "F2"},
	{KEY_F3, "F3"},
	{KEY_F4, "F4"},
	{KEY_F5, "F5"},
	{KEY_F6, "F6"},
	{KEY_F7, "F7"},
	{KEY_F8, "F8"},
	{KEY_F9, "F9"},
	{KEY_F10, "F10"},
	{KEY_F11, "F11"},
	{KEY_F12, "F12"},

	// KEY_CONSOLE has an exception in the keyname code
	{'`', "TILDE"},
	{KEY_PAUSE, "PAUSE/BREAK"},

	// virtual keys for mouse buttons and joystick buttons
	{KEY_MOUSE1+0,"MOUSE1"},
	{KEY_MOUSE1+1,"MOUSE2"},
	{KEY_MOUSE1+2,"MOUSE3"},
	{KEY_MOUSE1+3,"MOUSE4"},
	{KEY_MOUSE1+4,"MOUSE5"},
	{KEY_MOUSE1+5,"MOUSE6"},
	{KEY_MOUSE1+6,"MOUSE7"},
	{KEY_MOUSE1+7,"MOUSE8"},
	{KEY_MOUSEMOVE+0,"Mouse Up"},
	{KEY_MOUSEMOVE+1,"Mouse Down"},
	{KEY_MOUSEMOVE+2,"Mouse Left"},
	{KEY_MOUSEMOVE+3,"Mouse Right"},
	{KEY_MOUSEWHEELUP, "Wheel Up"},
	{KEY_MOUSEWHEELDOWN, "Wheel Down"},

	{KEY_JOY1+0, "A BUTTON"},
	{KEY_JOY1+1, "B BUTTON"},
	{KEY_JOY1+2, "X BUTTON"},
	{KEY_JOY1+3, "Y BUTTON"},
	{KEY_JOY1+4, "BACK BUTTON"},
	{KEY_JOY1+5, "GUIDE BUTTON"},
	{KEY_JOY1+6, "START BUTTON"},
	{KEY_JOY1+7, "L-STICK CLICK"},
	{KEY_JOY1+8, "R-STICK CLICK"},
	{KEY_JOY1+9, "L BUMPER"},
	{KEY_JOY1+10, "R BUMPER"},
	{KEY_JOY1+11, "D-PAD UP"},
	{KEY_JOY1+12, "D-PAD DOWN"},
	{KEY_JOY1+13, "D-PAD LEFT"},
	{KEY_JOY1+14, "D-PAD RIGHT"},
	{KEY_JOY1+15, "MISC. BUTTON"},
	{KEY_JOY1+16, "PADDLE1 BUTTON"},
	{KEY_JOY1+17, "PADDLE2 BUTTON"},
	{KEY_JOY1+18, "PADDLE3 BUTTON"},
	{KEY_JOY1+19, "PADDLE4 BUTTON"},
	{KEY_JOY1+20, "TOUCHPAD"},

	{KEY_AXIS1+0, "L-STICK LEFT"},
	{KEY_AXIS1+1, "L-STICK RIGHT"},
	{KEY_AXIS1+2, "L-STICK UP"},
	{KEY_AXIS1+3, "L-STICK DOWN"},
	{KEY_AXIS1+4, "R-STICK LEFT"},
	{KEY_AXIS1+5, "R-STICK RIGHT"},
	{KEY_AXIS1+6, "R-STICK UP"},
	{KEY_AXIS1+7, "R-STICK DOWN"},
	{KEY_AXIS1+8, "L TRIGGER"},
	{KEY_AXIS1+9, "R TRIGGER"},
};

static const char *gamecontrolname[num_gamecontrols] =
{
	"nothing", // a key/button mapped to gc_null has no effect
	"aimforward",
	"aimbackward",
	"turnleft",
	"turnright",
	"accelerate",
	"drift",
	"brake",
	"fire",
	"lookback",
	"camreset",
	"camtoggle",
	"spectate",
	"lookup",
	"lookdown",
	"centerview",
	"talkkey",
	"teamtalkkey",
	"scores",
	"console",
	"pause",
	"systemmenu",
	"screenshot",
	"recordgif",
	"viewpoint",
	"custom1",
	"custom2",
	"custom3",
};

#define NUMKEYNAMES (sizeof (keynames)/sizeof (keyname_t))

// If keybind is necessary to navigate menus, it's on this list.
boolean G_KeyBindIsNecessary(INT32 gc)
{
	switch (gc)
	{
		case gc_accelerate:
		case gc_brake:
		case gc_aimforward:
		case gc_aimbackward:
		case gc_turnleft:
		case gc_turnright:
		case gc_systemmenu:
			return true;
		default:
			return false;
	}
	return false;
}

boolean inputcheckdigital = false;

// Returns false if a key is deemed unreachable for this device.
boolean G_KeyIsAvailable(INT32 key, INT32 deviceID)
{
	// Invalid key number.
	if (key <= 0 || key >= NUMINPUTS)
	{
		return false;
	}

	// Valid keyboard-specific virtual key, but no keyboard attached for player.
	if (key < NUMKEYS && deviceID > 0)
	{
		return false;
	}

	// Valid controller-specific virtual key, but no controller attached for player.
	if (key >= KEY_JOY1 && key < JOYINPUTEND && deviceID <= 0)
	{
		return false;
	}

	// Valid mouse-specific virtual key, but no mouse attached for player. TODO HOW TO DETECT ACTIVE MOUSE CONNECTION
	/*
	if (key >= KEY_MOUSE1 && key < MOUSEINPUTEND && ????????)
	{
		return false;
	}
	*/

	// this is ugly as fuck, but it's also better than copy-pasting half of G_PlayerInputAnalog just for turn smoothing
	if (inputcheckdigital && key >= KEY_AXIS1 && key < JOYINPUTEND)
	{
		return false;
	}

	return true;
}

//
// Detach any keys associated to the given game control
// - pass the pointer to the gamecontrol table for the player being edited
void G_ClearControlKeys(INT32 (*setupcontrols)[MAXINPUTMAPPING], INT32 control)
{
	INT32 i;
	for (i = 0; i < MAXINPUTMAPPING; i++)
	{
		setupcontrols[control][i] = KEY_NULL;
	}
}

void G_ClearAllControlKeys(void)
{
	INT32 i, j;
	for (j = 0; j < MAXSPLITSCREENPLAYERS; j++)
	{
		for (i = 0; i < num_gamecontrols; i++)
		{
			G_ClearControlKeys(gamecontrol[j], i);
		}
	}
}

//
// Returns the name of a key (or virtual key for mouse and joy)
// the input value being an keynum
//
const char *G_KeynumToString(INT32 keynum)
{
	static char keynamestr[8];

	UINT32 j;

	// return a string with the ascii char if displayable
	if (keynum > ' ' && keynum <= 'z' && keynum != KEY_CONSOLE)
	{
		keynamestr[0] = (char)keynum;
		keynamestr[1] = '\0';
		return keynamestr;
	}

	// find a description for special keys
	for (j = 0; j < NUMKEYNAMES; j++)
		if (keynames[j].keynum == keynum)
			return keynames[j].name;

	// create a name for unknown keys
	sprintf(keynamestr, "KEY%d", keynum);
	return keynamestr;
}

INT32 G_KeyStringtoNum(const char *keystr)
{
	UINT32 j;

	if (!keystr[0])
		return 0;

	if (!keystr[1] && keystr[0] > ' ' && keystr[0] <= 'z')
		return keystr[0];

	if (!strncmp(keystr, "KEY", 3) && keystr[3] >= '0' && keystr[3] <= '9')
	{
		/* what if we out of range bruh? */
		j = atoi(&keystr[3]);
		if (j < NUMINPUTS)
			return j;
		return 0;
	}

	for (j = 0; j < NUMKEYNAMES; j++)
		if (!stricmp(keynames[j].name, keystr))
			return keynames[j].keynum;

	return 0;
}

void G_DefineDefaultControls(void)
{
	// Keyboard controls
	gamecontroldefault[gc_aimforward ][0] = KEY_UPARROW;
	gamecontroldefault[gc_aimbackward][0] = KEY_DOWNARROW;
	gamecontroldefault[gc_turnleft   ][0] = KEY_LEFTARROW;
	gamecontroldefault[gc_turnright  ][0] = KEY_RIGHTARROW;
	gamecontroldefault[gc_accelerate ][0] = 'a';
	gamecontroldefault[gc_drift      ][0] = 's';
	gamecontroldefault[gc_brake      ][0] = 'd';
	gamecontroldefault[gc_fire       ][0] = KEY_SPACE;
	gamecontroldefault[gc_lookback   ][0] = KEY_LSHIFT;

	gamecontroldefault[gc_pause      ][0] = KEY_PAUSE;
	gamecontroldefault[gc_console    ][0] = KEY_CONSOLE;
	gamecontroldefault[gc_screenshot ][0] = KEY_F8;
	gamecontroldefault[gc_recordgif  ][0] = KEY_F9;
	gamecontroldefault[gc_viewpoint  ][0] = KEY_F12;
	gamecontroldefault[gc_talkkey    ][0] = 't';
	//gamecontroldefault[gc_teamkey    ][0] = 'y';
	gamecontroldefault[gc_scores     ][0] = KEY_TAB;
	gamecontroldefault[gc_spectate   ][0] = '\'';
	gamecontroldefault[gc_lookup     ][0] = KEY_PGUP;
	gamecontroldefault[gc_lookdown   ][0] = KEY_PGDN;
	gamecontroldefault[gc_centerview ][0] = KEY_END;
	gamecontroldefault[gc_camreset   ][0] = KEY_HOME;
	gamecontroldefault[gc_camtoggle  ][0] = KEY_BACKSPACE;

	// Gamepad controls
	gamecontroldefault[gc_aimforward ][1] = KEY_AXIS1+2; // Left Y-
	gamecontroldefault[gc_aimbackward][1] = KEY_AXIS1+3; // Left Y+
	gamecontroldefault[gc_turnleft   ][1] = KEY_AXIS1+0; // Left X-
	gamecontroldefault[gc_turnright  ][1] = KEY_AXIS1+1; // Left X+
	gamecontroldefault[gc_accelerate ][1] = KEY_JOY1+0;  // A
	gamecontroldefault[gc_drift      ][1] = KEY_JOY1+10; // RB
	gamecontroldefault[gc_drift      ][2] = KEY_AXIS1+9; // RT
	gamecontroldefault[gc_brake      ][1] = KEY_JOY1+1;  // B
	gamecontroldefault[gc_fire       ][1] = KEY_JOY1+9;  // LB
	gamecontroldefault[gc_fire       ][2] = KEY_AXIS1+8; // LT
	gamecontroldefault[gc_lookback   ][1] = KEY_JOY1+2;  // X

	gamecontroldefault[gc_pause      ][1] = KEY_JOY1+4;  // Back
	gamecontroldefault[gc_viewpoint  ][1] = KEY_JOY1+3;  // Y
	gamecontroldefault[gc_systemmenu ][1] = KEY_JOY1+6;  // Start
	gamecontroldefault[gc_talkkey    ][1] = KEY_HAT1+1;  // D-Pad Down
	gamecontroldefault[gc_scores     ][1] = KEY_HAT1+0;  // D-Pad Up
}

void G_CopyControls(INT32 (*setupcontrols)[MAXINPUTMAPPING], INT32 (*fromcontrols)[MAXINPUTMAPPING], const INT32 *gclist, INT32 gclen)
{
	INT32 i, j, gc;

	for (i = 0; i < (gclist && gclen ? gclen : num_gamecontrols); i++)
	{
		gc = (gclist && gclen) ? gclist[i] : i;

		for (j = 0; j < MAXINPUTMAPPING; j++)
		{
			setupcontrols[gc][j] = fromcontrols[gc][j];
		}
	}
}

void G_SaveKeySetting(FILE *f, INT32 (*fromcontrolsa)[MAXINPUTMAPPING], INT32 (*fromcontrolsb)[MAXINPUTMAPPING], INT32 (*fromcontrolsc)[MAXINPUTMAPPING], INT32 (*fromcontrolsd)[MAXINPUTMAPPING])
{
	INT32 i, j;

	// TODO: would be nice to get rid of this code duplication
	for (i = 1; i < num_gamecontrols; i++)
	{
		fprintf(f, "setcontrol \"%s\" \"%s\"", gamecontrolname[i], G_KeynumToString(fromcontrolsa[i][0]));

		for (j = 1; j < MAXINPUTMAPPING+1; j++)
		{
			if (j < MAXINPUTMAPPING && fromcontrolsa[i][j])
			{
				fprintf(f, " \"%s\"", G_KeynumToString(fromcontrolsa[i][j]));
			}
			else
			{
				fprintf(f, "\n");
				break;
			}
		}
	}

	for (i = 1; i < num_gamecontrols; i++)
	{
		fprintf(f, "setcontrol2 \"%s\" \"%s\"", gamecontrolname[i],
			G_KeynumToString(fromcontrolsb[i][0]));

		for (j = 1; j < MAXINPUTMAPPING+1; j++)
		{
			if (j < MAXINPUTMAPPING && fromcontrolsb[i][j])
			{
				fprintf(f, " \"%s\"", G_KeynumToString(fromcontrolsb[i][j]));
			}
			else
			{
				fprintf(f, "\n");
				break;
			}
		}
	}

	for (i = 1; i < num_gamecontrols; i++)
	{
		fprintf(f, "setcontrol3 \"%s\" \"%s\"", gamecontrolname[i],
			G_KeynumToString(fromcontrolsc[i][0]));

		for (j = 1; j < MAXINPUTMAPPING+1; j++)
		{
			if (j < MAXINPUTMAPPING && fromcontrolsc[i][j])
			{
				fprintf(f, " \"%s\"", G_KeynumToString(fromcontrolsc[i][j]));
			}
			else
			{
				fprintf(f, "\n");
				break;
			}
		}
	}

	for (i = 1; i < num_gamecontrols; i++)
	{
		fprintf(f, "setcontrol4 \"%s\" \"%s\"", gamecontrolname[i],
			G_KeynumToString(fromcontrolsd[i][0]));

		for (j = 1; j < MAXINPUTMAPPING+1; j++)
		{
			if (j < MAXINPUTMAPPING && fromcontrolsd[i][j])
			{
				fprintf(f, " \"%s\"", G_KeynumToString(fromcontrolsd[i][j]));
			}
			else
			{
				fprintf(f, "\n");
				break;
			}
		}
	}
}

INT32 G_CheckDoubleUsage(INT32 keynum, INT32 playernum, boolean modify)
{
	INT32 result = gc_null;

	if (cv_controlperkey.value == 1)
	{
		INT32 i, j;
		for (i = 0; i < num_gamecontrols; i++)
		{
			for (j = 0; j < MAXINPUTMAPPING; j++)
			{
				if (gamecontrol[playernum][i][j] == keynum)
				{
					result = i;
					if (modify)
					{
						gamecontrol[playernum][i][j] = KEY_NULL;
					}
				}

				if (result && !modify)
					return result;
			}
		}
	}

	return result;
}

static void setcontrol(UINT8 player)
{
	INT32 numctrl;
	const char *namectrl;
	INT32 keynum;
	INT32 inputMap = 0;
	INT32 i;

	namectrl = COM_Argv(1);

	for (numctrl = 0;
		numctrl < num_gamecontrols && stricmp(namectrl, gamecontrolname[numctrl]);
		numctrl++)
	{ ; }

	if (numctrl == num_gamecontrols)
	{
		CONS_Printf(M_GetText("Control '%s' unknown\n"), namectrl);
		return;
	}

	for (i = 0; i < MAXINPUTMAPPING; i++)
	{
		keynum = G_KeyStringtoNum(COM_Argv(inputMap + 2));

		if (keynum >= 0)
		{
			(void)G_CheckDoubleUsage(keynum, player, true);

			// if keynum was rejected, try it again with the next key.
			while (keynum == 0)
			{
				inputMap++;
				if (inputMap >= MAXINPUTMAPPING)
				{
					break;
				}

				keynum = G_KeyStringtoNum(COM_Argv(inputMap + 2));

				if (keynum >= 0)
				{
					(void)G_CheckDoubleUsage(keynum, player, true);
				}
			}
		}

		if (keynum >= 0)
		{
			gamecontrol[player][numctrl][i] = keynum;
		}

		inputMap++;
		if (inputMap >= MAXINPUTMAPPING)
		{
			break;
		}
	}
}

void Command_Setcontrol_f(void)
{
	INT32 na;

	na = (INT32)COM_Argc();

	if (na < 3 || na > MAXINPUTMAPPING+2)
	{
		CONS_Printf(M_GetText("setcontrol <controlname> <keyname> [<keyname>] [<keyname>] [<keyname>]: set controls for player 1\n"));
		return;
	}

	setcontrol(0);
}

void Command_Setcontrol2_f(void)
{
	INT32 na;

	na = (INT32)COM_Argc();

	if (na < 3 || na > MAXINPUTMAPPING+2)
	{
		CONS_Printf(M_GetText("setcontrol2 <controlname> <keyname> [<keyname>] [<keyname>] [<keyname>]: set controls for player 2\n"));
		return;
	}

	setcontrol(1);
}

void Command_Setcontrol3_f(void)
{
	INT32 na;

	na = (INT32)COM_Argc();

	if (na < 3 || na > MAXINPUTMAPPING+2)
	{
		CONS_Printf(M_GetText("setcontrol3 <controlname> <keyname> [<keyname>] [<keyname>] [<keyname>]: set controls for player 3\n"));
		return;
	}

	setcontrol(2);
}

void Command_Setcontrol4_f(void)
{
	INT32 na;

	na = (INT32)COM_Argc();

	if (na < 3 || na > MAXINPUTMAPPING+2)
	{
		CONS_Printf(M_GetText("setcontrol4 <controlname> <keyname> [<keyname>] [<keyname>] [<keyname>]: set controls for player 4\n"));
		return;
	}

	setcontrol(3);
}
