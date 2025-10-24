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
#include <stdio.h>
#include <string.h>

#include "at_device.h"
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

  uint8_t in_data_mode;   /* current mode is DATA = 1 mode or COMMAND = 0 mode */
  uint8_t processing_cmd; /* indicate if a command is currently processed or if idle mode */
  bool data_receive;

  /* Parser context */
  // atparser_context_t parser;

} at_context_t;

void ATCore_init(UART_HandleTypeDef *huart);
bool ATCore_send_cmd(atcmd_desc_t *cmd);
bool ATCore_is_response_ready();
char* ATCore_get_last_response();

void fCmdBuild_NoParams(char *cmd, const char *cmd_string, atcmd_desc_t *atcmd_desc);

#endif  // _ATCORE_H_