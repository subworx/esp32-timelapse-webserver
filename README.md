# esp32-timelapse-webserver
ESP32-Cam with configurable time lapse option and web server.

Supports `AI Thinker` boards with `OV2640` cameras.

This project:
- initializes the OV2640 camera of your ESP32-Cam module
- connects to WiFi to receive the current date and time
- takes a picture every X seconds with the current time as filename
- saves the picture to SD

# Settings:
Change the values near the top of the file to match your requirements. All of these settings can be changed through the GUI later:

## Access Point settings
- `startAP` - Start in Access Point mode
- `AP_SSID` - Access Point name
- `AP_PASS` - Access Point password
- `Server_IP` and `Subnet_Mask` - Change if you need to

## Wireless client settings
- `connectWifi` - Connect to existing wireless network
-  `WIFI_SSID`, `WIFI_PASS` - Replace with your wireless network name and password
-  `WIFI_HOSTNAME` - How the ESP32 will be named on the network

## Other settings 
- `myTimezone` - Check https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv, find your region and paste the appropriate value.
- `CAM120DEG` - set to true to automatically enable aec2, vflip and hmirror. 
- various camera values - Some is self-explanatory, some can be found in the 
[ESP32 sources](https://github.com/espressif/esp32-camera/blob/master/driver/esp_camera.c), among other places.
- `period` - Delay between pictures. `period=1000;` means one picture every second.

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

# Internal pages
- **admin.html** - the main landing page for administration.
- **index.html** - if no index.html is found the server will generate a simple empty index.
- **info.html** - provides information about the esp board.
- **upload.html** - used to upload files to the esp board for the webserver.
- **update.html** - used to update the firmware on the esp board (fwupdate.bin).
- **fileman.html** - used to view / download / delete files on the internal storage of the esp board.
- **config.html** - used to configure WiFi settings.
- **camconfig.html** - used to configure camera settings.
- **format.html** - used to format the internal storage of the esp board.
- **reboot.html** - used to reboot the esp board.

If you want to edit some of the internal pages, they can be found in `Pages.h` and are gzipped:
- Copy the numbers from any PROGMEM block and paste them into  https://gchq.github.io/CyberChef/.
- Use `From Decimal (Comma) -> Gunzip` to create HTML.
- Edit what you want.
- Copy the new HTML into the top box, then use `Gzip (with filename) -> To Decimal (comma)` to calculate numbers again.
- Paste these numbers back into the correct block in Pages.h.
