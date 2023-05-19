#include <ETH.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <time.h>

#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include "PVfont.h"
#include "modbus.h"

// MD Parola settings
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 5

// Modbus settings
#define MB_IP "192.168.1.4"
#define MB_UNIT 1
#define MB_FUNCTION 3
#define MB_REGISTER_POWER 40091
#define MB_ENDIANESS MB_ENDIANESS_HBF_HWF
#define MB_DATATYPE MB_DATATYPE_FLOAT32

// Application settings
#define INTERVAL 5 // seconds
#define RETRY_AFTER 3 // times
#define RETRY_ERROR 3 // times

#define SHOW_POWER 0
#define SHOW_TIME 1
#define ON_POWER 0
#define ALWAYS 1

const struct {
    unsigned char Element;
    unsigned char When;
// Modify this array to your needs
} Show[2] = {
  // Element,   When
  { SHOW_POWER, ON_POWER },
  { SHOW_TIME,  ALWAYS },
};

// Time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

const char prefixes[] = " kMGTPEZYRQ";

bool eth_connected = false;
char message[12] = "";
unsigned char cycle = 0, RetryAfter = RETRY_AFTER, RetryError = 0;
unsigned long timer = 0;
float power = 0;

MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

Modbus M = Modbus();

WebServer server(80);

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
      ETH.setHostname("wESP32");
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
  if (M.available()) {
    RetryError = 0;
    M.read();
    power = M.getValue(MB_ENDIANESS, MB_DATATYPE);
  }
}

void printModbus() {
  uint8_t exponent, point, thousand;
  char prefix = 32; // " "

  exponent = log10(power);
  point = exponent % 3;
  thousand = exponent - point;
  if (thousand == 0) point = 3;
  if (thousand >= 0 && thousand <= 30) prefix = prefixes[(uint8_t)(thousand / 3)];
  else prefix = 63; // "?"
  switch (point) {
    case 0:
      sprintf(message, "%1.2f %cW", power / pow10(thousand), prefix);
      break;
    case 1:
      sprintf(message, "%2.1f %cW", power / pow10(thousand), prefix);
      break;
    default:
      sprintf(message, "%3.0f %cW", power / pow10(thousand), prefix);
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
          M.readInputRequest(MB_IP, MB_UNIT, MB_FUNCTION, MB_REGISTER_POWER);
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
          printModbus();
          break;
        case SHOW_TIME:
          printTime();
          break;
      }
    }

    //P.print(message);
    P.displayText(message, PA_RIGHT, 0, INTERVAL * 1000, PA_NO_EFFECT, PA_NO_EFFECT);
  }
}
