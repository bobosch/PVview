#include <ETH.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <time.h>

#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include "modbus.h"
#include "RemoteDebug.h" // https://github.com/JoaoLopesF/RemoteDebug
#include <Preferences.h> // https://randomnerdtutorials.com/esp32-save-data-permanently-preferences/

//#define DEBUG_DISABLED

// MD Parola settings
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4 // * 8 dots
#define LINES 1

#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 5

// Application settings
#define SHOW_POWER 0
#define SHOW_ENERGY 1
#define SHOW_TIME 2
const String NameElement[3] = { "Power", "Energy", "Time" };

#define ON_POWER 0
#define ALWAYS 1
const String NameWhen[2] = { "On power", "Always" };

#define LEFT 0
#define CENTER 1
#define RIGHT 2
const String NameAlign[3] = { "Left", "Center", "Right" };
const textPosition_t Align[3] = { PA_LEFT, PA_CENTER, PA_RIGHT };

// Modbus settings
#define MB_COUNT 1
const struct {
  unsigned char EM;
  const char *Host;
  unsigned char Unit;
} MB[MB_COUNT] = {
  // EM, IP,          Unit
  { 0, "FroniusSymo15", 1 },
};

struct {
    uint8_t Element;
    uint8_t When;
    uint8_t Align;
// Modify this array to your needs
} Show[3] = {
  // Element,    When,     Align
  { SHOW_POWER,  ON_POWER, RIGHT },
  { SHOW_ENERGY, ALWAYS,   RIGHT },
  { SHOW_TIME,   ALWAYS,   CENTER },
};

#if MAX_DEVICES == 6
#if LINES == 2
#define SPLIT_LINE
#include "Font8S.h"
#endif
#endif

#if MAX_DEVICES == 4
#include "Font8S.h"
#else
#include "Font8L.h"
#endif

struct {
    unsigned char Desc[13];
    MBEndianess Endianness;
    unsigned char Function;       // 3: holding registers, 4: input registers
    MBDataType VoltageDataType;
    unsigned int VoltageRegister; // Single phase voltage (V)
    signed char VoltageMultiplier;// 10^x
    MBDataType CurrentDataType;
    unsigned int CurrentRegister; // Single phase current (A)
    signed char CurrentMultiplier;// 10^x
    MBDataType PowerDataType;
    unsigned int PowerRegister;   // Total power (W)
    signed char PowerMultiplier;  // 10^x
    MBDataType EnergyDataType;
    unsigned int EnergyRegister;  // Total energy (Wh)
    signed char EnergyMultiplier; // 10^x
} EM[10] = {
    // Desc,  Endianness, Function, U:DataType, Reg, Mul, I:DataType, Reg, Mul, P:DataType, Reg, Mul, E:DataType, Reg, Mul
    // Modbus TCP inverter
    { "Fronius Symo",MB_HBF_HWF, 3, MB_FLOAT32, 40085, 0, MB_FLOAT32, 40073, 0, MB_FLOAT32, 40091, 0, MB_FLOAT32, 40101, 0 },
    { "Sungrow",     MB_HBF_LWF, 4, MB_UINT16,   5018, 0, MB_UINT16,   5021, 0, MB_UINT32,   5008, 0, MB_UINT32,   5003, 3 },
    { "Sunny WebBox",MB_HBF_HWF, 3, MB_UINT32,  30783, 0, MB_UINT32,  30797, 0, MB_SINT32,  30775, 0, MB_UINT64,  30513, 0 }, // Unit ID 2
    { "SolarEdge",   MB_HBF_HWF, 3, MB_SINT16,  40196, 0, MB_SINT16,  40191, 0, MB_SINT16,  40083, 0, MB_SINT32,  40226, 0 }, // SolarEdge SunSpec (0.01V / 0.1A / 1W / 1 Wh)
    // Modbus RTU electric meter
    { "ABB",         MB_HBF_HWF, 3, MB_UINT32, 0x5B00,-1, MB_UINT32, 0x5B0C,-2, MB_SINT32, 0x5B14,-2, MB_UINT64, 0x5000, 1 }, // ABB B23 212-100 (0.1V / 0.01A / 0.01W / 0.01kWh) RS485 wiring reversed / max read count 125
    { "Eastron",     MB_HBF_HWF, 4, MB_FLOAT32,   0x0, 0, MB_FLOAT32,   0x6, 0, MB_FLOAT32,  0x34, 0, MB_FLOAT32, 0x156, 3 }, // Eastron SDM630 (V / A / W / kWh) max read count 80
    { "Finder 7E",   MB_HBF_HWF, 4, MB_FLOAT32,0x1000, 0, MB_FLOAT32,0x100E, 0, MB_FLOAT32,0x1026, 0, MB_FLOAT32,0x1106, 0 }, // Finder 7E.78.8.400.0212 (V / A / W / Wh) max read count 127
    { "Finder 7M",   MB_HBF_HWF, 4, MB_FLOAT32,  2500, 0, MB_FLOAT32,  2516, 0, MB_FLOAT32,  2536, 0, MB_FLOAT32,  2638, 0 }, // Finder 7M.38.8.400.0212 (V / A / W / Wh) / Backlight 10173
    { "Phoenix Cont",MB_HBF_LWF, 4, MB_SINT32,    0x0,-1, MB_SINT32,    0xC,-3, MB_SINT32,   0x28,-1, MB_SINT32,   0x3E, 2 }, // PHOENIX CONTACT EEM-350-D-MCB (0,1V / mA / 0,1W / 0,1kWh) max read count 11
    { "WAGO",        MB_HBF_HWF, 3, MB_FLOAT32,0x5002, 0, MB_FLOAT32,0x500C, 0, MB_FLOAT32,0x5012, 3, MB_FLOAT32,0x6000, 3 }, // WAGO 879-30x0 (V / A / kW / kWh)
};

// Time
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

const char prefixes[] = " kMGTPEZYRQ";

bool eth_connected = false;
char message[LINES][12];
uint8_t Count, cycle = 0, digitsW, digitsWh, Interval, RetryAfter, RetryAfterCount, RetryError, RetryErrorCount = 0, Requested = 0;
unsigned long timer = 0;
float AddEnergy, Energy = 0, Power = 0, Sum;
String Hostname, NTPServer;

Preferences preferences;

MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

Modbus M[MB_COUNT];

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

String htmlSelect(String Name, const String Options[], uint8_t OptionsSize, uint8_t Selected) {
  uint8_t i;
  String ret = "<select name='" + Name + "' size='1'>";
  for (i = 0; i < OptionsSize; i++) {
    ret += "<option value='" + String(i) + "'";
    if (i == Selected) ret += " selected";
    ret += ">" + Options[i] + "</option>";
  }
  ret += "</select>";
  return ret;
}

// HTTP handlers
void handleRoot() {
  server.send(200, "text/plain", "Hello from wESP32!\n");
}
void handleNotFound() {
  server.send(404, "text/plain", String("No ") + server.uri() + " here!\n");
}
void handleSettings() {
  uint8_t edit,i;
  String TableShow = "";

  // Save settings
  String Send = server.arg("Send");
  if (Send == "1") {
    // Save preferences
    preferences.begin("PVview", false);
    // Hostname
    Hostname = server.arg("Hostname");
    debugD("New Hostname %s", Hostname);
    preferences.putString("Hostname", Hostname);
    // NTPServer
    NTPServer = server.arg("NTPServer");
    debugD("New NTPServer %s", NTPServer);
    preferences.putString("NTPServer", NTPServer);
    // Interval
    Interval = server.arg("Interval").toInt();
    debugD("New Interval %u s", Interval);
    preferences.putUChar("Interval", Interval);
    // RetryAfter
    RetryAfter = server.arg("RetryAfter").toInt();
    debugD("New RetryAfter %u", RetryAfter);
    preferences.putUChar("RetryAfter", RetryAfter);
    // RetryError
    RetryError = server.arg("RetryError").toInt();
    debugD("New RetryError %u", RetryError);
    preferences.putUChar("RetryError", RetryError);
    // AddEnergy
    AddEnergy = server.arg("AddEnergy").toFloat() * 1000;
    debugD("New AddEnergy %0.0f Wh", AddEnergy);
    preferences.putFloat("AddEnergy", AddEnergy);
    // Saved
    debugI("Settings saved");
    preferences.end();
  }

  edit = server.arg("edit").toInt();
  if (edit > 3) edit = 0;
  if (server.arg("Show") == "Save") {
    Show[edit - 1].Element = server.arg("ShowElement").toInt();
    Show[edit - 1].When = server.arg("ShowWhen").toInt();
    Show[edit - 1].Align = server.arg("ShowAlign").toInt();
    preferences.begin("PVview", false);
    preferences.putBytes("Show", &Show, sizeof(Show));
    preferences.end();
    edit = 0;
  }

  // Generate page
  for (i = 0; i < ARRAY_SIZE(Show); i++) {
    if (i + 1 == edit) {
      TableShow += "<tr><td>" + htmlSelect("ShowElement", NameElement, 3, Show[i].Element) + "</td><td>" + htmlSelect("ShowWhen", NameWhen, 2, Show[i].When) + "</td><td>" + htmlSelect("ShowAlign", NameAlign, 3, Show[i].Align) + "</td><td><input type='hidden' name='edit' value='" + String(i + 1) + "' /><input type='submit' name='Show' value='Save' /></td></tr>";
    } else {
      TableShow += "<tr><td>" + NameElement[Show[i].Element] + "</td><td>" + NameWhen[Show[i].When] + "</td><td>" + NameAlign[Show[i].Align] + "</td><td><a href='?edit=" + String(i + 1) + "'>Edit</a></td></tr>";
    }
  }
  server.send(200, "text/html",
  "<html>"
  "<head><style>"
  " form {}"
  " div { clear: both; }"
  " label { float: left; margin: 4px 0; width: 160px; }"
  " input { margin: 4px 0; }"
  " table { border-collapse: collapse; }"
  " th, td { border: 1px solid; padding: 3px 6px; }"
  "</style></head>"
  "<body>"
  "<form method='POST' action='/' enctype='multipart/form-data'>"
  " <fieldset><legend>Network</legend>"
  " <em>Changes requires a reboot to take effect</em>"
  " <div><label for='Hostname'>Hostname</label><input type='text' id='Hostname' name='Hostname' value='" + Hostname + "'/></div>"
  " <div><label for='NTPServer'>NTPServer</label><input type='text' id='NTPServer' name='NTPServer' value='" + NTPServer + "'/></div>"
  " </fieldset>"
  " <fieldset><legend>Display</legend>"
  " <div><label for='Interval'>Cycle after (s)</label><input type='text' id='Interval' name='Interval' value='" + String(Interval) + "'/></div>"
  " <div><label for='RetryAfter'>Maximum cycles without power request</label><input type='text' id='RetryAfter' name='RetryAfter' value='" + String(RetryAfter) + "'/></div>"
  " <div><label for='RetryError'>Minimum cycles with error before clear power value</label><input type='text' id='RetryError' name='RetryError' value='" + String(RetryError) + "'/></div>"
  " <div><label for='AddEnergy'>Add constant energy (kWh)</label><input type='text' id='AddEnergy' name='AddEnergy' value='" + String(AddEnergy / 1000) + "'/></div>"
  " </fieldset>"
  " <div><input type='hidden' name='Send' value='1' /><input type='submit' value='Save' /></div>"
  "</form>"
  "<form method='POST' action='/' enctype='multipart/form-data'>"
  " <table><tr><th>What</th><th>When</th><th>Align</th></tr>" + TableShow + "</table>"
  "</form>"
  "<p><a href='/serverIndex'>Firmware update</a></p>"
  "</body>"
  "</html>");
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(Hostname.c_str());
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

void printTime(char *str){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return;
  }

  sprintf(str, "%d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
}

void sumModbusValue(unsigned char show, float read, float &value) {
  if(!isnan(read) && !isinf(read)) {
    Count++;
    Sum += read;
    if (Count == MB_COUNT) {
      value = Sum;
    }
  }
}

void readModbus() {
  unsigned char MB_EM;
  unsigned int ID;
  float value;

  for(uint8_t i = 0; i < MB_COUNT; i++) {
    if (M[i].available()) {
      MB_EM = MB[i].EM;
      M[i].read();
      switch (Requested) {
        case SHOW_POWER:
          value = M[i].getValue(EM[MB_EM].Endianness, EM[MB_EM].PowerDataType, EM[MB_EM].PowerMultiplier);
          debugI("Modbus %u receive power %0.0f W", i, value);
          sumModbusValue(SHOW_POWER, value, Power);
          if (Count == MB_COUNT) {
            RetryErrorCount = 0;
          }
          break;
        case SHOW_ENERGY:
          value = M[i].getValue(EM[MB_EM].Endianness, EM[MB_EM].EnergyDataType, EM[MB_EM].EnergyMultiplier);
          debugI("Modbus %u receive energy %0.0f Wh", i, value);
          sumModbusValue(SHOW_ENERGY, value, Energy);
          if (Count == MB_COUNT) {
            debugI("Add constant energy %0.0f Wh", AddEnergy);
            Energy += AddEnergy;
          }
          break;
      }
    }
  }
}

uint8_t prefixUnit(float &value, String &unit, uint8_t maximumDigits) {
  uint8_t thousand;
  int8_t digits, exponent, point;
  char prefix = 32; // " "

  // Show more digits on wider displays
  exponent = log10(value) - (maximumDigits - 3);
  // Get position of decimal point
  point = exponent % 3;
  digits = exponent - point;
  // Don't show decimal point on smallest (default) unit
  if (digits <= 0) {
    digits = 0;
    point = 2;
  }
  // Don't show decimal point when using more than 5 digits
  if (maximumDigits >= 5) point = 2;

  // Get prefix
  thousand = digits / 3;
  if (thousand >= 0 && thousand <= 10) prefix = prefixes[thousand];
  else prefix = 63; // "?"

  // Reduce value depending on prefix
  value = value / pow10(digits);
  if (prefix != 32) unit = prefix + unit;

  return point;
}

void printModbus(char *str, float value, String unit, uint8_t maximumDigits) {
  uint8_t point, space = 32;

  if (unit == "") space = 0;

  point = prefixUnit(value, unit, maximumDigits);
  switch (point) {
    case 0:
      sprintf(str, "%.2f%c%s", value, space, unit);
      break;
    case 1:
      sprintf(str, "%.1f%c%s", value, space, unit);
      break;
    default:
      sprintf(str, "%.0f%c%s", value, space, unit);
      break;
  }
}

void setup() {
  // Load preferences
  preferences.begin("PVview", true);
  Hostname = preferences.getString("Hostname", "wESP32");
  NTPServer = preferences.getString("NTPServer", "pool.ntp.org");
  Interval = preferences.getUChar("Interval", 5);
  RetryAfter = preferences.getUChar("RetryAfter", 3);
  RetryError = preferences.getUChar("RetryError", 3);
  AddEnergy = preferences.getFloat("AddEnergy", 0);
  preferences.getBytes("Show", &Show, sizeof(Show));
  preferences.end();
  RetryAfterCount = RetryAfter;

  WiFi.onEvent(WiFiEvent);

  // Start the Ethernet, revision 7+ uses RTL8201
  ETH.begin(0, -1, 16, 17, ETH_PHY_RTL8201);

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, NTPServer.c_str());

  // You can browse to wesp32demo.local with this
  MDNS.begin("wesp32demo");

  // Bind HTTP handler
  server.on("/", handleSettings);
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

  // Preferences
  server.on("/settings", handleSettings);

  // Start the Ethernet web server
  server.begin();

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  // Debug
  Debug.begin(Hostname, 23, 3);
  Debug.showColors(true);
  Debug.setResetCmdEnabled(true);

  // Matrix display
  P.begin(LINES);

#if MAX_DEVICES == 4
  P.setFont(Font8S);
#else
  P.setFont(Font8L);
#endif
#if MAX_DEVICES > 5
  digitsW = (MAX_DEVICES * 8 - P.getTextColumns("M W")) / (P.getTextColumns("8") + 1);
  digitsWh = (MAX_DEVICES * 8 - P.getTextColumns("M Wh")) / (P.getTextColumns("8") + 1);
#else
  digitsW = 3;
  digitsWh = 3;
#endif

#ifdef SPLIT_LINE
  P.setZone(0, 0, 2);
  P.setFont(0, Font8S);
  P.setZone(1, 3, 5);
  P.setFont(1, Font8L);
#endif

  cycle = ARRAY_SIZE(Show) - 1;
  for (uint8_t i = 0; i < MB_COUNT; i++) {
    M[i] = Modbus();
  }
}

void loop() {
  uint8_t i, j, Element, MB_EM;
#ifdef SPLIT_LINE
  float value;
  String unit;
#endif

  // Only on ethernet connection
  if (eth_connected) {
    // Handle http connection
    server.handleClient();
    // Handle modbus response
    readModbus();

    // Wait INTERVAL
    if (timer < millis()) {
      timer = millis() + ((uint16_t)Interval * 1000);

      // Clear power when maximum retries exceeded
      if (RetryErrorCount > RetryError) {
        Power = 0;
      }

      // Find the next element to show
      for (i = 0; i < ARRAY_SIZE(Show); i++) {
        cycle = (cycle + 1) % ARRAY_SIZE(Show);
        if (cycle == 0) {
          RetryAfterCount++;
        }
        if (Show[cycle].When == ALWAYS || (Show[cycle].When == ON_POWER && Power > 0)) {
          break;
        }
      }

      // Request power after maximum cycles without power request
      if (RetryAfterCount > RetryAfter) {
        Element = SHOW_POWER;
      } else {
        Element = Show[cycle].Element;
      }

      // Request values from all electric meters
      Count = 0;
      Sum = 0;
      Requested = Element;
      for (i = 0; i < MB_COUNT; i++) {
        MB_EM = MB[i].EM;
        switch (Element) {
          case SHOW_POWER:
            if (i == 0) {
              RetryAfterCount = 0;
              RetryErrorCount++;
            }
            M[i].readInputRequest(MB[i].Host, MB[i].Unit, EM[MB_EM].Function, EM[MB_EM].PowerRegister, M[i].getDataTypeLength(EM[MB_EM].PowerDataType) / 2);
            debugD("Modbus %u request power", i);
            break;
          case SHOW_ENERGY:
            if (Power || !Energy) {
              M[i].readInputRequest(MB[i].Host, MB[i].Unit, EM[MB_EM].Function, EM[MB_EM].EnergyRegister, M[i].getDataTypeLength(EM[MB_EM].EnergyDataType) / 2);
              debugD("Modbus %u request energy", i);
            }
            break;
        }
      }
    }
  } else {
    strcpy(message[0], "No ETH");
  }

  if (P.displayAnimate()) {
    // Set timer a bit earlier for modbus request
    timer = millis() + ((uint16_t)Interval * 1000) - 800;

    // Show new element
    for(i = 0; i < LINES; i++) strcpy(message[i], "");
#ifdef SPLIT_LINE
    unit = "";
#endif
    if(Show[cycle].When == ALWAYS || (Show[cycle].When == ON_POWER && Power > 0)) {
      switch(Show[cycle].Element) {
#ifdef SPLIT_LINE
        case SHOW_POWER:
          value = Power;
          unit = "W";
          break;
        case SHOW_ENERGY:
          value = Energy;
          unit = "Wh";
          break;
#else
        case SHOW_POWER:
          printModbus(message[0], Power, "W", digitsW);
          break;
        case SHOW_ENERGY:
#if MAX_DEVICES == 4
          printModbus(message[0], Energy, "wh", digitsWh);
#else
          printModbus(message[0], Energy, "Wh", digitsWh);
#endif
          break;
#endif
        case SHOW_TIME:
          printTime(message[0]);
          break;
      }
    }

#ifdef SPLIT_LINE
    if (unit != "") {
      printModbus(message[0], value, "", 4);
      prefixUnit(value, unit, 4);
      sprintf(message[1], "%s", unit);
    }
#endif
    for(i = 0; i < LINES; i++) {
      debugV("Display line %u: %s", i, message[i]);
      P.displayZoneText(i, message[i], Align[Show[cycle].Align], 0, (uint16_t)Interval * 1000, PA_NO_EFFECT, PA_NO_EFFECT);
    }
  }

  Debug.handle();
}
