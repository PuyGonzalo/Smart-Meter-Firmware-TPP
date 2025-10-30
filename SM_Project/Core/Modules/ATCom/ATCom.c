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

com_register_st register_status;

bool is_device_connected = true;

void Com_register_device_process() {
  switch (register_status) {
    case COM_REG_INIT: {
      if (is_device_connected) {
        register_status = COM_REG_UDP_CTX;
      }
    }

    case COM_REG_UDP_CTX: {
      if (Com_UDP_context_process() != 0) {
        register_status = COM_REG_SEND;
      }
    }

    case COM_REG_SEND: {
      // Funcion que hace el envío de la conexión al backdoor.
    }

    case COM_REG_VERIFY_SEND: {
    }

    case COM_REG_WAIT: {
      // Esperar la repsuesta del HES.
    }

    case COM_REG_PROCESS_RESP: {
      // Procesar Respuesta del HES
    }

    case COM_REG_ACK: {
      // Enviar ACK al HES.
      // Despues de esto, debería volver a COM_REG_VERIFY_SEND
    }

    case COM_REG_FINISHED: {
      // Hacer nada, para siempre.
    }
  }
}