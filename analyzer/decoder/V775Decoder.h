#ifndef ANA_V775_DECODER_H
#define ANA_V775_DECODER_H

class TMEvent;

namespace ana {
struct DecodedEvent;
class V775Decoder {
 public:
  bool Decode(TMEvent& event, DecodedEvent& output) const;
};
}  // namespace ana

#endif

