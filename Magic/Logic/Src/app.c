/*
 * app.c
 *
 *  Основной цикл приложения.
 *  Управление TB6560 — по USB CDC (категория MOT в receiver.c).
 */

#include "app.h"
#include "limits.h"
#include "tb6560.h"

#include <stdint.h>

app_t app = {0};

const app_t dflt_app_params = {
    .settings =
        {
            .status = APS_STATUS_DFLT,
            .position_min  = 0U,
            .position_max  = 0U,
            .position_dir  = 1,
            .motor_speed   = 1000U,
        },
    .data =
        {
            .minutes          = 0U,
            .button           = 0U,
            .current_position = 0U,
        },
};

static void app_apply_executed_physical_steps(uint32_t steps, bool motor_dir_fwd)
{
  if (steps == 0U)
    return;

  uint32_t pmin = app.settings.position_min;
  uint32_t pmax = app.settings.position_max;

  bool logical_incr =
      (app.settings.position_dir >= 0) ? motor_dir_fwd : !motor_dir_fwd;

  uint64_t cur = (uint64_t)app.data.current_position;
  uint64_t pmi = (uint64_t)pmin;
  uint64_t pmx = (uint64_t)pmax;
  uint64_t st = (uint64_t)steps;

  if (logical_incr)
  {
    uint64_t nxt = cur + st;
    if (nxt < pmi)
      nxt = pmi;
    if (nxt > pmx)
      nxt = pmx;
    app.data.current_position = (uint32_t)nxt;
  }
  else
  {
    uint64_t nxt = (cur > st) ? (cur - st) : 0ULL;
    if (nxt < pmi)
      nxt = pmi;
    if (nxt > pmx)
      nxt = pmx;
    app.data.current_position = (uint32_t)nxt;
  }
}

void app_flip_dir_after_limit_stop(void)
{
  tb6560_set_direction_forward(!motor_data.direction_forward);
}

void app_process(void)
{
  limits_update();

  uint32_t done;
  bool     dir_fwd;
  if (tb6560_take_pending_move(&done, &dir_fwd) && app.settings.status == APS_STATUS_CALIB_OK
      && app.settings.position_max > app.settings.position_min)
    app_apply_executed_physical_steps(done, dir_fwd);

  if (motor_data.motion == TB6560_MOTION_IDLE)
    return;

  const int32_t dir = app.settings.position_dir;
  const bool    fwd = motor_data.direction_forward;

  const bool toward_logical_min =
      (dir >= 0) ? !fwd : fwd;
  const bool toward_logical_max =
      (dir >= 0) ? fwd : !fwd;

  if (limits_logical_min_engaged() && toward_logical_min)
  {
    tb6560_stop_steps();
    app_flip_dir_after_limit_stop();
  }
  else if (limits_logical_max_engaged() && toward_logical_max)
  {
    tb6560_stop_steps();
    app_flip_dir_after_limit_stop();
  }
}
