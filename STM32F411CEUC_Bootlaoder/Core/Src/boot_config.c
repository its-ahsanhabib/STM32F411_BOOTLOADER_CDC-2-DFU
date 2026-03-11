/*
 * boot_config.c
 *
 *  Created on: Mar 11, 2026
 *      Author: ahksh
 */

#include "boot_config.h"

/* STM32F411 — APB1ENR bit 28 = PWR clock */
#define PWR_CLK_ENABLE()  (RCC->APB1ENR |= RCC_APB1ENR_PWREN)

/* ─────────────────────────────────────────────────────
   CRC Calculation over flash region
───────────────────────────────────────────────────── */
uint32_t CalculateCRC(uint32_t start, uint32_t length)
{
    uint32_t *data = (uint32_t*)start;
    uint32_t  words = length / 4;

    __HAL_RCC_CRC_CLK_ENABLE();
    CRC->CR = CRC_CR_RESET;

    for (uint32_t i = 0; i < words; i++)
        CRC->DR = data[i];

    return CRC->DR;
}

/* ─────────────────────────────────────────────────────
   Validate application:
   - Stack pointer must not be blank
   - Reset vector must point inside flash
   - Optional: CRC check
───────────────────────────────────────────────────── */
bool IsApplicationValid(void)
{
    uint32_t appStack = *(volatile uint32_t*)APP_ADDRESS;
    uint32_t appEntry = *(volatile uint32_t*)(APP_ADDRESS + 4);

    /* Blank flash check */
    if (appStack == 0xFFFFFFFF || appStack == 0x00000000)
        return false;

    /* Reset vector must be inside flash */
    if (appEntry < 0x08000000 || appEntry > 0x0807FFFF)
        return false;

    /* Stack must be in STM32F411 SRAM (128KB: 0x20000000–0x20020000) */
    if (appStack < 0x20000000 || appStack > 0x20020000)  // ✅ F411 SRAM
        return false;

    /* Optional CRC check */
    uint32_t appSize    = *(uint32_t*)(APP_ADDRESS + 0x100);
    uint32_t storedCRC  = *(uint32_t*)(APP_ADDRESS + 0x104);

    /* Only do CRC if size looks sane */
    if (appSize > 0 && appSize < (APP_END_ADDRESS - APP_ADDRESS))
    {
        uint32_t calcCRC = CalculateCRC(APP_ADDRESS, appSize);
        if (calcCRC != storedCRC)
            return false;
    }

    return true;
}

/* ─────────────────────────────────────────────────────
   Jump to Application
   Call IsApplicationValid() before this!
───────────────────────────────────────────────────── */
void Bootloader_JumpToApplication(void)
{
    uint32_t appStack = *(volatile uint32_t*)APP_ADDRESS;
    uint32_t appEntry = *(volatile uint32_t*)(APP_ADDRESS + 4);

    printf("[BOOT] Jumping to app at 0x%08lX, stack=0x%08lX\r\n", appEntry, appStack);

    /* Disable all interrupts */
    __disable_irq();

    /* ── Kill SysTick completely ────────────────────── */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* ── Clear ALL pending IRQs ─────────────────────── */
    for(int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* ── Deinit HAL ─────────────────────── */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* Relocate vector table to app */
    SCB->VTOR = APP_ADDRESS;

    /* Set MSP to app's stack pointer */
    __set_MSP(appStack);

    /* ── Enable Interrupts ─────────────────────── */
    __enable_irq();

    /* Jump to app reset handler */
    pFunction appReset = (pFunction)appEntry;
    appReset();

    /* Should never reach here */
    while(1);
}

//void Bootloader_JumpToApplication(void)
//{
//    uint32_t appStack = *(volatile uint32_t*)APP_ADDRESS;
//    uint32_t appEntry = *(volatile uint32_t*)(APP_ADDRESS + 4);
//
//    printf("[BOOT] Jumping to app at 0x%08lX, stack=0x%08lX\r\n", appEntry, appStack);
//
//    /* ── Step 1: Deinit HAL FIRST (while SysTick still alive) ── */
//    HAL_DeInit();
//    HAL_RCC_DeInit();
//
//    /* ── Step 2: THEN disable IRQs ─────────────────────────── */
//    __disable_irq();
//
//    /* ── Step 3: THEN kill SysTick ─────────────────────────── */
//    SysTick->CTRL = 0;
//    SysTick->LOAD = 0;
//    SysTick->VAL  = 0;
//
//    /* ── Step 4: Clear ALL pending IRQs ────────────────────── */
//    for(int i = 0; i < 8; i++) {
//        NVIC->ICER[i] = 0xFFFFFFFF;
//        NVIC->ICPR[i] = 0xFFFFFFFF;
//    }
//
//    /* Reset ALL peripherals via RCC directly */
//    RCC->AHB1RSTR = 0xFFFFFFFF;  RCC->AHB1RSTR = 0;
//    RCC->AHB2RSTR = 0xFFFFFFFF;  RCC->AHB2RSTR = 0;
//    RCC->APB1RSTR = 0xFFFFFFFF;  RCC->APB1RSTR = 0;
//    RCC->APB2RSTR = 0xFFFFFFFF;  RCC->APB2RSTR = 0;
//
//    /* ── Step 5: Relocate vectors ───────────────────────────── */
//    SCB->VTOR = APP_ADDRESS;
//
//    /* Data Synchronization Barrier — ensure all writes complete */
//    __DSB();
//    __ISB();
//
//    /* ── Step 6: Jump ───────────────────────────────────────── */
//    __set_MSP(appStack);
//    pFunction appReset = (pFunction)appEntry;
//    appReset();
//
//    while(1);
//}

/* ─────────────────────────────────────────────────────
   Check backup register for DFU request magic number
   If not set → jump to app directly
───────────────────────────────────────────────────── */
void CheckForBootloaderMode(void)
{
    if (BACKUP_REGISTER == MAGIC_NUMBER)
    {
        /* Enable PWR clock — ✅ F411 register */
        PWR_CLK_ENABLE();

        /* Enable access to backup domain */
        PWR->CR |= PWR_CR_DBP;   // ✅ F411: PWR_CR not PWR_CR1

        /* Clear magic to avoid infinite DFU loop */
        BACKUP_REGISTER = 0;

        printf("[BOOT] App update requested — waiting for DFU upload\r\n");
        /* Return here — caller should then activate DFU */
    }
    else
    {
        if (!IsApplicationValid())
        {
            printf("[BOOT] No valid application found — staying in DFU\r\n");
            return; /* Stay in bootloader/DFU */
        }

        Bootloader_JumpToApplication();
    }
}


/* ── Helper: Address → Sector Number ─────────────────────── */
uint32_t GetSector(uint32_t Address)
{
    if      (Address < 0x08004000) return FLASH_SECTOR_0;  // 16KB
    else if (Address < 0x08008000) return FLASH_SECTOR_1;  // 16KB
    else if (Address < 0x0800C000) return FLASH_SECTOR_2;  // 16KB
    else if (Address < 0x08010000) return FLASH_SECTOR_3;  // 16KB
    else if (Address < 0x08020000) return FLASH_SECTOR_4;  // 64KB
    else if (Address < 0x08040000) return FLASH_SECTOR_5;  // 128KB
    else if (Address < 0x08060000) return FLASH_SECTOR_6;  // 128KB
    else                           return FLASH_SECTOR_7;  // 128KB
}


ResetReason_t getResetReason(void)
{
	ResetReason_t reset_cause;

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST))
    {
        reset_cause = RESET_CAUSE_LOW_POWER_RESET;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST))
    {
        reset_cause = RESET_CAUSE_WINDOW_WATCHDOG_RESET;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))
    {
        reset_cause = RESET_CAUSE_INDEPENDENT_WATCHDOG_RESET;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))
    {
        // This reset is induced by calling the ARM CMSIS
        // `NVIC_SystemReset()` function!
        reset_cause = RESET_CAUSE_SOFTWARE_RESET;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST))
    {
        reset_cause = RESET_CAUSE_POWER_ON_POWER_DOWN_RESET;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))
    {
        reset_cause = RESET_CAUSE_EXTERNAL_RESET_PIN_RESET;
    }
    // Needs to come *after* checking the `RCC_FLAG_PORRST` flag in order to
    // ensure first that the reset cause is NOT a POR/PDR reset. See note
    // below.
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST))
    {
        reset_cause = RESET_CAUSE_BROWNOUT_RESET;
    }
    else
    {
        reset_cause = RESET_CAUSE_UNKNOWN;
    }

    // Clear all the reset flags or else they will remain set during future
    // resets until system power is fully removed.
    __HAL_RCC_CLEAR_RESET_FLAGS();

    return reset_cause;
}


const char * getResetReasonName(ResetReason_t reset_cause)
{
    const char * reset_cause_name = "TBD";

    switch (reset_cause)
    {
        case RESET_CAUSE_UNKNOWN:
            reset_cause_name = "UNKNOWN";
            break;
        case RESET_CAUSE_LOW_POWER_RESET:
            reset_cause_name = "LOW_POWER_RESET";
            break;
        case RESET_CAUSE_WINDOW_WATCHDOG_RESET:
            reset_cause_name = "WINDOW_WATCHDOG_RESET";
            break;
        case RESET_CAUSE_INDEPENDENT_WATCHDOG_RESET:
            reset_cause_name = "INDEPENDENT_WATCHDOG_RESET";
            break;
        case RESET_CAUSE_SOFTWARE_RESET:
            reset_cause_name = "SOFTWARE_RESET";
            break;
        case RESET_CAUSE_POWER_ON_POWER_DOWN_RESET:
            reset_cause_name = "POWER-ON_RESET (POR) / POWER-DOWN_RESET (PDR)";
            break;
        case RESET_CAUSE_EXTERNAL_RESET_PIN_RESET:
            reset_cause_name = "EXTERNAL_RESET_PIN_RESET";
            break;
        case RESET_CAUSE_BROWNOUT_RESET:
            reset_cause_name = "BROWNOUT_RESET (BOR)";
            break;
    }

    return reset_cause_name;
}
