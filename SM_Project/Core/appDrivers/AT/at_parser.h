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


void fBuild_Envelope(); //Esto de parametros necesitaría recibir: msg_type, dev_id, seq, payload(de ser necesario), MAC
void fParse_Envelope();

/* Build AT Commands Functions */
void fCmdBuild_NoParams(char *cmd, const char *cmd_string, atcmd_desc_t *atcmd_desc);


#endif //_AT_PARSER_H_