/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/linker/linker-defs.h>

/*
            __event_type_start = .; \
            KEEP(*(".event_type")); \
            __event_type_end = .; \

            __event_subscriptions_start = .; \
            KEEP(*(".event_subscription")); \
            __event_subscriptions_end = .; \
*/

__zmk_events_rom_start = LOADADDR(__zmk_events);
SECTION_PROLOGUE(__zmk_events,,)
{
    __event_type_start = .;

        KEEP(*(".event_type"));
    __event_type_end = .;
    __event_subscriptions_start = .;

    KEEP(*(".event_subscription"));
    __event_subscriptions_end = .;

}
#ifdef CONFIG_XIP
GROUP_DATA_LINK_IN(ITCM , ROMABLE_REGION)
#else
GROUP_DATA_LINK_IN(ITCM , ITCM )
#endif


/*
SECTION_DATA_PROLOGUE(event_type,,SUBALIGN(4))
{
        __event_type_start = .;

        KEEP(*(SORT_BY_NAME(".event_type")))
         __event_type_end = .;

    } GROUP_LINK_IN(RAM)

SECTION_DATA_PROLOGUE(event_subscriptions,,SUBALIGN(4))
    {
          __event_subscriptions_start = .;

          KEEP(*(SORT_BY_NAME(".event_subscription")))
          __event_subscriptions_end = .;

    } GROUP_LINK_IN(RAM)
*/