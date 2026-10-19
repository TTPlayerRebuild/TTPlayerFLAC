#include "external_tags.h"
#include <ttpcomm/tags.h>
namespace ttp::flac {
namespace {
constexpr size_t limit = 16 * 1024 * 1024;
DWORD sync32(const BYTE *p) {
    require(!((p[0] | p[1] | p[2] | p[3]) & 128), E_NOTIMPL);
    return (DWORD(p[0]) << 21) | (DWORD(p[1]) << 14) | (DWORD(p[2]) << 7) | p[3];
}
Bytes range(Input &in, uint64_t at, size_t n) {
    require(n <= limit && at <= in.size && n <= in.size - at, E_NOTIMPL);
    Bytes b(n);
    in.at(at);
    in.exact(b.data(), DWORD(n));
    return b;
}
bool text_frame(const std::string &id) {
    // Only fields exposed by the original standard-content interface migrate.
    static const char *ids[] = {"TIT2", "TT2",  "TIT3", "TT3",  "TCOP", "TCR",  "TCOM",
                                "TCM",  "TPE1", "TP1",  "TPE2", "TP2",  "TPE3", "TP3",
                                "TALB", "TAL",  "TRCK", "TRK",  "TCON", "TCO",  "TDRC",
                                "TYER", "TYE",  "COMM", "COM",  "USLT", "ULT",  "TPUB",
                                "TPB",  "TENC", "TEN",  "WXXX", "WXX",  "TXXX", "TXX"};
    for (auto name : ids)
        if (id == name)
            return true;
    return false;
}
Bytes id3_prefix(Input &in) {
    if (!in.base)
        return {};
    require(in.base <= limit, E_NOTIMPL);
    auto b = range(in, 0, size_t(in.base));
    require(b.size() >= 10 && !memcmp(b.data(), "ID3", 3), E_NOTIMPL);
    unsigned major = b[3];
    require(major >= 2 && major <= 4 && b[4] == 0, E_NOTIMPL);
    const unsigned flags = b[5];
    size_t n = sync32(b.data() + 6), end = 10 + n;
    const bool footer = major == 4 && (flags & 16);
    require(end + (footer ? 10 : 0) == b.size(), E_NOTIMPL);
    if (footer)
        require(!memcmp(b.data() + end, "3DI", 3) &&
                    !memcmp(b.data() + end + 3, b.data() + 3, 7), E_NOTIMPL);
    using ttpcomm::bytes::View;
    namespace tags = ttpcomm::tags;
    View body(b.data() + 10, n);
    Bytes decoded;
    // v2.2/2.3 lengths describe the decoded body; v2.4 lengths describe the
    // stored frame payload. Its tag bit applies to every frame, not its header.
    if (major < 4 && (flags & 0x80)) {
        decoded = tags::RemoveUnsynchronization(body);
        body = View(decoded.data(), decoded.size());
    }
    const auto start = tags::FrameStart(body, major, flags);
    require(start.has_value(), E_NOTIMPL); // Includes extended-header CRC checks.
    Bytes kept;
    size_t p = *start;
    for (;;) {
        tags::Frame frame;
        const auto status = tags::NextFrame(body, major, p, frame);
        require(status != tags::FrameStatus::invalid, E_NOTIMPL);
        if (status == tags::FrameStatus::end)
            break;
        if (text_frame(std::string(frame.identifier))) {
            // The host has already supplied readable fields. A compressed or
            // encrypted text frame may not have been exposed; do not silently
            // drop it together with unrelated edits. Unknown/binary frames are
            // opaque and retain all of their status/format flags and payloads.
            require(major == 2 || !(frame.flags & (major == 3 ? 0xdfu : 0xbcu)),
                    E_NOTIMPL);
        } else {
            kept.insert(kept.end(), frame.raw.begin(), frame.raw.end());
        }
    }
    while (p < body.size())
        require(body[p++] == 0, E_NOTIMPL);
    if (kept.empty())
        return {};
    if (major < 4 && (flags & 0x80)) {
        Bytes encoded;
        encoded.reserve(kept.size());
        for (size_t i = 0; i < kept.size(); ++i) {
            encoded.push_back(kept[i]);
            if (kept[i] == 0xff &&
                (i + 1 == kept.size() || kept[i + 1] == 0 || kept[i + 1] >= 0xe0))
                encoded.push_back(0);
        }
        kept.swap(encoded);
    }
    require(kept.size() <= limit, E_NOTIMPL);
    Bytes out(b.begin(), b.begin() + 10);
    // Remove the extended header and its obsolete CRC/restrictions. Preserve
    // global unsynchronization, the experimental bit, and a matching footer.
    out[5] = BYTE(flags & ~0x40u);
    n = kept.size();
    for (int i = 9; i >= 6; --i) {
        out[i] = BYTE(n & 127);
        n >>= 7;
    }
    out.insert(out.end(), kept.begin(), kept.end());
    if (footer) {
        Bytes tail(out.begin(), out.begin() + 10);
        memcpy(tail.data(), "3DI", 3);
        out.insert(out.end(), tail.begin(), tail.end());
    }
    return out;
}
} // namespace
void ExternalTags::read(Input &input, uint64_t audio) {
    if (ready)
        return;
    struct Restore {
        Input &in;
        uint64_t at;
        ~Restore() {
            LARGE_INTEGER p{};
            p.QuadPart = at;
            in.stream->Seek(p, STREAM_SEEK_SET, nullptr);
        }
    } restore{input, tell(input.stream.p)};
    ExternalTags next;
    next.prefix = id3_prefix(input);
    next.end = input.size;
    std::vector<Bytes> trailing;
    for (unsigned pass = 0; pass < 8; ++pass) {
        if (next.end >= audio + 128) {
            auto v1 = range(input, next.end - 128, 128);
            if (!memcmp(v1.data(), "TAG", 3)) {
                next.end -= 128;
                continue;
            }
        }
        if (next.end >= audio + 26) {
            auto lyrics = range(input, next.end - 15, 15);
            if (!memcmp(lyrics.data() + 6, "LYRICS200", 9)) {
                DWORD n{};
                for (unsigned i = 0; i < 6; ++i) {
                    require(lyrics[i] >= '0' && lyrics[i] <= '9', E_NOTIMPL);
                    n = n * 10 + lyrics[i] - '0';
                }
                require(n >= 11 && n <= next.end - audio - 15, E_NOTIMPL);
                auto start = range(input, next.end - 15 - n, 11);
                require(!memcmp(start.data(), "LYRICSBEGIN", 11), E_NOTIMPL);
                next.end -= 15 + n;
                continue;
            }
            if (!memcmp(lyrics.data() + 6, "LYRICSEND", 9)) {
                size_t n = size_t(std::min<uint64_t>(5109, next.end - audio));
                auto text = range(input, next.end - n, n);
                auto start =
                    std::search(text.begin(), text.end() - 9, "LYRICSBEGIN", "LYRICSBEGIN" + 11);
                require(start != text.end() - 9, E_NOTIMPL);
                next.end -= text.end() - start;
                continue;
            }
        }
        if (next.end < audio + 32)
            break;
        auto footer = range(input, next.end - 32, 32);
        if (memcmp(footer.data(), "APETAGEX", 8))
            break;
        DWORD version = le32(footer.data() + 8), size = le32(footer.data() + 12),
              count = le32(footer.data() + 16), flags = le32(footer.data() + 20);
        require((version == 1000 || version == 2000) && size >= 32 && size <= limit &&
                    count <= 100000 && size <= next.end - audio,
                E_NOTIMPL);
        auto data = range(input, next.end - size, size - 32);
        size_t p{};
        Bytes items;
        DWORD kept{};
        for (DWORD i = 0; i < count; ++i) {
            require(data.size() - p >= 9, E_NOTIMPL);
            size_t begin = p;
            DWORD n = le32(data.data() + p), f = le32(data.data() + p + 4);
            p += 8;
            while (p < data.size() && data[p])
                ++p;
            require(p < data.size(), E_NOTIMPL);
            ++p;
            require(n <= data.size() - p, E_NOTIMPL);
            p += n;
            if (((f >> 1) & 3) != 0) {
                items.insert(items.end(), data.begin() + begin, data.begin() + p);
                ++kept;
            }
        }
        require(p == data.size(), E_NOTIMPL);
        next.end -= size;
        if (flags & 0x80000000u) {
            require(next.end >= audio + 32, E_NOTIMPL);
            auto header = range(input, next.end - 32, 32);
            require(!memcmp(header.data(), "APETAGEX", 8) && le32(header.data() + 12) == size &&
                        (le32(header.data() + 20) & 0x20000000u),
                    E_NOTIMPL);
            next.end -= 32;
        }
        if (kept) {
            Bytes tag;
            tag.insert(tag.end(), footer.begin(), footer.begin() + 8);
            put_le32(tag, 2000);
            put_le32(tag, DWORD(items.size() + 32));
            put_le32(tag, kept);
            put_le32(tag, 0);
            tag.resize(32, 0);
            items.insert(items.end(), tag.begin(), tag.end());
            trailing.push_back(std::move(items));
        }
    }
    for (auto it = trailing.rbegin(); it != trailing.rend(); ++it)
        next.suffix.insert(next.suffix.end(), it->begin(), it->end());
    require(next.end >= audio, E_NOTIMPL);
    next.ready = true;
    *this = std::move(next);
}
} // namespace ttp::flac
