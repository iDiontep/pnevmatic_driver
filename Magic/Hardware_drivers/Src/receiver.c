/*
 * receiver.c
 *
 *  Created on: 24 февр. 2026 г.
 *      Author: s20di
 *
 *  Обработка данных, полученных по USB CDC.
 *
 *  Протокол (согласованный контракт хост–прошивка):
 *  - Запросы: префикс EFGH, затем токены через пробел / , / =
 *  - Успех: для SET — строка «параметр значение» (см. ниже) или ответ GET
 *  - Ошибка: HGFE Error - ...\r\n
 *
 *  Категория MOT (TB6560):
 *  - EFGH SET MOT EN 0|1
 *  - EFGH SET MOT DIR 0|1|REV|FWD  (0=REV, 1=FWD)
 *  - EFGH SET MOT RUN|HZ <hz>  (синонимы; hz=0 — стоп)
 *  - EFGH SET MOT STOP
 *  - EFGH SET MOT MOVE <steps> <hz>  — неблокирующий ответ MOT MOVE <steps> <hz>
 *  - EFGH GET MOT STAT|ALL → та же строка MOT STAT … (ALL = синоним STAT)
 *
 *  При новой команде RUN / MOVE / STOP текущее движение отменяется (нет HGFE busy).
 */

#include "receiver.h"
#include "usbd_cdc_if.h"
#include "app.h"
#include "tb6560.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>

static void receiver_replyf(const char *fmt, ...)
{
  char buf[112];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n <= 0)
    return;
  if (n >= (int)sizeof buf)
    n = (int)sizeof buf - 1;
  CDC_Transmit_FS((uint8_t *)buf, (uint16_t)n);
}

/** То же условие, что strncmp(..., "EFGH", 4) после toupper в cli_process. */
static int receiver_prefix_is_efgh_ci(const char *s)
{
  return s[0] && s[1] && s[2] && s[3]
         && toupper((unsigned char)s[0]) == 'E'
         && toupper((unsigned char)s[1]) == 'F'
         && toupper((unsigned char)s[2]) == 'G'
         && toupper((unsigned char)s[3]) == 'H';
}

static void mot_reply_stat(void)
{
  motor_t st;
  tb6560_get_status(&st);

  const char *mode_str = "IDLE";
  if (st.motion == TB6560_MOTION_RUN)
    mode_str = "RUN";
  else if (st.motion == TB6560_MOTION_MOVE)
    mode_str = "MOVE";

  char resp[96];
  int n = snprintf(resp, sizeof(resp),
                   "MOT STAT EN=%u DIR=%u MODE=%s HZ=%lu REM=%lu\r\n",
                   (unsigned int)(st.motor_enabled ? 1U : 0U),
                   (unsigned int)(st.direction_forward ? 1U : 0U),
                   mode_str,
                   (unsigned long)st.step_hz,
                   (unsigned long)st.steps_remaining);
  if (n > 0)
  {
    if (n >= (int)sizeof(resp))
      n = (int)sizeof(resp) - 1;
    CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
  }
}

/* Простейший CLI-парсер по образцу uart_ble: команды начинаются с префикса EFGH */
static void cli_process(char *buffer)
{
  size_t buffer_len = strlen(buffer);
  if (buffer_len == 0)
    return;

  /* Переводим в верхний регистр */
  for (size_t i = 0; i < buffer_len; i++)
  {
    buffer[i] = (char)toupper((unsigned char)buffer[i]);
  }

  /* Проверяем префикс EFGH */
  if (buffer_len < 4 || strncmp(buffer, "EFGH", 4) != 0)
  {
    const char error_response[] = "HGFE Error - Invalid command format\r\n";
    CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
    return;
  }

  /* Сдвигаем указатель за префикс и пропускаем пробелы */
  char *cmd_buffer = buffer + 4;
  while (*cmd_buffer == ' ' || *cmd_buffer == '\t')
  {
    cmd_buffer++;
    if (*cmd_buffer == '\0')
      break;
  }

  /* Токенизация: разделители пробел, запятая, '=', CR/LF */
  char *tokens[10] = {0};
  int argc = 0;
  char *token = strtok(cmd_buffer, " ,=\r\n");
  while (token && argc < 10)
  {
    tokens[argc++] = token;
    token = strtok(NULL, " ,=\r\n");
  }

  if (argc < 2 || tokens[0] == NULL || tokens[1] == NULL)
  {
    const char error_response[] = "HGFE Error - Invalid Command\r\n";
    CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
    return;
  }

  const char *cmd = tokens[0]; /* GET / SET */
  const char *cat = tokens[1];

  /* ---------------- APS ---------------- */
  if (strcmp(cat, "APS") == 0)
  {
    if (strcmp(cmd, "SET") == 0)
    {
      if (argc >= 3 && strcmp(tokens[2], "DFLT") == 0)
      {
        app = dflt_app_params;
        receiver_replyf("APS DFLT %lu %u %lu %lu %lu\r\n",
                        (unsigned long)app.minutes,
                        (unsigned int)app.button,
                        (unsigned long)app.position_min,
                        (unsigned long)app.position_max,
                        (unsigned long)app.position_current);
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "POSITION_MIN") == 0)
      {
        unsigned long value = strtoul(tokens[3], NULL, 10);
        app.position_min = (uint32_t)value;
        receiver_replyf("APS POSITION_MIN %lu\r\n", (unsigned long)app.position_min);
        return;
      }

      const char error_response[] = "HGFE Error - Invalid SET APS command\r\n";
      CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
      return;
    }

    if (strcmp(cmd, "GET") == 0)
    {
      if (argc >= 3 && strcmp(tokens[2], "ALL") == 0)
      {
        char resp[80];
        int n = snprintf(resp, sizeof(resp), "APS ALL %lu %u %lu %lu %lu\r\n",
                         (unsigned long)app.minutes,
                         (unsigned int)app.button,
                         (unsigned long)app.position_min,
                         (unsigned long)app.position_max,
                         (unsigned long)app.position_current);
        if (n > 0)
          CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "POSITION_MIN") == 0)
      {
        char resp[64];
        int n = snprintf(resp, sizeof(resp), "APS POSITION_MIN %lu\r\n", (unsigned long)app.position_min);
        if (n > 0)
          CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
        return;
      }

      const char error_response[] = "HGFE Error - Invalid GET APS command\r\n";
      CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
      return;
    }

    const char error_response[] = "HGFE Error - Unknown APS command\r\n";
    CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
    return;
  }

  /* ---------------- MOT (TB6560) ---------------- */
  if (strcmp(cat, "MOT") == 0)
  {
    if (strcmp(cmd, "SET") == 0)
    {
      if (argc >= 4 && strcmp(tokens[2], "EN") == 0)
      {
        unsigned long v = strtoul(tokens[3], NULL, 10);
        tb6560_motor_enable(v != 0UL);
        motor_t st;
        tb6560_get_status(&st);
        receiver_replyf("MOT EN %u\r\n", (unsigned int)(st.motor_enabled ? 1U : 0U));
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "DIR") == 0)
      {
        if (strcmp(tokens[3], "FWD") == 0)
          tb6560_set_direction_forward(true);
        else if (strcmp(tokens[3], "REV") == 0)
          tb6560_set_direction_forward(false);
        else
        {
          unsigned long v = strtoul(tokens[3], NULL, 10);
          tb6560_set_direction_forward(v != 0UL);
        }
        motor_t st;
        tb6560_get_status(&st);
        receiver_replyf("MOT DIR %u\r\n", (unsigned int)(st.direction_forward ? 1U : 0U));
        return;
      }

      if (argc >= 4
          && (strcmp(tokens[2], "RUN") == 0 || strcmp(tokens[2], "HZ") == 0))
      {
        unsigned long hz = strtoul(tokens[3], NULL, 10);
        if (hz == 0UL)
          tb6560_stop_steps();
        else
          tb6560_set_step_rate_hz((uint32_t)hz);
        if (strcmp(tokens[2], "HZ") == 0)
          receiver_replyf("MOT HZ %lu\r\n", (unsigned long)hz);
        else
          receiver_replyf("MOT RUN %lu\r\n", (unsigned long)hz);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "STOP") == 0)
      {
        tb6560_stop_steps();
        receiver_replyf("MOT RUN %lu\r\n", 0UL);
        return;
      }

      if (argc >= 5 && strcmp(tokens[2], "MOVE") == 0)
      {
        unsigned long steps = strtoul(tokens[3], NULL, 10);
        unsigned long hz = strtoul(tokens[4], NULL, 10);
        if (steps == 0UL || hz == 0UL)
        {
          const char error_response[] = "HGFE Error - Invalid MOVE parameters\r\n";
          CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
          return;
        }
        tb6560_move_steps_start((uint32_t)steps, (uint32_t)hz);
        receiver_replyf("MOT MOVE %lu %lu\r\n", steps, hz);
        return;
      }

      const char error_response[] = "HGFE Error - Invalid SET MOT command\r\n";
      CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
      return;
    }

    if (strcmp(cmd, "GET") == 0)
    {
      if (argc >= 3
          && (strcmp(tokens[2], "STAT") == 0 || strcmp(tokens[2], "ALL") == 0))
      {
        mot_reply_stat();
        return;
      }

      const char error_response[] = "HGFE Error - Invalid GET MOT command\r\n";
      CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
      return;
    }

    const char error_response[] = "HGFE Error - Unknown MOT command\r\n";
    CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
    return;
  }

  const char error_response[] = "HGFE Error - Invalid Category\r\n";
  CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
}

#define RECEIVER_CMD_BUF_SZ 128

static char s_line_rx[RECEIVER_CMD_BUF_SZ];
static size_t s_line_len;

static void receiver_line_flush(void)
{
  if (s_line_len == 0U)
    return;

  char cmd[RECEIVER_CMD_BUF_SZ];
  size_t n = s_line_len;
  if (n >= sizeof(cmd))
    n = sizeof(cmd) - 1U;
  memcpy(cmd, s_line_rx, n);
  cmd[n] = '\0';
  s_line_len = 0U;

  /* Терминал может добавить пробелы; префикс часто шлют в нижнем регистре — иначе уходит в echo. */
  {
    char *p = cmd;
    while (*p == ' ' || *p == '\t')
      p++;
    if (p != cmd)
      memmove(cmd, p, strlen(p) + 1U);
  }
  if (strlen(cmd) == 0U)
    return;

  if (receiver_prefix_is_efgh_ci(cmd))
  {
    cli_process(cmd);
    return;
  }

  /* SET APS DFLT */
  if (strncmp(cmd, "SET APS DFLT", 12) == 0)
  {
    app = dflt_app_params;
    receiver_replyf("APS DFLT %lu %u %lu %lu %lu\r\n",
                    (unsigned long)app.minutes,
                    (unsigned int)app.button,
                    (unsigned long)app.position_min,
                    (unsigned long)app.position_max,
                    (unsigned long)app.position_current);
    return;
  }

  if (strncmp(cmd, "SET APS POSITION_MIN ", 22) == 0)
  {
    char *p = cmd + 22;
    unsigned long value = strtoul(p, NULL, 10);
    app.position_min = (uint32_t)value;
    receiver_replyf("APS POSITION_MIN %lu\r\n", (unsigned long)app.position_min);
    return;
  }

  if (strncmp(cmd, "GET APS ALL", 11) == 0)
  {
    char resp[80];
    int n = snprintf(resp, sizeof(resp), "APS ALL %lu %u %lu %lu %lu\r\n",
                     (unsigned long)app.minutes,
                     (unsigned int)app.button,
                     (unsigned long)app.position_min,
                     (unsigned long)app.position_max,
                     (unsigned long)app.position_current);
    if (n > 0)
      CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
    return;
  }

  if (strncmp(cmd, "GET APS POSITION_MIN", 21) == 0)
  {
    char resp[64];
    int n = snprintf(resp, sizeof(resp), "APS POSITION_MIN %lu\r\n", (unsigned long)app.position_min);
    if (n > 0)
      CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
    return;
  }

  /* MOT: короткая строка → тот же разбор, что и для EFGH … */
  if (strncmp(cmd, "SET MOT ", 8) == 0 || strncmp(cmd, "GET MOT ", 8) == 0)
  {
    char wrapped[RECEIVER_CMD_BUF_SZ + 8];
    int n = snprintf(wrapped, sizeof(wrapped), "EFGH %s", cmd);
    if (n > 0 && n < (int)sizeof(wrapped))
    {
      cli_process(wrapped);
      return;
    }
  }

  CDC_Transmit_FS((uint8_t *)cmd, (uint16_t)strlen(cmd));
}

/**
 * USB CDC часто вызывает приём по одному байту или коротким фрагментам: без сборки строки
 * приходит только "E", затем "F", … — каждый фрагмент уходил в echo; на ПК склеивалось в EFGHGETMOTSTAT.
 * Собираем до '\r' или '\n' (как терминал), затем разбираем одну команду.
 */
void Receiver_OnData(uint8_t *buf, uint32_t len)
{
  if (!buf || len == 0)
    return;

  for (uint32_t i = 0; i < len; i++)
  {
    uint8_t c = buf[i];
    if (c == '\r' || c == '\n')
    {
      receiver_line_flush();
      continue;
    }
    if (s_line_len + 1U >= RECEIVER_CMD_BUF_SZ)
      receiver_line_flush();
    if (s_line_len + 1U < RECEIVER_CMD_BUF_SZ)
      s_line_rx[s_line_len++] = (char)c;
  }
}
