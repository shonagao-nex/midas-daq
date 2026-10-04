#include "TreeReader.h"

#include "TBranch.h"
#include "TClass.h"
#include "TFile.h"
#include "TLeaf.h"
#include "TTree.h"

#include <cstdio>
#include <type_traits>

namespace {
  bool CheckLeaf(TTree* tree, const char* name, EDataType type, int length) {
    TBranch* branch = tree->GetBranch(name);
    if (!branch) {
      std::fprintf(stderr, "ERROR: missing ROOT branch '%s'\n", name);
      return false;
    }
    TClass* actual_class = nullptr;
    EDataType actual_type = kOther_t;
    TLeaf* leaf = branch->GetLeaf(name);
    if (branch->GetExpectedType(actual_class, actual_type) < 0 || actual_class || actual_type != type || !leaf || leaf->GetLeafCount() || leaf->GetLenStatic() != length) {
      std::fprintf(stderr, "ERROR: ROOT branch '%s' has unexpected type or length\n", name);
      return false;
    }
    return true;
  }
  
  bool CheckHits(TTree* tree, const char* name) {
    TBranch* branch = tree->GetBranch(name);
    if (!branch) {
      std::fprintf(stderr, "ERROR: missing ROOT branch '%s'\n", name);
      return false;
    }
    TClass* actual_class = nullptr;
    EDataType actual_type = kOther_t;
    TClass* expected = TClass::GetClass(typeid(TreeReader::Hits));
    if (!expected || branch->GetExpectedType(actual_class, actual_type) < 0 || actual_class != expected) {
      std::fprintf(stderr, "ERROR: ROOT branch '%s' must be vector<vector<int>>\n", name);
      return false;
    }
    return true;
  }
}  // namespace

TreeReader::TreeReader(const std::string& path) {
  static_assert(sizeof(std::int32_t) == sizeof(Int_t));
  static_assert(sizeof(std::int64_t) == sizeof(Long64_t));
  file_.reset(TFile::Open(path.c_str(), "READ"));
  if (!file_ || file_->IsZombie()) {
    std::fprintf(stderr, "ERROR: cannot open ROOT file: %s\n", path.c_str());
    return;
  }
  tree_ = file_->Get<TTree>("tree");
  if (!tree_) {
    std::fprintf(stderr, "ERROR: ROOT file '%s' has no TTree named 'tree'\n", path.c_str());
    return;
  }

#define BIND_LEAF(name, type, length, address) \
  if (!CheckLeaf(tree_, #name, type, length) || tree_->SetBranchAddress(#name, address) < 0) { \
    std::fprintf(stderr, "ERROR: cannot bind ROOT branch '%s'\n", #name); \
    return; \
  }
#define BIND_HITS(name) \
  if (!CheckHits(tree_, #name) || tree_->SetBranchAddress(#name, &name##_ptr_) < 0) { \
    std::fprintf(stderr, "ERROR: cannot bind ROOT branch '%s'\n", #name); \
    return; \
  }

  BIND_LEAF(event              , kLong64_t,  1, &event_              );
  BIND_LEAF(vme_counter        , kLong64_t,  1, &vme_counter_        );
  BIND_LEAF(easiroc_counter    , kLong64_t,  1, &easiroc_counter_    );
  BIND_LEAF(v792_counter       , kLong64_t,  1, &v792_counter_       );
  BIND_LEAF(v775_counter       , kLong64_t,  1, &v775_counter_       );
  BIND_LEAF(v1190_counter      , kLong64_t,  1, &v1190_counter_      );
  BIND_LEAF(v1720_counter      , kLong64_t,  1, &v1720_counter_      );
  BIND_LEAF(easiroc_raw_counter, kLong64_t,  1, &easiroc_raw_counter_);
  BIND_LEAF(qdc0               , kInt_t   , 32, qdc0_.data()         );
  BIND_LEAF(tdc0               , kInt_t   , 32, tdc0_.data()         );
  BIND_HITS(tle0);
  BIND_HITS(ttr0);
  BIND_HITS(fadc0);
  BIND_LEAF(eadc0              , kInt_t   , 64, eadc0_.data()        );
  BIND_HITS(etle0);
  BIND_HITS(ettr0);
#undef BIND_LEAF
#undef BIND_HITS
  valid_ = true;
}

TreeReader::~TreeReader() = default;

std::int64_t TreeReader::GetEntries() const {
  return valid_ ? tree_->GetEntries() : 0;
}

bool TreeReader::GetEntry(std::int64_t entry) {
  if (!valid_ || entry < 0 || entry >= GetEntries()) {
    std::fprintf(stderr, "ERROR: ROOT entry %lld is outside the tree\n",
                 static_cast<long long>(entry));
    return false;
  }
  if (tree_->GetEntry(entry) <= 0) {
    std::fprintf(stderr, "ERROR: failed to read ROOT entry %lld\n",
                 static_cast<long long>(entry));
    return false;
  }
  return true;
}
