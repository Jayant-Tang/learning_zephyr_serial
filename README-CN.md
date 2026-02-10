# Zephyr 异步串口例程

这是一个基于 Zephyr RTOS 的异步串口通信示例工程，实现了串口数据的接收、解析和回环发送功能。

## 功能概述

- 串口异步收发数据
- CRLF协议解析 (以`\r\n`结尾的数据包)
- 数据回环 (loopback) 功能
- 完整的错误处理和内存管理
- RX/TX 线程与消息队列
- 低功耗休眠/唤醒（按键）
- 通过异步串口 API 操作 USB CDC ACM

## 硬件支持

- nRF52DK (nRF52832)
- nRF52833DK
- nRF52840DK
- nRF5340DK (CPUAPP / CPUAPP NS)
- nRF54L15DK
- nRF54LM20DK (nRF54LM20A)
- nRF9151DK (NS)
- nRF9160DK (NS)

## 主要文件结构

```
src/
├── main.c              # 主应用程序
├── app_uart/
│   ├── app_uart.c      # 串口驱动封装
│   └── app_uart.h      # 串口API接口
├── app_usb/
│   ├── app_usb.c       # USB CDC ACM 初始化
│   ├── app_usb_callback.c # USB SMF 状态机
│   └── app_usb.h       # USB API接口
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

### UART 模式（默认）

1. 构建工程：
```bash
west build -p -d build -b nrf52840dk/nrf52840
```

2. 烧录固件：
```bash
west flash -d build
```

3. 使用串口工具连接设备，波特率115200

4. 发送以`\r\n`结尾的数据，观察回环效果

5. 串口低功耗休眠测试：

    - 按 button1 进入休眠
    - 按 button2 退出休眠

### USB CDC ACM 模式（可选）

1. 构建工程：
   使用 `prj_usb.conf` + `usb.overlay` 启用 USB CDC ACM 与异步适配器。
   
   ```bash
   # nRF52840DK 
   west build -p -d build_usb -b nrf52840dk/nrf52840 -- -DCONF_FILE="prj_usb.conf" -DDTC_OVERLAY_FILE="usb.overlay"
   
   # nRF52833DK
   west build -p -d build_usb_52833 -b nrf52833dk/nrf52833 -- -DCONF_FILE="prj_usb.conf" -DDTC_OVERLAY_FILE="usb.overlay"
   
   # nRF5340DK CPUAPP without TFM
   west build -p -d build_usb_5340 -b nrf5340dk/nrf5340/cpuapp -- -DCONF_FILE="prj_usb.conf" -DDTC_OVERLAY_FILE="usb.overlay"
   
   # nRF54LM20DK nRF54LM20A
   west build -p -d build_usb_54lm20 -b nrf54lm20dk/nrf54lm20a/cpuapp -- -DCONF_FILE="prj_usb.conf" -DDTC_OVERLAY_FILE="usb.overlay"
2. 烧录固件
   ```
   west flash -d build_usb
   ```

3. 使用串口工具连接设备 "Async Serial"，波特率任意

4. 发送以`\r\n`结尾的数据，观察回环效果

5. 拔出USB，设备进入低功耗；插入USB，设备退出低功耗。

   - Button 操作在 USB 模式下无效。

### USB 状态机

USB 通过状态机管理。CONNECTED 是父状态并包含子状态，DISCONNECTED 为低功耗状态。
Zephyr SMF 先执行子状态回调；如果子状态返回 `SMF_EVENT_HANDLED`，父状态回调不会再执行。

```mermaid
stateDiagram-v2
    DISCONNECTED --> CONNECTED: 插入
    CONNECTED --> DISCONNECTED: 拔出

    state CONNECTED {
        [*] --> CONFIGURED: 枚举完成
        CONFIGURED --> SUSPENDED: 挂起
        SUSPENDED --> CONFIGURED: 恢复
        CONFIGURED --> [*]: 复位/取消配置
        SUSPENDED --> [*]: 复位
    }
```

## 注意事项

### 外设引脚跨域分配

nRF54L系列，PERI Power Domain中的外设（如UART20/21/22）想使用GPIO Port 2时，属于跨域使用，需遵循以下2个条件

1. 一定要严格按照[手册中的引脚分配表格指定引脚](https://docs.nordicsemi.com/bundle/ps_nrf54L15/page/chapters/pin.html#d380e188)进行分配
2. 并且，在使用外设的时间内，还需要开启[CPU的constant latency mode](https://docs.nordicsemi.com/bundle/ps_nrf54L15/page/pmu.html#ariaid-title3)：
   - 开启`CONFIG_NRF_SYS_EVENT=y`
   - 在外设操作前后调用`nrf_sys_event_release_global_constlat()` 和 `nrf_sys_event_release_global_constlat()`.

文档：https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/app_dev/device_guides/nrf54l/pinmap.html

## 博客

- [Zephyr驱动与设备树实战——串口](https://www.cnblogs.com/jayant97/articles/17828907.html)

  
