# ESP8266MQTTMesh
Self-assembling mesh network built around the MQTT protocol for the ESP8266 with OTA support

## Overview
Provide a library that can build a mesh network between ESP8266 devices that will allow all nodes to communicate with an MQTT broker.
At least one node must be able to see a wiFi router, and there must me a host on the WiFi network running the MQTT broker.
The broker is responsible for retaining information about individual nodes (via retained messages)
Each node will expose a hidden AP which can be connected to from any other node on the network.  Note:  hiding the AP does not provide
any additional security, but does minimize the clutter of other WiFi clients in the area.

Additionally the library provides an OTA mechanism using the MQTT pathway which can update any/all nodes on the mesh.

This code was developed primarily for teh Sonoff line of relays, but should work with any ESP8266 board with sufficient flash memory

## OTA
While all nodes must run the same version of the ESP8622MQTTMesh library, each node may run a unique firmware with independent purposes.
The main purpose behind this library was to provide a backbone on which several home-automation sensors could be built.  As such
each node may need different code to achieve its purpose.  Because firmwares are large, and memory is limited on the ESP8266 platform,
there is only a single memory area to hold the incoming firmware.  To ensure that a given firmware is only consumed by the proper nodes,
The firmware defines a unique identifier that distinguishes itself from other code.  A given firmware is broadcast from the MQTT
broker to all nodes, but only nodes with a matching ID will update.

## Using the Library
### Prerequisites
This library has been converted to use Asynchronous communication for imroved reliability.  It requires the following libraries to be installed
* AsyncMqttClient
* ESPAsyncTCP

If OTA support is desired, the esp8266 module must have at least 1M or Flash (configured as 784k ROM, 256k SPIFFS)
### Library initialization
The ESP8266MQTTMesh library requires a number of parameters during initialization:
* `int firmware_id`:  This identifies a specific node codebase.  Each unique firmware should have its own id, and it should not be changed between revisions of the code
* `const char *firmware_ver`: This is a string that idenitfies the firmware.  It can be whatever you like.  It will be broadcast to the MQTT broker on successful connection
* `const char *networks[]`: A list of ssids to search for to connect to the wireless network.  the list should be terminated with an empty string
* `const char *network_password`: The password to use when connecting to the Wifi network.  Only a single password is supported, even if multiple SSIDs are specified
* `const char *mesh_password`: The password to use when a node connects to another node on the mesh
* `const char *base_ssid`: The base SSID used for mesh nodes.  The current node number (subnet) will be appended to this to create the node's unique SSID
* `const char *mqtt_server`: Host which runs the MQTT broker
* `int mqtt_port`: Port which the MQTT broker is running on
* `int mesh_port`: Port for mesh nodes to listen on for message parsing
* `const char *in_topic`: MQTT topic prefix for messages sent to the node
* `const char *out_topic`: MQTT topic prefix for messages from the node
### Interacting with the mesh
Besides the constructor, he code must call the `begin()` method during setup, and the `loop()` method in the main loop

If messages need to be received by the node, execute the `callback()` function during setup with a function pointer
(prototype: `void callback(const char *topic, const char *payload)`)

To send messages to the MQTT broker, use `publish(const char *topic, const char * payload)`
