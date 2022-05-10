/*
 * FileName:    NetworkReq_Mng.c
 * Brief:       Network requests manager
 */

/* ----------------------- Includes ----------------------- */

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
#include "NetworkReq_Mng.h"
#include "ButtonReq_Mng.h"
#include "Nvm_Mng.h"
#include "Mqtt_Mng.h"
#include "Misc.h"

/* ----------------------- Defines ------------------------ */
#define BACKUP_PRIVATE_KEY_ADDRESS  0xF1000
#define BACKUP_CA_CERT_ADDRESS      0xF0000
#define PRIVATE_KEY_ADDRESS         0xF6000
#define CA_CERT_ADDRESS             0xF5000
#define PAYLOAD_SIZE                512
/* ---------------------- Data Types ---------------------- */

/* ------------- Local variable declaration --------------- */

bool mqtt_serial_enable;
t_Nvm_Mng_Data *dev_conf;
t_NetworkReq_Mng_Data *NetworkReq_Mng_Data = NULL;
char s3_aws_url[128];
char mqtt_client_id[17];
char session_id[4];
bool network_feedback = false;
int8_t times = 0;
bool restartable;
bool going_down;
bool going_up;
bool stopped;
uint16_t roller_steps_to_publish = 0;
uint8_t roll_curr_perc = 0;
uint16_t roll_final_perc = 0;
uint16_t rise_steps_to_reach;
uint16_t fall_steps_to_reach;
float rise_fall_ratio;

bool disconnected_for_reboot = FALSE;
bool ap_mode_reboot_required = FALSE;

bool roller_network_command = FALSE;

t_timer turning_timer;

bool memory_deallocation_flag;
char mqtt_msg[256];

char base_topic[16];

/* DIMMER */
t_dimmer_state dimmer_curr_state[2];
bool dimmer_action_requested[2];
uint8_t dimmer_final_perc[2];
uint8_t dimmer_curr_perc[2];
uint8_t dimmer_delta_perc[2];
uint16_t dimmer_steps_to_do[2];
uint16_t dimmer_steps_done[2];
uint8_t dimmer_mqtt_final_perc[2];
uint8_t dimmer_mqtt_on_off[2];
bool dimmer_direction[2];    // HIGH = need to increase; LOW = need to decrease
t_timer dimmer_pression_timer[2];
t_timer dimmer_mqtt_shortpress_timer[2];

/* AWS updating related variables */
// ca_cert
struct espconn ca_cert_conn;
ip_addr_t ca_cert_ip;
esp_tcp ca_cert_tcp;
char ca_cert_mac_sha256[SHA256_STR_SIZE], ca_cert_mac_json[SHA256_STR_SIZE];
char *ca_cert_payload = NULL;
bool ca_cert_successful = FALSE;

// private_key
struct espconn private_key_conn;
ip_addr_t private_key_ip;
esp_tcp private_key_tcp;
char private_key_mac_sha256[SHA256_STR_SIZE], private_key_mac_json[SHA256_STR_SIZE];
char *private_key_payload = NULL;
bool private_key_successful = FALSE;

char *host = NULL;

/* ------------------- Global functions ------------------- */


void ICACHE_FLASH_ATTR get_userlink_from_string(char *dst, char *src, char *new_src)
{
    DEBUG_INFO("get_userlink_from_string | src: %s", src);
    char *token;

    token = strtok(src, "\"");
    token = strtok(NULL, "\"");
    token = strtok(NULL, "\"");
    token = strtok(NULL, "\"");

    strncpy(dst, token, os_strlen(token));
}

bool ICACHE_FLASH_ATTR check_NVM_consistency(uint32_t data_address, size_t data_len, char *sha256_mac)
{
    char from_flash[data_len];
    char nvm_sha256_mac[SHA256_STR_SIZE];

    if (read_from_flash(data_address, from_flash, data_len) == FALSE)
        return FALSE;

    compute_sha256(from_flash, data_len, nvm_sha256_mac);

    int res_sha256 = os_strcmp(nvm_sha256_mac, sha256_mac);
    if (res_sha256 != 0)
    {
        ERROR_INFO("check_NVM_consistency");
        ERROR_INFO("sha256_mac:\t\t%s", sha256_mac);
        ERROR_INFO("nvm_sha256_mac:\t%s", nvm_sha256_mac);
        return FALSE;
    }

    return TRUE;
}

bool ICACHE_FLASH_ATTR validate_aws_url(char *cert_url)
{
    char *aws_url = "http://s3.eu-central-1.amazonaws.com/powahome-iot-prod-certificates-etxftb5sxrzy/";
    int res_cmp = os_strncmp(aws_url, cert_url, os_strlen(aws_url));

    if (res_cmp != 0)
    {
        ERROR_INFO("validate_aws_url | os_strncmp [res_cmp: %d]", res_cmp);
        ERROR_INFO("aws_url: %s", aws_url);
        ERROR_INFO("cert_url: %s", cert_url);
        return FALSE;
    }

    DEBUG_INFO("validate_aws_url | MATCH!");
    return TRUE;
}

void ICACHE_FLASH_ATTR build_private_key_payload(char *bin_link)
{
    if (bin_link == NULL)
        return;

    private_key_payload = os_zalloc(sizeof(char)*PAYLOAD_SIZE);
    char *bucket, *file;

    host = split(bin_link, "//");
    bucket = split(host, "/");
    file = split(bucket, "/");

    os_sprintf(private_key_payload, "GET /%s/%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", bucket, file, host);
}

void ICACHE_FLASH_ATTR build_ca_cert_payload(char *bin_link)
{
    if (bin_link == NULL)
        return;

    ca_cert_payload = os_zalloc(sizeof(char)*PAYLOAD_SIZE);
    char *bucket, *file;

    host = split(bin_link, "//");
    bucket = split(host, "/");
    file = split(bucket, "/");

    os_sprintf(ca_cert_payload, "GET /%s/%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", bucket, file, host);
}

void ICACHE_FLASH_ATTR private_key_data_received(void *arg, char *pdata, unsigned short len)
{
    static uint32_t data_len = 0;
    static char *data_buff = NULL;
    static data_address = PRIVATE_KEY_ADDRESS;
    static uint32_t packet_len = 37564;

    if (packet_len == 37564)
    {
        char *token;
        char *str_len;
        char *p;

        /* should avoid memory leaking */
        if (data_buff != NULL)
            powa_free((void *)&data_buff, "data_buff");

        data_buff = os_zalloc(sizeof(uint8_t)*3000);
        if (data_buff == NULL)
        {
            ERROR_INFO("private_key_data_received | os_zalloc");
            return;
        }

        strcat(data_buff, pdata);

        token = split(data_buff, "Content-Length:");
        if (token == NULL)
        {
            ERROR_INFO("private_key_data_received | split on Content-Length:");
            ERROR_INFO("private_key_data_received | freeing 'data_buff'");
            powa_free((void *)&data_buff, "data_buff");
            return;
        }

        str_len = split(token, "Server: ");
        if (str_len == NULL)
        {
            ERROR_INFO("private_key_data_received | split on Server:");
            ERROR_INFO("private_key_data_received | freeing 'data_buff'");
            powa_free((void *)&data_buff, "data_buff");
            return;
        }

        /* get the data length from the HTTP response's header */
        packet_len = (uint16_t) strtoul(token, &p, 10); 
        token = split(str_len, "\r\n\r\n");
        if (token == NULL)
        {
            ERROR_INFO("private_key_data_received | split on \\r\\n\\r\\n");
            ERROR_INFO("private_key_data_received | freeing 'data_buff'");
            powa_free((void *)&data_buff, "data_buff");
            return;
        }

        /* clear the data */
        os_strcpy(data_buff, token);
        data_len = os_strlen(token);
    }
    else
    {
        os_memcpy(data_buff + data_len, pdata, len);
        data_len += len;
    }

    /* all data has been received */
    if (data_len >= packet_len)
    {
        compute_sha256(data_buff, data_len, private_key_mac_sha256);

        int res_strcmp = os_strcmp(private_key_mac_sha256, private_key_mac_json);
        if (res_strcmp != 0)
        {
            ERROR_INFO("private_key_mac_sha256:\t%s", private_key_mac_sha256);
            ERROR_INFO("private_key_mac_json:\t\t%s", private_key_mac_json);
            return;
        }
        else
        {
            DEBUG_INFO("private_key sha256 matches!");
            DEBUG_INFO("private_key_mac_sha256:\t%s", private_key_mac_sha256);
            DEBUG_INFO("private_key_mac_json:\t\t%s", private_key_mac_json);
        }

        private_key_successful = write_to_flash(data_buff, data_address, data_len);
        data_len = 0;
        packet_len = 37564;
        // free the allocated memory?
        // powa_free((void *)&data_buff, "data_buff");
        return;
    }
}

void ICACHE_FLASH_ATTR ca_cert_data_received(void *arg, char *pdata, unsigned short len)
{
    static uint32_t data_len = 0;
    static char *data_buff = NULL;
    static data_address = CA_CERT_ADDRESS;
    static uint32_t packet_len = 37564;

    if (packet_len == 37564)
    {
        char *token;
        char *str_len;
        char *p;

        /* should avoid leaking memory */
        if (data_buff != NULL)
            powa_free((void *)&data_buff, "data_buff");

        data_buff = os_zalloc(sizeof(uint8_t)*10000);
        if (data_buff == NULL)
        {
            ERROR_INFO("ca_cert_data_received | os_zalloc");
            return;
        }

        strcat(data_buff, pdata);

        token = split(data_buff, "Content-Length:");
        if (token == NULL)
        {
            ERROR_INFO("ca_cert_data_received | split on Content-Length:");
            ERROR_INFO("ca_cert_data_received | freeing 'data_buff'");
            powa_free((void *)&data_buff, "data_buff");
            return;
        }

        str_len = split(token, "Server: ");
        if (str_len == NULL)
        {
            ERROR_INFO("ca_cert_data_received | split on Server:");
            ERROR_INFO("ca_cert_data_received | freeing 'data_buff'");
            powa_free((void *)&data_buff, "data_buff");
            return;
        }

        /* get the data length from the HTTP response's header */
        packet_len = (uint16_t) strtoul(token, &p, 10); 
        token = split(str_len, "\r\n\r\n");
        if (token == NULL)
        {
            ERROR_INFO("ca_cert_data_received | split on \\r\\n\\r\\n");
            ERROR_INFO("ca_cert_data_received | freeing 'data_buff'");
            powa_free((void *)&data_buff, "data_buff");
            return;
        }

        /* clear the data */
        os_strcpy(data_buff, token);
        data_len = os_strlen(token);
    }
    else
    {
        os_memcpy(data_buff + data_len, pdata, len);
        data_len += len;
    }

    /* all data has been received */
    if (data_len >= packet_len)
    {
        compute_sha256(data_buff, data_len, ca_cert_mac_sha256);

        int res_strcmp = os_strcmp(ca_cert_mac_sha256, ca_cert_mac_json);
        if (res_strcmp != 0)
        {
            ERROR_INFO("ca_cert_mac_sha256:\t%s", ca_cert_mac_sha256);
            ERROR_INFO("ca_cert_mac_json:\t%s", ca_cert_mac_json);
            return;
        }
        else
        {
            DEBUG_INFO("ca_cert sha256 matches!");
            DEBUG_INFO("ca_cert_mac_sha256:\t%s", ca_cert_mac_sha256);
            DEBUG_INFO("ca_cert_mac_json:\t%s", ca_cert_mac_json);
        }

        ca_cert_successful = write_to_flash(data_buff, data_address, data_len);
        data_len = 0;
        packet_len = 37564;
        // free the allocated memory?
        // powa_free((void *(void *))&data_buff, "data_buff");
        return;
    }
}

void ca_cert_payload_sent(void *arg)
{
    os_free(ca_cert_payload);
}

void private_key_payload_sent(void *arg)
{
    os_free(private_key_payload);
}

void ICACHE_FLASH_ATTR ca_cert_tcp_connected(void *arg)
{
    struct espconn *conn = arg;

    espconn_regist_recvcb(conn, ca_cert_data_received);
    espconn_regist_sentcb(conn, ca_cert_payload_sent);
    espconn_sent(conn, ca_cert_payload, os_strlen(ca_cert_payload));
}

void ICACHE_FLASH_ATTR private_key_tcp_connected(void *arg)
{
    struct espconn *conn = arg;

    espconn_regist_recvcb(conn, private_key_data_received);
    espconn_regist_sentcb(conn, private_key_payload_sent);
    espconn_sent(conn, private_key_payload, os_strlen(private_key_payload));
}

void ICACHE_FLASH_ATTR private_key_dns_done(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct espconn *conn = arg;
    
    if (ipaddr == NULL) 
        ERROR_INFO("private_key_dns_done | DNS lookup failed\n");
    else
    {
        conn->type = ESPCONN_TCP;
        conn->state = ESPCONN_NONE;
        conn->proto.tcp = &private_key_tcp;
        conn->proto.tcp->local_port = espconn_port();
        conn->proto.tcp->remote_port = 80;
        os_memcpy(conn->proto.tcp->remote_ip, &ipaddr->addr, 4);

        espconn_regist_connectcb(conn, private_key_tcp_connected);
        espconn_connect(conn);
    }
}

void ICACHE_FLASH_ATTR ca_cert_dns_done(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct espconn *conn = arg;
    
    if (ipaddr == NULL) 
        ERROR_INFO("ca_cert_dns_done | DNS lookup failed\n");
    else
    {
        conn->type = ESPCONN_TCP;
        conn->state = ESPCONN_NONE;
        conn->proto.tcp = &ca_cert_tcp;
        conn->proto.tcp->local_port = espconn_port();
        conn->proto.tcp->remote_port = 80;
        os_memcpy(conn->proto.tcp->remote_ip, &ipaddr->addr, 4);

        espconn_regist_connectcb(conn, ca_cert_tcp_connected);
        espconn_connect(conn);
    }
}

void ICACHE_FLASH_ATTR update_aws_private_key(char *bin_link)
{
    build_private_key_payload(bin_link);
    espconn_gethostbyname(&private_key_conn, host, &private_key_ip, private_key_dns_done);
}

void ICACHE_FLASH_ATTR update_aws_ca_cert(char *bin_link)
{
    build_ca_cert_payload(bin_link);
    espconn_gethostbyname(&ca_cert_conn, host, &ca_cert_ip, ca_cert_dns_done);
}

void ICACHE_FLASH_ATTR update_aws_certificates(char *cert_json)
{
    char *token;
    char *url;
    char *mac;
    char *private_key_token, *ca_cert_token;
    char temp_cert_json[os_strlen(cert_json) + 1];
    os_strcpy(temp_cert_json, cert_json);

    // private key
    token = split(cert_json, "\"device\"");
    // DEBUG_INFO("token [device]: %s", token);
    if (token != NULL)
    {
        url = split(token, "\"bin\": \"");
        if (url == NULL)
        {
            ERROR_INFO("private_key | missing \"url\"");
            return;
        }
        token = split(url, "\"");
        if (token == NULL)
        {
            ERROR_INFO("private_key | missing \"token\"");
            return;
        }

        if (validate_aws_url(url) == FALSE)
            return;

        private_key_token = split(token, "\"sha256\": \"");
        if (private_key_token == NULL)
        {
            ERROR_INFO("private_key | missing \"private_key_token\"");
            return;
        }
        mac = split(private_key_token, "\"");
        if (mac == NULL)
        {
            ERROR_INFO("private_key | missing \"mac\"");
            return;
        }
        os_strcpy(private_key_mac_json, private_key_token);
        DEBUG_INFO("private_key");
        DEBUG_INFO("url: %s", url);
        DEBUG_INFO("mac: %s", private_key_mac_json);
        update_aws_private_key(url);
    }
    else
        ERROR_INFO("\"device\" missing");

    // DEBUG_INFO("");
    // DEBUG_INFO("temp_cert_json: %s", temp_cert_json);

    // ca_cert
    token = split(temp_cert_json, "\"ca\"");
    // DEBUG_INFO("token [ca]: %s", token);
    if (token != NULL)
    {
        url = split(token, "\"bin\": \"");
        if (url == NULL)
        {
            ERROR_INFO("ca_cert | missing \"url\"");
            return;
        }
        token = split(url, "\"");
        if (token == NULL)
        {
            ERROR_INFO("ca_cert | missing \"token\"");
            return;
        }

        // if (validate_aws_url(url) == FALSE)
        //     return;

        ca_cert_token = split(token, "\"sha256\": \"");
        if (ca_cert_token == NULL)
        {
            ERROR_INFO("ca_cert | missing \"ca_cert_token\"");
            return;
        }
        mac = split(ca_cert_token, "\"");
        if (mac == NULL)
        {
            ERROR_INFO("ca_cert | missing \"mac\"");
            return;
        }
        os_strcpy(ca_cert_mac_json, ca_cert_token);
        DEBUG_INFO("ca_cert");
        DEBUG_INFO("url: %s", url);
        DEBUG_INFO("mac: %s", ca_cert_mac_json);
        update_aws_ca_cert(url);
    }
    else
        ERROR_INFO("\"ca\" missing");
}

uint16_t ICACHE_FLASH_ATTR update_roll_curr_perc(bool save_flag)
{
    uint16_t perc_done = 0;
    float real_curr_perc = 0;
    int trunc_curr_perc = 0;
    uint8_t precision = 2;
    char float_buff[16];

    DEBUG_INFO("");
    DEBUG_INFO("dev_conf->m_rise_steps_curr_pos: %d | dev_conf->m_rise_steps: %d", dev_conf->m_rise_steps_curr_pos, dev_conf->m_rise_steps);
    DEBUG_INFO("dev_conf->m_fall_steps_curr_pos: %d | dev_conf->m_fall_steps: %d", dev_conf->m_fall_steps_curr_pos, dev_conf->m_fall_steps);
    DEBUG_INFO("");

    if (going_up)
    {
        real_curr_perc = (float) (dev_conf->m_rise_steps_curr_pos*100.0)/(dev_conf->m_rise_steps);
        print_float(real_curr_perc, float_buff, precision);
        trunc_curr_perc = my_round(real_curr_perc);
        trunc_curr_perc = (trunc_curr_perc > 100) ? 100 : trunc_curr_perc;
        trunc_curr_perc = (trunc_curr_perc < 0) ? 0 : trunc_curr_perc;
        DEBUG_INFO("RISING | real_curr_perc: %s | trunc_curr_perc: %d", float_buff, trunc_curr_perc);
        dev_conf->m_roll_curr_perc = trunc_curr_perc;
        dev_conf->m_fall_steps_curr_pos = my_round(dev_conf->m_rise_steps_curr_pos/rise_fall_ratio);
    }
    else if (going_down)
    {
        real_curr_perc = (float) (dev_conf->m_fall_steps_curr_pos*100.0)/(dev_conf->m_fall_steps);
        print_float(real_curr_perc, float_buff, precision);
        trunc_curr_perc = my_round(real_curr_perc);
        trunc_curr_perc = (trunc_curr_perc > 100) ? 100 : trunc_curr_perc;
        trunc_curr_perc = (trunc_curr_perc < 0) ? 0 : trunc_curr_perc;
        DEBUG_INFO("FALLING | real_curr_perc: %s | trunc_curr_perc: %d", float_buff, trunc_curr_perc);
        dev_conf->m_roll_curr_perc = trunc_curr_perc;
        dev_conf->m_rise_steps_curr_pos = my_round(dev_conf->m_fall_steps_curr_pos*rise_fall_ratio);
    }

    dev_conf->m_roll_currval = (dev_conf->m_roll_curr_perc*dev_conf->m_roll_totlen)/100;
    DEBUG_INFO("update_roll_curr_perc | dev_conf->m_roll_totlen: %d", dev_conf->m_roll_totlen);

    if (save_flag)
        Nvm_Mng_Save();

    roll_curr_perc = dev_conf->m_roll_curr_perc;
    return roll_curr_perc;
}

void ICACHE_FLASH_ATTR reboot_procedure(void)
{
    static bool asked_for_disconnection = FALSE;

    if (disconnected_for_reboot)    // did you receive the PUBACK for the disconnection message
    {
        restartable = FALSE;    
        if (ap_mode_reboot_required)
        {
            dev_conf->m_pam_mode = PAM_DISABLE;
            dev_conf->m_boot_mode = CONFIG_BOOTMODE_CONFIG;
            dev_conf->m_cfg_holder = 29;
            dev_conf->m_rebootreason = RR_AP_MODE;
        }
        real_NVM_save();
        system_restart();           // then restart
        return;
    }
    if (asked_for_disconnection == FALSE)   // did you signal that the next queue msg is the disconnection one?
    {
        asked_for_disconnection = TRUE;
        next_is_disconn_msg = TRUE;     // signal it
        if (ap_mode_reboot_required)
        {
            DEBUG_INFO("asked_for_disconnection FALSE | ap_mode_reboot_required");
            os_sprintf(mqtt_msg, "{ \"state\": { \"reported\": { \"connected\": false, \"configMode\": true, \"clientId\": \"%s\", \"sessionId\": \"%s\", \"isLwt\": false } } }", mqtt_client_id, session_id);
        }
        else
        {
            DEBUG_INFO("asked_for_disconnection FALSE");
            os_sprintf(mqtt_msg, "{ \"state\": { \"reported\": { \"connected\": false, \"clientId\": \"%s\", \"sessionId\": \"%s\", \"isLwt\": false } } }", mqtt_client_id, session_id);
        }
        publish_on_topic("status/update", mqtt_msg);
    }
}

/*
 * Name:    void NetworkReq_Mng_Hdlr(void)
 * Descr:   Network Request manager handler
 */

void ICACHE_FLASH_ATTR NetworkReq_Mng_Hdlr(void)
{
    if (NetworkReq_Mng_Data == NULL)
        return;

    t_Mqtt_Mng_Data_Msg *p_msg;
    uint8 received_command = 255;

    if (ca_cert_successful && private_key_successful)
    {
        DEBUG_INFO("ca_cert_successful && private_key_successful");
        dev_conf->m_rebootreason = RR_CERT_UPDATE;
        Nvm_Mng_Save();
        
        ca_cert_successful = FALSE;
        private_key_successful = FALSE;

        restartable = true;
    }

    if (restartable)        // do you need a restart?
    {
        if (is_roller())    // are you a roller?
        {
            if (stopped)    // are you still?
            {
                reboot_procedure(); // send the disconnection message and reboot
            }
        }
        else
        {
            reboot_procedure();
        }
    }
    else if (network_feedback)
    {
        if (times)
            config_feedback(times, TRUE);
    }
    else
    {
        if (mqtt_queue_is_empty() == FALSE)
        {
            p_msg = mqtt_queue_get_msg();

            NetworkReq_Mng_Data->m_topicbuf = (char*)os_zalloc(p_msg->m_msgtopiclen+1);
            NetworkReq_Mng_Data->m_databuf = (char*)os_zalloc(p_msg->m_msgdatalen+1);
            NetworkReq_Mng_Data->m_ansbuf = (char*)os_zalloc(256);

            os_strncpy(NetworkReq_Mng_Data->m_topicbuf, p_msg->m_msgtopic, p_msg->m_msgtopiclen);
            os_strncpy(NetworkReq_Mng_Data->m_databuf, p_msg->m_msgdata, p_msg->m_msgdatalen);

            Mqtt_Mng_SetDataAsRead();
            DEBUG_INFO("Receive topic: %s, data: %s", NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_databuf);
            if (is_switch())
            {
                strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
                strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
                strcat(NetworkReq_Mng_Data->m_topicapp, "/sw1/cmd");
                if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
                {
                    switch (NetworkReq_Mng_Data->m_databuf[0])
                    {
                        case '?':
                            int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L1));
                            publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                            break;
                        if (light_turning_timer[SW1].expired == TRUE)
                        {
                            case 'r':
                                GPIOMng_SetPinState(L1, !GPIOMng_GetPinState(L1));
                                arm_timer(light_turning_timer + SW1);
                                dev_conf->m_statuspin[L1] = GPIOMng_GetPinState(L1);
                                Nvm_Mng_Save();
                                int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L1));
                                publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                                break;
                            default:
                                received_command = NetworkReq_Mng_Data->m_databuf[0] - 48;   // char to num [48 = '0']
                                if ((received_command == 0) || (received_command == 1))
                                {
                                    GPIOMng_SetPinState(L1, received_command);
                                    arm_timer(light_turning_timer + SW1);
                                    dev_conf->m_statuspin[L1] = GPIOMng_GetPinState(L1);
                                    Nvm_Mng_Save();
                                    int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L1));
                                    publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                                }
                                break;
                        }
                    }
                }

                strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
                strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
                strcat(NetworkReq_Mng_Data->m_topicapp, "/sw2/cmd");
                if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
                {
                    switch (NetworkReq_Mng_Data->m_databuf[0])
                    {
                        case '?':
                            int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L2));
                            publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                            break;
                        if (light_turning_timer[SW2].expired == TRUE)
                        {
                            case 'r':
                                GPIOMng_SetPinState(L2, !GPIOMng_GetPinState(L2));
                                arm_timer(light_turning_timer + SW2);
                                dev_conf->m_statuspin[L2] = GPIOMng_GetPinState(L2);
                                Nvm_Mng_Save();
                                int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L2));
                                publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                                break;
                            default:
                                received_command = NetworkReq_Mng_Data->m_databuf[0] - 48;   // char to num [48 = '0']
                                if ((received_command == 0) || (received_command == 1))
                                {
                                    GPIOMng_SetPinState(L2, received_command);
                                    arm_timer(light_turning_timer + SW2);
                                    dev_conf->m_statuspin[L2] = GPIOMng_GetPinState(L2);
                                    Nvm_Mng_Save();
                                    int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L2));
                                    publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                                }
                                break;
                        }
                    }
                }

                strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
                strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
                strcat(NetworkReq_Mng_Data->m_topicapp, "/all/cmd");
                if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
                {
                    switch (NetworkReq_Mng_Data->m_databuf[0])
                    {
                        case '?':
                            int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L1));
                            publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                            int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L2));
                            publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                            break;
                        case 'r':
                            if (light_turning_timer[SW1].expired == TRUE)
                            {
                                GPIOMng_SetPinState(L1, !GPIOMng_GetPinState(L1));
                                arm_timer(light_turning_timer + SW1);
                                dev_conf->m_statuspin[L1] = GPIOMng_GetPinState(L1);
                                int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L1));
                                publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                            }
                            if (light_turning_timer[SW2].expired == TRUE)
                            {
                                GPIOMng_SetPinState(L2, !GPIOMng_GetPinState(L2));
                                arm_timer(light_turning_timer + SW2);
                                dev_conf->m_statuspin[L2] = GPIOMng_GetPinState(L2);
                                Nvm_Mng_Save();
                                int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L2));
                                publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                            }
                            break;
                        default:
                            received_command = NetworkReq_Mng_Data->m_databuf[0] - 48;   // char to num [48 = '0']
                            if ((received_command == 0) || (received_command == 1))
                            {
                                if (light_turning_timer[SW1].expired == TRUE)
                                {
                                    GPIOMng_SetPinState(L1, received_command);
                                    arm_timer(light_turning_timer + SW1);
                                    dev_conf->m_statuspin[L1] = GPIOMng_GetPinState(L1);
                                    int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L1));
                                    publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                                }
                                if (light_turning_timer[SW2].expired == TRUE)
                                {
                                    GPIOMng_SetPinState(L2, received_command);
                                    arm_timer(light_turning_timer + SW2);
                                    dev_conf->m_statuspin[L2] = GPIOMng_GetPinState(L2);
                                    int_to_char(NetworkReq_Mng_Data->m_ansbuf, GPIOMng_GetPinState(L2));
                                    publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                                }

                                Nvm_Mng_Save();
                            }
                            break;
                    }
                }
            }
            else if (is_toggle() || is_latched())
            {
                strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
                strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
                strcat(NetworkReq_Mng_Data->m_topicapp, "/sw1/cmd");
                if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
                {
                    if (NetworkReq_Mng_Data->m_databuf[0] == '?')
                    {
                        int_to_char(NetworkReq_Mng_Data->m_ansbuf, dev_conf->m_relecurrstatus_ch1);
                        publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                    }
                    else if ((dev_conf->m_numberstatus_ch1 == 2) && (NetworkReq_Mng_Data->m_databuf[0] == 'r'))
                    {
                        dev_conf->m_relecurrstatus_ch1 = !dev_conf->m_relecurrstatus_ch1;
                        int_to_char(NetworkReq_Mng_Data->m_ansbuf, dev_conf->m_relecurrstatus_ch1);
                        publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                        Nvm_Mng_Save();
                    }
                    else if (light_turning_timer[SW1].expired == TRUE)
                    {
                        uint8_t to_reach = atoi(NetworkReq_Mng_Data->m_databuf);

                        if (to_reach != dev_conf->m_relecurrstatus_ch1)
                        {
                            if (dev_conf->m_relay[0] == FALSE)
                            {
                                GPIOMng_SetPinState(L1, to_reach);
                                arm_timer(light_turning_timer + SW1);
                                dev_conf->m_relecurrstatus_ch1 = (dev_conf->m_relecurrstatus_ch1 + 1)%dev_conf->m_numberstatus_ch1;
                                Nvm_Mng_Save();
                                /* publish the new state */
                                char msg[8];
                                int_to_char(msg, dev_conf->m_relecurrstatus_ch1);
                                publish_on_topic("sw1/alert", msg);
                                GPIOMng_RstPinStsChanged(SW1);
                            }
                            else
                            {
                                if ((to_reach < dev_conf->m_numberstatus_ch1) && (to_reach != dev_conf->m_relecurrstatus_ch1))
                                {
                                    if (to_reach == 0)
                                        NetworkReq_Mng_Data->m_numitertodo_ch1 = dev_conf->m_numberstatus_ch1 - dev_conf->m_relecurrstatus_ch1;
                                    else if (to_reach > dev_conf->m_relecurrstatus_ch1)
                                        NetworkReq_Mng_Data->m_numitertodo_ch1 = to_reach - dev_conf->m_relecurrstatus_ch1;
                                    else
                                        NetworkReq_Mng_Data->m_numitertodo_ch1 = to_reach - dev_conf->m_relecurrstatus_ch1 + dev_conf->m_numberstatus_ch1;

                                    NetworkReq_Mng_Data->m_seqtmr_ch1 = 0;
                                    NetworkReq_Mng_Data->m_numitertodo_ch1--;
                                    GPIOMng_SetPinState(L1, TRUE);
                                    arm_timer(light_turning_timer + SW1);
                                    dev_conf->m_relecurrstatus_ch1 = (dev_conf->m_relecurrstatus_ch1 + 1)%(dev_conf->m_numberstatus_ch1);
                                    publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_databuf);
                                    Nvm_Mng_Save();
                                    GPIOMng_RstPinStsChanged(SW1);
                                }
                            }
                        }
                    }
                }

                strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
                strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
                strcat(NetworkReq_Mng_Data->m_topicapp, "/sw2/cmd");
                if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
                {
                    if (NetworkReq_Mng_Data->m_databuf[0] == '?')
                    {
                        int_to_char(NetworkReq_Mng_Data->m_ansbuf, dev_conf->m_relecurrstatus_ch2);
                        publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                    }
                    else if ((dev_conf->m_numberstatus_ch2 == 2) && (NetworkReq_Mng_Data->m_databuf[0] == 'r'))
                    {
                        dev_conf->m_relecurrstatus_ch2 = !dev_conf->m_relecurrstatus_ch2;
                        int_to_char(NetworkReq_Mng_Data->m_ansbuf, dev_conf->m_relecurrstatus_ch2);
                        publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                        Nvm_Mng_Save();
                    }
                    else if (light_turning_timer[SW2].expired == TRUE)
                    {
                        uint8_t to_reach = atoi(NetworkReq_Mng_Data->m_databuf);

                        if (to_reach != dev_conf->m_relecurrstatus_ch2)
                        {
                            if (dev_conf->m_relay[1] == FALSE)
                            {
                                GPIOMng_SetPinState(L2, to_reach);
                                arm_timer(light_turning_timer + SW2);
                                dev_conf->m_relecurrstatus_ch2 = (dev_conf->m_relecurrstatus_ch2 + 1)%dev_conf->m_numberstatus_ch2;
                                Nvm_Mng_Save();
                                /* publish the new state */
                                char msg[8];
                                int_to_char(msg, dev_conf->m_relecurrstatus_ch2);
                                publish_on_topic("sw2/alert", msg);
                                GPIOMng_RstPinStsChanged(SW2);
                            }
                            else
                            {
                                if ((to_reach < dev_conf->m_numberstatus_ch2) && (to_reach != dev_conf->m_relecurrstatus_ch2))
                                {
                                    if (to_reach == 0)
                                        NetworkReq_Mng_Data->m_numitertodo_ch2 = dev_conf->m_numberstatus_ch2 - dev_conf->m_relecurrstatus_ch2;
                                    else if (to_reach > dev_conf->m_relecurrstatus_ch2)
                                        NetworkReq_Mng_Data->m_numitertodo_ch2 = to_reach - dev_conf->m_relecurrstatus_ch2;
                                    else
                                        NetworkReq_Mng_Data->m_numitertodo_ch2 = to_reach - dev_conf->m_relecurrstatus_ch2 + dev_conf->m_numberstatus_ch2;

                                    NetworkReq_Mng_Data->m_seqtmr_ch2 = 0;
                                    NetworkReq_Mng_Data->m_numitertodo_ch2--;
                                    GPIOMng_SetPinState(L2, TRUE);
                                    arm_timer(light_turning_timer + SW2);
                                    dev_conf->m_relecurrstatus_ch2 = (dev_conf->m_relecurrstatus_ch2 + 1)%(dev_conf->m_numberstatus_ch2);
                                    publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_databuf);
                                    Nvm_Mng_Save();
                                    GPIOMng_RstPinStsChanged(SW2);
                                }
                            }
                        }
                    }
                }

                strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
                strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
                strcat(NetworkReq_Mng_Data->m_topicapp, "/all/cmd");
                if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
                {
                    if (NetworkReq_Mng_Data->m_databuf[0] == '?')
                    {
                        int_to_char(NetworkReq_Mng_Data->m_ansbuf, dev_conf->m_relecurrstatus_ch1);
                        publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                        int_to_char(NetworkReq_Mng_Data->m_ansbuf, dev_conf->m_relecurrstatus_ch2);
                        publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                    }
                    else
                    {
                        uint8_t to_reach = atoi(NetworkReq_Mng_Data->m_databuf);

                        // SW1
                        if ((dev_conf->m_numberstatus_ch1 == 2) && (NetworkReq_Mng_Data->m_databuf[0] == 'r'))
                        {
                            dev_conf->m_relecurrstatus_ch1 = !dev_conf->m_relecurrstatus_ch1;
                            int_to_char(NetworkReq_Mng_Data->m_ansbuf, dev_conf->m_relecurrstatus_ch1);
                            publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                        }
                        else if (light_turning_timer[SW1].expired == TRUE)
                        {
                            if (to_reach != dev_conf->m_relecurrstatus_ch1)
                            {
                                if (dev_conf->m_relay[0] == FALSE)
                                {
                                    GPIOMng_SetPinState(L1, to_reach);
                                    arm_timer(light_turning_timer + SW1);
                                    dev_conf->m_relecurrstatus_ch1 = (dev_conf->m_relecurrstatus_ch1 + 1)%dev_conf->m_numberstatus_ch1;
                                    /* publish the new state */
                                    char msg[8];
                                    int_to_char(msg, dev_conf->m_relecurrstatus_ch1);
                                    publish_on_topic("sw1/alert", msg);
                                    GPIOMng_RstPinStsChanged(SW1);
                                }
                                else
                                {
                                    if (to_reach < dev_conf->m_numberstatus_ch1)
                                    {
                                        if (to_reach == 0)
                                            NetworkReq_Mng_Data->m_numitertodo_ch1 = dev_conf->m_numberstatus_ch1 - dev_conf->m_relecurrstatus_ch1;
                                        else if (to_reach > dev_conf->m_relecurrstatus_ch1)
                                            NetworkReq_Mng_Data->m_numitertodo_ch1 = to_reach - dev_conf->m_relecurrstatus_ch1;
                                        else
                                            NetworkReq_Mng_Data->m_numitertodo_ch1 = to_reach - dev_conf->m_relecurrstatus_ch1 + dev_conf->m_numberstatus_ch1;

                                        NetworkReq_Mng_Data->m_seqtmr_ch1 = 0;
                                        NetworkReq_Mng_Data->m_numitertodo_ch1--;
                                        GPIOMng_SetPinState(L1, TRUE);
                                        arm_timer(light_turning_timer + SW1);
                                        dev_conf->m_relecurrstatus_ch1 = (dev_conf->m_relecurrstatus_ch1 + 1)%(dev_conf->m_numberstatus_ch1);
                                        publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_databuf);
                                        GPIOMng_RstPinStsChanged(SW1);
                                    }
                                }
                            }
                        }
                        // SW2
                        if ((dev_conf->m_numberstatus_ch2 == 2) && (NetworkReq_Mng_Data->m_databuf[0] == 'r'))
                        {
                            dev_conf->m_relecurrstatus_ch2 = !dev_conf->m_relecurrstatus_ch2;
                            int_to_char(NetworkReq_Mng_Data->m_ansbuf, dev_conf->m_relecurrstatus_ch2);
                            publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                        }
                        else if (light_turning_timer[SW2].expired == TRUE)
                        {
                            if (to_reach != dev_conf->m_relecurrstatus_ch2)
                            {
                                if (dev_conf->m_relay[1] == FALSE)
                                {
                                    GPIOMng_SetPinState(L2, to_reach);
                                    arm_timer(light_turning_timer + SW2);
                                    dev_conf->m_relecurrstatus_ch2 = (dev_conf->m_relecurrstatus_ch2 + 1)%dev_conf->m_numberstatus_ch2;
                                    Nvm_Mng_Save();
                                    /* publish the new state */
                                    char msg[8];
                                    int_to_char(msg, dev_conf->m_relecurrstatus_ch2);
                                    publish_on_topic("sw2/alert", msg);
                                    GPIOMng_RstPinStsChanged(SW2);
                                }
                                else
                                {
                                    if (to_reach < dev_conf->m_numberstatus_ch2)
                                    {
                                        if (to_reach == 0)
                                            NetworkReq_Mng_Data->m_numitertodo_ch2 = dev_conf->m_numberstatus_ch2 - dev_conf->m_relecurrstatus_ch2;
                                        else if (to_reach > dev_conf->m_relecurrstatus_ch2)
                                            NetworkReq_Mng_Data->m_numitertodo_ch2 = to_reach - dev_conf->m_relecurrstatus_ch2;
                                        else
                                            NetworkReq_Mng_Data->m_numitertodo_ch2 = to_reach - dev_conf->m_relecurrstatus_ch2 + dev_conf->m_numberstatus_ch2;

                                        NetworkReq_Mng_Data->m_seqtmr_ch2 = 0;
                                        NetworkReq_Mng_Data->m_numitertodo_ch2--;
                                        GPIOMng_SetPinState(L2, TRUE);
                                        arm_timer(light_turning_timer + SW2);
                                        dev_conf->m_relecurrstatus_ch2 = (dev_conf->m_relecurrstatus_ch2 + 1)%(dev_conf->m_numberstatus_ch2);
                                        publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_databuf);
                                        GPIOMng_RstPinStsChanged(SW2);
                                    }
                                }
                            }
                        }

                        Nvm_Mng_Save();
                    }
                }
            }
            else if (is_roller())
            {
                /* Create cmd topic for comparing on sw1*/
                strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
                strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
                strcat(NetworkReq_Mng_Data->m_topicapp, "/rb/cmd");

                /* Has a slider info received? */
                if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
                {
                    /* Prepare mqtt packet with the current arrival percentual */
                    roll_curr_perc = update_roll_curr_perc(FALSE);

                    /* Data is "question mark" */
                    if (NetworkReq_Mng_Data->m_databuf[0] == '?')
                    {
                        /* Prepare the topic */
                        os_sprintf(NetworkReq_Mng_Data->m_ansbuf, "%d:%d", roll_curr_perc, roll_curr_perc);
                        publish_on_topic("rb/alert", NetworkReq_Mng_Data->m_ansbuf);
                    }
                    else
                    {
                        /* If data is not question mark, actuate the command */
                        int final_perc_temp = atoi(NetworkReq_Mng_Data->m_databuf);
                        // only accept percentages in the range [0, 100]
                        if (!is_in_range(final_perc_temp, 0, 100))
                        {
                            ERROR_INFO("final_perc_temp (%d) not in range [0, 100]!", final_perc_temp);
                            return;
                        }

                        roller_network_command = TRUE;

                        if (final_perc_temp > roll_curr_perc)
                            GPIOMng_PinUnlock(SW2);
                        else
                            GPIOMng_PinUnlock(SW1);

                        if (stopped == FALSE)
                        {
                            if (turning_timer.expired)
                            {
                                if (going_up == TRUE)
                                {
                                    if (final_perc_temp < roll_curr_perc)
                                    {
                                        ERROR_INFO("Was going_up but requested to go down!");
                                        stop_or_damp();
                                    }
                                    else
                                    {
                                        DEBUG_INFO("Already going_up");
                                    }
                                }
                                else
                                {
                                    if (final_perc_temp > roll_curr_perc)
                                    {
                                        ERROR_INFO("Was going_down but requested to go up!");
                                        stop_or_damp();
                                    }
                                    else
                                    {
                                        DEBUG_INFO("Already going_down");
                                    }
                                }
                            }
                        }

                        roll_final_perc = (final_perc_temp > dev_conf->m_roll_totlen ? dev_conf->m_roll_totlen : final_perc_temp);
                        rise_steps_to_reach = roll_final_perc*dev_conf->m_rise_steps/100;
                        fall_steps_to_reach = roll_final_perc*dev_conf->m_fall_steps/100;
                        arm_roller_timer();
                        DEBUG_INFO("roll_final_perc: %d | rise_steps_to_reach: %d | fall_steps_to_reach: %d", roll_final_perc, rise_steps_to_reach, fall_steps_to_reach);

                        // need to STAND STILL
                        if (rise_steps_to_reach == dev_conf->m_rise_steps_curr_pos)
                            RollerBlind.m_rollactreq = FALSE;
                        if (fall_steps_to_reach == dev_conf->m_fall_steps_curr_pos)
                            RollerBlind.m_rollactreq = FALSE;

                        // need to FALL
                        if (fall_steps_to_reach < dev_conf->m_fall_steps_curr_pos)
                        {
                            ERROR_INFO("FALLING");
                            roller_steps_to_publish = roller_compute_steps_to_publish(false);
                            going_up = FALSE;
                            going_down = TRUE;
                            stopped = FALSE;
                            RollerBlind.m_rollactreq = TRUE;
                        }
                        // need to RISE
                        else if (rise_steps_to_reach > dev_conf->m_rise_steps_curr_pos)
                        {
                            ERROR_INFO("RISING");
                            roller_steps_to_publish = roller_compute_steps_to_publish(true);
                            going_up = TRUE;
                            going_down = FALSE;
                            stopped = FALSE;
                            RollerBlind.m_rollactreq = TRUE;
                        }
                    }
                }
            }
            else if (is_dimmer())
            {
                strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
                strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
                strcat(NetworkReq_Mng_Data->m_topicapp, "/sw1/cmd");
                if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
                {
                    char *perc = NULL;

                    switch (NetworkReq_Mng_Data->m_databuf[0])
                    {
                        case '?':
                            if (dev_conf->m_dimmer_logic_state[0] == 0)
                                int_to_char(NetworkReq_Mng_Data->m_ansbuf, 0);
                            else
                                os_sprintf(NetworkReq_Mng_Data->m_ansbuf, "1:%d", dev_conf->m_dimming_perc[0]);
                            publish_on_topic("sw1/alert", NetworkReq_Mng_Data->m_ansbuf);
                            break;
                        default:
                            perc = split(NetworkReq_Mng_Data->m_databuf, ":");
                            dimmer_mqtt_on_off[0] = (uint8_t) strtoul(NetworkReq_Mng_Data->m_databuf, NULL, 10);

                            if (perc != NULL)
                            {
                                uint8_t dimmer_numeric_perc = (uint8_t) strtoul(perc, NULL, 10);
                                if (!is_in_range(dimmer_numeric_perc, DIMMER_MIN_PERCENTAGE, DIMMER_MAX_PERCENTAGE))
                                {
                                    ERROR_INFO("Dimmer percentage (%d) out of range [%d, %d]", dimmer_numeric_perc, DIMMER_MIN_PERCENTAGE, DIMMER_MAX_PERCENTAGE);
                                    return;
                                }
                                if (dimmer_mqtt_on_off[0] == 0)
                                 {
                                    if (dev_conf->m_dimmer_logic_state[0] == 1)
                                     {
                                        arm_timer(dimmer_mqtt_shortpress_timer + 0);
                                        if (dimmer_curr_state[0] != DIMMER_IDLE)
                                        {
                                            dimmer_curr_state[0] = DIMMER_MQTT_PRE_SHORTPRESS;
                                            dimmer_direction[0] = !dimmer_direction[0];
                                            dimmer_pression_timer[0].expired = TRUE;
                                        }
                                        else
                                            dimmer_curr_state[0] = DIMMER_MQTT_SHORTPRESS;
                                        dimmer_curr_perc[0] = update_dimmer_curr_perc(0, FALSE);
                                     }
                                    break;
                                }

                                dimmer_mqtt_final_perc[0] = dimmer_numeric_perc;
                                dimmer_curr_perc[0] = update_dimmer_curr_perc(0, FALSE);

                                if (dimmer_mqtt_on_off[0] == 0 || dimmer_mqtt_on_off[0] == 1)
                                {
                                    if (dev_conf->m_dimmer_logic_state[0] == LOW)
                                    {
                                        if (dimmer_mqtt_final_perc[0] == 0)
                                        {
                                            dimmer_final_perc[0] = 1;
                                            dimmer_delta_perc[0] = abs(dimmer_final_perc[0] - dimmer_curr_perc[0]);
                                            dimmer_steps_to_do[0] = max((dimmer_delta_perc[0]*dev_conf->m_dimming_steps[0])/100, DIMMER_MIN_LONGPRESS_TIME/CYCLE_TIME);
                                            dimmer_steps_done[0] = 0;
                                            dimmer_curr_state[0] = DIMMER_COUNTING;
                                        }

                                        dimmer_curr_perc[0] = 0;
                                        dev_conf->m_dimming_perc[0] = 0;
                                    }

                                    DEBUG_INFO("dimmer_mqtt_on_off[0]: %d | dimmer_mqtt_final_perc[0]: %d", dimmer_mqtt_on_off[0], dimmer_mqtt_final_perc[0]);
                                    DEBUG_INFO("dimmer_curr_perc[0]: %d | dimmer_direction[0]: %s", dimmer_curr_perc[0], dimmer_direction[0] == HIGH ? "HIGH" : "LOW");

                                    if ((dimmer_mqtt_final_perc[0] > dimmer_curr_perc[0]) && (dimmer_direction[0] == LOW))
                                    {
                                        arm_timer(dimmer_mqtt_inversion_timer + 0);
                                        dimmer_curr_state[0] = DIMMER_MQTT_DIRECTION_INVERSION;
                                    }
                                    else if ((dimmer_mqtt_final_perc[0] < dimmer_curr_perc[0]) && (dimmer_direction[0] == HIGH))
                                    {
                                        arm_timer(dimmer_mqtt_inversion_timer + 0);
                                        dimmer_curr_state[0] = DIMMER_MQTT_DIRECTION_INVERSION;
                                    }
                                    else if (dimmer_mqtt_final_perc[0] != dimmer_curr_perc[0])
                                    {
                                        dimmer_final_perc[0] = dimmer_mqtt_final_perc[0];
                                        dimmer_delta_perc[0] = abs(dimmer_final_perc[0] - dimmer_curr_perc[0]);
                                        dimmer_steps_to_do[0] = max((dimmer_delta_perc[0]*dev_conf->m_dimming_steps[0])/100, DIMMER_MIN_LONGPRESS_TIME/CYCLE_TIME);
                                        dimmer_steps_done[0] = 0;
                                        dimmer_curr_state[0] = DIMMER_COUNTING;
                                    }
                                }
                            }
                            else
                            {
                                if (dimmer_mqtt_on_off[0] == 0)
                                 {
                                    if (dev_conf->m_dimmer_logic_state[0] == 0)
                                        break;
                                    else
                                    {
                                        arm_timer(dimmer_mqtt_shortpress_timer + 0);
                                        if (dimmer_curr_state[0] != DIMMER_IDLE)
                                        {
                                            dimmer_curr_state[0] = DIMMER_MQTT_PRE_SHORTPRESS;
                                            dimmer_direction[0] = !dimmer_direction[0];
                                            dimmer_pression_timer[0].expired = TRUE;
                                        }
                                        else
                                            dimmer_curr_state[0] = DIMMER_MQTT_SHORTPRESS;
                                    }
                                }
                                if (dimmer_mqtt_on_off[0] == 1)
                                {
                                    if (dev_conf->m_dimmer_logic_state[0] == 1)
                                        break;
                                    else
                                    {
                                        arm_timer(dimmer_mqtt_shortpress_timer + 0);
                                        dimmer_curr_state[0] = DIMMER_MQTT_SHORTPRESS;
                                    }
                                }
                            }
                            break;
                    }
                }

                /* SW2 */
                strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
                strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
                strcat(NetworkReq_Mng_Data->m_topicapp, "/sw2/cmd");
                if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
                {
                    char *perc = NULL;

                    switch (NetworkReq_Mng_Data->m_databuf[1])
                    {
                        case '?':
                            if (dev_conf->m_dimmer_logic_state[1] == 0)
                                int_to_char(NetworkReq_Mng_Data->m_ansbuf, 0);
                            else
                                os_sprintf(NetworkReq_Mng_Data->m_ansbuf, "1:%d", dev_conf->m_dimming_perc[1]);
                            publish_on_topic("sw2/alert", NetworkReq_Mng_Data->m_ansbuf);
                            break;
                        default:
                            perc = split(NetworkReq_Mng_Data->m_databuf, ":");
                            dimmer_mqtt_on_off[1] = (uint8_t) strtoul(NetworkReq_Mng_Data->m_databuf, NULL, 10);

                            if (perc != NULL)
                            {
                                uint8_t dimmer_numeric_perc = (uint8_t) strtoul(perc, NULL, 10);
                                if (!is_in_range(dimmer_numeric_perc, DIMMER_MIN_PERCENTAGE, DIMMER_MAX_PERCENTAGE))
                                {
                                    ERROR_INFO("Dimmer percentage (%d) out of range [%d, %d]", dimmer_numeric_perc, DIMMER_MIN_PERCENTAGE, DIMMER_MAX_PERCENTAGE);
                                    return;
                                }
                                if (dimmer_mqtt_on_off[1] == 0)
                                 {
                                    if (dev_conf->m_dimmer_logic_state[1] == 1)
                                     {
                                        arm_timer(dimmer_mqtt_shortpress_timer + 1);
                                        if (dimmer_curr_state[1] != DIMMER_IDLE)
                                        {
                                            dimmer_curr_state[1] = DIMMER_MQTT_PRE_SHORTPRESS;
                                            dimmer_direction[1] = !dimmer_direction[1];
                                            dimmer_pression_timer[1].expired = TRUE;
                                        }
                                        else
                                            dimmer_curr_state[1] = DIMMER_MQTT_SHORTPRESS;
                                        dimmer_curr_perc[1] = update_dimmer_curr_perc(0, FALSE);
                                     }
                                    break;
                                }

                                dimmer_mqtt_final_perc[1] = dimmer_numeric_perc;
                                dimmer_curr_perc[1] = update_dimmer_curr_perc(1, FALSE);

                                if (dimmer_mqtt_on_off[1] == 0 || dimmer_mqtt_on_off[1] == 1)
                                {
                                    if (dev_conf->m_dimmer_logic_state[1] == LOW)
                                    {
                                        if (dimmer_mqtt_final_perc[1] == 0)
                                        {
                                            dimmer_final_perc[1] = 1;
                                            dimmer_delta_perc[1] = abs(dimmer_final_perc[1] - dimmer_curr_perc[1]);
                                            dimmer_steps_to_do[1] = max((dimmer_delta_perc[1]*dev_conf->m_dimming_steps[1])/100, DIMMER_MIN_LONGPRESS_TIME/CYCLE_TIME);
                                            dimmer_steps_done[1] = 0;
                                            dimmer_curr_state[1] = DIMMER_COUNTING;
                                        }

                                        dimmer_curr_perc[1] = 0;
                                        dev_conf->m_dimming_perc[1] = 0;
                                    }

                                    DEBUG_INFO("dimmer_mqtt_on_off[1]: %d | dimmer_mqtt_final_perc[1]: %d", dimmer_mqtt_on_off[1], dimmer_mqtt_final_perc[1]);
                                    DEBUG_INFO("dimmer_curr_perc[1]: %d | dimmer_direction[1]: %s", dimmer_curr_perc[1], dimmer_direction[1] == HIGH ? "HIGH" : "LOW");

                                    if ((dimmer_mqtt_final_perc[1] > dimmer_curr_perc[1]) && (dimmer_direction[1] == LOW))
                                    {
                                        arm_timer(dimmer_mqtt_inversion_timer + 1);
                                        dimmer_curr_state[1] = DIMMER_MQTT_DIRECTION_INVERSION;
                                    }
                                    else if ((dimmer_mqtt_final_perc[1] < dimmer_curr_perc[1]) && (dimmer_direction[1] == HIGH))
                                    {
                                        arm_timer(dimmer_mqtt_inversion_timer + 1);
                                        dimmer_curr_state[1] = DIMMER_MQTT_DIRECTION_INVERSION;
                                    }
                                    else if (dimmer_mqtt_final_perc[1] != dimmer_curr_perc[1])
                                    {
                                        dimmer_final_perc[1] = dimmer_mqtt_final_perc[1];
                                        dimmer_delta_perc[1] = abs(dimmer_final_perc[1] - dimmer_curr_perc[1]);
                                        dimmer_steps_to_do[1] = max((dimmer_delta_perc[1]*dev_conf->m_dimming_steps[1])/100, DIMMER_MIN_LONGPRESS_TIME/CYCLE_TIME);
                                        dimmer_steps_done[1] = 0;
                                        dimmer_curr_state[1] = DIMMER_COUNTING;
                                    }
                                }
                            }
                            else
                            {
                                if (dimmer_mqtt_on_off[1] == 0)
                                 {
                                    if (dev_conf->m_dimmer_logic_state[1] == 0)
                                        break;
                                    else
                                    {
                                        arm_timer(dimmer_mqtt_shortpress_timer + 1);
                                        if (dimmer_curr_state[1] != DIMMER_IDLE)
                                        {
                                            dimmer_curr_state[1] = DIMMER_MQTT_PRE_SHORTPRESS;
                                            dimmer_direction[1] = !dimmer_direction[1];
                                            dimmer_pression_timer[1].expired = TRUE;
                                        }
                                        else
                                            dimmer_curr_state[1] = DIMMER_MQTT_SHORTPRESS;
                                    }
                                }
                                if (dimmer_mqtt_on_off[1] == 1)
                                {
                                    if (dev_conf->m_dimmer_logic_state[1] == 1)
                                        break;
                                    else
                                    {
                                        arm_timer(dimmer_mqtt_shortpress_timer + 1);
                                        dimmer_curr_state[1] = DIMMER_MQTT_SHORTPRESS;
                                    }
                                }
                            }
                            break;
                    }
                }
            }

            /* diag topic handler */
            strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
            strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
            strcat(NetworkReq_Mng_Data->m_topicapp, "/diag/cmd");

            if (!strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, strlen(NetworkReq_Mng_Data->m_topicapp)))
            {
                char config[512];
                char temp[32];

                switch (NetworkReq_Mng_Data->m_databuf[0])
                {
                    /* Disable PAM */
                    case '1':
                        dev_conf->m_pam_mode = PAM_DISABLE;
                        Nvm_Mng_Save();
                        DEBUG_INFO("PAM disabled");
                        break;

                    /* Enable PAM */
                    case '2':
                        dev_conf->m_pam_mode = PAM_ENABLE;
                        Nvm_Mng_Save();
                        DEBUG_INFO("PAM enabled");
                        break;

                    /* Version & PAM */
                    case '3':
                        os_sprintf(NetworkReq_Mng_Data->m_payloadchar,"{\"version\":\"%d.%d.%d\", \"pamMode\":%s}",FW_VERS_MAJOR,FW_VERS_MINOR,FW_VERS_BUGFIX,dev_conf->m_pam_mode ? "false" : "true");
                        publish_on_topic("diag/alert", NetworkReq_Mng_Data->m_payloadchar);
                        DEBUG_INFO("Version sent");
                        break;

                    /* Reboot in AP_MODE */
                    case '4':
                        if (dev_conf->m_pam_mode == PAM_ENABLE)
                        {
                            os_sprintf(mqtt_msg, "{ \"state\": { \"reported\": { \"connected\": false, \"configMode\": true, \"clientId\": \"%s\", \"sessionId\": \"%s\", \"isLwt\": false } } }", mqtt_client_id, session_id);
                            DEBUG_INFO("Case 4: going in AP!");
                            custom_restart(FALSE, FALSE, mqtt_msg);
                            ap_mode_reboot_required = TRUE;
//                            network_feedback = true;
//                            times = 2;
//                            DEBUG_INFO("network_feedback: %s | times: %d", network_feedback ? "TRUE" : "FALSE", times);
                        }
                        else
                            DEBUG_INFO("PAM must be ENABLED");
                        break;

                    /* RSSI */
                    case '5':
                        int_to_char(NetworkReq_Mng_Data->m_payloadchar, wifi_station_get_rssi());
                        publish_on_topic("diag/rssi", NetworkReq_Mng_Data->m_payloadchar);
                        DEBUG_INFO("Wifi station RSSI sent");
                        break;

                    /* Forced reboot | rebootreason = 10 */
                    case '6':
                        dev_conf->m_rebootreason = RR_REBOOT_CMD;
                        real_NVM_save();
                        /* Enable the flag, asking for a restart */
                        os_sprintf(mqtt_msg, "{ \"state\": { \"reported\": { \"connected\": false, \"clientId\": \"%s\", \"sessionId\": \"%s\", \"isLwt\": false } } }", mqtt_client_id, session_id);
                        DEBUG_INFO("Case 6: rebooting!");
                        custom_restart(FALSE, FALSE, mqtt_msg);
                        break;

                    /* Version */
                    case '7':
                        os_sprintf(NetworkReq_Mng_Data->m_payloadchar, "{\"version\": \"%d.%d.%d\"}", FW_VERS_MAJOR, FW_VERS_MINOR, FW_VERS_BUGFIX);
                        publish_on_topic("diag/version", NetworkReq_Mng_Data->m_payloadchar);
                        break;

                    /* PAM */
                    case '8':
                        os_sprintf(NetworkReq_Mng_Data->m_payloadchar,"{\"pamMode\": %s}", dev_conf->m_pam_mode ? "false" : "true");
                        publish_on_topic("diag/pam", NetworkReq_Mng_Data->m_payloadchar);
                        break;

                    /* Reboot reason */
                    case '9':
                        os_sprintf(NetworkReq_Mng_Data->m_payloadchar,"{\"rebootReasonCode\": %d}", dev_conf->m_rebootreason);
                        publish_on_topic("diag/rebootreason", NetworkReq_Mng_Data->m_payloadchar);
                        break;

                    /* enable publish_on_serial */
                    case 'e':
                        mqtt_serial_enable = TRUE;
                        publish_on_serial("enabled");
                        DEBUG_INFO("publish_on_serial ENABLED");
                        break;
                    /* disable publish_on_serial */
                    case 'd':
                        publish_on_serial("disabled");
                        mqtt_serial_enable = FALSE;
                        DEBUG_INFO("publish_on_serial DISABLED");
                        break;
                }
            }

            /* Check if FOTA topic has been updated */
            strcpy(NetworkReq_Mng_Data->m_topicapp, "/powa/");
            strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
            strcat(NetworkReq_Mng_Data->m_topicapp, "/fota/update/");
            /* Check address of binary to upgrade */
            strcat(NetworkReq_Mng_Data->m_topicapp, (system_get_userbin_addr() == 0x1000 ? "user2" : "user1"));

            char global_topic[32] = "/powa/fota/update";
            
            /* if hit, update the s3 binary url */
            if ((strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, os_strlen(NetworkReq_Mng_Data->m_topicapp)) == 0) ||
                    strncmp(NetworkReq_Mng_Data->m_topicbuf, global_topic, os_strlen(global_topic)) == 0)
            {
                get_userlink_from_string(s3_aws_url, NetworkReq_Mng_Data->m_databuf, NetworkReq_Mng_Data->m_databuf);
                Fota_Mng_StartFota();
            }

            strcpy(NetworkReq_Mng_Data->m_topicapp, "/powa/");
            strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
            strcat(NetworkReq_Mng_Data->m_topicapp, "/diag/debounce");
            
            if (strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, os_strlen(NetworkReq_Mng_Data->m_topicapp)) == 0)
            {
                DEBUG_INFO("Current dev_conf->m_debounce_timer: %d", dev_conf->m_debounce_timer);
                dev_conf->m_debounce_timer = atoi(NetworkReq_Mng_Data->m_databuf);

                if (!is_in_range(dev_conf->m_debounce_timer, DEB_MIN_TIME, DEB_MAX_TIME))
                    dev_conf->m_debounce_timer = DEB_MIN_TIME;

                DEBUG_INFO("Saving new debounce timer to %d", dev_conf->m_debounce_timer);
                Nvm_Mng_Save();
            }

            strcpy(NetworkReq_Mng_Data->m_topicapp, "/powa/");
            strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
            strcat(NetworkReq_Mng_Data->m_topicapp, "/diag/roller_delay");

            if (strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, os_strlen(NetworkReq_Mng_Data->m_topicapp)) == 0)
            {
                dev_conf->m_roller_delay = atoi(NetworkReq_Mng_Data->m_databuf);
                DEBUG_INFO("Saving new roller delay to %d", dev_conf->m_roller_delay);
                os_sprintf(NetworkReq_Mng_Data->m_ansbuf, "{ \"state\": { \"reported\": { \"config\": {\"rollerDelay\": %d } } } }", dev_conf->m_roller_delay);
                publish_on_topic("status/update", NetworkReq_Mng_Data->m_ansbuf);
                Nvm_Mng_Save();
            }

            strcpy(NetworkReq_Mng_Data->m_topicapp, base_topic);
            strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
            strcat(NetworkReq_Mng_Data->m_topicapp, "/certificates/update");

            if (strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, os_strlen(NetworkReq_Mng_Data->m_topicapp)) == 0)
                update_aws_certificates(NetworkReq_Mng_Data->m_databuf);

            strcpy(NetworkReq_Mng_Data->m_topicapp, "/powa/");
            strcat(NetworkReq_Mng_Data->m_topicapp, dev_conf->m_device_id);
            strcat(NetworkReq_Mng_Data->m_topicapp, "/diag/inhibit");
            
            if (strncmp(NetworkReq_Mng_Data->m_topicbuf, NetworkReq_Mng_Data->m_topicapp, os_strlen(NetworkReq_Mng_Data->m_topicapp)) == 0)
            {
                DEBUG_INFO("Current dev_conf->m_inhibit_max_time: %d", dev_conf->m_inhibit_max_time);
                dev_conf->m_inhibit_max_time = atoi(NetworkReq_Mng_Data->m_databuf);

                if (!is_in_range(dev_conf->m_inhibit_max_time, INHIBIT_MIN_TIME, INHIBIT_MAX_TIME))
                    dev_conf->m_inhibit_max_time = INHIBIT_MIN_TIME;

                DEBUG_INFO("Saving new inhibit_max_time to %d", dev_conf->m_inhibit_max_time);
                Nvm_Mng_Save();
            }

            /* free allocated memory */
            os_free(NetworkReq_Mng_Data->m_topicbuf);
            os_free(NetworkReq_Mng_Data->m_databuf);
            os_free(NetworkReq_Mng_Data->m_ansbuf);
        }

        /* Switch OFF pin after n millisec */
        if (is_toggle() || is_latched())
        {
            if (dev_conf->m_relay[0] == TRUE)
            {
                /* Check on channel 1 */
                if (NetworkReq_Mng_Data->m_seqtmr_ch1 < 1*dev_conf->m_reletmr_ch1)
                    NetworkReq_Mng_Data->m_seqtmr_ch1 += CYCLE_TIME;

                if (NetworkReq_Mng_Data->m_seqtmr_ch1 == dev_conf->m_reletmr_ch1)
                {
                    NetworkReq_Mng_Data->m_seqtmr_ch1 += CYCLE_TIME;
                    GPIOMng_SetPinState(L1, FALSE);
                }

                if ((NetworkReq_Mng_Data->m_numitertodo_ch1 > 0) && (NetworkReq_Mng_Data->m_seqtmr_ch1 == dev_conf->m_reletmr_ch1))
                {
                    NetworkReq_Mng_Data->m_seqtmr_ch1 = 0;
                    NetworkReq_Mng_Data->m_numitertodo_ch1--;
                    GPIOMng_SetPinState(L1, TRUE);
                    dev_conf->m_relecurrstatus_ch1 = (dev_conf->m_relecurrstatus_ch1 + 1)%(dev_conf->m_numberstatus_ch1);
                    Nvm_Mng_Save();
                    GPIOMng_RstPinStsChanged(SW1);
                }
            }

            if (dev_conf->m_relay[1] == TRUE)
            {
                /* Check on channel 2 */
                if (NetworkReq_Mng_Data->m_seqtmr_ch2 < dev_conf->m_reletmr_ch2)
                    NetworkReq_Mng_Data->m_seqtmr_ch2 += CYCLE_TIME;

                if (NetworkReq_Mng_Data->m_seqtmr_ch2 == dev_conf->m_reletmr_ch2)
                {
                    NetworkReq_Mng_Data->m_seqtmr_ch2 += CYCLE_TIME;
                    GPIOMng_SetPinState(L2, FALSE);
                }

                if ((NetworkReq_Mng_Data->m_numitertodo_ch2 > 0) && (NetworkReq_Mng_Data->m_seqtmr_ch2 == dev_conf->m_reletmr_ch2))
                {
                    NetworkReq_Mng_Data->m_seqtmr_ch2 = 0;
                    NetworkReq_Mng_Data->m_numitertodo_ch2--;
                    GPIOMng_SetPinState(L2, TRUE);
                    dev_conf->m_relecurrstatus_ch2 = (dev_conf->m_relecurrstatus_ch2 + 2)%(dev_conf->m_numberstatus_ch2);
                    Nvm_Mng_Save();
                    GPIOMng_RstPinStsChanged(SW2);
                }
            }
        }
    }
}

/*
 * Name:    bool NetworkReq_Mng_send_connected_status(void)
 * Descr:   Notify the broker about the connection
 * Return:  TRUE if success
 */

bool ICACHE_FLASH_ATTR NetworkReq_Mng_send_connected_status(void)
{
    DEBUG_INFO("Publishing \"connected\": true");

    char config[666];   // maximum buffer size should be (doing the math) ~600 chars; let's add a 11% tollerance and we obtain 666 chars
    char temp[32];

    os_sprintf(config, "{ \"state\": { \"reported\": { \"connected\": true, \"rssi\": %d, \"version\": \"%d.%d.%d\", \"rebootReasonCode\": %d, \"pamMode\": %s, \"clientId\": \"%s\", ", wifi_station_get_rssi(), FW_VERS_MAJOR, FW_VERS_MINOR, FW_VERS_BUGFIX, dev_conf->m_rebootreason, dev_conf->m_pam_mode == 0 ? "true" : "false", mqtt_client_id);


    /* Current state */
    if (is_roller())
    {
        os_sprintf(temp, "%d", roll_curr_perc);
        strcat(config, "\"rb\": "); strcat(config, temp); strcat(config, ", ");
    }
    else if (is_switch())
    {
        os_sprintf(temp, "%s", GPIOMng_GetPinState(L1) != 0 ? "true" : "false");
        strcat(config, "\"sw1\": "); strcat(config, temp); strcat(config, ", ");
        
        os_sprintf(temp, "%s", GPIOMng_GetPinState(L2) != 0 ? "true" : "false");
        strcat(config, "\"sw2\": "); strcat(config, temp); strcat(config, ", ");
    } 
    else if (is_toggle() || is_latched())
    {
        // TODO: manage also the STEP light with more than 2 states
        os_sprintf(temp, "%s", dev_conf->m_relecurrstatus_ch1 % 2 == 0 ? "true" : "false");
        strcat(config, "\"sw1\": "); strcat(config, temp); strcat(config, ", ");
        
        os_sprintf(temp, "%s", dev_conf->m_relecurrstatus_ch1 % 2 == 0 ? "true" : "false");
        strcat(config, "\"sw2\": "); strcat(config, temp); strcat(config, ", ");
    }
    else if (is_dimmer())
    {
        os_sprintf(temp, "%s", dev_conf->m_dimmer_logic_state[0] == HIGH ? "true" : "false");
        strcat(config, "\"sw1\": "); strcat(config, temp); strcat(config, ", ");
        
        os_sprintf(temp, "%s", dev_conf->m_dimmer_logic_state[1] == HIGH ? "true" : "false");
        strcat(config, "\"sw2\": "); strcat(config, temp); strcat(config, ", ");

        /* Brightness level dimmer_1 */
        os_sprintf(temp, "%d", dev_conf->m_dimming_perc[0]);
        strcat(config, "\"sw1Brightness\": "); strcat(config, temp); strcat(config, ", ");

        /* Brightness level dimmer_2 */
        os_sprintf(temp, "%d", dev_conf->m_dimming_perc[1]);
        strcat(config, "\"sw2Brightness\": "); strcat(config, temp); strcat(config, ", ");
    }


/* START OF CONFIGURATION */
    strcat(config, "\"config\": { ");

    os_sprintf(temp, "%d", dev_conf->m_config_version);
    strcat(config, "\"configVers\": "); strcat(config, temp); strcat(config, ", ");
    
    /* SSID */
    strcat(config, "\"ssid\": \""); strcat(config, dev_conf->m_sta_ssid); strcat(config, "\", ");
    
    /* MQTT Host */
    strcat(config, "\"mqttHost\": \""); strcat(config, dev_conf->m_mqtt_host); strcat(config, "\", ");
    
    /* MQTT Port */
    os_sprintf(temp, "%d", dev_conf->m_mqtt_port);
    strcat(config, "\"mqttPort\": "); strcat(config, temp); strcat(config, ", ");
    
    /* MQTT Keepalive */
    os_sprintf(temp, "%d", dev_conf->m_mqtt_keepalive);
    strcat(config, "\"mqttKeepAlive\": "); strcat(config, temp); strcat(config, ", ");

    /* Security */
    os_sprintf(temp, "%s", dev_conf->m_security ? "true" : "false");
    strcat(config, "\"security\": "); strcat(config, temp); strcat(config, ", ");

    /* Device type */
    strcat(config, "\"deviceType\": \""); strcat(config, device_type[dev_conf->m_type]); strcat(config, "\"");
    
    if (is_roller())
    {
        strcat(config, ", ");

        /* Roller rise time */
        os_sprintf(temp, "%d", dev_conf->m_rise_steps*CYCLE_TIME);
        strcat(config, "\"rollerRiseTime\": "); strcat(config, temp); strcat(config, ", ");

        /* Roller fall time */
        os_sprintf(temp, "%d", dev_conf->m_fall_steps*CYCLE_TIME);
        strcat(config, "\"rollerFallTime\": "); strcat(config, temp); strcat(config, ", ");

        /* Roller delay */
        os_sprintf(temp, "%d", dev_conf->m_roller_delay);
        strcat(config, "\"rollerDelay\": "); strcat(config, temp);
    }
    else if (is_toggle() || is_latched())
    {
        strcat(config, ", ");

        /* Relay1 maximum states */
        os_sprintf(temp, "%d", dev_conf->m_numberstatus_ch1);
        strcat(config, "\"maxCh1State\": "); strcat(config, temp); strcat(config, ", ");
        
        /* Relay1 timer */
        os_sprintf(temp, "%d", dev_conf->m_reletmr_ch1);
        strcat(config, "\"millisCh1\": "); strcat(config, temp); strcat(config, ", ");
        
        /* Relay2 maximum states */
        os_sprintf(temp, "%d", dev_conf->m_numberstatus_ch2);
        strcat(config, "\"maxCh2State\": "); strcat(config, temp); strcat(config, ", ");
        
        /* Relay2 timer */
        os_sprintf(temp, "%d", dev_conf->m_reletmr_ch2);
        strcat(config, "\"millisCh2\": "); strcat(config, temp);
    }
    else if (is_dimmer())
    {
        strcat(config, ", ");

        /* Brightness level dimmer_1 */
        os_sprintf(temp, "%d", dev_conf->m_dimming_steps[0]*CYCLE_TIME);
        strcat(config, "\"sw1Timer\": "); strcat(config, temp); strcat(config, ", ");

        /* Brightness level dimmer_1 */
        os_sprintf(temp, "%d", dev_conf->m_dimming_steps[1]*CYCLE_TIME);
        strcat(config, "\"sw2Timer\": "); strcat(config, temp);
    }


    /* trailing bracket */
    strcat(config, " }, ");

/* END OF CONFIGURATION */

    strcat(config, "\"configMode\": false"); strcat(config, ", ");
    strcat(config, "\"fotaMode\": false"); strcat(config, "} } }");
    publish_on_topic("status/update", config);
}

/*
 * Name:    bool NetworkReq_Mng_send_channels_status(void)
 * Descr:   Notify the broker about the channels status
 * Return:  TRUE if success
 */

bool ICACHE_FLASH_ATTR NetworkReq_Mng_send_channels_status(void)
{
    DEBUG_INFO("Publishing channels status");

    char temp[32];

    /* Current state */
    if (is_roller())
    {
        /* Prepare the topic */
        os_sprintf(temp, "%d:%d", roll_curr_perc, roll_curr_perc);
        publish_on_topic("rb/alert", temp);
    } 
    else if (is_switch())
    {
        int_to_char(temp, GPIOMng_GetPinState(L1));
        publish_on_topic("sw1/alert", temp);

        int_to_char(temp, GPIOMng_GetPinState(L2));
        publish_on_topic("sw2/alert", temp);
    } 
    else if (is_toggle() || is_latched())
    {
        int_to_char(temp, dev_conf->m_relecurrstatus_ch1);
        publish_on_topic("sw1/alert", temp);
        
        int_to_char(temp, dev_conf->m_relecurrstatus_ch2);
        publish_on_topic("sw2/alert", temp);
    }
    else if (is_dimmer())
    {
        dimmer_pub_message(0, FALSE);
        dimmer_pub_message(1, FALSE);
    }
}

/*
 * Name:    void NetworkReq_Mng_Init(void)
 * Descr:   Network requests manager initialization
 */

t_NetworkReq_Mng_Data *ICACHE_FLASH_ATTR alloc_new_network_struct(void)
{
    if (NetworkReq_Mng_Data != NULL)
        return;

    t_NetworkReq_Mng_Data *new_struct = os_zalloc(sizeof(char)*sizeof(t_NetworkReq_Mng_Data));

    new_struct->m_seqtmr_ch1 = 0;
    new_struct->m_seqtmr_ch2 = 0;

    return new_struct;
}

void ICACHE_FLASH_ATTR initialize_network_struct(void)
{
    DEBUG_INFO("initialize_network_struct | BEFORE: %p", NetworkReq_Mng_Data);
    NetworkReq_Mng_Data = alloc_new_network_struct();
    DEBUG_INFO("initialize_network_struct | AFTER: %p", NetworkReq_Mng_Data);
}

void ICACHE_FLASH_ATTR deinitialize_network_struct(void)
{
    powa_free((void *)&NetworkReq_Mng_Data, "NetworkReq_Mng_Data");
}

void ICACHE_FLASH_ATTR NetworkReq_Mng_Init(void)
{
    DEBUG_INFO("NetworkReq_Mng_Init...");

    initialize_network_struct();

    dev_conf = Nvm_Mng_GetNvm();

    DEBUG_INFO("NetworkReq_Mng_Data | roll_curr_perc: %d | dev_conf->m_roll_curr_perc: %d", roll_curr_perc, dev_conf->m_roll_curr_perc);
    DEBUG_INFO("NetworkReq_Mng_Init complete!");
}
