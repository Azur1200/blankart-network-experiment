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

static fixed_t K_Distance3D(fixed_t x1, fixed_t y1, fixed_t z1, fixed_t x2, fixed_t y2, fixed_t z2)
{
    fixed_t dist_xy = R_PointToDist2(x1,y1,x2,y2);

    return R_PointToDist2(0,z1,dist_xy,z2);
}

static fixed_t K_PlayerDistance3D(player_t *source, player_t *destination)
{
    if ((!source->mo) || (!destination->mo))
        return INT32_MAX; // Return a garbage value.

    return K_Distance3D(source->mo->x,source->mo->y,source->mo->z,destination->mo->x,destination->mo->y,destination->mo->z);
}

static UINT32 K_GetDTFDifference(player_t *source, player_t *destination)
{
    if ((!source->mo) || (!destination->mo))
        return INT16_MAX; // Return a garbage value.

    if (destination->distancetofinish > source->distancetofinish)
        return (destination->distancetofinish - source->distancetofinish);

    return (source->distancetofinish - destination->distancetofinish);
}

extern "C" {
player_t *closesttocluster;

INT32 K_CountNeighboringPlayersByDistance(player_t *sourcePlayer, fixed_t eps, vector3_t *out, void *nodevec)
{
    INT64 meanx, meany, meanz;
    INT32 N;
    N = 0;
    vector3_t neighborvector;
    std::vector<player_t *>clusternodes;
    std::vector<player_t *> *realvec;
    
    if (nodevec != nullptr)
        realvec = static_cast<std::vector<player_t *> *>(nodevec);

    if (!sourcePlayer->mo)
        return 0;

    meanx = meany = meanz = 0;

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

        if (!K_IsPlayerLosing(player))
            continue; // Ignore winning players to prevent sandbagging.
        
        // Scan all points in the database
        if (K_PlayerDistance3D(sourcePlayer, player) <= eps)
        {   
            clusterplayer[i] = true;
            clusternodes.push_back(player);

            if (realvec)
                realvec->push_back(player);

            // Scan our neighbor for any players close to them.
            K_CountNeighboringPlayersByDistance(player, eps, &neighborvector, &clusternodes);
        }
    }

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

INT32 K_CountNeighboringPlayersByDTF(player_t *sourcePlayer, fixed_t eps, vector3_t *out)
{
    INT64 mean;
    INT32 N = 0;
    vector3_t neighborvector;

    if (!sourcePlayer->mo)
        return 0;

    if (cleardtf)
        dtf_vec.clear();
    
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

        if (!K_IsPlayerLosing(player))
            continue; // Ignore winning players to prevent sandbagging.
        
        // Scan all points in the database
        if ((fixed_t)K_GetDTFDifference(sourcePlayer, player) <= eps)
        {
            //CONS_Printf("%d is less than %d, adding to nodes vector\n", K_GetDTFDifference(sourcePlayer, player), eps);
            clusterplayer[i] = true;
            dtf_vec.push_back(player); // Add to result

            cleardtf = false;

            // Scan our neighbor for any players close to them.
            K_CountNeighboringPlayersByDTF(player, eps, &neighborvector);

            cleardtf = true;
        }
    }

    N = dtf_vec.size();

    if (N != 0)
    {
        // Return the average center point of this cluster.
        for (i = 0; i < N; i++)
        {
            if (!dtf_vec[i])
                continue;

            if (!dtf_vec[i]->mo)
                continue;

            mean += dtf_vec[i]->distancetofinish;
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
            disttocluster = K_GetU32Diff(dtf_vec[i]->distancetofinish, mean);

            if (disttocluster < bestdist)
            {
                closesttocluster = dtf_vec[i];
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

INT32 K_CountNeighboringPlayers(player_t *sourcePlayer, fixed_t eps, vector3_t *out)
{
    // Dummy vector so the game doesn't SIGSEGV.
    // There's probably a better solution to this.
    std::vector<player_t *> dummy = {0};

    return K_CountNeighboringPlayersByDistance(sourcePlayer, eps, out, &dummy);
}

}