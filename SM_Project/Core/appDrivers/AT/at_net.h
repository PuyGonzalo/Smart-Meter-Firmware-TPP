/**
 * @file at_net.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-10-28
 * 
 * @copyright Copyright (c) 2025
 * 
*/

#ifndef _AT_NET_H_
#define _AT_NET_H_

#define HES_PROTOCOL_VERSION               0x00 /** */

#define HES_MSG_TYPE_HANDSHAKE             0x00 /** */
#define HES_MSG_TYPE_HANDSHAKE_RESPONSE    0x01 /** */
#define HES_MSG_TYPE_REGISTER_REQUEST      0x02 /** */
#define HES_MSG_TYPE_REGISTER_RESPONSE     0x03 /** */
#define HES_MSG_TYPE_READ_REQUEST          0x0A /** */
#define HES_MSG_TYPE_READ_RESPONSE         0x0B /** */
#define HES_MSG_TYPE_WRITE_REQUEST         0x14 /** */
#define HES_MSG_TYPE_WRITE_RESPONSE        0x15 /** */
#define HES_MSG_TYPE_EXECUTE_REQUEST       0x1E /** */
#define HES_MSG_TYPE_EXECUTE_RESPONSE      0x1F /** */
#define HES_MSG_TYPE_ACTION_REQUEST        0x28 /** */
#define HES_MSG_TYPE_ACTION_RESPONSE       0x29 /** */
#define HES_MSG_TYPE_ACK                   0xFF /** */

#define BG95_CONTEXT_ID 1
#define BG95_CONNECT_ID 2
#define BG95_BACKDOOR_PORT 6565
#define BG95_ACCESS_MODE 0
#define BG95_QICLOSE_DFLT_TIMEOUT 10
#define BG95_QISTATE_DFLT_QUERY 0

static const char BG95_QIOPEN_STR_PARAMS[] = "UDP%mechardo3d.mooo.com";

#endif //_AT_NET_H_