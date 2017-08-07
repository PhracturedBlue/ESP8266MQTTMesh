# ESP8266MeshHelloWorld

This is a trivial node that can be used to test the mesh network.

![](http://i.imgur.com/w0uKBjU.png)

If nodes can see the router, they communicate directly (Ex: 1 ->2). If not, they pass messages via the mesh network to other nodes. (Ex: 1 -> 2 -> 3)

## QUICKSTART
This describes one fast and easy way to get started, though there are other options.

You will require:
- A wirless router, with a dedicated secured (WPA2, AES) 2.4Ghz network, set to control channel 1 (2.412 Ghz), auto 20/40Mhz mode
- A computer (Linux or Windows tested) connected to the above network
- 1 or more ESP8266 chips to test with

1) Install the IDE: PlatformIO on the computer http://platformio.org/get-started/ide?install=atom
1b) Install the ESP8266MQTTMesh Library. PlatfromIO Home -> Libraries -> Search -> ```ESP8266MQTTMesh``` -> Install\
This should automatically install all necessary pre-requisites. 
2) Install the MQTT Broker: Mosquitto and all pre-requisites https://mosquitto.org/download/
3) Install the MQTT UI/Monitor: MQTT.fx http://www.mqttfx.org/
4) Install Python, and paho-mqtt. In Windows: ```python -m pip install paho-mqtt``` This is needed for OTA updates.
5) In Windows, add outgoing and incoming firewall rules to allow ports 1883 and optionally 1884
6) Start the Mosquitto Broker. In Windows, start a administrator command prompt, navigate to C:\Program Files (x86)\mosquitto and type in ```net start mosquitto```
7) Start MQTT.fx and click connect. Button on the right should turn green. Go to the subscribe tab, and click scan under topics collector
8) Connect an ESP8266 module to the computer and Open this example in PlatformIO. Edit the example Configuration as detailed below in the Configuration section. Click Build and ensure that everything works. Then click Upload. If Uploading fails, ensure that the driver for the USB->Serial converter you are using is happy, and that the proper COM port has been selected in PlatformIO.
9) Start the Serial Monitor in PlatformIO to see debug messages as needed.
10) You should now see Topics appearing in MQTT.fx window from step 7). Clicking on a topic should show data
![](http://i.imgur.com/ucylCqR.png)
11) Publishing data to a node can also be done through the UI by selecting the esp8266-in/* topic that matches the esp8266-out/* topic seen in the subscribe window.

## Configuration
Edit `credentials.h` and modify the configuration to suit your environment

The following variables should be set:
 - *NETWORK_LIST* : Specifies the SSIDs of your wireless network
 - *NETWORK_PASSWORD* : Specifies the password of your wireless network
 - *MQTT_SERVER* : Specify the IP address of your MQTT broker
 - *MQTT_PORT* : Specify the port of your MQTT broker

The following can optionally be changed:
 - *MESH_PASSWORD* : A string used by all nodes to prevent unauthorized access to the mesh network
 - *BASE_SSID* : The SSID base used for the mesh (you shouldn't need to change this)
 - *MESH_PORT* : The port that the mesh nodes listen on

These options are only relevant if the node is compiled with SSL enabled:
 - *MESH_SECURE* : Enable SSL bewteen mesh nodes.  This requires that the 'ssl/' directory be uploaded to the node
 - *MQTT_SECURE* : Enable SSL connection to the MQTT broker.
 - *MQTT_FINGERPRINT* : a 20-byte string that uniquely identifies your MQTTbroker.  A script to retrieve the fingerprint can be found [here](https://github.com/marvinroger/async-mqtt-client/blob/master/scripts/get-fingerprint/get-fingerprint.py)
   
## Compiling and uploading
This example has been designed to use platformio for building and install

### Non-SSL
`platformio run --target upload`

### SSL (Still experimental)
#### Install development branch of ESP8266 libraries
`platformio platform install https://github.com/platformio/platform-espressif8266.git#feature/stage`
#### Upload filesystem with SSL keys
`platformio run --target uploadfs`
#### Upload firmware
`platformio run -e ssl --target upload`

## OTA (Over the Air, wireless firware Update) (Work in Progress)
PlatformIO stores compiled binaries by default in the same folder as the project. Navigate to ```YOURPROJECTNAME\.pioenvs\esp12e``` and copy firmware.bin.
This repository contains ```send_ota.py``` under utils. Copy this file to the same location as the firmware.bin
To run a OTA example, you can run: (Assuming Windows, and the firmware id is left unchanged from this example)
```python send_ota.py --bin firmware.bin --id 0x1337```
This *should* update all nodes in the network.

