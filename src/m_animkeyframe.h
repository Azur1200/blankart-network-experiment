// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by "minenice".
// Copyright (C) 2026 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  m_animkeyframe.h
/// \brief Keyframe animation.

#ifndef __M_ANIMKEYFRAME__
#define __M_ANIMKEYFRAME__

#include "doomtype.h"
#include "m_easing.h"

#ifdef __cplusplus
extern "C" {
#endif

struct animkeyframe_t
{
	fixed_t value;
	int32_t time;
	easing_t keyeasing;
};

int32_t M_GetPrevKeyFrameIndexForTime(animkeyframe_t keyframelist[], int32_t listcount, fixed_t fractime);
int32_t M_GetNextKeyFrameIndexForTime(int32_t listcount, int32_t prev);
fixed_t M_GetKeyFrameValue(animkeyframe_t *prevkeyframe, animkeyframe_t *nextkeyframe, fixed_t fractime);
fixed_t M_GetAnimatedKeyFrameValue(animkeyframe_t keyframelist[], int32_t listcount, fixed_t fractime);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __M_ANIMKEYFRAME__