// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2023 by James Robert Roman.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_debug.cpp
/// \brief Software renderer debugging

#include <algorithm> // std::clamp

#include "cxxutil.hpp"
#include "r_debug_detail.hpp"

#include "command.h"
#include "i_time.h"
#include "m_fixed.h"
#include "r_draw.h"
#include "r_main.h"

using namespace srb2::r_debug;

namespace
{

CV_PossibleValue_t contrast_cons_t[] = {{-FRACUNIT, "MIN"}, {FRACUNIT, "MAX"}, {}};

}; // namespace

consvar_t cv_debugrender_contrast =
	CVAR_INIT("debugrender_contrast", "0.0", CV_CHEAT | CV_FLOAT, contrast_cons_t, nullptr);

consvar_t cv_debugrender_spriteclip = CVAR_INIT("debugrender_spriteclip", "Off", CV_CHEAT, CV_OnOff, nullptr);
consvar_t cv_debugrender_portal = CVAR_INIT("debugrender_portal", "Off", CV_CHEAT, CV_OnOff, nullptr);
consvar_t cv_debugrender_visplanes = CVAR_INIT("debugrender_visplanes", "Off", CV_CHEAT, CV_OnOff, nullptr);
consvar_t cv_debugfinishline = CVAR_INIT ("debugfinishline", "Off", CV_CHEAT, CV_OnOff, nullptr);
consvar_t cv_debugrender_freezebsp = CVAR_INIT ("debugrender_freezebsp", "Off", CV_CHEAT, CV_OnOff, nullptr);

uint32_t debugrender_highlight;

void R_CheckDebugHighlight(debugrender_highlight_t k)
{
	// If highlighting is enabled for anything, surfaces
	// must be highlighted in one of two colors, depending on
	// whether they fall under focus of the debug.

	if (debugrender_highlight)
	{
		r8_flatcolor = (debugrender_highlight & (1 << k)) ? detail::kHighlightOptions[k].color : 0x1F;
	}
}

int32_t R_AdjustLightLevel(int32_t light)
{
	constexpr fixed_t kRange = (LIGHTLEVELS - 1) * FRACUNIT;
	const fixed_t adjust = FixedMul(cv_debugrender_contrast.value, kRange);

	if (debugrender_highlight)
	{
		light = (kRange / 2) - (adjust / 2);

		SRB2_ASSERT(light >= 0);
		SRB2_ASSERT(light <= kRange);
	}
	else
	{
		light = std::clamp((light * FRACUNIT) - adjust, 0, kRange);
	}

	return light / FRACUNIT;
}

uint8_t R_DebugLineColor(const line_t *ld)
{
	const bool alt = (I_GetTime() % 70 < 35);

	switch (ld->special)
	{
	case 2001: // Ring Racers: Finish Line
		return alt ? 0x1F : 0x49; // black, yellow

	case 2003: // Ring Racers: Respawn Line
		return alt ? 0x23 : 0x00; // red, white
	}

	return 0x00;
}

void Command_Debugrender_highlight(void)
{
	const bool changes = COM_Argc() > 1;

	if (!CV_CheatsEnabled())
	{
		CONS_Printf("Cheats must be enabled.\n");
		return;
	}

	if (changes)
	{
		const char* arg = COM_Argv(1);

		debugrender_highlight = 0; // always reset

		if (COM_Argc() > 2 ||
			// approximate match "none"
			strncasecmp(arg, "none", strlen(arg)))
		{
			char* p = COM_Args();

			while (p)
			{
				p = detail::parse_highlight_arg(p);
			}
		}
	}

	detail::highlight_help(changes);
}
