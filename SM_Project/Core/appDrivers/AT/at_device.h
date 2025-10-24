/**
 * @file at_device.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-10-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */
 
#ifndef _AT_DEVICE_H_
#define _AT_DEVICE_H_

#include <stdbool.h>
#include <stdint.h>
#include "bg95.h"
#include "stm32l0xx_hal.h"
#include "bg95_at_cmd_lib.h"

#define ATCMD_MAX_PARAM_SIZE 10

typedef struct
{
  // at_type_t type;
  uint32_t id;
  uint32_t at_cmd_size;
  uint8_t cmd_mode;
  uint8_t params[ATCMD_MAX_PARAM_SIZE]; 
} atcmd_desc_t;

typedef struct {
  uint16_t cmd_id;
  const char *cmd_string;
  uint32_t timeout;
  void (*build)(char *cmd, const char *cmd_string, atcmd_desc_t *atcmd_desc);
  void (*analyze)(const char *response);
} BG95_at_LUT_t;


#endif //_AT_DEVICE_H_