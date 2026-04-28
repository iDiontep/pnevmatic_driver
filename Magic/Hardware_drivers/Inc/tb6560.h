/*
 * tb6560.h
 *
 * Управление драйвером BL-TB6560-V2.0 (Toshiba TB6560AHQ): CLK/STEP, CW/DIR, EN.
 * Схема входов и токи — см. https://totcnc.com/tblog/9_instruction-bl-tb6560-v2-0
 */

#ifndef TB6560_H_
#define TB6560_H_

#include <stdint.h>
#include <stdbool.h>

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Логический уровень на EN при включённом драйвере (обмотки под током).
 * На многих платах с оптронами драйвер «включён», когда через оптрон EN тока нет
 * (выход МК в состоянии, которое гасит ИК — часто это «включено» при LOW на линии к катоду).
 * Если у вас наоборот — установите в 1 перед включением tb6560.h
 * (или переопределите в препроцессоре проекта).
 */
#ifndef TB6560_EN_ASSERTED_LOW
#define TB6560_EN_ASSERTED_LOW 1
#endif

void tb6560_init(TIM_HandleTypeDef *htim_step);

void tb6560_motor_enable(bool on);

void tb6560_set_direction_forward(bool forward);

/** Режим тактирования STEP (для статуса и логики). */
typedef enum
{
  TB6560_MOTION_IDLE = 0,
  TB6560_MOTION_RUN,
  TB6560_MOTION_MOVE,
} tb6560_motion_t;

/**
 * Состояние мотора: категория MOT (USB), поля как в ответе GET MOT STAT.
 * steps_remaining обновляется из прерывания таймера — volatile.
 */
typedef struct motor
{
  bool motor_enabled;
  bool direction_forward;
  volatile tb6560_motion_t motion;
  uint32_t step_hz;
  volatile uint32_t steps_remaining;
  /** Задано при старте MOVE (диспетчеризация позиции). */
  volatile uint32_t move_steps_planned;
  /** Шаги, отданные при последнем выходе из MOVE (ISR или stop_steps); обнуляется take. */
  volatile uint32_t pending_executed_steps;
  /** Направление DIR на момент записи pending_executed_steps (для current_position). */
  volatile bool pending_dir_snap;
} motor_t;

/** Текущее состояние мотора; обновляется драйвером TB6560 и при MOT-командах по USB. */
extern motor_t motor_data;

/** Считывает и обнуляет последний завершённый MOVE; false если шагов не было. */
bool tb6560_take_pending_move(uint32_t *steps_out, bool *dir_forward_out);

/** Копия motor_data в *out (удобно для согласованного снимка всех полей). */
void tb6560_get_status(motor_t *out);

/**
 * Непрерывная генерация импульсов STEP (частота шагов на входе CLK драйвера).
 * @param hz частота в Гц; 0 — остановить PWM (двигатель можно держать включённым отдельно).
 */
void tb6560_set_step_rate_hz(uint32_t hz);

void tb6560_stop_steps(void);

/**
 * Ровно @p steps импульсов CLK при частоте @p step_hz, счёт в прерывании TIM.
 * Возврат сразу; по завершении переход в TB6560_MOTION_IDLE.
 * Новая команда RUN/MOVE/STOP отменяет текущую.
 */
void tb6560_move_steps_start(uint32_t steps, uint32_t step_hz);

/**
 * Ровно @p steps импульсов CLK при частоте @p step_hz, с подсчётом в прерывании TIM.
 * Направление задаётся до вызова через tb6560_set_direction_forward().
 * Блокирует CPU до конца перемещения (прерывания должны быть разрешены).
 */
void tb6560_move_steps_blocking(uint32_t steps, uint32_t step_hz);

#ifdef __cplusplus
}
#endif

#endif /* TB6560_H_ */
