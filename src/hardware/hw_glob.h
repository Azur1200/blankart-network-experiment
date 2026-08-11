// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_glob.h
/// \brief globals (shared data & code) for hw_ modules

#ifndef _HWR_GLOB_H_
#define _HWR_GLOB_H_

#include "hw_defs.h"
#include "hw_main.h"
#include "../m_misc.h"
#include "../r_defs.h"
#include "../p_setup.h"

#ifdef __cplusplus
extern "C" {
#endif

// the original aspect ratio of Doom graphics isn't square
#define ORIGINAL_ASPECT (320.0f/200.0f)

// Uncomment this to enable the OpenGL loading screen
//#define HWR_LOADING_SCREEN

// SRB2Kart

// -----------
// structures
// -----------

// a vertex of a Doom 'plane' polygon
typedef struct
{
	float x;
	float y;
	float z;
} polyvertex_t;

// a convex 'plane' polygon, clockwise order
typedef struct
{
	INT32 numpts;
	polyvertex_t pts[];
} poly_t;

// holds extra info for 3D render, for each subsector in subsectors[]
typedef struct
{
	poly_t *planepoly;  // the generated convex polygon
} poly_subsector_t;

// needed for sprite rendering
// equivalent of the software renderer's vissprites
typedef struct gl_vissprite_s
{
	float x1, x2;
	float z1, z2;
	float gz, gzt;

	float tz;
	float tracertz; // for MF2_LINKDRAW sprites, this contains tracer's tz for use in sorting

	float scale;
	float shadowheight, shadowscale;

	float spritexscale, spriteyscale;
	float spritexoffset, spriteyoffset;

	UINT32 renderflags;
	UINT8 rotateflags;

	dboolean flip, vflip;
	dboolean precip; // Tails 08-25-2002
	dboolean bbox;
	dboolean rotated;
	UINT8 translucency;       //alpha level 0-255

	angle_t angle; // for splats

	// Affine nonsense
	struct {
		// Vertex points for the affine sprite to be drawn to.
		//
		//  4--3
		//  | /|
		//  |/ |
		//  1--2
		f_vector2_t p1, p2, p3, p4;

		// The "root", or center, of the affine sprite
		polyvertex_t root;

		// Bounding point (leftmost and highest point), to resolve clipping
		polyvertex_t bounding_point;

		// Sine/cosine multiplier, used for billboarding.
		float patchsin, patchcos;

		affine_t transform; // The actual affine transformation.
	} affine;

	//Hurdler: 25/04/2000: now support colormap in hardware mode
	UINT8 *colormap;
	INT32 dispoffset; // copy of info->dispoffset, affects ordering but not drawing

	patch_t *gpatch;
	mobj_t *mobj; // NOTE: This is a precipmobj_t if precip is true !!! Watch out.
} gl_vissprite_t;

void HWR_ObjectLightLevelPost(gl_vissprite_t *spr, const sector_t *sector, INT32 *lightlevel, dboolean model);

typedef struct
{
	// Viewport.
	fixed_t viewx;
	fixed_t viewy;
	fixed_t viewz;
	angle_t viewangle;

	seg_t *seg; // seg that is used for drawing to the stencil buffer
	line_t *clipline;

	// angles for the left and right edges of the portal
	// relative to the viewpoint
	angle_t angle1;
	angle_t angle2;
} gl_portal_t;

typedef struct
{
	gl_portal_t *portals;
	unsigned int size;
	unsigned int capacity;
} gl_portal_array_t;

// --------
// hw_main.c
// -------

void HWR_RenderViewpoint(player_t *player, dboolean drawSkyTexture, dboolean is_skybox, dboolean timing, gl_portal_t *rootportal);
void HWR_PrepareTransform(player_t *player, dboolean is_skybox);
void HWR_ResetClipper(void);

// --------
// hw_map.c
// --------
extern poly_subsector_t *poly_subsectors;
extern size_t num_poly_subsector;

void HWR_InitPolyPool(void);
void HWR_FreePolyPool(void);

// --------
// hw_cache.c
// --------
RGBA_t *HWR_GetTexturePalette(void);

void HWR_InitMapTextures(void);
void HWR_LoadMapTextures(size_t pnumtextures);
void HWR_FreeMapTextures(void);
void HWR_PrecacheLevel(void);

patch_t *HWR_GetCachedGLPatchPwad(UINT16 wad, UINT16 lump);
patch_t *HWR_GetCachedGLPatch(lumpnum_t lumpnum);

void HWR_GetPatch(patch_t *patch);
void HWR_GetMappedPatch(patch_t *patch, const UINT8 *colormap);
void HWR_GetFadeMask(lumpnum_t fademasklumpnum);
patch_t *HWR_GetPic(lumpnum_t lumpnum);

GLMapTexture_t *HWR_GetTexture(INT32 tex, dboolean noencoremap);
void HWR_GetLevelFlat(levelflat_t *levelflat, dboolean noencoremap);
void HWR_GetRawFlat(lumpnum_t flatlumpnum, dboolean noencoremap);

void HWR_FreeTexture(patch_t *patch);
void HWR_FreeTextureData(patch_t *patch);
void HWR_FreeTextureColormaps(patch_t *patch);
void HWR_ClearAllTextures(void);
void HWR_FreeColormapCache(void);
void HWR_UnlockCachedPatch(GLPatch_t *gpatch);

void HWR_SetPalette(RGBA_t *palette);
void HWR_SetMapPalette(void);
UINT32 HWR_CreateLightTable(UINT8 *lighttable);
UINT32 HWR_GetLightTableID(extracolormap_t *colormap);
void HWR_ClearLightTables(void);

// --------
// hw_bsp.c
// --------
void HWR_RenderBSPNode(INT32 bspnum);

// --------
// hw_draw.c
// --------
extern INT32 patchformat;
extern INT32 textureformat;

// --------
// hw_plane.c
// --------
void HWR_RenderPlane(subsector_t *subsector, poly_subsector_t *xsub, dboolean isceiling, fixed_t fixedheight, FBITFIELD PolyFlags, INT32 lightlevel, levelflat_t *levelflat, sector_t *FOFsector, UINT8 alpha, extracolormap_t *planecolormap);
void HWR_RenderPolyObjectPlane(polyobj_t *polysector, dboolean isceiling, fixed_t fixedheight,
							   FBITFIELD blendmode, UINT8 lightlevel, levelflat_t *levelflat, sector_t *FOFsector,
							   UINT8 alpha, extracolormap_t *planecolormap);

// --------
// hw_drawnodes.c
// --------
void HWR_AddTransparentWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, INT32 texnum, dboolean noencore, FBITFIELD blend, dboolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap);
void HWR_AddTransparentFloor(levelflat_t *levelflat, poly_subsector_t *xsub, dboolean isceiling, fixed_t fixedheight, INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, dboolean fogplane, extracolormap_t *planecolormap);
void HWR_AddTransparentPolyobjectFloor(levelflat_t *levelflat, polyobj_t *polysector, dboolean isceiling, fixed_t fixedheight,
									   INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, extracolormap_t *planecolormap);
void HWR_PushDrawNodeState(void);
void HWR_PopDrawNodeState(void);
void HWR_RenderDrawNodes(void);

// --------
// hw_segs.c
// --------
void HWR_ProcessSeg(void);
void HWR_RenderWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blend, dboolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap);

// --------
// hw_shaders.c
// --------
dboolean HWR_InitShaders(void);
void HWR_CompileShaders(void);

int HWR_GetShaderFromTarget(int shader_target);

void HWR_LoadAllBaseShaders(void);
void HWR_LoadCustomShadersFromFile(UINT16 wadnum, dboolean PK3, dboolean startup);
const char *HWR_GetShaderName(INT32 shader);

extern customshaderxlat_t shaderxlat[];

// --------
// hw_sky.c
// --------
void HWR_ClearSkyDome(void);
void HWR_BuildSkyDome(void);
void HWR_DrawSkyBackground(player_t *player);

// --------
// hw_things.c
// --------

void HWR_ClearSprites(void);
void HWR_PushSpriteState(void);
void HWR_PopSpriteState(void);
void HWR_AddSprites(sector_t *sec);
void HWR_AddPrecipitationSprites(void);
UINT32 HWR_SortVisSprites(void);
void HWR_DrawSprites(void);
void HWR_ProjectBoundingBox(mobj_t *thing);

// --------
// hw_portal.c
// --------

#define INIT_PORTAL_ARRAY_SIZE 4

void HWR_PortalFrame(gl_portal_t *portal, dboolean set_culling);
gl_portal_array_t *HWR_GetPortalArray(void);
dboolean HWR_AddPortal(line_t *start, line_t *dest, seg_t *seg);
void HWR_ClearPortals(void);
void HWR_PortalClipping(gl_portal_t *portal);
void HWR_RenderPortalSeg(seg_t *seg);
void HWR_SetStencilState(INT32 state);
void HWR_RenderDepthEraser(dboolean visible);
void HWR_RenderPortal(gl_portal_t *portal, gl_portal_t *rootportal, player_t *player, dboolean is_skybox);

extern INT32 gl_portal_level;

extern dboolean gl_drawing_stencil;
extern sector_t *gl_portalcullsector;
extern line_t *gl_portalclipline;

extern dboolean gl_printportals;
extern INT32 gl_debugportal;


#ifdef __cplusplus
} // extern "C"
#endif

#endif //_HW_GLOB_
