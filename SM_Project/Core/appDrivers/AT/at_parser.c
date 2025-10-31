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

/* ---------------------- Private functions declaration --------------------- */

static void _get_str_slice(const char *str, size_t size, size_t offset,
                           size_t num_bytes, char *out_str, size_t out_size);
static void _num_to_hex(char *dest, size_t size, uint32_t num,
                        uint8_t num_bytes);

/* ----------------------- Public functions definition ---------------------- */

/**
 * @brief
 *
 * @param envp
 * @return char*
 */
char *fBuild_Envelope(envelope_t envp) {
  char *aux = NULL;
  return aux;
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
  _get_str_slice(envp, envp_size, 0, 1, str_aux, envp_size);
  envp_info->version = (uint8_t)strtoul(str_aux, &endptr, 10);

  /* Obtain Msg Type */
  _get_str_slice(envp, envp_size, 1, 1, str_aux, envp_size);
  envp_info->msg_type = (uint8_t)strtoul(str_aux, &endptr, 10);

  /* Obtain Dev ID */
  _get_str_slice(envp, envp_size, 2, 16, str_aux, envp_size);
  // envp_info->device_id = ;

  /* Obtain Seq */
  _get_str_slice(envp, envp_size, 18, 4, str_aux, envp_size);
  envp_info->seq = (uint32_t)strtoul(str_aux, &endptr, 10);

  /* Obtain Timestamp */
  _get_str_slice(envp, envp_size, 22, 8, str_aux, envp_size);
  envp_info->timestamp = (uint32_t)strtoul(str_aux, &endptr, 10);

  /* Obtain MAC Tag */
  _get_str_slice(envp, envp_size, 30, 16, str_aux, envp_size);
  // envp_info->mac = ;

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
void fCmdBuild_NoParams(char *cmd, const char *cmd_string,
                        atcmd_desc_t *atcmd_desc) {
  if (cmd == NULL || cmd_string == NULL) return;

  if (atcmd_desc->id == CMD_AT) {
    snprintf(cmd, atcmd_desc->at_cmd_size, "AT\r\n");
  } else {
    snprintf(cmd, atcmd_desc->at_cmd_size, "AT%s\r\n", cmd_string);
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