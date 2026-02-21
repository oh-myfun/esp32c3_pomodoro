idf_component_register(SRCS main.c encoder.c ui_manager.c lvgl_example.c wifi_manager.c 
                        pomodoro_engine.c storage_service.c time_service.c
                        INCLUDE_DIRS "."
                        REQUIRES esp_timer esp_driver_gpio esp_driver_spi esp_wifi esp_netif nvs_flash esp_http_server lwip
)
