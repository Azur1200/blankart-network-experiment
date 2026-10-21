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

#include "doomdef.h"
#include "m_animkeyframe.h"

// note: the one time a linked list might make sense(?)
// would suck for authoring though

// assumes the list of keyframes is sorted already (please make sure yall sort your keys)
int32_t M_GetPrevKeyFrameIndexForTime(animkeyframe_t keyframelist[], int32_t listcount, fixed_t fractime)
{
	// why
	if (listcount == 0) return -1;

	for (int32_t i = listcount - 1; i >= 0; i--)
	{
		if (fractime >= (keyframelist[i].time<<FRACBITS)) return i;
	}

	// we are before the first keyframe
	return 0;
}

int32_t M_GetNextKeyFrameIndexForTime(int32_t listcount, int32_t prev)
{
    // why
    if (prev == -1) return -1;

    // we're gonna go beyond the list, just give the same keyframe
    if (++prev >= listcount) return listcount-1;

    // we already incremented here
    return prev;
}

// eases between two keyframes
fixed_t M_GetKeyFrameValue(animkeyframe_t *prevkeyframe, animkeyframe_t *nextkeyframe, fixed_t fractime)
{
	fixed_t prevfixedtime = prevkeyframe->time<<FRACBITS;
	fixed_t nextfixedtime = nextkeyframe->time<<FRACBITS;

	fixed_t prevvalue = prevkeyframe->value;
	fixed_t nextvalue = nextkeyframe->value;

	fixed_t keyprog = 0;
	fixed_t weightedprev, weightednext;

	if (fractime < prevfixedtime) return prevvalue;
	if (fractime >= nextfixedtime) return nextvalue;
	if ((nextfixedtime - prevfixedtime) == 0) return nextvalue;

	keyprog = CLAMP(FixedDiv(fractime - prevfixedtime, (nextfixedtime - prevfixedtime)), 0, FRACUNIT);

	if (prevkeyframe->keyeasing == nextkeyframe->keyeasing)
	{
		// don't bother blending
		return easing_funclist[prevkeyframe->keyeasing](keyprog, prevvalue, nextvalue);
	}

	weightedprev = easing_funclist[prevkeyframe->keyeasing](keyprog, prevvalue, nextvalue);
	weightednext = easing_funclist[nextkeyframe->keyeasing](keyprog, prevvalue, nextvalue);

	return Easing_Linear(keyprog, weightedprev, weightednext);
}

// eases between all keyframes in an "animation"
fixed_t M_GetAnimatedKeyFrameValue(animkeyframe_t keyframelist[], int32_t listcount, fixed_t fractime)
{
    if (fractime >= (keyframelist[listcount - 1].time<<FRACBITS)) 
        return keyframelist[listcount - 1].value;

    int32_t prev = M_GetPrevKeyFrameIndexForTime(keyframelist, listcount, fractime);
    int32_t next = M_GetNextKeyFrameIndexForTime(listcount, prev);

    if (UNLIKELY(prev == -1 || next == -1))
    {
        return 0;
    }
    else if (UNLIKELY(prev == next)) // this should be covered by the first condition here
    {
        return keyframelist[next].value;
    }

    return M_GetKeyFrameValue(&keyframelist[prev], &keyframelist[next], fractime);
}