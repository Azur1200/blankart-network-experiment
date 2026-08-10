// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2025 by Kart Krew.
// Copyright (C) 2026 by "yama".
// Copyright (C) 2026 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_voicepreference.hpp
/// \brief Voice preference system and file I/O

#ifndef __R_VOICEPREF__
#define __R_VOICEPREF__

#include "r_skins.h"  // kartvoicenum_t

#ifdef __cplusplus
extern "C"
{
#endif

#define MAXPREFSKINSPERPAGE (10)

#define VOICEPREFSTRSIZE (sizeof("VoicePrefX  \n") + SKINNAMESIZE + VOICENAMESIZE)
#define VOICEPREFBUFSIZE ((VOICEPREFSTRSIZE) * MAXSKINS) + 1

    // Where to save preferences
    extern char preferencesdir[512];
    extern char voicepref_buffer[VOICEPREFBUFSIZE];
    extern boolean initedvoiceprefs;

    void R_SetLocalPreferredVoiceForSkin(UINT8 local_pid,
                                         const char* skin_name,
                                         const char* vox_name);
    kartvoicenum_t R_GetLocalPreferredVoiceForSkin(UINT8 local_pid, const char* skin_name);

    void R_SetLocalPreferredVoiceForSkinByID(UINT8 local_pid, INT32 skin_id, const char* vox_name);
    kartvoicenum_t R_GetLocalPreferredVoiceForSkinByID(UINT8 local_pid, INT32 skin_id);

    INT32 R_MakePreferredVoicesList(UINT8 local_pid,
                                    char* str_ptr,
                                    size_t str_capacity,
                                    INT32 page_num);

    boolean R_SaveVoicePreferences();
    boolean R_LoadVoicePreferences();

#ifdef __cplusplus
}
#endif

#endif // __R_VOICEPREF__
