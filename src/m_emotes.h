#ifndef __M_EMOTES__
#define __M_EMOTES__

#ifdef __cplusplus
extern "C" {
#endif

#include "doomdef.h"
#include "m_fixed.h"
#include "command.h"

extern consvar_t cv_emotes;

#define MAXEMOTENAME 32
#define MAXEMOTEFRAMES 32
#define EMOTEWIDTH 8

typedef struct emote_s {
	char name[MAXEMOTENAME+1];
	char frames[MAXEMOTEFRAMES][9];
	uint8_t numframes;
	tic_t timeperframe;
	patch_t *frames_cached[MAXEMOTEFRAMES];
} emote_t;

// Reads and adds emotes from EMOTES lump, with syntax similar to soc
void M_LoadEmotes(uint16_t wadnum);

// Load emotes from all base wads
void M_InitEmotes(void);

// Finds first matching emote, ignoring first few matches
emote_t *M_FindEmote(const char *name, int len, int skips);

// If first character is not ':', instantly returns null
// Starts from ':' character, goes until it finds ':' or end of string
// If end of string is found, or closing ':' is found but resulting emote name doesn't exist,
// it returns null, otherwise it returns emote and stores into *emotelen how much characters
// needs to be skipped to get past emote name
emote_t *M_VerifyEmote(const char *name, int *emotelen);

// Draw the emote, animation for animated emotes is done via I_GetTime
#define M_DrawEmote(x, y, emote, flags) M_DrawScaledEmote((x)<<FRACBITS, (y)<<FRACBITS, FRACUNIT, emote, flags)
void M_DrawScaledEmote(fixed_t x, fixed_t y, fixed_t scale, emote_t *emote, int32_t flags);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __M_EMOTES__
