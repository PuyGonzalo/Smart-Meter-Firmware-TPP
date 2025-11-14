/**
 * @file at_parser.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-10-28
 * 
 * @copyright Copyright (c) 2025
 * 
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
uint8_t *Parser_fBuild_Envelope(const envelope_t *envp, uint16_t *size);
bool fBuild_Envelope_w_payload(envelope_t *envp, char *payload, uint16_t payload_size);
bool Parser_fParse_Envelope(char *envp, uint16_t envp_size, envelope_t *envp_info);
bool fParse_Envelope_w_payload(envelope_t *envp, char *payload, uint16_t payload_size);
void Parser_build_cmd(char *out, size_t out_size,
                              const char *cmd_string, const atcmd_desc_t *desc);
uint32_t Parser_calculate_cmd_size(const char *cmd_string,
                                           const atcmd_desc_t *desc);
char *Parser_get_str_field(char *str, uint16_t field_index, uint16_t *field_len);
int16_t Parser_get_first_qird_value(const char *resp);

/* ----------------- Build AT Commands Function declaration ----------------- */
/**
* @addtogroup ATCommandBuilders
* @brief AT Commands builder functions.
* @{
*/
void fCmdBuild_NoParams(atcmd_desc_t *atcmd_desc);
void fCmdBuild_ATQIACT(atcmd_desc_t *atcmd_desc);
void fCmdBuild_ATQIDEACT(atcmd_desc_t *atcmd_desc);
void fCmdBuild_ATQIOPEN(atcmd_desc_t *atcmd_desc);
void fCmdBuild_ATQICLOSE(atcmd_desc_t *atcmd_desc);
void fCmdBuild_ATQISTATE(atcmd_desc_t *atcmd_desc);
void fCmdBuild_ATQISEND(atcmd_desc_t *atcmd_desc);
void fCmdBuild_ATQIRD(atcmd_desc_t *atcmd_desc);
/** @} */



#endif //_AT_PARSER_H_