#include "ble_peripheral.h"
#include "ble_central.h"
#include "ftms_defs.h"
#include "gradient_filter.h"
#include "proxy_stats.h"
#include <Arduino.h>

// Defined in main sketch
extern float g_descent_scale_factor;

bool g_zwift_connected = false;

static NimBLEServer* s_server = nullptr;
static NimBLECharacteristic* s_bike_data_char = nullptr;
static NimBLECharacteristic* s_control_point_char = nullptr;
static NimBLECharacteristic* s_status_char = nullptr;

// --- Server callbacks ---

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        Serial.printf("[Peripheral] Client connected: %s\n",
                      connInfo.getAddress().toString().c_str());
        g_zwift_connected = true;
        g_stats.zwift_connects++;
        // Allow additional connections (Zwift sometimes opens multiple)
        NimBLEDevice::startAdvertising();
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        Serial.printf("[Peripheral] Client disconnected (reason=%d)\n", reason);
        g_zwift_connected = false;
        g_stats.zwift_disconnects++;
        NimBLEDevice::startAdvertising();
    }
};

static ServerCallbacks s_server_callbacks;

// --- Control Point write callback ---

class ControlPointCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue val = chr->getValue();
        size_t len = val.size();
        if (len == 0) return;

        // Make a mutable copy
        uint8_t buf[64];
        if (len > sizeof(buf)) len = sizeof(buf);
        memcpy(buf, val.data(), len);

        uint8_t opcode = buf[0];

        if (opcode == FTMS_OP_SET_INDOOR_BIKE_SIM && len >= SIM_PARAMS_LENGTH) {
            // Extract all sim params for logging
            int16_t wind_speed = (int16_t)(buf[1] | (buf[2] << 8));
            int16_t orig_grade = (int16_t)(buf[SIM_PARAMS_GRADE_OFFSET] |
                                           (buf[SIM_PARAMS_GRADE_OFFSET + 1] << 8));
            uint8_t rolling_res = buf[5];
            uint8_t wind_res = buf[6];

            bool modified = process_sim_params(buf, len, g_descent_scale_factor);

            if (modified) {
                int16_t new_grade = (int16_t)(buf[SIM_PARAMS_GRADE_OFFSET] |
                                              (buf[SIM_PARAMS_GRADE_OFFSET + 1] << 8));
                Serial.printf("[Filter] Grade: %d -> %d (%.1f%% -> %.1f%%) | "
                              "Wind: %.1f m/s | CRR: %.4f | CW: %.2f kg/m\n",
                              orig_grade, new_grade,
                              orig_grade / 100.0, new_grade / 100.0,
                              wind_speed / 1000.0, rolling_res / 10000.0, wind_res / 100.0);
                g_stats.grades_modified++;
            } else {
                Serial.printf("[Sim] Grade: %.1f%% | Wind: %.1f m/s | CRR: %.4f | CW: %.2f kg/m\n",
                              orig_grade / 100.0,
                              wind_speed / 1000.0, rolling_res / 10000.0, wind_res / 100.0);
            }
        } else if (opcode == FTMS_OP_SET_TARGET_POWER && len >= 3) {
            int16_t power = (int16_t)(buf[1] | (buf[2] << 8));
            Serial.printf("[Passthrough] Target Power: %d W\n", power);
        } else if (opcode == FTMS_OP_REQUEST_CONTROL) {
            Serial.println("[Passthrough] Request Control");
        } else if (opcode == FTMS_OP_RESET) {
            Serial.println("[Passthrough] Reset");
        } else {
            Serial.printf("[Passthrough] Opcode: 0x%02X (%d bytes)\n", opcode, len);
        }

        // Forward to trainer
        if (!central_write_control_point(buf, len)) {
            Serial.println("[Peripheral] Failed to forward to trainer");
            g_stats.write_errors++;
            uint8_t resp[] = { FTMS_OP_RESPONSE_CODE, opcode, FTMS_RESULT_OPERATION_FAIL };
            chr->indicate(resp, sizeof(resp));
            return;
        }

        g_stats.packets_forwarded++;
        uint8_t resp[] = { FTMS_OP_RESPONSE_CODE, opcode, FTMS_RESULT_SUCCESS };
        chr->indicate(resp, sizeof(resp));
    }
};

static ControlPointCallbacks s_cp_callbacks;

// --- Public API ---

void peripheral_init() {
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_server_callbacks);

    NimBLEService* svc = s_server->createService(FTMS_SERVICE_UUID);

    // Indoor Bike Data (0x2AD2) — notify
    s_bike_data_char = svc->createCharacteristic(
        FTMS_INDOOR_BIKE_DATA_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // Control Point (0x2AD9) — write + indicate
    s_control_point_char = svc->createCharacteristic(
        FTMS_CONTROL_POINT_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE
    );
    s_control_point_char->setCallbacks(&s_cp_callbacks);

    // Feature (0x2ACC) — read
    NimBLECharacteristic* feature = svc->createCharacteristic(
        FTMS_FEATURE_UUID,
        NIMBLE_PROPERTY::READ
    );
    if (g_trainer_cache.valid && g_trainer_cache.feature_len > 0) {
        feature->setValue(g_trainer_cache.feature, g_trainer_cache.feature_len);
    }

    // Status (0x2AD3) — notify
    s_status_char = svc->createCharacteristic(
        FTMS_STATUS_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // Resistance Range (0x2AD6) — read
    if (g_trainer_cache.valid && g_trainer_cache.resistance_range_len > 0) {
        NimBLECharacteristic* rr = svc->createCharacteristic(
            FTMS_RESISTANCE_RANGE_UUID,
            NIMBLE_PROPERTY::READ
        );
        rr->setValue(g_trainer_cache.resistance_range, g_trainer_cache.resistance_range_len);
    }

    // Power Range (0x2AD8) — read
    if (g_trainer_cache.valid && g_trainer_cache.power_range_len > 0) {
        NimBLECharacteristic* pr = svc->createCharacteristic(
            FTMS_POWER_RANGE_UUID,
            NIMBLE_PROPERTY::READ
        );
        pr->setValue(g_trainer_cache.power_range, g_trainer_cache.power_range_len);
    }

    svc->start();

    // Configure advertising
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(FTMS_SERVICE_UUID);
    adv->setName("FTMS Proxy");
    adv->start();

    Serial.println("[Peripheral] Advertising as 'FTMS Proxy'");
}

void peripheral_notify_bike_data(const uint8_t* data, size_t len) {
    if (!g_zwift_connected || !s_bike_data_char) return;
    s_bike_data_char->notify(data, len);
}

void peripheral_notify_status(const uint8_t* data, size_t len) {
    if (!g_zwift_connected || !s_status_char) return;
    s_status_char->notify(data, len);
}
