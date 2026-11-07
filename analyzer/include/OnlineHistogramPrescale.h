#ifndef ANA_ONLINE_HISTOGRAM_PRESCALE_H
#define ANA_ONLINE_HISTOGRAM_PRESCALE_H

#include "AnalyzerMode.h"

#include <cstddef>

class MVOdb;

namespace ana {

class OnlineHistogramPrescale {
 public:
  enum class Kind { kHistogram, kWaveform };

  explicit OnlineHistogramPrescale(Kind kind = Kind::kHistogram);

  // Create the selected key with its default when absent.
  static bool EnsureOdb(MVOdb* odb, Kind kind = Kind::kHistogram);

  void BeginRun(MVOdb* odb);
  void Poll(MVOdb* odb);

  // One-based count of events eligible for this kind of plot.
  bool ShouldFill(AnalyzerMode mode, std::size_t event_count) const;
  int EffectiveValue() const { return effective_value_; }

 private:
  void Read(MVOdb* odb, bool announce);
  void Apply(int requested, bool announce);

  Kind kind_;
  int effective_value_;
  bool warned_invalid_ = false;
  bool warned_read_error_ = false;
};

}  // namespace ana

#endif
