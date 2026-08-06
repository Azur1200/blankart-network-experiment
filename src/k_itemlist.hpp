#ifndef __K_ITEMLIST__
#define __K_ITEMLIST__

#include "doomdef.h"
#include "k_items.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void K_ClearItemRollLists(void);
    void K_AddItemRollToList(INT32 pid, kartitemtype_e item, INT32 amount);
    fixed_t K_DrawItemList(INT32 pid, fixed_t x, fixed_t y);
    UINT32 K_GetItemListSize(INT32 pid);

    // Getter and setter

    void K_SetItemListEntry(INT32 pid, kartitemtype_e item, INT32 num);
    UINT32 K_GetItemListEntry(INT32 pid, kartitemtype_e item);

    // Netsynch; returns the size of the list buffer, always.
    UINT32 K_SyncItemList(savebuffer_t *save, INT32 pid);

#ifdef __cplusplus
}
#endif

#endif // __K_ITEMLIST__
