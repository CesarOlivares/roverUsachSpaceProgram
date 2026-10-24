#pragma once

#include "../model/BaseModel.h"
#include "../transport/RadioTx.h"

/*
 * Puente panel -> rover. Cada linea JSON del panel se decodifica, se codifica
 * en binario y sale por radio en el acto.
 *
 * La base NO repite ni encola ordenes por su cuenta: solo reenvia lo que llega
 * del panel. Si el panel se cierra o se desenchufa el USB, la base deja de
 * transmitir y el failsafe del rover corta en 400 ms. Una orden que no llega
 * al rover se descarta; nunca sale mas tarde.
 */
class BridgeController {
public:
    BridgeController(BaseModel &model, RadioTx &radio) : model_(model), radio_(radio) {}

    /** Una linea completa recibida del panel. */
    void onPanelLine(const char *line, uint32_t nowMs);

    /** Una linea que no cupo en el buffer. */
    void onPanelOverflow() { model_.notePanelError(); }

    /** Llamar en cada vuelta de loop(): informa del estado de la base. */
    void tick(uint32_t nowMs);

private:
    BaseModel &model_;
    RadioTx   &radio_;
    uint32_t   lastStatusMs_ = 0;
};
