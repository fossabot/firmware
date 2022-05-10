/*                       
 * FileName:    NetworkReq_Mng.h
 * Brief:       Network Request manager header file
 */ 

/* ----------------------- Includes ----------------------- */

/* ----------------------- Defines ------------------------ */

/* ---------------------- Data Types ---------------------- */

extern bool network_command;

typedef struct s_NetworkReq_Mng_Data
{
    char  m_topicapp[64];
    char *m_topicbuf;
    char *m_databuf;
    char *m_ansbuf;
    uint16 m_seqtmr_ch1;  /* Counter to know how to wait from ON and OFF (STEP LIGHTS) on ch1 */
    uint8 m_numitertodo_ch1; /* Number of iteration to do on ch1 */
    uint16 m_seqtmr_ch2;  /* Counter to know how to wait from ON and OFF (STEP LIGHTS) on ch2 */
    uint8 m_numitertodo_ch2; /* Number of iteration to do on ch2 */
    char  m_payloadchar[128];
} t_NetworkReq_Mng_Data;


/* -------------- Global function prototypes -------------- */

void ICACHE_FLASH_ATTR NetworkReq_Mng_Init(void);
void ICACHE_FLASH_ATTR NetworkReq_Mng_Hdlr(void);
bool ICACHE_FLASH_ATTR NetworkReq_Mng_send_connected_status(void);
bool ICACHE_FLASH_ATTR NetworkReq_Mng_send_channels_status(void); 
uint16_t ICACHE_FLASH_ATTR update_roll_curr_perc(bool);
uint16_t ICACHE_FLASH_ATTR get_roll_curr_perc(void);
void ICACHE_FLASH_ATTR initialize_network_struct(void);
t_NetworkReq_Mng_Data* ICACHE_FLASH_ATTR alloc_new_network_struct(void);
void ICACHE_FLASH_ATTR deinitialize_network_struct(void);
