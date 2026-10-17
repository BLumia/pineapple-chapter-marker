// save-test: headless end-to-end test of the save pipeline.
// usage: save-test <src> <target> <segments> [--backup]
//   segments "0-10,15-20,20-30": seconds; start-end marks an explicit end (gaps possible)
//   segments "0,30,60"      : starts only (end times auto)
//   --backup                : back up an existing target as *.bak
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>
#include <atomic>

#include "core/MediaWriter.h"
#include "core/TimeUtils.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QStringList args = app.arguments();
    if (args.size() < 4) {
        out << "usage: save-test <src> <target> <segments>\n"
            << "  segments: \"0-10,15-20,20-30\" (start-end, seconds) or \"0,30,60\" (starts only)\n";
        return 2;
    }

    const QString src = args.at(1);
    const QString target = args.at(2);

    QVector<pcm::Chapter> chapters;
    int index = 1;
    for (const QString& part : args.at(3).split(QLatin1Char(','))) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty())
            continue;
        pcm::Chapter ch;
        const int dash = trimmed.indexOf(QLatin1Char('-'));
        const QString startStr = (dash >= 0) ? trimmed.left(dash) : trimmed;
        const auto startUs = pcm::time::parseTimestamp(startStr);
        if (!startUs) {
            out << "bad segment: " << trimmed << "\n";
            return 2;
        }
        ch.startUs = *startUs;
        if (dash >= 0) {
            const auto endUs = pcm::time::parseTimestamp(trimmed.mid(dash + 1));
            if (!endUs) {
                out << "bad segment end: " << trimmed << "\n";
                return 2;
            }
            ch.endUs = *endUs;
        }
        ch.title = QStringLiteral("Chapter %1").arg(index++);
        chapters.append(ch);
    }

    pcm::SaveRequest req;
    req.sourcePath = src;
    req.targetPath = target;
    req.chapters = chapters;
    req.durationUs = 0;
    req.makeBackup = args.contains(QLatin1String("--backup"));

    std::atomic_bool cancelled{false};
    const pcm::SaveResult res = pcm::executeSave(req, cancelled, {});
    out << (res.ok ? "OK  : " : "FAIL: ") << res.message << "\n";
    if (!res.backupPath.isEmpty())
        out << "backup: " << res.backupPath << "\n";
    return res.ok ? 0 : 1;
}
