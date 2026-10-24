#include "RadioTx.h"

#include "../config/BaseConfig.h"
#include "../common/RadioProtocol.h"

static rf24_datarate_e dataRate()
{
    switch (radio::kDataRateKbps) {
    case 2000: return RF24_2MBPS;
    case 1000: return RF24_1MBPS;
    default:   return RF24_250KBPS;
    }
}

bool RadioTx::begin()
{
    SPI.begin(pins::kNrfSck, pins::kNrfMiso, pins::kNrfMosi, pins::kNrfCsn);

    if (!radio_.begin(&SPI, pins::kNrfCe, pins::kNrfCsn) || !radio_.isChipConnected()) {
        return false;
    }

    radio_.setChannel(radio::kChannel);
    radio_.setDataRate(dataRate());
    radio_.setPALevel(RF24_PA_HIGH);
    radio_.setCRCLength(RF24_CRC_16);
    radio_.setAutoAck(true);
    radio_.enableDynamicPayloads();
    radio_.enableAckPayload();
    radio_.setRetries(radio::kRetryDelay, radio::kRetryCount);
    radio_.openWritingPipe(radio::kRoverAddress);
    radio_.stopListening();
    return true;
}

bool RadioTx::send(const uint8_t *buf, uint8_t len, uint8_t *ack, uint8_t &ackLen)
{
    ackLen = 0;

    // write() bloquea hasta el ACK o hasta agotar los reintentos (~10 ms como
    // mucho); si falla, el propio RF24 vacia la FIFO: nada se queda pendiente
    // para salir mas tarde con una orden ya caducada.
    if (!radio_.write(buf, len)) return false;

    if (radio_.available()) {
        uint8_t n = radio_.getDynamicPayloadSize();
        if (n > 0 && n <= radio::kPayloadMax) {
            radio_.read(ack, n);
            ackLen = n;
        } else {
            radio_.flush_rx();
        }
    }
    return true;
}
