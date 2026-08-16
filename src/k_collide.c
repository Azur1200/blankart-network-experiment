/// \file  k_collide.c
/// \brief SRB2Kart item collision hooks

#include "k_collide.h"
#include "k_stats.h"
#include "doomstat.h"
#include "doomtype.h"
#include "p_mobj.h"
#include "k_kart.h"
#include "k_items.h"
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

// This/Other Item Damage
void K_ItemDamage(mobj_t *t1, mobj_t *t2, dboolean clash)
{
	if (t1->eflags & MFE_VERTICALFLIP)
		t1->z -= t1->height;
	else
		t1->z += t1->height;

	S_StartSound(t1, t1->info->deathsound);
	P_KillMobj(t1, t2, t2, DMG_NORMAL);

	P_SetObjectMomZ(t1, 8*FRACUNIT, false);
	P_InstaThrust(t1, K_GetCollideAngle(t2, t1), 16*FRACUNIT);

	if (clash)
		P_SpawnMobj(t1->x/2 + t2->x/2, t1->y/2 + t2->y/2, t1->z/2 + t2->z/2, MT_ITEMCLASH);
}

// makes sure that hitthing isn't set to tmthing, otherwise the bounce doesn't work
static void K_SetHitThing(mobj_t *t1, mobj_t *t2)
{
	P_SetTarget(&g_tm.hitthing, t1 == g_tm.thing ? t2 : t1);
}

#define NOMOREWIPEOUTS (10) // This is when wipeout damage becomes egregious

uint8_t K_GetOrbiJawzDamage(void)
{
	if (cv_kartorbijawzdmg.value)
		return (uint8_t)(cv_kartorbijawzdmg.value); // Just return the raw damage value, since it's part of the cvar

	// Otherwise, we're likely doing automatic damage balancing

	// FIXME: Do I need to say it?

	int32_t i = 0;
	int32_t numplaying = 0;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue; // Not playing

		if (players[i].spectator || P_MobjWasRemoved(players[i].mo))
			continue; // Spectating, or has no object

		numplaying++;
	}

	if (numplaying >= NOMOREWIPEOUTS)
	{
		// Using wipeout damage at this point would feel plain _bad_, so let's use crash damage
		return DMG_CRASH;
	}

	return DMG_WIPEOUT;
}

#undef NOMOREWIPEOUTS

dboolean K_OrbinautJawzCollide(mobj_t *t1, mobj_t *t2)
{
	dboolean damageitem = false;
	dboolean sprung = false;

	uint8_t orbidamage = K_GetOrbiJawzDamage();

	if (itempushing && t2->type == MT_RANDOMITEM
	 && (t1->type == MT_JAWZ_DUD || (t1->type == MT_ORBINAUT && t1->flags2 & MF2_AMBUSH)))
	{
		P_Thrust(t1, R_PointToAngle2(t2->x, t2->y, t1->x, t1->y), t2->radius/512);
		return true;
	}

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
		if (t2->player->flashing && !(t1->type == MT_ORBINAUT || t1->type == MT_JAWZ))
			return true;

		if (t2->player->hyudorotimer)
			return true; // no interaction

		if (t2->player->smonitortimer)
		{
			// t2's player has an S-Monitor, let them plow through us
			K_ItemDamage(t1, t2, false);
			return true; // Don't interact past this
		}

		if (t2->player->flamestore && (K_GetShieldFromPlayer(t2->player) == KSHIELD_FLAME))
		{
			// Melt item
			S_StartSound(t2, sfx_s3k43);
			damageitem = true;
			// and cut temperature and fuel by 10% of max
			t2->player->flamestore -= min(FLAMESTOREMAX/10, t2->player->flamestore);
			t2->player->flametimer -= min((itemtime*3)/10, t2->player->flametimer - 1);
			t2->player->flameburnstop = TICRATE/4;
		}
		else
		{
			if ((!P_IsObjectOnGround(t1)) && (!P_IsObjectOnGround(t2)) && ((orbidamage & DMG_TYPEMASK) == DMG_CRASH))
			{
				// If *both* the Orbinaut and the player are in the air, activate bumping
				t2->player->crash_bumpme = true;
			}


			// Player Damage
			P_DamageMobj(t2, t1, t1->target, 1, orbidamage);
			K_SetHitThing(t2, t1);
			S_StartSound(t2, sfx_s3k7b);
			//damageitem = true; // delayed until K_KartBouncing, don't mess with the orbi's momentum
		}
	}
	else if (t2->type == MT_ORBINAUT || t2->type == MT_JAWZ || t2->type == MT_JAWZ_DUD
		|| t2->type == MT_ORBINAUT_SHIELD || t2->type == MT_JAWZ_SHIELD
		|| t2->type == MT_BANANA || t2->type == MT_BANANA_SHIELD
		|| t2->type == MT_BALLHOG)
	{
		// Other Item Damage
		K_ItemDamage(t2, t1, true);
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
		K_ItemDamage(t1, t2, false);
	}

	if (sprung)
	{
		return false;
	}

	return true;
}

dboolean K_BananaBallhogCollide(mobj_t *t1, mobj_t *t2)
{
	dboolean damageitem = false;

	if (itempushing && t2->type == MT_RANDOMITEM && t1->type == MT_BANANA)
	{
		P_InstaThrust(t1, R_PointToAngle2(t2->x, t2->y, t1->x, t1->y), t2->radius/4);
		return true;
	}

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
		if (t2->player->flashing > 0)
			return true;

		// Banana snipe!
		if (t1->type == MT_BANANA && t1->health > 1)
			S_StartSound(t2, sfx_bsnipe);

		damageitem = true;

		if (t2->player->flamestore && (K_GetShieldFromPlayer(t2->player) == KSHIELD_FLAME))
		{
			// Melt item
			S_StartSound(t2, sfx_s3k43);
			// and cut temperature and fuel by 10% of max
			t2->player->flamestore -= min(FLAMESTOREMAX/10, t2->player->flamestore);
			t2->player->flametimer -= min((itemtime*3)/10, t2->player->flametimer - 1);
			t2->player->flameburnstop = TICRATE/4;
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
		K_ItemDamage(t2, t1, true);
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
		K_ItemDamage(t1, t2, false);
	}

	return true;
}

dboolean K_EggItemCollide(mobj_t *t1, mobj_t *t2)
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

		if (t2->player->smonitortimer)
		{
			// t2's player has an S-Monitor, don't do squat
			return true;
		}

		if (!P_CanPickupItem(t2->player, PICKUPITEM_EGGMAN))
			return true;

		if ((gametypes[gametype]->rules & GTR_BUMPERS) && t2->player->bumper <= 0)
		{
			if (t2->player->karmamode || t2->player->karmadelay)
				return true;
			t2->player->karmamode = KARMAMODE_EGGMAN;
		}
		else
		{
			K_StartRoulette(t2->player, KROULETTETYPE_EGGMAN);
		}

		if (t2->player->flamestore && (K_GetShieldFromPlayer(t2->player) == KSHIELD_FLAME))
		{
			// Melt item
			S_StartSound(t2, sfx_s3k43);
			P_KillMobj(t1, t2, t2, DMG_NORMAL);
			// and cut temperature and fuel by 10% of max
			t2->player->flamestore -= min(FLAMESTOREMAX/10, t2->player->flamestore);
			t2->player->flametimer -= min((itemtime*3)/10, t2->player->flametimer - 1);
			t2->player->flameburnstop = TICRATE/4;
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
				if ((gametypes[gametype]->rules & GTR_CIRCUIT) || t1->target->player->bumper > 0)
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

void K_EggMineBounce(mobj_t *t1, mobj_t *t2)
{
	fixed_t zimpulse;
	fixed_t impulse = min(max(FixedMul(R_PointToDist2(0, 0, t2->momx, t2->momy), 115*FRACUNIT/100), 15*mapobjectscale), 45*mapobjectscale);
	fixed_t pointx = (t1->x + t2->x)/2;
	fixed_t pointy = (t1->y + t2->y)/2;
	fixed_t localspacex = t1->x - pointx;
	fixed_t localspacey = t1->y - pointy;
	angle_t impulseangle = R_PointToAngle2(0, 0, (localspacex + t2->momx)/2, (localspacey + t2->momy)/2);
	P_InstaThrust(t1, impulseangle, impulse);
	zimpulse = (P_MobjFlip(t1) * min((P_AproxDistance(t2->momx, t2->momy) + P_AproxDistance(t1->momx, t1->momy)), 32*mapobjectscale)) + t2->momz/2;

	if (t2->type != MT_PLAYER)
	{
		fixed_t zdiff = (t1->z + (t1->height*P_MobjFlip(t1)/2)) - (t2->z + (t2->height*P_MobjFlip(t2))/2);
		zimpulse /= 2;
		if (zdiff < 0)
		{
			zimpulse *= -1;
		}
	}
	// doesn't this look familiar
	t1->z += 1;
	t1->momz = zimpulse;

	if (t1->extravalue1 == 0)
	{
		K_SpawnEggMineBumpEffect(t1);
		t1->extravalue1 = 6;
	}
}

dboolean K_EggMineCollide(mobj_t *t1, mobj_t *t2)
{
	dboolean damageitem = false;
	dboolean sprung = false;

	if (t1->health <= 0 || t2->health <= 0)
		return true;
	
	if (t2->player || t2->type == MT_EGGMINE || t2->type == MT_EGGMINE_SHIELD)
	{
		if ((t2 == t1->target || t2->target == t1->target) && t1->threshold > 0)
		{
			return true;
		}
		
		if (t2->player && t2->player->hyudorotimer)
		{
			return true; // no interaction
		}

		if (t2->player && t2->player->flamestore && (K_GetShieldFromPlayer(t2->player) == KSHIELD_FLAME))
		{
			// Melt item
			S_StartSound(t2, sfx_s3k43);
			// and cut temperature and fuel by 10% of max
			t2->player->flamestore -= min(FLAMESTOREMAX/10, t2->player->flamestore);
			t2->player->flametimer -= min((itemtime*3)/10, t2->player->flametimer - 1);
			t2->player->flameburnstop = TICRATE/4;
			return true;
		}
		else if (t2->type == MT_PLAYER && t1->health > 1)
		{
			if (t1->health > 2 && (P_DamageMobj(t2, t1, t1->target, 1, DMG_FLIPOVER)))
			{
				K_DoEggMineStrip(t2, t1, t1->target);
				S_StartSound(t2, sfx_bsnipe);
				P_RemoveMobj(t1);
				return true;
			}
			else if (P_IsObjectOnGround(t2))
			{
				K_ShoveObject(t1, t2, t1->scale/2);
				K_ShoveObject(t2, t1, t1->scale);
				S_StartSound(t2, sfx_cdfm37);
				K_SpawnEggMineBumpEffect(t1);
				t1->extravalue1 = 6;
				return true;
			}
		}
		K_EggMineBounce(t1, t2);
	}
	else if (t2->flags & MF_SPRING)
	{
		// Let thrown items hit springs!
		sprung = P_DoSpring(t2, t1);
	}
	else if (t2->type == MT_SSMINE_SHIELD || t2->type == MT_SSMINE || t2->type == MT_LANDMINE)
	{
		if (t1->health > 1)
		{
			t1->z += 1;
			t1->momz += 12*mapobjectscale;
			t1->momx /= 2;
			t1->momy /= 2;
		}

		damageitem = true;
		// Bomb death
		P_KillMobj(t2, t1, t1->target, DMG_NORMAL);
	}
	else if (P_IsKartItem(t2->type))
	{
		// eggmines don't kill eachother
		if (!(t2->type == MT_SPB || t2->type == MT_EGGMINE || t2->type == MT_EGGMINE_SHIELD))
		{
			K_ItemDamage(t2, t1, true);
			if (!(t2->type == MT_SPB)) damageitem = true;
		}
	}
	
	if (sprung)
	{
		return false;
	}
	
	if (damageitem && t1->health <= 1)
		P_KillMobj(t1, t2, t2->target, DMG_INSTAKILL);
	return true;
}

dboolean K_EggMineShieldCollide(mobj_t *t1, mobj_t *t2)
{
	if (t1->health <= 0 || t2->health <= 0)
		return true;
	
	if (t2->type == MT_SPB)
		return true;

	if (K_IsMissileOrKartItem(t2))
	{
		// eggmines don't kill eachother
		if (!(t2->type == MT_SPB || t2->type == MT_EGGMINE || t2->type == MT_EGGMINE_SHIELD))
		{
			P_KillMobj(t2, t1, t1->target, DMG_INSTAKILL);
			if (!(t2->type == MT_SPB)) P_KillMobj(t1, t2, t2->target, DMG_INSTAKILL);
		}
		return true;
	}
		
	return true;
}

dboolean K_MineCollide(mobj_t *t1, mobj_t *t2)
{
	if (((t1->target == t2) || (!(t2->flags & (MF_ENEMY|MF_BOSS)) && (t1->target == t2->target))) && (t1->threshold > 0 || (t2->type != MT_PLAYER && t2->threshold > 0)))
		return true;

	if (t1->health <= 0 || t2->health <= 0)
		return true;

	if (itempushing && t2->type == MT_RANDOMITEM)
	{
		P_Thrust(t1, R_PointToAngle2(t2->x, t2->y, t1->x, t1->y), t2->radius/512);
		return true;
	}

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
		P_KillMobj(t1, t2, t2, DMG_NORMAL);

		// Other Item Damage
		K_ItemDamage(t2, t1, false);
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

dboolean K_MineExplosionCollide(mobj_t *t1, mobj_t *t2)
{
	if (t2->player)
	{
		// don't sideswipe anyone boosting with a bubble shield!
		if (t2->player->flashing > 0 || t2->player->bubbleboost > 0)
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

dboolean K_LandMineCollide(mobj_t *t1, mobj_t *t2)
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

		if (t2->player->flamestore && (K_GetShieldFromPlayer(t2->player) == KSHIELD_FLAME))
		{
			// Melt item
			S_StartSound(t2, sfx_s3k43);
			// and cut temperature and fuel by 10% of max
			t2->player->flamestore -= min(FLAMESTOREMAX/10, t2->player->flamestore);
			t2->player->flametimer -= min((itemtime*3)/10, t2->player->flametimer - 1);
			t2->player->flameburnstop = TICRATE/4;
		}
		else
		{
			// Player Damage
			if (P_DamageMobj(t2, t1, t1->target, 1, DMG_FLIPOVER))
			{
				// these can only come from eggmines now
				K_DoEggMineStrip(t2, t1, t1->target);
			}
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
		K_ItemDamage(t2, t1, true);

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
static dboolean attractionhipower;
static fixed_t attractionPowerRate;

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

	if (thing->type == MT_SPB) // If you destroy a SPB, you don't get the luxury of a cooldown.
	{
		spbplace = -1;
		K_SetIndirectItemCooldown(0);
	}

	P_DamageMobj(thing, lightningSource, lightningSource, 1, DMG_VOLTAGE|DMG_CANTHURTSELF);
	return BMIT_CONTINUE;
}

void K_ThunderShieldAttack(mobj_t *actor, fixed_t size)
{
	int32_t bx, by, xl, xh, yl, yh;

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

static inline BlockItReturn_t PIT_AttractionShieldAttack(mobj_t *thing)
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

#if 0
	if (thing->player && (ATTRACTIONAOEDAMAGE == 0 || (!attractionhipower)))
	{
		// Don't damage players
		return BMIT_CONTINUE;
	}
#endif

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

	if (thing->type == MT_SPB) // If you destroy a SPB, you don't get the luxury of a cooldown.
	{
		spbplace = -1;
		K_SetIndirectItemCooldown(0);
	}

	if (thing->player)
	{
		if (ATTRACTIONAOEDAMAGE && attractionhipower)
		{
			P_DamageMobj(thing, lightningSource, lightningSource, 1, ATTRACTIONAOEDAMAGE|DMG_CANTHURTSELF);
		}
		if (ATTRACTIONAOEEMP && attractionhipower && thing != lightningSource)
		{
			//todo: filter out teammates
			K_SetPlayerItemCooldown(thing->player, TICRATE * ATTRACTIONAOEEMP, false);
		}
		if (ATTRACTIONAOESHOVE)
		{
			K_ShoveObject(thing, lightningSource, FixedMul(ATTRACTIONAOESHOVE, attractionPowerRate));
		}
	}
	else
	{
		P_DamageMobj(thing, lightningSource, lightningSource, 1, DMG_VOLTAGE|DMG_CANTHURTSELF);
	}
	return BMIT_CONTINUE;
}

void K_AttractionShieldAttack(mobj_t *actor, fixed_t size, fixed_t powerrate, dboolean hipower)
{
	int32_t bx, by, xl, xh, yl, yh;

	lightningDist = FixedMul(size, actor->scale);
	lightningSource = actor;
	attractionhipower = hipower;
	attractionPowerRate = powerrate;

	// Use blockmap to check for nearby shootables
	yh = (unsigned)(actor->y + lightningDist - bmaporgy)>>MAPBLOCKSHIFT;
	yl = (unsigned)(actor->y - lightningDist - bmaporgy)>>MAPBLOCKSHIFT;
	xh = (unsigned)(actor->x + lightningDist - bmaporgx)>>MAPBLOCKSHIFT;
	xl = (unsigned)(actor->x - lightningDist - bmaporgx)>>MAPBLOCKSHIFT;

	BMBOUNDFIX (xl, xh, yl, yh);

	for (by = yl; by <= yh; by++)
		for (bx = xl; bx <= xh; bx++)
			P_BlockThingsIterator(bx, by, PIT_AttractionShieldAttack);
}

static void K_BubbleShieldCollideDrain(player_t *player, mobj_t *bubble, int16_t dmg)
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

	// Damage the shield after an item hit.
	K_RemoveBubbleHealth(player, dmg);

	if (K_GetShieldFromPlayer(player) == KSHIELD_BUBBLE)
	{
		if ((dmg > 0) && (bubble) && (!P_MobjWasRemoved(bubble)))
		{
			// Play a distinct "crack!" noise to clue the player in.
			S_StartSound(bubble, sfx_s3k6e);
		}
	}

	// Apply a cooldown if the Bubble Shield took damage without shattering.
	if ((player->bubblehealth > 0) && (dmg > 0))
		player->bubbleblowup /= 2;
}

dboolean K_BubbleShieldReflectSpecialCase(const mobj_t *t)
{
	if ((!t) || (P_MobjWasRemoved(t)))
		return false; // Object doesn't even exist

	return t->type == MT_FALLINGROCK; // TODO: Maybe an object flag (MF_BUBBLEREFLECT)?
}

static dboolean K_BubbleReflectingTrapItem(const mobj_t *t)
{
	// slow orbis are traps
	if ((t->type == MT_ORBINAUT) && (FixedHypot(t->momx, t->momy) < 24*mapobjectscale))
		return true;

	return t->type == MT_BANANA || (t->type == MT_ORBINAUT && t->flags2 & MF2_AMBUSH) || t->type == MT_JAWZ_DUD ||
	       t->type == MT_EGGMANITEM || t->type == MT_SSMINE || t->type == MT_SSMINE_SHIELD || t->type == MT_LANDMINE ||
		   t->type == MT_EGGMINE || K_BubbleShieldReflectSpecialCase(t);
}

static dboolean K_StrongPlayerBump(const player_t *player)
{
	return ((player->invincibilitytimer)
	    || (player->growshrinktimer > 0));
}

dboolean K_BubbleShieldReflect(mobj_t *t1, mobj_t *t2)
{
	if (t2->threshold && !t2->player)
		return false;

	mobj_t *owner = t1->player ? t1 : t1->target;

	angle_t angle = R_PointToAngle2(t1->x, t1->y, t2->x, t2->y);
	fixed_t momentum = max(FixedHypot(owner->momx, owner->momy), FixedHypot(t2->momx, t2->momy));
	momentum = max(3*momentum/4, 16*mapobjectscale); // do SOMETHING!

	if (t2->player && ((t2->player->respawn)||(t2->player->flashing)))
	{
		// Don't reflect respawning/flashing players.
		return false;
	}

	if (t2->player && !K_StrongPlayerBump(t2->player))
	{
		P_Thrust(t2, angle, momentum/2);
	}
	else
	{
		P_InstaThrust(t2, angle, momentum);
		if (!t2->player)
			t2->angle = angle;
	}

	if (K_BubbleReflectingTrapItem(t2) && (t2->momz * P_MobjFlip(t2) <= 8*t2->scale))
	{
		// Stupid hack: Toss trap/dud items into the air
		t2->momz += (24*t2->scale) * P_MobjFlip(t2);
		t2->z += t2->momz;
	}

	if (!t2->player)
	{
		if (t2->type == MT_JAWZ)
			P_SetTarget(&t2->tracer, t2->target); // Back to the source!
		P_SetTarget(&t2->target, owner); // Let the source reflect it back again!
		t2->threshold = 10;
	}
	else if (t2->player->attractionattack)
	{
		// trolled
		t2->player->attractioncharge = 0;
		t2->player->attractionattack = 0;
		t2->player->attractionattack_hipower = 0;
		t2->player->attractionboost = 0;
		K_DisableSeekingReticule(t2->player);
	}

	S_StartSound(t1, sfx_s3k44);
	return true;
}

dboolean K_BubbleShieldCanReflect(mobj_t *t)
{
	return (t->type == MT_ORBINAUT || t->type == MT_JAWZ || t->type == MT_JAWZ_DUD
		|| t->type == MT_BANANA || t->type == MT_EGGMANITEM || t->type == MT_BALLHOG
		|| t->type == MT_SSMINE || t->type == MT_LANDMINE || t->type == MT_SINK || t->type == MT_EGGMINE
		|| t->type == MT_KART_LEFTOVER || K_BubbleShieldReflectSpecialCase(t)
		|| t->type == MT_PLAYER);
}

static int16_t K_GetBubbleDamage(mobj_t* itm)
{
	if ((!itm) || (P_MobjWasRemoved(itm)))
	{
		return 0;
	}

	int16_t dmg = BUBBLEITMDAMAGE;
	fixed_t momentum_epsilon = 32*mapobjectscale;

	switch (itm->type)
	{
	case MT_EGGMANITEM:
	case MT_BALLHOG:
	case MT_EGGMINE:
		// Experiment: Let Bubble Shields hard-counter Ballhogs and Eggboxes.
		dmg = 0;
		break;
	case MT_SINK:
	case MT_JAWZ:
		// Sinks and Jawz smash Bubble Shields to bits.
		dmg = MAXBUBBLEHEALTH;
		break;
	case MT_BANANA:
		if (itm->health <= 1)
		{
			// Not sniping; don't run the penetration check.
			break;
		}
		/*FALLTHRU*/
	case MT_ORBINAUT:
	{
		fixed_t momentum = K_Momentum2D(itm);

		// Reward players on the offensive by letting fast Orbinauts shatter Bubble Shields.
		// Same thing for Banana snipes.
		if (momentum >= momentum_epsilon)
		{
			dmg = MAXBUBBLEHEALTH;
		}

		break;
	}
	default:
		break;
	}

	return dmg;
}

dboolean K_BubbleShieldCollide(mobj_t *t1, mobj_t *t2)
{
	// don't bump yourself
	if (t1->target == t2)
		return true;

	if (K_BubbleShieldCanReflect(t2))
	{
		dboolean reflected = K_BubbleShieldReflect(t1, t2);

		if (reflected)
		{
			// Drain my stuff please.
			if ((!P_MobjWasRemoved(t1)) && (t1->target) && (t1->target->player))
			{
				K_BubbleShieldCollideDrain(t1->target->player,
							   t1,
							   K_GetBubbleDamage(t2));
			}
		}

		return reflected;
	}

	if (t2->flags & MF_SHOOTABLE)
	{
		dboolean shootable = P_DamageMobj(t2, t1, t1->target, 1, DMG_NORMAL);

		if (shootable)
		{
			// Drain my stuff please.

			// FfffffFFFFUCKING EXPLODING BARRELS AAAAAA
			if ((!P_MobjWasRemoved(t1)) && (t1->target) && (t1->target->player))
			{
				K_BubbleShieldCollideDrain(t1->target->player,
							   t1,
							   K_GetBubbleDamage(t2));
			}
		}

		return shootable;
	}

	// no interaction
	return true;
}

dboolean K_KitchenSinkCollide(mobj_t *t1, mobj_t *t2)
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

dboolean K_FallingRockCollide(mobj_t *t1, mobj_t *t2)
{
	if ((t2->player && (G_CompatLevel(0x0008) || !t2->player->hyudorotimer)) || t2->type == MT_FALLINGROCK)
	{
		if ((!cv_rockshove.value) || G_CompatLevel(0x0012))
		{
			K_SetHitThing(t1, t2);
		}
		else if (!t2->player->justbumped)
		{
			// masakit feels like rocks
			K_ShoveObject(t2, t1, 16*t2->scale);
			K_SpawnBumpEffect(t2);
			t2->player->justbumped = 6;
		}
	}

	return true;
}

dboolean K_SMKIceBlockCollide(mobj_t *t1, mobj_t *t2)
{
	if (!(t2->flags & MF_SOLID || t2->flags & MF_SHOOTABLE || t2->flags & MF_BOUNCE))
		return true;

	if (!(t2->health))
		return true;

	if (t2->type == MT_BANANA || t2->type == MT_BANANA_SHIELD
		|| t2->type == MT_EGGMANITEM || t2->type == MT_EGGMANITEM_SHIELD
		|| t2->type == MT_SSMINE || t2->type == MT_SSMINE_SHIELD
		|| t2->type == MT_ORBINAUT_SHIELD || t2->type == MT_JAWZ_SHIELD
		|| t2->type == MT_EGGMINE_SHIELD)
		return false;

	if (t1->health)
		P_KillMobj(t1, t2, t2, DMG_NORMAL);

	/*
	if (t2->player && (t2->player->invincibilitytimer > 0
		|| t2->player->growshrinktimer > 0))
		return true;
	*/

	K_SetHitThing(t1, t2);
	return false;
}

dboolean K_PvPTouchDamage(mobj_t *t1, mobj_t *t2)
{
	// What the fuck is calling this with stale refs? Whatever, validation's cheap.
	if (P_MobjWasRemoved(t1) || P_MobjWasRemoved(t2) || !t1->player || !t2->player)
		return false;

	const dboolean flameT1 = ((t1->player->flamestore >= FLAMESTOREMAX-1) && (t1->player->flametimer > 0));
	const dboolean flameT2 = ((t2->player->flamestore >= FLAMESTOREMAX-1) && (t2->player->flametimer > 0));
	const dboolean hyudoroT1 = (t1->player->hyudorotimer > 0);
	const dboolean hyudoroT2 = (t2->player->hyudorotimer > 0);

	dboolean t1Condition = false;
	dboolean t2Condition = false;

	uint8_t invindamage = INVINDAMAGE;

	if (invindamage)
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
	else
	{
		if (t1Condition == true && t2Condition == false)
		{
			return P_DamageMobj(t2, t1, t1, 1, invindamage);
		}
		else if (t1Condition == false && t2Condition == true)
		{
			return P_DamageMobj(t1, t2, t2, 1, invindamage);
		}
	}
	
	t1Condition = (t1->scale > t2->scale + (mapobjectscale/8));
	t2Condition = (t2->scale > t1->scale + (mapobjectscale/8));

	// Merritt suggestion: ONLY make Alt. Shrink squish/flipover interactions happen if and only
	// if the crushing player doesn't have an S-Monitor active

	if (t1Condition && (t1->player && t1->player->smonitortimer) && (t2->player && K_IsAltShrunk(t2->player)))
	{
		// t1Condition: t1's player has an S-Monitor, don't do squat
		// Extend some mercy to the players phasing through one another
		t1->player->justbumped = 6;
		t2->player->justbumped = 6;
		return false;
	}

	if (t2Condition && (t2->player && t2->player->smonitortimer) && (t1->player && K_IsAltShrunk(t1->player)))
	{
		// t2Condition: t2's player has an S-Monitor, don't do squat
		// Extend some mercy to the players phasing through one another
		t1->player->justbumped = 6;
		t2->player->justbumped = 6;
		return false;
	}

	if ((t1Condition == true || flameT1 == true) && (t2Condition == true || flameT2 == true))
	{
		return false;
	}
	else if (t1Condition == true && t2Condition == false)
	{
		if (G_CompatLevel(0x0008) || !hyudoroT1)
		{
			if ((t2->player) && (K_IsAltShrunk(t2->player)) &&
			    (!(t1->player && t1->player->growshrinktimer > 0)))
			{
				// Alt. Shrink: Just make the player flip over.
				if (P_DamageMobj(t2, NULL, NULL, 1, DMG_FLIPOVER))
				{
					K_AltShrinkPityIncrease(t2->player);
					t2->player->altshrinktimeshit++;

					P_InstaThrust(t2, K_MomentumAngle(t2), K_Momentum2D(t2) / 3);

					K_PlayPainSound(t2, NULL);

					return true;
				}
			}
			if (P_IsObjectOnGround(t1) && P_IsObjectOnGround(t2))
			{
				return P_DamageMobj(t2, t1, t1, 1, DMG_SQUISH);
			}
			else
			{
				switch (cv_kartairsquish.value)
				{
					case 1:
						return P_DamageMobj(t2, t1, t1, 1, DMG_SQUISH);
						break;
					case 2:
						return P_DamageMobj(t2, t1, t1, 1, DMG_FLIPOVER);
						break;
					default:
						return false;
						break;
				}
			}
		}
	}
	else if (t1Condition == false && t2Condition == true)
	{
		if (G_CompatLevel(0x0008) || !hyudoroT2)
		{
			// shitty code duplication 🥲

			if ((t1->player) && (K_IsAltShrunk(t1->player)) &&
			    (!(t2->player && t2->player->growshrinktimer > 0)))
			{
				// Alt. Shrink: Just make the player flip over.
				if (P_DamageMobj(t1, NULL, NULL, 1, DMG_FLIPOVER))
				{
					K_AltShrinkPityIncrease(t1->player);
					t1->player->altshrinktimeshit++;

					P_InstaThrust(t1, K_MomentumAngle(t1), K_Momentum2D(t1) / 3);

					K_PlayPainSound(t1, NULL);

					return true;
				}
			}
			else if (P_IsObjectOnGround(t1) && P_IsObjectOnGround(t2))
			{
				return P_DamageMobj(t1, t2, t2, 1, DMG_SQUISH);
			}
			else
			{
				switch (cv_kartairsquish.value)
				{
					case 1:
						return P_DamageMobj(t1, t2, t2, 1, DMG_SQUISH);
						break;
					case 2:
						return P_DamageMobj(t1, t2, t2, 1, DMG_FLIPOVER);
						break;
					default:
						return false;
						break;
				}
			}
		}
	}

	// Flame Shield dash damage
	if (cv_kartflame_shove.value)
	{
		if (flameT1 == true && flameT2 == false)
		{
			K_ShoveObject(t2, t1, 5*t1->scale);
			return true;
		}
		else if (flameT1 == false && flameT2 == true)
		{
			K_ShoveObject(t1, t2, 5*t1->scale);
			return true;
		}
		else
		{
			// non-overheating / "residual" flame, shove is much weaker
			t1Condition = ((t1->player->flamestore >= 1));
			t2Condition = ((t2->player->flamestore >= 1));
			if (t1Condition == true && t2Condition == false)
			{
				K_ShoveObject(t2, t1, 2*t1->scale);
				return true;
			}
			else if (t1Condition == false && t2Condition == true)
			{
				K_ShoveObject(t1, t2, 2*t1->scale);
				return true;
			}
		}
	}
	else
	{
		t1Condition = flameT1;
		t2Condition = flameT2;

		if (t1Condition == true && t2Condition == false)
		{
			return P_DamageMobj(t2, t1, t1, 1, DMG_FLIPOVER);
		}
		else if (t1Condition == false && t2Condition == true)
		{
			return P_DamageMobj(t1, t2, t2, 1, DMG_FLIPOVER);
		}
	}


	// Attraction Shield tackle damage
	// players with Bubble Shield equipped are protected
	t1Condition = (t1->player->attractionattack && t1->player->attractionattack_hipower && (K_GetShieldFromPlayer(t2->player) != KSHIELD_BUBBLE));
	t2Condition = (t2->player->attractionattack && t2->player->attractionattack_hipower && (K_GetShieldFromPlayer(t1->player) != KSHIELD_BUBBLE));
	if (t1Condition == true && t2Condition == false)
	{
		uint8_t damage = DMG_FLIPOVER;

		if (gametypes[gametype]->rules & GTR_BUMPERS)
			damage |= DMG_STEAL;

		return P_DamageMobj(t2, t1, t1, 1, damage);
	}
	else if (t1Condition == false && t2Condition == true)
	{
		uint8_t damage = DMG_FLIPOVER;

		if (gametypes[gametype]->rules & GTR_BUMPERS)
			damage |= DMG_STEAL;

		return P_DamageMobj(t1, t2, t2, 1, damage);
	}

	// Battle Mode Sneaker and Bubble damage
	// (Pogo Spring damage is handled in head-stomping code)
	if (gametypes[gametype]->rules & GTR_BUMPERS)
	{
		t1Condition = (((t1->player->sneakertimer > 0)
		|| (t1->player->bubbleboost > 0))
			&& !P_PlayerInPain(t1->player)
			&& (t1->player->flashing == 0));
		t2Condition = (((t2->player->sneakertimer > 0)
			|| (t2->player->bubbleboost > 0))
			&& !P_PlayerInPain(t2->player)
			&& (t2->player->flashing == 0));

		if (t1Condition == true && t2Condition == false)
		{
			return P_DamageMobj(t2, t1, t1, 1, DMG_WIPEOUT|DMG_STEAL);
		}
		else if (t1Condition == false && t2Condition == true)
		{
			return P_DamageMobj(t1, t2, t2, 1, DMG_WIPEOUT|DMG_STEAL);
		}
	}
	
	return false;
}
