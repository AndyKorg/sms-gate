#ifndef SIM900D_PDU_H
#define SIM900D_PDU_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Структура результата декодирования PDU
typedef struct {
    char sender[32];
    char timestamp[32];
    char text[512]; // UTF-8
    bool is_concat;
    uint8_t concat_ref;
    uint8_t concat_total;
    uint8_t concat_seq;
} sim900d_pdu_decoded_t;

// Декодирование PDU-строки (hex) в структуру
bool sim900d_decode_pdu(const char *pdu_hex, sim900d_pdu_decoded_t *out);

#endif // SIM900D_PDU_H