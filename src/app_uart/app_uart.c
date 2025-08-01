
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/pm/device.h>
#include <string.h>

#include "app_uart.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_uart, CONFIG_APP_UART_LOG_LEVEL);

#define RX_INACTIVE_TIMEOUT_US 1000000

/* serial device */ 
#if (IS_ENABLED(CONFIG_UART_ASYNC_ADAPTER) && IS_ENABLED(CONFIG_USB_CDC_ACM))
/* USB CDC ACM */
#include <zephyr/usb/usb_device.h>
#include <uart_async_adapter.h>

#define USB_UART_INST DT_ALIAS(my_usb_serial)
static const struct device *uart_dev = DEVICE_DT_GET(USB_UART_INST);
UART_ASYNC_ADAPTER_INST_DEFINE(async_adapter);
#else 

/* Normal UART */
#define UART_INST DT_ALIAS(learning_serial)
static const struct device *uart_dev = DEVICE_DT_GET(UART_INST);

#endif /* CONFIG_UART_ASYNC_ADAPTER */

/* uart rx memory pool for DMA */
#define BUF_SIZE CONFIG_APP_UART_RX_DMA_BLOCK_SIZE
#define BUF_NUM CONFIG_APP_UART_RX_DMA_BLOCK_NUMBER
static K_MEM_SLAB_DEFINE(uart_slab, BUF_SIZE, BUF_NUM, 4);

/* Queues for TX and RX packet */
struct uart_data_t {
    uint8_t *data;
    size_t len;
};
K_MSGQ_DEFINE(tx_queue, sizeof(struct uart_data_t), 16, 4);
K_MSGQ_DEFINE(rx_queue, sizeof(struct uart_data_t), 16, 4);

/* TX semaphores */
static K_SEM_DEFINE(tx_done, 0, 1);

int app_uart_sleep(void)
{
    int err;
    err = uart_rx_disable(uart_dev);
    if (err) {
        LOG_ERR("Failed to disable RX: %d", err);
        return err;
    }

#if !IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)
    // give some time for UART callback
    k_sleep(K_MSEC(10)); 
    err = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);
    if (err) {
        LOG_ERR("Failed to suspend device: %d", err);
        return err;
    }
#endif /* !CONFIG_PM_DEVICE_RUNTIME */ 

    return 0;
}

int app_uart_wakeup(void)
{
    uint8_t *buf;
    int err;

#if !IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)
    err = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_RESUME);
    if (err) {
        LOG_ERR("Failed to resume device: %d", err);
        return err;
    }
#endif /* !CONFIG_PM_DEVICE_RUNTIME */

    err = k_mem_slab_alloc(&uart_slab, (void **)&buf, K_NO_WAIT);
    if (err) {
        LOG_ERR("Failed to allocate RX buffer: %d", err);
        return err;
    }

    err = uart_rx_enable(uart_dev, buf, BUF_SIZE, RX_INACTIVE_TIMEOUT_US);
    if (err) {
        LOG_ERR("Failed to enable RX: %d", err);
        return err;
    }
    return 0;
}

/* async serial callback */
static void uart_callback(const struct device *dev,
			  struct uart_event *evt,
			  void *user_data)
{
	struct device *uart = user_data;
	int err;

	switch (evt->type) {
	case UART_TX_DONE:
        LOG_INF("TX done %d bytes", evt->data.tx.len);
        k_sem_give(&tx_done);
		break;

	case UART_TX_ABORTED:
        LOG_WRN("TX aborted");
        k_sem_give(&tx_done);
		break;

	case UART_RX_RDY:
    {
        uint8_t *p = &(evt->data.rx.buf[evt->data.rx.offset]);
        size_t len = evt->data.rx.len;

        LOG_INF("RX %d bytes", len);
        
        struct uart_data_t packet = {
            .data = k_malloc(len),
            .len = len,
        };

        if( NULL == packet.data){
            LOG_ERR("Failed to alloc memory for RX packet!!!");
            return;
        }

        // if the RX buffer is full, it will be free after the `uart_callback` return.
        // so the data should be copy here.
        memcpy(packet.data, p, len);

        err = k_msgq_put(&rx_queue, &packet, K_NO_WAIT);
        if (err) {
            LOG_ERR("Failed to put packet to RX queue, freeing memory");
            k_free(packet.data); 
        } else {
            LOG_INF("RX %d bytes copied", len);
        }
		break;
    }

	case UART_RX_BUF_REQUEST:
	{
		uint8_t *buf;
        LOG_INF("RX buffer request");
		err = k_mem_slab_alloc(&uart_slab, (void **)&buf, K_NO_WAIT);
		__ASSERT(err == 0, "Failed to allocate slab\n");

		err = uart_rx_buf_rsp(uart, buf, BUF_SIZE);
		__ASSERT(err == 0, "Failed to provide new buffer\n");
		break;
	}

	case UART_RX_BUF_RELEASED:
        LOG_INF("RX buffer released");
		k_mem_slab_free(&uart_slab, (void *)evt->data.rx_buf.buf);
		break;

	case UART_RX_DISABLED:
        LOG_INF("RX disabled");
		break;

	case UART_RX_STOPPED:
        LOG_INF("RX stopped");
		break;
	}
}

static packets_cb_t user_callback = NULL;

int app_uart_rx_cb_register(packets_cb_t cb)
{
    __ASSERT(cb != NULL, "Callback cannot be NULL");
    user_callback = cb;
    return 0;
}

int app_uart_tx(const uint8_t *byte, size_t len)
{
    if (byte == NULL || len == 0) {
        LOG_WRN("Invalid TX parameters");
        return -EINVAL;
    }

    // k_msgq will copy the "packet" element. So we can use local variable here
    struct uart_data_t packet = {
        .data = k_malloc(len),
        .len = len,
    };

    if( NULL == packet.data){
        LOG_ERR("Failed to alloc memory for TX packet");
        return -ENOMEM;
    }

    memcpy(packet.data, byte, len);

    int err = k_msgq_put(&tx_queue, &packet, K_NO_WAIT);
    if (err) {
        LOG_ERR("Failed to put packet to TX queue, freeing memory");
        k_free(packet.data); 
        return err;
    }

    return 0;
}

static void app_uart_rx_thread()
{
    struct uart_data_t packet = {0};
    int err;

    while(1) {
        err = k_msgq_get(&rx_queue, &packet, K_FOREVER);
        if (err) {
            LOG_ERR("Failed to get packet from RX queue");
            continue;
        }

        LOG_HEXDUMP_INF(packet.data, packet.len, "RX packet:");

        // the user callback is in thread context
        if (user_callback == NULL) {
            LOG_WRN("No user callback registered for RX packets");
            k_free(packet.data);
            continue;
        } 
        user_callback(packet.data, packet.len);
        k_free(packet.data);

    }
}

static void app_uart_tx_thread()
{
    while(1) {
        struct uart_data_t packet = {0};
        int err;
        err = k_msgq_get(&tx_queue, &packet, K_FOREVER);

        if (err) {
            LOG_ERR("Failed to get packet from TX queue");
            continue;
        }

        err = uart_tx(uart_dev, packet.data, packet.len, 0);
        if (err) {
            LOG_ERR("Failed to send tx data");
            k_free(packet.data);
            continue;
        }

        // wait for TX done
        k_sem_take(&tx_done, K_FOREVER);
        k_free(packet.data);
    }
}

static int app_uart_init(void)
{
    int err;
    uint8_t *buf;

	if (!device_is_ready(uart_dev)) {
        LOG_ERR("device %s is not ready; exiting", uart_dev->name);
        return -ENODEV;
    }

#if IS_ENABLED(CONFIG_UART_ASYNC_ADAPTER) && IS_ENABLED(CONFIG_USB_CDC_ACM)


    err = usb_enable(NULL);
    if (err && (err != -EALREADY)) {
        printk("Failed to enable USB\n");
        return -ENODEV;
    }

    /* Implement API adapter */
    uart_async_adapter_init(async_adapter, uart_dev);
    uart_dev = async_adapter;

#endif

	err = uart_callback_set(uart_dev, uart_callback, (void *)uart_dev);
	__ASSERT(err == 0, "Failed to set callback");

    // allocate buffer and start rx
    err = k_mem_slab_alloc(&uart_slab, (void **)&buf, K_NO_WAIT);
	__ASSERT(err == 0, "Failed to alloc slab");

    // for the UARTE that have "frame-timeout-supported" property,
    // the RX_INACTIVE_TIMEOUT_US doesn't take effect if it is bigger than max FRAMETIMEOUT of UARTE.
    // For example, nRF54L15
    err = uart_rx_enable(uart_dev, buf, BUF_SIZE, RX_INACTIVE_TIMEOUT_US);
    __ASSERT(err == 0, "Failed to enable rx");
    return 0;
}

K_THREAD_DEFINE(app_uart_rx_id, CONFIG_APP_UART_RX_THREAD_STACK_SIZE, app_uart_rx_thread, NULL, NULL, NULL,
		CONFIG_APP_UART_RX_THREAD_PRIORITY, 0, 0);

K_THREAD_DEFINE(app_uart_tx_id, CONFIG_APP_UART_TX_THREAD_STACK_SIZE, app_uart_tx_thread, NULL, NULL, NULL,
        CONFIG_APP_UART_TX_THREAD_PRIORITY, 0, 0);

SYS_INIT(app_uart_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
