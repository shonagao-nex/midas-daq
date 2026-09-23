#include "ExpressionResolver.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const std::string& message) {
  if (condition) return true;
  std::fprintf(stderr, "FAIL: %s\n", message.c_str());
  return false;
}

}  // namespace

int main() {
  ana::DecodedEvent event;
  event.counters.event = 12;
  event.counters.vme = 13;
  event.counters.easiroc = 14;
  event.counters.v792 = 15;
  event.counters.v775 = 16;
  event.counters.v1190 = 17;
  event.counters.v1720 = 18;
  event.counters.nim_easiroc = 19;
  event.v792.qdc0[0] = 100;
  event.v792.qdc0[31] = 131;
  event.v775.tdc0[0] = 200;
  event.easiroc.eadc0[63] = 363;
  event.v1190.tle0[0].push_back(400);
  event.v1190.ttr0[127].push_back(500);
  event.v1720.fadc0[7].push_back(600);
  event.easiroc.etle0[63].push_back(700);
  event.easiroc.ettr0[63].push_back(800);

  const std::vector<std::string> valid{
      "event",          "vme_counter",    "easiroc_counter",
      "v792_counter",   "v775_counter",   "v1190_counter",
      "v1720_counter",  "easiroc_raw_counter",
      "qdc0[0]",        "qdc0[31]",       "tdc0[0]",
      "eadc0[63]",      "tle0[0][0]",     "ttr0[127][0]",
      "fadc0[7][0]",    "etle0[63][0]",  "ettr0[63][0]",
  };
  const std::vector<std::string> invalid{
      "qdc0[32]",    "tdc0[-1]",    "tle0[128][0]",
      "tle0[0][-1]", "fadc0[8][0]", "unknown",
      "qdc0",        "qdc0[]",
  };

  ana::ExpressionResolver resolver;
  bool okay = true;
  for (const auto& expression : valid) {
    const auto parsed = resolver.Parse(expression);
    okay &= Check(parsed.IsValid(), expression + " should parse");
    okay &= Check(resolver.Resolve(parsed, event).valid,
                  expression + " should resolve");
  }
  for (const auto& expression : invalid) {
    const auto parsed = resolver.Parse(expression);
    okay &= Check(!parsed.IsValid(), expression + " should be invalid");
    okay &= Check(!resolver.Resolve(parsed, event).valid,
                  expression + " should not resolve");
  }

  okay &= Check(!resolver.Resolve("qdc0[1]", event).valid,
                "-999 fixed data should not resolve");
  okay &= Check(!resolver.Resolve("tle0[0][1]", event).valid,
                "missing hit should not resolve");
  okay &= Check(!resolver.Resolve("fadc0[0][0]", event).valid,
                "missing sample should not resolve");

  if (!okay) return 1;
  std::printf("ExpressionResolver tests passed: %zu valid, %zu invalid\n",
              valid.size(), invalid.size());
  return 0;
}
