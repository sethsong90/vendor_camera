/* server_debug.h
 *
 * Copyright (c) 2015 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#ifndef SERVER_DEBUG_H
#define SERVER_DEBUG_H
#define   MAX_FD_PER_PROCESS  1000

#if defined(__cplusplus)
extern "C" {
#endif

void dump_list_of_daemon_fd();
void* server_debug_dump_data_for_sof_freeze(void *status);
#if defined(__cplusplus)
}
#endif
#endif
