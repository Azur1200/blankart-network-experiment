/* object-specific code */
#ifndef k_objects_H
#define k_objects_H

#ifdef __cplusplus
extern "C" {
#endif

#include "taglist.h"

/* Loops */
mobj_t *Obj_FindLoopCenter(const mtag_t tag);
void Obj_InitLoopEndpoint(mobj_t *end, mobj_t *anchor);
void Obj_InitLoopCenter(mobj_t *center);
void Obj_LinkLoopAnchor(mobj_t *anchor, mobj_t *center, uint8_t type);
void Obj_LoopEndpointCollide(mobj_t *special, mobj_t *toucher);

/* Dash Rings */
void Obj_RegularDashRingSpawn(mobj_t *mobj);
void Obj_RainbowDashRingSpawn(mobj_t *mobj);
void Obj_DashRingSetup(mobj_t *mobj, mapthing_t *mthing);
void Obj_RainbowDashRingThink(mobj_t *mobj);
dboolean Obj_DashRingIsUsableByPlayer(mobj_t *ring, player_t *player);
void Obj_DashRingTouch(mobj_t *mobj, player_t *player);
void Obj_DashRingPlayerThink(player_t *player);
dboolean Obj_DashRingPlayerHasNoGravity(player_t *player);

/* Sneaker Panels */
void Obj_SneakerPanelSpriteScale(mobj_t *mobj);
void Obj_SneakerPanelSpawn(mobj_t *mobj);
void Obj_SneakerPanelSetup(mobj_t *mobj, mapthing_t *mthing);
void Obj_SneakerPanelCollide(mobj_t *pad, mobj_t *mo);
void Obj_SneakerPanelSpawnerSpawn(mobj_t *mobj);
void Obj_SneakerPanelSpawnerSetup(mobj_t *mobj, mapthing_t *mthing);
void Obj_SneakerPanelSpawnerFuse(mobj_t *mobj);

#ifdef __cplusplus
} // extern "C"
#endif

#endif/*k_objects_H*/
