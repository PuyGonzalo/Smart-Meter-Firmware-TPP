/**
 * @file atcom_registration.h
 * @ingroup atcom_registration
 * @brief Registration FSM against the HES.
 *
 * If credentials are already in EEPROM (Storage_is_registered) the FSM jumps
 * straight to FINISHED. Otherwise it brings up the UDP context, fetches the
 * IPv6 address, sends REGISTER_REQUEST, waits for REGISTER_RESPONSE, stores the
 * credentials and MAC, sends an ACK and drains the HES confirm-ACK.
 */

#ifndef _ATCOM_REGISTRATION_H_
#define _ATCOM_REGISTRATION_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @addtogroup atcom_registration
 * @{
 */

/** @brief States of the registration FSM. */
typedef enum {
  COM_REG_INIT,             /**< Entry; skips to FINISHED if already registered. */
  COM_REG_UDP_CTX,          /**< Bring up the PDP context and UDP socket. */
  COM_REG_FETCH_IPV6,       /**< Send AT+CGPADDR=1 to get the assigned IP. */
  COM_REG_WAIT_FETCH_IPV6,  /**< Wait for the CGPADDR response. */
  COM_REG_SEND,             /**< Send REGISTER_REQUEST (IMEI + IPv6). */
  COM_REG_WAIT_SEND_OK,     /**< Wait for SEND OK. */
  COM_REG_VERIFY_DATA_RDY,  /**< Re-poll QIRD len=0 for buffered data. */
  COM_REG_WAIT_SEND,        /**< Delay before polling for the response. */
  COM_REG_WAIT_DATA_RDY,    /**< Wait for the QIRD len=0 response. */
  COM_REG_DATA_REQUEST,     /**< Read the buffered REGISTER_RESPONSE. */
  COM_REG_DATA_REQUEST_WAIT,/**< Wait for the QIRD data response. */
  COM_REG_PROCESS_DATA,     /**< Parse response; store credentials + next_wake. */
  COM_REG_ACK,              /**< Send the final ACK. */
  COM_REG_WAIT_ACK_SEND,    /**< Wait for the ACK SEND OK. */
  COM_REG_DRAIN_POLL,       /**< QIRD len=0 — check if the HES confirm-ACK is buffered. */
  COM_REG_DRAIN_POLL_WAIT,  /**< Wait for the QIRD len=0 response. */
  COM_REG_DRAIN_READ_WAIT,  /**< Wait for the QIRD read that flushes the ACK. */
  COM_REG_FINISHED,         /**< Registration complete (idle). */
  COM_REG_RESTART_WAIT,     /**< Backoff before retrying. */
} registration_state_t;

/** @brief Context of the registration FSM. */
typedef struct {
  registration_state_t current_state; /**< Current state. */
  uint8_t failure_count;              /**< Consecutive failure count. */
  bool needs_hard_reset;              /**< Set after too many failures. */
  int32_t state_timeout_timer;        /**< Per-state timeout timer handle. */
  int32_t state_delay_timer;          /**< Inter-step delay timer handle. */
  int32_t error_backoff_timer;        /**< Exponential-backoff timer handle. */
  uint32_t poll_start_tick_ms;        /**< HAL_GetTick() when polling started. */
} registration_fsm_t;

/** @brief Create the FSM timers. Called from Com_Init(). */
void atcom_registration_init(void);

/** @brief Run one step of the registration FSM. */
void Com_register_device_process(void);

/**
 * @brief Run registration to completion (blocking, watchdog-fed).
 * @param timeout_ms Maximum time to wait for registration.
 * @retval true  Registration succeeded.
 * @retval false Timed out or hard-reset condition reached.
 */
bool Com_register_device_blocking(uint32_t timeout_ms);

/** @} */

#endif /* _ATCOM_REGISTRATION_H_ */
