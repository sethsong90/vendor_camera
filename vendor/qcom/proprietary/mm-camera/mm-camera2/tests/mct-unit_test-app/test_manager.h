/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#ifndef TEST_MANAGER_H
#define TEST_MANAGER_H

#include "cam_intf.h"
#include "test_suite_common.h"
#include "tm_interface.h"
#include "test_suite.h"

#include "tree.h"
#include "parser.h"
#include "dict.h"


#define TEST_MANAGER_ALIGN_4K 4096

#define MAX_OUT_BUFS 8

using namespace tm_interface;

#define STR_TYPES_LUT_SIZE  (sizeof(str_types_lut) / sizeof(str_types_t))
#define STR_ARRAY_ENTRIES   10
#define STR_TYPE_NAME_SIZE  32

typedef struct {
  char name[STR_TYPE_NAME_SIZE];
  cam_stream_type_t stream_type;
} str_types_t;

typedef struct {
  uint32_t    module_idx;
  uint32_t    stream_idx;
  void        *instance;

} tm_set_mod_t;

typedef struct {
  char                    *name;
  mct_module_type_t       module_type;
} tm_module_data_t;


class Test_Manager
{
public:
  Test_Manager();

  boolean executeTest(tm_intf_input_t *pInput);

  boolean setModuleMod(mct_module_t *p_mod, tm_set_mod_t *p_set_mod);

private:

  void fillStreamFeatures(tm_intf_input_t *test_case,
    cam_pp_feature_config_t *pp_feature_config, mct_stream_info_t *stream_info);

  void fillStreamIdentity(uint32_t session_idx, uint32_t stream_idx);

  void fillSessionStreamIdentity(uint32_t session_idx, uint32_t stream_idx);

  boolean createStream(uint32_t stream_identity, cam_stream_type_t stream_type);

  void destroyStream(cam_stream_type_t stream_type);

  boolean createSessionStream(void);

  boolean destroySessionStream(void);

  boolean createPipeline();

  void destroyPipeline(void);

  boolean modulesInit(void);

  void modulesDeinit(void);

  boolean modulesLink(cam_stream_type_t stream_type);

  boolean modulesUnlink(cam_stream_type_t stream_type);

  uint32_t getNumOfModules(void);

  boolean modulesAssignType(void);

  boolean modulesStart(void);

  boolean modulesStop(void);

  boolean modulesSetMod(void);

  const mct_module_init_name_t *findCurModuleInits(const mct_module_init_name_t *inits_lut,
    char *name);


private:
  mct_module_init_name_t *p_modules_inits_lut;

  TM_Interface              *p_tmi;
  tm_intf_input_t           m_input;

  mct_list_t                *modules;
  mct_pipeline_t            *pipeline;
  mct_stream_info_t         *stream_info;
  mct_stream_info_t         *session_stream_info;
  uint32_t                  parm_stream_identity;
  uint32_t                  session_id;
  uint32_t                  session_id_idx;
  streams_t                 streams;
  uint32_t                  session_stream_id;

  tm_module_data_t          modules_list[10];
  uint32_t                  num_modules;

  pthread_cond_t            bus_handle_cond;
  pthread_mutex_t           bus_handle_mutex;
  pthread_t                 bus_hdl_tid;

};


typedef struct {
  const char *type;
  uint32_t size;
} tm_payload_type_lut_t;

const tm_payload_type_lut_t tm_payload_type_lut[8] = {
  { "uint32_t", sizeof(uint32_t)},
  { "uint16_t", sizeof(uint16_t)},
  { "uint8_t" , sizeof(uint8_t)},
  { "int32_t", sizeof(int32_t)},
  { "int16_t", sizeof(int16_t)},
  { "int8_t" , sizeof(int8_t)},
  { "float", sizeof(float)},
  { NULL, 0}
};

class XML_Parser
{
public:
  XML_Parser() {}

  boolean parseConfig(char *filename, tm_intf_input_t *test_case);

  boolean parseUcParams(char *filename, tm_intf_input_t *test_case,
  tm_params_type_lut_t *params_lut);

private:

  boolean checkUsecaseParams(tm_intf_input_t *test_input);

  void parseParamGroups(xmlNodePtr pNode, tm_params_type_lut_t *params_lut);

  void initParamsTable(tm_params_type_lut_t *lut);

  float *fillPayloadBufFloat(float *pbuf, float val);

  uint8_t *fillPayloadBuf(uint8_t *pbuf, uint32_t val, uint32_t size);

  boolean fillPayloadValue(char *type, char *value, uint8_t **pbuf);

  boolean fillParamPayload(xmlNodePtr pNode, uint8_t *payload);

  void fillParamValue(xmlNodePtr pNode, tm_params_type_lut_t *lut);

  void parseParams(xmlNodePtr pNode, tm_params_type_lut_t *params_lut);

  boolean checkConfigParams(tm_intf_input_t *test_input);

  int getString(xmlNodePtr pNode, char *name, char *out);

  float getFloat(xmlNodePtr pNode, char *name);

  int getInt(xmlNodePtr pNode, char *name);

  uint32_t getCharArray(xmlNodePtr pNode, const char *name, char *array, uint32_t size, uint32_t size2);

  uint32_t getIntArray(xmlNodePtr pNode, const char *name, int *array, uint32_t size);

  char *getNodeValue(xmlNodePtr pNode, char *name);

  xmlNodePtr getNodeHandle(xmlNodePtr pNode, const char *name);

  char *findNodeProperty(xmlNodePtr pNode, char *name);

  xmlNodePtr getNextNodeValue(xmlNodePtr pNode, char **value);

  xmlNodePtr getChildNodeValue(xmlNodePtr pNode, char **value);

  char *getCurNodeValue(xmlNodePtr pNode);

  char *getCurNodeType(xmlNodePtr pNode);

  char *getNodeName(xmlNodePtr pNode, const char *name);

  char *getNodeType(xmlNodePtr pNode, const char *name);

  void fillStreamTypes(xmlNodePtr pNode, tm_intf_input_t *tm_input);

  char str_array[STR_ARRAY_ENTRIES][32];

};

#endif // TEST_MANAGER_H

