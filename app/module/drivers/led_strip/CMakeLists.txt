# Copyright (c) 2022 The ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library_amend()

zephyr_library_sources_ifdef(CONFIG_CKLED2001 ckled2001.c)
zephyr_library_sources_ifdef(CONFIG_SN3734    sn3734.c)
zephyr_library_sources_ifdef(CONFIG_LED_STRIP_COMPOSITE led_strip_composite.c)
