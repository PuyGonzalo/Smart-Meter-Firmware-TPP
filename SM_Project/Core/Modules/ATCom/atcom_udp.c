/**
 * @file atcom_udp.c
 * @brief UDP context FSM — PDP activation (AT+QIACT) + socket open (AT+QIOPEN).
 */

#include "atcom_udp.h"

#include "at_core.h"
#include "at_net.h"
#include "atcom_internal.h"
#include "bg95_at_cmd_lib.h"
#include "delay.h"

static udp_fsm_t udp_process = {.current_state = COM_UDP_IDLE,
                                .previous_state = COM_UDP_IDLE,
                                .pdp_context_ready = false,
                                .retry_count = 0,
                                .max_retries = 1,
                                .activation_failures = 0,
                                .last_attempt_time = 0,
                                .state_timeout_timer = -1};

void atcom_udp_init(void) {
  udp_process.state_timeout_timer = delay_timer_create();
  udp_process.current_state = COM_UDP_IDLE;
}

void atcom_udp_reset(void) {
  udp_process.current_state = COM_UDP_IDLE;
  udp_process.pdp_context_ready = false;
  udp_process.retry_count = 0;
}

int Com_UDP_context_process() {
  atcmd_desc_t cmd = (atcmd_desc_t)ATCMD_DESC_DEFAULT;

  // Check for timeout in waiting states
  if (udp_process.state_timeout_timer >= 0) {
    if (delay_has_finished(udp_process.state_timeout_timer)) {
      switch (udp_process.current_state) {
        case COM_UDP_QIACT_CHECK:
        case COM_UDP_QIACT_WAIT:
        case COM_UDP_QIOPEN_WAIT:
          // Timeout - treat as failure
          if (udp_process.retry_count < udp_process.max_retries) {
            udp_process.retry_count++;
            udp_process.current_state = COM_UDP_IDLE;  // Retry from start
          } else {
            udp_process.current_state = COM_UDP_PROCESS_ERROR;
          }
          return udp_process.current_state;
        default:
          break;
      }
    }
  }

  switch (udp_process.current_state) {
    /* --------------------------------------------------------- */
    case COM_UDP_IDLE: {
      if (!udp_process.pdp_context_ready) {
        // Query PDP context status first (AT+QIACT?)
        cmd.id = CMD_AT_QIACT;
        cmd.cmd_mode = AT_CMD_READ;
        if (ATCore_send_cmd(&cmd)) {
          delay_start(udp_process.state_timeout_timer, TIMEOUT_QIACT_QUERY);
          udp_process.current_state = COM_UDP_QIACT_CHECK;
        }
      } else {
        // Already have PDP context, just need to open socket
        udp_process.current_state = COM_UDP_QIOPEN_SEND;
      }
      break;
    }

    /* --------------------------------------------------------- */
    case COM_UDP_QIACT_CHECK: {
      if (!ATCore_is_response_ready()) break;

      ATCore_process_response();
      delay_stop(udp_process.state_timeout_timer);

      uint8_t status = ATCore_get_response_status();
      if (status == BG95_RESP_QI) {
        // AT+QIACT? returned +QIACT data -> PDP context already active
        udp_process.pdp_context_ready = true;
        udp_process.retry_count = 0;
        udp_process.current_state = COM_UDP_QIOPEN_SEND;
      } else if (status == BG95_RESP_OK) {
        // AT+QIACT? returned OK with no data -> need to activate
        cmd.id = CMD_AT_QIACT;
        cmd.cmd_mode = AT_CMD_WRITE_DEFAULT;
        if (ATCore_send_cmd(&cmd)) {
          delay_start(udp_process.state_timeout_timer, TIMEOUT_QIACT_ACTIVATE);
          udp_process.current_state = COM_UDP_QIACT_WAIT;
        }
      } else {
        // Error querying - retry or fail
        if (udp_process.retry_count < udp_process.max_retries) {
          udp_process.retry_count++;
          udp_process.current_state = COM_UDP_IDLE;
        } else {
          udp_process.current_state = COM_UDP_PROCESS_ERROR;
        }
      }
      break;
    }

    /* --------------------------------------------------------- */
    case COM_UDP_QIACT_WAIT: {
      if (!ATCore_is_response_ready()) break;

      ATCore_check_response();
      delay_stop(udp_process.state_timeout_timer);

      if (ATCore_get_response_status() == 0) {
        udp_process.retry_count = 0;  // Reset so QIOPEN gets full retry budget
        udp_process.current_state = COM_UDP_QIOPEN_SEND;
      } else {
        // Failed - retry or error
        if (udp_process.retry_count < udp_process.max_retries) {
          udp_process.retry_count++;
          udp_process.current_state = COM_UDP_IDLE;  // Will resend QIACT
        } else {
          udp_process.current_state = COM_UDP_PROCESS_ERROR;
        }
      }
      break;
    }

    /* --------------------------------------------------------- */
    case COM_UDP_QIOPEN_SEND: {
      cmd.id = CMD_AT_QIOPEN;
      cmd.cmd_mode = AT_CMD_WRITE_DEFAULT;
      if (ATCore_send_cmd(&cmd)) {
        delay_start(udp_process.state_timeout_timer, TIMEOUT_QIOPEN);
        udp_process.current_state = COM_UDP_QIOPEN_WAIT;
      }
      break;
    }

    /* --------------------------------------------------------- */
    case COM_UDP_QIOPEN_WAIT: {
      if (!ATCore_is_response_ready()) break;

      ATCore_check_response();
      delay_stop(udp_process.state_timeout_timer);

      if (ATCore_get_response_status() == 0) {
        udp_process.pdp_context_ready = true;
        udp_process.retry_count = 0;  // Reset for next time
        udp_process.current_state = COM_UDP_PROCESS_DONE;
      } else {
        // Failed - retry or error
        if (udp_process.retry_count < udp_process.max_retries) {
          udp_process.retry_count++;
          udp_process.current_state = COM_UDP_QIOPEN_SEND;  // Retry QIOPEN
        } else {
          udp_process.current_state = COM_UDP_PROCESS_ERROR;
        }
      }
      break;
    }

    /* --------------------------------------------------------- */
    case COM_UDP_PROCESS_DONE: {
      // Terminal state - do nothing
      break;
    }

    /* --------------------------------------------------------- */
    case COM_UDP_PROCESS_ERROR: {
      // Terminal state - do nothing
      break;
    }
  }

  return udp_process.current_state;
}
