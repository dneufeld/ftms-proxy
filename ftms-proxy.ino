#include <NimBLEDevice.h>
#include "ble_central.h"
#include "ble_peripheral.h"
#include "settings.h"
#include "proxy_stats.h"
ProxyStats g_stats;
void setup() {
    Serial.begin(115200); delay(500);
    settings_begin();
    NimBLEDevice::init("FTMS Proxy");
    // Keep both links at the mandatory MTU: trainer-generated FTMS fragments
    // can then be forwarded unchanged even if Zwift never negotiates MTU.
    NimBLEDevice::setMTU(23);
    central_init(); web_begin();
}
void loop() {
    static char command[32]; static size_t used = 0;
    while (Serial.available()) {
        const char c = Serial.read();
        if (c == '\n' || c == '\r') {
            command[used] = 0;
            if (!strcmp(command,"wifi off")) web_set_enabled(false);
            if (!strcmp(command,"wifi on")) web_set_enabled(true);
            used = 0;
        } else if (used < sizeof(command)-1) command[used++] = c;
    }
    central_loop();
    static bool started=false;
    if (!started && g_trainer_connected && g_trainer_cache.valid) { peripheral_init(); started=true; }
    if (started) peripheral_loop();
    static uint32_t last=0;
    if (uint32_t(millis()-last)>=30000) {last=millis();Serial.printf("Trainer=%d Zwift=%d heap=%lu min=%lu loop_stack=%lu\n",g_trainer_connected.load(),g_zwift_connected.load(),(unsigned long)ESP.getFreeHeap(),(unsigned long)ESP.getMinFreeHeap(),(unsigned long)uxTaskGetStackHighWaterMark(nullptr));}
    delay(5);
}
