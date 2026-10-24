#pragma once

/*
 * Log minimo sobre Serial.
 *
 * Serial es tambien el enlace por cable con el panel. Cada mensaje sale en una
 * sola escritura, asi que no parte una linea de telemetria por la mitad, pero
 * aun asi ensucia el canal: tras arrancar, Log::quiet() deja pasar solo los
 * avisos (failsafe, I2C caido). El panel descarta toda linea que no empiece por '{'.
 *
 * Pon "Core Debug Level" en "None" en el IDE para que los log_x del propio core
 * no se mezclen tampoco.
 */
namespace Log {

void info(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void warn(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/** A partir de aqui solo se emiten los avisos. */
void quiet();

}  // namespace Log
