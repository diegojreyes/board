/*
    MIT License

    Copyright (c) 2017-2019, Alexey Dynda

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include "oled_ssd1306.h"
#include "lcd_common.h"
#include "ssd1306_commands.h"
#include "../intf/ssd1306_interface.h"
#include "../intf/i2c/ssd1306_i2c.h"
// #include "../intf/spi/ssd1306_spi.h"
#include "../ssd1306_hal/io.h"
#ifdef SDL_EMULATION
#include "sdl_core.h"
#endif

#include <zephyr/kernel.h>

uint8_t s_ssd1306_startLine = 0;

static const uint8_t PROGMEM s_oled128x64_initData[] = {
#ifdef SDL_EMULATION
    SDL_LCD_SSD1306,
    0x00,
#endif
    SSD1306_DISPLAYOFF, // display off
    SSD1306_MEMORYMODE,
    HORIZONTAL_ADDRESSING_MODE,  // Page Addressing mode
    SSD1306_COMSCANDEC,          // Scan from 127 to 0 (Reverse scan)
    SSD1306_SETSTARTLINE | 0x00, // First line to start scanning from
    SSD1306_SETCONTRAST,
    0x7F,                    // contast value to 0x7F according to datasheet
    SSD1306_SEGREMAP | 0x01, // Use reverse mapping. 0x00 - is normal mapping
    SSD1306_NORMALDISPLAY,
    SSD1306_SETMULTIPLEX,
    63, // Reset to default MUX. See datasheet
    SSD1306_SETDISPLAYOFFSET,
    0x00, // no offset
    SSD1306_SETDISPLAYCLOCKDIV,
    0x80, // set to default ratio/osc frequency
    SSD1306_SETPRECHARGE,
    0x22, // switch precharge to 0x22 // 0xF1
    SSD1306_SETCOMPINS,
    0x12, // set divide ratio
    SSD1306_SETVCOMDETECT,
    0x20, // vcom deselect to 0x20 // 0x40
    SSD1306_CHARGEPUMP,
    0x14, // Enable charge pump
    SSD1306_DISPLAYALLON_RESUME,
    SSD1306_DISPLAYON,
};

// static const uint8_t PROGMEM s_oled128x32_initData[] =
// {
// #ifdef SDL_EMULATION
//     SDL_LCD_SSD1306,
//     0x00,
// #endif
//     SSD1306_DISPLAYOFF, // display off  //0xae
//     SSD1306_SETDISPLAYCLOCKDIV, 0x80,   //0xd5,0x80 d
//     SSD1306_SETMULTIPLEX, 31,           //0xa8,0x1f d
//     SSD1306_SETDISPLAYOFFSET, 0x00, // --no offset  //0xd3,0x00
//     SSD1306_SETSTARTLINE | 0x00,   //0x40
//     // SSD1306_CHARGEPUMP, 0x14, // 0x10
//     SSD1306_SEGREMAP | 0x01,  // Reverse mapping  //0xa1
//     SSD1306_COMSCANDEC,   //0xc8
//     SSD1306_SETCOMPINS, 0x02, //0xda ,0x02
//     SSD1306_SETCONTRAST, 0x7F, // contast value  //0x81,0x7f
//     SSD1306_SETPRECHARGE, 0x22, // 0x1F  //0xd9,0x22
//     SSD1306_SETVCOMDETECT, 0x40, //0xdb,0x40
//     SSD1306_MEMORYMODE, HORIZONTAL_ADDRESSING_MODE,//0x20,0x00
//     SSD1306_DISPLAYALLON_RESUME,//0xa4
//     SSD1306_NORMALDISPLAY, //0xa6
//     // SSD1306_DISPLAYON,
// };
#if 1
static const uint8_t PROGMEM s_oled128x32_initData[] = {
    // SSD1306_DISPLAYOFF, // display off
    // SSD1306_SETDISPLAYCLOCKDIV, 0x80,
    // SSD1306_SETMULTIPLEX, 31,
    // // 0xad,0x40, //internal iref;
    // SSD1306_SETDISPLAYOFFSET, 0x10,
    // SSD1306_SETSTARTLINE | 0x00,  //---
    // // SSD1306_CHARGEPUMP, 0x12,
    // SSD1306_SEGREMAP ,  // Reverse mapping
    // SSD1306_COMSCANDEC,
    // SSD1306_SETCOMPINS, 0x02, //---
    // SSD1306_SETCONTRAST, 0x30, // contast value
    // SSD1306_SETPRECHARGE, 0x22, // 0x1F
    // SSD1306_SETVCOMDETECT, 0x30,
    // SSD1306_MEMORYMODE, HORIZONTAL_ADDRESSING_MODE,//--
    // SSD1306_DISPLAYALLON_RESUME,
    // SSD1306_NORMALDISPLAY,
    // SSD1306_DISPLAYON,

    0xae,             /*display off*/
    0xa8, 0x1f,       /*multiplex ratio*/
    0xad, 0x40,       /*External or Internal IREF Selection */
    0xd3, 0x10,       /*set display offset*/
    0xa1,             /*set segment remap*/
    0xc8,             /*Com scan direction*/
    0xa6,             /*normal / reverse*/
    0x40,             /*set display start line*/
    0xa4, 0x81, 0x30, /*contract control*/
    0xd9, 0x22,       /*set pre-charge period*/
    0xd5, 0x80,       /*set osc division*/
    0xda, 0x10,       /*set COM pins*/
    0x20, 0x02,       // 0x05,/*Set Memory Addressing Mode */
    0xdb, 0x30,       /*set vcomh*/
    // 0x8d,0x12,/*set charge pump enable*/
    // 0x00,
    // 0x10,
};
#endif

static uint8_t s_column;
static uint8_t s_page;
static uint8_t s_width;

static void ssd1306_setBlock(lcduint_t x, lcduint_t y, lcduint_t w) {
    s_column = x;
    s_page = y;
    s_width = w;
    ssd1306_intf.start();
    // if (ssd1306_intf.spi)
    //     ssd1306_spiDataMode(0);
    // else
    ssd1306_intf.send(0x00);
#if 0
    ssd1306_intf.send(SSD1306_COLUMNADDR);
    ssd1306_intf.send(x);
    ssd1306_intf.send(w ? (x + w - 1) : (ssd1306_lcd.width - 1));
    ssd1306_intf.send(SSD1306_PAGEADDR);
    ssd1306_intf.send(y);
    ssd1306_intf.send((ssd1306_lcd.height >> 3) - 1);
#else
    ssd1306_intf.send(SSD1306_SETPAGE | y);
    ssd1306_intf.send(((x) >> 4) | SSD1306_SETHIGHCOLUMN);
    ssd1306_intf.send(((x) & 0x0f) | SSD1306_SETLOWCOLUMN);
#endif
    // if (ssd1306_intf.spi)
    // {
    //     ssd1306_spiDataMode(1);
    // }
    // else
    {
        ssd1306_intf.stop();
        ssd1306_intf.start();
        ssd1306_intf.send(0x40);
    }
}

static void ssd1306_nextPage(void) {
    ssd1306_intf.stop();
    ssd1306_setBlock(s_column, s_page + 1, s_width);
}

static void ssd1306_setMode_int(lcd_mode_t mode) {}

void ssd1306_displayOff() { ssd1306_sendCommand(SSD1306_DISPLAYOFF); }

void ssd1306_displayOn() { ssd1306_sendCommand(SSD1306_DISPLAYON); }

void ssd1306_setContrast(uint8_t contrast) {
    ssd1306_commandStart();
    ssd1306_intf.send(SSD1306_SETCONTRAST);
    ssd1306_intf.send(contrast);
    ssd1306_intf.stop();
}

void ssd1306_invertMode() { ssd1306_sendCommand(SSD1306_INVERTDISPLAY); }

void ssd1306_normalMode() { ssd1306_sendCommand(SSD1306_NORMALDISPLAY); }

void ssd1306_flipHorizontal(uint8_t mode) {
    ssd1306_sendCommand(SSD1306_SEGREMAP | (mode ? 0x00 : 0x01));
}

void ssd1306_flipVertical(uint8_t mode) {
    ssd1306_sendCommand(mode ? SSD1306_COMSCANINC : SSD1306_COMSCANDEC);
}

void ssd1306_setStartLine(uint8_t line) {
    s_ssd1306_startLine = line;
    ssd1306_sendCommand(SSD1306_SETSTARTLINE | (line & 0x3F));
}

uint8_t ssd1306_getStartLine(void) { return s_ssd1306_startLine; }

///////////////////////////////////////////////////////////////////////////////
//  I2C SSD1306 128x64
///////////////////////////////////////////////////////////////////////////////

void ssd1306_init() { ssd1306_128x64_i2c_init(); }

void ssd1306_128x64_init() {
    ssd1306_lcd.type = LCD_TYPE_SSD1306;
    ssd1306_lcd.height = 64;
    ssd1306_lcd.width = 128;
    ssd1306_lcd.set_block = ssd1306_setBlock;
    ssd1306_lcd.next_page = ssd1306_nextPage;
    ssd1306_lcd.send_pixels1 = ssd1306_intf.send;
    ssd1306_lcd.send_pixels_buffer1 = ssd1306_intf.send_buffer;
    ssd1306_lcd.set_mode = ssd1306_setMode_int;
    for (uint8_t i = 0; i < sizeof(s_oled128x64_initData); i++) {
        ssd1306_sendCommand(pgm_read_byte(&s_oled128x64_initData[i]));
    }
}

void ssd1306_128x64_i2c_init() {
    ssd1306_i2cInit();
    ssd1306_128x64_init();
}

void ssd1306_128x64_i2c_initEx(int8_t scl, int8_t sda, int8_t sa) {
    ssd1306_i2cInitEx(scl, sda, sa);
    ssd1306_128x64_init();
}

///////////////////////////////////////////////////////////////////////////////
//  SPI SSD1306 128x64
///////////////////////////////////////////////////////////////////////////////

void ssd1306_128x64_spi_init(int8_t rstPin, int8_t cesPin, int8_t dcPin) {
    if (rstPin >= 0) {
        ssd1306_resetController(rstPin, 10);
    }
    // ssd1306_spiInit(cesPin, dcPin);
    ssd1306_128x64_init();
}

///////////////////////////////////////////////////////////////////////////////
//  I2C SSD1306 128x32
///////////////////////////////////////////////////////////////////////////////

void ssd1306_128x32_init() {
    ssd1306_lcd.type = LCD_TYPE_SSD1306;
    ssd1306_lcd.height = 32;
    ssd1306_lcd.width = 128;
    ssd1306_lcd.set_block = ssd1306_setBlock;
    ssd1306_lcd.next_page = ssd1306_nextPage;
    ssd1306_lcd.send_pixels1 = ssd1306_intf.send;
    ssd1306_lcd.set_mode = ssd1306_setMode_int;
    for (uint8_t i = 0; i < sizeof(s_oled128x32_initData); i++) {
        ssd1306_sendCommand(pgm_read_byte(&s_oled128x32_initData[i]));
        // printk("i:%d,write:%x\n",i,s_oled128x32_initData[i]);
        // k_msleep(10);
    }
}

void ssd1306_128x32_i2c_init() {
    ssd1306_i2cInit();
    ssd1306_128x32_init();
}

///////////////////////////////////////////////////////////////////////////////
//  SPI SSD1306 128x32
///////////////////////////////////////////////////////////////////////////////

void ssd1306_128x32_spi_init(int8_t rstPin, int8_t cesPin, int8_t dcPin) {
    if (rstPin >= 0) {
        ssd1306_resetController(rstPin, 10);
    }
    // ssd1306_spiInit(cesPin, dcPin);
    ssd1306_128x32_init();
}
