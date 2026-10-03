#include "metadata.h"
#include <shlwapi.h>
namespace ttp::flac {
namespace {
struct Cursor {
    const StoredBytes &b;
    size_t p{};
    void need(size_t n) { require(p <= b.size() && n <= b.size() - p); }
    DWORD big() {
        need(4);
        auto n = be32(b.data() + p);
        p += 4;
        return n;
    }
    DWORD little() {
        need(4);
        auto n = le32(b.data() + p);
        p += 4;
        return n;
    }
    std::string_view view(bool bigendian) {
        auto n = bigendian ? big() : little();
        need(n);
        std::string_view s(reinterpret_cast<const char *>(b.data() + p), n);
        p += n;
        return s;
    }
    std::string str(bool bigendian) { return std::string(view(bigendian)); }
};
void str(Bytes &b, std::string_view s, bool big) {
    require(s.size() < metadata_limit);
    if (big)
        put32(b, DWORD(s.size()));
    else
        put_le32(b, DWORD(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
void image_dimensions(Cover &c) {
    const auto &b = c.data;
    if (b.size() >= 33 && !memcmp(b.data(), "\x89PNG\r\n\x1a\n", 8)) {
        require(be32(b.data() + 8) == 13 && !memcmp(b.data() + 12, "IHDR", 4), E_INVALIDARG);
        c.mime = L"image/png";
        c.width = be32(b.data() + 16);
        c.height = be32(b.data() + 20);
        const BYTE bits = b[24], type = b[25];
        unsigned components = type == 0 || type == 3 ? 1
                              : type == 2            ? 3
                              : type == 4            ? 2
                              : type == 6            ? 4
                                                     : 0;
        require(components && (bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16),
                E_INVALIDARG);
        c.depth = bits * components;
        if (type == 3) {
            for (size_t p = 33; p + 12 <= b.size();) {
                auto n = be32(b.data() + p);
                require(n <= b.size() - p - 12, E_INVALIDARG);
                if (!memcmp(b.data() + p + 4, "PLTE", 4)) {
                    c.colors = n / 3;
                    break;
                }
                p += n + 12;
            }
        }
    } else if (b.size() >= 10 &&
               (!memcmp(b.data(), "GIF87a", 6) || !memcmp(b.data(), "GIF89a", 6))) {
        c.mime = L"image/gif";
        c.width = le16(b.data() + 6);
        c.height = le16(b.data() + 8);
        require(b.size() >= 13, E_INVALIDARG);
        c.depth = ((b[10] >> 4) & 7) + 1;
        c.colors = (b[10] & 128) ? 1u << ((b[10] & 7) + 1) : 0;
    } else if (b.size() >= 4 && b[0] == 255 && b[1] == 216) {
        c.mime = L"image/jpeg";
        size_t p = 2;
        bool found{};
        while (p + 1 < b.size()) {
            require(b[p++] == 255, E_INVALIDARG);
            while (p < b.size() && b[p] == 255)
                ++p;
            require(p < b.size(), E_INVALIDARG);
            BYTE marker = b[p++];
            if (marker == 217 || marker == 218)
                break;
            if (marker == 1 || (marker >= 208 && marker <= 215))
                continue;
            require(p + 2 <= b.size(), E_INVALIDARG);
            size_t n = be16(b.data() + p);
            require(n >= 2 && n <= b.size() - p, E_INVALIDARG);
            if ((marker >= 192 && marker <= 195) || (marker >= 197 && marker <= 199) ||
                (marker >= 201 && marker <= 203) || (marker >= 205 && marker <= 207)) {
                require(n >= 8, E_INVALIDARG);
                c.height = be16(b.data() + p + 3);
                c.width = be16(b.data() + p + 5);
                c.depth = b[p + 2] * b[p + 7];
                found = true;
                break;
            }
            p += n;
        }
        require(found, E_INVALIDARG);
    } else
        throw Failure{E_INVALIDARG};
    require(c.width && c.height && c.width <= 100000 && c.height <= 100000 && c.depth,
            E_INVALIDARG);
}
void copy(IStream *a, IStream *b, uint64_t n, Bytes &buffer) {
    while (n) {
        DWORD count = DWORD(std::min<uint64_t>(buffer.size(), n));
        read_exact(a, buffer.data(), count);
        write_exact(b, buffer.data(), count);
        n -= count;
    }
}
struct Temporary {
    std::wstring path;
    ComPtr<IStream> stream;
    bool keep{};
    explicit Temporary(const std::wstring &dir) {
        wchar_t name[MAX_PATH]{};
        if (!GetTempFileNameW(dir.c_str(), L"tfl", 0, name))
            throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        path = name;
        auto hr = SHCreateStreamOnFileEx(name, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, FALSE,
                                         nullptr, stream.put());
        if (FAILED(hr)) {
            DeleteFileW(name);
            throw Failure{hr};
        }
    }
    ~Temporary() {
        stream.reset();
        if (!keep)
            DeleteFileW(path.c_str());
    }
};
} // namespace
bool key_equal(const std::string &a, const std::string &b) {
    if (a.size() != b.size())
        return false;
    auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
    for (size_t i = 0; i < a.size(); ++i)
        if (fold(a[i]) != fold(b[i]))
            return false;
    return true;
}
Cover parse_cover(const StoredBytes &data) {
    Cursor in{data};
    Cover c;
    c.type = in.big();
    c.mime = wide(in.str(true));
    c.description = wide(in.str(true));
    c.width = in.big();
    c.height = in.big();
    c.depth = in.big();
    c.colors = in.big();
    DWORD n = in.big();
    in.need(n);
    require(n <= picture_limit);
    c.data = data.slice(in.p, n);
    return c;
}
Cover make_cover(const Picture &p) {
    require(p.size >= sizeof(Picture) && p.data && p.bytes && p.bytes <= picture_limit &&
                p.type <= 20,
            E_INVALIDARG);
    Cover c;
    c.type = p.type;
    if (p.description)
        c.description = p.description;
    require(c.description.size() <= 1024 * 1024, E_INVALIDARG);
    c.data = Bytes(p.data, p.data + p.bytes);
    image_dimensions(c);
    return c;
}
Bytes cover_block(const Cover &c) {
    Bytes b;
    b.reserve(32 + utf8(c.mime).size() + utf8(c.description).size() + c.data.size());
    put32(b, c.type);
    str(b, utf8(c.mime), true);
    str(b, utf8(c.description), true);
    put32(b, c.width);
    put32(b, c.height);
    put32(b, c.depth);
    put32(b, c.colors);
    put32(b, DWORD(c.data.size()));
    b.insert(b.end(), c.data.begin(), c.data.end());
    require(b.size() <= 0xffffff);
    return b;
}
void NativeMetadata::read(Input &in) {
    base = in.base;
    size = in.size;
    in.at(base);
    BYTE marker[4];
    in.exact(marker, 4);
    require(!memcmp(marker, "fLaC", 4));
    uint64_t bytes{};
    bool last{};
    while (!last) {
        require(blocks.size() < 4096);
        BYTE header[4];
        in.exact(header, 4);
        last = !!(header[0] & 128);
        BYTE type = header[0] & 127;
        require(type != 127);
        DWORD n = (DWORD(header[1]) << 16) | (DWORD(header[2]) << 8) | header[3];
        bytes += n + 4;
        require(bytes <= metadata_limit);
        Bytes data(n);
        in.exact(data.data(), n);
        Block block{type, std::move(data)};
        if (blocks.empty())
            require(type == 0 && n == 34);
        else
            require(type != 0);
        if (type == 4) {
            Cursor cur{block.data};
            auto v = cur.str(false);
            if (vendor.empty())
                vendor = std::move(v);
            DWORD count = cur.little();
            require(count <= 100000 && count <= (n - cur.p) / 4);
            for (DWORD i = 0; i < count; ++i) {
                auto raw = cur.view(false);
                const auto at = raw.find('=');
                if (at != std::string::npos && at) {
                    tags.push_back(
                        {std::string(raw.substr(0, at)), std::string(raw.substr(at + 1)), raw});
                }
            }
        }
        if (type == 6) {
            bool shown{};
            try {
                auto c = parse_cover(block.data);
                if (c.mime != L"-->" && !c.data.empty() && covers.size() < 64) {
                    covers.push_back(std::move(c));
                    shown = true;
                }
            } catch (const Failure &) {
            }
            if (!shown)
                preserved_pictures.push_back(block);
        }
        blocks.push_back(std::move(block));
    }
    audio = tell(in.stream.p);
    require(!size || audio <= size);
    std::stable_partition(covers.begin(), covers.end(), [](const Cover &c) { return c.type == 3; });
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!in.path.empty() && GetFileAttributesExW(in.path.c_str(), GetFileExInfoStandard, &a))
        filetime = a.ftLastWriteTime;
}
void NativeMetadata::validate() const {
    uint64_t total{};
    auto add = [&](uint64_t n) {
        require(n <= 0xffffff);
        total += n + 4;
        require(total <= metadata_limit);
    };
    bool comments{};
    auto tags_size = [&]() {
        uint64_t n = 8 + (vendor.empty() ? 13 : vendor.size());
        for (const auto &t : tags)
            n += 4 + (t.raw.empty() ? t.key.size() + 1 + t.value.size() : t.raw.size());
        add(n);
        comments = true;
    };
    for (const auto &b : blocks) {
        if (b.type == 4 && tags_changed) {
            if (!comments)
                tags_size();
        } else if (b.type != 6 || !covers_changed)
            add(b.data.size());
    }
    if (tags_changed && !comments)
        tags_size();
    if (covers_changed) {
        for (const auto &c : covers)
            add(32ULL + utf8(c.mime).size() + utf8(c.description).size() + c.data.size());
        for (const auto &b : preserved_pictures)
            add(b.data.size());
    }
}
std::vector<Block> NativeMetadata::serialized() const {
    validate();
    std::vector<Block> out;
    bool comments{};
    auto add_tags = [&]() {
        Bytes b;
        str(b, vendor.empty() ? "TTPlayer FLAC" : vendor, false);
        put_le32(b, DWORD(tags.size()));
        size_t bytes = 8 + (vendor.empty() ? 13 : vendor.size());
        for (const auto &t : tags)
            bytes += 4 + (t.raw.empty() ? t.key.size() + 1 + t.value.size() : t.raw.size());
        b.reserve(bytes);
        for (const auto &t : tags) {
            if (t.raw.empty())
                str(b, t.key + "=" + t.value, false);
            else
                str(b, t.raw, false);
        }
        require(b.size() <= 0xffffff);
        out.push_back({4, std::move(b)});
        comments = true;
    };
    for (const auto &b : blocks) {
        if (b.type == 4 && tags_changed) {
            if (!comments)
                add_tags();
            continue;
        }
        if (b.type == 6 && covers_changed)
            continue;
        out.push_back(b);
    }
    if (tags_changed && !comments)
        add_tags();
    if (covers_changed) {
        for (const auto &c : covers)
            out.push_back({6, cover_block(c)});
        out.insert(out.end(), preserved_pictures.begin(), preserved_pictures.end());
    }
    uint64_t total{};
    for (const auto &b : out) {
        require(b.data.size() <= 0xffffff);
        total += b.data.size() + 4;
    }
    require(total <= metadata_limit);
    return out;
}
HRESULT NativeMetadata::save(Input &input) noexcept {
    if (!tags_changed && !covers_changed)
        return S_OK;
    return protect([&]() -> HRESULT {
        require(input.capabilities(true) & 4, E_ACCESSDENIED);
        auto output = serialized();
        STATSTG st{};
        check(input.stream->Stat(&st, STATFLAG_NONAME));
        require(st.cbSize.QuadPart == size, HRESULT_FROM_WIN32(ERROR_FILE_INVALID));
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!GetFileAttributesExW(input.path.c_str(), GetFileExInfoStandard, &a))
            throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        require(CompareFileTime(&a.ftLastWriteTime, &filetime) == 0,
                HRESULT_FROM_WIN32(ERROR_FILE_INVALID));
        const auto slash = input.path.find_last_of(L"\\/");
        std::wstring directory =
            slash == std::wstring::npos ? L"." : input.path.substr(0, slash + 1);
        Temporary next(directory);
        Bytes scratch(256 * 1024);
        const bool migrate = tags_changed && external.ready;
        if (migrate)
            write_exact(next.stream.p, external.prefix.data(), DWORD(external.prefix.size()));
        else {
            seek(input.stream.p, 0);
            copy(input.stream.p, next.stream.p, base, scratch);
        }
        write_exact(next.stream.p, "fLaC", 4);
        for (size_t i = 0; i < output.size(); ++i) {
            const auto &b = output[i];
            DWORD n = DWORD(b.data.size());
            BYTE h[4]{BYTE(b.type | (i + 1 == output.size() ? 128 : 0)), BYTE(n >> 16),
                      BYTE(n >> 8), BYTE(n)};
            write_exact(next.stream.p, h, 4);
            write_exact(next.stream.p, b.data.data(), n);
        }
        seek(input.stream.p, audio);
        copy(input.stream.p, next.stream.p, (migrate ? external.end : size) - audio, scratch);
        if (migrate)
            write_exact(next.stream.p, external.suffix.data(), DWORD(external.suffix.size()));
        check(next.stream->Commit(STGC_DEFAULT));
        uint64_t newsize = tell(next.stream.p);
        // Existing hosts may retain a non-delete-sharing file handle. Prefer an
        // atomic path replacement; use the original writable stream with a full
        // rollback image only when a host handle makes replacement impossible.
        ComPtr<IStream> writable;
        if ((input.mode & 3) == STGM_READWRITE)
            writable = ComPtr<IStream>(input.stream.p, true);
        input.extended.reset();
        input.stream.reset();
        next.stream.reset();
        if (!writable &&
            ReplaceFileW(input.path.c_str(), next.path.c_str(), nullptr, 0, nullptr, nullptr))
            return S_OK;
        if (!writable)
            check(SHCreateStreamOnFileEx(input.path.c_str(), STGM_READWRITE | STGM_SHARE_DENY_WRITE,
                                         0, FALSE, nullptr, writable.put()));
        // A successful atomic replacement needs no second full-file copy.
        // Only the handle-preserving fallback needs a rollback image.
        check(writable->Stat(&st, STATFLAG_NONAME));
        require(st.cbSize.QuadPart == size, HRESULT_FROM_WIN32(ERROR_FILE_INVALID));
        if (!GetFileAttributesExW(input.path.c_str(), GetFileExInfoStandard, &a))
            throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        require(CompareFileTime(&a.ftLastWriteTime, &filetime) == 0,
                HRESULT_FROM_WIN32(ERROR_FILE_INVALID));
        Temporary backup(directory);
        seek(writable.p, 0);
        copy(writable.p, backup.stream.p, size, scratch);
        check(backup.stream->Commit(STGC_DEFAULT));
        check(SHCreateStreamOnFileEx(next.path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE,
                                     nullptr, next.stream.put()));
        auto overwrite = [&](IStream *from, uint64_t length) {
            seek(from, 0);
            seek(writable.p, 0);
            copy(from, writable.p, length, scratch);
            ULARGE_INTEGER n{};
            n.QuadPart = length;
            check(writable->SetSize(n));
            check(writable->Commit(STGC_DEFAULT));
        };
        auto hr = protect([&]() -> HRESULT {
            overwrite(next.stream.p, newsize);
            return S_OK;
        });
        if (FAILED(hr)) {
            auto restored = protect([&]() -> HRESULT {
                overwrite(backup.stream.p, size);
                return S_OK;
            });
            if (FAILED(restored)) {
                backup.keep = true;
                OutputDebugStringW((L"ttp_flac: recovery file: " + backup.path + L"\n").c_str());
            }
        }
        return hr;
    });
}
} // namespace ttp::flac
