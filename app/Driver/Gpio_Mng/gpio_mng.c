/*
 * FileName:    gpio_mng.c
 * Brief:       Driver module for GPIO
 */

/* Includes */
#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "user_config.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "gpio16.h"
#include "gpio_mng.h"
#include "Nvm_Mng.h"
#include "Misc.h"

/* Data types */

typedef enum
{
    GM_FSM_DEB_NODEB = 0,
    GM_FSM_DEB_CHECK,
    GM_FSM_DEB_WAIT

}GPIO_FSM_DEB;

typedef struct
{
    uint32  PinAddr;    /* memory map address */
    uint8   PinFunc;    /* pin functionality */
    uint8   PinIdx;     /* Pin index */
    uint8   IsOut;      /* Is an output? */
    uint8   Debounce;   /* debounce time in case of input */
    uint8   RstVal;     /* Value to be set during initialization */
}GPIO_MNG_CONF;

typedef struct
{
    uint8               PinState;       /* State of the pin (1/0) */
    uint8               PinNewState;    /* New potential state of the pin (1/0) */
    uint8               PinStsChanged;  /* Is the pin state changed? */
    uint8               PinStsValid;    /* Is its current value valid? Used only by input */
    uint16              PinCurrDebTmr;  /* Current debounce timer */
    GPIO_FSM_DEB        PinDebSts;      /* State of the Debounce FSM */
    GPIO_MNG_CONF       *PinConf;       /* Configuration of the pin */
    uint8               PinPressed;     /* True if the pin status changes from 1 (NOT pressed button) to 0 (pressed button) */
    uint16              PressedTmr;     /* Timer to check if pin is locked */
    bool                PinLocked;      /* True if PinPressed TRUE for PRESSED_THRESHOLD milliseconds */
    bool                PinUnlocked;    /* True if pin is "not-pressed" and was Locked before */
}GPIO_MNG_PIN;

typedef struct
{
    GPIO_MNG_PIN    Pin[GPIO_MNG_PIN_MAX_NUM];
}GPIO_MNG;

/* Pins configuration of this device */

#if (POWA_HW_VERS == HW_VERS_1)
GPIO_MNG_CONF   GPIOMngConf[GPIO_MNG_PIN_MAX_NUM] = { {PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12, 12, FALSE, 10, 0},
                                                      {PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13, 13, FALSE, 10, 0},
                                                      {PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14, 14, TRUE, 0, 1},
                                                      {0, 0, 16, TRUE, 0, 1} };
#else
GPIO_MNG_CONF   GPIOMngConf[GPIO_MNG_PIN_MAX_NUM] = { {PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12, 12, FALSE, 50, 0},
                                                      {PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13, 13, FALSE, 50, 0},
                                                      {PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4, 4, TRUE, 0, 0},
                                                      {PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5, 5, TRUE, 0, 0}};
#endif

/* Data structure identifying the module */
GPIO_MNG GPIOMng;
uint16_t gpio_debounce_timer = 50;
uint16_t gpio_init_timer = 0;
bool gpio_init_timer_expired = FALSE;
bool booting_up;
t_Nvm_Mng_Data *dev_conf;

t_timer interchange_timer[2] = { { .timer = 0, .expired = TRUE, .name = "interchange_timer[0]" }, { .timer = 0, .expired = TRUE, .name = "interchange_timer[1]" } };
t_timer inhibit_timer[2] = { {.timer = 0, .expired = TRUE, .name = "inhibit_timer[0]" }, { .timer = 0, .expired = 0, .name = "inhibit_timer[1]" } };

/* Private functions */

void ICACHE_FLASH_ATTR arm_gpio_init_timer(void)
{
    gpio_init_timer = 0;
    gpio_init_timer_expired = FALSE;
    DEBUG_INFO("Arming gpio_init_timer!");
}

void ICACHE_FLASH_ATTR handle_gpio_init_timer(void)
{
    if (gpio_init_timer_expired == TRUE)
        return;

    if (gpio_init_timer >= MAX_GPIO_INIT_TIMER)
    {
        gpio_init_timer_expired = TRUE;
        gpio_init_timer = MAX_GPIO_INIT_TIMER;
        DEBUG_INFO("gpio_init_timer expired!");

        if (is_switch())
        {
            GPIOMng_SetPinState(L1, dev_conf->m_statuspin[L1]);
            GPIOMng_SetPinState(L2, dev_conf->m_statuspin[L2]);
        }

        if (is_latched())
        {
            GPIOMng_SetPinState(L1, dev_conf->m_relecurrstatus_ch1);
            GPIOMng_SetPinState(L2, dev_conf->m_relecurrstatus_ch2);
        }
        
        return;
    }

    gpio_init_timer += 5;
}

void ICACHE_FLASH_ATTR arm_interchange_timer(uint8_t index)
{
    BUTTON_INFO("arm_interchange_timer[%s]", index == SW1 ? "SW1" : "SW2");

    interchange_timer[index].timer = 0;
    interchange_timer[index].expired = FALSE;
}

void ICACHE_FLASH_ATTR handle_interchange_timer(uint8_t index)
{
    if (interchange_timer[index].expired == TRUE)
        return;

    if (interchange_timer[index].timer >= MAX_INTERCHANGE_TIMER)
    {
        interchange_timer[index].timer = MAX_INTERCHANGE_TIMER;
        interchange_timer[index].expired = TRUE;
        BUTTON_INFO("interchange_timer[%s] EXPIRED", index == SW1 ? "SW1" : "SW2");
        return;
    }

    interchange_timer[index].timer += CYCLE_TIME;
}

static void ICACHE_FLASH_ATTR GPIOMng_Debounce(GPIO_MNG_PIN *pPin)
{
    uint8_t pin_index = (pPin->PinConf->PinIdx == 12) ? SW1 : SW2;
    uint8_t timer_index = (pin_index == SW1) ? SW2 : SW1;

    handle_interchange_timer(pin_index);
    handle_timer(inhibit_timer + pin_index, CYCLE_TIME, dev_conf->m_inhibit_max_time);

    switch (pPin->PinDebSts)
    {
        /* Check for any changing in the input */
        case GM_FSM_DEB_CHECK:
            /* Acquire current pin state */
            if (pPin->PinConf->PinIdx != 16)
            {
                pPin->PinNewState = GPIO_INPUT_GET(pPin->PinConf->PinIdx);
            }
            else
            {
                pPin->PinNewState = gpio16_input_get();
            }
            /* Check if a variation of the input occured */
            if (pPin->PinState != pPin->PinNewState)
            {
                pPin->PinDebSts = GM_FSM_DEB_WAIT;
                pPin->PinStsValid = FALSE;

                pPin->PinUnlocked = FALSE;
                pPin->PressedTmr = 0;
            }
            else
            {
                pPin->PinStsValid = TRUE;

                if (pPin->PinNewState == 0)
                {
                    if (pPin->PressedTmr < PRESSED_THRESHOLD)
                        pPin->PressedTmr += CYCLE_TIME;

                    if (pPin->PressedTmr == PRESSED_THRESHOLD)
                    {
                        GPIOMng_PinLock(pPin->PinConf->PinIdx - 12);
                        pPin->PressedTmr++;
                    }
                }
                else
                    if (pPin->PinLocked == TRUE)
                        GPIOMng_PinUnlock(pPin->PinConf->PinIdx - 12);
            }

            break;

        /* Validate new pin state */
        case GM_FSM_DEB_WAIT:
            /* Increment debounce timer */
            pPin->PinCurrDebTmr += CYCLE_TIME;
            /* Check if the timer expire */
            if (pPin->PinCurrDebTmr >= dev_conf->m_debounce_timer)
            {
                /* Reset timer */
                pPin->PinCurrDebTmr = 0;
                /* Acquire current pin state */
                int debouncedPinNewState;
                if (pPin->PinConf->PinIdx != 16)
                {
                    debouncedPinNewState = GPIO_INPUT_GET(pPin->PinConf->PinIdx);
                }
                else
                {
                    debouncedPinNewState = gpio16_input_get();
                }

                /* If the pin effectively changed state */
                if (pPin->PinState != pPin->PinNewState && pPin->PinNewState == debouncedPinNewState)
                {
                    /* Check if it is a button pressed (transition from 1 to 0) */
                    pPin->PinPressed = (pPin->PinNewState == 0) ? TRUE : FALSE;

                    /* Update pin state */
                    pPin->PinState = pPin->PinNewState;

                    /* Signal a change in state of the pin */
                    pPin->PinStsValid = TRUE;
                    pPin->PinStsChanged = TRUE;
                    BUTTON_INFO("Changed pin: %s - State: %s - Pressed: %s", (pPin->PinConf->PinIdx == 12) ? "SW1" : "SW2", (pPin->PinState == TRUE) ? "True" : "False",  (pPin->PinPressed == TRUE) ? "True" : "False");

                    /* only arm the timer when the button is released */

                        if (pPin->PinPressed == FALSE)
                             arm_timer(inhibit_timer + pin_index);
                        else
                        {
                            if (inhibit_timer[pin_index].expired == FALSE)
                            {
                                ERROR_INFO("inhibit_timer[%d] NOT expired yet!", pin_index);
                                pPin->PinStsValid = FALSE;
                                pPin->PinStsChanged = FALSE;
                                return;
                            }
                        }

                        if (is_roller())
                        {
                            if (interchange_timer[pin_index].expired == FALSE)
                            {
                                pPin->PinStsValid = FALSE;
                                pPin->PinStsChanged = FALSE;
                                return;
                            }
                        }
                        arm_interchange_timer(timer_index);
                } 
                else
                {
                    /* Signal that there is no a change in state of the pin */
                    pPin->PinStsValid = TRUE;
                    arm_timer(inhibit_timer + pin_index);
                    pPin->PinStsChanged = FALSE;
                    BUTTON_INFO("Not changed pin: %s - State: %s - Pressed: %s", (pPin->PinConf->PinIdx == 12) ? "SW1" : "SW2", (pPin->PinState == TRUE) ? "True" : "False",  (pPin->PinPressed == TRUE) ? "True" : "False");
                } 

                /* Check for other changes */
                pPin->PinDebSts = GM_FSM_DEB_CHECK;
            }
            break;

        case GM_FSM_DEB_NODEB:
            /* No debounce to be handled */
            break;

        default:
            break;
    }
}



/*
 * Name:    void GPIOMng_SetPinState (uint8 Idx, uint8 Val)
 * Descr:   Set status of a certain GPIO
 */

void ICACHE_FLASH_ATTR GPIOMng_SetPinState(uint8 Idx, uint8 Val)
{
    uint8_t pin_curr_state = GPIOMng_GetPinState(Idx);

    if (pin_curr_state != Val)
        dev_conf->m_relay_counter[3-Idx]++;
    
    if (GPIOMngConf[Idx].PinIdx != 16)
    {
        GPIO_OUTPUT_SET(GPIOMngConf[Idx].PinIdx,Val);
    }
    else
    {
        gpio16_output_set(Val);
    }
    GPIOMng.Pin[Idx].PinState = Val;
}


/* Public functions */

/*
 * Name:    void GPIOMng_Init (uint8 rstcs)
 * Descr:   GPIO Manager initialization
 */

void ICACHE_FLASH_ATTR GPIOMng_Init(uint8 rstcs)
{
    dev_conf = Nvm_Mng_GetNvm();
    uint8 i;

    arm_gpio_init_timer();

    ///* GPIO 0 */
    //PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);
    //PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO0_U);

    ///* GPIO 2 */
    //PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
    //PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO2_U);

    ///* GPIO 14 */
    //PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
    //PIN_PULLUP_EN(PERIPHS_IO_MUX_MTMS_U);

    ///* GPIO 15 */
    //PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
    //PIN_PULLUP_EN(PERIPHS_IO_MUX_MTDO_U);

    //SET_HIGH(MAGENTA_GPIO);

    /* Iterate over the pins */
    for(i = 0; i < GPIO_MNG_PIN_MAX_NUM; i++)
    {
        if (GPIOMngConf[i].PinIdx != 16)
        {
            /* Configure the pin */
            PIN_FUNC_SELECT(GPIOMngConf[i].PinAddr,GPIOMngConf[i].PinFunc);

            /* Is the pin an input? */
            if (GPIOMngConf[i].IsOut == FALSE)
            {
                /* Set as input */
                GPIO_DIS_OUTPUT(GPIOMngConf[i].PinIdx);

                /* Initialize the debounce FSM */
                GPIOMng.Pin[i].PinDebSts = GM_FSM_DEB_CHECK;

                GPIOMng.Pin[i].PressedTmr = 0;
            }
            else
            {
                if (rstcs == FALSE)
                {
                    /* GPIO reset value */
                    GPIO_OUTPUT_SET(GPIOMngConf[i].PinIdx,GPIOMngConf[i].RstVal);
                }
                else
                {
                    /* Pin state from flash memory */
                    GPIOMng.Pin[i].PinState=dev_conf->m_statuspin[i];
                }

                /* Keep debounce disabled */
                GPIOMng.Pin[i].PinDebSts = GM_FSM_DEB_NODEB;
            }
        }
        else
        {
            if (GPIOMngConf[i].IsOut == FALSE)
            {
                /* Set as input */
                gpio16_input_conf();

                /* Initialize the debounce FSM */
                GPIOMng.Pin[i].PinDebSts = GM_FSM_DEB_CHECK;
            }
            else
            {
                /* Set as output */
                gpio16_output_conf();

                if (rstcs == FALSE)
                {
                    /* GPIO reset value */
                    gpio16_output_set(GPIOMngConf[i].RstVal);
                }
                else
                {
                    /* Pin state from flash memory */
                    GPIOMng.Pin[i].PinState=dev_conf->m_statuspin[i];
                }

                /* Keep debounce disabled */
                GPIOMng.Pin[i].PinDebSts = GM_FSM_DEB_NODEB;
            }
        }

        /* Init pin status, if reboot cause is not FOTA */
        if (rstcs == FALSE)
        {
            GPIOMng.Pin[i].PinState = GPIOMngConf[i].RstVal;
            GPIOMng.Pin[i].PinNewState = GPIOMngConf[i].RstVal;
        }

        GPIOMng.Pin[i].PinStsValid = FALSE;
        GPIOMng.Pin[i].PinStsChanged = FALSE;
        GPIOMng.Pin[i].PinCurrDebTmr = 0;
        GPIOMng.Pin[i].PinConf = &GPIOMngConf[i];
    }

    if (!is_in_range(dev_conf->m_debounce_timer, DEB_MIN_TIME, DEB_MAX_TIME))
        dev_conf->m_debounce_timer = DEB_MIN_TIME;

    //SET_LOW(MAGENTA_GPIO);
}

/*
 * Name:    void GPIOMng_Handler (void)
 * Descr:   GPIO Manager
 */

void ICACHE_FLASH_ATTR GPIOMng_Handler(void)
{
    handle_gpio_init_timer();

    uint8 i;

    /* Iterate for all the GPIOs */
    for (i = 0; i < GPIO_MNG_PIN_MAX_NUM; i++)
    {
        /* If debounce shall be managed */
        if (GPIOMng.Pin[i].PinDebSts != GM_FSM_DEB_NODEB)
        {
            /* Manage debounce */
            GPIOMng_Debounce(&(GPIOMng.Pin[i]));
        }
    }

    if (gpio_init_timer_expired == FALSE)
    {
        GPIOMng_RstPinStsChanged(SW1);
        GPIOMng_RstPinStsChanged(SW2);
        return;
    }
}

/*
 * Name:    void GPIOMng_GetPinState (uint8 Idx)
 * Descr:   Get status of a certain GPIO
 */

uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinState(uint8 Idx)
{
    return (uint8)GPIOMng.Pin[Idx].PinState;
}


/*
 * Name:    void GPIOMng_GetPinStsValid (uint8 Idx)
 * Descr:   Get status valid of a certain GPIO
 */

uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinStsValid(uint8 Idx)
{
    return GPIOMng.Pin[Idx].PinStsValid;
}

/*
 * Name:    void GPIOMng_GetPinStsChanged (uint8 Idx)
 * Descr:   Get changed property of a certain GPIO
 */

uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinStsChanged(uint8 Idx)
{
    return GPIOMng.Pin[Idx].PinStsChanged;
}

/*
 * Name:    void GPIOMng_RstPinStsChanged (uint8 Idx)
 * Descr:   Reset changed property of a certain GPIO
 */

void ICACHE_FLASH_ATTR GPIOMng_RstPinStsChanged(uint8 Idx)
{
    GPIOMng.Pin[Idx].PinStsChanged = FALSE;
}

/*
 * Name:    void GPIOMng_GetPinPressed (uint8 Idx)
 * Descr:   Get pressed button property of a certain GPIO
 */

uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinPressed(uint8 Idx)
{
    return GPIOMng.Pin[Idx].PinPressed;
}

void ICACHE_FLASH_ATTR GPIOMng_PinLock(uint8 Idx)
{
    DEBUG_INFO("Locking pin %d", Idx);
    GPIOMng.Pin[Idx].PinLocked = TRUE;
    GPIOMng.Pin[Idx].PinUnlocked = FALSE;
}

void ICACHE_FLASH_ATTR GPIOMng_PinUnlock(uint8 Idx)
{
    DEBUG_INFO("Unlocking pin %d", Idx);
    GPIOMng.Pin[Idx].PinLocked = FALSE;
    GPIOMng.Pin[Idx].PinUnlocked = TRUE;
}

uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinLocked(uint8 Idx)
{
    return GPIOMng.Pin[Idx].PinLocked;
}

uint8 ICACHE_FLASH_ATTR GPIOMng_GetPinUnlocked(uint8 Idx)
{
    return GPIOMng.Pin[Idx].PinUnlocked;
}

void ICACHE_FLASH_ATTR SET_LOW(uint8 gpio)
{
    GPIO_OUTPUT_SET(gpio, 0);
}

void ICACHE_FLASH_ATTR SET_HIGH(uint8 gpio)
{
    GPIO_OUTPUT_SET(gpio, 1);
}

void ICACHE_FLASH_ATTR SET_TOGGLE(uint8 gpio)
{
    GPIO_OUTPUT_SET(gpio, !GPIO_INPUT_GET(gpio));
}
