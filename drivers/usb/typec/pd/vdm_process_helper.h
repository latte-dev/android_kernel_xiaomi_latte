/*
 * vdm_process_helper.h: Intel USB PD VDM Helper Functions Header
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. Seee the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author: Venkataramana Kotakonda <venkataramana.kotakonda@intel.com>
 */

#ifndef VDM_PROCESS_HELPER_H
#define VDM_PROCESS_HELPER_H
int pe_handle_vendor_msg(struct policy_engine *pe, struct pd_packet *pkt);
int pe_send_discover_identity(struct policy_engine *pe, int type);
int pe_send_discover_svid(struct policy_engine *pe);
int pe_send_discover_mode(struct policy_engine *pe);
int pe_send_enter_mode(struct policy_engine *pe, int index);
int pe_send_display_status(struct policy_engine *pe);
int pe_send_display_configure(struct policy_engine *pe);

#endif /* VDM_PROCESS_HELPER_H */
