# ESP8266MeshHelloWorld

This is a trivial node that can be used to test the mesh network.

## Configuration
a sample configuration is provided in `src/credentials.h.example`.

1) First copy `src/credentials.h.example` to `src/credentials.h`
2) Edit `credentials.h` and modify the configuration to suit your environment

The following variables should be set:
 - *NETWORK_PASSWORD* : Specifies the password of your wireless network
 - *NETWORK_LIST* : Specifies the SSIDs of your wireless network
 - *MQTT_SERVER* : Specify the IP address of your MQTT broker
 - *MQTT_PORT* : Specify the port of your MQTT broker

The following can optionally be changed:
 - *MESH_PASSWORD* : A string used by all nodes to prevent unauthorized access to the mesh network
 - *MESH_PORT* : The port that the mesh nodes listen on

These options are only relevant if the node is compiled with SSL enabled:
 - *MESH_SECURE* : Enable SSL bewteen mesh nodes.  This requires that the `ssl_cert.h` file is present (Use [this](https://github.com/marvinroger/async-mqtt-client/blob/master/scripts/gen_server_cert.sh) script to generate this header)
 - *MQTT_SECURE* : Enable SSL connection to the MQTT broker.
 - *MQTT_FINGERPRINT* : a 20-byte string that uniquely identifies your MQTTbroker.  A script to retrieve the fingerprint can be found [here](https://github.com/marvinroger/async-mqtt-client/blob/master/scripts/get-fingerprint/get-fingerprint.py)
   
## Compiling and uploading
This example has been designed to use platformio for building and install
Assuming you have already setup a platformio environment:

### Non-SSL
`platformio run --target upload`

### SSL (Still experimental)
`platformio run -e ssl --target upload`
