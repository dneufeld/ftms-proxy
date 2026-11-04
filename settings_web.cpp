#include "settings.h"
#include "proxy_stats.h"
#include "ble_central.h"
#include "ble_peripheral.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <cerrno>
#include <cstdlib>
#include <cmath>
#include <atomic>
static portMUX_TYPE config_lock = portMUX_INITIALIZER_UNLOCKED;
static Settings config;
static Preferences prefs;
static WebServer web(80);
static DNSServer dns;
struct WifiCredentials { char ssid[33] = {}; char password[65] = {}; };
struct Stored { uint32_t version = 1; Settings settings; };
static WifiCredentials network;
static String admin_password, csrf, ap_name;
static bool ap = false;
static std::atomic<bool> restart_requested{false};
static uint32_t wifi_start, disconnected_at = 0;
static std::atomic<int> wifi_request{0};
void web_set_enabled(bool enabled) { wifi_request = enabled ? 2 : 1; }
Settings settings_snapshot() { portENTER_CRITICAL(&config_lock); Settings s = config; portEXIT_CRITICAL(&config_lock); return s; }
bool settings_save(const Settings& s) {
    if (!valid_settings(s)) return false;
    Stored record; record.settings = s;
    if (prefs.putBytes("config",&record,sizeof(record)) != sizeof(record)) return false;
    portENTER_CRITICAL(&config_lock); config = s; portEXIT_CRITICAL(&config_lock); return true;
}
static String random_secret() { char buf[33]; for (int i=0;i<4;i++) snprintf(buf+i*8,9,"%08lx",(unsigned long)esp_random()); return String(buf); }
void settings_begin() {
    if (!prefs.begin("ftms",false)) { Serial.println("NVS open failed"); abort(); }
    Stored saved;
    if (prefs.getBytesLength("config") == sizeof(saved) && prefs.getBytes("config",&saved,sizeof(saved)) == sizeof(saved) && saved.version == 1 && valid_settings(saved.settings)) config = saved.settings;
    if (prefs.getBytesLength("wifi") == sizeof(network)) prefs.getBytes("wifi",&network,sizeof(network));
    if (!memchr(network.ssid,0,sizeof(network.ssid)) || !memchr(network.password,0,sizeof(network.password))) network = {};
    admin_password = prefs.getString("admin","");
    if (admin_password.length() != 32) { admin_password = random_secret(); if (prefs.putString("admin",admin_password) != admin_password.length()) abort(); }
    csrf = random_secret();
    Serial.printf("Settings login: admin / %s (also setup AP password)\n",admin_password.c_str());
}
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>FTMS Proxy settings</title>
<style>body{font:17px system-ui;margin:24px auto;max-width:580px;padding:0 18px;background:#f5f6f8;color:#152331}label{display:block;margin:16px 0}input:not([type=checkbox]){display:block;box-sizing:border-box;width:100%;padding:10px;font:inherit}button{padding:12px;font:inherit;margin:8px 8px 8px 0}output{display:block;white-space:pre-line;padding:12px;background:white;border-radius:8px}small{display:block}#message{min-height:2em}</style>
<h1>FTMS Proxy</h1><output id="stats">Connecting…</output><form id="settings">
<label><input id="enabled" name="enabled" type="checkbox"> Enable grade filtering</label>
<label>Descent multiplier <input id="descent" name="descent" type="number" min="0" max="2" step="0.01" required></label>
<label>Climb multiplier <input id="climb" name="climb" type="number" min="0" max="2" step="0.01" required></label>
<label>Climb cap (%) <input id="cap" name="cap" type="number" min="0" max="30" step="0.01" required><small>0 disables the cap.</small></label>
<label>Crest smoothing (ms) <input id="crest" name="crest" type="number" min="0" max="10000" step="1" required><small>0 disables smoothing. Applied on incoming grade commands; it does not send commands on its own.</small></label>
<label>Trainer name contains <input id="target" name="target" maxlength="31" required><small>Changing the target requires a reboot.</small></label>
<button>Save settings</button></form><h2>Wi-Fi</h2><form id="wifi"><label>Network name <input name="ssid" maxlength="32" autocomplete="off" required></label><label>Password <input name="password" type="password" maxlength="63" autocomplete="new-password"><small>Leave blank only for an open network. Saved passwords are never returned.</small></label><button>Save Wi-Fi</button></form>
<button id="reboot">Reboot to apply Wi-Fi / trainer changes</button><p id="message" role="status"></p>
<script>
let token='',loaded=false;const el=id=>document.getElementById(id),message=text=>el('message').textContent=text;
async function poll(){try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error('State request failed: '+r.status);const s=await r.json();token=s.token;if(!loaded){for(const k of ['descent','climb','cap','crest','target'])el(k).value=s[k];el('enabled').checked=s.enabled;loaded=true;}el('stats').textContent=`Trainer: ${s.trainer?'connected':'searching'} · Zwift: ${s.zwift?'connected':'disconnected'}\nGrade: ${(s.requested/100).toFixed(2)}% requested → ${(s.sent/100).toFixed(2)}% sent\nLast command: ${s.command_ms} ms · Heap: ${s.heap} bytes (minimum ${s.min_heap})\nWi-Fi: ${s.ap?'setup hotspot':'home network'} · ${s.ip}`;}catch(e){el('stats').textContent='Connection unavailable: '+e.message;}}
async function post(path,data){data.set('token',token);const r=await fetch(path,{method:'POST',body:data});const text=await r.text();message(text);return r.ok;}
el('settings').onsubmit=async e=>{e.preventDefault();const d=new FormData(e.target);d.set('enabled',el('enabled').checked?'1':'0');try{await post('/api/settings',d);}catch(e){message(e.message);}};
el('wifi').onsubmit=async e=>{e.preventDefault();try{if(await post('/api/wifi',new FormData(e.target)))e.target.reset();}catch(e){message(e.message);}};
el('reboot').onclick=async()=>{if(confirm('Reboot the proxy? Finish your ride first.'))try{await post('/api/reboot',new FormData());}catch(e){message(e.message);}};
poll();setInterval(poll,3000);
</script></html>)HTML";
static String json_string(const char* p) {
    String out = "\""; for (;*p;p++) { unsigned char c=*p; if (c=='"' || c=='\\') { out+='\\'; out+=char(c); } else if (c<32) { char esc[7]; snprintf(esc,sizeof(esc),"\\u%04x",c); out+=esc; } else out+=char(c); } return out+'"';
}
static bool authorized(bool mutate = false) {
    if (!web.authenticate("admin",admin_password.c_str())) { web.requestAuthentication(BASIC_AUTH,"FTMS Proxy"); return false; }
    if (mutate) {
        String host = web.hostHeader();
        bool local_host = host == WiFi.localIP().toString() || host == WiFi.softAPIP().toString() || host == "ftms-proxy.local";
        String origin = web.header("Origin");
        if (!local_host || (origin.length() && origin != "http://"+host) || web.arg("token") != csrf) { web.send(403,"text/plain","Reload settings before saving"); return false; }
    }
    web.sendHeader("Cache-Control","no-store"); web.sendHeader("X-Content-Type-Options","nosniff"); return true;
}
static bool number(const char* name, float& value) {
    String text=web.arg(name); if (!text.length() || text.length()>16) return false;
    char* end; errno=0; float parsed = strtof(text.c_str(),&end);
    if (errno || *end || !std::isfinite(parsed)) return false; value=parsed; return true;
}
static void start_ap() {
    WiFi.disconnect(false); WiFi.mode(WIFI_AP);
    ap_name = "FTMS-Setup-" + WiFi.macAddress().substring(12); ap_name.replace(":","");
    ap = WiFi.softAP(ap_name.c_str(),admin_password.c_str(),1,false,2);
    if (ap) dns.start(53,"*",WiFi.softAPIP());
    Serial.printf("Setup hotspot: %s; open http://192.168.4.1/\n",ap_name.c_str());
}
static void web_task(void*) {
    bool joined = false, mdns_ready = false, disabled = false;
    for (;;) {
        const int request = wifi_request.exchange(0);
        if (request == 1) { dns.stop(); MDNS.end(); WiFi.mode(WIFI_OFF); disabled = true; ap = false; Serial.println("Wi-Fi off; BLE continues. Use wifi on or reboot to restore."); }
        if (request == 2) { disabled = false; joined = mdns_ready = false; disconnected_at = 0; wifi_start = millis(); WiFi.mode(WIFI_STA); if (network.ssid[0]) WiFi.begin(network.ssid,network.password); else start_ap(); }
        if (disabled) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (!ap) {
            if (WiFi.status() == WL_CONNECTED) {
                joined = true; disconnected_at = 0;
                if (!mdns_ready && MDNS.begin("ftms-proxy")) { MDNS.addService("http","tcp",80); mdns_ready = true; Serial.printf("Settings: http://%s/ or http://ftms-proxy.local/\n",WiFi.localIP().toString().c_str()); }
            } else if (!joined) {
                if (uint32_t(millis()-wifi_start)>20000) start_ap();
            } else if (!disconnected_at) disconnected_at=millis();
            else if (uint32_t(millis()-disconnected_at)>30000) start_ap();
        }
        if (ap) dns.processNextRequest();
        web.handleClient();
        if (restart_requested) { delay(500); ESP.restart(); }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void web_begin() {
    WiFi.persistent(false); // credentials are stored once in our NVS blob
    WiFi.mode(WIFI_STA);
    if (network.ssid[0]) { WiFi.begin(network.ssid,network.password); wifi_start=millis(); }
    else start_ap();
    const char* headers[] = {"Origin"}; web.collectHeaders(headers,1);
    web.on("/",HTTP_GET,[]{if (authorized()) web.send_P(200,"text/html; charset=utf-8",PAGE);});
    web.on("/api/state",HTTP_GET,[]{
        if (!authorized()) return;
        Settings s=settings_snapshot(); char values[600];
        snprintf(values,sizeof(values),"{\"descent\":%.4f,\"climb\":%.4f,\"cap\":%.4f,\"crest\":%lu,\"enabled\":%s,\"trainer\":%s,\"zwift\":%s,\"requested\":%ld,\"sent\":%ld,\"command_ms\":%lu,\"result\":%lu,\"last_opcode\":%lu,\"heap\":%lu,\"min_heap\":%lu,\"mtu_drops\":%lu,\"web_stack\":%lu,",s.descent,s.climb,s.climb_cap,(unsigned long)s.crest_ms,s.enabled?"true":"false",g_trainer_connected?"true":"false",g_zwift_connected?"true":"false",(long)STAT_GET(requested_grade),(long)STAT_GET(sent_grade),(unsigned long)STAT_GET(command_ms),(unsigned long)STAT_GET(last_result),(unsigned long)STAT_GET(last_opcode),(unsigned long)ESP.getFreeHeap(),(unsigned long)ESP.getMinFreeHeap(),(unsigned long)STAT_GET(mtu_drops),(unsigned long)uxTaskGetStackHighWaterMark(nullptr));
        String body=values; body+="\"target\":"+json_string(s.target)+",\"token\":"+json_string(csrf.c_str())+",\"ap\":"+(ap?"true":"false")+",\"ip\":"+json_string((ap?WiFi.softAPIP():WiFi.localIP()).toString().c_str())+"}";
        web.send(200,"application/json",body);
    });
    web.on("/api/settings",HTTP_POST,[]{
        if (!authorized(true)) return;
        Settings s=settings_snapshot(); float crest;
        String target=web.arg("target"), enabled=web.arg("enabled");
        if (!number("descent",s.descent) || !number("climb",s.climb) || !number("cap",s.climb_cap) || !number("crest",crest) || crest<0 || crest>10000 || floorf(crest)!=crest || !target.length() || target.length()>31 || (enabled!="0" && enabled!="1")) { web.send(400,"text/plain","Invalid settings"); return; }
        for (size_t i=0;i<target.length();i++) if (uint8_t(target[i])<32) {web.send(400,"text/plain","Invalid trainer name");return;}
        s.crest_ms=uint32_t(crest); s.enabled=enabled=="1"; target.toCharArray(s.target,sizeof(s.target));
        if (!valid_settings(s)) {web.send(400,"text/plain","Scale must be 0–2; cap 0–30%; smoothing 0–10000 ms");return;}
        bool ok=settings_save(s); web.send(ok?200:500,"text/plain",ok?"Settings saved; trainer name takes effect after reboot":"Settings save failed");
    });
    web.on("/api/wifi",HTTP_POST,[]{
        if (!authorized(true)) return;
        String ssid=web.arg("ssid"), pass=web.arg("password");
        if (!ssid.length() || ssid.length()>32 || pass.length()>63 || (pass.length() && pass.length()<8)) {web.send(400,"text/plain","SSID: 1–32 bytes; password: empty or 8–63 bytes");return;}
        WifiCredentials next; ssid.toCharArray(next.ssid,sizeof(next.ssid)); pass.toCharArray(next.password,sizeof(next.password));
        bool ok=prefs.putBytes("wifi",&next,sizeof(next))==sizeof(next); web.send(ok?200:500,"text/plain",ok?"Wi-Fi saved. Reboot when not riding.":"Wi-Fi save failed");
    });
    web.on("/api/reboot",HTTP_POST,[]{if(!authorized(true))return;if(g_zwift_connected){web.send(409,"text/plain","Disconnect Zwift before rebooting");return;}web.send(200,"text/plain","Rebooting");restart_requested=true;});
    web.onNotFound([]{if(web.uri().startsWith("/api/")){web.send(404,"text/plain","Not found");return;}web.sendHeader("Location",String("http://")+(ap?WiFi.softAPIP():WiFi.localIP()).toString()+"/",true);web.send(302,"text/plain","");});
    web.begin();
    if (xTaskCreatePinnedToCore(web_task,"settings",8192,nullptr,1,nullptr,1)!=pdPASS) {Serial.println("Web task allocation failed");abort();}
}
