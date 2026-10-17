#pragma once

#include <QString>
#include <optional>

namespace pcm {

// The unified chapter data model.
//
// endUs == std::nullopt means an "auto end":
//   - not the last chapter: inherits the next chapter's start time
//   - last chapter: end of media
// Reality across containers:
//   - MKV / MKA / ID3v2 CHAP: explicit ends are stored (gaps supported)
//   - MP4 / M4A / M4B: end implicitly equals the next start; gaps cannot be stored (warn before saving)
struct Chapter {
    qint64 startUs = 0;
    std::optional<qint64> endUs;
    QString title;

    bool operator==(const Chapter&) const = default;
};

} // namespace pcm
