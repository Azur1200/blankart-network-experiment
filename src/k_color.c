// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2020 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_color.c
/// \brief Skincolor & colormapping code

#include "k_color.h"

#include "doomdef.h"
#include "doomtype.h"
#include "r_draw.h"
#include "r_things.h"
#include "v_video.h"

UINT16 smonitorcolors[16] = {
    SKINCOLOR_BLOODCELL,   // 0
    SKINCOLOR_FUCHSIA,     // 1
    SKINCOLOR_MOON,        // 2
    SKINCOLOR_FIERY,       // 3
    SKINCOLOR_FLAME,       // 4
    SKINCOLOR_APRICOT,     // 5
    SKINCOLOR_GARDEN,      // 6
    SKINCOLOR_OLIVE,       // 7
    SKINCOLOR_LIME,        // 8
    SKINCOLOR_EMERALD,     // 9
    SKINCOLOR_NAVY,        // 10
    SKINCOLOR_BLUE,        // 11
    SKINCOLOR_SAPPHIRE,    // 12
    SKINCOLOR_BLUEBERRY,   // 13
    SKINCOLOR_PURPLE,      // 14
    SKINCOLOR_VIOLET       // 15
};

/*--------------------------------------------------
	UINT8 K_ColorRelativeLuminance(UINT8 r, UINT8 g, UINT8 b)

		See header file for description.
--------------------------------------------------*/
UINT8 K_ColorRelativeLuminance(UINT8 r, UINT8 g, UINT8 b)
{
	double redWeight = ((r * 1.0) / UINT8_MAX);
	double greenWeight = ((g * 1.0) / UINT8_MAX);
	double blueWeight = ((b * 1.0) / UINT8_MAX);
	double brightness = 0.5;

	redWeight = pow(redWeight, 2.2) * 0.2126;
	greenWeight = pow(greenWeight, 2.2) * 0.7152;
	blueWeight = pow(greenWeight, 2.2) * 0.0722;

	brightness = pow(redWeight + greenWeight + blueWeight, 1.0 / 2.2);

	return (UINT8)(brightness * UINT8_MAX);
}

/*--------------------------------------------------
	UINT16 K_RainbowColor(tic_t time)

		See header file for description.
--------------------------------------------------*/

UINT16 K_RainbowColor(tic_t time)
{
	return (UINT16)(FIRSTRAINBOWCOLOR + (time % (FIRSTSUPERCOLOR - FIRSTRAINBOWCOLOR)));
}

/*--------------------------------------------------
	UINT16 K_SMonitorColor(tic_t time)

		See header file for description.
--------------------------------------------------*/

UINT16 K_SMonitorColor(tic_t time)
{
	return (UINT16)(smonitorcolors[(time) & 15]);
}

/*--------------------------------------------------
	void K_RainbowColormap(UINT8 *dest_colormap, skincolornum_t skincolor)

		See header file for description.
--------------------------------------------------*/
void K_RainbowColormap(UINT8 *dest_colormap, skincolornum_t skincolor)
{
	INT32 i;
	RGBA_t color;
	UINT8 brightness;
	INT32 j;
	UINT8 colorbrightnesses[16];
	UINT16 brightdif;
	INT32 temp;

	// first generate the brightness of all the colours of that skincolour
	for (i = 0; i < 16; i++)
	{
		color = V_GetColor(skincolors[skincolor].ramp[i]);
		colorbrightnesses[i] = K_ColorRelativeLuminance(color.s.red, color.s.green, color.s.blue);
	}

	// next, for every colour in the palette, choose the transcolor that has the closest brightness
	for (i = 0; i < NUM_PALETTE_ENTRIES; i++)
	{
		if (i == 0 || i == 31) // pure black and pure white don't change
		{
			dest_colormap[i] = (UINT8)i;
			continue;
		}

		color = V_GetColor(i);
		brightness = K_ColorRelativeLuminance(color.s.red, color.s.green, color.s.blue);
		brightdif = 256;

		for (j = 0; j < 16; j++)
		{
			temp = abs((INT16)brightness - (INT16)colorbrightnesses[j]);

			if (temp < brightdif)
			{
				brightdif = (UINT16)temp;
				dest_colormap[i] = skincolors[skincolor].ramp[j];
			}
		}
	}
}

/*--------------------------------------------------
	void K_GenerateKartColormap(UINT8 *dest_colormap, INT32 skinnum, UINT8 color)

		See header file for description.
--------------------------------------------------*/
void K_GenerateKartColormap(UINT8 *dest_colormap, INT32 skinnum, skincolornum_t color)
{
	INT32 i;
	INT32 starttranscolor;

	if (skinnum == TC_BOSS
		|| skinnum == TC_ALLWHITE
		|| skinnum == TC_METALSONIC
		|| skinnum == TC_BLINK
		|| color == SKINCOLOR_NONE)
	{
		// Handle a couple of simple special cases
		for (i = 0; i < NUM_PALETTE_ENTRIES; i++)
		{
			if (skinnum == TC_ALLWHITE)
				dest_colormap[i] = 0;
			else if (skinnum == TC_BLINK)
				dest_colormap[i] = skincolors[color].ramp[3];
			else
				dest_colormap[i] = (UINT8)i;
		}

		// White!
		if (skinnum == TC_BOSS)
			dest_colormap[31] = 0;
		else if (skinnum == TC_METALSONIC)
			dest_colormap[143] = 0;

		return;
	}
	else if (skinnum == TC_RAINBOW)
	{
		K_RainbowColormap(dest_colormap, color);
		return;
	}

	starttranscolor = (skinnum != TC_DEFAULT) ? skins[skinnum]->starttranscolor : DEFAULT_STARTTRANSCOLOR;

	// Fill in the entries of the palette that are fixed
	for (i = 0; i < starttranscolor; i++)
		dest_colormap[i] = (UINT8)i;

	for (i = (UINT8)(starttranscolor + 16); i < NUM_PALETTE_ENTRIES; i++)
		dest_colormap[i] = (UINT8)i;

	// Build the translated ramp
	for (i = 0; i < SKIN_RAMP_LENGTH; i++)
	{
		// Sryder 2017-10-26: What was here before was most definitely not particularly readable, check above for new color translation table
		dest_colormap[starttranscolor + i] = skincolors[color].ramp[i];
	}
}

//}
