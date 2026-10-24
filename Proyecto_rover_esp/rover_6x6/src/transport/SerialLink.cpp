#include "SerialLink.h"

#include <Arduino.h>
#include "Dispatch.h"
#include "../view/TelemetryView.h"
#include "../util/Log.h"

#define SERIAL_RX_BUF_LEN  2048
#define SERIAL_TX_BUF_LEN  2048

void SerialLink::openPort()
{
    /*
     * El driver vacia el FIFO por interrupcion hacia un anillo de 2 KB. Con el
     * buffer por defecto y una lectura lenta se perdian bytes en mitad de una
     * linea a 115200 baudios: de ahi el desfase y el congelamiento de antes.
     */
    Serial.setRxBufferSize(SERIAL_RX_BUF_LEN);
    Serial.setTxBufferSize(SERIAL_TX_BUF_LEN);
    Serial.begin(cfg::kSerialBaud);
}

bool SerialLink::begin()
{
    Log::info("LINK", "Enlace por cable listo a %u baudios, %u Hz de telemetria.",
              (unsigned)cfg::kSerialBaud, (unsigned)cfg::kTelemetryHz);

    bool ok = xTaskCreatePinnedToCore(rxTask, "link_serial_rx", 4096, this, 6, nullptr, 0) == pdPASS;
    ok = ok && xTaskCreatePinnedToCore(telemetryTask, "link_serial_tlm", 4096, this, 4, nullptr, 0) == pdPASS;
    return ok;
}

void SerialLink::rxTask(void *arg)        { static_cast<SerialLink *>(arg)->rxLoop(); }
void SerialLink::telemetryTask(void *arg) { static_cast<SerialLink *>(arg)->telemetryLoop(); }

void SerialLink::rxLoop()
{
    static char    line[cfg::kMaxLineLen];
    static uint8_t chunk[128];
    size_t line_len = 0;
    bool   overflow = false;

    while (true) {
        int avail = Serial.available();
        if (avail <= 0) {
            // El anillo de 2 KB aguanta de sobra 1 ms de trafico.
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        size_t n = Serial.read(chunk, avail < (int)sizeof(chunk) ? (size_t)avail : sizeof(chunk));

        for (size_t i = 0; i < n; i++) {
            char c = (char)chunk[i];

            if (c == '\n' || c == '\r') {
                if (overflow) {
                    // Linea truncada: se descarta entera en vez de despachar basura.
                    ctrl_.reportInvalidFrame();
                    overflow = false;
                } else if (line_len > 0) {
                    line[line_len] = '\0';
                    dispatchLine(ctrl_, line);
                }
                line_len = 0;
                continue;
            }

            if (line_len < sizeof(line) - 1) {
                line[line_len++] = c;
            } else {
                // Marca desbordamiento y sigue consumiendo hasta el fin de linea,
                // para resincronizar en la siguiente trama.
                overflow = true;
                line_len = 0;
            }
        }
    }
}

void SerialLink::telemetryLoop()
{
    static char buf[cfg::kTelemetryBufLen];
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        size_t len = TelemetryView::render(ctrl_.snapshot(), 0, buf, sizeof(buf));
        // Una sola escritura por linea: el log no puede colarse en medio.
        if (len > 0) Serial.write((const uint8_t *)buf, len);

        // Cadencia fija: vTaskDelayUntil no acumula la deriva del tiempo de trabajo.
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(cfg::kTelemetryPeriodMs));
    }
}
