/**
 * @file atcom_session.h
 * @ingroup atcom_session
 * @brief Periodic session FSM with the HES.
 *
 * Once registered, the device wakes up periodically, brings up the UDP
 * context, optionally announces its IPv6 (if it changed), and then polls the
 * QIRD channel waiting for HES messages (READ_REQUEST, WRITE_REQUEST,
 * HANDSHAKE, etc.). Each message is routed through a dispatch table. The
 * session ends when the HES sends an ACK.
 */

#ifndef _ATCOM_SESSION_H_
#define _ATCOM_SESSION_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @addtogroup atcom_session
 * @{
 */

/** @brief States of the periodic session FSM. */
typedef enum {
  COM_SES_IDLE,                  /**< Waiting to start. */
  COM_SES_UDP_CTX,               /**< Establishing the UDP connection. */

  /* Announce phase (REGISTER_REQUEST with stored device_id != 0) */
  COM_SES_FETCH_IPV6,            /**< AT+CGPADDR=1 to read the current IPv6. */
  COM_SES_WAIT_FETCH_IPV6,       /**< Wait for the CGPADDR response. */
  COM_SES_SEND_ANNOUNCE,         /**< Build and send the IP announce. */

  /* Generic SEND_OK wait — transitions to session.after_send_ok */
  COM_SES_WAIT_SEND_OK,          /**< Wait for SEND OK after any send. */

  /* Poll loop while waiting for the next HES message */
  COM_SES_POLL_WAIT,             /**< Idle wait between polls. */
  COM_SES_CHECK_HES_DATA,        /**< AT+QIRD=ID,0 to check for available data. */
  COM_SES_CHECK_HES_DATA_WAIT,   /**< Wait for the QIRD len=0 response. */

  /* Reading and processing an HES message */
  COM_SES_READ_HES_MSG,          /**< AT+QIRD with size to read the bytes. */
  COM_SES_READ_HES_MSG_WAIT,     /**< Wait for the QIRD data response. */
  COM_SES_PROCESS_HES_MSG,       /**< Parse the envelope and dispatch. */

  COM_SES_DONE,                  /**< Session complete. */
  COM_SES_ERROR,                 /**< Unrecoverable error. */
  COM_SES_RESTART_WAIT,          /**< Backoff before retry. */
} session_state_t;

/** @brief Context of the session FSM. */
typedef struct {
  session_state_t current_state;   /**< Current state. */
  session_state_t after_send_ok;   /**< Next state after SEND OK. */
  uint8_t failure_count;           /**< Consecutive failure count. */
  bool needs_hard_reset;           /**< Set after too many failures. */
  uint32_t seq;                    /**< Session sequence counter. */
  int32_t state_timeout_timer;     /**< Per-state timeout timer handle. */
  int32_t state_delay_timer;       /**< Inter-step delay timer handle. */
  int32_t error_backoff_timer;     /**< Exponential-backoff timer handle. */
  uint8_t last_msg_type;           /**< msg_type of the last sent envelope (resend). */
  uint16_t last_payload_len;       /**< payload len of the last sent envelope (0 = NULL). */
  uint32_t last_seq;               /**< seq used in the last sent envelope (resend). */
  uint32_t poll_start_tick_ms;     /**< HAL_GetTick() when polling started (resend guard). */
  bool can_resend;                 /**< True only after at least one envelope was sent. */
} session_fsm_t;

/** @brief Create the FSM timers. Called from Com_Init(). */
void atcom_session_init(void);

/** @brief Reset session state and start a new periodic session. */
void Com_session_start(void);

/** @brief Run one step of the session FSM. */
void Com_session_process(void);

/**
 * @brief Whether the session has finished — either successfully or failed.
 *        Use Com_session_failed() to tell which.
 * @retval true  Session is done or in error state.
 */
bool Com_is_session_done(void);

/**
 * @brief Whether the session ended by hard reset (N consecutive failures).
 *        Only valid to read after Com_is_session_done() == true.
 * @retval true  Session failed after exhausting retries.
 */
bool Com_session_failed(void);

/** @} */

#endif /* _ATCOM_SESSION_H_ */
