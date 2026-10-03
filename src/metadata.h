#pragma once
#include "external_tags.h"
#include "io.h"
namespace ttp::flac {
constexpr size_t metadata_limit = 64 * 1024 * 1024;
constexpr DWORD picture_limit = 16 * 1024 * 1024 - 1024;
// Immutable shared storage: a picture views its existing metadata block, and
// a proposed edit copies descriptors instead of megabytes of image payload.
class StoredBytes {
    std::shared_ptr<const Bytes> owner_;
    size_t offset_{}, size_{};

  public:
    StoredBytes() = default;
    StoredBytes(Bytes bytes)
        : owner_(std::make_shared<const Bytes>(std::move(bytes))), size_(owner_->size()) {}
    size_t size() const { return size_; }
    bool empty() const { return !size_; }
    const BYTE *data() const {
        if (!owner_) return nullptr;
        return offset_ ? owner_->data() + offset_ : owner_->data();
    }
    const BYTE *begin() const { return data(); }
    const BYTE *end() const { return size_ ? data() + size_ : data(); }
    BYTE operator[](size_t i) const { return data()[i]; }
    StoredBytes slice(size_t offset, size_t length) const {
        require(offset <= size_ && length <= size_ - offset);
        StoredBytes result(*this);
        result.offset_ += offset;
        result.size_ = length;
        return result;
    }
};
struct Tag {
    std::string key, value;
    std::string_view raw; // Views immutable blocks owned by NativeMetadata.
};
struct Block {
    BYTE type{};
    StoredBytes data;
};
struct Cover {
    std::wstring mime, description;
    StoredBytes data;
    DWORD type{}, width{}, height{}, depth{}, colors{};
    Picture view{};
};
bool key_equal(const std::string &a, const std::string &b);
Cover parse_cover(const StoredBytes &data);
Cover make_cover(const Picture &picture);
Bytes cover_block(const Cover &cover);
struct NativeMetadata {
    std::vector<Block> blocks;
    std::vector<Block> preserved_pictures;
    std::vector<Tag> tags;
    std::vector<Cover> covers;
    std::string vendor;
    uint64_t base{}, audio{}, size{};
    FILETIME filetime{};
    bool tags_changed{}, covers_changed{};
    ExternalTags external;
    void read(Input &input);
    void validate() const;
    std::vector<Block> serialized() const;
    HRESULT save(Input &input) noexcept;
};
} // namespace ttp::flac
