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
 *  - Успех: OK\r\n или строка ответа GET
 *  - Ошибка: HGFE Error - ...\r\n
 *
 *  Категория MOT (TB6560):
 *  - EFGH SET MOT EN 0|1
 *  - EFGH SET MOT DIR 0|1|REV|FWD  (0=REV, 1=FWD)
 *  - EFGH SET MOT RUN <hz>  (hz=0 — стоп)
 *  - EFGH SET MOT STOP
 *  - EFGH SET MOT MOVE <steps> <hz>  — неблокирующий ответ OK
 *  - EFGH GET MOT STAT → MOT STAT EN=u DIR=u MODE=IDLE|RUN|MOVE HZ=lu REM=lu\r\n
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
        const char resp[] = "OK\r\n";
        CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "PARAM1") == 0)
      {
        unsigned long value = strtoul(tokens[3], NULL, 10);
        app.param1 = (uint32_t)value;
        const char resp[] = "OK\r\n";
        CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
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
                         (unsigned long)app.param1,
                         (unsigned long)app.param2,
                         (unsigned long)app.param3);
        if (n > 0)
          CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "PARAM") == 0)
      {
        char resp[64];
        int n = snprintf(resp, sizeof(resp), "APS PARAM1 %lu\r\n", (unsigned long)app.param1);
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
        const char resp[] = "OK\r\n";
        CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
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
        const char resp[] = "OK\r\n";
        CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
        return;
      }

      if (argc >= 4 && strcmp(tokens[2], "RUN") == 0)
      {
        unsigned long hz = strtoul(tokens[3], NULL, 10);
        if (hz == 0UL)
          tb6560_stop_steps();
        else
          tb6560_set_step_rate_hz((uint32_t)hz);
        const char resp[] = "OK\r\n";
        CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
        return;
      }

      if (argc >= 3 && strcmp(tokens[2], "STOP") == 0)
      {
        tb6560_stop_steps();
        const char resp[] = "OK\r\n";
        CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
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
        const char resp[] = "OK\r\n";
        CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
        return;
      }

      const char error_response[] = "HGFE Error - Invalid SET MOT command\r\n";
      CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
      return;
    }

    if (strcmp(cmd, "GET") == 0)
    {
      if (argc >= 3 && strcmp(tokens[2], "STAT") == 0)
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

void Receiver_OnData(uint8_t *buf, uint32_t len)
{
  if (!buf || len == 0)
    return;

  uint32_t in_len = len;

  char cmd[RECEIVER_CMD_BUF_SZ];
  if (len >= sizeof(cmd))
    len = sizeof(cmd) - 1;
  memcpy(cmd, buf, len);
  cmd[len] = '\0';

  if (len >= 4 && strncmp(cmd, "EFGH", 4) == 0)
  {
    cli_process(cmd);
    return;
  }

  /* SET APS DFLT */
  if (strncmp(cmd, "SET APS DFLT", 12) == 0)
  {
    app = dflt_app_params;
    const char resp[] = "OK\r\n";
    CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
    return;
  }

  if (strncmp(cmd, "SET APS PARAM1 ", 15) == 0)
  {
    char *p = cmd + 15;
    unsigned long value = strtoul(p, NULL, 10);
    app.param1 = (uint32_t)value;
    const char resp[] = "OK\r\n";
    CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
    return;
  }

  if (strncmp(cmd, "GET APS ALL", 11) == 0)
  {
    char resp[80];
    int n = snprintf(resp, sizeof(resp), "APS ALL %lu %u %lu %lu %lu\r\n",
                     (unsigned long)app.minutes,
                     (unsigned int)app.button,
                     (unsigned long)app.param1,
                     (unsigned long)app.param2,
                     (unsigned long)app.param3);
    if (n > 0)
      CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
    return;
  }

  if (strncmp(cmd, "GET APS PARAM", 13) == 0)
  {
    char resp[64];
    int n = snprintf(resp, sizeof(resp), "APS PARAM1 %lu\r\n", (unsigned long)app.param1);
    if (n > 0)
      CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
    return;
  }

  CDC_Transmit_FS(buf, (uint16_t)in_len);
}
