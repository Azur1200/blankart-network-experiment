#include "doomdef.h"
#include "doomstat.h"
#include "hu_stuff.h"
#include "d_player.h"
#include "m_fixed.h"
#include "k_kart.h"
#include "r_main.h"
#include "g_game.h"

#include "k_cluster.hpp"
#include <vector>

static fixed_t K_PlayerDistance3D(player_t *source, player_t *destination)
{
    if ((!source->mo) || (!destination->mo))
        return INT32_MAX; // Return a garbage value.

    return K_Distance3D(source->mo->x,source->mo->y,source->mo->z,destination->mo->x,destination->mo->y,destination->mo->z);
}

extern "C" {
player_t *closesttocluster;

fixed_t K_Distance3D(fixed_t x1, fixed_t y1, fixed_t z1, fixed_t x2, fixed_t y2, fixed_t z2)
{
    fixed_t dist_xy = R_PointToDist2(x1,y1,x2,y2);

    return R_PointToDist2(0,z1,dist_xy,z2);
}

UINT32 K_GetDTFDifference(player_t *source, player_t *destination)
{
    if ((!source->mo) || (!destination->mo))
        return INT16_MAX; // Return a garbage value.

    if (destination->distancetofinish > source->distancetofinish)
        return (destination->distancetofinish - source->distancetofinish);

    return (source->distancetofinish - destination->distancetofinish);
}

boolean K_ClusterFilter_NoFilter(player_t *player, UINT32 fval)
{
    // Ignores literally every parameter and returns true.
    return true;
}

// Store our last memory of the cluster player (because we're clearing it out for the second cluster).
static boolean clusterplayer_memory[MAXPLAYERS];

// Filters players based on various factors, such as playercount and position.
// fval = pingame
boolean K_ClusterFilter_BaseFilter(player_t *player, UINT32 fval)
{
    UINT8 pingame = static_cast<UINT8>(fval & 0xFF);

    if (player->position >= pingame)
    {
        return false; // Ignore last place. *They* need help the most!
    }

    if (pingame > 6)
    {
        // FIXME: Double-cluster algorithm.
        // Can't do it from within this function, or we're going to have too much overhead.
        // However: at above 18 players, we shouldn't even *consider* double-clustering.
        if (!K_IsPlayerLosing(player))
        {
            return false; // Ignore winning players to prevent sandbagging.
        }
    }
    else if (pingame > 4)
    {
        if (player->position <= 1)
        {
            return false; // Ignore the frontrunner.
        }
    }

    // Nothing to filter.

    return true;
}

INT32 K_CountNeighboringPlayersByDistance(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out, void *nodevec)
{
    INT64 meanx, meany, meanz;
    INT32 N;
    N = 0;
    vector3_t neighborvector;
    std::vector<player_t *>clusternodes;

    if (!sourcePlayer->mo)
        return 0;

    meanx = meany = meanz = 0;

    INT32 i;
    UINT8 pingame = 0;

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
			continue; // spectator

		if (!players[i].mo)
			continue;

        pingame++;
    }

    for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *player;

		if (!playeringame[i])
			continue;

		player = &players[i];

        if (player == sourcePlayer)
        {
            // Ignore ourselves.
            continue;
        }

        if (clusterplayer[i])
            continue; // Ignore players we've scanned already.

		if (player->spectator)
			continue; // spectator

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
            //CONS_Printf("%s is in this cluster\n", player_names[i]);

            if (nodevec)
                static_cast<std::vector<player_t *> *>(nodevec)->push_back(player);

            // Scan our neighbor for any players close to them.
            K_CountNeighboringPlayersByDistance(player, eps, cluster_filter, filterval, &neighborvector, nodevec);
        }
    }

    clusternodes = *static_cast<std::vector<player_t *> *>(nodevec);

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

        //CONS_Printf("mean x: %lld, mean y: %lld, mean z: %lld\n", meanx, meany, meanz);

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

            disttocluster = K_Distance3D(clusternodes[i]->mo->x,clusternodes[i]->mo->y,clusternodes[i]->mo->z,out->x,out->y,out->z);

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

static std::vector<player_t *> dtf_vec;
static boolean cleardtf = true;

static UINT32 K_GetU32Diff(UINT32 a, UINT32 b)
{
    return (b > a) ? (b - a) : (a - b);
}

INT32 K_CountNeighboringPlayersByDTFEx(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out, void *nodevec)
{
    INT64 mean;
    INT32 N = 0;
    vector3_t neighborvector;

    if (!nodevec)
    {
        // Nice goddamn try.
        return 0;
    }

    if (!sourcePlayer->mo)
        return 0;
    
    mean = 0;

    INT32 i;
    for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *player;

		if (!playeringame[i])
			continue;

		player = &players[i];

        if (player == sourcePlayer)
            continue; // Ignore ourselves.

        if (clusterplayer[i])
            continue; // Ignore players we've scanned already.

		if (player->spectator)
			continue; // spectator

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
            //CONS_Printf("%d is less than %d, adding to nodes vector\n", K_GetDTFDifference(sourcePlayer, player), eps);
            clusterplayer[i] = true;

            if (nodevec)
            {
                // Add to the result.
                static_cast<std::vector<player_t *> *>(nodevec)->push_back(player);
            }

            // Scan our neighbor for any players close to them.
            K_CountNeighboringPlayersByDTFEx(player, eps, cluster_filter, filterval, &neighborvector, nodevec);
        }
    }

    N = static_cast<std::vector<player_t *> *>(nodevec)->size();

    if (N != 0)
    {
        // Return the average center point of this cluster.
        for (i = 0; i < N; i++)
        {
            if (!static_cast<std::vector<player_t *> *>(nodevec)->at(i))
                continue;

            if (!static_cast<std::vector<player_t *> *>(nodevec)->at(i)->mo)
                continue;

            mean += static_cast<std::vector<player_t *> *>(nodevec)->at(i)->distancetofinish;
        }
        mean /= N;

        out->x = (fixed_t)(mean);
        out->y = 0;
        out->z = 0;

        // Get the player closest to the cluster.
        UINT32 disttocluster, bestdist;
        bestdist = UINT32_MAX;
        for (i = 0; i < N; i++)
        {
            disttocluster = K_GetU32Diff(static_cast<std::vector<player_t *> *>(nodevec)->at(i)->distancetofinish, mean);

            if (disttocluster < bestdist)
            {
                closesttocluster = static_cast<std::vector<player_t *> *>(nodevec)->at(i);
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

INT32 K_CountNeighboringPlayersByDTF(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out)
{
    if (cleardtf)
        dtf_vec.clear();

    return K_CountNeighboringPlayersByDTFEx(sourcePlayer, eps, cluster_filter, filterval, out, &dtf_vec);
}

INT32 K_CountNeighboringPlayers(player_t *sourcePlayer, fixed_t eps, playerfilter_f *cluster_filter, UINT32 filterval, vector3_t *out)
{
    // Dummy vector so the game doesn't SIGSEGV.
    // Turns out, it's also necessary for this system not to be placebo!
    std::vector<player_t *> dummy = {0};

    dummy.clear();
    INT32 result = K_CountNeighboringPlayersByDistance(sourcePlayer, eps, cluster_filter, filterval, out, &dummy);

    return result;
}

}