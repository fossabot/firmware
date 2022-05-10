/*
 * FileName:    AccessPoint_Mng.c
 * Brief:       Access Point manager for handling smartphone config
 */

/* ----------------------- Includes ----------------------- */

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
#include "Wifi_Mng.h"
#include "Nvm_Mng.h"

/* ----------------------- Defines ------------------------ */



/* ---------------------- Data Types ---------------------- */

typedef enum e_AccessPoint_Mng_Hdlr_Sts
{
    ACCESSPOINT_MNG_HDLR_INIT = 0,
    ACCESSPOINT_MNG_HDLR_WAITDATA,
    ACCESSPOINT_MNG_HDLR_CONFDEV,
    ACCESSPOINT_MNG_HDLR_FEEDBACK,
    ACCESSPOINT_MNG_HDLR_IDLE,
}t_AccessPoint_Mng_Hdlr_Sts;

typedef struct s_AccessPoint_Mng_Conf
{
    uint16  m_port;
    uint8   m_sckttype;
} t_AccessPoint_Mng_Conf;

typedef struct s_AccessPoint_Mng_Data
{
    struct espconn              m_espconn;      /* Esp connection object */
    t_AccessPoint_Mng_Hdlr_Sts  m_hdlrsts;      /* Current handler fsm state */
    uint8                       m_isdatarcv;    /* Has data received? */
    t_AccessPoint_Mng_Conf     *m_conf;         /* Default configuration */
    uint8                       m_datarcv[256]; /* Receving buffer */
} t_AccessPoint_Mng_Data;

/* -------------- Local function prototypes --------------- */

static void ICACHE_FLASH_ATTR AccessPoint_Mng_ReceiveData(void *arg, char *pdata, unsigned short len);
static void ICACHE_FLASH_ATTR AccessPoint_Mng_Activate(void);

/* ------------- Local variable declaration --------------- */


static t_AccessPoint_Mng_Data *AccessPoint_Mng_Data;
uint8_t feedback_timer = 0;
bool network_feedback;
int8_t times;
//uint32_t RISE = 0;
//uint32_t STOP = 0;
//uint32_t FALL = 0;
bool backoff_timer_enabled;

/* ------------------- Local functions -------------------- */

/*
 * Name:    static void AccessPoint_Mng_ReceiveData (void)
 * Descr:   Rountine called as callback when data is received when boot_mode = 1
 *          Main purpose is to parse configuration data received from smartphone, during
 *          configuration process, and configure PWH
 */

static void ICACHE_FLASH_ATTR AccessPoint_Mng_ReceiveData(void *arg, char *pdata, unsigned short len)
{
    /* Copy the received data int local buffer */
    os_memcpy(AccessPoint_Mng_Data->m_datarcv, pdata, len);
    DEBUG_INFO("Received: %s [len: %u]", pdata, len);
    /* Signal received data */
    AccessPoint_Mng_Data->m_isdatarcv = TRUE;
}

/*
 * Name:    void AccessPoint_Mng_Activate (void)
 * Descr:   Rountine called at ESP8266 Initialization
 */

static void ICACHE_FLASH_ATTR AccessPoint_Mng_Activate(void)
{
    /* Register on_connection callback */
    espconn_regist_recvcb(&AccessPoint_Mng_Data->m_espconn, AccessPoint_Mng_ReceiveData);
    /* Open a socket */
    espconn_accept(&AccessPoint_Mng_Data->m_espconn);
}

/* ------------------- Global functions ------------------- */

/*
 * Name:    void AccessPoint_Mng_Init (void)
 * Descr:   Access point manager initialize function
 */

t_AccessPoint_Mng_Data ICACHE_FLASH_ATTR *alloc_new_access_point_structure(void)
{
    t_AccessPoint_Mng_Data *new_struct = os_zalloc(sizeof(char)*sizeof(t_AccessPoint_Mng_Data));
    new_struct->m_conf = os_zalloc(sizeof(char)*sizeof(t_AccessPoint_Mng_Conf));

    new_struct->m_hdlrsts = ACCESSPOINT_MNG_HDLR_INIT;
    new_struct->m_conf->m_port = 2000;
    new_struct->m_conf->m_sckttype = ESPCONN_TCP;

    return new_struct;
}

void ICACHE_FLASH_ATTR AccessPoint_Mng_Init(void)
{
    DEBUG_INFO("AccessPoint_Mng_Init...");
    AccessPoint_Mng_Data = alloc_new_access_point_structure();
    DEBUG_INFO("AccessPoint_Mng_Init complete!");
}

void ICACHE_FLASH_ATTR light_config_feedback(int feed_times, bool need_reboot)
{
    t_Nvm_Mng_Data *dev_conf = Nvm_Mng_GetNvm();

//    if (feed_times == 0)
//        return;
//
//    if (is_in_range(feedback_timer, 0, LIGHT_FEEDBACK_TIMER - 1))
//    {
//        GPIOMng_SetPinState(L1, TRUE);
//        GPIOMng_SetPinState(L2, TRUE);
//        feedback_timer += 5;
//    }
//    else if (is_in_range(feedback_timer, LIGHT_FEEDBACK_TIMER, 2*LIGHT_FEEDBACK_TIMER - 1))
//    {
//        GPIOMng_SetPinState(L1, FALSE);
//        GPIOMng_SetPinState(L2, FALSE);
//        feedback_timer += 5;
//    }
//    else if (is_in_range(feedback_timer, 2*LIGHT_FEEDBACK_TIMER, 3*LIGHT_FEEDBACK_TIMER - 1))
//    {
//        GPIOMng_SetPinState(L1, TRUE);
//        GPIOMng_SetPinState(L2, TRUE);
//        feedback_timer += 5;
//    }
//    else if (is_in_range(feedback_timer, 3*LIGHT_FEEDBACK_TIMER, 4*LIGHT_FEEDBACK_TIMER - 1))
//    {
//        GPIOMng_SetPinState(L1, FALSE);
//        GPIOMng_SetPinState(L2, FALSE);
//        feedback_timer += 5;
//    }
//
//    if (feedback_timer >= 4*LIGHT_FEEDBACK_TIMER)
//    {
//        if (--times > 0)
//            feedback_timer = 0;
//        else
//        {
            dev_conf->m_need_feedback = FALSE;
            real_NVM_save();

            if (need_reboot == FALSE)
                return;

            DEBUG_INFO("light_config_feedback | NO MORE TIMES");

            if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_CONFIG)
            {
                /* Start in normal mode */
                dev_conf->m_boot_mode = CONFIG_BOOTMODE_NORMAL;
                /* Set the reboot reason to "device configuration" */
                dev_conf->m_rebootreason = RR_AP_MODE;
                /* Save new config in EEPROM */
                real_NVM_save();
                /* Restart PWH */
                AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_IDLE;
                DEBUG_INFO("Starting system from AP mode into NORMAL mode");
                system_restart();
            }
            else if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_NORMAL)
            {
                network_feedback = false;

                char msg[256];
                DEBUG_INFO("backoff_timer_enabled: %s", backoff_timer_enabled ? "TRUE" : "FALSE");
                if (backoff_timer_enabled == TRUE)
                    restartable = true;
                else
                {
                    to_be_restarted = true;
                    os_sprintf(msg, "{ \"state\": { \"reported\": { \"connected\": false, \"configMode\": true, \"clientId\": \"%s\", \"sessionId\": \"%s\", \"isLwt\": false } } }", mqtt_client_id, session_id);
                    publish_on_topic("status/update", msg);
                }
            }
//        }
//    }
}

void ICACHE_FLASH_ATTR roller_config_feedback(int feed_times, bool need_reboot)
{
    t_Nvm_Mng_Data *dev_conf = Nvm_Mng_GetNvm();

//    if (feed_times == 0)
//        return;
//
//    RISE = (10*dev_conf->m_rise_time)/100;
//    FALL = (10*dev_conf->m_fall_time)/100;
//    STOP = 1000;
//
//    //DEBUG_INFO("feed_times: %d | RISE: %u | FALL: %u | STOP: %u | feedback_timer: %u", feed_times, RISE, FALL, STOP, feedback_timer);
//
//    /* TIMING EXPLAINATION
//     * all the labels inside the [] refer to the following assumption
//     * ASSUMPTION:     RISE = FALL = 2000ms, STOP = 1000ms
//     * legend: [t0, tf] means "from t0 to tf, extremes included"
//     * EXAMPLE:
//     * roller moving [3000, 4999] means "the roller will move for 1999ms (4999 - 3000)"
//     * */
//
//    // roller moving [0, 1999]
//    if (is_in_range(feedback_timer, 0, FALL - 1))
//    {
//        GPIOMng_SetPinState(L1, FALSE);
//        GPIOMng_SetPinState(L2, TRUE);
//        feedback_timer += 5;
//    }
//    // roller stopped [2000, 2999]
//    else if (is_in_range(feedback_timer, FALL, FALL + STOP - 1))
//    {
//        GPIOMng_SetPinState(L1, FALSE);
//        GPIOMng_SetPinState(L2, FALSE);
//        feedback_timer += 5;
//    }
//    // roller moving (reverse) [3000, 4999]
//    else if (is_in_range(feedback_timer, FALL + STOP, FALL + STOP + RISE - 1))
//    {
//        GPIOMng_SetPinState(L2, FALSE);
//        GPIOMng_SetPinState(L1, TRUE);
//        feedback_timer += 5;
//    }
//    // roller stopped [5000, 5999]
//    else if (is_in_range(feedback_timer, FALL + STOP + RISE, FALL + 2*STOP + RISE - 1))
//    {
//        GPIOMng_SetPinState(L1, FALSE);
//        GPIOMng_SetPinState(L2, FALSE);
//        feedback_timer += 5;
//    }
//    /* END OF FEEDBACK ROUNDS */
//    if (feedback_timer >= FALL + 2*STOP + RISE)
//    {
//        if (--times > 0)
//            feedback_timer = 0;
//        else
//        {
            dev_conf->m_need_feedback = FALSE;
            real_NVM_save();

            if (need_reboot == FALSE)
                return;

            DEBUG_INFO("roller_config_feedback | NO MORE TIMES");

            if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_CONFIG)
            {
                /* Start in normal mode */
                dev_conf->m_boot_mode = CONFIG_BOOTMODE_NORMAL;
                /* Set the reboot reason to "device configuration" */
                dev_conf->m_rebootreason = RR_AP_MODE;
                /* Save new config in EEPROM */
                real_NVM_save();
                /* Restart PWH */
                AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_IDLE;
                DEBUG_INFO("Starting system from AP mode into NORMAL mode");
                system_restart();
            }
            else if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_NORMAL)
            {
                network_feedback = false;

                char msg[256];
                DEBUG_INFO("backoff_timer_enabled: %s", backoff_timer_enabled ? "TRUE" : "FALSE");
                if (backoff_timer_enabled == TRUE)
                    restartable = true;
                else
                {
                    to_be_restarted = true;
                    os_sprintf(msg, "{ \"state\": { \"reported\": { \"connected\": false, \"configMode\": true, \"clientId\": \"%s\", \"sessionId\": \"%s\", \"isLwt\": false } } }", mqtt_client_id, session_id);
                    custom_restart(FALSE, FALSE, msg);
                }
            }
//        }
//    }
}

void ICACHE_FLASH_ATTR config_feedback(int times, bool need_reboot)
{
    if ((times == 2) && (feedback_timer == 0)) {
        char msg[256];
        os_sprintf(msg, "{ \"state\": { \"reported\": { \"connected\": false, \"configMode\": true, \"clientId\": \"%s\", \"sessionId\": \"%s\", \"isLwt\": false } } }", mqtt_client_id, session_id);
        publish_on_topic("status/update", msg);
    }

    t_Nvm_Mng_Data *dev_conf = Nvm_Mng_GetNvm();

    if (is_roller())
        roller_config_feedback(times, need_reboot);
    else if (is_switch() || is_toggle() || is_latched() || is_dimmer())
        light_config_feedback(times, need_reboot);
    else
        ERROR_INFO("\r\nERROR: type %d is not a valid one!\n", dev_conf->m_type);
}


/*
 * Name:    void AccessPoint_Mng_Hdlr (void)
 * Descr:   Access Point manager handler
 */

void ICACHE_FLASH_ATTR AccessPoint_Mng_Hdlr(void)
{
    t_Nvm_Mng_Data *dev_conf;

    char *pun;
    uint8_t mac[6];
    char err_msg[256];

    switch(AccessPoint_Mng_Data->m_hdlrsts)
    {
        case ACCESSPOINT_MNG_HDLR_INIT:
            /* Set PWH to go in Access Point mode */
            if (Wifi_Mng_GetAccessPointStatus() == WIFI_MNG_STS_ACCESSPOINT_ON)
            {
                /* Setup access point socket parameters  */
                AccessPoint_Mng_Data->m_espconn.type = AccessPoint_Mng_Data->m_conf->m_sckttype;
                AccessPoint_Mng_Data->m_espconn.state = ESPCONN_NONE;
                AccessPoint_Mng_Data->m_espconn.proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
                AccessPoint_Mng_Data->m_espconn.proto.tcp->local_port = AccessPoint_Mng_Data->m_conf->m_port;
                /* Start access point */
                AccessPoint_Mng_Activate();
                /* Go to idle state waiting for commands */
                AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
            }
            break;

        case ACCESSPOINT_MNG_HDLR_WAITDATA:
            /* Has data received? */
            if (AccessPoint_Mng_Data->m_isdatarcv == TRUE)
            {
                /* Move handler to data analyzing state */
                AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_CONFDEV;
            }
            break;

        case ACCESSPOINT_MNG_HDLR_CONFDEV:
            /* Reset received data flasg */
            AccessPoint_Mng_Data->m_isdatarcv = FALSE;
            /* Read the eeprom configuration */
            dev_conf = Nvm_Mng_GetNvm();
            /* Analyze the received packet for PWH configuration */
            pun = strtok(AccessPoint_Mng_Data->m_datarcv, "\r");
            if (strncmp(pun, "SET", 3) == 0)
            {
                /* Wifi SSID or config_version */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }

                if (strncmp(pun, "?", 1) == 0)
                {
                    char msg[16];
                    os_sprintf(msg, "v%d.%d.%d", FW_VERS_MAJOR, FW_VERS_MINOR, FW_VERS_BUGFIX);
                    espconn_send(&AccessPoint_Mng_Data->m_espconn, msg, os_strlen(msg));
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    DEBUG_INFO("Received: %s | Sent: %s", pun, msg);
                    break;
                }

                int temp_val = atoi(pun);
                /* if App DIDN'T send config_version */
                if (temp_val < MIN_CONFIG_VAL || temp_val > MAX_CONFIG_VAL)
                {
                    DEBUG_INFO("Missing config_version! Old App!");
                    dev_conf->m_config_version = 0;
                    /* get WiFi SSID */
                    strcpy(dev_conf->m_sta_ssid, pun);
                }
                else
                {
                    /* string is EXACTLY "1", meaning that is the version */
                    if (!valid_config_version(pun))
                    {
                        DEBUG_INFO("Missing config_version! Old App!");
                        dev_conf->m_config_version = 0;
                        /* get WiFi SSID */
                        strcpy(dev_conf->m_sta_ssid, pun);
                        DEBUG_INFO("SSID: %s", dev_conf->m_sta_ssid);
                    }
                    else
                    {
                        /* get config_version */
                        dev_conf->m_config_version = atoi(pun);
                        DEBUG_INFO("Config version: %d", dev_conf->m_config_version);
                        /* get WiFi SSID */
                        pun = strtok(NULL, "\r");
                        strcpy(dev_conf->m_sta_ssid, pun);
                    }
                }
                DEBUG_INFO("SSID: %s", dev_conf->m_sta_ssid);

                /* Wifi PWD | can't check this one */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                strcpy(dev_conf->m_sta_pwd, pun);
                DEBUG_INFO("WiFi PWD: %s", dev_conf->m_sta_pwd);

                /* Wifi Type (don't care) */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }

                /* MQTT Host */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                //if (strcmp(pun, MQTT_HOST) != 0)
                //{
                //    ERROR_INFO("Error: invalid mqtt_host {%s}", pun);
                //    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                //    return;
                //}
                strcpy(dev_conf->m_mqtt_host, pun);
                DEBUG_INFO("MQTT Host: %s", dev_conf->m_mqtt_host);

                /* MQTT Port */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                dev_conf->m_mqtt_port = atoi(pun);
                //if (dev_conf->m_mqtt_port != MQTT_PORT)
                //{
                //    ERROR_INFO("Error: invalid mqtt_port {%d}", dev_conf->m_mqtt_port);
                //    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                //    break;
                //}
                DEBUG_INFO("MQTT port: %d", dev_conf->m_mqtt_port);

                /* MQTT User */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                strcpy(dev_conf->m_mqtt_user, pun);
                DEBUG_INFO("MQTT User: %s", dev_conf->m_mqtt_user);

                /* MQTT Pwd */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                strcpy(dev_conf->m_mqtt_pass, pun);
                DEBUG_INFO("MQTT Pass: %s", dev_conf->m_mqtt_pass);

                /* MQTT KeepAlive */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                dev_conf->m_mqtt_keepalive = atoi(pun);
                /* AWS valid keepalive time must be between 30 and 1200 */
                if ((dev_conf->m_mqtt_keepalive < MIN_MQTT_KEEPALIVE_TIME) || (dev_conf->m_mqtt_keepalive > MAX_MQTT_KEEPALIVE_TIME))
                {
                    ERROR_INFO("User defined keepalive %d not valid! (valid range 30~1200)\t[Using default: %d", dev_conf->m_mqtt_keepalive, MQTT_KEEPALIVE_DEFAULT);
                    dev_conf->m_mqtt_keepalive = MQTT_KEEPALIVE_DEFAULT;
                }
                DEBUG_INFO("MQTT Keepalive: %d", dev_conf->m_mqtt_keepalive);

                /* Powahome device type */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                dev_conf->m_type = atoi(pun);
                DEBUG_INFO("Device type: %d", dev_conf->m_type);

                /* Read in-flash value */
                uint8_t in_flash_value = Nvm_Mng_read_type();
                /* Is a valid roller? */
                bool valid_roller = (in_flash_value == 'R') && (is_roller());
                /* Is a valid light? */
                bool valid_light = (in_flash_value == 'S') && (is_switch() || is_toggle() || is_latched() || is_dimmer());

                if ((in_flash_value != 'R') && (in_flash_value != 'S'))
                {
                    ERROR_INFO("ERROR_INFO: no a valid type found in the device address! Check again the flash procedure!");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    break;
                }
                if (!valid_roller && !valid_light)
                {
                    ERROR_INFO("ERROR_INFO: in-flash value = %c | dev_conf->m_type = %d", in_flash_value, dev_conf->m_type);
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    break;
                }

                /* Powahome device id remote */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                strcpy(dev_conf->m_device_id_rem, pun);
                DEBUG_INFO("Remote Device ID: %s", dev_conf->m_device_id_rem);

                /* Security HTTPS (off/on) */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                dev_conf->m_security = atoi(pun);

                if (os_strcmp(dev_conf->m_mqtt_host, MQTT_HOST) == 0)
                {
                    dev_conf->m_security = 1;
                }
                DEBUG_INFO("Connection security: %s", dev_conf->m_security ? "true" : "false");

                /* Roller time | this could be set to a standard value in case of 0, i.e. 10 sec */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                dev_conf->m_roll_totlen = atoi(pun);
                /* if app was sending number of iteration instead of millis */
                if (dev_conf->m_config_version == 0)
                {
                    /* convert it to millis */
                    dev_conf->m_roll_totlen *= 5;
                    dev_conf->m_config_version = 1;
                }
                if (is_roller())
                {
                    if (!is_in_range(dev_conf->m_roll_totlen, ROLL_MIN_TOTLEN, ROLL_MAX_TOTLEN))
                    {
                        ERROR_INFO("dev_conf->m_roll_totlen {%d} out of range [%d, %d]! Setting it to the default value... [ROLL_MIN_TOTLEN: %d]",
                                dev_conf->m_roll_totlen, ROLL_MIN_TOTLEN, ROLL_MAX_TOTLEN, ROLL_MIN_TOTLEN);

                        dev_conf->m_roll_totlen = ROLL_MIN_TOTLEN;
                    }

                    DEBUG_INFO("Roller totlen: %d", dev_conf->m_roll_totlen);
                }
                else
                    dev_conf->m_roll_totlen = ROLL_MIN_TOTLEN;

                /* Roller Position | this could be set at 0 as default */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                dev_conf->m_roll_curr_perc = atoi(pun);
                if (is_roller())
                {
                    if (!is_in_range(dev_conf->m_roll_curr_perc, ROLL_MIN_VAL, ROLL_MAX_VAL))
                    {
                        ERROR_INFO("Error: invalid roller current position out of bound [0, 100] {%d}", dev_conf->m_roll_curr_perc);
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        break;
                    }
                    DEBUG_INFO("Roller current percentage: %d", dev_conf->m_roll_curr_perc);
                    dev_conf->m_roll_currval = dev_conf->m_roll_totlen*dev_conf->m_roll_curr_perc/100;
                }
                else
                    dev_conf->m_roll_curr_perc = 0;

                /* Number status for STEP Lights on ch1 */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                if (is_toggle() || is_latched())
                {
                    dev_conf->m_numberstatus_ch1 = atoi(pun);
                    if (!is_in_range(dev_conf->m_numberstatus_ch1, STEP_MIN_VAL, STEP_MAX_VAL))
                    {
                        ERROR_INFO("Error: max number of states out of bounds [%d, %d], {%d}", STEP_MIN_VAL, STEP_MAX_VAL, dev_conf->m_numberstatus_ch1);
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        break;
                    }
                    DEBUG_INFO("Number of status on channel 1: %d", dev_conf->m_numberstatus_ch1);
                }
                else
                    dev_conf->m_numberstatus_ch1 = STEP_MIN_VAL;

                /* Current status for STEP Lights on ch1 | this could be set at 0 as default */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                if (is_toggle() || is_latched())
                {
                    dev_conf->m_relecurrstatus_ch1 = atoi(pun);
                    if (dev_conf->m_relecurrstatus_ch1 > dev_conf->m_numberstatus_ch1)
                    {
                        ERROR_INFO("Error: current status greater than the max status {%d}", dev_conf->m_relecurrstatus_ch1);
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        break;
                    }
                    DEBUG_INFO("Current status on channel 1: %d", dev_conf->m_relecurrstatus_ch1);
                }
                else
                    dev_conf->m_relecurrstatus_ch1 = 0;

                /* Number millis to wait from ON to OFF STEP Lights on ch1 | this need to be fixed using the time (future patch) */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                if (is_toggle() || is_latched())
                {
                    dev_conf->m_reletmr_ch1 = atoi(pun);
                    if (!is_in_range(dev_conf->m_reletmr_ch1, RELAY_MIN_VAL, RELAY_MAX_VAL))
                    {
                        ERROR_INFO("Error: relay time out of bounds [50, 1000] {%d}", dev_conf->m_reletmr_ch1);
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        break;
                    }
                    DEBUG_INFO("Relay timer on channel 1: %d", dev_conf->m_reletmr_ch1);
                }
                else
                    dev_conf->m_reletmr_ch1 = RELAY_MIN_VAL;

                /* Number status for STEP Lights on ch2 */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                if (is_toggle() || is_latched())
                {
                    dev_conf->m_numberstatus_ch2 = atoi(pun);
                    if (dev_conf->m_numberstatus_ch2 == 0)
                    {
                        ERROR_INFO("Error: max number of states out of bounds [%d, %d], {%d}", STEP_MIN_VAL, STEP_MAX_VAL, dev_conf->m_numberstatus_ch2);
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        break;
                    }
                    DEBUG_INFO("Number of status on channel 2: %d", dev_conf->m_numberstatus_ch2);
                }
                else
                    dev_conf->m_numberstatus_ch2 = STEP_MIN_VAL;

                /* Current status for STEP Lights on ch2 */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                if (is_toggle() || is_latched())
                {
                    dev_conf->m_relecurrstatus_ch2 = atoi(pun);
                    if (dev_conf->m_relecurrstatus_ch2 > dev_conf->m_numberstatus_ch2)
                    {
                        ERROR_INFO("Error: current status greater than the max status {%d}", dev_conf->m_relecurrstatus_ch2);
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        break;
                    }
                    DEBUG_INFO("Current status on channel 2: %d", dev_conf->m_relecurrstatus_ch2);
                }
                else
                    dev_conf->m_relecurrstatus_ch2 = 0;

                /* Number millis to wait from ON to OFF STEP Lights on ch2 */
                if ((pun = strtok(NULL, "\r")) == NULL)
                {
                    ERROR_INFO("NULL value in config! Aborting...");
                    AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                    return;
                }
                if (is_toggle() || is_latched())
                {
                    dev_conf->m_reletmr_ch2 = atoi(pun);
                    if (!is_in_range(dev_conf->m_reletmr_ch2, RELAY_MIN_VAL, RELAY_MAX_VAL))
                    {
                        ERROR_INFO("Error: relay time out of bounds [50, 1000] {%d}", dev_conf->m_reletmr_ch2);
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        break;
                    }
                    DEBUG_INFO("Relay timer on channel 2: %d", dev_conf->m_reletmr_ch2);
                }
                else
                    dev_conf->m_reletmr_ch2 = RELAY_MIN_VAL;

                // CONFIG VERSION >= 2
                if (dev_conf->m_config_version >= 2)
                {
                    if ((pun = strtok(NULL, "\r")) == NULL)
                    {
                        ERROR_INFO("NULL value in config! Aborting...");
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        return;
                    }
                    if (is_roller())
                        dev_conf->m_roller_delay = atoi(pun);
                    else
                        dev_conf->m_roller_delay = 0;
                }
                else
                    dev_conf->m_roller_delay = 0;
                DEBUG_INFO("Roller delay: %d", dev_conf->m_roller_delay);

                if (dev_conf->m_config_version >= 4)
                {
                    if (is_latched())
                    {
                        dev_conf->m_relay[0] = FALSE;
                        dev_conf->m_relay[1] = FALSE;
                    }
                }
                if (is_latched())
                {
                    DEBUG_INFO("Relay on channel 1: %s", dev_conf->m_relay[0] ? "TRUE" : "FALSE");
                    DEBUG_INFO("Relay on channel 2: %s", dev_conf->m_relay[1] ? "TRUE" : "FALSE");
                }

                if (dev_conf->m_config_version >= 5)
                {
                    /* Rise steps */
                    if ((pun = strtok(NULL, "\r")) == NULL)
                    {
                        ERROR_INFO("NULL value in config! Aborting...");
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        return;
                    }
                    if (is_roller())
                    {
                        dev_conf->m_rise_steps = atoi(pun); // it's still 'rise_time', we will convert it into steps later on;
                        if (!is_in_range(dev_conf->m_rise_steps, ROLL_MIN_TOTLEN, ROLL_MAX_TOTLEN))
                        {
                            ERROR_INFO("Error: roller rising time out of bound [%d, %d] {%d}", ROLL_MIN_TOTLEN, ROLL_MAX_TOTLEN, dev_conf->m_rise_steps);
                            AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                            break;
                        }
                        dev_conf->m_rise_steps /= CYCLE_TIME;
                        dev_conf->m_rise_steps_curr_pos = (dev_conf->m_roll_curr_perc*dev_conf->m_rise_steps)/100;
                        DEBUG_INFO("Rise steps: %d | Rise steps curr pos: %d", dev_conf->m_rise_steps, dev_conf->m_rise_steps_curr_pos);
                    }
                    else
                    {
                        dev_conf->m_rise_steps = ROLL_MIN_TOTLEN/CYCLE_TIME;
                        dev_conf->m_rise_steps_curr_pos = 0;
                    }

                    /* Fall steps */
                    if ((pun = strtok(NULL, "\r")) == NULL)
                    {
                        ERROR_INFO("NULL value in config! Aborting...");
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        return;
                    }
                    if (is_roller())
                    {
                        dev_conf->m_fall_steps = atoi(pun); // it's still 'fall_time', we will convert it into steps later on;
                        if (!is_in_range(dev_conf->m_fall_steps, ROLL_MIN_TOTLEN, ROLL_MAX_TOTLEN))
                        {
                            ERROR_INFO("Error: roller rising time out of bound [%d, %d] {%d}", ROLL_MIN_TOTLEN, ROLL_MAX_TOTLEN, dev_conf->m_fall_steps);
                            AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                            break;
                        }
                        dev_conf->m_fall_steps /= CYCLE_TIME;
                        dev_conf->m_fall_steps_curr_pos = (dev_conf->m_roll_curr_perc*dev_conf->m_fall_steps)/100;
                        DEBUG_INFO("Fall steps: %d | Fall steps curr pos: %d", dev_conf->m_fall_steps, dev_conf->m_fall_steps_curr_pos);

                        if (dev_conf->m_rise_steps > dev_conf->m_fall_steps)
                            dev_conf->m_roll_totlen = dev_conf->m_rise_steps*CYCLE_TIME;
                        else
                            dev_conf->m_roll_totlen = dev_conf->m_fall_steps*CYCLE_TIME;
                    }
                    else
                    {
                        dev_conf->m_fall_steps = ROLL_MIN_TOTLEN/CYCLE_TIME;
                        dev_conf->m_fall_steps_curr_pos = 0;
                    }
                }

                if (dev_conf->m_config_version >= 6)
                {
                    if ((pun = strtok(NULL, "\r")) == NULL)
                    {
                        ERROR_INFO("NULL value in config! Aborting...");
                        AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                        return;
                    }
                    if (is_dimmer())
                    {
                        uint32_t dimming_time = atoi(pun);
                        if (!is_in_range(dimming_time, MIN_DIMMING_TIME, MAX_DIMMING_TIME))
                        {
                            ERROR_INFO("Error: dimming time out of bounds [%d, %d] {%d}", MIN_DIMMING_TIME, MAX_DIMMING_TIME, dimming_time);
                            AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                            break;
                        }
                        dev_conf->m_dimming_steps[0] = dimming_time/CYCLE_TIME;
                        DEBUG_INFO("Dimming steps [0]: %d", dev_conf->m_dimming_steps[0]);
                        dev_conf->m_dimming_perc[0] = 0;
                        dev_conf->m_dimmer_logic_state[0] = FALSE;

                        if ((pun = strtok(NULL, "\r")) == NULL)
                        {
                            ERROR_INFO("NULL value in config! Aborting...");
                            AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                            return;
                        }
                        dimming_time = atoi(pun);
                        if (!is_in_range(dimming_time, MIN_DIMMING_TIME, MAX_DIMMING_TIME))
                        {
                            ERROR_INFO("Error: dimming time out of bounds [%d, %d] {%d}", MIN_DIMMING_TIME, MAX_DIMMING_TIME, dimming_time);
                            AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
                            break;
                        }
                        dev_conf->m_dimming_steps[1] = dimming_time/CYCLE_TIME;
                        DEBUG_INFO("Dimming steps [1]: %d", dev_conf->m_dimming_steps[1]);
                        dev_conf->m_dimming_perc[1] = 0;
                        dev_conf->m_dimmer_logic_state[1] = FALSE;
                    }
                }

                wifi_get_macaddr(STATION_IF, mac);
                os_sprintf(dev_conf->m_device_id, MACSTR, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

                // roller config parameter initialization
//                if (is_roller())
//                {
//                    RISE = (10*dev_conf->m_rise_steps*CYCLE_TIME)/100;
//                    FALL = (10*dev_conf->m_fall_steps*CYCLE_TIME)/100;
//                    STOP = 1000;
//                    DEBUG_INFO("RISE: %d | FALL: %d | STOP: %d\n", RISE, FALL, STOP);
//                }

                dev_conf->m_need_feedback = TRUE;
                /* Start in normal mode */
                dev_conf->m_boot_mode = CONFIG_BOOTMODE_NORMAL;
                /* Set the reboot reason to "device configuration" */
                dev_conf->m_rebootreason = RR_AP_MODE;
                /* Save new config in EEPROM */
                real_NVM_save();
                /* Restart PWH */
                AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_IDLE;
                DEBUG_INFO("Starting system from AP mode into NORMAL mode");
                system_restart();
            }
            else
            {
                ERROR_INFO("Not a valid configuration string!");
                AccessPoint_Mng_Data->m_hdlrsts = ACCESSPOINT_MNG_HDLR_WAITDATA;
            }
            break;
        case ACCESSPOINT_MNG_HDLR_FEEDBACK:
        case ACCESSPOINT_MNG_HDLR_IDLE:
        default:
            break;
    }
}
