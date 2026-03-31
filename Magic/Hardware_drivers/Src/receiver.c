/*
 * receiver.c
 *
 *  Created on: 24 февр. 2026 г.
 *      Author: s20di
 *
 *  Обработка данных, полученных по USB CDC.
 */

#include "receiver.h"
#include "usbd_cdc_if.h"
#include "app.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

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
  char *tokens[8] = {0};
  int argc = 0;
  char *token = strtok(cmd_buffer, " ,=\r\n");
  while (token && argc < 8)
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
  const char *cat = tokens[1]; /* APS */

  if (strcmp(cat, "APS") != 0)
  {
    const char error_response[] = "HGFE Error - Invalid Category\r\n";
    CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
    return;
  }

  /* ---------------- SET APS ... ---------------- */
  if (strcmp(cmd, "SET") == 0)
  {
    if (argc >= 3 && strcmp(tokens[2], "DFLT") == 0)
    {
      app = dflt_app_params;
      const char resp[] = "OK\r\n";
      CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
      return;
    }

    /* SET APS PARAM1 X */
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

  /* ---------------- GET APS ... ---------------- */
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
      {
        CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
      }
      return;
    }

    if (argc >= 3 && strcmp(tokens[2], "PARAM") == 0)
    {
      char resp[64];
      int n = snprintf(resp, sizeof(resp), "APS PARAM1 %lu\r\n", (unsigned long)app.param1);
      if (n > 0)
      {
        CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
      }
      return;
    }

    const char error_response[] = "HGFE Error - Invalid GET APS command\r\n";
    CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
    return;
  }

  const char error_response[] = "HGFE Error - Unknown command\r\n";
  CDC_Transmit_FS((uint8_t *)error_response, (uint16_t)(sizeof(error_response) - 1));
}

void Receiver_OnData(uint8_t *buf, uint32_t len)
{
  if (!buf || len == 0)
    return;

  uint32_t in_len = len;

  /* Копируем команду в локальный буфер и завершаем нулём */
  char cmd[64];
  if (len >= sizeof(cmd))
  {
    len = sizeof(cmd) - 1;
  }
  memcpy(cmd, buf, len);
  cmd[len] = '\0';

  /* Если команда начинается с префикса EFGH – обрабатываем через CLI */
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

  /* SET APS PARAM1 X */
  if (strncmp(cmd, "SET APS PARAM1 ", 15) == 0)
  {
    char *p = cmd + 15;
    unsigned long value = strtoul(p, NULL, 10);
    app.param1 = (uint32_t)value;

    const char resp[] = "OK\r\n";
    CDC_Transmit_FS((uint8_t *)resp, (uint16_t)(sizeof(resp) - 1));
    return;
  }

  /* GET APS ALL */
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
    {
      CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
    }
    return;
  }

  /* GET APS PARAM */
  if (strncmp(cmd, "GET APS PARAM", 13) == 0)
  {
    char resp[64];
    int n = snprintf(resp, sizeof(resp), "APS PARAM1 %lu\r\n", (unsigned long)app.param1);
    if (n > 0)
    {
      CDC_Transmit_FS((uint8_t *)resp, (uint16_t)n);
    }
    return;
  }

  /* По умолчанию — эхо */
  CDC_Transmit_FS(buf, (uint16_t)in_len);
}
