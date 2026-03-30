idf_component_register(SRCS "display.c"
                            "drivers/lcd/esp_lcd_st7735.c"
                       INCLUDE_DIRS "."
                                    "drivers/lcd"
                       REQUIRES esp_lcd driver lvgl)