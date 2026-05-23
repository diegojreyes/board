# Copyright (c) 2020 The ZMK Contributors
# SPDX-License-Identifier: MIT

add_subdirectory_ifdef(CONFIG_GPIO gpio)
add_subdirectory_ifdef(CONFIG_KSCAN kscan)
add_subdirectory_ifdef(CONFIG_SENSOR sensor)
add_subdirectory_ifdef(CONFIG_DISPLAY display)
add_subdirectory_ifdef(CONFIG_CKLED2001 led_strip)
add_subdirectory_ifdef(CONFIG_SN3734 led_strip)
