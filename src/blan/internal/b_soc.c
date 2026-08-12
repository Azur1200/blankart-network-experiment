// Blankart
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by NepDisk
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#include "../b_soc.h"

// Map header toggle for wall transfering.
dboolean B_UseWallTransfer(mobj_t *mo)
{
	const mapheader_t *mapheader = mapheaderinfo[gamemap - 1];
	dboolean value = false;

	if (mo && mo->subsector && (mo->subsector->sector->specialflags & SSF_WALLTRANSFER))
		return true;

	if (mapnamespace == MNS_RINGRACERS)
		return true;

	//if (cv_kartwalltransfer.value)
		//return true;
    
	if (mapheader->use_walltransfer == true)
		value = true;

	if (mapheader->use_walltransfer == false)
		value = false;

	return value;
}

// Give each map a custom track complexity
int32_t B_TrackComplexity(void)
{
	const mapheader_t *mapheader = mapheaderinfo[gamemap - 1];
	const int32_t defaultvalue = atoi(cv_kartbot_basetrackcomplexity.defaultvalue);
	const int32_t mapvalue = mapheader->base_track_complexity;

	if (cv_kartbot_basetrackcomplexity.value != defaultvalue || mapvalue == INT32_MAX)
		return cv_kartbot_basetrackcomplexity.value;

	return mapvalue;
}

fixed_t K_TrackModifierMax(void)
{
	const mapheader_t *mapheader = mapheaderinfo[gamemap - 1];
	const fixed_t defaultvalue = FloatToFixed(atof(cv_kartbot_modifiermax.defaultvalue));
	const fixed_t mapvalue = mapheader->track_modifier_max;

	if (cv_kartbot_modifiermax.value != defaultvalue || mapvalue == INT32_MAX)
		return cv_kartbot_modifiermax.value;

	return mapvalue;
}
