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

#include "../i_gamepad.h"
#include "../i_system.h"

//
// I_ShutdownController
//
void I_ShutdownController(uint8_t index)
{
	(void)index;
}

void I_ShutdownControllerSystem(void)
{

}

//
// I_InitController
//
void I_InitController(uint8_t playernum)
{
	(void)playernum;
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

}

void I_ControllerScale2(void)
{

}

void I_ControllerScale3(void)
{

}

void I_ControllerScale4(void)
{

}

void I_UpdateControllerList(void)
{

}

//
// I_GetControllerID
// Cheat to get the device index for a game controller handle
//
int32_t I_GetControllerID(void *dev)
{
	(void)dev;
	return -1;
}

int32_t I_GetControllerSlotfromID(int32_t id)
{
	(void)id;
	return -1;
}

//
// I_GetControllerIDForPlayer
// Cheat to get the device index for a specified player
//
int32_t I_GetControllerIDForPlayer(uint8_t pnum)
{
	(void)pnum;
	return -1;
}

//
// I_NumControllers
// Updates the number of connected controllers
//
int32_t I_NumControllers(void)
{
	return 0;
}

//
// I_GetControllerName
// joystick name is straight from the driver
//
const char *I_GetControllerName(int32_t joyid)
{
	(void)joyid;
	return NULL;
}

//
// Controller Rumble and RGB LED stuff
//

dboolean I_ControllerSupportsRumble(int32_t playernum)
{
	(void)playernum;
	return false;
}

dboolean I_ControllerSupportsIndicatorMono(int32_t playernum)
{
	(void)playernum;
	return false;
}

dboolean I_ControllerSupportsIndicatorColor(int32_t playernum)
{
	(void)playernum;
	return false;
}

dboolean I_ControllerSupportsIndicatorPlayer(int32_t playernum)
{
	(void)playernum;
	return false;
}

dboolean I_ControllerSupportsSensorAccelerometer(int32_t playernum)
{
	(void)playernum;
	return false;
}

dboolean I_ControllerSupportsSensorGyro(int32_t playernum)
{
	(void)playernum;
	return false;
}

void I_ControllerRumble(int32_t playernum, uint16_t low_strength, uint16_t high_strength, uint32_t duration)
{
	(void)playernum;
	(void)low_strength;
	(void)high_strength;
	(void)duration;
}

void I_SetControllerIndicatorColor(int32_t playernum, uint8_t red, uint8_t green, uint8_t blue)
{
	(void)playernum;
	(void)red;
	(void)green;
	(void)blue;
}

void I_SetControllerIndicatorMonochrome(int32_t playernum, uint8_t brightness)
{
	(void)playernum;
	(void)brightness;
}

void I_SetControllerPlayerNumber(int32_t playernum, int32_t number)
{
	(void)playernum;
	(void)number;
}
