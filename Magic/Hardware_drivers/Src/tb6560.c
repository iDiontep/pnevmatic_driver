/*
 * tb6560.c
 *
 * TIM2 CH1 (PA0) — тактовый выход к CLK+/-; PA1 — DIR (CW); PA2 — EN.
 */

#include "tb6560.h"

static TIM_HandleTypeDef *s_htim;

motor_t motor_data = {
    .motor_enabled = false,
    .direction_forward = true,
    .motion = TB6560_MOTION_IDLE,
    .step_hz = 0U,
    .steps_remaining = 0U,
    .move_steps_planned = 0U,
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

/** Задать PSC/ARR и скважность ~50 % без запуска таймера */
static void apply_step_timing(uint32_t hz)
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
}

static void move_steps_begin(uint32_t steps, uint32_t step_hz)
{
  __HAL_TIM_DISABLE_IT(s_htim, TIM_IT_UPDATE);
  apply_step_timing(step_hz);

  motor_data.steps_remaining = steps;
  motor_data.motion = TB6560_MOTION_MOVE;
  motor_data.step_hz = step_hz;
  motor_data.move_steps_planned       = steps;
  motor_data.pending_executed_steps   = 0U;
  motor_data.pending_dir_snap         = false;
  __HAL_TIM_CLEAR_FLAG(s_htim, TIM_SR_UIF);
  __HAL_TIM_ENABLE_IT(s_htim, TIM_IT_UPDATE);
  HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
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
  }
}
