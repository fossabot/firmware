/*                       
 * FileName:    Mqtt_Mng.h
 * Brief:       Manager interface layer for the Mqtt library header
 */ 

/* ----------------------- Includes ----------------------- */

/* ----------------------- Defines ------------------------ */
#define SUB_ACK_MAX_RECONNECT_TIMER 30000
#define SUB_ACK_MIN_RECONNECT_TIMER 1000

#define MQTT_QUEUE_MAX_SIZE 3

/* ---------------------- Data Types ---------------------- */

typedef struct s_Mqtt_Mng_Data_Msg
{
    uint8       m_msgtopic[256];    /* Received message topic  | AWS limits it to 256 bytes */
    uint8       m_msgtopiclen;      /* Length of message topic | AWS limits it to 256 bytes */
    uint8       m_msgdata[1024];    /* Received message data   | AWS limits it to 16000 bytes but we're not implementing because it's very unlikely that a HUGE message will arrive */
    uint16      m_msgdatalen;       /* Length of message data  | AWS limits it to 16000 bytes */
} t_Mqtt_Mng_Data_Msg;

typedef struct s_Mqtt_Mng_Data
{
    MQTT_Client             m_mqttclient;   /* MQTT_Client data object */
    uint8                   m_isdatarec;    /* Flag indicating if data has been received */
} t_Mqtt_Mng_Data;

typedef struct s_Mqtt_Queue
{
    uint8_t write_index;                            // where to write
    uint8_t read_index;                             // where to read
    uint8_t queue_elems;                            // n of elements in the queue
    t_Mqtt_Mng_Data_Msg Queue[MQTT_QUEUE_MAX_SIZE]; // data 
} t_Mqtt_Queue;

extern t_Mqtt_Mng_Data Mqtt_Mng_Data;
extern bool resubscribe_required;
extern t_Mqtt_Queue *Mqtt_Queue;
extern char base_topic[16];

/* -------------- Global function prototypes -------------- */

void                ICACHE_FLASH_ATTR   mqtt_queue_init(void);
bool                ICACHE_FLASH_ATTR   mqtt_queue_is_empty(void);
t_Mqtt_Mng_Data_Msg ICACHE_FLASH_ATTR  *mqtt_queue_get_msg(void);
void                ICACHE_FLASH_ATTR   mqtt_queue_add(uint32_t*, const char *, uint32_t, const char *, uint32_t);
void                ICACHE_FLASH_ATTR   Mqtt_Mng_Init(void);
uint8               ICACHE_FLASH_ATTR   Mqtt_Mng_IsDataReceived(void);
void                ICACHE_FLASH_ATTR   Mqtt_Mng_SetDataAsRead(void);
t_Mqtt_Mng_Data_Msg ICACHE_FLASH_ATTR  *Mqtt_Mng_GetLastMsg(void);
MQTT_Client         ICACHE_FLASH_ATTR  *Mqtt_Mng_GetMqttClient(void);
void                ICACHE_FLASH_ATTR   Mqtt_Mng_Connect(void);
void                ICACHE_FLASH_ATTR   Mqtt_Mng_Disconnect(void);
bool                ICACHE_FLASH_ATTR   Mqtt_Mng_Publish(char* topic, char *data, uint16 datalen, uint8 qos, uint8 retain);
void                ICACHE_FLASH_ATTR   device_disconnected_procedure(bool);
