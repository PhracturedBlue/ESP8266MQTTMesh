#ifndef _ESP8266MQTTMESHBUILDER_H_
#define _ESP8266MQTTMESHBUILDER_H_

class ESP8266MQTTMesh::Builder {
private:
    const wifi_conn *networks;
    const char   *network_password;

    const char   *mqtt_server;
    int          mqtt_port;
    const char   *mqtt_username;
    const char   *mqtt_password;
 
    const char   *mesh_password;
    const char   *base_ssid;
    int          mesh_port;

    const char   *inTopic;
    const char   *outTopic;

    unsigned int firmware_id;
    const char   *firmware_ver;
#if ASYNC_TCP_SSL_ENABLED
    bool mqtt_secure;
    bool mesh_secure;
    const uint8_t *mqtt_fingerprint;
#endif

public:
    Builder(const wifi_conn *networks,
            const char   *network_password,
            const char   *mqtt_server,
            int          mqtt_port = 0):
       networks(networks),
       network_password(network_password),
       mqtt_server(mqtt_server),
       mqtt_port(mqtt_port),
       mqtt_username(NULL),
       mqtt_password(NULL),
       firmware_id(0),
       firmware_ver(NULL),
       mesh_password("ESP8266MQTTMesh"),
       base_ssid("mesh_esp8266-"),
       mesh_port(1884),
#if ASYNC_TCP_SSL_ENABLED
       mqtt_secure(false),
       mqtt_fingerprint(NULL),
       mesh_secure(false),
#endif
       inTopic("esp8266-in/"),
       outTopic("esp8266-out/")
       
       {}
    Builder& setVersion(const char *firmware_ver, int firmware_id) {
        this->firmware_id = firmware_id;
        this->firmware_ver = firmware_ver;
        return *this;
    }
    Builder& setMqttAuth(const char *username, const char *password) {
        this->mqtt_username = username;
        this->mqtt_password = password;
        return *this;
    }
    Builder& setMeshPassword(const char *password) { this->mesh_password = password; return *this; }
    Builder& setBaseSSID(const char *ssid) { this->base_ssid = ssid; return *this; }
    Builder& setMeshPort(int port) { this->mesh_port = port; return *this; }
    Builder& setTopic(const char *inTopic, const char *outTopic) {
        this->inTopic = inTopic;
        this->outTopic = outTopic;
        return *this;
    }
#if ASYNC_TCP_SSL_ENABLED
    Builder& setMqttSSL(bool enable, const uint8_t *fingerprint) {
        this->mqtt_secure = enable;
        this->mqtt_fingerprint = fingerprint;
        return *this;
    }
    Builder & setMeshSSL(bool enable) { this->mesh_secure = enable; return *this; }
#endif
    ESP8266MQTTMesh build() {
        return( ESP8266MQTTMesh(
            networks,
            network_password,

            mqtt_server,
            mqtt_port,
            mqtt_username,
            mqtt_password,

            firmware_ver,
            firmware_id,

            mesh_password,
            base_ssid,
            mesh_port,

#if ASYNC_TCP_SSL_ENABLED
            mqtt_secure,
            mqtt_fingerprint,
            mesh_secure,
#endif

            inTopic,
            outTopic));
    }
    ESP8266MQTTMesh *buildptr() {
        return( new ESP8266MQTTMesh(
            networks,
            network_password,

            mqtt_server,
            mqtt_port,
            mqtt_username,
            mqtt_password,

            firmware_ver,
            firmware_id,

            mesh_password,
            base_ssid,
            mesh_port,

#if ASYNC_TCP_SSL_ENABLED
            mqtt_secure,
            mqtt_fingerprint,
            mesh_secure,
#endif

            inTopic,
            outTopic));
    }
};
#endif //_ESP8266MQTTMESHBUILDER_H_
