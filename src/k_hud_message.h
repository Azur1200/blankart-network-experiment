#ifndef __K_HUD_MESSAGE__
#define __K_HUD_MESSAGE__

#include "doomtype.h"
#include "d_player.h"
#include "p_saveg.h"

#ifdef __cplusplus
extern "C" {
#endif

void K_AddMessage(const char *msg, dboolean interrupt, dboolean persist, dboolean hidebackground);
void K_ClearPersistentMessages();
void K_AddMessageForPlayer(player_t *player, const char *msg, dboolean interrupt, dboolean persist, dboolean hidebackground);
void K_ClearPersistentMessageForPlayer(player_t *player);
void K_AddCEchoMessage(char* msg);
void K_TickMessages(void);
void K_DrawMessageFeed(void);

size_t M_MessageLength(const char *s);
size_t M_SeekMessage(const char *s, size_t pos);
char* M_MessageSubString(const char *message, size_t start, size_t end);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
