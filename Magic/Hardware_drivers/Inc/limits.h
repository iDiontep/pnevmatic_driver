/*
 * limits.h
 *
 * Два концевика: PA3 (ход «назад» / REV), PA4 (ход «вперёд» / FWD).
 * Активный уровень LOW — замыкание на GND, внутренняя подтяжка вверх.
 */

#ifndef LIMITS_H_
#define LIMITS_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void limits_init(void);

/** Вызывать периодически (например из app_process): антидребезг входов. */
void limits_update(void);

/** PA3: сработал концевик со стороны отрицательного хода (при DIR=REV). */
bool limits_min_engaged(void);

/** PA4: сработал концевик со стороны положительного хода (при DIR=FWD). */
bool limits_max_engaged(void);

#ifdef __cplusplus
}
#endif

#endif /* LIMITS_H_ */
