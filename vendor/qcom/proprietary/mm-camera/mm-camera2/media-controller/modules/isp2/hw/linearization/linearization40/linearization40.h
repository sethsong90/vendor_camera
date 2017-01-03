/* linearization40.h
 *
 * Copyright (c) 2012-2013 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __LINEARIZATION40_H__
#define __LINEARIZATION40_H__

/* mctl headers */
#include "chromatix.h"
#include "chromatix_common.h"

/* isp headers */
#include "linearization_reg.h"
#include "isp_sub_module_common.h"

#define CALC_SLOPE(x1,x2,y1,y2) \
  ((float)(y2 - y1) /(float)(x2 -x1))

#ifndef sign
#define sign(x) (((x) < 0) ? (-1) : (1))
#endif

#ifndef Round
#define Round(x) (int)((x) + sign(x)*0.5)
#endif

typedef boolean (*ext_calc_clamp)(void *,
  void *, void *);
typedef boolean (*compute_delta)(void *,
  void *, void *);

typedef struct {
  ext_calc_clamp ext_calc_clamp;
  compute_delta compute_delta;
} ext_override_func;

typedef struct {
  float  r_val;
  float  gr_val;
  float  gb_val;
  float  b_val;
} blk_level_sub_t;

/** ISP_LinearizationRightCfgParams:
 *
 *  @pointSlopeR: Knee points for R channel Right
 *  @pointSlopeGb: Knee points for Gr channel Right
 *  @pointSlopeB: Knee points for B channel Right
 *  @pointSlopeGr: Knee points for Gb channel Right
 **/
typedef struct ISP_LinearizationRightCfgParams {
  /* Knee points for R channel Right */
  ISP_PointSlopeData pointSlopeR;

  /* Knee points for Gr channel Right */
  ISP_PointSlopeData pointSlopeGb;

  /* Knee points for B channel Right */
  ISP_PointSlopeData pointSlopeB;

  /* Knee points for Gb channel Right */
  ISP_PointSlopeData pointSlopeGr;
}__attribute__((packed, aligned(4))) ISP_LinearizationRightCfgParams;


/** ISP_LinearizationCmdType:
 *
 *  @dmi_set: dmi set value
 *  @CfgTbl: cfg tbl
 *  @dmi_reset: dmi reset value
 *  @CfgParams: cfg params
 **/
typedef struct ISP_LinearizationCmdType {
  uint32_t                   dmi_set[2];
  ISP_LinearizationCfgTable  CfgTbl;
  uint32_t                   dmi_reset[2];
  ISP_LinearizationCfgParams CfgParams;
}ISP_LinearizationCmdType;

/** ISP_LinearizationRightCmdType:
 *
 *  @CfgParams: cfg params
 *  @CfgTbl: cfg tbl
 **/
typedef struct ISP_LinearizationRightCmdType {
  ISP_LinearizationRightCfgParams CfgParams;
  ISP_LinearizationCfgTable       CfgTbl;
}ISP_LinearizationRightCmdType;

/** ISP_LinearizationLut:
 *
 *  @r_lut_p: r input
 *  @gr_lut_p: gr input
 *  @gb_lut_p: gc input
 *  @b_lut_p: b input
 *  @r_lut_base: r base
 *  @gr_lut_base: gr base
 *  @gb_lut_base: gb base
 *  @b_lut_base: b base
 *  @r_lut_delta: r delta
 *  @gr_lut_delta: gr delta
 *  @gb_lut_delta: gb delta
 *  @b_lut_delta: b delta
 **/
typedef struct {
  unsigned short r_lut_p[8]; /* 12uQ0 */
  unsigned short gr_lut_p[8]; /* 12uQ0 */
  unsigned short gb_lut_p[8]; /* 12uQ0 */
  unsigned short b_lut_p[8]; /* 12uQ0 */

  unsigned short r_lut_base[9]; /* 12uQ0 */
  unsigned short gr_lut_base[9]; /* 12uQ0 */
  unsigned short gb_lut_base[9]; /* 12uQ0 */
  unsigned short b_lut_base[9]; /* 12uQ0 */

  unsigned int r_lut_delta[9]; /* 18uQ9 */
  unsigned int gr_lut_delta[9]; /* 18uQ9 */
  unsigned int gb_lut_delta[9]; /* 18uQ9 */
  unsigned int b_lut_delta[9]; /* 18uQ9 */
}ISP_LinearizationLut;

/* High resolution LUT with floats*/
typedef struct {
  float r_lut_p[8];
  float r_lut_base[9];
  float gr_lut_p[8];
  float gr_lut_base[9];
  float gb_lut_p[8];
  float gb_lut_base[9];
  float b_lut_p[8];
  float b_lut_base[9];
} Linearization_high_res_Lut_t;

typedef enum {
  LINEAR_AEC_BRIGHT = 0,
  LINEAR_AEC_BRIGHT_NORMAL,
  LINEAR_AEC_NORMAL,
  LINEAR_AEC_NORMAL_LOW,
  LINEAR_AEC_LOW,
  LINEAR_AEC_LUX_MAX,
} isp_linear_lux_t;


/** linearization40_t:
 *
 *  @linear_cmd: linearization cmd config
 *  @trigger_info: trigger info handle
 *  @linear_lut: linear LUT table
 *  @applied_linear_lut: applied linear LUT table
 *  @cur_cct_type: cur cct type
 *  @cur_lux: cur lux
 *  @blk_inc_comp: blk inc
 *  @aec_update: aec update handle
 **/
typedef struct {
  ISP_LinearizationCmdType      linear_cmd;
  cct_trigger_info              trigger_info;
  ISP_LinearizationLut          linear_lut;
  ISP_LinearizationLut          applied_linear_lut;
  ISP_LinearizationCfgTable     applied_hw_lut;
  Linearization_high_res_Lut_t  high_res_lut;
  uint8_t                       linear_trigger_enable;
  uint32_t                      color_temp;
  awb_cct_type                  cur_cct_type;
  float                         blk_inc_comp;
  float                         aec_ratio;
  aec_update_t                  aec_update;
  boolean                       pedestal_enable;
  cam_flash_mode_t              cur_flash_mode;
  uint32_t                      metadump_enable;
#if defined(CHROMATIX_VERSION) && (CHROMATIX_VERSION < 0x306)
  Linearization_high_res_Lut_t  linear_table_A_lowlight;//changed to lowlightin 0x300
  Linearization_high_res_Lut_t  linear_table_A_normal;
  Linearization_high_res_Lut_t  linear_table_TL84_lowlight;//changed to lowlightin 0x300
  Linearization_high_res_Lut_t  linear_table_TL84_normal;
  Linearization_high_res_Lut_t  linear_table_Day_lowlight;//changed to lowlightin 0x300
  Linearization_high_res_Lut_t  linear_table_Day_normal;
#else
  Linearization_high_res_Lut_t  linear_table_lowlight;
  Linearization_high_res_Lut_t  linear_table_normallight;
#endif
  ext_override_func            *ext_func_table;
  isp_temporal_luxfilter_params_t lux_filter;
  blk_level_sub_t               blk_level_applied;
} linearization40_t;

#if OVERRIDE_FUNC
boolean linearization40_fill_func_table_ext(linearization40_t *);
#define FILL_FUNC_TABLE(field)linearization40_fill_func_table_ext(field)
#else
boolean linearization40_fill_func_table(linearization40_t *);
#define FILL_FUNC_TABLE(field)linearization40_fill_func_table(field)
#endif

boolean linearization40_init(isp_sub_module_t *isp_sub_module);

void linearization40_destroy(isp_sub_module_t *isp_sub_module);

boolean linearization40_streamoff(isp_sub_module_t *isp_sub_module, void *data);

boolean linearization40_set_chromatix_ptr(isp_sub_module_t *isp_sub_module,
  void *data);

boolean linearization40_stats_aec_update(isp_sub_module_t *isp_sub_module,
  void *data);

boolean linearization40_stats_awb_update(isp_sub_module_t *isp_sub_module,
  void *data);

boolean linearization40_trigger_update(isp_sub_module_t *isp_sub_module,
  void *data);

boolean linearization40_set_flash_mode(isp_sub_module_t *isp_sub_module,
  void *data);

boolean linearization40_update_base_tables(linearization40_t *linearization,
  chromatix_L_type *pchromatix_L);

boolean linearization40_set_hdr_mode(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

#endif /* __LINEARIZATION40_H__ */
