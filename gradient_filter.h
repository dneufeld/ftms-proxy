#ifndef GRADIENT_FILTER_H
#define GRADIENT_FILTER_H

#include <stdint.h>

// Scale a grade value. If negative (descent), multiply by scale_factor.
// grade is in FTMS units (0.01%), e.g. -800 = -8.00%
int16_t filter_grade(int16_t grade, float scale_factor);

// Process a full Indoor Bike Simulation Parameters packet (opcode 0x11).
// Modifies the grade field in-place if it's negative.
// Returns true if the grade was modified.
bool process_sim_params(uint8_t* data, size_t len, float scale_factor);

#endif // GRADIENT_FILTER_H
