// TTA1 reconstruction: 60303C5A, 60304381, 60304494, 60304C38.
// Rice/filter behavior cross-checked with the BSD TTA reference by
// Alexander Djourik and Pavel Zhilin, Copyright (c) 2004 True Audio Software.
// The full BSD notice is retained in docs/licenses/TTA-BSD.txt.
#include "tta.h"
#include <array>
namespace ttp::flac {
namespace {
const std::array<uint32_t, 256> &crc_table() {
    static constexpr auto table = []() {
        std::array<uint32_t, 256> t{};
        for (unsigned i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
            t[i] = c;
        }
        return t;
    }();
    return table;
}
uint32_t crc(const BYTE *p, size_t size) {
    uint32_t c = ~0u;
    const auto &t = crc_table();
    while (size--)
        c = t[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}
// Unsigned arithmetic deliberately preserves the original x86 modulo-32-bit
// filter state; signed overflow would be undefined with modern optimization.
struct Channel {
    uint32_t qm[8]{}, dx[9]{}, dl[9]{}, last{}, sum0{16384}, sum1{16384};
    unsigned k0{10}, k1{10};
    int32_t error{};
    uint32_t filter(uint32_t value, unsigned shift, bool mix) {
        uint32_t sum = 1u << (shift - 1);
        for (unsigned i = 0; i < 8; ++i) {
            if (error < 0)
                qm[i] -= dx[i];
            else if (error > 0)
                qm[i] += dx[i];
            sum += qm[i] * dl[i];
        }
        dx[8] = uint32_t(int32_t((dl[7] & 0xcfffffffu) | 0x40000000u) >> 28);
        dx[7] = uint32_t(int32_t((dl[6] & 0xdfffffffu) | 0x40000000u) >> 29);
        dx[6] = uint32_t(int32_t((dl[5] & 0xdfffffffu) | 0x40000000u) >> 29);
        dx[5] = uint32_t(int32_t(dl[4] | 0x40000000u) >> 30);
        error = int32_t(value);
        value += uint32_t(int32_t(sum) >> shift);
        dl[8] = value;
        if (mix) {
            dl[7] = dl[8] - dl[7];
            dl[6] = dl[7] - dl[6];
            dl[5] = dl[6] - dl[5];
        }
        for (unsigned i = 0; i < 8; ++i) {
            dl[i] = dl[i + 1];
            dx[i] = dx[i + 1];
        }
        return value;
    }
};
uint32_t threshold(unsigned k) { return k < 28 ? 1u << (k + 4) : 0x80000000u; }
void adapt(uint32_t value, uint32_t &sum, unsigned &k) {
    sum += value - (sum >> 4);
    if (k && sum < threshold(k))
        --k;
    else if (sum > threshold(k + 1)) {
        require(k < 31);
        ++k;
    }
}
uint32_t reverse16(uint32_t x) {
    x = ((x & 0x5555) << 1) | ((x >> 1) & 0x5555);
    x = ((x & 0x3333) << 2) | ((x >> 2) & 0x3333);
    x = ((x & 0x0f0f) << 4) | ((x >> 4) & 0x0f0f);
    return ((x & 255) << 8) | ((x >> 8) & 255);
}
} // namespace
void TtaDecoder::reset_bits() {
    begin_ = end_ = 0;
    bits_ = 0;
    available_ = 0;
    crc_ = ~0u;
    frame_bytes_ = 0;
}
BYTE TtaDecoder::byte(bool checksum) {
    if (begin_ == end_) {
        end_ = input_->read(bytes_.data(), DWORD(bytes_.size()));
        begin_ = 0;
        require(end_, STG_E_READFAULT);
    }
    const BYTE b = bytes_[begin_++];
    if (checksum) {
        require(++frame_bytes_ <= 64 * 1024 * 1024);
        crc_ = crc_table()[(crc_ ^ b) & 255] ^ (crc_ >> 8);
    }
    return b;
}
uint32_t TtaDecoder::bits(unsigned n) {
    require(n <= 32);
    while (available_ < n) {
        bits_ |= uint64_t(byte()) << available_;
        available_ += 8;
    }
    uint32_t v = uint32_t(bits_) & (n == 32 ? ~0u : ((1u << n) - 1));
    bits_ >>= n;
    available_ -= n;
    return v;
}
uint32_t TtaDecoder::unary() {
    uint32_t n{};
    while (bits(1)) {
        require(n < (1u << 24));
        ++n;
    }
    return n;
}
void TtaDecoder::open(Input &input) {
    bytes_.resize(65536);
    input_ = &input;
    input.at(input.base);
    BYTE h[22];
    input.exact(h, 22);
    require(!memcmp(h, "TTA1", 4) && crc(h, 18) == le32(h + 18));
    format_.wFormatTag = le16(h + 4);
    format_.nChannels = le16(h + 6);
    format_.wBitsPerSample = le16(h + 8);
    format_.nSamplesPerSec = le32(h + 10);
    total_ = le32(h + 14);
    require((format_.wFormatTag == 1 || format_.wFormatTag == 3) && format_.nChannels >= 1 &&
            format_.nChannels <= 8 && format_.nSamplesPerSec >= 1 &&
            format_.nSamplesPerSec <= 1048575);
    require(format_.wBitsPerSample == 8 || format_.wBitsPerSample == 16 ||
            format_.wBitsPerSample == 24 || format_.wBitsPerSample == 32);
    if (format_.wFormatTag == 3)
        require(format_.wBitsPerSample == 32);
    format_.nBlockAlign = WORD(format_.nChannels * (format_.wBitsPerSample / 8));
    format_.nAvgBytesPerSec = format_.nSamplesPerSec * format_.nBlockAlign;
    frame_length_ = DWORD(uint64_t(format_.nSamplesPerSec) * 256 / 245);
    require(frame_length_ && uint64_t(frame_length_) * format_.nBlockAlign <= 64 * 1024 * 1024);
    frame_count_ = DWORD((total_ + frame_length_ - 1) / frame_length_);
    require(frame_count_ <= 4 * 1024 * 1024);
    Bytes table((size_t(frame_count_) + 1) * 4);
    input.exact(table.data(), DWORD(table.size()));
    table_valid_ = crc(table.data(), table.size() - 4) == le32(table.data() + table.size() - 4);
    uint64_t offset = input.base + 22 + table.size();
    offsets_.reserve(frame_count_ + 1);
    for (DWORD i = 0; i < frame_count_; ++i) {
        offsets_.push_back(offset);
        DWORD n = le32(table.data() + i * 4);
        if (n < 4 || n > 64 * 1024 * 1024)
            table_valid_ = false;
        offset += n;
    }
    offsets_.push_back(offset);
    if (input.size && offset > input.size)
        table_valid_ = false;
    reset_bits();
}
bool TtaDecoder::read(Bytes &pcm) {
    if (frame_index_ >= frame_count_) {
        pcm.clear();
        return false;
    }
    const uint64_t start = uint64_t(frame_index_) * frame_length_;
    const DWORD samples = DWORD(std::min<uint64_t>(frame_length_, total_ - start));
    const unsigned channels = format_.nChannels, bytes = format_.wBitsPerSample / 8;
    const bool floating = format_.wFormatTag == 3;
    const unsigned states = channels * (floating ? 2 : 1), shift = bytes == 1   ? 10
                                                                   : bytes == 2 ? 9
                                                                   : bytes == 3 ? 10
                                                                                : 12;
    std::array<Channel, 16> state{};
    pcm.resize(size_t(samples) * format_.nBlockAlign);
    size_t out{};
    crc_ = ~0u;
    frame_bytes_ = 0;
    bits_ = 0;
    available_ = 0;
    for (DWORD sample = 0; sample < samples; ++sample) {
        uint32_t values[16]{};
        for (unsigned ch = 0; ch < states; ++ch) {
            auto &s = state[ch];
            uint32_t u = unary();
            const bool high = u != 0;
            const unsigned k = high ? s.k1 : s.k0;
            if (high)
                --u;
            require(k < 32 && uint64_t(u) <= (uint64_t(UINT32_MAX) >> k));
            uint32_t value = (u << k) | bits(k);
            if (high) {
                adapt(value, s.sum1, s.k1);
                value += 1u << s.k0;
            }
            adapt(value, s.sum0, s.k0);
            value = (value & 1) ? (value >> 1) + 1 : 0u - (value >> 1);
            value = s.filter(value, shift, bytes != 4);
            if (bytes == 4)
                value += s.last;
            else {
                unsigned p = bytes == 1 ? 4 : 5;
                value += uint32_t((int64_t(int32_t(s.last)) * ((1 << p) - 1)) >> p);
            }
            s.last = value;
            values[ch] = value;
        }
        if (floating) {
            for (unsigned ch = 0; ch < channels; ++ch) {
                uint32_t high = values[ch * 2], v = values[ch * 2 + 1];
                uint32_t sign = v & 0x80000000u;
                uint32_t abs = v & 0x80000000u ? 0u - v : v;
                uint32_t magnitude = abs - 1;
                uint32_t exponent = (high == 0 && magnitude == 0) ? 0 : 0x3f80;
                uint32_t word = reverse16(magnitude) | (high + exponent) << 16 | sign;
                for (unsigned b = 0; b < 4; ++b)
                    pcm[out++] = BYTE(word >> (8 * b));
            }
        } else {
            if (channels > 1) {
                values[channels - 1] += uint32_t(int32_t(values[channels - 2]) / 2);
                for (unsigned ch = channels - 1; ch > 0; --ch)
                    values[ch - 1] = values[ch] - values[ch - 1];
            }
            for (unsigned ch = 0; ch < channels; ++ch) {
                uint32_t v = values[ch];
                if (bytes == 1)
                    v += 128;
                for (unsigned b = 0; b < bytes; ++b)
                    pcm[out++] = BYTE(v >> (8 * b));
            }
        }
    }
    bits_ = 0;
    available_ = 0;
    BYTE c[4];
    for (auto &b : c)
        b = byte(false);
    require(le32(c) == ~crc_, HRESULT_FROM_WIN32(ERROR_CRC));
    ++frame_index_;
    position_ = start + samples;
    return true;
}
void TtaDecoder::seek_sample(uint64_t sample) {
    require(sample <= total_, E_INVALIDARG);
    require(seekable(), STG_E_INVALIDFUNCTION);
    frame_index_ = sample == total_ ? frame_count_ : DWORD(sample / frame_length_);
    input_->at(offsets_[frame_index_]);
    reset_bits();
    position_ = uint64_t(frame_index_) * frame_length_;
}
} // namespace ttp::flac
