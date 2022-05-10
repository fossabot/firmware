/*                       
 * FileName:    AccessPoint_Mng.h
 * Brief:       Access Point manager header file
 */ 

/* ----------------------- Includes ----------------------- */

/* ----------------------- Defines ------------------------ */
#define LIGHT_FEEDBACK_TIMER 300    // 500ms between light config commutation

/* ---------------------- Data Types ---------------------- */

/* -------------- Global function prototypes -------------- */

void ICACHE_FLASH_ATTR AccessPoint_Mng_Init(void);
void ICACHE_FLASH_ATTR AccessPoint_Mng_Hdlr(void);
