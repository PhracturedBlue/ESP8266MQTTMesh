#ifndef _ESP8266MQTTMESH_H_
#define _ESP8266MQTTMESH_H_

#if ! defined(MQTT_MAX_PACKET_SIZE)
    #define MQTT_MAX_PACKET_SIZE 1152
#endif
#if  ! defined(ESP8266MESHMQTT_DISABLE_OTA)
    //By default we support OTA
    #if ! defined(MQTT_MAX_PACKET_SIZE) || MQTT_MAX_PACKET_SIZE < (1024+128)
        #error "Must define MQTT_MAX_PACKET_SIZE >= 1152"
    #endif
    #define HAS_OTA 1
#else
    #define HAS_OTA 0
#endif

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>
#include <FS.h>
#include <functional>

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

typedef struct {
    unsigned int len;
    byte         md5[16];
} ota_info_t;

typedef struct {
    char bssid[19];
    int  ssid_idx;
    int  rssi;
} ap_t;
#define LAST_AP 5

class ESP8266MQTTMesh {

private:
    unsigned int firmware_id;
    const char   *firmware_ver;
    const char   **networks;
    const char   *network_password;
    const char   *mesh_password;
    const char   *base_ssid;
    const char   *mqtt_server;
    const int    mqtt_port;
    const int    mesh_port;
    const char   *inTopic;
    const char   *outTopic;
#if HAS_OTA
    uint32_t freeSpaceStart;
    uint32_t freeSpaceEnd;
#endif
#if ASYNC_TCP_SSL_ENABLED
    bool mqtt_secure;
    bool mesh_secure;
    const uint8_t *mqtt_fingerprint;
#endif
    AsyncServer     espServer;
    AsyncClient     *espClient[ESP8266_NUM_CLIENTS+1];
    uint8           espMAC[ESP8266_NUM_CLIENTS+1][6];
    AsyncMqttClient mqttClient;

    Ticker schedule;

    int retry_connect;
    ap_t ap[LAST_AP];
    int ap_idx = 0;
    char mySSID[16];
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

    bool match_bssid(const char *bssid);
    void scan();
    void connect();
    static void connect(ESP8266MQTTMesh *e) { e->connect(); };
    void schedule_connect(float delay = 5.0);
    void connect_mqtt();
    void shutdown_AP();
    void setup_AP();
    int read_subdomain(const char *fileName);
    void send_bssids(int idx);
    void handle_client_data(int idx, char *data);
    void parse_message(const char *topic, const char *msg);
    void mqtt_callback(const char* topic, const byte* payload, unsigned int length);
    bool send_message(int index, const char *topicOrMsg, const char *msg = NULL);
    void send_messages();
    void broadcast_message(const char *topicOrMsg, const char *msg = NULL);
    void handle_ota(const char *cmd, const char *msg);
    ota_info_t parse_ota_info(const char *str);
    bool check_ota_md5();
    bool isAPConnected(uint8 *mac);
    void getMAC(IPAddress ip, uint8 *mac);
    void assign_subdomain();
    static void assign_subdomain(ESP8266MQTTMesh *e) { e->assign_subdomain(); };

    WiFiEventHandler wifiConnectHandler;
    WiFiEventHandler wifiDisconnectHandler;
    //WiFiEventHandler wifiDHCPTimeoutHandler;
    WiFiEventHandler wifiAPConnectHandler;
    WiFiEventHandler wifiAPDisconnectHandler;

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
public:
    ESP8266MQTTMesh(unsigned int firmware_id, const char *firmware_ver,
                    const char **networks, const char *network_password, const char *mesh_password,
                    const char *base_ssid, const char *mqtt_server, int mqtt_port, int mesh_port,
                    const char *inTopic, const char *outTopic
#if ASYNC_TCP_SSL_ENABLED
                    , bool mqtt_secure = false,
                    const uint8_t *mqtt_fingerprint = NULL,
                    bool mesh_secure = false
#endif
                    );
    void setCallback(std::function<void(const char *topic, const char *msg)> _callback);
    void begin();
    void publish(const char *subtopic, const char *msg);
    bool connected();
    static bool keyValue(const char *data, char separator, char *key, int keylen, const char **value);
};
#endif //_ESP8266MQTTMESH_H_
