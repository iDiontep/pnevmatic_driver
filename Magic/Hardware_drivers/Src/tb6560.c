/*
 * tb6560.c
 *
 * TIM2 CH1 (PA0) — тактовый выход к CLK+/-; PA1 — DIR (CW); PA2 — EN.
 */

#include "tb6560.h"

static TIM_HandleTypeDef *s_htim;
static volatile uint32_t s_steps_remaining;

static bool s_motor_enabled;
static bool s_dir_forward;
static tb6560_motion_t s_motion;
static uint32_t s_step_hz;

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

void tb6560_get_status(tb6560_status_t *out)
{
  if (!out)
    return;
  out->motor_enabled = s_motor_enabled;
  out->direction_forward = s_dir_forward;
  out->motion = s_motion;
  out->step_hz = s_step_hz;
  out->steps_remaining = s_steps_remaining;
}

void tb6560_init(TIM_HandleTypeDef *htim_step)
{
  s_htim = htim_step;
  s_steps_remaining = 0;
  s_motor_enabled = false;
  s_dir_forward = true;
  s_motion = TB6560_MOTION_IDLE;
  s_step_hz = 0U;

  tb6560_motor_enable(false);
  tb6560_set_direction_forward(true);
  tb6560_stop_steps();
}

void tb6560_motor_enable(bool on)
{
  s_motor_enabled = on;
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
  s_dir_forward = forward;
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

  s_steps_remaining = 0;
  s_motion = TB6560_MOTION_IDLE;
  s_step_hz = 0U;
}

void tb6560_set_step_rate_hz(uint32_t hz)
{
  if (!s_htim || !hz)
  {
    tb6560_stop_steps();
    return;
  }

  __HAL_TIM_DISABLE_IT(s_htim, TIM_IT_UPDATE);
  s_steps_remaining = 0;

  apply_step_timing(hz);
  HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);

  s_motion = TB6560_MOTION_RUN;
  s_step_hz = hz;
}

static void move_steps_begin(uint32_t steps, uint32_t step_hz)
{
  __HAL_TIM_DISABLE_IT(s_htim, TIM_IT_UPDATE);
  apply_step_timing(step_hz);

  s_steps_remaining = steps;
  s_motion = TB6560_MOTION_MOVE;
  s_step_hz = step_hz;

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

  while (s_steps_remaining > 0U)
  {
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim != s_htim || s_steps_remaining == 0U)
    return;

  s_steps_remaining--;
  if (s_steps_remaining == 0U)
  {
    HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_1);
    __HAL_TIM_DISABLE_IT(s_htim, TIM_IT_UPDATE);
    s_motion = TB6560_MOTION_IDLE;
    s_step_hz = 0U;
  }
}
