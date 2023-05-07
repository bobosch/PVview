#ifndef _MODBUS_H_
#define _MODBUS_H_

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

void ModbusReadInputRequest(void);
int ModbusAvailable(void);
float ModbusGetValue(void);

#endif
