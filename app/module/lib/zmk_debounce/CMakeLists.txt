
zephyr_library()
zephyr_library_sources(debounce.c)

zephyr_code_relocate(FILES debounce.c LOCATION ITCM)