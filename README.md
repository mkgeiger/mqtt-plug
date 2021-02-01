# MQTT Plug
## Overview
Many WiFi plugs exist on the market from different manufacturers. I decided to use the `Luminea ZX-2820`. After the purchase, it turned out that this plug is difficult to open because there are no screws and the housing is glued, so I would have better purchased another one. But ok, you need a knife to cut the joints and to carefully open the housing without big dammage. Internally there is an ESP8285/ESP8266 module (Tuya TYWE2S) which can easily be reflashed with the available pads on the PCB. Also there is an intelligent single phase energy meter IC attached, which can measure voltage, current, different power types and the consumed energy. Also getting rid of the original firmware, which makes use of a Chinese MQTT cloud, is a good feeling. Instead, my software connects to a local self maintained MQTT broker (see other project from me).

## Hardware
### Main PCB
The hardware is the commercial product `Luminea ZX-2820`. It is sold by PEARL (https://www.pearl.de/a-ZX2820-3103.shtml) and Amazon (https://www.amazon.de/-/en/Luminea-Home-Control-Elesion-socket/dp/B078159RF6?language=de_DE).
![ZX2820](/photos/ZX2820.png)

### ESP8285 (Tuya TYWE2S) module
There is an ESP8285 (Tuya TYWE2S) module sitting on the PCB. It is in fact an ESP8266 with internal flash (1Mbyte). The following table shows the assignment between the GPIOs and their functions:

  Signal     | TYWE2S | Type
  -----------|--------|----------
  Button     | GPIO3  | input
  BL0937 CF  | GPIO4  | interrupt
  BL0937 CF1 | GPIO5  | interrupt
  BL0937 SEL | GPIO12 | output
  LED        | GPIO13 | output
  REL        | GPIO14 | output
  
![TYWE2S](/hardware/TYWE2S.png)

### BL0937 (Single Phase Energy Meter IC)
This single phase energy meter IC is very similar to the very popular HLW8012, at least it is pin compatible. By comparing the datasheets there are some significant differences. I took the driver code from https://github.com/xoseperez/hlw8012 and ported it to the BL0937, which you can be find beside the main program. Most important changes affected the 
* parameterization, e.g. internal reference voltage, up-/downstream resistors (measured), shunt resistor (measured), internal clock, etc.
* correction factors for calculated voltage, current and power. These were determined by comparing with measured values in a multimeter.
* interrupt handling, which was changed from any edge to falling edge only.
The datasheet can be found here:
![BL0937](/datasheets/BL0937_V1.02_en.pdf)

## Serial connection
The serial header (3.3V, RXD, TXD, GND) as well as GPIO0 and RESET (IO0, RST) are populated as test pads on the backside of the PCB and on the Tuya TYWE2S module itself. For accessing the serial header to solder some cables a small hole needs to be cut into the housing (the hole can be closed finally again with some hot glue). You can easily add some solder to fix the wires for the flash process. You need to connect to the serial programming interface of the ESP8266 chip. This is done by connecting any serial-to-USB converter (e.g. the FT232R) TX, RX, 3.3V and GND pins to the ESP8266 RX, TX, 3.3V and GND pins (cross connection!). Recheck your serial-to-USB converter so to ensure that it supplies 3.3V voltage and NOT 5V. 5V will damage the ESP chip!
![Uart](/photos/Uart.png)

## Flash mode and OTA (Over the Air)
To place the board into flashing mode the very first time, you will need to short IO0 (GPIO0) to GND. This can remain shorted while flashing is in progress, but you will need to remove the short in order to boot afterwards the flashed software.
In case you successfully flashed the very first time and the flashed software boots up, it can easily be reflashed by Over the Air and Wifi. The Arduino IDE supports this features and you can select the IP-address of your plug instead the UART channel for the software download. The password for OTA is `esp8266`. A successful flash process by OTA will be indicated afterwards by fast LED toggling.
![FlashReset](/photos/FlashReset.png)

## Installation
1. install Arduino IDE 1.8.1x
2. download and install the ESP8266 board supporting libraries with this URL: http://arduino.esp8266.com/stable/package_esp8266com_index.json
3. select the `Lolin(Wemos) D1 mini Lite` board
4. install the `Async MQTT client` library: https://github.com/marvinroger/async-mqtt-client/archive/master.zip
5. install the `Async TCP` library: https://github.com/me-no-dev/ESPAsyncTCP/archive/master.zip
6. install the `ArduinoOTA` library.
7. compile and flash

## SW configuration
The configuration is completely done in the web frontend of the WifiManager. At first startup, the software boots up in access point mode. In this mode you can configure parameters like
* Wifi SSID
* Wifi password
* MQTT broker IP address
* MQTT user
* MQTT password

After these settings were saved, with the next startup, the software boots into normal operating mode and connects to your Wifi and MQTT broker. A successful connection to the Broker is indicated by switching on the LED. Entering again into the WifiManager configuration menu can be done be holding the push button pressed during the startup of the software.

## SW normal operation
The software subsribes to MQTT topics, over which the relais state of the ZX-2820 can be changed and the consumed energy can be resetted. The software publishes every 3 seconds the new sampled values for the voltage, current, active power, reactive power, apparent power and consumed energy. Also the software supports re-connection to Wifi and to the MQTT broker in case of power loss, Wifi loss or MQTT broker unavailability. The MQTT topics begin with the device specific MAC-address string (in the following "A020A600F73A" as an example). This is useful when having multiple controllers in your MQTT cloud to avoid collisions.

Subscribe topics:
* Topic: "/A020A600F73A/relais"        Payload: "0" - "1"
* Topic: "/A020A600F73A/resenergy"     Payload: none

Publish topics:
* Topic: "/A020A600F73A/voltage"       Payload: "0" - "999"             voltage [Volt]
* Topic: "/A020A600F73A/current"       Payload: "0.00" - "99.99"        current [Ampere]
* Topic: "/A020A600F73A/actPower"      Payload: "0" - "9999"            actice power [Watt]
* Topic: "/A020A600F73A/reactPower"    Payload: "0" - "9999"            reactice power [Watt]
* Topic: "/A020A600F73A/appPower"      Payload: "0" - "9999"            apparent power [Watt]
* Topic: "/A020A600F73A/energy"        Payload: "0.000" - "9999.999"    energy [kWh]

## Photos
Opened housing:
![Plug1](/photos/Plug1.png)

TYWE2S and BL0937 modules on the PCB:
![Plug2](/photos/Plug2.png)

