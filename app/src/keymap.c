/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <drivers/behavior.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>
#include <zmk/sensors.h>
#include <zmk/virtual_key_position.h>

#include <zmk/ble.h>
#if ZMK_BLE_IS_CENTRAL
#include <zmk/split/bluetooth/central.h>
#endif

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/sensor_event.h>
#include <drivers/behavior.h>
#include "./behaviors/behavior_sensor_rotate_common.h"

static zmk_keymap_layers_state_t _zmk_keymap_layer_state = 0;
static uint8_t _zmk_keymap_layer_default = 0;

#define DT_DRV_COMPAT zmk_keymap

#if !DT_NODE_EXISTS(DT_DRV_INST(0))

#error "Keymap node not found, check a keymap is available and is has compatible = "zmk,keymap" set"

#endif

#define TRANSFORMED_LAYER(node)                                                                    \
    {LISTIFY(DT_PROP_LEN(node, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), node)}

#if ZMK_KEYMAP_HAS_SENSORS
#define _TRANSFORM_SENSOR_ENTRY(idx, layer)                                                        \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_PHANDLE_BY_IDX(layer, sensor_bindings, idx)),            \
        .param1 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(layer, sensor_bindings, idx, param1), (0),    \
                              (DT_PHA_BY_IDX(layer, sensor_bindings, idx, param1))),               \
        .param2 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(layer, sensor_bindings, idx, param2), (0),    \
                              (DT_PHA_BY_IDX(layer, sensor_bindings, idx, param2))),               \
    }

#define SENSOR_LAYER(node)                                                                         \
    COND_CODE_1(                                                                                   \
        DT_NODE_HAS_PROP(node, sensor_bindings),                                                   \
        ({LISTIFY(DT_PROP_LEN(node, sensor_bindings), _TRANSFORM_SENSOR_ENTRY, (, ), node)}),      \
        ({}))

#endif /* ZMK_KEYMAP_HAS_SENSORS */

#define LAYER_NAME(node) DT_PROP_OR(node, display_name, DT_PROP_OR(node, label, NULL))

// State

// When a behavior handles a key position "down" event, we record the layer state
// here so that even if that layer is deactivated before the "up", event, we
// still send the release event to the behavior in that layer also.
static uint32_t zmk_keymap_active_behavior_layer[ZMK_KEYMAP_LEN];

static struct zmk_behavior_binding zmk_keymap[ZMK_KEYMAP_LAYERS_LEN][ZMK_KEYMAP_LEN] = {
    DT_INST_FOREACH_CHILD_SEP(0, TRANSFORMED_LAYER, (, ))};

static const char *zmk_keymap_layer_names[ZMK_KEYMAP_LAYERS_LEN] = {
    DT_INST_FOREACH_CHILD_SEP(0, LAYER_NAME, (, ))};

#if ZMK_KEYMAP_HAS_SENSORS

static struct zmk_behavior_binding
    zmk_sensor_keymap[ZMK_KEYMAP_LAYERS_LEN][ZMK_KEYMAP_SENSORS_LEN] = {
        DT_INST_FOREACH_CHILD_SEP(0, SENSOR_LAYER, (, ))};

#endif /* ZMK_KEYMAP_HAS_SENSORS */

static inline int set_layer_state(uint8_t layer, bool state) {
    int ret = 0;
    if (layer >= ZMK_KEYMAP_LAYERS_LEN) {
        return -EINVAL;
    }

    // Default layer should *always* remain active
    if (layer == _zmk_keymap_layer_default && !state) {
        return 0;
    }

    zmk_keymap_layers_state_t old_state = _zmk_keymap_layer_state;
    WRITE_BIT(_zmk_keymap_layer_state, layer, state);
    // Don't send state changes unless there was an actual change
    if (old_state != _zmk_keymap_layer_state) {
        LOG_DBG("layer_changed: layer %d state %d", layer, state);
        ret = raise_layer_state_changed(layer, state);
        if (ret < 0) {
            LOG_WRN("Failed to raise layer state changed (%d)", ret);
        }
    }

    return ret;
}

uint8_t zmk_keymap_layer_default(void) { return _zmk_keymap_layer_default; }

zmk_keymap_layers_state_t zmk_keymap_layer_state(void) { return _zmk_keymap_layer_state; }

bool zmk_keymap_layer_active_with_state(uint8_t layer, zmk_keymap_layers_state_t state_to_test) {
    // The default layer is assumed to be ALWAYS ACTIVE so we include an || here to ensure nobody
    // breaks up that assumption by accident
    return (state_to_test & (BIT(layer))) == (BIT(layer)) || layer == _zmk_keymap_layer_default;
};

bool zmk_keymap_layer_active(uint8_t layer) {
    return zmk_keymap_layer_active_with_state(layer, _zmk_keymap_layer_state);
};

uint8_t zmk_keymap_highest_layer_active(void) {
    for (uint8_t layer = ZMK_KEYMAP_LAYERS_LEN - 1; layer > 0; layer--) {
        if (zmk_keymap_layer_active(layer)) {
            return layer;
        }
    }
    return zmk_keymap_layer_default();
}

int zmk_keymap_layer_activate(uint8_t layer) { return set_layer_state(layer, true); };

int zmk_keymap_layer_deactivate(uint8_t layer) { return set_layer_state(layer, false); };

int zmk_keymap_layer_toggle(uint8_t layer) {
    if (zmk_keymap_layer_active(layer)) {
        return zmk_keymap_layer_deactivate(layer);
    }

    return zmk_keymap_layer_activate(layer);
};

int zmk_keymap_layer_to(uint8_t layer) {
    for (int i = ZMK_KEYMAP_LAYERS_LEN - 1; i >= 0; i--) {
        zmk_keymap_layer_deactivate(i);
    }

    zmk_keymap_layer_activate(layer);

    return 0;
}

bool is_active_layer(uint8_t layer, zmk_keymap_layers_state_t layer_state) {
    return (layer_state & BIT(layer)) == BIT(layer) || layer == _zmk_keymap_layer_default;
}

const char *zmk_keymap_layer_name(uint8_t layer) {
    if (layer >= ZMK_KEYMAP_LAYERS_LEN) {
        return NULL;
    }

    return zmk_keymap_layer_names[layer];
}

int invoke_locally(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event,
                   bool pressed) {
#ifdef CONFIG_RETAIL_DEMO_ENABLE
    bool process_record_retail_demo(struct zmk_behavior_binding * binding, bool pressed);
    if (!process_record_retail_demo(binding, pressed))
        return 0;
#endif
#ifdef CONFIG_CHANGE_REPORT_RATE
    bool user_action_check(struct zmk_behavior_binding * binding,
                           struct zmk_behavior_binding_event event, bool pressed);
    if (!user_action_check(binding, event, pressed))
        return 0;
#endif
    if (pressed) {
        return behavior_keymap_binding_pressed(binding, event);
    } else {
        return behavior_keymap_binding_released(binding, event);
    }
}

int zmk_keymap_apply_position_state(uint8_t source, int layer, uint32_t position, bool pressed,
                                    int64_t timestamp) {
    // We want to make a copy of this, since it may be converted from
    // relative to absolute before being invoked
    struct zmk_behavior_binding binding = zmk_keymap[layer][position];
    const struct device *behavior;
    struct zmk_behavior_binding_event event = {
        .layer = layer,
        .position = position,
        .timestamp = timestamp,
    };

    LOG_DBG("layer: %d position: %d, binding name: %s", layer, position, binding.behavior_dev);

    behavior = zmk_behavior_get_binding(binding.behavior_dev);

    if (!behavior) {
        LOG_WRN("No behavior assigned to %d on layer %d", position, layer);
        return 1;
    }

    int err = behavior_keymap_binding_convert_central_state_dependent_params(&binding, event);
    if (err) {
        LOG_ERR("Failed to convert relative to absolute behavior binding (err %d)", err);
        return err;
    }

    enum behavior_locality locality = BEHAVIOR_LOCALITY_CENTRAL;
    err = behavior_get_locality(behavior, &locality);
    if (err) {
        LOG_ERR("Failed to get behavior locality %d", err);
        return err;
    }

    switch (locality) {
    case BEHAVIOR_LOCALITY_CENTRAL:
        return invoke_locally(&binding, event, pressed);
    case BEHAVIOR_LOCALITY_EVENT_SOURCE:
#if ZMK_BLE_IS_CENTRAL
        if (source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
            return invoke_locally(&binding, event, pressed);
        } else {
            return zmk_split_bt_invoke_behavior(source, &binding, event, pressed);
        }
#else
        return invoke_locally(&binding, event, pressed);
#endif
    case BEHAVIOR_LOCALITY_GLOBAL:
#if ZMK_BLE_IS_CENTRAL
        for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
            zmk_split_bt_invoke_behavior(i, &binding, event, pressed);
        }
#endif
        return invoke_locally(&binding, event, pressed);
    }

    return -ENOTSUP;
}

int zmk_keymap_position_state_changed(uint8_t source, uint32_t position, bool pressed,
                                      int64_t timestamp) {
    if (pressed) {
        zmk_keymap_active_behavior_layer[position] = _zmk_keymap_layer_state;
    }
    for (int layer = ZMK_KEYMAP_LAYERS_LEN - 1; layer >= _zmk_keymap_layer_default; layer--) {
        if (zmk_keymap_layer_active_with_state(layer, zmk_keymap_active_behavior_layer[position])) {
            int ret = zmk_keymap_apply_position_state(source, layer, position, pressed, timestamp);
            if (ret > 0) {
                LOG_DBG("behavior processing to continue to next layer");
                continue;
            } else if (ret < 0) {
                LOG_DBG("Behavior returned error: %d", ret);
                return ret;
            } else {
                return ret;
            }
        }
    }

    return -ENOTSUP;
}

#if ZMK_KEYMAP_HAS_SENSORS
int zmk_keymap_sensor_event(uint8_t sensor_index,
                            const struct zmk_sensor_channel_data *channel_data,
                            size_t channel_data_size, int64_t timestamp) {
    bool opaque_response = false;

    for (int layer = ZMK_KEYMAP_LAYERS_LEN - 1; layer >= 0; layer--) {
        struct zmk_behavior_binding *binding = &zmk_sensor_keymap[layer][sensor_index];

        LOG_DBG("layer: %d sensor_index: %d, binding name: %s", layer, sensor_index,
                binding->behavior_dev);

        const struct device *behavior = zmk_behavior_get_binding(binding->behavior_dev);
        if (!behavior) {
            LOG_DBG("No behavior assigned to %d on layer %d", sensor_index, layer);
            continue;
        }

        struct zmk_behavior_binding_event event = {
            .layer = layer,
            .position = ZMK_VIRTUAL_KEY_POSITION_SENSOR(sensor_index),
            .timestamp = timestamp,
        };

        int ret = behavior_sensor_keymap_binding_accept_data(
            binding, event, zmk_sensors_get_config_at_index(sensor_index), channel_data_size,
            channel_data);

        if (ret < 0) {
            LOG_WRN("behavior data accept for behavior %s returned an error (%d). Processing to "
                    "continue to next layer",
                    binding->behavior_dev, ret);
            continue;
        }

        enum behavior_sensor_binding_process_mode mode =
            (!opaque_response && layer >= _zmk_keymap_layer_default &&
             zmk_keymap_layer_active(layer))
                ? BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_TRIGGER
                : BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_DISCARD;

        ret = behavior_sensor_keymap_binding_process(binding, event, mode);

        if (ret == ZMK_BEHAVIOR_OPAQUE) {
            LOG_DBG("sensor event processing complete, behavior response was opaque");
            opaque_response = true;
        } else if (ret < 0) {
            LOG_DBG("Behavior returned error: %d", ret);
            return ret;
        }
    }

    return 0;
}

#endif /* ZMK_KEYMAP_HAS_SENSORS */

int keymap_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos_ev;
    if ((pos_ev = as_zmk_position_state_changed(eh)) != NULL) {
        return zmk_keymap_position_state_changed(pos_ev->source, pos_ev->position, pos_ev->state,
                                                 pos_ev->timestamp);
    }

#if ZMK_KEYMAP_HAS_SENSORS
    const struct zmk_sensor_event *sensor_ev;
    if ((sensor_ev = as_zmk_sensor_event(eh)) != NULL) {
        return zmk_keymap_sensor_event(sensor_ev->sensor_index, sensor_ev->channel_data,
                                       sensor_ev->channel_data_size, sensor_ev->timestamp);
    }
#endif /* ZMK_KEYMAP_HAS_SENSORS */

    return -ENOTSUP;
}

ZMK_LISTENER(keymap, keymap_listener);
#if ZMK_KEYMAP_HAS_SENSORS
ZMK_SUBSCRIPTION(keymap, zmk_sensor_event);
#endif /* ZMK_KEYMAP_HAS_SENSORS */

ZMK_SUBSCRIPTION(keymap, zmk_position_state_changed);

#if CONFIG_ZMK_LAUNCHER
void update_zmk_keymap(uint8_t layer, uint8_t pos, struct zmk_behavior_binding *binding) {
    zmk_keymap[layer][pos].behavior_dev = binding->behavior_dev;
    zmk_keymap[layer][pos].param1 = binding->param1;
    zmk_keymap[layer][pos].param2 = binding->param2;
}
struct zmk_behavior_binding *get_zmk_keymap(uint8_t layer, uint8_t pos) {
    return &zmk_keymap[layer][pos];
}
#if ZMK_KEYMAP_HAS_SENSORS

static struct zmk_behavior_binding sensor_layer_binding[ZMK_KEYMAP_LAYERS_LEN][2];
int sensor_layer_init(void) {
    for (int i = 0; i < ZMK_KEYMAP_LAYERS_LEN; i++) {
        struct zmk_behavior_binding *binding = &zmk_sensor_keymap[i][0];
        if (!binding)
            continue;
        if (memcmp(binding->behavior_dev, "enc_key_press", 13) == 0) {
            sensor_layer_binding[i][0].behavior_dev = "key_press";
            sensor_layer_binding[i][0].param1 = binding->param1;
            sensor_layer_binding[i][1].behavior_dev = "key_press";
            sensor_layer_binding[i][1].param1 = binding->param2;
        } else {
            const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
            const struct behavior_sensor_rotate_config *cfg = dev->config;
            sensor_layer_binding[i][0].behavior_dev = cfg->cw_binding.behavior_dev;
            sensor_layer_binding[i][0].param1 = cfg->cw_binding.param1;
            sensor_layer_binding[i][1].behavior_dev = cfg->ccw_binding.behavior_dev;
            sensor_layer_binding[i][1].param1 = cfg->ccw_binding.param1;
        }

        LOG_DBG("cc:%s,p:%x,%x,ccw:%s,p:%x,%x", sensor_layer_binding[i][0].behavior_dev,
                sensor_layer_binding[i][0].param1, sensor_layer_binding[i][0].param2,
                sensor_layer_binding[i][1].behavior_dev, sensor_layer_binding[i][1].param1,
                sensor_layer_binding[i][1].param2);
    }
    return 0;
}
struct zmk_behavior_binding *get_zmk_encode_map(uint8_t layer) {

    return &sensor_layer_binding[layer][0];
}
void update_zmk_encoder_map(uint8_t layer, bool cw, struct zmk_behavior_binding *binding,
                            char *target_binding) {
    zmk_sensor_keymap[layer]->behavior_dev = binding->behavior_dev;
    struct zmk_behavior_binding *local_binding =
        cw ? &sensor_layer_binding[layer][0] : &sensor_layer_binding[layer][1];
    if (memcmp(binding->behavior_dev, "enc_key_press", 13) == 0)
        local_binding->behavior_dev = "key_press";
    else if (memcmp(binding->behavior_dev, "rgb_encoder", 11) == 0)
        local_binding->behavior_dev = "rgb_ug";
    else if (memcmp(binding->behavior_dev, "mouse_encoder", 13) == 0) {
        local_binding->behavior_dev = "mouse_key_press";
    } else if (memcmp(binding->behavior_dev, "encoder_any", 11) == 0) {
        local_binding->behavior_dev = target_binding;
    }
    local_binding->param1 = binding->param1;
    local_binding->param2 = binding->param2;
}
SYS_INIT(sensor_layer_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif
#endif

#ifdef CONFIG_CHANGE_REPORT_RATE
#include "rgb/rgb_matrix.h"
#define ACTION_KEY1 0x001d // z
#define ACTION_KEY2 0x001b // x
#define ACTION_KEY3 0x0006 // c
#define ACTION_KEY4 0x000e // k
#define ACTION_FN1 55      // fn1
void set_report_rate_div(uint8_t div);
void set_report_rate(uint8_t div);
void launcher_save_debounce(void);
void led_all_on(uint8_t r, uint8_t g, uint8_t b);

typedef struct {
    uint8_t key3_press : 1;
    uint8_t action : 7;
} key_action_t;
key_action_t key_action;
// static uint32_t key_press_time;

bool user_action_check(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event, bool pressed) {
    static uint8_t key_state = 0;
    uint32_t pos = 0;

    if (memcmp(binding->behavior_dev, "momentary_layer", 15) == 0) {
        LOG_ERR("binding %s,key:%x", binding->behavior_dev, binding->param1);
        if (binding->param1 == 1 || binding->param1 == 3) {
            pos = ACTION_FN1;
        }
    } else if (memcmp(binding->behavior_dev, "key_press", 9) == 0) {
        LOG_ERR("binding key:%x", binding->param1);
        uint32_t key = binding->param1 & 0xffff;

        switch (key) {
        case ACTION_KEY1:
            pos = ACTION_KEY1;
            break;
        case ACTION_KEY2:
            pos = ACTION_KEY2;
            break;
        case ACTION_KEY3:
            pos = ACTION_KEY3;
            break;
        case ACTION_KEY4:
            pos = ACTION_KEY4;
            break;
        }
    }
    if (pos == 0)
        return true;
    if (pressed) {
        switch (pos) {
        case ACTION_KEY1:
            key_state |= 1 << 0;
            break;
        case ACTION_KEY2:
            key_state |= 1 << 1;
            break;
        case ACTION_KEY3:
            key_state |= 1 << 2;
            break;
        case ACTION_KEY4:
            key_state |= 1 << 3;
            break;
        case ACTION_FN1:
            key_state |= 1 << 4;
            break;
        }
        LOG_ERR("ACTION test state:%x,key:%x", key_state, pos);
        if (!key_action.key3_press &&
            ((key_state == 0x19) || (key_state == 0x1c) || (key_state == 0x1a))) {
            LOG_DBG("test 3key press");
            key_action.key3_press = 1;
            if (key_state == 0x19)
                key_action.action = 3;
            else if (key_state == 0x1c)
                key_action.action = 0;
            else if (key_state == 0x1a)
                key_action.action = 1;
            // key_press_time = k_uptime_get_32();
            switch (key_action.action) {
            case 0:
                led_all_on(0xff, 0, 0);
                break;
            case 1:
                led_all_on(0, 0xff, 0);
                break;
            case 3:
                led_all_on(0, 0, 0xff);
                break;
            }

            set_report_rate_div(key_action.action);
            LOG_ERR("set report rate:%d", key_action.action);
            set_report_rate(key_action.action);
            void update_launcher_report_rate(void);
            update_launcher_report_rate();
            launcher_save_debounce();

            if (key_state & 0x01) {
                binding->param1 = ACTION_KEY1;
                behavior_keymap_binding_released(binding, event);
            }
            if (key_state & 0x02) {
                binding->param1 = ACTION_KEY2;
                behavior_keymap_binding_released(binding, event);
            }
            if (key_state & 0x04) {
                binding->param1 = ACTION_KEY3;
                behavior_keymap_binding_released(binding, event);
            }
            if (key_state & 0x08) {
                binding->param1 = ACTION_KEY4;
                behavior_keymap_binding_released(binding, event);
            }

            return false;
        }
    } else {
        if (pos == ACTION_KEY1)
            key_state &= ~(1 << 0);
        else if (pos == ACTION_KEY2)
            key_state &= ~(1 << 1);
        if (pos == ACTION_KEY3)
            key_state &= ~(1 << 2);
        else if (pos == ACTION_KEY4)
            key_state &= ~(1 << 3);
        else if (pos == ACTION_FN1)
            key_state &= ~(1 << 4);

        key_action.key3_press = 0;
    }
    return true;
}
#endif