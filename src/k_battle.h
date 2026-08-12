#ifndef __K_BATTLE__
#define __K_BATTLE__

#include "doomtype.h"
#include "d_player.h"

#ifdef __cplusplus
extern "C" {
#endif

extern dboolean itembreaker;
extern int32_t nummapboxes, numgotboxes; // keep track of spawned battle mode items
extern uint8_t numtargets;

int32_t K_StartingBumperCount(void);
dboolean K_IsPlayerWanted(player_t *player);
dboolean K_IsPlayerMostWanted(player_t *player);
void K_CalculateBattleWanted(void);
void K_SpawnBattlePoints(player_t *source, player_t *victim, uint8_t amount);
void K_CheckBumpers(void);
uint8_t K_NumEmeralds(player_t *player);
void K_RunPaperItemSpawners(void);
void K_SpawnPlayerBattleBumpers(player_t *p);
void K_BattleInit(dboolean singleplayercontext);
void K_RespawnBattleBoxes(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
