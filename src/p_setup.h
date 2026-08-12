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
/// \file  p_setup.h
/// \brief Setup a game, startup stuff

#ifndef __P_SETUP__
#define __P_SETUP__

#include "g_mapnum.h"
#include "doomdata.h"
#include "doomstat.h"
#include "r_defs.h"
#include "k_terrain.h"
#include "w_wad.h"

#ifdef __cplusplus
extern "C" {
#endif

// map hash, sent to players via PT_SERVERINFO
extern uint64_t maphash;

extern consvar_t cv_writetextmap_includemappatch;

// Player spawn spots for deathmatch.
#define MAX_DM_STARTS 64
extern mapthing_t *deathmatchstarts[MAX_DM_STARTS];
extern int32_t numdmstarts, numcoopstarts, numredctfstarts, numbluectfstarts;

extern dboolean levelloading;
extern dboolean levelloaded; // is there an actual level loaded? for intermission
                            // (i.e. is it safe to call D_RenderLevel?)
extern uint8_t levelfadecol;

extern lumpnum_t lastloadedmaplumpnum; // for comparative savegame
extern virtres_t *curmapvirt;

extern dboolean usedspawnfinishline;

typedef enum
{
	MNS_UNKNOWN,
	MNS_SRB2KART,
	MNS_BLANKART,
	MNS_RINGRACERS,
	MNS_SRB2,
	MNS__MAX,
} mapnamespace_t;

extern mapnamespace_t mapnamespace;

/* for levelflat type */
enum
{
	LEVELFLAT_NONE,/* HOM time my friend */
	LEVELFLAT_TEXTURE,
};

//
// MAP used flats lookup table
//
struct levelflat_t
{
	char name[9]; // resource name from wad
	terrain_t *terrain;
	uint8_t type;
	int32_t texture_id;
};

extern size_t numlevelflats;
extern levelflat_t *levelflats;
int32_t P_AddLevelFlat(const char *flatname, levelflat_t *levelflat);
int32_t P_AddLevelFlatRuntime(const char *flatname);
int32_t P_CheckLevelFlat(const char *flatname);

extern size_t nummapthings;
extern mapthing_t *mapthings;

const char *P_MapNamespaceString(mapnamespace_t mns);
void P_SetupLevelSky(const char *skytexname, dboolean global);
void P_RespawnThings(void);
void P_FreeLevelState(void);
dboolean P_LoadLevel(dboolean fromnetsave, dboolean reloadinggamestate);
void P_PostLoadLevel(void);
#ifdef HWRENDER
void HWR_LoadLevel(void);
#endif
void P_UnloadLevel(void);
dboolean P_AddWadFile(const char *wadfilename, wadcompat_t compat, dboolean local);

#define MAPRET_ADDED (1)
#define MAPRET_CURRENTREPLACED (1<<1)
uint8_t P_InitMapData(dboolean existingmapheaders);
extern lumpnum_t wadnamelump;
extern mapnum_t wadnamemap;
#define WADNAMECHECK(name) (!strncmp(name, "WADNAME", 7))

// WARNING: The following functions should be grouped as follows:
// any amount of PartialAdds followed by MultiSetups until returned true,
// as soon as possible.
uint16_t P_PartialAddWadFile(const char *wadfilename, wadcompat_t compat, dboolean local);
// Run a single stage of multisetup, or all of them if fullsetup set.
//   fullsetup true: run everything
//   otherwise
//   stage 0: reload UI graphics (enough for unimportant WADs)
//   stage 1: reload textures
//   stage 2: reload animdefs and run post-setup actions
// returns true if setup finished on this call, false otherwise (always true on fullsetup)
// throws I_Error if called without any partial adds started as a safeguard
dboolean P_MultiSetupWadFiles(dboolean fullsetup);
// Get the current setup stage.
//   if negative, no PartialAdds done since last MultiSetup
//   if 0, partial adds done but MultiSetup not called yet
//   if positive, setup's partway done
int8_t P_PartialAddGetStage(void);
extern uint16_t partadd_earliestfile;

dboolean P_RunSOC(const char *socfilename);
void P_LoadSoundsRange(uint16_t wadnum, uint16_t first, uint16_t num);
void P_LoadMusicsRange(uint16_t wadnum, uint16_t first, uint16_t num);
//void P_WriteThings(void);
void P_UpdateSegLightOffset(seg_t *li);
dboolean P_ApplyLightOffset(uint8_t baselightnum, const sector_t *sector);
dboolean P_ApplyLightOffsetFine(uint8_t baselightlevel, const sector_t *sector);
dboolean P_SectorUsesDirectionalLighting(const sector_t *sector);
size_t P_PrecacheLevelFlats(void);
void P_AllocMapHeader(mapnum_t i);

void P_SetDemoFlickies(mapnum_t i);
void P_DeleteFlickies(mapnum_t i);

// Needed for NiGHTS
void P_ReloadRings(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
