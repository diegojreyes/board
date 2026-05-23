/*
 * Copyright (c) 2023 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// SETTINGS_PARTITION must match settings_nvs.c
#if DT_HAS_CHOSEN(zephyr_settings_partition)
#define SETTINGS_PARTITION DT_FIXED_PARTITION_ID(DT_CHOSEN(zephyr_settings_partition))
#else
#define SETTINGS_PARTITION FIXED_PARTITION_ID(storage_partition)
#endif

int zmk_settings_erase(void) {
    LOG_INF("Erasing settings flash partition");

    const struct flash_area *fa;
    int rc = flash_area_open(SETTINGS_PARTITION, &fa);
    if (rc) {
        LOG_ERR("Failed to open settings flash: %d", rc);
        return rc;
    }

    rc = flash_area_erase(fa, 0, fa->fa_size);
    if (rc) {
        LOG_ERR("Failed to erase settings flash: %d", rc);
    }

    flash_area_close(fa);

    return rc;
}


int zmk_settings_erase_at(uint8_t idx) {
    LOG_INF("Erasing settings flash partition");

    const struct flash_area *fa;
    int rc = flash_area_open(SETTINGS_PARTITION, &fa);
    if (rc) {
        LOG_ERR("Failed to open settings flash: %d", rc);
        return rc;
    }
    if(idx*4096 >=fa->fa_size) return -1;
    rc = flash_area_erase(fa, idx*4096, 4096);
    if (rc) {
        LOG_ERR("Failed to erase settings flash: %d", rc);
    }

    flash_area_close(fa);

    return rc;
}

#include <zmk/app_wdt.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>

#define NVS_ZERO_SCAN_SIZE    64
int  zmk_settings_check(void)
{
    const struct flash_area *fa;
    int rc = flash_area_open(SETTINGS_PARTITION, &fa);
    if (rc) {
        LOG_ERR("Failed to open settings flash: %d", rc);
        goto close_ret;
    }
    uint8_t buf[NVS_ZERO_SCAN_SIZE];
    rc =flash_area_read(fa,0,buf,NVS_ZERO_SCAN_SIZE);
    if (rc) {
        // flash_area_close(fa);
        // return rc;
        goto close_ret;
    }
    LOG_HEXDUMP_ERR(buf,16,"flash");
    bool all_zeros=true;
    for(int i =0;i<NVS_ZERO_SCAN_SIZE;i++)
        if(buf[i]!=0)
        {
            all_zeros =false;
        }

    if(all_zeros)
    {
        LOG_ERR("Erasing settings flash partition,size:%d",fa->fa_size);
        rc = flash_area_erase(fa, 0, fa->fa_size);
        if (rc) {
            LOG_ERR("Failed to erase settings flash: %d", rc);
        }
        else {
            // extern bool settings_subsys_initialized;
            // settings_subsys_initialized =false;
            // rc =settings_subsys_init();
            k_msleep(200);
            app_system_reset(WDT_FLAG_RESET_SOC);
        }
    }
close_ret:
    flash_area_close(fa);
    return rc;
}

#ifdef CONFIG_ENABLE_FLASH_PROTECT
#include "trace.h"
#include "patch_header_check.h"
#include "flash_nor_device.h"
uint32_t img_version_to_comparable(T_IMAGE_VERSION version)
{
    return (version.ver_info.img_sub_version._version_major << 28) |
           (version.ver_info.img_sub_version._version_minor << 20) |
           (version.ver_info.img_sub_version._version_revision << 5) |
           (version.ver_info.img_sub_version._version_reserve);
}
int rtk_flash_protect_settings(void)
{
    uint8_t bp_lv=0;
    FLASH_NOR_RET_TYPE rc= flash_nor_get_bp_lv_locked(FLASH_NOR_IDX_SPIC0,&bp_lv);
    DBG_DIRECT("bp_lv :%x,ret:%d",bp_lv,rc);
    T_IMAGE_VERSION t_img_version;
    get_ota_bank_image_version(true,IMG_BOOTPATCH,&t_img_version); 
    DBG_DIRECT("patch ver:%d.%d.%d.%d",
        t_img_version.ver_info.img_sub_version._version_major,
        t_img_version.ver_info.img_sub_version._version_minor,
        t_img_version.ver_info.img_sub_version._version_revision,
        t_img_version.ver_info.img_sub_version._version_reserve);
    uint32_t ver_comp = img_version_to_comparable(t_img_version);
    DBG_DIRECT("patch ver comp:%x",ver_comp);
    if(ver_comp>0x100016ee ) //v1.0.183.14
    {
        if(bp_lv!=6)
        {
            rc = flash_nor_set_bp_lv_locked(FLASH_NOR_IDX_SPIC0, 6);
            DBG_DIRECT("set bplv,ret:%d",rc);
            flash_nor_get_bp_lv_locked(FLASH_NOR_IDX_SPIC0,&bp_lv);
            if(bp_lv ==6)
            DBG_DIRECT("set bplv ok");
        }
        else
        {
            DBG_DIRECT("skip set bplv 6");
        }
    }
    return 0;
}
SYS_INIT(rtk_flash_protect_settings, POST_KERNEL, 1);
#endif 