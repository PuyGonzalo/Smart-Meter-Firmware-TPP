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

/* ============================================================================
 * DEBUG CONFIGURATION - Comment out for production values
 * ============================================================================
 */
/* Debug-only: device-initiated periodic session. Remove for production. */
#define DEBUG_SESSION_START

typedef enum {
  COM_REG_INIT,
  COM_REG_UDP_CTX,
  COM_REG_FETCH_IPV6,       /* Send AT+CGPADDR=1 to get assigned IP */
  COM_REG_WAIT_FETCH_IPV6,  /* Wait for CGPADDR response */
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
  COM_REG_DRAIN_POLL,       /* QIRD len=0 — check if HES confirm ACK is buffered */
  COM_REG_DRAIN_POLL_WAIT,  /* Wait for QIRD len=0 response */
  COM_REG_DRAIN_READ_WAIT,  /* Wait for QIRD read to flush the ACK */
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
  uint32_t poll_start_tick_ms;  /* HAL_GetTick() when polling started (resend guard) */
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
  COM_SES_IDLE,                  /* Waiting to start */
  COM_SES_UDP_CTX,               /* Establishing UDP connection */

  /* Announce phase (REGISTER_REQUEST with stored device_id != 0) */
  COM_SES_FETCH_IPV6,            /* AT+CGPADDR=1 to read current IPv6 */
  COM_SES_WAIT_FETCH_IPV6,       /* Wait for CGPADDR response */
  COM_SES_SEND_ANNOUNCE,         /* Build & send REGISTER_REQUEST */

  /* Generic SEND_OK wait — transitions to session.after_send_ok */
  COM_SES_WAIT_SEND_OK,

  /* Keepalive loop while waiting for next HES message */
  COM_SES_POLL_WAIT,             /* Idle wait between polls */
  COM_SES_CHECK_HES_DATA,        /* AT+QIRD=ID,0 to check if data available */
  COM_SES_CHECK_HES_DATA_WAIT,   /* Wait QIRD len=0 response */

  /* Reading & processing HES message */
  COM_SES_READ_HES_MSG,          /* AT+QIRD with size to read bytes */
  COM_SES_READ_HES_MSG_WAIT,     /* Wait QIRD data response */
  COM_SES_PROCESS_HES_MSG,       /* Parse envelope and dispatch */

  COM_SES_DONE,                  /* Session complete */
  COM_SES_ERROR,                 /* Unrecoverable error */
  COM_SES_RESTART_WAIT,          /* Backoff before retry */
} session_state_t;

/* Message types (IEC 62056 / protocol spec) */
#define MSG_TYPE_HANDSHAKE          0x00
#define MSG_TYPE_HANDSHAKE_RESPONSE 0x01
#define MSG_TYPE_REGISTER_REQUEST   0x02
#define MSG_TYPE_REGISTER_RESPONSE  0x03
#define MSG_TYPE_READ_REQUEST       0x0A
#define MSG_TYPE_READ_RESPONSE      0x0B
#define MSG_TYPE_WRITE_REQUEST      0x14
#define MSG_TYPE_WRITE_RESPONSE     0x15
#define MSG_TYPE_EXECUTE_REQUEST    0x1E
#define MSG_TYPE_EXECUTE_RESPONSE   0x1F
#define MSG_TYPE_ACTION_REQUEST     0x28
#define MSG_TYPE_ACTION_RESPONSE    0x29
#define MSG_TYPE_ACK                0xFF
#ifdef DEBUG_SESSION_START
#define MSG_TYPE_SESSION_START_REQUEST  0xF0
#endif

/* OBIS operation codes */
#define OBIS_OP_READ    0x00
#define OBIS_OP_WRITE   0x01
#define OBIS_OP_EXECUTE 0x02
#define OBIS_OP_ACTION  0x03

/* OBIS code identifiers (string form, matched by HES) */
#define OBIS_WATER_VOLUME  "1.0.1"
#define OBIS_CLOCK         "0.9.4"
#define OBIS_BATTERY       "C.6.1"
#define OBIS_NEXT_WAKE     "0.0.1"

/* Cold-start budget: device wakes this many seconds before the agreed
 * next_wake_time so the BG95 has time to attach + open PDP + send announce.
 * Refine empirically once we have field measurements. */
#define COLD_START_OFFSET_SEC  45U


typedef struct {
  session_state_t current_state;
  session_state_t after_send_ok;   /* Next state after SEND_OK */
  uint8_t failure_count;
  bool needs_hard_reset;
  uint32_t seq;                    /* Session sequence counter */
  int32_t state_timeout_timer;
  int32_t state_delay_timer;
  int32_t error_backoff_timer;
  uint8_t last_msg_type;           /* msg_type of last sent envelope (for resend) */
  uint16_t last_payload_len;       /* payload len of last sent envelope (0 = NULL) */
  uint32_t poll_start_tick_ms;     /* HAL_GetTick() when polling started (resend guard) */
  bool can_resend;                 /* true only after at least one envelope was sent */
} session_fsm_t;

/* Per-command timeouts (ms) — values from Quectel BG95 TCP/IP Application Note v1.4 */
#define TIMEOUT_INIT              5000    /* AT\r\n boot polling */
#define TIMEOUT_QIACT_QUERY       2000    /* AT+QIACT? — local modem query (~300 ms per doc) */
#define TIMEOUT_QIACT_ACTIVATE    150000  /* AT+QIACT=n — doc: 150 s, determined by network */
#define TIMEOUT_QIOPEN            150000  /* AT+QIOPEN  — doc: 150 s, determined by network */
#define TIMEOUT_SEND              5000    /* AT+QISENDEX — doc: 120 s, but UDP SEND_OK is local */
#define TIMEOUT_WAIT_RESPONSE     3000    /* generic inter-state delay */
#define TIMEOUT_CGPADDR           3000    /* AT+CGPADDR  — local PDP context query */
#define TIMEOUT_DATA_RDY          5000    /* AT+QIRD=x,0 — local buffer length query */
#define TIMEOUT_DATA_REQUEST      2000    /* AT+QIRD=x   — local buffer read */
#define POLL_RESEND_TIMEOUT_MS    30000U  /* Resend last msg if no HES response after 30 s */
#define TIMEOUT_DRAIN_WINDOW      6000   /* total window to drain HES confirm ACK after our ACK */
#define TIMEOUT_DRAIN_RETRY       500    /* inter-poll delay when QIRD returns 0 */

void Com_Init(void);
void Com_network_connection_process();
int Com_UDP_context_process();
void Com_register_device_process();
bool Com_register_device_blocking(uint32_t timeout_ms);
void Com_session_process(void);
bool Com_is_session_done(void);
void Com_session_start(void);
uint32_t Com_pop_pending_wake_seconds(void);



#endif //_ATCOM_H_