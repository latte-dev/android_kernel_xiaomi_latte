/*
 * Intel Vendor Defined xHCI hub-specific capability
 *
 * Copyright (C) 2015 Intel Corp.
 *
 * Author: Leszczynski, Konrad
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

#include <linux/usb/phy.h>
#include <linux/usb/otg.h>

#include "xhci.h"
#include "../core/hub.h"
#include "hub-intel-cap.h"

bool hub_intel_ssic_port_check(struct usb_device *udev)
{
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	/* Check if device present and the port matches */
	return (xhci->ssic_device_present) &&
			(udev->portnum == xhci->ssic_port_number);
}

void hub_intel_ssic_d3_set(unsigned long data)
{
	struct usb_port *port_dev = (struct usb_port *) data;

	/* Decrement the usage counter */
	pm_runtime_put(&port_dev->dev);
}

void hub_intel_ssic_d3_timer_init(struct usb_hcd *hcd,
		struct usb_port *port_dev)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	/* Check if already initialized */
	if (xhci->set_d3_timer.data)
		return;

	/* Initialize the timer */
	init_timer(&xhci->set_d3_timer);

	/* Set the values for the timer */
	xhci->set_d3_timer.data = (unsigned long) port_dev;
	xhci->set_d3_timer.function = hub_intel_ssic_d3_set;
	xhci->set_d3_timer.expires = jiffies +
			msecs_to_jiffies(SSIC_D3_TIMEOUT);

	xhci_dbg(xhci, "set_d3_timer initialized\n");
}

void hub_intel_ssic_d3_timer_set(struct usb_device *udev,
		struct usb_port *port_dev, bool enable)
{
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	/* Update the timer's data context */
	xhci->set_d3_timer.data = (unsigned long) port_dev;

	/* Enable the timer in disconnect case,
	 * remove it on successful enumeration
	 * (we need to decrease to counter) */
	if (enable)
		add_timer(&xhci->set_d3_timer);
	else
		if (del_timer(&xhci->set_d3_timer))
			hub_intel_ssic_d3_set((unsigned long) port_dev);
}
