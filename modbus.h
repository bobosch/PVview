#ifndef _MODBUS_H_
#define _MODBUS_H_

#include "Arduino.h"
#include <ETH.h>

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

class Modbus {
  public:
    Modbus();
    void readInputRequest(const char *ip, uint8_t unit, uint8_t function, unsigned int reg);
    int available(void);
    void read(void);
    unsigned int getTransactionID(void);
    float getValue(MBEndianess endianness, MBDataType dataType);

  private:
    float combineBytes(unsigned char *buf, unsigned char pos, MBEndianess endianness, MBDataType dataType);

    uint8_t rx[13];
    unsigned int transactionID;
    WiFiClient client;
    union mb_union {
        unsigned char c[4];
        signed long l;
        float f;
        signed int i[2];
    } MBUnion;
};

#endif
