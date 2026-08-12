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
    uint16_t id; // ID in the cluster
    fixed_t x, y, z;
    clusternodetype_t label;
    dboolean visited, in_cluster;
    player_t * player;
};

// Abstract form of a cluster; contains the nucleus node, density, and ID (by player placement)
struct clusterdata_t
{
    clusternode_t nucleus; // The node closest to the center of this cluster.
    uint16_t density; // The number of nodes in the cluster.
    uint16_t id; // Identifying number; used by the game to sort the data sructures
    int16_t pids[MAXPLAYERS]; // IDs of the players that belong to the nodes in this cluster
};

typedef dboolean (playerfilter_f)(player_t *player, uint32_t filter_value);

typedef dboolean (nodefunc_f)(clusternode_t * a, clusternode_t * b, fixed_t eps);

extern clusterdata_t * player_clusters;
extern int32_t num_player_clusters;

extern player_t *closesttocluster;

fixed_t K_Distance3D(fixed_t x1, fixed_t y1, fixed_t z1, fixed_t x2, fixed_t y2, fixed_t z2);
uint32_t K_GetDTFDifference(player_t *source, player_t *destination);

// Literally allows any player to pass through.
dboolean K_ClusterFilter_NoFilter(player_t *player, uint32_t fval);

dboolean K_ClusterFilter_BaseFilter(player_t *player, uint32_t fval);

// Filter function (`nodefunc_f`) for evaluating if a node is part of a cluster using its raw position in the database.
dboolean K_FilterNodeByEuclideanDistance(clusternode_t * a, clusternode_t * b, fixed_t eps);

// Filter function (`nodefunc_f`) for evaluating if a node is part of a cluster using its child player's distance to finish (DtF).
dboolean K_FilterNodeByDTF(clusternode_t * a, clusternode_t * b, fixed_t eps);

// Update the global player cluster array by using DBSCAN filtering
void K_UpdateGlobalClusters(fixed_t eps, int32_t min_pts, nodefunc_f * neighbor_func, playerfilter_f* cluster_filter, uint32_t filterval);

int32_t K_CountNeighboringPlayersByDistance(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, uint32_t filterval, vector3_t *out, void *nodevec);
int32_t K_CountNeighboringPlayersByDTFEx(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, uint32_t filterval, vector3_t *out, void *nodevec);
int32_t K_CountNeighboringPlayersByDTF(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, uint32_t filterval, vector3_t *out);
int32_t K_CountNeighboringPlayers(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, uint32_t filterval, vector3_t *out);

#ifdef __cplusplus
}
#endif

#endif // __K_CLUSTER__
