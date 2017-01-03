/*============================================================================

  Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

============================================================================*/

#ifndef __PPROC_PORT_H__
#define __PPROC_PORT_H__

#include "mct_stream.h"
#include "utils/pp_log.h"

#define PPROC_MAX_STREAM_PER_PORT 4
#define PPROC_MAX_SUBMODS 20

#ifdef MULTIPASS
//Use reproc count in filename for 2 step reprocess
#define CREATE_DUMP_FILENAME(buf, timebuf, pp_config, meta_frame_count,\
  stream_type_str, frame_id) \
  snprintf(buf, sizeof(buf), "%s_%d_%d_Metadata_%s_%d.bin", \
  timeBuf, pp_config.cur_reproc_count, meta_frame_count, \
  stream_type_str, frame_id);
#else
#define CREATE_DUMP_FILENAME(buf, timebuf, pp_config, meta_frame_count,\
  stream_type_str, frame_id) \
    snprintf(buf, sizeof(buf), "%s%d_Metadata_%s_%d.bin", \
    timeBuf, meta_frame_count, stream_type_str, frame_id);
#endif


mct_port_t *pproc_port_init(const char *name);
void pproc_port_deinit(mct_port_t *port);

boolean pproc_port_check_identity_in_port(void *data1, void *data2);
boolean pproc_port_check_port(mct_port_t *port, uint32_t identity);
mct_port_t *pproc_port_get_reserved_port(mct_module_t *module,
  uint32_t identity);
mct_stream_info_t *pproc_port_get_attached_stream_info(mct_port_t *port,
  uint32_t identity);
mct_port_t *pproc_port_resrv_port_on_module(mct_module_t *submod,
  mct_stream_info_t *stream_info, mct_port_direction_t direction,
  mct_port_t *pproc_port);
boolean pproc_port_check_meta_data_dump(cam_stream_type_t stream_type);

#endif /* __PPROC_PORT_H__ */
