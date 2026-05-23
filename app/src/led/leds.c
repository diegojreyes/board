/*
 * Copyright (c) 2018-2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/led.h>
#include <zephyr/pm/device.h>
#include "led_effect.h"
#include <zmk/leds.h>
#include <zephyr/drivers/gpio.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/led_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/endpoints.h>
#include <zephyr/logging/log.h>
#if CONFIG_LED_STRIP  
#include "../rgb/rgb_matrix.h" 
#endif
#include <zmk/battery.h>
#include <zmk/ppt/keyboard_ppt_app.h>
#include "trace.h"
#include "aon_reg.h"
#ifdef CONFIG_ZMK_SSD1306
#include <zmk/disp.h>
#endif 

LOG_MODULE_DECLARE(zmk,CONFIG_ZMK_LOG_LEVEL);

static hid_led_t keyboard_led_state;

int  ble_is_pairing(void);
void restore_led_pair(void)
{
	if(zmk_endpoints_selected().transport ==ZMK_TRANSPORT_BLE)
	{
		int index =ble_is_pairing();
		LOG_WRN("adv pair:%d",index);
		if(index!=-1)
		{
			blue_led_set_state(LED_PEER_STATE_PAIR,index);
		}
	}
	else if(zmk_endpoints_selected().transport ==ZMK_TRANSPORT_PPT)
	{
		if(zmk_ppt_get_state()==KEYBOARD_PPT_STATUS_PAIRING)
		{
			led_24G_set_state(LED_PEER_STATE_PAIR);
		}

	}
}

#ifdef CONFIG_LED_STRIP
_led_indicators rgb_led_indicators ;
void rgb_set_state(uint8_t type,uint8_t led_state,uint8_t index)
{
	// if( rgb_matrix_config.mode!=0x3f)
	// {
	// 	// rgb_led_indicators.rgb_enable =zmk_rgb_get_onoff_status();	
	// 	rgb_led_indicators.rgb_mode = rgb_matrix_config.mode;
	// }
	
	
	rgb_led_indicators.led_count =1;
	rgb_led_indicators.init=0;
	rgb_led_indicators.led_off_time =200;
	rgb_led_indicators.led_on_time =200;
	rgb_led_indicators.leds[0]=BT1_LED_INDEX+index;
	if(type==0)
	{
		rgb_led_indicators.rgb[0].r=0;
		rgb_led_indicators.rgb[0].g=0;
		rgb_led_indicators.rgb[0].b=0xff;
	}
	else
	{
		rgb_led_indicators.rgb[0].r=0;
		rgb_led_indicators.rgb[0].g=0xff;
		rgb_led_indicators.rgb[0].b=0;
	}

	switch(led_state)
	{
		case LED_PEER_STATE_DISCONNECTED:
			// if(rgb_led_indicators.led_flash_count !=-1)
			// {
			// 	rgb_led_indicators.led_flash_count =0;
			// 	return;
			// }
			// else
			// {
			// 	rgb_led_indicators.led_flash_count =0;
				
			// }
			if(rgb_led_indicators.running)
			{
				rgb_led_indicators.led_on_time =0;
				rgb_led_indicators.led_off_time =300;
				rgb_led_indicators.led_flash_count =1;
			}
			else {
				rgb_led_indicators.led_flash_count =0;
				return;
			}
		break;
		case LED_PEER_STATE_CONNECTED:
			rgb_led_indicators.led_flash_count =1;
			rgb_led_indicators.led_on_time =3000;
		break;
		case LED_PEER_STATE_PAIR:
			rgb_led_indicators.led_flash_count =-1;
			rgb_led_indicators.led_off_time =975;
			rgb_led_indicators.led_on_time =975;
			rgb_control_enable =false;
		break;
		case LED_PEER_STATE_RECONN:
			rgb_led_indicators.led_flash_count =3;
			rgb_led_indicators.led_off_time =100;
			rgb_led_indicators.led_on_time =100;
		break;
	}
	rgb_matrix_config.mode =0x3f;
	// LOG_ERR("backup mode:%d",rgb_led_indicators.rgb_mode);
	LOG_ERR("state:%d,flash count:%d,on:%d,off:%d\n",led_state,rgb_led_indicators.led_flash_count,rgb_led_indicators.led_on_time,rgb_led_indicators.led_off_time);
	zmk_rgb_led_indicatots_on();
}

void blue_led_set_state(uint8_t led_state,uint8_t index)
{
	static uint8_t last_index=0;
	LOG_ERR("blue_led_set_state:%d,cur run:%d,exclude:%d",led_state,rgb_led_indicators.running , rgb_led_indicators.exclude);
	if((rgb_led_indicators.running && rgb_led_indicators.exclude)
#if 0//CONFIG_SHIELD_KEYCHRON_RS87_ANSI		
		|| gpio_led_is_power_on()
#endif 	
	)
	{
		LOG_ERR("skip");
		return;
	}
	else if(rgb_led_indicators.running)
	{
		#if CONFIG_ENABLE_GPIO_LED	
			gpio_led_blue_set_state(led_state,index);
		#endif 
		if(led_state ==LED_PEER_STATE_RECONN && (last_index ==index))
		{
			rgb_led_indicators.led_flash_count =3;
			rgb_matrix_config.mode =0x3f;//force to led indicator mode
			return;
		}
	} 
	last_index =index;
	#if CONFIG_ENABLE_GPIO_LED	
		gpio_led_blue_set_state(led_state,index);
	#endif 
	rgb_set_state(0,led_state,index);
}

void led_24G_set_state(uint8_t led_state)
{
	LOG_ERR("led_24G_set_state:%d,cur run:%d,exclude:%d",led_state,rgb_led_indicators.running , rgb_led_indicators.exclude);
	if((rgb_led_indicators.running && rgb_led_indicators.exclude) 
#if 0//CONFIG_SHIELD_KEYCHRON_RS87_ANSI		
		|| gpio_led_is_power_on()
#endif 
	) 
	{
		LOG_ERR("skip");
		return;
	}
	else if(rgb_led_indicators.running)
	{
		#if CONFIG_ENABLE_GPIO_LED	
			gpio_led_24G_set_state(led_state);
		#endif 
		if(led_state ==LED_PEER_STATE_RECONN)
		{
			rgb_led_indicators.led_flash_count =3;
			rgb_matrix_config.mode =0x3f;//force to led indicator mode
			return;
		}
	}
	#if CONFIG_ENABLE_GPIO_LED	
		gpio_led_24G_set_state(led_state);
	#endif 
	rgb_set_state(1,led_state,3);
}
uint8_t get_rgb_test_start(void);
void led_bat_display(void)
{
	if(get_rgb_test_start()) return;
	if(zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT)
	{
		void zmk_ppt_stop_reconn(void);
		zmk_ppt_stop_reconn();
	}
	uint8_t level =get_battery_level();
	LOG_WRN("batt:%d",level);
	// if( rgb_matrix_config.mode!=0x3f)
	// {
	// 	// rgb_led_indicators.rgb_enable =zmk_rgb_get_onoff_status();
	// 	rgb_led_indicators.rgb_mode = rgb_matrix_config.mode;	
	// }
	if(level<10) level =10;
	else if(level>94)
	{
		level =100;
	}
	rgb_led_indicators.bat_level=level/10;
	rgb_led_indicators.leds[0]=16;
	rgb_led_indicators.init=0;
	rgb_led_indicators.exclude =1;
	rgb_led_indicators.bat_info =1;
	rgb_matrix_config.mode =0x3f;
	zmk_rgb_led_indicatots_on();
}
// void led_bat_display(void)
// {
// 	uint8_t level =get_battery_level();
// 	LOG_WRN("batt:%d",level);
// 	if( rgb_matrix_config.mode!=0x3f)
// 	{
// 		// rgb_led_indicators.rgb_enable =zmk_rgb_get_onoff_status();
// 		rgb_led_indicators.rgb_mode = rgb_matrix_config.mode;	
// 	}
// 	if(level<10) level =10;
// 	rgb_led_indicators.led_flash_count =-1;
// 	rgb_led_indicators.led_off_time =200;
// 	rgb_led_indicators.led_on_time =32767;
// 	rgb_led_indicators.led_count =level/10;
// 	rgb_led_indicators.init=0;
// 	rgb_led_indicators.exclude =1;
	
// 	for(int i=0;i<level/10;i++)
// 	{
// 		rgb_led_indicators.leds[i]=16+i;
// 		if(level >=70)
// 		{
// 			rgb_led_indicators.rgb[i].r =0;
// 			rgb_led_indicators.rgb[i].g =0xff;
// 			rgb_led_indicators.rgb[i].b =0;
// 		}
// 		else if(level >30)
// 		{
// 			rgb_led_indicators.rgb[i].r =0;
// 			rgb_led_indicators.rgb[i].g =0;
// 			rgb_led_indicators.rgb[i].b =0xff;
// 		}
// 		else 
// 		{
// 			rgb_led_indicators.rgb[i].r =0xff;
// 			rgb_led_indicators.rgb[i].g =0;
// 			rgb_led_indicators.rgb[i].b =0;
// 		}
// 	}

// 	rgb_matrix_config.mode =0x3f;
// 	zmk_rgb_led_indicatots_on();
// }
uint8_t led_is_display_bat(void)
{
	return rgb_led_indicators.bat_info;
}

void led_bat_display_off(void)
{
	LOG_WRN("led_bat_display_off");
	rgb_led_indicators.rgb_enable=0;
	rgb_led_indicators.running =0;
	rgb_led_indicators.exclude =0;
	rgb_led_indicators.led_flash_count=0;
	rgb_led_indicators.led_count =0;
	// rgb_matrix_config.enable =zmk_rgb_get_onoff_status();
	rgb_matrix_config.mode = rgb_matrix_config.back_mode;// rgb_led_indicators.rgb_mode;
	// rgb_matrix_config.enable? zmk_rgb_matrix_on():zmk_rgb_matrix_off();
	restore_led_pair();
}
void led_recover(uint8_t stop_rgb)
{
	LOG_WRN("led_recover");
	// if( rgb_matrix_config.mode!=0x3f)
	// {
		
	// 	rgb_led_indicators.rgb_mode = rgb_matrix_config.mode;
	// }
	keyboard_led_state.raw =0;
	rgb_led_indicators.led_flash_count =3;
	rgb_led_indicators.led_off_time =300;
	rgb_led_indicators.led_on_time  =300;
	rgb_led_indicators.led_count =0xff;
	rgb_led_indicators.rgb[0].r =0xff;
	rgb_led_indicators.rgb[0].g =0;
	rgb_led_indicators.rgb[0].b =0;
	rgb_led_indicators.init=0;
	rgb_led_indicators.exclude =1;
	rgb_led_indicators.bat_info =0;
	rgb_led_indicators.bat_low =0;
	rgb_matrix_config.mode =0x3f;
	zmk_rgb_led_indicatots_on();
	if(stop_rgb)
		rgb_matrix_config.enable =0;
}
void led_all_on(uint8_t r,uint8_t g, uint8_t b)
{
	rgb_led_indicators.led_flash_count =3;
	rgb_led_indicators.led_off_time =300;
	rgb_led_indicators.led_on_time  =300;
	rgb_led_indicators.led_count =0xff;
	rgb_led_indicators.rgb[0].r =r;
	rgb_led_indicators.rgb[0].g =g;
	rgb_led_indicators.rgb[0].b =b;
	rgb_led_indicators.init=0;
	rgb_led_indicators.exclude =1;
	rgb_matrix_config.mode =0x3f;
	zmk_rgb_led_indicatots_on();
}
void led_bat_low(void)
{
#if !defined(CONFIG_SHIELD_KEYCHRON_Q3ULTRA_ANSI) && !defined(CONFIG_SHIELD_KEYCHRON_Q6ULTRA_ANSI)	
	rgb_led_indicators.bat_low =1;
	rgb_led_indicators.bat_low_start_time =0;
	rgb_led_indicators.exclude =0;
	if(!rgb_led_indicators.running)
	{
		// if( rgb_matrix_config.mode!=0x3f)
		// {
		// 	// rgb_led_indicators.rgb_enable =zmk_rgb_get_onoff_status();
		// 	rgb_led_indicators.rgb_mode = rgb_matrix_config.mode;
		// }
		// rgb_led_indicators.led_flash_count =0;
		rgb_led_indicators.init=0;
		rgb_matrix_config.mode =0x3f;
		zmk_rgb_led_indicatots_on();
	}
#else
	gpio_led_bat_low();
#endif 	
}
void led_rgb_set_color(uint8_t r,uint8_t g,uint8_t b)
{
	// if( rgb_matrix_config.mode!=0x3f)
	// {
	// 	// rgb_led_indicators.rgb_enable =zmk_rgb_get_onoff_status();
	// 	rgb_led_indicators.rgb_mode = rgb_matrix_config.mode;
	// }
	rgb_led_indicators.led_flash_count =-1;
	rgb_led_indicators.led_off_time =0;
	rgb_led_indicators.led_on_time =3000;
	rgb_led_indicators.led_count =0xff;
	rgb_led_indicators.rgb[0].r =r;
	rgb_led_indicators.rgb[0].g =g;
	rgb_led_indicators.rgb[0].b =b;
	rgb_led_indicators.init=0;
	rgb_led_indicators.exclude=1;
	rgb_led_indicators.running=1;
	rgb_matrix_config.mode =0x3f;
	zmk_rgb_led_indicatots_on();
}
void led_charge_set_state(uint8_t state)
{
#ifdef CONFIG_ZMK_SSD1306
	if(LED_BAT_CHARGING==state)
		disp_bat_charging();
	else if(LED_BAT_NONE==state)
		disp_bat_charging_stop();
#endif 		
}
#endif 

uint8_t keyboad_gpio_led_set_onoff(uint8_t led_state);
void keyboad_led_set_onoff(uint8_t led_state)
{
	LOG_DBG("led state:%d",led_state);
	keyboard_led_state.raw =led_state;
#if CONFIG_ENABLE_GPIO_LED	
	uint8_t handled =keyboad_gpio_led_set_onoff(led_state);
	if(handled ) return ;
#endif 	

#if CONFIG_ZMK_SSD1306
	disp_set_led_state(keyboard_led_state.raw);
#elif CONFIG_LED_STRIP	
	if(!rgb_matrix_config.enable || bat_is_low()) 
	{ 
		LOG_DBG("set led state");
		enable_rgb_thread();
	}
#endif 	
	
}
hid_led_t keyboard_get_led_state(void)
{
	return keyboard_led_state;  
}
static int hid_indicator_listener(const zmk_event_t *eh)
{
	struct zmk_hid_indicators_changed *indicator =as_zmk_hid_indicators_changed(eh);
	if(indicator)
	{
		 keyboad_led_set_onoff(indicator->indicators);
	}
	struct zmk_led_state_changed * led = as_zmk_led_state_changed(eh);
	if(led)
	{
		if(led->led_state == LED_PEER_STATE_RECOVER)
		{
			led_recover(1);
		}
		else if(led->led_state == LED_PEER_STATE_POWR_OFF)
		{
			// leds_pwroff();
			keyboard_led_state.raw=0;
		}
		else 
		{
		#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR 
			void winlock_led_onoff(uint8_t onoff);
			void winlock_led_set(void);
			if(led->led_state == LED_PEER_STATE_CONNECTED)
			{
				winlock_led_set();
			}
			else if(led->led_state == LED_PEER_STATE_DISCONNECTED)
			{
				winlock_led_onoff(0);
			}
		#endif 
			switch(led->transport)
			{
			case ZMK_TRANSPORT_BLE:
				blue_led_set_state(led->led_state,led->index);
				break;
			case ZMK_TRANSPORT_PPT:
				led_24G_set_state(led->led_state);
				break;

			}
		}
		
	}
	return ZMK_EV_EVENT_HANDLED;
}
static ZMK_LISTENER(led_listener, hid_indicator_listener);
static ZMK_SUBSCRIPTION(led_listener, zmk_hid_indicators_changed);
static ZMK_SUBSCRIPTION(led_listener, zmk_led_state_changed);

uint32_t aon_read_reg(void)
{
    return AON_REG_READ(AON_NS_REG15X_APP);
}
void aon_write_reg(uint8_t value)
{
    AON_REG_WRITE(AON_NS_REG15X_APP,value);
}


void aon_write_state(uint8_t state)
{
	uint8_t reg = aon_read_reg()&0xff;
	reg |= state;
	aon_write_reg(reg);
}
void aon_clear_state(uint8_t state)
{
	uint8_t reg =aon_read_reg()&0xff;
	reg &= ~state;
	aon_write_reg(reg);
}
bool aon_get_state(uint8_t state)
{
	uint8_t reg =aon_read_reg()&0xff;
	return (reg & state);
}