# ESP8266MeshHelloWorld

This is a trivial node that can be used to test the mesh network.

## Configuration
a sample configuration is provided in `src/credentials.h.example`.

1) First copy `src/credentials.h.example` to `src/credentials.h`
2) Edit `credentials.h` and modify the configuration to suit your environment

The following variables should be set:
  *NETWORK_LIST* : Specifies the SSIDs of your wireless network
  *NETWORK_PASSWORD* : Specifies the password of your wireless network
  *MQTT_SERVER* : Specify the IP address of your MQTT broker
  *MQTT_PORT* : Specify the port of your MQTT broker

The following can optionally be changed:
  *MESH_PASSWORD* : A string used by all nodes to prevent unauthorized access to the mesh network
  *BASE_SSID* : The SSID base used for the mesh (you shouldn't need to change this)
  *MESH_PORT* : The port that the mesh nodes listen on

These options are only relevant if the node is compiled with SSL enabled:
  *MESH_SECURE* : Enable SSL bewteen mesh nodes.  This requires that the 'ssl/' directory be uploaded to the node
  *MQTT_SECURE* : Enable SSL connection to the MQTT broker.
  *MQTT_FINGERPRINT* : a 20-byte string that uniquely identifies your MQTTbroker.  A script to retrieve the fingerprint can be found [here](https://github.com/marvinroger/async-mqtt-client/blob/master/scripts/get-fingerprint/get-fingerprint.py)
   
## Compiling and uploading
This example has been designed to use platformio for building and install
Assuming you have already setup a platformio environment:

### Non-SSL
platformio run --target upload

### SSL (Still experimental)
#### Install development branch of ESP8266 libraries
platformio platform install https://github.com/platformio/platform-espressif8266.git#feature/stage
#### Upload filesystem with SSL keys
platformio run --target uploadfs
#### Upload firmware
platformio run -e ssl --target upload

