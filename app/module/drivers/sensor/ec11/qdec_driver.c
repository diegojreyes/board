/**
**********************************************************************************************************
*               Copyright(c) 2023, Realtek Semiconductor Corporation. All rights reserved.
**********************************************************************************************************
* @file     qdec_driver.c
* @brief    qdecoder module driver code
* @details
* @author   barry
* @date     2023-03-11
* @version  v1.0
*********************************************************************************************************
*/

/*============================================================================*
 *                              Includes
 *============================================================================*/
#include <rtl_aon_qdec.h>
#include <rtl_rcc.h>
#include <rtl_nvic.h>
#include <rtl_gpio.h>
#include <rtl_pinmux.h>
#include "qdec_driver.h"
#include <zephyr/logging/log.h>

#if QDEC_EN
LOG_MODULE_REGISTER(qdec, 4);
#define QDEC_X_PHA_PIN P9_0
#define QDEC_X_PHB_PIN P9_1
/*============================================================================*
*                              Macros
*============================================================================*/
#define  qdec_interrupt_handler     AON_QDEC_Handler

/*============================================================================*
*                              Local variables
*============================================================================*/
// static TimerHandle_t  qdec_allow_enter_dlps_timer;
static T_QDEC_LOCAL_DATA qdec_local_data;
/*============================================================================*
*                              Global variables
*============================================================================*/
T_QDEC_GLOBAL_DATA qdec_global_data;

/*============================================================================*
*                              Functions Declaration
*============================================================================*/
static void qdec_module_init_status_read(void);
void qdec_driver_init_data(void);
void qdec_module_pinmux_config(void);
void qdec_module_pad_config(void);
void qdec_cfg_init(uint32_t is_debounce, uint8_t phasea, uint8_t phaseb);
void qdec_module_nvic_config(void);
void qdec_module_init(void);
void qdec_module_deinit(void);
void qdec_module_enter_dlps_config(void) ;
bool qdec_enter_dlps_check(void) ;
void qdec_interrupt_handler(void) ;
static void qdec_module_init_status_read(void) ;
void qdec_start_is_allow_enter_dlps_timer(void) ;

/*============================================================================*
*                              Private Funcitons
*============================================================================*/
/**
 * @brief  Read qdecoder phases
 * @param  None
 * @return None
 */
static void qdec_module_init_status_read(void)
{
    AON_QDEC_CounterPauseCmd(AON_QDEC, AON_QDEC_AXIS_X, ENABLE);


    Pinmux_Config(QDEC_X_PHA_PIN, DWGPIO);
    Pinmux_Config(QDEC_X_PHB_PIN, DWGPIO);
    Pad_Config(QDEC_X_PHA_PIN, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE,
               PAD_OUT_LOW);
    Pad_Config(QDEC_X_PHB_PIN, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE,
               PAD_OUT_LOW);
    qdec_local_data.qdecoder_a_status = GPIO_ReadInputDataBit(GPIO_GetPort(QDEC_X_PHA_PIN),
                                                              GPIO_GetPin(QDEC_X_PHA_PIN));
    qdec_local_data.qdecoder_b_status = GPIO_ReadInputDataBit(GPIO_GetPort(QDEC_X_PHB_PIN),
                                                              GPIO_GetPin(QDEC_X_PHB_PIN));

    // qdec_module_pinmux_config();
    Pinmux_AON_Config(QDPH0_IN_P9_0_P9_1);
    AON_QDEC_CounterPauseCmd(AON_QDEC, AON_QDEC_AXIS_X, DISABLE);
	
    qdec_global_data.is_allowed_to_enter_dlps = true;
}


/******************************************************************
* @brief software timer CB function
 * @param  none
 * @return none
 * @retval void
 */
// static void qdec_allow_enter_dlps_timer_callback(TimerHandle_t p_timer)
// {
//     qdec_global_data.is_allowed_to_enter_dlps = true;
// }

/*============================================================================*
*                              Public Funcitons
*============================================================================*/
/**
 * @brief  Initialize qdecoder driver data
 * @param  None
 * @return None
 */
void qdec_driver_init_data(void)
{
    LOG_DBG("[qdec_driver_init_data] init data");
    memset(&qdec_global_data, 0, sizeof(qdec_global_data));
    qdec_global_data.is_allowed_to_enter_dlps = true;
}

/**
 * @brief qdecoder pinmux config
 * @param none
 * @return none
 * @retval void
 */
void qdec_module_pinmux_config(void)
{
    Pinmux_AON_Config(QDPH0_IN_P9_0_P9_1);
}

/**
 * @brief  Qdecoder module pad config
 * @param  None
 * @return None
 */
void qdec_module_pad_config(void)
{
    Pad_Config(QDEC_X_PHA_PIN, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE,
               PAD_OUT_LOW);
    Pad_Config(QDEC_X_PHB_PIN, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE,
               PAD_OUT_LOW);
}

/**
 * @brief  Initialize qdecoder parameters
 * @param  is_debounce - debounce enalbe or disable
 * @param  phasea - phase a volage level, 0: low, 1: high
 * @param  phaseb - phase b volage level, 0: low, 1: high
 * @return None
 */
void qdec_cfg_init(uint32_t is_debounce, uint8_t phasea, uint8_t phaseb)
{
    AON_QDEC_InitTypeDef qdecInitStruct = {0};
    AON_QDEC_StructInit(&qdecInitStruct);
    qdecInitStruct.debounceTimeX =
        20;/* uint 1/32 ms, recommended debounce time setting is between 600us and 1000us */
    qdecInitStruct.axisConfigX = ENABLE;
    qdecInitStruct.debounceEnableX = is_debounce;
    qdecInitStruct.initPhaseX = (phasea << 1) | phaseb;
    qdecInitStruct.manualLoadInitPhase = ENABLE;
    qdecInitStruct.counterScaleX = CounterScale_2_Phase;

    AON_QDEC_Init(AON_QDEC, &qdecInitStruct);

    AON_QDEC_INTConfig(AON_QDEC, AON_QDEC_X_INT_NEW_DATA, ENABLE);
    AON_QDEC_INTConfig(AON_QDEC, AON_QDEC_X_INT_ILLEAGE, ENABLE);
    AON_QDEC_INTMask(AON_QDEC, AON_QDEC_X_INT_MASK, DISABLE);
    AON_QDEC_INTMask(AON_QDEC, AON_QDEC_X_CT_INT_MASK, DISABLE);
    AON_QDEC_INTMask(AON_QDEC, AON_QDEC_X_ILLEAGE_INT_MASK, DISABLE);
    AON_QDEC_Cmd(AON_QDEC, AON_QDEC_AXIS_X, ENABLE);

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_StructInit(&GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Pin    = GPIO_GetPin(QDEC_X_PHA_PIN);
    GPIO_InitStruct.GPIO_Mode   = GPIO_MODE_IN;
    GPIO_InitStruct.GPIO_ITCmd  = DISABLE;
    GPIO_Init(GPIO_GetPort(QDEC_X_PHA_PIN), &GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Pin    = GPIO_GetPin(QDEC_X_PHB_PIN);
    GPIO_Init(GPIO_GetPort(QDEC_X_PHB_PIN), &GPIO_InitStruct);
}


/**
 * @brief  Qdecoder nvic config
 * @param  None
 * @return None
 */


/**
 * @brief  Initialize qdec module
 * @param  None
 * @return None
 */
void qdec_module_init(void)
{
    qdec_driver_init_data();
    /*please read qdec phase before qdec init*/
    qdec_module_init_status_read();
    qdec_local_data.pre_a_status = qdec_local_data.pre_b_status = qdec_local_data.qdecoder_a_status;
    qdec_global_data.pha_wakeup_level = (bool)qdec_local_data.qdecoder_a_status;
    qdec_global_data.phb_wakeup_level = (bool)qdec_local_data.qdecoder_b_status;
    LOG_DBG("[qdec_module_init] qdec init, qdecoder_a_status = %d, qdecoder_a_status = %d",
                    qdec_local_data.qdecoder_a_status, qdec_local_data.qdecoder_b_status);
    /*pahse init need to set sample phase when QDEC_CounterScaleX = 1.otherwise, qdec will have some issue if qdec is in the intermediate state when  ic power on  */
    qdec_cfg_init(ENABLE, qdec_local_data.qdecoder_a_status,
                  qdec_local_data.qdecoder_a_status);
}

/**
 * @brief  Qdec_module_deinit
 * @param  None
 * @return None
 */
void qdec_module_deinit(void)
{
    LOG_DBG("qdec deinit");
    AON_QDEC_Cmd(AON_QDEC, AON_QDEC_AXIS_X, DISABLE);
}

/**
 * @brief  Qdec module enter dlps config
 * @param  None
 * @return None
 */
void qdec_module_enter_dlps_config(void)
{
    qdec_global_data.enter_qdec_flag = 0;
    // if (app_global_data.mouse_ble_status == MOUSE_BLE_STATUS_LOW_POWER ||
    //     ppt_app_global_data.mouse_ppt_status == MOUSE_PPT_STATUS_LOW_POWER
    //     || paw3395_global_data.is_sensor_sleep)
    // {
    //     System_WakeUpPinDisable(QDEC_X_PHA_PIN);
    //     System_WakeUpPinDisable(QDEC_X_PHB_PIN);
    //     return;
    // }
		
    if (qdec_global_data.pha_wakeup_level)
    {
        System_WakeUpPinEnable(QDEC_X_PHA_PIN, PAD_WAKEUP_POL_LOW, PAD_WAKEUP_DEB_DISABLE);
    }
    else
    {
        System_WakeUpPinEnable(QDEC_X_PHA_PIN, PAD_WAKEUP_POL_HIGH, PAD_WAKEUP_DEB_DISABLE);
    }

    if (qdec_global_data.phb_wakeup_level)
    {
        System_WakeUpPinEnable(QDEC_X_PHB_PIN, PAD_WAKEUP_POL_LOW, PAD_WAKEUP_DEB_DISABLE);
    }
    else
    {
        System_WakeUpPinEnable(QDEC_X_PHB_PIN, PAD_WAKEUP_POL_HIGH, PAD_WAKEUP_DEB_DISABLE);
    }
}

/**
 * @brief  Check if qdec module allow enter dlps
 * @param  None
 * @return Result - true: allow enter dlps, false: can not enter dlps
 */
bool qdec_enter_dlps_check(void)
{
    return qdec_global_data.is_allowed_to_enter_dlps;
}

/**
 * @brief  Initialize qdecoder module sw timers
 * @param  None
 * @return None
 */
// void qdec_timer_init(void)
// {
//     if (false == os_timer_create(&qdec_allow_enter_dlps_timer, "qdec_allow_enter_dlps_timer",
//                                  1, 
//                                  IS_ALLOW_ENTER_DLPS_TIMEOUT_MS, false, qdec_allow_enter_dlps_timer_callback))
//     {
//         APP_PRINT_INFO0("[sw_timer_init] init qdec_allow_enter_dlps_timer_callback failed");
//     }
// }
/******************************************************************
* @brief
 * @param  none
 * @return none
 * @retval void
 */
// void qdec_start_is_allow_enter_dlps_timer(void)
// {
//     uint32_t state = 0;
//     os_timer_state_get(&qdec_allow_enter_dlps_timer, &state);
//     if (!state)
//     {
//         os_timer_start(&qdec_allow_enter_dlps_timer);
//     }
// }

/**
 * @brief  Qdecoder interrupt handler
 * @param  None
 * @return None
 */
void qdec_interrupt_handler(void)
{
    bool is_handle_qdec_data = false;
    // os_timer_stop(&qdec_allow_enter_dlps_timer);
    qdec_module_init_status_read();
    LOG_DBG("[qdec_interrupt_handler] qdecoder_a_status = %d, qdecoder_b_status = %d",
                    qdec_local_data.qdecoder_a_status, qdec_local_data.qdecoder_b_status);

    qdec_global_data.pha_wakeup_level = (bool)qdec_local_data.qdecoder_a_status;
    qdec_global_data.phb_wakeup_level = (bool)qdec_local_data.qdecoder_b_status;

    if ((qdec_local_data.qdecoder_a_status == qdec_local_data.qdecoder_b_status &&
         qdec_local_data.qdecoder_a_status != qdec_local_data.pre_a_status) ||
        qdec_global_data.enter_qdec_flag == (FLAG_QDEC_X_PHA_PIN_WAKE_UP | FLAG_QDEC_X_PHB_PIN_WAKE_UP))
    {
        qdec_local_data.pre_a_status = !qdec_local_data.pre_a_status;
        qdec_local_data.pre_b_status = !qdec_local_data.pre_b_status;
        is_handle_qdec_data = true;
    }
    else
    {
        LOG_DBG("[qdec_interrupt_handler] unexpected state");
    }

    /* Read direction */
    qdec_local_data.qdec_ctx.dir = AON_QDEC_GetAxisDirection(AON_QDEC, AON_QDEC_AXIS_X);

    if (AON_QDEC_GetFlagState(AON_QDEC, AON_QDEC_FLAG_NEW_CT_STATUS_X) == SET)
    {
        /* Clear qdec interrupt flags */
        AON_QDEC_ClearINTPendingBit(AON_QDEC, AON_QDEC_CLR_NEW_CT_X);
    }
    else if (AON_QDEC_GetFlagState(AON_QDEC, AON_QDEC_FLAG_ILLEGAL_STATUS_X) == SET)
    {
        /* Clear qdec interrupt flags */
        AON_QDEC_ClearINTPendingBit(AON_QDEC, AON_QDEC_CLR_ILLEGAL_INT_X);
        LOG_DBG("illegal int");
    }
    if (is_handle_qdec_data)
    {
        /*wheel data prepare*/
        if (qdec_local_data.qdec_ctx.dir)
        {
            
			
			// if(qdec_global_data.v_scroll_val>-20)
				qdec_global_data.v_scroll_val--;
        }
        else
        {
			// if(qdec_global_data.v_scroll_val<20)
				qdec_global_data.v_scroll_val++;
			
           // app_global_data.mouse_current_data.v_wheel = 0x01;
        }
        /* please turn on the is_allowed_to_enter_dlps flag here, otherwise, second qdec int may be lost*/
        qdec_local_data.qdec_ctx.pre_ct = qdec_local_data.qdec_ctx.cur_ct;



    }
    qdec_global_data.is_allowed_to_enter_dlps = true;
    while ((((AON_QDEC->AON_QDEC_INT_CLR & BIT1) >> 1) == 1) ||
           (((AON_QDEC->AON_QDEC_INT_CLR & BIT4) >> 4) == 1));

    do_trigger();


}

#endif

/******************* (C) COPYRIGHT 2023 Realtek Semiconductor Corporation *****END OF FILE****/
