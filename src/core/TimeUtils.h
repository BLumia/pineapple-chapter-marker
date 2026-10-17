#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

#include "Chapter.h"

class QRegularExpressionMatch;

namespace pcm::time {

constexpr qint64 kUsPerMs = 1000;
constexpr qint64 kUsPerSecond = 1000 * kUsPerMs;
constexpr qint64 kUsPerMinute = 60 * kUsPerSecond;
constexpr qint64 kUsPerHour = 60 * kUsPerMinute;

// Fixed-width clock for table display/editing: HH:MM:SS.mmm
QString formatClockFixed(qint64 us);

// Compact clock for export/duration display: H:MM:SS or M:SS (with .mmm only when needed)
QString formatClockCompact(qint64 us);

// Lenient parsing: "ss(.fff)" / "mm:ss(.fff)" / "hh:mm:ss(.fff)"
// Full-width colons and full-width commas are normalized automatically. Returns std::nullopt on failure.
std::optional<qint64> parseTimestamp(const QString& text);

struct TimestampParseResult {
    QVector<Chapter> chapters;
    QStringList warnings; // lines that failed to parse (first 20 kept)
    int lineCount = 0;    // non-empty lines fed to the parser
    int skippedLines = 0; // non-empty lines that failed to parse
};

// YouTube-style text import: one entry per line, timestamp at line start (bullets/brackets and other decorations allowed).
// Timestamp format m:ss[:ss][.fff]. Lines without a title get "Chapter N". Results are sorted by start time.
TimestampParseResult parseTimestampText(const QString& text);

// YouTube-style export: one "<m:ss|h:mm:ss> <title>" per line; empty titles get "Chapter N".
// Timestamps are whole seconds (rounded), matching YouTube chapter format.
QString toYouTubeText(const QVector<Chapter>& chapters);

// YouTube chapter display rules: at least 3 chapters, first at 0:00, strictly increasing times.
QStringList youTubeRuleWarnings(const QVector<Chapter>& chapters);

} // namespace pcm::time
