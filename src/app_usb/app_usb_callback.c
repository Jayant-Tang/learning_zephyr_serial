/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/smf.h>
#include <zephyr/sys/__assert.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(app_usb);

#include "app_usb.h"

struct usb_smf_ctx {
	struct smf_ctx ctx;
	const struct usbd_msg *msg;
};

enum usb_smf_state {
	USB_SMF_DISCONNECTED,
	USB_SMF_CONNECTED,
	USB_SMF_CONFIGURED,
	USB_SMF_SUSPENDED,
};

static struct usb_smf_ctx usb_smf;
static const struct smf_state usb_states[];
static struct usbd_context *usbd_ctx;
static bool usb_enabled;

static enum smf_state_result usb_state_disconnected_run(void *obj)
{
	struct usb_smf_ctx *s = (struct usb_smf_ctx *)obj;
	const struct usbd_msg *msg = s->msg;
	int err;

	if (!msg) {
		return SMF_EVENT_PROPAGATE;
	}

	/* Waiting for USB cable to be plugged in */
	switch (msg->type) {
	case USBD_MSG_VBUS_READY:
		/* VBUS detected - cable plugged in */
		smf_set_state(SMF_CTX(obj), &usb_states[USB_SMF_CONNECTED]);

		if (!usb_enabled) {
			err = usbd_enable(usbd_ctx);
			if (err == -ETIMEDOUT) {
				/* Probably the USB cable was disconnected before the usbd_enable
				 * was executed. Ignoring the error. The USB will be enabled once
				 * the cable is connected again.
				 */
				LOG_WRN("usbd_enable timed out");
				usb_enabled = false;
			} else if (err) {
				LOG_ERR("usbd_enable failed (err: %d)", err);
				usb_enabled = false;
			} else {
                LOG_INF("USB device enabled");
				usb_enabled = true;
			}
		}
		return SMF_EVENT_HANDLED;

	case USBD_MSG_VBUS_REMOVED:
		return SMF_EVENT_PROPAGATE;

	default:
		/* Ignore other events in disconnected state */
		LOG_WRN("Unexpected event %s in DISCONNECTED state",
			usbd_msg_type_string(msg->type));
		return SMF_EVENT_PROPAGATE;
	}
}

static enum smf_state_result usb_state_connected_run(void *obj)
{
	struct usb_smf_ctx *s = (struct usb_smf_ctx *)obj;
	const struct usbd_msg *msg = s->msg;
	int err;

	if (!msg) {
		return SMF_EVENT_PROPAGATE;
	}

	if (msg->type == USBD_MSG_VBUS_REMOVED) {
		/* VBUS removed - cable unplugged */
		smf_set_state(SMF_CTX(obj), &usb_states[USB_SMF_DISCONNECTED]);

		if (usb_enabled) {
			err = usbd_disable(usbd_ctx);
			if (err) {
				LOG_ERR("usbd_disable failed (err: %d)", err);
				usb_enabled = false;
				return SMF_EVENT_HANDLED;
			}
			usb_enabled = false;
            LOG_INF("USB device disabled");
		}

		return SMF_EVENT_HANDLED;
	}

	/* USB cable connected, waiting for enumeration */
	switch (msg->type) {
	case USBD_MSG_CONFIGURATION:
		/* USB configuration changed */
		LOG_INF("\tConfiguration value %d", msg->status);

		if (msg->status != 0) {
			/* Configured - enumeration complete */
			smf_set_state(SMF_CTX(obj), &usb_states[USB_SMF_CONFIGURED]);
		}
		return SMF_EVENT_HANDLED;

	case USBD_MSG_RESET:
		/* Host requested reset - stay in connected state (will re-enumerate) */
		LOG_DBG("USB reset in CONNECTED state");
		return SMF_EVENT_HANDLED;

	default:
		/* Ignore other events */
		return SMF_EVENT_PROPAGATE;
	}
}

static enum smf_state_result usb_state_configured_run(void *obj)
{
	struct usb_smf_ctx *s = (struct usb_smf_ctx *)obj;
	const struct usbd_msg *msg = s->msg;

	if (!msg) {
		return SMF_EVENT_PROPAGATE;
	}

	/* USB enumerated and ready for data transfer */
	switch (msg->type) {
	case USBD_MSG_SUSPEND:
		/* Host suspended the bus - must enter suspended state */
		smf_set_state(SMF_CTX(obj), &usb_states[USB_SMF_SUSPENDED]);
		return SMF_EVENT_HANDLED;

	case USBD_MSG_RESET:
		/* Host requested reset - return to connected state (will re-enumerate) */
		smf_set_state(SMF_CTX(obj), &usb_states[USB_SMF_CONNECTED]);
		return SMF_EVENT_HANDLED;

	case USBD_MSG_CONFIGURATION:
		/* USB configuration changed */
		LOG_DBG("\tConfiguration value %d", msg->status);

		if (msg->status == 0) {
			/* Deconfigured - return to connected state */
			smf_set_state(SMF_CTX(obj), &usb_states[USB_SMF_CONNECTED]);
		}
		return SMF_EVENT_HANDLED;

	case USBD_MSG_CDC_ACM_CONTROL_LINE_STATE:
		/* CDC ACM control line state changed (DTR/RTS signals) */
		{
			uint32_t dtr = 0, rts = 0;

			uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
			uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_RTS, &rts);
			LOG_INF("\tControl Line State: DTR=%d, RTS=%d", dtr, rts);

			/* Set DSR and DCD when DTR is asserted */
			if (dtr) {
				uart_line_ctrl_set(msg->dev, UART_LINE_CTRL_DCD, 1);
				uart_line_ctrl_set(msg->dev, UART_LINE_CTRL_DSR, 1);
			} else {
				uart_line_ctrl_set(msg->dev, UART_LINE_CTRL_DCD, 0);
				uart_line_ctrl_set(msg->dev, UART_LINE_CTRL_DSR, 0);
			}
		}
		return SMF_EVENT_HANDLED;
    
    case USBD_MSG_CDC_ACM_LINE_CODING:
        /* CDC ACM line coding changed (baud rate, parity, stop bits) */
        {
            uint32_t baudrate;
            int ret;

            ret = uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
            if (ret) {
                LOG_WRN("Failed to get baudrate, ret code %d", ret);
            } else {
                LOG_INF("\tBaudrate %u", baudrate);
            }
            
        }
        return SMF_EVENT_HANDLED;
	default:
		/* Ignore other events */
		return SMF_EVENT_PROPAGATE;
	}
}

static enum smf_state_result usb_state_suspended_run(void *obj)
{
	struct usb_smf_ctx *s = (struct usb_smf_ctx *)obj;
	const struct usbd_msg *msg = s->msg;

	if (!msg) {
		return SMF_EVENT_PROPAGATE;
	}

	/* USB suspended by host - in low power mode */
	switch (msg->type) {
	case USBD_MSG_RESUME:
		/* Host resumed the bus - return to configured state */
		smf_set_state(SMF_CTX(obj), &usb_states[USB_SMF_CONFIGURED]);
		return SMF_EVENT_HANDLED;

	case USBD_MSG_RESET:
		/* Host requested reset - return to connected state (will re-enumerate) */
		smf_set_state(SMF_CTX(obj), &usb_states[USB_SMF_CONNECTED]);
		return SMF_EVENT_HANDLED;

	default:
		/* Ignore other events */
		return SMF_EVENT_PROPAGATE;
	}
}

static const struct smf_state usb_states[] = {
    /* Parent state */
	[USB_SMF_DISCONNECTED] = SMF_CREATE_STATE(NULL, usb_state_disconnected_run,
						 NULL, NULL, NULL),
	[USB_SMF_CONNECTED] = SMF_CREATE_STATE(NULL, usb_state_connected_run, NULL,
					     NULL, NULL),

    /* Child states of CONNECTED */
	[USB_SMF_CONFIGURED] = SMF_CREATE_STATE(NULL, usb_state_configured_run, NULL,
					      &usb_states[USB_SMF_CONNECTED], NULL),
	[USB_SMF_SUSPENDED] = SMF_CREATE_STATE(NULL, usb_state_suspended_run, NULL,
					     &usb_states[USB_SMF_CONNECTED], NULL),
};

void app_usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
	int err;

	LOG_DBG("USBD MSG: %s", usbd_msg_type_string(msg->type));

	__ASSERT(ctx != NULL, "usbd context is NULL");
	usbd_ctx = ctx;

	usb_smf.msg = msg;
	err = smf_run_state(SMF_CTX(&usb_smf));
	usb_smf.msg = NULL;

	if (err) {
		LOG_ERR("USB SMF terminated (%d)", err);
	}
}

static int app_usb_callback_sys_init(void)
{
	usb_enabled = false;
	usb_smf.msg = NULL;

	smf_set_initial(SMF_CTX(&usb_smf), &usb_states[USB_SMF_DISCONNECTED]);

	return 0;
}

SYS_INIT(app_usb_callback_sys_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
