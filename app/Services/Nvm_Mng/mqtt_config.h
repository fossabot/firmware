#ifndef __MQTT_CONFIG_H__
#define __MQTT_CONFIG_H__

/* Last was 4 */
#define CFG_HOLDER 100 /*0xAEADBEEC  Change this value to load default configurations */

#define MQTT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

#define MQTT_HOST                   "a1fa17u1nzrgbv.iot.eu-central-1.amazonaws.com" //or "mqtt.yourdomain.com"
#define MQTT_PORT                   8883
#define MQTT_BUF_SIZE               2048
#define MQTT_KEEPALIVE_DEFAULT      42      /* seconds */
#define MQTT_KEEPALIVE_MIN          30      /* seconds */
#define MQTT_KEEPALIVE_MAX          1200    /* seconds */

///#define MQTT_CLIENT_ID       "a020a6%06x\0"
#define MQTT_CLIENT_ID      "%06x\0"
#define MQTT_USER           "\0"
#define MQTT_PASS           "\0"

#define STA_SSID "DEFAULT_SSID"
#define STA_PASS "default_pass"
#define STA_TYPE AUTH_WPA2_PSK

#define MQTT_RECONNECT_TIMEOUT  5   /*second*/

#define DEFAULT_SECURITY    1
#define QUEUE_BUFFER_SIZE               1536

#define PAM_ENABLE  0
#define PAM_DISABLE 1

//#define PROTOCOL_NAMEv31    /* MQTT version 3.1 compatible with Mosquitto v0.15 */
#define PROTOCOL_NAMEv311         /* MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing */

#endif // __MQTT_CONFIG_H__
