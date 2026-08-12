// Emacs style mode select   -*- C++ -*-
// BLANKART
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Portions Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2014-2020 by Sonic Team Junior.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//-----------------------------------------------------------------------------
/// \file i_gamepad.c
/// \brief Blankart Gamepad Handling

#ifdef HAVE_SDL

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_video.h>

#include "../doomdef.h"
#include "../d_main.h"
#include "../d_netcmd.h"
#include "../m_argv.h"
#include "../i_gamepad.h"
#include "../i_system.h"

#include "sdlmain.h"

// information on the Controller a player uses
controllerinfo_t controllerInfo[MAXSPLITSCREENPLAYERS] = {};
static int32_t numcontrollers = 0;

//
// I_ControllerReset
//
static void I_ControllerReset(controllerinfo_t *controller)
{
	if (controller->dev)
	{
		SDL_CloseGamepad(controller->dev);
	}

	controller->dev  = NULL;
	//controller->joydev  = NULL;
	controller->id   = -1;
	//controller->name = NULL;
	controller->hasrumble = false;
	controller->hasmonoled = false;
	controller->hasrgbled = false;
	controller->hasplayerled = false;
	controller->hasaccelerometer = false;
	controller->hasgyro = false;
	controller->flipbuttons = false;
}

//
// I_ShutdownController
//
void I_ShutdownController(uint8_t index)
{
	int32_t i;
	event_t event;

	event.device = index+1;
	event.type = ev_keyup;
	event.data2 = 0;
	event.data3 = 0;

	// emulate the up of all joystick buttons
	for (i = 0; i < JOYBUTTONS; i++)
	{
		event.data1 = KEY_JOY1+i;
		D_PostEvent(&event);
	}

	// reset joystick position
	event.type = ev_joystick;
	for (i = 0; i < JOYAXISES; i++)
	{
		event.data1 = i;
		D_PostEvent(&event);
	}

	// reset sensors
	event.type = ev_accelerometer;
	for (i = 0; i < 3; i++)
	{
		event.data1 = 0;
		event.data2 = FLOAT_TO_FIXED(9.80665f);
		event.data3 = 0;
		D_PostEvent(&event);
	}
	event.type = ev_gyroscope;
	for (i = 0; i < 3; i++)
	{
		event.data1 = 0;
		event.data2 = 0;
		event.data3 = 0;
		D_PostEvent(&event);
	}

	I_ControllerReset(&controllerInfo[index]);
}

void I_ShutdownControllerSystem(void)
{
	uint8_t i;

	// Yes, the name is misleading: these send neutral events to
	// clean up the unplugged joystick's input
	// Note these methods are internal to this file, not called elsewhere.

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
		I_ShutdownController(i);

	// reset this just in case
	numcontrollers = 0;

	if (SDL_WasInit(SDL_INIT_GAMEPAD) == SDL_INIT_GAMEPAD)
	{
		CONS_Printf("Shutting down gamecontroller system\n");
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
		I_OutputMsg("I_Joystick: SDL's Game Controller system has been shutdown\n");
	}

	if (SDL_WasInit(SDL_INIT_JOYSTICK) == SDL_INIT_JOYSTICK)
	{
		CONS_Printf("Shutting down joy system\n");
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
		I_OutputMsg("I_Joystick: SDL's Joystick system has been shutdown\n");
	}
}

//
// I_InitController
//
void I_InitController(uint8_t playernum)
{
	SDL_Gamepad *newcontroller = NULL;
	SDL_JoystickID *controller_list = NULL;
	int num_joy = 0;

	if (M_CheckParm("-nojoy"))
		return;

	{
		char dbpath[1024];
		sprintf(dbpath, "%s" PATHSEP "gamecontrollerdb.txt", srb2path);
		SDL_AddGamepadMappingsFromFile(dbpath);
	}

	{
		char dbpath[1024];
		sprintf(dbpath, "%s" PATHSEP "gamecontrollerdb_user.txt", srb2home);
		SDL_AddGamepadMappingsFromFile(dbpath);
	}

	if (M_CheckParm("-noxinput"))
		SDL_SetHintWithPriority("SDL_XINPUT_ENABLED", "0", SDL_HINT_OVERRIDE);

	if (M_CheckParm("-nohidapi"))
		SDL_SetHintWithPriority("SDL_JOYSTICK_HIDAPI", "0", SDL_HINT_OVERRIDE);

	if (M_CheckParm("-nolibusb"))
		SDL_SetHintWithPriority("SDL_HIDAPI_LIBUSB", "0", SDL_HINT_OVERRIDE);
	
	// need to figure out how to get it to recognize Motion Plus
	// if (!M_CheckParm("-nohidapiwii"))
	// 	SDL_SetHintWithPriority("SDL_JOYSTICK_HIDAPI_WII", "1", SDL_HINT_OVERRIDE);

	if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0)
	{
		CONS_Printf("I_InitController()...\n");

		if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK))
		{
			CONS_Printf(M_GetText("Couldn't initialize joystick: %s\n"), SDL_GetError());
			return;
		}
	}

	if (SDL_WasInit(SDL_INIT_GAMEPAD) == 0)
	{
		if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
		{
			CONS_Printf(M_GetText("Couldn't initialize gamepads: %s\n"), SDL_GetError());
			return;
		}
	}

	I_ControllerReset(&controllerInfo[playernum]);

	// dont need to do any of this if we dont want to use a controller
	if (cv_usecontroller[playernum].value <= 0)
		return;

	controller_list = SDL_GetGamepads(&num_joy);

	if (!controller_list)
	{
		CONS_Debug(DBG_GAMELOGIC, "Failed to fetch Controller list: %s\n", SDL_GetError());
		return;
	}

	if (cv_usecontroller[playernum].value > num_joy)
	{
		SDL_free(controller_list);
		return;
	}

	SDL_JoystickID joystick_id = controller_list[cv_usecontroller[playernum].value - 1];
	if (joystick_id == 0)
	{
		CONS_Debug(DBG_GAMELOGIC, "Failed to get Controller instance id for Player %d: %s\n", playernum+1, SDL_GetError());
		SDL_free(controller_list);
		return;
	}

	// dont need this anymore
	SDL_free(controller_list);

	newcontroller = SDL_OpenGamepad(joystick_id);

	if (!newcontroller)
	{
		CONS_Debug(DBG_GAMELOGIC, "Failed to open Controller with id %d for Player %d: %s\n", joystick_id, playernum+1, SDL_GetError());
		return;
	}

	// fill in our controller info
	controllerinfo_t *controller = &controllerInfo[playernum];
	controller->dev  = newcontroller;
	//controller->joydev = SDL_GetGamepadJoystick(newcontroller);
	controller->id   = joystick_id;
	//controller->name = SDL_GetGamepadNameForID(joystick_id);
	SDL_PropertiesID controllerproperties = SDL_GetGamepadProperties(newcontroller);
	controller->hasrumble = SDL_GetBooleanProperty(controllerproperties, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
	controller->hasrgbled = SDL_GetBooleanProperty(controllerproperties, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false);
	controller->hasmonoled = SDL_GetBooleanProperty(controllerproperties, SDL_PROP_GAMEPAD_CAP_MONO_LED_BOOLEAN, false) && (!controller->hasrgbled);
	controller->hasplayerled = SDL_GetBooleanProperty(controllerproperties, SDL_PROP_GAMEPAD_CAP_PLAYER_LED_BOOLEAN, false);

	controller->hasaccelerometer = SDL_GamepadHasSensor(newcontroller, SDL_SENSOR_ACCEL);
	controller->hasgyro = SDL_GamepadHasSensor(newcontroller, SDL_SENSOR_GYRO);

	controller->flipbuttons = SDL_GetGamepadButtonLabel(newcontroller, SDL_GAMEPAD_BUTTON_SOUTH) == SDL_GAMEPAD_BUTTON_LABEL_B;
	controller->flipgc = SDL_GetGamepadButtonLabel(newcontroller, SDL_GAMEPAD_BUTTON_WEST) == SDL_GAMEPAD_BUTTON_LABEL_B
	                  && SDL_GetGamepadButtonLabel(newcontroller, SDL_GAMEPAD_BUTTON_EAST) == SDL_GAMEPAD_BUTTON_LABEL_X;
	
	if (controller->hasaccelerometer)
	{
		CONS_Debug(DBG_GAMELOGIC, "Enabling accelerometer for controller %d\n", joystick_id);
		SDL_SetGamepadSensorEnabled(controller->dev, SDL_SENSOR_ACCEL, true);
	}

	if (controller->hasgyro)
	{
		CONS_Debug(DBG_GAMELOGIC, "Enabling gyro for controller %d\n", joystick_id);
		SDL_SetGamepadSensorEnabled(controller->dev, SDL_SENSOR_GYRO, true);
	}
}

void I_InitController1(void)
{
	I_InitController(0);
}

void I_InitController2(void)
{
	I_InitController(1);
}

void I_InitController3(void)
{
	I_InitController(2);
}

void I_InitController4(void)
{
	I_InitController(3);
}


//
// I_ControllerScale
//
void I_ControllerScale(void)
{
	Joystick[0].bGamepadStyle = cv_controllerscale[0].value == 0;
	controllerInfo[0].scale = Joystick[0].bGamepadStyle ? 1 : cv_controllerscale[0].value;
}

void I_ControllerScale2(void)
{
	Joystick[1].bGamepadStyle = cv_controllerscale[1].value == 0;
	controllerInfo[1].scale = Joystick[1].bGamepadStyle ? 1 : cv_controllerscale[1].value;
}

void I_ControllerScale3(void)
{
	Joystick[2].bGamepadStyle = cv_controllerscale[2].value == 0;
	controllerInfo[2].scale = Joystick[2].bGamepadStyle ? 1 : cv_controllerscale[2].value;
}

void I_ControllerScale4(void)
{
	Joystick[3].bGamepadStyle = cv_controllerscale[3].value == 0;
	controllerInfo[3].scale = Joystick[3].bGamepadStyle ? 1 : cv_controllerscale[3].value;
}

static SDL_JoystickID *controllerlist = NULL;
void I_UpdateControllerList(void)
{
	int num_joy = 0;

	if (controllerlist)
		SDL_free(controllerlist);
	controllerlist = SDL_GetGamepads(&num_joy);

	if (!controllerlist)
	{
		CONS_Debug(DBG_GAMELOGIC, "Failed to fetch Controller list: %s\n", SDL_GetError());
		numcontrollers = 0;
		return;
	}

	numcontrollers = num_joy;
}

//
// I_GetControllerID
// Cheat to get the device index for a game controller handle
//
int32_t I_GetControllerID(SDL_Gamepad *dev)
{
	if (dev)
	{
		return (int32_t)SDL_GetGamepadID(dev);
	}

	return -1;
}

int32_t I_GetControllerSlotfromID(int32_t id)
{
	if (id < 0)
		return -1;

	if (!controllerlist)
	{
		CONS_Debug(DBG_GAMELOGIC, "Failed to fetch Controller list: %s\n", SDL_GetError());
		return -1;
	}

	for (int i = 0; i < numcontrollers; i++)
	{
		if (controllerlist[i] == (SDL_JoystickID)id)
		{
			return i+1;
		}
	}

	return -1;
}

//
// I_GetControllerIDForPlayer
// Cheat to get the device index for a specified player
//
int32_t I_GetControllerIDForPlayer(uint8_t pnum)
{
	if (controllerInfo[pnum].dev)
	{
		return (int32_t)controllerInfo[pnum].id;
	}

	return -1;
}

//
// I_NumControllers
// Updates the number of connected controllers
//
int32_t I_NumControllers(void)
{
	return numcontrollers;
}

//
// I_GetControllerName
// joystick name is straight from the driver
//
const char *I_GetControllerName(int32_t joyid)
{
	if (SDL_WasInit(SDL_INIT_GAMEPAD) == SDL_INIT_GAMEPAD)
	{
		if (joyid <= 0 || joyid > numcontrollers)
			return NULL;

		if (!controllerlist)
			return NULL;

		SDL_JoystickID joystick_id = controllerlist[joyid - 1];

		return SDL_GetGamepadNameForID(joystick_id);
	}

	return NULL;
}

//
// Controller Rumble and RGB LED stuff
//

dboolean I_ControllerSupportsRumble(int32_t playernum)
{
	return (controllerInfo[playernum].dev && controllerInfo[playernum].hasrumble);
}

dboolean I_ControllerSupportsIndicatorMono(int32_t playernum)
{
	return (controllerInfo[playernum].dev && controllerInfo[playernum].hasmonoled);
}

dboolean I_ControllerSupportsIndicatorColor(int32_t playernum)
{
	return (controllerInfo[playernum].dev && controllerInfo[playernum].hasrgbled);
}

dboolean I_ControllerSupportsIndicatorPlayer(int32_t playernum)
{
	return (controllerInfo[playernum].dev && controllerInfo[playernum].hasplayerled);
}

dboolean I_ControllerSupportsSensorAccelerometer(int32_t playernum)
{
	return (controllerInfo[playernum].dev && controllerInfo[playernum].hasaccelerometer);
}

dboolean I_ControllerSupportsSensorGyro(int32_t playernum)
{
	return (controllerInfo[playernum].dev && controllerInfo[playernum].hasgyro);
}

void I_ControllerRumble(int32_t playernum, uint16_t low_strength, uint16_t high_strength, uint32_t duration)
{
	SDL_Gamepad *controller = controllerInfo[playernum].dev;

	if (controller == NULL || !I_ControllerSupportsRumble(playernum))
	{
		return;
	}

	SDL_RumbleGamepad(controller, low_strength, high_strength, duration);
}

void I_SetControllerIndicatorColor(int32_t playernum, uint8_t red, uint8_t green, uint8_t blue)
{
	SDL_Gamepad *controller = controllerInfo[playernum].dev;

	if (controller == NULL || !I_ControllerSupportsIndicatorColor(playernum))
	{
		return;
	}

	SDL_SetGamepadLED(controller, red, green, blue);
}

void I_SetControllerIndicatorMonochrome(int32_t playernum, uint8_t brightness)
{
	SDL_Gamepad *controller = controllerInfo[playernum].dev;

	if (controller == NULL || !I_ControllerSupportsIndicatorMono(playernum))
	{
		return;
	}

	SDL_SetGamepadLED(controller, brightness, 0, 0);
}

void I_SetControllerPlayerNumber(int32_t playernum, int32_t number)
{
	SDL_Gamepad *controller = controllerInfo[playernum].dev;

	if (controller == NULL || !I_ControllerSupportsIndicatorPlayer(playernum))
	{
		return;
	}

	SDL_SetGamepadPlayerIndex(controller, number);
}

#endif
