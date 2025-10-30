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

const char AT_RESPONSE_OK[] = "\r\nOK\r\n";
const char AT_RESPONSE_ERR[] = "\r\nERROR\r\n";
const char AT_RESPONSE_ERR_CME[] = "\r\n+CME ERROR:";

const bg95_driver_t BG95_Driver = {.init = &BG95_init,
                                   .send_command = &BG95_send_command,
                                   .is_response_ready = &BG95_is_response_ready,
                                   .check_response = &BG95_check_response,
                                   .get_response = &BG95_get_last_response,
                                   .rx_callback = &BG95_rxcplt_callback};

/* ---------------------- Private functions declaration --------------------- */

static void _BG95_config(BG95_t *bg95);
static void _enable_lvl_shifter(BG95_t *bg95);
static void _disable_lvl_shifter(BG95_t *bg95);
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

  bg95->responseReady = true;

  /*Sometimes, BG95 sends '\0' character before the message*/
  /*This is problematic for strstr function usage.*/
  if (bg95->rxBuffer[0] == '\0') {
    bg95->rxBuffer[0] = 76;
  }

  _BG95_set_last_response(bg95, rx_size);

  HAL_UART_DMAStop(bg95->huart);

  if (bg95->data_mode) {
    HAL_UARTEx_ReceiveToIdle_DMA(bg95->huart, (uint8_t *)bg95->rxBuffer,
                                 BG95_RX_BUFFER_SIZE);
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
  bg95->last_response_size = 0;
  bg95->data_mode = false;

  memset(bg95->rxBuffer, 0, BG95_RX_BUFFER_SIZE);
  memset(bg95->txBuffer, 0, BG95_TX_BUFFER_SIZE);
  memset(bg95->lastResponse, 0, BG95_RX_BUFFER_SIZE);

  bg95->lvl_shifter_pin.GPIOx = ENA_LVL_SHIFTER_GPIO_Port;
  bg95->lvl_shifter_pin.GPIO_Pin = ENA_LVL_SHIFTER_Pin;
  /// TODO: Agregar en un futuro pines para on/off del módulo.

  _BG95_config(bg95);
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

  /** TODO: Esto moverlo a la funcion de power-on el día de mañana */
  _enable_lvl_shifter(bg95);

  if ((HAL_UARTEx_ReceiveToIdle_DMA(bg95->huart, (uint8_t *)bg95->rxBuffer,
                                    BG95_RX_BUFFER_SIZE)) != HAL_OK) {
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
 * @return bg95_status_t
 */
bg95_status_t BG95_check_response(BG95_t *bg95) {
  if (bg95 == NULL) return BG95_ERROR_NULL_POINTER;

  if ((strstr(bg95->lastResponse, AT_RESPONSE_ERR) != NULL)) {
    bg95->responseStatus = BG95_RESP_ERROR;
  } else if ((strstr(bg95->lastResponse, AT_RESPONSE_ERR_CME) != NULL)) {
    bg95->responseStatus = BG95_RESP_ERROR_CME;
  } else if (strstr(bg95->lastResponse, AT_RESPONSE_OK) != NULL) {
    bg95->responseStatus = BG95_RESP_OK;
  }

  return BG95_OK;
}

/**
 * @brief Get the last response object
 * TODO: Esto esta medio al pedo ahora que lo pienso, pero lo dejo para
 * chequear.
 * @param bg95
 * @return char*
 */
char *BG95_get_last_response(BG95_t *bg95) {
  if (bg95 == NULL) return NULL;

  if (!bg95->responseReady) return NULL;

  return bg95->lastResponse;
}

/* ----------------------- Private function definition ---------------------- */

/**
 * @brief
 *
 * @param bg95
 */
static void _BG95_config(BG95_t *bg95) {}

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
 * @brief Set the last response object
 *
 * @param bg95
 * @param size
 */
static void _BG95_set_last_response(BG95_t *bg95, uint16_t size) {
  if (bg95 == NULL) return;

  bg95->rxBuffer[size] = '\0';
  bg95->last_response_size = size;

  uint16_t len = size;

  if (len >= sizeof(bg95->lastResponse)) len = sizeof(bg95->lastResponse) - 1;

  strncpy(bg95->lastResponse, bg95->rxBuffer, len);
  bg95->lastResponse[len] = '\0';
}