#include "display.h"
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7735.h" // 引入 ST7735 驅動的標頭檔
#include "lvgl.h"

static const char *TAG = "DISPLAY";

// 定義您的解析度屬性 (由您提供的參考代碼得知是橫置 160x80)
#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
// 下面先假設與系統原本的 DISPLAY_WIDTH 同步，為 160x80
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 80
#endif

// 將原本自定義的 GPIO 替換為實際硬體腳位
#define PIN_MOSI 47
#define PIN_SCLK 21
#define PIN_CS 41
#define PIN_DC 40
#define PIN_RST 45
#define PIN_BLK 42

// 全局變數
static esp_lcd_panel_handle_t panel_handle = NULL;
uint16_t *frame_buffer = NULL; // UI 畫圖的緩衝區

// 宣告一個給外部取得面板的函數 (如果需要)
esp_lcd_panel_handle_t get_panel_handle(void)
{
    return panel_handle;
}

// LVGL 刷屏完成通知
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

// LVGL 核心刷屏回調 (這裡自帶 Swap 與 偏移量)
static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int offsetx1 = area->x1 + 0;
    int offsety1 = area->y1 + 24;
    int offsetx2 = area->x2 + 0;
    int offsety2 = area->y2 + 24;

    uint32_t size = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    lv_draw_sw_rgb565_swap(px_map, size); // 直接讓 LVGL 幫我們做顏色反轉

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    lv_display_flush_ready(disp);
}

// 啟動 LVGL 引擎綁定
void display_lvgl_init(esp_lcd_panel_io_handle_t io_handle)
{
    lv_init();
    lv_display_t *disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // 分配 LVGL 用的 DMA Buffer
    size_t draw_buffer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color16_t);
    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);

    lv_display_set_buffers(disp, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, display_flush_cb);

    // 註冊中斷，刷屏更順暢
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, disp);
}

// ==========================================
// 1. 初始化顯示器 (使用 esp_lcd)
// ==========================================
void display_init(void)
{
    ESP_LOGI(TAG, "開始使用 ESP_LCD 初始化 ST7735");

    // 1. 初始化背光引腳 (先關閉)
    gpio_config_t bk_gpio = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_BLK};
    gpio_config(&bk_gpio);
    gpio_set_level(PIN_BLK, 0);

    // 2. 初始化 SPI 總線 (並開啟 DMA)
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 3. 綁定 IO (DC, CS)
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    // 4. 安裝 ST7735 面板驅動 (完全照抄成功版本: BGR)
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7735(io_handle, &panel_config, &panel_handle));

    // 5. 初始化指令 (完全照抄成功版本，不加額外的鏡像或反轉)
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    display_lvgl_init(io_handle);
    // 開啟背光亮起
    gpio_set_level(PIN_BLK, 0);
}

void display_set_brightness(uint8_t brightness)
{
    if (brightness > 100)
        brightness = 100;

    // PWM 亮度控制（0-255）
    // uint8_t pwm_value = (brightness * 255) / 100;

    // 這裡應該使用 PWM，目前設置為最大亮度
    gpio_set_level(PIN_LCD_BL, 1);

    ESP_LOGD(TAG, "背光亮度設置為: %d%%", brightness);
}