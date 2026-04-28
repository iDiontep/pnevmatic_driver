/*
 * receiver.h
 *
 *  Created on: 24 февр. 2026 г.
 *      Author: s20di
 *
 *  Приём и разбор команд по USB CDC (виртуальный COM).
 *
 *  === Общий формат ===
 *  - Строка ASCII; конец команды: CR и/или LF (если данные приходят фрагментами,
 *    без завершения строки разбор не выполняется).
 *  - Команды с префиксом EFGH (регистр префикса до разбора не важен); остальная
 *    часть строки перед разбором приводится к верхнему регистру.
 *  - Токены после EFGH разделяются пробелом, запятой или '='.
 *  - Успех: для SET — строка с именем параметра и значением (+ CRLF); для GET — см. ниже.
 *  - Ошибка: HGFE Error - ... и перевод строки.
 *  - Строка без распознанного префикса EFGH в начале (после trim) — эхо обратно на CDC.
 *  - Максимальная длина одной принятой строки ограничена буфером в Receiver_OnData (см. receiver.c).
 *
 *  === Категория APS (настройки, app.settings) ===
 *  EFGH SET APS DFLT
 *      — сброс настроек к дефолту; ответ: APS DFLT <status> <position_min> <position_max> <position_dir> <motor_speed> + CRLF
 *  EFGH GET APS ALL
 *      — ответ: APS ALL с теми же пятью полями (порядок: status первым) + CRLF
 *  EFGH SET APS POSITION_MIN|POSITION_MAX|MOTOR_SPEED|STATUS <число>
 *  EFGH SET APS POSITION_DIR 1|-1  (только 1 или -1; иначе HGFE Error)
 *  EFGH GET APS POSITION_MIN|POSITION_MAX|POSITION_DIR|MOTOR_SPEED|STATUS
 *
 *  === Категория APD (данные, app.data) ===
 *  EFGH SET APD DFLT
 *      — сброс данных к дефолту; ответ: APD DFLT <minutes> <button> <current_position> + CRLF
 *  EFGH GET APD ALL
 *      — ответ: APD ALL <minutes> <button> <current_position> + CRLF
 *  EFGH SET APD MINUTES|BUTTON|CURRENT_POSITION <число>
 *  EFGH GET APD MINUTES|BUTTON|CURRENT_POSITION
 *
 *  === Категория MOT (драйвер TB6560) ===
 *  Состояние: motor_data / tb6560_get_status().
 *  EFGH SET MOT EN 0|1          — ответ: MOT EN <0|1> + CRLF
 *  EFGH SET MOT DIR 0|1|REV|FWD — ответ: MOT DIR <0|1> + CRLF
 *  EFGH SET MOT RUN <X>        — поезд к абсолютной логической позиции X (концы APS POSITION_MIN/MAX);
 *                                ответ: MOT RUN <текущая позиция после остановки> + CRLF; APS должен быть откалиброван
 *  EFGH SET MOT HZ <hz>        — непрерывное вращение на hz Гц; ответ: MOT HZ <hz> (hz=0 — стоп такта)
 *  EFGH SET MOT STOP           — ответ: MOT RUN 0 + CRLF
 *  EFGH SET MOT MOVE <steps> <hz> — ответ: MOT MOVE <steps> <hz> + CRLF; steps и hz > 0
 *  EFGH GET MOT STAT | ALL    — ответ:
 *      MOT STAT EN=<0|1> DIR=<0|1> MODE=IDLE|RUN|MOVE HZ=<гц> REM=<осталось шагов> + CRLF
 *
 *  Новая RUN / MOVE / STOP отменяет текущее движение (отдельного busy нет).
 */

#ifndef RECEIVER_H_
#define RECEIVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Разбор принятых по USB: только EFGH … → cli_process; иначе эхо.
 * @param buf  Буфер данных (может не быть NUL-terminated).
 * @param len  Число байт
 */
void Receiver_OnData(uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* RECEIVER_H_ */
