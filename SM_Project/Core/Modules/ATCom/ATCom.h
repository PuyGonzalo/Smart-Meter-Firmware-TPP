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
  COM_REG_VERIFY_DATA_RDY,
  COM_REG_WAIT_SEND,
  COM_REG_SEND_RAW,
  COM_REG_WAIT_DATA_RDY,
  COM_REG_DATA_REQUEST,
  COM_REG_DATA_REQUEST_WAIT,
  COM_REG_PROCESS_DATA,
  COM_REG_ACK,
  COM_REG_WAIT_SEND_ACK_RAW,
  COM_REG_WAIT_ACK_SEND,
  COM_REG_FINISHED
} com_register_st;

typedef enum {
  COM_UDP_IDLE = 0,
  COM_UDP_PROCESS_DONE = 1,
  // COM_UDP_QIACT_SEND,
  COM_UDP_QIACT_WAIT,
  COM_UDP_QIOPEN_SEND,
  COM_UDP_QIOPEN_WAIT,
  COM_UDP_PROCESS_ERROR,
} com_udp_st;

typedef enum {
  EV_UDP_CMD_OK,
  EV_UDP_CMD_ERROR,
  EV_UDP_TIMEOUT,
  EV_UDP_URC_QIOPEN_OK,
  EV_UDP_URC_QIOPEN_FAIL,
  EV_UDP_START_UDP_SETUP,
} com_udp_ev_t;

typedef struct {
    com_udp_st state;
    com_udp_ev_t event;
    // uint32_t timeout_ms;
    uint8_t retries;
} udp_fsm_t;
 
void Com_network_connection_process();
int Com_UDP_context_process();
void Com_register_device_process();
void Com_process();



#endif //_ATCOM_H_