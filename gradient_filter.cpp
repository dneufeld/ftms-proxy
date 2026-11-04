#include "gradient_filter.h"
#include "ftms_defs.h"
#include <cmath>
#include <algorithm>
static int16_t bounded(float v) {
    return static_cast<int16_t>(std::lround(std::max(-32768.0f, std::min(32767.0f, v))));
}
int16_t filter_grade(int16_t grade, float scale) {
    if (!std::isfinite(scale) || scale < 0 || scale > 2) return grade;
    return grade < 0 ? bounded(grade * scale) : grade;
}
bool process_sim_params(uint8_t* data, size_t len, float scale) {
    if (!data || len != SIM_PARAMS_LENGTH || data[0] != FTMS_OP_SET_INDOOR_BIKE_SIM) return false;
    const int16_t before = static_cast<int16_t>(data[3] | (uint16_t(data[4]) << 8));
    const int16_t after = filter_grade(before, scale);
    data[3] = uint8_t(after); data[4] = uint8_t(uint16_t(after) >> 8);
    return before != after;
}
int16_t GradeFilter::apply(int16_t grade, const Settings& s, uint32_t now) {
    if (!s.enabled || !valid_settings(s)) { reset(); return grade; }
    float target = grade * (grade < 0 ? s.descent : s.climb);
    if (grade > 0 && s.climb_cap > 0) target = std::min(target, s.climb_cap * 100);
    if (grade < 0 && initialized && previous >= 0 && !ramp && s.crest_ms) {
        ramp = true; start = now; from = previous;
    }
    if (grade >= 0 || !s.crest_ms) ramp = false;
    if (ramp) {
        const uint32_t elapsed = now - start; // millis rollover safe
        if (elapsed >= s.crest_ms) ramp = false;
        else target = from + (target - from) * (float(elapsed) / s.crest_ms);
    }
    previous = bounded(target); initialized = true;
    return previous;
}
bool GradeFilter::process(uint8_t* data, size_t len, const Settings& s, uint32_t now) {
    if (!data || len != SIM_PARAMS_LENGTH || data[0] != FTMS_OP_SET_INDOOR_BIKE_SIM) return false;
    const int16_t before = static_cast<int16_t>(data[3] | (uint16_t(data[4]) << 8));
    const int16_t after = apply(before, s, now);
    data[3] = uint8_t(after); data[4] = uint8_t(uint16_t(after) >> 8);
    return before != after;
}
