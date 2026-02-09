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
#include "proxy_stats.h"

// --- Configuration ---
#define DESCENT_SCALE_FACTOR  0.5    // Scale descents to 50% of actual grade
#define TARGET_DEVICE_NAME    "Tacx"  // Substring match for trainer name

float g_descent_scale_factor = DESCENT_SCALE_FACTOR;
ProxyStats g_stats = {};

static bool s_peripheral_started = false;
static char s_serial_buf[64];
static uint8_t s_serial_pos = 0;

// --- Serial command processing ---

static void process_serial_command(const char* cmd) {
    if (strncmp(cmd, "scale ", 6) == 0) {
        float val = atof(cmd + 6);
        if (val >= 0.0 && val <= 2.0) {
            g_descent_scale_factor = val;
            Serial.printf("[Config] Descent scale set to %.2f (%.0f%%)\n", val, val * 100);
        } else {
            Serial.println("[Config] Invalid scale. Range: 0.0 - 2.0");
        }
    } else if (strcmp(cmd, "stats") == 0) {
        Serial.println("[Stats] --- Proxy Statistics ---");
        Serial.printf("  Packets forwarded:   %lu\n", (unsigned long)g_stats.packets_forwarded);
        Serial.printf("  Grades modified:     %lu\n", (unsigned long)g_stats.grades_modified);
        Serial.printf("  Write errors:        %lu\n", (unsigned long)g_stats.write_errors);
        Serial.printf("  Notifications:       %lu\n", (unsigned long)g_stats.notifications_relayed);
        Serial.printf("  Trainer connects:    %lu\n", (unsigned long)g_stats.trainer_connects);
        Serial.printf("  Trainer disconnects: %lu\n", (unsigned long)g_stats.trainer_disconnects);
        Serial.printf("  Zwift connects:      %lu\n", (unsigned long)g_stats.zwift_connects);
        Serial.printf("  Zwift disconnects:   %lu\n", (unsigned long)g_stats.zwift_disconnects);
        Serial.printf("  Free heap:           %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    } else if (strcmp(cmd, "help") == 0) {
        Serial.println("[Help] Commands:");
        Serial.println("  scale <0.0-2.0>  — Set descent scale factor");
        Serial.println("  stats            — Show statistics");
        Serial.println("  help             — Show this message");
    } else {
        Serial.printf("[Config] Unknown command: '%s' (try 'help')\n", cmd);
    }
}

static void poll_serial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_serial_pos > 0) {
                s_serial_buf[s_serial_pos] = '\0';
                process_serial_command(s_serial_buf);
                s_serial_pos = 0;
            }
        } else if (s_serial_pos < sizeof(s_serial_buf) - 1) {
            s_serial_buf[s_serial_pos++] = c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Let serial settle

    Serial.println("=================================");
    Serial.println("  FTMS Gradient Proxy v1.1");
    Serial.printf("  Descent scale: %.0f%%\n", g_descent_scale_factor * 100);
    Serial.printf("  Target device: %s\n", TARGET_DEVICE_NAME);
    Serial.println("  Type 'help' for commands");
    Serial.println("=================================");

    // Initialize NimBLE with enough memory for both roles
    NimBLEDevice::init("FTMS Proxy");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max TX power for reliable connection
    NimBLEDevice::setMTU(256);

    // Start scanning for the trainer (central role)
    central_init(TARGET_DEVICE_NAME);
}

void loop() {
    // Process serial commands
    poll_serial();

    // Run central state machine (scan, connect, reconnect)
    central_loop();

    // Once trainer is connected and cache is populated, start peripheral
    if (g_trainer_connected && g_trainer_cache.valid && !s_peripheral_started) {
        Serial.println("[Main] Trainer connected, starting peripheral...");
        peripheral_init();
        s_peripheral_started = true;
    }

    // Status heartbeat
    static unsigned long last_status = 0;
    unsigned long now = millis();
    if (now - last_status > 30000) { // Every 30s
        last_status = now;

        int rssi = central_get_rssi();

        Serial.printf("[Status] Trainer: %s | Zwift: %s | Scale: %.0f%% | "
                      "Fwd: %lu | Mod: %lu | Err: %lu | "
                      "Heap: %lu | RSSI: %d dBm | Up: %lus\n",
                      g_trainer_connected ? "yes" : "no",
                      g_zwift_connected ? "yes" : "no",
                      g_descent_scale_factor * 100,
                      (unsigned long)g_stats.packets_forwarded,
                      (unsigned long)g_stats.grades_modified,
                      (unsigned long)g_stats.write_errors,
                      (unsigned long)ESP.getFreeHeap(),
                      rssi,
                      now / 1000);
    }

    delay(10); // Don't spin too hard
}
