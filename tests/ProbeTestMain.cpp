// probe-test: prints probeMedia() results on the console for headless regression checks.
// usage: probe-test <media file> [...]
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>

#include "core/MediaProbe.h"
#include "core/TimeUtils.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        out << "usage: probe-test <media file> [...]\n";
        return 2;
    }

    for (int a = 1; a < args.size(); ++a) {
        const QString path = args.at(a);
        out << "== " << path << " ==\n";
        const pcm::ProbeResult r = pcm::probeMedia(path);
        if (!r.ok) {
            out << "ERROR: " << r.errorString << "\n";
            continue;
        }
        out << "format  : " << r.formatName << " / " << r.formatLongName << "\n";
        out << "duration: " << pcm::time::formatClockFixed(r.durationUs) << "\n";
        out << "streams :\n";
        for (const pcm::StreamInfo &s : r.streams) {
            out << "  #" << s.index << " " << s.typeName << " " << s.codecName
                << (s.isCover ? QStringLiteral(" [cover]") : QString())
                << (s.details.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(s.details))
                << "\n";
        }
        out << "chapters: " << r.chapters.size()
            << (r.hasChapters ? "" : QStringLiteral(" (container has none)")) << "\n";
        for (int i = 0; i < r.chapters.size(); ++i) {
            const pcm::Chapter &ch = r.chapters.at(i);
            qint64 endUs;
            const char *endKind;
            if (ch.endUs) {
                endUs = *ch.endUs;
                endKind = "explicit";
            } else {
                // mirrors ChapterDocument's auto rule
                endUs = (i + 1 < r.chapters.size()) ? r.chapters.at(i + 1).startUs
                                                    : (r.durationUs > 0 ? r.durationUs : ch.startUs);
                endKind = "auto";
            }
            out << "  " << (i + 1) << "  start=" << pcm::time::formatClockFixed(ch.startUs)
                << "  end=" << pcm::time::formatClockFixed(endUs) << " (" << endKind << ")"
                << "  title=\"" << ch.title << "\"\n";
        }
    }
    return 0;
}
