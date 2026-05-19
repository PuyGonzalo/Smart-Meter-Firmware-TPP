/**
 * @file at_core.c
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2025-10-24
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "at_core.h"

#include <stdint.h>
#include <string.h>

#include "at_device.h"
#include "at_parser.h"
#include "stm32l0xx_hal_def.h"
#include "stm32l0xx_hal_uart.h"

static at_context_t at_core;

#define AT_DEVICE ((BG95_t *)at_core.device)
#define AT_DRV ((const bg95_driver_t *)at_core.dev_driver)

/* -------------------------- Device specific BEGIN ------------------------- */

/**
 * @brief
 *
 */
extern const bg95_driver_t BG95_Driver;
static BG95_t bg95_device;

/**
 * @brief
 *
 */
static const BG95_at_LUT_t ATCMD_BG95_LUT[] = {
    /* cmd enum - cmd string - cmd timeout (in ms) - build cmd ftion - analyze
       cmd ftion */
    {CMD_AT, "", 2, fCmdBuild_NoParams},

    /* GENERAL MODEM commands */
    {CMD_AT_CGMI, "+GMI", 4, fCmdBuild_NoParams},
    {CMD_AT_CGMM, "+CGMM", 5, fCmdBuild_NoParams},
    {CMD_AT_CGMR, "+CGMR", 5, fCmdBuild_NoParams},
    {CMD_AT_CGSN, "+CGSN", 5, fCmdBuild_NoParams},
    {CMD_AT_CIMI, "+CIMI", 5, fCmdBuild_NoParams},
    {CMD_AT_CEER, "+CEER", 5, fCmdBuild_NoParams},
    {CMD_AT_CMEE, "+CMEE", 5, fCmdBuild_NoParams},
    {CMD_AT_CPIN, "+CPIN", 5, fCmdBuild_NoParams},
    {CMD_AT_CFUN, "+CFUN", 5, fCmdBuild_NoParams},
    {CMD_AT_COPS, "+COPS", 5, fCmdBuild_NoParams},
    {CMD_AT_CNUM, "+CNUM", 5, fCmdBuild_NoParams},
    {CMD_AT_CGATT, "+CGATT", 6, fCmdBuild_NoParams},
    {CMD_AT_CREG, "+CREG", 5, fCmdBuild_NoParams},
    {CMD_AT_CGREG, "+CGREG", 6, fCmdBuild_NoParams},
    {CMD_AT_CEREG, "+CEREG", 6, fCmdBuild_NoParams},
    {CMD_AT_CGEREP, "+CGEREP", 7, fCmdBuild_NoParams},
    {CMD_AT_CGEV, "+CGEV", 5, fCmdBuild_NoParams},
    {CMD_AT_CSQ, "+CSQ", 4, fCmdBuild_NoParams},
    {CMD_AT_CGDCONT, "+CGDCONT", 8, fCmdBuild_NoParams},
    {CMD_AT_CGACT, "+CGACT", 6, fCmdBuild_NoParams},
    {CMD_AT_CGDATA, "+CGDATA", 7, fCmdBuild_NoParams},
    {CMD_AT_CGPADDR, "+CGPADDR", 8, fCmdBuild_NoParams},
    {CMD_ATD, "D", 1, fCmdBuild_NoParams},
    {CMD_ATE0, "E0", 2, fCmdBuild_NoParams},
    {CMD_ATE0, "E1", 2, fCmdBuild_NoParams},
    {CMD_ATH, "H", 1, fCmdBuild_NoParams},
    {CMD_ATO, "O", 1, fCmdBuild_NoParams},
    {CMD_ATV, "V", 1, fCmdBuild_NoParams},
    {CMD_AT_AND_W, "&W", 2, fCmdBuild_NoParams},
    {CMD_AT_AND_D, "&D", 2, fCmdBuild_NoParams},
    {CMD_ATX, "X", 1, fCmdBuild_NoParams},
    {CMD_AT_IPR, "+IPR", 4, fCmdBuild_NoParams},
    {CMD_AT_IFC, "+IFC", 4, fCmdBuild_NoParams},

    /* TCP (IP) */
    {CMD_AT_QICSGP, "+QICSGP", 7, fCmdBuild_NoParams},
    {CMD_AT_QIACT, "+QIACT", 6, fCmdBuild_ATQIACT},
    {CMD_AT_QIDEACT, "+QIDEACT", 8, fCmdBuild_ATQIDEACT},
    {CMD_AT_QIOPEN, "+QIOPEN", 7, fCmdBuild_ATQIOPEN},
    {CMD_AT_QICLOSE, "+QICLOSE", 8, fCmdBuild_ATQICLOSE},
    {CMD_AT_QISTATE, "+QISTATE", 8, fCmdBuild_ATQISTATE},
    {CMD_AT_QIRD, "+QIRD", 5, fCmdBuild_ATQIRD},
    {CMD_AT_QISEND, "+QISEND", 7, fCmdBuild_ATQISEND},
    {CMD_AT_QISENDEX, "+QISENDEX", 10, fCmdBuild_ATQISENDEX},

    /* Other */
    {CMD_AT_QICFG, "+QICFG", 6, fCmdBuild_NoParams},

};

#define SIZE_ATCMD_BG95_LUT \
  ((uint16_t)(sizeof(ATCMD_BG95_LUT) / sizeof(BG95_at_LUT_t)))

/* --------------------------- Device specific END -------------------------- */

/* ---------------------- Private functions declaration --------------------- */

/* -------------------------------- Callbacks ------------------------------- */

/**
 * @brief
 *
 * @param huart
 * @param Size
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart->Instance == USART2) {
    AT_DRV->rx_callback(AT_DEVICE, Size);
  }
}

/* ----------------------- Public functions definition ---------------------- */

/**
 * @brief
 *
 * @param huart
 */
void ATCore_init(UART_HandleTypeDef *huart) {
  memset(&bg95_device, 0, sizeof(bg95_device));
  at_core.device = &bg95_device;

  at_core.dev_driver = (void *)&BG95_Driver;

  AT_DRV->init(AT_DEVICE, huart);
}

/**
 * @brief
 *
 */
void ATCore_config() {}

/**
 * @brief Power on the modem (enables level shifter and pulses PWRKEY).
 *
 * @return true if modem is ready, false on timeout.
 */
bool ATCore_power_on(void) {
  return BG95_power_on(AT_DEVICE) == BG95_OK;
}

/**
 * @brief Power off the modem (AT+QPOWD and disables level shifter).
 *
 * @return true on graceful power-down, false on timeout.
 */
bool ATCore_power_off(void) {
  return BG95_power_off(AT_DEVICE) == BG95_OK;
}

/**
 * @brief Wrapper over BG95_wait_until_ready(). Verifies the modem is in a
 *        usable state (AT alive + SIM READY + network attached) before
 *        network operations. Call after ATCore_power_on().
 */
bg95_ready_t ATCore_wait_until_ready(uint32_t at_timeout_ms,
                                      uint32_t sim_timeout_ms,
                                      uint32_t net_timeout_ms) {
  return BG95_wait_until_ready(AT_DEVICE, at_timeout_ms, sim_timeout_ms,
                                net_timeout_ms);
}

/**c
 * @brief
 *
 * @param dev_id
 */
void ATCore_set_device_id(uint8_t *dev_id) {
  for (int i = 0; i < ENV_DEVID_BYTES; i++) {
    at_core.device_id[i] = dev_id[i];
  }
}

/**
 * @brief
 *
 * @param dev_id
 * @retval true
 * @retval false
 */
bool ATCore_compare_device_id(uint8_t *dev_id) {
  bool result = true;

  for (int i = 0; i < ENV_DEVID_BYTES; i++) {
    if (at_core.device_id[i] != dev_id[i]) {
      result = false;
      break;
    }
  }

  return result;
}

/**
 * @brief
 *
 * @return uint8_t*
 */
void ATCore_get_device_id(uint8_t *dev_id_cpy) {
  // memcpy(dev_id_cpy, at_core.device_id, DEV_ID_BYTES);
  for (uint8_t i = 0; i < DEV_ID_BYTES; ++i) {
    dev_id_cpy[i] = at_core.device_id[i];
  }
}

/**
 * @brief
 *
 * @param dev_mac
 */
void ATCore_set_device_mac(uint8_t *dev_mac) {
  for (int i = 0; i < ENV_DEVID_BYTES; i++) {
    at_core.device_mac[i] = dev_mac[i];
  }
}

/**
 * @brief
 *
 * @param dev_mac
 * @retval true
 * @retval false
 */
bool ATCore_compare_device_mac(uint8_t *dev_mac) {
  bool result = true;

  for (int i = 0; i < ENV_DEVID_BYTES; i++) {
    if (at_core.device_mac[i] != dev_mac[i]) {
      result = false;
      break;
    }
  }

  return result;
}

/**
 * @brief
 *
 * @return uint8_t*
 */
void ATCore_get_device_mac(uint8_t *mac_cpy) {
  for (uint8_t i = 0; i < MAC_BYTES; ++i) {
    mac_cpy[i] = at_core.device_mac[i];
  }
}

/**
 * @brief
 *
 * @param cmd
 * @retval true
 * @retval false
 */
bool ATCore_send_cmd(atcmd_desc_t *cmd) {
  uint8_t ret;
  bool result;

  if (cmd->id >= SIZE_ATCMD_BG95_LUT) return false;

  uint16_t command_max_size = BG95_TX_BUFFER_SIZE;
  char command[command_max_size];
  uint32_t cmd_size_aux = 0;

  cmd->at_cmd_size = ATCMD_BG95_LUT[cmd->id].cmd_size;
  if (ATCMD_BG95_LUT[cmd->id].build) ATCMD_BG95_LUT[cmd->id].build(cmd);
  cmd_size_aux = cmd->at_cmd_size;

  if (cmd->cmd_mode == AT_CMD_EXEC) {
    snprintf(command, cmd_size_aux, "AT%s\r\n",
             ATCMD_BG95_LUT[cmd->id].cmd_string);
  } else if (cmd->cmd_mode == AT_CMD_TEST) {
    snprintf(command, cmd_size_aux, "AT%s=?\r\n",
             ATCMD_BG95_LUT[cmd->id].cmd_string);
  } else if (cmd->cmd_mode == AT_CMD_READ) {
    snprintf(command, cmd->at_cmd_size, "AT%s?\r\n",
             ATCMD_BG95_LUT[cmd->id].cmd_string);
  } else if (cmd->cmd_mode == AT_CMD_WRITE ||
             cmd->cmd_mode == AT_CMD_WRITE_DEFAULT ||
             cmd->cmd_mode == AT_CMD_WRITE_OPT) {
    cmd_size_aux =
        Parser_calculate_cmd_size(ATCMD_BG95_LUT[cmd->id].cmd_string, cmd);
    Parser_build_cmd(command, command_max_size,
                     ATCMD_BG95_LUT[cmd->id].cmd_string, cmd);
  }

  ret = AT_DRV->send_command(AT_DEVICE, command, cmd_size_aux);

  result = (ret == 0);

  return result;
}

bool ATCore_send_data(const uint8_t *data, uint16_t data_size) {
  uint8_t ret;
  bool result;

  ret = AT_DRV->send_data(AT_DEVICE, data, data_size);

  result = (ret == 0);

  return result;
}

/**
 * @brief
 *
 * @retval true
 * @retval false
 */
bool ATCore_is_response_ready() { return AT_DRV->is_response_ready(AT_DEVICE); }

/**
 * @brief
 *
 * @retval true
 * @retval false
 */
bool ATCore_is_send_ready() { return AT_DEVICE->data_send_rdy; }

uint8_t ATCore_check_response() {
  return AT_DRV->quick_check_response(AT_DEVICE);
}

/**
 * @brief
 *
 * @retval true
 * @retval false
 */
bool ATCore_process_response() { return AT_DRV->process_response(AT_DEVICE); }

/**
 * @brief
 *
 * @retval uint8_t Response status @see bg95_status_t
 */
uint8_t ATCore_get_response_status() {
  return AT_DRV->get_response_status(AT_DEVICE);
}

/**
 * @brief
 *
 * @return char*
 */
bool ATCore_get_last_response(char *copy, uint16_t copy_size,
                              uint16_t *response_size) {
  if (copy == NULL || response_size == NULL) return false;
  if (copy_size != BG95_RX_BUFFER_SIZE) return false;

  *response_size = AT_DEVICE->last_response_size;
  memcpy(copy, AT_DRV->get_response(AT_DEVICE), AT_DEVICE->last_response_size);

  return true;
}

/**
 * @brief
 *
 * @param str
 * @param field_index
 * @return true
 * @return false
 */
bool ATCore_cmp_str_in_field(const char *str, uint16_t field_index) {
  if (field_index > AT_DEVICE->last_response_fields) return false;
  char *aux_ptr;
  char *result;
  uint16_t field_size;

  aux_ptr =
      Parser_get_str_field(AT_DEVICE->lastResponse, field_index, &field_size);

  result = strstr(aux_ptr, str);

  return (result != NULL);
}

/**
 * @brief
 *
 * @return int16_t
 */
int16_t ATCore_get_first_qird_value() {
  char *aux_ptr;
  int16_t result;
  uint16_t field_size;

  aux_ptr = Parser_get_str_field(AT_DEVICE->lastResponse, 0, &field_size);

  result = Parser_get_first_qird_value(aux_ptr);

  return result;
}

/**
 * @brief
 *
 */
void ATCore_set_data_mode() { AT_DEVICE->data_mode = true; }

void ATCore_reset_rx() { AT_DRV->reset_rx(AT_DEVICE); }

/**
 * @brief Fetch IMEI from modem via AT+CGSN (blocking, up to 3 s).
 * @param out   Output buffer for NUL-terminated 15-digit IMEI string.
 * @param cap   Buffer capacity (must be >= 16).
 * @retval true  IMEI extracted successfully.
 * @retval false Timeout, command error, or parse failure.
 */
bool ATCore_get_imei(char *out, uint16_t cap) {
  if (!out || cap < 16) return false;

  atcmd_desc_t cmd = ATCMD_DESC_DEFAULT;
  cmd.id = CMD_AT_CGSN;
  cmd.cmd_mode = AT_CMD_EXEC;
  if (!ATCore_send_cmd(&cmd)) return false;

  uint32_t t0 = HAL_GetTick();
  while (!ATCore_is_response_ready()) {
    if ((HAL_GetTick() - t0) > 3000) return false;
  }
  ATCore_process_response();

  char resp[BG95_RX_BUFFER_SIZE];
  uint16_t resp_size;
  if (!ATCore_get_last_response(resp, sizeof(resp), &resp_size)) return false;

  /* Find 15 consecutive ASCII digits — the IMEI */
  for (uint16_t i = 0; i + 14 < resp_size; i++) {
    bool all_digits = true;
    for (uint8_t j = 0; j < 15; j++) {
      if (resp[i + j] < '0' || resp[i + j] > '9') {
        all_digits = false;
        break;
      }
    }
    if (all_digits) {
      memcpy(out, resp + i, 15);
      out[15] = '\0';
      return true;
    }
  }
  return false;
}

/* ---------------------- Private functions definition ---------------------- */
