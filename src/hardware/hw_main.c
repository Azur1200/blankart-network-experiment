// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_main.c
/// \brief hardware renderer, using the standard HardWareRender driver DLL for SRB2

#include <math.h>

#include "../doomstat.h"

#include "../qs22j.h"
#include "hw_defs.h"

#ifdef HWRENDER
#include "hw_clip.h"
#include "hw_glob.h"
#include "hw_light.h"
#include "hw_gpu.h"
#include "hw_batching.h"

#include "../i_video.h" // for rendermode == render_glide
#include "../v_video.h"
#include "../p_local.h"
#include "../p_setup.h"
#include "../r_fps.h"
#include "../r_local.h"
#include "../r_patch.h"
#include "../r_picformats.h"
#include "../r_bsp.h"
#include "../d_clisrv.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../r_splats.h"
#include "../g_game.h"
#include "../st_stuff.h"
#include "../i_system.h"
#include "../m_cheat.h"
#include "../f_finale.h"
#include "../r_things.h" // R_GetShadowZ
#include "../d_main.h"
#include "../p_slopes.h"
#include "hw_md2.h"

// SRB2Kart
#include "../k_kart.h"
#include "../r_fps.h"
#include "../r_plane.h" // R_FlatDimensionsFromLumpSize
#include "../k_items.h"

// ==========================================================================
// the hardware driver object
// ==========================================================================

// ==========================================================================
//                                                                     PROTOS
// ==========================================================================

void HWR_AddTransparentFloor(levelflat_t *levelflat, extrasubsector_t *xsub, boolean isceiling, fixed_t fixedheight, INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, boolean fogplane, extracolormap_t *planecolormap);
void HWR_AddTransparentPolyobjectFloor(levelflat_t *levelflat, polyobj_t *polysector, boolean isceiling, fixed_t fixedheight,
                             INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, extracolormap_t *planecolormap);

#define ABS(x) ((x) < 0 ? -(x) : (x))

// ==========================================================================
//                                                                    GLOBALS
// ==========================================================================

// base values set at SetViewSize
static float gl_basecentery;
static float gl_basecenterx;

float gl_baseviewwindowy, gl_basewindowcentery;
float gl_baseviewwindowx, gl_basewindowcenterx;
float gl_viewwidth, gl_viewheight; // viewport clipping boundaries (screen coords)
float gl_viewwindowx;

static float gl_centerx, gl_centery;
static float gl_viewwindowy; // top left corner of view window
static float gl_windowcenterx; // center of view window, for projection
static float gl_windowcentery;

static float gl_pspritexscale, gl_pspriteyscale;

static seg_t *gl_curline;
static side_t *gl_sidedef;
static line_t *gl_linedef;
sector_t *gl_frontsector;
sector_t *gl_backsector;

// Render stats
precise_t ps_hw_skyboxtime = 0;
precise_t ps_hw_nodesorttime = 0;
precise_t ps_hw_nodedrawtime = 0;
precise_t ps_hw_spritesorttime = 0;
precise_t ps_hw_spritedrawtime = 0;

// Render stats for batching
int ps_hw_numpolys = 0;
int ps_hw_numverts = 0;
int ps_hw_numcalls = 0;
int ps_hw_numshaders = 0;
int ps_hw_numtextures = 0;
int ps_hw_numpolyflags = 0;
int ps_hw_numcolors = 0;
precise_t ps_hw_batchsorttime = 0;
precise_t ps_hw_batchdrawtime = 0;

boolean gl_init = false;
boolean gl_maploaded = false;
boolean gl_sessioncommandsadded = false;
// false if shaders have not been initialized yet, or if shaders are not available
boolean gl_shadersavailable = false;

// Whether the internal state is set to palette rendering or not.
static boolean gl_palette_rendering_state = false;

// --------------------------------------------------------------------------
//                                              STUFF FOR THE PROJECTION CODE
// --------------------------------------------------------------------------

FTransform atransform;
// duplicates of the main code, set after R_SetupFrame() passed them into sharedstruct,
// copied here for local use
fixed_t dup_viewx, dup_viewy, dup_viewz;
angle_t dup_viewangle;

float gl_viewx, gl_viewy, gl_viewz;
float gl_viewsin, gl_viewcos;

// Maybe not necessary with the new T&L code (needs to be checked!)
float gl_viewludsin, gl_viewludcos; // look up down kik test
static float gl_fovlud;

static angle_t gl_aimingangle;

// ==========================================================================
// Lighting
// ==========================================================================

// Returns true if shaders can be used.
boolean HWR_UseShader(void)
{
	return (cv_glshaders.value && gl_shadersavailable);
}

boolean HWR_OverrideObjectLightLevel(mobj_t *thing, INT32 *lightlevel)
{
	if (R_ThingIsFullBright(thing))
		*lightlevel = 255;
	else if (R_ThingIsFullDark(thing))
		*lightlevel = 0;
	else if (thing->renderflags & RF_ABSOLUTELIGHTLEVEL)
		*lightlevel = R_ThingLightLevel(thing);
	else
		return false;

	return true;
}

void HWR_ObjectLightLevelPost(gl_vissprite_t *spr, const sector_t *sector, INT32 *lightlevel, boolean model)
{
	const boolean semibright = R_ThingIsSemiBright(spr->mobj);
	const boolean papersprite = R_ThingIsPaperSprite(spr->mobj);

	*lightlevel += R_ThingLightLevel(spr->mobj);

	if (maplighting.directional == true && P_SectorUsesDirectionalLighting(sector))
	{
		if (model == false) // this is implemented by shader
		{
			fixed_t extralight = R_GetSpriteDirectionalLighting(
				papersprite
				? spr->angle + (spr->flip ? -ANGLE_90 : ANGLE_90)
				: R_PointToAngle(spr->mobj->x, spr->mobj->y) // fixme
			);

			// Less change in contrast in dark sectors
			extralight = FixedMul(extralight, min(max(0, *lightlevel), 255) * FRACUNIT / 255);

			if (papersprite)
			{
				// Papersprite contrast should match walls
				*lightlevel += FixedFloor(extralight + (FRACUNIT / 2)) / FRACUNIT;
			}
			else
			{
				// simple OGL approximation
				fixed_t tr = R_PointToDist(spr->mobj->x, spr->mobj->y);
				fixed_t xscale = FixedDiv((vid.width / 2) << FRACBITS, tr);

				// Less change in contrast at further distances, to counteract DOOM diminished light
				fixed_t n = FixedDiv(FixedMul(xscale, LIGHTRESOLUTIONFIX), ((MAXLIGHTSCALE-1) << LIGHTSCALESHIFT));
				extralight = FixedMul(extralight, min(n, FRACUNIT));

				// Contrast is stronger for normal sprites, stronger than wall lighting is at the same distance
				*lightlevel += FixedFloor((extralight * 2) + (FRACUNIT / 2)) / FRACUNIT;
			}
		}

		// Semibright objects will be made slightly brighter to compensate contrast
		if (semibright)
		{
			*lightlevel += 16;
		}
	}

	if (semibright)
	{
		*lightlevel = 192 + (*lightlevel >> 1);
	}
}

void HWR_Lighting(FSurfaceInfo *Surface, INT32 light_level, extracolormap_t *colormap, const boolean directional)
{
	RGBA_t poly_color, tint_color, fade_color;

	poly_color.rgba = 0xFFFFFFFF;
	tint_color.rgba = (colormap != NULL) ? (UINT32)colormap->rgba : GL_DEFAULTMIX;
	fade_color.rgba = (colormap != NULL) ? (UINT32)colormap->fadergba : GL_DEFAULTFOG;

	// Crappy backup coloring if you can't do shaders
	if (!HWR_UseShader())
	{
		// be careful, this may get negative for high lightlevel values.
		float tint_alpha, fade_alpha;
		float red, green, blue;

		red = (float)poly_color.s.red;
		green = (float)poly_color.s.green;
		blue = (float)poly_color.s.blue;

		// 48 is just an arbritrary value that looked relatively okay.
		tint_alpha = (float)(sqrt(tint_color.s.alpha) * 48) / 255.0f;

		// 8 is roughly the brightness of the "close" color in Software, and 16 the brightness of the "far" color.
		// 8 is too bright for dark levels, and 16 is too dark for bright levels.
		// 12 is the compromise value. It doesn't look especially good anywhere, but it's the most balanced.
		// (Also, as far as I can tell, fade_color's alpha is actually not used in Software, so we only use light level.)
		fade_alpha = (float)(sqrt(255-light_level) * 12) / 255.0f;

		// Clamp the alpha values
		tint_alpha = min(max(tint_alpha, 0.0f), 1.0f);
		fade_alpha = min(max(fade_alpha, 0.0f), 1.0f);

		red = (tint_color.s.red * tint_alpha) + (red * (1.0f - tint_alpha));
		green = (tint_color.s.green * tint_alpha) + (green * (1.0f - tint_alpha));
		blue = (tint_color.s.blue * tint_alpha) + (blue * (1.0f - tint_alpha));

		red = (fade_color.s.red * fade_alpha) + (red * (1.0f - fade_alpha));
		green = (fade_color.s.green * fade_alpha) + (green * (1.0f - fade_alpha));
		blue = (fade_color.s.blue * fade_alpha) + (blue * (1.0f - fade_alpha));

		poly_color.s.red = (UINT8)red;
		poly_color.s.green = (UINT8)green;
		poly_color.s.blue = (UINT8)blue;
	}

	// Shift the lightlevel for Palette rendering mode to replicate software´s limited 32 lightlevels
	if (HWR_ShouldUsePaletteRendering())
		light_level = (light_level >> LIGHTSEGSHIFT) << LIGHTSEGSHIFT;

	// Clamp the light level, since it can sometimes go out of the 0-255 range from animations
	light_level = min(max(light_level, cv_secbright.value), 255);

	V_CubeApply(&tint_color);
	V_CubeApply(&fade_color);
	Surface->PolyColor.rgba = poly_color.rgba;
	Surface->TintColor.rgba = tint_color.rgba;
	Surface->FadeColor.rgba = fade_color.rgba;

	Surface->LightInfo.light_level = light_level;
	Surface->LightInfo.fade_start = (colormap != NULL) ? colormap->fadestart : 0;
	Surface->LightInfo.fade_end = (colormap != NULL) ? colormap->fadeend : 31;
	Surface->LightInfo.newfade = (colormap != NULL) ? !!(colormap->flags & CMF_NEWFADE) : false;

	Surface->LightInfo.directional = (maplighting.directional == true && directional == true);

	if (HWR_ShouldUsePaletteRendering())
		Surface->LightTableId = HWR_GetLightTableID(colormap);
	else
		Surface->LightTableId = 0;
}

UINT8 HWR_FogBlockAlpha(INT32 light, extracolormap_t *colormap) // Let's see if this can work
{
	RGBA_t realcolor, surfcolor;
	INT32 alpha;

	realcolor.rgba = (colormap != NULL) ? colormap->rgba : GL_DEFAULTMIX;

	if (cv_glshaders.value && gl_shadersavailable)
	{
		surfcolor.s.alpha = (255 - light);
	}
	else
	{
		light = light - (255 - light);

		// Don't go out of bounds
		if (light < 0)
			light = 0;
		else if (light > 255)
			light = 255;

		alpha = (realcolor.s.alpha*255)/25;

		// at 255 brightness, alpha is between 0 and 127, at 0 brightness alpha will always be 255
		surfcolor.s.alpha = (alpha*light) / (2*256) + 255-light;
	}

	return surfcolor.s.alpha;
}

static FUINT HWR_CalcWallLight(FUINT lightnum, seg_t *seg)
{
	INT16 finallight = lightnum;

	if (seg != NULL && P_ApplyLightOffsetFine(lightnum, seg->frontsector))
	{
		finallight += seg->hwLightOffset;

		if (finallight > 255) finallight = 255;
		if (finallight < 0) finallight = 0;
	}

	return (FUINT)finallight;
}

FBITFIELD HWR_GetBlendModeFlag(INT32 ast)
{
	switch (ast)
	{
		//case AST_COPY: -- intentionally defaults to translucent
		case AST_OVERLAY:
			return PF_Masked;
		case AST_ADD:
			return PF_Additive;
		case AST_SUBTRACT:
			return PF_ReverseSubtract;
		case AST_REVERSESUBTRACT:
			return PF_Subtractive;
		case AST_MODULATE:
			return PF_Multiplicative;
		default:
			return PF_Translucent;
	}

	return 0;
}

UINT8 HWR_GetTranstableAlpha(INT32 transtablenum)
{
	transtablenum = max(min(transtablenum, tr_trans90), 0);

	switch (transtablenum)
	{
		case 0          : return 0xff;
		case tr_trans10 : return 0xe6;
		case tr_trans20 : return 0xcc;
		case tr_trans30 : return 0xb3;
		case tr_trans40 : return 0x99;
		case tr_trans50 : return 0x80;
		case tr_trans60 : return 0x66;
		case tr_trans70 : return 0x4c;
		case tr_trans80 : return 0x33;
		case tr_trans90 : return 0x19;
	}

	return 0xff;
}

FBITFIELD HWR_SurfaceBlend(INT32 style, INT32 transtablenum, FSurfaceInfo *pSurf)
{
	pSurf->PolyColor.s.alpha = 0xff;

	if (style == AST_MODULATE)
		return PF_Multiplicative;

	if (!transtablenum && !style)
		return PF_Translucent;

	pSurf->PolyColor.s.alpha = HWR_GetTranstableAlpha(transtablenum);
	return HWR_GetBlendModeFlag(style);
}

FBITFIELD HWR_TranstableToAlpha(INT32 transtablenum, FSurfaceInfo *pSurf)
{
	if (!transtablenum)
	{
		pSurf->PolyColor.s.alpha = 0x00;
		return PF_Masked;
	}

	pSurf->PolyColor.s.alpha = HWR_GetTranstableAlpha(transtablenum);
	return PF_Translucent;
}

static void HWR_AddTransparentWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, INT32 texnum, boolean noencore, FBITFIELD blend, boolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap);

// ==========================================================================
// Wall generation from subsector segs
// ==========================================================================

/*
   wallVerts order is :
		3--2
		| /|
		|/ |
		0--1
*/

//
// HWR_ProjectWall
//
static void HWR_ProjectWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blendmode, INT32 lightlevel, extracolormap_t *wallcolormap)
{
	INT32 shader = SHADER_NONE;

	HWR_Lighting(pSurf, lightlevel, wallcolormap, P_SectorUsesDirectionalLighting(gl_frontsector));

	if (HWR_UseShader())
	{
		shader = SHADER_WALL;
		blendmode |= PF_ColorMapped;
	}

	HWR_ProcessPolygon(pSurf, wallVerts, 4, blendmode|PF_Modulated|PF_Occlude, shader, false);
}

// ==========================================================================
//                                                          BSP, CULL, ETC..
// ==========================================================================

//
// HWR_SplitWall
// SoM: split up and light walls according to the lightlist.
// This may also include leaving out parts of the wall that can't be seen
//
static void HWR_SplitWall(sector_t *sector, FOutVector *wallVerts, INT32 texnum, boolean noencore, FSurfaceInfo* Surf, INT32 cutflag, ffloor_t *pfloor, FBITFIELD polyflags)
{
	float realtop, realbot, top, bot;
	float pegt, pegb, pegmul;
	float height = 0.0f, bheight = 0.0f;

	float endrealtop, endrealbot, endtop, endbot;
	float endpegt, endpegb, endpegmul;
	float endheight = 0.0f, endbheight = 0.0f;

	float diff;

	fixed_t v1x = FloatToFixed(wallVerts[0].x);
	fixed_t v1y = FloatToFixed(wallVerts[0].z);
	fixed_t v2x = FloatToFixed(wallVerts[1].x);
	fixed_t v2y = FloatToFixed(wallVerts[1].z);

	const UINT8 alpha = Surf->PolyColor.s.alpha;
	FUINT lightnum = sector->lightlevel;
	extracolormap_t *colormap = NULL;

	realtop = top = wallVerts[3].y;
	realbot = bot = wallVerts[0].y;
	diff = top - bot;

	pegt = wallVerts[3].t;
	pegb = wallVerts[0].t;

	// Lactozilla: If both heights of a side lay on the same position, then this wall is a triangle.
	// To avoid division by zero, which would result in a NaN, we check if the vertical difference
	// between the two vertices is not zero.
	if (fpclassify(diff) == FP_ZERO)
		pegmul = 0.0;
	else
		pegmul = (pegb - pegt) / diff;

	endrealtop = endtop = wallVerts[2].y;
	endrealbot = endbot = wallVerts[1].y;
	diff = endtop - endbot;

	endpegt = wallVerts[2].t;
	endpegb = wallVerts[1].t;

	if (fpclassify(diff) == FP_ZERO)
		endpegmul = 0.0;
	else
		endpegmul = (endpegb - endpegt) / diff;

	for (INT32 i = 0; i < sector->numlights; i++)
	{
		if (endtop < endrealbot && top < realbot)
			return;

		lightlist_t *list = sector->lightlist;

		if (!(list[i].flags & FOF_NOSHADE))
		{
			if (pfloor && (pfloor->fofflags & FOF_FOG))
			{
				lightnum = pfloor->master->frontsector->lightlevel;
				colormap = pfloor->master->frontsector->extra_colormap;
			}
			else
			{
				lightnum = *list[i].lightlevel;
				colormap = *list[i].extra_colormap;
			}
		}

		boolean solid = false;

		if ((sector->lightlist[i].flags & FOF_CUTSOLIDS) && !(cutflag & FOF_EXTRA))
			solid = true;
		else if ((sector->lightlist[i].flags & FOF_CUTEXTRA) && (cutflag & FOF_EXTRA))
		{
			if (sector->lightlist[i].flags & FOF_EXTRA)
			{
				if ((sector->lightlist[i].flags & (FOF_FOG|FOF_SWIMMABLE)) == (cutflag & (FOF_FOG|FOF_SWIMMABLE))) // Only merge with your own types
					solid = true;
			}
			else
				solid = true;
		}
		else
			solid = false;

		height = FixedToFloat(P_GetLightZAt(&list[i], v1x, v1y));
		endheight = FixedToFloat(P_GetLightZAt(&list[i], v2x, v2y));

		if (solid)
		{
			bheight = FixedToFloat(P_GetFFloorBottomZAt(list[i].caster, v1x, v1y));
			endbheight = FixedToFloat(P_GetFFloorBottomZAt(list[i].caster, v2x, v2y));
		}

		if (endheight >= endtop && height >= top)
		{
			if (solid && top > bheight)
				top = bheight;
			if (solid && endtop > endbheight)
				endtop = endbheight;
		}

		if (i + 1 < sector->numlights)
		{
			bheight = FixedToFloat(P_GetLightZAt(&list[i+1], v1x, v1y));
			endbheight = FixedToFloat(P_GetLightZAt(&list[i+1], v2x, v2y));
		}
		else
		{
			bheight = realbot;
			endbheight = endrealbot;
		}

		if (endbheight > endtop)
			endbot = endtop;

		if (bheight >= top)
			continue;

		// Found a break
		// The heights are clamped to ensure the polygon doesn't cross itself.
		bot = bheight;

		if (bot < realbot)
			bot = realbot;

		endbot = min(max(endbheight, endrealbot), endtop);

		Surf->PolyColor.s.alpha = alpha;

		wallVerts[3].t = pegt + ((realtop - top) * pegmul);
		wallVerts[2].t = endpegt + ((endrealtop - endtop) * endpegmul);
		wallVerts[0].t = pegt + ((realtop - bot) * pegmul);
		wallVerts[1].t = endpegt + ((endrealtop - endbot) * endpegmul);

		// set top/bottom coords
		wallVerts[3].y = top;
		wallVerts[2].y = endtop;
		wallVerts[0].y = bot;
		wallVerts[1].y = endbot;

		if (polyflags & PF_Fog)
			HWR_AddTransparentWall(wallVerts, Surf, texnum, noencore, polyflags, true,  HWR_CalcWallLight(lightnum, gl_curline), colormap);
		else if (polyflags & PF_EnvironmentTrans)
			HWR_AddTransparentWall(wallVerts, Surf, texnum, noencore, polyflags, false,  HWR_CalcWallLight(lightnum, gl_curline), colormap);
		else
			HWR_ProjectWall(wallVerts, Surf, polyflags, HWR_CalcWallLight(lightnum, gl_curline), colormap);

		top = bot;
		endtop = endbot;
	}

	bot = realbot;
	endbot = endrealbot;
	if (endtop <= endrealbot && top <= realbot)
		return;

	Surf->PolyColor.s.alpha = alpha;

	wallVerts[3].t = pegt + ((realtop - top) * pegmul);
	wallVerts[2].t = endpegt + ((endrealtop - endtop) * endpegmul);
	wallVerts[0].t = pegt + ((realtop - bot) * pegmul);
	wallVerts[1].t = endpegt + ((endrealtop - endbot) * endpegmul);

	// set top/bottom coords
	wallVerts[3].y = top;
	wallVerts[2].y = endtop;
	wallVerts[0].y = bot;
	wallVerts[1].y = endbot;

	if (polyflags & PF_Fog)
		HWR_AddTransparentWall(wallVerts, Surf, texnum, noencore, polyflags, true,  HWR_CalcWallLight(lightnum, gl_curline), colormap);
	else if (polyflags & PF_EnvironmentTrans)
		HWR_AddTransparentWall(wallVerts, Surf, texnum, noencore, polyflags, false, HWR_CalcWallLight(lightnum, gl_curline), colormap);
	else
		HWR_ProjectWall(wallVerts, Surf, polyflags, HWR_CalcWallLight(lightnum, gl_curline), colormap);
}

// HWR_DrawSkyWall
// Draw walls into the depth buffer so that anything behind is culled properly
static void HWR_DrawSkyWall(FOutVector *wallVerts, FSurfaceInfo *Surf)
{
	HWR_SetCurrentTexture(NULL);
	// no texture
	wallVerts[3].t = wallVerts[2].t = 0;
	wallVerts[0].t = wallVerts[1].t = 0;
	wallVerts[0].s = wallVerts[3].s = 0;
	wallVerts[2].s = wallVerts[1].s = 0;
	// this no longer sets top/bottom coords, this should be done before calling the function
	HWR_ProjectWall(wallVerts, Surf, PF_Invisible|PF_NoTexture, 255, NULL);
	// PF_Invisible so it's not drawn into the colour buffer
	// PF_NoTexture for no texture
	// PF_Occlude is set in HWR_ProjectWall to draw into the depth buffer
}

// Returns true if the midtexture is visible, and false if... it isn't...
static boolean HWR_BlendMidtextureSurface(FSurfaceInfo *pSurf)
{
	FUINT blendmode = PF_Masked;

	pSurf->PolyColor.s.alpha = 0xFF;

	if (LIKELY(!gl_curline->polyseg))
	{
		if (gl_linedef->blendmode && gl_linedef->blendmode != AST_FOG)
		{
			if (gl_linedef->alpha >= 0 && gl_linedef->alpha < FRACUNIT)
				blendmode = HWR_SurfaceBlend(gl_linedef->blendmode, R_GetLinedefTransTable(gl_linedef->alpha), pSurf);
			else
				blendmode = HWR_GetBlendModeFlag(gl_linedef->blendmode);
		}
		else if (gl_linedef->alpha >= 0 && gl_linedef->alpha < FRACUNIT)
			blendmode = HWR_TranstableToAlpha(R_GetLinedefTransTable(gl_linedef->alpha), pSurf);
	}
	else if (gl_curline->polyseg->translucency > 0)
	{
		// Polyobject translucency is done differently
		if (gl_curline->polyseg->translucency >= NUMTRANSMAPS) // wall not drawn
			return false;
		else
			blendmode = HWR_TranstableToAlpha(gl_curline->polyseg->translucency, pSurf);
	}

	if (blendmode != PF_Masked && pSurf->PolyColor.s.alpha == 0x00)
		return false;

	pSurf->PolyFlags = blendmode;

	return true;
}

//
// HWR_ProcessSeg
// A portion or all of a wall segment will be drawn, from startfrac to endfrac,
//  where 0 is the start of the segment, 1 the end of the segment
// Anything between means the wall segment has been clipped with solidsegs,
//  reducing wall overdraw to a minimum
//
static void HWR_ProcessSeg(void) // Sort of like GLWall::Process in GZDoom
{
	FOutVector wallVerts[4];
	v2d_t vs, ve; // start, end vertices of 2d line (view from above)

	fixed_t worldtop, worldbottom;
	fixed_t worldhigh = 0, worldlow = 0;
	fixed_t worldtopslope, worldbottomslope;
	fixed_t worldhighslope = 0, worldlowslope = 0;
	fixed_t v1x, v1y, v2x, v2y;

	fixed_t h, l; // 3D sides and 2s middle textures
	fixed_t hS, lS;

	boolean tripwire;
	boolean noencore;

	gl_sidedef = gl_curline->sidedef;
	gl_linedef = gl_curline->linedef;

	if (LIKELY(gl_curline->pv1))
	{
		vs.x = ((polyvertex_t *)gl_curline->pv1)->x;
		vs.y = ((polyvertex_t *)gl_curline->pv1)->y;
	}
	else
	{
		vs.x = FIXED_TO_FLOAT(gl_curline->v1->x);
		vs.y = FIXED_TO_FLOAT(gl_curline->v1->y);
	}
	if (LIKELY(gl_curline->pv2))
	{
		ve.x = ((polyvertex_t *)gl_curline->pv2)->x;
		ve.y = ((polyvertex_t *)gl_curline->pv2)->y;
	}
	else
	{
		ve.x = FIXED_TO_FLOAT(gl_curline->v2->x);
		ve.y = FIXED_TO_FLOAT(gl_curline->v2->y);
	}

	v1x = FLOAT_TO_FIXED(vs.x);
	v1y = FLOAT_TO_FIXED(vs.y);
	v2x = FLOAT_TO_FIXED(ve.x);
	v2y = FLOAT_TO_FIXED(ve.y);

#define SLOPEPARAMS(slope, end1, end2, normalheight) \
	end1 = P_GetZAt(slope, v1x, v1y, normalheight); \
	end2 = P_GetZAt(slope, v2x, v2y, normalheight);

	SLOPEPARAMS(gl_frontsector->c_slope, worldtop,    worldtopslope,    gl_frontsector->ceilingheight)
	SLOPEPARAMS(gl_frontsector->f_slope, worldbottom, worldbottomslope, gl_frontsector->floorheight)

	// remember vertices ordering
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	// make a wall polygon (with 2 triangles), using the floor/ceiling heights,
	// and the 2d map coords of start/end vertices
	wallVerts[0].x = wallVerts[3].x = vs.x;
	wallVerts[0].z = wallVerts[3].z = vs.y;
	wallVerts[2].x = wallVerts[1].x = ve.x;
	wallVerts[2].z = wallVerts[1].z = ve.y;

	// x offset the texture
	fixed_t texturehpeg = gl_sidedef->textureoffset + gl_curline->offset;
	float cliplow = (float)texturehpeg;
	float cliphigh = (float)(texturehpeg + (gl_curline->flength*FRACUNIT));

	FUINT lightnum = 255;
	extracolormap_t *colormap = gl_frontsector->extra_colormap;

	FSurfaceInfo Surf = {};
	Surf.PolyColor.s.alpha = 255;

	tripwire = P_IsLineTripWire(gl_linedef);
	noencore = (gl_linedef->flags & ML_TFERLINE);

	if (tripwire == false)
	{
		lightnum = HWR_CalcWallLight(gl_frontsector->lightlevel, gl_curline);
	}

	INT32 gl_midtexture = R_GetTextureNum(gl_sidedef->midtexture);
	GLMapTexture_t *grTex = NULL;

	// two sided line
	if (gl_backsector)
	{
		INT32 gl_toptexture = 0, gl_bottomtexture = 0;

		fixed_t texturevpeg;

		boolean bothceilingssky = false; // turned on if both back and front ceilings are sky
		boolean bothfloorssky = false; // likewise, but for floors

		SLOPEPARAMS(gl_backsector->c_slope, worldhigh, worldhighslope, gl_backsector->ceilingheight)
		SLOPEPARAMS(gl_backsector->f_slope, worldlow,  worldlowslope,  gl_backsector->floorheight)

		if (mapnamespace == MNS_SRB2KART)
		{
			if (!gl_curline->polyseg) // Don't do it for polyobjects
			{
				// Sky Ceilings
				wallVerts[3].y = wallVerts[2].y = FIXED_TO_FLOAT(INT32_MAX);

				if (gl_frontsector->ceilingpic == skyflatnum)
				{
					if (gl_backsector->ceilingpic == skyflatnum)
					{
						// Both front and back sectors are sky, needs skywall from the frontsector's ceiling, but only if the
						// backsector is lower
						if ((worldhigh <= worldtop && worldhighslope <= worldtopslope)// Assuming ESLOPE is always on with my changes
							&& (worldhigh != worldtop || worldhighslope != worldtopslope))
							// Removing the second line above will render more rarely visible skywalls. Example: Cave garden ceiling in Dark race
						{
							wallVerts[0].y = FIXED_TO_FLOAT(worldhigh);
							wallVerts[1].y = FIXED_TO_FLOAT(worldhighslope);
							HWR_DrawSkyWall(wallVerts, &Surf);
						}
					}
					else
					{
						// Only the frontsector is sky, just draw a skywall from the front ceiling
						wallVerts[0].y = FIXED_TO_FLOAT(worldtop);
						wallVerts[1].y = FIXED_TO_FLOAT(worldtopslope);
						HWR_DrawSkyWall(wallVerts, &Surf);
					}
				}
				else if (gl_backsector->ceilingpic == skyflatnum)
				{
					// Only the backsector is sky, just draw a skywall from the front ceiling
					wallVerts[0].y = FIXED_TO_FLOAT(worldtop);
					wallVerts[1].y = FIXED_TO_FLOAT(worldtopslope);
					HWR_DrawSkyWall(wallVerts, &Surf);
				}

				// Sky Floors
				wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(INT32_MIN);

				if (gl_frontsector->floorpic == skyflatnum)
				{
					if (gl_backsector->floorpic == skyflatnum)
					{
						// Both front and back sectors are sky, needs skywall from the backsector's floor, but only if the
						// it's higher, also needs to check for bottomtexture as the floors don't usually move down
						// when both sides are sky floors
						if ((worldlow >= worldbottom && worldlowslope >= worldbottomslope)
							&& (worldlow != worldbottom || worldlowslope != worldbottomslope)
							// Removing the second line above will render more rarely visible skywalls. Example: Cave garden ceiling in Dark race
							&& !(gl_sidedef->bottomtexture))
						{
							wallVerts[3].y = FIXED_TO_FLOAT(worldlow);
							wallVerts[2].y = FIXED_TO_FLOAT(worldlowslope);
							HWR_DrawSkyWall(wallVerts, &Surf);
						}
					}
					else
					{
						// Only the backsector has sky, just draw a skywall from the back floor
						wallVerts[3].y = FIXED_TO_FLOAT(worldbottom);
						wallVerts[2].y = FIXED_TO_FLOAT(worldbottomslope);
						HWR_DrawSkyWall(wallVerts, &Surf);
					}
				}
				else if ((gl_backsector->floorpic == skyflatnum) && !(gl_sidedef->bottomtexture))
				{
					// Only the backsector has sky, just draw a skywall from the back floor if there's no bottomtexture
					wallVerts[3].y = FIXED_TO_FLOAT(worldlow);
					wallVerts[2].y = FIXED_TO_FLOAT(worldlowslope);
					HWR_DrawSkyWall(wallVerts, &Surf);
				}
			}
		}

		// hack to allow height changes in outdoor areas
		// This is what gets rid of the upper textures if there should be sky
		if (gl_frontsector->ceilingpic == skyflatnum
			&& gl_backsector->ceilingpic  == skyflatnum)
		{
			if (mapnamespace != MNS_SRB2KART)
				bothceilingssky = true;
			else
			{
				worldtop = worldhigh;
				worldtopslope = worldhighslope;
			}
		}

		if (mapnamespace != MNS_SRB2KART)
		{
			// likewise, but for floors and upper textures
			if (gl_frontsector->floorpic == skyflatnum
				&& gl_backsector->floorpic == skyflatnum)
			{
				bothfloorssky = true;
			}
		}

		// this is always false in binary anyways
		if (!bothceilingssky)
			gl_toptexture = R_GetTextureNum(gl_sidedef->toptexture);
		if (!bothfloorssky)
			gl_bottomtexture = R_GetTextureNum(gl_sidedef->bottomtexture);

		// check TOP TEXTURE
		if ((worldhighslope < worldtopslope || worldhigh < worldtop) && gl_toptexture)
		{
			// PEGGING
			if (gl_linedef->flags & ML_DONTPEGTOP)
				texturevpeg = 0;
			else if (gl_linedef->flags & ML_SKEWTD)
				texturevpeg = worldhigh + textureheight[gl_toptexture] - worldtop;
			else
				texturevpeg = gl_backsector->ceilingheight + textureheight[gl_toptexture] - gl_frontsector->ceilingheight;

			texturevpeg += gl_sidedef->rowoffset + gl_sidedef->offsety_top;

			// This is so that it doesn't overflow and screw up the wall, it doesn't need to go higher than the texture's height anyway
			texturevpeg %= textureheight[gl_toptexture];

			grTex = HWR_GetTexture(gl_toptexture, noencore);

			wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
			wallVerts[0].t = wallVerts[1].t = (texturevpeg + gl_frontsector->ceilingheight - gl_backsector->ceilingheight) * grTex->scaleY;
			wallVerts[0].s = wallVerts[3].s = (cliplow + gl_sidedef->offsetx_top) * grTex->scaleX;
			wallVerts[2].s = wallVerts[1].s = (cliphigh + gl_sidedef->offsetx_top) * grTex->scaleX;

			// Adjust t value for sloped walls
			if (!(gl_linedef->flags & ML_SKEWTD))
			{
				// Unskewed
				wallVerts[3].t -= (worldtop - gl_frontsector->ceilingheight) * grTex->scaleY;
				wallVerts[2].t -= (worldtopslope - gl_frontsector->ceilingheight) * grTex->scaleY;
				wallVerts[0].t -= (worldhigh - gl_backsector->ceilingheight) * grTex->scaleY;
				wallVerts[1].t -= (worldhighslope - gl_backsector->ceilingheight) * grTex->scaleY;
			}
			else if (gl_linedef->flags & ML_DONTPEGTOP)
			{
				// Skewed by top
				wallVerts[0].t = (texturevpeg + worldtop - worldhigh) * grTex->scaleY;
				wallVerts[1].t = (texturevpeg + worldtopslope - worldhighslope) * grTex->scaleY;
			}
			else
			{
				// Skewed by bottom
				wallVerts[0].t = wallVerts[1].t = (texturevpeg + worldtop - worldhigh) * grTex->scaleY;
				wallVerts[3].t = wallVerts[0].t - (worldtop - worldhigh) * grTex->scaleY;
				wallVerts[2].t = wallVerts[1].t - (worldtopslope - worldhighslope) * grTex->scaleY;
			}

			// set top/bottom coords
			wallVerts[3].y = FIXED_TO_FLOAT(worldtop);
			wallVerts[0].y = FIXED_TO_FLOAT(worldhigh);
			wallVerts[2].y = FIXED_TO_FLOAT(worldtopslope);
			wallVerts[1].y = FIXED_TO_FLOAT(worldhighslope);

			FBITFIELD polyflags = PF_Masked;

			if (grTex->mipmap.flags & TF_TRANSPARENT)
				polyflags = PF_Environment;

			if (gl_frontsector->numlights)
				HWR_SplitWall(gl_frontsector, wallVerts, gl_toptexture, noencore, &Surf, FOF_CUTLEVEL, NULL, polyflags);
			else if (grTex->mipmap.flags & TF_TRANSPARENT)
				HWR_AddTransparentWall(wallVerts, &Surf, gl_toptexture, noencore, polyflags, false, lightnum, colormap);
			else
				HWR_ProjectWall(wallVerts, &Surf, polyflags, lightnum, colormap);
		}

		// check BOTTOM TEXTURE
		if ((worldlowslope > worldbottomslope || worldlow > worldbottom) && gl_bottomtexture)
		{
			// PEGGING
			if (!(gl_linedef->flags & ML_DONTPEGBOTTOM))
				texturevpeg = 0;
			else if (gl_linedef->flags & ML_SKEWTD)
				texturevpeg = worldbottom - worldlow;
			else
				texturevpeg = gl_frontsector->floorheight - gl_backsector->floorheight;

			texturevpeg += gl_sidedef->rowoffset + gl_sidedef->offsety_bot;

			// This is so that it doesn't overflow and screw up the wall, it doesn't need to go higher than the texture's height anyway
			texturevpeg %= textureheight[gl_bottomtexture];

			grTex = HWR_GetTexture(gl_bottomtexture, noencore);

			wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
			wallVerts[0].t = wallVerts[1].t = (texturevpeg + gl_backsector->floorheight - gl_frontsector->floorheight) * grTex->scaleY;
			wallVerts[0].s = wallVerts[3].s = (cliplow + gl_sidedef->offsetx_bot) * grTex->scaleX;
			wallVerts[2].s = wallVerts[1].s = (cliphigh + gl_sidedef->offsetx_bot) * grTex->scaleX;

			// Adjust t value for sloped walls
			if (!(gl_linedef->flags & ML_SKEWTD))
			{
				// Unskewed
				wallVerts[0].t -= (worldbottom - gl_frontsector->floorheight) * grTex->scaleY;
				wallVerts[1].t -= (worldbottomslope - gl_frontsector->floorheight) * grTex->scaleY;
				wallVerts[3].t -= (worldlow - gl_backsector->floorheight) * grTex->scaleY;
				wallVerts[2].t -= (worldlowslope - gl_backsector->floorheight) * grTex->scaleY;
			}
			else if (gl_linedef->flags & ML_DONTPEGBOTTOM)
			{
				// Skewed by bottom
				wallVerts[0].t = wallVerts[1].t = (texturevpeg + worldlow - worldbottom) * grTex->scaleY;
				wallVerts[2].t = wallVerts[1].t - (worldlowslope - worldbottomslope) * grTex->scaleY;
			}
			else
			{
				// Skewed by top
				wallVerts[0].t = (texturevpeg + worldlow - worldbottom) * grTex->scaleY;
				wallVerts[1].t = (texturevpeg + worldlowslope - worldbottomslope) * grTex->scaleY;
			}

			// set top/bottom coords
			wallVerts[3].y = FIXED_TO_FLOAT(worldlow);
			wallVerts[0].y = FIXED_TO_FLOAT(worldbottom);
			wallVerts[2].y = FIXED_TO_FLOAT(worldlowslope);
			wallVerts[1].y = FIXED_TO_FLOAT(worldbottomslope);

			FBITFIELD polyflags = PF_Masked;

			if (grTex->mipmap.flags & TF_TRANSPARENT)
				polyflags = PF_Environment;

			if (gl_frontsector->numlights)
				HWR_SplitWall(gl_frontsector, wallVerts, gl_bottomtexture, noencore, &Surf, FOF_CUTLEVEL, NULL, polyflags);
			else if (grTex->mipmap.flags & TF_TRANSPARENT)
				HWR_AddTransparentWall(wallVerts, &Surf, gl_bottomtexture, noencore, polyflags, false, lightnum, colormap);
			else
				HWR_ProjectWall(wallVerts, &Surf, polyflags, lightnum, colormap);
		}

		// Render midtexture if there's one. Determine if it's visible first, though
		if (gl_midtexture && HWR_BlendMidtextureSurface(&Surf))
		{
			sector_t *front, *back;
			INT32 repeats;

			if (gl_linedef->frontsector->heightsec != -1)
				front = &sectors[gl_linedef->frontsector->heightsec];
			else
				front = gl_linedef->frontsector;

			if (gl_linedef->backsector->heightsec != -1)
				back = &sectors[gl_linedef->backsector->heightsec];
			else
				back = gl_linedef->backsector;

			if (gl_sidedef->repeatcnt)
				repeats = 1 + gl_sidedef->repeatcnt;
			else if (gl_linedef->flags & ML_WRAPMIDTEX)
			{
				fixed_t high, low;

				if (front->ceilingheight > back->ceilingheight)
					high = back->ceilingheight;
				else
					high = front->ceilingheight;

				if (front->floorheight > back->floorheight)
					low = front->floorheight;
				else
					low = back->floorheight;

				repeats = (high - low) / textureheight[gl_midtexture];
				if ((high - low) % textureheight[gl_midtexture])
					repeats++; // tile an extra time to fill the gap -- Monster Iestyn
			}
			else
				repeats = 1;

			// SoM: a little note: popentop and popenbottom
			// record the limits the texture can be displayed in.
			// polytop and polybottom, are the ideal (i.e. unclipped)
			// heights of the polygon, and h & l, are the final (clipped)
			// poly coords.
			fixed_t popentop, popenbottom, polytop, polybottom, lowcut, highcut;
			fixed_t popentopslope, popenbottomslope, polytopslope, polybottomslope, lowcutslope, highcutslope;

			// NOTE: With polyobjects, whenever you need to check the properties of the polyobject sector it belongs to,
			// you must use the linedef's backsector to be correct
			// From CB
			if (gl_curline->polyseg)
			{
				popentop = popentopslope = back->ceilingheight;
				popenbottom = popenbottomslope = back->floorheight;
			}
			else
            {
				popentop = min(worldtop, worldhigh);
				popenbottom = max(worldbottom, worldlow);
				popentopslope = min(worldtopslope, worldhighslope);
				popenbottomslope = max(worldbottomslope, worldlowslope);
			}

			// Find the wall's coordinates
			fixed_t midtexheight = textureheight[gl_midtexture] * repeats;

			// Texture is not skewed
			if (gl_linedef->flags & ML_NOSKEW)
			{
				if (gl_linedef->flags & ML_MIDPEG) // Peg it to the floor
				{
					polybottom = max(front->floorheight, back->floorheight) + gl_sidedef->rowoffset + gl_sidedef->offsety_mid;
					polytop = polybottom + midtexheight;
				}
				else // Peg it to the ceiling
				{
					polytop = min(front->ceilingheight, back->ceilingheight) + gl_sidedef->rowoffset + gl_sidedef->offsety_mid;
					polybottom = polytop - midtexheight;
				}

				// The right side's coordinates are the the same as the left side
				polytopslope = polytop;
				polybottomslope = polybottom;
			}
			else if (gl_linedef->flags & ML_MIDPEG) // Skew the texture, but peg it to the floor
			{
				polybottom = popenbottom + gl_sidedef->rowoffset + gl_sidedef->offsety_mid;
				polytop = polybottom + midtexheight;
				polybottomslope = popenbottomslope + gl_sidedef->rowoffset + gl_sidedef->offsety_mid;
				polytopslope = polybottomslope + midtexheight;
			}
			else // Skew it according to the ceiling's slope
			{
				polytop = popentop + gl_sidedef->rowoffset;
				polybottom = polytop - midtexheight;
				polytopslope = popentopslope + gl_sidedef->rowoffset;
				polybottomslope = polytopslope - midtexheight;
			}

			// CB
			// NOTE: With polyobjects, whenever you need to check the properties of the polyobject sector it belongs to,
			// you must use the linedef's backsector to be correct
			if (gl_curline->polyseg)
			{
				lowcut = polybottom;
				highcut = polytop;
				lowcutslope = polybottomslope;
				highcutslope = polytopslope;
			}
			else
			{
				// The cut-off values of a linedef can always be constant, since every line has an absoulute front and or back sector
				lowcut = popenbottom;
				highcut = popentop;
				lowcutslope = popenbottomslope;
				highcutslope = popentopslope;
			}

			h = min(highcut, polytop);
			l = max(polybottom, lowcut);
			hS = min(highcutslope, polytopslope);
			lS = max(polybottomslope, lowcutslope);

			// PEGGING
			fixed_t texturevpegslope;

			if (gl_linedef->flags & ML_MIDPEG)
			{
				texturevpeg = midtexheight - h + polybottom;
				texturevpegslope = midtexheight - hS + polybottomslope;
			}
			else
			{
				texturevpeg = polytop - h;
				texturevpegslope = polytopslope - hS;
			}

			grTex = HWR_GetTexture(gl_midtexture, noencore);

			// Left side
			wallVerts[3].t = texturevpeg * grTex->scaleY;
			wallVerts[0].t = (h - l + texturevpeg) * grTex->scaleY;
			wallVerts[0].s = wallVerts[3].s = (cliplow + gl_sidedef->offsetx_mid) * grTex->scaleX;

			// Right side
			wallVerts[2].t = texturevpegslope * grTex->scaleY;
			wallVerts[1].t = (hS - lS + texturevpegslope) * grTex->scaleY;
			wallVerts[2].s = wallVerts[1].s = (cliphigh + gl_sidedef->offsetx_mid) * grTex->scaleX;

			// set top/bottom coords
			// Take the texture peg into account, rather than changing the offsets past
			// where the polygon might not be.
			wallVerts[3].y = FIXED_TO_FLOAT(h);
			wallVerts[0].y = FIXED_TO_FLOAT(l);
			wallVerts[2].y = FIXED_TO_FLOAT(hS);
			wallVerts[1].y = FIXED_TO_FLOAT(lS);

			// TODO: Actually use the surface's flags so that I don't have to do this
			FUINT blendmode = Surf.PolyFlags;

			if (mapnamespace != MNS_SRB2KART)
			{
				// Correct to account for slopes
				fixed_t midtextureslant;

				if (gl_linedef->flags & ML_NOSKEW)
					midtextureslant = 0;
				else if (gl_linedef->flags & ML_MIDPEG)
					midtextureslant = worldlow < worldbottom
							  ? worldbottomslope-worldbottom
							  : worldlowslope-worldlow;
				else
					midtextureslant = worldtop < worldhigh
							  ? worldtopslope-worldtop
							  : worldhighslope-worldhigh;

				polytop += midtextureslant;
				polybottom += midtextureslant;

				highcut += worldtop < worldhigh
						 ? worldtopslope-worldtop
						 : worldhighslope-worldhigh;
				lowcut += worldlow < worldbottom
						? worldbottomslope-worldbottom
						: worldlowslope-worldlow;

				// Texture stuff
				h = min(highcut, polytop);
				l = max(polybottom, lowcut);

				{
					// PEGGING
					if (gl_linedef->flags & ML_MIDPEG)
						texturevpeg = textureheight[gl_sidedef->midtexture]*repeats - h + polybottom;
					else
						texturevpeg = polytop - h;
					wallVerts[2].t = texturevpeg * grTex->scaleY;
					wallVerts[1].t = (h - l + texturevpeg) * grTex->scaleY;
				}

				wallVerts[2].y = FIXED_TO_FLOAT(h);
				wallVerts[1].y = FIXED_TO_FLOAT(l);
			}

			// Render midtextures on two-sided lines with a z-buffer offset.
			// This will cause the midtexture appear on top, if a FOF overlaps with it.
			blendmode |= PF_Decal;

			if (tripwire == false && gl_frontsector->numlights)
			{
				if (!(blendmode & PF_Masked))
					HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, noencore, &Surf, FOF_TRANSLUCENT, NULL, blendmode); // vanilla just uses PF_Masked here - if we run into any issues, maybe change to that
				else
				{
					HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, noencore, &Surf, FOF_CUTLEVEL, NULL, blendmode); // vanilla just uses PF_Masked here - if we run into any issues, maybe change to that
				}
			}
			else if (!(blendmode & PF_Masked))
				HWR_AddTransparentWall(wallVerts, &Surf, gl_midtexture,  noencore, blendmode, false, lightnum, colormap);
			else
				HWR_ProjectWall(wallVerts, &Surf, blendmode, lightnum, colormap);
		}

		// Sky culling
		// No longer so much a mess as before!
		// It now is a mess again, thx kart!
		// see above!
		if (mapnamespace != MNS_SRB2KART)
		{
			if (!gl_curline->polyseg) // Don't do it for polyobjects
			{
				if (gl_frontsector->ceilingpic == skyflatnum
					&& gl_backsector->ceilingpic != skyflatnum) // don't cull if back sector is also sky
				{
					wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(INT32_MAX); // draw to top of map space
					wallVerts[0].y = FIXED_TO_FLOAT(worldtop);
					wallVerts[1].y = FIXED_TO_FLOAT(worldtopslope);
					HWR_DrawSkyWall(wallVerts, &Surf);
				}

				if (gl_frontsector->floorpic == skyflatnum
					&& gl_backsector->floorpic != skyflatnum) // same thing here
				{
					wallVerts[3].y = FIXED_TO_FLOAT(worldbottom);
					wallVerts[2].y = FIXED_TO_FLOAT(worldbottomslope);
					wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(INT32_MIN); // draw to bottom of map space
					HWR_DrawSkyWall(wallVerts, &Surf);
				}
			}
		}
	}
	else
	{
		// Single sided line... Deal only with the middletexture (if one exists)
		if (gl_midtexture && gl_linedef->special != HORIZONSPECIAL) // (Ignore horizon line for OGL)
		{
			fixed_t texturevpeg;

			// PEGGING
			if ((gl_linedef->flags & (ML_DONTPEGBOTTOM|ML_NOSKEW)) == (ML_DONTPEGBOTTOM|ML_NOSKEW))
				texturevpeg = gl_frontsector->floorheight + textureheight[gl_sidedef->midtexture] - gl_frontsector->ceilingheight + gl_sidedef->rowoffset + gl_sidedef->offsety_mid;
			else if (gl_linedef->flags & ML_DONTPEGBOTTOM)
				texturevpeg = worldbottom + textureheight[gl_sidedef->midtexture] - worldtop + gl_sidedef->rowoffset + gl_sidedef->offsety_mid;
			else
				// top of texture at top
				texturevpeg = gl_sidedef->rowoffset + gl_sidedef->offsety_mid;

			grTex = HWR_GetTexture(gl_midtexture, noencore);

			wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
			wallVerts[0].t = wallVerts[1].t = (texturevpeg + gl_frontsector->ceilingheight - gl_frontsector->floorheight) * grTex->scaleY;
			wallVerts[0].s = wallVerts[3].s = (cliplow + gl_sidedef->offsetx_mid) * grTex->scaleX;
			wallVerts[2].s = wallVerts[1].s = (cliphigh + gl_sidedef->offsetx_mid) * grTex->scaleX;

			// Texture correction for slopes
			if (gl_linedef->flags & ML_NOSKEW) {
				wallVerts[3].t += (gl_frontsector->ceilingheight - worldtop) * grTex->scaleY;
				wallVerts[2].t += (gl_frontsector->ceilingheight - worldtopslope) * grTex->scaleY;
				wallVerts[0].t += (gl_frontsector->floorheight - worldbottom) * grTex->scaleY;
				wallVerts[1].t += (gl_frontsector->floorheight - worldbottomslope) * grTex->scaleY;
			} else if (gl_linedef->flags & ML_DONTPEGBOTTOM) {
				wallVerts[3].t = wallVerts[0].t + (worldbottom-worldtop) * grTex->scaleY;
				wallVerts[2].t = wallVerts[1].t + (worldbottomslope-worldtopslope) * grTex->scaleY;
			} else {
				wallVerts[0].t = wallVerts[3].t - (worldbottom-worldtop) * grTex->scaleY;
				wallVerts[1].t = wallVerts[2].t - (worldbottomslope-worldtopslope) * grTex->scaleY;
			}

			//Set textures properly on single sided walls that are sloped
			wallVerts[3].y = FIXED_TO_FLOAT(worldtop);
			wallVerts[0].y = FIXED_TO_FLOAT(worldbottom);
			wallVerts[2].y = FIXED_TO_FLOAT(worldtopslope);
			wallVerts[1].y = FIXED_TO_FLOAT(worldbottomslope);

			// I don't think that solid walls can use translucent linedef types...
			if (gl_frontsector->numlights)
				HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, noencore, &Surf, FOF_CUTLEVEL, NULL, 0);
			else
			{
				FBITFIELD blendmode = PF_Masked;
				if (grTex->mipmap.flags & TF_TRANSPARENT)
					blendmode = PF_Environment;

				// I don't think that solid walls can use translucent linedef types...
				if (gl_frontsector->numlights)
					HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, noencore, &Surf, FOF_CUTLEVEL, NULL, blendmode);
				else
				{
					if (grTex->mipmap.flags & TF_TRANSPARENT)
						HWR_AddTransparentWall(wallVerts, &Surf, gl_midtexture,  noencore, blendmode, false, lightnum, colormap);
					else
						HWR_ProjectWall(wallVerts, &Surf, blendmode, lightnum, colormap);
				}
			}
		}
		else if (mapnamespace == MNS_SRB2KART)
		{
			//Set textures properly on single sided walls that are sloped
			wallVerts[3].y = FIXED_TO_FLOAT(worldtop);
			wallVerts[0].y = FIXED_TO_FLOAT(worldbottom);
			wallVerts[2].y = FIXED_TO_FLOAT(worldtopslope);
			wallVerts[1].y = FIXED_TO_FLOAT(worldbottomslope);

			// When there's no midtexture, draw a skywall to prevent rendering behind it
			HWR_DrawSkyWall(wallVerts, &Surf);
		}

		if (!gl_curline->polyseg)
		{
			if (gl_frontsector->ceilingpic == skyflatnum) // It's a single-sided line with sky for its sector
			{
				wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(INT32_MAX); // draw to top of map space
				wallVerts[0].y = FIXED_TO_FLOAT(worldtop);
				wallVerts[1].y = FIXED_TO_FLOAT(worldtopslope);
				HWR_DrawSkyWall(wallVerts, &Surf);
			}
			if (gl_frontsector->floorpic == skyflatnum)
			{
				wallVerts[3].y = FIXED_TO_FLOAT(worldbottom);
				wallVerts[2].y = FIXED_TO_FLOAT(worldbottomslope);
				wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(INT32_MIN); // draw to bottom of map space
				HWR_DrawSkyWall(wallVerts, &Surf);
			}
		}
	}


	//Hurdler: 3d-floors test
	if (gl_backsector && !Tag_Compare(&gl_frontsector->tags, &gl_backsector->tags) && (gl_backsector->ffloors || gl_frontsector->ffloors))
	{
		ffloor_t * rover;
		fixed_t    highcut = 0, lowcut = 0;
		fixed_t lowcutslope, highcutslope;

		// Used for height comparisons and etc across FOFs and slopes
		fixed_t high1, highslope1, low1, lowslope1;
		fixed_t high2, highslope2, low2, lowslope2;

		INT32 texnum;
		line_t * newline = NULL; // Multi-Property FOF

		lowcut = max(worldbottom, worldlow);
		highcut = min(worldtop, worldhigh);
		lowcutslope = max(worldbottomslope, worldlowslope);
		highcutslope = min(worldtopslope, worldhighslope);

		if (gl_backsector->ffloors)
		{
			for (rover = gl_backsector->ffloors; rover; rover = rover->next)
			{
				if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_RENDERSIDES))
					continue;
				if (!(rover->fofflags & FOF_ALLSIDES) && rover->fofflags & FOF_INVERTSIDES)
					continue;

				SLOPEPARAMS(*rover->t_slope, high1, highslope1, *rover->topheight)
				SLOPEPARAMS(*rover->b_slope, low1,  lowslope1,  *rover->bottomheight)

				if ((high1 < lowcut || highslope1 < lowcutslope) || (low1 > highcut || lowslope1 > highcutslope))
					continue;

				ffloor_t * r2;
				for (r2 = gl_frontsector->ffloors; r2; r2 = r2->next)
				{
					if (r2->master == rover->master) // Skip if same control line.
						break;

					const ffloortype_e r2flags = r2->fofflags;

					if (!(r2flags & FOF_EXISTS) || !(r2flags & FOF_RENDERSIDES))
						continue;

					if (rover->fofflags & FOF_EXTRA)
					{
						if (!(r2flags & FOF_CUTEXTRA))
							continue;

						if (r2flags & FOF_EXTRA && (r2flags & (FOF_TRANSLUCENT|FOF_FOG)) != (rover->fofflags & (FOF_TRANSLUCENT|FOF_FOG)))
							continue;
					}
					else
					{
						if (!(r2flags & FOF_CUTSOLIDS))
							continue;
					}

					SLOPEPARAMS(*r2->t_slope, high2, highslope2, *r2->topheight)
					SLOPEPARAMS(*r2->b_slope, low2,  lowslope2,  *r2->bottomheight)

					if ((high2 < lowcut || highslope2 < lowcutslope) || (low2 > highcut || lowslope2 > highcutslope))
						continue;
					if ((high1 > high2 || highslope1 > highslope2) || (low1 < low2 || lowslope1 < lowslope2))
						continue;

					break;
				}

				if (r2)
					continue;

				texnum = R_GetTextureNum(sides[rover->master->sidenum[0]].midtexture);

				if (rover->master->flags & ML_TFERLINE)
				{
					size_t linenum = min(gl_curline->linedef-gl_backsector->lines[0], (long int)rover->master->frontsector->linecount);
					newline = rover->master->frontsector->lines[0] + linenum;
					texnum = R_GetTextureNum(sides[newline->sidenum[0]].midtexture);
				}

				h  = P_GetFFloorTopZAt   (rover, v1x, v1y);
				hS = P_GetFFloorTopZAt   (rover, v2x, v2y);
				l  = P_GetFFloorBottomZAt(rover, v1x, v1y);
				lS = P_GetFFloorBottomZAt(rover, v2x, v2y);
				// Adjust the heights so the FOF does not overlap with top and bottom textures.
				if (h >= highcut && hS >= highcutslope)
				{
					h = highcut;
					hS = highcutslope;
				}
				if (l <= lowcut && lS <= lowcutslope)
				{
					l = lowcut;
					lS = lowcutslope;
				}
				//Hurdler: HW code starts here
				//FIXME: check if peging is correct
				// set top/bottom coords

				wallVerts[3].y = FIXED_TO_FLOAT(h);
				wallVerts[2].y = FIXED_TO_FLOAT(hS);
				wallVerts[0].y = FIXED_TO_FLOAT(l);
				wallVerts[1].y = FIXED_TO_FLOAT(lS);
				if (rover->fofflags & FOF_FOG)
				{
					wallVerts[3].t = wallVerts[2].t = 0;
					wallVerts[0].t = wallVerts[1].t = 0;
					wallVerts[0].s = wallVerts[3].s = 0;
					wallVerts[2].s = wallVerts[1].s = 0;
				}
				else
				{
					fixed_t texturevpeg;
					boolean attachtobottom = false;
					boolean slopeskew = false; // skew FOF walls with slopes?

					// Wow, how was this missing from OpenGL for so long?
					// ...Oh well, anyway, Lower Unpegged now changes pegging of FOFs like in software
					// -- Monster Iestyn 26/06/18
					if (newline)
					{
						texturevpeg = sides[newline->sidenum[0]].rowoffset + sides[newline->sidenum[0]].offsety_mid;
						attachtobottom = !!(newline->flags & ML_DONTPEGBOTTOM);
						slopeskew = !!(newline->flags & ML_SKEWTD);
					}
					else
					{
						texturevpeg = sides[rover->master->sidenum[0]].rowoffset + sides[rover->master->sidenum[0]].offsety_mid;
						attachtobottom = !!(gl_linedef->flags & ML_DONTPEGBOTTOM);
						slopeskew = !!(rover->master->flags & ML_SKEWTD);
					}

					grTex = HWR_GetTexture(texnum, noencore);

					if (!slopeskew) // no skewing
					{
						if (attachtobottom)
							texturevpeg -= *rover->topheight - *rover->bottomheight;
						wallVerts[3].t = (*rover->topheight - h + texturevpeg) * grTex->scaleY;
						wallVerts[2].t = (*rover->topheight - hS + texturevpeg) * grTex->scaleY;
						wallVerts[0].t = (*rover->topheight - l + texturevpeg) * grTex->scaleY;
						wallVerts[1].t = (*rover->topheight - lS + texturevpeg) * grTex->scaleY;
					}
					else
					{
						if (!attachtobottom) // skew by top
						{
							wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
							wallVerts[0].t = (h - l + texturevpeg) * grTex->scaleY;
							wallVerts[1].t = (hS - lS + texturevpeg) * grTex->scaleY;
						}
						else // skew by bottom
						{
							wallVerts[0].t = wallVerts[1].t = texturevpeg * grTex->scaleY;
							wallVerts[3].t = wallVerts[0].t - (h - l) * grTex->scaleY;
							wallVerts[2].t = wallVerts[1].t - (hS - lS) * grTex->scaleY;
						}
					}

					wallVerts[0].s = wallVerts[3].s = (cliplow + gl_sidedef->offsetx_mid) * grTex->scaleX;
					wallVerts[2].s = wallVerts[1].s = (cliphigh + gl_sidedef->offsetx_mid) * grTex->scaleX;
				}
				if (rover->fofflags & FOF_FOG)
				{
					FBITFIELD blendmode;

					blendmode = PF_Fog|PF_NoTexture;

					lightnum = HWR_CalcWallLight(rover->master->frontsector->lightlevel, gl_curline);
					colormap = rover->master->frontsector->extra_colormap;

					Surf.PolyColor.s.alpha = HWR_FogBlockAlpha(rover->master->frontsector->lightlevel, rover->master->frontsector->extra_colormap);

					if (gl_frontsector->numlights)
						HWR_SplitWall(gl_frontsector, wallVerts, 0, false, &Surf, rover->fofflags, rover, blendmode);
					else
						HWR_AddTransparentWall(wallVerts, &Surf, 0, false ,blendmode, true, lightnum, colormap);
				}
				else
				{
					FBITFIELD blendmode = PF_Masked;

					if (rover->fofflags & FOF_TRANSLUCENT)
					{
						if (rover->alpha < 256 || rover->blend)
						{
							blendmode = HWR_GetBlendModeFlag(rover->blend);
							Surf.PolyColor.s.alpha = (UINT8)(max(0, min(rover->alpha, 255)));
						}
					}

					if (gl_frontsector->numlights)
						HWR_SplitWall(gl_frontsector, wallVerts, texnum, noencore, &Surf, rover->fofflags, rover, blendmode);
					else
					{
						if (blendmode != PF_Masked)
							HWR_AddTransparentWall(wallVerts, &Surf, texnum, noencore, blendmode, false, lightnum, colormap);
						else
							HWR_ProjectWall(wallVerts, &Surf, PF_Masked, lightnum, colormap);
					}
				}
			}
		}

		if (gl_frontsector->ffloors) // Putting this seperate should allow 2 FOF sectors to be connected without too many errors? I think?
		{
			for (rover = gl_frontsector->ffloors; rover; rover = rover->next)
			{
				if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_RENDERSIDES))
					continue;
				if (!(rover->fofflags & FOF_ALLSIDES || rover->fofflags & FOF_INVERTSIDES))
					continue;

				SLOPEPARAMS(*rover->t_slope, high1, highslope1, *rover->topheight)
				SLOPEPARAMS(*rover->b_slope, low1,  lowslope1,  *rover->bottomheight)

				if ((high1 < lowcut || highslope1 < lowcutslope) || (low1 > highcut || lowslope1 > highcutslope))
					continue;

				ffloor_t * r2;
				for (r2 = gl_backsector->ffloors; r2; r2 = r2->next)
				{
					if (r2->master == rover->master) // Skip if same control line.
						break;

					const ffloortype_e r2flags = r2->fofflags;

					if (!(r2flags & FOF_EXISTS) || !(r2flags & FOF_RENDERSIDES))
						continue;

					if (rover->fofflags & FOF_EXTRA)
					{
						if (!(r2flags & FOF_CUTEXTRA))
							continue;

						if (r2flags & FOF_EXTRA && (r2flags & (FOF_TRANSLUCENT|FOF_FOG)) != (rover->fofflags & (FOF_TRANSLUCENT|FOF_FOG)))
							continue;
					}
					else
					{
						if (!(r2flags & FOF_CUTSOLIDS))
							continue;
					}

					SLOPEPARAMS(*r2->t_slope, high2, highslope2, *r2->topheight)
					SLOPEPARAMS(*r2->b_slope, low2,  lowslope2,  *r2->bottomheight)

					if ((high2 < lowcut || highslope2 < lowcutslope) || (low2 > highcut || lowslope2 > highcutslope))
						continue;
					if ((high1 > high2 || highslope1 > highslope2) || (low1 < low2 || lowslope1 < lowslope2))
						continue;

					break;
				}
				if (r2)
					continue;

				texnum = R_GetTextureNum(sides[rover->master->sidenum[0]].midtexture);

				if (rover->master->flags & ML_TFERLINE)
				{
					size_t linenum = min(gl_curline->linedef-gl_backsector->lines[0], (long int)rover->master->frontsector->linecount);
					newline = rover->master->frontsector->lines[0] + linenum;
					texnum = R_GetTextureNum(sides[newline->sidenum[0]].midtexture);
				}
				h  = P_GetFFloorTopZAt   (rover, v1x, v1y);
				hS = P_GetFFloorTopZAt   (rover, v2x, v2y);
				l  = P_GetFFloorBottomZAt(rover, v1x, v1y);
				lS = P_GetFFloorBottomZAt(rover, v2x, v2y);
				// Adjust the heights so the FOF does not overlap with top and bottom textures.
				if (h >= highcut && hS >= highcutslope)
				{
					h = highcut;
					hS = highcutslope;
				}
				if (l <= lowcut && lS <= lowcutslope)
				{
					l = lowcut;
					lS = lowcutslope;
				}
				//Hurdler: HW code starts here
				//FIXME: check if peging is correct
				// set top/bottom coords

				wallVerts[3].y = FIXED_TO_FLOAT(h);
				wallVerts[2].y = FIXED_TO_FLOAT(hS);
				wallVerts[0].y = FIXED_TO_FLOAT(l);
				wallVerts[1].y = FIXED_TO_FLOAT(lS);
				if (rover->fofflags & FOF_FOG)
				{
					wallVerts[3].t = wallVerts[2].t = 0;
					wallVerts[0].t = wallVerts[1].t = 0;
					wallVerts[0].s = wallVerts[3].s = 0;
					wallVerts[2].s = wallVerts[1].s = 0;
				}
				else
				{
					grTex = HWR_GetTexture(texnum, noencore);

					if (newline)
					{
						wallVerts[3].t = wallVerts[2].t = (*rover->topheight - h + sides[newline->sidenum[0]].rowoffset + sides[newline->sidenum[0]].offsety_mid) * grTex->scaleY;
						wallVerts[0].t = wallVerts[1].t = (h - l + (*rover->topheight - h + sides[newline->sidenum[0]].rowoffset) + sides[newline->sidenum[0]].offsety_mid) * grTex->scaleY;
					}
					else
					{
						wallVerts[3].t = wallVerts[2].t = (*rover->topheight - h + sides[rover->master->sidenum[0]].rowoffset + sides[rover->master->sidenum[0]].offsety_mid) * grTex->scaleY;
						wallVerts[0].t = wallVerts[1].t = (h - l + (*rover->topheight - h + sides[rover->master->sidenum[0]].rowoffset + sides[rover->master->sidenum[0]].offsety_mid)) * grTex->scaleY;
					}

					wallVerts[0].s = wallVerts[3].s = (cliplow + gl_sidedef->offsetx_mid) * grTex->scaleX;
					wallVerts[2].s = wallVerts[1].s = (cliphigh + gl_sidedef->offsetx_mid) * grTex->scaleX;
				}

				if (rover->fofflags & FOF_FOG)
				{
					FBITFIELD blendmode;

					blendmode = PF_Fog|PF_NoTexture;

					lightnum = HWR_CalcWallLight(rover->master->frontsector->lightlevel, gl_curline);
					colormap = rover->master->frontsector->extra_colormap;

					Surf.PolyColor.s.alpha = HWR_FogBlockAlpha(rover->master->frontsector->lightlevel, rover->master->frontsector->extra_colormap);

					if (gl_backsector->numlights)
						HWR_SplitWall(gl_backsector, wallVerts, 0, false, &Surf, rover->fofflags, rover, blendmode);
					else
						HWR_AddTransparentWall(wallVerts, &Surf, 0, false, blendmode, true, lightnum, colormap);
				}
				else
				{
					FBITFIELD blendmode = PF_Masked;

					if (rover->fofflags & FOF_TRANSLUCENT)
					{
						if (rover->alpha < 256 || rover->blend)
						{
							blendmode = HWR_GetBlendModeFlag(rover->blend);
							Surf.PolyColor.s.alpha = (UINT8)(max(0, min(rover->alpha, 255)));
						}
					}

					if (gl_backsector->numlights)
						HWR_SplitWall(gl_backsector, wallVerts, texnum, noencore, &Surf, rover->fofflags, rover, blendmode);
					else
					{
						if (blendmode != PF_Masked)
							HWR_AddTransparentWall(wallVerts, &Surf, texnum, noencore, blendmode, false, lightnum, colormap);
						else
							HWR_ProjectWall(wallVerts, &Surf, PF_Masked, lightnum, colormap);
					}
				}
			}
		}
	}
#undef SLOPEPARAMS
//Hurdler: end of 3d-floors test
}

// From PrBoom:
//
// e6y: Check whether the player can look beyond this line
//
boolean checkforemptylines = true;

// Don't modify anything here, just check
// Kalaron: Modified for sloped linedefs
static boolean CheckClip(seg_t * seg, sector_t * afrontsector, sector_t * abacksector)
{
	fixed_t frontf1,frontf2, frontc1, frontc2; // front floor/ceiling ends
	fixed_t backf1, backf2, backc1, backc2; // back floor ceiling ends

	boolean bothceilingssky = false, bothfloorssky = false;

	if (mapnamespace != MNS_SRB2KART)
	{
		if (abacksector->ceilingpic == skyflatnum && afrontsector->ceilingpic == skyflatnum)
			bothceilingssky = true;
		if (abacksector->floorpic == skyflatnum && afrontsector->floorpic == skyflatnum)
			bothfloorssky = true;
	}

	// GZDoom method of sloped line clipping

	if (afrontsector->f_slope || afrontsector->c_slope || abacksector->f_slope || abacksector->c_slope)
	{
		fixed_t v1x, v1y, v2x, v2y; // the seg's vertexes as fixed_t
		if (LIKELY(gl_curline->pv1))
		{
			v1x = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv1)->x);
			v1y = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv1)->y);
		}
		else
		{
			v1x = gl_curline->v1->x;
			v1y = gl_curline->v1->y;
		}
		if (LIKELY(gl_curline->pv2))
		{
			v2x = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv2)->x);
			v2y = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv2)->y);
		}
		else
		{
			v2x = gl_curline->v2->x;
			v2y = gl_curline->v2->y;
		}
#define SLOPEPARAMS(slope, end1, end2, normalheight) \
		end1 = P_GetZAt(slope, v1x, v1y, normalheight); \
		end2 = P_GetZAt(slope, v2x, v2y, normalheight);

		SLOPEPARAMS(afrontsector->f_slope, frontf1, frontf2, afrontsector->  floorheight)
		SLOPEPARAMS(afrontsector->c_slope, frontc1, frontc2, afrontsector->ceilingheight)
		SLOPEPARAMS( abacksector->f_slope,  backf1,  backf2,  abacksector->  floorheight)
		SLOPEPARAMS( abacksector->c_slope,  backc1,  backc2,  abacksector->ceilingheight)
#undef SLOPEPARAMS
	}
	else
	{
		frontf1 = frontf2 = afrontsector->  floorheight;
		frontc1 = frontc2 = afrontsector->ceilingheight;
		backf1  =  backf2 =  abacksector->  floorheight;
		backc1  =  backc2 =  abacksector->ceilingheight;
	}

	// properly render skies (consider door "open" if both ceilings are sky)
	// same for floors
	// lug: but only on kart maps :chaosleep:
	// kart does some funky crap on its binary maps and thus using this code breaks many maps visually and generally makes performance really bad
	if (mapnamespace != MNS_SRB2KART)
	{
		if (!bothceilingssky && !bothfloorssky)
		{
			// now check for closed sectors!
			if ((backc1 <= frontf1 && backc2 <= frontf2)
				|| (backf1 >= frontc1 && backf2 >= frontc2))
			{
				checkforemptylines = false;
				return true;
			}

			if (backc1 <= backf1 && backc2 <= backf2)
			{
				// preserve a kind of transparent door/lift special effect:
				if (((backc1 >= frontc1 && backc2 >= frontc2) || seg->sidedef->toptexture)
				&& ((backf1 <= frontf1 && backf2 <= frontf2) || seg->sidedef->bottomtexture))
				{
					checkforemptylines = false;
					return true;
				}
			}
		}

		if (!bothceilingssky)
		{
			if (backc1 != frontc1 || backc2 != frontc2)
			{
				checkforemptylines = false;
				return false;
			}
		}

		if (!bothfloorssky)
		{
			if (backf1 != frontf1 || backf2 != frontf2)
			{
				checkforemptylines = false;
				return false;
			}
		}
	}
	else
	{
		// not in vanilla kart, but this makes noclipcam alot more bearable when its stuck in walls
		// otherwise it may cull half your view which is not really desirable
		if (afrontsector == viewsector || abacksector == viewsector)
		{
			fixed_t viewf1, viewf2, viewc1, viewc2;
			if (afrontsector == viewsector)
			{
				viewf1 = frontf1;
				viewf2 = frontf2;
				viewc1 = frontc1;
				viewc2 = frontc2;
			}
			else
			{
				viewf1 = backf1;
				viewf2 = backf2;
				viewc1 = backc1;
				viewc2 = backc2;
			}

			// check if camera is outside the bounds of the floor and the ceiling (noclipping)
			// either above the ceiling or below the floor
			if ((viewz > viewc1 && viewz > viewc2) || (viewz < viewf1 && viewz < viewf2))
				return false;
		}

		// now check for closed sectors!

		// here we're talking about a CEILING lower than a floor. ...yeah we don't even need to bother.
		if (backc1 <= frontf1 && backc2 <= frontf2)
		{
			checkforemptylines = false;
			return true;
		}

		// here we're talking about floors higher than ceilings, don't even bother either.
		if (backf1 >= frontc1 && backf2 >= frontc2)
		{
			checkforemptylines = false;
			return true;
		}

		// Lat: Ok, here's what we need to do, we want to draw thok barriers. Let's define what a thok barrier is;
		// -Must have ceilheight <= floorheight
		// -ceilpic must be skyflatnum
		// -an adjacant sector needs to have a ceilingheight or a floor height different than the one we have, otherwise, it's just a huge ass wall, we shouldn't render past it.
		// -said adjacant sector cannot also be a thok barrier, because that's also dumb and we could render far more than we need to as a result :V

		if (backc1 <= backf1 && backc2 <= backf2)
		{
			checkforemptylines = false;

			// before we do anything, if both sectors are thok barriers, GET ME OUT OF HERE!
			if (frontc1 <= backc1 && frontc2 <= backc2)
				return true;	// STOP RENDERING.

			// draw floors at the top of thok barriers:
			if (backc1 < frontc1 || backc2 < frontc2)
				return false;

			if (backf1 > frontf1 || backf2 > frontf2)
				return false;

			return true;
		}

		// Window.
		// We know it's a window when the above isn't true and the back and front sectors don't match
		if (backc1 != frontc1 || backc2 != frontc2 || backf1 != frontf1 || backf2 != frontf2)
		{
			checkforemptylines = false;
			return false;
		}
	}

	return false;
}

// -----------------+
// HWR_AddLine      : Clips the given segment and adds any visible pieces to the line list.
// Notes            : gl_cursectorlight is set to the current subsector -> sector -> light value
//                  : (it may be mixed with the wall's own flat colour in the future ...)
// -----------------+
static void HWR_AddLine(seg_t * line)
{
	angle_t angle1, angle2;

	// SoM: Backsector needs to be run through R_FakeFlat
	static sector_t tempsec;

	fixed_t v1x, v1y, v2x, v2y; // the seg's vertexes as fixed_t
	if (line->polyseg && !(line->polyseg->flags & POF_RENDERSIDES))
		return;

	gl_curline = line;

	if (LIKELY(gl_curline->pv1))
	{
		v1x = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv1)->x);
		v1y = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv1)->y);
	}
	else
	{
		v1x = gl_curline->v1->x;
		v1y = gl_curline->v1->y;
	}
	if (LIKELY(gl_curline->pv2))
	{
		v2x = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv2)->x);
		v2y = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv2)->y);
	}
	else
	{
		v2x = gl_curline->v2->x;
		v2y = gl_curline->v2->y;
	}

	// OPTIMIZE: quickly reject orthogonal back sides.
	angle1 = R_PointToAngle64(v1x, v1y);
	angle2 = R_PointToAngle64(v2x, v2y);

	 // PrBoom: Back side, i.e. backface culling - read: endAngle >= startAngle!
	if (angle2 - angle1 < ANGLE_180)
		return;

	// PrBoom: use REAL clipping math YAYYYYYYY!!!

	if (!gld_clipper_SafeCheckRange(angle2, angle1))
	{
		return;
	}

	checkforemptylines = true;
	gl_backsector = line->backsector;

	if (!line->backsector)
	{
		gld_clipper_SafeAddClipRange(angle2, angle1);
	}
	else
	{
		boolean bothceilingssky = false, bothfloorssky = false;

		gl_backsector = R_FakeFlat(gl_backsector, &tempsec, NULL, NULL, true);

		if (mapnamespace != MNS_SRB2KART) // same here, thisll most likely break kart maps
		{
			if (gl_backsector->ceilingpic == skyflatnum && gl_frontsector->ceilingpic == skyflatnum)
				bothceilingssky = true;
			if (gl_backsector->floorpic == skyflatnum && gl_frontsector->floorpic == skyflatnum)
				bothfloorssky = true;

			if (bothceilingssky && bothfloorssky) // everything's sky? let's save us a bit of time then
			{
				if (!line->polyseg &&
					!line->sidedef->midtexture
					&& ((!gl_frontsector->ffloors && !gl_backsector->ffloors)
						|| Tag_Compare(&gl_frontsector->tags, &gl_backsector->tags)))
					return; // line is empty, don't even bother
				// treat like wide open window instead
				HWR_ProcessSeg(); // Doesn't need arguments because they're defined globally :D
				return;
			}
		}

		if (CheckClip(line, gl_frontsector, gl_backsector))
		{
			gld_clipper_SafeAddClipRange(angle2, angle1);
			checkforemptylines = false;
		}
		// Reject empty lines used for triggers and special events.
		// Identical floor and ceiling on both sides,
		//  identical light levels on both sides,
		//  and no middle texture.
		if (checkforemptylines && R_IsEmptyLine(line, gl_frontsector, gl_backsector))
			return;
	}

	HWR_ProcessSeg(); // Doesn't need arguments because they're defined globally :D
	return;
}

// HWR_CheckBBox
// Checks BSP node/subtree bounding box.
// Returns true
//  if some part of the bbox might be visible.
//
// modified to use local variables

static boolean HWR_CheckBBox(const fixed_t *bspcoord)
{
	INT32 boxpos;
	fixed_t px1, py1, px2, py2;
	angle_t angle1, angle2;

	// Find the corners of the box
	// that define the edges from current viewpoint.
	if (dup_viewx <= bspcoord[BOXLEFT])
		boxpos = 0;
	else if (dup_viewx < bspcoord[BOXRIGHT])
		boxpos = 1;
	else
		boxpos = 2;

	if (dup_viewy >= bspcoord[BOXTOP])
		boxpos |= 0;
	else if (dup_viewy > bspcoord[BOXBOTTOM])
		boxpos |= 1<<2;
	else
		boxpos |= 2<<2;

	if (boxpos == 5)
		return true;

	px1 = bspcoord[checkcoord[boxpos][0]];
	py1 = bspcoord[checkcoord[boxpos][1]];
	px2 = bspcoord[checkcoord[boxpos][2]];
	py2 = bspcoord[checkcoord[boxpos][3]];

	angle1 = R_PointToAngle64(px1, py1);
	angle2 = R_PointToAngle64(px2, py2);

	return gld_clipper_SafeCheckRange(angle2, angle1);
}

//
// HWR_AddPolyObjectSegs
//
// haleyjd 02/19/06
// Adds all segs in all polyobjects in the given subsector.
// Modified for hardware rendering.
//
static inline void HWR_AddPolyObjectSegs(void)
{
	size_t i, j;
	seg_t gl_fakeline;
	polyvertex_t pv1;
	polyvertex_t pv2;

	// Sort through all the polyobjects
	for (i = 0; i < numpolys; ++i)
	{
		// Render the polyobject's lines
		for (j = 0; j < po_ptrs[i]->segCount; ++j)
		{
			// Copy the info of a polyobject's seg, then convert it to OpenGL floating point
			memcpy(&gl_fakeline, po_ptrs[i]->segs[j], sizeof(seg_t));

			// Now convert the line to float and add it to be rendered
			pv1.x = FIXED_TO_FLOAT(gl_fakeline.v1->x);
			pv1.y = FIXED_TO_FLOAT(gl_fakeline.v1->y);
			pv2.x = FIXED_TO_FLOAT(gl_fakeline.v2->x);
			pv2.y = FIXED_TO_FLOAT(gl_fakeline.v2->y);

			gl_fakeline.pv1 = &pv1;
			gl_fakeline.pv2 = &pv2;

			HWR_AddLine(&gl_fakeline);
		}
	}
}

static void HWR_AddPolyObjectPlanes(void)
{
	size_t i;
	sector_t *polyobjsector;
	INT32 light = 0;

	// Polyobject Planes need their own function for drawing because they don't have extrasubsectors by themselves
	// It should be okay because polyobjects should always be convex anyway

	for (i  = 0; i < numpolys; i++)
	{
		polyobjsector = po_ptrs[i]->lines[0]->backsector; // the in-level polyobject sector

		if (!(po_ptrs[i]->flags & POF_RENDERPLANES)) // Only render planes when you should
			continue;

		if (po_ptrs[i]->translucency >= NUMTRANSMAPS)
			continue;

		if (polyobjsector->floorheight <= gl_frontsector->ceilingheight
			&& polyobjsector->floorheight >= gl_frontsector->floorheight
			&& (viewz < polyobjsector->floorheight))
		{
			light = R_GetPlaneLight(gl_frontsector, polyobjsector->floorheight, true);
			if (po_ptrs[i]->translucency > 0)
			{
				FSurfaceInfo Surf = {};
				FBITFIELD blendmode;
				blendmode = HWR_TranstableToAlpha(po_ptrs[i]->translucency, &Surf);
				HWR_AddTransparentPolyobjectFloor(&levelflats[polyobjsector->floorpic], po_ptrs[i], false, polyobjsector->floorheight,
													(light == -1 ? gl_frontsector->lightlevel : *gl_frontsector->lightlist[light].lightlevel), Surf.PolyColor.s.alpha, polyobjsector, blendmode, (light == -1 ? gl_frontsector->extra_colormap : *gl_frontsector->lightlist[light].extra_colormap));
			}
			else
			{
				HWR_GetLevelFlat(&levelflats[polyobjsector->floorpic], R_NoEncore(polyobjsector, &levelflats[polyobjsector->floorpic], false));
				HWR_RenderPolyObjectPlane(po_ptrs[i], false, polyobjsector->floorheight, PF_Occlude,
										(light == -1 ? gl_frontsector->lightlevel : *gl_frontsector->lightlist[light].lightlevel), &levelflats[polyobjsector->floorpic],
										polyobjsector, 255, (light == -1 ? gl_frontsector->extra_colormap : *gl_frontsector->lightlist[light].extra_colormap));
			}
		}

		if (polyobjsector->ceilingheight >= gl_frontsector->floorheight
			&& polyobjsector->ceilingheight <= gl_frontsector->ceilingheight
			&& (viewz > polyobjsector->ceilingheight))
		{
			light = R_GetPlaneLight(gl_frontsector, polyobjsector->ceilingheight, true);
			if (po_ptrs[i]->translucency > 0)
			{
				FSurfaceInfo Surf = {};
				FBITFIELD blendmode;
				blendmode = HWR_TranstableToAlpha(po_ptrs[i]->translucency, &Surf);
				HWR_AddTransparentPolyobjectFloor(&levelflats[polyobjsector->ceilingpic], po_ptrs[i], true, polyobjsector->ceilingheight,
				                                  (light == -1 ? gl_frontsector->lightlevel : *gl_frontsector->lightlist[light].lightlevel), Surf.PolyColor.s.alpha, polyobjsector, blendmode, (light == -1 ? gl_frontsector->extra_colormap : *gl_frontsector->lightlist[light].extra_colormap));
			}
			else
			{
				HWR_GetLevelFlat(&levelflats[polyobjsector->ceilingpic], R_NoEncore(polyobjsector, &levelflats[polyobjsector->ceilingpic], true));
				HWR_RenderPolyObjectPlane(po_ptrs[i], true, polyobjsector->ceilingheight, PF_Occlude,
				                          (light == -1 ? gl_frontsector->lightlevel : *gl_frontsector->lightlist[light].lightlevel), &levelflats[polyobjsector->ceilingpic],
				                          polyobjsector, 255, (light == -1 ? gl_frontsector->extra_colormap : *gl_frontsector->lightlist[light].extra_colormap));
			}
		}
	}
}

static FBITFIELD HWR_RippleBlend(sector_t *sector, ffloor_t *rover, boolean ceiling)
{
	return R_IsRipplePlane(sector, rover, ceiling) ? PF_Ripple : 0;
}

// -----------------+
// HWR_Subsector    : Determine floor/ceiling planes.
//                  : Add sprites of things in sector.
//                  : Draw one or more line segments.
// Notes            : Sets gl_cursectorlight to the light of the parent sector, to modulate wall textures
// -----------------+
static void HWR_Subsector(size_t num)
{
	INT16 count;
	seg_t *line;
	subsector_t *sub;
	static sector_t tempsec; //SoM: 4/7/2000
	INT32 floorlightlevel;
	INT32 ceilinglightlevel;
	INT32 locFloorHeight, locCeilingHeight;
	INT32 cullFloorHeight, cullCeilingHeight;
	INT32 light = 0;
	extracolormap_t *floorcolormap;
	extracolormap_t *ceilingcolormap;
	ffloor_t *rover;

#ifdef PARANOIA //no risk while developing, enough debugging nights!
	if (num >= addsubsector)
		I_Error("HWR_Subsector: ss %s with numss = %s, addss = %s\n",
			sizeu1(num), sizeu2(numsubsectors), sizeu3(addsubsector));

	/*if (num >= numsubsectors)
		I_Error("HWR_Subsector: ss %i with numss = %i",
		        num,
		        numsubsectors);*/
#endif

	if (num < numsubsectors)
	{
		// subsector
		sub = &subsectors[num];
		// sector
		gl_frontsector = sub->sector;
		// how many linedefs
		count = sub->numlines;
		// first line seg
		line = &segs[sub->firstline];
	}
	else
	{
		// there are no segs but only planes
		sub = &subsectors[0];
		gl_frontsector = sub->sector;
		count = 0;
		line = NULL;
	}

	//SoM: 4/7/2000: Test to make Boom water work in Hardware mode.
	gl_frontsector = R_FakeFlat(gl_frontsector, &tempsec, &floorlightlevel,
								&ceilinglightlevel, false);
	//FIXME: Use floorlightlevel and ceilinglightlevel insted of lightlevel.

	floorcolormap = ceilingcolormap = gl_frontsector->extra_colormap;

	// ------------------------------------------------------------------------
	// sector lighting, DISABLED because it's done in HWR_StoreWallRange
	// ------------------------------------------------------------------------
	/// \todo store a RGBA instead of just intensity, allow coloured sector lighting
	//light = (FUBYTE)(sub->sector->lightlevel & 0xFF) / 255.0f;
	//gl_cursectorlight.red   = light;
	//gl_cursectorlight.green = light;
	//gl_cursectorlight.blue  = light;
	//gl_cursectorlight.alpha = light;

// ----- end special tricks -----
	cullFloorHeight   = P_GetSectorFloorZAt  (gl_frontsector, viewx, viewy);
	cullCeilingHeight = P_GetSectorCeilingZAt(gl_frontsector, viewx, viewy);
	locFloorHeight    = P_GetSectorFloorZAt  (gl_frontsector, gl_frontsector->soundorg.x, gl_frontsector->soundorg.y);
	locCeilingHeight  = P_GetSectorCeilingZAt(gl_frontsector, gl_frontsector->soundorg.x, gl_frontsector->soundorg.y);

	if (gl_frontsector->ffloors)
	{
		boolean anyMoved = gl_frontsector->moved;

		if (anyMoved == false)
		{
			for (rover = gl_frontsector->ffloors; rover; rover = rover->next)
			{
				sector_t *controlSec = &sectors[rover->secnum];
				if (controlSec->moved == true)
				{
					anyMoved = true;
					break;
				}
			}
		}

		if (anyMoved == true)
		{
			gl_frontsector->numlights = sub->sector->numlights = 0;
			R_Prep3DFloors(gl_frontsector, locCeilingHeight);
			sub->sector->lightlist = gl_frontsector->lightlist;
			sub->sector->numlights = gl_frontsector->numlights;
			sub->sector->moved = gl_frontsector->moved = false;
		}

		light = R_GetPlaneLight(gl_frontsector, locFloorHeight, false);
		if (gl_frontsector->floorlightsec == -1 && !gl_frontsector->floorlightabsolute)
			floorlightlevel = max(0, min(255, *gl_frontsector->lightlist[light].lightlevel + gl_frontsector->floorlightlevel));
		floorcolormap = *gl_frontsector->lightlist[light].extra_colormap;

		light = R_GetPlaneLight(gl_frontsector, locCeilingHeight, false);
		if (gl_frontsector->ceilinglightsec == -1 && !gl_frontsector->ceilinglightabsolute)
			ceilinglightlevel = max(0, min(255, *gl_frontsector->lightlist[light].lightlevel + gl_frontsector->ceilinglightlevel));
		ceilingcolormap = *gl_frontsector->lightlist[light].extra_colormap;
	}

	sub->sector->extra_colormap = gl_frontsector->extra_colormap;

	// render floor ?
	// yeah, easy backface cull! :)
	if (cullFloorHeight < dup_viewz)
	{
		if (gl_frontsector->floorpic != skyflatnum)
		{
			if (sub->validcount != validcount)
			{
				HWR_GetLevelFlat(&levelflats[gl_frontsector->floorpic], R_NoEncore(gl_frontsector, &levelflats[gl_frontsector->floorpic], false));
				HWR_RenderPlane(sub, &extrasubsectors[num], false,
					// Hack to make things continue to work around slopes.
					locFloorHeight == cullFloorHeight ? locFloorHeight : gl_frontsector->floorheight,
					// We now return you to your regularly scheduled rendering.
					PF_Occlude | HWR_RippleBlend(gl_frontsector, NULL, false),
					floorlightlevel, &levelflats[gl_frontsector->floorpic], NULL, 255, floorcolormap);
			}
		}
	}

	if (cullCeilingHeight > dup_viewz)
	{
		if (gl_frontsector->ceilingpic != skyflatnum)
		{
			if (sub->validcount != validcount)
			{
				HWR_GetLevelFlat(&levelflats[gl_frontsector->ceilingpic], R_NoEncore(gl_frontsector, &levelflats[gl_frontsector->ceilingpic], true));
				HWR_RenderPlane(sub, &extrasubsectors[num], true,
					// Hack to make things continue to work around slopes.
					locCeilingHeight == cullCeilingHeight ? locCeilingHeight : gl_frontsector->ceilingheight,
					// We now return you to your regularly scheduled rendering.
					PF_Occlude | HWR_RippleBlend(gl_frontsector, NULL, true),
					ceilinglightlevel, &levelflats[gl_frontsector->ceilingpic], NULL, 255, ceilingcolormap);
			}
		}
	}

	if (gl_frontsector->ffloors)
	{
		/// \todo fix light, xoffs, yoffs, extracolormap ?
		for (rover = gl_frontsector->ffloors;
			rover; rover = rover->next)
		{
			fixed_t cullHeight, centerHeight;

            // bottom plane
			cullHeight   = P_GetFFloorBottomZAt(rover, viewx, viewy);
			centerHeight = P_GetFFloorBottomZAt(rover, gl_frontsector->soundorg.x, gl_frontsector->soundorg.y);

			if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_RENDERPLANES))
				continue;
			if (sub->validcount == validcount)
				continue;

			if (centerHeight <= locCeilingHeight &&
			    centerHeight >= locFloorHeight &&
			    ((dup_viewz < cullHeight && (rover->fofflags & FOF_BOTHPLANES || !(rover->fofflags & FOF_INVERTPLANES))) ||
			     (dup_viewz > cullHeight && (rover->fofflags & FOF_BOTHPLANES || rover->fofflags & FOF_INVERTPLANES))))
			{
				if (rover->fofflags & FOF_FOG)
				{
					UINT8 alpha;

					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);
					alpha = HWR_FogBlockAlpha(*gl_frontsector->lightlist[light].lightlevel, rover->master->frontsector->extra_colormap);

					HWR_AddTransparentFloor(0,
					                       &extrasubsectors[num],
										   false,
					                       *rover->bottomheight,
					                       *gl_frontsector->lightlist[light].lightlevel,
					                       alpha, rover->master->frontsector, PF_Fog|PF_NoTexture,
										   true, rover->master->frontsector->extra_colormap);
				}
				else if (rover->fofflags & FOF_TRANSLUCENT
					&& (rover->alpha < 256 || rover->blend)) // SoM: Flags are more efficient
				{
					FBITFIELD blendmode = HWR_GetBlendModeFlag(rover->blend) | HWR_RippleBlend(gl_frontsector, rover, false);

					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);

					HWR_AddTransparentFloor(&levelflats[*rover->bottompic],
					                       &extrasubsectors[num],
										   false,
					                       *rover->bottomheight,
					                       *gl_frontsector->lightlist[light].lightlevel,
					                       max(0, min(rover->alpha, 255)), rover->master->frontsector, blendmode,
					                       false, *gl_frontsector->lightlist[light].extra_colormap);
				}
				else
				{
					HWR_GetLevelFlat(&levelflats[*rover->bottompic], R_NoEncore(gl_frontsector, &levelflats[*rover->bottompic], false));
					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);
					HWR_RenderPlane(sub, &extrasubsectors[num], false, *rover->bottomheight, HWR_RippleBlend(gl_frontsector, rover, false) | PF_Occlude, *gl_frontsector->lightlist[light].lightlevel, &levelflats[*rover->bottompic],
					                rover->master->frontsector, 255, *gl_frontsector->lightlist[light].extra_colormap);
				}
			}

			// top plane
			cullHeight   = P_GetFFloorTopZAt(rover, viewx, viewy);
			centerHeight = P_GetFFloorTopZAt(rover, gl_frontsector->soundorg.x, gl_frontsector->soundorg.y);

			if (centerHeight >= locFloorHeight &&
			    centerHeight <= locCeilingHeight &&
			    ((dup_viewz > cullHeight && (rover->fofflags & FOF_BOTHPLANES || !(rover->fofflags & FOF_INVERTPLANES))) ||
			     (dup_viewz < cullHeight && (rover->fofflags & FOF_BOTHPLANES || rover->fofflags & FOF_INVERTPLANES))))
			{
				if (rover->fofflags & FOF_FOG)
				{
					UINT8 alpha;

					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);
					alpha = HWR_FogBlockAlpha(*gl_frontsector->lightlist[light].lightlevel, rover->master->frontsector->extra_colormap);

					HWR_AddTransparentFloor(0,
					                       &extrasubsectors[num],
										   true,
					                       *rover->topheight,
					                       *gl_frontsector->lightlist[light].lightlevel,
					                       alpha, rover->master->frontsector, PF_Fog|PF_NoTexture,
										   true, rover->master->frontsector->extra_colormap);
				}
				else if (rover->fofflags & FOF_TRANSLUCENT
					&& (rover->alpha < 256 || rover->blend)) // SoM: Flags are more efficient
				{
					FBITFIELD blendmode = HWR_GetBlendModeFlag(rover->blend) | HWR_RippleBlend(gl_frontsector, rover, false);

					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);

					HWR_AddTransparentFloor(&levelflats[*rover->toppic],
					                        &extrasubsectors[num],
											true,
					                        *rover->topheight,
					                        *gl_frontsector->lightlist[light].lightlevel,
					                        max(0, min(rover->alpha, 255)), rover->master->frontsector, blendmode,
					                        false, *gl_frontsector->lightlist[light].extra_colormap);
				}
				else
				{
					HWR_GetLevelFlat(&levelflats[*rover->toppic], R_NoEncore(gl_frontsector, &levelflats[*rover->toppic], true));
					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);
					HWR_RenderPlane(sub, &extrasubsectors[num], true, *rover->topheight, HWR_RippleBlend(gl_frontsector, rover, true) | PF_Occlude, *gl_frontsector->lightlist[light].lightlevel, &levelflats[*rover->toppic],
					                  rover->master->frontsector, 255, *gl_frontsector->lightlist[light].extra_colormap);
				}
			}
		}
	}

	// Draw all the polyobjects in this subsector
	if (sub->polyList)
	{
		polyobj_t *po = sub->polyList;

		numpolys = 0;

		// Count all the polyobjects, reset the list, and recount them
		while (po)
		{
			++numpolys;
			po = (polyobj_t *)(po->link.next);
		}

		// for render stats
		ps_numpolyobjects += numpolys;

		// Sort polyobjects
		R_SortPolyObjects(sub);

		// Draw polyobject lines.
		HWR_AddPolyObjectSegs();

		if (sub->validcount != validcount) // This validcount situation seems to let us know that the floors have already been drawn.
		{
			// Draw polyobject planes
			HWR_AddPolyObjectPlanes();
		}
	}

// Hurder ici se passe les choses INT32�essantes!
// on vient de tracer le sol et le plafond
// on trace �pr�ent d'abord les sprites et ensuite les murs
// hurdler: faux: on ajoute seulement les sprites, le murs sont trac� d'abord
	if (line)
	{
		if (sub->sector->validcount != validcount)
		{
			// draw sprites first, coz they are clipped to the solidsegs of
			// subsectors more 'in front'
			HWR_AddSprites(gl_frontsector);
		}

		//Hurdler: at this point validcount must be the same, but is not because
		//         gl_frontsector doesn't point anymore to sub->sector due to
		//         the call gl_frontsector = R_FakeFlat(...)
		//         if it's not done, the sprite is drawn more than once,
		//         what looks really bad with translucency or dynamic light,
		//         without talking about the overdraw of course.
		sub->sector->validcount = validcount;/// \todo fix that in a better way

		while (count--)
		{
			if (LIKELY(!line->glseg && !line->polyseg)) // ignore segs that belong to polyobjects
				HWR_AddLine(line);
			line++;
		}
	}

	sub->validcount = validcount;
}

//
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.
static void HWR_RenderBSPNode(INT32 bspnum)
{
	const node_t *bsp;
	INT32 side;
	ps_numbspcalls++;

	while (!(bspnum & NF_SUBSECTOR))  // Found a subsector?
	{
		bsp = &nodes[bspnum];

		// Decide which side the view point is on.
		side = R_PointOnSideFast(viewx, viewy, bsp);

		// Recursively divide front space.
		HWR_RenderBSPNode(bsp->children[side]);

        // Possibly divide back space
        if (!(HWR_CheckBBox(bsp->bbox[side^1])))
            return;

		bspnum = bsp->children[side^1];
	}

	// e6y: support for extended nodes
	HWR_Subsector(bspnum == -1 ? 0 : bspnum & ~NF_SUBSECTOR);
}

// A drawnode is something that points to a 3D floor, 3D side, or masked
// middle texture. This is used for sorting with sprites.
typedef struct
{
	FOutVector    wallVerts[4];
	FSurfaceInfo  Surf;
	INT32         texnum;
	FBITFIELD     blend;
	INT32         drawcount;
	boolean fogwall;
	boolean noencore;
	INT32 lightlevel;
	extracolormap_t *wallcolormap; // Doing the lighting in HWR_RenderWall now for correct fog after sorting
} wallinfo_t;

static wallinfo_t *wallinfo = NULL;
static size_t numwalls = 0; // a list of transparent walls to be drawn

void HWR_RenderWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blend, boolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap);

#define MAX_TRANSPARENTWALL 256

typedef struct
{
	extrasubsector_t *xsub;
	boolean isceiling;
	fixed_t fixedheight;
	INT32 lightlevel;
	levelflat_t *levelflat;
	INT32 alpha;
	sector_t *FOFSector;
	FBITFIELD blend;
	boolean fogplane;
	extracolormap_t *planecolormap;
	INT32 drawcount;
} planeinfo_t;

static size_t numplanes = 0; // a list of transparent floors to be drawn
static planeinfo_t *planeinfo = NULL;

typedef struct
{
	polyobj_t *polysector;
	boolean isceiling;
	fixed_t fixedheight;
	INT32 lightlevel;
	levelflat_t *levelflat;
	INT32 alpha;
	sector_t *FOFSector;
	FBITFIELD blend;
	extracolormap_t *planecolormap;
	INT32 drawcount;
} polyplaneinfo_t;

static size_t numpolyplanes = 0; // a list of transparent poyobject floors to be drawn
static polyplaneinfo_t *polyplaneinfo = NULL;

//Hurdler: 3D water sutffs
typedef struct gl_drawnode_s
{
	planeinfo_t *plane;
	polyplaneinfo_t *polyplane;
	wallinfo_t *wall;
	gl_vissprite_t *sprite;

//	struct gl_drawnode_s *next;
//	struct gl_drawnode_s *prev;
} gl_drawnode_t;

static INT32 drawcount = 0;

#define MAX_TRANSPARENTFLOOR 512

// This will likely turn into a copy of HWR_Add3DWater and replace it.
void HWR_AddTransparentFloor(levelflat_t *levelflat, extrasubsector_t *xsub, boolean isceiling, fixed_t fixedheight, INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, boolean fogplane, extracolormap_t *planecolormap)
{
	static size_t allocedplanes = 0;

	// Force realloc if buffer has been freed
	if (!planeinfo)
		allocedplanes = 0;

	if (allocedplanes < numplanes + 1)
	{
		allocedplanes += MAX_TRANSPARENTFLOOR;
		Z_Realloc(planeinfo, allocedplanes * sizeof (*planeinfo), PU_LEVEL, &planeinfo);
	}

	planeinfo[numplanes].isceiling = isceiling;
	planeinfo[numplanes].fixedheight = fixedheight;
	planeinfo[numplanes].lightlevel = ((planecolormap && (planecolormap->flags & CMF_FOG)) || mapnamespace == MNS_SRB2KART) ? lightlevel : 255;
	planeinfo[numplanes].levelflat = levelflat;
	planeinfo[numplanes].xsub = xsub;
	planeinfo[numplanes].alpha = alpha;
	planeinfo[numplanes].FOFSector = FOFSector;
	planeinfo[numplanes].blend = blend;
	planeinfo[numplanes].fogplane = fogplane;
	planeinfo[numplanes].planecolormap = planecolormap;
	planeinfo[numplanes].drawcount = drawcount++;

	numplanes++;
}

// Adding this for now until I can create extrasubsector info for polyobjects
// When that happens it'll just be done through HWR_AddTransparentFloor and HWR_RenderPlane
void HWR_AddTransparentPolyobjectFloor(levelflat_t *levelflat, polyobj_t *polysector, boolean isceiling, fixed_t fixedheight, INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, extracolormap_t *planecolormap)
{
	static size_t allocedpolyplanes = 0;

	// Force realloc if buffer has been freed
	if (!polyplaneinfo)
		allocedpolyplanes = 0;

	if (allocedpolyplanes < numpolyplanes + 1)
	{
		allocedpolyplanes += MAX_TRANSPARENTFLOOR;
		Z_Realloc(polyplaneinfo, allocedpolyplanes * sizeof (*polyplaneinfo), PU_LEVEL, &polyplaneinfo);
	}

	polyplaneinfo[numpolyplanes].isceiling = isceiling;
	polyplaneinfo[numpolyplanes].fixedheight = fixedheight;
	polyplaneinfo[numpolyplanes].lightlevel = ((planecolormap && (planecolormap->flags & CMF_FOG)) || mapnamespace == MNS_SRB2KART) ? lightlevel : 255;
	polyplaneinfo[numpolyplanes].levelflat = levelflat;
	polyplaneinfo[numpolyplanes].polysector = polysector;
	polyplaneinfo[numpolyplanes].alpha = alpha;
	polyplaneinfo[numpolyplanes].FOFSector = FOFSector;
	polyplaneinfo[numpolyplanes].blend = blend;
	polyplaneinfo[numpolyplanes].planecolormap = planecolormap;
	polyplaneinfo[numpolyplanes].drawcount = drawcount++;
	numpolyplanes++;
}

// putting sortindex and sortnode here so the comparator function can see them
gl_drawnode_t *sortnode;
size_t *sortindex;

static int CompareDrawNodes(const void *p1, const void *p2)
{
	size_t n1 = *(const size_t*)p1;
	size_t n2 = *(const size_t*)p2;
	INT32 v1 = 0;
	INT32 v2 = 0;
	INT32 diff;
	if (sortnode[n1].plane)
		v1 = sortnode[n1].plane->drawcount;
	else if (sortnode[n1].polyplane)
		v1 = sortnode[n1].polyplane->drawcount;
	else if (sortnode[n1].wall)
		v1 = sortnode[n1].wall->drawcount;
	else I_Error("CompareDrawNodes: n1 unknown");

	if (sortnode[n2].plane)
		v2 = sortnode[n2].plane->drawcount;
	else if (sortnode[n2].polyplane)
		v2 = sortnode[n2].polyplane->drawcount;
	else if (sortnode[n2].wall)
		v2 = sortnode[n2].wall->drawcount;
	else I_Error("CompareDrawNodes: n2 unknown");

	diff = v2 - v1;
	if (diff == 0) I_Error("CompareDrawNodes: diff is zero");
	return diff;
}

static int CompareDrawNodePlanes(const void *p1, const void *p2)
{
	size_t n1 = *(const size_t*)p1;
	size_t n2 = *(const size_t*)p2;
	if (!sortnode[n1].plane) I_Error("CompareDrawNodePlanes: Uh.. This isn't a plane! (n1)");
	if (!sortnode[n2].plane) I_Error("CompareDrawNodePlanes: Uh.. This isn't a plane! (n2)");
	return ABS(sortnode[n2].plane->fixedheight - viewz) - ABS(sortnode[n1].plane->fixedheight - viewz);
}

//
// HWR_CreateDrawNodes
// Creates and sorts a list of drawnodes for the scene being rendered.
static void HWR_CreateDrawNodes(void)
{
	UINT32 i = 0, p = 0;
	size_t run_start = 0;

	// Dump EVERYTHING into a huge drawnode list. Then we'll sort it!
	// Could this be optimized into _AddTransparentWall/_AddTransparentPlane?
	// Hell yes! But sort algorithm must be modified to use a linked list.
	sortnode = Z_Calloc((sizeof(planeinfo_t)*numplanes)
					+ (sizeof(polyplaneinfo_t)*numpolyplanes)
					+ (sizeof(wallinfo_t)*numwalls)
					,PU_STATIC, NULL);
	// todo:
	// However, in reality we shouldn't be re-copying and shifting all this information
	// that is already lying around. This should all be in some sort of linked list or lists.
	sortindex = Z_Calloc(sizeof(size_t) * (numplanes + numpolyplanes + numwalls), PU_STATIC, NULL);

	ps_hw_nodesorttime = I_GetPreciseTime();

	for (i = 0; i < numplanes; i++, p++)
	{
		sortnode[p].plane = &planeinfo[i];
		sortindex[p] = p;
	}

	for (i = 0; i < numpolyplanes; i++, p++)
	{
		sortnode[p].polyplane = &polyplaneinfo[i];
		sortindex[p] = p;
	}

	for (i = 0; i < numwalls; i++, p++)
	{
		sortnode[p].wall = &wallinfo[i];
		sortindex[p] = p;
	}

	ps_numdrawnodes = p;

	// p is the number of stuff to sort

	// sort the list based on the value of the 'drawcount' member of the drawnodes.
	qs22j(sortindex, p, sizeof(size_t), CompareDrawNodes);

	// an additional pass is needed to correct the order of consecutive planes in the list.
	// for each consecutive run of planes in the list, sort that run based on plane height and view height.
	while (run_start < p-1)// p-1 because a 1 plane run at the end of the list does not count
	{
		// locate run start
		if (sortnode[sortindex[run_start]].plane)
		{
			// found it, now look for run end
			size_t run_end;// (inclusive)
			for (i = run_start+1; i < p; i++)// size_t and UINT32 being used mixed here... shouldnt break anything though..
			{
				if (!sortnode[sortindex[i]].plane) break;
			}
			run_end = i-1;
			if (run_end > run_start)// if there are multiple consecutive planes, not just one
			{
				// consecutive run of planes found, now sort it
				qs22j(sortindex + run_start, run_end - run_start + 1, sizeof(size_t), CompareDrawNodePlanes);
			}
			run_start = run_end + 1;// continue looking for runs coming right after this one
		}
		else
		{
			// this wasnt the run start, try next one
			run_start++;
		}
	}

	ps_hw_nodesorttime = I_GetPreciseTime() - ps_hw_nodesorttime;

	ps_hw_nodedrawtime = I_GetPreciseTime();

	// Okay! Let's draw it all! Woo!
	GL_SetTransform(&atransform);

	for (i = 0; i < p; i++)
	{
		if (sortnode[sortindex[i]].plane)
		{
			// We aren't traversing the BSP tree, so make gl_frontsector null to avoid crashes.
			gl_frontsector = NULL;

			if (!(sortnode[sortindex[i]].plane->blend & PF_NoTexture))
				HWR_GetLevelFlat(sortnode[sortindex[i]].plane->levelflat, R_NoEncore(sortnode[sortindex[i]].plane->FOFSector, sortnode[sortindex[i]].plane->levelflat, sortnode[sortindex[i]].plane->isceiling));
			HWR_RenderPlane(NULL, sortnode[sortindex[i]].plane->xsub, sortnode[sortindex[i]].plane->isceiling, sortnode[sortindex[i]].plane->fixedheight, sortnode[sortindex[i]].plane->blend, sortnode[sortindex[i]].plane->lightlevel,
				sortnode[sortindex[i]].plane->levelflat, sortnode[sortindex[i]].plane->FOFSector, sortnode[sortindex[i]].plane->alpha, sortnode[sortindex[i]].plane->planecolormap);
		}
		else if (sortnode[sortindex[i]].polyplane)
		{
			// We aren't traversing the BSP tree, so make gl_frontsector null to avoid crashes.
			gl_frontsector = NULL;

			if (!(sortnode[sortindex[i]].polyplane->blend & PF_NoTexture))
				HWR_GetLevelFlat(sortnode[sortindex[i]].polyplane->levelflat, R_NoEncore(sortnode[sortindex[i]].polyplane->FOFSector, sortnode[sortindex[i]].polyplane->levelflat, sortnode[sortindex[i]].polyplane->isceiling));
			HWR_RenderPolyObjectPlane(sortnode[sortindex[i]].polyplane->polysector, sortnode[sortindex[i]].polyplane->isceiling, sortnode[sortindex[i]].polyplane->fixedheight, sortnode[sortindex[i]].polyplane->blend, sortnode[sortindex[i]].polyplane->lightlevel,
				sortnode[sortindex[i]].polyplane->levelflat, sortnode[sortindex[i]].polyplane->FOFSector, sortnode[sortindex[i]].polyplane->alpha, sortnode[sortindex[i]].polyplane->planecolormap);
		}
		else if (sortnode[sortindex[i]].wall)
		{
			if (!(sortnode[sortindex[i]].wall->blend & PF_NoTexture))
				HWR_GetTexture(sortnode[sortindex[i]].wall->texnum, sortnode[sortindex[i]].wall->noencore);
			HWR_RenderWall(sortnode[sortindex[i]].wall->wallVerts, &sortnode[sortindex[i]].wall->Surf, sortnode[sortindex[i]].wall->blend, sortnode[sortindex[i]].wall->fogwall,
				sortnode[sortindex[i]].wall->lightlevel, sortnode[sortindex[i]].wall->wallcolormap);
		}
	}

	ps_hw_nodedrawtime = I_GetPreciseTime() - ps_hw_nodedrawtime;

	numwalls = 0;
	numplanes = 0;
	numpolyplanes = 0;

	// No mem leaks, please.
	Z_Free(sortnode);
	Z_Free(sortindex);
}

// -----------------+
// HWR_ClearView : clear the viewwindow, with maximum z value
// -----------------+
static inline void HWR_ClearView(void)
{
	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	/// \bug faB - enable depth mask, disable color mask

	GL_GClipRect((INT32)gl_viewwindowx,
	                 (INT32)gl_viewwindowy,
	                 (INT32)(gl_viewwindowx + gl_viewwidth),
	                 (INT32)(gl_viewwindowy + gl_viewheight),
	                 ZCLIP_PLANE);
	GL_ClearBuffer(false, true, 0);

	//disable clip window - set to full size
	// rem by Hurdler
	// GL_GClipRect(0, 0, vid.width, vid.height);
}


// -----------------+
// HWR_SetViewSize  : set projection and scaling values
// -----------------+
void HWR_SetViewSize(void)
{
	// setup view size
	gl_viewwidth = (float)vid.width;
	gl_viewheight = (float)vid.height;

	if (r_splitscreen > 0)
	{
		gl_viewheight /= 2;

		if (r_splitscreen > 1)
		{
			gl_viewwidth /= 2;
		}
	}

	gl_basecenterx = gl_viewwidth / 2;
	gl_basecentery = gl_viewheight / 2;

	gl_baseviewwindowx = 0;
	gl_basewindowcenterx = gl_viewwidth / 2;

	gl_baseviewwindowy = 0;
	gl_basewindowcentery = gl_viewheight / 2;

	gl_pspritexscale = gl_viewwidth / BASEVIDWIDTH;
	gl_pspriteyscale = ((vid.height*gl_pspritexscale*BASEVIDWIDTH)/BASEVIDHEIGHT)/vid.width;

	GL_FlushScreenTextures();
}

// -------------------+
// HWR_ShiftViewPort  : offset viewport according to current split
// -------------------+
static void HWR_ShiftViewPort(void)
{
	gl_centerx = gl_basecenterx;
	gl_viewwindowx = gl_baseviewwindowx;
	gl_windowcenterx = gl_basewindowcenterx;

	gl_centery = gl_basecentery;
	gl_viewwindowy = gl_baseviewwindowy;
	gl_windowcentery = gl_basewindowcentery;

	if (viewssnum > ( r_splitscreen > 1 ))
	{
		gl_viewwindowy += gl_viewheight;
		gl_windowcentery += gl_viewheight;
	}

	if (r_splitscreen > 1 && viewssnum & 1)
	{
		gl_viewwindowx += gl_viewwidth;
		gl_windowcenterx += gl_viewwidth;
	}
}

// Set view aiming, for the sky dome, the skybox,
// and the normal view, all with a single function.
void HWR_SetTransformAiming(FTransform *trans, player_t *player, boolean skybox)
{
	// 1 = always on
	// 2 = chasecam only
	if (cv_glshearing.value == 1 || (cv_glshearing.value == 2 && R_IsViewpointThirdPerson(player, skybox)))
	{
		fixed_t fixedaiming = AIMINGTODY(aimingangle);
		trans->viewaiming = FIXED_TO_FLOAT(fixedaiming);
		trans->shearing = true;
		gl_aimingangle = 0;
	}
	else
	{
		trans->shearing = false;
		gl_aimingangle = aimingangle;
	}

	trans->anglex = (float)(gl_aimingangle>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);
}

//
// Sets the shader state.
//
static void HWR_SetShaderState(void)
{
	GL_SetSpecialState(HWD_SET_SHADERS, (INT32)HWR_UseShader());
}

static void HWR_ClearClipper(void)
{
	angle_t a1 = gld_FrustumAngle(gl_aimingangle);
	gld_clipper_Clear();
	gld_clipper_SafeAddClipRange(viewangle + a1, viewangle - a1);
#ifdef HAVE_SPHEREFRUSTRUM
	gld_FrustrumSetup();
#endif
}

// ==========================================================================
// Same as rendering the player view, but from the skybox object
// ==========================================================================
void HWR_RenderSkyboxView(player_t *player)
{
	UINT8 viewnum = R_GetViewNumber();
	camera_t *thiscam = &camera[viewnum];
	const float fpov = FIXED_TO_FLOAT(cv_fov[viewssnum].value+player->fovadd);

	if (!HWR_ShouldUsePaletteRendering())
	{
		// do we really need to save player (is it not the same)?
		player_t *saved_player = stplyr;
		stplyr = player;
		ST_doPaletteStuff();
		stplyr = saved_player;
#ifdef ALAM_LIGHTING
		HWR_SetLights(viewssnum);
#endif
	}

	// note: sets viewangle, viewx, viewy, viewz
	R_SkyboxFrame(viewssnum);

	// copy view cam position for local use
	dup_viewx = viewx;
	dup_viewy = viewy;
	dup_viewz = viewz;
	dup_viewangle = viewangle;

	// set window position
	HWR_ShiftViewPort();

	// check for new console commands.
	NetUpdate();

	gl_viewx = FIXED_TO_FLOAT(dup_viewx);
	gl_viewy = FIXED_TO_FLOAT(dup_viewy);
	gl_viewz = FIXED_TO_FLOAT(dup_viewz);
	gl_viewsin = FIXED_TO_FLOAT(viewsin);
	gl_viewcos = FIXED_TO_FLOAT(viewcos);

	//04/01/2000: Hurdler: added for T&L
	//                     It should replace all other gl_viewxxx when finished
	memset(&atransform, 0x00, sizeof(FTransform));

	HWR_SetTransformAiming(&atransform, player, true);
	atransform.angley = (float)(viewangle>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);

	gl_viewludsin = FIXED_TO_FLOAT(FINECOSINE(gl_aimingangle>>ANGLETOFINESHIFT));
	gl_viewludcos = FIXED_TO_FLOAT(-FINESINE(gl_aimingangle>>ANGLETOFINESHIFT));

	atransform.flip = false;
	if ((thiscam->postimgflags & POSTIMG_FLIP) && !(thiscam->postimgflags & POSTIMG_MIRROR))
		atransform.flip = true;

	atransform.mirror = false;
	if ((thiscam->postimgflags & POSTIMG_MIRROR) && !(thiscam->postimgflags & POSTIMG_FLIP))
		atransform.mirror = true;

	atransform.mirrorflip = false;
	if ((thiscam->postimgflags & POSTIMG_FLIP) && (thiscam->postimgflags & POSTIMG_MIRROR))
		atransform.mirrorflip = true;

	atransform.x      = gl_viewx;  // FIXED_TO_FLOAT(viewx)
	atransform.y      = gl_viewy;  // FIXED_TO_FLOAT(viewy)
	atransform.z      = gl_viewz;  // FIXED_TO_FLOAT(viewz)
	atransform.scalex = 1;
	atransform.scaley = (float)vid.width/vid.height;
	atransform.scalez = 1;

	atransform.fovxangle = fpov; // Tails
	atransform.fovyangle = fpov; // Tails
	HWR_RollTransform(&atransform, viewroll);
	atransform.splitscreen = r_splitscreen;

	gl_fovlud = (float)(1.0l/tan((double)(fpov*M_PIl/360l)));

	//------------------------------------------------------------------------
	HWR_ClearView();

	HWR_DrawSkyBackground(player);

	HWR_ClearSprites();

	drawcount = 0;

	//04/01/2000: Hurdler: added for T&L
	//                     Actually it only works on Walls and Planes
	GL_SetTransform(&atransform);

	HWR_ClearClipper();

	// Reset the shader state.
	HWR_SetShaderState();

	validcount++;

	if (LIKELY(cv_glbatching.value))
		HWR_StartBatching();

	HWR_AddPrecipitationSprites();

	HWR_RenderBSPNode((INT32)numnodes-1);

	if (LIKELY(cv_glbatching.value))
		HWR_RenderBatches();

	// Check for new console commands.
	NetUpdate();

#ifdef ALAM_LIGHTING
	//14/11/99: Hurdler: moved here because it doesn't work with
	// subsector, see other comments;
	HWR_ResetLights();
#endif

	// Draw MD2 and sprites
	HWR_SortVisSprites();
	HWR_DrawSprites();

#ifdef NEWCORONAS
	//Hurdler: they must be drawn before translucent planes, what about gl fog?
	HWR_DrawCoronas();
#endif

	if (numplanes || numpolyplanes || numwalls) //Hurdler: render 3D water and transparent walls after everything
	{
		HWR_CreateDrawNodes();
	}

	GL_SetTransform(NULL);
	GL_UnSetShader();

	// Check for new console commands.
	NetUpdate();

	// added by Hurdler for correct splitscreen
	// moved here by hurdler so it works with the new near clipping plane
	GL_GClipRect(0, 0, vid.width, vid.height, NZCLIP_PLANE);
}

// ==========================================================================
//
// ==========================================================================

void HWR_RollTransform(FTransform *tr, angle_t roll)
{
	if (roll != 0)
	{
		tr->rollangle = roll / (float)ANG1;
		tr->roll = true;
		tr->rollx = 1.0f;
		tr->rollz = 0.0f;
	}
}

void HWR_RenderPlayerView(void)
{
	player_t * player = &players[displayplayers[viewssnum]];
	UINT8 viewnum = R_GetViewNumber();
	camera_t *thiscam = &camera[viewnum];

	const float fpov = FIXED_TO_FLOAT(cv_fov[viewssnum].value+player->fovadd);

	const boolean skybox = (skyboxmo[0] && cv_skybox.value); // True if there's a skybox object and skyboxes are on

	FRGBAFloat ClearColor;

	ClearColor.red = 0.0f;
	ClearColor.green = 0.0f;
	ClearColor.blue = 0.0f;
	ClearColor.alpha = 1.0f;

	if (cv_glshaders.value)
		GL_SetShaderInfo(HWD_SHADERINFO_LEVELTIME, (INT32)leveltime); // The water surface shader needs the leveltime.

	if (viewssnum == 0) // Only do it if it's the first screen being rendered
		GL_ClearBuffer(true, false, &ClearColor); // Clear the Color Buffer, stops HOMs. Also seems to fix the skybox issue on Intel GPUs.

	ps_hw_skyboxtime = I_GetPreciseTime();
	if (skybox) // If there's a skybox and we should be drawing the sky, draw the skybox
		HWR_RenderSkyboxView(player); // This is drawn before everything else so it is placed behind
	ps_hw_skyboxtime = I_GetPreciseTime() - ps_hw_skyboxtime;

	{
		// do we really need to save player (is it not the same)?
		player_t *saved_player = stplyr;
		stplyr = player;
		ST_doPaletteStuff();
		stplyr = saved_player;
#ifdef ALAM_LIGHTING
		HWR_SetLights(viewssnum);
#endif
	}

	// note: sets viewangle, viewx, viewy, viewz
	R_SetupFrame(viewssnum, skybox);
	framecount++; // timedemo

	// copy view cam position for local use
	dup_viewx = viewx;
	dup_viewy = viewy;
	dup_viewz = viewz;
	dup_viewangle = viewangle;

	// set window position
	HWR_ShiftViewPort();

	if (r_splitscreen == 2 && player == &players[displayplayers[2]]) // No black void please.
	{
		// V_DrawPatchFill, but for the fourth screen only
		patch_t *gpatch = (patch_t *)W_CachePatchName("SRB2BACK", PU_CACHE);
		INT32 x, y, pw = SHORT(gpatch->width) * vid.dup, ph = SHORT(gpatch->height) * vid.dup;

		for (x = vid.width>>1; x < vid.width; x += pw)
		{
			for (y = vid.height>>1; y < vid.height; y += ph)
				HWR_DrawStretchyFixedPatch(gpatch, (x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT, FRACUNIT, V_NOSCALESTART, NULL);
		}
	}

	// check for new console commands.
	NetUpdate();

	gl_viewx = FIXED_TO_FLOAT(dup_viewx);
	gl_viewy = FIXED_TO_FLOAT(dup_viewy);
	gl_viewz = FIXED_TO_FLOAT(dup_viewz);
	gl_viewsin = FIXED_TO_FLOAT(viewsin);
	gl_viewcos = FIXED_TO_FLOAT(viewcos);

	//04/01/2000: Hurdler: added for T&L
	//                     It should replace all other gl_viewxxx when finished
	memset(&atransform, 0x00, sizeof(FTransform));

	HWR_SetTransformAiming(&atransform, player, false);
	atransform.angley = (float)(viewangle>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);

	gl_viewludsin = FIXED_TO_FLOAT(FINECOSINE(gl_aimingangle>>ANGLETOFINESHIFT));
	gl_viewludcos = FIXED_TO_FLOAT(-FINESINE(gl_aimingangle>>ANGLETOFINESHIFT));

	atransform.flip = false;
	if ((thiscam->postimgflags & POSTIMG_FLIP) && !(thiscam->postimgflags & POSTIMG_MIRROR))
		atransform.flip = true;

	atransform.mirror = false;
	if ((thiscam->postimgflags & POSTIMG_MIRROR) && !(thiscam->postimgflags & POSTIMG_FLIP))
		atransform.mirror = true;

	atransform.mirrorflip = false;
	if ((thiscam->postimgflags & POSTIMG_FLIP) && (thiscam->postimgflags & POSTIMG_MIRROR))
		atransform.mirrorflip = true;

	atransform.x      = gl_viewx;  // FIXED_TO_FLOAT(viewx)
	atransform.y      = gl_viewy;  // FIXED_TO_FLOAT(viewy)
	atransform.z      = gl_viewz;  // FIXED_TO_FLOAT(viewz)
	atransform.scalex = 1;
	atransform.scaley = (float)vid.width/vid.height;
	atransform.scalez = 1;

	atransform.fovxangle = fpov; // Tails
	atransform.fovyangle = fpov; // Tails
	HWR_RollTransform(&atransform, viewroll);
	atransform.splitscreen = r_splitscreen;

	gl_fovlud = (float)(1.0l/tan((double)(fpov*M_PIl/360l)));

	//------------------------------------------------------------------------
	HWR_ClearView(); // Clears the depth buffer and resets the view I believe

	if (!skybox) // Don't draw the regular sky if there's a skybox
		HWR_DrawSkyBackground(player);

	HWR_ClearSprites();

	drawcount = 0;

	//04/01/2000: Hurdler: added for T&L
	//                     Actually it only works on Walls and Planes
	GL_SetTransform(&atransform);

	HWR_ClearClipper();

	// Reset the shader state.
	HWR_SetShaderState();

	ps_numbspcalls = 0;
	ps_numpolyobjects = 0;
	ps_bsptime = I_GetPreciseTime();

	validcount++;

	if (cv_glbatching.value)
		HWR_StartBatching();

	HWR_AddPrecipitationSprites();

	HWR_RenderBSPNode((INT32)numnodes-1);

	ps_bsptime = I_GetPreciseTime() - ps_bsptime;

	if (cv_glbatching.value)
		HWR_RenderBatches();

	// Check for new console commands.
	NetUpdate();

#ifdef ALAM_LIGHTING
	//14/11/99: Hurdler: moved here because it doesn't work with
	// subsector, see other comments;
	HWR_ResetLights();
#endif

	// Draw MD2 and sprites
	ps_numsprites = gl_visspritecount;
	ps_hw_spritesorttime = I_GetPreciseTime();
	HWR_SortVisSprites();
	ps_hw_spritesorttime = I_GetPreciseTime() - ps_hw_spritesorttime;
	ps_hw_spritedrawtime = I_GetPreciseTime();
	HWR_DrawSprites();
	ps_hw_spritedrawtime = I_GetPreciseTime() - ps_hw_spritedrawtime;

#ifdef NEWCORONAS
	//Hurdler: they must be drawn before translucent planes, what about gl fog?
	HWR_DrawCoronas();
#endif

	ps_numdrawnodes = 0;
	ps_hw_nodesorttime = 0;
	ps_hw_nodedrawtime = 0;
	if (numplanes || numpolyplanes || numwalls) //Hurdler: render 3D water and transparent walls after everything
	{
		HWR_CreateDrawNodes();
	}

	GL_SetTransform(NULL);
	GL_UnSetShader();

	HWR_DoPostProcessor(player);

	// Check for new console commands.
	NetUpdate();

	// added by Hurdler for correct splitscreen
	// moved here by hurdler so it works with the new near clipping plane
	GL_GClipRect(0, 0, vid.width, vid.height, NZCLIP_PLANE);
}

// Returns whether palette rendering is "actually enabled."
// Can't have palette rendering if shaders are disabled.
boolean HWR_ShouldUsePaletteRendering(void)
{
	return (pMasterPalette != NULL && cv_glpaletterendering.value && HWR_UseShader());
}

// enable or disable palette rendering state depending on settings and availability
// called when relevant settings change
// shader recompilation is done in the cvar callback
static void HWR_TogglePaletteRendering(void)
{
	// which state should we go to?
	if (HWR_ShouldUsePaletteRendering())
	{
		// are we not in that state already?
		if (!gl_palette_rendering_state)
		{
			gl_palette_rendering_state = true;

			// The textures will still be converted to RGBA by r_opengl.
			// This however makes hw_cache use paletted blending for composite textures!
			// (patchformat is not touched)
			textureformat = GL_TEXFMT_P_8;

			HWR_SetMapPalette();
			HWR_SetPalette(pLocalPalette);

			// If the r_opengl "texture palette" stays the same during this switch, these textures
			// will not be cleared out. However they are still out of date since the
			// composite texture blending method has changed. Therefore they need to be cleared.
			HWR_LoadMapTextures(numtextures);
		}
	}
	else
	{
		// are we not in that state already?
		if (gl_palette_rendering_state)
		{
			gl_palette_rendering_state = false;
			textureformat = GL_TEXFMT_RGBA;
			HWR_SetPalette(pLocalPalette);
			// If the r_opengl "texture palette" stays the same during this switch, these textures
			// will not be cleared out. However they are still out of date since the
			// composite texture blending method has changed. Therefore they need to be cleared.
			HWR_LoadMapTextures(numtextures);
		}
	}
}

void HWR_LoadLevel(void)
{
#ifdef ALAM_LIGHTING
	// BP: reset light between levels (we draw preview frame lights on current frame)
	HWR_ResetLights();
#endif

	HWR_CreatePlanePolygons((INT32)numnodes - 1);

	// Build the sky dome
	HWR_ClearSkyDome();
	HWR_BuildSkyDome();

	if (HWR_ShouldUsePaletteRendering())
		HWR_SetMapPalette();

	gl_maploaded = true;
}

// ==========================================================================
//                                                         3D ENGINE COMMANDS
// ==========================================================================

CV_PossibleValue_t glshaders_cons_t[] = {{0, "Off"}, {1, "On"}, {2, "Ignore custom shaders"}, {0, NULL}};
#ifdef BAD_MODEL_OPTIONS
static CV_PossibleValue_t glmodelinterpolation_cons_t[] = {{0, "Off"}, {1, "Sometimes"}, {2, "Always"}, {0, NULL}};
#endif
static CV_PossibleValue_t glshearing_cons_t[] = {{0, "Off"}, {1, "On"}, {2, "Third-person"}, {0, NULL}};

static void CV_glfiltermode_OnChange(void);
static void CV_glanisotropic_OnChange(void);

static CV_PossibleValue_t glfiltermode_cons_t[]= {{HWD_SET_TEXTUREFILTER_POINTSAMPLED, "Nearest"},
	{HWD_SET_TEXTUREFILTER_BILINEAR, "Bilinear"}, {HWD_SET_TEXTUREFILTER_TRILINEAR, "Trilinear"},
	{HWD_SET_TEXTUREFILTER_MIXED1, "Linear_Nearest"},
	{HWD_SET_TEXTUREFILTER_MIXED2, "Nearest_Linear"},
	{HWD_SET_TEXTUREFILTER_MIXED3, "Nearest_Mipmap"},
	{0, NULL}};
CV_PossibleValue_t glanisotropicmode_cons_t[] = {{1, "MIN"}, {16, "MAX"}, {0, NULL}};
static CV_PossibleValue_t glsecbright_cons_t[] = {{0, "MIN"}, {255, "MAX"}, {0, NULL}};

consvar_t cv_glshaders = CVAR_INIT ("gr_shaders", "On", CV_SAVE, glshaders_cons_t, NULL);
consvar_t cv_glallowshaders = CVAR_INIT ("gr_allowclientshaders", "On", CV_NETVAR, CV_OnOff, NULL);
consvar_t cv_fovchange = CVAR_INIT ("gr_fovchange", "Off", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_glsecbright = CVAR_INIT("gr_secbright", "0", CV_SAVE, glsecbright_cons_t, NULL);

#ifdef ALAM_LIGHTING
consvar_t cv_gldynamiclighting = CVAR_INIT ("gr_dynamiclighting", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_glstaticlighting  = CVAR_INIT ("gr_staticlighting", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_glcoronas = CVAR_INIT ("gr_coronas", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_glcoronasize = CVAR_INIT ("gr_coronasize", "1", CV_SAVE|CV_FLOAT, 0, NULL);
#endif

consvar_t cv_glmodels = CVAR_INIT ("gr_models", "Off", CV_SAVE, CV_OnOff, NULL);

#ifdef BAD_MODEL_OPTIONS
consvar_t cv_glmodelinterpolation = CVAR_INIT ("gr_modelinterpolation", "Sometimes", CV_SAVE, glmodelinterpolation_cons_t, NULL);
consvar_t cv_glmodellighting = CVAR_INIT ("gr_modellighting", "Off", CV_SAVE, CV_OnOff, NULL);
#endif

consvar_t cv_glshearing = CVAR_INIT ("gr_shearing", "Off", CV_SAVE, glshearing_cons_t, NULL);
consvar_t cv_glspritebillboarding = CVAR_INIT ("gr_spritebillboarding", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_glskydome = CVAR_INIT ("gr_skydome", "On", CV_SAVE, CV_OnOff, NULL);

consvar_t cv_glfiltermode = CVAR_INIT ("gr_filtermode", "Nearest", CV_SAVE|CV_CALL, glfiltermode_cons_t, CV_glfiltermode_OnChange);
consvar_t cv_glanisotropicmode = CVAR_INIT ("gr_anisotropicmode", "1", CV_SAVE|CV_CALL, glanisotropicmode_cons_t, CV_glanisotropic_OnChange);

consvar_t cv_glsolvetjoin = CVAR_INIT ("gr_solvetjoin", "On", 0, CV_OnOff, NULL);

consvar_t cv_glbatching = CVAR_INIT ("gr_batching", "On", 0, CV_OnOff, NULL);

CV_PossibleValue_t glpalettedepth_cons_t[] = {{16, "16 bits"}, {24, "24 bits"}, {0, NULL}};
void CV_glpaletterendering_OnChange(void);
void CV_glpalettedepth_OnChange(void);

consvar_t cv_glpaletterendering = CVAR_INIT ("gr_paletteshader", "Off", CV_CALL|CV_SAVE, CV_OnOff, CV_glpaletterendering_OnChange);
consvar_t cv_glpalettedepth = CVAR_INIT ("gr_palettedepth", "16 bits", CV_SAVE|CV_CALL, glpalettedepth_cons_t, CV_glpalettedepth_OnChange);

#define ONLY_IF_GL_LOADED if (vid.glstate != VID_GL_LIBRARY_LOADED) return;

static void CV_glfiltermode_OnChange(void)
{
	ONLY_IF_GL_LOADED
	GL_SetSpecialState(HWD_SET_TEXTUREFILTERMODE, cv_glfiltermode.value);
}

static void CV_glanisotropic_OnChange(void)
{
	ONLY_IF_GL_LOADED
	GL_SetSpecialState(HWD_SET_TEXTUREANISOTROPICMODE, cv_glanisotropicmode.value);
}

void CV_glmodellighting_OnChange(void);
void CV_glmodellighting_OnChange(void)
{
	ONLY_IF_GL_LOADED
	// if shaders have been compiled, then they now need to be recompiled.
	if (gl_shadersavailable)
		HWR_CompileShaders();
}

void CV_glpaletterendering_OnChange(void)
{
	ONLY_IF_GL_LOADED
	if (gl_shadersavailable)
	{
		HWR_CompileShaders();
		HWR_TogglePaletteRendering();
	}
}

void CV_glpalettedepth_OnChange(void);
void CV_glpalettedepth_OnChange(void)
{
	ONLY_IF_GL_LOADED
	// refresh the screen palette
	if (HWR_ShouldUsePaletteRendering())
		HWR_SetPalette(pLocalPalette);
}

void CV_glshaders_OnChange(void);
void CV_glshaders_OnChange(void)
{
	ONLY_IF_GL_LOADED
	HWR_SetShaderState();
	if (cv_glpaletterendering.value)
	{
		// can't do palette rendering without shaders, so update the state if needed
		HWR_TogglePaletteRendering();
	}
}

//added by Hurdler: console varibale that are saved
void HWR_AddCommands(void)
{
	CV_RegisterVar(&cv_fovchange);

#ifdef ALAM_LIGHTING
	CV_RegisterVar(&cv_glstaticlighting);
	CV_RegisterVar(&cv_gldynamiclighting);
	CV_RegisterVar(&cv_glcoronasize);
	CV_RegisterVar(&cv_glcoronas);
#endif

#ifdef BAD_MODEL_OPTIONS
	CV_RegisterVar(&cv_glmodellighting);
	CV_RegisterVar(&cv_glmodelinterpolation);
#endif

	CV_RegisterVar(&cv_glmodels);

	CV_RegisterVar(&cv_glskydome);
	CV_RegisterVar(&cv_glspritebillboarding);
	CV_RegisterVar(&cv_glshearing);
	CV_RegisterVar(&cv_glshaders);
	CV_RegisterVar(&cv_glsecbright);
	CV_RegisterVar(&cv_glallowshaders);

	CV_RegisterVar(&cv_glfiltermode);
	CV_RegisterVar(&cv_glsolvetjoin);

	CV_RegisterVar(&cv_glbatching);

	CV_RegisterVar(&cv_glanisotropicmode);

	CV_RegisterVar(&cv_glpaletterendering);
	CV_RegisterVar(&cv_glpalettedepth);
}

void HWR_AddSessionCommands(void)
{
	if (gl_sessioncommandsadded)
		return;
	gl_sessioncommandsadded = true;
}

// --------------------------------------------------------------------------
// Setup the hardware renderer
// --------------------------------------------------------------------------
void HWR_Startup(void)
{
	if (!gl_init)
	{
		CONS_Printf("HWR_Startup()...\n");

		textureformat = patchformat = GL_TEXFMT_RGBA;

		HWR_InitPolyPool();
		HWR_AddSessionCommands();
		HWR_InitMapTextures();
		HWR_InitModels();
#ifdef ALAM_LIGHTING
		HWR_InitLight();
#endif

		gl_shadersavailable = HWR_InitShaders();
		HWR_SetShaderState();
		HWR_LoadAllCustomShaders();
		HWR_TogglePaletteRendering();
	}

	gl_init = true;
}

// --------------------------------------------------------------------------
// Called after switching to the hardware renderer
// --------------------------------------------------------------------------
void HWR_Switch(void)
{
	// Add session commands
	if (!gl_sessioncommandsadded)
		HWR_AddSessionCommands();

	// Set special states from CVARs
	GL_SetSpecialState(HWD_SET_TEXTUREFILTERMODE, cv_glfiltermode.value);
	GL_SetSpecialState(HWD_SET_TEXTUREANISOTROPICMODE, cv_glanisotropicmode.value);

	// Load textures
	HWR_LoadMapTextures(numtextures);

	// Create plane polygons
	if (!gl_maploaded && levelloaded)
	{
		HWR_ClearAllTextures();
		HWR_LoadLevel();
	}
}

// --------------------------------------------------------------------------
// Free resources allocated by the hardware renderer
// --------------------------------------------------------------------------
void HWR_Shutdown(void)
{
	CONS_Printf("HWR_Shutdown()\n");
	HWR_FreeExtraSubsectors();
	HWR_FreePolyPool();
	HWR_FreeMapTextures();
	GL_FlushScreenTextures();
}

void transform(float *cx, float *cy, float *cz)
{
	float tr_x,tr_y;
	// translation
	tr_x = *cx - gl_viewx;
	tr_y = *cz - gl_viewy;
//	*cy = *cy;

	// rotation around vertical y axis
	*cx = (tr_x * gl_viewsin) - (tr_y * gl_viewcos);
	tr_x = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

	//look up/down ----TOTAL SUCKS!!!--- do the 2 in one!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	tr_y = *cy - gl_viewz;

	*cy = (tr_x * gl_viewludcos) + (tr_y * gl_viewludsin);
	*cz = (tr_x * gl_viewludsin) - (tr_y * gl_viewludcos);

	//scale y before frustum so that frustum can be scaled to screen height
	*cy *= ORIGINAL_ASPECT * gl_fovlud;
	*cx *= gl_fovlud;
}

static void HWR_AddTransparentWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, INT32 texnum, boolean noencore, FBITFIELD blend, boolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap)
{
	static size_t allocedwalls = 0;

	// Force realloc if buffer has been freed
	if (!wallinfo)
		allocedwalls = 0;

	if (allocedwalls < numwalls + 1)
	{
		allocedwalls += MAX_TRANSPARENTWALL;
		Z_Realloc(wallinfo, allocedwalls * sizeof (*wallinfo), PU_LEVEL, &wallinfo);
	}

	memcpy(wallinfo[numwalls].wallVerts, wallVerts, sizeof (wallinfo[numwalls].wallVerts));
	memcpy(&wallinfo[numwalls].Surf, pSurf, sizeof (FSurfaceInfo));
	wallinfo[numwalls].texnum = texnum;
	wallinfo[numwalls].blend = blend;
	wallinfo[numwalls].noencore = noencore;
	wallinfo[numwalls].drawcount = drawcount++;
	wallinfo[numwalls].fogwall = fogwall;
	wallinfo[numwalls].lightlevel = lightlevel;
	wallinfo[numwalls].wallcolormap = wallcolormap;
	numwalls++;
}

void HWR_RenderWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blend, boolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap)
{
	FBITFIELD blendmode = blend;
	UINT8 alpha = pSurf->PolyColor.s.alpha; // retain the alpha

	INT32 shader = SHADER_NONE;

	// Lighting is done here instead so that fog isn't drawn incorrectly on transparent walls after sorting
	HWR_Lighting(pSurf, lightlevel, wallcolormap, P_SectorUsesDirectionalLighting(gl_frontsector));

	pSurf->PolyColor.s.alpha = alpha; // put the alpha back after lighting

	if (blend & PF_Environment)
		blendmode |= PF_Occlude;	// PF_Occlude must be used for solid objects

	if (HWR_UseShader())
	{
		if (fogwall)
			shader = SHADER_FOG;
		else
			shader = SHADER_WALL;

		blendmode |= PF_ColorMapped;
	}

	if (fogwall)
		blendmode |= PF_Fog;

	blendmode |= PF_Modulated;	// No PF_Occlude means overlapping (incorrect) transparency
	HWR_ProcessPolygon(pSurf, wallVerts, 4, blendmode, shader, false);
}

INT32 HWR_GetTextureUsed(void)
{
	return GL_GetTextureUsed();
}

void HWR_DoPostProcessor(player_t *player)
{
	GL_UnSetShader();

	// Armageddon Blast Flash!
	// Could this even be considered postprocessor?
	if (player->flashcount && !HWR_ShouldUsePaletteRendering())
	{
		FOutVector      v[4];
		FSurfaceInfo Surf = {};

		v[0].x = v[2].y = v[3].x = v[3].y = -4.0f;
		v[0].y = v[1].x = v[1].y = v[2].x = 4.0f;
		v[0].z = v[1].z = v[2].z = v[3].z = 4.0f; // 4.0 because of the same reason as with the sky, just after the screen is cleared so near clipping plane is 3.99

		// This won't change if the flash palettes are changed unfortunately, but it works for its purpose
		if (player->flashpal == PAL_NUKE)
		{
			Surf.PolyColor.s.red = 0xff;
			Surf.PolyColor.s.green = Surf.PolyColor.s.blue = 0x7F; // The nuke palette is kind of pink-ish
		}
		else
			Surf.PolyColor.s.red = Surf.PolyColor.s.green = Surf.PolyColor.s.blue = 0xff;

		Surf.PolyColor.s.alpha = 0xc0; // match software mode

		V_CubeApply(&Surf.PolyColor);

		GL_DrawPolygon(&Surf, v, 4, PF_Modulated|PF_Additive|PF_NoTexture|PF_NoDepthTest);
	}

	// Capture the screen for intermission and screen waving
	if (gamestate != GS_INTERMISSION)
		GL_MakeScreenTexture(HWD_SCREENTEXTURE_GENERIC1);

	if (r_splitscreen) // Not supported in splitscreen - someone want to add support?
		return;

	//UINT8 viewnum = R_GetViewNumber(); // see above
	//camera_t *thiscam = &camera[viewnum];
	camera_t *thiscam = &camera[0];

	// Drunken vision! WooOOooo~
	if (thiscam->postimgflags & POSTIMG_WATER || thiscam->postimgflags & POSTIMG_HEAT)
	{
		// 10 by 10 grid. 2 coordinates (xy)
		float v[SCREENVERTS][SCREENVERTS][2];
		float disStart = (leveltime-1) + FIXED_TO_FLOAT(R_GetTimeFrac(RTF_LEVEL));

		UINT8 x, y;
		INT32 WAVELENGTH;
		INT32 AMPLITUDE;
		INT32 FREQUENCY;

		// Modifies the wave.
		if (thiscam->postimgflags & POSTIMG_WATER)
		{
			WAVELENGTH = 5;
			AMPLITUDE = 40;
			FREQUENCY = 8;
		}
		else
		{
			WAVELENGTH = 10;
			AMPLITUDE = 60;
			FREQUENCY = 4;
		}

		for (x = 0; x < SCREENVERTS; x++)
		{
			for (y = 0; y < SCREENVERTS; y++)
			{
				// Change X position based on its Y position.
				v[x][y][0] = (x/((float)(SCREENVERTS-1.0f)/9.0f))-4.5f + (float)sin((disStart+(y*WAVELENGTH))/FREQUENCY)/AMPLITUDE;
				v[x][y][1] = (y/((float)(SCREENVERTS-1.0f)/9.0f))-4.5f;
			}
		}
		GL_PostImgRedraw(v);

		// Capture the screen again for screen waving on the intermission
		if (gamestate != GS_INTERMISSION)
			GL_MakeScreenTexture(HWD_SCREENTEXTURE_GENERIC1);
	}
	// Flipping of the screen isn't done here anymore
}

void HWR_StartScreenWipe(void)
{
	//CONS_Debug(DBG_RENDER, "In HWR_StartScreenWipe()\n");
	GL_MakeScreenTexture(HWD_SCREENTEXTURE_WIPE_START);
}

void HWR_EndScreenWipe(void)
{
	//CONS_Debug(DBG_RENDER, "In HWR_EndScreenWipe()\n");
	GL_MakeScreenTexture(HWD_SCREENTEXTURE_WIPE_END);
}

void HWR_DrawIntermissionBG(void)
{
	GL_DrawScreenTexture(HWD_SCREENTEXTURE_GENERIC1, NULL, 0);
}

//
// hwr mode wipes
//
static lumpnum_t wipelumpnum;

// puts wipe lumpname in wipename[9]
static boolean HWR_WipeCheck(UINT8 wipenum, UINT8 scrnnum)
{
	static char lumpname[9] = "FADEmmss";
	size_t lsize;

	// not a valid wipe number
	if (wipenum > 99 || scrnnum > 99)
		return false; // shouldn't end up here really, the loop should've stopped running beforehand

	// puts the numbers into the wipename
	lumpname[4] = '0'+(wipenum/10);
	lumpname[5] = '0'+(wipenum%10);
	lumpname[6] = '0'+(scrnnum/10);
	lumpname[7] = '0'+(scrnnum%10);
	wipelumpnum = W_CheckNumForName(lumpname);

	// again, shouldn't be here really
	if (wipelumpnum == LUMPERROR)
		return false;

	lsize = W_LumpLength(wipelumpnum);
	if (!(lsize == 256000 || lsize == 64000 || lsize == 16000 || lsize == 4000))
	{
		CONS_Alert(CONS_WARNING, "Fade mask lump %s of incorrect size, ignored\n", lumpname);
		return false; // again, shouldn't get here if it is a bad size
	}

	return true;
}

void HWR_DoWipe(UINT8 wipenum, UINT8 scrnnum)
{
	if (!HWR_WipeCheck(wipenum, scrnnum))
		return;

	HWR_GetFadeMask(wipelumpnum);
	GL_DoScreenWipe(HWD_SCREENTEXTURE_WIPE_START, HWD_SCREENTEXTURE_WIPE_END);
}

void HWR_DoTintedWipe(UINT8 wipenum, UINT8 scrnnum)
{
	// It does the same thing
	HWR_DoWipe(wipenum, scrnnum);
}

void HWR_RenderVhsEffect(fixed_t upbary, fixed_t downbary, UINT8 updistort, UINT8 downdistort, UINT8 barsize)
{
	GL_RenderVhsEffect(upbary, downbary, updistort, downdistort, barsize);
}

void HWR_MakeScreenFinalTexture(void)
{
	int tex = HWR_ShouldUsePaletteRendering() ? HWD_SCREENTEXTURE_GENERIC3 : HWD_SCREENTEXTURE_GENERIC2;
	GL_MakeScreenTexture(tex);
}

void HWR_DrawScreenFinalTexture(int width, int height)
{
	int tex = HWR_ShouldUsePaletteRendering() ? HWD_SCREENTEXTURE_GENERIC3 : HWD_SCREENTEXTURE_GENERIC2;
	GL_DrawScreenFinalTexture(tex, width, height);
}

#endif // HWRENDER
