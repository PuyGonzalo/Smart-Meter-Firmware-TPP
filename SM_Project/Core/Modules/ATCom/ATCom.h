/**
 * @file ATCom.h
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief 
 * @version 0.1
 * @date 2025-10-26
 * 
 * @copyright Copyright (c) 2025
 * 
*/

#ifndef _ATCOM_H_
#define _ATCOM_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "at_core.h"

typedef enum {
  COM_REG_INIT,
  COM_REG_UDP_CTX,
  COM_REG_SEND,
  COM_REG_WAIT_SEND_OK,
  COM_REG_VERIFY_DATA_RDY,
  COM_REG_WAIT_SEND,
  COM_REG_WAIT_DATA_RDY,
  COM_REG_DATA_REQUEST,
  COM_REG_DATA_REQUEST_WAIT,
  COM_REG_PROCESS_DATA,
  COM_REG_ACK,
  COM_REG_WAIT_ACK_SEND,
  COM_REG_FINISHED,
  COM_REG_RESTART_WAIT,
} registration_state_t;

typedef struct {
  registration_state_t current_state;
  uint8_t failure_count;
  bool needs_hard_reset;
  int32_t state_timeout_timer;
  int32_t state_delay_timer;
  int32_t error_backoff_timer;
} registration_fsm_t;

typedef enum {
  COM_UDP_IDLE = 0,
  COM_UDP_PROCESS_DONE = 1,
  COM_UDP_QIACT_WAIT,
  COM_UDP_QIOPEN_SEND,
  COM_UDP_QIOPEN_WAIT,
  COM_UDP_PROCESS_ERROR,
} com_udp_st;

typedef struct {
  com_udp_st current_state;
  com_udp_st previous_state;
  bool pdp_context_ready;
  uint8_t retry_count;
  uint8_t max_retries;
  uint8_t activation_failures;
  uint32_t last_attempt_time;
  int32_t state_timeout_timer;  // Delay timer for state timeout
} udp_fsm_t;

/* State timeout values (milliseconds) */
#define TIMEOUT_INIT              5000
#define TIMEOUT_UDP_CTX           1800   // 3 minutes for full UDP setup
#define TIMEOUT_QIACT             1500   // 150 seconds for PDP activation
#define TIMEOUT_QIOPEN            1500   // 150 seconds for socket open
#define TIMEOUT_SEND              5000
#define TIMEOUT_WAIT_RESPONSE     3000
#define TIMEOUT_DATA_RDY          5000    // 1 minute for data ready check
#define TIMEOUT_DATA_REQUEST      1000
#define MAX_FAILURES_HARD_RESET   10     // Hard reset after this many failures

void Com_Init(void);
void Com_network_connection_process();
int Com_UDP_context_process();
void Com_register_device_process();
void Com_process();



#endif //_ATCOM_H_