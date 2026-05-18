/**
 * @file ATCom.c
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief Glue de inicializacion y storage compartido entre los sub-modulos
 *        de ATCom. La logica real vive en atcom_udp.c, atcom_registration.c
 *        y atcom_session.c.
 *
 * @version 0.2
 * @date 2026-05-14
 */

#include "ATCom.h"

#include "atcom_internal.h"
#include "atcom_udp.h"

/* Seconds until next wake-up as dictated by HES. 0 means "no pending value".
 * Escrito por registration y session, leido por main loop. */
static uint32_t pending_wake_seconds = 0;

void Com_Init(void) {
  atcom_udp_init();
  atcom_registration_init();
  atcom_session_init();
}

void atcom_set_pending_wake_seconds(uint32_t s) {
  pending_wake_seconds = s;
}

uint32_t Com_pop_pending_wake_seconds(void) {
  uint32_t v = pending_wake_seconds;
  pending_wake_seconds = 0;
  return v;
}
