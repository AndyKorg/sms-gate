#ifndef M25P128_H_
#define M25P128_H_

#include <avr/io.h>

// Пины подключения к SPI
#define M25P128_SDI		PORTB5
#define M25P128_SDO		PORTB6
#define M25P128_SCK		PORTB7
#define M25P128_CS		PORTB4
#define M25P128_W_PIN	PORTB2
#define M25P128_HOLD	PORTB3

// Инициализация SPI и самого чипа M25P128
void M25P128_init(void);

// Управление линией CS
void M25P128_CS_Low(void);
void M25P128_CS_High(void);

// Чтение и запись по SPI
uint8_t M25P128_SPI_Transfer(uint8_t data);

// Команды управления
void M25P128_WriteEnable(void);
void M25P128_WriteDisable(void);
uint8_t M25P128_ReadStatusRegister(void);
void M25P128_WaitForWriteEnd(void);

// Чтение данных
void M25P128_ReadData(uint32_t address, uint8_t* buffer, uint16_t length);

// Запись данных
void M25P128_PageProgram(uint32_t address, const uint8_t* data, uint16_t length);

// Стирание
void M25P128_SectorErase(uint32_t address);
void M25P128_BulkErase(void);

// Идентификация чипа
void M25P128_ReadJEDEC_ID(uint8_t* manufacturerID, uint8_t* memoryType, uint8_t* capacity);

// Защита
void M25P128_Unprotect(void);

#endif /* M25P128_H_ */
