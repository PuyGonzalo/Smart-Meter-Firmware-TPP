/**
 * @file at_core.h
 * @ingroup atcore
 * @brief Generic AT command framework (ATCore) public API.
 *
 * ATCore sits between the application modules and the concrete modem driver
 * (BG95). It owns the device instance and its driver v-table, builds AT command
 * strings from descriptors, sends them and exposes the parsed responses. The
 * device credentials (id/MAC) are also cached here.
 *
 * @version 0.2
 * @date 2025-10-24
 */

#ifndef _AT_CORE_H_
#define _AT_CORE_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "at_device.h"
#include "at_parser.h"
#include "at_net.h"
#include "bg95.h"

/**
 * @addtogroup atcore
 * @{
 */

/** @brief Result of an AT operation. */
typedef enum {
  ATSTATUS_OK = 0,           /**< Command completed successfully. */
  ATSTATUS_ERROR,            /**< Command failed or returned ERROR. */
  ATSTATUS_TIMEOUT,          /**< No response within the timeout. */
  ATSTATUS_OK_PENDING_URC,   /**< OK, but an unsolicited result code is pending. */
} at_status_t;

/** @brief Whether the end-of-message marker was found while parsing. */
typedef enum {
  ATENDMSG_YES = 0,          /**< End of message reached. */
  ATENDMSG_NO,               /**< More data expected. */
  ATENDMSG_ERROR,            /**< Parse error. */
} at_endmsg_t;

/** @brief ATCore context: bound device, its driver and cached credentials. */
typedef struct
{
  void *dev_driver;                    /**< Concrete driver v-table (BG95_Driver). */
  void *device;                        /**< Concrete device instance (BG95_t). */

  uint8_t device_id[ENV_DEVID_BYTES];  /**< Device id assigned by the HES. */
  uint8_t device_mac[ENV_MAC_BYTES];   /**< MAC assigned by the HES. */

  uint8_t net_context;                 /**< Network/PDP context id. */

  uint8_t in_data_mode;   /**< Current mode: DATA = 1, COMMAND = 0. */
  uint8_t processing_cmd; /**< 1 while a command is in flight, 0 when idle. */
} at_context_t;

/**
 * @brief Initialize ATCore and the underlying device driver.
 * @param huart UART handle used to talk to the modem.
 */
void ATCore_init(UART_HandleTypeDef *huart);

/** @brief Reserved for future device configuration (currently a no-op). */
void ATCore_config();

/**
 * @brief Power the modem on (enable level shifter, pulse PWRKEY, poll AT).
 * @retval true  Modem answered AT.
 * @retval false Power-on timed out.
 */
bool ATCore_power_on(void);

/**
 * @brief Power the modem off gracefully and disable the level shifter.
 * @retval true  Clean power-down.
 * @retval false Timed out waiting for STATUS to go low.
 */
bool ATCore_power_off(void);

/**
 * @brief Wait until the modem is usable: AT alive + SIM READY + network attach.
 * @param at_timeout_ms  Timeout for the AT-alive phase.
 * @param sim_timeout_ms Timeout for the SIM-ready phase.
 * @param net_timeout_ms Timeout for the network-attach phase.
 * @return ::bg95_ready_t — OK or the specific phase that failed.
 */
bg95_ready_t ATCore_wait_until_ready(uint32_t at_timeout_ms,
                                      uint32_t sim_timeout_ms,
                                      uint32_t net_timeout_ms);

/** @brief Cache the device id (::ENV_DEVID_BYTES bytes). */
void ATCore_set_device_id(uint8_t *dev_id);

/** @brief Compare @p dev_id against the cached device id. @return true if equal. */
bool ATCore_compare_device_id(uint8_t *dev_id);

/** @brief Copy the cached device id into @p dev_id_cpy. */
void ATCore_get_device_id(uint8_t *dev_id_cpy);

/** @brief Cache the device MAC (::ENV_MAC_BYTES bytes). */
void ATCore_set_device_mac(uint8_t *dev_mac);

/** @brief Compare @p dev_mac against the cached MAC. @return true if equal. */
bool ATCore_compare_device_mac(uint8_t *dev_mac);

/** @brief Copy the cached MAC into @p mac_cpy. */
void ATCore_get_device_mac(uint8_t *mac_cpy);

/**
 * @brief Build an AT command from a descriptor and send it to the modem.
 * @param cmd Command descriptor (id, mode, parameters).
 * @retval true  Command was transmitted.
 * @retval false Invalid command id or transmit error.
 */
bool ATCore_send_cmd(atcmd_desc_t *cmd);

/**
 * @brief Send raw bytes to the modem (used in data mode after a '>' prompt).
 * @param data      Bytes to send.
 * @param data_size Number of bytes.
 * @retval true on success.
 */
bool ATCore_send_data(const uint8_t *data, uint16_t data_size);

/** @brief Whether a complete response has been received. */
bool ATCore_is_response_ready();

/** @brief Whether the modem raised the data-mode prompt ('>'). */
bool ATCore_is_send_ready();

/**
 * @brief Quick classification of the last response without full parsing.
 * @return A ::bg95_status_t value.
 */
uint8_t ATCore_check_response();

/**
 * @brief Parse the received response into fields.
 * @retval true  Response parsed successfully.
 * @retval false Parse error.
 */
bool ATCore_process_response();

/** @brief Status of the last response. @return A ::bg95_status_t value. */
uint8_t ATCore_get_response_status();

/**
 * @brief Copy the raw last response into a caller buffer.
 * @param[out] copy          Destination buffer.
 * @param      copy_size     Must equal ::BG95_RX_BUFFER_SIZE.
 * @param[out] response_size Number of valid bytes copied.
 * @retval true on success.
 */
bool ATCore_get_last_response(char *copy, uint16_t copy_size ,uint16_t *response_size);

/**
 * @brief Whether @p str appears in response field @p field_index.
 * @return true if found.
 */
bool ATCore_cmp_str_in_field(const char *str, uint16_t field_index);

/**
 * @brief Parse the length value from a +QIRD response.
 * @return The reported byte count, or a negative value on error.
 */
int16_t ATCore_get_first_qird_value();

/** @brief Arm data mode: the next reception looks for the '>' prompt. */
void ATCore_set_data_mode();

/** @brief Abort any in-flight DMA and clear the RX buffers and flags. */
void ATCore_reset_rx();

/**
 * @brief Fetch the modem IMEI via AT+CGSN (blocking, up to ~3 s).
 * @param[out] out NUL-terminated 15-digit IMEI string.
 * @param      cap Buffer capacity (must be >= 16).
 * @retval true  IMEI extracted.
 * @retval false Timeout, command error or parse failure.
 */
bool ATCore_get_imei(char *out, uint16_t cap);

/** @} */

#endif  // _ATCORE_H_
