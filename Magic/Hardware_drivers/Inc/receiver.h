/*
 * receiver.h
 *
 *  Created on: 24 февр. 2026 г.
 *      Author: s20di
 *
 *  Приём и разбор команд по USB CDC (виртуальный COM).
 *
 *  === Общий формат ===
 *  - Строка ASCII; конец команды: CR и/или LF (обязательно, если хост шлёт данные
 *    по одному байту или короткими USB-пакетами — иначе прошивка ждёт продолжения строки).
 *  - Разбор для ветки CLI: после префикса EFGH токены разделяются пробелом, запятой или '='.
 *  - Для команд с префиксом EFGH буквы в строке приводятся к верхнему регистру перед разбором;
 *    сам префикс при приёме по USB допускается в любом регистре (efgh … тоже CLI, не echo).
 *  - Успех: для SET — строка с именем параметра и новым значением (+ CRLF); для GET — как ниже.
 *  - Ошибка: строка с префиксом HGFE Error - ... и переводом строки.
 *  - Если строка не подошла ни под один известный шаблон — данные отправляются обратно (эхо).
 *  - Максимальная длина одной принятой строки ограничена буфером в Receiver_OnData (см. receiver.c).
 *
 *  === Категория APS (параметры приложения app_t) ===
 *  Команды с префиксом EFGH:
 *    EFGH SET APS DFLT              — ответ: APS DFLT <minutes> <button> <param1> <param2> <param3> + CRLF
 *    EFGH SET APS PARAM1 <число>  — ответ: APS PARAM1 <param1> + CRLF
 *    EFGH GET APS ALL             — ответ: APS ALL <minutes> <button> <param1> <param2> <param3> + CRLF
 *    EFGH GET APS PARAM           — ответ: APS PARAM1 <param1> + CRLF
 *
 *  Устаревший ввод без префикса EFGH (короткие строки, как раньше):
 *    SET APS DFLT
 *    SET APS PARAM1 <число>
 *    GET APS ALL
 *    GET APS PARAM
 *  MOT без префикса (те же подкоманды, что после EFGH MOT):
 *    SET MOT EN 0|1
 *    SET MOT DIR 0|1|REV|FWD
 *    SET MOT RUN <hz> | SET MOT HZ <hz>
 *    SET MOT STOP
 *    SET MOT MOVE <steps> <hz>
 *    GET MOT STAT | GET MOT ALL
 *
 *  === Категория MOT (драйвер TB6560 по USB) ===
 *  Текущее состояние: глобальный motor_data (tb6560.h); tb6560_get_status() — копия в буфер хоста.
 *  С префиксом EFGH (или короткий ввод SET MOT … / GET MOT … — внутри сводится к EFGH):
 *    EFGH SET MOT EN 0|1          — ответ: MOT EN <0|1> + CRLF
 *    EFGH SET MOT DIR 0|1|REV|FWD — ответ: MOT DIR <0|1> + CRLF
 *    EFGH SET MOT RUN <hz>       — ответ: MOT RUN <hz> + CRLF (hz=0 — стоп такта)
 *    EFGH SET MOT HZ <hz>        — то же, что RUN; ответ: MOT HZ <hz> + CRLF
 *    EFGH SET MOT STOP           — ответ: MOT RUN 0 + CRLF
 *    EFGH SET MOT MOVE <steps> <hz> — ответ: MOT MOVE <steps> <hz> + CRLF (неблокирующий старт);
 *                                   steps и hz должны быть > 0, иначе HGFE Error
 *    EFGH GET MOT STAT | ALL    — ответ одной строкой (ALL то же, что STAT):
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
