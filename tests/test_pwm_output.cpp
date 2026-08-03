// Tests for pwmOutput validation.
//
// pwmOutput describes the coils, flashers and lamps the boards drive, so a
// configuration mistake here has physical consequences. The fields that govern
// thermal protection (minPulseTime, maxPulseTime, holdPower,
// holdPowerActivationTime) are all validated as *present*; whether their
// *values* are safe is a separate question, covered at the end of this file.

#include "ConfigFixture.h"

using ppuc_test::LoadAndCaptureError;
using ppuc_test::LoadAndCaptureStdout;
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
  //
  // holdPowerActivationTime matters as much as holdPower. PwmDevices only
  // reduces power when `holdPowerActivationTime > 0`, so a hold power with an
  // activation time of 0 never engages and protects nothing. This test used to
  // set only holdPower and describe the result as safe; it was not.
  auto yaml = WithPwm();
  const auto pos = yaml.find("    maxPulseTime: 120");
  REQUIRE(pos != std::string::npos);
  yaml.replace(pos, std::string("    maxPulseTime: 120").size(),
               "    maxPulseTime: 0");
  const auto hp = yaml.find("    holdPower: 0");
  REQUIRE(hp != std::string::npos);
  yaml.replace(hp, std::string("    holdPower: 0").size(),
               "    holdPower: 40");
  const auto hpat = yaml.find("    holdPowerActivationTime: 0");
  REQUIRE(hpat != std::string::npos);
  yaml.replace(hpat, std::string("    holdPowerActivationTime: 0").size(),
               "    holdPowerActivationTime: 30");

  CHECK(LoadAndCaptureError(yaml).empty());
}

// --- thermal protection (STABILIZATION_PLAN.md 2.2) --------------------------
//
// A solenoid needs at least one of: maxPulseTime, hold power, or its own hold
// winding declared with dualWinding. With none of them, nothing bounds how
// long the coil stays energised.
//
// These warn rather than reject, for one release: every existing game config
// predates dualWinding, so a correctly wired dual-wound flipper currently
// looks exactly like an unprotected kicker. See the note in
// WarnAboutUnprotectedSolenoids().

namespace {

// Replaces `key: <old>` with `key: <new>` in the pwmOutput entry.
std::string WithFieldValue(std::string yaml, const std::string& line,
                           const std::string& replacement) {
  const auto pos = yaml.find(line);
  REQUIRE_MESSAGE(pos != std::string::npos, "fixture no longer contains " << line);
  yaml.replace(pos, line.size(), replacement);
  return yaml;
}

// The fixture coil, stripped of every protection mechanism.
std::string UnprotectedCoil() {
  return WithFieldValue(WithPwm(), "    maxPulseTime: 120",
                        "    maxPulseTime: 0");
}

}  // namespace

TEST_CASE("a solenoid with no thermal protection is warned about") {
  const auto output = LoadAndCaptureStdout(UnprotectedCoil());

  CHECK(output.find("WARNING") != std::string::npos);
  CHECK(output.find("no thermal protection") != std::string::npos);
  // The operator has to be able to find the offending entry.
  CHECK(output.find("Outhole Kicker") != std::string::npos);
  CHECK(output.find("pwmOutput[0]") != std::string::npos);
}

TEST_CASE("an unprotected solenoid is still accepted, for now") {
  // Deliberate: warn for one release, then error. Existing configs predate
  // dualWinding and refusing to start the machine over that costs more than
  // it protects.
  CHECK(LoadAndCaptureError(UnprotectedCoil()).empty());
}

TEST_CASE("maxPulseTime alone counts as protection") {
  CHECK(LoadAndCaptureStdout(WithPwm()).find("no thermal protection") ==
        std::string::npos);
}

TEST_CASE("hold power counts as protection") {
  auto yaml = UnprotectedCoil();
  yaml = WithFieldValue(yaml, "    holdPower: 0", "    holdPower: 64");
  yaml = WithFieldValue(yaml, "    holdPowerActivationTime: 0",
                        "    holdPowerActivationTime: 30");

  CHECK(LoadAndCaptureStdout(yaml).find("no thermal protection") ==
        std::string::npos);
}

TEST_CASE("hold power without an activation time does not count") {
  // A hold power that never engages protects nothing.
  auto yaml = WithFieldValue(UnprotectedCoil(), "    holdPower: 0",
                             "    holdPower: 64");

  CHECK(LoadAndCaptureStdout(yaml).find("no thermal protection") !=
        std::string::npos);
}

TEST_CASE("dualWinding counts as protection") {
  // A dual-wound flipper with an EOS contact holding on its own winding is
  // correct with maxPulseTime 0. This is the case the configuration could not
  // express before.
  const auto yaml = UnprotectedCoil() + "    dualWinding: true\n";

  CHECK(LoadAndCaptureStdout(yaml).find("no thermal protection") ==
        std::string::npos);
}

TEST_CASE("dualWinding false does not count as protection") {
  const auto yaml = UnprotectedCoil() + "    dualWinding: false\n";

  CHECK(LoadAndCaptureStdout(yaml).find("no thermal protection") !=
        std::string::npos);
}

TEST_CASE("an eosSwitch number is accepted alongside dualWinding") {
  const auto yaml =
      UnprotectedCoil() + "    dualWinding: true\n    eosSwitch: 42\n";

  CHECK(LoadAndCaptureError(yaml).empty());
  CHECK(LoadAndCaptureStdout(yaml).find("no thermal protection") ==
        std::string::npos);
}

TEST_CASE("dualWinding must be a boolean") {
  const auto yaml = UnprotectedCoil() + "    dualWinding: maybe\n";

  const auto error = LoadAndCaptureError(yaml);
  CHECK(error.find("dualWinding") != std::string::npos);
}

TEST_CASE("a lamp is not warned about") {
  // Lamps have no thermal protection to speak of and must not drown out the
  // coils that do.
  const auto yaml =
      WithFieldValue(UnprotectedCoil(), "    type: solenoid", "    type: lamp");

  CHECK(LoadAndCaptureStdout(yaml).find("no thermal protection") ==
        std::string::npos);
}

TEST_CASE("a motor is warned about like a solenoid") {
  // Motors move mass and have end-of-stroke contacts; an unbounded pulse is
  // the same class of problem.
  const auto yaml = WithFieldValue(UnprotectedCoil(), "    type: solenoid",
                                   "    type: motor");

  CHECK(LoadAndCaptureStdout(yaml).find("no thermal protection") !=
        std::string::npos);
}

TEST_CASE("a shaker is warned about like a solenoid") {
  const auto yaml = WithFieldValue(UnprotectedCoil(), "    type: solenoid",
                                   "    type: shaker");

  CHECK(LoadAndCaptureStdout(yaml).find("no thermal protection") !=
        std::string::npos);
}
