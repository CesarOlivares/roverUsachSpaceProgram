#pragma once

#include <stdint.h>
#include "../config/BaseConfig.h"
#include "../common/Telemetry.h"
#include "../common/RadioProtocol.h"

/*
 * Estado de la estacion base: la ultima telemetria conocida del rover
 * (reconstruida a partir de los paquetes de movimiento y de estado), la
 * calidad del enlace radio y el panel que esta al otro lado del USB.
 */
class BaseModel {
public:
    BaseModel();

    /* --- Panel --- */
    void        notePanelClient(const char *client);
    const char *panelClient() const { return panelClient_; }
    void        notePanelError() { pcErrors_++; }

    /* --- Radio --- */
    void recordSend(bool delivered, uint32_t nowMs);
    bool roverReachable(uint32_t nowMs) const;
    int  linkQuality() const;                 // % de envios con ACK en la ventana
    uint32_t sentOk() const   { return sentOk_; }
    uint32_t sentFail() const { return sentFail_; }

    /** Vuelca un paquete de telemetria del rover. false si no es valido. */
    bool applyTelemetry(const uint8_t *buf, uint8_t len);

    /**
     * Telemetria lista para el panel. El rover solo dice "el mando lo tiene la
     * radio"; la base lo traduce al id del panel conectado, que es quien de
     * verdad tiene el mando.
     */
    TelemetryData telemetryForPanel() const;

private:
    char             panelClient_[proto::kClientIdLen];
    TelemetryData    rover_;
    radio::OwnerCode owner_;
    bool             haveTelemetry_;

    uint32_t pcErrors_;
    uint32_t sentOk_;
    uint32_t sentFail_;
    uint32_t lastAckMs_;
    bool     everAcked_;
    bool     window_[cfg::kLqWindow];
    int      windowPos_;
    int      windowFill_;
};
