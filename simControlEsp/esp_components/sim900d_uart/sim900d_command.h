#ifndef SIM900D_COMMAND_H
#define SIM900D_COMMAND_H

// --- Коды команд и ответов ---
#define SIM900D_AT      "AT"
#define SIM900D_ATE     "ATE"     //Режим эхо вкл/выкл
#define SIM900D_ATV     "ATV"     //вывод результата - текстом или числом
#define SIM900D_CMGF    "CMGF"    //режим вывода смс - простой текст или pdu
#define SIM900D_CNMI    "CNMI"
#define SIM900D_CMGR    "CMGR"    //чтения SMS-сообщения из памяти SIM900D
#define SIM900D_CMGL    "CMGL"
#define SIM900D_CPIN    "CPIN"
#define SIM900D_CREG    "CREG"
#define SIM900D_CGREG   "CGREG"
#define SIM900D_CSQ     "CSQ"
#define SIM900D_COPS    "COPS"
#define SIM900D_OK      "OK"
#define SIM900D_ERROR   "ERROR"
#define SIM900D_READY   "READY"
#define SIM900D_CMTI    "CMTI"
#define SIM900D_CPMS    "CPMS"
#define SIM900D_CMGDA   "CMGDA"   // удаления всех SMS
#define SIM900D_CMGD    "CMGD"    // удаления SMS по индексу

/**
 * @brief Перечисление параметров команды AT+CNMI.
 *
 * Используется для идентификации параметров <mode>, <mt>, <bm>, <ds>, <bfr> в команде AT+CNMI.
 */
typedef enum {
    SIM900D_CNMI_PARAM_MODE = 0, /**< Параметр <mode> */
    SIM900D_CNMI_PARAM_MT,       /**< Параметр <mt> */
    SIM900D_CNMI_PARAM_BM,       /**< Параметр <bm> */
    SIM900D_CNMI_PARAM_DS,       /**< Параметр <ds> */
    SIM900D_CNMI_PARAM_BFR,      /**< Параметр <bfr> */
    SIM900D_CNMI_PARAM_MAX,
} sim900d_cnmi_param_t;

/**
 * @brief Перечисление параметров <mode> для команды AT+CNMI.
 */
typedef enum {
    SIM900D_CNMI_MODE_DISABLE = 0,      /**< Не уведомлять терминал. */
    SIM900D_CNMI_MODE_URC_ONLY = 1,     /**< Уведомлять терминал о новых SMS только через URC, без буферизации. */
    SIM900D_CNMI_MODE_BUFFER_URC = 2,   /**< Буферизация и уведомление через URC (обычно используется). */
    SIM900D_CNMI_MODE_DIRECT = 3        /**< Передавать новые SMS напрямую на терминал (без хранения в памяти). 
                                         * Поток данных GPRS будет смешан с сообщениями о новых SMS. Сложно парсить;
                                         */
} sim900d_cnmi_mode_t;

/**
 * @brief Перечисление параметров <mt> для команды AT+CNMI.
 */
typedef enum {
    SIM900D_CNMI_MT_DISABLE = 0, /**< Не уведомлять о новых SMS. */
    SIM900D_CNMI_MT_URC = 1,     /**< Уведомлять о новых SMS через URC. */
    SIM900D_CNMI_MT_BUFFER_URC = 2, /**< Буферизация и уведомление через URC. */
    SIM900D_CNMI_MT_DIRECT = 3      /**< Передавать новые SMS напрямую на терминал (без хранения в памяти). см. SIM900D_CNMI_MODE_DIRECT*/
} sim900d_cnmi_mt_t;

/**
 * @brief Перечисление параметров <bm> для команды AT+CNMI.
 */
typedef enum {
    SIM900D_CNMI_BM_DISABLE = 0, /**< Не уведомлять о broadcast сообщениях. */
    SIM900D_CNMI_BM_ENABLE = 2   /**< Уведомлять о broadcast сообщениях. */
} sim900d_cnmi_bm_t;

/**
 * @brief Перечисление параметров <ds> для команды AT+CNMI.
 */
typedef enum {
    SIM900D_CNMI_DS_DISABLE = 0, /**< Не выводить статус доставки. */
    SIM900D_CNMI_DS_ENABLE = 1   /**< Выводить статус доставки. */
} sim900d_cnmi_ds_t;

/**
 * @brief Перечисление параметров <bfr> для команды AT+CNMI.
 */
typedef enum {
    SIM900D_CNMI_BFR_DISABLE = 0, /**< Не выводить сообщения из буфера при включении. */
    SIM900D_CNMI_BFR_ENABLE = 1   /**< Выводить сообщения из буфера при включении. */
} sim900d_cnmi_bfr_t;

/**
 * @brief Перечисление режимов удаления SMS для команды AT+CMGDA в PDU-режиме.
 *
 * Используется для указания типа сообщений, которые будут удалены командой AT+CMGDA в PDU-режиме.
 */
typedef enum {
    SIM900D_CMGDA_PDU_DEL_READ = 1,     /**< Удалить только прочитанные сообщения ("DEL READ") */
    SIM900D_CMGDA_PDU_DEL_UNREAD,       /**< Удалить только непрочитанные сообщения ("DEL UNREAD") */
    SIM900D_CMGDA_PDU_DEL_SENT,         /**< Удалить только отправленные сообщения ("DEL SENT") */
    SIM900D_CMGDA_PDU_DEL_UNSENT,       /**< Удалить только неотправленные сообщения ("DEL UNSENT") */
    SIM900D_CMGDA_PDU_DEL_INBOX,        /**< Удалить только входящие сообщения ("DEL INBOX") */
    SIM900D_CMGDA_PDU_DEL_ALL,          /**< Удалить все сообщения ("DEL ALL") */
    SIM900D_CMGDA_PDU_DEL_MAX,
} sim900d_cmgda_pdu_mode_t;

/**
 * @brief Массив строковых представлений режимов удаления SMS для команды AT+CMGDA в текстовом режиме.
 *
 * Индексы соответствуют значениям sim900d_cmgda_pdu_mode_t.
 */
static const char *const sim900d_cmgda_text_modes[] = {
    "DEL READ",
    "DEL UNREAD",
    "DEL SENT",
    "DEL UNSENT",
    "DEL INBOX"
    "DEL ALL",
};

/**
 * @brief Структура для сопоставления режима удаления SMS (PDU) и его строкового представления (текстовый режим).
 */
typedef struct {
    sim900d_cmgda_pdu_mode_t pdu_mode;
    const char *text_mode;
} sim900d_cmgda_mode_pair_t;

/**
 * @brief Массив пар режимов удаления SMS: PDU-значение и строковое представление.
 *      сортировка массива в соответствии с sim900d_cmgda_pdu_mode_t
 */
static const sim900d_cmgda_mode_pair_t sim900d_cmgda_mode_pairs[] = {
    { SIM900D_CMGDA_PDU_DEL_READ,   "DEL READ"   },
    { SIM900D_CMGDA_PDU_DEL_UNREAD, "DEL UNREAD" },
    { SIM900D_CMGDA_PDU_DEL_SENT,   "DEL SENT"   },
    { SIM900D_CMGDA_PDU_DEL_UNSENT, "DEL UNSENT" },
    { SIM900D_CMGDA_PDU_DEL_INBOX,  "DEL INBOX"  },
    { SIM900D_CMGDA_PDU_DEL_ALL,    "DEL ALL"    },
};

/***********************************************
            Команды и ответы
***********************************************/

/**
 * @brief Базовая AT-команда для инициализации связи с SIM900D.
 */
#define SIM900D_CMD_AT SIM900D_AT "\r\n"

/**
 * @brief Ответ модуля об успешном выполнении команды ("OK").
 */
#define SIM900D_RESP_OK SIM900D_OK

/**
 * @brief Ответ модуля об ошибке ("ERROR").
 */
#define SIM900D_RESP_ERROR SIM900D_ERROR

/**
 * @brief Ответ модуля о готовоности.
 */
#define SIM900D_RESP_READY SIM900D_READY

/**
 * @brief Выключение эха.
 */
#define SIM900D_ECHO_OFF SIM900D_ATE "0"

/**
 * @brief Включение эха.
 */
#define SIM900D_ECHO_ON SIM900D_ATE "1"

/**
 * @brief Установка текстового режима SMS (AT+CMGF=1).
 */
#define SIM900D_CMD_SMS_MODE SIM900D_AT "+CMGF=1\r\n"

/**
 * @brief AT-команда для проверки поддерживаемых параметров команды +CNMI на модуле SIM900D.
 *
 * Эта команда запрашивает у модема поддерживаемые значения параметров команды +CNMI (уведомления о новых SMS).
 * В ответе модем укажет, какие значения параметров допустимы для настройки уведомлений о новых SMS-сообщениях.
 */
#define SIM900D_RESP_CNMI_TEST  SIM900D_AT "+" SIM900D_CNMI "=?\r\n"

/**
 * @brief Префикс ответа на команду настройки уведомлений ("AT+CNMI=")
 * 
 */
#define SIM900D_RESP_SMS_NOTIFY "+" SIM900D_CNMI ":"

/**
 * @brief Включение уведомлений о новых SMS (AT+CNMI=2,1,0,0,0).
 */
#define SIM900D_CMD_SMS_NOTIFY SIM900D_AT "+" SIM900D_CNMI "=2,1,0,0,0\r\n"

/**
 * @brief Префикс уведомления о новом SMS ("+CMTI:").
 */
#define SIM900D_RESP_CMTI "+" SIM900D_CMTI ":"

/**
 * @brief Форматированная команда для чтения SMS по индексу (AT+CMGR=%d).
 */
#define SIM900D_CMD_READ_SMS_FMT SIM900D_AT "+" SIM900D_CMGR "=%d\r\n"

/**
 * @brief Префикс ответа на команду чтения SMS ("+CMGR:").
 */
#define SIM900D_RESP_CMGR "+" SIM900D_CMGR ":"

/**
 * @brief Форматированная команда для чтения списка SMS (AT+CMGL="%s").
 */
#define SIM900D_CMD_LIST_SMS_FMT SIM900D_AT "+" SIM900D_CMGL "=\"%s\"\r\n"

/**
 * @brief Префикс ответа на команду для чтения списка SMS (CMGL="%s").
 */
#define SIM900D_RESP_CMGL "+" SIM900D_CMGL ":"

/**
 * @brief Запрос состояния PIN-кода SIM-карты (AT+CPIN?).
 */
#define SIM900D_CMD_CPIN SIM900D_AT "+" SIM900D_CPIN "?\r\n"

/**
 * @brief Префикс ответа на запрос PIN-кода ("+CPIN:").
 */
#define SIM900D_RESP_CPIN "+" SIM900D_CPIN ":"

/**
 * @brief Запрос статуса регистрации в сети (AT+CREG?).
 */
#define SIM900D_CMD_CREG SIM900D_AT "+" SIM900D_CREG "?\r\n"

/**
 * @brief Префикс ответа на запрос регистрации в сети ("+CREG:").
 */
#define SIM900D_RESP_CREG "+" SIM900D_CREG ":"

/**
 * @brief Запрос статуса регистрации в GPRS-сети (AT+CGREG?).
 */
#define SIM900D_CMD_CGREG SIM900D_AT "+" SIM900D_CGREG "?\r\n"

/**
 * @brief Префикс ответа на запрос регистрации в GPRS ("+CGREG:").
 */
#define SIM900D_RESP_CGREG "+" SIM900D_CGREG ":"

/**
 * @brief Запрос уровня сигнала (AT+CSQ).
 */
#define SIM900D_CMD_CSQ SIM900D_AT "+" SIM900D_CSQ "\r\n"

/**
 * @brief Префикс ответа на запрос уровня сигнала ("+CSQ:").
 */
#define SIM900D_RESP_CSQ "+" SIM900D_CSQ ":"

/**
 * @brief Запрос информации об операторе (AT+COPS?).
 */
#define SIM900D_CMD_COPS SIM900D_AT "+" SIM900D_COPS "?\r\n"

/**
 * @brief Префикс ответа на запрос информации об операторе ("+COPS:").
 */
#define SIM900D_RESP_COPS "+" SIM900D_COPS ":"

/**
 * @brief Запрос состояния памяти SMS (AT+CPMS?).
 */
#define SIM900D_CMD_CPMS SIM900D_AT "+" SIM900D_CPMS "?\r\n"

/**
 * @brief Префикс ответа на запрос состояния памяти SMS ("+CPMS:").
 */
#define SIM900D_RESP_CPMS "+" SIM900D_CPMS ":"

/**
 * @brief Команда для удаления SMS из памяти (AT+CMGDA=).
 * @details Удаляет SMS-сообщения из памяти модуля в соответствии с режимом удаления
 */
#define SIM900D_CMD_DELETE_SMS    SIM900D_AT "+" SIM900D_CMGDA "="

/**
 * @brief Форматированная команда для удаления SMS по индексу (AT+CMGD=%d).
 * @details Удаляет SMS-сообщение с указанным индексом из памяти модуля.
 */
#define SIM900D_CMD_DELETE_SMS_BY_INDEX_FMT SIM900D_AT "+"  SIM900D_CMGD "=%d\r\n"

/**
 * @brief Форматированная команда смены режима кодирования СМС (AT+CMGF=%d)
 */
#define SIM900D_CMD_CMGF_MODE   SIM900D_AT "+" SIM900D_CMGF "=%d\r\n"

#endif // SIM900D_COMMAND_H