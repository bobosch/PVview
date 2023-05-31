#include "modbus.h"

Modbus::Modbus() {
}

uint8_t Modbus::getDataTypeLength(MBDataType dataType) {
  switch (dataType) {
    case MB_UINT16:
    case MB_SINT16:
      return 2;
    case MB_UINT32:
    case MB_SINT32:
    case MB_FLOAT32:
      return 4;
    case MB_UINT64:
    case MB_SINT64:
    case MB_FLOAT64:
      return 8;
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
* @param MBDataType dataType
*/
float Modbus::combineBytes(unsigned char *buf, unsigned char pos, MBEndianess endianness, MBDataType dataType) {
  // ESP32 is little endian
  switch (getDataTypeLength(dataType)) {
    case 2:
      switch(endianness) {
        case MB_LBF_LWF: // low byte first, low word first (little endian)
        case MB_LBF_HWF: // low byte first, high word first
          Modbus::MBUnion16.c[0] = (unsigned char)buf[pos + 0];
          Modbus::MBUnion16.c[1] = (unsigned char)buf[pos + 1];
          break;
        case MB_HBF_LWF: // high byte first, low word first
        case MB_HBF_HWF: // high byte first, high word first (big endian)
          Modbus::MBUnion16.c[0] = (unsigned char)buf[pos + 1];
          Modbus::MBUnion16.c[1] = (unsigned char)buf[pos + 0];
          break;
        default:
          break;
      }
      break;
    case 4:
      switch(endianness) {
        case MB_LBF_LWF: // low byte first, low word first (little endian)
          Modbus::MBUnion32.c[0] = (unsigned char)buf[pos + 0];
          Modbus::MBUnion32.c[1] = (unsigned char)buf[pos + 1];
          Modbus::MBUnion32.c[2] = (unsigned char)buf[pos + 2];
          Modbus::MBUnion32.c[3] = (unsigned char)buf[pos + 3];
          break;
        case MB_LBF_HWF: // low byte first, high word first
          Modbus::MBUnion32.c[0] = (unsigned char)buf[pos + 2];
          Modbus::MBUnion32.c[1] = (unsigned char)buf[pos + 3];
          Modbus::MBUnion32.c[2] = (unsigned char)buf[pos + 0];
          Modbus::MBUnion32.c[3] = (unsigned char)buf[pos + 1];
          break;
        case MB_HBF_LWF: // high byte first, low word first
          Modbus::MBUnion32.c[0] = (unsigned char)buf[pos + 1];
          Modbus::MBUnion32.c[1] = (unsigned char)buf[pos + 0];
          Modbus::MBUnion32.c[2] = (unsigned char)buf[pos + 3];
          Modbus::MBUnion32.c[3] = (unsigned char)buf[pos + 2];
          break;
        case MB_HBF_HWF: // high byte first, high word first (big endian)
          Modbus::MBUnion32.c[0] = (unsigned char)buf[pos + 3];
          Modbus::MBUnion32.c[1] = (unsigned char)buf[pos + 2];
          Modbus::MBUnion32.c[2] = (unsigned char)buf[pos + 1];
          Modbus::MBUnion32.c[3] = (unsigned char)buf[pos + 0];
          break;
        default:
          break;
      }
      break;
    case 8:
      switch(endianness) {
        case MB_LBF_LWF: // low byte first, low word first (little endian)
          Modbus::MBUnion64.c[0] = (unsigned char)buf[pos + 0];
          Modbus::MBUnion64.c[1] = (unsigned char)buf[pos + 1];
          Modbus::MBUnion64.c[2] = (unsigned char)buf[pos + 2];
          Modbus::MBUnion64.c[3] = (unsigned char)buf[pos + 3];
          Modbus::MBUnion64.c[4] = (unsigned char)buf[pos + 4];
          Modbus::MBUnion64.c[5] = (unsigned char)buf[pos + 5];
          Modbus::MBUnion64.c[6] = (unsigned char)buf[pos + 6];
          Modbus::MBUnion64.c[7] = (unsigned char)buf[pos + 7];
          break;
        case MB_LBF_HWF: // low byte first, high word first
          Modbus::MBUnion64.c[0] = (unsigned char)buf[pos + 6];
          Modbus::MBUnion64.c[1] = (unsigned char)buf[pos + 7];
          Modbus::MBUnion64.c[2] = (unsigned char)buf[pos + 4];
          Modbus::MBUnion64.c[3] = (unsigned char)buf[pos + 5];
          Modbus::MBUnion64.c[4] = (unsigned char)buf[pos + 2];
          Modbus::MBUnion64.c[5] = (unsigned char)buf[pos + 3];
          Modbus::MBUnion64.c[6] = (unsigned char)buf[pos + 0];
          Modbus::MBUnion64.c[7] = (unsigned char)buf[pos + 1];
          break;
        case MB_HBF_LWF: // high byte first, low word first
          Modbus::MBUnion64.c[0] = (unsigned char)buf[pos + 1];
          Modbus::MBUnion64.c[1] = (unsigned char)buf[pos + 0];
          Modbus::MBUnion64.c[2] = (unsigned char)buf[pos + 3];
          Modbus::MBUnion64.c[3] = (unsigned char)buf[pos + 2];
          Modbus::MBUnion64.c[4] = (unsigned char)buf[pos + 5];
          Modbus::MBUnion64.c[5] = (unsigned char)buf[pos + 4];
          Modbus::MBUnion64.c[6] = (unsigned char)buf[pos + 7];
          Modbus::MBUnion64.c[7] = (unsigned char)buf[pos + 6];
          break;
        case MB_HBF_HWF: // high byte first, high word first (big endian)
          Modbus::MBUnion64.c[0] = (unsigned char)buf[pos + 7];
          Modbus::MBUnion64.c[1] = (unsigned char)buf[pos + 6];
          Modbus::MBUnion64.c[2] = (unsigned char)buf[pos + 5];
          Modbus::MBUnion64.c[3] = (unsigned char)buf[pos + 4];
          Modbus::MBUnion64.c[4] = (unsigned char)buf[pos + 3];
          Modbus::MBUnion64.c[5] = (unsigned char)buf[pos + 2];
          Modbus::MBUnion64.c[6] = (unsigned char)buf[pos + 1];
          Modbus::MBUnion64.c[7] = (unsigned char)buf[pos + 0];
          break;
        default:
          break;
      }
      break;
  }

  switch(dataType) {
    case MB_UINT16:
      return (float)Modbus::MBUnion16.u;
    case MB_SINT16:
      return (float)Modbus::MBUnion16.s;
    case MB_UINT32:
      return (float)Modbus::MBUnion32.u;
    case MB_SINT32:
      return (float)Modbus::MBUnion32.s;
    case MB_FLOAT32:
      return Modbus::MBUnion32.f;
    case MB_UINT64:
      return (float)Modbus::MBUnion64.u;
    case MB_SINT64:
      return (float)Modbus::MBUnion64.s;
    case MB_FLOAT64:
      return (float)Modbus::MBUnion64.f;
  }
}

void Modbus::readInputRequest(const char *ip, uint8_t unit, uint8_t function, uint16_t reg, uint8_t length) {
  uint8_t n = 0, tx[12];

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
  tx[n++] = length;

  if (client.connected()) client.stop();

  if (client.connect(ip, 502)) {
    client.write(tx, 12);
  }
}

int Modbus::available(void) {
  return client.available();
}

void Modbus::read(void) {
  uint16_t MessageLength;

  client.read(Modbus::rx, 9);
  Modbus::transactionID = (uint16_t)combineBytes(Modbus::rx, 0, MB_HBF_LWF, MB_UINT16); // 2 bytes Transaction ID
                                                                                        // 2 bytes Protocol Identifier
  MessageLength =         (uint16_t)combineBytes(Modbus::rx, 4, MB_HBF_LWF, MB_UINT16); // 2 bytes Message Length
                                                                                        // 1 byte  Unit Identifier
                                                                                        // 1 byte  Function Code
                                                                                        // 1 byte  Byte Count
  if ((MessageLength - 3) <= 9) {
    client.read(Modbus::rx, MessageLength - 3);
    Modbus::error = false;
  } else {
    Modbus::error = true;
  }
  client.stop();
}

unsigned int Modbus::getTransactionID(void) {
  return Modbus::transactionID;
}

float Modbus::getValue(MBEndianess endianness, MBDataType dataType, signed char Multiplier) {
  if(Modbus::error) {
    return NAN;
  } else {
    return combineBytes(Modbus::rx, 0, endianness, dataType) * pow10(Multiplier);
  }
}
