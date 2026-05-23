#ifndef __LCD128_32_DISP_H__
#define __LCD128_32_DISP_H__
enum {
    DISP_WECLOME,
    DISP_KB_STATUS,
    DISP_LIGHTNESS,
    DISP_LIGHT_SPEED,
    DISP_LIGHT_MODE,
    DISP_RECOVER,
    DISP_NONE = 0xff
};
enum {
    TYPE_LIGHTNESS,
    TYPE_LIGHTSPEED,
    TYPE_LIGHTMODE,
};
typedef struct {
    uint8_t start : 1;
    uint8_t running : 1;
    uint8_t batlow : 1;
} _charge_state;
typedef union {
    uint8_t status;
    struct {
        uint8_t caps_lock : 1;
        uint8_t num_lock : 1;
        uint8_t win_lock : 1;
        uint8_t res : 1;
        uint8_t mode : 4;
    };
} _kb_status;
int disp_init(void);
void disp_welcom(void);
void disp_test(void);
void disp_kb_status(void);
void disp_sleep(void);
void disp_exit_sleep(void);
void disp_reset(void);
void disp_set_led_state(uint8_t state);
void disp_set_mode(uint8_t mode);
void disp_change_setting(uint8_t type, uint8_t value);
void disp_update_bat(uint8_t level);
void disp_bat_charging(void);
void disp_bat_charging_stop(void);
void disp_low_bat(bool en);
void disp_set_winlock(uint8_t onoff);
#endif