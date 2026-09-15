// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by Team BlanKart
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  neo-spring.c
/// \brief 3D springboards

#include "../doomdef.h"
#include "../k_kart.h"
#include "../taglist.h"
#include "../p_local.h"
#include "../p_setup.h"
#include "../p_spec.h"
#include "../r_main.h"
#include "../k_objects.h"

#define neospring_setup(spring) spring->extravalue3
#define neospring_slopesetup(spring) spring->movecount
#define neospring_part_side(part) part->threshold

// Shamelessly taken from slope setup code
static void ReconfigureViaConstants(pslope_t *slope, const fixed_t a, const fixed_t b, const fixed_t c, const fixed_t d)
{
	fixed_t m;
	fixed_t o = 0;
	vector3_t *normal = &slope->normal;

	slope->constants[0] = a;
	slope->constants[1] = b;
	slope->constants[2] = c;
	slope->constants[3] = d;

	if (c)
		o = abs(c) <= FRACUNIT ? -FixedMul(d, FixedDiv(FRACUNIT, c)) : -FixedDiv(d, c);

	// Set origin.
	FV3_Load(&slope->o, 0, 0, o);

	// Get slope's normal.
	FV3_Load(normal, a, b, c);
	FV3_Normalize(normal);

	// Invert normal if it's facing down.
	if (normal->z < 0)
		FV3_Negate(normal);

	// Get direction vector
	m = FixedHypot(normal->x, normal->y);
	slope->d.x = -FixedDiv(normal->x, m);
	slope->d.y = -FixedDiv(normal->y, m);

	// Z delta
	slope->zdelta = FixedDiv(m, normal->z);

	// Get angles
	slope->xydirection = R_PointToAngle2(0, 0, slope->d.x, slope->d.y) + ANGLE_180;
	slope->zangle = R_PointToAngle2(0, 0, FRACUNIT, -slope->zdelta);
}

skincolornum_t Obj_HandleNeoSpringColor(fixed_t speedval)
{
	if (speedval >= 70 * FRACUNIT)
	{
		return SKINCOLOR_RED;
	}
	else if (speedval > 40 * FRACUNIT && speedval < 70 * FRACUNIT)
	{
		return SKINCOLOR_YELLOW;
	}
	else if (speedval > 30 * FRACUNIT && speedval <= 40 * FRACUNIT)
	{
		return SKINCOLOR_SAPPHIRE;
	}
	else if (speedval > 0 && speedval <= 30 * FRACUNIT)
	{
		return SKINCOLOR_GREY;
	}
	else
	{
		return SKINCOLOR_WHITE;
	}
}

#define SETUPNEXTPART \
	prev = cur;       \
	cur = cur->hnext; \
	cur->hprev = prev;

#define BACK 1
#define FRONT 2
#define SIDEL 3
#define SIDER 4
#define BOTTOM 5
#define TOP 6

void Obj_InitNeoSpringParts(mobj_t *spring)
{
	mobj_t *cur = spring, *prev = NULL;

	cur->hnext = P_SpawnMobjFromMobj(spring, 0, 0, 0, MT_NEOSPRING_PART);
	P_SetTarget(&cur->hnext->target, spring);
	neospring_part_side(cur->hnext) = BACK;

	SETUPNEXTPART

	cur->hnext = P_SpawnMobjFromMobj(spring, 0, 0, 0, MT_NEOSPRING_PART);
	P_SetTarget(&cur->hnext->target, spring);
	neospring_part_side(cur->hnext) = FRONT;

	SETUPNEXTPART

	cur->hnext = P_SpawnMobjFromMobj(spring, 0, 0, 0, MT_NEOSPRING_PART);
	P_SetTarget(&cur->hnext->target, spring);
	neospring_part_side(cur->hnext) = SIDEL;

	SETUPNEXTPART

	cur->hnext = P_SpawnMobjFromMobj(spring, 0, 0, 0, MT_NEOSPRING_PART);
	P_SetTarget(&cur->hnext->target, spring);
	neospring_part_side(cur->hnext) = SIDER;

	SETUPNEXTPART

	cur->hnext = P_SpawnMobjFromMobj(spring, 0, 0, 0, MT_NEOSPRING_PART);
	P_SetTarget(&cur->hnext->target, spring);
	neospring_part_side(cur->hnext) = BOTTOM;

	SETUPNEXTPART

	cur->hnext = P_SpawnMobjFromMobj(spring, 0, 0, 0, MT_NEOSPRING_PART);
	P_SetTarget(&cur->hnext->target, spring);
	neospring_part_side(cur->hnext) = TOP;

	neospring_setup(spring) = true;
}

#define UPDATEOFFSET(cur, spring, ofsx, ofsy, ofsz)                  \
	P_MoveOrigin(                                                    \
		cur,                                                         \
		spring->x + spring->sprxoff + FixedMul(ofsx, spring->scale), \
		spring->y + spring->spryoff + FixedMul(ofsy, spring->scale), \
		spring->z + spring->sprzoff + FixedMul(ofsz, spring->scale))

#define UPDATEANGLE(angle, ofsx, ofsy)                                                                                        \
	fixed_t ofsxbak = ofsx;                                                                                                   \
	fixed_t ofsybak = ofsy;                                                                                                   \
	ofsx = FixedMul(ofsxbak, FINECOSINE(angle >> ANGLETOFINESHIFT)) - FixedMul(ofsybak, FINESINE(angle >> ANGLETOFINESHIFT)); \
	ofsy = FixedMul(ofsxbak, FINESINE(angle >> ANGLETOFINESHIFT)) + FixedMul(ofsybak, FINECOSINE(angle >> ANGLETOFINESHIFT));

#define UPDATEPLANEANGLE(floorspriteslope, facingangle, angle, x, y, z)                                            \
	fixed_t slope_a = FixedMul(-FINESINE(angle >> ANGLETOFINESHIFT), FINECOSINE(facingangle >> ANGLETOFINESHIFT)); \
	fixed_t slope_b = FixedMul(-FINESINE(angle >> ANGLETOFINESHIFT), FINESINE(facingangle >> ANGLETOFINESHIFT));   \
	fixed_t slope_c = FINECOSINE(angle >> ANGLETOFINESHIFT);                                                       \
	fixed_t slope_d = -(FixedMul(slope_a, x) + FixedMul(slope_b, y) + FixedMul(slope_c, z));                       \
	ReconfigureViaConstants(floorspriteslope, slope_a, slope_b, slope_c, slope_d)

void Obj_HandleHorizNeoSpringParts(mobj_t *spring)
{
	mobj_t *cur = spring;

	angle_t a = spring->angle;
	fixed_t offsetx = 0;
	fixed_t offsety = 0;

	for (cur = spring->hnext; !P_MobjWasRemoved(cur); cur = cur->hnext)
	{
		switch (neospring_part_side(cur))
		{
			case BACK:
			{
				a = spring->angle;
				offsetx = -10 * FRACUNIT;
				offsety = 0;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, 0);
				P_SetMobjState(cur, S_NEOSPRING_BACK);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->angle = spring->angle - ANGLE_90;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = -1;
				break;
			}

			case FRONT:
			{
				UPDATEOFFSET(cur, spring, 0, 0, 0);
				P_SetMobjState(cur, S_NEOSPRING_FRONT);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->angle = spring->angle + ANGLE_90;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = 1;
				break;
			}

			case SIDEL:
			{
				a = spring->angle - ANGLE_90;
				offsetx = 30 * FRACUNIT;
				offsety = -6 * FRACUNIT;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, 0);
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->angle = spring->angle - ANGLE_180;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = 1;
				break;
			}

			case SIDER:
			{
				a = spring->angle - ANGLE_90;
				offsetx = -32 * FRACUNIT;
				offsety = -4 * FRACUNIT;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, 0);
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->angle = spring->angle + ANGLE_180;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = 1;
				break;
			}

			case BOTTOM:
			{
				a = spring->angle - ANGLE_90;
				offsetx = 32 * FRACUNIT;
				offsety = -6 * FRACUNIT;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, 0);
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_FLOORSPRITE | RF_NOSPLATBILLBOARD;
				cur->angle = spring->angle + ANGLE_90;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = -1;
				break;
			}

			case TOP:
			{
				a = spring->angle - ANGLE_90;
				offsetx = 32 * FRACUNIT;
				offsety = -6 * FRACUNIT;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, 63 * FRACUNIT);
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_FLOORSPRITE | RF_NOSPLATBILLBOARD;
				cur->angle = spring->angle + ANGLE_90;
				cur->scale = spring->scale;
				cur->color = spring->color;
				break;
			}

			default:
				break;
		}
	}
}

void Obj_InitDiagNeoSpringSlopes(mobj_t *spring)
{
	mobj_t *cur = spring;

	for (cur = spring->hnext; !P_MobjWasRemoved(cur); cur = cur->hnext)
	{
		switch (neospring_part_side(cur))
		{
		case TOP:
		case BOTTOM:
		case FRONT:
		case BACK:
		{
			cur->floorspriteslope = (pslope_t *)P_CreateFloorSpriteSlope(cur);
			break;
		}

		default:
			break;
		}
	}

	neospring_slopesetup(spring) = true;
}

// I'd much rather calculate offsets from rotation properly but this works for now
void Obj_HandleVertNeoSpringParts(mobj_t *spring, dboolean diag)
{
	mobj_t *cur = spring;

	angle_t a = spring->angle;
	fixed_t offsetx = 0;
	fixed_t offsety = 0;
	angle_t pitch = diag ? ANG60 : 0;

	for (cur = spring->hnext; !P_MobjWasRemoved(cur); cur = cur->hnext)
	{
		switch (neospring_part_side(cur))
		{
			case BACK:
			{
				a = spring->angle;
				offsetx = 30 * FRACUNIT;
				offsety = 0;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, (-26 * FRACUNIT) * P_MobjFlip(spring));
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->rollangle = ANGLE_90;
				cur->pitch = pitch;
				cur->angle = spring->angle + ANGLE_90;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = 1;

				if (diag)
				{
					cur->rollangle = 0;
					cur->renderflags |= RF_FLOORSPRITE | RF_NOSPLATBILLBOARD | RF_SLOPESPLAT;
					offsetx = (-15 * FRACUNIT) * P_MobjFlip(spring);
					offsety = (-28 * FRACUNIT);
					UPDATEANGLE(a, offsetx, offsety);
					UPDATEOFFSET(cur, spring, offsetx, offsety, (spring->eflags & MFE_VERTICALFLIP) ? 5 * FRACUNIT : 58 * FRACUNIT);
					UPDATEPLANEANGLE(cur->floorspriteslope, spring->angle, ANG30, cur->x, cur->y, cur->z);
					// Thanks Wumbo for your Cuboids mod.
					// The closer the sprite is to being perfectly upright, the more it needs to be squished vertically
					// Otherwise it will stretch to infinity and beyond
					// (also try to prevent seams when upright by applying a very tiny stretch)
					cur->spriteyscale = FixedMul(FINECOSINE(cur->floorspriteslope->zangle >> ANGLETOFINESHIFT), cur->scale + (cur->floorspriteslope->zangle == ANGLE_90 - 1 ? 1024 : 0));
				}
				break;
			}

			case FRONT:
			{
				a = spring->angle;
				offsetx = -30 * FRACUNIT;
				offsety = 0;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, (-26 * FRACUNIT) * P_MobjFlip(spring));
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->rollangle = ANGLE_90;
				cur->pitch = pitch;
				cur->angle = spring->angle + ANGLE_90;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = 1;

				if (diag)
				{
					cur->rollangle = 0;
					cur->renderflags |= RF_FLOORSPRITE | RF_NOSPLATBILLBOARD | RF_SLOPESPLAT;
					offsetx = (15 * FRACUNIT) * P_MobjFlip(spring);
					offsety = (-28 * FRACUNIT);
					UPDATEANGLE(a, offsetx, offsety);
					UPDATEOFFSET(cur, spring, offsetx, offsety, (spring->eflags & MFE_VERTICALFLIP) ? 58 * FRACUNIT : 5 * FRACUNIT);
					UPDATEPLANEANGLE(cur->floorspriteslope, spring->angle, ANG30, cur->x, cur->y, cur->z);
					// Thanks Wumbo for your Cuboids mod.
					// The closer the sprite is to being perfectly upright, the more it needs to be squished vertically
					// Otherwise it will stretch to infinity and beyond
					// (also try to prevent seams when upright by applying a very tiny stretch)
					cur->spriteyscale = FixedMul(FINECOSINE(cur->floorspriteslope->zangle >> ANGLETOFINESHIFT), cur->scale + (cur->floorspriteslope->zangle == ANGLE_90 - 1 ? 1024 : 0));
				}
				break;
			}

			case SIDEL:
			{
				a = spring->angle - ANGLE_90;
				offsetx = 30 * FRACUNIT;
				offsety = 0;
				UPDATEANGLE(a, offsetx, offsety);
				if (diag)
				{
					UPDATEOFFSET(cur, spring, offsetx, offsety, 0);
				}
				else
				{
					UPDATEOFFSET(cur, spring, offsetx, offsety, (-26 * FRACUNIT) * P_MobjFlip(spring));
				}
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->rollangle = ANGLE_90;
				cur->pitch = pitch;
				cur->angle = spring->angle - ANGLE_180;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = 1;
				break;
			}

			case SIDER:
			{
				a = spring->angle - ANGLE_90;
				offsetx = -30 * FRACUNIT;
				offsety = 0;
				UPDATEANGLE(a, offsetx, offsety);
				if (diag)
				{
					UPDATEOFFSET(cur, spring, offsetx, offsety, 0);
				}
				else
				{
					UPDATEOFFSET(cur, spring, offsetx, offsety, (-26 * FRACUNIT) * P_MobjFlip(spring));
				}
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->rollangle = ANGLE_90;
				cur->pitch = pitch;
				cur->angle = spring->angle + ANGLE_180;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = 1;
				break;
			}

			case BOTTOM:
			{
				a = spring->angle;
				offsetx = -30 * FRACUNIT;
				offsety = 0;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, (spring->eflags & MFE_VERTICALFLIP) ? 10 * FRACUNIT : 0);
				P_SetMobjState(cur, S_NEOSPRING_BACK);
				cur->renderflags |= RF_FLOORSPRITE | RF_NOSPLATBILLBOARD | RF_SLOPESPLAT;
				cur->pitch = pitch;
				cur->angle = spring->angle;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = -1;

				if (diag)
				{
					cur->rollangle = ANGLE_180;
					offsetx = 10 * FRACUNIT;
					offsety = 0;
					UPDATEANGLE(a, offsetx, offsety);
					UPDATEOFFSET(cur, spring, offsetx, offsety, ((spring->eflags & MFE_VERTICALFLIP) ? 5 * FRACUNIT : 0 * FRACUNIT));
					UPDATEPLANEANGLE(cur->floorspriteslope, spring->angle, ANGLE_120, cur->x, cur->y, cur->z);
					// Thanks Wumbo for your Cuboids mod.
					// The closer the sprite is to being perfectly upright, the more it needs to be squished vertically
					// Otherwise it will stretch to infinity and beyond
					// (also try to prevent seams when upright by applying a very tiny stretch)
					cur->spriteyscale = FixedMul(FINECOSINE(cur->floorspriteslope->zangle >> ANGLETOFINESHIFT), cur->scale + (cur->floorspriteslope->zangle == ANGLE_90 - 1 ? 1024 : 0));
				}

				break;
			}

			case TOP:
			{
				a = spring->angle;
				offsetx = -30 * FRACUNIT;
				offsety = 0;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, (spring->eflags & MFE_VERTICALFLIP) ? 0 : 10 * FRACUNIT);
				P_SetMobjState(cur, S_NEOSPRING_FRONT);
				cur->renderflags |= RF_FLOORSPRITE | RF_NOSPLATBILLBOARD | RF_SLOPESPLAT;
				cur->pitch = pitch;
				cur->angle = spring->angle;
				cur->scale = spring->scale;
				cur->color = spring->color;
				cur->dispoffset = 2;

				if (diag)
				{
					cur->rollangle = ANGLE_180;
					offsetx = 20 * FRACUNIT;
					offsety = 0;
					UPDATEANGLE(a, offsetx, offsety);
					UPDATEOFFSET(cur, spring, offsetx, offsety, (spring->eflags & MFE_VERTICALFLIP) ? 0 * FRACUNIT : 5 * FRACUNIT);
					UPDATEPLANEANGLE(cur->floorspriteslope, spring->angle, ANGLE_120, cur->x, cur->y, cur->z);
					// Thanks Wumbo for your Cuboids mod.
					// The closer the sprite is to being perfectly upright, the more it needs to be squished vertically
					// Otherwise it will stretch to infinity and beyond
					// (also try to prevent seams when upright by applying a very tiny stretch)
					cur->spriteyscale = FixedMul(FINECOSINE(cur->floorspriteslope->zangle >> ANGLETOFINESHIFT), cur->scale + (cur->floorspriteslope->zangle == ANGLE_90 - 1 ? 1024 : 0));
				}

				break;
			}

			default:
				break;
		}
	}
}

void Obj_ThinkNeoSpring(mobj_t *spring)
{
	if (neospring_setup(spring) == false)
		Obj_InitNeoSpringParts(spring);

	switch (spring->type)
	{
		case MT_NEOSPRING:
			Obj_HandleVertNeoSpringParts(spring, false);
			break;

		case MT_NEODIAG:
			if (neospring_slopesetup(spring) == false)
				Obj_InitDiagNeoSpringSlopes(spring);

			Obj_HandleVertNeoSpringParts(spring, true);
			break;

		case MT_NEOHORIZ:
			Obj_HandleHorizNeoSpringParts(spring);
			break;

		default:
			break;
	}
}
