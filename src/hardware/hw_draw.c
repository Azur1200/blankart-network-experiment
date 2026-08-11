// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_draw.c
/// \brief miscellaneous drawing (mainly 2d)

#ifdef __GNUC__
#include <unistd.h>
#endif

#include "../doomdef.h"

#ifdef HWRENDER
#include "hw_main.h"
#include "hw_glob.h"
#include "hw_gpu.h"

#include "../r_draw.h" //viewborderlump
#include "../r_main.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../v_video.h"
#include "../st_stuff.h"
#include "../p_local.h" // stplyr
#include "../g_game.h" // players
#include "../k_hud.h"
#include "../r_plane.h" // R_FlatDimensionsFromLumpSize

#include <fcntl.h>

typedef struct
{
	UINT8 id_field_length ; // 1
	UINT8 color_map_type  ; // 2
	UINT8 image_type      ; // 3
	UINT8 dummy[5]        ; // 4,  8
	INT16 x_origin        ; // 9, 10
	INT16 y_origin        ; //11, 12
	INT16 width           ; //13, 14
	INT16 height          ; //15, 16
	UINT8 image_pix_size  ; //17
	UINT8 image_descriptor; //18
} ATTRPACK TGAHeader; // sizeof is 18

static UINT8 softwaretranstogl[11]    = {  0, 25, 51, 76,102,127,153,178,204,229,255};
static UINT8 softwaretranstogl_hi[11] = {  0, 51,102,153,204,255,255,255,255,255,255};
static UINT8 softwaretranstogl_lo[11] = {  0, 12, 24, 36, 48, 60, 71, 83, 95,111,127};

void HWR_DrawStretchyFixedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, INT32 option, const UINT8 *colormap)
{
	FOutVector v[4];
	FBITFIELD flags;
	float cx = FIXED_TO_FLOAT(x);
	float cy = FIXED_TO_FLOAT(y);
	UINT32 alphalevel, blendmode;
	GLPatch_t *hwrPatch;

//  3--2
//  | /|
//  |/ |
//  0--1
	float dup, fscalew, fscaleh, fwidth, fheight;

	const cliprect_t *clip = V_GetClipRect();

	float s_min, s_max;
	float t_min, t_max;

	// make patch ready in hardware cache
	if (!colormap)
		HWR_GetPatch(gpatch);
	else
		HWR_GetMappedPatch(gpatch, colormap);

	hwrPatch = ((GLPatch_t *)gpatch->hardware);

	dup = (float)vid.dup;

	switch (option & V_SCALEPATCHMASK)
	{
		case V_NOSCALEPATCH:
			dup = 1.0f;
			break;
		case V_SMALLSCALEPATCH:
			dup = (float)vid.smalldup;
			break;
		case V_MEDSCALEPATCH:
			dup = (float)vid.meddup;
			break;
	}

	fscalew = fscaleh = FIXED_TO_FLOAT(pscale);
	if (vscale != pscale)
		fscaleh = FIXED_TO_FLOAT(vscale);

	// See my comments in v_video.c's V_DrawFixedPatch
	// -- Monster Iestyn 29/10/18
	{
		float offsetx = 0.0f, offsety = 0.0f;

		// left offset
		if (option & V_FLIP)
			offsetx = (float)(gpatch->width - gpatch->leftoffset) * fscalew;
		else
			offsetx = (float)(gpatch->leftoffset) * fscalew;

		// top offset
		if (option & V_VFLIP)
			offsety = (float)(gpatch->height - gpatch->topoffset) * fscaleh;
		else
			offsety = (float)(gpatch->topoffset) * fscaleh;

		cx -= offsetx;
		cy -= offsety;
	}

	if (!(option & V_NOSCALESTART))
	{
		cx = cx * dup;
		cy = cy * dup;

		if (!(option & V_SCALEPATCHMASK))
		{
			INT32 intx, inty;
			intx = (INT32)cx;
			inty = (INT32)cy;
			V_AdjustXYWithSnap(&intx, &inty, option, dup);
			cx = (float)intx;
			cy = (float)inty;
		}
	}

	if (pscale != FRACUNIT || (r_splitscreen && option & V_SPLITSCREEN))
	{
		fwidth = (float)(gpatch->width) * fscalew * dup;
		fheight = (float)(gpatch->height) * fscaleh * dup;
	}
	else
	{
		fwidth = (float)(gpatch->width) * dup;
		fheight = (float)(gpatch->height) * dup;
	}

	s_min = t_min = 0.0f;
	s_max = hwrPatch->max_s;
	t_max = hwrPatch->max_t;

	if (clip)
	{
		float cx1 = cx;
		float cy1 = cy;
		float cx2 = cx + fwidth;
		float cy2 = cy + fheight;

		if (cx1 < clip->left)
		{
			s_min = (clip->left - cx1) / fwidth * hwrPatch->max_s;
			cx1 = clip->left;
		}

		if (cy1 < clip->top)
		{
			t_min = (clip->top - cy1) / fheight * hwrPatch->max_t;
			cy1 = clip->top;
		}

		if (cx2 > clip->right)
		{
			s_max = s_min + (clip->right - cx1) / fwidth * hwrPatch->max_s;
			cx2 = clip->right;
		}

		if (cy2 > clip->bottom)
		{
			t_max = t_min + (clip->bottom - cy1) / fheight * hwrPatch->max_t;
			cy2 = clip->bottom;
		}

		cx = cx1;
		cy = cy1;
		fwidth = fmaxf(0.0f, cx2 - cx1);
		fheight = fmaxf(0.0f, cy2 - cy1);
	}

	// positions of the cx, cy, are between 0 and vid.width/vid.height now, we need them to be between -1 and 1
	cx = -1.0f + (cx / (vid.width / 2.0f));
	cy = 1.0f - (cy / (vid.height / 2.0f));

	// fwidth and fheight are similar
	fwidth /= vid.width / 2;
	fheight /= vid.height / 2;

	// set the polygon vertices to the right positions
	v[0].x = v[3].x = cx;
	v[2].x = v[1].x = cx + fwidth;

	v[0].y = v[1].y = cy;
	v[2].y = v[3].y = cy - fheight;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	if (option & V_FLIP)
	{
		v[0].s = v[3].s = s_max;
		v[2].s = v[1].s = s_min;
	}
	else
	{
		v[0].s = v[3].s = s_min;
		v[2].s = v[1].s = s_max;
	}

	if (option & V_VFLIP)
	{
		v[0].t = v[1].t = t_max;
		v[2].t = v[3].t = t_min;
	}
	else
	{
		v[0].t = v[1].t = t_min;
		v[2].t = v[3].t = t_max;
	}

	flags = PF_NoDepthTest;

	/*if (option & V_WRAPX)
		flags |= PF_ForceWrapX;
	if (option & V_WRAPY)
		flags |= PF_ForceWrapY;*/

	// clip it since it is used for bunny scroll in doom I
	if ((blendmode = ((option & V_BLENDMASK) >> V_BLENDSHIFT)))
		blendmode++; // realign to constants
	if ((alphalevel = ((option & V_ALPHAMASK) >> V_ALPHASHIFT)) || blendmode)
	{
		FSurfaceInfo Surf = {};
		Surf.PolyColor.s.red = Surf.PolyColor.s.green = Surf.PolyColor.s.blue = 0xff;

		flags |= HWR_GetBlendModeFlag(blendmode);

		if (alphalevel == 13)
			Surf.PolyColor.s.alpha = softwaretranstogl_lo[V_GetHUDTranslucency(option)];
		else if (alphalevel == 14)
			Surf.PolyColor.s.alpha = softwaretranstogl[V_GetHUDTranslucency(option)];
		else if (alphalevel == 15)
			Surf.PolyColor.s.alpha = softwaretranstogl_hi[V_GetHUDTranslucency(option)];
		else if (alphalevel < 10)
			Surf.PolyColor.s.alpha = softwaretranstogl[min(max((10 - alphalevel), 0), 10)];
		else
			Surf.PolyColor.s.alpha = 0;

		GL_DrawPolygon(&Surf, v, 4, flags|PF_Modulated);
	}
	else
		GL_DrawPolygon(NULL, v, 4, flags|PF_Translucent);
}

void HWR_DrawAffinePatch(patch_t *gpatch, fixed_t x, fixed_t y, const affine_t *transform, INT32 option, const UINT8 *colormap)
{
	//const cliprect_t *clip = V_GetClipRect();

	// make patch ready in hardware cache
	if (!colormap)
		HWR_GetPatch(gpatch);
	else
		HWR_GetMappedPatch(gpatch, colormap);

	// positions of the x, y, are between 0 and vid.width/vid.height now, we need them to be between -1 and 1
	float fwidth = vid.width;// / vid.dup;
	float fheight = vid.height;// / vid.dup;
	float cx = -1.0f + (x / (fwidth/2));
	float cy = 1.0f - (y / (fheight/2));

	float fa = FIXED_TO_FLOAT(transform->a) / vid.dup;
	float fd = FIXED_TO_FLOAT(transform->d) / vid.dup;
	float fc = FIXED_TO_FLOAT(transform->c) / vid.dup;
	float fb = FIXED_TO_FLOAT(transform->b) / vid.dup;
	float fx = FIXED_TO_FLOAT(transform->ox);
	float fy = FIXED_TO_FLOAT(transform->oy);

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
	if (fpclassify(determinant) == FP_ZERO)
		return;
	float ba = fd / determinant;
	float bb = -fb / determinant;
	float bc = -fc / determinant;
	float bd = fa / determinant;

	// set the polygon vertices to the right positions
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	FOutVector v[4] = {
		[3] = { .x = ba * -fx + bb * -fy + fx,
		        .y = bc * -fx + bd * -fy + fy },
		[2] = { .x = ba * (gpatch->width - fx) + bb * -fy + fx,
		        .y = bc * (gpatch->width - fx) + bd * -fy + fy },
		[0] = { .x = ba * -fx + bb * (gpatch->height - fy) + fx,
		        .y = bc * -fx + bd * (gpatch->height - fy) + fy },
		[1] = { .x = ba * (gpatch->width - fx) + bb * (gpatch->height - fy) + fx,
		        .y = bc * (gpatch->width - fx) + bd * (gpatch->height - fy) + fy },
	};

	// normalize to -1,1
	v[0].x = cx + (v[0].x / fwidth*2);
	v[1].x = cx + (v[1].x / fwidth*2);
	v[2].x = cx + (v[2].x / fwidth*2);
	v[3].x = cx + (v[3].x / fwidth*2);
	v[0].y = cy - (v[0].y / fheight*2);
	v[1].y = cy - (v[1].y / fheight*2);
	v[2].y = cy - (v[2].y / fheight*2);
	v[3].y = cy - (v[3].y / fheight*2);

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	const GLPatch_t *hwrPatch = ((GLPatch_t *)gpatch->hardware);
	float s_min = 0.f, t_min = 0.f;
	float s_max = hwrPatch->max_s, t_max = hwrPatch->max_t;

	if (option & V_FLIP)
	{
		v[0].s = v[3].s = s_max;
		v[2].s = v[1].s = s_min;
	}
	else
	{
		v[0].s = v[3].s = s_min;
		v[2].s = v[1].s = s_max;
	}

	if (option & V_VFLIP)
	{
		v[0].t = v[1].t = t_min;
		v[2].t = v[3].t = t_max;
	}
	else
	{
		v[0].t = v[1].t = t_max;
		v[2].t = v[3].t = t_min;
	}

	FBITFIELD flags = PF_NoDepthTest;

	// clip it since it is used for bunny scroll in doom I
	UINT32 alphalevel = (option & V_ALPHAMASK) >> V_ALPHASHIFT;
	UINT32 blendmode = (option & V_BLENDMASK) >> V_BLENDSHIFT;
	if (blendmode)
		blendmode++; // realign to constants
	if (alphalevel || blendmode)
	{
		FSurfaceInfo Surf = {};
		Surf.PolyColor.s.red = Surf.PolyColor.s.green = Surf.PolyColor.s.blue = 0xff;

		flags |= HWR_GetBlendModeFlag(blendmode);

		if (alphalevel == 13)
			Surf.PolyColor.s.alpha = softwaretranstogl_lo[V_GetHUDTranslucency(option)];
		else if (alphalevel == 14)
			Surf.PolyColor.s.alpha = softwaretranstogl[V_GetHUDTranslucency(option)];
		else if (alphalevel == 15)
			Surf.PolyColor.s.alpha = softwaretranstogl_hi[V_GetHUDTranslucency(option)];
		else if (alphalevel < 10)
			Surf.PolyColor.s.alpha = softwaretranstogl[min(max((10 - alphalevel), 0), 10)];
		else
			Surf.PolyColor.s.alpha = 0;

		GL_DrawPolygon(&Surf, v, 4, flags|PF_Modulated);
	}
	else
		GL_DrawPolygon(NULL, v, 4, flags|PF_Translucent);
}

void HWR_DrawCroppedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t pscale, INT32 option, fixed_t sx, fixed_t sy, fixed_t w, fixed_t h)
{
	FOutVector v[4];
	FBITFIELD flags;
	float cx = FIXED_TO_FLOAT(x);
	float cy = FIXED_TO_FLOAT(y);
	UINT32 alphalevel, blendmode;
	GLPatch_t *hwrPatch;

//  3--2
//  | /|
//  |/ |
//  0--1
	float dup, fscale, fwidth, fheight;

	// make patch ready in hardware cache
	HWR_GetPatch(gpatch);
	hwrPatch = ((GLPatch_t *)gpatch->hardware);

	dup = (float)vid.dup;

	switch (option & V_SCALEPATCHMASK)
	{
		case V_NOSCALEPATCH:
			dup = 1.0f;
			break;
		case V_SMALLSCALEPATCH:
			dup = (float)vid.smalldup;
			break;
		case V_MEDSCALEPATCH:
			dup = (float)vid.meddup;
			break;
	}

	fscale = FIXED_TO_FLOAT(pscale);

	// fuck it, no GL support for croppedpatch V_SPLITSCREEN right now. it's not like it's accessible to Lua or anything, and we only use it for menus...

	cy -= (float)(gpatch->topoffset) * fscale;
	cx -= (float)(gpatch->leftoffset) * fscale;

	if (!(option & V_NOSCALESTART))
	{
		cx = cx * dup;
		cy = cy * dup;

		if (!(option & V_SCALEPATCHMASK))
		{
			// if it's meant to cover the whole screen, black out the rest
			// no the patch is cropped do not do this ever

			// centre screen
			if (fabsf((float)vid.width - (float)BASEVIDWIDTH * dup) > 1.0E-36f)
			{
				if (option & V_SNAPTORIGHT)
					cx += ((float)vid.width - ((float)BASEVIDWIDTH * dup));
				else if (!(option & V_SNAPTOLEFT))
					cx += ((float)vid.width - ((float)BASEVIDWIDTH * dup))/2;
			}
			if (fabsf((float)vid.height - (float)BASEVIDHEIGHT * dup) > 1.0E-36f)
			{
				if (option & V_SNAPTOBOTTOM)
					cy += ((float)vid.height - ((float)BASEVIDHEIGHT * dup));
				else if (!(option & V_SNAPTOTOP))
					cy += ((float)vid.height - ((float)BASEVIDHEIGHT * dup))/2;
			}
		}
	}

	fwidth = w;
	fheight = h;

	if (sx + w > gpatch->width)
		fwidth = gpatch->width - sx;

	if (sy + h > gpatch->height)
		fheight = gpatch->height - sy;

	if (pscale != FRACUNIT)
	{
		fwidth *=  fscale * dup;
		fheight *=  fscale * dup;
	}
	else
	{
		fwidth *= dup;
		fheight *= dup;
	}

	// positions of the cx, cy, are between 0 and vid.width/vid.height now, we need them to be between -1 and 1
	cx = -1 + (cx / (vid.width/2));
	cy = 1 - (cy / (vid.height/2));

	// fwidth and fheight are similar
	fwidth /= vid.width / 2;
	fheight /= vid.height / 2;

	// set the polygon vertices to the right positions
	v[0].x = v[3].x = cx;
	v[2].x = v[1].x = cx + fwidth;

	v[0].y = v[1].y = cy;
	v[2].y = v[3].y = cy - fheight;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = ((sx)/(float)(gpatch->width))*hwrPatch->max_s;
	if (sx + w > gpatch->width)
		v[2].s = v[1].s = hwrPatch->max_s;
	else
		v[2].s = v[1].s = ((sx+w)/(float)(gpatch->width))*hwrPatch->max_s;

	v[0].t = v[1].t = ((sy)/(float)(gpatch->height))*hwrPatch->max_t;
	if (sy + h > gpatch->height)
		v[2].t = v[3].t = hwrPatch->max_t;
	else
		v[2].t = v[3].t = ((sy+h)/(float)(gpatch->height))*hwrPatch->max_t;

	flags = PF_NoDepthTest;

	/*if (option & V_WRAPX)
		flags |= PF_ForceWrapX;
	if (option & V_WRAPY)
		flags |= PF_ForceWrapY;*/

	// clip it since it is used for bunny scroll in doom I
	if ((blendmode = ((option & V_BLENDMASK) >> V_BLENDSHIFT)))
		blendmode++; // realign to constants
	if ((alphalevel = ((option & V_ALPHAMASK) >> V_ALPHASHIFT)) || blendmode)
	{
		FSurfaceInfo Surf = {};
		Surf.PolyColor.s.red = Surf.PolyColor.s.green = Surf.PolyColor.s.blue = 0xff;

		flags |= HWR_GetBlendModeFlag(blendmode);

		if (alphalevel == 13)
			Surf.PolyColor.s.alpha = softwaretranstogl_lo[V_GetHUDTranslucency(option)];
		else if (alphalevel == 14)
			Surf.PolyColor.s.alpha = softwaretranstogl[V_GetHUDTranslucency(option)];
		else if (alphalevel == 15)
			Surf.PolyColor.s.alpha = softwaretranstogl_hi[V_GetHUDTranslucency(option)];
		else if (alphalevel < 10)
			Surf.PolyColor.s.alpha = softwaretranstogl[min(max((10 - alphalevel), 0), 10)];
		else
			Surf.PolyColor.s.alpha = 0;

		GL_DrawPolygon(&Surf, v, 4, flags|PF_Modulated);
	}
	else
		GL_DrawPolygon(NULL, v, 4, flags|PF_Translucent);
}

void HWR_DrawPic(INT32 x, INT32 y, lumpnum_t lumpnum)
{
	FOutVector      v[4];
	const patch_t    *patch;

	// make pic ready in hardware cache
	patch = HWR_GetPic(lumpnum);

//  3--2
//  | /|
//  |/ |
//  0--1

	v[0].x = v[3].x = 2.0f * (float)x/vid.width - 1;
	v[2].x = v[1].x = 2.0f * (float)(x + patch->width*FIXED_TO_FLOAT(vid.fdup))/vid.width - 1;
	v[0].y = v[1].y = 1.0f - 2.0f * (float)y/vid.height;
	v[2].y = v[3].y = 1.0f - 2.0f * (float)(y + patch->height*FIXED_TO_FLOAT(vid.fdup))/vid.height;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = 0;
	v[2].s = v[1].s = ((GLPatch_t *)patch->hardware)->max_s;
	v[0].t = v[1].t = 0;
	v[2].t = v[3].t = ((GLPatch_t *)patch->hardware)->max_t;


	//Hurdler: Boris, the same comment as above... but maybe for pics
	// it not a problem since they don't have any transparent pixel
	// if I'm right !?
	// But then, the question is: why not 0 instead of PF_Masked ?
	// or maybe PF_Environment ??? (like what I said above)
	// BP: PF_Environment don't change anything ! and 0 is undifined
	GL_DrawPolygon(NULL, v, 4, PF_Translucent | PF_NoDepthTest);
}

// ==========================================================================
//                                                            V_VIDEO.C STUFF
// ==========================================================================


// --------------------------------------------------------------------------
// Fills a box of pixels using a flat texture as a pattern
// Fixed to properly align like the other draw functions -luigi budd
// --------------------------------------------------------------------------
void HWR_DrawFlatFill(INT32 x, INT32 y, INT32 w, INT32 h, lumpnum_t flatlumpnum)
{
	const size_t len = W_LumpLength(flatlumpnum);

	size_t sflatsize;
	double dflatsize;
	INT32 flatflag;

	FOutVector v[4];

	sflatsize = R_FlatDimensionsFromLumpSize(len);
	dflatsize = (double)sflatsize;
	flatflag = sflatsize - 1;

//  3--2
//  | /|
//  |/ |
//  0--1

	v[0].x = v[3].x = (x - 160.0f)/160.0f;
	v[2].x = v[1].x = ((x+w) - 160.0f)/160.0f;
	v[0].y = v[1].y = -(y - 100.0f)/100.0f;
	v[2].y = v[3].y = -((y+h) - 100.0f)/100.0f;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	// flat is 64x64 lod and texture offsets are [0.0, 1.0]
	v[0].s = v[3].s = (float)((x & flatflag)/dflatsize);
	v[2].s = v[1].s = (float)(v[0].s + w/dflatsize);
	v[0].t = v[1].t = (float)((y & flatflag)/dflatsize);
	v[2].t = v[3].t = (float)(v[0].t + h/dflatsize);

	HWR_GetRawFlat(flatlumpnum, false);

	//Hurdler: Boris, the same comment as above... but maybe for pics
	// it not a problem since they don't have any transparent pixel
	// if I'm right !?
	// BTW, I see we put 0 for PFs, and If I'm right, that
	// means we take the previous PFs as default
	// how can we be sure they are ok?
	GL_DrawPolygon(NULL, v, 4, PF_NoDepthTest); //PF_Translucent);
}


// --------------------------------------------------------------------------
// Fade down the screen so that the menu drawn on top of it looks brighter
// --------------------------------------------------------------------------
//  3--2
//  | /|
//  |/ |
//  0--1
void HWR_FadeScreenMenuBack(UINT16 color, UINT8 strength)
{
	FOutVector  v[4];
	FSurfaceInfo Surf = {};

	v[0].x = v[3].x = -1.0f;
	v[2].x = v[1].x =  1.0f;
	v[0].y = v[1].y = -1.0f;
	v[2].y = v[3].y =  1.0f;
	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = 0.0f;
	v[2].s = v[1].s = 1.0f;
	v[0].t = v[1].t = 1.0f;
	v[2].t = v[3].t = 0.0f;

	if (color & 0xFF00) // Do COLORMAP fade.
	{
		if (HWR_ShouldUsePaletteRendering())
		{
			const hwdscreentexture_t scr_tex = HWD_SCREENTEXTURE_GENERIC2;

			Surf.LightTableId = HWR_GetLightTableID(NULL);
			Surf.LightInfo.light_level = strength;
			GL_MakeScreenTexture(scr_tex);
			GL_SetShader(HWR_GetShaderFromTarget(SHADER_UI_COLORMAP_FADE));
			GL_DrawScreenTexture(scr_tex, &Surf, PF_ColorMapped|PF_NoDepthTest);
			GL_UnSetShader();

			return;
		}
		Surf.PolyColor.rgba = UINT2RGBA(0x01010160);
		Surf.PolyColor.s.alpha = (strength*8);
	}
	else // Do TRANSMAP** fade.
	{
		RGBA_t *palette = HWR_GetTexturePalette();
		Surf.PolyColor.rgba = palette[color&0xFF].rgba;
		Surf.PolyColor.s.alpha = softwaretranstogl[strength];
	}
	GL_DrawPolygon(&Surf, v, 4, PF_NoTexture|PF_Modulated|PF_Translucent|PF_NoDepthTest);
}

// -----------------+
// HWR_DrawFadeFill : draw flat coloured rectangle, with transparency
// -----------------+
void HWR_DrawFadeFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color, UINT16 actualcolor, UINT8 strength)
{
	FOutVector v[4];
	FSurfaceInfo Surf = {};
	float fx, fy, fw, fh;

//  3--2
//  | /|
//  |/ |
//  0--1

	fx = (float)x;
	fy = (float)y;
	fw = (float)w;
	fh = (float)h;

	if (!(color & V_NOSCALESTART))
	{
		float dup = (float)vid.dup;

		fx *= dup;
		fy *= dup;
		fw *= dup;
		fh *= dup;

		// adjustxy
	}

	if (fx >= vid.width || fy >= vid.height)
		return;
	if (fx < 0)
	{
		fw += fx;
		fx = 0;
	}
	if (fy < 0)
	{
		fh += fy;
		fy = 0;
	}

	if (fw <= 0 || fh <= 0)
		return;
	if (fx + fw > vid.width)
		fw = (float)vid.width - fx;
	if (fy + fh > vid.height)
		fh = (float)vid.height - fy;

	fx = -1 + fx / (vid.width / 2);
	fy = 1 - fy / (vid.height / 2);
	fw = fw / (vid.width / 2);
	fh = fh / (vid.height / 2);

	v[0].x = v[3].x = fx;
	v[2].x = v[1].x = fx + fw;
	v[0].y = v[1].y = fy;
	v[2].y = v[3].y = fy - fh;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = 0.0f;
	v[2].s = v[1].s = 1.0f;
	v[0].t = v[1].t = 0.0f;
	v[2].t = v[3].t = 1.0f;

	if (actualcolor & 0xFF00) // Do COLORMAP fade.
	{
		Surf.PolyColor.rgba = UINT2RGBA(0x01010160);
		Surf.PolyColor.s.alpha = (strength*8);
	}
	else // Do TRANSMAP** fade.
	{
		RGBA_t *palette = HWR_GetTexturePalette();
		Surf.PolyColor.rgba = palette[actualcolor&0xFF].rgba;
		Surf.PolyColor.s.alpha = softwaretranstogl[strength];
	}
	GL_DrawPolygon(&Surf, v, 4, PF_NoTexture|PF_Modulated|PF_Translucent|PF_NoDepthTest);
}

// Draw the console background with translucency support
void HWR_DrawConsoleBack(UINT32 color, INT32 height)
{
	FOutVector  v[4];
	FSurfaceInfo Surf = {};

	// setup some neat-o translucency effect
	if (!height) //cool hack 0 height is full height
		height = vid.height;

	v[0].x = v[3].x = -1.0f;
	v[2].x = v[1].x =  1.0f;
	v[0].y = v[1].y =  1.0f-((height<<1)/(float)vid.height);
	v[2].y = v[3].y =  1.0f;
	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = 0.0f;
	v[2].s = v[1].s = 1.0f;
	v[0].t = v[1].t = 1.0f;
	v[2].t = v[3].t = 0.0f;

	Surf.PolyColor.rgba = UINT2RGBA(color);
	Surf.PolyColor.s.alpha = 0x80;

	GL_DrawPolygon(&Surf, v, 4, PF_NoTexture|PF_Modulated|PF_Translucent|PF_NoDepthTest);
}

void HWR_EncoreInvertScreen(void)
{
	FOutVector v[4];
	FSurfaceInfo Surf = {};

	v[0].x = v[3].x = -1.0f;
	v[2].x = v[1].x =  1.0f;
	v[0].y = v[1].y = -1.0f;
	v[2].y = v[3].y =  1.0f;
	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = 0.0f;
	v[2].s = v[1].s = 1.0f;
	v[0].t = v[1].t = 1.0f;
	v[2].t = v[3].t = 0.0f;

	Surf.PolyColor.rgba = 0xFFFFFFFF;

	GL_DrawPolygon(&Surf, v, 4, PF_NoTexture|PF_NoDepthTest);
}

// Very similar to HWR_DrawConsoleBack, except we draw from the middle(-ish) of the screen to the bottom.
void HWR_DrawTutorialBack(UINT32 color, INT32 boxheight)
{
	FOutVector  v[4];
	FSurfaceInfo Surf = {};
	INT32 height;
	if (boxheight < 0)
		height = -boxheight;
	else
		height = (boxheight * 4) + (boxheight/2)*5; // 4 lines of space plus gaps between and some leeway

	// setup some neat-o translucency effect

	v[0].x = v[3].x = -1.0f;
	v[2].x = v[1].x =  1.0f;
	v[0].y = v[1].y =  -1.0f;
	v[2].y = v[3].y =  -1.0f+((height<<1)/(float)vid.height);
	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = 0.0f;
	v[2].s = v[1].s = 1.0f;
	v[0].t = v[1].t = 1.0f;
	v[2].t = v[3].t = 0.0f;

	Surf.PolyColor.rgba = UINT2RGBA(color);
	Surf.PolyColor.s.alpha = (color == 0 ? 0xC0 : 0x80); // make black darker, like software

	GL_DrawPolygon(&Surf, v, 4, PF_NoTexture|PF_Modulated|PF_Translucent|PF_NoDepthTest);
}

// ==========================================================================
//                                                     AM_MAP.C DRAWING STUFF
// ==========================================================================

// -----------------+
// HWR_drawAMline   : draw a line of the automap (the clipping is already done in automap code)
// Arg              : color is a RGB 888 value
// -----------------+
void HWR_drawAMline(const fline_t *fl, INT32 color)
{
	F2DCoord v1, v2;
	RGBA_t color_rgba;
	RGBA_t *palette = HWR_GetTexturePalette();

	color_rgba = palette[color&0xFF];

	v1.x = ((float)fl->a.x-(vid.width/2.0f))*(2.0f/vid.width);
	v1.y = ((float)fl->a.y-(vid.height/2.0f))*(2.0f/vid.height);

	v2.x = ((float)fl->b.x-(vid.width/2.0f))*(2.0f/vid.width);
	v2.y = ((float)fl->b.y-(vid.height/2.0f))*(2.0f/vid.height);

	GL_Draw2DLine(&v1, &v2, color_rgba);
}

// -----------------+
// HWR_DrawDiag     : draw flat coloured rectangle, with no texture
// -----------------+
void HWR_DrawDiag(INT32 x, INT32 y, INT32 wh, INT32 color)
{
	FOutVector v[4];
	FSurfaceInfo Surf = {};
	float fx, fy, fw, fh, fwait = 0;

	if (wh < 0)
		return; // consistency w/ software

//  3--2
//  | /|
//  |/ |
//  0--1

	fx = (float)x;
	fy = (float)y;
	fw = fh = (float)wh;

	if (!(color & V_NOSCALESTART))
	{
		float dup = (float)vid.dup;

		fx *= dup;
		fy *= dup;
		fw *= dup;
		fh *= dup;

		if (fabsf((float)vid.width - ((float)BASEVIDWIDTH * dup)) > 1.0E-36f)
		{
			if (color & V_SNAPTORIGHT)
				fx += ((float)vid.width - ((float)BASEVIDWIDTH * dup));
			else if (!(color & V_SNAPTOLEFT))
				fx += ((float)vid.width - ((float)BASEVIDWIDTH * dup)) / 2;
		}
		if (fabsf((float)vid.height - ((float)BASEVIDHEIGHT * dup)) > 1.0E-36f)
		{
			// same thing here
			if (color & V_SNAPTOBOTTOM)
				fy += ((float)vid.height - ((float)BASEVIDHEIGHT * dup));
			else if (!(color & V_SNAPTOTOP))
				fy += ((float)vid.height - ((float)BASEVIDHEIGHT * dup)) / 2;
		}
	}

	if (fx >= vid.width || fy >= vid.height)
		return;
	if (fx < 0)
	{
		fw += fx;
		fx = 0;
	}
	if (fy < 0)
	{
		fh += fy;
		fy = 0;
	}

	if (fw <= 0 || fh <= 0)
		return;
	if (fx + fw > vid.width)
	{
		fwait = fw - ((float)vid.width - fx);
		fw = (float)vid.width - fx;
	}
	if (fy + fh > vid.height)
		fh = (float)vid.height - fy;

	fx = -1 + fx / (vid.width / 2);
	fy = 1 - fy / (vid.height / 2);
	fw = fw / (vid.width / 2);
	fh = fh / (vid.height / 2);

	v[0].x = v[3].x = fx;
	v[2].x = v[1].x = fx + fw;
	v[0].y = v[1].y = fy;
	v[3].y = fy - fh;
	v[2].y = fy - fwait;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = 0.0f;
	v[2].s = v[1].s = 1.0f;
	v[0].t = v[1].t = 0.0f;
	v[2].t = v[3].t = 1.0f;

	Surf.PolyColor = V_GetColor(color);

	GL_DrawPolygon(&Surf, v, 4,
		PF_Modulated|PF_NoTexture|PF_NoDepthTest);
}

// -------------------+
// HWR_DrawConsoleFill     : draw flat coloured transparent rectangle because that's cool, and hw sucks less than sw for that.
// -------------------+
void HWR_DrawConsoleFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color, UINT32 actualcolor)
{
	FOutVector v[4];
	FSurfaceInfo Surf = {};
	float fx, fy, fw, fh;

//  3--2
//  | /|
//  |/ |
//  0--1

	fx = (float)x;
	fy = (float)y;
	fw = (float)w;
	fh = (float)h;

	if (!(color & V_NOSCALESTART))
	{
		float dup = (float)vid.dup;
		INT32 intx, inty;

		fx *= dup;
		fy *= dup;
		fw *= dup;
		fh *= dup;

		intx = (INT32)fx;
		inty = (INT32)fy;
		V_AdjustXYWithSnap(&intx, &inty, color, dup);
		fx = (float)intx;
		fy = (float)inty;
	}

	if (fx >= vid.width || fy >= vid.height)
		return;
	if (fx < 0)
	{
		fw += fx;
		fx = 0;
	}
	if (fy < 0)
	{
		fh += fy;
		fy = 0;
	}

	if (fw <= 0 || fh <= 0)
		return;
	if (fx + fw > vid.width)
		fw = (float)vid.width - fx;
	if (fy + fh > vid.height)
		fh = (float)vid.height - fy;

	fx = -1 + fx / (vid.width / 2);
	fy = 1 - fy / (vid.height / 2);
	fw = fw / (vid.width / 2);
	fh = fh / (vid.height / 2);

	v[0].x = v[3].x = fx;
	v[2].x = v[1].x = fx + fw;
	v[0].y = v[1].y = fy;
	v[2].y = v[3].y = fy - fh;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = 0.0f;
	v[2].s = v[1].s = 1.0f;
	v[0].t = v[1].t = 0.0f;
	v[2].t = v[3].t = 1.0f;

	Surf.PolyColor.rgba = UINT2RGBA(actualcolor);
	Surf.PolyColor.s.alpha = 0x80;

	GL_DrawPolygon(&Surf, v, 4, PF_NoTexture|PF_Modulated|PF_Translucent|PF_NoDepthTest);
}

// -----------------+
// HWR_DrawFill     : draw flat coloured rectangle, with no texture
// -----------------+
void HWR_DrawFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color)
{
	FOutVector v[4];
	FBITFIELD flags;
	FSurfaceInfo Surf = {};
	float fx, fy, fw, fh;
	UINT8 alphalevel = ((color & V_ALPHAMASK) >> V_ALPHASHIFT);
	UINT8 blendmode = ((color & V_BLENDMASK) >> V_BLENDSHIFT);
	RGBA_t *palette = HWR_GetTexturePalette();

//  3--2
//  | /|
//  |/ |
//  0--1

	fx = (float)x;
	fy = (float)y;
	fw = (float)w;
	fh = (float)h;

	if (!(color & V_NOSCALESTART))
	{
		if (x == 0 && y == 0 && w == BASEVIDWIDTH*FRACUNIT && h == BASEVIDHEIGHT*FRACUNIT)
		{
			RGBA_t rgbaColour = palette[color&0xFF];
			FRGBAFloat clearColour;
			clearColour.red = (float)rgbaColour.s.red / 255;
			clearColour.green = (float)rgbaColour.s.green / 255;
			clearColour.blue = (float)rgbaColour.s.blue / 255;
			clearColour.alpha = 1;
			GL_ClearBuffer(true, false, false, &clearColour);
			return;
		}
	}

	fx = -1 + fx / (vid.width / 2);
	fy = 1 - fy / (vid.height / 2);
	fw = fw / (vid.width / 2);
	fh = fh / (vid.height / 2);

	v[0].x = v[3].x = fx;
	v[2].x = v[1].x = fx + fw;
	v[0].y = v[1].y = fy;
	v[2].y = v[3].y = fy - fh;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].s = v[3].s = 0.0f;
	v[2].s = v[1].s = 1.0f;
	v[0].t = v[1].t = 0.0f;
	v[2].t = v[3].t = 1.0f;

	Surf.PolyColor = palette[color&0xFF];

	flags = HWR_GetBlendModeFlag(blendmode+1)|PF_Modulated|PF_NoDepthTest|PF_NoTexture;

	if (alphalevel)
	{
		if (alphalevel == 13) Surf.PolyColor.s.alpha = softwaretranstogl_lo[st_translucency]; // V_HUDTRANSHALF
		else if (alphalevel == 14) Surf.PolyColor.s.alpha = softwaretranstogl[st_translucency]; // V_HUDTRANS
		else if (alphalevel == 15) Surf.PolyColor.s.alpha = softwaretranstogl_hi[st_translucency]; // V_HUDTRANSDOUBLE
		else if (alphalevel < 10) Surf.PolyColor.s.alpha = softwaretranstogl[min(max((10 - alphalevel), 0), 10)];
		else Surf.PolyColor.s.alpha = 0;
	}

	GL_DrawPolygon(&Surf, v, 4,
		flags);
}

// -------------------+
// HWR_DrawFixedLine  : draw flat coloured rectangle, with no texture
// -------------------+
void HWR_DrawFixedLine(INT32 x1, INT32 y1, INT32 x2, INT32 y2, fixed_t w, INT32 color)
{
	FOutVector v1[4] = {0};
	FOutVector v2[4] = {0};
	FOutVector fuse[4] = {0};
	FBITFIELD flags;
	FSurfaceInfo Surf[3] = {0};
	float fx, fy, fx2, fy2, fw, fh;
	UINT8 alphalevel = ((color & V_ALPHAMASK) >> V_ALPHASHIFT);
	UINT8 blendmode = ((color & V_BLENDMASK) >> V_BLENDSHIFT);
	RGBA_t *palette = HWR_GetTexturePalette();

//     3-4
//    / \|
//   /   5
//  /   /
// 2   /
// |\ /
// 0-1
//
// Need to flip the triangles depending on the quadrant
// Q1 and Q3 are not flipped, but Q2 and Q4 are


	fx = ((float)x1);
	fy = (float)y1;
	fx2 = (float)x2;
	fy2 = (float)y2;

	fw = (float)w / 2.0f;
	fh = fw;
	
	// Get the angle between the two points to determine the quadrant
	UINT8 quadrant = (UINT8)((R_PointToAngleFloat2(x1, y1, x2, y2) >> 30));

	float sx = (((quadrant >= 1) && (quadrant < 3)) ? -1.0f : 1.0f);
	float sy = (((quadrant >> 1) & 1) ? -1.0f : 1.0f);

	fx = -1 + fx / (vid.width / 2);
	fy = 1 - fy / (vid.height / 2);
	fx2 = -1 + fx2 / (vid.width / 2);
	fy2 = 1 - fy2 / (vid.height / 2);
	fw = fw / (vid.width / 2);
	fh = fh / (vid.height / 2);

	v1[0].x = v1[2].x = fx - (fw * sx);
	v1[1].x = fx + (fw * sx);
	v1[0].y = v1[1].y = fy + (fh * sy);
	v1[2].y = fy - (fh * sy);

	v2[0].x = fx2 - (fw * sx);
	v2[1].x = v2[2].x = fx2 + (fw * sx);
	v2[0].y = v2[1].y = fy2 - (fh * sy);
	v2[2].y = fy2 + (fh * sy);

	fuse[0].x = v1[2].x;
	fuse[0].y = v1[2].y;

	fuse[1].x = v1[1].x;
	fuse[1].y = v1[1].y;

	fuse[2].x = v2[2].x;
	fuse[2].y = v2[2].y;

	fuse[3].x = v2[0].x;
	fuse[3].y = v2[0].y;

	v1[0].z = v1[1].z = v1[2].z = v1[3].z = 1.0f;
	v2[0].z = v2[1].z = v2[2].z = v2[3].z = 1.0f;
	fuse[0].z = fuse[1].z = fuse[2].z = fuse[3].z = 1.0f;

	v1[0].s = 0.0f;
	v1[2].s = v1[1].s = 1.0f;
	v1[0].t = v1[1].t = 0.0f;
	v1[2].t = 1.0f;

	v2[0].s = 0.0f;
	v2[2].s = v2[1].s = 1.0f;
	v2[0].t = v2[1].t = 0.0f;
	v2[2].t = 1.0f;

	fuse[0].s = fuse[3].s = 0.0f;
	fuse[2].s = fuse[1].s = 1.0f;
	fuse[0].t = fuse[1].t = 0.0f;
	fuse[2].t = fuse[3].t = 1.0f;

	Surf[0].PolyColor = palette[color&0xFF];
	Surf[1].PolyColor = palette[color&0xFF];
	Surf[2].PolyColor = palette[color&0xFF];

	flags = HWR_GetBlendModeFlag(blendmode+1)|PF_Modulated|PF_NoDepthTest|PF_NoTexture;

	if (alphalevel)
	{
		if (alphalevel == 13)
		{
			// V_HUDTRANSHALF
			Surf[0].PolyColor.s.alpha = softwaretranstogl_lo[st_translucency];
			Surf[1].PolyColor.s.alpha = softwaretranstogl_lo[st_translucency];
			Surf[2].PolyColor.s.alpha = softwaretranstogl_lo[st_translucency];
		}
		else if (alphalevel == 14)
		{
			// V_HUDTRANS
			Surf[0].PolyColor.s.alpha = softwaretranstogl[st_translucency];
			Surf[1].PolyColor.s.alpha = softwaretranstogl[st_translucency];
			Surf[2].PolyColor.s.alpha = softwaretranstogl[st_translucency];
		}
		else if (alphalevel == 15)
		{
			// V_HUDTRANSDOUBLE
			Surf[0].PolyColor.s.alpha = softwaretranstogl_hi[st_translucency];
			Surf[1].PolyColor.s.alpha = softwaretranstogl_hi[st_translucency];
			Surf[2].PolyColor.s.alpha = softwaretranstogl_hi[st_translucency];
		}
		else if (alphalevel < 10)
		{
			Surf[0].PolyColor.s.alpha = softwaretranstogl[min(max((10 - alphalevel), 0), 10)];
			Surf[1].PolyColor.s.alpha = softwaretranstogl[min(max((10 - alphalevel), 0), 10)];
			Surf[2].PolyColor.s.alpha = softwaretranstogl[min(max((10 - alphalevel), 0), 10)];
		}
		else
		{
			Surf[0].PolyColor.s.alpha = 0;
			Surf[1].PolyColor.s.alpha = 0;
			Surf[2].PolyColor.s.alpha = 0;
		}
	}

	GL_DrawPolygon(&Surf[0], v1, 3, flags);
	GL_DrawPolygon(&Surf[1], v2, 3, flags);
	GL_DrawPolygon(&Surf[2], fuse, 4, flags);
}

#ifdef HAVE_PNG

#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#ifndef _LFS64_LARGEFILE
#define _LFS64_LARGEFILE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 0
#endif

 #include "png.h"
 #ifdef PNG_WRITE_SUPPORTED
  #define USE_PNG // PNG is only used if write is supported (see ../m_misc.c)
 #endif
#endif

#ifndef USE_PNG
// --------------------------------------------------------------------------
// save screenshots with TGA format
// --------------------------------------------------------------------------
static inline dboolean saveTGA(const char *file_name, void *buffer,
	INT32 width, INT32 height)
{
	INT32 fd;
	TGAHeader tga_hdr;
	INT32 i;
	UINT8 *buf8 = buffer;

	fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return false;

	memset(&tga_hdr, 0, sizeof (tga_hdr));
	tga_hdr.width = LSBF_SHORT(width);
	tga_hdr.height = LSBF_SHORT(height);
	tga_hdr.image_pix_size = 24;
	tga_hdr.image_type = 2;
	tga_hdr.image_descriptor = 32;

	if ( -1 == write(fd, &tga_hdr, sizeof (TGAHeader)))
	{
		close(fd);
		return false;
	}
	// format to 888 BGR
	for (i = 0; i < width * height * 3; i+=3)
	{
		const UINT8 temp = buf8[i];
		buf8[i] = buf8[i+2];
		buf8[i+2] = temp;
	}
	if ( -1 == write(fd, buffer, width * height * 3))
	{
		close(fd);
		return false;
	}
	close(fd);
	return true;
}
#endif

// --------------------------------------------------------------------------
// screen shot
// --------------------------------------------------------------------------

UINT8 *HWR_GetScreenshot(INT32 scale)
{
	static UINT8 *buf = NULL;
	int tex = HWR_ShouldUsePaletteRendering() ? HWD_SCREENTEXTURE_GENERIC3 : HWD_SCREENTEXTURE_GENERIC2;

	buf = realloc(buf, (vid.width/scale)*(vid.height/scale)*3);

	if (!buf)
		return NULL;

	// returns 24bit 888 RGB
	GL_ReadScreenTexture(tex, (void *)buf, scale);
	return buf;
}

#endif //HWRENDER
