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
#define ATCMD_MAX_STRPARAM_SIZE 128

/**
 * @brief 
 * 
 */
typedef struct
{
  uint32_t id;
  uint32_t at_cmd_size;
  uint8_t cmd_mode;
  uint8_t msg_type;
  uint8_t params[ATCMD_MAX_PARAM_SIZE];
  char str_params[ATCMD_MAX_STRPARAM_SIZE];
  uint8_t nb_params;
  uint8_t nb_str_params;
  char payload[ATCMD_MAX_STRPARAM_SIZE];
} atcmd_desc_t;

/**
 * @brief 
 * 
 */
typedef struct {
  uint8_t version;
  uint8_t msg_type;
  uint8_t device_id[16];
  uint32_t seq;
  uint32_t timestamp; //! TODO: En el informe tenemos que esde 64 bits. VER...
  uint8_t mac[16];
} envelope_t;

/**
 * @brief 
 * 
 */
typedef struct {
  uint16_t cmd_id;
  const char *cmd_string;
  uint32_t timeout;
  void (*build)(char *cmd, const char *cmd_string, atcmd_desc_t *atcmd_desc);
  void (*analyze)(const char *response);
} BG95_at_LUT_t;


#endif //_AT_DEVICE_H_