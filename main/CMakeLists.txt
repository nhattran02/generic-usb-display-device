idf_component_register(
    SRCS
        main.c
        board.c
        lcd.c
        touch.c
        screen.c
        settings.c
        usb_glue.c
        gud/gud.c
        gud/gud_driver.c
        gud/gud_lz4.c
        ui/ui_port.c
        ui/ui_platform_esp.c
        ui/ui_app.c
        ui/ui_pages.c
        ui/ui_monitor.c
        ui/ui_wave.c
        ui/ui_vitals.c
    INCLUDE_DIRS . gud ui
    REQUIRES esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_ledc esp_timer esp_hw_support
    PRIV_REQUIRES tinyusb esp_psram nvs_flash esp_driver_tsens esp_app_format
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format -Wno-unused-function)

# idf.py -DBOARD_LCD_ROTATION=0 -DBOARD_LCD_SPI_HZ=40000000 build  (see board.h)
foreach(opt BOARD_LCD_ROTATION BOARD_LCD_SPI_HZ)
    if(DEFINED ${opt})
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${opt}=${${opt}})
    endif()
endforeach()
