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

If SSL support is required, the development (staging) branch of the esp8266 library is also needed.
If using platformio, this can be installed via these [instructions](http://docs.platformio.org/en/latest/platforms/espressif8266.html#using-arduino-framework-with-staging-version).

**NOTE:** Enabling SSL will add ~70kB to the firmware size, and may make it impossible to use OTA updates depending on firmware and flash size.

If OTA support is desired, the esp8266 module must have at least 1M or Flash (configured as 784k ROM, 256k SPIFFS).  The OTA image is stored
between the end of the firmware image and the beginning of the filesystem (i.e. not in the filesystem itself).  Thus, for a 1M Flash, the firmware can
be no larger than ~390kB

### Library initialization
The ESP8266MQTTMesh only requires 3 parameters to initialize, but there are many additional optional parameters:
```
ESP8266MQTTMesh mesh = ESP8266MQTTMesh::Builder(networks, network_password, mqtt_server, mqtt_port).build();
```
- `const char *networks[]` *Required*: A list of ssids to search for to connect to the wireless network.  the list should be terminated with an empty string
- `const char *network_password` *Required*: The password to use when connecting to the Wifi network.  Only a single password is supported, even if multiple SSIDs are specified
- `const char *mqtt_server` *Required*: Host which runs the MQTT broker
- `int mqtt_port` *Optional*: Port which the MQTT broker is running on.  Defaults to 1883 if MQTT SSL is not enabled.  Defaults to 8883 is MQTT SSL is enabled

Additional Parameters can be enabled via the *Builder* for example:
```
ESP8266MQTTMesh mesh = ESP8266MQTTMesh::Builder(networks, network_password, mqtt_server, mqtt_port)
                       .setVersion(firmware_ver, firmware_id)
                       .setMeshPassword(password)
                       .build()
                       .build();
```
These additional parameters are specified before calling build(), and as few or as many can be used as neeeded.

```
setVersion(firmware_ver, firmware_id)
```
- `const char *firmware_ver`: This is a string that idenitfies the firmware.  It can be whatever you like.  It will be broadcast to the MQTT broker on successful connection
- `int firmware_id`:  This identifies a specific node codebase.  Each unique firmware should have its own id, and it should not be changed between revisions of the code

```
setMqttAuth(username, password)
```
- `const char *username`: The username used to login to the MQTT broker
- `const char *password`: The password used to login to the MQTT broker

```
setMeshPassword(password)
```
- `const char *password`: The password to use when a node connects to another node on the mesh.  Default: `ESP8266MQTTMesh`

```
setBaseSSID(ssid)
```
- `const char *ssid`: The base SSID used for mesh nodes.  The current node number (subnet) will be appended to this to create the node's unique SSID.  Default: `mesh_esp8266-`

```
setMeshPort(port)
```
- `int port`: Port for mesh nodes to listen on for message parsing. Default: `1884`

```
setTopic(in_topic, out_topic)
```
- `const char *in_topic`: MQTT topic prefix for messages sent to the node.  Default: `esp8266-in/`
- `const char *out_topic`: MQTT topic prefix for messages from the node. Default: `esp8266-out/`

If SSL support is enabled, the following optional parameters are available:
```
setMqttSSL(enable, fingerprint)
```
- `bool enable`: Enable MQTT SSL support.  Default: `false`
- `const uint8_t *fingerprint`: Fingerprint to verify MQTT certificate (prevent man-in-the-middle attacks)

```
setMeshSSL(enable)
```
- `bool enable`: Enable SSL connection between mesh nodes

### Interacting with the mesh
Besides the constructor, he code must call the `begin()` method during setup, and the `loop()` method in the main loop

If messages need to be received by the node, execute the `callback()` function during setup with a function pointer
(prototype: `void callback(const char *topic, const char *payload)`)

To send messages to the MQTT broker, use `publish(const char *topic, const char * payload)`

### SSL support
SSL support is enabled by defining `ASYNC_TCP_SSL_ENABLED=1`.  This must be done globally during build.

Once enabled, SSL can be optionally enabled between the node and the MQTT broker or between mesh nodes (or both).

#### Using SSL with the MQTT Broker
**WARNING:** Make sure you do not use SHA512 certificate signatures on your MQTT broker.  They are not suppoorted by ESP8266 properly
Using an optional fingerprint ensures that the MQTT Broker is the one you expect.  Specifying a fingerprint is useful to prevent man-in-the-middle attacks.
The fingerprint can be generated by running:
```
utils/get_mqtt_fingerprint.py --host <MQTT host> --post <MQTT port>
```
This will also check the signature to make sure it is compatible with ESP8266

#### Using SSL between mesh nodes
**NOTE:** Enabling SSL between mesh nodes should not provide additional security, since the mesh connections are already secured via WPA, so enabling this is not recommended

To use SSL between mesh nodes, you must install 3 files into the '/ssl' directory on the SPIFFS filesystem of the node:
* `ssl/server.cer`
* `ssl/server.key`
* `ssl/fingerprint`

These files can be generated by running:
```
utils/gen_server_cert.sh
```
