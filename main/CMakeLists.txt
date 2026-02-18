idf_component_register(SRCS main.c lvgl_example.c
                        INCLUDE_DIRS "."
                        REQUIRES esp_timer esp_driver_gpio esp_driver_spi esp_lcd esp_lcd_st7789
)
