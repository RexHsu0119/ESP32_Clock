#include "display.h"

#include <string.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7735.h"
#include "lvgl.h"

static const char *TAG = "DISPLAY";

/* 顯示控制器設定 */
#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

/* ST7735 160x80 常見偏移 */
#define LCD_X_OFFSET 0
#define LCD_Y_OFFSET 24

/* 背光極性：
 * 依您目前板子的實際行為，0 看起來是亮、1 是關
 * 若之後發現相反，只要交換這兩個值即可
 */
#define LCD_BL_ON_LEVEL 1
#define LCD_BL_OFF_LEVEL 0

/* 全域面板 handle */
static esp_lcd_panel_handle_t panel_handle = NULL;

/* 若外部需要取得 panel handle */
esp_lcd_panel_handle_t get_panel_handle(void)
{
    return panel_handle;
}

/* SPI/LCD 傳輸完成後通知 LVGL flush 完成 */
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

/* LVGL flush callback
 * 注意：這裡不要再呼叫 lv_display_flush_ready()
 * 因為我們已經在 on_color_trans_done callback 裡通知了
 */
static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;

    int offset_x1 = area->x1 + LCD_X_OFFSET;
    int offset_y1 = area->y1 + LCD_Y_OFFSET;
    int offset_x2 = area->x2 + LCD_X_OFFSET;
    int offset_y2 = area->y2 + LCD_Y_OFFSET;

    uint32_t pixel_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

    /* ST7735 RGB565 資料常需要 swap */
    lv_draw_sw_rgb565_swap(px_map, pixel_count);

    esp_lcd_panel_draw_bitmap(panel_handle,
                              offset_x1,
                              offset_y1,
                              offset_x2 + 1,
                              offset_y2 + 1,
                              px_map);
}

/* 初始化 LVGL 與 display driver 綁定 */
void display_lvgl_init(esp_lcd_panel_io_handle_t io_handle)
{
    lv_init();

    lv_display_t *disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (disp == NULL)
    {
        ESP_LOGE(TAG, "建立 LVGL display 失敗");
        return;
    }

    /* 配置雙 buffer */
    size_t draw_buffer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color16_t);

    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);

    if (buf1 == NULL || buf2 == NULL)
    {
        ESP_LOGE(TAG, "配置 LVGL DMA buffer 失敗");
        return;
    }

    lv_display_set_buffers(disp, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, display_flush_cb);

    /* 註冊 SPI 傳輸完成 callback，由 callback 通知 LVGL flush ready */
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, disp));
}

/* 初始化顯示器 */
void display_init(void)
{
    ESP_LOGI(TAG, "開始使用 ESP_LCD 初始化 ST7735");

    /* 1. 初始化背光腳位，先關閉 */
    gpio_config_t bk_gpio = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio));
    gpio_set_level(PIN_LCD_BL, LCD_BL_OFF_LEVEL);

    /* 2. 初始化 SPI bus */
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SPI_SCLK,
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* 3. 建立 panel IO */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_SPI_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    /* 4. 建立 ST7735 panel driver */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7735(io_handle, &panel_config, &panel_handle));

    /* 5. 初始化面板 */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* 6. 綁定 LVGL */
    display_lvgl_init(io_handle);

    /* 7. 開啟背光 */
    gpio_set_level(PIN_LCD_BL, LCD_BL_ON_LEVEL);

    ESP_LOGI(TAG, "顯示模組初始化完成");
}

void display_set_brightness(uint8_t brightness)
{
    if (brightness > 100)
    {
        brightness = 100;
    }

    /* 目前尚未實作 PWM，先用開/關處理 */
    if (brightness == 0)
    {
        gpio_set_level(PIN_LCD_BL, LCD_BL_OFF_LEVEL);
    }
    else
    {
        gpio_set_level(PIN_LCD_BL, LCD_BL_ON_LEVEL);
    }

    ESP_LOGD(TAG, "背光亮度設置為: %d%%", brightness);
}