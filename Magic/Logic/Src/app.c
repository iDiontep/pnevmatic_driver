/*
 * app.c
 *
 *  Основной цикл приложения.
 */

#include "app.h"
#include "usbd_cdc_if.h"
#include "stm32f3xx_hal.h"

app_t app = {0};

const app_t dflt_app_params = {0, 0, 0, 0, 0};

void app_process(void)
{
//  static const char msg[] = "HELLOWORLD HELP\r\n";
//  CDC_Transmit_FS((uint8_t *)msg, sizeof(msg) - 1);
//  HAL_Delay(1000);
}
