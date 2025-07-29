/// \file  k_collide.c
/// \brief SRB2Kart item collision hooks

#include "k_collide.h"
#include "k_stats.h"
#include "doomstat.h"
#include "doomtype.h"
#include "p_mobj.h"
#include "k_kart.h"
#include "p_local.h"
#include "s_sound.h"
#include "r_main.h" // R_PointToAngle2, R_PointToDist2
#include "hu_stuff.h" // Sink snipe print
#include "doomdef.h" // Sink snipe print
#include "g_game.h" // Sink snipe print

angle_t K_GetCollideAngle(mobj_t *t1, mobj_t *t2)
{
	fixed_t momux, momuy;
	angle_t test;

	if (!(t1->flags & MF_PAPERCOLLISION))
	{
		return R_PointToAngle2(t1->x, t1->y, t2->x, t2->y)+ANGLE_90;
	}

	test = R_PointToAngle2(0, 0, t2->momx, t2->momy) + ANGLE_90 - t1->angle;
	if (test > ANGLE_180)
		test = t1->angle + ANGLE_180;
	else
		test = t1->angle;

	// intentional way around - sine...
	momuy = P_AproxDistance(t2->momx, t2->momy);
	momux = t2->momx - P_ReturnThrustY(t2, test, 2*momuy);
	momuy = t2->momy - P_ReturnThrustX(t2, test, 2*momuy);

	return R_PointToAngle2(0, 0, momux, momuy);
}

boolean K_OrbinautJawzCollide(mobj_t *t1, mobj_t *t2)
{
	boolean damageitem = false;
	boolean sprung = false;

	if (((t1->target == t2) || (!(t2->flags & (MF_ENEMY|MF_BOSS)) && (t1->target == t2->target))) && ((t1->threshold > 0 && t2->type == MT_PLAYER) || (t2->type != MT_PLAYER && t2->threshold > 0)))
		return true;

	if (t1->health <= 0 || t2->health <= 0)
		return true;

	if ((t1->type == MT_ORBINAUT_SHIELD || t1->type == MT_JAWZ_SHIELD) && t1->lastlook
		&& (t2->type == MT_ORBINAUT_SHIELD || t2->type == MT_JAWZ_SHIELD) && t2->lastlook
		&& (t1->target == t2->target)) // Don't hit each other if you have the same target
		return true;

	if (t2->player)
	{

		if (t2->player->hyudorotimer)
			return true; // no interaction

		if (t2->player->flamestore && (K_GetShieldFromPlayer(t2->player) == KITEM_FLAMESHIELD))
		{
			// Melt item
			S_StartSound(t2, sfx_s3k43);
		}
		else
		{
			// Player Damage
			P_DamageMobj(t2, t1, t1->target, 1, DMG_WIPEOUT);
			K_KartBouncing(t2, t1, false, false);
			S_StartSound(t2, sfx_s3k7b);
		}

		damageitem = true;
	}
	else if (t2->type == MT_ORBINAUT || t2->type == MT_JAWZ || t2->type == MT_JAWZ_DUD
		|| t2->type == MT_ORBINAUT_SHIELD || t2->type == MT_JAWZ_SHIELD
		|| t2->type == MT_BANANA || t2->type == MT_BANANA_SHIELD
		|| t2->type == MT_BALLHOG)
	{
		// Other Item Damage
		angle_t bounceangle = K_GetCollideAngle(t1, t2);

		S_StartSound(t2, t2->info->deathsound);
		P_KillMobj(t2, t1, t1, DMG_NORMAL);

		P_SetObjectMomZ(t2, 8*FRACUNIT, false);
		P_InstaThrust(t2, bounceangle, 16*FRACUNIT);

		P_SpawnMobj(t2->x/2 + t1->x/2, t2->y/2 + t1->y/2, t2->z/2 + t1->z/2, MT_ITEMCLASH);

		damageitem = true;
	}
	else if (t2->type == MT_SSMINE_SHIELD || t2->type == MT_SSMINE || t2->type == MT_LANDMINE)
	{
		damageitem = true;
		// Bomb death
		P_KillMobj(t2, t1, t1, DMG_NORMAL);
	}
	else if (t2->flags & MF_SPRING && (t1->type != MT_ORBINAUT_SHIELD && t1->type != MT_JAWZ_SHIELD))
	{
		// Let thrown items hit springs!
		sprung = P_DoSpring(t2, t1);
	}
	else if (t2->flags & MF_SHOOTABLE)
	{
		// Shootable damage
		P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);
		damageitem = true;
	}

	if (damageitem)
	{
		// This Item Damage
		angle_t bounceangle = K_GetCollideAngle(t2, t1);
		S_StartSound(t1, t1->info->deathsound);
		P_KillMobj(t1, t2, t2, DMG_NORMAL);

		P_SetObjectMomZ(t1, 8*FRACUNIT, false);
		P_InstaThrust(t1, bounceangle, 16*FRACUNIT);
	}

	if (sprung)
	{
		return false;
	}

	return true;
}

boolean K_BananaBallhogCollide(mobj_t *t1, mobj_t *t2)
{
	boolean damageitem = false;

	if (((t1->target == t2) || (!(t2->flags & (MF_ENEMY|MF_BOSS)) && (t1->target == t2->target))) && (t1->threshold > 0 || (t2->type != MT_PLAYER && t2->threshold > 0)))
		return true;

	if (t1->health <= 0 || t2->health <= 0)
		return true;

	if (((t1->type == MT_BANANA_SHIELD) && (t2->type == MT_BANANA_SHIELD))
		&& (t1->target == t2->target)) // Don't hit each other if you have the same target
		return true;

	if (t1->type == MT_BALLHOG && t2->type == MT_BALLHOG)
		return true; // Ballhogs don't collide with eachother

	if (t2->player)
	{

		// Banana snipe!
		if (t1->type == MT_BANANA && t1->health > 1)
			S_StartSound(t2, sfx_bsnipe);

		damageitem = true;

		if (t2->player->flamestore && (K_GetShieldFromPlayer(t2->player) == KITEM_FLAMESHIELD))
		{
			// Melt item
			S_StartSound(t2, sfx_s3k43);
		}
		else
		{
			P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);
		}
	}
	else if (t2->type == MT_BANANA || t2->type == MT_BANANA_SHIELD
		|| t2->type == MT_ORBINAUT || t2->type == MT_ORBINAUT_SHIELD
		|| t2->type == MT_JAWZ || t2->type == MT_JAWZ_DUD || t2->type == MT_JAWZ_SHIELD
		|| t2->type == MT_BALLHOG)
	{
		// Other Item Damage
		angle_t bounceangle = K_GetCollideAngle(t1, t2);

		S_StartSound(t2, t2->info->deathsound);
		P_KillMobj(t2, t1, t1, DMG_NORMAL);

		P_SetObjectMomZ(t2, 8*FRACUNIT, false);
		P_InstaThrust(t2, bounceangle, 16*FRACUNIT);

		P_SpawnMobj(t2->x/2 + t1->x/2, t2->y/2 + t1->y/2, t2->z/2 + t1->z/2, MT_ITEMCLASH);

		damageitem = true;
	}
	else if (t2->type == MT_SSMINE_SHIELD || t2->type == MT_SSMINE || t2->type == MT_LANDMINE)
	{
		damageitem = true;
		// Bomb death
		P_KillMobj(t2, t1, t1, DMG_NORMAL);
	}
	else if (t2->flags & MF_SHOOTABLE)
	{
		// Shootable damage
		P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);
		damageitem = true;
	}

	if (damageitem)
	{
		// This Item Damage
		angle_t bounceangle = K_GetCollideAngle(t2, t1);

		S_StartSound(t1, t1->info->deathsound);
		P_KillMobj(t1, t2, t2, DMG_NORMAL);

		P_SetObjectMomZ(t1, 8*FRACUNIT, false);
		P_InstaThrust(t1, bounceangle, 16*FRACUNIT);
	}

	return true;
}

boolean K_EggItemCollide(mobj_t *t1, mobj_t *t2)
{
	// Push fakes out of other item boxes
	if (t2->type == MT_RANDOMITEM || t2->type == MT_EGGMANITEM)
	{
		P_InstaThrust(t1, R_PointToAngle2(t2->x, t2->y, t1->x, t1->y), t2->radius/4);
		return true;
	}

	if (t2->player)
	{
		if ((t1->target == t2 || t1->target == t2->target) && (t1->threshold > 0))
			return true;

		if (t1->health <= 0 || t2->health <= 0)
			return true;

		if (!P_CanPickupItem(t2->player, 2))
			return true;

		if ((gametyperules & GTR_BUMPERS) && t2->player->bumper <= 0)
		{
			if (t2->player->karmamode || t2->player->karmadelay)
				return true;
			t2->player->karmamode = 2;
		}
		else
		{
			K_DropItems(t2->player); //K_StripItems(t2->player);
			//K_StripOther(t2->player);
			t2->player->itemroulette = KROULETTE_ACTIVE;
			t2->player->roulettetype = KROULETTETYPE_EGGMAN;
		}

		if (t2->player->flamestore && (K_GetShieldFromPlayer(t2->player) == KITEM_FLAMESHIELD))
		{
			// Melt item
			S_StartSound(t2, sfx_s3k43);
			P_KillMobj(t1, t2, t2, DMG_NORMAL);
			return true;
		}
		else
		{
			mobj_t *poof = P_SpawnMobj(t1->x, t1->y, t1->z, MT_EXPLODE);
			S_StartSound(poof, t1->info->deathsound);

#if 0
			// Eggbox snipe!
			if (t1->type == MT_EGGMANITEM && t1->health > 1)
				S_StartSound(t2, sfx_bsnipe);
#endif

			if (t1->target && t1->target->player)
			{
				if ((gametyperules & GTR_CIRCUIT) || t1->target->player->bumper > 0)
					t2->player->eggmanblame = t1->target->player-players;
				else
					t2->player->eggmanblame = t2->player-players;

				if (t1->target->hnext == t1)
				{
					P_SetTarget(&t1->target->hnext, NULL);
					t1->target->player->itemflags &= ~IF_EGGMANOUT;
				}
			}

			P_RemoveMobj(t1);
			return true;
		}
	}

	return true;
}

boolean K_MineCollide(mobj_t *t1, mobj_t *t2)
{
	if (((t1->target == t2) || (!(t2->flags & (MF_ENEMY|MF_BOSS)) && (t1->target == t2->target))) && (t1->threshold > 0 || (t2->type != MT_PLAYER && t2->threshold > 0)))
		return true;

	if (t1->health <= 0 || t2->health <= 0)
		return true;

	if (t2->player)
	{
		if (t2->player->flashing > 0)
			return true;

		// Bomb punting
		if ((t1->state >= &states[S_SSMINE1] && t1->state <= &states[S_SSMINE4])
			|| (t1->state >= &states[S_SSMINE_DEPLOY8] && t1->state <= &states[S_SSMINE_DEPLOY13]))
		{
			P_KillMobj(t1, t2, t2, DMG_NORMAL);
		}
		else
		{
			K_PuntMine(t1, t2);
		}
	}
	else if (t2->type == MT_ORBINAUT || t2->type == MT_JAWZ || t2->type == MT_JAWZ_DUD
		|| t2->type == MT_ORBINAUT_SHIELD || t2->type == MT_JAWZ_SHIELD)
	{
		// Bomb death
		angle_t bounceangle = K_GetCollideAngle(t1, t2);

		P_KillMobj(t1, t2, t2, DMG_NORMAL);

		// Other Item Damage
		S_StartSound(t2, t2->info->deathsound);
		P_KillMobj(t2, t1, t1, DMG_NORMAL);

		P_SetObjectMomZ(t2, 8*FRACUNIT, false);
		P_InstaThrust(t2, bounceangle, 16*FRACUNIT);
	}
	else if (t2->flags & MF_SHOOTABLE)
	{
		// Bomb death
		P_KillMobj(t1, t2, t2, DMG_NORMAL);
		// Shootable damage
		P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);
	}

	return true;
}

boolean K_MineExplosionCollide(mobj_t *t1, mobj_t *t2)
{
	if (t2->player)
	{
		if (t2->player->flashing > 0)
			return true;

		if (t1->state == &states[S_MINEEXPLOSION1])
		{
			P_DamageMobj(t2, t1, t1->target, 1, DMG_EXPLODE);
		}
		else
		{
			P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);
		}
	}
	else if (t2->flags & MF_SHOOTABLE)
	{
		// Shootable damage
		P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);
	}

	return true;
}

boolean K_LandMineCollide(mobj_t *t1, mobj_t *t2)
{
	if (((t1->target == t2) || (!(t2->flags & (MF_ENEMY|MF_BOSS)) && (t1->target == t2->target))) && (t1->threshold > 0 || (t2->type != MT_PLAYER && t2->threshold > 0)))
		return true;

	if (t1->health <= 0 || t2->health <= 0)
		return true;

	if (t2->player)
	{
		if (t2->player->flashing)
			return true;

		// Banana snipe!
		if (t1->health > 1)
			S_StartSound(t2, sfx_bsnipe);

		if (t2->player->flamestore && (K_GetShieldFromPlayer(t2->player) == KITEM_FLAMESHIELD))
		{
			// Melt item
			S_StartSound(t2, sfx_s3k43);
		}
		else
		{
			// Player Damage
			P_DamageMobj(t2, t1, t1->target, 1, DMG_EXPLODE);
		}

		P_SpawnMobj(t1->x, t1->y, t1->z, MT_MINEEXPLOSIONSOUND);
		P_KillMobj(t1, t2, t2, DMG_NORMAL);
	}
	else if (t2->type == MT_BANANA || t2->type == MT_BANANA_SHIELD
		|| t2->type == MT_ORBINAUT || t2->type == MT_ORBINAUT_SHIELD
		|| t2->type == MT_JAWZ || t2->type == MT_JAWZ_DUD || t2->type == MT_JAWZ_SHIELD
		|| t2->type == MT_BALLHOG)
	{
		// Other Item Damage
		angle_t bounceangle = K_GetCollideAngle(t1, t2);

		if (t2->eflags & MFE_VERTICALFLIP)
			t2->z -= t2->height;
		else
			t2->z += t2->height;

		S_StartSound(t2, t2->info->deathsound);
		P_KillMobj(t2, t1, t1, DMG_NORMAL);

		P_SetObjectMomZ(t2, 8*FRACUNIT, false);
		P_InstaThrust(t2, bounceangle, 16*FRACUNIT);

		P_SpawnMobj(t2->x/2 + t1->x/2, t2->y/2 + t1->y/2, t2->z/2 + t1->z/2, MT_ITEMCLASH);

		P_SpawnMobj(t1->x, t1->y, t1->z, MT_MINEEXPLOSIONSOUND);
		P_KillMobj(t1, t2, t2, DMG_NORMAL);
	}
	else if (t2->type == MT_SSMINE_SHIELD || t2->type == MT_SSMINE || t2->type == MT_LANDMINE)
	{
		P_SpawnMobj(t1->x, t1->y, t1->z, MT_MINEEXPLOSIONSOUND);
		P_KillMobj(t1, t2, t2, DMG_NORMAL);
		// Bomb death
		P_KillMobj(t2, t1, t1, DMG_NORMAL);
	}
	else if (t2->flags & MF_SHOOTABLE)
	{
		// Shootable damage
		P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);
		P_SpawnMobj(t1->x, t1->y, t1->z, MT_MINEEXPLOSIONSOUND);
		P_KillMobj(t1, t2, t2, DMG_NORMAL);
	}

	return true;
}

static mobj_t *lightningSource;
static fixed_t lightningDist;

static inline BlockItReturn_t PIT_ThunderShieldAttack(mobj_t *thing)
{
	if (lightningSource == NULL || P_MobjWasRemoved(lightningSource))
	{
		// Invalid?
		return BMIT_ABORT;
	}

	if (thing == NULL || P_MobjWasRemoved(thing))
	{
		// Invalid?
		return BMIT_ABORT;
	}

	if (thing == lightningSource)
	{
		// Don't explode yourself!!
		return BMIT_CONTINUE;
	}

	if (thing->health <= 0)
	{
		// Dead
		return BMIT_CONTINUE;
	}

	if (thing->type != MT_SPB)
	{
		if (!(thing->flags & MF_SHOOTABLE) || (thing->flags & MF_SCENERY))
		{
			// Not shootable
			return BMIT_CONTINUE;
		}
	}

	if (thing->player && thing->player->spectator)
	{
		// Spectator
		return BMIT_CONTINUE;
	}

	if (P_AproxDistance(thing->x - lightningSource->x, thing->y - lightningSource->y) > lightningDist + thing->radius)
	{
		// Too far away
		return BMIT_CONTINUE;
	}

#if 0
	if (P_CheckSight(lightningSource, thing) == false)
	{
		// Not in sight
		return BMIT_CONTINUE;
	}
#endif

	P_DamageMobj(thing, lightningSource, lightningSource, 1, DMG_VOLTAGE|DMG_CANTHURTSELF);
	return BMIT_CONTINUE;
}

void K_ThunderShieldAttack(mobj_t *actor, fixed_t size)
{
	INT32 bx, by, xl, xh, yl, yh;

	lightningDist = FixedMul(size, actor->scale);
	lightningSource = actor;

	// Use blockmap to check for nearby shootables
	yh = (unsigned)(actor->y + lightningDist - bmaporgy)>>MAPBLOCKSHIFT;
	yl = (unsigned)(actor->y - lightningDist - bmaporgy)>>MAPBLOCKSHIFT;
	xh = (unsigned)(actor->x + lightningDist - bmaporgx)>>MAPBLOCKSHIFT;
	xl = (unsigned)(actor->x - lightningDist - bmaporgx)>>MAPBLOCKSHIFT;

	BMBOUNDFIX (xl, xh, yl, yh);

	for (by = yl; by <= yh; by++)
		for (bx = xl; bx <= xh; bx++)
			P_BlockThingsIterator(bx, by, PIT_ThunderShieldAttack);
}

static void K_BubbleShieldCollideDrain(player_t *player, mobj_t *bubble)
{
	if (!player)
	{
		// Bad reference passed.
		return;
	}

	if (!bubble || P_MobjWasRemoved(bubble) )
	{
		// Bad/Stale reference passed.
		return;
	}

	player->bubblepop++;
	player->bubblecool = 15*4;
	player->itemflags &= ~IF_HOLDREADY;
	bubble->extravalue1 = 4;
	bubble->cvmem = 1;

	if (player->bubblepop > 1)
	{
		K_PopPlayerShield(player);
	}
}

boolean K_BubbleShieldReflect(mobj_t *t1, mobj_t *t2)
{
	mobj_t *owner = t1->player ? t1 : t1->target;

	if (t2->target != owner || !t2->threshold)
	{
		if (!t2->momx && !t2->momy)
		{
			t2->momz += (24*t2->scale) * P_MobjFlip(t2);
		}
		else
		{
			t2->momx = -6*t2->momx;
			t2->momy = -6*t2->momy;
			t2->momz = -6*t2->momz;
			t2->angle += ANGLE_180;
		}
		if (t2->type == MT_JAWZ)
			P_SetTarget(&t2->tracer, t2->target); // Back to the source!
		P_SetTarget(&t2->target, owner); // Let the source reflect it back again!
		t2->threshold = 10;
		S_StartSound(t1, sfx_s3k44);
	}

	return true;
}

boolean K_BubbleShieldCanReflect(mobj_t *t1, mobj_t *t2)
{
	return (t2->type == MT_ORBINAUT || t2->type == MT_JAWZ
		|| t2->type == MT_BANANA || t2->type == MT_EGGMANITEM || t2->type == MT_BALLHOG
		|| t2->type == MT_SSMINE || t2->type == MT_LANDMINE || t2->type == MT_SINK
		|| t2->type == MT_KART_LEFTOVER
		|| (t2->type == MT_PLAYER && t1->target != t2));
}

boolean K_BubbleShieldCollide(mobj_t *t1, mobj_t *t2)
{
	if (t1->type == MT_PLAYER)
		return true;

	if (t1->target == t2)
		return true;

	if (t2->type == MT_PLAYER)
	{
		if (P_PlayerInPain(t2->player)
			|| t2->player->flashing || t2->player->hyudorotimer
			|| t2->player->justbumped || t2->scale > t1->scale + (mapobjectscale/8))
			return true;
		// Player Damage
		if (K_KartBouncing(t2, t1->target, false, true))
		{
			P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);
			K_BubbleShieldCollideDrain(t1->target->player, t1);
			S_StartSound(t2, sfx_s3k44);
		}

		return true;
	}

	if (K_BubbleShieldCanReflect(t1, t2))
	{
		if (!t2->threshold)
		{
			// Drain my stuff please.
			K_BubbleShieldCollideDrain(t1->target->player, t1);
		}
		return K_BubbleShieldReflect(t1, t2);
	}

	if (t2->flags & MF_SHOOTABLE)
	{
		P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);
	}

	// no interaction
	return true;
}

boolean K_KitchenSinkCollide(mobj_t *t1, mobj_t *t2)
{
	if (((t1->target == t2) || (!(t2->flags & (MF_ENEMY|MF_BOSS)) && (t1->target == t2->target))) && (t1->threshold > 0 || (t2->type != MT_PLAYER && t2->threshold > 0)))
		return true;

	if (t2->player)
	{
		if (t2->player->flashing > 0)
			return true;

		K_StatPlayerSink(t1->player, P_MobjWasRemoved(t2->target) ? NULL : t2->target->player);

		S_StartSound(NULL, sfx_bsnipe); //let all players hear it.
		S_StartSound(NULL, sfx_cgot); //let all players hear it.

		HU_SetCEchoFlags(0);
		HU_SetCEchoDuration(5);
		HU_DoCEcho(va("%s\\was hit by a kitchen sink.\\\\\\\\", player_names[t2->player-players]));
		I_OutputMsg("%s was hit by a kitchen sink.\n", player_names[t2->player-players]);

		P_PlayerRingBurst(t2->player, t2->player->rings);
		P_DamageMobj(t2, t1, t1->target, 1, DMG_INSTAKILL);
		P_KillMobj(t1, t2, t2, DMG_NORMAL);
	}
	else if (t2->flags & MF_SHOOTABLE)
	{
		// Shootable damage
		P_KillMobj(t2, t2, t1->target, DMG_NORMAL);
		// This item damage
		P_KillMobj(t1, t2, t2, DMG_NORMAL);
	}

	return true;
}

boolean K_FallingRockCollide(mobj_t *t1, mobj_t *t2)
{
	if (t2->player || t2->type == MT_FALLINGROCK)
		K_KartBouncing(t2, t1, false, false);
	return true;
}

boolean K_SMKIceBlockCollide(mobj_t *t1, mobj_t *t2)
{
	if (!(t2->flags & MF_SOLID || t2->flags & MF_SHOOTABLE || t2->flags & MF_BOUNCE))
		return true;

	if (!(t2->health))
		return true;

	if (t2->type == MT_BANANA || t2->type == MT_BANANA_SHIELD
		|| t2->type == MT_EGGMANITEM || t2->type == MT_EGGMANITEM_SHIELD
		|| t2->type == MT_SSMINE || t2->type == MT_SSMINE_SHIELD
		|| t2->type == MT_ORBINAUT_SHIELD || t2->type == MT_JAWZ_SHIELD)
		return false;

	if (t1->health)
		P_KillMobj(t1, t2, t2, DMG_NORMAL);

	/*
	if (t2->player && (t2->player->invincibilitytimer > 0
		|| t2->player->growshrinktimer > 0))
		return true;
	*/

	K_KartBouncing(t2, t1, false, true);
	return false;
}

boolean K_PvPTouchDamage(mobj_t *t1, mobj_t *t2)
{
	const boolean flameT1 = ((t1->player->flamestore > 0) && (t1->player->flametimer > 0));
	const boolean flameT2 = ((t2->player->flamestore > 0) && (t2->player->flametimer > 0));

	boolean t1Condition = false;
	boolean t2Condition = false;

	// Rim suggestion: Flipover damage is negligible at best, just cull it from Invincibility as a whole.
	if (invintype == KARTINVIN_LEGACY)
	{
		t1Condition = (t1->player->invincibilitytimer > 0);
		t2Condition = (t2->player->invincibilitytimer > 0);
	}

	if ((t1Condition == true || flameT1 == true) && (t2Condition == true || flameT2 == true))
	{
		K_DoInstashield(t1->player);
		K_DoInstashield(t2->player);
		return false;
	}
	else if (invintype == KARTINVIN_LEGACY)
	{
		if (t1Condition == true && t2Condition == false)
		{
			P_DamageMobj(t2, t1, t1, 1, DMG_WIPEOUT);
			return true;
		}
		else if (t1Condition == false && t2Condition == true)
		{
			P_DamageMobj(t1, t2, t2, 1, DMG_WIPEOUT);
			return true;
		}
	}
	
	t1Condition = (t1->scale > t2->scale + (mapobjectscale/8));
	t2Condition = (t2->scale > t1->scale + (mapobjectscale/8));
	
	if ((t1Condition == true || flameT1 == true) && (t2Condition == true || flameT2 == true))
	{
		return false;
	}
	else if (t1Condition == true && t2Condition == false)
	{
		P_DamageMobj(t2, t1, t1, 1, DMG_SQUISH);
		return true;
	}
	else if (t1Condition == false && t2Condition == true)
	{
		P_DamageMobj(t1, t2, t2, 1, DMG_SQUISH);
		return true;
	}

	// Flame Shield dash damage
	t1Condition = flameT1;
	t2Condition = flameT2;

	if (t1Condition == true && t2Condition == false)
	{
		P_DamageMobj(t2, t1, t1, 1, DMG_FLIPOVER);
		return true;
	}
	else if (t1Condition == false && t2Condition == true)
	{
		P_DamageMobj(t1, t2, t2, 1, DMG_FLIPOVER);
		return true;
	}

	// Battle Mode Sneaker damage
	// (Pogo Spring damage is handled in head-stomping code)
	if (gametyperules & GTR_BUMPERS)
	{
		t1Condition = ((t1->player->sneakertimer > 0)
			&& !P_PlayerInPain(t1->player)
			&& (t1->player->flashing == 0));
		t2Condition = ((t2->player->sneakertimer > 0)
			&& !P_PlayerInPain(t2->player)
			&& (t2->player->flashing == 0));

		if (t1Condition == true && t2Condition == false)
		{
			P_DamageMobj(t2, t1, t1, 1, DMG_WIPEOUT|DMG_STEAL);
			return true;
		}
		else if (t1Condition == false && t2Condition == true)
		{
			P_DamageMobj(t1, t2, t2, 1, DMG_WIPEOUT|DMG_STEAL);
			return true;
		}
	}
	
	return false;
}
