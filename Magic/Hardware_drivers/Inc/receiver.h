/*
 * receiver.h
 *
 *  Created on: 24 февр. 2026 г.
 *      Author: s20di
 *
 *  Обработка данных, полученных по USB CDC (эхо: что получили — то и отправили).
 */

#ifndef RECEIVER_H_
#define RECEIVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Обработка принятых по USB данных (эхо: отправка обратно).
 * @param buf  Указатель на буфер с данными
 * @param len  Количество байт
 */
void Receiver_OnData(uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* RECEIVER_H_ */
