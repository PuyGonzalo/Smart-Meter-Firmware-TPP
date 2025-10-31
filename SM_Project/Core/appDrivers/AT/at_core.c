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
    {CMD_AT, "", BG95_AT_TIMEOUT, fCmdBuild_NoParams,
     NULL}, /*Ultimo parametro en realidad es fRspAnalyze_None: chequear si me
               sirven*/

    /* GENERAL MODEM commands */
    {CMD_AT_CGMI, "+GMI", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_AT_CGMM, "+CGMM", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_AT_CGMR, "+CGMR", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_AT_CGSN, "+CGSN", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_GSN, "+GSN", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_AT_CIMI, "+CIMI", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_AT_CEER, "+CEER", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_AT_CMEE, "+CMEE", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_CPIN, "+CPIN", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_CFUN, "+CFUN", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_COPS, "+COPS", BG95_COPS_TIMEOUT, NULL, NULL},
    {CMD_AT_CNUM, "+CNUM", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_AT_CGATT, "+CGATT", BG95_CGATT_TIMEOUT, NULL, NULL},
    {CMD_AT_CGPADDR, "+CGPADDR", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_CEREG, "+CEREG", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_CREG, "+CREG", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_CGREG, "+CGREG", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_CSQ, "+CSQ", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_AT_CGDCONT, "+CGDCONT", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_CGACT, "+CGACT", BG95_CGACT_TIMEOUT, NULL, NULL},
    {CMD_AT_CGDATA, "+CGDATA", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_CGEREP, "+CGEREP", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_CGEV, "+CGEV", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_ATD, "D", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_ATE0, "E0", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_ATE0, "E1", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_ATH, "H", BG95_ATH_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_ATO, "O", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_ATV, "V", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_ATX, "X", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_IPR, "+IPR", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_IFC, "+IFC", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_AND_W, "&W", BG95_DEFAULT_TIMEOUT, fCmdBuild_NoParams, NULL},
    {CMD_AT_AND_D, "&D", BG95_DEFAULT_TIMEOUT, NULL, NULL},

    /* TCP (IP) */
    {CMD_AT_QICSGP, "+QICSGP", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_QIACT, "+QIACT", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_QIDEACT, "+QIDEACT", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_QIOPEN, "+QIOPEN", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_QICLOSE, "+QICLOSE", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_QISTATE, "+QISTATE", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_QIRD, "+QIRD", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_QISENDEX, "+QISENDEX", BG95_DEFAULT_TIMEOUT, NULL, NULL},
    {CMD_AT_QISDE, "+QISDE", BG95_DEFAULT_TIMEOUT, NULL, NULL},

    /* Other */
    {CMD_AT_QICFG, "+QICFG", BG95_DEFAULT_TIMEOUT, NULL, NULL},

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
 * @param cmd
 * @return true
 * @return false
 */
bool ATCore_send_cmd(atcmd_desc_t *cmd) {
  uint8_t ret;
  bool result;

  if (cmd->id >= SIZE_ATCMD_BG95_LUT) return false;

  char command[cmd->at_cmd_size];
  ATCMD_BG95_LUT[cmd->id].build(command, ATCMD_BG95_LUT[cmd->id].cmd_string,
                                cmd);

  ret = AT_DRV->send_command(AT_DEVICE, command, cmd->at_cmd_size);

  result = (ret == 0);

  return result;
}

/**
 * @brief
 *
 * @return true
 * @return false
 */
bool ATCore_is_response_ready() { return AT_DEVICE->responseReady; }

/**
 * @brief
 *
 * @return char*
 */
char *ATCore_get_last_response() { return AT_DRV->get_response(AT_DEVICE); }

/* ---------------------- Private functions definition ---------------------- */
