#ifndef _ESP8266MQTTMESH_H_
#define _ESP8266MQTTMESH_H_

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
#include <PubSubClient.h>
#include <FS.h>
#include <functional>

#define TOPIC_LEN 32

#define DEBUG_EXTRA         0x10000000
#define DEBUG_MSG           0x00000001
#define DEBUG_MSG_EXTRA     (DEBUG_EXTRA | DEBUG_MSG)
#define DEBUG_WIFI          0x00000002
#define DEBUG_WIFI_EXTRA    (DEBUG_EXTRA | DEBUG_WIFI)
#define DEBUG_MQTT          0x00000004
#define DEBUG_MQTT_EXTRA    (DEBUG_EXTRA | DEBUG_MQTT)
#define DEBUG_OTA           0x00000008
#define DEBUG_OTA_EXTRA     (DEBUG_EXTRA | DEBUG_OTA)
#define DEBUG_ALL           0xFFFFFFFF
#define DEBUG_NONE          0x00000000

typedef struct {
    unsigned int len;
    byte         md5[16];
} ota_info_t;

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
    WiFiClient espClient;
    WiFiServer espServer;
    PubSubClient mqttClient;
    char mySSID[16];
    char buffer[MQTT_MAX_PACKET_SIZE];
    long lastMsg = 0;
    char msg[50];
    int value = 0;
    bool meshConnect = false;
    unsigned long lastReconnect = 0;
    bool AP_ready = false;
    std::function<void(const char *topic, const char *msg)> callback;

    bool connected() { return (WiFi.status() == WL_CONNECTED); }
    void die() { while(1) {} }

    bool match_bssid(const char *bssid);
    void connect();
    void connect_mqtt();
    void setup_AP();
    int read_subdomain(const char *fileName);
    void assign_subdomain();
    void send_bssids(IPAddress ip);
    void handle_client_connection(WiFiClient client);
    void parse_message(const char *topic, const char *msg);
    void mqtt_callback(const char* topic, const byte* payload, unsigned int length);
    void send_message(IPAddress ip, const char *topicOrMsg, const char *msg = NULL);
    void broadcast_message(const char *msg);
    void handle_ota(const char *cmd, const char *msg);
    ota_info_t parse_ota_info(const char *str);
    bool check_ota_md5();
public:
    ESP8266MQTTMesh(unsigned int firmware_id, const char *firmware_ver,
                    const char **networks, const char *network_password, const char *mesh_password,
                    const char *base_ssid, const char *mqtt_server, int mqtt_port, int mesh_port,
                    const char *inTopic, const char *outTopic);
    void setCallback(std::function<void(const char *topic, const char *msg)> _callback);
    void setup();
    void loop();
    void publish(const char *subtopic, const char *msg);
    static bool keyValue(const char *data, char separator, char *key, int keylen, const char **value);
};
#endif //_ESP8266MQTTMESH_H_
