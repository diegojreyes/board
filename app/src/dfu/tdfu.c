
#include "tdfu.h"
#include "tdfu_prv.h"
#include <patch_header_check.h>
#include "flash_nor_device.h"
#include <utils.h>
#include "trace.h"
#include "dfu_common.h"
#include "string.h"
#include "flash_map.h"
#include <zmk/app_wdt.h>

unsigned char my_scdfu_is_active(void);

#ifndef NULL
#define NULL 0
#endif

enum {
    SUPPORT_LE = 0x01,
    SUPPORT_BR = 0x01,
    SUPPORT_EDR = 0x02,
    SUPPORT_RF = 0x04,
};

enum {
    TX__RSP_RX_CMD_SUCCESS = 0xA1,
    TX__OTA_REPORT = 0xA3,
};

enum {
    CHIP_ID_NULL = 0,
    CHIP_ID_TELINK = 1,
    CHIP_ID_REALTEK = 2,
};

#define MY_DFU_CHIP_ID CHIP_ID_REALTEK

// #define COMMAND_VERSION 0x0001
#define COMMAND_VERSION 0x0003 // support chip id ,support 0x6e (get realtek patch version)

typedef struct {
    unsigned char model_number[10];
    unsigned char BR_LE_mode;
    unsigned char bluetooth_version;
    unsigned char fw_version[10];
    unsigned char hw_version[10];
    unsigned char command_version[2];

#if (ENABLE_DFU_CHIP_ID_CMD)
    unsigned char rsvd[3];
    unsigned char chip_id;
#endif

} BT_INFORMATION_TYPE;

#define DFU_VERSION_VALUE 0x0001
// #define DFU_VERSION_VALUE 0x0002 //support bootloader
// #define DFU_VERSION_VALUE 0x0003

typedef enum {
    DFU_ENC_NONE = 0,
    DFU_ENC_XXTEA,
    DFU_ENC_AES128,
} DFU_ENC_MODE_T;

#define DFU_ENC_SUPPORT DFU_ENC_NONE // DFU_ENC_XXTEA

typedef struct {
    unsigned char dfu_version_l;
    unsigned char dfu_version_h;
    unsigned char encrytion_m;
    // unsigned char bootloader_type;
    // unsigned char bootloader_vid[2];
    // unsigned char bottloader_pid[2];
} tDFU_VERSION;

typedef struct {
    unsigned char hdr_l;
    unsigned char hdr_h;
    unsigned char len;
    unsigned char len_n;
    unsigned char sn;
    unsigned char rsp_cmd;
    unsigned char ack_sn;
    unsigned char ack_cmd;
    unsigned char ack_status;
} CMD_RSP_HDR;

static unsigned char sc_dfu_rsp_buf[65];
static unsigned char sc_dfu_sn = 1;
static unsigned char sc_dfu_security_mode = DFU_ENC_NONE;
static unsigned char sc_dfu_enable = 0;
static unsigned short sc_dfu_sq = 0xffff;
static unsigned char sc_dfu_wait_reboot = 0;

#ifndef UINT32
#define UINT32 unsigned int
#endif

#ifndef UINT8
#define UINT8 unsigned char
#endif

#ifndef INT32
#define INT32 int
#endif

#define DEFAULT_KEY_VAL 0x8F15A902
UINT32 enc_key = 0x8F15A902;
UINT32 Dfu_CRC_Val = 0xFFFFFFFF;
UINT32 Dfu_CRC_Val_for_XXTEA = 0xFFFFFFFF;
static unsigned int dfu_location = 0;

static unsigned int last_sn = 0;

#define FLASH_WR_TEMP_BUFF_MAX_IDX 512
static unsigned short flash_wr_temp_idx = 0;
static unsigned char flash_wr_temp_buff[FLASH_WR_TEMP_BUFF_MAX_IDX];

void my_scdfu_init(void) {}

static unsigned char fw_sc_upgrade_over_myhid_start(void) {
    sc_dfu_enable = 1;
    dfu_location = 0;
    sc_dfu_sq = 0;
    sc_dfu_wait_reboot = 0;
    Dfu_CRC_Val = 0xFFFFFFFF;
    Dfu_CRC_Val_for_XXTEA = 0xFFFFFFFF;

    flash_wr_temp_idx = 0;
    last_sn = 0;

#if (WATCH_DOG_ENABLE == 1)
    app_watchdog_close();
    app_watchdog_open(MY_DFU_WATCH_DOG_TIMEOUT_MS, RESET_ALL_EXCEPT_AON);
#endif

    return 0;
}

static void dfu_commit_temp_buff_to_flash(void) {
    if (flash_wr_temp_idx) {
        dfu_write_data_to_flash(IMG_MCUAPP, dfu_location, 0, flash_wr_temp_idx, flash_wr_temp_buff);
        dfu_location += flash_wr_temp_idx;
        flash_wr_temp_idx = 0;
    }
}

static unsigned char fw_sc_upgrade_over_myhid_write_to_flash(unsigned char *pdata,
                                                             unsigned char len) {
    int ret = 0;

    if (sc_dfu_wait_reboot)
        return 1;

#if (MY_DFU_TRACE_ENABLE)
#if (MY_DFU_DIRECT_TRACE)
    DBG_DIRECT("dfu wr(%d,%x)[crc=%x],len=%d:,%b", sc_dfu_security_mode, dfu_location, Dfu_CRC_Val,
               len, TRACE_BINARY(len, pdata));
#else
    APP_PRINT_INFO5("dfu wr(%d,%x)[crc=%x],len=%d:,%b", sc_dfu_security_mode, dfu_location,
                    Dfu_CRC_Val, len, TRACE_BINARY(len, pdata));
#endif
#endif

    if ((sc_dfu_sq >= (OTA_TMP_SIZE >> 4)) || (len > 48))
        return 1;

    if ((flash_wr_temp_idx + len) <= FLASH_WR_TEMP_BUFF_MAX_IDX) {
        memcpy(&flash_wr_temp_buff[flash_wr_temp_idx], pdata, len);
        flash_wr_temp_idx += len;

        if (flash_wr_temp_idx == FLASH_WR_TEMP_BUFF_MAX_IDX)
            dfu_commit_temp_buff_to_flash();
    } else {
        dfu_commit_temp_buff_to_flash();

        memcpy(&flash_wr_temp_buff[flash_wr_temp_idx], pdata, len);
        flash_wr_temp_idx += len;
    }

    sc_dfu_sq++;

    return ret;
}

/*

*/
static void fw_sc_upgrade_rsp_ack(unsigned char *pdata, unsigned char len) {
    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)pdata;
    unsigned short sum;

    len += 2; // len of sum

    ackhdr->hdr_h = 0x55;
    ackhdr->hdr_l = 0xaa;
    ackhdr->len = len;
    ackhdr->len_n = ~len;
    ackhdr->sn = sc_dfu_sn++;

    if (sc_dfu_sn == 0)
        sc_dfu_sn = 1;

    sum = 0;
    for (unsigned char i = 0; i < (len - 2); i++)
        sum += sc_dfu_rsp_buf[i + 5];

    pdata[5 + len - 2] = sum & 0xff;
    pdata[5 + len - 1] = (sum >> 8) & 0xff;

    unsigned char ret = FW_SC_UPGRADE_HID_REPORT_SEND(MY_DFU_RPT_INPUT_ID, pdata, 32);

    APP_PRINT_INFO2("fw_sc_upgrade_rsp_ack, len=%d,ret=%d", len, ret);

    // retry again
    if (ret == 0) {
        // os_delay(1);
        ret = FW_SC_UPGRADE_HID_REPORT_SEND(MY_DFU_RPT_INPUT_ID, pdata, 32);
    }

    if (ret) {
        if ((len + 5) > 32) {
            // os_delay(1);
            ret = FW_SC_UPGRADE_HID_REPORT_SEND(MY_DFU_RPT_INPUT_ID, (pdata + 32), 32);

            if (ret == 0) {
                // retry again
                // os_delay(1);
                ret = FW_SC_UPGRADE_HID_REPORT_SEND(MY_DFU_RPT_INPUT_ID, (pdata + 32), 32);
            }
        }
    }

    if (ret == 0) {
        APP_PRINT_INFO1("fw_sc_upgrade_rsp_ack fail,ret = %d", ret);
        sc_dfu_enable = 0;
    }
}

static void fw_sc_upgrade_rsp_model_info(unsigned char sn) {
    // unsigned char rsp_idx=0;
    BT_INFORMATION_TYPE *ackpkt = (BT_INFORMATION_TYPE *)(&sc_dfu_rsp_buf[9]);
    unsigned char str_len;
    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;

    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));

    ackhdr->rsp_cmd = TX__OTA_REPORT; // rsp type
    ackhdr->ack_sn = sn;
    ackhdr->ack_cmd = SCFWU_OPCODE_GET_MODEL_INFO;
    ackhdr->ack_status = 0;

    ackpkt->BR_LE_mode = SUPPORT_LE | SUPPORT_RF;
    ackpkt->bluetooth_version = 0x03;
    ackpkt->command_version[0] = COMMAND_VERSION;
    ackpkt->command_version[1] = COMMAND_VERSION >> 8;

    // model string
    for (unsigned char i = 0; i < 10; i++) {
        ackpkt->model_number[i] = 0;
        ackpkt->fw_version[i] = 0;
        ackpkt->hw_version[i] = 0;
    }

    str_len = sizeof(MY_FWU_STRING_NAME) - 1;
    if (str_len > 10)
        str_len = 10;
    memcpy(((unsigned char *)(ackpkt->model_number)), MY_FWU_STRING_NAME, str_len);

    str_len = sizeof(MY_FW_VERSION) - 1;
    if (str_len > 10)
        str_len = 10;
    memcpy(((unsigned char *)(ackpkt->fw_version)), MY_FW_VERSION, str_len);

    str_len = sizeof(MY_HW_VERSION) - 1;
    if (str_len > 10)
        str_len = 10;
    memcpy(((unsigned char *)(ackpkt->hw_version)), MY_HW_VERSION, str_len);

#if (ENABLE_DFU_CHIP_ID_CMD)
    ackpkt->rsvd[0] = 0;
    ackpkt->rsvd[1] = 0;
    ackpkt->rsvd[2] = 0;
    ackpkt->chip_id = MY_DFU_CHIP_ID;
#endif

    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, sizeof(BT_INFORMATION_TYPE) + 4);
}

static void fw_sc_upgrade_rsp_dfu_version(unsigned char sn) {
    tDFU_VERSION *ackpkt = (tDFU_VERSION *)(&sc_dfu_rsp_buf[9]);
    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;

    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));

    ackpkt->dfu_version_l = DFU_VERSION_VALUE & 0xff;
    ackpkt->dfu_version_h = (DFU_VERSION_VALUE >> 8) & 0xff;
    ackpkt->encrytion_m = DFU_ENC_SUPPORT;
    // ackpkt->bootloader_type = 0x01;   //telink zksh boot protocol
    // ackpkt->bootloader_vid[0]=0;
    // ackpkt->bootloader_vid[1]=0;
    // ackpkt->bottloader_pid[0]=0;
    //  ackpkt->bottloader_pid[1]=0;
    ackhdr->rsp_cmd = TX__OTA_REPORT; // rsp type
    ackhdr->ack_sn = sn;
    ackhdr->ack_cmd = SCFWU_OPCODE_GET_DFU_VERSION;
    ackhdr->ack_status = 0;

    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, sizeof(tDFU_VERSION) + 4);
}

#if (MY_DFU_CHIP_ID == CHIP_ID_REALTEK)

static unsigned char cfu_get_patch_version_and_bank_num(unsigned char *pdata, unsigned char len) {
#define MAX_IMG_ID_NUMERS 6

    T_IMAGE_VERSION t_img_version;
    IMG_ID image_id[MAX_IMG_ID_NUMERS] = {IMG_BOOTPATCH,     IMG_OTA,    IMG_MCUPATCH,
                                          IMG_BT_STACKPATCH, IMG_MCUAPP, IMG_UPPERSTACK};

    unsigned char idx = 0;

    if (pdata == 0)
        return 0;

    if (len < MAX_IMG_ID_NUMERS * (2 + 4))
        return 0;

    for (unsigned char i = 0; i < MAX_IMG_ID_NUMERS; i++) {
        pdata[idx++] = image_id[i];
        pdata[idx++] = image_id[i] >> 8;

        get_ota_bank_image_version(true, image_id[i], &t_img_version);

        pdata[idx++] = t_img_version.ver_info.version;
        pdata[idx++] = t_img_version.ver_info.version >> 8;
        pdata[idx++] = t_img_version.ver_info.version >> 16;
        pdata[idx++] = t_img_version.ver_info.version >> 24;

        APP_PRINT_INFO6(
            "[cfu_get_patch_version_and_bank_num,%d],image id=%x, version = %d.%d.%d.%d", i,
            image_id[i], t_img_version.ver_info.img_sub_version._version_major,
            t_img_version.ver_info.img_sub_version._version_minor,
            t_img_version.ver_info.img_sub_version._version_revision,
            t_img_version.ver_info.img_sub_version._version_reserve);
    }
    return idx;
}

static void fw_sc_upgrade_rsp_rtk_patch_version(unsigned char sn) {
    // tDFU_VERSION *ackpkt = (tDFU_VERSION *)(&sc_dfu_rsp_buf[9]);
    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;
    unsigned char buf[48] = {0};
    unsigned char cpy_len = 0;

    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));

    cpy_len = cfu_get_patch_version_and_bank_num(buf, sizeof(buf));

    if (cpy_len) {
        unsigned char *pdata = &sc_dfu_rsp_buf[9];
        memcpy(pdata, buf, cpy_len);
    }

    ackhdr->rsp_cmd = TX__OTA_REPORT; // rsp type
    ackhdr->ack_sn = sn;
    ackhdr->ack_cmd = SCFWU_OPCODE_RTL_PATCH_VERSION;
    ackhdr->ack_status = 0;

    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, 4 + cpy_len);
}
#endif

static void fw_sc_upgrade_set_security_level(unsigned char sn, unsigned char mode) {
    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;

    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));

    if (mode <= DFU_ENC_AES128)
        sc_dfu_security_mode = mode;

#if (MY_DFU_TRACE_ENABLE)
#if (MY_DFU_DIRECT_TRACE)
    DBG_DIRECT("sc_dfu_security_mode = %d \r\n", sc_dfu_security_mode);
#else
    APP_PRINT_INFO1("sc_dfu_security_mode = %d \r\n", sc_dfu_security_mode);
#endif
#endif

    ackhdr->rsp_cmd = TX__OTA_REPORT; // rsp type
    ackhdr->ack_sn = sn;
    ackhdr->ack_cmd = SCFWU_OPCODE_SET_SC_METHOED;
    ackhdr->ack_status = (mode <= DFU_ENC_AES128) ? 0 : 1;

    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, 5 + 4);
}

static void fw_sc_upgrade_ack_success(unsigned char sn, unsigned char rx_cmd) {
    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;
    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));
    unsigned char idx = 0;

    ackhdr->rsp_cmd = TX__RSP_RX_CMD_SUCCESS; // rsp type
    ackhdr->ack_sn = sn;
    ackhdr->ack_cmd = rx_cmd;
    ackhdr->ack_status = 0;

    if (rx_cmd == SCFWU_OPCODE_SEND_BIN) {
        unsigned char *ackpkt = &sc_dfu_rsp_buf[9];
        ackpkt[idx++] = 0;
        ackpkt[idx++] = Dfu_CRC_Val & 0xff;
        ackpkt[idx++] = (Dfu_CRC_Val >> 8) & 0xff;
        ackpkt[idx++] = (Dfu_CRC_Val >> 16) & 0xff;
        ackpkt[idx++] = (Dfu_CRC_Val >> 24) & 0xff;
    }

    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, idx + 4);
}

static void fw_sc_upgrade_ack_fail(unsigned char sn, unsigned char rx_cmd, unsigned char reason) {
    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;
    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));

    ackhdr->rsp_cmd = TX__RSP_RX_CMD_SUCCESS; // rsp type
    ackhdr->ack_sn = sn;
    ackhdr->ack_cmd = rx_cmd;
    ackhdr->ack_status = reason;

    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, 4);
}

UINT32 CRC32(UINT32 crcval, UINT8 *buf, UINT32 length) {
    UINT32 crc32_table = 0;
    UINT32 bit = 0;

    while (length--) {
        crc32_table = ((crcval ^ *buf++) & 0xff);

        for (bit = 0; bit < 8; bit++) {
            if (crc32_table & 1) {
                crc32_table = (crc32_table >> 1) ^ (0xEDB88320);
            } else {
                crc32_table = crc32_table >> 1;
            }
        }

        crcval = (crcval >> 8) ^ crc32_table;
    }

    return crcval;
}

// UINT32 CRC32_for_XXTEA(UINT32 crcval, UINT8 *buf, UINT32 length)
// {
//     UINT32 crc32_table = 0;
//     UINT32 bit = 0;

//     while(length--)
//     {
//         crc32_table = ((crcval ^ *buf++)&0xff);

//         for(bit = 0; bit < 8; bit++)
//         {
//             if(crc32_table&1)
//             {
//                 crc32_table = (crc32_table >> 1)^(0xEDB88320);
//             }
//             else
//             {
//                 crc32_table = crc32_table >> 1;
//             }
//         }
//         crcval = (crcval >> 8)^crc32_table;
//     }

//     return crcval;
// }

// #define MX (z>>5^y<<2) + (y>>3^z<<4)^(sum^y) + (k[p&3^e]^z)

// UINT32 xxtea(UINT32* v, INT32 n, UINT32* k)
// {
//  UINT32 z=v[n-1], y=v[0], sum=0, e, DELTA=0x9e3779b9;
//  UINT32 p, rounds;

//  if(n > 1)
//  {/* 加密过程 */
//      rounds = 6 + 52/n;
//      while(rounds-- > 0)
//      {
//          sum += DELTA; e = (sum >> 2) & 3;
//          for (p=0; p<n-1; p++)

//              {   y = v[p+1];
//                  z = (v[p] += MX);}
//          y = v[0]; z = (v[n-1] += MX);
//      }
//      return 0;
//  }
//  else if(n < -1)
//  {/* 解密过程 */
//      n = -n; rounds = 6 + 52/n; sum = rounds*DELTA;
//      while(sum != 0)
//      {
//          e = (sum >> 2) & 3;
//          for (p=n-1; p>0; p--) z = v[p-1], y = v[p] -= MX;
//          z = v[n-1]; y = v[0] -= MX; sum -= DELTA;
//      }
//      return 0;
//  }
//  return 1;
// }

// unsigned char cipher(UINT8 *pBin, UINT32 len)
// {
//  UINT32 cipher_data[16];
//  unsigned char ret = 0;
//  if(len<=sizeof(cipher_data))
//  {
//      ret = 1;
//      //UINT32 cipher_data[len/4];
//      UINT32 key[4] = {0x8F15A902, 0x1D277B3F, 0x463C55B3, 0xA994D027};

//      key[0] = enc_key++;

//      memset(cipher_data, 0, len);

//      for(UINT32 i=0;i<(len/4);i+=4)
//      {
//          memcpy(&cipher_data[i], pBin+i*4, 16);
//          xxtea(&cipher_data[i], -4, key);
//          memcpy(pBin+i*4, &cipher_data[i], 16);
//      }
//  }

//  return ret;
// }

static void fw_sc_upgrade_write_bin_handle(unsigned char sn, unsigned char *dfu_data,
                                           unsigned char len) {
    unsigned char ret = 0;

    if (!sc_dfu_enable) {
        fw_sc_upgrade_ack_fail(sn, SCFWU_OPCODE_SEND_BIN, 0x01);
        return;
    }

    // APP_PRINT_INFO2("sn=%d,last_sn=%d",sn,last_sn);

    if (last_sn == sn) {
        APP_PRINT_INFO1("retry dfu frame, sn=%d", sn);
        fw_sc_upgrade_ack_success(sn, SCFWU_OPCODE_SEND_BIN);
        return;
    } else if (last_sn) {
        unsigned char next_sn = last_sn + 1;
        if (next_sn == 0)
            next_sn = 1;

        if (next_sn != sn)
            return;
    }

    last_sn = sn;

#if (MY_DFU_TRACE_ENABLE)
#if (MY_DFU_DIRECT_TRACE)
    DBG_DIRECT("fw_sc_upgrade_write_bin_handle,len=%d,%02x %02x %02x %02x\r\n", len, dfu_data[0],
               dfu_data[1], dfu_data[2], dfu_data[3]);
#else
    APP_PRINT_INFO5("fw_sc_upgrade_write_bin_handle,len=%d,%02x %02x %02x %02x\r\n", len,
                    dfu_data[0], dfu_data[1], dfu_data[2], dfu_data[3]);
#endif
#endif
    if (sc_dfu_security_mode == DFU_ENC_NONE) {
        Dfu_CRC_Val = CRC32(Dfu_CRC_Val, dfu_data, len);
    }
    // else if(sc_dfu_security_mode==DFU_ENC_XXTEA)
    //  {
    //  Dfu_CRC_Val_for_XXTEA = CRC32_for_XXTEA(Dfu_CRC_Val_for_XXTEA, dfu_data, len);
    //  cipher(dfu_data, len);
    //  Dfu_CRC_Val = CRC32(Dfu_CRC_Val, dfu_data, len);
    // }

    ret = fw_sc_upgrade_over_myhid_write_to_flash(dfu_data, len);

    // os_delay(1);
    // platform_delay_us(1000);

    if (ret == 0)
        fw_sc_upgrade_ack_success(sn, SCFWU_OPCODE_SEND_BIN);
    else
        fw_sc_upgrade_ack_fail(sn, SCFWU_OPCODE_SEND_BIN, 0x01);
}

static void fw_sc_upgrade_verify_crc(unsigned char sn, unsigned char *pdata, unsigned char len) {
    unsigned int Ciphertext_CRC32;
    unsigned int Plaintext_CRC32;
    unsigned char crc = 1;
    unsigned char idx = 0;

    unsigned char *ackpkt = &sc_dfu_rsp_buf[9];
    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;

    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));

    Ciphertext_CRC32 = 0;
    Ciphertext_CRC32 |= (pdata[0] & 0xff);
    Ciphertext_CRC32 |= ((pdata[1] << 8) & 0xff00);
    Ciphertext_CRC32 |= ((pdata[2] << 16) & 0xff0000);
    Ciphertext_CRC32 |= ((pdata[3] << 24) & 0xff000000);

    Plaintext_CRC32 = 0;
    Plaintext_CRC32 |= (pdata[4] & 0xff);
    Plaintext_CRC32 |= ((pdata[5] << 8) & 0xff00);
    Plaintext_CRC32 |= ((pdata[6] << 16) & 0xff0000);
    Plaintext_CRC32 |= ((pdata[7] << 24) & 0xff000000);

    APP_PRINT_INFO5("fw_sc_upgrade_verify_crc(%d),rx crc:%x,%x,cal crc:%x,%x\r\n",
                    sc_dfu_security_mode, Ciphertext_CRC32, Plaintext_CRC32, Dfu_CRC_Val,
                    Dfu_CRC_Val_for_XXTEA);

#if (MY_DFU_TRACE_ENABLE)
#if (MY_DFU_DIRECT_TRACE)
    DBG_DIRECT("fw_sc_upgrade_verify_crc(%d),rx crc:%x,%x,cal crc:%x,%x\r\n", sc_dfu_security_mode,
               Ciphertext_CRC32, Plaintext_CRC32, Dfu_CRC_Val, Dfu_CRC_Val_for_XXTEA);
#else
//  APP_PRINT_INFO5("fw_sc_upgrade_verify_crc(%d),rx crc:%x,%x,cal
//  crc:%x,%x\r\n",sc_dfu_security_mode,Ciphertext_CRC32,Plaintext_CRC32,Dfu_CRC_Val,Dfu_CRC_Val_for_XXTEA);
#endif
#endif

    if (sc_dfu_security_mode == DFU_ENC_NONE) {
        if ((Ciphertext_CRC32 == Plaintext_CRC32) && (Ciphertext_CRC32 == Dfu_CRC_Val)) {
            crc = 0;
        }
    }
    // else if(sc_dfu_security_mode==DFU_ENC_XXTEA)
    //  {
    //  if((Ciphertext_CRC32==Dfu_CRC_Val_for_XXTEA)
    //      && (Plaintext_CRC32==Dfu_CRC_Val))
    //      {
    //      crc=0;
    //  }
    // }

    ackhdr->rsp_cmd = TX__OTA_REPORT; // rsp type
    ackhdr->ack_sn = sn;
    ackhdr->ack_cmd = SCFWU_OPCODE_VERIFY_CRC32;
    ackhdr->ack_status = 0;

    if (crc == 0) {
        bool check_result = false;
        check_result = dfu_checksum(IMG_MCUAPP, 0);
        // add check
        uint32_t base_addr = flash_nor_get_bank_addr(FLASH_OTA_TMP);
        T_IMG_HEADER_FORMAT *p_header = NULL;
        p_header = (T_IMG_HEADER_FORMAT *)(base_addr);

        if (memcmp(p_header->git_ver._customer_name, MY_FWU_STRING_NAME, 8))
            check_result = false;

        if (check_result == false) {
            crc = 2; // error
            app_wdt_start();
        }
    }

    ackpkt[idx++] = crc;

    ackpkt[idx++] = Dfu_CRC_Val & 0xff;
    ackpkt[idx++] = (Dfu_CRC_Val >> 8) & 0xff;
    ackpkt[idx++] = (Dfu_CRC_Val >> 16) & 0xff;
    ackpkt[idx++] = (Dfu_CRC_Val >> 24) & 0xff;

    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, idx + 4);
}

static unsigned char fw_sc_upgrade_image_switch(unsigned char sn) {
#if (MY_DFU_TRACE_ENABLE)
    DBG_DIRECT("image switch1....,%d\r\n", sc_dfu_wait_reboot);
#endif

    if (sc_dfu_wait_reboot)
        return 1;

    sc_dfu_wait_reboot = 1;

    bool check_result = false;
    check_result = dfu_checksum(IMG_MCUAPP, 0);

#if (MY_DFU_TRACE_ENABLE)
    DBG_DIRECT("check_result=%d", check_result);
#endif

    if (check_result == false)
        return 1;

    T_IMG_HEADER_FORMAT *p_header = NULL;

    if (!is_ota_support_bank_switch()) {
        uint32_t base_addr = 0;
        base_addr = flash_nor_get_bank_addr(FLASH_OTA_TMP);
        p_header = (T_IMG_HEADER_FORMAT *)(base_addr);
        dfu_set_ready(p_header);

        void keyboad_led_set_onoff(uint8_t led_state);
        void zmk_rgb_sleep(void);
        keyboad_led_set_onoff(0);
#ifdef CONFIG_LED_STRIP
        zmk_rgb_sleep();
#endif
#if (MY_DFU_TRACE_ENABLE)
        DBG_DIRECT("dfu reboot !!!");
#endif
        platform_delay_ms(10);
        dfu_fw_reboot(RESET_ALL, DFU_ACTIVE_RESET);

        return 0;
    }

    return 1;
}

static void fw_sc_upgrade_build_info(unsigned char sn) {
    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;
    unsigned char *str_buf = &sc_dfu_rsp_buf[9];
    unsigned char offset = 0;

    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));

    ackhdr->rsp_cmd = TX__OTA_REPORT; // rsp type
    ackhdr->ack_sn = sn;
    ackhdr->ack_cmd = APP_CMD_DFU_BUILD_INFO;
    ackhdr->ack_status = 0;

    memcpy(str_buf, __DATE__, sizeof(__DATE__));
    str_buf[sizeof(__DATE__) - 1] = ' ';
    offset += sizeof(__DATE__);
    memcpy(str_buf + offset, __TIME__, sizeof(__TIME__));
    offset += sizeof(__TIME__);
    str_buf[offset - 1] = 0;

    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, offset + 4);
}

static void fw_sc_upgrade_get_uuid(unsigned char sn) {}

/*

*/
void my_scdfu_data_handle(unsigned char *pdata, unsigned char rxlen) {
    unsigned short hdr = 0;
    unsigned char len = pdata[2];
    unsigned char len_n = (~pdata[3]);
    unsigned char sn = pdata[4];

    hdr = pdata[1];
    hdr |= pdata[0] << 8;

    if ((hdr == SC_FWU_HEADER) || (hdr == SC_FWU_HEADER_ACK)) {
        if ((len == len_n) && (len > 2)) {
            unsigned short sum = 0;
            unsigned short sum_rx = 0;
            unsigned char len_of_dfu = len - 2;
            unsigned char op_code = pdata[5];

            for (unsigned char i = 0; i < len_of_dfu; i++)
                sum += pdata[5 + i];

            sum_rx = pdata[5 + len_of_dfu];
            sum_rx |= (pdata[5 + len_of_dfu + 1] << 8);

            if (sum_rx == sum) {
#if (MY_DFU_TRACE_ENABLE)
#if (MY_DFU_DIRECT_TRACE)
                DBG_DIRECT("fw_sc_upgrade_wr,len=%d,op_code=%02x,sum_rx=%04x,sum_cal=%04x,\r\n",
                           len_of_dfu, op_code, sum_rx, sum);
#else
                APP_PRINT_INFO4(
                    "fw_sc_upgrade_wr,len=%d,op_code=%02x,sum_rx=%04x,sum_cal=%04x,\r\n",
                    len_of_dfu, op_code, sum_rx, sum);
#endif
#endif
                switch (op_code) {
                case SCFWU_OPCODE_GET_MODEL_INFO:
                    fw_sc_upgrade_rsp_model_info(sn);
                    break;
                case SCFWU_OPCODE_GET_DFU_VERSION:
                    fw_sc_upgrade_rsp_dfu_version(sn);
                    break;
#if (MY_DFU_CHIP_ID == CHIP_ID_REALTEK)
                case SCFWU_OPCODE_RTL_PATCH_VERSION:
                    fw_sc_upgrade_rsp_rtk_patch_version(sn);
                    break;
#endif
                case SCFWU_OPCODE_SET_SC_METHOED: {
                    fw_sc_upgrade_set_security_level(sn, pdata[6]);
                } break;
                case SCFWU_OPCODE_START:
                    if (hdr == SC_FWU_HEADER_ACK) {
                        if (fw_sc_upgrade_over_myhid_start() == 0) {
#ifdef CONFIG_ENABLE_FLASH_PROTECT
                            uint8_t bp_lv = 0;
                            FLASH_NOR_RET_TYPE rc =
                                flash_nor_get_bp_lv_locked(FLASH_NOR_IDX_SPIC0, &bp_lv);
                            if (rc == FLASH_NOR_RET_SUCCESS && bp_lv != 0) {
                                APP_PRINT_INFO1("clear bp level=%d", bp_lv);
                                flash_nor_set_bp_lv_locked(FLASH_NOR_IDX_SPIC0, 0);
                            }
#endif
                            fw_sc_upgrade_ack_success(sn, SCFWU_OPCODE_START);
                            app_wdt_stop();
                        } else
                            fw_sc_upgrade_ack_fail(sn, SCFWU_OPCODE_START, 0x01);
                    }
                    break;
                case SCFWU_OPCODE_SEND_BIN:
                    fw_sc_upgrade_write_bin_handle(sn, pdata + 6, len - 3);
                    break;
                case SCFWU_OPCODE_VERIFY_CRC32:
                    dfu_commit_temp_buff_to_flash();
                    fw_sc_upgrade_verify_crc(sn, pdata + 6, len - 3);
                    break;
                case SCFWU_OPCODE_IMAGE_SWITCH:
                    fw_sc_upgrade_image_switch(sn);
                    break;
                case APP_CMD_DFU_BUILD_INFO:
                    fw_sc_upgrade_build_info(sn);
                    break;
                case APP_CMD_DFU_GET_CHIP_UUID:
                    fw_sc_upgrade_get_uuid(sn);
                    break;
                // make complier happly!!
                case 0xf2: {
                    unsigned char *ackpkt = &sc_dfu_rsp_buf[9];
                    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;
                    unsigned char *buf = my_dfu_get_prv_data();
                    unsigned char len = my_dfu_get_prv_len();

                    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));

                    ackhdr->rsp_cmd = TX__OTA_REPORT; // rsp type
                    ackhdr->ack_sn = sn;
                    ackhdr->ack_cmd = 0xf2;
                    ackhdr->ack_status = 0;

                    if (len > 50)
                        len = 50;

                    for (unsigned char i = 0; i < len; i++) {
                        ackpkt[i] = buf[i];
                    }

                    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, len + 4);
                } break;
#if DUAL_IMAGE_HDR
                case 0xf3: {
                    unsigned char *ackpkt = &sc_dfu_rsp_buf[9];
                    CMD_RSP_HDR *ackhdr = (CMD_RSP_HDR *)sc_dfu_rsp_buf;
                    unsigned char *buf = my_dfu_get_prv_data_fixes();
                    unsigned char len = my_dfu_get_prv_len_fixes();

                    memset(sc_dfu_rsp_buf, 0, sizeof(sc_dfu_rsp_buf));

                    ackhdr->rsp_cmd = TX__OTA_REPORT; // rsp type
                    ackhdr->ack_sn = sn;
                    ackhdr->ack_cmd = 0xf2;
                    ackhdr->ack_status = 0;

                    if (len > 50)
                        len = 50;

                    for (unsigned char i = 0; i < len; i++) {
                        ackpkt[i] = buf[i];
                    }

                    fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf, len + 4);

                } break;
#endif
                default:;
                    break;
                }
            } else {
                fw_sc_upgrade_ack_fail(sn, op_code, 0x01);
            }
        }
    }
}

unsigned char my_scdfu_is_active(void) { return sc_dfu_enable; }
