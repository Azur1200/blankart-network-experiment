// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by Kart Krew.
// Copyright (C) 2020 by Sonic Team Junior.
// Copyright (C) 2000 by DooM Legacy Team.
// Copyright (C) 1996 by id Software, Inc.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_draw_column.cpp
/// \brief column drawer functions
/// \note  no includes because this is included as part of r_draw.cpp

// ==========================================================================
// COLUMNS
// ==========================================================================

// A column is a vertical slice/span of a wall texture that uses
// a has a constant z depth from top to bottom.
//

#include "r_draw.h"
#include <tracy/tracy/Tracy.hpp>


#include "r_draw_flush.cpp"

enum DrawColumnType
{
	DC_BASIC			= 0x0000,
	DC_COLORMAP			= 0x0001,
	DC_TRANSMAP			= 0x0002,
	DC_BRIGHTMAP		= 0x0004,
	DC_HOLES			= 0x0008,
	DC_LIGHTLIST		= 0x0010,
	DC_DIRECT			= 0x0020, // draw our columns directly to screen!
};

template<DrawColumnType Type>
FUNCINLINE static ATTRINLINE constexpr UINT8 R_GetColumnTranslated(drawcolumndata_t* dc, UINT8 col)
{
	if constexpr (Type & DrawColumnType::DC_COLORMAP)
	{
		return dc->translation[col];
	}
	else
	{
		return col;
	}
}

template<DrawColumnType Type>
FUNCINLINE static ATTRINLINE constexpr UINT8 R_GetColumnBrightmapped(drawcolumndata_t* dc, UINT32 bit, UINT8 col)
{
	col = R_GetColumnTranslated<Type>(dc, col);

	if constexpr (Type & DrawColumnType::DC_BRIGHTMAP)
	{
		if (dc->brightmap[bit] == BRIGHTPIXEL)
		{
			return dc->fullbright[col];
		}
	}

	return dc->colormap[col];
}

// translucency is handled on flush side now!
template<DrawColumnType Type>
FUNCINLINE static ATTRINLINE constexpr UINT8 R_GetColumnTranslucent(drawcolumndata_t* dc, UINT8 *dest, UINT32 bit, UINT8 col)
{
	col = R_GetColumnBrightmapped<Type>(dc, bit, col);

	if constexpr (Type & DrawColumnType::DC_TRANSMAP)
	{
		return *(dc->transmap + (col << 8) + (*dest));
	}
	else
	{
		return col;
	}
}

template<DrawColumnType Type>
FUNCINLINE static ATTRINLINE constexpr UINT8 R_DrawColumnPixel(drawcolumndata_t* dc, UINT8 *dest, UINT32 bit)
{
	UINT8 col = dc->source[bit];

	if constexpr (Type & DrawColumnType::DC_HOLES)
	{
		if (col == TRANSPARENTPIXEL)
		{
			return *dest;
		}
	}

	// if we dont buffer our columns, we need to handle translucency again
	return R_GetColumnTranslucent<Type>(dc, dest, bit, col);
}

/**	\brief The R_DrawColumn function
	Experiment to make software go faster. Taken from the Boom source
*/
template<DrawColumnType Type>
static void R_DrawColumnTemplate(drawcolumndata_t *dc)
{
	INT32 count;
	UINT8 *dest;
	const INT32 vidheight = vid.height;

	// leban 1/17/99:
	// removed the + 1 here, adjusted the if test, and added an increment
	// later.  this helps a compiler pipeline a bit better.  the x86
	// assembler also does this.
	count = dc->yh - dc->yl;

	// leban 1/17/99:
	// this case isn't executed too often.  depending on how many instructions
	// there are between here and the second if test below, this case could
	// be moved down and might save instructions overall.  since there are
	// probably different wads that favor one way or the other, i'll leave
	// this alone for now.
	if (count < 0) // Zero length, column does not exceed a pixel.
	{
		return;
	}

	if ((unsigned)dc->x >= (unsigned)vid.width || dc->yl < 0 || dc->yh >= vidheight)
	{
		return;
	}

	if constexpr (Type & DrawColumnType::DC_LIGHTLIST)
	{
		constexpr DrawColumnType NewType = static_cast<DrawColumnType>(Type & ~DC_LIGHTLIST);
		INT32 i, realyh, height, bheight = 0, solid = 0;
		drawcolumndata_t dc_copy = *dc;

		realyh = dc_copy.yh;

		// This runs through the lightlist from top to bottom and cuts up the column accordingly.
		for (i = 0; i < dc_copy.numlights; i++)
		{
			// If the height of the light is above the column, get the colormap
			// anyway because the lighting of the top should be affected.
			solid = dc_copy.lightlist[i].flags & FOF_CUTSOLIDS;
			height = dc_copy.lightlist[i].height >> LIGHTSCALESHIFT;

			if (solid)
			{
				bheight = dc_copy.lightlist[i].botheight >> LIGHTSCALESHIFT;

				if (bheight < height)
				{
					// confounded slopes sometimes allow partial invertedness,
					// even including cases where the top and bottom heights
					// should actually be the same!
					// swap the height values as a workaround for this quirk
					INT32 temp = height;
					height = bheight;
					bheight = temp;
				}
			}

			if (height <= dc_copy.yl)
			{
				dc_copy.colormap = dc_copy.lightlist[i].rcolormap;
				dc_copy.fullbright = colormaps;

				if (encoremap)
				{
					dc_copy.colormap += COLORMAP_REMAPOFFSET;
					dc_copy.fullbright += COLORMAP_REMAPOFFSET;
				}

				if (solid && dc_copy.yl < bheight)
				{
					dc_copy.yl = bheight;
				}

				continue;
			}

			// Found a break in the column!
			dc_copy.yh = height;

			if (dc_copy.yh > realyh)
			{
				dc_copy.yh = realyh;
			}

			R_DrawColumnTemplate<NewType>(&dc_copy);

			if (solid)
			{
				dc_copy.yl = bheight;
			}
			else
			{
				dc_copy.yl = dc_copy.yh + 1;
			}

			dc_copy.colormap = dc_copy.lightlist[i].rcolormap;
			dc_copy.fullbright = colormaps;
			if (encoremap)
			{
				dc_copy.colormap += COLORMAP_REMAPOFFSET;
				dc_copy.fullbright += COLORMAP_REMAPOFFSET;
			}
		}

		dc_copy.yh = realyh;

		if (dc_copy.yl <= realyh)
		{
			R_DrawColumnTemplate<NewType>(&dc_copy);
		}
	}
	else
	{
		fixed_t fracstep;
		fixed_t frac;
		INT32 heightmask;
		INT32 npow2min;
		INT32 npow2max;

		// Framebuffer destination address.

		if constexpr (Type & DrawColumnType::DC_DIRECT)
			dest = R_Address(dc->x, dc->yl);
		else if constexpr ((Type & (DrawColumnType::DC_COLORMAP | DrawColumnType::DC_TRANSMAP))
			== (DrawColumnType::DC_COLORMAP | DrawColumnType::DC_TRANSMAP))
			dest = R_GetBufferColormapTrans(dc);
		else if constexpr (Type & DrawColumnType::DC_TRANSMAP)
			dest = R_GetBufferTrans(dc);
		else if constexpr (Type & DrawColumnType::DC_COLORMAP)
			dest = R_GetBufferColormap(dc);
		else
			dest = R_GetBufferOpaque(dc);

		INT32 vidwidth = 8; //SoM: Oh, Oh it's MAGIC! You know...

		if constexpr (Type & DrawColumnType::DC_DIRECT)
			vidwidth = vid.width;

		count++;

		// Determine scaling, which is the only mapping to be done.
		fracstep = dc->iscale;
		//frac = dc_texturemid + (dc_yl - centery)*fracstep;
		frac = (dc->texturemid + FixedMul((dc->yl << FRACBITS) - centeryfrac, fracstep)) * (!dc->hires);

		// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
		// This is as fast as it gets.
		heightmask = dc->sourcelength-1;
		npow2min = -1;
		npow2max = dc->sourcelength;

		if (heightmask == -1)
		{
			if (frac < 0)
				// adjust in case we underread
				frac += fracstep;

			// texture has no height, so just go
			while (--count > 0)
			{
				*dest = R_DrawColumnPixel<Type>(dc, dest, frac>>FRACBITS);
				dest += vidwidth;
				frac += fracstep;
			}
		}
		else if (dc->sourcelength & heightmask)   // not a power of 2 -- killough
		{
			heightmask = dc->texheight << FRACBITS;

			if (frac < 0)
			{
				while ((frac += heightmask) < 0)
				{
					;
				}
			}
			else
			{
				while (frac >= heightmask)
				{
					frac -= heightmask;
				}
			}

			do
			{
				// Re-map color indices from wall texture column
				//  using a lighting/special effects LUT.
				// heightmask is the Tutti-Frutti fix

				// -1 is the lower clamp bound because column posts have a "safe" byte before the real data
				// and a few bytes after as well
				//*dest = R_DrawColumnPixel<Type>(dc, dest, std::clamp(frac >> FRACBITS, npow2min, npow2max));
				{
					// jartha: faster on my AMD FX-6300 CPU.
					// Faster than ternaries, faster than std::min/std::max. Don't ask me why.
					// I tested by viewing a non-PO2 texture from a consistent distance so it covered the entire screen.
					// The framerate difference was about 50 frames at 640x400.
					INT32 n = frac >> FRACBITS;
					if (n < npow2min)
						n = npow2min;
					if (n > npow2max)
						n = npow2max;
					*dest = R_DrawColumnPixel<Type>(dc, dest, n);
				}

				dest += vidwidth;

				// Avoid overflow.
				if (fracstep > 0x7FFFFFFF - frac)
				{
					frac += fracstep - heightmask;
				}
				else
				{
					frac += fracstep;
				}

				while (frac >= heightmask)
				{
					frac -= heightmask;
				}
			}
			while (--count);
		}
		else
		{
			while ((count -= 2) >= 0) // texture height is a power of 2
			{
				*dest = R_DrawColumnPixel<Type>(dc, dest, (frac>>FRACBITS) & heightmask);

				dest += vidwidth;
				frac += fracstep;

				*dest = R_DrawColumnPixel<Type>(dc, dest, (frac>>FRACBITS) & heightmask);

				dest += vidwidth;
				frac += fracstep;
			}

			if (count & 1)
			{
				*dest = R_DrawColumnPixel<Type>(dc, dest, (frac>>FRACBITS) & heightmask);
			}
		}
	}
}

#define DEFINE_COLUMN_FUNC(name, flags) \
	void name(drawcolumndata_t *dc) \
	{ \
		ZoneScoped; \
		constexpr DrawColumnType opt = static_cast<DrawColumnType>(flags); \
		R_DrawColumnTemplate<opt>(dc); \
	}

#define DEFINE_COLUMN_COMBO(name, flags) \
	DEFINE_COLUMN_FUNC(name, flags) \
	DEFINE_COLUMN_FUNC(name ## _Brightmap, flags|DC_DIRECT|DC_BRIGHTMAP)

DEFINE_COLUMN_COMBO(R_DrawColumn, DC_DIRECT|DC_BASIC)
DEFINE_COLUMN_COMBO(R_DrawTranslucentColumn, DC_DIRECT|DC_TRANSMAP)
DEFINE_COLUMN_COMBO(R_DrawTranslatedColumn, DC_DIRECT|DC_COLORMAP)
DEFINE_COLUMN_COMBO(R_DrawColumnShadowed, DC_DIRECT|DC_LIGHTLIST)
DEFINE_COLUMN_COMBO(R_DrawTranslatedTranslucentColumn, DC_DIRECT|DC_COLORMAP|DC_TRANSMAP)
DEFINE_COLUMN_COMBO(R_Draw2sMultiPatchColumn, DC_DIRECT|DC_HOLES)
DEFINE_COLUMN_COMBO(R_Draw2sMultiPatchTranslucentColumn, DC_DIRECT|DC_HOLES|DC_TRANSMAP)

DEFINE_COLUMN_COMBO(R_DrawColumnFlush, DC_BASIC)
DEFINE_COLUMN_COMBO(R_DrawTranslucentColumnFlush, DC_TRANSMAP)
DEFINE_COLUMN_COMBO(R_DrawTranslatedColumnFlush, DC_COLORMAP)
DEFINE_COLUMN_COMBO(R_DrawColumnShadowedFlush, DC_LIGHTLIST)
DEFINE_COLUMN_COMBO(R_DrawTranslatedTranslucentColumnFlush, DC_COLORMAP|DC_TRANSMAP)
DEFINE_COLUMN_COMBO(R_Draw2sMultiPatchColumnFlush, DC_HOLES)
DEFINE_COLUMN_COMBO(R_Draw2sMultiPatchTranslucentColumnFlush, DC_HOLES|DC_TRANSMAP)

//skymyass
//DEFINE_COLUMN_FUNC(R_DrawSkyColumn, DC_SKY)

void R_DrawFogColumn(drawcolumndata_t *dc)
{
	ZoneScoped;

	INT32 count;
	UINT8 *dest;
	const INT32 vidheight = vid.height;
	const INT32 vidwidth = vid.width;

	count = dc->yh - dc->yl;

	// Zero length, column does not exceed a pixel.
	if (count < 0)
		return;

	if ((unsigned)dc->x >= (unsigned)vidwidth || dc->yl < 0 || dc->yh >= vidheight)
		return;

	// Framebuffer destination address.
	dest = R_Address(dc->x, dc->yl);

	// Determine scaling, which is the only mapping to be done.
	do
	{
		// Simple. Apply the colormap to what's already on the screen.
		*dest = dc->colormap[*dest];
		dest += vidwidth;
	}
	while (count--);
}

void R_DrawDropShadowColumn(drawcolumndata_t *dc)
{
	ZoneScoped;

	// Hack: A cut-down copy of R_DrawTranslucentColumn_8 that does not read texture
	// data since something about calculating the texture reading address for drop shadows is broken.
	// dc_texturemid and dc_iscale get wrong values for drop shadows, however those are not strictly
	// needed for the current design of the shadows, so this function bypasses the issue
	// by not using those variables at all.

	INT32 count;
	UINT8 *dest;
	const INT32 vidwidth = vid.width;

	count = dc->yh - dc->yl + 1;

	if (count <= 0) // Zero length, column does not exceed a pixel.
		return;

	dest = R_Address(dc->x, dc->yl);

	const UINT8 *transmap_offset = dc->transmap + (dc->shadowcolor << 8);
	while ((count -= 2) >= 0)
	{
		*dest = *(transmap_offset + (*dest));
		dest += vidwidth;
		*dest = *(transmap_offset + (*dest));
		dest += vidwidth;
	}

	if (count & 1)
		*dest = *(transmap_offset + (*dest));
}

void R_DrawColumn_Flat(drawcolumndata_t *dc)
{
	ZoneScoped;

	INT32 count;
	UINT8 color = dc->lightmap[dc->r8_flatcolor];
	UINT8 *dest;
	const INT32 vidheight = vid.height;
	const INT32 vidwidth = vid.width;

	count = dc->yh - dc->yl;

	if (count < 0) // Zero length, column does not exceed a pixel.
		return;

	if ((unsigned)dc->x >= (unsigned)vidwidth || dc->yl < 0 || dc->yh >= vidheight)
		return;

	// Framebuffer destination address.
	dest = R_Address(dc->x, dc->yl);

	count++;

	do
	{
		*dest = color;
		dest += vidwidth;
	}
	while (--count);
}
