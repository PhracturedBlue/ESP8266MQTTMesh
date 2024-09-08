/*
 *  Copyright (C) 2016 PhracturedBlue
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ESP8266MQTTMesh.h"

#define MESH_API_VER "001"

#include "Base64.h"

#include <limits.h>
#include <string>

#if HAS_OTA
extern "C" {
  #include "eboot_command.h"
  #include "user_interface.h"
  extern uint32_t _SPIFFS_start;
}
#endif

#if !defined(ESP32) && ! defined(pgm_read_with_offset) //Requires Arduino core 2.4.0
    #error "This version of the ESP8266 library is not supported"
#endif

enum {
    NETWORK_MESH_NODE  = -1,
};

//Define GATEWAY_ID to the value of ESP.getChipId() in order to prevent only a specific node from connecting via MQTT
#ifdef GATEWAY_ID
    #define IS_GATEWAY (_chipID == GATEWAY_ID)
#else
    #define IS_GATEWAY (1)
#endif

//#define EMMDBG_LEVEL (EMMDBG_WIFI | EMMDBG_MQTT | EMMDBG_OTA)
#ifndef EMMDBG_LEVEL
  #define EMMDBG_LEVEL EMMDBG_ALL_EXTRA
#endif

#define dbgPrintln(lvl, msg)               \
    if (((lvl) & (EMMDBG_LEVEL)) == (lvl)) \
    Serial.println(msg);
    //Serial.println(String("[") + __FUNCTION__ + String("] ") + msg);

size_t mesh_strlcat(char* dst, const char* src, size_t len)
{
    size_t slen = strlen(dst);
    return strlcpy(dst + slen, src, len - slen);
}
#define strlcat mesh_strlcat


ESP8266MQTTMesh::ESP8266MQTTMesh(const wifi_conn *networks,
                    const char *mqtt_server, int mqtt_port,
                    const char *mqtt_username, const char *mqtt_password,
                    const char *firmware_ver, int firmware_id,
                    const char *mesh_ssid, const char *_mesh_password, int mesh_port,
#if ASYNC_TCP_SSL_ENABLED
                    bool mqtt_secure, const uint8_t *mqtt_fingerprint, ssl_cert_t mesh_secure,
#endif
                    const char *inTopic, const char *outTopic
                    ) :
        firmware_id(firmware_id),
        firmware_ver(firmware_ver),
        networks(networks),
        mesh_ssid(mesh_ssid),
        mqtt_server(mqtt_server),
        mqtt_username(mqtt_username),
        mqtt_password(mqtt_password),
        mqtt_port(mqtt_port),
        mesh_port(mesh_port),
#if ASYNC_TCP_SSL_ENABLED
        mqtt_secure(mqtt_secure),
        mqtt_fingerprint(mqtt_fingerprint),
        mesh_secure(mesh_secure),
#endif
        inTopic(inTopic),
        outTopic(outTopic),
        espServer(mesh_port)
{
    strlcpy(mesh_password, _mesh_password, 64-strlen(MESH_API_VER));
    strlcat(mesh_password, MESH_API_VER, 64);
    mesh_bssid_key = 0x118d5b; //Seed
    for (int i = 0; mesh_password[i] != 0; i++) {
        mesh_bssid_key = lfsr(mesh_bssid_key, mesh_password[i]);
    }
    espClient[0] = new AsyncClient();
    String tmp = String(_chipID, HEX);
    tmp.toUpperCase();
    while (tmp.length() < 6)
        tmp = "0" + tmp;
    strlcpy(myID, (tmp + "/").c_str(), sizeof(myID));
    
    strlcpy(availableTopic, outTopic, sizeof(availableTopic));
    strlcat(availableTopic, myID, sizeof(availableTopic));
    strlcat(availableTopic, "info/available", sizeof(availableTopic));
    
#if HAS_OTA
    uint32_t usedSize = ESP.getSketchSize();
    // round one sector up
    freeSpaceStart = (usedSize + FLASH_SECTOR_SIZE - 1) & (~(FLASH_SECTOR_SIZE - 1));
    //freeSpaceEnd = (uint32_t)&_SPIFFS_start - 0x40200000;
    freeSpaceEnd = ESP.getFreeSketchSpace() + freeSpaceStart;
#endif
}

void ESP8266MQTTMesh::setCallback(std::function<void(const char *topic, const char *msg)> _callback) {
    callback = _callback;
}

void ESP8266MQTTMesh::begin() {
    int len = strlen(inTopic);
    if (len > 16) {
        dbgPrintln(EMMDBG_MSG, "Max inTopicLen == 16");
        die();
    }
    if (inTopic[len-1] != '/') {
        dbgPrintln(EMMDBG_MSG, "inTopic must end with '/'");
        die();
    }
    len = strlen(outTopic);
    if (len > 16) {
        dbgPrintln(EMMDBG_MSG, "Max outTopicLen == 16");
        die();
    }
    if (outTopic[len-1] != '/') {
        dbgPrintln(EMMDBG_MSG, "outTopic must end with '/'");
        die();
    }
    //dbgPrintln(EMMDBG_MSG, "Server: " + mqtt_server);
    //dbgPrintln(EMMDBG_MSG, "Port: " + String(mqtt_port));
    //dbgPrintln(EMMDBG_MSG, "User: " + mqtt_username ? mqtt_username : "None");
    //dbgPrintln(EMMDBG_MSG, "PW: " + mqtt_password? mqtt_password : "None");
    //dbgPrintln(EMMDBG_MSG, "Secure: " + mqtt_secure ? "True" : "False");
    //dbgPrintln(EMMDBG_MSG, "Mesh: " + mesh_secure ? "True" : "False");
    //dbgPrintln(EMMDBG_MSG, "Port: " + String(mesh_port));

    dbgPrintln(EMMDBG_MSG, "Starting Firmware " + String(firmware_id, HEX) + " : " + String(firmware_ver));
#if HAS_OTA
    dbgPrintln(EMMDBG_MSG, "OTA Start: 0x" + String(freeSpaceStart, HEX) + " OTA End: 0x" + String(freeSpaceEnd, HEX));
#endif
    uint8_t mac[6];
    generate_mac(mac, _chipID);
    //char macstr[18];
    //sprintf(macstr,"%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    //dbgPrintln(EMMDBG_MSG, "Changing MAC address to: " + String(macstr));
    do_blink = true;
    
    WiFi.disconnect();

    // This is needed to ensure both wifi_set_macaddr() calls work
    WiFi.mode(WIFI_AP_STA);
    bool ok_ap = wifi_set_macaddr(SOFTAP_IF, const_cast<uint8_t *>(mac));
    mac[0] |= 0x04;
    bool ok_sta = wifi_set_macaddr(STATION_IF, const_cast<uint8_t *>(mac));
    if (! ok_ap || ! ok_sta) {
        dbgPrintln(EMMDBG_MSG, "Failed to set MAC address");
        die();
    }
    dbgPrintln(EMMDBG_MSG, "MAC: " + WiFi.macAddress());
    dbgPrintln(EMMDBG_MSG, "SoftAPMAC: " + WiFi.softAPmacAddress());

    // In the ESP8266 2.3.0 API, there seems to be a bug which prevents a node configured as
    // WIFI_AP_STA from openning a TCP connection to it's gateway if the gateway is also
    // in WIFI_AP_STA
    WiFi.mode(WIFI_STA);

    this->connectWiFiEvents();

    espClient[0]->setNoDelay(true);
    espClient[0]->onConnect(   [this](void * arg, AsyncClient *c)                           { this->onConnect(c);         }, this);
    espClient[0]->onDisconnect([this](void * arg, AsyncClient *c)                           { this->onDisconnect(c);      }, this);
    espClient[0]->onError(     [this](void * arg, AsyncClient *c, int8_t error)             { this->onError(c, error);    }, this);
    espClient[0]->onAck(       [this](void * arg, AsyncClient *c, size_t len, uint32_t time){ this->onAck(c, len, time);  }, this);
    espClient[0]->onTimeout(   [this](void * arg, AsyncClient *c, uint32_t time)            { this->onTimeout(c, time);   }, this);
    espClient[0]->onData(      [this](void * arg, AsyncClient *c, void* data, size_t len)   { this->onData(c, data, len); }, this);

    espServer.onClient(     [this](void * arg, AsyncClient *c){ this->onClient(c);  }, this);
    espServer.setNoDelay(true);
#if ASYNC_TCP_SSL_ENABLED
    espServer.onSslFileRequest([this](void * arg, const char *filename, uint8_t **buf) -> int { return this->onSslFileRequest(filename, buf); }, this);
    if (mesh_secure.cert) {
        dbgPrintln(EMMDBG_WIFI, "Starting secure server");
        espServer.beginSecure("cert","key",NULL);
    } else
#endif
    espServer.begin();

    mqttClient.onConnect(    [this] (bool sessionPresent)                    { this->onMqttConnect(sessionPresent); });
    mqttClient.onDisconnect( [this] (AsyncMqttClientDisconnectReason reason) { this->onMqttDisconnect(reason); });
    mqttClient.onSubscribe(  [this] (uint16_t packetId, uint8_t qos)         { this->onMqttSubscribe(packetId, qos); });
    mqttClient.onUnsubscribe([this] (uint16_t packetId)                      { this->onMqttUnsubscribe(packetId); });
    mqttClient.onMessage(    [this] (char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
                                                                             { this->onMqttMessage(topic, payload, properties, len, index, total); });
    mqttClient.onPublish(    [this] (uint16_t packetId)                      { this->onMqttPublish(packetId); });

    mqttClient.setServer(mqtt_server, mqtt_port);
    if (mqtt_username || mqtt_password)
        mqttClient.setCredentials(mqtt_username, mqtt_password);

#if ASYNC_TCP_SSL_ENABLED
    mqttClient.setSecure(mqtt_secure);
    if (mqtt_fingerprint) {
        mqttClient.addServerFingerprint(mqtt_fingerprint);
    }
#endif
    //mqttClient.setCallback([this] (char* topic, byte* payload, unsigned int length) { this->mqtt_callback(topic, payload, length); });

    if (not meshConnect) {
        mqttClient.setWill(availableTopic, MSG_TYPE_RETAIN_QOS_0, true, "offline");
        dbgPrintln(EMMDBG_MQTT, ("last will set to topic: " + String(availableTopic)).c_str());
    }

    dbgPrintln(EMMDBG_WIFI_EXTRA, WiFi.status());
    dbgPrintln(EMMDBG_MSG_EXTRA, "Setup Complete");
    ap_ptr = NULL;
    connect();
}

#ifdef USE_WIFI_ONEVENT
//The ESP32 does not support the std::function onEvent variants

static ESP8266MQTTMesh *meshPtr;

#ifdef ESP32 
void staticWiFiEventHandler(arduino_event_t* event_struct)
{
    meshPtr->WiFiEventHandler(event_struct->event_id, event_struct->event_info);
}

#else
void staticWiFiEventHandler(system_event_id_t event, system_event_info_t info)
{
    meshPtr->WiFiEventHandler(event, info);
}
#endif

#ifdef ESP32
void ESP8266MQTTMesh::WiFiEventHandler(arduino_event_id_t event, arduino_event_info_t info)
{
    switch(event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    {
        struct WiFiEventStationModeGotIP e;
        e.ip = info.got_ip.ip_info.ip.addr;
        e.mask = info.got_ip.ip_info.netmask.addr;
        e.gw = info.got_ip.ip_info.gw.addr;
        this->onWifiConnect(e);
        break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    {
        struct WiFiEventStationModeDisconnected e;
        e.ssid.reserve(info.wifi_sta_disconnected.ssid_len+1);
        for(int i = 0; i < info.wifi_sta_disconnected.ssid_len; i++) {
            e.ssid += (char)info.wifi_sta_disconnected.ssid[i];
        }
        memcpy(e.bssid, info.wifi_sta_disconnected.bssid, 6);
        e.reason = info.wifi_sta_disconnected.reason;
        this->onWifiDisconnect(e);
        break;
    }
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    {
        this->onAPConnect(info.wifi_ap_staconnected);
        break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        this->onAPDisconnect(info.wifi_ap_stadisconnected);
        break;
    }
}
#else
void ESP8266MQTTMesh::WiFiEventHandler(system_event_id_t event, system_event_info_t info)
{
    switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
    {
        struct WiFiEventStationModeGotIP e;
        e.ip = info.got_ip.ip_info.ip.addr;
        e.mask = info.got_ip.ip_info.netmask.addr;
        e.gw = info.got_ip.ip_info.gw.addr;
        this->onWifiConnect(e);
        break;
    }
    case SYSTEM_EVENT_STA_DISCONNECTED:
    {
        struct WiFiEventStationModeDisconnected e;
        e.ssid.reserve(info.disconnected.ssid_len+1);
        for(int i = 0; i < info.disconnected.ssid_len; i++) {
            e.ssid += (char)info.disconnected.ssid[i];
        }
        memcpy(e.bssid, info.disconnected.bssid, 6);
        e.reason = info.disconnected.reason;
        this->onWifiDisconnect(e);
        break;
    }
    case SYSTEM_EVENT_AP_STACONNECTED:
    {
        this->onAPConnect(info.sta_connected);
        break;
    }
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        this->onAPDisconnect(info.sta_disconnected);
        break;
    }
}
#endif

void ESP8266MQTTMesh::connectWiFiEvents()
{
    meshPtr = this;
    WiFi.onEvent(&staticWiFiEventHandler);
}
#else //USE_WIFI_ONEVENT
void ESP8266MQTTMesh::connectWiFiEvents()
{
    wifiConnectHandler =
        WiFi.onStationModeGotIP(            [this] (const WiFiEventStationModeGotIP& e) {                this->onWifiConnect(e);    }); 
    wifiDisconnectHandler =
        WiFi.onStationModeDisconnected(     [this] (const WiFiEventStationModeDisconnected& e) {         this->onWifiDisconnect(e); });
    //wifiDHCPTimeoutHandler =
    //    WiFi.onStationModeDHCPTimeout(      [this] () {                                                  this->onDHCPTimeout();     });
    wifiAPConnectHandler =
        WiFi.onSoftAPModeStationConnected(  [this] (const WiFiEventSoftAPModeStationConnected& ip) {     this->onAPConnect(ip);     });
    wifiAPDisconnectHandler =
        WiFi.onSoftAPModeStationDisconnected([this] (const WiFiEventSoftAPModeStationDisconnected& ip) { this->onAPDisconnect(ip);  });
}
#endif //USE_WIFI_ONEVENT

uint32_t ESP8266MQTTMesh::lfsr(uint32_t seed, uint8_t b)
{
    // Linear feedback shift register with 32-bit Xilinx polinomial x^32 + x^22 + x^2 + x + 1
    // http://www.xilinx.com/support/documentation/application_notes/xapp052.pdf
    static const uint32_t LFSR_FEEDBACK = 0x80200003ul;
    static const uint32_t LFSR_INTAP = 32-1;
    for (int i = 0; i < 8; ++i) {
        seed = (seed >> 1) ^ ((-(seed & 1u) & LFSR_FEEDBACK) ^ ~((uint32_t)(b & 1) << LFSR_INTAP));
        b >>= 1;
    }
    return seed;
}

uint32_t ESP8266MQTTMesh::encrypt_id(uint32_t id) {
    // The lowest bits of the 1st octet need to be b'10 to indicate a
    // locally-administered unicast MAC address
    return (((lfsr(id, 0) * mesh_bssid_key) << 3) + 2) & 0x00FFFFFF;
}

void ESP8266MQTTMesh::generate_mac(uint8_t *bssid, uint32_t id) {
    uint32_t res = encrypt_id(id);
    for(int i = 0; i < 3; i++) {
        bssid[i] = (res >> (8 * i)) & 0xff;
    }
    for(int i = 0; i < 3; i++) {
        bssid[5-i] = (id >> (8 * i)) & 0xff;
    }
}

bool ESP8266MQTTMesh::verify_bssid(uint8_t *bssid) {
    //bit 3 is 1 for station and 0 for AP
    uint32_t wanted = ((bssid[2] << 16) | (bssid[1] << 8) | bssid[0]) & 0xFFFFFFFB;
    uint32_t id = (bssid[3] << 16) | (bssid[4] << 8) | bssid[5];
    uint32_t res = encrypt_id(id);
    return res == wanted;
}

bool ESP8266MQTTMesh::connected() {
    delay(0); // let the Interrupts execute
    return wifiConnected() && ((meshConnect && espClient[0] && espClient[0]->connected() && p2pConnected) || mqttClient.connected());
}

void ESP8266MQTTMesh::scan() {
    //Need to rescan
    if (! scanning) {
        ap_ptr = NULL;
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        dbgPrintln(EMMDBG_WIFI, "Scanning for networks");
        WiFi.scanDelete();
        WiFi.scanNetworks(true,true);
        scanning = true;
    }

    //scanComplete returns <0 while scanning is in progress
    int numberOfNetworksFound = WiFi.scanComplete();
    if (numberOfNetworksFound < 0) {
        return;
    }
    dbgPrintln(EMMDBG_WIFI, "Found: " + String(numberOfNetworksFound));

    scanning = false;

    //Initialize AP to be empty, and all unused APs on the unused list
    if (ap_unused == NULL) {
        ap_unused = ap;
    } else {
        for(ap_ptr = ap_unused; ap_ptr->next != NULL; ap_ptr = ap_ptr->next)
        {}
        ap_ptr->next = ap;
    }
    ap = NULL;

    for(int i = 0; i < numberOfNetworksFound; i++) {
        int network_idx = NETWORK_MESH_NODE;
        int rssi = WiFi.RSSI(i);
        dbgPrintln(EMMDBG_WIFI, "Found SSID: '" + WiFi.SSID(i) + "' BSSID '" + WiFi.BSSIDstr(i) + "'" + " RSSI: " + String(rssi));
        if (IS_GATEWAY) { //Always true except if configured that only a specific Node is allowed to connect to the real Acess Point
            network_idx = match_networks(WiFi.SSID(i).c_str(), WiFi.BSSIDstr(i).c_str());
        }
        if(network_idx == NETWORK_MESH_NODE) {
            if (WiFi.SSID(i).length()) { //Mesh Nodes have no SSID, so here are only "real" Acess Points, which did not matched the AP List
                dbgPrintln(EMMDBG_WIFI_EXTRA, "Did not match SSID list");
                continue;
            } else { //Here the Mesh Nodes are handled
                if (! verify_bssid(WiFi.BSSID(i))) { //Check if the Node is a Mesh Node, if not just ignore this Signal
                    dbgPrintln(EMMDBG_WIFI_EXTRA, "Failed to match BSSID");
                    continue;
                }
            }
        }//else Connection is a direct Access Point which matched the Access Point Credential List
        ap_t *next_ap;
        if (ap_unused == NULL) {
            next_ap = new ap_t;
        } else {
            next_ap =ap_unused;
            ap_unused = ap_unused->next;
        }
        //Assign next_ap
        next_ap->ssid_idx = network_idx;
        next_ap->rssi = rssi;
        memcpy(next_ap->bssid, WiFi.BSSID(i), 6);

        //sort by RSSI
        ap_t *ap_last = NULL;
        for(ap_ptr = ap; ap_ptr != NULL; ap_last = ap_ptr, ap_ptr = ap_ptr->next) {
            if(network_idx != NETWORK_MESH_NODE) {
                //Tested Signal is Wifi AP
                if ((ap_ptr->ssid_idx == NETWORK_MESH_NODE && //Current Signal is Mesh Node
                     (rssi >= -77 || rssi >= ap_ptr->rssi))|| //and Signal under Test to direct AP have to be quite decent or at least better then current one
                     (ap_ptr->ssid_idx != NETWORK_MESH_NODE && rssi >= ap_ptr->rssi))//or both are Ap Points, but tested one have stronger Signal
                {
                    break;
                }
            } else {
                //Tested Signal is mesh node
                if ((ap_ptr->ssid_idx == NETWORK_MESH_NODE || //if Actual Node is Mesh Node
                    (ap_ptr->ssid_idx != NETWORK_MESH_NODE && ap_ptr->rssi <= -77)) && //or is AP, but with weak Signal
                      rssi >= ap_ptr->rssi) //and in either Way have better Connection then actuall one
                {
                    break;
                }
            }
        }
        //Insert next_ap before ap_ptr (i.e. at last_ap)
        next_ap->next = ap_ptr;
        if (ap_last) {
            ap_last->next = next_ap;
        } else {
            //ap was empty, so create it
            ap = next_ap;
        }
    }
    ap_ptr = ap;
}

int ESP8266MQTTMesh::match_networks(const char *ssid, const char *bssid)
{
    for(int idx = 0; networks[idx].ssid != NULL; idx++) {
        if(networks[idx].bssid) {
            if (strcmp(bssid, networks[idx].bssid) == 0) {
                if (networks[idx].hidden && ssid[0] == 0) {
                    //hidden network
                    dbgPrintln(EMMDBG_WIFI, "Matched");
                    return idx;
                }
            } else {
                //Didn't match requested bssid
                continue;
            }
        }
        dbgPrintln(EMMDBG_WIFI_EXTRA, "Comparing " + String(networks[idx].ssid));
        if(ssid[0] != 0) {
            if ((! networks[idx].hidden) && strcmp(ssid, networks[idx].ssid) == 0) {
                //matched ssid (and bssid if needed)
                dbgPrintln(EMMDBG_WIFI, "Matched");
                return idx;
            }
        }
    }
    return NETWORK_MESH_NODE;
}

void ESP8266MQTTMesh::schedule_connect(float delay) {
    if(connectScheduled){
        return;
    }
    connectScheduled = true;
    dbgPrintln(EMMDBG_WIFI_EXTRA, "Scheduling reconnect for " + String(delay,2)+ " seconds from now");
    schedule.once(delay, connect_static, this);
}

void ESP8266MQTTMesh::connect() {
    connectScheduled = false;
    if (WiFi.isConnected()) {
        dbgPrintln(EMMDBG_WIFI, "Called connect when already connected!");
        return;
    }
    retry_connect = 1;
    if (scanning) {
        scan();
        schedule_connect(0.5);
        return;
    }
    if (! ap_ptr) {
        // No networks found, try again
        scan();
        schedule_connect(5.0);
        return;
    }
    int i = 0;
    for (ap_t *p = ap; p != NULL; p = p->next, i++) {
        dbgPrintln(EMMDBG_WIFI, String(i) + String(p == ap_ptr ? " * " : "   ") + mac_str(p->bssid) + " " + String(p->rssi));
    }
    char _mesh_ssid[32];
    const char *ssid;
    const char *password;
    if (ap_ptr->ssid_idx == NETWORK_MESH_NODE) {
        //This is a mesh node
        ssid = build_mesh_ssid(_mesh_ssid, ap_ptr->bssid);
        password = mesh_password;
        meshConnect = true;
    } else {
        ssid = networks[ap_ptr->ssid_idx].ssid;
        password = networks[ap_ptr->ssid_idx].password;
        meshConnect = false;
    }
    dbgPrintln(EMMDBG_WIFI, "Connecting to SSID : '" + String(ssid) + "' BSSID '" + mac_str(ap_ptr->bssid) + "'");
    WiFi.begin(ssid, password);
    alreaddyDisconnected = false;
}

String ESP8266MQTTMesh::mac_str(uint8_t *bssid) {
    char mac[19];
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return String(mac);
}
const char *ESP8266MQTTMesh::build_mesh_ssid(char buf[32], uint8_t *mac) {
    char chipid[8];
    sprintf(chipid, "_%02x%02x%02x", mac[3], mac[4], mac[5]);
    strlcpy(buf, mesh_ssid, sizeof(buf)-7);
    strlcat(buf, chipid, sizeof(buf));
    return buf;
}

void ESP8266MQTTMesh::parse_message(const char *topic, const char *msg) {
  int inTopicLen = strlen(inTopic);
  if (strstr(topic, inTopic) != topic) {
      return;
  }
  const char *subtopic = topic + inTopicLen;
  if (strstr(subtopic ,"ota/") == subtopic) {
#if HAS_OTA
      const char *cmd = subtopic + 4;
      handle_ota(cmd, msg);
#endif
      return;
  }
  else if (strstr(subtopic ,"fw/") == subtopic) {
      const char *cmd = subtopic + 3;
      handle_fw(cmd);
  }
  if (! callback) {
      return;
  }
  int myIDLen = strlen(myID);
  if(strstr(subtopic, myID) == subtopic) {
      //Only handle messages addressed to this node
      HandleMessages(subtopic + myIDLen, msg);
  }
  else if(strstr(subtopic, "broadcast/") == subtopic) {
      //Or messages sent to all nodes
      HandleMessages(subtopic + 10, msg);
  }
}

void ESP8266MQTTMesh::HandleMessages(const char *topic, const char *msg) {
  if(strstr(topic,"Ping") == topic){
    dbgPrintln(EMMDBG_MSG, "answering Ping!");
    String topic = "Ping";
    publish(topic.c_str(), String(1).c_str());
  }else if(strstr(topic,"Restart") == topic){
    dbgPrintln(EMMDBG_MSG, "Got Restart Command, restarting now");
    die();
  }else{
    callback(topic, msg);
  }
}

void ESP8266MQTTMesh::connect_mqtt() {
    dbgPrintln(EMMDBG_MQTT, "Attempting MQTT connection (" + String(mqtt_server) + ":" + String(mqtt_port) + ")...");
    // Attempt to connect
    mqttClient.connect();
}


void ESP8266MQTTMesh::publish(const char *subtopic, const char *msg, enum MSG_TYPE msgCmd) {
    publish(outTopic, myID, subtopic, msg, msgCmd);
}

void ESP8266MQTTMesh::publish_node(const char *subtopic, const char *msg, enum MSG_TYPE msgCmd) {
    publish(inTopic, myID, subtopic, msg, msgCmd);
}

void ESP8266MQTTMesh::publish(const char *topicDirection, const char *baseTopic, const char *subTopic, const char *msg, uint8_t msgType) {
    char topic[64];
    strlcpy(topic, topicDirection, sizeof(topic));
    strlcat(topic, baseTopic, sizeof(topic));
    strlcat(topic, subTopic, sizeof(topic));
    dbgPrintln(EMMDBG_MQTT_EXTRA, "Sending: " + String(topic) + "=" + String(msg));
    if (! meshConnect) {
        mqtt_publish(topic, msg, msgType);
    } else {
        send_message(0, topic, msg, msgType);
    }
}

void ESP8266MQTTMesh::shutdown_AP() {
    if(! AP_ready)
        return;
    for (int i = 1; i <= ESP8266_NUM_CLIENTS; i++) {
        if(espClient[i]) {
            delete espClient[i];
            espClient[i] = NULL;
        }
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    AP_ready = false;
}

void ESP8266MQTTMesh::setup_AP() {
    if (AP_ready)
        return;
    
    uint octet2 = WiFi.gatewayIP()[1] + 1;
    if (octet2==255) {
        octet2++;
    }
    if (octet2==0) {
        octet2++;
    }
    IPAddress apIP(WiFi.gatewayIP()[0],
                   octet2,
                   1,
                   1);
    IPAddress apGateway(apIP);
    IPAddress apSubmask(255, 255, 255, 0);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apIP, apGateway, apSubmask);
    char _mesh_ssid[32];
    uint8_t mac[6];
    build_mesh_ssid(_mesh_ssid, WiFi.softAPmacAddress(mac));
    WiFi.softAP(_mesh_ssid, mesh_password, WiFi.channel(), 1);
    dbgPrintln(EMMDBG_WIFI, "Initialized AP as '" + String(_mesh_ssid) + "'  IP '" + apIP.toString() + "'");
    AP_ready = true;
}

void ESP8266MQTTMesh::send_connected_msg() {
    //The list of nodes is only stored on the broker.  Individual nodes don't knowother node IDs
    /*char topic[10];
    strlcpy(topic, myID, sizeof(topic));
    topic[strlen(topic)-1] = 0; // Chop off trailing '/'
    publish(outTopic, "bssid/", topic, WiFi.softAPmacAddress().c_str(), MSG_TYPE_RETAIN_QOS_0);*/
    if(wasConnected){
        publish("info/reset_Reason", String("lost_Connection").c_str(), MSG_TYPE_RETAIN_QOS_0);
    }else{
#ifdef ESP32
        publish("info/reset_Reason", String(esp_reset_reason()).c_str(), MSG_TYPE_RETAIN_QOS_0);
#else
        publish("info/reset_Reason", String(ESP.getResetReason()).c_str(), MSG_TYPE_RETAIN_QOS_0);
#endif
    }
    
    publish("info/MAC", String(WiFi.macAddress()).c_str(), MSG_TYPE_RETAIN_QOS_0);
    publish("info/MAC_hosted_AP", String(WiFi.softAPmacAddress()).c_str(), MSG_TYPE_RETAIN_QOS_0);
    publish("info/IP_local", WiFi.localIP().toString().c_str(), MSG_TYPE_RETAIN_QOS_0);
    publish("info/RSSI", String(ap_ptr->rssi).c_str(), MSG_TYPE_RETAIN_QOS_0);
    publish("info/connectedTo", String(mac_str(ap_ptr->bssid)).c_str(), MSG_TYPE_RETAIN_QOS_0);
    
    publish("info/available", "online", MSG_TYPE_RETAIN_QOS_0);
}


bool ESP8266MQTTMesh::send_message(int index, const char *topicOrMsg, const char *msg, uint8_t msgType) {
    std::string completeMessage = "";
    if (msgType == 0) {
        msgType = MSG_TYPE_INVALID;
    }
    char msgTypeStr[2];
    msgTypeStr[0] = msgType;
    msgTypeStr[1] = '\0';
    if (index == 0) {
        //We only send the msgType upstream
        completeMessage += String(msgTypeStr).c_str();
    }
    completeMessage += String(topicOrMsg).c_str();
    if (msg) {
        completeMessage += String("=").c_str();
        completeMessage += String(msg).c_str();
    }
    //char c_string[completeMessage.length() + 2];
    //strcpy(c_string, completeMessage.c_str());
    //c_string[completeMessage.length()] = '\n';
    //c_string[completeMessage.length() + 1] = '\0';
    espClient[index]->write((completeMessage + '\n' + '\0').c_str());
    dbgPrintln(EMMDBG_WIFI_EXTRA, String("now sending raw Message: ") + completeMessage.c_str());
    return true;
}


/*
bool ESP8266MQTTMesh::send_message(int index, const char *topicOrMsg, const char *msg, uint8_t msgType) {
    char msgTypeStr[2];
    if (msgType == 0) {
        msgType = MSG_TYPE_INVALID;
    }
    msgTypeStr[0] = msgType;
    msgTypeStr[1] = 0;
    if (index == 0) {
        //We only send the msgType upstream
        espClient[index]->write(msgTypeStr,1);
    }
    espClient[index]->write(topicOrMsg);
    if (msg) {
        espClient[index]->write("=", 1);
        espClient[index]->write(msg);
    }
    espClient[index]->write("\n", 1);
    return true;
}
*/


void ESP8266MQTTMesh::broadcast_message(const char *topicOrMsg, const char *msg) {
    for (int i = 1; i <= ESP8266_NUM_CLIENTS; i++) {
        if (espClient[i]) {
            send_message(i, topicOrMsg, msg);
        }
    }
}

void ESP8266MQTTMesh::handle_client_data(int idx, char *rawdata) {
            dbgPrintln(EMMDBG_MQTT_EXTRA, "Received: msg from " + espClient[idx]->remoteIP().toString() + " on " + (idx == 0 ? "STA" : "AP"));
            const char *data = rawdata + (idx ? 1 : 0); //packages from other Modules use the first bit as an Message Type
            dbgPrintln(EMMDBG_MQTT_EXTRA, "--> '" + String(data) + "'");
            char topic[64];
            const char *msg;
            if (! keyValue(data, '=', topic, sizeof(topic), &msg)) {
                dbgPrintln(EMMDBG_MQTT, "Failed to handle message");
                return;
            }
            if (idx == 0) {
                //This is a packet from MQTT, need to rebroadcast to each connected station
                broadcast_message(data);
                parse_message(topic, msg);
            } else {
                unsigned char msgType = rawdata[0];
                if (strstr(topic,"/mesh_cmd")  == topic + strlen(topic) - 9) {
                    // We will handle this packet locally
                    // TODO: implement proper Routing instead of broadcasting each Package! connected Modules can communicate with this one by using the Topic "/mesh_cmd/..."
                    dbgPrintln(EMMDBG_MQTT, "received unknown Mesh Command from connected Node");
                } else {
                    if (! meshConnect) {
                        mqtt_publish(topic, msg, msgType);
                    } else {
                        send_message(0, data, NULL, msgType);
                    }
                }
            }
}

uint16_t ESP8266MQTTMesh::mqtt_publish(const char *topic, const char *msg, uint8_t msgType)
{
    uint8_t qos = 0;
    bool retain = false;
    if (msgType == MSG_TYPE_RETAIN_QOS_0
        || msgType == MSG_TYPE_RETAIN_QOS_1
        || msgType == MSG_TYPE_RETAIN_QOS_2)
    {
        qos = msgType - MSG_TYPE_RETAIN_QOS_0;
        retain = true;
    }
    else if(msgType == MSG_TYPE_QOS_0
            || msgType == MSG_TYPE_QOS_1
            || msgType == MSG_TYPE_QOS_2)
    {
        qos = msgType - MSG_TYPE_QOS_0;
    }
    return mqttClient.publish(topic, qos, retain, msg);
}

bool ESP8266MQTTMesh::keyValue(const char *data, char separator, char *key, int keylen, const char **value) {
  int maxIndex = strlen(data)-1;
  int i;
  for(i=0; i<=maxIndex && i <keylen-1; i++) {
      key[i] = data[i];
      if (key[i] == separator) {
          *value = data+i+1;
          key[i] = 0;
          return true;
      }
  }
  key[i] = 0;
  *value = NULL;
  return false;
}

void ESP8266MQTTMesh::get_fw_string(char *msg, int len, const char *prefix)
{
    char id[100];
    strlcpy(msg, prefix, len);
    if (strlen(prefix)) {
        strlcat(msg, " ", len);
    }
#ifdef ESP32
    sprintf(id, "ChipID:%06X FirmwareID:%04X v%s IP:%s %s", _chipID, firmware_id, firmware_ver, WiFi.localIP().toString().c_str(), meshConnect ? "mesh" : "");
#else
    os_sprintf(id, "ChipID:%06X FirmwareID:%04X v%s IP:%s %s", _chipID, firmware_id, firmware_ver, WiFi.localIP().toString().c_str(), meshConnect ? "mesh" : "");
#endif
    strlcat(msg, id, len);
}

void ESP8266MQTTMesh::handle_fw(const char *cmd) {
    // int len;
    // if(strstr(cmd, myID) == cmd) {
    //     len = strlen(myID);
    // } else if (strstr(cmd, "broadcast") == cmd) {
    //     len = 9;
    // } else {
    //     return;
    // }
    char msg[64];
    get_fw_string(msg, sizeof(msg), "");
    publish("fw", msg);
}

#if HAS_OTA
void ESP8266MQTTMesh::parse_ota_info(const char *str) {
    memset (&ota_info, 0, sizeof(ota_info));
    char kv[64];
    while(str) {
        keyValue(str, ',', kv, sizeof(kv), &str);
        dbgPrintln(EMMDBG_OTA_EXTRA, "Key/Value: " + String(kv));
        char key[32];
        const char *value;
        if (! keyValue(kv, ':', key, sizeof(key), &value)) {
            dbgPrintln(EMMDBG_OTA, "Failed to parse Key/Value: " + String(kv));
            continue;
        }
        dbgPrintln(EMMDBG_OTA_EXTRA, "Key: " + String(key) + " Value: " + String(value));
        if (0 == strcmp(key, "len")) {
            ota_info.len = strtoul(value, NULL, 10);
        } else if (0 == strcmp(key, "md5")) {
            if(strlen(value) == 24 && base64_dec_len(value, 24) == 16) {
              base64_decode((char *)ota_info.md5, value,  24);
            } else {
              dbgPrintln(EMMDBG_OTA, "Failed to parse md5");
            }
        }
    }
}
    
bool ESP8266MQTTMesh::check_ota_md5() {
    uint8_t buf[128];
    if (ota_info.len > freeSpaceEnd - freeSpaceStart) {
        return false;
    }
    MD5Builder _md5;
    _md5.begin();
    uint32_t address = freeSpaceStart;
    unsigned int len = ota_info.len;
    while(len) {
        int size = len > sizeof(buf) ? sizeof(buf) : len;
        if (! ESP.flashRead(address, (uint32_t *)buf, (size + 3) & ~3)) {
            return false;
        }
        _md5.add(buf, size);
        address += size;
        len -= size;
    }
    _md5.calculate();
    _md5.getBytes(buf);
    for (int i = 0; i < 16; i++) {
        if (buf[i] != ota_info.md5[i]) {
            return false;
        }
    }
    return true;
}

char * ESP8266MQTTMesh::md5(const uint8_t *msg, int len) {
    static char out[33];
    MD5Builder _md5;
    _md5.begin();
    _md5.add(msg, len);
    _md5.calculate();
    _md5.getChars(out);
    return out;
}
void ESP8266MQTTMesh::erase_sector() {
    uint32_t start = freeSpaceStart / FLASH_SECTOR_SIZE;
    //erase flash area here
    if (nextErase >= start) {
        ESP.flashEraseSector(nextErase--);
        schedule.once<void (*)(ESP8266MQTTMesh*), ESP8266MQTTMesh*>(0.001, erase_sector, this);
    } else {
        nextErase = 0;
        char deltaStr[10];
        uint32_t delta = micros() - startTime;
        itoa(delta, deltaStr, 10);
        dbgPrintln(EMMDBG_OTA, "Erase complete in " + String(delta / 1000000.0, 6) + " seconds");
        publish("ota/erase", deltaStr);
    }
}

void ESP8266MQTTMesh::handle_ota(const char *cmd, const char *msg) {
    dbgPrintln(EMMDBG_OTA_EXTRA, "OTA cmd " + String(cmd) + " Length: " + String(strlen(msg)));
    if(strstr(cmd, myID) == cmd) {
        cmd += strlen(myID);
    } else {
        char *end;
        unsigned int id = strtoul(cmd,&end, 16);
        if (id != firmware_id || *end != '/') {
            dbgPrintln(EMMDBG_OTA_EXTRA, "Ignoring OTA because firmwareID did not match " + String(firmware_id, HEX));
            return;
        }
        cmd += (end - cmd) + 1; //skip ID
    }
    if(0 == strcmp(cmd, "start")) {
        dbgPrintln(EMMDBG_OTA_EXTRA, "OTA Start");
        parse_ota_info(msg);
        if (ota_info.len == 0) {
            dbgPrintln(EMMDBG_OTA, "Ignoring OTA because firmware length = 0");
            return;
        }
        dbgPrintln(EMMDBG_OTA, "-> " + String(msg));
        if (ota_info.len > freeSpaceEnd - freeSpaceStart) {
            dbgPrintln(EMMDBG_MSG, "Not enough space for firmware: " + String(ota_info.len) + " > " + String(freeSpaceEnd - freeSpaceStart));
            return;
        }
        uint32_t end = (freeSpaceStart + ota_info.len + FLASH_SECTOR_SIZE - 1) & (~(FLASH_SECTOR_SIZE - 1));
        nextErase = end / FLASH_SECTOR_SIZE - 1;
        startTime = micros();
        dbgPrintln(EMMDBG_OTA, "Erasing " + String((end - freeSpaceStart)/ FLASH_SECTOR_SIZE) + " sectors");
        schedule.once<void (*)(ESP8266MQTTMesh*), ESP8266MQTTMesh*>(0.0, erase_sector, this);
    }
    else if(0 == strcmp(cmd, "check")) {
        if (strlen(msg) > 0) {
            char *out = md5((uint8_t *)msg, strlen(msg));
            publish("ota/check", out);
        } else {
            const char *md5ok = check_ota_md5() ? "MD5 Passed" : "MD5 Failed";
            dbgPrintln(EMMDBG_OTA, md5ok);
            publish("ota/check", md5ok);
        }
    }
    else if(0 == strcmp(cmd, "flash")) {
        if (! check_ota_md5()) {
            dbgPrintln(EMMDBG_MSG, "Flash failed due to md5 mismatch");
            publish("ota/flash", "Failed");
            return;
        }
        dbgPrintln(EMMDBG_OTA, "Flashing");
        
        eboot_command ebcmd;
        ebcmd.action = ACTION_COPY_RAW;
        ebcmd.args[0] = freeSpaceStart;
        ebcmd.args[1] = 0x00000;
        ebcmd.args[2] = ota_info.len;
        eboot_command_write(&ebcmd);
        // If this is a broadcast OTA update, this would never go through
        //publish("ota/flash", "Success");

        shutdown_AP();
        p2pConnected = false;
        mqttClient.disconnect();
        delay(100);
        ESP.restart();
        die();
    }
    else {
        char *end;
        unsigned int address = strtoul(cmd, &end, 10);
        if (address > freeSpaceEnd - freeSpaceStart || end != cmd + strlen(cmd)) {
            dbgPrintln(EMMDBG_MSG, "Illegal address " + String(address) + " specified");
            return;
        }
        int msglen = strlen(msg);
        if (msglen > 1024) {
            dbgPrintln(EMMDBG_MSG, "Message length " + String(msglen) + " too long");
            return;
        }
        byte data[768];
        long t = micros();
        int len = base64_decode((char *)data, msg, msglen);
        if (address + len > freeSpaceEnd) {
            dbgPrintln(EMMDBG_MSG, "Message length would run past end of free space");
            return;
        }
        dbgPrintln(EMMDBG_OTA_EXTRA, "Got " + String(len) + " bytes FW @ " + String(address, HEX));
        bool ok = ESP.flashWrite(freeSpaceStart + address, (uint32_t*) data, len);
        dbgPrintln(EMMDBG_OTA, "Wrote " + String(len) + " bytes @" + String(address, HEX) + " in " + String((micros() - t) / 1000000.0, 6) + " seconds");
        char topic[17];
        strlcpy(topic, "ota/md5/", sizeof(topic));
        itoa(address, topic + strlen(topic), 16);
        publish(topic, md5(data, len));
        if (! ok) {
            dbgPrintln(EMMDBG_MSG, "Failed to write firmware at " + String(freeSpaceStart + address, HEX) + " Length: " + String(len));
        }
    }
}
#endif //HAS_OTA

void ESP8266MQTTMesh::onWifiConnect(const WiFiEventStationModeGotIP& event) {
    do_blink = false and blink_status;
    
    // when connecting to the Mesh, not the direct Connection
    if (meshConnect) {
        dbgPrintln(EMMDBG_WIFI, "Connecting to mesh: " + WiFi.gatewayIP().toString() + " on port: " + String(mesh_port));
#if ASYNC_TCP_SSL_ENABLED
        espClient[0]->connect(WiFi.gatewayIP(), mesh_port, mesh_secure.cert ? true : false);
#else
        espClient[0]->connect(WiFi.gatewayIP(), mesh_port);
#endif
#ifdef ESP32
        schedule.once<ESP8266MQTTMesh*> (5000, checkConnectionEstablished_static, this);
#else
        schedule.once<void (*)(ESP8266MQTTMesh*), ESP8266MQTTMesh*> (5000, checkConnectionEstablished_static, this);
#endif
        bufptr[0] = inbuffer[0];
    } else {
        dbgPrintln(EMMDBG_WIFI, "Connecting to mqtt");
        connect_mqtt();
    }
}

void ESP8266MQTTMesh::checkConnectionEstablished() {
    if(!connected()){
        dbgPrintln(EMMDBG_WIFI, "restarting because tried to accomplish p2p Connection, but Connection wasn't astablished.");
        die();
        while(true){}
    }
}


void ESP8266MQTTMesh::onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
    do_blink = true and blink_status;
    
    //Reasons are here: ESP8266WiFiType.h-> WiFiDisconnectReason
    if (! connectScheduled) {
        schedule_connect(2.0);
    }
    dbgPrintln(EMMDBG_WIFI, "Disconnected from Wi-Fi: " + event.ssid + " because: " + String(event.reason));
    if (alreaddyDisconnected){ //prevent the Function to fire multiple times on a single Disconnect
        return;
    }
    alreaddyDisconnected = true;
    WiFi.disconnect();
    if (event.reason == WIFI_DISCONNECT_REASON_ASSOC_TOOMANY  && retry_connect) {
        // If we rebooted without a clean shutdown, we may still be associated with this AP, in which case
        // we'll be booted and should try again
        retry_connect--;
    } else{
        if (ap_ptr != NULL){
            if (ap_ptr->next != NULL){
                ap_ptr = ap_ptr->next;
            }else{
                ap_ptr = NULL;
            }
        }else{
            ap_ptr = NULL;
        }
    }
}

//void ESP8266MQTTMesh::onDHCPTimeout() {
//    dbgPrintln(EMMDBG_WIFI, "Failed to get DHCP info");
//}

void ESP8266MQTTMesh::onAPConnect(const WiFiEventSoftAPModeStationConnected& ip) {
    dbgPrintln(EMMDBG_WIFI, "Got connection from Station " + mac_str((uint8_t*)ip.mac));
}

void ESP8266MQTTMesh::onAPDisconnect(const WiFiEventSoftAPModeStationDisconnected& ip) {
    dbgPrintln(EMMDBG_WIFI, "Got disconnection from Station " + mac_str((uint8_t*)ip.mac));
}

void ESP8266MQTTMesh::onMqttConnect(bool sessionPresent) {
    dbgPrintln(EMMDBG_MQTT, "MQTT Connected");
    /*
    // Once connected, publish an announcement...
    char msg[128];
    get_fw_string(msg, sizeof(msg), "Connected");
    //strlcpy(publishMsg, outTopic, sizeof(publishMsg));
    //strlcat(publishMsg, WiFi.localIP().toString().c_str(), sizeof(publishMsg));
    publish(outTopic, "", "connect", msg, MSG_TYPE_NONE);
     */
    // ... and resubscribe
    char subscribe[TOPIC_LEN];
    strlcpy(subscribe, inTopic, sizeof(subscribe));
    strlcat(subscribe, "#", sizeof(subscribe));
    mqttClient.subscribe(subscribe, 0);

    send_connected_msg();
    setup_AP();
    wasConnected = true;
}

void ESP8266MQTTMesh::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    int r = (int8_t)reason;
    dbgPrintln(EMMDBG_MQTT, "Disconnected from MQTT: " + String(r));
#if ASYNC_TCP_SSL_ENABLED
    if (reason == AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT) {
        dbgPrintln(EMMDBG_MQTT, "Bad MQTT server fingerprint.");
        if (WiFi.isConnected()) {
            WiFi.disconnect();
        }
        return;
    }
#endif
    shutdown_AP();
    p2pConnected = false;
    if (WiFi.isConnected()) {
        connect_mqtt();
    }
}

void ESP8266MQTTMesh::onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void ESP8266MQTTMesh::onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void ESP8266MQTTMesh::onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if(index + len + 1 > MQTT_MAX_PACKET_SIZE){
    dbgPrintln(EMMDBG_MQTT_EXTRA, "Message arrived, but was to long, InputBuffer: " + String(MQTT_MAX_PACKET_SIZE) + ", total MSG Length: " + String(total) + ", handled Part Length: " + String(index + len));
    return;
  }
  if(index + len > total){
    dbgPrintln(EMMDBG_MQTT_EXTRA, "Message arrived but partial Lengths was bigger then total Length (" + String(index) + String(len) + ">" + String(total) + ")");
    return;
  }
  memcpy(&inbuffer[0][index], payload, len);
  inbuffer[0][total] = '\0';
  if (index + len == total) {
    dbgPrintln(EMMDBG_MQTT_EXTRA, "Message arrived [" + String(topic) + "] '" + String(inbuffer[0]) + "'");
    broadcast_message(topic, inbuffer[0]);
    parse_message(topic, inbuffer[0]);
  }
}

void ESP8266MQTTMesh::onMqttPublish(uint16_t packetId) {
  //Serial.println("Publish acknowledged.");
  //Serial.print("  packetId: ");
  //Serial.println(packetId);
}

#if ASYNC_TCP_SSL_ENABLED
int ESP8266MQTTMesh::onSslFileRequest(const char *filename, uint8_t **buf) {
    if(strcmp(filename, "cert") == 0) {
        *buf = (uint8_t *)mesh_secure.cert;
        return mesh_secure.cert_len;
    } else if(strcmp(filename, "key") == 0) {
        *buf = (uint8_t *)mesh_secure.key;
        return mesh_secure.key_len;
    } else if(strcmp(filename, "fingerprint") == 0) {
        *buf = (uint8_t *)mesh_secure.fingerprint;
        return 20;
    } else {
        *buf = 0;
        dbgPrintln(EMMDBG_WIFI, "Error reading SSL File: " + filename);
        return 0;
    }
}
#endif
void ESP8266MQTTMesh::onClient(AsyncClient* c) { //when other Node connects to this AP and so into the Sensor Mesh
    dbgPrintln(EMMDBG_WIFI, "Got client connection from: " + c->remoteIP().toString());
    for (int i = 1; i <= ESP8266_NUM_CLIENTS; i++) {
        if (! espClient[i]) {
            espClient[i] = c;
            espClient[i]->onDisconnect([this](void * arg, AsyncClient *c)                           { this->onDisconnect(c);      }, this);
            espClient[i]->onError(     [this](void * arg, AsyncClient *c, int8_t error)             { this->onError(c, error);    }, this);
            espClient[i]->onAck(       [this](void * arg, AsyncClient *c, size_t len, uint32_t time){ this->onAck(c, len, time);  }, this);
            espClient[i]->onTimeout(   [this](void * arg, AsyncClient *c, uint32_t time)            { this->onTimeout(c, time);   }, this);
            espClient[i]->onData(      [this](void * arg, AsyncClient *c, void* data, size_t len)   { this->onData(c, data, len); }, this);
            bufptr[i] = inbuffer[i];
            return;
        }
    }
    dbgPrintln(EMMDBG_WIFI, "Discarding client connection from: " + c->remoteIP().toString() + " because max Connections are alreaddy established!");
    c->close(1);
    delete c;
}

void ESP8266MQTTMesh::onConnect(AsyncClient* c) { //when this Node itself get a connection, not if a nother Node logs into this AP!
    dbgPrintln(EMMDBG_WIFI, "Connected to mesh");
    p2pConnected = true;
#if ASYNC_TCP_SSL_ENABLED
    if (mesh_secure.cert) {
        SSL* clientSsl = c->getSSL();
        bool sslFoundFingerprint = false;
        uint8_t *fingerprint;
        if (! clientSsl) {
            dbgPrintln(EMMDBG_WIFI, "Connection is not secure");
        } else if(onSslFileRequest("fingerprint", &fingerprint)) {
            if (ssl_match_fingerprint(clientSsl, fingerprint) == SSL_OK) {
                sslFoundFingerprint = true;
            }
            free(fingerprint);
        }

        if (!sslFoundFingerprint) {
            dbgPrintln(EMMDBG_WIFI, "Couldn't match SSL fingerprint");
            c->close(true);
            return;
        }
    }
#endif
    char msg[128];
    get_fw_string(msg, sizeof(msg), "Connected");
    publish(outTopic, "", "connect", msg, MSG_TYPE_NONE);
    send_connected_msg();
    setup_AP();
    wasConnected = true;
}

void ESP8266MQTTMesh::onDisconnect(AsyncClient* c) {
    if (c == espClient[0]) {
        dbgPrintln(EMMDBG_WIFI, "Disconnected from mesh");
        shutdown_AP();
        p2pConnected = false;
        WiFi.disconnect();
        return;
    }
    for (int i = 1; i <= ESP8266_NUM_CLIENTS; i++) {
        if (c == espClient[i]) {
            dbgPrintln(EMMDBG_WIFI, "Disconnected Client from this AP");
            delete espClient[i];
            espClient[i] = NULL;
            return;
        }
    }
    dbgPrintln(EMMDBG_WIFI, "Disconnected unknown client");
}
void ESP8266MQTTMesh::onError(AsyncClient* c, int8_t error) {
    dbgPrintln(EMMDBG_WIFI, "Got error on " + c->remoteIP().toString() + ": " + String(error));
}
void ESP8266MQTTMesh::onAck(AsyncClient* c, size_t len, uint32_t time) {
    dbgPrintln(EMMDBG_WIFI_EXTRA, "Got ack on " + c->remoteIP().toString() + ": " + String(len) + " / " + String(time));
}

void ESP8266MQTTMesh::onTimeout(AsyncClient* c, uint32_t time) {
    if(espClient[0] == c){ //Main Mesh Connection got Timeout
        dbgPrintln(EMMDBG_WIFI, "main Connection timed Out : " + String(time));
        shutdown_AP();
        p2pConnected = false;
        WiFi.disconnect();
        return;
    }else{ //connected Client timed out
        dbgPrintln(EMMDBG_WIFI, "Got timeout  " + c->remoteIP().toString() + ": " + String(time));
        c->close();
    }
}

void ESP8266MQTTMesh::onData(AsyncClient* c, void* data, size_t len) { //TODO: currently received Package has no String Terminator at the End, fix it!
    dbgPrintln(EMMDBG_WIFI_EXTRA, "Got data from " + c->remoteIP().toString() + ": " + String((char *)data));
    for (int idx = meshConnect ? 0 : 1; idx <= ESP8266_NUM_CLIENTS; idx++) {
        if (espClient[idx] == c) {
            if(bufptr[idx] + len > inbuffer[idx] + MQTT_MAX_PACKET_SIZE){
                dbgPrintln(EMMDBG_WIFI, "Bufferoverflow by handling fragmented Packages!!!! FragmentBuffer Adress: " + String(*bufptr[idx]) + ", Buffer Start: " + String(*inbuffer[idx]) + ", Package Length: " + String(len) + ", Max Package Length: " + String(MQTT_MAX_PACKET_SIZE));
                bufptr[idx] = inbuffer[idx];
                return;
            }
            char *dptr = (char *)data;
            for (size_t i = 0; i < len; i++) {
                if(dptr[i] == '\n') { //dptr[i]=='\n' steht immer am Ende eines vollständigen Paketes!
                    *bufptr[idx]++ = '\0'; //handles fragmented Packages even if a nother client sends Stuff in between
                    handle_client_data(idx, inbuffer[idx]);
                    bufptr[idx] = inbuffer[idx];
                }else{
                    *bufptr[idx]++ = dptr[i]; //handles fragmented Packages even if a nother client sends Stuff in between
                }
            }
            return;
        }
    }
    dbgPrintln(EMMDBG_WIFI, "Could not find client");
}

void ESP8266MQTTMesh::setID(const char *id){
    strncpy (myID, id, sizeof(myID));
    strlcat(myID, "/", sizeof(myID));
}

void ESP8266MQTTMesh::loop(){
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastBlink >= blinkInterval and do_blink) {
        lastBlink = currentMillis;
        
        int last_val = digitalRead(status_pin);
        digitalWrite(status_pin, not last_val);
    }
}

