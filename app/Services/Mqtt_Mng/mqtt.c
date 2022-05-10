/* mqtt.c
*  Protocol: http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include "user_interface.h"
#include "osapi.h"
#include "espconn.h"
#include "os_type.h"
#include "mem.h"
#include "mqtt_msg.h"
#include "debug.h"
#include "mqtt.h"
#include "queue.h"
#include "Nvm_Mng.h"
#include "Misc.h"

unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;
int8_t old_connState = 0;
bool phase_A = FALSE, phase_B = FALSE;
bool next_is_disconn_msg = FALSE;
uint16_t disc_msg_id = 0;
uint8_t mqtt_reconnect_counter = 0;
bool device_connected;

bool memory_deallocation_flag;
bool disconnected_for_reboot;

#ifdef GLOBAL_DEBUG_ON
char *connState_string[] = {
    "WIFI_INIT",
    "WIFI_CONNECTING",
    "WIFI_CONNECTING_ERROR",
    "WIFI_CONNECTED",
    "DNS_RESOLVE",
    "TCP_DISCONNECTING",
    "TCP_DISCONNECTED",
    "TCP_RECONNECT_DISCONNECTING",
    "TCP_RECONNECT_REQ",
    "TCP_RECONNECT",
    "TCP_CONNECTING",
    "TCP_CONNECTING_ERROR",
    "TCP_CONNECTED",
    "MQTT_CONNECT_SEND",
    "MQTT_CONNECT_SENDING",
    "MQTT_SUBSCIBE_SEND",
    "MQTT_SUBSCIBE_SENDING",
    "MQTT_DATA",
    "MQTT_KEEPALIVE_SEND",
    "MQTT_PUBLISH_RECV",
    "MQTT_PUBLISHING",
    "MQTT_DELETING",
    "MQTT_DELETED"
    };
#endif

uint16_t random_id_number[3];   // to generate a random client_id
bool sub_flag;

os_event_t mqtt_procTaskQueue[MQTT_TASK_QUEUE_SIZE];

LOCAL void ICACHE_FLASH_ATTR
mqtt_dns_found(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct espconn *pConn = (struct espconn *)arg;
    MQTT_Client* client = (MQTT_Client *)pConn->reverse;


    if (ipaddr == NULL)
    {
        MQTT_INFO("DNS: Found, but got no ip, try to reconnect");
        client->connState = TCP_RECONNECT_REQ;
        return;
    }

    MQTT_INFO("DNS: found ip %d.%d.%d.%d",
         *((uint8 *) &ipaddr->addr),
         *((uint8 *) &ipaddr->addr + 1),
         *((uint8 *) &ipaddr->addr + 2),
         *((uint8 *) &ipaddr->addr + 3));

    if (client->ip.addr == 0 && ipaddr->addr != 0)
    {
        os_memcpy(client->pCon->proto.tcp->remote_ip, &ipaddr->addr, 4);
        if (client->security) {
#ifdef MQTT_SSL_ENABLE
            espconn_secure_connect(client->pCon);
#else
            ERROR_INFO("TCP: Do not support SSL");
#endif
        }
        else {
            espconn_connect(client->pCon);
        }

        client->connState = TCP_CONNECTING;
        MQTT_INFO("TCP: connecting...");
    }

    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}



LOCAL void ICACHE_FLASH_ATTR
deliver_publish(MQTT_Client* client, uint8_t* message, int length)
{
    mqtt_event_data_t event_data;

    event_data.topic_length = length;
    event_data.topic = mqtt_get_publish_topic(message, &event_data.topic_length);
    event_data.data_length = length;
    event_data.data = mqtt_get_publish_data(message, &event_data.data_length);

    if (client->dataCb)
        client->dataCb((uint32_t*)client, event_data.topic, event_data.topic_length, event_data.data, event_data.data_length);

}

void ICACHE_FLASH_ATTR
mqtt_send_keepalive(MQTT_Client *client)
{
    MQTT_INFO("MQTT: Send keepalive packet to %s:%d", client->host, client->port);
    client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);
    client->mqtt_state.pending_msg_type = MQTT_MSG_TYPE_PINGREQ;
    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);


    client->sendTimeout = MQTT_SEND_TIMOUT;
    MQTT_INFO("MQTT: Sending, type: %d, id: %04X", client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
    err_t result = ESPCONN_OK;
    if (client->security) {
#ifdef MQTT_SSL_ENABLE
        result = espconn_secure_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
#else
        ERROR_INFO("TCP: Do not support SSL");
#endif
    }
    else {
        result = espconn_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
    }

    client->mqtt_state.outbound_message = NULL;
    if(ESPCONN_OK == result) {
        client->keepAliveTick = 0;
        client->connState = MQTT_DATA;
        system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
    }
    else {
        client->connState = TCP_RECONNECT_DISCONNECTING;
        system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
    }
}

/**
  * @brief  Delete tcp client and free all memory
  * @param  mqttClient: The mqtt client which contain TCP client
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_delete(MQTT_Client *mqttClient)
{
    t_Nvm_Mng_Data *dev_conf = Nvm_Mng_GetNvm();

    if (mqttClient->pCon != NULL)
    {
        t_Nvm_Mng_Data *dev_conf = Nvm_Mng_GetNvm();

        espconn_delete(mqttClient->pCon);
        if (mqttClient->pCon->proto.tcp)
            os_free(mqttClient->pCon->proto.tcp);
        os_free(mqttClient->pCon);
        mqttClient->pCon = NULL;

        /* CLIENT_ID CHANGE */
        mqtt_change_session_id();
    }
}

/**
  * @brief  Delete MQTT client and free all memory
  * @param  mqttClient: The mqtt client
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_client_delete(MQTT_Client *mqttClient)
{
    mqtt_tcpclient_delete(mqttClient);
    if (mqttClient->host != NULL) {
        os_free(mqttClient->host);
        mqttClient->host = NULL;
    }

    if (mqttClient->user_data != NULL) {
        os_free(mqttClient->user_data);
        mqttClient->user_data = NULL;
    }

    if(mqttClient->connect_info.client_id != NULL) {
        os_free(mqttClient->connect_info.client_id);
        mqttClient->connect_info.client_id = NULL;
    }

    if(mqttClient->connect_info.username != NULL) {
        os_free(mqttClient->connect_info.username);
        mqttClient->connect_info.username = NULL;
    }

    if(mqttClient->connect_info.password != NULL) {
        os_free(mqttClient->connect_info.password);
        mqttClient->connect_info.password = NULL;
    }

    if(mqttClient->connect_info.will_topic != NULL) {
        os_free(mqttClient->connect_info.will_topic);
        mqttClient->connect_info.will_topic = NULL;
    }

    if(mqttClient->connect_info.will_message != NULL) {
        os_free(mqttClient->connect_info.will_message);
        mqttClient->connect_info.will_message = NULL;
    }

    if(mqttClient->mqtt_state.in_buffer != NULL) {
        os_free(mqttClient->mqtt_state.in_buffer);
        mqttClient->mqtt_state.in_buffer = NULL;
    }

    if(mqttClient->mqtt_state.out_buffer != NULL) {
        os_free(mqttClient->mqtt_state.out_buffer);
        mqttClient->mqtt_state.out_buffer = NULL;
    }
}


/**
  * @brief  Client received callback function.
  * @param  arg: contain the ip link information
  * @param  pdata: received data
  * @param  len: the lenght of received data
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_recv(void *arg, char *pdata, unsigned short len)
{
    uint8_t msg_type;
    uint8_t msg_qos;
    uint8_t sub_msg_qos;
    uint16_t msg_id;
    uint8_t b = 0;

    struct espconn *pCon = (struct espconn*)arg;
    MQTT_Client *client = (MQTT_Client *)pCon->reverse;

    client->keepAliveTick = 0;
READPACKET:
    MQTT_INFO("TCP: data received %d bytes", len);
    if (len < MQTT_BUF_SIZE && len > 0) {
        os_memcpy(client->mqtt_state.in_buffer, pdata, len);


        msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
        msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
        msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
        switch (client->connState) {
        case MQTT_CONNECT_SENDING:
            if (msg_type == MQTT_MSG_TYPE_CONNACK) {
                if (client->mqtt_state.pending_msg_type != MQTT_MSG_TYPE_CONNECT) {
                    ERROR_INFO("MQTT: Invalid packet");
                    if (client->security) {
#ifdef MQTT_SSL_ENABLE
                        espconn_secure_disconnect(client->pCon);
#else
                        ERROR_INFO("TCP: Do not support SSL");
#endif
                    }
                    else {
                        espconn_disconnect(client->pCon);
                    }
                } else {
                    MQTT_INFO("MQTT: Connected to %s:%d", client->host, client->port);
                    mqtt_reconnect_counter = 0;
                    DEBUG_INFO("Resetting mqtt_reconnect_counter to 0");
                    client->connState = MQTT_DATA;
                    if (client->connectedCb)
                        client->connectedCb((uint32_t*)client);
                }

            }
            break;
        case MQTT_DATA:
        case MQTT_KEEPALIVE_SEND:
            client->mqtt_state.message_length_read = len;
            client->mqtt_state.message_length = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);


            switch (msg_type)
            {
                case MQTT_MSG_TYPE_SUBACK:
                    // PACKET print
                    sub_msg_qos = client->mqtt_state.in_buffer[4];
                    if (sub_msg_qos != 1)
                    {
                        for (b = 0; b < len; b++)
                            ERROR_INFO("\tclient->mqtt_state.in_buffer[%d]: %d\t(0x%02X)", b, client->mqtt_state.in_buffer[b], client->mqtt_state.in_buffer[b]);

                        ERROR_INFO("MQTT_MSG_TYPE_SUBACK | QoS: %d", sub_msg_qos);
                        if (sub_msg_qos == MQTT_SUBACK_ERROR)
                        {
                            // force client reconnection
                            ERROR_INFO("\n\nSUB_ACK_FAILED\n\n");
                            client->connState = SUB_ACK_FAILED;
                            break;
                        }
                    }
                    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_SUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id)
                    {
                        MQTT_INFO("MQTT: Subscribe successful");
                    }
                    break;
                case MQTT_MSG_TYPE_UNSUBACK:
                    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_UNSUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id)
                        MQTT_INFO("MQTT: UnSubscribe successful");
                    break;
                case MQTT_MSG_TYPE_PUBLISH:
                    if (msg_qos == 1)
                        client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
                    else if (msg_qos == 2)
                        client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);
                    if (msg_qos == 1 || msg_qos == 2) {
                        MQTT_INFO("MQTT: Queue response QoS: %d", msg_qos);
                        if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                            ERROR_INFO("MQTT: Queue full");
                        }
                    }

                    deliver_publish(client, client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
                    break;
                case MQTT_MSG_TYPE_PUBACK:
                    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id) {
                        MQTT_INFO("MQTT: received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish | pending_msg_id: %d", msg_id);
                        if (msg_id == disc_msg_id)
                            disconnected_for_reboot = TRUE;
                    }

                    break;
                case MQTT_MSG_TYPE_PUBREC:
                    client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, msg_id);
                    if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                        ERROR_INFO("MQTT: Queue full");
                    }
                    break;
                case MQTT_MSG_TYPE_PUBREL:
                    client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
                    if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                        ERROR_INFO("MQTT: Queue full");
                    }
                    break;
                case MQTT_MSG_TYPE_PUBCOMP:
                    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id) {
                        MQTT_INFO("MQTT: receive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish");
                    }
                    break;
                case MQTT_MSG_TYPE_PINGREQ:
                    client->mqtt_state.outbound_message = mqtt_msg_pingresp(&client->mqtt_state.mqtt_connection);
                    if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                        ERROR_INFO("MQTT: Queue full");
                    }
                    break;
                case MQTT_MSG_TYPE_PINGRESP:
                    MQTT_INFO("MQTT: MQTT_MSG_TYPE_PINGRESP received");
                    break;
            }
            // NOTE: this is done down here and not in the switch case above
            // because the PSOCK_READBUF_LEN() won't work inside a switch
            // statement due to the way protothreads resume.
            if (msg_type == MQTT_MSG_TYPE_PUBLISH)
            {
                len = client->mqtt_state.message_length_read;

                if (client->mqtt_state.message_length < client->mqtt_state.message_length_read)
                {
                    //client->connState = MQTT_PUBLISH_RECV;
                    //Not Implement yet
                    len -= client->mqtt_state.message_length;
                    pdata += client->mqtt_state.message_length;

                    MQTT_INFO("Get another published message");
                    goto READPACKET;
                }

            }
            break;
        }
    } else {
        ERROR_INFO("ERROR: Message too long");
    }
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}

/**
  * @brief  Client send over callback function.
  * @param  arg: contain the ip link information
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_sent_cb(void *arg)
{
    struct espconn *pCon = (struct espconn *)arg;
    MQTT_Client* client = (MQTT_Client *)pCon->reverse;
    MQTT_INFO("TCP: Sent");
    client->sendTimeout = 0;
    client->keepAliveTick = 0;

    if ((client->connState == MQTT_DATA || client->connState == MQTT_KEEPALIVE_SEND)
                && client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH) {
        if (client->publishedCb)
            client->publishedCb((uint32_t*)client);
    }
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}

void ICACHE_FLASH_ATTR mqtt_timer(void *arg)
{
    MQTT_Client* client = (MQTT_Client*)arg;

    if (client->connState == MQTT_DATA) {
        client->keepAliveTick ++;
        if (client->keepAliveTick > client->mqtt_state.connect_info->keepalive) {
            client->connState = MQTT_KEEPALIVE_SEND;
            system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
        }

    } else if (client->connState == TCP_RECONNECT_REQ) {
        client->reconnectTick ++;
        if (client->reconnectTick > MQTT_RECONNECT_TIMEOUT) {
            client->reconnectTick = 0;
            client->connState = TCP_RECONNECT;
            system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
            if (client->timeoutCb)
                client->timeoutCb((uint32_t*)client);
        }
    }
    if (client->sendTimeout > 0)
        client->sendTimeout --;
}

void ICACHE_FLASH_ATTR
mqtt_tcpclient_discon_cb(void *arg)
{

    struct espconn *pespconn = (struct espconn *)arg;
    MQTT_Client* client = (MQTT_Client *)pespconn->reverse;
    MQTT_INFO("TCP: Disconnected callback");
    disconnected_device_procedure(memory_deallocation_flag);
    if(TCP_DISCONNECTING == client->connState) {
        client->connState = TCP_DISCONNECTED;
    }
    else if(MQTT_DELETING == client->connState) {
        client->connState = MQTT_DELETED;
    }
    else {
        client->connState = TCP_RECONNECT_REQ;
    }
    if (client->disconnectedCb)
        client->disconnectedCb((uint32_t*)client);

    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}



/**
  * @brief  Tcp client connect success callback function.
  * @param  arg: contain the ip link information
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_connect_cb(void *arg)
{
    struct espconn *pCon = (struct espconn *)arg;
    MQTT_Client* client = (MQTT_Client *)pCon->reverse;

    espconn_regist_disconcb(client->pCon, mqtt_tcpclient_discon_cb);
    espconn_regist_recvcb(client->pCon, mqtt_tcpclient_recv);
    espconn_regist_sentcb(client->pCon, mqtt_tcpclient_sent_cb);
    MQTT_INFO("MQTT: Connected to broker %s:%d", client->host, client->port);

    mqtt_msg_init(&client->mqtt_state.mqtt_connection, client->mqtt_state.out_buffer, client->mqtt_state.out_buffer_length);
    client->mqtt_state.outbound_message = mqtt_msg_connect(&client->mqtt_state.mqtt_connection, client->mqtt_state.connect_info);
    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);

    client->sendTimeout = MQTT_SEND_TIMOUT;
    MQTT_INFO("MQTT: Sending, type: %d, id: %04X", client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
    if (client->security) {
#ifdef MQTT_SSL_ENABLE
        espconn_secure_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
#else
        ERROR_INFO("TCP: Do not support SSL");
#endif
    }
    else {
        espconn_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
    }

    client->mqtt_state.outbound_message = NULL;
    client->connState = MQTT_CONNECT_SENDING;
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}

/**
  * @brief  Tcp client connect repeat callback function.
  * @param  arg: contain the ip link information
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_recon_cb(void *arg, sint8 errType)
{
    t_Nvm_Mng_Data *dev_conf = Nvm_Mng_GetNvm();

    struct espconn *pCon = (struct espconn *)arg;
    MQTT_Client* client = (MQTT_Client *)pCon->reverse;

    MQTT_INFO("TCP: Reconnect to %s:%d", client->host, client->port);
    device_connected = FALSE;
    if (mqtt_reconnect_counter == MQTT_MAX_PAM_REC_COUNTER)
    {
        dev_conf->m_pam_mode = PAM_ENABLE;
        DEBUG_INFO("mqtt_tcpclient_recon_cb | Enabling PAM!");
    }
    else
    {
        DEBUG_INFO("mqtt_tcpclient_recon_cb | mqtt_reconnect_counter: %d/%d", mqtt_reconnect_counter, MQTT_MAX_PAM_REC_COUNTER);
        mqtt_reconnect_counter = min(mqtt_reconnect_counter + 1, MQTT_MAX_PAM_REC_COUNTER + 1);
    }

    client->connState = TCP_RECONNECT_REQ;

    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);

}

/**
  * @brief  MQTT publish function.
  * @param  client:     MQTT_Client reference
  * @param  topic:      string topic will publish to
  * @param  data:       buffer data send point to
  * @param  data_length: length of data
  * @param  qos:        qos
  * @param  retain:     retain
  * @retval TRUE if success queue
  */
BOOL ICACHE_FLASH_ATTR
MQTT_Publish(MQTT_Client *client, const char* topic, const char* data, int data_length, int qos, int retain)
{
    uint8_t dataBuffer[MQTT_BUF_SIZE];
    uint16_t dataLen;
    client->mqtt_state.outbound_message = mqtt_msg_publish(&client->mqtt_state.mqtt_connection,
                                          topic, data, data_length,
                                          qos, retain,
                                          &client->mqtt_state.pending_msg_id);
    if (client->mqtt_state.outbound_message->length == 0) {
        ERROR_INFO("MQTT: Queuing publish failed");
        return FALSE;
    }
    if (next_is_disconn_msg)
    {
        disc_msg_id = client->mqtt_state.pending_msg_id;
        os_printf("\n");
        os_printf("Queuing the following message as the disconnection one [msg_id: %d]: %s\n", disc_msg_id, data);
        os_printf("\n");
        next_is_disconn_msg = FALSE;
    }
    MQTT_INFO("MQTT: queuing publish, length: %d, queue size(%d/%d) | pending_msg_id: %d", client->mqtt_state.outbound_message->length, client->msgQueue.rb.fill_cnt, client->msgQueue.rb.size, client->mqtt_state.pending_msg_id);
    while (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
        ERROR_INFO("MQTT: Queue full");
        if (QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
            ERROR_INFO("MQTT: Serious buffer error. Setting sub_flag to FALSE");
            sub_flag = FALSE;
            return FALSE;
        }
    }
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
    return TRUE;
}

/**
  * @brief  MQTT subscibe function.
  * @param  client:     MQTT_Client reference
  * @param  topic:      string topic will subscribe
  * @param  qos:        qos
  * @retval TRUE if success queue
  */
BOOL ICACHE_FLASH_ATTR
MQTT_Subscribe(MQTT_Client *client, char* topic, uint8_t qos)
{
    uint8_t dataBuffer[MQTT_BUF_SIZE];
    uint16_t dataLen;

    client->mqtt_state.outbound_message = mqtt_msg_subscribe(&client->mqtt_state.mqtt_connection,
                                          topic, qos,
                                          &client->mqtt_state.pending_msg_id);
    MQTT_INFO("MQTT: queue subscribe, topic: \"%s\", id: %d", topic, client->mqtt_state.pending_msg_id);
    while (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
        ERROR_INFO("MQTT: Queue full");
        if (QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
            ERROR_INFO("MQTT: Serious buffer error. Setting sub_flag to FALSE");
            sub_flag = FALSE;
            return FALSE;
        }
    }
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
    return TRUE;
}

/**
  * @brief  MQTT un-subscibe function.
  * @param  client:     MQTT_Client reference
  * @param  topic:   String topic will un-subscribe
  * @retval TRUE if success queue
  */
BOOL ICACHE_FLASH_ATTR
MQTT_UnSubscribe(MQTT_Client *client, char* topic)
{
    uint8_t dataBuffer[MQTT_BUF_SIZE];
    uint16_t dataLen;
    client->mqtt_state.outbound_message = mqtt_msg_unsubscribe(&client->mqtt_state.mqtt_connection,
                                          topic,
                                          &client->mqtt_state.pending_msg_id);
    MQTT_INFO("MQTT: queue un-subscribe, topic\"%s\", id: %d", topic, client->mqtt_state.pending_msg_id);
    while (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
        ERROR_INFO("MQTT: Queue full");
        if (QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
            ERROR_INFO("MQTT: Serious buffer error. Setting sub_flag to FALSE");
            sub_flag = FALSE;
            return FALSE;
        }
    }
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
    return TRUE;
}

/**
  * @brief  MQTT ping function.
  * @param  client:     MQTT_Client reference
  * @retval TRUE if success queue
  */
BOOL ICACHE_FLASH_ATTR
MQTT_Ping(MQTT_Client *client)
{
    uint8_t dataBuffer[MQTT_BUF_SIZE];
    uint16_t dataLen;
    client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);
    if(client->mqtt_state.outbound_message->length == 0){
        ERROR_INFO("MQTT: Queuing publish failed");
        return FALSE;
    }
    MQTT_INFO("MQTT: queuing publish, length: %d, queue size(%d/%d)", client->mqtt_state.outbound_message->length, client->msgQueue.rb.fill_cnt, client->msgQueue.rb.size);
    while(QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1){
        ERROR_INFO("MQTT: Queue full");
        if(QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
            ERROR_INFO("MQTT: Serious buffer error. Setting sub_flag to FALSE");
            sub_flag = FALSE;
            return FALSE;
        }
    }
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
    return TRUE;
}

void ICACHE_FLASH_ATTR
MQTT_Task(os_event_t *e)
{
    //DEBUG_INFO("MQTT_Task");
    t_Nvm_Mng_Data *dev_conf = Nvm_Mng_GetNvm();

    MQTT_Client* client = (MQTT_Client*)e->par;
    uint8_t dataBuffer[MQTT_BUF_SIZE];
    uint16_t dataLen;
    static uint8_t mqtt_reconnect_counter = 0;

    if (e->par == 0)
        return;
    //MQTT_INFO("MQTT_Task | old_connState: %s | connState: %s", connState_string[old_connState], connState_string[client->connState]);

    if ((old_connState == TCP_RECONNECT_REQ) && (client->connState == TCP_RECONNECT))
    {
        phase_A = TRUE;
        phase_B = FALSE;
    }
    if ((phase_A == TRUE) && (old_connState == TCP_RECONNECT) && (client->connState == TCP_CONNECTING))
    {
        phase_A = FALSE;
        phase_B = TRUE;
    }
    if ((phase_B == TRUE) && (old_connState == TCP_CONNECTING) && (client->connState == TCP_RECONNECT_REQ))
    {
        phase_A = FALSE;
        phase_B = FALSE;
        dev_conf->m_mqtt_loop_counter++;
        MQTT_INFO("connState_loop_counter: %d", dev_conf->m_mqtt_loop_counter);
    }

    old_connState = client->connState;

    switch (client->connState) {

    case TCP_RECONNECT_REQ:
        break;
    case TCP_RECONNECT:
        mqtt_tcpclient_delete(client);
        MQTT_Connect(client);
        MQTT_INFO("TCP: Reconnect to: %s:%d", client->host, client->port);
        client->connState = TCP_CONNECTING;
        break;
    case MQTT_DELETING:
    case TCP_DISCONNECTING:
    case TCP_RECONNECT_DISCONNECTING:
        if (client->security) {
#ifdef MQTT_SSL_ENABLE
            espconn_secure_disconnect(client->pCon);
#else
            ERROR_INFO("TCP: Do not support SSL");
#endif
        }
        else {
            espconn_disconnect(client->pCon);
        }
        break;
    case TCP_DISCONNECTED:
        MQTT_INFO("MQTT: Disconnected");
        mqtt_tcpclient_delete(client);
        break;
    case MQTT_DELETED:
        MQTT_INFO("MQTT: Deleted client");
        mqtt_client_delete(client);
        break;
    case MQTT_KEEPALIVE_SEND:
        mqtt_send_keepalive(client);
        break;
    case MQTT_DATA:
        if (QUEUE_IsEmpty(&client->msgQueue) || client->sendTimeout != 0) {
            break;
        }
        if (QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == 0) {
            client->mqtt_state.pending_msg_type = mqtt_get_type(dataBuffer);
            client->mqtt_state.pending_msg_id = mqtt_get_id(dataBuffer, dataLen);


            client->sendTimeout = MQTT_SEND_TIMOUT;
            MQTT_INFO("MQTT: Sending, type: %d, id: %04X", client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
            client->keepAliveTick = 0;
            if (client->security) {
#ifdef MQTT_SSL_ENABLE
                espconn_secure_send(client->pCon, dataBuffer, dataLen);
#else
                ERROR_INFO("TCP: Do not support SSL");
#endif
            }
            else {
                espconn_send(client->pCon, dataBuffer, dataLen);
            }

            client->mqtt_state.outbound_message = NULL;
            break;
        }
        break;
    case SUB_ACK_FAILED:
        sub_ack_delayed_reconnect(client);
        break;
    }
}

/**
  * @brief  MQTT initialization connection function
  * @param  client:     MQTT_Client reference
  * @param  host:   Domain or IP string
  * @param  port:   Port to connect
  * @param  security:       1 for ssl, 0 for none
  * @retval None
  */
void ICACHE_FLASH_ATTR
MQTT_InitConnection(MQTT_Client *mqttClient, uint8_t* host, uint32_t port, uint8_t security)
{
    uint32_t temp;
    MQTT_INFO("MQTT_InitConnection");
    os_memset(mqttClient, 0, sizeof(MQTT_Client));
    temp = os_strlen(host);
    mqttClient->host = (uint8_t*)os_zalloc(temp + 1);
    os_strcpy(mqttClient->host, host);
    mqttClient->host[temp] = 0;
    mqttClient->port = port;
    mqttClient->security = security;

}

/**
  * @brief  MQTT initialization mqtt client function
  * @param  client:     MQTT_Client reference
  * @param  clientid:   MQTT client id
  * @param  client_user:MQTT client user
  * @param  client_pass:MQTT client password
  * @param  client_pass:MQTT keep alive timer, in second
  * @retval None
  */
void ICACHE_FLASH_ATTR
MQTT_InitClient(MQTT_Client *mqttClient, uint8_t* client_id, uint8_t* client_user, uint8_t* client_pass, uint32_t keepAliveTime, uint8_t cleanSession)
{
    uint32_t temp;
    char random_id[3];
    uint32_t rand_temp[4] = {0};
    MQTT_INFO("MQTT_InitClient");

    random_id_number[0] = os_random()%10;
    random_id_number[1] = os_random()%10;
    random_id_number[2] = os_random()%10;

    os_memset(&mqttClient->connect_info, 0, sizeof(mqtt_connect_info_t));

    os_sprintf(random_id, "%d%d%d", random_id_number[0], random_id_number[1], random_id_number[2]);
    temp = os_strlen(client_id);
    mqttClient->connect_info.client_id = (uint8_t*)os_zalloc(temp + 1);
    os_strcpy(mqttClient->connect_info.client_id, client_id);
    os_strcpy(session_id, random_id);
    mqttClient->connect_info.client_id[temp] = 0;
    os_strcpy(mqtt_client_id, mqttClient->connect_info.client_id);

    if (client_user)
    {
        temp = os_strlen(client_user);
        mqttClient->connect_info.username = (uint8_t*)os_zalloc(temp + 1);
        os_strcpy(mqttClient->connect_info.username, client_user);
        mqttClient->connect_info.username[temp] = 0;
    }

    if (client_pass)
    {
        temp = os_strlen(client_pass);
        mqttClient->connect_info.password = (uint8_t*)os_zalloc(temp + 1);
        os_strcpy(mqttClient->connect_info.password, client_pass);
        mqttClient->connect_info.password[temp] = 0;
    }


    mqttClient->connect_info.keepalive = keepAliveTime;
    mqttClient->connect_info.clean_session = cleanSession;

    mqttClient->mqtt_state.in_buffer = (uint8_t *)os_zalloc(MQTT_BUF_SIZE);
    mqttClient->mqtt_state.in_buffer_length = MQTT_BUF_SIZE;
    mqttClient->mqtt_state.out_buffer =  (uint8_t *)os_zalloc(MQTT_BUF_SIZE);
    mqttClient->mqtt_state.out_buffer_length = MQTT_BUF_SIZE;
    mqttClient->mqtt_state.connect_info = &mqttClient->connect_info;

    mqtt_msg_init(&mqttClient->mqtt_state.mqtt_connection, mqttClient->mqtt_state.out_buffer, mqttClient->mqtt_state.out_buffer_length);

    QUEUE_Init(&mqttClient->msgQueue, QUEUE_BUFFER_SIZE);

    system_os_task(MQTT_Task, MQTT_TASK_PRIO, mqtt_procTaskQueue, MQTT_TASK_QUEUE_SIZE);
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)mqttClient);
}
void ICACHE_FLASH_ATTR
MQTT_InitLWT(MQTT_Client *mqttClient, uint8_t* will_topic, uint8_t* will_msg, uint8_t will_qos, uint8_t will_retain)
{
    uint32_t temp;
    temp = os_strlen(will_topic);
    mqttClient->connect_info.will_topic = (uint8_t*)os_zalloc(temp + 1);
    os_strcpy(mqttClient->connect_info.will_topic, will_topic);
    mqttClient->connect_info.will_topic[temp] = 0;

    temp = os_strlen(will_msg);
    mqttClient->connect_info.will_message = (uint8_t*)os_zalloc(temp + 1);
    os_strcpy(mqttClient->connect_info.will_message, will_msg);
    mqttClient->connect_info.will_message[temp] = 0;


    mqttClient->connect_info.will_qos = will_qos;
    mqttClient->connect_info.will_retain = will_retain;
}
/**
  * @brief  Begin connect to MQTT broker
  * @param  client: MQTT_Client reference
  * @retval None
  */
void ICACHE_FLASH_ATTR
MQTT_Connect(MQTT_Client *mqttClient)
{
    if (mqttClient->pCon) {
        // Clean up the old connection forcefully - using MQTT_Disconnect
        // does not actually release the old connection until the
        // disconnection callback is invoked.
        mqtt_tcpclient_delete(mqttClient);
    }
    mqttClient->pCon = (struct espconn *)os_zalloc(sizeof(struct espconn));
    mqttClient->pCon->type = ESPCONN_TCP;
    mqttClient->pCon->state = ESPCONN_NONE;
    mqttClient->pCon->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
    mqttClient->pCon->proto.tcp->local_port = espconn_port();
    mqttClient->pCon->proto.tcp->remote_port = mqttClient->port;
    mqttClient->pCon->reverse = mqttClient;
    espconn_regist_connectcb(mqttClient->pCon, mqtt_tcpclient_connect_cb);
    espconn_regist_reconcb(mqttClient->pCon, mqtt_tcpclient_recon_cb);

    mqttClient->keepAliveTick = 0;
    mqttClient->reconnectTick = 0;


    os_timer_disarm(&mqttClient->mqttTimer);
    os_timer_setfn(&mqttClient->mqttTimer, (os_timer_func_t *)mqtt_timer, mqttClient);
    os_timer_arm(&mqttClient->mqttTimer, 1000, 1);

    if (UTILS_StrToIP(mqttClient->host, &mqttClient->pCon->proto.tcp->remote_ip)) {
        MQTT_INFO("TCP: Connect to ip  %s:%d", mqttClient->host, mqttClient->port);
        if (mqttClient->security)
        {
#ifdef MQTT_SSL_ENABLE
            espconn_secure_connect(mqttClient->pCon);
#else
            ERROR_INFO("TCP: Do not support SSL");
#endif
        }
        else
        {
            espconn_connect(mqttClient->pCon);
        }
    }
    else {
        MQTT_INFO("TCP: Connect to domain %s:%d", mqttClient->host, mqttClient->port);
        espconn_gethostbyname(mqttClient->pCon, mqttClient->host, &mqttClient->ip, mqtt_dns_found);
    }
    mqttClient->connState = TCP_CONNECTING;
}

void ICACHE_FLASH_ATTR
MQTT_Disconnect(MQTT_Client *mqttClient)
{
    mqttClient->connState = TCP_DISCONNECTING;
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)mqttClient);
    os_timer_disarm(&mqttClient->mqttTimer);
}

void ICACHE_FLASH_ATTR
MQTT_DeleteClient(MQTT_Client *mqttClient)
{
    mqttClient->connState = MQTT_DELETING;
    system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)mqttClient);
    os_timer_disarm(&mqttClient->mqttTimer);
}

void ICACHE_FLASH_ATTR
MQTT_OnConnected(MQTT_Client *mqttClient, MqttCallback connectedCb)
{
    mqttClient->connectedCb = connectedCb;
}

void ICACHE_FLASH_ATTR
MQTT_OnDisconnected(MQTT_Client *mqttClient, MqttCallback disconnectedCb)
{
    mqttClient->disconnectedCb = disconnectedCb;
}

void ICACHE_FLASH_ATTR
MQTT_OnData(MQTT_Client *mqttClient, MqttDataCallback dataCb)
{
    mqttClient->dataCb = dataCb;
}

void ICACHE_FLASH_ATTR
MQTT_OnPublished(MQTT_Client *mqttClient, MqttCallback publishedCb)
{
    mqttClient->publishedCb = publishedCb;
}

void ICACHE_FLASH_ATTR
MQTT_OnTimeout(MQTT_Client *mqttClient, MqttCallback timeoutCb)
{
    mqttClient->timeoutCb = timeoutCb;
}
