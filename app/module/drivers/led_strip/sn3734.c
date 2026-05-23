/*
 * Copyright (c) 2022 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_sn3734

#include <zephyr/drivers/led_strip.h>

#define LOG_LEVEL CONFIG_LED_STRIP_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sn3734);

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/pm/device.h>

// Registers Pages
#define SN3734_PWM_PAGE 0x00
#define SN3734_FUNC_PAGE 0x01

// Registers
#define SN3734_PWM_PAGE_PWM_START 0x01  // 0x01 ~ 0xFC
#define SN3734_FUNC_PAGE_PWM_START 0x01 // 0x01 ~ 0x48
#define SN3734_SCALING_START 0x49
#define SN3734_FUNC_REG_CONFIGURATION 0x60
#define SN3734_FUNC_REG_GLOBAL_CURRENT 0x61
#define SN3734_FUNC_REG_PULLDOWNUP 0x62
#define SN3734_FUNC_REG_SRS 0x63
#define SN3734_FUNC_REG_CS_PULLUP 0x91
#define SN3734_FUNC_REG_RESET 0xDF
#define SN3734_FUNC_REG_PWM_UPDATE 0xFD

#define SN3734_PWM_PAGE_REGISTER_COUNT 252
#define SN3734_FUNC_PAGE_REGISTER_COUNT 72
#define SN3734_PWM_REGISTER_COUNT (SN3734_PWM_PAGE_REGISTER_COUNT + SN3734_FUNC_PAGE_REGISTER_COUNT)
#define SCALING_REG_LEN 18

// Configuration Register
#define SN3734_SHUT_DOWN_MODE (0x0 << 0)
#define SN3734_NORMAL_MODE (0x1 << 0)

#define SN3734_PWM_MODE_6_2 (0b00 << 1)
#define SN3734_PWM_MODE_8 (0b01 << 1)
#define SN3734_PWM_MODE_8_4 (0b10 << 1)
#define SN3734_PWM_MODE_12 (0b11 << 1)

#define SN3734_SWS_ALL (0b0000 << 4)
#define SN3734_SWS_11 (0b0001 << 4)
#define SN3734_SWS_10 (0b0010 << 4)
#define SN3734_SWS_9 (0b0011 << 4)
#define SN3734_SWS_8 (0b0100 << 4)
#define SN3734_SWS_7 (0b0101 << 4)
#define SN3734_SWS_6 (0b0110 << 4)
#define SN3734_SWS_5 (0b0111 << 4)
#define SN3734_SWS_4 (0b1000 << 4)
#define SN3734_SWS_3 (0b1001 << 4)
#define SN3734_SWS_2 (0b1010 << 4)
#define SN3734_SWS_ALL_2SW (0b1011 << 4)
#define SN3734_SWS_10_2SW (0b1100 << 4)
#define SN3734_SWS_ALL_3SW (0b1101 << 4)
#define SN3734_SWS_ALL_4SW (0b1110 << 4)
#define SN3734_SWS_ALL_6SW (0b1111 << 4)

// Pull Down/Up Resistor Selection Register
#define SN3734_SWPDR_FLOATING 0b000 // Floating
#define SN3734_SWPDR_3V2 0b001      // 3.2V
#define SN3734_SWPDR_2V8 0b010      // 2.8V
#define SN3734_SWPDR_2V4 0b011      // 2.4V
#define SN3734_SWPDR_2V0 0b100      // 2.0V
#define SN3734_SWPDR_1V6 0b101      // 1.6V
#define SN3734_SWPDR_1V2 0b110      // 1.2V
#define SN3734_SWPDR_GND 0b111      // GND

#define SN3734_CSPUR_FLOATING (0b000 << 4)       // Floating
#define SN3734_CSPUR_PVCC_MINUS_3V2 (0b001 << 4) // PVCC-3.2V
#define SN3734_CSPUR_PVCC_MINUS_2V8 (0b010 << 4) // PVCC-2.8V
#define SN3734_CSPUR_PVCC_MINUS_2V4 (0b011 << 4) // PVCC-2.4V
#define SN3734_CSPUR_PVCC_MINUS_2V0 (0b100 << 4) // PVCC-2.0V
#define SN3734_CSPUR_PVCC_MINUS_1V6 (0b101 << 4) // PVCC-1.6V
#define SN3734_CSPUR_PVCC_MINUS_1V2 (0b110 << 4) // PVCC-1.2V
#define SN3734_CSPUR_PVCC (0b111 << 4)           // PVCC

#define SN3734_PHASE_180_DEGREE (0x01 << 7)

// SRS Enable Register

// Scan rate frequency of 8 bit
#define SN3734_PWM_FREQUENCY_6K_HZ 0b000
#define SN3734_PWM_FREQUENCY_8k_HZ 0b001
#define SN3734_PWM_FREQUENCY_1K5_HZ 0b010
#define SN3734_PWM_FREQUENCY_750_HZ 0b011
#define SN3734_PWM_FREQUENCY_375_HZ 0b100
#define SN3734_PWM_FREQUENCY_188HZ 0b101

#ifndef SN3734_SWS
#define SN3734_SWS SN3734_SWS_ALL
#endif

#ifndef SN2734_SCALING
#define SN2734_SCALING                                                                             \
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,                                         \
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#endif

#ifndef SN3734_SRS
#define SN3734_SRS SN3734_PWM_FREQUENCY_6K_HZ
#endif

#ifndef SN3734_SWPDR
#define SN3734_SWPDR SN3734_SWPDR_2V0
#endif

#ifndef SN3734_CSPUR
#define SN3734_CSPUR SN3734_CSPUR_PVCC_MINUS_2V0
#endif

#ifndef SN3734_GLOBAL_CURRENT
#define SN3734_GLOBAL_CURRENT 0xFF
#endif

#define SN3734_WRITE (0 << 7)
#define SN3734_READ (1 << 7)
#define SN3734_ID (0b110 << 4)

#define LED_PWM_CNT SN3734_PWM_REGISTER_COUNT

struct sn3734_channel_map {
    uint16_t ch_r;
    uint16_t ch_g;
    uint16_t ch_b;
};

struct sn3734_config {

    struct spi_dt_spec bus;

    struct gpio_dt_spec pwrctrl_gpio;
    uint8_t scan_phase_channels;
    struct sn3734_channel_map *map;
    uint32_t map_cnt;
    uint8_t *pwm_buffer;
    uint8_t *p_current_tune;
    uint8_t current_tune_cnt;
    uint8_t id;
    const struct pinctrl_dev_config *pcfg;
};

static bool update_required;
uint8_t hardware_sleep[2] = {0, 0};

static inline int spi_write_datas(const struct device *dev, uint8_t *p_data, uint8_t len) {
    const struct sn3734_config *config = dev->config;
    struct spi_buf tx_buf[1] = {
        {.buf = p_data, .len = len},
    };
    struct spi_buf_set tx_set = {.buffers = tx_buf, .count = 1};
    return spi_write_dt(&config->bus, &tx_set);
}
int sn3734_write(const struct device *dev, uint8_t page, uint8_t reg, uint8_t *p_data,
                 uint8_t len) {
    const struct sn3734_config *config = dev->config;
    uint8_t header[2] = {SN3734_WRITE | SN3734_ID | (page & 0x0F), reg};
    struct spi_buf tx_buf[2] = {
        {.buf = header, .len = 2},
        {.buf = p_data, .len = len},
    };
    struct spi_buf_set tx_set = {.buffers = tx_buf, .count = 2};
    return spi_write_dt(&config->bus, &tx_set);
}
static inline int sn3734_write_register(const struct device *dev, uint8_t page, uint8_t reg,
                                        uint8_t value) {
    uint8_t buf[3] = {SN3734_WRITE | SN3734_ID | (page & 0x0F), reg, value};
    return spi_write_datas(dev, buf, 3);
}
static inline int sn3734_read_register(const struct device *dev, uint8_t page, uint8_t reg,
                                       uint8_t *value) {
    const struct sn3734_config *config = dev->config;
    uint8_t tx[2] = {SN3734_READ | SN3734_ID | (page & 0x0F), reg};
    struct spi_buf tx_buf[1] = {
        {.buf = tx, .len = 2},
    };
    struct spi_buf_set tx_set = {.buffers = tx_buf, .count = 1};
    struct spi_buf rx_buf[1] = {
        {.buf = value, .len = 1},
    };
    struct spi_buf_set rx_set = {.buffers = rx_buf, .count = 1};
    return spi_transceive_dt(&config->bus, &tx_set, &rx_set);
}

static inline int sn3734_spi_flush_pwm_buffer(const struct device *dev) {
    const struct sn3734_config *config = dev->config;
    uint8_t pwm[LED_PWM_CNT];
    memcpy(pwm, config->pwm_buffer, LED_PWM_CNT);
    int ret = sn3734_write(dev, SN3734_PWM_PAGE, SN3734_PWM_PAGE_PWM_START, pwm,
                           SN3734_PWM_PAGE_REGISTER_COUNT);
    ret = sn3734_write(dev, SN3734_FUNC_PAGE, SN3734_FUNC_PAGE_PWM_START,
                       pwm + SN3734_PWM_PAGE_REGISTER_COUNT, SN3734_FUNC_PAGE_REGISTER_COUNT);

    sn3734_write_register(dev, SN3734_FUNC_PAGE, SN3734_FUNC_REG_PWM_UPDATE, 0);
    return ret;
}

static int sn3734_update_rgb(const struct device *dev, struct led_rgb *pixels, size_t num_pixels) {
    const struct sn3734_config *config = dev->config;

    if (num_pixels > config->map_cnt) {
        num_pixels = config->map_cnt;
    }

    // use num_pixels as index!!!
    if (config->pwm_buffer[config->map[num_pixels].ch_r] == pixels[0].r &&
        config->pwm_buffer[config->map[num_pixels].ch_g] == pixels[0].g &&
        config->pwm_buffer[config->map[num_pixels].ch_b] == pixels[0].b)
        return 0;

    config->pwm_buffer[config->map[num_pixels].ch_r] = pixels[0].r;
    config->pwm_buffer[config->map[num_pixels].ch_g] = pixels[0].g;
    config->pwm_buffer[config->map[num_pixels].ch_b] = pixels[0].b;

    update_required = true;

    return 0;
}

static int sn3734_update_channels(const struct device *dev, uint8_t *channels,
                                  size_t num_channels) {

    const struct sn3734_config *config = dev->config;

    const struct spi_dt_spec *spi_dt = &config->bus;
    gpio_pin_configure(spi_dt->config.cs.gpio.port, spi_dt->config.cs.gpio.pin,
                       GPIO_OUTPUT | GPIO_ACTIVE_LOW);

    if ((num_channels == config->id) || (num_channels == 0xff)) {

        if (!hardware_sleep[config->id & 0x0f]) {
            hardware_sleep[config->id & 0x0f] = 1;
            LOG_ERR("led hardware sleep,id:%x", config->id);
            if (hardware_sleep[0] && hardware_sleep[1]) {
                gpio_pin_configure_dt(&config->pwrctrl_gpio, GPIO_OUTPUT_LOW);
                LOG_ERR("led ctrl off!!!");
            }
        }

        return 0;
    }

    if (num_channels > 0 && update_required) {
        int rc = 0;

        if (hardware_sleep[config->id & 0x0f]) {
            hardware_sleep[config->id & 0x0f] = 0;
            LOG_ERR("led wakeup,id:%x", config->id);
            gpio_pin_configure_dt(&config->pwrctrl_gpio, GPIO_OUTPUT_HIGH);
            k_busy_wait(128);
        }
        rc = sn3734_spi_flush_pwm_buffer(dev);
        update_required = false;
        return rc;
    }
    return 0;
}
static struct k_spinlock lock;
static int sn3734_init(const struct device *dev) {
    const struct sn3734_config *config = dev->config;

    LOG_INF("Loaded %d channel mappings", config->map_cnt);

    if (!device_is_ready(config->bus.bus)) {
        LOG_ERR("%s bus not ready", config->bus.bus->name);
        return -ENODEV;
    }
    pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);

    LOG_WRN("ckled port:%s,pin:%d", config->pwrctrl_gpio.port->name, config->pwrctrl_gpio.pin);
    if (!device_is_ready(config->pwrctrl_gpio.port)) {
        LOG_ERR("GPIO is not ready: %s", config->pwrctrl_gpio.port->name);
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&config->pwrctrl_gpio, GPIO_OUTPUT_HIGH);
    if (err) {
        LOG_ERR("can't config pwrctrl gpio");
    }

    LOG_WRN("cs port:%s, pin:%d", config->bus.config.cs.gpio.port->name,
            config->bus.config.cs.gpio.pin);

    {
        k_spinlock_key_t key = k_spin_lock(&lock);
        // Reset all registers
        sn3734_write_register(dev, SN3734_FUNC_PAGE, SN3734_FUNC_REG_RESET, 0xAE);
        // Set to normal mode
        sn3734_write_register(dev, SN3734_FUNC_PAGE, SN3734_FUNC_REG_CONFIGURATION,
                              SN3734_NORMAL_MODE | SN3734_PWM_MODE_8 | SN3734_SWS);
        // Set Golbal Current Control Register
        sn3734_write_register(dev, SN3734_FUNC_PAGE, SN3734_FUNC_REG_GLOBAL_CURRENT,
                              SN3734_GLOBAL_CURRENT);
        // Set Pull up & Down for SWx CSy
        sn3734_write_register(dev, SN3734_FUNC_PAGE, SN3734_FUNC_REG_PULLDOWNUP,
                              SN3734_SWPDR | SN3734_CSPUR);
        // Set Scan rate frequencY
        sn3734_write_register(dev, SN3734_FUNC_PAGE, SN3734_FUNC_REG_SRS, (SN3734_SRS & 0b111));

        // Set Scaling register
        // uint8_t scaling_reg[SCALING_REG_LEN] = SN2734_SCALING;
        uint8_t current_tune[SCALING_REG_LEN] = {0};
        memcpy(current_tune, config->p_current_tune, config->current_tune_cnt);
        sn3734_write(dev, SN3734_FUNC_PAGE, SN3734_SCALING_START, current_tune, SCALING_REG_LEN);

        k_spin_unlock(&lock, key);
        LOG_INF("sn3734 spi inited!");
    }

    return 0;
}

static const struct led_strip_driver_api sn3734_api = {
    .update_rgb = sn3734_update_rgb,
    .update_channels = sn3734_update_channels,
};
#if IS_ENABLED(CONFIG_PM_DEVICE)

static int sn3734_pm_action(const struct device *dev, enum pm_device_action action) {
    const struct sn3734_config *config = dev->config;
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND: {
        pinctrl_apply_state(config->pcfg, PINCTRL_STATE_SLEEP);
        return 0;
    }

    case PM_DEVICE_ACTION_RESUME:

        pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
        return 0;
    default:
        return -ENOTSUP;
    }
}

#endif // IS_ENABLED(CONFIG_PM_DEVICE)

#define SN3734_INIT(n)                                                                             \
    static uint16_t sn3734_channel_map##n[] = DT_INST_PROP(n, map);                                \
    static uint8_t sn3734_current_tune##n[] = DT_INST_PROP(n, current_tune);                       \
    static uint8_t sn3734_pwm_buffer_##n[LED_PWM_CNT];                                             \
    PINCTRL_DT_INST_DEFINE(n);                                                                     \
    static const struct sn3734_config sn3734_config_##n = {                                        \
        .bus =                                                                                     \
            SPI_DT_SPEC_INST_GET(n, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0),   \
        .map = (struct sn3734_channel_map *)sn3734_channel_map##n,                                 \
        .map_cnt = DT_INST_PROP(0, chain_length),                                                  \
        .pwm_buffer = sn3734_pwm_buffer_##n,                                                       \
        .p_current_tune = sn3734_current_tune##n,                                                  \
        .current_tune_cnt = DT_INST_PROP_LEN(n, current_tune),                                     \
        .pwrctrl_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_DRV_INST(n), pwrctrl_gpios, 0),                 \
        .id = (n + 0xf0),                                                                          \
        .pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                                 \
    };                                                                                             \
    PM_DEVICE_DT_INST_DEFINE(n, sn3734_pm_action);                                                 \
    DEVICE_DT_INST_DEFINE(n, &sn3734_init, PM_DEVICE_DT_INST_GET(n), NULL, &sn3734_config_##n,     \
                          POST_KERNEL, CONFIG_SN3734_INIT_PRIORITY, &sn3734_api);

DT_INST_FOREACH_STATUS_OKAY(SN3734_INIT);
