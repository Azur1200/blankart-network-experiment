#ifndef __K_COLLIDE__
#define __K_COLLIDE__

#include "doomtype.h"
#include "p_mobj.h"

#ifdef __cplusplus
extern "C" {
#endif

angle_t K_GetCollideAngle(mobj_t *t1, mobj_t *t2);
void K_ItemDamage(mobj_t *t1, mobj_t *t2, dboolean clash);
uint8_t K_GetOrbiJawzDamage(void);
uint8_t K_GetOrbiJawzSoftDamage(void);
dboolean K_OrbinautJawzCollide(mobj_t *t1, mobj_t *t2);
dboolean K_BananaBallhogCollide(mobj_t *t1, mobj_t *t2);
dboolean K_EggItemCollide(mobj_t *t1, mobj_t *t2);
void K_EggMineBounce(mobj_t *t1, mobj_t *t2);
dboolean K_EggMineCollide(mobj_t *t1, mobj_t *t2);
dboolean K_EggMineShieldCollide(mobj_t *t1, mobj_t *t2);
dboolean K_MineCollide(mobj_t *t1, mobj_t *t2);
dboolean K_MineExplosionCollide(mobj_t *t1, mobj_t *t2);
dboolean K_LandMineCollide(mobj_t *t1, mobj_t *t2);
void K_ThunderShieldAttack(mobj_t *actor, fixed_t size);
void K_AttractionShieldAttack(mobj_t *actor, fixed_t size, fixed_t powerrate, dboolean hipower);

dboolean K_BubbleShieldReflect(mobj_t *t1, mobj_t *t2);
dboolean K_BubbleShieldCanReflect(mobj_t *t);
dboolean K_BubbleShieldReflectSpecialCase(const mobj_t *t);

dboolean K_StrongPlayerBump(player_t *player);
dboolean K_BubbleShieldCollide(mobj_t *t1, mobj_t *t2);
dboolean K_KitchenSinkCollide(mobj_t *t1, mobj_t *t2);
dboolean K_FallingRockCollide(mobj_t *t1, mobj_t *t2);
dboolean K_SMKIceBlockCollide(mobj_t *t1, mobj_t *t2);
dboolean K_PvPTouchDamage(mobj_t *t1, mobj_t *t2);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
