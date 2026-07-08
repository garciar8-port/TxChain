// Sentinel test — proves the build/test harness is live and that txchain_core
// links into a test binary via gtest_discover_tests. Real unit/golden tests
// (serializer round-trip, SHA-256 KAT, the three golden vectors) arrive with
// their owning M0 tickets (CRE-189 SHA-256, CRE-191 address/hex, CRE-192
// serializer + golden vectors).

#include <gtest/gtest.h>

#include "txchain/config.hpp"
#include "txchain/crypto.hpp"

// The harness itself runs.
TEST(Sentinel, HarnessIsLive) {
  EXPECT_EQ(2 + 2, 4);
}

// txchain_core is linked and its symbols are reachable from a test binary,
// proving the library boundary (Architecture §7.1) is wired end to end.
TEST(Sentinel, CoreLibraryLinks) {
  EXPECT_STREQ(txchain::config::module_name(), "config");
  EXPECT_STREQ(txchain::crypto::module_name(), "crypto");
}
