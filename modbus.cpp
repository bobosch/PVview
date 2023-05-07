#include "modbus.h"
#include <ETH.h>

WiFiClient client;

const byte request[] = {0x0a, 0x87, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x9c, 0x9b, 0x00, 0x02}; // Device 1 Function 03 Address 40091 -> Datatype 32bit float Big Endian

union mb_union {
    unsigned char c[4];
    signed long l;
    float f;
    signed int i[2];
} MBUnion;

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

void ModbusReadInputRequest(void) {
  if (client.connected()) client.stop();

  if (client.connect("192.168.1.4", 502)) {
    client.write(request, 12);
  }
}

int ModbusAvailable(void) {
  return client.available();
}

float ModbusGetValue(void) {
  uint8_t data[13];

  client.read(data, 13); // 9 bytes header, 4 bytes data
  client.stop();

  return combineBytes(data, 9, MB_ENDIANESS_HBF_HWF, MB_DATATYPE_FLOAT32);
}
