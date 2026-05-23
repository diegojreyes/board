/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */
/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include <zmk/board.h>

typedef enum {
	SINGLE_TONE_EVT_OPENED,     /* single tone open completed */
	SINGLE_TONE_EVT_CLOSED,     /* single tone close completed */
	SINGLE_TONE_EVT_DATA_IND,   /* single tone rx data indicated */
	SINGLE_TONE_EVT_DATA_XMIT,  /* single tone tx data transmitted */
	SINGLE_TONE_EVT_ERROR,      /* single tone error occurred */
} T_SINGLE_TONE_EVT;

#pragma pack(1)
typedef struct {
	uint8_t pkt_type;
	uint16_t opcode;
	uint8_t length;
	uint8_t moduleID;
	uint8_t subcmd;
	uint8_t start;
	uint8_t channle;
	uint8_t power_type;
	uint8_t tx_power;
} T_SINGLE_TONE_VEND_CMD_PARAMS;
#pragma pack()

typedef bool (*P_SINGLE_TONE_CALLBACK)(T_SINGLE_TONE_EVT evt, bool status, uint8_t *p_buf,
                                       uint32_t len);

/* export functions */
void single_tone_init(void);
//T_GAP_CAUSE single_tone(uint8_t isStart, uint8_t channel, uint8_t power);
void mp_test_init(void);
int mp_test_command_handler(uint8_t *data, int len);
void vhci_init(void);

/* _BEE_SINGLETONE_H_ */