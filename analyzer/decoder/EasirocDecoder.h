#ifndef ANA_EASIROC_DECODER_H
#define ANA_EASIROC_DECODER_H

class TMEvent;

namespace ana {
struct DecodedEvent;
class EasirocDecoder {
 public:
  bool Decode(TMEvent& event, DecodedEvent& output) const;
};
}  // namespace ana

#endif

