/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zmk/behavior_queue.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct q_item {
    uint32_t position;
    struct zmk_behavior_binding binding;
    bool press : 1;
    uint32_t wait : 31;
};
#if CONFIG_ZMK_LAUNCHER
extern uint8_t macro_exec_start;
extern uint8_t macro_exec_end;
bool macro_enabled_dlps=true;
uint8_t macro_running=0;
#endif 

#include <zephyr/drivers/counter.h>
#define TIMER DT_NODELABEL(timer2)
void rtk_counter_start(uint32_t usec);
const struct device *const counter_dev = DEVICE_DT_GET(TIMER);
static bool counter_running;

K_MSGQ_DEFINE(zmk_behavior_queue_msgq, sizeof(struct q_item), CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE, 4);

static void behavior_queue_process_next(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(queue_work, behavior_queue_process_next);

static void behavior_queue_process_next(struct k_work *work) {
    struct q_item item = {.wait = 0};

    while (k_msgq_get(&zmk_behavior_queue_msgq, &item, K_NO_WAIT) == 0) {
        LOG_DBG("Invoking %s: 0x%02x 0x%02x", item.binding.behavior_dev, item.binding.param1,
                item.binding.param2);

        struct zmk_behavior_binding_event event = {.position = item.position,
                                                   .timestamp = k_uptime_get()};

        if (item.press) {
            behavior_keymap_binding_pressed(&item.binding, event);
        } else {
            behavior_keymap_binding_released(&item.binding, event);
        }

        LOG_DBG("Processing next queued behavior in %dms", item.wait);

        if (item.wait >= 0) {
            if(item.wait ==0)
                k_work_schedule(&queue_work, K_NO_WAIT);//K_MSEC(item.wait));
            else
                rtk_counter_start(item.wait-60);
            break;
        }
    }
#if CONFIG_ZMK_LAUNCHER
    if(k_msgq_num_used_get(&zmk_behavior_queue_msgq)>0)
    {
        if(macro_exec_start)
        {
            macro_enabled_dlps=false;
            macro_running =1;
        }
            
    }
    else 
    {
        if(macro_exec_end)
        {
            macro_running =0;
            macro_exec_start=0;
            macro_exec_end=0;
            void ppt_macro_end(void);
            ppt_macro_end();
            counter_stop(counter_dev);
            counter_running =false;
            macro_enabled_dlps =true;
            LOG_ERR("macro stop");
        }
        
    }
#endif     
}

int zmk_behavior_queue_add(uint32_t position, const struct zmk_behavior_binding binding, bool press,
                           uint32_t wait) {
    struct q_item item = {.press = press, .binding = binding, .wait = wait};

    const int ret = k_msgq_put(&zmk_behavior_queue_msgq, &item, K_NO_WAIT);
    if (ret < 0) {
        return ret;
    }
    //add:wait macro queue to fill!
    // extern uint8_t macro_exec_start;
    // if(macro_exec_start)
    // {
    //     macro_exec_start =0;
    //     k_work_schedule(&queue_work, K_MSEC(2));
    // }
    // else
    // if (!k_work_delayable_is_pending(&queue_work)) 
    if(!counter_running && !k_work_delayable_is_pending(&queue_work))
    {
        behavior_queue_process_next(&queue_work.work);
    }

    return 0;
}
#if CONFIG_ZMK_LAUNCHER
uint8_t behavior_queue_is_full(void)
{
    return (k_msgq_num_used_get(&zmk_behavior_queue_msgq)>(CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE-32))?1:0;
}
uint16_t get_behavior_queue_num(void)
{
    return k_msgq_num_used_get(&zmk_behavior_queue_msgq);
}
#endif 


static struct counter_top_cfg top_cfg;
static void counter_interrupt_fn(const struct device *dev,
				      void *user_data)
{
    counter_running =false;
    // LOG_ERR("counter int!");
    if(!k_work_delayable_is_pending(&queue_work))
    {
        k_work_schedule(&queue_work, K_NO_WAIT);
        counter_stop(counter_dev);
        macro_enabled_dlps = true;
    }
        
   
}
void rtk_counter_start(uint32_t usec)
{
    
    if (!device_is_ready(counter_dev)) {
		printk("device not ready.\n");
		return ;
	}

	counter_start(counter_dev);

	top_cfg.flags = 0;
	top_cfg.ticks = counter_us_to_ticks(counter_dev, usec);
	top_cfg.callback = counter_interrupt_fn;
	top_cfg.user_data = &top_cfg;

	int err = counter_set_top_value(counter_dev, &top_cfg);
    if(err)
    {
        LOG_ERR("counter set top err:%d",err);
    }
    counter_running =true;
    macro_enabled_dlps=false;
}