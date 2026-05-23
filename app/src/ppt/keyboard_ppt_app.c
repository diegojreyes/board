/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <zmk/ppt/keyboard_ppt_app.h>
#include <zmk/mode_monitor.h>
#include <zmk/ppt.h>
#include <zmk/event_manager.h>
#include <zmk/events/ppt_conn_state_changed.h>
#include <zmk/events/led_state_changed.h>
#include "rtl_pinmux.h"
// #include "trace.h"
#include <zmk/endpoints.h>
#include <zmk/leds.h>
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#endif // IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/battery.h>
#include "../launcher/launcher.h"

LOG_MODULE_DECLARE(zmk, 4); // CONFIG_ZMK_LOG_LEVEL);
// #undef DBG_DIRECT
#define DBG_DIRECT LOG_DBG

enum {
    RF_DONGLE_USB_IDLE,
    RF_DONGLE_USB_CONNECTED,
    RF_DONGLE_USB_SUSPEND,
    RF_DONGLE_USB_BOOT_MODE,
};

typedef union {
    unsigned char value;
    struct {
        unsigned char led : 3;
        unsigned char rsvd0 : 2;
        unsigned char usb : 2;
        unsigned char xbox_en : 1;
        // unsigned char rsvd1:1;
    } f;
} sync_ack_state_flag_t;

extern uint8_t rgb_onoff_status;
int rgb_underglow_auto_state(bool new_state);

static uint8_t ppt_usb_state;
extern bool is_app_enabled_dlps;
extern struct k_work_delayable sleep_work;
void ringbuf_reset(void);
bool sync_master_set_hb_param(uint8_t level, uint32_t interval, uint32_t beat_times);
void zmk_usb_hid_set_protocol(uint8_t protocol);
#if FEATURE_SUPPORT_NO_ACTION_DISCONN
static void no_act_disconn_timer_callback(struct k_timer *p_timer);
static K_TIMER_DEFINE(no_act_disconn_timer, no_act_disconn_timer_callback, NULL);
#endif

void enter_lowpower(void);
void lowpower_settings(void);

extern uint8_t fn_win_lock;
void winlock_led_onoff(uint8_t onoff);
void winlock_led_set(void);

#ifdef EN_APP_CONTROL_RECONN
void start_ppt_reconn(void);
void stop_ppt_reconn(void);
#endif

/*============================================================================*
 *                             Macro
 *============================================================================*/
/* uint: dbm */
#if FEATURE_SUPPORT_APP_CFG_PPT_TX_POWER
#define PPT_TX_POWER_DBM_MAX 4
#define PPT_TX_POWER_DBM_MIN 4
#endif
/*============================================================================*
 *                             Global Variables
 *============================================================================*/
T_APP_PPT_GLOBAL_DATA ppt_app_global_data;

/*============================================================================*
 *                             Local Variables
 *============================================================================*/
static uint32_t usb_report_rate_level[REPORT_RATE_LEVEL_NUM] = {
    USB_REPORT_RATE_LEVEL_0, USB_REPORT_RATE_LEVEL_1, USB_REPORT_RATE_LEVEL_2};
static uint32_t ppt_report_rate_level[REPORT_RATE_LEVEL_NUM] = {
    PPT_REPORT_RATE_LEVEL_0, PPT_REPORT_RATE_LEVEL_1, PPT_REPORT_RATE_LEVEL_2};

static uint8_t last_conn_status;
static int64_t reconn_start_time;
/*============================================================================*
 *                             Functions Declaration
 *============================================================================*/
// static void ppt_app_send_msg_cb(sync_msg_type_t type, uint8_t *data, uint16_t len,
//                                 sync_send_info_t *info);
void zmk_ppt_sync_event_cb(sync_event_t event);

static void raise_ppt_status_changed_event(struct k_work *_work) {
    raise_zmk_ppt_conn_state_changed(
        (struct zmk_ppt_conn_state_changed){.conn_state = zmk_ppt_get_conn_state()});
}

K_WORK_DEFINE(ppt_status_notifier_work, raise_ppt_status_changed_event);
/*============================================================================*
 *                              Local Functions
 *============================================================================*/
K_MSGQ_DEFINE(launcher_ppt_msgq, 32, 16, 4);
void launcher_ppt_worker(struct k_work *work);
K_WORK_DEFINE(launcher_ppt_work, launcher_ppt_worker);
void launcher_ppt_worker(struct k_work *work) {
    uint8_t ppt_data[32];
    while (k_msgq_get(&launcher_ppt_msgq, ppt_data, K_NO_WAIT) == 0) {
        launcher_set_path(LAUNCHER_PATH_PPT);
        // LOG_HEXDUMP_ERR(ppt_data,16,"ppt");
        raw_hid_receive(ppt_data, sizeof(ppt_data));
    }
}
static bool hb_changed_fast;
void change_hb_cb(struct k_work *work) {
    sync_master_set_hb_param(0, 0, 0);
    sync_master_set_hb_param(1, 250000, 0xffffffff);
    hb_changed_fast = false;
    LOG_ERR("change slow hb!");
}
K_WORK_DELAYABLE_DEFINE(change_hb, change_hb_cb);

/******************************************************************
 * @brief  ppt_app_receive_msg_cb
 * @param  p_data - pointer to receive data.
 * @param  len - data length.
 * @param  info - receive information
 * @return none
 * @retval void
 */
static void ppt_app_receive_msg_cb(uint8_t *p_data, uint16_t len, sync_receive_info_t *info) {
    // LOG_HEXDUMP_WRN(p_data,len,"rx");
#if (SYNC_SUPPORT_MD1R)
    if (p_data[0] == 7) {
        sync_ack_state_flag_t *p_state = (sync_ack_state_flag_t *)&p_data[2];
        LOG_DBG("led:%x,usb:%x", p_state->f.led, p_state->f.usb);

        if (p_state->f.usb != ppt_usb_state) {
            ppt_usb_state = p_state->f.usb;
            if (ppt_usb_state == RF_DONGLE_USB_SUSPEND)
                p_state->f.led = 0;
#ifdef CONFIG_LED_STRIP
            if (rgb_onoff_status)
                rgb_underglow_auto_state(ppt_usb_state != RF_DONGLE_USB_SUSPEND);
#endif
            zmk_usb_hid_set_protocol(ppt_usb_state != RF_DONGLE_USB_BOOT_MODE);
        }
        struct zmk_hid_led_report_body led_report_body = {
            .leds = p_state->f.led,
        };
        struct zmk_endpoint_instance endpoint = {
            .transport = ZMK_TRANSPORT_PPT,
        };
        zmk_hid_indicators_process_report(&led_report_body, endpoint);
    }
#else
    if (p_data[0] == 0x40) // SYNC_OPCODE_DONGLE_STATE_INFO = 0x40
    {
        uint8_t usb_state = p_data[1];
        uint8_t led_state = p_data[2];
        if (led_state) {
            struct zmk_hid_led_report_body led_report_body = {
                .leds = led_state,
            };
            struct zmk_endpoint_instance endpoint = {
                .transport = ZMK_TRANSPORT_PPT,
            };
            zmk_hid_indicators_process_report(&led_report_body, endpoint);
        }
        if (usb_state != ppt_usb_state || usb_state == RF_DONGLE_USB_IDLE) {
            ppt_usb_state = usb_state;
            if ((ppt_usb_state & 0x02) == RF_DONGLE_USB_SUSPEND) {
                struct zmk_hid_led_report_body led_report_body = {
                    .leds = 0,
                };
                struct zmk_endpoint_instance endpoint = {
                    .transport = ZMK_TRANSPORT_PPT,
                };
                zmk_hid_indicators_process_report(&led_report_body, endpoint);

#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
                winlock_led_onoff(0);
#endif
                sync_master_set_hb_param(0, 0, 20);
            }
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
            else {
                winlock_led_set();
                sync_master_set_hb_param(0, 0, 0);
            }
#endif
#ifdef CONFIG_LED_STRIP
            if (rgb_onoff_status)
                rgb_underglow_auto_state((ppt_usb_state & 0x02) != RF_DONGLE_USB_SUSPEND);
#endif
#if 0
            static uint8_t back_nkro=0;
            if(ppt_usb_state &0x01)
            {
                back_nkro=zmk_get_nkro_status();
                if(back_nkro)
                {
                    LOG_ERR("boot protocol,set nkro disable");
                    zmk_set_nkro_status(false);
                }
            }
            else if(back_nkro)
            {
                LOG_ERR("boot protocol exit,set nkro enable");
                zmk_set_nkro_status(true);
                back_nkro =0;
            }
#else
                // zmk_usb_hid_set_protocol((ppt_usb_state &0x01)==0);
#endif
        }
    } else if (p_data[0] == 0x50) // SYNC_OPCODE_RAW_HID_SET_RPT = 0x50
    {
        if (p_data[1] == 1 && p_data[2] == 0x07) {
            uint8_t led = p_data[3];
            struct zmk_hid_led_report_body led_report_body = {
                .leds = led,
            };
            struct zmk_endpoint_instance endpoint = {
                .transport = ZMK_TRANSPORT_PPT,
            };
            zmk_hid_indicators_process_report(&led_report_body, endpoint);
        } else if (p_data[1] == 2) // inf
        {
            if (p_data[2] != kc_get_default_layer) {
                if (!hb_changed_fast) {
                    hb_changed_fast = true;
                    sync_master_set_hb_param(0, 0, 20);
                    sync_master_set_hb_param(1, 100000, 0xffffffff);
                    k_work_reschedule(&change_hb, K_MSEC(5000));
                }
            }
            int err = k_msgq_put(&launcher_ppt_msgq, &p_data[2], K_MSEC(100));
            if (err) {
                switch (err) {
                case -EAGAIN: {
                    LOG_WRN("queue full");
                    uint8_t buf[32];
                    k_msgq_get(&launcher_ppt_msgq, buf, K_NO_WAIT);
                    k_msgq_put(&launcher_ppt_msgq, &p_data[2], K_MSEC(100));
                }
                default:
                    LOG_WRN("Failed to queue launcher data (%d)", err);
                }
            }

            err = k_work_submit(&launcher_ppt_work);
        }
    }
#endif
}

/******************************************************************
 * @brief  ppt_app_send_msg_cb
 * @param  type - 2.4g message type.
 * @param  p_data - pointer to send data.
 * @param  len - data length
 * @param  info - sending information
 * @return none
 * @retval void
 */

/******************************************************************
 * @brief  zmk_ppt_sync_event_cb
 * @param  event - 2.4g sync event
 * @return none
 * @retval void
 */
void zmk_ppt_sync_event_cb(sync_event_t event) {
    switch (event) {
    case SYNC_EVENT_PAIRED: {
        DBG_DIRECT("[zmk_ppt_sync_event_cb] SYNC_EVENT_PAIRED");
        ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_PAIRED;
        ppt_app_global_data.is_ppt_bond = true;
        ppt_app_global_data.pair_time_count = 0;

        ppt_app_global_data.send_data_index = 0;
        int ppt_send_device_state_info(void);
        ppt_send_device_state_info();
        // read bonded data for reconn!
        if (!ppt_check_is_bonded()) {
            LOG_ERR("ERR,bond data lost?");
        }
    } break;
    case SYNC_EVENT_PAIR_TIMEOUT: {
        DBG_DIRECT("[zmk_ppt_sync_event_cb] SYNC_EVENT_PAIR_TIMEOUT");
        if (ppt_app_global_data.pair_time_count < PPT_PAIR_TIME_MAX_COUNT) {
            // keyboard_ppt_pair();
            ppt_pair();
            ppt_app_global_data.pair_time_count++;
        } else {
            ppt_app_global_data.pair_time_count = 0;
            ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_IDLE;
            raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state = LED_PEER_STATE_DISCONNECTED, .transport = ZMK_TRANSPORT_PPT});
            k_work_reschedule(&sleep_work, K_MSEC(40 * 1000));
            enter_lowpower();
        }
    } break;
    case SYNC_EVENT_CONNECTED: {
#ifdef EN_APP_CONTROL_RECONN
        stop_ppt_reconn();
#endif
        DBG_DIRECT("[zmk_ppt_sync_event_cb] SYNC_EVENT_CONNECTED");
        ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_CONNECTED;

        lowpower_settings();

#if FEATURE_SUPPORT_NO_ACTION_DISCONN
        if (ppt_app_global_data.reconnect_time_count <= 2) {
            k_timer_start(&no_act_disconn_timer, K_MSEC(NO_ACTION_DISCON_TIMEOUT), K_NO_WAIT);
        }
#endif
        k_work_cancel_delayable(&sleep_work);

        if ((reconn_start_time == 0) || (k_uptime_delta(&reconn_start_time) > 1000)) {

            raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state = LED_PEER_STATE_CONNECTED, .transport = ZMK_TRANSPORT_PPT});
            reconn_start_time = 0;
        }
        ppt_app_global_data.reconnect_time_count = 0;
        last_conn_status = 1;
    } break;
    case SYNC_EVENT_CONNECT_TIMEOUT: {
#ifdef EN_APP_CONTROL_RECONN
        stop_ppt_reconn();
#endif
        DBG_DIRECT("[zmk_ppt_sync_event_cb] SYNC_EVENT_CONNECT_TIMEOUT");
        ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_IDLE;
        ppt_usb_state = RF_DONGLE_USB_IDLE;
#ifndef EN_APP_CONTROL_RECONN
        if (ppt_app_global_data.reconnect_time_count < PPT_RECONNECT_TIME_MAX_COUNT) {
            keyboard_ppt_reconnect();
            ppt_app_global_data.reconnect_time_count++;
        } else
#endif
        {
            ringbuf_reset();
            raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state = LED_PEER_STATE_DISCONNECTED, .transport = ZMK_TRANSPORT_PPT});
            k_work_reschedule(&sleep_work, last_conn_status ? K_MSEC(6000) : K_MSEC(36000));
            last_conn_status = 0;
            enter_lowpower();
        }
    } break;
    case SYNC_EVENT_CONNECT_LOST: {
        DBG_DIRECT("[zmk_ppt_sync_event_cb] SYNC_EVENT_CONNECT_LOST");
        ppt_usb_state = RF_DONGLE_USB_IDLE;
        if (ppt_app_global_data.keyboard_ppt_status == KEYBOARD_PPT_STATUS_DISCONNECT_BY_USER) {
            ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_IDLE;

        } else {
            ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_IDLE;
            // raise_zmk_led_state_changed((struct zmk_led_state_changed){
            //     .led_state=LED_PEER_STATE_DISCONNECTED,
            //     .transport=ZMK_TRANSPORT_PPT});

#if 1
            if (ppt_app_global_data.is_ppt_bond) {
                if (ppt_app_global_data.reconnect_time_count < 1) // PPT_RECONNECT_TIME_MAX_COUNT)
                {
                    keyboard_ppt_reconnect();
                    reconn_start_time = k_uptime_get();
                    ppt_app_global_data.reconnect_time_count++;
                }
            }
            // else if (ppt_app_global_data.pair_time_count < PPT_PAIR_TIME_MAX_COUNT)
            // {
            //     keyboard_ppt_pair();
            //     ppt_app_global_data.pair_time_count++;
            // }
#endif
        }

#if FEATURE_SUPPORT_NO_ACTION_DISCONN
        /* stop no_act_disconn_timer if disconnection detected */
        k_timer_stop(&no_act_disconn_timer);
#endif
        struct zmk_hid_led_report_body led_report_body = {
            .leds = 0,
        };
        struct zmk_endpoint_instance endpoint = {
            .transport = ZMK_TRANSPORT_PPT,
        };
        zmk_hid_indicators_process_report(&led_report_body, endpoint);
    } break;
    default:
        break;
    }
    k_work_submit(&ppt_status_notifier_work);
}

/*============================================================================*
 *                              Global Functions
 *============================================================================*/
/******************************************************************
 * @brief  keyboard_ppt_pair
 * @param  none
 * @return none
 * @retval void
 */
void keyboard_ppt_pair(void) {
    if (zmk_endpoints_selected().transport != ZMK_TRANSPORT_PPT) {
        return;
    }
    ppt_stop_sync();
    DBG_DIRECT("keyboard ppt pair");
    raise_zmk_led_state_changed((struct zmk_led_state_changed){.led_state = LED_PEER_STATE_PAIR,
                                                               .transport = ZMK_TRANSPORT_PPT});
    keyboad_led_set_onoff(0);
    k_work_cancel_delayable(&sleep_work);
    if (true == ppt_pair()) {
        ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_PAIRING;
    }
}

/******************************************************************
 * @brief  keyboard_ppt_reconnect
 * @param  none
 * @return none
 * @retval void
 */
void keyboard_ppt_reconnect(void) {
    if (ppt_app_global_data.keyboard_ppt_status == KEYBOARD_PPT_STATUS_CONNECTING) {
        LOG_ERR("ppt in connecting!");
        return;
    }

    if (true == ppt_reconnect()) {
        ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_CONNECTING;

    } else {
        k_work_reschedule(&sleep_work, K_MSEC(40000));
    }
}

/******************************************************************
 * @brief  keyboard_stop_sync
 * @param  none
 * @return none
 * @retval void
 */
void keyboard_ppt_stop_sync(void) {
    ppt_stop_sync();
    ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_IDLE;
#if FEATURE_SUPPORT_NO_ACTION_DISCONN
    /* stop no_act_disconn_timer if disconnection detected */
    k_timer_stop(&no_act_disconn_timer);
#endif
}

/******************************************************************
 * @brief  keyboard_ppt_set_sync_interval
 * @param  none
 * @return none
 * @retval void
 */
void keyboard_ppt_set_sync_interval(void) {
    uint32_t ppt_interval_time_us = PPT_REPORT_RATE_LEVEL_0;
    sync_time_set(SYNC_TIME_PARAM_CONNECT_INTERVAL, ppt_interval_time_us);
    sync_time_set(SYNC_TIME_PARAM_CONNECT_INTERVAL_HIGH, ppt_interval_time_us);
}

/******************************************************************
 * @brief  keyboard_ppt_init
 * @param  none
 * @return none
 * @retval void
 */
void keyboard_ppt_init(void) {
    sync_acc_t acc;
#if !(defined(CONFIG_SHIELD_KEYCHRON_RS45_ANSI) || defined(CONFIG_SHIELD_KEYCHRON_RS87_ANSI))
    acc.addr = 0x8EBEC9D6;
#else
    acc.addr = 0x8ebe89ba;
#endif

    sync_acc_set_br(acc);

    ppt_sync_init(SYNC_ROLE_MASTER);
    sync_msg_reg_receive_cb(ppt_app_receive_msg_cb);
    // sync_msg_reg_send_cb(ppt_app_send_msg_cb);
    sync_event_cb_reg(zmk_ppt_sync_event_cb);
    ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_IDLE;
    ppt_app_global_data.is_ppt_bond = ppt_check_is_bonded();
    DBG_DIRECT("ppt_app_global_data.is_ppt_bond = %d", ppt_app_global_data.is_ppt_bond);

    sync_pair_rssi_set(PPT_PAIR_RSSI);

    /* set 2.4g connection interval */
    keyboard_ppt_set_sync_interval();

    /* set 2.4g connection heart beat interval */
    sync_master_set_hb_param(0, 0, 0);
    // sync_master_set_hb_param(1, 8000, 1);
    sync_master_set_hb_param(1, 250000, 0xffffffff);
    // sync_master_set_hb_param(2, PPT_DEFAULT_HEARTBEAT_INTERVAL_TIME, 0);

    /* set ppt tx_power */
#if FEATURE_SUPPORT_APP_CFG_PPT_TX_POWER
    sync_tx_power_set(false, PPT_TX_POWER_DBM_MAX, PPT_TX_POWER_DBM_MIN);
#endif
    /*Different message types have different queue size*/
    uint8_t msg_quota[SYNC_MSG_TYPE_NUM] = {0, 3, 3, 3};
    sync_msg_set_quota(msg_quota);

    // #if !ENABLE_2_4G_LOG
    //     sync_log_set(0, false);
    // #endif
    sync_log_set(0, false);
#ifdef CONFIG_REALTEK_USING_PPT_SYNC_SKN
    // use rtk lib not source code,change crc settings!
    sync_crc_set(16, 0x8005, 0xffff);
#endif
}

/******************************************************************
 * @brief    get_report_rate_level_by_index
 * @param    keyboard mode - keyboard mode type
 * @param    index - report rate index
 * @return   report rate value
 * @retval   uint32_t
 */
uint32_t get_report_rate_level_by_index(uint8_t keyboard_mode, uint32_t index) {
    if (keyboard_mode == USB_MODE) {
        return usb_report_rate_level[index];
    } else if (keyboard_mode == PPT_MODE) {
        return ppt_report_rate_level[index];
    }
    return 0;
}

/******************************************************************
 * @brief  enalbe 2.4g proprietary for dongle
 * @param  none
 * @return none
 * @retval void
 */
void keyboard_ppt_enable(void) {
    ppt_app_global_data.pair_time_count = 0;
    ppt_app_global_data.reconnect_time_count = 0;
    ppt_sync_enable();
    // if (ppt_app_global_data.is_ppt_bond)
    // {
    //     ppt_app_global_data.reconnect_time_count = 1;
    //     keyboard_ppt_reconnect();
    // }
}

/******************************************************************
 * @brief  init 2.4g proprietary global data
 * @param  none
 * @return none
 * @retval void
 */
void app_init_ppt_global_data(void) {
    memset(&ppt_app_global_data, 0, sizeof(ppt_app_global_data));
    ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_IDLE;
}

/******************************************************************
 * @brief  send keyboard data by ppt
 * @param  type - send type
 * @param  msg_retrans_count - retrans count
 * @param  keyboard_data
 * @return none
 * @retval void
 */
bool keyboard_app_ppt_send_data(sync_msg_type_t type, uint8_t msg_retrans_count,
                                T_KEYBOARD_DATA keyboard_data) {
    sync_err_code_t ret;

    if (keyboard_data.hid_report_type == HID_REPORT_KEYBOARD) {
        uint8_t tx_keyboard_data[KEYBOARD_DATA_SIZE + 1 + 3] = {0};

        memcpy(&tx_keyboard_data[2], keyboard_data.keyboard_common_data, KEYBOARD_DATA_SIZE);

        ret = ppt_app_send_data(type, msg_retrans_count, tx_keyboard_data,
                                KEYBOARD_DATA_SIZE + 1 + 3);
        if (ret == SYNC_ERR_CODE_SUCCESS) {
            // DBG_DIRECT("[key_handle_notify_hid_usage_buffer] send common_keyboard_buffer: 0x %b",
            //                 TRACE_BINARY(sizeof(tx_keyboard_data), tx_keyboard_data));
            LOG_HEXDUMP_DBG(tx_keyboard_data, sizeof(tx_keyboard_data), "send");
            return true;
        } else {
            DBG_DIRECT("keyboard app ppt send data fail! key store in queue! error reason = %d",
                       ret);
        }
    }
    if (keyboard_data.hid_report_type == HID_REPORT_CONSUMER) {
        uint8_t tx_comsumer_data[CONSUMER_DATA_SIZE + 1 + 3] = {0};

        memcpy(&tx_comsumer_data[2], keyboard_data.keyboard_consumer_data, CONSUMER_DATA_SIZE);

        ret = ppt_app_send_data(type, msg_retrans_count, tx_comsumer_data,
                                CONSUMER_DATA_SIZE + 1 + 3);
        if (ret == SYNC_ERR_CODE_SUCCESS) {
            // DBG_DIRECT("[key_handle_notify_hid_usage_buffer] send fn_keyboard_buffer: 0x %b",
            //                 TRACE_BINARY(sizeof(tx_comsumer_data), tx_comsumer_data));
            LOG_HEXDUMP_DBG(tx_comsumer_data, sizeof(tx_comsumer_data), "comsumer");
            return true;
        } else {
            DBG_DIRECT("keyboard app ppt send data fail! error reason = %d", ret);
        }
    }
    if (keyboard_data.hid_report_type == HID_REPORT_VENDOR) {
        uint8_t tx_vendor_data[VENDOR_DATA_SIZE + 1] = {0};
        tx_vendor_data[0] = SYNC_OPCODE_VENDOR;
        memcpy(&tx_vendor_data[1], keyboard_data.keyboard_vendor_data, VENDOR_DATA_SIZE);
        ret = ppt_app_send_data(type, msg_retrans_count, tx_vendor_data, VENDOR_DATA_SIZE + 1);
        if (ret == SYNC_ERR_CODE_SUCCESS) {
            // DBG_DIRECT("[key_handle_notify_hid_usage_buffer] send vendor_buffer: 0x %x",
            //                 TRACE_BINARY(sizeof(tx_vendor_data), tx_vendor_data));
            LOG_HEXDUMP_DBG(tx_vendor_data, sizeof(tx_vendor_data), "vendor");
            return true;
        } else {
            DBG_DIRECT("keyboard app ppt send data fail! error reason = %d", ret);
        }
    }
    return false;
}

#if FEATURE_SUPPORT_NO_ACTION_DISCONN
/**
 * @brief  No action and disconnect timer callback
 *         it is used to terminate connection after timeout
 * @param  p_timer - timer handler
 * @return None
 */
void no_act_disconn_timer_callback(struct k_timer *p_timer) {
    if (app_mode.is_in_ppt_mode) {
        if (ppt_app_global_data.mouse_ppt_status == KEYBOARD_PPT_STATUS_CONNECTED) {
            DBG_DIRECT("[KEYBOARD] Idle No Action Timeout, Disconnect.");
            keyboard_ppt_stop_sync();
        }
    }
}
#endif

bool zmk_ppt_is_ready(void) { return zmk_ppt_get_conn_state() == ZMK_PPT_CONN; }

enum zmk_ppt_conn_state zmk_ppt_get_conn_state(void) {
    // DBG_DIRECT("state: %d", ppt_app_global_data.keyboard_ppt_status);
    switch (ppt_app_global_data.keyboard_ppt_status) {
    case KEYBOARD_PPT_STATUS_PAIRED:    /* start sync but not paired */
    case KEYBOARD_PPT_STATUS_CONNECTED: /* connected success status */
    case KEYBOARD_PPT_STATUS_LOW_POWER:
        return ZMK_PPT_CONN;

    case KEYBOARD_PPT_STATUS_PAIRING:
    case KEYBOARD_PPT_STATUS_CONNECTING:
        return ZMK_PPT_CONN_NONE;

    default:
        return ZMK_PPT_CONN_NONE;
    }
}

void zmk_ppt_init(void) {
    static bool inited = false;
    if (bat_is_shutdown() && !zmk_usb_power_on())
        return;
    if (inited) {
        // keyboard_ppt_enable();
        zmk_ppt_reconn();
        return;
    }
    inited = true;

#if DT_NODE_HAS_STATUS(DT_INST(0, zmk_ppt_node), okay)
    DBG_DIRECT("zmk ppt init and start pair");
    keyboard_ppt_init();
    keyboard_ppt_enable();
    // keyboard_ppt_pair();
    zmk_ppt_reconn();
#else
    DBG_DIRECT("err :not find ppt node in device tree!");
#endif
}
uint8_t led_is_display_bat(void);
void zmk_ppt_reconn(void) {
    if (bat_is_shutdown())
        return;
    if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT &&
        ppt_app_global_data.keyboard_ppt_status != KEYBOARD_PPT_STATUS_PAIRING &&
        ppt_app_global_data.keyboard_ppt_status != KEYBOARD_PPT_STATUS_CONNECTED
    // &&ppt_app_global_data.keyboard_ppt_status != KEYBOARD_PPT_STATUS_CONNECTING
#ifdef CONFIG_LED_STRIP
        && (led_is_display_bat() == 0)
#endif
        && ((reconn_start_time == 0) || (k_uptime_delta(&reconn_start_time) > 1000))) {
        LOG_ERR("zmk_ppt_reconn");
        last_conn_status = 0;
        reconn_start_time = 0;
        raise_zmk_led_state_changed((struct zmk_led_state_changed){
            .led_state = LED_PEER_STATE_RECONN, .transport = ZMK_TRANSPORT_PPT});
#ifndef EN_APP_CONTROL_RECONN
        if (ppt_app_global_data.keyboard_ppt_status != KEYBOARD_PPT_STATUS_CONNECTING) {
            ppt_app_global_data.reconnect_time_count = 1;
            keyboard_ppt_reconnect();
        } else {
            ppt_app_global_data.reconnect_time_count = 1;
        }
#else
        start_ppt_reconn();
#endif
    }
}

void zmk_ppt_disconnect(void) {
    keyboard_ppt_stop_sync();
    ppt_app_global_data.keyboard_ppt_status = KEYBOARD_PPT_STATUS_DISCONNECT_BY_USER;
    zmk_ppt_sync_event_cb(SYNC_EVENT_CONNECT_LOST);
}
uint8_t zmk_ppt_get_state(void) { return ppt_app_global_data.keyboard_ppt_status; }
void zmk_ppt_stop_reconn(void) {
    if (ppt_app_global_data.keyboard_ppt_status == KEYBOARD_PPT_STATUS_CONNECTING) {
        keyboard_ppt_stop_sync();
    }
}

#ifndef CONFIG_LOG
// rtk lib use logs ,but release version close log,so need to skip logs !
void z_impl_z_log_msg_static_create(const void *source, const struct log_msg_desc desc,
                                    uint8_t *package, const void *data) {
    return;
}
#endif
#ifdef EN_APP_CONTROL_RECONN
static uint32_t reconn_count;
void reconn_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(reconn, reconn_cb);
void reconn_cb(struct k_work *work) {
    uint32_t ms_time = 0;
    // LOG_ERR("count:%d",reconn_count);
    if (reconn_count % 2 == 0) {
        if (ppt_app_global_data.keyboard_ppt_status != KEYBOARD_PPT_STATUS_CONNECTING) {

            keyboard_ppt_reconnect();
        }
        ms_time = 30;
    } else {
        ms_time = 20;
        zmk_ppt_stop_reconn();
    }
    reconn_count++;
    if (reconn_count < 40) {
        k_work_reschedule(&reconn, K_MSEC(ms_time));
    } else {
        zmk_ppt_sync_event_cb(SYNC_EVENT_CONNECT_TIMEOUT);
    }
}
void start_ppt_reconn(void) {
    reconn_count = 0;
    k_work_reschedule(&reconn, K_MSEC(10));
}
void stop_ppt_reconn(void) {
    reconn_count = 0;
    k_work_cancel_delayable(&reconn);
}
#endif