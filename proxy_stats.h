#pragma once
#include <stdint.h>
// Callbacks use atomic increments/loads; web never reads partially updated values.
struct ProxyStats {
    uint32_t packets_forwarded = 0, grades_modified = 0, write_errors = 0;
    uint32_t trainer_connects = 0, trainer_disconnects = 0;
    uint32_t zwift_connects = 0, zwift_disconnects = 0, notifications_relayed = 0;
    int32_t requested_grade = 0, sent_grade = 0;
    uint32_t command_ms = 0, mtu_drops = 0, last_result = 0, last_opcode = 0;
};
extern ProxyStats g_stats;
#define STAT_INC(field) __atomic_add_fetch(&g_stats.field, 1, __ATOMIC_RELAXED)
#define STAT_SET(field, value) __atomic_store_n(&g_stats.field, value, __ATOMIC_RELAXED)
#define STAT_GET(field) __atomic_load_n(&g_stats.field, __ATOMIC_RELAXED)
