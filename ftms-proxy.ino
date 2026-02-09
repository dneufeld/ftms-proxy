// FTMS Gradient Proxy for ESP32
//
// Sits between Zwift and a Tacx Neo 3M trainer, intercepting FTMS gradient
// commands and scaling negative (descent) grades by a configurable factor.
// Climbs and all other commands pass through untouched.

#include <NimBLEDevice.h>
#include "ftms_defs.h"
#include "gradient_filter.h"
#include "ble_central.h"
#include "ble_peripheral.h"

// --- Configuration ---
#define DESCENT_SCALE_FACTOR  0.5    // Scale descents to 50% of actual grade
#define TARGET_DEVICE_NAME    "Tacx"  // Substring match for trainer name

float g_descent_scale_factor = DESCENT_SCALE_FACTOR;

static bool s_peripheral_started = false;

void setup() {
    Serial.begin(115200);
    delay(1000); // Let serial settle

    Serial.println("=================================");
    Serial.println("  FTMS Gradient Proxy v1.0");
    Serial.printf("  Descent scale: %.0f%%\n", g_descent_scale_factor * 100);
    Serial.printf("  Target device: %s\n", TARGET_DEVICE_NAME);
    Serial.println("=================================");

    // Initialize NimBLE with enough memory for both roles
    NimBLEDevice::init("FTMS Proxy");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max TX power for reliable connection
    NimBLEDevice::setMTU(256);

    // Start scanning for the trainer (central role)
    central_init(TARGET_DEVICE_NAME);
}

void loop() {
    // Run central state machine (scan, connect, reconnect)
    central_loop();

    // Once trainer is connected and cache is populated, start peripheral
    if (g_trainer_connected && g_trainer_cache.valid && !s_peripheral_started) {
        Serial.println("[Main] Trainer connected, starting peripheral...");
        peripheral_init();
        s_peripheral_started = true;
    }

    // Status LED / heartbeat logging
    static unsigned long last_status = 0;
    unsigned long now = millis();
    if (now - last_status > 30000) { // Every 30s
        last_status = now;
        Serial.printf("[Status] Trainer: %s | Zwift: %s | Uptime: %lus\n",
                      g_trainer_connected ? "connected" : "disconnected",
                      g_zwift_connected ? "connected" : "disconnected",
                      now / 1000);
    }

    delay(10); // Don't spin too hard
}
