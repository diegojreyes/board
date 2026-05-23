#include <zephyr/sys/ring_buffer.h>
#include <zephyr/spinlock.h>
#include <zephyr/kernel.h>
#include <zmk/ppt/keyboard_ppt_app.h>
#include <zmk/ppt.h>
#include <zephyr/logging/log.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include "pm.h"

LOG_MODULE_DECLARE(zmk,4);//CONFIG_ZMK_LOG_LEVEL);
typedef struct
{
	unsigned char sn:2;
	unsigned char start:1;
	unsigned char md:1;
	unsigned char rsvd0:1;
	unsigned char fhop:1;
	unsigned char rsvd:2;
}rf_packet_md1r_hdr_bitf;
typedef struct {
    uint8_t opcode;
    union 
    {
        uint8_t plain_payload[33];
        struct {
            uint8_t magic;
            uint8_t enc_payload[32];
        };
    };
    uint16_t crc;

}__packed ppt_packet_t;

typedef struct{
    rf_packet_md1r_hdr_bitf hdr;
    ppt_packet_t packet;
}__packed zmk_ppt_report_t;

void zmk_set_nkro_status(bool enable);
bool zmk_get_nkro_status(void);

// extern volatile uint8_t md1r_seq;
extern atomic_t md1r_seq;
static struct k_spinlock lock;

#define USE_AES_ENC
#if (CONFIG_SYS_CLOCK_TICKS_PER_SEC <=1000)
#define POLL_PERIOD K_MSEC(1)
#elif (CONFIG_SYS_CLOCK_TICKS_PER_SEC ==10000)
#define POLL_PERIOD K_TICKS(5)
#endif 

static bool use_aes_enc=true;
bool aes128_ecb_encrypt(uint8_t plaintext[16], const uint8_t key[16], uint8_t *encrypted);
const  uint8_t kb_pub_usekeys[16] ={0x2c,0xfa,0xf3,0x02,0x6d,0xa0,0x48,0x04,0x82,0x59,0x90,0x4d,0x7a,0xe1,0xfa,0x03};
unsigned short whiten_and_calculate_crc_fast(unsigned char *data, unsigned char length);

void poll_timer_expiry_function(struct k_timer *timer);
K_TIMER_DEFINE(poll_timer, poll_timer_expiry_function, NULL);

RING_BUF_DECLARE(zmk_ppt_msgs, sizeof(zmk_ppt_report_t)*64);

void ringbuf_reset(void)
{
	ring_buf_reset(&zmk_ppt_msgs);
}
int ringbuf_used_get(void)
{
	return ring_buf_size_get(&zmk_ppt_msgs)/(sizeof(zmk_ppt_report_t));
}
bool ringbuf_almost_full(void)
{
    return ringbuf_used_get()>20;
}
int ringbuf_msg_peek(zmk_ppt_report_t * rep)
{
	return ring_buf_peek(&zmk_ppt_msgs,(uint8_t*)rep,sizeof(zmk_ppt_report_t));
}
int ringbuf_msg_get(zmk_ppt_report_t * rep)
{
	return ring_buf_get(&zmk_ppt_msgs,(uint8_t*)rep,sizeof(zmk_ppt_report_t));
}
int ringbuf_msg_put(zmk_ppt_report_t * rep)
{
	int err =0;

	if(ring_buf_space_get(&zmk_ppt_msgs)>=sizeof(zmk_ppt_report_t))
	{
		k_spinlock_key_t key =k_spin_lock(&lock);
		int wr=ring_buf_put(&zmk_ppt_msgs,(uint8_t*)rep,sizeof(zmk_ppt_report_t));
		if(wr<sizeof(zmk_ppt_report_t))
		{
			LOG_ERR("write err!");
		}
		k_spin_unlock(&lock,key);
	}
	else
	{
		LOG_ERR("no buf,discard old one!");
		zmk_ppt_report_t rep;
		ringbuf_msg_get(&rep);
		err =1;
	}
	return err;
}

void toggle_debug_pin(void);

int zmk_ppt_send_keyboard_report(uint8_t *report ,uint8_t len) {

    struct zmk_hid_keyboard_report_body *body =(struct zmk_hid_keyboard_report_body *)report;

    zmk_ppt_report_t rep;
    toggle_debug_pin();
    if(!use_aes_enc)
    {
        if(body->_reserved ==0)
        {
            rep.packet.opcode =SYNC_OPCODE_KEYBOARD;
#ifdef CONFIG_REALTEK_USING_PPT_SYNC_SKN            
            uint8_t buf[16]={0};
            buf[0]=report[0];
            memcpy(&buf[1],report+2,len-2); //skip reserve byte;
            memcpy(rep.packet.plain_payload,buf,len-1);
#else            
            memcpy(rep.packet.plain_payload,report,len);
#endif 
        }
        else
        {
            rep.packet.opcode =SYNC_OPCODE_ALL_KEYBOARD;
            rep.packet.plain_payload[0]=report[0];
            memcpy(&rep.packet.plain_payload[1],&report[2],len-2);
        }
    }
    else 
    {
        // LOG_HEXDUMP_DBG(report,len,"tx");
        uint8_t tmp_pub_usekeys[16];
        memcpy(tmp_pub_usekeys,kb_pub_usekeys,16);
        rep.packet.magic = k_uptime_get_32()&0xff;

        tmp_pub_usekeys[rep.packet.magic&0x0f] =  rep.packet.magic;
        tmp_pub_usekeys[rep.packet.magic>>4]   = ~rep.packet.magic;

        if(body->_reserved ==0)
        {
            rep.packet.opcode =SYNC_OPCODE_STD_KEYBOARD_ENCRYPTION_DATA;
#ifdef CONFIG_REALTEK_USING_PPT_SYNC_SKN            
            uint8_t buf[16]={0};
            buf[0]=report[0];
            memcpy(&buf[1],report+2,len-2); //skip reserve byte;
            aes128_ecb_encrypt(buf,tmp_pub_usekeys,rep.packet.enc_payload);
#else            
            aes128_ecb_encrypt(report,tmp_pub_usekeys,rep.packet.enc_payload);
#endif 
        }
        else
        {
            rep.packet.opcode =SYNC_OPCODE_ALL_KEYBOARD_ENCRYPTION_DATA;
            uint8_t text[16]={0};
            text[0]=report[0];
            memcpy(&text[1], report+2, 15); //skip report 's reserve byte;
            aes128_ecb_encrypt(text,tmp_pub_usekeys,rep.packet.enc_payload);
            for( int i=16;i<20;i++)
            {
                rep.packet.enc_payload[i]= report[i+1] ^ 0x9c;
            }
        }
    }

    if(!zmk_ppt_is_ready()) return -1;
    // if(k_timer_remaining_get(&poll_timer)==0)
    // {
    //     k_timer_start(&poll_timer,POLL_PERIOD ,K_FOREVER);
    // }
    // // LOG_DBG("msg:%d",ringbuf_used_get());
    // return ringbuf_msg_put(&rep);
    ringbuf_msg_put(&rep);
    LOG_DBG("msg:%d",ringbuf_used_get());
    if(k_timer_remaining_get(&poll_timer)==0)
        poll_timer_expiry_function(NULL);
    
    return 0;

}
int zmk_ppt_send_consumer_report(uint8_t *report ,uint8_t len) {

    zmk_ppt_report_t rep;
    rep.packet.opcode =SYNC_OPCODE_CONSUMER;
    memcpy(rep.packet.plain_payload,report,len);
    if(!zmk_ppt_is_ready()) return -1;
    // if(k_timer_remaining_get(&poll_timer)==0)
    // {
    //     k_timer_start(&poll_timer,POLL_PERIOD ,K_FOREVER);
    // }
    // return ringbuf_msg_put(&rep);
    ringbuf_msg_put(&rep);
    if(k_timer_remaining_get(&poll_timer)==0)
        poll_timer_expiry_function(NULL);
    return 0;
}
int zmk_ppt_send_mouse_report(uint8_t *report ,uint8_t len) {

    zmk_ppt_report_t rep;
    rep.packet.opcode =SYNC_OPCODE_MOUSE;
    memcpy(rep.packet.plain_payload,report,len);
    if(!zmk_ppt_is_ready()) return -1;
    // if(k_timer_remaining_get(&poll_timer)==0)
    // {
    //     k_timer_start(&poll_timer,POLL_PERIOD ,K_FOREVER);
    // }
    // return ringbuf_msg_put(&rep);
    ringbuf_msg_put(&rep);
    if(k_timer_remaining_get(&poll_timer)==0)
        poll_timer_expiry_function(NULL);
    return 0;
}
int zmk_24g_send_launcher_report(uint8_t *payload,uint8_t payload_len)
{
    zmk_ppt_report_t rep;
    rep.packet.opcode =0x51;
    payload_len = payload_len>32?32:payload_len;
    rep.packet.plain_payload[0]=2;//interface
    // LOG_HEXDUMP_ERR(payload,payload_len,"tx");
    memcpy(rep.packet.plain_payload+1,payload,payload_len);
    if(!zmk_ppt_is_ready()) return -1;
    ringbuf_msg_put(&rep);
    if(k_timer_remaining_get(&poll_timer)==0)
        poll_timer_expiry_function(NULL);
    return 0;
}
int ppt_send_device_state_info(void)
{
    zmk_ppt_report_t rep;
    rep.packet.opcode =0x41;

    rep.packet.plain_payload[0]=CONFIG_USB_DEVICE_VID &0xff;
    rep.packet.plain_payload[1]=CONFIG_USB_DEVICE_VID >>8;
    rep.packet.plain_payload[2]=CONFIG_USB_DEVICE_PID &0xff;
    rep.packet.plain_payload[3]=CONFIG_USB_DEVICE_PID >>8;

    if(!zmk_ppt_is_ready()) return -1;
    ringbuf_msg_put(&rep);
    if(k_timer_remaining_get(&poll_timer)==0)
        poll_timer_expiry_function(NULL);
    return 0;
}
void poll_timer_expiry_function(struct k_timer *timer)
{
    // LOG_DBG("free:%d,msg:%d",sync_msg_get_available_number(SYNC_MSG_TYPE_INFINITE_RETRANS),ringbuf_used_get());
    uint8_t count=0;
  
    while(sync_msg_get_available_number(SYNC_MSG_TYPE_INFINITE_RETRANS))
    {
        if(ringbuf_used_get())
        {
            zmk_ppt_report_t report[4];
            ringbuf_msg_get(&report[0]);
            
            uint8_t tx[64]={0};
            uint8_t opcode = report[0].packet.opcode;

            
            uint8_t len =0;            
            switch(opcode)
            {
                case SYNC_OPCODE_KEYBOARD:
#ifdef CONFIG_REALTEK_USING_PPT_SYNC_SKN
                    len=1+1+7;
#else                
                    len=1+1+8;
#endif   
                    break;
                case SYNC_OPCODE_CONSUMER:
                    len=1+1+2;
                    break;
                case SYNC_OPCODE_ALL_KEYBOARD:
                    len=1+1+20;
                    break;
                case SYNC_OPCODE_STD_KEYBOARD_ENCRYPTION_DATA:
                    len=1+1+1+16;
                    break;
                case SYNC_OPCODE_ALL_KEYBOARD_ENCRYPTION_DATA:
                     len=1+1+1+20;
                    break;
                case SYNC_OPCODE_MOUSE:
                    len=1+1+5;
                    break;
                case 0x41://device info;
                    len=1+1+4;
                    break;
                case 0x51://use for send launcher daa
                    len=1+1+33;
                    break;
            }
#if SYNC_SUPPORT_MD1R            
            memcpy(tx,&report[0],len);
            uint8_t packet_len = len -1;
#else       
            len -=1;
            memcpy(tx,&report[0].packet,len);
            uint8_t packet_len = len;
#endif             
            for(int i=1;i<4;i++)
            {
                if(ringbuf_used_get())
                {
                    
                    ringbuf_msg_peek(&report[i]);
                    if((opcode ==SYNC_OPCODE_STD_KEYBOARD_ENCRYPTION_DATA
                        || opcode ==SYNC_OPCODE_ALL_KEYBOARD_ENCRYPTION_DATA
                        || opcode ==SYNC_OPCODE_KEYBOARD 
                        || opcode ==SYNC_OPCODE_ALL_KEYBOARD) && report[i].packet.opcode == opcode  )
                    {
                        memcpy(tx+len,&report[i].packet,packet_len);
                        len +=packet_len;
                        ringbuf_msg_get(&report[i]);
                        if(opcode ==SYNC_OPCODE_STD_KEYBOARD_ENCRYPTION_DATA 
                            || opcode ==SYNC_OPCODE_ALL_KEYBOARD_ENCRYPTION_DATA
                            || opcode ==SYNC_OPCODE_ALL_KEYBOARD) 
                            break;
                    }
                    else 
                        break;
                }
                else
                    break;
            }

#if (SYNC_SUPPORT_MD1R)
            // k_spinlock_key_t key =k_spin_lock(&lock);
            // uint32_t lock = sync_enter_critical();
            tx[0] = md1r_seq&0x03;
            // md1r_seq++;
            atomic_inc(&md1r_seq);
            // k_spin_unlock(&lock,key);
            // sync_exit_critical(lock);
            LOG_DBG("rep sn:%d, op:%x,len:%d",tx[0],report[0].packet.opcode,len);
            uint8_t *p_crc = tx+len;            
            uint16_t crc =whiten_and_calculate_crc_fast(tx,len);
            *p_crc++ = crc &0xff;
            *p_crc =crc >>8;
            LOG_DBG("crc:%04x",crc); 
            sync_msg_send(SYNC_MSG_TYPE_INFINITE_RETRANS, tx,len+2, NULL);

            toggle_debug_pin();
#else 
            int err=sync_msg_send(SYNC_MSG_TYPE_INFINITE_RETRANS, tx,len, NULL);
            toggle_debug_pin();
            LOG_WRN("tx len:%d,err:%d",len,err);
#endif             
            if(++count >=3) break;
        }
        else
            break;
    }

    if(ringbuf_used_get())
        k_timer_start(&poll_timer,POLL_PERIOD ,K_FOREVER);
}
uint8_t get_report_rate(void);
//static bool nkro_backup;
void ppt_macro_start(void)
{
    if(zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT)
    {
       
        if(get_report_rate()>8)
        {
            LOG_ERR("ppt active!!!!");
            bt_power_mode_set(BTPOWER_ACTIVE);
        }    
        use_aes_enc=false;
    }
        
    else if(zmk_endpoints_selected().transport == ZMK_TRANSPORT_BLE)
    {
        //note:ble transport slow,nkro status may wrong
        return;
    }
#if !(CONFIG_ADAPATIVE_NKRO)    
    nkro_backup = zmk_get_nkro_status();
    LOG_DBG("start,enc:%d,nkro:%d",use_aes_enc,nkro_backup);
    if(nkro_backup)
    {
        zmk_hid_keyboard_clear();
        zmk_endpoints_send_report(HID_USAGE_KEY);
        zmk_set_nkro_status(false);
    }
#endif     
}
void ppt_macro_end(void)
{
    if(zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT)
    {
        use_aes_enc=true;
        if(get_report_rate()>8)
        {
            if(bt_power_mode_get()== BTPOWER_ACTIVE)
            {
                bt_power_mode_set(BTPOWER_DEEP_SLEEP);
                LOG_ERR("ppt low power");
            }
        }
        
    }
        
    else if(zmk_endpoints_selected().transport == ZMK_TRANSPORT_BLE)
    {
        return;
    }
#if !(CONFIG_ADAPATIVE_NKRO)        
    LOG_DBG("end,enc:%d,nkro:%d",use_aes_enc,nkro_backup);
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
    zmk_set_nkro_status(nkro_backup);
#endif    
}