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
#include "rtc.h"
#include "storage.h"
#include "stm32l0xx_hal.h"
#include "stm32l0xx_hal_def.h"
#include "pulse_counter.h"
#include "rlp.h"
#include "rtc.h"

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

static session_fsm_t session = {.current_state = COM_SES_IDLE,
                                .after_send_ok = COM_SES_KEEPALIVE_WAIT,
                                .failure_count = 0,
                                .needs_hard_reset = false,
                                .seq = 0,
                                .state_timeout_timer = -1,
                                .state_delay_timer = -1,
                                .error_backoff_timer = -1};

/* Buffer for session payload building */
static uint8_t ses_payload_buf[96];
static uint16_t ses_payload_len = 0;

/* IPv6 fetched at the start of each session for the announce */
static char ses_ipv6[48] = {0};

/* HAL_GetTick() value at the moment of last sent activity (announce, response,
 * or keepalive). Used to decide when to fire the next keepalive. */
static uint32_t ses_last_activity_ms = 0;

/* IPv6 address fetched after PDP context is up, used in registration payload */
static char reg_ipv6[48];
/* Registration payload buffer (IMEI string + IPv6 string, RLP-encoded) */
static uint8_t reg_payload_buf[80];
static uint16_t reg_payload_len = 0;

/* Seconds until next wake-up as dictated by HES. 0 means "no pending value". */
static uint32_t pending_wake_seconds = 0;

/* External variables */
bool is_device_connected = true;

/* Private function prototypes */
static void handle_failure(void);
static void session_handle_failure(void);
static bool session_send_envelope(uint8_t msg_type, const uint8_t *payload,
                                   uint16_t payload_size);
static uint16_t session_build_read_response(const uint8_t *req_payload,
                                             uint16_t req_len,
                                             uint8_t *out, uint16_t out_cap);

/**
 * @brief Initialize Communication Module
 */
void Com_Init(void) {
  // Create delay timers
  register_process.state_timeout_timer = delay_timer_create();
  register_process.state_delay_timer = delay_timer_create();
  register_process.error_backoff_timer = delay_timer_create();
  udp_process.state_timeout_timer = delay_timer_create();

  session.state_timeout_timer = delay_timer_create();
  session.state_delay_timer = delay_timer_create();
  session.error_backoff_timer = delay_timer_create();

  // Initialize states
  register_process.current_state = COM_REG_INIT;
  udp_process.current_state = COM_UDP_IDLE;
  session.current_state = COM_SES_IDLE;
}

/**
 * @brief Pop the wake-up delay (seconds) requested by HES, then clear it.
 * @return Delay in seconds, or 0 if HES has not provided a value.
 */
uint32_t Com_pop_pending_wake_seconds(void) {
  uint32_t v = pending_wake_seconds;
  pending_wake_seconds = 0;
  return v;
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

  // Stop DMA, clear all RX flags and buffer
  ATCore_reset_rx();

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
      if (Storage_is_registered()) {
        printf("Skipping registration — credentials loaded from EEPROM.\r\n");
        register_process.current_state = COM_REG_FINISHED;
        break;
      }
      if (is_device_connected) {
        delay_start(register_process.state_timeout_timer, TIMEOUT_INIT);
        register_process.current_state = COM_REG_UDP_CTX;
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_UDP_CTX: {
      int udp_result = Com_UDP_context_process();

      if (udp_result == COM_UDP_PROCESS_DONE) {
        delay_stop(register_process.state_timeout_timer);
        register_process.current_state = COM_REG_FETCH_IPV6;
      } else if (udp_result == COM_UDP_PROCESS_ERROR) {
        udp_process.current_state = COM_UDP_IDLE;
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_FETCH_IPV6: {
      cmd.id = CMD_AT_CGPADDR;
      cmd.cmd_mode = AT_CMD_WRITE;
      cmd.num_params[0] = 1;
      cmd.nb_num_params = 1;
      cmd.total_params = 1;
      cmd.param_types[0] = AT_PARAM_NUM;

      if (ATCore_send_cmd(&cmd)) {
        delay_start(register_process.state_timeout_timer, 3000);
        register_process.current_state = COM_REG_WAIT_FETCH_IPV6;
      } else {
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_WAIT_FETCH_IPV6: {
      if (!ATCore_is_response_ready()) break;

      delay_stop(register_process.state_timeout_timer);
      ATCore_process_response();

      char resp[BG95_RX_BUFFER_SIZE];
      uint16_t resp_size;
      ATCore_get_last_response(resp, sizeof(resp), &resp_size);

      /* Parse IP between quotes: +CGPADDR: 1,"<IP>" */
      reg_ipv6[0] = '\0';
      char *start = strchr(resp, '"');
      if (start) {
        start++;
        char *end = strchr(start, '"');
        if (end && end > start) {
          uint16_t len = (uint16_t)(end - start);
          if (len < sizeof(reg_ipv6)) {
            memcpy(reg_ipv6, start, len);
            reg_ipv6[len] = '\0';
          }
        }
      }

      delay_start(register_process.state_delay_timer, 1000);
      register_process.current_state = COM_REG_SEND;
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_SEND: {
      if (!delay_has_finished(register_process.state_delay_timer)) break;

      envp.version = 1;
      envp.msg_type = MSG_TYPE_REGISTER_REQUEST;
      envp.seq = 0;
      envp.timestamp_high = 0;
      envp.timestamp_low = 0;

      /* Build RLP payload: list[ imei_str, ipv6_str ] */
      char imei[STORAGE_IMEI_LEN] = {0};
      Storage_load_imei(imei, sizeof(imei));

      rlp_writer_t w;
      rlp_writer_init(&w, reg_payload_buf, sizeof(reg_payload_buf));
      uint16_t bm = rlp_list_begin(&w);
      rlp_encode_string(&w, imei);
      rlp_encode_string(&w, reg_ipv6);
      rlp_list_end(&w, bm);
      reg_payload_len = rlp_writer_ok(&w) ? rlp_writer_len(&w) : 0;

      const uint8_t *raw = Parser_fBuild_Envelope_w_payload(
          &envp, reg_payload_buf, reg_payload_len, &envp_size);

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
      const uint8_t *payload;
      uint16_t payload_size;

      ATCore_get_last_response(buf, sizeof(buf), &response_size);
      Parser_fParse_Envelope_w_payload((const uint8_t *)buf, response_size,
                                       &rx_env, &payload, &payload_size);

      if (rx_env.msg_type == MSG_TYPE_REGISTER_RESPONSE) {
        /* Sync RTC from envelope timestamp provided by HES */
        RTC_set_datetime(rx_env.timestamp_high, rx_env.timestamp_low);

        /* Decode RLP payload: list[ flag(u8), next_wake_time(u64) ] */
        if (payload_size > 0) {
          rlp_reader_t r, list;
          rlp_reader_init(&r, payload, payload_size);
          if (rlp_enter_list(&r, &list)) {
            uint8_t flag = 0xFF;
            uint64_t next_wake_time = 0;
            rlp_decode_uint8(&list, &flag);
            rlp_decode_uint64(&list, &next_wake_time);

            if (flag != 0x00) {
              handle_failure();
              break;
            }

            /* Save delta (in seconds) for main loop to honor as next wake-up.
             * Subtract COLD_START_OFFSET_SEC so the device wakes early enough
             * to power on the BG95, attach, fetch IPv6 and send the announce
             * before the agreed contact moment. */
            uint32_t now_high, now_low;
            RTC_get_timestamp(&now_high, &now_low);
            uint64_t now = ((uint64_t)now_high << 32) | now_low;
            if (next_wake_time > now + COLD_START_OFFSET_SEC) {
              uint64_t delta = next_wake_time - now - COLD_START_OFFSET_SEC;
              if (delta < 86400ULL) {
                pending_wake_seconds = (uint32_t)delta;
              }
            }
          }
        }

        ATCore_set_device_id(rx_env.device_id);
        ATCore_set_device_mac(rx_env.mac);
        Storage_save_credentials(rx_env.device_id, rx_env.mac);
        envp.seq = rx_env.seq + 1;

        delay_start(register_process.state_timeout_timer, GENERIC_RETRY_DELAY_MS);
        register_process.current_state = COM_REG_ACK;
      } else {
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
        delay_start(register_process.state_delay_timer, TIMEOUT_WAIT_RESPONSE);
        register_process.current_state = COM_REG_DRAIN_POLL;
      } else {
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_DRAIN_POLL: {
      /* Best-effort: give the HES a moment, then check if confirm ACK arrived. */
      if (!delay_has_finished(register_process.state_delay_timer)) break;

      cmd.id = CMD_AT_QIRD;
      cmd.cmd_mode = AT_CMD_WRITE_OPT;
      cmd.num_params[0] = BG95_CONNECT_ID;
      cmd.num_params[1] = 0;
      if (ATCore_send_cmd(&cmd)) {
        delay_start(register_process.state_delay_timer, TIMEOUT_DATA_RDY);
        register_process.current_state = COM_REG_DRAIN_POLL_WAIT;
      } else {
        register_process.current_state = COM_REG_FINISHED;
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_DRAIN_POLL_WAIT: {
      if (delay_has_finished(register_process.state_delay_timer)) {
        register_process.current_state = COM_REG_FINISHED;
        break;
      }
      if (!ATCore_is_response_ready()) break;

      ATCore_process_response();
      delay_stop(register_process.state_delay_timer);

      if (ATCore_get_first_qird_value() > 0) {
        cmd.id = CMD_AT_QIRD;
        cmd.cmd_mode = AT_CMD_WRITE;
        cmd.num_params[0] = BG95_CONNECT_ID;
        if (ATCore_send_cmd(&cmd)) {
          delay_start(register_process.state_delay_timer, TIMEOUT_DATA_REQUEST);
          register_process.current_state = COM_REG_DRAIN_READ_WAIT;
        } else {
          register_process.current_state = COM_REG_FINISHED;
        }
      } else {
        register_process.current_state = COM_REG_FINISHED;
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_DRAIN_READ_WAIT: {
      if (delay_has_finished(register_process.state_delay_timer)) {
        register_process.current_state = COM_REG_FINISHED;
        break;
      }
      if (!ATCore_is_response_ready()) break;

      ATCore_set_data_mode();
      ATCore_process_response();  /* discard — data not needed */
      delay_stop(register_process.state_delay_timer);
      register_process.current_state = COM_REG_FINISHED;
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
          delay_start(udp_process.state_timeout_timer, TIMEOUT_QIACT);
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
          delay_start(udp_process.state_timeout_timer, TIMEOUT_QIACT);
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

/* ============================================================================
 * Session FSM — Periodic data exchange with HES
 * ============================================================================
 */

/**
 * @brief Handle session failure: stop timers, reset UDP, exponential backoff
 */
static void session_handle_failure(void) {
  delay_stop(session.state_timeout_timer);
  delay_stop(session.state_delay_timer);

  udp_process.current_state = COM_UDP_IDLE;
  udp_process.pdp_context_ready = false;
  udp_process.retry_count = 0;

  ATCore_reset_rx();

  session.failure_count++;

  if (session.failure_count >= MAX_FAILURES_HARD_RESET) {
    session.needs_hard_reset = true;
    session.current_state = COM_SES_ERROR;
    return;
  }

  uint32_t backoff = RESTART_DELAY_BASE_MS * (1 << session.failure_count);
  if (backoff > RESTART_DELAY_MAX_MS) backoff = RESTART_DELAY_MAX_MS;

  delay_start(session.error_backoff_timer, backoff);
  session.current_state = COM_SES_RESTART_WAIT;
}

/**
 * @brief Build and send an envelope via QISENDEX
 * @param msg_type  Message type for the envelope
 * @param payload   Payload data (NULL for header-only envelopes)
 * @param payload_size  Size of payload (0 for header-only)
 * @return true if command was sent successfully
 */
static bool session_send_envelope(uint8_t msg_type, const uint8_t *payload,
                                   uint16_t payload_size) {
  envelope_t env;
  env.version = 1;
  env.msg_type = msg_type;
  ATCore_get_device_id(env.device_id);
  ATCore_get_device_mac(env.mac);
  env.seq = session.seq++;

  uint32_t ts_hi, ts_lo;
  RTC_get_timestamp(&ts_hi, &ts_lo);
  env.timestamp_high = ts_hi;
  env.timestamp_low = ts_lo;

  uint16_t total_size;
  const uint8_t *raw;

  if (payload != NULL && payload_size > 0) {
    raw = Parser_fBuild_Envelope_w_payload(&env, payload, payload_size,
                                            &total_size);
  } else {
    raw = Parser_fBuild_Envelope(&env, &total_size);
  }

  if (raw == NULL) return false;

  atcmd_desc_t cmd = (atcmd_desc_t)ATCMD_DESC_DEFAULT;
  cmd.id = CMD_AT_QISENDEX;
  cmd.cmd_mode = AT_CMD_WRITE;
  cmd.num_params[0] = BG95_CONNECT_ID;
  Parser_bytes_to_hex(raw, total_size, cmd.str_params);
  cmd.str_params[total_size * 2] = '\0';

  return ATCore_send_cmd(&cmd);
}

/**
 * @brief Append an OBIS [code, value] pair to an RLP writer.
 *        Value bytes are big-endian, byte-order matching what the HES expects.
 */
static void session_append_obis_value(rlp_writer_t *w, const char *code,
                                       const uint8_t *value, uint16_t value_len) {
  uint16_t bm = rlp_list_begin(w);
  rlp_encode_string(w, code);
  rlp_encode_bytes(w, value, value_len);
  rlp_list_end(w, bm);
}

/**
 * @brief Build a READ_RESPONSE payload from a list of OBIS codes requested
 *        by the HES.
 *
 *  Request payload: RLP list [ obis_code_str, ... ]
 *  Response payload: RLP list [ [obis_code_str, value_bytes], ... ]
 *
 *  Supported OBIS codes:
 *    "1.0.1" → water volume (uint64 BE, 8 B)
 *    "0.9.4" → current timestamp (uint64 BE, 8 B)
 *    "C.6.1" → battery level (uint8, 1 B)
 *
 * @return Size of encoded response, or 0 on error.
 */
static uint16_t session_build_read_response(const uint8_t *req_payload,
                                             uint16_t req_len,
                                             uint8_t *out, uint16_t out_cap) {
  rlp_reader_t r;
  rlp_reader_init(&r, req_payload, req_len);

  rlp_reader_t list_r;
  if (!rlp_enter_list(&r, &list_r)) return 0;

  rlp_writer_t w;
  rlp_writer_init(&w, out, out_cap);
  uint16_t outer = rlp_list_begin(&w);

  while (!rlp_reader_done(&list_r)) {
    char code[16];
    uint16_t code_len;
    if (!rlp_decode_string(&list_r, code, sizeof(code), &code_len)) return 0;

    if (strcmp(code, OBIS_WATER_VOLUME) == 0) {
      uint64_t v = (uint64_t)PulseCounter_get_volume_liters();
      uint8_t be[8];
      for (int i = 0; i < 8; i++) be[i] = (uint8_t)(v >> (56 - i * 8));
      session_append_obis_value(&w, code, be, sizeof(be));
    } else if (strcmp(code, OBIS_CLOCK) == 0) {
      uint32_t ts_hi, ts_lo;
      RTC_get_timestamp(&ts_hi, &ts_lo);
      uint64_t ts = ((uint64_t)ts_hi << 32) | ts_lo;
      uint8_t be[8];
      for (int i = 0; i < 8; i++) be[i] = (uint8_t)(ts >> (56 - i * 8));
      session_append_obis_value(&w, code, be, sizeof(be));
    } else if (strcmp(code, OBIS_BATTERY) == 0) {
      /* TODO: wire to real ADC reading; placeholder fixed value for now. */
      uint8_t bat = 85;
      session_append_obis_value(&w, code, &bat, 1);
    } else {
      /* Unknown OBIS code — emit empty value to keep the response well-formed. */
      session_append_obis_value(&w, code, NULL, 0);
    }
  }

  rlp_list_end(&w, outer);
  if (!rlp_writer_ok(&w)) return 0;
  return rlp_writer_len(&w);
}

/**
 * @brief Build a WRITE_RESPONSE payload that echoes back the OBIS codes the
 *        HES asked the device to write.
 *
 *  Response payload: RLP list [ uint8 success, list[ code_str, ... ] ]
 *
 *  Walks the WRITE_REQUEST payload to extract the codes (we do not store
 *  them separately to keep memory low).
 */
static uint16_t session_build_write_response(const uint8_t *req_payload,
                                              uint16_t req_len,
                                              bool success,
                                              uint8_t *out, uint16_t out_cap) {
  rlp_reader_t r;
  rlp_reader_init(&r, req_payload, req_len);
  rlp_reader_t list_r;
  if (!rlp_enter_list(&r, &list_r)) return 0;

  rlp_writer_t w;
  rlp_writer_init(&w, out, out_cap);
  uint16_t outer = rlp_list_begin(&w);
  rlp_encode_uint8(&w, success ? 1 : 0);
  uint16_t codes = rlp_list_begin(&w);

  while (!rlp_reader_done(&list_r)) {
    rlp_reader_t pair;
    if (!rlp_enter_list(&list_r, &pair)) return 0;
    char code[16];
    uint16_t code_len;
    if (!rlp_decode_string(&pair, code, sizeof(code), &code_len)) return 0;
    rlp_encode_string(&w, code);
    /* Skip the value field of the pair — not needed for the response. */
  }

  rlp_list_end(&w, codes);
  rlp_list_end(&w, outer);
  if (!rlp_writer_ok(&w)) return 0;
  return rlp_writer_len(&w);
}

/**
 * @brief Look up OBIS_NEXT_WAKE in a WRITE_REQUEST payload and return its
 *        uint64 BE value (Unix epoch seconds for the next agreed wake-up).
 *
 * @return true if found and decoded successfully.
 */
static bool session_extract_next_wake(const uint8_t *req_payload,
                                       uint16_t req_len,
                                       uint64_t *next_wake_out) {
  rlp_reader_t r;
  rlp_reader_init(&r, req_payload, req_len);
  rlp_reader_t list_r;
  if (!rlp_enter_list(&r, &list_r)) return false;

  while (!rlp_reader_done(&list_r)) {
    rlp_reader_t pair;
    if (!rlp_enter_list(&list_r, &pair)) return false;

    char code[16];
    uint16_t code_len;
    if (!rlp_decode_string(&pair, code, sizeof(code), &code_len)) return false;

    uint8_t value[16];
    uint16_t value_len;
    if (!rlp_decode_bytes(&pair, value, sizeof(value), &value_len)) return false;

    if (strcmp(code, OBIS_NEXT_WAKE) == 0 && value_len == 8) {
      uint64_t v = 0;
      for (int i = 0; i < 8; i++) v = (v << 8) | value[i];
      *next_wake_out = v;
      return true;
    }
  }
  return false;
}

/**
 * @brief Start a new periodic session
 */
void Com_session_start(void) {
  ATCore_reset_rx();  /* Clear any ORE/URCs accumulated since last DMA arm */
  session.current_state = COM_SES_UDP_CTX;
  session.after_send_ok = COM_SES_KEEPALIVE_WAIT;
  session.failure_count = 0;
  session.needs_hard_reset = false;
  session.seq = 0;
  ses_ipv6[0] = '\0';
  ses_last_activity_ms = HAL_GetTick();

  udp_process.current_state = COM_UDP_IDLE;
  udp_process.pdp_context_ready = false;
  udp_process.retry_count = 0;
}

/**
 * @brief Check if the session FSM has completed
 * @return true if session is done or in error state
 */
bool Com_is_session_done(void) {
  return (session.current_state == COM_SES_DONE ||
          session.current_state == COM_SES_ERROR);
}

/**
 * @brief Helper: send an envelope and transition to WAIT_SEND_OK with the
 *        given after_send_ok target. Records activity timestamp for the
 *        keepalive timer.
 */
static void session_send_and_wait(uint8_t msg_type, const uint8_t *payload,
                                   uint16_t payload_len,
                                   session_state_t after) {
  if (session_send_envelope(msg_type, payload, payload_len)) {
    ses_last_activity_ms = HAL_GetTick();
    delay_start(session.state_timeout_timer, TIMEOUT_SEND);
    session.after_send_ok = after;
    session.current_state = COM_SES_WAIT_SEND_OK;
  } else {
    session_handle_failure();
  }
}

/**
 * @brief Main session FSM — call repeatedly from main loop.
 *
 * Flow: UDP_CTX → FETCH_IPV6 → SEND_ANNOUNCE → WAIT_SEND_OK →
 *       KEEPALIVE_WAIT (poll + 10s keepalive) → READ_HES_MSG →
 *       PROCESS_HES_MSG (dispatch by msg_type) → SEND response →
 *       back to KEEPALIVE_WAIT, until HES sends ACK → DONE.
 */
void Com_session_process(void) {
  atcmd_desc_t cmd = (atcmd_desc_t)ATCMD_DESC_DEFAULT;

  /* Global timeout check */
  if (session.state_timeout_timer >= 0 &&
      session.current_state != COM_SES_IDLE &&
      session.current_state != COM_SES_KEEPALIVE_WAIT &&
      session.current_state != COM_SES_DONE &&
      session.current_state != COM_SES_ERROR) {
    if (delay_has_finished(session.state_timeout_timer)) {
      session_handle_failure();
      return;
    }
  }

  switch (session.current_state) {
    case COM_SES_IDLE:
      break;

    /* ---- Establish UDP connection to HES ---- */
    case COM_SES_UDP_CTX: {
      int udp_result = Com_UDP_context_process();
      if (udp_result == COM_UDP_PROCESS_DONE) {
        delay_stop(session.state_timeout_timer);
        session.current_state = COM_SES_FETCH_IPV6;
      } else if (udp_result == COM_UDP_PROCESS_ERROR) {
        udp_process.current_state = COM_UDP_IDLE;
        session_handle_failure();
      }
    } break;

    /* ---- Read current IPv6 assigned by the network ---- */
    case COM_SES_FETCH_IPV6: {
      cmd.id = CMD_AT_CGPADDR;
      cmd.cmd_mode = AT_CMD_WRITE;
      cmd.num_params[0] = 1;
      cmd.nb_num_params = 1;
      cmd.total_params = 1;
      cmd.param_types[0] = AT_PARAM_NUM;
      if (ATCore_send_cmd(&cmd)) {
        delay_start(session.state_timeout_timer, 3000);
        session.current_state = COM_SES_WAIT_FETCH_IPV6;
      } else {
        session_handle_failure();
      }
    } break;

    case COM_SES_WAIT_FETCH_IPV6: {
      if (!ATCore_is_response_ready()) break;
      delay_stop(session.state_timeout_timer);
      ATCore_process_response();

      char resp[BG95_RX_BUFFER_SIZE];
      uint16_t resp_size;
      ATCore_get_last_response(resp, sizeof(resp), &resp_size);

      ses_ipv6[0] = '\0';
      char *start = strchr(resp, '"');
      if (start) {
        start++;
        char *end = strchr(start, '"');
        if (end && end > start) {
          uint16_t len = (uint16_t)(end - start);
          if (len < sizeof(ses_ipv6)) {
            memcpy(ses_ipv6, start, len);
            ses_ipv6[len] = '\0';
          }
        }
      }
      session.current_state = COM_SES_SEND_ANNOUNCE;
    } break;

    /* ---- Announce: REGISTER_REQUEST with stored device_id (HES updates IP) ---- */
    case COM_SES_SEND_ANNOUNCE: {
      char imei[STORAGE_IMEI_LEN] = {0};
      Storage_load_imei(imei, sizeof(imei));

      rlp_writer_t w;
      rlp_writer_init(&w, ses_payload_buf, sizeof(ses_payload_buf));
      uint16_t bm = rlp_list_begin(&w);
      rlp_encode_string(&w, imei);
      rlp_encode_string(&w, ses_ipv6);
      rlp_list_end(&w, bm);
      ses_payload_len = rlp_writer_ok(&w) ? rlp_writer_len(&w) : 0;
      if (ses_payload_len == 0) {
        session_handle_failure();
        break;
      }
      session_send_and_wait(MSG_TYPE_REGISTER_REQUEST, ses_payload_buf,
                             ses_payload_len, COM_SES_KEEPALIVE_WAIT);
    } break;

    /* ---- Generic SEND_OK wait — transitions to session.after_send_ok ---- */
    case COM_SES_WAIT_SEND_OK: {
      if (!ATCore_is_response_ready()) break;
      ATCore_check_response();
      if (ATCore_get_response_status() == BG95_RESP_SEND_OK) {
        delay_stop(session.state_timeout_timer);
        delay_start(session.state_delay_timer, TIMEOUT_WAIT_RESPONSE);
        session.current_state = session.after_send_ok;
      } else {
        session_handle_failure();
      }
    } break;

    /* ---- Wait for HES message; fire keepalive every 10s of silence ---- */
    case COM_SES_KEEPALIVE_WAIT: {
      if ((HAL_GetTick() - ses_last_activity_ms) >= SESSION_KEEPALIVE_PERIOD_MS) {
        session.current_state = COM_SES_SEND_KEEPALIVE;
        break;
      }
      if (delay_has_finished(session.state_delay_timer)) {
        session.current_state = COM_SES_CHECK_HES_DATA;
      }
    } break;

    case COM_SES_CHECK_HES_DATA: {
      cmd.id = CMD_AT_QIRD;
      cmd.cmd_mode = AT_CMD_WRITE_OPT;
      cmd.num_params[0] = BG95_CONNECT_ID;
      cmd.num_params[1] = 0;
      if (ATCore_send_cmd(&cmd)) {
        delay_start(session.state_timeout_timer, TIMEOUT_DATA_RDY);
        session.current_state = COM_SES_CHECK_HES_DATA_WAIT;
      } else {
        delay_start(session.state_delay_timer, GENERIC_RETRY_DELAY_MS);
        session.current_state = COM_SES_KEEPALIVE_WAIT;
      }
    } break;

    case COM_SES_CHECK_HES_DATA_WAIT: {
      if (!ATCore_is_response_ready()) break;
      if (!ATCore_process_response()) {
        delay_start(session.state_delay_timer, GENERIC_RETRY_DELAY_MS);
        session.current_state = COM_SES_KEEPALIVE_WAIT;
        break;
      }
      if (ATCore_get_first_qird_value() > 0) {
        delay_stop(session.state_timeout_timer);
        session.current_state = COM_SES_READ_HES_MSG;
      } else {
        delay_start(session.state_delay_timer, TIMEOUT_WAIT_RESPONSE);
        session.current_state = COM_SES_KEEPALIVE_WAIT;
      }
    } break;

    case COM_SES_SEND_KEEPALIVE: {
      session_send_and_wait(MSG_TYPE_KEEPALIVE, NULL, 0, COM_SES_KEEPALIVE_WAIT);
    } break;

    /* ---- Read incoming HES message ---- */
    case COM_SES_READ_HES_MSG: {
      cmd.id = CMD_AT_QIRD;
      cmd.cmd_mode = AT_CMD_WRITE;
      cmd.num_params[0] = BG95_CONNECT_ID;
      if (ATCore_send_cmd(&cmd)) {
        delay_start(session.state_timeout_timer, TIMEOUT_DATA_REQUEST);
        session.current_state = COM_SES_READ_HES_MSG_WAIT;
      } else {
        session_handle_failure();
      }
    } break;

    case COM_SES_READ_HES_MSG_WAIT: {
      if (!ATCore_is_response_ready()) break;
      ATCore_set_data_mode();
      if (ATCore_process_response()) {
        delay_stop(session.state_timeout_timer);
        session.current_state = COM_SES_PROCESS_HES_MSG;
      } else {
        session_handle_failure();
      }
    } break;

    /* ---- Parse and dispatch HES message ---- */
    case COM_SES_PROCESS_HES_MSG: {
      uint16_t resp_size;
      char buf[BG95_RX_BUFFER_SIZE];
      envelope_t rx_env;
      ATCore_get_last_response(buf, sizeof(buf), &resp_size);

      const uint8_t *payload_ptr = NULL;
      uint16_t payload_len = 0;
      if (!Parser_fParse_Envelope_w_payload(
              (const uint8_t *)buf, resp_size, &rx_env,
              &payload_ptr, &payload_len)) {
        session_handle_failure();
        break;
      }

      switch (rx_env.msg_type) {
        case MSG_TYPE_REGISTER_RESPONSE: {
          /* HES confirmed the IP-update announce. Send our ACK back; the
           * authoritative next_wake_time arrives later in WRITE_REQUEST. */
          session_send_and_wait(MSG_TYPE_ACK, NULL, 0, COM_SES_KEEPALIVE_WAIT);
        } break;

        case MSG_TYPE_HANDSHAKE: {
          session_send_and_wait(MSG_TYPE_HANDSHAKE_RESPONSE, NULL, 0,
                                 COM_SES_KEEPALIVE_WAIT);
        } break;

        case MSG_TYPE_READ_REQUEST: {
          Storage_save_pulse_count(PulseCounter_get_count());
          ses_payload_len = session_build_read_response(
              payload_ptr, payload_len,
              ses_payload_buf, sizeof(ses_payload_buf));
          if (ses_payload_len == 0) {
            session_handle_failure();
            break;
          }
          session_send_and_wait(MSG_TYPE_READ_RESPONSE, ses_payload_buf,
                                 ses_payload_len, COM_SES_KEEPALIVE_WAIT);
        } break;

        case MSG_TYPE_WRITE_REQUEST: {
          uint64_t next_wake = 0;
          bool got_wake = session_extract_next_wake(payload_ptr, payload_len,
                                                     &next_wake);
          if (got_wake) {
            uint32_t now_hi, now_lo;
            RTC_get_timestamp(&now_hi, &now_lo);
            uint64_t now = ((uint64_t)now_hi << 32) | now_lo;
            if (next_wake > now + COLD_START_OFFSET_SEC) {
              uint64_t delta = next_wake - now - COLD_START_OFFSET_SEC;
              if (delta < 86400ULL) pending_wake_seconds = (uint32_t)delta;
            }
          }
          ses_payload_len = session_build_write_response(
              payload_ptr, payload_len, got_wake,
              ses_payload_buf, sizeof(ses_payload_buf));
          if (ses_payload_len == 0) {
            session_handle_failure();
            break;
          }
          session_send_and_wait(MSG_TYPE_WRITE_RESPONSE, ses_payload_buf,
                                 ses_payload_len, COM_SES_KEEPALIVE_WAIT);
        } break;

        case MSG_TYPE_ACK: {
          /* HES closed the session. Reset pulse counter checkpoint and
           * mark the session as complete. */
          PulseCounter_reset();
          Storage_save_pulse_count(0);
          session.failure_count = 0;
          session.current_state = COM_SES_DONE;
        } break;

        default:
          session_handle_failure();
          break;
      }
    } break;

    case COM_SES_DONE:
    case COM_SES_ERROR:
      break;

    case COM_SES_RESTART_WAIT: {
      if (delay_has_finished(session.error_backoff_timer)) {
        session.current_state = COM_SES_UDP_CTX;
        udp_process.current_state = COM_UDP_IDLE;
        udp_process.pdp_context_ready = false;
        udp_process.retry_count = 0;
      }
    } break;
  }
}