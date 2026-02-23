idf_component_register(SRCS main.c 
                        encoder.c 
                        driver/st7789_lcd.c 
                        driver/buzzer.c 
                        input/input_handler.c
                        ui/ui_manager.c 
                        ui/ui_list.c
                        ui/ui_screen_main.c 
                        ui/ui_screen_pomodoro.c 
                        ui/ui_screen_test.c
                        ui/ui_screen_settings.c 
                        ui/ui_screen_wifi.c
                        ui/ui_screen_chat.c 
                        ui/ui_screen_settings_pomodoro.c
                        lvgl_example.c 
                        wifi_manager.c 
                        pomodoro_engine.c 
                        storage_service.c 
                        time_service.c
                        INCLUDE_DIRS "." "ui" "driver" "input"
                        REQUIRES esp_timer esp_driver_gpio esp_driver_spi esp_driver_ledc esp_wifi esp_netif nvs_flash esp_http_server lwip
)
