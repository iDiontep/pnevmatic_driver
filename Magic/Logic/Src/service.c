/*
 * service.c — установка settings.position_min / position_max по концевикам до основного цикла.
 *
 * Успех: position_min = 0, data.current_position = 0, position_max = ход в шагах до MAX.
 * Ошибка (таймаут): стоп, двигатель off, app = dflt_app_params.
 */

#include "service.h"

#include "app.h"
#include "limits.h"
#include "eeprom.h"
#include "tb6560.h"

#include "stm32f3xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef SERVICE_CALIB_CHUNK
#define SERVICE_CALIB_CHUNK 400U
#endif
#ifndef SERVICE_CALIB_PHASE_MS
#define SERVICE_CALIB_PHASE_MS 120000U
#endif
#ifndef SERVICE_CALIB_BOUNCE_MS
#define SERVICE_CALIB_BOUNCE_MS 15000U
#endif

static bool phase_timed_out(uint32_t t_phase_start)
{
  return (HAL_GetTick() - t_phase_start) >= SERVICE_CALIB_PHASE_MS;
}

static bool bounce_timed_out(uint32_t t_bounce_start)
{
  return (HAL_GetTick() - t_bounce_start) >= SERVICE_CALIB_BOUNCE_MS;
}

static void calibrate_fail(void)
{
  tb6560_stop_steps();
  tb6560_motor_enable(false);
  app = dflt_app_params;
}

/** Направление к логическому MIN (к физ. MIN при dir>=0, к физ. MAX при dir<0). */
static void calib_set_direction_toward_logical_min(void)
{
  tb6560_set_direction_forward(app.settings.position_dir < 0);
}

/** Направление к логическому MAX. */
static void calib_set_direction_toward_logical_max(void)
{
  tb6560_set_direction_forward(app.settings.position_dir >= 0);
}

/** Отъезд от логического MIN (в сторону логического MAX). */
static void calib_set_direction_away_from_logical_min(void)
{
  calib_set_direction_toward_logical_max();
}

void service_calibrate_limits(void)
{
  const uint32_t chunk = SERVICE_CALIB_CHUNK;
  uint32_t       hz    = app.settings.motor_speed;
  if (hz == 0U)
    hz = 1000U;

  tb6560_motor_enable(true);

  /* Отъезд от MIN, если при старте уже на концевике */
  limits_update();
  if (limits_logical_min_engaged())
  {
    calib_set_direction_away_from_logical_min();
    uint32_t tb = HAL_GetTick();
    while (limits_logical_min_engaged())
    {
      limits_update();
      if (bounce_timed_out(tb))
      {
        calibrate_fail();
        return;
      }
      tb6560_move_steps_start(chunk, hz);
      while (motor_data.motion != TB6560_MOTION_IDLE)
      {
        limits_update();
        if (!limits_logical_min_engaged())
        {
          tb6560_stop_steps();
          goto after_bounce;
        }
        if (bounce_timed_out(tb))
        {
          tb6560_stop_steps();
          calibrate_fail();
          return;
        }
      }
    }
  after_bounce:
    ;
  }

  /* Фаза A: поиск логического MIN */
  calib_set_direction_toward_logical_min();
  uint32_t ta = HAL_GetTick();

  for (;;)
  {
    limits_update();
    if (limits_logical_min_engaged())
      break;

    if (phase_timed_out(ta))
    {
      calibrate_fail();
      return;
    }

    tb6560_move_steps_start(chunk, hz);
    while (motor_data.motion != TB6560_MOTION_IDLE)
    {
      limits_update();
      if (limits_logical_min_engaged())
      {
        tb6560_stop_steps();
        goto phase_a_done;
      }
      if (phase_timed_out(ta))
      {
        tb6560_stop_steps();
        calibrate_fail();
        return;
      }
    }
  }
phase_a_done:

  app.settings.position_min = 0U;
  app.data.current_position = 0U;

  /* Фаза B: логический MAX, накопление шагов */
  calib_set_direction_toward_logical_max();
  uint32_t tb_phase   = HAL_GetTick();
  uint32_t total_steps = 0U;

  for (;;)
  {
    limits_update();
    if (limits_logical_max_engaged())
    {
      app.settings.position_max = total_steps;
      tb6560_stop_steps();
      tb6560_motor_enable(false);
      (void)eeprom_save(&app.settings);
      return;
    }

    if ((HAL_GetTick() - tb_phase) >= SERVICE_CALIB_PHASE_MS)
    {
      calibrate_fail();
      return;
    }

    tb6560_move_steps_start(chunk, hz);
    while (motor_data.motion != TB6560_MOTION_IDLE)
    {
      limits_update();
      if (limits_logical_max_engaged())
      {
        uint32_t rem = motor_data.steps_remaining;
        tb6560_stop_steps();
        total_steps += chunk - rem;
        app.settings.position_max = total_steps;
        tb6560_motor_enable(false);
        (void)eeprom_save(&app.settings);
        return;
      }
      if ((HAL_GetTick() - tb_phase) >= SERVICE_CALIB_PHASE_MS)
      {
        tb6560_stop_steps();
        calibrate_fail();
        return;
      }
    }
    total_steps += chunk;
  }
}
