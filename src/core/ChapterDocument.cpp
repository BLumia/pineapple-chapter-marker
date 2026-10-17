#include "ChapterDocument.h"

#include <algorithm>

namespace pcm {

qint64 ChapterDocument::autoEndUs(int row) const
{
    if (row < 0 || row >= chapters.size())
        return 0;
    if (row + 1 < chapters.size())
        return chapters.at(row + 1).startUs;
    return durationUs > 0 ? durationUs : chapters.at(row).startUs;
}

qint64 ChapterDocument::effectiveEndUs(int row) const
{
    if (row < 0 || row >= chapters.size())
        return 0;
    return chapters.at(row).endUs.value_or(autoEndUs(row));
}

qint64 ChapterDocument::lengthUs(int row) const
{
    if (row < 0 || row >= chapters.size())
        return 0;
    return effectiveEndUs(row) - chapters.at(row).startUs;
}

void ChapterDocument::sortByStart()
{
    std::stable_sort(chapters.begin(), chapters.end(),
                     [](const Chapter &a, const Chapter &b) { return a.startUs < b.startUs; });
}

ChapterDocument::RowFlags ChapterDocument::rowFlags(int row) const
{
    RowFlags f;
    if (row < 0 || row >= chapters.size())
        return f;
    const Chapter &ch = chapters.at(row);
    f.startOutOfRange = ch.startUs < 0 || (durationUs > 0 && ch.startUs >= durationUs);
    if (ch.endUs)
        f.endInvalid = *ch.endUs <= ch.startUs;
    if (row > 0)
        f.overlapsPrev = ch.startUs < effectiveEndUs(row - 1);
    return f;
}

int ChapterDocument::chapterAt(qint64 us) const
{
    for (int i = 0; i < chapters.size(); ++i) {
        if (us >= chapters.at(i).startUs && us < effectiveEndUs(i))
            return i;
    }
    return -1;
}

} // namespace pcm
