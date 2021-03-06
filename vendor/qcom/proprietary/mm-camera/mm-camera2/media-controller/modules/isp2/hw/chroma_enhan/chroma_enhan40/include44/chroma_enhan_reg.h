/* chroma_enhan_reg.h
 *
 * Copyright (c) 2015 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */

#ifndef __CHROMA_ENHAN_REG_H__
#define __CHROMA_ENHAN_REG_H__

#define ISP_CC40_OFF 0x00000640
#define ISP_CC40_LEN 9

#define ISP_CC40_ENABLE_REGISTER 0x00000888

typedef struct ISP_Chroma_Enhance_CfgCmdType {
  /* Color Conversion (RGB to Y) Config */
  int32_t      RGBtoYConversionV0      :12;
  int32_t     /* reserved */           :20;

  /* Conversion Coefficient 1 */
  int32_t      RGBtoYConversionV1      :12;
  int32_t     /* reserved */           :20;

  /* Conversion Coefficient 2 */
  int32_t      RGBtoYConversionV2      :12;
  int32_t     /* reserved */           :20;

  /* Conversion Offset */
  uint32_t     RGBtoYConversionOffset  : 9;
  uint32_t     /* reserved */          :23;

  /* Chroma Enhance A Config */
  int32_t      ap                      :12;
  int32_t     /* reserved */           : 4;
  int32_t      am                      :12;
  int32_t     /* reserved */           : 4;

  /* Chroma Enhance B Config */
  int32_t      bp                      :12;
  int32_t     /* reserved */           : 4;
  int32_t      bm                      :12;
  int32_t     /* reserved */           : 4;

  /* Chroma Enhance C Config */
  int32_t      cp                      :12;
  int32_t     /* reserved */           : 4;
  int32_t      cm                      :12;
  int32_t     /* reserved */           : 4;

  /* Chroma Enhance D Config */
  int32_t      dp                      :12;
  int32_t     /* reserved */           : 4;
  int32_t      dm                      :12;
  int32_t     /* reserved */           : 4;

  /* Chroma Enhance K Config */
  int32_t      kcb                     :11;
  int32_t     /* reserved */           : 5;
  int32_t      kcr                     :11;
  int32_t     /* reserved */           : 5;
}__attribute__((packed, aligned(4))) ISP_Chroma_Enhance_CfgCmdType;

#endif /* __CHROMA_ENHAN_REG_H__ */
