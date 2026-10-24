// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2022 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_segs.c
/// \brief Processing segs, rendering walls

#ifdef HWRENDER
#include "hw_glob.h"
#include "hw_batching.h"
#include "../p_local.h"
#include "../p_slopes.h"
#include "../r_local.h"
#include "../z_zone.h"

static FUINT HWR_CalcWallLight(FUINT lightnum, seg_t *seg)
{
    int16_t finallight = lightnum;

    if (seg != NULL && P_ApplyLightOffsetFine(lightnum, seg->frontsector))
    {
        finallight += seg->hwLightOffset;

        if (finallight > 255) finallight = 255;
        if (finallight < 0) finallight = 0;
    }

    return (FUINT)finallight;
}

void HWR_RenderWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blend, dboolean fogwall, int32_t lightlevel, extracolormap_t *wallcolormap)
{
	FBITFIELD blendmode = blend;
	uint8_t alpha = pSurf->PolyColor.s.alpha; // retain the alpha

	int32_t shader = SHADER_NONE;

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
static void HWR_ProjectWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blendmode, int32_t lightlevel, extracolormap_t *wallcolormap)
{
	int32_t shader = SHADER_NONE;

	HWR_Lighting(pSurf, lightlevel, wallcolormap, P_SectorUsesDirectionalLighting(gl_frontsector));

	if (HWR_UseShader())
	{
		shader = SHADER_WALL;
		blendmode |= PF_ColorMapped;
	}

	// don't draw to color buffer when drawing to stencil
	if (gl_drawing_stencil)
	{
		blendmode |= PF_Invisible|PF_NoAlphaTest; // TODO not sure if any others than PF_Invisible are needed??
		blendmode &= ~PF_Masked;
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
static void HWR_SplitWall(sector_t *sector, FOutVector *wallVerts, int32_t texnum, dboolean noencore, FSurfaceInfo* Surf, int32_t cutflag, ffloor_t *pfloor, FBITFIELD polyflags)
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

	const uint8_t alpha = Surf->PolyColor.s.alpha;
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

	for (int32_t i = 0; i < sector->numlights; i++)
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

		dboolean solid = false;

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

		if (solid)
		{
			height = FixedToFloat(P_GetLightZAt(&list[i], v1x, v1y));
			endheight = FixedToFloat(P_GetLightZAt(&list[i], v2x, v2y));

			bheight = FixedToFloat(P_GetFFloorBottomZAt(list[i].caster, v1x, v1y));
			endbheight = FixedToFloat(P_GetFFloorBottomZAt(list[i].caster, v2x, v2y));

			if (endheight >= endtop && height >= top)
			{
				if (top > bheight)
					top = bheight;
				if (endtop > endbheight)
					endtop = endbheight;
			}
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

		if ((endbheight >= endtop) || (bheight >= top))
			continue;

		// Found a break
		// The heights are clamped to ensure the polygon doesn't cross itself.
		bot    = min(max(bheight, realbot), top);
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
			HWR_AddTransparentWall(wallVerts, Surf, texnum, noencore, polyflags, true, HWR_CalcWallLight(lightnum, gl_curline), colormap);
		else if (polyflags & PF_EnvironmentTrans)
			HWR_AddTransparentWall(wallVerts, Surf, texnum, noencore, polyflags, false, HWR_CalcWallLight(lightnum, gl_curline), colormap);
		else
			HWR_ProjectWall(wallVerts, Surf, polyflags, HWR_CalcWallLight(lightnum, gl_curline), colormap);

		top = bot;
		endtop = endbot;
	}

	bot = realbot;
	endbot = endrealbot;

	if ((endtop <= endrealbot) && (top <= realbot))
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
	if (cv_glskydebug.value)
	{
		wallVerts[3].t = wallVerts[2].t = 4;
		wallVerts[0].t = wallVerts[1].t = 0;
		wallVerts[0].s = wallVerts[3].s = 0.1;
		wallVerts[2].s = wallVerts[1].s = 0;
		HWR_GetTexture(cv_glskydebug.value, false);
		HWR_ProjectWall(wallVerts, Surf, 0, 255, NULL);
		return;
	}

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

typedef struct
{
	fixed_t worldtop, worldbottom;
	fixed_t worldhigh, worldlow;
	fixed_t worldtopslope, worldbottomslope;
	fixed_t worldhighslope, worldlowslope;
} gl_seg_bounds;

#define SLOPEPARAMS(slope, end1, end2, normalheight) \
	end1 = P_GetZAt(slope, v1x, v1y, normalheight); \
	end2 = P_GetZAt(slope, v2x, v2y, normalheight);

static void HWR_ProcessTwoSidedSegTop(FOutVector *wallVerts, FSurfaceInfo *Surf, gl_seg_bounds *b,
		float cliplow, float cliphigh, FUINT lightnum, int32_t gl_toptexture, dboolean noencore)
{
	GLMapTexture_t *grTex = HWR_GetTexture(gl_toptexture, noencore);

	fixed_t texturevpegtop;
	fixed_t texheight = textureheight[gl_toptexture];

	// PEGGING
	if (gl_linedef->flags & ML_DONTPEGTOP)
		texturevpegtop = 0;
	else if (gl_linedef->flags & ML_SKEWTD)
		texturevpegtop = b->worldhigh + texheight - b->worldtop;
	else
		texturevpegtop = gl_backsector->ceilingheight + texheight - gl_frontsector->ceilingheight;

	texturevpegtop += gl_sidedef->rowoffset + gl_sidedef->offsety_top;

	// This is so that it doesn't overflow and screw up the wall, it doesn't need to go higher than the texture's height anyway
	texturevpegtop %= texheight;

	grTex = HWR_GetTexture(gl_toptexture, noencore);

	wallVerts[3].t = wallVerts[2].t = texturevpegtop * grTex->scaleY;
	wallVerts[0].t = wallVerts[1].t = (texturevpegtop + (gl_frontsector->ceilingheight - gl_backsector->ceilingheight)) * grTex->scaleY;
	wallVerts[0].s = wallVerts[3].s = (cliplow + gl_sidedef->offsetx_top) * grTex->scaleX;
	wallVerts[2].s = wallVerts[1].s = (cliphigh + gl_sidedef->offsetx_top) * grTex->scaleX;

	// Adjust t value for sloped walls
	if (!(gl_linedef->flags & ML_SKEWTD))
	{
		// Unskewed
		wallVerts[3].t -= (b->worldtop - gl_frontsector->ceilingheight) * grTex->scaleY;
		wallVerts[2].t -= (b->worldtopslope - gl_frontsector->ceilingheight) * grTex->scaleY;
		wallVerts[0].t -= (b->worldhigh - gl_backsector->ceilingheight) * grTex->scaleY;
		wallVerts[1].t -= (b->worldhighslope - gl_backsector->ceilingheight) * grTex->scaleY;
	}
	else if (gl_linedef->flags & ML_DONTPEGTOP)
	{
		// Skewed by top
		wallVerts[0].t = (texturevpegtop + (b->worldtop - b->worldhigh)) * grTex->scaleY;
		wallVerts[1].t = (texturevpegtop + (b->worldtopslope - b->worldhighslope)) * grTex->scaleY;
	}
	else
	{
		// Skewed by bottom
		wallVerts[0].t = wallVerts[1].t = (texturevpegtop + (b->worldtop - b->worldhigh)) * grTex->scaleY;
		wallVerts[3].t = wallVerts[0].t - (b->worldtop - b->worldhigh) * grTex->scaleY;
		wallVerts[2].t = wallVerts[1].t - (b->worldtopslope - b->worldhighslope) * grTex->scaleY;
	}

	// set top/bottom coords
	wallVerts[3].y = FIXED_TO_FLOAT(b->worldtop);
	wallVerts[0].y = FIXED_TO_FLOAT(b->worldhigh);
	wallVerts[2].y = FIXED_TO_FLOAT(b->worldtopslope);
	wallVerts[1].y = FIXED_TO_FLOAT(b->worldhighslope);

	FBITFIELD polyflags = PF_Masked;

	if (grTex->mipmap.flags & TF_TRANSPARENT)
		polyflags = PF_Environment;

	if (!gl_drawing_stencil && gl_frontsector->numlights)
		HWR_SplitWall(gl_frontsector, wallVerts, gl_toptexture, noencore, Surf, FOF_CUTLEVEL, NULL, polyflags);
	else if (!gl_drawing_stencil && (grTex->mipmap.flags & TF_TRANSPARENT))
		HWR_AddTransparentWall(wallVerts, Surf, gl_toptexture, noencore, polyflags, false, lightnum, gl_frontsector->extra_colormap);
	else
		HWR_ProjectWall(wallVerts, Surf, polyflags, lightnum, gl_frontsector->extra_colormap);
}

static void HWR_ProcessTwoSidedSegBottom(FOutVector *wallVerts, FSurfaceInfo *Surf, gl_seg_bounds *b,
										 float cliplow, float cliphigh, FUINT lightnum, int32_t gl_bottomtexture, dboolean noencore)
{
	GLMapTexture_t *grTex = HWR_GetTexture(gl_bottomtexture, noencore);

	fixed_t texturevpegbottom;

	// PEGGING
	if (!(gl_linedef->flags & ML_DONTPEGBOTTOM))
		texturevpegbottom = 0;
	else if (gl_linedef->flags & ML_SKEWTD)
		texturevpegbottom = b->worldbottom - b->worldlow;
	else
		texturevpegbottom = gl_frontsector->floorheight - gl_backsector->floorheight;

	texturevpegbottom += gl_sidedef->rowoffset + gl_sidedef->offsety_bot;

	// This is so that it doesn't overflow and screw up the wall, it doesn't need to go higher than the texture's height anyway
	texturevpegbottom %= textureheight[gl_bottomtexture];

	wallVerts[3].t = wallVerts[2].t = texturevpegbottom * grTex->scaleY;
	wallVerts[0].t = wallVerts[1].t = (texturevpegbottom + (gl_backsector->floorheight - gl_frontsector->floorheight)) * grTex->scaleY;
	wallVerts[0].s = wallVerts[3].s = (cliplow + gl_sidedef->offsetx_bot) * grTex->scaleX;
	wallVerts[2].s = wallVerts[1].s = (cliphigh + gl_sidedef->offsetx_bot) * grTex->scaleX;

	// Adjust t value for sloped walls
	if (!(gl_linedef->flags & ML_SKEWTD))
	{
		// Unskewed
		wallVerts[0].t -= (b->worldbottom - gl_frontsector->floorheight) * grTex->scaleY;
		wallVerts[1].t -= (b->worldbottomslope - gl_frontsector->floorheight) * grTex->scaleY;
		wallVerts[3].t -= (b->worldlow - gl_backsector->floorheight) * grTex->scaleY;
		wallVerts[2].t -= (b->worldlowslope - gl_backsector->floorheight) * grTex->scaleY;
	}
	else if (gl_linedef->flags & ML_DONTPEGBOTTOM)
	{
		// Skewed by bottom
		wallVerts[0].t = wallVerts[1].t = (texturevpegbottom + (b->worldlow - b->worldbottom)) * grTex->scaleY;
		wallVerts[2].t = wallVerts[1].t - (b->worldlowslope - b->worldbottomslope) * grTex->scaleY;
	}
	else
	{
		// Skewed by top
		wallVerts[0].t = (texturevpegbottom + (b->worldlow - b->worldbottom)) * grTex->scaleY;
		wallVerts[1].t = (texturevpegbottom + (b->worldlowslope - b->worldbottomslope)) * grTex->scaleY;
	}

	// set top/bottom coords
	wallVerts[3].y = FIXED_TO_FLOAT(b->worldlow);
	wallVerts[0].y = FIXED_TO_FLOAT(b->worldbottom);
	wallVerts[2].y = FIXED_TO_FLOAT(b->worldlowslope);
	wallVerts[1].y = FIXED_TO_FLOAT(b->worldbottomslope);

	FBITFIELD polyflags = PF_Masked;

	if (grTex->mipmap.flags & TF_TRANSPARENT)
		polyflags = PF_Environment;

	if (!gl_drawing_stencil && gl_frontsector->numlights)
		HWR_SplitWall(gl_frontsector, wallVerts, gl_bottomtexture, noencore, Surf, FOF_CUTLEVEL, NULL, polyflags);
	else if (!gl_drawing_stencil && (grTex->mipmap.flags & TF_TRANSPARENT))
		HWR_AddTransparentWall(wallVerts, Surf, gl_bottomtexture, noencore, polyflags, false, lightnum, gl_frontsector->extra_colormap);
	else
		HWR_ProjectWall(wallVerts, Surf, polyflags, lightnum, gl_frontsector->extra_colormap);
}


// gl_midtexture can be inactive for this function
// when rendering twosided portal midtextures
static void HWR_ProcessTwoSidedSegMiddle(FOutVector *wallVerts, FSurfaceInfo *Surf, gl_seg_bounds *b,
										 float cliplow, float cliphigh, FUINT lightnum, int32_t gl_midtexture, dboolean noencore, dboolean tripwire)
{
	GLMapTexture_t *grTex = NULL;

	FBITFIELD blendmode;
	sector_t *front, *back;
	fixed_t h, l; // 2s middle textures
	fixed_t hS, lS;
	fixed_t popentop, popenbottom, polytop, polybottom, lowcut, highcut;
	fixed_t popentopslope, popenbottomslope, polytopslope, polybottomslope, lowcutslope, highcutslope;
	fixed_t texturevpeg = 0;
	int32_t repeats;
	fixed_t midtexheight = 0;
	fixed_t texturevpegslope = 0;

	if (gl_linedef->frontsector->heightsec != -1)
		front = &sectors[gl_linedef->frontsector->heightsec];
	else
		front = gl_linedef->frontsector;

	if (gl_linedef->backsector->heightsec != -1)
		back = &sectors[gl_linedef->backsector->heightsec];
	else
		back = gl_linedef->backsector;

	fixed_t texheight = gl_midtexture ? textureheight[gl_midtexture] : 0;

	if (gl_midtexture)
	{
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

			repeats = (high - low) / texheight;
			if ((high - low) % texheight)
				repeats++; // tile an extra time to fill the gap -- Monster Iestyn
		}
		else
			repeats = 1;
	}

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
		popentop = min(b->worldtop, b->worldhigh);
		popenbottom = max(b->worldbottom, b->worldlow);
		popentopslope = min(b->worldtopslope, b->worldhighslope);
		popenbottomslope = max(b->worldbottomslope, b->worldlowslope);
	}

	if (gl_midtexture)
	{
		// Find the wall's coordinates
		midtexheight = texheight * repeats;

		fixed_t rowoffset = gl_sidedef->rowoffset + gl_sidedef->offsety_mid;

		// Texture is not skewed
		if (gl_linedef->flags & ML_NOSKEW)
		{
			if (gl_linedef->flags & ML_MIDPEG) // Peg it to the floor
			{
				polybottom = max(front->floorheight, back->floorheight) + rowoffset;
				polytop = polybottom + midtexheight;
			}
			else // Peg it to the ceiling
			{
				polytop = min(front->ceilingheight, back->ceilingheight) + rowoffset;
				polybottom = polytop - midtexheight;
			}

			// The right side's coordinates are the the same as the left side
			polytopslope = polytop;
			polybottomslope = polybottom;
		}
		else if (gl_linedef->flags & ML_MIDPEG) // Skew the texture, but peg it to the floor
		{
			polybottom = popenbottom + rowoffset;
			polytop = polybottom + midtexheight;
			polybottomslope = popenbottomslope + rowoffset;
			polytopslope = polybottomslope + midtexheight;
		}
		else // Skew it according to the ceiling's slope
		{
			polytop = popentop + rowoffset;
			polybottom = polytop - midtexheight;
			polytopslope = popentopslope + rowoffset;
			polybottomslope = polytopslope - midtexheight;
		}
	}
	else
	{
		// portal stencil twosided midtexture, fills entire space
		polytop = popentop;
		polybottom = popenbottom;
		polytopslope = popentopslope;
		polybottomslope = polytopslope;
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

	// gl_midtexture can be inactive when rendering twosided portal midtextures
	if (gl_midtexture)
	{
		// PEGGING
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
	}

	// set top/bottom coords
	// Take the texture peg into account, rather than changing the offsets past
	// where the polygon might not be.
	wallVerts[3].y = FIXED_TO_FLOAT(h);
	wallVerts[0].y = FIXED_TO_FLOAT(l);
	wallVerts[2].y = FIXED_TO_FLOAT(hS);
	wallVerts[1].y = FIXED_TO_FLOAT(lS);

	// TODO: Actually use the surface's flags so that I don't have to do this
	blendmode = Surf->PolyFlags;

	// Render midtextures on two-sided lines with a z-buffer offset.
	// This will cause the midtexture appear on top, if a FOF overlaps with it.
	blendmode |= PF_Decal;

	if (!grTex)
		blendmode |= PF_NoTexture;

	if (!gl_drawing_stencil && tripwire == false && gl_frontsector->numlights)
	{
		if (!(blendmode & PF_Masked))
			HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, noencore, Surf, FOF_TRANSLUCENT, NULL, blendmode); // vanilla just uses PF_Masked here - if we run into any issues, maybe change to that
		else
			HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, noencore, Surf, FOF_CUTLEVEL, NULL, blendmode); // vanilla just uses PF_Masked here - if we run into any issues, maybe change to that
	}
	else if (!gl_drawing_stencil && !(blendmode & PF_Masked))
		HWR_AddTransparentWall(wallVerts, Surf, gl_midtexture, noencore, blendmode, false, lightnum, gl_frontsector->extra_colormap);
	else
		HWR_ProjectWall(wallVerts, Surf, blendmode, lightnum, gl_frontsector->extra_colormap);
}

static void HWR_ProcessTwoSidedSeg(FOutVector *wallVerts, gl_seg_bounds *b,
								   float cliplow, float cliphigh, FUINT lightnum, dboolean noencore, dboolean tripwire)
{
	FSurfaceInfo Surf = {};

	int32_t gl_toptexture = 0, gl_bottomtexture = 0;
	int32_t gl_midtexture;
	dboolean bothceilingssky = false; // turned on if both back and front ceilings are sky
	dboolean bothfloorssky = false; // likewise, but for floors

	Surf.PolyColor.s.alpha = 255;

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
					if ((b->worldhigh <= b->worldtop && b->worldhighslope <= b->worldtopslope)// Assuming ESLOPE is always on with my changes
						&& (b->worldhigh != b->worldtop || b->worldhighslope != b->worldtopslope))
						// Removing the second line above will render more rarely visible skywalls. Example: Cave garden ceiling in Dark race
					{
						wallVerts[0].y = FIXED_TO_FLOAT(b->worldhigh);
						wallVerts[1].y = FIXED_TO_FLOAT(b->worldhighslope);
						HWR_DrawSkyWall(wallVerts, &Surf);
					}
				}
				else
				{
					// Only the frontsector is sky, just draw a skywall from the front ceiling
					wallVerts[0].y = FIXED_TO_FLOAT(b->worldtop);
					wallVerts[1].y = FIXED_TO_FLOAT(b->worldtopslope);
					HWR_DrawSkyWall(wallVerts, &Surf);
				}
			}
			else if (gl_backsector->ceilingpic == skyflatnum)
			{
				// Only the backsector is sky, just draw a skywall from the front ceiling
				wallVerts[0].y = FIXED_TO_FLOAT(b->worldtop);
				wallVerts[1].y = FIXED_TO_FLOAT(b->worldtopslope);
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
					if ((b->worldlow >= b->worldbottom && b->worldlowslope >= b->worldbottomslope)
						&& (b->worldlow != b->worldbottom || b->worldlowslope != b->worldbottomslope)
						// Removing the second line above will render more rarely visible skywalls. Example: Cave garden ceiling in Dark race
						&& !(gl_sidedef->bottomtexture))
					{
						wallVerts[3].y = FIXED_TO_FLOAT(b->worldlow);
						wallVerts[2].y = FIXED_TO_FLOAT(b->worldlowslope);
						HWR_DrawSkyWall(wallVerts, &Surf);
					}
				}
				else
				{
					// Only the backsector has sky, just draw a skywall from the back floor
					wallVerts[3].y = FIXED_TO_FLOAT(b->worldbottom);
					wallVerts[2].y = FIXED_TO_FLOAT(b->worldbottomslope);
					HWR_DrawSkyWall(wallVerts, &Surf);
				}
			}
			else if ((gl_backsector->floorpic == skyflatnum) && !(gl_sidedef->bottomtexture))
			{
				// Only the backsector has sky, just draw a skywall from the back floor if there's no bottomtexture
				wallVerts[3].y = FIXED_TO_FLOAT(b->worldlow);
				wallVerts[2].y = FIXED_TO_FLOAT(b->worldlowslope);
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
			b->worldtop = b->worldhigh;
			b->worldtopslope = b->worldhighslope;
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
	if ((b->worldhighslope < b->worldtopslope || b->worldhigh < b->worldtop) && gl_toptexture)
	{
		HWR_ProcessTwoSidedSegTop(wallVerts, &Surf, b, cliplow, cliphigh, lightnum, gl_toptexture, noencore);
	}

	// check BOTTOM TEXTURE
	if ((b->worldlowslope > b->worldbottomslope || b->worldlow > b->worldbottom) && gl_bottomtexture)
	{
		HWR_ProcessTwoSidedSegBottom(wallVerts, &Surf, b, cliplow, cliphigh, lightnum, gl_bottomtexture, noencore);
	}

	gl_midtexture = R_GetTextureNum(gl_sidedef->midtexture);
	// twosided midtexture goes to stencil even if there's no midtexture
	// (software also renders twosided midtexture portals like this)
	if ((gl_midtexture && HWR_BlendMidtextureSurface(&Surf)) || gl_drawing_stencil)
	{
		HWR_ProcessTwoSidedSegMiddle(wallVerts, &Surf, b, cliplow, cliphigh, lightnum, gl_midtexture, noencore, tripwire);
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
				wallVerts[0].y = FIXED_TO_FLOAT(b->worldtop);
				wallVerts[1].y = FIXED_TO_FLOAT(b->worldtopslope);
				HWR_DrawSkyWall(wallVerts, &Surf);
			}

			if (gl_frontsector->floorpic == skyflatnum
				&& gl_backsector->floorpic != skyflatnum) // same thing here
			{
				wallVerts[3].y = FIXED_TO_FLOAT(b->worldbottom);
				wallVerts[2].y = FIXED_TO_FLOAT(b->worldbottomslope);
				wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(INT32_MIN); // draw to bottom of map space
				HWR_DrawSkyWall(wallVerts, &Surf);
			}
		}
	}
}

static void HWR_ProcessSingleSidedSeg(FOutVector *wallVerts, gl_seg_bounds *b,
									  float cliplow, float cliphigh, FUINT lightnum, dboolean noencore)
{
	FSurfaceInfo Surf;
	GLMapTexture_t *grTex = NULL;

	Surf.PolyColor.s.alpha = 255;

	// Single sided line... Deal only with the middletexture (if one exists)
	int32_t gl_midtexture = R_GetTextureNum(gl_sidedef->midtexture);

	if (gl_midtexture && gl_linedef->special != HORIZONSPECIAL) // (Ignore horizon line for OGL)
	{
		fixed_t texturevpeg;

		// PEGGING
		if ((gl_linedef->flags & (ML_DONTPEGBOTTOM|ML_NOSKEW)) == (ML_DONTPEGBOTTOM|ML_NOSKEW))
			texturevpeg = gl_frontsector->floorheight + textureheight[gl_sidedef->midtexture] - gl_frontsector->ceilingheight;
		else if (gl_linedef->flags & ML_DONTPEGBOTTOM)
			texturevpeg = b->worldbottom + textureheight[gl_sidedef->midtexture] - b->worldtop;
		else
			// top of texture at top
			texturevpeg = 0;

		texturevpeg += gl_sidedef->rowoffset + gl_sidedef->offsety_mid;

		grTex = HWR_GetTexture(gl_midtexture, noencore);

		wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
		wallVerts[0].t = wallVerts[1].t = (texturevpeg + (gl_frontsector->ceilingheight - gl_frontsector->floorheight)) * grTex->scaleY;
		wallVerts[0].s = wallVerts[3].s = (cliplow + gl_sidedef->offsetx_mid) * grTex->scaleX;
		wallVerts[2].s = wallVerts[1].s = (cliphigh + gl_sidedef->offsetx_mid) * grTex->scaleX;

		// Texture correction for slopes
		if (gl_linedef->flags & ML_NOSKEW)
		{
			wallVerts[3].t += (gl_frontsector->ceilingheight - b->worldtop) * grTex->scaleY;
			wallVerts[2].t += (gl_frontsector->ceilingheight - b->worldtopslope) * grTex->scaleY;
			wallVerts[0].t += (gl_frontsector->floorheight - b->worldbottom) * grTex->scaleY;
			wallVerts[1].t += (gl_frontsector->floorheight - b->worldbottomslope) * grTex->scaleY;
		}
		else if (gl_linedef->flags & ML_DONTPEGBOTTOM)
		{
			wallVerts[3].t = wallVerts[0].t + (b->worldbottom-b->worldtop) * grTex->scaleY;
			wallVerts[2].t = wallVerts[1].t + (b->worldbottomslope-b->worldtopslope) * grTex->scaleY;
		}
		else
		{
			wallVerts[0].t = wallVerts[3].t - (b->worldbottom-b->worldtop) * grTex->scaleY;
			wallVerts[1].t = wallVerts[2].t - (b->worldbottomslope-b->worldtopslope) * grTex->scaleY;
		}

		//Set textures properly on single sided walls that are sloped
		wallVerts[3].y = FIXED_TO_FLOAT(b->worldtop);
		wallVerts[0].y = FIXED_TO_FLOAT(b->worldbottom);
		wallVerts[2].y = FIXED_TO_FLOAT(b->worldtopslope);
		wallVerts[1].y = FIXED_TO_FLOAT(b->worldbottomslope);

		// I don't think that solid walls can use translucent linedef types...
		if (!gl_drawing_stencil && gl_frontsector->numlights)
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
				if (!gl_drawing_stencil && (grTex->mipmap.flags & TF_TRANSPARENT))
					HWR_AddTransparentWall(wallVerts, &Surf, gl_midtexture,  noencore, blendmode, false, lightnum, gl_frontsector->extra_colormap);
				else
					HWR_ProjectWall(wallVerts, &Surf, blendmode, lightnum, gl_frontsector->extra_colormap);
			}
		}
	}
	else
	{
		//Set textures properly on single sided walls that are sloped
		wallVerts[3].y = FIXED_TO_FLOAT(b->worldtop);
		wallVerts[0].y = FIXED_TO_FLOAT(b->worldbottom);
		wallVerts[2].y = FIXED_TO_FLOAT(b->worldtopslope);
		wallVerts[1].y = FIXED_TO_FLOAT(b->worldbottomslope);

		// When there's no midtexture, draw a skywall to prevent rendering behind it
		HWR_DrawSkyWall(wallVerts, &Surf);
	}

	if (!gl_curline->polyseg)
	{
		if (gl_frontsector->ceilingpic == skyflatnum) // It's a single-sided line with sky for its sector
		{
			wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(INT32_MAX); // draw to top of map space
			wallVerts[0].y = FIXED_TO_FLOAT(b->worldtop);
			wallVerts[1].y = FIXED_TO_FLOAT(b->worldtopslope);
			HWR_DrawSkyWall(wallVerts, &Surf);
		}
		if (gl_frontsector->floorpic == skyflatnum)
		{
			wallVerts[3].y = FIXED_TO_FLOAT(b->worldbottom);
			wallVerts[2].y = FIXED_TO_FLOAT(b->worldbottomslope);
			wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(INT32_MIN); // draw to bottom of map space
			HWR_DrawSkyWall(wallVerts, &Surf);
		}
	}
}

// struct for HWR_ProcessSegFOFs
typedef struct
{
	fixed_t highcut, lowcut;
	fixed_t lowcutslope, highcutslope;
} gl_fof_cuts;

// is_front_sector is true when this_sector is the front sector
static void HWR_ProcessSegFOFsSector(FOutVector *wallVerts, gl_fof_cuts *c,
									 float cliplow, float cliphigh, FUINT lightnum,
									 fixed_t v1x, fixed_t v1y, fixed_t v2x, fixed_t v2y,
									 sector_t *this_sector, sector_t *other_sector, dboolean is_front_sector, dboolean noencore)
{
	FSurfaceInfo Surf;
	GLMapTexture_t *grTex = NULL;
	extracolormap_t *colormap = gl_frontsector->extra_colormap;

	ffloor_t * rover;
	fixed_t h, l; // 3D sides
	fixed_t hS, lS;

	// Used for height comparisons and etc across FOFs and slopes
	fixed_t high1, highslope1, low1, lowslope1;
	fixed_t high2, highslope2, low2, lowslope2;

	int32_t texnum;
	line_t * newline = NULL; // Multi-Property FOF

	for (rover = this_sector->ffloors; rover; rover = rover->next)
	{
		if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_RENDERSIDES))
			continue;

		if (is_front_sector)
		{
			if (!(rover->fofflags & FOF_ALLSIDES || rover->fofflags & FOF_INVERTSIDES))
				continue;
		}
		else
		{
			if (!(rover->fofflags & FOF_ALLSIDES) && rover->fofflags & FOF_INVERTSIDES)
				continue;
		}

		SLOPEPARAMS(*rover->t_slope, high1, highslope1, *rover->topheight)
		SLOPEPARAMS(*rover->b_slope, low1,  lowslope1,  *rover->bottomheight)

		if ((high1 < c->lowcut || highslope1 < c->lowcutslope) || (low1 > c->highcut || lowslope1 > c->highcutslope))
			continue;

		ffloor_t * r2;
		for (r2 = other_sector->ffloors; r2; r2 = r2->next)
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

			if ((high2 < c->lowcut || highslope2 < c->lowcutslope) || (low2 > c->highcut || lowslope2 > c->highcutslope))
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
		if (h >= c->highcut && hS >= c->highcutslope)
		{
			h = c->highcut;
			hS = c->highcutslope;
		}

		if (l <= c->lowcut && lS <= c->lowcutslope)
		{
			l = c->lowcut;
			lS = c->lowcutslope;
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
			dboolean attachtobottom = false;
			dboolean slopeskew = false; // skew FOF walls with slopes?

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
				wallVerts[3].t = ((*rover->topheight - h) + texturevpeg) * grTex->scaleY;
				wallVerts[2].t = ((*rover->topheight - hS) + texturevpeg) * grTex->scaleY;
				wallVerts[0].t = ((*rover->topheight - l) + texturevpeg) * grTex->scaleY;
				wallVerts[1].t = ((*rover->topheight - lS) + texturevpeg) * grTex->scaleY;
			}
			else
			{
				if (!attachtobottom) // skew by top
				{
					wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
					wallVerts[0].t = ((h - l) + texturevpeg) * grTex->scaleY;
					wallVerts[1].t = ((hS - lS) + texturevpeg) * grTex->scaleY;
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

			if (other_sector->numlights)
				HWR_SplitWall(other_sector, wallVerts, 0, false, &Surf, rover->fofflags, rover, blendmode);
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
					Surf.PolyColor.s.alpha = (uint8_t)(max(0, min(rover->alpha, 255)));
				}
			}

			if (other_sector->numlights)
				HWR_SplitWall(other_sector, wallVerts, texnum, noencore, &Surf, rover->fofflags, rover, blendmode);
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

// is_front_sector is true when this_sector is the front sector
static void HWR_ProcessSegFOFs(FOutVector *wallVerts, gl_seg_bounds *b,
							   float cliplow, float cliphigh, FUINT lightnum,
							   fixed_t v1x, fixed_t v1y, fixed_t v2x, fixed_t v2y, dboolean noencore)
{
	gl_fof_cuts c;

	c.lowcut = max(b->worldbottom, b->worldlow);
	c.highcut = min(b->worldtop, b->worldhigh);
	c.lowcutslope = max(b->worldbottomslope, b->worldlowslope);
	c.highcutslope = min(b->worldtopslope, b->worldhighslope);

	if (gl_backsector->ffloors)
	{
		HWR_ProcessSegFOFsSector(wallVerts, &c, cliplow, cliphigh, lightnum,
								 v1x, v1y, v2x, v2y, gl_backsector, gl_frontsector, false, noencore);
	}

	if (gl_frontsector->ffloors) // Putting this seperate should allow 2 FOF sectors to be connected without too many errors? I think?
	{
		HWR_ProcessSegFOFsSector(wallVerts, &c, cliplow, cliphigh, lightnum,
								 v1x, v1y, v2x, v2y, gl_frontsector, gl_backsector, true, noencore);
	}
}

void HWR_ProcessSeg(void) // Sort of like GLWall::Process in GZDoom
{
	FOutVector wallVerts[4] = {0};
	v2d_t vs, ve; // start, end vertices of 2d line (view from above)

	gl_seg_bounds b = {0};
	fixed_t v1x, v1y, v2x, v2y;

	float cliplow = 0.0f, cliphigh = 0.0f;

	FUINT lightnum = 255;

	dboolean tripwire;
	dboolean noencore;

	gl_sidedef = gl_curline->sidedef;
	gl_linedef = gl_curline->linedef;

	tripwire = P_IsLineTripWire(gl_linedef);
	noencore = (gl_linedef->flags & ML_TFERLINE);

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

	// get boundaries for front sector
	SLOPEPARAMS(gl_frontsector->c_slope, b.worldtop,    b.worldtopslope,    gl_frontsector->ceilingheight)
	SLOPEPARAMS(gl_frontsector->f_slope, b.worldbottom, b.worldbottomslope, gl_frontsector->floorheight)

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
	{
		fixed_t texturehpeg = gl_sidedef->textureoffset + gl_curline->offset;
		cliplow = (float)texturehpeg;
		cliphigh = (float)(texturehpeg + (gl_curline->flength * FRACUNIT));
	}

	if (tripwire == false)
	{
		lightnum = HWR_CalcWallLight(gl_frontsector->lightlevel, gl_curline);
	}

	// two sided line
	if (gl_backsector)
	{
		// get boundaries for top and bottom textures
		SLOPEPARAMS(gl_backsector->c_slope, b.worldhigh, b.worldhighslope, gl_backsector->ceilingheight)
		SLOPEPARAMS(gl_backsector->f_slope, b.worldlow,  b.worldlowslope,  gl_backsector->floorheight)

		HWR_ProcessTwoSidedSeg(wallVerts, &b, cliplow, cliphigh, lightnum, noencore, tripwire);
	}
	else
	{
		HWR_ProcessSingleSidedSeg(wallVerts, &b, cliplow, cliphigh, lightnum, noencore);
	}

	if (!gl_drawing_stencil && gl_backsector && !Tag_Compare(&gl_frontsector->tags, &gl_backsector->tags) && (gl_backsector->ffloors || gl_frontsector->ffloors))
	{
		HWR_ProcessSegFOFs(wallVerts, &b, cliplow, cliphigh, lightnum,
						   v1x, v1y, v2x, v2y, noencore);
	}
}
#undef SLOPEPARAMS

#endif
