#ifndef ANA_MACROS_TREE_READER_H
#define ANA_MACROS_TREE_READER_H

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class TFile;
class TTree;

// Reads the "tree" TTree written by RootTreeWriter. Data is current after GetEntry().
class TreeReader {
 public:
  using Hits = std::vector<std::vector<std::int32_t>>;

  explicit TreeReader(const std::string& path);
  ~TreeReader();
  TreeReader(const TreeReader&) = delete;
  TreeReader& operator=(const TreeReader&) = delete;

  bool IsValid() const { return valid_; }
  std::int64_t GetEntries() const;
  bool GetEntry(std::int64_t entry);

  std::int64_t event() const { return event_; }
  std::int64_t vme_counter() const { return vme_counter_; }
  std::int64_t easiroc_counter() const { return easiroc_counter_; }
  std::int64_t v792_counter() const { return v792_counter_; }
  std::int64_t v775_counter() const { return v775_counter_; }
  std::int64_t v1190_counter() const { return v1190_counter_; }
  std::int64_t v1720_counter() const { return v1720_counter_; }
  std::int64_t easiroc_raw_counter() const { return easiroc_raw_counter_; }
  const std::array<std::int32_t, 32>& qdc0() const { return qdc0_; }
  const std::array<std::int32_t, 32>& tdc0() const { return tdc0_; }
  const Hits& tle0() const { return tle0_; }
  const Hits& ttr0() const { return ttr0_; }
  const Hits& fadc0() const { return fadc0_; }
  const std::array<std::int32_t, 64>& eadc0() const { return eadc0_; }
  const Hits& etle0() const { return etle0_; }
  const Hits& ettr0() const { return ettr0_; }

 private:
  std::unique_ptr<TFile> file_;
  TTree* tree_ = nullptr;  // Owned by file_.
  bool valid_ = false;
  long long event_ = 0, vme_counter_ = 0, easiroc_counter_ = 0;
  long long v792_counter_ = 0, v775_counter_ = 0, v1190_counter_ = 0;
  long long v1720_counter_ = 0, easiroc_raw_counter_ = 0;
  std::array<std::int32_t, 32> qdc0_{}, tdc0_{};
  std::array<std::int32_t, 64> eadc0_{};
  Hits tle0_, ttr0_, fadc0_, etle0_, ettr0_;
  Hits* tle0_ptr_ = &tle0_;
  Hits* ttr0_ptr_ = &ttr0_;
  Hits* fadc0_ptr_ = &fadc0_;
  Hits* etle0_ptr_ = &etle0_;
  Hits* ettr0_ptr_ = &ettr0_;
};

#endif
