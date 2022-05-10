/*
 * FileName:    Wifi_Mng.c
 * Brief:       Wifi manager
 */

/* ----------------------- Includes ----------------------- */

#include "user_interface.h"
#include "osapi.h"
#include "espconn.h"
#include "os_type.h"
#include "mem.h"
#include "mqtt_msg.h"
#include "debug.h"
#include "user_config.h"
#include "Wifi_Mng.h"
#include "Nvm_Mng.h"

/* ---------------------- Data Types ---------------------- */

typedef enum e_Wifi_Mng_Hdlr_Sts
{
    WIFI_MNG_HDLR_INIT = 0,
    WIFI_MNG_HDLR_IDLE,
    WIFI_MNG_HDLR_STATION_SET,
    WIFI_MNG_HDLR_STATION_CHECK,
    WIFI_MNG_HDLR_ACCESSPOINT_SET,
    WIFI_MNG_HDLR_ACCESSPOINT_CHECK
}t_Wifi_Mng_Hdlr_Sts;

typedef struct s_Wifi_Mng_Conf
{
    uint8   m_ipdev[4];
    uint8   m_ipgw[4];
    uint8   m_netmask[4];
    uint8   m_appwd[64];
    uint8   m_channel;
    uint8   m_maxconn;
    uint8   m_beacon;
    uint8   m_authmode;
} t_Wifi_Mng_Conf;

typedef struct s_Wifi_Mng_Data
{
    struct station_config       m_stationconf;
    struct ip_info              m_devip;
    uint8                       m_wifists;
    uint8                       m_wifistslast;
    struct softap_config        m_apconf;       /* Configuration of access point */
    struct ip_info              m_ipconf;       /* Configuration of ip address */
    t_Wifi_Mng_Hdlr_Sts         m_hdlrsts;
    t_Wifi_Mng_Mode             m_wifimode;
    t_Wifi_Mng_Station_Sts      m_stationsts;
    t_Wifi_Mng_AccessPoint_Sts  m_apsts;
    t_Wifi_Mng_Conf            *m_conf;
    uint8                       m_hostname[20];     /* Name of device on router */
    uint8                       m_wifipamcounter;   /* Counter for connection attempts to enabling PAM mode */
    uint8                       m_restartcounter;   /* Counter for connection attempts to restart system */

} t_Wifi_Mng_Data;

/* ------------- Local variable declaration --------------- */

static const t_Wifi_Mng_Conf Wifi_Mng_Conf = {          {192, 168, 5, 1},
                                                        {192, 168, 5, 1},
                                                        {255, 255, 255, 0},
                                                        "powahome\0",
                                                        3,
                                                        4,
                                                        100,
                                                        //AUTH_WPA2_PSK};
                                                        AUTH_OPEN};

static t_Wifi_Mng_Data Wifi_Mng_Data;
static t_Nvm_Mng_Data *dev_conf;

/* ------------- Global variable declaration --------------- */


/* ------------------- Local functions -------------------- */


/* ------------------- Global functions ------------------- */

/*
 * Name:    void Wifi_Mng_Init(void)
 * Descr:   Wifi manager initializer
 */

void ICACHE_FLASH_ATTR Wifi_Mng_Init(void)
{
    /* Initialize the configuration structure */
    Wifi_Mng_Data.m_conf = (t_Wifi_Mng_Conf*)&Wifi_Mng_Conf;
    /* Set FSM to Init state */
    Wifi_Mng_Data.m_hdlrsts = WIFI_MNG_HDLR_INIT;
    /* Configure manager data at default state */
    Wifi_Mng_Data.m_wifists = STATION_IDLE;
    Wifi_Mng_Data.m_wifistslast = STATION_IDLE;
    Wifi_Mng_Data.m_stationsts = WIFI_MNG_STS_STATION_OFF;
    Wifi_Mng_Data.m_apsts = WIFI_MNG_STS_ACCESSPOINT_OFF;
    Wifi_Mng_Data.m_wifimode = WIFI_MNG_MODE_NOCONF;
    Wifi_Mng_Data.m_wifipamcounter = 0;
    Wifi_Mng_Data.m_restartcounter = 0;
    /* Read the eeprom configuration */
    dev_conf = Nvm_Mng_GetNvm();

    /* if exponential backoff timer is not a valid one, set it to the minimum value */
    if (!(is_in_range(dev_conf->m_backoff, MIN_BACKOFF_TIMER, MAX_BACKOFF_TIMER)))
        dev_conf->m_backoff = MIN_BACKOFF_TIMER;

    /* Setup default configuration of WiFi module at startup */
    wifi_set_opmode(STATION_MODE);
}

static void ICACHE_FLASH_ATTR DHCP_Coarse_Tmr_Task(void *arg)
{
    dhcp_coarse_tmr();
}

static void ICACHE_FLASH_ATTR DHCP_Fine_Tmr_Task(void *arg)
{
    dhcp_fine_tmr();
}

void init_dhcp_lwip_timer()
{
    static ETSTimer DHCP_Coarse_Tmr;
    os_timer_disarm(&DHCP_Coarse_Tmr);
    os_timer_setfn(&DHCP_Coarse_Tmr, (os_timer_func_t *)DHCP_Coarse_Tmr_Task, NULL);
    os_timer_arm(&DHCP_Coarse_Tmr, 60 * SEC, TRUE);

    static ETSTimer DHCP_Fine_Tmr;
    os_timer_disarm(&DHCP_Fine_Tmr);
    os_timer_setfn(&DHCP_Fine_Tmr, (os_timer_func_t *)DHCP_Fine_Tmr_Task, NULL);
    os_timer_arm(&DHCP_Fine_Tmr, 500 * MS, TRUE);
}

/*
 * Name:    void Wifi_Mng_Hdlr(void)
 * Descr:   Wifi manager handler
 */

void ICACHE_FLASH_ATTR Wifi_Mng_Hdlr(void)
{
    // used to prevent and endless DHCP_request loop
    static uint8_t dhcp_restart_counter = 0;

    switch (Wifi_Mng_Data.m_hdlrsts)
    {
        case WIFI_MNG_HDLR_INIT:
            /* Go to idle state and wait for command */
            Wifi_Mng_Data.m_hdlrsts = WIFI_MNG_HDLR_IDLE;
            break;

        case WIFI_MNG_HDLR_IDLE:
            /* Init wifi module */
            if (Wifi_Mng_Data.m_wifimode == WIFI_MNG_MODE_STATION)
            {
                Wifi_Mng_Data.m_hdlrsts = WIFI_MNG_HDLR_STATION_SET;
            }
            else if (Wifi_Mng_Data.m_wifimode == WIFI_MNG_MODE_ACCESSPOINT)
            {
                Wifi_Mng_Data.m_hdlrsts = WIFI_MNG_HDLR_ACCESSPOINT_SET;
            }
            break;

        case WIFI_MNG_HDLR_STATION_SET:
            /* Setup wifi in station mode */
            DEBUG_INFO("WIFI_INIT");

            // Initializes also here DHCP timer that seems to be not well managed by the ESP implementation.
            // Sometimes the call of LWIP's dhcp_fine_tmr() is not registered and so no IP is granted.
            // In this way, we register ourself the needed timers for DHCP purposes
            init_dhcp_lwip_timer();

            Wifi_Mng_Data.m_wifipamcounter = 0;
            Wifi_Mng_Data.m_restartcounter = 0;
            os_sprintf(Wifi_Mng_Data.m_hostname,"Powahome-%s%c", dev_conf->m_device_id, Nvm_Mng_read_type());
            wifi_station_set_hostname(Wifi_Mng_Data.m_hostname);
            wifi_set_opmode_current(STATION_MODE);
            /* Configure Wifi struct */
            os_memset(&Wifi_Mng_Data.m_stationconf, 0, sizeof(struct station_config));
            os_sprintf(Wifi_Mng_Data.m_stationconf.ssid,"%s", dev_conf->m_sta_ssid);
            os_sprintf(Wifi_Mng_Data.m_stationconf.password,"%s", dev_conf->m_sta_pwd);
            /* Setup the Wifi configuration */
            wifi_station_set_config_current(&Wifi_Mng_Data.m_stationconf);
            uint8_t ret_val = wifi_station_dhcpc_set_maxtry(DHCP_MAX_RETRY);
            if (ret_val == 0)
                ERROR_INFO("wifi_station_dhcpc_set_maxtry FAIL");
            /* Request connection */
            wifi_station_connect();
            /* Update station status */
            Wifi_Mng_Data.m_stationsts = WIFI_MNG_STS_STATION_WAITCONN;
            /* Chekc if an IP has been set */
            Wifi_Mng_Data.m_hdlrsts = WIFI_MNG_HDLR_STATION_CHECK;

            Wifi_Mng_Data.m_wifipamcounter = 0;
            Wifi_Mng_Data.m_restartcounter = 0;
            break;

        case WIFI_MNG_HDLR_STATION_CHECK:
            /* Get device IP */
            wifi_get_ip_info(STATION_IF, &Wifi_Mng_Data.m_devip);
            Wifi_Mng_Data.m_wifists = wifi_station_get_connect_status();
            // if no IP is available, restart (after DHCP_MAX_COUNTER seconds) the device
            if ((Wifi_Mng_Data.m_wifists != STATION_GOT_IP) || (Wifi_Mng_Data.m_devip.ip.addr == 0))
            {
                if (wifi_station_get_connect_status() == STATION_WRONG_PASSWORD)
                {
                    /* Wrong password */
                    ERROR_INFO("WRONG PASSWORD");

                    if (dev_conf->m_pam_mode == PAM_DISABLE){
                        Wifi_Mng_Data.m_wifipamcounter++;
                    }
                    Wifi_Mng_Data.m_restartcounter++;
                    if (Wifi_Mng_Data.m_restartcounter == WIFI_ATTEMPTS_RESTART){

                        Wifi_Mng_Data.m_restartcounter++;

                        /* Fw restart */
                        dev_conf->m_rebootreason=3;
                        /* Save new config in EEPROM */
                        real_NVM_save();
                        /* Restart ESP */
                        system_restart();
                    }

                    wifi_station_connect();
                }
                else if(wifi_station_get_connect_status() == STATION_NO_AP_FOUND)
                {
                    /* The specified access point has not been found */
                    ERROR_INFO("STATION_NO_AP_FOUND");

                    if (dev_conf->m_pam_mode == PAM_DISABLE){
                        Wifi_Mng_Data.m_wifipamcounter++;
                    }
                    Wifi_Mng_Data.m_restartcounter++;
                    if (Wifi_Mng_Data.m_restartcounter == WIFI_ATTEMPTS_RESTART){

                        Wifi_Mng_Data.m_restartcounter++;
                        /* Fw restart */
                        dev_conf->m_rebootreason=3;
                        /* Save new config in EEPROM */
                        real_NVM_save();
                        /* Restart ESP */
                        system_restart();
                    }

                    wifi_station_connect();
                }
                else if(wifi_station_get_connect_status() == STATION_CONNECT_FAIL)
                {
                    /* Connection failed */
                    ERROR_INFO("STATION_CONNECT_FAIL");

                    if (dev_conf->m_pam_mode == PAM_DISABLE){
                        Wifi_Mng_Data.m_wifipamcounter++;
                    }
                    Wifi_Mng_Data.m_restartcounter++;
                    if (Wifi_Mng_Data.m_restartcounter == WIFI_ATTEMPTS_RESTART){

                        Wifi_Mng_Data.m_restartcounter++;

                        /* Fw restart */
                        dev_conf->m_rebootreason=3;
                        /* Save new config in EEPROM */
                        real_NVM_save();
                        /* Restart ESP */
                        system_restart();
                    }

                    wifi_station_connect();
                }
                else
                {
                    /* Nothing to do */
                }

                Wifi_Mng_Data.m_stationsts = WIFI_MNG_STS_STATION_WAITCONN;

                /* After (num)WIFI_ATTEMPTS_PAM wifi requests failed, enable PAM mode */
                if ((Wifi_Mng_Data.m_wifipamcounter >= WIFI_ATTEMPTS_PAM) && (dev_conf->m_pam_mode == PAM_DISABLE))
                {
                    dev_conf->m_pam_mode = PAM_ENABLE;
                    Nvm_Mng_Save();
                    DEBUG_INFO("PAM mode has been enabled");
                }
            }
            else
            {
                /* If connection come back, disable PAM mode */
                if ((Wifi_Mng_Data.m_wifipamcounter >= WIFI_ATTEMPTS_PAM) && (dev_conf->m_pam_mode == PAM_ENABLE))
                {
                    dev_conf->m_pam_mode = PAM_DISABLE;
                    Nvm_Mng_Save();
                    DEBUG_INFO("PAM mode has been disabled");
                }

                dhcp_restart_counter = 0;
                Wifi_Mng_Data.m_wifipamcounter=0;
                Wifi_Mng_Data.m_restartcounter=0;
                Wifi_Mng_Data.m_stationsts = WIFI_MNG_STS_STATION_CONNOK;
            }
            break;

        case WIFI_MNG_HDLR_ACCESSPOINT_SET:
            /* Setup WiFi in access point mode */
            wifi_set_opmode_current(SOFTAP_MODE);
            /* Stop DHCP service for allowing configuration*/
            wifi_softap_dhcps_stop();
            /* Setup the access point IP address */
            IP4_ADDR(&Wifi_Mng_Data.m_ipconf.ip,    Wifi_Mng_Data.m_conf->m_ipdev[0],
                                                    Wifi_Mng_Data.m_conf->m_ipdev[1],
                                                    Wifi_Mng_Data.m_conf->m_ipdev[2],
                                                    Wifi_Mng_Data.m_conf->m_ipdev[3]);
            /* Setup the access point IPgateway */
            IP4_ADDR(&Wifi_Mng_Data.m_ipconf.gw,    Wifi_Mng_Data.m_conf->m_ipgw[0],
                                                    Wifi_Mng_Data.m_conf->m_ipgw[1],
                                                    Wifi_Mng_Data.m_conf->m_ipgw[2],
                                                    Wifi_Mng_Data.m_conf->m_ipgw[3]);

            /* Setup the access point IPgateway */
            IP4_ADDR(&Wifi_Mng_Data.m_ipconf.netmask,   Wifi_Mng_Data.m_conf->m_netmask[0],
                                                        Wifi_Mng_Data.m_conf->m_netmask[1],
                                                        Wifi_Mng_Data.m_conf->m_netmask[2],
                                                        Wifi_Mng_Data.m_conf->m_netmask[3]);
            /* Setup the IP configuration into the device */
            wifi_set_ip_info(SOFTAP_IF, &Wifi_Mng_Data.m_ipconf);
            /* Restart DHCP service */
            wifi_softap_dhcps_start();
            /* Clean access point configuration */
            os_memset(&Wifi_Mng_Data.m_apconf, 0, sizeof(struct softap_config));
            os_sprintf(Wifi_Mng_Data.m_apconf.ssid, "Powahome-%s%c", dev_conf->m_device_id, Nvm_Mng_read_type());
            /* Setup access point parameters */
            strcpy(Wifi_Mng_Data.m_apconf.password, Wifi_Mng_Data.m_conf->m_appwd);
            Wifi_Mng_Data.m_apconf.channel = Wifi_Mng_Data.m_conf->m_channel;
            Wifi_Mng_Data.m_apconf.max_connection = Wifi_Mng_Data.m_conf->m_maxconn;
            Wifi_Mng_Data.m_apconf.beacon_interval = Wifi_Mng_Data.m_conf->m_beacon;
            Wifi_Mng_Data.m_apconf.authmode = Wifi_Mng_Data.m_conf->m_authmode;
            wifi_softap_set_config_current(&Wifi_Mng_Data.m_apconf);
            Wifi_Mng_Data.m_hdlrsts = WIFI_MNG_HDLR_ACCESSPOINT_CHECK;
            break;

        case WIFI_MNG_HDLR_ACCESSPOINT_CHECK:
            /* Access Point enabled */
            Wifi_Mng_Data.m_apsts = WIFI_MNG_STS_ACCESSPOINT_ON;
            break;

        default:
            break;

    }
}


/*
 * Name:    void Wifi_Mng_SetMode(void)
 * Descr:   Wifi manager handler
 */

void ICACHE_FLASH_ATTR Wifi_Mng_SetMode(t_Wifi_Mng_Mode p_mode)
{
    Wifi_Mng_Data.m_wifimode = p_mode;
}

/*
 * Name:    void Wifi_Mng_GetStationStatus(void)
 * Descr:   get wifi station status
 */

t_Wifi_Mng_Station_Sts ICACHE_FLASH_ATTR Wifi_Mng_GetStationStatus(void)
{
    return Wifi_Mng_Data.m_stationsts;
}

/*
 * Name:    void Wifi_Mng_GetAccessPointStatus(void)
 * Descr:   get access point status
 */

t_Wifi_Mng_AccessPoint_Sts ICACHE_FLASH_ATTR Wifi_Mng_GetAccessPointStatus(void)
{
    return Wifi_Mng_Data.m_apsts;
}
