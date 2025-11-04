// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2022 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  i_time.c
/// \brief Timing for the system layer.

#include "i_time.h"

#include <math.h>

#include "command.h"
#include "doomtype.h"
#include "d_netcmd.h"
#include "m_fixed.h"
#include "i_system.h"

timestate_t g_time;

static CV_PossibleValue_t timescale_cons_t[] = {{FRACUNIT/20, "MIN"}, {20*FRACUNIT, "MAX"}, {0, NULL}};
consvar_t cv_timescale = CVAR_INIT ("timescale", "1.0", CV_NETVAR|CV_CHEAT|CV_FLOAT, timescale_cons_t, NULL);

static precise_t enterprecise, oldenterprecise;
static fixed_t entertic, oldentertics;
static double tictimer;
static double ticratescaled;

// experiment to prevent timing issues
// this returns the global time state accounted with how much time has passed
// since it was last updated
static void I_GetTimeAndFrac(tic_t *outtics, fixed_t *outfrac)
{
	double elapsedseconds;

	elapsedseconds = (double)(I_GetPreciseTime() - oldenterprecise) / I_GetPrecisePrecision();

	double fractional, integral;
	fractional = modf((tictimer + elapsedseconds) * ticratescaled, &integral);

	if (outtics)
		*outtics = g_time.time + (tic_t)integral;
	if (outfrac)
		*outfrac = DoubleToFixed(fractional);
}

// A little more than the minimum sleep duration on Windows.
// May be incorrect for other platforms, but we don't currently have a way to
// query the scheduler granularity. SDL will do what's needed to make this as
// low as possible though.
#if defined(_WIN32)
#define MIN_SLEEP_DURATION_MS 1.6
#else
#define MIN_SLEEP_DURATION_MS 2.1
#endif

tic_t I_GetTime(void)
{
	tic_t tic;
	I_GetTimeAndFrac(&tic, NULL);
	return tic;
}

fixed_t I_GetTimeFrac(void)
{
	fixed_t frac;
	I_GetTimeAndFrac(NULL, &frac);
	return frac;
}

void I_InitializeTime(void)
{
	g_time.time = 0;
	g_time.timefrac = 0;

	enterprecise = 0;
	oldenterprecise = 0;
	tictimer = 0.0;
	ticratescaled = 1.0;

	CV_RegisterVar(&cv_timescale);

	// I_StartupTimer is preserved for potential subsystems that need to setup
	// timing information for I_GetPreciseTime and sleeping
	I_StartupTimer();
}

void I_UpdateTime(fixed_t timescale)
{
	double elapsedseconds;
	tic_t realtics;

	// get real tics
	ticratescaled = (double)((float)TICRATE * FixedToFloat(timescale));

	enterprecise = I_GetPreciseTime();
	elapsedseconds = (double)(enterprecise - oldenterprecise) / I_GetPrecisePrecision();
	tictimer += elapsedseconds;
	while (tictimer > 1.0/ticratescaled)
	{
		entertic += 1;
		tictimer -= 1.0/ticratescaled;
	}
	realtics = entertic - oldentertics;
	oldentertics = entertic;
	oldenterprecise = enterprecise;

	// Update global time state
	g_time.time += realtics;
	{
		double fractional, integral;
		fractional = modf(tictimer * ticratescaled, &integral);
		g_time.timefrac = FLOAT_TO_FIXED(fractional);
	}
}

void I_SleepDuration(precise_t duration)
{
	UINT64 precision = I_GetPrecisePrecision();
	INT32 sleepvalue = cv_sleep.value;
	UINT64 delaygranularity;
	precise_t cur;
	precise_t dest;

	{
		double gran = round(((double)(precision / 1000) * sleepvalue * MIN_SLEEP_DURATION_MS));
		delaygranularity = (UINT64)gran;
	}

	cur = I_GetPreciseTime();
	dest = cur + duration;

	// the reason this is not dest > cur is because the precise counter may wrap
	// two's complement arithmetic is our friend here, though!
	// e.g. cur 0xFFFFFFFFFFFFFFFE = -2, dest 0x0000000000000001 = 1
	// 0x0000000000000001 - 0xFFFFFFFFFFFFFFFE = 3
	while ((INT64)(dest - cur) > 0)
	{
		// If our cv_sleep value exceeds the remaining sleep duration, use the
		// hard sleep function.
		if (sleepvalue > 0 && (dest - cur) > delaygranularity)
		{
#if defined(_WIN32)
			DWORD sleepDuration = (DWORD)min((INT64)(dest - cur), sleepvalue);
			SleepEx(sleepDuration, TRUE);
#else
			I_Sleep(sleepvalue);
#endif
		}

		// Otherwise, this is a spinloop.

		cur = I_GetPreciseTime();
	}
}
