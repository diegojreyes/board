/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/usb.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/ppt.h>
#include <zmk/event_manager.h>
#include <zmk/ppt/keyboard_ppt_app.h>
// #include "trace.h"
#include <zephyr/kernel.h>

#define USE_AES_ENC

typedef struct
{
	unsigned char sn:2;
	unsigned char start:1;
	unsigned char md:1;
	unsigned char rsvd0:1;
	unsigned char fhop:1;
	unsigned char rsvd:2;
}rf_packet_md1r_hdr_bitf;
unsigned short whiten_and_calculate_crc_fast(unsigned char *data, unsigned char length);
uint32_t sync_enter_critical(void);
void sync_exit_critical(uint32_t flag);

extern volatile uint8_t md1r_seq;

// volatile uint8_t md1r_seq;
// unsigned short whiten_and_calculate_crc_fast(unsigned char *data, unsigned char length)
// {

// }

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#if 0
static uint8_t *get_keyboard_report(size_t *len) {

    struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
    *len = sizeof(*report);
    return (uint8_t *)report;
}

static int zmk_ppt_send_report(uint8_t *report, size_t len) {
    // for(uint8_t i=0; i<len; i++)
    // {
    //     DBG_DIRECT("ppt send data:0x%x", report[i]);
    // }
    int err = ppt_app_send_data(SYNC_MSG_TYPE_INFINITE_RETRANS,0,report,len);
    return err;
}


int zmk_ppt_send_keyboard_report(void) {
    size_t len;
    struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
    len = sizeof(report->body);
    //DBG_DIRECT("zmk ppt send keyboard data, len is %d",len);

    len =8;//test!!!
    uint8_t tx_keyboard_data[32]={0};
    rf_packet_md1r_hdr_bitf *hdr=(rf_packet_md1r_hdr_bitf *)&tx_keyboard_data[0];
    hdr->sn = md1r_seq++;
    if(hdr->sn==0)
    {
        hdr->sn= 1;
        md1r_seq ++;
    }
    tx_keyboard_data[1] = SYNC_OPCODE_KEYBOARD;
    memcpy(&tx_keyboard_data[2], &report->body, len);
    uint16_t crc = whiten_and_calculate_crc_fast(tx_keyboard_data,len+2);
    tx_keyboard_data[len+2]=crc&0xff;
    tx_keyboard_data[len+3]=crc>>8;    
    
    return zmk_ppt_send_report(tx_keyboard_data, (len+1+3));
    // uint8_t opcode = SYNC_OPCODE_KEYBOARD;
    // uint8_t ppt_report[len+1];
    // ppt_report[0] = opcode;
    // memcpy(&ppt_report[1], report, len);
    // return zmk_ppt_send_report(ppt_report, (len+1));
}

int zmk_ppt_send_consumer_report(void) {
    //DBG_DIRECT("zmk ppt send consumer data");
    struct zmk_hid_consumer_report *report = zmk_hid_get_consumer_report();
    uint16_t len =2;// sizeof(*report);

    uint8_t tx_keyboard_data[32]={0};
    rf_packet_md1r_hdr_bitf *hdr=(rf_packet_md1r_hdr_bitf *)&tx_keyboard_data[0];
    hdr->sn = md1r_seq++;
    if(hdr->sn==0)
    {
        hdr->sn= 1;
        md1r_seq ++;
    }
    tx_keyboard_data[1] = SYNC_OPCODE_CONSUMER;
    memcpy(&tx_keyboard_data[2], &report->body, len);
    uint16_t crc = whiten_and_calculate_crc_fast(tx_keyboard_data,len+2);
    tx_keyboard_data[len+2]=crc&0xff;
    tx_keyboard_data[len+3]=crc>>8;    
    
    return zmk_ppt_send_report(tx_keyboard_data, (len+1+3));
    // uint8_t opcode = SYNC_OPCODE_CONSUMER;
    // uint8_t ppt_report[len+1];
    // ppt_report[0] = opcode;
    // memcpy(&ppt_report[1], report, len);
    // return zmk_ppt_send_report(ppt_report, (len+1));
}
#endif 

K_SEM_DEFINE(ppt_send_sem,3,3);

static void ppt_app_send_msg_cb(sync_msg_type_t type, uint8_t *p_data, uint16_t len,
                                sync_send_info_t *info)
{
    k_sem_give(&ppt_send_sem);
    LOG_DBG("ppt_app_send_msg_cb, type: %d, len: %d, send_result: %d!", type, len,
                    info->res);
    
}
int zmk_ppt_send_keyboard_report(uint8_t *report ,uint8_t len) {
    uint8_t tx_keyboard_data[32]={0};
    k_sem_take(&ppt_send_sem,K_MSEC(20));        
    rf_packet_md1r_hdr_bitf *hdr=(rf_packet_md1r_hdr_bitf *)&tx_keyboard_data[0];
    uint32_t lock= sync_enter_critical();
    hdr->sn = md1r_seq++;
    // if(hdr->sn==0)
    // {
    //     hdr->sn= 1;
    //     md1r_seq ++;
    // }
    sync_exit_critical(lock);
    uint8_t index =1;
#ifndef USE_AES_ENC    
    if(!zmk_get_nkro_status())
    {
        tx_keyboard_data[index++] = SYNC_OPCODE_KEYBOARD;
        memcpy(&tx_keyboard_data[index], report, len);
        index +=len;
    }
    else
    {
        tx_keyboard_data[index++] = SYNC_OPCODE_ALL_KEYBOARD;
        tx_keyboard_data[index++] = report[0];
        memcpy(&tx_keyboard_data[index], report+2, len-2);
        index += len-2;
    }
#else
    bool aes128_ecb_encrypt(uint8_t plaintext[16], const uint8_t key[16], uint8_t *encrypted);
    const  uint8_t kb_pub_usekeys[16] ={0x2c,0xfa,0xf3,0x02,0x6d,0xa0,0x48,0x04,0x82,0x59,0x90,0x4d,0x7a,0xe1,0xfa,0x03};
    
    uint8_t tmp_pub_usekeys[16];
    memcpy(tmp_pub_usekeys,kb_pub_usekeys,16);
    uint8_t magic_t = k_uptime_get_32()&0xff;

    tmp_pub_usekeys[magic_t&0x0f] =  magic_t;
    tmp_pub_usekeys[magic_t>>4]   = ~magic_t;

    if(!zmk_get_nkro_status())
    {
        // static uint8_t tx_seq=0;
        tx_keyboard_data[index++] = SYNC_OPCODE_STD_KEYBOARD_ENCRYPTION_DATA;
        tx_keyboard_data[index++] = magic_t; 
        // report[1]=  tx_seq++;     
        aes128_ecb_encrypt(report,tmp_pub_usekeys,&tx_keyboard_data[index]);
        index+=16;
    }
    else
    {
        tx_keyboard_data[index++] = SYNC_OPCODE_ALL_KEYBOARD_ENCRYPTION_DATA;
        tx_keyboard_data[index++] = magic_t;

        uint8_t text[16]={0};
        text[0]=report[0];
        memcpy(&text[1], report+2, 15); //skip reserve byte;
        aes128_ecb_encrypt(text,tmp_pub_usekeys,&tx_keyboard_data[index]);
        index+=16;

        for( int i=0;i<4;i++)
        {
            tx_keyboard_data[index++]= report[17+i] ^ 0x9c;
        }
        // LOG_HEXDUMP_ERR(report,len,"rp");
        // LOG_HEXDUMP_ERR(tx_keyboard_data,index,"tx");
    }
#endif     
    uint16_t crc = whiten_and_calculate_crc_fast(tx_keyboard_data,index);
    tx_keyboard_data[index++]=crc&0xff;
    tx_keyboard_data[index++]=crc>>8;    
    // LOG_HEXDUMP_ERR(tx_keyboard_data,index,"aes");
    // return 0;
    return sync_msg_send(SYNC_MSG_TYPE_INFINITE_RETRANS, tx_keyboard_data,index, ppt_app_send_msg_cb);//SYNC_MSG_TYPE_DYNAMIC_RETRANS
    // return ppt_app_send_data(SYNC_MSG_TYPE_DYNAMIC_RETRANS,0,report,(len+1+3));//zmk_ppt_send_report(tx_keyboard_data, (len+1+3));

}

int zmk_ppt_send_consumer_report(uint8_t *report ,uint8_t len) {

    uint8_t tx_keyboard_data[32]={0};

    k_sem_take(&ppt_send_sem,K_MSEC(20));

    rf_packet_md1r_hdr_bitf *hdr=(rf_packet_md1r_hdr_bitf *)&tx_keyboard_data[0];
    uint32_t lock= sync_enter_critical();
    hdr->sn = md1r_seq++;
    // if(hdr->sn==0)
    // {
    //     hdr->sn= 1;
    //     md1r_seq ++;
    // }

    sync_exit_critical(lock);
    tx_keyboard_data[1] = SYNC_OPCODE_CONSUMER;
    memcpy(&tx_keyboard_data[2], report, len);
    uint16_t crc = whiten_and_calculate_crc_fast(tx_keyboard_data,len+2);
    tx_keyboard_data[len+2]=crc&0xff;
    tx_keyboard_data[len+3]=crc>>8;    
    
    return sync_msg_send(SYNC_MSG_TYPE_DYNAMIC_RETRANS, tx_keyboard_data,(len+1+3), ppt_app_send_msg_cb);
    // return ppt_app_send_data(SYNC_MSG_TYPE_DYNAMIC_RETRANS,0,report,(len+1+3));//zmk_ppt_send_report(tx_keyboard_data, (len+1+3));

}