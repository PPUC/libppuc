// The reset cause a board reports in its stats, as libppuc hands it to ppuc.
//
// QueryBoardStats() takes it from the top byte of the stats report's gate word
// and clears that byte, so highPowerGate means what it always did. The decoding
// is the shared protocol header's; this checks what reaches the log.

#include "ConfigFixture.h"
#include "io-boards/PPUCProtocolV2.h"

TEST_CASE("every reset cause is said in words") {
  CHECK(std::string(PPUC::ResetCauseName(ppuc::v2::kResetCausePowerOn)) ==
        "power-on or brown-out");
  CHECK(std::string(
            PPUC::ResetCauseName(ppuc::v2::kResetCauseMainLoopStalled)) ==
        "watchdog, main loop stalled");
  CHECK(std::string(PPUC::ResetCauseName(ppuc::v2::kResetCauseNotReported)) ==
        "not reported");
  CHECK(std::string(PPUC::ResetCauseName(200)) == "not reported");
}

TEST_CASE("the reset cause sits above the gate bits") {
  const uint32_t word =
      0x00160F0Bu | (static_cast<uint32_t>(ppuc::v2::kResetCauseRunPin)
                     << ppuc::v2::kStatsResetCauseShift);
  CHECK(ppuc::v2::StatsResetCause(word) == ppuc::v2::kResetCauseRunPin);
  CHECK(ppuc::v2::StatsResetCause(0x00160F0Bu) ==
        ppuc::v2::kResetCauseNotReported);
}
