#ifndef __APP_UART_H
#define __APP_UART_H

#include <stddef.h>
#include <stdint.h> 

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*packets_cb_t)(uint8_t *byte, size_t len);

/**
 * @brief Register callback function for received packets
 * @param cb Callback function pointer
 * @return 0 on success, negative error code on failure
 */
int app_uart_rx_cb_register(packets_cb_t cb);

/**
 * @brief Send data via UART
 * @param byte Pointer to data buffer
 * @param len Length of data to send
 * @return 0 on success, negative error code on failure
 */
int app_uart_tx(const uint8_t *byte, size_t len);

#ifdef __cplusplus
}
#endif

#endif //__APP_UART_H