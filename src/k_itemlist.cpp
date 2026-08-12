#include <unordered_map>

#include "byteptr.h"
#include "d_player.h"
#include "doomdef.h"
#include "k_items.h"
#include "p_saveg.h"
#include "v_video.h"

extern "C"
{
    std::unordered_map<kartitemtype_e, int32_t> itemcounter[MAXPLAYERS];

    //
    //  Clears out all item lists for all players. Should only run during race setup.
    //
    void K_ClearItemRollLists(void)
    {
        int32_t i;

        for (i = 0; i < MAXPLAYERS; i++)
        {
            itemcounter[i].clear();
        }
    }

    //
    //  Adds an item roll to the item roll list.
    //  If an entry for the item already exists, this function adds to its roll count.
    //
    void K_AddItemRollToList(int32_t pid, kartitemtype_e item, int32_t amount)
    {
        if (auto search = itemcounter[pid].find(item); search != itemcounter[pid].end())
            itemcounter[pid][item] += amount;
        else
            itemcounter[pid][item] = amount;  // If this roll didn't exist before, it does now :^)
    }

    //
    //  Gets the size of the item roll list. Primarily used for syncing netgames.
    //
    uint32_t K_GetItemListSize(int32_t pid)
    {
        return static_cast<uint32_t>(itemcounter[pid].size());
    }

    //
    //  Sets an entry in the item roll list to a specific value.
    //
    void K_SetItemListEntry(int32_t pid, kartitemtype_e item, int32_t num)
    {
        itemcounter[pid][item] = num;
    }

    //
    //  Gets the value of an entry in the item roll list, given it exists.
    //  If nothing is found, this value returns 0.
    //
    uint32_t K_GetItemListEntry(int32_t pid, kartitemtype_e item)
    {
        if (auto search = itemcounter[pid].find(item); search != itemcounter[pid].end())
        {
            return itemcounter[pid][item];
        }

        return 0;
    }

    //
    //  C and C++ are like oil and water at points; we need to synch every entry in the unordered
    //  map by doing... this.
    //
    uint32_t K_SyncItemList(savebuffer_t* save, int32_t pid)
    {
        uint32_t buffer_size;

        if (save->write)
        {
            // Buffer size = list size * 2 (2 entries for each buffer element)
            buffer_size = static_cast<uint32_t>(itemcounter[pid].size());

            // First, write the size of the buffer we're about to send over.
            WRITEUINT32(save->p, buffer_size);

            // Then, comb through the map and write every entry in it to the save buffer.
            for (const auto& n : itemcounter[pid])
            {
                WRITEUINT32(save->p, static_cast<uint32_t>(n.first));
                WRITEUINT32(save->p, static_cast<uint32_t>(n.second));
            }

            return buffer_size;
        }
        else
        {
            // Get the buffer size.
            buffer_size = READUINT32(save->p);

            uint32_t i, item, amt;
            for (i = 0; i < buffer_size; i++)
            {
                // Loop through the save buffer and write everything we find into the given item
                // list.
                item = READUINT32(save->p);
                amt = READUINT32(save->p);

                itemcounter[pid][static_cast<kartitemtype_e>(item)] = amt;
            }

            return buffer_size;
        }

        return 0;
    }

#define ITEMLOG_SPACE 1

    // Draws an item in the player's item list. Draws nothing if the list is empty.
    fixed_t K_DrawItemList(int32_t pid, fixed_t x, fixed_t y)
    {
        if (itemcounter[pid].size() < 1)
            return 0;

        patch_t* localpatch;
        fixed_t dx = x;
        int32_t ir_length;

        for (const auto& n : itemcounter[pid])
        {
            if (n.second < 1)
                continue;

            localpatch = K_GetCachedItemPatch(n.first, true, 0);
            ir_length = static_cast<int32_t>(strlen(va("%d", n.second)));

            V_DrawFixedPatch(dx, y - (25 * FRACUNIT / 6), FRACUNIT / 3, 0, localpatch, NULL);

            V_DrawSmallString((dx / FRACUNIT) + ((5 + (std::max(0, ir_length - 1) * 3))),
                              (y / FRACUNIT) + 2,
                              V_ALLOWLOWERCASE | V_6WIDTHSPACE,
                              va("%d", n.second));

            dx += (6 + std::max(0, (ir_length * 3) - 3) + ITEMLOG_SPACE) * FRACUNIT;
        }

        // Return the overall "width" of the list.
        return dx - x;
    }

#undef ITEMLOG_SPACE
}
