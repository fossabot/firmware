#ifndef MISC_H
#define MISC_H

#include "os_type.h"
#include "user_config.h"

#define MAX_UINT8           255
#define MAX_UINT16          65535
#define MAX_UINT32          4294967296

#define SHA256_BIN_SIZE     32
#define SHA256_STR_SIZE     SHA256_BIN_SIZE*2 + 1

/* Macros to convert everything to milliseconds */
#define MS      1
#define SECS    MS*1000
#define MINS    SECS*60
#define HOURS   MINS*60
#define SEC     SECS
#define MIN     MINS
#define HOUR    HOURS
#define MS_TO_US    1000

#define EPSILON_TIME            20   // percetage of error on a cyclic task
#define MAX_CYCLE_TASK_TIME     CYCLE_TIME*MS_TO_US + CYCLE_TIME*EPSILON_TIME*MS_TO_US/100
#define RELAY_COUNTER_HOURS_PERIOD  12   // how often to send the relay counter (hours)

#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)>(b)?(b):(a))

/* Generic timer struct */
typedef struct s_timer
{
    uint32_t    timer;
    bool        expired;
    char        name[32];
} t_timer;

typedef enum e_reboot_reason
{
    RR_POWER,
    RR_FOTA_OK,
    RR_AP_MODE,
    RR_WIFI,
    RR_FW_RESET,
    RR_FW_WATCHDOG,
    RR_HW_WATCHDOG,
    RR_FATAL_EXC,
    RR_DEEP_SLEEP,
    RR_MQTT,
    RR_REBOOT_CMD,
    RR_FOTA_FAIL,
    RR_CERT_UPDATE,
    RR_ROLLBACK,
} t_reboot_reason;

extern bool mqtt_serial_enable;
extern bool memory_deallocation_flag;
extern bool next_is_disconn_msg;
extern uint16_t disc_msg_id;
extern bool disconnected_for_reboot;

void ICACHE_FLASH_ATTR misc_init(void);
bool ICACHE_FLASH_ATTR is_switch(void);
bool ICACHE_FLASH_ATTR is_toggle(void);
bool ICACHE_FLASH_ATTR is_latched(void);
bool ICACHE_FLASH_ATTR is_roller(void);
bool ICACHE_FLASH_ATTR is_dimmer(void);
void ICACHE_FLASH_ATTR arm_timer(t_timer *);
void ICACHE_FLASH_ATTR handle_timer(t_timer *, uint8_t, uint32_t);
void ICACHE_FLASH_ATTR int_to_char(char *, int);
bool ICACHE_FLASH_ATTR is_a_number(char *);
bool ICACHE_FLASH_ATTR is_in_range(uint32_t, uint32_t, uint32_t);
bool ICACHE_FLASH_ATTR valid_config_version(char *);
char* ICACHE_FLASH_ATTR split(char *, const char *);
void ICACHE_FLASH_ATTR hex_to_string(char *, char *);
void ICACHE_FLASH_ATTR compute_sha256(uint8_t *, size_t, char *);
void ICACHE_FLASH_ATTR publish_on_topic(char *, char *);
bool ICACHE_FLASH_ATTR is_expired(t_timer *);
void ICACHE_FLASH_ATTR publish_on_serial(char *);
float ICACHE_FLASH_ATTR my_pow(float, uint8_t);
void ICACHE_FLASH_ATTR print_float(float, char *, uint8_t);
int ICACHE_FLASH_ATTR my_round(float);

/* Functions to check the system's status one minute after the bootup */
void ICACHE_FLASH_ATTR system_status_check_arm_timer(void);         // arm the timer that triggers the "system_status_check_function" one minute after the bootup
void ICACHE_FLASH_ATTR system_status_check_disarm_timer(void);      // disarm the timer that triggers the "system_status_check_function" one minute after the bootup
void ICACHE_FLASH_ATTR system_status_check_function(void);          // called when the "system_status_check_timer" expires
void ICACHE_FLASH_ATTR system_rollback(void);                       // function to call to perform a rollback to the previous version of firmware on the device

/* Miscellanous function to get the number of microseconds (us) since the device bootup */
uint64_t ICACHE_FLASH_ATTR system_get_us_time(void);

/* Custom os_free which also sets to NULL the freed pointer */
void ICACHE_FLASH_ATTR powa_free(void **, char *);

void ICACHE_FLASH_ATTR get_last_reset_cause(void);

char *ICACHE_FLASH_ATTR get_business_topic(void);
void ICACHE_FLASH_ATTR lower_string(char *);
bool ICACHE_FLASH_ATTR is_business(void);
void ICACHE_FLASH_ATTR parse_cert_from_string(char *, char *);

#endif
