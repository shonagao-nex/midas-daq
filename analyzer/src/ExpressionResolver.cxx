#include "ExpressionResolver.h"

#include <algorithm>
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
  const char* tdc_label = nullptr;
};

constexpr std::array<IndexedVariable, 8> kIndexedVariables{{
    {"qdc0", VariableKind::kQdc0, 32, 1},
    {"tdc0", VariableKind::kTdc0, 32, 1, "V775"},
    {"eadc0", VariableKind::kEadc0, 64, 1},
    {"tle0", VariableKind::kTle0, 128, 2, "V1190 LE"},
    {"ttr0", VariableKind::kTtr0, 128, 2, "V1190 TE"},
    {"etle0", VariableKind::kEtle0, 64, 2, "EASIROC LE"},
    {"ettr0", VariableKind::kEttr0, 64, 2, "EASIROC TE"},
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

std::string_view Trim(std::string_view text) {
  constexpr std::string_view whitespace = " \t\r\n";
  const auto first = text.find_first_not_of(whitespace);
  if (first == std::string_view::npos) return {};
  return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

constexpr std::string_view kHitChannelsFunction = "hit_channels";

ParsedExpression ParseHitChannels(std::string_view args) {
  args = Trim(args);
  if (args.size() < 2 || args.front() != '(' || args.back() != ')')
    return {VariableKind::kInvalid, 0, 0, "expected hit_channels(tdc0[ch], ...)"};
  args.remove_prefix(1);
  args.remove_suffix(1);

  ParsedExpression result;
  result.kind = VariableKind::kHitChannels;
  for (;;) {
    const auto comma = args.find(',');
    const auto argument = Trim(args.substr(0, comma));
    auto position = argument.find('[');
    const auto name = argument.substr(0, position);
    const auto variable = std::find_if(kIndexedVariables.begin(), kIndexedVariables.end(),
        [name](const IndexedVariable& v) { return v.name == name && v.tdc_label; });
    std::size_t channel = 0;
    if (variable == kIndexedVariables.end() || !ParseIndex(argument, position, channel) ||
        position != argument.size() || channel >= variable->channels) {
      return {VariableKind::kInvalid, 0, 0,
              "invalid TDC channel in hit_channels: '" + std::string(argument) + "'"};
    }
    if (std::any_of(result.hit_channels.begin(), result.hit_channels.end(),
                    [&](const TdcChannel& c) {
                      return c.kind == variable->kind && c.channel == channel;
                    })) {
      return {VariableKind::kInvalid, 0, 0,
              "duplicate TDC channel in hit_channels: '" + std::string(argument) + "'"};
    }
    result.hit_channels.push_back({variable->kind, channel,
        std::string(variable->tdc_label) + " ch" + std::to_string(channel)});
    if (comma == std::string_view::npos) break;
    args.remove_prefix(comma + 1);
  }
  const auto first_kind = result.hit_channels.front().kind;
  if (std::all_of(result.hit_channels.begin(), result.hit_channels.end(),
                  [first_kind](const TdcChannel& c) { return c.kind == first_kind; })) {
    for (auto& channel : result.hit_channels)
      channel.label = "ch" + std::to_string(channel.channel);
  }
  return result;
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

bool HasPositiveHit(const TdcChannel& channel, const DecodedEvent& event) {
  const auto index = channel.channel;
  const auto any_positive = [index](const auto& channels) {
    if (index >= channels.size()) return false;
    const auto& hits = channels[index];
    return std::any_of(hits.begin(), hits.end(), [](auto hit) { return hit > 0; });
  };
  switch (channel.kind) {
    case VariableKind::kTdc0:
      return index < event.v775.tdc0.size() && event.v775.tdc0[index] > 0;
    case VariableKind::kTle0: return any_positive(event.v1190.tle0);
    case VariableKind::kTtr0: return any_positive(event.v1190.ttr0);
    case VariableKind::kEtle0: return any_positive(event.easiroc.etle0);
    case VariableKind::kEttr0: return any_positive(event.easiroc.ettr0);
    default: return false;
  }
}

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
  const auto trimmed = Trim(text);
  if (trimmed.substr(0, kHitChannelsFunction.size()) == kHitChannelsFunction)
    return ParseHitChannels(trimmed.substr(kHitChannelsFunction.size()));

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
    case VariableKind::kHitChannels:
    case VariableKind::kInvalid:
      return {};
  }
  return {};
}

ResolveResult ExpressionResolver::Resolve(const std::string& expression,
                                          const DecodedEvent& event) const {
  return Resolve(Parse(expression), event);
}

ParsedExpression ExpressionResolver::ParseWaveform(
    const std::string& expression) const {
  // Keep scalar fadc0[ch][sample] syntax and validation unchanged.
  auto parsed = Parse(expression + "[0]");
  if (parsed.kind != VariableKind::kFadc0)
    return {VariableKind::kInvalid, 0, 0, "waveform requires fadc0[ch] (ch 0..7)"};
  return parsed;
}

}  // namespace ana
