#include <ETH.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <time.h>

#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include "PVfont.h"
#include "modbus.h"
#include "RemoteDebug.h"  //https://github.com/JoaoLopesF/RemoteDebug

// Network settings
#define HOSTNAME "wESP32"
//#define DEBUG_DISABLED

// MD Parola settings
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 5

// Modbus settings
#define MB_EM 0
#define MB_IP "192.168.1.4"
#define MB_UNIT 1

// Application settings
#define INTERVAL 5 // seconds
#define RETRY_AFTER 3 // times
#define RETRY_ERROR 3 // times

#define SHOW_POWER 0
#define SHOW_ENERGY 1
#define SHOW_TIME 2
#define ON_POWER 0
#define ALWAYS 1

const struct {
    unsigned char Element;
    unsigned char When;
    textPosition_t Align;
// Modify this array to your needs
} Show[3] = {
  // Element,    When,     Align
  { SHOW_POWER,  ON_POWER, PA_RIGHT },
  { SHOW_ENERGY, ALWAYS,   PA_RIGHT },
  { SHOW_TIME,   ALWAYS,   PA_CENTER },
};

struct {
    unsigned char Desc[13];
    MBEndianess Endianness;
    MBDataType DataType;
    unsigned char Function; // 3: holding registers, 4: input registers
    unsigned int PowerRegister; // Total power (W)
    signed char PowerMultiplier; // 10^x
    unsigned int EnergyRegister; // Total energy (Wh)
    signed char EnergyMultiplier; // 10^x
} EM[8] = {
    // Desc,         Endianness,          DataType,     Function, P_Reg,Mul, E_Reg,Mul
    { "Fronius Symo",MB_ENDIANESS_HBF_HWF,MB_DATATYPE_FLOAT32, 3,  40091, 0,  40101, 0 },
    { "Phoenix Cont",MB_ENDIANESS_HBF_LWF,MB_DATATYPE_INT32,   4,   0x28,-1,   0x3E, 2 }, // PHOENIX CONTACT EEM-350-D-MCB (0,1V / mA / 0,1W / 0,1kWh) max read count 11
    { "Finder 7E",   MB_ENDIANESS_HBF_HWF,MB_DATATYPE_FLOAT32, 4, 0x1026, 0, 0x1106, 0 }, // Finder 7E.78.8.400.0212 (V / A / W / Wh) max read count 127
    { "Eastron",     MB_ENDIANESS_HBF_HWF,MB_DATATYPE_FLOAT32, 4,   0x34, 0,  0x156, 3 }, // Eastron SDM630 (V / A / W / kWh) max read count 80
    { "ABB",         MB_ENDIANESS_HBF_HWF,MB_DATATYPE_INT32,   3, 0x5B14,-2, 0x5002, 1 }, // ABB B23 212-100 (0.1V / 0.01A / 0.01W / 0.01kWh) RS485 wiring reversed / max read count 125
    { "SolarEdge",   MB_ENDIANESS_HBF_HWF,MB_DATATYPE_INT16,   3,  40083, 0,  40226, 0 }, // SolarEdge SunSpec (0.01V (16bit) / 0.1A (16bit) / 1W (16bit) / 1 Wh (32bit))
    { "WAGO",        MB_ENDIANESS_HBF_HWF,MB_DATATYPE_FLOAT32, 3, 0x5012, 3, 0x6000, 3 }, // WAGO 879-30x0 (V / A / kW / kWh)
    { "Finder 7M",   MB_ENDIANESS_HBF_HWF,MB_DATATYPE_FLOAT32, 4,   2536, 0,   2638, 0 }, // Finder 7M.38.8.400.0212 (V / A / W / Wh) / Backlight 10173
};

// Time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

const char prefixes[] = " kMGTPEZYRQ";

bool eth_connected = false;
char message[12] = "";
unsigned char cycle = 0, RetryAfter = RETRY_AFTER, RetryError = 0;
unsigned int tID[2];
unsigned long timer = 0;
float energy = 0, power = 0;

MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

Modbus M = Modbus();

WebServer server(80);

RemoteDebug Debug;

const char* serverIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";

// HTTP handlers
void handleRoot() {
  server.send(200, "text/plain", "Hello from wESP32!\n");
}
void handleNotFound() {
  server.send(404, "text/plain", String("No ") + server.uri() + " here!\n");
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(HOSTNAME);
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      eth_connected = false;
      break;
    default:
      break;
  }
}

void printTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return;
  }

  sprintf(message, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
}

void readModbus() {
  unsigned int ID;

  if (M.available()) {
    M.read();
    ID = M.getTransactionID();
    debugD("Modbus receive ID %d", ID);
    if (ID == tID[SHOW_POWER]) {
      RetryError = 0;
      power = M.getValue(EM[MB_EM].Endianness, EM[MB_EM].DataType, EM[MB_EM].PowerMultiplier);
      debugI("Power is %0.0f", power);
    }
    if (ID == tID[SHOW_ENERGY]) {
      energy = M.getValue(EM[MB_EM].Endianness, EM[MB_EM].DataType, EM[MB_EM].EnergyMultiplier);
      debugI("Energy is %0.0f", energy);
    }
  }
}

void printModbus(float value, String unit) {
  uint8_t exponent, point, thousand;
  char prefix = 32; // " "

  exponent = log10(value);
  point = exponent % 3;
  thousand = exponent - point;
  if (thousand == 0) point = 3;
  if (thousand >= 0 && thousand <= 30) prefix = prefixes[(uint8_t)(thousand / 3)];
  else prefix = 63; // "?"
  switch (point) {
    case 0:
      sprintf(message, "%1.2f %c%s", value / pow10(thousand), prefix, unit);
      break;
    case 1:
      sprintf(message, "%2.1f %c%s", value / pow10(thousand), prefix, unit);
      break;
    default:
      sprintf(message, "%3.0f %c%s", value / pow10(thousand), prefix, unit);
      break;
  }
}

void setup() {
  WiFi.onEvent(WiFiEvent);

  // Start the Ethernet, revision 7+ uses RTL8201
  ETH.begin(0, -1, 16, 17, ETH_PHY_RTL8201);

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // You can browse to wesp32demo.local with this
  MDNS.begin("wesp32demo");

  // Bind HTTP handler
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  // Update
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  // Start the Ethernet web server
  server.begin();

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  // Debug
  Debug.begin(HOSTNAME, 23, 3);
  Debug.showColors(true);
  Debug.setResetCmdEnabled(true);

  // Matrix display
  P.begin();
  P.setFont(PVfont);

  cycle = ARRAY_SIZE(Show) - 1;
}

void loop() {
  unsigned char i, Element;

  if (eth_connected) {
    server.handleClient();
    readModbus();

    if (timer < millis()) {
      timer = millis() + (INTERVAL * 1000);

      if (RetryError > RETRY_ERROR) {
        power = 0;
      }

      for (i = 0; i < ARRAY_SIZE(Show); i++) {
        cycle = (cycle + 1) % ARRAY_SIZE(Show);
        if (cycle == 0) {
          RetryAfter++;
        }
        if (Show[cycle].When == ALWAYS || (Show[cycle].When == ON_POWER && power > 0)) {
          break;
        }
      }

      if (RetryAfter > RETRY_AFTER) {
        Element = SHOW_POWER;
      } else {
        Element = Show[cycle].Element;
      }

      switch (Element) {
        case SHOW_POWER:
          RetryAfter = 0;
          RetryError++;
          M.readInputRequest(MB_IP, MB_UNIT, EM[MB_EM].Function, EM[MB_EM].PowerRegister);
          tID[Element] = M.getTransactionID();
          debugD("Modbus request power ID %d", tID[Element]);
          break;
        case SHOW_ENERGY:
          if (power || !energy) {
            M.readInputRequest(MB_IP, MB_UNIT, EM[MB_EM].Function, EM[MB_EM].EnergyRegister);
            tID[Element] = M.getTransactionID();
            debugD("Modbus request energy ID %d", tID[Element]);
          }
          break;
      }
    }
  } else {
    strcpy(message, "No ETH");
  }

  if (P.displayAnimate()) {
    timer = millis() + (INTERVAL * 1000) - 800;

    strcpy(message, "");
    if(Show[cycle].When == ALWAYS || (Show[cycle].When == ON_POWER && power > 0)) {
      switch(Show[cycle].Element) {
        case SHOW_POWER:
          printModbus(power, "W");
          break;
        case SHOW_ENERGY:
          printModbus(energy, "wh");
          break;
        case SHOW_TIME:
          printTime();
          break;
      }
    }

    //P.print(message);
    debugV("Display: %s", message);
    P.displayText(message, Show[cycle].Align, 0, INTERVAL * 1000, PA_NO_EFFECT, PA_NO_EFFECT);
  }

  Debug.handle();
}
