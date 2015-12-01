/*
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#ifndef _sp_map_h_
#define _sp_map_h_


#ifndef _hrt_dummy_use_blob_sp
#define _hrt_dummy_use_blob_sp()
#endif

#define _hrt_cell_load_program_sp(proc) _hrt_cell_load_program_embedded(proc, sp)

/* function input_system_acquisition_stop: AD8 */

/* function longjmp: 69CF */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_HIVE_IF_SRST_MASK
#define HIVE_MEM_HIVE_IF_SRST_MASK scalar_processor_2400_dmem
#define HIVE_ADDR_HIVE_IF_SRST_MASK 0x1C8
#define HIVE_SIZE_HIVE_IF_SRST_MASK 16
#else
#endif
#endif
#define HIVE_MEM_sp_HIVE_IF_SRST_MASK scalar_processor_2400_dmem
#define HIVE_ADDR_sp_HIVE_IF_SRST_MASK 0x1C8
#define HIVE_SIZE_sp_HIVE_IF_SRST_MASK 16

/* function tmpmem_init_dmem: 66E2 */

/* function ia_css_isys_sp_token_map_receive_ack: 6026 */

/* function ia_css_dmaproxy_sp_set_addr_B: 3539 */

/* function ia_css_pipe_data_init_tagger_resources: A4F */

/* function debug_buffer_set_ddr_addr: DD */

/* function receiver_port_reg_load: ABC */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_mipi
#define HIVE_MEM_vbuf_mipi scalar_processor_2400_dmem
#define HIVE_ADDR_vbuf_mipi 0x6380
#define HIVE_SIZE_vbuf_mipi 12
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_mipi scalar_processor_2400_dmem
#define HIVE_ADDR_sp_vbuf_mipi 0x6380
#define HIVE_SIZE_sp_vbuf_mipi 12

/* function ia_css_event_sp_decode: 372A */

/* function ia_css_queue_get_size: 4B54 */

/* function ia_css_queue_load: 516B */

/* function setjmp: 69D8 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_sp2host_isys_event_queue
#define HIVE_MEM_sem_for_sp2host_isys_event_queue scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_sp2host_isys_event_queue 0x46D4
#define HIVE_SIZE_sem_for_sp2host_isys_event_queue 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_sp2host_isys_event_queue scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_sp2host_isys_event_queue 0x46D4
#define HIVE_SIZE_sp_sem_for_sp2host_isys_event_queue 20

/* function ia_css_dmaproxy_sp_wait_for_ack: 6F59 */

/* function ia_css_sp_rawcopy_func: 5390 */

/* function ia_css_tagger_buf_sp_pop_marked: 2BB2 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_stage
#define HIVE_MEM_isp_stage scalar_processor_2400_dmem
#define HIVE_ADDR_isp_stage 0x5C68
#define HIVE_SIZE_isp_stage 832
#else
#endif
#endif
#define HIVE_MEM_sp_isp_stage scalar_processor_2400_dmem
#define HIVE_ADDR_sp_isp_stage 0x5C68
#define HIVE_SIZE_sp_isp_stage 832

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_raw
#define HIVE_MEM_vbuf_raw scalar_processor_2400_dmem
#define HIVE_ADDR_vbuf_raw 0x314
#define HIVE_SIZE_vbuf_raw 4
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_raw scalar_processor_2400_dmem
#define HIVE_ADDR_sp_vbuf_raw 0x314
#define HIVE_SIZE_sp_vbuf_raw 4

/* function ia_css_sp_bin_copy_func: 52B7 */

/* function ia_css_queue_item_store: 4EB9 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_metadata_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_metadata_bufs scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_metadata_bufs 0x4B04
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_metadata_bufs 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_metadata_bufs scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_metadata_bufs 0x4B04
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_metadata_bufs 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_buffer_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_buffer_bufs scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_buffer_bufs 0x4B18
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_buffer_bufs 160
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_buffer_bufs scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_buffer_bufs 0x4B18
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_buffer_bufs 160

/* function sp_start_isp: 45D */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_binary_group
#define HIVE_MEM_sp_binary_group scalar_processor_2400_dmem
#define HIVE_ADDR_sp_binary_group 0x6058
#define HIVE_SIZE_sp_binary_group 32
#else
#endif
#endif
#define HIVE_MEM_sp_sp_binary_group scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_binary_group 0x6058
#define HIVE_SIZE_sp_sp_binary_group 32

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_sw_state
#define HIVE_MEM_sp_sw_state scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sw_state 0x6310
#define HIVE_SIZE_sp_sw_state 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_sw_state scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_sw_state 0x6310
#define HIVE_SIZE_sp_sp_sw_state 4

/* function ia_css_thread_sp_main: D50 */

/* function ia_css_ispctrl_sp_init_internal_buffers: 396B */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp2host_psys_event_queue_handle
#define HIVE_MEM_sp2host_psys_event_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp2host_psys_event_queue_handle 0x4BB8
#define HIVE_SIZE_sp2host_psys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_sp2host_psys_event_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp2host_psys_event_queue_handle 0x4BB8
#define HIVE_SIZE_sp_sp2host_psys_event_queue_handle 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_sp2host_psys_event_queue
#define HIVE_MEM_sem_for_sp2host_psys_event_queue scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_sp2host_psys_event_queue 0x46E8
#define HIVE_SIZE_sem_for_sp2host_psys_event_queue 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_sp2host_psys_event_queue scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_sp2host_psys_event_queue 0x46E8
#define HIVE_SIZE_sp_sem_for_sp2host_psys_event_queue 20

/* function ia_css_tagger_sp_propagate_frame: 2479 */

/* function input_system_reg_load: B11 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_handles
#define HIVE_MEM_vbuf_handles scalar_processor_2400_dmem
#define HIVE_ADDR_vbuf_handles 0x638C
#define HIVE_SIZE_vbuf_handles 960
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_handles scalar_processor_2400_dmem
#define HIVE_ADDR_sp_vbuf_handles 0x638C
#define HIVE_SIZE_sp_vbuf_handles 960

/* function ia_css_queue_store: 501F */

/* function ia_css_sp_flash_register: 2DE7 */

/* function ia_css_isys_sp_backend_create: 5C99 */

/* function ia_css_pipeline_sp_init: 1886 */

/* function ia_css_tagger_sp_configure: 2369 */

/* function ia_css_ispctrl_sp_end_binary: 3773 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs
#define HIVE_MEM_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x4BC4
#define HIVE_SIZE_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x4BC4
#define HIVE_SIZE_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 20

/* function receiver_port_reg_store: AC3 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_event_is_pending_mask
#define HIVE_MEM_event_is_pending_mask scalar_processor_2400_dmem
#define HIVE_ADDR_event_is_pending_mask 0x5C
#define HIVE_SIZE_event_is_pending_mask 44
#else
#endif
#endif
#define HIVE_MEM_sp_event_is_pending_mask scalar_processor_2400_dmem
#define HIVE_ADDR_sp_event_is_pending_mask 0x5C
#define HIVE_SIZE_sp_event_is_pending_mask 44

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cb_elems_frame
#define HIVE_MEM_sp_all_cb_elems_frame scalar_processor_2400_dmem
#define HIVE_ADDR_sp_all_cb_elems_frame 0x46FC
#define HIVE_SIZE_sp_all_cb_elems_frame 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cb_elems_frame scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_all_cb_elems_frame 0x46FC
#define HIVE_SIZE_sp_sp_all_cb_elems_frame 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp2host_isys_event_queue_handle
#define HIVE_MEM_sp2host_isys_event_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp2host_isys_event_queue_handle 0x4BD8
#define HIVE_SIZE_sp2host_isys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_sp2host_isys_event_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp2host_isys_event_queue_handle 0x4BD8
#define HIVE_SIZE_sp_sp2host_isys_event_queue_handle 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host_sp_com
#define HIVE_MEM_host_sp_com scalar_processor_2400_dmem
#define HIVE_ADDR_host_sp_com 0x413C
#define HIVE_SIZE_host_sp_com 220
#else
#endif
#endif
#define HIVE_MEM_sp_host_sp_com scalar_processor_2400_dmem
#define HIVE_ADDR_sp_host_sp_com 0x413C
#define HIVE_SIZE_sp_host_sp_com 220

/* function ia_css_queue_get_free_space: 4C7E */

/* function exec_image_pipe: 658 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_init_dmem_data
#define HIVE_MEM_sp_init_dmem_data scalar_processor_2400_dmem
#define HIVE_ADDR_sp_init_dmem_data 0x6314
#define HIVE_SIZE_sp_init_dmem_data 24
#else
#endif
#endif
#define HIVE_MEM_sp_sp_init_dmem_data scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_init_dmem_data 0x6314
#define HIVE_SIZE_sp_sp_init_dmem_data 24

/* function ia_css_sp_metadata_start: 5A76 */

/* function ia_css_bufq_sp_init_buffer_queues: 2E56 */

/* function ia_css_pipeline_sp_stop: 1869 */

/* function ia_css_tagger_sp_connect_pipes: 2853 */

/* function sp_isys_copy_wait: 6A1 */

/* function is_isp_debug_buffer_full: 337 */

/* function ia_css_dmaproxy_sp_configure_channel_from_info: 34A9 */

/* function encode_and_post_timer_event: 9C4 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_per_frame_data
#define HIVE_MEM_sp_per_frame_data scalar_processor_2400_dmem
#define HIVE_ADDR_sp_per_frame_data 0x4218
#define HIVE_SIZE_sp_per_frame_data 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_per_frame_data scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_per_frame_data 0x4218
#define HIVE_SIZE_sp_sp_per_frame_data 4

/* function ia_css_rmgr_sp_vbuf_dequeue: 6436 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_psys_event_queue_handle
#define HIVE_MEM_host2sp_psys_event_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_host2sp_psys_event_queue_handle 0x4BE4
#define HIVE_SIZE_host2sp_psys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_psys_event_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp_host2sp_psys_event_queue_handle 0x4BE4
#define HIVE_SIZE_sp_host2sp_psys_event_queue_handle 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_xmem_bin_addr
#define HIVE_MEM_xmem_bin_addr scalar_processor_2400_dmem
#define HIVE_ADDR_xmem_bin_addr 0x421C
#define HIVE_SIZE_xmem_bin_addr 4
#else
#endif
#endif
#define HIVE_MEM_sp_xmem_bin_addr scalar_processor_2400_dmem
#define HIVE_ADDR_sp_xmem_bin_addr 0x421C
#define HIVE_SIZE_sp_xmem_bin_addr 4

/* function tmr_clock_init: 141C */

/* function ia_css_pipeline_sp_run: 143D */

/* function memcpy: 6A78 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_GP_DEVICE_BASE
#define HIVE_MEM_GP_DEVICE_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_GP_DEVICE_BASE 0x31C
#define HIVE_SIZE_GP_DEVICE_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_GP_DEVICE_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_GP_DEVICE_BASE 0x31C
#define HIVE_SIZE_sp_GP_DEVICE_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_thread_sp_ready_queue
#define HIVE_MEM_ia_css_thread_sp_ready_queue scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_thread_sp_ready_queue 0x1E4
#define HIVE_SIZE_ia_css_thread_sp_ready_queue 12
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_thread_sp_ready_queue scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_thread_sp_ready_queue 0x1E4
#define HIVE_SIZE_sp_ia_css_thread_sp_ready_queue 12

/* function input_system_reg_store: B18 */

/* function ia_css_isys_sp_frontend_start: 5EAF */

/* function ia_css_uds_sp_scale_params: 6781 */

/* function ia_css_circbuf_increase_size: E35 */

/* function __divu: 69F6 */

/* function ia_css_thread_sp_get_state: C78 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_cont_capt_stop
#define HIVE_MEM_sem_for_cont_capt_stop scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_cont_capt_stop 0x470C
#define HIVE_SIZE_sem_for_cont_capt_stop 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_cont_capt_stop scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_cont_capt_stop 0x470C
#define HIVE_SIZE_sp_sem_for_cont_capt_stop 20

/* function thread_fiber_sp_main: E2E */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_isp_pipe_thread
#define HIVE_MEM_sp_isp_pipe_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_isp_pipe_thread 0x4850
#define HIVE_SIZE_sp_isp_pipe_thread 360
#else
#endif
#endif
#define HIVE_MEM_sp_sp_isp_pipe_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_isp_pipe_thread 0x4850
#define HIVE_SIZE_sp_sp_isp_pipe_thread 360

/* function ia_css_parambuf_sp_handle_parameter_sets: 127F */

/* function ia_css_spctrl_sp_set_state: 5AA5 */

/* function ia_css_thread_sem_sp_signal: 6C7A */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_IRQ_BASE
#define HIVE_MEM_IRQ_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_IRQ_BASE 0x2C
#define HIVE_SIZE_IRQ_BASE 16
#else
#endif
#endif
#define HIVE_MEM_sp_IRQ_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_IRQ_BASE 0x2C
#define HIVE_SIZE_sp_IRQ_BASE 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_TIMED_CTRL_BASE
#define HIVE_MEM_TIMED_CTRL_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_TIMED_CTRL_BASE 0x40
#define HIVE_SIZE_TIMED_CTRL_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_TIMED_CTRL_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_TIMED_CTRL_BASE 0x40
#define HIVE_SIZE_sp_TIMED_CTRL_BASE 4

/* function ia_css_isys_sp_isr: 7147 */

/* function ia_css_isys_sp_generate_exp_id: 6247 */

/* function ia_css_rmgr_sp_init: 6331 */

/* function ia_css_thread_sem_sp_init: 6D49 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_reading_cb_frame
#define HIVE_MEM_sem_for_reading_cb_frame scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_reading_cb_frame 0x4720
#define HIVE_SIZE_sem_for_reading_cb_frame 40
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_reading_cb_frame scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_reading_cb_frame 0x4720
#define HIVE_SIZE_sp_sem_for_reading_cb_frame 40

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_is_isp_requested
#define HIVE_MEM_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_is_isp_requested 0x328
#define HIVE_SIZE_is_isp_requested 4
#else
#endif
#endif
#define HIVE_MEM_sp_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_sp_is_isp_requested 0x328
#define HIVE_SIZE_sp_is_isp_requested 4

/* function ia_css_dmaproxy_sp_execute: 340F */

/* function ia_css_queue_is_empty: 70A6 */

/* function ia_css_pipeline_sp_has_stopped: 185F */

/* function ia_css_circbuf_extract: F39 */

/* function ia_css_tagger_buf_sp_is_locked_from_start: 2CC8 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_current_sp_thread
#define HIVE_MEM_current_sp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_current_sp_thread 0x1DC
#define HIVE_SIZE_current_sp_thread 4
#else
#endif
#endif
#define HIVE_MEM_sp_current_sp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_current_sp_thread 0x1DC
#define HIVE_SIZE_sp_current_sp_thread 4

/* function ia_css_spctrl_sp_get_spid: 5AAC */

/* function ia_css_bufq_sp_reset_buffers: 2EDD */

/* function ia_css_dmaproxy_sp_read_byte_addr: 6F87 */

/* function ia_css_rmgr_sp_uninit: 632A */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_threads_stack
#define HIVE_MEM_sp_threads_stack scalar_processor_2400_dmem
#define HIVE_ADDR_sp_threads_stack 0x164
#define HIVE_SIZE_sp_threads_stack 28
#else
#endif
#endif
#define HIVE_MEM_sp_sp_threads_stack scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_threads_stack 0x164
#define HIVE_SIZE_sp_sp_threads_stack 28

/* function ia_css_circbuf_peek: F1B */

/* function ia_css_parambuf_sp_wait_for_in_param: 1048 */

/* function ia_css_isys_sp_token_map_get_exp_id: 610F */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cb_elems_param
#define HIVE_MEM_sp_all_cb_elems_param scalar_processor_2400_dmem
#define HIVE_ADDR_sp_all_cb_elems_param 0x4748
#define HIVE_SIZE_sp_all_cb_elems_param 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cb_elems_param scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_all_cb_elems_param 0x4748
#define HIVE_SIZE_sp_sp_all_cb_elems_param 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_pipeline_sp_curr_binary_id
#define HIVE_MEM_pipeline_sp_curr_binary_id scalar_processor_2400_dmem
#define HIVE_ADDR_pipeline_sp_curr_binary_id 0x1F0
#define HIVE_SIZE_pipeline_sp_curr_binary_id 4
#else
#endif
#endif
#define HIVE_MEM_sp_pipeline_sp_curr_binary_id scalar_processor_2400_dmem
#define HIVE_ADDR_sp_pipeline_sp_curr_binary_id 0x1F0
#define HIVE_SIZE_sp_pipeline_sp_curr_binary_id 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_frame_desc
#define HIVE_MEM_sp_all_cbs_frame_desc scalar_processor_2400_dmem
#define HIVE_ADDR_sp_all_cbs_frame_desc 0x4758
#define HIVE_SIZE_sp_all_cbs_frame_desc 8
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_frame_desc scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_all_cbs_frame_desc 0x4758
#define HIVE_SIZE_sp_sp_all_cbs_frame_desc 8

/* function sp_isys_copy_func_v2: 69A */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_reading_cb_param
#define HIVE_MEM_sem_for_reading_cb_param scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_reading_cb_param 0x4760
#define HIVE_SIZE_sem_for_reading_cb_param 40
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_reading_cb_param scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_reading_cb_param 0x4760
#define HIVE_SIZE_sp_sem_for_reading_cb_param 40

/* function ia_css_queue_get_used_space: 4C32 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_cont_capt_start
#define HIVE_MEM_sem_for_cont_capt_start scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_cont_capt_start 0x4788
#define HIVE_SIZE_sem_for_cont_capt_start 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_cont_capt_start scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_cont_capt_start 0x4788
#define HIVE_SIZE_sp_sem_for_cont_capt_start 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_tmp_heap
#define HIVE_MEM_tmp_heap scalar_processor_2400_dmem
#define HIVE_ADDR_tmp_heap 0x6078
#define HIVE_SIZE_tmp_heap 640
#else
#endif
#endif
#define HIVE_MEM_sp_tmp_heap scalar_processor_2400_dmem
#define HIVE_ADDR_sp_tmp_heap 0x6078
#define HIVE_SIZE_sp_tmp_heap 640

/* function ia_css_rmgr_sp_get_num_vbuf: 663A */

/* function ia_css_ispctrl_sp_output_compute_dma_info: 41AC */

/* function ia_css_tagger_sp_lock_exp_id: 2136 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_s3a_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_s3a_bufs scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_s3a_bufs 0x4BF0
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_s3a_bufs 60
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_s3a_bufs scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_s3a_bufs 0x4BF0
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_s3a_bufs 60

/* function ia_css_queue_is_full: 4CC9 */

/* function debug_buffer_init_isp: E4 */

/* function ia_css_isys_sp_frontend_uninit: 5E69 */

/* function ia_css_tagger_sp_exp_id_is_locked: 206C */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_rmgr_sp_mipi_frame_sem
#define HIVE_MEM_ia_css_rmgr_sp_mipi_frame_sem scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_rmgr_sp_mipi_frame_sem 0x674C
#define HIVE_SIZE_ia_css_rmgr_sp_mipi_frame_sem 60
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_rmgr_sp_mipi_frame_sem scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_rmgr_sp_mipi_frame_sem 0x674C
#define HIVE_SIZE_sp_ia_css_rmgr_sp_mipi_frame_sem 60

/* function ia_css_rmgr_sp_refcount_dump: 6411 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_isp_parameters_id
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_isp_parameters_id scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x4C2C
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_isp_parameters_id 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x4C2C
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_pipe_threads
#define HIVE_MEM_sp_pipe_threads scalar_processor_2400_dmem
#define HIVE_ADDR_sp_pipe_threads 0x150
#define HIVE_SIZE_sp_pipe_threads 20
#else
#endif
#endif
#define HIVE_MEM_sp_sp_pipe_threads scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_pipe_threads 0x150
#define HIVE_SIZE_sp_sp_pipe_threads 20

/* function sp_event_proxy_func: 6AF */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_isys_event_queue_handle
#define HIVE_MEM_host2sp_isys_event_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_host2sp_isys_event_queue_handle 0x4C40
#define HIVE_SIZE_host2sp_isys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_isys_event_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp_host2sp_isys_event_queue_handle 0x4C40
#define HIVE_SIZE_sp_host2sp_isys_event_queue_handle 12

/* function ia_css_thread_sp_yield: 6BF8 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_param_desc
#define HIVE_MEM_sp_all_cbs_param_desc scalar_processor_2400_dmem
#define HIVE_ADDR_sp_all_cbs_param_desc 0x479C
#define HIVE_SIZE_sp_all_cbs_param_desc 8
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_param_desc scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_all_cbs_param_desc 0x479C
#define HIVE_SIZE_sp_sp_all_cbs_param_desc 8

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_dmaproxy_sp_invalidate_tlb
#define HIVE_MEM_ia_css_dmaproxy_sp_invalidate_tlb scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb 0x5C58
#define HIVE_SIZE_ia_css_dmaproxy_sp_invalidate_tlb 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_dmaproxy_sp_invalidate_tlb scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_dmaproxy_sp_invalidate_tlb 0x5C58
#define HIVE_SIZE_sp_ia_css_dmaproxy_sp_invalidate_tlb 4

/* function ia_css_thread_sp_fork: D05 */

/* function ia_css_tagger_sp_destroy: 285D */

/* function ia_css_dmaproxy_sp_vmem_read: 33AF */

/* function ia_css_ifmtr_sp_init: 6298 */

/* function initialize_sp_group: 668 */

/* function ia_css_tagger_buf_sp_peek: 2AD4 */

/* function ia_css_thread_sp_init: D31 */

/* function ia_css_isys_sp_reset_exp_id: 623F */

/* function qos_scheduler_update_fps: 6771 */

/* function ia_css_ispctrl_sp_set_stream_base_addr: 48A0 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_DMEM_BASE
#define HIVE_MEM_ISP_DMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_DMEM_BASE 0x10
#define HIVE_SIZE_ISP_DMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_DMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_DMEM_BASE 0x10
#define HIVE_SIZE_sp_ISP_DMEM_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_SP_DMEM_BASE
#define HIVE_MEM_SP_DMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_SP_DMEM_BASE 0x4
#define HIVE_SIZE_SP_DMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_SP_DMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_SP_DMEM_BASE 0x4
#define HIVE_SIZE_sp_SP_DMEM_BASE 4

/* function __ia_css_queue_is_empty_text: 4B8F */

/* function ia_css_dmaproxy_sp_read: 3425 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_raw_copy_line_count
#define HIVE_MEM_raw_copy_line_count scalar_processor_2400_dmem
#define HIVE_ADDR_raw_copy_line_count 0x2E8
#define HIVE_SIZE_raw_copy_line_count 4
#else
#endif
#endif
#define HIVE_MEM_sp_raw_copy_line_count scalar_processor_2400_dmem
#define HIVE_ADDR_sp_raw_copy_line_count 0x2E8
#define HIVE_SIZE_sp_raw_copy_line_count 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_tag_cmd_queue_handle
#define HIVE_MEM_host2sp_tag_cmd_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_host2sp_tag_cmd_queue_handle 0x4C4C
#define HIVE_SIZE_host2sp_tag_cmd_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_tag_cmd_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp_host2sp_tag_cmd_queue_handle 0x4C4C
#define HIVE_SIZE_sp_host2sp_tag_cmd_queue_handle 12

/* function ia_css_queue_peek: 4BA8 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_frame_cnt
#define HIVE_MEM_ia_css_flash_sp_frame_cnt scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_flash_sp_frame_cnt 0x4AF8
#define HIVE_SIZE_ia_css_flash_sp_frame_cnt 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_frame_cnt scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_flash_sp_frame_cnt 0x4AF8
#define HIVE_SIZE_sp_ia_css_flash_sp_frame_cnt 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_event_can_send_token_mask
#define HIVE_MEM_event_can_send_token_mask scalar_processor_2400_dmem
#define HIVE_ADDR_event_can_send_token_mask 0x88
#define HIVE_SIZE_event_can_send_token_mask 44
#else
#endif
#endif
#define HIVE_MEM_sp_event_can_send_token_mask scalar_processor_2400_dmem
#define HIVE_ADDR_sp_event_can_send_token_mask 0x88
#define HIVE_SIZE_sp_event_can_send_token_mask 44

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_thread
#define HIVE_MEM_isp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_isp_thread 0x5FA8
#define HIVE_SIZE_isp_thread 4
#else
#endif
#endif
#define HIVE_MEM_sp_isp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_isp_thread 0x5FA8
#define HIVE_SIZE_sp_isp_thread 4

/* function encode_and_post_sp_event_non_blocking: A0C */

/* function ia_css_isys_sp_frontend_destroy: 5F41 */

/* function is_ddr_debug_buffer_full: 2CC */

/* function ia_css_isys_sp_frontend_stop: 5E81 */

/* function ia_css_isys_sp_token_map_init: 61DD */

/* function ia_css_tagger_buf_sp_get_oldest_marked_offset: 2B24 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_threads_fiber
#define HIVE_MEM_sp_threads_fiber scalar_processor_2400_dmem
#define HIVE_ADDR_sp_threads_fiber 0x19C
#define HIVE_SIZE_sp_threads_fiber 28
#else
#endif
#endif
#define HIVE_MEM_sp_sp_threads_fiber scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_threads_fiber 0x19C
#define HIVE_SIZE_sp_sp_threads_fiber 28

/* function encode_and_post_sp_event: 995 */

/* function debug_enqueue_ddr: EE */

/* function ia_css_rmgr_sp_refcount_init_vbuf: 63CC */

/* function dmaproxy_sp_read_write: 7025 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_dmaproxy_isp_dma_cmd_buffer
#define HIVE_MEM_ia_css_dmaproxy_isp_dma_cmd_buffer scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_dmaproxy_isp_dma_cmd_buffer 0x5C5C
#define HIVE_SIZE_ia_css_dmaproxy_isp_dma_cmd_buffer 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_dmaproxy_isp_dma_cmd_buffer scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_dmaproxy_isp_dma_cmd_buffer 0x5C5C
#define HIVE_SIZE_sp_ia_css_dmaproxy_isp_dma_cmd_buffer 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_buffer_queue_handle
#define HIVE_MEM_host2sp_buffer_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_host2sp_buffer_queue_handle 0x4C58
#define HIVE_SIZE_host2sp_buffer_queue_handle 480
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_buffer_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp_host2sp_buffer_queue_handle 0x4C58
#define HIVE_SIZE_sp_host2sp_buffer_queue_handle 480

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_in_service
#define HIVE_MEM_ia_css_flash_sp_in_service scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_flash_sp_in_service 0x31A0
#define HIVE_SIZE_ia_css_flash_sp_in_service 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_in_service scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_flash_sp_in_service 0x31A0
#define HIVE_SIZE_sp_ia_css_flash_sp_in_service 4

/* function ia_css_dmaproxy_sp_process: 6D71 */

/* function ia_css_tagger_buf_sp_mark_from_end: 2DAC */

/* function ia_css_isys_sp_backend_rcv_acquire_ack: 5B4E */

/* function ia_css_isys_sp_backend_pre_acquire_request: 5B64 */

/* function ia_css_ispctrl_sp_init_cs: 386E */

/* function ia_css_spctrl_sp_init: 5ABA */

/* function sp_event_proxy_init: 6C4 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_previous_clock_tick
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_previous_clock_tick scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x4E38
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_previous_clock_tick 40
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x4E38
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick 40

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_output
#define HIVE_MEM_sp_output scalar_processor_2400_dmem
#define HIVE_ADDR_sp_output 0x4220
#define HIVE_SIZE_sp_output 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_output scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_output 0x4220
#define HIVE_SIZE_sp_sp_output 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_sems_for_host2sp_buf_queues
#define HIVE_MEM_ia_css_bufq_sp_sems_for_host2sp_buf_queues scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x4E60
#define HIVE_SIZE_ia_css_bufq_sp_sems_for_host2sp_buf_queues 800
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x4E60
#define HIVE_SIZE_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues 800

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_CTRL_BASE
#define HIVE_MEM_ISP_CTRL_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_CTRL_BASE 0x8
#define HIVE_SIZE_ISP_CTRL_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_CTRL_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_CTRL_BASE 0x8
#define HIVE_SIZE_sp_ISP_CTRL_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_INPUT_FORMATTER_BASE
#define HIVE_MEM_INPUT_FORMATTER_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_INPUT_FORMATTER_BASE 0x4C
#define HIVE_SIZE_INPUT_FORMATTER_BASE 16
#else
#endif
#endif
#define HIVE_MEM_sp_INPUT_FORMATTER_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_INPUT_FORMATTER_BASE 0x4C
#define HIVE_SIZE_sp_INPUT_FORMATTER_BASE 16

/* function sp_dma_proxy_reset_channels: 3694 */

/* function ia_css_isys_sp_backend_acquire: 5C6F */

/* function ia_css_tagger_sp_update_size: 2AA3 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_host_sp_queue
#define HIVE_MEM_ia_css_bufq_host_sp_queue scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_host_sp_queue 0x5180
#define HIVE_SIZE_ia_css_bufq_host_sp_queue 2008
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_host_sp_queue scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_host_sp_queue 0x5180
#define HIVE_SIZE_sp_ia_css_bufq_host_sp_queue 2008

/* function thread_fiber_sp_create: D9D */

/* function ia_css_dmaproxy_sp_set_increments: 3526 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_writing_cb_frame
#define HIVE_MEM_sem_for_writing_cb_frame scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_writing_cb_frame 0x47A4
#define HIVE_SIZE_sem_for_writing_cb_frame 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_writing_cb_frame scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_writing_cb_frame 0x47A4
#define HIVE_SIZE_sp_sem_for_writing_cb_frame 20

/* function receiver_reg_store: AD1 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_writing_cb_param
#define HIVE_MEM_sem_for_writing_cb_param scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_writing_cb_param 0x47B8
#define HIVE_SIZE_sem_for_writing_cb_param 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_writing_cb_param scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_writing_cb_param 0x47B8
#define HIVE_SIZE_sp_sem_for_writing_cb_param 20

/* function sp_start_isp_entry: 453 */
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifdef HIVE_ADDR_sp_start_isp_entry
#endif
#define HIVE_ADDR_sp_start_isp_entry 0x453
#endif
#define HIVE_ADDR_sp_sp_start_isp_entry 0x453

/* function ia_css_tagger_buf_sp_unmark_all: 2D30 */

/* function ia_css_tagger_buf_sp_unmark_from_start: 2D71 */

/* function ia_css_dmaproxy_sp_channel_acquire: 36C0 */

/* function ia_css_rmgr_sp_add_num_vbuf: 6616 */

/* function ia_css_isys_sp_token_map_create: 6226 */

/* function __ia_css_dmaproxy_sp_wait_for_ack_text: 337B */

/* function ia_css_tagger_sp_acquire_buf_elem: 2044 */

/* function ia_css_bufq_sp_is_dynamic_buffer: 3227 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_group
#define HIVE_MEM_sp_group scalar_processor_2400_dmem
#define HIVE_ADDR_sp_group 0x4230
#define HIVE_SIZE_sp_group 1184
#else
#endif
#endif
#define HIVE_MEM_sp_sp_group scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_group 0x4230
#define HIVE_SIZE_sp_sp_group 1184

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_event_proxy_thread
#define HIVE_MEM_sp_event_proxy_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_event_proxy_thread 0x49B8
#define HIVE_SIZE_sp_event_proxy_thread 72
#else
#endif
#endif
#define HIVE_MEM_sp_sp_event_proxy_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_event_proxy_thread 0x49B8
#define HIVE_SIZE_sp_sp_event_proxy_thread 72

/* function ia_css_thread_sp_kill: CCB */

/* function ia_css_tagger_sp_create: 2A51 */

/* function tmpmem_acquire_dmem: 66C3 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_MMU_BASE
#define HIVE_MEM_MMU_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_MMU_BASE 0x24
#define HIVE_SIZE_MMU_BASE 8
#else
#endif
#endif
#define HIVE_MEM_sp_MMU_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_MMU_BASE 0x24
#define HIVE_SIZE_sp_MMU_BASE 8

/* function ia_css_dmaproxy_sp_channel_release: 36AC */

/* function ia_css_dmaproxy_sp_is_idle: 368C */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_qos_start
#define HIVE_MEM_sem_for_qos_start scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_qos_start 0x47CC
#define HIVE_SIZE_sem_for_qos_start 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_qos_start scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_qos_start 0x47CC
#define HIVE_SIZE_sp_sem_for_qos_start 20

/* function isp_hmem_load: B4F */

/* function ia_css_tagger_sp_release_buf_elem: 2020 */

/* function ia_css_eventq_sp_send: 3702 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_isys_sp_error_cnt
#define HIVE_MEM_ia_css_isys_sp_error_cnt scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_isys_sp_error_cnt 0x6338
#define HIVE_SIZE_ia_css_isys_sp_error_cnt 16
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_isys_sp_error_cnt scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_isys_sp_error_cnt 0x6338
#define HIVE_SIZE_sp_ia_css_isys_sp_error_cnt 16

/* function ia_css_tagger_buf_sp_unlock_from_start: 2C60 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_debug_buffer_ddr_address
#define HIVE_MEM_debug_buffer_ddr_address scalar_processor_2400_dmem
#define HIVE_ADDR_debug_buffer_ddr_address 0xBC
#define HIVE_SIZE_debug_buffer_ddr_address 4
#else
#endif
#endif
#define HIVE_MEM_sp_debug_buffer_ddr_address scalar_processor_2400_dmem
#define HIVE_ADDR_sp_debug_buffer_ddr_address 0xBC
#define HIVE_SIZE_sp_debug_buffer_ddr_address 4

/* function sp_isys_copy_request: 6A8 */

/* function ia_css_rmgr_sp_refcount_retain_vbuf: 64A6 */

/* function ia_css_thread_sp_set_priority: CC3 */

/* function sizeof_hmem: BF6 */

/* function tmpmem_release_dmem: 66B2 */

/* function cnd_input_system_cfg: 392 */

/* function __ia_css_sp_rawcopy_func_critical: 70D0 */

/* function __ia_css_dmaproxy_sp_process_text: 331F */

/* function ia_css_dmaproxy_sp_set_width_exception: 3511 */

/* function sp_event_assert: 845 */

/* function ia_css_flash_sp_init_internal_params: 2E4B */

/* function ia_css_tagger_buf_sp_pop_unmarked_and_unlocked: 2B66 */

/* function __modu: 6A3C */

/* function ia_css_dmaproxy_sp_init_isp_vector: 3381 */

/* function isp_vamem_store: 0 */

/* function ia_css_tagger_sp_set_copy_pipe: 2A48 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_GDC_BASE
#define HIVE_MEM_GDC_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_GDC_BASE 0x44
#define HIVE_SIZE_GDC_BASE 8
#else
#endif
#endif
#define HIVE_MEM_sp_GDC_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_GDC_BASE 0x44
#define HIVE_SIZE_sp_GDC_BASE 8

/* function ia_css_queue_local_init: 4E93 */

/* function sp_event_proxy_callout_func: 6B09 */

/* function qos_scheduler_schedule_stage: 671D */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_thread_sp_num_ready_threads
#define HIVE_MEM_ia_css_thread_sp_num_ready_threads scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_thread_sp_num_ready_threads 0x4A48
#define HIVE_SIZE_ia_css_thread_sp_num_ready_threads 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_thread_sp_num_ready_threads scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_thread_sp_num_ready_threads 0x4A48
#define HIVE_SIZE_sp_ia_css_thread_sp_num_ready_threads 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_threads_stack_size
#define HIVE_MEM_sp_threads_stack_size scalar_processor_2400_dmem
#define HIVE_ADDR_sp_threads_stack_size 0x180
#define HIVE_SIZE_sp_threads_stack_size 28
#else
#endif
#endif
#define HIVE_MEM_sp_sp_threads_stack_size scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_threads_stack_size 0x180
#define HIVE_SIZE_sp_sp_threads_stack_size 28

/* function ia_css_ispctrl_sp_isp_done_row_striping: 4192 */

/* function __ia_css_isys_sp_isr_text: 5F6B */

/* function ia_css_queue_dequeue: 4D11 */

/* function is_qos_standalone_mode: 66F8 */

/* function ia_css_dmaproxy_sp_configure_channel: 6F9E */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_current_thread_fiber_sp
#define HIVE_MEM_current_thread_fiber_sp scalar_processor_2400_dmem
#define HIVE_ADDR_current_thread_fiber_sp 0x4A4C
#define HIVE_SIZE_current_thread_fiber_sp 4
#else
#endif
#endif
#define HIVE_MEM_sp_current_thread_fiber_sp scalar_processor_2400_dmem
#define HIVE_ADDR_sp_current_thread_fiber_sp 0x4A4C
#define HIVE_SIZE_sp_current_thread_fiber_sp 4

/* function ia_css_circbuf_pop: FCD */

/* function memset: 6ABB */

/* function irq_raise_set_token: B6 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_GPIO_BASE
#define HIVE_MEM_GPIO_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_GPIO_BASE 0x3C
#define HIVE_SIZE_GPIO_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_GPIO_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_GPIO_BASE 0x3C
#define HIVE_SIZE_sp_GPIO_BASE 4

/* function ia_css_pipeline_acc_stage_enable: 1818 */

/* function ia_css_tagger_sp_unlock_exp_id: 2091 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_ph
#define HIVE_MEM_isp_ph scalar_processor_2400_dmem
#define HIVE_ADDR_isp_ph 0x6348
#define HIVE_SIZE_isp_ph 28
#else
#endif
#endif
#define HIVE_MEM_sp_isp_ph scalar_processor_2400_dmem
#define HIVE_ADDR_sp_isp_ph 0x6348
#define HIVE_SIZE_sp_isp_ph 28

/* function ia_css_isys_sp_token_map_flush: 616B */

/* function ia_css_ispctrl_sp_init_ds: 39FA */

/* function get_xmem_base_addr_raw: 3DB5 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_param
#define HIVE_MEM_sp_all_cbs_param scalar_processor_2400_dmem
#define HIVE_ADDR_sp_all_cbs_param 0x47E0
#define HIVE_SIZE_sp_all_cbs_param 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_param scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_all_cbs_param 0x47E0
#define HIVE_SIZE_sp_sp_all_cbs_param 16

/* function ia_css_circbuf_create: 101B */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_sp_group
#define HIVE_MEM_sem_for_sp_group scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_sp_group 0x47F0
#define HIVE_SIZE_sem_for_sp_group 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_sp_group scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_sp_group 0x47F0
#define HIVE_SIZE_sp_sem_for_sp_group 20

/* function __ia_css_dmaproxy_sp_configure_channel_text: 34F0 */

/* function ia_css_framebuf_sp_wait_for_in_frame: 6641 */

/* function ia_css_sp_rawcopy_tag_frame: 57D7 */

/* function isp_hmem_clear: B1F */

/* function ia_css_framebuf_sp_release_in_frame: 6684 */

/* function ia_css_isys_sp_backend_snd_acquire_request: 5BC1 */

/* function ia_css_isys_sp_token_map_is_full: 5FF2 */

/* function input_system_acquisition_run: AF3 */

/* function ia_css_ispctrl_sp_start_binary: 384C */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_h_pipe_private_ddr_ptrs
#define HIVE_MEM_ia_css_bufq_sp_h_pipe_private_ddr_ptrs scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x5958
#define HIVE_SIZE_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x5958
#define HIVE_SIZE_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 20

/* function ia_css_eventq_sp_recv: 36D4 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_pool
#define HIVE_MEM_isp_pool scalar_processor_2400_dmem
#define HIVE_ADDR_isp_pool 0x308
#define HIVE_SIZE_isp_pool 4
#else
#endif
#endif
#define HIVE_MEM_sp_isp_pool scalar_processor_2400_dmem
#define HIVE_ADDR_sp_isp_pool 0x308
#define HIVE_SIZE_sp_isp_pool 4

/* function ia_css_rmgr_sp_rel_gen: 6373 */

/* function ia_css_tagger_sp_unblock_clients: 2919 */

/* function css_get_frame_processing_time_end: 2010 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_event_any_pending_mask
#define HIVE_MEM_event_any_pending_mask scalar_processor_2400_dmem
#define HIVE_ADDR_event_any_pending_mask 0x320
#define HIVE_SIZE_event_any_pending_mask 8
#else
#endif
#endif
#define HIVE_MEM_sp_event_any_pending_mask scalar_processor_2400_dmem
#define HIVE_ADDR_sp_event_any_pending_mask 0x320
#define HIVE_SIZE_sp_event_any_pending_mask 8

/* function ia_css_isys_sp_backend_push: 5B78 */

/* function sh_css_decode_tag_descr: 352 */

/* function debug_enqueue_isp: 27B */

/* function qos_scheduler_update_stage_budget: 6700 */

/* function ia_css_spctrl_sp_uninit: 5AB3 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_HIVE_IF_SWITCH_CODE
#define HIVE_MEM_HIVE_IF_SWITCH_CODE scalar_processor_2400_dmem
#define HIVE_ADDR_HIVE_IF_SWITCH_CODE 0x1D8
#define HIVE_SIZE_HIVE_IF_SWITCH_CODE 4
#else
#endif
#endif
#define HIVE_MEM_sp_HIVE_IF_SWITCH_CODE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_HIVE_IF_SWITCH_CODE 0x1D8
#define HIVE_SIZE_sp_HIVE_IF_SWITCH_CODE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_dis_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_dis_bufs scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_dis_bufs 0x596C
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_dis_bufs 140
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_dis_bufs scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_dis_bufs 0x596C
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_dis_bufs 140

/* function ia_css_tagger_buf_sp_lock_from_start: 2C94 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_isp_idle
#define HIVE_MEM_sem_for_isp_idle scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_isp_idle 0x4804
#define HIVE_SIZE_sem_for_isp_idle 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_isp_idle scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_isp_idle 0x4804
#define HIVE_SIZE_sp_sem_for_isp_idle 20

/* function ia_css_dmaproxy_sp_write_byte_addr: 33DE */

/* function ia_css_dmaproxy_sp_init: 3355 */

/* function ia_css_bufq_sp_release_dynamic_buf_clock_tick: 2F1D */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_VAMEM_BASE
#define HIVE_MEM_ISP_VAMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_VAMEM_BASE 0x14
#define HIVE_SIZE_ISP_VAMEM_BASE 12
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_VAMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_VAMEM_BASE 0x14
#define HIVE_SIZE_sp_ISP_VAMEM_BASE 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_rawcopy_sp_tagger
#define HIVE_MEM_ia_css_rawcopy_sp_tagger scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_rawcopy_sp_tagger 0x62F8
#define HIVE_SIZE_ia_css_rawcopy_sp_tagger 24
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_rawcopy_sp_tagger scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_rawcopy_sp_tagger 0x62F8
#define HIVE_SIZE_sp_ia_css_rawcopy_sp_tagger 24

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_exp_ids
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_exp_ids scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_exp_ids 0x59F8
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_exp_ids 70
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_exp_ids scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_exp_ids 0x59F8
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_exp_ids 70

/* function ia_css_queue_item_load: 4F85 */

/* function ia_css_spctrl_sp_get_state: 5A9E */

/* function ia_css_isys_sp_token_map_uninit: 6188 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_callout_sp_thread
#define HIVE_MEM_callout_sp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_callout_sp_thread 0x1E0
#define HIVE_SIZE_callout_sp_thread 4
#else
#endif
#endif
#define HIVE_MEM_sp_callout_sp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_callout_sp_thread 0x1E0
#define HIVE_SIZE_sp_callout_sp_thread 4

/* function thread_fiber_sp_init: E24 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_SP_PMEM_BASE
#define HIVE_MEM_SP_PMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_SP_PMEM_BASE 0x0
#define HIVE_SIZE_SP_PMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_SP_PMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_SP_PMEM_BASE 0x0
#define HIVE_SIZE_sp_SP_PMEM_BASE 4

/* function ia_css_isys_sp_token_map_snd_acquire_req: 60F8 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_isp_input_stream_format
#define HIVE_MEM_sp_isp_input_stream_format scalar_processor_2400_dmem
#define HIVE_ADDR_sp_isp_input_stream_format 0x4120
#define HIVE_SIZE_sp_isp_input_stream_format 20
#else
#endif
#endif
#define HIVE_MEM_sp_sp_isp_input_stream_format scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_isp_input_stream_format 0x4120
#define HIVE_SIZE_sp_sp_isp_input_stream_format 20

/* function __mod: 6A28 */

/* function ia_css_dmaproxy_sp_init_dmem_channel: 343F */

/* function ia_css_thread_sp_join: CF4 */

/* function ia_css_dmaproxy_sp_add_command: 7090 */

/* function ia_css_sp_metadata_thread_func: 5976 */

/* function __sp_event_proxy_func_critical: 6AF6 */

/* function ia_css_sp_metadata_wait: 5A65 */

/* function ia_css_circbuf_peek_from_start: EFD */

/* function ia_css_event_sp_encode: 375F */

/* function ia_css_thread_sp_run: D67 */

/* function sp_isys_copy_func: 68A */

/* function ia_css_isys_sp_backend_flush: 5BE1 */

/* function ia_css_isys_sp_backend_frame_exists: 5AFD */

/* function ia_css_sp_isp_param_init_isp_memories: 4A38 */

/* function register_isr: 83D */

/* function irq_raise: C8 */

/* function ia_css_dmaproxy_sp_mmu_invalidate: 32E5 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_HIVE_IF_SRST_ADDRESS
#define HIVE_MEM_HIVE_IF_SRST_ADDRESS scalar_processor_2400_dmem
#define HIVE_ADDR_HIVE_IF_SRST_ADDRESS 0x1B8
#define HIVE_SIZE_HIVE_IF_SRST_ADDRESS 16
#else
#endif
#endif
#define HIVE_MEM_sp_HIVE_IF_SRST_ADDRESS scalar_processor_2400_dmem
#define HIVE_ADDR_sp_HIVE_IF_SRST_ADDRESS 0x1B8
#define HIVE_SIZE_sp_HIVE_IF_SRST_ADDRESS 16

/* function pipeline_sp_initialize_stage: 195E */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_isys_sp_frontend_states
#define HIVE_MEM_ia_css_isys_sp_frontend_states scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_isys_sp_frontend_states 0x632C
#define HIVE_SIZE_ia_css_isys_sp_frontend_states 12
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_isys_sp_frontend_states scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_isys_sp_frontend_states 0x632C
#define HIVE_SIZE_sp_ia_css_isys_sp_frontend_states 12

/* function ia_css_dmaproxy_sp_read_byte_addr_mmio: 6F70 */

/* function ia_css_ispctrl_sp_done_ds: 39E1 */

/* function ia_css_sp_isp_param_get_mem_inits: 4A13 */

/* function ia_css_parambuf_sp_init_buffer_queues: 13F1 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_pfp_spref
#define HIVE_MEM_vbuf_pfp_spref scalar_processor_2400_dmem
#define HIVE_ADDR_vbuf_pfp_spref 0x310
#define HIVE_SIZE_vbuf_pfp_spref 4
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_pfp_spref scalar_processor_2400_dmem
#define HIVE_ADDR_sp_vbuf_pfp_spref 0x310
#define HIVE_SIZE_sp_vbuf_pfp_spref 4

/* function input_system_cfg: AB5 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_HMEM_BASE
#define HIVE_MEM_ISP_HMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_HMEM_BASE 0x20
#define HIVE_SIZE_ISP_HMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_HMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_HMEM_BASE 0x20
#define HIVE_SIZE_sp_ISP_HMEM_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_frames
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_frames scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_frames 0x5A40
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_frames 280
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_frames scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_frames 0x5A40
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_frames 280

/* function qos_scheduler_init_stage_budget: 675E */

/* function ia_css_isys_sp_backend_release: 5C56 */

/* function ia_css_isys_sp_backend_destroy: 5C80 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp2host_buffer_queue_handle
#define HIVE_MEM_sp2host_buffer_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp2host_buffer_queue_handle 0x5B58
#define HIVE_SIZE_sp2host_buffer_queue_handle 96
#else
#endif
#endif
#define HIVE_MEM_sp_sp2host_buffer_queue_handle scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp2host_buffer_queue_handle 0x5B58
#define HIVE_SIZE_sp_sp2host_buffer_queue_handle 96

/* function ia_css_isys_sp_token_map_check_mipi_frame_size: 60BC */

/* function ia_css_ispctrl_sp_init_isp_vars: 4705 */

/* function ia_css_isys_sp_frontend_has_empty_mipi_buffer_cb: 5CD2 */

/* function sp_warning: 870 */

/* function ia_css_rmgr_sp_vbuf_enqueue: 6466 */

/* function ia_css_tagger_sp_tag_exp_id: 21AB */

/* function ia_css_dmaproxy_sp_write: 33F5 */

/* function ia_css_parambuf_sp_release_in_param: 1245 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_irq_sw_interrupt_token
#define HIVE_MEM_irq_sw_interrupt_token scalar_processor_2400_dmem
#define HIVE_ADDR_irq_sw_interrupt_token 0x411C
#define HIVE_SIZE_irq_sw_interrupt_token 4
#else
#endif
#endif
#define HIVE_MEM_sp_irq_sw_interrupt_token scalar_processor_2400_dmem
#define HIVE_ADDR_sp_irq_sw_interrupt_token 0x411C
#define HIVE_SIZE_sp_irq_sw_interrupt_token 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_isp_addresses
#define HIVE_MEM_sp_isp_addresses scalar_processor_2400_dmem
#define HIVE_ADDR_sp_isp_addresses 0x5FAC
#define HIVE_SIZE_sp_isp_addresses 172
#else
#endif
#endif
#define HIVE_MEM_sp_sp_isp_addresses scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_isp_addresses 0x5FAC
#define HIVE_SIZE_sp_sp_isp_addresses 172

/* function ia_css_rmgr_sp_acq_gen: 638B */

/* function receiver_reg_load: ACA */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isps
#define HIVE_MEM_isps scalar_processor_2400_dmem
#define HIVE_ADDR_isps 0x6364
#define HIVE_SIZE_isps 28
#else
#endif
#endif
#define HIVE_MEM_sp_isps scalar_processor_2400_dmem
#define HIVE_ADDR_sp_isps 0x6364
#define HIVE_SIZE_sp_isps 28

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host_sp_queues_initialized
#define HIVE_MEM_host_sp_queues_initialized scalar_processor_2400_dmem
#define HIVE_ADDR_host_sp_queues_initialized 0x4134
#define HIVE_SIZE_host_sp_queues_initialized 4
#else
#endif
#endif
#define HIVE_MEM_sp_host_sp_queues_initialized scalar_processor_2400_dmem
#define HIVE_ADDR_sp_host_sp_queues_initialized 0x4134
#define HIVE_SIZE_sp_host_sp_queues_initialized 4

/* function ia_css_queue_uninit: 4E51 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_ispctrl_sp_isp_started
#define HIVE_MEM_ia_css_ispctrl_sp_isp_started scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_ispctrl_sp_isp_started 0x5C60
#define HIVE_SIZE_ia_css_ispctrl_sp_isp_started 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_ispctrl_sp_isp_started scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_ispctrl_sp_isp_started 0x5C60
#define HIVE_SIZE_sp_ia_css_ispctrl_sp_isp_started 4

/* function ia_css_bufq_sp_release_dynamic_buf: 2F89 */

/* function ia_css_dmaproxy_sp_set_height_exception: 3502 */

/* function ia_css_dmaproxy_sp_init_vmem_channel: 3473 */

/* function ia_css_dmaproxy_sp_write_byte_addr_mmio: 33C7 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_spref
#define HIVE_MEM_vbuf_spref scalar_processor_2400_dmem
#define HIVE_ADDR_vbuf_spref 0x30C
#define HIVE_SIZE_vbuf_spref 4
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_spref scalar_processor_2400_dmem
#define HIVE_ADDR_sp_vbuf_spref 0x30C
#define HIVE_SIZE_sp_vbuf_spref 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_metadata_thread
#define HIVE_MEM_sp_metadata_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_metadata_thread 0x4A00
#define HIVE_SIZE_sp_metadata_thread 72
#else
#endif
#endif
#define HIVE_MEM_sp_sp_metadata_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_metadata_thread 0x4A00
#define HIVE_SIZE_sp_sp_metadata_thread 72

/* function ia_css_queue_enqueue: 4D9B */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_request
#define HIVE_MEM_ia_css_flash_sp_request scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_flash_sp_request 0x4AFC
#define HIVE_SIZE_ia_css_flash_sp_request 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_request scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_flash_sp_request 0x4AFC
#define HIVE_SIZE_sp_ia_css_flash_sp_request 4

/* function ia_css_dmaproxy_sp_vmem_write: 3398 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_tagger_frames
#define HIVE_MEM_tagger_frames scalar_processor_2400_dmem
#define HIVE_ADDR_tagger_frames 0x4A50
#define HIVE_SIZE_tagger_frames 168
#else
#endif
#endif
#define HIVE_MEM_sp_tagger_frames scalar_processor_2400_dmem
#define HIVE_ADDR_sp_tagger_frames 0x4A50
#define HIVE_SIZE_sp_tagger_frames 168

/* function ia_css_isys_sp_token_map_snd_capture_req: 611A */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_reading_if
#define HIVE_MEM_sem_for_reading_if scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_reading_if 0x4818
#define HIVE_SIZE_sem_for_reading_if 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_reading_if scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_reading_if 0x4818
#define HIVE_SIZE_sp_sem_for_reading_if 20

/* function sp_generate_interrupts: 8EF */

/* function ia_css_pipeline_sp_start: 1871 */

/* function ia_css_thread_default_callout: 6BF1 */

/* function ia_css_sp_rawcopy_init: 5378 */

/* function tmr_clock_read: 1412 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_BAMEM_BASE
#define HIVE_MEM_ISP_BAMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_BAMEM_BASE 0x318
#define HIVE_SIZE_ISP_BAMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_BAMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_BAMEM_BASE 0x318
#define HIVE_SIZE_sp_ISP_BAMEM_BASE 4

/* function ia_css_isys_sp_frontend_rcv_capture_ack: 5D81 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_sems_for_sp2host_buf_queues
#define HIVE_MEM_ia_css_bufq_sp_sems_for_sp2host_buf_queues scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x5BB8
#define HIVE_SIZE_ia_css_bufq_sp_sems_for_sp2host_buf_queues 160
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x5BB8
#define HIVE_SIZE_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues 160

/* function css_get_frame_processing_time_start: 2018 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_frame
#define HIVE_MEM_sp_all_cbs_frame scalar_processor_2400_dmem
#define HIVE_ADDR_sp_all_cbs_frame 0x482C
#define HIVE_SIZE_sp_all_cbs_frame 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_frame scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_all_cbs_frame 0x482C
#define HIVE_SIZE_sp_sp_all_cbs_frame 16

/* function thread_sp_queue_print: D84 */

/* function sp_notify_eof: 89B */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_str2mem
#define HIVE_MEM_sem_for_str2mem scalar_processor_2400_dmem
#define HIVE_ADDR_sem_for_str2mem 0x483C
#define HIVE_SIZE_sem_for_str2mem 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_str2mem scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sem_for_str2mem 0x483C
#define HIVE_SIZE_sp_sem_for_str2mem 20

/* function ia_css_tagger_buf_sp_is_marked_from_start: 2CFC */

/* function ia_css_bufq_sp_acquire_dynamic_buf: 3141 */

/* function ia_css_circbuf_destroy: 1012 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_PMEM_BASE
#define HIVE_MEM_ISP_PMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_PMEM_BASE 0xC
#define HIVE_SIZE_ISP_PMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_PMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_PMEM_BASE 0xC
#define HIVE_SIZE_sp_ISP_PMEM_BASE 4

/* function ia_css_sp_isp_param_mem_load: 49A6 */

/* function ia_css_tagger_buf_sp_pop_from_start: 2AE8 */

/* function __div: 69E0 */

/* function ia_css_isys_sp_frontend_create: 5F52 */

/* function ia_css_rmgr_sp_refcount_release_vbuf: 6485 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_in_use
#define HIVE_MEM_ia_css_flash_sp_in_use scalar_processor_2400_dmem
#define HIVE_ADDR_ia_css_flash_sp_in_use 0x4B00
#define HIVE_SIZE_ia_css_flash_sp_in_use 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_in_use scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ia_css_flash_sp_in_use 0x4B00
#define HIVE_SIZE_sp_ia_css_flash_sp_in_use 4

/* function ia_css_thread_sem_sp_wait: 6CC5 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_sleep_mode
#define HIVE_MEM_sp_sleep_mode scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sleep_mode 0x4138
#define HIVE_SIZE_sp_sleep_mode 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_sleep_mode scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_sleep_mode 0x4138
#define HIVE_SIZE_sp_sp_sleep_mode 4

/* function ia_css_tagger_buf_sp_push: 2BF7 */

/* function mmu_invalidate_cache: D3 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_max_cb_elems
#define HIVE_MEM_sp_max_cb_elems scalar_processor_2400_dmem
#define HIVE_ADDR_sp_max_cb_elems 0x148
#define HIVE_SIZE_sp_max_cb_elems 8
#else
#endif
#endif
#define HIVE_MEM_sp_sp_max_cb_elems scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_max_cb_elems 0x148
#define HIVE_SIZE_sp_sp_max_cb_elems 8

/* function ia_css_queue_remote_init: 4E73 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_stop_req
#define HIVE_MEM_isp_stop_req scalar_processor_2400_dmem
#define HIVE_ADDR_isp_stop_req 0x46D0
#define HIVE_SIZE_isp_stop_req 4
#else
#endif
#endif
#define HIVE_MEM_sp_isp_stop_req scalar_processor_2400_dmem
#define HIVE_ADDR_sp_isp_stop_req 0x46D0
#define HIVE_SIZE_sp_isp_stop_req 4


#endif /* _sp_map_h_ */
