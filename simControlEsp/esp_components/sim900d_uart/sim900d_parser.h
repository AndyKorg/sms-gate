#ifndef SIM900D_PARSER_H
#define SIM900D_PARSER_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "esp_err.h"

#define MAX_PARAMS 10
#define MAX_PARAM_LEN 64
#define MAX_HANDLERS 10

/**
 * @enum sim900d_sms_mode_t
 * @brief Режимы работы SMS для модуля SIM900D.
 *
 * Этот перечисляемый тип определяет режимы отправки и получения SMS:
 * - SMS_MODE_TEXT: Текстовый режим (1) — сообщения передаются в обычном текстовом виде.
 * - SMS_MODE_PDU: PDU режим (2) — сообщения передаются в закодированном формате PDU.
 */
typedef enum {
    SMS_MODE_TEXT = 1,
    SMS_MODE_PDU = 2
} sim900d_sms_mode_t;

typedef struct {
    char params[MAX_PARAMS][MAX_PARAM_LEN];
    int paramCount;
    // false если ответ в конце не содержит OK, для многострочиных всегда true
    bool result;

    // Дополнительно для многострочного ответа:
    char multilineBody[512]; // SMS тело, может быть multiline
    // Кодировка в которой было принято смс. Для однострочных ответов не определено
    sim900d_sms_mode_t sms_mode;
} Sim900dParsedParams;

// Обработчик
typedef void (*Sim900dHandler)(Sim900dParsedParams* params);

typedef enum {
    /**
     * @enum parse_state_t
     * @brief Состояния процесса парсинга.
     *
     * - PARSE_STATE_IN_PROGRESS: парсинг еще продолжается.
     * - PARSE_STATE_DONE: парсинг завершен.
     */
    PARSE_STATE_DONE = 0,
    PARSE_STATE_IN_PROGRESS
} parse_state_t;

typedef enum {
    /**
     * @enum sms_param_index_t
     * @brief Индексы параметров для параметров если принято смс.
     *
     */
    SMS_PARAM_SENDER = 0,       // Отправитель
    SMS_PARAM_TIMESTAMP = 1,    // Временная метка
    SMS_PARAM_STAT = 2,         // Статус сообщения в памяти модуля, текстом
    SMS_PARAM_SMSC = 3,         // Номер SMS центра, дейстивтельно только для SMS_MODE_PDU
    SMS_PARAM_IS_CONCAT = 4,    // Флаг многочастного SMS, дейстивтельно только для SMS_MODE_PDU
    SMS_PARAM_CONCAT_REF = 5,   // Уникальный идентификатор группы частей, дейстивтельно только для SMS_MODE_PDU
    SMS_PARAM_CONCAT_TOTAL = 6, // Общее количество частей, дейстивтельно только для SMS_MODE_PDU
    SMS_PARAM_CONCAT_SEQ = 7,   // Номер текущей части, дейстивтельно только для SMS_MODE_PDU
    SMS_PARAM_COUNT = 8         // Общее количество параметров
} sms_param_index_t;

/**
 * @brief Регистрация обработчика для определённого префикса ответа SIM900D.
 * Представляет собой представление строки-префикса.
 *
 * Эта структура не владеет памятью для строки префикса;
 * вместо этого она хранит ссылку (указатель) на внешнюю строку.
 * Время жизни строки-префикса должно превышать время жизни этой структуры,
 * чтобы избежать висячих ссылок.
 *
 * Ограничения:
 * - Префикс хранится как указатель на строку (const char*),
 *   структура не управляет памятью и не копирует строку.
 * - Изменение или освобождение исходной строки-префикса во время использования
 *   этой структуры может привести к неопределённому поведению.
 *
 * @param prefix Префикс строки (например, "+CMTI", "+CMGR").
 * @param handler Функция-обработчик, вызываемая при совпадении префикса. 
 * Если NULL, то обработчик для prefix удаляется из списка
 * @return Возвращает ESP_OK при успешном добавлении/обновлении, ESP_OK при удалении, 
 * ESP_ERR_NO_MEM если нет места, ESP_ERR_INVALID_ARG при ошибке аргументов
 */
esp_err_t sim900d_register_handler(const char* prefix, Sim900dHandler handler);

/**
 * @brief Парсинг входящей строки от SIM900D и вызов соответствующего обработчика.
 *
 * Если строка соответствует зарегистрированному префиксу, вызывается обработчик.
 * Для многострочных ответов (например, +CMGR, +CMGL) накапливает тело сообщения до получения "OK" или "ERROR".
 *
 * @param line Входная строка для парсинга.
 */
parse_state_t sim900d_parse_line(const char* line);

/**
 * @brief Устанавливает или получает режим SMS для SIM900D.
 *
 * Эта функция позволяет установить или получить текущий режим SMS (текстовый или PDU) 
 * для модуля SIM900D. Доступ к режиму защищён мьютексом для обеспечения потокобезопасности.
 *
 * @param[out] mode     Указатель на переменную, в которую будет записан текущий режим SMS. 
 *                      Может быть NULL, если получение режима не требуется.
 * @param[in]  set_mode Режим SMS, который необходимо установить (используется только если set == true).
 *                      Допустимые значения: SMS_MODE_TEXT или SMS_MODE_PDU.
 * @param[in]  set      Если true — установить режим SMS в set_mode; если false — только получить текущий режим.
 *
 * @return
 *      - ESP_OK:        Операция выполнена успешно.
 *      - ESP_ERR_INVALID_ARG: Передан некорректный режим SMS для установки.
 *      - ESP_ERR_NO_MEM: Не удалось создать мьютекс из-за нехватки памяти.
 *      - ESP_FAIL:      Не удалось получить мьютекс.
 */
esp_err_t sim900d_parser_sms_mode(sim900d_sms_mode_t *mode, sim900d_sms_mode_t set_mode, bool set);

/**
 * @brief Инициализация парсера SIM900D.
 *
 * Сбрасывает внутренние структуры, очищает список обработчиков и буфер многострочного ответа.
 * @return ESP_OK если инициализация спешна, ESP_ERR_NO_MEM в противном случае.
 */
esp_err_t sim900d_parser_init();

#endif // SIM900D_PARSER_H