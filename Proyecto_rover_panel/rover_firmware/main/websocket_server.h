#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WS_SERVER_PORT 8080
#define FAILSAFE_TIMEOUT_MS 400

esp_err_t websocket_server_init(void);
bool websocket_is_armed(void);
void websocket_set_armed(bool armed);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_SERVER_H
