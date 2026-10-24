/*
 * Estacion base del Rover 6x6 - ESP32 conectada al PC por USB
 *
 * Puente entre el panel de control y el rover:
 *
 *   panel (navegador) --USB, lineas JSON--> esta ESP32 --nRF24L01, binario--> rover
 *   panel             <--telemetria JSON--  esta ESP32 <--ACK con telemetria--
 *
 * El panel habla exactamente el mismo protocolo que si estuviera enchufado al
 * rover por cable, asi que no sabe (ni le importa) que hay una radio en medio.
 *
 * Estructura (MVC):
 *   src/model/       BaseModel: ultima telemetria del rover y calidad del enlace
 *   src/view/        PcView: lo que se le manda al panel
 *   src/controller/  BridgeController: reenvia ordenes y recoge telemetria
 *   src/transport/   PcSerialLink (USB) y RadioTx (nRF24L01)
 *   src/common/      protocolo compartido con el rover (COPIA: se edita en
 *                    Proyecto_rover_esp/common y se sincroniza con tools/sync_common.ps1)
 *
 * Librerias: RF24 de TMRh20 (Gestor de librerias). Ajustes del IDE: ../README.md.
 */

#include "src/config/BaseConfig.h"
#include "src/model/BaseModel.h"
#include "src/transport/RadioTx.h"
#include "src/controller/BridgeController.h"
#include "src/transport/PcSerialLink.h"

static BaseModel        model;
static RadioTx          radioTx;
static BridgeController bridge(model, radioTx);
static PcSerialLink     pcLink(bridge);

void setup()
{
    PcSerialLink::openPort();

    // Las lineas que no empiezan por '{' el panel las trata como log.
    Serial.println("I (BASE) Estacion base del Rover 6x6 arrancando...");

    if (!radioTx.begin()) {
        // Sin radio no hay nada que puentear: se avisa en bucle para que se vea
        // en cualquier monitor serie que se abra despues.
        while (true) {
            Serial.println("W (BASE) El nRF24L01 no responde por SPI. Revisa cableado y 3,3 V.");
            delay(2000);
        }
    }

    Serial.println("I (BASE) Radio lista. Esperando ordenes del panel.");
}

void loop()
{
    uint32_t now = millis();
    pcLink.poll(now);
    bridge.tick(now);
    delay(1);
}
