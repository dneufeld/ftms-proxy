#include "ble_central.h"
#include "ble_peripheral.h"
#include "ftms_defs.h"
#include "proxy_stats.h"
#include <Arduino.h>

TrainerCharCache g_trainer_cache = {};
bool g_trainer_connected = false;

static const char* s_target_name = "Tacx";
static NimBLEClient* s_client = nullptr;
static NimBLERemoteCharacteristic* s_control_point_char = nullptr;
static bool s_scanning = false;
static NimBLEAdvertisedDevice* s_target_device = nullptr;

// Forward declarations
static void start_scan();

// --- Notification callbacks ---

static void bike_data_notify_cb(NimBLERemoteCharacteristic* chr,
                                 uint8_t* data, size_t len, bool isNotify) {
    g_stats.notifications_relayed++;
    peripheral_notify_bike_data(data, len);
}

static void status_notify_cb(NimBLERemoteCharacteristic* chr,
                              uint8_t* data, size_t len, bool isNotify) {
    g_stats.notifications_relayed++;
    peripheral_notify_status(data, len);
}

// --- Client callbacks ---

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* client) override {
        Serial.println("[Central] Connected to trainer");
        g_stats.trainer_connects++;
    }

    void onDisconnect(NimBLEClient* client, int reason) override {
        Serial.printf("[Central] Disconnected from trainer (reason=%d)\n", reason);
        g_trainer_connected = false;
        s_control_point_char = nullptr;
        g_stats.trainer_disconnects++;
    }
};

static ClientCallbacks s_client_callbacks;

// --- Scan callbacks ---

class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        if (s_target_device) return; // Already found one

        std::string name = device->getName();
        if (name.length() == 0) return;

        if (name.find(s_target_name) != std::string::npos) {
            Serial.printf("[Central] Found target: %s (%s)\n",
                          name.c_str(), device->getAddress().toString().c_str());
            // Stop scanning — we clone the device info we need
            NimBLEDevice::getScan()->stop();
            s_target_device = new NimBLEAdvertisedDevice(*device);
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        s_scanning = false;
        Serial.printf("[Central] Scan ended (reason=%d, found %d devices)\n",
                      reason, results.getCount());
    }
};

static ScanCallbacks s_scan_callbacks;

// --- Read cached characteristics from trainer ---

static bool read_trainer_characteristics(NimBLERemoteService* svc) {
    // Feature (0x2ACC) — required
    NimBLERemoteCharacteristic* feature = svc->getCharacteristic(FTMS_FEATURE_UUID);
    if (feature && feature->canRead()) {
        NimBLEAttValue val = feature->readValue();
        g_trainer_cache.feature_len = val.size();
        if (g_trainer_cache.feature_len > sizeof(g_trainer_cache.feature))
            g_trainer_cache.feature_len = sizeof(g_trainer_cache.feature);
        memcpy(g_trainer_cache.feature, val.data(), g_trainer_cache.feature_len);
        Serial.printf("[Central] Read Feature (%d bytes)\n", g_trainer_cache.feature_len);
    } else {
        Serial.println("[Central] WARNING: Could not read Feature characteristic");
    }

    // Resistance Range (0x2AD6) — optional
    NimBLERemoteCharacteristic* rr = svc->getCharacteristic(FTMS_RESISTANCE_RANGE_UUID);
    if (rr && rr->canRead()) {
        NimBLEAttValue val = rr->readValue();
        g_trainer_cache.resistance_range_len = val.size();
        if (g_trainer_cache.resistance_range_len > sizeof(g_trainer_cache.resistance_range))
            g_trainer_cache.resistance_range_len = sizeof(g_trainer_cache.resistance_range);
        memcpy(g_trainer_cache.resistance_range, val.data(), g_trainer_cache.resistance_range_len);
        Serial.printf("[Central] Read Resistance Range (%d bytes)\n", g_trainer_cache.resistance_range_len);
    }

    // Power Range (0x2AD8) — optional
    NimBLERemoteCharacteristic* pr = svc->getCharacteristic(FTMS_POWER_RANGE_UUID);
    if (pr && pr->canRead()) {
        NimBLEAttValue val = pr->readValue();
        g_trainer_cache.power_range_len = val.size();
        if (g_trainer_cache.power_range_len > sizeof(g_trainer_cache.power_range))
            g_trainer_cache.power_range_len = sizeof(g_trainer_cache.power_range);
        memcpy(g_trainer_cache.power_range, val.data(), g_trainer_cache.power_range_len);
        Serial.printf("[Central] Read Power Range (%d bytes)\n", g_trainer_cache.power_range_len);
    }

    g_trainer_cache.valid = true;
    return true;
}

// --- Connect to the discovered trainer ---

static bool connect_to_trainer() {
    if (!s_target_device) return false;

    Serial.printf("[Central] Connecting to %s...\n", s_target_device->getName().c_str());

    if (!s_client) {
        s_client = NimBLEDevice::createClient();
        s_client->setClientCallbacks(&s_client_callbacks, false);
        s_client->setConnectionParams(12, 12, 0, 200); // fast connection interval
    }

    if (!s_client->connect(s_target_device)) {
        Serial.println("[Central] Connection failed");
        delete s_target_device;
        s_target_device = nullptr;
        return false;
    }

    Serial.println("[Central] Connected, discovering FTMS service...");

    NimBLERemoteService* svc = s_client->getService(FTMS_SERVICE_UUID);
    if (!svc) {
        Serial.println("[Central] FTMS service not found!");
        s_client->disconnect();
        delete s_target_device;
        s_target_device = nullptr;
        return false;
    }

    // Read static characteristics
    read_trainer_characteristics(svc);

    // Subscribe to Indoor Bike Data notifications (0x2AD2)
    NimBLERemoteCharacteristic* bikeData = svc->getCharacteristic(FTMS_INDOOR_BIKE_DATA_UUID);
    if (bikeData && bikeData->canNotify()) {
        bikeData->subscribe(true, bike_data_notify_cb);
        Serial.println("[Central] Subscribed to Indoor Bike Data");
    } else {
        Serial.println("[Central] WARNING: Cannot subscribe to Indoor Bike Data");
    }

    // Subscribe to Fitness Machine Status notifications (0x2AD3)
    NimBLERemoteCharacteristic* status = svc->getCharacteristic(FTMS_STATUS_UUID);
    if (status && status->canNotify()) {
        status->subscribe(true, status_notify_cb);
        Serial.println("[Central] Subscribed to Fitness Machine Status");
    }

    // Get Control Point characteristic for writing
    s_control_point_char = svc->getCharacteristic(FTMS_CONTROL_POINT_UUID);
    if (!s_control_point_char) {
        Serial.println("[Central] WARNING: Control Point characteristic not found");
    } else {
        Serial.println("[Central] Control Point ready");
    }

    g_trainer_connected = true;

    delete s_target_device;
    s_target_device = nullptr;

    Serial.println("[Central] Trainer setup complete");
    return true;
}

// --- Scanning ---

static void start_scan() {
    if (s_scanning) return;

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scan_callbacks, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);

    Serial.println("[Central] Starting scan for FTMS trainers...");
    scan->start(10, false); // 10 second scan, don't continue after
    s_scanning = true;
}

// --- Public API ---

void central_init(const char* target_name) {
    s_target_name = target_name;
    start_scan();
}

void central_loop() {
    // If we found a device during scanning, connect to it
    if (s_target_device && !g_trainer_connected) {
        connect_to_trainer();
        return;
    }

    // If not connected and not scanning, restart scan
    if (!g_trainer_connected && !s_scanning && !s_target_device) {
        static unsigned long last_scan_time = 0;
        unsigned long now = millis();
        if (now - last_scan_time > 5000) { // Wait 5s between scan attempts
            last_scan_time = now;
            start_scan();
        }
    }
}

bool central_write_control_point(const uint8_t* data, size_t len) {
    if (!g_trainer_connected || !s_control_point_char) {
        Serial.println("[Central] Cannot write: not connected or no control point");
        return false;
    }

    return s_control_point_char->writeValue(data, len, true); // true = wait for response (write with response)
}

int central_get_rssi() {
    if (!g_trainer_connected || !s_client || !s_client->isConnected()) {
        return 0;
    }
    return s_client->getRssi();
}
