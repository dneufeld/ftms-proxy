#pragma once
#include <stdint.h>
#include <stddef.h>
#include "settings.h"
int16_t filter_grade(int16_t grade, float scale_factor);
bool process_sim_params(uint8_t* data, size_t len, float scale_factor);
class GradeFilter {
    bool initialized = false, ramp = false;
    uint32_t start = 0;
    int16_t previous = 0, from = 0;
public:
    void reset() { initialized = ramp = false; }
    int16_t apply(int16_t grade, const Settings& s, uint32_t now);
    bool process(uint8_t* data, size_t len, const Settings& s, uint32_t now);
};
