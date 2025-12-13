#include <unordered_map>

#include "byteptr.h"
#include "d_player.h"
#include "doomdef.h"
#include "k_items.h"
#include "p_saveg.h"
#include "v_video.h"

extern "C"
{
    std::unordered_map<kartitemtype_e, INT32> itemcounter[MAXPLAYERS];

    //
    //  Clears out all item lists for all players. Should only run during race setup.
    //
    void K_ClearItemRollLists(void)
    {
        INT32 i;

        for (i = 0; i < MAXPLAYERS; i++)
        {
            itemcounter[i].clear();
        }
    }

    //
    //  Adds an item roll to the item roll list.
    //  If an entry for the item already exists, this function adds to its roll count.
    //
    void K_AddItemRollToList(INT32 pid, kartitemtype_e item, INT32 amount)
    {
        if (auto search = itemcounter[pid].find(item); search != itemcounter[pid].end())
            itemcounter[pid][item] += amount;
        else
            itemcounter[pid][item] = amount;  // If this roll didn't exist before, it does now :^)
    }

    //
    //  Gets the size of the item roll list. Primarily used for syncing netgames.
    //
    UINT32 K_GetItemListSize(INT32 pid)
    {
        return static_cast<UINT32>(itemcounter[pid].size());
    }

    //
    //  Sets an entry in the item roll list to a specific value.
    //
    void K_SetItemListEntry(INT32 pid, kartitemtype_e item, INT32 num)
    {
        itemcounter[pid][item] = num;
    }

    //
    //  Gets the value of an entry in the item roll list, given it exists.
    //  If nothing is found, this value returns 0.
    //
    UINT32 K_GetItemListEntry(INT32 pid, kartitemtype_e item)
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
    UINT32 K_SyncItemList(savebuffer_t* save, INT32 pid)
    {
        UINT32 buffer_size;

        if (save->write)
        {
            // Buffer size = list size * 2 (2 entries for each buffer element)
            buffer_size = static_cast<UINT32>(itemcounter[pid].size());

            // First, write the size of the buffer we're about to send over.
            WRITEUINT32(save->p, buffer_size);

            // Then, comb through the map and write every entry in it to the save buffer.
            for (const auto& n : itemcounter[pid])
            {
                WRITEUINT32(save->p, static_cast<UINT32>(n.first));
                WRITEUINT32(save->p, static_cast<UINT32>(n.second));
            }

            return buffer_size;
        }
        else
        {
            // Get the buffer size.
            buffer_size = READUINT32(save->p);

            UINT32 i, item, amt;
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
    fixed_t K_DrawItemList(INT32 pid, fixed_t x, fixed_t y)
    {
        if (itemcounter[pid].size() < 1)
            return 0;

        patch_t* localpatch;
        fixed_t dx = x;
        INT32 ir_length;

        for (const auto& n : itemcounter[pid])
        {
            if (n.second < 1)
                continue;

            localpatch = K_GetCachedItemPatch(n.first, true, 0);
            ir_length = static_cast<INT32>(strlen(va("%d", n.second)));

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
