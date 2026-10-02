#ifndef ANA_ONLINE_HISTOGRAM_PRESCALE_H
#define ANA_ONLINE_HISTOGRAM_PRESCALE_H

#include "AnalyzerMode.h"

#include <cstddef>

class MVOdb;

namespace ana {

class OnlineHistogramPrescale {
 public:
  static constexpr const char* kOdbPath = "/Analyzer/OnlineHistogram/FillPrescale";

  // Called only after an online MIDAS connection exists. Existing values are
  // read without modification; a missing integer key is created with value 1.
  static bool EnsureOdb(MVOdb* odb);

  void BeginRun(MVOdb* odb);
  void Poll(MVOdb* odb);

  // decoded_event_count is the one-based count at ConsumeDecodedEvent.
  bool ShouldFill(AnalyzerMode mode, std::size_t decoded_event_count) const;
  int EffectiveValue() const { return effective_value_; }

 private:
  void Read(MVOdb* odb, bool announce);
  void Apply(int requested, bool announce);

  int effective_value_ = 1;
  bool warned_invalid_ = false;
  bool warned_read_error_ = false;
};

}  // namespace ana

#endif
