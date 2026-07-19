/**
 * @file bg95.h
 * @ingroup bg95
 * @brief Quectel BG95 modem driver: power control, readiness check and
 *        UART/DMA transport.
 *
 * @version 0.2
 * @date 2025-10-24
 */

#ifndef _BG95_H
#define _BG95_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "stm32l0xx_hal.h"
#include "bg95_at_cmd_lib.h"

/**
 * @addtogroup bg95
 * @{
 */

#define BG95_RX_BUFFER_SIZE 512   /**< Reception buffer size (bytes). */
#define BG95_TX_BUFFER_SIZE 300   /**< Transmission buffer size (bytes). */

#define BG95_PWRKEY_ON_MS        600   /**< PWRKEY low pulse to turn ON (>=500 ms). */
#define BG95_PWRKEY_OFF_MS       1000  /**< PWRKEY low pulse to turn OFF (>=650 ms). */
#define BG95_BOOT_TIMEOUT_MS     20000 /**< Max wait for modem ready after power on (cold boot ~13-16 s). */
#define BG95_AT_POLL_INTERVAL_MS 500   /**< Interval between AT polling attempts. */
#define BG95_POWEROFF_TIMEOUT_MS 30000 /**< Max wait for STATUS LOW after the PWRKEY off pulse. */

/* Defaults for BG95_wait_until_ready() — values from TCP/IP Application
 * Note v1.4, fig 1 (page 11 of the PDF). */
#define BG95_READY_AT_TIMEOUT_MS   15000  /**< AT responds — typical modem boot ~13 s. */
#define BG95_READY_SIM_TIMEOUT_MS  20000  /**< AT+CPIN? READY — Quectel recommends 20 s. */
#define BG95_READY_NET_TIMEOUT_MS  60000  /**< AT+CEREG? stat=1|5 — Quectel recommends 60 s. */

/** @brief Result code of a BG95 driver operation. */
typedef enum {
  BG95_OK = 0,               /**< Operation succeeded. */
  BG95_ERROR_NULL_POINTER,   /**< A required pointer argument was NULL. */
  BG95_SEND_CMD_ERROR,       /**< Failed to transmit the command. */
  BG95_CMD_RESP_ERROR,       /**< No/invalid response, or timeout. */
  BG95_ERROR_WRONG_PARAMS,   /**< Invalid parameters. */
} bg95_status_t;

/** @brief Classification of the last modem response. */
typedef enum {
  BG95_RESP_OK = 0,          /**< Terminated with "OK". */
  BG95_RESP_SEND_OK,         /**< Terminated with "SEND OK". */
  BG95_RESP_QI,              /**< A "+QI..." URC/response. */
  BG95_RESP_ERROR,           /**< Terminated with "ERROR". */
  BG95_RESP_ERROR_CME,       /**< "+CME ERROR:" response. */
  BG95_RESP_NOT_RECEIVED,    /**< No response received yet. */
} bg95_resp_status_t;

/**
 * @brief Result of BG95_wait_until_ready().
 *        Distinguishes the 3 phases so callers can pick an appropriate
 *        recovery action (reset MCU vs sleep until next cycle).
 */
typedef enum {
  BG95_READY_OK = 0,        /**< Modem is ready (AT + SIM + network). */
  BG95_READY_AT_TIMEOUT,    /**< AT not answering — HW/boot broken. */
  BG95_READY_SIM_TIMEOUT,   /**< SIM never reaches READY — missing or PIN-locked. */
  BG95_READY_NET_TIMEOUT,   /**< CEREG never reaches stat=1|5 — poor coverage. */
} bg95_ready_t;

/** @brief A GPIO pin (port + pin mask) used for modem control lines. */
typedef struct {
  GPIO_TypeDef *GPIOx;   /**< GPIO port. */
  uint16_t GPIO_Pin;     /**< GPIO pin mask. */
} BG95_Pin_t;

/** @brief BG95 device instance: UART, RX/TX buffers, state and control pins. */
typedef struct {
  UART_HandleTypeDef *huart;               /**< UART handle used for the modem. */

  char rxBuffer[BG95_RX_BUFFER_SIZE];      /**< DMA reception buffer. */
  uint16_t rx_size;                        /**< Bytes received in the last transfer. */
  char lastResponse[BG95_RX_BUFFER_SIZE];  /**< Cleaned copy of the last response. */
  uint16_t last_response_size;             /**< Size of ::lastResponse. */
  uint16_t last_response_fields;           /**< Number of parsed fields. */
  char txBuffer[BG95_TX_BUFFER_SIZE];      /**< Transmission buffer. */

  bool responseReady;                      /**< A full response is available. */
  bg95_resp_status_t responseStatus;       /**< Classification of the last response. */
  bool data_mode;                          /**< Waiting for the '>' data prompt. */
  bool data_send_rdy;                      /**< The '>' prompt was received. */

  BG95_Pin_t lvl_shifter_pin;              /**< Level-shifter enable pin. */
  BG95_Pin_t pwrkey_pin;                   /**< PWRKEY output pin. */
  BG95_Pin_t status_pin;                   /**< STATUS input pin. */
} BG95_t;

/** @brief Driver v-table used by ATCore to talk to the device generically. */
typedef struct {
  void (*init) (BG95_t *bg95, UART_HandleTypeDef *huart);                              /**< Initialize the device. */
  bg95_status_t (*send_command)(BG95_t *device, const char *cmd, uint16_t cmd_size);   /**< Send an AT command. */
  bg95_status_t (*send_data)(BG95_t *device, const uint8_t *data, uint16_t data_size); /**< Send raw data bytes. */
  bool (*is_response_ready)(BG95_t *bg95);            /**< Whether a response is ready. */
  bool (*process_response)(BG95_t *bg95);             /**< Parse the received response. */
  bg95_status_t (*quick_check_response)(BG95_t *bg95);/**< Quick response classification. */
  uint8_t (*get_response_status)(BG95_t *bg95);       /**< Last response status. */
  char* (*get_response)(BG95_t *bg95);                /**< Pointer to the last response. */
  void (*rx_callback)(BG95_t *device, uint16_t rx_size); /**< UART RX-complete callback. */
  void (*reset_rx)(BG95_t *bg95);                     /**< Abort DMA and clear RX state. */
} bg95_driver_t;

/* Public API — see bg95.c for the per-function documentation. */
void BG95_init(BG95_t *bg95, UART_HandleTypeDef *huart);
bg95_status_t BG95_power_on(BG95_t *bg95);
bg95_status_t BG95_power_off(BG95_t *bg95);
bg95_ready_t BG95_wait_until_ready(BG95_t *bg95,
                                    uint32_t at_timeout_ms,
                                    uint32_t sim_timeout_ms,
                                    uint32_t net_timeout_ms);
bg95_status_t BG95_send_command(BG95_t *bg95, const char *cmd, uint16_t cmd_size);
bg95_status_t BG95_send_raw_data(BG95_t *bg95, const uint8_t *data, uint16_t data_size);
bool BG95_process_rx(BG95_t *bg95);
bg95_status_t BG95_quick_check_response(BG95_t *bg95);
bool BG95_is_response_ready(BG95_t *bg95);
uint8_t BG95_get_response_status(BG95_t *bg95);
char* BG95_get_last_response(BG95_t *bg95);
void BG95_rxcplt_callback(BG95_t *bg95, uint16_t rx_size);
void BG95_reset_rx(BG95_t *bg95);

/** @} */

#endif
