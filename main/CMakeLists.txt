idf_component_register(
    SRCS
        main.c
        board.c
        lcd.c
        touch.c
        screen.c
        ui.c
        usb_glue.c
        gud/gud.c
        gud/gud_driver.c
    INCLUDE_DIRS . gud
    REQUIRES esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_ledc esp_timer esp_hw_support
    PRIV_REQUIRES tinyusb esp_psram nvs_flash
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format -Wno-unused-function)

# idf.py -DBOARD_LCD_ROTATION=0 build  (see BOARD_LCD_ROTATION in board.h)
if(DEFINED BOARD_LCD_ROTATION)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_LCD_ROTATION=${BOARD_LCD_ROTATION})
endif()
