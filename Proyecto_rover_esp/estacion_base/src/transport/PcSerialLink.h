#pragma once

#include <stddef.h>
#include "../config/BaseConfig.h"
#include "../controller/BridgeController.h"

/*
 * USB con el PC: lineas JSON del panel (Web Serial) a 115200 8N1. Corta en
 * lineas y se las pasa al puente. Sin tareas: se atiende desde loop().
 */
class PcSerialLink {
public:
    explicit PcSerialLink(BridgeController &bridge) : bridge_(bridge) {}

    /** Abre el puerto. Va lo primero en setup(). */
    static void openPort();

    /** Consume lo que haya llegado. Llamar en cada vuelta de loop(). */
    void poll(uint32_t nowMs);

private:
    BridgeController &bridge_;
    char   line_[cfg::kMaxLineLen];
    size_t len_      = 0;
    bool   overflow_ = false;
};
