// Tests for pwmOutput validation.
//
// pwmOutput describes the coils, flashers and lamps the boards drive, so a
// configuration mistake here has physical consequences. The fields that govern
// thermal protection (minPulseTime, maxPulseTime, holdPower,
// holdPowerActivationTime) are all validated as *present*; whether their
// *values* are safe is a separate question, covered at the end of this file.

#include "ConfigFixture.h"

using ppuc_test::LoadAndCaptureError;
using ppuc_test::ValidConfig;

namespace {

// A pwmOutput entry with every required field. Individual tests strip one
// field so a failure names the field that was removed.
std::string PwmBlock() {
  return R"YAML(
pwmOutput:
  -
    description: 'Outhole Kicker'
    board: 2
    port: 17
    number: 7
    power: 255
    minPulseTime: 20
    maxPulseTime: 120
    holdPower: 0
    holdPowerActivationTime: 0
    fastFlipSwitch: 0
    type: solenoid
    ballSearch: true
)YAML";
}

std::string WithPwm() { return ValidConfig() + PwmBlock(); }

// Removes `key: value` from the pwmOutput entry.
std::string WithoutField(const std::string& key) {
  const auto yaml = WithPwm();
  const auto needle = "\n    " + key + ":";
  const auto pos = yaml.find(needle);
  REQUIRE_MESSAGE(pos != std::string::npos,
                  "field not present in PwmBlock(): " << key);
  const auto lineEnd = yaml.find('\n', pos + 1);
  std::string result = yaml;
  result.erase(pos, lineEnd - pos);
  return result;
}

}  // namespace

TEST_CASE("a complete pwmOutput entry is accepted") {
  CHECK(LoadAndCaptureError(WithPwm()).empty());
}

TEST_CASE("every required pwmOutput field is enforced") {
  // Driven from the validator's own list. If a field is added to or removed
  // from the required set, this test is where that shows up.
  for (const char* field :
       {"description", "board", "port", "number", "power", "minPulseTime",
        "maxPulseTime", "holdPower", "holdPowerActivationTime",
        "fastFlipSwitch", "type"}) {
    CAPTURE(field);
    const auto error = LoadAndCaptureError(WithoutField(field));
    CHECK_FALSE(error.empty());
    CHECK_MESSAGE(error.find(field) != std::string::npos,
                  "the error should name the missing field");
  }
}

TEST_CASE("ballSearch is optional") {
  CHECK(LoadAndCaptureError(WithoutField("ballSearch")).empty());
}

TEST_CASE("a pwmOutput entry must be a map, not a scalar") {
  auto yaml = ValidConfig();
  yaml += "pwmOutput:\n  - notAMap\n";
  const auto error = LoadAndCaptureError(yaml);
  CHECK_FALSE(error.empty());
  CHECK(error.find("pwmOutput") != std::string::npos);
}

TEST_CASE("pwmOutput itself is optional") {
  // A machine with no PWM outputs at all is a valid, if unusual, config.
  CHECK(LoadAndCaptureError(ValidConfig()).empty());
}

// ---------------------------------------------------------------------------
// Thermal protection: current behaviour, and the known gap
// ---------------------------------------------------------------------------

TEST_CASE("a solenoid with a max pulse time is accepted") {
  CHECK(LoadAndCaptureError(WithPwm()).empty());
}

TEST_CASE("a solenoid relying on hold power is accepted") {
  // maxPulseTime 0 is correct here: holdPower drops the duty cycle to a
  // current the coil can dissipate indefinitely, so unlimited on-time is safe.
  auto yaml = WithPwm();
  const auto pos = yaml.find("    maxPulseTime: 120");
  REQUIRE(pos != std::string::npos);
  yaml.replace(pos, std::string("    maxPulseTime: 120").size(),
               "    maxPulseTime: 0");
  const auto hp = yaml.find("    holdPower: 0");
  REQUIRE(hp != std::string::npos);
  yaml.replace(hp, std::string("    holdPower: 0").size(),
               "    holdPower: 40");

  CHECK(LoadAndCaptureError(yaml).empty());
}

TEST_CASE("KNOWN GAP: a solenoid with no thermal protection is accepted") {
  // maxPulseTime 0 with holdPower 0 means nothing bounds how long this coil
  // can stay energised. For a single-winding coil that is a fire risk; for a
  // dual-wound coil with an EOS contact it is correct, and the configuration
  // cannot currently express the difference.
  //
  // This test characterises today's behaviour rather than endorsing it. When
  // the validator from STABILIZATION_PLAN.md 2.2 lands, this test SHOULD fail
  // and must be rewritten to assert rejection. Failing here is the reminder.
  auto yaml = WithPwm();
  const auto pos = yaml.find("    maxPulseTime: 120");
  REQUIRE(pos != std::string::npos);
  yaml.replace(pos, std::string("    maxPulseTime: 120").size(),
               "    maxPulseTime: 0");

  CHECK_MESSAGE(
      LoadAndCaptureError(yaml).empty(),
      "if this now fails, the coil protection validator has landed - rewrite "
      "this test to assert the rejection instead");
}
