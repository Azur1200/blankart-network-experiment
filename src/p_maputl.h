// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  p_maputl.h
/// \brief map utility functions

#ifndef __P_MAPUTL__
#define __P_MAPUTL__

#include "doomtype.h"
#include "r_defs.h"
#include "m_fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// P_MAPUTL
//
struct divline_t
{
	fixed_t x, y, dx, dy;
};

struct intercept_t
{
	fixed_t frac; // along trace line
	dboolean isaline;
	union
	{
		mobj_t *thing;
		line_t *line;
	} d;
};

typedef dboolean (*traverser_t)(intercept_t *in);

dboolean P_PathTraverse(fixed_t px1, fixed_t py1, fixed_t px2, fixed_t py2,
	int32_t pflags, traverser_t ptrav);

#define P_AproxDistance(dx, dy) FixedHypot(dx, dy)
void P_ClosestPointOnLine(fixed_t x, fixed_t y, const line_t *line, vertex_t *result);
void P_ClosestPointOnLine3D(const vector3_t *p, const vector3_t *line, vector3_t *result);
int32_t P_PointOnLineSide(fixed_t x, fixed_t y, const line_t *line);
void P_MakeDivline(const line_t *li, divline_t *dl);

struct opening_t
{
	fixed_t ceiling, floor, range;
	fixed_t lowfloor, highceiling;
	pslope_t *floorslope, *ceilingslope;
	ffloor_t *floorrover, *ceilingrover;
	fixed_t ceilingstep, ceilingdrop;
	fixed_t floorstep, floordrop;
	int32_t ceilingpic, floorpic;
	uint8_t fofType; // LO_FOF_ types for forcing FOF collide
};

#define LO_FOF_ANY		(0)
#define LO_FOF_FLOORS	(1)
#define LO_FOF_CEILINGS	(2)

void P_LineOpening(line_t *plinedef, mobj_t *mobj, opening_t *open);
void P_CameraLineOpening(line_t *plinedef, opening_t *open);
fixed_t P_InterceptVector(const divline_t *v2, const divline_t *v1);
int32_t P_BoxOnLineSide(const fixed_t *tmbox, const line_t *ld);
line_t * P_FindNearestLine(const fixed_t x, const fixed_t y, const sector_t *, const int32_t special);
void P_UnsetPrecipThingPosition(precipmobj_t *thing);
void P_SetPrecipitationThingPosition(precipmobj_t *thing);
void P_CreatePrecipSecNodeList(precipmobj_t *thing, fixed_t x,fixed_t y);
void P_HitSpecialLines(mobj_t *thing, fixed_t x, fixed_t y, fixed_t momx, fixed_t momy);

dboolean P_GetMidtextureTopBottom(line_t *linedef, opening_t *open, fixed_t *return_top, fixed_t *return_bottom);
dboolean P_GetTripwireTopBottom(line_t *linedef, fixed_t x, fixed_t y, fixed_t *return_top, fixed_t *return_bottom);
dboolean P_MidtextureIsSolid(line_t *linedef, mobj_t *mobj);

typedef enum
{
	BMIT_CONTINUE, // Continue blockmap search
	BMIT_STOP, // End blockmap search with success
	BMIT_ABORT // End blockmap search with failure
} BlockItReturn_t;

dboolean P_BlockLinesIterator(int32_t x, int32_t y, BlockItReturn_t(*func)(line_t *));
dboolean P_BlockThingsIterator(int32_t x, int32_t y, BlockItReturn_t(*func)(mobj_t *));

#define PT_ADDLINES		(1)
#define PT_ADDTHINGS	(2)

extern divline_t g_trace;

// call your user function for each line of the blockmap in the
// bbox defined by the radius
//dboolean P_RadiusLinesCheck(fixed_t radius, fixed_t x, fixed_t y,
//	dboolean (*func)(line_t *));

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __P_MAPUTL__
