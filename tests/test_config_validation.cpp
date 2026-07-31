// Tests for the game YAML validation pass in PPUC::LoadConfiguration().
//
// These matter because the schema is described in three places that have no
// shared artifact: the firmware config topics in io-boards, this validation
// pass, and the config-tool exporter. Validation is the only one of the three
// that can reject a bad file before it reaches hardware.

#include "ConfigFixture.h"

using ppuc_test::LoadAndCaptureError;
using ppuc_test::ValidConfig;

namespace {

// Replaces the first occurrence of `from` in `yaml`. Fails the test if the
// anchor is missing, so that edits to ValidConfig() cannot silently turn a
// test into a no-op.
std::string Replace(const std::string& yaml, const std::string& from,
                    const std::string& to) {
  const auto pos = yaml.find(from);
  REQUIRE_MESSAGE(pos != std::string::npos,
                  "anchor not found in ValidConfig(): " << from);
  std::string result = yaml;
  result.replace(pos, from.size(), to);
  return result;
}

// Removes the first occurrence of a line containing `needle`.
std::string RemoveLine(const std::string& yaml, const std::string& needle) {
  const auto pos = yaml.find(needle);
  REQUIRE_MESSAGE(pos != std::string::npos,
                  "anchor not found in ValidConfig(): " << needle);
  const auto lineStart = yaml.rfind('\n', pos);
  const auto lineEnd = yaml.find('\n', pos);
  std::string result = yaml;
  result.erase(lineStart, lineEnd - lineStart);
  return result;
}

}  // namespace

TEST_CASE("a well-formed configuration is accepted") {
  CHECK(LoadAndCaptureError(ValidConfig()).empty());
}

TEST_CASE("required root fields are enforced") {
  SUBCASE("rom") {
    const auto error = LoadAndCaptureError(RemoveLine(ValidConfig(), "rom:"));
    CHECK_FALSE(error.empty());
    CHECK(error.find("rom") != std::string::npos);
  }

  SUBCASE("serialPort") {
    const auto error =
        LoadAndCaptureError(RemoveLine(ValidConfig(), "serialPort:"));
    CHECK_FALSE(error.empty());
    CHECK(error.find("serialPort") != std::string::npos);
  }

  SUBCASE("platform") {
    const auto error =
        LoadAndCaptureError(RemoveLine(ValidConfig(), "platform:"));
    CHECK_FALSE(error.empty());
    CHECK(error.find("platform") != std::string::npos);
  }
}

TEST_CASE("boards must be present and be a sequence") {
  SUBCASE("missing entirely") {
    auto yaml = ValidConfig();
    const auto pos = yaml.find("boards:");
    REQUIRE(pos != std::string::npos);
    const auto end = yaml.find("switches:");
    REQUIRE(end != std::string::npos);
    yaml.erase(pos, end - pos);

    const auto error = LoadAndCaptureError(yaml);
    CHECK_FALSE(error.empty());
    CHECK(error.find("boards") != std::string::npos);
  }

  SUBCASE("present but not a sequence") {
    auto yaml = ValidConfig();
    const auto pos = yaml.find("boards:");
    const auto end = yaml.find("switches:");
    yaml.replace(pos, end - pos, "boards: notASequence\n");

    const auto error = LoadAndCaptureError(yaml);
    CHECK_FALSE(error.empty());
    CHECK(error.find("boards") != std::string::npos);
  }
}

TEST_CASE("malformed YAML is reported with a location") {
  // A tab character is illegal for indentation in YAML.
  const auto error =
      LoadAndCaptureError(Replace(ValidConfig(), "  -\n    number: 1",
                                  "  -\n\tnumber: 1"));
  CHECK_FALSE(error.empty());
  // The handler in LoadConfiguration() prefixes YAML errors with the file name
  // and a line/column, which is what makes a bad config diagnosable.
  CHECK(error.find("line") != std::string::npos);
}

TEST_CASE("the error message names the offending section") {
  // Regression guard for diagnosability: a validation failure that does not
  // say *where* the problem is costs the user far more than the failure itself.
  const auto error = LoadAndCaptureError(RemoveLine(ValidConfig(), "rom:"));
  REQUIRE_FALSE(error.empty());
  CHECK(error.find("invalid YAML configuration") != std::string::npos);
}

TEST_CASE("optional switch metadata is accepted") {
  // `debounce` is required; `debounceMode` and `button` are optional. `button`
  // is load-bearing at runtime — ppuc-pinmame uses it to decide whether the
  // machine has gone quiet — but a config may legitimately omit it.
  SUBCASE("without the optional keys") {
    auto yaml = Replace(ValidConfig(), "    button: true\n", "");
    yaml = Replace(yaml, "    debounceMode: standard\n", "");
    CHECK(LoadAndCaptureError(yaml).empty());
  }

  SUBCASE("debounce itself is required") {
    const auto error =
        LoadAndCaptureError(RemoveLine(ValidConfig(), "    debounce: 5"));
    CHECK_FALSE(error.empty());
    CHECK(error.find("debounce") != std::string::npos);
  }

  SUBCASE("all three debounce modes") {
    for (const char* mode : {"standard", "fastFlip", "slowStable"}) {
      CAPTURE(mode);
      const auto yaml =
          Replace(ValidConfig(), "debounceMode: standard",
                  std::string("debounceMode: ") + mode);
      CHECK(LoadAndCaptureError(yaml).empty());
    }
  }
}

