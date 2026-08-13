#include "k_cluster.hpp"

#include <vector>

#include "d_player.h"
#include "doomdef.h"
#include "doomstat.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "k_items.h"
#include "k_kart.h"
#include "m_fixed.h"
#include "r_main.h"
#include "z_zone.h"

static fixed_t K_PlayerDistance3D(player_t* source, player_t* destination)
{
    if ((!source->mo) || (!destination->mo))
        return INT32_MAX;  // Return a garbage value.

    return K_Distance3D(source->mo->x,
                        source->mo->y,
                        source->mo->z,
                        destination->mo->x,
                        destination->mo->y,
                        destination->mo->z);
}

extern "C"
{

clusterdata_t* player_clusters = NULL;

int32_t num_player_clusters = 0;

player_t* closesttocluster;

fixed_t K_Distance3D(fixed_t x1, fixed_t y1, fixed_t z1, fixed_t x2, fixed_t y2, fixed_t z2)
{
    fixed_t dist_xy = R_PointToDist2(x1, y1, x2, y2);

    return R_PointToDist2(0, z1, dist_xy, z2);
}

uint32_t K_GetDTFDifference(player_t* source, player_t* destination)
{
    if ((!source->mo) || (!destination->mo))
        return INT16_MAX;  // Return a garbage value.

    if (destination->distancetofinish > source->distancetofinish)
        return (destination->distancetofinish - source->distancetofinish);

    return (source->distancetofinish - destination->distancetofinish);
}

dboolean K_ClusterFilter_NoFilter(player_t* player, uint32_t fval)
{
    // Ignores literally every parameter and returns true.
    return true;
}

// Store our last memory of the cluster player (because we're clearing it out for the second
// cluster).
// static dboolean clusterplayer_memory[MAXPLAYERS];

// Filters players based on various factors, such as playercount and position.
// fval = pingame
dboolean K_ClusterFilter_BaseFilter(player_t* player, uint32_t fval)
{
    uint8_t pingame = static_cast<uint8_t>(fval & 0xFF);

    if (player->position >= pingame)
    {
        return false;  // Ignore last place. *They* need help the most!
    }

    if (pingame > 9)
    {
        // ALWAYS ignore first and second for the double-digit lobbies
        // As playercounts get bigger, the "top" player threshold does as well
        // Just in case, let's cap THAT at 4
        uint8_t top_positions = std::min(4, std::max(2, pingame / 8));

        if (player->position <= top_positions)
        {
            return false;  // Ignore winning players to prevent sandbagging.
        }
    }
    else if (pingame > 4)
    {
        if (player->position <= 1)
        {
            return false;  // Ignore the frontrunner.
        }
    }

    // Nothing to filter.
    return true;
}

int32_t K_CountNeighboringPlayersByDistance(player_t* sourcePlayer,
                                          fixed_t eps,
                                          playerfilter_f* cluster_filter,
                                          uint32_t filterval,
                                          vector3_t* out,
                                          void* nodevec)
{
    int64_t meanx, meany, meanz;
    int32_t N;
    N = 0;
    vector3_t neighborvector;
    std::vector<player_t*> clusternodes;

    if (!sourcePlayer->mo)
        return 0;

    meanx = meany = meanz = 0;

    int32_t i;
    uint8_t pingame = 0;

    // To hell with this; scan for ourselves and pre-emptively flag ourselves.
    for (i = 0; i < MAXPLAYERS; i++)
    {
        if (!playeringame[i])
            continue;

        if (&players[i] == sourcePlayer)
        {
            clusterplayer[i] = true;
        }

        if (players[i].spectator)
            continue;  // spectator

        if (!players[i].mo)
            continue;

        pingame++;
    }

    for (i = 0; i < MAXPLAYERS; i++)
    {
        player_t* player;

        if (!playeringame[i])
            continue;

        player = &players[i];

        if (player == sourcePlayer)
        {
            // Ignore ourselves.
            continue;
        }

        if (clusterplayer[i])
            continue;  // Ignore players we've scanned already.

        if (player->spectator)
            continue;  // spectator

        if (!player->mo)
            continue;

        if (cluster_filter)
        {
            if (!cluster_filter(player, filterval))
                continue;
        }

        // Scan all points in the database
        fixed_t dist = K_PlayerDistance3D(sourcePlayer, player);

        /*CONS_Printf("%s: checking %f against epsilon value %f\n",
            player_names[i],
            FIXED_TO_FLOAT(dist), FIXED_TO_FLOAT(eps)
        );*/
        if (dist <= eps)
        {
            clusterplayer[i] = true;
            // CONS_Printf("%s is in this cluster\n", player_names[i]);

            if (nodevec)
                static_cast<std::vector<player_t*>*>(nodevec)->push_back(player);

            // Scan our neighbor for any players close to them.
            K_CountNeighboringPlayersByDistance(
                player, eps, cluster_filter, filterval, &neighborvector, nodevec);
        }
    }

    clusternodes = *static_cast<std::vector<player_t*>*>(nodevec);

    N = clusternodes.size();

    if (N != 0)
    {
        // Return the average center point of this cluster.
        for (i = 0; i < N; i++)
        {
            if (!clusternodes[i])
                continue;

            if (!clusternodes[i]->mo)
                continue;

            meanx += clusternodes[i]->mo->x / FRACUNIT;
            meany += clusternodes[i]->mo->y / FRACUNIT;
            meanz += clusternodes[i]->mo->z / FRACUNIT;
        }

        meanx /= N;
        meany /= N;
        meanz /= N;

        // CONS_Printf("mean x: %lld, mean y: %lld, mean z: %lld\n", meanx, meany, meanz);

        out->x = (fixed_t)(meanx) << FRACBITS;
        out->y = (fixed_t)(meany) << FRACBITS;
        out->z = (fixed_t)(meanz) << FRACBITS;

        // Get the player closest to the cluster.
        fixed_t disttocluster, bestdist;
        bestdist = INT32_MAX;
        for (i = 0; i < N; i++)
        {
            if (!clusternodes[i])
                continue;

            if (!clusternodes[i]->mo)
                continue;

            disttocluster = K_Distance3D(clusternodes[i]->mo->x,
                                         clusternodes[i]->mo->y,
                                         clusternodes[i]->mo->z,
                                         out->x,
                                         out->y,
                                         out->z);

            if (disttocluster < bestdist)
            {
                closesttocluster = clusternodes[i];
                bestdist = disttocluster;
            }
        }
    }
    else
    {
        out->z = out->y = out->x = 0;
    }

    return N;
}

static std::vector<player_t*> dtf_vec;
static dboolean cleardtf = true;

static uint32_t K_GetU32Diff(uint32_t a, uint32_t b)
{
    return (b > a) ? (b - a) : (a - b);
}

int32_t K_CountNeighboringPlayersByDTFEx(player_t* sourcePlayer,
                                       fixed_t eps,
                                       playerfilter_f* cluster_filter,
                                       uint32_t filterval,
                                       vector3_t* out,
                                       void* nodevec)
{
    int64_t mean;
    int32_t N = 0;
    vector3_t neighborvector;

    if (!nodevec)
    {
        // Nice goddamn try.
        return 0;
    }

    if (!sourcePlayer->mo)
        return 0;

    mean = 0;

    int32_t i;
    for (i = 0; i < MAXPLAYERS; i++)
    {
        player_t* player;

        if (!playeringame[i])
            continue;

        player = &players[i];

        if (player == sourcePlayer)
            continue;  // Ignore ourselves.

        if (clusterplayer[i])
            continue;  // Ignore players we've scanned already.

        if (player->spectator)
            continue;  // spectator

        if (!player->mo)
            continue;

        if (cluster_filter)
        {
            if (!cluster_filter(player, filterval))
                continue;
        }

        // Scan all points in the database
        if ((fixed_t)K_GetDTFDifference(sourcePlayer, player) <= eps)
        {
            // CONS_Printf("%d is less than %d, adding to nodes vector\n",
            // K_GetDTFDifference(sourcePlayer, player), eps);
            clusterplayer[i] = true;

            if (nodevec)
            {
                // Add to the result.
                static_cast<std::vector<player_t*>*>(nodevec)->push_back(player);
            }

            // Scan our neighbor for any players close to them.
            K_CountNeighboringPlayersByDTFEx(
                player, eps, cluster_filter, filterval, &neighborvector, nodevec);
        }
    }

    N = static_cast<std::vector<player_t*>*>(nodevec)->size();

    if (N != 0)
    {
        // Return the average center point of this cluster.
        for (i = 0; i < N; i++)
        {
            if (!static_cast<std::vector<player_t*>*>(nodevec)->at(i))
                continue;

            if (!static_cast<std::vector<player_t*>*>(nodevec)->at(i)->mo)
                continue;

            mean += static_cast<std::vector<player_t*>*>(nodevec)->at(i)->distancetofinish;
        }
        mean /= N;

        out->x = (fixed_t)(mean);
        out->y = 0;
        out->z = 0;

        // Get the player closest to the cluster.
        uint32_t disttocluster, bestdist;
        bestdist = UINT32_MAX;
        for (i = 0; i < N; i++)
        {
            disttocluster = K_GetU32Diff(
                static_cast<std::vector<player_t*>*>(nodevec)->at(i)->distancetofinish, mean);

            if (disttocluster < bestdist)
            {
                closesttocluster = static_cast<std::vector<player_t*>*>(nodevec)->at(i);
                bestdist = disttocluster;
            }
        }
    }
    else
    {
        out->z = out->y = out->x = 0;
    }

    return N;
}

int32_t K_CountNeighboringPlayersByDTF(player_t* sourcePlayer,
                                     fixed_t eps,
                                     playerfilter_f* cluster_filter,
                                     uint32_t filterval,
                                     vector3_t* out)
{
    if (cleardtf)
        dtf_vec.clear();

    return K_CountNeighboringPlayersByDTFEx(
        sourcePlayer, eps, cluster_filter, filterval, out, &dtf_vec);
}

int32_t K_CountNeighboringPlayers(player_t* sourcePlayer,
                                fixed_t eps,
                                playerfilter_f* cluster_filter,
                                uint32_t filterval,
                                vector3_t* out)
{
    // Dummy vector so the game doesn't SIGSEGV.
    // Turns out, it's also necessary for this system not to be placebo!
    std::vector<player_t*> dummy = {0};

    dummy.clear();
    int32_t result = K_CountNeighboringPlayersByDistance(
        sourcePlayer, eps, cluster_filter, filterval, out, &dummy);

    return result;
}

dboolean K_FilterNodeByEuclideanDistance(clusternode_t* a, clusternode_t* b, fixed_t eps)
{
    int64_t eps_integer = (int32_t)(eps / FRACUNIT);

    return (static_cast<int64_t>(K_IntDistanceForMap(a->x, a->y, a->z, b->x, b->y, b->z)) <=
            eps_integer);
}

dboolean K_FilterNodeByDTF(clusternode_t* a, clusternode_t* b, fixed_t eps)
{
    if (!a)
        return false;

    if (!b)
        return false;

    if (!a->player)
        return false;

    if (!b->player)
        return false;

    int32_t eps_integer = (int32_t)(eps / FRACUNIT);

    // C++ is such an unserious language...
    int32_t dtf_diff = static_cast<int32_t>(
        std::min(static_cast<uint32_t>(0x3FFFFFFF), K_GetDTFDifference(a->player, b->player)));

    dtf_diff = FixedDiv(dtf_diff, mapobjectscale);

    return (dtf_diff <= eps_integer);
}

static void GetNodesInRange(std::vector<clusternode_t>* dest,
                            std::vector<clusternode_t>* DB,
                            clusternode_t node,
                            nodefunc_f* neighbor_func,
                            fixed_t eps)
{
    if (dest == nullptr)
        return;  // Null destination vector

    // Whatever is in this vector... it's gone now
    dest->clear();

    int32_t i = 0;
    for (i = 0; i < static_cast<int32_t>(DB->size()); i++)
    {
        if (DB->at(i).id == node.id)
        {
            // Don't count OURSELVES!
            continue;
        }

        if ((DB->at(i).visited) || (DB->at(i).in_cluster))
        {
            // Don't retrace steps!
            continue;
        }

        if (neighbor_func(&node, &DB->at(i), eps))
        {
            DB->at(i).visited = true;
            // We're neighbors now! :)
            dest->push_back(DB->at(i));
        }
    }
}

// ACTUAL DBSCAN clustering! Used to compile a dynamic array of data structures containing summaries
// of all present clusters.
static void K_DBSCAN(std::vector<clusternode_t> DB,
                     nodefunc_f* neighbor_func,
                     fixed_t eps,
                     int32_t minPts)
{
    int32_t num_clusters = 0;  // Cluster counter
    int32_t numneighbors = 0;
    int32_t i = 0;
    int32_t j = 0;
    int32_t k = 0;

    std::vector<clusternode_t> cluster;
    std::vector<clusternode_t> neighbors;
    std::vector<clusternode_t> neighbors2;

    std::vector<clusterdata_t> pending_globals;
    clusterdata_t new_data = {};
    vector3_64_t average_point = {0};

    int32_t average_player_pos = 0;

    fixed_t dist = 0;
    fixed_t closest = INT32_MAX;

    num_player_clusters = 0;

    for (i = 0; i < static_cast<int32_t>(DB.size()); i++)
    {
        if (DB[i].label)
        {
            // Previously processed in inner loop
            continue;
        }

        if (DB[i].in_cluster)
        {
            // Already in a cluster; ignore us
            continue;
        }

        GetNodesInRange(&neighbors, &DB, DB[i], neighbor_func, eps);  // Find neighbors

        numneighbors = neighbors.size();

        if ((numneighbors + 1) < minPts)  // Density check (factor ourselves in)
        {
            if (!DB[i].in_cluster)  // As long as we're in a cluster, we're NOT noise
            {
                // Bring the Noise!
                DB[i].label = NODETYPE_NOISE;  // Label as Noise
                DB[i].visited = false;
            }
            continue;
        }

        num_clusters++;  // next cluster label

        DB[i].label = NODETYPE_CORE;  // Label initial point
        DB[i].visited = true;

        if (!DB[i].in_cluster)
        {
            DB[i].in_cluster = true;
            cluster.push_back(DB[i]);
        }

        // SeedSet S := N \ {P} means creating a vector S with all nodes N excluding point P
        // GetNodesInRange already does this, so we can just use the neighbors vector

        j = 0;
        uint8_t stuck = 0;
        while (j < numneighbors)
        {
            stuck++;

            if (stuck >= 8)
            {
                // Immediately bail
                I_Error(
                    "DBSCAN while loop got stuck trying to process neighbors (iterated 8 times "
                    "with no progress)\n");
                return;
            }

            // Process every seed point Q
            if (neighbors[j].label == NODETYPE_NOISE)
            {
                // Change Noise to border point
                DB[neighbors[j].id].label = NODETYPE_BORDER;
                neighbors[j].label = NODETYPE_BORDER;

                // Just in case: mark the source node as visited
                DB[neighbors[j].id].visited = true;

                if (!DB[neighbors[j].id].in_cluster)
                {
                    DB[neighbors[j].id].in_cluster = true;
                    neighbors[j].in_cluster = true;
                    cluster.push_back(neighbors[j]);
                }
            }

            if (neighbors[j].label)
            {
                // Previously processed (e.g., border point)
                j++;
                stuck--;
                continue;
            }

            if (!neighbors[j].in_cluster)
            {
                // Label neighbor
                DB[neighbors[j].id].label = NODETYPE_CORE;
                neighbors[j].label = NODETYPE_CORE;

                DB[neighbors[j].id].in_cluster = true;
                neighbors[j].in_cluster = true;

                cluster.push_back(neighbors[j]);
            }

            GetNodesInRange(&neighbors2, &DB, neighbors[j], neighbor_func, eps);  // Find neighbors

            if ((static_cast<int32_t>(neighbors2.size()) + 1) >=
                minPts)  // Density check (if Q is a core point)
            {
                // Add new neighbors to seed set
                // S := S ∪ N

                for (k = 0; k < static_cast<int32_t>(neighbors2.size()); k++)
                {
                    DB[neighbors2[k].id].label = NODETYPE_CORE;
                    neighbors2[k].label = NODETYPE_CORE;  // Label neighbor
                    neighbors.push_back(neighbors2[k]);
                }
            }

            neighbors2.clear();

            if (static_cast<int32_t>(neighbors.size()) != numneighbors)
            {
                numneighbors = static_cast<int32_t>(neighbors.size());
                j = 0;  // Start the loop over
                stuck--;
                continue;
            }

            j++;
            stuck--;
        }
        // Empty our neighbor datasets
        neighbors.clear();
        neighbors2.clear();

        if (cluster.size() > 0)
        {
            // We have a populated cluster, so let's add it to our clusters list!

            // First, empty the temp data.

            memset(&new_data, 0, sizeof(new_data));
            memset(&average_point, 0, sizeof(average_point));

            // Set density
            new_data.density = cluster.size();

            // Now, get the average position for this cluster, and gather the player IDs
            for (j = 0; j < new_data.density; j++)
            {
                average_point.x += cluster[j].x;
                average_point.y += cluster[j].y;
                average_point.z += cluster[j].z;

                new_data.pids[j] = -1;

                if (cluster[j].player)
                {
                    new_data.pids[j] = static_cast<int16_t>(cluster[j].player - players);
                    average_player_pos += cluster[j].player->position;
                }
            }

            if (new_data.density > 1)
            {
                average_point.x /= new_data.density;
                average_point.y /= new_data.density;
                average_point.z /= new_data.density;

                average_player_pos /= new_data.density;
            }

            closest = INT32_MAX;

            // And get the node closest to this average point (the nucleus)
            for (j = 0; j < new_data.density; j++)
            {
                dist = K_IntDistanceForMap(cluster[j].x,
                                           cluster[j].y,
                                           cluster[j].z,
                                           static_cast<fixed_t>(average_point.x),
                                           static_cast<fixed_t>(average_point.y),
                                           static_cast<fixed_t>(average_point.z));

                if (dist < closest)
                {
                    new_data.nucleus.x = cluster[j].x;
                    new_data.nucleus.y = cluster[j].y;
                    new_data.nucleus.z = cluster[j].z;

                    new_data.nucleus.label = cluster[j].label;

                    new_data.nucleus.player = cluster[j].player;

                    new_data.id = average_player_pos;

                    closest = dist;
                }
            }

            // Finally, add it to the global vector
            pending_globals.push_back(new_data);

            // Before we go: empty this cluster
            cluster.clear();
        }
    }

    neighbors.clear();
    neighbors2.clear();
    DB.clear();

    // If we've cached a player cluster array, allocate, then sort them

    int32_t numglobals = pending_globals.size();

    if (numglobals > 0)
    {
        if (player_clusters)
        {
            player_clusters = static_cast<clusterdata_t*>(
                Z_Realloc(player_clusters, sizeof(clusterdata_t) * numglobals, PU_STATIC, NULL));
        }
        else
        {
            player_clusters = static_cast<clusterdata_t*>(
                Z_Malloc(sizeof(clusterdata_t) * numglobals, PU_STATIC, NULL));
        }

        memset(player_clusters, 0, sizeof(clusterdata_t) * numglobals);

        // Now: add ALL our datapoints to the table
        for (i = 0; i < numglobals; i++)
        {
            player_clusters[i] = pending_globals.at(i);
        }
    }
    else if (player_clusters)
    {
        Z_Free(player_clusters);
        player_clusters = NULL;
    }

    num_player_clusters = numglobals;
}

// Updates the `player_clusters` list via DBSCAN clustering; compiles multiple clusters at once,
// instead of a single one
void K_UpdateGlobalClusters(fixed_t eps,
                            int32_t min_pts,
                            nodefunc_f* neighbor_func,
                            playerfilter_f* cluster_filter,
                            uint32_t filterval)
{
    std::vector<clusternode_t> playernodes;

    playernodes.clear();

    clusternode_t temp_node = {0};

    int32_t i = 0;
    for (i = 0; i < MAXPLAYERS; i++)
    {
        player_t* player = NULL;

        if (!playeringame[i])
            continue;

        player = &players[i];

        if (player->spectator)
            continue;  // spectator

        if (!player->mo)
            continue;

        if (cluster_filter)
        {
            if (!cluster_filter(player, filterval))
                continue;
        }

        // Add this player to the vector, since they aren't filtered out

        memset(&temp_node, 0, sizeof(temp_node));

        temp_node.player = player;
        temp_node.x = player->mo->x;
        temp_node.y = player->mo->y;
        temp_node.z = player->mo->z;

        temp_node.id = static_cast<uint16_t>(playernodes.size());

        temp_node.in_cluster = false;
        temp_node.visited = false;

        playernodes.push_back(temp_node);
    }

    K_DBSCAN(playernodes, neighbor_func, eps, min_pts);
}
}
