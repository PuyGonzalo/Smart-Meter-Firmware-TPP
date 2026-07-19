/**
 * @file atcom_udp.h
 * @ingroup atcom_udp
 * @brief UDP context FSM (PDP activation + socket open).
 *
 * Shared by the registration and session FSMs. The FSM is "rewound" to the
 * IDLE state via atcom_udp_reset() whenever a consumer needs to start over
 * (failure handler, restart wait).
 */

#ifndef _ATCOM_UDP_H_
#define _ATCOM_UDP_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @addtogroup atcom_udp
 * @{
 */

/** @brief States of the UDP context FSM. */
typedef enum {
  COM_UDP_IDLE = 0,           /**< Idle; nothing in progress. */
  COM_UDP_PROCESS_DONE = 1,   /**< PDP context up and socket open. */
  COM_UDP_QIACT_CHECK,        /**< Activate/verify the PDP context (AT+QIACT). */
  COM_UDP_QIACT_WAIT,         /**< Wait for the QIACT response. */
  COM_UDP_QIOPEN_SEND,        /**< Open the UDP socket (AT+QIOPEN). */
  COM_UDP_QIOPEN_WAIT,        /**< Wait for the QIOPEN response. */
  COM_UDP_PROCESS_ERROR,      /**< Unrecoverable error (retries exhausted). */
} com_udp_st;

/** @brief Context of the UDP FSM (activation state, retries, timer). */
typedef struct {
  com_udp_st current_state;    /**< Current state. */
  com_udp_st previous_state;   /**< Previous state (debug/trace). */
  bool pdp_context_ready;      /**< True once the PDP context is active. */
  uint8_t retry_count;         /**< Retries used for the current step. */
  uint8_t max_retries;         /**< Max retries before propagating an error. */
  uint8_t activation_failures; /**< Cumulative QIACT failures. */
  uint32_t last_attempt_time;  /**< HAL_GetTick() of the last attempt. */
  int32_t state_timeout_timer; /**< Per-state timeout timer handle. */
} udp_fsm_t;

/* Lifecycle */

/** @brief Create the FSM timer. Called from Com_Init(). */
void atcom_udp_init(void);

/** @brief Force the FSM back to IDLE to retry from scratch. */
void atcom_udp_reset(void);

/**
 * @brief Run one FSM step.
 * @return Current state; callers care about ::COM_UDP_PROCESS_DONE and
 *         ::COM_UDP_PROCESS_ERROR.
 */
int Com_UDP_context_process(void);

/** @} */

#endif /* _ATCOM_UDP_H_ */
