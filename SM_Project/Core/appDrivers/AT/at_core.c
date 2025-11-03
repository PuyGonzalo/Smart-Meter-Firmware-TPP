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
    {CMD_AT, "", 2, fCmdBuild_NoParams,
     NULL}, /*Ultimo parametro en realidad es fRspAnalyze_None: chequear si me
               sirven*/

    /* GENERAL MODEM commands */
    {CMD_AT_CGMI, "+GMI", 4, fCmdBuild_NoParams},
    {CMD_AT_CGMM, "+CGMM", 5, fCmdBuild_NoParams},
    {CMD_AT_CGMR, "+CGMR", 5, fCmdBuild_NoParams},
    {CMD_AT_CGSN, "+CGSN", 5, NULL},
    {CMD_AT_GSN, "+GSN", 4, fCmdBuild_NoParams},
    {CMD_AT_CIMI, "+CIMI", 5, fCmdBuild_NoParams},
    {CMD_AT_CEER, "+CEER", 5, fCmdBuild_NoParams},
    {CMD_AT_CMEE, "+CMEE", 5, NULL},
    {CMD_AT_CPIN, "+CPIN", 5, NULL},
    {CMD_AT_CFUN, "+CFUN", 5, NULL},
    {CMD_AT_COPS, "+COPS", 5, NULL},
    {CMD_AT_CNUM, "+CNUM", 5, fCmdBuild_NoParams},
    {CMD_AT_CGATT, "+CGATT", 6, NULL},
    {CMD_AT_CGPADDR, "+CGPADDR", 8, NULL},
    {CMD_AT_CEREG, "+CEREG", 6, NULL},
    {CMD_AT_CREG, "+CREG", 5, NULL},
    {CMD_AT_CGREG, "+CGREG", 6, NULL},
    {CMD_AT_CSQ, "+CSQ", 4, fCmdBuild_NoParams},
    {CMD_AT_CGDCONT, "+CGDCONT", 8, NULL},
    {CMD_AT_CGACT, "+CGACT", 6, NULL},
    {CMD_AT_CGDATA, "+CGDATA", 7, NULL},
    {CMD_AT_CGEREP, "+CGEREP", 7, NULL},
    {CMD_AT_CGEV, "+CGEV", 5, fCmdBuild_NoParams},
    {CMD_ATD, "D", 1, NULL},
    {CMD_ATE0, "E0", 2, fCmdBuild_NoParams},
    {CMD_ATE0, "E1", 2, fCmdBuild_NoParams},
    {CMD_ATH, "H", 1, fCmdBuild_NoParams},
    {CMD_ATO, "O", 1, fCmdBuild_NoParams},
    {CMD_ATV, "V", 1, NULL},
    {CMD_ATX, "X", 1, NULL},
    {CMD_AT_IPR, "+IPR", 4, NULL},
    {CMD_AT_IFC, "+IFC", 4, NULL},
    {CMD_AT_AND_W, "&W", 2, fCmdBuild_NoParams},
    {CMD_AT_AND_D, "&D", 2, NULL},

    /* TCP (IP) */
    {CMD_AT_QICSGP, "+QICSGP", 7, NULL},
    {CMD_AT_QIACT, "+QIACT", 6, NULL},
    {CMD_AT_QIDEACT, "+QIDEACT", 8, NULL},
    {CMD_AT_QIOPEN, "+QIOPEN", 7, NULL},
    {CMD_AT_QICLOSE, "+QICLOSE", 8, NULL},
    {CMD_AT_QISTATE, "+QISTATE", 8, NULL},
    {CMD_AT_QIRD, "+QIRD", 5, NULL},
    {CMD_AT_QISENDEX, "+QISENDEX", 9, NULL},
    {CMD_AT_QISDE, "+QISDE", 6, NULL},

    /* Other */
    {CMD_AT_QICFG, "+QICFG", 6, NULL},

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
 * @param cmd
 * @retval true
 * @retval false
 */
bool ATCore_send_cmd(atcmd_desc_t *cmd) {
  uint8_t ret;
  bool result;

  if (cmd->id >= SIZE_ATCMD_BG95_LUT) return false;

  char command[256];
  ATCMD_BG95_LUT[cmd->id].build(cmd);

  ret = AT_DRV->send_command(AT_DEVICE, command, cmd->at_cmd_size);

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
char *ATCore_get_last_response() { return AT_DRV->get_response(AT_DEVICE); }

/* ---------------------- Private functions definition ---------------------- */
