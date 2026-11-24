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
/// \file  p_maputl.c
/// \brief Movement/collision utility functions, as used by functions in p_map.c
///        Blockmap iterator functions, and some PIT_* functions to use for iteration

#include "doomdef.h"
#include "cl_predict.h"
#include "doomstat.h"

#include "k_kart.h"
#include "p_local.h"
#include "r_main.h"
#include "r_data.h"
#include "r_textures.h"
#include "p_maputl.h"
#include "p_polyobj.h"
#include "p_slopes.h"
#include "z_zone.h"

//
// P_ClosestPointOnLine
// Finds the closest point on a given line to the supplied point
//
void P_ClosestPointOnLine(fixed_t x, fixed_t y, const line_t *line, vertex_t *result)
{
	fixed_t startx = line->v1->x;
	fixed_t starty = line->v1->y;
	fixed_t dx = line->dx;
	fixed_t dy = line->dy;

	// Determine t (the length of the vector from �Line[0]� to �p�)
	fixed_t cx, cy;
	fixed_t vx, vy;
	fixed_t magnitude;
	fixed_t t;

	//Sub (p, &Line[0], &c);
	cx = x - startx;
	cy = y - starty;

	//Sub (&Line[1], &Line[0], &V);
	vx = dx;
	vy = dy;

	//Normalize (&V, &V);
	magnitude = R_PointToDist2(line->v2->x, line->v2->y, startx, starty);
	vx = FixedDiv(vx, magnitude);
	vy = FixedDiv(vy, magnitude);

	t = (FixedMul(vx, cx) + FixedMul(vy, cy));

	// Return the point between �Line[0]� and �Line[1]�
	vx = FixedMul(vx, t);
	vy = FixedMul(vy, t);

	//Add (&Line[0], &V, out);
	result->x = startx + vx;
	result->y = starty + vy;
	return;
}

/// Similar to FV3_ClosestPointOnLine() except it actually works.
void P_ClosestPointOnLine3D(const vector3_t *p, const vector3_t *Line, vector3_t *result)
{
	const vector3_t* v1 = &Line[0];
	const vector3_t* v2 = &Line[1];
	vector3_t c, V, n;
	fixed_t t, d;
	FV3_SubEx(v2, v1, &V);
	FV3_SubEx(p, v1, &c);

	d = R_PointToDist2(0, v2->z, R_PointToDist2(v2->x, v2->y, v1->x, v1->y), v1->z);
	vector3_t vd = { d, d, d };
	FV3_Copy(&n, &V);
	FV3_Divide(&n, &vd);

	t = FV3_Dot(&n, &c);

	// Set closest point to the end if it extends past -Red
	if (t <= 0)
	{
		FV3_Copy(result, v1);
		return;
	}
	else if (t >= d)
	{
		FV3_Copy(result, v2);
		return;
	}

	vector3_t tv = { t, t, t };
	FV3_Mul(&n, &tv);

	FV3_AddEx(v1, &n, result);
	return;
}

//
// P_PointOnLineSide
// Returns 0 or 1
//
// killough 5/3/98: reformatted, cleaned up
// ioanch 20151228: made line const
//
int32_t P_PointOnLineSide(fixed_t x, fixed_t y, const line_t *line)
{
	return
		!line->dx ? x <= line->v1->x ? line->dy > 0 : line->dy < 0 :
		!line->dy ? y <= line->v1->y ? line->dx < 0 : line->dx > 0 :
		((int64_t)y - line->v1->y) * line->dx >= line->dy * ((int64_t)x - line->v1->x);
}

//
// P_BoxOnLineSide
// Considers the line to be infinite
// Returns side 0 or 1, -1 if box crosses the line.
//
// killough 5/3/98: reformatted, cleaned up
//
int32_t P_BoxOnLineSide(const fixed_t *tmbox, const line_t *ld)
{
	int32_t p;

	switch (ld->slopetype)
	{
		default: // shut up compiler warnings -- killough
		case ST_HORIZONTAL:
			return
				(tmbox[BOXBOTTOM] > ld->v1->y) == (p = tmbox[BOXTOP] > ld->v1->y) ?
				p ^ (ld->dx < 0) : -1;
		case ST_VERTICAL:
			return
				(tmbox[BOXLEFT] < ld->v1->x) == (p = tmbox[BOXRIGHT] < ld->v1->x) ?
				p ^ (ld->dy < 0) : -1;
		case ST_POSITIVE:
			return
				P_PointOnLineSide(tmbox[BOXRIGHT], tmbox[BOXBOTTOM], ld) ==
				(p = P_PointOnLineSide(tmbox[BOXLEFT], tmbox[BOXTOP], ld)) ? p : -1;
		case ST_NEGATIVE:
			return
				(P_PointOnLineSide(tmbox[BOXLEFT], tmbox[BOXBOTTOM], ld)) ==
				(p = P_PointOnLineSide(tmbox[BOXRIGHT], tmbox[BOXTOP], ld)) ? p : -1;
	}
}

//
// P_PointOnDivlineSide
// Returns 0 or 1.
//
// killough 5/3/98: reformatted, cleaned up
//
static int32_t P_PointOnDivlineSide(fixed_t x, fixed_t y, const divline_t *line)
{
	return
		line->dx == 0 ? x <= line->x ? line->dy > 0 : line->dy < 0 :
		line->dy == 0 ? y <= line->y ? line->dx < 0 : line->dx > 0 :
		(line->dy ^ line->dx ^ (x -= line->x) ^ (y -= line->y)) < 0 ? (line->dy ^ x) < 0 :
		(int64_t)(y) * line->dx >= (int64_t)(line->dy) * x;
}

//
// P_MakeDivline
//
// ioanch 20151230: made const
//
void P_MakeDivline(const line_t *li, divline_t *dl)
{
	dl->x = li->v1->x;
	dl->y = li->v1->y;
	dl->dx = li->dx;
	dl->dy = li->dy;
}

//
// P_InterceptVector
// Returns the fractional intercept point along the first divline.
// This is only called by the addthings and addlines traversers.
//
// killough 5/3/98: reformatted, cleaned up
// ioanch 20151229: added const
//
fixed_t P_InterceptVector(const divline_t *v2, const divline_t *v1)
{
	// This is from PRBoom+ by Colin Phipps , GPL 2
	// no precision/overflow problems
	int64_t den = (int64_t)(v1->dy) * v2->dx - (int64_t)(v1->dx) * v2->dy;
	den >>= 16;

	if (!den)
	{
		return 0;
	}

	return (fixed_t)(((int64_t)(v1->x - v2->x) * v1->dy - (int64_t)(v1->y - v2->y) * v1->dx) / den);
}

static fixed_t dist2line(const line_t *ld, const fixed_t x, const fixed_t y)
{
	return FixedHypot
		(
				ld->v1->x + (ld->dx / 2) - x,
				ld->v1->y + (ld->dy / 2) - y
		);
}

static void checknearline
(		line_t  * line,
		fixed_t * nearest,
		line_t ** near_line,
		const fixed_t x,
		const fixed_t y)
{
	const fixed_t d = dist2line(line, x, y);

	if (d < *nearest)
	{
		*nearest = d;
		*near_line = line;
	}
}

//
// P_FindNearestLine
// Returns the nearest line to a point which
// is in a sector and/or a specific type.
//
line_t * P_FindNearestLine
(		const fixed_t    x,
		const fixed_t    y,
		const sector_t * sector,
		const int32_t      special)
{
	fixed_t nearest = INT32_MAX;
	line_t *near_line = NULL;
	size_t i;
	int32_t line = -1;

	if (special == -1)
	{
		if (sector == NULL)
			sector = R_PointInSubsector(x, y)->sector;

		for (i = 0; i < sector->linecount; ++i)
		{
			checknearline(sector->lines[i], &nearest, &near_line, x, y);
		}
	}
	else if (sector != NULL)
	{
		for (i = 0; i < sector->linecount; ++i)
		{
			if (sector->lines[i]->special == special)
				checknearline(sector->lines[i], &nearest, &near_line, x, y);
		}
	}
	else
	{
		// if all linedefs are exhausted, this returns numlines...
		while (line = P_FindSpecialLineFromTag(special, -1, line), line != -1 && line != (int32_t)numlines)
		{
			checknearline(&lines[line], &nearest, &near_line, x, y);
		}
	}

	return near_line;
}

//
// P_LineOpening
// Sets opentop and openbottom to the window through a two sided line.
// OPTIMIZE: keep this precalculated
//

// P_CameraLineOpening
// P_LineOpening, but for camera
// Tails 09-29-2002
void P_CameraLineOpening(line_t *linedef, opening_t *open)
{
	sector_t *front;
	sector_t *back;
	fixed_t frontfloor, frontceiling, backfloor, backceiling;
	fixed_t thingtop;

	open->ceiling = open->highceiling = INT32_MAX;
	open->floor = open->lowfloor = INT32_MIN;
	open->range = 0;

	if (linedef->sidenum[1] == NO_INDEX)
	{
		// single sided line
		return;
	}

	front = linedef->frontsector;
	back = linedef->backsector;

	// Cameras use the heightsec's heights rather then the actual sector heights.
	// If you can see through it, why not move the camera through it too?
	if (front->camsec >= 0)
	{
		// SRB2CBTODO: ESLOPE (sectors[front->heightsec].f_slope)
		frontfloor   = P_GetSectorFloorZAt  (&sectors[front->camsec], mapcampointer->x, mapcampointer->y);
		frontceiling = P_GetSectorCeilingZAt(&sectors[front->camsec], mapcampointer->x, mapcampointer->y);
	}
	else if (front->heightsec >= 0)
	{
		// SRB2CBTODO: ESLOPE (sectors[front->heightsec].f_slope)
		frontfloor   = P_GetSectorFloorZAt  (&sectors[front->heightsec], mapcampointer->x, mapcampointer->x);
		frontceiling = P_GetSectorCeilingZAt(&sectors[front->heightsec], mapcampointer->x, mapcampointer->y);
	}
	else
	{
		frontfloor   = P_CameraGetFloorZ  (mapcampointer, front, g_tm.x, g_tm.y, linedef);
		frontceiling = P_CameraGetCeilingZ(mapcampointer, front, g_tm.x, g_tm.y, linedef);
	}

	if (back->camsec >= 0)
	{
		// SRB2CBTODO: ESLOPE (sectors[back->heightsec].f_slope)
		backfloor   = P_GetSectorFloorZAt  (&sectors[back->camsec], mapcampointer->x, mapcampointer->y);
		backceiling = P_GetSectorCeilingZAt(&sectors[back->camsec], mapcampointer->x, mapcampointer->y);
	}
	else if (back->heightsec >= 0)
	{
		// SRB2CBTODO: ESLOPE (sectors[back->heightsec].f_slope)
		backfloor   = P_GetSectorFloorZAt  (&sectors[back->heightsec], mapcampointer->x, mapcampointer->y);
		backceiling = P_GetSectorCeilingZAt(&sectors[back->heightsec], mapcampointer->x, mapcampointer->y);
	}
	else
	{
		backfloor = P_CameraGetFloorZ(mapcampointer, back, g_tm.x, g_tm.y, linedef);
		backceiling = P_CameraGetCeilingZ(mapcampointer, back, g_tm.x, g_tm.y, linedef);
	}

	thingtop = mapcampointer->z + mapcampointer->height;

	if (frontceiling < backceiling)
	{
		open->ceiling = frontceiling;
		open->highceiling = backceiling;
	}
	else
	{
		open->ceiling = backceiling;
		open->highceiling = frontceiling;
	}

	if (frontfloor > backfloor)
	{
		open->floor = frontfloor;
		open->lowfloor = backfloor;
	}
	else
	{
		open->floor = backfloor;
		open->lowfloor = frontfloor;
	}

	// Check for fake floors in the sector.
	if (front->ffloors || back->ffloors)
	{
		ffloor_t *rover;
		fixed_t delta1, delta2;

		// Check for frontsector's fake floors
		if (front->ffloors)
			for (rover = front->ffloors; rover; rover = rover->next)
			{
				fixed_t topheight, bottomheight;
				if (!(rover->fofflags & FOF_BLOCKOTHERS) || !(rover->fofflags & FOF_RENDERALL) || !(rover->fofflags & FOF_EXISTS) )
					continue;

				topheight = P_CameraGetFOFTopZ(mapcampointer, front, rover, g_tm.x, g_tm.y, linedef);
				bottomheight = P_CameraGetFOFBottomZ(mapcampointer, front, rover, g_tm.x, g_tm.y, linedef);

				delta1 = abs(mapcampointer->z - (bottomheight + ((topheight - bottomheight)/2)));
				delta2 = abs(thingtop - (bottomheight + ((topheight - bottomheight)/2)));
				if (bottomheight < open->ceiling && delta1 >= delta2)
					open->ceiling = bottomheight;
				else if (bottomheight < open->highceiling && delta1 >= delta2)
					open->highceiling = bottomheight;

				if (topheight > open->floor && delta1 < delta2)
					open->floor = topheight;
				else if (topheight > open->lowfloor && delta1 < delta2)
					open->lowfloor = topheight;
			}

		// Check for backsectors fake floors
		if (back->ffloors)
			for (rover = back->ffloors; rover; rover = rover->next)
			{
				fixed_t topheight, bottomheight;
				if (!(rover->fofflags & FOF_BLOCKOTHERS) || !(rover->fofflags & FOF_RENDERALL) || !(rover->fofflags & FOF_EXISTS) )
					continue;

				topheight = P_CameraGetFOFTopZ(mapcampointer, back, rover, g_tm.x, g_tm.y, linedef);
				bottomheight = P_CameraGetFOFBottomZ(mapcampointer, back, rover, g_tm.x, g_tm.y, linedef);

				delta1 = abs(mapcampointer->z - (bottomheight + ((topheight - bottomheight)/2)));
				delta2 = abs(thingtop - (bottomheight + ((topheight - bottomheight)/2)));
				if (bottomheight < open->ceiling && delta1 >= delta2)
					open->ceiling = bottomheight;
				else if (bottomheight < open->highceiling && delta1 >= delta2)
					open->highceiling = bottomheight;

				if (topheight > open->floor && delta1 < delta2)
					open->floor = topheight;
				else if (topheight > open->lowfloor && delta1 < delta2)
					open->lowfloor = topheight;
			}
	}

	open->range = (open->ceiling - open->floor);
}

dboolean
P_GetMidtextureTopBottom
(		line_t * linedef,
		opening_t *open,
		fixed_t * return_top,
		fixed_t * return_bottom)
{
	side_t *side = &sides[linedef->sidenum[0]];
	fixed_t textop, texbottom, texheight;
	int32_t texnum = R_GetTextureNum(side->midtexture); // make sure the texture is actually valid

	//sector_t *front = linedef->frontsector;
	//sector_t *back = linedef->backsector;
	//fixed_t z;

	if (!texnum)
		return false;
	// Get the midtexture's height
	texheight = textures[texnum]->height << FRACBITS;

	if (g_tm.sweep)
	{
		// Sweep Midtexture lines to prevent issues with some midtextures.
		P_TestLine(linedef);
	}

	// Set texbottom and textop to the Z coordinates of the texture's boundaries
#if 0
	// don't remove this code unless solid midtextures
	// on non-solid polyobjects should NEVER happen in the future
	if (linedef->polyobj && (linedef->polyobj->flags & POF_TESTHEIGHT))
	{
		if ((linedef->flags & ML_WRAPMIDTEX) && !side->repeatcnt) // "infinite" repeat
		{
			texbottom = back->floorheight + side->rowoffset + side->offsety_mid;
			textop = back->ceilingheight + side->rowoffset + side->offsety_mid;
		}
		else if (linedef->flags & ML_MIDPEG)
		{
			texbottom = back->floorheight + side->rowoffset + side->offsety_mid;
			textop = texbottom + texheight*(side->repeatcnt+1);
		}
		else
		{
			textop = back->ceilingheight + side->rowoffset + side->offsety_mid;
			texbottom = textop - texheight*(side->repeatcnt+1);
		}
	}
	else
#endif
	{
		if ((linedef->flags & ML_WRAPMIDTEX) && !side->repeatcnt) // "infinite" repeat
		{
			texbottom = open->floor + side->rowoffset + side->offsety_mid;
			textop = open->ceiling + side->rowoffset + side->offsety_mid;
		}
		else if (linedef->flags & ML_MIDPEG)
		{
			texbottom = open->floor + side->rowoffset + side->offsety_mid;
			textop = texbottom + texheight*(side->repeatcnt+1);
		}
		else
		{
			textop = open->ceiling + side->rowoffset + side->offsety_mid;
			texbottom = textop - texheight*(side->repeatcnt+1);
		}
	}

	if (return_top)
		*return_top = textop;

	if (return_bottom)
		*return_bottom = texbottom;

	return true;
}

dboolean
P_GetTripwireTopBottom
(		line_t * linedef,
		fixed_t x,
		fixed_t y,
		fixed_t * return_top,
		fixed_t * return_bottom)
{
	side_t *side = &sides[linedef->sidenum[0]];
	fixed_t textop, texbottom, texheight;
	int32_t texnum = R_GetTextureNum(side->midtexture); // make sure the texture is actually valid

	sector_t *front = linedef->frontsector;
	sector_t *back = linedef->backsector;
	fixed_t z;

	if (!texnum)
		return false;

	textop = P_GetSectorCeilingZAt(front, x, y);
	texbottom = P_GetSectorFloorZAt(front, x, y);

	if (back)
	{
		z = P_GetSectorCeilingZAt(back, x, y);

		if (z < textop)
			textop = z;

		z = P_GetSectorFloorZAt(back, x, y);

		if (z > texbottom)
			texbottom = z;
	}

	// Get the midtexture's height
	texheight = textures[texnum]->height << FRACBITS;

	// Set texbottom and textop to the Z coordinates of the texture's boundaries
#if 0
	// don't remove this code unless solid midtextures
	// on non-solid polyobjects should NEVER happen in the future
	if (linedef->polyobj && (linedef->polyobj->flags & POF_TESTHEIGHT))
	{
		if ((linedef->flags & ML_WRAPMIDTEX) && !side->repeatcnt) // "infinite" repeat
		{
			texbottom = back->floorheight + side->rowoffset;
			textop = back->ceilingheight + side->rowoffset;
		}
		else if (linedef->flags & ML_MIDPEG)
		{
			texbottom = back->floorheight + side->rowoffset;
			textop = texbottom + texheight*(side->repeatcnt+1);
		}
		else
		{
			textop = back->ceilingheight + side->rowoffset;
			texbottom = textop - texheight*(side->repeatcnt+1);
		}
	}
	else
#endif
	{
		if ((linedef->flags & ML_WRAPMIDTEX) && !side->repeatcnt) // "infinite" repeat
		{
			texbottom += side->rowoffset;
			textop += side->rowoffset;
		}
		else if (linedef->flags & ML_MIDPEG)
		{
			texbottom += side->rowoffset;
			textop = texbottom + texheight*(side->repeatcnt+1);
		}
		else
		{
			textop += side->rowoffset;
			texbottom = textop - texheight*(side->repeatcnt+1);
		}
	}

	if (return_top)
		*return_top = textop;

	if (return_bottom)
		*return_bottom = texbottom;

	return true;
}

dboolean P_MidtextureIsSolid(line_t *linedef, mobj_t *mobj)
{
	if (linedef->polyobj)
	{
		// don't do anything for polyobjects! ...for now
		return false;
	}

	if (P_IsLineTripWire(linedef) == true)
	{
		// Tripwire behavior.
		return (mobj->player != NULL && K_TripwirePass(mobj->player) == false);
	}

	// Determined solely by the flag.
	return ((linedef->flags & ML_MIDSOLID) == ML_MIDSOLID);
}

static void P_ProcessMidtextureTopBottom
(
	fixed_t textop,
	fixed_t texbottom,
	mobj_t *mobj,
	fixed_t thingtop,
	opening_t *open,
	fixed_t *topedge,
	fixed_t *botedge,
	int hi,
	int lo
)
{
	fixed_t texmid, delta1, delta2;
	texmid = texbottom+(textop-texbottom)/2;

	delta1 = abs(mobj->z - texmid);
	delta2 = abs(thingtop - texmid);

	if (delta1 > delta2)
	{
		// Below
		if (open->ceiling > texbottom)
		{
			topedge[lo] -= ( open->ceiling - texbottom );

			open->ceiling = texbottom;
			open->ceilingstep = ( thingtop    - topedge[lo] );
			open->ceilingdrop = ( topedge[hi] - topedge[lo] );
		}
	}
	else
	{
		// Above
		if (open->floor < textop)
		{
			botedge[hi] += ( textop - open->floor );

			open->floor = textop;
			open->floorstep = ( botedge[hi] - mobj->z );
			open->floordrop = ( botedge[hi] - botedge[lo] );
		}
	}
}

void P_LineOpening(line_t *linedef, mobj_t *mobj, opening_t *open)
{
	enum { FRONT, BACK };

	sector_t *front, *back;
	fixed_t thingtop = 0;
	vertex_t cross;

	/* these init to shut compiler up */
	fixed_t topedge[2] = {0};
	fixed_t botedge[2] = {0};

	int hi = 0;
	int lo = 0;

	// set these defaults so that polyobjects don't interfere with collision above or below them
	open->ceiling = open->highceiling = INT32_MAX;
	open->floor = open->lowfloor = INT32_MIN;
	open->range = 0;
	open->ceilingslope = open->floorslope = NULL;
	open->ceilingrover = open->floorrover = NULL;
	open->ceilingpic = open->floorpic = -1;
	open->ceilingstep = open->floorstep = 0;
	open->ceilingdrop = open->floordrop = 0;

	if (linedef->sidenum[1] == NO_INDEX)
	{
		// single sided line
		return;
	}

	P_ClosestPointOnLine(g_tm.x, g_tm.y, linedef, &cross);

	// Treat polyobjects kind of like 3D Floors
	if (linedef->polyobj && (linedef->polyobj->flags & POF_TESTHEIGHT))
	{
		front = linedef->frontsector;
		back = linedef->frontsector;
	}
	else
	{
		front = linedef->frontsector;
		back = linedef->backsector;
	}

	I_Assert(front != NULL);
	I_Assert(back != NULL);

	if (mobj)
	{
		thingtop = mobj->z + mobj->height;
	}

	if (!linedef->polyobj)
	{
		// Set open and high/low values here
		fixed_t          height[2];
		const sector_t * sector[2] = { front, back };

		height[FRONT] = P_GetCeilingZ(mobj, front, g_tm.x, g_tm.y, linedef);
		height[BACK]  = P_GetCeilingZ(mobj, back,  g_tm.x, g_tm.y, linedef);

		hi = ( height[0] < height[1] );
		lo = ! hi;

		open->ceiling			= height[lo];
		open->highceiling		= height[hi];
		open->ceilingslope		= sector[lo]->c_slope;
		open->ceilingpic		= sector[lo]->ceilingpic;

		if (mobj)
		{
			topedge[FRONT] = P_GetSectorCeilingZAt(front, cross.x, cross.y);
			topedge[BACK]  = P_GetSectorCeilingZAt(back,  cross.x, cross.y);

			open->ceilingstep = ( thingtop    - topedge[lo] );
			open->ceilingdrop = ( topedge[hi] - topedge[lo] );
		}

		height[FRONT] = P_GetFloorZ(mobj, front, g_tm.x, g_tm.y, linedef);
		height[BACK]  = P_GetFloorZ(mobj, back,  g_tm.x, g_tm.y, linedef);

		hi = ( height[0] < height[1] );
		lo = ! hi;

		open->floor				= height[hi];
		open->lowfloor			= height[lo];
		open->floorslope		= sector[hi]->f_slope;
		open->floorpic			= sector[hi]->floorpic;

		if (mobj)
		{
			botedge[FRONT] = P_GetSectorFloorZAt(front, cross.x, cross.y);
			botedge[BACK]  = P_GetSectorFloorZAt(back,  cross.x, cross.y);

			open->floorstep = ( botedge[hi] - mobj->z );
			open->floordrop = ( botedge[hi] - botedge[lo] );
		}
	}

	if (mobj)
	{
		// Check for collision with front side's midtexture if Effect 4 is set
		if (P_MidtextureIsSolid(linedef, mobj) == true)
		{
			fixed_t textop, texbottom;

			if (P_IsLineTripWire(linedef) && P_GetTripwireTopBottom(linedef, cross.x, cross.y, &textop, &texbottom))
			{
				P_ProcessMidtextureTopBottom(textop, texbottom, mobj, thingtop, open, topedge, botedge, hi, lo);
			}
			else if (P_GetMidtextureTopBottom(linedef, open, &textop, &texbottom))
			{
				P_ProcessMidtextureTopBottom(textop, texbottom, mobj, thingtop, open, topedge, botedge, hi, lo);
			}
		}

		if (linedef->polyobj)
		{
			// Treat polyobj's backsector like a 3D Floor
			if (linedef->polyobj->flags & POF_TESTHEIGHT)
			{
				const sector_t *polysec = linedef->backsector;
				fixed_t polytop, polybottom, polymid;
				fixed_t delta1, delta2;

				if (linedef->polyobj->flags & POF_CLIPPLANES)
				{
					polytop = polysec->ceilingheight;
					polybottom = polysec->floorheight;
				}
				else
				{
					polytop = INT32_MAX;
					polybottom = INT32_MIN;
				}

				switch (open->fofType)
				{
					case LO_FOF_FLOORS:
					{
						if (mobj->z >= polytop)
						{
							if (polytop > open->floor)
							{
								open->floor = polytop;
							}
							else if (polytop > open->lowfloor)
							{
								open->lowfloor = polytop;
							}
						}
						break;
					}
					case LO_FOF_CEILINGS:
					{
						if (thingtop <= polybottom)
						{
							if (polybottom < open->ceiling)
							{
								open->ceiling = polybottom;
							}
							else if (polybottom < open->highceiling)
							{
								open->highceiling = polybottom;
							}
						}
						break;
					}
					default:
					{
						polymid = polybottom + (polytop - polybottom) / 2;
						delta1 = abs(mobj->z - polymid);
						delta2 = abs(thingtop - polymid);

						if (delta1 > delta2)
						{
							if (polybottom < open->ceiling)
							{
								open->ceiling = polybottom;
							}
							else if (polybottom < open->highceiling)
							{
								open->highceiling = polybottom;
							}
						}
						else
						{
							if (polytop > open->floor)
							{
								open->floor = polytop;
							}
							else if (polytop > open->lowfloor)
							{
								open->lowfloor = polytop;
							}
						}
						break;
					}
				}
			}
			// otherwise don't do anything special, pretend there's nothing else there
		}
		else
		{
			// Check for fake floors in the sector.
			if (front->ffloors || back->ffloors)
			{
				ffloor_t *rover;
				fixed_t delta1, delta2;

				/* yuck */
				struct
				{
					fixed_t ceiling;
					fixed_t floor;
					ffloor_t * ceilingrover;
					ffloor_t *   floorrover;
				} fofopen[2] = {
					{ INT32_MAX, INT32_MIN, NULL, NULL },
					{ INT32_MAX, INT32_MIN, NULL, NULL },
				};

				// Check for frontsector's fake floors
				for (rover = front->ffloors; rover; rover = rover->next)
				{
					fixed_t topheight, bottomheight, midheight;

					if (!(rover->fofflags & FOF_EXISTS))
						continue;

					if (P_CheckSolidFFloorSurface(mobj, rover))
						;
					else if (!((rover->fofflags & FOF_BLOCKPLAYER && mobj->player)
						|| (rover->fofflags & FOF_BLOCKOTHERS && !mobj->player)))
						continue;

					if (open->fofType != LO_FOF_ANY)
					{
						topheight = P_VeryTopOfFOF(rover);
						bottomheight = P_VeryBottomOfFOF(rover);
					}
					else
					{
						topheight = P_GetFOFTopZ(mobj, front, rover, g_tm.x, g_tm.y, linedef);
						bottomheight = P_GetFOFBottomZ(mobj, front, rover, g_tm.x, g_tm.y, linedef);
					}

					switch (open->fofType)
					{
						case LO_FOF_FLOORS:
						{
							if (mobj->z >= topheight)
							{
								if ((rover->fofflags & FOF_INTANGIBLEFLATS) != FOF_REVERSEPLATFORM)
								{
									if (topheight > fofopen[FRONT].floor)
									{
										fofopen[FRONT].floor = topheight;
										fofopen[FRONT].floorrover = rover;
									}
								}
							}
							break;
						}
						case LO_FOF_CEILINGS:
						{
							if (thingtop <= bottomheight)
							{
								if ((rover->fofflags & FOF_INTANGIBLEFLATS) != FOF_PLATFORM)
								{
									if (bottomheight < fofopen[FRONT].ceiling)
									{
										fofopen[FRONT].ceiling = bottomheight;
										fofopen[FRONT].ceilingrover = rover;
									}
								}
							}
							break;
						}
						default:
						{
							midheight = bottomheight + (topheight - bottomheight) / 2;
							delta1 = abs(mobj->z - midheight);
							delta2 = abs(thingtop - midheight);

							if (delta1 > delta2)
							{
								// thing is below FOF
								if ((rover->fofflags & FOF_INTANGIBLEFLATS) != FOF_PLATFORM)
								{
									if (bottomheight < fofopen[FRONT].ceiling)
									{
										fofopen[FRONT].ceiling = bottomheight;
										fofopen[FRONT].ceilingrover = rover;
									}
								}
							}
							else
							{
								// thing is above FOF
								if ((rover->fofflags & FOF_INTANGIBLEFLATS) != FOF_REVERSEPLATFORM)
								{
									if (topheight > fofopen[FRONT].floor)
									{
										fofopen[FRONT].floor = topheight;
										fofopen[FRONT].floorrover = rover;
									}
								}
							}
							break;
						}
					}
				}

				// Check for backsectors fake floors
				for (rover = back->ffloors; rover; rover = rover->next)
				{
					fixed_t topheight, bottomheight, midheight;

					if (!(rover->fofflags & FOF_EXISTS))
						continue;

					if (P_CheckSolidFFloorSurface(mobj, rover))
						;
					else if (!((rover->fofflags & FOF_BLOCKPLAYER && mobj->player)
						|| (rover->fofflags & FOF_BLOCKOTHERS && !mobj->player)))
						continue;

					if (open->fofType != LO_FOF_ANY)
					{
						topheight = P_VeryTopOfFOF(rover);
						bottomheight = P_VeryBottomOfFOF(rover);
					}
					else
					{
						topheight = P_GetFOFTopZ(mobj, back, rover, g_tm.x, g_tm.y, linedef);
						bottomheight = P_GetFOFBottomZ(mobj, back, rover, g_tm.x, g_tm.y, linedef);
					}

					switch (open->fofType)
					{
						case LO_FOF_FLOORS:
						{
							if (mobj->z >= topheight)
							{
								if ((rover->fofflags & FOF_INTANGIBLEFLATS) != FOF_REVERSEPLATFORM)
								{
									if (topheight > fofopen[BACK].floor)
									{
										fofopen[BACK].floor = topheight;
										fofopen[BACK].floorrover = rover;
									}
								}
							}
							break;
						}
						case LO_FOF_CEILINGS:
						{
							if (thingtop <= bottomheight)
							{
								if ((rover->fofflags & FOF_INTANGIBLEFLATS) != FOF_PLATFORM)
								{
									if (bottomheight < fofopen[BACK].ceiling)
									{
										fofopen[BACK].ceiling = bottomheight;
										fofopen[BACK].ceilingrover = rover;
									}
								}
							}
							break;
						}
						default:
						{
							midheight = bottomheight + (topheight - bottomheight) / 2;
							delta1 = abs(mobj->z - midheight);
							delta2 = abs(thingtop - midheight);

							if (delta1 > delta2)
							{
								// thing is below FOF
								if ((rover->fofflags & FOF_INTANGIBLEFLATS) != FOF_PLATFORM)
								{
									if (bottomheight < fofopen[BACK].ceiling)
									{
										fofopen[BACK].ceiling = bottomheight;
										fofopen[BACK].ceilingrover = rover;
									}
								}
							}
							else
							{
								// thing is above FOF
								if ((rover->fofflags & FOF_INTANGIBLEFLATS) != FOF_REVERSEPLATFORM)
								{
									if (topheight > fofopen[BACK].floor)
									{
										fofopen[BACK].floor = topheight;
										fofopen[BACK].floorrover = rover;
									}
								}
							}
							break;
						}
					}
				}

				hi = ( fofopen[0].ceiling < fofopen[1].ceiling );
				lo = ! hi;

				if (fofopen[lo].ceiling <= open->ceiling)
				{
					topedge[lo] = P_GetFFloorBottomZAt(fofopen[lo].ceilingrover, cross.x, cross.y);

					if (fofopen[hi].ceiling < open->ceiling)
					{
						topedge[hi] = P_GetFFloorBottomZAt(fofopen[hi].ceilingrover, cross.x, cross.y);
					}

					open->ceiling			= fofopen[lo].ceiling;
					open->ceilingrover		= fofopen[lo].ceilingrover;
					open->ceilingslope		= *fofopen[lo].ceilingrover->b_slope;
					open->ceilingpic		= *fofopen[lo].ceilingrover->bottompic;
					open->ceilingstep		= ( thingtop    - topedge[lo] );
					open->ceilingdrop		= ( topedge[hi] - topedge[lo] );

					if (fofopen[hi].ceiling < open->highceiling)
					{
						open->highceiling = fofopen[hi].ceiling;
					}
				}
				else if (fofopen[lo].ceiling < open->highceiling)
				{
					open->highceiling = fofopen[lo].ceiling;
				}

				hi = ( fofopen[0].floor < fofopen[1].floor );
				lo = ! hi;

				if (fofopen[hi].floor >= open->floor)
				{
					botedge[hi] = P_GetFFloorTopZAt(fofopen[hi].floorrover, cross.x, cross.y);

					if (fofopen[lo].floor > open->floor)
					{
						botedge[lo] = P_GetFFloorTopZAt(fofopen[lo].floorrover, cross.x, cross.y);
					}

					open->floor				= fofopen[hi].floor;
					open->floorrover		= fofopen[hi].floorrover;
					open->floorslope		= *fofopen[hi].floorrover->t_slope;
					open->floorpic			= *fofopen[hi].floorrover->toppic;
					open->floorstep			= ( botedge[hi] - mobj->z );
					open->floordrop			= ( botedge[hi] - botedge[lo] );

					if (fofopen[lo].floor > open->lowfloor)
					{
						open->lowfloor = fofopen[lo].floor;
					}
				}
				else if (fofopen[hi].floor > open->lowfloor)
				{
					open->lowfloor = fofopen[hi].floor;
				}
			}
		}
	}

	open->range = (open->ceiling - open->floor);
}

static blocknode_t *freeblocks;

static blocknode_t *P_CreateBlockNode(mobj_t *thing, int x, int y)
{
	blocknode_t *block;

	if (freeblocks != NULL)
	{
		block = freeblocks;
		freeblocks = block->bnext;
	}
	else
		block = Z_Malloc(sizeof(blocknode_t), PU_LEVEL, NULL);

	block->blockindex = x + y*bmapwidth;
	block->mobj.mobj = thing;
	block->mnext = NULL;
	block->mprev = NULL;
	block->bprev = NULL;
	block->bnext = NULL;

	return block;
}

static void P_ReleaseBlockNode(blocknode_t *node)
{
	node->bnext = freeblocks;
	freeblocks = node;
}

//
// THING POSITION SETTING
//

//
// P_UnsetBlockmapEntry
// Unlinks a thing from block map and sectors.
//
void P_UnsetBlockmapEntry(mobj_t *thing)
{
	// [RH] Unlink from all blocks this actor uses
	blocknode_t *block = thing->blocknode;

	while (block != NULL)
	{
		if (block->mnext != NULL)
			block->mnext->mprev = block->mprev;
		*(block->mprev) = block->mnext;
		blocknode_t *next = block->bnext;
		P_ReleaseBlockNode(block);
		block = next;
	}

	thing->blocknode = NULL;
}

//
// P_UnsetThingPosition
// Unlinks a thing from block map and sectors.
// On each position change, BLOCKMAP and other
// lookups maintaining lists ot things inside
// these structures need to be updated.
//
void P_UnsetThingPosition(mobj_t *thing)
{
	I_Assert(thing != NULL);
	I_Assert(!P_MobjWasRemoved(thing));

	if (P_IsPredicting())
		return;

	if (!(thing->flags & MF_NOSECTOR))
	{
		/* invisible things don't need to be in sector list
		* unlink from subsector
		*
		* killough 8/11/98: simpler scheme using pointers-to-pointers for prev
		* pointers, allows head node pointers to be treated like everything else
		*/

		mobj_t **sprev = thing->sprev;
		mobj_t  *snext = thing->snext;
		if ((*sprev = snext) != NULL)  // unlink from sector list
			snext->sprev = sprev;

		// phares 3/14/98
		//
		// Save the sector list pointed to by touching_sectorlist.
		// In P_SetThingPosition, we'll keep any nodes that represent
		// sectors the Thing still touches. We'll add new ones then, and
		// delete any nodes for sectors the Thing has vacated. Then we'll
		// put it back into touching_sectorlist. It's done this way to
		// avoid a lot of deleting/creating for nodes, when most of the
		// time you just get back what you deleted anyway.
		//
		// If this Thing is being removed entirely, then the calling
		// routine will clear out the nodes in sector_list.

		sector_list = thing->touching_sectorlist;
		thing->touching_sectorlist = NULL; //to be restored by P_SetThingPosition
	}

	if (!(thing->flags & MF_NOBLOCKMAP))
		P_UnsetBlockmapEntry(thing);
}

void P_UnsetPrecipThingPosition(precipmobj_t *thing)
{
	P_UnsetBlockmapEntry((mobj_t *)thing);

	precipsector_list = thing->touching_sectorlist;
	thing->touching_sectorlist = NULL; //to be restored by P_SetPrecipThingPosition
}

void P_SetBlockmapEntry(mobj_t *thing, blocknode_t **bmap)
{
	// link into blockmap
	if (thing->precip || !(thing->flags & MF_NOBLOCKMAP))
	{
		int32_t x1 = (unsigned)(thing->x - thing->radius - bmaporgx)>>MAPBLOCKSHIFT;
		int32_t y1 = (unsigned)(thing->y - thing->radius - bmaporgy)>>MAPBLOCKSHIFT;
		int32_t x2 = (unsigned)(thing->x + thing->radius - bmaporgx)>>MAPBLOCKSHIFT;
		int32_t y2 = (unsigned)(thing->y + thing->radius - bmaporgy)>>MAPBLOCKSHIFT;

		thing->blocknode = NULL;

		blocknode_t **alink = &thing->blocknode;

		if (!(x1 >= bmapwidth || x2 < 0 || y1 >= bmapheight || y2 < 0))
		{
			// [RH] Link into every block this actor touches, not just the center one
			x1 = max(0, x1);
			y1 = max(0, y1);
			x2 = min(bmapwidth - 1, x2);
			y2 = min(bmapheight - 1, y2);
			for (int y = y1; y <= y2; ++y)
			{
				for (int x = x1; x <= x2; ++x)
				{
					blocknode_t **link = &bmap[y*bmapwidth + x];
					blocknode_t *node = P_CreateBlockNode(thing, x, y);

					//node->bnext = bnext; is this necessary?? --Super

					// Link in to block
					if ((node->mnext = *link) != NULL)
					{
						(*link)->mprev = &node->mnext;
					}
					node->mprev = link;
					*link = node;

					// Link in to actor
					node->bprev = alink;
					node->bnext = NULL;
					(*alink) = node;
					alink = &node->bnext;
				}
			}
		}
	}
}

//
// P_SetThingPosition
// Links a thing into both a block and a subsector
// based on it's x y.
// Sets thing->subsector properly
//
void P_SetThingPosition(mobj_t *thing)
{                                                      // link into subsector
	subsector_t *ss;
	sector_t *oldsec = NULL;
	fixed_t tfloorz, tceilz;

	I_Assert(thing != NULL);
	I_Assert(!P_MobjWasRemoved(thing));

	if (thing->player && thing->z <= thing->floorz && thing->subsector)
	{
		// I don't trust this so I'm leaving it alone. -Sal
		oldsec = thing->subsector->sector;
	}

	ss = thing->subsector = R_PointInSubsector(thing->x, thing->y);

	if (!P_IsPredicting())
	{
		if (!(thing->flags & MF_NOSECTOR))
		{
			// invisible things don't go into the sector links

			// killough 8/11/98: simpler scheme using pointer-to-pointer prev
			// pointers, allows head nodes to be treated like everything else

			mobj_t **link = &ss->sector->thinglist;
			mobj_t *snext = *link;
			if ((thing->snext = snext) != NULL)
				snext->sprev = &thing->snext;
			thing->sprev = link;
			*link = thing;

			// phares 3/16/98
			//
			// If sector_list isn't NULL, it has a collection of sector
			// nodes that were just removed from this Thing.

			// Collect the sectors the object will live in by looking at
			// the existing sector_list and adding new nodes and deleting
			// obsolete ones.

			// When a node is deleted, its sector links (the links starting
			// at sector_t->touching_thinglist) are broken. When a node is
			// added, new sector links are created.

			P_CreateSecNodeList(thing,thing->x,thing->y);
			thing->touching_sectorlist = sector_list; // Attach to Thing's mobj_t
			sector_list = NULL; // clear for next time
		}

		// inert things don't need to be in blockmap
		P_SetBlockmapEntry(thing, blocklinks);
	}

	// Allows you to 'step' on a new linedef exec when the previous
	// sector's floor is the same height.
	if (thing->player && oldsec != NULL && thing->subsector && oldsec != thing->subsector->sector)
	{
		if (thing->eflags & MFE_VERTICALFLIP)
		{
			tceilz = P_GetCeilingZ(thing, ss->sector, thing->x, thing->y, NULL);

			if (thing->z + thing->height >= tceilz)
				thing->eflags |= MFE_JUSTSTEPPEDDOWN;
		}
		else
		{
			tfloorz = P_GetFloorZ(thing, ss->sector, thing->x, thing->y, NULL);

			if (thing->z <= tfloorz)
				thing->eflags |= MFE_JUSTSTEPPEDDOWN;
		}
	}
}

//
// P_SetUnderlayPosition
// Links a thing into a subsector at the other end of the stack,
// so it appears behind all other sprites in that subsector.
// Sets thing->subsector properly
//
void P_SetUnderlayPosition(mobj_t *thing)
{                                                      // link into subsector
	subsector_t *ss;
	mobj_t **link, *lend;
	I_Assert(thing);

	ss = thing->subsector = R_PointInSubsector(thing->x, thing->y);
	link = &ss->sector->thinglist;
	for (lend = *link; lend && lend->snext; lend = lend->snext)
		;
	thing->snext = NULL;
	if (!lend)
	{
		thing->sprev = link;
		*link = thing;
	}
	else
	{
		thing->sprev = &lend->snext;
		lend->snext = thing;
	}

	P_CreateSecNodeList(thing,thing->x,thing->y);
	thing->touching_sectorlist = sector_list; // Attach to Thing's mobj_t
	sector_list = NULL; // clear for next time
}

void P_SetPrecipitationThingPosition(precipmobj_t *thing)
{
	thing->subsector = R_PointInSubsector(thing->x, thing->y);

	P_CreatePrecipSecNodeList(thing, thing->x, thing->y);
	thing->touching_sectorlist = precipsector_list; // Attach to Thing's precipmobj_t
	precipsector_list = NULL; // clear for next time

	// NOTE: this works because bnext/bprev are at the same
	// offsets in precipmobj_t and mobj_t
	P_SetBlockmapEntry((mobj_t*)thing, precipblocklinks);
}

//
// BLOCK MAP ITERATORS
// For each line/thing in the given mapblock,
// call the passed PIT_* function.
// If the function returns false,
// exit with false without checking anything else.
//


//
// P_BlockLinesIterator
// The validcount flags are used to avoid checking lines
// that are marked in multiple mapblocks,
// so increment validcount before the first call
// to P_BlockLinesIterator, then make one or more calls
// to it.
//
dboolean P_BlockLinesIterator(int32_t x, int32_t y, BlockItReturn_t (*func)(line_t *))
{
	int32_t offset;
	const int32_t *list; // Big blockmap
	polymaplink_t *plink; // haleyjd 02/22/06
	line_t *ld;

	if (x < 0 || y < 0 || x >= bmapwidth || y >= bmapheight)
		return true;

	offset = y*bmapwidth + x;

	// haleyjd 02/22/06: consider polyobject lines
	plink = polyblocklinks[offset];

	while (plink)
	{
		polyobj_t *po = plink->po;

		if (po->validcount != validcount) // if polyobj hasn't been checked
		{
			size_t i;
			po->validcount = validcount;

			for (i = 0; i < po->numLines; ++i)
			{
				BlockItReturn_t ret = BMIT_CONTINUE;

				if (po->lines[i]->validcount == validcount) // line has been checked
					continue;

				po->lines[i]->validcount = validcount;
				ret = func(po->lines[i]);

				if (ret == BMIT_ABORT)
				{
					return false;
				}
				else if (ret == BMIT_STOP)
				{
					return true;
				}
			}
		}
		plink = (polymaplink_t *)(plink->link.next);
	}

	offset = *(blockmap + offset); // offset = blockmap[y*bmapwidth+x];

	// First index is really empty, so +1 it.
	for (list = blockmaplump + offset + 1; *list != -1; list++)
	{
		BlockItReturn_t ret = BMIT_CONTINUE;

		ld = &lines[*list];

		if (ld->validcount == validcount)
			continue; // Line has already been checked.

		ld->validcount = validcount;
		ret = func(ld);

		if (ret == BMIT_ABORT)
		{
			return false;
		}
		else if (ret == BMIT_STOP)
		{
			return true;
		}
	}
	return true; // Everything was checked.
}


//
// P_BlockThingsIterator
//
dboolean P_BlockThingsIterator(int32_t x, int32_t y, BlockItReturn_t (*func)(mobj_t *))
{
	blocknode_t *block, *next = NULL;

	if (x < 0 || y < 0 || x >= bmapwidth || y >= bmapheight)
		return true;

	// Check interaction with the objects in the blockmap.
	for (block = blocklinks[y*bmapwidth + x]; block != NULL; block = next)
	{
		BlockItReturn_t ret = BMIT_CONTINUE;

		next = block->mnext; // We want to note our reference to mnext here!
		ret = func(block->mobj.mobj);

		if (ret == BMIT_ABORT)
		{
			return false; // failure
		}

		if (ret == BMIT_STOP)
		{
			return true; // success
		}
	}

	return true;
}

dboolean P_DoBlockThingsIterate(int x1, int y1, int x2, int y2, BlockItReturn_t (*func)(mobj_t *))
{
	dboolean status = true;

	for (int32_t bx = x1; bx <= x2; bx++)
		for (int32_t by = y1; by <= y2; by++)
			if (!P_BlockThingsIterator(bx, by, func))
				status = false;

	return status;
}

static bthingit_hash_entry_t *GetHashEntryForIterator(bthingit_t *it, int i)
{
	if (i < NUM_BTHINGIT_FIXEDHASH)
		return &it->fixedhash[i];
	else
		return &it->dynhash[i - NUM_BTHINGIT_FIXEDHASH];
}

static blocknode_t *GetBlockmapBlock(int x, int y)
{
	if (x >= 0 && y >= 0 && x < bmapwidth && y < bmapheight)
	{
		return blocklinks[y*bmapwidth + x];
	}
	else
	{
		// invalid block
		return NULL;
	}
}

static bthingit_t *freeiters;

bthingit_t *P_NewBlockThingsIterator(int x1, int y1, int x2, int y2)
{
	bthingit_t *it;
	blocknode_t *block;

	x1 = max(0, x1);
	y1 = max(0, y1);
	x2 = min(bmapwidth - 1, x2);
	y2 = min(bmapheight - 1, y2);

	if (x1 > x2 || y1 > y2)
		return NULL;

	block = GetBlockmapBlock(x1, y1);

	if (freeiters != NULL)
	{
		it = freeiters;
		freeiters = it->freechain;
	}
	else
		it = Z_Calloc(sizeof(bthingit_t), PU_LEVEL, NULL);

	it->x1 = x1;
	it->y1 = y1;
	it->x2 = x2;
	it->y2 = y2;
	it->curx = x1;
	it->cury = y1;
	it->block = block;
	it->freechain = NULL;
	it->numfixedhash = 0;
	it->dynhashcount = 0;

	for (size_t i = 0; i < NUM_BTHINGIT_BUCKETS; i++)
		it->buckets[i] = -1;

	return it;
}

mobj_t *P_BlockThingsIteratorNext(bthingit_t *it, dboolean centeronly)
{
	for (;;)
	{
		while (it->block != NULL)
		{
			mobj_t *mobj = it->block->mobj.mobj;
			blocknode_t *node = it->block;

			it->block = it->block->mnext;

			// Don't recheck things that were already checked
			if (node->bnext == NULL && node->bprev == &mobj->blocknode)
			{
				// This actor doesn't span blocks, so we know it can only ever be checked once.
				return mobj;
			}
			else
			{
				// Block boundaries for compatibility mode
				if (centeronly)
				{
					fixed_t blockleft = (it->curx * MAPBLOCKUNITS) + bmaporgx;
					fixed_t blockright = blockleft + MAPBLOCKUNITS;
					fixed_t blockbottom = (it->cury * MAPBLOCKUNITS) + bmaporgy;
					fixed_t blocktop = blockbottom + MAPBLOCKUNITS;

					// only return actors with the center in this block
					if (mobj->x >= blockleft && mobj->x < blockright &&
						mobj->y >= blockbottom && mobj->y < blocktop)
					{
						return mobj;
					}
				}
				else
				{
					bthingit_hash_entry_t *entry;
					int i;
					size_t hash = ((size_t)mobj >> 3) % NUM_BTHINGIT_BUCKETS;

					for (i = it->buckets[hash]; i >= 0; )
					{
						entry = GetHashEntryForIterator(it, i);
						if (entry->mobj == mobj)
						{
							// I've already been checked. Skip to the next mobj.
							break;
						}
						i = entry->next;
					}

					if (i < 0)
					{
						// Add mobj to the hash table and return it.
						if (it->numfixedhash < NUM_BTHINGIT_FIXEDHASH)
						{
							entry = &it->fixedhash[it->numfixedhash];
							entry->next = it->buckets[hash];
							it->buckets[hash] = it->numfixedhash++;
						}
						else
						{
							if (!it->dynhash)
							{
								it->dynhashcapacity = 50;
								Z_Calloc(it->dynhashcapacity * sizeof(*it->dynhash), PU_LEVEL, &it->dynhash);
							}
							if (it->dynhashcount == it->dynhashcapacity)
							{
								it->dynhashcapacity *= 2;
								it->dynhash = Z_Realloc(it->dynhash, it->dynhashcapacity * sizeof(*it->dynhash), PU_LEVEL, &it->dynhash);
							}
							i = (int)it->dynhashcount;
							it->dynhashcount++;
							entry = &it->dynhash[i];
							entry->next = it->buckets[hash];
							it->buckets[hash] = i + NUM_BTHINGIT_FIXEDHASH;
						}

						entry->mobj = mobj;
						return mobj;
					}
				}
			}
		}

		if (++it->curx > it->x2)
		{
			it->curx = it->x1;
			if (++it->cury > it->y2)
				return NULL;
		}

		it->block = GetBlockmapBlock(it->curx, it->cury);
	}

	return NULL;
}

void P_FreeBlockThingsIterator(bthingit_t *it)
{
	if (it)
	{
		it->freechain = freeiters;
		freeiters = it;
	}
}

void P_ClearBlockNodes(void)
{
	freeblocks = NULL;
	freeiters = NULL;
}

//
// INTERCEPT ROUTINES
//

//SoM: 4/6/2000: Limit removal
static intercept_t *intercepts = NULL;
static intercept_t *intercept_p = NULL;

divline_t g_trace = {};

//SoM: 4/6/2000: Remove limit on intercepts.
static void P_CheckIntercepts(void)
{
	static size_t max_intercepts = 0;
	size_t count = intercept_p - intercepts;

	if (max_intercepts <= count)
	{
		if (!max_intercepts)
			max_intercepts = 128;
		else
			max_intercepts *= 2;

		intercepts = Z_Realloc(intercepts, sizeof (*intercepts) * max_intercepts, PU_STATIC, NULL);

		intercept_p = intercepts + count;
	}
}

//
// PIT_AddLineIntercepts.
// Looks for lines in the given block
// that intercept the given trace
// to add to the intercepts list.
//
// A line is crossed if its endpoints
// are on opposite sides of the trace.
// Returns true if earlyout and a solid line hit.
//
static BlockItReturn_t PIT_AddLineIntercepts(line_t *ld)
{
	int32_t s1, s2;
	fixed_t frac;
	divline_t dl;

	// avoid precision problems with two routines
	if (g_trace.dx > FRACUNIT*16 || g_trace.dy > FRACUNIT*16
		|| g_trace.dx < -FRACUNIT*16 || g_trace.dy < -FRACUNIT*16)
	{
		// Hurdler: crash here with phobia when you shoot
		// on the door next the stone bridge
		// stack overflow???
		s1 = P_PointOnDivlineSide(ld->v1->x, ld->v1->y, &g_trace);
		s2 = P_PointOnDivlineSide(ld->v2->x, ld->v2->y, &g_trace);
	}
	else
	{
		s1 = P_PointOnLineSide(g_trace.x, g_trace.y, ld);
		s2 = P_PointOnLineSide(g_trace.x+g_trace.dx, g_trace.y+g_trace.dy, ld);
	}

	if (s1 == s2)
	{
		// Line isn't crossed.
		return BMIT_CONTINUE;
	}

	// Hit the line.
	P_MakeDivline(ld, &dl);
	frac = P_InterceptVector(&g_trace, &dl);

	if (frac < 0)
	{
		// behind source
		return BMIT_CONTINUE;
	}

	P_CheckIntercepts();

	intercept_p->frac = frac;
	intercept_p->isaline = true;
	intercept_p->d.line = ld;
	intercept_p++;

	return BMIT_CONTINUE; // continue
}

//
// PIT_AddThingIntercepts
//
static BlockItReturn_t PIT_AddThingIntercepts(mobj_t *thing)
{
	size_t numfronts = 0;
	divline_t line;
	int32_t i;

	// [RH] Don't check a corner to corner crossection for hit.
	// Instead, check against the actual bounding box

	// There's probably a smarter way to determine which two sides
	// of the thing face the trace than by trying all four sides...
	for (i = 0; i < 4; ++i)
	{
		switch (i)
		{
			case 0: // Top edge
			{
				line.y = thing->y + thing->radius;
				if (g_trace.y < line.y)
				{
					continue;
				}
				line.x = thing->x + thing->radius;
				line.dx = thing->radius * -2;
				line.dy = 0;
				break;
			}

			case 1: // Right edge
			{
				line.x = thing->x + thing->radius;
				if (g_trace.x < line.x)
				{
					continue;
				}
				line.y = thing->y - thing->radius;
				line.dx = 0;
				line.dy = thing->radius * 2;
				break;
			}

			case 2: // Bottom edge
			{
				line.y = thing->y - thing->radius;
				if (g_trace.y > line.y)
				{
					continue;
				}
				line.x = thing->x - thing->radius;
				line.dx = thing->radius * 2;
				line.dy = 0;
				break;
			}

			case 3: // Left edge
			{
				line.x = thing->x - thing->radius;
				if (g_trace.x > line.x)
				{
					continue;
				}
				line.y = thing->y + thing->radius;
				line.dx = 0;
				line.dy = thing->radius * -2;
				break;
			}
		}

		// Check if this side is facing the trace origin
		numfronts++;

		// If it is, see if the trace crosses it
		if (P_PointOnDivlineSide(line.x, line.y, &g_trace) !=
			P_PointOnDivlineSide(line.x + line.dx, line.y + line.dy, &g_trace))
		{
			// It's a hit
			fixed_t frac = P_InterceptVector(&g_trace, &line);

			if (frac < 0)
			{
				// behind source
				continue;
			}

			P_CheckIntercepts();

			intercept_p->frac = frac;
			intercept_p->isaline = false;
			intercept_p->d.thing = thing;
			intercept_p++;

			return BMIT_CONTINUE; // Keep going.
		}
	}

	// If none of the sides was facing the trace, then the trace
	// must have started inside the box, so add it as an intercept.
	if (numfronts == 0)
	{
		P_CheckIntercepts();

		intercept_p->frac = 0;
		intercept_p->isaline = false;
		intercept_p->d.thing = thing;
		intercept_p++;
	}

	return BMIT_CONTINUE; // Keep going.
}

//
// P_TraverseIntercepts
// Returns true if the traverser function returns true
// for all lines.
//
// killough 5/3/98: reformatted, cleaned up
//
static dboolean P_TraverseIntercepts(traverser_t func, fixed_t maxfrac)
{
	size_t count;
	intercept_t *in = NULL;

	count = intercept_p - intercepts;

	while (count--)
	{
		fixed_t dist = INT32_MAX;
		intercept_t *scan = NULL;

		for (scan = intercepts; scan < intercept_p; scan++)
		{
			if (scan->frac < dist)
			{
				dist = scan->frac;
				in = scan;
			}
		}

		if (dist > maxfrac)
		{
			return true; // Checked everything in range.
		}

		if (in)
		{
			if (!func(in))
			{
				return false; // Don't bother going farther.
			}

			in->frac = INT32_MAX;
		}
	}

	return true; // Everything was traversed.
}

//
// P_PathTraverse
// Traces a line from x1, y1 to x2, y2,
// calling the traverser function for each.
// Returns true if the traverser function returns true
// for all lines.
//
dboolean P_PathTraverse(fixed_t px1, fixed_t py1, fixed_t px2, fixed_t py2,
	int32_t flags, traverser_t trav)
{
	validcount++;
	intercept_p = intercepts;

	if (((px1 - bmaporgx) & (MAPBLOCKSIZE-1)) == 0)
		px1 += FRACUNIT; // Don't side exactly on a line.

	if (((py1 - bmaporgy) & (MAPBLOCKSIZE-1)) == 0)
		py1 += FRACUNIT; // Don't side exactly on a line.

	g_trace.x = px1;
	g_trace.y = py1;
	g_trace.dx = px2 - px1;
	g_trace.dy = py2 - py1;

	px1 -= bmaporgx;
	py1 -= bmaporgy;
	px2 -= bmaporgx;
	py2 -= bmaporgy;

	// blockmap traversal algorithm ported from antimony
	int gridpos_x = (unsigned)px1 >> MAPBLOCKSHIFT;
	int gridpos_y = (unsigned)py1 >> MAPBLOCKSHIFT;
	if (flags & PT_ADDLINES)
		if (!P_BlockLinesIterator(gridpos_x, gridpos_y, PIT_AddLineIntercepts))
			return false; // early out
	if (flags & PT_ADDTHINGS)
		if (!P_BlockThingsIterator(gridpos_x, gridpos_y, PIT_AddThingIntercepts))
			return false; // early out

	int endpos_x = (unsigned)px2 >> MAPBLOCKSHIFT;
	int endpos_y = (unsigned)py2 >> MAPBLOCKSHIFT;
	if (gridpos_x == endpos_x && gridpos_y == endpos_y)
		return P_TraverseIntercepts(trav, FRACUNIT);

	fixed_t pos_x = px1;
	fixed_t pos_y = py1;
	fixed_t dist_x = px2 - px1;
	fixed_t dist_y = py2 - py1;
	int dir_x = dist_x < 0 ? -1 : 1;
	int dir_y = dist_y < 0 ? -1 : 1;
	fixed_t delta_x;
	fixed_t delta_y;
	// special cases for 0 to avoid crashes
	if (dist_y == 0)
	{
		delta_x = INT32_MAX;
		delta_y = 0;
	}
	else if (dist_x == 0)
	{
		delta_x = 0;
		delta_y = INT32_MAX;
	}
	else
	{
		delta_x = FixedDiv2(dist_x, abs(dist_y));
		delta_y = FixedDiv2(dist_y, abs(dist_x));
	}

	while (gridpos_x >= 0 && gridpos_y >= 0 && gridpos_x < bmapwidth && gridpos_y < bmapheight)
	{
		fixed_t hitx_x;
		if (dir_x > 0)
			hitx_x = (gridpos_x+1) << MAPBLOCKSHIFT;
		else
			hitx_x = gridpos_x << MAPBLOCKSHIFT;

		fixed_t hity_y;
		if (dir_y > 0)
			hity_y = (gridpos_y+1) << MAPBLOCKSHIFT;
		else
			hity_y = gridpos_y << MAPBLOCKSHIFT;

		hitx_x -= pos_x;
		hity_y -= pos_y;
		fixed_t hitx_y = FixedMul(delta_y, abs(hitx_x));
		fixed_t hity_x = FixedMul(delta_x, abs(hity_y));
		if (FixedMul(hitx_x, hitx_x) + FixedMul(hitx_y, hitx_y) > FixedMul(hity_x, hity_x) + FixedMul(hity_y, hity_y))
		{
			gridpos_y += dir_y;
			pos_x += hity_x;
			pos_y += hity_y;
		}
		else if (FixedMul(hitx_x, hitx_x) + FixedMul(hitx_y, hitx_y) < FixedMul(hity_x, hity_x) + FixedMul(hity_y, hity_y))
		{
			gridpos_x += dir_x;
			pos_x += hitx_x;
			pos_y += hitx_y;
		}
		else
		{
			// we hit a corner, round down towards south-east
			if (dir_x < 0 && dir_y > 0)
			{
				gridpos_y += dir_y;
			}
			else if (dir_x > 0 && dir_y < 0)
			{
				gridpos_x += dir_x;
			}
			else
			{
				gridpos_x += dir_x;
				gridpos_y += dir_y;
			}

			pos_x += hitx_x;
			pos_y += hitx_y;
		}

		if (flags & PT_ADDLINES)
			if (!P_BlockLinesIterator(gridpos_x, gridpos_y, PIT_AddLineIntercepts))
				return false; // early out
		if (flags & PT_ADDTHINGS)
			if (!P_BlockThingsIterator(gridpos_x, gridpos_y, PIT_AddThingIntercepts))
				return false; // early out
		if (gridpos_x == endpos_x && gridpos_y == endpos_y)
			break;
	}

	// Go through the sorted list
	return P_TraverseIntercepts(trav, FRACUNIT);
}

// =========================================================================
//                                                        BLOCKMAP ITERATORS
// =========================================================================

// blockmap iterator for all sorts of use
// your routine must return FALSE to exit the loop earlier
// returns FALSE if the loop exited early after a false return
// value from your user function

//abandoned, maybe I'll need it someday..
/*
dboolean P_RadiusLinesCheck(fixed_t radius, fixed_t x, fixed_t y,
	dboolean (*func)(line_t *))
{
	int32_t xl, xh, yl, yh;
	int32_t bx, by;

	g_tm.bbox[BOXTOP] = y + radius;
	g_tm.bbox[BOXBOTTOM] = y - radius;
	g_tm.bbox[BOXRIGHT] = x + radius;
	g_tm.bbox[BOXLEFT] = x - radius;

	// check lines
	xl = (unsigned)(g_tm.bbox[BOXLEFT] - bmaporgx)>>MAPBLOCKSHIFT;
	xh = (unsigned)(g_tm.bbox[BOXRIGHT] - bmaporgx)>>MAPBLOCKSHIFT;
	yl = (unsigned)(g_tm.bbox[BOXBOTTOM] - bmaporgy)>>MAPBLOCKSHIFT;
	yh = (unsigned)(g_tm.bbox[BOXTOP] - bmaporgy)>>MAPBLOCKSHIFT;

	for (bx = xl; bx <= xh; bx++)
		for (by = yl; by <= yh; by++)
			if (!P_BlockLinesIterator(bx, by, func))
				return false;
	return true;
}
*/
