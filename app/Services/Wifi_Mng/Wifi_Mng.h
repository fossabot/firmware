
/*                       
* FileName: Wifi_Mng.h
* Brief:    Wifi manager header file
*/ 

#ifndef USER_WIFI_H_
#define USER_WIFI_H_

/* ----------------------- Includes ----------------------- */

/* ----------------------- Defines ------------------------ */
#define WIFI_ATTEMPTS_PAM 3
#define WIFI_ATTEMPTS_RESTART 180

/* ---------------------- Data Types ---------------------- */

typedef enum e_Wifi_Mng_Mode
{
    WIFI_MNG_MODE_NOCONF,       /* Initial state before configuration */
    WIFI_MNG_MODE_STATION = 0,  /* Configure the WiFi module as a normal wifi device */
    WIFI_MNG_MODE_ACCESSPOINT   /* Configure the WiFi module as an access point */
}t_Wifi_Mng_Mode;

typedef enum e_Wifi_Mng_Station_Sts
{
    WIFI_MNG_STS_STATION_OFF,
    WIFI_MNG_STS_STATION_WAITCONN,
    WIFI_MNG_STS_STATION_CONNOK,
}t_Wifi_Mng_Station_Sts;

typedef enum e_Wifi_Mng_AccessPoint_Sts
{
    WIFI_MNG_STS_ACCESSPOINT_OFF,
    WIFI_MNG_STS_ACCESSPOINT_ON
}t_Wifi_Mng_AccessPoint_Sts;

/* -------------- Global function prototypes -------------- */

void                        ICACHE_FLASH_ATTR Wifi_Mng_Init(void);
void                        ICACHE_FLASH_ATTR Wifi_Mng_Hdlr(void);
void                        ICACHE_FLASH_ATTR Wifi_Mng_SetMode(t_Wifi_Mng_Mode p_mode);
t_Wifi_Mng_Station_Sts      ICACHE_FLASH_ATTR Wifi_Mng_GetStationStatus(void);
t_Wifi_Mng_AccessPoint_Sts  ICACHE_FLASH_ATTR Wifi_Mng_GetAccessPointStatus(void);

#endif /* USER_WIFI_H_ */
