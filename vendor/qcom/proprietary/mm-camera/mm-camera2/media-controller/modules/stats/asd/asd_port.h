/* asd_port.h
 *
 * Copyright (c) 2013,2016 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */

#ifndef __ASD_PORT_H__
#define __ASD_PORT_H__

#include "mct_port.h"
#include "asd_module.h"
#include "asd_thread.h"


#define ASD_PORT_STATE_CREATED        0x1
#define ASD_PORT_STATE_RESERVED       0x2
#define ASD_PORT_STATE_UNRESERVED     0x3
#define ASD_PORT_STATE_LINKED         0x4
#define ASD_PORT_STATE_UNLINKED       0x5
#define ASD_PORT_STATE_STREAMON       0x6
#define ASD_PORT_STATE_STREAMOFF      0x7
#define ASD_PORT_STATE_AECAWB_RUNNING (ASD_PORT_STATE_STREAMON | (0x00000001 << 8))
#define ASD_PORT_STATE_AF_RUNNING     (ASD_PORT_STATE_STREAMON | (0x00000001 << 9))

/** _asd_port_private
 *    @asd_object: session index
 *    @port:       stream index
 *
 * Each asd module object should be used ONLY for one Bayer
 * session/stream set - use this structure to store session
 * and stream indices information.
 **/
typedef struct _asd_port_private {
  unsigned int      reserved_id;
  void              *asd_iface_handle; /* Handle to algo interface data */
  cam_stream_type_t stream_type;
  unsigned int      state;
  cam_scene_mode_type scene_mode;
  asd_object_t      asd_object;
  asd_thread_data_t *thread_data;
  mct_stream_info_t stream_info;
  boolean           asd_enable;
  cam_sync_type_t   dual_cam_sensor_info;
  uint32_t          intra_peer_id;
  mct_bus_msg_asd_decision_t last_asd_decision;
  char              asd_debug_data_array[ASD_DEBUG_DATA_SIZE];
  uint32_t          asd_debug_data_size;

  /* Extended functionality only below this line */
  boolean           use_extension;
  stats_util_override_func_t func_tbl;
  void              *ext_param;
} asd_port_private_t;

/* Expose to ASD module */
void    asd_port_deinit(mct_port_t *port);
boolean asd_port_find_identity(mct_port_t *port, unsigned int identity);
boolean asd_port_init(mct_port_t *port, unsigned int identity);

/* Expose to ASD extension */
void * asd_port_load_interface(asd_ops_t *asd_ops);
void asd_port_unload_interface(asd_ops_t *asd_ops, void* asd_handle);
void asd_port_send_asd_info_to_eztune(mct_port_t *port,
                                        asd_output_data_t *output);
void asd_send_bus_message(mct_port_t *port,
                          mct_bus_msg_type_t bus_msg_type,
                          void* payload,
                          int size,
                          int sof_id);

#endif /* __ASD_PORT_H__ */
