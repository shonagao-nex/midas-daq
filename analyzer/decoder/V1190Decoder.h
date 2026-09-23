#ifndef ANA_V1190_DECODER_H
#define ANA_V1190_DECODER_H

class TMEvent;

namespace ana {
struct DecodedEvent;
class V1190Decoder {
 public:
  bool Decode(TMEvent& event, DecodedEvent& output) const;
};
}  // namespace ana

#endif

