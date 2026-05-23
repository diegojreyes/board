/**
*********************************************************************************************************
*               Copyright(c) 2018, Realtek Semiconductor Corporation. All rights reserved.
*********************************************************************************************************
* @file      mouse_qdecoder.h
* @brief
* @details
* @author
* @date
* @version   v1.0
* *********************************************************************************************************
*/

#ifndef _QDEC_DRIVER_H__
#define _QDEC_DRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtl876x.h>
// #include <rtl_utils.h>

#define QDEC_EN 1

#if QDEC_EN

/*============================================================================*
 *                         Macros
 *============================================================================*/
#ifdef QDEC_PRINT_LOG
#define QDEC_DBG_BUFFER(MODULE, LEVEL, fmt, para_num, ...)                                         \
    DBG_BUFFER_##LEVEL(TYPE_BEE3, SUBTYPE_FORMAT, MODULE, fmt, para_num, ##__VA_ARGS__)
#else
#define QDEC_DBG_BUFFER(MODULE, LEVEL, fmt, para_num, ...) ((void)0)
#endif

#define IS_ALLOW_ENTER_DLPS_TIMEOUT_MS 20

#define FLAG_QDEC_X_PHA_PIN_WAKE_UP 0x01
#define FLAG_QDEC_X_PHB_PIN_WAKE_UP 0x02

/*============================================================================*
 *                              Types
 *============================================================================*/

typedef struct t_qdec_ctx {
    int16_t pre_ct; // previous counter value
    int16_t cur_ct; // current counter value
    uint16_t dir;   // 1--up; 0-- down
} T_QDEC_CTX;

typedef struct {
    uint8_t qdecoder_a_status;
    uint8_t qdecoder_b_status;
    uint8_t pre_a_status;
    uint8_t pre_b_status;
    T_QDEC_CTX qdec_ctx;
} T_QDEC_LOCAL_DATA;

typedef struct {
    bool is_allowed_to_enter_dlps;
    bool pha_wakeup_level;
    bool phb_wakeup_level;
    uint8_t enter_qdec_flag;
    signed char v_scroll_val;
} T_QDEC_GLOBAL_DATA;

extern T_QDEC_GLOBAL_DATA qdec_global_data;
/*============================================================================*
 *                       Interface Functions
 *============================================================================*/
void qdec_driver_init_data(void);
void qdec_module_pinmux_config(void);
void qdec_module_pad_config(void);
void qdec_module_init(void);
void qdec_module_deinit(void);
void qdec_cfg_init(uint32_t is_debounce, uint8_t phasea, uint8_t phaseb);
void qdec_module_enter_dlps_config(void);
void qdec_module_dlps_timer_init(void);
bool qdec_enter_dlps_check(void);
void qdec_module_nvic_config(void);
void qdec_timer_init(void);
void qdec_start_is_allow_enter_dlps_timer(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
