#ifndef __K_ITEMLIST__
#define __K_ITEMLIST__

#include "doomdef.h"
#include "k_items.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void K_ClearItemRollLists(void);
    void K_AddItemRollToList(int32_t pid, kartitemtype_e item, int32_t amount);
    fixed_t K_DrawItemList(int32_t pid, fixed_t x, fixed_t y);
    uint32_t K_GetItemListSize(int32_t pid);

    // Getter and setter

    void K_SetItemListEntry(int32_t pid, kartitemtype_e item, int32_t num);
    uint32_t K_GetItemListEntry(int32_t pid, kartitemtype_e item);

    // Netsynch; returns the size of the list buffer, always.
    uint32_t K_SyncItemList(savebuffer_t *save, int32_t pid);

#ifdef __cplusplus
}
#endif

#endif // __K_ITEMLIST__
