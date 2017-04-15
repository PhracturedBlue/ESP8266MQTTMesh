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

