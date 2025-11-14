/**
 * @file ATCom.c
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief
 * @version 0.1
 * @date 2025-10-26
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "ATCom.h"

#include <string.h>

#include "at_core.h"
#include "at_device.h"
#include "at_net.h"
#include "at_parser.h"
#include "bg95_at_cmd_lib.h"
#include "delay.h"
#include "stm32l0xx_hal.h"
#include "stm32l0xx_hal_def.h"

static com_register_st register_status = COM_REG_INIT;
static udp_fsm_t udp = {.state = COM_UDP_IDLE, .retries = 10};

static bool upd_pdp_context_ready = false;
envelope_t envp = {.version = 1, .device_id = {0}, .mac = {0}};
static uint16_t envp_size;
bool is_device_connected = true;
extern int32_t at_command_delay;

void Com_register_device_process() {
  atcmd_desc_t cmd = (atcmd_desc_t)ATCMD_DESC_DEFAULT;

  switch (register_status) {
    case COM_REG_INIT: {
      if (is_device_connected) {
        register_status = COM_REG_UDP_CTX;
      }
      break;
    }

    case COM_REG_UDP_CTX: {
      if (Com_UDP_context_process() == COM_UDP_PROCESS_DONE) {
        delay_start(at_command_delay, 10000);
        register_status = COM_REG_SEND;
      } else {
        register_status = COM_REG_UDP_CTX;
      }
      break;
    }

    case COM_REG_SEND: {
      if (delay_has_finished(at_command_delay)) {
        cmd.id = CMD_AT_QISEND;
        cmd.cmd_mode = AT_CMD_WRITE;
        cmd.num_params[0] = BG95_CONNECT_ID;
        //! TODO: Hacer funcion (en parser o acá) para que quede mas legible.
        envp.version = 1;
        envp.msg_type = 2;
        envp.seq = 0;
        envp.timestamp_high = 0;
        envp.timestamp_low = 0;

        uint16_t envp_size;
        Parser_fBuild_Envelope(&envp, &envp_size);
        cmd.num_params[1] = envp_size;

        ATCore_set_data_mode();
        ATCore_send_cmd(&cmd);
        register_status = COM_REG_SEND_RAW;
      } else {
        register_status = COM_REG_SEND;
      }

      break;
    }

    case COM_REG_SEND_RAW: {
      if (ATCore_is_send_ready()) {
        //! TODO: Poner el envelope acá y trabajarlo en
        //! la funcion de build.
        //! Asi me evito llamar dos veces a la funcion.
        uint8_t *aux;
        aux = Parser_fBuild_Envelope(&envp, &envp_size);
        ATCore_send_data(aux, envp_size);
        register_status = COM_REG_WAIT_SEND;
      }
      break;
    }

    case COM_REG_VERIFY_SEND: {
      break;
    }

    case COM_REG_VERIFY_DATA_RDY: {
      if (delay_has_finished(at_command_delay)) {
        cmd.id = CMD_AT_QIRD;
        cmd.cmd_mode = AT_CMD_WRITE_OPT;
        cmd.num_params[0] = BG95_CONNECT_ID;
        cmd.num_params[1] = 0;
        if (ATCore_send_cmd(&cmd)) {
          register_status = COM_REG_WAIT_DATA_RDY;
        } else {
          delay_start(at_command_delay, 10000);
          register_status = COM_REG_VERIFY_DATA_RDY;
        }
      }

      break;
    }

    case COM_REG_WAIT_DATA_RDY: {
      if (ATCore_is_response_ready()) {
        if (ATCore_process_response()) {
          if (ATCore_get_first_qird_value() != 0) {
            register_status = COM_REG_DATA_REQUEST;
          } else {
            delay_start(at_command_delay, 10000);
            register_status = COM_REG_VERIFY_DATA_RDY;
          }
        } else {
          delay_start(at_command_delay, 10000);
          register_status = COM_REG_VERIFY_DATA_RDY;
        }
      } else {
        register_status = COM_REG_WAIT_DATA_RDY;
      }
      break;
    }

    case COM_REG_WAIT_SEND: {
      if (ATCore_is_response_ready()) {
        ATCore_check_response();
        if (ATCore_get_response_status() == BG95_RESP_SEND_OK) {
          delay_start(at_command_delay, 10000);  /// TODO: adsf
          register_status = COM_REG_VERIFY_DATA_RDY;
        } else {
          register_status = COM_REG_SEND;
        }
      } else {
        register_status = COM_REG_WAIT_SEND;
      }

      break;
    }

    case COM_REG_DATA_REQUEST: {
      cmd.id = CMD_AT_QIRD;
      cmd.cmd_mode = AT_CMD_WRITE;
      cmd.num_params[0] = BG95_CONNECT_ID;
      ATCore_send_cmd(&cmd);
      register_status = COM_REG_DATA_REQUEST_WAIT;
      break;
    }

    case COM_REG_DATA_REQUEST_WAIT: {
      if (ATCore_is_response_ready()) {
        ATCore_set_data_mode();
        if (ATCore_process_response()) {
          register_status = COM_REG_PROCESS_DATA;
        } else {
          register_status = COM_REG_DATA_REQUEST;
        }
      } else {
        register_status = COM_REG_DATA_REQUEST_WAIT;
      }
      break;
    }

    case COM_REG_PROCESS_DATA: {
      uint16_t response_size;
      envelope_t envelop;
      char response_cpy[BG95_RX_BUFFER_SIZE];
      ATCore_get_last_response(response_cpy, BG95_RX_BUFFER_SIZE,
                               &response_size);
      Parser_fParse_Envelope(response_cpy, response_size, &envelop);
      if (envelop.msg_type == 3) {
        uint8_t dev_id[DEV_ID_BYTES];
        uint8_t mac[MAC_BYTES];
        memcpy(dev_id, envelop.device_id, DEV_ID_BYTES);
        memcpy(mac, envelop.mac, MAC_BYTES);
        ATCore_set_device_id(dev_id);
        ATCore_set_device_mac(mac);
        envp.seq = envelop.seq + 1;
      }
      delay_start(at_command_delay, 10000);
      register_status = COM_REG_ACK;
      break;
    }

    case COM_REG_ACK: {
      if (delay_has_finished(at_command_delay)) {
        //! TODO: Hacer funcion (en parser) para que quede mas legible.
        // envp.mac = ATCore_get_device_mac;
        uint8_t dev_id[DEV_ID_BYTES];
        uint8_t mac[MAC_BYTES];
        ATCore_get_device_id(dev_id);
        ATCore_get_device_mac(mac);
        memcpy(envp.device_id, dev_id, DEV_ID_BYTES);
        memcpy(envp.mac, mac, MAC_BYTES);
        envp.version = 1;
        envp.msg_type = 255;
        envp.timestamp_high = 0;
        envp.timestamp_low = 0;

        cmd.id = CMD_AT_QISEND;
        cmd.cmd_mode = AT_CMD_WRITE;
        cmd.num_params[0] = BG95_CONNECT_ID;
        register_status = COM_REG_SEND;

        uint16_t envp_size;
        Parser_fBuild_Envelope(&envp, &envp_size);
        cmd.num_params[1] = envp_size;
        ATCore_set_data_mode();
        ATCore_send_cmd(&cmd);
        delay_start(at_command_delay, 2000);
        register_status = COM_REG_WAIT_SEND_ACK_RAW;
      } else {
        register_status = COM_REG_ACK;
      }

      break;
    }

    case COM_REG_WAIT_SEND_ACK_RAW: {
      if (ATCore_is_send_ready()) {
        //! TODO: Poner el envelope acá y trabajarlo en
        //! la funcion de build.
        //! Asi me evito llamar dos veces a la funcion.
        if (delay_has_finished(at_command_delay)) {
          uint8_t *aux;
          aux = Parser_fBuild_Envelope(&envp, &envp_size);
          ATCore_send_data(aux, envp_size);
          register_status = COM_REG_WAIT_ACK_SEND;
        } else {
          register_status = COM_REG_WAIT_SEND_ACK_RAW;
        }
      }
      break;
    }

    case COM_REG_WAIT_ACK_SEND: {
      if (ATCore_is_response_ready()) {
        ATCore_check_response();
        if (ATCore_get_response_status() == BG95_RESP_SEND_OK) {
          register_status = COM_REG_FINISHED;
        } else {
          register_status = COM_REG_ACK;
        }
      } else {
        register_status = COM_REG_WAIT_ACK_SEND;
      }
      break;
    }

    case COM_REG_FINISHED: {
      // Hacer nada, para siempre.
      break;
    }
  }
}

int Com_UDP_context_process() {
  atcmd_desc_t cmd = (atcmd_desc_t)ATCMD_DESC_DEFAULT;

  switch (udp.state) {
    case COM_UDP_IDLE: {
      if (!upd_pdp_context_ready) {
        cmd.id = CMD_AT_QIACT;
        cmd.cmd_mode = AT_CMD_WRITE_DEFAULT;
        ATCore_send_cmd(&cmd);
        udp.state = COM_UDP_QIACT_WAIT;
      }
      break;
    }

    case COM_UDP_QIACT_WAIT: {
      if (ATCore_is_response_ready()) {
        ATCore_check_response();
        if (ATCore_get_response_status() == 0) {
          udp.state = COM_UDP_QIOPEN_SEND;
        } else if (ATCore_get_response_status() != 0 ||
                   udp.event == EV_UDP_TIMEOUT) {
          if (udp.retries++ < 3) {
            cmd.id = CMD_AT_QIACT;
            cmd.cmd_mode = AT_CMD_WRITE_DEFAULT;
            ATCore_send_cmd(&cmd);
          } else {
            udp.state = COM_UDP_PROCESS_ERROR;
          }
        }
      }
      break;
    }

    case COM_UDP_QIOPEN_SEND: {
      cmd.id = CMD_AT_QIOPEN;
      cmd.cmd_mode = AT_CMD_WRITE_DEFAULT;
      ATCore_send_cmd(&cmd);
      udp.state = COM_UDP_QIOPEN_WAIT;
      // udp.timeout_ms = 15000;
      break;
    }

    case COM_UDP_QIOPEN_WAIT: {
      if (ATCore_is_response_ready()) {
        ATCore_check_response();
        if (ATCore_get_response_status() == 0) {
          udp.state = COM_UDP_PROCESS_DONE;
          upd_pdp_context_ready = true;
        } else {
          udp.state = COM_UDP_PROCESS_ERROR;
        }
      }
      break;
    }

    case COM_UDP_PROCESS_DONE: {
      printf("UDP context established.\r\n");
      break;
    }

    case COM_UDP_PROCESS_ERROR: {
      printf("UDP setup failed.\n");
      break;
    }
  }

  return udp.state;
}