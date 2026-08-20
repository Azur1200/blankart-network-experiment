// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2020 by Jaime "Lactozilla" Passos.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_patchrotation.c
/// \brief Patch rotation.

#include "r_patchrotation.h"
#include "r_things.h" // FEETADJUST
#include "z_zone.h"
#include "w_wad.h"
#include "r_main.h" // R_PointToAngle
#include "k_kart.h" // K_Sliptiding
#include "p_tick.h"
#include "typedef.h"
#include "r_fps.h"
#include "k_items.h"

#ifdef ROTSPRITE
fixed_t rollcosang[ROTANGLES];
fixed_t rollsinang[ROTANGLES];

angle_t R_GetPitchRollAngle(mobj_t *mobj, player_t *viewPlayer, interpmobjstate_t *interp)
{
	angle_t viewingAngle, rollOrPitch;
	angle_t sloperoll = !interp ? 0 : interp->slopepitch;
	angle_t slopepitch = !interp ? 0 : interp->sloperoll;
	fixed_t pitchMul, rollMul;

	if (mobj->player)
	{
		// We are the player, use camera for air tilting.
		viewingAngle = R_PointToAnglePlayer(viewPlayer, mobj->x, mobj->y);
	}
	else
	{
		// For regular objects, use the camera; just not the *player's* camera.
		//... Unless you are a Nights bumber...
		if (mobj->type == MT_NIGHTSBUMPER)
			viewingAngle = mobj->angle;
		else
			viewingAngle = R_PointToAngleFloat(mobj->x, mobj->y);
	}

	pitchMul = -FINESINE(viewingAngle >> ANGLETOFINESHIFT);
	rollMul = FINECOSINE(viewingAngle >> ANGLETOFINESHIFT);

	if (!cv_sloperoll.value || (mobj->player && (mobj->player->pogospring > 0)))
	{
		sloperoll = 0;
		slopepitch = 0;
	}

	rollOrPitch = FixedMul(interp->pitch + sloperoll, pitchMul) + FixedMul(interp->roll + slopepitch, rollMul);

	return rollOrPitch;
}

static angle_t R_PlayerSpriteRotation(player_t *player, player_t *viewPlayer, dboolean fliptilt)
{
	angle_t viewingAngle = R_PointToAnglePlayer(viewPlayer, player->mo->x, player->mo->y);
	angle_t angleDelta = (viewingAngle - player->mo->angle);

	int32_t nulltilt = (K_NullDriftTiltEnabled()) ? FixedAngle(player->karttilt) : 0;
	int32_t aiztilt = (cv_sliptidetilt.value) ? player->aizdrifttilt : 0;

	dboolean nullBeforeSlip = (abs(aiztilt) < abs(nulltilt));
	int32_t liftsign = ((nullBeforeSlip) ? intsign(nulltilt) : intsign(aiztilt));

	// Sliptide tilt: Nulldrifts take priority if and ONLY if they're the larger value compared to sliptides.
	angle_t sliptideLift = max(abs(aiztilt), abs(nulltilt)) * liftsign;

	if (fliptilt)
	{
		// Vertical flip affine hack: invert the angle so it still looks right.
		sliptideLift *= -1;
	}

	angle_t rollAngle = 0;

	if (sliptideLift)
	{
		/* (from side) tilt downward if turning
		   toward camera, upward if away. */
		rollAngle +=
			FixedMul(sliptideLift, FINESINE(AbsAngle(angleDelta) >> ANGLETOFINESHIFT)) +
			FixedMul(sliptideLift, FINECOSINE(angleDelta >> ANGLETOFINESHIFT));
	}

	if (player->pogospring)
	{
		rollAngle = 0;
	}

	return rollAngle;
}

// Hacky dboolean to check if we're the rainbow S-Monitor overlay
dboolean R_IsOverlayingSMonitorPlayer(mobj_t* mobj)
{
	return ((mobj->type == MT_OVERLAY) &&
			(mobj->target) && (mobj->extravalue2) && (mobj->target->player) &&
			(mobj->target->player->smonitortimer));
}

angle_t R_ModelRotationAngle(mobj_t *mobj, player_t *viewPlayer, dboolean fliptilt)
{
	angle_t rollAngle = mobj->rollangle;

	if (mobj->player)
	{
		rollAngle += R_PlayerSpriteRotation(mobj->player, viewPlayer, fliptilt);
	}
	else if (R_IsOverlayingSMonitorPlayer(mobj))
	{
		rollAngle += R_PlayerSpriteRotation(mobj->target->player, viewPlayer, fliptilt);
	}

	return rollAngle;
}

angle_t R_SpriteRotationAngle(mobj_t *mobj, player_t *viewPlayer, interpmobjstate_t *interp, dboolean fliptilt)
{
	angle_t rollOrPitch;
	
	if (R_IsOverlayingSMonitorPlayer(mobj))
	{
		rollOrPitch = R_GetPitchRollAngle(mobj->target, viewPlayer, interp);
	}
	else
	{
		rollOrPitch = R_GetPitchRollAngle(mobj, viewPlayer, interp);
	}
	return (rollOrPitch + R_ModelRotationAngle(mobj, viewPlayer, fliptilt));
}

int32_t R_GetRollAngle(angle_t rollangle)
{
	int32_t ra = AngleFixed(rollangle)>>FRACBITS;
#if (ROTANGDIFF > 1)
	ra += (ROTANGDIFF/2);
#endif
	ra /= ROTANGDIFF;
	ra %= ROTANGLES;
	return ra;
}

#define VISROTMUL (ANG1 * ROTANGDIFF)

// Simulates "rollangle" angling
angle_t R_ConvToRollAngle(angle_t ang)
{
	return (cv_fakerollangle.value) ? ((angle_t)R_GetRollAngle(ang) * VISROTMUL) : ang;
}

vector2_t* R_RotateSpriteOffsetsByPitchRoll(
	mobj_t* mobj,
	dboolean vflip,
	dboolean hflip,
	dboolean affine,
	interpmobjstate_t *interp,
	vector2_t* out,
	vector2_t* rolloffs)
{
	fixed_t rotcos, rotsin, finx, finy;
	vector2_t xvec, yvec;
	(void)affine;

	// input offsets
	fixed_t xoffs, yoffs, xpiv, ypiv;

	// final offsets
	int16_t visx, visy, visz;
	int16_t vxpiv, vypiv;

	// visual rotation
	angle_t visrollang;

	// camera angle
	angle_t viewingAngle = R_PointToAngle(mobj->x, mobj->y);

	// rotate ourselves entirely by the sprite's own rotation angle
	angle_t visrot = R_SpriteRotationAngle(mobj, NULL, interp, false);

	if (vflip)
	{
		// Invert the angle for vertically flipped sprites.
		visrot = InvAngle(visrot);
	}

	// xoffs = (-cos(xoff) + sin(yoff))
	xoffs =
		FixedMul(mobj->bakeyoff, -FINECOSINE(mobj->angle >> ANGLETOFINESHIFT)) +
		FixedMul(mobj->bakexoff, FINESINE(mobj->angle >> ANGLETOFINESHIFT));
	xpiv =
		FixedMul(mobj->bakeypiv, -FINECOSINE(mobj->angle >> ANGLETOFINESHIFT)) +
		FixedMul(mobj->bakexpiv, FINESINE(mobj->angle >> ANGLETOFINESHIFT));

	// yoffs = (-sin(yoff) + cos(xoff))
	yoffs =
		FixedMul(mobj->bakeyoff, -FINESINE(mobj->angle >> ANGLETOFINESHIFT)) +
		FixedMul(mobj->bakexoff, FINECOSINE(mobj->angle >> ANGLETOFINESHIFT));
	ypiv =
		FixedMul(mobj->bakeypiv, -FINESINE(mobj->angle >> ANGLETOFINESHIFT)) +
		FixedMul(mobj->bakexpiv, FINECOSINE(mobj->angle >> ANGLETOFINESHIFT));

	visrollang = (R_GetRollAngle(visrot) * VISROTMUL) * (hflip ? -1 : 1);

	// get pitch and roll multipliers, mainly used to align the
	// viewpoint with the camera
	fixed_t pitchMul = -FINESINE(viewingAngle >> ANGLETOFINESHIFT);
	fixed_t rollMul = FINECOSINE(viewingAngle >> ANGLETOFINESHIFT);

	// get visual positions
	visz = visy = visx = 0;
	visz = (int16_t)(-(mobj->bakezoff / FRACUNIT));
	visx = (int16_t)(FixedMul((yoffs / FRACUNIT), rollMul));
	visy = (int16_t)(FixedMul((xoffs / FRACUNIT), pitchMul));

	vxpiv = (int16_t)(FixedMul((ypiv / FRACUNIT), rollMul));
	vypiv = (int16_t)(FixedMul((xpiv / FRACUNIT), pitchMul));

	// rotate by rollangle
	finx = (visx + visy);
	finy = -visz;

	rotcos = FINECOSINE(visrollang >> ANGLETOFINESHIFT);
	rotsin = FINESINE(visrollang >> ANGLETOFINESHIFT);

	xvec.x = FixedMul(finx, rotcos);
	xvec.y = FixedMul(finx, -rotsin);

	yvec.x = FixedMul(finy, rotsin);
	yvec.y = FixedMul(finy, -rotcos);

	rolloffs->x = (fixed_t)(FixedMul(mobj->rollingxoffset, rotcos) + FixedMul(mobj->rollingyoffset, rotsin));
	rolloffs->y = (fixed_t)(FixedMul(mobj->rollingxoffset, -rotsin) - FixedMul(mobj->rollingyoffset, -rotcos));

	// set finalized offsets
	out->x = (fixed_t)(xvec.x + yvec.x + vxpiv + vypiv);
	out->y = (fixed_t)(xvec.y - yvec.y) + (mobj->bakezpiv / FRACUNIT);

	// flip based on vflip and hflip
	// flip the view angle if we're horizontally flipped
	if (hflip)
	{
		out->x *= -1;
		rolloffs->x *= 1;
	}

	if (vflip)
	{
		out->y *= -1;
		rolloffs->y *= 1;
	}
	return out;
}

#undef VISROTMUL

patch_t *Patch_GetRotated(patch_t *patch, int32_t angle, dboolean flip)
{
	rotsprite_t *rotsprite = patch->rotated;
	if (angle < 1 || angle >= ROTANGLES)
		return NULL;

	if (rotsprite == NULL)
	{
		rotsprite = RotatedPatch_Create(ROTANGLES);
		patch->rotated = rotsprite;
	}

	if (flip)
		angle += rotsprite->angles;

	if (rotsprite->patches[angle] == NULL)
	{
		int32_t xpivot = 0, ypivot = 0;

		if (patch->alignflags & PATCHALIGN_USEPIVOTS)
		{
			xpivot = patch->pivot.x;
			ypivot = patch->pivot.y;
		}
		else
		{
			xpivot = patch->leftoffset;
			ypivot = patch->topoffset;
		}
		
		RotatedPatch_DoRotation(rotsprite, patch, angle, xpivot, ypivot, flip);
	}

	return rotsprite->patches[angle];
}

patch_t *Patch_GetRotatedSprite(
	spriteframe_t *sprite,
	size_t frame, size_t spriteangle,
	dboolean flip, dboolean adjustfeet,
	void *info, int32_t rotationangle)
{
	rotsprite_t *rotsprite;
	spriteinfo_t *sprinfo = (spriteinfo_t *)info;
	int32_t idx = rotationangle;
	uint8_t type = (adjustfeet ? 1 : 0);

	if (rotationangle < 1 || rotationangle >= ROTANGLES)
		return NULL;

	rotsprite = sprite->rotated[type][spriteangle];

	if (rotsprite == NULL)
	{
		rotsprite = RotatedPatch_Create(ROTANGLES);
		sprite->rotated[type][spriteangle] = rotsprite;
	}

	if (flip)
		idx += rotsprite->angles;

	if (rotsprite->patches[idx] == NULL)
	{
		patch_t *patch;
		int32_t xpivot = 0, ypivot = 0;
		lumpnum_t lump = sprite->lumppat[spriteangle];

		if (lump == LUMPERROR)
			return NULL;

		patch = W_CachePatchNum(lump, PU_SPRITE);

		if (in_bit_array(sprinfo->available, frame))
		{
			xpivot = sprinfo->pivot[frame].x;
			ypivot = sprinfo->pivot[frame].y;
		}
		else if (in_bit_array(sprinfo->available, SPRINFO_DEFAULT_PIVOT))
		{
			xpivot = sprinfo->pivot[SPRINFO_DEFAULT_PIVOT].x;
			ypivot = sprinfo->pivot[SPRINFO_DEFAULT_PIVOT].y;
		}
		else
		{
			xpivot = patch->leftoffset;
			ypivot = patch->height / 2;
		}

		RotatedPatch_DoRotation(rotsprite, patch, rotationangle, xpivot, ypivot, flip);

		//BP: we cannot use special tric in hardware mode because feet in ground caused by z-buffer
		if (adjustfeet)
			((patch_t *)rotsprite->patches[idx])->topoffset += FEETADJUST>>FRACBITS;

		if (sprinfo == &spriteinfo[SPR_ITEM]) // easier than funneling this into dorotation...
		{
			((patch_t *)rotsprite->patches[idx])->leftoffset += 25;
			((patch_t *)rotsprite->patches[idx])->topoffset += 50;
		}
	}

	return rotsprite->patches[idx];
}

void Patch_Rotate(patch_t *patch, int32_t angle, int32_t xpivot, int32_t ypivot, dboolean flip)
{
	if (patch->rotated == NULL)
		patch->rotated = RotatedPatch_Create(ROTANGLES);
	RotatedPatch_DoRotation(patch->rotated, patch, angle, xpivot, ypivot, flip);
}

rotsprite_t *RotatedPatch_Create(int32_t numangles)
{
	rotsprite_t *rotsprite = Z_Calloc(sizeof(rotsprite_t), PU_STATIC, NULL);
	rotsprite->angles = numangles;
	rotsprite->patches = Z_Calloc(rotsprite->angles * 2 * sizeof(void *), PU_STATIC, NULL);
	return rotsprite;
}

static void RotatedPatch_CalculateDimensions(
	int32_t width, int32_t height,
	fixed_t ca, fixed_t sa,
	int32_t *newwidth, int32_t *newheight)
{
	fixed_t fixedwidth = (width * FRACUNIT);
	fixed_t fixedheight = (height * FRACUNIT);

	fixed_t w1 = abs(FixedMul(fixedwidth, ca) - FixedMul(fixedheight, sa));
	fixed_t w2 = abs(FixedMul(-fixedwidth, ca) - FixedMul(fixedheight, sa));
	fixed_t h1 = abs(FixedMul(fixedwidth, sa) + FixedMul(fixedheight, ca));
	fixed_t h2 = abs(FixedMul(-fixedwidth, sa) + FixedMul(fixedheight, ca));

	w1 = FixedInt(FixedCeil(w1 + (FRACUNIT/2)));
	w2 = FixedInt(FixedCeil(w2 + (FRACUNIT/2)));
	h1 = FixedInt(FixedCeil(h1 + (FRACUNIT/2)));
	h2 = FixedInt(FixedCeil(h2 + (FRACUNIT/2)));

	*newwidth = max(width, max(w1, w2));
	*newheight = max(height, max(h1, h2));
}

static void RotatedPatch_AllocImgBuf(uint16_t **buf, size_t size, size_t *capacity)
{
	if (!*buf)
	{
		*buf = Z_Malloc(*capacity, PU_STATIC, NULL);
	}

	size_t allocsize = size * sizeof(uint16_t);

	// make sure our buffer always fits with what we need
	if (allocsize > *capacity)
	{
		while (*capacity < allocsize)
			*capacity *= 2;

		Z_Free(*buf);
		*buf = Z_Malloc(*capacity, PU_STATIC, NULL);
	}

	//CONS_Printf("RotatedPatch_AllocBuf: Allocated %.2f KB\n", *capacity / 1024.0);
}

void RotatedPatch_DoRotation(rotsprite_t *rotsprite, patch_t *patch, int32_t angle, int32_t xpivot, int32_t ypivot, dboolean flip)
{
	patch_t *rotated;
	static uint16_t *rawdst = NULL, *rawconv = NULL;
	uint16_t *rawout;
	size_t size;
	static size_t dst_capacity = (1024 * 1024), conv_capacity = (1024 * 1024); // 1 MB should be enough for most cases
	pictureflags_t bflip = (flip) ? PICFLAGS_XFLIP : 0;

	int32_t width = patch->width;
	int32_t height = patch->height;
	int32_t leftoffset = patch->leftoffset;
	int32_t newwidth, newheight;

	fixed_t ca = rollcosang[angle];
	fixed_t sa = rollsinang[angle];
	fixed_t xcenter, ycenter;
	int32_t idx = angle;
	int32_t x, y;
	int32_t sx, sy;
	int32_t dx, dy;
	int32_t ox, oy;
	int32_t minx, miny, maxx, maxy;

	// Don't cache angle = 0
	if (angle < 1 || angle >= ROTANGLES)
		return;

	if (flip)
	{
		idx += rotsprite->angles;
		xpivot = width - xpivot;
		leftoffset = width - leftoffset;
	}

	if (rotsprite->patches[idx])
		return;

	// Find the dimensions of the rotated patch.
	RotatedPatch_CalculateDimensions(width, height, ca, sa, &newwidth, &newheight);

	xcenter = (xpivot * FRACUNIT);
	ycenter = (ypivot * FRACUNIT);

	if (xpivot != width / 2 || ypivot != height / 2)
	{
		newwidth *= 2;
		newheight *= 2;
	}

	minx = newwidth;
	miny = newheight;
	maxx = 0;
	maxy = 0;

	// Draw the rotated sprite to a temporary buffer.
	size = (newwidth * newheight);
	if (!size)
		size = (width * height);

	RotatedPatch_AllocImgBuf(&rawdst, size, &dst_capacity);

	for (size_t i = 0; i < size; i++)
		rawdst[i] = 0x0000;

	for (dy = 0; dy < newheight; dy++)
	{
		for (dx = 0; dx < newwidth; dx++)
		{
			x = (dx - (newwidth / 2)) * FRACUNIT;
			y = (dy - (newheight / 2)) * FRACUNIT;
			sx = FixedMul(x, ca) + FixedMul(y, sa) + xcenter;
			sy = -FixedMul(x, sa) + FixedMul(y, ca) + ycenter;

			sx >>= FRACBITS;
			sy >>= FRACBITS;

			if (sx >= 0 && sy >= 0 && sx < width && sy < height)
			{
				void *input = Picture_GetPatchPixel(patch, PICFMT_PATCH, sx, sy, bflip);
				if (input != NULL)
				{
					rawdst[(dy * newwidth) + dx] = (0xFF00 | (*(uint8_t *)input));
					if (dx < minx)
						minx = dx;
					if (dy < miny)
						miny = dy;
					if (dx > maxx)
						maxx = dx;
					if (dy > maxy)
						maxy = dy;
				}
			}
		}
	}

	ox = (newwidth / 2) + (leftoffset - xpivot);
	oy = (newheight / 2) + (patch->topoffset - ypivot);
	width = (maxx - minx);
	height = (maxy - miny);

	if ((unsigned)(width * height) > size)
	{
		uint16_t *src, *dest;

		size = (width * height);

		RotatedPatch_AllocImgBuf(&rawconv, size, &conv_capacity);
		memset(rawconv, 0, conv_capacity * sizeof(uint16_t));

		src = &rawdst[(miny * newwidth) + minx];
		dest = rawconv;
		dy = height;

		while (dy--)
		{
			memcpy(dest, src, width * sizeof(uint16_t));
			dest += width;
			src += newwidth;
		}

		ox -= minx;
		oy -= miny;

		rawout = rawconv;
	}
	else
	{
		width = newwidth;
		height = newheight;

		rawout = rawdst;
	}

	// make patch
	rotated = (patch_t *)Picture_Convert(PICFMT_FLAT16, rawout, PICFMT_PATCH, 0, NULL, width, height, 0, 0, 0);

	Z_ChangeTag(rotated, PU_PATCH_ROTATED);
	Z_SetUser(rotated, (void **)(&rotsprite->patches[idx]));

	rotated->leftoffset = ox;
	rotated->topoffset = oy;
}
#endif
