/**
 * @file atcom_registration.h
 * @brief FSM de registro contra el HES.
 *
 * Si las credenciales ya estan en EEPROM (Storage_is_registered) salta
 * directo a FINISHED. Si no: levanta UDP context, fetch IPv6, manda
 * REGISTER_REQUEST, espera REGISTER_RESPONSE, guarda credentials y MAC,
 * manda ACK y drena el confirm-ACK del HES.
 */

#ifndef _ATCOM_REGISTRATION_H_
#define _ATCOM_REGISTRATION_H_

#include <stdbool.h>
#include <stdint.h>

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
  uint32_t poll_start_tick_ms;
} registration_fsm_t;

void atcom_registration_init(void);
void Com_register_device_process(void);
bool Com_register_device_blocking(uint32_t timeout_ms);

#endif /* _ATCOM_REGISTRATION_H_ */
