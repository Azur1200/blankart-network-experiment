#ifndef __K_COLLIDE__
#define __K_COLLIDE__

#include "doomtype.h"
#include "p_mobj.h"

#ifdef __cplusplus
extern "C" {
#endif

angle_t K_GetCollideAngle(mobj_t *t1, mobj_t *t2);
void K_ItemDamage(mobj_t *t1, mobj_t *t2, boolean clash);
UINT8 K_GetOrbiJawzDamage(void);
boolean K_OrbinautJawzCollide(mobj_t *t1, mobj_t *t2);
boolean K_BananaBallhogCollide(mobj_t *t1, mobj_t *t2);
boolean K_EggItemCollide(mobj_t *t1, mobj_t *t2);
void K_EggMineBounce(mobj_t *t1, mobj_t *t2);
boolean K_EggMineCollide(mobj_t *t1, mobj_t *t2);
boolean K_EggMineShieldCollide(mobj_t *t1, mobj_t *t2);
boolean K_MineCollide(mobj_t *t1, mobj_t *t2);
boolean K_MineExplosionCollide(mobj_t *t1, mobj_t *t2);
boolean K_LandMineCollide(mobj_t *t1, mobj_t *t2);
void K_ThunderShieldAttack(mobj_t *actor, fixed_t size);
void K_AttractionShieldAttack(mobj_t *actor, fixed_t size, fixed_t powerrate, boolean hipower);

boolean K_BubbleShieldReflect(mobj_t *t1, mobj_t *t2);
boolean K_BubbleShieldCanReflect(mobj_t *t);
boolean K_BubbleShieldReflectSpecialCase(const mobj_t *t);

boolean K_BubbleShieldCollide(mobj_t *t1, mobj_t *t2);
boolean K_KitchenSinkCollide(mobj_t *t1, mobj_t *t2);
boolean K_FallingRockCollide(mobj_t *t1, mobj_t *t2);
boolean K_SMKIceBlockCollide(mobj_t *t1, mobj_t *t2);
boolean K_PvPTouchDamage(mobj_t *t1, mobj_t *t2);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
