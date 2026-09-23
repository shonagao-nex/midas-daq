#ifndef ANA_V792_DECODER_H
#define ANA_V792_DECODER_H

class TMEvent;

namespace ana {
struct DecodedEvent;
class V792Decoder {
 public:
  bool Decode(TMEvent& event, DecodedEvent& output) const;
};
}  // namespace ana

#endif

