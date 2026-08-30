#ifndef DIAG_H
#define DIAG_H

#include <Arduino.h>
#include "pinout.h"

/*
 * Caja negra de diagnostico.
 *
 * El problema a cazar es un corte de failsafe: 400 ms sin tramas validas. Para
 * saber POR QUE no llegaron hace falta ver los milisegundos previos, y a 20 Hz
 * no se puede imprimir cada trama sin ahogar el puerto. Asi que todo se anota
 * en un anillo en RAM (barato: una escritura de 12 bytes) y solo se imprime
 * cuando algo va mal.
 *
 * La pregunta que esto contesta:
 *
 *   silencio_ws    ms desde el ultimo BYTE recibido por WebSocket
 *   silencio_trama ms desde la ultima ORDEN valida despachada
 *
 *   - los dos altos          -> el transporte se corto (WiFi / cliente caido)
 *   - ws bajo, trama alto    -> los bytes llegan pero se rechazan (parseo,
 *                               arbitraje de mando, linea desbordada)
 *
 * Poner ROVER_DIAG a 0 lo compila fuera entero.
 */
#define ROVER_DIAG 1

// Umbrales para anotar un evento "lento" en el anillo.
#define DIAG_STALL_US     50000    // una vuelta de loop() por encima de 50 ms
#define DIAG_I2C_US       20000    // una escritura de servo por encima de 20 ms
#define DIAG_RING         48       // eventos guardados (48 * 12 B = 576 B)

enum : uint8_t {
  D_BOOT = 0, D_WSCONN, D_WSDISC, D_WSERR, D_WSDROP, D_WSBYTES,
  D_RXOK, D_RXBAD, D_ARM, D_DISARM, D_STOP, D_DRIVE, D_SERVO, D_TEST, D_HB,
  D_NOCTRL, D_FAILSAFE, D_STALL, D_I2CSLOW, D_TLMSLOW, D_PCARST, D_SERRX,
  D_MUTE, D_LINKOK,
};

static const char *const DIAG_NAME[] = {
  "BOOT", "WS+", "WS-", "WSERR", "WSDROP", "WSBYTES",
  "RX", "RXBAD", "ARM", "DISARM", "STOP", "DRIVE", "SERVO", "TEST", "HB",
  "NOCTRL", "FAILSAFE", "STALL", "I2CSLOW", "TLMSLOW", "PCARST", "SER",
  "MUDO", "ENLACE_OK",
};

struct DiagEv {
  uint32_t ms;
  int32_t  val;
  uint8_t  code;
};

static DiagEv   g_diag[DIAG_RING];
static uint8_t  g_diag_head  = 0;
static uint32_t g_diag_total = 0;

/*
 * Marcadores de tiempo y maximos. Los escriben tanto loop() como la tarea de
 * AsyncTCP; son de 32 bits, asi que cada escritura suelta es atomica en el S3 y
 * para un diagnostico no hace falta mas rigor.
 */
static volatile uint32_t g_ws_last_data_ms = 0;   // ultimo byte recibido por WS
static volatile uint32_t g_ws_bytes        = 0;   // bytes WS acumulados
static volatile uint32_t g_lines_ok        = 0;   // lineas despachadas con exito
static volatile uint32_t g_lines_noctrl    = 0;   // rechazadas por arbitraje de mando
static uint32_t g_gap_max_ms  = 0;                // mayor hueco entre tramas validas
static uint32_t g_loop_max_us = 0;
static uint32_t g_i2c_max_us  = 0;
static uint32_t g_tlm_max_us  = 0;
static char     g_last_act[16]  = {0};            // ultima accion aceptada
static char     g_last_bad[72]  = {0};            // ultima linea rechazada, cruda

#if ROVER_DIAG

static inline void diagEv(uint8_t code, int32_t val) {
  DiagEv &e = g_diag[g_diag_head];
  e.ms   = millis();
  e.val  = val;
  e.code = code;
  g_diag_head = (uint8_t)((g_diag_head + 1) % DIAG_RING);
  g_diag_total++;
}

static inline void diagMax(uint32_t &destino, uint32_t valor) {
  if (valor > destino) destino = valor;
}

// Guarda la linea cruda que no se pudo usar. Se sanea: va dentro de un log.
static inline void diagSaveBad(const char *linea) {
  size_t n = 0;
  for (const char *p = linea; *p && n < sizeof(g_last_bad) - 1; p++) {
    g_last_bad[n++] = (*p >= 32 && *p < 127) ? *p : '.';
  }
  g_last_bad[n] = '\0';
}

static inline void diagSaveAct(const char *act) {
  size_t n = 0;
  for (const char *p = act; *p && n < sizeof(g_last_act) - 1; p++) g_last_act[n++] = *p;
  g_last_act[n] = '\0';
}

#else
static inline void diagEv(uint8_t, int32_t) {}
static inline void diagMax(uint32_t &, uint32_t) {}
static inline void diagSaveBad(const char *) {}
static inline void diagSaveAct(const char *) {}
#endif // ROVER_DIAG

#endif // DIAG_H
