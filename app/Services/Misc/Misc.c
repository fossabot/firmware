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
#include "../Services/.third_party/include/mbedtls/x509_crt.h"

bool mqtt_serial_enable = FALSE;

t_Nvm_Mng_Data *dev_conf;
static ETSTimer system_status_check_timer;
uint8_t roll_curr_perc;
bool device_connected;
t_NetworkReq_Mng_Data *NetworkReq_Mng_Data;

bool to_be_disconnected;
bool to_be_restarted;
bool restartable;
bool stopped;
bool is_5ms_disconnection_timer_armed = FALSE;
char base_topic[16];

static ETSTimer disc_5ms_roller_check_stopped;

/* Use this function to initialize miscellanous variables that could have been introduced with a FOTA upgrade */
void ICACHE_FLASH_ATTR misc_init(void)
{
    dev_conf = Nvm_Mng_GetNvm();

    if ((dev_conf->m_inhibit_max_time == 0) || (dev_conf->m_inhibit_max_time == -1) || (dev_conf->m_inhibit_max_time == MAX_UINT16))
        dev_conf->m_inhibit_max_time = 5;
    if ((dev_conf->m_rise_steps_curr_pos == 0) || (dev_conf->m_rise_steps_curr_pos == -1) || (dev_conf->m_rise_steps_curr_pos == MAX_UINT16))
    {
        dev_conf->m_rise_steps_curr_pos = dev_conf->m_roll_curr_perc*dev_conf->m_rise_steps/100;
        DEBUG_INFO("misc_init | dev_conf->m_rise_steps_curr_pos: %d", dev_conf->m_rise_steps_curr_pos);
    }
    if ((dev_conf->m_fall_steps_curr_pos == 0) || (dev_conf->m_fall_steps_curr_pos == -1) || (dev_conf->m_fall_steps_curr_pos == MAX_UINT16))
    {
        dev_conf->m_fall_steps_curr_pos = dev_conf->m_roll_curr_perc*dev_conf->m_fall_steps/100;
        DEBUG_INFO("misc_init | dev_conf->m_fall_steps_curr_pos: %d", dev_conf->m_fall_steps_curr_pos);
    }

    // Initializes 'm_fota_status' inside the struct if not set yet
    if (!is_in_range(dev_conf->m_fota_status, FOTA_STATUS_OK, FOTA_STATUS_ROLLBACK))
        dev_conf->m_fota_status = FOTA_STATUS_PENDING;

    // Initializes 'm_rollback_counter' inside the struct if not set yet
    if (!is_in_range(dev_conf->m_rollback_counter, 0, MAX_ROLLBACK_COUNTER))
        dev_conf->m_rollback_counter = 0;

    roll_curr_perc = dev_conf->m_roll_curr_perc;
}

bool ICACHE_FLASH_ATTR is_switch(void)
{
   return ((dev_conf->m_type == CONFIG_TYPE_NORMAL_LIGHT) || (dev_conf->m_type == CONFIG_TYPE_REMOTE_LIGHT));
}

bool ICACHE_FLASH_ATTR is_toggle(void)
{
   return ((dev_conf->m_type == CONFIG_TYPE_NORMAL_STEP_LIGHT) || (dev_conf->m_type == CONFIG_TYPE_REMOTE_STEP_LIGHT));
}

bool ICACHE_FLASH_ATTR is_latched(void)
{
   return ((dev_conf->m_type == CONFIG_TYPE_NORMAL_LATCHED) || (dev_conf->m_type == CONFIG_TYPE_REMOTE_LATCHED));
}

bool ICACHE_FLASH_ATTR is_roller(void)
{
   return ((dev_conf->m_type == CONFIG_TYPE_NORMAL_ROLLER) || (dev_conf->m_type == CONFIG_TYPE_REMOTE_ROLLER));
}

bool ICACHE_FLASH_ATTR is_dimmer(void)
{
   return ((dev_conf->m_type == CONFIG_TYPE_NORMAL_DIMMER) || (dev_conf->m_type == CONFIG_TYPE_REMOTE_DIMMER));
}

void ICACHE_FLASH_ATTR arm_timer(t_timer *timer)
{
    timer->timer = 0;
    timer->expired = FALSE;
    DEBUG_INFO("\t\t\t\t\t\tARM_TIMER: %s", timer->name);
}

void ICACHE_FLASH_ATTR handle_timer(t_timer *timer, uint8_t timer_step, uint32_t timer_max_value)
{
    if (timer->expired == TRUE)
        return;

    if (timer->timer >= timer_max_value)
    {
        timer->timer = timer_max_value;
        timer->expired = TRUE;
        DEBUG_INFO("\t\t\t\t\t\tTIMER %s EXPIRED", timer->name);
        return;
    }

    timer->timer += timer_step;
}

/* Convert from 0 [num] to '0' [char] */
void ICACHE_FLASH_ATTR int_to_char(char *dest, int source)
{
    os_sprintf(dest, "%d", source);
}

bool ICACHE_FLASH_ATTR is_a_number(char *data)
{
    if (os_strcmp(data, "0") == 0)
        return true;

    return (atoi(data) != 0);
}

bool ICACHE_FLASH_ATTR is_in_range(uint32_t value, uint32_t min_val, uint32_t max_val)
{
    bool flag = ((min_val <= value) && (value <= max_val));
    return flag;
}

bool ICACHE_FLASH_ATTR valid_config_version(char *string)
{
    bool ret_val = FALSE;
    int i;

    for (i = MIN_CONFIG_VAL; i <= MAX_CONFIG_VAL; i++)
    {
        if (atoi(string) == i)
        {
            DEBUG_INFO("Matching config_version: %d", atoi(string));
            ret_val = TRUE;
            break;
        }
        else
            DEBUG_INFO("Not a matching config_version: %d", atoi(string));

    }

    return ret_val;
}

char ICACHE_FLASH_ATTR *split(char *str, const char *delim)
{
    char *p = os_strstr(str, delim);

    if (p == NULL)
        return NULL;

    *p = '\0';
    return (p + os_strlen(delim));
}

void ICACHE_FLASH_ATTR hex_to_string(char *input, char *output)
{
    int i;

    for (i = 0; i < SHA256_BIN_SIZE; i++)
        os_sprintf(output + 2*i, "%02x", input[i]);
}

void ICACHE_FLASH_ATTR compute_sha256(uint8_t *to_hash, size_t len, char *out_string)
{
    char output[SHA256_BIN_SIZE];

    mbedtls_sha256(to_hash, len, output, 0);
    hex_to_string(output, out_string);
}

void ICACHE_FLASH_ATTR publish_on_topic(char *topic, char *msg)
{
    if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_CONFIG)
        return;

    char full_topic[64];

    /* Need to build the base of the topic URI */
    os_strcpy(full_topic, base_topic);
    os_strcat(full_topic, dev_conf->m_device_id);
    os_strcat(full_topic, "/");
    os_strcat(full_topic, topic);

    DEBUG_INFO("Publishing %s on %s", msg, full_topic);
    Mqtt_Mng_Publish(full_topic, msg, os_strlen(msg), 1, 0);
}

bool ICACHE_FLASH_ATTR is_expired(t_timer *timer)
{
    return timer->expired;
}

void ICACHE_FLASH_ATTR publish_on_serial(char *msg)
{
    if (dev_conf->m_boot_mode == CONFIG_BOOTMODE_CONFIG)
        return;

    if (!mqtt_serial_enable)
        return;

    char full_topic[64];

    /* Need to build the base of the topic URI */
    os_strcpy(full_topic, base_topic);
    os_strcat(full_topic, dev_conf->m_device_id);
    os_strcat(full_topic, "/diag/serial");

    DEBUG_INFO("Publishing %s on %s", msg, full_topic);
    Mqtt_Mng_Publish(full_topic, msg, os_strlen(msg), 1, 0);
}

int ICACHE_FLASH_ATTR my_round(float f_val)
{
    int i_val = (int) f_val;
    uint16_t decimal = (f_val - i_val)*10;

    if (f_val > 0)
    {
        if (decimal >= 5)
            i_val++;
    }
    else if (f_val < 0)
    {
        if (decimal >= 5)
            i_val--;
    }

    return i_val;
}

float ICACHE_FLASH_ATTR my_pow(float base, uint8_t esp)
{
    float power = 1;

    while (esp--)
        power *= base;

    return power;
}

void ICACHE_FLASH_ATTR print_float(float f_val, char *buff, uint8_t precision)
{
    int i_val = (int) f_val;
    uint16_t decimal = (f_val - i_val)*my_pow(10, precision);
    os_sprintf(buff, "%d.%u", i_val, decimal);
}

void ICACHE_FLASH_ATTR disarm_timer_for_roller_stop_disconnection(void)
{
    DEBUG_INFO("disarm_timer_for_roller_stop_disconnection");
    os_timer_disarm(&disc_5ms_roller_check_stopped);
}

void ICACHE_FLASH_ATTR check_if_roller_is_stopped_for_disconnection(void)
{
    if (stopped)
    {
        disarm_timer_for_roller_stop_disconnection();
        dev_conf->m_backoff = min(dev_conf->m_backoff*2, MAX_BACKOFF_TIMER);
        dev_conf->m_rebootreason = RR_MQTT;
        real_NVM_save();
        system_restart();
        return;
    }
}

void ICACHE_FLASH_ATTR arm_timer_for_roller_stop_disconnection(void)
{
    DEBUG_INFO("arm_timer_for_roller_stop_disconnection");
    is_5ms_disconnection_timer_armed = TRUE;
    os_timer_setfn(&disc_5ms_roller_check_stopped, (os_timer_func_t *)check_if_roller_is_stopped_for_disconnection, NULL);
    os_timer_arm(&disc_5ms_roller_check_stopped, CYCLE_TIME, TRUE);
}

void ICACHE_FLASH_ATTR system_status_check_arm_timer(void)
{
    DEBUG_INFO("system_status_check_arm_timer");
    os_printf("system_status_check_arm_timer | Current backoff: %d seconds\n", dev_conf->m_backoff/1000);
    os_timer_setfn(&system_status_check_timer, (os_timer_func_t *)system_status_check_function, NULL);
    // Arm the timer as repeating timer
    os_timer_arm(&system_status_check_timer, dev_conf->m_backoff, FALSE);
}

void ICACHE_FLASH_ATTR system_status_check_disarm_timer(void)
{
    DEBUG_INFO("system_status_check_disarm_timer");
    os_timer_disarm(&system_status_check_timer);
}

void ICACHE_FLASH_ATTR system_status_check_function(void)
{
    DEBUG_INFO("");
    DEBUG_INFO("\tsystem_status_check_function");
    DEBUG_INFO("");

    os_printf("system_status_check_function | next check will be in %d seconds\n", dev_conf->m_backoff/1000);
    // If the device is connected
    if (device_connected == TRUE)
    {
        system_status_check_disarm_timer();
        system_status_check_arm_timer();
        os_printf("system_status_check_function | everything is ok\n");
        return;
    }

    // If the device is not connected and it comes from a FOTA
    if (dev_conf->m_fota_status == FOTA_STATUS_PENDING)
    {
        // Increases the rollback counter
        dev_conf->m_rollback_counter++;
    }

    #ifdef GLOBAL_DEBUG_ON
    ERROR_INFO(" ");
    ERROR_INFO("system_status_check_function | dev_conf->m_fota_status: %s", fota_status_string[dev_conf->m_fota_status]);
    ERROR_INFO("system_status_check_function | dev_conf->m_rollback_counter: %d/%d", dev_conf->m_rollback_counter, MAX_ROLLBACK_COUNTER);
    ERROR_INFO(" ");
    #endif

    // If reached the max rollback counter
    if (dev_conf->m_rollback_counter >= MAX_ROLLBACK_COUNTER)
    {
        // Rollbacks the system to the previous app after sum from i=0 to (MAX_ROLLBACK_COUNTER -1) of 2^i minutes attempts (eg. MAX_ROLLBACK_COUNTER = 3 => 7 minutes)
        update_fota_status(FOTA_STATUS_ROLLBACK);
        system_rollback();
    }
    // Otherwise
    else
    {
        if (is_roller())
        {
            if (is_5ms_disconnection_timer_armed == FALSE)
                arm_timer_for_roller_stop_disconnection();
        }
        else
        {
            // Increases the backoff and restart the system
            dev_conf->m_backoff = min(dev_conf->m_backoff*2, MAX_BACKOFF_TIMER);
            dev_conf->m_rebootreason = RR_MQTT;
            dev_conf->m_pam_mode = PAM_ENABLE;  // Maybe enable the PAM so that, in case of a misconfiguration of the MQTT broker, the device can be reconfigured
            real_NVM_save();
            system_restart();
        }
    }
}

void ICACHE_FLASH_ATTR system_rollback(void)
{
    system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
    uint32_t curr_userbin_address = system_get_userbin_addr();
    DEBUG_INFO("userbin_swapping | curr_userbin_address: 0x%2X", curr_userbin_address);
    DEBUG_INFO("userbin_swapping | system_upgrade_reboot...");
    system_upgrade_reboot();
}

uint64_t ICACHE_FLASH_ATTR system_get_us_time(void)
{
    static uint32_t system_prev_us_time = 0;
    uint32_t system_curr_us_time = system_get_time();
    static uint64_t system_us_time = 0;

    system_curr_us_time = system_get_time();

    if (system_curr_us_time < system_prev_us_time)
        system_us_time += (MAX_UINT32 - system_prev_us_time + system_curr_us_time);
    else
        system_us_time += (system_curr_us_time - system_prev_us_time);

    system_prev_us_time = system_curr_us_time;
    return system_us_time;
}

void ICACHE_FLASH_ATTR powa_free(void **p, char *p_name)
{
    DEBUG_INFO("powa_free | freeing \"%s\"", p_name);
    DEBUG_INFO("\tBEFORE: %p", *p);
    os_free(*p);
    *p = NULL;
    DEBUG_INFO("\tAFTER: %p", *p);
}

void ICACHE_FLASH_ATTR custom_restart(bool deallocation_flag, bool disconnection_flag, char *mqtt_msg)
{
    DEBUG_INFO("custom_restart");
    disconnected_device_procedure(deallocation_flag);
    to_be_disconnected = disconnection_flag;
    restartable = true;
}

/* For more information read the API Reference about the "system_get_rst_info" function */
void ICACHE_FLASH_ATTR get_last_reset_cause(void)
{
    struct rst_info *rst_info = system_get_rst_info();

    switch (rst_info->reason)
    {
        case 0: // Normal power on
        case 6: // External system reset
            dev_conf->m_rebootreason = RR_POWER;
            GPIOMng_Init(FALSE);
            break;
        case 1: // Hardware watchdog reset
            dev_conf->m_rebootreason = RR_HW_WATCHDOG;
            GPIOMng_Init(FALSE);
            break;
        case 2: // Exception reset
            dev_conf->m_rebootreason = RR_FATAL_EXC;
            GPIOMng_Init(TRUE);
            break;
        case 3: // Software watchdog reset
            dev_conf->m_rebootreason = RR_FW_WATCHDOG;
            GPIOMng_Init(TRUE);
            break;
        case 4: // Software reset
            GPIOMng_Init(TRUE);
            break;
        case 5: // Wake up from deep-sleep
            dev_conf->m_rebootreason = RR_DEEP_SLEEP;
            GPIOMng_Init(FALSE);
            break;
        default:
            ERROR_INFO("get_last_reset_cause | unknown reset cause [%d]", rst_info->reason);
            break;
    }

    DEBUG_INFO("");
    DEBUG_INFO("get_last_reset_cause | dev_conf->m_rebootreason: %d", dev_conf->m_rebootreason);
    DEBUG_INFO("");
}

char ICACHE_FLASH_ATTR *get_business_topic(void)
{
    DEBUG_INFO("get_business_topic | BEFORE: %d", system_get_free_heap_size());
    mbedtls_x509_crt my_cert;
    mbedtls_x509_crt_init(&my_cert);

    int max_size = 2048;
    char *string = os_zalloc(sizeof(char)*max_size);
    char *cert = os_zalloc(sizeof(char)*max_size);
    bool ret_val = read_from_flash(0xF6000, (uint8_t *)string, max_size);

    if (!ret_val)
        ERROR_INFO("get_string_info | read_from_flash");

    parse_cert_from_string(string+32, cert);
    size_t cert_len = os_strlen(cert);
    //DEBUG_INFO("cert: %s | len: %d", cert, cert_len);
    char *sub_name = os_zalloc(sizeof(char)*16);

    int err = mbedtls_x509_crt_parse(&my_cert, cert, cert_len + 1);
    if (err)
    {
        ERROR_INFO("mbedtls_x509_crt_parse | err: %d [-0x%4X]", err, -err);

        os_free(cert);
        mbedtls_x509_crt_free(&my_cert);

        // Returns the default one
        os_sprintf(sub_name, "%s", "Powahome");
        return sub_name;
    }

    err = mbedtls_x509_crt_info(cert, 1024, "", &my_cert);
    if (err < 0)
    {
        ERROR_INFO("mbedtls_x509_crt_info | err: %d", err);

        os_free(cert);
        mbedtls_x509_crt_free(&my_cert);

        // Returns the default one
        os_sprintf(sub_name, "%s", "Powahome");
        return sub_name;
    }

    char *token = split(cert, "subject name");
    sub_name = split(token, "O=");
    token = split(sub_name, ",");

    os_free(string);
    os_free(cert);
    mbedtls_x509_crt_free(&my_cert);
    DEBUG_INFO("get_business_topic | AFTER: %d", system_get_free_heap_size());
    return sub_name;
}

void ICACHE_FLASH_ATTR lower_string(char *string)
{
    size_t str_len = os_strlen(string);
    size_t i;

    for (i = 0; i < str_len; i++)
        string[i] = (char) tolower((unsigned char) string[i]);
}

bool ICACHE_FLASH_ATTR is_business(void)
{
    return (os_strncmp(base_topic, "/powa/", 6));
}

void ICACHE_FLASH_ATTR parse_cert_from_string(char *string, char *cert)
{
    DEBUG_INFO("parse_cert_from_string");
    char *token = split(string, "CERTIFICATE-----");
    split(token, "-----END");
    os_sprintf(cert, "-----BEGIN CERTIFICATE-----%s-----END CERTIFICATE-----", token);
}
