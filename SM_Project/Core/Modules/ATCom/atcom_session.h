/**
 * @file atcom_session.h
 * @brief FSM de sesion periodica con el HES.
 *
 * Una vez registrado, el dispositivo despierta periodicamente, levanta UDP
 * context, opcionalmente anuncia su IPv6 (si cambio), y queda polleando el
 * canal QIRD esperando mensajes del HES (READ_REQUEST, WRITE_REQUEST,
 * HANDSHAKE, etc). Cada mensaje se despacha por dispatch table. Termina
 * cuando llega un ACK del HES.
 */

#ifndef _ATCOM_SESSION_H_
#define _ATCOM_SESSION_H_

#include <stdbool.h>
#include <stdint.h>

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
  uint32_t last_seq;               /* seq used in the last sent envelope (for resend) */
  uint32_t poll_start_tick_ms;     /* HAL_GetTick() when polling started (resend guard) */
  bool can_resend;                 /* true only after at least one envelope was sent */
} session_fsm_t;

void atcom_session_init(void);
void Com_session_start(void);
void Com_session_process(void);
bool Com_is_session_done(void);

#endif /* _ATCOM_SESSION_H_ */
