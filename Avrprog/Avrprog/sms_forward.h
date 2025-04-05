#ifndef SMS_FORWARD_H_
#define SMS_FORWARD_H_

void gsm_wait_and_forward_sms();
int sms_forward_init(UsartModule_t gsm_usart, const char* forward_phone, UsartModule_t log_usart);

#endif /* SMS_FORWARD_H_ */