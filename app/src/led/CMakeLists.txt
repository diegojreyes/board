# Copyright (c) 2020 The ZMK Contributors
# SPDX-License-Identifier: MIT

target_sources(app PRIVATE leds.c)
target_sources_ifdef(CONFIG_ENABLE_GPIO_LED app PRIVATE gpio_leds.c)
