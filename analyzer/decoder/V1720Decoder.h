#ifndef ANA_V1720_DECODER_H
#define ANA_V1720_DECODER_H

class TMEvent;

namespace ana {
struct DecodedEvent;
class V1720Decoder {
 public:
  bool Decode(TMEvent& event, DecodedEvent& output) const;
};
}  // namespace ana

#endif

