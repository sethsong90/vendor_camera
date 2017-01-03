/*============================================================================
Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential.
============================================================================*/

#include <stdlib.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/media.h>

#include "camera_dbg.h"
#include "cam_types.h"
#include "cam_intf.h"
#include "mct_controller.h"
#include "modules.h"
#include "iface_ispif.h"
#include "iface.h"
#include "iface_util.h"

#ifdef _ANDROID_
#include <cutils/properties.h>
#endif

/* #define ISPIF_DEBUG */

/** iface_ispif_hw_reset: reset ISPIF HW
 *    @ispif: ispif pointer
 *    @ispif_sink_port: sink port pointer
 *
 *  This function runs in MCTL thread context.
 *
 *  This function resets ISPIF HW
 *
 *  Return:  0 - Success
 *          -1 - Incorrect number of isps or hw reset error
 *
 **/
int iface_ispif_hw_reset(
  iface_t         *iface,
  iface_port_t    *iface_sink_port,
  iface_session_t *session __unused)
{
  int rc = 0;
  iface_sink_port_t *sink_port = &iface_sink_port->u.sink_port;
  uint32_t primary_cid_idx;
  iface_ispif_t *ispif = &iface->ispif_data.ispif_hw;
  struct ispif_cfg_data *cfg_cmd = &ispif->cfg_cmd;
  int num_isps = 0;
  int i;

  if (ispif->num_active_streams > 0)
    return 0;

  num_isps = iface->isp_axi_data.num_subdev;
  if (num_isps > VFE_MAX) {
    CDBG_ERROR("%s: num_ips = %d, larger than max allowed %d",
      __func__, num_isps, VFE_MAX);
    return -1;
  }

  primary_cid_idx = iface_ispif_util_find_primary_cid(&sink_port->sensor_out_info,
    &sink_port->sensor_cap);
  if (SENSOR_CID_CH_MAX - 1 < primary_cid_idx) {
    CDBG_ERROR("%s: primary_cid_idx = %d, max allowed %d", __func__,
      primary_cid_idx, SENSOR_CID_CH_MAX);
    return -1;
  }

  /* set vfe ioctl to kernel*/
  memset(cfg_cmd, 0, sizeof(struct ispif_cfg_data));
  cfg_cmd->cfg_type = ISPIF_SET_VFE_INFO;
  cfg_cmd->vfe_info.num_vfe = num_isps;
  for (i = 0; i < num_isps; i++) {
    cfg_cmd->vfe_info.info[i].max_resolution = iface->isp_axi_data.isp_subdev[i].hw_cap.max_resolution;//    isp_info[i].max_resolution;
    cfg_cmd->vfe_info.info[i].id = iface->isp_axi_data.isp_subdev[i].hw_cap.isp_id;
    cfg_cmd->vfe_info.info[i].ver = iface->isp_axi_data.isp_subdev[i].isp_version;
  }
  rc = ioctl(ispif->fd, VIDIOC_MSM_ISPIF_CFG, cfg_cmd);
  if (rc < 0) {
    CDBG_ERROR("%s: ISPIF_SET_VFE_INFO kernel return error = %d", __func__, rc);
    return rc;
  }

  /* ispif init cmd to kernel*/
  memset(cfg_cmd, 0, sizeof(struct ispif_cfg_data));
  cfg_cmd->cfg_type = ISPIF_INIT;
  cfg_cmd->csid_version =
    sink_port->sensor_cap.sensor_cid_ch[primary_cid_idx].csid_version;
  rc = ioctl(ispif->fd, VIDIOC_MSM_ISPIF_CFG, cfg_cmd);
  if (rc != -EAGAIN && rc != 0) {
    /* error in ioctl init */
    CDBG_ERROR("%s: ISPIF_INIT, kernel return error = %d", __func__, rc);
    return rc;
  }

  return rc;
}


/** iface_ispif_util_find_primary_cid:
 *    @sensor_cfg: Sensor configuration
 *    @sensor_cap: Sensor source port capabilities
 *
 *  This function runs in MCTL thread context.
 *
 *  This function finds primary CID for sensor source port by format
 *
 *  Return: Array index of the primary CID
 **/
uint32_t iface_ispif_util_find_primary_cid(sensor_out_info_t *sensor_cfg,
  sensor_src_port_cap_t *sensor_cap)
{
  int i;
  for(i = 0; i < sensor_cap->num_cid_ch; i++)
    if (sensor_cfg->fmt == sensor_cap->sensor_cid_ch[i].fmt)
      break;

  if(i == sensor_cap->num_cid_ch) {
    CDBG_ERROR("%s:%d error cannot find primary sensor format", __func__,
      __LINE__);

    return(0);
  }

  return(i);
}

/** iface_ispif_destroy:
 *    @ispif: ispif pointer
 *
 *  This function runs in MCTL thread context.
 *
 *  This stub function will contain code to destroy ispif module.
 *
 *  Return:  None
 **/
void iface_ispif_destroy (iface_ispif_t *ispif __unused)
{
  return;
}

/** iface_ispif_dump_config:
*    @ispif: ispif pointer
*
*  Return:  0 - Success
*           negative value - unsuccessful start of streaming
**/
static void iface_dump_ispif_config(struct msm_ispif_param_data *ispif_config_parm)
{
   uint32_t i;
   int j;

   if (ispif_config_parm == NULL || ispif_config_parm->num <= 0) {
      CDBG_ERROR("%s: error, no ispif config in config parm\n", __func__);
      return;
   }

   for (i = 0; i < ispif_config_parm->num; i++) {
     CDBG_HIGH("%s:  vfe_intf %d(0-VFE0, 1-VFE1)\n", __func__,
       ispif_config_parm->entries[i].vfe_intf);
     CDBG_HIGH("%s: intftype = %d(0/2-PIX0,PIX1, 1/3/4 - RDI0,RDI1,RDI2)\n",
       __func__, ispif_config_parm->entries[i].intftype);
     CDBG_HIGH("%s: num_cids = %d\n", __func__,
      ispif_config_parm->entries[i].num_cids);
     for (j = 0; j < ispif_config_parm->entries[i].num_cids; j++) {
        CDBG_HIGH("%s: cids[%d] = %d\n", __func__, j,
          ispif_config_parm->entries[i].cids[j]);
     }
     CDBG_HIGH("%s: csid = %d\n", __func__, ispif_config_parm->entries[i].csid);
     CDBG_HIGH("%s: crop_enable = %d, fisrt pixel = %d, last pixel = %d\n",
       __func__, ispif_config_parm->entries[i].crop_enable,
       ispif_config_parm->entries[i].crop_start_pixel,
       ispif_config_parm->entries[i].crop_end_pixel);
   }

}

/** iface_ispif_get_cfg_params_from_hw_streams:
 *    @iface: iface pointer
 *    @session: pointer to ispif session
 *    @num_streams: number of streams
 *    @stream_ids: stream ids
 *    @cfg_cmd: output array of cfg params
 *    @start: if stream start or stop
 *
 *  This function prepares the cfg_cmd array with params for
 *  starting/stopping intf
 *
 *  Return:  0 - Success
 *           negative value - unsuccessful start of streaming
 **/
int iface_ispif_get_cfg_params_from_hw_streams(
  iface_t               *iface,
  iface_port_t          *iface_sink_port __unused,
  iface_session_t       *session,
  int                    num_hw_streams,
  uint32_t              *hw_stream_ids,
  struct ispif_cfg_data *cfg_cmd,
  boolean                start)
{
  int rc = 0, isp_id, i;
  uint32_t k;
  uint32_t *active_count = NULL;
  iface_hw_stream_t *hw_stream = NULL;
  iface_ispif_t *ispif = &iface->ispif_data.ispif_hw;

  memset(cfg_cmd, 0, sizeof(struct ispif_cfg_data));

  for (i = 0; i < num_hw_streams; i++) {
    hw_stream =  iface_util_find_hw_stream_in_session(session, hw_stream_ids[i]);
    if (hw_stream == NULL) {
      CDBG_ERROR("%s: stream not exist. error\n", __func__);
      goto error;
    }

    CDBG("%s: config hw_stream id = %x use_pix %d,"
      " vfe_intf_mask %x, axi_path %d, num_cid %d\n",
      __func__, hw_stream->hw_stream_id, hw_stream->use_pix,
      hw_stream->interface_mask, hw_stream->axi_path, hw_stream->num_cids);

    for (isp_id = 0; isp_id < VFE_MAX; isp_id++) {
      if ((hw_stream->interface_mask & (0xffff << (16 * isp_id))) == 0) {
         CDBG("%s: hw stream %x does not use vfe%d interface\n",
           __func__, hw_stream->hw_stream_id, isp_id);
         continue;
       }

      CDBG("%s: config VFE%d for hw stream id %x\n",
        __func__, isp_id, hw_stream->hw_stream_id);
      /* hi 16 bits for ISP1 and low 16 bits for ISP0 */
      if ((1 << isp_id) & session->session_resource.isp_id_mask)
        cfg_cmd->params.entries[cfg_cmd->params.num].vfe_intf =
          (enum msm_ispif_vfe_intf)isp_id;
      else
        continue;

      /*dual vfe configuration and only for pix interface*/
      if (session->session_resource.ispif_split_info.is_split &&
          hw_stream->use_pix == 1) {
        ispif_out_info_t* split_info = &session->session_resource.ispif_split_info;
        if (isp_id == VFE0) {
          /* left half image */
          cfg_cmd->params.entries[cfg_cmd->params.num].crop_enable       = 1;
          cfg_cmd->params.entries[cfg_cmd->params.num].crop_start_pixel  = 0;
          cfg_cmd->params.entries[cfg_cmd->params.num].crop_end_pixel    =
            split_info->right_stripe_offset + split_info->overlap - 1;
        } else {
          cfg_cmd->params.entries[cfg_cmd->params.num].crop_enable      = 1;
          cfg_cmd->params.entries[cfg_cmd->params.num].crop_start_pixel =
            split_info->right_stripe_offset;
          cfg_cmd->params.entries[cfg_cmd->params.num].crop_end_pixel   =
            session->sensor_out_info.dim_output.width - 1;
        }
      }

      cfg_cmd->params.entries[cfg_cmd->params.num].intftype =
        iface_util_find_isp_intf_type(hw_stream, isp_id);
      if (cfg_cmd->params.entries[cfg_cmd->params.num].intftype == INTF_MAX) {
        CDBG_ERROR("%s:error! invalid ispif interface mask INTF_MAX", __func__);
        memset(&cfg_cmd->params.entries[cfg_cmd->params.num], 0,
           sizeof(cfg_cmd->params.entries[cfg_cmd->params.num]));
        continue;
      }

      active_count = &(ispif->interface_state[isp_id].
        active_count[cfg_cmd->params.entries[cfg_cmd->params.num].intftype]);

      if (start) {
        ispif->num_active_streams++;
        ispif->interface_state[isp_id].
        active_count[cfg_cmd->params.entries[cfg_cmd->params.num].intftype]++;

        /* check if this isp, this interface already config or not*/
        if (active_count && *active_count > 1) {
          IFACE_HIGH("interface %d already active\n",
            cfg_cmd->params.entries[cfg_cmd->params.num].intftype);
          memset(&cfg_cmd->params.entries[cfg_cmd->params.num], 0,
            sizeof(cfg_cmd->params.entries[cfg_cmd->params.num]));
          continue;
        }
      } else {
        ispif->num_active_streams--;
        ispif->interface_state[isp_id].
        active_count[cfg_cmd->params.entries[cfg_cmd->params.num].intftype]--;

        /* active count--, only when active count ==0, we turn off this interface*/
        if (active_count && *active_count > 0) {
          IFACE_HIGH("interace %d active count = %d, skip stop ispif \n",
            cfg_cmd->params.entries[cfg_cmd->params.num].intftype, *active_count);

          memset(&cfg_cmd->params.entries[cfg_cmd->params.num], 0,
            sizeof(cfg_cmd->params.entries[cfg_cmd->params.num]));
          continue;
        }
      }

      /* Note, in current implementation, sensor put all CIDs into one array
       * with following order: 0 - x-1 : non meta CIDs
       * Start with x meta CIDs are provided.
       */

     cfg_cmd->params.entries[cfg_cmd->params.num].num_cids =
     hw_stream->num_cids;
      for (k = 0; k < hw_stream->num_cids; k++) {
        cfg_cmd->params.entries[cfg_cmd->params.num].cids[k] =
          hw_stream->cids[k];
        cfg_cmd->params.entries[cfg_cmd->params.num].csid =
          hw_stream->csid;
      }

      /* one entry added */
      cfg_cmd->params.num++;
      if (cfg_cmd->params.num > MAX_PARAM_ENTRIES) {
        CDBG_ERROR("%s: error, parm entries %d > max value %d\n",
          __func__, cfg_cmd->params.num, MAX_PARAM_ENTRIES);
        rc = -1;
        goto error;
      }
    }
  }

error:
  return rc;
}

/** iface_ispif_proc_streamon:
 *    @iface: iface pointer
 *    @session: pointer to ispif session
 *    @ispif_sink_port: ispif sink port
 *    @num_streams: number of streams to be started
 *    @stream_ids: stream ids
 *
 *  This function runs in MCTL thread context.
 *
 *  This function start the streaming in ispif module
 *
 *  Return:  0 - Success
 *           negative value - unsuccessful start of streaming
 **/
int iface_ispif_proc_streamon(iface_t *iface, iface_session_t *session,
  iface_port_t *iface_sink_port, int num_hw_streams, uint32_t *hw_stream_ids)
{
  int i, rc = 0;
  uint32_t num_cid_ch_non_meta = 0;
  iface_ispif_t *ispif = &iface->ispif_data.ispif_hw;
  struct ispif_cfg_data *cfg_cmd = &ispif->cfg_cmd;
  boolean start = TRUE;

  CDBG("%s: E", __func__);

  /* start ispif */
  if (ispif->fd <= 0) {
    ispif->fd = open(iface->ispif_data.subdev_name, O_RDWR | O_NONBLOCK);
    if (ispif->fd <= 0) {
      CDBG_ERROR("%s: cannot open ispif '%s'\n", __func__, iface->ispif_data.subdev_name);
      return -1;
    }
  }

  pthread_mutex_lock(&ispif->mutex);
  if (ispif->num_active_streams == 0) {
    /* first streamon, reset ispif */
    rc = iface_ispif_hw_reset(iface, iface_sink_port, session);
    if (rc < 0) {
      CDBG_ERROR("%s: ispif_hw_reset failed", __func__);
      pthread_mutex_unlock(&ispif->mutex);
      return rc;
    }

    CDBG_HIGH("%s: ispif_hw_reset done\n", __func__);
  }

  memset(cfg_cmd, 0, sizeof(struct ispif_cfg_data));

  rc = iface_ispif_get_cfg_params_from_hw_streams(iface, iface_sink_port,
    session, num_hw_streams, hw_stream_ids, cfg_cmd, start);
  if (rc < 0) {
    CDBG_ERROR("%s:%d Error preparing cfg_cmd %d \n", __func__, __LINE__, rc);
    goto error;
  }

  if (cfg_cmd->params.num > 0) {
    iface_dump_ispif_config(&cfg_cmd->params);
    cfg_cmd->cfg_type = ISPIF_CFG;
    rc = ioctl(ispif->fd, VIDIOC_MSM_ISPIF_CFG, cfg_cmd);
    if (rc != 0) {
      CDBG_ERROR("%s: ISPIF_CFG error = %d\n", __func__, rc);
      goto error;
    }

    cfg_cmd->cfg_type = ISPIF_START_FRAME_BOUNDARY;
    rc = ioctl(ispif->fd, VIDIOC_MSM_ISPIF_CFG, cfg_cmd);
    if (rc != 0) {
      CDBG_ERROR("%s: ISPIF_START_FRAME_BOUNDARY error = %d\n", __func__, rc);
      goto error;
    }

    CDBG("%s: START_FRAME_BOUNDARY done\n", __func__);
  }
  pthread_mutex_unlock(&ispif->mutex);
  CDBG("%s: X, rc = %d", __func__, rc);
  return rc;

error:
  pthread_mutex_unlock(&ispif->mutex);
  CDBG("%s:ERROR, rc = %d\n", __func__, rc);
  /* need to roll back */
  if (ispif->num_active_streams == 0) {
    if (ispif->fd > 0) {
      close(ispif->fd);
      ispif->fd = 0;
    }
  }

  return rc;
}

/** iface_ispif_proc_streamoff:
 *    @ispif: pointer to ispif instance
 *    @session: ispif session
 *    @num_streams:  number ot streams
 *    @stream_ids: array of stream ids
 *
 *  This function runs in MCTL thread context.
 *
 * This function stops ispif streaming
 *
 *  Return:  0 - Success
 *          -1 - Cannot find stream or
 *               invalid interface mask
 *               VIDIOC_MSM_ISPIF_CFG ioctl returned error
 *        -100 - Try to stop two non meta- CIDs with one stream
 *        -200 - Too many parameter entries
 **/
int32_t iface_ispif_streamoff(iface_t *iface, iface_session_t *session,
  iface_port_t *iface_sink_port, uint32_t num_hw_streams, uint32_t *hw_stream_ids)
{
  int32_t rc = 0, i;
  boolean start = FALSE;
  iface_ispif_t *ispif = &iface->ispif_data.ispif_hw;
  struct ispif_cfg_data *cfg_cmd = &ispif->cfg_cmd;

  CDBG("%s: Enter\n", __func__);

  memset(cfg_cmd, 0, sizeof(struct ispif_cfg_data));

  rc = iface_ispif_get_cfg_params_from_hw_streams(iface, iface_sink_port,
    session, num_hw_streams, hw_stream_ids, cfg_cmd, start);

  /* if we had a timeout error while stopping previous stream, the ISPIF is
     already in bad state, so there is no point in stopping other streams
     as well. The ISPIF will recover during HW reset at streamon. */
  if (cfg_cmd->params.num > 0) {
    cfg_cmd->cfg_type = ISPIF_STOP_FRAME_BOUNDARY;
    rc = ioctl(ispif->fd, VIDIOC_MSM_ISPIF_CFG, cfg_cmd);

    if(rc != 0) {
      if (errno == ETIMEDOUT) {
        CDBG_ERROR("%s, error - ISPIF stop on frame boundary timed out!!!!!",
          __func__);
        /*  ISPIF couldn't stop properly.
            We initiate recovery sequence now */
        mct_bus_msg_t bus_msg;
        mct_bus_msg_error_message_t err_msg;
        memset(&bus_msg, 0, sizeof(bus_msg));

        /* Close device */
        if (ispif->fd > 0) {
          close(ispif->fd);
          ispif->fd = 0;
        }
        session->streamoff_error = TRUE;
        /* Bus message to MCTL for each active session */
        for (i = 0; i < IFACE_MAX_SESSIONS; i++) {
          if (iface->sessions[i].iface != NULL) {
            bus_msg.sessionid = iface->sessions[i].session_id;
            bus_msg.type = MCT_BUS_MSG_SEND_HW_ERROR;
            if (TRUE != mct_module_post_bus_msg(iface->module, &bus_msg))
              CDBG_ERROR("%s: MCT_BUS_MSG_SEND_HW_ERROR to bus error\n", __func__);
          }
        }

        return rc;
      } else {
        CDBG_ERROR("%s: ISPIF_CFG error = %d\n", __func__, rc);
        assert(fd == 0);

        return rc;
      }
    }

    if (ispif->num_active_streams == 0) {
      /* no more active stream close ispif */
      if (ispif->fd > 0) {
        close(ispif->fd);
        ispif->fd = 0;
      }
    }
  }

end:
  CDBG("%s: X, rc = %d\n", __func__, rc);

  return rc;

}
