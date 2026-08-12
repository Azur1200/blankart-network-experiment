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
    extern dboolean initedvoiceprefs;

    void R_SetLocalPreferredVoiceForSkin(uint8_t local_pid,
                                         const char* skin_name,
                                         const char* vox_name);
    kartvoicenum_t R_GetLocalPreferredVoiceForSkin(uint8_t local_pid, const char* skin_name);

    void R_SetLocalPreferredVoiceForSkinByID(uint8_t local_pid, int32_t skin_id, const char* vox_name);
    kartvoicenum_t R_GetLocalPreferredVoiceForSkinByID(uint8_t local_pid, int32_t skin_id);

    int32_t R_MakePreferredVoicesList(uint8_t local_pid,
                                    char* str_ptr,
                                    size_t str_capacity,
                                    int32_t page_num);

    dboolean R_SaveVoicePreferences();
    dboolean R_LoadVoicePreferences();

#ifdef __cplusplus
}
#endif

#endif // __R_VOICEPREF__
