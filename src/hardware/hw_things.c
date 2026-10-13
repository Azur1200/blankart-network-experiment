// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2022 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_things.c
/// \brief Sprite rendering

#ifdef HWRENDER
#include "hw_glob.h"
#include "hw_batching.h"
#include "../g_game.h"
#include "../k_items.h"
#include "../p_slopes.h"
#include "../p_local.h"
#include "../z_zone.h"
#include "../r_fps.h"
#include "../r_local.h"
#include "../qs22j.h"

static void HWR_ProjectSprite(mobj_t *thing);
static void HWR_ProjectPrecipitationSprite(precipmobj_t *thing);

// sprites are drawn after all wall and planes are rendered, so that
// sprite translucency effects apply on the rendered view (instead of the background sky!!)

typedef struct
{
	FOutVector verts[4];
	gl_vissprite_t *spr;
} zbuffersprite_t;

typedef struct
{
	uint32_t gl_visspritecount;
	gl_vissprite_t *gl_visspritechunks[MAXVISSPRITES >> VISSPRITECHUNKBITS];
	zbuffersprite_t linkdrawlist[MAXVISSPRITES];
	uint32_t linkdrawcount;
} gl_sprite_state_t;

// TODO this array is ~3 megabytes because of linkdrawlist...
// maybe turn that into a dynamic array
static gl_sprite_state_t state_stack[MAXPORTALS_CAP+1] = {0};
static int stack_level = 0;
static gl_sprite_state_t *cst = &state_stack[0]; // current state

// --------------------------------------------------------------------------
// HWR_ClearSprites
// Called at viewpoint start.
// --------------------------------------------------------------------------
void HWR_ClearSprites(void)
{
	cst->gl_visspritecount = 0;
}

// pushes all sprite rendering state to stack
void HWR_PushSpriteState(void)
{
	if (stack_level == MAXPORTALS_CAP)
		I_Error("HWR_PushSpriteState: State stack overflow");

	stack_level++;
	cst++;
}

void HWR_PopSpriteState(void)
{
	if (stack_level == 0)
		I_Error("HWR_PopSpriteState: State stack underflow");

	stack_level--;
	cst--;
}

// --------------------------------------------------------------------------
// HWR_NewVisSprite
// --------------------------------------------------------------------------
static gl_vissprite_t gl_overflowsprite;

static gl_vissprite_t *HWR_GetVisSprite(uint32_t num)
{
		uint32_t chunk = num >> VISSPRITECHUNKBITS;

		// Allocate chunk if necessary
		if (!cst->gl_visspritechunks[chunk])
			Z_Malloc(sizeof(gl_vissprite_t) * VISSPRITESPERCHUNK, PU_LEVEL, &cst->gl_visspritechunks[chunk]);

		return cst->gl_visspritechunks[chunk] + (num & VISSPRITEINDEXMASK);
}

static gl_vissprite_t *HWR_NewVisSprite(void)
{
	if (cst->gl_visspritecount == MAXVISSPRITES)
		return &gl_overflowsprite;

	return HWR_GetVisSprite(cst->gl_visspritecount++);
}

// A hack solution for transparent surfaces appearing on top of linkdraw sprites.
// Keep a list of linkdraw sprites and draw their shapes to the z-buffer after all other
// sprite drawing is done. (effectively the z-buffer drawing of linkdraw sprites is delayed)
// NOTE: This will no longer be necessary once full translucent sorting is implemented, where
// translucent sprites and surfaces are sorted together.

// add the necessary data to the list for delayed z-buffer drawing
static void HWR_LinkDrawHackAdd(FOutVector *verts, gl_vissprite_t *spr)
{
	if (cst->linkdrawcount < MAXVISSPRITES)
	{
		memcpy(cst->linkdrawlist[cst->linkdrawcount].verts, verts, sizeof(FOutVector) * 4);
		cst->linkdrawlist[cst->linkdrawcount].spr = spr;
		cst->linkdrawcount++;
	}
}

// process and clear the list of sprites for delayed z-buffer drawing
static void HWR_LinkDrawHackFinish(void)
{
	uint32_t i;
	FSurfaceInfo surf = {};
	surf.PolyColor.rgba = 0xFFFFFFFF;
	surf.TintColor.rgba = 0xFFFFFFFF;
	surf.FadeColor.rgba = 0xFFFFFFFF;
	surf.LightInfo.light_level = 0;
	surf.LightInfo.fade_start = 0;
	surf.LightInfo.fade_end = 31;
	surf.LightInfo.newfade = false;
	for (i = 0; i < cst->linkdrawcount; i++)
	{
		// draw sprite shape, only to z-buffer
		HWR_GetPatch(cst->linkdrawlist[i].spr->gpatch);
		HWR_ProcessPolygon(&surf, cst->linkdrawlist[i].verts, 4, PF_Translucent|PF_Occlude|PF_Invisible, 0, false);
	}
	// reset list
	cst->linkdrawcount = 0;
}

//
// HWR_DoCulling
// Hardware version of R_DoCulling
// (see r_main.c)
static dboolean HWR_DoCulling(line_t *cullheight, line_t *viewcullheight, float vz, float bottomh, float toph)
{
	float cullplane;

	if (!cullheight)
		return false;

	cullplane = FIXED_TO_FLOAT(cullheight->frontsector->floorheight);
	if (cullheight->args[1]) // Group culling
	{
		if (!viewcullheight)
			return false;

		// Make sure this is part of the same group
		if (viewcullheight->frontsector == cullheight->frontsector)
		{
			// OK, we can cull
			if (vz > cullplane && toph < cullplane) // Cull if below plane
				return true;

			if (bottomh > cullplane && vz <= cullplane) // Cull if above plane
				return true;
		}
	}
	else // Quick culling
	{
		if (vz > cullplane && toph < cullplane) // Cull if below plane
			return true;

		if (bottomh > cullplane && vz <= cullplane) // Cull if above plane
			return true;
	}

	return false;
}

static patch_t *shadowpatch = NULL;

static void HWR_DrawDropShadow(mobj_t *thing, fixed_t scale)
{
	patch_t *gpatch = NULL;
	GLPatch_t *hwrpatch;
	FOutVector shadowVerts[4];
	FSurfaceInfo sSurf = {};
	float fscale; float fx; float fy; float offset;
	extracolormap_t *colormap = NULL;
	FBITFIELD blendmode = PF_ReverseSubtract;
	int32_t shader = SHADER_NONE;
	uint8_t i;
	int32_t heightsec, phs;
	int8_t flip = P_MobjFlip(thing);

	int32_t light;
	fixed_t scalemul;
	fixed_t floordiff;
	fixed_t groundz;
	pslope_t *groundslope;

	// uncapped/interpolation
	interpmobjstate_t interp = {0};

	R_InterpolateMobjState(thing, R_GetTimeFrac(RTF_LEVEL), &interp);

	// yes, this can happen. but it only creates exploding shadows in SOME maps. like MAPH9
	if (interp.radius <= 0)
		return;

	// sprite offset
	interp.x += thing->sprxoff;
	interp.y += thing->spryoff;
	interp.z += thing->sprzoff;

	groundz = R_GetShadowZ(thing, &groundslope);

	heightsec = thing->subsector->sector->heightsec;
	if (viewplayer->mo && viewplayer->mo->subsector)
		phs = viewplayer->mo->subsector->sector->heightsec;
	else
		phs = -1;

	if (heightsec != -1 && phs != -1) // only clip things which are in special sectors
	{
		if (gl_viewz < FIXED_TO_FLOAT(sectors[phs].floorheight) ?
			thing->z >= sectors[heightsec].floorheight :
			thing->z < sectors[heightsec].floorheight)
			return;
		if (gl_viewz > FIXED_TO_FLOAT(sectors[phs].ceilingheight) ?
			thing->z < sectors[heightsec].ceilingheight && gl_viewz >= FIXED_TO_FLOAT(sectors[heightsec].ceilingheight) :
			thing->z >= sectors[heightsec].ceilingheight)
			return;
	}

	floordiff = abs((flip < 0 ? interp.height : 0) + interp.z - groundz);

	if (!shadowpatch || !shadowpatch->hardware)
	{
		CONS_Debug(DBG_RENDER, "Recaching shadow patch\n");
		shadowpatch = (patch_t *)W_CachePatchName("DSHADOW", PU_SPRITE);
	}

	gpatch = shadowpatch;

	if (!gpatch)
		return;

	HWR_GetPatch(gpatch);

	hwrpatch = (GLPatch_t *)gpatch->hardware;

	// shouldnt ever be the case after HWR_GetPatch
	if (!hwrpatch || !hwrpatch->mipmap->format) // why do we check for the mipmäp´s format?
		return;

	scalemul = FixedMul(FRACUNIT - floordiff/640, scale);
	scalemul = FixedMul(scalemul, (interp.radius*2) / gpatch->height);

	fscale = FIXED_TO_FLOAT(scalemul);
	fx = FIXED_TO_FLOAT(interp.x);
	fy = FIXED_TO_FLOAT(interp.y);

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	if (thing && fabsf(fscale - 1.0f) > 1.0E-36f)
		offset = (gpatch->height/2) * fscale;
	else
		offset = (float)(gpatch->height/2);

	shadowVerts[2].x = shadowVerts[3].x = fx + offset;
	shadowVerts[1].x = shadowVerts[0].x = fx - offset;
	shadowVerts[1].z = shadowVerts[2].z = fy - offset;
	shadowVerts[0].z = shadowVerts[3].z = fy + offset;

	for (i = 0; i < 4; i++)
	{
		float oldx = shadowVerts[i].x;
		float oldy = shadowVerts[i].z;
		shadowVerts[i].x = fx + ((oldx - fx) * gl_viewcos) - ((oldy - fy) * gl_viewsin);
		shadowVerts[i].z = fy + ((oldx - fx) * gl_viewsin) + ((oldy - fy) * gl_viewcos);
	}

	if (groundslope)
	{
		for (i = 0; i < 4; i++)
		{
			fixed_t slopez = P_GetSlopeZAt(groundslope, FLOAT_TO_FIXED(shadowVerts[i].x), FLOAT_TO_FIXED(shadowVerts[i].z));
			shadowVerts[i].y = FIXED_TO_FLOAT(slopez) + flip * 0.05f;
		}
	}
	else
	{
		for (i = 0; i < 4; i++)
			shadowVerts[i].y = FIXED_TO_FLOAT(groundz) + flip * 0.05f;
	}

	shadowVerts[0].s = shadowVerts[3].s = 0;
	shadowVerts[2].s = shadowVerts[1].s = hwrpatch->max_s;

	shadowVerts[3].t = shadowVerts[2].t = 0;
	shadowVerts[0].t = shadowVerts[1].t = hwrpatch->max_t;

	if (!(thing->renderflags & RF_NOCOLORMAPS))
	{
		if (thing->subsector->sector->numlights)
		{
			// Always use the light at the top instead of whatever I was doing before
			light = R_GetPlaneLight(thing->subsector->sector, groundz, false);

			if (*thing->subsector->sector->lightlist[light].extra_colormap)
				colormap = *thing->subsector->sector->lightlist[light].extra_colormap;
		}
		else if (thing->subsector->sector->extra_colormap)
			colormap = thing->subsector->sector->extra_colormap;
	}

	HWR_Lighting(&sSurf, 255, colormap, P_SectorUsesDirectionalLighting(thing->subsector->sector));
	sSurf.PolyColor.s.alpha = 255;

	if (thing->whiteshadow == true)
	{
		blendmode = PF_Additive;
	}

	if (HWR_UseShader())
	{
		shader = SHADER_SPRITE;
		blendmode |= PF_ColorMapped;
	}

	HWR_ProcessPolygon(&sSurf, shadowVerts, 4, blendmode|PF_Modulated, shader, false);
}

// This is expecting a pointer to an array containing 4 wallVerts for a sprite
static void HWR_RotateSpritePolyToAim(gl_vissprite_t *spr, FOutVector *wallVerts, const dboolean precip)
{
	const dboolean affine = ((spr->mobj && !P_MobjWasRemoved(spr->mobj)) && ((spr->mobj->player != NULL) || R_ThingIsAffineSprite(spr->mobj)));

	if ((cv_glspritebillboarding.value == 1 || (cv_glspritebillboarding.value == 2 &&
		(spr->mobj->player)))
		&& spr && spr->mobj && !R_ThingIsPaperSprite(spr->mobj)
		&& wallVerts)
	{
		// uncapped/interpolation
		interpmobjstate_t interp = {0};

		// do interpolation
		if (spr->precip)
		{
			R_InterpolatePrecipMobjState((precipmobj_t *)spr->mobj, R_GetTimeFrac(RTF_LEVEL), &interp);
		}
		else
		{
			R_InterpolateMobjState(spr->mobj, R_GetTimeFrac(RTF_LEVEL), &interp);
		}

		float basey = FIXED_TO_FLOAT(interp.z);
		float lowy = wallVerts[0].y;
		if (!precip && P_MobjFlip(spr->mobj) == -1) // precip doesn't have eflags so they can't flip
		{
			basey = FIXED_TO_FLOAT(spr->mobj->z + spr->mobj->height);
		}
		// Rotate sprites to fully billboard with the camera
		// X, Y, AND Z need to be manipulated for the polys to rotate around the
		// origin, because of how the origin setting works I believe that should
		// be mobj->z or mobj->z + mobj->height

		if (affine)
		{
			wallVerts[0].y = (spr->affine.p1.y * gl_viewludsin) + spr->affine.root.y;
			wallVerts[0].x += (spr->affine.p1.y * gl_viewludcos) * gl_viewcos;
			wallVerts[0].z += (spr->affine.p1.y * gl_viewludcos) * gl_viewsin;

			wallVerts[1].y = (spr->affine.p2.y * gl_viewludsin) + spr->affine.root.y;
			wallVerts[1].x += (spr->affine.p2.y * gl_viewludcos) * gl_viewcos;
			wallVerts[1].z += (spr->affine.p2.y * gl_viewludcos) * gl_viewsin;

			wallVerts[2].y = (spr->affine.p3.y * gl_viewludsin) + spr->affine.root.y;
			wallVerts[2].x += (spr->affine.p3.y * gl_viewludcos) * gl_viewcos;
			wallVerts[2].z += (spr->affine.p3.y * gl_viewludcos) * gl_viewsin;

			wallVerts[3].y = (spr->affine.p4.y * gl_viewludsin) + spr->affine.root.y;
			wallVerts[3].x += (spr->affine.p4.y * gl_viewludcos) * gl_viewcos;
			wallVerts[3].z += (spr->affine.p4.y * gl_viewludcos) * gl_viewsin;
		}
		else
		{
			wallVerts[2].y = wallVerts[3].y = (spr->gzt - basey) * gl_viewludsin + basey;
			wallVerts[0].y = wallVerts[1].y = (lowy - basey) * gl_viewludsin + basey;
			// translate back to be around 0 before translating back
			wallVerts[3].x += ((spr->gzt - basey) * gl_viewludcos) * gl_viewcos;
			wallVerts[2].x += ((spr->gzt - basey) * gl_viewludcos) * gl_viewcos;

			wallVerts[0].x += ((lowy - basey) * gl_viewludcos) * gl_viewcos;
			wallVerts[1].x += ((lowy - basey) * gl_viewludcos) * gl_viewcos;

			wallVerts[3].z += ((spr->gzt - basey) * gl_viewludcos) * gl_viewsin;
			wallVerts[2].z += ((spr->gzt - basey) * gl_viewludcos) * gl_viewsin;

			wallVerts[0].z += ((lowy - basey) * gl_viewludcos) * gl_viewsin;
			wallVerts[1].z += ((lowy - basey) * gl_viewludcos) * gl_viewsin;
		}
	}
}

static void HWR_SplitSprite(gl_vissprite_t *spr)
{
	FOutVector wallVerts[4];
	FOutVector baseWallVerts[4]; // This is what the verts should end up as
	patch_t *gpatch;
	GLPatch_t *hwrpatch;
	FSurfaceInfo Surf = {};
	extracolormap_t *colormap = NULL;
	int32_t lightlevel;
	dboolean lightset = true;
	FBITFIELD blend = 0;
	FBITFIELD occlusion;
	int32_t shader = SHADER_NONE;
	dboolean use_linkdraw_hack = false;
	uint8_t alpha;

	int32_t i;
	float realtop, realbot, top, bot;
	float ttop, tbot, tmult;
	float bheight;
	float realheight, heightmult;
	const sector_t *sector = spr->mobj->subsector->sector;
	const lightlist_t *list = sector->lightlist;
	float endrealtop, endrealbot, endtop, endbot;
	float endbheight;
	float endrealheight;
	fixed_t temp;
	fixed_t v1x, v1y, v2x, v2y;

	gpatch = spr->gpatch;

	// cache the patch in the graphics card memory
	//12/12/99: Hurdler: same comment as above (for md2)
	//Hurdler: 25/04/2000: now support colormap in hardware mode
	HWR_GetMappedPatch(gpatch, spr->colormap);

	hwrpatch = (GLPatch_t *)gpatch->hardware;

	baseWallVerts[0].x = baseWallVerts[3].x = spr->x1;
	baseWallVerts[2].x = baseWallVerts[1].x = spr->x2;
	baseWallVerts[0].z = baseWallVerts[3].z = spr->z1;
	baseWallVerts[1].z = baseWallVerts[2].z = spr->z2;

	baseWallVerts[2].y = baseWallVerts[3].y = spr->gzt;
	baseWallVerts[0].y = baseWallVerts[1].y = spr->gz;

	v1x = FLOAT_TO_FIXED(spr->x1);
	v1y = FLOAT_TO_FIXED(spr->z1);
	v2x = FLOAT_TO_FIXED(spr->x2);
	v2y = FLOAT_TO_FIXED(spr->z2);

	if (spr->flip)
	{
		baseWallVerts[0].s = baseWallVerts[3].s = hwrpatch->max_s;
		baseWallVerts[2].s = baseWallVerts[1].s = 0;
	}
	else
	{
		baseWallVerts[0].s = baseWallVerts[3].s = 0;
		baseWallVerts[2].s = baseWallVerts[1].s = hwrpatch->max_s;
	}

	// flip the texture coords (look familiar?)
	if (spr->vflip)
	{
		baseWallVerts[3].t = baseWallVerts[2].t = hwrpatch->max_t;
		baseWallVerts[0].t = baseWallVerts[1].t = 0;
	}
	else
	{
		baseWallVerts[3].t = baseWallVerts[2].t = 0;
		baseWallVerts[0].t = baseWallVerts[1].t = hwrpatch->max_t;
	}

	// push it toward the camera to mitigate floor-clipping sprites
	if (/*!R_ThingIsPaperSprite(spr->mobj) && */!(spr->mobj->terrain && spr->mobj->terrain->floorClip)) // but not for papersprites or for floorclip
	{
		// Let dispoffset work first since this adjust each vertex
		HWR_RotateSpritePolyToAim(spr, baseWallVerts, false);

		// push it toward the camera to mitigate floor-clipping sprites
		float sprdist = 0;

		float f_xdiff = (spr->x1 - gl_viewx);
		float f_ydiff = (spr->z1 - gl_viewy);
		float f_zdiff = (spr->gzt - gl_viewz);

		sprdist = sqrtf((f_xdiff*f_xdiff) + (f_ydiff*f_ydiff) + (f_zdiff*f_zdiff));

		float distfact = ((2.0f*spr->dispoffset) + 20.0f) / sprdist;
		for (i = 0; i < 4; i++)
		{
			baseWallVerts[i].x += (gl_viewx - baseWallVerts[i].x)*distfact;
			baseWallVerts[i].z += (gl_viewy - baseWallVerts[i].z)*distfact;
			baseWallVerts[i].y += (gl_viewz - baseWallVerts[i].y)*distfact;
		}
	}

	realtop = top = baseWallVerts[3].y;
	realbot = bot = baseWallVerts[0].y;
	ttop = baseWallVerts[3].t;
	tbot = baseWallVerts[0].t;
	tmult = (tbot - ttop) / (top - bot);

	endrealtop = endtop = baseWallVerts[2].y;
	endrealbot = baseWallVerts[1].y;

	// copy the contents of baseWallVerts into the drawn wallVerts array
	// baseWallVerts is used to know the final shape to easily get the vertex
	// co-ordinates
	memcpy(wallVerts, baseWallVerts, sizeof(baseWallVerts));

	// if sprite has linkdraw, then dont write to z-buffer (by not using PF_Occlude)
	// this will result in sprites drawn afterwards to be drawn on top like intended when using linkdraw.
	if ((spr->mobj->flags2 & MF2_LINKDRAW) && spr->mobj->tracer)
		occlusion = 0;
	else
		occlusion = PF_Occlude;

	// Determine the blendmode and translucency value
	{
		uint32_t blendmode, trans;
		if (spr->mobj->renderflags & RF_BLENDMASK)
			blendmode = (spr->mobj->renderflags & RF_BLENDMASK) >> RF_BLENDSHIFT;
		else
			blendmode = (spr->mobj->frame & FF_BLENDMASK) >> FF_BLENDSHIFT;
		if (blendmode)
			blendmode++; // realign to constants

		if (spr->mobj->renderflags & RF_TRANSMASK)
			trans = (spr->mobj->renderflags & RF_TRANSMASK) >> RF_TRANSSHIFT;
		else
			trans = (spr->mobj->frame & FF_TRANSMASK) >> FF_TRANSSHIFT;
		if (trans >= NUMTRANSMAPS)
			return; // cap

		blend = HWR_SurfaceBlend(blendmode, trans, &Surf);

		// if sprite has PF_ALWAYSONTOP, draw on top of everything.
		if (cv_debugrender_spriteclip.value || spr->mobj->renderflags & RF_ALWAYSONTOP)
			blend |= PF_NoDepthTest;

		if (!trans && !blendmode)
		{
			// BP: i agree that is little better in environement but it don't
			//     work properly under glide nor with fogcolor to ffffff :(
			// Hurdler: PF_Environement would be cool, but we need to fix
			//          the issue with the fog before
			blend |= occlusion;
			if (!occlusion) use_linkdraw_hack = true;
		}
	}

	Surf.PolyColor.s.alpha = FixedMul(R_GetThingFade(spr->mobj), Surf.PolyColor.s.alpha);

	if (HWR_UseShader())
	{
		shader = SHADER_SPRITE;
		blend |= PF_ColorMapped;
	}

	alpha = Surf.PolyColor.s.alpha;

	// Start with the lightlevel and colormap from the top of the sprite
	lightlevel = *list[sector->numlights - 1].lightlevel;
	if (!R_ThingIsFullBright(spr->mobj) && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
		colormap = *list[sector->numlights - 1].extra_colormap;

	i = 0;
	temp = FLOAT_TO_FIXED(realtop);

	lightset = HWR_OverrideObjectLightLevel(spr->mobj, &lightlevel);

	for (i = 1; i < sector->numlights; i++)
	{
		fixed_t h = P_GetLightZAt(&sector->lightlist[i], spr->mobj->x, spr->mobj->y);
		if (h <= temp)
		{
			if (!lightset)
				lightlevel = *list[i-1].lightlevel > 255 ? 255 : *list[i-1].lightlevel;
			if (!R_ThingIsFullBright(spr->mobj) && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
				colormap = *list[i-1].extra_colormap;
			break;
		}
	}

	if (!lightset)
		HWR_ObjectLightLevelPost(spr, sector, &lightlevel, false);

	for (i = 0; i < sector->numlights; i++)
	{
		if (endtop < endrealbot && top < realbot)
			return;

		// even if we aren't changing colormap or lightlevel, we still need to continue drawing down the sprite
		if (!(list[i].flags & FOF_NOSHADE) && (list[i].flags & FOF_CUTSPRITES))
		{
			if (!lightset)
			{
				lightlevel = *list[i].lightlevel > 255 ? 255 : *list[i].lightlevel;
				HWR_ObjectLightLevelPost(spr, sector, &lightlevel, false);
			}

			if (!R_ThingIsFullBright(spr->mobj) && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
				colormap = *list[i].extra_colormap;
		}

		if (i + 1 < sector->numlights)
		{
			temp = P_GetLightZAt(&list[i+1], v1x, v1y);
			bheight = FIXED_TO_FLOAT(temp);
			temp = P_GetLightZAt(&list[i+1], v2x, v2y);
			endbheight = FIXED_TO_FLOAT(temp);
		}
		else
		{
			bheight = realbot;
			endbheight = endrealbot;
		}

		if (endbheight >= endtop && bheight >= top)
			continue;

		bot = bheight;

		if (bot < realbot)
			bot = realbot;

		endbot = endbheight;

		if (endbot < endrealbot)
			endbot = endrealbot;

		wallVerts[3].t = ttop + ((realtop - top) * tmult);
		wallVerts[2].t = ttop + ((endrealtop - endtop) * tmult);
		wallVerts[0].t = ttop + ((realtop - bot) * tmult);
		wallVerts[1].t = ttop + ((endrealtop - endbot) * tmult);

		wallVerts[3].y = top;
		wallVerts[2].y = endtop;
		wallVerts[0].y = bot;
		wallVerts[1].y = endbot;

		// The x and y only need to be adjusted in the case that it's not a papersprite
		if ((cv_glspritebillboarding.value == 1 || (cv_glspritebillboarding.value == 2 && (spr->mobj->player)))
			&& !cv_glshearing.value
			&& spr->mobj && !R_ThingIsPaperSprite(spr->mobj))
		{
			// Get the x and z of the vertices so billboarding draws correctly
			realheight = realbot - realtop;
			endrealheight = endrealbot - endrealtop;
			heightmult = (realtop - top) / realheight;
			wallVerts[3].x = baseWallVerts[3].x + (baseWallVerts[3].x - baseWallVerts[0].x) * heightmult;
			wallVerts[3].z = baseWallVerts[3].z + (baseWallVerts[3].z - baseWallVerts[0].z) * heightmult;

			heightmult = (endrealtop - endtop) / endrealheight;
			wallVerts[2].x = baseWallVerts[2].x + (baseWallVerts[2].x - baseWallVerts[1].x) * heightmult;
			wallVerts[2].z = baseWallVerts[2].z + (baseWallVerts[2].z - baseWallVerts[1].z) * heightmult;

			heightmult = (realtop - bot) / realheight;
			wallVerts[0].x = baseWallVerts[3].x + (baseWallVerts[3].x - baseWallVerts[0].x) * heightmult;
			wallVerts[0].z = baseWallVerts[3].z + (baseWallVerts[3].z - baseWallVerts[0].z) * heightmult;

			heightmult = (endrealtop - endbot) / endrealheight;
			wallVerts[1].x = baseWallVerts[2].x + (baseWallVerts[2].x - baseWallVerts[1].x) * heightmult;
			wallVerts[1].z = baseWallVerts[2].z + (baseWallVerts[2].z - baseWallVerts[1].z) * heightmult;
		}

		HWR_Lighting(&Surf, lightlevel, colormap, P_SectorUsesDirectionalLighting(sector) && !R_ThingIsFullBright(spr->mobj));

		Surf.PolyColor.s.alpha = alpha;

		HWR_ProcessPolygon(&Surf, wallVerts, 4, blend|PF_Modulated, shader, false);

		if (use_linkdraw_hack)
			HWR_LinkDrawHackAdd(wallVerts, spr);

		top = bot;
		endtop = endbot;
	}

	bot = realbot;
	endbot = endrealbot;
	if (endtop <= endrealbot && top <= realbot)
		return;

	// If we're ever down here, somehow the above loop hasn't draw all the light levels of sprite
	wallVerts[3].t = ttop + ((realtop - top) * tmult);
	wallVerts[2].t = ttop + ((endrealtop - endtop) * tmult);
	wallVerts[0].t = ttop + ((realtop - bot) * tmult);
	wallVerts[1].t = ttop + ((endrealtop - endbot) * tmult);

	wallVerts[3].y = top;
	wallVerts[2].y = endtop;
	wallVerts[0].y = bot;
	wallVerts[1].y = endbot;

	HWR_Lighting(&Surf, lightlevel, colormap, P_SectorUsesDirectionalLighting(sector));

	Surf.PolyColor.s.alpha = alpha;

	HWR_ProcessPolygon(&Surf, wallVerts, 4, blend|PF_Modulated, shader, false);

	if (use_linkdraw_hack)
		HWR_LinkDrawHackAdd(wallVerts, spr);
}

static void HWR_DrawBoundingBox(gl_vissprite_t *vis)
{
	FOutVector v[24];
	FSurfaceInfo Surf = {0};
	RGBA_t *palette = HWR_GetTexturePalette();

	//
	// create a cube (side view)
	//
	//  5--4  3
	//        |
	//        |
	//  0--1  2
	//
	// repeat this 4 times (overhead)
	//
	//
	// 17    20  21    11
	//    16 15  14 10
	// 27 22  *--*  07 12
	//        |  |
	// 26 23  *--*  06 13
	//    24 00  01 02
	// 25    05  04    03
	//

	v[000].x = v[005].x = v[015].x = v[016].x = v[017].x = v[020].x =
		v[022].x = v[023].x = v[024].x = v[025].x = v[026].x = v[027].x = vis->x1; // west

	v[001].x = v[002].x = v[003].x = v[004].x = v[006].x = v[007].x =
		v[010].x = v[011].x = v[012].x = v[013].x = v[014].x = v[021].x = vis->x2; // east

	v[000].z = v[001].z = v[002].z = v[003].z = v[004].z = v[005].z =
		v[006].z = v[013].z = v[023].z = v[024].z = v[025].z = v[026].z = vis->z1; // south

	v[007].z = v[010].z = v[011].z = v[012].z = v[014].z = v[015].z =
		v[016].z = v[017].z = v[020].z = v[021].z = v[022].z = v[027].z = vis->z2; // north

	v[000].y = v[001].y = v[002].y = v[006].y = v[007].y = v[010].y =
		v[014].y = v[015].y = v[016].y = v[022].y = v[023].y = v[024].y = vis->gz; // bottom

	v[003].y = v[004].y = v[005].y = v[011].y = v[012].y = v[013].y =
		v[017].y = v[020].y = v[021].y = v[025].y = v[026].y = v[027].y = vis->gzt; // top

	Surf.PolyColor = palette[R_GetBoundingBoxColor(vis->mobj)];

	HWR_ProcessPolygon(&Surf, v, 24, PF_Modulated|PF_NoTexture|PF_WireFrame, SHADER_NONE, false);
}

// -----------------+
// HWR_DrawSprite   : Draw flat sprites
//                  : (monsters, bonuses, weapons, lights, ...)
// Returns          :
// -----------------+
static void HWR_DrawSprite(gl_vissprite_t *spr)
{
	FOutVector wallVerts[4];
	patch_t *gpatch;
	GLPatch_t *hwrpatch;
	FSurfaceInfo Surf = {};
	const dboolean affine = ((spr->mobj && !P_MobjWasRemoved(spr->mobj)) && ((spr->mobj->player != NULL) || R_ThingIsAffineSprite(spr->mobj)) && (!R_ThingIsFloorSprite(spr->mobj)));
	const dboolean splat = R_ThingIsFloorSprite(spr->mobj);

	if (!spr->mobj || !spr->mobj->subsector)
		return;

	if (spr->mobj->subsector->sector->numlights
		&& (spr->mobj->renderflags & RF_ABSOLUTELIGHTLEVEL) == 0
		&& !splat && !affine)
	{
		HWR_SplitSprite(spr);
		return;
	}

	// cache sprite graphics
	//12/12/99: Hurdler:
	//          OK, I don't change anything for MD2 support because I want to be
	//          sure to do it the right way. So actually, we keep normal sprite
	//          in memory and we add the md2 model if it exists for that sprite

	gpatch = spr->gpatch;

	// create the sprite billboard
	//
	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	if (splat)
	{
		F2DCoord verts[4];
		F2DCoord rotated[4];

		angle_t angle;
		float ca, sa;
		float w, h;
		float xscale, yscale;
		float xoffset, yoffset;
		float leftoffset, topoffset;
		float scale = spr->scale;
		float zoffset = (P_MobjFlip(spr->mobj) * 0.05f);
		pslope_t *splatslope = NULL;
		int32_t i;

		renderflags_t renderflags = spr->renderflags;
		if (renderflags & RF_SHADOWEFFECTS)
			scale *= spr->shadowscale;

		if (spr->rotateflags & SRF_3D || renderflags & RF_NOSPLATBILLBOARD)
			angle = spr->angle;
		else
			angle = viewangle;

		if (!spr->rotated)
			angle += spr->mobj->rollangle;

		angle = -angle;
		angle += ANGLE_90;

		topoffset = spr->spriteyoffset;
		leftoffset = spr->spritexoffset;
		if (spr->flip)
			leftoffset = ((float)gpatch->width - leftoffset);

		xscale = spr->scale * spr->spritexscale;
		yscale = spr->scale * spr->spriteyscale;

		xoffset = leftoffset * xscale;
		yoffset = topoffset * yscale;

		w = (float)gpatch->width * xscale;
		h = (float)gpatch->height * yscale;

		// Set positions

		// 3--2
		// |  |
		// 0--1

		verts[3].x = -xoffset;
		verts[3].y = yoffset;

		verts[2].x = w - xoffset;
		verts[2].y = yoffset;

		verts[1].x = w - xoffset;
		verts[1].y = -h + yoffset;

		verts[0].x = -xoffset;
		verts[0].y = -h + yoffset;

		ca = FIXED_TO_FLOAT(FINECOSINE((-angle)>>ANGLETOFINESHIFT));
		sa = FIXED_TO_FLOAT(FINESINE((-angle)>>ANGLETOFINESHIFT));

		// Rotate
		for (i = 0; i < 4; i++)
		{
			rotated[i].x = (verts[i].x * ca) - (verts[i].y * sa);
			rotated[i].y = (verts[i].x * sa) + (verts[i].y * ca);
		}

		// Translate
		for (i = 0; i < 4; i++)
		{
			wallVerts[i].x = rotated[i].x + spr->x1;
			wallVerts[i].z = rotated[i].y + spr->z1;
		}

		if (renderflags & (RF_SLOPESPLAT | RF_OBJECTSLOPESPLAT))
		{
			pslope_t *standingslope = spr->mobj->standingslope; // The slope that the object is standing on.

			// The slope that was defined for the sprite.
			if (renderflags & RF_SLOPESPLAT)
				splatslope = spr->mobj->floorspriteslope;

			if (standingslope && (renderflags & RF_OBJECTSLOPESPLAT))
				splatslope = standingslope;
		}

		// Set vertical position
		if (splatslope)
		{
			for (i = 0; i < 4; i++)
			{
				fixed_t slopez = P_GetSlopeZAt(splatslope, FLOAT_TO_FIXED(wallVerts[i].x), FLOAT_TO_FIXED(wallVerts[i].z));
				wallVerts[i].y = FIXED_TO_FLOAT(slopez) + zoffset;
			}
		}
		else
		{
			for (i = 0; i < 4; i++)
				wallVerts[i].y = FIXED_TO_FLOAT(spr->gz) + zoffset;
		}
	}
	else if (affine)
	{
		// Affines are weird; we've generated the transformations for them,
		// so now we need to piece together each wall vertex.
		const float _cos = spr->affine.patchcos;
		const float _sin = spr->affine.patchsin;

		wallVerts[0].x = spr->affine.root.x + (spr->affine.p1.x * _cos);
		wallVerts[0].z = spr->affine.root.z + (spr->affine.p1.x * _sin);
		wallVerts[0].y = spr->affine.root.y + spr->affine.p1.y;

		wallVerts[1].x = spr->affine.root.x + (spr->affine.p2.x * _cos);
		wallVerts[1].z = spr->affine.root.z + (spr->affine.p2.x * _sin);
		wallVerts[1].y = spr->affine.root.y + spr->affine.p2.y;

		wallVerts[2].x = spr->affine.root.x + (spr->affine.p3.x * _cos);
		wallVerts[2].z = spr->affine.root.z + (spr->affine.p3.x * _sin);
		wallVerts[2].y = spr->affine.root.y + spr->affine.p3.y;

		wallVerts[3].x = spr->affine.root.x + (spr->affine.p4.x * _cos);
		wallVerts[3].z = spr->affine.root.z + (spr->affine.p4.x * _sin);
		wallVerts[3].y = spr->affine.root.y + spr->affine.p4.y;
	}
	else
	{
		// these were already scaled in HWR_ProjectSprite
		wallVerts[0].x = wallVerts[3].x = spr->x1;
		wallVerts[2].x = wallVerts[1].x = spr->x2;
		wallVerts[2].y = wallVerts[3].y = spr->gzt;
		wallVerts[0].y = wallVerts[1].y = spr->gz;

		// make a wall polygon (with 2 triangles), using the floor/ceiling heights,
		// and the 2d map coords of start/end vertices
		wallVerts[0].z = wallVerts[3].z = spr->z1;
		wallVerts[1].z = wallVerts[2].z = spr->z2;
	}

	// cache the patch in the graphics card memory
	//12/12/99: Hurdler: same comment as above (for md2)
	//Hurdler: 25/04/2000: now support colormap in hardware mode
	HWR_GetMappedPatch(gpatch, spr->colormap);

	hwrpatch = (GLPatch_t *)gpatch->hardware;

	if (spr->flip)
	{
		wallVerts[0].s = wallVerts[3].s = hwrpatch->max_s;
		wallVerts[2].s = wallVerts[1].s = 0;
	}
	else
	{
		wallVerts[0].s = wallVerts[3].s = 0;
		wallVerts[2].s = wallVerts[1].s = hwrpatch->max_s;
	}

	// flip the texture coords (look familiar?)
	if (spr->vflip)
	{
		wallVerts[3].t = wallVerts[2].t = hwrpatch->max_t;
		wallVerts[0].t = wallVerts[1].t = 0;
	}
	else
	{
		wallVerts[3].t = wallVerts[2].t = 0;
		wallVerts[0].t = wallVerts[1].t = hwrpatch->max_t;
	}

	float sprdist = 0.0f, distfact = 0.0f;
	size_t i;

	if (!splat /*&& !R_ThingIsPaperSprite(spr->mobj)*/ && !(spr->mobj->terrain && spr->mobj->terrain->floorClip))
	{
		// Let dispoffset work first since this adjust each vertex
		HWR_RotateSpritePolyToAim(spr, wallVerts, false);

		// push it toward the camera to mitigate floor-clipping sprites
		polyvertex_t pushpoint = {0};

		if (affine)
		{
			pushpoint.x = spr->affine.bounding_point.x;
			pushpoint.y = spr->affine.bounding_point.y;
			pushpoint.z = spr->affine.bounding_point.z;
		}
		else
		{
			pushpoint.x = spr->x1;
			pushpoint.y = spr->z1;
			pushpoint.z = spr->gzt;
		}

		float f_xdiff = (pushpoint.x - gl_viewx);
		float f_ydiff = (pushpoint.y - gl_viewy);
		float f_zdiff = (pushpoint.z - gl_viewz);

		sprdist = sqrtf((f_xdiff*f_xdiff) + (f_ydiff*f_ydiff) + (f_zdiff*f_zdiff));

		distfact = ((2.0f* spr->dispoffset) + 20.0f) / sprdist;
		for (i = 0; i < 4; i++)
		{
			wallVerts[i].x += (gl_viewx - wallVerts[i].x)*distfact;
			wallVerts[i].z += (gl_viewy - wallVerts[i].z)*distfact;
			wallVerts[i].y += (gl_viewz - wallVerts[i].y)*distfact;
		}
	}
	else if (R_ThingIsFloorSprite(spr->mobj))
	{
		sprdist = sqrtf((spr->x1 - gl_viewx)*(spr->x1 - gl_viewx) + (spr->z1 - gl_viewy)*(spr->z1 - gl_viewy));
		distfact = (2.0f + 20.0f) / sprdist;

		// pull splats out of the floor
		for (i = 0; i < 4; i++)
		{
			wallVerts[i].x += (gl_viewx - wallVerts[i].x)*distfact;
			wallVerts[i].z += (gl_viewy - wallVerts[i].z)*distfact;
			wallVerts[i].y += (gl_viewz - wallVerts[i].y)*distfact;
		}
	}

	// This needs to be AFTER the shadows so that the regular sprites aren't drawn completely black.
	// sprite lighting by modulating the RGB components
	/// \todo coloured

	// colormap test
	{
		sector_t *sector = spr->mobj->subsector->sector;
		int32_t lightlevel = 0;
		dboolean lightset = HWR_OverrideObjectLightLevel(spr->mobj, &lightlevel);
		extracolormap_t *colormap = NULL;

		if (!R_ThingIsFullBright(spr->mobj) && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
			colormap = sector->extra_colormap;

		if ((splat||affine) && sector->numlights)
		{
			// Affine splitting is going to require more advanced tricks
			// (GLSL fragment shader, or generating a "split" polygon on the fly).
			// I've already spent too much time on this damn thing, so I'm just doing
			// what precipitation (and, by extension, MD2) does.

			fixed_t _zoffs = 0;

			if (affine)
			{
				_zoffs = spr->mobj->height; // Always use the light at the top instead of whatever I was doing before
			}

			int32_t light = R_GetPlaneLight(sector, spr->mobj->z + _zoffs, false);

			if (!lightset)
				lightlevel = *sector->lightlist[light].lightlevel > 255 ? 255 : *sector->lightlist[light].lightlevel;

			if (!R_ThingIsFullBright(spr->mobj) && *sector->lightlist[light].extra_colormap && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
				colormap = *sector->lightlist[light].extra_colormap;
		}
		else if (!lightset)
			lightlevel = sector->lightlevel > 255 ? 255 : sector->lightlevel;

		if (!lightset)
			HWR_ObjectLightLevelPost(spr, sector, &lightlevel, false);

		HWR_Lighting(&Surf, lightlevel, colormap, P_SectorUsesDirectionalLighting(sector) && !R_ThingIsFullBright(spr->mobj));
	}

	{
		int32_t shader = SHADER_NONE;
		FBITFIELD blend = 0;
		FBITFIELD occlusion;
		dboolean use_linkdraw_hack = false;

		// if sprite has linkdraw, then dont write to z-buffer (by not using PF_Occlude)
		// this will result in sprites drawn afterwards to be drawn on top like intended when using linkdraw.
		if ((spr->mobj->flags2 & MF2_LINKDRAW) && spr->mobj->tracer)
			occlusion = 0;
		else
			occlusion = PF_Occlude;

		// Determine the blendmode and translucency value
		{
			uint32_t blendmode, trans;
			if (spr->mobj->renderflags & RF_BLENDMASK)
				blendmode = (spr->mobj->renderflags & RF_BLENDMASK) >> RF_BLENDSHIFT;
			else
				blendmode = (spr->mobj->frame & FF_BLENDMASK) >> FF_BLENDSHIFT;
			if (blendmode)
				blendmode++; // realign to constants

			if (spr->mobj->renderflags & RF_TRANSMASK)
				trans = (spr->mobj->renderflags & RF_TRANSMASK) >> RF_TRANSSHIFT;
			else
				trans = (spr->mobj->frame & FF_TRANSMASK) >> FF_TRANSSHIFT;
			if (trans >= NUMTRANSMAPS)
				return; // cap

			blend = HWR_SurfaceBlend(blendmode, trans, &Surf);

			// if sprite has PF_ALWAYSONTOP, draw on top of everything.
			if (cv_debugrender_spriteclip.value || spr->mobj->renderflags & RF_ALWAYSONTOP)
				blend |= PF_NoDepthTest;

			if (!trans && !blendmode)
			{
				// BP: i agree that is little better in environement but it don't
				//     work properly under glide nor with fogcolor to ffffff :(
				// Hurdler: PF_Environement would be cool, but we need to fix
				//          the issue with the fog before
				blend |= occlusion;
				if (!occlusion) use_linkdraw_hack = true;
			}
		}

		if (spr->renderflags & RF_SHADOWEFFECTS)
		{
			int32_t alpha = Surf.PolyColor.s.alpha;
			alpha -= ((int32_t)(spr->shadowheight / 4.0f)) + 75;
			if (alpha < 1)
				return;

			Surf.PolyColor.s.alpha = (uint8_t)(alpha);
			blend = PF_Translucent|occlusion;
			if (!occlusion) use_linkdraw_hack = true;
		}

		Surf.PolyColor.s.alpha = FixedMul(R_GetThingFade(spr->mobj), Surf.PolyColor.s.alpha);

		if (HWR_UseShader())
		{
			shader = SHADER_SPRITE;
			blend |= PF_ColorMapped;
		}

		HWR_ProcessPolygon(&Surf, wallVerts, 4, blend|PF_Modulated, shader, false);

		if (use_linkdraw_hack)
			HWR_LinkDrawHackAdd(wallVerts, spr);
	}
}

// Sprite drawer for precipitation
static inline void HWR_DrawPrecipitationSprite(gl_vissprite_t *spr)
{
	int32_t shader = SHADER_NONE;
	FBITFIELD blend = 0;
	FOutVector wallVerts[4];
	patch_t *gpatch;
	GLPatch_t *hwrpatch;
	FSurfaceInfo Surf = {};

	if (!spr->mobj || !spr->mobj->subsector)
		return;

	// cache sprite graphics
	gpatch = spr->gpatch;

	// cache the patch in the graphics card memory
	//12/12/99: Hurdler: same comment as above (for md2)
	//Hurdler: 25/04/2000: now support colormap in hardware mode
	HWR_GetMappedPatch(gpatch, spr->colormap);

	hwrpatch = (GLPatch_t *)gpatch->hardware;

	// create the sprite billboard
	//
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	wallVerts[0].x = wallVerts[3].x = spr->x1;
	wallVerts[2].x = wallVerts[1].x = spr->x2;
	wallVerts[2].y = wallVerts[3].y = spr->gzt;
	wallVerts[0].y = wallVerts[1].y = spr->gz;

	// make a wall polygon (with 2 triangles), using the floor/ceiling heights,
	// and the 2d map coords of start/end vertices
	wallVerts[0].z = wallVerts[3].z = spr->z1;
	wallVerts[1].z = wallVerts[2].z = spr->z2;

	// Let dispoffset work first since this adjust each vertex
	HWR_RotateSpritePolyToAim(spr, wallVerts, true);

	wallVerts[0].s = wallVerts[3].s = 0;
	wallVerts[2].s = wallVerts[1].s = hwrpatch->max_s;

	wallVerts[3].t = wallVerts[2].t = 0;
	wallVerts[0].t = wallVerts[1].t = hwrpatch->max_t;

	// colormap test
	{
		sector_t *sector = spr->mobj->subsector->sector;
		uint8_t lightlevel = 255;
		extracolormap_t *colormap = sector->extra_colormap;

		if (sector->numlights)
		{
			// Always use the light at the top instead of whatever I was doing before
			int32_t light = R_GetPlaneLight(sector, spr->mobj->z + spr->mobj->height, false);

			if (!R_ThingIsFullBright(spr->mobj))
				lightlevel = *sector->lightlist[light].lightlevel > 255 ? 255 : *sector->lightlist[light].lightlevel;

			if (!(spr->mobj->renderflags & RF_NOCOLORMAPS))
			{
				if (*sector->lightlist[light].extra_colormap)
				colormap = *sector->lightlist[light].extra_colormap;
			}
		}
		else
		{
			if (!R_ThingIsFullBright(spr->mobj))
				lightlevel = sector->lightlevel > 255 ? 255 : sector->lightlevel;

			if (!(spr->mobj->renderflags & RF_NOCOLORMAPS))
			{
				if (sector->extra_colormap)
					colormap = sector->extra_colormap;
			}
		}

		//lightlevel = 128 + (lightlevel>>1);

		HWR_Lighting(&Surf, lightlevel, colormap, P_SectorUsesDirectionalLighting(sector));
	}

	// Determine the blendmode and translucency value
	{
		uint32_t blendmode, trans;
		if (spr->mobj->renderflags & RF_BLENDMASK)
			blendmode = (spr->mobj->renderflags & RF_BLENDMASK) >> RF_BLENDSHIFT;
		else
			blendmode = (spr->mobj->frame & FF_BLENDMASK) >> FF_BLENDSHIFT;
		if (blendmode)
			blendmode++; // realign to constants

		if (spr->mobj->renderflags & RF_TRANSMASK)
			trans = (spr->mobj->renderflags & RF_TRANSMASK) >> RF_TRANSSHIFT;
		else
			trans = (spr->mobj->frame & FF_TRANSMASK) >> FF_TRANSSHIFT;
		if (trans >= NUMTRANSMAPS)
			return; // cap

		blend = HWR_SurfaceBlend(blendmode, trans, &Surf);
		if (!trans && !blendmode)
		{
			// BP: i agree that is little better in environement but it don't
			//     work properly under glide nor with fogcolor to ffffff :(
			// Hurdler: PF_Environement would be cool, but we need to fix
			//          the issue with the fog before
			blend |= PF_Occlude;
		}
	}

	if (HWR_UseShader())
	{
		shader = SHADER_SPRITE;
		blend |= PF_ColorMapped;
	}

	HWR_ProcessPolygon(&Surf, wallVerts, 4, blend|PF_Modulated, shader, false);
}

// --------------------------------------------------------------------------
// Sort vissprites by distance
// --------------------------------------------------------------------------
gl_vissprite_t* gl_vsprorder[MAXVISSPRITES];

// Note: For more correct transparency the transparent sprites would need to be
// sorted and drawn together with transparent surfaces.
static int CompareVisSprites(const void *p1, const void *p2)
{
	gl_vissprite_t* spr1 = *(gl_vissprite_t*const*)p1;
	gl_vissprite_t* spr2 = *(gl_vissprite_t*const*)p2;
	int idiff;
	float fdiff;
	float tz1, tz2;

	// Make transparent sprites last. Comment from the previous sort implementation:
	// Sryder:	Oh boy, while it's nice having ALL the sprites sorted properly, it fails when we bring MD2's into the
	//			mix and they want to be translucent. So let's place all the translucent sprites and MD2's AFTER
	//			everything else, but still ordered of course, the depth buffer can handle the opaque ones plenty fine.
	//			We just need to move all translucent ones to the end in order
	// TODO:	Fully sort all sprites and MD2s with walls and floors, this part will be unnecessary after that
	int transparency1;
	int transparency2;

	int renderflags1;
	int renderflags2;

	int frame1;
	int frame2;

	int linkdraw1;
	int linkdraw2;

	// bbox doesn't need to be sorted
	if (spr1->bbox || spr2->bbox)
		return 0;

	// check for precip first, because then sprX->mobj is actually a precipmobj_t and does not have flags2 or tracer
	linkdraw1 = !spr1->precip && (spr1->mobj->flags2 & MF2_LINKDRAW) && spr1->mobj->tracer;
	linkdraw2 = !spr2->precip && (spr2->mobj->flags2 & MF2_LINKDRAW) && spr2->mobj->tracer;

	// ^ is the XOR operation
	// if comparing a linkdraw and non-linkdraw sprite or 2 linkdraw sprites with different tracers, then use
	// the tracer's properties instead of the main sprite's.
	if ((linkdraw1 && linkdraw2 && spr1->mobj->tracer != spr2->mobj->tracer) || (linkdraw1 ^ linkdraw2))
	{
		if (linkdraw1)
		{
			tz1 = spr1->tracertz;
			renderflags1 = spr1->mobj->tracer->renderflags;
			frame1 = spr1->mobj->tracer->frame;
		}
		else
		{
			tz1 = spr1->tz;
			renderflags1 = spr1->mobj->renderflags;
			frame1 = spr1->mobj->frame;
		}
		if (linkdraw2)
		{
			tz2 = spr2->tracertz;
			renderflags2 = spr2->mobj->tracer->renderflags;
			frame2 = spr2->mobj->tracer->frame;
		}
		else
		{
			tz2 = spr2->tz;
			renderflags2 = spr2->mobj->renderflags;
			frame2 = spr2->mobj->frame;
		}
	}
	else
	{
		tz1 = spr1->tz;
		renderflags1 = (spr1->precip ? 0 : spr1->mobj->renderflags);
		frame1 = spr1->mobj->frame;
		tz2 = spr2->tz;
		renderflags2 = (spr2->precip ? 0 : spr2->mobj->renderflags);
		frame2 = spr2->mobj->frame;
	}

	// first compare transparency flags, then compare tz, then compare dispoffset

	transparency1 = (renderflags1 & RF_TRANSMASK) ?
		((renderflags1 & RF_TRANSMASK)>>RF_TRANSSHIFT) :
		((frame1 & FF_TRANSMASK)>>FF_TRANSSHIFT);

	transparency2 = (renderflags2 & RF_TRANSMASK) ?
		((renderflags2 & RF_TRANSMASK)>>RF_TRANSSHIFT) :
		((frame2 & FF_TRANSMASK)>>FF_TRANSSHIFT);

	idiff = transparency1 - transparency2;
	if (idiff != 0) return idiff;

	fdiff = tz2 - tz1; // this order seems correct when checking with apitrace. Back to front.
	if (fabsf(fdiff) < 1.0E-36f)
		return spr1->dispoffset - spr2->dispoffset; // smallest dispoffset first if sprites are at (almost) same location.
	else if (fdiff > 0)
		return 1;
	else
		return -1;
}

uint32_t HWR_SortVisSprites(void)
{
	uint32_t i;
	for (i = 0; i < cst->gl_visspritecount; i++)
	{
		gl_vsprorder[i] = HWR_GetVisSprite(i);
	}
	qs22j(gl_vsprorder, cst->gl_visspritecount, sizeof(gl_vissprite_t*), CompareVisSprites);
	return cst->gl_visspritecount;
}

// --------------------------------------------------------------------------
//  Draw all vissprites
// --------------------------------------------------------------------------

// added the stransform so they can be switched as drawing happenes so MD2s and sprites are sorted correctly with each other
void HWR_DrawSprites(void)
{
	uint32_t i;
	dboolean skipshadow = false; // skip shadow if it was drawn already for a linkdraw sprite encountered earlier in the list

#ifdef BAD_MODEL_OPTIONS
	GL_SetSpecialState(HWD_SET_MODEL_LIGHTING, cv_glmodellighting.value);
#else
	GL_SetSpecialState(HWD_SET_MODEL_LIGHTING, 1);
#endif

	for (i = 0; i < cst->gl_visspritecount; i++)
	{
		gl_vissprite_t *spr = gl_vsprorder[i];
		if (spr->bbox)
			HWR_DrawBoundingBox(spr);
		else
		if (spr->precip)
			HWR_DrawPrecipitationSprite(spr);
		else
		{
			if (spr->mobj && spr->mobj->shadowscale && cv_shadow.value && !skipshadow)
			{
				HWR_DrawDropShadow(spr->mobj, spr->mobj->shadowscale);
			}

			if ((spr->mobj->flags2 & MF2_LINKDRAW) && spr->mobj->tracer)
			{
				// If this linkdraw sprite is behind a sprite that has a shadow,
				// then that shadow has to be drawn first, otherwise the shadow ends up on top of
				// the linkdraw sprite because the linkdraw sprite does not modify the z-buffer.
				// The !skipshadow check is there in case there are multiple linkdraw sprites connected
				// to the same tracer, so the tracer's shadow only gets drawn once.
				if (cv_shadow.value && !skipshadow && spr->dispoffset < 0 && spr->mobj->tracer->shadowscale)
				{
					HWR_DrawDropShadow(spr->mobj->tracer, spr->mobj->tracer->shadowscale);
					skipshadow = true;
					// The next sprite in this loop should be either another linkdraw sprite or the tracer.
					// When the tracer is inevitably encountered, skipshadow will cause it's shadow
					// to get skipped and skipshadow will get set to false by the 'else' clause below.
				}
			}
			else
			{
				skipshadow = false;
			}

			if (spr->mobj && spr->mobj->skin && spr->mobj->sprite == SPR_PLAY)
			{
				if (LIKELY(!cv_glmodels.value || md2_playermodels[((skin_t*)spr->mobj->skin)->skinnum].notfound || md2_playermodels[((skin_t*)spr->mobj->skin)->skinnum].scale < 0.0f))
					HWR_DrawSprite(spr);
				else
				{
					if (!HWR_DrawModel(spr))
						HWR_DrawSprite(spr);
				}
			}
			else
			{
				if (LIKELY(!cv_glmodels.value || md2_models[spr->mobj->sprite].notfound || md2_models[spr->mobj->sprite].scale < 0.0f))
					HWR_DrawSprite(spr);
				else
				{
					if (!HWR_DrawModel(spr))
						HWR_DrawSprite(spr);
				}
			}
		}
	}
	GL_SetSpecialState(HWD_SET_MODEL_LIGHTING, 0);

	// At the end of sprite drawing, draw shapes of linkdraw sprites to z-buffer, so they
	// don't get drawn over by transparent surfaces.
	HWR_LinkDrawHackFinish();
	// Work around a r_opengl.c bug with PF_Invisible by making this SetBlend call
	// where PF_Invisible is off and PF_Masked is on.
	// (Other states probably don't matter. Here I left them same as in LinkDrawHackFinish)
	// Without this workaround the rest of the draw calls in this frame (including UI, screen texture)
	// can get drawn using an incorrect glBlendFunc, resulting in a occasional black screen.
	GL_SetBlend(PF_Translucent|PF_Occlude|PF_Masked);
}

// --------------------------------------------------------------------------
// HWR_AddSprites
// During BSP traversal, this adds sprites by sector.
// --------------------------------------------------------------------------
static uint8_t sectorlight;
void HWR_AddSprites(sector_t *sec)
{
	mobj_t *thing;
	fixed_t limit_dist;

	// BSP is traversed by subsector.
	// A sector might have been split into several
	//  subsectors during BSP building.
	// Thus we check whether its already added.
	if (sec->validcount == validcount)
		return;

	// Well, now it will be done.
	sec->validcount = validcount;

	// sprite lighting
	sectorlight = sec->lightlevel & 0xff;

	// Handle all things in sector.
	// If a limit exists, handle things a tiny bit different.
	limit_dist = (fixed_t)(cv_drawdist.value) * mapobjectscale;
	for (thing = sec->thinglist; thing; thing = thing->snext)
	{
		if (R_ThingWithinDist(thing, limit_dist))
		{
			if (R_ThingVisible(thing))
			{
				HWR_ProjectSprite(thing);
			}

			HWR_ProjectBoundingBox(thing);
		}
	}
}

// --------------------------------------------------------------------------
// HWR_AddPrecipitationSprites
// This renders through the blockmap instead of BSP to avoid
// iterating a huge amount of precipitation sprites in sectors
// that are beyond drawdist.
// --------------------------------------------------------------------------
void HWR_AddPrecipitationSprites(void)
{
	const fixed_t drawdist = cv_drawdist_precip.value * mapobjectscale;

	int32_t xl, xh, yl, yh, bx, by;
	blocknode_t *block, *next;
	precipmobj_t *th;

	// no, no infinite draw distance for precipitation. this option at zero is supposed to turn it off
	if (drawdist == 0)
	{
		return;
	}

	// No weather to draw so save some time by skipping blockmap iterations
	if (curWeather == PRECIP_NONE || curWeather == PRECIP_STORM_NORAIN)
	{
		return;
	}

	R_GetRenderBlockMapDimensions(drawdist, &xl, &xh, &yl, &yh);

	for (bx = xl; bx <= xh; bx++)
	{
		for (by = yl; by <= yh; by++)
		{
			for (block = precipblocklinks[(by * bmapwidth) + bx]; block; block = next)
			{
				next = block->bnext;
				th = block->mobj.precipmobj;

				if (R_PrecipThingVisible(th))
				{
					HWR_ProjectPrecipitationSprite(th);
				}
			}
		}
	}
}

// --------------------------------------------------------------------------
// HWR_ProjectSprite
//  Generates a vissprite for a thing if it might be visible.
// --------------------------------------------------------------------------
// BP why not use xtoviexangle/viewangletox like in bsp ?....
static void HWR_ProjectSprite(mobj_t *thing)
{
	gl_vissprite_t *vis;
	float tr_x, tr_y;
	float tz;
	float tracertz = 0.0f;
	float x1, x2;
	float rightsin, rightcos;
	float this_scale, this_xscale, this_yscale;
	fixed_t highresscale;
	float spritexscale, spriteyscale;
	float shadowheight = 1.0f, shadowscale = 1.0f;
	float gz, gzt;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
#ifdef ROTSPRITE
	spriteinfo_t *sprinfo;
#endif
	md2_t *md2;
	size_t lumpoff;
	unsigned rot;
	uint16_t flip;
	dboolean vflip = (!(thing->eflags & MFE_VERTICALFLIP) != !R_ThingVerticallyFlipped(thing));
	dboolean mirrored = thing->mirrored;
	dboolean hflip = (!R_ThingHorizontallyFlipped(thing) != !mirrored);
	int32_t dispoffset;

	angle_t ang;
	int32_t heightsec, phs;
	const dboolean splat = R_ThingIsFloorSprite(thing);
	const dboolean papersprite = (R_ThingIsPaperSprite(thing) && !splat);
	float z1, z2;

	fixed_t spr_width, spr_height;
	fixed_t spr_offset, spr_topoffset;
#ifdef ROTSPRITE
	patch_t *rotsprite = NULL;
	int32_t rollangle = 0;
	angle_t spriterotangle = 0;
	vector2_t visoffs, rotoffset;
#endif

	// Affines
	dboolean affinesprite = (((thing->player != NULL) || R_ThingIsAffineSprite(thing)) && (!splat));
	affine_t affine_transform = {0};
	vector2_t affine_scale = {0};
	f_vector2_t affine_pivotoffsetdiff = {0};

	polyvertex_t affine_rootpoint = {0};
	f_vector2_t affine_point[4] = {0};
	float leftbound = 0, backbound = 0, topbound = 0;

	// uncapped/interpolation
	interpmobjstate_t interp = {0};
	mobj_t *interptarg;

	if (G_GamestateUsesLevel() == false)
		return;

	if (!thing)
		return;

	interptarg = thing;

	if (R_IsOverlayingSMonitorPlayer(thing))
	{
		// Kill overlay misalignment
		interptarg = thing->target;
	}

	R_InterpolateMobjState(interptarg, R_GetTimeFrac(RTF_LEVEL), &interp);

	dispoffset = thing->dispoffset;

	// sprite offset
	interp.x += thing->sprxoff;
	interp.y += thing->spryoff;
	interp.z += thing->sprzoff;

	if (thing->player)
	{
		interp.z += thing->player->crash_zoffset;
	}

	if (interp.spritexscale < 1 || interp.spriteyscale < 1)
		return;

	this_scale = FIXED_TO_FLOAT(interp.scale);
	spritexscale = FIXED_TO_FLOAT(interp.spritexscale);
	spriteyscale = FIXED_TO_FLOAT(interp.spriteyscale);

	// transform the origin point
	tr_x = FIXED_TO_FLOAT(interp.x) - gl_viewx;
	tr_y = FIXED_TO_FLOAT(interp.y) - gl_viewy;

	// rotation around vertical axis
	tz = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

	// thing is behind view plane?
	if (tz < ZCLIP_PLANE && !(papersprite || splat))
	{
		if (cv_glmodels.value) //Yellow: Only MD2's dont disappear
		{
			if (thing->skin && thing->sprite == SPR_PLAY)
				md2 = &md2_playermodels[( (skin_t *)thing->skin)->skinnum];
			else
				md2 = &md2_models[thing->sprite];

			if (md2->notfound || md2->scale < 0.0f)
				return;
		}
		else
			return;
	}

	// The above can stay as it works for cutting sprites that are too close
	tr_x = FIXED_TO_FLOAT(interp.x);
	tr_y = FIXED_TO_FLOAT(interp.y);

	// decide which patch to use for sprite relative to player
#ifdef RANGECHECK
	if ((unsigned)thing->sprite >= numsprites)
		I_Error("HWR_ProjectSprite: invalid sprite number %i ", thing->sprite);
#endif

	rot = thing->frame&FF_FRAMEMASK;

	//Fab : 02-08-98: 'skin' override spritedef currently used for skin
	if (thing->skin && thing->sprite == SPR_PLAY)
	{
		sprdef = &((skin_t *)thing->skin)->sprites[thing->sprite2];
#ifdef ROTSPRITE
		sprinfo = &((skin_t *)thing->skin)->sprinfo[thing->sprite2];
#endif
	}
	else if (thing->sprite == SPR_ITEM)
	{
		int32_t item = thing->threshold > 0 && thing->threshold < numkartitems ? thing->threshold : 0;
		dboolean alt = item > 0 && K_IsKartItemAlternate(item);

		sprdef = alt ? &kartitems[item].altspritedef : &kartitems[item].spritedef;
#ifdef ROTSPRITE
		sprinfo = &spriteinfo[SPR_ITEM];
#endif
	}
	else
	{
		sprdef = &sprites[thing->sprite];
#ifdef ROTSPRITE
		sprinfo = &spriteinfo[thing->sprite];
#endif
	}

	if (rot >= sprdef->numframes)
	{
		CONS_Alert(CONS_ERROR, M_GetText("HWR_ProjectSprite: invalid sprite frame %s/%s for %s\n"),
			sizeu1(rot), sizeu2(sprdef->numframes), sprnames[thing->sprite]);
		thing->sprite = states[S_UNKNOWN].sprite;
		thing->frame = states[S_UNKNOWN].frame;
		sprdef = &sprites[thing->sprite];
#ifdef ROTSPRITE
		sprinfo = &spriteinfo[thing->sprite];
#endif
		rot = thing->frame&FF_FRAMEMASK;
		thing->state->sprite = thing->sprite;
		thing->state->frame = thing->frame;
	}

	sprframe = &sprdef->spriteframes[rot];

#ifdef PARANOIA
	if (!sprframe)
		I_Error("sprframes NULL for sprite %d\n", thing->sprite);
#endif

	ang = R_PointToAngle (interp.x, interp.y) - interp.angle;
	if (mirrored)
		ang = InvAngle(ang);

	if (sprframe->rotate == SRF_SINGLE)
	{
		// use single rotation for all views
		rot = 0;                        //Fab: for vis->patch below
		lumpoff = sprframe->lumpid[0];     //Fab: see note above
		flip = sprframe->flip; // Will only be 0x00 or 0xFF

		if (papersprite && ang < ANGLE_180)
			flip ^= 0xFFFF;
	}
	else
	{
		// choose a different rotation based on player view
		if ((sprframe->rotate & SRF_RIGHT) && (ang < ANGLE_180)) // See from right
			rot = 6; // F7 slot
		else if ((sprframe->rotate & SRF_LEFT) && (ang >= ANGLE_180)) // See from left
			rot = 2; // F3 slot
		else if (sprframe->rotate & SRF_3DGE) // 16-angle mode
		{
			rot = (ang+ANGLE_180+ANGLE_11hh)>>28;
			rot = ((rot & 1)<<3)|(rot>>1);
		}
		else // Normal behaviour
			rot = (ang+ANGLE_202h)>>29;

		//Fab: lumpid is the index for spritewidth,spriteoffset... tables
		lumpoff = sprframe->lumpid[rot];
		flip = sprframe->flip & (1<<rot);

		if (papersprite && ang < ANGLE_180)
			flip ^= (1<<rot);
	}

	if (thing->skin && ((skin_t *)thing->skin)->highresscale != FRACUNIT)
	{
		float hi_res = ((skin_t *)thing->skin)->highresscale;
		this_scale *= FIXED_TO_FLOAT(hi_res);
		highresscale = hi_res;
	}
	else
	{
		highresscale = FRACUNIT;
	}

	spr_width = spritecachedinfo[lumpoff].width;
	spr_height = spritecachedinfo[lumpoff].height;
	spr_offset = spritecachedinfo[lumpoff].offset;
	spr_topoffset = spritecachedinfo[lumpoff].topoffset;

	float base_topoffs = FIXED_TO_FLOAT(spr_topoffset);

#ifdef ROTSPRITE
	spriterotangle = R_SpriteRotationAngle(thing, NULL, &interp, (affinesprite && vflip));

	if (spriterotangle != 0
	&& !(splat && !(thing->renderflags & RF_NOSPLATROLLANGLE))
	&& (!affinesprite)) // Affines are capable of rotation; this is redundant
	{
		rollangle = R_GetRollAngle(papersprite == vflip
				? spriterotangle : InvAngle(spriterotangle));
		rotsprite = Patch_GetRotatedSprite(sprframe, (thing->frame & FF_FRAMEMASK), rot, flip, false, (thing->renderflags2 & RF2_PIVOTATOFFSETS), sprinfo, rollangle);

		if (rotsprite != NULL)
		{
			spr_width = rotsprite->width << FRACBITS;
			spr_height = rotsprite->height << FRACBITS;
			spr_offset = rotsprite->leftoffset << FRACBITS;
			spr_topoffset = rotsprite->topoffset << FRACBITS;
			spr_topoffset += FEETADJUST;

			// flip -> rotate, not rotate -> flip
			flip = 0;
		}
	}

	// initialize and rotate pitch/roll vectors
	visoffs.x = 0;
	visoffs.y = 0;
	rotoffset.x = 0;
	rotoffset.y = 0;

	const fixed_t visoffymul = (vflip ? -FRACUNIT : FRACUNIT);

	if (R_ThingIsUsingBakedOffsets(interptarg))
	{
		R_RotateSpriteOffsetsByPitchRoll(interptarg,
										 vflip,
										 hflip,
										 false,
										 &interp,
										 &visoffs,
										 &rotoffset);

		rotoffset.x *= FRACUNIT;
	}
#endif

	if (affinesprite)
	{
		vector2_t affine_pivot = {0};

		vector2_t patch_defaultpivot = {0};
		if (interptarg->renderflags2 & RF2_PIVOTATOFFSETS)
		{
			patch_defaultpivot.x = spr_offset;
			patch_defaultpivot.y = spr_topoffset;

			if (interptarg->player)
			{
				if (!(wadfiles[((skin_t *)interptarg->skin)->wadnum]->compatmode))
				{
					// RR skins actually pivot themselves 4 pixels above their offset
					// Ideally, any modifications to that would supply their own SPRTINFO, so...
					patch_defaultpivot.y -= (4 * FRACUNIT);
				}
			}
		}
		else
		{
			patch_defaultpivot.x = (spr_width / 2);
			patch_defaultpivot.y = (spr_height / 2);
		}

		R_GetPivotVectorFromSpriteInfo(&affine_pivot,
									   &patch_defaultpivot,
									   sprinfo,
									   (thing->frame & FF_FRAMEMASK));

		fixed_t use_xoffset = spr_offset;

		if (flip)
		{
			use_xoffset = spr_width - spr_offset;
		}

		if (thing->renderflags & RF_ABSOLUTEOFFSETS)
		{
			use_xoffset = FixedDiv(interp.spritexoffset,highresscale);
		}
		else
		{
			int8_t flipoffset = 1;
			if ((thing->renderflags & RF_FLIPOFFSETS) && flip)
				flipoffset = -1;
			use_xoffset += (FixedDiv(interp.spritexoffset,highresscale) * flipoffset);
		}

		affine_pivotoffsetdiff.x = FIXED_TO_FLOAT(affine_pivot.x - use_xoffset);
		affine_pivotoffsetdiff.y = FIXED_TO_FLOAT(affine_pivot.y - spr_topoffset);

		affine_scale.x = FLOAT_TO_FIXED(spritexscale * this_scale);
		affine_scale.y = FLOAT_TO_FIXED(spriteyscale * this_scale);

		angle_t angle;

		int32_t flipsign = ((flip && papersprite) ? -1 : 1); // Flip OGL affine papersprites for Software parity

		angle = R_ConvToRollAngle(spriterotangle) * flipsign;

		const dboolean renderflip = ((thing->renderflags & RF_FLIPOFFSETS) == RF_FLIPOFFSETS);
		const fixed_t rolloffs_x = FixedDiv(interptarg->rollingxoffset * FRACUNIT, highresscale) * (((!renderflip) && flip) ? -1 : 1);
		const fixed_t rolloffs_y = FixedDiv(interptarg->rollingyoffset * FRACUNIT, highresscale) * (((!renderflip) && vflip) ? -1 : 1);
		fixed_t y_piv = affine_pivot.y;

		if (vflip)
		{
			// Flip the upper offset, and use *that* as the pivot
			y_piv = spr_height - y_piv;
		}

		fixed_t sa = FSIN(angle), ca = FCOS(angle);

		if (R_AffinePreScale(interptarg))
		{
			affine_transform.a = FixedDiv(ca, affine_scale.x);
			affine_transform.b = FixedDiv(-sa, affine_scale.x);
			affine_transform.c = FixedDiv(sa, affine_scale.y);
			affine_transform.d = FixedDiv(ca, affine_scale.y);
		}
		else
		{
			// Simulate shitty SRB2 "scale after rotate" nonsense
			affine_transform.a = FixedDiv(ca, affine_scale.x);
			affine_transform.b = FixedDiv(-sa, affine_scale.y);
			affine_transform.c = FixedDiv(sa, affine_scale.x);
			affine_transform.d = FixedDiv(ca, affine_scale.y);
		}

		affine_transform.ox = affine_pivot.x - rolloffs_x;
		affine_transform.oy = y_piv - rolloffs_y;

		spritexscale = 1.0;
		spriteyscale = 1.0;
	}

	if (thing->renderflags & RF_ABSOLUTEOFFSETS)
	{
		spr_offset = FixedDiv(interp.spritexoffset,highresscale);
#ifdef ROTSPRITE
		spr_topoffset = (FixedDiv(interp.spriteyoffset,highresscale) + FixedDiv((visoffs.y * visoffymul), mapobjectscale) + (rotoffset.y * visoffymul));
#else
		spr_topoffset = FixedDiv(interp.spriteyoffset,highresscale);
#endif
	}
	else
	{
		int8_t flipoffset = 1;

		if ((thing->renderflags & RF_FLIPOFFSETS) && flip)
			flipoffset = -1;

		spr_offset += FixedDiv(interp.spritexoffset,highresscale) * flipoffset;
#ifdef ROTSPRITE
		spr_topoffset += (FixedDiv(interp.spriteyoffset,highresscale) + FixedDiv((visoffs.y * visoffymul),
		mapobjectscale) + (rotoffset.y * visoffymul)) * flipoffset;
#else
		spr_topoffset += FixedDiv(interp.spriteyoffset,highresscale) * flipoffset;
#endif
	}

	if (papersprite)
	{
		rightsin = FIXED_TO_FLOAT(FINESINE(interp.angle >> ANGLETOFINESHIFT));
		rightcos = FIXED_TO_FLOAT(FINECOSINE(interp.angle >> ANGLETOFINESHIFT));
	}
	else
	{
		rightsin = FIXED_TO_FLOAT(FINESINE((viewangle + ANGLE_90)>>ANGLETOFINESHIFT));
		rightcos = FIXED_TO_FLOAT(FINECOSINE((viewangle + ANGLE_90)>>ANGLETOFINESHIFT));
	}

	flip = !flip != !hflip;

	if (thing->renderflags & RF_SHADOWEFFECTS)
	{
		mobj_t *caster = thing->target;

		if (caster && !P_MobjWasRemoved(caster))
		{
			interpmobjstate_t casterinterp = {0};
			fixed_t groundz;
			fixed_t floordiff;

			R_InterpolateMobjState(caster, R_GetTimeFrac(RTF_LEVEL), &casterinterp);

			groundz = R_GetShadowZ(thing, NULL);
			floordiff = abs(((thing->eflags & MFE_VERTICALFLIP) ? casterinterp.height : 0) + casterinterp.z - groundz);

			shadowheight = FIXED_TO_FLOAT(floordiff);
			shadowscale = FIXED_TO_FLOAT(FixedMul(FRACUNIT - floordiff/640, casterinterp.scale));

			if (splat)
				spritexscale *= shadowscale;
			spriteyscale *= shadowscale;
		}
	}

	this_xscale = spritexscale * this_scale;
	this_yscale = spriteyscale * this_scale;

	if (splat)
	{
		z1 = z2 = tr_y;
		x1 = x2 = tr_x;
		gz = gzt = interp.z;
	}
	else
	{
#ifdef ROTSPRITE
		if (visoffs.x)
		{
			visoffs.x = (FixedDiv((visoffs.x * FRACUNIT), mapobjectscale));
		}
#endif
		if (affinesprite)
		{
			x1 = x2 = z1 = z2 = 0.0f;

			float f_offs, f_width, f_height;

			f_offs = FIXED_TO_FLOAT(spr_offset);
			f_width = FIXED_TO_FLOAT(spr_width);
			f_height = FIXED_TO_FLOAT(spr_height);

			// From HWR_DrawAffinePatch

			float fa = FIXED_TO_FLOAT(affine_transform.a);
			float fd = FIXED_TO_FLOAT(affine_transform.d);
			float fc = FIXED_TO_FLOAT(affine_transform.c);
			float fb = FIXED_TO_FLOAT(affine_transform.b);
			float fx = FIXED_TO_FLOAT(affine_transform.ox);
			float fy = FIXED_TO_FLOAT(affine_transform.oy);

			// now, the matrix passed to this function maps screen coordinates to texel coordinates...
			// but to translate this from software to GL, we have to figure out where each corner
			// (or vertex) of the patch should end up on the screen.
			// which means we have to map texel coordinates to screen coordinates.
			// which means we have to invert the matrix.
			// how do you invert a matrix?
			// ...
			// i don't fucking know, i spent a day on this and got absolutely nowhere, but this guy knows:
			// https://nigeltao.github.io/blog/2021/inverting-3x2-affine-transformation-matrix.html
			float determinant = fa * fd - fb * fc;
			if (determinant > 0.0f)
			{
				float ba = fd / determinant;
				float bb = -fb / determinant;
				float bc = -fc / determinant;
				float bd = fa / determinant;

				// set the polygon vertices to the right positions
				//  3--2
				//  | /|
				//  |/ |
				//  0--1
				affine_point[3].x = (ba * -fx + bb * -fy + fx);
				affine_point[3].y = (bc * -fx + bd * -fy + fy);

				affine_point[2].x = ba * (f_width - fx) + bb * -fy + fx;
				affine_point[2].y = bc * (f_width - fx) + bd * -fy + fy;

				affine_point[0].x = ba * -fx + bb * (f_height - fy) + fx;
				affine_point[0].y = bc * -fx + bd * (f_height - fy) + fy;

				affine_point[1].x = ba * (f_width - fx) + bb * (f_height - fy) + fx;
				affine_point[1].y = bc * (f_width - fx) + bd * (f_height - fy) + fy;

				// Focus each point on the center.
				affine_point[0].x = fx - affine_point[0].x;
				affine_point[1].x = fx - affine_point[1].x;
				affine_point[2].x = fx - affine_point[2].x;
				affine_point[3].x = fx - affine_point[3].x;

				affine_point[1].y -= fy;
				affine_point[2].y -= fy;
				affine_point[0].y -= fy;
				affine_point[3].y -= fy;

				// ...okay, now comb through all four vertices and set the output bounds based on this.
				// "Why not a loop?" According to SM64 programming wizard Kaze Emanuar,
				// loops take more processing time. If we can help it, it's better to just cut corners.
				float f_left = 0;
				float f_bottom = 0;
				float f_right = 0;
				float f_top = 0;
				float f_xlen = 0;
				float f_ylen = 0;
#define BOUNDCHECK(i)                                     \
	{                                                 \
		f_left = min(affine_point[i].x, f_left); \
		f_bottom =  max(affine_point[i].y, f_bottom); \
		f_top =  min(affine_point[i].y, f_top); \
		f_right =  max(affine_point[i].x, f_right); \
		f_xlen = (f_right - f_left); \
		f_ylen = (f_bottom - f_top); \
	}

				BOUNDCHECK(0);
				BOUNDCHECK(1);
				BOUNDCHECK(2);
				BOUNDCHECK(3);

#undef BOUNDCHECK

				backbound = (f_left * rightsin);
				leftbound = (f_left * rightcos);
				topbound = f_bottom * -1;

				// Invert the Y values.
				affine_point[0].y *= -1.0f;
				affine_point[1].y *= -1.0f;
				affine_point[2].y *= -1.0f;
				affine_point[3].y *= -1.0f;

				affine_rootpoint.x = tr_x;
				affine_rootpoint.z = tr_y;

				affine_pivotoffsetdiff.x *= FIXED_TO_FLOAT(affine_scale.x);
				affine_pivotoffsetdiff.y *= FIXED_TO_FLOAT(affine_scale.y);

				affine_rootpoint.x -= (affine_pivotoffsetdiff.x * rightcos);
				affine_rootpoint.z -= (affine_pivotoffsetdiff.x * rightsin);

#ifdef ROTSPRITE
				const float xrescale = this_xscale / FIXED_TO_FLOAT(mapobjectscale);
				float f_visx = (FIXED_TO_FLOAT(FixedMul(visoffs.x, mapobjectscale))) * xrescale;
				if (flip)
				{
					spr_offset -= visoffs.x;
					affine_rootpoint.x -= (f_visx * rightcos);
					affine_rootpoint.z -= (f_visx * rightsin);
				}
				else
				{
					spr_offset += visoffs.x;
					affine_rootpoint.x += (f_visx * rightcos);
					affine_rootpoint.z += (f_visx * rightsin);
				}
#endif
			}
			else
			{
				goto nodeterminant;
			}

		}
		else
		{
nodeterminant:
			if (flip)
			{
#ifdef ROTSPRITE
				spr_offset -= visoffs.x;
				spr_offset -= rotoffset.x;
#endif
				x1 = (FIXED_TO_FLOAT((spr_width - spr_offset)) * this_xscale);
				x2 = (FIXED_TO_FLOAT(spr_offset) * this_xscale);
			}
			else
			{
#ifdef ROTSPRITE
				spr_offset += visoffs.x;
				spr_offset += rotoffset.x;
#endif
				x1 = (FIXED_TO_FLOAT(spr_offset) * this_xscale);
				x2 = (FIXED_TO_FLOAT(spr_width - spr_offset) * this_xscale);
			}

			z1 = tr_y + x1 * rightsin;
			z2 = tr_y - x2 * rightsin;
			x1 = tr_x + x1 * rightcos;
			x2 = tr_x - x2 * rightcos;
		}
		// test if too close
	/*
		if (papersprite)
		{
			z1 = tz - x1 * angle_scalez;
			z2 = tz + x2 * angle_scalez;

			if (max(z1, z2) < ZCLIP_PLANE)
				return;
		}
	*/


		if (thing->terrain && thing->terrain->floorClip)
			spr_topoffset -= thing->terrain->floorClip;

		affine_rootpoint.y = 0;

		const float f_topoffs = (FIXED_TO_FLOAT(spr_topoffset));

		if (vflip)
		{
			affine_rootpoint.y = (FIXED_TO_FLOAT(interp.z + thing->height) + affine_pivotoffsetdiff.y) - (f_topoffs * this_yscale);
		}
		else
		{
			affine_rootpoint.y = (FIXED_TO_FLOAT(interp.z) - affine_pivotoffsetdiff.y) + (f_topoffs * this_yscale);
		}

		if (!affinesprite)
		{
			if (vflip)
			{
				gz = affine_rootpoint.y;
				gzt = gz + (FIXED_TO_FLOAT(spr_height) * this_yscale);
			}
			else
			{
				gzt = affine_rootpoint.y;
				gz = gzt - (FIXED_TO_FLOAT(spr_height) * this_yscale);
			}
		}
		else
		{
			if (vflip)
			{
				affine_rootpoint.y += (base_topoffs * this_yscale);
				gz = affine_rootpoint.y - (f_topoffs * this_yscale);
				gzt = gz + (FIXED_TO_FLOAT(spr_height) * this_yscale);
			}
			else
			{
				affine_rootpoint.y -= (base_topoffs * this_yscale);
				gzt = affine_rootpoint.y + (f_topoffs * this_yscale);
				gz = gzt - (FIXED_TO_FLOAT(spr_height) * this_yscale);
			}
		}
	}

	if (thing->subsector->sector->cullheight)
	{
		if (HWR_DoCulling(thing->subsector->sector->cullheight, viewsector->cullheight, gl_viewz, gz, gzt))
			return;
	}

	heightsec = thing->subsector->sector->heightsec;
	if (viewplayer && viewplayer->mo && viewplayer->mo->subsector)
		phs = viewplayer->mo->subsector->sector->heightsec;
	else
		phs = -1;

	if (heightsec != -1 && phs != -1) // only clip things which are in special sectors
	{
		fixed_t secheight;
		const fixed_t fgzt = FloatToFixed(gzt);

		secheight = P_GetSectorFloorZAt(&sectors[heightsec], viewx, viewy);
		if (viewz < P_GetSectorFloorZAt(&sectors[phs], interp.x, interp.y) ?
			interp.z >= secheight :
			fgzt < secheight)
			return;

		secheight = P_GetSectorCeilingZAt(&sectors[heightsec], viewx, viewy);
		if (viewz > P_GetSectorCeilingZAt(&sectors[phs], interp.x, interp.y) ?
			fgzt < secheight && viewz >= secheight :
			interp.z >= secheight)
			return;
	}

	if ((thing->flags2 & MF2_LINKDRAW) && thing->tracer)
	{
		interpmobjstate_t tracer_interp = {0};

		if (! R_ThingVisible(thing->tracer))
			return;

		R_InterpolateMobjState(thing->tracer, R_GetTimeFrac(RTF_LEVEL), &tracer_interp);

		// calculate tz for tracer, same way it is calculated for this sprite
		// transform the origin point
		tr_x = FIXED_TO_FLOAT(tracer_interp.x) - gl_viewx;
		tr_y = FIXED_TO_FLOAT(tracer_interp.y) - gl_viewy;

		// rotation around vertical axis
		tracertz = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

		// Software does not render the linkdraw sprite if the tracer is behind the view plane,
		// so do the same check here.
		// NOTE: This check has the same flaw as the view plane check at the beginning of HWR_ProjectSprite:
		// the view aiming angle is not taken into account, leading to sprites disappearing too early when they
		// can still be seen when looking down/up at steep angles.
		if (tracertz < ZCLIP_PLANE)
			return;

		// if the sprite is behind the tracer, invert dispoffset, putting the sprite behind the tracer
		if (tz > tracertz)
			dispoffset *= -1;
	}

	// store information in a vissprite
	vis = HWR_NewVisSprite();

	if (affinesprite)
	{
		vis->affine.p1.x = affine_point[0].x;
		vis->affine.p1.y = affine_point[0].y;

		vis->affine.p2.x = affine_point[1].x;
		vis->affine.p2.y = affine_point[1].y;

		vis->affine.p3.x = affine_point[2].x;
		vis->affine.p3.y = affine_point[2].y;

		vis->affine.p4.x = affine_point[3].x;
		vis->affine.p4.y = affine_point[3].y;

		vis->affine.root.x = affine_rootpoint.x;
		vis->affine.root.y = affine_rootpoint.y;
		vis->affine.root.z = affine_rootpoint.z;

		vis->affine.patchcos = rightcos;
		vis->affine.patchsin = rightsin;

		vis->affine.transform.a = affine_transform.a;
		vis->affine.transform.c = affine_transform.b;
		vis->affine.transform.b = affine_transform.c;
		vis->affine.transform.d = affine_transform.d;
		vis->affine.transform.ox = affine_transform.ox;
		vis->affine.transform.oy = affine_transform.oy;

		vis->affine.bounding_point.x = affine_rootpoint.x + leftbound;
		vis->affine.bounding_point.y = affine_rootpoint.z + backbound;
		vis->affine.bounding_point.z = affine_rootpoint.y + topbound;

		// We're using this info to draw the affine sprite; zero-out "standard" draw info.
		vis->x1 = vis->x2 = vis->z1 = vis->z2 = 0;
		vis->gz = vis->gzt = 0;
	}
	else
	{
		vis->x1 = x1;
		vis->x2 = x2;
		vis->z1 = z1;
		vis->z2 = z2;
	}

	vis->tz = tz; // Keep tz for the simple sprite sorting that happens
	vis->tracertz = tracertz;

	vis->renderflags = thing->renderflags;
	vis->rotateflags = sprframe->rotate;

	vis->shadowheight = shadowheight;
	vis->shadowscale = shadowscale;
	vis->dispoffset = dispoffset; // Monster Iestyn: 23/11/15: HARDWARE SUPPORT AT LAST
	vis->flip = flip;

	vis->scale = this_scale;
	vis->spritexscale = spritexscale;
	vis->spriteyscale = spriteyscale;
	vis->spritexoffset = FIXED_TO_FLOAT(spr_offset);
	vis->spriteyoffset = FIXED_TO_FLOAT(spr_topoffset);

	vis->rotated = false;

#ifdef ROTSPRITE
	if (rotsprite)
	{
		vis->gpatch = (patch_t *)rotsprite;
		vis->rotated = true;
	}
	else
#endif
		vis->gpatch = (patch_t *)W_CachePatchNum(sprframe->lumppat[rot], PU_SPRITE);

	vis->mobj = thing;

	//Hurdler: 25/04/2000: now support colormap in hardware mode
	/*
	else if ((vis->mobj->flags & (MF_ENEMY|MF_BOSS)) && (vis->mobj->flags2 & MF2_FRET) && !(vis->mobj->flags & MF_GRENADEBOUNCE) && (leveltime & 1)) // Bosses "flash"
	{
		if (vis->mobj->type == MT_CYBRAKDEMON || vis->mobj->colorized)
			vis->colormap = R_GetTranslationColormap(TC_ALLWHITE, 0, GTC_CACHE);
		else if (vis->mobj->type == MT_METALSONIC_BATTLE)
			vis->colormap = R_GetTranslationColormap(TC_METALSONIC, 0, GTC_CACHE);
		else
			vis->colormap = R_GetTranslationColormap(TC_BOSS, 0, GTC_CACHE);
	}
	*/
	if (thing->color)
	{
		// New colormap stuff for skins Tails 06-07-2002
		if (thing->colorized)
		{
			vis->colormap = R_GetTranslationColormap(
			    R_IsOverlayingSMonitorPlayer(thing) ? TC_BLINK : TC_RAINBOW,
			    thing->color,
			    GTC_CACHE);
		}
		else if (thing->skin && thing->sprite == SPR_PLAY) // This thing is a player!
		{
			size_t skinnum = ((skin_t*)thing->skin)->skinnum;
			vis->colormap = R_GetTranslationColormap((int32_t)skinnum, thing->color, GTC_CACHE);
		}
		else
		{
			vis->colormap = R_GetTranslationColormap(TC_DEFAULT, vis->mobj->color ? vis->mobj->color : SKINCOLOR_GREEN, GTC_CACHE);
		}
	}
	else
	{
		vis->colormap = colormaps;

		if (encoremap && !(thing->flags & MF_DONTENCOREMAP))
			vis->colormap += COLORMAP_REMAPOFFSET;
	}

	if (!affinesprite)
	{
		// set top/bottom coords
		vis->gzt = gzt;
		vis->gz = gz;
	}

	//CONS_Debug(DBG_RENDER, "------------------\nH: sprite  : %d\nH: frame   : %x\nH: type    : %d\nH: sname   : %s\n\n",
	//            thing->sprite, thing->frame, thing->type, sprnames[thing->sprite]);

	vis->vflip = vflip;

	vis->precip = false;
	vis->bbox = false;

	vis->angle = interp.angle;
}

// Precipitation projector for hardware mode
static void HWR_ProjectPrecipitationSprite(precipmobj_t *thing)
{
	gl_vissprite_t *vis;
	float tr_x, tr_y;
	float tz;
	float x1, x2;
	float z1, z2;
	float rightsin, rightcos;
	float this_scale;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	size_t lumpoff;
	unsigned rot = 0;
	uint8_t flip;

	if (!thing)
		return;

	// uncapped/interpolation
	interpmobjstate_t interp = {0};

	// okay... this is a hack, but weather isn't networked, so it should be ok
	if (!P_PrecipThinker(thing))
	{
		return;
	}

	// do interpolation
	R_InterpolatePrecipMobjState(thing, R_GetTimeFrac(RTF_LEVEL), &interp);

	this_scale = FIXED_TO_FLOAT(interp.scale);

	// transform the origin point
	tr_x = FIXED_TO_FLOAT(interp.x) - gl_viewx;
	tr_y = FIXED_TO_FLOAT(interp.y) - gl_viewy;

	// rotation around vertical axis
	tz = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

	// thing is behind view plane?
	if (tz < ZCLIP_PLANE)
		return;

	tr_x = FIXED_TO_FLOAT(interp.x);
	tr_y = FIXED_TO_FLOAT(interp.y);

	// decide which patch to use for sprite relative to player
	if ((unsigned)thing->sprite >= numsprites)
	{
		CONS_Debug(DBG_RENDER, "R_ProjectPrecipitationSprite: invalid sprite number %d\n",
			thing->sprite);
		return;
	}

	sprdef = &sprites[thing->sprite];

	if ((uint8_t)(thing->frame&FF_FRAMEMASK) >= sprdef->numframes)
	{
		CONS_Debug(DBG_RENDER, "R_ProjectPrecipitationSprite: invalid sprite frame %d : %d for %s\n",
			thing->sprite, thing->frame, sprnames[thing->sprite]);
		return;
	}

	sprframe = &sprdef->spriteframes[ thing->frame & FF_FRAMEMASK];


	if (!sprframe)
#ifdef PARANOIA
		I_Error("R_ProjectPrecipitationSprite: sprframes NULL for sprite %d\n", thing->sprite);
#else
		return;
#endif

	// use single rotation for all views
	lumpoff = sprframe->lumpid[0];
	flip = sprframe->flip; // Will only be 0x00 or 0xFF

	rightsin = FIXED_TO_FLOAT(FINESINE((viewangle + ANGLE_90)>>ANGLETOFINESHIFT));
	rightcos = FIXED_TO_FLOAT(FINECOSINE((viewangle + ANGLE_90)>>ANGLETOFINESHIFT));
	if (flip)
	{
		x1 = FIXED_TO_FLOAT(spritecachedinfo[lumpoff].width - spritecachedinfo[lumpoff].offset);
		x2 = FIXED_TO_FLOAT(spritecachedinfo[lumpoff].offset);
	}
	else
	{
		x1 = FIXED_TO_FLOAT(spritecachedinfo[lumpoff].offset);
		x2 = FIXED_TO_FLOAT(spritecachedinfo[lumpoff].width - spritecachedinfo[lumpoff].offset);
	}

	x1 *= this_scale;
	x2 *= this_scale;

	z1 = tr_y + x1 * rightsin;
	z2 = tr_y - x2 * rightsin;
	x1 = tr_x + x1 * rightcos;
	x2 = tr_x - x2 * rightcos;

	//
	// store information in a vissprite
	//
	vis = HWR_NewVisSprite();
	vis->x1 = x1;
	vis->x2 = x2;
	vis->z1 = z1;
	vis->z2 = z2;
	vis->tz = tz;
	vis->dispoffset = 0; // Monster Iestyn: 23/11/15: HARDWARE SUPPORT AT LAST
	vis->gpatch = (patch_t *)W_CachePatchNum(sprframe->lumppat[rot], PU_SPRITE);
	vis->flip = flip;
	vis->mobj = (mobj_t *)thing;

	vis->colormap = NULL;

	if (encoremap && !(thing->flags & MF_DONTENCOREMAP))
		vis->colormap += COLORMAP_REMAPOFFSET;

	// set top/bottom coords
	vis->gzt = FIXED_TO_FLOAT(interp.z) + (FIXED_TO_FLOAT(spritecachedinfo[lumpoff].topoffset) * this_scale);
	vis->gz = vis->gzt - (FIXED_TO_FLOAT(spritecachedinfo[lumpoff].height) * this_scale);

	vis->precip = true;
	vis->bbox = false;
}

void HWR_ProjectBoundingBox(mobj_t *thing)
{
	gl_vissprite_t *vis;
	float tr_x, tr_y;
	float tz;

	// uncapped/interpolation
	interpmobjstate_t interp = {0};

	if (!thing)
		return;

	if (!R_ThingBoundingBoxVisible(thing))
		return;

	R_InterpolateMobjState(thing, R_GetTimeFrac(RTF_LEVEL), &interp);

	// transform the origin point
	tr_x = FIXED_TO_FLOAT(interp.x) - gl_viewx;
	tr_y = FIXED_TO_FLOAT(interp.y) - gl_viewy;

	// rotation around vertical axis
	tz = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

	// thing is behind view plane?
	if (tz < ZCLIP_PLANE)
		return;

	tr_x += gl_viewx;
	tr_y += gl_viewy;

	vis = HWR_NewVisSprite();
	vis->x1 = tr_x - FIXED_TO_FLOAT(interp.radius);
	vis->x2 = tr_x + FIXED_TO_FLOAT(interp.radius);
	vis->z1 = tr_y - FIXED_TO_FLOAT(interp.radius);
	vis->z2 = tr_y + FIXED_TO_FLOAT(interp.radius);
	vis->gz = FIXED_TO_FLOAT(interp.z);
	vis->gzt = vis->gz + FIXED_TO_FLOAT(interp.height);
	vis->mobj = thing;

	vis->precip = false;
	vis->bbox = true;
}

#endif
