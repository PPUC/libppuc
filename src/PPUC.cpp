#include "PPUC.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "Adafruit_NeoPixel.h"
#include "RS485Comm.h"
#include "io-boards/PPUCProtocolV2.h"
#include "io-boards/Event.h"
#include "io-boards/PPUCPlatforms.h"

namespace {

std::string FormatYamlLocation(const YAML::Mark& mark) {
  if (mark.is_null()) {
    return "unknown location";
  }

  std::stringstream ss;
  ss << "line " << (mark.line + 1) << ", column " << (mark.column + 1);
  return ss.str();
}

const char* LedConfigBlockName(uint32_t type) {
  switch (type) {
    case LED_TYPE_LAMP:
      return "ledStripes.lamps";
    case LED_TYPE_FLASHER:
      return "ledStripes.flashers";
    case LED_TYPE_GI:
      return "ledStripes.gi";
    default:
      return "ledStripes.<unknown>";
  }
}

uint8_t ResolveLedTypeValue(const std::string& type) {
  if (type == "RGB") return NEO_RGB;
  if (type == "RBG") return NEO_RBG;
  if (type == "GRB") return NEO_GRB;
  if (type == "GBR") return NEO_GBR;
  if (type == "BRG") return NEO_BRG;
  if (type == "BGR") return NEO_BGR;

  if (type == "WRGB") return NEO_WRGB;
  if (type == "WRBG") return NEO_WRBG;
  if (type == "WGRB") return NEO_WGRB;
  if (type == "WGBR") return NEO_WGBR;
  if (type == "WBRG") return NEO_WBRG;
  if (type == "WBGR") return NEO_WBGR;

  if (type == "RWGB") return NEO_RWGB;
  if (type == "RWBG") return NEO_RWBG;
  if (type == "RGWB") return NEO_RGWB;
  if (type == "RGBW") return NEO_RGBW;
  if (type == "RBWG") return NEO_RBWG;
  if (type == "RBGW") return NEO_RBGW;

  if (type == "GWRB") return NEO_GWRB;
  if (type == "GWBR") return NEO_GWBR;
  if (type == "GRWB") return NEO_GRWB;
  if (type == "GRBW") return NEO_GRBW;
  if (type == "GBWR") return NEO_GBWR;
  if (type == "GBRW") return NEO_GBRW;

  if (type == "BWRG") return NEO_BWRG;
  if (type == "BWGR") return NEO_BWGR;
  if (type == "BRWG") return NEO_BRWG;
  if (type == "BRGW") return NEO_BRGW;
  if (type == "BGWR") return NEO_BGWR;
  if (type == "BGRW") return NEO_BGRW;

  return 0;
}

std::string OptionalStringField(const YAML::Node& node, const char* field) {
  try {
    if (node && node[field]) {
      return node[field].as<std::string>();
    }
  } catch (const YAML::Exception&) {
  }
  return "";
}

std::string ConfigItemContext(const YAML::Node& item, const std::string& path) {
  std::stringstream ss;
  ss << path;

  const std::string description = OptionalStringField(item, "description");
  if (!description.empty()) {
    ss << ", description '" << description << "'";
  }

  ss << " (" << FormatYamlLocation(item.Mark()) << ")";
  return ss.str();
}

std::string LedConfigItemContext(const YAML::Node& item, uint32_t type,
                                 uint8_t board, uint32_t port,
                                 size_t itemIndex) {
  std::stringstream ss;
  ss << LedConfigBlockName(type) << "[" << itemIndex << "]"
     << " on board " << static_cast<unsigned>(board) << ", port " << port;

  const std::string description = OptionalStringField(item, "description");
  if (!description.empty()) {
    ss << ", description '" << description << "'";
  }

  ss << " (" << FormatYamlLocation(item.Mark()) << ")";
  return ss.str();
}

void RequireYamlNode(const YAML::Node& node, const std::string& path) {
  if (!node) {
    throw std::runtime_error("invalid YAML configuration: missing required '" +
                             path + "'");
  }
}

template <typename T>
T ReadRequiredYamlField(const YAML::Node& item, const char* field,
                        const std::string& context) {
  const YAML::Node value = item[field];
  if (!value) {
    throw std::runtime_error("invalid YAML configuration: " + context +
                             " is missing required field '" + field + "'");
  }

  try {
    return value.as<T>();
  } catch (const YAML::Exception& e) {
    throw std::runtime_error("invalid YAML configuration: " + context +
                             " has invalid field '" + field + "' at " +
                             FormatYamlLocation(value.Mark()) + ": " +
                             e.what());
  }
}

uint32_t ParseRequiredHexColorField(const YAML::Node& item, const char* field,
                                    const std::string& context) {
  std::string value =
      ReadRequiredYamlField<std::string>(item, field, context);
  if (!value.empty() && value[0] == '#') {
    value.erase(0, 1);
  }

  uint32_t color = 0;
  std::stringstream ss;
  ss << std::hex << value;
  ss >> color;
  if (ss.fail() || !ss.eof()) {
    throw std::runtime_error("invalid YAML configuration: " + context +
                             " has invalid field '" + field +
                             "': expected hexadecimal color, got '" + value +
                             "'");
  }
  return color;
}

uint32_t ParseHexColorValue(const YAML::Node& value,
                            const std::string& context) {
  std::string text = value.as<std::string>();
  if (!text.empty() && text[0] == '#') {
    text.erase(0, 1);
  }

  uint32_t color = 0;
  std::stringstream ss;
  ss << std::hex << text;
  ss >> color;
  if (ss.fail() || !ss.eof()) {
    throw std::runtime_error("invalid YAML configuration: " + context +
                             " expected hexadecimal color, got '" + text +
                             "'");
  }
  return color;
}

void ValidateLedEffectColorFields(const YAML::Node& effect,
                                  const std::string& effectPath) {
  const std::string context = ConfigItemContext(effect, effectPath);
  const YAML::Node colors = effect["colors"];
  if (!colors || !colors.IsSequence() || colors.size() == 0 ||
      colors.size() > 3) {
    throw std::runtime_error("invalid YAML configuration: " + context +
                             " field 'colors' must contain 1 to 3 "
                             "hexadecimal colors");
  }

  for (std::size_t i = 0; i < colors.size(); ++i) {
    ParseHexColorValue(colors[i],
                       context + " field 'colors[" + std::to_string(i) +
                           "]'");
  }
}

std::array<uint32_t, 3> BuildLedEffectColors(const YAML::Node& effect,
                                             const std::string& context) {
  std::array<uint32_t, 3> colors = {0, 0, 0};
  const YAML::Node colorNodes = effect["colors"];
  for (std::size_t i = 0; i < colorNodes.size() && i < colors.size(); ++i) {
    colors[i] = ParseHexColorValue(
        colorNodes[i], context + " field 'colors[" + std::to_string(i) +
                           "]'");
  }

  return colors;
}

uint32_t BuildWs2812FxOptions(const YAML::Node& effect) {
  uint32_t options = 0;
  if (effect["reverse"] && effect["reverse"].as<uint32_t>() != 0) {
    options |= 0x80u;
  }
  if (effect["gamma"] && effect["gamma"].as<bool>()) {
    options |= 0x08u;
  }
  if (effect["fadeRate"]) {
    options |= (effect["fadeRate"].as<uint32_t>() & 0x07u) << 4;
  }
  if (effect["size"]) {
    options |= (effect["size"].as<uint32_t>() & 0x03u) << 1;
  }
  if (effect["options"]) {
    options |= effect["options"].as<uint32_t>() & 0xFFu;
  }
  return options;
}

template <typename T>
void ValidateRequiredField(const YAML::Node& item, const std::string& path,
                           const char* field) {
  ReadRequiredYamlField<T>(item, field, ConfigItemContext(item, path));
}

template <typename T>
void ValidateOptionalField(const YAML::Node& item, const std::string& path,
                           const char* field) {
  const YAML::Node value = item[field];
  if (!value) {
    return;
  }

  try {
    value.as<T>();
  } catch (const YAML::Exception& e) {
    throw std::runtime_error("invalid YAML configuration: " +
                             ConfigItemContext(item, path) +
                             " has invalid field '" + field + "' at " +
                             FormatYamlLocation(value.Mark()) + ": " +
                             e.what());
  }
}

void ValidateRequiredSequence(const YAML::Node& node,
                              const std::string& path) {
  RequireYamlNode(node, path);
  if (!node.IsSequence()) {
    throw std::runtime_error("invalid YAML configuration: '" + path +
                             "' must be a list at " +
                             FormatYamlLocation(node.Mark()));
  }
}

void ValidateOptionalSequence(const YAML::Node& node,
                              const std::string& path) {
  if (!node) {
    return;
  }
  if (node.IsMap() && node.size() == 0) {
    return;
  }
  if (!node.IsSequence()) {
    throw std::runtime_error("invalid YAML configuration: '" + path +
                             "' must be a list at " +
                             FormatYamlLocation(node.Mark()));
  }
}

bool HasSequenceItems(const YAML::Node& node) {
  return node && node.IsSequence();
}

void ValidateRequiredMap(const YAML::Node& node, const std::string& path) {
  RequireYamlNode(node, path);
  if (!node.IsMap()) {
    throw std::runtime_error("invalid YAML configuration: '" + path +
                             "' must be a map at " +
                             FormatYamlLocation(node.Mark()));
  }
}

void ValidateEffectModeField(const YAML::Node& item, const std::string& path,
                             const char* field) {
  const YAML::Node value = item[field];
  if (!value) {
    throw std::runtime_error("invalid YAML configuration: " +
                             ConfigItemContext(item, path) +
                             " is missing required field '" + field + "'");
  }

  try {
    value.as<std::string>();
  } catch (const YAML::Exception& e) {
    throw std::runtime_error("invalid YAML configuration: " +
                             ConfigItemContext(item, path) +
                             " has invalid field '" + field + "' at " +
                             FormatYamlLocation(value.Mark()) + ": " +
                             e.what());
  }
}

template <typename Callback>
void ValidateOptionalItems(const YAML::Node& parent, const char* field,
                           const std::string& parentPath,
                           Callback callback) {
  const YAML::Node items = parent[field];
  const std::string path = parentPath + "." + field;
  ValidateOptionalSequence(items, path);
  if (!HasSequenceItems(items)) {
    return;
  }

  size_t index = 0;
  for (YAML::Node item : items) {
    const std::string itemPath =
        path + "[" + std::to_string(index++) + "]";
    ValidateRequiredMap(item, itemPath);
    callback(item, itemPath);
  }
}

void ValidateLedConfigBlock(const YAML::Node& ledStripe, const char* field,
                            const std::string& ledStripePath) {
  ValidateOptionalItems(ledStripe, field, ledStripePath,
                        [](const YAML::Node& item,
                           const std::string& itemPath) {
                          ValidateRequiredField<std::string>(
                              item, itemPath, "description");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "number");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "ledNumber");
                          ParseRequiredHexColorField(
                              item, "color", ConfigItemContext(item, itemPath));
                        });
}

void ValidateNamedEffectTriggerFields(const YAML::Node& effect,
                                      const std::string& effectPath) {
  ValidateOptionalField<std::string>(effect, effectPath, "name");
  ValidateOptionalField<uint32_t>(effect, effectPath, "value");

  const YAML::Node simpleTrigger = effect["simpleTrigger"];
  if (simpleTrigger) {
    ValidateRequiredMap(simpleTrigger, effectPath + ".simpleTrigger");
    ValidateRequiredField<std::string>(simpleTrigger,
                                       effectPath + ".simpleTrigger",
                                       "source");
    ValidateRequiredField<uint32_t>(simpleTrigger,
                                    effectPath + ".simpleTrigger", "number");
    ValidateRequiredField<uint32_t>(simpleTrigger,
                                    effectPath + ".simpleTrigger", "value");
    const std::string source = simpleTrigger["source"].as<std::string>();
    if (source != "switch" && source != "lamp" && source != "light") {
      throw std::runtime_error(
          "invalid YAML configuration: " + effectPath +
          ".simpleTrigger.source must be 'switch' or 'lamp', got '" + source +
          "' at " + FormatYamlLocation(simpleTrigger["source"].Mark()));
    }
    const uint32_t value = simpleTrigger["value"].as<uint32_t>();
    if (value > 1) {
      throw std::runtime_error(
          "invalid YAML configuration: " + effectPath +
          ".simpleTrigger.value must be 0 or 1 at " +
          FormatYamlLocation(simpleTrigger["value"].Mark()));
    }
  }
}

void ValidatePpucConfiguration(const YAML::Node& config) {
  ValidateRequiredMap(config, "root");
  ValidateRequiredField<bool>(config, "root", "debug");
  ValidateRequiredField<std::string>(config, "root", "rom");
  ValidateRequiredField<std::string>(config, "root", "serialPort");
  ValidateRequiredField<std::string>(config, "root", "platform");
  ValidateRequiredField<uint8_t>(config, "root", "coinDoorClosedSwitch");
  ValidateRequiredField<uint8_t>(config, "root", "gameOnSolenoid");

  const YAML::Node boards = config["boards"];
  ValidateRequiredSequence(boards, "boards");
  size_t boardIndex = 0;
  for (YAML::Node board : boards) {
    const std::string path = "boards[" + std::to_string(boardIndex++) + "]";
    ValidateRequiredMap(board, path);
    ValidateRequiredField<uint8_t>(board, path, "number");
    ValidateRequiredField<bool>(board, path, "pollEvents");
  }

  const YAML::Node switchMatrix = config["switchMatrix"];
  if (switchMatrix) {
    ValidateRequiredMap(switchMatrix, "switchMatrix");
    ValidateRequiredField<uint8_t>(switchMatrix, "switchMatrix", "board");
    ValidateRequiredField<bool>(switchMatrix, "switchMatrix", "activeLow");
    ValidateRequiredField<uint8_t>(switchMatrix, "switchMatrix", "rows");
    ValidateOptionalItems(
        switchMatrix, "switches", "switchMatrix",
        [](const YAML::Node& item, const std::string& itemPath) {
          ValidateRequiredField<std::string>(item, itemPath, "description");
          ValidateRequiredField<uint8_t>(item, itemPath, "board");
          ValidateRequiredField<uint32_t>(item, itemPath, "port");
          ValidateRequiredField<uint32_t>(item, itemPath, "number");
          ValidateOptionalField<bool>(item, itemPath, "button");
        });
  }

  ValidateOptionalItems(config, "switches", "root",
                        [](const YAML::Node& item,
                           const std::string& itemPath) {
                          ValidateRequiredField<std::string>(
                              item, itemPath, "description");
                          ValidateRequiredField<uint8_t>(
                              item, itemPath, "board");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "port");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "number");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "debounce");
                          ValidateOptionalField<std::string>(
                              item, itemPath, "debounceMode");
                          ValidateOptionalField<std::string>(
                              item, itemPath, "debounce_mode");
                          ValidateOptionalField<bool>(
                              item, itemPath, "button");
                        });

  const YAML::Node switchGroups = config["switchGroups"];
  if (switchGroups) {
    if (!switchGroups.IsMap()) {
      throw std::runtime_error("invalid YAML configuration: 'switchGroups' "
                               "must be a map at " +
                               FormatYamlLocation(switchGroups.Mark()));
    }

    for (YAML::const_iterator it = switchGroups.begin();
         it != switchGroups.end(); ++it) {
      const std::string name = it->first.as<std::string>();
      if (name.empty()) {
        throw std::runtime_error(
            "invalid YAML configuration: switchGroups contains an empty group "
            "name at " +
            FormatYamlLocation(it->first.Mark()));
      }
      if (name == "buttons") {
        throw std::runtime_error(
            "invalid YAML configuration: switchGroups.buttons is reserved for "
            "switches with button: true");
      }

      const YAML::Node group = it->second;
      const std::string groupPath = "switchGroups." + name;
      ValidateRequiredMap(group, groupPath);
      ValidateRequiredSequence(group["switches"], groupPath + ".switches");
      size_t switchIndex = 0;
      for (YAML::Node switchNumber : group["switches"]) {
        try {
          switchNumber.as<uint16_t>();
        } catch (const YAML::Exception& e) {
          throw std::runtime_error(
              "invalid YAML configuration: " + groupPath + ".switches[" +
              std::to_string(switchIndex) + "] at " +
              FormatYamlLocation(switchNumber.Mark()) + ": " + e.what());
        }
        ++switchIndex;
      }
    }
  }

  ValidateOptionalItems(config, "coilGiMappings", "root",
                        [](const YAML::Node& item,
                           const std::string& itemPath) {
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "coil");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "gi");
                          ValidateOptionalField<uint32_t>(
                              item, itemPath, "onBrightness");
                          ValidateOptionalField<uint32_t>(
                              item, itemPath, "offBrightness");
                        });

  ValidateOptionalItems(config, "pwmOutput", "root",
                        [](const YAML::Node& item,
                           const std::string& itemPath) {
                          ValidateRequiredField<std::string>(
                              item, itemPath, "description");
                          ValidateRequiredField<uint8_t>(
                              item, itemPath, "board");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "port");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "number");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "power");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "minPulseTime");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "maxPulseTime");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "holdPower");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "holdPowerActivationTime");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "fastFlipSwitch");
                          ValidateRequiredField<std::string>(
                              item, itemPath, "type");
                          ValidateOptionalField<bool>(
                              item, itemPath, "ballSearch");
                          // Declares a coil that carries its own hold winding,
                          // so an unbounded pulse on the power winding is not
                          // the fire risk it would otherwise be. See
                          // WarnAboutUnprotectedSolenoids below.
                          ValidateOptionalField<bool>(
                              item, itemPath, "dualWinding");
                          // The end-of-stroke contact, when it is wired back to
                          // an input rather than only breaking the power
                          // winding. Optional even for a dual-wound coil.
                          ValidateOptionalField<uint32_t>(
                              item, itemPath, "eosSwitch");
                          ValidateOptionalItems(
                              item, "effects", itemPath,
                              [](const YAML::Node& effect,
                                 const std::string& effectPath) {
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "duration");
                                ValidateEffectModeField(
                                    effect, effectPath, "effect");
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "frequency");
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "maxIntensity");
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "minIntensity");
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "mode");
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "priority");
                                ValidateRequiredField<int16_t>(
                                    effect, effectPath, "repeat");
                                ValidateNamedEffectTriggerFields(
                                    effect, effectPath);
                              });
                        });

  ValidateOptionalItems(config, "ledStripes", "root",
                        [](const YAML::Node& item,
                           const std::string& itemPath) {
                          ValidateRequiredField<uint8_t>(
                              item, itemPath, "board");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "port");
                          ValidateRequiredField<std::string>(
                              item, itemPath, "ledType");
                          const std::string ledType =
                              item["ledType"].as<std::string>();
                          if (ResolveLedTypeValue(ledType) == 0) {
                            throw std::runtime_error(
                                "invalid YAML configuration: " + itemPath +
                                ".ledType has unsupported value '" + ledType +
                                "' at " +
                                FormatYamlLocation(item["ledType"].Mark()));
                          }
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "brightness");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "amount");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "afterGlow");
                          ValidateRequiredField<uint32_t>(
                              item, itemPath, "lightUp");

                          ValidateOptionalItems(
                              item, "segments", itemPath,
                              [](const YAML::Node& segment,
                                 const std::string& segmentPath) {
                                ValidateRequiredField<uint32_t>(
                                    segment, segmentPath, "number");
                                ValidateRequiredField<uint32_t>(
                                    segment, segmentPath, "from");
                                ValidateRequiredField<uint32_t>(
                                    segment, segmentPath, "to");
                              });

                          ValidateOptionalItems(
                              item, "effects", itemPath,
                              [](const YAML::Node& effect,
                                 const std::string& effectPath) {
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "segment");
                                ValidateLedEffectColorFields(effect,
                                                             effectPath);
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "duration");
                                ValidateEffectModeField(
                                    effect, effectPath, "effect");
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "reverse");
                                ValidateOptionalField<uint32_t>(
                                    effect, effectPath, "fadeRate");
                                ValidateOptionalField<bool>(
                                    effect, effectPath, "gamma");
                                ValidateOptionalField<uint32_t>(
                                    effect, effectPath, "size");
                                ValidateOptionalField<uint32_t>(
                                    effect, effectPath, "options");
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "speed");
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "mode");
                                ValidateRequiredField<uint32_t>(
                                    effect, effectPath, "priority");
                                ValidateRequiredField<int16_t>(
                                    effect, effectPath, "repeat");
                                ValidateNamedEffectTriggerFields(
                                    effect, effectPath);
                              });

                          ValidateLedConfigBlock(item, "lamps", itemPath);
                          ValidateLedConfigBlock(item, "flashers", itemPath);
                          ValidateLedConfigBlock(item, "gi", itemPath);
                        });
}

std::unordered_map<std::string, std::vector<uint16_t>> ParseSwitchGroups(
    const YAML::Node& config) {
  std::unordered_map<std::string, std::vector<uint16_t>> groups;

  auto addButtonSwitches = [&groups](const YAML::Node& switches) {
    if (!HasSequenceItems(switches)) {
      return;
    }
    for (YAML::Node n_switch : switches) {
      if (n_switch["button"] && n_switch["button"].as<bool>()) {
        groups["buttons"].push_back(n_switch["number"].as<uint16_t>());
      }
    }
  };

  const YAML::Node switchMatrix = config["switchMatrix"];
  if (switchMatrix) {
    addButtonSwitches(switchMatrix["switches"]);
  }
  addButtonSwitches(config["switches"]);

  const YAML::Node switchGroups = config["switchGroups"];
  if (switchGroups) {
    for (YAML::const_iterator it = switchGroups.begin();
         it != switchGroups.end(); ++it) {
      const std::string name = it->first.as<std::string>();
      std::vector<uint16_t> numbers;
      for (YAML::Node switchNumber : it->second["switches"]) {
        numbers.push_back(switchNumber.as<uint16_t>());
      }
      groups[name] = std::move(numbers);
    }
  }

  for (auto& entry : groups) {
    auto& numbers = entry.second;
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
  }

  return groups;
}

std::vector<PPUCCoilGiMapping> ParseCoilGiMappings(const YAML::Node& config) {
  std::vector<PPUCCoilGiMapping> mappings;
  const YAML::Node coilGiMappings = config["coilGiMappings"];
  if (!HasSequenceItems(coilGiMappings)) {
    return mappings;
  }

  for (YAML::Node item : coilGiMappings) {
    PPUCCoilGiMapping mapping;
    mapping.coil = item["coil"].as<uint16_t>();
    mapping.gi = item["gi"].as<uint8_t>();
    mapping.onBrightness =
        item["onBrightness"] ? item["onBrightness"].as<uint8_t>() : 8;
    mapping.offBrightness =
        item["offBrightness"] ? item["offBrightness"].as<uint8_t>() : 0;
    mappings.push_back(mapping);
  }

  return mappings;
}

}  // namespace

PPUC::PPUC() {
  m_rom = (char*)malloc(16);
  m_serial = (char*)malloc(128);

  m_pRS485Comm = new RS485Comm();
}

PPUC::~PPUC() {
  m_pRS485Comm->Disconnect();
  delete m_pRS485Comm;
}

void PPUC::SetLogMessageCallback(PPUC_LogMessageCallback callback,
                                 const void* userData) {
  m_pRS485Comm->SetLogMessageCallback(callback, userData);
}

void PPUC::Disconnect() { m_pRS485Comm->Disconnect(); }

uint8_t PPUC::ResolveLedType(const std::string& type) {
  return ResolveLedTypeValue(type);
}

namespace {
// Defined below, next to ResolvePwmType which it depends on.
void WarnAboutUnprotectedSolenoids(const YAML::Node& config);
}  // namespace

void PPUC::LoadConfiguration(const char* configFile) {
  // Load config file. But options set via command line are preferred.
  try {
    m_ppucConfig = YAML::LoadFile(configFile);
    ValidatePpucConfiguration(m_ppucConfig);
    WarnAboutUnprotectedSolenoids(m_ppucConfig);
    m_switchGroups = ParseSwitchGroups(m_ppucConfig);
    m_coilGiMappings = ParseCoilGiMappings(m_ppucConfig);
  } catch (const YAML::Exception& e) {
    throw std::runtime_error(
        "invalid YAML configuration in '" + std::string(configFile) + "' at " +
        FormatYamlLocation(e.mark) + ": " + e.what());
  }

  const std::string rootContext =
      ConfigItemContext(m_ppucConfig, "root");
  m_debug = ReadRequiredYamlField<bool>(m_ppucConfig, "debug", rootContext);
  std::string c_rom =
      ReadRequiredYamlField<std::string>(m_ppucConfig, "rom", rootContext);
  strcpy(m_rom, c_rom.c_str());
  std::string c_serial = ReadRequiredYamlField<std::string>(
      m_ppucConfig, "serialPort", rootContext);
  strcpy(m_serial, c_serial.c_str());
  std::string c_platform = ReadRequiredYamlField<std::string>(
      m_ppucConfig, "platform", rootContext);
  m_platform = PLATFORM_WPC;
  if (strcmp(c_platform.c_str(), "WPC") == 0) {
    m_platform = PLATFORM_WPC;
  } else if (strcmp(c_platform.c_str(), "DE") == 0) {
    m_platform = PLATFORM_DATA_EAST;
  } else if (strcmp(c_platform.c_str(), "SYS3") == 0) {
    m_platform = PLATFORM_SYS3;
  } else if (strcmp(c_platform.c_str(), "SYS4") == 0) {
    m_platform = PLATFORM_SYS4;
  } else if (strcmp(c_platform.c_str(), "SYS6") == 0) {
    m_platform = PLATFORM_SYS6;
  } else if (strcmp(c_platform.c_str(), "SYS7") == 0) {
    m_platform = PLATFORM_SYS7;
  } else if (strcmp(c_platform.c_str(), "SYS11") == 0) {
    m_platform = PLATFORM_SYS11;
  } else if (strcmp(c_platform.c_str(), "BALLY35") == 0) {
    m_platform = PLATFORM_BALLY35;
  } else if (strcmp(c_platform.c_str(), "WHITESTAR") == 0) {
    m_platform = PLATFORM_WHITESTAR;
  } else if (strcmp(c_platform.c_str(), "SAM") == 0) {
    m_platform = PLATFORM_SAM;
  } else if (strcmp(c_platform.c_str(), "CAPCOM") == 0) {
    m_platform = PLATFORM_CAPCOM;
  } else {
    // Default unknown platforms to non-WPC behavior so features like
    // always-on GI do not silently disappear on older systems.
    m_platform = PLATFORM_SYS11;
  }
}

void PPUC::SetDebug(bool debug) {
  m_pRS485Comm->SetDebug(debug);
  m_debug = debug;
}

void PPUC::SetDebugErrors(bool debugErrors) {
  m_pRS485Comm->SetDebugErrors(debugErrors);
}

void PPUC::SetSkippedBoardsCsv(const char* skippedBoardsCsv) {
  m_skippedBoards.clear();

  if (!skippedBoardsCsv || skippedBoardsCsv[0] == '\0') {
    return;
  }

  const std::string csv(skippedBoardsCsv);
  size_t start = 0;
  while (start < csv.size()) {
    const size_t end = csv.find(',', start);
    const std::string token =
        csv.substr(start, end == std::string::npos ? std::string::npos
                                                   : end - start);
    if (!token.empty()) {
      char* parseEnd = nullptr;
      const long value = strtol(token.c_str(), &parseEnd, 10);
      if (parseEnd != token.c_str() && *parseEnd == '\0' &&
          value >= 0 && value <= 255) {
        m_skippedBoards.insert(static_cast<uint8_t>(value));
      }
    }

    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
}

void PPUC::SetSwitchReplyDelayUs(uint32_t delayUs) {
  m_switchReplyDelayUs = delayUs;
  m_pRS485Comm->SetSwitchReplyDelayUs(delayUs);
}

void PPUC::SetCoilHoldFrames(uint8_t holdFrames) {
  m_coilHoldFrames = holdFrames;
  m_pRS485Comm->SetCoilHoldFrames(holdFrames);
}

void PPUC::SetOutputFrameIntervalMs(uint32_t intervalMs) {
  m_pRS485Comm->SetOutputFrameIntervalMs(intervalMs);
}

void PPUC::SetDisableFastFlipForTests(bool disableFastFlipForTests) {
  m_disableFastFlipForTests = disableFastFlipForTests;
}

void PPUC::SetForceHardReset(bool forceHardReset) {
  m_forceHardReset = forceHardReset;
}

bool PPUC::GetDebug() { return m_debug; }

void PPUC::SetRom(const char* rom) { strcpy(m_rom, rom); }

const char* PPUC::GetRom() { return m_rom; }

void PPUC::SetSerial(const char* serial) { strcpy(m_serial, serial); }

const char* PPUC::GetSerial() { return m_serial; }

bool PPUC::AbortConfigurationEarly() const {
  return m_pRS485Comm->ShouldAbortConfigurationEarly();
}

namespace {
uint32_t ResolvePwmType(const std::string& type) {
  if (type == "flasher") {
    return PWM_TYPE_FLASHER;
  }
  if (type == "lamp") {
    return PWM_TYPE_LAMP;
  }
  if (type == "motor") {
    return PWM_TYPE_MOTOR;
  }
  if (type == "shaker") {
    return PWM_TYPE_SHAKER;
  }
  return PWM_TYPE_SOLENOID;
}

// Whether leaving this device energised indefinitely damages something.
//
// Solenoids, motors and shakers move mass and dissipate real power. Flashers
// count too: a flasher driven from a PWM output is an incandescent bulb behind
// a driver transistor, and holding it on cooks the bulb and its socket.
//
// A flasher mapped to a WS2812 needs no such protection, and is not a
// consideration here: addressable LEDs are configured as a role under
// `ledStripes`, never as a `pwmOutput` entry. Anything reaching this function
// is driven from a real PWM output.
//
// Lamps are the exception - a lamp output is meant to sit on indefinitely.
bool PwmTypeNeedsThermalProtection(const std::string& type) {
  return ResolvePwmType(type) != PWM_TYPE_LAMP;
}

// Reports coils that nothing bounds.
//
// A solenoid needs at least one mechanism to stop it staying energised:
//
//   1. maxPulseTime > 0        - the board drops it after that long
//   2. hold power              - holdPower > 0 with holdPowerActivationTime,
//                                so it falls back to a current the coil can
//                                survive continuously
//   3. dualWinding: true       - the coil has its own hold winding, and its
//                                EOS contact transfers to it mechanically
//
// With none of them, a single-winding coil that the ROM leaves on burns out,
// and takes the driver transistor and possibly more with it.
//
// This warns rather than rejects, for one release. Every existing game config
// predates the dualWinding field, so a correctly wired dual-wound flipper is
// indistinguishable from an unprotected kicker until those configs are
// re-exported. Refusing to start the machine over that would cost more than
// it protects. The intent is to make it an error once configs have caught up.
void WarnAboutUnprotectedSolenoids(const YAML::Node& config) {
  const YAML::Node& pwmOutput = config["pwmOutput"];
  if (!HasSequenceItems(pwmOutput)) {
    return;
  }

  size_t index = 0;
  for (const YAML::Node& item : pwmOutput) {
    const std::string itemPath = "pwmOutput[" + std::to_string(index++) + "]";

    if (!PwmTypeNeedsThermalProtection(item["type"].as<std::string>())) {
      continue;
    }

    const uint32_t maxPulseTime = item["maxPulseTime"].as<uint32_t>();
    const uint32_t holdPower = item["holdPower"].as<uint32_t>();
    const uint32_t holdPowerActivationTime =
        item["holdPowerActivationTime"].as<uint32_t>();
    const bool dualWinding =
        item["dualWinding"] && item["dualWinding"].as<bool>();

    if (maxPulseTime > 0 || (holdPower > 0 && holdPowerActivationTime > 0) ||
        dualWinding) {
      continue;
    }

    const std::string description =
        item["description"] ? item["description"].as<std::string>() : "";

    printf(
        "PPUC: WARNING: %s ('%s', number %u) has no thermal protection: "
        "maxPulseTime is 0, holdPower is 0 and dualWinding is not set.\n"
        "PPUC: WARNING: nothing bounds how long this device can stay "
        "energised. Set maxPulseTime, or holdPower together with "
        "holdPowerActivationTime, or declare 'dualWinding: true' if this coil "
        "has its own hold winding and an EOS contact. At %s.\n",
        itemPath.c_str(), description.c_str(),
        static_cast<unsigned>(item["number"].as<uint32_t>()),
        FormatYamlLocation(item.Mark()).c_str());
  }
}

bool IsDecimalScalar(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return std::isdigit(c) != 0;
         });
}

uint32_t ResolveLedEffectMode(const YAML::Node& node) {
  const std::string value = node.as<std::string>();
  if (IsDecimalScalar(value)) {
    return node.as<uint32_t>();
  }

  static const std::unordered_map<std::string, uint32_t> kModes = {
      {"static", 0},          {"blink", 1},          {"breath", 2},
      {"color_wipe", 3},      {"scan", 13},          {"running_lights", 18},
      {"twinkle_fade_random", 22},                   {"sparkle", 23},
      {"strobe", 26},         {"chase_rainbow", 33}, {"running_color", 40},
      {"running_random", 42}, {"larson_scanner", 43},{"comet", 44},
      {"fireworks", 45},      {"fire_flicker", 48},  {"tricolor_chase", 54},
      {"twinklefox", 55},     {"rain", 56},          {"heartbeat", 64},
      {"multi_comet", 66},    {"popcorn", 68},       {"oscillator", 69},
  };

  const auto it = kModes.find(value);
  if (it == kModes.end()) {
    throw YAML::Exception(node.Mark(), "unknown LED effect '" + value + "'");
  }
  return it->second;
}

uint32_t ResolvePwmEffectMode(const YAML::Node& node) {
  const std::string value = node.as<std::string>();
  if (IsDecimalScalar(value)) {
    return node.as<uint32_t>();
  }

  static const std::unordered_map<std::string, uint32_t> kModes = {
      {"sine", PWM_EFFECT_SINE},
      {"ramp_down_stop", PWM_EFFECT_RAMP_DOWN_STOP},
      {"impulse", PWM_EFFECT_IMPULSE},
  };

  const auto it = kModes.find(value);
  if (it == kModes.end()) {
    throw YAML::Exception(node.Mark(), "unknown PWM effect '" + value + "'");
  }
  return it->second;
}
}  // namespace

uint32_t PPUC::ResolveSwitchDebounceMode(const YAML::Node& node) {
  if (!node) {
    return SWITCH_DEBOUNCE_STANDARD;
  }

  const std::string value = node.as<std::string>();
  if (IsDecimalScalar(value)) {
    return node.as<uint32_t>();
  }

  static const std::unordered_map<std::string, uint32_t> kModes = {
      {"standard", SWITCH_DEBOUNCE_STANDARD},
      {"fastFlip", SWITCH_DEBOUNCE_FAST_FLIP},
      {"fast_flip", SWITCH_DEBOUNCE_FAST_FLIP},
      {"slowStable", SWITCH_DEBOUNCE_SLOW_STABLE},
      {"slow_stable", SWITCH_DEBOUNCE_SLOW_STABLE},
  };

  const auto it = kModes.find(value);
  if (it == kModes.end()) {
    throw YAML::Exception(node.Mark(),
                          "unknown switch debounce mode '" + value + "'");
  }
  return it->second;
}

void SendNamedEffectTriggerConfig(RS485Comm* comm, const YAML::Node& effectNode,
                                  uint32_t type, uint8_t board,
                                  uint32_t port) {
  if (!comm || !effectNode || !effectNode["name"]) {
    return;
  }

  const std::string name = effectNode["name"].as<std::string>();
  if (name.find_first_not_of(" \t\r\n") == std::string::npos) {
    return;
  }

  const uint32_t value = effectNode["value"] ? effectNode["value"].as<uint32_t>()
                                             : 1u;
  const uint32_t number = HashNamedTriggerId(name.c_str());

  uint8_t index = 0;
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_PORT,
                                        port));
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_TYPE,
                                        type));
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_SOURCE,
                                        EVENT_SOURCE_EFFECT));
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_NUMBER,
                                        number));
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_VALUE,
                                        value));
}

uint32_t ResolveSimpleTriggerSource(const std::string& source) {
  if (source == "switch") {
    return EVENT_SOURCE_SWITCH;
  }
  if (source == "lamp" || source == "light") {
    return EVENT_SOURCE_LIGHT;
  }
  throw std::runtime_error("unsupported simple trigger source '" + source +
                           "'");
}

void SendSimpleEffectTriggerConfig(RS485Comm* comm, const YAML::Node& effectNode,
                                   uint32_t type, uint8_t board,
                                   uint32_t port,
                                   const std::string& context) {
  if (!comm || !effectNode || !effectNode["simpleTrigger"]) {
    return;
  }

  const YAML::Node trigger = effectNode["simpleTrigger"];
  const uint32_t source =
      ResolveSimpleTriggerSource(trigger["source"].as<std::string>());
  const uint32_t number = trigger["number"].as<uint32_t>();
  const uint32_t value = trigger["value"].as<uint32_t>();
  if (value > 1) {
    throw std::runtime_error("invalid YAML configuration: " + context +
                             ".simpleTrigger.value must be 0 or 1");
  }

  uint8_t index = 0;
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_PORT,
                                        port));
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_TYPE,
                                        type));
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_SOURCE,
                                        source));
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_NUMBER,
                                        number));
  comm->SendConfigEvent(new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER,
                                        index++, (uint8_t)CONFIG_TOPIC_VALUE,
                                        value));
}

void PPUC::SendLedConfigBlock(const YAML::Node& items, uint32_t type,
                              uint8_t board, uint32_t port) {
  if (HasSequenceItems(items)) {
    size_t itemIndex = 0;
    for (YAML::Node n_item : items) {
      const std::string context =
          LedConfigItemContext(n_item, type, board, port, itemIndex);
      ++itemIndex;

      if (AbortConfigurationEarly()) {
        return;
      }
      const std::string description =
          ReadRequiredYamlField<std::string>(n_item, "description", context);
      if (m_debug) {
        // @todo user logger
        printf("Description: %s\n", description.c_str());
      }

      const uint32_t number =
          ReadRequiredYamlField<uint32_t>(n_item, "number", context);
      const uint32_t ledNumber =
          ReadRequiredYamlField<uint32_t>(n_item, "ledNumber", context);
      const uint32_t color =
          ParseRequiredHexColorField(n_item, "color", context);

      uint8_t index = 0;
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
                          (uint8_t)CONFIG_TOPIC_PORT, port));
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
                          (uint8_t)CONFIG_TOPIC_TYPE, type));
      m_pRS485Comm->SendConfigEvent(new ConfigEvent(
          board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
          (uint8_t)CONFIG_TOPIC_NUMBER, number));
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
                          (uint8_t)CONFIG_TOPIC_LED_NUMBER, ledNumber));

      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
                          (uint8_t)CONFIG_TOPIC_COLOR, color));

      m_lamps.push_back(
          PPUCLamp(board, port, (uint8_t)type, static_cast<uint8_t>(number),
                   description, color));
    }
  }
}

bool PPUC::Connect() {
  try {
    ValidatePpucConfiguration(m_ppucConfig);
  } catch (const std::exception& e) {
    printf("PPUC: %s\n", e.what());
    return false;
  }

  if (!m_pRS485Comm->Connect(m_serial)) {
    return false;
  }

  auto startupAttempt = [this]() -> bool {
    m_coils.clear();
    m_lamps.clear();
    m_switches.clear();

    auto isSkippedBoard = [this](uint8_t boardNumber) {
      return m_skippedBoards.count(boardNumber) != 0;
    };

    uint8_t index = 0;
    std::vector<uint8_t> switchBoards;
    std::set<uint16_t> coilNumbers;
    std::set<uint16_t> lampNumbers;
    std::set<uint16_t> switchNumbers;
    std::set<uint16_t> buttonSwitchNumbers;
    std::unordered_map<uint8_t, std::vector<uint16_t>> switchNumbersByBoard;
    const YAML::Node& boards = m_ppucConfig["boards"];
    std::vector<uint8_t> configuredBoards;
    for (YAML::Node n_board : boards) {
      const uint8_t boardNumber = n_board["number"].as<uint8_t>();
      configuredBoards.push_back(boardNumber);
      if (isSkippedBoard(boardNumber)) {
        continue;
      }

      m_pRS485Comm->SendConfigEvent(new ConfigEvent(
          boardNumber, (uint8_t)CONFIG_TOPIC_PLATFORM, 0,
          (uint8_t)CONFIG_TOPIC_PLATFORM, m_platform));

      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(boardNumber,
                          (uint8_t)CONFIG_TOPIC_COIN_DOOR_CLOSED_SWITCH, 0,
                          (uint8_t)CONFIG_TOPIC_NUMBER,
                          m_ppucConfig["coinDoorClosedSwitch"].as<uint8_t>()));
      m_coinDoorClosedSwitch =
          m_ppucConfig["coinDoorClosedSwitch"].as<uint8_t>();

      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(boardNumber,
                          (uint8_t)CONFIG_TOPIC_GAME_ON_SOLENOID, 0,
                          (uint8_t)CONFIG_TOPIC_NUMBER,
                          m_ppucConfig["gameOnSolenoid"].as<uint8_t>()));
      m_gameOnSolenoid = m_ppucConfig["gameOnSolenoid"].as<uint8_t>();

      if (n_board["pollEvents"].as<bool>()) {
        m_pRS485Comm->RegisterSwitchBoard(boardNumber);
        switchBoards.push_back(boardNumber);
      }

      if (AbortConfigurationEarly()) {
        return false;
      }
    }

    if (AbortConfigurationEarly()) {
      return false;
    }

    coilNumbers.insert(m_gameOnSolenoid);

    const YAML::Node& switchMatrix = m_ppucConfig["switchMatrix"];
    if (switchMatrix) {
      const YAML::Node& matrixSwitches = switchMatrix["switches"];
      if (HasSequenceItems(matrixSwitches)) {
        for (YAML::Node n_switch : matrixSwitches) {
          const uint16_t switchNumber = n_switch["number"].as<uint16_t>();
          switchNumbers.insert(switchNumber);
          if (n_switch["button"] && n_switch["button"].as<bool>()) {
            buttonSwitchNumbers.insert(switchNumber);
          }
          switchNumbersByBoard[n_switch["board"].as<uint8_t>()].push_back(
              switchNumber);
        }
      }
    }

    const YAML::Node& switches = m_ppucConfig["switches"];
    if (HasSequenceItems(switches)) {
      for (YAML::Node n_switch : switches) {
        const uint16_t switchNumber = n_switch["number"].as<uint16_t>();
        switchNumbers.insert(switchNumber);
        if (n_switch["button"] && n_switch["button"].as<bool>()) {
          buttonSwitchNumbers.insert(switchNumber);
        }
        switchNumbersByBoard[n_switch["board"].as<uint8_t>()].push_back(
            switchNumber);
      }
    }

    const YAML::Node& pwmOutput = m_ppucConfig["pwmOutput"];
    if (HasSequenceItems(pwmOutput)) {
      for (YAML::Node n_pwmOutput : pwmOutput) {
        if (isSkippedBoard(n_pwmOutput["board"].as<uint8_t>())) {
          continue;
        }
        std::string c_type = n_pwmOutput["type"].as<std::string>();
        const uint16_t number = n_pwmOutput["number"].as<uint16_t>();
        if (strcmp(c_type.c_str(), "lamp") == 0) {
          lampNumbers.insert(number);
        } else {
          coilNumbers.insert(number);
        }
      }
    }

    const YAML::Node& ledStripes = m_ppucConfig["ledStripes"];
    if (HasSequenceItems(ledStripes)) {
      for (YAML::Node n_ledStripe : ledStripes) {
        if (isSkippedBoard(n_ledStripe["board"].as<uint8_t>())) {
          continue;
        }
        const YAML::Node& lamps = n_ledStripe["lamps"];
        if (HasSequenceItems(lamps)) {
          for (YAML::Node n_lamp : lamps) {
            lampNumbers.insert(n_lamp["number"].as<uint16_t>());
          }
        }
        const YAML::Node& flashers = n_ledStripe["flashers"];
        if (HasSequenceItems(flashers)) {
          for (YAML::Node n_flasher : flashers) {
            coilNumbers.insert(n_flasher["number"].as<uint16_t>());
          }
        }
      }
    }

    std::vector<uint16_t> coilMapping(coilNumbers.begin(), coilNumbers.end());
    std::vector<uint16_t> lampMapping(lampNumbers.begin(), lampNumbers.end());
    std::vector<uint16_t> switchMapping(switchNumbers.begin(),
                                        switchNumbers.end());

    ppuc::v2::RuntimeConfig runtimeConfig;
    runtimeConfig.coilBits =
        std::max<uint16_t>(1, static_cast<uint16_t>(coilMapping.size()));
    runtimeConfig.lampBits =
        std::max<uint16_t>(1, static_cast<uint16_t>(lampMapping.size()));
    runtimeConfig.switchBits =
        std::max<uint16_t>(1, static_cast<uint16_t>(switchMapping.size()));
    runtimeConfig.coilBits =
        std::min<uint16_t>(runtimeConfig.coilBits, ppuc::v2::kMaxCoilBits);
    runtimeConfig.lampBits =
        std::min<uint16_t>(runtimeConfig.lampBits, ppuc::v2::kMaxLampBits);
    runtimeConfig.switchBits =
        std::min<uint16_t>(runtimeConfig.switchBits, ppuc::v2::kMaxSwitchBits);
    coilMapping.resize(runtimeConfig.coilBits);
    lampMapping.resize(runtimeConfig.lampBits);
    switchMapping.resize(runtimeConfig.switchBits);
    m_pRS485Comm->SetMappings(coilMapping, lampMapping, switchMapping);
    m_pRS485Comm->SetRuntimeConfig(runtimeConfig);
    m_pRS485Comm->SetConfiguredBoards(configuredBoards);
    m_pRS485Comm->SetSwitchNumbersByBoard(switchNumbersByBoard);
    m_pRS485Comm->SetButtonSwitchNumbers(buttonSwitchNumbers);
    m_pRS485Comm->SetSkippedBoards(m_skippedBoards);

    // Send switch matrix configuration to I/O boards
    // IMPORTANT: This must be done before sending individual switch configs
    // because the existence of a switch matrix changes the amount of dedicated
    // switches available.
    if (switchMatrix &&
        !isSkippedBoard(switchMatrix["board"].as<uint8_t>())) {
      index = 0;
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                          (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                          (uint8_t)CONFIG_TOPIC_ACTIVE_LOW,
                          switchMatrix["activeLow"].as<bool>()));
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                          (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                          (uint8_t)CONFIG_TOPIC_NUM_ROWS,
                          switchMatrix["rows"].as<uint8_t>()));

      const YAML::Node& switches = switchMatrix["switches"];
      if (HasSequenceItems(switches)) {
        for (YAML::Node n_switch : switches) {
          if (m_debug) {
            // @todo user logger
            printf("Description: %s\n",
                   n_switch["description"].as<std::string>().c_str());
          }

          if (!isSkippedBoard(n_switch["board"].as<uint8_t>())) {
            index = 0;
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                n_switch["board"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                (uint8_t)CONFIG_TOPIC_PORT, n_switch["port"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                n_switch["board"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                (uint8_t)CONFIG_TOPIC_NUMBER,
                n_switch["number"].as<uint32_t>()));
          }

          m_switches.push_back(PPUCSwitch(
              n_switch["board"].as<uint8_t>(), n_switch["port"].as<uint8_t>(),
              n_switch["number"].as<uint8_t>(),
              n_switch["description"].as<std::string>(),
              n_switch["button"] && n_switch["button"].as<bool>()));
        }
      }
    }

    if (AbortConfigurationEarly()) {
      return false;
    }

    // Send switch configuration to I/O boards
    if (HasSequenceItems(switches)) {
      for (YAML::Node n_switch : switches) {
        if (m_debug) {
          // @todo user logger
          printf("Description: %s\n",
                 n_switch["description"].as<std::string>().c_str());
        }

        if (!isSkippedBoard(n_switch["board"].as<uint8_t>())) {
          index = 0;
          m_pRS485Comm->SendConfigEvent(new ConfigEvent(
              n_switch["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_SWITCHES,
              index++, (uint8_t)CONFIG_TOPIC_PORT,
              n_switch["port"].as<uint32_t>()));
          m_pRS485Comm->SendConfigEvent(new ConfigEvent(
              n_switch["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_SWITCHES,
              index++, (uint8_t)CONFIG_TOPIC_NUMBER,
              n_switch["number"].as<uint32_t>()));
          m_pRS485Comm->SendConfigEvent(new ConfigEvent(
              n_switch["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_SWITCHES,
              index++, (uint8_t)CONFIG_TOPIC_DEBOUNCE_TIME,
              n_switch["debounce"].as<uint32_t>()));
          YAML::Node debounceMode =
              n_switch["debounceMode"] ? n_switch["debounceMode"]
                                       : n_switch["debounce_mode"];
          m_pRS485Comm->SendConfigEvent(new ConfigEvent(
              n_switch["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_SWITCHES,
              index++, (uint8_t)CONFIG_TOPIC_MODE,
              ResolveSwitchDebounceMode(debounceMode)));
        }

        m_switches.push_back(PPUCSwitch(
            n_switch["board"].as<uint8_t>(), n_switch["port"].as<uint8_t>(),
            n_switch["number"].as<uint8_t>(),
            n_switch["description"].as<std::string>(),
            n_switch["button"] && n_switch["button"].as<bool>()));

        if (AbortConfigurationEarly()) {
          return false;
        }
      }
    }

    if (AbortConfigurationEarly()) {
      return false;
    }

    // Send PWM configuration to I/O boards
    if (HasSequenceItems(pwmOutput)) {
      for (YAML::Node n_pwmOutput : pwmOutput) {
        if (isSkippedBoard(n_pwmOutput["board"].as<uint8_t>())) {
          continue;
        }
        if (m_debug) {
          // @todo user logger
          printf("Description: %s\n",
                 n_pwmOutput["description"].as<std::string>().c_str());
        }

        index = 0;
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_PORT,
            n_pwmOutput["port"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_NUMBER,
            n_pwmOutput["number"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_POWER,
            n_pwmOutput["power"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_MIN_PULSE_TIME,
            n_pwmOutput["minPulseTime"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_MAX_PULSE_TIME,
            n_pwmOutput["maxPulseTime"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_HOLD_POWER,
            n_pwmOutput["holdPower"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_HOLD_POWER_ACTIVATION_TIME,
            n_pwmOutput["holdPowerActivationTime"].as<uint32_t>()));
        const uint32_t fastSwitch =
            m_disableFastFlipForTests ? 0u
                                      : n_pwmOutput["fastFlipSwitch"].as<uint32_t>();
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_FAST_SWITCH, fastSwitch));
        std::string c_type = n_pwmOutput["type"].as<std::string>();
        uint32_t type = ResolvePwmType(c_type);
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_TYPE, type));

        const YAML::Node& pwm_effects = n_pwmOutput["effects"];
        if (HasSequenceItems(pwm_effects)) {
          for (YAML::Node n_pwm_effect : pwm_effects) {
            index = 0;
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_PORT,
                                n_pwmOutput["port"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_DURATION,
                                n_pwm_effect["duration"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_EFFECT,
                                ResolvePwmEffectMode(n_pwm_effect["effect"])));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_FREQUENCY,
                                n_pwm_effect["frequency"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_MAX_INTENSITY,
                                n_pwm_effect["maxIntensity"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_MIN_INTENSITY,
                                n_pwm_effect["minIntensity"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_MODE,
                                n_pwm_effect["mode"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_PRIORITY,
                                n_pwm_effect["priority"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_REPEAT,
                                n_pwm_effect["repeat"].as<int16_t>() == -1
                                    ? 255
                                    : (n_pwm_effect["repeat"].as<int16_t>() == -2
                                           ? 254
                                           : n_pwm_effect["repeat"]
                                                 .as<uint32_t>())));

            SendNamedEffectTriggerConfig(
                m_pRS485Comm, n_pwm_effect, CONFIG_TOPIC_PWM_EFFECT,
                n_pwmOutput["board"].as<uint8_t>(),
                n_pwmOutput["port"].as<uint32_t>());
            SendSimpleEffectTriggerConfig(
                m_pRS485Comm, n_pwm_effect, CONFIG_TOPIC_PWM_EFFECT,
                n_pwmOutput["board"].as<uint8_t>(),
                n_pwmOutput["port"].as<uint32_t>(),
                ConfigItemContext(n_pwm_effect, "pwmOutput.effects"));

            if (AbortConfigurationEarly()) {
              return false;
            }
          }
        }

        m_coils.push_back(
            PPUCCoil(n_pwmOutput["board"].as<uint8_t>(),
                     n_pwmOutput["port"].as<uint8_t>(), (uint8_t)type,
                     n_pwmOutput["number"].as<uint8_t>(),
                     n_pwmOutput["description"].as<std::string>(),
                     n_pwmOutput["ballSearch"] &&
                         n_pwmOutput["ballSearch"].as<bool>()));

        if (AbortConfigurationEarly()) {
          return false;
        }
      }
    }

    if (AbortConfigurationEarly()) {
      return false;
    }

    // Send LED configuration to I/O boards
    if (HasSequenceItems(ledStripes)) {
      for (YAML::Node n_ledStripe : ledStripes) {
        if (isSkippedBoard(n_ledStripe["board"].as<uint8_t>())) {
          continue;
        }
        index = 0;
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_ledStripe["board"].as<uint8_t>(),
            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
            (uint8_t)CONFIG_TOPIC_PORT, n_ledStripe["port"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_ledStripe["board"].as<uint8_t>(),
            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
            (uint8_t)CONFIG_TOPIC_TYPE,
            ResolveLedType(n_ledStripe["ledType"].as<std::string>())));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
                            (uint8_t)CONFIG_TOPIC_BRIGHTNESS,
                            n_ledStripe["brightness"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
                            (uint8_t)CONFIG_TOPIC_AMOUNT_LEDS,
                            n_ledStripe["amount"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
                            (uint8_t)CONFIG_TOPIC_AFTER_GLOW,
                            n_ledStripe["afterGlow"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
                            (uint8_t)CONFIG_TOPIC_LIGHT_UP,
                            n_ledStripe["lightUp"].as<uint32_t>()));

        const YAML::Node& segments = n_ledStripe["segments"];
        if (HasSequenceItems(segments)) {
          for (YAML::Node n_segment : segments) {
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_SEGMENT, index++,
                                (uint8_t)CONFIG_TOPIC_PORT,
                                n_ledStripe["port"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_SEGMENT, index++,
                                (uint8_t)CONFIG_TOPIC_NUMBER,
                                n_segment["number"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                n_ledStripe["board"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_LED_SEGMENT, index++,
                (uint8_t)CONFIG_TOPIC_FROM, n_segment["from"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                n_ledStripe["board"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_LED_SEGMENT, index++,
                (uint8_t)CONFIG_TOPIC_TO, n_segment["to"].as<uint32_t>()));

            if (AbortConfigurationEarly()) {
              return false;
            }
          }
        }

        const YAML::Node& led_effects = n_ledStripe["effects"];
        if (HasSequenceItems(led_effects)) {
          for (YAML::Node n_led_effect : led_effects) {
            index = 0;
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_PORT,
                                n_ledStripe["port"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_LED_SEGMENT,
                                n_led_effect["segment"].as<uint32_t>()));
            const std::string effectContext =
                ConfigItemContext(n_led_effect, "ledStripes.effects");
            const std::array<uint32_t, 3> colors =
                BuildLedEffectColors(n_led_effect, effectContext);
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_COLOR, colors[0]));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_COLOR_2, colors[1]));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_COLOR_3, colors[2]));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_DURATION,
                                n_led_effect["duration"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_EFFECT,
                                ResolveLedEffectMode(n_led_effect["effect"])));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_REVERSE,
                                n_led_effect["reverse"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_SPEED,
                                n_led_effect["speed"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_MODE,
                                n_led_effect["mode"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_PRIORITY,
                                n_led_effect["priority"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                n_ledStripe["board"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                (uint8_t)CONFIG_TOPIC_OPTIONS,
                BuildWs2812FxOptions(n_led_effect)));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_REPEAT,
                                n_led_effect["repeat"].as<int16_t>() == -1
                                    ? 255
                                    : (n_led_effect["repeat"].as<int16_t>() == -2
                                           ? 254
                                           : n_led_effect["repeat"]
                                                 .as<uint32_t>())));

            SendNamedEffectTriggerConfig(
                m_pRS485Comm, n_led_effect, CONFIG_TOPIC_LED_EFFECT,
                n_ledStripe["board"].as<uint8_t>(),
                n_ledStripe["port"].as<uint32_t>());
            SendSimpleEffectTriggerConfig(
                m_pRS485Comm, n_led_effect, CONFIG_TOPIC_LED_EFFECT,
                n_ledStripe["board"].as<uint8_t>(),
                n_ledStripe["port"].as<uint32_t>(),
                ConfigItemContext(n_led_effect, "ledStripes.effects"));

            if (AbortConfigurationEarly()) {
              return false;
            }
          }
        }

        SendLedConfigBlock(n_ledStripe["lamps"], LED_TYPE_LAMP,
                           n_ledStripe["board"].as<uint8_t>(),
                           n_ledStripe["port"].as<uint32_t>());
        SendLedConfigBlock(n_ledStripe["flashers"], LED_TYPE_FLASHER,
                           n_ledStripe["board"].as<uint8_t>(),
                           n_ledStripe["port"].as<uint32_t>());
        SendLedConfigBlock(n_ledStripe["gi"], LED_TYPE_GI,
                           n_ledStripe["board"].as<uint8_t>(),
                           n_ledStripe["port"].as<uint32_t>());

        if (AbortConfigurationEarly()) {
          return false;
        }
      }
    }

    if (AbortConfigurationEarly()) {
      return false;
    }

    if (m_pRS485Comm->HadConfigurationFailure()) {
      return false;
    }

    m_pRS485Comm->FinalizeConfiguredBoardPresence();
    m_pRS485Comm->SetActiveSwitchBoards(switchBoards);

    // Configure token-ring handoff across the full logical switch-board
    // order, including virtualized boards. The host synthesizes replies for
    // virtualized boards when the chain reaches their token.
    for (size_t i = 0; i < switchBoards.size(); ++i) {
      const uint8_t current = switchBoards[i];
      if (!m_pRS485Comm->IsBoardPresent(current)) {
        continue;
      }

      const uint8_t next = (i + 1 < switchBoards.size())
                               ? switchBoards[i + 1]
                               : ppuc::v2::kNoBoard;
      m_pRS485Comm->SendConfigEvent(new ConfigEvent(
          current, (uint8_t)CONFIG_TOPIC_SWITCH_CHAIN, 0,
          (uint8_t)CONFIG_TOPIC_NEXT_BOARD, next));
      m_pRS485Comm->SendConfigEvent(new ConfigEvent(
          current, (uint8_t)CONFIG_TOPIC_SWITCH_CHAIN, 1,
          (uint8_t)CONFIG_TOPIC_SWITCH_REPLY_DELAY_US, m_switchReplyDelayUs));
    }

    if (AbortConfigurationEarly()) {
      return false;
    }

    if (m_pRS485Comm->HadConfigurationFailure()) {
      return false;
    }

    // Start the V2 runtime only after all board-local config was applied.
    if (!m_pRS485Comm->SendSetupFrame()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!m_pRS485Comm->SendMappingFrames()) {
      return false;
    }

    // Wait before continuing.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Turn on the GI for non WPC platforms.
    if (PLATFORM_WPC != m_platform) {
      SetGIState(/* string */ 1, /* full brightness */ 8);
    }

    m_pRS485Comm->Run();
    return true;
  };

  bool startupAttemptHadException = false;
  auto runStartupAttempt = [&startupAttempt,
                            &startupAttemptHadException]() -> bool {
    try {
      return startupAttempt();
    } catch (const YAML::Exception& e) {
      startupAttemptHadException = true;
      printf("PPUC: invalid YAML configuration at %s: %s\n",
             FormatYamlLocation(e.mark).c_str(), e.what());
      return false;
    } catch (const std::exception& e) {
      startupAttemptHadException = true;
      printf("PPUC: %s\n", e.what());
      return false;
    }
  };

  if (m_forceHardReset) {
    printf("PPUC: starting board configuration using forced hard reset.\n");
    if (!m_pRS485Comm->ResetBoards()) {
      printf("PPUC: forced hard reset could not be started; startup aborted.\n");
      return false;
    }
    if (runStartupAttempt()) {
      return true;
    }
    if (startupAttemptHadException) {
      return false;
    }
    const std::vector<uint8_t> missingBoards =
        m_pRS485Comm->GetMissingConfiguredBoards();
    printf("PPUC: forced hard reset startup still failed; startup aborted.\n");
    if (missingBoards.empty()) {
      printf(
          "PPUC: one or more boards still did not acknowledge configuration.\n");
    } else {
      printf("PPUC: boards without config ACK:");
      for (const uint8_t board : missingBoards) {
        printf(" %u", board);
      }
      printf("\n");
    }
    printf(
        "PPUC: press the reset button on these boards or power cycle the machine.\n");
    printf(
        "PPUC: if this is intentional, start again with --skip-boards for the missing boards.\n");
    return false;
  }

  printf("PPUC: starting board configuration using soft restart.\n");
  if (!m_pRS485Comm->RestartBoards()) {
    printf("PPUC: soft restart could not be started; startup aborted.\n");
    return false;
  }
  if (runStartupAttempt()) {
    return true;
  }
  if (startupAttemptHadException) {
    return false;
  }

  const std::vector<uint8_t> missingBoards =
      m_pRS485Comm->GetMissingConfiguredBoards();
  printf("PPUC: soft restart startup failed; startup aborted.\n");
  if (missingBoards.empty()) {
    printf("PPUC: one or more boards still did not acknowledge configuration.\n");
  } else {
    printf("PPUC: boards without config ACK:");
    for (const uint8_t board : missingBoards) {
      printf(" %u", board);
    }
    printf("\n");
  }
  printf("PPUC: press the reset button on these boards or power cycle the machine.\n");
  printf("PPUC: if you want to force a board reboot first, start again with --hard-reset.\n");
  printf("PPUC: if this is intentional, start again with --skip-boards for the missing boards.\n");
  return false;
}

void PPUC::SetSolenoidState(int number, int state) {
  uint16_t solNo = number;
  uint8_t solState = state == 0 ? 0 : 1;
  m_pRS485Comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, solNo, solState));
}

void PPUC::SetLampState(int number, int state) {
  uint16_t lampNo = number;
  uint8_t lampState = state == 0 ? 0 : 1;
  m_pRS485Comm->QueueEvent(new Event(EVENT_SOURCE_LIGHT, lampNo, lampState));
}

void PPUC::SetGIState(int string, int brightness) {
  if (string < 1 || string > ppuc::v2::kGiStrings) {
    return;
  }

  uint8_t giBrightness = 0;
  if (brightness > 0) {
    giBrightness = static_cast<uint8_t>(brightness);
  }
  m_pRS485Comm->QueueEvent(new Event(
      EVENT_SOURCE_GI, static_cast<uint16_t>(string),
      ppuc::v2::ClampGiLevel(giBrightness)));
}

void PPUC::SetSwitchState(int number, int state) {
  m_pRS485Comm->SetVirtualSwitchState(static_cast<uint16_t>(number),
                                      state == 0 ? 0 : 1);
}

void PPUC::SetSwitchRefreshIdleMs(uint32_t idleMs) {
  m_switchRefreshIdleMs = idleMs;
  m_pRS485Comm->SetSwitchRefreshIdleMs(idleMs);
}

void PPUC::TriggerEvent(uint8_t source, int number, int value) {
  m_pRS485Comm->QueueEvent(new Event(source, static_cast<uint16_t>(number),
                                     static_cast<uint8_t>(value)));
}

bool PPUC::IsSwitchVirtualized(int number) {
  return m_pRS485Comm->IsSwitchVirtualized(static_cast<uint16_t>(number));
}

bool PPUC::IsBoardVirtualized(uint8_t board) {
  return m_pRS485Comm->IsBoardVirtualized(board);
}

PPUCSwitchState* PPUC::GetNextSwitchState() {
  return m_pRS485Comm->GetNextSwitchState();
}

uint32_t PPUC::GetCleanSwitchReplyChainCount() {
  return m_pRS485Comm->GetCleanSwitchReplyChainCount();
}

void PPUC::StartUpdates() {
  if (PLATFORM_WPC != m_platform) {
    // Older systems such as System 6 do not provide useful GI updates through
    // PinMAME, so reassert the single default GI string when runtime output
    // starts.
    SetGIState(/* string */ 1, /* full brightness */ 8);
  }
  m_pRS485Comm->QueueEvent(new Event(EVENT_RUN, 1, 1));
}

void PPUC::StopUpdates() {
  m_pRS485Comm->QueueEvent(new Event(EVENT_RUN, 1, 0));
}

std::vector<PPUCCoil> PPUC::GetCoils() {
  std::sort(
      m_coils.begin(), m_coils.end(),
      [](const PPUCCoil& a, const PPUCCoil& b) { return a.number < b.number; });

  return m_coils;
}

std::vector<PPUCLamp> PPUC::GetLamps() {
  std::sort(
      m_lamps.begin(), m_lamps.end(),
      [](const PPUCLamp& a, const PPUCLamp& b) { return a.number < b.number; });

  return m_lamps;
}

std::vector<PPUCSwitch> PPUC::GetSwitches() {
  std::sort(m_switches.begin(), m_switches.end(),
            [](const PPUCSwitch& a, const PPUCSwitch& b) {
              return a.number < b.number;
            });

  return m_switches;
}

std::unordered_map<std::string, std::vector<uint16_t>> PPUC::GetSwitchGroups() {
  return m_switchGroups;
}

const std::vector<PPUCCoilGiMapping>& PPUC::GetCoilGiMappings() const {
  return m_coilGiMappings;
}
