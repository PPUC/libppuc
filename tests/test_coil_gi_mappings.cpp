// Tests for coilGiMappings parsing.
//
// Williams System 11 games drive GI strings from a coil. ppuc-pinmame watches
// the mapped coil and sets the mapped GI string to onBrightness/offBrightness.
// Brightness values are clamped to the PPUC GI range 0..8.

#include "ConfigFixture.h"
#include "io-boards/PPUCProtocolV2.h"

using ppuc_test::TempYaml;
using ppuc_test::ValidConfig;

namespace {

std::vector<PPUCCoilGiMapping> LoadMappings(const std::string& yaml) {
  TempYaml file(yaml);
  PPUC ppuc;
  ppuc.LoadConfiguration(file.path());
  return ppuc.GetCoilGiMappings();
}

std::string WithMappings(const std::string& mappings) {
  return ValidConfig() + mappings;
}

}  // namespace

TEST_CASE("coilGiMappings are optional") {
  CHECK(LoadMappings(ValidConfig()).empty());
}

TEST_CASE("a single mapping is parsed") {
  const auto mappings = LoadMappings(WithMappings(R"YAML(
coilGiMappings:
  - coil: 12
    gi: 1
    onBrightness: 8
    offBrightness: 0
)YAML"));

  REQUIRE(mappings.size() == 1);
  CHECK(mappings[0].coil == 12);
  CHECK(mappings[0].gi == 1);
  CHECK(mappings[0].onBrightness == 8);
  CHECK(mappings[0].offBrightness == 0);
}

TEST_CASE("one coil may drive several GI strings") {
  const auto mappings = LoadMappings(WithMappings(R"YAML(
coilGiMappings:
  - coil: 12
    gi: 1
    onBrightness: 8
    offBrightness: 0
  - coil: 12
    gi: 2
    onBrightness: 8
    offBrightness: 0
)YAML"));

  REQUIRE(mappings.size() == 2);
  CHECK(mappings[0].coil == 12);
  CHECK(mappings[1].coil == 12);
  CHECK(mappings[0].gi != mappings[1].gi);
}

TEST_CASE("inverted mappings are supported") {
  // offBrightness > onBrightness is legitimate: some strings are lit when the
  // coil is inactive.
  const auto mappings = LoadMappings(WithMappings(R"YAML(
coilGiMappings:
  - coil: 13
    gi: 3
    onBrightness: 0
    offBrightness: 8
)YAML"));

  REQUIRE(mappings.size() == 1);
  CHECK(mappings[0].onBrightness == 0);
  CHECK(mappings[0].offBrightness == 8);
}

TEST_CASE("out-of-range brightness is preserved at parse time") {
  // Clamping happens downstream, not here: PPUC::SetGIState() and
  // RS485Comm::QueueEvent() both apply ppuc::v2::ClampGiLevel() before the
  // value reaches the wire. This test pins the layering so that a future
  // refactor which moves clamping into the parser is a deliberate decision
  // rather than an accident.
  const auto mappings = LoadMappings(WithMappings(R"YAML(
coilGiMappings:
  - coil: 14
    gi: 1
    onBrightness: 15
    offBrightness: 0
)YAML"));

  REQUIRE(mappings.size() == 1);
  CHECK(mappings[0].onBrightness == 15);
}

TEST_CASE("ClampGiLevel enforces the wire contract") {
  // This is the guarantee that actually matters: the v2 payload packs GI into
  // 4 bits with a valid range of 0..8, so nothing above kMaxGiLevel may reach
  // an IO board regardless of what a game YAML asks for.
  CHECK(ppuc::v2::ClampGiLevel(0) == 0);
  CHECK(ppuc::v2::ClampGiLevel(4) == 4);
  CHECK(ppuc::v2::ClampGiLevel(8) == 8);
  CHECK(ppuc::v2::ClampGiLevel(9) == ppuc::v2::kMaxGiLevel);
  CHECK(ppuc::v2::ClampGiLevel(15) == ppuc::v2::kMaxGiLevel);
  CHECK(ppuc::v2::ClampGiLevel(255) == ppuc::v2::kMaxGiLevel);
}
