# Auditoría visual del panel (rover_web_gcs)

## Qué fallaba en la versión anterior

| # | Problema | Por qué importa | Cambio aplicado |
|---|---|---|---|
| 1 | Demasiados colores de relleno (azul, morado, rojo) compitiendo, sin relación con la marca | La vista no sabía dónde mirar | Paleta clara "papel" de [spaceprogram.cl](https://spaceprogram.cl): fondo `#F3F0E8`, texto tinta `#0E1424`, dorado `#E5A958` para la acción principal y el naranja de Robótica `#E2622F` para la marca. Color de estado solo en lo que se mueve: verde avance/ok, dorado retroceso/aviso, rojo peligro |
| 2 | **ARMAR** en rojo, igual que la parada de emergencia | En un momento de nervios se confunden las dos acciones | Armar en dorado; desarmar con contorno verde; el rojo queda solo para la emergencia |
| 3 | El estado de armado era una píldora pequeña en la cabecera | Es lo primero que tiene que ver el operario | Indicador grande "ARMADO / DESARMADO" arriba de la columna de seguridad |
| 4 | Seis etiquetas "0%" iguales en las ruedas | Hay un solo BTS7960: las seis muestran siempre lo mismo | Un medidor semicircular de tracción con aguja (avance verde, retroceso ámbar) |
| 5 | La dirección solo se veía en texto, en la tabla de pines | No se veía cómo estaban giradas las ruedas | Las ruedas del esquema giran con el ángulo real de su servo (4WS) y muestran ±grados |
| 6 | Mapa de pines, tramas JSON en bruto y memoria libre ocupaban la pantalla de operación | Información de depuración, no de conducción | Movidos a la pestaña Diagnóstico |
| 7 | Iconos con emojis | Se ven distintos en cada sistema y no combinan | Iconos SVG del paquete (`js/view/icons.js`), que heredan el color del tema |
| 8 | Textos de 0,65 rem en métricas e insignias | Difíciles de leer en campo | Tamaño mínimo ~0,72 rem, cifras en monoespaciada |
| 9 | Botones deshabilitados casi iguales a los activos | No se distinguía qué se podía pulsar | Deshabilitados en contorno apagado |
| 10 | Por debajo de 1200 px todo pasaba a una columna | En portátiles pequeños obligaba a desplazarse mucho | Dos columnas hasta 720 px; una sola columna en móvil |

## Nuevo: botón ASENTIR (servo 5)

- Está en Operación (tarjeta "Servo 5 · cabeceo") y en Diagnóstico.
- Mueve el canal 4 adelante y atrás y lo deja en el centro: `90 → 115 → 65 → 115 → 65 → 90`.
- Usa `SERVO_SET`, así que no hace falta armar, pero sí tener el mando. No requiere cambios de firmware.
- La parada de emergencia o una desconexión cortan el gesto al instante.
- Ajustes en `js/config.js` → `NOD`: amplitud, repeticiones y pausa. Si cabecea al revés, pon `AMPLITUDE` en negativo.

## Tipografía

- Las mismas fuentes que spaceprogram.cl: **Horizon** para "ROVER 6×6" y **Now** para el resto.
- El panel las busca instaladas en el PC o como archivos en `rover_web_gcs/fonts/` (`Horizon.woff`, `Now-Regular.woff`, `Now-Medium.woff`, `Now-Bold.woff`).
- Si no las encuentra usa Audiowide y Space Grotesk (Google Fonts); sin Internet, la fuente del sistema.

## Elementos visuales

El paquete completo está en `Proyecto_rover_panel/elementos_visuales/`. Los iconos que usa el panel van incrustados en `js/view/icons.js` para que hereden el color y funcionen al abrir el panel desde disco (`file://`).
