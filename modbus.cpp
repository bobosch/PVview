#include "modbus.h"
#include <ETH.h>

WiFiClient client;

union mb_union {
    unsigned char c[4];
    signed long l;
    float f;
    signed int i[2];
} MBUnion;

unsigned int transaction = 0;

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
 * @param MBDataType dataType
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

void ModbusReadInputRequest(const char *ip, uint8_t unit, uint8_t function, unsigned int reg) {
  uint8_t data[12];
  unsigned int n = 0;

  data[n++] = (uint8_t)(transaction>>8); // Transaction Identifier
  data[n++] = (uint8_t)transaction++;
  data[n++] = 0;                         // Protocol Identifier
  data[n++] = 0;
  data[n++] = 0;                         // Message Length
  data[n++] = 6;
  data[n++] = unit;                      // Unit Identifier
  data[n++] = function;                  // Function Code
  data[n++] = (uint8_t)(reg>>8);         // Data Address of first register
  data[n++] = (uint8_t)reg;
  data[n++] = 0;                         // Total number of registers
  data[n++] = 2;

  if (client.connected()) client.stop();

  if (client.connect(ip, 502)) {
    client.write(data, 12);
  }
}

int ModbusAvailable(void) {
  return client.available();
}

float ModbusGetValue(MBEndianess endianness, MBDataType dataType) {
  uint8_t data[13];

  client.read(data, 13); // 9 bytes header, 4 bytes data
  client.stop();

  return combineBytes(data, 9, endianness, dataType);
}
