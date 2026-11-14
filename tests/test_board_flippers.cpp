// Tests for board-controlled flippers (`boardFlippers: true`).
//
// The flipper windings are picked by the rule the board uses for a flipper
// (FastSwitchOutputHolds), and a game whose flipper button or EOS is on a
// different board than the winding is refused: the mode exists so that nothing
// on the bus can drop a flipper.

#include <algorithm>

#include "ConfigFixture.h"

using ppuc_test::CaptureStdout;
using ppuc_test::LoadAndCaptureError;
using ppuc_test::TempYaml;
using ppuc_test::ValidConfig;

namespace {

// The base config plus a flipper button and EOS on board 1 (GPIO 5 and 6), a
// power and a hold winding, and a sling.
std::string FlipperConfig(const std::string& boardFlippers,
                          const std::string& eosBoard = "1",
                          const std::string& buttonBoard = "1") {
  std::string yaml = ValidConfig();
  if (!boardFlippers.empty()) {
    yaml += "boardFlippers: " + boardFlippers + "\n";
  }
  // Appended to the base config's switches sequence.
  const std::string switches = R"YAML(
  -
    description: 'Left Flipper Button'
    number: 114
    board: )YAML" + buttonBoard + R"YAML(
    port: 5
    debounce: 10
    debounceMode: fastFlip
  -
    description: 'Left Flipper EOS'
    number: 113
    board: )YAML" + eosBoard + R"YAML(
    port: 6
    debounce: 10
    debounceMode: standard
  -
    description: 'Left Sling'
    number: 61
    board: 1
    port: 7
    debounce: 10
    debounceMode: fastFlip
)YAML";
  yaml.insert(yaml.find("\n", yaml.find("debounceMode: standard\n",
                                        yaml.find("'OUTHOLE'"))) ,
              switches.substr(0, switches.size() - 1));
  yaml += R"YAML(pwmOutput:
  -
    description: 'Left Flipper Power'
    board: 1
    port: 19
    number: 47
    power: 255
    minPulseTime: 0
    maxPulseTime: 40
    holdPower: 0
    holdPowerActivationTime: 0
    fastFlipSwitch: 114
    stopSwitches:
      - 113
    type: coil
  -
    description: 'Left Flipper Hold'
    board: 1
    port: 20
    number: 48
    power: 255
    minPulseTime: 0
    maxPulseTime: 0
    holdPower: 0
    holdPowerActivationTime: 0
    fastFlipSwitch: 114
    holdWinding: true
    type: coil
  -
    description: 'Left Sling'
    board: 1
    port: 21
    number: 9
    power: 192
    minPulseTime: 0
    maxPulseTime: 150
    holdPower: 0
    holdPowerActivationTime: 0
    fastFlipSwitch: 61
    type: coil
)YAML";
  return yaml;
}

std::vector<uint16_t> BoardFlipperCoils(const std::string& yaml) {
  TempYaml file(yaml);
  std::vector<uint16_t> coils;
  CaptureStdout([&] {
    PPUC ppuc;
    ppuc.LoadConfiguration(file.path());
    coils = ppuc.GetBoardFlipperCoils();
  });
  std::sort(coils.begin(), coils.end());
  return coils;
}

}  // namespace

TEST_CASE("the flipper windings are board flippers, a sling is not") {
  const auto yaml = FlipperConfig("true");
  REQUIRE(LoadAndCaptureError(yaml).empty());
  CHECK(BoardFlipperCoils(yaml) == std::vector<uint16_t>{47, 48});
}

namespace {

std::string Without(std::string yaml, const std::string& part) {
  const auto pos = yaml.find(part);
  REQUIRE_MESSAGE(pos != std::string::npos, "not in the config: " << part);
  return yaml.erase(pos, part.size());
}

const char* kEos = "    stopSwitches:\n      - 113\n";

}  // namespace

TEST_CASE("a power winding without an EOS is still a board flipper") {
  // A fixed pulse on the power winding instead of an EOS: on its own it looks
  // like a sling, but its button holds the hold winding.
  const auto yaml = Without(FlipperConfig("true"), kEos);
  REQUIRE(LoadAndCaptureError(yaml).empty());
  CHECK(BoardFlipperCoils(yaml) == std::vector<uint16_t>{47, 48});
}

TEST_CASE("a lone pulsed coil on a button is not a flipper") {
  // No EOS and no hold winding on the button: that is a sling's shape.
  std::string yaml = Without(FlipperConfig("true"), kEos);
  const auto hold = yaml.find("  -\n    description: 'Left Flipper Hold'");
  const auto sling = yaml.find("  -\n    description: 'Left Sling'\n    board: 1\n    port: 21");
  REQUIRE(hold != std::string::npos);
  REQUIRE(sling != std::string::npos);
  yaml.erase(hold, sling - hold);
  const auto error = LoadAndCaptureError(yaml);
  CHECK(error.find("no pwmOutput is a flipper winding") != std::string::npos);
}

TEST_CASE("without boardFlippers nothing changes") {
  CHECK(BoardFlipperCoils(FlipperConfig("")).empty());
  CHECK(BoardFlipperCoils(FlipperConfig("false")).empty());
}

TEST_CASE("boardFlippers must be a boolean") {
  const auto error = LoadAndCaptureError(FlipperConfig("sometimes"));
  CHECK(error.find("boardFlippers") != std::string::npos);
}

TEST_CASE("a flipper button on another board is refused") {
  const auto error = LoadAndCaptureError(FlipperConfig("true", "1", "2"));
  CHECK(error.find("switch 114 is on board 2") != std::string::npos);
}

TEST_CASE("a flipper EOS on another board is refused") {
  const auto error = LoadAndCaptureError(FlipperConfig("true", "2", "1"));
  CHECK(error.find("switch 113 is on board 2") != std::string::npos);
}

TEST_CASE("boardFlippers without any flipper winding is refused") {
  std::string yaml = ValidConfig() + "boardFlippers: true\n";
  const auto error = LoadAndCaptureError(yaml);
  CHECK(error.find("no pwmOutput is a flipper winding") != std::string::npos);
}
