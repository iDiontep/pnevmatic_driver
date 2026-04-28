/*
 * eeprom.h — сохранение app_settings_t во внутреннюю Flash (последняя страница).
 */

#ifndef EEPROM_H_
#define EEPROM_H_

#include <stdbool.h>

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Запись текущих APS во Flash (стирание одной страницы + программирование).
 * @return true при успехе HAL.
 */
bool eeprom_save(const app_settings_t *settings);

/**
 * Чтение APS из Flash; при неверной магии/версии/контрольной сумме — false.
 */
bool eeprom_try_load(app_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif /* EEPROM_H_ */
