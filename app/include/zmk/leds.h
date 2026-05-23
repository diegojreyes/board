#ifndef __LEDS_H__
#define __LEDS_H__
// int leds_init(void);
typedef union {
    uint8_t raw;
    struct {
        bool num_lock : 1;
        bool caps_lock : 1;
        bool scroll_lock : 1;
        bool compose : 1;
        bool kana : 1;
        uint8_t reserved : 3;
    };
} hid_led_t;

void leds_start(void);
void leds_stop(void);
void blue_led_set_state(uint8_t led_state, uint8_t index);
void keyboad_led_set_onoff(uint8_t led_state);
void led_24G_set_state(uint8_t led_state);
void led_charge_set_state(uint8_t led_state);
void led_recover(uint8_t stop_rgb);
void led_bat_display(void);
void led_bat_display_off(void);
uint8_t get_charge_led_state(void);

hid_led_t keyboard_get_led_state(void);
void led_rgb_set_color(uint8_t r, uint8_t g, uint8_t b);
// void aon_write_reg(uint8_t value);
// uint32_t aon_read_reg(void);
bool aon_get_state(uint8_t state);
void aon_clear_state(uint8_t state);
void aon_write_state(uint8_t state);

void gpio_led_bat_low(void);
void gpio_led_24G_set_state(uint8_t led_state);
void gpio_led_blue_set_state(uint8_t led_state, uint8_t index);
void gpio_led_charge_set_state(uint8_t led_state);
void gpio_led_recover(void);
void gpio_led_power_on(void);
void gpio_led_bat_display(void);
void gpio_led_bat_display_off(void);
uint8_t gpio_led_is_power_on(void);
uint8_t zmk_usb_power_on(void);

enum {
    LED_PEER_STATE_DISCONNECTED,
    LED_PEER_STATE_CONNECTED,
    LED_PEER_STATE_PAIR,
    LED_PEER_STATE_RECONN,
    LED_PEER_STATE_FAILE,
    LED_PEER_STATE_POWER_ON,
    LED_PEER_STATE_ON_100MS,
    LED_PEER_STATE_BAT_LOW,
    LED_PEER_STATE_BAT_CHARGING,
    LED_PEER_STATE_BAT_CHARGEDONE,
    LED_PEER_STATE_RECOVER,
    LED_PEER_STATE_RECOVER1,
    LED_PEER_STATE_POWR_OFF,
};
enum {
    LED_BAT_NONE,
    LED_BAT_CHARGING,
    LED_BAT_CHARGE_DONE,
    LED_BAT_LOW,
    LED_BAT_SHUT_DOWN,
};

enum {
    AON_STATE_NONE,
    AON_STAET_RGB_TEST = 1,
    AON_STATE_RECOVER = 2,
    AON_STATE_SLEEP = 4,
};

#endif