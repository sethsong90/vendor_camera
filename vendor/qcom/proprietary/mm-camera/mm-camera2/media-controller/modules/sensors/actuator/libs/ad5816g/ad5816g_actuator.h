/**
 * ad5816g_actuator.h
 *
 * DESCRIPTION
 *  Auto-Generated by Actuator tool.
 *
 * Copyright (c) 2014-2015 Qualcomm Technologies, Inc. All Rights Reserved.
 *   Qualcomm Technologies Proprietary and Confidential.
 */
  {
    .actuator_params =
    {
      .module_name = "liteon",
      .actuator_name = "ad5816g",
      .i2c_addr = 0x1C,
      .i2c_data_type = CAMERA_I2C_WORD_DATA,
      .i2c_addr_type = CAMERA_I2C_BYTE_ADDR,
      .act_type = ACTUATOR_TYPE_VCM,
      .data_size = 10,
      .reg_tbl =
      {
        .reg_tbl_size = 1,
        .reg_params =
        {
          {
            .reg_write_type = ACTUATOR_WRITE_DAC,
            .hw_mask = 0x00000000,
            .reg_addr = 0x03,
            .hw_shift = 0,
            .data_shift = 0,
          },
        },
      },
      .init_setting_size = 5,
      .init_settings =
      {
        {
          .reg_addr = 0x02,
          .addr_type = CAMERA_I2C_BYTE_ADDR,
          .reg_data = 0x01, /* sw reset */
          .data_type = CAMERA_I2C_BYTE_DATA,
          .i2c_operation = ACTUATOR_I2C_OP_WRITE,
          .delay = 0,
        },
        {
          .reg_addr = 0x02,
          .addr_type = CAMERA_I2C_BYTE_ADDR,
          .reg_data = 0x12, /* set ring ctrl & linear mode */
          .data_type = CAMERA_I2C_BYTE_DATA,
          .i2c_operation = ACTUATOR_I2C_OP_WRITE,
          .delay = 0,
        },
        {
          .reg_addr = 0x06,
          .addr_type = CAMERA_I2C_BYTE_ADDR,
          .reg_data = 0x20, /* set ARC-RES1 mode & clk_range = 555 KHz */
          .data_type = CAMERA_I2C_BYTE_DATA,
          .i2c_operation = ACTUATOR_I2C_OP_WRITE,
          .delay = 0,
        },
        {
          .reg_addr = 0x07,
          .addr_type = CAMERA_I2C_BYTE_ADDR,
          .reg_data = 0x6D, /* VCM_FREQ = 0x6D -> fRES = 82.6 Hz */
          .data_type = CAMERA_I2C_BYTE_DATA,
          .i2c_operation = ACTUATOR_I2C_OP_WRITE,
          .delay = 0,
        },
        {
          .reg_addr = 0x08,
          .addr_type = CAMERA_I2C_BYTE_ADDR,
          .reg_data = 0x5D,
          .data_type = CAMERA_I2C_BYTE_DATA,
          .i2c_operation = ACTUATOR_I2C_OP_WRITE,
          .delay = 0,
        },
      },
    }, /* actuator_params */

    .actuator_tuned_params =
    {
      .scenario_size =
      {
        1, /* MOVE_NEAR */
        1, /* MOVE_FAR */
      },
      .ringing_scenario =
      {
        /* MOVE_NEAR */
        {
          374,
        },
        /* MOVE_FAR */
        {
          374,
        },
      },
      .initial_code = 180,
      .region_size = 1,
      .region_params =
      {
        {
          .step_bound =
          {
            374, /* Macro step boundary*/
            0, /* Infinity step boundary*/
          },
          .code_per_step = 1,
          .qvalue = 128,
        },
      },
      .damping =
      {
        /* damping[MOVE_NEAR] */
        {
          /* Scenario 0 */
          {
            .ringing_params =
            {
              /* Region 0 */
              {
                .damping_step = 0x3FF,
                .damping_delay = 15000,
                .hw_params = 0x00000400,
              },
            },
          },
        },
        /* damping[MOVE_NEAR] */
        {
          /* Scenario 0 */
          {
            .ringing_params =
            {
              /* Region 0 */
              {
                .damping_step = 0x3FF,
                .damping_delay = 15000,
                .hw_params = 0x00000400,
              },
            },
          },
        },
      },
    }, /* actuator_tuned_params */
  },
