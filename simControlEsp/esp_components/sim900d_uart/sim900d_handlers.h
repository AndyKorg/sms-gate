#ifndef SIM900D_HANDLERS_H
#define SIM900D_HANDLERS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Регистрирует все обработчики AT-ответов SIM900D.
 * Вызывать один раз при инициализации модуля.
 */
void sim900d_register_handlers(void);

#ifdef __cplusplus
}
#endif

#endif // SIM900D_HANDLERS_H
