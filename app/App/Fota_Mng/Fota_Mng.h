/* Fota_Mng.h */

#ifndef FOTA_CONFIG_H
#define FOTA_CONFIG_H

/* ----------------------- Includes ----------------------- */
#include "espconn.h"

/* ----------------------- Defines ------------------------ */
#define FOTA_CHECK_TIMER_MS 60000
#define FOTA_MAX_ATTEMPTS   2
#define MAX_ROLLBACK_COUNTER 3

/* ---------------------- Data Types ---------------------- */

typedef enum e_fota_status
{
    FOTA_STATUS_OK = 0,
    FOTA_STATUS_STARTED,
    FOTA_STATUS_UPGRADING,
    FOTA_STATUS_PENDING,
    FOTA_STATUS_ROLLBACK,
} t_fota_status;

#ifdef GLOBAL_DEBUG_ON
const char *fota_status_string[5]; 
#endif

typedef enum e_Fota_Mng_Hdlr_Sts
{
    FOTA_MNG_HDLR_IDLE = 0,
    FOTA_MNG_HDLR_CONF,
    FOTA_MNG_HDLR_WAITCONN,
    FOTA_MNG_HDLR_WAIT_SEC_DISC,
    FOTA_MNG_HDLR_CONNACK,
    FOTA_MNG_HDLR_END
}t_Fota_Mng_Hdlr_Sts;

typedef struct s_Fota_Mng_Conf
{
    uint8   m_ipsrv[4];                 /* server IP */
    uint16  m_lport;                    /* local port */
    uint16  m_rport;                    /* remote port */
    uint8   m_sckttype;                 /* socket type */
} t_Fota_Mng_Conf;

typedef struct s_Fota_Mng_Data
{
    uint8                       m_srvconn;
    uint8                       m_fotares;
    uint8                       m_userbin[48];
    t_Fota_Mng_Hdlr_Sts         m_hdlrsts;
    struct espconn              m_fotaconn;
    struct upgrade_server_info *m_fotasrv;
    const t_Fota_Mng_Conf      *m_conf;         /* Default configuration */
} t_Fota_Mng_Data;

/* -------------- Global function prototypes -------------- */

void ICACHE_FLASH_ATTR Fota_Mng_Init(void);
void ICACHE_FLASH_ATTR Fota_Mng_Hdlr(void);
void ICACHE_FLASH_ATTR Fota_Mng_StartFota(void);
uint8_t* ICACHE_FLASH_ATTR Fota_Mng_allocate_payload(uint8_t);
bool ICACHE_FLASH_ATTR Fota_Mng_create_pheadbuffer(uint8_t *);
void ICACHE_FLASH_ATTR update_fota_status(t_fota_status);
void ICACHE_FLASH_ATTR initialize_fota_struct(void);
t_Fota_Mng_Data* ICACHE_FLASH_ATTR alloc_new_fota_struct(void);
void ICACHE_FLASH_ATTR deinitialize_network_struct(void);

#endif /* FOTA_CONFIG_H */
