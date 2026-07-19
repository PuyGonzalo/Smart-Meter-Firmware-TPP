/**
 * @file atcom_registration.c
 * @ingroup atcom_registration
 * @brief Initial registration FSM against the HES.
 */

#include "atcom_registration.h"

#include <string.h>

#include "at_core.h"
#include "at_net.h"
#include "at_parser.h"
#include "atcom_internal.h"
#include "atcom_udp.h"
#include "bg95_at_cmd_lib.h"
#include "delay.h"
#include "iwdg.h"
#include "rlp.h"
#include "rtc.h"
#include "storage.h"
#include "stm32l0xx_hal.h"

static registration_fsm_t register_process = {.current_state = COM_REG_INIT,
                                              .failure_count = 0,
                                              .needs_hard_reset = false,
                                              .state_timeout_timer = -1,
                                              .state_delay_timer = -1,
                                              .error_backoff_timer = -1,
                                              .poll_start_tick_ms = 0};

/* Envelope for device registration */
static envelope_t envp = {
    .version = 1,
    .device_id = {0},
    .mac = {0},
};
static uint16_t envp_size = 0;

/* IPv6 address fetched after PDP context is up, used in registration payload */
static char reg_ipv6[48];
/* Registration payload buffer (IMEI string + IPv6 string, RLP-encoded) */
static uint8_t reg_payload_buf[80];
static uint16_t reg_payload_len = 0;

/* Stub: en el futuro chequear estado de red real. Hoy siempre true. */
static bool is_device_connected = true;

static void handle_failure(void);

void atcom_registration_init(void) {
  register_process.state_timeout_timer = delay_timer_create();
  register_process.state_delay_timer = delay_timer_create();
  register_process.error_backoff_timer = delay_timer_create();
  register_process.current_state = COM_REG_INIT;
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
  atcom_udp_reset();

  // Stop DMA, clear all RX flags and buffer
  ATCore_reset_rx();

  register_process.failure_count++;

  if (register_process.failure_count >= MAX_FAILURES_HARD_RESET) {
    register_process.needs_hard_reset = true;
    register_process.current_state = COM_REG_RESTART_WAIT;  /* idle state; blocking loop exits on needs_hard_reset */
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
 * @brief Run device registration to completion (blocking).
 * @return true if registration successful, false if failed.
 */
bool Com_register_device_blocking(uint32_t timeout_ms) {
  uint32_t start_time = HAL_GetTick();

  // Reset FSM to initial state
  register_process.current_state = COM_REG_INIT;
  register_process.failure_count = 0;
  register_process.needs_hard_reset = false;
  atcom_udp_reset();

  while (1) {
    // Run one iteration of the FSM
    Com_register_device_process();

    // Check for fatal failure first (takes priority over any state)
    if (register_process.needs_hard_reset) {
      return false;
    }

    // Check for successful completion
    if (register_process.current_state == COM_REG_FINISHED) {
      return true;
    }

    // Check for global timeout
    if ((HAL_GetTick() - start_time) > timeout_ms) {
      return false;
    }

    // Feed the watchdog: registration can block for minutes (QIACT/QIOPEN
    // retries), far longer than the IWDG window.
    HAL_IWDG_Refresh(&hiwdg);

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
        atcom_udp_reset();
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
        delay_start(register_process.state_timeout_timer, TIMEOUT_CGPADDR);
        register_process.current_state = COM_REG_WAIT_FETCH_IPV6;
      } else {
        handle_failure();
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_WAIT_FETCH_IPV6: {
      if (delay_has_finished(register_process.state_timeout_timer)) { handle_failure(); break; }
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
      if (delay_has_finished(register_process.state_timeout_timer)) { handle_failure(); break; }
      if (!ATCore_is_response_ready()) break;

      ATCore_check_response();

      if (ATCore_get_response_status() == BG95_RESP_SEND_OK) {
        delay_stop(register_process.state_timeout_timer);
        register_process.poll_start_tick_ms = HAL_GetTick();
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
      if (delay_has_finished(register_process.state_timeout_timer)) { handle_failure(); break; }
      if (!ATCore_is_response_ready()) break;

      if (!ATCore_process_response()) {
        delay_start(register_process.state_timeout_timer,
                    GENERIC_RETRY_DELAY_MS);
        break;
      }

      if (ATCore_get_first_qird_value() > 0) {
        register_process.current_state = COM_REG_DATA_REQUEST;
      } else if ((HAL_GetTick() - register_process.poll_start_tick_ms) >= POLL_RESEND_TIMEOUT_MS) {
        register_process.poll_start_tick_ms = HAL_GetTick();
        delay_start(register_process.state_delay_timer, GENERIC_RETRY_DELAY_MS);
        register_process.current_state = COM_REG_SEND;
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
      if (delay_has_finished(register_process.state_timeout_timer)) { handle_failure(); break; }
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
                atcom_set_pending_wake_seconds((uint32_t)delta);
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
      if (delay_has_finished(register_process.state_timeout_timer)) { handle_failure(); break; }
      if (!ATCore_is_response_ready()) break;

      ATCore_check_response();

      if (ATCore_get_response_status() == BG95_RESP_SEND_OK) {
        register_process.failure_count = 0;
        delay_stop(register_process.state_timeout_timer);
        delay_start(register_process.state_timeout_timer, TIMEOUT_DRAIN_WINDOW);
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
        ATCore_reset_rx();
        register_process.current_state = COM_REG_FINISHED;
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_DRAIN_POLL_WAIT: {
      if (delay_has_finished(register_process.state_delay_timer)) {
        ATCore_reset_rx();
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
          ATCore_reset_rx();
          register_process.current_state = COM_REG_FINISHED;
        }
      } else if (!delay_has_finished(register_process.state_timeout_timer)) {
        delay_start(register_process.state_delay_timer, TIMEOUT_DRAIN_RETRY);
        register_process.current_state = COM_REG_DRAIN_POLL;
      } else {
        ATCore_reset_rx();
        register_process.current_state = COM_REG_FINISHED;
      }
    } break;

    /* --------------------------------------------------------- */
    case COM_REG_DRAIN_READ_WAIT: {
      if (delay_has_finished(register_process.state_delay_timer)) {
        ATCore_reset_rx();
        register_process.current_state = COM_REG_FINISHED;
        break;
      }
      if (!ATCore_is_response_ready()) break;

      ATCore_set_data_mode();
      ATCore_process_response();  /* discard — data not needed */
      delay_stop(register_process.state_delay_timer);
      ATCore_reset_rx();
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
