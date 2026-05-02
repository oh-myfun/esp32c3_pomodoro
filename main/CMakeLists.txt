idf_component_register(SRCS main.c 
                          driver/st7789_lcd.c 
                          driver/buzzer.c
                          input/input_handler.c
                          ui/ui_manager.c 
                          ui/ui_list.c
                          ui/ui_screen_main.c 
                          ui/ui_screen_pomodoro.c 
                          ui/ui_screen_settings.c 
                          ui/ui_screen_wifi.c
                          ui/ui_screen_chat.c 
                          ui/ui_screen_settings_pomodoro.c
                          network/wifi_manager.c 
                          pomodoro/pomodoro_engine.c 
                          storage/storage_service.c 
                          time/time_service.c
                          INCLUDE_DIRS "." "ui" "driver" "input" "network" "storage" "time" "pomodoro"
                          REQUIRES esp_timer driver esp_wifi esp_netif nvs_flash esp_http_server lwip button knob
                          )
