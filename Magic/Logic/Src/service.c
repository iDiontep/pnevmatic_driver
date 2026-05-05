/*
 * service.c — установка settings.position_min / position_max по концевикам до основного цикла.
 *
 * Успех: position_min = 0, position_max = ход в шагах, current_position = position_max (стык с MAX),
 * фаза C — подбор ramp_* в APS и проверка ходом «половина туда–обратно».
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
  (void)tb6560_take_pending_move(NULL, NULL);
  tb6560_motor_enable(false);
  app = dflt_app_params;
  tb6560_set_move_ramp(app.settings.ramp_step_interval, app.settings.ramp_hz_step,
                       app.settings.ramp_min_hz);
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

/** Нога 1: к середине — старт с MAX; нештатно раннее срабатывание MIN при незаконченном MOVE. */
static bool ramp_wait_leg1_ok(uint32_t t_start)
{
  while (motor_data.motion != TB6560_MOTION_IDLE)
  {
    limits_update();
    if (motor_data.steps_remaining > 0U && limits_logical_min_engaged())
    {
      tb6560_stop_steps();
      return false;
    }
    if (phase_timed_out(t_start))
    {
      tb6560_stop_steps();
      return false;
    }
  }
  return true;
}

/** Нога 2: к MAX — концевик при оставшихся шагах или нет MAX в конце → ошибка. */
static bool ramp_wait_leg2_ok(uint32_t t_start)
{
  while (motor_data.motion != TB6560_MOTION_IDLE)
  {
    limits_update();
    if (motor_data.steps_remaining > 0U)
    {
      if (limits_logical_min_engaged() || limits_logical_max_engaged())
      {
        tb6560_stop_steps();
        return false;
      }
    }
    if (phase_timed_out(t_start))
    {
      tb6560_stop_steps();
      return false;
    }
  }
  limits_update();
  return limits_logical_max_engaged();
}

static bool ramp_try_half_roundtrip(uint32_t half_span, uint32_t hz)
{
  uint32_t t0 = HAL_GetTick();
  calib_set_direction_toward_logical_min();
  tb6560_move_steps_start(half_span, hz);
  if (!ramp_wait_leg1_ok(t0))
    return false;

  t0 = HAL_GetTick();
  calib_set_direction_toward_logical_max();
  tb6560_move_steps_start(half_span, hz);
  return ramp_wait_leg2_ok(t0);
}

/**
 * Подбор ramp_*; при half_span==0 — дефолты из TB6560_RAMP_*.
 * @return false если ни один набор не прошёл проверку.
 */
static bool service_ramp_calibration_tune(uint32_t half_span, uint32_t hz)
{
  if (half_span == 0U)
  {
    app.settings.ramp_step_interval = TB6560_RAMP_STEP_INTERVAL;
    app.settings.ramp_hz_step       = TB6560_RAMP_HZ_STEP;
    app.settings.ramp_min_hz        = TB6560_RAMP_MIN_HZ;
    tb6560_set_move_ramp(app.settings.ramp_step_interval, app.settings.ramp_hz_step,
                         app.settings.ramp_min_hz);
    return true;
  }

  static const uint32_t cand_iv[] = { 20U, 15U, 12U, 10U, 8U, 5U };
  static const uint32_t cand_dhz[] = { 5U, 8U, 10U, 12U, 15U };
  static const uint32_t cand_min[] = { 150U, 120U, 100U, 80U, 60U, 50U };

  for (unsigned i = 0; i < sizeof(cand_iv) / sizeof(cand_iv[0]); i++)
  {
    for (unsigned j = 0; j < sizeof(cand_dhz) / sizeof(cand_dhz[0]); j++)
    {
      for (unsigned k = 0; k < sizeof(cand_min) / sizeof(cand_min[0]); k++)
      {
        tb6560_set_move_ramp(cand_iv[i], cand_dhz[j], cand_min[k]);
        if (ramp_try_half_roundtrip(half_span, hz))
        {
          app.settings.ramp_step_interval = cand_iv[i];
          app.settings.ramp_hz_step       = cand_dhz[j];
          app.settings.ramp_min_hz        = cand_min[k];
          return true;
        }
      }
    }
  }
  return false;
}

static bool service_finish_with_ramp_phase(uint32_t total_span_steps, uint32_t hz)
{
  uint32_t half = (total_span_steps - app.settings.position_min) / 2U;

  tb6560_stop_steps();
  app_flip_dir_after_limit_stop();
  (void)tb6560_take_pending_move(NULL, NULL);

  if (!service_ramp_calibration_tune(half, hz))
    return false;

  app.data.current_position = total_span_steps;
  tb6560_motor_enable(false);
  app.settings.status = APS_STATUS_CALIB_OK;
  (void)eeprom_save(&app.settings);
  return true;
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
        app_flip_dir_after_limit_stop();
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

  (void)tb6560_take_pending_move(NULL, NULL);

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
      app.data.current_position    = total_steps;
      if (!service_finish_with_ramp_phase(total_steps, hz))
        calibrate_fail();
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
        app.data.current_position    = total_steps;
        if (!service_finish_with_ramp_phase(total_steps, hz))
          calibrate_fail();
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
