/**
 * @file at_parser.h
 * @ingroup atcore
 * @brief Envelope build/parse, AT command-string building and response
 *        field helpers.
 *
 * Handles both directions of the wire format: serializing/deserializing the
 * 46-byte envelope (optionally with an RLP payload) and formatting AT command
 * strings from an ::atcmd_desc_t. See at_parser.c for the per-function
 * documentation.
 *
 * @version 0.2
 * @date 2025-10-28
 */

#ifndef _AT_PARSER_H_
#define _AT_PARSER_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "at_device.h"
#include "at_net.h"

/**
 * @addtogroup atcore
 * @{
 */

/* Envelope field offsets and sizes (bytes). */
#define ENV_VERSION_OFFSET 0
#define ENV_VERSION_BYTES 1
#define ENV_MSGTYPE_OFFSET 1
#define ENV_MSGTYPE_BYTES 1
#define ENV_DEVID_OFFSET 2
#define ENV_DEVID_BYTES (DEV_ID_BYTES)
#define ENV_SEQ_OFFSET 18
#define ENV_SEQ_BYTES 4
#define ENV_TIMESTAMP_OFFSET 22
#define ENV_TIMESTAMP_BYTES 8
#define ENV_MAC_OFFSET 30
#define ENV_MAC_BYTES (MAC_BYTES)
#define MAX_ENV_SIZE 128

/* ---------------------- Public functions declaration ---------------------- */
const uint8_t *Parser_fBuild_Envelope(const envelope_t *envp, uint16_t *size);
const uint8_t *Parser_fBuild_Envelope_w_payload(const envelope_t *envp,
                                                 const uint8_t *payload,
                                                 uint16_t payload_size,
                                                 uint16_t *total_size);
bool Parser_fParse_Envelope(char *envp, uint16_t envp_size, envelope_t *envp_info);
bool Parser_fParse_Envelope_w_payload(const uint8_t *data, uint16_t data_size,
                                       envelope_t *envp_info,
                                       const uint8_t **payload_out,
                                       uint16_t *payload_size_out);
void Parser_build_cmd(char *out, size_t out_size,
                              const char *cmd_string, const atcmd_desc_t *desc);
uint32_t Parser_calculate_cmd_size(const char *cmd_string,
                                           const atcmd_desc_t *desc);
char *Parser_get_str_field(char *str, uint16_t field_index, uint16_t *field_len);
int16_t Parser_get_first_qird_value(const char *resp);
void Parser_bytes_to_hex(const uint8_t *data, uint16_t len, char *hex_out);

/* ----------------- Build AT Commands Function declaration ----------------- */
/**
 * @defgroup atcmd_builders AT command builders
 * @ingroup atcore
 * @brief Per-command functions that fill an ::atcmd_desc_t before it is sent.
 *        Each builder formats the parameters specific to its AT command.
 * @{
 */
void fCmdBuild_NoParams(atcmd_desc_t *atcmd_desc);   /**< Commands with no parameters. */
void fCmdBuild_ATQIACT(atcmd_desc_t *atcmd_desc);    /**< AT+QIACT (activate PDP). */
void fCmdBuild_ATQIDEACT(atcmd_desc_t *atcmd_desc);  /**< AT+QIDEACT (deactivate PDP). */
void fCmdBuild_ATQIOPEN(atcmd_desc_t *atcmd_desc);   /**< AT+QIOPEN (open socket). */
void fCmdBuild_ATQICLOSE(atcmd_desc_t *atcmd_desc);  /**< AT+QICLOSE (close socket). */
void fCmdBuild_ATQISTATE(atcmd_desc_t *atcmd_desc);  /**< AT+QISTATE (socket state). */
void fCmdBuild_ATQISEND(atcmd_desc_t *atcmd_desc);   /**< AT+QISEND (send data). */
void fCmdBuild_ATQISENDEX(atcmd_desc_t *atcmd_desc); /**< AT+QISENDEX (send hex data). */
void fCmdBuild_ATQIRD(atcmd_desc_t *atcmd_desc);     /**< AT+QIRD (read received data). */
/** @} */

/** @} */

#endif //_AT_PARSER_H_
