#pragma once
#include <stdint.h>
#include <stddef.h>
struct Settings {
    float descent = 0.5f;
    float climb = 1.0f;
    float climb_cap = 0; // percent; zero disables cap
    uint32_t crest_ms = 0;
    bool enabled = true;
    char target[32] = "Tacx";
};
bool valid_settings(const Settings& s);
#ifdef ARDUINO
Settings settings_snapshot();
bool settings_save(const Settings& s);
void settings_begin();
void web_begin();
void web_set_enabled(bool enabled);
#endif
