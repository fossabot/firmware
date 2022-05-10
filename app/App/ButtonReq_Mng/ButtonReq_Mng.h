/*
 * FileName:    ButtonReq_Mng.h
 * Brief:       Physical request manager header file
 */

/* ----------------------- Includes ----------------------- */
#include "Misc.h"

/* ----------------------- Defines ------------------------ */
#define BUTTON_SAVE_TIMER       2500    /* millis to pass to save state after button press */
#define STOP_ITER_LOAD_CONFIG   3       /* number of magic_sequence iteration before stop feeding the load */
#define SCALE                   100
#define MS_TO_STEPS             0.2
#define MIN_STEPS_TO_DO         200     /* minimum number of steps to do; the equivalent time (in milliseconds) is MIN_STEPS_TO_DO*5 */
#define PERC_DEADZONE           2

#define DIMMER_MIN_PERCENTAGE   0
#define DIMMER_MAX_PERCENTAGE   100

/* ---------------------- Data Types ---------------------- */
typedef enum e_dimmer_state
{
    DIMMER_INIT = 0,
    DIMMER_COUNTING,
    DIMMER_SHORTPRESS,
    DIMMER_LONGPRESS,
    DIMMER_MQTT_OFF,
    DIMMER_MQTT_LONGPRESS,
    DIMMER_MQTT_DIRECTION_INVERSION,
    DIMMER_MQTT_RESTING,
    DIMMER_MQTT_CHARGING_UP,
    DIMMER_MQTT_PRE_SHORTPRESS,
    DIMMER_MQTT_SHORTPRESS,
    DIMMER_RESET,
    DIMMER_IDLE
} t_dimmer_state;

extern uint16_t tot_step_done;
extern uint16_t roller_steps_to_do;
extern uint16_t roller_steps_done;
extern uint16_t roll_delta_perc;
extern uint8_t roll_curr_perc;
extern uint16_t roll_final_perc;
extern uint16_t steps_to_publish;
extern uint16_t rise_steps_to_reach;
extern uint16_t fall_steps_to_reach;
extern float rise_fall_ratio;

extern t_dimmer_state dimmer_curr_state[2];
extern bool dimmer_action_requested[2];
extern uint8_t dimmer_final_perc[2];
extern uint8_t dimmer_curr_perc[2];
extern uint8_t dimmer_delta_perc[2];
extern uint16_t dimmer_steps_to_do[2];
extern uint16_t dimmer_steps_done[2];
extern uint16_t dimmer_steps_to_publish[2];
extern uint32_t dimmer_tot_steps_done[2];
extern uint8_t dimmer_mqtt_final_perc[2];
extern bool dimmer_direction[2];    // HIGH = need to increase; LOW = need to decrease
extern t_timer dimmer_pression_timer[2];
extern t_timer dimmer_mqtt_inversion_timer[2];
extern t_timer dimmer_mqtt_shortpress_timer[2];

extern t_timer turning_timer;
extern t_timer light_turning_timer[2];
extern bool going_down;
extern bool going_up;

/* -------------- Global function prototypes -------------- */

void ICACHE_FLASH_ATTR ButtonReq_Mng_Init(void);
void ICACHE_FLASH_ATTR ButtonReq_Mng_Hdlr(void);
bool ICACHE_FLASH_ATTR ButtonReq_Mng_GetRollActReq(void);
void ICACHE_FLASH_ATTR arm_roller_timer(void);
void ICACHE_FLASH_ATTR handle_roller_timer(void);
void ICACHE_FLASH_ATTR stop_or_damp(void);
uint16_t ICACHE_FLASH_ATTR roller_compute_steps_to_publish(bool);
