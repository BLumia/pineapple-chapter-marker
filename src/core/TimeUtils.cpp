#include "TimeUtils.h"

#include <algorithm>

#include <QCoreApplication>
#include <QRegularExpression>

namespace pcm::time {

namespace {

QString defaultChapterTitle(int n)
{
    return QStringLiteral("Chapter %1").arg(n);
}

} // namespace

QString formatClockFixed(qint64 us)
{
    if (us < 0)
        us = 0;
    const qint64 totalMs = (us + kUsPerMs / 2) / kUsPerMs; // round to the nearest millisecond
    const qint64 ms = totalMs % 1000;
    const qint64 totalSec = totalMs / 1000;
    const qint64 s = totalSec % 60;
    const qint64 m = (totalSec / 60) % 60;
    const qint64 h = totalSec / 3600;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}

QString formatClockCompact(qint64 us)
{
    if (us < 0)
        us = 0;
    const qint64 totalMs = (us + kUsPerMs / 2) / kUsPerMs;
    const qint64 ms = totalMs % 1000;
    const qint64 totalSec = totalMs / 1000;
    const qint64 s = totalSec % 60;
    const qint64 m = (totalSec / 60) % 60;
    const qint64 h = totalSec / 3600;
    QString base = (h > 0)
        ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
    if (ms != 0)
        base += QStringLiteral(".%1").arg(ms, 3, 10, QLatin1Char('0'));
    return base;
}

std::optional<qint64> parseTimestamp(const QString& raw)
{
    QString s = raw.trimmed();
    if (s.isEmpty())
        return std::nullopt;
    s.replace(QChar(0xFF1A), QLatin1Char(':')); // ： -> :
    s.replace(QChar(0xFF0C), QLatin1Char('.')); // ， -> .

    const QStringList segs = s.split(QLatin1Char(':'));
    if (segs.size() > 3)
        return std::nullopt;

    static constexpr qint64 unitSeconds[3] = {3600, 60, 1};
    qint64 totalUs = 0;

    for (int i = 0; i < segs.size(); ++i) {
        const bool isLast = (i == segs.size() - 1);
        const QString seg = segs.at(i).trimmed();
        const qint64 secMul = unitSeconds[3 - segs.size() + i];

        if (isLast) {
            // the last segment may carry decimals
            const int dot = seg.indexOf(QLatin1Char('.'));
            const QString intPart = (dot >= 0) ? seg.left(dot) : seg;
            const QString fracPart = (dot >= 0) ? seg.mid(dot + 1) : QString();

            if (intPart.isEmpty())
                return std::nullopt;
            for (const QChar &c : intPart)
                if (!c.isDigit())
                    return std::nullopt;

            qint64 us = intPart.toLongLong() * secMul * kUsPerSecond;
            if (!fracPart.isEmpty()) {
                if (fracPart.size() > 3)
                    return std::nullopt;
                int frac = 0;
                for (const QChar &c : fracPart) {
                    if (!c.isDigit())
                        return std::nullopt;
                    frac = frac * 10 + c.digitValue();
                }
                for (int pad = fracPart.size(); pad < 3; ++pad)
                    frac *= 10;
                us += qint64(frac) * kUsPerMs;
            }
            totalUs += us;
        } else {
            if (seg.isEmpty())
                return std::nullopt;
            for (const QChar &c : seg)
                if (!c.isDigit())
                    return std::nullopt;
            totalUs += seg.toLongLong() * secMul * kUsPerSecond;
        }
    }
    return totalUs;
}

TimestampParseResult parseTimestampText(const QString& text)
{
    // Line structure: [bullet] [opening bracket] timestamp [closing bracket] [separator decoration] title
    static const QRegularExpression lineRe(QStringLiteral(
        "^\\s*"
        "(?:[-\x{2013}\x{2014}*\x{2022}\x{00b7}>\\x{2192}]+\\s*)?"
        "[\\[\\(\x{ff08}\x{3010}]?\\s*"
        "(?<stamp>\\d{1,3}:[0-5]?\\d(?::[0-5]?\\d)?(?:[.,]\\d{1,3})?)"
        "\\s*[\\]\\)\x{ff09}\x{3011}]?\\s*"
        "(?:[-\x{2013}\x{2014}:~>|\x{ff5e}.)\\]]+\\s*)?"
        "(?<title>.*)$"));

    TimestampParseResult result;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        line.replace(QChar(0xFF1A), QLatin1Char(':')); // normalize full-width colons
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        ++result.lineCount;

        const QRegularExpressionMatch m = lineRe.match(trimmed);
        if (!m.hasMatch()) {
            ++result.skippedLines;
            if (result.warnings.size() < 20)
                result.warnings << QCoreApplication::translate("TimeUtils", "Line %1: unrecognized timestamp: %2").arg(i + 1).arg(trimmed.left(40));
            continue;
        }

        const auto stampUs = parseTimestamp(m.captured(QStringLiteral("stamp")));
        if (!stampUs) {
            ++result.skippedLines;
            continue;
        }

        Chapter ch;
        ch.startUs = *stampUs;
        ch.title = m.captured(QStringLiteral("title")).trimmed();
        if (ch.title.isEmpty())
            ch.title = defaultChapterTitle(result.chapters.size() + 1);
        result.chapters.append(ch);
    }

    std::stable_sort(result.chapters.begin(), result.chapters.end(),
                     [](const Chapter &a, const Chapter &b) { return a.startUs < b.startUs; });
    return result;
}

QString toYouTubeText(const QVector<Chapter>& chapters)
{
    QStringList lines;
    lines.reserve(chapters.size());
    for (int i = 0; i < chapters.size(); ++i) {
        const Chapter &ch = chapters.at(i);
        QString title = ch.title.trimmed();
        if (title.isEmpty())
            title = defaultChapterTitle(i + 1);
        // YouTube timestamps only support whole seconds: always round to the nearest second
        // so a single off-grid chapter does not stand out with a ".mmm" fraction.
        const qint64 startSec = (ch.startUs + kUsPerSecond / 2) / kUsPerSecond;
        lines << formatClockCompact(startSec * kUsPerSecond) + QLatin1Char(' ') + title;
    }
    return lines.join(QLatin1Char('\n'));
}

QStringList youTubeRuleWarnings(const QVector<Chapter>& chapters)
{
    QStringList w;
    if (chapters.isEmpty()) {
        w << QCoreApplication::translate("TimeUtils", "No chapters yet.");
        return w;
    }
    if (chapters.size() < 3)
        w << QCoreApplication::translate("TimeUtils", "YouTube only shows the chapter list with at least 3 chapters (currently %1).").arg(chapters.size());
    if (chapters.first().startUs != 0)
        w << QCoreApplication::translate("TimeUtils", "YouTube requires the first chapter to start at 0:00.");
    for (int i = 1; i < chapters.size(); ++i) {
        if (chapters.at(i).startUs <= chapters.at(i - 1).startUs) {
            w << QCoreApplication::translate("TimeUtils", "Chapter times must strictly increase (not increasing from item %1).").arg(i + 1);
            break;
        }
    }
    return w;
}

} // namespace pcm::time
