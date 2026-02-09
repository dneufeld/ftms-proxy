#ifndef FTMS_DEFS_H
#define FTMS_DEFS_H

// FTMS Service UUID
#define FTMS_SERVICE_UUID           "1826"

// FTMS Characteristic UUIDs
#define FTMS_INDOOR_BIKE_DATA_UUID  "2AD2"
#define FTMS_CONTROL_POINT_UUID     "2AD9"
#define FTMS_FEATURE_UUID           "2ACC"
#define FTMS_STATUS_UUID            "2AD3"
#define FTMS_RESISTANCE_RANGE_UUID  "2AD6"
#define FTMS_POWER_RANGE_UUID       "2AD8"

// FTMS Control Point opcodes
#define FTMS_OP_REQUEST_CONTROL     0x00
#define FTMS_OP_RESET               0x01
#define FTMS_OP_SET_TARGET_POWER    0x05
#define FTMS_OP_SET_INDOOR_BIKE_SIM 0x11
#define FTMS_OP_RESPONSE_CODE       0x80

// FTMS Control Point result codes
#define FTMS_RESULT_SUCCESS         0x01
#define FTMS_RESULT_NOT_SUPPORTED   0x02
#define FTMS_RESULT_INVALID_PARAM   0x03
#define FTMS_RESULT_OPERATION_FAIL  0x04
#define FTMS_RESULT_NOT_PERMITTED   0x05

// Indoor Bike Simulation Parameters packet layout (opcode 0x11)
// Byte 0:   Opcode (0x11)
// Byte 1-2: Wind Speed (sint16, units 0.001 m/s)
// Byte 3-4: Grade (sint16, units 0.01%)
// Byte 5:   Rolling Resistance Coefficient (uint8, units 0.0001)
// Byte 6:   Wind Resistance Coefficient (uint8, units 0.01 kg/m)
#define SIM_PARAMS_LENGTH           7
#define SIM_PARAMS_GRADE_OFFSET     3

#endif // FTMS_DEFS_H
