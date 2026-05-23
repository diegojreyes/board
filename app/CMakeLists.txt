cmake_minimum_required(VERSION 3.13.1)

set(CONFIG_APPLICATION_DEFINED_SYSCALL true)

set(ZEPHYR_EXTRA_MODULES "${ZMK_EXTRA_MODULES};${CMAKE_CURRENT_SOURCE_DIR}/module;${CMAKE_CURRENT_SOURCE_DIR}/keymap-module")

# ---------------------------------------------------------------------------
# Generate the VERSION file before find_package(Zephyr).
# Extract CONFIG_BT_DIS_FW_REV_STR directly from the SHIELD's .conf file
# to avoid depending on autoconf.h (which does not exist on a first build).
# ---------------------------------------------------------------------------
set(_ZMK_VERSION_STR "")
set(_ZMK_VERSION_CONF_FILES "")

# Collect candidate conf files: shield .conf files for each SHIELD entry, plus prj.conf
if(DEFINED SHIELD)
  string(REPLACE " " ";" _SHIELD_LIST "${SHIELD}")
  foreach(_sh IN LISTS _SHIELD_LIST)
    file(GLOB_RECURSE _sh_confs
         "${CMAKE_CURRENT_SOURCE_DIR}/boards/shields/*/${_sh}.conf")
    list(APPEND _ZMK_VERSION_CONF_FILES ${_sh_confs})
  endforeach()
endif()
list(APPEND _ZMK_VERSION_CONF_FILES "${CMAKE_CURRENT_SOURCE_DIR}/prj.conf")

foreach(_cf IN LISTS _ZMK_VERSION_CONF_FILES)
  if(EXISTS "${_cf}")
    # Tell CMake to re-configure when this conf file changes
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_cf}")
    file(READ "${_cf}" _cf_content)
    string(REGEX MATCH
           "CONFIG_BT_DIS_FW_REV_STR=\"v([0-9]+)\\.([0-9]+)\\.([0-9]+)\""
           _m "${_cf_content}")
    if(_m)
      set(_ZMK_VERSION_STR "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
      set(VERSION_MAJOR ${CMAKE_MATCH_1})
      set(VERSION_MINOR ${CMAKE_MATCH_2})
      set(PATCHLEVEL    ${CMAKE_MATCH_3})
    endif()
  endif()
endforeach()

if(_ZMK_VERSION_STR STREQUAL "")
  # Fall back to the existing VERSION file; if it doesn't exist either, use a default
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    set(VERSION_MAJOR 0)
    set(VERSION_MINOR 0)
    set(PATCHLEVEL    0)
    set(_ZMK_VERSION_STR "1.0.0")
    message(WARNING "CONFIG_BT_DIS_FW_REV_STR not found, using default version 1.0.0")
  endif()
endif()

if(NOT _ZMK_VERSION_STR STREQUAL "")
  set(_NEW_VERSION_CONTENT
      "VERSION_MAJOR = ${VERSION_MAJOR}\nVERSION_MINOR = ${VERSION_MINOR}\nPATCHLEVEL = ${PATCHLEVEL}\nVERSION_TWEAK = 0\nEXTRAVERSION = Zk\n")
  set(_OLD_VERSION_CONTENT "")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _OLD_VERSION_CONTENT)
  endif()
  if(NOT _OLD_VERSION_CONTENT STREQUAL _NEW_VERSION_CONTENT)
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" "${_NEW_VERSION_CONTENT}")
    message(STATUS "VERSION file updated: ${_ZMK_VERSION_STR}")
  else()
    message(STATUS "VERSION file is already up to date: ${_ZMK_VERSION_STR}")
  endif()
endif()

# Find Zephyr. This also loads Zephyr's build system.
find_package(Zephyr REQUIRED HINTS ../zephyr)
project(zmk)

if(CONFIG_ZMK_SLEEP)
  zephyr_linker_sources(SECTIONS include/linker/zmk-pm-devices.ld)
endif()

zephyr_linker_sources(SECTIONS include/linker/zmk-behaviors.ld)
# zephyr_linker_sources(RODATA include/linker/zmk-events.ld)
zephyr_linker_sources(SECTIONS include/linker/zmk-events.ld)
#zephyr_linker_sources(SECTIONS include/linker/itcm.ld)

zephyr_syscall_header(${APPLICATION_SOURCE_DIR}/include/drivers/behavior.h)
zephyr_syscall_header(${APPLICATION_SOURCE_DIR}/include/drivers/ext_power.h)

zephyr_include_directories(${ZEPHYR_BASE}/subsys/bluetooth/host)
# Add your source file to the "app" target. This must come after
# find_package(Zephyr) which defines the target.
target_include_directories(app PRIVATE include)

target_sources(app PRIVATE src/stdlib.c)
# target_sources(app PRIVATE src/activity.c)
target_sources(app PRIVATE src/behavior.c)
target_sources(app PRIVATE src/kscan.c)
target_sources(app PRIVATE src/matrix_transform.c)
target_sources(app PRIVATE src/sensors.c)
target_sources_ifdef(CONFIG_ZMK_WPM app PRIVATE src/wpm.c)
target_sources(app PRIVATE src/event_manager.c)
target_sources_ifdef(CONFIG_ZMK_EXT_POWER app PRIVATE src/ext_power_generic.c)
target_sources(app PRIVATE src/events/activity_state_changed.c)
target_sources(app PRIVATE src/events/position_state_changed.c)
target_sources(app PRIVATE src/events/sensor_event.c)
target_sources(app PRIVATE src/events/mouse_button_state_changed.c)
target_sources(app PRIVATE src/events/led_state_changed.c)
target_sources_ifdef(CONFIG_ZMK_WPM app PRIVATE src/events/wpm_state_changed.c)
target_sources_ifdef(CONFIG_USB_DEVICE_STACK app PRIVATE src/events/usb_conn_state_changed.c)
target_sources(app PRIVATE src/behaviors/behavior_reset.c)
target_sources_ifdef(CONFIG_ZMK_EXT_POWER app PRIVATE src/behaviors/behavior_ext_power.c)
if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources(app PRIVATE src/hid_keychron.c)
  #target_sources_ifdef(CONFIG_ZMK_MOUSE app PRIVATE src/mouse.c)
  target_sources(app PRIVATE src/behaviors/behavior_key_press.c)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_KEY_TOGGLE app PRIVATE src/behaviors/behavior_key_toggle.c)
  target_sources(app PRIVATE src/behaviors/behavior_hold_tap.c)
  target_sources(app PRIVATE src/behaviors/behavior_sticky_key.c)
  target_sources(app PRIVATE src/behaviors/behavior_caps_word.c)
  target_sources(app PRIVATE src/behaviors/behavior_key_repeat.c)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_MACRO app PRIVATE src/behaviors/behavior_macro.c)
  target_sources(app PRIVATE src/behaviors/behavior_momentary_layer.c)
  target_sources(app PRIVATE src/behaviors/behavior_mod_morph.c)
  target_sources(app PRIVATE src/behaviors/behavior_outputs.c)
  target_sources(app PRIVATE src/behaviors/behavior_toggle_layer.c)
  target_sources(app PRIVATE src/behaviors/behavior_to_layer.c)
  target_sources(app PRIVATE src/behaviors/behavior_transparent.c)
  target_sources(app PRIVATE src/behaviors/behavior_none.c)
   target_sources(app PRIVATE src/behaviors/behavior_keychron.c)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_SENSOR_ROTATE app PRIVATE src/behaviors/behavior_sensor_rotate.c)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_SENSOR_ROTATE_VAR app PRIVATE src/behaviors/behavior_sensor_rotate_var.c)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_SENSOR_ROTATE_COMMON app PRIVATE src/behaviors/behavior_sensor_rotate_common.c)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_MOUSE_KEY_PRESS app PRIVATE src/behaviors/behavior_mouse_key_press.c)
  target_sources(app PRIVATE src/combo.c)
  target_sources(app PRIVATE src/behaviors/behavior_tap_dance.c)
  target_sources(app PRIVATE src/behavior_queue.c)
  target_sources(app PRIVATE src/conditional_layer.c)
  target_sources(app PRIVATE src/endpoints.c)
  target_sources(app PRIVATE src/events/endpoint_changed.c)
  target_sources(app PRIVATE src/hid_listener.c)
  target_sources(app PRIVATE src/keymap.c)
  target_sources(app PRIVATE src/events/layer_state_changed.c)
  target_sources(app PRIVATE src/events/modifiers_state_changed.c)
  target_sources(app PRIVATE src/events/keycode_state_changed.c)
  # target_sources(app PRIVATE src/mode_monitor.c)
  target_sources(app PRIVATE src/app_wdt.c)
  target_sources_ifdef(CONFIG_ZMK_HID_INDICATORS app PRIVATE src/hid_indicators.c)
  target_sources_ifdef(CONFIG_ZMK_PPT app PRIVATE src/events/ppt_conn_state_changed.c)
  target_sources_ifdef(CONFIG_ZMK_PPT app PRIVATE src/ppt/keyboard_ppt_app.c)
  target_sources_ifdef(CONFIG_ZMK_PPT app PRIVATE src/ppt/ppt_sync_app.c)
  target_sources_ifdef(CONFIG_ZMK_PPT app PRIVATE src/ppt/ppt_send.c)
  #target_sources_ifdef(CONFIG_ZMK_PPT app PRIVATE src/ppt.c)

  if (CONFIG_ZMK_BLE)
    target_sources(app PRIVATE src/events/ble_active_profile_changed.c)
    target_sources(app PRIVATE src/behaviors/behavior_bt.c)
    target_sources(app PRIVATE src/ble.c)
    target_sources(app PRIVATE src/hog.c)
  endif()
endif()

target_sources(app PRIVATE src/activity.c)

target_sources_ifdef(CONFIG_ZMK_RGB_UNDERGLOW app PRIVATE src/behaviors/behavior_rgb_underglow.c)
target_sources_ifdef(CONFIG_ZMK_BACKLIGHT app PRIVATE src/behaviors/behavior_backlight.c)

target_sources_ifdef(CONFIG_ZMK_BATTERY_REPORTING app PRIVATE src/events/battery_state_changed.c)
target_sources_ifdef(CONFIG_ZMK_BATTERY_REPORTING app PRIVATE src/battery.c)

target_sources_ifdef(CONFIG_ZMK_HID_INDICATORS app PRIVATE src/events/hid_indicators_changed.c)

target_sources_ifdef(CONFIG_ZMK_SPLIT app PRIVATE src/events/split_peripheral_status_changed.c)
add_subdirectory(src/split)

target_sources_ifdef(CONFIG_USB_DEVICE_STACK app PRIVATE src/usb.c)
target_sources_ifdef(CONFIG_ZMK_USB app PRIVATE src/usb_hid.c)
target_sources_ifdef(CONFIG_ZMK_RGB_UNDERGLOW app PRIVATE src/rgb_underglow.c)
target_sources_ifdef(CONFIG_ZMK_BACKLIGHT app PRIVATE src/backlight.c)
target_sources_ifdef(CONFIG_ZMK_LOW_PRIORITY_WORK_QUEUE app PRIVATE src/workqueue.c)
target_sources(app PRIVATE src/main.c)

add_subdirectory(src/display/)
add_subdirectory_ifdef(CONFIG_SETTINGS src/settings/)

add_subdirectory(src/led/)
add_subdirectory_ifdef(CONFIG_RTK_DFU src/dfu/)
add_subdirectory_ifdef(CONFIG_ZMK_LAUNCHER src/launcher)
add_subdirectory_ifdef(CONFIG_LED_STRIP src/rgb)
target_sources_ifdef(CONFIG_LED_STRIP app PRIVATE src/behaviors/behavior_rgb_underglow.c)

#kscan.c
zephyr_code_relocate(FILES src/kscan.c LOCATION ITCM)
# matrix_transform.c
zephyr_code_relocate(FILES src/matrix_transform.c LOCATION ITCM)
# keymap.c
zephyr_code_relocate(FILES src/keymap.c LOCATION ITCM)
# event_manager.c
zephyr_code_relocate(FILES src/event_manager.c LOCATION ITCM)
# keycode_state_changed.c
zephyr_code_relocate(FILES src/events/keycode_state_changed.c LOCATION ITCM)
# position_state_changed.c
zephyr_code_relocate(FILES src/events/position_state_changed.c LOCATION ITCM)
# behavior.c
zephyr_code_relocate(FILES src/behavior.c LOCATION ITCM)
# behavior_queue.c
zephyr_code_relocate(FILES src/behavior_queue.c LOCATION ITCM)
# behavior_key_press.c
if (NOT CONFIG_LOG)
zephyr_code_relocate(FILES src/behaviors/behavior_key_press.c LOCATION ITCM)
zephyr_code_relocate(FILES src/behaviors/behavior_hold_tap.c LOCATION ITCM)
zephyr_code_relocate(FILES src/behaviors/behavior_sticky_key.c LOCATION ITCM)
zephyr_code_relocate(FILES src/combo.c LOCATION ITCM)
zephyr_code_relocate(FILES src/activity.c LOCATION ITCM)
zephyr_code_relocate(FILES src/battery.c LOCATION ITCM)
endif()
#hid_listener.c
zephyr_code_relocate(FILES src/hid_listener.c LOCATION ITCM)
# endpoints.c
zephyr_code_relocate(FILES src/endpoints.c LOCATION ITCM)
# usb_hid.c
zephyr_code_relocate(FILES src/usb_hid.c LOCATION ITCM)
# hid_keychron.c
zephyr_code_relocate(FILES src/hid_keychron.c LOCATION ITCM)
# hog.c
#zephyr_code_relocate(FILES src/hog.c LOCATION ITCM)
# activity.c
zephyr_code_relocate(FILES src/activity.c LOCATION ITCM)
# sensors.c
zephyr_code_relocate(FILES src/sensors.c LOCATION ITCM)
# leds.c
zephyr_code_relocate(FILES src/led/leds.c LOCATION ITCM)
# rgb_matrix_drivers.c
zephyr_code_relocate(FILES src/rgb/rgb_matrix_drivers.c LOCATION ITCM)
# battery.c
zephyr_code_relocate(FILES src/battery.c LOCATION ITCM)
#  ppt_send.c
zephyr_code_relocate(FILES src/ppt/ppt_send.c LOCATION ITCM)

target_sources(app PRIVATE src/mp_test/mp_test.c)
target_sources(app PRIVATE src/mp_test/single_tone.c)

add_subdirectory_ifdef(CONFIG_ZMK_SSD1306 src/ssd1306)
target_sources_ifdef(CONFIG_ZMK_SSD1306 app PRIVATE src/disp.c)
target_sources(app PRIVATE src/gatt_disc.c)

zephyr_cc_option(-Wfatal-errors)


# ---------------------------------------------------------------------------
# After a successful build: prepend header to the firmware image for RTK's
# OTA DFU, which requires a header with metadata.
# The header prepending tool is a available for Windows, Linux x86_64,
# and macOS ARM64.
# ---------------------------------------------------------------------------
if(WIN32)
  set(_PREPEND_HEADER tools/prepend_header/windows-x86_64/prepend_header.exe)
elseif(APPLE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(_PREPEND_HEADER tools/prepend_header/darwin-arm64/prepend_header)
  else()
    message(FATAL_ERROR "Intel Mac is not supported. Please use an Apple Silicon Mac.")
  endif()
else()
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(FATAL_ERROR "Linux ARM is not supported.")
  else()
    set(_PREPEND_HEADER tools/prepend_header/linux-x86_64/prepend_header)
  endif()
endif()

add_custom_target(rtk_post_build ALL
  COMMAND ${_PREPEND_HEADER} -t app_code -p build/zephyr/zmk.bin -m 1 -c sha256 -b 15
  COMMAND ${CMAKE_COMMAND} -E rm -f build/zephyr/zmk_MP.bin
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_dependencies(rtk_post_build zephyr_final)
