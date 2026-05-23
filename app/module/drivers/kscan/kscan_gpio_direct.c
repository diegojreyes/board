/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "kscan_gpio.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/pm/device.h>
#include <zmk/debounce.h>
#include <zephyr/drivers/pinctrl.h>
#include "trace.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#undef DBG_DIRECT
#define DBG_DIRECT LOG_ERR

void swd_pin_disable(void);

#define DT_DRV_COMPAT zmk_kscan_gpio_direct

#if CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS >= 0
#define INST_DEBOUNCE_PRESS_MS(n) CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS
#else
#define INST_DEBOUNCE_PRESS_MS(n)                                                                  \
    DT_INST_PROP_OR(n, debounce_period, DT_INST_PROP(n, debounce_press_ms))
#endif

#if CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS >= 0
#define INST_DEBOUNCE_RELEASE_MS(n) CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS
#else
#define INST_DEBOUNCE_RELEASE_MS(n)                                                                \
    DT_INST_PROP_OR(n, debounce_period, DT_INST_PROP(n, debounce_release_ms))
#endif

#define USE_POLLING IS_ENABLED(CONFIG_ZMK_KSCAN_DIRECT_POLLING)
#define USE_INTERRUPTS (!USE_POLLING)

#define COND_INTERRUPTS(code) COND_CODE_1(CONFIG_ZMK_KSCAN_DIRECT_POLLING, (), code)
#define COND_POLL_OR_INTERRUPTS(pollcode, intcode)                                                 \
    COND_CODE_1(CONFIG_ZMK_KSCAN_DIRECT_POLLING, pollcode, intcode)

#define INST_INPUTS_LEN(n) DT_INST_PROP_LEN(n, input_gpios)
#define KSCAN_DIRECT_INPUT_CFG_INIT(idx, inst_idx)                                                 \
    KSCAN_GPIO_GET_BY_IDX(DT_DRV_INST(inst_idx), input_gpios, idx)

struct kscan_direct_irq_callback {
    const struct device *dev;
    struct gpio_callback callback;
};

struct kscan_direct_data {
    const struct device *dev;
    struct kscan_gpio_list inputs;
    kscan_callback_t callback;
    struct k_work_delayable work;
#if USE_INTERRUPTS
    /** Array of length config->inputs.len */
    struct kscan_direct_irq_callback *irqs;
#endif
    /** Timestamp of the current or scheduled scan. */
    int64_t scan_time;
    /** Current state of the inputs as an array of length config->inputs.len */
    struct zmk_debounce_state *pin_state;
    uint32_t press_rows;
};

struct kscan_direct_config {
    struct zmk_debounce_config debounce_config;
    int32_t debounce_scan_period_ms;
    int32_t poll_period_ms;
    bool toggle_mode;
    const struct pinctrl_dev_config *pcfg;
};
static uint8_t long_poll;
static uint8_t suspend;
bool kscan_enabled_dlps;

#if USE_INTERRUPTS
static int kscan_direct_interrupt_configure(const struct device *dev, const gpio_flags_t flags) {
    const struct kscan_direct_data *data = dev->data;

    for (int i = 0; i < data->inputs.len; i++) {
        const struct gpio_dt_spec *gpio = &data->inputs.gpios[i].spec;
         int err = gpio_pin_interrupt_configure_dt(gpio, flags);
        if (err) {
            LOG_ERR("Unable to configure interrupt for pin %u on %s,err:%d", gpio->pin, gpio->port->name,err);
            return err;
        }
#if 0        
        if(flags ==GPIO_INT_MODE_DISABLE_ONLY)
        {
            gpio_pin_interrupt_configure_dt(gpio, flags);
        }
        else
        {
            //note:change int level to skip freq poll!
            int v=gpio_pin_get(gpio->port,gpio->pin);
            if(gpio->pin==24)
                LOG_WRN("usb det:%d",v);
            int err = gpio_pin_interrupt_configure_dt(gpio, v?GPIO_INT_LEVEL_INACTIVE:GPIO_INT_LEVEL_ACTIVE);
            if (err) {
                LOG_ERR("Unable to configure interrupt for pin %u on %s,err:%d", gpio->pin, gpio->port->name,err);
                return err;
            }
        }
#endif         
    }

    return 0;
}
#endif

#if USE_INTERRUPTS
int kscan_direct_interrupt_enable(const struct device *dev) {
    return kscan_direct_interrupt_configure(dev, GPIO_INT_LEVEL_ACTIVE);
}
#endif

#if USE_INTERRUPTS
static int kscan_direct_interrupt_disable(const struct device *dev) {
    return kscan_direct_interrupt_configure(dev, GPIO_INT_MODE_DISABLE_ONLY);//GPIO_INT_DISABLE);
}
#endif

#if USE_INTERRUPTS
static void kscan_direct_irq_callback_handler(const struct device *port, struct gpio_callback *cb,
                                              const gpio_port_pins_t pin) {
    struct kscan_direct_irq_callback *irq_data =
        CONTAINER_OF(cb, struct kscan_direct_irq_callback, callback);
    struct kscan_direct_data *data = irq_data->dev->data;
    suspend=0;
    // is_app_enabled_dlps=false;
    // DBG_DIRECT("gpio int:%x",pin);
    // Disable our interrupts temporarily to avoid re-entry while we scan.
    kscan_direct_interrupt_disable(data->dev);

    data->scan_time = k_uptime_get();

    k_work_reschedule(&data->work, K_NO_WAIT);
}
#endif

static gpio_flags_t kscan_gpio_get_extra_flags(const struct gpio_dt_spec *gpio, bool active) {
    if (!active) {
        return ((BIT(0) & gpio->dt_flags) ? GPIO_PULL_UP : GPIO_PULL_DOWN);
    }
    return 0;
}

static int kscan_inputs_set_flags(const struct kscan_gpio_list *inputs,
                                  const struct gpio_dt_spec *active_gpio) {
    for (int i = 0; i < inputs->len; i++) {
        const bool active = &inputs->gpios[i].spec == active_gpio;
        const gpio_flags_t extra_flags =
            GPIO_INPUT | kscan_gpio_get_extra_flags(&inputs->gpios[i].spec, active);
        LOG_DBG("Extra flags equal to: %d", extra_flags);

        int err = gpio_pin_configure_dt(&inputs->gpios[i].spec, extra_flags);
        if (err) {
            LOG_ERR("Unable to configure flags on pin %d on %s", inputs->gpios[i].spec.pin,
                    inputs->gpios[i].spec.port->name);
            return err;
        }
    }
    return 0;
}

static void kscan_direct_read_continue(const struct device *dev) {
    const struct kscan_direct_config *config = dev->config;
    struct kscan_direct_data *data = dev->data;

    data->scan_time +=  config->debounce_scan_period_ms;
    // DBG_DIRECT("continue:%d",data->scan_time);
    kscan_enabled_dlps =false;
    k_work_reschedule(&data->work, K_TIMEOUT_ABS_MS(data->scan_time));
}

#if 0
static void kscan_direct_read_end(const struct device *dev) {
#if USE_INTERRUPTS
    // Return to waiting for an interrupt.
    kscan_direct_interrupt_enable(dev);
#else
    struct kscan_direct_data *data = dev->data;
    const struct kscan_direct_config *config = dev->config;

    data->scan_time += config->poll_period_ms;

    // Return to polling slowly.
    k_work_reschedule(&data->work, K_TIMEOUT_ABS_MS(data->scan_time));
#endif
}
#endif

static int kscan_direct_read(const struct device *dev) {
    struct kscan_direct_data *data = dev->data;
    const struct kscan_direct_config *config = dev->config;
    suspend=0;
    // LOG_ERR("kscan_direct_read");
    // Read the inputs.
    struct kscan_gpio_port_state state = {0};

    for (int i = 0; i < data->inputs.len; i++) {
        const struct kscan_gpio *gpio = &data->inputs.gpios[i];

        const int active = kscan_gpio_pin_get(gpio, &state);
        if (active < 0) {
            LOG_ERR("Failed to read port %s: %i", gpio->spec.port->name, active);
            return active;
        }
        // LOG_ERR("i:%d,active:%d,pin:%d",i,active,gpio->spec.pin);
        zmk_debounce_update(&data->pin_state[gpio->index], active, config->debounce_scan_period_ms,
                            &config->debounce_config);
    }

    // Process the new state.
    bool continue_scan = false;
    bool changed=false;
    for (int i = 0; i < data->inputs.len; i++) {
        const struct kscan_gpio *gpio = &data->inputs.gpios[i];
        struct zmk_debounce_state *deb_state = &data->pin_state[gpio->index];
        
        if (zmk_debounce_get_changed(deb_state)) {
            const bool pressed = zmk_debounce_is_pressed(deb_state);
            // DBG_DIRECT("Sending event at 0,%d state %s", gpio->index, pressed ? "on" : "off");
            LOG_ERR("Sending event at 0,%d state %s", gpio->index, pressed ? "on" : "off");
            if(pressed)
                    data->press_rows |= BIT(i);
                else
                    data->press_rows &= ~BIT(i);
            
            if(data->callback) 
                data->callback(dev, 0, gpio->index, pressed);

            if (config->toggle_mode && pressed) {
                kscan_inputs_set_flags(&data->inputs, &gpio->spec);
            }

        }

        continue_scan = continue_scan || (deb_state->counter > 0);//zmk_debounce_is_active(deb_state);
        if(deb_state->counter) changed =true;
        // DBG_DIRECT("i:%d,press:%d,change:%d,count:%d,continue:%d",i,deb_state->pressed,deb_state->changed,deb_state->counter,continue_scan);
    }
    long_poll = !changed;
    if (continue_scan) {
        // At least one key is pressed or the debouncer has not yet decided if
        // it is pressed. Poll quickly until everything is released.
        kscan_direct_read_continue(dev);
    } else {
        // All keys are released. Return to normal.
        // kscan_direct_read_end(dev);
        for (int i = 0; i < data->inputs.len; i++) {
            const struct gpio_dt_spec *gpio = &data->inputs.gpios[i].spec;
            int value = gpio_pin_get_raw(gpio->port,gpio->pin);
            int err = gpio_pin_interrupt_configure_dt(gpio, value? GPIO_INT_LEVEL_LOW:GPIO_INT_LEVEL_HIGH );
            if (err) {
                LOG_ERR("Unable to configure interrupt for pin %u on %s,err:%d", gpio->pin, gpio->port->name,err);
                return err;
            }
        }
    }

    return 0;
}

static void kscan_direct_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct kscan_direct_data *data = CONTAINER_OF(dwork, struct kscan_direct_data, work);
    // LOG_ERR("kscan_direct_work_handler");
    kscan_enabled_dlps =true;
    kscan_direct_read(data->dev);
}

static int kscan_direct_configure(const struct device *dev, kscan_callback_t callback) {
    struct kscan_direct_data *data = dev->data;

    if (!callback) {
        return -EINVAL;
    }

    data->callback = callback;
    return 0;
}

static int kscan_direct_enable(const struct device *dev) {
    struct kscan_direct_data *data = dev->data;
    // LOG_ERR("kscan_direct_enable");
    data->scan_time = k_uptime_get();
    //make sure scan has result!
    for(int i=0;i<2;i++)
    {
        kscan_direct_read(dev);
        k_msleep(20);//need delay more time !!!
    }
    // Read will automatically start interrupts/polling once done.
    return kscan_direct_read(dev);
}

static int kscan_direct_disable(const struct device *dev) {
    struct kscan_direct_data *data = dev->data;

    // LOG_ERR("kscan_direct_disable");
    k_work_cancel_delayable(&data->work);

#if USE_INTERRUPTS
    return kscan_direct_interrupt_disable(dev);
#else
    return 0;
#endif
}

static int kscan_direct_init_input_inst(const struct device *dev, const struct gpio_dt_spec *gpio,
                                        const int index, bool toggle_mode) {
    if (!device_is_ready(gpio->port)) {
        LOG_ERR("GPIO is not ready: %s", gpio->port->name);
        return -ENODEV;
    }
    int err = gpio_pin_configure_dt(
        gpio, GPIO_INPUT | (toggle_mode ? kscan_gpio_get_extra_flags(gpio, false) : 0));
    if (err) {
        LOG_ERR("Unable to configure pin %u on %s for input", gpio->pin, gpio->port->name);
        return err;
    }

    // DBG_DIRECT("Configured pin %d on %s for input", gpio->pin, gpio->port->name);

#if USE_INTERRUPTS
    struct kscan_direct_data *data = dev->data;
    struct kscan_direct_irq_callback *irq = &data->irqs[index];

    irq->dev = dev;
    gpio_init_callback(&irq->callback, kscan_direct_irq_callback_handler, BIT(gpio->pin));
    err = gpio_add_callback(gpio->port, &irq->callback);
    if (err) {
        LOG_ERR("Error adding the callback to the input device: %i", err);
        return err;
    }
#endif

    return 0;
}

static int kscan_direct_init_inputs(const struct device *dev) {
    const struct kscan_direct_data *data = dev->data;
    const struct kscan_direct_config *config = dev->config;

    for (int i = 0; i < data->inputs.len; i++) {
        const struct gpio_dt_spec *gpio = &data->inputs.gpios[i].spec;
        int err = kscan_direct_init_input_inst(dev, gpio, i, config->toggle_mode);
        if (err) {
            return err;
        }
    }

    return 0;
}

static int kscan_direct_init(const struct device *dev) {
    struct kscan_direct_data *data = dev->data;
    const struct kscan_direct_config *config = dev->config;
    data->dev = dev;
    swd_pin_disable();
    // Sort inputs by port so we can read each port just once per scan.
    pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);

    // kscan_gpio_list_sort_by_port(&data->inputs);

    kscan_direct_init_inputs(dev);

    k_work_init_delayable(&data->work, kscan_direct_work_handler);

    return 0;
}

static const struct kscan_driver_api kscan_direct_api = {
    .config = kscan_direct_configure,
    .enable_callback = kscan_direct_enable,
    .disable_callback = kscan_direct_disable,
};

#if IS_ENABLED(CONFIG_PM_DEVICE)

static int kscan_direct_pm_action(const struct device *dev, enum pm_device_action action) {
    const struct kscan_direct_config *config = dev->config;
    struct kscan_direct_data *data = dev->data;

    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:       
        {
            const struct pinctrl_state *state;
            pinctrl_lookup_state(config->pcfg, PINCTRL_STATE_SLEEP, &state);
            k_work_cancel_delayable(&data->work);
            for (int i = 0; i < data->inputs.len; i++) {
                const struct gpio_dt_spec *gpio = &data->inputs.gpios[i].spec;
                int value = gpio_pin_get_raw(gpio->port,gpio->pin);
                System_WakeUpPinEnable(state->pins[i].pin, value?PAD_WAKEUP_POL_LOW:PAD_WAKEUP_POL_HIGH, DISABLE);
            }
            pinctrl_apply_state(config->pcfg, PINCTRL_STATE_SLEEP);
#if CONFIG_DEBUG_IO
            Pad_Config(P2_7, PAD_SW_MODE, PAD_NOT_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE, PAD_OUT_LOW);
#endif         
            return 0;
        }
        
        
    case PM_DEVICE_ACTION_RESUME:
#if CONFIG_DEBUG_IO    
        Pad_Config(P2_7, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_LOW);
#endif
        const struct pinctrl_state *state;
        pinctrl_lookup_state(config->pcfg, PINCTRL_STATE_DEFAULT, &state);
        for (int i = 0; i < data->inputs.len; i++) {
            System_WakeUpPinDisable(state->pins[i].pin);

            if (System_WakeUpInterruptValue(state->pins[i].pin) == SET)
            {
                LOG_ERR("direct wakeup pin:%d",state->pins[i].pin);
                Pad_ClearWakeupINTPendingBit(state->pins[i].pin);
            }
        }
        pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);

        return 0;
    default:
        return -ENOTSUP;
    }
}

#endif // IS_ENABLED(CONFIG_PM_DEVICE)

#define KSCAN_DIRECT_INIT(n)                                                                       \
    BUILD_ASSERT(INST_DEBOUNCE_PRESS_MS(n) <= DEBOUNCE_COUNTER_MAX,                                \
                 "ZMK_KSCAN_DEBOUNCE_PRESS_MS or debounce-press-ms is too large");                 \
    BUILD_ASSERT(INST_DEBOUNCE_RELEASE_MS(n) <= DEBOUNCE_COUNTER_MAX,                              \
                 "ZMK_KSCAN_DEBOUNCE_RELEASE_MS or debounce-release-ms is too large");             \
                                                                                                   \
    PINCTRL_DT_INST_DEFINE(n);                                                                     \
    static struct kscan_gpio kscan_direct_inputs_##n[] = {                                         \
        LISTIFY(INST_INPUTS_LEN(n), KSCAN_DIRECT_INPUT_CFG_INIT, (, ), n)};                        \
                                                                                                   \
    static struct zmk_debounce_state kscan_direct_state_##n[INST_INPUTS_LEN(n)];                   \
                                                                                                   \
    COND_INTERRUPTS(                                                                               \
        (static struct kscan_direct_irq_callback kscan_direct_irqs_##n[INST_INPUTS_LEN(n)];))      \
                                                                                                   \
    static struct kscan_direct_data kscan_direct_data_##n = {                                      \
        .inputs = KSCAN_GPIO_LIST(kscan_direct_inputs_##n),                                        \
        .pin_state = kscan_direct_state_##n,                                                       \
        COND_INTERRUPTS((.irqs = kscan_direct_irqs_##n, ))};                                       \
                                                                                                   \
    static struct kscan_direct_config kscan_direct_config_##n = {                                  \
        .debounce_config =                                                                         \
            {                                                                                      \
                .debounce_press_ms = INST_DEBOUNCE_PRESS_MS(n),                                    \
                .debounce_release_ms = INST_DEBOUNCE_RELEASE_MS(n),                                \
            },                                                                                     \
        .debounce_scan_period_ms = DT_INST_PROP(n, debounce_scan_period_ms),                       \
        .poll_period_ms = DT_INST_PROP(n, poll_period_ms),                                         \
        .toggle_mode = DT_INST_PROP(n, toggle_mode),                                               \
        .pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                                 \
    };                                                                                             \
                                                                                                   \
    PM_DEVICE_DT_INST_DEFINE(n, kscan_direct_pm_action);                                           \
    DEVICE_DT_INST_DEFINE(n, &kscan_direct_init, PM_DEVICE_DT_INST_GET(n), &kscan_direct_data_##n,                     \
                          &kscan_direct_config_##n, POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY,       \
                          &kscan_direct_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_DIRECT_INIT);
