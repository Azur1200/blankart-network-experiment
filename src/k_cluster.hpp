#include "doomdef.h"
#include "d_player.h"
#include "m_fixed.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern player_t *closesttocluster;

INT32 K_CountNeighboringPlayersByDistance(player_t *sourcePlayer, fixed_t eps, vector3_t *out, void *nodevec);
INT32 K_CountNeighboringPlayersByDTF(player_t *sourcePlayer, fixed_t eps, vector3_t *out);
INT32 K_CountNeighboringPlayers(player_t *sourcePlayer, fixed_t eps, vector3_t *out);

#ifdef __cplusplus
}
#endif