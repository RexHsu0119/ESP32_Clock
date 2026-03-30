#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_lcd_panel_io.h"

// ST7735S 顯示解析度
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 80

// SPI 引腳定義
#define PIN_SPI_MOSI 47
#define PIN_SPI_SCLK 21
#define PIN_SPI_CS 41
#define PIN_LCD_DC 40
#define PIN_LCD_RST 45
#define PIN_LCD_BL 42

/**
 * @brief 初始化顯示模組
 */
void display_init(void);

void display_lvgl_init(esp_lcd_panel_io_handle_t io_handle);

/**
 * @brief 設置亮度
 * @param brightness 亮度值（0-100）
 */
void display_set_brightness(uint8_t brightness);

/**
 * @brief 刷新顯示
 */
// void display_refresh(void);

// 常用顏色定義（RGB565）
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_YELLOW 0xFFE0
#define COLOR_CYAN 0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY 0x8410
#define COLOR_DARK_GRAY 0x4208

#endif