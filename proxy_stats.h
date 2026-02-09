#ifndef PROXY_STATS_H
#define PROXY_STATS_H

#include <stdint.h>

struct ProxyStats {
    uint32_t packets_forwarded;
    uint32_t grades_modified;
    uint32_t write_errors;
    uint32_t trainer_connects;
    uint32_t trainer_disconnects;
    uint32_t zwift_connects;
    uint32_t zwift_disconnects;
    uint32_t notifications_relayed;
};

extern ProxyStats g_stats;

#endif // PROXY_STATS_H
