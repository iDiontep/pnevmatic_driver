/*
 * app.c
 *
 *  Основной цикл приложения.
 *  Управление TB6560 — по USB CDC (категория MOT в receiver.c).
 */

#include "app.h"
#include "limits.h"
#include "tb6560.h"

app_t app = {0};

const app_t dflt_app_params = {
    .settings =
        {
            .status        = 0x80085U,
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

void app_process(void)
{
  limits_update();

  if (motor_data.motion == TB6560_MOTION_IDLE)
    return;

  const int32_t dir = app.settings.position_dir;
  const bool    fwd = motor_data.direction_forward;

  const bool toward_logical_min =
      (dir >= 0) ? !fwd : fwd;
  const bool toward_logical_max =
      (dir >= 0) ? fwd : !fwd;

  if (limits_logical_min_engaged() && toward_logical_min)
    tb6560_stop_steps();
  else if (limits_logical_max_engaged() && toward_logical_max)
    tb6560_stop_steps();
}
