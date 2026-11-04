#include "ble_peripheral.h"
#include "ble_central.h"
#include "ftms_defs.h"
#include "gradient_filter.h"
#include "proxy_stats.h"
#include "settings.h"
#include <Arduino.h>
#include <freertos/queue.h>
std::atomic<bool> g_zwift_connected{false};
static NimBLEServer* server;
static NimBLECharacteristic *bike, *control, *status, *training;
static NimBLECharacteristic *features, *resistance, *power, *inclination, *heart;
static std::atomic<uint16_t> peer{BLE_HS_CONN_HANDLE_NONE};
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool busy = false, subscribed = false, awaiting_ack = false;
static std::atomic<bool> reset_links{false}, trainer_lost{false};
static uint8_t opcode;
static uint32_t started;
struct Packet { uint8_t data[20]; size_t len; };
static QueueHandle_t commands, replies;
static GradeFilter filter;
static void disconnect_peer() { if (server && peer != BLE_HS_CONN_HANDLE_NONE) server->disconnect(peer); }
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        if (g_zwift_connected || !g_trainer_connected) { s->disconnect(info.getConnHandle()); return; }
        peer = info.getConnHandle(); g_zwift_connected = true; STAT_INC(zwift_connects);
        NimBLEDevice::stopAdvertising(); // one controller, no shared control ownership
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) override {
        if (info.getConnHandle() != peer) return;
        g_zwift_connected = false; peer = BLE_HS_CONN_HANDLE_NONE; STAT_INC(zwift_disconnects);
        portENTER_CRITICAL(&lock); busy = subscribed = awaiting_ack = false; portEXIT_CRITICAL(&lock);
        reset_links = true; // reconnect upstream to release the old controller's permission
    }
} server_callbacks;
class ControlCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info, uint16_t bits) override {
        if (info.getConnHandle() == peer) { portENTER_CRITICAL(&lock); subscribed = (bits & 2) != 0; if (busy && !subscribed) reset_links = true; portEXIT_CRITICAL(&lock); }
    }
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        auto value = c->getValue();
        portENTER_CRITICAL(&lock);
        bool accept = info.getConnHandle() == peer && subscribed && !busy && g_trainer_connected;
        if (accept) { busy = true; awaiting_ack = false; opcode = value.size() ? value[0] : 0; started = millis(); }
        portEXIT_CRITICAL(&lock);
        // NimBLE's void onWrite API cannot return application ATT errors. Fail
        // closed on invalid pipelining/CCCD instead of claiming another success.
        if (!accept || value.size() == 0) { reset_links = true; return; }
        Packet packet = {};
        if (value.size() > sizeof(packet.data) || (value[0] == FTMS_OP_SET_INDOOR_BIKE_SIM && value.size() != SIM_PARAMS_LENGTH)) {
            packet.len = 3; packet.data[0] = 0x80; packet.data[1] = opcode; packet.data[2] = FTMS_RESULT_INVALID_PARAM;
            if (xQueueSend(replies,&packet,0) != pdTRUE) reset_links = true;
            return;
        }
        packet.len = value.size(); memcpy(packet.data,value.data(),packet.len);
        if (xQueueSend(commands,&packet,0) != pdTRUE) reset_links = true;
    }
    void onStatus(NimBLECharacteristic*, NimBLEConnInfo& info, int code) override {
        if (info.getConnHandle() != peer) return;
        portENTER_CRITICAL(&lock);
        if (awaiting_ack) { busy = false; awaiting_ack = false; if (code != BLE_HS_EDONE) reset_links = true; }
        portEXIT_CRITICAL(&lock);
    }
} control_callbacks;
void peripheral_control_response(const uint8_t* p, size_t n) {
    portENTER_CRITICAL(&lock); bool wanted = busy && !awaiting_ack && n >= 3 && p[0] == 0x80 && p[1] == opcode; portEXIT_CRITICAL(&lock);
    if (!wanted || n > 20) return;
    Packet packet = {}; packet.len = n; memcpy(packet.data,p,n);
    if (xQueueSend(replies,&packet,0) != pdTRUE) reset_links = true;
}
void peripheral_trainer_lost() { trainer_lost = true; }
static void cached(NimBLECharacteristic*& c, NimBLEService* svc, const char* uuid, CachedValue& v, bool notify = false) {
    if (!v.len) return;
    if (!c) c = svc->createCharacteristic(uuid,NIMBLE_PROPERTY::READ | (notify ? NIMBLE_PROPERTY::NOTIFY : 0));
    c->setValue(v.bytes,v.len);
}
void peripheral_init() {
    commands = xQueueCreate(1,sizeof(Packet)); replies = xQueueCreate(1,sizeof(Packet));
    if (!commands || !replies) { Serial.println("Queue allocation failed"); abort(); }
    server = NimBLEDevice::createServer(); server->setCallbacks(&server_callbacks);
    auto svc = server->createService(FTMS_SERVICE_UUID);
    bike = svc->createCharacteristic(FTMS_INDOOR_BIKE_DATA_UUID,NIMBLE_PROPERTY::NOTIFY);
    status = svc->createCharacteristic(FTMS_STATUS_UUID,NIMBLE_PROPERTY::NOTIFY);
    control = svc->createCharacteristic(FTMS_CONTROL_POINT_UUID,NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE,20);
    control->setCallbacks(&control_callbacks);
    cached(features,svc,FTMS_FEATURE_UUID,g_trainer_cache.feature);
    cached(resistance,svc,FTMS_RESISTANCE_RANGE_UUID,g_trainer_cache.resistance);
    cached(power,svc,FTMS_POWER_RANGE_UUID,g_trainer_cache.power);
    cached(inclination,svc,FTMS_INCLINATION_RANGE_UUID,g_trainer_cache.inclination);
    cached(heart,svc,FTMS_HEART_RANGE_UUID,g_trainer_cache.heart);
    cached(training,svc,FTMS_TRAINING_STATUS_UUID,g_trainer_cache.training,true);
    svc->start(); auto adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(FTMS_SERVICE_UUID); adv->setName("FTMS Proxy"); const uint8_t machine[] = {1,0x20,0}; adv->setServiceData(NimBLEUUID(FTMS_SERVICE_UUID),std::string(reinterpret_cast<const char*>(machine),sizeof(machine))); adv->enableScanResponse(true); adv->setAppearance(0x0480); // Generic Cycling
}
void peripheral_loop() {
    if (!server) return;
    if (reset_links || trainer_lost) {
        reset_links = trainer_lost = false;
        NimBLEDevice::stopAdvertising(); disconnect_peer(); central_disconnect();
        xQueueReset(commands); xQueueReset(replies); filter.reset();
        portENTER_CRITICAL(&lock); busy = awaiting_ack = subscribed = false; portEXIT_CRITICAL(&lock);
        return;
    }
    if (g_trainer_connected && !g_zwift_connected && !NimBLEDevice::getAdvertising()->isAdvertising()) {
        // Same physical trainer after reconnect. Changing target requires reboot
        // so its optional characteristic layout is rediscovered from scratch.
        features->setValue(g_trainer_cache.feature.bytes,g_trainer_cache.feature.len);
        if (training) training->setValue(g_trainer_cache.training.bytes,g_trainer_cache.training.len);
        NimBLEDevice::startAdvertising();
    }
    Packet packet;
    if (xQueueReceive(commands,&packet,0) == pdTRUE) {
        if (packet.data[0] == FTMS_OP_RESET) filter.reset();
        if (packet.data[0] == FTMS_OP_SET_INDOOR_BIKE_SIM) {
            STAT_SET(requested_grade, int16_t(packet.data[3] | (uint16_t(packet.data[4]) << 8)));
            if (filter.process(packet.data,packet.len,settings_snapshot(),millis())) STAT_INC(grades_modified);
        }
        // Arduino loop task, never the NimBLE host callback.
        if (!central_write_control_point(packet.data,packet.len)) {
            STAT_INC(write_errors); reset_links = true; return; // ATT failure is not FTMS success
        }
        STAT_INC(packets_forwarded);
        if (packet.data[0] == 0x11) STAT_SET(sent_grade, int16_t(packet.data[3] | (uint16_t(packet.data[4]) << 8)));
    }
    if (xQueueReceive(replies,&packet,0) == pdTRUE && g_zwift_connected) {
        portENTER_CRITICAL(&lock); awaiting_ack = true; portEXIT_CRITICAL(&lock);
        STAT_SET(command_ms, uint32_t(millis()-started));
        STAT_SET(last_opcode, packet.data[1]); STAT_SET(last_result, packet.data[2]);
        Serial.printf("FTMS result: opcode=0x%02x result=0x%02x latency=%lu ms\n",packet.data[1],packet.data[2],(unsigned long)STAT_GET(command_ms));
        if (!control->indicate(packet.data,packet.len,peer)) reset_links = true;
    }
    portENTER_CRITICAL(&lock); bool expired = busy && uint32_t(millis()-started) > 30000; portEXIT_CRITICAL(&lock);
    if (expired) { STAT_INC(write_errors); reset_links = true; }
}
static void notify(NimBLECharacteristic* c, const uint8_t* p, size_t n) {
    if (!c || !g_zwift_connected) return;
    // Global MTU=23 on both links: trainer must split its data record itself.
    // Never silently truncate a forwarded FTMS record.
    if (n > 20) { STAT_INC(mtu_drops); reset_links = true; return; }
    c->setValue(p,n); c->notify(p,n,peer);
}
void peripheral_notify_bike_data(const uint8_t* p,size_t n) { notify(bike,p,n); }
void peripheral_notify_status(const uint8_t* p,size_t n) { notify(status,p,n); }
void peripheral_notify_training(const uint8_t* p,size_t n) { notify(training,p,n); }
