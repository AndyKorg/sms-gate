#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim900d_pdu.h"

static const char *TAG = "SIM900_PDU";

// Вспомогательная функция: конвертация hex-символа в число
static int hex_char_to_int(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

// Вспомогательная функция: конвертация 2 hex-символов в байт
static uint8_t hex_to_byte(const char *hex) {
  int high = hex_char_to_int(hex[0]);
  int low = hex_char_to_int(hex[1]);
  if (high < 0 || low < 0)
    return 0;
  return (high << 4) | low;
}

// Вспомогательная функция: декодирование BCD номера телефона
static void decode_phone_number(const char *hex_digits, int digit_count, char *output) {
  char temp[31] = {0}; // Уменьшаем размер для учета символа '+'
  int temp_pos = 0;
  int hex_len = strlen(hex_digits);

  // Декодируем BCD (меняем местами nibbles в каждом байте)
  for (int i = 0; i < hex_len && temp_pos < 30; i += 2) {
    if (i + 1 < hex_len) {
      char low_nibble = hex_digits[i + 1]; // Младший полубайт
      char high_nibble = hex_digits[i];    // Старший полубайт

      // Добавляем младший полубайт первым
      if (low_nibble != 'F' && low_nibble != 'f' && temp_pos < 30) {
        temp[temp_pos++] = low_nibble;
      }
      // Добавляем старший полубайт вторым
      if (high_nibble != 'F' && high_nibble != 'f' && temp_pos < 30) {
        temp[temp_pos++] = high_nibble;
      }
    }
  }

  // Обрезаем до нужной длины цифр
  if (digit_count > 0 && digit_count < temp_pos) {
    temp[digit_count] = '\0';
  }

  // Добавляем + в начало для международных номеров
  output[0] = '+';
  strncpy(output + 1, temp, 30);
  output[31] = '\0';
}

// Вспомогательная функция: декодирование timestamp
static void decode_timestamp(const char *hex_timestamp, char *output) {
  char parts[7][3];

  // Декодируем каждую пару BCD
  for (int i = 0; i < 7; i++) {
    char pair[3];
    pair[0] = hex_timestamp[i * 2 + 1]; // Младший nibble
    pair[1] = hex_timestamp[i * 2];     // Старший nibble
    pair[2] = '\0';
    strcpy(parts[i], pair);
  }

  // Форматируем как DD.MM.YY HH:MM:SS
  snprintf(output, 32, "%s.%s.20%s %s:%s:%s", parts[2], parts[1], parts[0], // день.месяц.год
           parts[3], parts[4], parts[5]);                                   // час:минута:секунда
}

#define  GSM_7BITS_ESCAPE   0x1b

static const unsigned char gsm7bits_to_latin1[128] = {
  '@', 0xa3,  '$', 0xa5, 0xe8, 0xe9, 0xf9, 0xec, 0xf2, 0xc7, '\n', 0xd8, 0xf8, '\r', 0xc5, 0xe5,
    0,  '_',    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0xc6, 0xe6, 0xdf, 0xc9,
  ' ',  '!',  '"',  '#', 0xa4,  '%',  '&', '\'',  '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',
  '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',
 0xa1,  'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',
  'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',  'X',  'Y',  'Z', 0xc4, 0xd6, 0xd1, 0xdc, 0xa7,
 0xbf,  'a',  'b',  'c',  'd',  'e',  'f',  'g',  'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',
  'p',  'q',  'r',  's',  't',  'u',  'v',  'w',  'x',  'y',  'z', 0xe4, 0xf6, 0xf1, 0xfc, 0xe0,
};

static const unsigned char gsm7bits_extend_to_latin1[128] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,'\f',   0,   0,   0,   0,   0,
    0,   0,   0,   0, '^',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0, '{', '}',   0,   0,   0,   0,   0,'\\',
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, '[', '~', ']',   0,
  '|',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

static int G7bitToAscii(char* buffer, int buffer_length)
{
	uint8_t i;

	for (i = 0; i<buffer_length; i++) {
		if (buffer[i] < 128) {
			if (buffer[i] == GSM_7BITS_ESCAPE) {
				buffer[i] = gsm7bits_extend_to_latin1[(uint8_t) buffer[i + 1]];
				memmove(&buffer[i + 1], &buffer[i + 2], buffer_length - i - 1);
				buffer_length--;
			} else {
				buffer[i] = gsm7bits_to_latin1[(uint8_t) buffer[i]];
			}
		}
	}

	return buffer_length;
}

#define BITMASK_7BITS 0x7F

static void decode_gsm7_address(const char *hex_data, size_t length, char *output, size_t septet_count) {

  for (size_t u = 0; u < length; u++) {
    ESP_LOGV(TAG, "Hex: %02x", hex_data[u]);
  }

  size_t out_index = 0;

  if (length > 0) {
    output[out_index++] = hex_data[0] & BITMASK_7BITS;
  }

  if (septet_count > 1) {
    int carry_bits = 1;
    size_t i = 1;
    for (; i < length && out_index < septet_count; ++i) {

      output[out_index++] = BITMASK_7BITS & ((hex_data[i] << carry_bits) | (hex_data[i - 1] >> (8 - carry_bits)));

      if (out_index == septet_count)
        break;

      carry_bits++;

      if (carry_bits == 8) {
        carry_bits = 1;
        output[out_index++] = hex_data[i] & BITMASK_7BITS;
        if (out_index == septet_count)
          break;
      }
    }
    if (out_index < septet_count) // Add last remainder.
      output[out_index++] = hex_data[i - 1] >> (8 - carry_bits);
  }
  output[out_index] = '\0';
  
  G7bitToAscii(output, out_index);
}

// Вспомогательная функция: декодирование адреса отправителя
static void decode_sender_address(const char *hex_digits, int digit_count, uint8_t sender_type, char *output) {
  // Анализируем тип адреса
  uint8_t type_of_number = (sender_type & 0x70) >> 4; // Биты 6-4
  uint8_t numbering_plan = sender_type & 0x0F;        // Биты 3-0

  ESP_LOGV(TAG, "Address type analysis: sender_type=0x%02X, TON=0x%X, NPI=0x%X", sender_type, type_of_number,
           numbering_plan);
  ESP_LOGV(TAG, "Hex digits: %s, digit_count: %d", hex_digits, digit_count);

  switch (type_of_number) {
  case 0x00: // Unknown type
  case 0x01: // International number
  case 0x02: // National number
  case 0x03: // Network specific number
  case 0x04: // Subscriber number
  case 0x06: // Abbreviated number
  {
    // Числовой адрес в BCD формате
    decode_phone_number(hex_digits, digit_count, output);
    ESP_LOGV(TAG, "Decoded numeric address: %s", output);
    break;
  }

  case 0x05: // Alphanumeric address
  {
    char *buffer = calloc(digit_count, sizeof(char) + 1);
    // Конвертируем hex в байты
    for (int i = 0; i < digit_count; i++) {
      buffer[i] = hex_to_byte(hex_digits + i * 2);
    }
    // Алфанумерический адрес в GSM 7-bit формате
    decode_gsm7_address(buffer, digit_count, output, 9);
    free(buffer);
    ESP_LOGV(TAG, "Decoded alphanumeric address: %s", output);
    break;
  }

  default: {
    // Неизвестный тип - пытаемся декодировать как числовой
    ESP_LOGW(TAG, "Unknown address type 0x%X, trying numeric decode", type_of_number);
    decode_phone_number(hex_digits, digit_count, output);
    break;
  }
  }
}

// Основная функция декодирования PDU
bool sim900d_decode_pdu(const char *pdu_hex, sim900d_pdu_decoded_t *out) {
  if (!pdu_hex || !out) {
    ESP_LOGE(TAG, "Invalid parameters");
    return false;
  }

  // Инициализируем выходную структуру
  memset(out, 0, sizeof(sim900d_pdu_decoded_t));

  const char *pos = pdu_hex;
  int hex_len = strlen(pdu_hex);

  ESP_LOGV(TAG, "Decoding PDU: %s", pdu_hex);

  // 1. SMSC длина и адрес
  uint8_t smsc_len = hex_to_byte(pos);
  pos += 2;

  if (smsc_len > 0) {
    // SMSC Type of Address
    uint8_t smsc_type = hex_to_byte(pos);
    pos += 2;

    // SMSC Number (BCD format)
    int smsc_digits_len = (smsc_len - 1) * 2; // Вычитаем 1 байт для типа адреса
    char smsc_hex[32];
    if (smsc_digits_len > 0 && smsc_digits_len < 32) {
      strncpy(smsc_hex, pos, smsc_digits_len);
      smsc_hex[smsc_digits_len] = '\0';

      // Декодируем номер SMSC
      decode_phone_number(smsc_hex, (smsc_len - 1) * 2, out->smsc);
      ESP_LOGV(TAG, "SMSC: %s (type: 0x%02X)", out->smsc, smsc_type);
    } else {
      strcpy(out->smsc, "");
    }

    pos += smsc_digits_len;
  } else {
    strcpy(out->smsc, "");
    ESP_LOGV(TAG, "No SMSC provided");
  }

  // 2. PDU Type
  uint8_t pdu_type = hex_to_byte(pos);
  pos += 2;

  bool has_udh = (pdu_type & 0x40) != 0;
  ESP_LOGV(TAG, "PDU Type: 0x%02X, UDH: %s", pdu_type, has_udh ? "Yes" : "No");

  // 3. Sender Address Length
  uint8_t sender_len = hex_to_byte(pos);
  pos += 2;

  // 4. Sender Address Type
  uint8_t sender_type = hex_to_byte(pos);
  pos += 2;

  ESP_LOGV(TAG, "Sender address length: %d digits, type: 0x%02X", sender_len, sender_type);

  // 5. Sender Address
  int sender_hex_len = (sender_len + 1) / 2 * 2; // Округляем до четного
  char sender_hex[32];

  if (sender_hex_len > 0 && sender_hex_len < 32) {
    strncpy(sender_hex, pos, sender_hex_len);
    sender_hex[sender_hex_len] = '\0';
    pos += sender_hex_len;

    // Используем специализированную функцию для декодирования адреса
    decode_sender_address(sender_hex, sender_len, sender_type, out->sender);
  } else {
    strcpy(out->sender, "Unknown");
    pos += sender_hex_len;
  }

  // 6. Protocol Identifier
  pos += 2; // Пропускаем PID

  // 7. Data Coding Scheme
  uint8_t dcs = hex_to_byte(pos);
  pos += 2;

  bool is_unicode = (dcs == 0x08);
  ESP_LOGV(TAG, "DCS: 0x%02X (%s)", dcs, is_unicode ? "Unicode" : "GSM 7-bit");

  // 8. Service Centre Time Stamp
  char timestamp_hex[15];
  strncpy(timestamp_hex, pos, 14);
  timestamp_hex[14] = '\0';
  pos += 14;

  decode_timestamp(timestamp_hex, out->timestamp);
  ESP_LOGV(TAG, "Timestamp: %s", out->timestamp);

  // 9. User Data Length
  uint8_t udl = hex_to_byte(pos);
  pos += 2;

  ESP_LOGV(TAG, "User Data Length: %d bytes", udl);

  // 10. User Data Header (если есть)
  if (has_udh) {
    uint8_t udh_len = hex_to_byte(pos);
    pos += 2;

    ESP_LOGV(TAG, "UDH Length: %d bytes", udh_len);

    // Ищем информационный элемент для конкатенации (IEI = 0x00)
    const char *udh_pos = pos;
    for (int i = 0; i < udh_len * 2; i += 2) {
      uint8_t iei = hex_to_byte(udh_pos + i);
      if (iei == 0x00) { // Concatenated SMS reference number
        uint8_t iedl = hex_to_byte(udh_pos + i + 2);
        if (iedl == 3) {
          out->is_concat = true;
          out->concat_ref = hex_to_byte(udh_pos + i + 4);
          out->concat_total = hex_to_byte(udh_pos + i + 6);
          out->concat_seq = hex_to_byte(udh_pos + i + 8);

          ESP_LOGV(TAG, "Concatenated SMS: part %d of %d (ref: %d)", out->concat_seq, out->concat_total,
                   out->concat_ref);
        }
        break;
      }
    }

    pos += udh_len * 2;
  }

  // 11. User Data (текст сообщения)
  int remaining_len = hex_len - (pos - pdu_hex);
  if (remaining_len <= 0) {
    ESP_LOGW(TAG, "No user data found");
    return true;
  }

  if (is_unicode) {
    // Декодируем Unicode (UCS2/UTF-16BE)
    int text_bytes = remaining_len / 2;
    uint8_t *bytes = malloc(text_bytes);
    if (!bytes) {
      ESP_LOGE(TAG, "Memory allocation failed");
      return false;
    }

    // Конвертируем hex в байты
    for (int i = 0; i < text_bytes; i++) {
      bytes[i] = hex_to_byte(pos + i * 2);
    }

    // Конвертируем UTF-16BE в UTF-8
    int utf8_pos = 0;
    for (int i = 0; i < text_bytes; i += 2) {
      if (i + 1 >= text_bytes)
        break;

      uint16_t unicode_char = (bytes[i] << 8) | bytes[i + 1];

      // Простая конвертация Unicode в UTF-8
      if (unicode_char < 0x80) {
        // ASCII
        if (utf8_pos < sizeof(out->text) - 1) {
          out->text[utf8_pos++] = (char)unicode_char;
        }
      } else if (unicode_char < 0x800) {
        // 2-байтная UTF-8 последовательность
        if (utf8_pos < sizeof(out->text) - 2) {
          out->text[utf8_pos++] = 0xC0 | (unicode_char >> 6);
          out->text[utf8_pos++] = 0x80 | (unicode_char & 0x3F);
        }
      } else {
        // 3-байтная UTF-8 последовательность
        if (utf8_pos < sizeof(out->text) - 3) {
          out->text[utf8_pos++] = 0xE0 | (unicode_char >> 12);
          out->text[utf8_pos++] = 0x80 | ((unicode_char >> 6) & 0x3F);
          out->text[utf8_pos++] = 0x80 | (unicode_char & 0x3F);
        }
      }
    }

    out->text[utf8_pos] = '\0';
    free(bytes);

  } else {
    // GSM 7-bit (упрощенная версия - как raw bytes)
    int text_bytes = remaining_len / 2;
    if (text_bytes > sizeof(out->text) - 1) {
      text_bytes = sizeof(out->text) - 1;
    }

    for (int i = 0; i < text_bytes; i++) {
      uint8_t byte = hex_to_byte(pos + i * 2);
      // Простая фильтрация печатных символов
      out->text[i] = (byte >= 32 && byte <= 126) ? byte : '?';
    }
    out->text[text_bytes] = '\0';
  }

  ESP_LOGV(TAG, "Decoded text: %s", out->text);

  return true;
}