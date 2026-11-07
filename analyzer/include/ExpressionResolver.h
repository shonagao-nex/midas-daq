#ifndef ANA_EXPRESSION_RESOLVER_H
#define ANA_EXPRESSION_RESOLVER_H

#include "DecodedEvent.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ana {

enum class VariableKind {
  kInvalid,
  kEvent,
  kVmeCounter,
  kEasirocCounter,
  kV792Counter,
  kV775Counter,
  kV1190Counter,
  kV1720Counter,
  kEasirocRawCounter,
  kQdc0,
  kTdc0,
  kEadc0,
  kTle0,
  kTtr0,
  kEtle0,
  kEttr0,
  kFadc0,
  kHitChannels,
};

struct TdcChannel {
  VariableKind kind;
  std::size_t channel;
  std::string label;
};

bool HasPositiveHit(const TdcChannel& channel, const DecodedEvent& event);

struct ParsedExpression {
  VariableKind kind = VariableKind::kInvalid;
  std::size_t channel = 0;
  std::size_t subindex = 0;
  std::string error;
  std::vector<TdcChannel> hit_channels{};

  bool IsValid() const { return kind != VariableKind::kInvalid; }
};

struct ResolveResult {
  bool valid = false;
  double value = 0.0;
};

class ExpressionResolver {
 public:
  ParsedExpression Parse(const std::string& expression) const;
  ParsedExpression ParseWaveform(const std::string& expression) const;
  ResolveResult Resolve(const ParsedExpression& expression,
                        const DecodedEvent& event) const;
  ResolveResult Resolve(const std::string& expression,
                        const DecodedEvent& event) const;
};

}  // namespace ana

#endif
