/**
 * @file ATCom.h
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief 
 * @version 0.1
 * @date 2025-10-26
 * 
 * @copyright Copyright (c) 2025
 * 
*/

#ifndef _ATCOM_H_
#define _ATCOM_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "at_core.h"

typedef enum {
  COM_REG_INIT,
  COM_REG_UDP_CTX,
  COM_REG_SEND,
  COM_REG_VERIFY_SEND,
  COM_REG_WAIT,
  COM_REG_PROCESS_RESP,
  COM_REG_ACK,
  COM_REG_FINISHED
} com_register_st;
 
void Com_network_connection_process();
int Com_UDP_context_process();
void Com_register_device_process();
void Com_process();



#endif //_ATCOM_H_