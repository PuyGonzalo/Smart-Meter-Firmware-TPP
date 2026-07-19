/**
 * @file atcom_session.c
 * @ingroup atcom_session
 * @brief Session FSM — periodic data exchange with the HES.
 */

#include "atcom_session.h"

#include <stddef.h>
#include <string.h>

#include "at_core.h"
#include "at_net.h"
#include "at_parser.h"
#include "atcom_internal.h"
#include "atcom_udp.h"
#include "bg95_at_cmd_lib.h"
#include "delay.h"
#include "pulse_counter.h"
#include "rlp.h"
#include "rtc.h"
#include "storage.h"
#include "stm32l0xx_hal.h"

static session_fsm_t session = {.current_state = COM_SES_IDLE,
                                .after_send_ok = COM_SES_POLL_WAIT,
                                .failure_count = 0,
                                .needs_hard_reset = false,
                                .seq = 0,
                                .state_timeout_timer = -1,
                                .state_delay_timer = -1,
                                .error_backoff_timer = -1,
                                .last_msg_type = 0,
                                .last_payload_len = 0,
                                .poll_start_tick_ms = 0,
                                .can_resend = false};

/* Buffer for session payload building */
static uint8_t ses_payload_buf[96];
static uint16_t ses_payload_len = 0;

/* IPv6 fetched at the start of each session for the announce */
static char ses_ipv6[48] = {0};

/* Last IPv6 successfully announced to HES — persists across sessions in RAM */
static char last_announced_ipv6[48] = {0};

/* Private function prototypes */
static void session_handle_failure(void);
static bool session_send_envelope(uint8_t msg_type, const uint8_t *payload,
                                   uint16_t payload_size);
static uint16_t session_build_read_response(const uint8_t *req_payload,
                                             uint16_t req_len,
                                             uint8_t *out, uint16_t out_cap);
static void session_dispatch_hes_msg(uint8_t msg_type, const uint8_t *payload_ptr,
                                      uint16_t payload_len);

void atcom_session_init(void) {
  session.state_timeout_timer = delay_timer_create();
  session.state_delay_timer = delay_timer_create();
  session.error_backoff_timer = delay_timer_create();
  session.current_state = COM_SES_IDLE;
}

/**
 * @brief Handle session failure: stop timers, reset UDP, exponential backoff
 */
static void session_handle_failure(void) {
  delay_stop(session.state_timeout_timer);
  delay_stop(session.state_delay_timer);

  atcom_udp_reset();

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
  session.last_seq = session.seq;
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
  session.after_send_ok = COM_SES_POLL_WAIT;
  session.failure_count = 0;
  session.needs_hard_reset = false;
  session.seq = 0;
  ses_ipv6[0] = '\0';
  session.last_msg_type = 0;
  session.last_payload_len = 0;
  session.poll_start_tick_ms = 0;
  session.can_resend = false;

  atcom_udp_reset();
}

/**
 * @brief Check if the session FSM has completed
 * @return true if session is done or in error state
 */
bool Com_is_session_done(void) {
  return (session.current_state == COM_SES_DONE ||
          session.current_state == COM_SES_ERROR);
}

bool Com_session_failed(void) {
  return session.needs_hard_reset;
}

/**
 * @brief Helper: send an envelope and transition to WAIT_SEND_OK with the
 *        given after_send_ok target.
 */
static void session_send_and_wait(uint8_t msg_type, const uint8_t *payload,
                                   uint16_t payload_len,
                                   session_state_t after) {
  if (session_send_envelope(msg_type, payload, payload_len)) {
    session.last_msg_type = msg_type;
    session.last_payload_len = payload_len;
    session.can_resend = true;
    delay_start(session.state_timeout_timer, TIMEOUT_SEND);
    session.after_send_ok = after;
    session.current_state = COM_SES_WAIT_SEND_OK;
  } else {
    session_handle_failure();
  }
}

typedef void (*msg_handler_t)(const uint8_t *, uint16_t);

static void handle_register_response(const uint8_t *p, uint16_t len) {
  (void)p; (void)len;
  session_send_and_wait(MSG_TYPE_ACK, NULL, 0, COM_SES_POLL_WAIT);
}

static void handle_handshake(const uint8_t *p, uint16_t len) {
  (void)p; (void)len;
  /* Payload: RLP list [ status(u8) ]. Phase 7 (HMAC) not validated yet,
   * so always MSG_STATUS_OK. */
  rlp_writer_t w;
  rlp_writer_init(&w, ses_payload_buf, sizeof(ses_payload_buf));
  uint16_t bm = rlp_list_begin(&w);
  rlp_encode_uint8(&w, MSG_STATUS_OK);
  rlp_list_end(&w, bm);
  ses_payload_len = rlp_writer_ok(&w) ? rlp_writer_len(&w) : 0;
  if (ses_payload_len == 0) {
    session_handle_failure();
    return;
  }
  session_send_and_wait(MSG_TYPE_HANDSHAKE_RESPONSE, ses_payload_buf,
                         ses_payload_len, COM_SES_POLL_WAIT);
}

static void handle_read_request(const uint8_t *payload_ptr, uint16_t payload_len) {
  Storage_save_pulse_count(PulseCounter_get_count());
  ses_payload_len = session_build_read_response(
      payload_ptr, payload_len, ses_payload_buf, sizeof(ses_payload_buf));
  if (ses_payload_len == 0) {
    session_handle_failure();
    return;
  }
  session_send_and_wait(MSG_TYPE_READ_RESPONSE, ses_payload_buf,
                         ses_payload_len, COM_SES_POLL_WAIT);
}

static void handle_write_request(const uint8_t *payload_ptr, uint16_t payload_len) {
  uint64_t next_wake = 0;
  bool got_wake = session_extract_next_wake(payload_ptr, payload_len, &next_wake);
  if (got_wake) {
    uint32_t now_hi, now_lo;
    RTC_get_timestamp(&now_hi, &now_lo);
    uint64_t now = ((uint64_t)now_hi << 32) | now_lo;
    if (next_wake > now + COLD_START_OFFSET_SEC) {
      uint64_t delta = next_wake - now - COLD_START_OFFSET_SEC;
      if (delta < 86400ULL) atcom_set_pending_wake_seconds((uint32_t)delta);
    }
  }
  ses_payload_len = session_build_write_response(
      payload_ptr, payload_len, got_wake, ses_payload_buf, sizeof(ses_payload_buf));
  if (ses_payload_len == 0) {
    session_handle_failure();
    return;
  }
  session_send_and_wait(MSG_TYPE_WRITE_RESPONSE, ses_payload_buf,
                         ses_payload_len, COM_SES_POLL_WAIT);
}

static void handle_ack(const uint8_t *p, uint16_t len) {
  (void)p; (void)len;
  PulseCounter_reset();
  Storage_save_pulse_count(0);
  session.failure_count = 0;
  session.current_state = COM_SES_DONE;
}

static void session_dispatch_hes_msg(uint8_t msg_type, const uint8_t *payload_ptr,
                                      uint16_t payload_len) {
  static const struct {
    uint8_t type;
    msg_handler_t handler;
  } dispatch_table[] = {
    { MSG_TYPE_REGISTER_RESPONSE, handle_register_response },
    { MSG_TYPE_HANDSHAKE,         handle_handshake         },
    { MSG_TYPE_READ_REQUEST,      handle_read_request      },
    { MSG_TYPE_WRITE_REQUEST,     handle_write_request     },
    { MSG_TYPE_ACK,               handle_ack               },
  };

  for (size_t i = 0; i < sizeof(dispatch_table) / sizeof(dispatch_table[0]); i++) {
    if (dispatch_table[i].type == msg_type) {
      dispatch_table[i].handler(payload_ptr, payload_len);
      return;
    }
  }
  session_handle_failure();
}

/**
 * @brief Main session FSM — call repeatedly from main loop.
 *
 * Flow: UDP_CTX → FETCH_IPV6 → SEND_ANNOUNCE → [SESSION_START_DEBUG →]
 *       WAIT_SEND_OK → KEEPALIVE_WAIT (poll) → READ_HES_MSG →
 *       PROCESS_HES_MSG (dispatch by msg_type) → SEND response →
 *       back to KEEPALIVE_WAIT, until HES sends ACK → DONE.
 */
void Com_session_process(void) {
  atcmd_desc_t cmd = (atcmd_desc_t)ATCMD_DESC_DEFAULT;

  /* Global timeout check */
  if (session.state_timeout_timer >= 0 &&
      session.current_state != COM_SES_IDLE &&
      session.current_state != COM_SES_POLL_WAIT &&
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
#ifdef DEBUG_SESSION_START
        session_send_and_wait(MSG_TYPE_SESSION_START_REQUEST, NULL, 0,
                               COM_SES_POLL_WAIT);
#else
        session.current_state = COM_SES_FETCH_IPV6;
#endif
      } else if (udp_result == COM_UDP_PROCESS_ERROR) {
        atcom_udp_reset();
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
      if (strcmp(ses_ipv6, last_announced_ipv6) == 0) {
        session.poll_start_tick_ms = HAL_GetTick();
        delay_start(session.state_delay_timer, TIMEOUT_WAIT_RESPONSE);
        session.current_state = COM_SES_POLL_WAIT;
      } else {
        session.current_state = COM_SES_SEND_ANNOUNCE;
      }
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
      strncpy(last_announced_ipv6, ses_ipv6, sizeof(last_announced_ipv6) - 1);
      last_announced_ipv6[sizeof(last_announced_ipv6) - 1] = '\0';
      session_send_and_wait(MSG_TYPE_REGISTER_REQUEST, ses_payload_buf,
                             ses_payload_len, COM_SES_POLL_WAIT);
    } break;

    /* ---- Generic SEND_OK wait — transitions to session.after_send_ok ---- */
    case COM_SES_WAIT_SEND_OK: {
      if (!ATCore_is_response_ready()) break;
      if (ATCore_check_response() != BG95_OK) {
        /* Unrecognized response — likely a +QIURC: "recv" URC that arrived
         * before SEND OK. Re-arm DMA and keep waiting; global TIMEOUT_SEND
         * handles the case where SEND OK never arrives. */
        ATCore_reset_rx();
        break;
      }
      if (ATCore_get_response_status() == BG95_RESP_SEND_OK) {
        delay_stop(session.state_timeout_timer);
        session.poll_start_tick_ms = HAL_GetTick();
        delay_start(session.state_delay_timer, TIMEOUT_WAIT_RESPONSE);
        session.current_state = session.after_send_ok;
      } else {
        session_handle_failure();
      }
    } break;

    /* ---- Wait for HES message, then poll ---- */
    case COM_SES_POLL_WAIT: {
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
        session.current_state = COM_SES_POLL_WAIT;
      }
    } break;

    case COM_SES_CHECK_HES_DATA_WAIT: {
      if (!ATCore_is_response_ready()) break;
      if (!ATCore_process_response()) {
        delay_start(session.state_delay_timer, GENERIC_RETRY_DELAY_MS);
        session.current_state = COM_SES_POLL_WAIT;
        break;
      }
      if (ATCore_get_first_qird_value() > 0) {
        delay_stop(session.state_timeout_timer);
        session.current_state = COM_SES_READ_HES_MSG;
      } else if (session.can_resend &&
                 (HAL_GetTick() - session.poll_start_tick_ms) >= POLL_RESEND_TIMEOUT_MS) {
        session.poll_start_tick_ms = HAL_GetTick();
        session.seq = session.last_seq;
        session_send_and_wait(session.last_msg_type,
                              session.last_payload_len > 0 ? ses_payload_buf : NULL,
                              session.last_payload_len,
                              COM_SES_POLL_WAIT);
      } else {
        delay_start(session.state_delay_timer, TIMEOUT_WAIT_RESPONSE);
        session.current_state = COM_SES_POLL_WAIT;
      }
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

      if (rx_env.msg_type == MSG_TYPE_HANDSHAKE) {
        RTC_set_datetime(rx_env.timestamp_high, rx_env.timestamp_low);
      }

      session_dispatch_hes_msg(rx_env.msg_type, payload_ptr, payload_len);
    } break;

    case COM_SES_DONE:
    case COM_SES_ERROR:
      break;

    case COM_SES_RESTART_WAIT: {
      if (delay_has_finished(session.error_backoff_timer)) {
        session.current_state = COM_SES_UDP_CTX;
        atcom_udp_reset();
      }
    } break;
  }
}
