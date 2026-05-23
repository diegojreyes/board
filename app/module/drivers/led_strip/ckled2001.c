/*
 * Copyright (c) 2022 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#if CONFIG_CKLED2001_I2C
#define DT_DRV_COMPAT zmk_ckled2001
#else
#define DT_DRV_COMPAT zmk_spi_ckled2001
#endif 

#include <zephyr/drivers/led_strip.h>

#define LOG_LEVEL CONFIG_LED_STRIP_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ckled2001);

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/pm/device.h>
#define REG_SET_CMD_PAGE 0xFD
#define LED_CONTROL_PAGE 0x00
#define LED_PWM_PAGE 0x01
#define FUNCTION_PAGE 0x03
#define CURRENT_TUNE_PAGE 0x04

#define REG_CONFIGRATION 0x00
#define MSKSW_SHUTDOWN_MODE 0x0
#define MSKSW_NORMAL_MODE 0x1

#define REG_PDU 0x13
#define MSKSET_CA_CB_CHANNEL 0xAA
#define MSKCLR_CA_CB_CHANNEL 0x00

#define REG_SCAN_PHASE 0x14
#define MSKPHASE_CHANNELS(cnt) (12 - cnt)

#define REG_SLEW_RATE_CONTROL_MODE1 0x15
#define MSKPWM_DELAY_PHASE_ENABLE 0x04
#define MSKPWM_DELAY_PHASE_DISABLE 0x00

#define REG_SLEW_RATE_CONTROL_MODE2 0x16
#define MSKDRIVING_SINKING_CHHANNEL_SLEWRATE_ENABLE 0xC0
#define MSKDRIVING_SINKING_CHHANNEL_SLEWRATE_DISABLE 0x00

#define REG_SOFTWARE_SLEEP 0x1A
#define MSKSLEEP_ENABLE 0x02
#define MSKSLEEP_DISABLE 0x00

#define COUNT_BETWEEN(a, b) ((b - a) + 1)

#define LED_CONTROL_CNT 24
#define LED_PWM_CNT 192
#define CURRENT_TUNE_CNT 12

struct ckled2001_channel_map {
    uint8_t ch_r;
    uint8_t ch_g;
    uint8_t ch_b;
};

struct ckled2001_config {
#if CONFIG_CKLED2001_I2C    
    struct i2c_dt_spec bus;
#else 
    struct spi_dt_spec bus;
#endif     
    struct gpio_dt_spec pwrctrl_gpio;
    uint8_t scan_phase_channels;
    struct ckled2001_channel_map *map;
    uint32_t map_cnt;
    uint8_t *pwm_buffer;
    uint8_t *p_current_tune;
    uint8_t current_tune_cnt;
    uint8_t id;
    const struct pinctrl_dev_config *pcfg;
};

static bool update_required;
uint8_t hardware_sleep[2]={0,0};  

#if !(CONFIG_CKLED2001_I2C)
static inline int spi_write_datas(const struct device *dev ,uint8_t * p_data,uint8_t len)
{
    const struct ckled2001_config *config = dev->config;
    struct spi_buf tx_buf[1] = {
        {.buf = p_data, .len = len},
    };
    struct spi_buf_set tx_set = { .buffers = tx_buf, .count = 1 };
    return spi_write_dt(&config->bus,&tx_set);
}
static inline int ckled2001_spi_write_reg(const struct device *dev,uint8_t page,uint8_t reg,uint8_t value)
{
    uint8_t buf[3]={0x20|page,reg,value};
    return spi_write_datas(dev,buf,3);
}
static inline int ckled2001_spi_read_reg(const struct device *dev,uint8_t page,uint8_t reg,uint8_t *value)
{
    const struct ckled2001_config *config = dev->config;
    uint8_t tx[2]={0xa0|page,reg};
    struct spi_buf tx_buf[1] = {
        {.buf = tx, .len = 2},
    };
    struct spi_buf_set tx_set = { .buffers = tx_buf, .count = 1 };
    struct spi_buf rx_buf[1] = {
        {.buf = value, .len = 1},
    };
    struct spi_buf_set rx_set = { .buffers = rx_buf, .count = 1 };
    return spi_transceive_dt(&config->bus,&tx_set,&rx_set);
}

static inline int ckled2001_spi_set_control(const struct device *dev,uint8_t value)
{
    const struct ckled2001_config *config = dev->config;
    uint8_t tx[LED_CONTROL_CNT+2]={0x20|LED_CONTROL_PAGE,0};
    struct spi_buf tx_buf[1] = {
        {.buf = tx,.len=LED_CONTROL_CNT+2},

    };
    memset(&tx[2],value,LED_CONTROL_CNT);
    struct spi_buf_set tx_set = { .buffers = tx_buf, .count = 1 };
    return spi_write_dt(&config->bus,&tx_set);
}
static inline int ckled2001_spi_set_tune(const struct device *dev )
{
    const struct ckled2001_config *config = dev->config;
    uint8_t tx[CURRENT_TUNE_CNT+2]={0x20|CURRENT_TUNE_PAGE,0};
    struct spi_buf tx_buf[1] = {
        {.buf = tx,.len=config->current_tune_cnt+2},

    };
    memcpy(&tx[2],config->p_current_tune,config->current_tune_cnt);
    struct spi_buf_set tx_set = { .buffers = tx_buf, .count = 1 };
    return spi_write_dt(&config->bus,&tx_set);
}
static inline int ckled2001_spi_flush_pwm_buffer(const struct device *dev) {
    const struct ckled2001_config *config = dev->config;
#if 0    
    uint8_t cmd[2]={0x20|LED_PWM_PAGE,0};
    struct spi_buf tx_buf[2] = {
        {.buf = cmd,.len=2},
        {.buf = config->pwm_buffer, .len = LED_PWM_CNT},
    };
    struct spi_buf_set tx_set = { .buffers = tx_buf, .count = 2 };
#else
    

    uint8_t cmd[2+LED_PWM_CNT]={0x20|LED_PWM_PAGE,0};
    struct spi_buf tx_buf[1] = {
        {.buf = cmd,.len=2+LED_PWM_CNT}        
    };
    memcpy(&cmd[2],config->pwm_buffer,LED_PWM_CNT);
    struct spi_buf_set tx_set = { .buffers = tx_buf, .count = 1 };    
#endif    
    return spi_write_dt(&config->bus,&tx_set);
    // const struct spi_dt_spec *spi_dt = &config->bus;
    // // gpio_pin_configure_dt(&spi_dt->config.cs.gpio)
    // gpio_pin_configure(spi_dt->config.cs.gpio.port,spi_dt->config.cs.gpio.pin,GPIO_OUTPUT|GPIO_ACTIVE_LOW);
    // gpio_pin_set_dt(&spi_dt->config.cs.gpio, 1);
    // k_msleep(2);
    // gpio_pin_set_dt(&spi_dt->config.cs.gpio, 0);
    // return 0;
    // // LOG_ERR("set cs pin:%d,port:%p,flag:%x",spi_dt->config.cs.gpio.pin,spi_dt->config.cs.gpio.port,spi_dt->config.cs.gpio.dt_flags);
}
#else

static inline int ckled2001_write_reg(const struct device *dev, uint8_t reg, uint8_t value) {
    const struct ckled2001_config *config = dev->config;
    return i2c_burst_write_dt(&config->bus, reg, &value, 1);
}

static inline int ckled2001_read_reg(const struct device *dev, uint8_t reg, uint8_t *value) {
    const struct ckled2001_config *config = dev->config;
    return i2c_reg_read_byte_dt(&config->bus, reg, value);
}

static inline int ckled2001_set_control(const struct device *dev, uint8_t value) {
    LOG_INF("set control:%x",value);
 
    ckled2001_write_reg(dev, REG_SET_CMD_PAGE, LED_CONTROL_PAGE);
    for (int i = 0; i < LED_CONTROL_CNT; i++) {
        ckled2001_write_reg(dev, i, value);
    }
   
    return 0;
}


static inline int ckled2001_flush_pwm_buffer(const struct device *dev) {
    const struct ckled2001_config *config = dev->config;
    ckled2001_write_reg(dev, REG_SET_CMD_PAGE, LED_PWM_PAGE);
    return i2c_burst_write_dt(&config->bus, 0, (const uint8_t *)config->pwm_buffer, LED_PWM_CNT);
}
#endif 

static int ckled2001_update_rgb(const struct device *dev, struct led_rgb *pixels,
                                size_t num_pixels) {
    const struct ckled2001_config *config = dev->config;

    if (num_pixels > config->map_cnt) {
        num_pixels = config->map_cnt;
    }

    //use num_pixels as index!!!
    if(config->pwm_buffer[config->map[num_pixels].ch_r] == pixels[0].r
        && config->pwm_buffer[config->map[num_pixels].ch_g] == pixels[0].g
        && config->pwm_buffer[config->map[num_pixels].ch_b] == pixels[0].b)
        return 0;
   
    config->pwm_buffer[config->map[num_pixels].ch_r] = pixels[0].r;
    config->pwm_buffer[config->map[num_pixels].ch_g] = pixels[0].g;
    config->pwm_buffer[config->map[num_pixels].ch_b] = pixels[0].b;

    update_required =true;

    // for (size_t i = 0; i < num_pixels; i++) {
    //     config->pwm_buffer[config->map[i].ch_r] = pixels[i].r;
    //     config->pwm_buffer[config->map[i].ch_g] = pixels[i].g;
    //     config->pwm_buffer[config->map[i].ch_b] = pixels[i].b;
    // }
    return 0;
    
}

static int ckled2001_update_channels(const struct device *dev, uint8_t *channels,
                                     size_t num_channels) {
     
    const struct ckled2001_config *config = dev->config;   

    const struct spi_dt_spec *spi_dt = &config->bus;
    gpio_pin_configure(spi_dt->config.cs.gpio.port,spi_dt->config.cs.gpio.pin,GPIO_OUTPUT|GPIO_ACTIVE_LOW);

    if((num_channels == config->id) || (num_channels ==0xff)) //sleep when id=0xf0/0xf1/0xff
    {

        if(!hardware_sleep[config->id &0x0f])
        {
            hardware_sleep[config->id &0x0f]=1;
            ckled2001_spi_write_reg(dev, FUNCTION_PAGE,REG_CONFIGRATION, MSKSW_SHUTDOWN_MODE);
            LOG_ERR("led hardware sleep,id:%x",config->id);
            if(hardware_sleep[0]&& hardware_sleep[1]) //two ic use same sdb control
            {
                gpio_pin_configure_dt(&config->pwrctrl_gpio, GPIO_OUTPUT_LOW);
                LOG_ERR("led ctrl off!!!");
            }
        }
        // else
        // {
        //     LOG_ERR("skip led hardware sleep,id:%x",config->id);
        // }   
        
        return 0;
    }   
                                  
    if(num_channels>0 && update_required)
    {
        int rc=0;
        #if CONFIG_CKLED2001_I2C
            rc= ckled2001_flush_pwm_buffer(dev);
        #else

            if(hardware_sleep[config->id &0x0f])
            {
                hardware_sleep[config->id &0x0f] =0;                
                LOG_ERR("led wakeup,id:%x",config->id);
                gpio_pin_configure_dt(&config->pwrctrl_gpio, GPIO_OUTPUT_HIGH);
                k_busy_wait(128);
                ckled2001_spi_write_reg(dev, FUNCTION_PAGE,REG_CONFIGRATION, MSKSW_NORMAL_MODE);
                // k_usleep(100);
            }
            rc= ckled2001_spi_flush_pwm_buffer(dev);
        #endif 
            // LOG_INF("update pwm buffer:%d",rc);
        // update_required =false; 
        return rc;
    }
    return 0;
}
static struct k_spinlock lock;
static int ckled2001_init(const struct device *dev) {
    const struct ckled2001_config *config = dev->config;

    LOG_INF("Loaded %d channel mappings", config->map_cnt);

    if (!device_is_ready(config->bus.bus)) {
        LOG_ERR("%s bus not ready", config->bus.bus->name);
        return -ENODEV;
    }
    pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
#if CONFIG_CKLED2001_I2C    
    ckled2001_write_reg(dev, REG_SET_CMD_PAGE, FUNCTION_PAGE);
    uint8_t chip_id = 0;
    ckled2001_read_reg(dev, 0x11, &chip_id);
    LOG_INF("ckled2001 found:%d", (chip_id == 0x8a));
    if (chip_id == 0x8a) {
#if 1
        // Set functions
        ckled2001_write_reg(dev, REG_SET_CMD_PAGE, FUNCTION_PAGE);
        ckled2001_write_reg(dev, REG_CONFIGRATION, MSKSW_SHUTDOWN_MODE);
        ckled2001_write_reg(dev, REG_PDU, MSKSET_CA_CB_CHANNEL);
        ckled2001_write_reg(dev, REG_SCAN_PHASE, MSKPHASE_CHANNELS(config->scan_phase_channels));
        ckled2001_write_reg(dev, REG_SLEW_RATE_CONTROL_MODE1, MSKPWM_DELAY_PHASE_ENABLE);
        ckled2001_write_reg(dev, REG_SLEW_RATE_CONTROL_MODE2,
                            MSKDRIVING_SINKING_CHHANNEL_SLEWRATE_ENABLE);
        ckled2001_write_reg(dev, REG_SOFTWARE_SLEEP, MSKSLEEP_DISABLE);

        // Turn off all LEDs
        ckled2001_set_control(dev, 0x00);

        // Init PWM page
        memset(config->pwm_buffer, 0x00, LED_PWM_CNT);
        ckled2001_flush_pwm_buffer(dev);

        // Init current page
        ckled2001_write_reg(dev, REG_SET_CMD_PAGE, CURRENT_TUNE_PAGE);
        
        for (int i = 0; i < config->current_tune_cnt; i++)
            ckled2001_write_reg(dev, i, config->p_current_tune[i]);


        // Turn on all LEDs
        ckled2001_set_control(dev, 0xFF);

        // Set to normal mode
        ckled2001_write_reg(dev, REG_SET_CMD_PAGE, FUNCTION_PAGE);
        ckled2001_write_reg(dev, REG_CONFIGRATION, MSKSW_NORMAL_MODE);
        LOG_INF("ckled2001 inited!");
#endif
    }
#else 
    // Pad_Config(P2_4, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_HIGH);
    // Pad_Config(P2_6, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_HIGH);
    // Pad_Config(P4_3, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_HIGH);
    // Pinmux_Config(P2_4,DWGPIO);
    // Pinmux_Config(P2_6,DWGPIO);
    // Pinmux_Config(P4_3,DWGPIO);

    LOG_WRN("ckled port:%s,pin:%d",config->pwrctrl_gpio.port->name,config->pwrctrl_gpio.pin);
    if (!device_is_ready(config->pwrctrl_gpio.port)) {
        LOG_ERR("GPIO is not ready: %s", config->pwrctrl_gpio.port->name);
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&config->pwrctrl_gpio, GPIO_OUTPUT_HIGH);
    if(err)
    {
        LOG_ERR("can't config pwrctrl gpio");
    }
    // gpio_pin_set_dt(&config->pwrctrl_gpio,1);
    LOG_WRN("cs port:%s, pin:%d",config->bus.config.cs.gpio.port->name,config->bus.config.cs.gpio.pin);
    // uint8_t chip_id = 0;
    // ckled2001_spi_read_reg(dev,FUNCTION_PAGE, 0x11, &chip_id);
    // LOG_INF("ckled2001 found:%d", (chip_id == 0x8a));
    // if (chip_id == 0x8a) 
    {
        k_spinlock_key_t key =k_spin_lock(&lock);
        ckled2001_spi_write_reg(dev,FUNCTION_PAGE, REG_CONFIGRATION, MSKSW_SHUTDOWN_MODE);
        ckled2001_spi_write_reg(dev,FUNCTION_PAGE, REG_PDU, MSKSET_CA_CB_CHANNEL);
        ckled2001_spi_write_reg(dev,FUNCTION_PAGE, REG_SCAN_PHASE, MSKPHASE_CHANNELS(config->scan_phase_channels));
        ckled2001_spi_write_reg(dev,FUNCTION_PAGE, REG_SLEW_RATE_CONTROL_MODE1, MSKPWM_DELAY_PHASE_ENABLE);
        ckled2001_spi_write_reg(dev,FUNCTION_PAGE,REG_SLEW_RATE_CONTROL_MODE2,
                            MSKDRIVING_SINKING_CHHANNEL_SLEWRATE_ENABLE);
        ckled2001_spi_write_reg(dev,FUNCTION_PAGE, REG_SOFTWARE_SLEEP, MSKSLEEP_DISABLE);

        // Turn off all LEDs
        ckled2001_spi_set_control(dev, 0x00);

        // Init PWM page
        memset(config->pwm_buffer, 0x00, LED_PWM_CNT);
        ckled2001_spi_flush_pwm_buffer(dev);

        // Init current page     
        ckled2001_spi_set_tune(dev);

        // Turn on all LEDs
        ckled2001_spi_set_control(dev, 0xFF);

        // Set to normal mode
        ckled2001_spi_write_reg(dev, FUNCTION_PAGE,REG_CONFIGRATION, MSKSW_NORMAL_MODE);
        k_spin_unlock(&lock,key);
        LOG_INF("ckled2001 spi inited!");    
    }
#endif     
    return 0;
}

static const struct led_strip_driver_api ckled2001_api = {
    .update_rgb = ckled2001_update_rgb,
    .update_channels = ckled2001_update_channels,
};
#if IS_ENABLED(CONFIG_PM_DEVICE)

static int ckled2001_pm_action(const struct device *dev, enum pm_device_action action) {
    const struct ckled2001_config *config = dev->config;
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:        
    {
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
#if CONFIG_CKLED2001_I2C
#define CKLED2001_INIT(n)                                                                          \
    static uint8_t ckled2001_channel_map##n[] = DT_INST_PROP(n, map);                              \
    static uint8_t ckled2001_current_tune##n[] = DT_INST_PROP(n, current_tune);                                                                                               \
    static uint8_t ckled2001_pwm_buffer_##n[LED_PWM_CNT];                                          \
                                                                                                   \
    static const struct ckled2001_config ckled2001_config_##n = {                                  \
        .bus = I2C_DT_SPEC_INST_GET(n),                                                            \
        .scan_phase_channels = DT_INST_PROP_OR(n, scan_phase_channels, 12),                        \
        .map = (struct ckled2001_channel_map *)ckled2001_channel_map##n,                           \
        .map_cnt = DT_INST_PROP_LEN(n, map) / 3,                                                   \
        .pwm_buffer = ckled2001_pwm_buffer_##n,                                                    \
        .p_current_tune = ckled2001_current_tune##n,                                               \
        .current_tune_cnt = DT_INST_PROP_LEN(n, current_tune),                                     \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, &ckled2001_init, NULL, NULL, &ckled2001_config_##n, POST_KERNEL,      \
                          CONFIG_CKLED2001_INIT_PRIORITY, &ckled2001_api);

DT_INST_FOREACH_STATUS_OKAY(CKLED2001_INIT);
#else
#define CKLED2001_INIT(n)                                                                          \
    static uint8_t ckled2001_channel_map##n[] = DT_INST_PROP(n, map);                              \
    static uint8_t ckled2001_current_tune##n[] = DT_INST_PROP(n, current_tune);                                                                                               \
    static uint8_t ckled2001_pwm_buffer_##n[LED_PWM_CNT];                                          \
    PINCTRL_DT_INST_DEFINE(n);                                                                     \
    static const struct ckled2001_config ckled2001_config_##n = {                                  \
        .bus=SPI_DT_SPEC_INST_GET(n, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0),  \
        .scan_phase_channels = DT_INST_PROP_OR(n, scan_phase_channels, 12),                        \
        .map = (struct ckled2001_channel_map *)ckled2001_channel_map##n,                           \
        .map_cnt = DT_INST_PROP_LEN(n, map) / 3,                                                   \
        .pwm_buffer = ckled2001_pwm_buffer_##n,                                                    \
        .p_current_tune = ckled2001_current_tune##n,                                               \
        .current_tune_cnt = DT_INST_PROP_LEN(n, current_tune),                                     \
        .pwrctrl_gpio =GPIO_DT_SPEC_GET_BY_IDX(DT_DRV_INST(n), pwrctrl_gpios, 0),                  \
        .id = (n+0xf0),                                                                            \
        .pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                                 \
    };                                                                                             \
    PM_DEVICE_DT_INST_DEFINE(n, ckled2001_pm_action);                                           \
    DEVICE_DT_INST_DEFINE(n, &ckled2001_init, PM_DEVICE_DT_INST_GET(n), NULL, &ckled2001_config_##n, POST_KERNEL,      \
                          CONFIG_CKLED2001_INIT_PRIORITY, &ckled2001_api);

DT_INST_FOREACH_STATUS_OKAY(CKLED2001_INIT);
#endif 