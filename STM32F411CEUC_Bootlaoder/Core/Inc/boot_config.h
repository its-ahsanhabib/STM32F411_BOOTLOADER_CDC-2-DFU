/*
 * boot_config.h
 *
 *  Created on: Mar 11, 2026
 *      Author: ahksh
 */
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ── Flash Map: STM32F411 (512KB) ──────────────────────
   Sector 0-3: 16KB each  → 0x08000000–0x0800FFFF (bootloader)
   Sector 4:   64KB        → 0x08010000–0x0801FFFF
   Sector 5-7: 128KB each  → 0x08020000–0x0807FFFF (app)
──────────────────────────────────────────────────────── */
#define APP_ADDRESS       0x08008000UL   // Start of app (after bootloader)
#define APP_END_ADDRESS   0x0807FFFFUL   // End of 512KB flash
#define FLASH_END_ADDRESS 0x0807FFFFUL

/* Flash timing (ms) */
#define FLASH_PROGRAM_TIME   50
#define FLASH_ERASE_TIME     5000

/* Magic number in backup register to request DFU */
#define MAGIC_NUMBER         0xDEADBEEF
#define BACKUP_REGISTER      RTC->BKP0R   // F411 backup register

typedef void (*pFunction)(void);

uint32_t CalculateCRC(uint32_t start, uint32_t length);
bool     IsApplicationValid(void);
void     Bootloader_JumpToApplication(void);
void     CheckForBootloaderMode(void);

#endif /* BOOT_CONFIG_H */
