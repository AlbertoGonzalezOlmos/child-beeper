/* Host-only stub: lets the pure-host test build include public headers that
 * declare esp_err_t return types (cb_button.h). IDF's real esp_err.h is not
 * host-compilable; nothing in tests/host/stubs is ever compiled into the
 * firmware. */
#ifndef ESP_ERR_H
#define ESP_ERR_H

typedef int esp_err_t;

#endif /* ESP_ERR_H */
