#include <unity.h>
#include "geo.h"

void setUp() {}
void tearDown() {}

// University of Waterloo, roughly
static const float LAT = 43.4723f;
static const float LON = -80.5449f;

// Degrees of latitude per meter on a 6371 km sphere
static const float DEG_PER_M = 1.0f / 111194.9f;

void test_same_point_is_zero() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, haversine_distance(LAT, LON, LAT, LON));
}

void test_one_degree_of_latitude() {
    TEST_ASSERT_FLOAT_WITHIN(50.0f, 111195.0f, haversine_distance(43.0f, LON, 44.0f, LON));
}

void test_one_degree_of_longitude_at_equator() {
    TEST_ASSERT_FLOAT_WITHIN(50.0f, 111195.0f, haversine_distance(0.0f, 10.0f, 0.0f, 11.0f));
}

void test_is_symmetric() {
    float ab = haversine_distance(LAT, LON, 43.6532f, -79.3832f);
    float ba = haversine_distance(43.6532f, -79.3832f, LAT, LON);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, ab, ba);
}

// main.cpp skips the Places lookup until the user has moved 20 m.
// float lat/lon near Waterloo only resolve to about half a meter.
void test_resolves_the_20m_gate() {
    float below = haversine_distance(LAT, LON, LAT + 15.0f * DEG_PER_M, LON);
    float above = haversine_distance(LAT, LON, LAT + 25.0f * DEG_PER_M, LON);
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 15.0f, below);
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 25.0f, above);
    TEST_ASSERT_TRUE(below < 20.0f);
    TEST_ASSERT_TRUE(above > 20.0f);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_same_point_is_zero);
    RUN_TEST(test_one_degree_of_latitude);
    RUN_TEST(test_one_degree_of_longitude_at_equator);
    RUN_TEST(test_is_symmetric);
    RUN_TEST(test_resolves_the_20m_gate);
    return UNITY_END();
}
