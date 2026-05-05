/*
 * tb6560.c
 *
 * TIM2 CH1 (PA0) — тактовый выход к CLK+/-; PA1 — DIR (CW); PA2 — EN.
 */

#include "tb6560.h"

static TIM_HandleTypeDef *s_htim;

/** Активный профиль разгона MOVE (APS / tb6560_set_move_ramp). */
static uint32_t s_move_ramp_iv  = TB6560_RAMP_STEP_INTERVAL;
static uint32_t s_move_ramp_dhz = TB6560_RAMP_HZ_STEP;
static uint32_t s_move_ramp_min = TB6560_RAMP_MIN_HZ;

void tb6560_set_move_ramp(uint32_t step_interval, uint32_t hz_step, uint32_t min_hz)
{
  if (step_interval == 0U)
    step_interval = TB6560_RAMP_STEP_INTERVAL;
  if (hz_step == 0U)
    hz_step = TB6560_RAMP_HZ_STEP;
  if (min_hz == 0U)
    min_hz = TB6560_RAMP_MIN_HZ;

  if (step_interval > 500U)
    step_interval = 500U;
  if (hz_step > 500U)
    hz_step = 500U;
  if (min_hz > 100000U)
    min_hz = 100000U;

  s_move_ramp_iv  = step_interval;
  s_move_ramp_dhz = hz_step;
  s_move_ramp_min = min_hz;
}

motor_t motor_data = {
    .motor_enabled = false,
    .direction_forward = true,
    .motion = TB6560_MOTION_IDLE,
    .step_hz = 0U,
    .steps_remaining = 0U,
    .move_steps_planned = 0U,
    .ramp_peak_hz = 0U,
    .ramp_accel_steps = 0U,
    .ramp_decel_steps = 0U,
    .ramp_flat_move = false,
    .pending_executed_steps = 0U,
    .pending_dir_snap = false,
};

bool tb6560_take_pending_move(uint32_t *steps_out, bool *dir_forward_out)
{
  uint32_t v;
  bool     d;
  __disable_irq();
  v                                 = motor_data.pending_executed_steps;
  d                                 = motor_data.pending_dir_snap;
  motor_data.pending_executed_steps = 0U;
  motor_data.pending_dir_snap       = false;
  __enable_irq();
  if (v == 0U)
    return false;
  if (steps_out != NULL)
    *steps_out = v;
  if (dir_forward_out != NULL)
    *dir_forward_out = d;
  return true;
}

static uint32_t tim2_cntclk_hz(void)
{
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  /* Таймеры APB1: при делителе APB1 != 1 такт таймера = 2 * PCLK1 (STM32F3). */
  if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
  {
    return pclk1 * 2U;
  }
  return pclk1;
}

/** PSC/ARR/CCR для STEP при заданной частоте; PWM остановлен, таймер отключён. */
static void tim_configure_pwm_hz(uint32_t hz)
{
  if (!s_htim || hz == 0U)
    return;

  uint64_t timclk = tim2_cntclk_hz();
  uint64_t period_counts = timclk / (uint64_t)hz;
  if (period_counts < 2ULL)
    period_counts = 2ULL;

  uint32_t psc = (uint32_t)((period_counts - 1ULL) / 65536ULL);
  if (psc > 0xFFFFu)
    psc = 0xFFFFu;

  uint32_t arr = (uint32_t)(period_counts / (uint64_t)(psc + 1U)) - 1U;
  if (arr > 0xFFFFFFFFu)
    arr = 0xFFFFFFFFu;

  HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_1);
  __HAL_TIM_DISABLE(s_htim);

  s_htim->Instance->PSC = psc;
  s_htim->Instance->ARR = arr;
  s_htim->Instance->EGR = TIM_EGR_UG;

  uint32_t pulse = (arr + 1U) / 2U;
  if (pulse == 0U)
    pulse = 1U;
  __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, pulse);
}

/** После tim_configure_pwm_hz: возобновить PWM и UPDATE IRQ для MOVE. */
static void tim_restart_pwm_move_it(void)
{
  __HAL_TIM_CLEAR_FLAG(s_htim, TIM_SR_UIF);
  HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
}

/** Задать PSC/ARR и скважность ~50 % без запуска таймера */
static void apply_step_timing(uint32_t hz)
{
  tim_configure_pwm_hz(hz);
}

/**
 * Профиль MOVE: accel_steps / decel_steps кратны активному step_interval или нули.
 * При коротком ходе — усечённый треугольник (меньший peak_hz).
 */
static void move_compute_ramp(uint32_t planned, uint32_t target_hz,
                              uint32_t *accel_out, uint32_t *decel_out,
                              uint32_t *peak_out, bool *flat_out)
{
  const uint32_t min_hz = s_move_ramp_min;
  const uint32_t delta  = s_move_ramp_dhz;
  const uint32_t iv     = s_move_ramp_iv;

  if (target_hz <= min_hz)
  {
    *accel_out = 0U;
    *decel_out = 0U;
    *peak_out  = target_hz;
    *flat_out  = true;
    return;
  }

  uint32_t tiers_full     = (target_hz - min_hz + delta - 1U) / delta;
  uint32_t full_ramp_steps = tiers_full * iv;

  if (planned >= 2U * full_ramp_steps && full_ramp_steps > 0U)
  {
    *accel_out = full_ramp_steps;
    *decel_out = full_ramp_steps;
    *peak_out  = target_hz;
    *flat_out  = false;
    return;
  }

  uint32_t tiers_half = (planned / 2U) / iv;
  if (tiers_half > tiers_full)
    tiers_half = tiers_full;

  if (tiers_half == 0U)
  {
    *accel_out = 0U;
    *decel_out = 0U;
    *peak_out  = target_hz;
    *flat_out  = true;
    return;
  }

  uint32_t ramp_steps = tiers_half * iv;
  *accel_out          = ramp_steps;
  *decel_out          = ramp_steps;
  uint32_t peak       = min_hz + tiers_half * delta;
  if (peak > target_hz)
    peak = target_hz;
  *peak_out = peak;
  *flat_out = false;
}

static void ramp_reset_fields(void)
{
  motor_data.ramp_peak_hz      = 0U;
  motor_data.ramp_accel_steps  = 0U;
  motor_data.ramp_decel_steps  = 0U;
  motor_data.ramp_flat_move    = false;
}

void tb6560_get_status(motor_t *out)
{
  if (!out)
    return;
  *out = motor_data;
}

void tb6560_init(TIM_HandleTypeDef *htim_step)
{
  s_htim = htim_step;

  motor_data.motor_enabled = false;
  motor_data.direction_forward = true;
  motor_data.motion = TB6560_MOTION_IDLE;
  motor_data.step_hz = 0U;
  motor_data.steps_remaining      = 0U;
  motor_data.move_steps_planned       = 0U;
  motor_data.pending_executed_steps   = 0U;
  motor_data.pending_dir_snap         = false;
  ramp_reset_fields();
  tb6560_set_direction_forward(true);
  tb6560_stop_steps();
}

void tb6560_motor_enable(bool on)
{
  motor_data.motor_enabled = on;
#if TB6560_EN_ASSERTED_LOW
  HAL_GPIO_WritePin(TB6560_EN_GPIO_Port, TB6560_EN_Pin,
                    on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#else
  HAL_GPIO_WritePin(TB6560_EN_GPIO_Port, TB6560_EN_Pin,
                    on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#endif
}

void tb6560_set_direction_forward(bool forward)
{
  motor_data.direction_forward = forward;
  HAL_GPIO_WritePin(TB6560_DIR_GPIO_Port, TB6560_DIR_Pin,
                    forward ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void tb6560_stop_steps(void)
{
  if (!s_htim)
    return;

  __HAL_TIM_DISABLE_IT(s_htim, TIM_IT_UPDATE);
  HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_1);
  __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, 0);

  uint32_t rem_done = motor_data.steps_remaining;
  if (motor_data.motion == TB6560_MOTION_MOVE && motor_data.move_steps_planned > 0U)
  {
    motor_data.pending_executed_steps = motor_data.move_steps_planned - rem_done;
    motor_data.pending_dir_snap       = motor_data.direction_forward;
  }

  motor_data.steps_remaining = 0;
  motor_data.motion = TB6560_MOTION_IDLE;
  motor_data.step_hz = 0U;
  ramp_reset_fields();
}

void tb6560_set_step_rate_hz(uint32_t hz)
{
  if (!s_htim || !hz)
  {
    tb6560_stop_steps();
    return;
  }

  __HAL_TIM_DISABLE_IT(s_htim, TIM_IT_UPDATE);
  motor_data.steps_remaining = 0;

  apply_step_timing(hz);
  HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);

  motor_data.motion = TB6560_MOTION_RUN;
  motor_data.step_hz = hz;
  ramp_reset_fields();
}

static void move_steps_begin(uint32_t steps, uint32_t step_hz_target)
{
  uint32_t accel;
  uint32_t decel;
  uint32_t peak_hz;
  bool     flat;

  move_compute_ramp(steps, step_hz_target, &accel, &decel, &peak_hz, &flat);

  __HAL_TIM_DISABLE_IT(s_htim, TIM_IT_UPDATE);

  uint32_t start_hz;
  if (flat)
  {
    start_hz                   = peak_hz;
    motor_data.ramp_flat_move  = true;
    motor_data.ramp_peak_hz    = peak_hz;
    motor_data.ramp_accel_steps = 0U;
    motor_data.ramp_decel_steps = 0U;
  }
  else
  {
    start_hz                   = s_move_ramp_min;
    motor_data.ramp_flat_move  = false;
    motor_data.ramp_peak_hz    = peak_hz;
    motor_data.ramp_accel_steps = accel;
    motor_data.ramp_decel_steps = decel;
  }

  tim_configure_pwm_hz(start_hz);
  motor_data.step_hz = start_hz;

  motor_data.steps_remaining = steps;
  motor_data.motion = TB6560_MOTION_MOVE;
  motor_data.move_steps_planned       = steps;
  motor_data.pending_executed_steps   = 0U;
  motor_data.pending_dir_snap         = false;
  __HAL_TIM_CLEAR_FLAG(s_htim, TIM_SR_UIF);
  __HAL_TIM_ENABLE_IT(s_htim, TIM_IT_UPDATE);
  tim_restart_pwm_move_it();
}

void tb6560_move_steps_start(uint32_t steps, uint32_t step_hz)
{
  if (!s_htim || steps == 0U || step_hz == 0U)
    return;

  move_steps_begin(steps, step_hz);
}

void tb6560_move_steps_blocking(uint32_t steps, uint32_t step_hz)
{
  if (!s_htim || steps == 0U || step_hz == 0U)
    return;

  move_steps_begin(steps, step_hz);

  while (motor_data.steps_remaining > 0U)
  {
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim != s_htim || motor_data.steps_remaining == 0U)
    return;

  motor_data.steps_remaining--;

  if (motor_data.steps_remaining == 0U)
  {
    HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_1);
    __HAL_TIM_DISABLE_IT(s_htim, TIM_IT_UPDATE);
    motor_data.pending_executed_steps = motor_data.move_steps_planned;
    motor_data.pending_dir_snap       = motor_data.direction_forward;
    motor_data.motion = TB6560_MOTION_IDLE;
    motor_data.step_hz = 0U;
    ramp_reset_fields();
    return;
  }

  if (motor_data.ramp_flat_move)
    return;

  const uint32_t planned = motor_data.move_steps_planned;
  const uint32_t rem     = motor_data.steps_remaining;
  const uint32_t exec    = planned - rem;

  const uint32_t accel_s = motor_data.ramp_accel_steps;
  const uint32_t decel_s = motor_data.ramp_decel_steps;
  const uint32_t peak    = motor_data.ramp_peak_hz;
  const uint32_t iv      = s_move_ramp_iv;
  const uint32_t delta   = s_move_ramp_dhz;
  const uint32_t min_hz  = s_move_ramp_min;

  const bool in_accel = (exec <= accel_s);
  const bool in_decel = (rem <= decel_s);
  const bool in_cruise = (!in_accel && !in_decel);

  if (in_cruise)
  {
    if (motor_data.step_hz != peak)
    {
      tim_configure_pwm_hz(peak);
      motor_data.step_hz = peak;
      tim_restart_pwm_move_it();
    }
    return;
  }

  if (in_accel)
  {
    if ((exec % iv) == 0U && exec > 0U)
    {
      uint32_t hz = motor_data.step_hz + delta;
      if (hz > peak)
        hz = peak;
      if (hz != motor_data.step_hz)
      {
        tim_configure_pwm_hz(hz);
        motor_data.step_hz = hz;
        tim_restart_pwm_move_it();
      }
    }
    return;
  }

  /* торможение */
  if (in_decel && (rem % iv) == 0U && rem < decel_s)
  {
    uint32_t hz = motor_data.step_hz;
    if (hz > min_hz + delta)
      hz -= delta;
    else
      hz = min_hz;

    if (hz != motor_data.step_hz)
    {
      tim_configure_pwm_hz(hz);
      motor_data.step_hz = hz;
      tim_restart_pwm_move_it();
    }
  }
}
