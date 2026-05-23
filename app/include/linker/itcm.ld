#include <zephyr/linker/linker-defs.h>

SECTION_PROLOGUE(._itcm_section,,)
{
    #KEEP(*(SORT_BY_ALIGNMENT(.itcm.*)))
    #/home/william/repos/rtk/new/zmk/modules/hal/realtek/bee/drivers/ppt/sync_1to1/lib/rtl87x2g/libppt_sync_master.a:(.text* .rodata* .data* .bss*)
    . = ALIGN(4);
    KEEP(*ppt_sync_app.c.obj(.text* .rodata* .data* .bss*))

} > ITCM AT > FLASH