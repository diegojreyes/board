/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_keychron

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <drivers/behavior.h>

#include <dt-bindings/zmk/keychron.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/app_wdt.h>
#include <zephyr/drivers/watchdog.h>
#include <zmk/leds.h>
#include <zephyr/logging/log.h>
#include <zmk/events/led_state_changed.h>
#include <zmk/leds.h>
#include <zmk/ppt/keyboard_ppt_app.h>
#include <zmk/hid.h>
#include <zmk/usb.h>
#include <zmk/battery.h>
#if CONFIG_LED_STRIP  
#include "../rgb/rgb_matrix.h" 
#endif
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/gpio.h>
#ifdef CONFIG_ZMK_SSD1306
#include <zmk/disp.h>
#endif
#include <zmk/keymap.h> 

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

extern struct k_work_delayable sleep_work; 
void set_usb_report_rate(void);
void set_ppt_report_rate(void);
void save_mode(enum zmk_transport);
void set_disp_mode(uint8_t mode);
void change_to_ble(uint8_t index);
void change_to_ppt(void);
#ifdef CONFIG_ENABLE_WIN_LOCK
#warning "enable winlock"
void launcher_update_winlock(uint8_t winlock);
void winlock_led_set(void);
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
const struct gpio_dt_spec led_winlock=GPIO_DT_SPEC_GET(DT_NODELABEL(win_led),gpios);
#endif 
uint8_t fn_win_lock=0;
static uint8_t backup_winlock;
void winlock_led_onoff(uint8_t onoff);
#endif
const struct gpio_dt_spec usb_det= GPIO_DT_SPEC_GET(DT_NODELABEL(usb_det),  gpios);


#if CONFIG_SHIELD_KEYCHRON_RS45_ANSI
static uint8_t mode_wireless;
static  enum zmk_transport stored_transport;
#endif 

void lowpower_settings(void);
void exit_lowpower(void);

static enum zmk_transport hardware_select_transport =ZMK_TRANSPORT_USB;
static uint8_t last_trans;
static uint8_t bat_charge_state;
static uint8_t usb_power_on;

uint8_t zmk_usb_power_on(void)
{
    return usb_power_on;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
#if CONFIG_SHIELD_KEYCHRON_RS45_ANSI                                         
    LOG_WRN("kc pressed:%d,%d",binding->param1,binding->param2);
    if(binding->param1 == BT_PAIR || binding->param1 == BT_SEL)
    {
        change_to_ble(binding->param2);
    }   
    else if(binding->param1 == PPT_PAIR || binding->param1 == PPT_SEL)
    {
        change_to_ppt();
    }   
#endif                                        
    switch (binding->param1) {
    case BT_PAIR:
        return zmk_ble_prof_pair(binding->param2);
    case BT_SEL:
        return zmk_ble_prof_select(binding->param2);
    case PPT_PAIR:
        keyboard_ppt_pair();
        return 0;
    case PPT_SEL:
        zmk_ppt_reconn();
        return 0;

    case MOD_USB:
        LOG_WRN("usb det,cur transport:%d",hardware_select_transport);
        usb_power_on =1;
        clear_bat_shutdown();
#if CONFIG_SHIELD_KEYCHRON_RS45_ANSI 
        if(!mode_wireless)//ZMK_TRANSPORT_USB) 
#else
        if(hardware_select_transport==ZMK_TRANSPORT_USB)  
#endif 
        {
            LOG_WRN("select usb");
        #if CONFIG_LED_STRIP
            enable_rgb_thread();
        #endif 
        #if CONFIG_ENABLE_GPIO_LED	
            gpio_led_power_on();
        #endif 
        #if CONFIG_SHIELD_KEYCHRON_RS45_ANSI 
            // if(hardware_select_transport == ZMK_TRANSPORT_BLE || hardware_select_transport==ZMK_TRANSPORT_PPT)
            // {
            //     zmk_usb_deinit();
            // }
            // save_mode(ZMK_TRANSPORT_USB);
            led_bat_display_off();
            hardware_select_transport = ZMK_TRANSPORT_USB;
        #endif     
            k_work_cancel_delayable(&sleep_work);
            // lowpower_settings();
            set_usb_report_rate();
            return zmk_endpoints_select_transport(hardware_select_transport);
        }
        else
        {
            zmk_usb_init();
            exit_lowpower();
#ifdef CONFIG_LED_STRIP            
            if(bat_is_low())
                enable_rgb_thread();
#endif                
        }       
        return 0;
    case MOD_BLE:
        LOG_WRN("to ble,last trans:%d,cur:%d",last_trans,zmk_endpoints_selected().transport);
        if(zmk_endpoints_selected().transport !=ZMK_TRANSPORT_PPT
            && last_trans !=ZMK_TRANSPORT_PPT)
        {
            hardware_select_transport=ZMK_TRANSPORT_BLE;
            last_trans =ZMK_TRANSPORT_BLE;
            LOG_WRN("select ble");
        #if CONFIG_ENABLE_GPIO_LED	 
            gpio_led_power_on();
            // aon_write_reg(0xf0);
            aon_clear_state(AON_STATE_SLEEP);
        #endif  
            return zmk_endpoints_select_transport(hardware_select_transport);
        }
        else
        {
            LOG_WRN("reboot for ble");
            LOG_PANIC();
#if CONFIG_LED_STRIP            
            zmk_rgb_matrix_off();
#endif            
            app_system_reset(WDT_FLAG_RESET_SOC);
        }
        return 0;
    case MOD_PPT:
        LOG_WRN("to ppt,last trans:%d,cur:%d",last_trans,zmk_endpoints_selected().transport);
        if(zmk_endpoints_selected().transport !=ZMK_TRANSPORT_BLE
            && last_trans !=ZMK_TRANSPORT_BLE)
        {
            hardware_select_transport=ZMK_TRANSPORT_PPT;
            last_trans =ZMK_TRANSPORT_PPT;
            LOG_WRN("select ppt");
        #if CONFIG_ENABLE_GPIO_LED	
            gpio_led_power_on();
            // aon_write_reg(0xf0);
            aon_clear_state(AON_STATE_SLEEP);
        #endif 
            set_ppt_report_rate();
            return zmk_endpoints_select_transport(hardware_select_transport);
        }
        else
        {
            LOG_WRN("reboot for ppt");
            LOG_PANIC();
#if CONFIG_LED_STRIP            
            zmk_rgb_matrix_off();
#endif             
            app_system_reset(WDT_FLAG_RESET_SOC);
        }
        return 0;
    case BAT_CHG:
        LOG_WRN("Charging");
        led_charge_set_state(LED_BAT_CHARGING);
        bat_charge_state=1;
        return 0;
    case BAT_CHGD:
        LOG_WRN("Charge done");
        led_charge_set_state(LED_BAT_CHARGE_DONE);
        return 0;
    case BAT_INFO:
        if(zmk_endpoints_selected().transport !=ZMK_TRANSPORT_USB)
        led_bat_display();
        return 0;
    case RECOVER:
        void zmk_factory_recover(void);
        zmk_factory_recover();
        return 0;
    case MOD_NKRO:
        zmk_toggle_nkro();
        return 0;
#if CONFIG_LED_STRIP        
    case RGB_TEST:    
        void zmk_rgb_test_start(void);
        zmk_rgb_test_start();
        return 0;
#endif    
#ifdef CONFIG_ENABLE_WIN_LOCK
    case FN_WIN:
        
        {
            uint8_t cur = (binding->param2==0)?1:0;
            if(cur != fn_win_lock)
            {
                fn_win_lock = cur;
                backup_winlock = fn_win_lock;
                launcher_update_winlock(fn_win_lock);
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR       
                if(zmk_ble_is_ready() || zmk_ppt_is_ready()|| zmk_endpoints_selected().transport ==ZMK_TRANSPORT_USB)
                    winlock_led_set();         
#endif        
            }
            
            LOG_ERR("fn win lock:%d,param2:%d",fn_win_lock,binding->param2);
        }
        
        return 0; 
#endif        
#if CONFIG_SHIELD_KEYCHRON_RS45_ANSI        
    case MOD_WIRELESS:
        LOG_WRN("set wireless mode");
        settings_load_subtree("savemode");
        mode_wireless =1;
        if(!stored_transport )
            save_mode(ZMK_TRANSPORT_BLE);

        hardware_select_transport = stored_transport;
        
        zmk_endpoints_select_transport(hardware_select_transport);
        return 0;  
#endif    
#ifdef CONFIG_SOFTWARE_SWITCH_LAYER
    case SWITCH_LAYER:
        LOG_ERR("switch layer:%d",binding->param2);
        {
            #define MAC_LAYER 0
            #define WIN_LAYER 2
            
            zmk_keymap_layer_deactivate(binding->param2?MAC_LAYER:WIN_LAYER);
            zmk_keymap_layer_activate(binding->param2?WIN_LAYER:MAC_LAYER);
            void save_mac_win_layer(uint8_t win);
            save_mac_win_layer(binding->param2);
            led_recover(0);
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR   
            fn_win_lock =backup_winlock;
            if(binding->param2==0)  
            {
                backup_winlock =fn_win_lock;
                fn_win_lock =0;
            }          
            winlock_led_set(); 
#endif
        }
        return 0;
#endif
    default:
        LOG_ERR("Unknown keychron command: %d", binding->param1);
    }

    return -ENOTSUP;
}
static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {

    switch (binding->param1) {

    case BAT_CHG:
        bat_charge_state=0;
        led_charge_set_state(LED_BAT_NONE);
        return 0;
    case BAT_CHGD:
        if(bat_charge_state)
            led_charge_set_state(LED_BAT_CHARGING);
        else
            led_charge_set_state(LED_BAT_NONE);
        return 0;
    case BAT_INFO:
// #ifndef CONFIG_LED_STRIP    
//         led_bat_display_off();
// #endif 
        return 0;
    case MOD_USB: 
        LOG_WRN("usb lost");
        usb_power_on =0;
        lowpower_settings();
        if(zmk_usb_get_status()!=USB_DC_SUSPEND)
        {
            LOG_WRN("usb deinit");
            zmk_usb_deinit();
        }
            
        return 0;
    case MOD_BLE:  
         LOG_WRN("ble exit");
         zmk_hid_keyboard_clear();
         zmk_endpoints_send_report(HID_USAGE_KEY);
         raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state=LED_PEER_STATE_POWR_OFF,
                .transport=ZMK_TRANSPORT_BLE});
         // hardware_select_transport=ZMK_TRANSPORT_USB;
         // zmk_endpoints_select_transport(hardware_select_transport);
         LOG_PANIC();
#if CONFIG_LED_STRIP            
        zmk_rgb_matrix_off();
        k_msleep(200);
#endif          
         app_system_reset(WDT_FLAG_RESET_SOC);
         return 0;
    case MOD_PPT:
         LOG_WRN("ppt exit");
         zmk_hid_keyboard_clear();
         zmk_endpoints_send_report(HID_USAGE_KEY);
         raise_zmk_led_state_changed((struct zmk_led_state_changed){
                .led_state=LED_PEER_STATE_POWR_OFF,
                .transport=ZMK_TRANSPORT_PPT});
         // hardware_select_transport=ZMK_TRANSPORT_USB;
         // zmk_endpoints_select_transport(hardware_select_transport);
         LOG_PANIC();
#if CONFIG_LED_STRIP            
        zmk_rgb_matrix_off();
        k_msleep(200);
#endif          
         app_system_reset(WDT_FLAG_RESET_SOC);
         return 0;
    case MOD_NKRO:
        zmk_toggle_nkro_up();
        return 0;
#if CONFIG_SHIELD_KEYCHRON_RS45_ANSI
    case MOD_WIRELESS:
        mode_wireless =0;
        LOG_DBG("clear mode wireless");
        if(usb_power_on)
        {
            // zmk_usb_deinit();
            hardware_select_transport =ZMK_TRANSPORT_USB;
            zmk_endpoints_select_transport(hardware_select_transport);
        }
        return 0;
#endif          
    }
    return -ENOTSUP;
}

static int behavior_keychron_init(const struct device *dev) {

    gpio_pin_configure_dt(&usb_det,GPIO_INPUT);
    usb_power_on = gpio_pin_get(usb_det.port,usb_det.pin);
    LOG_ERR("usb power on:%d",usb_power_on);
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR 
    gpio_pin_configure_dt(&led_winlock,GPIO_OUTPUT);
    gpio_pin_set_dt(&led_winlock ,0);
#endif 
     return 0; 
}

static const struct behavior_driver_api behavior_keychron_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

enum zmk_transport get_hardware_select_transport(void)
{
    return hardware_select_transport;
}
bool is_ble(void)
{
    return hardware_select_transport == ZMK_TRANSPORT_BLE;
}
BEHAVIOR_DT_INST_DEFINE(0, behavior_keychron_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_keychron_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */

#if CONFIG_SHIELD_KEYCHRON_RS45_ANSI 
void change_to_ble(uint8_t index)
{
    if(!mode_wireless) return;
    LOG_WRN("to ble,last trans:%d,cur:%d",last_trans,zmk_endpoints_selected().transport);
    if(zmk_endpoints_selected().transport !=ZMK_TRANSPORT_PPT
        && last_trans !=ZMK_TRANSPORT_PPT)
    {
        // hardware_select_transport=ZMK_TRANSPORT_BLE;
        save_mode(ZMK_TRANSPORT_BLE);
        last_trans =ZMK_TRANSPORT_BLE;
        LOG_WRN("select ble");
        zmk_endpoints_select_transport(hardware_select_transport);
    }
    else
    {
        // if(zmk_ble_active_profile_index()!=index)
        {
            LOG_ERR("save profile:%d",index);
            settings_save_one("ble/active_profile", &index, sizeof(index));
        }
            
        save_mode(ZMK_TRANSPORT_BLE);
        k_msleep(50);
        LOG_WRN("reboot for ble:%d,active:%d",index,zmk_ble_active_profile_index());
#if CONFIG_LED_STRIP            
        zmk_rgb_matrix_off();
#endif            
        LOG_PANIC();
        app_system_reset(WDT_FLAG_RESET_SOC);
        // ppt_stop_sync();
        // sync_deinit();
        // zmk_ble_restart();
        // k_msleep(100);
        // zmk_endpoints_select_transport(hardware_select_transport);
    }
}
void change_to_ppt(void)
{
    if(!mode_wireless) return;
    LOG_WRN("to ppt,last trans:%d,cur:%d",last_trans,zmk_endpoints_selected().transport);
    if(zmk_endpoints_selected().transport !=ZMK_TRANSPORT_BLE
        && last_trans !=ZMK_TRANSPORT_BLE)
    {
        // hardware_select_transport=ZMK_TRANSPORT_PPT;
        save_mode(ZMK_TRANSPORT_PPT );
        last_trans =ZMK_TRANSPORT_PPT;
        LOG_WRN("select ppt");
        zmk_endpoints_select_transport(hardware_select_transport);
    }
    else
    {
        save_mode(ZMK_TRANSPORT_PPT );
        k_msleep(50);
        LOG_WRN("reboot for ppt");
#if CONFIG_LED_STRIP            
        zmk_rgb_matrix_off();
#endif             
        LOG_PANIC();
        app_system_reset(WDT_FLAG_RESET_SOC);
    }    
}
static int mode_handle_set(const char *name, size_t len, settings_read_cb read_cb,
    void *cb_arg);
struct settings_handler mode_handler = {.name = "savemode", .h_set = mode_handle_set};

static int mode_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                                void *cb_arg) {
                              
    LOG_DBG("Setting launcher value %s", name);

    if (settings_name_steq(name, "mode", NULL)) {
        if (len != sizeof(stored_transport)) {
            LOG_ERR("Invalid mode size (got %d expected %d)", len, sizeof(stored_transport));
            return -EINVAL;
        }
        int err = read_cb(cb_arg, &stored_transport, sizeof(stored_transport));
        if (err <= 0) {
            LOG_ERR("Failed to read mode  from settings (err %d)", err);
            return err;
        }
        LOG_ERR("read mode:%d,size:%d",stored_transport,sizeof(stored_transport));
    }
    return 0;
}
void save_mode(enum zmk_transport mod)
{
    int rc;
    if(mod ==stored_transport ) return;
    stored_transport = mod;
    rc = settings_save_one("savemode/mode", (const void *)&stored_transport, sizeof(stored_transport));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
void read_mode(void)
{

    int rc = settings_subsys_init();
    if (rc) {
        printk("settings subsys initialization: fail (err %d)\n", rc);
       
    }
    rc = settings_register(&mode_handler);
    if (rc) {
        LOG_ERR("Failed to setup the profile settings handler (err %d)", rc);
        
    }
    settings_load_subtree("savemode");

    LOG_DBG("transport :%d",stored_transport);
}
void mode_init(void)
{

    int rc = settings_subsys_init();
    if (rc) {
        printk("settings subsys initialization: fail (err %d)\n", rc);
       
    }
    rc = settings_register(&mode_handler);
    if (rc) {
        LOG_ERR("Failed to setup the profile settings handler (err %d)", rc);
        
    }
    settings_load_subtree("savemode");

    LOG_DBG("transport :%d",hardware_select_transport);
    if(hardware_select_transport != ZMK_TRANSPORT_USB
        && hardware_select_transport !=0xff)
    {
        if(hardware_select_transport == ZMK_TRANSPORT_BLE)
            change_to_ble(0);
        else if(hardware_select_transport == ZMK_TRANSPORT_PPT)
        {
            // set_disp_mode(4);
            change_to_ppt();
        } 
    }
    else
    {
        zmk_endpoints_select_transport(ZMK_TRANSPORT_USB);
    }
}
#endif 
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR 
static uint8_t winlock_led_on;
uint8_t winlock_led_is_on(void)
{
    return winlock_led_on;
}
void winlock_led_onoff(uint8_t onoff)
{

    gpio_pin_set_dt(&led_winlock ,onoff);
    winlock_led_on =onoff;
    LOG_ERR("WINLOCK ONOFF:%d",onoff);

}
void set_backup_winlock(uint8_t mac)
{
    backup_winlock =fn_win_lock;
    if(mac)
        fn_win_lock = 0;
}

#ifdef CONFIG_ZMK_SSD1306
void winlock_disp_cb(struct k_work * work)
{
    disp_set_winlock(fn_win_lock);
}

K_WORK_DELAYABLE_DEFINE(winlock_disp,winlock_disp_cb);
#endif 
uint8_t get_mac_win_layer(void);
void winlock_led_set(void)
{
    static uint8_t last_layer =0;
    if(last_layer != get_mac_win_layer() ||get_mac_win_layer() )
    {
        last_layer = get_mac_win_layer();
        winlock_led_onoff(fn_win_lock);
    }

    // disp_set_winlock(fn_win_lock);
#ifdef CONFIG_ZMK_SSD1306
    k_work_reschedule(&winlock_disp,K_MSEC(10));
#endif
}
#endif 

uint8_t get_charge_state(void)
{
    return bat_charge_state;
}