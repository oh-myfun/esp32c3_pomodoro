idf_component_register(SRCS main.c encoder.c ui_manager.c lvgl_example.c
                        INCLUDE_DIRS "."
                        REQUIRES esp_timer esp_driver_gpio
)
