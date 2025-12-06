#include <unordered_map>

#include "d_player.h"
#include "doomdef.h"
#include "k_items.h"
#include "v_video.h"

extern "C"
{
    std::unordered_map<kartitemtype_e, INT32> itemcounter[MAXPLAYERS];

    void K_ClearItemRollLists(void)
    {
        INT32 i;

        for (i = 0; i < MAXPLAYERS; i++)
        {
            itemcounter[i].clear();
        }
    }

    void K_AddItemRollToList(INT32 pid, kartitemtype_e item, INT32 amount)
    {
        if (auto search = itemcounter[pid].find(item); search != itemcounter[pid].end())
            itemcounter[pid][item] += amount;
        else
            itemcounter[pid][item] = amount;  // If this roll didn't exist before, it does now :^)
    }

#define ITEMLOG_SPACE 1

    // Draws an item in the player's item list. Draws nothing if the list is empty.
    void K_DrawItemList(INT32 pid, fixed_t x, fixed_t y)
    {
        if (itemcounter[pid].size() < 1)
            return;

        patch_t* localpatch;
        fixed_t dx = x;
        INT32 ir_length;

        for (const auto& n : itemcounter[pid])
        {
            if (n.second < 1)
                continue;

            localpatch = K_GetCachedItemPatch(n.first, true, 0);
            ir_length = (INT32)strlen(va("%d", n.second));

            V_DrawFixedPatch(dx, y - (25 * FRACUNIT / 6), FRACUNIT / 3, 0, localpatch, NULL);
            V_DrawSmallString((dx / FRACUNIT) + ((5 + (std::max(0, ir_length - 1) * 3))),
                              (y / FRACUNIT) + 2,
                              V_ALLOWLOWERCASE | V_6WIDTHSPACE,
                              va("%d", n.second));

            dx += (6 + std::max(0, (ir_length * 3) - 3) + ITEMLOG_SPACE) * FRACUNIT;
        }
    }

#undef ITEMLOG_SPACE
}
