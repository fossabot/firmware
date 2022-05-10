/* HEADER File of gpio_mng module */
#ifndef __GPIO_MNG_H__
#define __GPIO_MNG_H__

#define GPIO_MNG_PIN_MAX_NUM    4
#define DEB_MIN_TIME 5
#define DEB_MAX_TIME 1000
#define PRESSED_THRESHOLD   500
#define MAX_GPIO_INIT_TIMER 1000
#define MAX_INTERCHANGE_TIMER 50
#define INHIBIT_MIN_TIME    5
#define INHIBIT_MAX_TIME    5000

/* Initializer of GPIOMng module */
void ICACHE_FLASH_ATTR GPIOMng_Init(uint8 rstcs);
/* Handler of GPIOMng module */
void ICACHE_FLASH_ATTR GPIOMng_Handler(void);
/* Function returning pin state */
uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinState(uint8 Idx);
/* Function setting pin state */
void ICACHE_FLASH_ATTR GPIOMng_SetPinState(uint8 Idx, uint8 Val);
/* Get if the status of the pin changed */
uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinStsChanged(uint8 Idx);
/* Reset the status of the pin changed flag */
void ICACHE_FLASH_ATTR GPIOMng_RstPinStsChanged(uint8 Idx);
/* Get validity of the acquired pin */
uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinStsValid(uint8 Idx);
uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinPressed(uint8 Idx);
void ICACHE_FLASH_ATTR GPIOMng_PinLock(uint8 Idx);
void ICACHE_FLASH_ATTR GPIOMng_PinUnlock(uint8 Idx);
uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinUnlocked(uint8 Idx);
uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinLocked(uint8 Idx);
void ICACHE_FLASH_ATTR SET_LOW(uint8);
void ICACHE_FLASH_ATTR SET_HIGH(uint8);
void ICACHE_FLASH_ATTR SET_TOGGLE(uint8);

#endif
