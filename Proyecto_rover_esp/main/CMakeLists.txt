idf_component_register(
    SRCS
        "main.c"
        "mcpwm_motors.c"
        "stepper_drv.c"
        "pca9685_servo.c"
        "telemetry_adc.c"
        "rover_link.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio      # driver/gpio.h
        esp_driver_ledc      # driver/ledc.h    - PWM de los 6 motores
        esp_driver_i2c       # driver/i2c_master.h - PCA9685
        esp_driver_uart      # driver/uart.h    - enlace por cable
        esp_adc              # esp_adc/adc_oneshot.h - bateria
        esp_timer
        esp_wifi             # solo con ROVER_LINK_WIRED = 0
        esp_netif
        esp_http_server
        nvs_flash
)
