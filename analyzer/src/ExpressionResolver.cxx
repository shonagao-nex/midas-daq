#include "ExpressionResolver.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace ana {
namespace {

struct IndexedVariable {
  std::string_view name;
  VariableKind kind;
  std::size_t channels;
  int dimensions;
};

constexpr std::array<IndexedVariable, 8> kIndexedVariables{{
    {"qdc0", VariableKind::kQdc0, 32, 1},
    {"tdc0", VariableKind::kTdc0, 32, 1},
    {"eadc0", VariableKind::kEadc0, 64, 1},
    {"tle0", VariableKind::kTle0, 128, 2},
    {"ttr0", VariableKind::kTtr0, 128, 2},
    {"etle0", VariableKind::kEtle0, 64, 2},
    {"ettr0", VariableKind::kEttr0, 64, 2},
    {"fadc0", VariableKind::kFadc0, 8, 2},
}};

bool ParseIndex(std::string_view text, std::size_t& position,
                std::size_t& value) {
  if (position >= text.size() || text[position] != '[') return false;
  ++position;
  if (position >= text.size() ||
      !std::isdigit(static_cast<unsigned char>(text[position])))
    return false;

  std::size_t result = 0;
  while (position < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[position]))) {
    const unsigned digit = static_cast<unsigned>(text[position] - '0');
    if (result > (std::numeric_limits<std::size_t>::max() - digit) / 10)
      return false;
    result = result * 10 + digit;
    ++position;
  }
  if (position >= text.size() || text[position] != ']') return false;
  ++position;
  value = result;
  return true;
}

ResolveResult Value(std::int64_t value) {
  if (value == kInvalidCounter) return {};
  return {true, static_cast<double>(value)};
}

ResolveResult Value(std::int32_t value) {
  if (value == kInvalidValue) return {};
  return {true, static_cast<double>(value)};
}

ResolveResult VectorValue(const std::vector<std::vector<std::int32_t>>& values,
                          std::size_t channel, std::size_t subindex) {
  if (channel >= values.size() || subindex >= values[channel].size()) return {};
  return Value(values[channel][subindex]);
}

}  // namespace

ParsedExpression ExpressionResolver::Parse(const std::string& expression) const {
  static constexpr std::array<std::pair<std::string_view, VariableKind>, 8>
      scalars{{
          {"event", VariableKind::kEvent},
          {"vme_counter", VariableKind::kVmeCounter},
          {"easiroc_counter", VariableKind::kEasirocCounter},
          {"v792_counter", VariableKind::kV792Counter},
          {"v775_counter", VariableKind::kV775Counter},
          {"v1190_counter", VariableKind::kV1190Counter},
          {"v1720_counter", VariableKind::kV1720Counter},
          {"easiroc_raw_counter", VariableKind::kEasirocRawCounter},
      }};

  for (const auto& scalar : scalars) {
    if (expression == scalar.first)
      return {scalar.second, 0, 0, {}};
  }

  const std::string_view text(expression);
  for (const auto& variable : kIndexedVariables) {
    if (text.size() <= variable.name.size() ||
        text.substr(0, variable.name.size()) != variable.name)
      continue;

    std::size_t position = variable.name.size();
    std::size_t channel = 0;
    std::size_t subindex = 0;
    if (!ParseIndex(text, position, channel))
      return {VariableKind::kInvalid, 0, 0, "invalid index syntax"};
    if (variable.dimensions == 2 && !ParseIndex(text, position, subindex))
      return {VariableKind::kInvalid, 0, 0, "second index is required"};
    if (position != text.size())
      return {VariableKind::kInvalid, 0, 0, "unexpected trailing text"};
    if (channel >= variable.channels)
      return {VariableKind::kInvalid, 0, 0, "channel is out of range"};
    return {variable.kind, channel, subindex, {}};
  }
  return {VariableKind::kInvalid, 0, 0, "unknown variable"};
}

ResolveResult ExpressionResolver::Resolve(
    const ParsedExpression& expression, const DecodedEvent& event) const {
  if (!expression.IsValid()) return {};
  switch (expression.kind) {
    case VariableKind::kEvent:
      return Value(event.counters.event);
    case VariableKind::kVmeCounter:
      return Value(event.counters.vme);
    case VariableKind::kEasirocCounter:
      return Value(event.counters.easiroc);
    case VariableKind::kV792Counter:
      return Value(event.counters.v792);
    case VariableKind::kV775Counter:
      return Value(event.counters.v775);
    case VariableKind::kV1190Counter:
      return Value(event.counters.v1190);
    case VariableKind::kV1720Counter:
      return Value(event.counters.v1720);
    case VariableKind::kEasirocRawCounter:
      return Value(event.counters.nim_easiroc);
    case VariableKind::kQdc0:
      if (expression.channel >= event.v792.qdc0.size()) return {};
      return Value(event.v792.qdc0[expression.channel]);
    case VariableKind::kTdc0:
      if (expression.channel >= event.v775.tdc0.size()) return {};
      return Value(event.v775.tdc0[expression.channel]);
    case VariableKind::kEadc0:
      if (expression.channel >= event.easiroc.eadc0.size()) return {};
      return Value(event.easiroc.eadc0[expression.channel]);
    case VariableKind::kTle0:
      return VectorValue(event.v1190.tle0, expression.channel,
                         expression.subindex);
    case VariableKind::kTtr0:
      return VectorValue(event.v1190.ttr0, expression.channel,
                         expression.subindex);
    case VariableKind::kEtle0:
      return VectorValue(event.easiroc.etle0, expression.channel,
                         expression.subindex);
    case VariableKind::kEttr0:
      return VectorValue(event.easiroc.ettr0, expression.channel,
                         expression.subindex);
    case VariableKind::kFadc0:
      return VectorValue(event.v1720.fadc0, expression.channel,
                         expression.subindex);
    case VariableKind::kInvalid:
      return {};
  }
  return {};
}

ResolveResult ExpressionResolver::Resolve(const std::string& expression,
                                          const DecodedEvent& event) const {
  return Resolve(Parse(expression), event);
}

}  // namespace ana
