/*
 * FileName:    ButtonReq_Mng.c
 * Brief:       Button request manager, handling the button requests
 */

/* ----------------------- Includes ----------------------- */

#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "gpio_mng.h"
#include "espconn.h"
#include "string.h"
#include "upgrade.h"
#include "user_config.h"
#include "ButtonReq_Mng.h"
#include "Mqtt_Mng.h"
#include "Nvm_Mng.h"
#include "Misc.h"

/* ---------------------- Data Types ---------------------- */

typedef struct s_ButtonReq_Mng_Conf
{
    uint16  m_tmrval;
    uint8   m_cntval;
} t_ButtonReq_Mng_Conf;

typedef struct s_ButtonReq_Mng_Data
{
    uint8                         m_topicapp[64];
    uint8                         m_databuf[20];
    uint8                         m_seqcnt_ch1;     /* Counter to know number of click on button on ch1 */
    uint16                        m_seqtmr_ch1;     /* Counter to know how to wait from ON and OFF (STEP LIGHTS) on ch2 */
    uint8                         m_seqcnt_ch2;     /* Counter to know number of click on button on ch1 */
    uint16                        m_seqtmr_ch2;     /* Counter to know how to wait from ON and OFF (STEP LIGHTS) on ch2 */
    uint8                         m_magicseqcnt;
    uint16                        m_magicseqtmr;
    t_ButtonReq_Mng_Conf   *m_conf;
} t_ButtonReq_Mng_Data;

t_Nvm_Mng_Data *dev_conf;

static t_ButtonReq_Mng_Conf ButtonReq_Mng_Conf   =   {400, 16};
static t_ButtonReq_Mng_Data ButtonReq_Mng_Data;

/* Parameters used to reboot in AP_MODE changing status */
uint16_t timer_val = 1000;       // ms that need to pass between two consecutive status change [to reboot in AP_MODE]
uint8_t counter_val = 10;       // number of status change needed to reboot in AP_MODE
uint8_t magic_sequence;
uint16_t magic_timer;

/* Toggle timers flag, to know when to change the state */
uint8_t sequence_counter[2];    // keep track of switch status changes
uint16_t sequence_timer[2];     // used to check if [toggle] output needs to change
uint8_t queue_counter[2];       // keep track of status yet-to-change

/* pinout is "SW1 = 0; SW2 = 1; L2 = 2; L1 = 3" */
uint8_t sw_pin[2] = {SW1, SW2}; // switch pinout
uint8_t out_pin[2] = {L1, L2};  // output pinout
uint8_t sw_topic[2][16] = {"sw1/alert", "sw2/alert"};   // switch topic [for MQTT]
uint16_t button_save_timer[2] = {BUTTON_SAVE_TIMER + CYCLE_TIME, BUTTON_SAVE_TIMER + CYCLE_TIME};
uint16_t pressed_timer[2] = {0};

bool stopped = TRUE;
bool going_down = FALSE;
bool going_up = FALSE;
bool roller_timer_expired = FALSE;
bool network_feedback;
bool roller_network_command;
uint16_t roller_current_timer = 0;

uint16_t roller_tot_steps_done = 0;
uint16_t roller_steps_to_do;
uint16_t roller_steps_done;
uint16_t roller_steps_to_publish;
uint16_t roll_delta_perc;
uint16_t roll_final_perc;
float rise_fall_ratio = 0.0;
bool temp_rise = TRUE;
bool temp_fall = TRUE;

t_timer turning_timer = { .timer = 0, .expired = TRUE, .name = "turning timer" };
t_timer light_turning_timer[2] = {  {.timer = 0, .expired = TRUE, .name = "light turning timer [SW1]"}, {.timer = 0, .expired = TRUE, .name = "light turning timer [SW2]"}  };

/* DIMMER VARIABLES */
t_dimmer_state dimmer_curr_state[2];
bool dimmer_action_requested[2];
uint8_t dimmer_final_perc[2];
uint8_t dimmer_curr_perc[2];
uint8_t dimmer_delta_perc[2];
uint16_t dimmer_steps_to_do[2];
uint16_t dimmer_steps_done[2];
uint16_t dimmer_steps_to_publish[2];
uint32_t dimmer_tot_steps_done[2];
uint8_t dimmer_mqtt_final_perc[2];
bool dimmer_direction[2];    // HIGH = need to increase; LOW = need to decrease
t_timer dimmer_pression_timer[2];
t_timer dimmer_mqtt_timer[2];
t_timer dimmer_mqtt_inversion_timer[2];
t_timer dimmer_mqtt_charging_up_timer[2];
t_timer dimmer_mqtt_shortpress_timer[2];
t_timer dimmer_mqtt_resting_timer[2];
bool dimmer_counting[2];

bool ask_for_disconnection;
bool next_is_disconn_msg;
bool ap_mode_reboot_required;

/* ------------------- Local functions definitions ------------------- */

static void ICACHE_FLASH_ATTR ButtonReq_Mng_Roller_AccessPointAlert(void);
static void ICACHE_FLASH_ATTR ButtonReq_Mng_Roller(int);
static void ICACHE_FLASH_ATTR ButtonReq_Mng_Roller_Action(void);
static void ICACHE_FLASH_ATTR ButtonReq_Mng_Roller_PubAlert(bool);

/* ------------------- Global functions ------------------- */

void ICACHE_FLASH_ATTR arm_turning_timer()
{
    turning_timer.expired = FALSE;
    turning_timer.timer = 0;

    BUTTON_INFO("\t\t\t\t\t\tArming turning timer");
}

void ICACHE_FLASH_ATTR stop_or_damp(void)
{
    temp_rise = TRUE;
    temp_fall = TRUE;
    ERROR_INFO("roller_tot_steps_done: %d | MIN_STEPS_TO_DO: %d", roller_tot_steps_done, MIN_STEPS_TO_DO);
    /* STOP in all the case because we are not able to use in the right way the damp */
    // ERROR_INFO("roller_tot_steps_done: %d | MIN_STEPS_TO_DO: %d", roller_tot_steps_done, MIN_STEPS_TO_DO);
    // ERROR_INFO("STOP");
    update_roll_curr_perc(TRUE);
    ERROR_INFO("STOP");
    GPIOMng_SetPinState(L1, FALSE);
    GPIOMng_SetPinState(L2, FALSE);
    going_up = FALSE;
    going_down = FALSE;
    stopped = TRUE;
    arm_timer(&turning_timer);
    return;
    // TODO: implement damp here
}

void ICACHE_FLASH_ATTR arm_no_action_timer()
{
    RollerBlind.no_action = TRUE;
    RollerBlind.no_action_timer = 0;

    BUTTON_INFO("\t\t\t\t\t\tArming no action timer");
}

void ICACHE_FLASH_ATTR arm_roller_timer(void)
{
    BUTTON_INFO("Arming roller timer");
    roller_timer_expired = FALSE;
    roller_current_timer = 0;
}

uint16_t ICACHE_FLASH_ATTR roller_compute_steps_to_publish(bool rising_flag)
{
    uint16_t max_steps = 0;
    uint16_t curr_steps = 0;
    int16_t steps_to_publish = 0;
    uint16_t next_steps_landmark = 0;
    uint8_t curr_perc = 0;
    uint8_t quo = 0, rem = 0;

    // roller is rising, compute the ceiling for the next publish
    if (rising_flag)
    {
        max_steps = dev_conf->m_rise_steps;
        curr_steps = dev_conf->m_rise_steps_curr_pos;
        curr_perc = curr_steps*100/max_steps;
        // Compute the quotient and reminder
        quo = curr_perc/ROLLER_PUB_PERC;
        rem = curr_perc%ROLLER_PUB_PERC;
        BUTTON_INFO("curr_perc: %d | ROLLER_PUB_PERC: %d | max_steps: %d | quo: %d | rem: %d", curr_perc, ROLLER_PUB_PERC, max_steps, quo, rem);
        // Compute the amount of steps for the next publish (this compute the ceiling)
        next_steps_landmark = (max_steps*(quo + (rem > 0))*ROLLER_PUB_PERC)/100;
        // Compute the number of steps to do in order to publish
        steps_to_publish = next_steps_landmark - curr_steps;
        // Account for inaccurate steps counting, which happen when we press the button near a percentage suitable for the publish
        if (steps_to_publish <= 0)
            steps_to_publish = max_steps*ROLLER_PUB_PERC/100;
        BUTTON_INFO("\n");
        BUTTON_INFO("roller_compute_steps_to_publish | %s | max_steps: %d | curr_steps: %d | next_steps_landmark: %d | steps_to_publish: %d", rising_flag ? "RISING" : "FALLING", max_steps, curr_steps, next_steps_landmark, steps_to_publish);
    }
    // roller is falling, compute the floor for the next publish
    else
    {
        max_steps = dev_conf->m_fall_steps;
        curr_steps = dev_conf->m_fall_steps_curr_pos;
        curr_perc = curr_steps*100/max_steps;
        // Compute the quotient
        quo = curr_perc/ROLLER_PUB_PERC;
        BUTTON_INFO("curr_perc: %d | ROLLER_PUB_PERC: %d | max_steps: %d | quo: %d | rem: %d", curr_perc, ROLLER_PUB_PERC, max_steps, quo, rem);
        // Compute the amount of steps for the next publish (this compute the floor)
        next_steps_landmark = (max_steps*quo*ROLLER_PUB_PERC)/100;
        // Compute the number of steps to do in order to publish
        steps_to_publish = curr_steps - next_steps_landmark;
        // Account for inaccurate steps counting, which happen when we press the button near a percentage suitable for the publish
        if (steps_to_publish <= 0)
            steps_to_publish = max_steps*ROLLER_PUB_PERC/100;
        BUTTON_INFO("\n");
        BUTTON_INFO("roller_compute_steps_to_publish | %s | max_steps: %d | curr_steps: %d | next_steps_landmark: %d | steps_to_publish: %d", rising_flag ? "RISING" : "FALLING", max_steps, curr_steps, next_steps_landmark, steps_to_publish);
    }

    return steps_to_publish;
}

void ICACHE_FLASH_ATTR ButtonReq_Mng_Init(void)
{
    dev_conf = Nvm_Mng_GetNvm();
    int i;

    /* TO CHECK: kinda useless */
    if (is_toggle() || is_latched())
        counter_val /= 2;

    /* Initialize global variables */
    for (i = 0; i < 2; i++)
    {
        sequence_counter[i] = 0;
        sequence_timer[i] = 0;
        queue_counter[i] = 0;
    }

    if (is_dimmer())
    {
        dimmer_curr_state[0] = DIMMER_INIT;
        dimmer_curr_state[1] = DIMMER_INIT;
    }

    if (is_roller())
    {
        /* Set to the right position the roller if in overflow */
        if (dev_conf->m_roll_currval == ROLL_MAX_TOTLEN/2)
        {
            DEBUG_INFO("ButtonReq_Mng_Init | dev_conf->m_roll_currval: %d", dev_conf->m_roll_currval);
            dev_conf->m_roll_currval = dev_conf->m_roll_totlen;
        }

        if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_CONFIG)
        {
            /* if in config, start in middle position and assume huge totlen to span the entire roller */
            dev_conf->m_roll_totlen = ROLL_MAX_TOTLEN;
            RollerBlind.m_rollcurrval = dev_conf->m_roll_totlen/2;
        }
        else
        {
            RollerBlind.m_rollcurrval = dev_conf->m_roll_currval;
        }

        RollerBlind.m_rollfinalval = 0;
        RollerBlind.m_rollactreq = FALSE;

        rise_fall_ratio = (float) dev_conf->m_rise_steps/dev_conf->m_fall_steps;
        char float_buff[16];
        print_float(rise_fall_ratio, float_buff, 2);

        ERROR_INFO("");
        ERROR_INFO("\tdev_conf->m_roll_curr_perc: %d", dev_conf->m_roll_curr_perc);
        ERROR_INFO("\tdev_conf->m_rise_steps: %d", dev_conf->m_rise_steps);
        ERROR_INFO("\tdev_conf->m_fall_steps: %d", dev_conf->m_fall_steps);
        ERROR_INFO("\tdev_conf->m_rise_steps_curr_pos: %d", dev_conf->m_rise_steps_curr_pos);
        ERROR_INFO("\tdev_conf->m_fall_steps_curr_pos: %d", dev_conf->m_fall_steps_curr_pos);
        ERROR_INFO("\trise_fall_ratio: %s", float_buff);
        ERROR_INFO("");

        // Initialize the roller with the output disabled and in a "stopped" state
        going_up    = FALSE;
        going_down  = FALSE;
        stopped     = TRUE;
        GPIOMng_SetPinState(L1, FALSE);
        GPIOMng_SetPinState(L2, FALSE);
    }
}

void ICACHE_FLASH_ATTR handle_magic_sequence(int index)
{
    if (index == 1)
        return;

    if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_CONFIG)
        return;

    #if (POWA_APCONFIG_MODE == PAM_ON)
        if (dev_conf->m_pam_mode == PAM_ENABLE)
        {
            if (disconnected_for_reboot)    // did you receive the PUBACK for the disconnection message
            {
                restartable = FALSE;    
                if (ap_mode_reboot_required)
                {
                    dev_conf->m_pam_mode = PAM_DISABLE;
                    dev_conf->m_boot_mode = CONFIG_BOOTMODE_CONFIG;
                    dev_conf->m_cfg_holder = 29;
                    dev_conf->m_rebootreason = RR_AP_MODE;
                }
                real_NVM_save();
                system_restart();           // then restart
                return;
            }

            magic_timer = 0;
            BUTTON_INFO("\t\tAP magic_sequence: %d/%d", magic_sequence, counter_val);

            if (++magic_sequence >= counter_val)
            {
                if (device_connected == TRUE)
                {
                    ap_mode_reboot_required = true;
                    restartable = true;
                }
                else
                {
                    dev_conf->m_pam_mode = PAM_DISABLE;
                    dev_conf->m_boot_mode = CONFIG_BOOTMODE_CONFIG;
                    dev_conf->m_cfg_holder = 29;
                    dev_conf->m_rebootreason = RR_AP_MODE;
                    real_NVM_save();
                    system_restart();
                    return;
                }

                DEBUG_INFO("OFFING the outputs");
                GPIOMng_SetPinState(L1, FALSE);
                GPIOMng_SetPinState(L2, FALSE);
                stopped = true;
                return;
            }
        }
      #endif
}

void ICACHE_FLASH_ATTR ButtonReq_Mng_Toggle(int index)
{
    /* get the timer that says how much the relay need to be fed */
    uint16_t compare_timer = (index == 0 ? dev_conf->m_reletmr_ch1 : dev_conf->m_reletmr_ch2);

    if ((GPIOMng_GetPinStsValid(sw_pin[index]) == TRUE) && (GPIOMng_GetPinStsChanged(sw_pin[index]) == TRUE))
    {
        // if the button is pressed
        if (GPIOMng_GetPinState(sw_pin[index]) == FALSE)
        {
            if (is_toggle())
                GPIOMng_SetPinState(out_pin[index], TRUE);
        }
        else
        {
            /* If toggle is released */
            if (magic_sequence < STOP_ITER_LOAD_CONFIG)
            {
                if (dev_conf->m_relay[index] == TRUE)
                    queue_counter[index]++;
                else
                {
                    GPIOMng_SetPinState(out_pin[index], !GPIOMng_GetPinState(out_pin[index]));
                    if (index == 0)
                        dev_conf->m_relecurrstatus_ch1 = (dev_conf->m_relecurrstatus_ch1 + 1)%dev_conf->m_numberstatus_ch1;
                    else
                        dev_conf->m_relecurrstatus_ch2 = (dev_conf->m_relecurrstatus_ch2 + 1)%dev_conf->m_numberstatus_ch2;
                    Nvm_Mng_Save();
                    /* publish the new state */
                    char msg[8];
                    int_to_char(msg, (index == 0 ? dev_conf->m_relecurrstatus_ch1 : dev_conf->m_relecurrstatus_ch2));
                    publish_on_topic(sw_topic[index], msg);
                }
            }
        }

        GPIOMng_RstPinStsChanged(sw_pin[index]);

        #if (POWA_APCONFIG_MODE == PAM_ON)
            handle_magic_sequence(index);
        #endif
    }

    /* there are still more states to change */
    if (queue_counter[index] > 0)
    {
        /* check if timer is still running */
        if (sequence_timer[index] < compare_timer)
        {
            // DEBUG_INFO("sequence_timer[%d] = %d < %d", index, sequence_timer[index], compare_timer);
            /* start feeding the relay */
            GPIOMng_SetPinState(out_pin[index], TRUE);
            /* increase the timer's counter */
            sequence_timer[index] += CYCLE_TIME;
        }

        /* timer just expired (i.e. time to stop feeding the relay) */
        if (sequence_timer[index] >= compare_timer)
        {
            /* if an additional relay is present, stop feeding it */
            if (dev_conf->m_relay[index] == TRUE)
                GPIOMng_SetPinState(out_pin[index], FALSE);

            /* update the new state */
            if (index == 0)
                dev_conf->m_relecurrstatus_ch1 = (dev_conf->m_relecurrstatus_ch1 + 1)%dev_conf->m_numberstatus_ch1;
            else
                dev_conf->m_relecurrstatus_ch2 = (dev_conf->m_relecurrstatus_ch2 + 1)%dev_conf->m_numberstatus_ch2;

            /* save state to NVM */
            Nvm_Mng_Save();
            /* decrease queue counter (i.e. one less state to change) */
            queue_counter[index]--;
            /* publish the new state */
            char msg[8];
            int_to_char(msg, (index == 0 ? dev_conf->m_relecurrstatus_ch1 : dev_conf->m_relecurrstatus_ch2));
            publish_on_topic(sw_topic[index], msg);
            /* rearm the timer */
            sequence_timer[index] = 0;
        }
    }
}

void ICACHE_FLASH_ATTR ButtonReq_Mng_Switch(int index)
{
    char in_flash_type = Nvm_Mng_read_type();

    /* Increase button save timer */
    if (button_save_timer[index] < BUTTON_SAVE_TIMER)
        button_save_timer[index] += CYCLE_TIME;

    /* If timer expires */
    if ((button_save_timer[index] == BUTTON_SAVE_TIMER) && (in_flash_type == 'S'))
    {
        int current_pin_state = GPIOMng_GetPinState(out_pin[index]);
        int saved_pin_state = dev_conf->m_statuspin[out_pin[index]];

        if (current_pin_state != saved_pin_state)
        {
            /* Skip saving because the actual value is different from what I want to save */

            /* Save on structure */
            dev_conf->m_statuspin[out_pin[index]] = current_pin_state;

            /* Reset button save timer */
            button_save_timer[index] = 0;
            BUTTON_INFO("Skip saving state for pin %d...", index);
        } else
        {
            /* Save current status */
            BUTTON_INFO("Timer expired (%d ms) for pin %d, saving state %d to NVM...", button_save_timer[index], index, dev_conf->m_statuspin[out_pin[index]]);
            Nvm_Mng_Save();
            BUTTON_INFO("State of pin %d saved %d to NVM!", index, dev_conf->m_statuspin[out_pin[index]]);

            /* Avoid sequential saves */
            button_save_timer[index] = BUTTON_SAVE_TIMER + CYCLE_TIME;
        }
    }

    /* If pin is not valid, return */
    if ((GPIOMng_GetPinStsValid(sw_pin[index]) == FALSE))
        return;

    /* If pin status did not change */
    if (GPIOMng_GetPinStsChanged(sw_pin[index]) == FALSE)
        return;

    if (magic_sequence < STOP_ITER_LOAD_CONFIG)
    {
        if (light_turning_timer[index].expired == TRUE)
        {
            int pin_state = GPIOMng_GetPinState(out_pin[index]);
            int new_pin_state = !pin_state;

            GPIOMng_SetPinState(out_pin[index], new_pin_state);
            arm_timer(light_turning_timer + index);

            /* Publish on alert topic for Smartphone App update */
            char msg[8];
            int_to_char(msg, new_pin_state);
            publish_on_topic(sw_topic[index], msg);

            /* Save on structure */
            dev_conf->m_statuspin[out_pin[index]] = new_pin_state;
        }

        /* Reset button save timer */
        button_save_timer[index] = 0;
    }

    GPIOMng_RstPinStsChanged(sw_pin[index]);

    #if (POWA_APCONFIG_MODE == PAM_ON)
        handle_magic_sequence(index);
    #endif
}

static void ICACHE_FLASH_ATTR ButtonReq_Mng_Roller_New_Action(void)
{
    static uint8 prev_action_req = FALSE;

    /* Is an action requested? */
    if (RollerBlind.m_rollactreq == TRUE)
    {
        /* It just switched from OFF to ON */
        if (prev_action_req != RollerBlind.m_rollactreq)
        {
            turning_timer.timer = MAX_TURNING_TIMER;
            ButtonReq_Mng_Roller_PubAlert(FALSE);
        }

        // FALLING
        if (going_down)
        {
            if ((fall_steps_to_reach < dev_conf->m_fall_steps_curr_pos) && (magic_sequence < STOP_ITER_LOAD_CONFIG))
            {
                /* FALLING */
                if (roller_timer_expired)
                {
                    if (temp_fall)
                    {
                        DEBUG_INFO("Falling | Start counting!");
                        temp_fall = FALSE;
                    }
                    dev_conf->m_fall_steps_curr_pos--;
                    roller_steps_done++;
                }

                // if was stopped, start moving but avoid instant reverse
                if (stopped == TRUE)
                    arm_timer(&turning_timer);

                stopped = FALSE;
                going_up = FALSE;
                going_down = TRUE;

                GPIOMng_SetPinState(L1, FALSE);
                GPIOMng_SetPinState(L2, TRUE);
            }
            else
            {
                update_roll_curr_perc(TRUE);
                RollerBlind.m_rollactreq = FALSE;
            }
        }
        if (going_up)
        {
            if ((rise_steps_to_reach > dev_conf->m_rise_steps_curr_pos) && (magic_sequence < STOP_ITER_LOAD_CONFIG))
            {
                /* RISING*/
                if (roller_timer_expired)
                {
                    if (temp_rise)
                    {
                        DEBUG_INFO("RISING | Start counting!");
                        temp_rise = FALSE;
                    }
                    dev_conf->m_rise_steps_curr_pos++;
                    roller_steps_done++;
                }

                // if was stopped, start moving but avoid instant reverse
                if (stopped == TRUE)
                    arm_timer(&turning_timer);

                stopped = FALSE;
                going_up = TRUE;
                going_down = FALSE;

                GPIOMng_SetPinState(L1, TRUE);
                GPIOMng_SetPinState(L2, FALSE);
            }
            else
            {
                update_roll_curr_perc(TRUE);
                RollerBlind.m_rollactreq = FALSE;
            }
        }
        if ((dev_conf->m_rise_steps_curr_pos == rise_steps_to_reach) && (dev_conf->m_fall_steps_curr_pos == fall_steps_to_reach))
        {
            update_roll_curr_perc(TRUE);
            RollerBlind.m_rollactreq = FALSE;
            stopped = TRUE;
            going_up = FALSE;
            going_down = FALSE;
        }

        /* Publish an update every ROLLER_PUB_PERC of the run */
        if ((roller_steps_done == roller_steps_to_publish) && (roller_steps_done != 0))
        {
            ButtonReq_Mng_Roller_PubAlert(FALSE);

            if (going_up)
                roller_steps_to_publish = roller_compute_steps_to_publish(true);
            else if (going_down)
                roller_steps_to_publish = roller_compute_steps_to_publish(false);
        }
    }
    else
    {
        /* Stop roller */
        if (GPIOMng_GetPinState(L1) != GPIOMng_GetPinState(L2))
        {
//          GPIOMng_SetPinState(L2, FALSE);
//          GPIOMng_SetPinState(L1, FALSE);
            DEBUG_INFO("\t\t\t\t\t\t\t\t\t\tSTOPPING HERE!");
            stop_or_damp();

            stopped = TRUE;
            going_up = FALSE;
            going_down = FALSE;
            arm_timer(&turning_timer);
            ButtonReq_Mng_Roller_PubAlert(TRUE);
        }

        if (roller_network_command == TRUE)
            roller_network_command = FALSE;
    }

    /* Update last value */
    prev_action_req = RollerBlind.m_rollactreq;
}

static void ICACHE_FLASH_ATTR ButtonReq_Mng_Roller_PubAlert(bool save_flag)
{
    uint8_t roller_curr_perc = update_roll_curr_perc(save_flag);
    uint8_t roller_pub_perc = 0;

    // Round up the percentage, to the closest ROLLER_PUB_PERC multiple, only if the roller is moving, otherwise (i.e. it is not moving) publish the actual percetage
    if (!stopped)
    {
        // EXPLAINATION: round the percentage to the closest (if there is a tie, round toward the smallest) ROLLER_PUB_PERC multiple
        // EXAMPLE: ROLLER_PUB_PERC = 5; roller_curr_perc = 62;
        //          roller_pub_perc = 5*((62/5) + ((62%5) > (5/2))) = 5*(12 + (2 > 2)) = 5*(12 + 0) = 5*12 = 60
        // EXAMPLE: ROLLER_PUB_PERC = 5; roller_curr_perc = 63;
        //          roller_pub_perc = 5*((63/5) + ((63%5) > (5/2))) = 5*(12 + (3 > 2)) = 5*(12 + 1) = 5*13 = 65
        // NOTE: if you prefer to round up (in case of a tie) toward the biggest, replace ">" with ">="
        roller_pub_perc = ROLLER_PUB_PERC*((roller_curr_perc/ROLLER_PUB_PERC) + ((roller_curr_perc%ROLLER_PUB_PERC) > (ROLLER_PUB_PERC/2)));
    }
    else
        roller_pub_perc = roller_curr_perc;

    os_sprintf(ButtonReq_Mng_Data.m_databuf, "%d:%d", roller_pub_perc, roll_final_perc);
    publish_on_topic("rb/alert", ButtonReq_Mng_Data.m_databuf);
    roller_steps_done = 0;
}

bool ICACHE_FLASH_ATTR ButtonReq_Mng_GetRollActReq(void)
{
    return RollerBlind.m_rollactreq;
}

uint16_t ICACHE_FLASH_ATTR update_dimmer_curr_perc(uint8_t index, bool save_flag)
{
    int8_t perc_done = 0;

    // dimmering up
    if (dimmer_direction[index] == HIGH)
    {
        perc_done = (dimmer_steps_done[index]*100)/(dev_conf->m_dimming_steps[index]);
        if (dev_conf->m_dimming_perc[index] + perc_done > 100)
            dev_conf->m_dimming_perc[index] = 100;
        else
            dev_conf->m_dimming_perc[index] += perc_done;
    }
    else
    {
        perc_done = (dimmer_steps_done[index]*100)/(dev_conf->m_dimming_steps[index]);
        if (dev_conf->m_dimming_perc[index] - perc_done < 0)
            dev_conf->m_dimming_perc[index] = 0;
        else
            dev_conf->m_dimming_perc[index] -= perc_done;
    }

    if (save_flag)
        Nvm_Mng_Save();

    if (dev_conf->m_dimming_perc[index] == 0)
    {
        dimmer_steps_to_do[index] = 0;
        dimmer_curr_perc[index] = 0;
    }
    else
    {
        dimmer_steps_to_do[index] -= dimmer_steps_done[index];
        dimmer_tot_steps_done[index] += dimmer_steps_done[index];
        dimmer_steps_done[index] = 0;
        dimmer_curr_perc[index] = dev_conf->m_dimming_perc[index];
    }

    return dev_conf->m_dimming_perc[index];
}

void ICACHE_FLASH_ATTR detect_dimmer_action(uint8_t index)
{
    if (dimmer_pression_timer[index].expired)
        return;

    DEBUG_INFO("detect_dimmer_action | dimmer_pression_timer[%d]: %d", index, dimmer_pression_timer[index].timer);
    if (is_in_range(dimmer_pression_timer[index].timer, DIMMER_MIN_SHORTPRESS_TIME, DIMMER_MAX_SHORTPRESS_TIME))
    {
        if (dimmer_counting[index])
        {
            arm_timer(dimmer_mqtt_shortpress_timer + index);
            dimmer_curr_state[index] = DIMMER_MQTT_PRE_SHORTPRESS;
            dimmer_direction[index] = !dimmer_direction[index];
        }
        else
            dimmer_curr_state[index] = DIMMER_SHORTPRESS;
    }

    if (dimmer_pression_timer[index].timer > DIMMER_MIN_LONGPRESS_TIME)
        dimmer_curr_perc[index] = update_dimmer_curr_perc(index, TRUE);

    dimmer_pression_timer[index].expired = TRUE;
    dimmer_pression_timer[index].timer = 0;
    dimmer_steps_to_do[index] = 0;
    dimmer_steps_done[index] = 0;

    GPIOMng_SetPinState(out_pin[index], LOW);
}

void ICACHE_FLASH_ATTR dimmer_pub_message(uint8_t index, bool save_flag)
{
    dev_conf->m_dimming_perc[index] = update_dimmer_curr_perc(index, save_flag);

    os_sprintf(ButtonReq_Mng_Data.m_databuf, "%d:%d", dev_conf->m_dimmer_logic_state[index], dev_conf->m_dimming_perc[index]);
    publish_on_topic(sw_topic[index], ButtonReq_Mng_Data.m_databuf);
}

bool ICACHE_FLASH_ATTR ButtonReq_Mng_Dimmer(uint8_t index)
{
    uint8_t dimmer_done_perc;

    handle_timer(dimmer_pression_timer + index, CYCLE_TIME, -1); // -1 means "greatest possible time"

    switch (dimmer_curr_state[index])
    {
        // initialize the dimmer
        case DIMMER_INIT:
            dimmer_counting[index] = FALSE;
            dimmer_action_requested[index] = FALSE;
            dimmer_steps_to_do[index] = 0;
            dimmer_tot_steps_done[index] = 0;
            dimmer_delta_perc[index] = 0;
            dimmer_final_perc[index] = 0;
            dimmer_steps_to_publish[index] = dev_conf->m_dimming_steps[index]/DIMMER_PUB_PERC;
            dimmer_curr_perc[index] = dev_conf->m_dimming_perc[index];
            dimmer_direction[index] = HIGH;
            dimmer_curr_state[index] = DIMMER_IDLE;
            dimmer_pression_timer[index].timer = 0;
            dimmer_pression_timer[index].expired = TRUE;
            dimmer_mqtt_inversion_timer[index].timer = 0;
            dimmer_mqtt_inversion_timer[index].expired = TRUE;
            dimmer_mqtt_charging_up_timer[index].timer = 0;
            dimmer_mqtt_charging_up_timer[index].expired = TRUE;
            dimmer_mqtt_shortpress_timer[index].timer = 0;
            dimmer_mqtt_shortpress_timer[index].expired = TRUE;
            dimmer_mqtt_resting_timer[index].timer = 0;
            dimmer_mqtt_resting_timer[index].expired = TRUE;
            os_sprintf(dimmer_pression_timer[index].name, "dimmer_pression_timer[%d]", index);
            break;
        // toggle logic state
        case DIMMER_SHORTPRESS:
            DEBUG_INFO("DIMMER_SHORTPRESS");
            dev_conf->m_dimmer_logic_state[index] = !dev_conf->m_dimmer_logic_state[index];
            dimmer_counting[index] = FALSE;
            dimmer_curr_state[index] = DIMMER_IDLE;
            dimmer_action_requested[index] = FALSE;
            char msg[8];
            if (dev_conf->m_dimmer_logic_state[index] == 0)
                int_to_char(msg, dev_conf->m_dimmer_logic_state[index]);
            else
                os_sprintf(msg, "1:%d", dev_conf->m_dimming_perc[index]);
            publish_on_topic(sw_topic[index], msg);
            break;
        // dimmer the output
        case DIMMER_COUNTING:
            if ((dimmer_steps_done[index] >= dimmer_steps_to_do[index])) // && (dimmer_steps_to_do[index] != 0))
            {
                DEBUG_INFO("DIMMER_COUNTING | dimmer_steps_done == dimmer_steps_to_do | Switching to DIMMER_IDLE");
                dimmer_pub_message(index, TRUE);
                dimmer_counting[index] = FALSE;
                dimmer_curr_state[index] = DIMMER_IDLE;
                dimmer_action_requested[index] = FALSE;
                dimmer_curr_perc[index] = dimmer_final_perc[index];
                dimmer_steps_to_do[index] = 0;
                dimmer_steps_done[index] = 0;
                dimmer_direction[index] = !dimmer_direction[index];
                GPIOMng_SetPinState(out_pin[index], LOW);
                dimmer_counting[index] = FALSE;
                dev_conf->m_dimmer_logic_state[index] = HIGH;
                break;
            }
            if (dimmer_steps_done[index] < dimmer_steps_to_do[index])
            {
                if (dimmer_curr_perc[index] == 0)
                    dimmer_direction[index] = HIGH;

                dimmer_counting[index] = TRUE;
                dimmer_steps_done[index]++;
                dimmer_done_perc = (dimmer_steps_done[index]*100)/dev_conf->m_dimming_steps[index];

                GPIOMng_SetPinState(out_pin[index], HIGH);
                dev_conf->m_dimmer_logic_state[index] = HIGH;

                if (dimmer_done_perc >= DIMMER_PUB_PERC)
                    dimmer_pub_message(index, FALSE);
                break;
            }

            DEBUG_INFO("DIMMER_COUNTING | Nothing of the above! | done: %d | to_do: %d", dimmer_steps_done[index], dimmer_steps_to_do[index]);
            break;
        case DIMMER_IDLE:
            if (dimmer_pression_timer[index].expired)
                break;
            if (dimmer_pression_timer[index].timer >= DIMMER_RESET_TIME)
            {
                publish_on_serial("DIMMER_IDLE | Switching to DIMMER_RESET");
                dimmer_curr_state[index] = DIMMER_RESET;
                break;
            }
            if (dimmer_action_requested[index] == FALSE)
                break;
            if (dimmer_pression_timer[index].timer <= DIMMER_MIN_LONGPRESS_TIME)
                break;

            publish_on_serial("DIMMER_IDLE | entering DIMMER_COUNTING");
            if (dev_conf->m_dimmer_logic_state[index] == LOW)
            {
                dimmer_final_perc[index] = 100;
                dimmer_direction[index] = HIGH;
                dimmer_curr_perc[index] = 0;
                dev_conf->m_dimming_perc[index] = 0;
            }
            else
            {
                dimmer_final_perc[index] = dimmer_direction[index] == HIGH ? 100 : 0;
                dimmer_curr_perc[index] = update_dimmer_curr_perc(index, FALSE);
            }

            dimmer_delta_perc[index] = abs(dimmer_final_perc[index] - dimmer_curr_perc[index]);
            dimmer_steps_to_do[index] = max((dimmer_delta_perc[index]*dev_conf->m_dimming_steps[index])/100, DIMMER_MIN_LONGPRESS_TIME/CYCLE_TIME);
            dimmer_steps_done[index] = 0;
            dimmer_curr_state[index] = DIMMER_COUNTING;
            dev_conf->m_dimmer_logic_state[index] = HIGH;

            if (dimmer_delta_perc[index] == 0)
            {
                dimmer_action_requested[index] = FALSE;
                if (dimmer_final_perc[index] == 0)
                    dev_conf->m_dimmer_logic_state[index] = LOW;
                break;
            }

            break;
        case DIMMER_RESET:
            DEBUG_INFO("\t\t\tDIMMER_RESET");
            dimmer_pression_timer[index].expired = TRUE;
            dimmer_pression_timer[index].timer = 0;
            dev_conf->m_dimmer_logic_state[index] = HIGH;
            dev_conf->m_dimming_perc[index] = 100;
            dimmer_action_requested[index] = FALSE;
            dimmer_curr_perc[index] = 100;
            dimmer_final_perc[index] = 100;
            dimmer_steps_done[index] = 0;
            dimmer_steps_to_do[index] = 0;
            dimmer_direction[index] = LOW;
            dimmer_counting[index] = FALSE;
            dimmer_curr_state[index] = DIMMER_IDLE;
            break;
        case DIMMER_MQTT_DIRECTION_INVERSION:
            //DEBUG_INFO("\t\tDIMMER_MQTT_DIRECTION_INVERSION");
            if (dimmer_mqtt_inversion_timer[index].timer >= DIMMER_MIN_LONGPRESS_TIME)
            {
                dimmer_mqtt_inversion_timer[index].expired = TRUE;
                dimmer_final_perc[index] = dimmer_mqtt_final_perc[index];
                dimmer_curr_perc[index] = update_dimmer_curr_perc(index, FALSE);
                dimmer_delta_perc[index] = abs(dimmer_final_perc[index] - dimmer_curr_perc[index]);
                dimmer_steps_to_do[index] = max((dimmer_delta_perc[index]*dev_conf->m_dimming_steps[index])/100, DIMMER_MIN_LONGPRESS_TIME/CYCLE_TIME);
                dimmer_steps_done[index] = 0;
                dimmer_direction[index] = (dimmer_final_perc[index] > dimmer_curr_perc[index]) ? HIGH : LOW;
                arm_timer(dimmer_mqtt_resting_timer + index);
                dimmer_curr_state[index] = DIMMER_MQTT_RESTING;
            }
            GPIOMng_SetPinState(out_pin[index], HIGH);
            dimmer_mqtt_inversion_timer[index].timer += CYCLE_TIME;
            break;
        case DIMMER_MQTT_RESTING:
            DEBUG_INFO("\t\t DIMMER_MQTT_RESTING");
            if (dimmer_mqtt_resting_timer[index].timer >= DIMMER_REST_TIME)
            {
                dimmer_mqtt_resting_timer[index].expired = TRUE;
                dimmer_curr_state[index] = DIMMER_MQTT_CHARGING_UP;
            }
            GPIOMng_SetPinState(out_pin[index], LOW);
            dimmer_mqtt_resting_timer[index].timer += CYCLE_TIME;
            break;
        case DIMMER_MQTT_CHARGING_UP:
           if (dimmer_mqtt_charging_up_timer[index].timer >= DIMMER_MIN_LONGPRESS_TIME)
           {
                dimmer_mqtt_charging_up_timer[index].expired = TRUE;
                dimmer_curr_state[index] = DIMMER_COUNTING;
           }
           dimmer_mqtt_charging_up_timer[index].timer += CYCLE_TIME;
           break;
        case DIMMER_MQTT_PRE_SHORTPRESS:
            if (dimmer_mqtt_shortpress_timer[index].expired == FALSE)
            {
                if (dimmer_mqtt_shortpress_timer[index].timer >= DIMMER_MIN_SHORTPRESS_TIME)
                {
                    dimmer_mqtt_shortpress_timer[index].expired = TRUE;
                    arm_timer(dimmer_mqtt_shortpress_timer + index);
                    dimmer_curr_state[index] = DIMMER_MQTT_SHORTPRESS;
                }
                else
                {
                    GPIOMng_SetPinState(out_pin[index], LOW);
                    dimmer_mqtt_shortpress_timer[index].timer += CYCLE_TIME;
                }
                break;
            }
            break;
        case DIMMER_MQTT_SHORTPRESS:
            DEBUG_INFO("DIMMER_MQTT_SHORTPRESS");
            if (dimmer_mqtt_shortpress_timer[index].expired == FALSE)
            {
                if (dimmer_mqtt_shortpress_timer[index].timer >= DIMMER_MIN_SHORTPRESS_TIME)
                {
                    dev_conf->m_dimmer_logic_state[index] = !dev_conf->m_dimmer_logic_state[index];
                    GPIOMng_SetPinState(out_pin[index], LOW);
                    dimmer_mqtt_shortpress_timer[index].expired = TRUE;
                    dimmer_action_requested[index] = FALSE;
                    if (dev_conf->m_dimmer_logic_state[index] == HIGH)
                        dimmer_pub_message(index, TRUE);
                    else
                    {
                        int_to_char(msg, 0);
                        publish_on_topic(sw_topic[index], msg);
                    }
                    dimmer_counting[index] = FALSE;
                    dimmer_curr_state[index] = DIMMER_IDLE;
                }
                else
                {
                    GPIOMng_SetPinState(out_pin[index], HIGH);
                    dimmer_mqtt_shortpress_timer[index].timer += CYCLE_TIME;
                }
                break;
            }
            break;
        default:
            break;
    }

    if (GPIOMng_GetPinStsValid(sw_pin[index]) == FALSE)
        return;
    if (GPIOMng_GetPinStsChanged(sw_pin[index]) == FALSE)
        return;

    // button pressed
    if (GPIOMng_GetPinState(sw_pin[index]) == FALSE)
    {
        char serial_msg[32];
        os_sprintf(serial_msg, "Button %d pressed", index);
        publish_on_serial(serial_msg);
        dimmer_curr_state[index] = DIMMER_IDLE;
        GPIOMng_SetPinState(out_pin[index], TRUE);
        arm_timer(dimmer_pression_timer + index);
    }
    // button released
    else
    {
        DEBUG_INFO("dimmer_curr_state[%d]: %d", index, dimmer_curr_state[index]);
        detect_dimmer_action(index);
    }

    dimmer_action_requested[index] = TRUE;
    GPIOMng_RstPinStsChanged(sw_pin[index]);

    #if (POWA_APCONFIG_MODE == PAM_ON)
        handle_magic_sequence(index);
    #endif
}

void ICACHE_FLASH_ATTR handle_light_turning_timer(int8_t index)
{
    if (light_turning_timer[index].expired == TRUE)
        return;

    if (light_turning_timer[index].timer >= MAX_LIGHT_TURNING_TIMER)
    {
        BUTTON_INFO("\t\t\t\t\t\tlight_turning_timer[%d] expired", index);
        light_turning_timer[index].timer = MAX_LIGHT_TURNING_TIMER;
        light_turning_timer[index].expired = TRUE;
        return;
    }

    light_turning_timer[index].timer += CYCLE_TIME;
}

void ICACHE_FLASH_ATTR handle_pressed_timer(void)
{
    int i = 0;
    for (i = 0; i < 2; i++)
    {
        if (GPIOMng_GetPinPressed(sw_pin[i]) == TRUE)
        {
            if (pressed_timer[i] < PRESSED_THRESHOLD)
                pressed_timer[i] += CYCLE_TIME;

            if (pressed_timer[i] == PRESSED_THRESHOLD)
                       {
                               DEBUG_INFO("Locking pin %d", sw_pin[i]);
                GPIOMng_PinLock(sw_pin[i]);
                               pressed_timer[i]++;
                       }
        }
        else
        {
            pressed_timer[i] = 0;
                       if (GPIOMng_GetPinLocked(sw_pin[i]) == TRUE)
                       {
                               DEBUG_INFO("Unlocking pin %d", sw_pin[i]);
                               GPIOMng_PinUnlock(sw_pin[i]);
                       }
        }
    }
}

void ICACHE_FLASH_ATTR handle_turning_timer(void)
{
    if (turning_timer.expired == TRUE)
        return;

    if (turning_timer.timer >= MAX_TURNING_TIMER)
    {
        BUTTON_INFO("\t\t\t\t\t\tTurning timer expired");
        turning_timer.timer = MAX_TURNING_TIMER;
        turning_timer.expired = TRUE;
        return;
    }

    turning_timer.timer += CYCLE_TIME;
}

void ICACHE_FLASH_ATTR handle_no_action_timer(void)
{
    if (RollerBlind.no_action == FALSE)
        return;

    if (RollerBlind.no_action_timer >= MAX_TURNING_TIMER)
    {
        BUTTON_INFO("\t\t\t\t\t\tNo action timer expired");
        RollerBlind.no_action_timer = MAX_TURNING_TIMER;
        RollerBlind.no_action = FALSE;
        return;
    }

    RollerBlind.no_action_timer += CYCLE_TIME;
}

void ICACHE_FLASH_ATTR handle_roller_timer(void)
{
    if (roller_timer_expired == TRUE)
        return;

    if (roller_current_timer >= dev_conf->m_roller_delay + (MAX_TURNING_TIMER - turning_timer.timer))
    {
        BUTTON_INFO("Roller timer expired!");
        roller_current_timer = dev_conf->m_roller_delay + MAX_TURNING_TIMER - turning_timer.timer;
        roller_timer_expired = TRUE;
        return;
    }

    roller_current_timer += CYCLE_TIME;
}

void ICACHE_FLASH_ATTR ButtonReq_Mng_Hdlr(void)
{
    if (network_feedback)
        return;

    // Avoid input detection when doing the reboot procedure
    if (restartable)
        return;

    int i;

    handle_roller_timer();
    handle_turning_timer();
    handle_no_action_timer();
    handle_light_turning_timer(SW1);
    handle_light_turning_timer(SW2);

    if (is_roller())
    {
        if ((turning_timer.expired) && (dev_conf->m_boot_mode == CONFIG_BOOTMODE_NORMAL))
            ButtonReq_Mng_Roller_New_Action();
    }
    else if (is_toggle() || is_latched())
    {
        ButtonReq_Mng_Toggle(0);
        ButtonReq_Mng_Toggle(1);
    }
    else if (is_switch())
    {
        ButtonReq_Mng_Switch(0);
        ButtonReq_Mng_Switch(1);
    }
    else if (is_dimmer())
    {
        ButtonReq_Mng_Dimmer(0);
        ButtonReq_Mng_Dimmer(1);
    }

    /* In case Access Point mode is enabled */
    #if (POWA_APCONFIG_MODE == PAM_ON)
        magic_timer += CYCLE_TIME;

        if (magic_timer == timer_val + MAX_TURNING_TIMER)
        {
            BUTTON_INFO("TIMEOUT EXPIRED: resetting magic sequence");
            magic_sequence = 0;
        }
    #endif

    /* Code valid only if it is a roller */
    if (!is_roller())
        return;

    if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_CONFIG)
    {
        /* If the pin are not valid */
        if ((GPIOMng_GetPinStsValid(SW1) != TRUE) || (GPIOMng_GetPinStsValid(SW2) != TRUE))
            return;

        /* If the turning timer is not expired */
        if (!turning_timer.expired)
            return;

        /* If no action could be processed */
        if (RollerBlind.no_action)
            return;

        /* Gets changed state for pins */
        uint8 is_sw1_changed = GPIOMng_GetPinStsChanged(SW1);
        uint8 is_sw2_changed = GPIOMng_GetPinStsChanged(SW2);

        /* Gets pressed state for pins */
        uint8 is_sw1_pressed = GPIOMng_GetPinPressed(SW1);
        uint8 is_sw2_pressed = GPIOMng_GetPinPressed(SW2);

        /* If no pins changed, move the roller */
        if ((is_sw1_changed == FALSE) && (is_sw2_changed == FALSE))
            return;

        /* If it is pressed only the rise, RISE the roller */
        if (((is_sw1_pressed == TRUE) || (GPIOMng_GetPinLocked(SW1) == TRUE)) && (is_sw2_pressed == FALSE))
        {
            if (going_down)
            {
                GPIOMng_SetPinState(L1, FALSE);
                GPIOMng_SetPinState(L2, FALSE);
                arm_turning_timer();
                stopped = TRUE;
                going_up = FALSE;
                going_down = FALSE;
                return; // stop the roller without resetting the status change of the pin
            }
            else
            {
                GPIOMng_SetPinState(L2, FALSE);
                GPIOMng_SetPinState(L1, TRUE);
                arm_timer(&turning_timer);
                stopped = FALSE;
                going_up = TRUE;
                going_down = FALSE;
            }
        }

        /* If it is pressed only the fall, FALL the roller */
        if ((is_sw1_pressed == FALSE) && ((is_sw2_pressed == TRUE) || (GPIOMng_GetPinLocked(SW2) == TRUE)))
        {
            if (going_up)
            {
                GPIOMng_SetPinState(L1, FALSE);
                GPIOMng_SetPinState(L2, FALSE);
                arm_timer(&turning_timer);
                stopped = TRUE;
                going_up = FALSE;
                going_down = FALSE;
                return; // stop the roller without resetting the status change of the pin
            }
            else
            {
                GPIOMng_SetPinState(L1, FALSE);
                GPIOMng_SetPinState(L2, TRUE);
                arm_timer(&turning_timer);
                stopped = FALSE;
                going_up = FALSE;
                going_down = TRUE;
            }
        }

        if ((is_sw1_pressed == is_sw2_pressed) && (stopped == FALSE))
        {
            GPIOMng_SetPinState(L1, FALSE);
            GPIOMng_SetPinState(L2, FALSE);
            stopped = TRUE;
            arm_timer(&turning_timer);
        }

        /* If it is pressed only one of them */
        // if (((is_sw1_pressed == TRUE) && (is_sw2_pressed == FALSE)) || ((is_sw1_pressed == FALSE) && (is_sw2_pressed == TRUE)))
        if ((is_sw1_pressed ^ is_sw2_pressed) == TRUE)
        {
            /* Arms no action timer */
            if (stopped == FALSE)
                arm_no_action_timer();
        }

        /* Reset pins status changed in order to know if they will change the next time */
        GPIOMng_RstPinStsChanged(SW1);
        GPIOMng_RstPinStsChanged(SW2);
    }
    else
    {
        if (GPIOMng_GetPinStsValid(SW1) == TRUE)
        {
            if ((roller_network_command == TRUE) && (GPIOMng_GetPinLocked(SW1) == TRUE) && going_down)
            {
                stop_or_damp();
                roller_network_command = FALSE;
            }
            else if (GPIOMng_GetPinStsChanged(SW1) == TRUE)
            {
                arm_roller_timer();
                // if UP button is pressed
                if (GPIOMng_GetPinState(SW1) == FALSE)
                {
                    // if roller is still
                    if (RollerBlind.m_rollactreq == FALSE)
                    {
                        roll_final_perc = 100;
                        rise_steps_to_reach = dev_conf->m_rise_steps;
                        roller_steps_to_publish = roller_compute_steps_to_publish(true);
                        RollerBlind.m_rollactreq = TRUE;
                        going_up    = TRUE;
                        going_down  = FALSE;
                        stopped     = FALSE;
                    }
                    if ((roller_network_command == TRUE) && (GPIOMng_GetPinLocked(SW1) == TRUE))
                    {
                        stop_or_damp();
                        roll_final_perc = 100;
                        rise_steps_to_reach = dev_conf->m_rise_steps;
                        roller_steps_to_publish = roller_compute_steps_to_publish(true);
                        RollerBlind.m_rollactreq = TRUE;
                        roller_network_command = FALSE;
                        arm_timer(&turning_timer);
                    }
                }
                // UP button has been released
                else
                {
                    // it was going down
                    if (going_down)
                    {
                        if (GPIOMng_GetPinPressed(SW2) == FALSE)
                        {
                            ButtonReq_Mng_Roller_PubAlert(FALSE);
                            stop_or_damp();
                            RollerBlind.m_rollactreq = FALSE;
                        }
                    }
                    if (GPIOMng_GetPinLocked(SW1))
                    {
                        RollerBlind.m_rollactreq = FALSE;
                        GPIOMng_PinUnlock(SW1);
                    }
                }
                GPIOMng_RstPinStsChanged(SW1);

                #if (POWA_APCONFIG_MODE == PAM_ON)
                    handle_magic_sequence(0);
                #endif
            }
        }

        if (GPIOMng_GetPinStsValid(SW2) == TRUE)
        {
            if ((roller_network_command == TRUE) && (GPIOMng_GetPinLocked(SW2) == TRUE) && going_up)
            {
                stop_or_damp();
                roller_network_command = FALSE;
            }
            else if (GPIOMng_GetPinStsChanged(SW2) == TRUE)
            {
                arm_roller_timer();
                // if down button is pressed
                if (GPIOMng_GetPinState(SW2) == FALSE)
                {
                    // if roller is still
                    if (RollerBlind.m_rollactreq == FALSE)
                    {
                        roll_final_perc = 0;
                        fall_steps_to_reach = 0;
                        roller_steps_to_publish = roller_compute_steps_to_publish(false);
                        RollerBlind.m_rollactreq = TRUE;
                        going_up    = FALSE;
                        going_down  = TRUE;
                        stopped     = FALSE;
                    }
                    if ((roller_network_command == TRUE) && (GPIOMng_GetPinLocked(SW2) == TRUE))
                    {
                        stop_or_damp();
                        roll_final_perc = 0;
                        fall_steps_to_reach = 0;
                        roller_steps_to_publish = roller_compute_steps_to_publish(false);
                        RollerBlind.m_rollactreq = TRUE;
                        roller_network_command = FALSE;
                        arm_timer(&turning_timer);
                    }
                }
                // DOWN button has been released
                else
                {
                    // it was going up
                    if (going_up)
                    {
                        if (GPIOMng_GetPinPressed(SW1) == FALSE)
                        {
                            ButtonReq_Mng_Roller_PubAlert(FALSE);
                            stop_or_damp();
                            RollerBlind.m_rollactreq = FALSE;
                        }
                    }
                    if (GPIOMng_GetPinLocked(SW2))
                    {
                        RollerBlind.m_rollactreq = FALSE;
                        GPIOMng_PinUnlock(SW2);
                    }
                }
                GPIOMng_RstPinStsChanged(SW2);
            }
        }
    }

    /* In case Access Point mode is enabled */
    #if (POWA_APCONFIG_MODE == PAM_ON)
        magic_timer += CYCLE_TIME;

        if (magic_timer == timer_val + MAX_TURNING_TIMER)
        {
            BUTTON_INFO("TIMEOUT EXPIRED: resetting magic sequence");
            magic_sequence = 0;
        }
    #endif
}
