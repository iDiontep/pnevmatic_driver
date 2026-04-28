/*
 * app.h
 *
 *  Основной цикл приложения.
 */

#ifndef APP_H_
#define APP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Параметры приложения */
typedef struct {
  uint32_t minutes;
  uint8_t  button;
  uint32_t position_min;
  uint32_t position_max;
  uint32_t position_current;
} app_t;

/** Глобальный экземпляр параметров приложения */
extern app_t app;

/** Параметры по умолчанию: все поля 0 */
extern const app_t dflt_app_params;

/**
 * @brief Обработка одного шага основного цикла программы.
 *        Вызывается из main() в цикле while(1).
 */
void app_process(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H_ */
