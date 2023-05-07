#include <ETH.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include <SPI.h>

#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include "PVfont.h"
#include "modbus.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 5

#define INTERVAL 10 // seconds

const char prefixes[] = " kMGTPEZYRQ";

bool eth_connected = false;
unsigned long timer = 0;

MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

WebServer server(80);

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
  float power;
  uint8_t exponent, point, thousand;
  char message[12], prefix = 32; // " "

  if (eth_connected) {
    server.handleClient();

    if (timer < millis()) {
      timer = millis() + (INTERVAL * 1000);
      strcpy(message, "");
      ModbusReadInputRequest();
    }

    if (ModbusAvailable()) {
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
