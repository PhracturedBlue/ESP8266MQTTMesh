#ifndef _ESP8266MQTTMESHBUILDER_H_
#define _ESP8266MQTTMESHBUILDER_H_

class ESP8266MQTTMesh::Builder {
private:
    const wifi_conn *networks;

    const char   *mqtt_server;
    int          mqtt_port;
    const char   *mqtt_username;
    const char   *mqtt_password;
 
    const char   *mesh_ssid;
    const char   *mesh_password;
    int          mesh_port;

    const char   *inTopic;
    const char   *outTopic;

    unsigned int firmware_id;
    const char   *firmware_ver;
#if ASYNC_TCP_SSL_ENABLED
    bool mqtt_secure;
    ssl_cert_t mesh_secure;
    const uint8_t *mqtt_fingerprint;
    void fix_mqtt_port() { if (! mqtt_port) mqtt_port = mqtt_secure ? 8883 : 1883; };
#else
    void fix_mqtt_port() { if (! mqtt_port) mqtt_port = 1883; };
#endif

public:
    Builder(const wifi_conn *networks,
            const char   *mqtt_server,
            int          mqtt_port = 0):
       networks(networks),
       mqtt_server(mqtt_server),
       mqtt_port(mqtt_port),
       mqtt_username(NULL),
       mqtt_password(NULL),
       firmware_id(0),
       firmware_ver(NULL),
       mesh_ssid("esp8266_mqtt_mesh"),
       mesh_password("ESP8266MQTTMesh"),
       mesh_port(1884),
#if ASYNC_TCP_SSL_ENABLED
       mqtt_secure(false),
       mqtt_fingerprint(NULL),
       mesh_secure({NULL, NULL, NULL, 0, 0}),
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
    Builder& setMeshSSID(const char *ssid) { this->mesh_ssid = ssid; return *this; }
    Builder& setMeshPassword(const char *password) { this->mesh_password = password; return *this; }
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
    Builder & setMeshSSL(const uint8_t *ssl_cert, uint32_t ssl_cert_len,
                         const uint8_t *ssl_key, uint32_t ssl_key_len,
                         const uint8_t *ssl_fingerprint) {
        this->mesh_secure.cert = ssl_cert;
        this->mesh_secure.key = ssl_key;
        this->mesh_secure.fingerprint = ssl_fingerprint;
        this->mesh_secure.cert_len = ssl_cert_len;
        this->mesh_secure.key_len = ssl_key_len;
        return *this;
    }
#endif
    ESP8266MQTTMesh build() {
        fix_mqtt_port();
        return( ESP8266MQTTMesh(
            networks,

            mqtt_server,
            mqtt_port,
            mqtt_username,
            mqtt_password,

            firmware_ver,
            firmware_id,

            mesh_ssid,
            mesh_password,
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
        fix_mqtt_port();
        return( new ESP8266MQTTMesh(
            networks,

            mqtt_server,
            mqtt_port,
            mqtt_username,
            mqtt_password,

            firmware_ver,
            firmware_id,

            mesh_ssid,
            mesh_password,
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
