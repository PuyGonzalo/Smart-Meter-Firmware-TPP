/**
 * @file rlp.c
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief RLP (Recursive Length Prefix) encoder/decoder implementation.
 * @version 0.1
 * @date 2026-04-06
 *
 * @copyright Copyright (c) 2026
 */

#include "rlp.h"
#include <string.h>

/* ========================= Internal helpers ============================== */

static void _write_byte(rlp_writer_t *w, uint8_t b) {
    if (w->overflow) return;
    if (w->pos >= w->cap) {
        w->overflow = true;
        return;
    }
    w->buf[w->pos++] = b;
}

static void _write_bytes(rlp_writer_t *w, const uint8_t *data, uint16_t len) {
    if (w->overflow || len == 0) return;
    if (w->pos + len > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->pos, data, len);
    w->pos += len;
}

/**
 * @brief Number of bytes needed to represent a u16 value in big-endian.
 */
static uint8_t _byte_count_u16(uint16_t val) {
    if (val <= 0xFF) return 1;
    return 2;
}

/**
 * @brief Write the RLP string/bytes header (length prefix).
 *
 * Called only when the single-byte shortcut does NOT apply (i.e., len != 1
 * or the byte value > 0x7f). The caller is responsible for that check.
 */
static void _encode_string_header(rlp_writer_t *w, uint16_t len) {
    if (len <= 55) {
        _write_byte(w, 0x80 + (uint8_t)len);
    } else {
        uint8_t bc = _byte_count_u16(len);
        _write_byte(w, 0xb7 + bc);
        if (bc == 2) _write_byte(w, (uint8_t)(len >> 8));
        _write_byte(w, (uint8_t)(len & 0xFF));
    }
}

/* ==================== Writer (encoder) public API ======================== */

void rlp_writer_init(rlp_writer_t *w, uint8_t *buf, uint16_t capacity) {
    w->buf = buf;
    w->cap = capacity;
    w->pos = 0;
    w->overflow = false;
}

uint16_t rlp_writer_len(const rlp_writer_t *w) {
    return w->pos;
}

bool rlp_writer_ok(const rlp_writer_t *w) {
    return !w->overflow;
}

void rlp_encode_bytes(rlp_writer_t *w, const uint8_t *data, uint16_t len) {
    if (len == 1 && data[0] <= 0x7f) {
        /* Single byte in [0x00..0x7f] is encoded as itself */
        _write_byte(w, data[0]);
    } else {
        _encode_string_header(w, len);
        _write_bytes(w, data, len);
    }
}

void rlp_encode_string(rlp_writer_t *w, const char *str) {
    uint16_t len = (uint16_t)strlen(str);
    rlp_encode_bytes(w, (const uint8_t *)str, len);
}

void rlp_encode_uint8(rlp_writer_t *w, uint8_t val) {
    if (val == 0) {
        _write_byte(w, 0x80);  /* Zero = empty string */
    } else {
        rlp_encode_bytes(w, &val, 1);
    }
}

void rlp_encode_uint64(rlp_writer_t *w, uint64_t val) {
    if (val == 0) {
        _write_byte(w, 0x80);
        return;
    }

    /* Determine number of significant bytes (big-endian, no leading zeros) */
    uint8_t be[8];
    uint8_t len = 0;
    uint64_t tmp = val;

    while (tmp > 0) {
        len++;
        tmp >>= 8;
    }

    /* Fill big-endian representation */
    for (uint8_t i = 0; i < len; i++) {
        be[i] = (uint8_t)(val >> ((len - 1 - i) * 8));
    }

    rlp_encode_bytes(w, be, len);
}

uint16_t rlp_list_begin(rlp_writer_t *w) {
    uint16_t bookmark = w->pos;
    /* Reserve max header space: 1 tag + 2 length bytes */
    w->pos += RLP_LIST_HEADER_RESERVE;
    if (w->pos > w->cap) {
        w->overflow = true;
    }
    return bookmark;
}

void rlp_list_end(rlp_writer_t *w, uint16_t bookmark) {
    if (w->overflow) return;

    uint16_t content_start = bookmark + RLP_LIST_HEADER_RESERVE;
    uint16_t content_len = w->pos - content_start;

    /* Build the actual list header */
    uint8_t header[3];
    uint8_t header_len;

    if (content_len <= 55) {
        header[0] = 0xc0 + (uint8_t)content_len;
        header_len = 1;
    } else if (content_len <= 255) {
        header[0] = 0xf8;
        header[1] = (uint8_t)content_len;
        header_len = 2;
    } else {
        header[0] = 0xf9;
        header[1] = (uint8_t)(content_len >> 8);
        header[2] = (uint8_t)(content_len & 0xFF);
        header_len = 3;
    }

    /* Close the gap between reserved space and actual header */
    uint8_t gap = RLP_LIST_HEADER_RESERVE - header_len;
    if (gap > 0 && content_len > 0) {
        memmove(w->buf + bookmark + header_len,
                w->buf + content_start,
                content_len);
    }
    w->pos -= gap;

    /* Write the header at the bookmark position */
    memcpy(w->buf + bookmark, header, header_len);
}

/* ===================== Reader internal helpers =========================== */

/**
 * @brief Parse the RLP prefix at current position.
 *
 * @param r           Reader context
 * @param is_list     Output: true if item is a list, false if string
 * @param data_offset Output: byte offset where data/content begins
 * @param data_len    Output: length of data (string) or content (list)
 * @retval true  Prefix parsed successfully
 * @retval false Error (reader error flag set)
 */
static bool _read_prefix(rlp_reader_t *r, bool *is_list,
                          uint16_t *data_offset, uint16_t *data_len) {
    if (r->pos >= r->len) {
        r->error = true;
        return false;
    }

    uint8_t prefix = r->buf[r->pos];

    if (prefix <= 0x7f) {
        /* Single byte value */
        *is_list = false;
        *data_offset = r->pos;
        *data_len = 1;
        return true;
    }

    if (prefix <= 0xb7) {
        /* Short string: [0x80 + len] + data */
        uint16_t len = prefix - 0x80;
        *is_list = false;
        *data_offset = r->pos + 1;
        *data_len = len;
        if (*data_offset + len > r->len) {
            r->error = true;
            return false;
        }
        return true;
    }

    if (prefix <= 0xbf) {
        /* Long string: [0xb7 + ll] + length_bytes + data */
        uint8_t ll = prefix - 0xb7;
        if (r->pos + 1 + ll > r->len) {
            r->error = true;
            return false;
        }
        uint16_t len = 0;
        for (uint8_t i = 0; i < ll; i++) {
            len = (len << 8) | r->buf[r->pos + 1 + i];
        }
        *is_list = false;
        *data_offset = r->pos + 1 + ll;
        *data_len = len;
        if (*data_offset + len > r->len) {
            r->error = true;
            return false;
        }
        return true;
    }

    if (prefix <= 0xf7) {
        /* Short list: [0xc0 + content_len] + items */
        uint16_t len = prefix - 0xc0;
        *is_list = true;
        *data_offset = r->pos + 1;
        *data_len = len;
        if (*data_offset + len > r->len) {
            r->error = true;
            return false;
        }
        return true;
    }

    /* Long list: [0xf7 + ll] + length_bytes + items */
    uint8_t ll = prefix - 0xf7;
    if (r->pos + 1 + ll > r->len) {
        r->error = true;
        return false;
    }
    uint16_t len = 0;
    for (uint8_t i = 0; i < ll; i++) {
        len = (len << 8) | r->buf[r->pos + 1 + i];
    }
    *is_list = true;
    *data_offset = r->pos + 1 + ll;
    *data_len = len;
    if (*data_offset + len > r->len) {
        r->error = true;
        return false;
    }
    return true;
}

/* ==================== Reader (decoder) public API ======================== */

void rlp_reader_init(rlp_reader_t *r, const uint8_t *buf, uint16_t len) {
    r->buf = buf;
    r->len = len;
    r->pos = 0;
    r->error = false;
}

bool rlp_reader_ok(const rlp_reader_t *r) {
    return !r->error;
}

bool rlp_reader_done(const rlp_reader_t *r) {
    return r->pos >= r->len;
}

bool rlp_decode_uint8(rlp_reader_t *r, uint8_t *out) {
    if (r->error) return false;

    bool is_list;
    uint16_t offset, len;
    if (!_read_prefix(r, &is_list, &offset, &len)) return false;
    if (is_list) { r->error = true; return false; }

    if (len == 0) {
        *out = 0;
    } else if (len == 1) {
        *out = r->buf[offset];
    } else {
        r->error = true;
        return false;
    }

    r->pos = offset + len;
    return true;
}

bool rlp_decode_uint64(rlp_reader_t *r, uint64_t *out) {
    if (r->error) return false;

    bool is_list;
    uint16_t offset, len;
    if (!_read_prefix(r, &is_list, &offset, &len)) return false;
    if (is_list) { r->error = true; return false; }

    if (len == 0) {
        *out = 0;
    } else if (len <= 8) {
        *out = 0;
        for (uint16_t i = 0; i < len; i++) {
            *out = (*out << 8) | r->buf[offset + i];
        }
    } else {
        r->error = true;
        return false;
    }

    r->pos = offset + len;
    return true;
}

bool rlp_decode_string(rlp_reader_t *r, char *out, uint16_t out_cap,
                        uint16_t *out_len) {
    if (r->error) return false;

    bool is_list;
    uint16_t offset, len;
    if (!_read_prefix(r, &is_list, &offset, &len)) return false;
    if (is_list) { r->error = true; return false; }
    if (len >= out_cap) { r->error = true; return false; }

    memcpy(out, r->buf + offset, len);
    out[len] = '\0';
    if (out_len) *out_len = len;

    r->pos = offset + len;
    return true;
}

bool rlp_decode_bytes(rlp_reader_t *r, uint8_t *out, uint16_t out_cap,
                       uint16_t *out_len) {
    if (r->error) return false;

    bool is_list;
    uint16_t offset, len;
    if (!_read_prefix(r, &is_list, &offset, &len)) return false;
    if (is_list) { r->error = true; return false; }
    if (len > out_cap) { r->error = true; return false; }

    memcpy(out, r->buf + offset, len);
    if (out_len) *out_len = len;

    r->pos = offset + len;
    return true;
}

bool rlp_enter_list(rlp_reader_t *r, rlp_reader_t *list_reader) {
    if (r->error) return false;

    bool is_list;
    uint16_t offset, len;
    if (!_read_prefix(r, &is_list, &offset, &len)) return false;
    if (!is_list) { r->error = true; return false; }

    /* Sub-reader bounded to list content */
    rlp_reader_init(list_reader, r->buf + offset, len);

    /* Advance parent past the entire list */
    r->pos = offset + len;
    return true;
}
