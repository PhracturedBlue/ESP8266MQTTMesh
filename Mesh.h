#ifndef _MESH_H_
#define _MESH_H_

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FS.h>

class Mesh {
private:
    WiFiClient espClient;
    WiFiServer espServer;
    PubSubClient mqttClient;
    String mySSID = "";
    long lastMsg = 0;
    char msg[50];
    int value = 0;
    bool meshConnect = false;
    unsigned long lastReconnect = 0;
    bool AP_ready = false;

    bool connected() { return (WiFi.status() == WL_CONNECTED); }
    void die() { while(1) {} }

    String getValue(String data, char separator, int index);
    bool match_bssid(String bssid);
    void connect();
    void connect_mqtt();
    void publish(String subtopic, String msg);
    void setup_AP();
    int read_subdomain(String fileName);
    void assign_subdomain();
    void handle_client_connection(WiFiClient client);
    static void callback(char* topic, byte* payload, unsigned int length);
    static void send_message(IPAddress ip, String msg);
    static void broadcast_message(String msg);
public:
    Mesh();
    void setup();
    void loop();
};
#endif //_MESH_H_
