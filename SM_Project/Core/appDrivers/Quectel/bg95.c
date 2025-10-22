#include "bg95.h"

#include <stdio.h>
#include <string.h>
#include "stm32l0xx_hal_uart.h"
#include "stm32l0xx_hal_uart_ex.h"

const char AT_RESPONSE_OK[] = "\r\nOK\r\n";
const char AT_RESPONSE_ERR[] = "\r\nERROR\r\n";
const char AT_RESPONSE_ERR_CME[] = "\r\n+CME ERROR:";

const bg95_driver_t BG95_Driver = {
  .init = &BG95_init,
  .send_command = &BG95_send_command,
  .is_response_ready = &BG95_is_response_ready,
  .get_response = &get_last_response,
  .rx_callback = &BG95_rx_callback
};

void BG95_init(BG95_t *bg95, UART_HandleTypeDef *huart) {
  bg95->huart = huart;
  bg95->rxHead = bg95->rxTail = 0;
  bg95->responseReady = false;

  // HAL_UART_Receive_IT(bg95->huart, &bg95->rxByte, 1);
}

bg95_status_t BG95_send_command(BG95_t *bg95, const char *cmd, uint16_t cmd_size) {
  if (bg95 == NULL || cmd == NULL) return BG95_ERROR_NULL_POINTER;

  if (cmd_size > BG95_TX_BUFFER_SIZE) return BG95_ERROR_WRONG_PARAMS;

  strncpy(bg95->txBuffer, cmd, cmd_size);
  bg95->txBuffer[cmd_size + 1] = '\0';

  if ((HAL_UART_Transmit_IT(bg95->huart, (uint8_t *)bg95->txBuffer, strlen(bg95->txBuffer))) != HAL_OK) {
    return BG95_SEND_CMD_ERROR;
  }

  return BG95_OK;
}


bool BG95_is_response_ready(BG95_t *bg95) {
  if (bg95 == NULL) return false;

  if ((check_response_end(bg95) == BG95_OK) && bg95->responseStatus != BG95_RESP_NOT_FINISHED) {
    set_last_response(bg95); /* Acá copio el rxBuffer en lastResponse, reseteo rxBuffer(head y tail) y seteo responseReady a true*/
  }
  return bg95->responseReady;
}

bg95_status_t check_response_end(BG95_t *bg95) {
  if (bg95 == NULL) return BG95_ERROR_NULL_POINTER;

  if ((strstr(bg95->rxBuffer, AT_RESPONSE_ERR) != NULL)) {
    bg95->responseStatus = BG95_RESP_ERROR;
  } else if ((strstr(bg95->rxBuffer, AT_RESPONSE_ERR_CME) != NULL)) {
    bg95->responseStatus = BG95_RESP_ERROR_CME;
  } else if (strstr(bg95->rxBuffer, AT_RESPONSE_OK) != NULL) {
    bg95->responseStatus = BG95_RESP_OK;
  } else {
    bg95->responseStatus = BG95_RESP_NOT_FINISHED;
    bg95->responseReady = false;
  }

  if (bg95->responseStatus != BG95_RESP_NOT_FINISHED) {
    bg95->rxHead++;
    bg95->rxBuffer[bg95->rxHead] = '\0'; /*Le agrego un \0 para indicar que termino la cadena de caracteres*/
  }

  return BG95_OK;
}

void set_last_response(BG95_t *bg95){
  if (bg95 == NULL) return;

  size_t len = strlen(bg95->rxBuffer);

  if (len >= sizeof(bg95->lastResponse))
    len = sizeof(bg95->lastResponse) - 1;

  strncpy(bg95->lastResponse, bg95->rxBuffer, len);strncpy(bg95->lastResponse, bg95->rxBuffer, len);
  bg95->lastResponse[len] = '\0';

  bg95->rxHead = 0;
  bg95->rxTail = 0;
  memset(bg95->rxBuffer, 0, sizeof(bg95->rxBuffer));

  bg95->responseReady = true;
}

char* get_last_response(BG95_t *bg95){
  if (bg95 == NULL) return NULL;
  
  if (!bg95->responseReady) return NULL;

  return bg95->lastResponse;
}

// volatile uint32_t rx_total_count = 0;

void BG95_rx_callback(BG95_t *bg95){ 

}