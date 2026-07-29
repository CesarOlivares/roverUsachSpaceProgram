# La interfaz web vive en el proyecto del panel: esa carpeta es la unica fuente
# de verdad. EMBED_FILES la incrusta en el binario, asi que un solo
# `idf.py flash` deja la UI servida desde la ESP, sin particion de archivos.
# Editar cualquiera de los tres ficheros fuerza la recompilacion.
set(ROVER_WWW "${CMAKE_CURRENT_LIST_DIR}/../../Proyecto_rover_panel/rover_web_gcs")

idf_component_register(
    SRCS
        "main.c"
        "mcpwm_motors.c"
        "stepper_drv.c"
        "pca9685_servo.c"
        "rover_link.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        # Los simbolos salen del nombre del fichero:
        # index.html -> _binary_index_html_start / _end
        "${ROVER_WWW}/index.html"
        "${ROVER_WWW}/app.js"
        "${ROVER_WWW}/style.css"
    REQUIRES
        esp_driver_gpio      # driver/gpio.h
        esp_driver_ledc      # driver/ledc.h       - PWM de los 6 motores
        esp_driver_i2c       # driver/i2c_master.h - PCA9685
        esp_driver_uart      # driver/uart.h       - enlace por cable
        esp_timer
        esp_wifi             # addon web: SoftAP
        esp_netif
        esp_http_server      # addon web: UI + WebSocket
        nvs_flash
)
