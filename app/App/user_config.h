#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

#define USE_OPTIMIZE_PRINTF

#define CYCLE_TIME          5*MS
#define ROLLER_PUB_PERC     5  // roller publishes every ROLLER_PUB_PERC perc

/* dimmer-specific timers */
#define DIMMER_PUB_PERC             10      // dimmer publishes every DIMMER_PUB_PERC perc
#define DIMMER_MIN_SHORTPRESS_TIME  100     // to detect dimmer shortpress start
#define DIMMER_MAX_SHORTPRESS_TIME  1000    // to detect dimmer shortpress end
#define DIMMER_MIN_LONGPRESS_TIME   1000    // to detect dimmer longpress start
#define DIMMER_MAX_LONGPRESS_TIME   10000   // to detect dimmer longpress end
#define DIMMER_RESET_TIME           11000   // to detect dimmer reset
#define DIMMER_REST_TIME            500

#define GPIO_BANG_PIN       0
#define BUTTON_BANG_PIN     2
#define NETWORK_BANG_PIN    14
#define CYCLE_BANG_PIN      15

#define YELLOW_GPIO         CYCLE_BANG_PIN
#define CYAN_GPIO           GPIO_BANG_PIN
#define MAGENTA_GPIO        BUTTON_BANG_PIN
#define BLUE_GPIO           NETWORK_BANG_PIN

/* Fast fix - to be improved */
typedef struct e_RollerBlind
{
    uint16      m_rollcurrval;          /* current roller position */
    uint16      m_rollfinalval;         /* final roller position */
    bool        m_rollactreq;           /* is an action required? */
    bool        no_action;              /* True if to be stopped */
    uint16_t    no_action_timer;        /* Current time for no action */
}t_RollerBlind;
extern t_RollerBlind RollerBlind;

/* Device types */
extern const char* device_type[10];

/* Link to the update binary */
extern char s3_aws_url[128];

/* Flags for status update messages */
extern bool to_be_disconnected;
extern bool restartable;
extern bool to_be_restarted; // if true, device can be restart; nope otherwise
extern bool fota_restartable;   // if true, device can be restarted after FOTA
extern bool to_be_fota_restarted;

/* Backoff timer parameters (minutes) */
#define MINUTES_TO_MS       60000 //(60 seconds * 1000 milliseconds)
#define MIN_BACKOFF_TIMER   1*MINUTES_TO_MS
#define MAX_BACKOFF_TIMER   4*MINUTES_TO_MS
#define DHCP_MAX_RETRY      3

/* Defines the size of the banner at BOOT and at FOTA */
#define POWA_BANNER_FLAG 0   // 0 = big; 1 = small

/* Defines for firmware version */
#define FW_VERS_MAJOR   8
#define FW_VERS_MINOR   8
#define FW_VERS_BUGFIX  8

/* Defines for enabling/disabling the fota upgrade */
#define PF_ON   1
#define PF_OFF  0
#define POWA_FOTA PF_ON

/* Defines for enabling/disabling the AP mode for configuration */
#define PAM_ON  1
#define PAM_OFF 0
#define POWA_APCONFIG_MODE PAM_ON

/* Defines for hardware version */
#define HW_VERS_1   0
#define HW_VERS_2   1
#define HW_VERS_MAX 2
#define POWA_HW_VERS HW_VERS_2

/* Defines for switches and loads pinout */
#define SW1 0
#define SW2 1
#define L2  2
#define L1  3

#define HIGH TRUE
#define LOW FALSE

#define ROLL_MIN_TOTLEN 5   // could be set to CYCLE_TIME
#define ROLL_MAX_TOTLEN 60000

/* min and MAX percentage for roller */
#define ROLL_MIN_VAL 0
#define ROLL_MAX_VAL 100

/* min and MAX relay feeding time (ms) */
#define RELAY_MIN_VAL 50
#define RELAY_MAX_VAL 1000

/* min and MAX values for max_num_status */
#define STEP_MIN_VAL 2
#define STEP_MAX_VAL 4

/* min and MAX values for config_version */
#define MIN_CONFIG_VAL 1
#define MAX_CONFIG_VAL 6

/* min and MAX values for the dimming time (ms) */
#define MIN_DIMMING_TIME 50
#define MAX_DIMMING_TIME 10000

/* min and MAX MQTT keepalive time */
#define MIN_MQTT_KEEPALIVE_TIME 30
#define MAX_MQTT_KEEPALIVE_TIME 1200

extern int8_t times;
extern bool network_feedback;
extern bool device_connected;

#if (POWA_HW_VERS == HW_VERS_1)
  #define CFG_LOCATION  0x79    /* Please don't change or if you know what you doing */
#else
  #define CFG_LOCATION  0xF9    /* Please don't change or if you know what you doing */
#endif

#endif

