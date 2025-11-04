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

char envelope[MAX_ENV_SIZE];

/* ---------------------- Private functions declaration --------------------- */

static void _get_str_slice(const char *str, size_t size, size_t offset,
                           size_t num_bytes, char *out_str, size_t out_size);

static void _num_to_hex(char *dest, size_t size, uint32_t num,
                        uint8_t num_bytes);

static int8_t _hexstr_to_bytes(const char *hexstr, uint16_t hex_len,
                               uint8_t *out, uint16_t out_len);

static void _bytes_to_hexstr(const uint8_t *src, uint16_t len, char *dst);

static char *next_token(const char **str);

/* ----------------------- Public functions definition ---------------------- */

/**
 * @brief
 *
 * @param envp
 * @param size
 * @return char*
 */
char *fBuild_Envelope(const envelope_t *envp, uint16_t *size) {
  if (!envp || !size) return NULL;

  char *p = envelope;
  uint16_t total_len = 0;

  /* Version */
  total_len += sprintf(p + total_len, "%02X", envp->version);

  /* Msg Type */
  total_len += sprintf(p + total_len, "%02X", envp->msg_type);

  /* Device ID */
  _bytes_to_hexstr(envp->device_id, sizeof(envp->device_id), p + total_len);
  total_len += sizeof(envp->device_id) * 2;

  /* SEQ */
  total_len += sprintf(p + total_len, "%08lX", envp->seq);

  /* Timestamp */
  total_len += sprintf(p + total_len, "%08lX", envp->timestamp);

  /* MAC */
  _bytes_to_hexstr(envp->mac, sizeof(envp->mac), p + total_len);
  total_len += sizeof(envp->mac) * 2;

  envelope[total_len] = '\0';
  *size = total_len;

  return envelope;
}

/**
 * @brief
 *
 * @param envp
 * @param payload
 * @param payload_size
 * @return true
 * @return false
 */
bool fBuild_Envelope_w_payload(envelope_t *envp, char *payload,
                               uint16_t payload_size) {
  return true;
}

/**
 * @brief
 *
 * @param envp
 * @retval true
 * @retval false
 */
bool fParse_Envelope(char *envp, uint16_t envp_size, envelope_t *envp_info) {
  if (envp_info == NULL || envp == NULL) return false;

  char str_aux[envp_size];
  char *endptr;

  /* Obtain Version */
  _get_str_slice(envp, envp_size, ENV_VERSION_OFFSET, ENV_VERSION_BYTES,
                 str_aux, envp_size);
  envp_info->version = (uint8_t)strtoul(str_aux, &endptr, 10);

  /* Obtain Msg Type */
  _get_str_slice(envp, envp_size, ENV_MSGTYPE_OFFSET, ENV_MSGTYPE_BYTES,
                 str_aux, envp_size);
  envp_info->msg_type = (uint8_t)strtoul(str_aux, &endptr, 10);

  /* Obtain Dev ID */
  _get_str_slice(envp, envp_size, ENV_DEVID_OFFSET, ENV_DEVID_BYTES, str_aux,
                 envp_size);
  _hexstr_to_bytes(str_aux, (ENV_DEVID_BYTES * 2), envp_info->device_id,
                   ENV_DEVID_BYTES);

  /* Obtain Seq */
  _get_str_slice(envp, envp_size, ENV_DEVID_OFFSET, ENV_SEQ_BYTES, str_aux,
                 envp_size);
  envp_info->seq = (uint32_t)strtoul(str_aux, &endptr, 10);

  /* Obtain Timestamp */
  _get_str_slice(envp, envp_size, ENV_TIMESTAMP_OFFSET, ENV_TIMESTAMP_BYTES,
                 str_aux, envp_size);
  envp_info->timestamp = (uint32_t)strtoul(str_aux, &endptr, 10);

  /* Obtain MAC Tag */
  _get_str_slice(envp, envp_size, ENV_MAC_OFFSET, ENV_MAC_BYTES, str_aux,
                 envp_size);
  _hexstr_to_bytes(str_aux, (ENV_MAC_BYTES * 2), envp_info->mac, ENV_MAC_BYTES);

  return true;
}

/**
 * @brief
 *
 * @param envp
 * @param payload
 * @param payload_size
 * @return true
 * @return false
 */
bool fParse_Envelope_w_payload(envelope_t *envp, char *payload,
                               uint16_t payload_size) {
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

  if (desc->nb_params > 0)
    written += snprintf(out + written, out_size - written, "=");

  const char *sptr = desc->str_params;

  for (uint8_t i = 0; i < desc->nb_params; i++) {
    if (desc->param_types[i] == AT_PARAM_NUM) {
      written += snprintf(out + written, out_size - written, "%lu",
                          desc->num_params[i]);
    } else if (desc->param_types[i] == AT_PARAM_STR) {
      const char *token = next_token(&sptr);
      if (token)
        written += snprintf(out + written, out_size - written, "\"%s\"", token);
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

  bool has_params = (desc->nb_params > 0) || (desc->nb_str_params > 0);
  if (has_params) size += 1; /* '=' */

  /* Int params: Each number converted to string + coma */
  for (uint8_t i = 0; i < desc->nb_params; i++)
    size += 10 + 1; /* up to 10 digits per number + ',' */

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
char *get_str_field(char *str, uint16_t field_index, uint16_t *field_len) {
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
    atcmd_desc->nb_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_READ) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 5; /* 2 "AT" + 1 "?" + 2 "\r\n" */
    atcmd_desc->nb_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_params = 1;
    atcmd_desc->num_params[0] = BG95_CONTEXT_ID;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->total_params = 1;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_params = 1;
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
    atcmd_desc->nb_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_params = 1;
    atcmd_desc->num_params[0] = BG95_CONTEXT_ID;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->total_params = 1;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_params = 1;
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
    atcmd_desc->nb_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_params = 4;
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
    atcmd_desc->nb_params = 5;
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
    atcmd_desc->nb_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_params = 2;
    atcmd_desc->num_params[0] = BG95_CONNECT_ID;
    atcmd_desc->num_params[1] = BG95_QICLOSE_DFLT_TIMEOUT;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->total_params = 2;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_params = 2;
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
    atcmd_desc->nb_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_READ ||
             atcmd_desc->cmd_mode == AT_CMD_EXEC) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 5; /* 2 "AT" + 1 "?" + 2 "\r\n" */
    atcmd_desc->nb_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_DEFAULT) {
    atcmd_desc->nb_params = 2;
    atcmd_desc->num_params[0] = BG95_QISTATE_DFLT_QUERY;
    atcmd_desc->num_params[1] = BG95_CONTEXT_ID;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->total_params = 2;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_params = 2;
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

  if (atcmd_desc->cmd_mode == AT_CMD_TEST) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 6; /* 2 "AT" + 2 "=?" + 2 "\r\n" */
    atcmd_desc->nb_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_params = 1;
    atcmd_desc->nb_str_params = 1;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_STR;
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
void fCmdBuild_ATQIRD(atcmd_desc_t *atcmd_desc) {
  if (atcmd_desc == NULL) return;

  if (atcmd_desc->cmd_mode == AT_CMD_TEST) {
    atcmd_desc->at_cmd_size =
        atcmd_desc->at_cmd_size + 6; /* 2 "AT" + 2 "=?" + 2 "\r\n" */
    atcmd_desc->nb_params = 0;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->total_params = 0;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE_OPT) {
    atcmd_desc->nb_params = 2;
    atcmd_desc->nb_str_params = 0;
    atcmd_desc->param_types[0] = AT_PARAM_NUM;
    atcmd_desc->param_types[1] = AT_PARAM_NUM;
    atcmd_desc->total_params = 2;
  } else if (atcmd_desc->cmd_mode == AT_CMD_WRITE) {
    atcmd_desc->nb_params = 1;
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
  static char token[64];  // max size per string param
  if (!*str || **str == '\0') return NULL;

  const char *start = *str;
  char *out = token;

  while (*start && *start != '%') *out++ = *start++;
  *out = '\0';
  *str = (*start == '%') ? start + 1 : start;

  return token;
}
