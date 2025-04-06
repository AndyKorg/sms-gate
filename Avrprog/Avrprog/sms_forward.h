#ifndef SMS_FORWARD_H_
#define SMS_FORWARD_H_

#define MAX_RESET_ATTEMPTS 10

// Тип callback-функции для управления PWRKEY
typedef void (*PwrKeyControlCallback)(uint8_t level);

void gsm_wait_and_forward_sms();
int sms_forward_init(UsartModule_t gsm_usart, const char* forward_phone, UsartModule_t log_usart);
int sim_reset(PwrKeyControlCallback pwr_callback);

#endif /* SMS_FORWARD_H_ */