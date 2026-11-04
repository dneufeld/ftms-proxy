#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include "Arduino.h"
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_HS_EDONE 14
namespace NIMBLE_PROPERTY { enum {READ=1,WRITE=2,NOTIFY=4,INDICATE=8}; }
struct NimBLEConnInfo { uint16_t getConnHandle() const {return 1;} };
class NimBLECharacteristic;
class NimBLEServer;
struct NimBLECharacteristicCallbacks {
    virtual ~NimBLECharacteristicCallbacks() {}
    virtual void onSubscribe(NimBLECharacteristic*,NimBLEConnInfo&,uint16_t) {}
    virtual void onWrite(NimBLECharacteristic*,NimBLEConnInfo&) {}
    virtual void onStatus(NimBLECharacteristic*,NimBLEConnInfo&,int) {}
};
struct NimBLEServerCallbacks {
    virtual ~NimBLEServerCallbacks() {}
    virtual void onConnect(NimBLEServer*,NimBLEConnInfo&) {}
    virtual void onDisconnect(NimBLEServer*,NimBLEConnInfo&,int) {}
};
struct NimBLEAttValue : std::vector<uint8_t> { using std::vector<uint8_t>::vector; };
struct NimBLECharacteristic {
    NimBLECharacteristicCallbacks* callbacks=nullptr;
    NimBLEAttValue value;
    std::vector<std::vector<uint8_t>> indications;
    void setCallbacks(NimBLECharacteristicCallbacks* cb) {callbacks=cb;}
    NimBLEAttValue getValue() {return value;}
    void setValue(const uint8_t* p,size_t n) {value.assign(p,p+n);}
    bool indicate(const uint8_t* p,size_t n,uint16_t) {indications.emplace_back(p,p+n);return true;}
    bool notify(const uint8_t*,size_t,uint16_t) {return true;}
};
struct NimBLEService {
    std::map<std::string,NimBLECharacteristic*> chars;
    NimBLECharacteristic* createCharacteristic(const char* id,int,int=512) {return chars[id]=new NimBLECharacteristic;}
    void start() {}
};
struct NimBLEServer {
    NimBLEServerCallbacks* callbacks=nullptr;
    int disconnects=0;
    void setCallbacks(NimBLEServerCallbacks* cb) {callbacks=cb;}
    bool disconnect(uint16_t) {disconnects++;return true;}
    NimBLEService* createService(const char*) {return new NimBLEService;}
};
struct NimBLEUUID { explicit NimBLEUUID(const char*) {} };
struct NimBLEAdvertising {
    bool advertising=false;
    void addServiceUUID(const char*) {} void setName(const char*) {} void enableScanResponse(bool) {} void setAppearance(int) {}
    void setServiceData(NimBLEUUID,const std::string&) {}
    bool isAdvertising() {return advertising;}
};
struct NimBLEDevice {
    static NimBLEAdvertising* getAdvertising() {static NimBLEAdvertising adv;return &adv;}
    static NimBLEServer* createServer() {return new NimBLEServer;}
    static bool startAdvertising() {getAdvertising()->advertising=true;return true;}
    static bool stopAdvertising() {getAdvertising()->advertising=false;return true;}
};
