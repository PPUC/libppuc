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
#include <iterator>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

// Runs `fn` with stdout redirected to a file and returns what it wrote.
//
// Both loaders route through this so a validation warning never leaks into the
// test runner's output, where it reads like a failure.
template <typename Fn>
inline std::string CaptureStdout(Fn&& fn) {
  static int counter = 0;
  const std::string outPath = std::string(P_tmpdir) + "/ppuc_test_stdout_" +
                              std::to_string(counter++) + ".txt";

  fflush(stdout);
#ifdef _WIN32
  const int savedFd = _dup(_fileno(stdout));
#else
  const int savedFd = dup(fileno(stdout));
#endif
  REQUIRE(savedFd >= 0);
  REQUIRE(freopen(outPath.c_str(), "w", stdout) != nullptr);

  fn();

  fflush(stdout);
#ifdef _WIN32
  _dup2(savedFd, _fileno(stdout));
  _close(savedFd);
#else
  dup2(savedFd, fileno(stdout));
  close(savedFd);
#endif
  clearerr(stdout);

  std::ifstream in(outPath);
  std::string captured((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
  in.close();
  std::remove(outPath.c_str());
  return captured;
}

// Loads a configuration and returns the error message, or an empty string when
// the configuration was accepted. Anything printed along the way is swallowed;
// use LoadAndCaptureStdout when that is what the test is about.
inline std::string LoadAndCaptureError(const std::string& yaml) {
  TempYaml file(yaml);
  std::string error;
  CaptureStdout([&] {
    PPUC ppuc;
    try {
      ppuc.LoadConfiguration(file.path());
    } catch (const std::exception& e) {
      error = e.what();
    }
  });
  return error;
}

// Loads a configuration and returns whatever it printed to stdout.
//
// Warnings are not exceptions, so there is nothing to catch. Rather than
// exposing the warning collector as API purely for tests - which would let the
// tests pass while the message never actually reaches a user - this captures
// the real output of the real entry point, the same text an operator sees in
// their terminal.
inline std::string LoadAndCaptureStdout(const std::string& yaml) {
  TempYaml file(yaml);
  return CaptureStdout([&] {
    try {
      PPUC ppuc;
      ppuc.LoadConfiguration(file.path());
    } catch (const std::exception&) {
      // A rejected configuration may still have warned first; the caller
      // decides what it cares about.
    }
  });
}

}  // namespace ppuc_test
