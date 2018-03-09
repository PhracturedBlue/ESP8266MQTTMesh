#ifndef _ESP8266MQTTMESH_H_
#define _ESP8266MQTTMESH_H_

#if ! defined(MQTT_MAX_PACKET_SIZE)
    #define MQTT_MAX_PACKET_SIZE 1152
#endif
#if  ! defined(ESP8266MESHMQTT_DISABLE_OTA) && ! defined(ESP32)
    //By default we support OTA
    #if ! defined(MQTT_MAX_PACKET_SIZE) || MQTT_MAX_PACKET_SIZE < (1024+128)
        #error "Must define MQTT_MAX_PACKET_SIZE >= 1152"
    #endif
    #define HAS_OTA 1
#else
    #define HAS_OTA 0
#endif

#include <Arduino.h>

#ifdef ESP32
  #include <AsyncTCP.h>
  #include <ESP32Ticker.h>
  #define USE_WIFI_ONEVENT
  #include "WiFiCompat.h"
#else
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
  #include <Ticker.h>
#endif

#include <AsyncMqttClient.h>
#include <FS.h>
#include <functional>

#ifdef ESP32
  #define _chipID ((unsigned long)ESP.getEfuseMac())
#else
  #define _chipID ESP.getChipId()
#endif

#define TOPIC_LEN 64

#define EMMDBG_EXTRA         0x10000000
#define EMMDBG_MSG           0x00000001
#define EMMDBG_MSG_EXTRA     (EMMDBG_EXTRA | EMMDBG_MSG)
#define EMMDBG_WIFI          0x00000002
#define EMMDBG_WIFI_EXTRA    (EMMDBG_EXTRA | EMMDBG_WIFI)
#define EMMDBG_MQTT          0x00000004
#define EMMDBG_MQTT_EXTRA    (EMMDBG_EXTRA | EMMDBG_MQTT)
#define EMMDBG_OTA           0x00000008
#define EMMDBG_OTA_EXTRA     (EMMDBG_EXTRA | EMMDBG_OTA)
#define EMMDBG_TIMING        0x00000010
#define EMMDBG_TIMING_EXTRA  (EMMDBG_EXTRA | EMMDBG_TIMING)
#define EMMDBG_FS            0x00000020
#define EMMDBG_FS_EXTRA      (EMMDBG_EXTRA | EMMDBG_OTA)
#define EMMDBG_ALL           0x8FFFFFFF
#define EMMDBG_ALL_EXTRA     0xFFFFFFFF
#define EMMDBG_NONE          0x00000000

#ifndef ESP8266_NUM_CLIENTS
  #define ESP8266_NUM_CLIENTS 4
#endif

enum MSG_TYPE {
    MSG_TYPE_NONE = 0xFE,
    MSG_TYPE_INVALID = 0xFF,
    MSG_TYPE_QOS_0 = 10,
    MSG_TYPE_QOS_1 = 11,
    MSG_TYPE_QOS_2 = 12,
    MSG_TYPE_RETAIN_QOS_0 = 13,
    MSG_TYPE_RETAIN_QOS_1 = 14,
    MSG_TYPE_RETAIN_QOS_2 = 15,
};


typedef struct {
    const uint8_t *cert;
    const uint8_t *key;
    const uint8_t *fingerprint;
    uint32_t cert_len;
    uint32_t key_len;
} ssl_cert_t;

typedef struct {
    unsigned int len;
    byte         md5[16];
} ota_info_t;

typedef struct ap_t {
    struct ap_t *next;
    int32_t rssi;
    uint8_t bssid[6];
    int16_t ssid_idx;
} ap_t;

typedef struct {
    const char *ssid;
    const char *password;
    const char *bssid;
    bool hidden;
} wifi_conn;
#define WIFI_CONN(ssid, password, bssid, hidden) \
    { ssid, password, bssid, hidden }

class ESP8266MQTTMesh {
public:
    class Builder;
private:
    const unsigned int firmware_id;
    const char   *firmware_ver;
    const wifi_conn *networks;

    const char   *mesh_ssid;
    char         mesh_password[64];
    const char   *mqtt_server;
    const char   *mqtt_username;
    const char   *mqtt_password;
    const int    mqtt_port;
    const int    mesh_port;
    uint32_t     mesh_bssid_key;

    const char   *inTopic;
    const char   *outTopic;
#if HAS_OTA
    uint32_t freeSpaceStart;
    uint32_t freeSpaceEnd;
    uint32_t nextErase;
    uint32_t startTime;
    ota_info_t ota_info;
#endif
#if ASYNC_TCP_SSL_ENABLED
    bool mqtt_secure;
    ssl_cert_t mesh_secure;
    const uint8_t *mqtt_fingerprint;
#endif
    AsyncServer     espServer;
    AsyncClient     *espClient[ESP8266_NUM_CLIENTS+1] = {0};
    uint8_t         espMAC[ESP8266_NUM_CLIENTS+1][6];
    AsyncMqttClient mqttClient;

    Ticker schedule;

    int retry_connect;
    ap_t *ap = NULL;
    ap_t *ap_ptr;
    ap_t *ap_unused = NULL;
    char myID[10];
    char inbuffer[ESP8266_NUM_CLIENTS+1][MQTT_MAX_PACKET_SIZE];
    char *bufptr[ESP8266_NUM_CLIENTS+1];
    long lastMsg = 0;
    char msg[50];
    int value = 0;
    bool meshConnect = false;
    unsigned long lastReconnect = 0;
    unsigned long lastStatus = 0;
    bool connecting = 0;
    bool scanning = 0;
    bool AP_ready = false;
    std::function<void(const char *topic, const char *msg)> callback;

    bool wifiConnected() { return (WiFi.status() == WL_CONNECTED); }
    void die() { while(1) {} }

    uint32_t lfsr(uint32_t seed, uint8_t b);
    uint32_t encrypt_id(uint32_t id);
    void generate_mac(uint8_t *bssid, uint32_t id);
    bool verify_bssid(uint8_t *bssid);

    int match_networks(const char *ssid, const char *bssid);
    void scan();
    void connect();
    static void connect(ESP8266MQTTMesh *e) { e->connect(); };
    String mac_str(uint8_t *bssid);
    const char *build_mesh_ssid(char buf[32], uint8_t *mac);
    void schedule_connect(float delay = 5.0);
    void connect_mqtt();
    void shutdown_AP();
    void setup_AP();
    void handle_client_data(int idx, char *data);
    void parse_message(const char *topic, const char *msg);
    void mqtt_callback(const char* topic, const byte* payload, unsigned int length);
    uint16_t mqtt_publish(const char *topic, const char *msg, uint8_t msgType);
    void publish(const char *topicDirection, const char *baseTopic, const char *subTopic, const char *msg, uint8_t msgType);
    bool send_message(int index, const char *topicOrMsg, const char *msg = NULL, uint8_t msgType = MSG_TYPE_NONE);
    void send_messages();
    void send_connected_msg();
    void broadcast_message(const char *topicOrMsg, const char *msg = NULL);
    void get_fw_string(char *msg, int len, const char *prefix);
    void handle_fw(const char *cmd);
    void handle_ota(const char *cmd, const char *msg);
    void parse_ota_info(const char *str);
    char * md5(const uint8_t *msg, int len);
    bool check_ota_md5();
    void assign_subdomain();
    static void assign_subdomain(ESP8266MQTTMesh *e) { e->assign_subdomain(); };
    void erase_sector();
    static void erase_sector(ESP8266MQTTMesh *e) { e->erase_sector(); };

    void connectWiFiEvents();

#ifndef USE_WIFI_ONEVENT
    WiFiEventHandler wifiConnectHandler;
    WiFiEventHandler wifiDisconnectHandler;
    WiFiEventHandler wifiAPConnectHandler;
    WiFiEventHandler wifiAPDisconnectHandler;
#endif

    void onWifiConnect(const WiFiEventStationModeGotIP& event);
    void onWifiDisconnect(const WiFiEventStationModeDisconnected& event);
    //void onDHCPTimeout();
    void onAPConnect(const WiFiEventSoftAPModeStationConnected& ip);
    void onAPDisconnect(const WiFiEventSoftAPModeStationDisconnected& ip);

    void onMqttConnect(bool sessionPresent);
    void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
    void onMqttSubscribe(uint16_t packetId, uint8_t qos);
    void onMqttUnsubscribe(uint16_t packetId);
    void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
    void onMqttPublish(uint16_t packetId);

    int onSslFileRequest(const char *filename, uint8_t **buf);
    void onClient(AsyncClient* c);
    void onConnect(AsyncClient* c);
    void onDisconnect(AsyncClient* c);
    void onError(AsyncClient* c, int8_t error);
    void onAck(AsyncClient* c, size_t len, uint32_t time);
    void onTimeout(AsyncClient* c, uint32_t time);
    void onData(AsyncClient* c, void* data, size_t len);

    ESP8266MQTTMesh(const wifi_conn *networks,
                    const char *mqtt_server, int mqtt_port,
                    const char *mqtt_username, const char *mqtt_password,
                    const char *firmware_ver, int firmware_id,
                    const char *mesh_ssid, const char *mesh_password, int mesh_port,
#if ASYNC_TCP_SSL_ENABLED
                    bool mqtt_secure, const uint8_t *mqtt_fingerprint, ssl_cert_t mesh_secure,
#endif
                    const char *inTopic, const char *outTopic);
public:
    void setCallback(std::function<void(const char *topic, const char *msg)> _callback);
    void begin();
    void publish(const char *subtopic, const char *msg, enum MSG_TYPE msgCmd = MSG_TYPE_NONE);
    void publish_node(const char *subtopic, const char *msg, enum MSG_TYPE msgCmd = MSG_TYPE_NONE);
    bool connected();
    static bool keyValue(const char *data, char separator, char *key, int keylen, const char **value);
#ifdef USE_WIFI_ONEVENT
    void WiFiEventHandler(system_event_id_t event, system_event_info_t info);
#endif
};

#include "ESP8266MQTTMeshBuilder.h"

#endif //_ESP8266MQTTMESH_H_
