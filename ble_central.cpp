#include "ble_central.h"
#include "ble_peripheral.h"
#include "ftms_defs.h"
#include "proxy_stats.h"
#include "settings.h"
#include <Arduino.h>
TrainerCharCache g_trainer_cache;
std::atomic<bool> g_trainer_connected{false};
static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* control = nullptr;
static std::string pinned_address;
static char target_name[32] = "Tacx";
static std::atomic<bool> scanning{false};
static NimBLEAdvertisedDevice* target = nullptr;
static portMUX_TYPE scan_lock = portMUX_INITIALIZER_UNLOCKED;
static void bike_cb(NimBLERemoteCharacteristic*, uint8_t* p, size_t n, bool) { STAT_INC(notifications_relayed); peripheral_notify_bike_data(p,n); }
static void status_cb(NimBLERemoteCharacteristic*, uint8_t* p, size_t n, bool) { peripheral_notify_status(p,n); }
static void training_cb(NimBLERemoteCharacteristic*, uint8_t* p, size_t n, bool) { peripheral_notify_training(p,n); }
static void control_cb(NimBLERemoteCharacteristic*, uint8_t* p, size_t n, bool notify) { if (!notify) peripheral_control_response(p,n); }
class ClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient*, int) override {
        if (!g_trainer_connected.exchange(false)) return;
        STAT_INC(trainer_disconnects);
        peripheral_trainer_lost(); // flag only: no blocking calls from host task
    }
} client_callbacks;
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* d) override {
        if (!pinned_address.empty() && d->getAddress().toString() != pinned_address) return;
        if (!d->isAdvertisingService(NimBLEUUID(FTMS_SERVICE_UUID)) || d->getName().find(target_name) == std::string::npos) return;
        portENTER_CRITICAL(&scan_lock);
        bool empty = target == nullptr;
        portEXIT_CRITICAL(&scan_lock);
        if (!empty) return;
        auto copy = new NimBLEAdvertisedDevice(*d);
        portENTER_CRITICAL(&scan_lock); target = copy; portEXIT_CRITICAL(&scan_lock);
        NimBLEDevice::getScan()->stop();
    }
    void onScanEnd(const NimBLEScanResults&, int) override { scanning = false; }
} scan_callbacks;
static bool read(NimBLERemoteService* svc, const char* uuid, CachedValue& dest, bool required = false) {
    auto chr = svc->getCharacteristic(uuid);
    if (!chr) return !required;
    if (!chr->canRead()) return false;
    auto value = chr->readValue();
    if (value.size() == 0 || value.size() > sizeof(dest.bytes)) return false;
    dest.len = value.size(); memcpy(dest.bytes, value.data(), dest.len); return true;
}
static bool subscribe(NimBLERemoteService* svc, const char* uuid, NimBLERemoteCharacteristic::notify_callback cb, bool indicate, bool required) {
    auto chr = svc->getCharacteristic(uuid);
    if (!chr) return !required;
    return (indicate ? chr->canIndicate() : chr->canNotify()) && chr->subscribe(!indicate, cb);
}
void central_init() {
    const Settings s = settings_snapshot(); memcpy(target_name,s.target,sizeof(target_name));
    auto scan = NimBLEDevice::getScan(); scan->setScanCallbacks(&scan_callbacks,false);
    scan->setActiveScan(true); scan->setInterval(160); scan->setWindow(48); scan->setMaxResults(0);
}
void central_disconnect() {
    // Withdraw readiness before the asynchronous GAP disconnect completes.
    if (g_trainer_connected.exchange(false)) { STAT_INC(trainer_disconnects); peripheral_trainer_lost(); }
    if (client && client->isConnected()) client->disconnect();
}
void central_loop() {
    NimBLEAdvertisedDevice* found;
    portENTER_CRITICAL(&scan_lock); found = target; target = nullptr; portEXIT_CRITICAL(&scan_lock);
    if (found) {
        if (!client) { client = NimBLEDevice::createClient(); client->setClientCallbacks(&client_callbacks,false); client->setConnectTimeout(5000); client->setConnectionParams(24,40,0,400); }
        g_trainer_cache = {}; control = nullptr;
        if (client && client->connect(found)) {
            auto svc = client->getService(FTMS_SERVICE_UUID);
            bool ok = svc && read(svc,FTMS_FEATURE_UUID,g_trainer_cache.feature,true) && g_trainer_cache.feature.len == 8 &&
                read(svc,FTMS_RESISTANCE_RANGE_UUID,g_trainer_cache.resistance) && read(svc,FTMS_POWER_RANGE_UUID,g_trainer_cache.power) &&
                read(svc,FTMS_INCLINATION_RANGE_UUID,g_trainer_cache.inclination) && read(svc,FTMS_HEART_RANGE_UUID,g_trainer_cache.heart) &&
                read(svc,FTMS_TRAINING_STATUS_UUID,g_trainer_cache.training);
            if (ok) {
                control = svc->getCharacteristic(FTMS_CONTROL_POINT_UUID);
                ok = control && control->canWrite() && control->canIndicate() &&
                    subscribe(svc,FTMS_CONTROL_POINT_UUID,control_cb,true,true) &&
                    subscribe(svc,FTMS_INDOOR_BIKE_DATA_UUID,bike_cb,false,true) &&
                    subscribe(svc,FTMS_STATUS_UUID,status_cb,false,true) &&
                    subscribe(svc,FTMS_TRAINING_STATUS_UUID,training_cb,false,false);
            }
            if (ok) { pinned_address = found->getAddress().toString(); g_trainer_cache.valid = true; g_trainer_connected = true; STAT_INC(trainer_connects); Serial.println("Trainer ready: FTMS control indications subscribed"); }
            else { Serial.println("Trainer rejected: missing/read/subscribe failure in FTMS profile"); client->disconnect(); }
        }
        delete found; return;
    }
    static uint32_t last = 0;
    if (!g_trainer_connected && !scanning && uint32_t(millis()-last) >= 5000) {
        last = millis(); scanning = true;
        if (!NimBLEDevice::getScan()->start(10000,false)) scanning = false; // NimBLE 2.x: milliseconds
    }
}
bool central_write_control_point(const uint8_t* p, size_t n) {
    return g_trainer_connected && client && client->isConnected() && control && control->writeValue(p,n,true);
}
int central_get_rssi() { return g_trainer_connected && client ? client->getRssi() : 0; }
