# Zephyr 异步串口例程

这是一个基于 Zephyr RTOS 的串口通信示例工程，实现了串口数据的接收、解析和回环发送功能。

## 功能概述

- 串口异步收发数据
- CRLF协议解析 (以`\r\n`结尾的数据包)
- 数据回环 (loopback) 功能
- 完整的错误处理和内存管理
- 低功耗开关

## 硬件支持

- nRF52840DK
- nRF54L15DK

## 主要文件结构

```
src/
├── main.c              # 主应用程序
├── app_uart/
│   ├── app_uart.c      # 串口驱动封装
│   └── app_uart.h      # 串口API接口
└── ...
```

## 串口使用说明

### 1. 包含头文件
```c
#include "app_uart.h"
```

### 2. 在main函数中注册数据接收回调函数
```c
static void uart_callback(uint8_t *byte, size_t len)
{
    // 处理接收到的数据
    for (size_t i = 0; i < len; i++) {
        bytes_to_packet(byte[i]);  // 协议解析
    }
}

int main()
{
    int err;
    
    LOG_INF("Starting UART application");
    
    // 注册数据接收回调函数
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

### 3. 发送数据
```c
uint8_t data[] = "Hello World\r\n";
int err = app_uart_tx(data, sizeof(data));
if (err) {
    LOG_ERR("Send failed: %d", err);
}
```

## 协议包解析

工程实现了简单的CRLF协议：
- 数据以 `\r\n` 结尾表示一个完整数据包
- 使用状态机解析字节流
- 收到完整数据包后自动回环发送

## 编译和运行

1. 构建工程：
```bash
west build -p -d build -b nrf52840dk/nrf52840
```

2. 烧录固件：
```bash
west flash
```

3. 使用串口工具连接设备，波特率115200

4. 发送以`\r\n`结尾的数据，观察回环效果

5. 串口低功耗休眠测试：
   **54L15DK：**

   - 按button0进入休眠
   - 按button1退出休眠

   **52840DK：**

   - 按button1进入休眠
   - 按button2退出休眠

## 博客

- [Zephyr驱动与设备树实战——串口](https://www.cnblogs.com/jayant97/articles/17828907.html)

  
