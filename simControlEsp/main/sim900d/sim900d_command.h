#ifndef SIM900D_COMMAND_H
#define SIM900D_COMMAND_H

/**
 * @brief Базовая AT-команда для инициализации связи с SIM900D.
 */
#define SIM900D_CMD_AT "AT\r\n"

/**
 * @brief Установка текстового режима SMS (AT+CMGF=1).
 */
#define SIM900D_CMD_SMS_MODE "AT+CMGF=1\r\n"

/**
 * @brief Включение уведомлений о новых SMS (AT+CNMI=2,1,0,0,0).
 */
#define SIM900D_CMD_SMS_NOTIFY "AT+CNMI=2,1,0,0,0\r\n"

/**
 * @brief Форматированная команда для чтения SMS по индексу (AT+CMGR=%d).
 */
#define SIM900D_CMD_READ_SMS_FMT "AT+CMGR=%d\r\n"

/**
 * @brief Форматированная команда для чтения списка SMS (AT+CMGL="%s").
 */
#define SIM900D_CMD_LIST_SMS_FMT "AT+CMGL=\"%s\"\r\n"

/**
 * @brief Запрос состояния PIN-кода SIM-карты (AT+CPIN?).
 */
#define SIM900D_CMD_CPIN "AT+CPIN?\r\n"

/**
 * @brief Запрос статуса регистрации в сети (AT+CREG?).
 */
#define SIM900D_CMD_CREG "AT+CREG?\r\n"

/**
 * @brief Запрос статуса регистрации в GPRS-сети (AT+CGREG?).
 */
#define SIM900D_CMD_CGREG "AT+CGREG?\r\n"

/**
 * @brief Запрос уровня сигнала (AT+CSQ).
 */
#define SIM900D_CMD_CSQ "AT+CSQ\r\n"

/**
 * @brief Запрос информации об операторе (AT+COPS?).
 */
#define SIM900D_CMD_COPS "AT+COPS?\r\n"

/**
 * @brief Ответ модуля об успешном выполнении команды ("OK").
 */
#define SIM900D_RESP_OK "OK"

/**
 * @brief Ответ модуля об ошибке ("ERROR").
 */
#define SIM900D_RESP_ERROR "ERROR"

/**
 * @brief Ответ модуля о готовоности.
 */
#define SIM900D_RESP_READY "READY"

/**
 * @brief Префикс уведомления о новом SMS ("+CMTI:").
 */
#define SIM900D_RESP_CMTI "+CMTI:"

/**
 * @brief Префикс ответа на команду чтения SMS ("+CMGR:").
 */
#define SIM900D_RESP_CMGR "+CMGR:"

/**
 * @brief Префикс ответа на команду для чтения списка SMS (CMGL="%s").
 */
#define SIM900D_RESP_CMGL "+CMGL:"

/**
 * @brief Префикс ответа на запрос PIN-кода ("+CPIN:").
 */
#define SIM900D_RESP_CPIN "+CPIN:"

/**
 * @brief Префикс ответа на запрос регистрации в сети ("+CREG:").
 */
#define SIM900D_RESP_CREG "+CREG:"

/**
 * @brief Префикс ответа на запрос регистрации в GPRS ("+CGREG:").
 */
#define SIM900D_RESP_CGREG "+CGREG:"

/**
 * @brief Префикс ответа на запрос уровня сигнала ("+CSQ:").
 */
#define SIM900D_RESP_CSQ "+CSQ:"

/**
 * @brief Префикс ответа на запрос информации об операторе ("+COPS:").
 */
#define SIM900D_RESP_COPS "+COPS:"

#endif // SIM900D_COMMAND_H