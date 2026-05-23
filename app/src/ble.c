/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include <zephyr/settings/settings.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci_types.h>
// #include <zephyr/bluetooth/host/keys.h>
#include "keys.h"
#include "conn_internal.h"
#include <zmk/app_wdt.h>
#include <zephyr/drivers/watchdog.h>
#include "ppt_sync.h"
#include <zmk/battery.h>

#if IS_ENABLED(CONFIG_SETTINGS)

#include <zephyr/settings/settings.h>

#endif

#include <zephyr/logging/log.h>
#include "trace.h"
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/ble.h>
#include <zmk/keys.h>
#include <zmk/split/bluetooth/uuid.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/led_state_changed.h>
#include <zmk/leds.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/hid_indicators.h>
#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)
#include <zmk/events/keycode_state_changed.h>

#define PASSKEY_DIGITS 6

static struct bt_conn *auth_passkey_entry_conn;
RING_BUF_DECLARE(passkey_entries, PASSKEY_DIGITS);

#endif /* IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY) */

enum advertising_type {
    ZMK_ADV_NONE,
    ZMK_ADV_DIR,
    ZMK_ADV_CONN,
    ZMK_ADV_RECONN,
    ZMK_ADV_PAIR,
} advertising_status;

#define CURR_ADV(adv) (adv << 4)

#define ADV_RECONN_TIME_OUT (3000) 
#define ADV_PAIR_TIME_OUT (3*60*1000)

struct transfer_state {
    uint8_t adv_type : 4;
    uint8_t profile : 4;
    uint8_t disc_profile;
};

void set_delay_clear_bonds(uint8_t set);
uint8_t delay_clear_bonds(void);
int zmk_ble_reset(void);
static void load_identities(void);
static uint8_t get_pair_bt_id(void);
static uint8_t get_reconn_bt_id(void);
static void update_bt_id(uint8_t bt_id);
static size_t bond_check(uint8_t local_id);
void print_device_addr(uint8_t id);
void copy_profile_to_same_peer(const bt_addr_le_t *peer);

void enter_lowpower(void);
void lowpower_settings(void);

static struct transfer_state transfer;
enum advertising_type adv_state=ZMK_ADV_RECONN;

extern bool is_app_enabled_dlps;
bool settings_enabled_dlps=true;
static bool force_sleep;
static bool last_connected;
void sleep_worker(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(sleep_work, sleep_worker);

void update_conn_param_worker(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(update_conn_param_work, update_conn_param_worker);

void adv_timeout_work_callback(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(adv_timeout_work, adv_timeout_work_callback);

#include <zephyr/bluetooth/services/bas.h>
#include <zmk/battery.h>
void battery_report_cb(struct k_work *work)
{
    bt_bas_set_battery_level(get_battery_level());
    LOG_DBG("update bat:%d",get_battery_level());
}
K_WORK_DELAYABLE_DEFINE(battery_report_work, battery_report_cb);

static struct zmk_ble_profile profiles[ZMK_BLE_PROFILE_COUNT];
static uint8_t active_profile;
static bt_addr_le_t * inc_bt_addr(uint8_t index);


#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)


struct bt_le_adv_param zmk_adv_conn_name={
    .id = 0,
    .options =BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME | BT_LE_ADV_OPT_USE_IDENTITY,
    .interval_min =BT_GAP_ADV_FAST_INT_MIN_1,
    .interval_max =BT_GAP_ADV_FAST_INT_MAX_1
};
#define ZMK_ADV_CONN_NAME  &zmk_adv_conn_name                                                                       
    // BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME | BT_LE_ADV_OPT_USE_IDENTITY, BT_GAP_ADV_FAST_INT_MIN_2, 
    //                 BT_GAP_ADV_FAST_INT_MAX_2, NULL)   

// BUILD_ASSERT(DEVICE_NAME_LEN <= 16, "ERROR: BLE device name is too long. Max length: 16");

static const struct bt_data zmk_ble_ad[] = {
    // BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0xC1, 0x03),
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_SOME, 0x12, 0x18, /* HID Service */
                  0x0f, 0x18                       /* Battery Service */
                  ),
};
//add swift pair
static const struct bt_data zmk_ble_ad_win[] = {
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0xC1, 0x03),
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_SOME, 0x12, 0x18, /* HID Service */
                  0x0f, 0x18                       /* Battery Service */
                  ),
    //swift pair
#if CONFIG_SHIELD_KEYCHRON_RS87_ANSI || CONFIG_SHIELD_KEYCHRON_RS45_ANSI
    BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA,0x06,0x00, 0x03,0x00,0x80, 'L','E',' ','K','e','y','b','o','a','r','d')
#else    
    BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA,0x06,0x00, 0x03,0x00,0x80, 'K','e','y','c','h','r','o','n',' ','K','B')
#endif     

};
static const struct bt_data zmk_ble_ad_sr[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};


#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static bt_addr_le_t peripheral_addrs[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

static void raise_profile_changed_event(void) {
    raise_zmk_ble_active_profile_changed((struct zmk_ble_active_profile_changed){
        .index = active_profile, .profile = &profiles[active_profile]});
    LOG_DBG("profile change:%d",active_profile);
    if (transfer.adv_type &&!zmk_ble_profile_is_connected(transfer.disc_profile)) {
        LOG_DBG("transfer type:%d,profile:%d",transfer.adv_type,transfer.profile);
        switch (transfer.adv_type) {
        case ZMK_ADV_PAIR:
            transfer.adv_type = 0;
            zmk_ble_prof_pair(transfer.profile);
            break;
        case ZMK_ADV_RECONN:
            transfer.adv_type = 0;
            zmk_ble_prof_select(transfer.profile);
            break;
        }
    }
}

static void raise_profile_changed_event_callback(struct k_work *work) {
    raise_profile_changed_event();
}

K_WORK_DEFINE(raise_profile_changed_event_work, raise_profile_changed_event_callback);

bool zmk_ble_active_profile_is_open(void) {
    return !bt_addr_le_cmp(&profiles[active_profile].peer, BT_ADDR_LE_ANY);
}

void set_profile_address(uint8_t index, const bt_addr_le_t *addr) {
    char setting_name[17];
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    memcpy(&profiles[index].peer, addr, sizeof(bt_addr_le_t));
    sprintf(setting_name, "ble/profiles/%d", index);
    LOG_DBG("Setting profile addr for %s to %s", setting_name, addr_str);
#if IS_ENABLED(CONFIG_SETTINGS)
    settings_enabled_dlps =false;
    settings_save_one(setting_name, &profiles[index], sizeof(struct zmk_ble_profile));
    settings_enabled_dlps = true;
#endif
    k_work_submit(&raise_profile_changed_event_work);
}
bool zmk_ble_profile_is_connected(uint8_t index) {
    struct bt_conn *conn;
    struct bt_conn_info info;
    bt_addr_le_t *addr = &profiles[index].peer;
    if (!bt_addr_le_cmp(addr, BT_ADDR_LE_ANY)) {
        return false;
    } else if ((conn = bt_conn_lookup_addr_le(profiles[index].bt_id, addr)) == NULL) {
        return false;
    }

    bt_conn_get_info(conn, &info);

    bt_conn_unref(conn);

    return info.state == BT_CONN_STATE_CONNECTED;
}
bool zmk_ble_active_profile_is_connected(void) {
    struct bt_conn *conn;
    struct bt_conn_info info;
    bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    if (!bt_addr_le_cmp(addr, BT_ADDR_LE_ANY)) {
        return false;
    } else if ((conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr)) == NULL) {
        return false;
    }

    bt_conn_get_info(conn, &info);

    bt_conn_unref(conn);

    return info.state == BT_CONN_STATE_CONNECTED;
}

#define CHECKED_ADV_STOP()                                                                         \
    err = bt_le_adv_stop();                                                                        \
    advertising_status = ZMK_ADV_NONE;                                                             \
    if (err) {                                                                                     \
        LOG_ERR("Failed to stop advertising (err %d)", err);                                       \
        return err;                                                                                \
    }

#define CHECKED_DIR_ADV()                                                                          \
    addr = zmk_ble_active_profile_addr();                                                          \
    conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, addr);                                            \
    if (conn != NULL) { /* TODO: Check status of connection */                                     \
        LOG_DBG("Skipping advertising, profile host is already connected");                        \
        bt_conn_unref(conn);                                                                       \
        return 0;                                                                                  \
    }                                                                                              \
    err = bt_le_adv_start(BT_LE_ADV_CONN_DIR_LOW_DUTY(addr), zmk_ble_ad, ARRAY_SIZE(zmk_ble_ad),   \
                          NULL, 0);                                                                \
    if (err) {                                                                                     \
        LOG_ERR("Advertising failed to start (err %d)", err);                                      \
        return err;                                                                                \
    }                                                                                              \
    advertising_status = ZMK_ADV_DIR;
#ifndef CONFIG_SHIELD_KEYCHRON_RS45_ANSI  
#define CHECKED_OPEN_ADV()                                                                         \
    zmk_adv_conn_name.id = profiles[active_profile].bt_id;                                         \
    if(zmk_keymap_highest_layer_active()>=2 && (adv_state ==ZMK_ADV_PAIR))                         \
    {                                                                                              \
        err=bt_le_adv_start(&zmk_adv_conn_name, zmk_ble_ad_win, ARRAY_SIZE(zmk_ble_ad_win), zmk_ble_ad_sr, ARRAY_SIZE(zmk_ble_ad_sr));\
    }                                                                                              \
    else                                                                                           \
        err = bt_le_adv_start(&zmk_adv_conn_name, zmk_ble_ad, ARRAY_SIZE(zmk_ble_ad), zmk_ble_ad_sr, ARRAY_SIZE(zmk_ble_ad_sr));    \
    if (err) {                                                                                     \
        LOG_ERR("Advertising failed to start (err %d)", err);                                      \
        return err;                                                                                \
    }                                                                                              \
    advertising_status = ZMK_ADV_CONN;                                                             \
    LOG_WRN("adv id:%d",zmk_adv_conn_name.id);
#else 

#define CHECKED_OPEN_ADV()                                                                         \
    zmk_adv_conn_name.id = profiles[active_profile].bt_id;                                         \
    if( adv_state ==ZMK_ADV_PAIR)                                                                  \
    {                                                                                              \
        err=bt_le_adv_start(&zmk_adv_conn_name, zmk_ble_ad_win, ARRAY_SIZE(zmk_ble_ad_win), zmk_ble_ad_sr, ARRAY_SIZE(zmk_ble_ad_sr));\
    }                                                                                              \
    else                                                                                           \
        err = bt_le_adv_start(&zmk_adv_conn_name, zmk_ble_ad, ARRAY_SIZE(zmk_ble_ad), zmk_ble_ad_sr, ARRAY_SIZE(zmk_ble_ad_sr));    \
    if (err) {                                                                                     \
        LOG_ERR("Advertising failed to start (err %d)", err);                                      \
        return err;                                                                                \
    }                                                                                              \
    advertising_status = ZMK_ADV_CONN;                                                             \
    LOG_WRN("adv id:%d",zmk_adv_conn_name.id);
#endif 

int update_advertising(void) {
    int err = 0;
    bt_addr_le_t *addr;
    struct bt_conn *conn;
    enum advertising_type desired_adv = ZMK_ADV_NONE;
    k_work_cancel_delayable(&sleep_work);
    if (adv_state == ZMK_ADV_RECONN) {
        profiles[active_profile].bt_id = get_reconn_bt_id();
        // blue_led_set_state( LED_PEER_STATE_RECONN);
        raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state=LED_PEER_STATE_RECONN,
                .transport=ZMK_TRANSPORT_BLE,
                .index =active_profile});
        k_work_reschedule(&adv_timeout_work, K_MSEC(ADV_RECONN_TIME_OUT));
        
    } else if (adv_state == ZMK_ADV_PAIR) {
        profiles[active_profile].bt_id = get_pair_bt_id();
        k_work_reschedule(&adv_timeout_work, K_MSEC(ADV_PAIR_TIME_OUT));
        profiles[active_profile].bonded =0;
        // blue_led_set_state( LED_PEER_STATE_PAIR);
        raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state=LED_PEER_STATE_PAIR,
                .transport=ZMK_TRANSPORT_BLE,
                .index=active_profile});
    }
    if (zmk_ble_active_profile_is_open()) {
        desired_adv = ZMK_ADV_CONN;
    } else if (!zmk_ble_active_profile_is_connected()) {
        desired_adv = ZMK_ADV_CONN;
        // Need to fix directed advertising for privacy centrals. See
        // https://github.com/zephyrproject-rtos/zephyr/pull/14984 char
        // addr_str[BT_ADDR_LE_STR_LEN]; bt_addr_le_to_str(zmk_ble_active_profile_addr(), addr_str,
        // sizeof(addr_str));

        // LOG_DBG("Directed advertising to %s", addr_str);
        // desired_adv = ZMK_ADV_DIR;
    }
    LOG_DBG("advertising from %d to %d", advertising_status, desired_adv);

    switch (desired_adv + CURR_ADV(advertising_status)) {
    case ZMK_ADV_NONE + CURR_ADV(ZMK_ADV_DIR):
    case ZMK_ADV_NONE + CURR_ADV(ZMK_ADV_CONN):
        CHECKED_ADV_STOP();
        break;
    case ZMK_ADV_DIR + CURR_ADV(ZMK_ADV_DIR):
    case ZMK_ADV_DIR + CURR_ADV(ZMK_ADV_CONN):
        CHECKED_ADV_STOP();
        CHECKED_DIR_ADV();
        break;
    case ZMK_ADV_DIR + CURR_ADV(ZMK_ADV_NONE):
        CHECKED_DIR_ADV();
        break;
    case ZMK_ADV_CONN + CURR_ADV(ZMK_ADV_CONN): //add
    case ZMK_ADV_CONN + CURR_ADV(ZMK_ADV_DIR):
        CHECKED_ADV_STOP();
        CHECKED_OPEN_ADV();
        break;
    case ZMK_ADV_CONN + CURR_ADV(ZMK_ADV_NONE):
        CHECKED_OPEN_ADV();
        break;
    }

    return 0;
};

static void update_advertising_callback(struct k_work *work) { update_advertising(); }

K_WORK_DEFINE(update_advertising_work, update_advertising_callback);

static void clear_profile_bond(uint8_t profile) {
    if (bt_addr_le_cmp(&profiles[profile].peer, BT_ADDR_LE_ANY)) {
        bt_unpair(BT_ID_DEFAULT, &profiles[profile].peer);
        set_profile_address(profile, BT_ADDR_LE_ANY);
    }
}

void zmk_ble_clear_bonds(void) {
    LOG_DBG("zmk_ble_clear_bonds()");

    clear_profile_bond(active_profile);

    update_advertising();
};

void zmk_ble_clear_all_bonds(void) {
    LOG_DBG("zmk_ble_clear_all_bonds()");

    // Unpair all profiles
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        clear_profile_bond(i);
    }

    // Automatically switch to profile 0
    zmk_ble_prof_select(0);

    update_advertising();
};

int zmk_ble_active_profile_index(void) { return active_profile; }

int zmk_ble_active_profile_btid(void) { return profiles[active_profile].bt_id; }

int zmk_ble_profile_index(const bt_addr_le_t *addr) {
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (bt_addr_le_cmp(addr, &profiles[i].peer) == 0) {
            return i;
        }
    }
    return -ENODEV;
}

#if IS_ENABLED(CONFIG_SETTINGS)
static void ble_save_profile_work(struct k_work *work) {
    settings_enabled_dlps =false;
    settings_save_one("ble/active_profile", &active_profile, sizeof(active_profile));
    settings_enabled_dlps =true;
}

static struct k_work_delayable ble_save_work;
#endif

static int ble_save_profile(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    return k_work_reschedule(&ble_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
#else
    return 0;
#endif
}

int zmk_ble_prof_select(uint8_t index) {
    if (index >= ZMK_BLE_PROFILE_COUNT) {
        return -ERANGE;
    }
    if(zmk_endpoints_selected().transport !=ZMK_TRANSPORT_BLE)
    {
        return -ENOTSUP;
    }
    LOG_DBG("profile %d", index);
    if (active_profile == index && zmk_ble_active_profile_is_connected()) {
        LOG_DBG("skip when connected");
        return 0;
    }
    if (zmk_ble_active_profile_is_connected())
    {
        zmk_ble_prof_disconnect(active_profile);
        transfer.adv_type =ZMK_ADV_RECONN;
        transfer.profile = index;
        transfer.disc_profile = active_profile;
        last_connected =0;
        return -ERANGE;
    }
    if(active_profile !=index)
    {
        active_profile = index;
        ble_save_profile();
    }
    last_connected =0;
    adv_state =ZMK_ADV_RECONN;
    profiles[active_profile].bt_id = get_reconn_bt_id();
    print_device_addr(profiles[active_profile].bt_id);
    update_advertising();

    raise_profile_changed_event();

    return 0;
};

int zmk_ble_prof_pair(uint8_t index) {
    if (index >= ZMK_BLE_PROFILE_COUNT) {
        return -ERANGE;
    }
    if(zmk_endpoints_selected().transport !=ZMK_TRANSPORT_BLE)
    {
        return -ENOTSUP;
    }
    LOG_DBG("profile %d", index);
    k_work_cancel_delayable(&adv_timeout_work);
    if (zmk_ble_active_profile_is_connected()) // wait for disconnect!
    {
        zmk_ble_prof_disconnect(active_profile);
        transfer.adv_type = ZMK_ADV_PAIR;
        transfer.profile = index;
        transfer.disc_profile = active_profile;
        return -ERANGE;
    }
    if (active_profile != index) {
        active_profile = index;
        ble_save_profile();

    }
    profiles[active_profile].bt_id = get_pair_bt_id();
    adv_state =ZMK_ADV_PAIR;
    print_device_addr(profiles[active_profile].bt_id);
    update_advertising();

    raise_profile_changed_event();

    return 0;
};

int zmk_ble_prof_next(void) {
    LOG_DBG("select next profile %d",(active_profile+1)%ZMK_BLE_PROFILE_COUNT);
    return zmk_ble_prof_select((active_profile + 1) % ZMK_BLE_PROFILE_COUNT);
};

int zmk_ble_prof_prev(void) {
    LOG_DBG("select previous profile %d",(active_profile + ZMK_BLE_PROFILE_COUNT - 1) %
                               ZMK_BLE_PROFILE_COUNT);
    return zmk_ble_prof_select((active_profile + ZMK_BLE_PROFILE_COUNT - 1) %
                               ZMK_BLE_PROFILE_COUNT);
};

int zmk_ble_prof_disconnect(uint8_t index) {
    if (index >= ZMK_BLE_PROFILE_COUNT)
        return -ERANGE;

    bt_addr_le_t *addr = &profiles[index].peer;
    struct bt_conn *conn;
    int result;

    if (!bt_addr_le_cmp(addr, BT_ADDR_LE_ANY)) {
        return -ENODEV;
    } else if ((conn = bt_conn_lookup_addr_le(profiles[index].bt_id, addr)) == NULL) {//cc
        return -ENODEV;
    }

    result = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    LOG_DBG("Disconnected from profile %d: %d", index, result);

    bt_conn_unref(conn);
    return result;
}

bt_addr_le_t *zmk_ble_active_profile_addr(void) { return &profiles[active_profile].peer; }

char *zmk_ble_active_profile_name(void) { return profiles[active_profile].name; }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

int zmk_ble_put_peripheral_addr(const bt_addr_le_t *addr) {
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        // If the address is recognized and already stored in settings, return
        // index and no additional action is necessary.
        if (bt_addr_le_cmp(&peripheral_addrs[i], addr) == 0) {
            LOG_DBG("Found existing peripheral address in slot %d", i);
            return i;
        } else {
            char addr_str[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(&peripheral_addrs[i], addr_str, sizeof(addr_str));
            LOG_DBG("peripheral slot %d occupied by %s", i, addr_str);
        }

        // If the peripheral address slot is open, store new peripheral in the
        // slot and return index. This compares against BT_ADDR_LE_ANY as that
        // is the zero value.
        if (bt_addr_le_cmp(&peripheral_addrs[i], BT_ADDR_LE_ANY) == 0) {
            char addr_str[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
            LOG_DBG("Storing peripheral %s in slot %d", addr_str, i);
            bt_addr_le_copy(&peripheral_addrs[i], addr);

            char setting_name[32];
            sprintf(setting_name, "ble/peripheral_addresses/%d", i);
            settings_save_one(setting_name, addr, sizeof(bt_addr_le_t));

            return i;
        }
    }

    // The peripheral does not match a known peripheral and there is no
    // available slot.
    return -ENOMEM;
}

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

#if IS_ENABLED(CONFIG_SETTINGS)

static int ble_profiles_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                                   void *cb_arg) {
    const char *next;

    LOG_DBG("Setting BLE value %s", name);

    if (settings_name_steq(name, "profiles", &next) && next) {
        char *endptr;
        uint8_t idx = strtoul(next, &endptr, 10);
        if (*endptr != '\0') {
            LOG_WRN("Invalid profile index: %s", next);
            return -EINVAL;
        }

        if (len != sizeof(struct zmk_ble_profile)) {
            LOG_ERR("Invalid profile size (got %d expected %d)", len,
                    sizeof(struct zmk_ble_profile));
            return -EINVAL;
        }

        if (idx >= ZMK_BLE_PROFILE_COUNT) {
            LOG_WRN("Profile address for index %d is larger than max of %d", idx,
                    ZMK_BLE_PROFILE_COUNT);
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &profiles[idx], sizeof(struct zmk_ble_profile));
        if (err <= 0) {
            LOG_ERR("Failed to handle profile address from settings (err %d)", err);
            return err;
        }

        char addr_str[BT_ADDR_LE_STR_LEN];
        bt_addr_le_to_str(&profiles[idx].peer, addr_str, sizeof(addr_str));

        LOG_DBG("Loaded %s address for profile %d", addr_str, idx);
    } else if (settings_name_steq(name, "active_profile", &next) && !next) {
        if (len != sizeof(active_profile)) {
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &active_profile, sizeof(active_profile));
        if (err <= 0) {
            LOG_ERR("Failed to handle active profile from settings (err %d)", err);
            return err;
        }
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    else if (settings_name_steq(name, "peripheral_addresses", &next) && next) {
        if (len != sizeof(bt_addr_le_t)) {
            return -EINVAL;
        }

        int i = atoi(next);
        if (i < 0 || i >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
            LOG_ERR("Failed to store peripheral address in memory");
        } else {
            int err = read_cb(cb_arg, &peripheral_addrs[i], sizeof(bt_addr_le_t));
            if (err <= 0) {
                LOG_ERR("Failed to handle peripheral address from settings (err %d)", err);
                return err;
            }
        }
    }
#endif

    return 0;
};

struct settings_handler profiles_handler = {.name = "ble", .h_set = ble_profiles_handle_set};
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

static bool is_conn_active_profile(const struct bt_conn *conn) {
    return bt_addr_le_cmp(bt_conn_get_dst(conn), &profiles[active_profile].peer) == 0;
}
void start_discover(struct bt_conn * conn,uint32_t delay);
static void connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];
    struct bt_conn_info info;
    lowpower_settings();
    LOG_DBG("Connected thread: %p", k_current_get());

    bt_conn_get_info(conn, &info);

    if (info.role != BT_CONN_ROLE_PERIPHERAL) {
        LOG_DBG("SKIPPING FOR ROLE %d", info.role);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    advertising_status = ZMK_ADV_NONE;

    if (err) {
        LOG_WRN("Failed to connect to %s (%u)", addr, err);
        update_advertising();
        return;
    }

    LOG_DBG("Connected %s", addr);
    if(profiles[active_profile].bonded)
        start_discover(conn,3000);

    adv_state =ZMK_ADV_NONE;
    profiles[active_profile].connected =1;

    last_connected =true;

    k_work_cancel_delayable(&adv_timeout_work);
    k_work_cancel_delayable(&sleep_work);
    // blue_led_set_state( LED_PEER_STATE_CONNECTED);
    raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state=LED_PEER_STATE_CONNECTED,
                .transport=ZMK_TRANSPORT_BLE,
                .index=active_profile});  

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)              
    update_advertising();
#endif 
    if (is_conn_active_profile(conn)) {
        LOG_DBG("Active profile connected");
        
        k_work_submit(&raise_profile_changed_event_work);
    }
    k_work_reschedule(&battery_report_work,K_MSEC(2500));

    k_work_reschedule(&update_conn_param_work,K_MSEC(5000));  
}



static void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];
    struct bt_conn_info info;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_DBG("Disconnected from %s (reason 0x%02x)", addr, reason);

    bt_conn_get_info(conn, &info);

    if (info.role != BT_CONN_ROLE_PERIPHERAL) {
        LOG_DBG("SKIPPING FOR ROLE %d", info.role);
        return;
    }
    enter_lowpower();
    profiles[active_profile].connected =0;
    if ((reason != BT_HCI_ERR_REMOTE_USER_TERM_CONN) &&
        (reason != BT_HCI_ERR_LOCALHOST_TERM_CONN) && (reason != BT_HCI_ERR_REMOTE_POWER_OFF))
    // We need to do this in a work callback, otherwise the advertising update will still see the
    // connection for a profile as active, and not start advertising yet.
    {
        adv_state =ZMK_ADV_RECONN;
        k_work_submit(&update_advertising_work);
    }
    else if( transfer.adv_type==0)
    {
        k_work_reschedule(&sleep_work, K_MSEC(10000));
    }

    struct zmk_hid_led_report_body led_report_body ={
        .leds =0,
    };
    struct zmk_endpoint_instance endpoint = {
        .transport = ZMK_TRANSPORT_BLE,
        .ble = {
            .profile_index = active_profile,
            }
    };
    zmk_hid_indicators_process_report(&led_report_body, endpoint);

    raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state=LED_PEER_STATE_DISCONNECTED,
                .transport=ZMK_TRANSPORT_BLE,
                .index=active_profile});  

    if (is_conn_active_profile(conn)) {
        LOG_DBG("Active profile disconnected");
        k_work_submit(&raise_profile_changed_event_work);
    }
    k_work_cancel_delayable(&update_conn_param_work);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        LOG_DBG("Security changed: %s level %u", addr, level);
        if(level >= BT_SECURITY_L2)
        {
            bt_addr_le_t *addr = zmk_ble_active_profile_addr();
            if (!bt_addr_le_cmp(addr, BT_ADDR_LE_ANY)) {
                set_profile_address(active_profile, bt_conn_get_dst(conn));
                update_bt_id(conn->id);
                profiles[active_profile].bonded =1;
                start_discover(conn,1000);
            }
        }
    } else {
        LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
         if(err ==BT_SECURITY_ERR_PIN_OR_KEY_MISSING 
            ||err == BT_SECURITY_ERR_AUTH_REQUIREMENT) 
         {
            bt_unpair(profiles[active_profile].bt_id, bt_conn_get_dst(conn));
            LOG_ERR("unpair,id:%d",profiles[active_profile].bt_id);
         }
    }
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                             uint16_t timeout) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_DBG("%s: interval %d latency %d timeout %d", addr, interval, latency, timeout);
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
    .le_param_updated = le_param_updated,
};

/*
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_DBG("Passkey for %s: %06u", addr, passkey);
}
*/

#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)

static void auth_passkey_entry(struct bt_conn *conn) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_DBG("Passkey entry requested for %s", addr);
    ring_buf_reset(&passkey_entries);
    auth_passkey_entry_conn = bt_conn_ref(conn);
}

#endif

static void auth_cancel(struct bt_conn *conn) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)
    if (auth_passkey_entry_conn) {
        bt_conn_unref(auth_passkey_entry_conn);
        auth_passkey_entry_conn = NULL;
    }

    ring_buf_reset(&passkey_entries);
#endif

    LOG_DBG("Pairing cancelled: %s", addr);
}
#if 0
static bool pairing_allowed_for_current_profile(struct bt_conn *conn) {
    return zmk_ble_active_profile_is_open() ||
           (IS_ENABLED(CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE) &&
            bt_addr_le_cmp(zmk_ble_active_profile_addr(), bt_conn_get_dst(conn)) == 0);
}
#endif 
static enum bt_security_err auth_pairing_accept(struct bt_conn *conn,
                                                const struct bt_conn_pairing_feat *const feat) {
    struct bt_conn_info info;
    bt_conn_get_info(conn, &info);

    LOG_DBG("role %d, open? %s", info.role, zmk_ble_active_profile_is_open() ? "yes" : "no");
    // if (info.role == BT_CONN_ROLE_PERIPHERAL && !pairing_allowed_for_current_profile(conn)) {
    //     LOG_WRN("Rejecting pairing request to taken profile %d", active_profile);
    //     return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
    // }

    return BT_SECURITY_ERR_SUCCESS;
};

static void auth_pairing_complete(struct bt_conn *conn, bool bonded) {
    struct bt_conn_info info;
    char addr[BT_ADDR_LE_STR_LEN];
    const bt_addr_le_t *dst = bt_conn_get_dst(conn);

    bt_addr_le_to_str(dst, addr, sizeof(addr));
    bt_conn_get_info(conn, &info);

    if (info.role != BT_CONN_ROLE_PERIPHERAL) {
        LOG_DBG("SKIPPING FOR ROLE %d", info.role);
        return;
    }

    // if (!pairing_allowed_for_current_profile(conn)) {
    //     LOG_ERR("Pairing completed but current profile is not open: %s", addr);
    //     bt_unpair(BT_ID_DEFAULT, dst);
    //     return;
    // }

    set_profile_address(active_profile, dst);
    // update_advertising();
    update_bt_id(conn->id);
    copy_profile_to_same_peer(dst);
    profiles[active_profile].bonded =1;
    start_discover(conn,1000);
};

static struct bt_conn_auth_cb zmk_ble_auth_cb_display = {
    .pairing_accept = auth_pairing_accept,
// .passkey_display = auth_passkey_display,

#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)
    .passkey_entry = auth_passkey_entry,
#endif
    .cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb zmk_ble_auth_info_cb_display = {
    .pairing_complete = auth_pairing_complete,
};

static void zmk_ble_ready(int err) {
    LOG_DBG("ready? %d", err);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }
    LOG_WRN("active_profile:%d",active_profile);
    update_advertising();
}

int zmk_ble_init(void) {
    
//add flag to prevent multi init!
    static bool inited=false;
    if(bat_is_shutdown() && !zmk_usb_power_on()) return -1;
    if(inited)
    {
        // if(adv_state== ZMK_ADV_NONE)
        // {
        //     LOG_DBG("zmk_ble_init,restart adv");
        //     adv_state=ZMK_ADV_RECONN;
        //     update_advertising();
        // }
        zmk_ble_reconn();
        return 0;
    }
    inited =true;
//---

    int err = bt_enable(NULL);

    if (err) {
        LOG_ERR("BLUETOOTH FAILED (%d)", err);
        return err;
    }

#if IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();

    err = settings_register(&profiles_handler);
    if (err) {
        LOG_ERR("Failed to setup the profile settings handler (err %d)", err);
        return err;
    }

    k_work_init_delayable(&ble_save_work, ble_save_profile_work);
    settings_load_subtree("ble");
    settings_load_subtree("bt");

#endif
    load_identities();

    // if(delay_clear_bonds())
    // {
    //     zmk_ble_reset();
    //     set_delay_clear_bonds(0);
    // }

#if IS_ENABLED(CONFIG_ZMK_BLE_CLEAR_BONDS_ON_START)
    LOG_WRN("Clearing all existing BLE bond information from the keyboard");

    bt_unpair(BT_ID_DEFAULT, NULL);

    for (int i = 0; i < 8; i++) {
        char setting_name[15];
        sprintf(setting_name, "ble/profiles/%d", i);

        err = settings_delete(setting_name);
        if (err) {
            LOG_ERR("Failed to delete setting: %d", err);
        }
    }

    // Hardcoding a reasonable hardcoded value of peripheral addresses
    // to clear so we properly clear a split central as well.
    for (int i = 0; i < 8; i++) {
        char setting_name[32];
        sprintf(setting_name, "ble/peripheral_addresses/%d", i);

        err = settings_delete(setting_name);
        if (err) {
            LOG_ERR("Failed to delete setting: %d", err);
        }
    }

#endif // IS_ENABLED(CONFIG_ZMK_BLE_CLEAR_BONDS_ON_START)
    bt_conn_cb_register(&conn_callbacks);
    bt_conn_auth_cb_register(&zmk_ble_auth_cb_display);
    bt_conn_auth_info_cb_register(&zmk_ble_auth_info_cb_display);

    zmk_ble_ready(0);
    return 0;
}

int zmk_ble_deinit(void)
{
    bt_disable();
    return 0;
}

#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)

static bool zmk_ble_numeric_usage_to_value(const zmk_key_t key, const zmk_key_t one,
                                           const zmk_key_t zero, uint8_t *value) {
    if (key < one || key > zero) {
        return false;
    }

    *value = (key == zero) ? 0 : (key - one + 1);
    return true;
}

static int zmk_ble_handle_key_user(struct zmk_keycode_state_changed *event) {
    zmk_key_t key = event->keycode;

    LOG_DBG("key %d", key);

    if (!auth_passkey_entry_conn) {
        LOG_DBG("No connection for passkey entry");
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (event->state) {
        LOG_DBG("Key press, ignoring");
        return ZMK_EV_EVENT_HANDLED;
    }

    if (key == HID_USAGE_KEY_KEYBOARD_ESCAPE) {
        bt_conn_auth_cancel(auth_passkey_entry_conn);
        return ZMK_EV_EVENT_HANDLED;
    }

    if (key == HID_USAGE_KEY_KEYBOARD_RETURN || key == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER) {
        uint8_t digits[PASSKEY_DIGITS];
        uint32_t count = ring_buf_get(&passkey_entries, digits, PASSKEY_DIGITS);

        uint32_t passkey = 0;
        for (int i = 0; i < count; i++) {
            passkey = (passkey * 10) + digits[i];
        }

        LOG_DBG("Final passkey: %d", passkey);
        bt_conn_auth_passkey_entry(auth_passkey_entry_conn, passkey);
        bt_conn_unref(auth_passkey_entry_conn);
        auth_passkey_entry_conn = NULL;
        return ZMK_EV_EVENT_HANDLED;
    }

    uint8_t val;
    if (!(zmk_ble_numeric_usage_to_value(key, HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION,
                                         HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS, &val) ||
          zmk_ble_numeric_usage_to_value(key, HID_USAGE_KEY_KEYPAD_1_AND_END,
                                         HID_USAGE_KEY_KEYPAD_0_AND_INSERT, &val))) {
        LOG_DBG("Key not a number, ignoring");
        return ZMK_EV_EVENT_HANDLED;
    }

    if (ring_buf_space_get(&passkey_entries) <= 0) {
        uint8_t discard_val;
        ring_buf_get(&passkey_entries, &discard_val, 1);
    }
    ring_buf_put(&passkey_entries, &val, 1);
    LOG_DBG("value entered: %d, digits collected so far: %d", val,
            ring_buf_size_get(&passkey_entries));

    return ZMK_EV_EVENT_HANDLED;
}

static int zmk_ble_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *kc_state;

    kc_state = as_zmk_keycode_state_changed(eh);

    if (kc_state != NULL) {
        return zmk_ble_handle_key_user(kc_state);
    }

    return 0;
}

ZMK_LISTENER(zmk_ble, zmk_ble_listener);
ZMK_SUBSCRIPTION(zmk_ble, zmk_keycode_state_changed);
#endif /* IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY) */


//SYS_INIT(zmk_ble_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);

static void load_identities(void) {

    bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
    int i;
    size_t count = ARRAY_SIZE(addrs);

    bt_id_get(addrs, &count);

    LOG_INF("Device has %zu identities\n", count);

    /* Default identity should always exist */
    for (i = 0; i < count; i++) {
        char addr_str[BT_ADDR_LE_STR_LEN] = {0};

        bt_addr_le_to_str(&addrs[i], addr_str, sizeof(addr_str));
        LOG_INF("Device i:%d,addr:%s\n", i, addr_str);
    }

    for (; count < CONFIG_BT_ID_MAX; count++) {
        int err = bt_id_create(NULL, NULL);

        if (err < 0) {
            LOG_ERR("Cannot create identity (err:%d)\n", err);

            break;
        } else {

            LOG_INF("Identity %zu created\n", count);
        }
    }
    for(i=0;i<ZMK_BLE_PROFILE_COUNT;i++)
    {
        // profiles[i].bt_id =i*2+1;
        // profiles[i].bonded =0;
        profiles[i].connected =0;
    }
    for(i=0;i<ZMK_BLE_PROFILE_COUNT;i++)
    {
        if(bt_addr_le_cmp(&profiles[i].peer, BT_ADDR_LE_ANY))
        {
            uint8_t id = i*2+1;
            LOG_DBG("check id:%d",id);
            if(bt_keys_find(BT_KEYS_LTK|BT_KEYS_LTK_P256,id,&profiles[i].peer)!=NULL)
            {
                profiles[i].bt_id =id;
                profiles[i].bonded =1;
                LOG_WRN("profile:%d,bt_id:%d,bonded",i,profiles[i].bt_id);
            }
            else if(bt_keys_find(BT_KEYS_LTK|BT_KEYS_LTK_P256,id+1,&profiles[i].peer)!=NULL)
            {
                profiles[i].bt_id =id+1;
                profiles[i].bonded =1;
                LOG_WRN("profile:%d,bt_id:%d,bonded",i,profiles[i].bt_id);
            }
        }
    }
    for(i=0;i<ZMK_BLE_PROFILE_COUNT;i++)
    {
        LOG_DBG("profile:%d,bt_id:%d,bonded:%d",i,profiles[i].bt_id,profiles[i].bonded);
    }
    // for(i=1;i<CONFIG_BT_ID_MAX;i++)
    // {
    //    struct bt_keys * keys= bt_keys_find(BT_KEYS_ALL,1,&profiles[0].peer);
    //    if(keys)
    //    {
    //         LOG_WRN("keys flag:%02x",keys->keys);
    //    }
    // }
}
static uint8_t get_reconn_bt_id(void)
{
    uint8_t id = active_profile *2+1;
    if(!bt_addr_le_cmp(&profiles[active_profile].peer, BT_ADDR_LE_ANY))
    {
        return id;
    }
    else
    {
        if(bt_keys_find(BT_KEYS_LTK|BT_KEYS_LTK_P256,profiles[active_profile].bt_id,&profiles[active_profile].peer)!=NULL)
        {
            return profiles[active_profile].bt_id;
        }
        else 
        {
            if(bt_keys_find(BT_KEYS_LTK|BT_KEYS_LTK_P256,id,&profiles[active_profile].peer)!=NULL)
            {
                return id;
            }
            else if(bt_keys_find(BT_KEYS_LTK|BT_KEYS_LTK_P256,id+1,&profiles[active_profile].peer)!=NULL)
                return id+1;
            else 
            {
                LOG_WRN("reset bt id:%d",id);
                set_profile_address(active_profile,BT_ADDR_LE_ANY);
                bt_id_reset(id,inc_bt_addr(id),NULL);
                return id;
            }
        }
    }
    
        
}
static uint8_t get_pair_bt_id(void)
{
    uint8_t id= active_profile*2+1;
    if(!bt_addr_le_cmp(&profiles[active_profile].peer, BT_ADDR_LE_ANY))
        return id;
    else
    {
        for(int i=0;i<2;i++)
        {
            if(bt_keys_find(BT_KEYS_LTK|BT_KEYS_LTK_P256,id+i,&profiles[active_profile].peer)!=NULL)
            {
                LOG_DBG("id:%d,bonded",id+i);
                uint8_t next_id =id+(1-i);
                LOG_DBG("pair id:%d",next_id);
                bt_id_reset(next_id,inc_bt_addr(next_id),NULL);
                return next_id;
            }
            else
            {
                LOG_DBG("id:%d,Not bonded",id+i);
                bt_id_reset(id+i,inc_bt_addr(id+i),NULL);
                return id+i;
            }

        }
        return id;
    }

}

static bt_addr_le_t * inc_bt_addr(uint8_t index)
{
    static bt_addr_le_t inced_bt_addr;
    bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
    size_t count = ARRAY_SIZE(addrs);
    bt_id_get(addrs, &count);
    LOG_DBG("bt id:%d,count:%d",index,count);
    if(index <count)
    {
        char addr_str[30] = {0};
        bt_addr_le_to_str(&addrs[index], addr_str, sizeof(addr_str));
        LOG_INF("old Device addr:%s\n", addr_str);
        addrs[index].a.val[0] +=1;
        bt_addr_le_to_str(&addrs[index], addr_str, sizeof(addr_str));
        LOG_INF("New Device addr:%s\n", addr_str);
        memcpy(&inced_bt_addr,&addrs[index],sizeof(bt_addr_le_t));
        return &inced_bt_addr;
    }
    else
    {
        return NULL;
    }
}
static void bond_cnt_cb(const struct bt_bond_info *info, void *user_data) {
    size_t *cnt = user_data;
#if 0    
    uint8_t id = *cnt >>4;
    *cnt &=0x0f;
    LOG_DBG("id:%d",id);
    struct bt_keys *key=bt_keys_find(BT_KEYS_LTK|BT_KEYS_LTK_P256,id,&info->addr);
    if(key)
    {
        LOG_DBG("key flag:%02x",key->keys);
    }
    char addr_str[30] = {0};
    bt_addr_le_to_str(&info->addr, addr_str, sizeof(addr_str));
    LOG_INF("addr:%s\n", addr_str);
    bt_id_reset(id,inc_bt_addr(id),NULL);
#endif     
    (*cnt)++;
}

static size_t bond_check(uint8_t local_id) {
    size_t cnt = 0;// |(local_id<<4);

    bt_foreach_bond(local_id, bond_cnt_cb, &cnt);
    return cnt;
}
static void update_bt_id(uint8_t bt_id)
{
    uint8_t base=active_profile*2 +1;
    uint8_t next_id = ((bt_id - base) +1)%2 + base ;   
    LOG_DBG("bt_id:%d,next:%d",bt_id,next_id);
    uint8_t bond=bond_check(next_id);
    if(bond)
    {
        LOG_WRN("bt id reset:%d",next_id);
        bt_id_reset(next_id,inc_bt_addr(next_id),NULL);
    }
}

void adv_timeout_work_callback(struct k_work *work) {
    int err = bt_le_adv_stop();
    advertising_status = ZMK_ADV_NONE;
    // blue_led_set_state( LED_PEER_STATE_DISCONNECTED);
    raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state=LED_PEER_STATE_DISCONNECTED,
                .transport=ZMK_TRANSPORT_BLE,
                .index=active_profile});

    LOG_INF("adv timeout ,stop");
    if (err) {
        LOG_ERR("Failed to stop advertising (err %d)", err);
    }
    if(adv_state ==ZMK_ADV_RECONN)
    {
        k_work_reschedule(&sleep_work, last_connected?K_MSEC(10000-ADV_RECONN_TIME_OUT): K_MSEC(40*1000-ADV_RECONN_TIME_OUT));
        last_connected =0;
    }
    else
        k_work_reschedule(&sleep_work, K_MSEC(40*1000));
    adv_state = ZMK_ADV_NONE;  
    enter_lowpower();
}

void sleep_worker(struct k_work *work)
{
    LOG_INF("sleep");
    set_force_sleep(true);
}

void set_force_sleep(bool enable)
{
    force_sleep = enable;
    // is_app_enabled_dlps =true;
}

bool force_to_sleep(void)
{
    return force_sleep;
}

void zmk_ble_reconn(void)
{
    if(adv_state == ZMK_ADV_NONE)
    {
        if(!profiles[active_profile].connected)
        {
            LOG_WRN("zmk_ble_reconn");
            zmk_ble_prof_select(active_profile);
        }
    }
    else if (adv_state== ZMK_ADV_RECONN)
    {
        raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state=LED_PEER_STATE_RECONN,
                .transport=ZMK_TRANSPORT_BLE,
                .index =active_profile});
        k_work_reschedule(&adv_timeout_work, K_MSEC(ADV_RECONN_TIME_OUT));
    }
}
int zmk_ble_reset(void) 
{
    char setting_name[20];
    LOG_DBG("");
    
    int i;
    for(i=1;i<CONFIG_BT_ID_MAX;i++)
    {
       int ret= bt_id_reset(i, inc_bt_addr(i), NULL); 
       LOG_DBG("bt id reset:%d",ret);
    }

    for(i=0;i<ZMK_BLE_PROFILE_COUNT;i++)
    {
        sprintf(setting_name, "ble/profiles/%d", i);
        settings_delete(setting_name);
    }
    settings_delete("ble/active_profile");
    return 0;
};
void app_wdt_feed(void);
void led_recover(uint8_t stop_rgb);
void led_rgb_set_color(uint8_t r,uint8_t g,uint8_t b);
static struct k_work_delayable factory_recover_work;
static void factory_recover_work_cb(struct k_work *work)
{
    static uint8_t state =1;
    LOG_ERR("recover state:%d",state);
    // DBG_DIRECT("recover state:%d",state);
    
    app_wdt_feed();//Note:must feed dog when erase flash!
    aon_write_state(AON_STATE_RECOVER);
    if(state ==1)
    {
        led_recover(1);    
        k_work_schedule(&factory_recover_work, K_MSEC(10));    
        state++;
    }
    else if(state ==2)
    {
#ifdef CONFIG_ZMK_SSD1306        
        void disp_reset(void);
        disp_reset();
#endif         
        k_work_reschedule(&factory_recover_work, K_MSEC(1700));
        state++;
    }
    else
    {
        zmk_hid_keyboard_clear();
        zmk_hid_consumer_clear();
        zmk_endpoints_send_report(HID_USAGE_KEY);
        zmk_endpoints_send_report(HID_USAGE_CONSUMER);
        settings_enabled_dlps =false;
        sync_bond_info_t bond_info;
        int rc =sync_nvm_get_bond_info(&bond_info);
        LOG_INF("ppt bond ret:%d",rc);
        int zmk_settings_erase(void);
        rc=zmk_settings_erase();
        LOG_INF("settings erase ret:%d",rc);
        extern bool settings_subsys_initialized;
        settings_subsys_initialized =false;
        rc =settings_subsys_init();
        LOG_INF("settings_subsys_init ret:%d",rc);
        rc=sync_nvm_set_bond_info(&bond_info);
        LOG_INF("sync_nvm_set_bond_info ret:%d",rc);

        LOG_ERR("reset!!");
        // DBG_DIRECT("reset!!!");
        k_msleep(50);
        app_system_reset(WDT_FLAG_RESET_SOC);
    }
}

void zmk_factory_recover(void)
{
    LOG_ERR("zmk_factory_recover");
    // if(zmk_endpoints_selected().transport==ZMK_TRANSPORT_BLE)
    // {
    //     zmk_ble_prof_disconnect(active_profile);    
    //     k_msleep(100);
    //     bt_le_adv_stop();
    // }
    // else
    // {
    //     void zmk_ppt_disconnect(void);
    //     zmk_ppt_disconnect();
    //     // set_delay_clear_bonds(1);
    // }
    
    k_work_init_delayable(&factory_recover_work, factory_recover_work_cb);
    k_work_reschedule(&factory_recover_work, K_MSEC(200));
#ifdef  CONFIG_LED_STRIP    
    led_rgb_set_color(0,0,0);
#endif     
}
// int load_immediate_value(const char *name, void *dest, size_t len);
// uint8_t delay_clear_bonds(void)
// {
//     uint8_t delay_clear_bond=0;
//     int rc =load_immediate_value("delay_clear_bond", &delay_clear_bond, sizeof(delay_clear_bond));
//     if (rc == -ENOENT) {
//         delay_clear_bond = 0;
//         LOG_DBG("delay_clear_bond:%d,default",delay_clear_bond);
//     }
//     else if(rc ==0)
//     {
//         LOG_DBG("delay_clear_bond:%d",delay_clear_bond);
//     }
//     return delay_clear_bond;
// }
// void set_delay_clear_bonds(uint8_t set)
// {
//     uint8_t delay_clear_bond=set;
//     LOG_ERR("set_delay_clear_bonds:%d",set);
//     settings_save_one("delay_clear_bond", &delay_clear_bond, sizeof(delay_clear_bond));
// }
void zmk_ble_sleep(void)
{
    zmk_ble_prof_disconnect(active_profile);
    k_work_cancel_delayable(&adv_timeout_work);
    bt_le_adv_stop();
}
void print_device_addr(uint8_t id)
{
    bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
    size_t count = ARRAY_SIZE(addrs);
    bt_id_get(addrs, &count);
    if(id <count)
    {
        char addr_str[64] = {0};

        bt_addr_le_to_str(&addrs[id], addr_str, sizeof(addr_str));
        LOG_WRN("Device addr:%s\n", addr_str);

    }
    else
    {
        LOG_INF("Device addr NULL");
    }
}
void copy_profile_to_same_peer(const bt_addr_le_t *peer) {
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (!bt_addr_le_cmp(&profiles[i].peer, peer) &&
            (profiles[i].bt_id != profiles[active_profile].bt_id)) {
            profiles[i].bt_id = profiles[active_profile].bt_id;
            profiles[i].bonded = 1;
            char setting_name[20];
            sprintf(setting_name, "ble/profiles/%d", i);
            settings_enabled_dlps =false;
            settings_save_one(setting_name, &profiles[i], sizeof(struct zmk_ble_profile));
            LOG_INF("copy profle %d to %d,id:%d", active_profile, i,profiles[i].bt_id);
            settings_enabled_dlps = true;
        }
    }
}

int  ble_is_pairing(void)
{
    if(adv_state == ZMK_ADV_PAIR)
        return active_profile;
    else
        return -1;
}

uint8_t get_os_type(void);
void update_conn_param_worker(struct k_work *work)
{
    int rc=0;
    struct bt_le_conn_param *active_param =BT_LE_CONN_PARAM(6,6,30,400);
    if(get_os_type())
    {
        //mac os
        active_param->interval_max = 12;
        active_param->interval_min = 12;
    }
    struct bt_conn *conn;
    bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    
    conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr);
    if (conn != NULL) {
        LOG_DBG("conn:%p,state:%d",conn,conn->state);
        struct bt_conn_info info ;
        bt_conn_get_info(conn,&info);
        LOG_ERR("conn interval:%d,latency:%d,timeout:%d",info.le.interval,info.le.latency,info.le.timeout);
        //if(info.le.latency==0 || info.le.interval !=6)
        {
            LOG_ERR("change to inteval:%d",active_param->interval_max);
            rc= bt_conn_le_param_update(conn,active_param);
            LOG_ERR("rc:%d",rc);
        }
        bt_conn_unref(conn);
    }
}

uint8_t zmk_ble_is_ready(void)
{
    return profiles[active_profile].connected;
}