// Runs the actual peripheral bridge; BLE/RTOS transports are in-process fakes.
// This proves command ordering/bytes, not radio interoperability or heap use.
#include "settings.h"
Settings settings_snapshot();
#include "../ble_peripheral.cpp"
#include <cassert>
#include <cstdio>
ProxyStats g_stats;
TrainerCharCache g_trainer_cache;
std::atomic<bool> g_trainer_connected{true};
uint32_t fake_time=100;
static std::vector<std::vector<uint8_t>> upstream;
static bool write_ok=true;
static int disconnects=0;
Settings settings_snapshot() {return Settings{};}
bool central_write_control_point(const uint8_t* p,size_t n) {upstream.emplace_back(p,p+n);return write_ok;}
void central_disconnect() {disconnects++;g_trainer_connected=false;}
static void write(std::initializer_list<uint8_t> bytes) {
    control->value.assign(bytes); NimBLEConnInfo info; control->callbacks->onWrite(control,info);
}
static void ack() {NimBLEConnInfo info;control->callbacks->onStatus(control,info,BLE_HS_EDONE);}
int main() {
    g_trainer_cache.valid=true; g_trainer_cache.feature.len=8;
    peripheral_init(); peripheral_loop(); assert(NimBLEDevice::getAdvertising()->isAdvertising());
    NimBLEConnInfo info; server->callbacks->onConnect(server,info); control->callbacks->onSubscribe(control,info,2);
    write({0}); assert(upstream.empty()); assert(control->indications.empty()); // callback never blocks on trainer
    peripheral_loop(); assert(upstream.back()==std::vector<uint8_t>{0}); assert(control->indications.empty());
    uint8_t wrong[]={0x80,5,1}; peripheral_control_response(wrong,3); peripheral_loop(); assert(control->indications.empty());
    uint8_t denied[]={0x80,0,5}; peripheral_control_response(denied,3); peripheral_loop();
    assert(control->indications.back()==std::vector<uint8_t>({0x80,0,5})); assert(busy); ack(); assert(!busy);
    write({0x11,0x34,0x12,0x18,0xfc,42,99}); peripheral_loop();
    assert(upstream.back()==std::vector<uint8_t>({0x11,0x34,0x12,0x0c,0xfe,42,99}));
    assert(STAT_GET(requested_grade)==-1000 && STAT_GET(sent_grade)==-500);
    uint8_t success[]={0x80,0x11,1}; peripheral_control_response(success,3); peripheral_loop(); ack();
    size_t writes=upstream.size(); write({0x11,0,0}); peripheral_loop();
    assert(upstream.size()==writes); assert(control->indications.back()==std::vector<uint8_t>({0x80,0x11,3})); ack();
    write({1}); peripheral_loop(); assert(upstream.back()==std::vector<uint8_t>{1});
    // Pipelining is never forwarded or answered with fabricated success.
    write({5,200,0}); assert(reset_links); peripheral_loop(); assert(disconnects==1); assert(upstream.size()==writes+1);
    // Link loss clears queued commands and keeps advertising off.
    g_zwift_connected=false; peer=BLE_HS_CONN_HANDLE_NONE;
    peripheral_trainer_lost(); peripheral_loop(); assert(!busy && !subscribed); assert(!NimBLEDevice::getAdvertising()->isAdvertising());
    puts("Actual bridge tests PASS: queued writes, actual result relay, sign/bytes, reset, pipelining, disconnect");
}
