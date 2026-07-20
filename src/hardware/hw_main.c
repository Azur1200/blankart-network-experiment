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
//                                                                    GLOBALS
// ==========================================================================

seg_t *gl_curline;
side_t *gl_sidedef;
line_t *gl_linedef;
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

boolean gl_rendering_skybox = false;

// --------------------------------------------------------------------------
//                                              STUFF FOR THE PROJECTION CODE
// --------------------------------------------------------------------------

FTransform atransform;

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

boolean HWR_IsWireframeMode(void)
{
	return (cv_glwireframe.value /*&& cv_debug*/);
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

	if (HWR_UseShader())
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

// Returns true if the midtexture is visible, and false if... it isn't...
boolean HWR_BlendMidtextureSurface(FSurfaceInfo *pSurf)
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

// -----------------+
// HWR_SetViewSize  : set projection and scaling values
// -----------------+
void HWR_SetViewSize(void)
{
	GL_FlushScreenTextures();
}

// Set view aiming, for the sky dome, the skybox,
// and the normal view, all with a single function.
void HWR_SetTransformAiming(FTransform *trans, player_t *player, boolean skybox, boolean side_effect)
{
	angle_t temp_aimingangle;
	// 1 = always on
	// 2 = chasecam only
	if (cv_glshearing.value == 1 || (cv_glshearing.value == 2 && R_IsViewpointThirdPerson(player, skybox)))
	{
		fixed_t fixedaiming = AIMINGTODY(aimingangle);
		trans->viewaiming = FixedToFloat(fixedaiming) * ((float)vid.width / (float)vid.height) / ((float)BASEVIDWIDTH / (float)BASEVIDHEIGHT);
		if (splitscreen == 1) // only for 2 player splitscreen
			trans->viewaiming *= 2.125f; // splitscreen adjusts fov with 0.8, so compensate (but only halfway, since splitscreen means only half the screen is used)
		trans->shearing = true;
		temp_aimingangle = 0;
	}
	else
	{
		trans->shearing = false;
		temp_aimingangle = aimingangle;
	}

	trans->anglex = (float)(temp_aimingangle>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);
	if (side_effect)
		gl_aimingangle = temp_aimingangle;
}

void HWR_ResetClipper(void)
{
	angle_t a1 = gld_FrustumAngle(gl_aimingangle);
	gld_clipper_Clear();
	gld_clipper_SafeAddClipRange(viewangle + a1, viewangle - a1);
#ifdef HAVE_SPHEREFRUSTRUM
	gld_FrustrumSetup();
#endif
}

// Tells the backend are shaders being used for 3d rendering.
static void HWR_SetShaderState(void)
{
	GL_SetSpecialState(HWD_SET_SHADERS, (INT32)HWR_UseShader());
}

// prepare all transform related variables based on the current "frame"
// (R_SetupFrame etc)
void HWR_PrepareTransform(player_t *player, boolean is_skybox)
{
	UINT8 viewnum = R_GetViewNumber();
	camera_t *thiscam = &camera[viewnum];
	const float fpov = FIXED_TO_FLOAT(cv_fov[viewssnum].value+player->fovadd);

	gl_viewx = FIXED_TO_FLOAT(viewx);
	gl_viewy = FIXED_TO_FLOAT(viewy);
	gl_viewz = FIXED_TO_FLOAT(viewz);
	gl_viewsin = FIXED_TO_FLOAT(viewsin);
	gl_viewcos = FIXED_TO_FLOAT(viewcos);

	//04/01/2000: Hurdler: added for T&L
	//                     It should replace all other gl_viewxxx when finished
	memset(&atransform, 0x00, sizeof(FTransform));

	HWR_SetTransformAiming(&atransform, player, is_skybox, true);
	atransform.angley = (float)(viewangle>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);

	gl_viewludsin = FIXED_TO_FLOAT(FINECOSINE(gl_aimingangle>>ANGLETOFINESHIFT));
	gl_viewludcos = FIXED_TO_FLOAT(-FINESINE(gl_aimingangle>>ANGLETOFINESHIFT));

	if (thiscam->postimgflags & POSTIMG_FLIP)
	{
		if (thiscam->postimgflags & POSTIMG_MIRROR)
			atransform.fliptype = TRANSFORM_MIRRORFLIP;
		else
			atransform.fliptype = TRANSFORM_FLIP;
	}
	else if (thiscam->postimgflags & POSTIMG_MIRROR)
	{
		atransform.fliptype = TRANSFORM_MIRROR;
	}

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
}

static void HWR_EnterSkyboxState(void)
{
	HWR_PushBatchingState();
	HWR_PushSpriteState();
	HWR_PushDrawNodeState();
	gl_rendering_skybox = true;
}

static void HWR_LeaveSkyboxState(void)
{
	HWR_PopBatchingState();
	HWR_PopSpriteState();
	HWR_PopDrawNodeState();
	gl_rendering_skybox = false;
}

void HWR_RenderViewpoint(player_t *player, boolean drawSkyTexture, boolean is_skybox, boolean timing, gl_portal_t *rootportal)
{
	unsigned int i;
	gl_portal_array_t *portal_array;

	const boolean wireframe = HWR_IsWireframeMode();

	HWR_PrepareTransform(player, is_skybox);
	// we need to set our transformations now so portal clipping
	// has the projectionmatrix set up with our actual view
	// since our clipper uses this to determine our actual visible geometry
	GL_SetTransform(&atransform);
	HWR_ClearSprites();
	HWR_ResetClipper();

	if (rootportal)
		HWR_PortalClipping(rootportal);

	if (wireframe)
		GL_SetSpecialState(HWD_SET_WIREFRAME, 1);

	if (timing)
	{
		ps_numbspcalls = 0;
		ps_numpolyobjects = 0;
		ps_bsptime = I_GetPreciseTime();
	}

	validcount++;

	HWR_StartBatching();

	HWR_AddPrecipitationSprites();

	HWR_RenderBSPNode((INT32)numnodes-1);
	// restore portal clipping variables
	gl_portalcullsector = NULL;
	gl_portalclipline = NULL;

	if (timing)
	{
		ps_bsptime = I_GetPreciseTime() - ps_bsptime;
	}

	if (gl_printportals && !gl_portal_level && !gl_rendering_skybox)
		CONS_Printf("Portal recursion summary:\n");

	//if (gl_printportals)
	//	CONS_Printf("%d bsp calls\n", ps_numbspcalls);

	{
		// HWR_DrawSkyBackground is not able to set the texture without
		// pausing batching first
		HWR_PauseBatching();
		if (skyboxmo[0] && cv_skybox.value && !is_skybox && !rootportal && !gl_debugportal)
		{
			//if (gl_printportals)
			//	CONS_Printf("drawing a skybox\n");
			R_SkyboxFrame(viewssnum);

			// render skybox while keeping batches, sprites and drawnodes
			// from the regular viewpoint stashed in the state stacks
			HWR_EnterSkyboxState();
			HWR_RenderViewpoint(player, true, true, false, rootportal);
			HWR_LeaveSkyboxState();

			// restore (=clear) z-buffer, but only in the portal window
			if (rootportal)
				HWR_RenderDepthEraser(false);
			else
				GL_ClearBuffer(false, true, false, 0);

			// restore transform
			if (rootportal)
				HWR_PortalFrame(rootportal, false);
			else
				R_SetupFrame(viewssnum, is_skybox);

			HWR_PrepareTransform(player, is_skybox);
		}
		else
		{
			if (drawSkyTexture)
				HWR_DrawSkyBackground(player);
		}
		// turn batching back on
		HWR_StartBatching();
	}

	// apply transform to backend now when we're actually drawing to the screen
	GL_SetTransform(&atransform);
	HWR_ResetClipper();

	HWR_RenderBatches();

	// this is a hacky way to get rid of the main viewpoint for the debugportal
	// command but maybe simpler than other options
	if (gl_debugportal && !gl_portal_level)
		GL_ClearBuffer(true, true, true, 0);

	portal_array = HWR_GetPortalArray();
	for (i = 0; i < portal_array->size; i++)
		HWR_RenderPortal(&portal_array->portals[i], rootportal, player, is_skybox);
	HWR_ClearPortals();
	// if there was portals, restore stencil state since HWR_RenderPortal
	// has altered it
	if (i)
	{
		HWR_SetStencilState(gl_portal_level ?
		HWD_STENCIL_PORTAL_INSIDE : HWD_STENCIL_INACTIVE);
		if (gl_printportals && !gl_rendering_skybox)
		{
			CONS_Printf("%*c%d: %u portals rendered\n", gl_portal_level+1, 'L',
						gl_portal_level, i);
		}
	}

	// hack for debugportal, see earlier comment
	if (gl_debugportal && !gl_portal_level)
	{
		gl_portal_level = 15; // this will make the sprites and drawnodes get discarded
		HWR_SetStencilState(HWD_STENCIL_PORTAL_INSIDE);
		gl_portal_level = 0;
	}

	// Draw MD2 and sprites

	if (timing)
	{
		ps_hw_spritesorttime = I_GetPreciseTime();
	}

	UINT32 count = HWR_SortVisSprites();

	if (timing)
		ps_numsprites = count;

	if (timing)
	{
		ps_hw_spritesorttime = I_GetPreciseTime() - ps_hw_spritesorttime;
		ps_hw_spritedrawtime = I_GetPreciseTime();
	}

	HWR_DrawSprites();

	if (timing)
	{
		ps_hw_spritedrawtime = I_GetPreciseTime() - ps_hw_spritedrawtime;
	}

#ifdef NEWCORONAS
	//Hurdler: they must be drawn before translucent planes, what about gl fog?
	HWR_DrawCoronas();
#endif

	if (timing)
	{
		ps_numdrawnodes = 0;
		ps_hw_nodesorttime = 0;
		ps_hw_nodedrawtime = 0;
	}

	HWR_RenderDrawNodes(); //Hurdler: render 3D water and transparent walls after everything

	if (wireframe)
		GL_SetSpecialState(HWD_SET_WIREFRAME, 0);

	// hack for debugportal, see earlier comment
	if (gl_debugportal && !gl_portal_level)
		HWR_SetStencilState(HWD_STENCIL_INACTIVE);
}

// ==========================================================================
// Same as rendering the player view, but from the skybox object
// ==========================================================================
void HWR_RenderSkyboxView(player_t *player)
{
	// note: sets viewangle, viewx, viewy, viewz
	R_SkyboxFrame(viewssnum);

	HWR_RenderViewpoint(player, true, true, false, NULL);
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

// -----------------+
// HWR_ClearView : clear the viewwindow, with maximum z value. also clears stencil buffer.
// -----------------+
static inline void HWR_ClearView(void)
{
	GL_GClipRect(viewwindowx,
				 viewwindowy,
				 (viewwindowx + viewwidth),
				 (viewwindowy + viewheight),
				 ZCLIP_PLANE);

	GL_ClearBuffer(false, true, true, NULL);
}

void HWR_RenderPlayerView(void)
{
	player_t * player = &players[displayplayers[viewssnum]];

	const boolean skybox = (skyboxmo[0] && cv_skybox.value); // True if there's a skybox object and skyboxes are on

	FRGBAFloat ClearColor;

	ClearColor.red = 0.0f;
	ClearColor.green = 0.0f;
	ClearColor.blue = 0.0f;
	ClearColor.alpha = 1.0f;

	if (cv_glshaders.value)
	{
		GL_SetShaderInfo(HWD_SHADERINFO_LEVELTIME, (INT32)leveltime); // The water surface shader needs the leveltime.

		const angle_t light_angle = maplighting.angle - viewangle + ANGLE_90; // I fucking hate OGL's coordinate system
		GL_SetShaderInfo(HWD_SHADERINFO_LIGHT_X, FINECOSINE(light_angle >> ANGLETOFINESHIFT));
		GL_SetShaderInfo(HWD_SHADERINFO_LIGHT_Y, 0);
		GL_SetShaderInfo(HWD_SHADERINFO_LIGHT_Z,  -FINESINE(light_angle >> ANGLETOFINESHIFT));

		GL_SetShaderInfo(HWD_SHADERINFO_LIGHT_CONTRAST, maplighting.contrast);
		GL_SetShaderInfo(HWD_SHADERINFO_LIGHT_BACKLIGHT, maplighting.backlight);
	}

	if (viewssnum == 0) // Only do it if it's the first screen being rendered
		GL_ClearBuffer(true, true, true, &ClearColor); // Clear the Color Buffer, stops HOMs. Also seems to fix the skybox issue on Intel GPUs.

	ps_hw_skyboxtime = I_GetPreciseTime();
	if (skybox) // If there's a skybox and we should be drawing the sky, draw the skybox
	{
		// Check for new console commands.
		NetUpdate();

		HWR_ClearView();
		HWR_RenderSkyboxView(player); // This is drawn before everything else so it is placed behind

		// Check for new console commands.
		NetUpdate();
	}
	ps_hw_skyboxtime = I_GetPreciseTime() - ps_hw_skyboxtime;

	if (!HWR_ShouldUsePaletteRendering())
	{
		// do we really need to save player (is it not the same)?
		player_t *saved_player = stplyr;
		stplyr = player;
		ST_doPaletteStuff();
		stplyr = saved_player;
	}

	// Reset the shader state.
	HWR_SetShaderState();

	// note: sets viewangle, viewx, viewy, viewz
	R_SetupFrame(viewssnum, skybox);
	framecount++; // timedemo

	// Check for new console commands.
	NetUpdate();

	HWR_ClearView();

	HWR_RenderViewpoint(player,
						!skybox, // Don't draw the regular sky if there's a skybox
						false,
						true, // Main view is profiled
						NULL);

	gl_printportals = false;
	gl_debugportal = 0;

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
static CV_PossibleValue_t glsprbillboard_cons_t[] = {{0, "Off"}, {1, "On"}, {2, "Players"}, {0, NULL}};

static void CV_glfiltermode_OnChange(void);
static void CV_glanisotropic_OnChange(void);
static void CV_screentextures_OnChange(void);

static CV_PossibleValue_t glfiltermode_cons_t[]= {{HWD_SET_TEXTUREFILTER_POINTSAMPLED, "Nearest"},
	{HWD_SET_TEXTUREFILTER_BILINEAR, "Bilinear"}, {HWD_SET_TEXTUREFILTER_TRILINEAR, "Trilinear"},
	{HWD_SET_TEXTUREFILTER_MIXED1, "Linear_Nearest"},
	{HWD_SET_TEXTUREFILTER_MIXED2, "Nearest_Linear"},
	{HWD_SET_TEXTUREFILTER_MIXED3, "Nearest_Mipmap"},
	{0, NULL}};
CV_PossibleValue_t glanisotropicmode_cons_t[] = {{1, "MIN"}, {16, "MAX"}, {0, NULL}};
static CV_PossibleValue_t glsecbright_cons_t[] = {{0, "MIN"}, {255, "MAX"}, {0, NULL}};

consvar_t cv_glshaders = CVAR_INIT ("gr_shaders", "On", CV_SAVE, glshaders_cons_t, NULL);
consvar_t cv_fovchange = CVAR_INIT ("gr_fovchange", "Off", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_glsecbright = CVAR_INIT("gr_secbright", "0", CV_SAVE, glsecbright_cons_t, NULL);

// The current screen texture implementation is inefficient and disabling it can result in significant
// performance gains on lower end hardware. The game is still quite playable without this functionality.
// Features that break when disabling this:
//  - water and heat wave effects
//  - intermission background
//  - full screen scaling (use native resolution or windowed mode to avoid this)
static CV_PossibleValue_t glscreentextures_cons_t[] = {{0, "Off"}, {1, "Wipes Only"}, {2, "All"}, {0, NULL}};
consvar_t cv_glscreentextures = CVAR_INIT("gr_screentextures", "All", CV_CALL|CV_SAVE, glscreentextures_cons_t, CV_screentextures_OnChange);

consvar_t cv_glmodels = CVAR_INIT ("gr_models", "Off", CV_SAVE, CV_OnOff, NULL);

#ifdef BAD_MODEL_OPTIONS
consvar_t cv_glmodelinterpolation = CVAR_INIT ("gr_modelinterpolation", "Sometimes", CV_SAVE, glmodelinterpolation_cons_t, NULL);
consvar_t cv_glmodellighting = CVAR_INIT ("gr_modellighting", "On", CV_SAVE, CV_OnOff, NULL);
#endif

consvar_t cv_glshearing = CVAR_INIT ("gr_shearing", "Off", CV_SAVE, glshearing_cons_t, NULL);
consvar_t cv_glspritebillboarding = CVAR_INIT ("gr_spritebillboarding", "On", CV_SAVE, glsprbillboard_cons_t, NULL);
consvar_t cv_glskydome = CVAR_INIT ("gr_skydome", "On", CV_SAVE, CV_OnOff, NULL);

consvar_t cv_glfiltermode = CVAR_INIT ("gr_filtermode", "Nearest", CV_SAVE|CV_CALL, glfiltermode_cons_t, CV_glfiltermode_OnChange);
consvar_t cv_glanisotropicmode = CVAR_INIT ("gr_anisotropicmode", "1", CV_SAVE|CV_CALL, glanisotropicmode_cons_t, CV_glanisotropic_OnChange);

consvar_t cv_glsolvetjoin = CVAR_INIT ("gr_solvetjoin", "On", 0, CV_OnOff, NULL);

consvar_t cv_glpolytile = CVAR_INIT ("gr_polytile", "Off", 0, CV_OnOff, NULL);

CV_PossibleValue_t grpolyshape_cons_t[] = {
	{0, "Subsector"},
	{1, "Fat"},
	{2, "Trim"},
	{3, "NotConvex"},
	{0, NULL}
};
consvar_t cv_glpolyshape = CVAR_INIT ("gr_polygon_shape", "Trim", CV_SAVE, grpolyshape_cons_t, NULL);

consvar_t cv_glbatching = CVAR_INIT ("gr_batching", "On", 0, CV_OnOff, NULL);

consvar_t cv_glwireframe = CVAR_INIT ("gr_wireframe", "Off", 0, CV_OnOff, NULL);

CV_PossibleValue_t glpalettedepth_cons_t[] = {{16, "16 bits"}, {24, "24 bits"}, {0, NULL}};
void CV_glpaletterendering_OnChange(void);
void CV_glpalettedepth_OnChange(void);
void CV_gllightdithering_OnChange(void);

consvar_t cv_glpaletterendering = CVAR_INIT ("gr_paletteshader", "Off", CV_CALL|CV_SAVE, CV_OnOff, CV_glpaletterendering_OnChange);
consvar_t cv_glpalettedepth = CVAR_INIT ("gr_palettedepth", "24 bits", CV_SAVE|CV_CALL, glpalettedepth_cons_t, CV_glpalettedepth_OnChange);

consvar_t cv_gllightdither = CVAR_INIT ("gr_lightdithering", "Off", CV_CALL|CV_SAVE, CV_OnOff, CV_gllightdithering_OnChange);

// Isolates rendering to one of the top level portals.
// (Stencil cutting of the portal is also disabled)
// Use gr_printportals to find the number to use.
consvar_t cv_gldebugportal = CVAR_INIT ("gr_debugportal", "0", 0, CV_Unsigned, NULL);

consvar_t cv_glskydebug = CVAR_INIT ("gr_skydebug", "0", 0, CV_Unsigned, NULL);

#define ONLY_IF_GL_LOADED if (vid.glstate != VID_GL_LIBRARY_LOADED) return;

static void CV_screentextures_OnChange(void)
{
	ONLY_IF_GL_LOADED
	if (cv_glscreentextures.value != 2)
	{
		if (cv_glpaletterendering.value != 0)
			CV_SetValue(&cv_glpaletterendering, 0);
	}
	GL_SetSpecialState(HWD_SET_SCREEN_TEXTURES, cv_glscreentextures.value);
}

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

	if (cv_glpaletterendering.value != 0 && cv_glscreentextures.value != 2) // can't do palette rendering without screen textures
		CV_SetValue(&cv_glpaletterendering, 0);

	if (gl_shadersavailable)
	{
		HWR_CompileShaders();
		HWR_TogglePaletteRendering();
		HWR_PrecacheLevel();
	}
}

void CV_gllightdithering_OnChange(void)
{
	ONLY_IF_GL_LOADED
	if (gl_shadersavailable)
	{
		HWR_CompileShaders();
	}
}

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

static void Command_Glprintportals_f(void)
{
	if (cht_debug)
	{
		gl_printportals = true;
		CONS_Printf("List of top level portals:\n");
	}
	else
	{
		CONS_Printf("This command is only available in devmode.\n");
	}
}

//added by Hurdler: console varibale that are saved
void HWR_AddCommands(void)
{
	CV_RegisterVar(&cv_fovchange);

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

	CV_RegisterVar(&cv_glfiltermode);

	CV_RegisterVar(&cv_glsolvetjoin);
	CV_RegisterVar(&cv_glpolytile);
	CV_RegisterVar(&cv_glpolyshape);

	CV_RegisterVar(&cv_glbatching);

	CV_RegisterVar(&cv_glwireframe);

	CV_RegisterVar(&cv_glanisotropicmode);

	CV_RegisterVar(&cv_glpaletterendering);
	CV_RegisterVar(&cv_glpalettedepth);

	CV_RegisterVar(&cv_gllightdither);

	COM_AddCommand("gr_printportals", Command_Glprintportals_f);
	CV_RegisterVar(&cv_gldebugportal);

	CV_RegisterVar(&cv_glskydebug);

	CV_RegisterVar(&cv_glscreentextures);
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

		HWR_LoadAllBaseShaders();
		gl_shadersavailable = HWR_InitShaders();
		HWR_SetShaderState();
		HWR_TogglePaletteRendering();

		if (cv_glscreentextures.value != 2)
		{
			if (cv_glpaletterendering.value != 0)
				CV_SetValue(&cv_glpaletterendering, 0);
		}

		GL_SetSpecialState(HWD_SET_SCREEN_TEXTURES, cv_glscreentextures.value);
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

	// screen textures are needed for the rest of the effects
	if (cv_glscreentextures.value != 2)
		return;

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
	if (cv_glscreentextures.value == 0)
	{
		V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31); // just draw a black screen instead of flashing and crap
		return;
	}

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
