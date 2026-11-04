#pragma once
#include <NimBLEDevice.h>
#include <atomic>
extern std::atomic<bool> g_zwift_connected;
void peripheral_init();
void peripheral_loop();
void peripheral_trainer_lost();
void peripheral_notify_bike_data(const uint8_t* data, size_t len);
void peripheral_notify_status(const uint8_t* data, size_t len);
void peripheral_notify_training(const uint8_t* data, size_t len);
void peripheral_control_response(const uint8_t* data, size_t len);
