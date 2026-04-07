/**
 * @file rlp.h
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief RLP (Recursive Length Prefix) encoder/decoder for protocol payloads.
 *
 * Implements standard Ethereum RLP encoding for compact serialization of
 * OBIS-based payloads over UDP. All operations use caller-provided buffers
 * (no dynamic allocation).
 *
 * RLP encoding rules:
 *  - Single byte [0x00..0x7f]: encoded as-is
 *  - Byte string 0-55 bytes:   [0x80 + len] + data
 *  - Byte string 56+ bytes:    [0xb7 + len_of_len] + len_bytes + data
 *  - List content 0-55 bytes:  [0xc0 + content_len] + items
 *  - List content 56+ bytes:   [0xf7 + len_of_len] + len_bytes + items
 *  - Integers: big-endian, no leading zeros. Zero = empty string (0x80).
 *
 * @version 0.1
 * @date 2026-04-06
 *
 * @copyright Copyright (c) 2026
 */

#ifndef _RLP_H_
#define _RLP_H_

#include <stdbool.h>
#include <stdint.h>

/* Max bytes reserved for a list header (covers content up to 65535 bytes) */
#define RLP_LIST_HEADER_RESERVE 3

/* ========================== Writer (encoder) ============================= */

typedef struct {
    uint8_t *buf;       /**< Output buffer */
    uint16_t cap;       /**< Buffer capacity */
    uint16_t pos;       /**< Current write position */
    bool overflow;      /**< Set if buffer capacity exceeded */
} rlp_writer_t;

/**
 * @brief Initialize writer with a caller-provided buffer.
 */
void rlp_writer_init(rlp_writer_t *w, uint8_t *buf, uint16_t capacity);

/**
 * @brief Get the number of bytes written so far.
 */
uint16_t rlp_writer_len(const rlp_writer_t *w);

/**
 * @brief Check if no overflow has occurred.
 */
bool rlp_writer_ok(const rlp_writer_t *w);

/**
 * @brief Encode a raw byte array as an RLP string.
 */
void rlp_encode_bytes(rlp_writer_t *w, const uint8_t *data, uint16_t len);

/**
 * @brief Encode a NUL-terminated C string as an RLP string.
 */
void rlp_encode_string(rlp_writer_t *w, const char *str);

/**
 * @brief Encode a uint8 as an RLP integer (big-endian, no leading zeros).
 */
void rlp_encode_uint8(rlp_writer_t *w, uint8_t val);

/**
 * @brief Encode a uint64 as an RLP integer (big-endian, no leading zeros).
 */
void rlp_encode_uint64(rlp_writer_t *w, uint64_t val);

/**
 * @brief Begin an RLP list. Returns a bookmark for rlp_list_end().
 *
 * Usage:
 * @code
 *   uint16_t bm = rlp_list_begin(&w);
 *   rlp_encode_uint8(&w, 0);       // op_code
 *   rlp_encode_string(&w, "1.0.1"); // item
 *   rlp_list_end(&w, bm);
 * @endcode
 */
uint16_t rlp_list_begin(rlp_writer_t *w);

/**
 * @brief End an RLP list opened with rlp_list_begin().
 * @param w     Writer context
 * @param bookmark Value returned by rlp_list_begin()
 */
void rlp_list_end(rlp_writer_t *w, uint16_t bookmark);

/* ========================== Reader (decoder) ============================= */

typedef struct {
    const uint8_t *buf; /**< Input buffer */
    uint16_t len;       /**< Buffer length */
    uint16_t pos;       /**< Current read position */
    bool error;         /**< Set on decode error */
} rlp_reader_t;

/**
 * @brief Initialize reader with an RLP-encoded buffer.
 */
void rlp_reader_init(rlp_reader_t *r, const uint8_t *buf, uint16_t len);

/**
 * @brief Check if no decode error has occurred.
 */
bool rlp_reader_ok(const rlp_reader_t *r);

/**
 * @brief Check if all data has been consumed.
 */
bool rlp_reader_done(const rlp_reader_t *r);

/**
 * @brief Decode next item as uint8.
 */
bool rlp_decode_uint8(rlp_reader_t *r, uint8_t *out);

/**
 * @brief Decode next item as uint64 (big-endian).
 */
bool rlp_decode_uint64(rlp_reader_t *r, uint64_t *out);

/**
 * @brief Decode next item as a NUL-terminated string.
 * @param out     Output buffer (must have room for string + NUL)
 * @param out_cap Capacity of output buffer
 * @param out_len If non-NULL, receives the string length (excluding NUL)
 */
bool rlp_decode_string(rlp_reader_t *r, char *out, uint16_t out_cap,
                        uint16_t *out_len);

/**
 * @brief Decode next item as raw bytes.
 * @param out     Output buffer
 * @param out_cap Capacity of output buffer
 * @param out_len If non-NULL, receives the number of bytes decoded
 */
bool rlp_decode_bytes(rlp_reader_t *r, uint8_t *out, uint16_t out_cap,
                       uint16_t *out_len);

/**
 * @brief Enter an RLP list, creating a sub-reader bounded to its content.
 *
 * The parent reader advances past the entire list. The sub-reader can be
 * used to decode items within the list.
 *
 * @param r            Parent reader (advanced past the list)
 * @param list_reader  Sub-reader initialized to the list content
 */
bool rlp_enter_list(rlp_reader_t *r, rlp_reader_t *list_reader);

#endif /* _RLP_H_ */
