#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

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
 * @brief 讓 LCD 進入顯示關閉狀態
 */
void display_sleep(void);

/**
 * @brief 喚醒 LCD 顯示
 */
void display_wake(void);

/**
 * @brief 進入 Deep Sleep 前準備顯示器
 *        會關閉 LCD 顯示、關閉背光，並保持背光腳位狀態
 */
void display_prepare_for_sleep(void);

/**
 * @brief 從 Deep Sleep 喚醒後恢復顯示器相關 GPIO 狀態
 *        若 display 尚未初始化，至少會先解除背光腳位 hold
 */
void display_resume_from_sleep(void);

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