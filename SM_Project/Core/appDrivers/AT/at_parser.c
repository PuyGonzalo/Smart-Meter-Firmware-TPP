/**
 * @file at_parser.c
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2025-10-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "at_parser.h"

#include <stdint.h>
#include <string.h>

#include "at_device.h"

static uint8_t raw_envelope[MAX_ENV_SIZE];

/* ---------------------- Private functions declaration --------------------- */

static void _get_str_slice(const char *str, size_t size, size_t offset,
                           size_t num_bytes, char *out_str, size_t out_size);

static void _num_to_hex(char *dest, size_t size, uint32_t num,
                        uint8_t num_bytes);

static int8_t _hexstr_to_bytes(const char *hexstr, uint16_t hex_len,
                               uint8_t *out, uint16_t out_len);

static void _bytes_to_hexstr(const uint8_t *src, uint16_t len, char *dst);

static char *next_token(const char **str);

static uint8_t _count_decimal_digits(uint32_t n);

/* ----------------------- Public functions definition ---------------------- */

/**
 * @brief
 *
 * @param envp_info
 * @param size
 * @return char*
 */
const uint8_t *Parser_fBuild_Envelope(const envelope_t *envp_info,
                                      uint16_t *size) {
  if (!envp_info || !size) return NULL;

  uint16_t total_len = 0;

  /* Version */
  raw_envelope[ENV_VERSION_OFFSET] = envp_info->version;
  total_len += ENV_VERSION_BYTES;

  /* Msg Type */
  raw_envelope[ENV_MSGTYPE_OFFSET] = envp_info->msg_type;
  total_len += ENV_MSGTYPE_BYTES;

  /* Device ID */
  memcpy((raw_envelope + ENV_DEVID_OFFSET), envp_info->device_id,
         ENV_DEVID_BYTES);
  total_len += ENV_DEVID_BYTES;

  /* SEQ */
  for (uint8_t i = 0; i < ENV_SEQ_BYTES; ++i) {
    raw_envelope[ENV_SEQ_OFFSET + i] = (envp_info->seq >> (8 * (3 - i))) & 0xFF;
  }
  total_len += ENV_SEQ_BYTES;

  /* Timestamp */
  uint8_t timestamp_BD2 = ENV_TIMESTAMP_BYTES >> 1;

  for (uint8_t i = 0; i < timestamp_BD2; ++i) {
    raw_envelope[ENV_TIMESTAMP_OFFSET + i] =
        (envp_info->timestamp_high >> (8 * (3 - i))) & 0xFF;
  }

  uint8_t timestamp_low_offset = ENV_TIMESTAMP_OFFSET + timestamp_BD2;

  for (uint8_t i = 0; i < timestamp_BD2; ++i) {
    raw_envelope[timestamp_low_offset + i] =
        (envp_info->timestamp_low >> (8 * (3 - i))) & 0xFF;
  }
  total_len += ENV_TIMESTAMP_BYTES;

  /* MAC */
  memcpy((raw_envelope + ENV_MAC_OFFSET), envp_info->mac, ENV_MAC_BYTES);
  total_len += ENV_MAC_BYTES;

  *size = total_len;

  return raw_envelope;
}

/**
 * @brief Build a complete message: header(30) + payload(N) + MAC(16).
 *
 * Internally calls Parser_fBuild_Envelope() to build the header and MAC,
 * then shifts the MAC right to insert the payload between them.
 *
 * @param envp          Envelope struct with header fields and MAC.
 * @param payload       Payload bytes (e.g., RLP-encoded OBIS data). May be NULL if payload_size is 0.
 * @param payload_size  Payload length in bytes.
 * @param total_size    Output: total message size (30 + payload_size + 16).
 * @retval Pointer to static internal buffer with the assembled message.
 * @retval NULL on error (invalid params or message exceeds MAX_ENV_SIZE).
 */
const uint8_t *Parser_fBuild_Envelope_w_payload(const envelope_t *envp,
                                                 const uint8_t *payload,
                                                 uint16_t payload_size,
                                                 uint16_t *total_size) {
  if (!envp || !total_size) return NULL;

  uint16_t total = ENV_MAC_OFFSET + payload_size + ENV_MAC_BYTES;
  if (total > MAX_ENV_SIZE) return NULL;

  /* Build header(30) + MAC(16) into raw_envelope using existing function */
  uint16_t base_size;
  if (!Parser_fBuild_Envelope(envp, &base_size)) return NULL;

  /* raw_envelope now has: [header(30)][MAC(16)]
   * We need:              [header(30)][payload(N)][MAC(16)]
   * Shift MAC to make room for payload */
  if (payload_size > 0) {
    memmove(raw_envelope + ENV_MAC_OFFSET + payload_size,
            raw_envelope + ENV_MAC_OFFSET, ENV_MAC_BYTES);

    if (payload) {
      memcpy(raw_envelope + ENV_MAC_OFFSET, payload, payload_size);
    }
  }

  *total_size = total;
  return raw_envelope;
}

/**
 * @brief
 *
 * @param envp
 * @retval true
 * @retval false
 */
bool Parser_fParse_Envelope(char *envp, uint16_t envp_size,
                            envelope_t *envp_info) {
  if (envp_info == NULL || envp == NULL) return false;

  /* Obtain Version */
  envp_info->version = *(envp + ENV_VERSION_OFFSET);
  // memcpy((envp + ENV_VERSION_OFFSET), &envp_info->version,
  // ENV_VERSION_BYTES);

  /* Obtain Msg Type */
  envp_info->msg_type = *(envp + ENV_MSGTYPE_OFFSET);
  // memcpy((envp + ENV_MSGTYPE_OFFSET), &envp_info->msg_type,
  // ENV_MSGTYPE_BYTES);

  /* Obtain Dev ID */
  memcpy(envp_info->device_id, (envp + ENV_DEVID_OFFSET), ENV_DEVID_BYTES);

  /* Obtain Seq */
  envp_info->seq = 0;

  for (uint8_t i = 0; i < ENV_SEQ_BYTES; i++) {
    envp_info->seq = (envp_info->seq << 8) | (uint8_t)envp[ENV_SEQ_OFFSET + i];
  }

  /* Obtain Timestamp */
  envp_info->timestamp_high = 0;
  uint8_t timestamp_BD2 = ENV_TIMESTAMP_BYTES >> 1;

  for (uint8_t i = 0; i < timestamp_BD2; i++) {
    envp_info->timestamp_high = (envp_info->timestamp_high << 8) |
                                (uint8_t)envp[ENV_TIMESTAMP_OFFSET + i];
  }

  envp_info->timestamp_low = 0;

  for (uint8_t i = 0; i < timestamp_BD2; i++) {
    envp_info->timestamp_low =
        (envp_info->timestamp_low << 8) |
        (uint8_t)envp[ENV_TIMESTAMP_OFFSET + timestamp_BD2 + i];
  }

  /* Obtain MAC Tag */
  memcpy(envp_info->mac, (envp + ENV_MAC_OFFSET), ENV_MAC_BYTES);

  return true;
}

/**
 * @brief Parse a complete message: header(30) + payload(N) + MAC(16).
 *
 * Extracts envelope fields from the header, provides a pointer into the
 * payload region, and reads the MAC from the end of the message.
 *
 * @param data             Raw message bytes received via QIRD.
 * @param data_size        Total message length.
 * @param envp_info        Output: parsed envelope fields.
 * @param payload_out      Output: pointer to payload start within data buffer.
 *                         NULL-safe: pass NULL if payload is not needed.
 * @param payload_size_out Output: payload length in bytes.
 *                         NULL-safe: pass NULL if not needed.
 * @retval true   Message parsed successfully.
 * @retval false  Message too short or invalid.
 */
bool Parser_fParse_Envelope_w_payload(const uint8_t *data, uint16_t data_size,
                                       envelope_t *envp_info,
                                       const uint8_t **payload_out,
                                       uint16_t *payload_size_out) {
  if (!data || !envp_info) return false;
  if (data_size < ENV_MAC_OFFSET + ENV_MAC_BYTES) return false;

  /* Parse header fields (version, msg_type, device_id, seq, timestamp).
   * Parser_fParse_Envelope reads MAC from offset 30, which is wrong when
   * there is a payload. We fix the MAC below. */
  if (!Parser_fParse_Envelope((char *)data, data_size, envp_info))
    return false;

  uint16_t payload_size = data_size - ENV_MAC_OFFSET - ENV_MAC_BYTES;

  /* Fix MAC: read from the actual end of the message */
  if (payload_size > 0) {
    memcpy(envp_info->mac, data + ENV_MAC_OFFSET + payload_size, ENV_MAC_BYTES);
  }
  /* If payload_size == 0, MAC is already at offset 30 (correct) */

  if (payload_out) *payload_out = data + ENV_MAC_OFFSET;
  if (payload_size_out) *payload_size_out = payload_size;

  return true;
}

/**
 * @brief
 *
 * @param out
 * @param out_size
 * @param cmd_string
 * @param desc
 */
void Parser_build_cmd(char *out, size_t out_size, const char *cmd_string,
                      const atcmd_desc_t *desc) {
  if (!out || !cmd_string || !desc) return;

  size_t written = 0;
  written += snprintf(out + written, out_size - written, "AT%s", cmd_string);

  if (desc->total_params > 0)
    written += snprintf(out + written, out_size - written, "=");

  const char *sptr = desc->str_params;

  uint8_t num_param_count = 0;
  uint8_t str_param_count = 0;

  for (uint8_t i = 0; i < desc->total_params; i++) {
    if (desc->param_types[i] == AT_PARAM_NUM) {
      if (num_param_count < desc->nb_num_params) {
        written += snprintf(out + written, out_size - written, "%lu",
                            desc->num_params[num_param_count]);
        num_param_count++;
      }
    } else if (desc->param_types[i] == AT_PARAM_STR) {
      if (str_param_count < desc->nb_str_params) {
        const char *token = next_token(&sptr);
        if (token)
          written +=
              snprintf(out + written, out_size - written, "\"%s\"", token);

        str_param_count++;
      }
    }

    if (i < desc->total_params - 1)
      written += snprintf(out + written, out_size - written, ",");
  }

  snprintf(out + written, out_size - written, "\r\n");
}

/**
 * @brief
 *
 * @param cmd_string
 * @param desc
 * @return uint32_t
 */
uint32_t Parser_calculate_cmd_size(const char *cmd_string,
                                   const atcmd_desc_t *desc) {
  if (!cmd_string || !desc) return 0;

  uint32_t size = 0;

  /* "AT" + command name */
  size += 2 + strlen(cmd_string);

  bool has_params = (desc->nb_num_params > 0) || (desc->nb_str_params > 0);
  if (has_params) size += 1; /* '=' */

  /* Int params: Each number converted to string + coma */
  for (uint8_t i = 0; i < desc->nb_num_params; i++) {
    size += _count_decimal_digits(desc->num_params[i]);
    if ((i < desc->nb_num_params - 1) || (desc->nb_str_params > 0))
      size += 1; /* comma only if not last or if there are string params */
  }

  /* Strings params: str_params with delimiter '%' */
  if (desc->nb_str_params > 0 && strlen(desc->str_params) > 0) {
    const char *p = desc->str_params;
    while (*p) {
      if (*p == '%')
        size += 3; /* Quotes and comma */
      else
        size++;
      p++;
    }
    size += 2; /* quotes around last param */
  }

  size += 2; /* "\r\n" */
  size += 1; /* '\0' */

  return size;
}

/**
 * @brief Get pointer and length of field N from a str with '\r\n' delimitter.
 *
 * @param str Input string (ended with '\0').
 * @param field_index Index of wanted field (0 = first field).
 * @param field_len Pointer to uint16_t variable where field length is stored.
 * @retval Pointer to beginning of str field.
 * @retval NULL if field doesn't exist.
 *
 * @code
 * char str[] = "+CSQ: 15,99\r\n+CREG: 0,1\r\n+CGATT: 1"
 * uint16_t field_index = 1;
 *
 * char *example_ptr;
 * uint16_t wanted_field_size;
 *
 * // This will get:
 * // Pointer to "+CREG: 0,1" beginning.
 * // wanted_field_size = 10
 * example_ptr = get_str_field(str, field_index, &wanted_field_size);
 * @endcode
 *
 */
char *Parser_get_str_field(char *str, uint16_t field_index,
                           uint16_t *field_len) {
  if (str == NULL || field_len == NULL) return NULL;

  char *p = str;
  uint16_t current = 0;

  while (current < field_index) {
    char *next = strstr(p, "\r\n");
    if (!next) return NULL;
    p = next + 2;
    current++;
  }

  const char *end = strstr(p, "\r\n");
  if (!end) end = p + strlen(p);

  *field_len = (uint16_t)(end - p);
  return p;
}

/**
 * @brief
 *
 * @param resp
 * @return int16_t
 */
void Parser_bytes_to_hex(const uint8_t *data, uint16_t len, char *hex_out) {
  _bytes_to_hexstr(data, len, hex_out);
}

int16_t Parser_get_first_qird_value(const char *resp) {
  const char *p = strstr(resp, "+QIRD:");
  if (!p) return -1;  // not found

  p += 6;                               // skip "+QIRD:"
  while (*p == ' ' || *p == '\t') p++;  // skip spaces if any

  // strtol converts until a non-digit character (e.g. ',')
  char *end;
  long value = strtol(p, &end, 10);

  // check that something valid was parsed
  if (end == p) return -1;  // no digits found

  return (int)value;
}

/* ------------------ Build AT Commands Function definition ----------------- */

/**
 * @brief
 *
 * @param cmd
 * @param cmd_string
 * @param atcmd_desc
 */
void fCmdBuild_NoParams(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->id == CMD_AT) {
    // snprintf(cmd, atcmd_desc->at_cmd_size, "AT\r\n");
  } else {
    // snprintf(cmd, atcmd_desc->at_cmd_size, "AT%s\r\n", cmd_string);
  }
}

/**
 * @brief
 *
 * @param cmd
 * @param cmd_string
 * @param atcmd_desc
 */
void fCmdBuild_ATQICSGP(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->id == CMD_AT) {
    // snprintf(cmd, atcmd_desc->at_cmd_size, "AT\r\n");
  } else {
    // snprintf(cmd, atcmd_desc->at_cmd_size, "AT%s\r\n", cmd_string);
  }
}

/**
 * @brief
 *
 * @param cmd
 * @param cmd_string
 * @param atcmd_desc
 */
void fCmdBuild_ATQIACT(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->cmd_mode == AT_CMD_TEST) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 6; /* 2 "AT" + 2 "=?" + 2 "\r\n" */
    atcmd_desc->nb_num_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_READ) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 5; /* 2 "AT" + 1 "?" + 2 "\r\n" */
    atcmd_desc->nb_num_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_num_params = 1;
    atcmd_desc->num_params[0] = BG95_CONTEXT_ID;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->total_params = 1;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_num_params = 1;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->total_params = 1;
  }
}

/**
 * @brief
 *
 * @param cmd
 * @param cmd_string
 * @param atcmd_desc
 */
void fCmdBuild_ATQIDEACT(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->cmd_mode == AT_CMD_TEST) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 6; /* 2 "AT" + 2 "=?" + 2 "\r\n" */
    atcmd_desc->nb_num_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_num_params = 1;
    atcmd_desc->num_params[0] = BG95_CONTEXT_ID;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->total_params = 1;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_num_params = 1;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->total_params = 1;
  }
}

/**
 * @brief
 *
 * @param cmd
 * @param cmd_string
 * @param atcmd_desc
 */
void fCmdBuild_ATQIOPEN(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->cmd_mode == AT_CMD_TEST) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 6; /* 2 "AT" + 2 "=?" + 2 "\r\n" */
    atcmd_desc->nb_num_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_num_params = 4;
    atcmd_desc->num_params[0] = BG95_CONTEXT_ID;
    atcmd_desc->num_params[1] = BG95_CONNECT_ID;
    atcmd_desc->num_params[2] = BG95_BACKDOOR_PORT;
    atcmd_desc->num_params[3] = BG95_ACCESS_MODE;
    atcmd_desc->nb_str_params = 2;
    strncpy(atcmd_desc->str_params, BG95_QIOPEN_STR_PARAMS,
            strlen(BG95_QIOPEN_STR_PARAMS));
    atcmd_desc->str_params[strlen(BG95_QIOPEN_STR_PARAMS)] = '\0';
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->param_types[2] = AT_PARAM_STR;
    atcmd_desc->param_types[3] = AT_PARAM_STR;
    atcmd_desc->param_types[4] = AT_PARAM_NUM;
    atcmd_desc->param_types[5] = AT_PARAM_NUM;
    atcmd_desc->total_params = 6;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_num_params = 5;
    atcmd_desc->nb_str_params = 2;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->param_types[2] = AT_PARAM_STR;
    atcmd_desc->param_types[3] = AT_PARAM_STR;
    atcmd_desc->param_types[4] = AT_PARAM_NUM;
    atcmd_desc->param_types[5] = AT_PARAM_NUM;
    atcmd_desc->param_types[6] = AT_PARAM_NUM;
    atcmd_desc->total_params = 7;
  }
}

/**
 * @brief
 *
 * @param cmd
 * @param cmd_string
 * @param atcmd_desc
 */
void fCmdBuild_ATQICLOSE(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->cmd_mode == AT_CMD_TEST) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 6; /* 2 "AT" + 2 "=?" + 2 "\r\n" */
    atcmd_desc->nb_num_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_num_params = 2;
    atcmd_desc->num_params[0] = BG95_CONNECT_ID;
    atcmd_desc->num_params[1] = BG95_QICLOSE_DFLT_TIMEOUT;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->total_params = 2;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_num_params = 2;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->total_params = 2;
  }
}

/**
 * @brief
 *
 * @param cmd
 * @param cmd_string
 * @param atcmd_desc
 */
void fCmdBuild_ATQISTATE(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->cmd_mode == AT_CMD_TEST) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 6; /* 2 "AT" + 2 "=?" + 2 "\r\n" */
    atcmd_desc->nb_num_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_READ ||
             atcmd_desc->cmd_mode == AT_CMD_EXEC) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 5; /* 2 "AT" + 1 "?" + 2 "\r\n" */
    atcmd_desc->nb_num_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_num_params = 2;
    atcmd_desc->num_params[0] = BG95_QISTATE_DFLT_QUERY;
    atcmd_desc->num_params[1] = BG95_CONTEXT_ID;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->total_params = 2;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_num_params = 2;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->total_params = 2;
  }
}

/**
 * @brief
 *
 * @param cmd
 * @param cmd_string
 * @param atcmd_desc
 */
void fCmdBuild_ATQISEND(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->cmd_mode == AT_CMD_TEST) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 6; /* 2 "AT" + 2 "=?" + 2 "\r\n" */
    atcmd_desc->nb_num_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_num_params = 2;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->total_params = 2;
  }
}

/**
 * @brief
 *
 * @param cmd
 * @param cmd_string
 * @param atcmd_desc
 */
void fCmdBuild_ATQISENDEX(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_num_params = 1;
    atcmd_desc->nb_str_params = 1;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_STR;
    atcmd_desc->total_params = 2;
  }
}

void fCmdBuild_ATQIRD(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->cmd_mode == AT_CMD_TEST) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 6; /* 2 "AT" + 2 "=?" + 2 "\r\n" */
    atcmd_desc->nb_num_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_OPT) {
    atcmd_desc->nb_num_params = 2;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->total_params = 2;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_num_params = 1;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->total_params = 1;
  }
}

/* ---------------------- Private functions definition ---------------------- */

/**
 * @brief Get the str slice
 *
 * @param str Original string.
 * @param size Size of original string (str).
 * @param offset Offset in bytes.
 * @param num_bytes Number of bytes to get from offset.
 * @param out_str Output string.
 * @param out_size Size of output string.
 */
static void _get_str_slice(const char *str, size_t size, size_t offset,
                           size_t num_bytes, char *out_str, size_t out_size) {
  if (!str || !out_str) return;

  size_t start = offset * 2;
  size_t length = num_bytes * 2;

  if (start + length > size) {
    out_str[0] = '\0';
    return;
  }

  if (out_size < length + 1) {
    out_str[0] = '\0';
    return;
  }

  memcpy(out_str, str + start, length);
  out_str[length] = '\0';
  // Afuera de esta funcion, si necesito obtener un numero de este pedaso,
  // usar strtoul().
}

/**
 * @brief
 *
 * @param dest
 * @param size
 * @param num
 * @param num_bytes
 */
static void __attribute__((unused)) _num_to_hex(char *dest, size_t dest_size,
                                                uint32_t num,
                                                uint8_t num_bytes) {
  int width = num_bytes * 2;
  snprintf(dest, dest_size, "%0*lX", width, num);
}

/**
 * @brief Converts a hex string into an array of bytes.
 *
 * @param hexstr Input string (Example: "A1B2C3D4...")
 * @param hex_len Length of input string.
 * @param out Output array where bytes are going to be stored.
 * @param out_len Length of output array.
 * @retval int Amount of bytes converted.
 * @retval -1 in case of error.
 */
static int8_t _hexstr_to_bytes(const char *hexstr, uint16_t hex_len,
                               uint8_t *out, uint16_t out_len) {
  if (!hexstr || !out) return -1;

  if (hex_len % 2 != 0) return -1;

  uint16_t bytes_len = hex_len / 2;
  if (bytes_len > out_len) return -1;

  for (uint16_t i = 0; i < bytes_len; i++) {
    char c1 = toupper((unsigned char)hexstr[2 * i]);
    char c2 = toupper((unsigned char)hexstr[(2 * i) + 1]);

    uint8_t nibble1, nibble2;

    if (c1 >= '0' && c1 <= '9')
      nibble1 = c1 - '0';
    else if (c1 >= 'A' && c1 <= 'F')
      nibble1 = c1 - 'A' + 10;
    else
      return -1;

    if (c2 >= '0' && c2 <= '9')
      nibble2 = c2 - '0';
    else if (c2 >= 'A' && c2 <= 'F')
      nibble2 = c2 - 'A' + 10;
    else
      return -1;

    out[i] = (nibble1 << 4) | nibble2;
  }

  return bytes_len;
}

/**
 * @brief
 *
 * @param src
 * @param len
 * @param dst
 */
static void _bytes_to_hexstr(const uint8_t *src, uint16_t len, char *dst) {
  static const char hex[] = "0123456789ABCDEF";
  for (uint16_t i = 0; i < len; i++) {
    dst[i * 2] = hex[(src[i] >> 4) & 0xF];
    dst[i * 2 + 1] = hex[src[i] & 0xF];
  }
}

/**
 * @brief
 *
 * @param str
 * @return char*
 */
static char *next_token(const char **str) {
  static char token[ATCMD_MAX_STRPARAM_SIZE];
  if (!*str || **str == '\0') return NULL;

  const char *start = *str;
  char *out = token;

  while (*start && *start != '%') *out++ = *start++;
  *out = '\0';
  *str = (*start == '%') ? start + 1 : start;

  return token;
}

static uint8_t _count_decimal_digits(uint32_t n) {
  if (n < 10) return 1;
  if (n < 100) return 2;
  if (n < 1000) return 3;
  if (n < 10000) return 4;
  if (n < 100000) return 5;
  if (n < 1000000) return 6;
  if (n < 10000000) return 7;
  if (n < 100000000) return 8;
  if (n < 1000000000) return 9;

  return 10;
}