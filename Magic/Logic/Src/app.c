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

const app_t dflt_app_params = {0, 0, 0, 0, 0};

void app_process(void)
{
  limits_update();

  if (motor_data.motion == TB6560_MOTION_IDLE)
    return;

  if (limits_min_engaged() && !motor_data.direction_forward)
    tb6560_stop_steps();
  else if (limits_max_engaged() && motor_data.direction_forward)
    tb6560_stop_steps();
}
