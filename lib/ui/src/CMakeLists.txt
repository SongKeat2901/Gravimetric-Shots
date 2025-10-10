SET(SOURCES screens/ui_MainScreen.c
    screens/ui_SettingScreen.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    images/ui_img_flush_png.c
    images/ui_img_coffee_png.c
    images/ui_img_cancel_png.c
    images/ui_img_timer_png.c
    images/ui_img_refresh_png.c
    images/ui_img_weight_png.c
    images/ui_img_arrow_png.c
    images/ui_img_config_png.c
    images/ui_img_bluetooth_png.c
    images/ui_img_light_png.c
    images/ui_img_return_png.c)

add_library(ui ${SOURCES})
