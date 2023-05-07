#include <ETH.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include <SPI.h>

#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include "PVfont.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 5

#define INTERVAL 10 // seconds

typedef enum mb_endianess {
  MB_ENDIANESS_LBF_LWF = 0,
  MB_ENDIANESS_LBF_HWF = 1,
  MB_ENDIANESS_HBF_LWF = 2,
  MB_ENDIANESS_HBF_HWF = 3,
} MBEndianess;

typedef enum mb_datatype {
  MB_DATATYPE_INT32 = 0,
  MB_DATATYPE_FLOAT32 = 1,
  MB_DATATYPE_INT16 = 2,
} MBDataType;

union mb_union {
    unsigned char c[4];
    signed long l;
    float f;
    signed int i[2];
} MBUnion;

const byte request[] = {0x0a, 0x87, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x9c, 0x9b, 0x00, 0x02}; // Device 1 Function 03 Address 40091 -> Datatype 32bit float Big Endian
const char prefixes[] = " kMGTPEZYRQ";

bool eth_connected = false;
uint8_t exponent = 1;
unsigned long timer = 0;

MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

WebServer server(80);

WiFiClient client;

/*
uint8_t	curString = 0;
const char *msg[] =
{
  "Parola for",
  "Arduino", 
  "LED Matrix",
  "Display" 
};
#define NEXT_STRING ((curString + 1) % ARRAY_SIZE(msg))
*/

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

/**
 * Combine Bytes received over modbus
 * 
 * @param pointer to buf
 * @param unsigned char pos
 * @param unsigned char endianness:
 *        0: low byte first, low word first (little endian)
 *        1: low byte first, high word first
 *        2: high byte first, low word first
 *        3: high byte first, high word first (big endian)
 * @param MBDataType dataType: used to determine how many bytes should be combined
 */
float combineBytes(unsigned char *buf, unsigned char pos, MBEndianess endianness, MBDataType dataType) {
    // ESP32 is little endian
    switch(endianness) {
        case MB_ENDIANESS_LBF_LWF: // low byte first, low word first (little endian)
            MBUnion.c[0] = (unsigned char)buf[pos + 0];
            MBUnion.c[1] = (unsigned char)buf[pos + 1];
            MBUnion.c[2] = (unsigned char)buf[pos + 2];
            MBUnion.c[3] = (unsigned char)buf[pos + 3];
            break;
        case MB_ENDIANESS_LBF_HWF: // low byte first, high word first
            MBUnion.c[0] = (unsigned char)buf[pos + 2];
            MBUnion.c[1] = (unsigned char)buf[pos + 3];
            MBUnion.c[2] = (unsigned char)buf[pos + 0];
            MBUnion.c[3] = (unsigned char)buf[pos + 1];
            break;
        case MB_ENDIANESS_HBF_LWF: // high byte first, low word first
            MBUnion.c[0] = (unsigned char)buf[pos + 1];
            MBUnion.c[1] = (unsigned char)buf[pos + 0];
            MBUnion.c[2] = (unsigned char)buf[pos + 3];
            MBUnion.c[3] = (unsigned char)buf[pos + 2];
            break;
        case MB_ENDIANESS_HBF_HWF: // high byte first, high word first (big endian)
            MBUnion.c[0] = (unsigned char)buf[pos + 3];
            MBUnion.c[1] = (unsigned char)buf[pos + 2];
            MBUnion.c[2] = (unsigned char)buf[pos + 1];
            MBUnion.c[3] = (unsigned char)buf[pos + 0];
            break;
        default:
            break;
    }

    switch(dataType) {
      case MB_DATATYPE_INT16:
        return (float)MBUnion.i[0];
      case MB_DATATYPE_INT32:
        return (float)MBUnion.l;
      case MB_DATATYPE_FLOAT32:
        return MBUnion.f;
    }
}

void ModbusReadInputRequest(void) {
  if (client.connected()) client.stop();

  if (client.connect("192.168.1.4", 502)) {
    client.write(request, 12);
  }
}

float ModbusGetValue(void) {
  uint8_t data[13];

  client.read(data, 13); // 9 bytes header, 4 bytes data
  client.stop();

  return combineBytes(data, 9, MB_ENDIANESS_HBF_HWF, MB_DATATYPE_FLOAT32);
}

void setup() {
  WiFi.onEvent(WiFiEvent);

  // Start the Ethernet, revision 7+ uses RTL8201
  ETH.begin(0, -1, 16, 17, ETH_PHY_RTL8201);
  // You can browse to wesp32demo.local with this
  MDNS.begin("wesp32demo");

  // Bind HTTP handler
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  // Start the Ethernet web server
  server.begin();
  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  // Matrix display
  P.begin();
  P.setFont(PVfont);
}

void loop() {
  float power = 0;
  uint8_t point = 0, thousand;
  char message[12], prefix = 32; // " "

  if (eth_connected) {
    server.handleClient();

    if (timer < millis()) {
      timer = millis() + (INTERVAL * 1000);
      strcpy(message, "");
      ModbusReadInputRequest();
    }

    if (client.available()) {
      power = ModbusGetValue();
      if(power > 0) {
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
    }
  } else {
    strcpy(message, "No ETH");
  }

  if (P.displayAnimate()) {
    timer = millis() + (INTERVAL * 1000) - 500;
    //P.print(message);
    P.displayText(message, PA_RIGHT, 0, INTERVAL * 1000, PA_NO_EFFECT, PA_NO_EFFECT);
  }
}
