// Tests for skipping boards that carry only slow switches.
//
// A cabinet's flipper buttons sit on the boards that drive the flipper coils,
// where latency is the whole point. The start button, coin door and tilt are on
// a board where it is irrelevant, and polling that board on every cycle costs
// every other board a reply's worth of wire time.
//
// The token ring is a linked list configured onto the boards, so the host can
// enter it late but cannot skip a board in the middle. Everything here follows
// from that: slow boards are sorted into a prefix, and the entry point is
// either the front or just past that prefix.

#include <string>
#include <vector>

#include "ConfigFixture.h"
#include "RS485Comm.h"
#include "doctest.h"

using ppuc_test::TempYaml;
using ppuc_test::ValidConfig;

namespace {

// The entry index for each of the first `cycles` polls.
std::vector<uint8_t> EntrySequence(uint8_t slowCount, uint8_t boardCount,
                                   uint8_t divider, uint32_t cycles) {
  std::vector<uint8_t> out;
  for (uint32_t cycle = 0; cycle < cycles; ++cycle) {
    out.push_back(RS485Comm::SwitchChainEntryIndex(slowCount, boardCount,
                                                   divider, cycle));
  }
  return out;
}

}  // namespace

TEST_CASE("with no slow boards every cycle starts at the front") {
  CHECK(EntrySequence(0, 4, 8, 5) == std::vector<uint8_t>{0, 0, 0, 0, 0});
}

TEST_CASE("one slow board is skipped on all but every eighth cycle") {
  const auto seq = EntrySequence(1, 4, 8, 16);
  CHECK(seq == std::vector<uint8_t>{0, 1, 1, 1, 1, 1, 1, 1,
                                    0, 1, 1, 1, 1, 1, 1, 1});
}

TEST_CASE("two slow boards are skipped together") {
  // They are a prefix, so entering past them is a single index.
  const auto seq = EntrySequence(2, 5, 4, 8);
  CHECK(seq == std::vector<uint8_t>{0, 2, 2, 2, 0, 2, 2, 2});
}

TEST_CASE("a board is polled at least once per divider cycles") {
  // The guarantee the feature rests on: a start button press is seen within
  // `divider` poll cycles, not eventually.
  for (uint8_t divider = 2; divider <= 32; ++divider) {
    const auto seq = EntrySequence(1, 3, divider, 200);
    uint32_t sinceFullPoll = 0;
    uint32_t worst = 0;
    for (const uint8_t entry : seq) {
      if (entry == 0) {
        sinceFullPoll = 0;
        continue;
      }
      if (++sinceFullPoll > worst) {
        worst = sinceFullPoll;
      }
    }
    REQUIRE_MESSAGE(worst == static_cast<uint32_t>(divider - 1),
                    "divider " << (int)divider << " left the slow board "
                               << worst << " cycles unpolled");
  }
}

TEST_CASE("a chain of only slow boards is still polled every cycle") {
  // Skipping every board would poll nothing at all, and there is no latency to
  // protect: no other board is waiting behind this one.
  CHECK(EntrySequence(2, 2, 8, 4) == std::vector<uint8_t>{0, 0, 0, 0});
  CHECK(EntrySequence(1, 1, 8, 4) == std::vector<uint8_t>{0, 0, 0, 0});
}

TEST_CASE("a slow count beyond the chain never points past the end") {
  // SetActiveSwitchBoards clamps, but the rule must not depend on that: an
  // index past m_switchBoardCounter would read outside the array.
  for (uint8_t slow = 0; slow <= 8; ++slow) {
    for (uint32_t cycle = 0; cycle < 8; ++cycle) {
      const uint8_t entry = RS485Comm::SwitchChainEntryIndex(slow, 3, 8, cycle);
      REQUIRE(entry < 3);
    }
  }
}

TEST_CASE("a divider of zero or one disables skipping rather than dividing") {
  CHECK(EntrySequence(1, 4, 0, 4) == std::vector<uint8_t>{0, 0, 0, 0});
  CHECK(EntrySequence(1, 4, 1, 4) == std::vector<uint8_t>{0, 0, 0, 0});
}

TEST_CASE("the setter refuses a divider that would divide by zero") {
  RS485Comm comm;
  comm.SetActiveSwitchBoards({1, 2, 3}, 1);
  comm.SetSlowSwitchPollDivider(0);
  // Not directly observable, so this asserts the contract the run loop relies
  // on: whatever the setter stored, the rule below never divides by zero and
  // never returns an out-of-range index.
  CHECK(RS485Comm::SwitchChainEntryIndex(1, 3, 1, 0) == 0);
}

// --- configuration -----------------------------------------------------------

TEST_CASE("slowSwitches is optional and must be a boolean") {
  const std::string base = ValidConfig();
  TempYaml valid(base);
  PPUC ppuc;
  CHECK_NOTHROW(ppuc.LoadConfiguration(valid.path()));

  std::string withFlag = base;
  const std::string anchor = "    pollEvents: true\n";
  REQUIRE(withFlag.find(anchor) != std::string::npos);
  withFlag.replace(withFlag.find(anchor), anchor.size(),
                   anchor + "    slowSwitches: true\n");
  TempYaml flagged(withFlag);
  PPUC flaggedPpuc;
  CHECK_NOTHROW(flaggedPpuc.LoadConfiguration(flagged.path()));

  std::string bad = base;
  bad.replace(bad.find(anchor), anchor.size(),
              anchor + "    slowSwitches: maybe\n");
  TempYaml wrong(bad);
  PPUC wrongPpuc;
  CHECK_THROWS_AS(wrongPpuc.LoadConfiguration(wrong.path()),
                  std::runtime_error);
}
