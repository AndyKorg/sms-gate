#include "sim900d_pdu.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---

// Перевод hex-символов в байт
static uint8_t hex_to_byte(const char *hex) {
    char buf[3] = { hex[0], hex[1], 0 };
    return (uint8_t)strtoul(buf, NULL, 16);
}

// Декодировка 7-bit packed GSM в ASCII
static int gsm7_unpack(const uint8_t *data, int len, char *out) {
    int out_len = 0;
    int carry = 0, carry_bits = 0;
    for (int i = 0; i < len; i++) {
        uint8_t current = data[i];
        out[out_len++] = ((current << carry_bits) & 0x7F) | carry;
        carry = current >> (7 - carry_bits);
        carry_bits++;
        if (carry_bits == 7) {
            out[out_len++] = carry;
            carry = 0;
            carry_bits = 0;
        }
    }
    out[out_len] = 0;
    return out_len;
}

// Перевод semi-octet в строку номера
static void decode_number(const uint8_t *src, int len, char *out) {
    int j = 0;
    for (int i = 0; i < len; i++) {
        uint8_t b = src[i];
        out[j++] = (b & 0x0F) + '0';
        if ((b >> 4) != 0x0F)
            out[j++] = (b >> 4) + '0';
    }
    out[j] = 0;
}

// Декодировка времени
static void decode_timestamp(const uint8_t *src, char *out) {
    for (int i = 0; i < 7; ++i) {
        out[i*2]   = (src[i] & 0x0F) + '0';
        out[i*2+1] = ((src[i] >> 4) & 0x0F) + '0';
    }
    out[14] = 0;
}

// --- ГЛАВНАЯ ФУНКЦИЯ ---

bool sim900d_decode_pdu(const char *pdu_hex, sim900d_pdu_decoded_t *out) {
    uint8_t buffer[512];
    size_t len = strlen(pdu_hex) / 2;
    if (len > sizeof(buffer)) return false;

    for (size_t i = 0; i < len; i++) {
        buffer[i] = hex_to_byte(pdu_hex + i * 2);
    }

    size_t idx = 0;
    uint8_t smsc_len = buffer[idx++];
    idx += smsc_len; // пропустить SMSC

    uint8_t pdu_type = buffer[idx++];
    bool udhi = pdu_type & 0x40;

    uint8_t sender_len = buffer[idx++];
    uint8_t sender_type = buffer[idx++];
    int sender_bytes = (sender_len + 1) / 2;
    decode_number(buffer + idx, sender_bytes, out->sender);
    idx += sender_bytes;

    idx++; // PID
    uint8_t dcs = buffer[idx++];

    decode_timestamp(buffer + idx, out->timestamp);
    idx += 7;

    uint8_t user_data_len = buffer[idx++];

    const uint8_t *user_data = buffer + idx;
    int skip = 0;

    out->is_concat = false;
    if (udhi) {
        uint8_t udhl = user_data[0];
        if (udhl >= 5 && user_data[1] == 0x00) { // 8-bit concat UDH
            out->is_concat = true;
            out->concat_ref = user_data[2];
            out->concat_total = user_data[3];
            out->concat_seq = user_data[4];
            skip = udhl + 1;
        }
    }

    // Декодируем текст
    if ((dcs & 0x0C) == 0x00) { // GSM 7-bit
        gsm7_unpack(user_data + skip, user_data_len - skip, out->text);
    } else {
        strncpy(out->text, "[UNSUPPORTED ENCODING]", sizeof(out->text));
    }

    return true;
}
