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

#include "at_device.h"


/* ---------------------- Public functions declaration ---------------------- */
char *fBuild_Envelope(envelope_t envp);
bool fBuild_Envelope_w_payload(envelope_t *envp, char *payload, uint16_t payload_size);
bool fParse_Envelope(char *envp, uint16_t envp_size, envelope_t *envp_info);
bool fParse_Envelope_w_payload(envelope_t *envp, char *payload, uint16_t payload_size);
char *get_str_field(char *str, uint16_t field_index, uint16_t *field_len);

/* ----------------- Build AT Commands Function declaration ----------------- */
void fCmdBuild_NoParams(char *cmd, const char *cmd_string, atcmd_desc_t *atcmd_desc);


#endif //_AT_PARSER_H_