#ifndef _ESP8266MQTTMESH_H_
#define _ESP8266MQTTMESH_H_

#if  ! defined(ESP8266MESHMQTT_DISABLE_OTA)
    #define HAS_OTA 1
    //By default we support OTA
    #define MQTT_MAX_PACKET_SIZE (1024 + 128)
#else
    #define HAS_OTA 0
#endif

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FS.h>
#include <functional>

#define TOPIC_LEN 32

typedef struct {
    unsigned int id;
    unsigned int len;
    byte         md5[16];
} ota_info_t;

class ESP8266MQTTMesh {
private:
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
    void mqtt_callback(char* topic, byte* payload, unsigned int length);
    void send_message(IPAddress ip, const char *msg);
    void broadcast_message(const char *msg);
    void handle_ota(const char *cmd, const char *msg);
    ota_info_t parse_ota_info(const char *str);
    bool check_ota_md5();
public:
    ESP8266MQTTMesh(const char **networks, const char *network_password, const char *mesh_password,
                    const char *base_ssid, const char *mqtt_server, int mqtt_port, int mesh_port,
                    const char *inTopic, const char *outTopic);
    void setCallback(std::function<void(const char *topic, const char *msg)> _callback);
    void setup();
    void loop();
    void publish(const char *subtopic, const char *msg);
    static bool keyValue(const char *data, char separator, char *key, int keylen, const char **value);
    static int  read_until(Stream &f, char *buf, char delim, int len);
};
#endif //_ESP8266MQTTMESH_H_
