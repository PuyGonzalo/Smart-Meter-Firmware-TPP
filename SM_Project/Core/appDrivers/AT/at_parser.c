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

static void _get_str_slice(const char *str, size_t size, size_t offset,
                           size_t num_bytes, char *out_str, size_t out_size);
static void _num_to_hex(char *dest, size_t size, uint32_t num,
                        uint8_t num_bytes);

/* ----------------------- Public functions definition ---------------------- */

// Doxygen
void fBuild_Envelope() {}

// Doxygen
void fParse_Envelope() {}

/* ----------------------- Build AT Commands Functions ----------------------*/

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

  if (atcmd_desc->id == 0) {
    snprintf(cmd, atcmd_desc->at_cmd_size, "AT\r\n");
  } else {
    snprintf(cmd, atcmd_desc->at_cmd_size, "AT%s", cmd_string);
  }
}

/* ---------------------- Private functions definition ---------------------- */

/**
 * @brief Get the str slice object
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
static void _num_to_hex(char *dest, size_t dest_size, uint32_t num,
                        uint8_t num_bytes) {
  int width = num_bytes * 2;
  snprintf(dest, dest_size, "%0*X", width, num);
}