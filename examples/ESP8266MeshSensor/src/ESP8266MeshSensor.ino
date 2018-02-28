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
 *
 *  HLW8012 code Copyright (C) 2016-2017 by Xose Pérez <xose dot perez at gmail dot com>
 */

/* Sonoff POW w/ DS18B20 attached to GPIO2(SDA) */
#define GREEN_LED   15 //MTDO
#define RELAY       12 //MTDI
#define BUTTON       0 //GPIO0
#define DS18B20      2 //GPIO2
#define HLW8012_SEL  5 //GPIO5
#define HLW8012_CF  14 //MTMS
#define HLW8012_CF1 13 //MTCK

/* See credentials.h.examle for contents of credentials.h */
#include "credentials.h"
#include "capabilities.h"
#include <ESP8266WiFi.h>
#include <FS.h>
#if HAS_DS18B20
    #include <OneWire.h>
    #include <DallasTemperature.h>
#endif
#if HAS_HLW8012
    #include <HLW8012.h>
#endif

#include <ESP8266MQTTMesh.h>

#if HAS_DS18B20 && HAS_HLW8012
    #define      FIRMWARE_ID        0x4455
#elif HAS_DS18B20
    #define      FIRMWARE_ID        0x4454
#elif HAS_HLW8012
    #define      FIRMWARE_ID        0x4453
#else
    #define      FIRMWARE_ID        0x4452
#endif

#define      FIRMWARE_VER       "0.8.1"
//const char*  networks[]       = NETWORK_LIST;
const wifi_conn networks[] = {
    WIFI_CONN("foo", NETWORK_PASSWORD, NULL, false),
    WIFI_CONN("bar", NETWORK_PASSWORD, "XX:XX:XX:XX:XX:XX", true),
    NULL
};
const char*  mesh_password    = MESH_PASSWORD;
const char*  mqtt_server      = MQTT_SERVER;

#if HAS_DS18B20
OneWire oneWire(DS18B20);
DallasTemperature ds18b20(&oneWire);
DeviceAddress ds18b20Address;
#endif

#if HAS_HLW8012
#define HLW8012_CURRENT_R           0.001
#define HLW8012_VOLTAGE_R_UP        ( 5 * 470000 ) // Real: 2280k
#define HLW8012_VOLTAGE_R_DOWN      ( 1000 ) // Real 1.009k
#define HLW8012_UPDATE_INTERVAL     2000
#define HLW8012_MIN_CURRENT         0.05
#define HLW8012_MIN_POWER           10
HLW8012 hlw8012;
unsigned int power_sum;
unsigned int voltage_sum;
double current_sum;
double energy; //in Watt-Seconds
unsigned long power_sample_count = 0;
bool hlw8012Enabled = false;

void hlw8012_cf1_interrupt();
void hlw8012_cf_interrupt();
void hlw8012_enable_interrupts(bool enabled);
unsigned int hlw8012_getActivePower();
double hlw8012_getCurrent();
unsigned int hlw8012_getVoltage();
#endif

ESP8266MQTTMesh mesh = ESP8266MQTTMesh::Builder(networks, mqtt_server)
                     .setVersion(FIRMWARE_VER, FIRMWARE_ID)
                     .setMeshPassword(mesh_password)
                     .build();

bool relayState = false;
bool stateChanged = false;
int  heartbeat  = 60000;
float temperature = 0.0;

void read_config();
void save_config();
void callback(const char *topic, const char *msg);
String build_json();

void setup() {
    pinMode(GREEN_LED, OUTPUT);
    pinMode(RELAY,     OUTPUT);
    pinMode(BUTTON,     INPUT);
    Serial.begin(115200);
    delay(5000);
    mesh.setCallback(callback);
    mesh.begin();
#if HAS_DS18B20
    ds18b20.begin();
    ds18b20.getAddress(ds18b20Address, 0);
    ds18b20.setWaitForConversion(false);
    ds18b20.requestTemperatures();
#endif
Serial.println("HLW8012 start");
#if HAS_HLW8012
    hlw8012.begin(HLW8012_CF, HLW8012_CF1, HLW8012_SEL, HIGH, true);
    hlw8012.setResistors(HLW8012_CURRENT_R, HLW8012_VOLTAGE_R_UP, HLW8012_VOLTAGE_R_DOWN);
#endif
Serial.println("HLW8012 end");
    //mesh.setup will initialize the filesystem
    if (SPIFFS.exists("/config")) {
        read_config();
    }
Serial.println("config end");
    digitalWrite(RELAY, relayState);
}

void loop() {
    static unsigned long pressed = 0;
    static unsigned long lastSend = 0;
    static bool needToSend = false;

    unsigned long now = millis();

#if HAS_DS18B20
    if (ds18b20.isConversionAvailable(ds18b20Address)) {
        temperature = ds18b20.getTempF(ds18b20Address);
        ds18b20.requestTemperatures();
    }    
#endif

#if HAS_HLW8012
    if (! hlw8012Enabled && mesh.connected()) {
        hlw8012_enable_interrupts(true);
    } else if (hlw8012Enabled && ! mesh.connected()) {
        hlw8012_enable_interrupts(false);
    }
    static unsigned long last_hlw8012_update = 0;
    if (now - last_hlw8012_update > HLW8012_UPDATE_INTERVAL) {
        static unsigned int power[3] = {0};
        static unsigned int voltage[3] = {0};
        static double current[3] = {0};
        for (int i = 0; i < 2; i++) {
            power[i] = power[i+1];
            voltage[i] = voltage[i+1];
            current[i] = current[i+1];
        }
        power[2]    = hlw8012_getActivePower();
        voltage[2]  = hlw8012_getVoltage();
        current[2]  = hlw8012_getCurrent();

        //Spike removal
        if (power[1] > 0 && power[0] == 0 && power[2] == 0) {
            power[1] = 0;
        }
        if (current[1] > 0 && current[0] == 0 && current[2] == 0) {
            current[1] = 0;
        }
        if (voltage[1] > 0 && voltage[0] == 0 && voltage[2] == 0) {
            voltage[1] = 0;
        }
        power_sum += power[0];
        current_sum += current[0];
        voltage_sum += voltage[0];
        energy += 1.0 * power[0] * (now - last_hlw8012_update) / 1000.0;
        power_sample_count++;
        last_hlw8012_update = now;
    }
#endif
    if (! digitalRead(BUTTON))  {
        if(pressed == 0) {
            relayState = ! relayState;
            digitalWrite(RELAY, relayState);
            stateChanged = true;
        }
        pressed = now;
    } else if (pressed && now - pressed > 100) {
        pressed = 0;
    }
    if (stateChanged) {
        save_config();
        needToSend = true;
        stateChanged = false;
    } else if (now - lastSend > heartbeat) {
        needToSend = true;
    }
    if (! mesh.connected()) {
        return;
    }
    if (needToSend) {
        lastSend = now;
        String data = build_json();
#if HAS_HLW8012
        power_sum = 0;
        current_sum = 0;
        voltage_sum = 0;
        power_sample_count = 0;
#endif
        mesh.publish("status", data.c_str());
        needToSend = false;
    }   
}

void callback(const char *topic, const char *msg) {
    if (0 == strcmp(topic, "heartbeat")) {
       unsigned int hb = strtoul(msg, NULL, 10);
       if (hb > 10000) {
           heartbeat = hb;
           save_config();
       }
    }
    else if (0 == strcmp(topic, "state")) {
       bool nextState = strtoul(msg, NULL, 10) ? true : false;
       if (relayState != nextState) {
           relayState = nextState;
           digitalWrite(RELAY, relayState);
           stateChanged = true;
       }
    }
#if HAS_HLW8012
    else if (0 == strcmp(topic, "expectedpower")) {
       int pow = atoi(msg);
       if (pow > 0) {
           hlw8012.expectedActivePower(pow);
           save_config();
       }
    }
    else if (0 == strcmp(topic, "expectedvoltage")) {
       int volt = atoi(msg);
       if (volt > 0) {
           hlw8012.expectedVoltage(volt);
           save_config();
       }
    }
    else if (0 == strcmp(topic, "expectedcurrent")) {
       double current = atof(msg);
       if (current > 0) {
           hlw8012.expectedCurrent(current);
           save_config();
       }
    }
    else if (0 == strcmp(topic, "resetpower")) {
       int state = atoi(msg);
       if (state > 0) {
           hlw8012.resetMultipliers();
           save_config();
       }
    }
#endif //HAS_HLW8012
}

String build_json() {
    String msg = "{";
    msg += " \"relay\":\"" + String(relayState ? "ON" : "OFF") + "\"";
#if HAS_DS18B20
        msg += ", \"temp\":" + String(temperature, 2);
#endif
#if HAS_HLW8012
        double count = power_sample_count ? power_sample_count : 1;
        double power   = (double)power_sum / count;
        double current = (double)current_sum / count;
        double voltage = (double)voltage_sum / count;
        double apparent= voltage * current;
        double pfactor = (apparent > 0) ? 100 * power / apparent : 100;
        if (pfactor > 100) {
            pfactor = 100;
        }
        msg += ", \"power\":" + String(power, 3);
        msg += ", \"current\":" + String(current, 3);
        msg += ", \"voltage\":" + String(voltage, 3);
        msg += ", \"pf\":" + String(pfactor, 3);
        msg += ", \"energy\":" + String(energy / 3600, 3);      //Watt-Hours
#endif
    msg += "}";
    return msg;
}

void read_config() {
    File f = SPIFFS.open("/config", "r");
    if (! f) {
        Serial.println("Failed to read config");
        return;
    }
    while(f.available()) {
        char s[32];
        char key[32];
        const char *value;
        s[f.readBytesUntil('\n', s, sizeof(s)-1)] = 0;
        if (! ESP8266MQTTMesh::keyValue(s, '=', key, sizeof(key), &value)) {
            continue;
        }
        if (0 == strcmp(key, "RELAY")) {
            relayState = value[0] == '0' ? 0 : 1;
        }
        else if (0 == strcmp(key, "HEARTBEAT")) {
            heartbeat = atoi(value);
            if (heartbeat < 1000) {
                heartbeat = 1000;
            } else if (heartbeat > 60 * 60 * 1000) {
                heartbeat = 5 * 60 * 1000;
            }
        }
#if HAS_HLW8012
        else if  (0 == strcmp(key, "hlw8012PowerMult")) {
            double dbl = atof(value);
            if (dbl > 0) hlw8012.setPowerMultiplier(dbl);
             
        }
        else if  (0 == strcmp(key, "hlw8012CurrentMult")) {
            double dbl = atof(value);
            if (dbl > 0) hlw8012.setCurrentMultiplier(dbl);
        }
        else if  (0 == strcmp(key, "hlw8012VoltageMult")) {
            double dbl = atof(value);
            if (dbl > 0) hlw8012.setVoltageMultiplier(dbl);
        }
#endif //HAS_HLW8012
    }
    f.close();
}

void save_config() {
    File f = SPIFFS.open("/config", "w");
    if (! f) {
        Serial.println("Failed to write config");
        return;
    }
    f.print("RELAY=" + String(relayState ? "1" : "0") + "\n");
    f.print("HEARTBEAT=" + String(heartbeat) + "\n");
#if HAS_HLW8012
    f.print("hlw8012PowerMult="   + String(hlw8012.getPowerMultiplier()));
    f.print("hlw8012CurrentMult=" + String(hlw8012.getCurrentMultiplier()));
    f.print("hlw8012VoltageMult=" + String(hlw8012.getVoltageMultiplier()));
#endif
    f.close();
}


// When using interrupts we have to call the library entry point
// whenever an interrupt is triggered
#if HAS_HLW8012
void hlw8012_cf1_interrupt() {
    hlw8012.cf1_interrupt();
}

void hlw8012_cf_interrupt() {
    hlw8012.cf_interrupt();
}
void hlw8012_enable_interrupts(bool enabled) {
    if (enabled) {
        attachInterrupt(HLW8012_CF1, hlw8012_cf1_interrupt, CHANGE);
        attachInterrupt(HLW8012_CF,  hlw8012_cf_interrupt,  CHANGE);
        hlw8012Enabled = true;
    } else {
        detachInterrupt(HLW8012_CF1);
        detachInterrupt(HLW8012_CF);
        hlw8012Enabled = false;
    }
}
unsigned int hlw8012_getActivePower() {
    unsigned int power = hlw8012.getActivePower();
    if (power < HLW8012_MIN_POWER) power = 0;
    return power;
}

double hlw8012_getCurrent() {
    double current = hlw8012.getCurrent();
    if (current < HLW8012_MIN_CURRENT) current = 0;
    return current;
}

unsigned int hlw8012_getVoltage() {
    return hlw8012.getVoltage();
}
#endif
