#include "RadioLink.h"

#include "../common/RadioProtocol.h"
#include "../hal/Pinout.h"
#include "../view/TelemetryView.h"
#include "../util/Log.h"

static rf24_datarate_e dataRate()
{
    switch (radio::kDataRateKbps) {
    case 2000: return RF24_2MBPS;
    case 1000: return RF24_1MBPS;
    default:   return RF24_250KBPS;
    }
}

bool RadioLink::begin()
{
    spi_.begin(pins::kNrfSck, pins::kNrfMiso, pins::kNrfMosi, pins::kNrfCsn);

    if (!radio_.begin(&spi_, pins::kNrfCe, pins::kNrfCsn) || !radio_.isChipConnected()) {
        Log::warn("RADIO", "El nRF24L01 no responde por SPI. Revisa cableado y 3,3 V. Solo queda el cable.");
        return false;
    }

    radio_.setChannel(radio::kChannel);
    radio_.setDataRate(dataRate());
    radio_.setPALevel(radio::kPaLevel);
    radio_.setCRCLength(RF24_CRC_16);
    radio_.setAutoAck(true);
    radio_.enableDynamicPayloads();
    radio_.enableAckPayload();
    radio_.openReadingPipe(1, radio::kRoverAddress);
    radio_.startListening();

    // La primera orden ya se lleva telemetria de vuelta.
    loadAckPayload();

    if (xTaskCreatePinnedToCore(task, "link_radio", 4096, this, 6, nullptr, 0) != pdPASS) {
        return false;
    }

    Log::info("RADIO", "nRF24L01 escuchando en el canal %u (%u kbps).",
              (unsigned)radio::kChannel, (unsigned)radio::kDataRateKbps);
    return true;
}

void RadioLink::task(void *arg) { static_cast<RadioLink *>(arg)->loop(); }

void RadioLink::loop()
{
    uint8_t buf[radio::kPayloadMax];

    while (true) {
        uint8_t pipe;
        if (!radio_.available(&pipe)) {
            // 1 ms de sondeo: la FIFO RX del nRF24 guarda 3 paquetes y la base
            // manda uno cada ~50 ms.
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        uint8_t len = radio_.getDynamicPayloadSize();
        if (len == 0 || len > radio::kPayloadMax) {
            // Longitud corrupta: el datasheet manda vaciar la FIFO.
            radio_.flush_rx();
            ctrl_.reportInvalidFrame();
            continue;
        }
        radio_.read(buf, len);

        Command cmd;
        if (radio::decodeCommand(buf, len, cmd)) {
            ctrl_.handle(cmd);
        } else {
            ctrl_.reportInvalidFrame();
        }

        loadAckPayload();
    }
}

void RadioLink::loadAckPayload()
{
    uint8_t buf[radio::kPayloadMax];
    size_t  len = TelemetryView::renderRadio(ctrl_.snapshot(), nextIsMotion_, buf, sizeof(buf));
    nextIsMotion_ = !nextIsMotion_;
    if (len == 0) return;

    // Se descarta la telemetria que no llego a salir: la base siempre recibe
    // la mas reciente, nunca una cola de estados viejos.
    radio_.flush_tx();
    radio_.writeAckPayload(1, buf, (uint8_t)len);
}
