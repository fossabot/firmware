/* Nvm_Mng.h */

#ifndef USER_CONFIG_H_
#define USER_CONFIG_H_

/* ----------------------- Includes ----------------------- */

#include "os_type.h"
#include "user_config.h"
#include "Fota_Mng.h"
#include "Misc.h"

/* ----------------------- Defines ------------------------ */

#define REMOTE_DEF "POWA_d85881\0"  /* for test */
#define GPIO_MNG_PIN_MAX_NUM    4
#define MAX_TURNING_TIMER 1000
#define MAX_LIGHT_TURNING_TIMER 300
#define MAX_NVM_SAVE_TIME   2000

/* ---------------------- Data Types ---------------------- */

/* Switch type */
typedef enum e_ConfigType
{
    CONFIG_TYPE_NORMAL_LIGHT = 0,   // 0
    CONFIG_TYPE_REMOTE_LIGHT,       // 1
    CONFIG_TYPE_NORMAL_ROLLER,      // 2
    CONFIG_TYPE_REMOTE_ROLLER,      // 3
    CONFIG_TYPE_NORMAL_STEP_LIGHT,  // 4
    CONFIG_TYPE_REMOTE_STEP_LIGHT,  // 5
    CONFIG_TYPE_NORMAL_LATCHED,     // 6
    CONFIG_TYPE_REMOTE_LATCHED,     // 7
    CONFIG_TYPE_NORMAL_DIMMER,      // 8
    CONFIG_TYPE_REMOTE_DIMMER       // 9
}t_ConfigType;

/* Boot mode */
typedef enum e_ConfigBootMode
{
    CONFIG_BOOTMODE_NORMAL = 0,
    CONFIG_BOOTMODE_CONFIG
}t_ConfigBootMode;

/* Nvm Data manager data */
typedef struct s_Nvm_Mng_Data
{
    uint32              m_cfg_holder;           /* config holder, to be changed for resetting at default */
    uint8               m_device_id[32];        /* device id of the Powa device */
    uint8               m_sta_ssid[64];         /* SSID of the WiFi connection */
    uint8               m_sta_pwd[64];          /* Password of the WiFi connection */
    uint32              m_sta_type;             /* WiFi connection type */
    uint8               m_mqtt_host[64];        /* Host name of the MQTT Broker */
    uint32              m_mqtt_port;            /* Port of the MQTT Broker */
    uint8               m_mqtt_user[32];        /* User name of the MQTT Broker */
    uint8               m_mqtt_pass[32];        /* Password of the MQTT Broker */
    uint32              m_mqtt_keepalive;       /* keepalive.. */
    t_ConfigBootMode    m_boot_mode;            /* Boot mode (normal or config mode) */
    t_ConfigType        m_type;                 /* Normal / remote / powaroller */
    uint16              m_roll_totlen;          /* total length of the roller */
    uint16              m_roll_currval;         /* current value of the roller */
    uint8               m_pam_mode;             /* Pam mode */
    uint8               m_device_id_rem[32];    /* device id of the remote switch */
    uint8               m_security;             /* flag indicating if security SSL is active */
    uint8               m_statuspin[4];         /* Array of pin stauts, must be dimensioned with define GPIO_MNG_PIN_MAX_NUM*/
    uint8               m_rebooted;             /* Describe if the reconnection is for reboot [0=YES/1=NO] */
    uint8               m_rebootreason;         /* Describe reason of last reboot [0=HARD / 1=FOTA / 2=AP_CONFIGURATION / 3=FW_RESTART] */
    uint8               m_relecurrstatus_ch1;   /* Current status for STEP LIGHTS on ch1 */
    uint8               m_numberstatus_ch1;     /* Possible status for rele on ch 1 */
    uint16              m_reletmr_ch1;          /* Time from ON to OFF */
    uint8               m_relecurrstatus_ch2;   /* Current status for STEP LIGHTS on ch2 */
    uint8               m_numberstatus_ch2;     /* Possible status for rele on ch 2 */
    uint16              m_reletmr_ch2;          /* Time from ON to OFF  on ch2 */
    uint16              m_debounce_timer;       /* Debounce timer for the GPIO Handler */
    uint16              m_config_version;       /* To track the config version sent through TCP packet */
    uint32              m_relay_counter[2];     /* To track the number of relay commutation */
    uint16              m_roller_delay;         /* Time to wait before updating roller position */
    bool                m_relay[2];             /* If FALSE use the toggle as a switch */
    uint32              m_backoff;              /* Exponential-backoff-like timer to reboot in case of network issues */
    bool                m_need_feedback;        
    uint32              m_mqtt_loop_counter;    /* Counter for the MQTT connecting loop */
    uint16              m_rise_steps;           /* Time to open the roller      [ms] */
    uint16              m_fall_steps;           /* Time to close the roller     [ms] */
    uint16              m_roll_curr_perc;       /* Roller current percentage    [%] | could also be uint8 */
    uint16              m_inhibit_max_time;     /* To prevent action when fast-poweroff */
    uint16              m_dimming_steps[2];     /* Steps to bring the dimmer from 0 to 100% */
    uint8               m_dimming_perc[2];      /* Current dimmer percentage */
    bool                m_dimmer_logic_state[2];/* Current dimmer logic state */
    uint16              m_rise_steps_curr_pos;
    uint16              m_fall_steps_curr_pos;
    t_fota_status       m_fota_status;          /* status of the FOTA */
    uint8_t             m_rollback_counter;     /* counter to perform the firmware rollback */

} t_Nvm_Mng_Data;

/* Nvm data saving flag */
typedef struct {
    uint8 flag;
    uint8 pad[3];
} SAVE_FLAG;

/* -------------- Global function prototypes -------------- */
void            ICACHE_FLASH_ATTR Nvm_Mng_Init(void);
void            ICACHE_FLASH_ATTR Nvm_Mng_Save(void);
bool            ICACHE_FLASH_ATTR Nvm_Mng_Load(void);
t_Nvm_Mng_Data* ICACHE_FLASH_ATTR Nvm_Mng_GetNvm(void);
uint8_t         ICACHE_FLASH_ATTR Nvm_Mng_read_type(void);
bool            ICACHE_FLASH_ATTR read_from_flash(uint32_t, uint8_t *, uint32_t);
bool            ICACHE_FLASH_ATTR write_to_flash(uint8_t *, uint32_t, uint32_t);
void            check_for_NVM_save(void);
void            real_NVM_save(void);

/* ------------- Global variable declaration -------------- */
extern t_Nvm_Mng_Data Nvm_Mng_Data;
extern uint16_t random_id_number[3];
extern bool going_down;
extern bool pressed;
extern uint16_t gpio_debounce_timer;

#endif /* USER_CONFIG_H_ */
