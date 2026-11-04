#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
extern uint32_t fake_time;
inline uint32_t millis() { return fake_time; }
struct SerialStub { void println(const char*) {} template<typename... T> void printf(const char*,T...) {} };
static SerialStub Serial;
