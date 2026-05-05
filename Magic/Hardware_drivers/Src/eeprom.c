/*
 * eeprom.c — STM32F373 256 KB: последняя страница Flash (2 KB) под APS.
 */

#include "eeprom.h"

#include "stm32f3xx_hal.h"
#include "stm32f3xx_hal_flash.h"
#include "stm32f3xx_hal_flash_ex.h"

#include <string.h>

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 0x800U
#endif

/** База последней страницы основной Flash (256 KB − 2 KB). */
#define APS_FLASH_PAGE_ADDR ((uint32_t)(FLASH_BASE + 256U * 1024U - FLASH_PAGE_SIZE))

#define APS_EEPROM_MAGIC ((uint32_t)0xC0ADBEE5u)
#define APS_EEPROM_VER ((uint32_t)2u)

typedef struct
{
  uint32_t       magic;
  uint32_t       version;
  app_settings_t settings;
  uint32_t       chk;
} eeprom_aps_store_t;

_Static_assert(sizeof(eeprom_aps_store_t) == 44u, "eeprom packing");

static uint32_t eeprom_chk(const eeprom_aps_store_t *b)
{
  const app_settings_t *s = &b->settings;
  return b->magic ^ b->version ^ s->status ^ s->position_min ^ s->position_max
         ^ (uint32_t)s->position_dir ^ s->motor_speed ^ s->ramp_step_interval
         ^ s->ramp_hz_step ^ s->ramp_min_hz;
}

bool eeprom_try_load(app_settings_t *settings)
{
  if (!settings)
    return false;

  const eeprom_aps_store_t *stored =
      (const eeprom_aps_store_t *)(uintptr_t)APS_FLASH_PAGE_ADDR;

  eeprom_aps_store_t blk;
  memcpy(&blk, stored, sizeof(blk));

  if (blk.magic != APS_EEPROM_MAGIC || blk.version != APS_EEPROM_VER)
    return false;
  if (blk.chk != eeprom_chk(&blk))
    return false;
  if (blk.settings.position_dir != 1 && blk.settings.position_dir != -1)
    return false;

  *settings = blk.settings;
  return true;
}

bool eeprom_save(const app_settings_t *settings)
{
  if (!settings)
    return false;

  eeprom_aps_store_t blk;
  blk.magic    = APS_EEPROM_MAGIC;
  blk.version  = APS_EEPROM_VER;
  blk.settings = *settings;
  blk.chk      = eeprom_chk(&blk);

  const uint32_t nwords = (uint32_t)(sizeof(blk) / sizeof(uint32_t));

  FLASH_EraseInitTypeDef er = {0};
  uint32_t                 page_err = 0U;
  er.TypeErase              = FLASH_TYPEERASE_PAGES;
  er.PageAddress            = APS_FLASH_PAGE_ADDR;
  er.NbPages                = 1U;

  HAL_StatusTypeDef st = HAL_FLASH_Unlock();
  if (st != HAL_OK)
    return false;

  st = HAL_FLASHEx_Erase(&er, &page_err);
  if (st != HAL_OK)
  {
    HAL_FLASH_Lock();
    return false;
  }

  const uint8_t *bytes = (const uint8_t *)&blk;
  for (uint32_t i = 0U; i < nwords; i++)
  {
    uint32_t word;
    memcpy(&word, bytes + i * 4U, sizeof(word));
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APS_FLASH_PAGE_ADDR + i * 4U, word);
    if (st != HAL_OK)
      break;
  }

  HAL_FLASH_Lock();
  return st == HAL_OK;
}
