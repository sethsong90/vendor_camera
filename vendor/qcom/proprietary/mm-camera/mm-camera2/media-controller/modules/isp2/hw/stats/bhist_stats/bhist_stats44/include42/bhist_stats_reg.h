/*
 * Copyright (c) 2015 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 *
 */
#ifndef __BHIST_STATS_REG_H__
#define __BHIST_STATS_REG_H__

#define BHIST_STATS_OFF 0x000008BC
#define BHIST_STATS_LEN 2

#define BHIST_CGC_OVERRIDE TRUE
#define BHIST_CGC_OVERRIDE_REGISTER 0x14
#define BHIST_CGC_OVERRIDE_BIT 18

/** ISP_StatsBhist_CfgCmdType:
 *
 *  @rgnHOffset: H offset
 *  @rgnVOffset: V offset
 *  @rgnHNum: H num
 *  @rgnVNum: V num
 **/
typedef struct ISP_StatsBhist_CfgCmdType {
  /*  VFE_STATS_BHIST_RGN_OFFSET_CFG   */
  uint32_t        rgnHOffset            :   13;
  uint32_t      /* reserved */          :    3;
  uint32_t        rgnVOffset            :   12;
  uint32_t       /*reserved */          :    4;
  /*  VFE_STATS_BHIST_RGN_SIZE_CFG */
  uint32_t        rgnHNum               :    12;
  uint32_t        rgnVNum               :    12;
  uint32_t      /* reserved 23:31 */    :     8;
} __attribute__((packed, aligned(4))) ISP_StatsBhist_CfgCmdType;

#endif /* __BHIST_STATS_REG_H__ */
