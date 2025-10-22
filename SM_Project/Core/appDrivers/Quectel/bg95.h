#ifndef _BG95_H
#define _BG95_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "stm32l0xx_hal.h"

#define BG95_RX_BUFFER_SIZE 512
#define BG95_TX_BUFFER_SIZE 128


typedef enum {
  BG95_OK = 0,
  BG95_ERROR_NULL_POINTER,
  BG95_SEND_CMD_ERROR,
  BG95_ERROR_WRONG_PARAMS,
} bg95_status_t;

typedef enum {
  BG95_RESP_OK = 0,
  BG95_RESP_ERROR,
  BG95_RESP_ERROR_CME,
  BG95_RESP_NOT_FINISHED,
} bg95_resp_status_t;

typedef struct {
  UART_HandleTypeDef *huart;

  char rxBuffer[BG95_RX_BUFFER_SIZE];
  char lastResponse[BG95_RX_BUFFER_SIZE];
  volatile uint16_t rxHead;
  volatile uint16_t rxTail;
  char txBuffer[BG95_TX_BUFFER_SIZE];
  uint8_t rxByte;
  bool responseReady;
  bg95_resp_status_t responseStatus;

} BG95_t;

typedef struct {
  void (*init) (BG95_t *bg95, UART_HandleTypeDef *huart);
  bg95_status_t (*send_command)(BG95_t *device, const char *cmd, uint16_t cmd_size);
  bool (*is_response_ready)(BG95_t *bg95);
  char* (*get_response)(BG95_t *bg95);
  void (*rx_callback)(BG95_t *device);
} bg95_driver_t;


/* API */
void BG95_init(BG95_t *bg95, UART_HandleTypeDef *huart);
bg95_status_t BG95_send_command(BG95_t *bg95, const char *cmd, uint16_t cmd_size);
bool BG95_is_response_ready(BG95_t *bg95);
bg95_status_t check_response_end(BG95_t *bg95);
void set_last_response(BG95_t *bg95);
char* get_last_response(BG95_t *bg95);
void BG95_rx_callback(BG95_t *bg95);

#endif
