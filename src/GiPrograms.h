#pragma once

#include <cstdint>

#include "io-boards/PPUCProtocolV2.h"

namespace ppuc {

// What the host has told the boards about each GI string's color program, and
// which of those still have to go out (io-boards kAdminGiProgram).
//
// The program is sent once per change, not repeated: a board renders it until
// it is told otherwise, so bus time is spent only when a mode with a color
// starts or stops. Two things make a string go out again without a change:
//
// - a session resync. A frame sent while a board was out of step was dropped
//   by it, and nothing else would put it right. A board that already has the
//   program keeps running it, so this does not restart a fade.
// - nothing else: a board that restarted lost its whole configuration, and
//   ppuc ends the game then (boardsLostConfiguration).
//
// Not thread-safe; RS485Comm holds a mutex around it.
class GiProgramTable {
 public:
  GiProgramTable() { Reset(); }

  // The boards have been restarted: every string shows its configured colors
  // and nothing is due.
  void Reset() {
    for (uint8_t i = 0; i < v2::kGiStrings; ++i) {
      m_programs[i] = v2::GiProgram();
      m_programs[i].string = static_cast<uint8_t>(i + 1);
      m_due[i] = false;
      m_touched[i] = false;
    }
  }

  // True when this changes what the string shows, so a frame is due. The same
  // program again is not a change.
  bool Set(const v2::GiProgram& program) {
    if (program.string < 1 || program.string > v2::kGiStrings) {
      return false;
    }
    const uint8_t i = static_cast<uint8_t>(program.string - 1);
    if (m_programs[i] == program) {
      return false;
    }
    m_programs[i] = program;
    m_due[i] = true;
    m_touched[i] = true;
    return true;
  }

  // Every string that has been given a program since the boards were
  // restarted goes out again. After a session resync.
  void MarkSentDue() {
    for (uint8_t i = 0; i < v2::kGiStrings; ++i) {
      if (m_touched[i]) {
        m_due[i] = true;
      }
    }
  }

  bool AnyDue() const {
    for (uint8_t i = 0; i < v2::kGiStrings; ++i) {
      if (m_due[i]) return true;
    }
    return false;
  }

  // The next program to send, lowest string first.
  bool TakeDue(v2::GiProgram& out) {
    for (uint8_t i = 0; i < v2::kGiStrings; ++i) {
      if (m_due[i]) {
        m_due[i] = false;
        out = m_programs[i];
        return true;
      }
    }
    return false;
  }

  const v2::GiProgram& Get(uint8_t string) const {
    return m_programs[(string >= 1 && string <= v2::kGiStrings) ? string - 1
                                                                : 0];
  }

 private:
  v2::GiProgram m_programs[v2::kGiStrings];
  bool m_due[v2::kGiStrings];
  bool m_touched[v2::kGiStrings];
};

}  // namespace ppuc
