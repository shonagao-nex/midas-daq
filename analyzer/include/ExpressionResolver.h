#ifndef ANA_EXPRESSION_RESOLVER_H
#define ANA_EXPRESSION_RESOLVER_H

#include "DecodedEvent.h"

#include <cstddef>
#include <string>

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
};

struct ParsedExpression {
  VariableKind kind = VariableKind::kInvalid;
  std::size_t channel = 0;
  std::size_t subindex = 0;
  std::string error;

  bool IsValid() const { return kind != VariableKind::kInvalid; }
};

struct ResolveResult {
  bool valid = false;
  double value = 0.0;
};

class ExpressionResolver {
 public:
  ParsedExpression Parse(const std::string& expression) const;
  ResolveResult Resolve(const ParsedExpression& expression,
                        const DecodedEvent& event) const;
  ResolveResult Resolve(const std::string& expression,
                        const DecodedEvent& event) const;
};

}  // namespace ana

#endif
