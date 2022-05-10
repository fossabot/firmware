/*
 * FileName:    main.c
 * Brief:       Main of PowaSwitch project
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
#include "gpio_mng.h"
#include "espconn.h"
#include "string.h"
#include "upgrade.h"
#include "AccessPoint_Mng.h"
#include "ButtonReq_Mng.h"
#include "Fota_Mng.h"
#include "NetworkReq_Mng.h"
#include "Mqtt_Mng.h"
#include "Nvm_Mng.h"
#include "Wifi_Mng.h"
#include "Misc.h"

/* ---------------------- Data Types ---------------------- */

typedef enum e_Wifi_Mng_Hdlr_Sts
{
    APP1_MNG_HDLR_IDLE = 0,
    APP1_MNG_HDLR_WIFI_CONF,
    APP1_MNG_HDLR_STATION_CONN_WAIT,
    APP1_MNG_HDLR_STATION_CONN_CHECK,
    APP1_MNG_HDLR_ACCESSPOINT_CONN_WAIT,
}t_App1_Mng_Hdlr_Sts;

/* Interval timers for tasks execution */
static ETSTimer Task_CYCLE_TIME_Tmr;
static ETSTimer Task1s_Tmr;
static ETSTimer Task1h_Tmr;
static ETSTimer DebugTmr;

/* To be fixed */
t_RollerBlind RollerBlind;

uint16_t dimmer_steps_done[2];
t_dimmer_state dimmer_curr_state[2];

/* Var for task states */
static t_App1_Mng_Hdlr_Sts App1_Mng_Hdlr_Sts = APP1_MNG_HDLR_WIFI_CONF;

uint8 boot_mode;
bool sub_flag;
bool check_my_sub_flag;
int8_t times;
size_t dev_type_len = 0;
bool device_connected;
bool memory_deallocation_flag = TRUE;

char base_topic[16];
const char * device_type[10] = { "CONFIG_TYPE_NORMAL_LIGHT", "CONFIG_TYPE_REMOTE_LIGHT",
                                "CONFIG_TYPE_NORMAL_ROLLER", "CONFIG_TYPE_REMOTE_ROLLER",
                                "CONFIG_TYPE_NORMAL_STEP_LIGHT", "CONFIG_TYPE_REMOTE_STEP_LIGHT",
                                "CONFIG_TYPE_NORMAL_LATCHED", "CONFIG_TYPE_REMOTE_LATCHED",
                                "CONFIG_TYPE_NORMAL_DIMMER", "CONFIG_TYPE_REMOTE_DIMMER"    };

#if defined(GLOBAL_DEBUG_ON)
    t_timer dimmer_pression_timer[2];
#endif

t_Nvm_Mng_Data *dev_conf;

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
 *******************************************************************************/
uint32 ICACHE_FLASH_ATTR
user_rf_cal_sector_set(void)
{
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

void send_relay_counter(void)
{
    // If it is a business device, don't send the relay counter
    if (is_business())
    {
        DEBUG_INFO("Device is a a business one: not sending relay counter");
        return;
    }
    // counter in order to send the relay counter every RELAY_COUNTER_HOURS_PERIOD hours
    // it is initialized as RELAY_COUNTER_HOURS_PERIOD in order to succeed on the first call
    static uint8_t hourly_counter = RELAY_COUNTER_HOURS_PERIOD;
    if (hourly_counter < RELAY_COUNTER_HOURS_PERIOD)
    {
        DEBUG_INFO("hourly_counter is %d/%d: not sending the relay counter", hourly_counter, RELAY_COUNTER_HOURS_PERIOD);
        hourly_counter++;
        return;
    }
    if (device_connected == FALSE)
    {
        DEBUG_INFO("Device is not connected: not sending relay counter");
        return;
    }

    char msg[128];
    os_sprintf(msg,"{ \"state\": { \"reported\": { \"firstRelayCounter\": %d, \"secondRelayCounter\": %d, \"rssi\": %d} } }", dev_conf->m_relay_counter[0], dev_conf->m_relay_counter[1], wifi_station_get_rssi());
    publish_on_topic("status/update", msg);

    // Reset the counter
    hourly_counter = 1;
}

static void ICACHE_FLASH_ATTR Task_App1(void)
{

//#ifndef GLOBAL_DEBUG_ON
//    os_printf("system_get_free_heap_size: %u\n", system_get_free_heap_size());
//#endif

    /* Wifi manager handler */
    Wifi_Mng_Hdlr();

    switch(App1_Mng_Hdlr_Sts)
    {
        case APP1_MNG_HDLR_IDLE:
            break;

        case APP1_MNG_HDLR_WIFI_CONF:
            /* In case boot_mode flag is zero, start in normal mode */
            if (boot_mode == CONFIG_BOOTMODE_NORMAL)
            {
                /* Wifi Configuration */
                Wifi_Mng_SetMode(WIFI_MNG_MODE_STATION);
                /* Go to check for MQTT connection request */
                App1_Mng_Hdlr_Sts = APP1_MNG_HDLR_STATION_CONN_WAIT;
            }
            else
            {
                /* Wifi Configuration */
                Wifi_Mng_SetMode(WIFI_MNG_MODE_ACCESSPOINT);
                /* Go to idle for access point waiting*/
                App1_Mng_Hdlr_Sts = APP1_MNG_HDLR_ACCESSPOINT_CONN_WAIT;
            }
            break;

        case APP1_MNG_HDLR_STATION_CONN_WAIT:
            /* If the wifi is connected */
            if (Wifi_Mng_GetStationStatus() == WIFI_MNG_STS_STATION_CONNOK)
            {
                /* Connect to MQTT server */
                Mqtt_Mng_Connect();

                /* Go to periodical check */
                App1_Mng_Hdlr_Sts = APP1_MNG_HDLR_STATION_CONN_CHECK;
            }
            break;

        case APP1_MNG_HDLR_STATION_CONN_CHECK:
            /* If connection get lost */
            if (Wifi_Mng_GetStationStatus() != WIFI_MNG_STS_STATION_CONNOK || (check_my_sub_flag && !sub_flag))
            {
                DEBUG_INFO("DISCONNECTING");
                /* Disconnect to MQTT server */
                Mqtt_Mng_Disconnect();
                disconnected_device_procedure(memory_deallocation_flag);
                sub_flag = TRUE;
                device_connected = FALSE;

                /* Go to periodical check */
                App1_Mng_Hdlr_Sts = APP1_MNG_HDLR_STATION_CONN_WAIT;
            }

            break;

        case APP1_MNG_HDLR_ACCESSPOINT_CONN_WAIT:
            /* idle for access point */
            break;

        default:
            break;

    }
}

static void ICACHE_FLASH_ATTR Task_CYCLE_TIME(void *arg)
{
    static uint64_t prev_cycle_time = 0;
    uint64_t curr_cycle_time = system_get_us_time();
    uint16_t elapsed_us_time = curr_cycle_time - prev_cycle_time;

    if (elapsed_us_time > MAX_CYCLE_TASK_TIME)
    {
        uint8_t missed_steps = elapsed_us_time/(CYCLE_TIME*MS_TO_US);

        if (is_roller())
        {
            if (going_up)
                if (dev_conf->m_rise_steps_curr_pos < rise_steps_to_reach)
                {
                    os_printf("[roller going_up] elapsed_us_time: %d/%d | missed_steps: %d\n", elapsed_us_time, MAX_CYCLE_TASK_TIME, missed_steps);
                    dev_conf->m_rise_steps_curr_pos += missed_steps;
                }
            if (going_down)
                if (dev_conf->m_fall_steps_curr_pos > fall_steps_to_reach)
                {
                    os_printf("[roller going_down] elapsed_us_time: %d/%d | missed_steps: %d\n", elapsed_us_time, MAX_CYCLE_TASK_TIME, missed_steps);
                    dev_conf->m_rise_steps_curr_pos += missed_steps;
                    dev_conf->m_fall_steps_curr_pos -= missed_steps;
                }
        }
        if (is_dimmer())
        {
            uint8_t d = 0;
            for (d = 0; d < 2; d++)
            {
                if (dimmer_curr_state[d] == DIMMER_COUNTING)
                {
                    os_printf("[dimmer SW%d] elapsed_us_time: %d/%d | missed_steps: %d\n", d+1, elapsed_us_time, MAX_CYCLE_TASK_TIME, missed_steps);
                    dimmer_steps_done[d] += missed_steps;
                }
            }
        }
    }

    check_for_NVM_save();

    //SET_TOGGLE(YELLOW_GPIO);

    if (dev_conf->m_need_feedback == TRUE)
    {
        times = 1;
        config_feedback(times, FALSE);
    }
    else
    {
        /* In case boot_mode flag is zero, start in normal mode */
        if (boot_mode == CONFIG_BOOTMODE_NORMAL)
        {
            /* GPIO */
            //SET_HIGH(CYAN_GPIO);
            GPIOMng_Handler();
            //SET_LOW(CYAN_GPIO);

            /* Button */
            //SET_HIGH(MAGENTA_GPIO);
            ButtonReq_Mng_Hdlr();
            //SET_LOW(MAGENTA_GPIO);

            /* FOTA */
            Fota_Mng_Hdlr();

            /* Network */
            //SET_HIGH(BLUE_GPIO);
            NetworkReq_Mng_Hdlr();
            //SET_LOW(BLUE_GPIO);
        }
        else
        {
            GPIOMng_Handler();
            /* Handler for applicative layer */
            ButtonReq_Mng_Hdlr();
            AccessPoint_Mng_Hdlr();
        }
    }

    prev_cycle_time = curr_cycle_time;
}

/*
 * Name:    void Task1s (void *arg)
 * Descr:   Task 1 sec
 */

static void ICACHE_FLASH_ATTR Task1s(void *arg)
{
    Task_App1();
}

void ICACHE_FLASH_ATTR Task1h(void *arg)
{
    send_relay_counter();
    static bool to_rearm = TRUE;

    if (to_rearm)
    {
        /* Setup cyclic task of 1h */
        os_timer_disarm(&Task1h_Tmr);
        os_timer_setfn(&Task1h_Tmr, (os_timer_func_t *)Task1h, NULL);
        os_timer_arm(&Task1h_Tmr, 1*HOUR, TRUE);    // timer starts one minute after the device's boot then it will be scheduled every hour

        to_rearm = FALSE;
    }
}

/*
 * Name:    void DebugTmr_Clbk (void *arg)
 * Descr:   Task for debug purpose.
 */

static void ICACHE_FLASH_ATTR DebugTmr_Clbk(void *arg)
{
    /* reconfigure this timer to be re-triggered */
    if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_CONFIG)
        return;

    if (is_switch())
        DEBUG_INFO("[%16llu us] %d(%d) - %d(%d) [FREE HEAP: %u]", system_get_us_time(), GPIOMng_GetPinState(L1), GPIOMng_GetPinState(SW1), GPIOMng_GetPinState(L2), GPIOMng_GetPinState(SW2), system_get_free_heap_size());
    else if (is_toggle() || is_latched())
        DEBUG_INFO("[%16llu us] %d | %d [FREE HEAP: %u]", system_get_us_time(), dev_conf->m_relecurrstatus_ch1, dev_conf->m_relecurrstatus_ch2, system_get_free_heap_size());
    else if (is_roller())
        DEBUG_INFO("[%16llu us] %d : %d\tdev_conf->m_rise_steps_curr_pos: %d | dev_conf->m_fall_steps_curr_pos: %d\t[FREE HEAP: %u]", system_get_us_time(), roll_curr_perc, roll_final_perc, dev_conf->m_rise_steps_curr_pos, dev_conf->m_fall_steps_curr_pos, system_get_free_heap_size());
    else if (is_dimmer())
        DEBUG_INFO("[%16llu us] %d (%d) | %d (%d) [%s | %s] [%s | %s]", system_get_us_time(), dev_conf->m_dimmer_logic_state[0], dev_conf->m_dimming_perc[0], dev_conf->m_dimmer_logic_state[1], dev_conf->m_dimming_perc[1], dimmer_direction[0] == HIGH ? "HIGH" : "LOW", dimmer_direction[1] == HIGH ? "HIGH" : "LOW", dimmer_pression_timer[0].expired ? "TRUE" : "FALSE", dimmer_pression_timer[1].expired ? "TRUE" : "FALSE");
    else
        ERROR_INFO("ERROR: invalid type [%d]", dev_conf->m_type);
}

void ICACHE_FLASH_ATTR powa_boot_banner(void)
{
    char string_version[20];
    char string_user[6];
    char char_type = Nvm_Mng_read_type();

    os_sprintf(string_version, "%d.%d.%d", FW_VERS_MAJOR, FW_VERS_MINOR, FW_VERS_BUGFIX);
    os_sprintf(string_user, "%s", system_get_userbin_addr() == 0x1000 ? "user1" : "user2");

    if (POWA_BANNER_FLAG)
    {
        DEBUG_INFO("###################################");
        DEBUG_INFO("# Powahome | Made in Rome with <3 #");
        DEBUG_INFO("# ------------------------------- #");
        DEBUG_INFO("# Version: %s                  #", string_version);
        DEBUG_INFO("# Device type: %s              #", string_user);
        DEBUG_INFO("# User: %c                     #", char_type);
        DEBUG_INFO("###################################\n\n");
    }
    else
    {
        DEBUG_INFO("     ###########################################################");
        DEBUG_INFO("     #   ____   _____        ___    _   _  ___  __  __ _____   #");
        DEBUG_INFO("     #  |  _ \\ / _ \\ \\      / / \\  | | | |/ _ \\|  \\/  | ____|  #");
        DEBUG_INFO("     #  | |_) | | | \\ \\ /\\ / / _ \\ | |_| | | | | |\\/| |  _|    #");
        DEBUG_INFO("     #  |  __/| |_| |\\ V  V / ___ \\|  _  | |_| | |  | | |___   #");
        DEBUG_INFO("     #  |_|    \\___/  \\_/\\_/_/   \\_\\_| |_|\\___/|_|  |_|_____|  #");
        DEBUG_INFO("     #                                                         #");
        DEBUG_INFO("     ###########################################################");
        DEBUG_INFO("            Version: %s       |       User: %s (%c)", string_version, string_user, char_type);
        DEBUG_INFO("     ###########################################################");
    }
}

/*
 * Name:    void TaskTmr_Init (void)
 * Descr:   Rountine used for configuring applicative tasks and timers
 */

void ICACHE_FLASH_ATTR TaskTmr_Init(void)
{
    /* Setup cyclic task of 1sec */
    os_timer_disarm(&Task1s_Tmr);
    os_timer_setfn(&Task1s_Tmr, (os_timer_func_t *)Task1s, NULL);
    os_timer_arm(&Task1s_Tmr, 1*SEC, TRUE);

    /* Setup cyclic task of CYCLE_TIME ms */
    os_timer_disarm(&Task_CYCLE_TIME_Tmr);
    os_timer_setfn(&Task_CYCLE_TIME_Tmr, (os_timer_func_t *)Task_CYCLE_TIME, NULL);
    os_timer_arm(&Task_CYCLE_TIME_Tmr, CYCLE_TIME, TRUE);

    /* Arm the timer to check if, after one minute, the device is connected: if it is, everything is ok;
    otherwise check if it's due to a probable FOTA gone wrong, in which case increase a counter to trigger a future system_rollback is the problem persists;
    if the connection problem it's not due the FOTA, just reboot the device, maybe it's just a temporary problem due to connection problem */
    if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_NORMAL)
        system_status_check_arm_timer();

    /* in case the boot is in normal mode */
    if (boot_mode == CONFIG_BOOTMODE_NORMAL)
    {
        /* Setup cyclic task of 1h */
        os_timer_disarm(&Task1h_Tmr);
        os_timer_setfn(&Task1h_Tmr, (os_timer_func_t *)Task1h, NULL);
        os_timer_arm(&Task1h_Tmr, 1*MIN, FALSE);    // timer starts one minute after the device's boot then it will be scheduled every hour

        /* Setup cyclic task for debug */
#if defined(GLOBAL_DEBUG_ON)
        os_timer_disarm(&DebugTmr);
        os_timer_setfn(&DebugTmr, (os_timer_func_t *)DebugTmr_Clbk, NULL);
        os_timer_arm(&DebugTmr, 1*SEC, TRUE);
#endif
    }

    powa_boot_banner();
}

void ICACHE_FLASH_ATTR check_relay_counter(void)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (dev_conf->m_relay_counter[i] == -1)
            dev_conf->m_relay_counter[i] = 0;
    }
}

void ICACHE_FLASH_ATTR check_config_version(void)
{
    /* If switching from old config to new one, update older step-timers to new ms-timers */
    if ((dev_conf->m_config_version == -1) || (dev_conf->m_config_version == 0) || (dev_conf->m_config_version == MAX_UINT16))
    {
        DEBUG_INFO("\t\t\tBEFORE check_config_version");
        DEBUG_INFO("dev_conf->m_config_version: %d", dev_conf->m_config_version);
        DEBUG_INFO("dev_conf->m_roll_totlen: %d", dev_conf->m_roll_totlen);
        DEBUG_INFO("dev_conf->m_roll_currval: %d", dev_conf->m_roll_currval);
        DEBUG_INFO("dev_conf->m_reletmr_ch1: %d (ms)", dev_conf->m_reletmr_ch1);
        DEBUG_INFO("dev_conf->m_reletmr_ch2: %d (ms)", dev_conf->m_reletmr_ch2);

        dev_conf->m_config_version = 1;
        dev_conf->m_roll_totlen *= 5;
        dev_conf->m_roll_currval *= 5;
        dev_conf->m_reletmr_ch1 *= 5;
        dev_conf->m_reletmr_ch2 *= 5;

        DEBUG_INFO("\t\t\tAFTER check_config_version");
        DEBUG_INFO("dev_conf->m_config_version: %d", dev_conf->m_config_version);
        DEBUG_INFO("dev_conf->m_roll_totlen: %d", dev_conf->m_roll_totlen);
        DEBUG_INFO("dev_conf->m_roll_currval: %d", dev_conf->m_roll_currval);
        DEBUG_INFO("dev_conf->m_reletmr_ch1: %d (ms)", dev_conf->m_reletmr_ch1);
        DEBUG_INFO("dev_conf->m_reletmr_ch2: %d (ms)", dev_conf->m_reletmr_ch2);
    }

    /* roller delay introduced with config_version = 2 */
    if (dev_conf->m_config_version < 2)
    {
        DEBUG_INFO("Upgrading config_version from %d to 2", dev_conf->m_config_version);
        dev_conf->m_config_version = 2;
        dev_conf->m_roller_delay = 0;
        DEBUG_INFO("dev_conf->m_roller_delay: %d", dev_conf->m_roller_delay);
    }

    if (!is_in_range(dev_conf->m_mqtt_loop_counter, 60000, 0))
        dev_conf->m_mqtt_loop_counter = 0;

    if (dev_conf->m_config_version < 4)
    {
        DEBUG_INFO("Upgrading config_version from %d to 4", dev_conf->m_config_version);
        dev_conf->m_config_version = 4;
        dev_conf->m_relay[0] = TRUE;
        dev_conf->m_relay[1] = TRUE;
        DEBUG_INFO("dev_conf->m_relay[0]: %s", dev_conf->m_relay[0] ? "TRUE" : "FALSE");
        DEBUG_INFO("dev_conf->m_relay[1]: %s", dev_conf->m_relay[1] ? "TRUE" : "FALSE");
    }
    if (dev_conf->m_config_version < 5)
    {
        DEBUG_INFO("Upgrading config_version from %d to 5", dev_conf->m_config_version);
        dev_conf->m_config_version = 5;
        dev_conf->m_roll_curr_perc = dev_conf->m_roll_currval*100/dev_conf->m_roll_totlen;
        // saturate the current percentage in the range [0, 100]
        if (dev_conf->m_roll_curr_perc > 100)
            dev_conf->m_roll_curr_perc = 100;
        if (dev_conf->m_roll_curr_perc < 0)
            dev_conf->m_roll_curr_perc = 0;
        dev_conf->m_rise_steps = dev_conf->m_roll_totlen/CYCLE_TIME;
        dev_conf->m_fall_steps = dev_conf->m_roll_totlen/CYCLE_TIME;
        DEBUG_INFO("dev_conf->m_roll_currval: %d", dev_conf->m_roll_currval);
        DEBUG_INFO("dev_conf->m_roll_curr_perc: %d", dev_conf->m_roll_curr_perc);
        DEBUG_INFO("dev_conf->m_rise_steps: %d", dev_conf->m_rise_steps);
        DEBUG_INFO("dev_conf->m_fall_steps: %d", dev_conf->m_fall_steps);
    }
}

bool ICACHE_FLASH_ATTR check_device_type(void)
{
    dev_type_len = sizeof(device_type)/sizeof(device_type[0]);

    if (dev_conf->m_type >= dev_type_len)
    {
        dev_conf->m_pam_mode = PAM_DISABLE;
        dev_conf->m_cfg_holder = 29;
        dev_conf->m_rebootreason = RR_AP_MODE;
        dev_conf->m_boot_mode = CONFIG_BOOTMODE_CONFIG;
        real_NVM_save();
        system_restart();
        return FALSE;
    }

    return TRUE;
}

void ICACHE_FLASH_ATTR type_consistency_check(void)
{
    uint8_t in_flash_value = 42;    // value that need to be read
    uint32_t to_flash_value = 33;    // value that need to be saved
    uint8_t *addr = (uint8_t *)&to_flash_value;

    /* Read value at the 0x000f7000 address */
    in_flash_value = Nvm_Mng_read_type();

    /* != R && != S*/
    if ((in_flash_value != 'R') && (in_flash_value != 'S'))
    {
        if (is_switch() || is_toggle() || is_latched())
            to_flash_value = 'S';
        else if (is_roller())
            to_flash_value = 'R';

        /* Erase sector before write;
         * > why?
         * < because magic
         */
        spi_flash_erase_sector(0xF7);
        spi_flash_write(0xF7 * SPI_FLASH_SEC_SIZE, (uint32_t *)addr, 1);
    }
}

void ICACHE_FLASH_ATTR user_init(void)
{
    /* UART initialization */
    uart_init(BIT_RATE_115200, BIT_RATE_115200);
    system_set_os_print(1);
    /* Load configuration for WIFI and MQTT Broker */
    bool cfg_load = Nvm_Mng_Load();
    /* Read the eeprom configuration */
    dev_conf = Nvm_Mng_GetNvm();

  #if (POWA_APCONFIG_MODE == PAM_OFF)
    dev_conf->m_boot_mode = CONFIG_BOOTMODE_NORMAL;
  #endif

    boot_mode = dev_conf->m_boot_mode;
    get_last_reset_cause();

    /* Initialize Service layer */
    Wifi_Mng_Init();
    Nvm_Mng_Init();
    Mqtt_Mng_Init();

    check_config_version();
    check_relay_counter();

    /* Initialize Applicative layer */
    misc_init();
    ButtonReq_Mng_Init();

    // The Network and FOTA applicative layer initialization were moved after MQTT connection due to memory constraints.
    // See Mqtt_Mng.c#mqttConnectedCb

    if (boot_mode == CONFIG_BOOTMODE_CONFIG)
        AccessPoint_Mng_Init();

    /* Check if the m_type field is consistent with the flash */
    type_consistency_check();

    times = 1;
    /* Configure task and timers for applicative functionalities */
    TaskTmr_Init();

    if (check_device_type())
    {
        DEBUG_INFO("");
        DEBUG_INFO("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
        DEBUG_INFO("");
        DEBUG_INFO("Main task running at %d ms", CYCLE_TIME);
        DEBUG_INFO("dev_conf->m_config_version: %d", dev_conf->m_config_version);
        DEBUG_INFO("dev_conf->m_type: %s", device_type[dev_conf->m_type]);
        DEBUG_INFO("dev_conf->m_roller_delay: %d", dev_conf->m_roller_delay);
        DEBUG_INFO("dev_conf->m_rise_steps: %d", dev_conf->m_rise_steps);
        DEBUG_INFO("dev_conf->m_fall_steps: %d", dev_conf->m_fall_steps);
        DEBUG_INFO("dev_conf->m_backoff: %d", dev_conf->m_backoff);
        DEBUG_INFO("SDK version: %s", system_get_sdk_version());
        DEBUG_INFO("System started...");
        DEBUG_INFO("");
        DEBUG_INFO("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    }
    else
    {
        ERROR_INFO("dev_conf->m_type: %d BIGGER THAN dev_type_len: %d. STARTING IN CONFIG_MODE!!", dev_conf->m_type, dev_type_len);
    }
}
