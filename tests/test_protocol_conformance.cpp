// Runs the shared protocol conformance suite under doctest.
//
// The checks are compiled from io-boards/test/conformance/, staged into
// third-party/include/io-boards/ by external.sh alongside PPUCProtocolV2.h
// itself. io-boards runs the identical code under Unity, so host and firmware
// assert the same wire contract rather than two suites that can drift.

#include "ProtocolConformance.h"
#include "doctest.h"

TEST_CASE("v2 protocol conformance") {
  for (size_t i = 0; i < ppuc_conformance::kCaseCount; ++i) {
    const auto& c = ppuc_conformance::kCases[i];
    CAPTURE(c.name);
    const auto result = c.fn();
    CHECK_MESSAGE(result.ok, c.name << ": " << result.detail);
  }
}
