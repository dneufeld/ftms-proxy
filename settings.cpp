#include "settings.h"
#include <cmath>
#include <cstring>
bool valid_settings(const Settings& s) {
    return std::isfinite(s.descent) && s.descent >= 0 && s.descent <= 2 &&
        std::isfinite(s.climb) && s.climb >= 0 && s.climb <= 2 &&
        std::isfinite(s.climb_cap) && s.climb_cap >= 0 && s.climb_cap <= 30 &&
        s.crest_ms <= 10000 && s.target[0] && std::memchr(s.target, 0, sizeof(s.target));
}
