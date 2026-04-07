/*
 * receiver.h
 *
 *  Created on: 24 февр. 2026 г.
 *      Author: s20di
 *
 *  Приём и разбор команд по USB CDC (виртуальный COM).
 *
 *  === Общий формат ===
 *  - Строка ASCII, конец строки: CR и/или LF (как в терминале).
 *  - Разбор для ветки CLI: после префикса EFGH токены разделяются пробелом, запятой или '='.
 *  - Для команд с префиксом EFGH буквы в строке приводятся к верхнему регистру перед разбором.
 *  - Успех: ответ OK с переводом строки (CRLF) или строка ответа GET (ниже).
 *  - Ошибка: строка с префиксом HGFE Error - ... и переводом строки.
 *  - Если строка не подошла ни под один известный шаблон — данные отправляются обратно (эхо).
 *  - Максимальная длина одной принятой строки ограничена буфером в Receiver_OnData (см. receiver.c).
 *
 *  === Категория APS (параметры приложения app_t) ===
 *  Команды с префиксом EFGH:
 *    EFGH SET APS DFLT              — сброс app к значениям по умолчанию
 *    EFGH SET APS PARAM1 <число>  — запись app.param1
 *    EFGH GET APS ALL             — ответ: APS ALL <minutes> <button> <param1> <param2> <param3> + CRLF
 *    EFGH GET APS PARAM           — ответ: APS PARAM1 <param1> + CRLF
 *
 *  Устаревший ввод без префикса EFGH (короткие строки, как раньше):
 *    SET APS DFLT
 *    SET APS PARAM1 <число>
 *    GET APS ALL
 *    GET APS PARAM
 *
 *  === Категория MOT (драйвер TB6560 по USB) ===
 *  Только с префиксом EFGH:
 *    EFGH SET MOT EN 0|1          — выкл/вкл обмотки (motor enable)
 *    EFGH SET MOT DIR 0|1|REV|FWD — направление: 0 или REV = назад, 1 или FWD = вперёд
 *                                   (логика «вперёд» — как в tb6560_set_direction_forward)
 *    EFGH SET MOT RUN <hz>       — непрерывные импульсы STEP с частотой hz [Гц]; hz=0 эквивалентно STOP
 *    EFGH SET MOT STOP           — останов такта (без снятия EN, если он был включён)
 *    EFGH SET MOT MOVE <steps> <hz> — ровно steps импульсов с частотой hz; ответ OK сразу (неблокирующий
 *                                   режим на прошивке); steps и hz должны быть > 0, иначе HGFE Error
 *    EFGH GET MOT STAT           — ответ одной строкой:
 *                                   MOT STAT EN=<0|1> DIR=<0|1> MODE=IDLE|RUN|MOVE HZ=<гц> REM=<осталось шагов> + CRLF
 *                                   REM > 0 только в режиме MOVE; в IDLE обычно HZ=0.
 *
 *  Поведение при смене команды: новая RUN / MOVE / STOP отменяет текущее движение (отдельного кода busy нет).
 */

#ifndef RECEIVER_H_
#define RECEIVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Разбор принятых по USB данных: CLI (EFGH …), короткие APS, иначе эхо.
 * @param buf  Указатель на буфер с данными (как прислал хост; может не быть NUL-terminated).
 * @param len  Количество байт
 */
void Receiver_OnData(uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* RECEIVER_H_ */
