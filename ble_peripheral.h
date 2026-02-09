#ifndef BLE_PERIPHERAL_H
#define BLE_PERIPHERAL_H

#include <NimBLEDevice.h>

extern bool g_zwift_connected;

// Initialize peripheral role (call after central_init, once trainer cache is valid)
void peripheral_init();

// Notify Zwift with indoor bike data from the trainer
void peripheral_notify_bike_data(const uint8_t* data, size_t len);

// Notify Zwift with fitness machine status from the trainer
void peripheral_notify_status(const uint8_t* data, size_t len);

#endif // BLE_PERIPHERAL_H
