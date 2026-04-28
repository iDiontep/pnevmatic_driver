/*
 * limits.h
 *
 * Два концевика: PA3 (ход «назад» / REV), PA4 (ход «вперёд» / FWD).
 * Активный уровень LOW — замыкание на GND, внутренняя подтяжка вверх.
 *
 * Логический min/max задаётся app.settings.position_dir (см. limits_logical_*).
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

/**
 * Логический MIN/MAX с учётом position_dir: при dir == -1 смысл PA3/PA4
 * для «минимума» и «максимума» оси меняется местами.
 */
bool limits_logical_min_engaged(void);
bool limits_logical_max_engaged(void);

#ifdef __cplusplus
}
#endif

#endif /* LIMITS_H_ */
