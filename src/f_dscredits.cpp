// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Vivian "toastergrl" Grannell.
// Copyright (C) 2025 by Kart Krew.
// Copyright (C) 2026 by "yama".
// Copyright (C) 2026 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_dscredits.cpp
/// \brief (Mostly) secret MKDS credits :)

#include <chrono>

#include "f_dscredits.hpp"

#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "d_netcmd.h"
#include "f_finale.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "r_local.h"
#include "s_sound.h"
#include "i_time.h"
#include "i_video.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "i_system.h"
#include "i_threads.h"
#include "m_menu.h"
#include "dehacked.h"
#include "g_input.h"
#include "console.h"
#include "m_random.h"
#include "m_misc.h" // moviemode functionality
#include "y_inter.h"
#include "m_cond.h"
#include "p_local.h"
#include "p_setup.h"
#include "st_stuff.h" // hud hiding
#include "r_fps.h" // R_GetTimeFrac
#include "r_things.h" // numskins
#include "r_skins.h"

#include "lua_hud.h"
#include "lua_hook.h"

//static int16_t dscredits_idx = 0;

static longtic_t elapsed = 0;
static longtic_t lastelapsed = 0;
static longtic_t elapsed_diff;

static longtic_t blancredits_start = 13335000;
static longtic_t blancredits_end = 186384000;
static longtic_t blancredits_length = (blancredits_end - blancredits_start);
static fixed_t blancredits_dividend = blancredits_length / FRACUNIT;
static fixed_t blancredits_height;

static longtic_t kartcredits_start = 195977000;
static longtic_t kartcredits_end = 294593000;
static longtic_t kartcredits_length = (kartcredits_end - kartcredits_start);
static fixed_t kartcredits_dividend = kartcredits_length / FRACUNIT;
static fixed_t kartcredits_height;

static longtic_t finalescene;
static int32_t finalesecs, finalespeed;

static dboolean dscredits_running = false;

// Title Screen
static patch_t *ttblanoverlay;

// Scenery
#define NUMDSBUILDINGS 6
static patch_t *dsground; // Ground
static patch_t *dsgrass; // Grass
static patch_t *dssky; // Sky
static patch_t *tylergag; // Tyler
static patch_t *dsbuildings[NUMDSBUILDINGS];

std::chrono::high_resolution_clock timer;
std::chrono::high_resolution_clock::time_point start, curr;

#define FULLCREDITSBGCOLOR (254)

#define CREDSKINCOUNT (MAXPLAYERS + DSUNIQUEPLAYERS)

#define MAXMISCOBJS (MAXDSOBJECTS - CREDSKINCOUNT)

#define KARTMOVE (884736)

// 183 pixels in 20.5 seconds
#define RIVALAPPROACH (509542)

#define SPECIALAPPROACH (3396949)

#define FINALESPD (1791317)

#define MICROSECS(x) (x * 1000000)

#define FINALESECDELAY (MICROSECS(1))

// "Overconfident" character's initial speed. Dynamically adapts to screen resolution.
#define TOODAMNFAST(w) ((w + 256) * FRACUNIT)

#define MICROSEC_TICS (MICROSECS(1) / TICRATE)

#define LASTSCENE (21)

// Converts microseconds to fixed-point values.
#define UsecToFixed(x) (static_cast<fixed_t>((static_cast<int64_t>(x / 1000) * FRACUNIT) / 1000))

static fixed_t scroll_slow = (5033164); // 192 pixels in 2.5 seconds
static fixed_t scroll_fast = (12582912); // 192 pixels in 1 second

// Generic scrolling multiplier
// 0 = current, 1 = last, 2 = offset
static fixed_t scene_scroll[3];

static fixed_t tylerscroll;
static longtic_t hityler;

static longtic_t building_delay;
static longtic_t player_delay;

static uint16_t numbuildings;
static uint16_t numplayerobjs;

static dboolean spawnplayers;
static int32_t playersadded;
static int32_t player_iterate;


static uint16_t specialiterate;

static inline int64_t LongFixedMul(fixed_t a, fixed_t b)
{
	// Need to cast to unsigned before shifting to avoid undefined behaviour
	// for negative integers
	return (int64_t)(((uint64_t)((int64_t)a * b)) >> FRACBITS);
}

// Handles interpolating microseconds into a scroll offset.
#define CreditsScrollFx(x,y) (static_cast<fixed_t>(LongFixedMul(UsecToFixed(x), y)))
#define CreditsScroll(x,y) (CreditsScrollFx(x,y) >> FRACBITS)

static uint16_t credits_skins[CREDSKINCOUNT];
static uint16_t credits_colors[CREDSKINCOUNT];

static uint16_t player1_idx, rival_idx, special_idx, specialp, tylermemoriam;

static const char* gamettl = "BLANKART";

dscredits_t dscredits;

extern "C" 
{

// These timings are in microseconds (1 second is 1000000 microseconds)
longtic_t creditstimings[] = {
    // ACT 1 (Kart credits)
    7335000,   // Title screen text fades out (fadeout is 1.5 seconds)
    13335000,  // Credits begin to scroll
    32005000,  // Screen fades into sunset scene (fadein is 2 seconds)

    77350000,  // Characters begin to randomly appear
               // (spawn rate should be betwen 2.5 and 5 seconds)

    134528000, // Stop spawning characters for some time

    160036000, // If they're not an RR character, spawn in Tails.
               // If they ARE an RR character, spawn in one of 3 random rivals.
               // They should drive up for around 20 seconds, before stopping.

    180036000, // Stop the rival here.

    // ACT 2 (Blan credits)
    195977000, // Both players speed up! The HOPE-O-METER is at maximum!
    210489000, // Spawn a random "special" character. These stop in place for a bit.
               // (Drive ahead for 6 seconds, then stop for 4, then drive off).

    218154000, // Spawn another special character. This one acts the same as the previous.

    226260000, // Spawn a third special character. This one's overconfident and ends up spinning out.
               // * Drive ahead for 1 second (really fast!)
               // * Pause offscreen on the left for 2.5 seconds
               // * Spin around all the way back for 2.75 seconds

    234260000, // Back to normalcy, mostly. Spawn a random character.

    239260000, // The "overconfident" one returns, this time properly ready and
               // moving at a decent pace.
               // They lead the pack of randomly spawning racers.
               // Back to normalcy (for real this time)!

    254688000, // Stop spawning characters for some time
    267874000, // The big finish! All racers come in in a line...

    296536000, // ...and they all drive off into the sunset.

    298978000, // Fade the screen.

    300226428, // SONIC ROBO BLAST 2
    301374285, // *vroom* KART

    303311000, // One more fadeout...

    308203000, // (scene 21) This should return you to the title screen.

    INFLONGTICS // Dummy (Let's *not* crash the game...)
};

tic_t dscreditsticks = 0;

static int32_t F_FindFirstFreeObj()
{
    int32_t i = 0;
    for (i = 0; i < MAXMISCOBJS; i++)
    {
        // Found one.
        if (!dscredits.creditsobj[i].active)
        {
            break;
        }
    }

    return i;
}

static void F_SpawnCreditsObject(int32_t id, int32_t x, int32_t y, fixed_t scale, patch_t *patch, int64_t stopat)
{
    dscredits.creditsobj[id].active = true;

    dscredits.creditsobj[id].position[0] = x << FRACBITS;
    dscredits.creditsobj[id].position[1] = y << FRACBITS;
    dscredits.creditsobj[id].scale = scale;

    dscredits.creditsobj[id].patch = patch;

    dscredits.creditsobj[id].movestop = stopat;
}

static int32_t F_FindFirstFreePlayer()
{
    int32_t i = 0;
    for (i = MAXMISCOBJS; i < MAXDSOBJECTS; i++)
    {
        // Found one.
        if (!dscredits.creditsobj[i].active)
        {
            break;
        }
    }

    return i;
}

static void F_SpawnCreditsPlayer(int32_t id, int32_t x, int32_t y, fixed_t scale, uint16_t skin, uint16_t color, int64_t stopat)
{
    dscredits.creditsobj[id].active = true;

    dscredits.creditsobj[id].position[0] = x << FRACBITS;
    dscredits.creditsobj[id].position[1] = y << FRACBITS;
    dscredits.creditsobj[id].scale = scale;

    dscredits.creditsobj[id].skin = skin;
    dscredits.creditsobj[id].color = color;

    dscredits.creditsobj[id].movestop = stopat;
    dscredits.creditsobj[id].movestart = stopat;
}

static fixed_t F_FixedLerp(fixed_t v0, fixed_t v1, fixed_t t)
{
    return FixedMul(v1,t) + FixedMul(FRACUNIT - t, v0);
}

void F_SecretCreditsTicker(void)
{
    uint16_t i;
    int32_t credskins;
    uint16_t pskin, pcol;

    if (dscreditsticks < 1)
    {
        dscredits.creditsscene = 0;
        tylermemoriam = 0;
        hityler = 0;

        building_delay = 0;
        numbuildings = 0;

        player_delay = 0;
        player_iterate = 0;

        specialiterate = 0;
        playersadded = 0;

        finalescene = 0;
        finalespeed = 0;
        finalesecs = -1;

        spawnplayers = false;
        dscredits_running = false;
    }

    if (dscreditsticks == 1)
    {
        // Use the second tic to load in all necessary content.

        // Timer and scene counter.
        elapsed = 0;
        dscredits.creditsscene = 0;

        credskins = CREDSKINCOUNT - 1;

        ttblanoverlay = static_cast<patch_t*>(W_CachePatchName("BKLOGOOV", PU_PATCH_LOWPRIORITY));

        // Scenery patches
        dsground = static_cast<patch_t*>(W_CachePatchName("EXCDGND", PU_PATCH_LOWPRIORITY));
        dsgrass = static_cast<patch_t*>(W_CachePatchName("EXCDGRS", PU_PATCH_LOWPRIORITY));
        dssky = static_cast<patch_t*>(W_CachePatchName("EXCDSKY", PU_PATCH_LOWPRIORITY));
        tylergag = static_cast<patch_t*>(W_CachePatchName("TYLER52", PU_PATCH_LOWPRIORITY));

        const char* patchNames[] = {
			"EXCDBD1", "EXCDBD2", "EXCDBD3", "EXCDBD4", "EXCDBD5", "EXCDBD6"
		};

        for (i = 0; i < NUMDSBUILDINGS; i++)
        {
            dsbuildings[i] = static_cast<patch_t*>(W_CachePatchName(patchNames[i], PU_PATCH_LOWPRIORITY));
        }

        // HUD object array
        for (i = 0; i < MAXDSOBJECTS; i++)
		{
            dscredits.creditsobj[i].active = false;
            dscredits.creditsobj[i].skin = MAXSKINS;
            dscredits.creditsobj[i].scale = FRACUNIT;
        }

        // Allocate skins for the local players.
        for (i = 0; i < (splitscreen + 1); i++)
        {
            pskin = R_SkinAvailable(cv_skin[i].string);

            if (pskin >= MAXSKINS)
            {
                pskin = 0;
            }

            if (!R_SkinUsable(-1, pskin))
            {
                pskin = 0;
            }

            if (i == 0)
            {
                // Player 1 always gets priority.
                player1_idx = credskins;
            }

            pcol = cv_playercolor[i].value;
            credits_skins[credskins] = pskin;
            credits_colors[credskins] = pcol;
            credskins--;
        }

        // Allocate the rival's skin, if possible.
        uint16_t grabskins[MAXSKINS];
        uint16_t rand, rivalskin;

        grabskins[0] = R_SkinAvailable(skins[pskin]->rivals[0]);
        grabskins[1] = R_SkinAvailable(skins[pskin]->rivals[1]);
        grabskins[2] = R_SkinAvailable(skins[pskin]->rivals[2]);

        // Pick a random initial point to find the rival.
        rand = M_RandomKey(SKINRIVALS);

        // Start verifying and looking for the first valid rival.
        for (i = 0; i < 5; i++)
        {
            rivalskin = grabskins[(i + rand) % SKINRIVALS];

            if ((rivalskin >= MAXSKINS)||(!R_SkinUsable(-1, rivalskin)))
            {
                // Not valid (likely -1, or can't be used)
                continue;
            }

            // Got one, I think!
            break;
        }

        if (rivalskin >= MAXSKINS)
        {
            // Couldn't find a rival. Get Tails!
            rivalskin = R_SkinAvailable("tails");

            if ((rivalskin >= MAXSKINS)||(!R_SkinUsable(-1, rivalskin)))
            {
                // ...goddammit.
                rivalskin = 0;
            }
        }

        // Add in the rival.
        rival_idx = credskins;
        credits_skins[credskins--] = rivalskin;

        uint16_t usableskins = 0;
        uint16_t j;

        for (i = 0; i < numskins; i++)
        {
            if (!R_SkinUsable(-1, i))
                continue;

            for (j = 0; j < (splitscreen + 1); j++)
            {
                if (i == credits_skins[(CREDSKINCOUNT - 1) - j])
                {
                    // Already used by a player.
                    continue;
                }
            }

            if (i == rivalskin)
            {
                // Already used by the rival.
                continue;
            }

            grabskins[usableskins++] = i;
        }

        if (!usableskins)
        {
            I_Error("F_SecretCreditsTicker: No valid skins to pick from!?");
        }

        // Pick a random initial point to load in the usable skins
        rand = M_RandomKey(usableskins);

        // Pre-allocate the "special character" index. This can be random.
        special_idx = credskins;

        while(credskins >= 0)
        {
            // Allocate the remaining skins from the skin list.
            credits_skins[credskins] = grabskins[((rand + credskins) % usableskins)];
            credits_colors[credskins] = skins[credits_skins[credskins]]->prefcolor;
            credskins--;

            if (credskins < 0)
            {
                // Break if we're out of range!
                break;
            }
        }

        // Finally, get the height of the credits scroll. Consistent timing is nice. :)
        kartcredits_height = 0;
        blancredits_height = 0;

        for (i = 0; credits[i]; i++)
        {
            switch(credits[i][0])
            {
            case 0:
                kartcredits_height += 50<<FRACBITS;
                break;
            case 1:
                kartcredits_height += 16<<FRACBITS;
                break;
            default:
                kartcredits_height += 20<<FRACBITS;
                break;
            }
        }

        for (i = 0; blancredits[i]; i++)
        {
            switch(blancredits[i][0])
            {
            case 0:
                blancredits_height += 50<<FRACBITS;
                break;
            case 1:
                blancredits_height += 16<<FRACBITS;
                break;
            default:
                blancredits_height += 20<<FRACBITS;
                break;
            }
        }
    }

    dscreditsticks++;

    int32_t w = vid.scaledwidth;
    int32_t h = (vid.height / vid.dup);

    const int32_t vidxdiff = (w - BASEVIDWIDTH) / 2;
    const int32_t vidydiff = (h - BASEVIDHEIGHT) / 2;

    if (dscreditsticks == CREDITSDELAY)
    {
        // Roll credits!
        dscredits_running = true;

        F_SpawnCreditsPlayer(player1_idx + MAXMISCOBJS,
                             (-vidxdiff) + w - 77,
                             (-vidydiff) + h - (LSBF_SHORT(dsground->height) >> 1) - 1,
                             FRACUNIT >> 1,
                             credits_skins[player1_idx],
                             credits_colors[player1_idx],
                             INFLONGTICS);

        start = timer.now();

        S_ChangeMusicInternal("BLNCDS", false);
	    S_ShowMusicCredit(0, 5*TICRATE, 0);
    }

    if (!dscredits_running)
    {
        return;
    }

    scene_scroll[1] = scene_scroll[0];

    curr = timer.now();
    const std::chrono::duration<double> fp_ms = curr - start;

    // This is fucking stupid.
    lastelapsed = elapsed;
    elapsed = static_cast<longtic_t>(fp_ms.count() * 1000000);

    if ((tylermemoriam) && (!hityler))
    {
        hityler = elapsed;
    }

    if (elapsed > creditstimings[dscredits.creditsscene])
    {
        // Increment the scene count by 1.
        dscredits.creditsscene++;
    }

    fixed_t lerptime = std::max(0,
                                std::min(static_cast<fixed_t>(FRACUNIT),
                                         static_cast<fixed_t>(elapsed - creditstimings[7]) / 8));

    fixed_t scroll_mul = scroll_slow;
    elapsed_diff = elapsed - lastelapsed;

    if (dscredits.creditsscene > 1)
    {
        scroll_mul = F_FixedLerp(scroll_slow,
                                 scroll_fast,
                                 lerptime);

        if ((elapsed > creditstimings[2]) && (dscredits.creditsscene < 16))
        {
            scene_scroll[0] += CreditsScroll((elapsed_diff), scroll_mul);
        }
        else if (dscredits.creditsscene >= 16)
        {
            finalespeed = CreditsScrollFx((elapsed_diff), scroll_mul) * -1;

            if (specialiterate == 7)
            {
                for (i = MAXMISCOBJS; i < MAXMISCOBJS + MAXPLAYERS; i++)
                {
                    if (!dscredits.creditsobj[i].active)
                    {
                        continue;
                    }

                    dscredits.creditsobj[i].speed[0] = -scroll_mul;
                }
                specialiterate++;

                dscredits.creditsobj[rival_idx + MAXMISCOBJS].speed[0] = -scroll_mul;
            }
            
            dscredits.creditsobj[player1_idx + MAXMISCOBJS].position[0] += finalespeed;
        }

        scene_scroll[2] = (scene_scroll[0] - scene_scroll[1]);

        if (hityler)
        {
            tylerscroll = CreditsScroll((elapsed - hityler), scroll_mul);
        }
    }

    if (dscredits.creditsscene > 2)
    {
        int16_t idx;

        numbuildings = 0;
        numplayerobjs = 0;

        building_delay = static_cast<longtic_t>(std::max(static_cast<int64_t>(0), static_cast<int64_t>(building_delay) - static_cast<int64_t>(elapsed_diff)));

        if (!building_delay)
        {
            idx = F_FindFirstFreeObj();

            patch_t *bld = dsbuildings[M_RandomKey(NUMDSBUILDINGS)];

            F_SpawnCreditsObject(idx,
                                 (-vidxdiff) - LSBF_SHORT(bld->width),
                                 (-vidydiff) + h - (LSBF_SHORT(dsground->height) >> 1) - LSBF_SHORT(bld->height),
                                 FRACUNIT,
                                 bld,
                                 INFLONGTICS);

            // 5 or 9 seconds, random delay
            building_delay = static_cast<longtic_t>(M_RandomRange(5, 9)) *
                F_FixedLerp(1000000,400000,lerptime);
        }

        // Dynamically do this for consistent timing.
        fixed_t vidwidthmul = 205 * w;
        fixed_t approach = FixedMul(SPECIALAPPROACH, vidwidthmul);

        if (spawnplayers)
        {
            player_delay = static_cast<longtic_t>(std::max(static_cast<int64_t>(0), static_cast<int64_t>(player_delay) - static_cast<int64_t>(elapsed_diff)));

            if (!player_delay)
            {
                idx = F_FindFirstFreePlayer();

                F_SpawnCreditsPlayer(idx,
                                    (-vidxdiff) + w + 128,
                                    (-vidydiff) + h - (LSBF_SHORT(dsground->height) >> 1) - 1,
                                    FRACUNIT >> 1,
                                    credits_skins[player_iterate],
                                    credits_colors[player_iterate],
                                    INFLONGTICS);

                dscredits.creditsobj[idx].speed[0] = -KARTMOVE;

                // 8 or 12 seconds, random delay
                if (dscredits.creditsscene >= 12)
                {
                    dscredits.creditsobj[idx].speed[0] =
                        (dscredits.creditsobj[idx].speed[0] * 9) / 2;
                    player_delay = static_cast<longtic_t>(M_RandomRange(8, 12)) * 250000;
                }
                else
                {
                    player_delay = static_cast<longtic_t>(M_RandomRange(8, 12)) * 1000000;
                }

                playersadded++;
                player_iterate++;
                player_iterate %= MAXPLAYERS;
            }
        }

        // Set up players for the second set of spawns.
        if (dscredits.creditsscene == 12)
        {
            player_delay = static_cast<longtic_t>(M_RandomRange(8, 12)) * 250000;
        }

        if ((dscredits.creditsscene == 4) || (dscredits.creditsscene == 13))
        {
            spawnplayers = true;
        }
        else
        {
            spawnplayers = false;
        }

        // Rival handling.
        if (dscredits.creditsscene == 6)
        {
            if (!specialiterate)
            {
                F_SpawnCreditsPlayer(rival_idx + MAXMISCOBJS,
                                    (-vidxdiff) + w + 128,
                                    (-vidydiff) + h - (LSBF_SHORT(dsground->height) >> 1) - 1,
                                    FRACUNIT >> 1,
                                    credits_skins[rival_idx],
                                    skins[credits_skins[rival_idx]]->prefcolor,
                                    INFLONGTICS);

                dscredits.creditsobj[rival_idx + MAXMISCOBJS].speed[0] = -RIVALAPPROACH;
                dscredits.creditsobj[rival_idx + MAXMISCOBJS].movestop = (40 * 1000000);
                specialiterate++;
            }
        }

        if (dscredits.creditsobj[rival_idx + MAXMISCOBJS].active)
        {
            if (dscredits.creditsobj[rival_idx + MAXMISCOBJS].movestop > 0)
            {
                dscredits.creditsobj[rival_idx + MAXMISCOBJS].movestop = std::max(
                        static_cast<int64_t>(0),
                        static_cast<int64_t>(dscredits.creditsobj[rival_idx + MAXMISCOBJS].movestop) -
                            static_cast<int64_t>(elapsed_diff));

                if (!dscredits.creditsobj[rival_idx + MAXMISCOBJS].movestop)
                {
                    dscredits.creditsobj[rival_idx + MAXMISCOBJS].speed[0] = 0;
                    dscredits.creditsobj[rival_idx + MAXMISCOBJS].movestop = -1;
                }
            }

            // Apply movements.
            if (dscredits.creditsscene == 8)
            {
                // Scrolls with the FG
                dscredits.creditsobj[rival_idx + MAXMISCOBJS].position[0] += (scene_scroll[2] << FRACBITS);
            }

            // Moves by speed.
            dscredits.creditsobj[rival_idx + MAXMISCOBJS].position[0] += CreditsScrollFx(
                (elapsed_diff), dscredits.creditsobj[rival_idx + MAXMISCOBJS].speed[0]);
            dscredits.creditsobj[rival_idx + MAXMISCOBJS].position[1] += CreditsScrollFx(
                (elapsed_diff), dscredits.creditsobj[rival_idx + MAXMISCOBJS].speed[1]);

            // Clamp to "hide" the rival at points
            fixed_t xpos = (dscredits.creditsobj[rival_idx + MAXMISCOBJS].position[0] >> FRACBITS);
            fixed_t max_xpos = (-vidxdiff + w + 128);
            fixed_t min_xpos = (-vidxdiff - 128);


            if (xpos < min_xpos)
            {
                dscredits.creditsobj[rival_idx + MAXMISCOBJS].position[0] = (min_xpos << FRACBITS);
            }
            else if (xpos > max_xpos)
            {
                dscredits.creditsobj[rival_idx + MAXMISCOBJS].position[0] = (max_xpos << FRACBITS);
            }
        }

        // Special characters.
        if (((dscredits.creditsscene == 9) && (specialiterate < 2)) ||
            ((dscredits.creditsscene == 10) && (specialiterate < 3)))
        {
            idx = F_FindFirstFreePlayer();
            F_SpawnCreditsPlayer(idx,
                                 (-vidxdiff) + w + 128,
                                 (-vidydiff) + h - (LSBF_SHORT(dsground->height) >> 1) - 1,
                                 FRACUNIT >> 1,
                                 credits_skins[player_iterate],
                                 credits_colors[player_iterate],
                                 INFLONGTICS);

            dscredits.creditsobj[idx].speed[0] = -approach;
            dscredits.creditsobj[idx].movestop = (6 * 1000000);

            dscredits.creditsobj[idx].movestart = (8 * 1000000);

            
            dscredits.creditsobj[idx].movestartspd[0] = -(approach * 2);
            specialiterate++;

            player_iterate++;
            player_iterate %= MAXPLAYERS;

            playersadded++;
        }
        else if ((dscredits.creditsscene == 11) && (specialiterate < 4))
        {
            idx = F_FindFirstFreePlayer();

            F_SpawnCreditsPlayer(idx,
                                 (-vidxdiff) + w + 128,
                                 (-vidydiff) + h - (LSBF_SHORT(dsground->height) >> 1) - 1,
                                 FRACUNIT >> 1,
                                 credits_skins[special_idx],
                                 credits_colors[special_idx],
                                 INFLONGTICS);

            dscredits.creditsobj[idx].speed[0] = -TOODAMNFAST(w);
            dscredits.creditsobj[idx].movestop = (1150000);

            dscredits.creditsobj[idx].movestart = (3500000);
            dscredits.creditsobj[idx].movestartspd[0] = scroll_mul;

            specialp = idx;

            specialiterate++;
        }

        // Miscellaneous interactions.
        if ((dscredits.creditsobj[specialp].movestop <= 0) && (specialiterate == 4))
        {
            dscredits.creditsobj[specialp].spinout = true;
            specialiterate++;
        }

        if ((dscredits.creditsscene == 12) && (specialiterate == 5))
        {
            dscredits.creditsobj[rival_idx + MAXMISCOBJS].speed[0] = -((KARTMOVE * 9) / 2);
            specialiterate++;
        }

        if ((dscredits.creditsscene == 13) && (specialiterate == 6))
        {
            dscredits.creditsobj[specialp].spinout = false;
            dscredits.creditsobj[specialp].speed[0] = -((KARTMOVE * 9) / 2);
            specialiterate++;
        }

        // Finale!
        if (dscredits.creditsscene == 15)
        {
            if (!finalescene)
            {
                finalescene = elapsed;
            }

            if ((((elapsed - finalescene) / FINALESECDELAY) != (longtic_t)finalesecs))
            {
                finalesecs = ((elapsed - finalescene) / FINALESECDELAY);

                if (playersadded > 0)
                {
                    playersadded--;
                    player_iterate--;

                    while (player_iterate < 0)
                    {
                        player_iterate += MAXPLAYERS;
                    }

                    idx = F_FindFirstFreePlayer();

                    F_SpawnCreditsPlayer(idx,
                                        (-vidxdiff) - 128,
                                        (-vidydiff) + h - (LSBF_SHORT(dsground->height) >> 1) - 1,
                                        FRACUNIT >> 1,
                                        credits_skins[player_iterate],
                                        credits_colors[player_iterate],
                                        INFLONGTICS);

                    dscredits.creditsobj[idx].speed[0] = FixedMul(vidwidthmul,FINALESPD);
                }
                else
                {
                    if (playersadded > -1)
                    {
                        playersadded--;

                        dscredits.creditsobj[specialp].speed[0] = FixedMul(vidwidthmul,FINALESPD);
                    }
                    else if (playersadded > -2)
                    {
                        playersadded--;

                        dscredits.creditsobj[rival_idx + MAXMISCOBJS].speed[0] = FixedMul(vidwidthmul,FINALESPD);
                    }
                }
            }
        }

        // Handle scenery.
        for (i = 0; i < MAXMISCOBJS; i++)
        {
            if (!dscredits.creditsobj[i].active)
            {
                continue;
            }

            numbuildings++;

            // Scroll along the background.
            dscredits.creditsobj[i].position[0] += ((scene_scroll[2] << FRACBITS) >> 1);

            if ((dscredits.creditsobj[i].position[0] >> FRACBITS) > ((-vidxdiff + w) + 48))
            {
                // Offscreen, unload ourselves.
                dscredits.creditsobj[i].active = false;
                continue;
            }
        }

        // Handle players. Unique objects are handled on a case-by-case basis.
        for (i = MAXMISCOBJS; i < MAXMISCOBJS + MAXPLAYERS; i++)
        {
            if (!dscredits.creditsobj[i].active)
            {
                continue;
            }

            numplayerobjs++;

            if (dscredits.creditsobj[i].movestop > 0)
            {
                dscredits.creditsobj[i].movestop = std::max(
                        static_cast<int64_t>(0),
                        static_cast<int64_t>(dscredits.creditsobj[i].movestop) -
                            static_cast<int64_t>(elapsed_diff));

                if (!dscredits.creditsobj[i].movestop)
                {
                    dscredits.creditsobj[i].speed[0] = 0;
                    dscredits.creditsobj[i].movestop = -1;
                }
            }

            if (dscredits.creditsobj[i].movestart > 0)
            {
                dscredits.creditsobj[i].movestart = std::max(
                        static_cast<int64_t>(0),
                        static_cast<int64_t>(dscredits.creditsobj[i].movestart) -
                            static_cast<int64_t>(elapsed_diff));

                if (!dscredits.creditsobj[i].movestart)
                {
                    dscredits.creditsobj[i].speed[0] = dscredits.creditsobj[i].movestartspd[0];
                    dscredits.creditsobj[i].movestart = -1;
                }
            }

            // Apply movements.
            dscredits.creditsobj[i].position[0] += CreditsScrollFx(
                (elapsed_diff), dscredits.creditsobj[i].speed[0]);
            dscredits.creditsobj[i].position[1] += CreditsScrollFx(
                (elapsed_diff), dscredits.creditsobj[i].speed[1]);

            if (dscredits.creditsscene < 15) // For the finale, no offscreen handling allowed!
            {
                if (((dscredits.creditsobj[i].position[0] >> FRACBITS) < (-vidxdiff - 128)) &&
                    (i != specialp)) // The special player needs to be handled differently.
                {
                    // Offscreen, unload ourselves.
                    dscredits.creditsobj[i].active = false;
                    continue;
                }
            }

            if (i == specialp)
            {
                // Handle offscreening VERY differently
                fixed_t xpos = (dscredits.creditsobj[i].position[0] >> FRACBITS);
                fixed_t max_xpos = (-vidxdiff + w + 128);
                fixed_t min_xpos = (-vidxdiff - 128);

                if (xpos < min_xpos)
                {
                    dscredits.creditsobj[i].position[0] = (min_xpos << FRACBITS);
                }
                else if ((xpos > max_xpos) && (dscredits.creditsscene < 15))
                {
                    dscredits.creditsobj[i].position[0] = (max_xpos << FRACBITS);
                }
            }
        }
    }

    // After ALL THAT, check if the credits are finished.
    if (dscredits.creditsscene >= LASTSCENE)
    {
        sawfullcredits = true;
        F_StartGameEvaluation();
    }
    
    /*if (dscredits.creditsscene > 1)
    {
        // "Simulate" the drawing of the credits so that dedicated mode doesn't get stuck
        fixed_t y = (80<<FRACBITS) - 5*(animtimer<<FRACBITS)/8;

        // Draw credits text on top
        for (i = 0; credits[i]; i++)
        {
            switch(credits[i][0])
            {
                case 0: y += 80<<FRACBITS; break;
                case 1: y += 30<<FRACBITS; break;
                default: y += 12<<FRACBITS; break;
            }
            if (FixedMul(y,vid.dup) > vid.height)
                break;
        }
    }

	// Do this here rather than in the drawer you doofus! (this is why dedicated mode broke at credits)
	if (!credits[i] && y <= 120<<FRACBITS && !finalecount)
	{
		timetonext = 5*TICRATE+1;
		finalecount = 5*TICRATE;
	}

	if (timetonext)
		timetonext--;
	else
		animtimer++;

	if (finalecount && --finalecount == 0)
		F_StartGameEvaluation();*/
}

#define sign(x) ((x < 0) ? -1 : ((x > 0) ? 1 : 0))
#define absmod(x,y) ((abs(x) % y) * sign(x))

// Repeats horizontally until it reaches across the screen.
static void F_DrawBannerPatch(fixed_t x, fixed_t y, fixed_t scale, int32_t scrn, patch_t *patch, const uint8_t *colormap)
{
    fixed_t w = vid.scaledwidth;
    fixed_t vidxdiff = ((w - BASEVIDWIDTH) << FRACBITS) / 2;
    fixed_t pw = (static_cast<fixed_t>(LSBF_SHORT(patch->width)) * scale);

    fixed_t xx = ((-vidxdiff + absmod(x * FRACUNIT, pw)) - (pw << 1));
    
    while(xx < (w << FRACBITS))
    {
        V_DrawFixedPatch(xx, y<<FRACBITS, scale, scrn, patch, colormap);
        xx += pw;
    }
}

#undef sign

// returns false if the character couldn't be rendered
static dboolean M_DrawCharacterSprite(int16_t x, int16_t y, fixed_t scale, uint8_t fade, int16_t skin, uint8_t spr2, uint8_t rotation, uint32_t frame, int32_t addflags, uint8_t *colormap)
{
	uint8_t spr;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	patch_t *sprpatch;

	spr = P_GetSkinSprite2(skins[skin], spr2, NULL);
	sprdef = &skins[skin]->sprites[spr];

    uint8_t *fademap = R_GetTranslationColormap(TC_BLINK, SKINCOLOR_BLACK, GTC_MENUCACHE);
    int16_t fadealpha = 10 - fade;

	if (!sprdef->numframes) // No frames ??
		return false; // Can't render!

	frame %= sprdef->numframes;

	sprframe = &sprdef->spriteframes[frame];
	sprpatch = static_cast<patch_t*>(W_CachePatchNum(sprframe->lumppat[rotation], PU_CACHE));

	if (sprframe->flip & (1<<rotation)) // Only for first sprite
	{
		addflags ^= V_FLIP; // This sprite is left/right flipped!
	}

	if ((skins[skin]->highresscale != FRACUNIT)||(scale != FRACUNIT))
	{
		V_DrawFixedPatch(x<<FRACBITS,
					y<<FRACBITS,
					FixedMul(scale, skins[skin]->highresscale),
					addflags, sprpatch, colormap);

        if (fadealpha < 10)
        {
            V_DrawFixedPatch(x<<FRACBITS,
					y<<FRACBITS,
					FixedMul(scale, skins[skin]->highresscale),
					addflags|(fadealpha << V_ALPHASHIFT), sprpatch, fademap);
        }
	}
	else
    {
		V_DrawMappedPatch(x, y, addflags, sprpatch, colormap);

        if (fadealpha < 10)
        {
            V_DrawMappedPatch(x, y, addflags|(fadealpha << V_ALPHASHIFT), sprpatch, fademap);
        }
    }

	return true;
}


static void F_DrawCreditsObjects()
{
    int32_t ticker = FixedMul(TICRATE, UsecToFixed(elapsed));

    int32_t i;
    for (i = 0; i < MAXDSOBJECTS + DSUNIQUEPLAYERS; i++)
    {
        if (!dscredits.creditsobj[i].active)
        {
            continue;
        }

        if (dscredits.creditsobj[i].skin != MAXSKINS)
        {
            // Player object.
            //char debug[256];
            uint8_t *colormap = R_GetTranslationColormap(dscredits.creditsobj[i].skin,
                                                       static_cast<skincolornum_t>(dscredits.creditsobj[i].color),
                                                       GTC_MENUCACHE);

            /*snprintf(debug, sizeof(debug),
                "xspd: %f\nstopat: %d\nspinout: %d\n",
                FIXED_TO_FLOAT(CreditsScrollFx((elapsed_diff), dscredits.creditsobj[i].speed[0])),
                dscredits.creditsobj[i].movestop,
                static_cast<uint8_t>(dscredits.creditsobj[i].spinout));*/

            M_DrawCharacterSprite(dscredits.creditsobj[i].position[0] >> FRACBITS,
                                  dscredits.creditsobj[i].position[1] >> FRACBITS,
                                  dscredits.creditsobj[i].scale,
                                  4,
                                  dscredits.creditsobj[i].skin,
                                  dscredits.creditsobj[i].spinout ? SPR2_SPIN : SPR2_FSTN,
                                  dscredits.creditsobj[i].spinout ? (((ticker * 5)/8) & 7) : 2,
                                  ticker + i,
                                  0,
                                  colormap);


            /*V_DrawThinString((dscredits.creditsobj[i].position[0] >> FRACBITS) - 128,
                             (dscredits.creditsobj[i].position[1] >> FRACBITS) - 96,
                             V_ALLOWLOWERCASE,
                             debug);*/
        }
        else if (dscredits.creditsobj[i].patch)
        {
            V_DrawFixedPatch((dscredits.creditsobj[i].position[0] >> FRACBITS) << FRACBITS,
                             (dscredits.creditsobj[i].position[1] >> FRACBITS) << FRACBITS,
                             dscredits.creditsobj[i].scale,
                             0, dscredits.creditsobj[i].patch, NULL);
        }
    }
}

static void F_DrawCreditsArea(fixed_t y_root, fixed_t height, fixed_t xdiff)
{
    fixed_t y, sky_y, ground_y;

    y = y_root;

    // Store the ground area
    y -= static_cast<fixed_t>(LSBF_SHORT(dsground->height) >> 1);
    ground_y = sky_y = y;

    sky_y -= static_cast<fixed_t>(LSBF_SHORT(dssky->height) >> 1);

    // Draw the sky
    F_DrawBannerPatch(scene_scroll[0] >> 1, sky_y, FRACUNIT >> 1, 0, dssky, NULL);

    // Draw Tyler
    if (tylermemoriam)
    {
        V_DrawPatch(-xdiff - LSBF_SHORT(tylergag->width) + ((tylerscroll * 3) >> 1), ground_y - (height >> 1), V_TRANSLUCENT, tylergag);
    }

    // Draw the game objects (buildings, players)
    F_DrawCreditsObjects();

    // Draw the ground and grass
    F_DrawBannerPatch(scene_scroll[0], ground_y, FRACUNIT >> 1, 0, dsground, NULL);

    ground_y -= static_cast<fixed_t>(LSBF_SHORT(dsgrass->height) >> 1);

    F_DrawBannerPatch(scene_scroll[0], ground_y, FRACUNIT >> 1, 0, dsgrass, NULL);
}

// Not really "fake" considering this is the actual title sequence.
static void F_FakeTitleScreen(tic_t count)
{
    int32_t transval = CLAMP((count - 50)>>1, 0, 10);
    int32_t alphaflags = (transval<<V_ALPHASHIFT);

    if (transval && (10 - transval))
    {
        V_DrawFadeScreen(0, 10 - transval);
    }
    
    F_DrawCompositeGameLogo(count - 25, 48);
    if (alphaflags)
    {
        V_DrawFixedPatch(30<<FRACBITS, 64<<FRACBITS, FRACUNIT, V_SNAPTOTOP|V_ADD|alphaflags, ttblanoverlay, NULL);
    }
}

void F_SecretCreditsDrawer(void)
{
    int32_t alpha = 0, titlealpha = 0, titlefade = 0;
    int32_t w, h;
    uint8_t bgcolor = 31;

    w = vid.scaledwidth;
    h = (vid.height / vid.dup);

    const int32_t vidxdiff = (w - BASEVIDWIDTH) / 2;
    const int32_t vidydiff = (h - BASEVIDHEIGHT) / 2;

    if (!dscredits_running)
    {
        V_DrawFill(-vidxdiff - 2, -vidydiff - 2, w + 2, h + 2, bgcolor);
        return;
    }

    if ((dscredits.creditsscene > 2) && (dscredits.creditsscene < 19))
    {
        bgcolor = FULLCREDITSBGCOLOR;
    }

    V_DrawFill(-vidxdiff - 2, -vidydiff - 2, w + 2, h + 2, bgcolor);

    if (dscredits.creditsscene < 2)
    {
        V_DrawCenteredStringAtFixed(160<<FRACBITS, 100<<FRACBITS, 0, gamettl);
    }

    if (dscredits.creditsscene == 1)
    {
        alpha = std::min(static_cast<longtic_t>(10), std::max(static_cast<longtic_t>(0), elapsed - creditstimings[0]) / 150000); // 1.5 seconds for fadeout 
    }
    else if (dscredits.creditsscene == 3)
    {
        alpha = 10 - std::min(static_cast<longtic_t>(10), std::max(static_cast<longtic_t>(0), elapsed - creditstimings[2]) / 200000); // 2.0 seconds for fadein 
    }
    else if (dscredits.creditsscene >= 17)
    {
        alpha = std::min(static_cast<longtic_t>(10), std::max(static_cast<longtic_t>(0), elapsed - creditstimings[16]) / 200000); // 2.0 seconds for fadeout
        titlealpha = std::min(static_cast<longtic_t>(10), std::max(static_cast<longtic_t>(0), elapsed - creditstimings[16]) / 100000); // 1 second for banner fadein
    }

    if (dscredits.creditsscene >= 20)
    {
        titlefade = std::min(static_cast<longtic_t>(10), std::max(static_cast<longtic_t>(0), elapsed - creditstimings[19]) / 400000); // 4.0 seconds for fadeout 
    }
    
    if ((dscredits.creditsscene > 2) && (dscredits.creditsscene < 19))
    {
        F_DrawCreditsArea(static_cast<fixed_t>(-vidydiff + h), static_cast<fixed_t>(h), static_cast<fixed_t>(vidxdiff));
    }

    if (alpha)
    {
        V_DrawFill(-vidxdiff - 2, -vidydiff - 2, w + 2, h + 2, ((10 - alpha) << V_ALPHASHIFT)|31);
    }

    if (dscredits.creditsscene >= 18)
    {
        tic_t fakefinalecount = static_cast<tic_t>(std::max(static_cast<longtic_t>(0),
                                                            elapsed - creditstimings[17]) /
                                                            MICROSEC_TICS);

        F_FakeTitleScreen(fakefinalecount);

        if (titlefade)
        {
            V_DrawFill(-vidxdiff - 2, -vidydiff - 2, w + 2, h + 2, ((10 - titlefade) << V_ALPHASHIFT)|31);
        }
    }

    int32_t y, i;
    // The credits text overlays everything.
    if ((dscredits.creditsscene > 1) && (dscredits.creditsscene < 8))
    {
        y = ((BASEVIDHEIGHT + vidydiff) << FRACBITS) -
            FixedMul(blancredits_height + (h << FRACBITS), std::min(std::max(blancredits_start, elapsed) - blancredits_start, blancredits_end) / blancredits_dividend);

        // Draw credits text on top
        for (i = 0; blancredits[i]; i++)
        {
            switch(blancredits[i][0])
            {
            case 0:
                y += 50<<FRACBITS;
                break;
            case 1:
                if (y>>FRACBITS > -(10 + vidydiff))
                    V_DrawString(8 - vidxdiff, y >> FRACBITS, V_ORANGEMAP, &blancredits[i][1]);
                y += 16<<FRACBITS;
                break;
            default:
                if (y>>FRACBITS > -(10 + vidydiff))
                    V_DrawRightAlignedThinString(248 - vidxdiff, y >> FRACBITS, V_ALLOWLOWERCASE, blancredits[i]);
                y += 20<<FRACBITS;
                break;
            }
            if (((y>>FRACBITS) * vid.dup) > vid.height)
                break;
        }
    }
    else if (dscredits.creditsscene > 7)
    {
        y = ((BASEVIDHEIGHT + vidydiff) << FRACBITS) -
            FixedMul(kartcredits_height + (h << FRACBITS), std::min(std::max(kartcredits_start, elapsed) - kartcredits_start, kartcredits_end) / kartcredits_dividend);

        // Draw credits text on top
        for (i = 0; credits[i]; i++)
        {
            switch(credits[i][0])
            {
            case 0:
                y += 50<<FRACBITS;
                break;
            case 1:
                if (y>>FRACBITS > -(10 + vidydiff))
                    V_DrawString(8 - vidxdiff, y >> FRACBITS, V_ORANGEMAP, &credits[i][1]);
                y += 16<<FRACBITS;
                break;
            case 2:
                // Tyler spawn
                tylermemoriam = 1;
                if (y>>FRACBITS > -(10 + vidydiff))
                    V_DrawRightAlignedThinString(248 - vidxdiff, y >> FRACBITS, V_ALLOWLOWERCASE, &credits[i][1]);
                y += 20<<FRACBITS;
                break;
            default:
                if (y>>FRACBITS > -(10 + vidydiff))
                    V_DrawRightAlignedThinString(248 - vidxdiff, y >> FRACBITS, V_ALLOWLOWERCASE, credits[i]);
                y += 20<<FRACBITS;
                break;
            }
            if (((y>>FRACBITS) * vid.dup) > vid.height)
                break;
        }
    }

    /*char debug[256];

    snprintf(debug, sizeof(debug), "Tics: %d\n",static_cast<tic_t>(elapsed / MICROSEC_TICS));

    V_DrawRightAlignedThinString(-vidxdiff + w, -vidydiff, V_ALLOWLOWERCASE, debug);*/
}

#undef CREDSKINCOUNT

}
