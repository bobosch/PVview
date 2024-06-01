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
#define WIDTH 4 // * 8 dots
#define LINES 1

#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 5

// Application settings
//#define PVOUTPUT
#define REQUEST_BUFFER_SIZE 10

#define SHOW_POWER 0
#define SHOW_ENERGY 1
#define SHOW_TIME 2
#define SHOW_ENERGY_DAY 3
#define REQUEST_Temp 4
#define REQUEST_DC1V 5
#define REQUEST_DC1A 6
#define REQUEST_DC2V 7
#define REQUEST_DC2A 8
const String NameElement[4] = { "Power", "Energy", "Time", "Energy/Day" };

#define ON_POWER 0
#define ALWAYS 1
const String NameWhen[2] = { "On power", "Always" };

#define LEFT 0
#define CENTER 1
#define RIGHT 2
const String NameAlign[3] = { "Left", "Center", "Right" };
const textPosition_t Align[3] = { PA_LEFT, PA_CENTER, PA_RIGHT };

// Network
#define ETH_DISCONNECTED 0
#define ETH_CONNECTED 1
#define ETH_GOT_IP 2

// Modbus settings
#define MB_COUNT 5
struct {
  unsigned char EM;
  IPAddress IP;
  unsigned char Unit;
} MB[MB_COUNT];

struct {
    uint8_t Element;
    uint8_t When;
    uint8_t Align;
// Modify this array to your needs
} Show[4] = {
  // Element,        When,     Align
  { SHOW_TIME,       ALWAYS,   CENTER },
  { SHOW_POWER,      ON_POWER, RIGHT  },
  { SHOW_ENERGY_DAY, ALWAYS,   RIGHT  },
  { SHOW_ENERGY,     ALWAYS,   RIGHT  },
};

#define MAX_DEVICES (WIDTH * LINES)
#if WIDTH == 3
#if LINES == 2
#define SPLIT_LINE
#endif
#endif

#if defined SPLIT_LINE || WIDTH <= 5
#include "Font8S.h"
#endif

#if WIDTH != 4
#include "Font8L.h"
#endif

const struct {
    const char Desc[13];
    unsigned char FixedUnitId;
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
    MBDataType EnergyDayDataType;
    unsigned int EnergyDayRegister;  // Daily energy (Wh)
    signed char EnergyDayMultiplier; // 10^x
} EM[10] = {
    // Desc, Unit, Endianness, Function, U:DataType, Reg, Mul, I:DataType, Reg, Mul, P:DataType, Reg, Mul, E:DataType, Reg, Mul, E/d:DataType,Reg,Mul
    // Modbus TCP inverter
    { "Fronius Symo", 1, MB_HBF_HWF, 3, MB_FLOAT32, 40085, 0, MB_FLOAT32, 40073, 0, MB_FLOAT32, 40091, 0, MB_UINT64,    509, 0, MB_UINT64,    501, 0 },
    { "Sungrow",      1, MB_HBF_LWF, 4, MB_UINT16,   5018, 0, MB_UINT16,   5021, 0, MB_UINT32,   5008, 0, MB_UINT32,  13002, 3, MB_UINT32,  13001, 3 },
    { "SMA Sunny",    0, MB_HBF_HWF, 3, MB_UINT32,  30783,-2, MB_UINT32,  30797,-3, MB_SINT32,  30775, 0, MB_UINT64,  30513, 0, MB_UINT64,  30517, 0 }, // SMA (0,01V / mA / W / Wh ) max read count 125 / Unit 2 for WebBox
    { "SolarEdge",    1, MB_HBF_HWF, 3, MB_SINT16,  40196, 0, MB_SINT16,  40191, 0, MB_SINT16,  40083, 0, MB_SINT32,  40226, 0, MB_UINT16,      0, 0 }, // SolarEdge SunSpec (0.01V / 0.1A / 1W / 1 Wh)
    // Modbus RTU electric meter
    { "ABB",          0, MB_HBF_HWF, 3, MB_UINT32, 0x5B00,-1, MB_UINT32, 0x5B0C,-2, MB_SINT32, 0x5B14,-2, MB_UINT64, 0x5000, 1, MB_UINT16,      0, 0 }, // ABB B23 212-100 (0.1V / 0.01A / 0.01W / 0.01kWh) RS485 wiring reversed / max read count 125
    { "Eastron",      0, MB_HBF_HWF, 4, MB_FLOAT32,   0x0, 0, MB_FLOAT32,   0x6, 0, MB_FLOAT32,  0x34, 0, MB_FLOAT32, 0x156, 3, MB_UINT16,      0, 0 }, // Eastron SDM630 (V / A / W / kWh) max read count 80
    { "Finder 7E",    0, MB_HBF_HWF, 4, MB_FLOAT32,0x1000, 0, MB_FLOAT32,0x100E, 0, MB_FLOAT32,0x1026, 0, MB_FLOAT32,0x1106, 0, MB_UINT16,      0, 0 }, // Finder 7E.78.8.400.0212 (V / A / W / Wh) max read count 127
    { "Finder 7M",    0, MB_HBF_HWF, 4, MB_FLOAT32,  2500, 0, MB_FLOAT32,  2516, 0, MB_FLOAT32,  2536, 0, MB_FLOAT32,  2638, 0, MB_UINT16,      0, 0 }, // Finder 7M.38.8.400.0212 (V / A / W / Wh) / Backlight 10173
    { "Phoenix Cont", 0, MB_HBF_LWF, 4, MB_SINT32,    0x0,-1, MB_SINT32,    0xC,-3, MB_SINT32,   0x28,-1, MB_SINT32,   0x3E, 2, MB_UINT16,      0, 0 }, // PHOENIX CONTACT EEM-350-D-MCB (0,1V / mA / 0,1W / 0,1kWh) max read count 11
    { "WAGO",         0, MB_HBF_HWF, 3, MB_FLOAT32,0x5002, 0, MB_FLOAT32,0x500C, 0, MB_FLOAT32,0x5012, 3, MB_FLOAT32,0x6000, 3, MB_UINT16,      0, 0 }, // WAGO 879-30x0 (V / A / kW / kWh)
};
const String NameEM[10] = {"Fronius Symo", "Sungrow", "SMA Sunny", "SolarEdge", "ABB", "Eastron", "Finder 7E", "Finder 7M", "Phoenix Cont", "WAGO"};
#define INVERTER_COUNT 4

const char prefixes[] = " kMGTPEZYRQ";

bool SmallNumbers;
char message[LINES][15];
uint8_t Count, cycle = 0, Cycles[LINES], digitsW, digitsWh, digitsWhd, eth_status = ETH_DISCONNECTED, Intensity, IntensityMin, Interval, MBcount;
uint8_t RetryAfter, RetryAfterCount, RetryError, RetryErrorCount = 0, Request[REQUEST_BUFFER_SIZE], RequestIn = 0, RequestOut = 0, ZeroSecond = 255;
unsigned long timer = LONG_MAX, RequestTimer = LONG_MAX;
float AddEnergy, Energy = 0, EnergyDay = 0, IntensityMaxPower, MultiplyEnergy, Power = 0, Sum;
String Hostname, NTPServer;

struct tm timeinfo;
Preferences preferences;
MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
Modbus M[MB_COUNT];
WebServer server(80);
RemoteDebug Debug;

#ifdef PVOUTPUT
#define PVO_BUFFER_SIZE 256
#include <HTTPClient.h>
// Aditional information from inverter
const struct {
    MBDataType MPPTVoltageDataType;
    signed char MPPTVoltageMultiplier; // 10^x
    unsigned int MPPT1VoltageRegister; // Voltage (V)
    unsigned int MPPT2VoltageRegister; // Current (A)
    MBDataType MPPTCurrentDataType;
    signed char MPPTCurrentMultiplier; // 10^x
    unsigned int MPPT1CurrentRegister; // Voltage (V)
    unsigned int MPPT2CurrentRegister; // Current (A)
    MBDataType TemperatureDataType;
    signed char TemperatureMultiplier; // 10^x
    unsigned int TemperatureRegister; // Temperature (°C)
} Inv[INVERTER_COUNT] = {
    // U:DataType, Mul, 1:Reg, 2:Reg, I:DataType, Mul, 1:Reg, 2:Reg, T:DataType, Mul, Reg
    { MB_UINT16,  -2,   40283, 40303, MB_UINT16, -2,   40282, 40302, MB_UINT16, -3, 40289 },
    { MB_UINT16,   0,    5011,  5013, MB_UINT16,  0,    5012,  5014, MB_UINT16,  0,     0 },
    { MB_SINT32,  -2,   30771, 30959, MB_SINT32, -3,   30769, 30957, MB_SINT32, -1, 30953 }, // (0,01V / mA)
    { MB_SINT32,   0,       0,     0, MB_SINT32,  0,       0,     0, MB_UINT16,  0,     0 },
};
uint8_t PVOIn = 0, PVOOut = 0;
int PVO_ID;
float MPPT1Current = 0, MPPT1Voltage = 0, MPPT2Current = 0, MPPT2Voltage = 0, PowerMax = 0, Temperature= 0;
String PVO_APIkey, PVO[PVO_BUFFER_SIZE];
#endif

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

String htmlSelect(String Name, const String Options[], uint8_t OptionsSize, uint8_t Selected, const String Parameters) {
  uint8_t i;
  String ret = "<select name=\"" + Name + "\" size=\"1\" " + Parameters + ">";
  for (i = 0; i < OptionsSize; i++) {
    ret += "<option value=\"" + String(i) + "\"";
    if (i == Selected) ret += " selected";
    ret += ">" + Options[i] + "</option>";
  }
  ret += "</select>";
  return ret;
}
String htmlSelect(String Name, const String Options[], uint8_t OptionsSize, uint8_t Selected) {
  return htmlSelect(Name, Options, OptionsSize, Selected, "");
}

// HTTP handlers
void handleRoot() {
  server.send(200, "text/plain", "Hello from wESP32!\n");
}
void handleNotFound() {
  server.send(404, "text/plain", String("No ") + server.uri() + " here!\n");
}
void handleSettings() {
  uint8_t clear, MBEdit, ShowEdit, i;
  String CheckSmallNumbers = "", TableMB = "", TableShow = "";

  // Save settings
  String Send = server.arg("Send");
  if (Send == "1") {
    // Save preferences
    preferences.begin("PVview", false);
    // Hostname
    Hostname = server.arg("Hostname");
    debugD("New Hostname %s", Hostname.c_str());
    preferences.putString("Hostname", Hostname);
    // NTPServer
    NTPServer = server.arg("NTPServer");
    debugD("New NTPServer %s", NTPServer.c_str());
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
    // MultiplyEnergy
    MultiplyEnergy = server.arg("MultiplyEnergy").toFloat();
    debugD("New MultiplyEnergy %0.6f", MultiplyEnergy);
    preferences.putFloat("MultiplyEnergy", MultiplyEnergy);
    // CheckSmallNumbers
    SmallNumbers = server.arg("SmallNumbers").toInt() % 2;
    debugD("New SmallNumbers %s", SmallNumbers ? "true" : "false");
    preferences.putBool("SmallNumbers", SmallNumbers);
    // Intensity
    Intensity = server.arg("Intensity").toInt() % 16;
    debugD("New Intensity %u", Intensity);
    P.setIntensity(Intensity);
    preferences.putUChar("Intensity", Intensity);
    // IntensityMaxPower
    IntensityMaxPower = server.arg("IntensityMaxPower").toFloat();
    debugD("New IntensityMaxPower %0.0f", IntensityMaxPower);
    preferences.putFloat("IntensityMaxPow", IntensityMaxPower);
    // IntensityMin
    IntensityMin = server.arg("IntensityMin").toInt() % 16;
    debugD("New IntensityMin %u", IntensityMin);
    preferences.putUChar("IntensityMin", IntensityMin);
#ifdef PVOUTPUT
    // PVO API key
    PVO_APIkey = server.arg("PVO_APIkey");
    debugD("New PVO_APIkey %s", PVO_APIkey.c_str());
    preferences.putString("PVO_APIkey", PVO_APIkey);
    // PVO ID
    PVO_ID = server.arg("PVO_ID").toInt();
    debugD("New PVO_ID %u", PVO_ID);
    preferences.putInt("PVO_ID", PVO_ID);
 #endif
    // Saved
    debugI("Settings saved");
    preferences.end();
  }

  if (SmallNumbers) CheckSmallNumbers = " checked";

  ShowEdit = server.arg("ShowEdit").toInt();
  if (ShowEdit > 4) ShowEdit = 0;
  if (ShowEdit && server.arg("Show") == "Save") {
    Show[ShowEdit - 1].Element = server.arg("ShowElement").toInt();
    Show[ShowEdit - 1].When = server.arg("ShowWhen").toInt();
    Show[ShowEdit - 1].Align = server.arg("ShowAlign").toInt();
    preferences.begin("PVview", false);
    preferences.putBytes("Show", &Show, sizeof(Show));
    preferences.end();
    ShowEdit = 0;
  }

  if (server.arg("MBNew") == "1" && MBcount < 5) MBcount++;
  if (server.arg("MBDel") == "1" && MBcount > 0) {
    MBcount--;
    preferences.begin("PVview", false);
    preferences.putUChar("MBcount", MBcount);
    preferences.end();
  }
  MBEdit = server.arg("MBEdit").toInt();
  if (MBEdit > MBcount) MBEdit = 0;
  if (MBEdit && server.arg("MB") == "Save") {
    MB[MBEdit - 1].EM = server.arg("MBEM").toInt();
    MB[MBEdit - 1].IP[0] = server.arg("MBIP0").toInt();
    MB[MBEdit - 1].IP[1] = server.arg("MBIP1").toInt();
    MB[MBEdit - 1].IP[2] = server.arg("MBIP2").toInt();
    MB[MBEdit - 1].IP[3] = server.arg("MBIP3").toInt();
    if (EM[MB[MBEdit - 1].EM].FixedUnitId) {
      MB[MBEdit - 1].Unit = EM[MB[MBEdit - 1].EM].FixedUnitId;
    } else {
      MB[MBEdit - 1].Unit = server.arg("MBUnit").toInt();
    }
    preferences.begin("PVview", false);
    preferences.putBytes("MB", &MB, sizeof(MB));
    preferences.putUChar("MBcount", MBcount);
    preferences.end();
    MBEdit = 0;
  }

  clear = server.arg("clear").toInt();
  if (clear == 2) {
    preferences.begin("PVview", false);
    preferences.clear();
    debugI("Factory default saved");
    preferences.end();
    server.sendHeader("Location", "/",true);
    server.send(302, "text/plain", "");
    ESP.restart();
  }

  // Generate page
  for (i = 0; i < ARRAY_SIZE(Show); i++) {
    if (i + 1 == ShowEdit) {
      TableShow += "<tr><td>" + htmlSelect("ShowElement", NameElement, 4, Show[i].Element) + "</td><td>" + htmlSelect("ShowWhen", NameWhen, 2, Show[i].When) + "</td><td>" + htmlSelect("ShowAlign", NameAlign, 3, Show[i].Align) + "</td><td><input type='hidden' name='ShowEdit' value='" + String(i + 1) + "' /><input type='submit' name='Show' value='Save' /></td></tr>";
    } else {
      TableShow += "<tr><td>" + NameElement[Show[i].Element] + "</td><td>" + NameWhen[Show[i].When] + "</td><td>" + NameAlign[Show[i].Align] + "</td><td><a href='?ShowEdit=" + String(i + 1) + "'>Edit</a></td></tr>";
    }
  }
  for (i = 0; i < MBcount; i++) {
    if (i + 1 == MBEdit) {
      TableMB += "<tr><td>" + htmlSelect("MBEM", NameEM, 10, MB[i].EM) + "</td>";
      TableMB += "<td class='IP'><input type='text' name='MBIP0' value='" + String(MB[i].IP[0]) + "'>.<input type='text' name='MBIP1' value='" + String(MB[i].IP[1]) + "'>.<input type='text' name='MBIP2' value='" + String(MB[i].IP[2]) + "'>.<input type='text' name='MBIP3' value='" + String(MB[i].IP[3]) + "'></td>";
      TableMB += "<td><input type='text' id='MBUnit' name='MBUnit' value='" + String(MB[i].Unit) + "' /></td>";
      TableMB += "<td><input type='hidden' name='MBEdit' value='" + String(i + 1) + "' /><input type='submit' name='MB' value='Save' /></td></tr>";
    } else {
      TableMB += "<tr><td>" + String(EM[MB[i].EM].Desc) + "</td><td>" + MB[i].IP.toString() + "</td><td>" + String(MB[i].Unit) + "</td><td><a href='?MBEdit=" + String(i + 1) + "'>Edit</a>";
      if(i + 1 == MBcount) TableMB += " <a href='?MBDel=1'>Delete</a>";
      TableMB += "</td></tr>";
    }
  }
  if (MBcount < MB_COUNT) TableMB += "<tr><td></td><td></td><td></td><td><a href='?MBNew=1'>New</a></td></tr>";
  server.send(200, "text/html",
  "<html>"
  "<head><style>"
  " form {}"
  " div { clear: both; }"
  " label { float: left; margin: 4px 0; width: 160px; }"
  " input { margin: 4px 0; }"
  " table { border-collapse: collapse; }"
  " th, td { border: 1px solid; padding: 3px 6px; }"
  " .IP input { width:40px; }"
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
  " <div><label for='MultiplyEnergy'>Multiply energy with factor</label><input type='text' id='MultiplyEnergy' name='MultiplyEnergy' value='" + String(MultiplyEnergy, 6) + "'/></div>"
  " <div><label for='SmallNumbers'>Small numbers</label><input type='checkbox' id='SmallNumbers' name='SmallNumbers' value='1'" + CheckSmallNumbers + "/></div>"
  " <div><label for='Intensity'>Intensity (0-15)</label><input type='text' id='Intensity' name='Intensity' value='" + String(Intensity) + "'/></div>"
  " <div><label for='IntensityMaxPower'>Reduce intensity below power (0 = off)</label><input type='text' id='IntensityMaxPower' name='IntensityMaxPower' value='" + String(IntensityMaxPower, 0) + "'/></div>"
  " <div><label for='IntensityMin'>Minimum intensity (0-15)</label><input type='text' id='IntensityMin' name='IntensityMin' value='" + String(IntensityMin) + "'/></div>"
  " </fieldset>"
#ifdef PVOUTPUT
  " <fieldset><legend>PVOutput</legend>"
  " <div><label for='PVO_APIkey'>API key</label><input type='text' id='PVO_APIkey' name='PVO_APIkey' value='" + PVO_APIkey + "'/></div>"
  " <div><label for='PVO_ID'>System ID</label><input type='text' id='PVO_ID' name='PVO_ID' value='" + String(PVO_ID) + "'/></div>"
  " <a href='/PVObuffer'>Unsent buffer</a>"
  " </fieldset>"
#endif
  " <div><input type='hidden' name='Send' value='1' /><input type='submit' value='Save' /></div>"
  "</form>"
  "<form method='POST' action='/' enctype='multipart/form-data'>"
  " <table><tr><th>What</th><th>When</th><th>Align</th></tr>" + TableShow + "</table>"
  "</form>"
  "<form method='POST' action='/' enctype='multipart/form-data'>"
  " <table><tr><th>Inverter / Electric meter</th><th>IP address</th><th>Modbus unit</th></tr>" + TableMB + "</table>"
  "</form>"
  "<p><a href='/serverIndex'>Firmware update</a></p>"
  "<p><a href='/?clear=" + String(clear + 1) + "'>Factory reset</a> (Click two times)</p>"
  "</body>"
  "</html>");
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(Hostname.c_str());
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      eth_status = ETH_CONNECTED;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      eth_status = ETH_GOT_IP;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      eth_status = ETH_DISCONNECTED;
      break;
    default:
      break;
  }
}

void printTime(char *str){
  if (!getLocalTime(&timeinfo)) return;

  sprintf(str, "%d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
}

void increase(uint8_t &pointer, uint8_t max) {
  pointer++;
  if (pointer == max) pointer = 0;
}

void checkTime(void) {
  if (!getLocalTime(&timeinfo)) {
    debugE("Failed to get time");
    return;
  }

  if (floor(timeinfo.tm_sec / 10) == 0) {
    if (ZeroSecond == 255) ZeroSecond = 0;
  } else {
    ZeroSecond = 255;
  }

  if (ZeroSecond == 0) {
    ZeroSecond = 1;
    // Clear daily energy at midnight
    if (timeinfo.tm_hour == 0 && timeinfo.tm_min == 0) {
      EnergyDay = 0;
    }
#ifdef PVOUTPUT
    if (PVO_APIkey && PVO_ID) {
      // Send summary at the end of the day when no energy was generated
      if (timeinfo.tm_hour == 23 && timeinfo.tm_min == 55 && EnergyDay == 0) {
        sendReport(true);
      }
      // Send statistic every 5 minutes
      if (!(timeinfo.tm_min % 5)) {
        Request[RequestIn] = SHOW_ENERGY_DAY;
        increase(RequestIn, REQUEST_BUFFER_SIZE);
        Request[RequestIn] = REQUEST_Temp;
        increase(RequestIn, REQUEST_BUFFER_SIZE);
        Request[RequestIn] = REQUEST_DC1V;
        increase(RequestIn, REQUEST_BUFFER_SIZE);
        Request[RequestIn] = REQUEST_DC1A;
        increase(RequestIn, REQUEST_BUFFER_SIZE);
        Request[RequestIn] = REQUEST_DC2V;
        increase(RequestIn, REQUEST_BUFFER_SIZE);
        Request[RequestIn] = REQUEST_DC2A;
        increase(RequestIn, REQUEST_BUFFER_SIZE);
      }
    }
#endif
  }
}

#ifdef PVOUTPUT
void handlePVObuffer(void) {
  uint8_t PVOTemp;
  String data = "";

  PVOTemp = PVOOut;
  while (PVOIn != PVOTemp) {
    data += PVO[PVOTemp] + "\n";
    increase(PVOTemp, PVO_BUFFER_SIZE);
  }

  server.send(200, "text/plain", data);
}

void sendPVO(void) {
  uint8_t PVOCount, PVOTemp;
  int httpResponseCode;
  String data, response;

  PVOCount = 1;
  PVOTemp = PVOOut;
  data = "data=" + PVO[PVOTemp];
  increase(PVOTemp, PVO_BUFFER_SIZE);

  while ((PVOIn != PVOTemp) && PVOCount < 30) {
    PVOCount++;
    data += ";" + PVO[PVOTemp];
    increase(PVOTemp, PVO_BUFFER_SIZE);
  }

  debugI("Send data to PVOutput.org");
  debugV("%s", data.c_str());

  // https://stackoverflow.com/questions/3677400/making-a-http-post-request-using-arduino
  HTTPClient http;
  http.begin("https://pvoutput.org/service/r2/addbatchstatus.jsp");

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("X-Pvoutput-Apikey", PVO_APIkey);
  http.addHeader("X-Pvoutput-SystemId", String(PVO_ID));

  httpResponseCode = http.POST(data); //Send the actual POST request
  debugV("HTTP response code %u", httpResponseCode);

  if(httpResponseCode > 0){
    response = http.getString();
    debugV("HTTP response %s", response.c_str());
  }
  http.end();

  if (httpResponseCode == 200) {
    PVOOut = PVOTemp;
  }
}

void sendReport(bool ZeroDay) {
  char str[15];
  uint8_t i;
  debugV("PowerMax %0.0f MPPT1V %0.0f MPPT2V %0.0f Temp %0.0f", PowerMax, MPPT1Voltage, MPPT2Voltage, Temperature);
  if (PowerMax > 0 || MPPT1Voltage > 0 || MPPT2Voltage > 0 || ZeroDay) {
// https://pvoutput.org/help/api_specification.html#add-batch-status-service
// Field              Format   Unit         Example
// Date               yyyymmdd date         20210228
// Time               hh:mm    time         13:00
// Energy Generation  number   watt hours   10000
// Power Generation   number   watts        2000
// Energy Consumption number   watt hours   10000
// Power Consumption  number   watts        2000
// Temperature        decimal  celsius      23.4
// Voltage            decimal  volts        240.7
// Extended Value v7  number   User Defined 100.5
// Extended Value v8  number   User Defined 328
// Extended Value v9  number   User Defined -291
// Extended Value v10 number   User Defined 29
// Extended Value v11 number   User Defined 192
// Extended Value v12 number   User Defined 9281.24

    // Date & Time
    sprintf(str, "%04d%02d%02d,%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min);
    // Add to buffer
    if (ZeroDay) {
      PVO[PVOIn] = String(str) + ",0";
    } else {
      PVO[PVOIn] = String(str) + "," + String(EnergyDay, 0) + "," + String(PowerMax, 0) + ",,," + String(Temperature, 1) + ",," + String(MPPT1Voltage, 1) + "," + String(MPPT1Current, 2) + "," + String(MPPT2Voltage, 1) + "," + String(MPPT2Current, 2);
    }
    increase(PVOIn, PVO_BUFFER_SIZE);

    sendPVO();

    PowerMax = 0;
  }
}
#endif

void sumModbusValue(float read, float &value) {
  if(!isnan(read) && !isinf(read) && read >= 0) {
    Sum += read;
  }
  Count++;
  if (Count == MBcount) {
    value = Sum;
  }
}

void readModbus() {
  unsigned char MB_EM;
  uint8_t i;
  float value;

  for(i = 0; i < MBcount; i++) {
    if (M[i].available()) {
      MB_EM = MB[i].EM;
      M[i].read();
      switch (Request[RequestOut]) {
        case SHOW_POWER:
          value = M[i].getValue(EM[MB_EM].Endianness, EM[MB_EM].PowerDataType, EM[MB_EM].PowerMultiplier);
          debugI("Modbus %u receive power %0.0f W", i, value);
          sumModbusValue(value, Power);
          if (Count == MBcount) {
            RetryErrorCount = 0;
            if (IntensityMaxPower > 0) {
              if (Power < IntensityMaxPower && (Intensity - IntensityMin) > 0) P.setIntensity((uint8_t)((Power / (IntensityMaxPower / (Intensity - IntensityMin))) + IntensityMin + 0.5));
              else P.setIntensity(Intensity);
            }
#ifdef PVOUTPUT
            if (Power > PowerMax) PowerMax = Power;
#endif
          }
          break;
        case SHOW_ENERGY:
          value = M[i].getValue(EM[MB_EM].Endianness, EM[MB_EM].EnergyDataType, EM[MB_EM].EnergyMultiplier);
          debugI("Modbus %u receive energy %0.0f Wh", i, value);
          sumModbusValue(value, Energy);
          if (Count == MBcount) {
            if (MBcount > 1) {
              debugI("Sum of received energy %0.0f Wh", Energy);
            }
            if (MultiplyEnergy != 1.0) {
              Energy *= MultiplyEnergy;
              debugI("Corrected with factor %0.6f energy %0.0f Wh", MultiplyEnergy, Energy);
            }
            if (AddEnergy > 0) {
              debugI("Add constant energy %0.0f Wh", AddEnergy);
              Energy += AddEnergy;
              debugI("Total energy %0.0f Wh", Energy);
            }
          }
          break;
        case SHOW_ENERGY_DAY:
          value = M[i].getValue(EM[MB_EM].Endianness, EM[MB_EM].EnergyDayDataType, EM[MB_EM].EnergyDayMultiplier);
          debugI("Modbus %u receive daily energy %0.0f Wh", i, value);
          sumModbusValue(value, EnergyDay);
          break;
#ifdef PVOUTPUT
        case REQUEST_Temp:
          if (MB_EM < INVERTER_COUNT) {
            Temperature = M[i].getValue(EM[MB_EM].Endianness, Inv[MB_EM].TemperatureDataType, Inv[MB_EM].TemperatureMultiplier);
            debugI("Modbus %u receive temperature %0.1f °C", i, Temperature);
            if (Temperature < 0) Temperature = 0;
          }
          Count++;
          break;
        case REQUEST_DC1V:
          if (MB_EM < INVERTER_COUNT) {
            MPPT1Voltage = M[i].getValue(EM[MB_EM].Endianness, Inv[MB_EM].MPPTVoltageDataType, Inv[MB_EM].MPPTVoltageMultiplier);
            debugI("Modbus %u receive MPPT 1 voltage %0.0f V", i, MPPT1Voltage);
            if (MPPT1Voltage < 0) MPPT1Voltage = 0;
          }
          Count++;
          break;
        case REQUEST_DC1A:
          if (MB_EM < INVERTER_COUNT) {
            MPPT1Current = M[i].getValue(EM[MB_EM].Endianness, Inv[MB_EM].MPPTCurrentDataType, Inv[MB_EM].MPPTCurrentMultiplier);
            debugI("Modbus %u receive MPPT 1 current %0.2f A", i, MPPT1Current);
            if (MPPT1Current < 0) MPPT1Current = 0;
          }
          Count++;
          break;
        case REQUEST_DC2V:
          if (MB_EM < INVERTER_COUNT) {
            MPPT2Voltage = M[i].getValue(EM[MB_EM].Endianness, Inv[MB_EM].MPPTVoltageDataType, Inv[MB_EM].MPPTVoltageMultiplier);
            debugI("Modbus %u receive MPPT 2 voltage %0.0f V", i, MPPT2Voltage);
            if (MPPT2Voltage < 0) MPPT2Voltage = 0;
          }
          Count++;
          break;
        case REQUEST_DC2A:
          if (MB_EM < INVERTER_COUNT) {
            MPPT2Current = M[i].getValue(EM[MB_EM].Endianness, Inv[MB_EM].MPPTCurrentDataType, Inv[MB_EM].MPPTCurrentMultiplier);
            debugI("Modbus %u receive MPPT 2 current %0.2f A", i, MPPT2Current);
            if (MPPT2Current < 0) MPPT2Current = 0;
          }
          Count++;
          if (Count == MBcount) sendReport(false);
          break;
#endif
      }
    }
  }
}

uint8_t prefixUnit(float &value, String &unit, uint8_t maximumDigits, bool small) {
  uint8_t exponent, thousand = 0;
  int8_t decimal = 0, over;
  char prefix = 32; // " "

  // Number of digits
  exponent = floor(log10(value));
  if (small) {
    // Number of thousands fits into
    thousand = floor(exponent / 3);
    // Decimal places to fill
    decimal = maximumDigits - (exponent - thousand * 3) - 1;
    if (decimal < 0) {
      decimal = maximumDigits - 1;
      thousand++;
    }
    if (decimal > thousand * 3) decimal = thousand * 3;
  } else {
    // Over maximum digits
    over = exponent - maximumDigits + 3;
    // Number of thousands needed
    if (over > 0) thousand = floor(over / 3);
    // Get decimal places
    if (over > 2) decimal = 2 - (over - thousand * 3);
    if (decimal >= maximumDigits) decimal = maximumDigits - 1;
    // No decimal places when using more than 6 digits
    if (maximumDigits > 6) decimal = 0;
  }

  // Get prefix
  if (thousand >= 0 && thousand <= 10) prefix = prefixes[thousand];
  else prefix = 63; // "?"

  // Reduce value depending on prefix
  value = value / pow10(thousand * 3);
  if (prefix != 32) unit = prefix + unit;

  return (uint8_t)decimal;
}

void printModbus(char *str, float value, String unit, uint8_t maximumDigits) {
  uint8_t decimal, space = 32;

  if (unit == "") space = 0;
  decimal = prefixUnit(value, unit, maximumDigits, SmallNumbers);
  switch (decimal) {
    case 0:
      sprintf(str, "%.0f%c%s", value, space, unit);
      break;
    case 1:
      sprintf(str, "%.1f%c%s", value, space, unit);
      break;
    case 2:
      sprintf(str, "%.2f%c%s", value, space, unit);
      break;
    case 3:
      sprintf(str, "%.3f%c%s", value, space, unit);
      break;
    case 4:
      sprintf(str, "%.4f%c%s", value, space, unit);
      break;
    case 5:
      sprintf(str, "%.5f%c%s", value, space, unit);
      break;
    case 6:
      sprintf(str, "%.6f%c%s", value, space, unit);
      break;
    default:
      sprintf(str, "%.7f%c%s", value, space, unit);
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
  MultiplyEnergy = preferences.getFloat("MultiplyEnergy", 1);
  preferences.getBytes("Show", &Show, sizeof(Show));
  preferences.getBytes("MB", &MB, sizeof(MB));
  MBcount = preferences.getUChar("MBcount", 0);
  SmallNumbers = preferences.getBool("SmallNumbers", false);
  Intensity = preferences.getUChar("Intensity", 7);
  IntensityMaxPower = preferences.getFloat("IntensityMaxPow", 0);
  IntensityMin = preferences.getUChar("IntensityMin", 0);
#ifdef PVOUTPUT
  PVO_APIkey = preferences.getString("PVO_APIkey");
  PVO_ID = preferences.getInt("PVO_ID");
#endif
  preferences.end();
  RetryAfterCount = RetryAfter;

  WiFi.onEvent(WiFiEvent);

  // Start the Ethernet, revision 7+ uses RTL8201
  ETH.begin(0, -1, 16, 17, ETH_PHY_RTL8201);

  // Init and get the time
  configTzTime("CET-1CEST,M3.5.0/02,M10.5.0/03", NTPServer.c_str());

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

#ifdef PVOUTPUT
  // PVOutput buffer
  server.on("/PVObuffer", handlePVObuffer);

#endif
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
  P.setIntensity(Intensity);

  // Matrix font width
#if WIDTH <= 4
  P.setFont(Font8S);
#else
  P.setFont(Font8L);
#endif
  digitsW = (WIDTH * 8 - P.getTextColumns(". kW")) / (P.getTextColumns("8") + 1);
  digitsWh = (WIDTH * 8 - P.getTextColumns(". kWh")) / (P.getTextColumns("8") + 1);
#if WIDTH == 5
  P.setFont(Font8S);
#endif
  digitsWhd = (WIDTH * 8 - P.getTextColumns(". kWh/d")) / (P.getTextColumns("8") + 1);

  // Matrix zones
#if LINES == 2
  P.setZone(0, 0, WIDTH - 1);
  P.setZone(1, WIDTH, MAX_DEVICES - 1);
#endif

  // Matrix intensity
  P.setIntensity(Intensity);

  cycle = ARRAY_SIZE(Show) - 1;
  for (uint8_t i = 0; i < MB_COUNT; i++) {
    M[i] = Modbus();
  }
}

void requestAll (uint8_t Element) {
  uint8_t i, MB_EM;

  Count = 0;
  Sum = 0;
  for (i = 0; i < MBcount; i++) {
    MB_EM = MB[i].EM;
    switch (Element) {
      case SHOW_POWER:
        if (i == 0) {
          RetryAfterCount = 0;
          RetryErrorCount++;
        }
        M[i].readInputRequest(MB[i].IP, MB[i].Unit, EM[MB_EM].Function, EM[MB_EM].PowerRegister, M[i].getDataTypeLength(EM[MB_EM].PowerDataType) / 2);
        debugD("Modbus %u request power", i);
        break;
      case SHOW_ENERGY:
        M[i].readInputRequest(MB[i].IP, MB[i].Unit, EM[MB_EM].Function, EM[MB_EM].EnergyRegister, M[i].getDataTypeLength(EM[MB_EM].EnergyDataType) / 2);
        debugD("Modbus %u request energy", i);
        break;
      case SHOW_TIME:
        Count++;
        break;
      case SHOW_ENERGY_DAY:
        M[i].readInputRequest(MB[i].IP, MB[i].Unit, EM[MB_EM].Function, EM[MB_EM].EnergyDayRegister, M[i].getDataTypeLength(EM[MB_EM].EnergyDayDataType) / 2);
        debugD("Modbus %u request daily energy", i);
        break;
#ifdef PVOUTPUT
      default:
        if (MB_EM < INVERTER_COUNT) {
          switch (Element) {
            case REQUEST_Temp:
              if (Inv[MB_EM].TemperatureRegister > 0) {
                M[i].readInputRequest(MB[i].IP, MB[i].Unit, EM[MB_EM].Function, Inv[MB_EM].TemperatureRegister, M[i].getDataTypeLength(Inv[MB_EM].TemperatureDataType) / 2);
                debugD("Modbus %u request temperature", i);
              } else Count++;
              break;
            case REQUEST_DC1V:
              if (Inv[MB_EM].MPPT1VoltageRegister > 0) {
                M[i].readInputRequest(MB[i].IP, MB[i].Unit, EM[MB_EM].Function, Inv[MB_EM].MPPT1VoltageRegister, M[i].getDataTypeLength(Inv[MB_EM].MPPTVoltageDataType) / 2);
                debugD("Modbus %u request MPPT 1 voltage", i);
              } else Count++;
              break;
            case REQUEST_DC1A:
              if (Inv[MB_EM].MPPT1CurrentRegister > 0) {
                M[i].readInputRequest(MB[i].IP, MB[i].Unit, EM[MB_EM].Function, Inv[MB_EM].MPPT1CurrentRegister, M[i].getDataTypeLength(Inv[MB_EM].MPPTCurrentDataType) / 2);
                debugD("Modbus %u request MPPT 1 current", i);
              } else Count++;
              break;
            case REQUEST_DC2V:
              if (Inv[MB_EM].MPPT2VoltageRegister > 0) {
                M[i].readInputRequest(MB[i].IP, MB[i].Unit, EM[MB_EM].Function, Inv[MB_EM].MPPT2VoltageRegister, M[i].getDataTypeLength(Inv[MB_EM].MPPTVoltageDataType) / 2);
                debugD("Modbus %u request MPPT 2 voltage", i);
              } else Count++;
              break;
            case REQUEST_DC2A:
              if (Inv[MB_EM].MPPT2CurrentRegister > 0) {
                M[i].readInputRequest(MB[i].IP, MB[i].Unit, EM[MB_EM].Function, Inv[MB_EM].MPPT2CurrentRegister, M[i].getDataTypeLength(Inv[MB_EM].MPPTCurrentDataType) / 2);
                debugD("Modbus %u request MPPT 2 current", i);
              } else Count++;
              break;
          }
        } else Count++;
        break;
#endif
    }
  }
}

uint8_t getNextElement (void) {
  uint8_t i;

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
    return (SHOW_POWER);
  } else {
    return(Show[cycle].Element);
  }
}

void PVview() {
  uint8_t Element = 255, l = 0;

  // Wait INTERVAL (a bit earlier for modbus request)
  if (millis() - timer > ((uint16_t)Interval * 1000) - ((LINES + 1) * 400)) {
    timer = millis();

#ifndef SPLIT_LINE
    for (l = 0; l < LINES; l++) {
#endif
      Request[RequestIn] = getNextElement();
      // Avoid duplicate requests with multiple lines
      if (Request[RequestIn] != Element && (Request[RequestIn] == SHOW_POWER || Power)) {
        Element = Request[RequestIn];
        increase(RequestIn, REQUEST_BUFFER_SIZE);
      }
      Cycles[l] = cycle;
#ifndef SPLIT_LINE
    }
#endif
  }

  // Process request queue
  if (RequestIn != RequestOut) {
    // Request next when idle (Count = 255)
    if (Count == 255) {
      RequestTimer = millis();
      requestAll(Request[RequestOut]);
    }
    // Go to next item when received last one
    if (Count == MBcount) {
      Count = 255;
      increase(RequestOut, REQUEST_BUFFER_SIZE);
    }
    // Discard requests on timeout
    else if ((millis() - RequestTimer) > 2000) {
      Count = 255;
      RequestOut = RequestIn;
      debugW("Discard all requests because of no modbus response");
    }
  }

  checkTime();
}

void display() {
  uint8_t l;
#ifdef SPLIT_LINE
  float value;
  String unit;
#endif

  if (P.displayAnimate()) {
    // Sync timer with display
    timer = millis();

    // Show new element
    for (l = 0; l < LINES; l++) strcpy(message[l], "");
#ifdef SPLIT_LINE
    l = 0;
    unit = "";
#else
    for(l = 0; l < LINES; l++) {
#endif

      if(Show[Cycles[l]].When == ALWAYS || (Show[Cycles[l]].When == ON_POWER && Power > 0)) {
#ifdef SPLIT_LINE
        P.setFont(0, Font8S);
        P.setFont(1, Font8L);
        switch(Show[Cycles[0]].Element) {
          case SHOW_POWER:
            value = Power;
            unit = "W";
            break;
          case SHOW_ENERGY:
            value = Energy;
            unit = "Wh";
            break;
          case SHOW_ENERGY_DAY:
            value = EnergyDay;
            unit = "Wh/d";
            P.setFont(1, Font8S);
            break;
          case SHOW_TIME:
            printTime(message[0]);
            break;
        }
#else
#if WIDTH == 5
        P.setFont(l, Font8L);
#endif
        switch(Show[Cycles[l]].Element) {
          case SHOW_POWER:
            printModbus(message[l], Power, "W", digitsW);
            break;
          case SHOW_ENERGY:
#if WIDTH == 4
            printModbus(message[l], Energy, "wh", digitsWh);
#else
            printModbus(message[l], Energy, "Wh", digitsWh);
#endif
            break;
          case SHOW_ENERGY_DAY:
#if WIDTH == 5
            P.setFont(l, Font8S);
#endif
            printModbus(message[l], EnergyDay, "Wh/d", digitsWhd);
            break;
          case SHOW_TIME:
            printTime(message[l]);
            break;
        }
#endif
      }

#ifdef SPLIT_LINE
      if (unit != "") {
        printModbus(message[0], value, "", 4);
        prefixUnit(value, unit, 4, SmallNumbers);
        sprintf(message[1], "%s", unit);
      }
#else
    }
#endif
    for(l = 0; l < LINES; l++) {
      debugV("Display line %u: %s", l, message[l]);
      P.displayZoneText(l, message[l], Align[Show[Cycles[l]].Align], 0, (uint16_t)Interval * 1000, PA_NO_EFFECT, PA_NO_EFFECT);
    }
  }
}

void loop() {
  // Check ethernet status
  switch (eth_status) {
    case ETH_GOT_IP:
      // Handle http connection
      server.handleClient();
      // Handle modbus response
      readModbus();
      // Main
      PVview();
      break;
    case ETH_CONNECTED:
      strcpy(message[0], "No IP");
      break;
    case ETH_DISCONNECTED:
      strcpy(message[0], "No ETH");
      break;
  }

  // Show
  display();

  // Console
  Debug.handle();
}
