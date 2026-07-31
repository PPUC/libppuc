#pragma once

// Test support for exercising PPUC::LoadConfiguration() against YAML written
// on the fly.
//
// The validation helpers in src/PPUC.cpp live in an anonymous namespace and
// therefore have internal linkage. Rather than change production code to make
// them reachable, these tests drive the real public entry point and assert on
// the std::runtime_error it raises. That tests the behaviour users actually
// get, including the message text they have to debug from.

#include <cstdio>
#include <fstream>
#include <string>

#include "PPUC.h"
#include "doctest.h"

namespace ppuc_test {

// Writes a YAML document to a uniquely named temporary file and removes it
// again on destruction.
class TempYaml {
 public:
  explicit TempYaml(const std::string& contents) {
    static int counter = 0;
    m_path = std::string(P_tmpdir) + "/ppuc_test_" +
             std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" +
             std::to_string(counter++) + ".yml";

    std::ofstream out(m_path);
    REQUIRE_MESSAGE(out.is_open(), "could not create temp file " << m_path);
    out << contents;
    out.close();
  }

  ~TempYaml() { std::remove(m_path.c_str()); }

  TempYaml(const TempYaml&) = delete;
  TempYaml& operator=(const TempYaml&) = delete;

  const char* path() const { return m_path.c_str(); }

 private:
  std::string m_path;
};

// The smallest configuration that passes validation.
//
// Deliberately minimal: `switchMatrix`, `pwmOutput`, `ledStripes`,
// `switchGroups` and `coilGiMappings` are all optional sections, so tests that
// care about them append their own block. A small base means a failing test
// points at the thing the test changed rather than at fixture noise.
//
// The required root fields are enforced in ValidatePpucConfiguration():
// debug, rom, serialPort, platform, coinDoorClosedSwitch, gameOnSolenoid.
inline std::string ValidConfig() {
  return R"YAML(
ppucVersion: 1
rom: testrom
serialPort: dummy
platform: WPC
debug: false
coinDoorClosedSwitch: 22
gameOnSolenoid: 19
boards:
  -
    number: 1
    pollEvents: true
  -
    number: 2
    pollEvents: false
switches:
  -
    description: 'START BUTTON'
    number: 11
    board: 1
    port: 1
    debounce: 5
    debounceMode: standard
    button: true
  -
    description: 'OUTHOLE'
    number: 12
    board: 1
    port: 2
    debounce: 5
    debounceMode: slowStable
)YAML";
}

// Loads a configuration and returns the error message, or an empty string when
// the configuration was accepted.
inline std::string LoadAndCaptureError(const std::string& yaml) {
  TempYaml file(yaml);
  PPUC ppuc;
  try {
    ppuc.LoadConfiguration(file.path());
  } catch (const std::exception& e) {
    return e.what();
  }
  return {};
}

}  // namespace ppuc_test
