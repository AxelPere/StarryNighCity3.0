/*============================================================================
  ST7796S 4-wire SPI LCD Driver for Raspberry Pi Pico 2W (RP2350)
  ----------------------------------------------------------------------------
  Pin connections (Pico 2W -> ST7796 breakout)

      GPIO 17  ->  CS    -- Chip-select   (active-LOW, manual toggle)
      GPIO 20  ->  DC    -- Data/Command (1=data, 0=cmd)
      GPIO 21  ->  RST   -- Hardware reset (active-LOW)
      GPIO 19  ->  MOSI  -- SPI0 TX
      GPIO 16  <-  MISO  -- SPI0 RX (only needed if reading GRAM, unused here)
      GPIO 18  ->  SCK   -- SPI0 clock
      GPIO 22  ->  LED   -- Back-light (PWM capable pin)
      VBUS      ->  VCC   -- Panel supply
      GND      ->  GND   -- Common ground
============================================================================*/

#ifndef ST7796_PICO_H
#define ST7796_PICO_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"

/*============================  DISPLAY PARAMS ============================*/
#define TFT_WIDTH    480
#define TFT_HEIGHT   320

/*============================  COLOR MACROS  =============================*/
/* Pack 5-6-5 RGB into a uint16_t.  Input range: R 0-31, G 0-63, B 0-31 */
#define RGB565(b,g,r)  ( ((r) << 11) | ((g) << 5) | (b) )
#define COLOR_BLACK    RGB565(0,0,0)
#define COLOR_WHITE    RGB565(31,63,31)
#define COLOR_RED      RGB565(31,0,0)
#define COLOR_GREEN    RGB565(0,63,0)
#define COLOR_BLUE     RGB565(0,0,31)
#define COLOR_YELLOW    RGB565(31, 63, 0)
#define COLOR_GRID_LINE RGB565(4, 8, 12) // Subtle dark cyan/blue grid lines
#define COLOR_WATER RGB565(4, 15, 35) // Deep dark night water

#define COLOR_NIGHT_SKY  RGB565(0, 0, 4)  // dark blue
#define TRANSPARENT    0xFFFF

/*============================ PIN ASSIGNMENTS ===========================*/
#define TFT_SPI_PORT  spi0
#define TFT_PIN_CS    17
#define TFT_PIN_DC    20
#define TFT_PIN_RST   21
#define TFT_PIN_MOSI  19
#define TFT_PIN_MISO  16   /* unused but reserved, do not reassign elsewhere */
#define TFT_PIN_SCK   18
#define TFT_PIN_LED   22

#define TFT_SPI_BAUDRATE  (65 * 1000 * 1000)  /* 60 MHz, ST7796 supports up to ~62.5MHz?*/

/* Single-purpose helpers, mirroring the original CS_LOW()/CS_HIGH() naming */
#define CS_LOW()      gpio_put(TFT_PIN_CS, 0)
#define CS_HIGH()     gpio_put(TFT_PIN_CS, 1)
#define DC_CMD()      gpio_put(TFT_PIN_DC, 0)
#define DC_DATA()     gpio_put(TFT_PIN_DC, 1)
#define RST_LOW()     gpio_put(TFT_PIN_RST, 0)
#define RST_HIGH()    gpio_put(TFT_PIN_RST, 1)

/*============================  FRAMEBUFFER ===============================*/
/* 480 * 320 * 2 bytes = 307,200 bytes (~300KB). Lives in normal SRAM. */
static uint16_t framebuffer[TFT_WIDTH * TFT_HEIGHT];

static inline void fb_set(int x, int y, uint16_t color)
{
    if ((unsigned)x < TFT_WIDTH && (unsigned)y < TFT_HEIGHT) {
        framebuffer[y * TFT_WIDTH + x] = (uint16_t)((color >> 8) | (color << 8)); /* swap to big-endian for the panel */
    }
}

static inline void fill_screen(uint16_t color)
{
    uint16_t swapped = (uint16_t)((color >> 8) | (color << 8));
    for (int i = 0; i < TFT_WIDTH * TFT_HEIGHT; ++i) {
        framebuffer[i] = swapped;
    }
}

/*============================  LOW-LEVEL SPI WRITE PRIMS =================*/
static inline void tft_write_cmd(uint8_t c)
{
    DC_CMD();
    CS_LOW();
    spi_write_blocking(TFT_SPI_PORT, &c, 1);
    CS_HIGH();
}

static inline void tft_write_u8(uint8_t d)
{
    DC_DATA();
    CS_LOW();
    spi_write_blocking(TFT_SPI_PORT, &d, 1);
    CS_HIGH();
}

static inline void tft_write_u16(uint16_t d)
{
    uint8_t buf[2] = { (uint8_t)(d >> 8), (uint8_t)(d & 0xFF) };
    DC_DATA();
    CS_LOW();
    spi_write_blocking(TFT_SPI_PORT, buf, 2);
    CS_HIGH();
}

/*============================  ADDRESS WINDOW ============================*/
static inline void tft_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // Column Address Set
    DC_CMD();  CS_LOW(); sleep_us(1);
    uint8_t cmd_a = 0x2A;
    spi_write_blocking(TFT_SPI_PORT, &cmd_a, 1);
    DC_DATA(); sleep_us(1);
    uint8_t caset[4] = { (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF), (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF) };
    spi_write_blocking(TFT_SPI_PORT, caset, 4);
    CS_HIGH(); sleep_us(2);

    // Row Address Set
    DC_CMD();  CS_LOW(); sleep_us(1);
    uint8_t cmd_b = 0x2B;
    spi_write_blocking(TFT_SPI_PORT, &cmd_b, 1);
    DC_DATA(); sleep_us(1);
    uint8_t raset[4] = { (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF), (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF) };
    spi_write_blocking(TFT_SPI_PORT, raset, 4);
    CS_HIGH(); sleep_us(2);

    // Memory Write Command
    DC_CMD();  CS_LOW(); sleep_us(1);
    uint8_t cmd_c = 0x2C;
    spi_write_blocking(TFT_SPI_PORT, &cmd_c, 1);
    sleep_us(1);
    CS_HIGH(); sleep_us(2);
}

/*============================  PRESENT (full-frame blit) =================*/
/* Pushes the entire framebuffer to the panel in one continuous SPI burst.
   This replaces the AVR version's per-call immediate streaming -- here we
   draw everything into RAM first, then present it all at once per frame. */

static inline void tft_present(void)
{
    tft_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);

    DC_DATA();
    CS_LOW();
    sleep_us(2);
    // Write full 307.2 KB buffer
    spi_write_blocking(TFT_SPI_PORT, (const uint8_t *)framebuffer, sizeof(framebuffer));
    sleep_us(2);
    CS_HIGH();
    sleep_us(10); // Hold time before next frame
}

/*============================  BACKLIGHT (PWM) ============================*/
static inline void tft_set_backlight(uint8_t level /* 0-255 */)
{
    pwm_set_gpio_level(TFT_PIN_LED, (uint16_t)level << 8);
}

/*============================  POWER-UP SEQUENCE ==========================*/
static inline void tft_init(void)
{
    /* GPIO setup for control pins */
    gpio_init(TFT_PIN_CS);  gpio_set_dir(TFT_PIN_CS, GPIO_OUT);  CS_HIGH();
    gpio_init(TFT_PIN_DC);  gpio_set_dir(TFT_PIN_DC, GPIO_OUT);
    gpio_init(TFT_PIN_RST); gpio_set_dir(TFT_PIN_RST, GPIO_OUT); RST_HIGH();

    /* SPI setup */
    spi_init(TFT_SPI_PORT, TFT_SPI_BAUDRATE);
    spi_set_format(TFT_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(TFT_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(TFT_PIN_SCK,  GPIO_FUNC_SPI);
    /* MISO not used for this driver, left unconfigured intentionally */

    /* Backlight PWM setup */
    gpio_set_function(TFT_PIN_LED, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(TFT_PIN_LED);
    pwm_set_wrap(slice, 65535);
    pwm_set_enabled(slice, true);
    tft_set_backlight(255); /* full brightness */

    /* hardware reset >= 10us, mirrors original timing */
    RST_LOW();  sleep_ms(20);
    RST_HIGH(); sleep_ms(120);

    tft_write_cmd(0x01); sleep_ms(150);   /* SWRESET */
    tft_write_cmd(0x11); sleep_ms(120);   /* SLPOUT  */

    tft_write_cmd(0x3A); tft_write_u8(0x55);  /* COLMOD = 16-bit */

    static const uint8_t madctl[] = { 0x00, 0x60, 0xC0, 0xA0 }; /* 0,90,180,270 deg */
    tft_write_cmd(0x36);                      /* MADCTL */
    tft_write_u8(madctl[1]);                  /* 90 deg rotation */

    tft_write_cmd(0xB6); tft_write_u8(0x0A); tft_write_u8(0x82);
    tft_write_cmd(0x20);                      /* INVOFF */

    tft_write_cmd(0x29); sleep_ms(20);        /* DISPON */
}

/*============================  Drawing Tools ==========================*/
void draw_rect(int x, int y, int w, int h, uint16_t color){
    if (w == 0 || h == 0) return; // nothing to draw

    int x2 = x + w - 1;
    int y2 = y + h - 1;  // "+h" still means "further from origin", just now that's upward

    for(int i = x; i <= x2; i++){
        fb_set(i, y, color);
        fb_set(i, y2, color);
    }
    for(int j = y; j <= y2; j++){
        fb_set(x, j, color);
        fb_set(x2, j, color);
    }
}

void draw_rect_filled(int x, int y, int w, int h, uint16_t color){
    if (w == 0 || h == 0) return; // nothing to draw

    int x2 = x + w - 1;
    int y2 = y + h - 1;

    for(int i = x; i <= x2; i++){
        for(int j = y; j <= y2; j++){
            fb_set(i, j, color);
        }
    }
}

void draw_line(int x0, int y0, int x1, int y1, uint16_t color){
    if(x0 > x1) {
        // swap points to ensure x0 <= x1
        int temp = x0; x0 = x1; x1 = temp;
        temp = y0; y0 = y1; y1 = temp;
    }

    if(x0 == x1) { // vertical line
        if (y0 > y1) { int temp = y0; y0 = y1; y1 = temp; }
        for(int j = y0; j <= y1; j++){
            fb_set(x0, j, color);
        }
        return;
    }

    if(abs(y1 - y0) > abs(x1 - x0)) { // steep slope, iterate over y
        if (y0 > y1) { // swap points to ensure y0 <= y1
            int temp = x0; x0 = x1; x1 = temp;
            temp = y0; y0 = y1; y1 = temp;
        }
        for(int j = y0; j <= y1; j++){
            int i = x0 + (x1 - x0) * (j - y0) / (y1 - y0); // linear interpolation
            fb_set(i, j, color);
        }
        return;
    }

    for(int i = x0; i <= x1; i++){
        int y = y0 + (y1 - y0) * (i - x0) / (x1 - x0); // linear interpolation
        fb_set(i, y, color);
    }
}

#endif /* ST7796_PICO_H */
