/*============================================================================
   Copyright (c) 2012 - 2015 Qualcomm Technologies, Inc. All Rights Reserved.
   Qualcomm Technologies Proprietary and Confidential.

   This file defines the media/module/master controller's interface with the
   DSPS modules. The functionalities od this module include:

   1. Control communication with the sensor module
   2. Process data received from the sensors

============================================================================*/
#include "dsps_hw.h"
#ifdef FEATURE_GYRO_DSPS
#include <sensor1.h>
#include <sns_smgr_api_v01.h>
#include <sns_time_api_v02.h>
#include <sns_reg_api_v02.h>
#include "sns_sam_gravity_vector_v01.h"


void dsps_sensor1_callback(intptr_t *data, sensor1_msg_header_s *msg_hdr,
  sensor1_msg_type_e msg_type, void *msg_ptr);
#endif


/*===========================================================================
 * FUNCTION      dsps_close
 *
 * DESCRIPTION   Close a connection with the sensor framework
 *==========================================================================*/
int dsps_close(sensor1_config_t *dsps_config)
{
  if (dsps_config == NULL)
    return -1;
#ifdef FEATURE_GYRO_DSPS
  if (sensor1_close(dsps_config->handle) != SENSOR1_SUCCESS)
    return -1;
#else
  return -1;
#endif
  return 0;
}


/*===========================================================================
 * FUNCTION      dsps_disconnect
 *
 * DESCRIPTION   Deregister an mctl client with the DSPS Thread
 *=========================================================================*/
int dsps_disconnect(void * sensor_config)
{
  int rc = 0;
  sensor1_config_t * dsps_config = (sensor1_config_t *)sensor_config;

  if (dsps_close(dsps_config) < 0) {
    IS_ERR("Error in closing sensor connection");
    rc = -1;
  }
  pthread_mutex_destroy(&(dsps_config->callback_mutex));
  pthread_cond_destroy(&(dsps_config->callback_condvar));
  pthread_mutex_destroy(&(dsps_config->thread_mutex));
  pthread_cond_destroy(&(dsps_config->thread_condvar));

  return rc;
}

/*===========================================================================
 * FUNCTION      dsps_open
 *
 * DESCRIPTION   Open a new connection with the sensor framework
 *==========================================================================*/
int dsps_open(void *sensor_config)
{
  sensor1_config_t *dsps_config = (sensor1_config_t *)sensor_config;
#ifdef FEATURE_GYRO_DSPS
  /* Open sensor1 port */
  IS_LOW("try to sensor1_open()");
  if (sensor1_open(&dsps_config->handle,
      (sensor1_notify_data_cb_t)&dsps_sensor1_callback,
      (intptr_t)dsps_config) == SENSOR1_SUCCESS)
    return 0;
#endif
  return -1;
}


#ifdef FEATURE_GYRO_DSPS
/*===========================================================================
 * FUNCTION      dsps_set_expiry_time
 *
 * DESCRIPTION   Set the expiry time for timed wait by adding timeout
 *               value to current time.
 *==========================================================================*/
void dsps_set_expiry_time(struct timespec *expiry_time)
{
  struct timeval current_time;

  gettimeofday(&current_time, NULL);
  expiry_time->tv_sec = current_time.tv_sec;
  expiry_time->tv_nsec = current_time.tv_usec * NSEC_PER_USEC;
  expiry_time->tv_sec += SENSOR_TIME_OUT * MSEC_TO_SEC;
  expiry_time->tv_sec += (expiry_time->tv_nsec + (SENSOR_TIME_OUT % SEC_TO_MSEC)
    * MSEC_TO_NSEC) / NSEC_PER_SEC;
  expiry_time->tv_nsec += (SENSOR_TIME_OUT % SEC_TO_MSEC) * MSEC_TO_NSEC;
  expiry_time->tv_nsec %= NSEC_PER_SEC;
}

/*===========================================================================
 * FUNCTION      dsps_wait_for_response
 *
 * DESCRIPTION   Wait for response from sensor until timer expires.
 *               Condition variable here is signaled from
 *               dsps_process_response()
 *==========================================================================*/
int dsps_wait_for_response(sensor1_config_t *dsps_config)
{
  int ret;
  struct timespec expiry_time;

  dsps_set_expiry_time(&expiry_time);

  pthread_mutex_lock(&(dsps_config->callback_mutex));

  /* Check if callback has already arrived */
  if (dsps_config->callback_arrived == 1) {
    IS_LOW("Callback received before wait\n");
    ret = 0;
    goto end;
  }

  /* Timed wait for callback */
  ret = pthread_cond_timedwait(&(dsps_config->callback_condvar),
    &(dsps_config->callback_mutex), &expiry_time);

  if ((!ret) && (dsps_config->callback_arrived == 0)) {
    IS_ERR("Error! Timed wait returned without callback.\n");
    ret = -1;
  }

end:
  pthread_mutex_unlock(&(dsps_config->callback_mutex));
  return ret;
}


/** dsps_get_gyro_samples:
 *    @data: gyro data from sensor
 *    @dsps_obj: dsps object (client handle)
 *    @cb_data: memory to store the gyro data
 *
 * This function transfers gyro data from sensors' memory to that of camera's.
 **/
void dsps_get_gyro_samples(void *data, sensor1_config_t *dsps_obj,
  dsps_cb_data_t *cb_data)
{
  uint8_t i, sample_len;
  uint32_t timestamp;
  sns_smgr_buffering_query_ind_msg_v01 *sensor_data = data;

  sample_len = (sensor_data->Samples_len > STATS_GYRO_MAX_SAMPLE_BUFFER_SIZE) ?
    STATS_GYRO_MAX_SAMPLE_BUFFER_SIZE : sensor_data->Samples_len;
  cb_data->u.gyro.sample_len = sample_len;

  timestamp = sensor_data->FirstSampleTimestamp;

  for (i = 0; i < sample_len; i++) {
    timestamp += sensor_data->Samples[i].TimeStampOffset;
    cb_data->u.gyro.sample[i].timestamp = (uint64_t)timestamp *
      USEC_PER_SEC / DSPS_HZ + dsps_obj->dsps_time_state.ts_offset;
    IS_LOW("timestamp (ticks) = %u, timestamp (us) = %llu, ts_offset = %llu",
       timestamp, cb_data->u.gyro.sample[i].timestamp,
      dsps_obj->dsps_time_state.ts_offset);
    cb_data->u.gyro.sample[i].value[0] = sensor_data->Samples[i].Data[0];
    cb_data->u.gyro.sample[i].value[1] = sensor_data->Samples[i].Data[1];
    cb_data->u.gyro.sample[i].value[2] = sensor_data->Samples[i].Data[2];
  }
}


/** dsps_prepare_req_header_gyro:
 *    @req_hdr: request header
 *    @msg_data: request message data
 *
 * This function prepares the header for the request message.
 **/
static void dsps_prepare_req_header_gyro(sensor1_msg_header_s *req_hdr,
    sensor1_req_data_t *msg_data)
{
  /* Prepare Message Header */
  switch (msg_data->msg_type) {
  case DSPS_ENABLE_REQ:
  case DSPS_DISABLE_REQ:
    req_hdr->service_number = SNS_SMGR_SVC_ID_V01;
    req_hdr->msg_id = SNS_SMGR_BUFFERING_REQ_V01;
    req_hdr->msg_size = sizeof(sns_smgr_buffering_req_msg_v01);
    req_hdr->txn_id = 0;
    break;

  case DSPS_GET_REPORT:
    req_hdr->service_number = SNS_SMGR_SVC_ID_V01;
    req_hdr->msg_id = SNS_SMGR_BUFFERING_QUERY_REQ_V01;
    req_hdr->msg_size = sizeof(sns_smgr_buffering_query_req_msg_v01);
    req_hdr->txn_id = msg_data->u.gyro.seqnum;
    break;

  case DSPS_TIMESTAMP_REQ:
    req_hdr->service_number = SNS_TIME2_SVC_ID_V01;
    req_hdr->msg_id = SNS_TIME_TIMESTAMP_REQ_V02;
    req_hdr->msg_size = sizeof(sns_time_timestamp_req_msg_v02);
    req_hdr->txn_id = 0;
    break;

  default:
     IS_ERR("Invalid type");
  }
}


/*===========================================================================
 * FUNCTION      dsps_prepare_req_header_gravity
 *
 * DESCRIPTION   Prepare header for a request message
 *==========================================================================*/
static void dsps_prepare_req_header_gravity(sensor1_msg_header_s *req_hdr,
    sensor1_req_data_t *msg_data)
{
  /* Prepare Message Header */
  switch (msg_data->msg_type) {
    case DSPS_ENABLE_REQ:
      req_hdr->service_number = SNS_SAM_GRAVITY_VECTOR_SVC_ID_V01;
      req_hdr->msg_id = SNS_SAM_GRAVITY_ENABLE_REQ_V01;
      req_hdr->msg_size = sizeof(sns_sam_gravity_enable_req_msg_v01);
      req_hdr->txn_id = 0;
      break;
    case DSPS_DISABLE_REQ:
      req_hdr->service_number = SNS_SAM_GRAVITY_VECTOR_SVC_ID_V01;
      req_hdr->msg_id = SNS_SAM_GRAVITY_DISABLE_REQ_V01;
      req_hdr->msg_size = sizeof(sns_sam_gravity_disable_req_msg_v01);
      req_hdr->txn_id = 0;
      break;
    case DSPS_GET_REPORT:
      req_hdr->service_number = SNS_SAM_GRAVITY_VECTOR_SVC_ID_V01;
      req_hdr->msg_id = SNS_SAM_GRAVITY_GET_REPORT_REQ_V01;
      req_hdr->msg_size = sizeof(sns_sam_gravity_get_report_req_msg_v01);
      req_hdr->txn_id = 0;
      break;
    default:
       IS_ERR("Invalid type");
  }
}


/*===========================================================================
 * FUNCTION      dsps_prepare_req_header
 *
 * DESCRIPTION   Prepare header for a request message
 *==========================================================================*/
static void dsps_prepare_req_header(sensor1_msg_header_s *req_hdr,
    sensor1_req_data_t *msg_data)
{
  int rc = 0;
  switch (msg_data->sensor_type) {
  case DSPS_DATA_TYPE_GYRO:
    dsps_prepare_req_header_gyro(req_hdr, msg_data);
    break;
  case DSPS_DATA_TYPE_GRAVITY_VECTOR:
    dsps_prepare_req_header_gravity(req_hdr, msg_data);
    break;
  default:
     IS_ERR("Sensor type %d not supported yet!",
      msg_data->sensor_type);
    rc = -1;
    break;
  }
}


/** dsps_prepare_req_msg_gyro:
 *    @dsps_obj: dsps object (client handle)
 *    @msg_data: request message data
 *
 * This function fills in the request message.
 * This function assumes that sensor1_alloc_msg_buf returns 0-initialized
 * memory (calloc).
 **/
static void *dsps_prepare_req_msg_gyro(sensor1_config_t *dsps_obj,
  sensor1_req_data_t *msg_data)
{
  int size = 0;
  sensor1_error_e err;
  void *req_msg = NULL;
  sns_smgr_buffering_req_msg_v01 *req;
  sns_smgr_buffering_query_req_msg_v01 *query_req;

  if (msg_data->msg_type == DSPS_GET_REPORT) {
    size = sizeof(sns_smgr_buffering_query_req_msg_v01);
    err = sensor1_alloc_msg_buf(dsps_obj->handle, size, (void *)&query_req);
    if (err == SENSOR1_SUCCESS) {
      query_req->QueryId = SNS_SMGR_ID_GYRO_V01;
      query_req->QueryId |= (uint16_t)(msg_data->u.gyro.seqnum << 8) & 0xFF00;
      query_req->SensorId = SNS_SMGR_ID_GYRO_V01;
      query_req->DataType = SNS_SMGR_DATA_TYPE_PRIMARY_V01;
      query_req->TimePeriod[0] =
        (msg_data->u.gyro.t_start - dsps_obj->dsps_time_state.ts_offset) *
        NSEC_PER_USEC * DSPS_HZ / NSEC_PER_SEC;
      query_req->TimePeriod[1] =
        (msg_data->u.gyro.t_end - dsps_obj->dsps_time_state.ts_offset) *
        NSEC_PER_USEC * DSPS_HZ / NSEC_PER_SEC;
       IS_LOW("QueryId = 0x%x, seqnum = 0x%x, t_start = %llu, t_end = %llu,"
        " ts_offset = %llu", query_req->QueryId,
        msg_data->u.gyro.seqnum, msg_data->u.gyro.t_start,
        msg_data->u.gyro.t_end, dsps_obj->dsps_time_state.ts_offset);
       IS_LOW("TimePeriod[0] = %u, TimePeriod[1] = %u",
        query_req->TimePeriod[0], query_req->TimePeriod[1]);
      req_msg = query_req;
    }
  } else if (msg_data->msg_type == DSPS_TIMESTAMP_REQ) {
    size = sizeof(sns_time_timestamp_req_msg_v02);
    err = sensor1_alloc_msg_buf(dsps_obj->handle, size, &req_msg);
    if (err != SENSOR1_SUCCESS) {
      req_msg = NULL;
    }
  } else if (msg_data->msg_type == DSPS_ENABLE_REQ ||
             msg_data->msg_type == DSPS_DISABLE_REQ) {
    size = sizeof(sns_smgr_buffering_req_msg_v01);
    err = sensor1_alloc_msg_buf(dsps_obj->handle, size, (void *)&req);
    if (err == SENSOR1_SUCCESS) {
      req->ReportId = SNS_SMGR_ID_GYRO_V01;
      if (msg_data->msg_type == DSPS_ENABLE_REQ) {
        req->Action = SNS_SMGR_BUFFERING_ACTION_ADD_V01;
        req->ReportRate = 0;
        req->Item_len = 1;
        req->Item[0].SensorId = SNS_SMGR_ID_GYRO_V01;
        req->Item[0].DataType = SNS_SMGR_DATA_TYPE_PRIMARY_V01;
        req->Item[0].Decimation = SNS_SMGR_DECIMATION_FILTER_V01;
        req->Item[0].Calibration = SNS_SMGR_CAL_SEL_FULL_CAL_V01;
        req->Item[0].SamplingRate = msg_data->u.gyro.gyro_sample_rate;
        req->Item[0].SampleQuality =
          SNS_SMGR_SAMPLE_QUALITY_ACCURATE_TIMESTAMP_V01;
        req->notify_suspend_valid = FALSE;
        req->SrcModule_valid = FALSE;
      } else {
        req->Action = SNS_SMGR_BUFFERING_ACTION_DELETE_V01;
      }
      req_msg = req;
    }
  } else {
     IS_ERR("Invalid type");
  }

  return req_msg;
}


/*===========================================================================
 * FUNCTION      dsps_prepare_req_msg_gravity
 *
 * DESCRIPTION   Prepare body of a request message
 *==========================================================================*/
static void *dsps_prepare_req_msg_gravity(sensor1_config_t *dsps_config,
  sensor1_req_data_t *msg_data)
{
  int size = 0;
  int rc = 0;
  sensor1_error_e error;
  void *req_msg = NULL;

   IS_LOW("Prepare Request message of type : %d",
    msg_data->msg_type);

  switch (msg_data->msg_type) {
  case DSPS_ENABLE_REQ: {
    sns_sam_gravity_enable_req_msg_v01 *enable_req_msg;
    size = sizeof(sns_sam_gravity_enable_req_msg_v01);
    error = sensor1_alloc_msg_buf(dsps_config->handle, size, &req_msg);
    if (error != SENSOR1_SUCCESS) {
       IS_ERR("DSPS_ENABLE_REQ: Error allocating buffer %d\n",
        error);
      return NULL;
    }

    /* Gravity vector enable request message has following fields:
     * Mandatory -
     * report_period: ouput rate - units of seconds, Q16. 0 to report
     *   at sampling rate.
     * Optional -
     * sample_rate_valid: true if sample rate to be passed.
     * sample_rate: in Hz, Q16. If less than report rate, set to report rate
     * notify_suspend_valid: true if notify_suspend is being passed.
     * notify_suspend: send indicaiton for the request when the processor
     *   is in suspend state.
     **/
    enable_req_msg = (sns_sam_gravity_enable_req_msg_v01 *)req_msg;
    enable_req_msg->report_period = msg_data->u.gravity.report_period;
    if (msg_data->u.gravity.sample_rate_valid) {
      enable_req_msg->sample_rate_valid = TRUE;
      enable_req_msg->sample_rate = msg_data->u.gravity.sample_rate;
    }
  }
    break;

  case DSPS_DISABLE_REQ: {
    sns_sam_gravity_disable_req_msg_v01 *disable_req_msg;
    size = sizeof(sns_sam_gravity_disable_req_msg_v01);
    error = sensor1_alloc_msg_buf(dsps_config->handle, size, &req_msg);
    if (error != SENSOR1_SUCCESS) {
       IS_ERR("DSPS_DISABLE_REQ: Error allocating buffer %d\n",
         error);
      return NULL;
    }
    disable_req_msg = (sns_sam_gravity_disable_req_msg_v01 *)req_msg;
    disable_req_msg->instance_id = dsps_config->instance_id_gravity;
  }
    break;

  case DSPS_GET_REPORT: {
    sns_sam_gravity_get_report_req_msg_v01 *get_report_req_msg;
    size = sizeof(sns_sam_gravity_get_report_req_msg_v01);
    error = sensor1_alloc_msg_buf(dsps_config->handle, size, &req_msg);
    if (error != SENSOR1_SUCCESS) {
       IS_ERR("DSPS_GET_REPORT: Error allocating buffer %d\n",
        error);
      return NULL;
    }

    get_report_req_msg = (sns_sam_gravity_get_report_req_msg_v01 *)req_msg;
    get_report_req_msg->instance_id = dsps_config->instance_id_gravity;
  }
    break;

  default:
     IS_ERR("Invalid type");
    return NULL;
  }

  return req_msg;
} /* dsps_prepare_req_msg_gravity */


/*===========================================================================
 * FUNCTION      dsps_prepare_req_msg
 *
 * DESCRIPTION   Prepare body of a request message
 *==========================================================================*/
void *dsps_prepare_req_msg(sensor1_config_t *dsps_config,
    sensor1_req_data_t *msg_data)
{
  int size = 0;
  void *req_msg = NULL;

  switch (msg_data->sensor_type) {
  case DSPS_DATA_TYPE_GYRO:
    req_msg = dsps_prepare_req_msg_gyro(dsps_config, msg_data);
    break;
  case DSPS_DATA_TYPE_GRAVITY_VECTOR:
    req_msg = dsps_prepare_req_msg_gravity(dsps_config, msg_data);
    break;
  default:
     IS_ERR("Sensor type %d not supported yet!",
      msg_data->sensor_type);
    break;
  }

  return req_msg;
} /* dsps_prepare_req_msg_gravity */


/*===========================================================================
 * FUNCTION      dsps_send_request
 *
 * DESCRIPTION   Send a request message to the sensor framework.
 *               Typically used for adding and deleting reports
 *==========================================================================*/
int dsps_send_request(void *sensor_config,
  void *req_data, int wait)
{
  sensor1_error_e error;
  sensor1_msg_header_s req_hdr;
  void *req_msg = NULL;
  sensor1_config_t *dsps_config = (sensor1_config_t *)sensor_config;
  sensor1_req_data_t *msg_data = (sensor1_req_data_t *)req_data;


  req_msg = dsps_prepare_req_msg(dsps_config, msg_data);
  if (req_msg == NULL) {
     IS_ERR("Error preparing request message!");
    return -1;
  }

  dsps_prepare_req_header(&req_hdr, msg_data);

  dsps_config->error = 0;
  dsps_config->callback_arrived = 0;

  error = sensor1_write(dsps_config->handle, &req_hdr, req_msg);
  if (error != SENSOR1_SUCCESS) {
     IS_ERR("Error writing request message\n");
    sensor1_free_msg_buf(dsps_config->handle, req_msg);
    return -1;
  }

  if (wait) {
    /* Wait for a response */
    if (dsps_wait_for_response(dsps_config) != 0) {
       IS_ERR("Request response timed out\n");
      return -1;
    }

    if (dsps_config->error)
      return -1;
  }

  return 0;
}


/*===========================================================================
 * FUNCTION      dsps_handle_broken_pipe
 *
 * DESCRIPTION   Handle error condition of broken pipe with the sensor
 *               framework
 *==========================================================================*/
void dsps_handle_broken_pipe(sensor1_config_t *dsps_config)
{
   IS_ERR("Broken Pipe Exception\n");
  pthread_mutex_lock(&(dsps_config->thread_mutex));
  dsps_config->status = DSPS_BROKEN_PIPE;
  pthread_mutex_unlock(&(dsps_config->thread_mutex));
  pthread_cond_signal(&(dsps_config->thread_condvar));
}


/*===========================================================================
 * FUNCTION      dsps_process_repsonse_gyro
 *
 * DESCRIPTION   Process response received from sensor framework.
 *               A response message is in response to a message sent to the
 *               sensor framework. Signal waiting condition variable in
 *               dsps_wait_for_response()
 *=========================================================================*/
static void dsps_process_response_gyro(sensor1_config_t *dsps_obj,
    sensor1_msg_header_s *msg_hdr, void *msg_ptr)
{
  switch (msg_hdr->msg_id) {
  case SNS_SMGR_BUFFERING_RESP_V01: {
    sns_smgr_buffering_resp_msg_v01 *resp = msg_ptr;
    if (resp->Resp.sns_result_t != SNS_RESULT_SUCCESS_V01) {
       IS_ERR("Request denied, error code = %d",
        resp->Resp.sns_err_t);
    }
  }
    break;

  case SNS_SMGR_BUFFERING_QUERY_RESP_V01: {
    sns_smgr_buffering_query_resp_msg_v01 *resp = msg_ptr;
    if (resp->Resp.sns_result_t != SNS_RESULT_SUCCESS_V01) {
       IS_ERR("Query request denied, error code = %d, QId_valid = %d, "
        "QueryId = 0x%x, AckNak_valid = %d, AckNak = %d",
        resp->Resp.sns_err_t, resp->QueryId_valid, resp->QueryId,
        resp->AckNak_valid, resp->AckNak);
    }
  }
    break;

  case SNS_TIME_TIMESTAMP_RESP_V02: {
    sns_time_timestamp_resp_msg_v02 *resp = msg_ptr;
    if (resp->resp.sns_result_t == SNS_RESULT_SUCCESS_V01) {
      if (resp->timestamp_dsps_valid && resp->timestamp_apps_valid) {
        uint64_t apps_us = resp->timestamp_apps / NSEC_PER_USEC;
        uint64_t dsps_us = (uint64_t)resp->timestamp_dsps * USEC_PER_SEC /
          DSPS_HZ;

        dsps_obj->dsps_time_state.ts_offset = apps_us - dsps_us;
        dsps_obj->dsps_time_state.ts_offset_valid = 1;
         IS_LOW("apps_us = %llu, dsps_us = %lld, ts_offset = %llu",
           apps_us, dsps_us, dsps_obj->dsps_time_state.ts_offset);

        if (dsps_us < dsps_obj->dsps_time_state.ts_dsps_prev) {
          dsps_obj->dsps_time_state.ts_dsps_ro_cnt++;
          dsps_obj->dsps_time_state.ts_offset +=
            (dsps_obj->dsps_time_state.ts_dsps_ro_cnt * UINT32_MAX);
        }
        dsps_obj->dsps_time_state.ts_dsps_prev = dsps_us;
      }
    }
  }
    break;

  default:
     IS_ERR("Response not valid");
    break;
  }
  pthread_mutex_lock(&(dsps_obj->callback_mutex));
  dsps_obj->callback_arrived = 1;
  pthread_mutex_unlock(&(dsps_obj->callback_mutex));
  pthread_cond_signal(&(dsps_obj->callback_condvar));
} /* dsps_process_response_gyro */


/*===========================================================================
 * FUNCTION      dsps_process_response_gravity
 *
 * DESCRIPTION   Process response received from sensor framework.
 *               A response message is in response to a message sent to the
 *               sensor framework. Signal waiting condition variable in
 *               dsps_wait_for_response()
 *=========================================================================*/
static void dsps_process_response_gravity(sensor1_config_t *dsps_config,
    sensor1_msg_header_s *msg_hdr, void *msg_ptr)
{
  sns_sam_gravity_enable_resp_msg_v01 *enable_resp_msg;
  sns_sam_gravity_disable_resp_msg_v01 *disable_resp_msg;
  sns_sam_gravity_get_report_resp_msg_v01 *get_report_resp_msg;

  switch (msg_hdr->msg_id) {
    case SNS_SAM_GRAVITY_ENABLE_RESP_V01:
      enable_resp_msg = (sns_sam_gravity_enable_resp_msg_v01*)msg_ptr;
      if (enable_resp_msg->resp.sns_result_t == SNS_RESULT_SUCCESS_V01) {
        if (enable_resp_msg->instance_id_valid) {
          dsps_config->instance_id_gravity = enable_resp_msg->instance_id;
           IS_LOW("Ensable Response Instance ID received: %d",
             dsps_config->instance_id_gravity);
        }
      } else {
         IS_ERR("Enable Request failed (err: %d)",
          enable_resp_msg->resp.sns_err_t);
      }
      break;
    case SNS_SAM_GRAVITY_DISABLE_RESP_V01:
      disable_resp_msg = (sns_sam_gravity_disable_resp_msg_v01*)msg_ptr;
      if (disable_resp_msg->resp.sns_result_t == SNS_RESULT_SUCCESS_V01) {
        if (disable_resp_msg->instance_id_valid)
          dsps_config->instance_id_gravity = INVALID_INSTANCE_ID;
         IS_LOW("Disable Response Instance ID received: %d",
          dsps_config->instance_id_gravity);
      } else {
         IS_ERR("Disable Request failed (err: %d)",
          disable_resp_msg->resp.sns_err_t);
      }
      break;
    case SNS_SAM_GRAVITY_GET_REPORT_RESP_V01:
      get_report_resp_msg = (sns_sam_gravity_get_report_resp_msg_v01 *)msg_ptr;
      if (get_report_resp_msg->resp.sns_result_t == SNS_RESULT_SUCCESS_V01) {
         IS_LOW("Report Request Accepted\n");
      } else {
         IS_ERR("Report Request Denied\n");
      }
      break;
    default:
       IS_ERR("Response not valid");
      break;
  }
  pthread_mutex_lock(&(dsps_config->callback_mutex));
  dsps_config->callback_arrived = 1;
  pthread_mutex_unlock(&(dsps_config->callback_mutex));
  pthread_cond_signal(&(dsps_config->callback_condvar));
} /* dsps_process_response_gravity */


/*===========================================================================
 * FUNCTION      dsps_process_indication_gyro
 *
 * DESCRIPTION   Process indication received from sensor framework.
 *=========================================================================*/
static void dsps_process_indication_gyro(sensor1_config_t *dsps_obj,
    sensor1_msg_header_s *msg_hdr, void *msg_ptr)
{
  sns_smgr_buffering_query_ind_msg_v01 *indication = msg_ptr;

  switch (msg_hdr->msg_id) {
  case SNS_SMGR_BUFFERING_QUERY_IND_V01: {
    dsps_cb_data_t cb_data;
    dsps_get_gyro_samples(indication, dsps_obj, &cb_data);
    cb_data.type = DSPS_DATA_TYPE_GYRO;
    cb_data.u.gyro.seq_no = (indication->QueryId & 0xFF00) >> 8;
    dsps_obj->dsps_callback(dsps_obj->port, &cb_data);
  }
    break;

  default:
     IS_ERR("Invalid Indication ID\n");
    break;
  }
} /* dsps_process_response_gyro */


/*===========================================================================
 * FUNCTION      dsps_process_indication_gravity
 *
 * DESCRIPTION   Process indication received from sensor framework.
 *=========================================================================*/
static void dsps_process_indication_gravity(sensor1_config_t *dsps_config,
  sensor1_msg_header_s *msg_hdr, void *msg_ptr)
{
  switch(msg_hdr->msg_id) {
  case SNS_SAM_GRAVITY_REPORT_IND_V01: {
    sns_sam_gravity_report_ind_msg_v01 *indication =
        (sns_sam_gravity_report_ind_msg_v01 *) msg_ptr;
    sns_sam_gravity_result_s_v01 *gravity_data =
      &indication->result;
    dsps_cb_data_t cb_data;

    cb_data.type = DSPS_DATA_TYPE_GRAVITY_VECTOR;
    memcpy(&cb_data.u.gravity.gravity, gravity_data->gravity,
      sizeof(float) * 3);
    memcpy(&cb_data.u.gravity.lin_accel, gravity_data->lin_accel,
      sizeof(float) *3);
    cb_data.u.gravity.accuracy = gravity_data->accuracy;
    dsps_config->dsps_callback(dsps_config->port, &cb_data);
  }
    break;
  default:
     IS_ERR("Invalid Indication ID\n");
    break;
  }
} /* dsps_process_indication_gravity */


/*===========================================================================
 * FUNCTION      dsps_sensor1_callback
 *
 * DESCRIPTION   Callback function to be registered with the sensor framework.
 *               This will be called in context of the sensor framework.
 *==========================================================================*/
void dsps_sensor1_callback(intptr_t *data,
  sensor1_msg_header_s *msg_hdr,
  sensor1_msg_type_e msg_type,
  void *msg_ptr)
{
  sensor1_config_t *dsps_obj =(sensor1_config_t *)data;
  sensor1_handle_s *handle = dsps_obj->handle;

  switch (msg_type) {
  case SENSOR1_MSG_TYPE_RESP:
    if (msg_hdr->service_number == SNS_SMGR_SVC_ID_V01 ||
        msg_hdr->service_number == SNS_TIME2_SVC_ID_V01) {
      IS_LOW("DSPS Response Received\n");
      dsps_process_response_gyro(dsps_obj, msg_hdr, msg_ptr);
    } else if (msg_hdr->service_number == SNS_SAM_GRAVITY_VECTOR_SVC_ID_V01) {
      IS_LOW("DSPS Gravity Vector Response Received\n");
      dsps_process_response_gravity(dsps_obj, msg_hdr, msg_ptr);
    } else {
       IS_ERR("Response id %d from service %d not supported\n",
        msg_hdr->msg_id, msg_hdr->service_number);
    }
    break;

  case SENSOR1_MSG_TYPE_IND:
    if (msg_hdr->service_number == SNS_SMGR_SVC_ID_V01) {
       IS_LOW("DSPS Gyro Indication Received\n");
      dsps_process_indication_gyro(dsps_obj, msg_hdr, msg_ptr);
    } else if (msg_hdr->service_number == SNS_SAM_GRAVITY_VECTOR_SVC_ID_V01) {
       IS_LOW("DSPS Gravity Vector Indication Received!");
      dsps_process_indication_gravity(dsps_obj, msg_hdr, msg_ptr);
    } else {
       IS_ERR("Unexpected Indication Msg type received ");
    }
    break;

  case SENSOR1_MSG_TYPE_BROKEN_PIPE:
    dsps_handle_broken_pipe(dsps_obj);
    break;

  default:
     IS_ERR("Invalid Message Type\n");
    break;
  }

  if (msg_ptr != NULL) {
    sensor1_free_msg_buf(handle, msg_ptr);
  }
}

void dump_time(const char *id)
{
  struct timespec t_now;

#if !defined(LOG_DEBUG)
  CAM_UNUSED_PARAM(id);
#endif
  clock_gettime( CLOCK_REALTIME, &t_now );
  IS_HIGH("%s, %s, time, %llu, (ms)", __FUNCTION__,id,
       (((int64_t)t_now.tv_sec * 1000) + t_now.tv_nsec/1000000));
}
#endif /* FEATURE_GYRO_DSPS */
