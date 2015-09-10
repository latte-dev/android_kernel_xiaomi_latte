/*
 * Copyright (C) 2015 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 */

/* SSIC D3 Disable Workaround
 * Timeout is set to 15 seconds.
 */
#define SSIC_D3_TIMEOUT	15000

extern bool hub_intel_ssic_port_check(struct usb_device *udev);
extern void hub_intel_ssic_d3_set(unsigned long data);
extern void hub_intel_ssic_d3_timer_init(struct usb_hcd *hcd,
		struct usb_port *port_dev);
extern void hub_intel_ssic_d3_timer_set(struct usb_device *udev,
		struct usb_port *port_dev, bool enable);
