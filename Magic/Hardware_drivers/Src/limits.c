/*
 * limits.c
 */

#include "limits.h"
#include "main.h"

#include "stm32f3xx_hal.h"

#define LIMITS_DEBOUNCE_MS 5U

typedef struct
{
  bool prev_sample;
  uint32_t last_change_ms;
  bool stable;
} limits_debounce_t;

static limits_debounce_t s_min;
static limits_debounce_t s_max;

static bool limits_raw_low(GPIO_TypeDef *port, uint16_t pin)
{
  return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET;
}

static bool limits_debounce_step(limits_debounce_t *d, bool raw)
{
  uint32_t t = HAL_GetTick();
  if (raw != d->prev_sample)
  {
    d->prev_sample = raw;
    d->last_change_ms = t;
    return d->stable;
  }
  if ((t - d->last_change_ms) >= LIMITS_DEBOUNCE_MS)
    d->stable = raw;
  return d->stable;
}

static void limits_debounce_sync(limits_debounce_t *d, bool raw)
{
  uint32_t t = HAL_GetTick();
  d->prev_sample = raw;
  d->last_change_ms = t;
  d->stable = raw;
}

void limits_init(void)
{
  limits_debounce_sync(&s_min, limits_raw_low(LIMIT_SW_MIN_GPIO_Port, LIMIT_SW_MIN_Pin));
  limits_debounce_sync(&s_max, limits_raw_low(LIMIT_SW_MAX_GPIO_Port, LIMIT_SW_MAX_Pin));
}

void limits_update(void)
{
  limits_debounce_step(&s_min, limits_raw_low(LIMIT_SW_MIN_GPIO_Port, LIMIT_SW_MIN_Pin));
  limits_debounce_step(&s_max, limits_raw_low(LIMIT_SW_MAX_GPIO_Port, LIMIT_SW_MAX_Pin));
}

bool limits_min_engaged(void)
{
  return s_min.stable;
}

bool limits_max_engaged(void)
{
  return s_max.stable;
}
