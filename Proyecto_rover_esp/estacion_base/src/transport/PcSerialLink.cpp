#include "PcSerialLink.h"

#include <Arduino.h>

void PcSerialLink::openPort()
{
    Serial.setRxBufferSize(2048);
    Serial.setTxBufferSize(2048);
    Serial.begin(cfg::kSerialBaud);
}

void PcSerialLink::poll(uint32_t nowMs)
{
    uint8_t chunk[128];

    int avail;
    while ((avail = Serial.available()) > 0) {
        size_t n = Serial.read(chunk, avail < (int)sizeof(chunk) ? (size_t)avail : sizeof(chunk));

        for (size_t i = 0; i < n; i++) {
            char c = (char)chunk[i];

            if (c == '\n' || c == '\r') {
                if (overflow_) {
                    // Linea truncada: se descarta entera en vez de reenviar basura.
                    bridge_.onPanelOverflow();
                    overflow_ = false;
                } else if (len_ > 0) {
                    line_[len_] = '\0';
                    bridge_.onPanelLine(line_, nowMs);
                }
                len_ = 0;
                continue;
            }

            if (len_ < sizeof(line_) - 1) {
                line_[len_++] = c;
            } else {
                overflow_ = true;
                len_ = 0;
            }
        }
    }
}
