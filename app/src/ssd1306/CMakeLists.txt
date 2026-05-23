
target_sources(app PRIVATE ssd1306_1bit.c)
target_sources(app PRIVATE ssd1306_8bit.c)
target_sources(app PRIVATE ssd1306_16bit.c)
target_sources(app PRIVATE ssd1306_fonts.c)
target_sources(app PRIVATE ssd1306_generic.c)
target_sources(app PRIVATE ssd1306_menu.c)

target_sources(app PRIVATE lcd/lcd_common.c)
target_sources(app PRIVATE lcd/oled_ssd1306.c)

target_sources(app PRIVATE intf/i2c/ssd1306_i2c.c)
target_sources(app PRIVATE intf/ssd1306_interface.c)
target_sources(app PRIVATE ssd1306_hal/rtk/platform.c)

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/intf/)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/intf/i2c/)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lcd/)

