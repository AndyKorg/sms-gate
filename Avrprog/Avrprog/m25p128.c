#include <avr/io.h>
#include <util/delay.h>
#include "m25p128.h"

// ==========================
// Вспомогательные макросы
// ==========================
#define CS_LOW()     PORTB &= ~(1 << M25P128_CS)
#define CS_HIGH()    PORTB |= (1 << M25P128_CS)

// ==========================
// SPI - передача и приём
// ==========================
uint8_t M25P128_SPI_Transfer(uint8_t data) {
	SPDR0 = data;
	while (!(SPSR0 & (1 << SPIF0)));
	return SPDR0;
}

// ==========================
// Инициализация SPI и пинов
// ==========================
void M25P128_init(void) {
	// Настройка SPI: pins как выход
	DDRB |= (1 << M25P128_SDI) | (1 << M25P128_SCK) | (1 << M25P128_CS) | (1 << M25P128_HOLD) | (1 << M25P128_W_PIN);
	PORTB |= (1 << M25P128_W_PIN);
	PORTB |= (1 << M25P128_HOLD);
	
	// SDO (MISO) как вход
	DDRB &= ~(1 << M25P128_SDO);

	// Включение SPI, мастер-режим, fosc/16
	SPCR0 = (1 << SPE0) | (1 << MSTR0) | (1 << SPR00);

	// Установка CS в HIGH
	CS_HIGH();
}

// ==========================
// Управление статусом
// ==========================
void M25P128_WriteEnable(void) {
	CS_LOW();
	M25P128_SPI_Transfer(0x06); // WRITE ENABLE
	CS_HIGH();
}

void M25P128_WriteDisable(void) {
	CS_LOW();
	M25P128_SPI_Transfer(0x04); // WRITE DISABLE
	CS_HIGH();
}

uint8_t M25P128_ReadStatusRegister(void) {
	uint8_t status;
	CS_LOW();
	M25P128_SPI_Transfer(0x05); // READ STATUS REGISTER
	status = M25P128_SPI_Transfer(0xFF);
	CS_HIGH();
	return status;
}

void M25P128_WaitForWriteEnd(void) {
	while (M25P128_ReadStatusRegister() & 0x01); // WIP bit
}

// ==========================
// Чтение данных
// ==========================
void M25P128_ReadData(uint32_t address, uint8_t* buffer, uint16_t length) {
	CS_LOW();
	M25P128_SPI_Transfer(0x03); // READ DATA
	M25P128_SPI_Transfer((address >> 16) & 0xFF);
	M25P128_SPI_Transfer((address >> 8) & 0xFF);
	M25P128_SPI_Transfer(address & 0xFF);
	for (uint16_t i = 0; i < length; i++) {
		buffer[i] = M25P128_SPI_Transfer(0xFF);
	}
	CS_HIGH();
}

// ==========================
// Запись страницы (до 256 байт)
// ==========================
void M25P128_PageProgram(uint32_t address, const uint8_t* data, uint16_t length) {
	if (length > 256) length = 256; // Ограничение размера страницы

	M25P128_WriteEnable();
	CS_LOW();
	M25P128_SPI_Transfer(0x02); // PAGE PROGRAM
	M25P128_SPI_Transfer((address >> 16) & 0xFF);
	M25P128_SPI_Transfer((address >> 8) & 0xFF);
	M25P128_SPI_Transfer(address & 0xFF);
	for (uint16_t i = 0; i < length; i++) {
		M25P128_SPI_Transfer(data[i]);
	}
	CS_HIGH();
	M25P128_WaitForWriteEnd();
}

// ==========================
// Стирание сектора (обычно 64KB)
// ==========================
void M25P128_SectorErase(uint32_t address) {
	M25P128_WriteEnable();
	CS_LOW();
	M25P128_SPI_Transfer(0xD8); // SECTOR ERASE
	M25P128_SPI_Transfer((address >> 16) & 0xFF);
	M25P128_SPI_Transfer((address >> 8) & 0xFF);
	M25P128_SPI_Transfer(address & 0xFF);
	CS_HIGH();
	M25P128_WaitForWriteEnd();
}

// ==========================
// Полное стирание чипа
// ==========================
void M25P128_BulkErase(void) {
	M25P128_WriteEnable();
	CS_LOW();
	M25P128_SPI_Transfer(0xC7); // BULK ERASE
	CS_HIGH();
	M25P128_WaitForWriteEnd();
}

// ==========================
// Чтение JEDEC ID
// ==========================
void M25P128_ReadJEDEC_ID(uint8_t* manufacturerID, uint8_t* memoryType, uint8_t* capacity) {
	CS_LOW();
	M25P128_SPI_Transfer(0x9F); // JEDEC ID
	*manufacturerID = M25P128_SPI_Transfer(0xFF);
	*memoryType = M25P128_SPI_Transfer(0xFF);
	*capacity = M25P128_SPI_Transfer(0xFF);
	CS_HIGH();
}

// ==========================
// Снятие защиты (если включена)
// ==========================
void M25P128_Unprotect(void) {
	M25P128_WriteEnable();
	CS_LOW();
	M25P128_SPI_Transfer(0x01); // WRITE STATUS REGISTER
	M25P128_SPI_Transfer(0x00); // Полная разблокировка
	CS_HIGH();
	M25P128_WaitForWriteEnd();
}
