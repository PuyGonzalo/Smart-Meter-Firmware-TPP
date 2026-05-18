/**
 * @file atcom_internal.h
 * @brief Defines y helpers compartidos entre los sub-modulos de ATCom.
 *        No incluir desde codigo de aplicacion: solo desde atcom_*.c.
 */

#ifndef _ATCOM_INTERNAL_H_
#define _ATCOM_INTERNAL_H_

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * DEBUG CONFIGURATION - Comment out for production values
 * ============================================================================
 */
#define DEBUG_FAST_TIMEOUTS

/* Debug-only: device-initiated periodic session. Remove for production. */
#define DEBUG_SESSION_START

#ifdef DEBUG_FAST_TIMEOUTS
#define RESTART_DELAY_BASE_MS    200
#define RESTART_DELAY_MAX_MS     3000
#define GENERIC_RETRY_DELAY_MS   500
#define MAX_FAILURES_HARD_RESET  20   /* ~3 min total before hard reset (asume ~5s state timeout + backoff cap 3s por ciclo) */
#else
#define RESTART_DELAY_BASE_MS    1000
#define RESTART_DELAY_MAX_MS     5000  /* cap at 5s so 5 failures fit in <120s global timeout */
#define GENERIC_RETRY_DELAY_MS   10000
#define MAX_FAILURES_HARD_RESET  5    /* ~61s total before hard reset */
#endif

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

/* Status flags reutilizados como respuesta en HANDSHAKE_RESPONSE,
 * REGISTER_RESPONSE, etc. Spec: Body/07B-estructura-de-mensajes.tex */
#define MSG_STATUS_OK                 0x00
#define MSG_STATUS_ERR_INVALID_KEY    0x01
#define MSG_STATUS_ERR_INVALID_MSG    0x02
#define MSG_STATUS_ERR_INVALID_PROTO  0x03

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

/* Cross-module helpers implemented in ATCom.c */
void atcom_set_pending_wake_seconds(uint32_t s);

#endif /* _ATCOM_INTERNAL_H_ */
