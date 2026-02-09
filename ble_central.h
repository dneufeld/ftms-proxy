#ifndef BLE_CENTRAL_H
#define BLE_CENTRAL_H

#include <NimBLEDevice.h>

// Cached characteristic values read from the trainer
struct TrainerCharCache {
    uint8_t feature[32];
    size_t  feature_len;
    uint8_t resistance_range[32];
    size_t  resistance_range_len;
    uint8_t power_range[32];
    size_t  power_range_len;
    bool    valid;
};

extern TrainerCharCache g_trainer_cache;
extern bool g_trainer_connected;

// Initialize central role and start scanning
void central_init(const char* target_name);

// Run central state machine (call from loop())
void central_loop();

// Forward a control point write to the trainer
bool central_write_control_point(const uint8_t* data, size_t len);

// Get RSSI of the trainer connection (returns 0 if not connected)
int central_get_rssi();

#endif // BLE_CENTRAL_H
