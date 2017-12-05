# SPIFFS Filesystem usage

The ESP8266MQTTMesh code takes advantage of using a SPIFFS file-system to store persistant data.  Specifically, the mapping of known mesh-node MAC addresses to subdomains is stored here.  The filesystem is also used to store meta-data during an OTA update (the actual OTA firmware is stored in the free space outside the filesystem).

The following files are used by the ESP8266MQTTMesh code:
* /ota : File containing meta-data for OTA (checksum and size of firmware to be uploaded)
* /bssid/<MAC address> : Mapping of mesh node to sub-domain.  Mac addresses are all-caps, in the form AA:BB:CC:DD:EE:FF 

The /bssid/<MAC address> file contains only a single integer: the subdomain of the given mesh node followed by a new-line

I.e. for mesh node `mesh_esp8266-4` with MAC address `5E:CF:7F:A0:33:EB`, there will be a file `/bssid/5E:CF:7F:A0:33:EB` containing `4`.  Note that there will be a file for each known node including a given node's own MAC address.

If you wish to pre-populate the filesystem to avoid the need for nodes to connect to the broker at least once, this can be done by creating a `bssid/` subdirectory locally, and filling it with the MAC-address/id mapping for each of your nodes.  You can then use platformio to upload the filesystem to each node following these [instructions](http://docs.platformio.org/en/latest/platforms/espressif8266.html#uploading-files-to-file-system-spiffs).  If pre-defining node mappings, it is important to also populate the broker withthis mapping, since nodes only store their subdomain on the broker during initial assignment, and the broker is the definitive store for each node's subdomain.


