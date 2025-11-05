/**
 * @file at_core.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-10-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef _AT_CORE_H_
#define _AT_CORE_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "at_device.h"
#include "at_parser.h"
#include "at_net.h"
#include "bg95.h"

/**
 * @brief 
 * 
 */
typedef enum {
  ATSTATUS_OK = 0,
  ATSTATUS_ERROR,
  ATSTATUS_TIMEOUT,
  ATSTATUS_OK_PENDING_URC,
} at_status_t;

/**
 * @brief 
 * 
 */
typedef enum {
  ATENDMSG_YES = 0,
  ATENDMSG_NO,
  ATENDMSG_ERROR,
} at_endmsg_t;

/**
 * @brief 
 * 
 */
typedef struct
{
  void *dev_driver;
  void *device;

  uint8_t device_id[ENV_DEVID_BYTES];
  uint8_t device_mac[ENV_MAC_BYTES];

  uint8_t net_context;

  uint8_t in_data_mode;   /* current mode is DATA = 1 mode or COMMAND = 0 mode */
  uint8_t processing_cmd; /* indicate if a command is currently processed or if idle mode */
} at_context_t;

void ATCore_init(UART_HandleTypeDef *huart);
void ATCore_config();
void ATCore_set_device_id(uint8_t *dev_id);
bool ATCore_compare_device_id(uint8_t *dev_id);
void ATCore_set_device_mac(uint8_t *dev_mac);
bool ATCore_compare_device_mac(uint8_t *dev_mac);
bool ATCore_send_cmd(atcmd_desc_t *cmd);
bool ATCore_is_response_ready();
uint8_t ATCore_check_response();
bool ATCore_process_response();
uint8_t ATCore_get_response_status();
char* ATCore_get_last_response();
bool ATCore_cmp_str_in_field(const char *str, uint16_t field_index);
int16_t ATCore_get_first_qird_value();
void ATCore_set_data_mode();

#endif  // _ATCORE_H_