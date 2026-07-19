/**
 * @file at_net.h
 * @ingroup atcore
 * @brief Network/protocol constants: HES message types, BG95 context/connect
 *        ids and the HES server endpoint.
 *
 * @note The HES_MSG_TYPE_* values mirror the MSG_TYPE_* defines in
 *       atcom_internal.h; they are kept here for the network layer.
 *
 * @version 0.2
 * @date 2025-10-28
 */

#ifndef _AT_NET_H_
#define _AT_NET_H_

#define HES_PROTOCOL_VERSION               0x00

/* HES protocol message types (see atcom_internal.h and the protocol spec). */
#define HES_MSG_TYPE_HANDSHAKE             0x00
#define HES_MSG_TYPE_HANDSHAKE_RESPONSE    0x01
#define HES_MSG_TYPE_REGISTER_REQUEST      0x02
#define HES_MSG_TYPE_REGISTER_RESPONSE     0x03
#define HES_MSG_TYPE_READ_REQUEST          0x0A
#define HES_MSG_TYPE_READ_RESPONSE         0x0B
#define HES_MSG_TYPE_WRITE_REQUEST         0x14
#define HES_MSG_TYPE_WRITE_RESPONSE        0x15
#define HES_MSG_TYPE_EXECUTE_REQUEST       0x1E
#define HES_MSG_TYPE_EXECUTE_RESPONSE      0x1F
#define HES_MSG_TYPE_ACTION_REQUEST        0x28
#define HES_MSG_TYPE_ACTION_RESPONSE       0x29
#define HES_MSG_TYPE_ACK                   0xFF

/* BG95 connection parameters. */
#define BG95_CONTEXT_ID 1            /* PDP context id (AT+QIACT). */
#define BG95_CONNECT_ID 2            /* Socket/connect id (AT+QIOPEN). */
#define BG95_BACKDOOR_PORT 6565      /* HES UDP port. */
#define BG95_ACCESS_MODE 0           /* QIOPEN access mode: buffer access. */
#define BG95_QICLOSE_DFLT_TIMEOUT 10 /* Default AT+QICLOSE timeout (s). */
#define BG95_QISTATE_DFLT_QUERY 0    /* Default AT+QISTATE query type. */

/* HES server endpoint used to open the UDP socket. */
static const char BG95_QIOPEN_STR_PARAMS[] = "UDP%mechardo3d.mooo.com";

#endif //_AT_NET_H_
