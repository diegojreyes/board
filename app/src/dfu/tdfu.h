#ifndef __MY_DFU_H__
#define __MY_DFU_H__

#define MY_DFU_TRACE_ENABLE 0
#define MY_DFU_DIRECT_TRACE 1

#define MY_DFU_WATCH_DOG_TIMEOUT_MS 90000

#define ENABLE_DFU_CHIP_ID_CMD 1

#define MY_DFU_RPT_INPUT_ID 0xb1
#define MY_DFU_RPT_OUTPUT_ID 0xb2

enum {
    SC_FWU_HEADER = 0xaa55,
    SC_FWU_HEADER_ACK = 0xaa56,
};

enum {
    SCFWU_OPCODE_GET_MODEL_INFO = 0x60,
    SCFWU_OPCODE_GET_DFU_VERSION = 0x61,
    SCFWU_OPCODE_SET_SC_METHOED = 0x62,
    SCFWU_OPCODE_START = 0x63,
    SCFWU_OPCODE_SEND_BIN = 0x64,
    SCFWU_OPCODE_VERIFY_CRC32 = 0x65,
    SCFWU_OPCODE_IMAGE_SWITCH = 0x66,
    APP_CMD_DFU_GET_CHIP_UUID = 0x6d,
    SCFWU_OPCODE_RTL_PATCH_VERSION = 0x6e,
    APP_CMD_DFU_BUILD_INFO = 0x6f,
};

void my_scdfu_init(void);
unsigned char my_scdfu_is_active(void);
void my_scdfu_data_handle(unsigned char *pdata, unsigned char rxlen);
unsigned char usb_send_user_if_data(unsigned char report_id, unsigned char *data,
                                    unsigned short len);
#if 1
#define FW_SC_UPGRADE_HID_REPORT_SEND(id, pdata, len) usb_send_user_if_data(id, pdata, len)
#else
#define FW_SC_UPGRADE_HID_REPORT_SEND(id, pdata, len)
#endif

#endif
