/*
 * app.c
 *
 *  Основной цикл приложения.
 *  Управление TB6560 — по USB CDC (категория MOT в receiver.c).
 */

#include "app.h"

app_t app = {0};

const app_t dflt_app_params = {0, 0, 0, 0, 0};

void app_process(void)
{
}
