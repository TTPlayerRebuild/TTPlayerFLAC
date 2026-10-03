#include "metadata.h"
#include "tta.h"
#include <FLAC/stream_decoder.h>
namespace ttp::flac {
namespace {
class AudioReader final : public Reader, public Metadata, public Thumbnail, public MetadataCommit {
    LONG refs_{1};
    bool tta_{}, opened_{}, eof_{};
    Input input_;
    ComPtr<Metadata> content_;
    NativeMetadata metadata_;
    TtaDecoder tta_decoder_;
    FLAC__StreamDecoder *decoder_{};
    WAVEFORMATEX format_{};
    uint64_t samples_{}, position_{}, skip_{};
    DWORD duration_{}, bitrate_{};
    Bytes pcm_;
    size_t consumed_{};
    HRESULT error_{S_OK};
    unsigned sync_errors_{};
    bool block_ready_{};
    bool commit_attempted_{};
    HRESULT commit_result_{S_OK};
    static FLAC__StreamDecoderReadStatus
    read_callback(const FLAC__StreamDecoder *, FLAC__byte out[], size_t *n, void *p) noexcept {
        auto &s = *static_cast<AudioReader *>(p);
        ULONG got{};
        auto hr = protect([&]() -> HRESULT {
            require(n && *n && *n <= MAXDWORD, E_INVALIDARG);
            got = s.input_.read(out, ULONG(*n));
            return S_OK;
        });
        if (FAILED(hr)) {
            s.error_ = hr;
            *n = 0;
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        *n = got;
        return got ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE
                   : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }
    static FLAC__StreamDecoderSeekStatus seek_callback(const FLAC__StreamDecoder *,
                                                       FLAC__uint64 offset, void *p) noexcept {
        auto &s = *static_cast<AudioReader *>(p);
        if (!s.input_.seekable)
            return FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;
        auto hr = protect([&]() -> HRESULT {
            require(offset <= UINT64_MAX - s.input_.base);
            s.input_.at(s.input_.base + offset);
            return S_OK;
        });
        if (FAILED(hr)) {
            s.error_ = hr;
            return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
        }
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }
    static FLAC__StreamDecoderTellStatus tell_callback(const FLAC__StreamDecoder *,
                                                       FLAC__uint64 *out, void *p) noexcept {
        auto &s = *static_cast<AudioReader *>(p);
        auto hr = protect([&]() -> HRESULT {
            auto pos = tell(s.input_.stream.p);
            require(pos >= s.input_.base);
            *out = pos - s.input_.base;
            return S_OK;
        });
        return FAILED(hr) ? FLAC__STREAM_DECODER_TELL_STATUS_ERROR
                          : FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }
    static FLAC__StreamDecoderLengthStatus length_callback(const FLAC__StreamDecoder *,
                                                           FLAC__uint64 *out, void *p) noexcept {
        auto &s = *static_cast<AudioReader *>(p);
        if (!s.input_.size || s.input_.size < s.input_.base)
            return FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED;
        *out = s.input_.size - s.input_.base;
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }
    static FLAC__bool eof_callback(const FLAC__StreamDecoder *, void *p) noexcept {
        auto &s = *static_cast<AudioReader *>(p);
        ULARGE_INTEGER pos{};
        LARGE_INTEGER z{};
        return s.input_.size && SUCCEEDED(s.input_.stream->Seek(z, STREAM_SEEK_CUR, &pos)) &&
               pos.QuadPart >= s.input_.size;
    }
    static FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder *,
                                                         const FLAC__Frame *f,
                                                         const FLAC__int32 *const data[],
                                                         void *p) noexcept {
        auto &s = *static_cast<AudioReader *>(p);
        auto hr = protect([&]() -> HRESULT {
            check(s.error_);
            require(f && f->header.channels == s.format_.nChannels &&
                        f->header.bits_per_sample == s.format_.wBitsPerSample &&
                        f->header.sample_rate == s.format_.nSamplesPerSec,
                    E_NOTIMPL);
            require(f->header.blocksize && f->header.blocksize <= 65535);
            s.pcm_.resize(size_t(f->header.blocksize) * s.format_.nBlockAlign);
            s.consumed_ = 0;
            s.block_ready_ = true;
            const unsigned bytes = s.format_.wBitsPerSample / 8;
            size_t at{};
            for (unsigned i = 0; i < f->header.blocksize; ++i)
                for (unsigned c = 0; c < f->header.channels; ++c) {
                    uint32_t v = uint32_t(data[c][i]);
                    if (bytes == 1)
                        v ^= 128;
                    for (unsigned b = 0; b < bytes; ++b)
                        s.pcm_[at++] = BYTE(v >> (8 * b));
                }
            return S_OK;
        });
        if (FAILED(hr)) {
            s.error_ = hr;
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }
    static void metadata_callback(const FLAC__StreamDecoder *, const FLAC__StreamMetadata *m,
                                  void *p) noexcept {
        if (m->type != FLAC__METADATA_TYPE_STREAMINFO)
            return;
        auto &s = *static_cast<AudioReader *>(p);
        const auto &i = m->data.stream_info;
        s.format_.wFormatTag = WAVE_FORMAT_PCM;
        s.format_.nChannels = WORD(i.channels);
        s.format_.wBitsPerSample = WORD(i.bits_per_sample);
        s.format_.nSamplesPerSec = i.sample_rate;
        s.format_.nBlockAlign = WORD(i.channels * (i.bits_per_sample / 8));
        s.format_.nAvgBytesPerSec = s.format_.nBlockAlign * i.sample_rate;
        s.samples_ = i.total_samples;
    }
    static void error_callback(const FLAC__StreamDecoder *, FLAC__StreamDecoderErrorStatus e,
                               void *p) noexcept {
        auto &s = *static_cast<AudioReader *>(p);
        if (e == FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC && ++s.sync_errors_ <= 128)
            return;
        s.error_ = HRESULT_FROM_WIN32(e == FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH
                                          ? ERROR_CRC
                                          : ERROR_INVALID_DATA);
    }
    void close_decoder() {
        if (decoder_) {
            FLAC__stream_decoder_finish(decoder_);
            FLAC__stream_decoder_delete(decoder_);
            decoder_ = nullptr;
        }
    }
    ~AudioReader() {
        close_decoder();
        if (!tta_) {
            content_.reset();
            auto hr = commit_attempted_ ? commit_result_ : metadata_.save(input_);
            if (FAILED(hr)) {
                wchar_t message[160];
                swprintf_s(message,
                           L"ttp_flac: metadata save failed: 0x%08lX; original file retained or "
                           L"rollback attempted.\n",
                           ULONG(hr));
                OutputDebugStringW(message);
            }
        }
    }
    void mutable_metadata() {
        require(opened_, E_UNEXPECTED);
        require(input_.capabilities(!tta_) & 4, E_ACCESSDENIED);
    }
    void change_tags(std::vector<Tag> updated) {
        if (content_)
            metadata_.external.read(input_, metadata_.audio);
        const bool previous = metadata_.tags_changed;
        metadata_.tags.swap(updated);
        metadata_.tags_changed = true;
        try {
            metadata_.validate();
        } catch (...) {
            metadata_.tags.swap(updated);
            metadata_.tags_changed = previous;
            throw;
        }
    }
    void change_covers(std::vector<Cover> updated) {
        const bool previous = metadata_.covers_changed;
        metadata_.covers.swap(updated);
        metadata_.covers_changed = true;
        try {
            metadata_.validate();
        } catch (...) {
            metadata_.covers.swap(updated);
            metadata_.covers_changed = previous;
            throw;
        }
    }
    bool next_block() {
        block_ready_ = false;
        consumed_ = 0;
        check(error_);
        if (tta_)
            return tta_decoder_.read(pcm_);
        for (unsigned n = 0; n < 1024 && !block_ready_; ++n) {
            const auto state = FLAC__stream_decoder_get_state(decoder_);
            if (state == FLAC__STREAM_DECODER_END_OF_STREAM) {
                if (samples_ && position_ < samples_)
                    throw Failure{STG_E_READFAULT};
                return false;
            }
            require(FLAC__stream_decoder_process_single(decoder_) != 0,
                    FAILED(error_) ? error_ : E_FAIL);
            check(error_);
        }
        require(block_ready_, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        return true;
    }

  public:
    explicit AudioReader(bool tta) : tta_(tta) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (same(id, IID_IUnknown) || same(id, iid_reader))
            *out = static_cast<Reader *>(this);
        else if (same(id, iid_metadata) && (!tta_ || content_))
            *out = static_cast<Metadata *>(this);
        else if (same(id, iid_thumbnail) && !tta_)
            *out = static_cast<Thumbnail *>(this);
        else if (same(id, iid_metadata_commit) && !tta_)
            *out = static_cast<MetadataCommit *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = InterlockedDecrement(&refs_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Commit() override {
        if (commit_attempted_)
            return commit_result_;
        if (tta_)
            return E_NOINTERFACE;
        if (!opened_)
            return E_UNEXPECTED;
        commit_attempted_ = true;
        opened_ = false;
        close_decoder();
        content_.reset();
        commit_result_ = metadata_.save(input_);
        return commit_result_;
    }
    HRESULT STDMETHODCALLTYPE Open(IStream *stream, DWORD) override {
        if (!stream)
            return E_POINTER;
        if (input_.stream || commit_attempted_)
            return E_UNEXPECTED;
        return protect([&]() -> HRESULT {
            input_.open(stream);
            if (auto create = standard_content()) {
                ULONGLONG length{};
                auto hr = create(stream, tta_ ? 4 : 5, content_.put(), &length);
                if (FAILED(hr))
                    content_.reset();
            }
            input_.signature(tta_ ? "TTA1" : "fLaC");
            if (tta_) {
                tta_decoder_.open(input_);
                format_ = tta_decoder_.format();
                samples_ = tta_decoder_.total();
            } else {
                metadata_.read(input_);
                if (content_) {
                    DWORD n{};
                    if (SUCCEEDED(content_->Count(&n)) && n < 100000)
                        for (DWORD i = 0; i < n; ++i) {
                            wchar_t *key{}, *value{};
                            auto hr = content_->At(i, &key, &value);
                            if (SUCCEEDED(hr) && key && value && *key && *value) {
                                auto k = utf8(key);
                                bool found = false;
                                for (const auto &t : metadata_.tags)
                                    if (key_equal(t.key, k)) {
                                        found = true;
                                        break;
                                    }
                                if (!found)
                                    metadata_.tags.push_back({k, utf8(value), {}});
                            }
                            CoTaskMemFree(key);
                            CoTaskMemFree(value);
                        }
                }
                input_.at(input_.base);
                decoder_ = FLAC__stream_decoder_new();
                require(decoder_, E_OUTOFMEMORY);
                FLAC__stream_decoder_set_md5_checking(decoder_, false);
                auto status = FLAC__stream_decoder_init_stream(
                    decoder_, read_callback, seek_callback, tell_callback, length_callback,
                    eof_callback, write_callback, metadata_callback, error_callback, this);
                require(status == FLAC__STREAM_DECODER_INIT_STATUS_OK);
                require(FLAC__stream_decoder_process_until_end_of_metadata(decoder_) != 0);
                check(error_);
            }
            require(format_.nChannels >= 1 && format_.nChannels <= 8 && format_.nSamplesPerSec &&
                    format_.nBlockAlign);
            require(format_.wBitsPerSample == 8 || format_.wBitsPerSample == 16 ||
                        format_.wBitsPerSample == 24 || format_.wBitsPerSample == 32,
                    E_NOTIMPL);
            duration_ = milliseconds(samples_, format_.nSamplesPerSec);
            if (duration_)
                bitrate_ = dword(input_.size > UINT64_MAX / 8000 ? MAXDWORD
                                                                 : input_.size * 8000 / duration_);
            opened_ = true;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Capabilities(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = input_.capabilities(!tta_);
        if (tta_ && opened_ && !tta_decoder_.seekable())
            *out &= ~2u;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Duration(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = duration_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Format(WAVEFORMATEX **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!opened_)
            return E_UNEXPECTED;
        auto *p = static_cast<WAVEFORMATEX *>(CoTaskMemAlloc(sizeof(format_)));
        if (!p)
            return E_OUTOFMEMORY;
        *p = format_;
        *out = p;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferSize(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = format_.nBlockAlign * (tta_ ? 2048 : 1024);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CodecName(wchar_t **out) override {
        return text(tta_ ? L"TTA|True Audio" : L"FLAC|FLAC Audio", out);
    }
    HRESULT STDMETHODCALLTYPE Bitrate(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = bitrate_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BitDepth(WORD *out) override {
        if (!out)
            return E_POINTER;
        *out = format_.wBitsPerSample;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCallback(IUnknown *p) override {
        if (!input_.stream)
            return E_UNEXPECTED;
        return input_.callback(p);
    }
    HRESULT STDMETHODCALLTYPE Start() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Read(Buffer *buffer) override {
        if (!buffer)
            return E_POINTER;
        const auto result = protect([&]() -> HRESULT {
            require(opened_, E_UNEXPECTED);
            check(buffer->SetLength(0));
            check(error_);
            if (eof_)
                return S_FALSE;
            DWORD capacity{}, length{};
            BYTE *data{};
            check(buffer->Capacity(&capacity));
            require(capacity >= format_.nBlockAlign, E_INVALIDARG);
            check(buffer->Data(&data, &length));
            require(data, E_POINTER);
            capacity -= capacity % format_.nBlockAlign;
            while (consumed_ == pcm_.size()) {
                if (!next_block()) {
                    eof_ = true;
                    return S_FALSE;
                }
                if (skip_) {
                    size_t n = size_t(std::min<uint64_t>(skip_, pcm_.size()));
                    consumed_ += n;
                    skip_ -= n;
                }
            }
            const DWORD n = DWORD(std::min<size_t>(capacity, pcm_.size() - consumed_));
            memcpy(data, pcm_.data() + consumed_, n);
            check(buffer->SetLength(n));
            consumed_ += n;
            position_ += n / format_.nBlockAlign;
            return S_OK;
        });
        if (FAILED(result) && result != E_INVALIDARG && result != E_POINTER)
            error_ = result;
        return result;
    }
    HRESULT STDMETHODCALLTYPE Seek(DWORD *ms) override {
        if (!ms)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            require(opened_, E_UNEXPECTED);
            uint64_t sample = uint64_t(*ms) * format_.nSamplesPerSec / 1000;
            require(!samples_ || sample <= samples_, E_INVALIDARG);
            error_ = S_OK;
            sync_errors_ = 0;
            pcm_.clear();
            consumed_ = 0;
            skip_ = 0;
            eof_ = samples_ && sample == samples_;
            if (tta_) {
                tta_decoder_.seek_sample(sample);
                if (!eof_)
                    skip_ = (sample % (uint64_t(format_.nSamplesPerSec) * 256 / 245)) *
                            format_.nBlockAlign;
            } else if (!eof_) {
                const auto state = FLAC__stream_decoder_get_state(decoder_);
                if (state == FLAC__STREAM_DECODER_ABORTED ||
                    state == FLAC__STREAM_DECODER_SEEK_ERROR)
                    require(FLAC__stream_decoder_flush(decoder_) != 0);
                if (!FLAC__stream_decoder_seek_absolute(decoder_, sample)) {
                    error_ = FAILED(error_) ? error_ : E_FAIL;
                    throw Failure{error_};
                }
                check(error_);
            }
            position_ = sample;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Count(DWORD *out) override {
        if (!out)
            return E_POINTER;
        if (tta_)
            return content_ ? content_->Count(out) : E_NOINTERFACE;
        *out = DWORD(metadata_.tags.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE At(DWORD i, wchar_t **key, wchar_t **value) override {
        if (!key || !value)
            return E_POINTER;
        *key = *value = nullptr;
        if (tta_)
            return content_ ? content_->At(i, key, value) : E_NOINTERFACE;
        return protect([&]() -> HRESULT {
            require(i < metadata_.tags.size(), E_INVALIDARG);
            check(text(wide(metadata_.tags[i].key), key));
            auto hr = text(wide(metadata_.tags[i].value), value);
            if (FAILED(hr)) {
                CoTaskMemFree(*key);
                *key = nullptr;
            }
            return hr;
        });
    }
    HRESULT STDMETHODCALLTYPE Get(const char *key, wchar_t **out) override {
        if (!out || !key)
            return E_POINTER;
        *out = nullptr;
        if (tta_)
            return content_ ? content_->Get(key, out) : E_NOINTERFACE;
        return protect([&]() -> HRESULT {
            for (auto i = metadata_.tags.rbegin(); i != metadata_.tags.rend(); ++i)
                if (key_equal(i->key, key))
                    return text(wide(i->value), out);
            return E_INVALIDARG;
        });
    }
    HRESULT STDMETHODCALLTYPE Set(const char *key, const wchar_t *value) override {
        if (tta_)
            return content_ ? content_->Set(key, value) : E_NOINTERFACE;
        return protect([&]() -> HRESULT {
            mutable_metadata();
            if (!key) {
                require(!value, E_INVALIDARG);
                change_tags({});
                return S_OK;
            }
            const std::string name(key);
            require(!name.empty() && name.size() <= 1024, E_INVALIDARG);
            for (unsigned char c : name)
                require(c >= 32 && c <= 125 && c != '=', E_INVALIDARG);
            auto updated = metadata_.tags;
            bool replaced{};
            std::string v = value ? utf8(value) : "";
            require(v.size() <= 16 * 1024 * 1024, E_INVALIDARG);
            for (size_t i = updated.size(); i > 0; --i)
                if (key_equal(updated[i - 1].key, name)) {
                    if (!v.empty() && !replaced) {
                        updated[i - 1].value = v;
                        updated[i - 1].raw = {};
                        replaced = true;
                    } else
                        updated.erase(updated.begin() + i - 1);
                }
            if (!v.empty() && !replaced)
                updated.push_back({name, v, {}});
            change_tags(std::move(updated));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE PictureCount(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = DWORD(metadata_.covers.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MaximumBytes(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = picture_limit;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MaximumCount(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = 64;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PictureAt(DWORD i, const Picture **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (i >= metadata_.covers.size())
            return E_INVALIDARG;
        auto &c = metadata_.covers[i];
        c.view = {sizeof(Picture),      c.mime.c_str(), c.description.c_str(),
                  DWORD(c.data.size()), c.data.data(),  c.type};
        *out = &c.view;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddPicture(const Picture *p) override {
        if (!p)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            mutable_metadata();
            require(metadata_.covers.size() < 64, E_INVALIDARG);
            auto c = make_cover(*p);
            auto updated = metadata_.covers;
            updated.push_back(std::move(c));
            change_covers(std::move(updated));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE RemovePicture(DWORD i) override {
        return protect([&]() -> HRESULT {
            mutable_metadata();
            require(i < metadata_.covers.size(), E_INVALIDARG);
            auto updated = metadata_.covers;
            updated.erase(updated.begin() + i);
            change_covers(std::move(updated));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE ReplacePicture(DWORD i, const Picture *p, DWORD mask) override {
        if (!p)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            mutable_metadata();
            require(p->size >= sizeof(Picture) && i < metadata_.covers.size() && !(mask & ~15u),
                    E_INVALIDARG);
            auto c = metadata_.covers[i];
            if (mask & 1)
                c.mime = p->mime ? p->mime : L"";
            if (mask & 2)
                c.description = p->description ? p->description : L"";
            if (mask & 4) {
                require(p->type <= 20, E_INVALIDARG);
                c.type = p->type;
            }
            if (mask & 8) {
                Picture merged{sizeof(Picture), c.mime.c_str(), c.description.c_str(),
                               p->bytes,        p->data,        c.type};
                c = make_cover(merged);
            }
            auto updated = metadata_.covers;
            updated[i] = std::move(c);
            change_covers(std::move(updated));
            return S_OK;
        });
    }
};
} // namespace
HRESULT make_reader(bool tta, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return protect([&]() -> HRESULT {
        *out = static_cast<Reader *>(new AudioReader(tta));
        return S_OK;
    });
}
} // namespace ttp::flac
