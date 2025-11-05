#ifndef _BG95_AT_CMD_LIB_H_
#define _BG95_AT_CMD_LIB_H_

#define BG95_COPS_CMD_SIZE 6 /* 180 sec */
#define BG95_CGATT_CMD_SIZE 6
#define BG95_CGACT_CMD_SIZE 6
#define BG95_ATH_CMD_SIZE 6
#define BG95_AT_CMD_SIZE 6
#define BG95_QNWINFO_CMD_SIZE 6
#define BG95_CPSMS_CMD_SIZE 6
#define BG95_CEDRX_CMD_SIZE 6

#if (USE_SOCKETS_TYPE == USE_SOCKETS_MODEM)
#define BG95_QIOPEN_CMD_SIZE 1
#define BG95_QICLOSE_CMD_SIZE 15
#define BG95_QIACT_CMD_SIZE 15
#define BG95_QIDEACT_CMD_SIZE 4
#define BG95_QPING_CMD_SIZE 1
#define BG95_QIDNSGIP_CMD_SIZE 6
#endif /* (USE_SOCKETS_TYPE == USE_SOCKETS_MODEM) */

#define BG95_MODEM_SYNCHRO_AT_MAX_RETRIES ((uint8_t)30U)
#define BG95_MAX_SIM_STATUS_RETRIES                                         \
  ((uint8_t)20U) /* maximum number of AT+QINISTAT retries to wait SIM ready \
                  * multiply by BG95_SIMREADY_CMD_SIZE to compute global    \
                  * timeout value                                           \
                  */

enum {
  /* standard commands */
  CMD_AT = 0, /* empty command or empty answer */

  /* 3GPP TS 27.007 and GSM 07.07 commands */
  CMD_AT_CGMI,  /* Request manufacturer identification */
  CMD_AT_CGMM,  /* Model identification */
  CMD_AT_CGMR,  /* Revision identification */
  CMD_AT_CGSN,  /* Product serial number identification */
  CMD_AT_CIMI,  /* IMSI */
  CMD_AT_CEER,  /* Extended error report */
  CMD_AT_CMEE,  /* Report mobile equipment error */
  CMD_AT_CPIN,  /* Enter PIN */
  CMD_AT_CFUN,  /* Set phone functionality */
  CMD_AT_COPS,  /* Operator selection */
  CMD_AT_CNUM,  /* Subscriber number */
  CMD_AT_CGATT, /* PS attach or detach */
  CMD_AT_CREG,  /* Network registration: enable or disable +CREG urc */
  CMD_AT_CGREG, /* GPRS network registation status: enable or disable +CGREG urc
                 */
  CMD_AT_CEREG, /* EPS network registration status: enable or disable +CEREG urc
                 */
  CMD_AT_CGEREP,  /* Packet domain event reporting: enable or disable +CGEV urc
                   */
  CMD_AT_CGEV,    /* EPS bearer indication status */
  CMD_AT_CSQ,     /* Signal quality */
  CMD_AT_CGDCONT, /* Define PDP context */
  CMD_AT_CGACT,   /* PDP context activate or deactivate */
  CMD_AT_CGDATA,  /* Enter Data state */
  CMD_AT_CGPADDR, /* Show PDP Address */

  /* V.25TER commands */
  CMD_ATD,  /* Dial */
  CMD_ATE0, /* Command Echo deactivated*/
  CMD_ATE1, /* Command Echo activated*/
  CMD_ATH,  /* Hook control (disconnect existing connection) */
  CMD_ATO,  /* Return to online data state (switch from COMMAND to DATA mode) */
  CMD_ATV,  /* DCE response format */
  CMD_AT_AND_W, /* Store current Parameters to User defined profile */
  CMD_AT_AND_D, /* Set DTR function mode */
  CMD_ATX,      /* CONNECT Result code and monitor call progress */
  CMD_AT_IPR,   /* Fixed DTE rate */
  CMD_AT_IFC,   /* set DTE-DCE local flow control */

  /* TCP (IP) commands */
  CMD_AT_QICSGP,
  CMD_AT_QIACT,
  CMD_AT_QIDEACT,
  CMD_AT_QIOPEN,
  CMD_AT_QICLOSE,
  CMD_AT_QISTATE,
  CMD_AT_QIRD,
  CMD_AT_QISENDEX,

  /* Other commands */
  CMD_AT_QICFG,
};

static const char AT_RESPONSE_OK[] = "\r\nOK\r\n";
static const char AT_RESPONSE_SEND_OK[] = "\r\nSEND OK\r\n";
static const char AT_RESPONSE_ERR[] = "\r\nERROR\r\n";
static const char AT_RESPONSE_ERR_CME[] = "\r\n+CME ERROR:";
static const char AT_RESPONSE_QI[] = "+QI";

#endif  // _BG95_AT_CMD_LIB_H_