/**
 * @file ATCom.c
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief
 * @version 0.1
 * @date 2025-10-26
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "ATCom.h"

#include <string.h>

#include "at_core.h"
#include "at_device.h"
#include "at_net.h"
#include "at_parser.h"
#include "bg95_at_cmd_lib.h"
#include "delay.h"
#include "stm32l0xx_hal.h"
#include "stm32l0xx_hal_def.h"

/* ============================================================================
 * DEBUG CONFIGURATION - Comment out for production values
 * ============================================================================
 */
#define DEBUG_FAST_TIMEOUTS

#ifdef DEBUG_FAST_TIMEOUTS
/* Fast timeouts for testing */
#define RESTART_DELAY_BASE_MS 200   // Base delay before restart (was 1000)
#define RESTART_DELAY_MAX_MS 3000   // Max restart delay cap (was 30000)
#define GENERIC_RETRY_DELAY_MS 500  // Generic retry delay (was 10000)
#else
/* Production timeouts */
#define RESTART_DELAY_BASE_MS 1000
#define RESTART_DELAY_MAX_MS 30000
#define GENERIC_RETRY_DELAY_MS 10000
#endif

/* FSM State Variables */
static registration_fsm_t register_process = {.current_state = COM_REG_INIT,
                                              .failure_count = 0,
                                              .needs_hard_reset = false,
                                              .state_timeout_timer = -1,
                                              .state_delay_timer = -1,
                                              .error_backoff_timer = -1};

static udp_fsm_t udp_process = {.current_state = COM_UDP_IDLE,
                                .previous_state = COM_UDP_IDLE,
                                .pdp_context_ready = false,
                                .retry_count = 0,
                                .max_retries = 3,
                                .activation_failures = 0,
                                .last_attempt_time = 0,
                                .state_timeout_timer = -1};

/* Envelope for device registration */
static envelope_t envp = {
    .version = 1,
    .device_id = {0},
    .mac = {0},
};
static uint16_t envp_size = 0;

/* External variables */
bool is_device_connected = true;

/* Private function prototypes */
static void handle_failure(void);

/**
 * @brief Initialize Communication Module
 */
void Com_Init(void) {
  // Create delay timers
  register_process.state_timeout_timer = delay_timer_create();
  register_process.state_delay_timer = delay_timer_create();
  register_process.error_backoff_timer = delay_timer_create();
  udp_process.state_timeout_timer = delay_timer_create();

  // Initialize states
  register_process.current_state = COM_REG_INIT;
  udp_process.current_state = COM_UDP_IDLE;
}

/**
 * @brief Handle any failure: cleanup all state and schedule restart
 *
 * On any error (timeout, command failure, unexpected response), this function:
 * 1. Stops all active timers
 * 2. Resets the UDP FSM and AT core driver state
 * 3. Schedules a restart with exponential backoff
 * 4. Sets needs_hard_reset if too many consecutive failures
 */
static void handle_failure(void) {
  delay_stop(register_process.state_timeout_timer);
  delay_stop(register_process.state_delay_timer);

  // Reset UDP FSM so it re-establishes connection from scratch
  udp_process.current_state = COM_UDP_IDLE;
  udp_process.pdp_context_ready = false;
  udp_process.retry_count = 0;

  // Clear modem driver state in case we were mid-data-transfer
  ATCore_clear_data_mode();

  register_process.failure_count++;

  if (register_process.failure_count >= MAX_FAILURES_HARD_RESET) {
    register_process.needs_hard_reset = true;
    return;
  }

  // Exponential backoff: base * 2^failure_count, capped
  uint32_t backoff = RESTART_DELAY_BASE_MS
                     * (1 << register_process.failure_count);
  if (backoff > RESTART_DELAY_MAX_MS) backoff = RESTART_DELAY_MAX_MS;

  delay_start(register_process.error_backoff_timer, backoff);
  register_process.current_state = COM_REG_RESTART_WAIT;
}

/**
 * @brief Run device registration to completion (blocking)
 * @param timeout_ms Maximum time to wait for registration
 * @return true if registration successful, false if failed
 */
bool Com_register_device_blocking(uint32_t timeout_ms) {
  uint32_t start_time = HAL_GetTick();

  // Reset FSM to initial state
  register_process.current_state = COM_REG_INIT;
  register_process.failure_count = 0;
  register_process.needs_hard_reset = false;
  udp_process.current_state = COM_UDP_IDLE;
  udp_process.pdp_context_ready = false;
  udp_process.retry_count = 0;

  while (1) {
    // Run one iteration of the FSM
    Com_register_device_process();

    // Check for successful completion
    if (register_process.current_state == COM_REG_FINISHED) {
      return true;
    }

    // Check for fatal failure
    if (register_process.needs_hard_reset) {
      return false;
    }

    // Check for global timeout
    if ((HAL_GetTick() - start_time) > timeout_ms) {
      return false;
    }

    // Optional: Feed watchdog here
    // HAL_IWDG_Refresh(&hiwdg);

    // Small delay to prevent tight polling
    HAL_Delay(10);
  }
}

void Com_register_device_process(void) {
  atcmd_desc_t cmd = ATCMD_DESC_DEFAULT;

  // Check for state timeout — on any timeout, cleanup and restart
  if (register_process.state_timeout_timer >= 0) {
    if (delay_has_finished(register_process.state_timeout_timer)) {
      handle_failure();
      return;
    }
  }

  switch (register_process.current_state) {
    /* --------------------------------------------------------- */
    case COM_REG_INIT: {
      if (is_device_connected) {
        delay_start(register_process.state_timeout_timer, TIMEOUT_INIT);
        register_process.current_state = COM_REG_UDP_CTX;
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_UDP_CTX: {
      int udp_result = Com_UDP_context_process();

      if (udp_result == COM_UDP_PROCESS_DONE) {
        delay_start(register_process.state_delay_timer, 1000);
        delay_stop(register_process.state_timeout_timer);
        register_process.current_state = COM_REG_SEND;
      } else if (udp_result == COM_UDP_PROCESS_ERROR) {
        udp_process.current_state = COM_UDP_IDLE;
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_SEND: {
      if (!delay_has_finished(register_process.state_delay_timer)) break;

      envp.version = 1;
      envp.msg_type = 2;
      envp.seq = 0;
      envp.timestamp_high = 0;
      envp.timestamp_low = 0;

      const uint8_t *raw = Parser_fBuild_Envelope(&envp, &envp_size);

      cmd.id = CMD_AT_QISENDEX;
      cmd.cmd_mode = AT_CMD_WRITE;
      cmd.num_params[0] = BG95_CONNECT_ID;
      Parser_bytes_to_hex(raw, envp_size, cmd.str_params);
      cmd.str_params[envp_size * 2] = '\0';

      if (ATCore_send_cmd(&cmd)) {
        delay_start(register_process.state_timeout_timer, TIMEOUT_SEND);
        register_process.current_state = COM_REG_WAIT_SEND_OK;
      } else {
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_WAIT_SEND_OK: {
      if (!ATCore_is_response_ready()) break;

      ATCore_check_response();

      if (ATCore_get_response_status() == BG95_RESP_SEND_OK) {
        delay_stop(register_process.state_timeout_timer);
        delay_start(register_process.state_delay_timer, TIMEOUT_WAIT_RESPONSE);
        register_process.current_state = COM_REG_WAIT_SEND;
      } else {
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_WAIT_SEND: {
      if (!delay_has_finished(register_process.state_delay_timer)) break;

      cmd.id = CMD_AT_QIRD;
      cmd.cmd_mode = AT_CMD_WRITE_OPT;
      cmd.num_params[0] = BG95_CONNECT_ID;
      cmd.num_params[1] = 0;

      if (ATCore_send_cmd(&cmd)) {
        delay_start(register_process.state_timeout_timer, TIMEOUT_DATA_RDY);
        register_process.current_state = COM_REG_WAIT_DATA_RDY;
      } else {
        delay_start(register_process.state_timeout_timer,
                    GENERIC_RETRY_DELAY_MS);
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_VERIFY_DATA_RDY: {
      cmd.id = CMD_AT_QIRD;
      cmd.cmd_mode = AT_CMD_WRITE_OPT;
      cmd.num_params[0] = BG95_CONNECT_ID;
      cmd.num_params[1] = 0;

      if (ATCore_send_cmd(&cmd)) {
        delay_start(register_process.state_timeout_timer, TIMEOUT_DATA_RDY);
        register_process.current_state = COM_REG_WAIT_DATA_RDY;
      } else {
        delay_start(register_process.state_timeout_timer,
                    GENERIC_RETRY_DELAY_MS);
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_WAIT_DATA_RDY: {
      if (!ATCore_is_response_ready()) break;

      if (!ATCore_process_response()) {
        delay_start(register_process.state_timeout_timer,
                    GENERIC_RETRY_DELAY_MS);
        break;
      }

      if (ATCore_get_first_qird_value() > 0) {
        register_process.current_state = COM_REG_DATA_REQUEST;
      } else {
        delay_start(register_process.state_timeout_timer,
                    GENERIC_RETRY_DELAY_MS);
        register_process.current_state = COM_REG_VERIFY_DATA_RDY;
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_DATA_REQUEST: {
      cmd.id = CMD_AT_QIRD;
      cmd.cmd_mode = AT_CMD_WRITE;
      cmd.num_params[0] = BG95_CONNECT_ID;

      if (ATCore_send_cmd(&cmd)) {
        delay_start(register_process.state_timeout_timer, TIMEOUT_DATA_REQUEST);
        register_process.current_state = COM_REG_DATA_REQUEST_WAIT;
      } else {
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_DATA_REQUEST_WAIT: {
      if (!ATCore_is_response_ready()) break;

      ATCore_set_data_mode();

      if (ATCore_process_response()) {
        register_process.current_state = COM_REG_PROCESS_DATA;
      } else {
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_PROCESS_DATA: {
      uint16_t response_size;
      char buf[BG95_RX_BUFFER_SIZE];
      envelope_t rx_env;

      ATCore_get_last_response(buf, sizeof(buf), &response_size);
      Parser_fParse_Envelope(buf, response_size, &rx_env);

      if (rx_env.msg_type == 3) {
        ATCore_set_device_id(rx_env.device_id);
        ATCore_set_device_mac(rx_env.mac);
        envp.seq = rx_env.seq + 1;

        delay_start(register_process.state_timeout_timer,
                    GENERIC_RETRY_DELAY_MS);
        register_process.current_state = COM_REG_ACK;
      } else {
        // Unexpected message type
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_ACK: {
      ATCore_get_device_id(envp.device_id);
      ATCore_get_device_mac(envp.mac);

      envp.version = 1;
      envp.msg_type = 255;
      envp.timestamp_high = 0;
      envp.timestamp_low = 0;

      const uint8_t *raw = Parser_fBuild_Envelope(&envp, &envp_size);

      cmd.id = CMD_AT_QISENDEX;
      cmd.cmd_mode = AT_CMD_WRITE;
      cmd.num_params[0] = BG95_CONNECT_ID;
      Parser_bytes_to_hex(raw, envp_size, cmd.str_params);
      cmd.str_params[envp_size * 2] = '\0';

      if (ATCore_send_cmd(&cmd)) {
        delay_start(register_process.state_timeout_timer, TIMEOUT_SEND);
        register_process.current_state = COM_REG_WAIT_ACK_SEND;
      } else {
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_WAIT_ACK_SEND: {
      if (!ATCore_is_response_ready()) break;

      ATCore_check_response();

      if (ATCore_get_response_status() == BG95_RESP_SEND_OK) {
        register_process.failure_count = 0;
        delay_stop(register_process.state_timeout_timer);
        register_process.current_state = COM_REG_FINISHED;
      } else {
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_FINISHED: {
      // Idle forever - registration complete
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_RESTART_WAIT: {
      // Wait for backoff delay, then restart from scratch
      if (delay_has_finished(register_process.error_backoff_timer)) {
        register_process.current_state = COM_REG_INIT;
      }
    } break;
  }
}

int Com_UDP_context_process() {
  atcmd_desc_t cmd = (atcmd_desc_t)ATCMD_DESC_DEFAULT;

  // Check for timeout in waiting states
  if (udp_process.state_timeout_timer >= 0) {
    if (delay_has_finished(udp_process.state_timeout_timer)) {
      switch (udp_process.current_state) {
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
        cmd.id = CMD_AT_QIACT;
        cmd.cmd_mode = AT_CMD_WRITE_DEFAULT;
        if (ATCore_send_cmd(&cmd)) {
          delay_start(udp_process.state_timeout_timer, TIMEOUT_QIACT);
          udp_process.current_state = COM_UDP_QIACT_WAIT;
        }
      } else {
        // Already have PDP context, just need to open socket
        udp_process.current_state = COM_UDP_QIOPEN_SEND;
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
        printf("UDP context established.\r\n");
      } else {
        // Failed - retry or error
        if (udp_process.retry_count < udp_process.max_retries) {
          udp_process.retry_count++;
          udp_process.current_state = COM_UDP_QIOPEN_SEND;  // Retry QIOPEN
        } else {
          udp_process.current_state = COM_UDP_PROCESS_ERROR;
          printf("UDP setup failed (QIOPEN).\r\n");
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