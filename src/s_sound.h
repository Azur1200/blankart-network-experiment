// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  s_sound.h
/// \brief The not so system specific sound interface

#ifndef __S_SOUND__
#define __S_SOUND__

#include "doomtype.h"
#include "i_sound.h" // musictype_t
#include "sounds.h"
#include "m_fixed.h"
#include "command.h"
#include "tables.h" // angle_t

#ifdef HAVE_OPENMPT
#include "libopenmpt/libopenmpt.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_OPENMPT
extern openmpt_module *openmpt_mhandle;
#endif

// mask used to indicate sound origin is player item pickup
#define PICKUP_SOUND 0x8000

extern consvar_t stereoreverse;
extern consvar_t cv_soundvolume, cv_closedcaptioning, cv_digmusicvolume; 

extern consvar_t surround;
extern consvar_t cv_numChannels;

extern consvar_t cv_gamedigimusic;

extern consvar_t cv_gamesounds;
extern consvar_t cv_playmusicifunfocused;
extern consvar_t cv_playsoundifunfocused;

extern consvar_t cv_music_resync_threshold;
extern consvar_t cv_music_resync_powerups_only;
extern consvar_t cv_altmusic;
extern consvar_t cv_streamersafemusic;

extern char musicqueue_string[];
extern int64_t musicqueue_time;
extern dboolean musicqueue_loops;
extern uint16_t musicqueue_flags;

#ifdef HAVE_OPENMPT
extern consvar_t cv_modfilter;
#endif

extern CV_PossibleValue_t soundvolume_cons_t[];

typedef enum
{
	SF_TOTALLYSINGLE =  1, // Only play one of these sounds at a time...GLOBALLY
	SF_NOMULTIPLESOUND =  2, // Like SF_NOINTERRUPT, but doesnt care what the origin is
	SF_OUTSIDESOUND  =  4, // Volume is adjusted depending on how far away you are from 'outside'
	SF_X4AWAYSOUND   =  8, // Hear it from 4x the distance away
	SF_X8AWAYSOUND   = 16, // Hear it from 8x the distance away
	SF_NOINTERRUPT   = 32, // Only play this sound if it isn't already playing on the origin
	SF_X2AWAYSOUND   = 64, // Hear it from 2x the distance away
} soundflags_t;

struct listener_t {
	fixed_t x, y, z;
	fixed_t momx, momy, momz;
	angle_t angle;
};

struct channel_t
{
	// sound information (if null, channel avail.)
	sfxinfo_t *sfxinfo;

	// origin of sound
	const void *origin;

	// initial volume of sound, which is applied after distance and direction
	int32_t volume;

	// handle of the sound being played
#ifdef DUALSNDBUFFER
	int32_t handleL;
	int32_t handleR;
#else
	int32_t handle;
#endif
	// length sound has been playing
	float duration;
	float endtime;

	// EFX
	efx_t *efx;
	float efxtail;
};

struct caption_t {
	channel_t *c;
	sfxinfo_t *s;
	uint16_t t;
	uint8_t b;
};

#define NUMCAPTIONS 8
#define MAXCAPTIONTICS (2*TICRATE)
#define CAPTIONFADETICS 20

extern caption_t closedcaptions[NUMCAPTIONS];
void S_StartCaption(sfxenum_t sfx_id, int32_t cnum, uint16_t lifespan);
void S_ResetCaptions(void);

// register sound vars and commands at game startup
void S_RegisterSoundStuff(void);

//
// Initializes sound stuff, including volume
// Sets channels, SFX, allocates channel buffer, sets S_sfx lookup.
//
void S_InitSfxChannels(int32_t sfxVolume);

//
// Per level startup code.
// Kills playing sounds at start of level, determines music if any, changes music.
//
void S_StopSounds(void);
void S_ClearSfx(void);
void S_InitLevelMusic(dboolean fromnetsave);

//
// Basically a W_GetNumForName that adds "ds" at the beginning of the string. Returns a lumpnum.
//
lumpnum_t S_GetSfxLumpNum(sfxinfo_t *sfx);

//
// Sound Status
//

dboolean S_SoundDisabled(void);

//
// Start sound for thing at <origin> using <sound_id> from sounds.h
//
void S_StartSound(const void *origin, sfxenum_t sound_id);

// Will start a sound at a given volume.
void S_StartSoundAtVolumeEx(const void *origin_p, sfxenum_t sfx_id, int32_t volume, efx_t *efx);
#define S_StartSoundAtVolume(origin, sound_id, volume) S_StartSoundAtVolumeEx(origin, sound_id, volume, NULL)

// Stop sound for thing at <origin>
void S_StopSound(void *origin);

//
// Music Status
//

dboolean S_DigMusicDisabled(void);
dboolean S_MusicDisabled(void);
dboolean S_MusicPlaying(void);
dboolean S_MusicPaused(void);
dboolean S_MusicStopped(void);
dboolean S_MusicNotInFocus(void);
musictype_t S_MusicType(void);
const char *S_MusicTypeName(musictype_t mustype);
const char *S_MusicName(void);

dboolean S_MusicExists(const char *mname);
dboolean S_MusicInfo(char *mname, uint16_t *mflags, dboolean *looping);


//
// Music Effects
//

// Set Speed of Music
dboolean S_SpeedMusic(float speed);

#define MAXDEFTRACKS 3

// Music credits
struct musicdef_t
{
	char name[MAXDEFTRACKS][7];
	uint32_t hash[MAXDEFTRACKS];
	uint8_t numtracks;
	char *title;
	char *author;
	char *source;
	char *composers;
	char *usage;
	int volume;
	dboolean contentidunsafe;
	dboolean legacy;
	musicdef_t *next;
};

extern struct cursongcredit
{
	char *text;
	uint16_t anim;
	uint8_t trans;
	fixed_t x;
	fixed_t old_x;
	fixed_t y;
	int32_t snapflags;
} cursongcredit;

extern char *g_realsongcredit;

extern musicdef_t soundtestsfx;
extern musicdef_t *musicdefstart;
extern musicdef_t **soundtestdefs;
extern int32_t numsoundtestdefs;
extern uint8_t soundtestpage;
extern int musicdef_volume;

void S_LoadMusicDefs(uint16_t wadnum);
void S_InitMusicDefs(void);
musicdef_t *S_FindMusicDef(const char *name, uint8_t *i);
void S_LoadMusicCredit(void);
void S_UnloadMusicCredit(void);
void S_ShowMusicCredit(fixed_t yoffset, uint16_t animtime, int32_t snapflags);
void S_ChangeMusicCreditSettings(int32_t yoffset, int32_t snapflags);
void S_StopMusicCredit(void);
dboolean S_PrepareSoundTest(void);

//
// Music Seeking
//

// Get Length of Music
uint32_t S_GetMusicLength(void);

// Set LoopPoint of Music
dboolean S_SetMusicLoopPoint(uint32_t looppoint);

// Get LoopPoint of Music
uint32_t S_GetMusicLoopPoint(void);

// Set Position of Music
dboolean S_SetMusicPosition(uint32_t position);

// Get Position of Music
uint32_t S_GetMusicPosition(void);

//
// Music Stacking (Jingles)
//

struct musicstack_t
{
	char musname[7];
	uint16_t musflags;
	dboolean looping;
	uint32_t position;
	tic_t tic;
	uint16_t status;
	lumpnum_t mlumpnum;
	dboolean noposition; // force music stack resuming from zero (like music_stack_noposition)

    musicstack_t *prev;
    musicstack_t *next;
};

extern char music_stack_nextmusname[7];
extern dboolean music_stack_noposition;
extern uint32_t music_stack_fadeout;
extern uint32_t music_stack_fadein;

void S_SetStackAdjustmentStart(void);
void S_AdjustMusicStackTics(void);
void S_RetainMusic(const char *mname, uint16_t mflags, dboolean looping, uint32_t position, uint16_t status);
dboolean S_RecallMusic(uint16_t status, dboolean fromfirst);

//
// Music Playback
//

/* this is for the sake of the hook */
struct MusicChange {
	char    * newname;
	uint16_t  * mflags;
	dboolean * looping;
	uint32_t  * position;
	uint32_t  * prefadems;
	uint32_t  * fadeinms;
};

enum
{
	MUS_SPECIAL = 1,/* powerups--invincibility, grow */
};

// Start music track, arbitrary, given its name, and set whether looping
// note: music flags 12 bits for tracknum (gme, other formats with more than one track)
//       13-15 aren't used yet
//       and the last bit we ignore (internal game flag for resetting music on reload)
void S_ChangeMusicEx(const char *mmusic, uint16_t mflags, dboolean looping, uint32_t position, uint32_t prefadems, uint32_t fadeinms);
#define S_ChangeMusicInternal(a,b) S_ChangeMusicEx(a,0,b,0,0,0)
#define S_ChangeMusic(a,b,c) S_ChangeMusicEx(a,b,c,0,0,0)

void S_SetMusicQueue(const char *mmusic, dboolean looping, tic_t time);
void S_ClearMusicQueue(void);
void S_TickMusicQueue(void);

void S_ChangeMusicSpecial (const char *mmusic);

void S_SetRestoreMusicFadeInCvar (consvar_t *cvar);
#define S_ClearRestoreMusicFadeInCvar() \
	S_SetRestoreMusicFadeInCvar(0)
int  S_GetRestoreMusicFadeIn (void);

void S_SetMusicUsage (int type);
int  S_MusicUsage (void);

// Stops the music.
void S_StopMusic(void);

// Stop and resume music, during game PAUSE.
void S_PauseAudio(void);
void S_ResumeAudio(void);

// Enable and disable sound effects
void S_EnableSound(void);
void S_DisableSound(void);

//
// Music Fading
//

void S_SetInternalMusicVolume(int32_t volume);
void S_StopFadingMusic(void);
dboolean S_FadeMusicFromVolume(uint8_t target_volume, int16_t source_volume, uint32_t ms);
#define S_FadeMusic(a, b) S_FadeMusicFromVolume(a, -1, b)
#define S_FadeInChangeMusic(a,b,c,d) S_ChangeMusicEx(a,b,c,0,0,d)
dboolean S_FadeOutStopMusic(uint32_t ms);

//
// Updates music & sounds
//
void S_UpdateSounds(void);
void S_UpdateClosedCaptions(void);

FUNCMATH fixed_t S_CalculateSoundDistance(fixed_t px1, fixed_t py1, fixed_t pz1, fixed_t px2, fixed_t py2, fixed_t pz2);

int32_t S_GetSoundVolume(sfxinfo_t *sfx, int32_t volume);

void S_SetSfxVolume(int32_t volume);
void S_SetMusicVolume(int32_t digvolume);
#define S_SetDigMusicVolume S_SetMusicVolume
#define S_InitMusicVolume() S_SetMusicVolume(-1)

int32_t S_OriginPlaying(void *origin);
int32_t S_IdPlaying(sfxenum_t id);
int32_t S_SoundPlaying(void *origin, sfxenum_t id);

void S_StartSoundName(void *mo, const  char *soundname);

void S_StopSoundByID(void *origin, sfxenum_t sfx_id);
void S_StopSoundByNum(sfxenum_t sfxnum);

// EFX
void S_InitEFXArray(efx_t *efx);
void S_CreateGlobalEFX(efx_t *efx);
void S_DeleteGlobalEFX(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
