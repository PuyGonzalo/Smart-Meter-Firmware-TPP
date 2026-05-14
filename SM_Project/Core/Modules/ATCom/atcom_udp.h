/**
 * @file atcom_udp.h
 * @brief UDP context FSM (PDP activation + socket open).
 *
 * Compartida entre registration y session. La FSM se "rebobina" al estado
 * IDLE via atcom_udp_reset() cuando alguno de los consumidores necesita
 * volver a empezar (failure handler, restart wait).
 */

#ifndef _ATCOM_UDP_H_
#define _ATCOM_UDP_H_

#include <stdbool.h>
#include <stdint.h>

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
  int32_t state_timeout_timer;
} udp_fsm_t;

/* Lifecycle */
void atcom_udp_init(void);     /* crea timer (llamado desde Com_Init) */
void atcom_udp_reset(void);    /* fuerza estado a IDLE para reintentar desde 0 */

/* FSM step — devuelve el estado actual (interesa COM_UDP_PROCESS_DONE/_ERROR) */
int Com_UDP_context_process(void);

#endif /* _ATCOM_UDP_H_ */
