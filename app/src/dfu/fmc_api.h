/**
*****************************************************************************************
*     Copyright(c) 2017, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
* @file    fmc_api.h
* @brief   This file provides fmc api wrapper for sdk customer.
* @author  yuhungweng
* @date    2020-11-26
* @version v1.0
* *************************************************************************************
*/

#ifndef __FMC_API_H_
#define __FMC_API_H_
#include <stdbool.h>
#include <stdint.h>

/** @defgroup FMC  Flexible Memory Controller
 * @brief
 * @{
 */

/** @defgroup FMC_API_Exported_Constants  FMC API Exported Constants
 * @brief
 * @{
 */
#define FMC_SEC_SECTION_LEN 0x1000

/** End of FMC_API_Exported_Constants
 * @}
 */

/** @defgroup FMC_API_Exported_Types  FMC API Exported Types
 * @brief
 * @{
 */
typedef enum {
    FMC_FLASH_NOR_IDX0,
    FMC_FLASH_NOR_IDX1,
    FMC_FLASH_NOR_IDX2,
    FMC_FLASH_NOR_IDX3
} FMC_FLASH_NOR_IDX_TYPE;

typedef enum {
    FMC_FLASH_NOR_ERASE_CHIP = 1,
    FMC_FLASH_NOR_ERASE_SECTOR = 2,
    FMC_FLASH_NOR_ERASE_BLOCK = 4,
} FMC_FLASH_NOR_ERASE_MODE;

typedef enum {
    PARTITION_FLASH_OCCD,
    PARTITION_FLASH_BOOT_PATCH0,
    PARTITION_FLASH_BOOT_PATCH1,
    PARTITION_FLASH_OTA_BANK_0,
    PARTITION_FLASH_OTA_BANK_1,
    PARTITION_FLASH_SECURE_APP0,
    PARTITION_FLASH_SECURE_APP1,
    PARTITION_FLASH_SECURE_APP_DATA0,
    PARTITION_FLASH_SECURE_APP_DATA1,
    PARTITION_FLASH_RO_DATA1,
    PARTITION_FLASH_RO_DATA2,
    PARTITION_FLASH_RO_DATA3,
    PARTITION_FLASH_RO_DATA4,
    PARTITION_FLASH_RO_DATA5,
    PARTITION_FLASH_RO_DATA6,
    PARTITION_FLASH_BKP_DATA1,
    PARTITION_FLASH_BKP_DATA2,
    PARTITION_FLASH_OTA_TMP,
    PARTITION_FLASH_FTL,

} T_FLASH_PARTITION_NAME;

typedef enum _FLASH_IMG_ID {
    FLASH_IMG_OTA = 0, /* OTA header */
    FLASH_IMG_MCUPATCH = 1,
    FLASH_IMG_MCUAPP = 2,
    FLASH_IMG_MCUCONFIG = 3,
    FLASH_IMG_MCUAPPDATA1 = 4,
    FLASH_IMG_MCUAPPDATA2 = 5,
    FLASH_IMG_MCUAPPDATA3 = 6,
    FLASH_IMG_MCUAPPDATA4 = 7,
    FLASH_IMG_MCUAPPDATA5 = 8,
    FLASH_IMG_MCUAPPDATA6 = 9,
    FLASH_IMG_UPPERSTACK = 10,
    FLASH_IMG_MAX,
} FLASH_IMG_ID;

typedef enum {
    FMC_PSRAM_WB_REFRESH_FULL = 0,
    FMC_PSRAM_WB_REFRESH_BOTTOM_1_2 = 1,
    FMC_PSRAM_WB_REFRESH_BOTTOM_1_4 = 2,
    FMC_PSRAM_WB_REFRESH_BOTTOM_1_8 = 3,
    FMC_PSRAM_WB_REFRESH_NONE = 4,
    FMC_PSRAM_WB_REFRESH_TOP_1_2 = 5,
    FMC_PSRAM_WB_REFRESH_TOP_1_4 = 6,
    FMC_PSRAM_WB_REFRESH_TOP_1_8 = 7,
} FMC_PSRAM_WB_PARTIAL_ARRAY_REFRESH;

typedef enum {
    PSRAM_WB_PARTIAL_ARRAY_SET,
    PSRAM_WB_ENTER_LMP_MODE,
    PSRAM_WB_EXIT_LMP_MODE,
} PSRAM_LMP_CTRL_FUNCTION_CALL;

typedef enum {
    FMC_PSRAM_LPM_STANDBY_MODE,
    FMC_PSRAM_LPM_HALF_SLEEP_MODE,
    FMC_PSRAM_LPM_DEEP_POWER_DOWN_MODE,
} FMC_PSRAM_LPM_TYPE;

/** @defgroup FMC_API_Flash_Callback_Definition  FMC API Flash Callback Definition
 * @ingroup FMC_API_Exported_Types
 * @brief
 * @{
 */
typedef void (*FMC_FLASH_NOR_ASYNC_CB)(void);

/** End of FMC_API_Flash_Callback_Definition
 * @}
 */
/** End of FMC_API_Exported_Types
 * @}
 */

/** @defgroup FMC_API_Exported_Functions  FMC API Exported Functions
 * @brief
 * @{
 */

/**
 * @brief           task-safe of @ref flash_nor_read
 * @param addr      the ram address mapping of nor flash going to be read
 * @param data      data buffer to be read into
 * @param len       read data length
 * @return          true if read successful, otherwise false
 */
bool fmc_flash_nor_read(uint32_t addr, void *data, uint32_t len);

/**
 * @brief           task-safe of @ref flash_nor_write
 * @param addr      the ram address mapping of nor flash going to be written
 * @param data      data buffer to be write into
 * @param len       write data length
 * @return          true if write successful, otherwise false
 */
bool fmc_flash_nor_write(uint32_t addr, void *data, uint32_t len);

/**
 * @brief           task-safe of @ref flash_nor_erase
 * @param addr      the ram address mapping of nor flash going to be erased
 * @param mode      erase mode defined as @ref FMC_FLASH_NOR_ERASE_MODE
 * @return          true if erase successful, otherwise false
 */
bool fmc_flash_nor_erase(uint32_t addr, FMC_FLASH_NOR_ERASE_MODE mode);

/**
 * @brief           task-safe nor flash auto dma read with sequential transaction enabled
 * @param src       the ram address mapping of nor flash going to be read from
 * @param dst       the ram address going to be written to
 * @param len       dma data length
 * @param cb        call back function which is to be executed when dma finishing
 * @return          true if read successful, otherwise false
 */
bool fmc_flash_nor_auto_dma_read(uint32_t src, uint32_t dst, uint32_t len,
                                 FMC_FLASH_NOR_ASYNC_CB cb);

/**
 * @brief           set block protect level with lock
 * @param addr       nor flash addr
 * @param bp_lv     nor flash BP level
 * @return          true if set bp successful, otherwise fals
 */
bool fmc_flash_nor_set_bp_lv(uint32_t addr, uint8_t bp_lv);

/**
 * @brief           get block protect level with lock
 * @param addr      nor flash addr
 * @param bp_lv     nor flash BP level
 * @return          true if get bp successful, otherwise fals
 */
bool fmc_flash_nor_get_bp_lv(uint32_t addr, uint8_t *bp_lv);

/**
 * @brief           get flash partition addr
 * @param name      flash partition name
 * @return          true if get partition successful, otherwise fals
 */
uint32_t flash_partition_addr_get(T_FLASH_PARTITION_NAME name);

/**
 * @brief           get flash partition size
 * @param name      flash partition name
 * @return          true if get partition size successful, otherwise fals
 */
uint32_t flash_partition_size_get(T_FLASH_PARTITION_NAME name);

/**
 * @brief           get image payload flash addr
 * @param id        flash image id number
 * @return          true if get image payload flash addr success, otherwise fals
 */
uint32_t flash_cur_bank_img_payload_addr_get(FLASH_IMG_ID id);

/**
 * @brief           get image header flash addr
 * @param id        flash image id number
 * @return          true if get image header flash addr success, otherwise fals
 */
uint32_t flash_cur_bank_img_header_addr_get(FLASH_IMG_ID id);

// bool fmc_flash_nor_clock_switch(FMC_FLASH_NOR_IDX_TYPE idx, uint32_t required_mhz,
//                                 uint32_t *actual_mhz);

/**
 * @brief           get psram power status
 * @return          true if psram power on
 */
bool get_psram_power_status(void);

/**
 * @brief           shut down spic pad
 * @param id        flash image id number
 * @param enable    true if shut down spic pad
 */
void shut_down_spic_pad_imp(FMC_FLASH_NOR_IDX_TYPE idx, bool enable);

/**
 * @brief           set psram retention partition
 * @param idx       specific psram
 * @param partial   psram  retention partition
 * @return          true if set success
 */
bool fmc_psram_wb_set_partial_refresh(FMC_FLASH_NOR_IDX_TYPE idx,
                                      FMC_PSRAM_WB_PARTIAL_ARRAY_REFRESH partial);

/**
 * @brief           psram enter low power mode
 * @param idx       specific psram
 * @param lpm_mode  psram low power mode
 * @return          true if enter low power mode success
 */
bool fmc_psram_enter_lpm(FMC_FLASH_NOR_IDX_TYPE idx, FMC_PSRAM_LPM_TYPE lpm_mode);

/**
 * @brief           psram exit low power mode
 * @param idx       specific psram
 * @param lpm_mode  psram low power mode
 * @return          true if exit low power mode success
 */
bool fmc_psram_exit_lpm(FMC_FLASH_NOR_IDX_TYPE idx, FMC_PSRAM_LPM_TYPE lpm_mode);

// bool fmc_psram_clock_switch(CLK_FREQ_TYPE clk);

/** End of FMC_API_Exported_Functions
 * @}
 */

/** End of FMC
 * @}
 */

#endif
