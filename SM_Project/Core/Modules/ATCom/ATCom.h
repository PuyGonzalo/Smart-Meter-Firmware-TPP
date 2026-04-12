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
  COM_UDP_QIACT_CHECK,
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

/* ---- Periodic session FSM ---- */

typedef enum {
  COM_SES_IDLE,               /* Waiting to start */
  COM_SES_UDP_CTX,            /* Establishing UDP connection */
  COM_SES_SEND_MSG,           /* Build and send envelope via QISENDEX */
  COM_SES_WAIT_SEND_OK,       /* Wait for SEND_OK from modem */
  COM_SES_WAIT_DATA,          /* Delay before polling for incoming data */
  COM_SES_CHECK_DATA_RDY,     /* Poll QIRD len=0 to check if data available */
  COM_SES_WAIT_DATA_RDY,      /* Wait for QIRD poll response */
  COM_SES_READ_DATA,          /* QIRD to read actual data */
  COM_SES_READ_DATA_WAIT,     /* Wait for QIRD data response */
  COM_SES_PROCESS_MSG,        /* Parse received msg, decide next action */
  COM_SES_DONE,               /* Session complete */
  COM_SES_ERROR,              /* Unrecoverable error */
  COM_SES_RESTART_WAIT,       /* Backoff before retry */
} session_state_t;

/* Message types (IEC 62056 / protocol spec) */
#define MSG_TYPE_HANDSHAKE          0x00
#define MSG_TYPE_HANDSHAKE_RESPONSE 0x01
#define MSG_TYPE_REGISTER_REQUEST   0x02
#define MSG_TYPE_REGISTER_RESPONSE  0x03
#define MSG_TYPE_ANNOUNCE           0x04
#define MSG_TYPE_READ_REQUEST       0x0A
#define MSG_TYPE_READ_RESPONSE      0x0B
#define MSG_TYPE_WRITE_REQUEST      0x14
#define MSG_TYPE_WRITE_RESPONSE     0x15
#define MSG_TYPE_EXECUTE_REQUEST    0x1E
#define MSG_TYPE_EXECUTE_RESPONSE   0x1F
#define MSG_TYPE_ACTION_REQUEST     0x28
#define MSG_TYPE_ACTION_RESPONSE    0x29
#define MSG_TYPE_ACK                0xFF

/* OBIS operation codes */
#define OBIS_OP_READ    0x00
#define OBIS_OP_WRITE   0x01
#define OBIS_OP_EXECUTE 0x02
#define OBIS_OP_ACTION  0x03

typedef struct {
  session_state_t current_state;
  session_state_t after_send_ok;   /* Next state after SEND_OK */
  uint8_t failure_count;
  bool needs_hard_reset;
  uint32_t seq;                    /* Session sequence counter */
  int32_t state_timeout_timer;
  int32_t state_delay_timer;
  int32_t error_backoff_timer;
} session_fsm_t;

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
bool Com_register_device_blocking(uint32_t timeout_ms);
void Com_session_process(void);
bool Com_is_session_done(void);
void Com_session_start(void);



#endif //_ATCOM_H_