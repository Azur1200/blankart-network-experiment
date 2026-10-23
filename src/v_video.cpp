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
/// \file  v_video.c
/// \brief Gamma correction LUT stuff
///        Functions to draw patches (by post) directly to screen.
///        Functions to blit a block to the screen.

#include "doomdef.h"
#include "doomtype.h"
#include "d_main.h"
#include "r_local.h"
#include "p_local.h" // stplyr
#include "g_game.h" // players
#include "v_video.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "f_finale.h"
#include "r_draw.h"
#include "console.h"

#include "i_video.h" // rendermode
#include "z_zone.h"
#include "m_misc.h"
#include "m_random.h"
#include "doomstat.h"
#include "r_fps.h"
#include "qs22j.h"

#ifdef HWRENDER
#include "hardware/hw_glob.h"
#endif

// SRB2Kart
#include "k_hud.h"
#include "k_boss.h"
#include "i_time.h"
#include "k_dialogue.h" // K_GetDialogueSlide
#include "v_draw.hpp"

static CV_PossibleValue_t fps_cons_t[] = {{0, "No"}, {1, "Normal"}, {2, "Compact"}, {3, "Old"}, {4, "Old Compact"}, {0, NULL}};
consvar_t cv_ticrate = CVAR_INIT ("showfps", "No", CV_SAVE, fps_cons_t, NULL);

static void CV_palette_OnChange(void);

#ifdef BACKWARDSCOMPATCORRECTION
static CV_PossibleValue_t gamma_cons_t[] = {{-15, "MIN"}, {5, "MAX"}, {0, NULL}};
consvar_t cv_globalgamma = CVAR_INIT ("gamma", "0", CV_SAVE|CV_CALL|CV_NOINIT, gamma_cons_t, CV_palette_OnChange);
#endif

static CV_PossibleValue_t brightness_cons_t[] = {{-15, "MIN"}, {5, "MAX"}, {0, NULL}};
consvar_t cv_globalbrightness = CVAR_INIT ("brightness", "0", CV_SAVE|CV_CALL, brightness_cons_t, CV_palette_OnChange);

static CV_PossibleValue_t saturation_cons_t[] = {{0, "MIN"}, {10, "MAX"}, {0, NULL}};
consvar_t cv_globalsaturation = CVAR_INIT ("saturation", "10", CV_SAVE|CV_CALL|CV_NOINIT, saturation_cons_t, CV_palette_OnChange);

#define huecoloursteps 4

static CV_PossibleValue_t hue_cons_t[] = {{0, "MIN"}, {(huecoloursteps*6)-1, "MAX"}, {0, NULL}};
consvar_t cv_rhue = CVAR_INIT ("rhue",  "0", CV_SAVE|CV_CALL|CV_NOINIT, hue_cons_t, CV_palette_OnChange);
consvar_t cv_yhue = CVAR_INIT ("yhue",  "4", CV_SAVE|CV_CALL|CV_NOINIT, hue_cons_t, CV_palette_OnChange);
consvar_t cv_ghue = CVAR_INIT ("ghue",  "8", CV_SAVE|CV_CALL|CV_NOINIT, hue_cons_t, CV_palette_OnChange);
consvar_t cv_chue = CVAR_INIT ("chue", "12", CV_SAVE|CV_CALL|CV_NOINIT, hue_cons_t, CV_palette_OnChange);
consvar_t cv_bhue = CVAR_INIT ("bhue", "16", CV_SAVE|CV_CALL|CV_NOINIT, hue_cons_t, CV_palette_OnChange);
consvar_t cv_mhue = CVAR_INIT ("mhue", "20", CV_SAVE|CV_CALL|CV_NOINIT, hue_cons_t, CV_palette_OnChange);

consvar_t cv_rbrightness = CVAR_INIT ("rbrightness", "0", CV_SAVE|CV_CALL|CV_NOINIT, brightness_cons_t, CV_palette_OnChange);
consvar_t cv_ybrightness = CVAR_INIT ("ybrightness", "0", CV_SAVE|CV_CALL|CV_NOINIT, brightness_cons_t, CV_palette_OnChange);
consvar_t cv_gbrightness = CVAR_INIT ("gbrightness", "0", CV_SAVE|CV_CALL|CV_NOINIT, brightness_cons_t, CV_palette_OnChange);
consvar_t cv_cbrightness = CVAR_INIT ("cbrightness", "0", CV_SAVE|CV_CALL|CV_NOINIT, brightness_cons_t, CV_palette_OnChange);
consvar_t cv_bbrightness = CVAR_INIT ("bbrightness", "0", CV_SAVE|CV_CALL|CV_NOINIT, brightness_cons_t, CV_palette_OnChange);
consvar_t cv_mbrightness = CVAR_INIT ("mbrightness", "0", CV_SAVE|CV_CALL|CV_NOINIT, brightness_cons_t, CV_palette_OnChange);

consvar_t cv_rsaturation = CVAR_INIT ("rsaturation", "10", CV_SAVE|CV_CALL|CV_NOINIT, saturation_cons_t, CV_palette_OnChange);
consvar_t cv_ysaturation = CVAR_INIT ("ysaturation", "10", CV_SAVE|CV_CALL|CV_NOINIT, saturation_cons_t, CV_palette_OnChange);
consvar_t cv_gsaturation = CVAR_INIT ("gsaturation", "10", CV_SAVE|CV_CALL|CV_NOINIT, saturation_cons_t, CV_palette_OnChange);
consvar_t cv_csaturation = CVAR_INIT ("csaturation", "10", CV_SAVE|CV_CALL|CV_NOINIT, saturation_cons_t, CV_palette_OnChange);
consvar_t cv_bsaturation = CVAR_INIT ("bsaturation", "10", CV_SAVE|CV_CALL|CV_NOINIT, saturation_cons_t, CV_palette_OnChange);
consvar_t cv_msaturation = CVAR_INIT ("msaturation", "10", CV_SAVE|CV_CALL|CV_NOINIT, saturation_cons_t, CV_palette_OnChange);

static CV_PossibleValue_t constextsize_cons_t[] = {
	{V_NOSCALEPATCH, "Small"}, {V_SMALLSCALEPATCH, "Medium"}, {V_MEDSCALEPATCH, "Large"}, {0, "Huge"},
	{0, NULL}};
static void CV_constextsize_OnChange(void);
consvar_t cv_constextsize = CVAR_INIT ("con_textsize", "Medium", CV_SAVE|CV_CALL, constextsize_cons_t, CV_constextsize_OnChange);

consvar_t cv_palette = CVAR_INIT ("palette", "", CV_CHEAT|CV_CALL|CV_NOINIT, NULL, CV_palette_OnChange);
consvar_t cv_palettenum = CVAR_INIT ("palettenum", "0", CV_CHEAT|CV_CALL|CV_NOINIT, CV_Unsigned, CV_palette_OnChange);

// local copy of the palette for V_GetColor()
RGBA_t *pLocalPalette = NULL;
RGBA_t *pMasterPalette = NULL;
RGBA_t *pGammaCorrectedPalette = NULL;

static size_t currentPaletteSize;

/*
The following was an extremely helpful resource when developing my Colour Cube LUT.
http://http.developer.nvidia.com/GPUGems2/gpugems2_chapter24.html
Please check it out if you're trying to maintain this.
toast 18/04/17
*/

float Cubepal[2][2][2][3];
dboolean Cubeapply = false;

// returns whether to apply cube, selectively avoiding expensive operations
static dboolean InitCube(void)
{
	dboolean apply = false;
	uint8_t q;
	float working[2][2][2][3] = // the initial positions of the corners of the colour cube!
	{
		{
			{
				{0.0, 0.0, 0.0}, // black corner
				{0.0, 0.0, 1.0}  // blue corner
			},
			{
				{0.0, 1.0, 0.0}, // green corner
				{0.0, 1.0, 1.0}  // cyan corner
			}
		},
		{
			{
				{1.0, 0.0, 0.0}, // red corner
				{1.0, 0.0, 1.0}  // magenta corner
			},
			{
				{1.0, 1.0, 0.0}, // yellow corner
				{1.0, 1.0, 1.0}  // white corner
			}
		}
	};
	float desatur[3]; // grey
	float globalgammamul, globalgammaoffs;
	dboolean doinggamma;

	if (loaded_config == false)
		return false;

#define diffcons(cv) (cv.value != atoi(cv.defaultvalue))

	doinggamma = diffcons(cv_globalbrightness);

#define gammascale 8
	globalgammamul = (cv_globalbrightness.value ? ((255.0f - (gammascale*abs(cv_globalbrightness.value))) / 255.0f) : 1.0f);
	globalgammaoffs = ((cv_globalbrightness.value > 0) ? ((gammascale*cv_globalbrightness.value) / 255.0f) : 0.0f);
	desatur[0] = desatur[1] = desatur[2] = globalgammaoffs + (0.33*globalgammamul);

	if (doinggamma
		|| diffcons(cv_rhue)
		|| diffcons(cv_yhue)
		|| diffcons(cv_ghue)
		|| diffcons(cv_chue)
		|| diffcons(cv_bhue)
		|| diffcons(cv_mhue)
		|| diffcons(cv_rbrightness)
		|| diffcons(cv_ybrightness)
		|| diffcons(cv_gbrightness)
		|| diffcons(cv_cbrightness)
		|| diffcons(cv_bbrightness)
		|| diffcons(cv_mbrightness)) // set the gamma'd/hued positions (saturation is done later)
	{
		float mod, tempgammamul, tempgammaoffs;

		apply = true;

		working[0][0][0][0] = working[0][0][0][1] = working[0][0][0][2] = globalgammaoffs;
		working[1][1][1][0] = working[1][1][1][1] = working[1][1][1][2] = globalgammaoffs+globalgammamul;

#define dohue(hue, gamma, loc) \
		tempgammamul = (gamma ? ((255 - (gammascale*abs(gamma)))/255.0)*globalgammamul : globalgammamul);\
		tempgammaoffs = ((gamma > 0) ? ((gammascale*gamma)/255.0) + globalgammaoffs : globalgammaoffs);\
		mod = ((hue % huecoloursteps)*(tempgammamul)/huecoloursteps);\
		switch (hue/huecoloursteps)\
		{\
			case 0:\
			default:\
				loc[0] = tempgammaoffs+tempgammamul;\
				loc[1] = tempgammaoffs+mod;\
				loc[2] = tempgammaoffs;\
				break;\
			case 1:\
				loc[0] = tempgammaoffs+tempgammamul-mod;\
				loc[1] = tempgammaoffs+tempgammamul;\
				loc[2] = tempgammaoffs;\
				break;\
			case 2:\
				loc[0] = tempgammaoffs;\
				loc[1] = tempgammaoffs+tempgammamul;\
				loc[2] = tempgammaoffs+mod;\
				break;\
			case 3:\
				loc[0] = tempgammaoffs;\
				loc[1] = tempgammaoffs+tempgammamul-mod;\
				loc[2] = tempgammaoffs+tempgammamul;\
				break;\
			case 4:\
				loc[0] = tempgammaoffs+mod;\
				loc[1] = tempgammaoffs;\
				loc[2] = tempgammaoffs+tempgammamul;\
				break;\
			case 5:\
				loc[0] = tempgammaoffs+tempgammamul;\
				loc[1] = tempgammaoffs;\
				loc[2] = tempgammaoffs+tempgammamul-mod;\
				break;\
		}
		dohue(cv_rhue.value, cv_rbrightness.value, working[1][0][0]);
		dohue(cv_yhue.value, cv_ybrightness.value, working[1][1][0]);
		dohue(cv_ghue.value, cv_gbrightness.value, working[0][1][0]);
		dohue(cv_chue.value, cv_cbrightness.value, working[0][1][1]);
		dohue(cv_bhue.value, cv_bbrightness.value, working[0][0][1]);
		dohue(cv_mhue.value, cv_mbrightness.value, working[1][0][1]);
#undef dohue
	}

#define dosaturation(a, e) a = ((1 - work)*e + work*a)
#define docvsat(cv_sat, hue, gamma, r, g, b) \
	if diffcons(cv_sat)\
	{\
		float work, mod, tempgammamul, tempgammaoffs;\
		apply = true;\
		work = (cv_sat.value/10.0);\
		mod = ((hue % huecoloursteps)*(1.0)/huecoloursteps);\
		if (hue & huecoloursteps)\
			mod = 2-mod;\
		else\
			mod += 1;\
		tempgammamul = (gamma ? ((255 - (gammascale*abs(gamma)))/255.0)*globalgammamul : globalgammamul);\
		tempgammaoffs = ((gamma > 0) ? ((gammascale*gamma)/255.0) + globalgammaoffs : globalgammaoffs);\
		for (q = 0; q < 3; q++)\
			dosaturation(working[r][g][b][q], (tempgammaoffs+(desatur[q]*mod*tempgammamul)));\
	}

	docvsat(cv_rsaturation, cv_rhue.value, cv_rbrightness.value, 1, 0, 0);
	docvsat(cv_ysaturation, cv_yhue.value, cv_ybrightness.value, 1, 1, 0);
	docvsat(cv_gsaturation, cv_ghue.value, cv_gbrightness.value, 0, 1, 0);
	docvsat(cv_csaturation, cv_chue.value, cv_cbrightness.value, 0, 1, 1);
	docvsat(cv_bsaturation, cv_bhue.value, cv_bbrightness.value, 0, 0, 1);
	docvsat(cv_msaturation, cv_mhue.value, cv_mbrightness.value, 1, 0, 1);

#undef gammascale

	if diffcons(cv_globalsaturation)
	{
		float work = (cv_globalsaturation.value/10.0);

		apply = true;

		for (q = 0; q < 3; q++)
		{
			dosaturation(working[1][0][0][q], desatur[q]);
			dosaturation(working[0][1][0][q], desatur[q]);
			dosaturation(working[0][0][1][q], desatur[q]);

			dosaturation(working[1][1][0][q], 2*desatur[q]);
			dosaturation(working[0][1][1][q], 2*desatur[q]);
			dosaturation(working[1][0][1][q], 2*desatur[q]);
		}
	}

#undef dosaturation

#undef diffcons

	if (!apply)
		return false;

#define dowork(i, j, k, l) \
	if (working[i][j][k][l] > 1.0)\
		working[i][j][k][l] = 1.0;\
	else if (working[i][j][k][l] < 0.0)\
		working[i][j][k][l] = 0.0;\
	Cubepal[i][j][k][l] = working[i][j][k][l]
	for (q = 0; q < 3; q++)
	{
		dowork(0, 0, 0, q);
		dowork(1, 0, 0, q);
		dowork(0, 1, 0, q);
		dowork(1, 1, 0, q);
		dowork(0, 0, 1, q);
		dowork(1, 0, 1, q);
		dowork(0, 1, 1, q);
		dowork(1, 1, 1, q);
	}
#undef dowork

	return true;
}

uint32_t V_GammaCorrect(uint32_t input, double power)
{
	RGBA_t result;
	double linear;

	result.rgba = input;

	linear = ((double)result.s.red)/255.0f;
	linear = pow(linear, power)*255.0f;
	result.s.red = (uint8_t)(linear);
	linear = ((double)result.s.green)/255.0f;
	linear = pow(linear, power)*255.0f;
	result.s.green = (uint8_t)(linear);
	linear = ((double)result.s.blue)/255.0f;
	linear = pow(linear, power)*255.0f;
	result.s.blue = (uint8_t)(linear);

	return result.rgba;
}

// keep a copy of the palette so that we can get the RGB value for a color index at any time.
static void LoadPalette(const char *lumpname)
{
	lumpnum_t lumpnum = W_GetNumForName(lumpname);
	size_t i, palsize;
	uint8_t *pal;

	currentPaletteSize = W_LumpLength(lumpnum);
	palsize = currentPaletteSize / 3;

	Cubeapply = InitCube();

	if (pLocalPalette != pMasterPalette)
		Z_Free(pLocalPalette);
	Z_Free(pMasterPalette);
	Z_Free(pGammaCorrectedPalette);

	pMasterPalette = static_cast<RGBA_t *>(Z_Malloc(sizeof (*pMasterPalette)*palsize, PU_STATIC, NULL));
	if (Cubeapply)
		pLocalPalette = static_cast<RGBA_t *>(Z_Malloc(sizeof (*pLocalPalette)*palsize, PU_STATIC, NULL));
	else
		pLocalPalette = pMasterPalette;
	pGammaCorrectedPalette = static_cast<RGBA_t *>(Z_Malloc(sizeof (*pGammaCorrectedPalette)*palsize, PU_STATIC, NULL));

	pal = static_cast<uint8_t *>(W_CacheLumpNum(lumpnum, PU_CACHE));
	for (i = 0; i < palsize; i++)
	{
		pMasterPalette[i].s.red = *pal++;
		pMasterPalette[i].s.green = *pal++;
		pMasterPalette[i].s.blue = *pal++;
		pMasterPalette[i].s.alpha = 0xFF;
	}

	// remap the palette from 2.1 to 2.2 indices in compatmode
	RGBA_t *palcopy = NULL;
	if (wadfiles[WADFILENUM(lumpnum)]->compatmode)
	{
		palcopy = static_cast<RGBA_t *>(malloc(sizeof(*palcopy)*palsize));

		if (palcopy == NULL)
			I_Error("LoadPalette: Out of memory!\n");

		memcpy(palcopy, pMasterPalette, sizeof(*palcopy)*palsize);
	}

	for (i = 0; i < palsize; i++)
	{
		if (palcopy)
			pMasterPalette[i].rgba = palcopy[R_InvPaletteRemap(i)].rgba;
		pGammaCorrectedPalette[i].rgba = V_GammaDecode(pMasterPalette[i].rgba);

		if (!Cubeapply)
			continue;

		// Short hand this so its easier to type
		RGBA_t pGCP = pGammaCorrectedPalette[i];

		V_CubeApply(&pGCP);
		pLocalPalette[i].rgba = V_GammaEncode(pGCP.rgba);
	}

	if (palcopy)
		free(palcopy);
}

void V_CubeApply(RGBA_t *input)
{
	float working[4][3];
	float linear;
	uint8_t q;

	if (!Cubeapply)
		return;

	linear = ((*input).s.red/255.0);
#define dolerp(e1, e2) ((1 - linear)*e1 + linear*e2)
	for (q = 0; q < 3; q++)
	{
		working[0][q] = dolerp(Cubepal[0][0][0][q], Cubepal[1][0][0][q]);
		working[1][q] = dolerp(Cubepal[0][1][0][q], Cubepal[1][1][0][q]);
		working[2][q] = dolerp(Cubepal[0][0][1][q], Cubepal[1][0][1][q]);
		working[3][q] = dolerp(Cubepal[0][1][1][q], Cubepal[1][1][1][q]);
	}

	linear = ((*input).s.green/255.0);
	for (q = 0; q < 3; q++)
	{
		working[0][q] = dolerp(working[0][q], working[1][q]);
		working[1][q] = dolerp(working[2][q], working[3][q]);
	}

	linear = ((*input).s.blue/255.0);
	for (q = 0; q < 3; q++)
	{
		working[0][q] = 255*dolerp(working[0][q], working[1][q]);
		if (working[0][q] > 255.0f)
			working[0][q] = 255.0f;
		else if (working[0][q] < 0.0f)
			working[0][q] = 0.0;
	}
#undef dolerp

	(*input).s.red = (uint8_t)(working[0][0]);
	(*input).s.green = (uint8_t)(working[0][1]);
	(*input).s.blue = (uint8_t)(working[0][2]);
}

const char *R_GetPalname(uint16_t num)
{
	static char palname[9];
	char newpal[9] = "PLAYPAL";

	if (num > 0 && num <= 10000)
		snprintf(newpal, 8, "PAL%04u", num-1);

	strncpy(palname, newpal, 8);
	return palname;
}

const char *GetPalette(void)
{
	const char *user = cv_palette.string;

	if (user && user[0])
	{
		if (W_CheckNumForName(user) == LUMPERROR)
		{
			CONS_Alert(CONS_WARNING,
					"cv_palette %s lump does not exist\n", user);
		}
		else
		{
			return cv_palette.string;
		}
	}

	if (gamestate == GS_LEVEL)
		return R_GetPalname((encoremode ? mapheaderinfo[gamemap-1]->encorepal : mapheaderinfo[gamemap-1]->palette));

	return "PLAYPAL";
}

void V_ReloadPalette(void)
{
	LoadPalette(GetPalette());
}

// -------------+
// V_SetPalette : Set the current palette to use for palettized graphics
//              :
// -------------+
void V_SetPalette(int32_t palettenum)
{
	if (!pLocalPalette)
		V_ReloadPalette();

	if (palettenum == 0)
	{
		palettenum = cv_palettenum.value;

		if (palettenum * 256U > currentPaletteSize - 256)
		{
			CONS_Alert(CONS_WARNING,
					"cv_palettenum %d out of range\n",
					palettenum);
			palettenum = 0;
		}
	}

#ifdef HWRENDER
	if (rendermode == render_opengl)
		HWR_SetPalette(&pLocalPalette[palettenum*256]);
#if (defined (__unix__) && !defined (MSDOS)) || defined (UNIXCOMMON) || defined (HAVE_SDL)
	else
#endif
#endif
	if (rendermode != render_none)
		I_SetPalette(&pLocalPalette[palettenum*256]);
}

void V_SetPaletteLump(const char *pal)
{
	LoadPalette(pal);
	V_SetPalette(0);
}

static void CV_palette_OnChange(void)
{
	if (loaded_config == false)
		return;
	// recalculate Color Cube
	V_ReloadPalette();
	V_SetPalette(0);
}

static void CV_constextsize_OnChange(void)
{
	if (!con_startup)
		con_recalc = true;
}


// --------------------------------------------------------------------------
// Copy a rectangular area from one bitmap to another (8bpp)
// --------------------------------------------------------------------------
void VID_BlitLinearScreen(const uint8_t *restrict srcptr, uint8_t *restrict destptr, int32_t width, int32_t height, size_t srcrowbytes, size_t destrowbytes)
{
	if (srcrowbytes == destrowbytes && srcrowbytes == (size_t)width)
	{
		size_t i = srcrowbytes * height;
#if defined(__SSE__)
		while (i >= 16)
		{
			// TODO: find where the buffer is misaligned at times and align it
			_mm_storeu_ps((float *)destptr, _mm_loadu_ps((const float *)srcptr));
			srcptr += 16;
			destptr += 16;
			i -= 16;
		}
#endif
		memcpy(destptr, srcptr, i);
	}
	else
	{
		while (height--)
		{
			memcpy(destptr, srcptr, width);

			destptr += destrowbytes;
			srcptr += srcrowbytes;
		}
	}
}

// Returns the palette index representing the specific skin color.
uint8_t V_GetColorFromSkinColorEx(skincolornum_t skincolor, uint8_t idx)
{
	if (dedicated)
	{
		return TRANSPARENTPIXEL; // Fuck off
	}

	idx = std::min<uint8_t>(idx, 15); // Do not EVER let this go out of bounds!

	if (skincolor >= numskincolors)
	{
		return TRANSPARENTPIXEL; // Out of range, don't bother
	}

	skincolor_t *color = &skincolors[skincolor];

	return color->ramp[idx];
}

static colorlookup_t colormix_lut;

// Returns the palette index representing the specific skin color, plus a brightness value that can
// dim the given color.
uint8_t V_GetColorFromSkinColorWithBrightness(skincolornum_t skincolor, uint8_t idx, fixed_t brightness)
{
	if (dedicated)
	{
		return TRANSPARENTPIXEL; // Fuck off
	}

	if (brightness == FRACUNIT)
	{
		// At full brightness, so no need for complicated color dimming
		return V_GetColorFromSkinColorEx(skincolor, idx);
	}

	idx = std::min<uint8_t>(idx, 15); // Do not EVER let this go out of bounds!

	brightness = CLAMP(brightness, 0, FRACUNIT);

	if (skincolor >= numskincolors)
	{
		return TRANSPARENTPIXEL; // Out of range, don't bother
	}

	skincolor_t *color = &skincolors[skincolor];

	// Get the color from our skincolor
	RGBA_t my_color = V_GetMasterColor(color->ramp[idx]);

	// Dim the colors
	my_color.s.red = FixedMul(my_color.s.red, brightness);
	my_color.s.green = FixedMul(my_color.s.green, brightness);
	my_color.s.blue = FixedMul(my_color.s.blue, brightness);

	// Get the finalized color value
	InitColorLUT(&colormix_lut, pMasterPalette, false);
	uint8_t my_pal_index = GetColorLUT(&colormix_lut, my_color.s.red, my_color.s.green, my_color.s.blue);

	return my_pal_index;
}

void V_AdjustXYWithSnap(int32_t *x, int32_t *y, uint32_t options, int32_t dup)
{
	// dup adjustments pretend that screen width is BASEVIDWIDTH * dup
	int32_t screenwidth = vid.width;
	int32_t screenheight = vid.height;
	int32_t basewidth = BASEVIDWIDTH * dup;
	int32_t baseheight = BASEVIDHEIGHT * dup;
	int8_t player = -1;
	uint8_t i;

	if (r_splitscreen > 0)
	{
		if (options & V_SPLITSCREEN)
		{
			screenheight /= 2;
			baseheight /= 2;

			if (r_splitscreen > 1)
			{
				screenwidth /= 2;
				basewidth /= 2;
			}
		}
	}
	else if ((options & (V_SLIDEIN|V_SNAPTOBOTTOM)) == (V_SLIDEIN|V_SNAPTOBOTTOM))
	{
		int32_t slide = K_GetDialogueSlide(51 * FRACUNIT);
		if (slide)
		{
			*y -= FixedMul(slide, dup);
		}
	}

	for (i = 0; i <= r_splitscreen; i++)
	{
		if (stplyr == &players[displayplayers[i]])
		{
			player = i;
			break;
		}
	}

	if (vid.width != (BASEVIDWIDTH * dup))
	{
		if (options & V_SNAPTORIGHT)
			*x += (screenwidth - basewidth);
		else if (!(options & V_SNAPTOLEFT))
			*x += (screenwidth - basewidth) / 2;
	}

	if (vid.height != (BASEVIDHEIGHT * dup))
	{
		if (options & V_SNAPTOBOTTOM)
			*y += (screenheight - baseheight);
		else if (!(options & V_SNAPTOTOP))
			*y += (screenheight - baseheight) / 2;
	}

	if (options & V_SPLITSCREEN)
	{
		if (r_splitscreen == 1)
		{
			if (player == 1)
				*y += screenheight;
		}
		else if (r_splitscreen > 1)
		{
			if (player == 1 || player == 3)
				*x += screenwidth;

			if (player == 2 || player == 3)
				*y += screenheight;
		}
	}

	if ((options & V_SLIDEIN))
	{
		if (st_fadein < FRACUNIT)
		{
			if ((options & (V_SNAPTORIGHT|V_SNAPTOLEFT|V_SPLITSCREEN)) != 0)
			{
				dboolean slidefromright = false;

				const fixed_t offsetAmount = (screenwidth * FRACUNIT/2);
				int32_t offset = (offsetAmount - FixedMul(offsetAmount, st_fadein)) / FRACUNIT;

				if (r_splitscreen > 1)
				{
					if (player & 1)
						slidefromright = true;
				}

				if (options & V_SNAPTORIGHT)
					slidefromright = true;
				else if (options & V_SNAPTOLEFT)
					slidefromright = false;

				if (slidefromright == true)
				{
					offset = -offset;
				}

				*x -= offset;
			}
			else
			{
				const fixed_t offsetAmount = (screenheight * FRACUNIT/2);
				int32_t offset = (offsetAmount - FixedMul(offsetAmount, st_fadein)) / FRACUNIT;

				if (options & V_SNAPTOBOTTOM)
				{
					offset = -offset;
				}

				*y -= offset;
			}
		}
	}
}

static cliprect_t cliprect;

const cliprect_t *V_GetClipRect(void)
{
	if (cliprect.enabled == false)
	{
		return NULL;
	}

	return &cliprect;
}

static inline void V_GetClipBounds(int32_t *left, int32_t *right, int32_t *top, int32_t *bottom)
{
	if (cliprect.enabled)
	{
		*left = cliprect.left;
		*right = cliprect.right;
		*top = cliprect.top;
		*bottom = cliprect.bottom;
	}
	else
	{
		*left = 0;
		*right = vid.width;
		*top = 0;
		*bottom = vid.height;
	}
}

void V_SetClipRect(fixed_t x, fixed_t y, fixed_t w, fixed_t h, int32_t flags)
{
	// Adjust position.
	if (!(flags & V_NOSCALESTART))
	{
		fixed_t dup = vid.dup;

		if (flags & V_SCALEPATCHMASK)
		{
			switch ((flags & V_SCALEPATCHMASK) >> V_SCALEPATCHSHIFT)
			{
				case 1: // V_NOSCALEPATCH
					dup = 1;
					break;
				case 2: // V_SMALLSCALEPATCH
					dup = vid.smalldup;
					break;
				case 3: // V_MEDSCALEPATCH
					dup = vid.meddup;
					break;
				default:
					break;
			}
		}

		// fudge w/h to avoid precision loss (for carefully clipped drawfills)
		w += x % (FRACUNIT/dup);
		h += y % (FRACUNIT/dup);

		x = FixedMul(x, dup);
		y = FixedMul(y, dup);
		w = FixedMul(w, dup);
		h = FixedMul(h, dup);

		if (!(flags & V_SCALEPATCHMASK))
		{
			V_AdjustXYWithSnap(&x, &y, flags, dup);
		}
	}

	if (x < 0)
	{
		w += x;
		x = 0;
	}

	if (y < 0)
	{
		h += y;
		y = 0;
	}

	if (x + w > vid.width)
		w = vid.width - x;
	if (y + h > vid.height)
		h = vid.height - y;

	x = std::min<fixed_t>(x, vid.width);
	y = std::min<fixed_t>(y, vid.height);
	w = std::max<fixed_t>(w, 0);
	h = std::max<fixed_t>(h, 0);

	cliprect.left = x;
	cliprect.top = y;
	cliprect.right = x + w;
	cliprect.bottom = y + h;
	cliprect.flags = flags;
	cliprect.enabled = true;


	//V_DrawFill(cliprect.left, cliprect.top, cliprect.right - cliprect.left, cliprect.bottom - cliprect.top, V_NOSCALESTART);
	//CONS_Printf("[(%d, %d), (%d, %d)]\n", cliprect.left, cliprect.top, cliprect.right, cliprect.bottom);
}

// sets up a cliprect for a splitscreen player (and actually accounts for non-green res)
void V_SetClipRectForPlayer(uint8_t pnum)
{
	int32_t w = vid.width, h = vid.height;
	if (r_splitscreen == 0)
		V_ClearClipRect(); // no need to
	else if (r_splitscreen == 1)
		V_SetClipRect(0, pnum ? h/2 : 0, w, h/2, V_NOSCALESTART);
	else
		V_SetClipRect(pnum & 1 ? w/2 : 0, pnum & 2 ? h/2 : 0, w/2, h/2, V_NOSCALESTART);
}

void V_ClearClipRect(void)
{
	cliprect.enabled = false;
}

void V_SaveClipRect(cliprect_t *copy)
{
	*copy = cliprect;
}

void V_RestoreClipRect(const cliprect_t *copy)
{
	cliprect = *copy;
}

static uint8_t hudplusalpha[11]  = { 10,  8,  6,  4,  2,  0,  0,  0,  0,  0,  0};
static uint8_t hudminusalpha[11] = { 10,  9,  9,  8,  8,  7,  7,  6,  6,  5,  5};

uint8_t V_GetHudTransHalf(void)
{
	return hudminusalpha[st_translucency];
}

uint8_t V_GetHudTrans(void)
{
	return 10 - st_translucency;
}

uint8_t V_GetHudTransDouble(void)
{
	return hudplusalpha[st_translucency];
}

static const uint8_t *v_colormap = NULL;
static const uint8_t *v_translevel = NULL;

#define STANDARDDRAW 1
#define MAPPEDDRAW 2
#define TRANSLUCENTDRAW 3
#define TRANSMAPPEDDRAW 4

/*
static int sortcoords(const void *a, const void *b)
{
	return *(const fixed_t *)a - *(const fixed_t *)b;
}
*/

affine_bounding_t* V_GetAffineBounds(const affine_t* transform,
				     patch_t* patch,
					 fixed_t divisor,
				     affine_bounding_t* out,
					 dboolean unrestrict_bound)
{
	// A decent chunk of this code is just fixedpointizing stuff
	// from HWR_GetAffinePatch; That system's near flawless, why fix what isn't broken?
	// Kudos to Generic for making my life easier 🥹

	// First, let's set our output to a bunch of dummy values.

	out->l = INT32_MAX;
	out->t = INT32_MAX;
	out->r = INT32_MIN;
	out->b = INT32_MIN;

	fixed_t fa = FixedDiv(transform->a, divisor);
	fixed_t fd = FixedDiv(transform->d, divisor);
	fixed_t fc = FixedDiv(transform->c, divisor);
	fixed_t fb = FixedDiv(transform->b, divisor);
	fixed_t fx = (transform->ox);
	fixed_t fy = (transform->oy);

	// now, the matrix passed to this function maps screen coordinates to texel coordinates...
	// but to translate this from software to GL, we have to figure out where each corner
	// (or vertex) of the patch should end up on the screen.
	// which means we have to map texel coordinates to screen coordinates.
	// which means we have to invert the matrix.
	// how do you invert a matrix?
	// ...
	// i don't fucking know, i spent a day on this and got absolutely nowhere, but this guy
	// knows:
	// https://nigeltao.github.io/blog/2021/inverting-3x2-affine-transformation-matrix.html

	// ...so I thought this would be standard fare order-of-operations stuff
	// NOPE!
	// The compiled assembly code does THIS:
	// (fa * fd) - (fb * fc)
	fixed_t determinant = FixedMul(fa, fd) - FixedMul(fb, fc);

	if (determinant == 0)
		return out;

	fixed_t ba = FixedDiv(fd, determinant);
	fixed_t bb = FixedDiv(-fb, determinant);
	fixed_t bc = FixedDiv(-fc, determinant);
	fixed_t bd = FixedDiv(fa, determinant);

	// set the polygon vertices to the right positions
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	fixed_t pw = (patch->width << FRACBITS);
	fixed_t ph = (patch->height << FRACBITS);

	// "sorry, unimplemented: non-trivial designated initializers not supported"
	// THANK YOU VERY MUCH CEEPEEPEE!!!
	// The thing that works in C suddenly doesn't in the NEAR-IDENTICAL language becuse uhhhhhhhhhhhhhh
	vector2_t v[4] = {};

	v[3] = {.x = (FixedMul(ba, -fx) - FixedMul(bb, fy)) + fx, .y = (FixedMul(bc, -fx) - FixedMul(bd, fy)) + fy };
	v[2] = {.x = (FixedMul(ba, (pw - fx)) - FixedMul(bb, fy)) + fx, .y = (FixedMul(bc, (pw - fx)) - FixedMul(bd, fy)) + fy };
	v[0] = {.x = (FixedMul(ba, -fx) + FixedMul(bb, (ph - fy))) + fx, .y = (FixedMul(bc, -fx) + FixedMul(bd, (ph - fy))) + fy };
	v[1] = {.x = (FixedMul(ba, (pw - fx)) + FixedMul(bb, (ph - fy))) + fx, .y = (FixedMul(bc, (pw - fx)) + FixedMul(bd, (ph - fy))) + fy };

	// Get the leftmost and uppermost bounds of the current resolution.
	int32_t vw = vid.width, vh = vid.height;
	int32_t leftmost = ((BASEVIDWIDTH - vw) / 2), uppermost = ((BASEVIDHEIGHT - vh) / 2);

	if (unrestrict_bound)
	{
		vw = vh = INT32_MAX;
		leftmost = uppermost = INT32_MIN;
	}

	// ...okay, now comb through all four vertices and set the output bounds based on this.
	// "Why not a loop?" According to SM64 programming wizard Kaze Emanuar,
	// loops take more processing time. If we can help it, it's better to just cut corners.
#define BOUNDCHECK(i)                                     \
	{                                                 \
		out->l = std::max<fixed_t>(leftmost, std::min(v[i].x >> FRACBITS, out->l)); \
		out->r = std::min<fixed_t>(vw, std::max(v[i].x >> FRACBITS, out->r)); \
		out->t = std::max<fixed_t>(uppermost, std::min(v[i].y >> FRACBITS, out->t)); \
		out->b = std::min<fixed_t>(vh, std::max(v[i].y >> FRACBITS, out->b)); \
	}

	BOUNDCHECK(0);
	BOUNDCHECK(1);
	BOUNDCHECK(2);
	BOUNDCHECK(3);

#undef BOUNDCHECK

	// Cool, we have our bounds. Get the diffs so the loops have something to reference.
	out->xlen = abs(out->r - out->l);
	out->ylen = abs(out->b - out->t);

	out->xleft = (fx >> FRACBITS) - out->l;
	out->xright = out->r - (fx >> FRACBITS);

	out->yup = (fy >> FRACBITS) - out->t;
	out->ydown = out->b - (fy >> FRACBITS);

	return out;
}

static uint32_t V_GetAlphaLevel(int32_t scrn)
{
	switch (scrn & V_ALPHAMASK)
	{
		case V_HUDTRANSHALF:
			return hudminusalpha[V_GetHUDTranslucency(scrn)];

		case V_HUDTRANS:
			return 10 - V_GetHUDTranslucency(scrn);

		case V_HUDTRANSDOUBLE:
			return hudplusalpha[V_GetHUDTranslucency(scrn)];

		default:
			return (scrn & V_ALPHAMASK) >> V_ALPHASHIFT;
	}
}

void V_DrawAffinePatch(fixed_t x, fixed_t y, const affine_t *transform, int32_t scrn, patch_t *patch, const uint8_t *colormap)
{
	uint32_t alphalevel = 0, blendmode = 0;
	uint8_t patchdrawtype = STANDARDDRAW;

	if (rendermode == render_none)
		return;

	if ((blendmode = ((scrn & V_BLENDMASK) >> V_BLENDSHIFT)))
		blendmode++; // realign to constants

	if ((alphalevel = V_GetAlphaLevel(scrn)) >= 10)
		return;

	if ((v_translevel = R_GetBlendTable(blendmode, alphalevel)))
		patchdrawtype = TRANSLUCENTDRAW;

	int32_t dup;
	switch (scrn & V_SCALEPATCHMASK)
	{
		case V_NOSCALEPATCH:
			dup = 1;
			break;
		case V_SMALLSCALEPATCH:
			dup = vid.smalldup;
			break;
		case V_MEDSCALEPATCH:
			dup = vid.meddup;
			break;
		default:
			dup = vid.dup;
			break;
	}

	if (scrn & V_NOSCALESTART)
	{
		x >>= FRACBITS;
		y >>= FRACBITS;
	}
	else
	{
		x = FixedMul(x,dup<<FRACBITS);
		y = FixedMul(y,dup<<FRACBITS);
		x += transform->ox * (dup-1);
		y += transform->oy * (dup-1);
		x >>= FRACBITS;
		y >>= FRACBITS;

		if (!(scrn & V_SCALEPATCHMASK)) // Center it if necessary
			V_AdjustXYWithSnap(&x, &y, scrn, dup);
	}

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		HWR_DrawAffinePatch(patch, x, y, transform, scrn, colormap);
		return;
	}
#endif

	affine_bounding_t bounds = {};
	V_GetAffineBounds(transform, patch, vid.dup * FRACUNIT, &bounds, false);

	Patch_GenerateFlat(patch, static_cast<pictureflags_t>(0));
	const uint16_t *src = static_cast<uint16_t *>(patch->flats[0]);
	if (src == NULL)
		return;

	const fixed_t a = transform->a / dup;
	const fixed_t b = transform->b / dup;
	const fixed_t c = transform->c / dup;
	const fixed_t d = transform->d / dup;
	fixed_t cx = transform->ox;
	fixed_t cy = transform->oy;

	const int32_t scrwidth = vid.width;
	const int32_t pw = patch->width, ph = patch->height;

	/*
	fixed_t determinant = FixedMul(a, d) - FixedMul(b, c);
	if (determinant == 0)
		return;
	fixed_t ba = FixedDiv(d, determinant);
	fixed_t bb = FixedDiv(-b, determinant);
	fixed_t bc = FixedDiv(-c, determinant);
	fixed_t bd = FixedDiv(a, determinant);

	fixed_t x1 = FixedMul(ba, -cx)     + FixedMul(bb, -cy) + cx;
	fixed_t y1 = FixedMul(bc, -cx)     + FixedMul(bd, -cy) + cy;
	fixed_t x2 = FixedMul(ba, pw*FRACUNIT - cx) + FixedMul(bb, -cy) + cx;
	fixed_t y2 = FixedMul(bc, pw*FRACUNIT - cx) + FixedMul(bd, -cy) + cy;
	fixed_t x3 = FixedMul(ba, -cx)     + FixedMul(bb, ph*FRACUNIT - cy) + cx;
	fixed_t y3 = FixedMul(bc, -cx)     + FixedMul(bd, ph*FRACUNIT - cy) + cy;
	fixed_t x4 = FixedMul(ba, pw*FRACUNIT - cx) + FixedMul(bb, ph*FRACUNIT - cy) + cx;
	fixed_t y4 = FixedMul(bc, pw*FRACUNIT - cx) + FixedMul(bd, ph*FRACUNIT - cy) + cy;
	*/

	int32_t ydiff = (bounds.yup - (transform->oy >> FRACBITS));
	int32_t xdiff = (bounds.xleft - (transform->ox >> FRACBITS));

	// Get the clipping values, since that can vary per-resolution.
	int32_t yclip = std::min(y - ydiff, 0) * -1;
	int32_t xclip = std::min(x - xdiff, 0) * -1;

	ydiff -= yclip;
	xdiff -= xclip;

	// Get the leftmost and uppermost bounds of the current resolution.
	const int32_t vw = vid.width, vh = vid.height;

	int32_t yy = CLAMP(y - ydiff, 0, vh);
	int32_t xx = CLAMP(x - xdiff, 0, vw);

	int32_t xmax = std::max(bounds.xlen - xclip, pw), ymax = std::max(bounds.ylen - yclip, ph);

	// Offset our X and Y positions by the bounding differences.
	fixed_t cxx = cx + (xdiff * FRACUNIT);
	fixed_t cyy = cy + (ydiff * FRACUNIT);

	intptr_t dest_y = (intptr_t)(vid.screens[0]) + yy * vw;

	uint8_t * const destbase = (uint8_t * const)(dest_y + xx);
	int32_t dx = 0, dy = 0;
	uint8_t mypixel = TRANSPARENTPIXEL;
	for (dy = 0; dy < ymax; dy++)
	{
		// yoinked from NovaSquirrel's mode 7 preview
		// ...which is in turn yoinked from Mesen's S-PPU code
		// i can't do matrix math to save my life :face_holding_back_tears:
		// (m7xofs and m7yofs are already factored in by destbase)
		fixed_t ux = (FixedMul(a, -cxx) + FixedMul(b, -cyy) + b*(dy) + cx);
		fixed_t uy = FixedMul(c, -cxx) + FixedMul(d, -cyy) + d*(dy) + cy;
		uint8_t *dest = (destbase) + (dy) * scrwidth;

		for (dx = 0; dx < xmax; dx++, dest++)
		{
			const int32_t srcx = ux >> FRACBITS;
			const int32_t srcy = uy >> FRACBITS;
			ux += a;
			uy += c;

			if (srcx < 0 || srcx >= pw || srcy < 0 || srcy >= ph)
				continue;

			const uint16_t pixel = src[srcy * pw + srcx];
			if (pixel < 0xff00)
				continue;

			if (colormap != NULL)
				mypixel = colormap[pixel & 0xff];
			else
				mypixel = (uint8_t)(pixel & 0xff);

			if (patchdrawtype == TRANSLUCENTDRAW)
			{
				// Translucent
				*dest = *(v_translevel + (((mypixel)<<8)&0xff00) + (*dest&0xff));
			}
			else
			{
				// Opaque
				*dest = mypixel;
			}
		}
	}

	/*
	fixed_t xsort[4] = { x1, x2, x3, x4 };
	fixed_t ysort[4] = { y1, y2, y3, y4 };
	qs22j(xsort, 4, sizeof(*xsort), sortcoords);
	qs22j(ysort, 4, sizeof(*ysort), sortcoords);
	fixed_t dx1, dx2;
	dx1 = FixedDiv(x1 - x2, y2 - y1);
	dx2 = FixedDiv(x1 - x3, y3 - y1);
	fixed_t px1 = x*FRACUNIT + x1;
	fixed_t px2 = x*FRACUNIT + x2;
	fixed_t py = y*FRACUNIT + y1;
	for (uint32_t i = 0; i < 10; i++)
	{
		for (fixed_t A = px1; A < px2; A += FRACUNIT)
			V_DrawFixedFill(A, py + i*FRACUNIT, FRACUNIT, FRACUNIT, V_NOSCALESTART | (leveltime&0xff));
		px1 += dx1;
		px2 += dx2;
	}
	V_DrawFixedFill(x*FRACUNIT + x1, y*FRACUNIT + y1, FRACUNIT, FRACUNIT, V_NOSCALESTART | (leveltime&0xff));
	V_DrawFixedFill(x*FRACUNIT + x2, y*FRACUNIT + y2, FRACUNIT, FRACUNIT, V_NOSCALESTART | (leveltime&0xff));
	V_DrawFixedFill(x*FRACUNIT + x3, y*FRACUNIT + y3, FRACUNIT, FRACUNIT, V_NOSCALESTART | (leveltime&0xff));
	V_DrawFixedFill(x*FRACUNIT + x4, y*FRACUNIT + y4, FRACUNIT, FRACUNIT, V_NOSCALESTART | (leveltime&0xff));
	*/
};

// draws a patch rotated counter-clockwise by angle degrees
// note that scaling is applied BEFORE rotation!
void V_DrawRotatedPatch(fixed_t x, fixed_t y, angle_t angle, fixed_t pscale, fixed_t vscale, int32_t scrn, patch_t *patch, const uint8_t *colormap)
{
	fixed_t sa = FSIN(angle), ca = FCOS(angle);
	affine_t t = {
		.a = FixedDiv(ca, pscale),
		.b = FixedDiv(-sa, pscale),
		.c = FixedDiv(sa, vscale),
		.d = FixedDiv(ca, vscale),
		.ox = (patch->pivot.x + patch->leftoffset) * FRACUNIT,
		.oy = (patch->pivot.y + patch->topoffset) * FRACUNIT,
	};
	V_DrawAffinePatch(x, y, &t, scrn, patch, colormap);
}

uint32_t V_GetHUDTranslucency(int32_t scrn)
{
	if (scrn & V_SLIDEIN)
	{
		return cv_translucenthud.value;
	}

	if (scrn & V_SPLITSCREEN)
	{
		return FixedMul(cv_translucenthud.value, st_fadein);
	}

	return st_translucency;
}

// Draws a patch scaled to arbitrary size.
void V_DrawStretchyFixedPatch(fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, int32_t scrn, patch_t *patch, const uint8_t *colormap)
{
	uint8_t patchdrawtype;
	uint32_t alphalevel, blendmode;

	fixed_t col, ofs, colfrac, rowfrac, fdup, vdup;
	int32_t dup;
	const column_t *column;
	uint8_t *desttop, *dest, *deststart, *destend;
	const uint8_t *source, *deststop;
	fixed_t pwidth; // patch width
	fixed_t offx = 0; // x offset
	const int32_t vidwidth = vid.width;

	const cliprect_t *clip = V_GetClipRect();

	if (rendermode == render_none)
		return;

#ifdef HWRENDER
	//if (rendermode != render_soft && !con_startup)		// Why?
	if (rendermode == render_opengl)
	{
		HWR_DrawStretchyFixedPatch(patch, x, y, pscale, vscale, scrn, colormap);
		return;
	}
#endif

	patchdrawtype = STANDARDDRAW;

	if ((blendmode = ((scrn & V_BLENDMASK) >> V_BLENDSHIFT)))
		blendmode++; // realign to constants

	if ((alphalevel = V_GetAlphaLevel(scrn)) >= 10)
		return;

	if ((v_translevel = R_GetBlendTable(blendmode, alphalevel)))
		patchdrawtype = TRANSLUCENTDRAW;

	v_colormap = NULL;
	if (colormap)
	{
		v_colormap = colormap;
		patchdrawtype = (v_translevel) ? TRANSMAPPEDDRAW : MAPPEDDRAW;
	}

	dup = vid.dup;
	if (scrn & V_SCALEPATCHMASK) switch ((scrn & V_SCALEPATCHMASK) >> V_SCALEPATCHSHIFT)
	{
		case 1: // V_NOSCALEPATCH
			dup = 1;
			break;
		case 2: // V_SMALLSCALEPATCH
			dup = vid.smalldup;
			break;
		case 3: // V_MEDSCALEPATCH
			dup = vid.meddup;
			break;
		default:
			break;
	}

	// only use one dup, to avoid stretching (har har)
	fdup = vdup = FixedMul(dup<<FRACBITS, pscale);
	if (vscale != pscale)
		vdup = FixedMul(dup<<FRACBITS, vscale);
	colfrac = FixedDiv(FRACUNIT, fdup);
	rowfrac = FixedDiv(FRACUNIT, vdup);

	{
		fixed_t offsetx = 0, offsety = 0;

		// left offset
		if (scrn & V_FLIP)
			offsetx = FixedMul((patch->width - patch->leftoffset)<<FRACBITS, pscale) + 1;
		else
			offsetx = FixedMul(patch->leftoffset<<FRACBITS, pscale);

		// top offset
		if (scrn & V_VFLIP)
			offsety = FixedMul((patch->height - patch->topoffset)<<FRACBITS, vscale) + 1;
		else
			offsety = FixedMul(patch->topoffset<<FRACBITS, vscale);

		// Subtract the offsets from x/y positions
		x -= offsetx;
		y -= offsety;
	}

	desttop = vid.screens[scrn&V_SCREENMASK];

	if (!desttop)
		return;

	deststop = desttop + vid.rowbytes * vid.height;

	if (scrn & V_NOSCALESTART)
	{
		x >>= FRACBITS;
		y >>= FRACBITS;
		desttop += (y*vid.width) + x;
	}
	else
	{
		x = FixedMul(x,dup<<FRACBITS);
		y = FixedMul(y,dup<<FRACBITS);
		x >>= FRACBITS;
		y >>= FRACBITS;

		// Center it if necessary
		if (!(scrn & V_SCALEPATCHMASK))
		{
			V_AdjustXYWithSnap(&x, &y, scrn, dup);
		}

		desttop += (y*vidwidth) + x;
	}

	if (pscale != FRACUNIT) // scale width properly
	{
		pwidth = patch->width<<FRACBITS;
		pwidth = FixedMul(pwidth, pscale);
		pwidth = FixedMul(pwidth, dup<<FRACBITS);
		pwidth >>= FRACBITS;
	}
	else
		pwidth = patch->width * dup;

	deststart = desttop;
	destend = desttop + pwidth;

	for (col = 0; (col>>FRACBITS) < patch->width; col += colfrac, ++offx, desttop++)
	{
		int32_t topdelta, prevdelta = -1;

		if (scrn & V_FLIP) // offx is measured from right edge instead of left
		{
			if (x+pwidth-offx < (clip ? clip->left : 0)) // don't draw off the left of the screen (WRAP PREVENTION)
				break;
			if (x+pwidth-offx >= (clip ? clip->right : vidwidth)) // don't draw off the right of the screen (WRAP PREVENTION)
				continue;
		}
		else
		{
			if (x+offx < (clip ? clip->left : 0)) // don't draw off the left of the screen (WRAP PREVENTION)
				continue;
			if (x+offx >= (clip ? clip->right : vidwidth)) // don't draw off the right of the screen (WRAP PREVENTION)
				break;
		}

		column = (const column_t *)((const uint8_t *)(patch->columns) + (patch->columnofs[col>>FRACBITS]));

		switch (patchdrawtype)
		{
			case STANDARDDRAW:
				while (column->topdelta != 0xff)
				{
					fixed_t offy = 0;

					topdelta = column->topdelta;
					if (topdelta <= prevdelta)
						topdelta += prevdelta;
					prevdelta = topdelta;
					source = (const uint8_t *)(column) + 3;

					dest = desttop;
					if (scrn & V_FLIP)
						dest = deststart + (destend - dest);
					topdelta = FixedInt(FixedMul(topdelta << FRACBITS, vdup));
					dest += topdelta * vidwidth;

					if (scrn & V_VFLIP)
					{
						for (ofs = (column->length << FRACBITS)-1; dest < deststop && ofs >= 0; ofs -= rowfrac, ++offy)
						{
							if (clip != NULL)
							{
								const int32_t cy = y + topdelta - offy;

								if (cy < clip->top) // don't draw off the top of the clip rect
								{
									dest += vidwidth;
									continue;
								}

								if (cy >= clip->bottom) // don't draw off the bottom of the clip rect
								{
									dest += vidwidth;
									continue;
								}
							}

							if (dest >= vid.screens[scrn&V_SCREENMASK]) // don't draw off the top of the screen (CRASH PREVENTION)
								*dest = source[ofs>>FRACBITS];

							dest += vidwidth;
						}
					}
					else
					{
						for (ofs = 0; dest < deststop && ofs < (column->length << FRACBITS); ofs += rowfrac, ++offy)
						{
							if (clip != NULL)
							{
								const int32_t cy = y + topdelta + offy;

								if (cy < clip->top) // don't draw off the top of the clip rect
								{
									dest += vidwidth;
									continue;
								}

								if (cy >= clip->bottom) // don't draw off the bottom of the clip rect
								{
									dest += vidwidth;
									continue;
								}
							}

							if (dest >= vid.screens[scrn&V_SCREENMASK]) // don't draw off the top of the screen (CRASH PREVENTION)
								*dest = source[ofs>>FRACBITS];

							dest += vidwidth;
						}
					}

					column = (const column_t *)((const uint8_t *)column + column->length + 4);
				}
			break;

			case MAPPEDDRAW:
				while (column->topdelta != 0xff)
				{
					fixed_t offy = 0;

					topdelta = column->topdelta;
					if (topdelta <= prevdelta)
						topdelta += prevdelta;
					prevdelta = topdelta;
					source = (const uint8_t *)(column) + 3;

					dest = desttop;
					if (scrn & V_FLIP)
						dest = deststart + (destend - dest);
					topdelta = FixedInt(FixedMul(topdelta << FRACBITS, vdup));
					dest += topdelta * vidwidth;

					if (scrn & V_VFLIP)
					{
						for (ofs = (column->length << FRACBITS)-1; dest < deststop && ofs >= 0; ofs -= rowfrac, ++offy)
						{
							if (clip != NULL)
							{
								const int32_t cy = y + topdelta - offy;

								if (cy < clip->top) // don't draw off the top of the clip rect
								{
									dest += vidwidth;
									continue;
								}

								if (cy >= clip->bottom) // don't draw off the bottom of the clip rect
								{
									dest += vidwidth;
									continue;
								}
							}

							if (dest >= vid.screens[scrn&V_SCREENMASK]) // don't draw off the top of the screen (CRASH PREVENTION)
								*dest = *(v_colormap + source[ofs>>FRACBITS]);

							dest += vidwidth;
						}
					}
					else
					{
						for (ofs = 0; dest < deststop && ofs < (column->length << FRACBITS); ofs += rowfrac, ++offy)
						{
							if (clip != NULL)
							{
								const int32_t cy = y + topdelta + offy;

								if (cy < clip->top) // don't draw off the top of the clip rect
								{
									dest += vidwidth;
									continue;
								}

								if (cy >= clip->bottom) // don't draw off the bottom of the clip rect
								{
									dest += vidwidth;
									continue;
								}
							}

							if (dest >= vid.screens[scrn&V_SCREENMASK]) // don't draw off the top of the screen (CRASH PREVENTION)
								*dest = *(v_colormap + source[ofs>>FRACBITS]);

							dest += vidwidth;
						}
					}

					column = (const column_t *)((const uint8_t *)column + column->length + 4);
				}
				break;

			case TRANSMAPPEDDRAW:
				while (column->topdelta != 0xff)
				{
					fixed_t offy = 0;

					topdelta = column->topdelta;
					if (topdelta <= prevdelta)
						topdelta += prevdelta;
					prevdelta = topdelta;
					source = (const uint8_t *)(column) + 3;

					dest = desttop;
					if (scrn & V_FLIP)
						dest = deststart + (destend - dest);
					topdelta = FixedInt(FixedMul(topdelta << FRACBITS, vdup));
					dest += topdelta * vidwidth;

					if (scrn & V_VFLIP)
					{
						for (ofs = (column->length << FRACBITS)-1; dest < deststop && ofs >= 0; ofs -= rowfrac, ++offy)
						{
							if (clip != NULL)
							{
								const int32_t cy = y + topdelta - offy;

								if (cy < clip->top) // don't draw off the top of the clip rect
								{
									dest += vidwidth;
									continue;
								}

								if (cy >= clip->bottom) // don't draw off the bottom of the clip rect
								{
									dest += vidwidth;
									continue;
								}
							}

							if (dest >= vid.screens[scrn&V_SCREENMASK]) // don't draw off the top of the screen (CRASH PREVENTION)
								*dest = *(v_translevel + (((*(v_colormap + source[ofs>>FRACBITS]))<<8)&0xff00) + (*dest&0xff));

							dest += vidwidth;
						}
					}
					else
					{
						for (ofs = 0; dest < deststop && ofs < (column->length << FRACBITS); ofs += rowfrac, ++offy)
						{
							if (clip != NULL)
							{
								const int32_t cy = y + topdelta + offy;

								if (cy < clip->top) // don't draw off the top of the clip rect
								{
									dest += vidwidth;
									continue;
								}

								if (cy >= clip->bottom) // don't draw off the bottom of the clip rect
								{
									dest += vidwidth;
									continue;
								}
							}

							if (dest >= vid.screens[scrn&V_SCREENMASK]) // don't draw off the top of the screen (CRASH PREVENTION)
								*dest = *(v_translevel + (((*(v_colormap + source[ofs>>FRACBITS]))<<8)&0xff00) + (*dest&0xff));

							dest += vidwidth;
						}
					}

					column = (const column_t *)((const uint8_t *)column + column->length + 4);
				}
				break;

				case TRANSLUCENTDRAW:
				while (column->topdelta != 0xff)
				{
					fixed_t offy = 0;

					topdelta = column->topdelta;
					if (topdelta <= prevdelta)
						topdelta += prevdelta;
					prevdelta = topdelta;
					source = (const uint8_t *)(column) + 3;

					dest = desttop;
					if (scrn & V_FLIP)
						dest = deststart + (destend - dest);
					topdelta = FixedInt(FixedMul(topdelta << FRACBITS, vdup));
					dest += topdelta * vidwidth;

					if (scrn & V_VFLIP)
					{
						for (ofs = (column->length << FRACBITS)-1; dest < deststop && ofs >= 0; ofs -= rowfrac, ++offy)
						{
							if (clip != NULL)
							{
								const int32_t cy = y + topdelta - offy;

								if (cy < clip->top) // don't draw off the top of the clip rect
								{
									dest += vidwidth;
									continue;
								}

								if (cy >= clip->bottom) // don't draw off the bottom of the clip rect
								{
									dest += vidwidth;
									continue;
								}
							}

							if (dest >= vid.screens[scrn&V_SCREENMASK]) // don't draw off the top of the screen (CRASH PREVENTION)
								*dest = *(v_translevel + ((source[ofs>>FRACBITS]<<8)&0xff00) + (*dest&0xff));

							dest += vidwidth;
						}
					}
					else
					{
						for (ofs = 0; dest < deststop && ofs < (column->length << FRACBITS); ofs += rowfrac, ++offy)
						{
							if (clip != NULL)
							{
								const int32_t cy = y + topdelta + offy;

								if (cy < clip->top) // don't draw off the top of the clip rect
								{
									dest += vidwidth;
									continue;
								}

								if (cy >= clip->bottom) // don't draw off the bottom of the clip rect
								{
									dest += vidwidth;
									continue;
								}
							}

							if (dest >= vid.screens[scrn&V_SCREENMASK]) // don't draw off the top of the screen (CRASH PREVENTION)
								*dest = *(v_translevel + ((source[ofs>>FRACBITS]<<8)&0xff00) + (*dest&0xff));

							dest += vidwidth;
						}
					}

					column = (const column_t *)((const uint8_t *)column + column->length + 4);
				}
				break;
		}
	}
}

// Draws a patch cropped and scaled to arbitrary size.
void V_DrawCroppedPatch(fixed_t x, fixed_t y, fixed_t pscale, int32_t scrn, patch_t *patch, fixed_t sx, fixed_t sy, fixed_t w, fixed_t h)
{
	cliprect_t oldClip = cliprect;

	V_SetClipRect(x, y, w, h, scrn);

	x -= sx;
	y -= sy;

	V_DrawStretchyFixedPatch(x, y, pscale, pscale, scrn, patch, NULL);

	cliprect = oldClip;
}

//
// V_DrawContinueIcon
// Draw a mini player!  If we can, that is.  Otherwise we draw a star.
//
void V_DrawContinueIcon(int32_t x, int32_t y, int32_t flags, int32_t skinnum, uint16_t skincolor)
{
	(void)skinnum;
	(void)skincolor;
	V_DrawScaledPatch(x - 10, y - 14, flags, static_cast<patch_t *>(W_CachePatchName("CONTINS", PU_PATCH)));
}

//
// V_DrawBlock
// Draw a linear block of pixels into the view buffer.
//
void V_DrawBlock(int32_t x, int32_t y, int32_t scrn, int32_t width, int32_t height, const uint8_t *src)
{
	uint8_t *dest;
	const uint8_t *deststop;

#ifdef RANGECHECK
	if (x < 0 || x + width > vid.width || y < 0 || y + height > vid.height || (unsigned)scrn > 4)
		I_Error("Bad V_DrawBlock");
#endif

	dest = vid.screens[scrn] + y*vid.width + x;
	deststop = vid.screens[scrn] + vid.rowbytes * vid.height;

	while (height--)
	{
		memcpy(dest, src, width);

		src += width;
		dest += vid.width;
		if (dest > deststop)
			return;
	}
}

//
// Fills a box of pixels with a single color, NOTE: scaled to screen size
//
void V_DrawFixedFill(fixed_t x, fixed_t y, int32_t w, int32_t h, int32_t c)
{
	uint8_t *dest;
	const uint8_t *deststop;
	uint32_t alphalevel = ((c & V_ALPHAMASK) >> V_ALPHASHIFT);
	uint32_t blendmode = ((c & V_BLENDMASK) >> V_BLENDSHIFT);
	//int32_t u;

	if (rendermode == render_none)
		return;

	v_translevel = NULL;
	if (alphalevel || blendmode)
	{
		if (alphalevel == 13) // V_HUDTRANSHALF
			alphalevel = hudminusalpha[st_translucency];
		else if (alphalevel == 14) // V_HUDTRANS
			alphalevel = 10 - st_translucency;
		else if (alphalevel == 15) // V_HUDTRANSDOUBLE
			alphalevel = hudplusalpha[st_translucency];

		if (alphalevel >= 10)
			return; // invis

		if (alphalevel || blendmode)
			v_translevel = R_GetBlendTable(blendmode+1, alphalevel);
	}

	if (!(c & V_NOSCALESTART))
	{
		int32_t dup = vid.dup;

		if (x == 0 && y == 0 && w == BASEVIDWIDTH*FRACUNIT && h == BASEVIDHEIGHT*FRACUNIT)
		{ // Clear the entire screen, from dest to deststop. Yes, this really works.
#ifdef HWRENDER
			if (rendermode == render_opengl)
				goto fillscreen;
#endif
			memset(vid.screens[0], (c&255), vid.width * vid.height);
			return;
		}

		x = FixedMul(x, dup);
		y = FixedMul(y, dup);
		w = FixedMul(w, dup);
		h = FixedMul(h, dup);

		// Center it if necessary
		V_AdjustXYWithSnap(&x, &y, c, dup);
	}
	else
	{
		x >>= FRACBITS;
		y >>= FRACBITS;
		w >>= FRACBITS;
		h >>= FRACBITS;
	}

	int32_t left, right, top, bottom;
	V_GetClipBounds(&left, &right, &top, &bottom);

	if (x >= right || y >= bottom)
		return; // off the screen

	if (x < left)
	{
		w += x - left;
		x = left;
	}
	if (y < top)
	{
		h += y - top;
		y = top;
	}

	if (w <= 0 || h <= 0)
		return; // zero width/height wouldn't draw anything
	if (x + w > right)
		w = right - x;
	if (y + h > bottom)
		h = bottom - y;

#ifdef HWRENDER
	//if (rendermode != render_soft && !con_startup)		// Not this again
	if (rendermode == render_opengl)
	{
fillscreen:
		HWR_DrawFill(x, y, w, h, c);
		return;
	}
#endif

	dest = vid.screens[0] + y*vid.width + x;
	deststop = vid.screens[0] + vid.rowbytes * vid.height;

	c &= 255;

	// borrowing this from jimitia's new hud drawing functions rq
	if (alphalevel)
	{
		v_translevel += c<<8;
		for (;(--h >= 0) && dest < deststop; dest += vid.width)
		{
			for (x = 0; x < w; x++)
				dest[x] = v_translevel[dest[x]];
		}
	}
	else
	{
		for (;(--h >= 0) && dest < deststop; dest += vid.width)
			memset(dest, c, w);
	}
}

// When drawing translucent lines, this prevents overlapping pixels.
uint8_t occupied_pixels[MAXVIDWIDTH * MAXVIDHEIGHT] = {0};

//
// Draws a line from the point (x1, y1) to the point (x2, y2).
// Based on Milo Yip's C implementation of Bresenham's line drawing algorithm
// https://github.com/miloyip/line
//
void V_DrawFixedLine(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2, fixed_t width, int32_t c)
{
	uint8_t *dest;
	uint8_t _dest = 0;
	const uint8_t *deststop;
	uint32_t alphalevel = ((c & V_ALPHAMASK) >> V_ALPHASHIFT);
	uint32_t blendmode = ((c & V_BLENDMASK) >> V_BLENDSHIFT);
	fixed_t hwr_width = width;
	//int32_t u;
	vector2_t p1 = {0}, p2 = {0};
	if (rendermode == render_none)
		return;

	if (width <= 0)
		return;

	double width_half_d = (FixedToDouble(width) / 2.0);

	v_translevel = NULL;
	if (alphalevel || blendmode)
	{
		if (alphalevel == 13) // V_HUDTRANSHALF
			alphalevel = hudminusalpha[st_translucency];
		else if (alphalevel == 14) // V_HUDTRANS
			alphalevel = 10 - st_translucency;
		else if (alphalevel == 15) // V_HUDTRANSDOUBLE
			alphalevel = hudplusalpha[st_translucency];

		if (alphalevel >= 10)
			return; // invis

		if (alphalevel || blendmode)
		{
			// Only clear to the actual width of what we want to draw to;
			// drawing the LINE is expensive enough!
			memset(occupied_pixels, 0, vid.rowbytes * vid.height);
			v_translevel = R_GetBlendTable(blendmode+1, alphalevel);
		}
	}

	if (!(c & V_NOSCALESTART))
	{
		int32_t dup = vid.dup;

		hwr_width = FixedMul(width, dup);

		width *= dup;

		p1.x = FixedMul(x1, dup);
		p1.y = FixedMul(y1, dup);
		p2.x = FixedMul(x2, dup);
		p2.y = FixedMul(y2, dup);

		// Center it if necessary
		V_AdjustXYWithSnap(&p1.x, &p1.y, c, dup);
		V_AdjustXYWithSnap(&p2.x, &p2.y, c, dup);
	}
	else
	{
		p1.x = x1 >> FRACBITS;
		p2.x = x2 >> FRACBITS;
		p1.y = y1 >> FRACBITS;
		p2.y = y2 >> FRACBITS;
		hwr_width = width >> FRACBITS;
	}

	dboolean wideline = (width > FRACUNIT);

	int32_t left, right, top, bottom;
	V_GetClipBounds(&left, &right, &top, &bottom);

#ifdef HWRENDER
	//if (rendermode != render_soft && !con_startup)		// Not this again
	if (rendermode == render_opengl)
	{
		HWR_DrawFixedLine(p1.x, p1.y, p2.x, p2.y, hwr_width, c);
		return;
	}
#endif

	deststop = vid.screens[0] + vid.rowbytes * vid.height;

	c &= 255;

	int dx = abs(p2.x - p1.x), sx = p1.x < p2.x ? 1 : -1;
	int dy = abs(p2.y - p1.y), sy = p1.y < p2.y ? 1 : -1;
	int err = (dx > dy ? dx : -dy) / 2;

	int32_t xx = p1.x;
	int32_t yy = p1.y;
	int32_t _x = 0;
	int32_t _y = yy;
	int32_t h = 1;

	int32_t leftmost = 0, rightmost = 0, realwidth = 0;

	int32_t index = 0;

	if (alphalevel)
		v_translevel += c<<8; // :patrick_mouth:

	while (xx != p2.x || yy != p2.y)
	{
		int e2 = err;

		if (wideline)
		{
			_y = (int32_t)((double)(yy) - width_half_d);
			h = width >> FRACBITS;
		}
		else
		{
			_y = yy;
		}

		if (xx < left || xx >= right || _y < top || _y >= bottom)
		{
			if (e2 > -dx) { err -= dy; xx += sx; }
			if (e2 <  dy) { err += dx; yy += sy; }
			continue; // Out of screen bounds
		}

		leftmost = CLAMP((int32_t)((double)(xx) - width_half_d), left, right - 1);
		rightmost = CLAMP(leftmost + (width >> FRACBITS), left, right - 1);

		if (wideline)
		{
			dest = vid.screens[0] + _y*vid.width + leftmost;
			index = _y*vid.width + leftmost;
		}
		else
		{
			dest = vid.screens[0] + _y*vid.width + xx;
			index = _y*vid.width + xx;
		}

		if (leftmost >= rightmost)
		{
			if (e2 > -dx) { err -= dy; xx += sx; }
			if (e2 <  dy) { err += dx; yy += sy; }
			continue; // Inverted
		}

		realwidth = rightmost - leftmost;

		if (alphalevel)
		{
			// borrowing this from jimitia's new hud drawing functions rq

			if (wideline)
			{
				for (;(--h >= 0) && dest < deststop; dest += vid.width)
				{
					for (_x = 0; _x < (width >> FRACBITS); _x++)
					{
						if (!occupied_pixels[index + _x])
						{
							// Gotta do this, otherwise pixels will
							// overlap one another and it'll look
							// all gross
							dest[_x] = v_translevel[dest[_x]];
							occupied_pixels[index + _x] = 1;
						}
					}

					index += vid.width;
				}
			}
			else
			{
				_dest = *dest;

				*dest = v_translevel[_dest];
			}

			if (e2 > -dx) { err -= dy; xx += sx; }
			if (e2 <  dy) { err += dx; yy += sy; }
		}
		else
		{
			if (wideline)
			{
				for (;(--h >= 0) && dest < deststop; dest += vid.width)
					memset(dest, c, realwidth);
			}
			else
			{
				*dest = (uint8_t)(c);
			}

			if (e2 > -dx) { err -= dy; xx += sx; }
			if (e2 <  dy) { err += dx; yy += sy; }
		}
	}
}

#ifdef HWRENDER
// This is now a function since it's otherwise repeated 2 times and honestly looks retarded:
static uint32_t V_GetHWConsBackColor(void)
{
	RGBA_t output;

	switch (cons_backcolor.value)
	{
		case 0:		output.s.red = 0xff; output.s.green = 0xff; output.s.blue = 0xff;	break; 	// White
		case 1:		output.s.red = 0x80; output.s.green = 0x80; output.s.blue = 0x80;	break; 	// Black
		case 2:		output.s.red = 0xde; output.s.green = 0xb8; output.s.blue = 0x87;	break;	// Sepia
		case 3:		output.s.red = 0x40; output.s.green = 0x20; output.s.blue = 0x10;	break; 	// Brown
		case 4:		output.s.red = 0xfa; output.s.green = 0x80; output.s.blue = 0x72;	break; 	// Pink
		case 5:		output.s.red = 0xff; output.s.green = 0x69; output.s.blue = 0xb4;	break; 	// Raspberry
		case 6:		output.s.red = 0xff; output.s.green = 0x00; output.s.blue = 0x00;	break; 	// Red
		case 7:		output.s.red = 0xff; output.s.green = 0xd6; output.s.blue = 0x83;	break;	// Creamsicle
		case 8:		output.s.red = 0xff; output.s.green = 0x80; output.s.blue = 0x00;	break; 	// Orange
		case 9:		output.s.red = 0xda; output.s.green = 0xa5; output.s.blue = 0x20;	break; 	// Gold
		case 10:	output.s.red = 0x80; output.s.green = 0x80; output.s.blue = 0x00;	break; 	// Yellow
		case 11:	output.s.red = 0x00; output.s.green = 0xff; output.s.blue = 0x00;	break; 	// Emerald
		case 12:	output.s.red = 0x00; output.s.green = 0x80; output.s.blue = 0x00;	break; 	// Green
		case 13:	output.s.red = 0x40; output.s.green = 0x80; output.s.blue = 0xff;	break; 	// Cyan
		case 14:	output.s.red = 0x46; output.s.green = 0x82; output.s.blue = 0xb4;	break; 	// Steel
		case 15:	output.s.red = 0x1e; output.s.green = 0x90; output.s.blue = 0xff;	break;	// Periwinkle
		case 16:	output.s.red = 0x00; output.s.green = 0x00; output.s.blue = 0xff;	break; 	// Blue
		case 17:	output.s.red = 0xff; output.s.green = 0x00; output.s.blue = 0xff;	break; 	// Purple
		case 18:	output.s.red = 0xee; output.s.green = 0x82; output.s.blue = 0xee;	break; 	// Lavender
		// Default green
		default:	output.s.red = 0x00; output.s.green = 0x80; output.s.blue = 0x00;	break;
	}

	V_CubeApply(&output);
	return (output.s.red << 24) | (output.s.green << 16) | (output.s.blue << 8);
}
#endif

// THANK YOU MPC!!!
// and thanks toaster for cleaning it up.

void V_DrawFillConsoleMap(int32_t x, int32_t y, int32_t w, int32_t h, int32_t c)
{
	uint8_t *dest;
	const uint8_t *deststop;
	int32_t u;
	uint8_t *fadetable;
	uint32_t alphalevel = 0;

	if (rendermode == render_none)
		return;

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		uint32_t hwcolor = V_GetHWConsBackColor();
		HWR_DrawConsoleFill(x, y, w, h, c, hwcolor);	// we still use the regular color stuff but only for flags. actual draw color is "hwcolor" for this.
		return;
	}
#endif

	if ((alphalevel = V_GetAlphaLevel(c)) >= 10)
		return;


	if (!(c & V_NOSCALESTART))
	{
		int32_t dup = vid.dup;

		x *= dup;
		y *= dup;
		w *= dup;
		h *= dup;

		// Center it if necessary
		V_AdjustXYWithSnap(&x, &y, c, dup);
	}

	if (x >= vid.width || y >= vid.height)
		return; // off the screen
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}

	if (w <= 0 || h <= 0)
		return; // zero width/height wouldn't draw anything
	if (x + w > vid.width)
		w = vid.width-x;
	if (y + h > vid.height)
		h = vid.height-y;

	dest = vid.screens[0] + y*vid.width + x;
	deststop = vid.screens[0] + vid.rowbytes * vid.height;

	c &= 255;

	// Jimita (12-04-2018)
	if (alphalevel)
	{
		fadetable = R_GetTranslucencyTable(alphalevel) + (c*256);
		for (;(--h >= 0) && dest < deststop; dest += vid.width)
		{
			u = 0;
			while (u < w)
			{
				dest[u] = fadetable[consolebgmap[dest[u]]];
				u++;
			}
		}
	}
	else
	{
		for (;(--h >= 0) && dest < deststop; dest += vid.width)
		{
			u = 0;
			while (u < w)
			{
				dest[u] = consolebgmap[dest[u]];
				u++;
			}
		}
	}
}

//
// Fills a triangle of pixels with a single color, NOTE: scaled to screen size
//
// ...
// ..  <-- this shape only for now, i'm afraid
// .
//
void V_DrawDiag(int32_t x, int32_t y, int32_t wh, int32_t c)
{
	uint8_t *dest;
	const uint8_t *deststop;
	int32_t w, h, wait = 0;

	if (rendermode == render_none)
		return;

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		HWR_DrawDiag(x, y, wh, c);
		return;
	}
#endif

	if (!(c & V_NOSCALESTART))
	{
		int32_t dup = vid.dup;

		x *= dup;
		y *= dup;
		wh *= dup;

		// Center it if necessary
		V_AdjustXYWithSnap(&x, &y, c, dup);
	}

	if (x >= vid.width || y >= vid.height)
		return; // off the screen

	if (y < 0)
	{
		wh += y;
		y = 0;
	}

	w = h = wh;

	if (x < 0)
	{
		w += x;
		x = 0;
	}

	if (w <= 0 || h <= 0)
		return; // zero width/height wouldn't draw anything
	if (x + w > vid.width)
	{
		wait = w - (vid.width - x);
		w = vid.width - x;
	}
	if (y + w > vid.height)
		h = vid.height - y;

	if (h > w)
		h = w;

	dest = vid.screens[0] + y*vid.width + x;
	deststop = vid.screens[0] + vid.rowbytes * vid.height;

	c &= 255;

	for (;(--h >= 0) && dest < deststop; dest += vid.width)
	{
		memset(dest, c, w);
		if (wait)
			wait--;
		else
			w--;
	}
}

//
// If color is 0x00 to 0xFF, draw transtable (strength range 0-9).
// Else, use COLORMAP lump (strength range 0-31).
// c is not color, it is for flags only. transparency flags will be ignored.
// IF YOU ARE NOT CAREFUL, THIS CAN AND WILL CRASH!
// I have kept the safety checks for strength out of this function;
// I don't trust Lua users with it, so it doesn't matter.
//
void V_DrawFadeFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t c, uint16_t color, uint8_t strength)
{
	uint8_t *dest;
	const uint8_t *deststop;
	int32_t u;
	uint8_t *fadetable;

	if (rendermode == render_none)
		return;

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		// ughhhhh please can someone else do this? thanks ~toast 25/7/19 in 38 degrees centigrade w/o AC
		HWR_DrawFadeFill(x, y, w, h, c, color, strength); // toast two days later - left above comment in 'cause it's funny
		return;
	}
#endif

	if (!(c & V_NOSCALESTART))
	{
		int32_t dup = vid.dup;

		x *= dup;
		y *= dup;
		w *= dup;
		h *= dup;

		// Center it if necessary
		// adjustxy
	}

	if (x >= vid.width || y >= vid.height)
		return; // off the screen
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}

	if (w <= 0 || h <= 0)
		return; // zero width/height wouldn't draw anything
	if (x + w > vid.width)
		w = vid.width-x;
	if (y + h > vid.height)
		h = vid.height-y;

	dest = vid.screens[0] + y*vid.width + x;
	deststop = vid.screens[0] + vid.rowbytes * vid.height;

	c &= 255;

	fadetable = ((color & 0xFF00) // Color is not palette index?
		? ((uint8_t *)colormaps + strength*256) // Do COLORMAP fade.
		: ((uint8_t *)R_GetTranslucencyTable((9-strength)+1) + color*256)); // Else, do TRANSMAP** fade.
	for (;(--h >= 0) && dest < deststop; dest += vid.width)
	{
		u = 0;
		while (u < w)
		{
			dest[u] = fadetable[dest[u]];
			u++;
		}
	}
}

//
// Fills a box of pixels using a flat texture as a pattern, scaled to screen size.
//
void V_DrawFlatFill(int32_t x, int32_t y, int32_t w, int32_t h, lumpnum_t flatnum)
{
	int32_t u, v, dup;
	fixed_t dx, dy, xfrac, yfrac;
	const uint8_t *src, *deststop;
	uint8_t *flat, *dest;
	size_t size, lflatsize, flatshift;

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		HWR_DrawFlatFill(x, y, w, h, flatnum);
		return;
	}
#endif

	size = W_LumpLength(flatnum);

	switch (size)
	{
		case 4194304: // 2048x2048 lump
			lflatsize = 2048;
			flatshift = 11;
			break;
		case 1048576: // 1024x1024 lump
			lflatsize = 1024;
			flatshift = 10;
			break;
		case 262144:// 512x512 lump
			lflatsize = 512;
			flatshift = 9;
			break;
		case 65536: // 256x256 lump
			lflatsize = 256;
			flatshift = 8;
			break;
		case 16384: // 128x128 lump
			lflatsize = 128;
			flatshift = 7;
			break;
		case 1024: // 32x32 lump
			lflatsize = 32;
			flatshift = 5;
			break;
		case 256: // 16x16 lump
			lflatsize = 16;
			flatshift = 4;
			break;
		case 64: // 8x8 lump
			lflatsize = 8;
			flatshift = 3;
			break;
		default: // 64x64 lump
			lflatsize = 64;
			flatshift = 6;
			break;
	}

	flat = static_cast<uint8_t *>(W_CacheLumpNum(flatnum, PU_CACHE));

	dup = vid.dup;

	dest = vid.screens[0] + y*dup*vid.width + x*dup;
	deststop = vid.screens[0] + vid.rowbytes * vid.height;

	// from V_DrawScaledPatch
	if (vid.width != BASEVIDWIDTH * dup)
	{
		// dup adjustments pretend that screen width is BASEVIDWIDTH * dup,
		// so center this imaginary screen
		dest += (vid.width - (BASEVIDWIDTH * dup)) / 2;
	}
	if (vid.height != BASEVIDHEIGHT * dup)
	{
		// same thing here
		dest += (vid.height - (BASEVIDHEIGHT * dup)) * vid.width / 2;
	}

	w *= dup;
	h *= dup;

	dx = FixedDiv(FRACUNIT, dup<<(FRACBITS-2));
	dy = FixedDiv(FRACUNIT, dup<<(FRACBITS-2));

	yfrac = 0;
	for (v = 0; v < h; v++, dest += vid.width)
	{
		xfrac = 0;
		src = flat + (((yfrac>>FRACBITS) & (lflatsize - 1)) << flatshift);
		for (u = 0; u < w; u++)
		{
			if (&dest[u] > deststop)
				return;
			dest[u] = src[(xfrac>>FRACBITS)&(lflatsize-1)];
			xfrac += dx;
		}
		yfrac += dy;
	}
}

//
// V_DrawPatchFill
//
void V_DrawPatchFill(patch_t *pat)
{
	int32_t x, y, pw = pat->width * vid.dup, ph = pat->height * vid.dup;

	for (x = 0; x < vid.width; x += pw)
	{
		for (y = 0; y < vid.height; y += ph)
			V_DrawScaledPatch(x, y, V_NOSCALESTART, pat);
	}
}

// Draws a patch and tries to always fill the screen with the patch
void V_DrawAdaptiveScaledFullScreenPatch(patch_t *patch)
{
	fixed_t x = 0, y = 0;
	fixed_t scale = ((vid.width * FRACUNIT) / patch->width); // fit the screen horizontally
	fixed_t scaled_height = FixedMul(patch->height << FRACBITS, scale);

	// however, if this means the patch doesent fill out the screen vertically then
	if (scaled_height < (vid.height << FRACBITS))
	{
		scale = ((vid.height * FRACUNIT) / patch->height); // scale it to fit the screen vertically
		x = ((vid.width << FRACBITS) - FixedMul(patch->width << FRACBITS, scale)) / 2;
	}
	else
		y = (vid.height << FRACBITS) - scaled_height;

	V_DrawFixedPatch(x, y, scale, V_NOSCALEPATCH, patch, NULL);
}

// Draws a patch and scales it to fill out the screen vertically while remaining centered
void V_DrawVerticallyScaledFullScreenPatch(patch_t *patch)
{
	fixed_t scale = ((vid.height * FRACUNIT) / patch->height);
	fixed_t x = ((vid.width << FRACBITS) - FixedMul(patch->width << FRACBITS, scale)) / 2; // i fucking hate maths

	V_DrawFixedPatch(x, 0, scale, V_NOSCALEPATCH, patch, NULL);
}

// Draws a patch and scales it to fill out the screen horizontally
// centers the patch when its too small to fit the screen vertically
void V_DrawHorizontallyScaledFullScreenPatch(patch_t *patch)
{
	fixed_t scale = ((vid.width * FRACUNIT) / patch->width);
	fixed_t scaled_height = FixedMul(patch->height << FRACBITS, scale);
	fixed_t y = (vid.height << FRACBITS) - scaled_height;

	// center it if it does not fit the screen vertically
	if (scaled_height < (vid.height << FRACBITS))
	{
		y /= 2;
	}

	V_DrawFixedPatch(0, y, scale, V_NOSCALEPATCH, patch, NULL);
}

void V_DrawVhsEffect(dboolean rewind)
{
	fixed_t uby, dby;
	// upbary is the bar going from top to bottom for some reason
	static fixed_t upbary = 100*FRACUNIT, downbary = 150*FRACUNIT;

	uint8_t barsize, updistort, downdistort;

	uint16_t y;
	uint32_t x, pos;

	uint8_t *buf, *tmp;
	uint8_t *normalmapstart, *thismapstart;
#ifdef HQ_VHS
	uint8_t *tmapstart;
#endif
	int8_t offs;

	barsize = vid.udup << 5;
	updistort = vid.udup << (rewind ? 5 : 3);
	downdistort = updistort >> 1;

	if (rewind)
		V_DrawVhsEffect(false); // experimentation

	upbary -= renderdeltatics * (fixed_t)(vid.udup * (rewind ? 3 : 1.8f));
	downbary += renderdeltatics * (vid.udup * (rewind ? 2 : 1));

	if (upbary < -barsize*FRACUNIT)
		upbary = vid.height << FRACBITS;
	if (upbary > vid.height << FRACBITS)
		upbary = -barsize*FRACUNIT;
	if (downbary > vid.height << FRACBITS)
		downbary = -barsize*FRACUNIT;
	if (downbary < -barsize*FRACUNIT)
		downbary = vid.height << FRACBITS;

	uby = upbary >> FRACBITS;
	dby = downbary >> FRACBITS;

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		HWR_RenderVhsEffect(uby, dby, updistort, downdistort, barsize);
		return;
	}
#endif

	buf = vid.screens[0];
	tmp = vid.screens[4];

	normalmapstart = ((uint8_t *)transtables + (8<<FF_TRANSSHIFT|(19<<8)));
#ifdef HQ_VHS
	tmapstart = ((uint8_t *)transtables + (6<<FF_TRANSSHIFT));
#endif

	pos = 0;

	for (y = 0; y < vid.height; y+=2)
	{
		thismapstart = normalmapstart;
		offs = 0;

		if (y >= uby && y < uby+barsize)
		{
			thismapstart -= (2<<FF_TRANSSHIFT) - (5<<8);
			offs += updistort * 2.0f * std::min(y-uby, uby+barsize-y) / barsize;
		}

		if (y >= dby && y < dby+barsize)
		{
			thismapstart -= (2<<FF_TRANSSHIFT) - (5<<8);
			offs -= downdistort * 2.0f * std::min(y-dby, dby+barsize-y) / barsize;
		}

		offs += M_RandomKey(vid.dup<<1);

		// lazy way to avoid crashes
		if ((y == 0 && offs < 0) || (y >= vid.height-2 && offs > 0))
			offs = 0;

		for (x = std::min(pos+(size_t)vid.width*2, (size_t)vid.width*vid.height); pos < x; pos++)
		{
			tmp[pos] = thismapstart[buf[pos+offs]];
#ifdef HQ_VHS
			tmp[pos] = tmapstart[buf[pos]<<8 | tmp[pos]];
#endif
		}
	}

	memcpy(buf, tmp, vid.width*vid.height);
}

//
// Fade all the screen buffer, so that the menu is more readable,
// especially now that we use the small hufont in the menus...
// If color is 0x00 to 0xFF, draw transtable (strength range 0-9).
// Else, use COLORMAP lump (strength range 0-31).
// IF YOU ARE NOT CAREFUL, THIS CAN AND WILL CRASH!
// I have kept the safety checks out of this function;
// the v.fadeScreen Lua interface handles those.
//
void V_DrawFadeScreen(uint16_t color, uint8_t strength)
{
#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		HWR_FadeScreenMenuBack(color, strength);
		return;
	}
#endif

	{
		const uint8_t *fadetable =
			(color > 0xFFF0) // Grab a specific colormap palette?
			? R_GetTranslationColormap(color | 0xFFFF0000, static_cast<skincolornum_t>(strength), GTC_CACHE)
			: ((color & 0xFF00) // Color is not palette index?
			? ((uint8_t *)colormaps + strength*256) // Do COLORMAP fade.
			: ((uint8_t *)R_GetTranslucencyTable((9-strength)+1) + color*256)); // Else, do TRANSMAP** fade.
		const uint8_t *deststop = vid.screens[0] + vid.rowbytes * vid.height;
		uint8_t *buf = vid.screens[0];

		// heavily simplified -- we don't need to know x or y
		// position when we're doing a full screen fade
		for (; buf < deststop; ++buf)
			*buf = fadetable[*buf];
	}
}

//
// Fade the screen buffer, using a custom COLORMAP lump.
// Split from V_DrawFadeScreen, because that function has
// WAY too many options piled on top of it as is. :V
//
void V_DrawCustomFadeScreen(const char *lump, uint8_t strength)
{
#ifdef HWRENDER
	if (rendermode != render_soft && rendermode != render_none)
	{
		//HWR_DrawCustomFadeScreen(color, strength);
		return;
	}
#endif

	{
		lumpnum_t lumpnum = LUMPERROR;
		lighttable_t *clm = NULL;

		if (lump != NULL)
			lumpnum = W_GetNumForName(lump);
		else
			return;

		if (lumpnum != LUMPERROR)
		{
			clm = static_cast<lighttable_t *>(Z_MallocAlign(COLORMAP_SIZE, PU_STATIC, NULL, 8));
			W_ReadLump(lumpnum, clm);

			if (clm != NULL)
			{
				const uint8_t *fadetable = ((uint8_t *)clm + strength*256);
				const uint8_t *deststop = vid.screens[0] + vid.rowbytes * vid.height;
				uint8_t *buf = vid.screens[0];

				// heavily simplified -- we don't need to know x or y
				// position when we're doing a full screen fade
				for (; buf < deststop; ++buf)
					*buf = fadetable[*buf];

				Z_Free(clm);
				clm = NULL;
			}
		}
	}
}

// Simple translucency with one color, over a set number of lines starting from the top.
void V_DrawFadeConsBack(int32_t plines)
{
	uint8_t *deststop, *buf;

#ifdef HWRENDER // not win32 only 19990829 by Kin
	if (rendermode == render_opengl)
	{
		uint32_t hwcolor = V_GetHWConsBackColor();
		HWR_DrawConsoleBack(hwcolor, plines);
		return;
	}
#endif

	// heavily simplified -- we don't need to know x or y position,
	// just the stop position
	deststop = vid.screens[0] + vid.rowbytes * std::min(plines, vid.height);
	for (buf = vid.screens[0]; buf < deststop; ++buf)
		*buf = consolebgmap[*buf];
}


//
// Invert the entire screen, for Encore fades
//
void V_EncoreInvertScreen(void)
{
#ifdef HWRENDER
	if (rendermode != render_soft && rendermode != render_none)
	{
		HWR_EncoreInvertScreen();
		return;
	}
#endif

	{
		const uint8_t *deststop = vid.screens[0] + vid.rowbytes * vid.height;
		uint8_t *buf = vid.screens[0];

		for (; buf < deststop; ++buf)
		{
			*buf = NearestColor(
				255 - pLocalPalette[*buf].s.red,
				255 - pLocalPalette[*buf].s.green,
				255 - pLocalPalette[*buf].s.blue
			);
		}
	}
}

// Very similar to F_DrawFadeConsBack, except we draw from the middle(-ish) of the screen to the bottom.
void V_DrawPromptBack(int32_t boxheight, int32_t color)
{
	uint8_t *deststop, *buf;

	if (color >= 256 && color < 512)
	{
		if (boxheight < 0)
			boxheight = -boxheight;
		else // 4 lines of space plus gaps between and some leeway
			boxheight = ((boxheight * 4) + (boxheight/2)*5);
		V_DrawFill((BASEVIDWIDTH-(vid.scaledwidth))/2, BASEVIDHEIGHT-boxheight, (vid.scaledwidth),boxheight, (color-256)|V_SNAPTOBOTTOM);
		return;
	}

	boxheight *= vid.dup;

	if (color == INT32_MAX)
		color = cons_backcolor.value;

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		uint32_t hwcolor;
		RGBA_t output;

		switch (color)
		{
			case 0:		output.s.red = 0xff; output.s.green = 0xff; output.s.blue = 0xff;	break; 	// White
			case 1:		output.s.red = 0x80; output.s.green = 0x80; output.s.blue = 0x80;	break; 	// Black
			case 2:		output.s.red = 0xde; output.s.green = 0xb8; output.s.blue = 0x87;	break;	// Sepia
			case 3:		output.s.red = 0x40; output.s.green = 0x20; output.s.blue = 0x10;	break; 	// Brown
			case 4:		output.s.red = 0xfa; output.s.green = 0x80; output.s.blue = 0x72;	break; 	// Pink
			case 5:		output.s.red = 0xff; output.s.green = 0x69; output.s.blue = 0xb4;	break; 	// Raspberry
			case 6:		output.s.red = 0xff; output.s.green = 0x00; output.s.blue = 0x00;	break; 	// Red
			case 7:		output.s.red = 0xff; output.s.green = 0xd6; output.s.blue = 0x83;	break;	// Creamsicle
			case 8:		output.s.red = 0xff; output.s.green = 0x80; output.s.blue = 0x00;	break; 	// Orange
			case 9:		output.s.red = 0xda; output.s.green = 0xa5; output.s.blue = 0x20;	break; 	// Gold
			case 10:	output.s.red = 0x80; output.s.green = 0x80; output.s.blue = 0x00;	break; 	// Yellow
			case 11:	output.s.red = 0x00; output.s.green = 0xff; output.s.blue = 0x00;	break; 	// Emerald
			case 12:	output.s.red = 0x00; output.s.green = 0x80; output.s.blue = 0x00;	break; 	// Green
			case 13:	output.s.red = 0x40; output.s.green = 0x80; output.s.blue = 0xff;	break; 	// Cyan
			case 14:	output.s.red = 0x46; output.s.green = 0x82; output.s.blue = 0xb4;	break; 	// Steel
			case 15:	output.s.red = 0x1e; output.s.green = 0x90; output.s.blue = 0xff;	break;	// Periwinkle
			case 16:	output.s.red = 0x00; output.s.green = 0x00; output.s.blue = 0xff;	break; 	// Blue
			case 17:	output.s.red = 0xff; output.s.green = 0x00; output.s.blue = 0xff;	break; 	// Purple
			case 18:	output.s.red = 0xee; output.s.green = 0x82; output.s.blue = 0xee;	break; 	// Lavender
			// Default green
			default:	output.s.red = 0x00; output.s.green = 0x80; output.s.blue = 0x00;	break;
		}

		V_CubeApply(&output);
		hwcolor = (output.s.red << 24) | (output.s.green << 16) | (output.s.blue << 8);
		HWR_DrawTutorialBack(hwcolor, boxheight);
		return;
	}
#endif


	CON_SetupBackColormapEx(color, true);

	// heavily simplified -- we don't need to know x or y position,
	// just the start and stop positions
	buf = deststop = vid.screens[0] + vid.rowbytes * vid.height;
	if (boxheight < 0)
		buf += vid.rowbytes * boxheight;
	else // 4 lines of space plus gaps between and some leeway
		buf -= vid.rowbytes * ((boxheight * 4) + (boxheight/2)*5);
	for (; buf < deststop; ++buf)
		*buf = promptbgmap[*buf];
}

// Gets string colormap, used for 0x80 color codes
//
uint8_t *V_GetStringColormap(int32_t colorflags)
{
#if 0 // perfect
	switch ((colorflags & V_CHARCOLORMASK) >> V_CHARCOLORSHIFT)
	{
	case  1: // 0x81, purple
		return purplemap;
	case  2: // 0x82, yellow
		return yellowmap;
	case  3: // 0x83, green
		return greenmap;
	case  4: // 0x84, blue
		return bluemap;
	case  5: // 0x85, red
		return redmap;
	case  6: // 0x86, gray
		return graymap;
	case  7: // 0x87, orange
		return orangemap;
	case  8: // 0x88, sky
		return skymap;
	case  9: // 0x89, lavender
		return lavendermap;
	case 10: // 0x8A, gold
		return goldmap;
	case 11: // 0x8B, aqua-green
		return aquamap;
	case 12: // 0x8C, magenta
		return magentamap;
	case 13: // 0x8D, pink
		return pinkmap;
	case 14: // 0x8E, brown
		return brownmap;
	case 15: // 0x8F, tan
		return tanmap;
	default: // reset
		return NULL;
	}
#else // optimised
	colorflags = ((colorflags & V_CHARCOLORMASK) >> V_CHARCOLORSHIFT);
	if (!colorflags || colorflags > 15) // int32_t is signed, but V_CHARCOLORMASK is a very restrictive mask.
		return NULL;
	return (purplemap+((colorflags-1)<<8));
#endif
}

int32_t V_DanceYOffset(int32_t counter)
{
	const int32_t duration = 16;
	const int32_t step = (I_GetTime() + counter) % duration;

	return abs(step - (duration / 2)) - (duration / 4);
}

static dboolean V_CharacterValid(font_t *font, int c)
{
	return (c >= 0 && c < font->size && font->font[c] != NULL);
}

// Writes a single character (draw WHITE if bit 7 set)
//
void V_DrawFixedCharacter(fixed_t x, fixed_t y, int32_t c, dboolean lowercaseallowed)
{
	int32_t flags;
	const uint8_t *colormap = V_GetStringColormap(c);

	flags = c & ~(V_CHARCOLORMASK | V_PARAMMASK);
	c &= 0x7f;
	if (lowercaseallowed)
		c -= HU_FONTSTART;
	else
		c = toupper(c) - HU_FONTSTART;
	if (c < 0 || c >= HU_FONTSIZE || !fontv[HU_FONT].font[c])
		return;

	V_DrawFixedPatch(x, y, FRACUNIT, flags, fontv[HU_FONT].font[c], colormap);
}

// Writes a single character for the chat (half scaled). (draw WHITE if bit 7 set)
// 16/02/19: Scratch the scaling thing, chat doesn't work anymore under 2x res -Lat'
//
void V_DrawChatCharacter(int32_t x, int32_t y, int32_t c, dboolean lowercaseallowed, uint8_t *colormap)
{
	int32_t w, flags;
	//const uint8_t *colormap = V_GetStringColormap(c);

	flags = c & ~(V_CHARCOLORMASK | V_PARAMMASK);
	c &= 0x7f;
	if (lowercaseallowed)
		c -= HU_FONTSTART;
	else
		c = toupper(c) - HU_FONTSTART;
	if (c < 0 || c >= HU_FONTSIZE || !fontv[HU_FONT].font[c])
		return;

	w = fontv[HU_FONT].font[c]->width / 2;
	if (x + w > vid.width)
		return;

	V_DrawFixedPatch(x*FRACUNIT, y*FRACUNIT, FRACUNIT/2, flags, fontv[HU_FONT].font[c], colormap);
}

// V_TitleCardStringWidth
// Get the string's width using the titlecard font.
int32_t V_TitleCardStringWidth(const char *str)
{
	int32_t xoffs = 0;
	const char *ch = str;
	char c;
	patch_t *pp;

	for (;;ch++)
	{
		if (!*ch)
			break;

		if (*ch == '\n')
		{
			xoffs = 0;
			continue;
		}

		c = *ch;
		c = toupper(c);
		c -= LT_FONTSTART;

		// check if character exists, if not, it's a space.
		if (c < 0 || c >= LT_FONTSIZE || !fontv[GTOL_FONT].font[(int32_t)c])
		{
			xoffs += 10;
			continue;
		}

		pp = fontv[GTFN_FONT].font[(int32_t)c];

		xoffs += pp->width-5;
	}

	return xoffs;
}

// V_DrawTitleCardScreen.
// see v_video.h's prototype for more information.
//
void V_DrawTitleCardString(int32_t x, int32_t y, const char *str, int32_t flags, dboolean bossmode, int32_t timer, int32_t threshold)
{

	int32_t xoffs = 0;
	int32_t yoffs = 0;
	int32_t i = 0;

	// per-letter variables
	fixed_t scalex;
	fixed_t offs;
	int32_t let_time;
	int32_t flipflag;
	angle_t fakeang;

	const char *ch = str;
	char c;
	patch_t *pp;
	patch_t *ol;

	x -= 2;	// Account for patch width...

	if (flags & V_SNAPTORIGHT)
		x -= V_TitleCardStringWidth(str);


	for (;;ch++, i++)
	{

		scalex = FRACUNIT;
		offs = 0;
		let_time = timer - i;
		flipflag = 0;

		if (!*ch)
			break;

		if (*ch == '\n')
		{
			xoffs = x;
			yoffs += 32;

			continue;
		}

		c = *ch;

		c = toupper(c);
		c -= LT_FONTSTART;

		// check if character exists, if not, it's a space.
		if (c < 0 || c >= LT_FONTSIZE || !fontv[GTFN_FONT].font[(int32_t)c])
		{
			xoffs += 10;
			continue;
		}

		ol = fontv[GTOL_FONT].font[(int32_t)c];
		pp = fontv[GTFN_FONT].font[(int32_t)c];

		if (bossmode)
		{
			if (let_time <= 0)
				return;
			if (threshold > 0)
			{
				if (threshold > 3)
					return;
				fakeang = (threshold*ANGLE_45)/2;
				scalex = FINECOSINE(fakeang>>ANGLETOFINESHIFT);
			}
			offs = ((FRACUNIT-scalex)*pp->width)/2;
		}
		else if (timer)
		{
			// make letters appear
			if (!threshold)
				;
			else if (let_time < threshold)
			{
				if (let_time <= 0)
					return;	// No reason to continue drawing, none of the next letters will be drawn either.

				// otherwise; scalex must start at 0
				// let's have each letter do 4 spins (360*4 + 90 = 1530 "degrees")
				fakeang = std::min(360 + 90, let_time*41) * ANG1;
				scalex = FINESINE(fakeang>>ANGLETOFINESHIFT);
			}
			else if (!bossmode && let_time > threshold)
			{
				// Make letters disappear...
				let_time -= threshold;

				fakeang = std::max(0, (360+90) - let_time*41)*ANG1;
				scalex = FINESINE(fakeang>>ANGLETOFINESHIFT);
			}

			// Because of how our patches are offset, we need to counter the displacement caused by changing the scale with an offset of our own.
			offs = ((FRACUNIT-scalex)*pp->width)/2;
		}

		// And now, we just need to draw the stuff.
		flipflag = (scalex < 0) ? V_FLIP : 0;

		if (scalex && ol && pp)
		{
			//CONS_Printf("%d\n", (int32_t)c);
			V_DrawStretchyFixedPatch((x + xoffs)*FRACUNIT + offs, (y+yoffs)*FRACUNIT, abs(scalex), FRACUNIT, flags|flipflag, ol, NULL);
			V_DrawStretchyFixedPatch((x + xoffs)*FRACUNIT + offs, (y+yoffs)*FRACUNIT, abs(scalex), FRACUNIT, flags|flipflag, pp, NULL);
		}

		xoffs += pp->width -5;
	}
}


// Precompile a wordwrapped string to any given width.
// This is a muuuch better method than V_WORDWRAP.
char *V_WordWrap(int32_t x, int32_t w, int32_t option, const char *string)
{
	int c;
	size_t chw, i, lastusablespace = 0;
	size_t slen;
	char *newstring = Z_StrDup(string);
	int32_t spacewidth = 4, charwidth = 0;

	slen = strlen(string);

	if (w == 0)
		w = BASEVIDWIDTH;
	w -= x;
	x = 0;

	switch (option & V_SPACINGMASK)
	{
		case V_MONOSPACE:
			spacewidth = 8;
			/* FALLTHRU */
		case V_OLDSPACING:
			charwidth = 8;
			break;
		case V_6WIDTHSPACE:
			spacewidth = 6;
		default:
			break;
	}

	for (i = 0; i < slen; ++i)
	{
		c = newstring[i];
		if ((uint8_t)c & 0x80) //color parsing! -Inuyasha 2.16.09
			continue;

		if (c == '\n')
		{
			x = 0;
			lastusablespace = 0;
			continue;
		}

		if (!(option & V_ALLOWLOWERCASE))
			c = toupper(c);
		c -= HU_FONTSTART;

		if (c < 0 || c >= HU_FONTSIZE || !fontv[HU_FONT].font[c])
		{
			chw = spacewidth;
			lastusablespace = i;
		}
		else
			chw = (charwidth ? charwidth : fontv[HU_FONT].font[c]->width);

		x += chw;

		if (lastusablespace != 0 && x > w)
		{
			newstring[lastusablespace] = '\n';
			i = lastusablespace;
			lastusablespace = 0;
			x = 0;
		}
	}
	return newstring;
}

static inline fixed_t FixedCharacterDim(
		fixed_t  scale,
		fixed_t   chw,
		int32_t    hchw,
		int32_t    dup,
		fixed_t *  cwp)
{
	(void)scale;
	(void)hchw;
	(void)dup;
	(*cwp) = chw;
	return 0;
}

static inline fixed_t VariableCharacterDim(
		fixed_t  scale,
		fixed_t   chw,
		int32_t    hchw,
		int32_t    dup,
		fixed_t *  cwp)
{
	(void)chw;
	(void)hchw;
	(void)dup;
	(*cwp) = FixedMul ((*cwp) << FRACBITS, scale);
	return 0;
}

static inline fixed_t CenteredCharacterDim(
		fixed_t  scale,
		fixed_t   chw,
		int32_t    hchw,
		int32_t    dup,
		fixed_t *  cwp)
{
	int32_t cxoff;
	/*
	For example, center a 4 wide patch to 8 width:
	4/2   = 2
	8/2   = 4
	4 - 2 = 2 (our offset)
	2 + 4 = 6 = 8 - 2 (equal space on either side)
	*/
	cxoff  = hchw -((*cwp) >> 1 );
	(*cwp) = chw;
	return FixedMul (( cxoff * dup )<< FRACBITS, scale);
}

static inline fixed_t BunchedCharacterDim(
		fixed_t  scale,
		fixed_t   chw,
		int32_t    hchw,
		int32_t    dup,
		fixed_t *  cwp)
{
	(void)chw;
	(void)hchw;
	(void)dup;
	(*cwp) = FixedMul (std::max (1, (*cwp) - 1) << FRACBITS, scale);
	return 0;
}

typedef struct
{
	fixed_t    chw;
	fixed_t spacew;
	fixed_t    lfh;
	fixed_t (*dim_fn)(fixed_t,fixed_t,int32_t,int32_t,fixed_t *);
	uint8_t button_yofs;
} fontspec_t;

static void V_GetFontSpecification(int fontno, int32_t flags, fontspec_t *result)
{
	/*
	Hardcoded until a better system can be implemented
	for determining how fonts space.
	*/

	// All other properties are guaranteed to be set
	result->chw = 0;
	result->button_yofs = 0;

	const int32_t spacing = ( flags & V_SPACINGMASK );

	switch (fontno)
	{
		default:
		case HU_FONT:
			result->spacew = 4;
			switch (spacing)
			{
				case V_MONOSPACE:
					result->spacew = 8;
					/* FALLTHRU */
				case V_OLDSPACING:
					result->chw    = 8;
					break;
				case V_6WIDTHSPACE:
					result->spacew = 6;
			}
			break;
		case TINY_FONT:
			result->spacew = 2;
			switch (spacing)
			{
				case V_MONOSPACE:
					result->spacew = 5;
					/* FALLTHRU */
				case V_OLDSPACING:
					result->chw    = 5;
					break;
				case V_6WIDTHSPACE:
					result->spacew = 3;
					break;
			}
			break;
		case LT_FONT:
			result->spacew = 12;
			break;
		case CRED_FONT:
			result->spacew = 16;
			break;
	}
	switch (fontno)
	{
		default:
		case HU_FONT:
		case TINY_FONT:
		case KART_FONT:
			result->lfh = 12;
			break;
		case LT_FONT:
		case CRED_FONT:
			result->lfh    = 12;
			break;
	}

	switch (fontno)
	{
		case TINY_FONT:
		{
			if (result->chw)
				result->dim_fn = FixedCharacterDim;
			else
			{
				/* Reuse this flag for the alternate bunched-up spacing. */
				if (( flags & V_6WIDTHSPACE ))
					result->dim_fn = BunchedCharacterDim;
				else
					result->dim_fn = VariableCharacterDim;
			}
			break;
		}
		default:
		{
			if (result->chw)
				result->dim_fn = CenteredCharacterDim;
			else
				result->dim_fn = VariableCharacterDim;
			break;
		}
	}

	switch (fontno)
	{
		case HU_FONT:
			result->button_yofs = 4;
			break;
		default:
			result->button_yofs = 2;
			break;
	}
}

static uint8_t V_GetButtonCodeWidth(gamecontrols_e control)
{
	uint8_t x = 16;
	int32_t keynum = 0;

	switch (control)
	{
	case gc_aimforward:
		keynum = G_KeynumFromGamecontrol(gc_aimforward);
		break;
	case gc_aimbackward:
		keynum = G_KeynumFromGamecontrol(gc_aimbackward);
		break;
	case gc_turnleft:
		keynum = G_KeynumFromGamecontrol(gc_turnleft);
		break;
	case gc_turnright:
		keynum = G_KeynumFromGamecontrol(gc_turnright);
		break;
	case gc_drift:
		keynum = G_KeynumFromGamecontrol(gc_drift);
		break;
	case gc_fire:
		keynum = G_KeynumFromGamecontrol(gc_fire);
		break;
	case gc_systemmenu:
		keynum = G_KeynumFromGamecontrol(gc_systemmenu);
		break;
	case gc_lookback:
		keynum = G_KeynumFromGamecontrol(gc_lookback);
		break;
	case gc_accelerate:
		keynum = G_KeynumFromGamecontrol(gc_accelerate);
		break;
	case gc_brake:
		keynum = G_KeynumFromGamecontrol(gc_brake);
		break;
	case gc_horncode:
		keynum = G_KeynumFromGamecontrol(gc_horncode);
		break;
	case gc_custom1:
		keynum = G_KeynumFromGamecontrol(gc_custom1);
		break;
	case gc_custom2:
		keynum = G_KeynumFromGamecontrol(gc_custom2);
		break;
	case gc_custom3:
		keynum = G_KeynumFromGamecontrol(gc_custom3);
		break;
	default:
		break;
	}

	if (G_SupportedGamePadButtonDrawing(keynum))
	{
		return x;
	}
	else
	{
		int32_t length = V_SmallStringWidth(G_KeynumToString(keynum), 0);

		return std::max<int32_t>(x, length + (length / 2));
	}
}

typedef struct
{
	int32_t     hchw;/* half-width for centering */

	int32_t     dup;

	fixed_t  right;
	fixed_t    bot;

	font_t   *font;

	dboolean uppercase;
	dboolean notcolored;
	dboolean vflip;

	dboolean   dance;
	dboolean nodanceoverride;
	int32_t     dancecounter;

	fixed_t cx, cy;

	fixed_t cxoff, cyoff;
	fixed_t cw;

	fixed_t   left;

	int c;
} stringdraw_t;

typedef enum
{
	STRINGDRAW_BREAK = -1,
	STRINGDRAW_RETURN,
	STRINGDRAW_NORMAL,
} stringdraw_e;

#define DRAWBUTTON(buttoncode, state) \
	sd->cw = V_GetButtonCodeWidth(buttoncode) * sd->dup; \
	sd->cxoff = (*fontspec->dim_fn)(scale, fontspec->chw, sd->hchw, sd->dup, &sd->cw); \
	srb2::Draw( \
	FixedToFloat(sd->cx + sd->cxoff), \
	FixedToFloat(sd->cy + sd->cyoff) - ((fontspec->button_yofs) * sd->dup)) \
	.flags(flags) \
	.button(buttoncode, state); \
	sd->cx += sd->cw; \

#define BUTTONCODE(buttonname, buttoncode) \
	if (fastcmp(tagstring, #buttonname)) \
	{ \
		DRAWBUTTON(buttoncode, false) \
	} \
	else if (fastcmp(tagstring, va("%s_pressed", #buttonname))) \
	{ \
		DRAWBUTTON(buttoncode, true) \
	} \
	else if (fastcmp(tagstring, va("%s_animated", #buttonname))) \
	{ \
		DRAWBUTTON(buttoncode, {}) \
	}

#define HANDLECOLOR(colorname, hexcode) \
if (fastcmp(tagstring, #colorname)) \
{ \
	if (sd->notcolored) \
	{ \
		colormap = V_GetStringColormap( \
		( ( hexcode & 0x7f )<< V_CHARCOLORSHIFT )& \
		V_CHARCOLORMASK); \
	} \
}

static stringdraw_e drawtexttag(
	stringdraw_t *sd,
	fixed_t x,
	fixed_t y,
	int32_t flags,
	fixed_t scale,
	char *tagstring,
	const char *originalstring,
	const uint8_t *&colormap,
	fontspec_t *fontspec)
{
	(void)y;
	(void)originalstring;

	BUTTONCODE(up, gc_aimforward)
	else BUTTONCODE(down, gc_aimbackward)
	else BUTTONCODE(left, gc_turnright)
	else BUTTONCODE(right, gc_turnleft)
	else BUTTONCODE(lookback, gc_lookback)
	else BUTTONCODE(drift, gc_drift)
	else BUTTONCODE(item, gc_fire)
	else BUTTONCODE(systemmenu, gc_systemmenu)
	else BUTTONCODE(accel, gc_accelerate)
	else BUTTONCODE(brake, gc_brake)
	else BUTTONCODE(horn, gc_horncode)
	else BUTTONCODE(custom1, gc_custom1)
	else BUTTONCODE(custom2, gc_custom2)
	else BUTTONCODE(custom3, gc_custom3)
	else HANDLECOLOR(white, 0x80)
	else HANDLECOLOR(purple, 0x81)
	else HANDLECOLOR(yellow, 0x82)
	else HANDLECOLOR(green, 0x83)
	else HANDLECOLOR(blue, 0x84)
	else HANDLECOLOR(red, 0x85)
	else HANDLECOLOR(gray, 0x86)
	else HANDLECOLOR(orange, 0x87)
	else HANDLECOLOR(sky, 0x88)
	else HANDLECOLOR(lavender, 0x89)
	else HANDLECOLOR(gold, 0x8A)
	else HANDLECOLOR(aqua, 0x8B)
	else HANDLECOLOR(magenta, 0x8C)
	else HANDLECOLOR(pink, 0x8D)
	else HANDLECOLOR(brown, 0x8E)
	else HANDLECOLOR(tan, 0x8F)
	else if (fastcmp(tagstring, "newline"))
	{
		sd->cy += fontspec->lfh;
		if (sd->cy >= sd->bot)
			return STRINGDRAW_RETURN;
		sd->cx  =   x;
	}

	return STRINGDRAW_NORMAL;
}

#undef DRAWBUTTON
#undef BUTTONCODE
#undef HANDLECOLOR

static stringdraw_e drawtexttagwidth(
	stringdraw_t *sd,
	int32_t flags,
	fixed_t scale,
	char *tagstring,
	const char *originalstring,
	fontspec_t *fontspec)
{
	(void)flags;
	(void)originalstring;
	(void)fontspec;
#define BUTTONCODE(buttonname, buttoncode) \
	if (fastcmp(tagstring, #buttonname)) \
	{ \
		sd->cw = V_GetButtonCodeWidth(buttoncode) * sd->dup; \
		sd->cx += sd->cw * scale; \
		sd->right = sd->cx; \
	} \
	else if (fastcmp(tagstring, va("%s_pressed", #buttonname))) \
	{ \
		sd->cw = V_GetButtonCodeWidth(buttoncode) * sd->dup; \
		sd->cx += sd->cw * scale; \
		sd->right = sd->cx; \
	} \
	else if (fastcmp(tagstring, va("%s_animated", #buttonname))) \
	{ \
		sd->cw = V_GetButtonCodeWidth(buttoncode) * sd->dup; \
		sd->cx += sd->cw * scale; \
		sd->right = sd->cx + (sd->cw * scale); \
	}

	BUTTONCODE(up, gc_aimforward)
	else BUTTONCODE(down, gc_aimbackward)
	else BUTTONCODE(left, gc_turnright)
	else BUTTONCODE(right, gc_turnleft)
	else BUTTONCODE(lookback, gc_lookback)
	else BUTTONCODE(drift, gc_drift)
	else BUTTONCODE(item, gc_fire)
	else BUTTONCODE(systemmenu, gc_systemmenu)
	else BUTTONCODE(accel, gc_accelerate)
	else BUTTONCODE(brake, gc_brake)
	else BUTTONCODE(horn, gc_horncode)
	else BUTTONCODE(custom1, gc_custom1)
	else BUTTONCODE(custom2, gc_custom2)
	else BUTTONCODE(custom3, gc_custom3)
#undef BUTTONCODE
	else if (fastcmp(tagstring, "newline"))
	{
		sd->cx  =  0;
	}

	return STRINGDRAW_NORMAL;
}

void V_DrawStringScaledEx(
		fixed_t    x,
		fixed_t    y,
		fixed_t      scale,
		fixed_t     vscale,
		fixed_t spacescale,
		fixed_t    lfscale,
		int32_t      flags,
		const uint8_t *colormap,
		int        fontno,
		const char *s)
{
	stringdraw_t sd = {0};

	sd.uppercase  = !( flags & V_ALLOWLOWERCASE );
	flags	&= ~(V_FLIP);/* These two (V_ALLOWLOWERCASE) share a bit. */

	sd.dance           = (flags & V_STRINGDANCE) != 0;
	sd.nodanceoverride = !sd.dance;
	sd.dancecounter    = 0;
	sd.vflip = (flags & V_VFLIP) != 0;

	/* Some of these flags get overloaded in this function so
	   don't pass them on. */
	flags &= ~(V_PARAMMASK);

	if (sd.vflip) flags |= V_VFLIP;

	if (colormap == NULL)
	{
		colormap   =  V_GetStringColormap(( flags & V_CHARCOLORMASK ));
	}

	sd.notcolored = !colormap;

	sd.font       = &fontv[fontno];

	fontspec_t fontspec;

	V_GetFontSpecification(fontno, flags, &fontspec);

	sd.hchw     = fontspec.chw >> 1;

	fontspec.chw    <<= FRACBITS;
	fontspec.spacew <<= FRACBITS;
	fontspec.lfh    <<= FRACBITS;

#define Mul( id, scale ) ( id = FixedMul (scale, id) )
	Mul    (fontspec.chw,      scale);
	Mul (fontspec.spacew,      scale);
	Mul    (fontspec.lfh,      scale);

	Mul (fontspec.spacew, spacescale);
	Mul    (fontspec.lfh,    lfscale);
#undef  Mul

	if (( flags & V_NOSCALESTART ))
	{
		sd.dup      = vid.dup;

		sd.hchw     *=     sd.dup;

		fontspec.chw      *=     sd.dup;
		fontspec.spacew   *=     sd.dup;
		fontspec.lfh      *=     sd.dup;

		sd.right     = vid.width;
	}
	else
	{
		sd.dup      = 1;

		sd.right     = ( vid.scaledwidth );
		if (!( flags & V_SNAPTOLEFT ))
		{
			sd.left   = ( sd.right - BASEVIDWIDTH )/ 2;/* left edge of drawable area */
			sd.right -= sd.left;
		}
	}

	sd.right      <<=               FRACBITS;
	sd.bot          = vid.height << FRACBITS;

	sd.cx = x;
	sd.cy = y;
	sd.cyoff = 0;

	for (; ( sd.c = *s ); ++s, ++sd.dancecounter)
	{
		switch (sd.c)
		{
			case '\n':
				sd.cy += fontspec.lfh;
				if (sd.cy >= sd.bot)
					return;
				sd.cx  =   x;
				break;
			case '<': // Control Codes
			{
				if (flags & V_USETEXTTAG)
				{
					char temp[1024] = {};
					strncpy(temp, s, strlen(s));
					char *end = strchr(temp, '>');

					// No end why bother.
					if (end != NULL)
					{
						int32_t size = end - temp;
						char temp2[1024] = {};

						strncpy(temp2, temp+1, size-1);

						stringdraw_e sde = drawtexttag(&sd, x, y, flags, scale, temp2, s, colormap, &fontspec);

						if (sde == STRINGDRAW_RETURN)
						{
							return;
						}
						else if (sde == STRINGDRAW_BREAK)
						{
							break;
						}

						// Now skip past this part of the string.
						s += size;
						break;
					}
				}
			}
			default:
				if (sd.c &  0x80)
				{
					if (sd.notcolored)
					{
						colormap = V_GetStringColormap(
								( ( sd.c & 0x7f )<< V_CHARCOLORSHIFT )&
								V_CHARCOLORMASK);
					}
					if (sd.nodanceoverride)
					{
						sd.dance = false;
						sd.cyoff = 0;
					}
				}
				else if (sd.c == V_STRINGDANCE)
				{
					sd.dance = true;
				}
				else if (sd.cx < sd.right)
				{
					if (sd.uppercase)
					{
						sd.c = toupper(sd.c);
					}
					else if (V_CharacterValid(sd.font, sd.c - sd.font->start) == false)
					{
						// Try the other case if it doesn't exist
						if (sd.c >= 'A' && sd.c <= 'Z')
						{
							sd.c = tolower(sd.c);
						}
						else if (sd.c >= 'a' && sd.c <= 'z')
						{
							sd.c = toupper(sd.c);
						}
					}

					if (sd.dance)
					{
						sd.cyoff = V_DanceYOffset(sd.dancecounter) * FRACUNIT;
					}

					sd.c -= sd.font->start;
					if (V_CharacterValid(sd.font, sd.c) == true)
					{
						// Remove offsets from patch
						fixed_t patchxofs = LSBF_SHORT (sd.font->font[sd.c]->leftoffset) * sd.dup * scale;
						sd.cw = LSBF_SHORT (sd.font->font[sd.c]->width) * sd.dup;
						sd.cxoff = (*fontspec.dim_fn)(scale, fontspec.chw, sd.hchw, sd.dup, &sd.cw);
						V_DrawStretchyFixedPatch(sd.cx + sd.cxoff + patchxofs, sd.cy + sd.cyoff, scale, vscale,
								flags, sd.font->font[sd.c], colormap);
						sd.cx += sd.cw;
					}
					else
					{
						sd.cx += fontspec.spacew;
					}
				}
		}
	}
}

fixed_t V_SubStringScaledWidth(
		fixed_t      scale,
		fixed_t spacescale,
		fixed_t    lfscale,
		int32_t      flags,
		int        fontno,
		int32_t length,
		const char *s)
{
	if (s == NULL)
		return 0;

	stringdraw_t sd = {0};

	int32_t i;

	fixed_t fullwidth = 0;

	if (length < 0)
		length = strlen(s);

	sd.uppercase  = !( flags & V_ALLOWLOWERCASE );
	flags	&= ~(V_FLIP);/* These two (V_ALLOWLOWERCASE) share a bit. */

	sd.font       = &fontv[fontno];

	fontspec_t fontspec;

	V_GetFontSpecification(fontno, flags, &fontspec);

	sd.hchw     = fontspec.chw >> 1;

	fontspec.chw    <<= FRACBITS;
	fontspec.spacew <<= FRACBITS;

#define Mul( id, scale ) ( id = FixedMul (scale, id) )
	Mul    (fontspec.chw,      scale);
	Mul (fontspec.spacew,      scale);
	Mul    (fontspec.lfh,      scale);

	Mul (fontspec.spacew, spacescale);
	Mul    (fontspec.lfh,    lfscale);
#undef  Mul

	if (( flags & V_NOSCALESTART ))
	{
		sd.dup      = vid.dup;

		sd.hchw     *=     sd.dup;

		fontspec.chw      *=     sd.dup;
		fontspec.spacew   *=     sd.dup;
		fontspec.lfh      *=     sd.dup;
	}
	else
	{
		sd.dup      = 1;
	}

	sd.cx = 0;
	sd.right = 0;

	for (i = 0; ( (sd.c = *s) && i < length ); ++s, i++)
	{
		switch (sd.c)
		{
			case '\n':
				sd.cx  =   0;
				break;
			case '<': // Control Codes
			{
				if (flags & V_USETEXTTAG)
				{
					char temp[1024] = {};
					strncpy(temp, s, strlen(s));
					char *end = strchr(temp, '>');

					// No end why bother.
					if (end != NULL)
					{
						int32_t size = end - temp;
						char temp2[1024] = {};

						strncpy(temp2, temp+1, size-1);

						stringdraw_e sde = drawtexttagwidth(&sd, flags, scale, temp2, s, &fontspec);

						if (sde == STRINGDRAW_RETURN)
						{
							return 0;
						}
						else if (sde == STRINGDRAW_BREAK)
						{
							break;
						}

						// Now skip past this part of the string.
						s += size;
						break;
					}
				}
			}
			default:
				if (( sd.c & 0x80 ) || sd.c == V_STRINGDANCE)
					continue;

				if (sd.uppercase)
				{
					sd.c = toupper(sd.c);
				}
				else if (V_CharacterValid(sd.font, sd.c - sd.font->start) == false)
				{
					// Try the other case if it doesn't exist
					if (sd.c >= 'A' && sd.c <= 'Z')
					{
						sd.c = tolower(sd.c);
					}
					else if (sd.c >= 'a' && sd.c <= 'z')
					{
						sd.c = toupper(sd.c);
					}
				}

				sd.c -= sd.font->start;
				if (V_CharacterValid(sd.font, sd.c) == true)
				{
					sd.cw = LSBF_SHORT (sd.font->font[sd.c]->width) * sd.dup;

					// How bunched dims work is by incrementing cx slightly less than a full character width.
					// This causes the next character to be drawn overlapping the previous.
					// We need to count the full width to get the rightmost edge of the string though.
					sd.right = sd.cx + (sd.cw * scale);

					(*fontspec.dim_fn)(scale, fontspec.chw, sd.hchw, sd.dup, &sd.cw);
					sd.cx += sd.cw;
				}
				else
					sd.cx += fontspec.spacew;
		}

		fullwidth = std::max<fixed_t>(sd.right, std::max(sd.cx, fullwidth));
	}

	return fullwidth;
}

fixed_t V_SubStringScaledLengthToFit(
	fixed_t      scale,
	fixed_t spacescale,
	fixed_t    lfscale,
	int32_t      flags,
	int        fontno,
	int32_t length,
	const char *s)
{
	if (s == NULL)
		return 0;

	stringdraw_t sd = {0};

	int32_t i;

	fixed_t fullwidth = 0;

	if (length < 0)
		length = strlen(s);

	sd.uppercase  = !( flags & V_ALLOWLOWERCASE );
	flags	&= ~(V_FLIP);/* These two (V_ALLOWLOWERCASE) share a bit. */

	sd.font       = &fontv[fontno];

	fontspec_t fontspec;

	V_GetFontSpecification(fontno, flags, &fontspec);

	sd.hchw     = fontspec.chw >> 1;

	fontspec.chw    <<= FRACBITS;
	fontspec.spacew <<= FRACBITS;

#define Mul( id, scale ) ( id = FixedMul (scale, id) )
	Mul    (fontspec.chw,      scale);
	Mul (fontspec.spacew,      scale);
	Mul    (fontspec.lfh,      scale);

	Mul (fontspec.spacew, spacescale);
	Mul    (fontspec.lfh,    lfscale);
#undef  Mul

	if (( flags & V_NOSCALESTART ))
	{
		sd.dup      = vid.dup;

		sd.hchw     *=     sd.dup;

		fontspec.chw      *=     sd.dup;
		fontspec.spacew   *=     sd.dup;
		fontspec.lfh      *=     sd.dup;
	}
	else
	{
		sd.dup      = 1;
	}

	sd.cx = 0;
	sd.right = 0;

	for (i = 0; ( (sd.c = *s) && i < length ); ++s, i++)
	{
		switch (sd.c)
		{
			case '\n':
				sd.cx  =   0;
				break;
			case '<': // Control Codes
			{
				if (flags & V_USETEXTTAG)
				{
					char temp[1024] = {};
					strncpy(temp, s, strlen(s));
					char *end = strchr(temp, '>');

					// No end why bother.
					if (end != NULL)
					{
						int32_t size = end - temp;
						char temp2[1024] = {};

						strncpy(temp2, temp+1, size-1);

						stringdraw_e sde = drawtexttagwidth(&sd, flags, scale, temp2, s, &fontspec);

						if (sde == STRINGDRAW_RETURN)
						{
							return 0;
						}
						else if (sde == STRINGDRAW_BREAK)
						{
							break;
						}

						// Now skip past this part of the string.
						s += size;
						break;
					}
				}
			}
			default:
				if (( sd.c & 0xF0 ) == 0x80 || sd.c == V_STRINGDANCE)
					continue;

			if (sd.uppercase)
			{
				sd.c = toupper(sd.c);
			}
			else if (V_CharacterValid(sd.font, sd.c - sd.font->start) == false)
			{
				// Try the other case if it doesn't exist
				if (sd.c >= 'A' && sd.c <= 'Z')
				{
					sd.c = tolower(sd.c);
				}
				else if (sd.c >= 'a' && sd.c <= 'z')
				{
					sd.c = toupper(sd.c);
				}
			}

			sd.c -= sd.font->start;
			if (V_CharacterValid(sd.font, sd.c) == true)
			{
				sd.cw = LSBF_SHORT (sd.font->font[sd.c]->width) * sd.dup;

				// How bunched dims work is by incrementing cx slightly less than a full character width.
				// This causes the next character to be drawn overlapping the previous.
				// We need to count the full width to get the rightmost edge of the string though.
				sd.right = sd.cx + (sd.cw * scale);

				(*fontspec.dim_fn)(scale, fontspec.chw, sd.hchw, sd.dup, &sd.cw);
				sd.cx += sd.cw;
			}
			else
				sd.cx += fontspec.spacew;
		}

		fullwidth = std::max<fixed_t>(sd.right, std::max(sd.cx, fullwidth));
	}

	return std::max(i-1, 0);
}

// Modify a string to wordwrap at any given width.
char * V_ScaledWordWrap(
		fixed_t          w,
		fixed_t      scale,
		fixed_t spacescale,
		fixed_t    lfscale,
		int32_t      flags,
		int        fontno,
		const char *s)
{
	stringdraw_t sd = {0};

	sd.uppercase  = !( flags & V_ALLOWLOWERCASE );
	flags	&= ~(V_FLIP);/* These two (V_ALLOWLOWERCASE) share a bit. */

	sd.font       = &fontv[fontno];

	fontspec_t fontspec;

	V_GetFontSpecification(fontno, flags, &fontspec);

	sd.hchw     = fontspec.chw >> 1;

	fontspec.chw    <<= FRACBITS;
	fontspec.spacew <<= FRACBITS;

#define Mul( id, scale ) ( id = FixedMul (scale, id) )
	Mul    (fontspec.chw,      scale);
	Mul (fontspec.spacew,      scale);
	Mul    (fontspec.lfh,      scale);

	Mul (fontspec.spacew, spacescale);
	Mul    (fontspec.lfh,    lfscale);
#undef  Mul

	if (( flags & V_NOSCALESTART ))
	{
		sd.dup      = vid.dup;

		sd.hchw     *=     sd.dup;

		fontspec.chw      *=     sd.dup;
		fontspec.spacew   *=     sd.dup;
		fontspec.lfh      *=     sd.dup;
	}
	else
	{
		sd.dup      = 1;
	}

	sd.cx = 0;
	sd.right = 0;

	size_t reader = 0, writer = 0, startwriter = 0;
	fixed_t cxatstart = 0;

	size_t len = strlen(s) + 1;
	size_t potentialnewlines = 8;
	size_t sparenewlines = potentialnewlines;

	char *newstring = static_cast<char *>(Z_Malloc(len + sparenewlines, PU_STATIC, NULL));

	for (; ( sd.c = s[reader] ); ++reader, ++writer)
	{
		newstring[writer] = s[reader];

		sd.right = 0;

		switch (sd.c)
		{
			case '\n':
				sd.cx  =   0;
				cxatstart = 0;
				startwriter = 0;
				break;
			case '<': // Control Codes
			{
				if (flags & V_USETEXTTAG)
				{
					char temp[1024] = {};
					strncpy(temp, s, strlen(s));
					char *end = strchr(temp, '>');

					// No end why bother.
					if (end != NULL)
					{
						int32_t size = end - temp;
						char temp2[1024] = {};

						strncpy(temp2, temp+1, size-1);

						stringdraw_e sde = drawtexttagwidth(&sd, flags, scale, temp2, s, &fontspec);

						if (sde == STRINGDRAW_RETURN)
						{
							return 0;
						}
						else if (sde == STRINGDRAW_BREAK)
						{
							break;
						}

						// Now skip past this part of the string.
						s += size;
						break;
					}
				}
			}
			default:
				if (( sd.c & 0x80 ) || sd.c == V_STRINGDANCE)
					;
				else
				{
					if (sd.uppercase)
					{
						sd.c = toupper(sd.c);
					}
					else if (V_CharacterValid(sd.font, sd.c - sd.font->start) == false)
					{
						// Try the other case if it doesn't exist
						if (sd.c >= 'A' && sd.c <= 'Z')
						{
							sd.c = tolower(sd.c);
						}
						else if (sd.c >= 'a' && sd.c <= 'z')
						{
							sd.c = toupper(sd.c);
						}
					}

					sd.c -= sd.font->start;
					if (V_CharacterValid(sd.font, sd.c) == true)
					{
						sd.cw = LSBF_SHORT (sd.font->font[sd.c]->width) * sd.dup;

						// How bunched dims work is by incrementing cx slightly less than a full character width.
						// This causes the next character to be drawn overlapping the previous.
						// We need to count the full width to get the rightmost edge of the string though.
						sd.right = sd.cx + (sd.cw * scale);

						(*fontspec.dim_fn)(scale, fontspec.chw, sd.hchw, sd.dup, &sd.cw);
						sd.cx += sd.cw;
					}
					else
					{
						sd.cx += fontspec.spacew;
						cxatstart = sd.cx;
						startwriter = writer;
					}
				}
		}

		// Start trying to wrap if presumed length exceeds the space we have on-screen.
		if (sd.right && sd.right > w)
		{
			if (startwriter != 0)
			{
				newstring[startwriter] = '\n';
				sd.cx -= cxatstart;
				cxatstart = 0;
				startwriter = 0;
			}
			else
			{
				if (sparenewlines == 0)
				{
					sparenewlines = (potentialnewlines *= 2);
					newstring = static_cast<char *>(Z_Realloc(newstring, len + sparenewlines, PU_STATIC, NULL));
				}

				sparenewlines--;
				len++;

				newstring[writer++] = '\n'; // Over-write previous
				sd.cx = sd.cw; // Valid value in the only case right is currently set
				newstring[writer] = s[reader]; // Re-add
			}
		}
	}

	newstring[writer] = '\0';

	return newstring;
}
//

void V_DrawCenteredString(int32_t x, int32_t y, int32_t option, const char *string)
{
	x -= V_StringWidth(string, option)/2;
	V_DrawString(x, y, option, string);
}

void V_DrawRightAlignedString(int32_t x, int32_t y, int32_t option, const char *string)
{
	x -= V_StringWidth(string, option);
	V_DrawString(x, y, option, string);
}

void V_DrawRightAlignedStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string)
{
	x -= V_StringWidth(string, option)*FRACUNIT;
	V_DrawStringAtFixed(x, y, option, string);
}

void V_DrawCenteredSmallString(int32_t x, int32_t y, int32_t option, const char *string)
{
	x -= V_SmallStringWidth(string, option)/2;
	V_DrawSmallString(x, y, option, string);
}

void V_DrawCenteredSmallStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string)
{
	x -= (V_SmallStringWidth(string, option) / 2)<<FRACBITS;
	V_DrawStringScaled(x, y, FRACUNIT/2, FRACUNIT/2, FRACUNIT/2, option, HU_FONT, string);
}

void V_DrawRightAlignedSmallString(int32_t x, int32_t y, int32_t option, const char *string)
{
	x -= V_SmallStringWidth(string, option);
	V_DrawSmallString(x, y, option, string);
}

void V_DrawRightAlignedSmallStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string)
{
	x -= V_SmallStringWidth(string, option)<<FRACBITS;
	V_DrawSmallStringAtFixed(x, y, option, string);
}

void V_DrawCenteredThinString(int32_t x, int32_t y, int32_t option, const char *string)
{
	x -= V_ThinStringWidth(string, option)/2;
	V_DrawThinString(x, y, option, string);
}

void V_DrawRightAlignedThinString(int32_t x, int32_t y, int32_t option, const char *string)
{
	x -= V_ThinStringWidth(string, option);
	V_DrawThinString(x, y, option, string);
}

void V_DrawCenteredThinStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string)
{
	x -= (V_ThinStringWidth(string, option) / 2) * FRACUNIT;
	V_DrawThinStringAtFixed(x, y, option, string);
}

void V_DrawRightAlignedThinStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string)
{
	x -= V_ThinStringWidth(string, option) * FRACUNIT;
	V_DrawThinStringAtFixed(x, y, option, string);
}

void V_DrawCenteredStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string)
{
	x -= (V_StringWidth(string, option) / 2) * FRACUNIT;
	V_DrawStringAtFixed(x, y, option, string);
}

// Draws a number using the PING font thingy.
// TODO: Merge number drawing functions into one with "font name" selection.

int32_t V_DrawPingNumPadded(int32_t x, int32_t y, int32_t flags, int32_t num, uint16_t padding, const uint8_t *colormap)
{
	int32_t w = LSBF_SHORT(fontv[PINGNUM_FONT].font[0]->width);	// this SHOULD always be 5 but I guess custom graphics exist.

	if (flags & V_NOSCALESTART)
		w *= vid.dup;

	if (num < 0)
		num = -num;

	// draw the number
	do
	{
		x -= (w-1);	// Oni wanted their outline to intersect.
		V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, FRACUNIT, flags, fontv[PINGNUM_FONT].font[num%10], colormap);

		if (num) // No need to divide if we're at zero...
			num /= 10;

		if (padding)
			padding--; // Subtract padding
	} while (num || padding);

	return x;
}

int32_t V_DrawPingNumAtFixed(fixed_t x, fixed_t y, int32_t flags, int32_t num, const uint8_t *colormap)
{
	return V_DrawPingNumScaled(x, y, FRACUNIT, flags, num, colormap);
}

int32_t V_DrawPingNumScaled(fixed_t x, fixed_t y, fixed_t scale, int32_t flags, int32_t num, const uint8_t *colormap)
{
	int32_t w = LSBF_SHORT(fontv[PINGNUM_FONT].font[0]->width);

	if (flags & V_NOSCALESTART)
		w *= vid.dup;

	if (num < 0)
		num = -num;

	// draw the number
	do
	{
		x -= (w-1)*scale;
		V_DrawFixedPatch(x, y, scale, flags, fontv[PINGNUM_FONT].font[num%10], colormap);
		num /= 10;
	} while (num);

	return x;
}

// Jaden: Draw a number using the position numbers.
//
void V_DrawRankNum(int32_t x, int32_t y, int32_t flags, int32_t num, int32_t digits, const uint8_t *colormap)
{
	int32_t w = kp_facenum[0]->width - 1;

	if (flags & V_NOSCALESTART)
		w *= vid.dup;

	if (num < 0)
		num = -num;

	// draw the number
	do
	{
		x -= (w - 1);

		V_DrawFixedPatch(x << FRACBITS, y << FRACBITS, FRACUNIT, flags, kp_facenum[num % 10], colormap);
		num /= 10;
	} while (--digits);
}

// Draws a tallnum.  Replaces two functions in y_inter and st_stuff
void V_DrawTallNum(int32_t x, int32_t y, int32_t flags, int32_t num)
{
	int32_t w = LSBF_SHORT(fontv[TALLNUM_FONT].font[0]->width);
	dboolean neg;

	if (flags & V_NOSCALESTART)
		w *= vid.dup;

	if ((neg = num < 0))
		num = -num;

	// draw the number
	do
	{
		x -= w;
		V_DrawScaledPatch(x, y, flags, fontv[TALLNUM_FONT].font[num % 10]);
		num /= 10;
	} while (num);

	// draw a minus sign if necessary
	//if (neg)
		//V_DrawScaledPatch(x - w, y, flags, tallminus); // Tails
}

// Draws a number with a set number of digits.
// Does not handle negative numbers in a special way, don't try to feed it any.
void V_DrawPaddedTallNum(int32_t x, int32_t y, int32_t flags, int32_t num, int32_t digits)
{
	int32_t w = fontv[TALLNUM_FONT].font[0]->width;

	if (flags & V_NOSCALESTART)
		w *= vid.dup;

	if (num < 0)
		num = -num;

	// draw the number
	do
	{
		x -= w;
		V_DrawScaledPatch(x, y, flags, fontv[TALLNUM_FONT].font[num % 10]);
		num /= 10;
	} while (--digits);
}

void V_DrawPaddedTallColorNumAtFixed(fixed_t x, fixed_t y, int32_t flags, int32_t num, int32_t digits, const uint8_t *colormap)
{
	int32_t w = fontv[TALLNUM_FONT].font[0]->width;

	if (flags & V_NOSCALESTART)
		w *= vid.dup;

	if (num < 0)
		num = -num;

	// draw the number
	do
	{
		x -= w*FRACUNIT;
		V_DrawFixedPatch(x, y, FRACUNIT, flags, fontv[TALLNUM_FONT].font[num % 10], colormap);
		num /= 10;
	} while (--digits);
}

// Find string width from lt_font chars
//
int32_t V_LevelNameWidth(const char *string)
{
	int32_t c, w = 0;
	size_t i;

	for (i = 0; i < strlen(string); i++)
	{
		if (string[i] & 0x80)
			continue;
		c = string[i] - LT_FONTSTART;
		if (c < 0 || c >= LT_FONTSIZE || !fontv[LT_FONT].font[c])
			w += 12;
		else
			w += fontv[LT_FONT].font[c]->width;
	}

	return w;
}

// Find max height of the string
//
int32_t V_LevelNameHeight(const char *string)
{
	int32_t c, w = 0;
	size_t i;

	for (i = 0; i < strlen(string); i++)
	{
		c = string[i] - LT_FONTSTART;
		if (c < 0 || c >= LT_FONTSIZE || !fontv[LT_FONT].font[c])
			continue;

		if (fontv[LT_FONT].font[c]->height > w)
			w = fontv[LT_FONT].font[c]->height;
	}

	return w;
}

// Find maximum length for substring taken from current string to fit into given width

//

dboolean *heatshifter = NULL;
int32_t lastheight = 0;
int32_t heatindex[MAXSPLITSCREENPLAYERS] = {0, 0, 0, 0};

//
// V_DoPostProcessor
//
// Perform a particular image postprocessing function.
//

void V_DoPostProcessor(int32_t view, int32_t param)
{
#if NUMSCREENS < 5
	// do not enable image post processing for ARM, SH and MIPS CPUs
	(void)view;
	(void)type;
	(void)param;
#else
	(void)param; // unused motion blur stuff
	int32_t yoffset, xoffset;

#ifdef HWRENDER
	if (rendermode != render_soft)
		return;
#endif

	if (view < 0 || view > 3 || view > r_splitscreen)
		return;

	camera_t *thiscam = &camera[view];

	if (!thiscam->postimgflags)
		return;

	if ((view == 1 && r_splitscreen == 1) || view >= 2)
		yoffset = viewheight;
	else
		yoffset = 0;

	if ((view == 1 || view == 3) && r_splitscreen > 1)
		xoffset = viewwidth;
	else
		xoffset = 0;

	uint8_t *tmpscr = vid.screens[4];
	uint8_t *srcscr = vid.screens[0];

	if (thiscam->postimgflags & POSTIMG_WATER)
	{
		int32_t y;
		// Set disStart to a range from 0 to FINEANGLE, incrementing by 128 per tic
		angle_t disStart = (((leveltime-1)*128) + (R_GetTimeFrac(RTF_LEVEL) / (FRACUNIT/128))) & FINEMASK;
		int32_t newpix;
		int32_t sine;
		//uint8_t *transme = R_GetTranslucencyTable(tr_trans50);

		for (y = yoffset; y < yoffset+viewheight; y++)
		{
			sine = (FINESINE(disStart)*5)>>FRACBITS;
			newpix = abs(sine);

			if (sine < 0)
			{
				memcpy(&tmpscr[(y*vid.width)+xoffset+newpix], &srcscr[(y*vid.width)+xoffset], viewwidth-newpix);

				// Cleanup edge
				while (newpix)
				{
					tmpscr[(y*vid.width)+xoffset+newpix] = srcscr[(y*vid.width)+xoffset];
					newpix--;
				}
			}
			else
			{
				memcpy(&tmpscr[(y*vid.width)+xoffset+0], &srcscr[(y*vid.width)+xoffset+sine], viewwidth-newpix);

				// Cleanup edge
				while (newpix)
				{
					tmpscr[(y*vid.width)+xoffset+viewwidth-newpix] = srcscr[(y*vid.width)+xoffset+(viewwidth-1)];
					newpix--;
				}
			}

			/*
			 *			Unoptimized version
			 *			for (x = 0; x < vid.width; x++)
			 *			{
			 *				newpix = (x + sine);
			 *
			 *				if (newpix < 0)
			 *					newpix = 0;
			 *				else if (newpix >= vid.width)
			 *					newpix = vid.width-1;
			 *
			 *				tmpscrwater[y*vid.width + x] = srcscr[y*vid.width+newpix]; // *(transme + (srcscr[y*vid.width+x]<<8) + srcscr[y*vid.width+newpix]);
		}*/
			disStart += 22;//the offset into the displacement map, increment each game loop
			disStart &= FINEMASK; //clip it to FINEMASK
		}

		uint8_t *tmp = tmpscr;
		tmpscr = srcscr;
		srcscr = tmp;
	}
	else if (thiscam->postimgflags & POSTIMG_HEAT) // Heat wave
	{
		int32_t y;

		// Make sure table is built
		if (heatshifter == NULL || lastheight != viewheight)
		{
			Z_Free(heatshifter);
			heatshifter = static_cast<dboolean *>(Z_Calloc(viewheight * sizeof(dboolean), PU_STATIC, NULL));

			for (y = 0; y < viewheight; y++)
			{
				if (M_RandomChance(FRACUNIT/8)) // 12.5%
					heatshifter[y] = true;
			}

			heatindex[0] = heatindex[1] = heatindex[2] = heatindex[3] = 0;
			lastheight = viewheight;
		}

		for (y = yoffset; y < yoffset+viewheight; y++)
		{
			if (heatshifter[heatindex[view]++])
			{
				// Shift this row of pixels to the right by 2
				tmpscr[(y*vid.width)+xoffset] = srcscr[(y*vid.width)+xoffset];
				memcpy(&tmpscr[(y*vid.width)+xoffset], &srcscr[(y*vid.width)+xoffset+vid.dup], viewwidth-vid.dup);
			}
			else
				memcpy(&tmpscr[(y*vid.width)+xoffset], &srcscr[(y*vid.width)+xoffset], viewwidth);

			heatindex[view] %= viewheight;
		}

		if (renderisnewtic) // This isn't interpolated... but how do you interpolate a one-pixel shift?
		{
			heatindex[view]++;
			heatindex[view] %= vid.height;
		}

		uint8_t *tmp = tmpscr;
		tmpscr = srcscr;
		srcscr = tmp;
	}

	/*if (thiscam->postimg & POSTIMG_MOTION) // Motion Blur!
	{
		int32_t x, y;

		// TODO: Add a postimg_param so that we can pick the translucency level...
		uint8_t *transme = transtables + ((param-1)<<FF_TRANSSHIFT);

		for (y = yoffset; y < yoffset+viewheight; y++)
		{
			for (x = xoffset; x < xoffset+viewwidth; x++)
				tmpscr[y*vid.width + x] =     colormaps[*(transme     + (srcscr   [(y*vid.width)+x ] <<8) + (tmpscr[(y*vid.width)+x]))];
		}
	}*/

	if ((thiscam->postimgflags & POSTIMG_FLIP) && !(thiscam->postimgflags & POSTIMG_MIRROR)) // Flip the screen upside-down
	{
		int32_t y, y2;

		for (y = yoffset, y2 = yoffset+viewheight - 1; y < yoffset+viewheight; y++, y2--)
			memcpy(&tmpscr[(y2*vid.width)+xoffset], &srcscr[(y*vid.width)+xoffset], viewwidth);

		uint8_t *tmp = tmpscr;
		tmpscr = srcscr;
		srcscr = tmp;
	}
	else if ((thiscam->postimgflags & POSTIMG_MIRROR) && !(thiscam->postimgflags & POSTIMG_FLIP)) // Flip the screen on the x axis
	{
		int32_t y, x, x2;

		for (y = yoffset; y < yoffset+viewheight; y++)
			for (x = xoffset, x2 = xoffset+((viewwidth)-1); x < xoffset+(viewwidth); x++, x2--)
				tmpscr[y*vid.width + x2] = srcscr[y*vid.width + x];

		uint8_t *tmp = tmpscr;
		tmpscr = srcscr;
		srcscr = tmp;
	}
	else if ((thiscam->postimgflags & POSTIMG_MIRROR) && (thiscam->postimgflags & POSTIMG_FLIP)) // Flip the screen upside-down and on the x axis
	{
		int32_t y, x;

		for (y = yoffset; y < yoffset + viewheight; y++)
			for (x = xoffset; x < xoffset + viewwidth; x++)
				tmpscr[((yoffset + viewheight - 1 - y) * vid.width) + xoffset + viewwidth - (x - xoffset) - 1] = srcscr[(y * vid.width) + x];

		uint8_t *tmp = tmpscr;
		tmpscr = srcscr;
		srcscr = tmp;
	}

	VID_BlitLinearScreen(srcscr+vid.width*yoffset+xoffset, tmpscr+vid.width*yoffset+xoffset,
						 viewwidth, viewheight, vid.width, vid.width);
#endif
}

// Generates a RGB565 color look-up table
void InitColorLUT(colorlookup_t *lut, RGBA_t *palette, dboolean makecolors)
{
	size_t palsize = (sizeof(RGBA_t) * 256);

	if (!lut->init || memcmp(lut->palette, palette, palsize))
	{
		size_t i;

		lut->init = true;
		memcpy(lut->palette, palette, palsize);

		for (i = 0; i < sizeof(lut->table)/sizeof(*lut->table); i++)
			lut->table[i] = 0xFFFF;

		if (makecolors)
		{
			uint8_t r, g, b;

			for (r = 0; r < 0xFF; r++)
			for (g = 0; g < 0xFF; g++)
			for (b = 0; b < 0xFF; b++)
			{
				i = CLUTINDEX(r, g, b);
				if (lut->table[i] == 0xFFFF)
					lut->table[i] = NearestPaletteColor(r, g, b, palette);
			}
		}
	}
}

uint8_t GetColorLUT(colorlookup_t *lut, uint8_t r, uint8_t g, uint8_t b)
{
	int32_t i = CLUTINDEX(r, g, b);
	if (lut->table[i] == 0xFFFF)
		lut->table[i] = NearestPaletteColor(r, g, b, lut->palette);
	return lut->table[i];
}

uint8_t GetColorLUTDirect(colorlookup_t *lut, uint8_t r, uint8_t g, uint8_t b)
{
	int32_t i = CLUTINDEX(r, g, b);
	return lut->table[i];
}

// V_Init
// old software stuff, buffers are allocated at video mode setup
// here we set the screens[x] pointers accordingly
// WARNING: called at runtime (don't init cvar here)
void V_Init(void)
{
	int32_t i;
	int32_t screensize = vid.rowbytes * vid.height;

	for (i = 0; i < NUMSCREENS; i++)
	{
		if (vid.screens[i])
		{
#if defined(__SSE__)
			aligned_free(vid.screens[i]);
#else
			free(vid.screens[i]);
#endif
		}

		vid.screens[i] = NULL;
	}

	// start address of NUMSCREENS * width*height vidbuffers
	if (screensize > 0)
	{
		for (i = 0; i < NUMSCREENS; i++)
		{
			// we need to allocate these relative to their cpu restrictions to not trigger segfaults
			// TODO: add support for sve and neon
#if defined(__SSE__)
			while (screensize & 15)
				screensize++;
			vid.screens[i] = static_cast<uint8_t *>(aligned_alloc(16, screensize));
#else
			vid.screens[i] = malloc(screensize);
#endif
			memset(vid.screens[i], 0, screensize);
		}
	}

#ifdef DEBUG
	CONS_Debug(DBG_RENDER, "V_Init done:\n");
	for (i = 0; i < NUMSCREENS; i++)
		CONS_Debug(DBG_RENDER, " vid.screens[%d] = %x\n", i, screens[i]);
#endif
}

void V_Recalc(void)
{
	// scale 1,2,3 times in x and y the patches for the menus and overlays...

	// calculated once and for all, used by routines in v_video.c and v_draw.c
	// Set dup based on width or height, whichever is less
	if (((vid.width*FRACUNIT) / BASEVIDWIDTH) < ((vid.height*FRACUNIT) / BASEVIDHEIGHT))
	{
		vid.dup = std::max(vid.width / BASEVIDWIDTH, 1);
		vid.fdup = (vid.width*FRACUNIT) / BASEVIDWIDTH;
	}
	else
	{
		vid.dup = std::max(vid.height / BASEVIDHEIGHT, 1);
		vid.fdup = (vid.height*FRACUNIT) / BASEVIDHEIGHT;
	}

	vid.udup = vid.dup;

	if (loaded_config // this could use a better name, since it is more and indicator that early startup is done and its safe to do sketchy shit now :chaosleep:
	&& (vid.width > 720) && (vid.height > 1280)) // ehhhh well this thing has so many issues, so ill lock it to higher resolutions instead
	{
		vid.dup = FixedDiv(vid.dup, cv_highreshudscale.value);
		vid.fdup = FixedDiv(vid.fdup, cv_highreshudscale.value);
	}

	vid.scaledwidth = (vid.width/vid.dup);
	vid.scaledheight = (vid.height/vid.dup);

	vid.meddup = (uint8_t)(vid.dup >> 1) + 1;
	vid.smalldup = (uint8_t)(vid.dup / 3) + 1;
}
