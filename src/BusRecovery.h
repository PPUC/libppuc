#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>


namespace ppuc {

// How soon another session resync may follow the last one.
//
// A resync is the answer to a board in the wrong session, and it is no answer
// at all to a wire that has come loose or a board that is not powered: it
// fails again at once, and every attempt is a stretch with no lamp or coil
// updates. Before this, a board that never answered cost a resync every few
// chains for as long as the game ran.
//
// The first resync goes ahead at once. One that follows the last within
// kHelpWindow did not help, and each such one doubles the wait before the
// next, from kFirstDelay up to kMaxDelay. A quiet kHelpWindow forgets it all.
// The machine keeps running meanwhile: outputs go out and the boards that do
// answer are polled.
class ResyncBackoff {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::milliseconds kHelpWindow{5000};
  static constexpr std::chrono::milliseconds kFirstDelay{500};
  static constexpr std::chrono::milliseconds kMaxDelay{8000};

  bool Allowed(Clock::time_point now) const {
    return !m_haveLast || now >= m_last + Delay();
  }

  // A resync was carried out at `now`.
  void Note(Clock::time_point now) {
    if (m_haveLast && now - m_last < kHelpWindow + Delay()) {
      m_level = std::min<uint8_t>(m_level + 1, kMaxLevel);
    } else {
      m_level = 0;
    }
    m_last = now;
    m_haveLast = true;
  }

  std::chrono::milliseconds Delay() const {
    if (m_level == 0) {
      return std::chrono::milliseconds(0);
    }
    const auto delay = kFirstDelay * (1 << (m_level - 1));
    return std::min<std::chrono::milliseconds>(delay, kMaxDelay);
  }

  uint8_t Level() const { return m_level; }

 private:
  static constexpr uint8_t kMaxLevel = 6;
  Clock::time_point m_last{};
  bool m_haveLast = false;
  uint8_t m_level = 0;
};

// Which boards have stopped answering the switch token.
//
// The token passes from board to board, so one that does not answer used to
// end the chain: every pass waited out the whole reply window for it, about
// 40 ms with nothing sent, and the boards behind it were never polled at all.
// Their switches froze, and output frames went out at a tenth of their rate.
//
// A board that missed kMissesToSuspend tokens in a row is suspended: the host
// passes the token straight on to the next board and asks the suspended one
// only once per kProbeInterval. Its first answer reinstates it.
class SwitchBoardSuspension {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr uint8_t kMissesToSuspend = 3;
  static constexpr std::chrono::milliseconds kProbeInterval{1000};

  // Whether to wait for this board's reply now. False while it is suspended
  // and no probe is due; a probe that is due is taken by this call.
  bool ShouldWait(uint8_t board, Clock::time_point now) {
    Entry& e = m_boards[board];
    if (!e.suspended) {
      return true;
    }
    if (now < e.nextProbe) {
      return false;
    }
    e.nextProbe = now + kProbeInterval;
    return true;
  }

  // Returns whether this miss suspended the board.
  bool NoteMiss(uint8_t board, Clock::time_point now) {
    Entry& e = m_boards[board];
    if (e.misses < 255) {
      ++e.misses;
    }
    if (!e.suspended && e.misses >= kMissesToSuspend) {
      e.suspended = true;
      e.nextProbe = now + kProbeInterval;
      return true;
    }
    return false;
  }

  // Returns whether the board had been suspended.
  bool NoteAnswer(uint8_t board) {
    Entry& e = m_boards[board];
    const bool was = e.suspended;
    e = Entry();
    return was;
  }

  bool Suspended(uint8_t board) const { return m_boards[board].suspended; }

  void Reset() {
    for (Entry& e : m_boards) {
      e = Entry();
    }
  }

 private:
  struct Entry {
    uint8_t misses = 0;
    bool suspended = false;
    Clock::time_point nextProbe{};
  };
  Entry m_boards[256];
};

}  // namespace ppuc
