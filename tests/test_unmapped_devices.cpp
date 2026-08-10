// Tests for reporting output numbers the configuration does not map.
//
// A number the game drives but no board claims used to be dropped in silence.
// That makes a typo in a YAML indistinguishable from a broken lamp, a stuck
// coil or a wiring fault, and it is the one of those four that costs nothing
// to rule out.
//
// The reporting has to survive the deployment it runs in: ppuc-pinmame is
// normally headless on a read-only Pi, so recording is unconditional and
// printing is not. These tests assert on the record.

#include <string>
#include <vector>

#include "RS485Comm.h"
#include "doctest.h"

namespace {

// A comm object with a small, known mapping: coils 11 and 12, lamps 21 and 22.
// Nothing is connected; QueueEvent only consults the maps.
std::unique_ptr<RS485Comm> Mapped() {
  auto comm = std::make_unique<RS485Comm>();
  comm->SetMappings({11, 12}, {21, 22}, {31});
  return comm;
}

bool Mentions(const std::vector<std::string>& lines, const std::string& needle) {
  for (const std::string& line : lines) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

size_t CountMentioning(const std::vector<std::string>& lines,
                       const std::string& needle) {
  size_t n = 0;
  for (const std::string& line : lines) {
    if (line.find(needle) != std::string::npos) {
      ++n;
    }
  }
  return n;
}

}  // namespace

TEST_CASE("a mapped coil is not reported") {
  auto comm = Mapped();
  comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, 11, 1));
  CHECK_FALSE(Mentions(comm->GetRecentAnomalies(), "not mapped"));
}

TEST_CASE("an unmapped coil is named") {
  auto comm = Mapped();
  comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, 99, 1));

  const auto anomalies = comm->GetRecentAnomalies();
  REQUIRE(Mentions(anomalies, "not mapped"));
  CHECK(Mentions(anomalies, "coil 99"));
}

TEST_CASE("an unmapped lamp is named") {
  auto comm = Mapped();
  comm->QueueEvent(new Event(EVENT_SOURCE_LIGHT, 77, 1));
  CHECK(Mentions(comm->GetRecentAnomalies(), "lamp 77"));
}

TEST_CASE("a GI string outside the fixed range is named") {
  auto comm = Mapped();
  comm->QueueEvent(new Event(EVENT_SOURCE_GI, 9, 4));
  CHECK(Mentions(comm->GetRecentAnomalies(), "GI string 9"));
}

TEST_CASE("a valid GI string is not reported") {
  auto comm = Mapped();
  comm->QueueEvent(new Event(EVENT_SOURCE_GI, 1, 4));
  CHECK_FALSE(Mentions(comm->GetRecentAnomalies(), "not mapped"));
}

TEST_CASE("the same number is reported once, not once per frame") {
  // The reason this matters: a ROM drives its outputs every frame, so a
  // per-occurrence report would bury everything else in the ring within a
  // second and the rate limiter alone would hide which numbers were involved.
  auto comm = Mapped();
  for (int i = 0; i < 200; ++i) {
    comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, 99, i % 2));
  }
  CHECK(CountMentioning(comm->GetRecentAnomalies(), "coil 99") == 1);
}

TEST_CASE("different numbers are each reported") {
  // Which number it is *is* the diagnosis, so these must not collapse into one
  // rate-limited line the way ordinary anomalies do.
  auto comm = Mapped();
  comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, 91, 1));
  comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, 92, 1));
  comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, 93, 1));

  const auto anomalies = comm->GetRecentAnomalies();
  CHECK(Mentions(anomalies, "coil 91"));
  CHECK(Mentions(anomalies, "coil 92"));
  CHECK(Mentions(anomalies, "coil 93"));
}

TEST_CASE("a coil and a lamp with the same number are both reported") {
  // They are separate namespaces. Keying only on the number would report the
  // first and swallow the second.
  auto comm = Mapped();
  comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, 55, 1));
  comm->QueueEvent(new Event(EVENT_SOURCE_LIGHT, 55, 1));

  const auto anomalies = comm->GetRecentAnomalies();
  CHECK(Mentions(anomalies, "coil 55"));
  CHECK(Mentions(anomalies, "lamp 55"));
}

TEST_CASE("a flood of distinct numbers stops rather than growing without bound") {
  // A config whose numbering is wholly wrong could otherwise add an entry per
  // number for as long as the machine runs.
  auto comm = Mapped();
  for (uint16_t n = 100; n < 400; ++n) {
    comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, n, 1));
  }

  const auto anomalies = comm->GetRecentAnomalies();
  CHECK(Mentions(anomalies, "further ones are not reported"));
  // The cap is on distinct numbers remembered, not on ring entries, and the
  // ring is smaller still - what matters is that the run terminated with a
  // statement that it was truncated rather than silently.
  CHECK(CountMentioning(anomalies, "further ones are not reported") == 1);
}
