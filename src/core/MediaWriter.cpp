#include "MediaWriter.h"

#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#include "FfmpegGuards.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

#include <cmath>

namespace pcm {

namespace {

bool isWritableContainer(const char* ofmtName)
{
    const QString name = QString::fromUtf8(ofmtName);
    return name == QLatin1String("matroska") || name == QLatin1String("webm")
        || name == QLatin1String("mp4") || name == QLatin1String("mov")
        || name == QLatin1String("ipod") // .m4a / .m4b
        || name == QLatin1String("mp3");
}

QString avErrorString(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// remux: copy all streams + inject chapters, output to tmpPath. Returns an error message (empty = success).
QString remuxToTemp(const QString& srcPath, const QVector<Chapter>& chapters,
                    qint64 durationUs, const QString& tmpPath,
                    const std::atomic_bool& cancelled,
                    const std::function<void(int)>& progress)
{
    AVFormatContext* in = nullptr;
    ff::FormatContextGuard inGuard(&in);
    int ret = avformat_open_input(&in, srcPath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0)
        return QObject::tr("Cannot open source file: %1").arg(avErrorString(ret));
    // find_stream_info failing is not fatal (chapters/stream params are usually readable anyway)
    avformat_find_stream_info(in, nullptr);

    // No-reencode precondition: the target container must accept the source codecs.
    // The most common violation is "any audio -> MP3": the MP3 container only takes MP3 audio; fail with a clear error early.
    if (QFileInfo(srcPath).suffix().compare(QLatin1String("mp3"), Qt::CaseInsensitive) != 0
        && QFileInfo(tmpPath).suffix().compare(QLatin1String("mp3"), Qt::CaseInsensitive) == 0) {
        for (unsigned i = 0; i < in->nb_streams; ++i) {
            const AVCodecParameters* par = in->streams[i]->codecpar;
            if (par->codec_type == AVMEDIA_TYPE_AUDIO
                && par->codec_id != AV_CODEC_ID_MP3) {
                return QObject::tr("The MP3 container only holds MP3 audio (current codec: %1; "
                                   "cannot convert without re-encoding). Use MKV / MKA / M4A instead.")
                    .arg(QString::fromUtf8(avcodec_get_name(par->codec_id)));
            }
        }
    }

    AVFormatContext* out = nullptr;
    ret = avformat_alloc_output_context2(&out, nullptr, nullptr, tmpPath.toUtf8().constData());
    if (ret < 0 || !out)
        return QObject::tr("Cannot create output container context: %1").arg(avErrorString(ret));

    // Output context cleanup (including a possibly open avio handle)
    auto cleanupOut = [&out]() {
        if (!out)
            return;
        if (out->pb)
            avio_closep(&out->pb);
        avformat_free_context(out);
        out = nullptr;
    };
    auto fail = [&](const QString& msg) {
        cleanupOut();
        return msg;
    };

    if (!isWritableContainer(out->oformat->name))
        return fail(QObject::tr("Container %1 does not support writing chapters").arg(QString::fromUtf8(out->oformat->name)));

    // Container-level tags (equivalent of -map_metadata 0)
    av_dict_copy(&out->metadata, in->metadata, 0);

    // Per-stream copy of params / metadata / disposition (equivalent of -map 0 -c copy)
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        const AVStream* is = in->streams[i];
        AVStream* os = avformat_new_stream(out, nullptr);
        if (!os)
            return fail(QObject::tr("Failed to create output stream"));
        if (avcodec_parameters_copy(os->codecpar, is->codecpar) < 0)
            return fail(QObject::tr("Failed to copy stream parameters"));
        os->codecpar->codec_tag = 0;
        av_dict_copy(&os->metadata, is->metadata, 0);
        os->disposition = is->disposition;
    }

    // Chapters: millisecond timebase; explicit ends kept (anything beyond the container's ability is silently truncated by the container; the UI warned upfront)
    for (int i = 0; i < chapters.size(); ++i) {
        const Chapter& ch = chapters.at(i);
        const qint64 autoEnd = (i + 1 < chapters.size()) ? chapters.at(i + 1).startUs : durationUs;
        const qint64 startUs = qMax<qint64>(0, ch.startUs);
        qint64 endUs = ch.endUs.value_or(autoEnd);
        if (endUs <= startUs)
            endUs = startUs + 1000; // at least 1ms to avoid an invalid interval

        AVChapter* c = static_cast<AVChapter*>(av_mallocz(sizeof(AVChapter)));
        if (!c)
            return fail(QObject::tr("Memory allocation failed"));
        c->time_base = AVRational{1, 1000};
        c->start = (startUs + 500) / 1000;
        c->end = (endUs + 500) / 1000;
        c->id = i;
        av_dict_set(&c->metadata, "title", ch.title.toUtf8().constData(), 0);
        int nb = int(out->nb_chapters);
        av_dynarray_add(&out->chapters, &nb, c);
        out->nb_chapters = unsigned(nb);
    }

    // MP3 uses ID3v2.4 (CHAP/CTOC are most complete under v2.4)
    AVDictionary* muxOpts = nullptr;
    if (QLatin1String(out->oformat->name) == QLatin1String("mp3"))
        av_dict_set(&muxOpts, "id3v2_version", "4", 0);

    if (!(out->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out->pb, tmpPath.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_dict_free(&muxOpts);
            return fail(QObject::tr("Cannot create temporary file: %1").arg(avErrorString(ret)));
        }
    }

    ret = avformat_write_header(out, &muxOpts);
    av_dict_free(&muxOpts);
    if (ret < 0)
        return fail(QObject::tr("Failed to write header: %1").arg(avErrorString(ret)));

    const qint64 inSize = qMax<qint64>(1, QFileInfo(srcPath).size());
    int lastPct = -1;

    AVPacket* pkt = av_packet_alloc();
    ff::AllocGuard<AVPacket> pktGuard(&pkt);

    while (true) {
        if (cancelled.load()) {
            cleanupOut();
            return QObject::tr("Canceled");
        }
        ret = av_read_frame(in, pkt);
        if (ret < 0)
            break; // EOF or read error: stop copying (EOF is normal)

        AVStream* is = in->streams[pkt->stream_index];
        AVStream* os = out->streams[pkt->stream_index];
        av_packet_rescale_ts(pkt, is->time_base, os->time_base);
        pkt->pos = -1;
        // av_interleaved_write_frame takes over the packet reference on success or failure; no unref needed
        ret = av_interleaved_write_frame(out, pkt);
        if (ret < 0)
            return fail(QObject::tr("Failed to write data: %1").arg(avErrorString(ret)));

        if (progress && out->pb) {
            const int pct = int(avio_tell(out->pb) * 100 / inSize);
            if (pct != lastPct) {
                lastPct = pct;
                progress(qMin(99, pct));
            }
        }
    }

    if (av_write_trailer(out) < 0)
        return fail(QObject::tr("Failed to write trailer"));

    cleanupOut();
    if (progress)
        progress(100);
    return {};
}

// Post-write integrity check: opens, streams match, chapter count matches, duration close
QString validateOutput(const QString& tmpPath, const QString& srcPath, int expectedChapters)
{
    AVFormatContext* src = nullptr;
    ff::FormatContextGuard srcGuard(&src);
    if (avformat_open_input(&src, srcPath.toUtf8().constData(), nullptr, nullptr) < 0)
        return QObject::tr("Validation failed: cannot reopen source file");

    AVFormatContext* out = nullptr;
    ff::FormatContextGuard outGuard(&out);
    if (avformat_open_input(&out, tmpPath.toUtf8().constData(), nullptr, nullptr) < 0)
        return QObject::tr("Validation failed: cannot open output file");

    // mov-family muxers automatically attach a chapter text track (data stream)
    // when writing chapters,
    // So strictly compare only "media streams" (audio/video/subtitle) and ignore the attached data stream.
    auto mediaCount = [](AVFormatContext* c) {
        int n = 0;
        for (unsigned i = 0; i < c->nb_streams; ++i) {
            if (c->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_DATA)
                ++n;
        }
        return n;
    };
    if (mediaCount(out) != mediaCount(src))
        return QObject::tr("Validation failed: stream count mismatch (source %1 -> new %2)")
                    .arg(mediaCount(src)).arg(mediaCount(out));
    const unsigned common = qMin<quint64>(src->nb_streams, out->nb_streams);
    for (unsigned i = 0; i < common; ++i) {
        if (out->streams[i]->codecpar->codec_type != src->streams[i]->codecpar->codec_type)
            return QObject::tr("Validation failed: stream #%1 has a different type").arg(i + 1);
    }
    if (out->nb_chapters != expectedChapters)
        return QObject::tr("Validation failed: chapter count mismatch (expected %1, got %2)")
                    .arg(expectedChapters).arg(out->nb_chapters);
    if (src->duration > 0 && out->duration > 0) {
        const qint64 tol = qMax<qint64>(2000000, src->duration / 50); // 2s or 2%
        if (qAbs(src->duration - out->duration) > tol)
            return QObject::tr("Validation failed: output duration deviates too much");
    }
    return {};
}

} // namespace

SaveCapability saveCapabilityFor(const QString& targetPath)
{
    const QString ext = QFileInfo(targetPath).suffix().toLower();
    SaveCapability cap;
    cap.containerDescription = ext.toUpper();
    if (ext == QLatin1String("mkv") || ext == QLatin1String("mka") || ext == QLatin1String("webm")) {
        cap.canWriteChapters = true;
        cap.preservesGaps = true;
    } else if (ext == QLatin1String("mp4") || ext == QLatin1String("m4a")
               || ext == QLatin1String("m4b") || ext == QLatin1String("mov")) {
        cap.canWriteChapters = true;
        cap.preservesGaps = false; // QT chapter track / chpl have no explicit ends
    } else if (ext == QLatin1String("mp3")) {
        cap.canWriteChapters = true;
        cap.preservesGaps = true; // ID3v2 CHAP carries start/end
    }
    return cap;
}

QVector<int> chaptersLosingEnds(const QVector<Chapter>& chapters, qint64 durationUs,
                                const SaveCapability& cap)
{
    QVector<int> result;
    if (cap.preservesGaps || !cap.canWriteChapters)
        return result;
    for (int i = 0; i < chapters.size(); ++i) {
        const Chapter& ch = chapters.at(i);
        if (!ch.endUs)
            continue;
        const qint64 autoEnd = (i + 1 < chapters.size()) ? chapters.at(i + 1).startUs : durationUs;
        if (*ch.endUs != autoEnd)
            result.append(i);
    }
    return result;
}

SaveResult executeSave(const SaveRequest& request, const std::atomic_bool& cancelled,
                       const std::function<void(int)>& progress)
{
    SaveResult result;
    const QFileInfo targetInfo(request.targetPath);
    const QString ext = targetInfo.suffix();
    if (ext.isEmpty()) {
        result.message = QObject::tr("Target file has no extension; cannot determine the container format");
        return result;
    }
    if (!saveCapabilityFor(request.targetPath).canWriteChapters) {
        result.message = QObject::tr("Target container does not support writing chapters");
        return result;
    }

    const QString tmpPath = QString("%1/%2.pcm-tmp-%3.%4")
                                .arg(targetInfo.absolutePath(), targetInfo.completeBaseName())
                                .arg(uint(QRandomGenerator::global()->generate() & 0xFFFFFF), 6, 16, QChar('0'))
                                .arg(ext);

    QString err = remuxToTemp(request.sourcePath, request.chapters, request.durationUs,
                              tmpPath, cancelled, progress);
    if (err.isEmpty() && !cancelled.load())
        err = validateOutput(tmpPath, request.sourcePath, request.chapters.size());

    if (!err.isEmpty() || cancelled.load()) {
        QFile::remove(tmpPath);
        result.message = cancelled.load() ? QObject::tr("Canceled") : err;
        return result;
    }

    // Replace the target file (backup -> rename -> roll back on failure)
    QString backup;
    if (QFileInfo::exists(request.targetPath)) {
        if (request.makeBackup) {
            backup = request.targetPath + QStringLiteral(".bak");
            QFile::remove(backup);
            if (!QFile::rename(request.targetPath, backup)) {
                QFile::remove(tmpPath);
                result.message = QObject::tr("Cannot back up the original file (it may be locked by another program); the original file was left unchanged.");
                return result;
            }
        } else {
            QFile::remove(request.targetPath);
        }
    }
    if (!QFile::rename(tmpPath, request.targetPath)) {
        if (!backup.isEmpty()) {
            QFile::rename(backup, request.targetPath); // roll back
            result.message = QObject::tr("Failed to replace the target file; the backup was rolled back.");
        } else {
            result.message = QObject::tr("Failed to replace the target file (output kept at %1)").arg(tmpPath);
        }
        return result;
    }

    result.ok = true;
    result.backupPath = backup;
    result.message = backup.isEmpty()
        ? QObject::tr("Saved to %1").arg(targetInfo.fileName())
        : QObject::tr("Saved; the original file was backed up as %1").arg(QFileInfo(backup).fileName());
    return result;
}

SaveWorker::SaveWorker(SaveRequest request, QObject* parent)
    : QThread(parent), m_request(std::move(request))
{
}

SaveWorker::~SaveWorker() = default;

void SaveWorker::run()
{
    const SaveResult result = executeSave(m_request, m_cancelled, [this](int percent) {
        emit progressChanged(percent);
    });
    emit saveFinished(result.ok, result.message, result.backupPath);
}

} // namespace pcm
