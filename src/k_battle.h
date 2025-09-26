#ifndef __K_BATTLE__
#define __K_BATTLE__

#include "doomtype.h"
#include "d_player.h"

#ifdef __cplusplus
extern "C" {
#endif

extern boolean itembreaker;
extern INT32 nummapboxes, numgotboxes; // keep track of spawned battle mode items
extern UINT8 numtargets;

INT32 K_StartingBumperCount(void);
boolean K_IsPlayerWanted(player_t *player);
boolean K_IsPlayerMostWanted(player_t *player);
void K_CalculateBattleWanted(void);
void K_SpawnBattlePoints(player_t *source, player_t *victim, UINT8 amount);
void K_CheckBumpers(void);
UINT8 K_NumEmeralds(player_t *player);
void K_RunPaperItemSpawners(void);
void K_SpawnPlayerBattleBumpers(player_t *p);
void K_BattleInit(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
