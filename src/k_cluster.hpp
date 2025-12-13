#include "doomdef.h"
#include "d_player.h"
#include "m_fixed.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef boolean (playerfilter_f)(player_t *player, UINT32 filter_value);
extern player_t *closesttocluster;

// Literally allows any player to pass through.
boolean K_ClusterFilter_NoFilter(player_t *player, UINT32 fval);

boolean K_ClusterFilter_BaseFilter(player_t *player, UINT32 fval);

INT32 K_CountNeighboringPlayersByDistance(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out, void *nodevec);
INT32 K_CountNeighboringPlayersByDTFEx(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out, void *nodevec);
INT32 K_CountNeighboringPlayersByDTF(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out);
INT32 K_CountNeighboringPlayers(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out);

#ifdef __cplusplus
}
#endif