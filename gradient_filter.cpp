#include "gradient_filter.h"
#include "ftms_defs.h"
#include <string.h>

int16_t filter_grade(int16_t grade, float scale_factor) {
    if (grade < 0) {
        return (int16_t)(grade * scale_factor);
    }
    return grade;
}

bool process_sim_params(uint8_t* data, size_t len, float scale_factor) {
    if (len < SIM_PARAMS_LENGTH || data[0] != FTMS_OP_SET_INDOOR_BIKE_SIM) {
        return false;
    }

    // Grade is little-endian sint16 at bytes 3-4
    int16_t grade = (int16_t)(data[SIM_PARAMS_GRADE_OFFSET] |
                              (data[SIM_PARAMS_GRADE_OFFSET + 1] << 8));

    if (grade >= 0) {
        return false;
    }

    int16_t filtered = filter_grade(grade, scale_factor);

    data[SIM_PARAMS_GRADE_OFFSET] = (uint8_t)(filtered & 0xFF);
    data[SIM_PARAMS_GRADE_OFFSET + 1] = (uint8_t)((filtered >> 8) & 0xFF);

    return true;
}
