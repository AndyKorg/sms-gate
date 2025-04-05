#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

// Структура для кольцевого буфера
typedef struct {
	uint8_t *buf;     // Буфер данных
	unsigned char tail;  // Индекс хвоста (первый элемент)
	unsigned char head;  // Индекс головы (следующий элемент)
	unsigned char size;  // Размер очереди
} RingBuffer_t;

// Инициализация кольцевого буфера с заданным размером
static inline void RingBuffer_init(RingBuffer_t *rb, unsigned char size) {
	rb->buf = (uint8_t *)malloc(size * sizeof(uint8_t));  // Выделяем память для буфера
	rb->size = size;
	rb->tail = 0;
	rb->head = 0;
}

// Количество элементов в кольцевом буфере
static inline unsigned char RingBuffer_count(RingBuffer_t *rb) {
	return rb->head - rb->tail;
}

// Проверка, заполнен ли кольцевой буфер
static inline bool RingBuffer_is_full(RingBuffer_t *rb) {
	return RingBuffer_count(rb) == rb->size;
}

// Проверка, пуст ли кольцевой буфер
static inline bool RingBuffer_is_empty(RingBuffer_t *rb) {
	return rb->tail == rb->head;
}

// Свободное место в кольцевом буфере
static inline unsigned char RingBuffer_space(RingBuffer_t *rb) {
	return rb->size - RingBuffer_count(rb);
}

// Поместить элемент в кольцевой буфер
static inline void RingBuffer_push(RingBuffer_t *rb, uint8_t byte) {
	if (RingBuffer_is_full(rb)) {
		// Буфер переполнен, обработка ошибки
		return;
	}
	rb->buf[rb->head & (rb->size - 1)] = byte;  // Индексируем по кругу
	rb->head++;
}

// Получить первый элемент из кольцевого буфера
static inline uint8_t RingBuffer_front(RingBuffer_t *rb) {
	return rb->buf[rb->tail & (rb->size - 1)];
}

// Удалить первый элемент из кольцевого буфера
static inline void RingBuffer_pop(RingBuffer_t *rb) {
	if (!RingBuffer_is_empty(rb)) {
		rb->tail++;
	}
}

// Очистить кольцевой буфер
static inline void RingBuffer_flush(RingBuffer_t *rb) {
	rb->tail = 0;
	rb->head = 0;
}

// Освобождение памяти для кольцевого буфера
static inline void RingBuffer_free(RingBuffer_t *rb) {
	free(rb->buf);
}

#endif // RINGBUFFER_H
