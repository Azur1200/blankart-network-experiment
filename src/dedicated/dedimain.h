// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 2006-2020 by Sonic Team Junior.
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
/// \file
/// \brief System specific interface stuff.

#ifndef __dedimain__
#define __dedimain__

#include "../m_fixed.h"
#include "../doomdef.h"

extern boolean consolevent;
extern boolean framebuffer;

#ifdef __cplusplus
extern "C" {
#endif

void I_GetConsoleEvents(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
