// BLANKART
//-----------------------------------------------------------------------------
/// \file  k_altshrink.h
/// \brief Alternative Shrink item type functions.

#ifndef __K_ALTSHRINK__
#define __K_ALTSHRINK__

#include "doomdef.h"
#include "d_player.h" // Need for player_t
#include "command.h" // Need for player_t
#include "m_fixed.h"
#include "k_kart.h"

extern consvar_t cv_kartaltshrinktime;
extern consvar_t cv_kartshrinktype;

extern consvar_t cv_kartstacking_altshrink_speedboost;
extern consvar_t cv_kartstacking_altshrink_accelboost;
extern consvar_t cv_kartstacking_altshrink_stackable;

extern UINT8 shrinktype;

#define ALTSHRINKTIME CV_Get(&cv_kartaltshrinktime)

#define SHRINKSPEEDBOOST CV_Get(&cv_kartstacking_altshrink_speedboost)
#define SHRINKACCELBOOST CV_Get(&cv_kartstacking_altshrink_accelboost)
#define SHRINKSTACKABLE CV_Get(&cv_kartstacking_altshrink_stackable)

/**	\brief	Returns true or false if Alt. Shrink is active.

	\return	Are we using the alternative Shrink type?
*/
FUNCINLINE static ATTRINLINE boolean K_AltShrinkActive(void)
{
    return (shrinktype == KARTINVIN_ALTERN);
}


/**	\brief	Returns true or false if the player is shrunk.

	\param	player		(player_t) Player to test shrunken status for

	\return	Is this player shrunk?
*/
FUNCINLINE static ATTRINLINE boolean K_IsShrunk(player_t *player)
{
    return (player->growshrinktimer < 0);
}

FUNCINLINE static ATTRINLINE INT16 K_GetShrinkTime(player_t *player)
{
    return (player->growshrinktimer * -1);
}

FUNCINLINE static ATTRINLINE fixed_t K_AccomodateShrinkScaling(fixed_t x)
{
    return FixedDiv(x, SHRINK_SCALE);
}

boolean K_IsShrunkMode(player_t *player, boolean legacy);

#define K_IsLegacyShrunk(player) (K_IsShrunkMode(player, true))
#define K_IsAltShrunk(player) (K_IsShrunkMode(player, false))

void K_UpdateShrinkType(void);
void K_AltShrinkIFrames(player_t *player);
void K_AltShrinkPityIncrease(player_t *player);

#endif // __K_ALTSHRINK__