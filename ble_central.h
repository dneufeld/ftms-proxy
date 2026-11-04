#pragma once
#include <NimBLEDevice.h>
#include <atomic>
struct CachedValue { uint8_t bytes[32] = {}; size_t len = 0; };
struct TrainerCharCache {
    CachedValue feature, resistance, power, inclination, heart, training;
    bool valid = false;
};
extern TrainerCharCache g_trainer_cache;
extern std::atomic<bool> g_trainer_connected;
void central_init();
void central_loop();
bool central_write_control_point(const uint8_t* data, size_t len);
void central_disconnect();
int central_get_rssi();
