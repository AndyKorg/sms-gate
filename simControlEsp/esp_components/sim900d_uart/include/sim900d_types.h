#ifndef SIM900D_TYPES_H
#define SIM900D_TYPES_H

/**
 * @brief Структура, представляющая SMS-сообщение.
 *
 * Эта структура содержит информацию о полученном SMS-сообщении,
 * включая его индекс в памяти, номер отправителя и текст сообщения.
 *
 * @typedef sms_message_t
 * @param index   Индекс SMS-сообщения в памяти.
 * @param sender  Нуль-терминированная строка с номером телефона отправителя (до 31 символа).
 * @param text    Нуль-терминированная строка с текстом SMS-сообщения (до 160 символов).
 */
typedef struct {
  int index;
  char sender[32];
  char text[512];
  char status[32];
  char timestamp[32];
} sms_message_t;

#endif // SIM900D_TYPES_H
