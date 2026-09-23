#ifndef ANA_ROOT_TREE_WRITER_H
#define ANA_ROOT_TREE_WRITER_H

#include "DecodedEvent.h"

#include <cstdint>

class TFile;
class TTree;

namespace ana {

class RootTreeWriter {
 public:
  RootTreeWriter() = default;
  ~RootTreeWriter() = default;

  RootTreeWriter(const RootTreeWriter&) = delete;
  RootTreeWriter& operator=(const RootTreeWriter&) = delete;

  bool BeginRun(TFile* output_file);
  void Fill(const DecodedEvent& event);
  void EndRun();

  std::int64_t Entries() const;

 private:
  TFile* output_file_ = nullptr;
  TTree* tree_ = nullptr;
  long long event_ = kInvalidCounter;
  long long vme_counter_ = kInvalidCounter;
  long long easiroc_counter_ = kInvalidCounter;
  long long v792_counter_ = kInvalidCounter;
  long long v775_counter_ = kInvalidCounter;
  long long v1190_counter_ = kInvalidCounter;
  long long v1720_counter_ = kInvalidCounter;
  long long easiroc_raw_counter_ = kInvalidCounter;
  DecodedEvent buffer_;
};

}  // namespace ana

#endif
