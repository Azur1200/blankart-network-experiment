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
/// \file  v_video.h
/// \brief Gamma correction LUT

#ifndef __V_VIDEO__
#define __V_VIDEO__

#include "doomdef.h"
#include "doomtype.h"
#include "r_defs.h"
#include "r_main.h"

#if defined(__SSE__)
#ifdef _WIN32
#include <malloc.h>
#define aligned_alloc(align, size) _aligned_malloc(size, align)
#define aligned_free(ptr) _aligned_free(ptr)
#else
#define aligned_free(ptr) free(ptr)
#endif

#include <immintrin.h>
#endif

// SRB2Kart
#include "hu_stuff.h" // fonts

#ifdef __cplusplus
extern "C" {
#endif

//
// VIDEO
//

extern consvar_t cv_ticrate, cv_constextsize,
#ifdef BACKWARDSCOMPATCORRECTION
cv_globalgamma,
#endif
cv_globalbrightness, cv_globalsaturation,
cv_rhue, cv_yhue, cv_ghue, cv_chue, cv_bhue, cv_mhue,
cv_rbrightness, cv_ybrightness, cv_gbrightness, cv_cbrightness, cv_bbrightness, cv_mbrightness,
cv_rsaturation, cv_ysaturation, cv_gsaturation, cv_csaturation, cv_bsaturation, cv_msaturation,
cv_palette, cv_palettenum;

// Allocates buffer screens, call before R_Init.
void V_Init(void);

// Recalculates the viddef (dup, etc.) according to the current screen resolution.
void V_Recalc(void);

// Color look-up table
#define CLUTINDEX(r, g, b) (((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)

struct colorlookup_t
{
	dboolean init;
	RGBA_t palette[256];
	uint16_t table[0xFFFF+1];
};

void InitColorLUT(colorlookup_t *lut, RGBA_t *palette, dboolean makecolors);
uint8_t GetColorLUT(colorlookup_t *lut, uint8_t r, uint8_t g, uint8_t b);
uint8_t GetColorLUTDirect(colorlookup_t *lut, uint8_t r, uint8_t g, uint8_t b);

// Loads the correct palette into memory
void V_ReloadPalette(void);

// Set the current RGB palette lookup to use for palettized graphics
void V_SetPalette(int32_t palettenum);

void V_SetPaletteLump(const char *pal);

const char *R_GetPalname(uint16_t num);
const char *GetPalette(void);

extern RGBA_t *pLocalPalette;
extern RGBA_t *pMasterPalette;
extern RGBA_t *pGammaCorrectedPalette;

uint32_t V_GammaCorrect(uint32_t input, double power);
#define V_GammaDecode(input) V_GammaCorrect(input, 2.2f)
#define V_GammaEncode(input) V_GammaCorrect(input, (1/2.2f))

void V_CubeApply(RGBA_t *input);

// Retrieve the ARGB value from a palette color index
#define V_GetColor(color) (pLocalPalette[color&0xFF])
#define V_GetMasterColor(color) (pMasterPalette[color&0xFF])

// Bottom 8 bits are used for parameter (character)
#define V_PARAMMASK          0x000000FF

// Bottom bit is used for screen (patches)
#define V_SCREENMASK          0x0000000F

#define V_STRINGDANCE        0x00000002 // (strings/characters only) funny undertale
#define V_VFLIP              0x00000010 // (patches only) Vertical flip

// flags hacked in scrn (not supported by all functions (see src))
// patch scaling uses bits 9 and 10
#define V_SCALEPATCHSHIFT    8
#define V_SCALEPATCHMASK     0x00000300
#define V_NOSCALEPATCH       0x00000100
#define V_SMALLSCALEPATCH    0x00000200
#define V_MEDSCALEPATCH      0x00000300

// string spacing uses bits 11 and 12
#define V_SPACINGMASK        0x00000C00
#define V_6WIDTHSPACE        0x00000400 // early 2.1 style spacing, variable widths, 6 character space
#define V_OLDSPACING         0x00000800 // Old style spacing, 8 per character 4 per space
#define V_MONOSPACE          0x00000C00 // Don't do width checks on characters, all characters 8 width

// use bits 13-16 for colors
#define V_CHARCOLORSHIFT     12
#define V_CHARCOLORMASK      0x0000F000
// for simplicity's sake, shortcuts to specific colors
#define V_PURPLEMAP          0x00001000
#define V_YELLOWMAP          0x00002000
#define V_GREENMAP           0x00003000
#define V_BLUEMAP            0x00004000
#define V_REDMAP             0x00005000
#define V_GRAYMAP            0x00006000
#define V_ORANGEMAP          0x00007000
#define V_SKYMAP             0x00008000
#define V_LAVENDERMAP        0x00009000
#define V_GOLDMAP            0x0000A000
#define V_AQUAMAP            0x0000B000
#define V_MAGENTAMAP         0x0000C000
#define V_PINKMAP            0x0000D000
#define V_BROWNMAP           0x0000E000
#define V_TANMAP             0x0000F000

// use bits 17-20 for alpha transparency
#define V_ALPHASHIFT         16
#define V_ALPHAMASK          0x000F0000
// define specific translucencies
#define V_10TRANS            0x00010000
#define V_20TRANS            0x00020000
#define V_30TRANS            0x00030000
#define V_40TRANS            0x00040000
#define V_TRANSLUCENT        0x00050000 // TRANS50
#define V_60TRANS            0x00060000
#define V_70TRANS            0x00070000
#define V_80TRANS            0x00080000 // used to be V_8020TRANS
#define V_90TRANS            0x00090000
#define V_HUDTRANSHALF       0x000D0000
#define V_HUDTRANS           0x000E0000 // draw the hud translucent
#define V_HUDTRANSDOUBLE     0x000F0000
// Macros follow
#define V_USERHUDTRANSHALF   ((10-(cv_translucenthud.value/2))<<V_ALPHASHIFT)
#define V_USERHUDTRANS       ((10-cv_translucenthud.value)<<V_ALPHASHIFT)
#define V_USERHUDTRANSDOUBLE ((10-min(cv_translucenthud.value*2, 10))<<V_ALPHASHIFT)

// careful with the 4 bits after V_ALPHAMASK; mauromania inadvertently sets these

#define V_SNAPTOTOP          0x00100000 // for centering
#define V_SNAPTOBOTTOM       0x00200000 // for centering
#define V_SNAPTOLEFT         0x00400000 // for centering
#define V_SNAPTORIGHT        0x00800000 // for centering

// use bits 25-27 for blendmodes
#define V_BLENDSHIFT         24
#define V_BLENDMASK          0x07000000
// preshifted blend flags minus 1 as effects don't distinguish between AST_COPY and AST_TRANSLUCENT
#define V_ADD                ((AST_ADD-1)<<V_BLENDSHIFT) // Additive
#define V_SUBTRACT           ((AST_SUBTRACT-1)<<V_BLENDSHIFT) // Subtractive
#define V_REVERSESUBTRACT    ((AST_REVERSESUBTRACT-1)<<V_BLENDSHIFT) // Reverse subtractive
#define V_MODULATE           ((AST_MODULATE-1)<<V_BLENDSHIFT) // Modulate
#define V_OVERLAY            ((AST_OVERLAY-1)<<V_BLENDSHIFT) // Overlay

#define V_ALLOWLOWERCASE     0x10000000 // (strings only) allow fonts that have lowercase letters to use them
#define V_FLIP               0x10000000 // (patches only) Horizontal flip
#define V_SLIDEIN            0x20000000 // Slide in from the sides on level load, depending on snap flags

#define V_NOSCALESTART       0x40000000 // don't scale x, y, start coords
#define V_SPLITSCREEN        0x80000000 // Add half of screen width or height automatically depending on player number

uint32_t V_GetHUDTranslucency(int32_t scrn);

void V_AdjustXYWithSnap(int32_t *x, int32_t *y, uint32_t options, int32_t dup);

uint8_t V_GetColorFromSkinColorEx(skincolornum_t skincolor, uint8_t idx);
uint8_t V_GetColorFromSkinColorWithBrightness(skincolornum_t skincolor, uint8_t idx, fixed_t brightness);

#define V_GetColorFromSkinColor(skincolor) (V_GetColorFromSkinColorEx(skincolor, 4))

struct cliprect_t
{
	fixed_t left, right, top, bottom;
	int32_t flags;
	dboolean enabled;
};

const cliprect_t *V_GetClipRect(void);
void V_SetClipRect(fixed_t x, fixed_t y, fixed_t w, fixed_t h, int32_t flags);
void V_SetClipRectForPlayer(uint8_t pnum);
void V_ClearClipRect(void);
void V_SaveClipRect(cliprect_t *copy);
void V_RestoreClipRect(const cliprect_t *copy);

// defines for old functions
#define V_DrawPatch(x,y,s,p) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT, s|V_NOSCALESTART|V_NOSCALEPATCH, p, NULL)
#define V_DrawTranslucentMappedPatch(x,y,s,p,c) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT, s, p, c)
#define V_DrawSmallTranslucentMappedPatch(x,y,s,p,c) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT/2, s, p, c)
#define V_DrawTinyTranslucentMappedPatch(x,y,s,p,c) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT/4, s, p, c)
#define V_DrawMappedPatch(x,y,s,p,c) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT, s, p, c)
#define V_DrawSmallMappedPatch(x,y,s,p,c) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT/2, s, p, c)
#define V_DrawTinyMappedPatch(x,y,s,p,c) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT/4, s, p, c)
#define V_DrawScaledPatch(x,y,s,p) V_DrawFixedPatch((x)*FRACUNIT, (y)<<FRACBITS, FRACUNIT, s, p, NULL)
#define V_DrawSmallScaledPatch(x,y,s,p) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT/2, s, p, NULL)
#define V_DrawTinyScaledPatch(x,y,s,p) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT/4, s, p, NULL)
#define V_DrawTranslucentPatch(x,y,s,p) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT, s, p, NULL)
#define V_DrawSmallTranslucentPatch(x,y,s,p) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT/2, s, p, NULL)
#define V_DrawTinyTranslucentPatch(x,y,s,p) V_DrawFixedPatch((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT/4, s, p, NULL)
#define V_DrawSciencePatch(x,y,s,p,sc) V_DrawFixedPatch(x,y,sc,s,p,NULL)
#define V_DrawFixedPatch(x,y,sc,s,p,c) V_DrawStretchyFixedPatch(x,y,sc,sc,s,p,c)
void V_DrawStretchyFixedPatch(fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, int32_t scrn, patch_t *patch, const uint8_t *colormap);
void V_DrawCroppedPatch(fixed_t x, fixed_t y, fixed_t pscale, int32_t scrn, patch_t *patch, fixed_t sx, fixed_t sy, fixed_t w, fixed_t h);

affine_bounding_t* V_GetAffineBounds(const affine_t* transform,
				     patch_t* patch,
					 fixed_t divisor,
				     affine_bounding_t* out,
					 dboolean unrestrict_bound);
void V_DrawAffinePatch(fixed_t x, fixed_t y, const affine_t *transform, int32_t scrn, patch_t *patch, const uint8_t *colormap);
void V_DrawRotatedPatch(fixed_t x, fixed_t y, angle_t angle, fixed_t pscale, fixed_t vscale, int32_t scrn, patch_t *patch, const uint8_t *colormap);

void V_DrawContinueIcon(int32_t x, int32_t y, int32_t flags, int32_t skinnum, uint16_t skincolor);

// Draw a linear block of pixels into the view buffer.
void V_DrawBlock(int32_t x, int32_t y, int32_t scrn, int32_t width, int32_t height, const uint8_t *src);

// fill a box with a single color
#define V_DrawFill(x,y,w,h,c) V_DrawFixedFill((x)<<FRACBITS, (y)<<FRACBITS, (w)<<FRACBITS, (h)<<FRACBITS, c)
void V_DrawFixedFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t c);
void V_DrawFillConsoleMap(int32_t x, int32_t y, int32_t w, int32_t h, int32_t c);
// draw a line from {x1,y1} to {x2,y2}
void V_DrawFixedLine(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2, fixed_t width, int32_t c);
#define V_DrawLine(x1,y1,x2,y2,w,c) V_DrawFixedLine((x1)<<FRACBITS, (y1)<<FRACBITS, (x2)<<FRACBITS, (y2)<<FRACBITS, w, c)
// fill a triangle with a single color
void V_DrawDiag(int32_t x, int32_t y, int32_t wh, int32_t c);
// fill a box with a flat as a pattern
void V_DrawFlatFill(int32_t x, int32_t y, int32_t w, int32_t h, lumpnum_t flatnum);

// HUD translusency functions
uint8_t V_GetHudTransHalf(void);
uint8_t V_GetHudTrans(void);
uint8_t V_GetHudTransDouble(void);


// draw wobbly VHS pause stuff
void V_DrawVhsEffect(dboolean rewind);

// fade down the screen buffer before drawing the menu over
void V_DrawFadeScreen(uint16_t color, uint8_t strength);
// available to lua over my dead body, which will probably happen in this heat
void V_DrawFadeFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t c, uint16_t color, uint8_t strength);

void V_DrawCustomFadeScreen(const char *lump, uint8_t strength);
void V_DrawFadeConsBack(int32_t plines);

void V_EncoreInvertScreen(void);

void V_DrawPromptBack(int32_t boxheight, int32_t color);

/* Convenience macros for leagacy string function macros. */

/* Yes I'm lazy RN, deal with it. */
#define V_DrawStringScaled( x,y,scale,space_scale,linefeed_scale,option,font,string ) \
	V_DrawStringScaledEx(x,y,scale,scale,space_scale,linefeed_scale,option,NULL,font,string)
#define V__DrawOneScaleString( x,y,scale,option,font,string ) \
	V_DrawStringScaled(x,y,scale,FRACUNIT,FRACUNIT,option,font,string)
#define V__DrawDupString( x,y,scale,option,font,string )\
	V__DrawOneScaleString ((x)<<FRACBITS,(y)<<FRACBITS,scale,option,font,string)

int32_t V_DanceYOffset(int32_t counter);

// draw a single character
#define V_DrawCharacter(x, y, c, l) V_DrawFixedCharacter((x)*FRACUNIT, (y)*FRACUNIT, c, l)
void V_DrawFixedCharacter(fixed_t x, fixed_t y, int32_t c, dboolean lowercaseallowed);
// draw a single character, but for the chat
void V_DrawChatCharacter(int32_t x, int32_t y, int32_t c, dboolean lowercaseallowed, uint8_t *colormap);

uint8_t *V_GetStringColormap(int32_t colorflags);

#define V_DrawLevelTitle( x,y,option,string ) \
	V__DrawDupString (x,y,FRACUNIT,option,LT_FONT,string)

// wordwrap a string using the hu_font
char *V_WordWrap(int32_t x, int32_t w, int32_t option, const char *string);

// draw a string using a font
void V_DrawStringScaledEx(
		fixed_t     x,
		fixed_t     y,
		fixed_t           scale,
		fixed_t          vscale,
		fixed_t     space_scale,
		fixed_t  linefeed_scale,
		int32_t       flags,
		const uint8_t *colormap,
		int         font,
		const char *text);

fixed_t V_StringScaledWidth(
		fixed_t      scale,
		fixed_t spacescale,
		fixed_t    lfscale,
		int32_t      flags,
		int        fontno,
		const char *s);

char * V_ScaledWordWrap(
		fixed_t          w,
		fixed_t      scale,
		fixed_t spacescale,
		fixed_t    lfscale,
		int32_t      flags,
		int        fontno,
		const char *s);

// draw a string using the hu_font
#define V_DrawString( x,y,option,string ) \
	V__DrawDupString (x,y,FRACUNIT,option,HU_FONT,string)
#define V_DrawKartString( x,y,option,string ) \
	V__DrawDupString (x,y,FRACUNIT,option,KART_FONT,string)
#define V_DrawScalingKartString( x,y,sc,option,string ) \
	V__DrawDupString (x,y,sc,option,KART_FONT,string)
#define V_DrawKartStringAtFixed( x,y,option,string ) \
	V__DrawOneScaleString (x,y,FRACUNIT,option,KART_FONT,string)
#define V_DrawScalingKartStringAtFixed( x,y,sc,option,string ) \
	V__DrawOneScaleString (x,y,sc,option,KART_FONT,string)
void V_DrawCenteredString(int32_t x, int32_t y, int32_t option, const char *string);
void V_DrawRightAlignedString(int32_t x, int32_t y, int32_t option, const char *string);
void V_DrawRightAlignedStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string);

// draw a string using the hu_font, 0.5x scale
#define V_DrawSmallString( x,y,option,string ) \
	V__DrawDupString (x,y,FRACUNIT>>1,option,HU_FONT,string)
#define V_DrawSmallStringAtFixed( x,y,option,string ) \
	V__DrawOneScaleString (x,y,FRACUNIT>>1,option,HU_FONT,string)
void V_DrawCenteredSmallString(int32_t x, int32_t y, int32_t option, const char *string);
void V_DrawCenteredSmallStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string);
void V_DrawRightAlignedSmallString(int32_t x, int32_t y, int32_t option, const char *string);

// Draw a centered string.
void V_DrawCenteredStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string);

// draw a string using the tny_font
#define V_DrawThinString( x,y,option,string ) \
	V__DrawDupString (x,y,FRACUNIT,option,TINY_FONT,string)
void V_DrawCenteredThinString(int32_t x, int32_t y, int32_t option, const char *string);
void V_DrawRightAlignedThinString(int32_t x, int32_t y, int32_t option, const char *string);

#define V_DrawStringAtFixed( x,y,option,string ) \
	V__DrawOneScaleString (x,y,FRACUNIT,option,HU_FONT,string)

#define V_DrawThinStringAtFixed( x,y,option,string ) \
	V__DrawOneScaleString (x,y,FRACUNIT,option,TINY_FONT,string)

void V_DrawCenteredThinStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string);
void V_DrawRightAlignedThinStringAtFixed(fixed_t x, fixed_t y, int32_t option, const char *string);

// Draws a titlecard font string.
// timer: when the letters start appearing (leave to 0 to disable)
// threshold: when the letters start disappearing (leave to 0 to disable) (both are int32_t in case you supply negative values...)
// NOTE: This function ignores most conventional string flags (V_RETURN8, V_ALLOWLOWERCASE ...)
// NOTE: This font only works with uppercase letters.
void V_DrawTitleCardString(int32_t x, int32_t y, const char *str, int32_t flags, dboolean bossmode, int32_t timer, int32_t threshold);

// returns thr width of a string drawn using the above function.
int32_t V_TitleCardStringWidth(const char *str);

// Draw tall nums, used for menu, HUD, intermission
void V_DrawTallNum(int32_t x, int32_t y, int32_t flags, int32_t num);
void V_DrawPaddedTallNum(int32_t x, int32_t y, int32_t flags, int32_t num, int32_t digits);
void V_DrawPaddedTallColorNumAtFixed(fixed_t x, fixed_t y, int32_t flags, int32_t num, int32_t digits, const uint8_t *colormap);

// Draw ping numbers. Used by the scoreboard and that one ping option. :P
// This is a separate function because IMO lua should have access to it as well.
int32_t V_DrawPingNumPadded(int32_t x, int32_t y, int32_t flags, int32_t num, uint16_t padding, const uint8_t *colormap);
#define V_DrawPingNum(x, y, flags, num, colormap) \
	V_DrawPingNumPadded(x, y, flags, num, 0, colormap)

int32_t V_DrawPingNumAtFixed(fixed_t x, fixed_t y, int32_t flags, int32_t num, const uint8_t *colormap);
int32_t V_DrawPingNumScaled(fixed_t x, fixed_t y, fixed_t scale, int32_t flags, int32_t num, const uint8_t *colormap);

// Rank numbers.
void V_DrawRankNum(int32_t x, int32_t y, int32_t flags, int32_t num, int32_t digits, const uint8_t *colormap);

// Find string width from lt_font chars
int32_t V_LevelNameWidth(const char *string);
int32_t V_LevelNameHeight(const char *string);
int16_t V_LevelActNumWidth(uint8_t num); // act number width

#define V_DrawCreditString( x,y,option,string ) \
	V__DrawOneScaleString (x,y,FRACUNIT,option,CRED_FONT,string)


int32_t V_CreditSubStringWidth(const char *string, int32_t length, int32_t option);
#define V_CreditStringWidth(string) V_ThinSubStringWidth(string, -1, 0)

// Find string width from hu_font chars
int32_t V_SubStringWidth(const char *string, int32_t length, int32_t option);
#define V_StringWidth(string, option) V_SubStringWidth(string, -1, option)
// Find string width from hu_font chars, 0.5x scale
int32_t V_SmallSubStringWidth(const char *string, int32_t length, int32_t option);
#define V_SmallStringWidth(string, option) V_SmallSubStringWidth(string, -1, option)
// Find string width from tny_font chars
int32_t V_ThinSubStringWidth(const char *string, int32_t length, int32_t option);
#define V_ThinStringWidth(string, option) V_ThinSubStringWidth(string, -1, option)

int32_t V_SubStringLengthToFit(const char *string, int32_t width, int32_t option);

// this is pretty dumb, but has to be done like this, otherwise the fps counter just disappears sometimes for no reason lol
FUNCINLINE static ATTRINLINE int32_t V_LocalTransFlag(void)
{
	return ((10-cv_translucenthud.value)*V_10TRANS);
}

void V_DoPostProcessor(int32_t view, int32_t param);

void V_DrawPatchFill(patch_t *pat);

void V_DrawAdaptiveScaledFullScreenPatch(patch_t *patch);
void V_DrawVerticallyScaledFullScreenPatch(patch_t *patch);
void V_DrawHorizontallyScaledFullScreenPatch(patch_t *patch);

void VID_BlitLinearScreen(const uint8_t *srcptr, uint8_t *destptr, int32_t width, int32_t height, size_t srcrowbytes,
	size_t destrowbytes);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
