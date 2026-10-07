// Tests for the check of every device against its board's hardware.
//
// A port in the game YAML is a GPIO number, and what a GPIO is depends on the
// board type: GPIO 19 is a coil driver on an IO_16_8_1, a matrix strobe on an
// IO_16x8_matrix and a lamp column on an Out_8x10. The table that says which
// is which is io-boards/PPUCBoardTypes.h, shared with the firmware. These
// tests pin what the host does with it, because a device on the wrong kind of
// pin does not fail loudly on the bus - it drives whatever is there.

#include "ConfigFixture.h"

using ppuc_test::LoadAndCaptureError;

namespace {

// A configuration with one board of each type and nothing on them.
std::string Boards() {
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
    type: IO_16_8_1
  -
    number: 2
    pollEvents: true
    type: Opto_16
  -
    number: 3
    pollEvents: true
    type: IO_16x8_matrix
  -
    number: 4
    pollEvents: false
    type: Out_8x10
  -
    number: 5
    pollEvents: true
)YAML";
}

std::string Switch(int board, int port, int number = 11) {
  return "  -\n    description: 'switch'\n    number: " +
         std::to_string(number) + "\n    board: " + std::to_string(board) +
         "\n    port: " + std::to_string(port) +
         "\n    debounce: 5\n    debounceMode: standard\n";
}

std::string Pwm(int board, int port, const std::string& type,
                int number = 7) {
  return "  -\n    description: 'output'\n    number: " +
         std::to_string(number) + "\n    board: " + std::to_string(board) +
         "\n    port: " + std::to_string(port) +
         "\n    power: 255\n    minPulseTime: 0\n    maxPulseTime: 50\n"
         "    holdPower: 0\n    holdPowerActivationTime: 0\n"
         "    fastFlipSwitch: 0\n    type: " +
         type + "\n";
}

std::string LedString(int board, int port) {
  return "ledStripes:\n  -\n    board: " + std::to_string(board) +
         "\n    port: " + std::to_string(port) +
         "\n    ledType: GRB\n    brightness: 128\n    amount: 60\n"
         "    afterGlow: 0\n    lightUp: 0\n";
}

std::string SwitchMatrix(int board, int rows, int position) {
  return "switchMatrix:\n  description: 'matrix'\n  board: " +
         std::to_string(board) + "\n  activeLow: true\n  rows: " +
         std::to_string(rows) +
         "\n  switches:\n    -\n      description: 'matrix switch'\n"
         "      board: " +
         std::to_string(board) + "\n      port: " + std::to_string(position) +
         "\n      number: 31\n";
}

std::string LampMatrix(int board, int rows, int position, int number = 41) {
  return "lampMatrix:\n  description: 'lamps'\n  board: " +
         std::to_string(board) + "\n  rows: " + std::to_string(rows) +
         "\n  lamps:\n    -\n      description: 'matrix lamp'\n"
         "      port: " +
         std::to_string(position) + "\n      number: " +
         std::to_string(number) + "\n";
}

bool Mentions(const std::string& error, const std::string& text) {
  return error.find(text) != std::string::npos;
}

}  // namespace

TEST_CASE("a board type is optional and must be one the protocol knows") {
  CHECK(LoadAndCaptureError(Boards()).empty());

  const auto error =
      LoadAndCaptureError(Boards() + "  -\n    number: 6\n"
                                     "    pollEvents: true\n"
                                     "    type: IO_32_0_0\n");
  CHECK(Mentions(error, "unknown board type 'IO_32_0_0'"));
}

TEST_CASE("a board without a type is an IO_16_8_1") {
  // Every board was one before the others existed.
  CHECK(LoadAndCaptureError(Boards() + "switches:\n" + Switch(5, 3)).empty());
  CHECK(LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(5, 27, "coil"))
            .empty());

  PPUC ppuc;
  ppuc_test::TempYaml file(Boards());
  ppuc.LoadConfiguration(file.path());
  CHECK(ppuc.GetConfiguredBoardType(1) == 1);
  CHECK(ppuc.GetConfiguredBoardType(2) == 4);
  CHECK(ppuc.GetConfiguredBoardType(3) == 2);
  CHECK(ppuc.GetConfiguredBoardType(4) == 3);
  CHECK(ppuc.GetConfiguredBoardType(5) == 1);
  CHECK(ppuc.GetConfiguredBoardType(6) == 0);
}

TEST_CASE("a switch needs an input of its board") {
  for (int board : {1, 2, 3}) {
    CAPTURE(board);
    CHECK(LoadAndCaptureError(Boards() + "switches:\n" + Switch(board, 3))
              .empty());
    CHECK(LoadAndCaptureError(Boards() + "switches:\n" + Switch(board, 18))
              .empty());
    CHECK(Mentions(
        LoadAndCaptureError(Boards() + "switches:\n" + Switch(board, 19)),
        "port 19 is not a switch input"));
  }

  // The RS485 driver enable, and a connector pin number instead of a GPIO.
  CHECK(Mentions(LoadAndCaptureError(Boards() + "switches:\n" + Switch(1, 2)),
                 "not a switch input on board 1 (IO_16_8_1)"));

  // Out_8x10 has no inputs at all: GPIO 3 is a 3 A lamp driver there.
  CHECK(Mentions(LoadAndCaptureError(Boards() + "switches:\n" + Switch(4, 3)),
                 "not a switch input on board 4 (Out_8x10)"));
}

TEST_CASE("an output needs a driver of its board") {
  for (int port : {19, 24, 26, 27}) {
    CAPTURE(port);
    CHECK(LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(1, port, "coil"))
              .empty());
    CHECK(LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(3, port, "lamp"))
              .empty());
  }

  // GPIO 25 is the on-board LED, in the middle of the outputs.
  CHECK(Mentions(
      LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(1, 25, "coil")),
      "port 25 is not an output on board 1 (IO_16_8_1)"));

  // An IO_16_8_1 input doubles as a low-power output. An IO_16x8_matrix input
  // does not.
  CHECK(LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(1, 17, "lamp"))
            .empty());
  CHECK(Mentions(
      LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(3, 17, "lamp")),
      "port 17 is not an output on board 3 (IO_16x8_matrix)"));

  // Opto_16 has no drivers.
  CHECK(Mentions(
      LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(2, 19, "coil")),
      "port 19 is not an output on board 2 (Opto_16)"));
}

TEST_CASE("two outputs of one board may not share a PWM channel") {
  // GPIO 3 and GPIO 19 are the same RP2040 PWM channel: a lamp dimmed on the
  // first input would fire the coil on the first high-power output.
  const auto error = LoadAndCaptureError(Boards() + "pwmOutput:\n" +
                                         Pwm(1, 19, "coil", 7) +
                                         Pwm(1, 3, "lamp", 8));
  CHECK(Mentions(error, "port 3 shares its PWM channel with port 19"));

  // On different boards they are different chips.
  CHECK(LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(1, 19, "coil", 7) +
                            Pwm(5, 3, "lamp", 8))
            .empty());
  CHECK(LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(1, 19, "coil", 7) +
                            Pwm(1, 4, "lamp", 8))
            .empty());
}

TEST_CASE("an LED string belongs on the special output") {
  for (int board : {1, 2, 3, 4}) {
    CAPTURE(board);
    CHECK(LoadAndCaptureError(Boards() + LedString(board, 29)).empty());
  }
  CHECK(Mentions(LoadAndCaptureError(Boards() + LedString(1, 19)),
                 "port 19 is not the LED output of board 1 (IO_16_8_1)"));
}

TEST_CASE("IO_16_8_1 keeps its 4 column switch matrix") {
  CHECK(LoadAndCaptureError(Boards() + SwitchMatrix(1, 4, 15)).empty());
  CHECK(LoadAndCaptureError(Boards() + SwitchMatrix(1, 8, 31)).empty());

  CHECK(Mentions(LoadAndCaptureError(Boards() + SwitchMatrix(1, 16, 0)),
                 "4 column matrix with 4 or 8 rows"));
  CHECK(Mentions(LoadAndCaptureError(Boards() + SwitchMatrix(1, 8, 32)),
                 "position 32 is outside the matrix"));
}

TEST_CASE("IO_16x8_matrix scans eight strobes by up to sixteen returns") {
  CHECK(LoadAndCaptureError(Boards() + SwitchMatrix(3, 16, 127)).empty());
  CHECK(LoadAndCaptureError(Boards() + SwitchMatrix(3, 8, 63)).empty());
  CHECK(LoadAndCaptureError(Boards() + SwitchMatrix(3, 10, 79)).empty());

  CHECK(Mentions(LoadAndCaptureError(Boards() + SwitchMatrix(3, 16, 128)),
                 "position 128 is outside the matrix"));
  CHECK(Mentions(LoadAndCaptureError(Boards() + SwitchMatrix(3, 8, 64)),
                 "position 64 is outside the matrix"));
  CHECK(Mentions(LoadAndCaptureError(Boards() + SwitchMatrix(3, 17, 0)),
                 "supports 1 to 16 rows"));
}

TEST_CASE("the strobed matrix takes the whole IO_16x8_matrix") {
  // All sixteen inputs are returns and all eight outputs strobes.
  CHECK(Mentions(LoadAndCaptureError(Boards() + SwitchMatrix(3, 8, 0) +
                                     "switches:\n" + Switch(3, 18)),
                 "every input of board 3 (IO_16x8_matrix) is a return"));
  CHECK(Mentions(LoadAndCaptureError(Boards() + SwitchMatrix(3, 8, 0) +
                                     "pwmOutput:\n" + Pwm(3, 27, "lamp")),
                 "port 27 is a strobe of the switch matrix"));

  // Other boards are unaffected.
  CHECK(LoadAndCaptureError(Boards() + SwitchMatrix(3, 8, 0) + "switches:\n" +
                            Switch(1, 18))
            .empty());
}

TEST_CASE("boards without a matrix refuse one") {
  CHECK(Mentions(LoadAndCaptureError(Boards() + SwitchMatrix(2, 8, 0)),
                 "board 2 (Opto_16) cannot scan a switch matrix"));
  CHECK(Mentions(LoadAndCaptureError(Boards() + SwitchMatrix(4, 8, 0)),
                 "board 4 (Out_8x10) cannot scan a switch matrix"));
  CHECK(Mentions(LoadAndCaptureError(Boards() + LampMatrix(1, 8, 0)),
                 "board 1 (IO_16_8_1) cannot drive a lamp matrix"));
}

TEST_CASE("Out_8x10 drives a lamp matrix of 8 columns by up to 10 rows") {
  CHECK(LoadAndCaptureError(Boards() + LampMatrix(4, 10, 79)).empty());
  CHECK(LoadAndCaptureError(Boards() + LampMatrix(4, 8, 63)).empty());

  CHECK(Mentions(LoadAndCaptureError(Boards() + LampMatrix(4, 10, 80)),
                 "position 80 is outside the matrix"));
  CHECK(Mentions(LoadAndCaptureError(Boards() + LampMatrix(4, 11, 0)),
                 "supports 1 to 10 rows"));
  CHECK(Mentions(LoadAndCaptureError(Boards() + LampMatrix(4, 8, 0, 0)),
                 "lamp number 0 is not a lamp"));
}

TEST_CASE("the lamp matrix fields are required") {
  const std::string valid = Boards() + LampMatrix(4, 8, 0);
  CHECK(LoadAndCaptureError(valid).empty());

  for (const char* field : {"  rows: 8\n", "      port: 0\n",
                            "      number: 41\n",
                            "      description: 'matrix lamp'\n"}) {
    CAPTURE(field);
    std::string yaml = valid;
    const auto pos = yaml.find(field);
    REQUIRE(pos != std::string::npos);
    yaml.erase(pos, std::string(field).size());
    CHECK_FALSE(LoadAndCaptureError(yaml).empty());
  }
}

TEST_CASE("Out_8x10 outputs are lamp drivers and nothing else") {
  // Lo_10 on GPIO 3 and Hi_8 on GPIO 17, each with a lamp of its own.
  CHECK(LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(4, 3, "lamp", 7) +
                            Pwm(4, 17, "lamp", 8))
            .empty());

  // No PWM on this board, so the channel GPIO 3 and GPIO 19 would share on
  // another board is not a conflict here.
  CHECK(LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(4, 3, "lamp", 7) +
                            Pwm(4, 19, "lamp", 8))
            .empty());

  CHECK(Mentions(
      LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(4, 3, "coil")),
      "board 4 (Out_8x10) drives lamps only, not a coil"));
  // A test point between the two groups of outputs.
  CHECK(Mentions(
      LoadAndCaptureError(Boards() + "pwmOutput:\n" + Pwm(4, 13, "lamp")),
      "port 13 is not an output on board 4 (Out_8x10)"));
}

TEST_CASE("a line of the lamp matrix cannot carry a lamp of its own") {
  // Position 0 is Hi_1 (GPIO 24) by Lo_1 (GPIO 12).
  CHECK(Mentions(LoadAndCaptureError(Boards() + LampMatrix(4, 8, 0) +
                                     "pwmOutput:\n" + Pwm(4, 24, "lamp")),
                 "port 24 is a line of the lamp matrix"));
  CHECK(Mentions(LoadAndCaptureError(Boards() + LampMatrix(4, 8, 0) +
                                     "pwmOutput:\n" + Pwm(4, 12, "lamp")),
                 "port 12 is a line of the lamp matrix"));

  // With 8 rows in use, Lo_9 and Lo_10 are free.
  CHECK(LoadAndCaptureError(Boards() + LampMatrix(4, 8, 0) + "pwmOutput:\n" +
                            Pwm(4, 3, "lamp"))
            .empty());
}
