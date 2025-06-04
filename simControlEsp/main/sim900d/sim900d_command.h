#ifndef SIM900D_COMMAND_H
#define SIM900D_COMMAND_H

// --- Общие части ---
#define SIM900D_AT_PREFIX      "AT"
#define SIM900D_CRLF           "\r\n"
#define SIM900D_PLUS           "+"
#define SIM900D_COLON          ":"
#define SIM900D_QUESTION       "?" 

// --- Коды команд и ответов ---
#define SIM900D_CODE_OK        "OK"     // Команда успешно выполнена.
#define SIM900D_CODE_ERROR     "ERROR"  // Ошибка выполнения команды.

#define SIM900D_CODE_CMGF      "CMGF"   // Установка формата SMS (текст/PDU).
#define SIM900D_CODE_CNMI      "CNMI"   // Настройка уведомлений о новых сообщениях.
#define SIM900D_CODE_CMGR      "CMGR"   // Чтение SMS из памяти.
#define SIM900D_CODE_CPIN      "CPIN"   // Ввод PIN-кода.
#define SIM900D_CODE_CREG      "CREG"   // Статус регистрации в сети.
#define SIM900D_CODE_CGREG     "CGREG"  // Статус регистрации в GPRS-сети.
#define SIM900D_CODE_CSQ       "CSQ"    // Отчёт об уровне сигнала.
#define SIM900D_CODE_COPS      "COPS"   // Выбор оператора.
#define SIM900D_CODE_CMTI      "CMTI"   // Уведомление о новом SMS.

// --- Команды ---
/**
 * @brief Базовая AT-команда для инициализации связи с SIM900D.
 */
#define SIM900D_CMD_AT SIM900D_AT_PREFIX SIM900D_CRLF

/**
 * @brief Установка текстового режима SMS (AT+CMGF=1).
 */
#define SIM900D_CMD_SMS_MODE SIM900D_AT_PREFIX SIM900D_PLUS SIM900D_CODE_CMGF "=1" SIM900D_CRLF

/**
 * @brief Включение уведомлений о новых SMS (AT+CNMI=2,1,0,0,0).
 */
#define SIM900D_CMD_SMS_NOTIFY SIM900D_AT_PREFIX SIM900D_PLUS SIM900D_CODE_CNMI "=2,1,0,0,0" SIM900D_CRLF

/**
 * @brief Форматированная команда для чтения SMS по индексу (AT+CMGR=%d).
 */
#define SIM900D_CMD_READ_SMS_FMT SIM900D_AT_PREFIX SIM900D_PLUS SIM900D_CODE_CMGR "=%d" SIM900D_CRLF

/**
 * @brief Запрос состояния PIN-кода SIM-карты (AT+CPIN?).
 */
#define SIM900D_CMD_CPIN SIM900D_AT_PREFIX SIM900D_PLUS SIM900D_CODE_CPIN SIM900D_QUESTION SIM900D_CRLF

/**
 * @brief Запрос статуса регистрации в сети (AT+CREG?).
 */
#define SIM900D_CMD_CREG SIM900D_AT_PREFIX SIM900D_PLUS SIM900D_CODE_CREG SIM900D_QUESTION SIM900D_CRLF

/**
 * @brief Запрос статуса регистрации в GPRS-сети (AT+CGREG?).
 */
#define SIM900D_CMD_CGREG SIM900D_AT_PREFIX SIM900D_PLUS SIM900D_CODE_CGREG SIM900D_QUESTION SIM900D_CRLF

/**
 * @brief Запрос уровня сигнала (AT+CSQ).
 */
#define SIM900D_CMD_CSQ SIM900D_AT_PREFIX SIM900D_PLUS SIM900D_CODE_CSQ SIM900D_CRLF

/**
 * @brief Запрос информации об операторе (AT+COPS?).
 */
#define SIM900D_CMD_COPS SIM900D_AT_PREFIX SIM900D_PLUS SIM900D_CODE_COPS SIM900D_QUESTION SIM900D_CRLF

// --- Ответы и префиксы ---
/**
 * @brief Ответ модуля об успешном выполнении команды ("OK").
 */
#define SIM900D_RESP_OK SIM900D_CODE_OK

/**
 * @brief Ответ модуля об ошибке ("ERROR").
 */
#define SIM900D_RESP_ERROR SIM900D_CODE_ERROR

/**
 * @brief Префикс уведомления о новом SMS ("+CMTI:").
 */
#define SIM900D_RESP_CMTI SIM900D_PLUS SIM900D_CODE_CMTI SIM900D_COLON

/**
 * @brief Префикс ответа на команду чтения SMS ("+CMGR:").
 */
#define SIM900D_RESP_CMGR SIM900D_PLUS SIM900D_CODE_CMGR SIM900D_COLON

/**
 * @brief Префикс ответа на запрос PIN-кода ("+CPIN:").
 */
#define SIM900D_RESP_CPIN SIM900D_PLUS SIM900D_CODE_CPIN SIM900D_COLON

/**
 * @brief Префикс ответа на запрос регистрации в сети ("+CREG:").
 */
#define SIM900D_RESP_CREG SIM900D_PLUS SIM900D_CODE_CREG SIM900D_COLON

/**
 * @brief Префикс ответа на запрос регистрации в GPRS ("+CGREG:").
 */
#define SIM900D_RESP_CGREG SIM900D_PLUS SIM900D_CODE_CGREG SIM900D_COLON

/**
 * @brief Префикс ответа на запрос уровня сигнала ("+CSQ:").
 */
#define SIM900D_RESP_CSQ SIM900D_PLUS SIM900D_CODE_CSQ SIM900D_COLON

/**
 * @brief Префикс ответа на запрос информации об операторе ("+COPS:").
 */
#define SIM900D_RESP_COPS SIM900D_PLUS SIM900D_CODE_COPS SIM900D_COLON

#endif // SIM900D_COMMAND_H