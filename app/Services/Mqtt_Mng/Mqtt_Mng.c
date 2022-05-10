/*
 * FileName:    Mqtt_Mng.c
 * Brief:       Manager for Mqtt service
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
#include "Nvm_Mng.h"
#include "Mqtt_Mng.h"
#include "Misc.h"

/* ----------------------- Defines ------------------------ */

#define MQTT_CONNECTION_ATTEMPTS 4

/* ---------------------- Data Types ---------------------- */

t_Mqtt_Mng_Data Mqtt_Mng_Data;
t_Mqtt_Queue *Mqtt_Queue;

char mqtt_client_id[17];
char session_id[4];
bool restartable = FALSE;
bool to_be_restarted = FALSE;
bool to_be_disconnected = FALSE;
bool fota_reboot_flag;
bool sub_flag;
bool check_my_sub_flag = FALSE;
bool backoff_timer_enabled;
bool device_connected = FALSE;
bool resubscribe_required = FALSE;
t_timer sub_ack_reconnect_timer = { .timer = 0, .expired = TRUE, .name = "sub_ack_reconnect_timer" };

char base_topic[16];
/* ------------------- Local functions -------------------- */

void ICACHE_FLASH_ATTR MQTT_Create_LWT(MQTT_Client *mqttClient)
{
    t_Nvm_Mng_Data *dev_conf = Nvm_Mng_GetNvm();
    char mqttPayload[128];
    char mqtt_lwt_topic[64];

    os_sprintf(mqttPayload, "{ \"state\": { \"reported\": { \"connected\": false, \"clientId\": \"%s\", \"sessionId\": \"%s\", \"isLwt\": true } } }", mqtt_client_id, session_id);
    strcpy(mqtt_lwt_topic, base_topic);
    strcat(mqtt_lwt_topic, dev_conf->m_device_id);
    strcat(mqtt_lwt_topic, "/status");
    strcat(mqtt_lwt_topic, "/update");
    MQTT_InitLWT(mqttClient, mqtt_lwt_topic, mqttPayload, 0, 0);
}

void ICACHE_FLASH_ATTR mqtt_change_session_id(void)
{
    char random_id[4];
    random_id_number[0] = os_random()%10;
    random_id_number[1] = os_random()%10;
    random_id_number[2] = os_random()%10;

    os_sprintf(random_id, "%d%d%d", random_id_number[0], random_id_number[1], random_id_number[2]);
    os_strcpy(session_id, random_id);
}

/*
 * Name:    void mqttConnectedCb(uint32_t *args)
 * Descr:   Callback function called when connected to MQTT broker
 */

static void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args)
{
    char mqttTopicCmd[64];
    char payload[4];
    MQTT_Client* client = (MQTT_Client*)args;
    t_Nvm_Mng_Data *dev_conf;

    /* Read the eeprom configuration */
    dev_conf = Nvm_Mng_GetNvm();
    sub_flag = TRUE;
    resubscribe_required = FALSE;

    MQTT_INFO("MQTT: Connected");
    /* Create and subscribe to all the necessary topics */
    /* PowaSwitch device */
    if (is_switch() || is_toggle() || is_latched() || is_dimmer())
    {
        strcpy(mqttTopicCmd, base_topic);
        strcat(mqttTopicCmd, dev_conf->m_device_id);
        strcat(mqttTopicCmd, "/sw1/cmd");
        MQTT_Subscribe(client, mqttTopicCmd, 1);

        strcpy(mqttTopicCmd, base_topic);
        strcat(mqttTopicCmd, dev_conf->m_device_id);
        strcat(mqttTopicCmd, "/sw2/cmd");
        MQTT_Subscribe(client, mqttTopicCmd, 1);

        strcpy(mqttTopicCmd, base_topic);
        strcat(mqttTopicCmd, dev_conf->m_device_id);
        strcat(mqttTopicCmd, "/all/cmd");
        MQTT_Subscribe(client, mqttTopicCmd, 1);
    }
    else if (is_roller())
    {
        strcpy(mqttTopicCmd, base_topic);
        strcat(mqttTopicCmd, dev_conf->m_device_id);
        strcat(mqttTopicCmd, "/rb/cmd");
        MQTT_Subscribe(client, mqttTopicCmd, 1);

        strcpy(mqttTopicCmd, base_topic);
        strcat(mqttTopicCmd, dev_conf->m_device_id);
        strcat(mqttTopicCmd, "/all/cmd");
        MQTT_Subscribe(client, mqttTopicCmd, 1);

        strcpy(mqttTopicCmd, base_topic);
        strcat(mqttTopicCmd, dev_conf->m_device_id);
        strcat(mqttTopicCmd, "/diag");
        strcat(mqttTopicCmd, "/rise");
        MQTT_Subscribe(client, mqttTopicCmd, 1);

        strcpy(mqttTopicCmd, base_topic);
        strcat(mqttTopicCmd, dev_conf->m_device_id);
        strcat(mqttTopicCmd, "/diag");
        strcat(mqttTopicCmd, "/fall");
        MQTT_Subscribe(client, mqttTopicCmd, 1);

        strcpy(mqttTopicCmd, base_topic);
        strcat(mqttTopicCmd, dev_conf->m_device_id);
        strcat(mqttTopicCmd, "/diag");
        strcat(mqttTopicCmd, "/roller_delay");
        MQTT_Subscribe(client, mqttTopicCmd, 1);
    }

    /* Subscribe for commands */
    strcpy(mqttTopicCmd, base_topic);
    strcat(mqttTopicCmd, dev_conf->m_device_id);
    strcat(mqttTopicCmd, "/diag");
    strcat(mqttTopicCmd, "/cmd");
    MQTT_Subscribe(client, mqttTopicCmd, 1);

    /* Subscribe for FOTA upgrades */
    strcpy(mqttTopicCmd, "/powa/");
    strcat(mqttTopicCmd, dev_conf->m_device_id);
    strcat(mqttTopicCmd, "/fota");
    strcat(mqttTopicCmd, "/update/user1");
    MQTT_Subscribe(client, mqttTopicCmd, 1);

    strcpy(mqttTopicCmd, "/powa/");
    strcat(mqttTopicCmd, dev_conf->m_device_id);
    strcat(mqttTopicCmd, "/fota");
    strcat(mqttTopicCmd, "/update/user2");
    MQTT_Subscribe(client, mqttTopicCmd, 1);

    strcpy(mqttTopicCmd, "/powa");
    strcat(mqttTopicCmd, "/fota");
    strcat(mqttTopicCmd, "/update/user1");
    MQTT_Subscribe(client, mqttTopicCmd, 1);

    strcpy(mqttTopicCmd, "/powa");
    strcat(mqttTopicCmd, "/fota");
    strcat(mqttTopicCmd, "/update/user2");
    MQTT_Subscribe(client, mqttTopicCmd, 1);

    strcpy(mqttTopicCmd, "/powa/");
    strcat(mqttTopicCmd, dev_conf->m_device_id);
    strcat(mqttTopicCmd, "/diag");
    strcat(mqttTopicCmd, "/debounce");
    MQTT_Subscribe(client, mqttTopicCmd, 1);

    strcpy(mqttTopicCmd,"/powa/");
    strcat(mqttTopicCmd,dev_conf->m_device_id);
    strcat(mqttTopicCmd,"/diag");
    strcat(mqttTopicCmd,"/inhibit");
    MQTT_Subscribe(client, mqttTopicCmd, 1);

    /* certificates update */
    strcpy(mqttTopicCmd, base_topic);
    strcat(mqttTopicCmd, dev_conf->m_device_id);
    strcat(mqttTopicCmd, "/certificates");
    strcat(mqttTopicCmd, "/update");
    MQTT_Subscribe(client, mqttTopicCmd, 1);

    /* Disable PAM after successful connection */
    dev_conf->m_pam_mode = PAM_DISABLE;

    /* Reset exponential backoff after successful connection */
    backoff_timer_enabled = FALSE;
    dev_conf->m_backoff = MIN_BACKOFF_TIMER;
    system_status_check_disarm_timer();
    system_status_check_arm_timer();

    check_my_sub_flag = TRUE;

    /* Send status update */
    NetworkReq_Mng_send_connected_status();

    /* Send channels update */
    NetworkReq_Mng_send_channels_status();
    device_connected = TRUE;

    // Initializes the queue for the MQTT messages
    mqtt_queue_init();

    // Initializes Application services for network and FOTA
    NetworkReq_Mng_Init();
    Fota_Mng_Init();

    if (dev_conf->m_fota_status == FOTA_STATUS_PENDING)
        update_fota_status(FOTA_STATUS_OK);

    disarm_timer_for_roller_stop_disconnection();
}

/*
 * Name:    void mqttDisconnectedCb(uint32_t *args)
 * Descr:   Callback function called when disconnected from MQTT broker
 */

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
}

/*
 * Name:    void mqttPublishedCb(uint32_t *args)
 * Descr:   Callback function called when MQTT message has b een published
 */

static void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    uint16_t topic_length, data_length;
    const char *topic_data = mqtt_get_publish_topic(client->mqtt_state.out_buffer, &client->mqtt_state.message_length);
    const char *publish_data = mqtt_get_publish_data(client->mqtt_state.out_buffer, &data_length);
    uint16_t msg_id = mqtt_get_id(client->mqtt_state.out_buffer, client->mqtt_state.in_buffer_length);

    MQTT_INFO("MQTT: Published");

    if (to_be_restarted)
    {
        restartable = true;

        if (to_be_disconnected)
            Mqtt_Mng_Disconnect();
    }
    if (to_be_fota_restarted)
    {
        fota_restartable = true;
        to_be_fota_restarted = false;

        if (to_be_disconnected)
            Mqtt_Mng_Disconnect();
    }
}

t_Mqtt_Queue *ICACHE_FLASH_ATTR alloc_new_mqtt_queue(void)
{
    DEBUG_INFO("alloc_new_mqtt_queue...");
    if (Mqtt_Queue != NULL)
        return;

    t_Mqtt_Queue *new_struct = os_zalloc(sizeof(char)*sizeof(t_Mqtt_Queue));

    new_struct->queue_elems = 0;
    new_struct->write_index = 0;
    new_struct->read_index = 0;

    DEBUG_INFO("alloc_new_mqtt_queue [OK]");
    return new_struct;
}

void ICACHE_FLASH_ATTR mqtt_queue_init(void)
{
    DEBUG_INFO("mqtt_queue_init | BEFORE: %p", Mqtt_Queue);
    Mqtt_Queue = alloc_new_mqtt_queue();
    DEBUG_INFO("mqtt_queue_init | AFTER: %p", Mqtt_Queue);
}

void ICACHE_FLASH_ATTR mqtt_queue_deinit(void)
{
    powa_free((void *)&Mqtt_Queue, "Mqtt_Queue");
}

bool ICACHE_FLASH_ATTR mqtt_queue_is_empty(void)
{
    bool ret_val = FALSE;
    uint8_t queue_elems = 0;

    if (Mqtt_Queue == NULL)
        return TRUE;

    queue_elems = Mqtt_Queue->queue_elems;

    if (queue_elems == 0 || queue_elems > MQTT_QUEUE_MAX_SIZE)
        ret_val = TRUE;
    else
    {
        DEBUG_INFO("mqtt_queue_is_empty | queue_elems: %d", queue_elems);
        ret_val = FALSE;
    }

    return ret_val;
}

void ICACHE_FLASH_ATTR mqtt_queue_add(uint32_t *args, const char *topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
    t_Mqtt_Mng_Data_Msg *curr_elem = Mqtt_Queue->Queue + Mqtt_Queue->write_index;

    os_memcpy(curr_elem->m_msgtopic, topic, topic_len);
    curr_elem->m_msgtopiclen = topic_len;
    os_memcpy(curr_elem->m_msgdata, data, data_len);
    curr_elem->m_msgdatalen = data_len;

    Mqtt_Queue->write_index = (Mqtt_Queue->write_index + 1)%MQTT_QUEUE_MAX_SIZE;
    Mqtt_Queue->queue_elems = min(Mqtt_Queue->queue_elems + 1, MQTT_QUEUE_MAX_SIZE);

    DEBUG_INFO("mqtt_queue_add | Element added to the queue! Current elements: %d", Mqtt_Queue->queue_elems);
}

t_Mqtt_Mng_Data_Msg ICACHE_FLASH_ATTR *mqtt_queue_get_msg(void)
{
    t_Mqtt_Mng_Data_Msg *curr_elem = (Mqtt_Queue->Queue + Mqtt_Queue->read_index);

    Mqtt_Queue->read_index = (Mqtt_Queue->read_index + 1)%MQTT_QUEUE_MAX_SIZE;
    Mqtt_Queue->queue_elems--;

    DEBUG_INFO("mqtt_queue_get_msg | Element removed from the queue! Current elements: %d", Mqtt_Queue->queue_elems);
    return curr_elem;
}

/* ------------------- Global functions ------------------- */

/*
 * Name:    uint8 Mqtt_Mng_Init(void)
 * Descr:   Initialize Mqtt connection
 */

void ICACHE_FLASH_ATTR Mqtt_Mng_Init(void)
{
    t_Nvm_Mng_Data *dev_conf;

    /* Read the eeprom configuration */
    dev_conf = Nvm_Mng_GetNvm();

    if (dev_conf->m_security)
    {
        /* Configure secure */
        espconn_secure_set_size(0x01, 2048);

        #if (POWA_HW_VERS == HW_VERS_1)
            espconn_secure_ca_enable(0x01, 0x75);
            espconn_secure_cert_req_enable(0x01, 0x76);
        #else
            espconn_secure_ca_enable(0x01, 0xF5);
            espconn_secure_cert_req_enable(0x01, 0xF6);
        #endif
    }

    char *business_name = get_business_topic();
    DEBUG_INFO("business_name: %s", business_name);
    if (strcmp(business_name, "Powahome"))
    {
        lower_string(business_name);
        os_sprintf(base_topic, "/%s/", business_name);
    }
    else
    {
        os_strcpy(base_topic, "/powa/");
    }

    os_free(business_name);
    /* Configure MQTT Broker informations */
    MQTT_InitConnection(&Mqtt_Mng_Data.m_mqttclient, dev_conf->m_mqtt_host, dev_conf->m_mqtt_port, dev_conf->m_security);
    /* Configure MQTT info for broker connection */
    MQTT_InitClient(&Mqtt_Mng_Data.m_mqttclient, dev_conf->m_device_id, dev_conf->m_mqtt_user, dev_conf->m_mqtt_pass, dev_conf->m_mqtt_keepalive, 1);
    /* Send info if powahome is offline */
    MQTT_Create_LWT(&Mqtt_Mng_Data.m_mqttclient);
    /* Setup callback functions for MQTT events */
    MQTT_OnConnected(&Mqtt_Mng_Data.m_mqttclient, mqttConnectedCb);
    MQTT_OnDisconnected(&Mqtt_Mng_Data.m_mqttclient, mqttDisconnectedCb);
    MQTT_OnPublished(&Mqtt_Mng_Data.m_mqttclient, mqttPublishedCb);
    MQTT_OnData(&Mqtt_Mng_Data.m_mqttclient, mqtt_queue_add);
    Mqtt_Mng_Data.m_isdatarec = FALSE;
}

/*
 * Name:    uint8 Mqtt_Mng_IsDataReceived(void)
 * Descr:   return TRUE or FALSE depending on data received
 */

uint8 ICACHE_FLASH_ATTR Mqtt_Mng_IsDataReceived(void)
{
    return Mqtt_Mng_Data.m_isdatarec;
}

/*
 * Name:    void Mqtt_Mng_SetDataAsRead(void)
 * Descr:   Rese4t received data flag
 */

void ICACHE_FLASH_ATTR Mqtt_Mng_SetDataAsRead(void)
{
    Mqtt_Mng_Data.m_isdatarec = FALSE;
}

/*
 * Name:    void Mqtt_Mng_Connect(void)
 * Descr:   Request MQTT server connection
 */

void ICACHE_FLASH_ATTR Mqtt_Mng_Connect(void)
{
    MQTT_Connect(&Mqtt_Mng_Data.m_mqttclient);
}

/*
 * Name:    void Mqtt_Mng_Disconnect(void)
 * Descr:   Request MQTT server disconnection
 */

void ICACHE_FLASH_ATTR Mqtt_Mng_Disconnect(void)
{
    MQTT_Disconnect(&Mqtt_Mng_Data.m_mqttclient);
}

/*
 * Name:    bool Mqtt_Mng_Publish(char* topic, char *data, uint16 datalen, uint8 qos, uint8 retain)
 * Descr:   Publish MQTT message
 */

bool ICACHE_FLASH_ATTR Mqtt_Mng_Publish(char* topic, char *data, uint16 datalen, uint8 qos, uint8 retain)
{
    return MQTT_Publish(&Mqtt_Mng_Data.m_mqttclient, topic, data, datalen, qos, retain);;
}

void ICACHE_FLASH_ATTR sub_ack_delayed_reconnect(MQTT_Client* client)
{
    static uint16_t reconnect_delay_ms = 0;
    // only arm the timer the first time
    if (reconnect_delay_ms == 0)
    {
        int rand_num = rand();
        reconnect_delay_ms = max(rand_num%SUB_ACK_MAX_RECONNECT_TIMER, SUB_ACK_MIN_RECONNECT_TIMER);
        DEBUG_INFO("sub_ack_delayed_reconnect | reconnect_delay_ms: %d [rand_num: %d]", reconnect_delay_ms, rand_num);
        arm_timer(&sub_ack_reconnect_timer);
    }

    handle_timer(&sub_ack_reconnect_timer, 1, reconnect_delay_ms);
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);

    // if the timer is expired, force the reconnection
    if (is_expired(&sub_ack_reconnect_timer))
    {
        reconnect_delay_ms = 0;
        client->connState = TCP_RECONNECT_DISCONNECTING;
        system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
        return;
    }
}

void ICACHE_FLASH_ATTR disconnected_device_procedure(bool memory_deallocation_flag)
{
    if (!memory_deallocation_flag)
        return;

    device_connected = FALSE;
    deinitialize_network_struct();
    deinitialize_fota_struct();
    mqtt_queue_deinit();
}
