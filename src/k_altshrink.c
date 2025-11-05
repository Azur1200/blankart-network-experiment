// BLANKART
//-----------------------------------------------------------------------------
/// \file  k_altshrink.c
/// \brief Alternative Shrink item type functions.

#include "doomdef.h"
#include "doomtype.h"
#include "doomstat.h"

#include "command.h"
#include "console.h"
#include "d_player.h"
#include "k_altshrink.h"
#include "k_cluster.hpp"
#include "m_fixed.h"
#include "p_local.h"


UINT8 shrinktype;

void K_UpdateShrinkType(void)
{
    if (leveltime < starttime)
	{
		CONS_Printf(M_GetText("Shrink type has been changed to \"%s\".\n"), cv_kartshrinktype.string);
	}

    shrinktype = (UINT8)cv_kartshrinktype.value;
}

/**	\brief	Depending on the Shrink type set, this returns true or false if the player is shrunk.

	\param	player		(player_t) Player to test shrunken status for
	\param	legacy		(boolean) Legacy shrink?

	\return	void
*/
boolean K_IsShrunkMode(player_t *player, boolean legacy)
{
    boolean shrunk = K_IsShrunk(player);
    boolean legacytype = (shrinktype == KARTINVIN_LEGACY);

    if (legacy)
    {
        return ((shrunk) && (legacytype));
    }

    return ((shrunk) && (!legacytype));
}

#define ALTSHRINK_EPSILON (320 * FRACUNIT)
#define NEIGHBOR_IFRAMES (TICRATE / 2)
#define BASE_IFRAMES (2 * TICRATE)

void K_AltShrinkIFrames(player_t *player)
{
	vector3_t tempclusterpoint;

	// Find neighbors
    INT32 N = K_CountNeighboringPlayers(player, ALTSHRINK_EPSILON, &tempclusterpoint);

	// For every neighbor, add some iframes for a clean breakaway.
	player->flashing = (UINT16)min((INT32)(UINT16_MAX), BASE_IFRAMES + (NEIGHBOR_IFRAMES * N));
}

#define PITY_SHRINKINCREASE_BASE (3)
#define PITY_SHRINKINCREASE (TICRATE / 2)

void K_AltShrinkPityIncrease(player_t *player)
{
    // Increase your shrink timer by a little bit for every player you run into.
    INT32 shrinktime = (UINT32)(K_GetShrinkTime(player));

    fixed_t dimin = FRACUNIT;

    dimin = max(0, 5 - player->altshrinktimeshit) * FRACUNIT / 5;

    shrinktime = min(INT16_MAX, shrinktime + PITY_SHRINKINCREASE_BASE + FixedMul(PITY_SHRINKINCREASE, dimin));

    player->growshrinktimer = ((INT16)shrinktime) * -1;
}