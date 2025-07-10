#ifndef SIM900D_PARSER_H
#define SIM900D_PARSER_H

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
    // false если ответ в конеце не содержит OK, для многострочиных всегда true
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
 * @brief Разбирает строку вида "(1,2,5-7,10)" и возвращает массив чисел.
 *        Если встречается диапазон через '-', то добавляет все числа из диапазона.
 * 
 * @param str Входная строка (например, "(1,2,5-7,10)")
 * @param outCount Указатель на переменную, куда будет записано количество чисел
 * @return int* Указатель на массив чисел (выделяется через malloc, не забудьте освободить)
 */
int* sim900d_parse_number_list(const char* str, int* outCount);

/**
 * @brief Устанавливает текущий режим SMS (текстовый или PDU).
 *
 * @param mode Режим SMS (SMS_MODE_TEXT или SMS_MODE_PDU)
 * @return ESP_OK при успешной установке, ESP_ERR_INVALID_ARG при ошибке аргументов
 */
esp_err_t sim900d_set_sms_mode(sim900d_sms_mode_t mode);

/**
 * @brief Инициализация парсера SIM900D.
 *
 * Сбрасывает внутренние структуры, очищает список обработчиков и буфер многострочного ответа.
 * @return ESP_OK если инициализация спешна, ESP_ERR_NO_MEM в противном случае.
 */
esp_err_t sim900d_parser_init();

#endif // SIM900D_PARSER_H