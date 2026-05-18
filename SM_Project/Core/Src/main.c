/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "usart.h"
#include "rtc.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

#include "ATCom.h"
#include "delay.h"
#include "printf_retarget.h"
#include "pulse_counter.h"
#include "storage.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define REGISTRATION_TIMEOUT_MS  720000  /* Max time for device registration (12 min: covers QIACT 150s + QIOPEN 150s with retries) */
#define SESSION_PERIOD_SEC       60      /* Interval between HES sessions */

/* Uncomment to wipe EEPROM on boot (forces re-registration) */
#define DEBUG_ERASE_EEPROM

/* Tests — descomentar UNO a la vez (o ninguno para flujo normal) */
// #define TEST_PWRKEY_STATUS
//#define TEST_RTC_WAKEUP
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/*Pequeña pruebita*/
int32_t at_command_delay;
extern UART_HandleTypeDef huart2;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#if defined(TEST_PWRKEY_STATUS) || defined(TEST_RTC_WAKEUP)
static inline bool read_status(void) {
  return HAL_GPIO_ReadPin(QUECTEL_STATUS_GPIO_Port,
                          QUECTEL_STATUS_Pin) == GPIO_PIN_SET;
}
#endif

#ifdef TEST_PWRKEY_STATUS
static void run_pwrkey_status_test(void) {
  printf("\r\n=== PWRKEY + STATUS TEST ===\r\n");

  bool s0 = read_status();
  printf("[1] STATUS inicial: %d  (esperado: 0)\r\n", s0);

  printf("[2] Power ON...\r\n");
  uint32_t t0 = HAL_GetTick();
  bool on_ok = ATCore_power_on();
  uint32_t on_ms = HAL_GetTick() - t0;
  printf("    ATCore_power_on = %d  (%lu ms)\r\n", on_ok, on_ms);

  bool s1 = read_status();
  printf("[3] STATUS post power_on: %d  (esperado: 1)\r\n", s1);

  HAL_Delay(3000);

  printf("[4] Power OFF (PWRKEY pulse + polling STATUS)...\r\n");
  t0 = HAL_GetTick();
  bool off_ok = ATCore_power_off();
  uint32_t off_ms = HAL_GetTick() - t0;
  printf("    ATCore_power_off = %d  (%lu ms)\r\n", off_ok, off_ms);
  /* off_ok == 1 ya implica que STATUS fue a LOW dentro del timeout */

  bool pass = !s0 && on_ok && s1 && off_ok;
  printf("=== RESULT: %s ===\r\n\r\n", pass ? "PASS" : "FAIL");

  while (1) { HAL_Delay(1000); }
}
#endif

#ifdef TEST_RTC_WAKEUP
#define TEST_RTC_PERIOD_SEC  30   /* mas corto que SESSION_PERIOD_SEC para iterar rapido */
#define TEST_RTC_CYCLES      5    /* cantidad de ciclos antes de trap */

static void run_rtc_wakeup_test(void) {
  printf("\r\n=== RTC WAKEUP TEST ===\r\n");
  printf("Ciclo: power_on -> (simula sesion 2s) -> power_off -> alarm(%ds)\r\n",
         TEST_RTC_PERIOD_SEC);
  printf("Se ejecutan %d ciclos. HSI mide el elapsed; compara con LSE.\r\n\r\n",
         TEST_RTC_CYCLES);

  for (uint32_t cycle = 1; cycle <= TEST_RTC_CYCLES; cycle++) {
    printf("--- Ciclo %lu/%d ---\r\n", cycle, TEST_RTC_CYCLES);

    uint32_t t0 = HAL_GetTick();
    bool on_ok = ATCore_power_on();
    printf("  [t=%lums] power_on=%d  STATUS=%d  (took %lums)\r\n",
           HAL_GetTick(), on_ok, read_status(), HAL_GetTick() - t0);

    /* Aca iria Com_session_*() en produccion — saltado en este test */
    HAL_Delay(2000);

    t0 = HAL_GetTick();
    bool off_ok = ATCore_power_off();
    printf("  [t=%lums] power_off=%d  (took %lums)\r\n",
           HAL_GetTick(), off_ok, HAL_GetTick() - t0);

    printf("  [t=%lums] Armando alarma RTC por %ds...\r\n",
           HAL_GetTick(), TEST_RTC_PERIOD_SEC);
    uint32_t t_arm = HAL_GetTick();
    RTC_arm_alarm(TEST_RTC_PERIOD_SEC);
    while (!RTC_alarm_fired()) { /* busy-wait */ }
    uint32_t elapsed = HAL_GetTick() - t_arm;
    RTC_clear_alarm_flag();

    int32_t drift_ms = (int32_t)elapsed - (TEST_RTC_PERIOD_SEC * 1000);
    printf("  [t=%lums] ALARM FIRED! elapsed=%lums drift=%+ldms\r\n\r\n",
           HAL_GetTick(), elapsed, drift_ms);
  }

  printf("=== %d ciclos completados ===\r\n", TEST_RTC_CYCLES);
  while (1) { HAL_Delay(1000); }
}
#endif
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_LPUART1_UART_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
  delay_init();
  ATCore_init(&huart2);
  Com_Init();
  Storage_init();

#ifdef DEBUG_ERASE_EEPROM
  Storage_erase_all();

#endif

  /* Load credentials from EEPROM if device was previously registered */
  if (Storage_is_registered()) {
    uint8_t dev_id[DEV_ID_BYTES];
    uint8_t mac[MAC_BYTES];
    Storage_load_credentials(dev_id, mac);
    ATCore_set_device_id(dev_id);
    ATCore_set_device_mac(mac);
    PulseCounter_set_count(Storage_load_pulse_count());
  }

  /*Creo delay para comandos at*/
  at_command_delay = delay_timer_create();

#ifdef TEST_PWRKEY_STATUS
  run_pwrkey_status_test();  /* nunca retorna */
#endif
#ifdef TEST_RTC_WAKEUP
  run_rtc_wakeup_test();     /* nunca retorna */
#endif

  /* Power on modem and run registration if needed */
  ATCore_power_on();

  /* Fetch and persist IMEI on first boot (modem must be on) */
  if (!Storage_has_imei()) {
    char imei[STORAGE_IMEI_LEN];
    if (ATCore_get_imei(imei, sizeof(imei))) {
      Storage_save_imei(imei);
    }
  }

  if (!Storage_is_registered()) {
    if (!Com_register_device_blocking(REGISTRATION_TIMEOUT_MS)) {
      NVIC_SystemReset();
    }
  }

  /* If HES provided a wake-up time during registration, honor it before the
   * first session: power off and sleep until the absolute moment HES asked. */
  uint32_t initial_wake = Com_pop_pending_wake_seconds();
  if (initial_wake > 0) {
    ATCore_power_off();
    RTC_arm_alarm(initial_wake);
    while (!RTC_alarm_fired()) { }
    RTC_clear_alarm_flag();
    ATCore_power_on();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Com_session_start();
    while (!Com_is_session_done()) {
      Com_session_process();
    }
    ATCore_power_off();

    /* Sesion fallida tras agotar reintentos (10 fallas consecutivas):
     * mismo criterio que registration — hard reset para empezar limpio. */
    if (Com_session_failed()) {
      NVIC_SystemReset();
    }

    /* HES dictates next wake-up if it sent one in this session's response;
     * fall back to local default cadence otherwise. */
    uint32_t next_wake = Com_pop_pending_wake_seconds();
    if (next_wake == 0) next_wake = SESSION_PERIOD_SEC;

    /* TODO(Phase 5.1): enter Stop mode here instead of busy-wait */
    RTC_arm_alarm(next_wake);
    while (!RTC_alarm_fired()) { }
    RTC_clear_alarm_flag();

    ATCore_power_on();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2|RCC_PERIPHCLK_LPUART1
                              |RCC_PERIPHCLK_RTC;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_PCLK1;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enables the Clock Security System
  */
  HAL_RCCEx_EnableLSECSS();
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
