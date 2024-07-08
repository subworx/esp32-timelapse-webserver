# esp32-timelapse-webserver
ESP32-Cam with configurable time lapse option and web server.

Supports `AI Thinker` boards with `OV2640` cameras.

This project:
- initializes the OV2640 camera of your ESP32-Cam module
- connects to WiFi to receive the current date and time
- takes a picture every X seconds with the current time as filename
- saves the picture to SD

# Settings:
Change the following values near the top of the file to match your requirements:

`ssid`, `password` - match these to your local WiFi Access Point (ssid) and password.

`myTimezone` - check https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv, find your region and paste the appropriate value.

Change the last uncommented `delay()` value at the end of the file to adjust the picture interval. delay(1000) = 1 second.

# Requirements
Required Arduino IDE libraries:
- `AsyncTCP` by `dvarrel`
- `ESP Async WebServer` by `Me-No-Dev`
- `SD` by `Arduino, Sparkfun`
- `Time` by `Michael Margolis`

Install or update the ESP32 core by adding this URL to the [Additional Boards Manager URLs](https://docs.arduino.cc/learn/starting-guide/cores) section in the Arduino "Preferences".

`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

then go to the "Boards Manager" and install or update the "esp32" core.


If you have problems with the board being identified/found in Windows then you might need to install the [USB to UART Bridge drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers).

# Flashing
In Arduino IDE, make the following settings in the `Tools` menu:
- `Board`: esp32 -> AI Thinker ESP32-CAM
- `CPU Frequency`: 240 MHz (WiFi/BT)
- `Core Debug Level`: none
- `Erase All Flash Before Sketch Upload`: disabled, but may be useful to fix weird bugs
- `Flash Frequency`: 80 MHz
- `Flash Mode`: QIO
- `Partition Scheme`: Huge APP (3MB No OTA/1MB SPIFFS)
