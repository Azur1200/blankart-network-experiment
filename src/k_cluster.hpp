#ifndef __K_CLUSTER__
#define __K_CLUSTER__

#include "cvector.h"
#include "doomdef.h"
#include "d_player.h"
#include "m_fixed.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    NODETYPE_NOISE = 1, // Woag
    NODETYPE_BORDER,
    NODETYPE_CORE
} ATTRPACK clusternodetype_t;

struct clusternode_t
{
    UINT16 id; // ID in the cluster
    fixed_t x, y, z;
    clusternodetype_t label;
    dboolean visited, in_cluster;
    player_t * player;
};

// Abstract form of a cluster; contains the nucleus node, density, and ID (by player placement)
struct clusterdata_t
{
    clusternode_t nucleus; // The node closest to the center of this cluster.
    UINT16 density; // The number of nodes in the cluster.
    UINT16 id; // Identifying number; used by the game to sort the data sructures
    INT16 pids[MAXPLAYERS]; // IDs of the players that belong to the nodes in this cluster
};

typedef dboolean (playerfilter_f)(player_t *player, UINT32 filter_value);

typedef dboolean (nodefunc_f)(clusternode_t * a, clusternode_t * b, fixed_t eps);

extern clusterdata_t * player_clusters;
extern INT32 num_player_clusters;

extern player_t *closesttocluster;

fixed_t K_Distance3D(fixed_t x1, fixed_t y1, fixed_t z1, fixed_t x2, fixed_t y2, fixed_t z2);
UINT32 K_GetDTFDifference(player_t *source, player_t *destination);

// Literally allows any player to pass through.
dboolean K_ClusterFilter_NoFilter(player_t *player, UINT32 fval);

dboolean K_ClusterFilter_BaseFilter(player_t *player, UINT32 fval);

// Filter function (`nodefunc_f`) for evaluating if a node is part of a cluster using its raw position in the database.
dboolean K_FilterNodeByEuclideanDistance(clusternode_t * a, clusternode_t * b, fixed_t eps);

// Filter function (`nodefunc_f`) for evaluating if a node is part of a cluster using its child player's distance to finish (DtF).
dboolean K_FilterNodeByDTF(clusternode_t * a, clusternode_t * b, fixed_t eps);

// Update the global player cluster array by using DBSCAN filtering
void K_UpdateGlobalClusters(fixed_t eps, INT32 min_pts, nodefunc_f * neighbor_func, playerfilter_f* cluster_filter, UINT32 filterval);

INT32 K_CountNeighboringPlayersByDistance(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out, void *nodevec);
INT32 K_CountNeighboringPlayersByDTFEx(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out, void *nodevec);
INT32 K_CountNeighboringPlayersByDTF(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out);
INT32 K_CountNeighboringPlayers(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out);

#ifdef __cplusplus
}
#endif

#endif // __K_CLUSTER__
