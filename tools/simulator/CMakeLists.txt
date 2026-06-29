cmake_minimum_required(VERSION 3.16)
project(pomodoro_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(LVGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../managed_components/lvgl__lvgl")
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Glob LVGL sources (re-run cmake if files change)
file(GLOB_RECURSE LVGL_SRCS "${LVGL_DIR}/src/*.c")

add_executable(sim
    main.c
    fake/fake_services.c

    # UI layer
    ${PROJECT_ROOT}/main/ui/ui_helpers.c
    ${PROJECT_ROOT}/main/ui/ui_manager.c
    ${PROJECT_ROOT}/main/ui/ui_list.c
    ${PROJECT_ROOT}/main/ui/i18n.c
    ${PROJECT_ROOT}/main/ui/custom_font_14.c
    ${PROJECT_ROOT}/main/ui/custom_font_16.c
    ${PROJECT_ROOT}/main/ui/ui_screen_main.c
    ${PROJECT_ROOT}/main/ui/ui_screen_pomodoro.c
    ${PROJECT_ROOT}/main/ui/ui_screen_buddy.c
    ${PROJECT_ROOT}/main/ui/ui_screen_sensor.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_system.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_pomodoro.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_alarm.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_buddy.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_time.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_light.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_sound.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_sensor.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_debug.c
    ${PROJECT_ROOT}/main/ui/ui_screen_wifi.c
    ${PROJECT_ROOT}/main/ui/ui_screen_wifi_saved.c
    ${PROJECT_ROOT}/main/ui/ui_screen_bridge_scan.c
    ${PROJECT_ROOT}/main/ui/ui_screen_pressure_info.c
    ${PROJECT_ROOT}/main/ui/ui_text_input.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_light_demo.c
    ${PROJECT_ROOT}/main/ui/ui_screen_settings_sound_demo.c

    # Buddy rendering
    ${PROJECT_ROOT}/main/buddy/buddy_render.c
    ${PROJECT_ROOT}/main/buddy/buddy_chars.c
    ${PROJECT_ROOT}/main/buddy/buddies/capybara.c
    ${PROJECT_ROOT}/main/buddy/buddies/duck.c
    ${PROJECT_ROOT}/main/buddy/buddies/goose.c
    ${PROJECT_ROOT}/main/buddy/buddies/blob.c
    ${PROJECT_ROOT}/main/buddy/buddies/cat.c
    ${PROJECT_ROOT}/main/buddy/buddies/dragon.c
    ${PROJECT_ROOT}/main/buddy/buddies/octopus.c
    ${PROJECT_ROOT}/main/buddy/buddies/owl.c
    ${PROJECT_ROOT}/main/buddy/buddies/penguin.c
    ${PROJECT_ROOT}/main/buddy/buddies/turtle.c
    ${PROJECT_ROOT}/main/buddy/buddies/snail.c
    ${PROJECT_ROOT}/main/buddy/buddies/ghost.c
    ${PROJECT_ROOT}/main/buddy/buddies/axolotl.c
    ${PROJECT_ROOT}/main/buddy/buddies/cactus.c
    ${PROJECT_ROOT}/main/buddy/buddies/robot.c
    ${PROJECT_ROOT}/main/buddy/buddies/rabbit.c
    ${PROJECT_ROOT}/main/buddy/buddies/mushroom.c
    ${PROJECT_ROOT}/main/buddy/buddies/chonk.c

    # Pomodoro engine
    ${PROJECT_ROOT}/main/pomodoro/pomodoro_engine.c

    ${LVGL_SRCS}
)

target_include_directories(sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_ROOT}/main
    ${PROJECT_ROOT}/main/ui
    ${PROJECT_ROOT}/main/buddy
    ${PROJECT_ROOT}/main/service
    ${PROJECT_ROOT}/main/pomodoro
    ${PROJECT_ROOT}/main/input
    ${PROJECT_ROOT}/main/driver
    ${LVGL_DIR}
    ${LVGL_DIR}/src
)

target_compile_options(sim PRIVATE
    -O2
    -include "${CMAKE_CURRENT_SOURCE_DIR}/time_compat.h"
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-format
    -Wno-incompatible-pointer-types
    -Wno-int-conversion
    -Wno-discarded-qualifiers
    -Wno-implicit-function-declaration
    -Wno-builtin-declaration-mismatch
    -Wno-attributes
    -fno-strict-aliasing
)

target_compile_definitions(sim PRIVATE
    LV_CONF_INCLUDE_SIMPLE=1
)

if(WIN32)
    target_compile_definitions(sim PRIVATE _USE_MATH_DEFINES)
endif()
