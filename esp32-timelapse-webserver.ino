/*
TODOs:
- fix config read/write/everything
- webcam stream?
*/

// https://www.instructables.com/ESP32-CAM-Take-Photo-and-Save-to-MicroSD-Card-With/
// https://github.com/stooged/ESP32-Server-900u/


#include "esp_camera.h"
#include "FS.h"                // SD Card ESP32
#include "SD_MMC.h"            // SD Card ESP32
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
#include "driver/rtc_io.h"
#include <WiFi.h>
#include "time.h"
#include "ESPAsyncWebServer.h"
#include "esp_task_wdt.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>

//-------------------DEFAULT SETTINGS------------------//

                        // use config.ini [ true / false ]
#define USECONFIG true  // this will allow you to change these settings below via the admin webpage. \
                        // if you want to permanently use the values below then set this to false.

#define FILESYS SD_MMC

//create access point
boolean startAP = false;
String AP_SSID = "esp32-cam";
String AP_PASS = "password";
IPAddress Server_IP(192, 168, 178, 100);
IPAddress Subnet_Mask(255, 255, 255, 0);

//connect to wifi
boolean connectWifi = true;
String WIFI_SSID = "xxxxx";
String WIFI_PASS = "xxxxx";
String WIFI_HOSTNAME = "esp32-cam";

//server port
int WEB_PORT = 80;

// Displayed firmware version
String firmwareVer = "1.10";

// REPLACE WITH YOUR TIMEZONE STRING: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
String myTimezone ="CET-1CEST,M3.5.0,M10.5.0/3";

int brightness = 0;     // -2 to 2
int contrast = 0;       // -2 to 2
int saturation = 0;     // -2 to 2
int special_effect = 0; // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
int whitebal = 1;       // 0 = disable , 1 = enable
int awb_gain = 1;       // 0 = disable , 1 = enable
int wb_mode = 0;        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
int exposure_ctrl = 1;  // 0 = disable , 1 = enable
int aec2 = 0;           // 0 = disable , 1 = enable
int ae_level = 0;       // -2 to 2
int aec_value = 300;    // 0 to 1200
int gain_ctrl = 1;      // 0 = disable , 1 = enable
int agc_gain = 0;       // 0 to 30
int gainceiling = (gainceiling_t)0;  // 0 to 6
int bpc = 0;            // 0 = disable , 1 = enable
int wpc = 1;            // 0 = disable , 1 = enable
int raw_gma = 1;        // 0 = disable , 1 = enable
int hmirror = 0;        // 0 = disable , 1 = enable
int lenc = 1;           // 0 = disable , 1 = enable
int vflip = 0;          // 0 = disable , 1 = enable
int dcw = 1;            // 0 = disable , 1 = enable
int colorbar = 0;       // 0 = disable , 1 = enable

//const unsigned long period = 1000; // period in milliseconds - 1sec
const unsigned long period = 180000; // period in milliseconds - 3min

//-----------------------------------------------------//

#include "Pages.h"
#include "jzip.h"

DNSServer dnsServer;
AsyncWebServer server(WEB_PORT);
boolean isFormating = false;
unsigned long bootTime = 0;
unsigned long startMillis;
unsigned long currentMillis;
bool firstPic = false; // has first picture (before period) been taken? false = no, yes = true
File upFile;


String split(String str, String from, String to) {
  String tmpstr = str;
  tmpstr.toLowerCase();
  from.toLowerCase();
  to.toLowerCase();
  int pos1 = tmpstr.indexOf(from);
  int pos2 = tmpstr.indexOf(to, pos1 + from.length());
  String retval = str.substring(pos1 + from.length(), pos2);
  return retval;
}


bool instr(String str, String search) {
  int result = str.indexOf(search);
  if (result == -1) {
    return false;
  }
  return true;
}


String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + " B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + " KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + " MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
  }
}


String urlencode(String str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  char code2;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      code2 = '\0';
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
    yield();
  }
  encodedString.replace("%2E", ".");
  return encodedString;
}


void sendwebmsg(AsyncWebServerRequest *request, String htmMsg) {
  String tmphtm = "<!DOCTYPE html><html><head><link rel=\"stylesheet\" href=\"style.css\"></head><center><br><br><br><br><br><br>" + htmMsg + "</center></html>";
  request->send(200, "text/html", tmphtm);
}


void handleFwUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    String path = request->url();
    if (path != "/update.html") {
      request->send(500, "text/plain", "Internal Server Error");
      return;
    }
    if (!filename.equals("fwupdate.bin")) {
      sendwebmsg(request, "Invalid update file: " + filename);
      return;
    }
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    //HWSerial.printf("Update Start: %s\n", filename.c_str());
    if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
      Update.printError(Serial);
      sendwebmsg(request, "Update Failed: " + String(Update.errorString()));
    }
  }
  if (!Update.hasError()) {
    if (Update.write(data, len) != len) {
      Update.printError(Serial);
      sendwebmsg(request, "Update Failed: " + String(Update.errorString()));
    }
  }
  if (final) {
    if (Update.end(true)) {
      //HWSerial.printf("Update Success: %uB\n", index+len);
      String tmphtm = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\"><style type=\"text/css\">body {background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}</style></head><center><br><br><br><br><br><br>Update Success, Rebooting.</center></html>";
      request->send(200, "text/html", tmphtm);
      delay(1000);
      ESP.restart();
    } else {
      Update.printError(Serial);
    }
  }
}


void handleDelete(AsyncWebServerRequest *request) {
  if (!request->hasParam("file", true)) {
    request->redirect("/fileman.html");
    return;
  }
  String path = request->getParam("file", true)->value();
  if (path.length() == 0) {
    request->redirect("/fileman.html");
    return;
  }
  if (FILESYS.exists("/" + path) && path != "/" && !path.equals("config.ini")) {
    FILESYS.remove("/" + path);
  }
  request->redirect("/fileman.html");
}



void handleFileMan(AsyncWebServerRequest *request) {
  File dir = FILESYS.open("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>File Manager</title><link rel=\"stylesheet\" href=\"style.css\"><style>body{overflow-y:auto;} th{border: 1px solid #dddddd; background-color:gray;padding: 8px;}</style><script>function statusDel(fname) {var answer = confirm(\"Are you sure you want to delete \" + fname + \" ?\");if (answer) {return true;} else { return false; }} </script></head><body><br><table id=filetable></table><script>var filelist = [";
  int fileCount = 0;
  while (dir) {
    File file = dir.openNextFile();
    if (!file) {
      dir.close();
      break;
    }
    String fname = String(file.name());
    if (fname.length() > 0 && !fname.equals("config.ini") && !file.isDirectory()) {
      fileCount++;
      fname.replace("|", "%7C");
      fname.replace("\"", "%22");
      output += "\"" + fname + "|" + formatBytes(file.size()) + "\",";
    }
    file.close();
    esp_task_wdt_reset();
  }
  if (fileCount == 0) {
    output += "];</script><center>No files found<br>You can upload files using the <a href=\"/upload.html\" target=\"mframe\"><u>File Uploader</u></a> page.</center></p></body></html>";
  } else {
    output += "];var output = \"\";filelist.forEach(function(entry) {var splF = entry.split(\"|\"); output += \"<tr>\";output += \"<td><a href=\\\"\" +  splF[0] + \"\\\">\" + splF[0] + \"</a></td>\"; output += \"<td>\" + splF[1] + \"</td>\";output += \"<td><a href=\\\"/\" + splF[0] + \"\\\" download><button type=\\\"submit\\\">Download</button></a></td>\";output += \"<td><form action=\\\"/delete\\\" method=\\\"post\\\"><button type=\\\"submit\\\" name=\\\"file\\\" value=\\\"\" + splF[0] + \"\\\" onClick=\\\"return statusDel('\" + splF[0] + \"');\\\">Delete</button></form></td>\";output += \"</tr>\";}); document.getElementById(\"filetable\").innerHTML = \"<tr><th colspan='1'><center>File Name</center></th><th colspan='1'><center>File Size</center></th><th colspan='1'><center><a href='/dlall' target='mframe'><button type='submit'>Download All</button></a></center></th><th colspan='1'><center><a href='/format.html' target='mframe'><button type='submit'>Delete All</button></a></center></th></tr>\" + output;</script></body></html>";
  }
  request->send(200, "text/html", output);
}


void handleDlFiles(AsyncWebServerRequest *request) {
  File dir = FILESYS.open("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>File Downloader</title><link rel=\"stylesheet\" href=\"style.css\"><style>body{overflow-y:auto;}</style><script type=\"text/javascript\" src=\"jzip.js\"></script><script>var filelist = [";
  int fileCount = 0;
  while (dir) {
    File file = dir.openNextFile();
    if (!file) {
      dir.close();
      break;
    }
    String fname = String(file.name());
    if (fname.length() > 0 && !fname.equals("config.ini") && !file.isDirectory()) {
      fileCount++;
      fname.replace("\"", "%22");
      output += "\"" + fname + "\",";
    }
    file.close();
    esp_task_wdt_reset();
  }
  if (fileCount == 0) {
    output += "];</script></head><center>No files found to download<br>You can upload files using the <a href=\"/upload.html\" target=\"mframe\"><u>File Uploader</u></a> page.</center></p></body></html>";
  } else {
    output += "]; async function dlAll(){var zip = new JSZip();for (var i = 0; i < filelist.length; i++) {if (filelist[i] != ''){var xhr = new XMLHttpRequest();xhr.open('GET',filelist[i],false);xhr.overrideMimeType('text/plain; charset=x-user-defined'); xhr.onload = function(e) {if (this.status == 200) {zip.file(filelist[i], this.response,{binary: true});}};xhr.send();document.getElementById('fp').innerHTML = 'Adding: ' + filelist[i];await new Promise(r => setTimeout(r, 50));}}document.getElementById('gen').style.display = 'none';document.getElementById('comp').style.display = 'block';zip.generateAsync({type:'blob'}).then(function(content) {saveAs(content,'esp_files.zip');});}</script></head><body onload='setTimeout(dlAll,100);'><center><br><br><br><br><div id='gen' style='display:block;'><div id='loader'></div><br><br>Generating ZIP<br><p id='fp'></p></div><div id='comp' style='display:none;'><br><br><br><br>Complete<br><br>Downloading: esp_files.zip</div></center></body></html>";
  }
  request->send(200, "text/html", output);
}


// TODO: Add CAM values etc
#if USECONFIG
void handleConfig(AsyncWebServerRequest *request) {
  if (request->hasParam("ap_ssid", true) && request->hasParam("ap_pass", true) && request->hasParam("web_ip", true) && request->hasParam("web_port", true) && request->hasParam("subnet", true) && request->hasParam("wifi_ssid", true) && request->hasParam("wifi_pass", true) && request->hasParam("wifi_host", true)) {
    AP_SSID = request->getParam("ap_ssid", true)->value();
    if (!request->getParam("ap_pass", true)->value().equals("********")) {
      AP_PASS = request->getParam("ap_pass", true)->value();
    }
    WIFI_SSID = request->getParam("wifi_ssid", true)->value();
    if (!request->getParam("wifi_pass", true)->value().equals("********")) {
      WIFI_PASS = request->getParam("wifi_pass", true)->value();
    }
    String tmpip = request->getParam("web_ip", true)->value();
    String tmpwport = request->getParam("web_port", true)->value();
    String tmpsubn = request->getParam("subnet", true)->value();
    String WIFI_HOSTNAME = request->getParam("wifi_host", true)->value();
    String tmpua = "false";
    String tmpcw = "false";
        if (request->hasParam("useap", true)) { tmpua = "true"; }
    if (request->hasParam("usewifi", true)) { tmpcw = "true"; }
        if (tmpua.equals("false") && tmpcw.equals("false")) { tmpua = "true"; }
    //TODO Fix to work with the other FS module, but to SD
    File iniFile = SD_MMC.open("/config.ini", "w");
    if (iniFile) {
      iniFile.print("\r\nAP_SSID=" + AP_SSID + "\r\nAP_PASS=" + AP_PASS + "\r\nWEBSERVER_IP=" + tmpip + "\r\nWEBSERVER_PORT=" + tmpwport + "\r\nSUBNET_MASK=" + tmpsubn + "\r\nWIFI_SSID=" + WIFI_SSID + "\r\nWIFI_PASS=" + WIFI_PASS + "\r\nWIFI_HOST=" + WIFI_HOSTNAME + "\r\nUSEAP=" + tmpua + "\r\nCONWIFI=" + tmpcw + "\r\n");
      iniFile.close();
    }
    String htmStr = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\"><style type=\"text/css\">#loader {z-index: 1;width: 50px;height: 50px;margin: 0 0 0 0;border: 6px solid #f3f3f3;border-radius: 50%;border-top: 6px solid #3498db;width: 50px;height: 50px;-webkit-animation: spin 2s linear infinite;animation: spin 2s linear infinite; } @-webkit-keyframes spin {0%{-webkit-transform: rotate(0deg);}100%{-webkit-transform: rotate(360deg);}}@keyframes spin{0%{ transform: rotate(0deg);}100%{transform: rotate(360deg);}}body {background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;} #msgfmt {font-size: 16px; font-weight: normal;}#status {font-size: 16px; font-weight: normal;}</style></head><center><br><br><br><br><br><p id=\"status\"><div id='loader'></div><br>Config saved<br>Rebooting</p></center></html>";
    request->send(200, "text/html", htmStr);
    delay(1000);
    ESP.restart();
  } else {
    request->redirect("/config.html");
  }
}
#endif

void handleReboot(AsyncWebServerRequest *request) {
  Serial.print("Rebooting ESP");
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", rebooting_gz, sizeof(rebooting_gz));
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
  delay(1000);
  ESP.restart();
}


#if USECONFIG
void handleConfigHtml(AsyncWebServerRequest *request) {
  String tmpUa = "";
  String tmpCw = "";
  if (startAP) { tmpUa = "checked"; }
  if (connectWifi) { tmpCw = "checked"; }
//TODO update "config page" for more shit
  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Config Editor</title><style type=\"text/css\">body {background-color: #1451AE; color: #ffffff; font-size: 14px;font-weight: bold;margin: 0 0 0 0.0;padding: 0.4em 0.4em 0.4em 0.6em;}input[type=\"submit\"]:hover {background: #ffffff;color: green;}input[type=\"submit\"]:active{outline-color: green;color: green;background: #ffffff; }table {font-family: arial, sans-serif;border-collapse: collapse;}td {border: 1px solid #dddddd;text-align: left;padding: 8px;}th {border: 1px solid #dddddd; background-color:gray;text-align: center;padding: 8px;}</style></head><body><form action=\"/config.html\" method=\"post\"><center><table><tr><th colspan=\"2\"><center>Access Point</center></th></tr><tr><td>AP SSID:</td><td><input name=\"ap_ssid\" value=\"" + AP_SSID + "\"></td></tr><tr><td>AP PASSWORD:</td><td><input name=\"ap_pass\" value=\"********\"></td></tr><tr><td>AP IP:</td><td><input name=\"web_ip\" value=\"" + Server_IP.toString() + "\"></td></tr><tr><td>SUBNET MASK:</td><td><input name=\"subnet\" value=\"" + Subnet_Mask.toString() + "\"></td></tr><tr><td>START AP:</td><td><input type=\"checkbox\" name=\"useap\" " + tmpUa + "></td></tr><tr><th colspan=\"2\"><center>Web Server</center></th></tr><tr><td>WEBSERVER PORT:</td><td><input name=\"web_port\" value=\"" + String(WEB_PORT) + "\"></td></tr><tr><th colspan=\"2\"><center>Wifi Connection</center></th></tr><tr><td>WIFI SSID:</td><td><input name=\"wifi_ssid\" value=\"" + WIFI_SSID + "\"></td></tr><tr><td>WIFI PASSWORD:</td><td><input name=\"wifi_pass\" value=\"********\"></td></tr><tr><td>WIFI HOSTNAME:</td><td><input name=\"wifi_host\" value=\"" + WIFI_HOSTNAME + "\"></td></tr><tr><td>CONNECT WIFI:</td><td><input type=\"checkbox\" name=\"usewifi\" " + tmpCw + "></td></tr><tr><th colspan=\"2\"><center>Auto USB Wait</center></th></tr><tr><td>WAIT TIME(ms):</td><td><input name=\"usbwait\" value=\" + USB_WAIT + \"></td></tr><tr><th colspan=\"2\"><center>ESP Sleep Mode</center></th></tr><tr><td>ENABLE SLEEP:</td><td><input type=\"checkbox\" name=\"espsleep\"  + tmpSlp + ></td></tr><tr><td>TIME TO SLEEP(minutes):</td><td><input name=\"sleeptime\" value=\" + TIME2SLEEP + \"></td></tr></table><br><input id=\"savecfg\" type=\"submit\" value=\"Save Config\"></center></form></body></html>";
  request->send(200, "text/html", htmStr);
}
#endif


void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    String path = request->url();
    if (path != "/upload.html") {
      request->send(500, "text/plain", "Internal Server Error");
      return;
    }
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    if (filename.equals("/config.ini")) { return; }
    //HWSerial.printf("Upload Start: %s\n", filename.c_str());
    upFile = FILESYS.open(filename, "w");
  }
  if (upFile) {
    upFile.write(data, len);
  }
  if (final) {
    upFile.close();
    //HWSerial.printf("upload Success: %uB\n", index+len);
  }
}


void handleInfo(AsyncWebServerRequest *request) {
  float flashFreq = (float)ESP.getFlashChipSpeed() / 1000.0 / 1000.0;
  FlashMode_t ideMode = ESP.getFlashChipMode();
  String mcuType = CONFIG_IDF_TARGET;
  mcuType.toUpperCase();
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>System Information</title><link rel=\"stylesheet\" href=\"style.css\"></head>";
  output += "<hr>###### Software ######<br><br>";
  output += "Firmware version " + firmwareVer + "<br>";
  output += "SDK version: " + String(ESP.getSdkVersion()) + "<br><hr>";
  output += "###### Board ######<br><br>";
  output += "MCU: " + mcuType + "<br>";
#if defined(USB_PRODUCT)
  output += "Board: " + String(USB_PRODUCT) + "<br>";
#endif
  output += "Chip Id: " + String(ESP.getChipModel()) + "<br>";
  output += "CPU frequency: " + String(ESP.getCpuFreqMHz()) + "MHz<br>";
  output += "Cores: " + String(ESP.getChipCores()) + "<br><hr>";
  output += "###### Flash chip information ######<br><br>";
  output += "Flash chip Id: " + String(ESP.getFlashChipMode()) + "<br>";
  output += "Estimated Flash size: " + formatBytes(ESP.getFlashChipSize()) + "<br>";
  output += "Flash frequency: " + String(flashFreq) + " MHz<br>";
  output += "Flash write mode: " + String((ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT"
                                                                     : ideMode == FM_DIO  ? "DIO"
                                                                     : ideMode == FM_DOUT ? "DOUT"
                                                                                          : "UNKNOWN"))
            + "<br><hr>";
  output += "###### Storage information ######<br><br>";
  output += "Storage Device: SD<br>";
  output += "Total Size: " + formatBytes(FILESYS.totalBytes()) + "<br>";
  output += "Used Space: " + formatBytes(FILESYS.usedBytes()) + "<br>";
  output += "Free Space: " + formatBytes(FILESYS.totalBytes() - FILESYS.usedBytes()) + "<br><hr>";
  if (ESP.getPsramSize() > 0) {
    output += "###### PSRam information ######<br><br>";
    output += "Psram Size: " + formatBytes(ESP.getPsramSize()) + "<br>";
    output += "Free psram: " + formatBytes(ESP.getFreePsram()) + "<br>";
    output += "Max alloc psram: " + formatBytes(ESP.getMaxAllocPsram()) + "<br><hr>";
  }
  output += "###### Ram information ######<br><br>";
  output += "Ram size: " + formatBytes(ESP.getHeapSize()) + "<br>";
  output += "Free ram: " + formatBytes(ESP.getFreeHeap()) + "<br>";
  output += "Max alloc ram: " + formatBytes(ESP.getMaxAllocHeap()) + "<br><hr>";
  output += "###### Sketch information ######<br><br>";
  output += "Sketch hash: " + ESP.getSketchMD5() + "<br>";
  output += "Sketch size: " + formatBytes(ESP.getSketchSize()) + "<br>";
  output += "Free space available: " + formatBytes(ESP.getFreeSketchSpace() - ESP.getSketchSize()) + "<br><hr>";
  output += "##### Camera Parameters #####<br><br>";
  output += "</html>";
  request->send(200, "text/html", output);
}

//TODO fix values, add cam
#if USECONFIG
void writeConfig() {
  File iniFile = FILESYS.open("/config.ini", "w");
  if (iniFile) {
    String tmpua = "false";
    String tmpcw = "false";
    String tmpslp = "false";
    if (startAP) { tmpua = "true"; }
    if (connectWifi) { tmpcw = "true"; }
    iniFile.print("\r\nAP_SSID=" + AP_SSID + "\r\nAP_PASS=" + AP_PASS + "\r\nWEBSERVER_IP=" + Server_IP.toString() + "\r\nWEBSERVER_PORT=" + String(WEB_PORT) + "\r\nSUBNET_MASK=" + Subnet_Mask.toString() + "\r\nWIFI_SSID=" + WIFI_SSID + "\r\nWIFI_PASS=" + WIFI_PASS + "\r\nWIFI_HOST=" + WIFI_HOSTNAME + "\r\nUSEAP=" + tmpua + "\r\nCONWIFI=" + tmpcw + "\r\nUSBWAIT= + USB_WAIT + \r\nESPSLEEP= + tmpslp + \r\nSLEEPTIME= + TIME2SLEEP + \r\n");
    iniFile.close();
  }
}
#endif


//----------------------//
//  EPS CAM FUNCTIONS   //
//----------------------//

// Pin definition for CAMERA_MODEL_AI_THINKER
// Change pin definition if you're using another ESP32 camera module
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22


// Stores the camera configuration parameters
camera_config_t config;


// Initializes the camera
void configInitCamera(){
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  //config.xclk_freq_hz = 20000000;
  config.xclk_freq_hz = 16000000;
  config.pixel_format = PIXFORMAT_JPEG; //YUV422,GRAYSCALE,RGB565,JPEG
  config.grab_mode = CAMERA_GRAB_LATEST;


  // Select lower framesize if the camera doesn't support PSRAM
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 10; //0-63 lower number means higher quality
    config.fb_count = 1;
    //Serial.printf(" - psram found - ");
  } else {
    //config.frame_size = FRAMESIZE_SVGA;
    config.frame_size = FRAMESIZE_UXGA;
    //config.jpeg_quality = 12;
    config.jpeg_quality = 10;
    config.fb_count = 1;
    //Serial.printf(" - NO psram found - ");
  }
  
  // Initialize the Camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  /*s->set_brightness(s, 0);     // -2 to 2
  s->set_contrast(s, 0);       // -2 to 2
  s->set_saturation(s, 0);     // -2 to 2
  s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
  s->set_aec2(s, 0);           // 0 = disable , 1 = enable
  s->set_ae_level(s, 0);       // -2 to 2
  s->set_aec_value(s, 300);    // 0 to 1200
  s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
  s->set_agc_gain(s, 0);       // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
  s->set_bpc(s, 0);            // 0 = disable , 1 = enable
  s->set_wpc(s, 1);            // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
  s->set_lenc(s, 1);           // 0 = disable , 1 = enable
  s->set_vflip(s, 0);          // 0 = disable , 1 = enable
  s->set_dcw(s, 1);            // 0 = disable , 1 = enable
  s->set_colorbar(s, 0);       // 0 = disable , 1 = enable*/
  s->set_brightness(s, brightness);     // -2 to 2
  s->set_contrast(s, contrast);       // -2 to 2
  s->set_saturation(s, saturation);     // -2 to 2
  s->set_special_effect(s, special_effect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_whitebal(s, whitebal);       // 0 = disable , 1 = enable
  s->set_awb_gain(s, awb_gain);       // 0 = disable , 1 = enable
  s->set_wb_mode(s, wb_mode);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_exposure_ctrl(s, exposure_ctrl);  // 0 = disable , 1 = enable
  s->set_aec2(s, aec2);           // 0 = disable , 1 = enable
  s->set_ae_level(s, ae_level);       // -2 to 2
  s->set_aec_value(s, aec_value);    // 0 to 1200
  s->set_gain_ctrl(s, gain_ctrl);      // 0 = disable , 1 = enable
  s->set_agc_gain(s, agc_gain);       // 0 to 30
  s->set_gainceiling(s, gainceiling_t(gainceiling));  // 0 to 6
  s->set_bpc(s, bpc);            // 0 = disable , 1 = enable
  s->set_wpc(s, wpc);            // 0 = disable , 1 = enable
  s->set_raw_gma(s, raw_gma);        // 0 = disable , 1 = enable
  s->set_hmirror(s, hmirror);        // 0 = disable , 1 = enable
  s->set_lenc(s, lenc);           // 0 = disable , 1 = enable
  s->set_vflip(s, vflip);          // 0 = disable , 1 = enable
  s->set_dcw(s, dcw);            // 0 = disable , 1 = enable
  s->set_colorbar(s, colorbar);       // 0 = disable , 1 = enable
}


// Connect to wifi
/*void initWiFi(){
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting Wifi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
}*/


// Function to set timezone
void setTimezone(String timezone){
  Serial.printf("  Setting Timezone to %s\n",timezone.c_str());
  setenv("TZ",timezone.c_str(),1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
}


// Connect to NTP server and adjust timezone
void initTime(String timezone){
  struct tm timeinfo;
  Serial.println("Setting up time");
  configTime(0, 0, "pool.ntp.org");    // First connect to NTP server, with 0 TZ offset
  if(!getLocalTime(&timeinfo)){
    Serial.println(" Failed to obtain time");
    return;
  }
  Serial.println("Got the time from NTP");
  // Now we can set the real timezone
  setTimezone(timezone);
}


// Get the picture filename based on the current ime
String getPictureFilename(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "";
  }
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d_%H-%M-%S", &timeinfo);
  //Serial.println(timeString);
  String filename = "/picture_" + String(timeString) +".jpg";
  return filename; 
}


// Initialize the micro SD card
void initMicroSDCard(){
  // Start Micro SD card
  Serial.println("Starting SD Card");
  if(!SD_MMC.begin()){
    Serial.println("SD Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD Card attached");
    return;
  }
}


// Take photo and save to microSD card
void takeSavePhoto(){
  // Take Picture with Camera
  camera_fb_t * fb = esp_camera_fb_get();
 
  //Uncomment the following lines if you're getting old pictures
  //esp_camera_fb_return(fb); // dispose the buffered image
  //fb = NULL; // reset to capture errors
  //fb = esp_camera_fb_get();
  
  if(!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
  }


  // Path where new picture will be saved in SD Card
  String path = getPictureFilename();
  //Serial.printf("Picture file name: %s\n", path.c_str());
  
  // Save picture to microSD card
  fs::FS &fs = SD_MMC; 
  File file = fs.open(path.c_str(),FILE_WRITE);
  if(!file){
    Serial.printf("Failed to open file in writing mode");
  } 
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved: %s\n", path.c_str());
  }
  file.close();
  esp_camera_fb_return(fb); 
}


void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector

  Serial.begin(115200);
  Serial.println("Version: " + firmwareVer);
  delay(2000);

#if USECONFIG
  if (FILESYS.exists("/config.ini")) {
    File iniFile = FILESYS.open("/config.ini", "r");
    if (iniFile) {
      String iniData;
      while (iniFile.available()) {
        char chnk = iniFile.read();
        iniData += chnk;
      }
      iniFile.close();

      if (instr(iniData, "AP_SSID=")) {
        AP_SSID = split(iniData, "AP_SSID=", "\r\n");
        AP_SSID.trim();
      }

      if (instr(iniData, "AP_PASS=")) {
        AP_PASS = split(iniData, "AP_PASS=", "\r\n");
        AP_PASS.trim();
      }

      if (instr(iniData, "WEBSERVER_IP=")) {
        String strwIp = split(iniData, "WEBSERVER_IP=", "\r\n");
        strwIp.trim();
        Server_IP.fromString(strwIp);
      }

      if (instr(iniData, "SUBNET_MASK=")) {
        String strsIp = split(iniData, "SUBNET_MASK=", "\r\n");
        strsIp.trim();
        Subnet_Mask.fromString(strsIp);
      }

      if (instr(iniData, "WIFI_SSID=")) {
        WIFI_SSID = split(iniData, "WIFI_SSID=", "\r\n");
        WIFI_SSID.trim();
      }

      if (instr(iniData, "WIFI_PASS=")) {
        WIFI_PASS = split(iniData, "WIFI_PASS=", "\r\n");
        WIFI_PASS.trim();
      }

      if (instr(iniData, "WIFI_HOST=")) {
        WIFI_HOSTNAME = split(iniData, "WIFI_HOST=", "\r\n");
        WIFI_HOSTNAME.trim();
      }

      if (instr(iniData, "USEAP=")) {
        String strua = split(iniData, "USEAP=", "\r\n");
        strua.trim();
        if (strua.equals("true")) {
          startAP = true;
        } else {
          startAP = false;
        }
      }

      if (instr(iniData, "CONWIFI=")) {
        String strcw = split(iniData, "CONWIFI=", "\r\n");
        strcw.trim();
        if (strcw.equals("true")) {
          connectWifi = true;
        } else {
          connectWifi = false;
        }
      }
    }
  } else {
    writeConfig();
  }
#endif

  if (startAP) {
    //HWSerial.println("SSID: " + AP_SSID);
    //HWSerial.println("Password: " + AP_PASS);
    //HWSerial.println("");
    //HWSerial.println("WEB Server IP: " + Server_IP.toString());
    //HWSerial.println("Subnet: " + Subnet_Mask.toString());
    //HWSerial.println("WEB Server Port: " + String(WEB_PORT));
    //HWSerial.println("");
    WiFi.softAPConfig(Server_IP, Server_IP, Subnet_Mask);
    WiFi.softAP(AP_SSID.c_str(), AP_PASS.c_str());
    //HWSerial.println("WIFI AP started");
    dnsServer.setTTL(30);
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
    dnsServer.start(53, "*", Server_IP);
    //HWSerial.println("DNS server started");
    //HWSerial.println("DNS Server IP: " + Server_IP.toString());
  }

  if (connectWifi && WIFI_SSID.length() > 0 && WIFI_PASS.length() > 0) {
    //WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.hostname(WIFI_HOSTNAME);
    WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());
    Serial.print("WIFI connecting");
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(500);
    }
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println(" Wifi failed to connect");
    } else {
      IPAddress LAN_IP = WiFi.localIP();
      if (LAN_IP) {
        Serial.println(" Wifi Connected");
        Serial.println("WEB Server LAN IP: " + LAN_IP.toString());
        Serial.println("WEB Server Port: " + String(WEB_PORT));
        Serial.println("WEB Server Hostname: " + WIFI_HOSTNAME);
        String mdnsHost = WIFI_HOSTNAME;
        mdnsHost.replace(".local", "");
        MDNS.begin(mdnsHost.c_str());
        if (!startAP) {
          dnsServer.setTTL(30);
          dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
          dnsServer.start(53, "*", LAN_IP);
          Serial.println("DNS server started");
          Serial.println("DNS Server IP: " + LAN_IP.toString());
        }
      }
    }
  }
  
  // Initialize Wi-Fi
  //initWiFi();
  // Initialize time with timezone
  initTime(myTimezone);    
  // Initialize the camera  
  Serial.print("Initializing the camera module...");
  configInitCamera();
  Serial.println("Ok!");
  // Initialize MicroSD
  Serial.print("Initializing the MicroSD card module... ");
  initMicroSDCard();

  // web server reacts
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Microsoft Connect Test");
  });

#if USECONFIG
  server.on("/config.ini", HTTP_ANY, [](AsyncWebServerRequest *request) {
    request->send(404);
  });
#endif

  server.on("/upload.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", upload_gz, sizeof(upload_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on(
    "/upload.html", HTTP_POST, [](AsyncWebServerRequest *request) {
      request->redirect("/fileman.html");
    },
    handleFileUpload);

  server.on("/fileman.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleFileMan(request);
  });

  server.on("/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleDelete(request);
  });

#if USECONFIG
  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleConfigHtml(request);
  });

  server.on("/config.html", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleConfig(request);
  });
#endif

  server.on("/admin.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", admin_gz, sizeof(admin_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on("/reboot.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", reboot_gz, sizeof(reboot_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on("/reboot.html", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleReboot(request);
  });

  server.on("/update.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", update_gz, sizeof(update_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on(
    "/update.html", HTTP_POST, [](AsyncWebServerRequest *request) {
    },
    handleFwUpdate);

  server.on("/info.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleInfo(request);
  });

  server.on("/dlall", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleDlFiles(request);
  });

  server.serveStatic("/", FILESYS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) {
    //Serial.println(request->url());
    String path = request->url();
    if (path.endsWith("index.html") || path.endsWith("index.htm") || path.endsWith("/")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_gz, sizeof(index_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }
    if (path.endsWith("style.css")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", style_gz, sizeof(style_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }
    if (path.endsWith("menu.html")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", menu_gz, sizeof(menu_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }
    request->send(404);
  });
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.begin();
  Serial.println("HTTP server started");

  bootTime = millis();
  startMillis = bootTime;
}


void loop() {    
  if (isFormating) {
  Serial.print("Formatting Storage");
  isFormating = false;
  FILESYS.end();
  //FILESYS.format();
  FILESYS.begin();
  delay(1000);
#if USECONFIG
  writeConfig();
#endif
  }

  // take the first pic after boot
  if (!firstPic) {
    takeSavePhoto();
    firstPic = true;
  }

  // Take and Save Photo every <period> min
  currentMillis = millis();
  if (currentMillis - startMillis >= period) {
    takeSavePhoto();
    startMillis = currentMillis;
  }
  // delay 1min
  //delay(10000);

  dnsServer.processNextRequest();

}