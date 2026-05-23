#include "rgb_matrix.h"
#include "./lib8tion/lib8tion.h"
#include <math.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zmk/leds.h>
LOG_MODULE_DECLARE(zmk,CONFIG_ZMK_LOG_LEVEL);

bool rgb_effect_none(effect_params_t *params);
bool breathing(effect_params_t* params);
bool spiral_fade_brightness(effect_params_t* params);
bool cycle_all(effect_params_t* params);
bool cycle_left_right(effect_params_t* params);
bool cycle_up_down(effect_params_t* params);
bool rainbow_moving(effect_params_t* params);
bool cycle_out_in(effect_params_t* params);
bool cycle_out_in_dual_effect(effect_params_t* params);
bool cycle_pinwheel(effect_params_t* params);
bool cycle_spiral(effect_params_t* params);
bool spin_around_center(effect_params_t* params);
bool rainbow_spin(effect_params_t* params);
bool random_raindrop(effect_params_t* params);
bool random_light_key(effect_params_t* params);
bool typing_heatmap(effect_params_t* params);
bool digital_rain(effect_params_t* params);
bool solid_reactive_simple(effect_params_t* params);
bool solid_reactive_multiwide(effect_params_t* params);
bool solid_reactive_multinexus(effect_params_t* params);
bool splash(effect_params_t* params);
bool solid_splash(effect_params_t* params);
bool rgb_run(effect_params_t *params);
bool solid_color(effect_params_t* params);
bool led_eff_indicators(effect_params_t* params);

bool mixed_rgb(effect_params_t *params) ;
bool per_key_rgb(effect_params_t *params);

#ifdef CONFIG_KEYCHRON_RGB_ENABLE
#define zmk_rgb_matrix_set_color(i,r,g,b)  zmk_rgb_matrix_region_set_color(params->region,i,r,g,b)
#define zmk_rgb_matrix_set_color_all(r,g,b) zmk_rgb_matrix_region_set_color_all(params->region,r,g,b)
#endif 

rgb_effect_func  rgb_effect_funcs[]=
{
    rgb_effect_none,
#ifdef ENABLE_RGB_EFFECT_SOLID_COLOR
    solid_color,
#endif     
#ifdef ENABLE_RGB_EFFECT_BREATHING
    breathing,
#endif 
#ifdef ENABLE_RGB_EFFECT_SPIRAL_BRIGHTNESS
    spiral_fade_brightness,
#endif     
#ifdef ENABLE_RGB_EFFECT_CYCLE_ALL
    cycle_all,
#endif    
#ifdef ENABLE_RGB_EFFECT_CYCLE_LEFT_RIGHT
    cycle_left_right,
#endif 
#ifdef ENABLE_RGB_EFFECT_CYCLE_UP_DOWN
    cycle_up_down,
#endif   
#ifdef ENABLE_RGB_EFFECT_RAINBOW_MOVING
    rainbow_moving,
#endif   
#ifdef ENABLE_RGB_EFFECT_CYCLE_OUT_IN
    cycle_out_in,
#endif   
#ifdef ENABLE_RGB_EFFECT_CYCLE_OUT_IN_DUAL
    cycle_out_in_dual_effect,
#endif   
#ifdef ENABLE_RGB_EFFECT_CYCLE_PINWHEEL
    cycle_pinwheel,
#endif  
#ifdef ENABLE_RGB_EFFECT_CYCLE_SPIRAL
    cycle_spiral,
#endif    
#ifdef ENABLE_RGB_EFFECT_SPIN_AROUND_CENTER
    spin_around_center,
#endif 
#ifdef ENABLE_RGB_EFFECT_RAINBOW_SPIN
    rainbow_spin,
#endif 
#ifdef ENABLE_RGB_EFFECT_RANDOM_RAINDROPS
    random_raindrop,
#endif  
#ifdef ENABLE_RGB_EFFECT_RANDOM_LIGHT_KEY
    random_light_key,
#endif    
#ifdef ENABLE_RGB_EFFECT_TYPING_HEATMAP
    typing_heatmap,
#endif 
#ifdef ENABLE_RGB_EFFECT_DIGITAL_RAIN
    digital_rain,
#endif  
#ifdef ENABLE_RGB_EFFECT_SOLID_REACTIVE_SIMPLE
    solid_reactive_simple,
#endif
#ifdef ENABLE_RGB_EFFECT_SOLID_REACTIVE_MULTIWIDE
    solid_reactive_multiwide,
#endif   
#ifdef ENABLE_RGB_EFFECT_SOLID_REACTIVE_MULTINEXUS
    solid_reactive_multinexus,
#endif
#ifdef ENABLE_RGB_EFFECT_SPLASH
    splash,
#endif 
#ifdef ENABLE_RGB_EFFECT_SOLID_SPLASH
    solid_splash,
#endif    

#ifdef ENABLE_RGB_EFFECT_PER_KEY_RGB
    per_key_rgb,
#endif 
#ifdef ENABLE_RGB_EFFECT_MIXED_RGB
    mixed_rgb,
#endif 


#ifdef ENABLE_RGB_EFFECT_RUN
    rgb_run,
#endif   
};
typedef HSV (*dx_dy_f)(HSV hsv, int16_t dx, int16_t dy, uint8_t time);
typedef HSV (*dx_dy_dist_f)(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint8_t time);
typedef HSV (*i_f)(HSV hsv, uint8_t i, uint8_t time);
typedef HSV (*sin_cos_i_f)(HSV hsv, int8_t sin, int8_t cos, uint8_t i, uint8_t time);
typedef HSV (*reactive_splash_f)(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint16_t tick);
typedef HSV (*reactive_f)(HSV hsv, uint16_t offset);

bool effect_reactive_splash_calc(uint8_t start, effect_params_t* params, reactive_splash_f effect_func) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    uint8_t count = g_last_hit_tracker.count;
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        HSV hsv = rgb_matrix_config.hsv;
        hsv.v   = 0;
        for (uint8_t j = start; j < count; j++) {
            int16_t  dx   = g_led_config.point[i].x - g_last_hit_tracker.x[j];
            int16_t  dy   = g_led_config.point[i].y - g_last_hit_tracker.y[j];
            uint8_t  dist = sqrt16(dx * dx + dy * dy);
            uint16_t tick = scale16by8(g_last_hit_tracker.tick[j], qadd8(rgb_matrix_config.speed, 1));
            hsv           = effect_func(hsv, dx, dy, dist, tick);
        }
        //LOG_DBG("i:%d,hsv,cfg:%d,%d,%d,new:%d,%d,%d,",i,rgb_matrix_config.hsv.h,rgb_matrix_config.hsv.s,rgb_matrix_config.hsv.v,hsv.h,hsv.s,hsv.v);
        hsv.v   = scale8(hsv.v, rgb_matrix_config.hsv.v);
        RGB rgb = rgb_matrix_hsv_to_rgb(hsv);
        zmk_rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}
bool effect_reactive_calc(effect_params_t* params, reactive_f effect_func) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    uint8_t d=0;
    uint16_t max_tick = 65535 / qadd8(rgb_matrix_config.speed, 1);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        uint16_t tick = max_tick;
        // Reverse search to find most recent key hit
        for (int8_t j = g_last_hit_tracker.count - 1; j >= 0; j--) {
            if (g_last_hit_tracker.index[j] == i && g_last_hit_tracker.tick[j] < tick) {
                tick = g_last_hit_tracker.tick[j];
                d=1;
                LOG_INF("hit index:%d,tick:%d,count:%d",i,tick,g_last_hit_tracker.count);
                break;
            }
        }

        uint16_t offset = scale16by8(tick, qadd8(rgb_matrix_config.speed, 1));
        RGB      rgb    = rgb_matrix_hsv_to_rgb(effect_func(rgb_matrix_config.hsv, offset));
        if(d) {
            LOG_DBG("offset:%d,i:%d,rgb:%02x%02x%02x",offset,i,rgb.r,rgb.g,rgb.b);
        }
        zmk_rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}
bool effect_sin_cos_time_calc(effect_params_t* params, sin_cos_i_f effect_func) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    uint16_t time      = scale16by8(g_rgb_timer, rgb_matrix_config.speed / 4);
    int8_t   cos_value = cos8(time) - 128;
    int8_t   sin_value = sin8(time) - 128;
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        RGB rgb = rgb_matrix_hsv_to_rgb(effect_func(rgb_matrix_config.hsv, cos_value, sin_value, i, time));
        zmk_rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}

bool effect_time_calc(effect_params_t* params, i_f effect_func) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    uint8_t time = scale16by8(g_rgb_timer, qadd8(rgb_matrix_config.speed / 4, 1));
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        RGB rgb = rgb_matrix_hsv_to_rgb(effect_func(rgb_matrix_config.hsv, i, time));
        zmk_rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}

bool effect_dxy_calc(effect_params_t* params, dx_dy_f effect_func) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    uint8_t time = scale16by8(g_rgb_timer, rgb_matrix_config.speed / 2);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        int16_t dx  = g_led_config.point[i].x - k_rgb_matrix_center.x;
        int16_t dy  = g_led_config.point[i].y - k_rgb_matrix_center.y;
        RGB     rgb = rgb_matrix_hsv_to_rgb(effect_func(rgb_matrix_config.hsv, dx, dy, time));
        zmk_rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}

bool effect_dxy_dist_calc(effect_params_t* params, dx_dy_dist_f effect_func) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    uint8_t time = scale16by8(g_rgb_timer, rgb_matrix_config.speed / 2);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        int16_t dx   = g_led_config.point[i].x - k_rgb_matrix_center.x;
        int16_t dy   = g_led_config.point[i].y - k_rgb_matrix_center.y;
        uint8_t dist = sqrt16(dx * dx + dy * dy);
        RGB     rgb  = rgb_matrix_hsv_to_rgb(effect_func(rgb_matrix_config.hsv, dx, dy, dist, time));
        zmk_rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}

bool rgb_effect_none(effect_params_t *params) {
    if (!params->init) {
        return false;
    }

    zmk_rgb_matrix_set_color_all(0, 0, 0);
    return false;
}

#ifdef ENABLE_RGB_EFFECT_BREATHING
bool breathing(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    HSV      hsv  = rgb_matrix_config.hsv;
    uint16_t time = scale16by8(g_rgb_timer, rgb_matrix_config.speed / 8);
    LOG_DBG("rgb timer:%d,spd:%d,scale:%d",g_rgb_timer,rgb_matrix_config.speed,time);
    hsv.v         = scale8(abs8(sin8(time) - 128) * 2, hsv.v);
    LOG_DBG("hsv1:%d,v2:%d,sin:%d",rgb_matrix_config.hsv.v,hsv.v,abs8(sin8(time) - 128) * 2);
    RGB rgb       = rgb_matrix_hsv_to_rgb(hsv);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        zmk_rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}
#endif 

#ifdef ENABLE_RGB_EFFECT_SPIRAL_BRIGHTNESS
//spiral fades brightness

static HSV spiral_fade_brightness_calc(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint8_t time) {
    hsv.v = scale8(hsv.v + dist - time - atan2_8(dy, dx), hsv.v);
    return hsv;
}

bool spiral_fade_brightness(effect_params_t* params) {
    return effect_dxy_dist_calc(params, &spiral_fade_brightness_calc);
}

#endif     // ENABLE_RGB_EFFECT_BAND_SPIRAL_VAL


#ifdef ENABLE_RGB_EFFECT_CYCLE_ALL
//solid hue cycling through full gradient

static HSV cycle_all_calc(HSV hsv, uint8_t i, uint8_t time) {
    hsv.h = time;
    return hsv;
}

bool cycle_all(effect_params_t* params) {
    return effect_time_calc(params, &cycle_all_calc);
}

#endif     // ENABLE_RGB_EFFECT_CYCLE_ALL

#ifdef ENABLE_RGB_EFFECT_CYCLE_LEFT_RIGHT

static HSV cycle_left_right_change(HSV hsv, uint8_t i, uint8_t time) {
    hsv.h = g_led_config.point[i].x - time;
    return hsv;
}

bool cycle_left_right(effect_params_t* params) {
    return effect_time_calc(params, &cycle_left_right_change);
}

#endif     // ENABLE_RGB_EFFECT_CYCLE_LEFT_RIGHT

#ifdef ENABLE_RGB_EFFECT_CYCLE_UP_DOWN

static HSV cycle_up_down_change(HSV hsv, uint8_t i, uint8_t time) {
    hsv.h = g_led_config.point[i].y - time;
    return hsv;
}

bool cycle_up_down(effect_params_t* params) {
    return effect_time_calc(params, &cycle_up_down_change);
}

#endif     // ENABLE_RGB_EFFECT_CYCLE_UP_DOWN

#ifdef ENABLE_RGB_EFFECT_RAINBOW_MOVING

static HSV rainbow_moving_change(HSV hsv, uint8_t i, uint8_t time) {
    hsv.h += abs8(g_led_config.point[i].y - k_rgb_matrix_center.y) + (g_led_config.point[i].x - time);
    return hsv;
}

bool rainbow_moving(effect_params_t* params) {
    return effect_time_calc(params, &rainbow_moving_change);
}

#endif     // ENABLE_RGB_EFFECT_RAINBOW_MOVING 

#ifdef ENABLE_RGB_EFFECT_CYCLE_OUT_IN
//gradient scrolling out to in

static HSV cycle_out_in_change(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint8_t time) {
    hsv.h = 3 * dist / 2 + time;
    return hsv;
}

bool cycle_out_in(effect_params_t* params) {
    return effect_dxy_dist_calc(params, &cycle_out_in_change);
}
#endif     // ENABLE_RGB_EFFECT_CYCLE_OUT_IN

#ifdef ENABLE_RGB_EFFECT_CYCLE_OUT_IN_DUAL
//dual gradients scrolling out to in

static HSV cycle_out_in_dual_change(HSV hsv, int16_t dx, int16_t dy, uint8_t time) {
    dx           = (k_rgb_matrix_center.x / 2) - abs8(dx);
    uint8_t dist = sqrt16(dx * dx + dy * dy);
    hsv.h        = 3 * dist + time;
    return hsv;
}

bool cycle_out_in_dual_effect(effect_params_t* params) {
    return effect_dxy_calc(params, &cycle_out_in_dual_change);
}

#endif     // ENABLE_RGB_EFFECT_CYCLE_OUT_IN_DUAL

#ifdef ENABLE_RGB_EFFECT_CYCLE_PINWHEEL

static HSV cycle_pinwheel_change(HSV hsv, int16_t dx, int16_t dy, uint8_t time) {
    hsv.h = atan2_8(dy, dx) + time;
    return hsv;
}

bool cycle_pinwheel(effect_params_t* params) {
    return effect_dxy_calc(params, &cycle_pinwheel_change);
}

#endif     // ENABLE_RGB_EFFECT_CYCLE_PINWHEEL

#ifdef ENABLE_RGB_EFFECT_CYCLE_SPIRAL

static HSV cycle_spiral_change(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint8_t time) {
    hsv.h = dist - time - atan2_8(dy, dx);
    return hsv;
}

bool cycle_spiral(effect_params_t* params) {
    return effect_dxy_dist_calc(params, &cycle_spiral_change);
}

#endif     // ENABLE_RGB_EFFECT_CYCLE_SPIRAL

#ifdef ENABLE_RGB_EFFECT_SPIN_AROUND_CENTER
//gradient spinning around center of keyboard

static HSV spin_around_center_change(HSV hsv, int8_t sin, int8_t cos, uint8_t i, uint8_t time) {
    hsv.h += ((g_led_config.point[i].y - k_rgb_matrix_center.y) * cos + (g_led_config.point[i].x - k_rgb_matrix_center.x) * sin) / 128;
    return hsv;
}

bool spin_around_center(effect_params_t* params) {
    return effect_sin_cos_time_calc(params, &spin_around_center_change);
}

#endif     // ENABLE_RGB_EFFECT_DUAL_BEACON

#ifdef ENABLE_RGB_EFFECT_RAINBOW_SPIN
//tighter gradient spinning around center

static HSV rainbow_spin_change(HSV hsv, int8_t sin, int8_t cos, uint8_t i, uint8_t time) {
    hsv.h += ((g_led_config.point[i].y - k_rgb_matrix_center.y) * 2 * cos + (g_led_config.point[i].x - k_rgb_matrix_center.x) * 2 * sin) / 128;
    return hsv;
}

bool rainbow_spin(effect_params_t* params) {
    return effect_sin_cos_time_calc(params, &rainbow_spin_change);
}

#endif     // ENABLE_RGB_EFFECT_RAINBOW_SPIN

#ifdef ENABLE_RGB_EFFECT_RANDOM_RAINDROPS
//Randomly changes a single key's hue and saturation

static void random_raindrops_set_color(int i, effect_params_t* params) {
    if (!HAS_ANY_FLAGS(g_led_config.flags[i], params->flags)) return;
    HSV hsv = {random8(), random8_min_max(127, 255), rgb_matrix_config.hsv.v};
    RGB rgb = rgb_matrix_hsv_to_rgb(hsv);
    zmk_rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
}

bool random_raindrop(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (!params->init) {
        // Change one LED every tick, make sure speed is not 0
        if (scale16by8(g_rgb_timer, qadd8(rgb_matrix_config.speed, 16)) % 5 == 0) {
            random_raindrops_set_color(random8_max(RGB_MATRIX_LED_COUNT), params);
        }
    } else {
        for (int i = led_min; i < led_max; i++) {
            random_raindrops_set_color(i, params);
        }
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}

#endif     // ENABLE_RGB_EFFECT_RANDOM_RAINDROPS

#ifdef ENABLE_RGB_EFFECT_RANDOM_LIGHT_KEY
//Randomly light keys with random hues
#ifndef CONFIG_KEYCHRON_RGB_ENABLE
static inline void set_random_light_key(uint8_t led_index,effect_params_t* params,uint32_t *wait_timer) {
    if (!HAS_ANY_FLAGS(g_led_config.flags[led_index], params->flags)) {
        return;
    }
    HSV hsv = (random8() & 2) ? (HSV){0, 0, 0} : (HSV){random8(), random8_min_max(127, 255), rgb_matrix_config.hsv.v};
    RGB rgb = rgb_matrix_hsv_to_rgb(hsv);
    zmk_rgb_matrix_set_color(led_index, rgb.r, rgb.g, rgb.b);
    *wait_timer = g_rgb_timer + (500 / scale16by8(qadd8(rgb_matrix_config.speed, 16), 16));
}
bool random_light_key(effect_params_t* params) {
    static uint32_t wait_timer = 0;
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (g_rgb_timer > wait_timer) {
        set_random_light_key(random8_max(RGB_MATRIX_LED_COUNT),params,&wait_timer);
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}
#else
 //update code;
static uint32_t rain_wait_timer = 0;
static uint8_t region_mask = 0;


bool random_light_key(effect_params_t* params) {
    region_mask |= 0x01 << params->region;

    inline uint32_t interval(void) {
        return 500 / scale16by8(qadd8(rgb_matrix_config.speed, 16), 16);
    }

    inline void set_random_light_key(uint8_t led_index, uint8_t region) {
        if (!HAS_ANY_FLAGS(g_led_config.flags[led_index], params->flags)) {
            return;
        }
        HSV hsv = (random8() & 2) ? (HSV){0, 0, 0} : (HSV){random8(), random8_min_max(127, 255), rgb_matrix_config.hsv.v};
        RGB rgb = rgb_matrix_hsv_to_rgb(hsv);

        zmk_rgb_matrix_region_set_color(region, led_index, rgb.r, rgb.g, rgb.b);
        rain_wait_timer = g_rgb_timer + interval();
    }

    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (g_rgb_timer > rain_wait_timer) {
        for (uint8_t i=0; i<2; i++) {     // TODO: more region?
            if (region_mask & (0x01 << i)) {
                set_random_light_key(random8_max(RGB_MATRIX_LED_COUNT), i);
            }
        }
        region_mask = 0;
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}

#endif 

#endif     // ENABLE_RGB_EFFECT_RANDOM_LIGHT_KEY

#ifdef ENABLE_RGB_EFFECT_TYPING_HEATMAP
#define RGB_MATRIX_TYPING_HEATMAP_INCREASE_STEP 32
#define RGB_MATRIX_TYPING_HEATMAP_DECREASE_DELAY_MS 25
#define RGB_MATRIX_TYPING_HEATMAP_SPREAD 40
#define RGB_MATRIX_TYPING_HEATMAP_AREA_LIMIT 16
static inline uint8_t led_distance(led_point_t led_a, led_point_t led_b) {
    return sqrt16(((int16_t)(led_a.x - led_b.x) * (int16_t)(led_a.x - led_b.x)) + ((int16_t)(led_a.y - led_b.y) * (int16_t)(led_a.y - led_b.y)));
}
void process_rgb_matrix_typing_heatmap(uint8_t row, uint8_t col) {

    if (g_led_config.matrix_co[row][col] == NO_LED) { // skip as pressed key doesn't have an led position
        return;
    }
    for (uint8_t i_row = 0; i_row < MATRIX_ROWS; i_row++) {
        for (uint8_t i_col = 0; i_col < MATRIX_COLS; i_col++) {
            if (g_led_config.matrix_co[i_row][i_col] == NO_LED) { // skip as target key doesn't have an led position
                continue;
            }
            if (i_row == row && i_col == col) {
                g_rgb_frame_buffer[row][col] = qadd8(g_rgb_frame_buffer[row][col], RGB_MATRIX_TYPING_HEATMAP_INCREASE_STEP);
            } else {
                uint8_t distance = led_distance(g_led_config.point[g_led_config.matrix_co[row][col]], g_led_config.point[g_led_config.matrix_co[i_row][i_col]]);

                if (distance <= RGB_MATRIX_TYPING_HEATMAP_SPREAD) {
                    uint8_t amount = qsub8(RGB_MATRIX_TYPING_HEATMAP_SPREAD, distance);
                    if (amount > RGB_MATRIX_TYPING_HEATMAP_AREA_LIMIT) {
                        amount = RGB_MATRIX_TYPING_HEATMAP_AREA_LIMIT;
                    }
                    g_rgb_frame_buffer[i_row][i_col] = qadd8(g_rgb_frame_buffer[i_row][i_col], amount);
                }
            }
        }
    }

}

// A timer to track the last time we decremented all heatmap values.
static uint16_t heatmap_decrease_timer;
// Whether we should decrement the heatmap values during the next update.
static bool decrease_heatmap_values;

bool typing_heatmap(effect_params_t* params) {
    static uint8_t last_iter = 0;
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    if (params->init) {
        zmk_rgb_matrix_set_color_all(0, 0, 0);
        memset(g_rgb_frame_buffer, 0, sizeof g_rgb_frame_buffer);
    }

    // The heatmap animation might run in several iterations depending on
    // `RGB_MATRIX_LED_PROCESS_LIMIT`, therefore we only want to update the
    // timer when the animation starts.
    if (params->iter == 0 && last_iter != params->iter) {
        decrease_heatmap_values = k_uptime_delta_32(heatmap_decrease_timer) >= RGB_MATRIX_TYPING_HEATMAP_DECREASE_DELAY_MS;

        // Restart the timer if we are going to decrease the heatmap this frame.
        if (decrease_heatmap_values) {
            heatmap_decrease_timer = k_uptime_get_32();
        }
    }

    // Render heatmap & decrease
    uint8_t count = 0;
    for (uint8_t row = 0; row < MATRIX_ROWS && count < RGB_MATRIX_LED_PROCESS_LIMIT; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS && RGB_MATRIX_LED_PROCESS_LIMIT; col++) {
            if (g_led_config.matrix_co[row][col] >= led_min && g_led_config.matrix_co[row][col] < led_max) {
                count++;
                uint8_t val = g_rgb_frame_buffer[row][col];
                if (!HAS_ANY_FLAGS(g_led_config.flags[g_led_config.matrix_co[row][col]], params->flags)) continue;

                HSV hsv = {170 - qsub8(val, 85), rgb_matrix_config.hsv.s, scale8((qadd8(170, val) - 170) * 3, rgb_matrix_config.hsv.v)};
                RGB rgb = rgb_matrix_hsv_to_rgb(hsv);
                zmk_rgb_matrix_set_color(g_led_config.matrix_co[row][col], rgb.r, rgb.g, rgb.b);

                if (decrease_heatmap_values && last_iter != params->iter) {
                    g_rgb_frame_buffer[row][col] = qsub8(val, 1);
                }
            }
        }
    }
    last_iter = params->iter;
    return zmk_rgb_matrix_check_finished_leds(led_max);
}

#endif    

#ifdef ENABLE_RGB_EFFECT_DIGITAL_RAIN

// lower the number for denser effect/wider keyboard
#define RGB_DIGITAL_RAIN_DROPS 24

uint8_t rain_rgb_frame_buffer[MATRIX_ROWS][MATRIX_COLS] = {{0}};

bool digital_rain(effect_params_t* params) {
    // algorithm ported from https://github.com/tremby/Kaleidoscope-LEDEffect-DigitalRain
    const uint8_t drop_ticks           = 28;
    const uint8_t pure_green_intensity = (((uint16_t)rgb_matrix_config.hsv.v) * 3) >> 2;
    const uint8_t max_brightness_boost = (((uint16_t)rgb_matrix_config.hsv.v) * 3) >> 2;
    static uint8_t max_intensity       = RGB_MATRIX_DEFAULT_VAL;
    const uint8_t decay_ticks          = 0xff / (max_intensity?max_intensity:0xff);

    static uint8_t drop  = 0;
    static uint8_t decay = 0;
    static bool render = true;

    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    if (params->init) {
        zmk_rgb_matrix_set_color_all( 0, 0, 0);
        memset(rain_rgb_frame_buffer, 0, sizeof(rain_rgb_frame_buffer));
        drop = 0;
    }

    if (params->iter == 0) {

        if (max_intensity != rgb_matrix_config.hsv.v) {
            // Check if value is decreased
            if (max_intensity > rgb_matrix_config.hsv.v) {
                for (uint8_t col = 0; col < MATRIX_COLS; col++) {
                    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
                        rain_rgb_frame_buffer[row][col] = rain_rgb_frame_buffer[row][col] * (uint16_t)rgb_matrix_config.hsv.v / max_intensity;
                    }
                }
            }

            max_intensity = rgb_matrix_config.hsv.v;
        }

        if (render)
            decay++;

        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
                if (render) {
                    if (row == 0 && drop == 0 && rand() < RAND_MAX / RGB_DIGITAL_RAIN_DROPS) {
                        // top row, pixels have just fallen and we're
                        // making a new rain drop in this column
                        rain_rgb_frame_buffer[row][col] = max_intensity;
                    } else if (rain_rgb_frame_buffer[row][col] > 0 && rain_rgb_frame_buffer[row][col] < max_intensity) {
                        // neither fully bright nor dark, decay it
                        if (decay == decay_ticks) {
                            rain_rgb_frame_buffer[row][col]--;
                        }
                    }
                }
                // set the pixel colour
                uint8_t led[LED_HITS_TO_REMEMBER];
                uint8_t led_count = zmk_rgb_matrix_map_row_column_to_led(row, col, led);

                // TODO: multiple leds are supported mapped to the same row/column
                if (led_count > 0) {
                    if (rain_rgb_frame_buffer[row][col] > pure_green_intensity) {
                        const uint8_t boost = (uint8_t)((uint16_t)max_brightness_boost * (rain_rgb_frame_buffer[row][col] - pure_green_intensity) / (max_intensity - pure_green_intensity));
                        zmk_rgb_matrix_set_color(led[0], boost, max_intensity, boost);
                    } else {
                        const uint8_t green = (uint8_t)((uint16_t)max_intensity * rain_rgb_frame_buffer[row][col] / pure_green_intensity);
                        zmk_rgb_matrix_set_color(led[0], 0, green, 0);
                    }
                }
            }
        }

        if (render) {
            if (decay == decay_ticks) {
                decay = 0;
            }

            if (++drop > drop_ticks) {
                // reset drop timer
                drop = 0;
                for (uint8_t row = MATRIX_ROWS - 1; row > 0; row--) {
                    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
                        // if ths is on the bottom row and bright allow decay
                        if (row == MATRIX_ROWS - 1 && rain_rgb_frame_buffer[row][col] == max_intensity) {
                            rain_rgb_frame_buffer[row][col]--;
                        }
                        // check if the pixel above is bright
                        if (rain_rgb_frame_buffer[row - 1][col] >= max_intensity) { // Note: can be larger than max_intensity if val was recently decreased
                            // allow old bright pixel to decay
                            rain_rgb_frame_buffer[row - 1][col] = max_intensity - 1;
                            // make this pixel bright
                            rain_rgb_frame_buffer[row][col] = max_intensity;
                        }
                    }
                }
            }
        }
        render = false;
    } else {
        render = true;
    }

    return zmk_rgb_matrix_check_finished_leds(led_max);
}


#endif    


#ifdef ENABLE_RGB_EFFECT_SOLID_REACTIVE_SIMPLE

static HSV solid_reactive_simple_change(HSV hsv, uint16_t offset) {
#            ifdef RGB_MATRIX_SOLID_REACTIVE_GRADIENT_MODE
    hsv.h = scale16by8(g_rgb_timer, qadd8(rgb_matrix_config.speed, 8) >> 4);
#            endif
    hsv.v = scale8(255 - offset, hsv.v);
    return hsv;
}

bool solid_reactive_simple(effect_params_t* params) {
    return effect_reactive_calc(params, &solid_reactive_simple_change);
}

#endif     // ENABLE_RGB_EFFECT_SOLID_REACTIVE_SIMPLE


#ifdef ENABLE_RGB_EFFECT_SOLID_REACTIVE_MULTIWIDE

static HSV solid_reactive_wide_change(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint16_t tick) {
    uint16_t effect = tick + dist * 5;
    if (effect > 255) effect = 255;
   // hsv.h = scale16by8(g_rgb_timer, qadd8(rgb_matrix_config.speed, 8) >> 4);
    hsv.v = qadd8(hsv.v, 255 - effect);
    return hsv;
}



bool solid_reactive_multiwide(effect_params_t* params) {
    return effect_reactive_splash_calc(0, params, &solid_reactive_wide_change);
}

#endif     //ENABLE_RGB_EFFECT_SOLID_REACTIVE_MULTIWIDE


#ifdef ENABLE_RGB_EFFECT_SOLID_REACTIVE_MULTINEXUS

static HSV solid_reactive_nexus_change(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint16_t tick) {
    uint16_t effect = tick - dist;
    if (effect > 255) effect = 255;
    if (dist > 72) effect = 255;
    if ((dx > 8 || dx < -8) && (dy > 8 || dy < -8)) effect = 255;
#            ifdef RGB_MATRIX_SOLID_REACTIVE_GRADIENT_MODE
    hsv.h = scale16by8(g_rgb_timer, qadd8(rgb_matrix_config.speed, 8) >> 4) + dy / 4;
#            else
    hsv.h = rgb_matrix_config.hsv.h + dy / 4;
#            endif
    hsv.v = qadd8(hsv.v, 255 - effect);
    return hsv;
}

bool solid_reactive_multinexus(effect_params_t* params) {
    return effect_reactive_splash_calc(0, params, &solid_reactive_nexus_change);
}

#endif     //ENABLE_RGB_EFFECT_SOLID_REACTIVE_MULTINEXUS

#ifdef ENABLE_RGB_EFFECT_SPLASH

HSV splash_change(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint16_t tick) {
    uint16_t effect = tick - dist;
    if (effect > 255) effect = 255;
    hsv.h += effect;
    hsv.v = qadd8(hsv.v, 255 - effect);
    return hsv;
}

bool splash(effect_params_t* params) {
    return effect_reactive_splash_calc(qsub8(g_last_hit_tracker.count, 1), params, &splash_change);
}

#endif  

#ifdef ENABLE_RGB_EFFECT_SOLID_SPLASH

HSV solid_splash_change(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint16_t tick) {
    uint16_t effect = tick - dist;
    if (effect > 255) effect = 255;
    hsv.v = qadd8(hsv.v, 255 - effect);
    return hsv;
}

bool solid_splash(effect_params_t* params) {
    return effect_reactive_splash_calc(qsub8(g_last_hit_tracker.count, 1), params, &solid_splash_change);
}

#endif    

#ifdef ENABLE_RGB_EFFECT_RUN
HSV runner(HSV hsv, uint8_t i, uint8_t time)
{
    static uint8_t index =0;
    static uint16_t last_time=0;
    static HSV last_hsv={0,0,0};
    HSV ret={0,0,0};
    
    {

        if(index== i)
        {
            ret.h = time;
            ret.s = hsv.s;
            ret.v = hsv.v;
            last_hsv =ret;
            
        }
        else if(i<index && i>(index-3)&& index>=3)
        {

            return last_hsv;
        }
        // else
        // {
        //     ret.s =0;
        //     ret.v =0;
        // }
    }
    uint16_t time1 = scale16by8(g_rgb_timer, rgb_matrix_config.speed / 8 +1);
    uint16_t deltar = time1>last_time?(time1-last_time):(time1+65536-last_time);
    // LOG_DBG("time1:%d,last_time:%d,deltar:%d",time1,last_time,deltar);
    if(deltar>32)
    {
        
        index +=1;
        if(index ==RGB_MATRIX_LED_COUNT)
        {
            index =0;
        }
        LOG_DBG("index:%d,time:%d,last_time:%d",index,time1,last_time);
        last_time =time1;
    }
    // if(i ==(RGB_MATRIX_LED_COUNT-1))
    // {
    //     index+=1;
    //     if(index ==RGB_MATRIX_LED_COUNT)
    //     {
    //         index =0;
    //     }
    // }
    return ret;

}
bool rgb_run(effect_params_t *params)
{
    return effect_time_calc(params,&runner);
}
#endif 
#ifdef ENABLE_RGB_EFFECT_SOLID_COLOR
bool solid_color(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    RGB rgb =rgb_matrix_hsv_to_rgb(rgb_matrix_config.hsv);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        zmk_rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return zmk_rgb_matrix_check_finished_leds(led_max);
}
#endif 

extern void os_state_indicate();
// #undef zmk_rgb_matrix_set_color_all
// #undef zmk_rgb_matrix_set_color

bool led_eff_indicators(effect_params_t* params)
{
    static uint32_t flash_count =0;
    static uint32_t led_start_time=0;
    static uint8_t onoff=0;
    static uint8_t bat_index=0;
    static uint8_t bat_state=0;
    if(!rgb_led_indicators.init)
    {
        LOG_WRN("led_eff init,flash count:%d,index:%d",rgb_led_indicators.led_flash_count,rgb_led_indicators.leds[0]);
        led_start_time= 0;
        flash_count =0;
        onoff =0xff;
        rgb_led_indicators.init=1;
        zmk_rgb_matrix_set_color_all(0, 0, 0);
        rgb_led_indicators.running =1;
        bat_index=0;
        bat_state=0;
    }
    if(rgb_led_indicators.led_count !=0xff || get_rgb_test_start())
    {
        // if(CAPS_LOCK_INDEX !=0xff)
        // {
        //     if(keyboard_get_led_state()&0x02)
        //         zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,0xff,0xff,0xff);
        //     else if(get_rgb_test_start())
        //     {
        //         zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
        //     }
        //     else 
        //         zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,0,0,0);
        // }
        // if(NUM_LOCK_INDEX !=0xff)
        // {
        //     if(keyboard_get_led_state()&0x01)
        //         zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,0xff,0xff,0xff);
        //     else if(get_rgb_test_start())
        //     {
        //         zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
        //     }
        //     else 
        //         zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,0,0,0);
        // }
        os_state_indicate();
        #ifdef NUM_LOCK_INDEX
            if(keyboard_get_led_state().num_lock==0)
            {
                if(get_rgb_test_start())
                {
                    zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
                }
                else 
                    zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,0,0,0);
            }
                
        #endif 
        #ifdef CAPS_LOCK_INDEX
            if(keyboard_get_led_state().caps_lock==0)
            {
                if(get_rgb_test_start())
                {
                    zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
                }
                else 
                    zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,0,0,0);
            }
        #endif
    }
    if(rgb_led_indicators.bat_info)
    {
        
        uint32_t deltar=0;
        if(led_start_time ==0)
        {
            led_start_time =k_uptime_get_32();
        } 
        else
            deltar=k_uptime_delta_32(led_start_time);
        switch(bat_state)
        {
            case 0:
            {
                if(bat_index <rgb_led_indicators.bat_level)
                {
                    zmk_rgb_matrix_set_color(BT1_LED_INDEX+bat_index,0xff,0xff,0xff);
                }
                if(deltar>120)
                {
                    bat_index++;
                    if(bat_index ==rgb_led_indicators.bat_level )
                    {
                        bat_state ++;
                    }
                    led_start_time=k_uptime_get_32();
                    LOG_DBG("level:%d,index:%d,state:%d",rgb_led_indicators.bat_level,bat_index,bat_state);
                }
            }
            break;
            case 1:
            {
                uint8_t level =rgb_led_indicators.bat_level;
                for(int i=0;i<level;i++)
                {
#if (!defined(CONFIG_SHIELD_KEYCHRON_Q3ULTRA_ANSI) && !defined(CONFIG_SHIELD_KEYCHRON_Q6ULTRA_ANSI)&& !defined(CONFIG_SHIELD_KEYCHRON_Q1ULTRA_ANSI))                    
                    if(level >=7)
                    {
                        zmk_rgb_matrix_set_color(BT1_LED_INDEX+i,0,0xff,0);
                    }
                    else if(level >3)
                    {
                         zmk_rgb_matrix_set_color(BT1_LED_INDEX+i,0,0,0xff);
                    }
#else
                    if(level >3)
                    {
                         zmk_rgb_matrix_set_color(BT1_LED_INDEX+i,0,0xff,0);
                    }
#endif            
                    else 
                    {
                         zmk_rgb_matrix_set_color(BT1_LED_INDEX+i,0xff,0,0);
                    }
                }
                if(deltar>1500)
                {
                    bat_state ++;
                    LOG_DBG("state%d",bat_state);
                }
            }
            break;
            case 2:
                rgb_matrix_config.mode = rgb_matrix_config.back_mode;// rgb_led_indicators.rgb_mode;
                rgb_led_indicators.rgb_enable =0;
                rgb_led_indicators.running =0;
                rgb_led_indicators.exclude =0;
                rgb_led_indicators.bat_info=0;
                LOG_DBG("batinfo stop,rgb enalbe:%d",rgb_matrix_config.enable);
                void restore_led_pair(void);
                restore_led_pair();

            break;
        }
        return true;
    }
    if(rgb_led_indicators.bat_low)
    {
        static uint8_t bat_on_off=0xff;
        static uint8_t count=0;
        if(rgb_led_indicators.bat_low_start_time==0)
        {
            count =0;
            bat_on_off=0xff;
            rgb_led_indicators.bat_low_start_time = k_uptime_get_32();
        }        
        zmk_rgb_matrix_set_color(BAT_LOW_INDEX,bat_on_off?0xff:0,0,0);
#ifdef CONFIG_SHIELD_KEYCHRON_V10ULTRA_ANSI
        zmk_rgb_matrix_set_color(BAT_LOW_INDEX1,bat_on_off?0xff:0,0,0);
#endif         
        uint32_t deltar=k_uptime_delta_32(rgb_led_indicators.bat_low_start_time); 
        if(deltar >980)
        {
            bat_on_off = ~bat_on_off;
            LOG_ERR("bat low onoff:%d,flash count:%d",bat_on_off,rgb_led_indicators.led_flash_count);
            rgb_led_indicators.bat_low_start_time = k_uptime_get_32();
            if(count ++ >=9)
            {
                LOG_DBG("bat low finish");
                rgb_led_indicators.bat_low =0;
            }
        }
    }
    if(flash_count < rgb_led_indicators.led_flash_count || rgb_led_indicators.led_flash_count == -1)
    {

        uint32_t deltar=k_uptime_delta_32(led_start_time);
        if(onoff)
        {
            if(led_start_time ==0&&(rgb_led_indicators.led_on_time>0) )
            {
                LOG_DBG("on");
                if(rgb_led_indicators.led_count ==0xff)
                {
                    zmk_rgb_matrix_set_color_all(rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
                    // if(CAPS_LOCK_INDEX!=0xff)
                    // {
                    //     if(keyboard_get_led_state()&0x02)
                    //         zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,0xff,0xff,0xff);
                    //     else 
                    //         zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
                    // }
                    // if(NUM_LOCK_INDEX!=0xff)
                    // {
                    //     if(keyboard_get_led_state()&0x01)
                    //         zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,0xff,0xff,0xff);
                    //     else 
                    //         zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
                    // }
                    os_state_indicate();
                    #ifdef NUM_LOCK_INDEX
                        if(keyboard_get_led_state().num_lock==0)
                            zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b); 
                    #endif 
                    #ifdef CAPS_LOCK_INDEX
                        if(keyboard_get_led_state().caps_lock==0)
                            zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b); 
                    #endif
                }
                else
                for(int i=0;i<rgb_led_indicators.led_count ;i++)
                {
                    zmk_rgb_matrix_set_color(rgb_led_indicators.leds[i], rgb_led_indicators.rgb[i].r, rgb_led_indicators.rgb[i].g, rgb_led_indicators.rgb[i].b);
                }
                led_start_time =k_uptime_get_32();
                return true;
            }
            
            if((deltar > rgb_led_indicators.led_on_time)  )
            {
                if (rgb_led_indicators.led_off_time !=0)
                {
                    LOG_DBG("off");                    
                    if(rgb_led_indicators.led_count ==0xff)
                    {
                        zmk_rgb_matrix_set_color_all(0,0,0);
                    }
                    else
                    for(int i=0;i<rgb_led_indicators.led_count ;i++)
                    {
                        zmk_rgb_matrix_set_color(rgb_led_indicators.leds[i], 0, 0, 0);
                    }
                    onoff = ~ onoff;
                }
                led_start_time =k_uptime_get_32();
                flash_count ++;
            }
        }
        else
        {
             if(deltar > rgb_led_indicators.led_off_time)
             {
                LOG_DBG("on");
                led_start_time =k_uptime_get_32();
                if(rgb_led_indicators.led_count ==0xff)
                {
                    zmk_rgb_matrix_set_color_all(rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
                    // if(CAPS_LOCK_INDEX!=0xff)
                    // {
                    //     if(keyboard_get_led_state()&0x02)
                    //         zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,0xff,0xff,0xff);
                    //     else 
                    //         zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
                    // }
                    // if(NUM_LOCK_INDEX!=0xff)
                    // {
                    //     if(keyboard_get_led_state()&0x01)
                    //         zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,0xff,0xff,0xff);
                    //     else 
                    //         zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b);
                    // }
                    os_state_indicate();
                    #ifdef NUM_LOCK_INDEX
                        if(keyboard_get_led_state().num_lock==0)
                            zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b); 
                    #endif 
                    #ifdef CAPS_LOCK_INDEX
                        if(keyboard_get_led_state().caps_lock==0)
                            zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,rgb_led_indicators.rgb[0].r, rgb_led_indicators.rgb[0].g, rgb_led_indicators.rgb[0].b); 
                    #endif 
                }
                else
                for(int i=0;i<rgb_led_indicators.led_count ;i++)
                {
                    zmk_rgb_matrix_set_color(rgb_led_indicators.leds[i], rgb_led_indicators.rgb[i].r, rgb_led_indicators.rgb[i].g, rgb_led_indicators.rgb[i].b);
                }
                onoff = ~ onoff;
                
             }
        }
       
    }
    else if(rgb_led_indicators.led_flash_count != -1 && !rgb_led_indicators.bat_low)
    {
        LOG_WRN("set back rgb mode:%d,enable:%d",rgb_matrix_config.back_mode,rgb_matrix_config.enable);
        // rgb_matrix_config.enable =zmk_rgb_get_onoff_status();//rgb_led_indicators.rgb_enable;
        rgb_matrix_config.mode = rgb_matrix_config.back_mode;//rgb_led_indicators.rgb_mode;
        // rgb_matrix_config.enable? zmk_rgb_matrix_on():zmk_rgb_matrix_off();
        rgb_led_indicators.rgb_enable =0;
        rgb_led_indicators.running =0;
        rgb_led_indicators.exclude =0;
        rgb_led_indicators.led_flash_count=0;
        if(!rgb_control_enable)
            rgb_control_enable =true;
    }
    return true;
}