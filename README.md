# Async UART example for Zephyr

[中文说明](https://github.com/Jayant-Tang/learning_zephyr_serial/blob/master/README-CN.md)

This is a UART communication example project based on Zephyr RTOS, implementing serial data reception, parsing, and loopback transmission functionality.

## Features

- Asynchronous UART data transmission and reception
- CRLF protocol parsing (packets ending with `\r\n`)
- Data loopback functionality
- Complete error handling and memory management
- Low power switch

## Hardware Support

- nRF52840DK
- nRF54L15DK

## Project Structure

```
src/
├── main.c              # Main application
├── app_uart/
│   ├── app_uart.c      # UART driver wrapper
│   └── app_uart.h      # UART API interface
└── ...
```

## UART Usage Guide

### 1. Include Header Files
```c
#include "app_uart.h"
```

### 2. Register Data Reception Callback in Main Function
```c
static void uart_callback(uint8_t *byte, size_t len)
{
    // Process received data
    for (size_t i = 0; i < len; i++) {
        bytes_to_packet(byte[i]);  // Protocol parsing
    }
}

int main()
{
    int err;
    
    LOG_INF("Starting UART application");
    
    // Register data reception callback function
    err = app_uart_rx_cb_register(uart_callback);
    if (err) {
        LOG_ERR("Failed to register RX callback: %d", err);
        return err;
    }
    
    LOG_INF("UART application initialized successfully");
    k_sleep(K_FOREVER);
    return 0;
}
```

### 3. Send Data
```c
uint8_t data[] = "Hello World\r\n";
int err = app_uart_tx(data, sizeof(data));
if (err) {
    LOG_ERR("Send failed: %d", err);
}
```

## Protocol Packet Parsing

The project implements a simple CRLF protocol:
- Data ending with `\r\n` represents a complete packet
- Uses FSM to parse byte streams
- Automatically sends loopback after receiving complete packets

## Build and Run

1. Build the project:
```bash
west build -p -d build -b nrf52840dk/nrf52840
```

2. Flash firmware:
```bash
west flash
```

3. Connect to the device using a serial tool with baud rate 115200

4. Send data ending with `\r\n` and observe the loopback effect

5. UART low-power sleep test:
   **nRF54L15DK:**

   - Press button0 to enter sleep mode
   - Press button1 to exit sleep mode

   **nRF52840DK:**

   - Press button1 to enter sleep mode
   - Press button2 to exit sleep mode

## Important Notes

### Peripheral Pin Cross-Domain Assignment

When GPIO and peripherals on the nRF54L15 are located in different power domains, it is essential to strictly follow the [pin assignment table specified in the manual](https://docs.nordicsemi.com/bundle/ps_nrf54L15/page/chapters/pin.html#d380e188) for pin allocation. Additionally, during the time when peripherals are in use, it is necessary to enable the [CPU's constant latency mode](https://docs.nordicsemi.com/bundle/ps_nrf54L15/page/pmu.html#ariaid-title3).

## Blog Reference

- [Zephyr Driver and Device Tree Practice - UART (Chinese)](https://www.cnblogs.com/jayant97/articles/17828907.html)

  
