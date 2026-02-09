// Host-side unit tests for gradient_filter.
// Compile and run: g++ -std=c++11 -o test_gradient_filter test_gradient_filter.cpp gradient_filter.cpp && ./test_gradient_filter

#include "gradient_filter.h"
#include "ftms_defs.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-50s ", #name); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (_a != _b) { \
            printf("FAIL (expected %d, got %d)\n", (int)_b, (int)_a); \
            return; \
        } \
    } while(0)

#define ASSERT_TRUE(x) \
    do { \
        if (!(x)) { \
            printf("FAIL (expected true)\n"); \
            return; \
        } \
    } while(0)

#define ASSERT_FALSE(x) \
    do { \
        if ((x)) { \
            printf("FAIL (expected false)\n"); \
            return; \
        } \
    } while(0)

// Helper: build a sim params packet
static void make_sim_packet(uint8_t* buf, int16_t wind_speed, int16_t grade,
                            uint8_t rolling_res, uint8_t wind_res) {
    buf[0] = FTMS_OP_SET_INDOOR_BIKE_SIM;
    buf[1] = (uint8_t)(wind_speed & 0xFF);
    buf[2] = (uint8_t)((wind_speed >> 8) & 0xFF);
    buf[3] = (uint8_t)(grade & 0xFF);
    buf[4] = (uint8_t)((grade >> 8) & 0xFF);
    buf[5] = rolling_res;
    buf[6] = wind_res;
}

static int16_t read_grade(const uint8_t* buf) {
    return (int16_t)(buf[3] | (buf[4] << 8));
}

// --- filter_grade tests ---

void test_positive_grade_unchanged() {
    TEST(filter_grade: positive grade unchanged);
    ASSERT_EQ(filter_grade(500, 0.5), 500);
    PASS();
}

void test_zero_grade_unchanged() {
    TEST(filter_grade: zero grade unchanged);
    ASSERT_EQ(filter_grade(0, 0.5), 0);
    PASS();
}

void test_negative_grade_scaled() {
    TEST(filter_grade: negative grade scaled);
    ASSERT_EQ(filter_grade(-800, 0.5), -400);
    PASS();
}

void test_negative_grade_scale_zero() {
    TEST(filter_grade: scale 0.0 flattens descent);
    ASSERT_EQ(filter_grade(-800, 0.0), 0);
    PASS();
}

void test_negative_grade_scale_one() {
    TEST(filter_grade: scale 1.0 leaves descent unchanged);
    ASSERT_EQ(filter_grade(-800, 1.0), -800);
    PASS();
}

void test_negative_grade_small() {
    TEST(filter_grade: small negative grade);
    // -1 * 0.5 = -0.5, truncated to 0
    ASSERT_EQ(filter_grade(-1, 0.5), 0);
    PASS();
}

void test_negative_grade_odd() {
    TEST(filter_grade: odd negative grade rounds toward zero);
    // -3 * 0.5 = -1.5, truncated to -1
    ASSERT_EQ(filter_grade(-3, 0.5), -1);
    PASS();
}

void test_extreme_negative() {
    TEST(filter_grade: INT16_MIN with scale 0.5);
    int16_t result = filter_grade(-32768, 0.5);
    // -32768 * 0.5 = -16384
    ASSERT_EQ(result, -16384);
    PASS();
}

void test_max_positive() {
    TEST(filter_grade: INT16_MAX unchanged);
    ASSERT_EQ(filter_grade(32767, 0.5), 32767);
    PASS();
}

void test_scale_quarter() {
    TEST(filter_grade: scale 0.25);
    ASSERT_EQ(filter_grade(-1000, 0.25), -250);
    PASS();
}

// --- process_sim_params tests ---

void test_process_modifies_negative_grade() {
    TEST(process_sim_params: modifies negative grade);
    uint8_t buf[7];
    make_sim_packet(buf, 0, -800, 50, 100);
    ASSERT_TRUE(process_sim_params(buf, 7, 0.5));
    ASSERT_EQ(read_grade(buf), -400);
    PASS();
}

void test_process_leaves_positive_grade() {
    TEST(process_sim_params: leaves positive grade);
    uint8_t buf[7];
    make_sim_packet(buf, 0, 500, 50, 100);
    ASSERT_FALSE(process_sim_params(buf, 7, 0.5));
    ASSERT_EQ(read_grade(buf), 500);
    PASS();
}

void test_process_leaves_zero_grade() {
    TEST(process_sim_params: leaves zero grade);
    uint8_t buf[7];
    make_sim_packet(buf, 0, 0, 50, 100);
    ASSERT_FALSE(process_sim_params(buf, 7, 0.5));
    ASSERT_EQ(read_grade(buf), 0);
    PASS();
}

void test_process_preserves_other_fields() {
    TEST(process_sim_params: preserves other fields);
    uint8_t buf[7];
    make_sim_packet(buf, 1234, -800, 42, 99);
    process_sim_params(buf, 7, 0.5);
    // Wind speed preserved
    int16_t wind = (int16_t)(buf[1] | (buf[2] << 8));
    ASSERT_EQ(wind, 1234);
    // Rolling resistance preserved
    ASSERT_EQ(buf[5], 42);
    // Wind resistance preserved
    ASSERT_EQ(buf[6], 99);
    PASS();
}

void test_process_rejects_wrong_opcode() {
    TEST(process_sim_params: rejects wrong opcode);
    uint8_t buf[7];
    make_sim_packet(buf, 0, -800, 50, 100);
    buf[0] = 0x05; // target power opcode
    ASSERT_FALSE(process_sim_params(buf, 7, 0.5));
    PASS();
}

void test_process_rejects_short_packet() {
    TEST(process_sim_params: rejects short packet);
    uint8_t buf[7];
    make_sim_packet(buf, 0, -800, 50, 100);
    ASSERT_FALSE(process_sim_params(buf, 4, 0.5)); // too short
    PASS();
}

void test_process_realistic_descent() {
    TEST(process_sim_params: realistic -12% descent);
    uint8_t buf[7];
    // -12.00% = -1200 in 0.01% units
    make_sim_packet(buf, 500, -1200, 40, 51);
    ASSERT_TRUE(process_sim_params(buf, 7, 0.5));
    ASSERT_EQ(read_grade(buf), -600); // -6.00%
    PASS();
}

void test_process_gentle_descent() {
    TEST(process_sim_params: gentle -1% descent);
    uint8_t buf[7];
    make_sim_packet(buf, 0, -100, 40, 51);
    ASSERT_TRUE(process_sim_params(buf, 7, 0.5));
    ASSERT_EQ(read_grade(buf), -50); // -0.50%
    PASS();
}

int main() {
    printf("gradient_filter tests\n");
    printf("=====================\n\n");

    printf("filter_grade:\n");
    test_positive_grade_unchanged();
    test_zero_grade_unchanged();
    test_negative_grade_scaled();
    test_negative_grade_scale_zero();
    test_negative_grade_scale_one();
    test_negative_grade_small();
    test_negative_grade_odd();
    test_extreme_negative();
    test_max_positive();
    test_scale_quarter();

    printf("\nprocess_sim_params:\n");
    test_process_modifies_negative_grade();
    test_process_leaves_positive_grade();
    test_process_leaves_zero_grade();
    test_process_preserves_other_fields();
    test_process_rejects_wrong_opcode();
    test_process_rejects_short_packet();
    test_process_realistic_descent();
    test_process_gentle_descent();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
