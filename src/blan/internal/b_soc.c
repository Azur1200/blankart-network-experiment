// Blankart
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by NepDisk
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#include "../b_soc.h"

// Map header toggle for wall transfering.
boolean B_UseWallTransfer(void)
{
	const mapheader_t *mapheader = mapheaderinfo[gamemap - 1];
	boolean value = false;

	if (mapnamespace == MNS_RINGRACERS)
		return true;

	if (cv_kartwalltransfer.value)
		return true;
    
	if (mapheader->use_walltransfer == true)
		value = true;

	if (mapheader->use_walltransfer == false)
		value = false;

	return value;
}

// Map header toggle for TerrainDefs.
boolean B_UseTerrainDef(void)
{
	const mapheader_t *mapheader = mapheaderinfo[gamemap - 1];
	boolean value = false;

	if (mapnamespace == MNS_RINGRACERS)
		return true;

	if (mapheader->use_terrain == true)
		value = true;

	if (mapheader->use_terrain == false)
		value = false;

	return value;
}
