/*
 * service.c — установка position_min / position_max по концевикам до основного цикла.
 *
 * Успех: position_min = 0, position_current = 0, position_max = ход в шагах до MAX.
 * Ошибка (таймаут): стоп, двигатель off, app = dflt_app_params.
 */

#include "service.h"

#include "app.h"
#include "limits.h"
#include "tb6560.h"

#include "stm32f3xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef SERVICE_CALIB_CHUNK
#define SERVICE_CALIB_CHUNK 400U
#endif
#ifndef SERVICE_CALIB_HZ
#define SERVICE_CALIB_HZ 800U
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

void service_calibrate_limits(void)
{
  const uint32_t chunk = SERVICE_CALIB_CHUNK;
  const uint32_t hz = SERVICE_CALIB_HZ;

  tb6560_motor_enable(true);

  /* Отъезд от MIN, если при старте уже на концевике */
  limits_update();
  if (limits_min_engaged())
  {
    tb6560_set_direction_forward(true);
    uint32_t tb = HAL_GetTick();
    while (limits_min_engaged())
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
        if (!limits_min_engaged())
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

  /* Фаза A: поиск MIN (REV) */
  tb6560_set_direction_forward(false);
  uint32_t ta = HAL_GetTick();

  for (;;)
  {
    limits_update();
    if (limits_min_engaged())
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
      if (limits_min_engaged())
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

  app.position_min = 0U;
  app.position_current = 0U;

  /* Фаза B: MAX (FWD), накопление шагов */
  tb6560_set_direction_forward(true);
  uint32_t tb_phase = HAL_GetTick();
  uint32_t total_steps = 0U;

  for (;;)
  {
    limits_update();
    if (limits_max_engaged())
    {
      app.position_max = total_steps;
      tb6560_stop_steps();
      tb6560_motor_enable(false);
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
      if (limits_max_engaged())
      {
        uint32_t rem = motor_data.steps_remaining;
        tb6560_stop_steps();
        total_steps += chunk - rem;
        app.position_max = total_steps;
        tb6560_motor_enable(false);
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
