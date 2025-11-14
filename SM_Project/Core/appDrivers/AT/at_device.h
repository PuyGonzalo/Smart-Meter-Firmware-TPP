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
#define DEV_ID_BYTES 16
#define MAC_BYTES 16

/**
 * @brief Macro Initialize atcmd_desc_t struct.
 *
 * @code
 * // Usage:
 * atcmd_desc_t cmd = (atcmd_desc_t)ATCMD_DESC_DEFAULT;
 * @endcode
 */
#define ATCMD_DESC_DEFAULT \
    { .id = 0, \
      .cmd_mode = AT_CMD_EXEC, \
      .envelope = NULL, \
      .envp_size = 0, \
      .num_params = {0}, \
      .str_params = {0}, \
      .nb_num_params = 0, \
      .nb_str_params = 0, \
      .total_params = 0, \
      .param_types = {AT_NO_PARAM}}

/**
 * @brief 
 * 
 */
typedef enum {
    AT_PARAM_NUM,
    AT_PARAM_STR,
    AT_NO_PARAM
} at_param_type_t;

typedef enum {
    AT_CMD_TEST,
    AT_CMD_EXEC,
    AT_CMD_READ,
    AT_CMD_WRITE,
    AT_CMD_WRITE_OPT,
    AT_CMD_WRITE_DEFAULT,
} at_cmd_mode_t;

/**
 * @brief 
 * 
 */
typedef struct {
  uint8_t version;
  uint8_t msg_type;
  uint8_t device_id[DEV_ID_BYTES];
  uint32_t seq;
  uint32_t timestamp_high;
  uint32_t timestamp_low;
  uint8_t mac[MAC_BYTES];
} envelope_t;

/**
 * @brief 
 * 
 */
typedef struct
{
  uint32_t id;                                        /*!<  */
  uint32_t at_cmd_size;                               /*!<  */
  at_cmd_mode_t cmd_mode;                             /*!< @see at_cmd_mode_t */
  envelope_t *envelope;                               /*!<  */
  uint32_t envp_size;                                 /*!<  */
  uint32_t num_params[ATCMD_MAX_PARAM_SIZE];          /*!<  */
  char str_params[ATCMD_MAX_STRPARAM_SIZE];           /*!<  */
  uint8_t nb_num_params;                              /*!<  */
  uint8_t nb_str_params;                              /*!<  */
  uint8_t total_params;                               /*!<  */
  at_param_type_t param_types[ATCMD_MAX_PARAM_SIZE];  /*!<  */
} atcmd_desc_t;

/**
 * @brief 
 * 
 */
typedef struct {
  uint16_t cmd_id;
  const char *cmd_string;
  uint16_t cmd_size;
  void (*build)(atcmd_desc_t *atcmd_desc);
} BG95_at_LUT_t;


#endif //_AT_DEVICE_H_