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

#define neospring_part_side(part) part->threshold

skincolornum_t Obj_HandleNeoSpringColor(fixed_t speedval)
{
	if (speedval >= 70 *FRACUNIT)
	{
		return SKINCOLOR_RED;
	}
	else if (speedval > 40*FRACUNIT && speedval < 70*FRACUNIT)
	{
		return SKINCOLOR_YELLOW;
	}
	else if (speedval > 30*FRACUNIT && speedval <= 40*FRACUNIT)
	{
		return SKINCOLOR_BLUEBERRY;
	}
	else if (speedval > 0 && speedval <= 30 *FRACUNIT)
	{
		return SKINCOLOR_GREY;
	}
	else
	{
		return SKINCOLOR_WHITE;
	}
}

#define SETUPNEXTPART prev = cur; cur = cur->hnext; cur->hprev = prev;

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

#define UPDATEOFFSET(cur, spring, ofsx, ofsy, ofsz)  \
P_MoveOrigin( \
	cur, \
	spring->x+spring->sprxoff+FixedMul(ofsx, spring->scale), \
	spring->y+spring->spryoff+FixedMul(ofsy, spring->scale), \
	spring->z+spring->sprzoff+FixedMul(ofsz, spring->scale))

#define UPDATEANGLE(angle, ofsx, ofsy) \
	fixed_t ofsxbak = ofsx; \
	fixed_t ofsybak = ofsy; \
	ofsx = FixedMul(ofsxbak, FINECOSINE(angle >> ANGLETOFINESHIFT)) - FixedMul(ofsybak, FINESINE(angle >> ANGLETOFINESHIFT)); \
	ofsy = FixedMul(ofsxbak, FINESINE(angle >> ANGLETOFINESHIFT)) + FixedMul(ofsybak, FINECOSINE(angle >> ANGLETOFINESHIFT)); \


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
				offsetx = -10*FRACUNIT;
				offsety = 0;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx,offsety, 0);
				P_SetMobjState(cur, S_NEOSPRING_BACK);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->angle = spring->angle - ANGLE_90;
				cur->scale = spring->scale;
				cur->color = spring->color;
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
				break;
			}

			case SIDEL:
			{
				a = spring->angle - ANGLE_90;
				offsetx = 30*FRACUNIT;
				offsety = -6*FRACUNIT;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, 0);
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->angle = spring->angle - ANGLE_180;
				cur->scale = spring->scale;
				cur->color = spring->color;
				break;
			}

			case SIDER:
			{
				a = spring->angle - ANGLE_90;
				offsetx = -32*FRACUNIT;
				offsety = -4*FRACUNIT;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, 0);
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_PAPERSPRITE;
				cur->angle = spring->angle + ANGLE_180;
				cur->scale = spring->scale;
				cur->color = spring->color;
				break;
			}

			case BOTTOM:
			{
				a = spring->angle - ANGLE_90;
				offsetx = 32*FRACUNIT;
				offsety = -6*FRACUNIT;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, 0);
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_FLOORSPRITE|RF_NOSPLATBILLBOARD;
				cur->angle = spring->angle + ANGLE_90;
				cur->scale = spring->scale;
				cur->color = spring->color;
				break;
			}

			case TOP:
			{
				a = spring->angle - ANGLE_90;
				offsetx = 32*FRACUNIT;
				offsety = -6*FRACUNIT;
				UPDATEANGLE(a, offsetx, offsety);
				UPDATEOFFSET(cur, spring, offsetx, offsety, 63*FRACUNIT);
				P_SetMobjState(cur, S_NEOSPRING_SIDE);
				cur->renderflags |= RF_FLOORSPRITE|RF_NOSPLATBILLBOARD;
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

void Obj_ThinkNeoSpring(mobj_t * spring)
{
	if (neospring_setup(spring) == false)
		Obj_InitNeoSpringParts(spring);

	switch (spring->type)
	{

		case MT_NEOSPRING:
			break;

		case MT_NEODIAG:
			break;

		case MT_NEOHORIZ:
			Obj_HandleHorizNeoSpringParts(spring);
			break;

		default:
			break;
	}
}
