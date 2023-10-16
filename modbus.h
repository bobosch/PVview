#ifndef _MODBUS_H_
#define _MODBUS_H_

#include "Arduino.h"
#include <ETH.h>

typedef enum mb_endianess {
  MB_LBF_LWF = 0,
  MB_LBF_HWF = 1,
  MB_HBF_LWF = 2,
  MB_HBF_HWF = 3,
} MBEndianess;

typedef enum mb_datatype {
  MB_UINT16 = 0,
  MB_SINT16 = 1,
  MB_UINT32 = 2,
  MB_SINT32 = 3,
  MB_FLOAT32 = 4,
  MB_UINT64 = 5,
  MB_SINT64 = 6,
  MB_FLOAT64 = 7,
} MBDataType;

class Modbus {
  public:
    Modbus();
    uint8_t getDataTypeLength(MBDataType dataType);
    void readInputRequest(IPAddress ip, uint8_t unit, uint8_t function, uint16_t reg, uint8_t length);
    int available(void);
    void read(void);
    unsigned int getTransactionID(void);
    float getValue(MBEndianess endianness, MBDataType dataType, signed char Multiplier);

  private:
    float combineBytes(unsigned char *buf, unsigned char pos, MBEndianess endianness, MBDataType dataType);

    bool error;
    uint8_t rx[9];
    unsigned int transactionID;
    WiFiClient client;
    union mb_union16 {
        uint8_t c[2];
        uint16_t u;
        int16_t s;
    } MBUnion16;
    union mb_union32 {
        uint8_t c[4];
        uint32_t u;
        int32_t s;
        float f;
    } MBUnion32;
    union mb_union64 {
        uint8_t c[8];
        uint64_t u;
        int64_t s;
        double f;
    } MBUnion64;
};

#endif
