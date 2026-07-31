// Tests for switchGroups parsing.
//
// Switch groups are consumed by the Lua rules engine in ppuc via
// ppuc.switchGroupState/Closing/Opening. The `buttons` group is special: it is
// built from switches marked `button: true` and must not be overridable from
// YAML, because ball search and switch-refresh idle detection depend on it.

#include <algorithm>

#include "ConfigFixture.h"

using ppuc_test::TempYaml;
using ppuc_test::ValidConfig;

namespace {

// Loads a configuration and returns the parsed groups. Fails the test if the
// configuration was rejected.
std::unordered_map<std::string, std::vector<uint16_t>> LoadGroups(
    const std::string& yaml) {
  TempYaml file(yaml);
  PPUC ppuc;
  ppuc.LoadConfiguration(file.path());
  return ppuc.GetSwitchGroups();
}

bool Contains(const std::vector<uint16_t>& haystack, uint16_t needle) {
  return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

std::string WithGroups(const std::string& groups) {
  return ValidConfig() + groups;
}

}  // namespace

TEST_CASE("switchGroups are optional") {
  const auto groups = LoadGroups(ValidConfig());
  // No explicit groups declared, but `buttons` is synthesised.
  CHECK(groups.count("buttons") == 1);
}

TEST_CASE("the built-in buttons group is derived from button: true switches") {
  const auto groups = LoadGroups(ValidConfig());
  REQUIRE(groups.count("buttons") == 1);

  const auto& buttons = groups.at("buttons");
  // Switch 11 is marked `button: true` in ValidConfig(); switch 12 is not.
  CHECK(Contains(buttons, 11));
  CHECK_FALSE(Contains(buttons, 12));
}

TEST_CASE("declared groups are parsed") {
  const auto groups = LoadGroups(WithGroups(R"YAML(
switchGroups:
  playfield:
    switches: [11, 12]
)YAML"));

  REQUIRE(groups.count("playfield") == 1);
  const auto& playfield = groups.at("playfield");
  CHECK(Contains(playfield, 11));
  CHECK(Contains(playfield, 12));
}

TEST_CASE("multiple groups coexist") {
  const auto groups = LoadGroups(WithGroups(R"YAML(
switchGroups:
  playfield:
    switches: [12]
  standups:
    switches: [11]
)YAML"));

  CHECK(groups.count("playfield") == 1);
  CHECK(groups.count("standups") == 1);
  CHECK(groups.count("buttons") == 1);
}

TEST_CASE("declaring the buttons group is rejected outright") {
  // `buttons` is derived from switches marked `button: true` and drives
  // ball-search suppression and switch-refresh idle detection. Rather than
  // silently ignoring an override, validation rejects the file — which is the
  // stronger behaviour, since a silently ignored group would look like it
  // worked.
  const auto error = ppuc_test::LoadAndCaptureError(WithGroups(R"YAML(
switchGroups:
  buttons:
    switches: [12]
)YAML"));

  CHECK_FALSE(error.empty());
  CHECK(error.find("buttons") != std::string::npos);
  CHECK(error.find("reserved") != std::string::npos);
}
