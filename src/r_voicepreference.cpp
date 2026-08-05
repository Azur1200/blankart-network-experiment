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
/// \file  r_voicepreference.cpp
/// \brief Voice preference system and file I/O

#include "r_voicepreference.hpp"

#include <string>
#include <unordered_map>
#include <vector>

#include "d_main.h"
#include "d_player.h"  // General player data
#include "doomdef.h"
#include "m_misc.h"
#include "r_skins.h"  // kartvoice
#include "v_video.h"  // Colormaps
#include "z_zone.h"

extern "C"
{
    std::unordered_map<std::string, std::string> localvoiceprefs[MAXSPLITSCREENPLAYERS];

    char preferencesdir[512] = "preferences" PATHSEP;

    char voicepref_buffer[VOICEPREFBUFSIZE] = {0};

    // Assign a preferred voice for a given skin name to a local player.
    void R_SetLocalPreferredVoiceForSkin(UINT8 local_pid,
                                         const char* skin_name,
                                         const char* vox_name)
    {
        if (auto search = localvoiceprefs[local_pid].find(skin_name);
            search != localvoiceprefs[local_pid].end())
        {
            // Voice exists; overwrite the current preference
            localvoiceprefs[local_pid][skin_name].assign(
                vox_name, std::max(static_cast<size_t>(VOICENAMESIZE), strlen(vox_name)));
            return;
        }

        // Entry doesn't exist yet; make a new one
        localvoiceprefs[local_pid][skin_name] = std::string(vox_name);
    }

    // Search through a local (non-netsynched) hashmap for a player's preferred voice for the given
    // skin name. Returns MAXSKINVOICES if nothing is found.
    kartvoicenum_t R_GetLocalPreferredVoiceForSkin(UINT8 local_pid, const char* skin_name)
    {
        if (auto search = localvoiceprefs[local_pid].find(skin_name);
            search != localvoiceprefs[local_pid].end())
        {
            // Preferred voice exists in this map; begin searching through the skin's voices to make
            // sure we can load it in for real
            const std::string found_vox = localvoiceprefs[local_pid][skin_name];

            const INT32 skin_id = R_SkinAvailable(skin_name);

            if (skin_id == -1)
            {
                // Skin does not exist
                return MAXSKINVOICES;
            }

            char check_vox[VOICENAMESIZE + 1] = {0};

            found_vox.copy(check_vox, VOICENAMESIZE);
            check_vox[VOICENAMESIZE] = 0;

            // Return whatever the search result gives us. If it's nothing, it'll return
            // MAXSKINVOICES
            return R_FindIDForVoice(skins[skin_id], check_vox);
        }

        // Nope, no dice!
        return MAXSKINVOICES;
    }

    // Assign a preferred voice for a given skin ID number to a local player.
    void R_SetLocalPreferredVoiceForSkinByID(UINT8 local_pid, INT32 skin_id, const char* vox_name)
    {
        if (skin_id >= numskins)
        {
            // Out of range; do nothing
            return;
        }

        // Preferred voice exists in this map; make sure the skin's loaded in, then overwrite
        const INT32 _skin_id = R_SkinAvailable(skins[skin_id]->name);

        if (_skin_id < 0)  // Doing a less-than-zero check so any garbage data in the negatives gets
                           // ensnared by this...
        {
            // Skin does not exist
            return;
        }

        // Okay, this skin exists! Write them in!
        // This does things unconditionally; if it already exists, the current preference is
        // overwritten. If it *doesn't*, the unordered map will add a new entry. It's a win-win!
        if (auto search = localvoiceprefs[local_pid].find(skins[_skin_id]->name);
            search != localvoiceprefs[local_pid].end())
        {
            // Voice exists; overwrite the current preference
            localvoiceprefs[local_pid][skins[_skin_id]->name].assign(
                vox_name, std::max(static_cast<size_t>(VOICENAMESIZE), strlen(vox_name)));
            return;
        }

        localvoiceprefs[local_pid][skins[_skin_id]->name] = vox_name;
    }

    // Search through a local (non-netsynched) hashmap for a player's preferred voice for the given
    // skin ID. Returns MAXSKINVOICES if nothing is found.
    kartvoicenum_t R_GetLocalPreferredVoiceForSkinByID(UINT8 local_pid, INT32 skin_id)
    {
        if (skin_id >= numskins)
        {
            // Out of range; do nothing
            return MAXSKINVOICES;
        }

        // Preferred voice exists in this map; make sure the skin's loaded in, then overwrite
        const INT32 _skin_id = R_SkinAvailable(skins[skin_id]->name);

        if (_skin_id < 0)  // Doing a less-than-zero check so any garbage data in the negatives gets
                           // ensnared by this...
        {
            // Skin does not exist
            return MAXSKINVOICES;
        }

        // We are now 100% sure the skin 100% exists. Make sure it exists in the player's preference
        // list
        if (auto search = localvoiceprefs[local_pid].find(skins[_skin_id]->name);
            search != localvoiceprefs[local_pid].end())
        {
            // Preferred voice exists in this map; begin searching through the skin's voices to make
            // sure we can load it in for real
            const std::string_view found_vox = localvoiceprefs[local_pid][skins[_skin_id]->name];

            char check_vox[VOICENAMESIZE + 1] = {0};

            found_vox.copy(check_vox, VOICENAMESIZE);
            check_vox[VOICENAMESIZE] = 0;

            // Return whatever the search result gives us. If it's nothing, it'll return
            // MAXSKINVOICES
            return R_FindIDForVoice(skins[_skin_id], check_vox);
        }

        // Nope, no dice!
        return MAXSKINVOICES;
    }

    // Clones a list of a the given local player's preferred voices to a string pointer.
    // Returns true or false if any data is written to the string in question.
    INT32 R_MakePreferredVoicesList(UINT8 local_pid,
                                    char* str_ptr,
                                    size_t str_capacity,
                                    INT32 page_num)
    {
        std::string output = "";
        char color_prefix[2] = {0};
        char temp_skin_name[SKINNAMESIZE + 1] = {0};
        char temp_vox_name[VOICENAMESIZE + 1] = {0};

        UINT16 chatcolor = 0;
        INT32 skin_id = -1;
        INT32 i = 0;
        const INT32 real_page_num = std::max(0, page_num - 1);
        const INT32 preflistsize = static_cast<INT32>(localvoiceprefs[local_pid].size());
        boolean noskin = false;

        INT32 numpages = (preflistsize / MAXPREFSKINSPERPAGE) + 1;

        if (page_num > numpages)
        {
            output += va("There %s only %d %s.\n",
                         (numpages != 1) ? "are" : "is",
                         numpages,
                         (numpages != 1) ? "pages" : "page");

            memset(str_ptr, 0, str_capacity);

            // After everything, copy the output to our destination pointer
            output.copy(str_ptr, str_capacity);

            return static_cast<INT32>(output.size());
        }

        for (const auto& n : localvoiceprefs[local_pid])
        {
            i++;
            noskin = false;

            if (i < (MAXPREFSKINSPERPAGE * real_page_num))
                continue;  // Not at the initial entry for this page yet.

            if (i > (MAXPREFSKINSPERPAGE))
                break;  // Duck out immediately

            memset(temp_skin_name, 0, sizeof(temp_skin_name));

            n.first.copy(temp_skin_name, SKINNAMESIZE);
            temp_skin_name[SKINNAMESIZE] = 0;

            skin_id = R_SkinAvailable(temp_skin_name);

            if (skin_id < 0)  // Doing a less-than-zero check so any garbage data in the negatives
                              // gets ensnared by this...
            {
                // Skin does not exist; print a greyed-out version of their codename
                noskin = true;
            }

            memset(temp_vox_name, 0, sizeof(temp_vox_name));

            n.second.copy(temp_vox_name, VOICENAMESIZE);
            temp_vox_name[VOICENAMESIZE] = 0;

            chatcolor = skincolors[skins[skin_id]->prefcolor].chatcolor;

            if (chatcolor > V_TANMAP)
            {
                sprintf(color_prefix, "%c", '\x80');
            }
            else
            {
                sprintf(color_prefix, "%c", '\x80' + (chatcolor >> V_CHARCOLORSHIFT));
            }

            color_prefix[1] = 0;

            // Write the entire output in a single go
            if (noskin)
                output +=
                    va("\x82Local player %d's preferred voice for \x86\"%s\" (skin not "
                       "loaded)\x82:\x80 %s\n\n",
                       local_pid + 1,
                       temp_skin_name,
                       temp_vox_name);
            else
                output +=
                    va("\x82Local player %d's preferred voice for %s%s\x82 (\x80\"%s\"\x82):\x80 "
                       "%s\n\n",
                       local_pid + 1,
                       color_prefix,
                       skins[skin_id]->realname,
                       skins[skin_id]->name,
                       temp_vox_name);
        }

        if (output.size() <= 0)
        {
            // Do absolutely nothing and return
            return -1;
        }

        // Write out our page count
        output += va("Total %d %s. Page (%d/%d)\n",
                     preflistsize,
                     (preflistsize != 1) ? "preferences" : "preference",
                     page_num,
                     numpages);

        // For debug/size estimation; we have 4096 skins, man.
        // CONS_Printf("Final output string size: %d\n", static_cast<INT32>(output.size()));

        // After everything, copy the output to our destination pointer
        output.copy(str_ptr, str_capacity);

        // Finally, return the signal that we succeeded
        return static_cast<INT32>(output.size());
    }

    // Saves voice preferences to a file.
    // Returns false if any of the four files fail to save; returns true otherwise.
    boolean R_SaveVoicePreferences()
    {
        static char voxpref_file[MAXSPLITSCREENPLAYERS][21] = {"blanpreferences.cfg",
                                                               "blanpreferences2.cfg",
                                                               "blanpreferences3.cfg",
                                                               "blanpreferences4.cfg"};

        FILE* f = NULL;

        char currconfig[MAX_WADPATH + 4] = {0};
        char backupfile[MAX_WADPATH + 4] = {0};

        char temp_skin_name[SKINNAMESIZE + 1] = {0};
        char temp_vox_name[VOICENAMESIZE + 1] = {0};

        INT32 i = 0;
        INT32 skin_id = 0;
        std::string file_output = "";
        boolean noskin = false;

        M_MkdirEach(preferencesdir, M_PathParts(preferencesdir) - 2, 0755);

        INT32 snum = 0;
        for (snum = 0; snum < MAXSPLITSCREENPLAYERS; snum++)
        {
            file_output.clear();

            // Clear out config directory
            memset(currconfig, 0, sizeof(currconfig));
            memset(backupfile, 0, sizeof(backupfile));

            // Write a new config directory
            snprintf(currconfig, sizeof(currconfig), "%s%s", preferencesdir, voxpref_file[snum]);
            currconfig[sizeof(currconfig) - 1] = '\0';

            // Create backup of the config file
            snprintf(backupfile, sizeof(backupfile), "%s.bak", currconfig);
            backupfile[sizeof(backupfile) - 1] = '\0';

            FILE* config = fopen(currconfig, "r");

            if (config != NULL)
            {
                fclose(config);
                if (FIL_CopyFile(currconfig, backupfile) == false)
                {
                    CONS_Alert(CONS_WARNING,
                               "Failed to create a backup of the configuration file. Will not "
                               "attempt to write to file.\n");
                    return false;
                }
            }

            if (!strstr(currconfig, ".cfg"))
            {
                CONS_Alert(CONS_NOTICE, M_GetText("Config filename must be .cfg\n"));
                return false;
            }

            f = fopen(currconfig, "w");
            if (!f)
            {
                CONS_Alert(CONS_ERROR,
                           M_GetText("Couldn't save game config file %s for player %d\n"),
                           currconfig,
                           snum + 1);
                return false;
            }

            fprintf(f, "%s", va("// BlanKart preferences file for local player %d.\n", snum + 1));
            fprintf(
                f,
                "// Due to the nature of the unordered map system, data here may be shuffled!\n");

            // Save our voice
            fprintf(f, "Voice %s\n", localvoicedata[snum].name);

            i = 0;
            for (const auto& n : localvoiceprefs[snum])
            {
                noskin = false;
                i++;

                memset(voicepref_buffer, 0, sizeof(voicepref_buffer));
                memset(temp_skin_name, 0, sizeof(temp_skin_name));

                n.first.copy(temp_skin_name, SKINNAMESIZE);
                temp_skin_name[SKINNAMESIZE] = 0;

                skin_id = R_SkinAvailable(temp_skin_name);

                if (skin_id < 0)  // Doing a less-than-zero check so any garbage data in the
                                  // negatives gets ensnared by this...
                {
                    // Skin does not exist; write the last name we remember it being
                    noskin = true;
                }

                memset(temp_vox_name, 0, sizeof(temp_vox_name));

                n.second.copy(temp_vox_name, VOICENAMESIZE);
                temp_vox_name[VOICENAMESIZE] = 0;

                // Write the entire output in a single go
                file_output += va("VoicePref %s %s\n",
                                  (noskin) ? temp_skin_name : skins[skin_id]->name,
                                  temp_vox_name);
            }

            if (file_output.size() > 0)
            {
                file_output.copy(voicepref_buffer, VOICEPREFBUFSIZE);
                voicepref_buffer[VOICEPREFBUFSIZE - 1] = 0;

                fprintf(f, "%s",voicepref_buffer);
            }

            fclose(f);
        }

        return true;
    }

    // Loads voice preferences from a file.
    // Returns false if all 4 players fail to load a config; true otherwise.
    boolean R_LoadVoicePreferences()
    {
        static char voxpref_file[MAXSPLITSCREENPLAYERS][21] = {"blanpreferences.cfg",
                                                               "blanpreferences2.cfg",
                                                               "blanpreferences3.cfg",
                                                               "blanpreferences4.cfg"};

        FILE* f = NULL;

        char currconfig[MAX_WADPATH + 4] = {0};
        char backupfile[MAX_WADPATH + 4] = {0};

        char buffer[MAXLINELEN] = {0};

        char* word;
        char* word2;
        char* word3;

        INT32 i = 0;
        INT32 snum = 0;
        INT32 strikes = 0;

        std::vector<std::string> data_to_load[2];
        char name_buffer[2][VOICENAMESIZE] = {{0}};

        for (snum = 0; snum < MAXSPLITSCREENPLAYERS; snum++)
        {
            // Clear out config directory
            memset(currconfig, 0, sizeof(currconfig));
            memset(backupfile, 0, sizeof(backupfile));

            // Write a new config directory
            snprintf(currconfig, sizeof(currconfig), "%s%s", preferencesdir, voxpref_file[snum]);
            currconfig[sizeof(currconfig) - 1] = '\0';

            f = fopen(currconfig, "r");

            if (!f)
            {
                CONS_Alert(CONS_WARNING,
                           M_GetText("Could not open preferences for player %d\n"),
                           snum + 1);
                strikes++;
                continue;
            }

            for (i = 0; fgets(buffer, (int)sizeof(buffer), f); i++)
            {
                word = strtok(buffer, " ");

                if (word)
                    strupr(word);
                else
                    continue;

                if (fastcmp(word, "//"))
                {
                    // This is a comment, ignore it
                    continue;
                }

                if (fastcmp(word, "VOICE"))
                {
                    word2 = strtok(NULL, " \t\r\n");

                    if (word2)
                    {
                        memset(localvoicedata[snum].name, 0, sizeof(localvoicedata[snum].name));
                        strncpy(localvoicedata[snum].name, word2, strlen(word2));
                    }

                    continue;
                }
                else if (fastcmp(word, "VOICEPREF"))
                {
                    word2 = strtok(NULL, " ");

                    if (word2)
                    {
                        word3 = strtok(NULL, " \t\r\n");

                        if (word3)
                        {
                            data_to_load[0].push_back(std::string(word2));
                            data_to_load[1].push_back(std::string(word3));
                        }
                    }

                    continue;
                }
            }

            fclose(f);

            // Load in the data, back-to-front to avoid annoying shuffling
            while (data_to_load[0].size() > 0)
            {
                i = static_cast<INT32>(data_to_load[0].size() - 1);
                memset(name_buffer[0], 0, sizeof(name_buffer[0]));
                memset(name_buffer[1], 0, sizeof(name_buffer[0]));

                // Set the buffer data
                data_to_load[0].at(i).copy(name_buffer[0], sizeof(name_buffer[0]));
                data_to_load[1].at(i).copy(name_buffer[1], sizeof(name_buffer[1]));

                name_buffer[0][sizeof(name_buffer[0]) - 1] = 0;
                name_buffer[1][sizeof(name_buffer[1]) - 1] = 0;

                // Pop the entry from the array
                data_to_load[0].pop_back();
                data_to_load[1].pop_back();

                // Set preference in-game
                R_SetLocalPreferredVoiceForSkin(snum, name_buffer[0], name_buffer[1]);
            }
        }

        return (strikes >= 4) ? false : true;
    }
}
