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
    
	if (mapheader->use_walltransfer == true)
		value = true;

	if (mapheader->use_walltransfer == false)
		value = false;

	if (mapnamespace == MNS_RINGRACERS)
		value = true;

	if (cv_kartwalltransfer.value)
		value = true;

	return value;
}
