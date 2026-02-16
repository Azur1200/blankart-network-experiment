// BLANKART
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
#include "k_items.h"
#include "keys.h"
#include "hu_stuff.h" // need HUFONT start & end
#include "d_net.h"
#include "console.h"
#include "i_gamepad.h" // JOYAXISRANGE
#include "m_menu.h" // menustack
#include "i_system.h"
#include "g_game.h"
#include "v_video.h"
#include "p_local.h"
#include "k_kart.h"

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

consvar_t cv_litesteer[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("litesteer",  "Off", CV_SAVE, CV_OnOff, NULL),
	CVAR_INIT ("litesteer2", "Off", CV_SAVE, CV_OnOff, NULL),
	CVAR_INIT ("litesteer3", "Off", CV_SAVE, CV_OnOff, NULL),
	CVAR_INIT ("litesteer4", "Off", CV_SAVE, CV_OnOff, NULL)
};

consvar_t cv_turnsmooth[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("turnsmoothing",  "Off", CV_SAVE, turnsmooth_cons_t, NULL),
	CVAR_INIT ("turnsmoothing2", "Off", CV_SAVE, turnsmooth_cons_t, NULL),
	CVAR_INIT ("turnsmoothing3", "Off", CV_SAVE, turnsmooth_cons_t, NULL),
	CVAR_INIT ("turnsmoothing4", "Off", CV_SAVE, turnsmooth_cons_t, NULL)
};

static void G_ResetPlayerControllerRumble(INT32 player);
static void rumble_off_handle(void);
static void rumble_off_handle2(void);
static void rumble_off_handle3(void);
static void rumble_off_handle4(void);

static void G_ResetPlayerControllerIndicatorColor(INT32 player);
static void led_off_handle(void);
static void led_off_handle2(void);
static void led_off_handle3(void);
static void led_off_handle4(void);

consvar_t cv_controllerrumble[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("rumble", "On", CV_SAVE, CV_OnOff, rumble_off_handle),
	CVAR_INIT ("rumble2", "On", CV_SAVE, CV_OnOff, rumble_off_handle2),
	CVAR_INIT ("rumble3", "On", CV_SAVE, CV_OnOff, rumble_off_handle3),
	CVAR_INIT ("rumble4", "On", CV_SAVE, CV_OnOff, rumble_off_handle4)
};

static CV_PossibleValue_t rumblestrength_cons_t[] = {{FRACUNIT/4, "MIN"}, {FRACUNIT*4, "MAX"}, {0, NULL}};

consvar_t cv_rumblestrength[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("rumblestrength",  "1.0", CV_SAVE|CV_FLOAT, rumblestrength_cons_t, NULL),
	CVAR_INIT ("rumblestrength2", "1.0", CV_SAVE|CV_FLOAT, rumblestrength_cons_t, NULL),
	CVAR_INIT ("rumblestrength3", "1.0", CV_SAVE|CV_FLOAT, rumblestrength_cons_t, NULL),
	CVAR_INIT ("rumblestrength4", "1.0", CV_SAVE|CV_FLOAT, rumblestrength_cons_t, NULL)
};

static CV_PossibleValue_t controllerled_cons_t[] = {{0, "Off"}, {1, "Skincolor"}, {2, "Mobjcolor"}, {0, NULL}};
consvar_t cv_controllerled[MAXSPLITSCREENPLAYERS] = {
	CVAR_INIT ("gamepadled",  "Skincolor", CV_SAVE|CV_CALL|CV_NOINIT, controllerled_cons_t, led_off_handle),
	CVAR_INIT ("gamepadled2", "Skincolor", CV_SAVE|CV_CALL|CV_NOINIT, controllerled_cons_t, led_off_handle2),
	CVAR_INIT ("gamepadled3", "Skincolor", CV_SAVE|CV_CALL|CV_NOINIT, controllerled_cons_t, led_off_handle3),
	CVAR_INIT ("gamepadled4", "Skincolor", CV_SAVE|CV_CALL|CV_NOINIT, controllerled_cons_t, led_off_handle4)
};

static void rumble_off_handle(void)
{
	if (cv_controllerrumble[0].value == 0)
		G_ResetPlayerControllerRumble(0);
}

static void rumble_off_handle2(void)
{
	if (cv_controllerrumble[1].value == 0)
		G_ResetPlayerControllerRumble(1);
}

static void rumble_off_handle3(void)
{
	if (cv_controllerrumble[2].value == 0)
		G_ResetPlayerControllerRumble(2);
}

static void rumble_off_handle4(void)
{
	if (cv_controllerrumble[3].value == 0)
		G_ResetPlayerControllerRumble(3);
}

static void led_off_handle(void)
{
	G_ResetPlayerControllerIndicatorColor(0);
}

static void led_off_handle2(void)
{
	G_ResetPlayerControllerIndicatorColor(1);
}

static void led_off_handle3(void)
{
	G_ResetPlayerControllerIndicatorColor(2);
}

static void led_off_handle4(void)
{
	G_ResetPlayerControllerIndicatorColor(3);
}

// current state of the keys
// JOYAXISRANGE for fully pressed, 0 for not pressed
inputpoll_t gamekeydown = {};
inputpoll_t gamekeydown_msec = {};

// several key codes (or virtual key) per game control
INT32 gamecontrol[MAXSPLITSCREENPLAYERS][num_gamecontrols][MAXINPUTMAPPING] = {};

INT32 gamecontroldefault[num_gamecontrols][MAXINPUTMAPPING] = {
	[gc_aimforward ] = {KEY_UPARROW,    KEY_AXIS1+2             }, // Left Y-
	[gc_aimbackward] = {KEY_DOWNARROW,  KEY_AXIS1+3             }, // Left Y+
	[gc_turnleft   ] = {KEY_LEFTARROW,  KEY_AXIS1+0             }, // Left X-
	[gc_turnright  ] = {KEY_RIGHTARROW, KEY_AXIS1+1             }, // Left X+
	[gc_accelerate ] = {'a',            KEY_JOY1+0              }, // East
	[gc_drift      ] = {'s',            KEY_JOY1+10, KEY_AXIS1+9}, // RB, RT
	[gc_brake      ] = {'d',            KEY_JOY1+1              }, // South
	[gc_fire       ] = {KEY_SPACE,      KEY_JOY1+9,  KEY_AXIS1+8}, // LB, LT
	[gc_lookback   ] = {KEY_LSHIFT,     KEY_JOY1+2              }, // North
	[gc_horncode   ] = {'r',            KEY_JOY1+8              }, // R-Stick Click

	[gc_pause      ] = {KEY_PAUSE,      KEY_JOY1+4 }, // Back
	[gc_systemmenu ] = {                KEY_JOY1+6 }, // Start
	[gc_console    ] = {KEY_CONSOLE                },
	[gc_screenshot ] = {KEY_F8                     },
	[gc_recordgif  ] = {KEY_F9                     },
	[gc_viewpoint  ] = {KEY_F12,        KEY_JOY1+3 }, // West
	[gc_talkkey    ] = {'t',            KEY_HAT1+1 }, // D-Pad Down
	//[gc_teamkey    ] = {'y'                        },
	[gc_scores     ] = {KEY_TAB,        KEY_HAT1+0 }, // D-Pad Up
	[gc_spectate   ] = {'\''                       },
	[gc_lookup     ] = {KEY_PGUP,       KEY_AXIS1+6}, // Right Y-
	[gc_lookdown   ] = {KEY_PGDN,       KEY_AXIS1+7}, // Right Y+
	[gc_centerview ] = {KEY_END                    },
	[gc_camreset   ] = {KEY_HOME                   },
	[gc_camtoggle  ] = {KEY_BACKSPACE              },
};

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
		if (deviceID == I_GetControllerIDForPlayer(i) + 1)
		{
			return i;
		}
	}

	return -1;
}

// converts ev_joystick axis to its corresponding key
INT32 G_AxisToKey(event_t *ev)
{
	return KEY_AXIS1 + (ev->data1 >= JOYANALOGS
						? ev->data1 + JOYANALOGS // triggers
						: ev->data1*2 + (ev->data2 >= 0)); // analog sticks
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

	INT32 device = ev->device;

	// not a keyboard?
	// try to see if its a controller in use
	if (device > KEYBOARD_MOUSE_DEVICE)
		device = I_GetControllerSlotfromID(device-1);

	// invalid device
	if (device < KEYBOARD_MOUSE_DEVICE || device >= MAXDEVICES)
		return;

	switch (ev->type)
	{
		case ev_keydown:
			if (ev->data1 < NUMINPUTS)
			{
				gamekeydown[device][ev->data1] = JOYAXISRANGE;
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
				gamekeydown[device][ev->data1] = 0;
			}
#ifdef PARANOIA
			else
			{
				CONS_Debug(DBG_GAMELOGIC, "Bad upkey input %d\n", ev->data1);
			}
#endif
			break;

		case ev_mouse: // buttons are virtual keys
			if (menustack[0])
			{
				break;
			}

			// X axis
			if (ev->data2 < 0)
			{
				// Left
				gamekeydown[device][KEY_MOUSEMOVE + 2] = abs(ev->data2);
				gamekeydown[device][KEY_MOUSEMOVE + 3] = 0;
			}
			else
			{
				// Right
				gamekeydown[device][KEY_MOUSEMOVE + 2] = 0;
				gamekeydown[device][KEY_MOUSEMOVE + 3] = abs(ev->data2);
			}

			// Y axis
			if (ev->data3 < 0)
			{
				// Up
				gamekeydown[device][KEY_MOUSEMOVE] = abs(ev->data3);
				gamekeydown[device][KEY_MOUSEMOVE + 1] = 0;
			}
			else
			{
				// Down
				gamekeydown[device][KEY_MOUSEMOVE] = 0;
				gamekeydown[device][KEY_MOUSEMOVE + 1] = abs(ev->data3);
			}
			break;

		case ev_joystick: // buttons are virtual keys
			if (menustack[0])
			{
				break;
			}

			if (ev->data1 >= JOYAXISES)
			{
#ifdef PARANOIA
				CONS_Debug(DBG_GAMELOGIC, "Bad joystick axis event %d\n", ev->data1);
#endif
				break;
			}

			// tf is this for?
			if (G_GetDevicePlayer(ev->device) == 0)
			{
				if (CON_Ready() || chat_on)
					break;
			}

			i = G_AxisToKey(ev);
			if (ev->data1 < JOYANALOGS)
			{
				// analog stick axes have a negative and positive keydown
				i -= ev->data2 >= 0;
				gamekeydown[device][i++] = max(0, -ev->data2);
			}
			gamekeydown[device][i] = max(0, ev->data2);

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
	{KEY_JOY1+15, "MISC. BUTTON 1"},
	{KEY_JOY1+16, "PADDLE BUTTON 1"},
	{KEY_JOY1+17, "PADDLE BUTTON 2"},
	{KEY_JOY1+18, "PADDLE BUTTON 3"},
	{KEY_JOY1+19, "PADDLE BUTTON 4"},
	{KEY_JOY1+20, "TOUCHPAD"},
	{KEY_JOY1+21, "MISC. BUTTON 2"},
	{KEY_JOY1+22, "MISC. BUTTON 3"},
	{KEY_JOY1+23, "MISC. BUTTON 4"},
	{KEY_JOY1+24, "MISC. BUTTON 5"},
	{KEY_JOY1+25, "MISC. BUTTON 6"},

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

	{KEY_ACCELEROMETER1+0, "TILT -X"},
	{KEY_ACCELEROMETER1+1, "TILT +X"},
	{KEY_ACCELEROMETER1+2, "TILT -Y"},
	{KEY_ACCELEROMETER1+3, "TILT +Y"},
	{KEY_ACCELEROMETER1+4, "TILT -Z"},
	{KEY_ACCELEROMETER1+5, "TILT +Z"},

	{KEY_GYROSCOPE1+0, "ROTATE -X"},
	{KEY_GYROSCOPE1+1, "ROTATE +X"},
	{KEY_GYROSCOPE1+2, "ROTATE -Y"},
	{KEY_GYROSCOPE1+3, "ROTATE +Y"},
	{KEY_GYROSCOPE1+4, "ROTATE -Z"},
	{KEY_GYROSCOPE1+5, "ROTATE +Z"},
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
	"respawn",
	"director",
	"horncode",
};

#define NUMKEYNAMES (sizeof (keynames)/sizeof (keyname_t))

skincolornum_t G_GetSkinColorForController(INT32 playernum)
{
	I_Assert(playernum >= 0 && playernum < MAXSPLITSCREENPLAYERS);

	if (gamestate == GS_LEVEL)
	{
		const player_t *player = &players[displayplayers[playernum]];

		if (player)
		{
			// make rgb rainbow vomit when invul or flash blue when grow
			if ((cv_controllerled[playernum].value == 2) && player->mo && player->mo->color)
				return (skincolornum_t)player->mo->color;

			// take actual player skincolour when ingame
			if (player->skincolor)
				return (skincolornum_t)player->skincolor;
		}
	}

	// otherwise just fallback to whatever the cvar is
	return (skincolornum_t)cv_playercolor[playernum].value;
}

// Sets the Indicator LED on supported gamepads to a desired skincolor
// pass SKINCOLOR_NONE/0 to set the players skin color
void G_SetPlayerControllerIndicatorColor(INT32 playernum, UINT16 color)
{
	skincolornum_t skincolor;
	byteColor_t byte_color;

	I_Assert(playernum >= 0 && playernum < MAXSPLITSCREENPLAYERS);

	if (cv_controllerled[playernum].value == 0)
	{
		return;
	}

	// so we can override this
	skincolor = color ? color : G_GetSkinColorForController(playernum);
	byte_color = V_GetColor(skincolors[skincolor].ramp[8]).s;

	I_SetControllerIndicatorColor(playernum, byte_color.red, byte_color.green, byte_color.blue);
}

static void G_ResetPlayerControllerIndicatorColor(INT32 playernum)
{
	I_Assert(playernum >= 0 && playernum < MAXSPLITSCREENPLAYERS);

	if (cv_controllerled[playernum].value == 0)
	{
		I_SetControllerIndicatorColor(playernum, 0, 0, 255);
	}
	else
	{
		G_SetPlayerControllerIndicatorColor(playernum, 0);
	}
}

static skincolornum_t curcolor[MAXSPLITSCREENPLAYERS] = {};

void G_ControllerLEDTick(void)
{
	UINT8 i;
	static skincolornum_t newcolor = SKINCOLOR_NONE;
	const INT32 numcontrollers = I_NumControllers();

	if (numcontrollers == 0)
	{
		return;
	}

	for (i = 0; i <= splitscreen; i++)
	{
		if (!cv_usecontroller[i].value || !cv_controllerled[i].value)
			continue;

		// Players controller does not have an addressable rbg light, bummer!
		if (!I_ControllerSupportsIndicatorColor(i))
			continue;

		newcolor = G_GetSkinColorForController(i);

		if (curcolor[i] == newcolor) // dont update if same colour
			continue;

		G_SetPlayerControllerIndicatorColor(i, newcolor);
		curcolor[i] = newcolor;
	}
}

void G_ResetControllerLED(void)
{
	memset(curcolor, 0, sizeof(curcolor));
}

static void G_ResetPlayerControllerRumble(INT32 playernum)
{
	I_Assert(playernum >= 0 && playernum < MAXSPLITSCREENPLAYERS);

	I_ControllerRumble(playernum, 0, 0, 0);
}

void G_ResetAllControllerRumbles(void)
{
	for (int i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		I_ControllerRumble(i, 0, 0, 0);
	}
}

void G_PlayerControllerRumble(INT32 playernum, UINT16 low_strength, UINT16 high_strength, UINT32 duration)
{
	I_Assert(playernum >= 0 && playernum < MAXSPLITSCREENPLAYERS);

	if (cv_controllerrumble[playernum].value == 0)
	{
		return;
	}

	// doesent need to be super precise
	// but ensure this is within range (0-65535)
	// placed here so it applies to lua aswell
	low_strength = (UINT16)min(FixedMul(low_strength, cv_rumblestrength[playernum].value), UINT16_MAX);
	high_strength = (UINT16)min(FixedMul(high_strength, cv_rumblestrength[playernum].value), UINT16_MAX);

	I_ControllerRumble(playernum, low_strength, high_strength, duration);
}

// rumble strengths
enum
{
	RUMBLE_VERYSTRONG = FRACUNIT / 4,   // 16384
	RUMBLE_STRONG     = FRACUNIT / 8,   // 8192
	RUMBLE_MODERATE   = FRACUNIT / 64,  // 1024
	RUMBLE_WEAK       = FRACUNIT / 128, // 512
	RUMBLE_VERYWEAK   = FRACUNIT / 256, // 256
};

// Controller rumble!
// this keeps track of a bunch of things
// and makes your controller rumble accordingly
void G_ControllerRumbleTick(void)
{
	UINT8 i;
	const INT32 numcontrollers = I_NumControllers();

	if (dedicated || numcontrollers == 0 || gamestate != GS_LEVEL)
	{
		return;
	}

	for (i = 0; i <= splitscreen; i++)
	{
		if (!cv_usecontroller[i].value || !cv_controllerrumble[i].value)
		{
			continue;
		}

		// Players controller does not support rumble, bummer!
		if (!I_ControllerSupportsRumble(i))
			continue;

		if (camera[i].freecam)
		{
			continue;
		}

		UINT16 low = 0, high = 0;
		// for how long the action should rumble, in ms
		UINT16 lenght = 57; // 57ms is a short pulse, matches super well with this updating once per tic
		const player_t *player = &players[g_localplayers[i]];

		// allow lua to do some crap for spectators
		if (player->spectator || !player->mo)
		{
			continue;
		}

		// reset the rumble if you exit or are ded lel
		if (player->exiting ||
			player->playerstate == PST_DEAD ||
			player->respawn > 1)
		{
			G_PlayerControllerRumble(i, low, high, 0);
			continue;
		}

		if (player->spinouttimer)
		{
			//low = high = FRACUNIT / 6;
			low = high = FixedMul((RUMBLE_VERYSTRONG), (FixedDiv(player->spinouttimer, (3*TICRATE / 2)))); // try do some some kinda fadeout, 3*TICRATE / 2 is the "default" spinout time
		}
		else if (player->flamestore && !player->offroad)
		{
			high = (player->flamestore * RUMBLE_MODERATE) / FLAMESTOREMAX;
			lenght = 32;
		}
		else if (player->sneakertimer > (sneakertime-(TICRATE/2)) || player->bubbleboost > (BUBBLEBOOSTTIME-(TICRATE/2)))
		{
			low = high = RUMBLE_STRONG;
		}
		else if ((player->offroad)
			&& player->speed != 0
			&& P_IsObjectOnGround(player->mo))
		{
			// weaken this depending on if you got hyu or invinc
			if (player->hyudorotimer)
			{
				high = RUMBLE_WEAK;
			}
			else if (player->flameoverheat)
			{
				low = RUMBLE_MODERATE;
			}
			else if (player->invincibilitytimer)
			{
				high = RUMBLE_MODERATE;
			}
			else
			{
				low = high = RUMBLE_MODERATE;
			}
		}
		else if ((player->bananadrag > TICRATE)
			&& player->speed != 0
			&& P_IsObjectOnGround(player->mo))
		{
			if (leveltime & 1) // this is actually funny lel
				high = RUMBLE_MODERATE;
		}

		if (player->pflags & PF_BRAKEDRIFT)
		{
			high = CLAMP((high + RUMBLE_VERYWEAK), 0, UINT16_MAX);
		}

		// pulse when gettin new driftlevel
		if (/*player->driftcharge // gets reset within K_KartDrift
			&& */player->driftlevel)
		{
			high = CLAMP((high + RUMBLE_VERYWEAK), 0, UINT16_MAX);

			// rumble during charging drifts
			// when you reach new driftlevel
			if (player->driftlevel < 20)
			{
				if (player->driftlevel == 2)
					lenght = 114;
				else if (player->driftlevel == 3)
					lenght = 144;
				else if (player->driftlevel == 4)
					lenght = 174;
			}

			// drift release
			// adjust those if you want
			if (player->driftlevel >= 20)
			{
				// give some oompfh on release
				low = CLAMP((low + RUMBLE_VERYWEAK), 0, UINT16_MAX);

				if (player->driftlevel == 20)
					lenght = 114;
				else if (player->driftlevel == 30)
					lenght = 144;
				else if (player->driftlevel == 40)
					lenght = 174;
			}
		}

		// pulse when using rings
		// let this come last
		if ((player->cmd.buttons & BT_ATTACK)
			&& (player->itemflags & IF_USERINGS)
			&& (player->rings > 0
			&& player->rings > player->rings-1
			&& player->rings < player->rings+1)
		)
		{
			low = CLAMP((low + RUMBLE_VERYWEAK), 0, UINT16_MAX);
		}

		// hack alert! i just dont want this thing constantly resetting the rumble lol
		if (low == 0 && high == 0)
		{
			continue;
		}

		G_PlayerControllerRumble(i, low, high, lenght);
	}
}

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

// Returns false if a key is deemed unreachable for this device.
boolean G_KeyIsAvailable(INT32 key, INT32 deviceID, boolean digital)
{
	// Invalid key number.
	if (key <= 0 || key >= NUMINPUTS)
	{
		return false;
	}

	// analog input only
	if (digital && key >= KEY_AXIS1 && key < JOYINPUTEND)
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
		if (fasticmp(keynames[j].name, keystr))
			return keynames[j].keynum;

	return 0;
}

void G_ResetControls(UINT8 p)
{
	INT32 i, j, k;

	for (i = 0; i < num_gamecontrols; i++)
	{
		for (j = k = 0; j < MAXINPUTMAPPING; j++)
		{
			INT32 key = gamecontroldefault[i][j];
			gamecontrol[p][i][j] = 0;
			if (p == 0 || key >= NUMKEYS) // keyboard is for P1 only
				gamecontrol[p][i][k++] = key;
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
		numctrl < num_gamecontrols && gamecontrolname[numctrl] && !fasticmp(namectrl, gamecontrolname[numctrl]);
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

	// clear out binds that weren't set
	while (++i < MAXINPUTMAPPING)
		gamecontrol[player][numctrl][i] = 0;
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
