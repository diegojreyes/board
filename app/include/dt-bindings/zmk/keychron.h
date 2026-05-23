/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */


#define BT_PAIR 	0
#define BT_SEL 		1
#define PPT_PAIR 	2
#define PPT_SEL 	3

#define BAT_CHG 	4
#define BAT_CHGD 	5
#define BAT_INFO	6  //fn+b

#define MOD_USB 	7
#define MOD_BLE 	8
#define MOD_PPT 	9

#define LAYER_MAC 	10
#define LAYER_WIN 	11

#define RECOVER 	12  //fn+j+z
#define FN_EXG		13  //fn+x+l

#define MOD_NKRO    14
#define RGB_TEST    15
#define FN_WIN      16
#define MOD_WIRELESS 17
#define SWITCH_LAYER   18

#define RCV 		RECOVER 0 
#define BATCHG 		BAT_CHG 0 
#define BATCHGD 	BAT_CHGD 0 
#define BATINFO 	BAT_INFO 0 

#define SEL_USB 	MOD_USB 0
#define SEL_BLE 	MOD_BLE 0
#define SEL_PPT 	MOD_PPT 0
#define NKRO        MOD_NKRO 0
#define RGBTEST     RGB_TEST 0

#define MO_MAC_LAYER   SWITCH_LAYER 0
#define MO_WIN_LAYER   SWITCH_LAYER 1

#define FN_WIN_L    FN_WIN 0
#define FN_WIN_S    FN_WIN 1
#define SEL_WIRELESS MOD_WIRELESS 0
