#include "doomdef.h"
#include "k_items.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void K_ClearItemRollLists(void);
    void K_AddItemRollToList(INT32 pid, kartitemtype_e item, INT32 amount);
    void K_DrawItemList(INT32 pid, fixed_t x, fixed_t y);

#ifdef __cplusplus
}
#endif