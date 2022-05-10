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
#include "Fota_Mng.h"
#include "Nvm_Mng.h"

#ifdef GLOBAL_DEBUG_ON
const char *fota_status_string[] =
{
    "FOTA_STATUS_OK",
    "FOTA_STATUS_STARTED",
    "FOTA_STATUS_UPGRADING",
    "FOTA_STATUS_PENDING",
    "FOTA_STATUS_ROLLBACK",
};
#endif

static ETSTimer fota_timer;
char s3_aws_url[128];
t_Fota_Mng_Data *Fota_Mng_Data = NULL;
uint8_t fota_curr_attempt = 1;
bool to_be_fota_restarted = false;
bool fota_restartable = false;
t_Nvm_Mng_Data *dev_conf;

char fota_hostname[128];
t_Fota_Mng_Conf Fota_Mng_Conf = {{255, 255, 255, 255}, 2001, 80, ESPCONN_TCP};

void ICACHE_FLASH_ATTR upgrade_routine(void)
{
    fota_restartable = false;
    /* Disable PAM is FOTA is successful */
    dev_conf->m_pam_mode = PAM_DISABLE;
    /* Set the correct reboot reason */
    dev_conf->m_rebootreason = RR_FOTA_OK;
    update_fota_status(FOTA_STATUS_PENDING);
    real_NVM_save();

    FOTA_INFO("Firmware upgraded successfully [%d/%d]. System will reboot soon...", fota_curr_attempt, FOTA_MAX_ATTEMPTS);

    system_upgrade_reboot();
    return;
}

void ICACHE_FLASH_ATTR parse_s3_url(void)
{
    char *temp;

    /* https | to be discarded */
    temp = strtok(s3_aws_url, "/");

    /* hostname | to be discarded */
    temp = strtok(NULL, "/");
    os_sprintf(fota_hostname, temp);

    /* userbin */
    temp = strtok(NULL, "");
    os_strcpy(s3_aws_url, temp);
}

/* Callback function called when FOTA upgrade terminate */
LOCAL void ICACHE_FLASH_ATTR FotaUpgradeResp(void *arg)
{
    FOTA_INFO("FotaUpgradeResp");
    Fota_Mng_Data->m_fotares = TRUE;
}

/* Callback function called when connected to FOTA server */
LOCAL void ICACHE_FLASH_ATTR FotaUpgradeConnCb(void *arg)
{
    FOTA_INFO("FotaUpgradeConnCb");
    /* Set the connection flag successfull */
    Fota_Mng_Data->m_srvconn = TRUE;

    /* Disarm the timeout timer*/
    os_timer_disarm(&fota_timer);
}

t_Fota_Mng_Data *ICACHE_FLASH_ATTR alloc_new_fota_struct(void)
{
    if (Fota_Mng_Data != NULL)
        return;

    t_Fota_Mng_Data *new_struct = os_zalloc(sizeof(char)*sizeof(t_Fota_Mng_Data));

    new_struct->m_fotasrv = NULL;
    new_struct->m_hdlrsts = FOTA_MNG_HDLR_IDLE;
    new_struct->m_conf = &(Fota_Mng_Conf);

    return new_struct;
}

void ICACHE_FLASH_ATTR initialize_fota_struct(void)
{
    DEBUG_INFO("initialize_fota_struct | BEFORE: %p", Fota_Mng_Data);
    Fota_Mng_Data = alloc_new_fota_struct();
    DEBUG_INFO("initialize_fota_struct | AFTER: %p", Fota_Mng_Data);
}

void ICACHE_FLASH_ATTR deinitialize_fota_struct(void)
{
    powa_free((void *)&Fota_Mng_Data, "Fota_Mng_Data");
}

/* FOTA Manager initialization function */
void ICACHE_FLASH_ATTR Fota_Mng_Init(void)
{
    FOTA_INFO("Fota_Mng_Init");
    dev_conf = Nvm_Mng_GetNvm();
    initialize_fota_struct();
    FOTA_INFO("Fota_Mng_Init complete!");
}

static void ICACHE_FLASH_ATTR fota_increase_counter()
{
    if (fota_curr_attempt < FOTA_MAX_ATTEMPTS)
    {
        /* Increase the counter */
        fota_curr_attempt++;
        FOTA_INFO("FOTA current attempt: %d/%d", fota_curr_attempt, FOTA_MAX_ATTEMPTS);
        /* Try to upgrade another time */
        Fota_Mng_Data->m_hdlrsts = FOTA_MNG_HDLR_CONF;
    }
    else
    {
        FOTA_INFO("Reached FOTA_MAX_ATTEMPTS [%d], going into IDLE status", FOTA_MAX_ATTEMPTS);
        /* Reset the counter */
        fota_curr_attempt = 0;
        /* Go to IDLE, waiting to be triggered with an MQTT command */
        Fota_Mng_Data->m_hdlrsts = FOTA_MNG_HDLR_IDLE;
    }
}

static void ICACHE_FLASH_ATTR fota_timer_cb(void *arg)
{
    FOTA_INFO("fota_timer_cb");

    struct espconn *conn = (struct espconn *)arg;
    espconn_disconnect(conn);

    fota_increase_counter();
}

/* FOTA Manager handler */
void ICACHE_FLASH_ATTR Fota_Mng_Hdlr(void)
{
    if (Fota_Mng_Data == NULL)
        return;

    switch(Fota_Mng_Data->m_hdlrsts)
    {
        case FOTA_MNG_HDLR_IDLE:
            break;

        case FOTA_MNG_HDLR_CONF:
            FOTA_INFO("Configuring FOTA server connection");
            /* Reset the connection flag */
            Fota_Mng_Data->m_srvconn = FALSE;
            /* Reset the connection flag */
            Fota_Mng_Data->m_fotares = FALSE;

            /* Arm the timer for the timeout */
            os_timer_setfn(&fota_timer, (os_timer_func_t *)fota_timer_cb, NULL);
            os_timer_arm(&fota_timer, 1*MIN, FALSE);

            /* Request connection to FOTA server */
            espconn_connect(&Fota_Mng_Data->m_fotaconn);
            /* Go to wait conn state */
            Fota_Mng_Data->m_hdlrsts = FOTA_MNG_HDLR_WAITCONN;
            break;

        case FOTA_MNG_HDLR_WAITCONN:
            if (Fota_Mng_Data->m_srvconn == TRUE)
            {
                /* Go to wait conn state */
                Fota_Mng_Data->m_hdlrsts = FOTA_MNG_HDLR_CONNACK;
            }
            break;

        case FOTA_MNG_HDLR_CONNACK:
            FOTA_INFO("Connected to upgrade server");
            /* Configure FOTA parameters */
            uint8_t url_string[512];    // from strlen(url) we get that the length is 344, so let's allocate a bit more
            int len = 0;
            uint8_t *pheadbuffer = Fota_Mng_allocate_payload(255);   // old static pheadbuffer, now is dynamic
            Fota_Mng_create_pheadbuffer(pheadbuffer);
            Fota_Mng_Data->m_fotasrv = (struct upgrade_server_info *)os_zalloc(sizeof(struct upgrade_server_info));
            os_strcpy(Fota_Mng_Data->m_fotasrv->upgrade_version, "");
            os_strcpy(Fota_Mng_Data->m_fotasrv->pre_version,"");
            os_memcpy(Fota_Mng_Data->m_fotasrv->ip, Fota_Mng_Data->m_fotaconn.proto.tcp->remote_ip, 4);
            Fota_Mng_Data->m_fotasrv->port = Fota_Mng_Data->m_conf->m_rport;
            Fota_Mng_Data->m_fotasrv->check_cb = FotaUpgradeResp;
            Fota_Mng_Data->m_fotasrv->check_times = FOTA_CHECK_TIMER_MS;
            /* Detect the right FW binary to download depending on the current partition */

            os_sprintf(url_string, "GET /%s HTTP/1.1\r\nHost: %s\r\n%s", s3_aws_url, fota_hostname, pheadbuffer);
            DEBUG_INFO("url_string: %s", url_string);
            len = strlen(url_string);
            /* TODO Allocate the server url. To be removed. No dynamic alloc */
            if (Fota_Mng_Data->m_fotasrv->url == NULL)
                Fota_Mng_Data->m_fotasrv->url = (uint8_t *)os_zalloc(strlen(url_string) + 1);

            /* Set the download address */
            os_strncpy(Fota_Mng_Data->m_fotasrv->url, url_string, len);
            FOTA_INFO("Requesting firmware from %s", Fota_Mng_Data->m_fotasrv->url);
            /* Start FOTA Update */
            if (system_upgrade_start(Fota_Mng_Data->m_fotasrv) == false)
            {
                FOTA_INFO("Upgrade is already started");
            }
            else
            {
                FOTA_INFO("Upgrade started");
                update_fota_status(FOTA_STATUS_UPGRADING);
            }
            /* Go to wait end fota state */
            Fota_Mng_Data->m_hdlrsts = FOTA_MNG_HDLR_END;
            break;

        case FOTA_MNG_HDLR_END:
            if ((Fota_Mng_Data->m_fotares == TRUE) && (ButtonReq_Mng_GetRollActReq() == FALSE))
            {
                if (fota_restartable == TRUE)
                {
                    upgrade_routine();
                    return;
                }

                /* Check FOTA return flag. If TRUE FOTA is successful, if FALSE FOTA failed */
                if (Fota_Mng_Data->m_fotasrv->upgrade_flag == TRUE)
                {
                    memory_deallocation_flag = FALSE;
                    if (to_be_fota_restarted == FALSE)
                    {
                        char msg[128];
                        os_sprintf(msg, "{ \"state\": { \"reported\": { \"connected\": false, \"fotaMode\": true, \"clientId\": \"%s\", \"sessionId\": \"%s\", \"isLwt\": false } } }", mqtt_client_id, session_id);
                        publish_on_topic("status/update", msg);
                        to_be_fota_restarted = TRUE;
                    }
                }
                else
                {
                    /* Set the correct reboot reason */
                    dev_conf->m_rebootreason = RR_FOTA_FAIL;
                    FOTA_INFO("Firmware upgrade failed");
                    Nvm_Mng_Save();
                }

                if (to_be_fota_restarted == FALSE)
                {
                    fota_increase_counter();
                }
            }
            break;
    }
}

/* DNS implementation*/
LOCAL void ICACHE_FLASH_ATTR user_esp_platform_dns_found(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;

    if (ipaddr == NULL) {
        FOTA_INFO("IP address not found");
        return;
    }

    FOTA_INFO("IP address resolved: %d.%d.%d.%d", *((uint8 *)&ipaddr->addr), *((uint8 *)&ipaddr->addr + 1), *((uint8 *)&ipaddr->addr + 2), *((uint8 *)&ipaddr->addr + 3));
    FOTA_INFO("Setting ip address...");
    os_memcpy(Fota_Mng_Data->m_fotaconn.proto.tcp->remote_ip, &ipaddr->addr, 4);

    FOTA_INFO("Disconnecting from DNS server");
    espconn_disconnect(pespconn);

    FOTA_INFO("Setting new FOTA state and start upgrade");
    Fota_Mng_Data->m_hdlrsts = FOTA_MNG_HDLR_CONF;
    update_fota_status(FOTA_STATUS_STARTED);
    return;
}


/*FOTA Manager Start fota */
void ICACHE_FLASH_ATTR Fota_Mng_StartFota(void)
{
    if (Fota_Mng_Data->m_hdlrsts != FOTA_MNG_HDLR_IDLE)
    {
        FOTA_INFO("Running FOTA procedure: new upgrade request will be ignored! [m_hdlrsts: %d]", Fota_Mng_Data->m_hdlrsts);
        return;
    }

    FOTA_INFO("Fota_Mng_StartFota");
    /* Configure socket for TCP connection on FOTA port*/
    Fota_Mng_Data->m_fotaconn.type = Fota_Mng_Data->m_conf->m_sckttype;
    Fota_Mng_Data->m_fotaconn.state = ESPCONN_NONE;
    Fota_Mng_Data->m_fotaconn.proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
    Fota_Mng_Data->m_fotaconn.proto.tcp->local_port = Fota_Mng_Data->m_conf->m_lport;
    Fota_Mng_Data->m_fotaconn.proto.tcp->remote_port = Fota_Mng_Data->m_conf->m_rport;
    espconn_regist_connectcb(&Fota_Mng_Data->m_fotaconn, FotaUpgradeConnCb);

    parse_s3_url();
    ip_addr_t ipaddr;

    FOTA_INFO("Fota_Mng_StartFota Asking for IP address of FOTA server (hostname: %s)", fota_hostname);
    // Gets the IP from hostname
    espconn_gethostbyname(&Fota_Mng_Data->m_fotaconn, fota_hostname, &ipaddr, user_esp_platform_dns_found);
}

uint8_t ICACHE_FLASH_ATTR *Fota_Mng_allocate_payload(uint8_t size)
{
    uint8_t *payload = os_zalloc(sizeof(char)*size);
    return payload;
}

bool ICACHE_FLASH_ATTR Fota_Mng_create_pheadbuffer(uint8_t *pheadbuffer)
{
    uint8_t temp[128];

    strcpy(pheadbuffer, "Connection: keep-alive\r\n");
    os_sprintf(temp, "User-Agent: Powahome-%s/%d.%d.%d (%s)\r\n", dev_conf->m_device_id, FW_VERS_MAJOR, FW_VERS_MINOR, FW_VERS_BUGFIX, system_get_userbin_addr() == USER_BIN2 ? "user2" : "user1");
    strcat(pheadbuffer, temp);
    strcat(pheadbuffer, "Accept: */*\r\n\r\n");
}

void ICACHE_FLASH_ATTR update_fota_status(t_fota_status new_fota_status)
{
    #ifdef GLOBAL_DEBUG_ON
    DEBUG_INFO(" ");
    DEBUG_INFO("update_fota_status | %d -> %d", dev_conf->m_fota_status, new_fota_status);
    DEBUG_INFO("update_fota_status | %s -> %s", fota_status_string[dev_conf->m_fota_status], fota_status_string[new_fota_status]);
    DEBUG_INFO(" ");
    #endif

    dev_conf->m_fota_status = new_fota_status;

    if (new_fota_status == FOTA_STATUS_OK)
    {
        dev_conf->m_rollback_counter = 0;
        Nvm_Mng_Save();
    }
    if (new_fota_status == FOTA_STATUS_ROLLBACK)
    {
        dev_conf->m_rebootreason = RR_ROLLBACK;
        real_NVM_save();
    }
}
