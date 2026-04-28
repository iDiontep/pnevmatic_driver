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

/** APS — настройки приложения */
typedef struct
{
  uint32_t position_min;
  uint32_t position_max;
  int32_t  position_dir; /**< логический смысл лимитов: 1 или -1 */
  uint32_t motor_speed; /**< Гц STEP; 0 недопустим в протоколе — подставить дефолт 1000 */
} app_settings_t;

/** APD — данные приложения */
typedef struct
{
  uint32_t minutes;
  uint8_t  button;
  uint32_t current_position;
} app_data_t;

typedef struct
{
  app_settings_t settings;
  app_data_t     data;
} app_t;

/** Глобальный экземпляр параметров приложения */
extern app_t app;

/** Параметры по умолчанию */
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
