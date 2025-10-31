/**
 * @file bg95.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-10-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef _BG95_H
#define _BG95_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "stm32l0xx_hal.h"
#include "bg95_at_cmd_lib.h"

#define BG95_RX_BUFFER_SIZE 512
#define BG95_TX_BUFFER_SIZE 128
#define ENA_LVL_SHIFTER_Pin GPIO_PIN_0
#define ENA_LVL_SHIFTER_GPIO_Port GPIOB

/**
 * @brief 
 * 
 */
typedef enum {
  BG95_OK = 0,
  BG95_ERROR_NULL_POINTER,
  BG95_SEND_CMD_ERROR,
  BG95_ERROR_WRONG_PARAMS,
} bg95_status_t;

/**
 * @brief 
 * 
 */
typedef enum {
  BG95_RESP_OK = 0,
  BG95_RESP_ERROR,
  BG95_RESP_ERROR_CME,
  BG95_RESP_NOT_FINISHED,
} bg95_resp_status_t;

/**
 * @brief 
 * 
 */
typedef struct {
  GPIO_TypeDef *GPIOx;
  uint16_t GPIO_Pin;
  
} BG95_Pin_t;

/**
 * @brief 
 * 
 */
typedef struct {
  UART_HandleTypeDef *huart; /** */

  char rxBuffer[BG95_RX_BUFFER_SIZE]; /** */
  char lastResponse[BG95_RX_BUFFER_SIZE]; /** */
  uint16_t last_response_size; /** */
  uint16_t last_response_fields; /** */
  char txBuffer[BG95_TX_BUFFER_SIZE]; /** */

  bool responseReady; /** */
  bg95_resp_status_t responseStatus; /** */
  bool data_mode; /** */ // Esto en principio no hace falta

  BG95_Pin_t lvl_shifter_pin; /** */

} BG95_t;

/**
 * @brief 
 * 
 */
typedef struct {
  void (*init) (BG95_t *bg95, UART_HandleTypeDef *huart); /** */
  bg95_status_t (*send_command)(BG95_t *device, const char *cmd, uint16_t cmd_size); /** */
  bool (*is_response_ready)(BG95_t *bg95);
  char* (*get_response)(BG95_t *bg95); /** */
  void (*rx_callback)(BG95_t *device, uint16_t rx_size); /** */
} bg95_driver_t;


void BG95_init(BG95_t *bg95, UART_HandleTypeDef *huart);
bg95_status_t BG95_send_command(BG95_t *bg95, const char *cmd, uint16_t cmd_size);
bool BG95_is_response_ready(BG95_t *bg95);
char* BG95_get_last_response(BG95_t *bg95);
void BG95_rxcplt_callback(BG95_t *bg95, uint16_t rx_size);

#endif
