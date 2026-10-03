#pragma once
#include "io.h"
namespace ttp::flac {
// Text exposed by CreateStdContent is migrated to native comments. Retain
// external binary/unknown fields, and stage all changes in the same transaction.
struct ExternalTags {
    Bytes prefix, suffix;
    uint64_t end{};
    bool ready{};
    void read(Input &input, uint64_t audio);
};
} // namespace ttp::flac
