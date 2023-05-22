#include "modbus.h"

Modbus::Modbus() {
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
* @param MBDataType dataType
*/
float Modbus::combineBytes(unsigned char *buf, unsigned char pos, MBEndianess endianness, MBDataType dataType) {
    // ESP32 is little endian
    if (dataType == MB_DATATYPE_INT16) {
        switch(endianness) {
            case MB_ENDIANESS_LBF_LWF: // low byte first, low word first (little endian)
            case MB_ENDIANESS_LBF_HWF: // low byte first, high word first
                Modbus::MBUnion.c[0] = (unsigned char)buf[pos + 0];
                Modbus::MBUnion.c[1] = (unsigned char)buf[pos + 1];
                break;
            case MB_ENDIANESS_HBF_LWF: // high byte first, low word first
            case MB_ENDIANESS_HBF_HWF: // high byte first, high word first (big endian)
                Modbus::MBUnion.c[0] = (unsigned char)buf[pos + 1];
                Modbus::MBUnion.c[1] = (unsigned char)buf[pos + 0];
                break;
            default:
                break;
        }
        Modbus::MBUnion.c[2] = (unsigned char)0;
        Modbus::MBUnion.c[3] = (unsigned char)0;
    } else {
        switch(endianness) {
            case MB_ENDIANESS_LBF_LWF: // low byte first, low word first (little endian)
                Modbus::MBUnion.c[0] = (unsigned char)buf[pos + 0];
                Modbus::MBUnion.c[1] = (unsigned char)buf[pos + 1];
                Modbus::MBUnion.c[2] = (unsigned char)buf[pos + 2];
                Modbus::MBUnion.c[3] = (unsigned char)buf[pos + 3];
                break;
            case MB_ENDIANESS_LBF_HWF: // low byte first, high word first
                Modbus::MBUnion.c[0] = (unsigned char)buf[pos + 2];
                Modbus::MBUnion.c[1] = (unsigned char)buf[pos + 3];
                Modbus::MBUnion.c[2] = (unsigned char)buf[pos + 0];
                Modbus::MBUnion.c[3] = (unsigned char)buf[pos + 1];
                break;
            case MB_ENDIANESS_HBF_LWF: // high byte first, low word first
                Modbus::MBUnion.c[0] = (unsigned char)buf[pos + 1];
                Modbus::MBUnion.c[1] = (unsigned char)buf[pos + 0];
                Modbus::MBUnion.c[2] = (unsigned char)buf[pos + 3];
                Modbus::MBUnion.c[3] = (unsigned char)buf[pos + 2];
                break;
            case MB_ENDIANESS_HBF_HWF: // high byte first, high word first (big endian)
                Modbus::MBUnion.c[0] = (unsigned char)buf[pos + 3];
                Modbus::MBUnion.c[1] = (unsigned char)buf[pos + 2];
                Modbus::MBUnion.c[2] = (unsigned char)buf[pos + 1];
                Modbus::MBUnion.c[3] = (unsigned char)buf[pos + 0];
                break;
            default:
                break;
        }
    }

    switch(dataType) {
      case MB_DATATYPE_INT16:
        return (float)Modbus::MBUnion.i[0];
      case MB_DATATYPE_INT32:
        return (float)Modbus::MBUnion.l;
      case MB_DATATYPE_FLOAT32:
        return Modbus::MBUnion.f;
    }
}

void Modbus::readInputRequest(const char *ip, uint8_t unit, uint8_t function, unsigned int reg) {
  uint8_t tx[12];
  unsigned int n = 0;

  Modbus::transactionID = random(65536);

  tx[n++] = (uint8_t)(Modbus::transactionID>>8); // Transaction Identifier
  tx[n++] = (uint8_t)Modbus::transactionID;
  tx[n++] = 0;                         // Protocol Identifier
  tx[n++] = 0;
  tx[n++] = 0;                         // Message Length
  tx[n++] = 6;
  tx[n++] = unit;                      // Unit Identifier
  tx[n++] = function;                  // Function Code
  tx[n++] = (uint8_t)(reg>>8);         // Data Address of first register
  tx[n++] = (uint8_t)reg;
  tx[n++] = 0;                         // Total number of registers
  tx[n++] = 2;

  if (client.connected()) client.stop();

  if (client.connect(ip, 502)) {
    client.write(tx, 12);
  }
}

int Modbus::available(void) {
  return client.available();
}

void Modbus::read(void) {
  client.read(Modbus::rx, 13); // 9 bytes header, 4 bytes data
  client.stop();

  Modbus::transactionID = (unsigned int)combineBytes(Modbus::rx, 0, MB_ENDIANESS_HBF_LWF, MB_DATATYPE_INT16);
}

unsigned int Modbus::getTransactionID(void) {
  return Modbus::transactionID;
}

float Modbus::getValue(MBEndianess endianness, MBDataType dataType, signed char Multiplier) {
  return combineBytes(Modbus::rx, 9, endianness, dataType) * pow10(Multiplier);
}
