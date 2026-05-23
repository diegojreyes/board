/*
 * Copyright (c) 2023 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/linker/linker-defs.h>

#ITERABLE_SECTION_ROM(zmk_behavior_ref, 4)

__zmk_behavior_ref_rom_start =LOADADDR(zmk_behavior_ref_area);
ITERABLE_SECTION_RAM(zmk_behavior_ref, 4)
