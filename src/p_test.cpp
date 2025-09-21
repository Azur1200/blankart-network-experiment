// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2023 by James Robert Roman
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#include <optional>
#include <vector>

#include "command.h"
#include "math/fixed.hpp"
#include "p_sweep.hpp"

#include "p_local.h"
#include "g_demo.h"
#include "r_main.h"

consvar_t cv_showgremlins = CVAR_INIT ("showgremlins", "Off", 0, CV_OnOff, NULL);

namespace
{

std::vector<line_t*> g_lines;

};

void P_TestLine(line_t* ld)
{
	g_lines.emplace_back(ld);
}

line_t* P_SweepTestLines(fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by, fixed_t r, vector2_t* return_normal)
{
	using namespace srb2::math;
	using namespace srb2::sweep;

	struct Collision
	{
		unit z;
		vec2 normal;
		line_t* ld;

		bool operator<(const Collision& b) const { return z < b.z; }
	};

	std::optional<Collision> collision;

	LineSegment<Fixed> l{{ax, ay}, {bx, by}};
	AABBvsLine sweep{r, l};

	for (line_t* ld : g_lines)
	{
		LineSegment<Fixed> ls{{ld->v1->x, ld->v1->y}, {ld->v2->x, ld->v2->y}};
		Result rs = sweep(ls);
		if (rs.hit)
		{
			if (!collision || rs.hit->z < collision->z)
			{
				collision = {rs.hit->z, rs.hit->n, ld};
			}
		}
	}

	if (!collision)
	{
		line_t *line = nullptr;

		if (!g_lines.empty() && !G_CompatLevel(0x000E))
		{
			// FIXME: This condition is a failsafe!
			// SlopeAABBvsLine::vs_slope can sometimes report
			// no collision despite P_CheckPosition saying otherwise.
			// (Generally related to infinitesimal numbers or overflows.)
			// When it reports no collision, that means no line and
			// no normals to base the collision off of.
			// Here we provide the last line checked and normals based on
			// the line and the player's momentum angle.
			// But the proper fix would be to make vs_slope work!!

			line = g_lines.back();

			angle_t lineangle = line->angle;
			angle_t mobjangle = R_PointToAngle2(ax, ay, bx, by);
			angle_t diff = lineangle - mobjangle;

			lineangle += diff > ANGLE_180 ? -ANGLE_90 : ANGLE_90;

			return_normal->x = FINECOSINE((lineangle >> ANGLETOFINESHIFT) & FINEMASK);
			return_normal->y = FINESINE((lineangle >> ANGLETOFINESHIFT) & FINEMASK);
		}

		g_lines.clear();
		return line;
	}

	g_lines.clear();
	return_normal->x = Fixed {collision->normal.x};
	return_normal->y = Fixed {collision->normal.y};

	return collision->ld;
}

void P_ClearTestLines(void)
{
	g_lines.clear();
}
