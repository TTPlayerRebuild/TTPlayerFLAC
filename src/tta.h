#pragma once
#include "io.h"
namespace ttp::flac {
class TtaDecoder {
    Input *input_{};
    WAVEFORMATEX format_{};
    uint64_t total_{}, position_{};
    DWORD frame_length_{}, frame_index_{}, frame_count_{};
    std::vector<uint64_t> offsets_;
    bool table_valid_{};
    Bytes bytes_; // Allocated only by TTA Open; FLAC readers need no TTA cache.
    size_t begin_{}, end_{};
    uint64_t bits_{};
    unsigned available_{};
    uint32_t crc_{};
    size_t frame_bytes_{};
    BYTE byte(bool checksum = true);
    uint32_t bits(unsigned n);
    uint32_t unary();
    void reset_bits();

  public:
    void open(Input &input);
    bool read(Bytes &pcm);
    void seek_sample(uint64_t sample);
    bool seekable() const { return table_valid_ && input_->seekable; }
    uint64_t total() const { return total_; }
    const WAVEFORMATEX &format() const { return format_; }
};
} // namespace ttp::flac
