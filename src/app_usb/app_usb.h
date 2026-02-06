/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_USB_H
#define APP_USB_H

#include <zephyr/usb/usbd.h>

void app_usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg);

#endif /* APP_USB_H */
