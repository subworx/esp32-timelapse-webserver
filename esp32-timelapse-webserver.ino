/*

ESP32 CAM control - timelapse with simple web interface
crapped together from:
 - https://www.instructables.com/ESP32-CAM-Take-Photo-and-Save-to-MicroSD-Card-With/
 - https://github.com/stooged/ESP32-Server-900u/

TODOs:
- fix save/reset cam config - form variables?
- cam config preview
- webcam stream?

DONE:
+ copy jailbreak esp stuff - file management, info
+ take picture every 3 minutes
+ fixed config read/write for server + cam
+ separated camera/system config
+ update camera config without reboot

NOTES:
To edit the html stuff (pages.h), copy the numbers and paste into https://gchq.github.io/CyberChef/ using
From Decimal (Comma) -> Gunzip to create HTML, then Gzip (with filename) -> To Decimal (comma), then paste
back into pages.h

*/

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
String firmwareVer = "1.30";

// REPLACE WITH YOUR TIMEZONE STRING: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
String myTimezone ="CET-1CEST,M3.5.0,M10.5.0/3";

int brightness = 0;     // -2 to 2
int contrast = 0;       // -2 to 2
int saturation = 0;     // -2 to 2
int sharpness = 0;      // -2 to 2
int denoise = 1;        // 0 = disable, 1 = enable
int special_effect = 0; // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
int wb_mode = 0;        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
int whitebal = 1;       // 0 = disable , 1 = enable
int awb_gain = 1;       // 0 = disable , 1 = enable
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
int lenc = 1;           // 0 = disable , 1 = enable
int hmirror = 0;        // 0 = disable , 1 = enable
int vflip = 0;          // 0 = disable , 1 = enable
int dcw = 1;            // 0 = disable , 1 = enable
int colorbar = 0;       // 0 = disable , 1 = enable

//const unsigned long period = 1000; // period in milliseconds - 1sec
unsigned long period = 180000; // period in milliseconds - 3min

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
      String tmphtm = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\"><style type=\"text/css\">";
      tmphtm += "body {background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}";
      tmphtm += "</style></head><center><br><br><br><br><br><br>Update Success, Rebooting.</center></html>";
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
  if (SD_MMC.exists("/" + path) && path != "/" && !path.equals("config.ini") && !path.equals("camconfig.ini")) {
    SD_MMC.remove("/" + path);
  }
  request->redirect("/fileman.html");
}


void handleFileMan(AsyncWebServerRequest *request) {
  File dir = SD_MMC.open("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>File Manager</title>";
  output += "<link rel=\"stylesheet\" href=\"style.css\"><style>body{overflow-y:auto;} th{border: 1px solid #dddddd; background-color:gray;padding: 8px;}";
  output += "</style><script>function statusDel(fname) {var answer = confirm(\"Are you sure you want to delete \" + fname + \" ?\");";
  output += "if (answer) {return true;} else { return false; }} </script></head><body><br><table id=filetable></table><script>var filelist = [";
  int fileCount = 0;
  while (dir) {
    File file = dir.openNextFile();
    if (!file) {
      dir.close();
      break;
    }
    String fname = String(file.name());
    if (fname.length() > 0 && !fname.equals("config.ini") && !fname.equals("camconfig.ini") && !file.isDirectory()) {
      fileCount++;
      fname.replace("|", "%7C");
      fname.replace("\"", "%22");
      output += "\"" + fname + "|" + formatBytes(file.size()) + "\",";
    }
    file.close();
    esp_task_wdt_reset();
  }
  if (fileCount == 0) {
    output += "];</script><center>No files found<br>You can upload files using the <a href=\"/upload.html\" target=\"mframe\">";
    output += "<u>File Uploader</u></a> page.</center></p></body></html>";
  } else {
    output += "];var output = \"\";filelist.forEach(function(entry) {var splF = entry.split(\"|\"); output += \"<tr>\";output += \"<td>";
    output += "<a href=\\\"\" +  splF[0] + \"\\\">\" + splF[0] + \"</a></td>\"; output += \"<td>\" + splF[1] + \"</td>\";output += \"<td>";
    output += "<a href=\\\"/\" + splF[0] + \"\\\" download><button type=\\\"submit\\\">Download</button></a></td>\";output += \"<td>";
    output += "<form action=\\\"/delete\\\" method=\\\"post\\\"><button type=\\\"submit\\\" name=\\\"file\\\" value=\\\"\" + splF[0] + \"\\\" ";
    output += "onClick=\\\"return statusDel('\" + splF[0] + \"');\\\">Delete</button></form></td>\";output += \"</tr>\";}); ";
    output += "document.getElementById(\"filetable\").innerHTML = \"<tr><th colspan='1'><center>File Name</center></th><th colspan='1'>";
    output += "<center>File Size</center></th><th colspan='1'><center><a href='/dlall' target='mframe'><button type='submit'>Download All</button></a>";
    output += "</center></th><th colspan='1'><center><a href='/format.html' target='mframe'><button type='submit'>Delete All</button></a></center></th>";
    output += "</tr>\" + output;</script></body></html>";
  }
  request->send(200, "text/html", output);
}


void handleDlFiles(AsyncWebServerRequest *request) {
  File dir = SD_MMC.open("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>File Downloader</title>";
  output += "<link rel=\"stylesheet\" href=\"style.css\"><style>body{overflow-y:auto;}</style><script type=\"text/javascript\" src=\"jzip.js\"></script>";
  output += "<script>var filelist = [";
  int fileCount = 0;
  while (dir) {
    File file = dir.openNextFile();
    if (!file) {
      dir.close();
      break;
    }
    String fname = String(file.name());
    if (fname.length() > 0 && !fname.equals("config.ini") && !fname.equals("camconfig.ini") && !file.isDirectory()) {
      fileCount++;
      fname.replace("\"", "%22");
      output += "\"" + fname + "\",";
    }
    file.close();
    esp_task_wdt_reset();
  }
  if (fileCount == 0) {
    output += "];</script></head><center>No files found to download<br>You can upload files using the <a href=\"/upload.html\" target=\"mframe\">";
    output += "<u>File Uploader</u></a> page.</center></p></body></html>";
  } else {
    output += "]; async function dlAll(){var zip = new JSZip();for (var i = 0; i < filelist.length; i++) {if (filelist[i] != '')";
    output += "{var xhr = new XMLHttpRequest();xhr.open('GET',filelist[i],false);xhr.overrideMimeType('text/plain; charset=x-user-defined'); ";
    output += "xhr.onload = function(e) {if (this.status == 200) {zip.file(filelist[i], this.response,{binary: true});}};";
    output += "xhr.send();document.getElementById('fp').innerHTML = 'Adding: ' + filelist[i];await new Promise(r => setTimeout(r, 50));}}";
    output += "document.getElementById('gen').style.display = 'none';document.getElementById('comp').style.display = 'block';";
    output += "zip.generateAsync({type:'blob'}).then(function(content) {saveAs(content,'esp_files.zip');});}</script></head>";
    output += "<body onload='setTimeout(dlAll,100);'><center><br><br><br><br><div id='gen' style='display:block;'><div id='loader'></div><br><br>";
    output += "Generating ZIP<br><p id='fp'></p></div><div id='comp' style='display:none;'><br><br><br><br>Complete<br><br>Downloading: ";
    output += "esp_files.zip</div></center></body></html>";
  }
  request->send(200, "text/html", output);
}


void handleConfig(AsyncWebServerRequest *request) {
  if (request->hasParam("ap_ssid", true) && request->hasParam("ap_pass", true) && request->hasParam("web_ip", true) && 
  request->hasParam("web_port", true) && request->hasParam("subnet", true) && request->hasParam("wifi_ssid", true) && 
  request->hasParam("wifi_pass", true) && request->hasParam("wifi_host", true)) {
    //Serial.println("-- processing config --");
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
    String iniContent = "\r\nAP_SSID=" + AP_SSID;
    iniContent += "\r\nAP_PASS=" + AP_PASS;
    iniContent += "\r\nWEBSERVER_IP=" + tmpip;
    iniContent += "\r\nWEBSERVER_PORT=" + tmpwport;
    iniContent += "\r\nSUBNET_MASK=" + tmpsubn;
    iniContent += "\r\nWIFI_SSID=" + WIFI_SSID;
    iniContent += "\r\nWIFI_PASS=" + WIFI_PASS;
    iniContent += "\r\nWIFI_HOST=" + WIFI_HOSTNAME;
    iniContent += "\r\nUSEAP=" + tmpua;
    iniContent += "\r\nCONWIFI=" + tmpcw;
    iniContent += "\r\n";
    //Serial.println(iniContent);
    File iniFile = SD_MMC.open("/config.ini", "w");
    if (iniFile) {
      //Serial.println("writing ini");
      iniFile.print(iniContent);
      iniFile.close();
    }
    String htmStr = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\">";
    htmStr += "<style type=\"text/css\">#loader {z-index: 1;width: 50px;height: 50px;margin: 0 0 0 0;border: 6px solid #f3f3f3;";
    htmStr += "border-radius: 50%;border-top: 6px solid #3498db;width: 50px;height: 50px;-webkit-animation: spin 2s linear infinite;";
    htmStr += "animation: spin 2s linear infinite; } @-webkit-keyframes spin {0%{-webkit-transform: rotate(0deg);}";
    htmStr += "100%{-webkit-transform: rotate(360deg);}}@keyframes spin{0%{ transform: rotate(0deg);}100%{transform: rotate(360deg);}}";
    htmStr += "body {background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}";
    htmStr += " #msgfmt {font-size: 16px; font-weight: normal;}#status {font-size: 16px; font-weight: normal;}</style></head><center><br><br><br><br><br>";
    htmStr += "<p id=\"status\"><div id='loader'></div><br>Config saved<br>Rebooting</p></center></html>";
    request->send(200, "text/html", htmStr);
    delay(1000);
    ESP.restart();
  } else {
    //Serial.println(" broken/missing/whatever parameters for config");
    request->redirect("/config.html");
  }
}

void handleCamConfig(AsyncWebServerRequest *request) {
  if (request->hasParam("mytimezone", true) && request->hasParam("brightness", true) && request->hasParam("contrast", true) && 
  request->hasParam("saturation", true) && request->hasParam("sharpness", true) && request->hasParam("denoise", true) && 
  request->hasParam("special_effect", true) && request->hasParam("whitebal", true) && request->hasParam("awb_gain", true) &&
  request->hasParam("wb_mode", true) && request->hasParam("exposure_ctrl", true) && request->hasParam("aec2", true) &&
  request->hasParam("ae_level", true) && request->hasParam("aec_value", true) && request->hasParam("gain_ctrl", true) &&
  request->hasParam("agc_gain", true) && request->hasParam("gainceiling", true) && request->hasParam("bpc", true) &&
  request->hasParam("wpc", true) && request->hasParam("raw_gma", true) && request->hasParam("lenc", true) &&
  request->hasParam("hmirror", true) && request->hasParam("vflip", true) && request->hasParam("dcw", true) &&
  request->hasParam("colorbar", true) && request->hasParam("period", true)) {
    //Serial.println("-- processing config --");
    String tmpMytimezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    if (request->hasParam("mytimezone", true)) { tmpMytimezone = request->getParam("mytimezone", true)->value(); myTimezone = tmpMytimezone;}
    String tmpBrightness = "0";
    if (request->hasParam("brightness", true)) { tmpBrightness = request->getParam("brightness", true)->value(); brightness = tmpBrightness.toInt();}
    String tmpContrast = "0";
    if (request->hasParam("contrast", true)) { tmpContrast = request->getParam("contrast", true)->value(); contrast = tmpContrast.toInt();}
    String tmpSaturation = "0";
    if (request->hasParam("saturation", true)) { tmpSaturation = request->getParam("saturation", true)->value(); saturation = tmpSaturation.toInt();}
    String tmpSharpness = "0";
    if (request->hasParam("sharpness", true)) { tmpSharpness = request->getParam("sharpness", true)->value(); sharpness = tmpSharpness.toInt();}
    String tmpDenoise = "1";
    if (request->hasParam("denoise", true)) { tmpDenoise = request->getParam("denoise", true)->value(); denoise = tmpDenoise.toInt();}
    String tmpSpecial_effect = "0";
    if (request->hasParam("special_effect", true)) { tmpSpecial_effect = request->getParam("special_effect", true)->value(); special_effect = tmpSpecial_effect.toInt();}
    String tmpWhitebal = "1";
    if (request->hasParam("whitebal", true)) { tmpWhitebal = request->getParam("whitebal", true)->value(); whitebal = tmpWhitebal.toInt();}
    String tmpAwb_gain = "1";
    if (request->hasParam("awb_gain", true)) { tmpAwb_gain = request->getParam("awb_gain", true)->value(); awb_gain = tmpAwb_gain.toInt();}
    String tmpWb_mode = "0";
    if (request->hasParam("wb_mode", true)) { tmpWb_mode = request->getParam("wb_mode", true)->value(); wb_mode = tmpWb_mode.toInt();}
    String tmpExposure_ctrl = "1";
    if (request->hasParam("exposure_ctrl", true)) { tmpExposure_ctrl = request->getParam("exposure_ctrl", true)->value(); exposure_ctrl = tmpExposure_ctrl.toInt();}
    String tmpAec2 = "0";
    if (request->hasParam("aec2", true)) { tmpAec2 = request->getParam("aec2", true)->value(); aec2 = tmpAec2.toInt();}
     String tmpAe_level = "0";
    if (request->hasParam("ae_level", true)) { tmpAe_level = request->getParam("ae_level", true)->value(); ae_level = tmpAe_level.toInt();}
    String tmpAec_value = "300";
    if (request->hasParam("aec_value", true)) { tmpAec_value = request->getParam("aec_value", true)->value(); aec_value = tmpAec_value.toInt();}
    String tmpGain_ctrl = "1";
    if (request->hasParam("gain_ctrl", true)) { tmpGain_ctrl = request->getParam("gain_ctrl", true)->value(); gain_ctrl = tmpGain_ctrl.toInt();}
    String tmpAgc_gain = "0";
    if (request->hasParam("agc_gain", true)) { tmpAgc_gain = request->getParam("agc_gain", true)->value(); agc_gain = tmpAgc_gain.toInt();}
    String tmpGainceiling = "0";
    if (request->hasParam("gainceiling", true)) { tmpGainceiling = request->getParam("gainceiling", true)->value(); gainceiling = tmpGainceiling.toInt();}
    String tmpBpc = "0";
    if (request->hasParam("bpc", true)) { tmpBpc = request->getParam("bpc", true)->value(); bpc = tmpBpc.toInt();}
    String tmpWpc = "1";
    if (request->hasParam("wpc", true)) { tmpWpc = request->getParam("wpc", true)->value(); wpc = tmpWpc.toInt();}
    String tmpRaw_gma = "1";
    if (request->hasParam("raw_gma", true)) { tmpRaw_gma = request->getParam("raw_gma", true)->value(); raw_gma = tmpRaw_gma.toInt();}
    String tmpLenc = "1";
    if (request->hasParam("lenc", true)) { tmpLenc = request->getParam("lenc", true)->value(); lenc = tmpLenc.toInt();}
    String tmpHmirror = "0";
    if (request->hasParam("hmirror", true)) { tmpHmirror = request->getParam("hmirror", true)->value(); hmirror = tmpHmirror.toInt();}
    String tmpVflip = "0";
    if (request->hasParam("vflip", true)) { tmpVflip = request->getParam("vflip", true)->value(); vflip = tmpVflip.toInt();}
    String tmpDcw = "1";
    if (request->hasParam("dcw", true)) { tmpDcw = request->getParam("dcw", true)->value(); dcw = tmpDcw.toInt();}
    String tmpColorbar = "0";
    if (request->hasParam("colorbar", true)) { tmpColorbar = request->getParam("colorbar", true)->value(); colorbar = tmpColorbar.toInt();}
    String tmpPeriod = "180000";
    if (request->hasParam("period", true)) { tmpPeriod = request->getParam("period", true)->value(); period = tmpPeriod.toInt();}
    String iniContent = "MYTIMEZONE=" + tmpMytimezone;
    iniContent += "\r\nBRIGHTNESS=" + tmpBrightness;
    iniContent += "\r\nCONTRAST=" + tmpContrast;
    iniContent += "\r\nSATURATION=" + tmpSaturation;
    iniContent += "\r\nSHARPNESS=" + tmpSharpness;
    iniContent += "\r\nDENOISE=" + tmpDenoise;
    iniContent += "\r\nSPECIAL_EFFECT=" + tmpSpecial_effect;
    iniContent += "\r\nWHITEBAL=" + tmpWhitebal;
    iniContent += "\r\nAWB_GAIN=" + tmpAwb_gain;
    iniContent += "\r\nWB_MODE=" + tmpWb_mode;
    iniContent += "\r\nEXPOSURE_CTRL=" + tmpExposure_ctrl;
    iniContent += "\r\nAEC2=" + tmpAec2;
    iniContent += "\r\nAE_LEVEL=" + tmpAe_level;
    iniContent += "\r\nAEC_VALUE=" + tmpAec_value;
    iniContent += "\r\nGAIN_CTRL=" + tmpGain_ctrl;
    iniContent += "\r\nAGC_GAIN=" + tmpAgc_gain;
    iniContent += "\r\nGAINCEILING=" + tmpGainceiling;
    iniContent += "\r\nBPC=" + tmpBpc;
    iniContent += "\r\nWPC=" + tmpWpc;
    iniContent += "\r\nRAW_GMA=" + tmpRaw_gma;
    iniContent += "\r\nLENC=" + tmpLenc;
    iniContent += "\r\nHMIRROR=" + tmpHmirror;
    iniContent += "\r\nVFLIP=" + tmpVflip;
    iniContent += "\r\nDCW=" + tmpDcw;
    iniContent += "\r\nCOLORBAR=" + tmpColorbar;
    iniContent += "\r\nPERIOD=" + tmpPeriod;
    iniContent += "\r\n";
    //Serial.println(iniContent);
    File iniFile = SD_MMC.open("/camconfig.ini", "w");
    if (iniFile) {
      iniFile.print(iniContent);
      iniFile.close();
    }
    //String htmStr = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\">";
    String htmStr = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/camconfig.html\">";
    htmStr += "<style type=\"text/css\">#loader {z-index: 1;width: 50px;height: 50px;margin: 0 0 0 0;border: 6px solid #f3f3f3;";
    htmStr += "border-radius: 50%;border-top: 6px solid #3498db;width: 50px;height: 50px;-webkit-animation: spin 2s linear infinite;";
    htmStr += "animation: spin 2s linear infinite; } @-webkit-keyframes spin {0%{-webkit-transform: rotate(0deg);}";
    htmStr += "100%{-webkit-transform: rotate(360deg);}}@keyframes spin{0%{ transform: rotate(0deg);}100%{transform: rotate(360deg);}}";
    htmStr += "body {background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}";
    htmStr += " #msgfmt {font-size: 16px; font-weight: normal;}#status {font-size: 16px; font-weight: normal;}</style></head><center><br><br><br><br><br>";
    htmStr += "<p id=\"status\"><div id='loader'></div><br>Cam config saved<br>Reloading</p></center></html>";
    request->send(200, "text/html", htmStr);
    delay(1000);
    //ESP.restart();
    Serial.println("--- setting new camera settings ---");
    sensor_t * s = esp_camera_sensor_get();
    //Serial.println("set brightness: " + String(brightness));
    s->set_brightness(s, brightness);     // -2 to 2
    //Serial.println("set contrast: " + String(contrast));
    s->set_contrast(s, contrast);       // -2 to 2
    //Serial.println("set saturation: " + String(saturation));
    s->set_saturation(s, saturation);     // -2 to 2
    //Serial.println("set sharpness: " + String(sharpness));
    s->set_sharpness(s, sharpness);      // -2 to 2
    //Serial.println("set denoise: " + String(denoise));
    s->set_denoise(s, denoise);        // 0 = disable, 1 = enable
    //Serial.println("set special_effect: " + String(special_effect));
    s->set_special_effect(s, special_effect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
    //Serial.println("set whitebal: " + String(whitebal));
    s->set_whitebal(s, whitebal);       // 0 = disable , 1 = enable
    //Serial.println("set awb_gain: " + String(awb_gain));
    s->set_awb_gain(s, awb_gain);       // 0 = disable , 1 = enable
    //Serial.println("set wb_mode: " + String(wb_mode));
    s->set_wb_mode(s, wb_mode);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
    //Serial.println("set exposure_ctrl: " + String(exposure_ctrl));
    s->set_exposure_ctrl(s, exposure_ctrl);  // 0 = disable , 1 = enable
    //Serial.println("set aec2: " + String(aec2));
    s->set_aec2(s, aec2);           // 0 = disable , 1 = enable
    //Serial.println("set aec_value: " + String(aec_value));
    s->set_aec_value(s, aec_value);    // 0 to 1200
    //Serial.println("set gain_ctrl: " + String(gain_ctrl));
    s->set_gain_ctrl(s, gain_ctrl);      // 0 = disable , 1 = enable
    //Serial.println("set agc_gain: " + String(agc_gain));
    s->set_agc_gain(s, agc_gain);       // 0 to 30
    //Serial.println("set gainceiling: " + String(gainceiling));
    s->set_gainceiling(s, gainceiling_t(gainceiling));  // 0 to 6
    //Serial.println("set bpc: " + String(bpc));
    s->set_bpc(s, bpc);            // 0 = disable , 1 = enable
    //Serial.println("set wpc: " + String(wpc));
    s->set_wpc(s, wpc);            // 0 = disable , 1 = enable
    //Serial.println("set raw_gma: " + String(raw_gma));
    s->set_raw_gma(s, raw_gma);        // 0 = disable , 1 = enable
    //Serial.println("set lenc: " + String(lenc));
    s->set_lenc(s, lenc);           // 0 = disable , 1 = enable
    //Serial.println("set hmirror: " + String(hmirror));
    s->set_hmirror(s, hmirror);        // 0 = disable , 1 = enable
    //Serial.println("set vflip: " + String(vflip));
    s->set_vflip(s, vflip);          // 0 = disable , 1 = enable
    //Serial.println("set dcw: " + String(dcw));
    s->set_dcw(s, dcw);            // 0 = disable , 1 = enable
    //Serial.println("set colorbar: " + String(colorbar));
    s->set_colorbar(s, colorbar);       // 0 = disable , 1 = enable
  } else {
    //Serial.println(" broken/missing/whatever parameters for config");
    request->redirect("/camconfig.html");
  }
}


void handleReboot(AsyncWebServerRequest *request) {
  Serial.print("Rebooting ESP");
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", rebooting_gz, sizeof(rebooting_gz));
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
  delay(1000);
  ESP.restart();
}


void handleConfigHtml(AsyncWebServerRequest *request) {
  String tmpUa = "";
  String tmpCw = "";
  if (startAP) { tmpUa = " checked"; }
  if (connectWifi) { tmpCw = " checked"; }
  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Config Editor</title>";
  htmStr += "<style type=\"text/css\">body {background-color: #1451AE; color: #ffffff; font-size: 14px;font-weight: bold;margin: 0 0 0 0.0;";
  htmStr += "padding: 0.4em 0.4em 0.4em 0.6em;}input[type=\"submit\"]:hover ";
  htmStr += "{background: #ffffff;color: green;}input[type=\"submit\"]:active{outline-color: green;color: green;background: #ffffff; }";
  htmStr += "table {font-family: arial, sans-serif;border-collapse: collapse;}td {border: 1px solid #dddddd;text-align: left;padding: 8px;}";
  htmStr += "th {border: 1px solid #dddddd; background-color:gray;text-align: center;padding: 8px;}";
  htmStr += "a:link {color: #ffffff;}a:visited {color: #ffffff;}a:hover {color: #ffffff;}a:active {color: #ffffff;}";
  htmStr += "</style></head><body>";
  htmStr += "<form action=\"/config.html\" method=\"post\"><center><table>";
  htmStr += "<tr><th colspan=\"2\"><center>Access Point</center></th></tr>";
  htmStr += "<tr><td>AP SSID:</td><td><input name=\"ap_ssid\" value=\"" + AP_SSID + "\"></td></tr>";
  htmStr += "<tr><td>AP Password:</td><td><input name=\"ap_pass\" value=\"********\"></td></tr>";
  htmStr += "<tr><td>AP IP:</td><td><input name=\"web_ip\" value=\"" + Server_IP.toString() + "\"></td></tr>";
  htmStr += "<tr><td>Subnet Mask:</td><td><input name=\"subnet\" value=\"" + Subnet_Mask.toString() + "\"></td></tr>";
  htmStr += "<tr><td>Start AP:</td><td><input type=\"checkbox\" name=\"useap\" " + tmpUa + "></td></tr>";
  htmStr += "<tr><th colspan=\"2\"><center>Web Server</center></th></tr>";
  htmStr += "<tr><td>Webserver Port:</td><td><input name=\"web_port\" value=\"" + String(WEB_PORT) + "\"></td></tr>";
  htmStr += "<tr><th colspan=\"2\"><center>Wifi Connection</center></th></tr>";
  htmStr += "<tr><td>WiFi SSID:</td><td><input name=\"wifi_ssid\" value=\"" + WIFI_SSID + "\"></td></tr>";
  htmStr += "<tr><td>WiFi Password:</td><td><input name=\"wifi_pass\" value=\"********\"></td></tr>";
  htmStr += "<tr><td>WiFi Hostname:</td><td><input name=\"wifi_host\" value=\"" + WIFI_HOSTNAME + "\"></td></tr>";
  htmStr += "<tr><td>Connect WiFi:</td><td><input type=\"checkbox\" name=\"usewifi\" " + tmpCw + "></td></tr>";
  htmStr += "</table><br><input id=\"savecfg\" type=\"submit\" value=\"Save Config\"></center></form></body></html>";
  request->send(200, "text/html", htmStr);
}

void handleCamConfigHtml(AsyncWebServerRequest *request) {
  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Config Editor</title>";
  htmStr += "<style type=\"text/css\">body {background-color: #1451AE; color: #ffffff; font-size: 14px;font-weight: bold;margin: 0 0 0 0.0;";
  htmStr += "padding: 0.4em 0.4em 0.4em 0.6em;}input[type=\"submit\"]:hover ";
  htmStr += "{background: #ffffff;color: green;}input[type=\"submit\"]:active{outline-color: green;color: green;background: #ffffff; }";
  htmStr += "table {font-family: arial, sans-serif;border-collapse: collapse;}td {border: 1px solid #dddddd;text-align: left;padding: 8px;}";
  htmStr += "th {border: 1px solid #dddddd; background-color:gray;text-align: center;padding: 8px;}";
  htmStr += "a:link {color: #ffffff;}a:visited {color: #ffffff;}a:hover {color: #ffffff;}a:active {color: #ffffff;}";
  htmStr += "</style></head><body>";
  htmStr += "<form action=\"/camconfig.html\" method=\"post\"><center><table>";
  htmStr += "<tr><th colspan=\"2\"><center>Time Zone</th></tr>";
  htmStr += "<tr><td><a href=\"https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv\" target=\"_blank\">";
  htmStr += "My Timezone</a>:</td><td><input name=\"mytimezone\" value=\"" + String(myTimezone) + "\"></td></tr>";
  htmStr += "<tr><th colspan=\"2\"><center>Camera</th></tr>";
  htmStr += "<tr><td>Period (ms):</td><td><input name=\"period\" value=\"" + String(period) + "\"></td></tr>";
  htmStr += "<tr><td>Brightness (-2 to 2, Default 0):</td><td><input name=\"brightness\" value=\"" + String(brightness) + "\"></td></tr>";
  htmStr += "<tr><td>Contrast (-2 to 2, Default 0):</td><td><input name=\"contrast\" value=\"" + String(contrast) + "\"></td></tr>";
  htmStr += "<tr><td>Saturation (-2 to 2, Default 0):</td><td><input name=\"saturation\" value=\"" + String(saturation) + "\"></td></tr>";
  htmStr += "<tr><td>Sharpness (-2 to 2, Default 0):</td><td><input name=\"sharpness\" value=\"" + String(sharpness) + "\"></td></tr>";
  htmStr += "<tr><td>Denoise (0 / 1, Default 1):</td><td><input name=\"denoise\" value=\"" + String(denoise) + "\"></td></tr>";
  htmStr += "<tr><td>Special Effect:</td>";
  htmStr += "<td><select name=\"special_effect\">";
  htmStr += "<option value=\"0\""; if (special_effect == 0) {htmStr += " selected";} htmStr += ">0 - No effect</option>";
  htmStr += "<option value=\"1\""; if (special_effect == 1) {htmStr += " selected";} htmStr += ">1 - Negative</option>";
  htmStr += "<option value=\"2\""; if (special_effect == 2) {htmStr += " selected";} htmStr += ">2 - Grayscale</option>";
  htmStr += "<option value=\"3\""; if (special_effect == 3) {htmStr += " selected";} htmStr += ">3 - Red Tint</option>";
  htmStr += "<option value=\"4\""; if (special_effect == 4) {htmStr += " selected";} htmStr += ">4 - Green Tint</option>";
  htmStr += "<option value=\"5\""; if (special_effect == 5) {htmStr += " selected";} htmStr += ">5 - Blue tint</option>";
  htmStr += "<option value=\"6\""; if (special_effect == 6) {htmStr += " selected";} htmStr += ">6 - Sepia</option>";
  htmStr += "</select></td></tr>";
  htmStr += "<tr><td>White Balance (0 / 1, Default 1):</td><td><input name=\"whitebal\" value=\"" + String(whitebal) + "\"></td></tr>";
  htmStr += "<tr><td>awb_gain (0 / 1, Default 1):</td><td><input name=\"awb_gain\" value=\"" + String(awb_gain) + "\"></td></tr>";
  htmStr += "<tr><td>wb_mode (0-4, if awb_gain enabled, Default 0):</td>";
  htmStr += "<td><select name=\"wb_mode\">";
  htmStr += "<option value=\"0\""; if (wb_mode == 0) {htmStr += " selected";} htmStr += ">0 - Auto</option>";
  htmStr += "<option value=\"1\""; if (wb_mode == 1) {htmStr += " selected";} htmStr += ">1 - Sunny</option>";
  htmStr += "<option value=\"2\""; if (wb_mode == 2) {htmStr += " selected";} htmStr += ">2 - Cloudy</option>";
  htmStr += "<option value=\"3\""; if (wb_mode == 3) {htmStr += " selected";} htmStr += ">3 - Office</option>";
  htmStr += "<option value=\"4\""; if (wb_mode == 4) {htmStr += " selected";} htmStr += ">4 - Home</option>";
  htmStr += "</select></td></tr>";
  htmStr += "<tr><td>exposure_ctrl (0 / 1, Default 1):</td><td><input name=\"exposure_ctrl\" value=\"" + String(exposure_ctrl) + "\"></td></tr>";
  htmStr += "<tr><td>aec2 (0 / 1, Default 0):</td><td><input name=\"aec2\" value=\"" + String(aec2) + "\"></td></tr>";
  htmStr += "<tr><td>ae_level (-2 to 2, Default 0):</td><td><input name=\"ae_level\" value=\"" + String(ae_level) + "\"></td></tr>";
  htmStr += "<tr><td>aec_value (0-1200, Default 300):</td><td><input name=\"aec_value\" value=\"" + String(aec_value) + "\"></td></tr>";
  htmStr += "<tr><td>gain_ctrl (0 / 1, Default 1):</td><td><input name=\"gain_ctrl\" value=\"" + String(gain_ctrl) + "\"></td></tr>";
  htmStr += "<tr><td>agc_gain (0-30, Default 0):</td><td><input name=\"agc_gain\" value=\"" + String(agc_gain) + "\"></td></tr>";
  htmStr += "<tr><td>gainceiling (0-6, Default 0):</td><td><input name=\"gainceiling\" value=\"" + String(gainceiling) + "\"></td></tr>";
  htmStr += "<tr><td>bpc (0 / 1, Default 0):</td><td><input name=\"bpc\" value=\"" + String(bpc) + "\"></td></tr>";
  htmStr += "<tr><td>wpc (0 / 1, Default 1):</td><td><input name=\"wpc\" value=\"" + String(wpc) + "\"></td></tr>";
  htmStr += "<tr><td>raw_gma (0 / 1, Default 1):</td><td><input name=\"raw_gma\" value=\"" + String(raw_gma) + "\"></td></tr>";
  htmStr += "<tr><td>lenc (0 / 1, Default 1):</td><td><input name=\"lenc\" value=\"" + String(lenc) + "\"></td></tr>";
  htmStr += "<tr><td>hmirror (0 / 1, Default 0):</td><td><input name=\"hmirror\" value=\"" + String(hmirror) + "\"></td></tr>";
  htmStr += "<tr><td>vflip (0 / 1, Default 0):</td><td><input name=\"vflip\" value=\"" + String(vflip) + "\"></td></tr>";
  htmStr += "<tr><td>dcw (0 / 1, Default 1):</td><td><input name=\"dcw\" value=\"" + String(dcw) + "\"></td></tr>";
  htmStr += "<tr><td>colorbar (0 / 1, Default 0):</td><td><input name=\"colorbar\" value=\"" + String(colorbar) + "\"></td></tr>";
  htmStr += "</table><br><input name=\"savecfg\" id=\"savecfg\" type=\"submit\" value=\"Save Config\"></center></form></body></html>";
  request->send(200, "text/html", htmStr);
}


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
    if (filename.equals("/camconfig.ini")) { return; }
    //HWSerial.printf("Upload Start: %s\n", filename.c_str());
    upFile = SD_MMC.open(filename, "w");
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
  String output = "<!DOCTYPE html><html><head>";
  output += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  output += "<title>System Information</title><link rel=\"stylesheet\" href=\"style.css\"></head>";
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
  output += "Total Size: " + formatBytes(SD_MMC.totalBytes()) + "<br>";
  output += "Used Space: " + formatBytes(SD_MMC.usedBytes()) + "<br>";
  output += "Free Space: " + formatBytes(SD_MMC.totalBytes() - SD_MMC.usedBytes()) + "<br><hr>";
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
  output += "##### Network information #####<br><br>";
  if (startAP) {
    output += "SSID: " + AP_SSID + "<br>";
    output += "Server IP: " + Server_IP.toString() + "<br>";
    output += "Subnet: " + Subnet_Mask.toString() + "<br>";
    output += "Server Port: " + String(WEB_PORT) + "<br><hr>";
  }
  if (connectWifi) {
    output += "SSID: " + WIFI_SSID + "<br>";
    output += "Server LAN IP: " + WiFi.localIP().toString() + "<br>";
    output += "Server Port: " + String(WEB_PORT) + "<br>";
    output += "Server Hostname: " + WIFI_HOSTNAME + "<br><hr>";
  }
  output += "##### Camera Parameters #####<br><br>";
  output += "Picture interval (ms): " + String(period) + "<br>";
  output += "Brightness (-2 to 2): " + String(brightness) + "<br>";
  output += "Contrast (-2 to 2): " + String(contrast) + "<br>";
  output += "Saturation (-2 to 2): " + String(saturation) + "<br>";
  output += "Sharpness (-2 to 2): " + String(sharpness) + "<br>";
  output += "Denoise (0/1): " + String(denoise) + "<br>";
  output += "Special Effect (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Ting, 6 - Sepia): " + String(special_effect) + "<br>";
  output += "White balance (0/1): " + String(whitebal) + "<br>";
  output += "White Balance Gain (0/1): " + String(awb_gain) + "<br>";
  output += "White Balance Mode (0-4 - if White Balance Gain=1: 0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home): " + String(wb_mode) + "<br>";
  output += "Exposure Control (0/1): " + String(exposure_ctrl) + "<br>";
  output += "Automatic Exposure Control 2 (0/1): " + String(aec2) + "<br>";
  output += "Automatic Exposure Level (-2 to 2): " + String(ae_level) + "<br>";
  output += "Automatic Exposure Control Value (0-1200): " + String(aec_value) + "<br>";
  output += "Gain Control (0/1): " + String(gain_ctrl) + "<br>";
  output += "Gain Control Gain (0-30): " + String(agc_gain) + "<br>";
  output += "Gain Control Gain ceiling (0-6): " + String(gainceiling) + "<br>";
  output += "bpc (0/1): " + String(bpc) + "<br>";
  output += "wpc (0/1): " + String(wpc) + "<br>";
  output += "raw_gma (0/1): " + String(raw_gma) + "<br>";
  output += "lenc (0/1): " + String(lenc) + "<br>";
  output += "Horizontal mirror (0/1): " + String(hmirror) + "<br>";
  output += "Vertical flip (0/1): " + String(vflip) + "<br>";
  output += "dcw (0/1): " + String(dcw) + "<br>";
  output += "colorbar (0/1): " + String(colorbar) + "<br><hr>";
  output += "###### Sketch information ######<br><br>";
  output += "Sketch hash: " + ESP.getSketchMD5() + "<br>";
  output += "Sketch size: " + formatBytes(ESP.getSketchSize()) + "<br>";
  output += "Free space available: " + formatBytes(ESP.getFreeSketchSpace() - ESP.getSketchSize()) + "<br><hr>";
  output += "</html>";
  request->send(200, "text/html", output);
}

void writeConfig() {
  File iniFile = SD_MMC.open("/config.ini", "w");
  if (iniFile) {
    String tmpua = "false";
    String tmpcw = "false";
    String tmpslp = "false";
    if (startAP) { tmpua = "true"; }
    if (connectWifi) { tmpcw = "true"; }
    String iniContent = "\r\nAP_SSID=" + AP_SSID;
    iniContent += "\r\nAP_PASS=" + AP_PASS;
    iniContent += "\r\nWEBSERVER_IP=" + Server_IP.toString();
    iniContent += "\r\nWEBSERVER_PORT=" + String(WEB_PORT);
    iniContent += "\r\nSUBNET_MASK=" + Subnet_Mask.toString();
    iniContent += "\r\nWIFI_SSID=" + WIFI_SSID;
    iniContent += "\r\nWIFI_PASS=" + WIFI_PASS;
    iniContent += "\r\nWIFI_HOST=" + WIFI_HOSTNAME;
    iniContent += "\r\nUSEAP=" + tmpua;
    iniContent += "\r\nCONWIFI=" + tmpcw;
    iniContent += "\r\n";
    iniFile.print(iniContent);
    iniFile.close();
  }
}

void writeCamConfig() {
  File camIniFile = SD_MMC.open("/camconfig.ini", "w");
  if (camIniFile) {
    Serial.println("camconfig.ini opened for writeCamConfig");
    String camIniContent = "\r\nMYTIMEZONE=" + myTimezone;
    camIniContent += "\r\nBRIGHTNESS=" + String(brightness);
    camIniContent += "\r\nCONTRAST=" + String(contrast);
    camIniContent += "\r\nSATURATION=" + String(saturation);
    camIniContent += "\r\nSHARPNESS=" + String(sharpness);
    camIniContent += "\r\nDENOISE=" + String(denoise);
    camIniContent += "\r\nSPECIAL_EFFECT=" + String(special_effect);
    camIniContent += "\r\nWHITEBAL=" + String(whitebal);
    camIniContent += "\r\nAWB_GAIN=" + String(awb_gain);
    camIniContent += "\r\nWB_MODE=" + String(wb_mode);
    camIniContent += "\r\nEXPOSURE_CTRL=" + String(exposure_ctrl);
    camIniContent += "\r\nAEC2=" + String(aec2);
    camIniContent += "\r\nAE_LEVEL=" + String(ae_level);
    camIniContent += "\r\nAEC_VALUE=" + String(aec_value);
    camIniContent += "\r\nGAIN_CTRL=" + String(gain_ctrl);
    camIniContent += "\r\nAGC_GAIN=" + String(agc_gain);
    camIniContent += "\r\nGAINCEILING=" + String(gainceiling);
    camIniContent += "\r\nBPC=" + String(bpc);
    camIniContent += "\r\nWPC=" + String(wpc);
    camIniContent += "\r\nRAW_GMA=" + String(raw_gma);
    camIniContent += "\r\nLENC=" + String(lenc);
    camIniContent += "\r\nHMIRROR=" + String(hmirror);
    camIniContent += "\r\nVFLIP=" + String(vflip);
    camIniContent += "\r\nDCW=" + String(dcw);
    camIniContent += "\r\nCOLORBAR=" + String(colorbar);
    camIniContent += "\r\nPERIOD=" + String(period);
    camIniContent += "\r\n";
    Serial.println(camIniContent);
    camIniFile.print(camIniContent);
    camIniFile.close();
  }
}


//----------------------//
//  ESP CAM FUNCTIONS   //
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
    config.frame_size = FRAMESIZE_SXGA;
    config.jpeg_quality = 12;
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
  //s->set_brightness(s, 0);     // -2 to 2
  //Serial.println("set brightness: " + String(brightness));
  s->set_brightness(s, brightness);     // -2 to 2
  //s->set_contrast(s, 0);       // -2 to 2
  //Serial.println("set contrast: " + String(contrast));
  s->set_contrast(s, contrast);       // -2 to 2
  //s->set_saturation(s, 0);     // -2 to 2
  //Serial.println("set saturation: " + String(saturation));
  s->set_saturation(s, saturation);     // -2 to 2
  //s->set_sharpness(s, 0);      // -2 to 2
  //Serial.println("set sharpness: " + String(sharpness));
  s->set_sharpness(s, sharpness);      // -2 to 2
  //s->set_denoise(s, 1);        // 0 = disable, 1 = enable
  //Serial.println("set denoise: " + String(denoise));
  s->set_denoise(s, denoise);        // 0 = disable, 1 = enable
  //s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  //Serial.println("set special_effect: " + String(special_effect));
  s->set_special_effect(s, special_effect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  //s->set_whitebal(s, 1);       // 0 = disable , 1 = enabled
  //Serial.println("set whitebal: " + String(whitebal));
  s->set_whitebal(s, whitebal);       // 0 = disable , 1 = enable
  //s->set_awb_gain(s, 1);       // 0 = disable , 1 = enabled
  //Serial.println("set awb_gain: " + String(awb_gain));
  s->set_awb_gain(s, awb_gain);       // 0 = disable , 1 = enable
  //s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  //Serial.println("set wb_mode: " + String(wb_mode));
  s->set_wb_mode(s, wb_mode);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  //s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
  //Serial.println("set exposure_ctrl: " + String(exposure_ctrl));
  s->set_exposure_ctrl(s, exposure_ctrl);  // 0 = disable , 1 = enable
  //s->set_aec2(s, 0);           // 0 = disable , 1 = enable
  //Serial.println("set aec2: " + String(aec2));
  s->set_aec2(s, aec2);           // 0 = disable , 1 = enable
  //s->set_aec_value(s, 300);    // 0 to 1200
  //Serial.println("set aec_value: " + String(aec_value));
  s->set_aec_value(s, aec_value);    // 0 to 1200
  //s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
  //Serial.println("set gain_ctrl: " + String(gain_ctrl));
  s->set_gain_ctrl(s, gain_ctrl);      // 0 = disable , 1 = enable
  //s->set_agc_gain(s, 0);       // 0 to 30
  //Serial.println("set agc_gain: " + String(agc_gain));
  s->set_agc_gain(s, agc_gain);       // 0 to 30
  //s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
  //Serial.println("set gainceiling: " + String(gainceiling));
  s->set_gainceiling(s, gainceiling_t(gainceiling));  // 0 to 6
  //s->set_bpc(s, 0);            // 0 = disable , 1 = enable
  //Serial.println("set bpc: " + String(bpc));
  s->set_bpc(s, bpc);            // 0 = disable , 1 = enable
  //s->set_wpc(s, 1);            // 0 = disable , 1 = enable
  //Serial.println("set wpc: " + String(wpc));
  s->set_wpc(s, wpc);            // 0 = disable , 1 = enable
  //s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
  //Serial.println("set raw_gma: " + String(raw_gma));
  s->set_raw_gma(s, raw_gma);        // 0 = disable , 1 = enable
  //s->set_lenc(s, 1);           // 0 = disable , 1 = enable
  //Serial.println("set lenc: " + String(lenc));
  s->set_lenc(s, lenc);           // 0 = disable , 1 = enable
  //s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
  //Serial.println("set hmirror: " + String(hmirror));
  s->set_hmirror(s, hmirror);        // 0 = disable , 1 = enable
  //s->set_vflip(s, 0);          // 0 = disable , 1 = enable
  //Serial.println("set vflip: " + String(vflip));
  s->set_vflip(s, vflip);          // 0 = disable , 1 = enable
  //s->set_dcw(s, 1);            // 0 = disable , 1 = enable
  //Serial.println("set dcw: " + String(dcw));
  s->set_dcw(s, dcw);            // 0 = disable , 1 = enable
  //s->set_colorbar(s, 0);       // 0 = disable , 1 = enable
  //Serial.println("set colorbar: " + String(colorbar));
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
  Serial.println("Starting SD Card... ");
  if(!SD_MMC.begin()){
    Serial.println("SD Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD Card attached");
    return;
  }
  Serial.println("Done");
}


// Take photo and save to microSD card
void takeSavePhoto(){
  // Take Picture with Camera
  camera_fb_t * fb = esp_camera_fb_get();
 
  //Uncomment the following lines if you're getting old pictures
  esp_camera_fb_return(fb); // dispose the buffered image
  fb = NULL; // reset to capture errors
  fb = esp_camera_fb_get();
  
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
  delay(1000);
  Serial.println("Version: " + firmwareVer);
  delay(2000);

  // Initialize MicroSD
  Serial.print("Initializing the MicroSD card module... ");
  initMicroSDCard();

  if (SD_MMC.exists("/config.ini")) {
    //Serial.println("config.ini exists");
    File iniFile = SD_MMC.open("/config.ini", "r");
    if (iniFile) {
      //Serial.println("config.ini found, loading");
      String iniData;
      while (iniFile.available()) {
        char chnk = iniFile.read();
        iniData += chnk;
      }
      iniFile.close();

      //Serial.println("processing ini entries:");

      if (instr(iniData, "AP_SSID=")) {
        AP_SSID = split(iniData, "AP_SSID=", "\r\n");
        AP_SSID.trim();
      }
      //Serial.println("AP_SSID = " + AP_SSID);

      if (instr(iniData, "AP_PASS=")) {
        AP_PASS = split(iniData, "AP_PASS=", "\r\n");
        AP_PASS.trim();
      }
      //Serial.println("AP_PASS = " + AP_PASS);

      if (instr(iniData, "WEBSERVER_IP=")) {
        String strwIp = split(iniData, "WEBSERVER_IP=", "\r\n");
        strwIp.trim();
        Server_IP.fromString(strwIp);
      }
      //Serial.println("WEBSERVER_IP = " + String(Server_IP));

      if (instr(iniData, "SUBNET_MASK=")) {
        String strsIp = split(iniData, "SUBNET_MASK=", "\r\n");
        strsIp.trim();
        Subnet_Mask.fromString(strsIp);
      }
      //Serial.println("SUBNET_MASK = " + String(Subnet_Mask));

      if (instr(iniData, "WIFI_SSID=")) {
        WIFI_SSID = split(iniData, "WIFI_SSID=", "\r\n");
        WIFI_SSID.trim();
      }
      //Serial.println("WIFI_SSID = " + WIFI_SSID);

      if (instr(iniData, "WIFI_PASS=")) {
        WIFI_PASS = split(iniData, "WIFI_PASS=", "\r\n");
        WIFI_PASS.trim();
      }
      //Serial.println("WIFI_PASS = " + WIFI_PASS);

      if (instr(iniData, "WIFI_HOST=")) {
        WIFI_HOSTNAME = split(iniData, "WIFI_HOST=", "\r\n");
        WIFI_HOSTNAME.trim();
      }
      //Serial.println("WIFI_HOSTNAME = " + WIFI_HOSTNAME);

      if (instr(iniData, "USEAP=")) {
        String strua = split(iniData, "USEAP=", "\r\n");
        strua.trim();
        if (strua.equals("true")) {
          startAP = true;
        } else {
          startAP = false;
        }
      }
      //Serial.println("USEAP = " + String(startAP));

      if (instr(iniData, "CONWIFI=")) {
        String strcw = split(iniData, "CONWIFI=", "\r\n");
        strcw.trim();
        if (strcw.equals("true")) {
          connectWifi = true;
        } else {
          connectWifi = false;
        }
      }
      //Serial.println("CONNWIFI = " + String(connectWifi));

      /*if (instr(iniData, "MYTIMEZONE=")) {
        String strtz = split(iniData, "MYTIMEZONE=", "\r\n");
        strtz.trim();
        myTimezone = strtz;
      }
      //Serial.println("MYTIMEZONE = " + myTimezone);

      if (instr(iniData, "BRIGHTNESS=")) {
        String strbrt = split(iniData, "BRIGHTNESS=", "\r\n");
        strbrt.trim();
        brightness = strbrt.toInt();
      }
      //Serial.println("BRIGHTNESS = " + String(brightness));
      
      if (instr(iniData, "CONTRAST=")) {
        String strcon = split(iniData, "CONTRAST=", "\r\n");
        strcon.trim();
        contrast = strcon.toInt();
      }
      //Serial.println("CONTRAST = " + String(contrast));

      if (instr(iniData, "SATURATION=")) {
        String strsat = split(iniData, "SATURATION=", "\r\n");
        strsat.trim();
        saturation = strsat.toInt();
        
      }
      //Serial.println("SATURATION = " + String(saturation));

      if (instr(iniData, "SHARPNESS=")) {
        String strsha = split(iniData, "SHARPNESS=", "\r\n");
        strsha.trim();
        sharpness = strsha.toInt();
      }
      //Serial.println("SHARPNESS = " + String(sharpness));

      if (instr(iniData, "DENOISE=")) {
        String strden = split(iniData, "DENOISE=", "\r\n");
        strden.trim();
        denoise = strden.toInt();
      }
      //Serial.println("DENOISE = " + String(denoise));

      if (instr(iniData, "SPECIAL_EFFECT=")) {
        String strsfx = split(iniData, "SPECIAL_EFFECT=", "\r\n");
        strsfx.trim();
        special_effect = strsfx.toInt();        
      }
      //Serial.println("SPECIAL_EFFECT = " + String(special_effect));

      if (instr(iniData, "WHITEBAL=")) {
        String strwhi = split(iniData, "WHITEBAL=", "\r\n");
        strwhi.trim();
        whitebal = strwhi.toInt();
      }
      //Serial.println("WHITEBAL = " + String(whitebal));

      if (instr(iniData, "AWB_GAIN=")) {
        String strawb = split(iniData, "AWB_GAIN=", "\r\n");
        strawb.trim();
        awb_gain = strawb.toInt();
      }
      //Serial.println("AWB_GAIN = " + String(awb_gain));

      if (instr(iniData, "WB_MODE=")) {
        String strwbm = split(iniData, "WB_MODE=", "\r\n");
        strwbm.trim();
        wb_mode = strwbm.toInt();        
      }
      //Serial.println("WB_MODE = " + String(wb_mode));

      if (instr(iniData, "EXPOSURE_CTRL=")) {
        String strexp = split(iniData, "EXPOSURE_CTRL=", "\r\n");
        strexp.trim();
        exposure_ctrl = strexp.toInt();
      }
      //Serial.println("EXPOSURE_CTRL = " + String(exposure_ctrl));

      if (instr(iniData, "AEC2=")) {
        String straec2 = split(iniData, "AEC2=", "\r\n");
        straec2.trim();
        aec2 = straec2.toInt();
      }
      //Serial.println("AEC2 = " + String(aec2));

      if (instr(iniData, "AE_LEVEL=")) {
        String strael = split(iniData, "AE_LEVEL=", "\r\n");
        strael.trim();
        ae_level = strael.toInt();
      }
      //Serial.println("AE_LEVEL = " + String(ae_level));

      if (instr(iniData, "AEC_VALUE=")) {
        String straecv = split(iniData, "AEC_VALUE=", "\r\n");
        straecv.trim();
        aec_value = straecv.toInt();
      }
      //Serial.println("AEC_VALUE = " + String(aec_value));

      if (instr(iniData, "GAIN_CTRL=")) {
        String strgai = split(iniData, "GAIN_CTRL=", "\r\n");
        strgai.trim();
        gain_ctrl = strgai.toInt();
      }
      //Serial.println("GAIN_CTRL = " + String(gain_ctrl));

      if (instr(iniData, "AGC_GAIN=")) {
        String stragcg = split(iniData, "AGC_GAIN=", "\r\n");
        stragcg.trim();
        agc_gain = stragcg.toInt();
      }
      //Serial.println("AGC_GAIN = " + String(agc_gain));

      if (instr(iniData, "GAINCEILING")) {
        String strgac= split(iniData, "GAINCEILING=", "\r\n");
        strgac.trim();
        gainceiling = strgac.toInt();
      }
      //Serial.println("GAINCEILING = " + String(gainceiling));

      if (instr(iniData, "BPC=")) {
        String strbpc = split(iniData, "BPC=", "\r\n");
        strbpc.trim();
        bpc = strbpc.toInt();
      }
      //Serial.println("BPC = " + String(bpc));

      if (instr(iniData, "WPC=")) {
        String strwpc = split(iniData, "WPC=", "\r\n");
        strwpc.trim();
        wpc = strwpc.toInt();
      }
      //Serial.println("WPC = " + String(wpc));

      if (instr(iniData, "RAW_GMA=")) {
        String strraw = split(iniData, "RAW_GMA=", "\r\n");
        strraw.trim();
        raw_gma = strraw.toInt();
      }
      //Serial.println("RAW_GMA = " + String(raw_gma));

      if (instr(iniData, "LENC=")) {
        String strlenc = split(iniData, "LENC=", "\r\n");
        strlenc.trim();
        lenc = strlenc.toInt();
      }
      //Serial.println("LENC = " + String(lenc));

      if (instr(iniData, "HMIRROR=")) {
        String strhmi = split(iniData, "HMIRROR=", "\r\n");
        strhmi.trim();
        hmirror = strhmi.toInt();
      }
      //Serial.println("HMIRROR = " + String(hmirror));

      if (instr(iniData, "VFLIP=")) {
        String strvfl = split(iniData, "VFLIP=", "\r\n");
        strvfl.trim();
        vflip = strvfl.toInt();
      }
      //Serial.println("VFLIP = " + String(vflip));

      if (instr(iniData, "DCW=")) {
        String strdcw = split(iniData, "DCW=", "\r\n");
        strdcw.trim();
        dcw = strdcw.toInt();
      }
      //Serial.println("DCW = " + String(dcw));

      if (instr(iniData, "COLORBAR=")) {
        String strcolo = split(iniData, "COLORBAR=", "\r\n");
        strcolo.trim();
        colorbar = strcolo.toInt();
      }
      //Serial.println("COLORBAR = " + String(colorbar));

      if (instr(iniData, "PERIOD=")) {
        String strper = split(iniData, "PERIOD=", "\r\n");
        strper.trim();
        period = strper.toInt();
      }
      //Serial.println("PERIOD = " + String(period));
      */
    }
  } else {
    //Serial.println("no config.ini found, start writeconfig");
    writeConfig();
  }

  if (SD_MMC.exists("/camconfig.ini")) {
    //Serial.println("camconfig.ini exists");
    File camIniFile = SD_MMC.open("/camconfig.ini", "r");
    if (camIniFile) {
      Serial.println("camconfig.ini found, loading");
      String camIniData;
      while (camIniFile.available()) {
        char chnk = camIniFile.read();
        camIniData += chnk;
      }
      camIniFile.close();

      //Serial.println("processing ini entries:");

      if (instr(camIniData, "MYTIMEZONE=")) {
        String strtz = split(camIniData, "MYTIMEZONE=", "\r\n");
        strtz.trim();
        myTimezone = strtz;
      }
      Serial.println("MYTIMEZONE = " + myTimezone);

      if (instr(camIniData, "BRIGHTNESS=")) {
        String strbrt = split(camIniData, "BRIGHTNESS=", "\r\n");
        strbrt.trim();
        brightness = strbrt.toInt();
      }
      Serial.println("BRIGHTNESS = " + String(brightness));
      
      if (instr(camIniData, "CONTRAST=")) {
        String strcon = split(camIniData, "CONTRAST=", "\r\n");
        strcon.trim();
        contrast = strcon.toInt();
      }
      Serial.println("CONTRAST = " + String(contrast));

      if (instr(camIniData, "SATURATION=")) {
        String strsat = split(camIniData, "SATURATION=", "\r\n");
        strsat.trim();
        saturation = strsat.toInt();
        
      }
      Serial.println("SATURATION = " + String(saturation));

      if (instr(camIniData, "SHARPNESS=")) {
        String strsha = split(camIniData, "SHARPNESS=", "\r\n");
        strsha.trim();
        sharpness = strsha.toInt();
      }
      Serial.println("SHARPNESS = " + String(sharpness));

      if (instr(camIniData, "DENOISE=")) {
        String strden = split(camIniData, "DENOISE=", "\r\n");
        strden.trim();
        denoise = strden.toInt();
      }
      Serial.println("DENOISE = " + String(denoise));

      if (instr(camIniData, "SPECIAL_EFFECT=")) {
        String strsfx = split(camIniData, "SPECIAL_EFFECT=", "\r\n");
        strsfx.trim();
        special_effect = strsfx.toInt();        
      }
      Serial.println("SPECIAL_EFFECT = " + String(special_effect));

      if (instr(camIniData, "WHITEBAL=")) {
        String strwhi = split(camIniData, "WHITEBAL=", "\r\n");
        strwhi.trim();
        whitebal = strwhi.toInt();
      }
      Serial.println("WHITEBAL = " + String(whitebal));

      if (instr(camIniData, "AWB_GAIN=")) {
        String strawb = split(camIniData, "AWB_GAIN=", "\r\n");
        strawb.trim();
        awb_gain = strawb.toInt();
      }
      Serial.println("AWB_GAIN = " + String(awb_gain));

      if (instr(camIniData, "WB_MODE=")) {
        String strwbm = split(camIniData, "WB_MODE=", "\r\n");
        strwbm.trim();
        wb_mode = strwbm.toInt();        
      }
      Serial.println("WB_MODE = " + String(wb_mode));

      if (instr(camIniData, "EXPOSURE_CTRL=")) {
        String strexp = split(camIniData, "EXPOSURE_CTRL=", "\r\n");
        strexp.trim();
        exposure_ctrl = strexp.toInt();
      }
      Serial.println("EXPOSURE_CTRL = " + String(exposure_ctrl));

      if (instr(camIniData, "AEC2=")) {
        String straec2 = split(camIniData, "AEC2=", "\r\n");
        straec2.trim();
        aec2 = straec2.toInt();
      }
      Serial.println("AEC2 = " + String(aec2));

      if (instr(camIniData, "AE_LEVEL=")) {
        String strael = split(camIniData, "AE_LEVEL=", "\r\n");
        strael.trim();
        ae_level = strael.toInt();
      }
      Serial.println("AE_LEVEL = " + String(ae_level));

      if (instr(camIniData, "AEC_VALUE=")) {
        String straecv = split(camIniData, "AEC_VALUE=", "\r\n");
        straecv.trim();
        aec_value = straecv.toInt();
      }
      Serial.println("AEC_VALUE = " + String(aec_value));

      if (instr(camIniData, "GAIN_CTRL=")) {
        String strgai = split(camIniData, "GAIN_CTRL=", "\r\n");
        strgai.trim();
        gain_ctrl = strgai.toInt();
      }
      Serial.println("GAIN_CTRL = " + String(gain_ctrl));

      if (instr(camIniData, "AGC_GAIN=")) {
        String stragcg = split(camIniData, "AGC_GAIN=", "\r\n");
        stragcg.trim();
        agc_gain = stragcg.toInt();
      }
      Serial.println("AGC_GAIN = " + String(agc_gain));

      if (instr(camIniData, "GAINCEILING")) {
        String strgac= split(camIniData, "GAINCEILING=", "\r\n");
        strgac.trim();
        gainceiling = strgac.toInt();
      }
      Serial.println("GAINCEILING = " + String(gainceiling));

      if (instr(camIniData, "BPC=")) {
        String strbpc = split(camIniData, "BPC=", "\r\n");
        strbpc.trim();
        bpc = strbpc.toInt();
      }
      Serial.println("BPC = " + String(bpc));

      if (instr(camIniData, "WPC=")) {
        String strwpc = split(camIniData, "WPC=", "\r\n");
        strwpc.trim();
        wpc = strwpc.toInt();
      }
      Serial.println("WPC = " + String(wpc));

      if (instr(camIniData, "RAW_GMA=")) {
        String strraw = split(camIniData, "RAW_GMA=", "\r\n");
        strraw.trim();
        raw_gma = strraw.toInt();
      }
      Serial.println("RAW_GMA = " + String(raw_gma));

      if (instr(camIniData, "LENC=")) {
        String strlenc = split(camIniData, "LENC=", "\r\n");
        strlenc.trim();
        lenc = strlenc.toInt();
      }
      Serial.println("LENC = " + String(lenc));

      if (instr(camIniData, "HMIRROR=")) {
        String strhmi = split(camIniData, "HMIRROR=", "\r\n");
        strhmi.trim();
        hmirror = strhmi.toInt();
      }
      Serial.println("HMIRROR = " + String(hmirror));

      if (instr(camIniData, "VFLIP=")) {
        String strvfl = split(camIniData, "VFLIP=", "\r\n");
        strvfl.trim();
        vflip = strvfl.toInt();
      }
      Serial.println("VFLIP = " + String(vflip));

      if (instr(camIniData, "DCW=")) {
        String strdcw = split(camIniData, "DCW=", "\r\n");
        strdcw.trim();
        dcw = strdcw.toInt();
      }
      Serial.println("DCW = " + String(dcw));

      if (instr(camIniData, "COLORBAR=")) {
        String strcolo = split(camIniData, "COLORBAR=", "\r\n");
        strcolo.trim();
        colorbar = strcolo.toInt();
      }
      Serial.println("COLORBAR = " + String(colorbar));

      if (instr(camIniData, "PERIOD=")) {
        String strper = split(camIniData, "PERIOD=", "\r\n");
        strper.trim();
        period = strper.toInt();
      }
      Serial.println("PERIOD = " + String(period));
    }
  } else {
    Serial.println("no camconfig.ini found, start writeCamConfig");
    writeCamConfig();
  }


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
        //Serial.println("WEB Server LAN IP: " + LAN_IP.toString());
        //Serial.println("WEB Server Port: " + String(WEB_PORT));
        //Serial.println("WEB Server Hostname: " + WIFI_HOSTNAME);
        String mdnsHost = WIFI_HOSTNAME;
        mdnsHost.replace(".local", "");
        MDNS.begin(mdnsHost.c_str());
        if (!startAP) {
          dnsServer.setTTL(30);
          dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
          dnsServer.start(53, "*", LAN_IP);
          Serial.println("DNS server started");
          //Serial.println("DNS Server IP: " + LAN_IP.toString());
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
  
  // web server reacts
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Microsoft Connect Test");
  });

  server.on("/config.ini", HTTP_ANY, [](AsyncWebServerRequest *request) {
    request->send(404);
  });

  server.on("/camconfig.ini", HTTP_ANY, [](AsyncWebServerRequest *request) {
    request->send(404);
  });

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

  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleConfigHtml(request);
  });

  server.on("/config.html", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleConfig(request);
  });

  server.on("/camconfig.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleCamConfigHtml(request);
  });

  server.on("/camconfig.html", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleCamConfig(request);
  });

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

  server.serveStatic("/", SD_MMC, "/").setDefaultFile("index.html");

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
  SD_MMC.end();
  //SD_MMC.format();
  SD_MMC.begin();
  delay(1000);
  writeConfig();
  writeCamConfig();
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