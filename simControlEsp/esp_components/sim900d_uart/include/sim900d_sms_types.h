#ifndef SIM900D_TYPES_H
#define SIM900D_TYPES_H

#define SIM900D_TEXT_LEN_MAX  512

#define SIM900S_SMS_MODE_PDU          true
#define SIM900S_SMS_MODE_SIMPLE_TEXT  false

/**
 * @brief Структура, представляющая SMS-сообщение.
 *
 * Эта структура содержит информацию о полученном SMS-сообщении,
 * включая его индекс в памяти, номер отправителя и текст сообщения.
 *
 * @typedef sms_message_t
 * @param index   Индекс SMS-сообщения в памяти модуля. (не путать с индексом в многочастном СМС)
 * @param sender  Нуль-терминированная строка с номером телефона отправителя (до 31 символа).
 * @param text    Нуль-терминированная строка с текстом SMS-сообщения (до 160 символов).
 * @param status  Статус сообщения в памяти модуля. См. sim900d_sms_status_t.text
 * @param timestamp дата и время получения: YY/MM/DD,hh:mm:ss±zz
 * @param pdu_mode режим приема смс, true - расширенный режим pdu, false - простой текстовый режим
 * ----- Парамтеры ниже заполняются только в режиме sim900d_sms_mode_t.SMS_MODE_PDU ----
 * @param is_concat Многочастное сообщение. В режиме SMS_MODE_TEXT равно false.
 * @param concat_ref уникальный идентификатор для группы частей. Уникален для отправителя. В режиме SMS_MODE_TEXT равно 0.
 * @param concat_total общее количество частей. В режиме SMS_MODE_TEXT равно 1.
 * @param concat_seq номер текущей части. В режиме SMS_MODE_TEXT равно 1.
 * @param smsc Номер SMS центра. В режиме SMS_MODE_TEXT null
 * 
 */
typedef struct {
  int index;
  char sender[32];
  char text[SIM900D_TEXT_LEN_MAX];
  char status[32];
  char timestamp[32];
  bool pdu_mode;
  bool is_concat;
  int concat_ref; 
  int concat_total;
  int concat_seq;
  char smsc[32];
} sms_message_t;

#endif // SIM900D_TYPES_H
