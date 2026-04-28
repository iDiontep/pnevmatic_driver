/*
 * receiver.c
 *
 *  Created on: 24 февр. 2026 г.
 *      Author: s20di
 *
 *  Обработка данных, полученных по USB CDC.
 *
 *  Протокол (согласованный контракт хост–прошивка):
 *  - Разбор только через cli_process после префикса EFGH (в любом регистре).
 *  - Запросы: токены через пробел / , / =
 *  - Успех: для SET — строка «параметр значение» (см. receiver.h) или ответ GET
 *  - Ошибка: HGFE Error - ...\r\n
 *
 *  Категории: APS (настройки), APD (данные), MOT (TB6560) — см. receiver.h.
 *
 *  При новой команде RUN / MOVE / STOP текущее движение отменяется (нет HGFE busy).
 *  MOT RUN — переезд к координате; непрерывное — MOT HZ.
 */

#include "receiver.h"
#include "usbd_cdc_if.h"
#include "app.h"
#include "limits.h"
#include "tb6560.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>

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

  /* ---------------- APS (настройки) ---------------- */
  if (strcmp(cat, "APS") == 0)
  {
    if (strcmp(cmd, "SET") == 0)
    {
      if (argc >= 3 && strcmp(tokens[2], "DFLT") == 0)
      {
        app.settings = dflt_app_params.settings;
        receiver_replyf("APS DFLT %lu %lu %lu %ld %lu\r\n",
                        (unsigned long)app.settings.status,
                        (unsigned long)app.settings.position_min,
                        (unsigned long)app.settings.position_max,
                        (long)app.settings.position_dir,
                        (unsigned long)app.settings.motor_speed);
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "POSITION_MIN") == 0)
      {
        unsigned long value = strtoul(tokens[3], NULL, 10);
        app.settings.position_min = (uint32_t)value;
        receiver_replyf("APS POSITION_MIN %lu\r\n", (unsigned long)app.settings.position_min);
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "POSITION_MAX") == 0)
      {
        unsigned long value = strtoul(tokens[3], NULL, 10);
        app.settings.position_max = (uint32_t)value;
        receiver_replyf("APS POSITION_MAX %lu\r\n", (unsigned long)app.settings.position_max);
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "POSITION_DIR") == 0)
      {
        long v = strtol(tokens[3], NULL, 10);
        if (v != 1L && v != -1L)
        {
          const char error_response[] = "HGFE Error - POSITION_DIR must be 1 or -1\r\n";
          CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
          return;
        }
        app.settings.position_dir = (int32_t)v;
        receiver_replyf("APS POSITION_DIR %ld\r\n", (long)app.settings.position_dir);
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "MOTOR_SPEED") == 0)
      {
        unsigned long value = strtoul(tokens[3], NULL, 10);
        app.settings.motor_speed = (uint32_t)value;
        receiver_replyf("APS MOTOR_SPEED %lu\r\n", (unsigned long)app.settings.motor_speed);
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "STATUS") == 0)
      {
        unsigned long value = strtoul(tokens[3], NULL, 0);
        app.settings.status = (uint32_t)value;
        receiver_replyf("APS STATUS %lu\r\n", (unsigned long)app.settings.status);
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
        char resp[120];
        int n = snprintf(resp, sizeof(resp), "APS ALL %lu %lu %lu %ld %lu\r\n",
                         (unsigned long)app.settings.status,
                         (unsigned long)app.settings.position_min,
                         (unsigned long)app.settings.position_max,
                         (long)app.settings.position_dir,
                         (unsigned long)app.settings.motor_speed);
        if (n > 0)
          CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "POSITION_MIN") == 0)
      {
        receiver_replyf("APS POSITION_MIN %lu\r\n", (unsigned long)app.settings.position_min);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "POSITION_MAX") == 0)
      {
        receiver_replyf("APS POSITION_MAX %lu\r\n", (unsigned long)app.settings.position_max);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "POSITION_DIR") == 0)
      {
        receiver_replyf("APS POSITION_DIR %ld\r\n", (long)app.settings.position_dir);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "MOTOR_SPEED") == 0)
      {
        receiver_replyf("APS MOTOR_SPEED %lu\r\n", (unsigned long)app.settings.motor_speed);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "STATUS") == 0)
      {
        receiver_replyf("APS STATUS %lu\r\n", (unsigned long)app.settings.status);
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

  /* ---------------- APD (данные) ---------------- */
  if (strcmp(cat, "APD") == 0)
  {
    if (strcmp(cmd, "SET") == 0)
    {
      if (argc >= 3 && strcmp(tokens[2], "DFLT") == 0)
      {
        app.data = dflt_app_params.data;
        receiver_replyf("APD DFLT %lu %u %lu\r\n",
                        (unsigned long)app.data.minutes,
                        (unsigned int)app.data.button,
                        (unsigned long)app.data.current_position);
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "MINUTES") == 0)
      {
        unsigned long value = strtoul(tokens[3], NULL, 10);
        app.data.minutes = (uint32_t)value;
        receiver_replyf("APD MINUTES %lu\r\n", (unsigned long)app.data.minutes);
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "BUTTON") == 0)
      {
        unsigned long value = strtoul(tokens[3], NULL, 10);
        app.data.button = (uint8_t)value;
        receiver_replyf("APD BUTTON %u\r\n", (unsigned int)app.data.button);
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "CURRENT_POSITION") == 0)
      {
        unsigned long value = strtoul(tokens[3], NULL, 10);
        app.data.current_position = (uint32_t)value;
        receiver_replyf("APD CURRENT_POSITION %lu\r\n", (unsigned long)app.data.current_position);
        return;
      }

      const char error_response[] = "HGFE Error - Invalid SET APD command\r\n";
      CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
      return;
    }

    if (strcmp(cmd, "GET") == 0)
    {
      if (argc >= 3 && strcmp(tokens[2], "ALL") == 0)
      {
        char resp[96];
        int n = snprintf(resp, sizeof(resp), "APD ALL %lu %u %lu\r\n",
                         (unsigned long)app.data.minutes,
                         (unsigned int)app.data.button,
                         (unsigned long)app.data.current_position);
        if (n > 0)
          CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "MINUTES") == 0)
      {
        receiver_replyf("APD MINUTES %lu\r\n", (unsigned long)app.data.minutes);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "BUTTON") == 0)
      {
        receiver_replyf("APD BUTTON %u\r\n", (unsigned int)app.data.button);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "CURRENT_POSITION") == 0)
      {
        receiver_replyf("APD CURRENT_POSITION %lu\r\n", (unsigned long)app.data.current_position);
        return;
      }

      const char error_response[] = "HGFE Error - Invalid GET APD command\r\n";
      CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
      return;
    }

    const char error_response[] = "HGFE Error - Unknown APD command\r\n";
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

      /** Непрерывное вращение только через MOT HZ ... (RUN переопределён на позицию). */
      if (argc >= 4 && strcmp(tokens[2], "HZ") == 0)
      {
        unsigned long hz = strtoul(tokens[3], NULL, 10);
        if (hz == 0UL)
          tb6560_stop_steps();
        else
          tb6560_set_step_rate_hz((uint32_t)hz);
        receiver_replyf("MOT HZ %lu\r\n", (unsigned long)hz);
        return;
      }

      /** RUN <X> — абсолютная логическая позиция в [APS POSITION_MIN, POSITION_MAX]. */
      if (argc >= 4 && strcmp(tokens[2], "RUN") == 0)
      {
        if (app.settings.status != APS_STATUS_CALIB_OK)
        {
          const char error_response[] = "HGFE Error - APS not calibrated\r\n";
          CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
          return;
        }

        uint32_t pos_min = app.settings.position_min;
        uint32_t pos_max = app.settings.position_max;
        if (pos_max <= pos_min)
        {
          const char error_response[] = "HGFE Error - invalid APS position range\r\n";
          CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
          return;
        }

        unsigned long target_ul = strtoul(tokens[3], NULL, 10);
        uint32_t          X       = (uint32_t)target_ul;
        if (X < pos_min)
          X = pos_min;
        if (X > pos_max)
          X = pos_max;

        int64_t cur   = (int64_t)app.data.current_position;
        int64_t tgt   = (int64_t)X;
        int64_t delta = tgt - cur;
        if (delta == 0)
        {
          receiver_replyf("MOT RUN %lu\r\n", (unsigned long)X);
          return;
        }

        uint32_t steps = (uint32_t)(delta < 0 ? -delta : delta);
        uint32_t hz    = app.settings.motor_speed;
        if (hz == 0U)
          hz = 1000U;

        const bool logical_increase = delta > 0;
        tb6560_set_direction_forward(
            (app.settings.position_dir >= 0) ? logical_increase : !logical_increase);

        tb6560_move_steps_start(steps, hz);

        bool aborted_by_limit = false;
        while (motor_data.steps_remaining > 0U)
        {
          limits_update();
          const int32_t dir = app.settings.position_dir;
          const bool    fwd = motor_data.direction_forward;
          const bool toward_logical_min =
              (dir >= 0) ? !fwd : fwd;
          const bool toward_logical_max =
              (dir >= 0) ? fwd : !fwd;

          if (limits_logical_min_engaged() && toward_logical_min)
          {
            tb6560_stop_steps();
            aborted_by_limit = true;
            break;
          }
          if (limits_logical_max_engaged() && toward_logical_max)
          {
            tb6560_stop_steps();
            aborted_by_limit = true;
            break;
          }
        }

        if (!aborted_by_limit)
          app.data.current_position = X;
        else if (limits_logical_min_engaged())
          app.data.current_position = pos_min;
        else if (limits_logical_max_engaged())
          app.data.current_position = pos_max;

        (void)tb6560_take_pending_move(NULL, NULL);

        receiver_replyf("MOT RUN %lu\r\n", (unsigned long)app.data.current_position);
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
