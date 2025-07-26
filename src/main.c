#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app, CONFIG_APP_LOG_LEVEL);

#include "app_uart.h"

/* RX packets buffer */
static uint8_t serial_cmd_buf[256];

/* RX FSM states */
enum protocol_state {
    S_DATA_RECEIVED, // normal data state
    S_CR_RECEIVED, // received '\r' last time
};

static void bytes_to_packet(uint8_t byte)
{
    static uint32_t len = 0;
    static enum protocol_state state = S_DATA_RECEIVED; 

    if (len >= sizeof(serial_cmd_buf)) {
        LOG_WRN("Serial command buffer overflow, resetting");
        // reset
        len = 0;
        state = S_DATA_RECEIVED;
        return;
    }

    serial_cmd_buf[len++] = byte;

    switch(state) {

    case S_DATA_RECEIVED:
        if ('\r' == byte) {
            state = S_CR_RECEIVED;  
        }
        break;


    case S_CR_RECEIVED:
    {   
        if ('\n' == byte) {
            LOG_HEXDUMP_INF(serial_cmd_buf, len, "Received packets:");

            // loopback
            int err = app_uart_tx(serial_cmd_buf, len);
            if (err) {
                LOG_ERR("Failed to send loopback data: %d", err);
            }

            // clear
            len = 0;
            state = S_DATA_RECEIVED;

        } else {
            LOG_WRN("Received \\r, but no \\n after!!!");
            // reset
            len = 0;
            state = S_DATA_RECEIVED;
        }
        break;
    }

    default:
        LOG_ERR("Unknown state!");
        // reset state to prevent infinite error logging
        len = 0;
        state = S_DATA_RECEIVED;
        break;
    }
}

static void uart_callback(uint8_t *byte, size_t len)
{
    if (byte == NULL || len == 0) {
        LOG_WRN("Invalid callback parameters");
        return;
    }
    
    // received are byte streams, we need to transform them into packets
    for (size_t i = 0; i < len; i++) {
        bytes_to_packet(byte[i]);
    }
}

int main()
{
    int err;
    
    LOG_INF("Starting UART application");
        
    err = app_uart_rx_cb_register(uart_callback);
    if (err) {
        LOG_ERR("Failed to register RX callback: %d", err);
        return err;
    }
    
    LOG_INF("UART application initialized successfully");
    k_sleep(K_FOREVER);
    return 0;
}