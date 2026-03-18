/**
 * @file bg95.c
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2025-10-24
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "bg95.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32l0xx_hal_def.h"
#include "stm32l0xx_hal_uart.h"
#include "stm32l0xx_hal_uart_ex.h"

const bg95_driver_t BG95_Driver = {
    .init = &BG95_init,
    .send_command = &BG95_send_command,
    .send_data = &BG95_send_raw_data,
    .is_response_ready = &BG95_is_response_ready,
    .process_response = &BG95_process_rx,
    .quick_check_response = &BG95_quick_check_response,
    .get_response_status = &BG95_get_response_status,
    .get_response = &BG95_get_last_response,
    .rx_callback = &BG95_rxcplt_callback,
    .reset_rx = &BG95_reset_rx};

/* ---------------------- Private functions declaration --------------------- */

static void _enable_lvl_shifter(BG95_t *bg95);
static void _disable_lvl_shifter(BG95_t *bg95);
static bg95_status_t _BG95_check_response(BG95_t *bg95);
static bg95_status_t _BG95_process_data(BG95_t *bg95);
static void _BG95_set_last_response(BG95_t *bg95, uint16_t size);

/* -------------------------------- Callbacks ------------------------------- */

/**
 * @brief
 *
 * @param bg95
 * @param huart
 * @param rx_size
 */
void BG95_rxcplt_callback(BG95_t *bg95, uint16_t rx_size) {
  if (rx_size == 0) {  // Si esto se da, hubo algun error.
    bg95->responseReady = false;
    return;
  }

  bg95->rx_size = rx_size;

  /*Sometimes, BG95 sends '\0' character before the message*/
  /*This is problematic for strstr function usage.*/
  if (bg95->rxBuffer[0] == '\0') {
    bg95->rxBuffer[0] = 76;
  }

  HAL_UART_DMAStop(bg95->huart);

  if (bg95->data_mode) {
    bool prompt_found = false;
    for (uint16_t i = 0; i < rx_size; i++) {
      if (bg95->rxBuffer[i] == '>') {
        prompt_found = true;
        bg95->data_send_rdy = true;
        bg95->data_mode = false;

        break;
      }
    }
    if (prompt_found) return;
  } else {
    bg95->responseReady = true;
  }
}

/* ----------------------- Public functions definition ---------------------- */

/**
 * @brief
 *
 * @param bg95
 * @param huart
 */
void BG95_init(BG95_t *bg95, UART_HandleTypeDef *huart) {
  bg95->huart = huart;
  bg95->responseReady = false;
  bg95->responseStatus = BG95_RESP_NOT_RECEIVED;
  bg95->last_response_size = 0;
  bg95->rx_size = 0;
  bg95->data_mode = false;

  memset(bg95->rxBuffer, 0, BG95_RX_BUFFER_SIZE);
  memset(bg95->txBuffer, 0, BG95_TX_BUFFER_SIZE);
  memset(bg95->lastResponse, 0, BG95_RX_BUFFER_SIZE);

  bg95->lvl_shifter_pin.GPIOx = ENA_LVL_SHIFTER_GPIO_Port;
  bg95->lvl_shifter_pin.GPIO_Pin = ENA_LVL_SHIFTER_Pin;
  /// TODO: Agregar en un futuro pines para on/off del módulo.
}

/**
 * @brief
 *
 * @param bg95
 * @param cmd
 * @param cmd_size
 * @return bg95_status_t
 */
bg95_status_t BG95_send_command(BG95_t *bg95, const char *cmd,
                                uint16_t cmd_size) {
  if (bg95 == NULL || cmd == NULL) return BG95_ERROR_NULL_POINTER;

  if (cmd_size > BG95_TX_BUFFER_SIZE) return BG95_ERROR_WRONG_PARAMS;

  strncpy(bg95->txBuffer, cmd, cmd_size);
  bg95->txBuffer[cmd_size] = '\0';
  bg95->responseReady = false;

  __HAL_UART_FLUSH_DRREGISTER(bg95->huart);

  /** TODO: Esto moverlo a la funcion de power-on el día de mañana */
  _enable_lvl_shifter(bg95);

  if ((HAL_UARTEx_ReceiveToIdle_DMA(bg95->huart, (uint8_t *)bg95->rxBuffer,
                                    BG95_RX_BUFFER_SIZE)) != HAL_OK) {
    HAL_UART_DMAStop(bg95->huart);
    return BG95_SEND_CMD_ERROR;
  }

  if ((HAL_UART_Transmit(bg95->huart, (uint8_t *)bg95->txBuffer,
                         strlen(bg95->txBuffer), HAL_MAX_DELAY)) != HAL_OK) {
    return BG95_SEND_CMD_ERROR;
  }

  return BG95_OK;
}

/**
 * @brief
 *
 * @param bg95
 * @param data
 * @param data_size
 * @return bg95_status_t
 */
bg95_status_t BG95_send_raw_data(BG95_t *bg95, const uint8_t *data,
                                 uint16_t data_size) {
  if (bg95 == NULL || data == NULL) return BG95_ERROR_NULL_POINTER;

  if (data_size > BG95_TX_BUFFER_SIZE) return BG95_ERROR_WRONG_PARAMS;

  bg95->responseReady = false;
  bg95->data_send_rdy = false;

  __HAL_UART_FLUSH_DRREGISTER(bg95->huart);

  /** TODO: Esto moverlo a la funcion de power-on el día de mañana */
  _enable_lvl_shifter(bg95);

  if ((HAL_UARTEx_ReceiveToIdle_DMA(bg95->huart, (uint8_t *)bg95->rxBuffer,
                                    BG95_RX_BUFFER_SIZE)) != HAL_OK) {
    HAL_UART_DMAStop(bg95->huart);
    return BG95_SEND_CMD_ERROR;
  }

  if ((HAL_UART_Transmit(bg95->huart, data, data_size, HAL_MAX_DELAY)) !=
      HAL_OK) {
    return BG95_SEND_CMD_ERROR;
  }

  return BG95_OK;
}

/**
 * @brief
 *
 * @param bg95
 * @retval true
 * @retval false
 */
bool BG95_process_rx(BG95_t *bg95) {
  if (!bg95->responseReady) return false;

  if (!bg95->data_mode) {
    if (_BG95_check_response(bg95) != BG95_OK) return false;

    _BG95_set_last_response(bg95, bg95->rx_size);

    _disable_lvl_shifter(bg95);
    bg95->responseReady = false;
    bg95->rx_size = 0;
    return true;

  } else {
    if (_BG95_process_data(bg95) == BG95_OK) {
      _disable_lvl_shifter(bg95);
      bg95->responseReady = false;
      bg95->rx_size = 0;
      return true;
    } else {
      _disable_lvl_shifter(bg95);
      bg95->responseReady = false;
      bg95->rx_size = 0;
      return false;
    }
  }

  _disable_lvl_shifter(bg95);
  bg95->responseReady = false;
  bg95->rx_size = 0;

  return false;
}

/**
 * @brief
 *
 * @param bg95
 * @return bg95_status_t
 */
bg95_status_t BG95_quick_check_response(BG95_t *bg95) {
  if (bg95 == NULL) return BG95_ERROR_NULL_POINTER;

  if ((strstr(bg95->rxBuffer, AT_RESPONSE_ERR) != NULL)) {
    bg95->responseStatus = BG95_RESP_ERROR;
  } else if ((strstr(bg95->rxBuffer, AT_RESPONSE_ERR_CME) != NULL)) {
    bg95->responseStatus = BG95_RESP_ERROR_CME;
  } else if ((strstr(bg95->rxBuffer, AT_RESPONSE_SEND_OK) != NULL)) {
    bg95->responseStatus = BG95_RESP_SEND_OK;
  } else if (strstr(bg95->rxBuffer, AT_RESPONSE_OK) != NULL) {
    bg95->responseStatus = BG95_RESP_OK;
  } else {
    bg95->responseReady = false;
    bg95->rx_size = 0;
    _disable_lvl_shifter(bg95);
    return BG95_CMD_RESP_ERROR;
  }

  bg95->responseReady = false;
  bg95->rx_size = 0;

  _disable_lvl_shifter(bg95);

  return BG95_OK;
}

/**
 * @brief
 *
 * @param bg95
 * @return true
 * @return false
 */
bool BG95_is_response_ready(BG95_t *bg95) {
  if (bg95 == NULL) return false;

  return bg95->responseReady;
}

/**
 * @brief
 *
 * @param bg95
 * @return uint8_t
 */
uint8_t BG95_get_response_status(BG95_t *bg95) { return bg95->responseStatus; }

/**
 * @brief Get the last response object
 * @param bg95
 * @return char*
 */
char *BG95_get_last_response(BG95_t *bg95) {
  if (bg95 == NULL) return NULL;

  return bg95->lastResponse;
}

/**
 * @brief Abort any pending DMA reception and reset all RX state.
 *        Call this on error/timeout before restarting the FSM.
 */
void BG95_reset_rx(BG95_t *bg95) {
  if (bg95 == NULL) return;

  HAL_UART_DMAStop(bg95->huart);

  bg95->responseReady = false;
  bg95->responseStatus = BG95_RESP_NOT_RECEIVED;
  bg95->data_mode = false;
  bg95->data_send_rdy = false;
  bg95->rx_size = 0;

  memset(bg95->rxBuffer, 0, BG95_RX_BUFFER_SIZE);
}

/* ----------------------- Private function definition ---------------------- */

/**
 * @brief
 *
 * @param bg95
 */
static void _enable_lvl_shifter(BG95_t *bg95) {
  HAL_GPIO_WritePin(bg95->lvl_shifter_pin.GPIOx, bg95->lvl_shifter_pin.GPIO_Pin,
                    GPIO_PIN_SET);
}

static void _disable_lvl_shifter(BG95_t *bg95) {
  HAL_GPIO_WritePin(bg95->lvl_shifter_pin.GPIOx, bg95->lvl_shifter_pin.GPIO_Pin,
                    GPIO_PIN_RESET);
}

/**
 * @brief
 *
 * @param bg95
 * @return bg95_status_t
 */
static bg95_status_t _BG95_check_response(BG95_t *bg95) {
  if (bg95 == NULL) return BG95_ERROR_NULL_POINTER;

  if ((strstr(bg95->rxBuffer, AT_RESPONSE_QI) != NULL)) {
    bg95->responseStatus = BG95_RESP_QI;
  } else if ((strstr(bg95->rxBuffer, AT_RESPONSE_ERR_CME) != NULL)) {
    bg95->responseStatus = BG95_RESP_ERROR_CME;
  } else if (strstr(bg95->rxBuffer, AT_RESPONSE_ERR) != NULL) {
    bg95->responseStatus = BG95_RESP_ERROR;
  } else if (strstr(bg95->rxBuffer, AT_RESPONSE_OK) != NULL) {
    bg95->responseStatus = BG95_RESP_OK;
  }

  return BG95_OK;
}

/**
 * @brief
 *
 * @param bg95
 * @return bg95_status_t
 */
static bg95_status_t _BG95_process_data(BG95_t *bg95) {
  if (bg95 == NULL) return BG95_ERROR_NULL_POINTER;

  const char *p = strstr(bg95->rxBuffer, "+QIRD:");

  if (!p) return BG95_CMD_RESP_ERROR;  // not found

  p += 6;                               // skip "+QIRD:"
  while (*p == ' ' || *p == '\t') p++;  // skip spaces if any

  char *end;
  uint32_t value = strtol(p, &end, 10);

  if (end == p) return BG95_CMD_RESP_ERROR;  // no digits found

  memcpy(bg95->lastResponse, end + 2, value);
  bg95->last_response_size = value;
  bg95->last_response_fields = 1;
  bg95->data_mode = false;

  return BG95_OK;
}

/**
 * @brief Set the last response message (cleaned) and count its
 * fields.<br><br>
 *
 * Removes:<br>
 * - Leading \r\n<br>
 * - Final <code>\r\nOK\r\n</code>, <code>\r\nERROR\r\n</code> or
 * <code>\r\n</code> (for CME errors)<br><br>Also counts number of fields
 * (<code>\r\n</code> separated lines).
 *
 * @warning Only used this function with an OK response.
 *
 * @param bg95 Pointer to BG95 instance
 * @param size Size of received data in rxBuffer
 */
static void _BG95_set_last_response(BG95_t *bg95, uint16_t size) {
  if (bg95 == NULL || size == 0) return;

  bg95->rxBuffer[size] = '\0';

  char *start = bg95->rxBuffer;
  char *end = bg95->rxBuffer + size;

  if (strncmp(start, "\r\n", 2) == 0) {
    start += 2;
  }

  char *aux_ptr = NULL;

  if (bg95->responseStatus == BG95_RESP_OK) {
    aux_ptr = strstr(start, AT_RESPONSE_OK);
  } else if (bg95->responseStatus == BG95_RESP_ERROR) {
    aux_ptr = strstr(start, AT_RESPONSE_ERR);
  } else if (bg95->responseStatus == BG95_RESP_ERROR_CME) {
    aux_ptr = end - 2; /* In this case I only need to discard the las \r\n */
  } else if (bg95->responseStatus == BG95_RESP_QI) {
    aux_ptr = end - 2; /* In this case I only need to discard the las \r\n */
  }

  if (aux_ptr) {
    end = aux_ptr;
  }

  uint16_t len = (uint16_t)(end - start);
  if (len >= sizeof(bg95->lastResponse)) len = sizeof(bg95->lastResponse) - 1;

  strncpy(bg95->lastResponse, start, len);
  bg95->lastResponse[len] = '\0';
  bg95->last_response_size = len;

  uint16_t fields = 0;

  if (bg95->responseStatus != BG95_RESP_QI) {
    for (char *p = bg95->lastResponse; *p; ++p) {
      if (p[0] == '\r' && p[1] == '\n') {
        fields++;
        p++;
      }
    }

    bg95->last_response_fields = fields;
  }
}