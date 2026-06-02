// Host-native unit tests for ota_version.h — the auto-update version logic.
// Run with: pio test -e native
#include <unity.h>
#include "ota_version.h"

using namespace ota_version;

// ── is_newer: basic ordering ───────────────────────────────────────────────
void test_newer_minor(void) {
  TEST_ASSERT_TRUE(is_newer("v0.2", "v0.1"));
  TEST_ASSERT_FALSE(is_newer("v0.1", "v0.2"));
}
void test_equal_is_not_newer(void) {
  TEST_ASSERT_FALSE(is_newer("v0.1", "v0.1"));
  TEST_ASSERT_FALSE(is_newer("v1.5", "v1.5"));
}
void test_newer_major_beats_minor(void) {
  TEST_ASSERT_TRUE(is_newer("v1.0", "v0.9"));
  TEST_ASSERT_FALSE(is_newer("v0.9", "v1.0"));
}
void test_multi_digit_minor(void) {
  // 10 > 9 numerically (not string compare)
  TEST_ASSERT_TRUE(is_newer("v0.10", "v0.9"));
  TEST_ASSERT_FALSE(is_newer("v0.9", "v0.10"));
}

// ── is_newer: auto-deploy 'a' suffix is ignored in comparison ───────────────
void test_auto_suffix_ignored_in_compare(void) {
  TEST_ASSERT_TRUE(is_newer("v0.7a", "v0.6"));   // 0.7 > 0.6
  TEST_ASSERT_FALSE(is_newer("v0.6a", "v0.6"));  // 0.6 == 0.6 → not newer
  TEST_ASSERT_FALSE(is_newer("v0.6a", "v0.6a")); // equal → not newer (loop guard)
}

// ── is_newer: fail-safe on bad input ────────────────────────────────────────
void test_bad_input_not_newer(void) {
  TEST_ASSERT_FALSE(is_newer("", "v0.1"));
  TEST_ASSERT_FALSE(is_newer("garbage", "v0.1"));
  TEST_ASSERT_FALSE(is_newer(nullptr, "v0.1"));
  TEST_ASSERT_FALSE(is_newer("v0.2", "")); // current unparseable → not newer
}
void test_leading_v_optional(void) {
  TEST_ASSERT_TRUE(is_newer("0.2", "0.1"));   // no leading v
  TEST_ASSERT_TRUE(is_newer("V0.2", "v0.1")); // capital V
}
void test_missing_minor(void) {
  TEST_ASSERT_TRUE(is_newer("v1", "v0.9"));   // v1 == v1.0 > v0.9
  TEST_ASSERT_FALSE(is_newer("v1", "v1.0"));  // equal
}

// ── is_auto_tag ─────────────────────────────────────────────────────────────
void test_auto_tag_detection(void) {
  TEST_ASSERT_TRUE(is_auto_tag("v0.7a"));
  TEST_ASSERT_TRUE(is_auto_tag("v1.0a"));
  TEST_ASSERT_FALSE(is_auto_tag("v0.7"));
  TEST_ASSERT_FALSE(is_auto_tag("v1.0"));
  TEST_ASSERT_FALSE(is_auto_tag(""));
  TEST_ASSERT_FALSE(is_auto_tag(nullptr));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_newer_minor);
  RUN_TEST(test_equal_is_not_newer);
  RUN_TEST(test_newer_major_beats_minor);
  RUN_TEST(test_multi_digit_minor);
  RUN_TEST(test_auto_suffix_ignored_in_compare);
  RUN_TEST(test_bad_input_not_newer);
  RUN_TEST(test_leading_v_optional);
  RUN_TEST(test_missing_minor);
  RUN_TEST(test_auto_tag_detection);
  return UNITY_END();
}
