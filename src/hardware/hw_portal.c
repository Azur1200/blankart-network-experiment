// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2022 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_portal.c
/// \brief Visual Portal rendering

#ifdef HWRENDER
#include "hw_glob.h"
#include "hw_clip.h"
#include "hw_gpu.h"
#include "../r_local.h"
#include "../g_game.h"
#include "../r_fps.h"
#include "../z_zone.h"

// TODO magic number 16
gl_portal_array_t gl_portal_arrays[MAXPORTALS_CAP+1] = {0};

int32_t gl_portal_level = 0; // portal recursion level

dboolean gl_drawing_stencil = false; // used when drawing segs to stencil buffer
sector_t *gl_portalcullsector = NULL;
line_t *gl_portalclipline = NULL;

// debug tools
dboolean gl_printportals = false; // print info about portals on this frame
int32_t gl_debugportal = 0; // hide main viewpoint and only render this portal without stencil

void HWR_PortalFrame(gl_portal_t *portal, dboolean set_culling)
{
	viewx = portal->viewx;
	viewy = portal->viewy;
	viewz = portal->viewz;

	viewangle = portal->viewangle;
	viewsin = FINESINE(viewangle>>ANGLETOFINESHIFT);
	viewcos = FINECOSINE(viewangle>>ANGLETOFINESHIFT);

	// set_culling will be true if RenderBSPNode is about to be called
	// so need to enable portal culling for that
	if (set_culling)
	{
		gl_portalcullsector = portal->clipline->frontsector;
		gl_portalclipline = portal->clipline;
	}
}

// get currently used portal array
gl_portal_array_t *HWR_GetPortalArray(void)
{
	int32_t level = gl_portal_level;
	if (gl_rendering_skybox)
		level++;
	return &gl_portal_arrays[level];
}

dboolean HWR_AddPortal(line_t *start, line_t *dest, seg_t *seg)
{
	gl_portal_array_t *array;
	gl_portal_t *portal;

	angle_t dangle;
	fixed_t disttopoint;
	angle_t angtopoint;
	vertex_t dest_c, start_c;

	if ((gl_portal_level + gl_rendering_skybox) >= cv_maxportals.value ||
		(gl_debugportal &&
		(gl_debugportal != (start-lines) || gl_portal_level)))
	{
		return false;
	}
	if (gl_debugportal)
		gl_debugportal = -1; // skip other portal segs from the same line

	array = HWR_GetPortalArray();

	// yet another dynamic array
	if (!array->portals)
	{
		array->capacity = INIT_PORTAL_ARRAY_SIZE;
		array->portals = Z_Malloc(sizeof(gl_portal_t) * array->capacity, PU_LEVEL,
								  &array->portals);
	}
	else if (array->size == array->capacity)
	{
		array->capacity *= 2;
		array->portals = Z_Realloc(array->portals, sizeof(gl_portal_t) * array->capacity,
								   PU_LEVEL, &array->portals);
	}

	portal = &array->portals[array->size++];

	// Most fixed-point calculations and trigonometric function tables are replaced by
	// floats and cmath library calls in this part to improve the precision of the
	// location and angle of the new viewpoint.
	//
	// This reduces artefacts on the edges of portals, showing thin lines/pixels
	// of the underlying graphics. (for example the sky texture) It's not 100%
	// perfectly aligned and artefact-free, but looks noticeably
	// better than the original code. I'm not even sure if it's this
	// code or the nodebuilder or hw_map or something else causing the remaining issues..
	//#define R_PointToAngle2Precise R_PointToAngle2
	//#define R_PointToDist2Precise R_PointToDist2
	dangle = R_PointToAngle2Precise(0,0,dest->dx,dest->dy) - R_PointToAngle2Precise(start->dx,start->dy,0,0);

	// looking glass center
	start_c.x = start->v1->x/2 + start->v2->x/2;
	start_c.y = start->v1->y/2 + start->v2->y/2;

	// other side center
	dest_c.x = dest->v1->x/2 + dest->v2->x/2;
	dest_c.y = dest->v1->y/2 + dest->v2->y/2;

	disttopoint = R_PointToDist2Precise(start_c.x, start_c.y, viewx, viewy);
	angtopoint = R_PointToAngle2Precise(start_c.x, start_c.y, viewx, viewy);
	angtopoint += dangle;

	float fang = ((float)angtopoint / 4294967296.0f) * 2.0f * M_PI;

	//portal->viewx = dest_c.x + FixedMul(FINECOSINE(angtopoint>>ANGLETOFINESHIFT), disttopoint);
	//portal->viewy = dest_c.y + FixedMul(FINESINE(angtopoint>>ANGLETOFINESHIFT), disttopoint);
	//portal->viewx = dest_c.x + FixedMul(FLOAT_TO_FIXED(cos(fang)), disttopoint);
	//portal->viewy = dest_c.y + FixedMul(FLOAT_TO_FIXED(sin(fang)), disttopoint);
	// cos and sin are just scaling disttopoint so no need for float-fixed conversions
	portal->viewx = dest_c.x + (fixed_t)(cos(fang) * disttopoint);
	portal->viewy = dest_c.y + (fixed_t)(sin(fang) * disttopoint);
	portal->viewz = viewz + dest->frontsector->floorheight - start->frontsector->floorheight;
	portal->viewangle = viewangle + dangle;
	portal->seg = seg;
	portal->clipline = dest;

	portal->angle1 = R_PointToAngle64(seg->v1->x, seg->v1->y) + dangle;
	portal->angle2 = R_PointToAngle64(seg->v2->x, seg->v2->y) + dangle;

	return true;
}

void HWR_ClearPortals(void)
{
	HWR_GetPortalArray()->size = 0;
}

// clip the area outside the portal destination window
void HWR_PortalClipping(gl_portal_t *portal)
{
	gld_clipper_SafeAddClipRange(portal->angle1, portal->angle2);
}

void HWR_RenderPortalSeg(seg_t *seg)
{
	gl_drawing_stencil = true;
	gl_curline = seg;
	gl_frontsector = seg->frontsector;
	gl_backsector = seg->backsector;
	HWR_ProcessSeg();
	gl_drawing_stencil = false;
	// need to work around the r_opengl PF_Invisible bug with this call
	// similarly as in the linkdraw hack in HWR_DrawSprites
	GL_SetBlend(PF_Translucent|PF_Occlude|PF_Masked);
}

void HWR_SetStencilState(int32_t state)
{
	GL_SetStencilMode(state, gl_portal_level);
}

// clear the depth buffer from the stenciled area so portal
// content doesn't get clipped by previous buffer content
// (glClear ignores the stencil buffer so can't be used for this purpose)
void HWR_RenderDepthEraser(dboolean visible)
{
	FOutVector verts[4] = {0};
	FBITFIELD blendflags = PF_Occlude|PF_NoDepthTest|PF_NoTexture|PF_NoAlphaTest;
	if (!visible)
		blendflags |= PF_Invisible;
	// so this is apparently how you draw the far clipping plane when
	// pfnSetTransform(NULL) is active
	const float a = FAR_CLIPPING_PLANE;
	verts[0].x = -a; verts[0].y = -a; verts[0].z = a;
	verts[1].x = -a; verts[1].y =  a; verts[1].z = a;
	verts[2].x =  a; verts[2].y =  a; verts[2].z = a;
	verts[3].x =  a; verts[3].y = -a; verts[3].z = a;
	GL_SetTransform(NULL);
	GL_DrawPolygon(NULL, verts, 4, blendflags);
}

void HWR_RenderPortal(gl_portal_t *portal, gl_portal_t *rootportal, player_t *player, dboolean is_skybox)
{
	HWR_PushSpriteState();
	HWR_PushDrawNodeState();

	if (!gl_debugportal)
	{
		HWR_SetStencilState(HWD_STENCIL_PORTAL_BEGIN);
		HWR_RenderPortalSeg(portal->seg);
	}

	gl_portal_level++;
	if (!gl_debugportal)
		HWR_SetStencilState(HWD_STENCIL_PORTAL_INSIDE);

	HWR_RenderDepthEraser(true);

	HWR_PortalFrame(portal, true);
	HWR_RenderViewpoint(player, true, is_skybox, false, portal);
	// restore previous frame and transform
	if (rootportal)
		HWR_PortalFrame(rootportal, false);
	else if (is_skybox)
		R_SkyboxFrame(viewssnum);
	else
		R_SetupFrame(viewssnum, is_skybox);
	HWR_PrepareTransform(player, is_skybox);
	GL_SetTransform(&atransform);
	HWR_ResetClipper();

	if (!gl_debugportal)
	{
		HWR_SetStencilState(HWD_STENCIL_PORTAL_FINISH);
		HWR_RenderPortalSeg(portal->seg);
	}
	gl_portal_level--;

	HWR_PopSpriteState();
	HWR_PopDrawNodeState();
}

#endif
