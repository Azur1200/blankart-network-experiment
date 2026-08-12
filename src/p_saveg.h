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
/// \file  p_saveg.h
/// \brief Savegame I/O, archiving, persistence

#ifndef __P_SAVEG__
#define __P_SAVEG__

#include "g_mapnum.h"

#ifdef __cplusplus
extern "C" {
#endif

// 1024 bytes is plenty for a savegame
#define SAVEGAMESIZE (1024)

// For netgames
#define NETSAVEGAMESIZE (768*1024)

// Persistent storage/archiving.
// These are the load / save game routines.

void P_SaveGame(savebuffer_t *save, mapnum_t mapnum);
void P_SaveNetGame(savebuffer_t *save, dboolean resending);
dboolean P_LoadGame(savebuffer_t *save, mapnum_t mapoverride);
dboolean P_LoadNetGame(savebuffer_t *save, dboolean reloading);

mobj_t *P_FindNewPosition(uint32_t oldposition);

struct savedata_t
{
	uint16_t skin;
	int32_t score;
	int32_t lives;
	uint16_t emeralds;
	uint8_t numgameovers;
};

extern savedata_t savedata;

struct savebuffer_t
{
	uint8_t *buffer;
	uint8_t *p;
	uint8_t *end;
	size_t size;
	dboolean write;
};

dboolean P_SaveBufferZAlloc(savebuffer_t *save, size_t alloc_size, int32_t tag, void *user);
#define P_SaveBufferAlloc(a,b) P_SaveBufferZAlloc(a, b, PU_STATIC, NULL)
dboolean P_SaveBufferFromExisting(savebuffer_t *save, uint8_t *existing_buffer, size_t existing_size);
dboolean P_SaveBufferFromLump(savebuffer_t *save, lumpnum_t lump);
size_t P_SaveBufferFromFile(savebuffer_t *save, char const *name);
void P_SaveBufferFree(savebuffer_t *save);

dboolean MobjIsNetSynced(mobj_t *mobj);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
