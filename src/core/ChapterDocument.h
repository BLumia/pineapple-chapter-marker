#pragma once

#include <QVector>

#include "Chapter.h"

namespace pcm {

// Chapter list + media duration; the core editable document.
class ChapterDocument {
public:
    QVector<Chapter> chapters;
    qint64 durationUs = 0; // 0 when unknown

    int rowCount() const { return chapters.size(); }

    // "Auto end" of chapter row: next chapter's start; for the last chapter the media end (falls back to its start when unknown)
    qint64 autoEndUs(int row) const;
    // Explicit end, or the auto end
    qint64 effectiveEndUs(int row) const;
    qint64 lengthUs(int row) const;

    void sortByStart();

    struct RowFlags {
        bool startOutOfRange = false; // start out of range (<0 or >= media duration)
        bool endInvalid = false;      // explicit end <= start
        bool overlapsPrev = false;    // overlaps the previous chapter's effective range
    };
    RowFlags rowFlags(int row) const;

    // Index of the chapter containing time us; -1 if none
    int chapterAt(qint64 us) const;
};

} // namespace pcm
