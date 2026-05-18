/**
 * @file ATCom.h
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief API publica del modulo ATCom — orquesta registro y sesiones
 *        periodicas contra el HES via BG95.
 *
 * Los detalles de las FSM internas (registration / UDP / session) viven en
 * atcom_registration.h, atcom_udp.h y atcom_session.h respectivamente.
 *
 * @version 0.2
 * @date 2026-05-14
 */

#ifndef _ATCOM_H_
#define _ATCOM_H_

#include <stdbool.h>
#include <stdint.h>

#include "at_core.h"  /* re-export para mantener transitive includes en main.c */
#include "atcom_registration.h"
#include "atcom_session.h"

/**
 * @brief Inicializa los timers internos de las tres FSM (registration,
 *        UDP context y session). Llamar una vez en boot.
 */
void Com_Init(void);

/**
 * @brief Pop the wake-up delay (seconds) requested by HES, then clear it.
 * @return Delay in seconds, or 0 if HES has not provided a value.
 */
uint32_t Com_pop_pending_wake_seconds(void);

#endif /* _ATCOM_H_ */
