# ESP8266MeshSensor

This example is designed to use the ESP8266MQTTMesh library with a Sonoff Relay
(it has only been tested with the SonoffPOW, but should work with other variants
with minor changes)

## Required Libraries
The following libraries are required to build ths example
* ESP8266MQTTMesh
* HLW8012 (if using a Sonoff POW)
* OneWire (if using DS18B20)
* DallasTemperature (is using DS18B20)

## Compiling
The collowing configurations are needed before compiling:
* copy credentials.h.example to credentials.h and update as needed
* ensure the relevant pins are defined if you have a DS18B20 or HLW8012

## MQTT Commands

| Topic               | Message     | Description |
|---------------------|-------------|-------------|
| \<prefix>/heartbeat       | \<number>    | How often to send current status in millseconds (default 60000) |
| \<prefix>/expectedpower   | \<number>    | Calculate HLW8012 calibration based on current power            |
| \<prefix>/expectedvoltage | \<number>    | Calculate HLW8012 calibration based on current voltage          |
| \<prefix>/expectedcurrent | \<number>    | Calculate HLW8012 calibration based on current current          |
| \<prefix>/resetpower      | \<number>    | Reset all of the HLW8012 calibration values                     |
