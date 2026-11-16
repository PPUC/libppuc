// How the host recovers from a board that does not answer.
//
// On Dirty Harry one board that stopped answering cost the whole bus: every
// chain waited out its reply window, the boards behind it were never polled,
// and a resync followed every few chains, each one a stretch without outputs
// that could not fix a loose wire.

#include <chrono>

#include "BusRecovery.h"
#include "doctest.h"

using ppuc::ResyncBackoff;
using ppuc::SwitchBoardSuspension;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

TEST_CASE("the first resync goes ahead at once") {
  ResyncBackoff backoff;
  CHECK(backoff.Allowed(Clock::now()));
}

TEST_CASE("resyncs that do not help wait longer each time") {
  ResyncBackoff backoff;
  const auto t0 = Clock::now();
  backoff.Note(t0);
  CHECK(backoff.Delay() == milliseconds(0));
  CHECK(backoff.Allowed(t0 + milliseconds(1)));

  backoff.Note(t0 + milliseconds(300));
  CHECK(backoff.Delay() == ResyncBackoff::kFirstDelay);
  CHECK_FALSE(backoff.Allowed(t0 + milliseconds(700)));
  CHECK(backoff.Allowed(t0 + milliseconds(800)));

  auto t = t0 + milliseconds(800);
  backoff.Note(t);
  CHECK(backoff.Delay() == ResyncBackoff::kFirstDelay * 2);
  for (int i = 0; i < 10; i++) {
    t += backoff.Delay();
    backoff.Note(t);
  }
  CHECK(backoff.Delay() == ResyncBackoff::kMaxDelay);
}

TEST_CASE("a resync that helped resets the backoff") {
  ResyncBackoff backoff;
  const auto t0 = Clock::now();
  backoff.Note(t0);
  backoff.Note(t0 + milliseconds(100));
  CHECK(backoff.Level() == 1);
  // The next one is needed only long after: the last one worked.
  backoff.Note(t0 + milliseconds(100) + ResyncBackoff::kHelpWindow +
               backoff.Delay() + milliseconds(1));
  CHECK(backoff.Level() == 0);
  CHECK(backoff.Delay() == milliseconds(0));
}

TEST_CASE("a board is suspended after missing tokens in a row") {
  SwitchBoardSuspension s;
  const auto t0 = Clock::now();
  for (uint8_t i = 1; i < SwitchBoardSuspension::kMissesToSuspend; i++) {
    CHECK_FALSE(s.NoteMiss(4, t0));
    CHECK(s.ShouldWait(4, t0));
  }
  CHECK(s.NoteMiss(4, t0));
  CHECK(s.Suspended(4));
  CHECK_FALSE(s.ShouldWait(4, t0 + milliseconds(10)));
  // Other boards are not affected.
  CHECK(s.ShouldWait(3, t0));
}

TEST_CASE("an answer in between starts the count again") {
  SwitchBoardSuspension s;
  const auto t0 = Clock::now();
  s.NoteMiss(4, t0);
  s.NoteMiss(4, t0);
  CHECK_FALSE(s.NoteAnswer(4));
  CHECK_FALSE(s.NoteMiss(4, t0));
  CHECK_FALSE(s.Suspended(4));
}

TEST_CASE("a suspended board is asked once per probe interval") {
  SwitchBoardSuspension s;
  const auto t0 = Clock::now();
  for (uint8_t i = 0; i < SwitchBoardSuspension::kMissesToSuspend; i++) {
    s.NoteMiss(4, t0);
  }
  CHECK_FALSE(s.ShouldWait(4, t0 + SwitchBoardSuspension::kProbeInterval -
                                  milliseconds(1)));
  const auto probe = t0 + SwitchBoardSuspension::kProbeInterval;
  CHECK(s.ShouldWait(4, probe));
  // Taken: the next one is a whole interval later.
  CHECK_FALSE(s.ShouldWait(4, probe + milliseconds(1)));
  // A failed probe does not suspend it a second time.
  CHECK_FALSE(s.NoteMiss(4, probe));
  CHECK(s.ShouldWait(4, probe + SwitchBoardSuspension::kProbeInterval));
}

TEST_CASE("its first answer reinstates it") {
  SwitchBoardSuspension s;
  const auto t0 = Clock::now();
  for (uint8_t i = 0; i < SwitchBoardSuspension::kMissesToSuspend; i++) {
    s.NoteMiss(4, t0);
  }
  CHECK(s.NoteAnswer(4));
  CHECK_FALSE(s.Suspended(4));
  CHECK(s.ShouldWait(4, t0));
}
