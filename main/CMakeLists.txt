idf_component_register(SRCS main.c
                          driver/st7789_lcd.c
                          driver/buzzer.c
                          driver/ws2812.c
                          input/input_handler.c
                          ui/ui_manager.c
                          ui/ui_list.c
                          ui/ui_screen_main.c
                          ui/ui_screen_pomodoro.c
                          ui/ui_screen_settings.c
                          ui/ui_screen_wifi.c
                          ui/ui_screen_buddy.c
                          ui/ui_screen_settings_pomodoro.c
                          ui/ui_screen_settings_light.c
                          ui/ui_screen_settings_buddy.c
                          ui/ui_screen_settings_time.c
                          ui/ui_screen_settings_system.c
                          ui/ui_screen_wifi_saved.c
                          service/wifi_service.c
                          pomodoro/pomodoro_engine.c
                          service/storage_service.c
                          service/sound_service.c
                          service/led_service.c
                          service/time_service.c
                          buddy/buddy_chars.c
                          buddy/buddy.c
                          INCLUDE_DIRS "." "ui" "driver" "input" "service" "pomodoro" "buddy"
                          REQUIRES esp_timer driver esp_wifi esp_netif nvs_flash lwip button knob json
                          )
