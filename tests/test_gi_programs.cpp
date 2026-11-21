// Tests for GI color programs (custom games color the GI with their modes).
//
// What goes to the boards is decided here: a game says "static, configured
// color" when no colored mode runs, and that has to become the default, which
// puts every LED's own color back. And a program goes on the bus once per
// change, not on every call.

#include "ConfigFixture.h"
#include "GiPrograms.h"
#include "io-boards/PPUCProtocolV2.h"

using ppuc_test::TempYaml;
using ppuc_test::ValidConfig;

namespace {

// GI string 1 is warm white on two LEDs, string 2 has one white LED and one
// red accent.
std::string GiConfig() {
  return ValidConfig() + R"YAML(
ledStripes:
  -
    board: 2
    port: 29
    ledType: GRB
    brightness: 128
    amount: 60
    afterGlow: 0
    lightUp: 0
    gi:
      -
        description: 'GI 1 left'
        number: 1
        ledNumber: 0
        color: 'FFE0B0'
      -
        description: 'GI 1 right'
        number: 1
        ledNumber: 1
        color: '#FFE0B0'
      -
        description: 'GI 2 white'
        number: 2
        ledNumber: 2
        color: 'FFFFFF'
      -
        description: 'GI 2 accent'
        number: 2
        ledNumber: 3
        color: 'FF0000'
)YAML";
}

struct Loaded {
  TempYaml file{GiConfig()};
  PPUC ppuc;
  Loaded() { ppuc.LoadConfiguration(file.path()); }
};

}  // namespace

TEST_CASE("the configured GI colors are known per string") {
  Loaded l;
  CHECK(l.ppuc.GetConfiguredGiColors(1) == std::set<uint32_t>{0xFFE0B0});
  CHECK(l.ppuc.GetConfiguredGiColors(2) ==
        std::set<uint32_t>{0xFFFFFF, 0xFF0000});
  CHECK(l.ppuc.GetConfiguredGiColors(3).empty());
  CHECK(l.ppuc.GetConfiguredGiColors(0).empty());
  CHECK(l.ppuc.GetConfiguredGiColors(6).empty());
}

TEST_CASE("effect names are the ones the Lua call takes") {
  CHECK(PPUC::GiEffectFromName("default") == PPUC_GI_EFFECT_DEFAULT);
  CHECK(PPUC::GiEffectFromName("static") == PPUC_GI_EFFECT_STATIC);
  CHECK(PPUC::GiEffectFromName("cycle") == PPUC_GI_EFFECT_CYCLE);
  CHECK(PPUC::GiEffectFromName("fade") == PPUC_GI_EFFECT_FADE);
  CHECK(PPUC::GiEffectFromName("top") == -1);
  CHECK(PPUC::GiEffectFromName("") == -1);
}

TEST_CASE("a mode color is sent as a static program") {
  Loaded l;
  PPUCGiProgram p;
  REQUIRE(
      l.ppuc.ResolveGiColorProgram(1, {0xFF3000}, PPUC_GI_EFFECT_STATIC, 0, p));
  CHECK(p.string == 1);
  CHECK(p.effect == PPUC_GI_EFFECT_STATIC);
  CHECK(p.colors == std::vector<uint32_t>{0xFF3000});
  CHECK(p.periodMs == 0);
}

TEST_CASE("the configured color, static, is the default") {
  Loaded l;
  PPUCGiProgram p;
  REQUIRE(
      l.ppuc.ResolveGiColorProgram(1, {0xFFE0B0}, PPUC_GI_EFFECT_STATIC, 0, p));
  CHECK(p.effect == PPUC_GI_EFFECT_DEFAULT);
  CHECK(p.colors.empty());

  // A string of mixed colors: any of them means "back to configured", so the
  // accent LED keeps its red instead of the whole string turning white.
  REQUIRE(
      l.ppuc.ResolveGiColorProgram(2, {0xFFFFFF}, PPUC_GI_EFFECT_STATIC, 0, p));
  CHECK(p.effect == PPUC_GI_EFFECT_DEFAULT);

  // Another string's color is a color like any other.
  REQUIRE(
      l.ppuc.ResolveGiColorProgram(1, {0xFFFFFF}, PPUC_GI_EFFECT_STATIC, 0, p));
  CHECK(p.effect == PPUC_GI_EFFECT_STATIC);
}

TEST_CASE("default needs no colors, and no colors is the default") {
  Loaded l;
  PPUCGiProgram p;
  REQUIRE(l.ppuc.ResolveGiColorProgram(3, {0x123456}, PPUC_GI_EFFECT_DEFAULT,
                                       500, p));
  CHECK(p.effect == PPUC_GI_EFFECT_DEFAULT);
  CHECK(p.colors.empty());
  CHECK(p.periodMs == 0);

  REQUIRE(l.ppuc.ResolveGiColorProgram(3, {}, PPUC_GI_EFFECT_FADE, 500, p));
  CHECK(p.effect == PPUC_GI_EFFECT_DEFAULT);
}

TEST_CASE("stacked mode colors cycle or fade, four at most") {
  Loaded l;
  PPUCGiProgram p;
  REQUIRE(l.ppuc.ResolveGiColorProgram(
      2, {0xFF3000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF},
      PPUC_GI_EFFECT_FADE, 2000, p));
  CHECK(p.effect == PPUC_GI_EFFECT_FADE);
  CHECK(p.periodMs == 2000);
  // Highest priority first: the fifth is the one left out.
  CHECK(p.colors ==
        std::vector<uint32_t>{0xFF3000, 0x00FF00, 0x0000FF, 0xFFFF00});

  // One color has nothing to cycle through.
  REQUIRE(l.ppuc.ResolveGiColorProgram(2, {0x00FF00}, PPUC_GI_EFFECT_CYCLE,
                                       2000, p));
  CHECK(p.effect == PPUC_GI_EFFECT_STATIC);
  CHECK(p.periodMs == 0);

  // Nor has a period of 0.
  REQUIRE(l.ppuc.ResolveGiColorProgram(2, {0x00FF00, 0x0000FF},
                                       PPUC_GI_EFFECT_CYCLE, 0, p));
  CHECK(p.effect == PPUC_GI_EFFECT_STATIC);
  CHECK(p.colors == std::vector<uint32_t>{0x00FF00});
}

TEST_CASE("strings and effects out of range are refused") {
  Loaded l;
  PPUCGiProgram p;
  CHECK_FALSE(
      l.ppuc.ResolveGiColorProgram(0, {0xFF0000}, PPUC_GI_EFFECT_STATIC, 0, p));
  CHECK_FALSE(
      l.ppuc.ResolveGiColorProgram(6, {0xFF0000}, PPUC_GI_EFFECT_STATIC, 0, p));
  CHECK_FALSE(l.ppuc.ResolveGiColorProgram(1, {0xFF0000}, 4, 0, p));
  CHECK_FALSE(
      l.ppuc.SetGiColorProgram(9, {0xFF0000}, PPUC_GI_EFFECT_STATIC, 0));
}

TEST_CASE("a GI program goes out once per change") {
  ppuc::GiProgramTable table;
  ppuc::v2::GiProgram program;
  CHECK_FALSE(table.TakeDue(program));

  ppuc::v2::GiProgram red;
  red.string = 2;
  red.effect = ppuc::v2::kGiEffectStatic;
  red.colorCount = 1;
  red.colors[0] = 0xFF0000;
  CHECK(table.Set(red));
  CHECK_FALSE(table.Set(red));  // the same again is not a change
  REQUIRE(table.TakeDue(program));
  CHECK(program == red);
  CHECK_FALSE(table.TakeDue(program));

  // Back to the configured colors is a change...
  ppuc::v2::GiProgram defaults;
  defaults.string = 2;
  CHECK(table.Set(defaults));
  REQUIRE(table.TakeDue(program));
  CHECK(program.effect == ppuc::v2::kGiEffectDefault);
  // ...but the default where nothing was ever set is not.
  defaults.string = 4;
  CHECK_FALSE(table.Set(defaults));
}

TEST_CASE("after a resync every string that was set goes out again") {
  ppuc::GiProgramTable table;
  ppuc::v2::GiProgram fade;
  fade.string = 1;
  fade.effect = ppuc::v2::kGiEffectFade;
  fade.colorCount = 2;
  fade.periodMs = 1000;
  fade.colors[0] = 0xFF0000;
  fade.colors[1] = 0x0000FF;
  table.Set(fade);
  ppuc::v2::GiProgram program;
  REQUIRE(table.TakeDue(program));

  table.MarkSentDue();
  REQUIRE(table.TakeDue(program));
  CHECK(program == fade);
  // String 1 only: the others were never set and boards still have them at
  // the default.
  CHECK_FALSE(table.TakeDue(program));

  // A restart of the boards forgets everything.
  table.Reset();
  table.MarkSentDue();
  CHECK_FALSE(table.AnyDue());
}
