# ESP8266MeshIRRemote

This is an example of an IR Blaster controlled via MQTT commands

The blaster uses Pronto codes (though it should be easy to add support for all protocols handled by the IRRemote library).
The Pronto protocol is very flexible, and should be able to handle virtually all IR codes.

Codes can be saved in a file on the device to make it easy to send frequently used codes

## Hardware

The IRBlaster directory contains the Eagle libraries for the IRBlaster used in this example.

The board is quite basic.  It uses an ESP01 module along with a 2n7000 to drive an IR LED.  A LM1117-3.3 module is connected
to a mini-usb to provide power.

This was built before Eagle
was moved to a subscription plan.  I have since moved to using KiCad, but there was no reason to redesign the schematics.

A PDF of the schematic is provided.  Boards can be ordered from OshPark here:
<a href="https://oshpark.com/shared_projects/EaJlztYI"><img src="https://oshpark.com/assets/badge-5b7ec47045b78aef6eb9d83b3bac6b1920de805e9a0c227658eac6e19a045b9c.png" alt="Order from OSH Park"></img></a>

## MQTT Commands

| Topic | Message | Description |
|-------|---------|-------------|
| \<topic>/send            | code=\<pronto code>                                           | send specified code one time |
| \<topic>/send            | repeat=5,code=\<pronto code>                                  | send specified code 5 times |
| \<topic>/send            | repeat=5,code=\<pronto code1>,repeat=3,pronto=\<pronto code2>  | send code1 5 times followed by sending code2 3 times |
| \<topic>/send            | repeat=5,file=\<filename1>,file=\<filename2>,...               | send pronto code previously saved in 'filename1' 5 times, followed by the contents of 'filename2' |
| \<topic>/list            | ""                                                           | returns a list of all saved files |
| \<topic>/debug           | \<1|0>                                                        | enable/disable debugging messages over MQTT |
| \<topic>/save/\<filename> | code=\<pronto code>                                           | save specified pronto code to 'filename' on esp8266 device |
| \<topic>/read/\<filename> | ""                                                           | return contents of 'flename' via MQTT |
